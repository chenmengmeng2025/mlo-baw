#include "ampdu-limit-controller.h"

#include "ns3/abort.h"
#include "ns3/assert.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/wifi-mac.h"
#include "ns3/wifi-ppdu.h"
#include "ns3/wifi-psdu.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("AmpduLimitController");
NS_OBJECT_ENSURE_REGISTERED(AmpduLimitController);

AmpduLimitController::AmpduLimitController(Ptr<WifiMac> mac)
    : m_mac(mac)
{
    NS_ABORT_MSG_IF(!m_mac, "AmpduLimitController requires a valid WifiMac");
    const auto nLinks = m_mac->GetNLinks();
    m_ampduLimits.assign(nLinks, std::numeric_limits<uint32_t>::max());
    m_frameRates.assign(nLinks, 0.0);
    m_interPpduGaps.resize(nLinks);
    m_lastLoggedLimits.assign(nLinks, -1);
    m_gapCounts.assign(nLinks, 0);
    m_nextGapIndices.assign(nLinks, 0);
    m_gapSums.assign(nLinks, 0.0);
    m_decisionSources.assign(nLinks, "uninitialized");
    m_lastLoggedDecisionSources.resize(nLinks);
    m_ppduTimeWindow.assign(nLinks, {0.0, 0.0});
}

AmpduLimitController::~AmpduLimitController()
{
}

TypeId
AmpduLimitController::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::AmpduLimitController").SetParent<Object>().SetGroupName("Wifi");
    return tid;
}

void
AmpduLimitController::NotifyPpduTxDuration(Ptr<const WifiPpdu> ppdu, Time duration, uint8_t linkId)
{
    NS_ASSERT_MSG(linkId < m_ppduTimeWindow.size(),
                  "Invalid link ID " << +linkId << " for " << m_ppduTimeWindow.size()
                                     << " PPDU timing windows");

    // Only QoS-data PPDUs mark the start of a real transmission window.
    if (!ppdu->GetPsdu()->GetHeader(0).IsQosData())
    {
        return;
    }

    double currentTime = Simulator::Now().GetMicroSeconds();
    double endTime = currentTime + duration.GetMicroSeconds();

    auto& slot = m_ppduTimeWindow[linkId]; // [start, end]

    if (slot[1] == 0.0)
    {
        // First transmission on this link: just record the window, do not
        // update the inter-PPDU gap statistics yet (there is no previous
        // window to compare against).
        slot = {currentTime, endTime};
        return;
    }

    // Gap between the end of the previous transmission and the start of
    // this one.
    double gap = currentTime - slot[1];
    if (gap > 0.0)
    {
        auto& samples = m_interPpduGaps[linkId];
        auto& count = m_gapCounts[linkId];
        auto& next = m_nextGapIndices[linkId];
        auto& sum = m_gapSums[linkId];

        if (count == GAP_HISTORY_SIZE)
        {
            sum -= samples[next];
        }
        else
        {
            ++count;
        }

        samples[next] = gap;
        sum += gap;
        next = (next + 1) % GAP_HISTORY_SIZE;
    }

    // Record this transmission's [start, end] window.
    slot = {currentTime, endTime};
}

bool
AmpduLimitController::IsWithinOtherLinkPpduWindow(uint8_t linkId, double extraTime) const
{
    NS_ASSERT_MSG(m_mac->GetNLinks() == 2,
                  "IsWithinOtherLinkPpduWindow is only meaningful for 2-link MLO.");

    uint8_t otherLinkId = 1 - linkId;
    double now = Simulator::Now().GetMicroSeconds();

    return m_ppduTimeWindow[otherLinkId][0] < now &&
           now < (m_ppduTimeWindow[otherLinkId][1] + extraTime);
}

uint32_t
AmpduLimitController::GetAmpduLimit(uint8_t linkId,
                                    uint32_t policy,
                                    double bawSize,
                                    std::string_view bawSource,
                                    bool logFlag)
{
    uint32_t ampduLimitRes = 0;
    const uint32_t kUnlimited = std::numeric_limits<uint32_t>::max();
    std::string decisionSource;

    if (m_mac->GetNLinks() == 2)
    {
        switch (policy)
        {
        case 1: // greedy: both links unlimited
            ampduLimitRes = kUnlimited;
            decisionSource = "greedy_unlimited";
            break;

        case 2: // damla: dynamically computed limit (2-link MLO only)
        {
            NS_ASSERT_MSG(m_frameRates.size() >= 2 && m_frameRates[0] > 0.0 &&
                              m_frameRates[1] > 0.0,
                          "Data rate not available for both links.");

            // Timing observations are unreliable during startup. Use a balanced BAW split
            // to avoid unstable DAMLA predictions until the initial phase has completed.
            if (Simulator::Now().GetSeconds() < 1.1)
            {
                ampduLimitRes = static_cast<uint32_t>(std::ceil(bawSize / 2.0));
                decisionSource =
                    "damla_" + std::string(bawSource) + "_balanced_bootstrap";
                break;
            }

            const auto getMeanGap = [this](uint8_t id) {
                return m_gapCounts[id] == 0
                           ? 0.0
                           : m_gapSums[id] / static_cast<double>(m_gapCounts[id]);
            };
            const auto getCycleOverhead = [this, &getMeanGap](uint8_t id) {
                constexpr double PHY_OVERHEAD_US = 56.0;
                return m_gapCounts[id] == 0 ? 0.0 : getMeanGap(id) + PHY_OVERHEAD_US;
            };

            const uint8_t otherLinkId = 1 - linkId;
            const double otherMeanGap = getMeanGap(otherLinkId);
            const double thisCycleOverhead = getCycleOverhead(linkId);
            const double otherCycleOverhead = getCycleOverhead(otherLinkId);
            // Predict the next other-link PPDU start from the end of its last
            // complete PPDU plus the observed inter-PPDU gap. PHY overhead is
            // already included in that PPDU end and must not be added again.
            const double T_i2u = m_ppduTimeWindow[otherLinkId][1] + otherMeanGap -
                                 Simulator::Now().GetMicroSeconds();

            if (T_i2u > 0)
            {
                const double rate0 = m_frameRates[0];
                const double rate1 = m_frameRates[1];
                const double thisRate = m_frameRates[linkId];
                const double rateSquares = rate0 * rate0 + rate1 * rate1;

                const double T_u2i =
                    (bawSize * thisRate + thisCycleOverhead * rateSquares -
                     otherCycleOverhead * thisRate * thisRate) /
                    (rateSquares + rate0 * rate1);

                ampduLimitRes = static_cast<uint32_t>(
                    std::ceil((T_i2u + std::max(0.0, T_u2i - thisCycleOverhead)) *
                              thisRate));
                decisionSource =
                    "damla_" + std::string(bawSource) + "_adaptive_mean_gap";
            }
            else
            {
                ampduLimitRes = kUnlimited;
                decisionSource =
                    "damla_" + std::string(bawSource) + "_prediction_expired_unlimited";
            }
            break;
        }

        case 3: // only2G: link 0 unlimited, link 1 disabled
            ampduLimitRes = linkId ? 0 : kUnlimited;
            decisionSource = "only2g";
            break;

        case 4: // only5G: link 0 disabled, link 1 unlimited
            ampduLimitRes = linkId ? kUnlimited : 0;
            decisionSource = "only5g";
            break;

        case 6: // bothset: use the fixed per-link limits
            ampduLimitRes = m_ampduLimits[linkId];
            decisionSource = "fixed_limit";
            break;

        default:
            NS_FATAL_ERROR("Unsupported policy in 2-link mode: " << policy);
        }
    }
    else if (m_mac->GetNLinks() == 3)
    {
        switch (policy)
        {
        case 1: // greedy: all three links unlimited
            ampduLimitRes = kUnlimited;
            decisionSource = "greedy_unlimited";
            break;

        case 3: // only2G: link 0 only, links 1/2 disabled
            ampduLimitRes = (linkId == 0) ? kUnlimited : 0;
            decisionSource = "only2g";
            break;

        case 4: // only5G: link 1 only, links 0/2 disabled
            ampduLimitRes = (linkId == 1) ? kUnlimited : 0;
            decisionSource = "only5g";
            break;

        case 5: // only6G: link 2 only, links 0/1 disabled
            ampduLimitRes = (linkId == 2) ? kUnlimited : 0;
            decisionSource = "only6g";
            break;

        case 6: // allset: use the fixed per-link limits
            ampduLimitRes = m_ampduLimits[linkId];
            decisionSource = "fixed_limit";
            break;

        default:
            NS_FATAL_ERROR("Unsupported policy in 3-link mode: " << policy);
        }
    }
    else
    {
        NS_FATAL_ERROR("Unsupported number of links: " << m_mac->GetNLinks());
    }

    m_decisionSources[linkId] = decisionSource;
    if (logFlag &&
        (m_lastLoggedLimits[linkId] != static_cast<int64_t>(ampduLimitRes) ||
         m_lastLoggedDecisionSources[linkId] != decisionSource))
    {
        std::cout << "[AMPDU_LIMIT]"
                  << " timeNs=" << Simulator::Now().GetNanoSeconds()
                  << " link=" << +linkId
                  << " value=" << ampduLimitRes
                  << " source=" << decisionSource << std::endl;
        m_lastLoggedLimits[linkId] = ampduLimitRes;
        m_lastLoggedDecisionSources[linkId] = decisionSource;
    }

    return ampduLimitRes;
}

const std::string&
AmpduLimitController::GetLastDecisionSource(uint8_t linkId) const
{
    return m_decisionSources.at(linkId);
}

void
AmpduLimitController::SetAmpduLimit(int limit0, int limit1, int limit2)
{
    std::vector<int> inputLimits = {limit0, limit1, limit2};
    for (std::size_t i = 0; i < m_ampduLimits.size() && i < inputLimits.size(); ++i)
    {
        m_ampduLimits[i] = inputLimits[i];
    }
}

void
AmpduLimitController::SetDatarateSetting(uint32_t datarate0, uint32_t datarate1, uint32_t datarate2)
{
    constexpr double SUBFRAME_SIZE_BITS = 1572.0 * 8.0;
    const std::array<uint32_t, 3> inputRates = {datarate0, datarate1, datarate2};
    for (std::size_t i = 0; i < m_frameRates.size() && i < inputRates.size(); ++i)
    {
        m_frameRates[i] = static_cast<double>(inputRates[i]) / SUBFRAME_SIZE_BITS / 1e6;
    }
}

} // namespace ns3