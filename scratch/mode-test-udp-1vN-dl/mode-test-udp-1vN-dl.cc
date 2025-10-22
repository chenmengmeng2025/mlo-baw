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
    double throughput;
    double pct1;
    double time;
    std::vector<double> thpt;
    std::vector<double> p;
    std::vector<double> occ;
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
    Stats(double tp, double pct, double tm, std::vector<double> t, std::vector<double> pr, 
        std::vector<double> oc, std::vector<double> dr,  std::vector<double> btr, std::vector<double> sbtr,  std::vector<double> br,
        std::vector<uint32_t> bc,std::vector<uint32_t> bc2, std::vector<uint32_t> tn,
        std::vector<uint64_t> tt, mldParams pm, std::vector<uint32_t> maxl, std::vector<uint32_t> meanl)
    : throughput(tp), pct1(pct), time(tm), thpt(std::move(t)), p(std::move(pr)),
        occ(std::move(oc)), datarate(std::move(dr)), blocktimerate(std::move(btr)), severeblocktimerate(std::move(sbtr)), blockrate(std::move(br)),
        blockCnt(std::move(bc)), blockCnt_tr(std::move(bc2)), 
        txopNum(std::move(tn)), txopTime(std::move(tt)), params(std::move(pm)), maxAmpduLength(std::move(maxl)), meanAmpduLength(std::move(meanl))
        {}
};

std::vector<Stats> results;
std::deque<uint32_t> throughputQueue;
std::unordered_map<double, double> throughputMap;
// std::unordered_map<double, std::pair<int, double>> blockinfoMap;

int cnt = 0;
void
GetRxBytes(bool udp, const ApplicationContainer& serverApp, uint32_t payloadSize)
{
    uint32_t rxBytes = 0;
    if (udp)
    {
        rxBytes = payloadSize * DynamicCast<UdpServer>(serverApp.Get(0))->GetReceived();
    }
    else
    {
        rxBytes = DynamicCast<PacketSink>(serverApp.Get(0))->GetTotalRx();
    }
    throughputQueue.push_back(rxBytes);
    if (throughputQueue.size() > 2)
    {
        throughputQueue.pop_front();
    }
}

void
GetRxBytes2(bool udp, const ApplicationContainer& serverApp, uint32_t payloadSize, uint64_t& rxBytes)
{
    if (udp)
    {
            rxBytes = payloadSize * DynamicCast<UdpServer>(serverApp.Get(0))->GetReceived();
    }
    else
    {
            rxBytes = DynamicCast<PacketSink>(serverApp.Get(0))->GetTotalRx();
    }
}

void
SaveParams(mldParams pm, double pct1, double time, std::vector<double> thpt, std::vector<double> p, std::vector<double> occ, std::vector<double> datarate, std::vector<double> blocktimerate,std::vector<double> severeblocktimerate, std::vector<double> blockrate, std::vector<uint32_t> blockCnt, std::vector<uint32_t> blockCnt_tr, std::vector<uint64_t> txopTime, std::vector<uint32_t> txopNum, std::vector<uint32_t> maxAmpduLength, std::vector<uint32_t> meanAmpduLength)
{
    Stats res{0.0, pct1, time, thpt, p, occ, datarate, blocktimerate, severeblocktimerate, blockrate, blockCnt, blockCnt_tr, txopNum, txopTime, pm, maxAmpduLength, meanAmpduLength};
    results.push_back(res);
    Simulator::Schedule(NanoSeconds(1), [&](){
        for (auto & result : results)
        {
            if(result.throughput == 0.0 && throughputMap[result.time] != 0.0)
            {
                result.throughput = throughputMap[result.time];
                // std::cout << "Set Throughput : " << throughputMap[result.time] <<" " <<  result.time << " s" << std::endl;
            }
        }
    });
}

void
PrintIntermediateTput(bool udp,
                    const ApplicationContainer& serverApp,
                    uint32_t payloadSize,
                    Time tputInterval,
                    Time simulationTime)
{
    Time now = Simulator::Now();
    cnt ++;
    GetRxBytes(udp, serverApp, payloadSize);
    if (throughputQueue.size() == 2) {
        double tp = (throughputQueue.back() - throughputQueue.front()) * 8. / tputInterval.GetMicroSeconds();
        // std::cout << "[" << (now - tputInterval).As(Time::S) << " - " << now.As(Time::S)
        // << "] Throughput (Mbit/s):";
        // std::cout << "\t\t" << tp << "; "; // Mbit/s
        // std::cout << std::endl;
        throughputMap[now.GetSeconds()] = tp;
    }
    if (now + tputInterval < simulationTime)
    {
        Simulator::Schedule(tputInterval,
                            &PrintIntermediateTput,
                            udp,
                            serverApp,
                            payloadSize,
                            tputInterval,
                            simulationTime);
    }
}

std::string txopOutputFile("./Txop.csv");
std::string txopMpduNumberOutputFile("./TxInfo.csv");
void
SaveTxopStats(std::unordered_map<uint8_t, std::vector<std::pair<uint64_t, uint64_t>>> txopList,
              std::unordered_map<uint8_t, std::vector<std::tuple<uint64_t, uint64_t, uint32_t>>> numList)
{
    std::ofstream fout(txopOutputFile, std::ios::out);
    fout << "LinkId,TxopStartTime,TxopEndTime" << std::endl;
    for (uint8_t i = 0; i < 2; i++)
    {
        for (size_t j = 0; j < txopList[i].size(); ++j)
            fout << (uint32_t)i << "," << txopList[i][j].first << "," << txopList[i][j].second << std::endl;
    }
    fout.close();
    std::ofstream fout2(txopMpduNumberOutputFile, std::ios::out);
    fout2 << "LinkId,TxTime,TxDuration,Nmpdus" << std::endl;
    for (uint8_t i = 0; i < 2; i++) {
        for (const auto &it : numList[i]) {
            fout2 << +i << "," << std::get<0>(it) << "," << std::get<1>(it) << "," << std::get<2>(it) << std::endl;
        }
    }
    fout2.close();
}

void updateThroughputCSV(const std::string& pretitle, int bw1, int bw2,
                         int mcs1, int mcs2, int nss,
                         int nsld1, int nsld2, int baw,
                         int maxAmpduNum0, int maxAmpduNum1,
                         int seedNumber, double totalthroughput,
                         double totalthroughput1, double totalthroughput2)
{
    std::string filename = "throughput_10s.csv";
    bool file_exists = std::filesystem::exists(filename);
    std::vector<std::vector<std::string>> rows;
    bool found = false;
    bool has_mcs = false;
    bool has_nss = false;

    // ========== 1. 读取文件 ==========
    if (file_exists)
    {
        std::ifstream infile(filename);
        std::string line;
        bool is_header = true;

        while (std::getline(infile, line))
        {
            if (line.empty()) continue;
            std::stringstream ss(line);
            std::vector<std::string> tokens;
            std::string token;
            while (std::getline(ss, token, ','))
            {
                token.erase(0, token.find_first_not_of(" \t"));
                token.erase(token.find_last_not_of(" \t") + 1);
                tokens.push_back(token);
            }

            if (is_header)
            {
                // ---- 检查表头列结构 ----
                has_mcs = (std::find(tokens.begin(), tokens.end(), "mcs1") != tokens.end());
                has_nss = (std::find(tokens.begin(), tokens.end(), "nss") != tokens.end());

                // 若旧文件缺少 throughput1/2 列
                if (tokens.size() == 10 && tokens.back() == "Throughput(Mbps)")
                {
                    tokens.push_back("Throughput1(Mbps)");
                    tokens.push_back("Throughput2(Mbps)");
                }

                // 若旧文件缺少 mcs1/mcs2 → 自动补列
                if (!has_mcs)
                {
                    auto it = tokens.begin() + 3; // 在 bw2 后插入
                    tokens.insert(it, {"mcs1", "mcs2"});
                    has_mcs = true;
                }

                // 若旧文件缺少 nss → 在 mcs2 后插入
                if (!has_nss)
                {
                    auto it = std::find(tokens.begin(), tokens.end(), "mcs2");
                    if (it != tokens.end()) ++it;
                    tokens.insert(it, "nss");
                    has_nss = true;
                }

                rows.push_back(tokens);
                is_header = false;
                continue;
            }

            // ---- 普通数据行 ----
            if (tokens.size() >= 9)
            {
                if (!has_mcs)
                {
                    tokens.insert(tokens.begin() + 3, "13"); // mcs1
                    tokens.insert(tokens.begin() + 4, "13"); // mcs2
                }

                if (!has_nss)
                {
                    tokens.insert(tokens.begin() + 5, "2"); // nss 默认值2
                }

                size_t idx_bw1 = 1;
                size_t idx_bw2 = 2;
                size_t idx_mcs1 = 3;
                size_t idx_mcs2 = 4;
                size_t idx_nss = 5;
                size_t base = 6;

                std::string f_pretitle = tokens[0];
                int f_bw1 = std::stoi(tokens[idx_bw1]);
                int f_bw2 = std::stoi(tokens[idx_bw2]);
                int f_mcs1 = std::stoi(tokens[idx_mcs1]);
                int f_mcs2 = std::stoi(tokens[idx_mcs2]);
                int f_nss = std::stoi(tokens[idx_nss]);
                int f_nsld1 = std::stoi(tokens[base]);
                int f_nsld2 = std::stoi(tokens[base + 1]);
                int f_baw = std::stoi(tokens[base + 2]);
                int f_maxAmpduNum0 = std::stoi(tokens[base + 3]);
                int f_maxAmpduNum1 = std::stoi(tokens[base + 4]);
                int f_seed = std::stoi(tokens[base + 5]);

                // ---- 判断是否匹配 ----
                if (f_pretitle == pretitle && f_bw1 == bw1 && f_bw2 == bw2 &&
                    f_mcs1 == mcs1 && f_mcs2 == mcs2 && f_nss == nss &&
                    f_nsld1 == nsld1 && f_nsld2 == nsld2 && f_baw == baw &&
                    f_maxAmpduNum0 == maxAmpduNum0 && f_maxAmpduNum1 == maxAmpduNum1 &&
                    f_seed == seedNumber)
                {
                    if (tokens.size() >= base + 9)
                    {
                        tokens[base + 6] = std::to_string(totalthroughput);
                        tokens[base + 7] = std::to_string(totalthroughput1);
                        tokens[base + 8] = std::to_string(totalthroughput2);
                    }
                    else
                    {
                        while (tokens.size() < base + 6) tokens.push_back("");
                        tokens.push_back(std::to_string(totalthroughput));
                        tokens.push_back(std::to_string(totalthroughput1));
                        tokens.push_back(std::to_string(totalthroughput2));
                    }
                    found = true;
                }
            }
            rows.push_back(tokens);
        }
        infile.close();
    }
    else
    {
        // ========== 2. 文件不存在 → 创建新表 ==========
        rows.push_back({
            "pertitle", "bw1", "bw2", "mcs1", "mcs2", "nss",
            "nsld1", "nsld2", "baw",
            "maxAmpduNum0", "maxAmpduNum1", "seed",
            "Throughput(Mbps)", "Throughput1(Mbps)", "Throughput2(Mbps)"
        });
        has_mcs = has_nss = true;
    }

    // ========== 3. 插入新行 ==========
    if (!found)
    {
        std::vector<std::string> new_row = {
            pretitle,
            std::to_string(bw1),
            std::to_string(bw2),
            std::to_string(mcs1),
            std::to_string(mcs2),
            std::to_string(nss),
            std::to_string(nsld1),
            std::to_string(nsld2),
            std::to_string(baw),
            std::to_string(maxAmpduNum0),
            std::to_string(maxAmpduNum1),
            std::to_string(seedNumber),
            std::to_string(totalthroughput),
            std::to_string(totalthroughput1),
            std::to_string(totalthroughput2)
        };

        bool inserted = false;
        for (auto it = rows.begin() + 1; it != rows.end(); ++it)
        {
            auto& r = *it;
            if (r.size() < 9) continue;

            std::string f_pretitle = r[0];
            int f_bw1 = std::stoi(r[1]);
            int f_bw2 = std::stoi(r[2]);
            int f_mcs1 = std::stoi(r[3]);
            int f_mcs2 = std::stoi(r[4]);
            int f_nss = std::stoi(r[5]);
            int offset = 6;
            int f_nsld1 = std::stoi(r[offset]);
            int f_nsld2 = std::stoi(r[offset + 1]);
            int f_baw = std::stoi(r[offset + 2]);
            int f_maxAmpduNum0 = std::stoi(r[offset + 3]);
            int f_maxAmpduNum1 = std::stoi(r[offset + 4]);
            int f_seed = std::stoi(r[offset + 5]);

            if (std::tie(pretitle, bw1, bw2, mcs1, mcs2, nss,
                         nsld1, nsld2, baw, maxAmpduNum0, maxAmpduNum1, seedNumber) <
                std::tie(f_pretitle, f_bw1, f_bw2, f_mcs1, f_mcs2, f_nss,
                         f_nsld1, f_nsld2, f_baw, f_maxAmpduNum0, f_maxAmpduNum1, f_seed))
            {
                rows.insert(it, new_row);
                inserted = true;
                break;
            }
        }
        if (!inserted)
            rows.push_back(new_row);
    }

    // ========== 4. 写回文件 ==========
    std::ofstream outfile(filename, std::ios::out | std::ios::trunc);
    for (auto& row : rows)
    {
        for (size_t i = 0; i < row.size(); ++i)
        {
            outfile << row[i];
            if (i != row.size() - 1)
                outfile << ", ";
        }
        outfile << "\n";
    }
    outfile.close();
}




std::string ppduTxOutputFile("./PPDU.csv");
std::string rtsctsTxOutputFile("./RTSCTS.csv");

void
NotifyPpduTxDurationMLDSTA(Ptr<const WifiPpdu> ppdu, Time duration, uint8_t linkid)
{
    if(ppdu->GetPsdu()->GetHeader(0).IsRts() && Simulator::Now().GetSeconds() < 6){
            std::fstream file;
            file.open(rtsctsTxOutputFile, std::ios::out | std::ios::app);
            file << "MLD_RTS_" << uint32_t(linkid) << "," << Simulator::Now().GetMicroSeconds() << ","
                    << Simulator::Now().GetMicroSeconds() + duration.GetMicroSeconds()<<std::endl;
            file.close();
        return;
    }
    if(ppdu->GetPsdu()->GetHeader(0).IsCts() && Simulator::Now().GetSeconds() < 6){
        std::fstream file;
        file.open(rtsctsTxOutputFile, std::ios::out | std::ios::app);
        file << "MLD_CTS_" << uint32_t(linkid) << "," << Simulator::Now().GetMicroSeconds() << ","
                << Simulator::Now().GetMicroSeconds() + duration.GetMicroSeconds()<<std::endl;
        file.close();
        return;
    }
    if (!ppdu->GetPsdu()->GetHeader(0).IsQosData())
        return;
    Ptr<const WifiPsdu> psdu = ppdu->GetPsdu();
    uint32_t nmpdus = 0;
    if (psdu->IsAggregate())
    {
        nmpdus = psdu->GetNMpdus();
    } else {
        nmpdus = 1;
    }
    if (Simulator::Now().GetSeconds() < 6)
    {
        std::fstream file;
        file.open(ppduTxOutputFile, std::ios::out | std::ios::app);
        if (nmpdus != 0)
            file << "MLD" << uint32_t(linkid) << "," << Simulator::Now().GetMicroSeconds() << ","
                    << Simulator::Now().GetMicroSeconds() + duration.GetMicroSeconds() << ","
                    << nmpdus << std::endl;
        file.close();
    }
}

void
NotifyPpduTxDurationSLD2G(uint32_t staIndex, Ptr<const WifiPpdu> ppdu, Time duration, uint8_t linkid)
{
    if(ppdu->GetPsdu()->GetHeader(0).IsRts() && Simulator::Now().GetSeconds() < 6){
        std::fstream file;
        file.open(rtsctsTxOutputFile, std::ios::out | std::ios::app);
        file << "SLD2G_RTS_" << staIndex << "_" << uint32_t(linkid) << ","
                << Simulator::Now().GetMicroSeconds() << ","
                << Simulator::Now().GetMicroSeconds() + duration.GetMicroSeconds() <<std::endl;
        file.close();
        return;
    }
    if(ppdu->GetPsdu()->GetHeader(0).IsCts() && Simulator::Now().GetSeconds() < 6){
        std::fstream file;
        file.open(rtsctsTxOutputFile, std::ios::out | std::ios::app);
        file << "SLD2G_CTS_" << staIndex << "_" << uint32_t(linkid) << ","
                << Simulator::Now().GetMicroSeconds() << ","
                << Simulator::Now().GetMicroSeconds() + duration.GetMicroSeconds() <<std::endl;
        file.close();
        return;
    }
    if (!ppdu->GetPsdu()->GetHeader(0).IsQosData())
        return;

    Ptr<const WifiPsdu> psdu = ppdu->GetPsdu();
    uint32_t nmpdus = psdu->IsAggregate() ? psdu->GetNMpdus() : 1;

    if (Simulator::Now().GetSeconds() < 6 && nmpdus != 0)
    {
        std::fstream file;
        file.open(ppduTxOutputFile, std::ios::out | std::ios::app);
        file << "SLD2G_" << staIndex << "_" << uint32_t(linkid) << ","
             << Simulator::Now().GetMicroSeconds() << ","
             << Simulator::Now().GetMicroSeconds() + duration.GetMicroSeconds() << ","
             << nmpdus << std::endl;
        file.close();
    }
}

void
NotifyPpduTxDurationSLD5G(uint32_t staIndex, Ptr<const WifiPpdu> ppdu, Time duration, uint8_t linkid)
{
    if(ppdu->GetPsdu()->GetHeader(0).IsRts() && Simulator::Now().GetSeconds() < 6){
        std::fstream file;
        file.open(rtsctsTxOutputFile, std::ios::out | std::ios::app);
        file << "SLD5G_RTS_" << staIndex << "_" << uint32_t(linkid) << ","
                << Simulator::Now().GetMicroSeconds() << ","
                << Simulator::Now().GetMicroSeconds() + duration.GetMicroSeconds() <<std::endl;
        file.close();
        return;
    }
    if(ppdu->GetPsdu()->GetHeader(0).IsCts() && Simulator::Now().GetSeconds() < 6){
        std::fstream file;
        file.open(rtsctsTxOutputFile, std::ios::out | std::ios::app);
        file << "SLD5G_CTS_" << staIndex << "_" << uint32_t(linkid) << ","
                << Simulator::Now().GetMicroSeconds() << ","
                << Simulator::Now().GetMicroSeconds() + duration.GetMicroSeconds() <<std::endl;
        file.close();
        return;
    }
    if (!ppdu->GetPsdu()->GetHeader(0).IsQosData())
        return;

    Ptr<const WifiPsdu> psdu = ppdu->GetPsdu();
    uint32_t nmpdus = psdu->IsAggregate() ? psdu->GetNMpdus() : 1;

    if (Simulator::Now().GetSeconds() < 6 && nmpdus != 0)
    {
        std::fstream file;
        file.open(ppduTxOutputFile, std::ios::out | std::ios::app);
        file << "SLD5G_" << staIndex << "_" << uint32_t(linkid) << ","
             << Simulator::Now().GetMicroSeconds() << ","
             << Simulator::Now().GetMicroSeconds() + duration.GetMicroSeconds() << ","
             << nmpdus << std::endl;
        file.close();
    }
}

void
NotifyPpduTxDurationMLDAP(Ptr<const WifiPpdu> ppdu, Time duration, uint8_t linkid)
{
    if(ppdu->GetPsdu()->GetHeader(0).IsRts() && Simulator::Now().GetSeconds() < 6){
        std::fstream file;
        file.open(rtsctsTxOutputFile, std::ios::out | std::ios::app);
        file << "AP_RTS_" << uint32_t(linkid) << "," << Simulator::Now().GetMicroSeconds() << ","
                << Simulator::Now().GetMicroSeconds() + duration.GetMicroSeconds() <<std::endl;
        file.close();
        return;
    }
    if(ppdu->GetPsdu()->GetHeader(0).IsCts() && Simulator::Now().GetSeconds() < 6){
        std::fstream file;
        file.open(rtsctsTxOutputFile, std::ios::out | std::ios::app);
        file << "AP_CTS_" << uint32_t(linkid) << "," << Simulator::Now().GetMicroSeconds() << ","
                << Simulator::Now().GetMicroSeconds() + duration.GetMicroSeconds() <<std::endl;
        file.close();
        return;
    }
    if (!ppdu->GetPsdu()->GetHeader(0).IsQosData())
        return;
    Ptr<const WifiPsdu> psdu = ppdu->GetPsdu();
    uint32_t nmpdus = 0;
    if (psdu->IsAggregate())
    {
        nmpdus = psdu->GetNMpdus();
        
    } else {
        nmpdus = 1;
    }
    if (Simulator::Now().GetSeconds() < 6)
    {
        std::fstream file;
        file.open(ppduTxOutputFile, std::ios::out | std::ios::app);
        if (nmpdus != 0)
            file <<"AP_" << uint32_t(linkid) << "," << Simulator::Now().GetMicroSeconds() << ","
                << Simulator::Now().GetMicroSeconds() + duration.GetMicroSeconds() << ","
                << nmpdus << std::endl;
        file.close();
    }
}

double totalThroughput = 0.0;
double totalThroughput1 = 0.0;
double totalThroughput2 = 0.0;
void 
NotifyMLDThroughput(Time statsBeginTime,
                    Time statsEndTime,
                    uint32_t payloadSize,
                    Ptr<const WifiPpdu> ppdu,
                    Time duration,
                    uint8_t linkid)
{
    // 仅统计 QoS Data 帧
    if (!ppdu->GetPsdu()->GetHeader(0).IsQosData())
        return;

    Time now = Simulator::Now();

    // 仅在统计区间内统计
    if (now < statsBeginTime || now > statsEndTime)
        return;

    Ptr<const WifiPsdu> psdu = ppdu->GetPsdu();
    uint32_t nmpdus = psdu->IsAggregate() ? psdu->GetNMpdus() : 1;
    if (nmpdus == 0)
        return;

    double interval = (statsEndTime - statsBeginTime).GetSeconds();
    double bits = static_cast<double>(nmpdus) * payloadSize * 8.0;
    totalThroughput += bits / interval / 1e6;
    if (linkid == 0)
        totalThroughput1 += bits / interval / 1e6;
    else if (linkid == 1)
        totalThroughput2 += bits / interval / 1e6;

}


int
main(int argc, char* argv[])
{
    if (std::filesystem::exists(txopOutputFile)) { 
        std::filesystem::remove(txopOutputFile);
    }

    if (std::filesystem::exists(txopMpduNumberOutputFile)) { 
        std::filesystem::remove(txopMpduNumberOutputFile);
    }
    uint32_t seedNumber = 1;
    uint32_t mcs1 = 13;
    uint32_t mcs2 = 13;
    uint32_t bw1 = 20;
    uint32_t bw2 = 20;
    double r1 = 1;
    double r2 = 1;
    uint32_t ch1 = 0;
    uint32_t ch2 = 0; 
    // std::string rateCtrl{"ideal"};
    std::string rateCtrl{"constant"};
    // std::string rateCtrl{"minstrel"};

    uint16_t mpduBufferSize{1024};
    uint32_t maxAmpduSize{1024 * 8 * (1500 + 150)}; // 1048575
    uint32_t maxAmpduSize1{1 * (1500 + 150)};
    uint32_t maxAmpduSize2{1 * (1500 + 150)};
    uint32_t maxAmpduNum0 = 0;
    uint32_t maxAmpduNum1 = 0;

    uint32_t txoplimit1 = 0, txoplimit2 = 0;
    uint32_t singleLink = 0;
    uint8_t nStaSlds1 = 0;
    uint8_t nStaSlds2 = 0;
    double period_update = 0.1;
    bool grid_search_enable = false;
    uint32_t nss = 2;
    uint8_t mode = 1;

    double simT = 0;
    double transmission_delay = 0;
    bool param_update = false;
    bool redundancy_enable = false;
    bool logsender = false;
    bool logreceiver = false;
    bool logmode = false;
    Time simT_delayEnd = NanoSeconds(2);
    uint32_t maxGroupSize = 1;
    uint32_t pretitleint = 0;
    std::string pretitle = "my"; //my2 means caculate-2
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
    cmd.AddValue("ratectrl", "rate control alg", rateCtrl);
    cmd.AddValue("bawsize", "BA Window Size", mpduBufferSize);
    cmd.AddValue("max_ampdusize", "Max AmpduSize of AP 0", maxAmpduSize);
    cmd.AddValue("max_ampdusize1", "Max AmpduSize of AP 1", maxAmpduSize1);
    cmd.AddValue("max_ampdusize2", "Max AmpduSize of AP 2", maxAmpduSize2);
    cmd.AddValue("gridsearch", "enable gridsearch", grid_search_enable);
    cmd.AddValue("mode", "MLO Mode Setting", mode);
    cmd.AddValue("simt", "simulation time", simT);
    cmd.AddValue("period", "update period", period_update);
    cmd.AddValue("txop1", "TxopLimit on 2.4 G", txoplimit1);
    cmd.AddValue("txop2", "TxopLimit on 5 G", txoplimit2);
    cmd.AddValue("sl", "Single Link if > 0", singleLink);
    cmd.AddValue("nss", "mimo", nss);
    cmd.AddValue("maxgroupsize", "maxgroupsize", maxGroupSize);
    cmd.AddValue("delay", "delay setting", transmission_delay); // 传输延时，单位为微秒
    cmd.AddValue("nsld1", "interference setting", nStaSlds1);
    cmd.AddValue("nsld2", "interference setting", nStaSlds2);
    cmd.AddValue("redundancy", "redundancy setting", redundancy_enable);
    cmd.AddValue("param_update", "param update setting", param_update); 
    cmd.AddValue("logsender", "new transmitter architecture log setting", logsender);
    cmd.AddValue("logreceiver", "new receiver architecture log setting", logreceiver);
    cmd.AddValue("logmode", "mode log setting", logmode);
    cmd.AddValue("pretitle", "pre title", pretitleint);
    cmd.AddValue("maxampdunum0", "max mpdu number of 2.4G", maxAmpduNum0);
    cmd.AddValue("maxampdunum1", "max mpdu number of 5G", maxAmpduNum1);
    cmd.AddValue("scenario", "Simulation scenario", scenario);

    cmd.Parse(argc, argv);

    uint32_t originalSeed = seedNumber;
    if (simT == 0) simT = period_update * 10 + 1;
    Time period{Seconds(period_update)};
    if (!(nStaSlds1)) r1 = 1e-9;  
    if (!(nStaSlds2)) r2 = 1e-9;
    Time tputInterval = period; // interval for detailed throughput measurement
    std::string title;
    if(pretitleint == 1) pretitle = "greedy";
    if(pretitleint == 2) {pretitle = "damla";}
    if(pretitleint == 3) pretitle = "only5G";
    if(pretitleint == 4) pretitle = "only2G";
    if(pretitleint == 5) pretitle = "sumbawby2g"; // used to fixset
    if(pretitleint == 6) pretitle = "bothset"; // used to fix2g
    title = pretitle + "_baw_" + std::to_string(mpduBufferSize) + "_bw_" + std::to_string(bw1) + "_" + std::to_string(bw2) + "_mcs_" + std::to_string(mcs1) + "_" + std::to_string(mcs2) 
                        + "_interference_" + std::to_string(nStaSlds1) + "_" + std::to_string(nStaSlds2) + "_seed_" + std::to_string(seedNumber);
    if(pretitleint == 5) {
        title = title + "_maxAmpduNum0_" + std::to_string(maxAmpduNum0);
    }
    if(pretitleint == 6) {
        title = title + "_maxAmpduNum0_" + std::to_string(maxAmpduNum0) + "_maxAmpduNum1_" + std::to_string(maxAmpduNum1);
    }

    std::filesystem::path scenarioDir = filepath.parent_path() / scenario;
    // 若文件夹不存在则创建
    if (!std::filesystem::exists(scenarioDir))
    {
        std::filesystem::create_directories(scenarioDir);
    }
    ppduTxOutputFile = (scenarioDir / (title + "_PPDU.csv")).string();
    if (std::filesystem::exists(ppduTxOutputFile)) { 
        std::filesystem::remove(ppduTxOutputFile);
    }
    rtsctsTxOutputFile = (scenarioDir / (title + "_RTSCTS.csv")).string();
    if (std::filesystem::exists(rtsctsTxOutputFile)) { 
        std::filesystem::remove(rtsctsTxOutputFile);
    }
    if (mode && logmode) mode = mode | (1 << 4);
    if (mode && logsender) mode = mode | (1 << 5);
    uint8_t mode_recv = 1 << 2;
    if (mode_recv && logreceiver) mode_recv = mode_recv | (1 << 6);
    std::string csv_file = (scenarioDir / (title + ".csv")).string();
    std::cout << csv_file << std::endl;
    // LogComponentEnable("PhyEntity", LOG_LEVEL_DEBUG);
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

    if (useRts) // 默认不使用RTS CTS
    {
        Config::SetDefault("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue("0"));
        // Config::SetDefault("ns3::WifiDefaultProtectionManager::EnableMuRts", BooleanValue(true));
    }
    // Config::SetDefault("ns3::WifiMacQueue::MaxDelay", TimeValue(simulationTime * 2));

    // Set infinitely long queue
    //  Config::SetDefault(
    //      "ns3::WifiMacQueue::MaxSize",
    //      QueueSizeValue(QueueSize(QueueSizeUnit::PACKETS, 1024)));

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
    Ptr<WifiMac> mac_ap = DynamicCast<WifiNetDevice>(apDev.Get(0))->GetMac();
    std::cout << "AP0 MAC: " << apDev.Get(0)->GetAddress()<< std::endl;
    for (uint8_t i = 0; i < nLinks; ++i) {
        auto fem = mac_ap->GetFrameExchangeManager(i);
        std::cout << "\t apDevice " << "linkId " << std::to_string(i) << " mac address: " << fem->GetAddress() << std::endl;
    }
    // print mac address of mld
    for (size_t id = 0; id < nStaMlds; ++id) {
        Ptr<WifiMac> mac_mld = DynamicCast<WifiNetDevice>(mldDev.Get(id))->GetMac(); 
        std::cout << "MLD" << (id) << " MAC: " << mldDev.Get(id)->GetAddress() << std::endl;
        for (uint8_t i = 0; i < nLinks; ++i) {
            auto fem = mac_mld->GetFrameExchangeManager(i);
            std::cout << "\t mldDevice " << "linkId " << std::to_string(i) << " mac address: " << fem->GetAddress() << std::endl;
        }
    }
    for (size_t id = nStaMlds; id < nStaMlds + nStaSlds1; ++id) {
        Ptr<WifiMac> mac_mld = DynamicCast<WifiNetDevice>(mldDev.Get(id))->GetMac(); 
        std::cout << "SLD_2.4G" << (id) << " MAC: " << mldDev.Get(id)->GetAddress() << std::endl;
        auto fem = mac_mld->GetFrameExchangeManager(0);
        std::cout << "\t sldDevice " << "linkId " << std::to_string(0) << " mac address: " << fem->GetAddress() << std::endl;
        DynamicCast<WifiNetDevice>(mldDev.Get(id))->GetPhy(0)->TraceConnectWithoutContext(
            "PpduTxDuration",
            MakeBoundCallback(&NotifyPpduTxDurationSLD2G, id - nStaMlds));
        DynamicCast<WifiNetDevice>(mldDev.Get(id))->GetPhy(1)->TraceConnectWithoutContext(
            "PpduTxDuration",
            MakeBoundCallback(&NotifyPpduTxDurationSLD2G, id - nStaMlds));
    }
    for (size_t id = nStaMlds + nStaSlds1; id < nStaMlds + nStaSlds1 + nStaSlds2; ++id) {
        Ptr<WifiMac> mac_mld = DynamicCast<WifiNetDevice>(mldDev.Get(id))->GetMac(); 
        std::cout << "SLD_5G" << (id) << " MAC: " << mldDev.Get(id)->GetAddress() << std::endl;
        auto fem = mac_mld->GetFrameExchangeManager(1);
        std::cout << "\t sldDevice " << "linkId " << std::to_string(1) << " mac address: " << fem->GetAddress() << std::endl;
        DynamicCast<WifiNetDevice>(mldDev.Get(id))->GetPhy(0)->TraceConnectWithoutContext(
            "PpduTxDuration",
            MakeBoundCallback(&NotifyPpduTxDurationSLD5G, id - nStaMlds - nStaSlds1));
        DynamicCast<WifiNetDevice>(mldDev.Get(id))->GetPhy(1)->TraceConnectWithoutContext(
            "PpduTxDuration",
            MakeBoundCallback(&NotifyPpduTxDurationSLD5G, id - nStaMlds - nStaSlds1));
    }

    // MLD AP PPDU TX Duration Output
    DynamicCast<WifiNetDevice>(apDev.Get(0))->GetPhy(0)->TraceConnectWithoutContext("PpduTxDuration",MakeCallback(&NotifyPpduTxDurationMLDAP));
    DynamicCast<WifiNetDevice>(apDev.Get(0))->GetPhy(1)->TraceConnectWithoutContext("PpduTxDuration",MakeCallback(&NotifyPpduTxDurationMLDAP));
    DynamicCast<WifiNetDevice>(mldDev.Get(0))->GetPhy(0)->TraceConnectWithoutContext("PpduTxDuration",MakeCallback(&NotifyPpduTxDurationMLDSTA));
    DynamicCast<WifiNetDevice>(mldDev.Get(0))->GetPhy(1)->TraceConnectWithoutContext("PpduTxDuration",MakeCallback(&NotifyPpduTxDurationMLDSTA));

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

    double bssRadius = 1; // 圆形分布半径

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
    const auto maxLoad =  (maxLoad2 + maxLoad5) * 2; 
    std::cout << "maxload = " << std::to_string((maxLoad2 + maxLoad5)/1e6) << " Mbps; 2.4 GHz: " <<  std::to_string(maxLoad2/1e6) << " Mbps, 5 GHz: " << std::to_string(maxLoad5/1e6) << " Mbps" << std::endl;
    const auto packetInterval = payloadSize * 8.0 / maxLoad;
    UdpClientHelper client(apNodeInterface.GetAddress(0), port);
    client.SetAttribute("MaxPackets", UintegerValue(0));
    client.SetAttribute("Interval", TimeValue(Seconds(packetInterval)));
    client.SetAttribute("PacketSize", UintegerValue(payloadSize));
    for (uint32_t i = 0; i < mldNodes.GetN(); ++i){
        ApplicationContainer clientApp = client.Install(mldNodes.Get(i));
        seedNumber += client.AssignStreams(mldNodes.Get(i), seedNumber);
        if(i) clientApp.Start(Seconds(1));
        else clientApp.Start(Seconds(0.999));
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
    if (singleLink & 0x03) mldMappingStr = "0,1,2,3,4,5,6,7 "  + std::to_string((singleLink & 0x02) > 0);
    std::string mldMappingStr1 = "0,1,2,3,4,5,6,7 0";
    std::string mldMappingStr2 = "0,1,2,3,4,5,6,7 1";

    int index = 0;
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
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/Nsld24", UintegerValue(nStaSlds1)); 
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/Nsld5", UintegerValue(nStaSlds2)); 
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/BandWidth24", UintegerValue(bw1));
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/BandWidth5", UintegerValue(bw2));

    Config::Set("/NodeList/0/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/Mode", UintegerValue(mode_recv)); // 只负责接收，无msdu_grouper, mode只要非0, 接收端就是新架构，BA只包含各自链路所收到的包的接收信息，各自维护自己的bitmap
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/GridSearchEnable", BooleanValue(grid_search_enable)); // 是否开启网格搜索，用于静态场景下的最优参数搜索，只有在param_update = true时才会更新参数
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/ParamUpdate", BooleanValue(param_update));
    // Config::Set("/NodeList/0/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/GridSearchParameter", StringValue("./scratch/params.json")); // 网格搜索使用的参数集合
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/RedundancyEnable", BooleanValue(redundancy_enable)); // 是否启用冗余模式


    /* OBSS EDCA */
    for (uint32_t i = 0; i < allNodes.GetN(); ++i)
    {
        Config::Set("/NodeList/" + std::to_string(i) +
                "/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/Aifsns", AttributeContainerValue<UintegerValue>(std::list<uint64_t>{3,3}));
        Config::Set("/NodeList/" + std::to_string(i) +
                "/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/Aifsns", AttributeContainerValue<UintegerValue>(std::list<uint64_t>{3,3}));
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
    Config::Set("/NodeList/0/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/Aifsns", AttributeContainerValue<UintegerValue>(std::list<uint64_t>{3,3}));
    Config::Set("/NodeList/0/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/MinCws", AttributeContainerValue<UintegerValue>(std::list<int>{15,15}));
    Config::Set("/NodeList/0/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/MaxCws", AttributeContainerValue<UintegerValue>(std::list<int>{1023,1023}));
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/Aifsns", AttributeContainerValue<UintegerValue>(std::list<uint64_t>{3,3}));
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/MinCws", AttributeContainerValue<UintegerValue>(std::list<int>{15,15}));
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/MaxCws", AttributeContainerValue<UintegerValue>(std::list<int>{1023,1023}));


    std::cout << "TxopLimits : (" << txoplimit1 << "," << txoplimit2  << ")" << std::endl;
    std::vector<Time> txopLimitList = {MicroSeconds(32) * txoplimit1, MicroSeconds(32) * txoplimit2};
    std::cout << txopLimitList[0] << " " << txopLimitList[1] << std::endl;
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/TxopLimits", AttributeContainerValue<TimeValue>(txopLimitList));

    // phy.EnablePcap("ap0-trace-udp", apDev.Get(0));
    // phySld2.EnablePcap("ap1-trace-udp", apDev.Get(1));
    // phySld5.EnablePcap("ap2-trace-udp", apDev.Get(2));
    // phy.EnablePcap("mld-trace-udp", mldDev.Get(0));
    // phySld2.EnablePcap("sld2-trace-udp", sldDev2.Get(0));
    // phySld5.EnablePcap("sld5-trace-udp", sldDev5.Get(0));
    Config::ConnectWithoutContext("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/GetNextParams", MakeCallback(&SaveParams));
    Config::ConnectWithoutContext("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/GetTxopTimeStats",MakeCallback(&SaveTxopStats));
    
    Simulator::Schedule(Seconds(1.0), &PrintIntermediateTput, udp, ulserverApp, payloadSize, tputInterval, simulationTime + simT_delayEnd);
    
    // uint64_t rx_totalbytesStart = 0;
    Time statsBeginTime = Seconds(1.0) + period*5;
    std::cout << "statsBeginTime: " << statsBeginTime.As(Time::S) << std::endl;
    Time statsEndTime = period * ((simulationTime + simT_delayEnd) / period).GetInt();
    std::cout << "statsEndTime: " << statsEndTime.As(Time::S) << std::endl;
    DynamicCast<WifiNetDevice>(mldDev.Get(0))->GetPhy(0)->TraceConnectWithoutContext("PpduTxDuration", MakeBoundCallback(&NotifyMLDThroughput, statsBeginTime, statsEndTime, payloadSize));
    DynamicCast<WifiNetDevice>(mldDev.Get(0))->GetPhy(1)->TraceConnectWithoutContext("PpduTxDuration", MakeBoundCallback(&NotifyMLDThroughput, statsBeginTime, statsEndTime, payloadSize));
    Simulator::Stop(simulationTime + simT_delayEnd);
    Simulator::Run();
    Simulator::Destroy();
    std::cout << "Total DL Throughput [ " << statsBeginTime.As(Time::S) <<" - " << statsEndTime.As(Time::S) << "]: \t\t\t" << totalThroughput << " Mbit/s" << std::endl;
    if (mode == 0) return 0;
    std::ofstream fout(csv_file, std::ios::out);
    fout << "No, Time, Mode, CWmin1, CWmax1, CWmin2, CWmax2, Aifsn1, Aifsn2, TxopLimit1, TxopLimit2, AmpduLimit1, AmpduLimit2, RTS_CTS1, RTS_CTS2, MaxSlrc1, "
            "MaxSsrc1, MaxSlrc2, MaxSsrc2, RedundancyThreshold1, RedundancyThreshold2, RedundancyFixedNumber1, "
            "RedundancyFixedNumber2, BlockCnt1, BlockCnt2, BlockCnt1_True, BlockCnt2_True, TxopTime1(us), TxopTime2(us), TxopCnt1, TxopCnt2, MaxAmpduLength1, MaxAmpduLength2, MeanAmpduLength1, MeanAmpduLength2, PSR1, PSR2, Occupancy Rate 1, Occupancy Rate 2, blocktimerate1, blocktimerate2, severeblocktimerate1, severeblocktimerate2, blockrate1, blockrate2, datarate1, datarate2, throughput1, throughput2, pct1, Throughput(Mbps)" << std::endl;
    if (!results.empty()) {
        for (const auto & res : results)
        {
            const mldParams & params = res.params;
            fout << params.No << ", " << res.time << ", " << (uint32_t)mode << ", "
                  << params.CWmins[0] << ", " << params.CWmaxs[0] << ", " << params.CWmins[1]
                  << ", " << params.CWmaxs[1] << ", " << params.Aifsns[0] << ", "
                  << params.Aifsns[1] << ", " << params.TxopLimits[0] << ", "
                  << params.TxopLimits[1] << ", " << params.AmpduLimits[0] << ", "
                  << params.AmpduLimits[1] << ", " << params.RTS_CTS[0] << ", " << params.RTS_CTS[1]
                  << ", " << params.MaxSsrcs[0] << ", " << params.MaxSsrcs[0] << ", "
                  << params.MaxSlrcs[1] << ", " << params.MaxSlrcs[1] << ", "
                  << params.RedundancyThresholds[0] << ", " << params.RedundancyThresholds[1]
                  << ", " << params.RedundancyFixedNumbers[0] << ", "
                  << params.RedundancyFixedNumbers[1] << ", " << res.blockCnt[0] << ", "
                  << res.blockCnt[1] << ", " << res.blockCnt_tr[0] << ", " << res.blockCnt_tr[1]
                  << ", " << res.txopTime[0] << ", " << res.txopTime[1] << ", " << res.txopNum[0]
                  << ", " << res.txopNum[1] << ", " << res.maxAmpduLength[0] << ", "
                  << res.maxAmpduLength[1] << ", " << res.meanAmpduLength[0] << ", "
                  << res.meanAmpduLength[1] << ", " << res.p[0] << ", " << res.p[1] << ", "
                  << res.occ[0] << ", " << res.occ[1] << ", " << res.blocktimerate[0] << ", "
                  << res.blocktimerate[1] << ", " << res.severeblocktimerate[0] << ", "
                  << res.severeblocktimerate[1] << ", " << res.blockrate[0] << ", "
                  << res.blockrate[1] << ", " << res.datarate[0] << ", " << res.datarate[1] << ", "
                  << res.thpt[0] << ", " << res.thpt[1] << ", " << res.pct1 << ", "
                  << res.throughput << std::endl;
        }
        fout.close();
    }
    
    std::vector<double> res_throughputs;
    std::cout << "No, Time, Mode, CWmin1, CWmax1, CWmin2, CWmax2, Aifsn1, Aifsn2, TxopLimit1, TxopLimit2, AmpduLimit1, AmpduLimit2, RTS_CTS1, RTS_CTS2, MaxSlrc1, "
            "MaxSsrc1, MaxSlrc2, MaxSsrc2, RedundancyThreshold1, RedundancyThreshold2, RedundancyFixedNumber1, "
            "RedundancyFixedNumber2, BlockCnt1, BlockCnt2, BlockCnt1_True, BlockCnt2_True, TxopTime1(us), TxopTime2(us), TxopCnt1, TxopCnt2, MaxAmpduLength1, MaxAmpduLength2, MeanAmpduLength1, MeanAmpduLength2, PSR1, PSR2, Occupancy Rate 1, Occupancy Rate 2, blocktimerate1, blocktimerate2, severeblocktimerate1, severeblocktimerate2, blockrate1, blockrate2, datarate1, datarate2, throughput1, throughput2, pct1, Throughput(Mbps)" << std::endl;
    if (!results.empty())
    for (const auto & res : results)
    {
        const mldParams & params = res.params;
        std::cout << params.No << ", " << res.time << ", " << (uint32_t)mode << ", "
                  << params.CWmins[0] << ", " << params.CWmaxs[0] << ", " << params.CWmins[1]
                  << ", " << params.CWmaxs[1] << ", " << params.Aifsns[0] << ", "
                  << params.Aifsns[1] << ", " << params.TxopLimits[0] << ", "
                  << params.TxopLimits[1] << ", " << params.AmpduLimits[0] << ", "
                  << params.AmpduLimits[1] << ", " << params.RTS_CTS[0] << ", " << params.RTS_CTS[1]
                  << ", " << params.MaxSsrcs[0] << ", " << params.MaxSsrcs[0] << ", "
                  << params.MaxSlrcs[1] << ", " << params.MaxSlrcs[1] << ", "
                  << params.RedundancyThresholds[0] << ", " << params.RedundancyThresholds[1]
                  << ", " << params.RedundancyFixedNumbers[0] << ", "
                  << params.RedundancyFixedNumbers[1] << ", " << res.blockCnt[0] << ", "
                  << res.blockCnt[1] << ", " << res.blockCnt_tr[0] << ", " << res.blockCnt_tr[1]
                  << ", " << res.txopTime[0] << ", " << res.txopTime[1] << ", " << res.txopNum[0]
                  << ", " << res.txopNum[1] << ", " << res.maxAmpduLength[0] << ", "
                  << res.maxAmpduLength[1] << ", " << res.meanAmpduLength[0] << ", "
                  << res.meanAmpduLength[1] << ", " << res.p[0] << ", " << res.p[1] << ", "
                  << res.occ[0] << ", " << res.occ[1] << ", " << res.blocktimerate[0] << ", "
                  << res.blocktimerate[1] << ", " << res.severeblocktimerate[0] << ", "
                  << res.severeblocktimerate[1] << ", " << res.blockrate[0] << ", "
                  << res.blockrate[1] << ", " << res.datarate[0] << ", " << res.datarate[1] << ", "
                  << res.thpt[0] << ", " << res.thpt[1] << ", " << res.pct1 << ", "
                  << res.throughput << std::endl;
             res_throughputs.push_back(res.throughput);
    }

    updateThroughputCSV(pretitle, bw1, bw2, mcs1, mcs2, nss, nStaSlds1, nStaSlds2, mpduBufferSize, maxAmpduNum0, maxAmpduNum1, originalSeed, totalThroughput, totalThroughput1, totalThroughput2);


    auto calc_std_dev = [](std::vector<double>& v, int n) -> std::pair<double, double> {
        auto start = v.end() - n;
        double sum = std::accumulate(start, v.end(), 0.0);
        double mean = sum / n;
        double var = 0.0;
        double mn = 1e5;
        int idx = 0;
        for (auto it = start; it != v.end(); ++it) {
            double d = std::abs(*it - mean);
            var += std::pow(d, 2);
            if (d < mn) {
                mn = d;
                idx = v.end() - it;
            }
        }
        std::cout << "use the " << idx << "th to the last of the result." << std::endl;
        var /= n;
    return std::make_pair(sqrt(var), mean);
    };
    auto ans = calc_std_dev(res_throughputs, 3);
    double cv =  ans.first / ans.second;
    
    std::cout << "standard deviation: " << ans.first << std::endl;
    std::cout << "coeff of variation: " << cv * 100 << "% " << std::endl;
    if (cv > 0.1) {
        std::cout << "Please set longer simulation time use: --simT" << std::endl;
    }
    std::cout << "Throughput = " << ans.second << " Mbps" << std::endl;

    std::cout << "result saved: " << csv_file << std::endl;

    return 0;
}