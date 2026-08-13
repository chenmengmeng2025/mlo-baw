/*
 * Copyright (c) 2020 Universita' degli Studi di Napoli Federico II
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Author: Stefano Avallone <stavallo@unina.it>
 */

#include "recipient-block-ack-agreement.h"

#include "ctrl-headers.h"
#include "mac-rx-middle.h"
#include "wifi-mpdu.h"
#include "wifi-utils.h"

#include "ns3/log.h"
#include "ns3/packet.h"

#include <algorithm>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("RecipientBlockAckAgreement");

bool
RecipientBlockAckAgreement::Compare::operator()(const Key& a, const Key& b) const
{
    return ((a.first - *a.second + SEQNO_SPACE_SIZE) % SEQNO_SPACE_SIZE) <
           ((b.first - *b.second + SEQNO_SPACE_SIZE) % SEQNO_SPACE_SIZE);
}

RecipientBlockAckAgreement::RecipientBlockAckAgreement(Mac48Address originator,
                                                       bool amsduSupported,
                                                       uint8_t tid,
                                                       uint16_t bufferSize,
                                                       uint16_t timeout,
                                                       uint16_t startingSeq,
                                                       bool htSupported,
                                                       uint8_t nLinks,
                                                       uint32_t mode)
    : BlockAckAgreement(originator, tid)
{
    NS_LOG_FUNCTION(this << originator << amsduSupported << +tid << bufferSize << timeout
                         << startingSeq << htSupported);

    m_amsduSupported = amsduSupported;
    m_bufferSize = bufferSize;
    m_timeout = timeout;
    m_startingSeq = startingSeq;
    m_htSupported = htSupported;
    m_mode = mode;
    m_scoreboard.Init(startingSeq, bufferSize);
    m_linkScoreboards.resize(nLinks);
    for (auto& board : m_linkScoreboards)
    {
        board.Init(startingSeq, bufferSize);
    }
    m_winStartB = startingSeq;
    m_winSizeB = bufferSize;
}

RecipientBlockAckAgreement::~RecipientBlockAckAgreement()
{
    NS_LOG_FUNCTION_NOARGS();
    m_bufferedMpdus.clear();
    m_rxMiddle = nullptr;
}

void
RecipientBlockAckAgreement::SetMacRxMiddle(const Ptr<MacRxMiddle> rxMiddle)
{
    NS_LOG_FUNCTION(this << rxMiddle);
    m_rxMiddle = rxMiddle;
}

void
RecipientBlockAckAgreement::PassBufferedMpdusUntilFirstLost()
{
    NS_LOG_FUNCTION(this);

    // There cannot be old MPDUs in the buffer (we just check the MPDU with the
    // highest sequence number)
    NS_ASSERT((m_mode & (1 << 1)) || m_bufferedMpdus.empty() ||
              GetDistance(m_bufferedMpdus.rbegin()->first.first, m_winStartB) <
                  SEQNO_SPACE_HALF_SIZE);

    auto it = m_bufferedMpdus.begin();

    while (it != m_bufferedMpdus.end() && it->first.first == m_winStartB)
    {
        NS_LOG_DEBUG("Forwarding up: " << *it->second);
        m_rxMiddle->Receive(it->second, WIFI_LINKID_UNDEFINED);
        it = m_bufferedMpdus.erase(it);
        m_winStartB = (m_winStartB + 1) % SEQNO_SPACE_SIZE;
    }
}

void
RecipientBlockAckAgreement::PassBufferedMpdusWithSeqNumberLessThan(uint16_t newWinStartB)
{
    NS_LOG_FUNCTION(this << newWinStartB);

    // There cannot be old MPDUs in the buffer (we just check the MPDU with the
    // highest sequence number)
    NS_ASSERT((m_mode & (1 << 1)) || m_bufferedMpdus.empty() ||
              GetDistance(m_bufferedMpdus.rbegin()->first.first, m_winStartB) <
                  SEQNO_SPACE_HALF_SIZE);

    auto it = m_bufferedMpdus.begin();

    while (it != m_bufferedMpdus.end() &&
           GetDistance(it->first.first, m_winStartB) < GetDistance(newWinStartB, m_winStartB))
    {
        NS_LOG_DEBUG("Forwarding up: " << *it->second);
        m_rxMiddle->Receive(it->second, WIFI_LINKID_UNDEFINED);
        it = m_bufferedMpdus.erase(it);
    }
    m_winStartB = newWinStartB;
}

void
RecipientBlockAckAgreement::NotifyReceivedMpdu(Ptr<const WifiMpdu> mpdu, uint8_t linkId)
{
    NS_LOG_FUNCTION(this << *mpdu);

    const bool distributedReceiver = (m_mode & (1 << 1));
    uint16_t mpduSeqNumber = mpdu->GetHeader().GetSequenceNumber();

    // Select the scoreboard: per-link scoreboard in distributed receiver mode,
    // shared scoreboard otherwise.
    NS_ABORT_MSG_IF(distributedReceiver && linkId >= m_linkScoreboards.size(),
                    "Invalid link ID " << +linkId << " for " << m_linkScoreboards.size()
                                       << " recipient scoreboards");

    // The shared reordering window provides the common sequence-number reference
    // used below to keep old or delayed duplicates out of the global reorder buffer.
    // The local scoreboard is updated first so that this link can still acknowledge
    // every MPDU it actually receives.
    const uint16_t globalDistance = GetDistance(mpduSeqNumber, m_winStartB);

    auto& scoreboard = distributedReceiver ? m_linkScoreboards[linkId] : m_scoreboard;

    uint16_t distance = GetDistance(mpduSeqNumber, scoreboard.GetWinStart());

    /* Update the scoreboard (see Section 10.24.7.3 of 802.11-2016) */
    if (distance < scoreboard.GetWinSize())
    {
        // set to 1 the bit in position SN within the bitmap
        scoreboard.At(distance) = true;
    }
    else if (distance < SEQNO_SPACE_HALF_SIZE)
    {
        scoreboard.Advance(distance - scoreboard.GetWinSize() + 1);
        scoreboard.At(scoreboard.GetWinSize() - 1) = true;
    }
    else if (distributedReceiver && globalDistance < SEQNO_SPACE_HALF_SIZE &&
             distance >= SEQNO_SPACE_HALF_SIZE)
    {
        // The MPDU is current globally, so the upper-half local distance means that
        // this link's independent scoreboard lags the shared receive state. Move the
        // local window forward so that this link's BA can report the received MPDU.
        const auto previousWinStart = scoreboard.GetWinStart();
        scoreboard.Advance(distance - scoreboard.GetWinSize() + 1);
        scoreboard.At(scoreboard.GetWinSize() - 1) = true;

        if (m_mode & (1 << 3))
        {
            std::cout << "[RX_SCOREBOARD_RESYNC]"
                      << " timeNs=" << Simulator::Now().GetNanoSeconds()
                      << " link=" << +linkId
                      << " sn=" << mpduSeqNumber
                      << " localWin=" << previousWinStart << "->" << scoreboard.GetWinStart()
                      << " localDistance=" << distance
                      << " globalWin=" << m_winStartB
                      << " globalDistance=" << globalDistance << std::endl;
        }
    }

    if (globalDistance >= SEQNO_SPACE_HALF_SIZE)
    {
        if (distributedReceiver && (m_mode & (1 << 3)))
        {
            std::cout << "[RX_GLOBAL_OLD_DROP]"
                      << " timeNs=" << Simulator::Now().GetNanoSeconds()
                      << " link=" << +linkId
                      << " sn=" << mpduSeqNumber
                      << " localWin=" << scoreboard.GetWinStart()
                      << " globalWin=" << m_winStartB
                      << " globalDistance=" << globalDistance << std::endl;
        }
        return;
    }

    distance = globalDistance;

    /* Update the receive reordering buffer (see Section 10.24.7.6.2 of 802.11-2016) */
    if (distance < m_winSizeB)
    {
        // 1. Store the received MPDU in the buffer, if no MSDU with the same sequence
        // number is already present
        m_bufferedMpdus.insert({{mpduSeqNumber, &m_winStartB}, mpdu});

        // 2. Pass MSDUs or A-MSDUs up to the next MAC process if they are stored in
        // the buffer in order of increasing value of the Sequence Number subfield
        // starting with the MSDU or A-MSDU that has SN=WinStartB
        // 3. Set WinStartB to the value of the Sequence Number subfield of the last
        // MSDU or A-MSDU that was passed up to the next MAC process plus one.
        PassBufferedMpdusUntilFirstLost();
    }
    else if (distance < SEQNO_SPACE_HALF_SIZE)
    {
        // 1. Store the received MPDU in the buffer, if no MSDU with the same sequence
        // number is already present
        m_bufferedMpdus.insert({{mpduSeqNumber, &m_winStartB}, mpdu});

        // 2. Set WinEndB = SN
        // 3. Set WinStartB = WinEndB – WinSizeB + 1
        // 4. Pass any complete MSDUs or A-MSDUs stored in the buffer with Sequence Number
        // subfield values that are lower than the new value of WinStartB up to the next
        // MAC process in order of increasing Sequence Number subfield value. Gaps may
        // exist in the Sequence Number subfield values of the MSDUs or A-MSDUs that are
        // passed up to the next MAC process.
        const auto newWinStart =
            (mpduSeqNumber + SEQNO_SPACE_SIZE - m_winSizeB + 1) % SEQNO_SPACE_SIZE;
        PassBufferedMpdusWithSeqNumberLessThan(newWinStart);

        // 5. Pass MSDUs or A-MSDUs stored in the buffer up to the next MAC process in
        // order of increasing value of the Sequence Number subfield starting with
        // WinStartB and proceeding sequentially until there is no buffered MSDU or
        // A-MSDU for the next sequential Sequence Number subfield value
        PassBufferedMpdusUntilFirstLost();
    }
}

void
RecipientBlockAckAgreement::Flush()
{
    NS_LOG_FUNCTION(this);

    if (m_mode & (1 << 1))
    {
        for (const auto& [key, mpdu] : m_bufferedMpdus)
        {
            m_rxMiddle->Receive(mpdu, WIFI_LINKID_UNDEFINED);
        }
        m_bufferedMpdus.clear();
        return;
    }

    PassBufferedMpdusWithSeqNumberLessThan(m_scoreboard.GetWinStart());
    PassBufferedMpdusUntilFirstLost();
}

void
RecipientBlockAckAgreement::NotifyReceivedBar(uint16_t startingSequenceNumber, uint8_t linkId)
{
    NS_LOG_FUNCTION(this << startingSequenceNumber);

    const bool distributedReceiver = (m_mode & (1 << 1));

    // Select the scoreboard: per-link scoreboard in distributed receiver mode,
    // shared scoreboard otherwise.
    NS_ABORT_MSG_IF(distributedReceiver && linkId >= m_linkScoreboards.size(),
                    "Invalid link ID " << +linkId << " for " << m_linkScoreboards.size()
                                       << " recipient scoreboards");

    const uint16_t globalDistance = GetDistance(startingSequenceNumber, m_winStartB);
    auto& scoreboard = distributedReceiver ? m_linkScoreboards[linkId] : m_scoreboard;

    uint16_t distance = GetDistance(startingSequenceNumber, scoreboard.GetWinStart());

    if (distributedReceiver && (m_mode & (1 << 3)))
    {
        std::cout << Simulator::Now() << " ReceivedBar: startingSeqno = " << startingSequenceNumber
                  << " on Link " << +linkId << " distance = " << distance
                  << " winStart = " << scoreboard.GetWinStart() << std::endl;
    }

    /* Update the scoreboard (see Section 10.24.7.3 of 802.11-2016) */
    if (distance > 0 && distance < scoreboard.GetWinSize())
    {
        // advance by SSN - WinStartR, so that WinStartR becomes equal to SSN
        scoreboard.Advance(distance);
        NS_ASSERT(scoreboard.GetWinStart() == startingSequenceNumber);
    }
    else if (distance > 0 && distance < SEQNO_SPACE_HALF_SIZE)
    {
        // reset the window and set WinStartR to SSN
        scoreboard.Reset(startingSequenceNumber);
    }
    else if (distributedReceiver && globalDistance < SEQNO_SPACE_HALF_SIZE &&
             distance >= SEQNO_SPACE_HALF_SIZE)
    {
        if (m_mode & (1 << 3))
        {
            std::cout << "ReceivedBar: distance >= SEQNO_SPACE_HALF_SIZE, startingseqno = "
                      << startingSequenceNumber
                      << ", before WinStart = " << scoreboard.GetWinStart() << " m_mode "
                      << m_mode << std::endl;
        }

        scoreboard.Reset(startingSequenceNumber);

        if (m_mode & (1 << 3))
        {
            std::cout << "after WinStart = " << scoreboard.GetWinStart() << std::endl;
        }
    }
    // else (sync mode, distance == 0 or distance >= SEQNO_SPACE_HALF_SIZE): ignore, as before.

    if (globalDistance >= SEQNO_SPACE_HALF_SIZE)
    {
        if (distributedReceiver && (m_mode & (1 << 3)))
        {
            std::cout << Simulator::Now() << " Link" << +linkId
                      << " excluded globally old BAR from shared reorder state: SSN = "
                      << startingSequenceNumber << " globalWinStart = " << m_winStartB
                      << std::endl;
        }
        return;
    }

    distance = globalDistance;

    /* Update the receive reordering buffer (see Section 10.24.7.6.2 of 802.11-2016) */
    if (distance > 0 && distance < SEQNO_SPACE_HALF_SIZE)
    {
        // 1. set WinStartB = SSN
        // 3. Pass any complete MSDUs or A-MSDUs stored in the buffer with Sequence
        // Number subfield values that are lower than the new value of WinStartB up to
        // the next MAC process in order of increasing Sequence Number subfield value
        PassBufferedMpdusWithSeqNumberLessThan(startingSequenceNumber);

        // 4. Pass MSDUs or A-MSDUs stored in the buffer up to the next MAC process
        // in order of increasing Sequence Number subfield value starting with
        // SN=WinStartB and proceeding sequentially until there is no buffered MSDU
        // or A-MSDU for the next sequential Sequence Number subfield value
        PassBufferedMpdusUntilFirstLost();
    }
}

void
RecipientBlockAckAgreement::FillBlockAckBitmap(CtrlBAckResponseHeader* blockAckHeader,
                                               uint8_t linkId, std::size_t index) const
{
    NS_LOG_FUNCTION(this << blockAckHeader << index);
    if (blockAckHeader->IsBasic())
    {
        NS_FATAL_ERROR("Basic block ack is not supported.");
    }
    else if (blockAckHeader->IsMultiTid())
    {
        NS_FATAL_ERROR("Multi-tid block ack is not supported.");
    }
    else if (blockAckHeader->IsCompressed() || blockAckHeader->IsExtendedCompressed() ||
             blockAckHeader->IsMultiSta())
    {
        // Select the scoreboard: per-link scoreboard in distributed receiver mode,
        // shared scoreboard otherwise.
        const bool distributedReceiver = (m_mode & (1 << 1));
        NS_ABORT_MSG_IF(distributedReceiver && linkId >= m_linkScoreboards.size(),
                        "Invalid link ID " << +linkId << " for " << m_linkScoreboards.size()
                                           << " recipient scoreboards");
        const auto& scoreboard =
            distributedReceiver ? m_linkScoreboards[linkId] : m_scoreboard;

        // The Starting Sequence Number subfield of the Block Ack Starting Sequence
        // Control subfield of the BlockAck frame shall be set to any value in the
        // range (WinEndR – 63) to WinStartR (Sec. 10.24.7.5 of 802.11-2016).
        // We set it to WinStartR
        uint16_t ssn = scoreboard.GetWinStart();
        NS_LOG_DEBUG("SSN=" << ssn);
        blockAckHeader->SetStartingSequence(ssn, index);
        blockAckHeader->ResetBitmap(index);

        for (std::size_t i = 0; i < scoreboard.GetWinSize(); i++)
        {
            if (scoreboard.At(i))
            {
                blockAckHeader->SetReceivedPacket((ssn + i) % SEQNO_SPACE_SIZE, index);
            }
        }
    }
}

} // namespace ns3
