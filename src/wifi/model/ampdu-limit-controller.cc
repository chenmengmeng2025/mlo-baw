#include "ampdu-limit-controller.h"

#include "ns3/ampdu-subframe-header.h"
#include "ns3/frame-exchange-manager.h"
#include "ns3/msdu-aggregator.h"
#include "ns3/wifi-tx-vector.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <ostream>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("AmpduLimitController");
NS_OBJECT_ENSURE_REGISTERED(AmpduLimitController);

AmpduLimitController::AmpduLimitController()
    : AmpduLimitController(nullptr)
{
}

AmpduLimitController::AmpduLimitController(Ptr<WifiMac> mac)
    : m_mac(mac)
{
    m_ampduLimits.assign(m_mac->GetNLinks(), std::numeric_limits<uint32_t>::max());
    m_datarateSetting.assign(m_mac->GetNLinks(), 0);
    m_interPpduGaps.assign(m_mac->GetNLinks(), std::vector<double>());
    m_ppduTimeWindow.assign(m_mac->GetNLinks(), {0.0, 0.0});
}

AmpduLimitController::~AmpduLimitController()
{
}

TypeId
AmpduLimitController::GetTypeId()
{
    static TypeId tid = TypeId("ns3::AmpduLimitController")
                             .SetParent<Object>()
                             .SetGroupName("Wifi")
                             .AddConstructor<AmpduLimitController>();
    return tid;
}

void
AmpduLimitController::NotifyPpduTxDuration(Ptr<const WifiPpdu> ppdu, Time duration, uint8_t linkId)
{
    // Only QoS-data PPDUs mark the start of a "real" transmission window;
    // control/management frames (RTS/CTS/ACK/BlockAck) are ignored.
    if (!ppdu->GetPsdu()->GetHeader(0).IsQosData())
    {
        return;
    }

    const auto& hdr = ppdu->GetPsdu()->GetHeader(0);
    if (hdr.IsRts() || hdr.IsCts() || hdr.IsAck() || hdr.IsBlockAck())
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
        if (m_interPpduGaps[linkId].size() >= 10)
        {
            m_interPpduGaps[linkId].erase(m_interPpduGaps[linkId].begin());
        }
        m_interPpduGaps[linkId].push_back(gap);
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
AmpduLimitController::GetAmpduLimit(uint8_t linkId, uint32_t policy, uint32_t bawSize, bool logFlag)
{
    uint32_t ampduLimitRes = 0;
    const uint32_t kUnlimited = std::numeric_limits<uint32_t>::max();

    if (m_mac->GetNLinks() == 2)
    {
        switch (policy)
        {
        case 1: // greedy: both links unlimited
            ampduLimitRes = kUnlimited;
            break;

        case 2: // damla: dynamically computed limit (2-link MLO only)
        {
            NS_ASSERT_MSG(m_datarateSetting.size() >= 2 && m_datarateSetting[0] != 0 &&
                              m_datarateSetting[1] != 0,
                          "Data rate not available for both links.");

            // Average inter-PPDU gap per link, plus a fixed PHY overhead
            // T_PH, used as the "service time" t_i in the DAMLA model.
            double T_PH = 56.0;
            std::vector<double> t;
            for (const auto& gaps : m_interPpduGaps)
            {
                if (gaps.empty())
                {
                    t.push_back(0);
                }
                else
                {
                    double sum = 0.0;
                    for (double value : gaps)
                    {
                        sum += value;
                    }
                    t.push_back(sum / gaps.size() + T_PH);
                }
            }

            // Time remaining until the other link's current transmission
            // (plus its estimated service time) is expected to finish.
            double T_i2u = m_ppduTimeWindow[1 - linkId][1] + t[1 - linkId] -
                           Simulator::Now().GetMicroSeconds();

            if (T_i2u > 0)
            {
                // Per-link frame rate (subframes per microsecond), derived
                // from the assumed data rate and a fixed subframe size.
                std::vector<double> R_f;
                double L_subf = 1572 * 8;
                for (double r : m_datarateSetting)
                {
                    R_f.push_back(r / L_subf);
                }

                double T_u2i = (bawSize * R_f[linkId] +
                                 t[linkId] * (R_f[0] * R_f[0] + R_f[1] * R_f[1]) -
                                 t[1 - linkId] * (R_f[linkId] * R_f[linkId])) /
                               (R_f[0] * R_f[0] + R_f[0] * R_f[1] + R_f[1] * R_f[1]);

                ampduLimitRes = static_cast<uint32_t>(
                    std::ceil((T_i2u + std::max(0.0, T_u2i - t[linkId])) * R_f[linkId]));
            }
            else
            {
                ampduLimitRes = kUnlimited;
            }
            break;
        }

        case 3: // only2G: link 0 unlimited, link 1 disabled
            ampduLimitRes = linkId ? 0 : kUnlimited;
            break;

        case 4: // only5G: link 0 disabled, link 1 unlimited
            ampduLimitRes = linkId ? kUnlimited : 0;
            break;

        case 6: // bothset: use the fixed per-link limits
            ampduLimitRes = m_ampduLimits[linkId];
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
            break;

        case 3: // only2G: link 0 only, links 1/2 disabled
            ampduLimitRes = (linkId == 0) ? kUnlimited : 0;
            break;

        case 4: // only5G: link 1 only, links 0/2 disabled
            ampduLimitRes = (linkId == 1) ? kUnlimited : 0;
            break;

        case 5: // only6G: link 2 only, links 0/1 disabled
            ampduLimitRes = (linkId == 2) ? kUnlimited : 0;
            break;

        case 6: // allset: use the fixed per-link limits
            ampduLimitRes = m_ampduLimits[linkId];
            break;

        default:
            NS_FATAL_ERROR("Unsupported policy in 3-link mode: " << policy);
        }
    }
    else
    {
        NS_FATAL_ERROR("Unsupported number of links: " << m_mac->GetNLinks());
    }

    if (m_lastUpdateTime != Simulator::Now() && logFlag)
    {
        std::cout << "AmpduLimits" << static_cast<uint32_t>(linkId) << " = " << ampduLimitRes
                  << std::endl;
    }
    m_lastUpdateTime = Simulator::Now();

    return ampduLimitRes;
}

void
AmpduLimitController::SetAmpduLimit(int limit0, int limit1, int limit2)
{
    std::vector<int> inputLimits = {limit0, limit1, limit2};
    for (std::size_t i = 0; i < m_ampduLimits.size() && i < inputLimits.size(); ++i)
    {
        m_ampduLimits[i] = inputLimits[i];
        std::cout << "Set AmpduLimit for link " << i << " to " << inputLimits[i] << std::endl;
    }
}

void
AmpduLimitController::SetDatarateSetting(uint32_t datarate0, uint32_t datarate1, uint32_t datarate2)
{
    std::vector<uint32_t> inputRates = {datarate0, datarate1, datarate2};
    for (std::size_t i = 0; i < m_datarateSetting.size() && i < inputRates.size(); ++i)
    {
        m_datarateSetting[i] = inputRates[i];
    }
}

} // namespace ns3