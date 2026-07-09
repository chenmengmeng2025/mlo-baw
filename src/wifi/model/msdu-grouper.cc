#include "ns3/ampdu-subframe-header.h"
#include "ns3/frame-exchange-manager.h"
#include "ns3/msdu-aggregator.h"
#include "ns3/wifi-tx-vector.h"

#include "msdu-grouper.h"
#include <iostream>
#include <ostream>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("MsduGrouper");
NS_OBJECT_ENSURE_REGISTERED(MsduGrouper);

MsduGrouper::MsduGrouper():MsduGrouper(nullptr, 0) {} 

MsduGrouper::MsduGrouper(Ptr<WifiMac> mac, uint32_t mode)
    :m_mac(mac), m_mode(mode)
{
    m_ppduTimeWindow.assign(m_mac->GetNLinks(), {0.0, 0.0});
}

MsduGrouper::~MsduGrouper() {}

TypeId
MsduGrouper::GetTypeId()
{
    static TypeId tid = TypeId("ns3::MsduGrouper")
                            .SetParent<Object>()
                            .SetGroupName("Wifi")
                            .AddConstructor<MsduGrouper>();
    return tid;
}

// PPDU (AMPDU) Tx
void
MsduGrouper::NotifyPpduTxDuration(Ptr<const WifiPpdu> ppdu, Time duration, uint8_t linkId)
// PHY层检测PPDU发送事件
{
    if (!ppdu->GetPsdu()->GetHeader(0).IsQosData())
        return;

    const auto& hdr = ppdu->GetPsdu()->GetHeader(0);
    if (hdr.IsRts() || hdr.IsCts() || hdr.IsAck() || hdr.IsBlockAck())
        return;

    double current_time = Simulator::Now().GetMicroSeconds();
    double end_time     = current_time + duration.GetMicroSeconds();

    auto& slot = m_ppduTimeWindow[linkId]; // [start, end]

    if (slot[1] == 0.0)
    {
        // 第一次：直接记录，不更新 m_t
        slot = {current_time, end_time};
        return;
    }

    // 计算与上一次结束时间的间隔
    double new_t = current_time - slot[1];
    if (new_t > 0.0)
    {
        if (m_t[linkId].size() >= 10)
            m_t[linkId].erase(m_t[linkId].begin());
        m_t[linkId].push_back(new_t);
    }

    // 更新为本次的 [start, end]
    slot = {current_time, end_time};
}

uint8_t 
MsduGrouper::GetMode() {
    return m_mode;
}

uint32_t
MsduGrouper::GetAmpduLimit(uint8_t linkId, uint32_t preTitle, uint32_t bawSize)
{
    uint32_t ampduLimitRes = 0;
    const uint32_t kUnlimited = std::numeric_limits<uint32_t>::max();
    if (m_mac->GetNLinks() == 2)
    {
        switch (preTitle)
        {
            case 1: // greedy: 两条链路均无限
                ampduLimitRes = kUnlimited;
                break;

            case 2: // damla: 动态计算（仅2链路）
            {
                NS_ASSERT_MSG(m_datarate_setting.size() >= 2 &&
                            m_datarate_setting[0] != 0.0 &&
                            m_datarate_setting[1] != 0.0,
                            "Data rate not available for both links.");

                // // 为了正确收敛，前1.05秒内直接返回bawSize/2，避免初始阶段数据不足导致的计算异常（只在PER=0时使用）
                // if(Simulator::Now().GetSeconds() < 1.05) {
                //     std::cout << "start period, m_ampduLimits" << (uint32_t)linkId << " = " << bawSize/2 << std::endl;
                //     ampduLimitRes = static_cast<int>(std::ceil(bawSize/2));
                //     break;
                // }
                double T_PH = 56.0;
                std::vector<double> t;
                for (const auto& vec : m_t) {
                    if (vec.empty()) {
                        t.push_back(0);
                    } else {
                        double sum = 0.0;
                        for (double value : vec) sum += value;
                        t.push_back(sum / vec.size() + T_PH);
                    }
                }

                double T_i2u = m_ppduTimeWindow[1 - linkId][1] + t[1 - linkId]
                            - Simulator::Now().GetMicroSeconds();

                if (T_i2u > 0) {
                    std::vector<double> R_f;
                    double L_subf = 1572 * 8;
                    for (double r : m_datarate_setting) R_f.push_back(r / L_subf);
                    double T_u2i = (bawSize * R_f[linkId]
                                    + t[linkId]     * (R_f[0]*R_f[0] + R_f[1]*R_f[1])
                                    - t[1-linkId]   * (R_f[linkId]*R_f[linkId]))
                                / (R_f[0]*R_f[0] + R_f[0]*R_f[1] + R_f[1]*R_f[1]);

                    ampduLimitRes = static_cast<int>(
                        std::ceil((T_i2u + std::max(0.0, T_u2i - t[linkId])) * R_f[linkId]));

                } else {
                    ampduLimitRes = kUnlimited;
                }

                if (ampduLimitRes < 0){
                    ampduLimitRes = kUnlimited;
                }
                break;
            }
            case 3: // only2G: link0无限，link1禁用
                ampduLimitRes = linkId ? 0 : kUnlimited;
                break;
            case 4: // only5G: link0禁用，link1无限
                ampduLimitRes = linkId ? kUnlimited : 0;
                break;
            case 6: // bothset: 使用各链路预设限制
                ampduLimitRes = m_ampduLimits[linkId];
                break;
            default:
                NS_FATAL_ERROR("Unsupported preTitle in 2-link mode: " << preTitle);
            }
    }
    else if (m_mac->GetNLinks() == 3)
    {
        switch (preTitle)
        {
            case 1: // greedy: 三条链路均无限
                ampduLimitRes = kUnlimited;
                break;
            case 3: // only2G: 仅link0，禁用link1/2
                ampduLimitRes = (linkId == 0) ? kUnlimited : 0;
                break;
            case 4: // only5G: 仅link1，禁用link0/2
                ampduLimitRes = (linkId == 1) ? kUnlimited : 0;
                break;
            case 5: // only6G: 仅link2，禁用link0/1
                ampduLimitRes = (linkId == 2) ? kUnlimited : 0;
                break;
            case 6: // allset: 使用各链路预设限制
                ampduLimitRes = m_ampduLimits[linkId];
                break;
            default:
                NS_FATAL_ERROR("Unsupported preTitle in 3-link mode: " << preTitle);
        }
    }
    else
    {
        NS_FATAL_ERROR("Unsupported number of links: " << m_mac->GetNLinks());
    }

    if (m_lastUpdateTime != Simulator::Now() && m_mode & 1 << 5) 
        std::cout << "AmpduLimits" << (uint32_t)linkId << " = " << ampduLimitRes << std::endl;
    m_lastUpdateTime = Simulator::Now();

    return ampduLimitRes;
}

void 
MsduGrouper::SetAmpduLimit(int limit0, int limit1, int limit2) {
    m_ampduLimits[0] = limit0;
    m_ampduLimits[1] = limit1;
    m_ampduLimits[2] = limit2;
}

}// namespace ns3