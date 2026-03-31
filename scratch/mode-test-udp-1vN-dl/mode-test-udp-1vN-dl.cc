/*
* Copyright (c) 2024
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation;
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*
*/
#include "ns3/string.h"
#include "ns3/attribute-container.h"
#include "ns3/command-line.h"
#include "ns3/config.h"
#include "ns3/constant-rate-wifi-manager.h"
#include "ns3/eht-configuration.h"
#include "ns3/eht-phy.h"
#include "ns3/frame-exchange-manager.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/log.h"
#include "ns3/mobility-helper.h"
#include "ns3/multi-model-spectrum-channel.h"
#include "ns3/on-off-helper.h"
#include "ns3/packet-sink.h"
#include "ns3/packet-sink-helper.h"
#include "ns3/packet-socket-client.h"
#include "ns3/packet-socket-helper.h"
#include "ns3/packet-socket-server.h"
#include "ns3/udp-client-server-helper.h"
#include "ns3/udp-server.h"
#include "ns3/qos-utils.h"
#include "ns3/rng-seed-manager.h"
#include "ns3/spectrum-wifi-helper.h"
#include "ns3/uinteger.h"
#include "ns3/ap-wifi-mac.h"
#include "ns3/sta-wifi-mac.h"
#include "ns3/wifi-mac-queue.h"
#include "ns3/wifi-net-device.h"
#include "ns3/wifi-phy-common.h"
#include "ns3/wifi-phy.h"
#include "ns3/wifi-utils.h"
#include "ns3/yans-wifi-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/wifi-phy-rx-trace-helper.h"
#include "ns3/tcp-socket.h"
#include <deque>
#include <filesystem>
#define PI 3.1415926535

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("mlo-obss-dl-ucp");

struct Stats {
    std::vector<double> throughput;
    double time;
    std::vector<double> datarate;
    std::vector<double> blocktimerate;
    std::vector<double> severeblocktimerate;
    std::vector<double> blockrate;
    std::vector<uint32_t> blockCnt;
    std::vector<uint32_t> blockCnt_tr;
    std::vector<uint32_t> txopNum;
    std::vector<uint64_t> txopTime;
    mldParams params;
    std::vector<uint32_t> maxAmpduLength;
    std::vector<uint32_t> meanAmpduLength;
    Stats(std::vector<double> tp, double tm, std::vector<double> dr,  std::vector<double> btr, std::vector<double> sbtr,  std::vector<double> br,
        std::vector<uint32_t> bc,std::vector<uint32_t> bc2, std::vector<uint32_t> tn,
        std::vector<uint64_t> tt, mldParams pm, std::vector<uint32_t> maxl, std::vector<uint32_t> meanl)
    : throughput(tp), time(tm), datarate(std::move(dr)), blocktimerate(std::move(btr)), severeblocktimerate(std::move(sbtr)), blockrate(std::move(br)),
        blockCnt(std::move(bc)), blockCnt_tr(std::move(bc2)), txopNum(std::move(tn)), 
        txopTime(std::move(tt)), params(std::move(pm)), maxAmpduLength(std::move(maxl)), meanAmpduLength(std::move(meanl))
        {}
};

// std::vector<Stats> results;
std::unordered_map<double, std::vector<double>> throughputMap;

// void
// SaveParams(mldParams pm, double pct1, double time, std::vector<double> thpt, std::vector<double> p, std::vector<double> occ, std::vector<double> datarate, std::vector<double> blocktimerate,std::vector<double> severeblocktimerate, std::vector<double> blockrate, std::vector<uint32_t> blockCnt, std::vector<uint32_t> blockCnt_tr, std::vector<uint64_t> txopTime, std::vector<uint32_t> txopNum, std::vector<uint32_t> maxAmpduLength, std::vector<uint32_t> meanAmpduLength)
// {
//     Stats res{{0}, time, datarate, blocktimerate, severeblocktimerate, blockrate, blockCnt, blockCnt_tr, txopNum, txopTime, pm, maxAmpduLength, meanAmpduLength};
//     results.push_back(res);
//     Simulator::Schedule(NanoSeconds(1), [&](){
//         for (auto & result : results)
//         {
//             if(throughputMap.find(result.time) != throughputMap.end())
//             {
//                 result.throughput = throughputMap[result.time];
//             }
//         }
//     });
// }

// 安全 stoi，遇到非法字符返回默认值
int safe_stoi(const std::string& s, int default_val = 0)
{
    try {
        return std::stoi(s);
    } catch (...) {
        return default_val;
    }
}

// 去掉字符串前后空格
std::string trim(const std::string& s)
{
    size_t start = s.find_first_not_of(" \t");
    size_t end = s.find_last_not_of(" \t");
    if (start == std::string::npos || end == std::string::npos)
        return "";
    return s.substr(start, end - start + 1);
}

void updateThroughputCSV(const std::string& pretitle, int bw1, int bw2,
                         int mcs1, int mcs2, int nss,
                         int nsld1, int nsld2,
                         int maxAmpduNumSld0, int maxAmpduNumSld1,
                         int baw,
                         int maxAmpduNum0, int maxAmpduNum1,
                         int seedNumber, double totalthroughput,
                         double totalthroughput1, double totalthroughput2,
                         double pM1, double pM2)
{
    std::string filename = "throughput_0120.csv";
    std::vector<std::vector<std::string>> rows;
    bool found = false;

    // ================= 1. 读取文件 =================
    if (std::filesystem::exists(filename))
    {
        std::ifstream infile(filename);
        std::string line;

        while (std::getline(infile, line))
        {
            if (line.empty()) continue;

            std::stringstream ss(line);
            std::vector<std::string> tokens;
            std::string token;

            while (std::getline(ss, token, ','))
            {
                tokens.push_back(trim(token));
            }
            rows.push_back(tokens);
        }
        infile.close();

        // ========== 1.1 兼容旧 CSV，检测并升级 ==========
        bool needUpgrade = false;

        if (!rows.empty())
        {
            // 新格式应至少 19 列，且第 9 列是 maxAmpduNumSld0
            if (rows[0].size() < 19 || rows[0][8] != "maxAmpduNumSld0")
            {
                needUpgrade = true;
            }
        }

        if (needUpgrade)
        {
            // 升级表头
            rows[0] = {
                "pertitle","bw1","bw2","mcs1","mcs2","nss",
                "nsld1","nsld2",
                "maxAmpduNumSld0","maxAmpduNumSld1",
                "baw",
                "maxAmpduNum0","maxAmpduNum1","seed",
                "Throughput(Mbps)","Throughput1(Mbps)","Throughput2(Mbps)",
                "pM1","pM2"
            };

            // 升级旧数据行，回填默认值 = 1
            for (size_t i = 1; i < rows.size(); ++i)
            {
                auto& r = rows[i];
                if (r.size() >= 17)
                {
                    // 在 nsld2(index=7) 后插入
                    r.insert(r.begin() + 8, "1"); // maxAmpduNumSld0
                    r.insert(r.begin() + 9, "1"); // maxAmpduNumSld1
                }
            }
        }

        // ========== 1.2 查找并更新已有行 ==========
        for (auto& tokens : rows)
        {
            if (tokens.size() < 19 || tokens[0] == "pertitle")
                continue;

            std::string f_pretitle = tokens[0];
            int f_bw1 = safe_stoi(tokens[1]);
            int f_bw2 = safe_stoi(tokens[2]);
            int f_mcs1 = safe_stoi(tokens[3]);
            int f_mcs2 = safe_stoi(tokens[4]);
            int f_nss = safe_stoi(tokens[5]);
            int f_nsld1 = safe_stoi(tokens[6]);
            int f_nsld2 = safe_stoi(tokens[7]);
            int f_maxAmpduNumSld0 = safe_stoi(tokens[8]);
            int f_maxAmpduNumSld1 = safe_stoi(tokens[9]);
            int f_baw = safe_stoi(tokens[10]);
            int f_maxAmpduNum0 = safe_stoi(tokens[11]);
            int f_maxAmpduNum1 = safe_stoi(tokens[12]);
            int f_seed = safe_stoi(tokens[13]);

            if (f_pretitle == pretitle &&
                f_bw1 == bw1 && f_bw2 == bw2 &&
                f_mcs1 == mcs1 && f_mcs2 == mcs2 &&
                f_nss == nss &&
                f_nsld1 == nsld1 && f_nsld2 == nsld2 &&
                f_maxAmpduNumSld0 == maxAmpduNumSld0 &&
                f_maxAmpduNumSld1 == maxAmpduNumSld1 &&
                f_baw == baw &&
                f_maxAmpduNum0 == maxAmpduNum0 &&
                f_maxAmpduNum1 == maxAmpduNum1 &&
                f_seed == seedNumber)
            {
                tokens[14] = std::to_string(totalthroughput);
                tokens[15] = std::to_string(totalthroughput1);
                tokens[16] = std::to_string(totalthroughput2);
                tokens[17] = std::to_string(pM1);
                tokens[18] = std::to_string(pM2);
                found = true;
                break;
            }
        }
    }
    else
    {
        // ================= 新文件，直接写表头 =================
        rows.push_back({
            "pertitle","bw1","bw2","mcs1","mcs2","nss",
            "nsld1","nsld2",
            "maxAmpduNumSld0","maxAmpduNumSld1",
            "baw",
            "maxAmpduNum0","maxAmpduNum1","seed",
            "Throughput(Mbps)","Throughput1(Mbps)","Throughput2(Mbps)",
            "pM1","pM2"
        });
    }

    // ================= 2. 插入新行 =================
    if (!found)
    {
        rows.push_back({
            pretitle,
            std::to_string(bw1),
            std::to_string(bw2),
            std::to_string(mcs1),
            std::to_string(mcs2),
            std::to_string(nss),
            std::to_string(nsld1),
            std::to_string(nsld2),
            std::to_string(maxAmpduNumSld0),
            std::to_string(maxAmpduNumSld1),
            std::to_string(baw),
            std::to_string(maxAmpduNum0),
            std::to_string(maxAmpduNum1),
            std::to_string(seedNumber),
            std::to_string(totalthroughput),
            std::to_string(totalthroughput1),
            std::to_string(totalthroughput2),
            std::to_string(pM1),
            std::to_string(pM2)
        });
    }

    // ================= 3. 写回文件 =================
    std::ofstream outfile(filename, std::ios::out | std::ios::trunc);
    for (const auto& row : rows)
    {
        for (size_t i = 0; i < row.size(); ++i)
        {
            outfile << row[i];
            if (i + 1 < row.size()) outfile << ",";
        }
        outfile << "\n";
    }
    outfile.close();
}


std::string ppduTxOutputFile("./PPDU.csv");
std::string rtsctsTxOutputFile("./RTSCTS.csv");
std::string baTxOutputFile("./BA.csv");
enum class StaType
{
    MLD_STA,
    SLD_2G,
    SLD_5G,
    MLD_AP
};
void
NotifyPpduTxDurationUnified(StaType staType,
                            int32_t staIndex,
                            Ptr<const WifiPpdu> ppdu,
                            Time duration,
                            uint8_t linkid) 
{
    const auto& hdr = ppdu->GetPsdu()->GetHeader(0);
    uint64_t startUs = Simulator::Now().GetMicroSeconds();
    uint64_t endUs   = startUs + duration.GetMicroSeconds();

    /* ---------- 构造前缀 ---------- */
    std::ostringstream prefix;
    switch (staType)
    {
    case StaType::MLD_STA:
        prefix << "MLD";
        break;
    case StaType::SLD_2G:
        prefix << "SLD2G_" << staIndex;
        break;
    case StaType::SLD_5G:
        prefix << "SLD5G_" << staIndex;
        break;
    case StaType::MLD_AP:
        prefix << "AP";
        break;
    }

    /* ---------- RTS / CTS ---------- */
    if (hdr.IsRts() || hdr.IsCts())
    {
        std::fstream file(rtsctsTxOutputFile, std::ios::out | std::ios::app);
        file << prefix.str()
             << (hdr.IsRts() ? "_RTS_" : "_CTS_")
             << uint32_t(linkid) << ","
             << startUs << "," << endUs << std::endl;
        return;
    }

    /* ---------- AP 专属：BA / ACK ---------- */
    if (staType == StaType::MLD_AP && (hdr.IsBlockAck() || hdr.IsAck()))
    {
        std::fstream file(baTxOutputFile, std::ios::out | std::ios::app);
        file << prefix.str()
             << (hdr.IsBlockAck() ? "_BA_" : "_ACK_")
             << uint32_t(linkid) << ","
             << startUs << "," << endUs << std::endl;
        return;
    }

    /* ---------- 只统计 QoS Data ---------- */
    if (!hdr.IsQosData())
    {
        return;
    }

    /* ---------- MPDU 数量 ---------- */
    Ptr<const WifiPsdu> psdu = ppdu->GetPsdu();
    uint32_t nMpdu = psdu->IsAggregate() ? psdu->GetNMpdus() : 1;

    if (nMpdu == 0)
    {
        return;
    }

    /* ---------- 数据帧记录 ---------- */
    std::fstream file(ppduTxOutputFile, std::ios::out | std::ios::app);
    file << prefix.str() << "_" << uint32_t(linkid) << ","
         << startUs << "," << endUs << ","
         << nMpdu << std::endl;
}


double mpduNumMLD = 0.0;
double mpduNumMLD2G = 0.0;
double mpduNumMLD5G = 0.0;
double rtsCountMLD2G = 0;
double rtsCountMLD5G = 0;
double ppduCountMLD2G = 0;
double ppduCountMLD5G = 0;

std::map<uint32_t, double> mpduNumSLD2G; 
std::map<uint32_t, double> mpduNumSLD5G; 
std::map<uint32_t, double> rtsCountSLD2G; 
std::map<uint32_t, double> rtsCountSLD5G;
std::map<uint32_t, double> ppduCountSLD2G; 
std::map<uint32_t, double> ppduCountSLD5G;

void NotifyPerformance(
    Time statsBeginTime,
    Time statsEndTime,
    uint32_t payloadSize,
    uint32_t staIndex,
    bool isMld,
    Ptr<const WifiPpdu> ppdu,
    Time duration,
    uint8_t linkid)
{
    Time now = Simulator::Now();
    if (now < statsBeginTime || now + duration > statsEndTime)
        return;

    if (ppdu->GetPsdu()->GetHeader(0).IsRts())
    {
        if (isMld) {
            if (linkid == 0) rtsCountMLD2G++;
            else if (linkid == 1) rtsCountMLD5G++;
        } else {
            if (linkid == 0) rtsCountSLD2G[staIndex]++;
            else if (linkid == 1) rtsCountSLD5G[staIndex]++;
        }
        return;
    }

    if (!ppdu->GetPsdu()->GetHeader(0).IsQosData())
        return;

    Ptr<const WifiPsdu> psdu = ppdu->GetPsdu();
    uint32_t nmpdus = psdu->IsAggregate() ? psdu->GetNMpdus() : 1;
    if (nmpdus == 0)
        return;

    if (isMld) {
        mpduNumMLD += nmpdus;
        if (linkid == 0) {
            mpduNumMLD2G += nmpdus;
            ppduCountMLD2G++;
        } else if (linkid == 1) {
            mpduNumMLD5G += nmpdus;
            ppduCountMLD5G++;
        }
    } else {
        if (linkid == 0) {
            mpduNumSLD2G[staIndex] += nmpdus;  
            ppduCountSLD2G[staIndex] += 1;      
            
        } else if (linkid == 1) {
            mpduNumSLD5G[staIndex] += nmpdus;
            ppduCountSLD5G[staIndex] += 1;
        }
    }
}

double mpduNumMLDLast = 0.0;
double mpduNumMLD2GLast = 0.0;
double mpduNumMLD5GLast = 0.0;
void
PrintIntermediateTput(bool udp,
                    const ApplicationContainer& serverApp,
                    uint32_t payloadSize,
                    Time tputInterval,
                    Time simulationTime)
{
    Time now = Simulator::Now();
    if (mpduNumMLDLast) {
        double tp = (mpduNumMLD - mpduNumMLDLast) * 8. * payloadSize / tputInterval.GetMicroSeconds();
        double tp1 = (mpduNumMLD2G - mpduNumMLD2GLast) * 8. * payloadSize / tputInterval.GetMicroSeconds();
        double tp2 = (mpduNumMLD5G - mpduNumMLD5GLast) * 8. * payloadSize / tputInterval.GetMicroSeconds();
        throughputMap[now.GetSeconds()] = {tp, tp1, tp2};
    }
    if (now + tputInterval < simulationTime){
        Simulator::Schedule(tputInterval,
                            &PrintIntermediateTput,
                            udp,
                            serverApp,
                            payloadSize,
                            tputInterval,
                            simulationTime);
        mpduNumMLDLast = mpduNumMLD;
        mpduNumMLD2GLast = mpduNumMLD2G;
        mpduNumMLD5GLast = mpduNumMLD5G;
    }
}

void PrintSldStats(double interval, uint32_t payloadSize)
{
    std::cout << "------ SLD Statistics ------" << std::endl;
    std::cout << "STA\tpS\t\tThroughput(Mbps)\tBand" << std::endl;

    // 为计算平均值准备变量
    double sumPs2G = 0.0, sumTp2G = 0.0;
    int count2G = 0;
    double sumPs5G = 0.0, sumTp5G = 0.0;
    int count5G = 0;

    // 处理 2.4G SLD
    for (auto &entry : rtsCountSLD2G)
    {
        uint32_t staIndex = entry.first;
        double rts = entry.second;
        double ppdu = ppduCountSLD2G.count(staIndex) ? ppduCountSLD2G[staIndex] : 0;
        double tp = mpduNumSLD2G.count(staIndex) ? mpduNumSLD2G[staIndex] * payloadSize * 8.0 / interval : 0;

        double pS = (rts > 0) ? ppdu / rts : 0.0;

        // 更新统计
        sumPs2G += pS;
        sumTp2G += tp;
        count2G++;

        std::cout << staIndex << "\t"
                  << std::fixed << std::setprecision(6)
                  << pS << "\t\t"
                  << std::setprecision(3) << tp << "\t\t"
                  << std::setprecision(6)
                  << "2.4G" << std::endl;
    }

    // 处理 5G SLD
    for (auto &entry : rtsCountSLD5G)
    {
        uint32_t staIndex = entry.first;
        double rts = entry.second;
        double ppdu = ppduCountSLD5G.count(staIndex) ? ppduCountSLD5G[staIndex] : 0;
        double tp = mpduNumSLD5G.count(staIndex) ? mpduNumSLD5G[staIndex] * payloadSize * 8.0 / interval : 0;

        double pS = (rts > 0) ? ppdu / rts : 0.0;

        sumPs5G += pS;
        sumTp5G += tp;
        count5G++;

        std::cout << staIndex << "\t"
                  << std::fixed << std::setprecision(6)
                  << pS << "\t\t"
                  << std::setprecision(3) << tp << "\t\t"
                  << std::setprecision(6) // 恢复精度设置
                  << "5G" << std::endl;
    }

    std::cout << "-----------------------------" << std::endl;
    
    // 输出平均值
    std::cout << "\n------ Averages ------" << std::endl;
    
    if (count2G > 0)
    {
        double avgPs2G = sumPs2G / count2G;
        double avgTp2G = sumTp2G / count2G;
        std::cout << "2.4G: pS average = " << std::fixed << std::setprecision(6) << avgPs2G
                  << ", Throughput average = " << std::setprecision(3) << avgTp2G << " Mbps" << std::endl;
    }
    else
    {
        std::cout << "2.4G: No data" << std::endl;
    }

    if (count5G > 0)
    {
        double avgPs5G = sumPs5G / count5G;
        double avgTp5G = sumTp5G / count5G;
        std::cout << "5G:   pS average = " << std::fixed << std::setprecision(6) << avgPs5G
                  << ", Throughput average = " << std::setprecision(3) << avgTp5G << " Mbps" << std::endl;
    }
    else
    {
        std::cout << "5G:   No data" << std::endl;
    }
    
    std::cout << "---------------------" << std::endl;
}

// 退避统计类
class BackoffAndChannelMonitor : public Object
{
    public:
        struct BackoffRecord
        {
            Time time;
            Time backoffStartTime;
            uint32_t slotsDecreased;
            uint32_t remainingSlots;
        };

        BackoffAndChannelMonitor(uint32_t nodeId, uint8_t linkId, Time statsStartTime, Time statsEndTime)
            : m_statsStartTime(statsStartTime),
            m_statsEndTime(statsEndTime),
            m_nodeId(nodeId),
            m_linkId(linkId),
            m_totalBackoffSlots(0)
        {
        }

        // 退避值变化回调
        void NotifyBackoffSlotsTrace(Time backoffStartTime, uint32_t slotsDecreased, uint32_t remainingSlots)
        {
            Time now = Simulator::Now();
            if( now < m_statsStartTime || now > m_statsEndTime ) return;
            if (!m_backoffHistory.empty()) {
                if (now == m_backoffHistory.back().time && backoffStartTime == m_backoffHistory.back().backoffStartTime 
                    && slotsDecreased == m_backoffHistory.back().slotsDecreased && remainingSlots == m_backoffHistory.back().remainingSlots) 
                    return;
            }
            m_totalBackoffSlots += slotsDecreased;        
            m_backoffHistory.push_back(BackoffRecord{
                now,
                backoffStartTime,
                slotsDecreased,
                remainingSlots
            });
        }

        // 打印统计结果
        void PrintStatistics()
        {
            double totalTime = (m_statsEndTime - m_statsStartTime).GetSeconds();
            Time slotTime = MicroSeconds(9);
            Time totalBackoffTime = m_totalBackoffSlots * slotTime;
            double backoffRatio = totalBackoffTime.GetSeconds() / totalTime * 100.0;
            
            std::cout << "Node " << m_nodeId << " Link " << (uint32_t)m_linkId << ":" << std::endl;
            std::cout << "  总退避时隙数: " << m_totalBackoffSlots << std::endl;
            std::cout << "  总退避时间: " << totalBackoffTime.GetMicroSeconds() << " μs ("
                    << backoffRatio << "%)" << std::endl;
        }

        uint32_t GetTotalBackoffSlots() const { return m_totalBackoffSlots; }
        const std::vector<BackoffRecord>& GetBackoffHistory() const { return m_backoffHistory; }

    private:
        Time m_statsStartTime;
        Time m_statsEndTime;
        uint32_t m_nodeId;
        uint8_t m_linkId;
        uint32_t m_totalBackoffSlots;
        std::vector<BackoffRecord> m_backoffHistory; // 退避历史记录
};

// 全局监控器映射: nodeId -> linkId -> monitor
std::map<uint32_t, std::map<uint8_t, Ptr<BackoffAndChannelMonitor>>> g_backoffMonitors;

// 回调函数：退避值变化
void BackoffSlotsTraceCallback(uint32_t nodeId, uint8_t linkId, Time backoffStartTime, uint32_t slotsDecreased, uint32_t remainingSlots)
{
    if (g_backoffMonitors.count(nodeId) && g_backoffMonitors[nodeId].count(linkId))
    {
        g_backoffMonitors[nodeId][linkId]->NotifyBackoffSlotsTrace(backoffStartTime, slotsDecreased, remainingSlots);
    }
}

// 设置退避监控
void SetupBackoffAndChannelMonitoring(NodeContainer nodes, Time statsStartTime, Time statsEndTime)
{
    for (uint32_t i = 0; i < nodes.GetN(); ++i)
    {
        Ptr<Node> node = nodes.Get(i);
        uint32_t nodeId = node->GetId();
        Ptr<NetDevice> device = node->GetDevice(0);
        Ptr<WifiNetDevice> wifiDevice = DynamicCast<WifiNetDevice>(device);
        
        if (wifiDevice)
        {
            Ptr<WifiMac> mac = wifiDevice->GetMac();
            uint8_t numLinks = mac->GetNLinks();
            
            for (uint8_t linkId = 0; linkId < numLinks; ++linkId)
            {
                g_backoffMonitors[nodeId][linkId] = Create<BackoffAndChannelMonitor>(nodeId, linkId, statsStartTime, statsEndTime);     
                Ptr<QosTxop> edca = nullptr;
                if (mac)
                {
                    edca = mac->GetQosTxop(AC_BE);
                    if (edca)
                    {
                        edca->TraceConnectWithoutContext("BackoffSlotsTrace",
                                          MakeBoundCallback(&BackoffSlotsTraceCallback, nodeId));
                    }
                }
            }
        }
    }
}

// 保存详细的退避历史到CSV
void SaveDetailedBackoffHistory(const std::string& filename, 
                                NodeContainer nodes)
{
    std::ofstream fout(filename);
    fout << "NodeId,LinkId,Time,BackoffStartTime,SlotsDecreased,RemainingSlots\n";
    
    for (uint32_t i = 0; i < nodes.GetN(); ++i)
    {
        uint32_t nodeId = nodes.Get(i)->GetId();
        
        if (g_backoffMonitors.count(nodeId))
        {
            for (auto& linkEntry : g_backoffMonitors[nodeId])
            {
                uint8_t linkId = linkEntry.first;
                Ptr<BackoffAndChannelMonitor> monitor = linkEntry.second;
                
                const auto& history = monitor->GetBackoffHistory();
                for (const auto& record : history)
                {
                    fout << nodeId << ","
                         << (uint32_t)linkId << ","
                         << record.time.GetMicroSeconds() << ","
                         << record.backoffStartTime.GetMicroSeconds() << ","
                         << record.slotsDecreased << ","
                         << record.remainingSlots << "\n";
                }
                monitor->PrintStatistics();
            }
        }
    }
    
    fout.close();
}

int
main(int argc, char* argv[])
{
    uint32_t seedNumber = 1;
    uint32_t mcs1 = 13;
    uint32_t mcs2 = 10;
    uint32_t bw1 = 20;
    uint32_t bw2 = 80;
    double r1 = 1;
    double r2 = 1;
    uint32_t ch1 = 0;
    uint32_t ch2 = 0; 
    // std::string rateCtrl{"ideal"};
    std::string rateCtrl{"constant"};
    // std::string rateCtrl{"minstrel"};

    uint16_t mpduBufferSize{512};
    uint32_t maxAmpduSize{1024 * 8 * (1500 + 150)}; // 1048575
    uint32_t maxAmpduSize1{1 * (1500 + 80)};
    uint32_t maxAmpduSize2{1 * (1500 + 80)};
    uint32_t maxAmpduNum0 = 3;
    uint32_t maxAmpduNum1 = 209;
    uint32_t maxAmpduNumSld0 = 1;
    uint32_t maxAmpduNumSld1 = 1;
    double SLDinterval = 1.0;

    uint32_t txoplimit1 = 0, txoplimit2 = 0;
    uint8_t nStaSlds1 = 1;
    uint8_t nStaSlds2 = 1;
    double period_update = 0.5;
    bool grid_search_enable = false;
    uint32_t nss = 4;
    uint8_t mode = 1;

    double simT = 2.45;
    double transmission_delay = 0;
    bool param_update = false;
    bool redundancy_enable = false;
    bool logsender = false;
    bool logreceiver = false;
    bool logmode = false;
    Time simT_delayEnd = NanoSeconds(2);
    uint32_t maxGroupSize = 1;
    uint32_t pretitleint = 6;
    std::string scenario = "default";  // 默认场景
    CommandLine cmd(__FILE__);
    std::filesystem::path filepath = __FILE__;
    cmd.AddValue("seed", "seed number", seedNumber);
    cmd.AddValue("mcs1", "MCS for 2.4 GHz", mcs1);
    cmd.AddValue("mcs2", "MCS for 5 GHz", mcs2);
    cmd.AddValue("loadrate1", "load rate on 2.4 GHz", r1);
    cmd.AddValue("loadrate2", "load rate on 5 GHz", r2);
    cmd.AddValue("ch1", "channel id on 2.4 GHz", ch1);
    cmd.AddValue("ch2", "channel id on 5 GHz", ch2);
    cmd.AddValue("bw1", "band width on 2.4 GHz", bw1);
    cmd.AddValue("bw2", "band width on 5 GHz", bw2);
    // cmd.AddValue("ratectrl", "rate control alg", rateCtrl);
    cmd.AddValue("bawsize", "BA Window Size", mpduBufferSize);
    cmd.AddValue("max_ampdusize", "Max AmpduSize of AP 0", maxAmpduSize);
    // cmd.AddValue("max_ampdusize1", "Max AmpduSize of AP 1", maxAmpduSize1);
    // cmd.AddValue("max_ampdusize2", "Max AmpduSize of AP 2", maxAmpduSize2);
    // cmd.AddValue("gridsearch", "enable gridsearch", grid_search_enable);
    // cmd.AddValue("mode", "MLO Mode Setting", mode);
    cmd.AddValue("simt", "simulation time", simT);
    cmd.AddValue("period", "update period", period_update);
    // cmd.AddValue("txop1", "TxopLimit on 2.4 G", txoplimit1);
    // cmd.AddValue("txop2", "TxopLimit on 5 G", txoplimit2);
    cmd.AddValue("nss", "mimo", nss);
    cmd.AddValue("maxgroupsize", "maxgroupsize", maxGroupSize);
    // cmd.AddValue("delay", "delay setting", transmission_delay); // 传输延时，单位为微秒
    cmd.AddValue("nsld1", "interference setting", nStaSlds1);
    cmd.AddValue("nsld2", "interference setting", nStaSlds2);
    // cmd.AddValue("redundancy", "redundancy setting", redundancy_enable);
    // cmd.AddValue("param_update", "param update setting", param_update); 
    // cmd.AddValue("logsender", "new transmitter architecture log setting", logsender);
    // cmd.AddValue("logreceiver", "new receiver architecture log setting", logreceiver);
    // cmd.AddValue("logmode", "mode log setting", logmode);
    cmd.AddValue("pretitle", "pre title", pretitleint);
    cmd.AddValue("maxampdunum0", "max mpdu number of 2.4G", maxAmpduNum0);
    cmd.AddValue("maxampdunum1", "max mpdu number of 5G", maxAmpduNum1);
    cmd.AddValue("ampdunumsld0", "max ampdu num of SLD 2.4G", maxAmpduNumSld0);
    cmd.AddValue("ampdunumsld1", "max ampdu num of SLD 5G", maxAmpduNumSld1);
    cmd.AddValue("scenario", "Simulation scenario", scenario);
    cmd.AddValue("SLDinterval", "interval for throughput measurement", SLDinterval);

    cmd.Parse(argc, argv);

    maxAmpduSize1 = maxAmpduNumSld0 * (1500 + 72);
    maxAmpduSize2 = maxAmpduNumSld1 * (1500 + 72);
    uint32_t originalSeed = seedNumber;
    if (simT == 0) simT = period_update * 10 + 1;
    Time period{Seconds(period_update)};
    if (!(nStaSlds1)) r1 = 1e-9;  
    if (!(nStaSlds2)) r2 = 1e-9;
    Time tputInterval = period; // interval for detailed throughput measurement

    std::string pretitle = "";
    switch (pretitleint){
        case 1: pretitle = "greedy";   break;
        case 2: pretitle = "damla";    break;
        case 3: pretitle = "only5G";   break;
        case 4: pretitle = "only2G";   break;
        case 5: pretitle = "sumbawby2g"; break; // AmpduNum2G = *, AmpduNum5G = BAW - *
        case 6: pretitle = "bothset";  break;
        default: pretitle = "unknown"; break;
    }
    std::ostringstream oss;
    oss << pretitle
        << "_baw_" << mpduBufferSize
        << "_bw_" << bw1 << "_" << bw2
        << "_mcs_" << mcs1 << "_" << mcs2
        << "_interference_" << static_cast<uint32_t>(nStaSlds1) << "_" << static_cast<uint32_t>(nStaSlds2)
        << "_seed_" << seedNumber;

    if (SLDinterval != 1.0)
    {
        oss << "_sldinterval_" << SLDinterval;
    }
    if(maxAmpduNumSld0 != 1)
    {
        oss << "_maxAmpduNumSld0_" << maxAmpduNumSld0;
    }
    if(maxAmpduNumSld1 != 1)
    {
        oss << "_maxAmpduNumSld1_" << maxAmpduNumSld1;
    }

    if (pretitleint == 5)
    {
        oss << "_maxAmpduNum0_" << maxAmpduNum0;
    }
    else if (pretitleint == 6)
    {
        oss << "_maxAmpduNum0_" << maxAmpduNum0
            << "_maxAmpduNum1_" << maxAmpduNum1;
    }

    std::string title = oss.str();
    std::filesystem::path scenarioDir = filepath.parent_path() / scenario;
    std::filesystem::create_directories(scenarioDir);

    auto prepareFile = [&](const std::string& name) {
        std::string fullpath = (scenarioDir / name).string();
        std::filesystem::remove(fullpath); // 不存在也不会报错
        return fullpath;
    };

    ppduTxOutputFile = prepareFile(title + "_PPDU.csv");
    rtsctsTxOutputFile = prepareFile(title + "_RTSCTS.csv");
    baTxOutputFile = prepareFile(title + "_BA.csv");

    std::string csv_file = (scenarioDir / (title + ".csv")).string();

    if (mode && logmode) mode = mode | (1 << 4);
    if (mode && logsender) mode = mode | (1 << 5);
    uint8_t mode_recv = 1 << 2;
    if (mode_recv && logreceiver) mode_recv = mode_recv | (1 << 6);

    bool udp = true;
    uint8_t nLinks = 2;
    RngSeedManager::SetSeed(seedNumber);
    RngSeedManager::SetRun(seedNumber);
    double txPower = 20; 
    bool useRts{true};

    int gi = 800;
    Time simulationTime{Seconds(simT)};
    std::string dlAckSeqType{"NO-OFDMA"};
    size_t nStaMlds{1};
    uint32_t payloadSize = 1500; // must fit in the max TX duration when transmitting at MCS 0 over an RU of 26 tones

    if (useRts)
    {
        Config::SetDefault("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue("0"));
        // Config::SetDefault("ns3::WifiDefaultProtectionManager::EnableMuRts", BooleanValue(true));
    }
    // Config::SetDefault("ns3::WifiMacQueue::MaxDelay", TimeValue(simulationTime * 2));

    // Set infinitely long queue
     Config::SetDefault(
         "ns3::WifiMacQueue::MaxSize",
         QueueSizeValue(QueueSize(QueueSizeUnit::PACKETS, 1024*2)));

    // Disable fragmentation
    Config::SetDefault("ns3::WifiRemoteStationManager::FragmentationThreshold",   
        UintegerValue(std::numeric_limits<uint32_t>::max()));

    // Make retransmissions persistent
     Config::SetDefault("ns3::WifiRemoteStationManager::MaxSlrc",
             UintegerValue(std::numeric_limits<uint32_t>::max()));
     Config::SetDefault("ns3::WifiRemoteStationManager::MaxSsrc",
             UintegerValue(std::numeric_limits<uint32_t>::max())); 

    NodeContainer apNodes;
    NodeContainer mldNodes;
    apNodes.Create(1);
    mldNodes.Create(nStaMlds + nStaSlds1 + nStaSlds2);
    NetDeviceContainer apDev;
    NetDeviceContainer mldDev;

    /* WIFI Configuration */
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211be);
    std::vector<uint32_t> mcs{mcs1, mcs2};
    std::vector<uint32_t> bandwidth{bw1, bw2};
    std::vector<uint32_t> channelnum{ch1, ch2};
    std::string dataModeStr;
    uint64_t nonHtRefRateMbps;
    std::string ctrlRateStr; 

    if (rateCtrl == "constant") {
        for (uint8_t i = 0; i < nLinks; ++i) {
            if (i == 0) {
            dataModeStr = "EhtMcs" + std::to_string(mcs[i]);
            nonHtRefRateMbps = EhtPhy::GetNonHtReferenceRate(mcs[i]) / 1e6;
            ctrlRateStr = "ErpOfdmRate" + std::to_string(nonHtRefRateMbps) + "Mbps";
            std::cout << "Link " << std::to_string(i) <<" ControlRate: " << ctrlRateStr << " DataMode: " << dataModeStr << std::endl;
            wifi.SetRemoteStationManager(i, 
                                            "ns3::ConstantRateWifiManager",
                                            "DataMode", StringValue(dataModeStr),
                                            "ControlMode", StringValue(ctrlRateStr));
            }
            else {
            dataModeStr = "EhtMcs" + std::to_string(mcs[i]);
            nonHtRefRateMbps = EhtPhy::GetNonHtReferenceRate(mcs[i]) / 1e6;
            ctrlRateStr = "OfdmRate" + std::to_string(nonHtRefRateMbps) + "Mbps";
            std::cout << "Link " << std::to_string(i) <<" ControlRate: " << ctrlRateStr << " DataMode: " << dataModeStr << std::endl;
            wifi.SetRemoteStationManager(i, 
                                            "ns3::ConstantRateWifiManager",
                                            "DataMode", StringValue(dataModeStr),
                                            "ControlMode", StringValue(ctrlRateStr));
            }
        }
    } else if (rateCtrl == "ideal") {
        std::cout << "Ideal Rate Control" << std::endl;
        for (uint8_t i = 0; i < nLinks; ++i) {
            wifi.SetRemoteStationManager(i, 
                                            "ns3::IdealWifiManager");
        }
    }

    // PD SR
    // wifi.SetObssPdAlgorithm("ns3::ConstantObssPdAlgorithm", "ObssPdLevel", DoubleValue(-72.0));

    /* PropagationLossModel Configuration */
    Ptr<MultiModelSpectrumChannel> spectrumChannel_2 = CreateObject<MultiModelSpectrumChannel>();
    Ptr<LogDistancePropagationLossModel> lossModel_2 = CreateObject<LogDistancePropagationLossModel>();
    lossModel_2->SetAttribute("Exponent", DoubleValue(2.0));
    lossModel_2->SetAttribute("ReferenceDistance", DoubleValue(1.0));
    lossModel_2->SetAttribute("ReferenceLoss", DoubleValue(40.046));
    spectrumChannel_2->AddPropagationLossModel(lossModel_2);
    
    Ptr<MultiModelSpectrumChannel> spectrumChannel_5 = CreateObject<MultiModelSpectrumChannel>();
    Ptr<LogDistancePropagationLossModel> lossModel_5 = CreateObject<LogDistancePropagationLossModel>();
    lossModel_5->SetAttribute("Exponent", DoubleValue(3.5));
    lossModel_5->SetAttribute("ReferenceDistance", DoubleValue(1.0));
    lossModel_5->SetAttribute("ReferenceLoss", DoubleValue(50));
    spectrumChannel_5->AddPropagationLossModel(lossModel_5);

    /* PHY Configuration */
    SpectrumWifiPhyHelper phy(nLinks);
    if (nss > 1) {
        phy.Set("Antennas", UintegerValue(nss));
        phy.Set("MaxSupportedTxSpatialStreams", UintegerValue(nss));
        phy.Set("MaxSupportedRxSpatialStreams", UintegerValue(nss));
    }
    phy.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);
    phy.SetErrorRateModel("ns3::TableBasedErrorRateModel");
    phy.AddChannel(spectrumChannel_2, WIFI_SPECTRUM_2_4_GHZ);
    phy.AddChannel(spectrumChannel_5, WIFI_SPECTRUM_5_GHZ);
    
    for (uint8_t i = 0; i < nLinks; ++i) {
        if (i == 0) phy.AddPhyToFreqRangeMapping(i, WIFI_SPECTRUM_2_4_GHZ);
        else phy.AddPhyToFreqRangeMapping(i, WIFI_SPECTRUM_5_GHZ);
    }
    phy.Set("TxPowerStart", DoubleValue(txPower));
    phy.Set("TxPowerEnd", DoubleValue(txPower));
    for (uint8_t i = 0; i < nLinks; ++i)
    {
        std::string channelSetting = "{" + std::to_string(channelnum[i]) +", " + std::to_string(bandwidth[i]);
        if (i == 0) phy.Set(i, "ChannelSettings", StringValue(channelSetting + ", BAND_2_4GHZ, 0}"));
        else phy.Set(i, "ChannelSettings", StringValue(channelSetting + ", BAND_5GHZ, 0}"));
    }

    /* MAC Configuration */
    WifiMacHelper mac;
    Ssid bssSsid = Ssid("AP-MLD");
    // 1. MLO BSS 设置
    // MLD AP0
    // uint64_t beaconInterval = 100 * 1024;
    uint64_t beaconInterval = std::min<uint64_t>((ceil((simT * 1000000) / 1024) * 1024), (65535 * 1024));

    mac.SetType("ns3::ApWifiMac",
                "BeaconInterval",
                TimeValue(MicroSeconds(beaconInterval)),
                "EnableBeaconJitter",
                BooleanValue(false),
                "Ssid",
                SsidValue(bssSsid));
    apDev = wifi.Install(phy, mac, apNodes.Get(0));
    // Set AP transmission delay
    if (transmission_delay >= 0) { // 固定时延
        DynamicCast<WifiNetDevice>(apDev.Get(0))->GetPhy(0)->SetTransmissionDelay(MicroSeconds(transmission_delay));
        DynamicCast<WifiNetDevice>(apDev.Get(0))->GetPhy(1)->SetTransmissionDelay(MicroSeconds(transmission_delay));
    } 
    else 
    { // 使用公式计算时延
        DynamicCast<WifiNetDevice>(apDev.Get(0))->GetPhy(0)->SetTransmissionDelay(MicroSeconds(-1));
        DynamicCast<WifiNetDevice>(apDev.Get(0))->GetPhy(1)->SetTransmissionDelay(MicroSeconds(-1));
    }

    mac.SetType("ns3::StaWifiMac",
                "Ssid",
                SsidValue(bssSsid),
                "ActiveProbing",
                BooleanValue(false));
    mldDev = wifi.Install(phy, mac, mldNodes);
    
    Time statsBeginTime = Seconds(1.5);
    std::cout << "statsBeginTime: " << statsBeginTime.As(Time::S) << std::endl;
    Time statsEndTime = period * ((simulationTime + simT_delayEnd) / period).GetInt();
    std::cout << "statsEndTime: " << statsEndTime.As(Time::S) << std::endl;
    Ptr<WifiMac> mac_ap = DynamicCast<WifiNetDevice>(apDev.Get(0))->GetMac();
    std::cout << "AP0 MAC: " << apDev.Get(0)->GetAddress()<< std::endl;
    for (uint8_t i = 0; i < nLinks; ++i) {
        auto fem = mac_ap->GetFrameExchangeManager(i);
        std::cout << "\t apDevice " << "linkId " << std::to_string(i) << " mac address: " << fem->GetAddress() << std::endl;
    }
    for (size_t id = 0; id < nStaMlds; ++id) {
        Ptr<WifiMac> mac_mld = DynamicCast<WifiNetDevice>(mldDev.Get(id))->GetMac(); 
        std::cout << "MLD" << (id) << " MAC: " << mldDev.Get(id)->GetAddress() << std::endl;
        for (uint8_t i = 0; i < nLinks; ++i) {
            auto fem = mac_mld->GetFrameExchangeManager(i);
            std::cout << "\t mldDevice " << "linkId " << std::to_string(i) << " mac address: " << fem->GetAddress() << std::endl;
        }
    }
    DynamicCast<WifiNetDevice>(apDev.Get(0))->GetPhy(0)->TraceConnectWithoutContext(
        "PpduTxDuration",
        MakeBoundCallback(&NotifyPpduTxDurationUnified, StaType::MLD_AP, -1));
    DynamicCast<WifiNetDevice>(apDev.Get(0))->GetPhy(1)->TraceConnectWithoutContext(
        "PpduTxDuration",
        MakeBoundCallback(&NotifyPpduTxDurationUnified, StaType::MLD_AP, -1));
    DynamicCast<WifiNetDevice>(mldDev.Get(0))->GetPhy(0)->TraceConnectWithoutContext(
        "PpduTxDuration",
        MakeBoundCallback(&NotifyPpduTxDurationUnified, StaType::MLD_STA, -1));
    DynamicCast<WifiNetDevice>(mldDev.Get(0))->GetPhy(1)->TraceConnectWithoutContext(
        "PpduTxDuration",
        MakeBoundCallback(&NotifyPpduTxDurationUnified, StaType::MLD_STA, -1));
    DynamicCast<WifiNetDevice>(mldDev.Get(0))->GetPhy(0)->TraceConnectWithoutContext(
        "PpduTxDuration",
         MakeBoundCallback(&NotifyPerformance, statsBeginTime, statsEndTime, payloadSize, 0, true));
    DynamicCast<WifiNetDevice>(mldDev.Get(0))->GetPhy(1)->TraceConnectWithoutContext(
        "PpduTxDuration",
         MakeBoundCallback(&NotifyPerformance, statsBeginTime, statsEndTime, payloadSize, 0, true));

    for (size_t id = nStaMlds; id < nStaMlds + nStaSlds1; ++id) {
        Ptr<WifiMac> mac_mld = DynamicCast<WifiNetDevice>(mldDev.Get(id))->GetMac(); 
        std::cout << "SLD_2.4G_" << (id) << " MAC: " << mldDev.Get(id)->GetAddress() << std::endl;
        auto fem = mac_mld->GetFrameExchangeManager(0);
        std::cout << "\t sldDevice " << "linkId " << std::to_string(0) << " mac address: " << fem->GetAddress() << std::endl;
        uint32_t staIndex = id - nStaMlds;
        DynamicCast<WifiNetDevice>(mldDev.Get(id))->GetPhy(0)->TraceConnectWithoutContext(
            "PpduTxDuration",
            MakeBoundCallback(&NotifyPpduTxDurationUnified, StaType::SLD_2G, staIndex));
        DynamicCast<WifiNetDevice>(mldDev.Get(id))->GetPhy(1)->TraceConnectWithoutContext(
            "PpduTxDuration",
            MakeBoundCallback(&NotifyPpduTxDurationUnified, StaType::SLD_2G, staIndex));
        DynamicCast<WifiNetDevice>(mldDev.Get(id))->GetPhy(0)->TraceConnectWithoutContext(
            "PpduTxDuration",
            MakeBoundCallback(&NotifyPerformance, statsBeginTime, statsEndTime, payloadSize, staIndex, false));
        DynamicCast<WifiNetDevice>(mldDev.Get(id))->GetPhy(1)->TraceConnectWithoutContext(
            "PpduTxDuration",
            MakeBoundCallback(&NotifyPerformance, statsBeginTime, statsEndTime, payloadSize, staIndex, false));
    }

    for (size_t id = nStaMlds + nStaSlds1; id < nStaMlds + nStaSlds1 + nStaSlds2; ++id) {
        Ptr<WifiMac> mac_mld = DynamicCast<WifiNetDevice>(mldDev.Get(id))->GetMac(); 
        std::cout << "SLD_5G_" << (id) << " MAC: " << mldDev.Get(id)->GetAddress() << std::endl;
        auto fem = mac_mld->GetFrameExchangeManager(1);
        std::cout << "\t sldDevice " << "linkId " << std::to_string(1) << " mac address: " << fem->GetAddress() << std::endl;
        uint32_t staIndex = id - nStaMlds - nStaSlds1;
        DynamicCast<WifiNetDevice>(mldDev.Get(id))->GetPhy(0)->TraceConnectWithoutContext(
            "PpduTxDuration",
            MakeBoundCallback(&NotifyPpduTxDurationUnified, StaType::SLD_5G, staIndex));
        DynamicCast<WifiNetDevice>(mldDev.Get(id))->GetPhy(1)->TraceConnectWithoutContext(
            "PpduTxDuration",
            MakeBoundCallback(&NotifyPpduTxDurationUnified, StaType::SLD_5G, staIndex));
        DynamicCast<WifiNetDevice>(mldDev.Get(id))->GetPhy(0)->TraceConnectWithoutContext(
            "PpduTxDuration",
            MakeBoundCallback(&NotifyPerformance, statsBeginTime, statsEndTime, payloadSize, staIndex, false));
        DynamicCast<WifiNetDevice>(mldDev.Get(id))->GetPhy(1)->TraceConnectWithoutContext(
            "PpduTxDuration",
            MakeBoundCallback(&NotifyPerformance, statsBeginTime, statsEndTime, payloadSize, staIndex, false));
    }


    NetDeviceContainer devices;
    devices.Add(apDev);
    devices.Add(mldDev);
    wifi.AssignStreams(devices, seedNumber);

    // Set guard interval, MPDU buffer size, MaxAmsduSize, MaxAmpduSize
    Config::Set("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/HeConfiguration/GuardInterval",
                TimeValue(NanoSeconds(gi)));

    NodeContainer allNodes(apNodes, mldNodes);
    for (uint32_t i = 0; i < allNodes.GetN(); ++i)
    {
        if(i >= apNodes.GetN() + nStaMlds + nStaSlds1) // 5G SLD
            Config::Set("/NodeList/" + std::to_string(i) +
                        "/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_MaxAmpduSize",
                        UintegerValue(maxAmpduSize2));
        else if(i >= apNodes.GetN() + nStaMlds) // 2.4G SLD
            Config::Set("/NodeList/" + std::to_string(i) +
                        "/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_MaxAmpduSize",
                        UintegerValue(maxAmpduSize1));
    }

    Config::Set("/NodeList/0/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_MaxAmpduSize",
                UintegerValue(maxAmpduSize));
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_MaxAmpduSize",
                UintegerValue(maxAmpduSize));

    Config::Set("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/MpduBufferSize",
                UintegerValue(mpduBufferSize));

    // ---------------- Mobility: 圆形分布 ----------------
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();

    // 主 AP0 
    positionAlloc->Add(Vector(0.0, 2.0, 0.0));
    std::cout << "AP 坐标: (0.0, 2.0, 0.0)" << std::endl;

    double bssRadius = 0.1; // 圆形分布半径

    // STA-SLD 2.4G
        double step = 360.0 / (nStaMlds + nStaSlds1 + nStaSlds2);
        for (uint32_t i = 0; i < nStaMlds + nStaSlds1 + nStaSlds2; i++) {
            double ang = step * i * M_PI / 180.0;
            double x = 0.0 + bssRadius * cos(ang);
            double y = 2.0 + bssRadius * sin(ang);
            positionAlloc->Add(Vector(x, y, 0.0));
            std::cout << " STA" << i << ": (" << x << ", " << y << ", 0.0)" << std::endl;
        }

    MobilityHelper mobility;
    mobility.SetPositionAllocator(positionAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(allNodes);
    
    /* Internet stack*/
    InternetStackHelper stack;
    stack.Install(allNodes);
    seedNumber += stack.AssignStreams(allNodes, seedNumber);

    Ipv4AddressHelper address;
    address.SetBase("10.0.0.0", "255.255.255.0");
    Ipv4InterfaceContainer apNodeInterface = address.Assign(apDev.Get(0));
    Ipv4InterfaceContainer mldNodeInterface = address.Assign(mldDev);

    std::cout << "AP0 IP: " << apNodeInterface.GetAddress(0) << std::endl;
    for (size_t i = 0; i < nStaMlds + nStaSlds1 + nStaSlds2; ++i) {
        std::cout << "  STA-" << std::to_string(i) + ": " << mldNodeInterface.GetAddress(i) << std::endl;
    }

    /* Setting applications */
    ApplicationContainer ulserverApp;
    uint16_t port = 9;
    UdpServerHelper server(port);
    ulserverApp = server.Install(apNodes.Get(0));
    seedNumber += server.AssignStreams(apNodes.Get(0), seedNumber);
    ulserverApp.Start(Seconds(0.0));
    ulserverApp.Stop(simulationTime + simT_delayEnd);

    // if (rateCtrl == "ideal" || rateCtrl == "constant") mcs[0] = mcs[1] = 13;
    const auto maxLoad2 = EhtPhy::GetDataRate(mcs[0], bandwidth[0] , NanoSeconds(gi), 1) * nss;
    const auto maxLoad5 = EhtPhy::GetDataRate(mcs[1], bandwidth[1] , NanoSeconds(gi), 1) * nss;
    std::cout << "maxload = " << std::to_string((maxLoad2 + maxLoad5)/1e6) << " Mbps; 2.4 GHz: " <<  std::to_string(maxLoad2/1e6) << " Mbps, 5 GHz: " << std::to_string(maxLoad5/1e6) << " Mbps" << std::endl;
    const auto packetInterval = payloadSize * 8.0 / (maxLoad2 + maxLoad5) / 2; 
    const auto packetInterval2 = payloadSize * 8.0 / maxLoad2 / SLDinterval;
    const auto packetInterval5 = payloadSize * 8.0 / maxLoad5 / SLDinterval;
    std::cout << "2.4 GHz: " <<  std::to_string(packetInterval2) << " s, 5 GHz: " << std::to_string(packetInterval5) << " s" << std::endl;
    UdpClientHelper client(apNodeInterface.GetAddress(0), port);
    client.SetAttribute("MaxPackets", UintegerValue(0));
    client.SetAttribute("PacketSize", UintegerValue(payloadSize));
    for (uint32_t i = 0; i < mldNodes.GetN(); ++i){
        client.SetAttribute("Interval", TimeValue(Seconds(packetInterval)));
        if(i == 0) client.SetAttribute("Interval", TimeValue(Seconds(packetInterval)));
        else if(i < nStaMlds + nStaSlds1) client.SetAttribute("Interval", TimeValue(Seconds(packetInterval2)));
        else client.SetAttribute("Interval", TimeValue(Seconds(packetInterval5)));
        ApplicationContainer clientApp = client.Install(mldNodes.Get(i));
        seedNumber += client.AssignStreams(mldNodes.Get(i), seedNumber);
        clientApp.Start(Seconds(1));
        clientApp.Stop(simulationTime + simT_delayEnd);
    }

    // Enable TID-to-Link Mapping for AP and MLD STAs
    for (auto i = mldDev.Begin(); i != mldDev.End(); ++i)
    {
        auto wifiDev = DynamicCast<WifiNetDevice>(*i);
        wifiDev->GetMac()->GetEhtConfiguration()->SetAttribute("TidToLinkMappingNegSupport",
                                                            EnumValue(WifiTidToLinkMappingNegSupport::ANY_LINK_SET));
    }


    std::string mldMappingStr = "0,1,2,3,4,5,6,7 0,1";
    if(pretitleint == 3) mldMappingStr = "0,1,2,3,4,5,6,7 1"; // only 5G
    if(pretitleint == 4) mldMappingStr = "0,1,2,3,4,5,6,7 0"; // only 2G
    std::string mldMappingStr1 = "0,1,2,3,4,5,6,7 0";
    std::string mldMappingStr2 = "0,1,2,3,4,5,6,7 1";

    size_t index = 0;
    for (auto i = mldDev.Begin(); i != mldDev.End(); ++i, ++index)
    {
        auto wifiDev = DynamicCast<WifiNetDevice>(*i);
        wifiDev->GetMac()->SetAttribute("ActiveProbing", BooleanValue(true));
        std::string mappingToUse;
        if (index < nStaMlds) mappingToUse = mldMappingStr;
        else if (index < nStaMlds + nStaSlds1) mappingToUse = mldMappingStr1;
        else mappingToUse = mldMappingStr2;
        wifiDev->GetMac()->GetEhtConfiguration()->SetAttribute("TidToLinkMappingDl", StringValue(mappingToUse));
        wifiDev->GetMac()->GetEhtConfiguration()->SetAttribute("TidToLinkMappingUl", StringValue(mappingToUse));
    }

    auto apWifiDev = DynamicCast<WifiNetDevice>(apDev.Get(0));
    apWifiDev->GetMac()->GetEhtConfiguration()->SetAttribute("TidToLinkMappingDl", StringValue(mldMappingStr));
    apWifiDev->GetMac()->GetEhtConfiguration()->SetAttribute("TidToLinkMappingUl",StringValue(mldMappingStr));


    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/UseExplicitBarAfterMissedBlockAck", BooleanValue(false)); // 开启隐式BAR
    Config::Set("/NodeList/0/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/UseExplicitBarAfterMissedBlockAck", BooleanValue(false)); // 开启隐式BAR
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/MaxGroupSize", UintegerValue(maxGroupSize));
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/Period", TimeValue(period));
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/Mode", UintegerValue(mode)); // mode = 1 表示 模式一(硬件仲裁)， mode = 2 表示 模式二(软件仲裁)
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/PreTitle", UintegerValue(pretitleint));
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/MaxAmpduNum0", UintegerValue(maxAmpduNum0));
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/MaxAmpduNum1", UintegerValue(maxAmpduNum1));
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/DataRate24", DoubleValue(maxLoad2)); 
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/DataRate5", DoubleValue(maxLoad5));

    Config::Set("/NodeList/0/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/Mode", UintegerValue(mode_recv)); // 只负责接收，无msdu_grouper, mode只要非0, 接收端就是新架构，BA只包含各自链路所收到的包的接收信息，各自维护自己的bitmap
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/GridSearchEnable", BooleanValue(grid_search_enable)); // 是否开启网格搜索，用于静态场景下的最优参数搜索，只有在param_update = true时才会更新参数
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/ParamUpdate", BooleanValue(param_update));
    // Config::Set("/NodeList/0/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/GridSearchParameter", StringValue("./scratch/params.json")); // 网格搜索使用的参数集合
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/RedundancyEnable", BooleanValue(redundancy_enable)); // 是否启用冗余模式


    /* OBSS EDCA */
    for (uint32_t i = 0; i < allNodes.GetN(); ++i)
    {
        Config::Set("/NodeList/" + std::to_string(i) +
                "/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/Aifsns", AttributeContainerValue<UintegerValue>(std::list<uint64_t>{2,2}));
        Config::Set("/NodeList/" + std::to_string(i) +
                "/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/Aifsns", AttributeContainerValue<UintegerValue>(std::list<uint64_t>{2,2}));
        Config::Set("/NodeList/" + std::to_string(i) +
                "/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/MinCws", AttributeContainerValue<UintegerValue>(std::list<int>{15,15}));
        Config::Set("/NodeList/" + std::to_string(i) +
                "/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/MinCws", AttributeContainerValue<UintegerValue>(std::list<int>{15,15}));
        Config::Set("/NodeList/" + std::to_string(i) +
                "/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/MaxCws", AttributeContainerValue<UintegerValue>(std::list<int>{1023,1023}));
        Config::Set("/NodeList/" + std::to_string(i) +
                "/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/MaxCws", AttributeContainerValue<UintegerValue>(std::list<int>{1023,1023}));   
    }
    /* BSS EDCA */
    Config::Set("/NodeList/0/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/Aifsns", AttributeContainerValue<UintegerValue>(std::list<uint64_t>{2,2}));
    Config::Set("/NodeList/0/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/MinCws", AttributeContainerValue<UintegerValue>(std::list<int>{15,15}));
    Config::Set("/NodeList/0/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/MaxCws", AttributeContainerValue<UintegerValue>(std::list<int>{1023,1023}));
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/Aifsns", AttributeContainerValue<UintegerValue>(std::list<uint64_t>{2,2}));
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/MinCws", AttributeContainerValue<UintegerValue>(std::list<int>{15,15}));
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/MaxCws", AttributeContainerValue<UintegerValue>(std::list<int>{1023,1023}));


    std::cout << "TxopLimits : (" << txoplimit1 << "," << txoplimit2  << ")" << std::endl;
    std::vector<Time> txopLimitList = {MicroSeconds(32) * txoplimit1, MicroSeconds(32) * txoplimit2};
    std::cout << txopLimitList[0] << " " << txopLimitList[1] << std::endl;
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/TxopLimits", AttributeContainerValue<TimeValue>(txopLimitList));
    // Config::ConnectWithoutContext("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/GetNextParams", MakeCallback(&SaveParams));
    
    Simulator::Schedule(Seconds(1.0), &PrintIntermediateTput, udp, ulserverApp, payloadSize, tputInterval, simulationTime + simT_delayEnd);
    Simulator::Schedule(Seconds(0.5), &SetupBackoffAndChannelMonitoring, mldNodes, statsBeginTime, statsEndTime);
 
    Simulator::Stop(simulationTime + simT_delayEnd);
    Simulator::Run();
    Simulator::Destroy();
    if (mode == 0) return 0;
    
    // 保存详细的退避历史
    std::string historyFile = (scenarioDir / (title + "_BackoffHistory.csv")).string();
    SaveDetailedBackoffHistory(historyFile, mldNodes);

    // std::ofstream fout(csv_file, std::ios::out);
    // fout << "No, Time, Mode, CWmin1, CWmax1, CWmin2, CWmax2, Aifsn1, Aifsn2, TxopLimit1, TxopLimit2, AmpduLimit1, AmpduLimit2, RTS_CTS1, RTS_CTS2,"
    //         "BlockCnt1, BlockCnt2, BlockCnt1_True, BlockCnt2_True, TxopTime1(us), TxopTime2(us), TxopCnt1, TxopCnt2, MaxAmpduLength1, MaxAmpduLength2, MeanAmpduLength1, MeanAmpduLength2, "
    //         "blocktimerate1, blocktimerate2, severeblocktimerate1, severeblocktimerate2, blockrate1, blockrate2, datarate1, datarate2, throughput1, throughput2, Throughput(Mbps)" << std::endl;
    // if (!results.empty()) {
    //     for (const auto & res : results)
    //     {
    //         const mldParams & params = res.params;
    //         fout << params.No << ", " << res.time << ", " << (uint32_t)mode << ", "
    //               << params.CWmins[0] << ", " << params.CWmaxs[0] << ", " << params.CWmins[1]
    //               << ", " << params.CWmaxs[1] << ", " << params.Aifsns[0] << ", "
    //               << params.Aifsns[1] << ", " << params.TxopLimits[0] << ", "
    //               << params.TxopLimits[1] << ", " << params.AmpduLimits[0] << ", "
    //               << params.AmpduLimits[1] << ", " << params.RTS_CTS[0] << ", " << params.RTS_CTS[1] << ", "
    //               << res.blockCnt[0] << ", "<< res.blockCnt[1] << ", " << res.blockCnt_tr[0] << ", " << res.blockCnt_tr[1]
    //               << ", " << res.txopTime[0] << ", " << res.txopTime[1] << ", " << res.txopNum[0]
    //               << ", " << res.txopNum[1] << ", " << res.maxAmpduLength[0] << ", "
    //               << res.maxAmpduLength[1] << ", " << res.meanAmpduLength[0] << ", "
    //               << res.meanAmpduLength[1] << ", " << res.blocktimerate[0] << ", "
    //               << res.blocktimerate[1] << ", " << res.severeblocktimerate[0] << ", "
    //               << res.severeblocktimerate[1] << ", " << res.blockrate[0] << ", "
    //               << res.blockrate[1] << ", " << res.datarate[0] << ", " << res.datarate[1] << ", "
    //               << res.throughput[1] << ", " << res.throughput[2] << ", "<< res.throughput[0] << std::endl;
    //     }
    //     fout.close();
    // }
    
    // std::vector<double> res_throughputs;
    // for (const auto & res : results)
    // {
    //     res_throughputs.push_back(res.throughput[0]);
    // }

    double pM1, pM2;
    if(rtsCountMLD2G == 0) pM1 = 0;
    else pM1 = ppduCountMLD2G / rtsCountMLD2G;
    if(rtsCountMLD5G == 0) pM2 = 0;
    else pM2 = ppduCountMLD5G / rtsCountMLD5G;
    double interval = (statsEndTime - statsBeginTime).GetMicroSeconds();
    double throughputMLD = mpduNumMLD * payloadSize * 8.0 / interval;
    double throughputMLD2G = mpduNumMLD2G * payloadSize * 8.0 / interval;
    double throughputMLD5G = mpduNumMLD5G * payloadSize * 8.0 / interval;
    updateThroughputCSV(pretitle, bw1, bw2, mcs1, mcs2, nss, nStaSlds1, nStaSlds2, maxAmpduNumSld0, maxAmpduNumSld1, mpduBufferSize, maxAmpduNum0, maxAmpduNum1, originalSeed, throughputMLD, throughputMLD2G, throughputMLD5G, pM1, pM2);
    std::cout <<"[ " << statsBeginTime.As(Time::S) <<" - " << statsEndTime.As(Time::S) << "]" << std::endl;
    std::cout << "MLD Throughput " << throughputMLD << " Mbit/s " << throughputMLD2G << " Mbit/s (2.4G) " << throughputMLD5G << " Mbit/s (5G)" << std::endl;
    std::cout << "pM: " << pM1 << " (2.4G), " << pM2 << " (5G)" << std::endl;
    PrintSldStats(interval, payloadSize);

    // auto calc_std_dev = [](std::vector<double>& v, int n) -> std::pair<double, double> {
    //     auto start = v.end() - n;
    //     double sum = std::accumulate(start, v.end(), 0.0);
    //     double mean = sum / n;
    //     double var = 0.0;
    //     double mn = 1e5;
    //     int idx = 0;
    //     for (auto it = start; it != v.end(); ++it) {
    //         double d = std::abs(*it - mean);
    //         var += std::pow(d, 2);
    //         if (d < mn) {
    //             mn = d;
    //             idx = v.end() - it;
    //         }
    //     }
    //     std::cout << "use the " << idx << "th to the last of the result." << std::endl;
    //     var /= n;
    // return std::make_pair(sqrt(var), mean);
    // };
    // auto ans = calc_std_dev(res_throughputs, 3);
    // double cv =  ans.first / ans.second;
    
    // std::cout << "standard deviation: " << ans.first << std::endl;
    // std::cout << "coeff of variation: " << cv * 100 << "% " << std::endl;
    // if (cv > 0.1) {
    //     std::cout << "Please set longer simulation time use: --simT" << std::endl;
    // }
    // std::cout << "result saved: " << csv_file << std::endl;

    return 0;
}