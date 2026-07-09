#ifndef MSDU_GROUPER_H
#define MSDU_GROUPER_H

  
#include "ns3/wifi-mpdu.h"
#include "ns3/wifi-psdu.h"
#include "ns3/wifi-ppdu.h"
#include "ns3/wifi-mac-queue.h"
#include "ns3/log.h"
#include "ns3/random-variable-stream.h"
#include "ns3/wifi-mac.h"
#include "ns3/udp-client-server-helper.h"
#include <fstream>
#include <deque>
// #include <range/v3/view/cartesian_product.hpp>
#include <nlohmann/json.hpp>

namespace ns3 {
class MsduGrouper : public Object
{
public:
    static TypeId GetTypeId();
    
    /**
     * 构造函数
     * \param maxGroupSize 每组的最大MSDU数量
     */
    MsduGrouper (Ptr<WifiMac> mac, uint32_t mode);
    MsduGrouper ();
    ~MsduGrouper();
    /**
     * 更新PPDU发送时间
     * \param ppdu PPDU指针
     * \param duration 发送持续时间
     */
    void NotifyPpduTxDuration(Ptr<WifiPpdu const> ppdu, Time duration, uint8_t linkId);
    
    uint32_t GetAmpduLimit(uint8_t linkId, uint32_t preTitle, uint32_t mpduBufferSize);

    void SetAmpduLimit(int limit0, int limit1, int limit2); 

    uint8_t GetMode();

    std::vector<std::array<double, 2>> m_ppduTimeWindow;
    Time m_lastUpdateTime;
    std::vector<std::vector<double>> m_t = {{}, {}, {}};
    std::vector<uint32_t> m_datarate_setting = {0, 0, 0};
private:
    Ptr<WifiMac> m_mac; //!< the wifi MAC
    uint32_t m_mode;
    std::vector<int> m_ampduLimits; // 每条链路的最大AMPDU聚合长度
};

} // namespace ns3

#endif /* MSDU_GROUPER_H */