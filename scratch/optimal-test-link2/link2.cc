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

double safe_stod(const std::string& s) 
{
    try { return std::stod(s); } catch (...) { return 0.0; }
}

void updateThroughputCSV(const std::string& scenario, const std::string& pretitle, int bw1, int bw2,
                         int mcs1, int mcs2, int nss,
                         int nsld1, int nsld2,
                         int maxAmpduNumSld0, int maxAmpduNumSld1,
                         double per0, double per1,              
                         int baw,
                         int maxAmpduNum0, int maxAmpduNum1,
                         int seedNumber, double totalthroughput,
                         double totalthroughput1, double totalthroughput2,
                         double pM1, double pM2)
{
    std::ostringstream oss;
    oss << "throughput_" + scenario + ".csv";
    std::string filename = oss.str();
    std::vector<std::vector<std::string>> rows;
    bool found = false;

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
                tokens.push_back(trim(token));
            rows.push_back(tokens);
        }
        infile.close();

        // ========== 查找并更新已有行 ==========
        for (auto& tokens : rows)
        {
            if (tokens.size() < 21 || tokens[0] == "pertitle")
                continue;

            std::string f_pretitle        = tokens[0];
            int    f_bw1               = safe_stoi(tokens[1]);
            int    f_bw2               = safe_stoi(tokens[2]);
            int    f_mcs1              = safe_stoi(tokens[3]);
            int    f_mcs2              = safe_stoi(tokens[4]);
            int    f_nss               = safe_stoi(tokens[5]);
            int    f_nsld1             = safe_stoi(tokens[6]);
            int    f_nsld2             = safe_stoi(tokens[7]);
            int    f_maxAmpduNumSld0   = safe_stoi(tokens[8]);
            int    f_maxAmpduNumSld1   = safe_stoi(tokens[9]);
            double f_per0              = safe_stod(tokens[10]); 
            double f_per1              = safe_stod(tokens[11]); 
            int    f_baw               = safe_stoi(tokens[12]);
            int    f_maxAmpduNum0      = safe_stoi(tokens[13]);
            int    f_maxAmpduNum1      = safe_stoi(tokens[14]);
            int    f_seed              = safe_stoi(tokens[15]);

            if (f_pretitle == pretitle &&
                f_bw1 == bw1 && f_bw2 == bw2 &&
                f_mcs1 == mcs1 && f_mcs2 == mcs2 &&
                f_nss == nss &&
                f_nsld1 == nsld1 && f_nsld2 == nsld2 &&
                f_maxAmpduNumSld0 == maxAmpduNumSld0 &&
                f_maxAmpduNumSld1 == maxAmpduNumSld1 &&
                f_per0 == per0 && f_per1 == per1 &&             
                f_baw == baw &&
                f_maxAmpduNum0 == maxAmpduNum0 &&
                f_maxAmpduNum1 == maxAmpduNum1 &&
                f_seed == seedNumber)
            {
                tokens[16] = std::to_string(totalthroughput);
                tokens[17] = std::to_string(totalthroughput1);
                tokens[18] = std::to_string(totalthroughput2);
                tokens[19] = std::to_string(pM1);
                tokens[20] = std::to_string(pM2);
                found = true;
                break;
            }
        }
    }
    else
    {
        rows.push_back({
            "pertitle","bw1","bw2","mcs1","mcs2","nss",
            "nsld1","nsld2",
            "maxAmpduNumSld0","maxAmpduNumSld1",
            "per0","per1",                                      
            "baw",
            "maxAmpduNum0","maxAmpduNum1","seed",
            "Throughput(Mbps)","Throughput1(Mbps)","Throughput2(Mbps)",
            "pM1","pM2"
        });
    }

    // ================= 插入新行 =================
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
            std::to_string(per0),                               
            std::to_string(per1),                               
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

    // ================= 写回文件 =================
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

void PpduTxRecord(StaType staType, int32_t staIndex, Ptr<const WifiPpdu> ppdu, Time duration, uint8_t linkid) 
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

struct LinkStats {
    double mpduNum = 0.0;
    double rtsCount = 0.0;
    double ppduCount = 0.0;
    uint32_t successfulMpdus = 0;
    uint32_t failedMpdus = 0;

    double lossRate() const {
        uint32_t total = successfulMpdus + failedMpdus;
        return total > 0 ? static_cast<double>(failedMpdus) / total : 0.0;
    }

    double pRatio() const {
        return rtsCount > 0 ? ppduCount / rtsCount : 0.0;
    }

    double tput(uint32_t payloadSize, double interval) const {
        return static_cast<double>(successfulMpdus) * payloadSize * 8.0 / interval;
    }
};

struct NodeStats {
    LinkStats link[2]; // link[0]=2G, link[1]=5G

    double totalSuccMpdu() const { return link[0].successfulMpdus + link[1].successfulMpdus; }

    double tput(uint32_t payloadSize, double interval) const {
        return static_cast<double>(totalSuccMpdu()) * payloadSize * 8.0 / interval;
    }
};

NodeStats statsMLD;
std::map<uint32_t, NodeStats> statsSLD;

void NotifyPpduTxDuration(Time statsBeginTime, Time statsEndTime, uint32_t staIndex, bool isMld, Ptr<const WifiPpdu> ppdu, Time duration, uint8_t linkid)
{
    Time now = Simulator::Now();
    if (now < statsBeginTime || now + duration > statsEndTime)
        return;

    NodeStats& stats = isMld ? statsMLD : statsSLD[staIndex];

    if (ppdu->GetPsdu()->GetHeader(0).IsRts()) {
        stats.link[linkid].rtsCount++;
        return;
    }

    if (!ppdu->GetPsdu()->GetHeader(0).IsQosData())
        return;

    Ptr<const WifiPsdu> psdu = ppdu->GetPsdu();
    uint32_t nmpdus = psdu->IsAggregate() ? psdu->GetNMpdus() : 1;
    if (nmpdus == 0)
        return;

    stats.link[linkid].mpduNum += nmpdus;
    stats.link[linkid].ppduCount++;
}

void NotifyBlockAckResult(Time statsBeginTime, Mac48Address recipient, uint8_t tid, uint8_t linkId, uint16_t nSuccessfulMpdus, uint16_t nFailedMpdus)
{
    Time now = Simulator::Now();
    if (now < statsBeginTime || linkId > 1)
        return;
    statsMLD.link[linkId].successfulMpdus += nSuccessfulMpdus;
    statsMLD.link[linkId].failedMpdus += nFailedMpdus;
    std::cout << now.GetMicroSeconds() << " us: "
            << " BlockAck from " << recipient
            << " tid=" << (uint32_t)tid
            << " link=" << (uint32_t)linkId
            << " success=" << nSuccessfulMpdus
            << " failed=" << nFailedMpdus
            << " total=" << (nSuccessfulMpdus + nFailedMpdus)
            << std::endl;
}

void PrintStats(Time statsBeginTime, Time statsEndTime, uint32_t payloadSize)
{
    double interval = (statsEndTime - statsBeginTime).GetMicroSeconds();
    std::cout << "[ " << statsBeginTime.As(Time::S) << " - " << statsEndTime.As(Time::S) << " ]\n";

    // MLD
    std::cout << "MLD Throughput "
              << statsMLD.tput(payloadSize, interval) << " Mbit/s "
              << statsMLD.link[0].tput(payloadSize, interval) << " Mbit/s (2.4G) "
              << statsMLD.link[1].tput(payloadSize, interval) << " Mbit/s (5G)\n"
              << "pM: " << statsMLD.link[0].pRatio() << " (2.4G), "
                        << statsMLD.link[1].pRatio() << " (5G)\n"
              << "PER: 2.4G=" << statsMLD.link[0].lossRate()
              << "  5G="   << statsMLD.link[1].lossRate() << "\n"
              << "Successful/Failed MPDUs:"
              << "  2.4G=" << statsMLD.link[0].successfulMpdus << "/" << statsMLD.link[0].failedMpdus
              << "  5G="   << statsMLD.link[1].successfulMpdus << "/" << statsMLD.link[1].failedMpdus
              << "  total=" << statsMLD.link[0].successfulMpdus + statsMLD.link[1].successfulMpdus
              << "/"        << statsMLD.link[0].failedMpdus     + statsMLD.link[1].failedMpdus << "\n";

    // SLD
    std::cout << "\n------ SLD Statistics ------\n"
              << "STA\tpS\t\tThroughput(Mbps)\tBand\n";

    struct Avg { double sumPs = 0, sumTp = 0; int count = 0; };
    Avg avg2G, avg5G;

    auto printLink = [&](uint32_t staIndex, int linkId, const char* band, Avg& avg) {
        const LinkStats& l = statsSLD[staIndex].link[linkId];
        double ps = l.pRatio();
        double tp = l.tput(payloadSize, interval);
        avg.sumPs += ps; avg.sumTp += tp; avg.count++;
        std::cout << staIndex << "\t"
                  << std::fixed << std::setprecision(6) << ps << "\t\t"
                  << std::setprecision(3) << tp << "\t\t"
                  << band << "\n";
    };

    for (auto& [idx, _] : statsSLD) {
        if (statsSLD[idx].link[0].rtsCount > 0) printLink(idx, 0, "2.4G", avg2G);
        if (statsSLD[idx].link[1].rtsCount > 0) printLink(idx, 1, "5G",   avg5G);
    }

    std::cout << "\n------ Averages ------\n";
    auto printAvg = [](const char* band, const Avg& avg) {
        if (avg.count > 0)
            std::cout << band << ": pS avg=" << std::fixed << std::setprecision(6)
                      << avg.sumPs / avg.count
                      << ", Tput avg=" << std::setprecision(3)
                      << avg.sumTp / avg.count << " Mbps\n";
        else
            std::cout << band << ": No data\n";
    };
    printAvg("2.4G", avg2G);
    printAvg("5G",   avg5G);
    std::cout << "---------------------\n";
}

int main(int argc, char* argv[])
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
    std::string rateCtrl{"constant"};

    uint16_t mpduBufferSize{512};
    uint32_t maxAmpduSize{1024 * 8 * (1500 + 150)};
    uint32_t maxAmpduNum0 = 10;
    uint32_t maxAmpduNum1 = 10;
    uint32_t maxAmpduNumSld0 = 1;
    uint32_t maxAmpduNumSld1 = 1;
    // double SLDinterval = 1.0;

    uint32_t txoplimit1 = 0, txoplimit2 = 0;
    uint8_t nStaSlds1 = 1;
    uint8_t nStaSlds2 = 1;
    double period = 0.5;
    uint32_t nss = 4;
    uint8_t mode = 1;
    double fixedPER0 = 0.0;
    double fixedPER1 = 0.0;

    double simT = 5.5;
    bool logsender = false;
    bool logreceiver = false;
    Time simT_delayEnd = NanoSeconds(2);
    // uint32_t maxGroupSize = 1;
    uint32_t pretitleint = 6;
    std::string scenario = "default";
    
    CommandLine cmd(__FILE__);
    std::filesystem::path filepath = __FILE__;
    cmd.AddValue("seed", "seed number", seedNumber);
    cmd.AddValue("mcs1", "MCS for 2.4 GHz", mcs1);
    cmd.AddValue("mcs2", "MCS for 5 GHz", mcs2);
    cmd.AddValue("loadrate1", "load rate on 2.4 GHz", r1);
    cmd.AddValue("loadrate2", "load rate on 5 GHz", r2);
    // cmd.AddValue("ch1", "channel id on 2.4 GHz", ch1);
    // cmd.AddValue("ch2", "channel id on 5 GHz", ch2);
    cmd.AddValue("bw1", "band width on 2.4 GHz", bw1);
    cmd.AddValue("bw2", "band width on 5 GHz", bw2);
    cmd.AddValue("bawsize", "BA Window Size", mpduBufferSize);
    // cmd.AddValue("max_ampdusize", "Max AmpduSize of AP 0", maxAmpduSize);
    cmd.AddValue("simt", "simulation time", simT);
    cmd.AddValue("period", "", period);
    cmd.AddValue("nss", "mimo", nss);
    // cmd.AddValue("maxgroupsize", "maxgroupsize", maxGroupSize);
    cmd.AddValue("nsld0", "interference setting", nStaSlds1);
    cmd.AddValue("nsld1", "interference setting", nStaSlds2);
    cmd.AddValue("logsender", "new transmitter architecture log setting", logsender);
    cmd.AddValue("logreceiver", "new receiver architecture log setting", logreceiver);
    cmd.AddValue("pretitle", "pre title", pretitleint);
    cmd.AddValue("maxampdunum0", "max mpdu number of 2.4G", maxAmpduNum0);
    cmd.AddValue("maxampdunum1", "max mpdu number of 5G", maxAmpduNum1);
    cmd.AddValue("ampdunumsld0", "max ampdu num of SLD 2.4G", maxAmpduNumSld0);
    cmd.AddValue("ampdunumsld1", "max ampdu num of SLD 5G", maxAmpduNumSld1);
    cmd.AddValue("scenario", "Simulation scenario", scenario);
    // cmd.AddValue("SLDinterval", "interval for throughput measurement", SLDinterval);
    cmd.AddValue("fixedPER0", "fixed PER for link 0", fixedPER0);
    cmd.AddValue("fixedPER1", "fixed PER for link 1", fixedPER1);
    cmd.Parse(argc, argv);

    uint32_t maxAmpduSize1 = maxAmpduNumSld0 * (1500 + 72);
    uint32_t maxAmpduSize2 = maxAmpduNumSld1 * (1500 + 72);
    uint32_t originalSeed = seedNumber;
    if (!(nStaSlds1)) r1 = 1e-9;  
    if (!(nStaSlds2)) r2 = 1e-9;

    std::string pretitle = "";
    switch (pretitleint){
        case 1: pretitle = "greedy";   break;
        case 2: pretitle = "damla";    break;
        case 3: pretitle = "only2G";   break;
        case 4: pretitle = "only5G";   break;
        case 6: pretitle = "bothset";  break;
        default: pretitle = "unknown"; break;
    }
    
    std::ostringstream oss;
    oss << pretitle
        << "_baw_" << mpduBufferSize
        << "_bw_" << bw1 << "_" << bw2
        << "_mcs_" << mcs1 << "_" << mcs2
        << "_interference_" << static_cast<uint32_t>(nStaSlds1) << "_" << static_cast<uint32_t>(nStaSlds2);
    // if (SLDinterval != 1.0)                      oss << "_sldinterval_" << SLDinterval;
    if (maxAmpduNumSld0 && nStaSlds1)            oss << "_maxAmpduNumSld0_" << maxAmpduNumSld0;
    if (maxAmpduNumSld1 && nStaSlds2)            oss << "_maxAmpduNumSld1_" << maxAmpduNumSld1;
    if (fixedPER0 > 0)                           oss << std::fixed << std::setprecision(2) << "_fixedPER0_" << fixedPER0;
    if (fixedPER1 > 0)                           oss << std::fixed << std::setprecision(2) << "_fixedPER1_" << fixedPER1;
    else if (pretitleint == 6)                   oss << "_maxAmpduNum0_" << maxAmpduNum0 << "_maxAmpduNum1_" << maxAmpduNum1;
    oss << "_seed_" << seedNumber;

    std::string title = oss.str();
    std::filesystem::path scenarioDir = filepath.parent_path() / scenario;
    std::filesystem::create_directories(scenarioDir);

    auto prepareFile = [&](const std::string& name) {
        std::string fullpath = (scenarioDir / name).string();
        std::filesystem::remove(fullpath);
        return fullpath;
    };

    ppduTxOutputFile = prepareFile(title + "_PPDU.csv");
    rtsctsTxOutputFile = prepareFile(title + "_RTSCTS.csv");
    baTxOutputFile = prepareFile(title + "_BA.csv");
    std::cout << "PPDU Tx csv: " << ppduTxOutputFile << std::endl;

    if (logsender) mode = mode | (1 << 5);
    uint8_t mode_recv = 1 << 2;
    if (logreceiver) mode_recv = mode_recv | (1 << 6);

    uint8_t nLinks = 2;
    RngSeedManager::SetSeed(seedNumber);
    RngSeedManager::SetRun(seedNumber);
    double txPower = 20; 
    bool useRts{true};
    int gi = 800;
    Time simulationTime{Seconds(simT)};
    size_t nStaMlds{1};
    uint32_t payloadSize = 1500;

    if (useRts)
    {
        Config::SetDefault("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue("0"));
    }

    Config::SetDefault("ns3::WifiMacQueue::MaxSize", QueueSizeValue(QueueSize(QueueSizeUnit::PACKETS, 1024*2)));
    Config::SetDefault("ns3::WifiRemoteStationManager::FragmentationThreshold", UintegerValue(std::numeric_limits<uint32_t>::max()));
    Config::SetDefault("ns3::WifiRemoteStationManager::MaxSlrc", UintegerValue(std::numeric_limits<uint32_t>::max()));
    Config::SetDefault("ns3::WifiRemoteStationManager::MaxSsrc", UintegerValue(std::numeric_limits<uint32_t>::max())); 

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
            dataModeStr = "EhtMcs" + std::to_string(mcs[i]);
            nonHtRefRateMbps = EhtPhy::GetNonHtReferenceRate(mcs[i]) / 1e6;
            ctrlRateStr = (i == 0) ? "ErpOfdmRate" + std::to_string(nonHtRefRateMbps) + "Mbps" : "OfdmRate" + std::to_string(nonHtRefRateMbps) + "Mbps";
            std::cout << "Link " << std::to_string(i) <<" ControlRate: " << ctrlRateStr << " DataMode: " << dataModeStr << std::endl;
            wifi.SetRemoteStationManager(i, "ns3::ConstantRateWifiManager", "DataMode", StringValue(dataModeStr), "ControlMode", StringValue(ctrlRateStr));
        }
    }

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
        std::string channelSetting = "{" + std::to_string(channelnum[i]) + ", " + std::to_string(bandwidth[i]);
        if (i == 0) phy.Set(i, "ChannelSettings", StringValue(channelSetting + ", BAND_2_4GHZ, 0}"));
        else phy.Set(i, "ChannelSettings", StringValue(channelSetting + ", BAND_5GHZ, 0}"));
    }

    /* MAC Configuration */
    WifiMacHelper mac;
    Ssid bssSsid = Ssid("AP-MLD");
    uint64_t beaconInterval = std::min<uint64_t>((ceil((simT * 1000000) / 1024) * 1024), (65535 * 1024));

    mac.SetType("ns3::ApWifiMac", "BeaconInterval", TimeValue(MicroSeconds(beaconInterval)), "EnableBeaconJitter", BooleanValue(false), "Ssid", SsidValue(bssSsid));
    apDev = wifi.Install(phy, mac, apNodes.Get(0));

    mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(bssSsid), "ActiveProbing", BooleanValue(false));
    mldDev = wifi.Install(phy, mac, mldNodes);
    
    Time statsBeginTime = Seconds(1.5);
    std::cout << "statsBeginTime: " << statsBeginTime.As(Time::S) << std::endl;
    Time statsEndTime = Seconds(period) * ((simulationTime + simT_delayEnd) / Seconds(period)).GetInt();
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
    
    DynamicCast<WifiNetDevice>(apDev.Get(0))->GetPhy(0)->TraceConnectWithoutContext("PpduTxDuration", MakeBoundCallback(&PpduTxRecord, StaType::MLD_AP, -1));
    DynamicCast<WifiNetDevice>(apDev.Get(0))->GetPhy(1)->TraceConnectWithoutContext("PpduTxDuration", MakeBoundCallback(&PpduTxRecord, StaType::MLD_AP, -1));
    DynamicCast<WifiNetDevice>(mldDev.Get(0))->GetPhy(0)->TraceConnectWithoutContext("PpduTxDuration", MakeBoundCallback(&PpduTxRecord, StaType::MLD_STA, -1));
    DynamicCast<WifiNetDevice>(mldDev.Get(0))->GetPhy(1)->TraceConnectWithoutContext("PpduTxDuration", MakeBoundCallback(&PpduTxRecord, StaType::MLD_STA, -1));
    DynamicCast<WifiNetDevice>(mldDev.Get(0))->GetPhy(0)->TraceConnectWithoutContext("PpduTxDuration", MakeBoundCallback(&NotifyPpduTxDuration, statsBeginTime, statsEndTime, 0, true));
    DynamicCast<WifiNetDevice>(mldDev.Get(0))->GetPhy(1)->TraceConnectWithoutContext("PpduTxDuration", MakeBoundCallback(&NotifyPpduTxDuration, statsBeginTime, statsEndTime, 0, true));
    DynamicCast<WifiNetDevice>(mldDev.Get(0))->GetMac()->GetQosTxop(0)->GetBaManager()->TraceConnectWithoutContext("BlockAckResult", MakeBoundCallback(&NotifyBlockAckResult, statsBeginTime)); 
        
    for (size_t id = nStaMlds; id < nStaMlds + nStaSlds1; ++id) {
        Ptr<WifiMac> mac_mld = DynamicCast<WifiNetDevice>(mldDev.Get(id))->GetMac(); 
        std::cout << "SLD_2.4G_" << (id) << " MAC: " << mldDev.Get(id)->GetAddress() << std::endl;
        auto fem = mac_mld->GetFrameExchangeManager(0);
        std::cout << "\t sldDevice " << "linkId " << std::to_string(0) << " mac address: " << fem->GetAddress() << std::endl;
        uint32_t staIndex = id - nStaMlds;
        DynamicCast<WifiNetDevice>(mldDev.Get(id))->GetPhy(0)->TraceConnectWithoutContext("PpduTxDuration", MakeBoundCallback(&PpduTxRecord, StaType::SLD_2G, staIndex));
        DynamicCast<WifiNetDevice>(mldDev.Get(id))->GetPhy(1)->TraceConnectWithoutContext("PpduTxDuration", MakeBoundCallback(&PpduTxRecord, StaType::SLD_2G, staIndex));
        DynamicCast<WifiNetDevice>(mldDev.Get(id))->GetPhy(0)->TraceConnectWithoutContext("PpduTxDuration", MakeBoundCallback(&NotifyPpduTxDuration, statsBeginTime, statsEndTime, staIndex, false));
        DynamicCast<WifiNetDevice>(mldDev.Get(id))->GetPhy(1)->TraceConnectWithoutContext("PpduTxDuration", MakeBoundCallback(&NotifyPpduTxDuration, statsBeginTime, statsEndTime, staIndex, false));
    }

    for (size_t id = nStaMlds + nStaSlds1; id < nStaMlds + nStaSlds1 + nStaSlds2; ++id) {
        Ptr<WifiMac> mac_mld = DynamicCast<WifiNetDevice>(mldDev.Get(id))->GetMac(); 
        std::cout << "SLD_5G_" << (id) << " MAC: " << mldDev.Get(id)->GetAddress() << std::endl;
        auto fem = mac_mld->GetFrameExchangeManager(1);
        std::cout << "\t sldDevice " << "linkId " << std::to_string(1) << " mac address: " << fem->GetAddress() << std::endl;
        uint32_t staIndex = id - nStaMlds - nStaSlds1;
        DynamicCast<WifiNetDevice>(mldDev.Get(id))->GetPhy(0)->TraceConnectWithoutContext("PpduTxDuration", MakeBoundCallback(&PpduTxRecord, StaType::SLD_5G, staIndex));
        DynamicCast<WifiNetDevice>(mldDev.Get(id))->GetPhy(1)->TraceConnectWithoutContext("PpduTxDuration", MakeBoundCallback(&PpduTxRecord, StaType::SLD_5G, staIndex));
        DynamicCast<WifiNetDevice>(mldDev.Get(id))->GetPhy(0)->TraceConnectWithoutContext("PpduTxDuration", MakeBoundCallback(&NotifyPpduTxDuration, statsBeginTime, statsEndTime, staIndex, false));
        DynamicCast<WifiNetDevice>(mldDev.Get(id))->GetPhy(1)->TraceConnectWithoutContext("PpduTxDuration", MakeBoundCallback(&NotifyPpduTxDuration, statsBeginTime, statsEndTime, staIndex, false));
    }

    NetDeviceContainer devices;
    devices.Add(apDev);
    devices.Add(mldDev);
    wifi.AssignStreams(devices, seedNumber);

    Config::Set("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/HeConfiguration/GuardInterval", TimeValue(NanoSeconds(gi)));

    NodeContainer allNodes(apNodes, mldNodes);
    for (uint32_t i = 0; i < allNodes.GetN(); ++i)
    {
        if(i >= apNodes.GetN() + nStaMlds + nStaSlds1) 
            Config::Set("/NodeList/" + std::to_string(i) + "/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_MaxAmpduSize", UintegerValue(maxAmpduSize2));
        else if(i >= apNodes.GetN() + nStaMlds) 
            Config::Set("/NodeList/" + std::to_string(i) + "/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_MaxAmpduSize", UintegerValue(maxAmpduSize1));
    }

    Config::Set("/NodeList/0/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_MaxAmpduSize", UintegerValue(maxAmpduSize));
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_MaxAmpduSize", UintegerValue(maxAmpduSize));
    Config::Set("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/MpduBufferSize", UintegerValue(mpduBufferSize));

    // ---------------- Mobility: 圆形分布 ----------------
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    positionAlloc->Add(Vector(0.0, 2.0, 0.0));
    std::cout << "AP 坐标: (0.0, 2.0, 0.0)" << std::endl;

    double bssRadius = 0.1;
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

    const auto maxLoad2 = EhtPhy::GetDataRate(mcs[0], bandwidth[0] , NanoSeconds(gi), 1) * nss;
    const auto maxLoad5 = EhtPhy::GetDataRate(mcs[1], bandwidth[1] , NanoSeconds(gi), 1) * nss;
    std::cout << "maxload = " << std::to_string((maxLoad2 + maxLoad5)/1e6) << " Mbps; 2.4 GHz: " <<  std::to_string(maxLoad2/1e6) << " Mbps, 5 GHz: " << std::to_string(maxLoad5/1e6) << " Mbps" << std::endl;
    
    const auto packetInterval = payloadSize * 8.0 / (maxLoad2 + maxLoad5) / 2; 
    const auto packetInterval2 = payloadSize * 8.0 / maxLoad2;
    const auto packetInterval5 = payloadSize * 8.0 / maxLoad5;
    std::cout << "2.4 GHz: " <<  std::to_string(packetInterval2) << " s, 5 GHz: " << std::to_string(packetInterval5) << " s" << std::endl;
    
    UdpClientHelper client(apNodeInterface.GetAddress(0), port);
    client.SetAttribute("MaxPackets", UintegerValue(0));
    client.SetAttribute("PacketSize", UintegerValue(payloadSize));
    for (uint32_t i = 0; i < mldNodes.GetN(); ++i){
        if(i == 0) client.SetAttribute("Interval", TimeValue(Seconds(packetInterval)));
        else if(i < nStaMlds + nStaSlds1) client.SetAttribute("Interval", TimeValue(Seconds(packetInterval2)));
        else client.SetAttribute("Interval", TimeValue(Seconds(packetInterval5)));
        ApplicationContainer clientApp = client.Install(mldNodes.Get(i));
        seedNumber += client.AssignStreams(mldNodes.Get(i), seedNumber);
        clientApp.Start(Seconds(1));
        clientApp.Stop(simulationTime + simT_delayEnd);
    }

    for (auto i = mldDev.Begin(); i != mldDev.End(); ++i)
    {
        auto wifiDev = DynamicCast<WifiNetDevice>(*i);
        wifiDev->GetMac()->GetEhtConfiguration()->SetAttribute("TidToLinkMappingNegSupport", EnumValue(WifiTidToLinkMappingNegSupport::ANY_LINK_SET));
    }

    std::string mldMappingStr = "0,1,2,3,4,5,6,7 0,1";
    std::string mldMappingStr1 = "0,1,2,3,4,5,6,7 0";
    std::string mldMappingStr2 = "0,1,2,3,4,5,6,7 1";

    size_t index = 0;
    for (auto i = mldDev.Begin(); i != mldDev.End(); ++i, ++index)
    {
        auto wifiDev = DynamicCast<WifiNetDevice>(*i);
        wifiDev->GetMac()->SetAttribute("ActiveProbing", BooleanValue(true));
        std::string mappingToUse = (index < nStaMlds) ? mldMappingStr : ((index < nStaMlds + nStaSlds1) ? mldMappingStr1 : mldMappingStr2);
        wifiDev->GetMac()->GetEhtConfiguration()->SetAttribute("TidToLinkMappingDl", StringValue(mappingToUse));
        wifiDev->GetMac()->GetEhtConfiguration()->SetAttribute("TidToLinkMappingUl", StringValue(mappingToUse));
    }

    auto apWifiDev = DynamicCast<WifiNetDevice>(apDev.Get(0));
    apWifiDev->GetMac()->GetEhtConfiguration()->SetAttribute("TidToLinkMappingDl", StringValue(mldMappingStr));
    apWifiDev->GetMac()->GetEhtConfiguration()->SetAttribute("TidToLinkMappingUl", StringValue(mldMappingStr));

    std::vector<double> fixedPERs = {fixedPER0, fixedPER1};
    Config::Set("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/FixedPER", AttributeContainerValue<DoubleValue>(fixedPERs));

    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/UseExplicitBarAfterMissedBlockAck", BooleanValue(false));
    Config::Set("/NodeList/0/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/UseExplicitBarAfterMissedBlockAck", BooleanValue(false));
    // Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/MaxGroupSize", UintegerValue(maxGroupSize));
    // Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/Period", TimeValue(period));
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/Mode", UintegerValue(mode));
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/PreTitle", UintegerValue(pretitleint));
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/MaxAmpduNum0", UintegerValue(maxAmpduNum0));
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/MaxAmpduNum1", UintegerValue(maxAmpduNum1));
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/DataRate24", DoubleValue(maxLoad2)); 
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/DataRate5", DoubleValue(maxLoad5));

    Config::Set("/NodeList/0/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/Mode", UintegerValue(mode_recv));

    /* OBSS & BSS EDCA */
    for (uint32_t i = 0; i < allNodes.GetN(); ++i)
    {
        Config::Set("/NodeList/" + std::to_string(i) + "/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/Aifsns", AttributeContainerValue<UintegerValue>(std::list<uint64_t>{2,2}));
        Config::Set("/NodeList/" + std::to_string(i) + "/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/MinCws", AttributeContainerValue<UintegerValue>(std::list<int>{15,15}));
        Config::Set("/NodeList/" + std::to_string(i) + "/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/MaxCws", AttributeContainerValue<UintegerValue>(std::list<int>{1023,1023}));
    }
    Config::Set("/NodeList/0/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/Aifsns", AttributeContainerValue<UintegerValue>(std::list<uint64_t>{2,2}));
    Config::Set("/NodeList/0/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/MinCws", AttributeContainerValue<UintegerValue>(std::list<int>{15,15}));
    Config::Set("/NodeList/0/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/MaxCws", AttributeContainerValue<UintegerValue>(std::list<int>{1023,1023}));
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/Aifsns", AttributeContainerValue<UintegerValue>(std::list<uint64_t>{2,2}));
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/MinCws", AttributeContainerValue<UintegerValue>(std::list<int>{15,15}));
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/MaxCws", AttributeContainerValue<UintegerValue>(std::list<int>{1023,1023}));
    
    std::vector<Time> txopLimitList = {MicroSeconds(32) * txoplimit1, MicroSeconds(32) * txoplimit2};
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/TxopLimits", AttributeContainerValue<TimeValue>(txopLimitList));
    
    Simulator::Stop(simulationTime + simT_delayEnd);
    Simulator::Run();
    Simulator::Destroy();
    
    if (mode == 0) return 0;
    
    double interval = (statsEndTime - statsBeginTime).GetMicroSeconds();
    updateThroughputCSV(scenario, pretitle, bw1, bw2, mcs1, mcs2, nss, nStaSlds1, nStaSlds2, maxAmpduNumSld0, maxAmpduNumSld1, fixedPER0, fixedPER1, mpduBufferSize, maxAmpduNum0, maxAmpduNum1, originalSeed, statsMLD.tput(payloadSize, interval), statsMLD.link[0].tput(payloadSize, interval), statsMLD.link[1].tput(payloadSize, interval), statsMLD.link[0].pRatio(), statsMLD.link[1].pRatio());
    PrintStats(statsBeginTime, statsEndTime, payloadSize);

    return 0;
}