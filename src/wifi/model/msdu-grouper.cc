#include "ns3/ampdu-subframe-header.h"
#include "ns3/frame-exchange-manager.h"
#include "ns3/msdu-aggregator.h"
#include "ns3/wifi-tx-vector.h"

#include "msdu-grouper.h"
#include <iostream>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("MsduGrouper");
NS_OBJECT_ENSURE_REGISTERED(MsduGrouper);

QueueStats::QueueStats(Time period, Ptr<WifiMac> mac)
{
    cycle_time = period;
    m_initialized = false;
    blockwindow_begin = {Seconds(0), Seconds(0)};
    severe_blockwindow_begin = {Seconds(0), Seconds(0)};
    blockwindow_Total = {Seconds(0), Seconds(0)};
    m_mac = mac;
}

QueueStats::QueueStats()
{
    cycle_time = Seconds(0.1);
    m_initialized = false;
    blockwindow_begin = {Seconds(0), Seconds(0)};
    severe_blockwindow_begin = {Seconds(0), Seconds(0)};
    blockwindow_Total = {Seconds(0), Seconds(0)};
}

QueueStats::~QueueStats()
{
    m_bawqueue.clear();
}

bool
QueueStats::Initialize()
{
    m_initialized = true;
    return m_initialized;
}

double
QueueStats::GetThroughput(uint8_t linkId, Time period) // period <= 0, 返回从1s到当前时间的平均吞吐; period > 0, 返回前period时间内的平均吞吐
{
    double throughput = 0;
    for (const auto& it : m_mpduinfos)
    {
        if (it.m_rxstate &&
            (!period.IsStrictlyPositive() || Simulator::Now() - it.m_txTime < period) &&
            (it.m_linkIds & (1 << linkId)))
        {
            throughput += it.m_size;
        }
    }
    if (period.IsStrictlyPositive())
        return throughput * 8 / period.GetMicroSeconds();
    return throughput * 8 / (Simulator::Now().GetMicroSeconds() - 1e6);
}

std::vector<double>
QueueStats::GetRedundancyThroughput(Time period) // 计算冗余模式下传输的吞吐
{
    std::vector<double> redundancyThroughput = {0,0};
    for (const auto& it : m_mpduinfos)
    {
        if (it.m_rxstate &&
            (!period.IsStrictlyPositive() || Simulator::Now() - it.m_txTime < period) &&
            (it.m_redundancy == 1))
        {
            redundancyThroughput[0] += it.m_size;
        }
        if (it.m_rxstate &&
            (!period.IsStrictlyPositive() || Simulator::Now() - it.m_txTime < period) &&
            (it.m_redundancy == 2))
        {
            redundancyThroughput[1] += it.m_size;
        }
    }
    if (period.IsStrictlyPositive()){
        for (double& it : redundancyThroughput) {
            it = it * 8 / period.GetMicroSeconds();
        }
    }
    else {
        for (double& it : redundancyThroughput) {
            it = it * 8 / (Simulator::Now().GetMicroSeconds() - 1e6);
        }
    }
    return redundancyThroughput;
}

double
QueueStats::GetChannelEfficiency(uint8_t linkId, Time period)// period <= 0, 返回从1s到当前时间的平均信道占用率; period > 0, 返回前period时间内的平均信道占用率
{
    Time totalduration;
    if (period.IsStrictlyPositive())
    {
        for (auto it = m_ppduinfos.rbegin(); it != m_ppduinfos.rend(); it++)
        {
            if (Simulator::Now() - it->m_mpduinfos[0].m_txTime < period &&
                (it->linkId & (1 << linkId)))
            {
                totalduration += it->txDuration;
            }
        }
        return totalduration.GetSeconds() / period.GetSeconds();
    }
    else
    {
        for (auto it = m_ppduinfos.rbegin(); it != m_ppduinfos.rend(); it++)
        {
            if ((it->linkId & (1 << linkId)))
            {
                totalduration += it->txDuration;
            }
        }
        return totalduration.GetSeconds() / (Simulator::Now().GetSeconds() - 1);
    }
    return 0;
}

double
QueueStats::GetMpduSuccessRate(uint8_t linkId, Time period)
{
    double successnum = 0;
    double totalnum = 0;
    for (const auto& it : m_mpduinfos)
    {
        if ((!period.IsStrictlyPositive() || Simulator::Now() - it.m_txTime < period) &&
            (it.m_linkIds & (linkId + 1)))
        {
            if (it.m_rxstate)
                successnum++;
            totalnum += it.m_txcount;
        }
    }
    return successnum / (totalnum == 0 ? 1 : totalnum);
}

std::vector<uint32_t>
QueueStats::GetRecentAMPDULengths(uint8_t linkId, Time period)
{
    std::vector<uint32_t> lengths;
    if (period.IsStrictlyPositive())
    {
        for (auto it = m_ppduinfos.rbegin(); it != m_ppduinfos.rend(); it++)
        {
            if ((it->m_mpduinfos[0].m_linkIds & (linkId + 1)) &&
                (Simulator::Now() - it->txTime < period))
            {
                lengths.push_back(it->m_mpduinfos.size());
            }
        }
    }
    else
    {
        for (auto it = m_ppduinfos.rbegin(); it != m_ppduinfos.rend(); it++)
        {
            if ((it->m_mpduinfos[0].m_linkIds & (linkId + 1)))
            {
                lengths.push_back(it->m_mpduinfos.size());
            }
        }
    }
    return lengths.size() > 0 ? lengths : std::vector<uint32_t>{0};
}

double
QueueStats::GetAverageDataRate(uint8_t linkId, Time period)
{
    double datarate = 0;
    Time totalduration = Seconds(0);
    if (period.IsStrictlyPositive())
    {
        for (auto it = m_ppduinfos.rbegin(); it != m_ppduinfos.rend(); it++)
        {
            if (it->txDuration.IsStrictlyPositive() &&
                (it->linkId & (1 << linkId)) &&
                (Simulator::Now() - it->txTime < period))
            {
                datarate += it->m_mpduinfos[0].DataRate * it->txDuration.GetSeconds();
                totalduration += it->txDuration;
            }
        }
    }
    else
    {
        for (auto it = m_ppduinfos.rbegin(); it != m_ppduinfos.rend(); it++)
        {
            if (it->txDuration.IsStrictlyPositive() &&
                (it->linkId & (1 << linkId)))
            {
                datarate += it->m_mpduinfos[0].DataRate * it->txDuration.GetSeconds();
                totalduration += it->txDuration;
            }
        }
    }

    return datarate / (totalduration.IsStrictlyPositive() ? totalduration.GetMicroSeconds() : (Simulator::Now().GetMicroSeconds() - 1e6)); // 单位Mbps
}

std::vector<double>
QueueStats::GetBlockTimeRate(Time period) // 获得软卡窗时长占比
{
    std::vector<double> blocktimerate{0, 0};
    if (period.IsStrictlyPositive())
    {
        for (auto linkId = 0; linkId < 2; linkId++)
        {
            double blockTime = 0;
            for (const auto& it : blockwindow_time[linkId])
            {
                if (it.first > Simulator::Now() - period)
                {
                    blockTime += it.second.GetSeconds();
                }
            }
            blocktimerate[linkId] = blockTime / period.GetSeconds();
        }
    }
    return blocktimerate;
}

std::vector<double>
QueueStats::GetSevereBlockTimeRate(Time period) // 获得硬卡窗时长占比
{
    std::vector<double> severeblocktimerate{0, 0};
    if (period.IsStrictlyPositive())
    {
        for (auto linkId = 0; linkId < 2; linkId++)
        {
            double blockTime = 0;
            for (const auto& it : severe_blockwindow_time[linkId])
            {
                if (it.first > Simulator::Now() - period)
                {
                    blockTime += it.second.GetSeconds();
                }
            }
            severeblocktimerate[linkId] = blockTime / period.GetSeconds();
        }
    }
    return severeblocktimerate;
}

std::vector<uint32_t> 
QueueStats::GetBlockCnt(Time period) {
    std::vector<uint32_t> blockCnt{0, 0};
    if (period.IsStrictlyPositive())
    {
        for (auto linkId = 0; linkId < 2; linkId++)
        {
            double cnt = 0;
            for (const auto& it : blockwindow_time[linkId])
            {
                if (it.first >= Simulator::Now() - period)
                {
                    ++cnt;
                }
            }
            blockCnt[linkId] = cnt;
        }
    } else {
        for (auto linkId = 0; linkId < 2; linkId++)
        {
            blockCnt[linkId] = blockwindow_time[linkId].size();;
        }
    }
    return blockCnt;
}

std::vector<uint32_t> 
QueueStats::GetBlockCnt_other_inflight(Time period = Seconds(0)) 
/*
** 返回值为按链路统计的卡窗次数，类型为 std::vector<uint32_t>。
** period 参数可选，用于指定统计窗口周期（单位为 Time，默认为 0 表示统计全部时段）。
** 与其他类型的阻塞（如 TCP 流控引起的空队列或节点不饱和）区分开，本函数只统计真实由于链路冲突或调度不均引起的卡窗，该值也称为 blockCnt_True，见最终输出的csv表格
*/
{
    std::vector<uint32_t> blockCnt{0, 0};
    if (period.IsStrictlyPositive())
    {
        for (auto linkId = 0; linkId < 2; linkId++)
        {
            double cnt = 0;
            for (const auto& it : blockwindow_time_other_inflight[linkId])
            {
                if (it >= Simulator::Now() - period)
                {
                    ++cnt;
                }
            }
            blockCnt[linkId] = cnt;
        }
    } else {
        for (auto linkId = 0; linkId < 2; linkId++)
        {
            blockCnt[linkId] = blockwindow_time_other_inflight[linkId].size();;
        }
    }
    return blockCnt;
}

// 入队
bool
QueueStats::Enqueue(Ptr<const WifiMpdu> mpdu) 
/* 测试中，MPDU 第一次被分配序列号时，视为正式入队。
** 该时刻标志着该 MPDU 被纳入发送窗口进行调度与管理，相关统计如入队时间、重传次数等从此时开始计入。
*/
{
    Mac48Address recipient = mpdu->GetOriginal()->GetHeader().GetAddr1();
    uint8_t tid = mpdu->GetHeader().GetQosTid();
    auto it = m_bawqueue.find({recipient, tid});
    WiFiBawQueueIt mpduit;
    mpduit.seqNo = mpdu->GetHeader().GetSequenceNumber();
    mpduit.assignState = 0;
    mpduit.assignState = mpdu->GetAllocatedLink(); // 模式二中分配的链路
    mpduit.retryState = 0; // 传输次数默认设置为0，表示未被传输
    mpduit.acked = false;
    mpduit.discarded = false;
    mpduit.packet = mpdu->GetPacket(); 
    if (it == m_bawqueue.end()) // 第一次BA窗队列建立
    {
        m_bawqueue[{recipient, tid}] = std::vector<WiFiBawQueueIt>();
        m_bawqueue[{recipient, tid}].push_back(mpduit);
    }
    else
    {  
    /* 
    考虑到序列号（SN）为 12 比特，取值范围为 [0, 4095]，系统采用固定长度为4096的BA窗队列以实现对所有可能 SN 的完整映射。尽管协议中实际BA窗大小通常为256 、1024，使用全SN范围大小的队列便于处理序列号循环与快速定位，无需动态分配，简化实现逻辑。
    */
        it->second.push_back(mpduit);
        if (it->second.size() > 4096)
        {
            it->second.erase(it->second.begin());
        }
    }
    return true;
}

// 出队
bool
QueueStats::Pop(Ptr<const WifiMpdu> mpdu, bool ackordiscard)
/* 测试中，MPDU 出队原因仅有两种：ACK 成功 或 被丢弃（超时/重传次数过多）
 * ackOrDiscard = true 表示发送成功；false 表示丢弃
 */
{
    if (!mpdu->GetHeader().IsQosData())
        return false;
    Mac48Address recipient = mpdu->GetOriginal()->GetHeader().GetAddr1();
    uint8_t tid = mpdu->GetHeader().GetQosTid();
    if (auto recipientMld = m_mac->GetMldAddress(recipient))
    {
        recipient = *recipientMld;
    }
    std::vector<WiFiBawQueueIt>& queueit = m_bawqueue.find({recipient, tid})->second;
    uint16_t seqNo = mpdu->GetHeader().GetSequenceNumber();
    auto mpduit = std::find_if(queueit.begin(), queueit.end(), [&seqNo](const WiFiBawQueueIt& it) {
        return it.seqNo == seqNo;
    });
    mpduit->acked = ackordiscard;
    mpduit->discarded = !ackordiscard;
    NS_ASSERT(mpduit != queueit.end());
    while (queueit.size() > 0) // 删除前面已出队的包信息，直到第一个未出队为止
    {
        if (queueit.front().acked || queueit.front().discarded) 
        {
            queueit.erase(queueit.begin());
        }
        else
        {
            break;
        }
    }
    return true;
}

MsduGrouper::MsduGrouper():MsduGrouper(1, 4096, nullptr, nullptr, 0, Seconds(0)) {} // 委托构造给原构造函数，设置默认参数

MsduGrouper::MsduGrouper(uint32_t maxGroupSize,
                         uint32_t maxGroupNumber,
                         Ptr<WifiMacQueue> queue,
                         Ptr<WifiMac> mac,
                         uint32_t mode,
                         Time period)
    :
      m_maxGroupSize(maxGroupSize),
      m_queue(queue),
      m_mac(mac),
      m_mode(mode),
      m_period(period),
      m_cnt(0),
      m_maxGroupNumber(maxGroupNumber),
      m_currentGroup(0),
      m_currentCount(0),
      m_firstMsdu(nullptr),
      m_link1Pct(0),
      m_state(0),
      m_initial_link1Pct(0.0),
      m_increase_adj(false),
      m_prev_thp(0.0),
      m_has_reversed(false),
      m_locked_avg_thp(0.0),
      m_locked_sum_thp(0.0),
      m_locked_count(0)
{
    m_datarate = {0, 0};
    m_locked_avg_txop = {0, 0};
    m_locked_sum_txop = {0, 0};
    m_redundancyMode = 0;
    m_redundancyThreshold = {0.5, 0.5};
    m_maxAmpduSize = {0, 0};
    m_startTime = Seconds(1);
    m_queueStats = QueueStats(period, mac);
    m_maxRedundantPackets = {0, 0};
    m_RedundantPacketCnt = {0, 0};
    m_redundancyFixedNumber = {0, 0};
    m_inflighted = {0, 0};
    m_gs_enable = false;
    m_param_update = false;
    m_redundancy_enable = 0; // 默认关闭冗余模式
    m_maxtxoplimit = 0;
    m_ampduLimits = {-1, -1};
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

void
MsduGrouper::AssignAmsduByCnt()
{
    uint32_t value = static_cast<int>(256 * (1 - m_link1Pct)); // 分配给5G链路的数量
    
    if (m_cnt >= 256) // 累计数量大于256后归0，重新顺序分配
    {
        m_cnt = 0;
    }
    bool log = (m_mode & 0x02) && (m_mode & (1 << 4));
    if (log) std::cout << "开始软件仲裁分配链路：" << std::endl;
    if (m_cnt < value) // 优先连续分配给5G链路
    {
        m_firstMsdu->SetAllocatedLink(2); // 分配给 5G 链路
        if (log) std::cout << Simulator::Now() << ", 分配 Group-" << m_firstMsdu->GetGroupNumber() << " 给5G链路 "<<std::endl;
    }
    else
    {
        m_firstMsdu->SetAllocatedLink(1); // 分配给 2.4G 链路
        if (log) std::cout << Simulator::Now() << ", 分配 Group-" << m_firstMsdu->GetGroupNumber() << " 给5G链路 "<<std::endl;
    }
    
    m_cnt++;
}

double
MsduGrouper::SetLink1PctUdp(double thp1, double thp2, double rethp)
{
    double newLink1Pct = m_link1Pct; // 默认保持当前值
    if (!m_step_adjust)
    {
        rethp = std::round((GetQueueStats().GetRedundancyThroughput(m_period / 2)[0] +
                            GetQueueStats().GetRedundancyThroughput(m_period / 2)[1]) *
                           100) /
                100;
        thp1 = std::round(GetQueueStats().GetThroughput(0, m_period / 2) * 100) / 100;
        thp2 = std::round(GetQueueStats().GetThroughput(1, m_period / 2) * 100) / 100;
    }
    double current_thp = thp1 + thp2 - rethp;
    m_thp_map[m_link1Pct] = current_thp; // 记录当前比例下的总吞吐量

    switch (m_state)
    {
    case 0: { // 初始状态
        m_datarate[0] = GetQueueStats().GetAverageDataRate(0, m_period);
        newLink1Pct = 0;
        m_state = 1;
        break;
    }

    case 1: {
        m_datarate[1] = GetQueueStats().GetAverageDataRate(1, m_period);
        m_initial_link1Pct =
            std::clamp(std::round(m_datarate[0] / (m_datarate[0] + m_datarate[1]) * 100) / 100, 0.0, 1.0);
        newLink1Pct = m_initial_link1Pct;
        m_state = 2; // 转移到状态1
        break;
    }

    case 2: {
        m_prev_thp = current_thp;
        m_increase_adj = false; // 初始调整方向：减小
        newLink1Pct = adjustLinkPct(m_link1Pct);
        m_state = 4;
        break;
    }
    case 3: { // 锁定状态，监测吞吐量变化
        m_locked_count++;
        double change = 0;
        std::vector<double> change_re = {0, 0};
        if (m_locked_avg_thp)
            change = std::abs(current_thp - m_locked_avg_thp) / m_locked_avg_thp;
        ;
        if (m_locked_avg_txop[0])
            change_re[0] = std::abs(m_txop_num[0] - m_locked_avg_txop[0]) / m_locked_avg_txop[0];
        if (m_locked_avg_txop[1])
            change_re[1] = std::abs(m_txop_num[1] - m_locked_avg_txop[1]) / m_locked_avg_txop[1];
        // std::cout<<"link1Pct "<<m_link1Pct<<std::endl;
        // std::cout<<" current_total "<<current_thp<<" m_locked_total "<<m_locked_avg_thp<<" change
        // "<<change<<std::endl; std::cout<<" m_locked_total_re0 "<<m_locked_avg_txop[0]<<"
        // m_locked_total_re1 "<<m_locked_avg_txop[1]<<" change_re0 "<<change_re[0]<<" change_re1
        // "<<change_re[1]<<std::endl;
        if (m_locked_count > 1 && (change >= 0.06 || change_re[0] > 4 || change_re[1] > 4))
        { // 总吞吐量变化超过3%
            m_state = 1;
            newLink1Pct = 0;
            m_thp_map.clear();
            m_prev_thp = 0.0;
            m_increase_adj = false;
            m_locked_sum_thp = 0.0;
            m_locked_avg_thp = 0.0;
            m_locked_sum_txop = {0, 0};
            m_locked_avg_txop = {0, 0};
            m_locked_count = 0;
            m_has_reversed = false;
        }
        else if (m_locked_count > 1)
        {
            // 未触发更新，累加当前吞吐量并计算平均值
            m_locked_sum_thp += current_thp;
            m_locked_sum_txop[0] += m_txop_num[0];
            m_locked_sum_txop[1] += m_txop_num[1];
            m_locked_avg_thp = m_locked_sum_thp / (m_locked_count - 1);
            m_locked_avg_txop[0] = m_locked_sum_txop[0] / (m_locked_count - 1);
            m_locked_avg_txop[1] = m_locked_sum_txop[1] / (m_locked_count - 1);
        }
        break;
    }
    case 4: {
        if (!m_has_reversed)
        {
            if (current_thp >= m_prev_thp)
            { // 吞吐量提升，继续调整
                m_prev_thp = current_thp;
                newLink1Pct = adjustLinkPct(m_link1Pct);
            }
            else
            {                                     // 吞吐量下降
                m_increase_adj = !m_increase_adj; // 反转方向
                newLink1Pct = adjustLinkPct(m_initial_link1Pct);
            }
            m_has_reversed = true;
        }
        else
        {
            if (current_thp >= m_prev_thp * 0.99)
            { // 吞吐量提升，继续调整
                m_prev_thp = current_thp;
                newLink1Pct = adjustLinkPct(m_link1Pct);
            }
            else
            { // 吞吐量下降
                newLink1Pct = lockOptimalValue();
            }
        }
        break;
    }
    default:
        break;
    }

    m_step_adjust = (std::abs(std::round((newLink1Pct - m_link1Pct) * 100) / 100) <= STEP);

    return newLink1Pct;
}

double
MsduGrouper::SetLink1PctTcp(double thp1, double thp2, double rethp)
{
    double newLink1Pct = m_link1Pct; // 默认保持当前值

    if (!m_step_adjust)
    {
        rethp = std::round((GetQueueStats().GetRedundancyThroughput(m_period / 2)[0] +
                            GetQueueStats().GetRedundancyThroughput(m_period / 2)[1]) *
                           100) /
                100;
        thp1 = std::round(GetQueueStats().GetThroughput(0, m_period / 2) * 100) / 100;
        thp2 = std::round(GetQueueStats().GetThroughput(1, m_period / 2) * 100) / 100;
    }

    double current_thp = thp1 + thp2 - rethp;
    m_thp_map[m_link1Pct] = current_thp; // 记录当前比例下的总吞吐量
    switch (m_state)
    {
    case 0: {
        newLink1Pct = 0;
        m_prev_thp = current_thp;
        m_state = 1;
        break;
    }
    case 1: { // 动态调整阶段
        m_increase_adj = true;
        if (current_thp >= m_prev_thp * 0.99 || m_thp_map.size() < 5)
        {
            m_prev_thp = current_thp;
            newLink1Pct = adjustLinkPct(m_link1Pct);
        }
        else
        { // 吞吐量下降，锁定最佳值
            newLink1Pct = lockOptimalValue();
        }
        break;
    }

    case 3: { // 锁定状态，监测吞吐量变化
        m_locked_count++;
        double change = 0;
        std::vector<double> change_re = {0, 0};
        if (m_locked_avg_thp)
            change = std::abs(current_thp - m_locked_avg_thp) / m_locked_avg_thp;
        ;
        if (m_locked_avg_txop[0])
            change_re[0] = std::abs(m_txop_num[0] - m_locked_avg_txop[0]) / m_locked_avg_txop[0];
        if (m_locked_avg_txop[1])
            change_re[1] = std::abs(m_txop_num[1] - m_locked_avg_txop[1]) / m_locked_avg_txop[1];
        // std::cout << "link1Pct " << m_link1Pct << std::endl;
        // std::cout << " current_total " << current_thp << " m_locked_total " << m_locked_avg_thp
        //           << " change " << change << std::endl;
        // std::cout << " m_locked_total_re0 " << m_locked_avg_txop[0] << " m_locked_total_re1 "
        //           << m_locked_avg_txop[1] << " change_re0 " << change_re[0] << " change_re1 "
        //           << change_re[1] << std::endl;
        if (m_locked_count > 1 && (change >= 0.1 || change_re[0] > 3 || change_re[1] > 3))
        {
            m_state = 1;
            newLink1Pct = 0;
            m_thp_map.clear();
            m_prev_thp = 0.0;
            m_increase_adj = false;
            m_locked_sum_thp = 0.0;
            m_locked_avg_thp = 0.0;
            m_locked_sum_txop = {0, 0};
            m_locked_avg_txop = {0, 0};
            m_locked_count = 0;
        }
        else if (m_locked_count > 1)
        {
            // std::cout << "计算" << std::endl;
            // 未触发更新，累加当前吞吐量并计算平均值
            m_locked_sum_thp += current_thp;
            m_locked_sum_txop[0] += m_txop_num[0];
            m_locked_sum_txop[1] += m_txop_num[1];
            m_locked_avg_thp = m_locked_sum_thp / (m_locked_count - 1);
            m_locked_avg_txop[0] = m_locked_sum_txop[0] / (m_locked_count - 1);
            m_locked_avg_txop[1] = m_locked_sum_txop[1] / (m_locked_count - 1);
        }
        break;
    }
    default:
        break;
    }

    m_step_adjust =
        (std::abs(std::round((newLink1Pct - m_link1Pct) * 100) / 100) <= STEP) && m_state == 3;

    return newLink1Pct;
}

void
MsduGrouper::AggregateMsdu(Ptr<WifiMpdu> msdu)
{
    if(msdu->GetPacketSize() > 1000 && !istcp){
        istcp = true; // 判断测试流量为TCP流量
        m_link1Pct = 0.5;
    }
    if (m_mode == 0 || m_maxGroupSize == 1)
        return;
    // 检查是否需要切换组
    if (m_currentCount >= m_maxGroupSize) {
        m_currentGroup = (m_currentGroup + 1) % m_maxGroupNumber;
        m_currentCount = 0;
        m_firstMsdu = nullptr;
    }

    // 设置当前MSDU的组号并递增计数器
    msdu->SetGroupNumber(m_currentGroup);
    m_currentCount++;
    bool log = (m_mode & (1 << 4));
    // 处理首个MSDU或聚合逻辑
    if (m_currentCount == 1) {
        m_firstMsdu = msdu;
        if (log) {
            std::cout << Simulator::Now() << " First MSDU added to Group-" << m_currentGroup << std::endl;
        }
        if (m_mode & 0x02) {
            AssignAmsduByCnt(); // 模式二，基于优先级的提前连续分配
        }
    } else {
        // 聚合到首个MSDU
        if (m_firstMsdu && m_firstMsdu->GetGroupNumber() == msdu->GetGroupNumber() && m_firstMsdu != msdu) {
            m_queue->DequeueIfQueued({m_firstMsdu});
            m_firstMsdu->Aggregate(msdu);
            m_queue->Replace(msdu, m_firstMsdu);
            if (log) std::cout << Simulator::Now() << " Amsdu聚合: 第" << m_currentCount << "个Msdu聚合到 Group-" << m_currentGroup << std::endl;
        }
    }

    // 组满后准备切换
    if (m_currentCount == m_maxGroupSize) {
        m_currentGroup = (m_currentGroup + 1) % m_maxGroupNumber;
        m_currentCount = 0;
        m_firstMsdu = nullptr;
    }
}

void
MsduGrouper::AddCurrentGroup(uint32_t itemgroup)
{
    if (itemgroup == m_currentGroup)
    {
        m_currentGroup = (m_currentGroup + 1) % m_maxGroupNumber;
        m_currentCount = 0;
        m_firstMsdu = nullptr;
    }
}

// MPDU 入队
void
MsduGrouper::NotifyPacketEnqueue(Ptr<const WifiMpdu> mpdu, bool firstAssignSeqNo)
/*
** 测试中，MPDU 第一次被分配序列号时，视为正式入队。
*/
{
    if (!mpdu->GetHeader().IsQosData())
        return;
    if (firstAssignSeqNo)
    {   
        MPDUInfo mpduinfo;
        mpduinfo.m_Uid = mpdu->GetPacket()->GetUid();
        mpduinfo.m_receiver = mpdu->GetHeader().GetAddr1();
        mpduinfo.m_size = mpdu->GetPacket()->GetSize();
        mpduinfo.m_msduNum = 0;
        mpduinfo.m_mpduSeqNo = mpdu->GetHeader().GetSequenceNumber();
        mpduinfo.m_rxstate = false;
        mpduinfo.m_txcount = 0;
        mpduinfo.m_linkIds = 0;
        mpduinfo.m_ackTime = Seconds(0);
        mpduinfo.m_redundancy = 0;
        m_queueStats.m_mpduinfos.push_back(std::move(mpduinfo));
        m_queueStats.Enqueue(mpdu);
    }
}

// PPDU (AMPDU) Tx
void
MsduGrouper::NotifyPpduTxDuration(Ptr<const WifiPpdu> ppdu, Time duration, uint8_t linkId)
// PHY层检测PPDU发送事件
{
    if (!ppdu->GetPsdu()->GetHeader(0).IsQosData())
        return;
    PPDUInfo ppduinfo;
    ppduinfo.linkId = 1 << linkId;
    ppduinfo.txDuration = duration;
    ppduinfo.txTime = Simulator::Now();
    Ptr<const WifiPsdu> psdu = ppdu->GetPsdu();
    uint32_t nmpdus = 0;
    if (psdu->IsAggregate())
    {
        nmpdus = psdu->GetNMpdus();
        for (uint32_t i = 0; i < nmpdus; i++)
        {
            Ptr<Packet> packet = psdu->GetAmpduSubframe(i);
            auto it = std::find_if(
                m_queueStats.m_mpduinfos.rbegin(),
                m_queueStats.m_mpduinfos.rend(),
                [&packet](const MPDUInfo& it) { return it.m_Uid == packet->GetUid(); });
            if (it != m_queueStats.m_mpduinfos.rend())
            {
                it->m_txTime = Simulator::Now();
                ppduinfo.m_mpduinfos.push_back(*it);
            }
            else
            {
                NS_ABORT_MSG("MsduGrouper::NotifyPpduTxDuration: MPDU not found in QueueStats"
                             << Simulator::Now() << " " << packet->GetUid());
            }
        }
    }
    else
    {
        nmpdus = 1;
        Ptr<const Packet> packet = psdu->GetPacket();
        auto it =
            std::find_if(m_queueStats.m_mpduinfos.rbegin(),
                         m_queueStats.m_mpduinfos.rend(),
                         [&packet](const MPDUInfo& it) { return it.m_Uid == packet->GetUid(); });
        if (it != m_queueStats.m_mpduinfos.rend())
        {
            it->m_txTime = Simulator::Now();
            ppduinfo.m_mpduinfos.push_back(*it);
        }
        else
        {
            NS_ABORT_MSG("MsduGrouper::NotifyPpduTxDuration: MPDU not found in QueueStats"
                         << Simulator::Now() << " " << packet->GetUid());
        }
    }
    m_txtime_nmpdu_List[linkId].emplace_back(Simulator::Now().GetMicroSeconds(), duration.GetMicroSeconds(), nmpdus);
    m_queueStats.m_ppduinfos.push_back(ppduinfo);
}

// MPDU Discard
void
MsduGrouper::NotifyDiscardedMpdu(Ptr<const WifiMpdu> mpdu)
{
    m_queueStats.Pop(mpdu, false);
}

void 
MsduGrouper::NotifyPacketRedundancy(Ptr<const WifiMpdu> mpdu, uint8_t linkId)
/*
** 冗余模式发包检测
*/
{
    uint64_t id = mpdu->GetPacket()->GetUid(); // 获得包号
    auto it = std::find_if(m_queueStats.m_mpduinfos.begin(),
                           m_queueStats.m_mpduinfos.end(),
                           [&id](const MPDUInfo& it) { return it.m_Uid == id; });
    if (it != m_queueStats.m_mpduinfos.end())
    {
        it->m_redundancy = 1 << linkId;
    }
}

void
MsduGrouper::NotifyPhyTxEvent(Ptr<const Packet> packet,
                              uint16_t channelFreqMhz,
                              WifiTxVector txVector,
                              MpduInfo aMpdu,
                              uint16_t staId)
{
    uint64_t id = packet->GetUid(); // 获得包的标号
    uint8_t linkId = 0x00;
    if (channelFreqMhz < 3000) // 2.4G链路
    {
        linkId = 0x01;
    }
    else if (channelFreqMhz < 5700) // 5G链路
    {
        linkId = 0x02;
    }
    Ptr<Packet> p = packet->Copy();
    if (txVector.IsAggregation())
    {
        AmpduSubframeHeader subHdr;
        uint32_t extractedLength;
        p->RemoveHeader(subHdr);
        extractedLength = subHdr.GetLength();
        p = p->CreateFragment(0, static_cast<uint32_t>(extractedLength));
    }
    WifiMacHeader hdr;
    p->PeekHeader(hdr);
    if (!hdr.IsQosData() || !hdr.HasData())
        return;
    auto it = std::find_if(m_queueStats.m_mpduinfos.rbegin(),
                           m_queueStats.m_mpduinfos.rend(),
                           [&id](const MPDUInfo& mit) { return mit.m_Uid == id; });
    if (it != m_queueStats.m_mpduinfos.rend())
    {
        if (hdr.IsQosData() && hdr.IsQosAmsdu())
        {
            it->m_msduNum = m_maxGroupSize;
        }
        it->m_linkIds |= linkId;
        it->m_txcount += 1;
        it->DataRate = txVector.GetMode(staId).GetDataRate(txVector.GetChannelWidth(),
                                                           txVector.GetGuardInterval(),
                                                           1) *
                       txVector.GetNss(staId);
        m_datarateList[linkId].push_back(it->DataRate / 1e6);
        if (m_datarateList[linkId].size() > 100)
        {
            m_datarateList[linkId].erase(m_datarateList[linkId].begin());
        }
    }
    else
    {
        MPDUInfo mpduinfo;
        mpduinfo.m_Uid = id;
        mpduinfo.m_receiver = hdr.GetAddr1();
        mpduinfo.m_size = p->GetSize() - hdr.GetSerializedSize();
        mpduinfo.m_msduNum = 0;
        mpduinfo.m_rxstate = false;
        mpduinfo.m_txcount = 1;
        mpduinfo.m_linkIds = linkId;
        mpduinfo.m_ackTime = Seconds(0);
        mpduinfo.m_txTime = Simulator::Now();
        if (hdr.IsQosData() && hdr.IsQosAmsdu())
        {
            mpduinfo.m_msduNum = m_maxGroupSize;
        }
        mpduinfo.DataRate = txVector.GetMode(staId).GetDataRate(txVector.GetChannelWidth(),
                                                                txVector.GetGuardInterval(),
                                                                1) * txVector.GetNss(staId);
        m_queueStats.m_mpduinfos.push_back(mpduinfo);
        m_datarateList[linkId].push_back(mpduinfo.DataRate / 1e6);
        if (m_datarateList[linkId].size() > 100)
        {
            m_datarateList[linkId].erase(m_datarateList[linkId].begin());
        }
    }
    Mac48Address recipient = hdr.GetAddr1();
    uint8_t tid = hdr.GetQosTid();
    if (auto recipientMld = m_mac->GetMldAddress(recipient))
    {
        recipient = *recipientMld;
    }
    auto& queue = m_queueStats.m_bawqueue.find({recipient, tid})->second;
    uint16_t seqNo = hdr.GetSequenceNumber();
    auto queueit = std::find_if(queue.begin(), queue.end(), [&seqNo](const WiFiBawQueueIt& it) {
        return it.seqNo == seqNo;
    });
    if (queueit != queue.end())
    {
        queueit->retryState += 1;
        queueit->assignState = queueit->assignState | linkId;
        queueit->acked = false;
        queueit->discarded = false;
    }
    else
    {
        NS_ABORT_MSG("MsduGrouper::NotifyPhyTxEvent: MPDU not found in BawQueue" << Simulator::Now()
                                                                                 << " " << seqNo);
    }
}

void
MsduGrouper::NotifyAcked(Ptr<const WifiMpdu> mpdu, uint8_t linkId)
{
    uint64_t id = mpdu->GetPacket()->GetUid(); // 获得包号
    auto it = std::find_if(m_queueStats.m_mpduinfos.begin(),
                           m_queueStats.m_mpduinfos.end(),
                           [&id](const MPDUInfo& it) { return it.m_Uid == id; });
    if (it != m_queueStats.m_mpduinfos.end())
    {
        it->m_msduNum = mpdu->GetNMsdus();
        it->m_rxstate = true;
        it->m_ackTime = Simulator::Now();
    }
    m_queueStats.Pop(mpdu, true);
}

bool
MsduGrouper::GetRedundancyMode(uint8_t linkId)
// 对应链路上冗余模式是否开启
{
    return m_redundancyMode & (1 << linkId);
}

void
MsduGrouper::SetRedundancyMode(uint8_t linkId, uint32_t re_num)
// 开启对应链路上的冗余模式，最大冗余个数设置为re_num
{
    if (re_num == 0)
        return;
    m_redundancyMode = m_redundancyMode | (1 << linkId);
    m_maxRedundantPackets[linkId] = re_num;
    // std::cout << "Redundancy mode opened on Link " << uint32_t(linkId) << " MaxNum: " << re_num
    // << std::endl;
}

void
MsduGrouper::ResetRedundancyMode(uint8_t linkId)
{
    // std::cout << "Redundancy mode closed on Link " << uint32_t(linkId) << std::endl;
    m_redundancyMode = m_redundancyMode & ~(1 << linkId);
    m_RedundantPacketCnt[linkId] = 0;
    m_maxRedundantPackets[linkId] = 0;
}

uint32_t
MsduGrouper::GetBAWindowThreshold(uint8_t linkId)
{
    return m_maxAmpduSize[linkId] * m_redundancyThreshold[linkId];
}

bool
MsduGrouper::UpdateAmpduSize(uint8_t linkId, uint32_t size)
{
    if (!m_mode)
        return false;
    if (size > m_maxAmpduSize[linkId])
    {
        m_maxAmpduSize[linkId] = size;
    }
    if (Simulator::Now() > m_startTime + MilliSeconds(10))
    {
        if (size < GetBAWindowThreshold(linkId))
        {
            m_blockrateList[linkId].emplace_back(Simulator::Now(),
                                                 (double)size / m_maxAmpduSize[linkId]);
            if (!m_queueStats.blockwindow_begin[linkId].IsStrictlyPositive())
            {
                m_queueStats.blockwindow_begin[linkId] = Simulator::Now();
                // std::cout << Simulator::Now() << " 卡窗开始 on Link " << (uint32_t)linkId <<
                // std::endl;
            }
            if (m_inflighted[1 - linkId])
                m_queueStats.blockwindow_time_other_inflight[linkId].push_back(Simulator::Now());
        }
        else
        {
            if (m_queueStats.blockwindow_begin[linkId].IsStrictlyPositive())
            {
                m_queueStats.blockwindow_Total[linkId] +=
                    Simulator::Now() - m_queueStats.blockwindow_begin[linkId];
                m_queueStats.blockwindow_time[linkId].emplace_back(
                    m_queueStats.blockwindow_begin[linkId],
                    Simulator::Now() - m_queueStats.blockwindow_begin[linkId]);
                // std::cout << Simulator::Now() << " 卡窗结束 on Link " << (uint32_t)linkId <<
                // std::endl;
                m_queueStats.blockwindow_begin[linkId] = Seconds(0);
            }
        }
        if (size == 0) {
            if (!m_queueStats.severe_blockwindow_begin[linkId].IsStrictlyPositive())
            {
            m_queueStats.severe_blockwindow_begin[linkId] = Simulator::Now();
            }
        } else {
            if (m_queueStats.severe_blockwindow_begin[linkId].IsStrictlyPositive())
            {
                m_queueStats.severe_blockwindow_time[linkId].emplace_back(
                    m_queueStats.severe_blockwindow_begin[linkId],
                    Simulator::Now() - m_queueStats.severe_blockwindow_begin[linkId]);
                m_queueStats.severe_blockwindow_begin[linkId] = Seconds(0);
            }
        }
    }

    uint32_t redundancy_num = 0;
    if (Simulator::Now() > m_startTime && size == 0 && (m_mode & 0x01)) 
    // 模式一，硬卡窗，开启冗余
    {
        if (m_redundancy_enable & (1 << linkId))
        {
            // std::cout << "开启冗余 on Link " << (uint32_t)linkId << std::endl;
            uint32_t mpdusize = GetMeanMpduSize();
            auto datarate = m_queueStats.GetAverageDataRate(linkId, m_period);
            auto it = m_queueStats.m_ppduinfos.rbegin();
            while(it!=m_queueStats.m_ppduinfos.rend() && (it->linkId & (1 << linkId))) {++it;};
            if (it != m_queueStats.m_ppduinfos.rend() && it->txTime + it->txDuration > Simulator::Now() + MicroSeconds(32)) {
                redundancy_num =  std::floor((it->txTime + it->txDuration - Simulator::Now()).GetMicroSeconds() * datarate / mpdusize / 8);
                if (redundancy_num > 0 && m_mode & (1 << 5)) std::cout << "开启冗余 on Link " << (uint32_t)linkId <<  ", redundancy_num = " << redundancy_num << std::endl;
            }
            SetRedundancyMode(linkId, redundancy_num);   
            UpdateRedundancyCnt(linkId); 
        }
        return redundancy_num > 0;
    }
    if (Simulator::Now() > m_startTime && size == 0 && (m_mode & 0x02) && linkId == 1) 
    // 模式二，硬卡窗，只在5G上开启冗余
    {
        if (m_redundancy_enable & (1 << linkId)) {
            redundancy_num = static_cast<uint32_t>(std::lround(256 * (1.0 - m_link1Pct)));
            if (istcp && (m_link1Pct == 0.5 || m_state != 3))
                redundancy_num = 0; 
            // if (redundancy_num > 0) std::cout << "开启冗余 on Link " << (uint32_t)linkId <<  ",
            // redundancy_num = " << redundancy_num << std::endl;
            SetRedundancyMode(linkId, redundancy_num);
            UpdateRedundancyCnt(linkId);
        }
        return redundancy_num > 0;
    }
    return false;
}

mldParams
MsduGrouper::GetNextEdcaParameters(bool initial)
{
    if (m_param_update || initial) {
        auto params = m_gs->GetNext();
        m_current_params = params;
        return params;
    }
    else return m_current_params;
}

mldParams
MsduGrouper::GetCurrentEdcaParameters()
{
    return m_current_params;
}

mldParams
MsduGrouper::GetNewEdcaParameters(bool initial, uint16_t winSize, double p1, double p2, double datarate1, double datarate2, double occ1, double occ2)
{
    auto mpdusize = GetMeanMpduSize();
    uint32_t txoplimit = 0;
    uint32_t aifsn1 = 2, aifsn2 = 2;
    mldParams params;
    params.No = 1;
    params.CWmins = {1, 1};
    params.CWmaxs = {3, 3};
    params.Aifsns = {aifsn1, aifsn2};
    params.RTS_CTS = {0, 0};
    params.MaxSlrcs = {std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max()};
    params.MaxSsrcs = {std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max()};
    params.TxopLimits = {0, 0}; // < 171
    params.AmpduLimits = {-1, -1}; 
    params.AmpduSizes = {0, 0};
    params.RedundancyThresholds = {0.5, 0.5};
    params.RedundancyFixedNumbers = {0, 0};
    params.link1Pct = 1;
    if (!m_param_update || initial) {
        params.No = 0;
        m_current_params = params;
        return params;
    }
    if (m_mode & 0x01) { // 模式一
        if (occ1 == 0)
        datarate1 = std::accumulate(m_datarateList[1].begin(), m_datarateList[1].end(), 0.0) /
                    (m_datarateList[1].size() > 0 ? m_datarateList[1].size() : 1);
        if (occ2 == 0)
        datarate2 = std::accumulate(m_datarateList[2].begin(), m_datarateList[2].end(), 0.0) /
                    (m_datarateList[2].size() > 0 ? m_datarateList[2].size() : 1);
        int maxtxoplimit1 = std::ceil(std::min(winSize * mpdusize * 8 / datarate1 / 32, 170.));
        int maxtxoplimit2 = std::ceil(std::min(winSize * mpdusize * 8 / datarate2 / 32, 170.));
        m_maxtxoplimit = std::min(maxtxoplimit1, maxtxoplimit2);
        txoplimit = std::ceil((double) winSize / (p1 * datarate1 + p2 * datarate2) * mpdusize * 8 / 32);
        if (txoplimit > m_maxtxoplimit) {
            txoplimit = std::ceil((double) (winSize / 2) / (p1 * datarate1 + p2 * datarate2) * mpdusize * 8 / 32);
        }
        if(!istcp) {
            std::cout << "MLO Algorithm to Set Txoplimit: " << txoplimit << std::endl;
            params.TxopLimits = {txoplimit, txoplimit};
        } else { 
            // 减少对TCP ACK的干扰，避免TCP窗过小
            if (p1 < 0.9) {
                params.RTS_CTS[0] = 1;
                params.CWmins[0] = 3;
                params.CWmaxs[0] = 15;
                if (occ1 < 0.2) { // 信道占用率过低，可能是由于干扰导致，因此需要增大退避窗大小以避免碰撞
                    params.CWmins[0] = 7;
                    params.CWmaxs[0] = 31;
                }
            } else {
                params.RTS_CTS[0] = m_current_params.RTS_CTS[0];
                params.CWmins[0] = m_current_params.CWmins[0];
                params.CWmaxs[0] = m_current_params.CWmaxs[0];
            }
            if (p2 < 0.9) params.RTS_CTS[1] = 1; // 传输成功率过低，通过RTS/CTS来避免干扰
            // TCP流量不能通过简单通过调整TxopLimits来实现对齐，原因是TCP流量有拥塞控制机制，队列可能没有包，这样如果TxopLimits设置过大，会导致大段空口的浪费
            // 通过控制最大聚合数，来实现对齐
            std::cout << "TCP TRAFFIC" << std::endl;
            int n1 = 0, n2 = 0;
            int QOSDATA = mpdusize * 8;
            int TCPACK = 60 * 8 * m_maxGroupSize; // 一个MPDU对应的TCPACK大小 
            // n2 * QOSDATA / datarate2 >= n1 * QOSDATA / datarate1 + (n1 + n2) * TCPACK / datarate1
            std::cout << "datarate1: " << datarate1 << " datarate2: " << datarate2 << std::endl;
            for (n1 = 0; n1 < 256; n1++) {
                n2 = winSize - n1;
                if ((double)n2 * QOSDATA /datarate2 <= n1 * QOSDATA / datarate1 + (n1 + n2) * TCPACK / datarate1) {
                    n1 --;
                    n2 ++;
                    break;
                }
            }
            std::cout << "n1: " << n1 << " n2: " << n2 << std::endl;
            if (n1 < 0) {
                n1 = 0; // 关闭2.4G链路
                n2 = -1; // 表示5G上尽可能聚合最大数量的包
            }
            // SetAmpduLimit(0, n1);
            // SetAmpduLimit(1, n2);
            params.AmpduLimits = {n1, n2};
        }
        m_current_params = params;
        return params;
    }
    if (m_mode & 0x02) {
        double link1Pct = 1;  
        auto rethp = std::round((GetQueueStats().GetRedundancyThroughput(m_period)[0] +
                        GetQueueStats().GetRedundancyThroughput(m_period)[1]) *
                        100) / 100;
        auto thp1 = std::round(GetQueueStats().GetThroughput(0, m_period) * 100) / 100;
        auto thp2 = std::round(GetQueueStats().GetThroughput(1, m_period) * 100) / 100;
        std::cout <<"mode2  :" <<  istcp << " " << thp1 << ", " << thp2 << ", " << rethp << std::endl;
        if (istcp)
            link1Pct = SetLink1PctTcp(thp1, thp2, rethp); // need: 步长根据winSize设置 alg3
        else
            link1Pct = SetLink1PctUdp(thp1, thp2, rethp); // need: 步长根据winSize设置 alg4
        if (istcp) { // TCP流量才需要开启RTS/CTS，来避免与TCP ACK的干扰
            if ((occ1 * (1 - p1) > 0.05)) {
                params.RTS_CTS[0] = 1;
            }
            if ((occ2 * (1 - p2) > 0.05)) {
                params.RTS_CTS[1] = 1;
            }
        }
        params.link1Pct = link1Pct;
        m_current_params = params;
        return params;
    }
    return params;
}

uint32_t
MsduGrouper::AvailableRedundancy(uint8_t linkId)
{
    if (m_redundancyMode & (1 << linkId))
    {
        if (m_maxRedundantPackets[linkId] > m_RedundantPacketCnt[linkId])
        {
            m_RedundantPacketCnt[linkId] += 1;
            return 1;
        }
        else
        {
            ResetRedundancyMode(linkId);
            return 0;
        }
    }
    return 0;
}

void
MsduGrouper::UpdateRedundancyCnt(uint8_t linkId)
{
    m_RedundantPacketCnt[linkId] = 0;
}

void
MsduGrouper::UpdateRedundancyThreshold(const std::vector<double> thresholds)
{
    m_redundancyThreshold = thresholds;
}

bool
MsduGrouper::IsGridSearchEnabled()
{
    return m_gs_enable && m_mode;
}

bool
MsduGrouper::IsParamUpdateEnabled()
{
    return m_param_update && m_mode;
}

void
MsduGrouper::UpdateRedundancyFixedNumber(const std::vector<uint32_t> n)
{
    m_redundancyFixedNumber = n;
}

void
MsduGrouper::EnableGridSearch(std::string filename)
{
    m_gs_enable = true;
    m_gs = new GridSearch(filename, m_mode);
}

void 
MsduGrouper::EnableParamUpdate() {
    m_param_update = true;
}

void
MsduGrouper::SetTxopTimeEnd(uint64_t time /* us */, uint8_t linkId)
{
    m_txopTimeEnd[linkId] = time;
    m_txopList[linkId].emplace_back(m_txopTimeBegin[linkId], m_txopTimeEnd[linkId]);
    m_txopTimeEnd[linkId] = 0;
}

void
MsduGrouper::ResetInflighedCnt()
{
    m_inflighted[0] = 0;
    m_inflighted[1] = 0;
}

std::vector<uint32_t>
MsduGrouper::GetMaxAmpduLength()
{
    return m_maxAmpduSize;
}

std::vector<double>
MsduGrouper::GetMeanBlockRate(Time period)
{
    std::vector<double> meanblockrate{-1, -1};
    if (period.IsStrictlyPositive())
    {
        for (auto linkId = 0; linkId < 2; linkId++)
        {
            double blockrate = 0;
            uint32_t blockcnt = 0;
            for (const auto& it : m_blockrateList[linkId])
            {
                if (it.first > Simulator::Now() - period)
                {
                    blockrate += it.second;
                    blockcnt++;
                }
            }
            meanblockrate[linkId] = blockrate / (blockcnt == 0 ? 1 : blockcnt);
        }
    }
    return meanblockrate;
}

std::vector<uint64_t> 
MsduGrouper::GetMeanTxopTime(Time period) {
    std::vector<uint64_t> meanTxopTime{0, 0};
    if (period.IsStrictlyPositive())
    {
        for (auto linkId = 0; linkId < 2; linkId++)
        {
            uint64_t txoptime = 0;
            uint32_t txopcnt = 0;
            for (const auto& it : m_txopList[linkId])
            {
                if (it.first > (uint32_t)(Simulator::Now().GetMicroSeconds() - period.GetMicroSeconds()))
                {
                    txoptime += it.second - it.first;
                    txopcnt ++;
                }
            }
            meanTxopTime[linkId] = txoptime / (txopcnt == 0 ? 1 : txopcnt);
        }
    }
    return meanTxopTime;
}

std::vector<uint32_t> 
MsduGrouper::GetMeanTxMpduNum(Time period) {
    std::vector<uint32_t> meanTxopMpduNum{0, 0};
    if (period.IsStrictlyPositive())
    {
        for (auto linkId = 0; linkId < 2; linkId++)
        {
            uint32_t txopmpdunum = 0;
            uint32_t txopcnt = 0;
            for (const auto& it : m_txtime_nmpdu_List[linkId])
            {
                if (std::get<0>(it) > (uint32_t)(Simulator::Now().GetMicroSeconds() - period.GetMicroSeconds()))
                {
                    txopmpdunum += std::get<2>(it);
                    txopcnt ++;
                }
            }
            meanTxopMpduNum[linkId] = txopmpdunum / (txopcnt == 0 ? 1 : txopcnt);
        }
    }
    return meanTxopMpduNum;
}

void
MsduGrouper::ClearStats()
{
    m_maxAmpduSize = {0, 0};
    m_txop_num = {0, 0};
    m_startTime = Simulator::Now();
}

void
MsduGrouper::SetLink1Pct(double link1Pct){
    m_link1Pct = std::round(link1Pct * 1000) / 1000;
}

double
MsduGrouper::GetLink1Pct(){
    return m_link1Pct;
}

uint32_t
MsduGrouper::GetMaxGroupSize(){
    return m_maxGroupSize;
}

Time
MsduGrouper::GetStartTime(){
    return m_startTime;
}

Mac48Address 
MsduGrouper::GetRecipient() {
    return m_queueStats.m_bawqueue.begin()->first.first;
}

double 
MsduGrouper::GetPpduDurationPerMpdu(uint8_t linkId, Time period) {
    double ans = 1e-6;
    if (period.IsStrictlyPositive())
    {
        uint32_t nmpdus = 0;
        uint32_t times = 0;
        for (const auto& it : m_txtime_nmpdu_List[linkId])
        {
            if (std::get<0>(it) > (uint32_t)(Simulator::Now().GetMicroSeconds() - period.GetMicroSeconds()))
            {
                nmpdus += std::get<2>(it);
                times += std::get<1>(it);
            }
        }
        ans = (double)times / (nmpdus == 0 ? 1 : nmpdus);
    }
    return ans;
}

uint32_t 
MsduGrouper::GetMeanMpduSize() {
    uint32_t size = 0;
    uint32_t cnt = 0;
    for (auto it = m_queueStats.m_mpduinfos.rbegin(); it != m_queueStats.m_mpduinfos.rend(); it++)
    {
        if (it->m_size > 0)
        {
            size += it->m_size;
            cnt++;
        }
        if (cnt >= 100)
        {
            break;
        }
    }
    return cnt == 0 ? 0 : size / cnt;
}

void 
MsduGrouper::EnableRedundancyMode(){
    m_redundancy_enable = 0b11;
}

uint8_t 
MsduGrouper::GetMode() {
    return m_mode;
}

uint32_t 
MsduGrouper::GetAmpduLimit(uint8_t linkId) {
    if (m_ampduLimits[linkId] < 0) return std::numeric_limits<uint32_t>::max();
    return m_ampduLimits[linkId];
} 

void 
MsduGrouper::SetAmpduLimit(uint8_t linkId, int limit) {
    m_ampduLimits[linkId] = limit;
}

}// namespace ns3