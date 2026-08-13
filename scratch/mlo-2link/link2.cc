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

// ns-3 headers, alphabetically sorted.
#include "ns3/ap-wifi-mac.h"
#include "ns3/attribute-container.h"
#include "ns3/command-line.h"
#include "ns3/config.h"
#include "ns3/constant-rate-wifi-manager.h"
#include "ns3/eht-configuration.h"
#include "ns3/eht-phy.h"
#include "ns3/frame-exchange-manager.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/log.h"
#include "ns3/mobility-helper.h"
#include "ns3/multi-model-spectrum-channel.h"
#include "ns3/rng-seed-manager.h"
#include "ns3/spectrum-wifi-helper.h"
#include "ns3/sta-wifi-mac.h"
#include "ns3/string.h"
#include "ns3/udp-client-server-helper.h"
#include "ns3/udp-server.h"
#include "ns3/uinteger.h"
#include "ns3/wifi-mac-queue.h"
#include "ns3/wifi-net-device.h"
#include "ns3/wifi-phy.h"
#include "ns3/yans-wifi-helper.h"

// Standard library headers, alphabetically sorted.
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("mlo-obss-dl-ucp");

/**
 * Convert a string to an int, returning \p defaultVal instead of throwing
 * if the string does not contain a valid integer.
 *
 * \param s the string to convert
 * \param defaultVal the value returned when conversion fails
 * \return the parsed integer, or \p defaultVal on failure
 */
int
SafeStoi(const std::string& s, int defaultVal = 0)
{
    try
    {
        return std::stoi(s);
    }
    catch (...)
    {
        return defaultVal;
    }
}

/**
 * Convert a string to a double, returning 0.0 instead of throwing if the
 * string does not contain a valid number.
 *
 * \param s the string to convert
 * \return the parsed double, or 0.0 on failure
 */
double
SafeStod(const std::string& s)
{
    try
    {
        return std::stod(s);
    }
    catch (...)
    {
        return 0.0;
    }
}

/**
 * Strip leading and trailing spaces/tabs from a string.
 *
 * \param s the string to trim
 * \return the trimmed string, or an empty string if \p s is all whitespace
 */
std::string
Trim(const std::string& s)
{
    std::size_t start = s.find_first_not_of(" \t");
    std::size_t end = s.find_last_not_of(" \t");
    if (start == std::string::npos || end == std::string::npos)
    {
        return "";
    }
    return s.substr(start, end - start + 1);
}

/**
 * Append (or update, if a matching row already exists) one row of
 * throughput results in "throughput_<scenario>.csv". Rows are matched on
 * every configuration column (policy, bandwidths, MCS, NSS, interference
 * setup, A-MPDU settings, PER, BAW size and seed); only the throughput 
 * columns are overwritten on a match.
 *
 * \param scenario the simulation scenario name
 * \param policy the human-readable A-MPDU control policy name
 * \param bw0 link-0 (2.4 GHz) channel width
 * \param bw1 link-1 (5 GHz) channel width
 * \param mcs0 link-0 MCS index
 * \param mcs1 link-1 MCS index
 * \param nss number of spatial streams
 * \param nsld1 number of interfering SLD STAs on link 0
 * \param nsld2 number of interfering SLD STAs on link 1
 * \param maxAmpduNumSld0 max A-MPDU size (in MPDUs) configured for SLD STAs on link 0
 * \param maxAmpduNumSld1 max A-MPDU size (in MPDUs) configured for SLD STAs on link 1
 * \param per0 fixed PER configured for link 0
 * \param per1 fixed PER configured for link 1
 * \param baw block ack window size
 * \param maxAmpduNum0 max A-MPDU size configured for the MLD on link 0
 * \param maxAmpduNum1 max A-MPDU size configured for the MLD on link 1
 * \param seedNumber the RNG seed used for this run
 * \param totalthroughput aggregate MLD throughput (Mbps)
 * \param totalthroughput1 MLD throughput on link 0 (Mbps)
 * \param totalthroughput2 MLD throughput on link 1 (Mbps)
 */
void
UpdateThroughputCsv(const std::string& scenario,
                     const std::string& policy,
                     int bw0,
                     int bw1,
                     int mcs0,
                     int mcs1,
                     int nss,
                     int nsld1,
                     int nsld2,
                     int maxAmpduNumSld0,
                     int maxAmpduNumSld1,
                     double per0,
                     double per1,
                     int baw,
                     int maxAmpduNum0,
                     int maxAmpduNum1,
                     int seedNumber,
                     double totalthroughput,
                     double totalthroughput1,
                     double totalthroughput2)
{
    const std::string filename = "throughput_" + scenario + ".csv";
    std::vector<std::vector<std::string>> rows;
    bool found = false;

    if (std::filesystem::exists(filename))
    {
        std::ifstream infile(filename);
        std::string line;
        while (std::getline(infile, line))
        {
            if (line.empty())
            {
                continue;
            }
            std::stringstream ss(line);
            std::vector<std::string> tokens;
            std::string token;
            while (std::getline(ss, token, ','))
            {
                tokens.push_back(Trim(token));
            }
            rows.push_back(tokens);
        }
        infile.close();

        // Look for an existing row with the same configuration and update
        // its throughput/pM columns in place.
        for (auto& tokens : rows)
        {
            if (tokens.size() < 19 || tokens[0] == "policy")
            {
                continue;
            }

            const std::string fPolicy = tokens[0];
            const int fBw1 = SafeStoi(tokens[1]);
            const int fBw2 = SafeStoi(tokens[2]);
            const int fMcs1 = SafeStoi(tokens[3]);
            const int fMcs2 = SafeStoi(tokens[4]);
            const int fNss = SafeStoi(tokens[5]);
            const int fNsld1 = SafeStoi(tokens[6]);
            const int fNsld2 = SafeStoi(tokens[7]);
            const int fMaxAmpduNumSld0 = SafeStoi(tokens[8]);
            const int fMaxAmpduNumSld1 = SafeStoi(tokens[9]);
            const double fPer0 = SafeStod(tokens[10]);
            const double fPer1 = SafeStod(tokens[11]);
            const int fBaw = SafeStoi(tokens[12]);
            const int fMaxAmpduNum0 = SafeStoi(tokens[13]);
            const int fMaxAmpduNum1 = SafeStoi(tokens[14]);
            const int fSeed = SafeStoi(tokens[15]);

            if (fPolicy == policy && fBw1 == bw0 && fBw2 == bw1 && fMcs1 == mcs0 &&
                fMcs2 == mcs1 && fNss == nss && fNsld1 == nsld1 && fNsld2 == nsld2 &&
                fMaxAmpduNumSld0 == maxAmpduNumSld0 && fMaxAmpduNumSld1 == maxAmpduNumSld1 &&
                fPer0 == per0 && fPer1 == per1 && fBaw == baw && fMaxAmpduNum0 == maxAmpduNum0 &&
                fMaxAmpduNum1 == maxAmpduNum1 && fSeed == seedNumber)
            {
                tokens[16] = std::to_string(totalthroughput);
                tokens[17] = std::to_string(totalthroughput1);
                tokens[18] = std::to_string(totalthroughput2);
                found = true;
                break;
            }
        }
    }
    else
    {
        // File does not exist yet: write the header row first.
        rows.push_back({"policy",
                         "bw0",
                         "bw1",
                         "mcs0",
                         "mcs1",
                         "nss",
                         "nsld0",
                         "nsld1",
                         "maxAmpduNumSld0",
                         "maxAmpduNumSld1",
                         "per0",
                         "per1",
                         "baw",
                         "maxAmpduNum0",
                         "maxAmpduNum1",
                         "seed",
                         "Throughput(Mbps)",
                         "Throughput0(Mbps)",
                         "Throughput1(Mbps)"});
    }

    if (!found)
    {
        rows.push_back({policy,
                         std::to_string(bw0),
                         std::to_string(bw1),
                         std::to_string(mcs0),
                         std::to_string(mcs1),
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
                         std::to_string(totalthroughput2)});
    }

    std::ofstream outfile(filename, std::ios::out | std::ios::trunc);
    for (const auto& row : rows)
    {
        for (std::size_t i = 0; i < row.size(); ++i)
        {
            outfile << row[i];
            if (i + 1 < row.size())
            {
                outfile << ",";
            }
        }
        outfile << "\n";
    }
    outfile.close();
}

// Per-run CSV trace files. Their actual paths are set in main() once the
// scenario/output directory is known.
std::string ppduTxOutputFile("./PPDU.csv");
std::string rtsctsTxOutputFile("./RTSCTS.csv");
std::string baTxOutputFile("./BA.csv");

/// Identifies which kind of station a PHY trace callback belongs to.
enum class StaType
{
    MLD_STA, //!< the (single) multi-link device STA under test
    SLD_2G,  //!< a single-link device generating interference on the 2.4 GHz link
    SLD_5G,  //!< a single-link device generating interference on the 5 GHz link
    MLD_AP   //!< the multi-link AP
};

/**
 * PHY-level "PpduTxDuration" trace sink: logs every transmitted PPDU to the
 * appropriate CSV file (RTS/CTS, BlockAck/ACK, or QoS-data), tagged with the
 * originating station type, index and link ID.
 *
 * \param staType the type of station that transmitted the PPDU
 * \param staIndex index of the station within its type (ignored for AP/MLD_STA)
 * \param ppdu the transmitted PPDU
 * \param duration the PPDU's transmission duration
 * \param linkid the link the PPDU was transmitted on
 */
void
PpduTxRecord(StaType staType, int32_t staIndex, Ptr<const WifiPpdu> ppdu, Time duration, uint8_t linkid)
{
    const auto& hdr = ppdu->GetPsdu()->GetHeader(0);
    const uint64_t startUs = Simulator::Now().GetMicroSeconds();
    const uint64_t endUs = startUs + duration.GetMicroSeconds();

    // Build the station-identifying prefix used in the CSV output.
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

    // RTS / CTS frames.
    if (hdr.IsRts() || hdr.IsCts())
    {
        std::fstream file(rtsctsTxOutputFile, std::ios::out | std::ios::app);
        file << prefix.str() << (hdr.IsRts() ? "_RTS_" : "_CTS_") << uint32_t(linkid) << ","
             << startUs << "," << endUs << std::endl;
        return;
    }

    // AP-only: BlockAck / ACK frames.
    if (staType == StaType::MLD_AP && (hdr.IsBlockAck() || hdr.IsAck()))
    {
        std::fstream file(baTxOutputFile, std::ios::out | std::ios::app);
        file << prefix.str() << (hdr.IsBlockAck() ? "_BA_" : "_ACK_") << uint32_t(linkid) << ","
             << startUs << "," << endUs << std::endl;
        return;
    }

    // Only QoS-data frames are recorded from here on.
    if (!hdr.IsQosData())
    {
        return;
    }

    Ptr<const WifiPsdu> psdu = ppdu->GetPsdu();
    const uint32_t nMpdu = psdu->IsAggregate() ? psdu->GetNMpdus() : 1;
    if (nMpdu == 0)
    {
        return;
    }

    std::fstream file(ppduTxOutputFile, std::ios::out | std::ios::app);
    file << prefix.str() << "_" << uint32_t(linkid) << "," << startUs << "," << endUs << ","
         << nMpdu << std::endl;
}

/// Per-link running statistics used to derive throughput and PER.
struct LinkStats
{
    uint32_t successfulMpdus = 0;  //!< MPDUs acknowledged as successful via BlockAck
    uint32_t failedMpdus = 0;      //!< MPDUs acknowledged as failed via BlockAck

    /**
     * \return the fraction of MPDUs that were reported as failed (0 if none were reported)
     */
    double LossRate() const
    {
        const uint32_t total = successfulMpdus + failedMpdus;
        return total > 0 ? static_cast<double>(failedMpdus) / total : 0.0;
    }

    /**
     * \param payloadSize the application payload size, in bytes
     * \param interval the measurement interval, in microseconds
     * \return the throughput achieved over the measurement interval, in Mbit/s
     */
    double Tput(uint32_t payloadSize, double interval) const
    {
        return static_cast<double>(successfulMpdus) * payloadSize * 8.0 / interval;
    }
};

/// Aggregate statistics for a node across its two links (link 0 = 2.4 GHz, link 1 = 5 GHz).
struct NodeStats
{
    LinkStats link[2]; //!< per-link statistics: link[0] = 2.4 GHz, link[1] = 5 GHz

    /**
     * \return the total number of successfully acknowledged MPDUs across both links
     */
    double TotalSuccMpdu() const
    {
        return link[0].successfulMpdus + link[1].successfulMpdus;
    }

    /**
     * \param payloadSize the application payload size, in bytes
     * \param interval the measurement interval, in microseconds
     * \return the aggregate throughput across both links, in Mbit/s
     */
    double Tput(uint32_t payloadSize, double interval) const
    {
        return static_cast<double>(TotalSuccMpdu()) * payloadSize * 8.0 / interval;
    }
};

NodeStats statsMLD; //!< statistics for the single MLD STA under test

/**
 * BlockAck-manager "BlockAckResult" trace sink: accumulates the MLD STA's
 * per-link successful/failed MPDU counters and logs the result.
 *
 * \param statsBeginTime start of the measurement window; results before this time are ignored
 * \param statsEndTime end of the measurement window; results at or after this time are ignored
 * \param recipient the address of the BlockAck recipient (i.e. the peer that sent it)
 * \param tid the traffic ID the BlockAck applies to
 * \param linkId the link the BlockAck was received on (only 0 and 1 are tracked)
 * \param nSuccessfulMpdus number of MPDUs acknowledged as successful
 * \param nFailedMpdus number of MPDUs acknowledged as failed
 */
void
NotifyBlockAckResult(Time statsBeginTime,
                      Time statsEndTime,
                      Mac48Address recipient,
                      uint8_t tid,
                      uint8_t linkId,
                      uint16_t nSuccessfulMpdus,
                      uint16_t nFailedMpdus)
{
    const Time now = Simulator::Now();
    if (now < statsBeginTime || now >= statsEndTime || linkId > 1)
    {
        return;
    }

    statsMLD.link[linkId].successfulMpdus += nSuccessfulMpdus;
    statsMLD.link[linkId].failedMpdus += nFailedMpdus;
}

/**
 * Print a summary of the MLD's per-link throughput/PER and the interfering
 * SLD STAs' probe ratio and throughput, plus per-band averages across SLDs.
 *
 * \param statsBeginTime start of the measurement window
 * \param statsEndTime end of the measurement window
 * \param payloadSize the application payload size, in bytes
 */
void
PrintStats(Time statsBeginTime, Time statsEndTime, uint32_t payloadSize)
{
    const double interval = (statsEndTime - statsBeginTime).GetMicroSeconds();
    std::cout << "[ " << statsBeginTime.As(Time::S) << " - " << statsEndTime.As(Time::S) << " ]\n";

    // MLD summary.
    std::cout << "MLD Throughput " << statsMLD.Tput(payloadSize, interval) << " Mbit/s "
              << statsMLD.link[0].Tput(payloadSize, interval) << " Mbit/s (2.4G) "
              << statsMLD.link[1].Tput(payloadSize, interval) << " Mbit/s (5G)\n"
              << "PER: 2.4G=" << statsMLD.link[0].LossRate() << "  5G=" << statsMLD.link[1].LossRate()
              << "\n"
              << "Successful/Failed MPDUs:"
              << "  2.4G=" << statsMLD.link[0].successfulMpdus << "/" << statsMLD.link[0].failedMpdus
              << "  5G=" << statsMLD.link[1].successfulMpdus << "/" << statsMLD.link[1].failedMpdus
              << "  total=" << statsMLD.link[0].successfulMpdus + statsMLD.link[1].successfulMpdus
              << "/" << statsMLD.link[0].failedMpdus + statsMLD.link[1].failedMpdus << "\n";
}

int
main(int argc, char* argv[])
{
    // ---------------- Command-line configurable parameters ----------------
    uint32_t seedNumber = 1;
    uint32_t mcs0 = 13;
    uint32_t mcs1 = 10;
    uint32_t bw0 = 20;
    uint32_t bw1 = 80;

    uint16_t mpduBufferSize{512};
    uint32_t maxAmpduNum0 = 10;
    uint32_t maxAmpduNum1 = 10;
    uint32_t maxAmpduNumSld0 = 1;
    uint32_t maxAmpduNumSld1 = 1;

    uint32_t nStaSlds0 = 1; //!< number of interfering SLD STAs on link 0 (2.4 GHz)
    uint32_t nStaSlds1 = 1; //!< number of interfering SLD STAs on link 1 (5 GHz)
    double period = 0.5;
    uint32_t nss = 2;
    double fixedPER0 = 0.0;
    double fixedPER1 = 0.0;

    double simT = 5.5;
    bool logsender = false;
    bool logreceiver = false;
    bool distributedSender = true;
    bool distributedReceiver = true;
    bool enableAmpduLimit = true;
    Time simT_delayEnd = NanoSeconds(2);
    uint32_t policyint = 6;
    std::string scenario = "default";

    CommandLine cmd(__FILE__);
    std::filesystem::path filepath = __FILE__;
    cmd.AddValue("seed", "seed number", seedNumber);
    cmd.AddValue("mcs0", "MCS for 2.4 GHz", mcs0);
    cmd.AddValue("mcs1", "MCS for 5 GHz", mcs1);
    cmd.AddValue("bw0", "band width on 2.4 GHz", bw0);
    cmd.AddValue("bw1", "band width on 5 GHz", bw1);
    cmd.AddValue("bawsize", "BA Window Size", mpduBufferSize);
    cmd.AddValue("simt", "simulation time", simT);
    cmd.AddValue("period", "throughput measurement bucket size", period);
    cmd.AddValue("nss", "number of spatial streams (MIMO)", nss);
    cmd.AddValue("nsld0", "number of interfering SLD STAs on link 0", nStaSlds0);
    cmd.AddValue("nsld1", "number of interfering SLD STAs on link 1", nStaSlds1);
    cmd.AddValue("logsender", "enable transmitter-side logging", logsender);
    cmd.AddValue("logreceiver", "enable receiver-side logging", logreceiver);
    cmd.AddValue("distributedSender", "enable distributed sender-side read pointers", distributedSender);
    cmd.AddValue("distributedReceiver", "enable distributed receiver-side BA scoreboards", distributedReceiver);
    cmd.AddValue("ampduLimit", "enable per-link A-MPDU aggregation limits", enableAmpduLimit);
    cmd.AddValue("policy", "A-MPDU limit control policy ID", policyint);
    cmd.AddValue("maxampdunum0", "max A-MPDU size (MPDUs) for the MLD on link 0", maxAmpduNum0);
    cmd.AddValue("maxampdunum1", "max A-MPDU size (MPDUs) for the MLD on link 1", maxAmpduNum1);
    cmd.AddValue("ampdunumsld0", "max A-MPDU size (MPDUs) for SLD STAs on link 0", maxAmpduNumSld0);
    cmd.AddValue("ampdunumsld1", "max A-MPDU size (MPDUs) for SLD STAs on link 1", maxAmpduNumSld1);
    cmd.AddValue("scenario", "simulation scenario name (used for output paths)", scenario);
    cmd.AddValue("fixedPER0", "fixed PER for link 0", fixedPER0);
    cmd.AddValue("fixedPER1", "fixed PER for link 1", fixedPER1);
    cmd.Parse(argc, argv);

    const uint32_t maxAmpduSizeSld0 = maxAmpduNumSld0 * (1500 + 72);
    const uint32_t maxAmpduSizeSld1 = maxAmpduNumSld1 * (1500 + 72);
    const uint32_t originalSeed = seedNumber;

    // Human-readable policy name, used both for logging and for the output filenames.
    std::string policy;
    switch (policyint)
    {
    case 1:
        policy = "greedy";
        break;
    case 2:
        policy = "damla";
        break;
    case 3:
        policy = "only2G";
        break;
    case 4:
        policy = "only5G";
        break;
    case 6:
        policy = "bothset";
        break;
    default:
        policy = "unknown";
        break;
    }

    const uint32_t effectiveMaxAmpduNumSld0 = nStaSlds0 == 0 ? 0 : maxAmpduNumSld0;
    const uint32_t effectiveMaxAmpduNumSld1 = nStaSlds1 == 0 ? 0 : maxAmpduNumSld1;
    const uint32_t effectiveMaxAmpduNum0 = policyint == 6 ? maxAmpduNum0 : 0;
    const uint32_t effectiveMaxAmpduNum1 = policyint == 6 ? maxAmpduNum1 : 0;

    // ---------------- Build the output file title / directory ----------------
    // Compact naming scheme:
    //   <policy>-w<BAW>-bw<BW0>x<BW1>-m<MCS0>x<MCS1>-nss<NSS>
    //   -sld<N0>x<N1>-sa<SLD-A-MPDU0>x<SLD-A-MPDU1>
    //   -per<PER0>x<PER1>[-ma<MLD-A-MPDU0>x<MLD-A-MPDU1>]
    //   -ds<DISTRIBUTED_SENDER>-dr<DISTRIBUTED_RECEIVER>-al<A-MPDU_LIMIT>
    //   -t<SIM_TIME>-dt<STATS_PERIOD>-s<SEED>
    //
    // All result-affecting command-line parameters are retained to prevent
    // different configurations from overwriting the same trace files.
    std::ostringstream oss;
    oss << policy << "-w" << mpduBufferSize << "-bw" << bw0 << "x" << bw1 << "-m" << mcs0
        << "x" << mcs1 << "-nss" << nss << "-sld" << static_cast<uint32_t>(nStaSlds0) << "x"
        << static_cast<uint32_t>(nStaSlds1) << "-sa" << effectiveMaxAmpduNumSld0 << "x"
        << effectiveMaxAmpduNumSld1 << "-per" << fixedPER0 << "x" << fixedPER1;
    if (policyint == 6)
    {
        oss << "-ma" << effectiveMaxAmpduNum0 << "x" << effectiveMaxAmpduNum1;
    }
    oss << "-ds" << distributedSender << "-dr" << distributedReceiver << "-al"
        << enableAmpduLimit << "-t" << simT << "-dt" << period << "-s" << seedNumber;

    const std::string title = oss.str();
    const std::filesystem::path scenarioDir = filepath.parent_path() / scenario;
    std::filesystem::create_directories(scenarioDir);

    auto prepareFile = [&](const std::string& name) {
        const std::string fullpath = (scenarioDir / name).string();
        std::filesystem::remove(fullpath);
        return fullpath;
    };

    ppduTxOutputFile = prepareFile(title + "_PPDU.csv");
    rtsctsTxOutputFile = prepareFile(title + "_RTSCTS.csv");
    baTxOutputFile = prepareFile(title + "_BA.csv");

    // Distributed multi-radio MLD sender processing: bit 0 enables per-link read
    // pointers and delayed cross-link synchronization. A-MPDU limiting is
    // controlled independently by the EnableAmpduLimit attribute.
    uint8_t senderModeFlags = distributedSender ? (1 << 0) : 0;
    if (logsender)
    {
        senderModeFlags |= (1 << 2);
    }

    // Distributed multi-radio MLD receiver processing: bit 1 selects the
    // independent-scoreboard BA mode (IEEE 802.11be
    // Clause 35.3.8, Mode (i)): each affiliated STA maintains its own BA
    // scoreboard and is not required to synchronize it with the other
    // affiliated STAs, so a BA frame generated on one link only reflects the
    // reception status of MPDUs delivered on that same link.
    uint8_t receiverModeFlags = distributedReceiver ? (1 << 1) : 0;
    if (distributedReceiver && logreceiver)
    {
        receiverModeFlags |= (1 << 3);
    }

    // ---------------- Fixed simulation parameters ----------------
    const uint8_t nLinks = 2;
    RngSeedManager::SetSeed(seedNumber);
    RngSeedManager::SetRun(seedNumber);
    const double txPower = 20;
    const bool useRts{true};
    const int gi = 800;
    const Time simulationTime{Seconds(simT)};
    const std::size_t nStaMlds{1};
    const uint32_t payloadSize = 1500;

    if (useRts)
    {
        Config::SetDefault("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue("0"));
    }

    Config::SetDefault("ns3::WifiMacQueue::MaxSize",
                        QueueSizeValue(QueueSize(QueueSizeUnit::PACKETS, 1024 * 2)));
    Config::SetDefault("ns3::WifiRemoteStationManager::FragmentationThreshold",
                        UintegerValue(std::numeric_limits<uint32_t>::max()));
    Config::SetDefault("ns3::WifiRemoteStationManager::MaxSlrc",
                        UintegerValue(std::numeric_limits<uint32_t>::max()));
    Config::SetDefault("ns3::WifiRemoteStationManager::MaxSsrc",
                        UintegerValue(std::numeric_limits<uint32_t>::max()));

    // ---------------- Nodes and devices ----------------
    NodeContainer apNodes;
    NodeContainer mldNodes;
    apNodes.Create(1);
    mldNodes.Create(nStaMlds + nStaSlds0 + nStaSlds1);
    NetDeviceContainer apDev;
    NetDeviceContainer mldDev;

    // ---------------- WiFi (EHT) configuration ----------------
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211be);
    std::vector<uint32_t> mcs{mcs0, mcs1};
    std::vector<uint32_t> bandwidth{bw0, bw1};
    std::vector<uint32_t> channelnum{0, 0};

    for (uint8_t i = 0; i < nLinks; ++i)
    {
        const std::string dataModeStr = "EhtMcs" + std::to_string(mcs[i]);
        const uint64_t nonHtRefRateMbps = EhtPhy::GetNonHtReferenceRate(mcs[i]) / 1e6;
        const std::string ctrlRateStr = (i == 0)
                                             ? "ErpOfdmRate" + std::to_string(nonHtRefRateMbps) + "Mbps"
                                             : "OfdmRate" + std::to_string(nonHtRefRateMbps) + "Mbps";
        const double dataRateMbps =
            EhtPhy::GetDataRate(mcs[i], bandwidth[i], NanoSeconds(gi), 1) * nss / 1e6;
        std::cout << "[LINK_RATE]"
                  << " link=" << +i
                  << " controlMode=" << ctrlRateStr
                  << " dataMode=" << dataModeStr
                  << " dataRateMbps=" << dataRateMbps << std::endl;
        wifi.SetRemoteStationManager(i,
                                      "ns3::ConstantRateWifiManager",
                                      "DataMode",
                                      StringValue(dataModeStr),
                                      "ControlMode",
                                      StringValue(ctrlRateStr));
    }

    // ---------------- Propagation loss model configuration ----------------
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

    // ---------------- PHY configuration ----------------
    SpectrumWifiPhyHelper phy(nLinks);
    if (nss > 1)
    {
        phy.Set("Antennas", UintegerValue(nss));
        phy.Set("MaxSupportedTxSpatialStreams", UintegerValue(nss));
        phy.Set("MaxSupportedRxSpatialStreams", UintegerValue(nss));
    }
    phy.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);
    phy.SetErrorRateModel("ns3::TableBasedErrorRateModel");
    phy.AddChannel(spectrumChannel_2, WIFI_SPECTRUM_2_4_GHZ);
    phy.AddChannel(spectrumChannel_5, WIFI_SPECTRUM_5_GHZ);

    for (uint8_t i = 0; i < nLinks; ++i)
    {
        if (i == 0)
        {
            phy.AddPhyToFreqRangeMapping(i, WIFI_SPECTRUM_2_4_GHZ);
        }
        else
        {
            phy.AddPhyToFreqRangeMapping(i, WIFI_SPECTRUM_5_GHZ);
        }
    }
    phy.Set("TxPowerStart", DoubleValue(txPower));
    phy.Set("TxPowerEnd", DoubleValue(txPower));
    for (uint8_t i = 0; i < nLinks; ++i)
    {
        const std::string channelSetting =
            "{" + std::to_string(channelnum[i]) + ", " + std::to_string(bandwidth[i]);
        if (i == 0)
        {
            phy.Set(i, "ChannelSettings", StringValue(channelSetting + ", BAND_2_4GHZ, 0}"));
        }
        else
        {
            phy.Set(i, "ChannelSettings", StringValue(channelSetting + ", BAND_5GHZ, 0}"));
        }
    }

    // ---------------- MAC configuration ----------------
    WifiMacHelper mac;
    const Ssid bssSsid = Ssid("AP-MLD");
    const uint64_t beaconInterval =
        std::min<uint64_t>((ceil((simT * 1000000) / 1024) * 1024), (65535 * 1024));

    mac.SetType("ns3::ApWifiMac",
                "BeaconInterval",
                TimeValue(MicroSeconds(beaconInterval)),
                "EnableBeaconJitter",
                BooleanValue(false),
                "Ssid",
                SsidValue(bssSsid));
    apDev = wifi.Install(phy, mac, apNodes.Get(0));

    mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(bssSsid), "ActiveProbing", BooleanValue(false));

    // The interfering "SLD" nodes intentionally use the same two-link device
    // installation as the MLD STA. For this uplink throughput experiment, an
    // SLD is represented equivalently by mapping all of its TIDs to exactly one
    // link below: link 0 for the 2.4 GHz interferers and link 1 for the 5 GHz
    // interferers. Consequently, their application QoS data occupies only the
    // designated link, while the common installation keeps node creation and
    // per-link PHY/MAC configuration uniform.
    mldDev = wifi.Install(phy, mac, mldNodes);

    const Time statsBeginTime = Seconds(1.5);
    const Time statsEndTime =
        Seconds(period) * ((simulationTime + simT_delayEnd) / Seconds(period)).GetInt();
    std::cout << "[STATS_WINDOW]"
              << " begin=" << statsBeginTime.As(Time::S)
              << " end=" << statsEndTime.As(Time::S)
              << " duration=" << (statsEndTime - statsBeginTime).As(Time::S) << std::endl;

    // ---------------- Trace connections ----------------
    // AP and the MLD STA under test: connect PHY-level CSV logging (PpduTxRecord).
    for (uint8_t linkId = 0; linkId < nLinks; ++linkId)
    {
        DynamicCast<WifiNetDevice>(apDev.Get(0))
            ->GetPhy(linkId)
            ->TraceConnectWithoutContext("PpduTxDuration",
                                          MakeBoundCallback(&PpduTxRecord, StaType::MLD_AP, -1));
        DynamicCast<WifiNetDevice>(mldDev.Get(0))
            ->GetPhy(linkId)
            ->TraceConnectWithoutContext("PpduTxDuration",
                                          MakeBoundCallback(&PpduTxRecord, StaType::MLD_STA, -1));
    }
    DynamicCast<WifiNetDevice>(mldDev.Get(0))
        ->GetMac()
        ->GetQosTxop(0)
        ->GetBaManager()
        ->TraceConnectWithoutContext("BlockAckResult",
                                      MakeBoundCallback(&NotifyBlockAckResult,
                                                        statsBeginTime,
                                                        statsEndTime));

    // Interfering SLD STAs on link 0 (2.4 GHz).
    for (std::size_t id = nStaMlds; id < nStaMlds + nStaSlds0; ++id)
    {
        const uint32_t staIndex = id - nStaMlds;
        for (uint8_t linkId = 0; linkId < nLinks; ++linkId)
        {
            DynamicCast<WifiNetDevice>(mldDev.Get(id))
                ->GetPhy(linkId)
                ->TraceConnectWithoutContext(
                    "PpduTxDuration",
                    MakeBoundCallback(&PpduTxRecord, StaType::SLD_2G, staIndex));
        }
    }

    // Interfering SLD STAs on link 1 (5 GHz).
    for (std::size_t id = nStaMlds + nStaSlds0; id < nStaMlds + nStaSlds0 + nStaSlds1; ++id)
    {
        const uint32_t staIndex = id - nStaMlds - nStaSlds0;
        for (uint8_t linkId = 0; linkId < nLinks; ++linkId)
        {
            DynamicCast<WifiNetDevice>(mldDev.Get(id))
                ->GetPhy(linkId)
                ->TraceConnectWithoutContext(
                    "PpduTxDuration",
                    MakeBoundCallback(&PpduTxRecord, StaType::SLD_5G, staIndex));
        }
    }

    NetDeviceContainer devices;
    devices.Add(apDev);
    devices.Add(mldDev);
    wifi.AssignStreams(devices, seedNumber);

    Config::Set("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/HeConfiguration/GuardInterval",
                TimeValue(NanoSeconds(gi)));

    // ---------------- Per-node A-MPDU size / buffer configuration ----------------
    NodeContainer allNodes(apNodes, mldNodes);
    for (uint32_t i = 0; i < allNodes.GetN(); ++i)
    {
        if (i >= apNodes.GetN() + nStaMlds + nStaSlds0)
        {
            Config::Set("/NodeList/" + std::to_string(i) +
                            "/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_MaxAmpduSize",
                        UintegerValue(maxAmpduSizeSld1));
        }
        else if (i >= apNodes.GetN() + nStaMlds)
        {
            Config::Set("/NodeList/" + std::to_string(i) +
                            "/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_MaxAmpduSize",
                        UintegerValue(maxAmpduSizeSld0));
        }
    }

    // AP and MLD STA get a large nominal A-MPDU size cap; the actual limit is
    // enforced dynamically by AmpduLimitController via the BE_Txop attributes below.
    Config::Set("/NodeList/0/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_MaxAmpduSize",
                UintegerValue(1024 * 8 * (1500 + 150)));
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_MaxAmpduSize",
                UintegerValue(1024 * 8 * (1500 + 150)));
    Config::Set("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/MpduBufferSize",
                UintegerValue(mpduBufferSize));

    // ---------------- Mobility: circular layout around the AP ----------------
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    positionAlloc->Add(Vector(0.0, 2.0, 0.0));

    const double bssRadius = 0.1;
    const double step = 360.0 / (nStaMlds + nStaSlds0 + nStaSlds1);
    for (uint32_t i = 0; i < nStaMlds + nStaSlds0 + nStaSlds1; i++)
    {
        const double ang = step * i * M_PI / 180.0;
        const double x = 0.0 + bssRadius * cos(ang);
        const double y = 2.0 + bssRadius * sin(ang);
        positionAlloc->Add(Vector(x, y, 0.0));
    }

    MobilityHelper mobility;
    mobility.SetPositionAllocator(positionAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(allNodes);

    // ---------------- Internet stack ----------------
    InternetStackHelper stack;
    stack.Install(allNodes);
    seedNumber += stack.AssignStreams(allNodes, seedNumber);

    Ipv4AddressHelper address;
    address.SetBase("10.0.0.0", "255.255.255.0");
    const Ipv4InterfaceContainer apNodeInterface = address.Assign(apDev.Get(0));
    const Ipv4InterfaceContainer mldNodeInterface = address.Assign(mldDev);


    // ---------------- Applications ----------------
    uint16_t port = 9;
    UdpServerHelper server(port);
    ApplicationContainer ulserverApp = server.Install(apNodes.Get(0));
    seedNumber += server.AssignStreams(apNodes.Get(0), seedNumber);
    ulserverApp.Start(Seconds(0.0));
    ulserverApp.Stop(simulationTime + simT_delayEnd);

    const auto maxLoad2 = EhtPhy::GetDataRate(mcs[0], bandwidth[0], NanoSeconds(gi), 1) * nss;
    const auto maxLoad5 = EhtPhy::GetDataRate(mcs[1], bandwidth[1], NanoSeconds(gi), 1) * nss;

    // MLD STA offered load is split evenly between the two links; each SLD
    // STA is loaded up to its own link's max rate.
    const auto packetInterval = payloadSize * 8.0 / (maxLoad2 + maxLoad5) / 2;
    const auto packetInterval2 = payloadSize * 8.0 / maxLoad2;
    const auto packetInterval5 = payloadSize * 8.0 / maxLoad5;

    UdpClientHelper client(apNodeInterface.GetAddress(0), port);
    client.SetAttribute("MaxPackets", UintegerValue(0));
    client.SetAttribute("PacketSize", UintegerValue(payloadSize));
    for (uint32_t i = 0; i < mldNodes.GetN(); ++i)
    {
        if (i == 0)
        {
            client.SetAttribute("Interval", TimeValue(Seconds(packetInterval)));
        }
        else if (i < nStaMlds + nStaSlds0)
        {
            client.SetAttribute("Interval", TimeValue(Seconds(packetInterval2)));
        }
        else
        {
            client.SetAttribute("Interval", TimeValue(Seconds(packetInterval5)));
        }
        ApplicationContainer clientApp = client.Install(mldNodes.Get(i));
        seedNumber += client.AssignStreams(mldNodes.Get(i), seedNumber);
        clientApp.Start(Seconds(1));
        clientApp.Stop(simulationTime + simT_delayEnd);
    }

    // ---------------- TID-to-link mapping (MLO negotiation) ----------------
    for (auto i = mldDev.Begin(); i != mldDev.End(); ++i)
    {
        auto wifiDev = DynamicCast<WifiNetDevice>(*i);
        wifiDev->GetMac()->GetEhtConfiguration()->SetAttribute(
            "TidToLinkMappingNegSupport",
            EnumValue(WifiTidToLinkMappingNegSupport::ANY_LINK_SET));
    }

    const std::string mldMappingStr = "0,1,2,3,4,5,6,7 0,1"; //!< MLD STA: all TIDs on both links
    // Equivalent SLD representation: pin every TID to one designated link.
    const std::string mldMappingStr1 = "0,1,2,3,4,5,6,7 0"; //!< 2.4 GHz SLD
    const std::string mldMappingStr2 = "0,1,2,3,4,5,6,7 1"; //!< 5 GHz SLD

    std::size_t index = 0;
    for (auto i = mldDev.Begin(); i != mldDev.End(); ++i, ++index)
    {
        auto wifiDev = DynamicCast<WifiNetDevice>(*i);
        wifiDev->GetMac()->SetAttribute("ActiveProbing", BooleanValue(true));
        const std::string mappingToUse =
            (index < nStaMlds) ? mldMappingStr
                                : ((index < nStaMlds + nStaSlds0) ? mldMappingStr1 : mldMappingStr2);
        wifiDev->GetMac()->GetEhtConfiguration()->SetAttribute("TidToLinkMappingDl",
                                                                 StringValue(mappingToUse));
        wifiDev->GetMac()->GetEhtConfiguration()->SetAttribute("TidToLinkMappingUl",
                                                                 StringValue(mappingToUse));
    }

    auto apWifiDev = DynamicCast<WifiNetDevice>(apDev.Get(0));
    apWifiDev->GetMac()->GetEhtConfiguration()->SetAttribute("TidToLinkMappingDl",
                                                               StringValue(mldMappingStr));
    apWifiDev->GetMac()->GetEhtConfiguration()->SetAttribute("TidToLinkMappingUl",
                                                               StringValue(mldMappingStr));

    // ---------------- Fixed PER and A-MPDU limit controller configuration ----------------
    const std::vector<double> fixedPERs = {fixedPER0, fixedPER1};
    Config::Set("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/FixedPER",
                AttributeContainerValue<DoubleValue>(fixedPERs));

    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/UseExplicitBarAfterMissedBlockAck",
                BooleanValue(false));
    Config::Set("/NodeList/0/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/UseExplicitBarAfterMissedBlockAck",
                BooleanValue(false));
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/Mode",
                UintegerValue(senderModeFlags));
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/EnableAmpduLimit",
                BooleanValue(enableAmpduLimit));
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/Policy",
                UintegerValue(policyint));
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/MaxAmpduNum0",
                UintegerValue(maxAmpduNum0));
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/MaxAmpduNum1",
                UintegerValue(maxAmpduNum1));
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/DataRate24",
                DoubleValue(maxLoad2));
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/DataRate5",
                DoubleValue(maxLoad5));
    Config::Set("/NodeList/0/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/Mode",
                UintegerValue(receiverModeFlags));

    // ---------------- OBSS/BSS EDCA parameters (equal AIFSN/CW for all nodes) ----------------
    for (uint32_t i = 0; i < allNodes.GetN(); ++i)
    {
        Config::Set("/NodeList/" + std::to_string(i) +
                        "/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/Aifsns",
                    AttributeContainerValue<UintegerValue>(std::list<uint64_t>{2, 2}));
        Config::Set("/NodeList/" + std::to_string(i) +
                        "/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/MinCws",
                    AttributeContainerValue<UintegerValue>(std::list<int>{15, 15}));
        Config::Set("/NodeList/" + std::to_string(i) +
                        "/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/MaxCws",
                    AttributeContainerValue<UintegerValue>(std::list<int>{1023, 1023}));
    }

    const std::vector<Time> txopLimitList = {MicroSeconds(0), MicroSeconds(0)};
    Config::Set("/NodeList/1/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/TxopLimits",
                AttributeContainerValue<TimeValue>(txopLimitList));

    // ---------------- Run ----------------
    Simulator::Stop(simulationTime + simT_delayEnd);
    Simulator::Run();
    Simulator::Destroy();

    const double interval = (statsEndTime - statsBeginTime).GetMicroSeconds();
    UpdateThroughputCsv(scenario,
                         policy,
                         bw0,
                         bw1,
                         mcs0,
                         mcs1,
                         nss,
                         nStaSlds0,
                         nStaSlds1,
                         effectiveMaxAmpduNumSld0,
                         effectiveMaxAmpduNumSld1,
                         fixedPER0,
                         fixedPER1,
                         mpduBufferSize,
                         effectiveMaxAmpduNum0,
                         effectiveMaxAmpduNum1,
                         originalSeed,
                         statsMLD.Tput(payloadSize, interval),
                         statsMLD.link[0].Tput(payloadSize, interval),
                         statsMLD.link[1].Tput(payloadSize, interval));
    PrintStats(statsBeginTime, statsEndTime, payloadSize);

    return 0;
}