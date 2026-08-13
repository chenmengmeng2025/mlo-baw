/*
 * Copyright (c) 2019 Universita' degli Studi di Napoli Federico II
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Author: Stefano Avallone <stavallo@unina.it>
 */

#include "block-ack-window.h"
#include "ns3/simulator.h"
#include "wifi-utils.h"

#include "ns3/log.h"
#include <cmath>
#include <iomanip>
#include <ostream>
#include <vector>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("BlockAckWindow");

BlockAckWindow::BlockAckWindow()
    : m_winStart(0),
      m_head(0)
{
}

void
BlockAckWindow::Init(uint16_t winStart, uint16_t winSize)
{
    NS_LOG_FUNCTION(this << winStart << winSize);
    m_winStart = winStart;
    m_window.assign(winSize, false);
    m_statesWindow.assign(winSize, ElementState::UNACKED);
    m_head = 0;
}

void
BlockAckWindow::Reset(uint16_t winStart)
{
    Init(winStart, m_window.size());
}

uint16_t
BlockAckWindow::GetWinStart() const
{
    return m_winStart;
}

uint16_t
BlockAckWindow::GetWinEnd() const
{
    return (m_winStart + m_window.size() - 1) % SEQNO_SPACE_SIZE;
}

std::size_t
BlockAckWindow::GetWinSize() const
{
    return m_window.size();
}

std::vector<bool>::reference
BlockAckWindow::At(std::size_t distance)
{
    NS_ASSERT(distance < m_window.size());

    return m_window.at((m_head + distance) % m_window.size());
}

std::vector<bool>::const_reference
BlockAckWindow::At(std::size_t distance) const
{
    NS_ASSERT(distance < m_window.size());

    return m_window.at((m_head + distance) % m_window.size());
}

void
BlockAckWindow::Advance(std::size_t count)
{
    NS_LOG_FUNCTION(this << count);

    if (count >= m_window.size())
    {
        Reset((m_winStart + count) % SEQNO_SPACE_SIZE);
        return;
    }

    for (std::size_t i = 0; i < count; i++)
    {
        m_window[m_head] = false;
        m_statesWindow[m_head] = ElementState::UNACKED;
        m_head = (m_head + 1) % m_window.size();
    }
    m_winStart = (m_winStart + count) % SEQNO_SPACE_SIZE;
}

void
BlockAckWindow::SetElementState(std::size_t distance, ElementState state)
{
    NS_LOG_FUNCTION(this << distance << static_cast<uint32_t>(state));
    NS_ASSERT(distance < m_statesWindow.size());
    m_statesWindow.at((m_head + distance) % m_statesWindow.size()) = state;
}

BlockAckWindow::ElementState
BlockAckWindow::GetElementState(std::size_t distance) const
{
    NS_ASSERT(distance < m_statesWindow.size());
    return m_statesWindow.at((m_head + distance) % m_statesWindow.size());
}

static std::string
ElementStateToStr(BlockAckWindow::ElementState s)
{
    switch (s)
    {
    case BlockAckWindow::ElementState::UNACKED:     return "UNACKED    ";
    case BlockAckWindow::ElementState::ACKED:       return "ACKED      ";
    case BlockAckWindow::ElementState::INFLIGHT_2G: return "INFLIGHT_2G";
    case BlockAckWindow::ElementState::INFLIGHT_5G: return "INFLIGHT_5G";
    case BlockAckWindow::ElementState::INFLIGHT_6G: return "INFLIGHT_6G";
    default:                                        return "UNKNOWN    ";
    }
}

// ---------------------------------------------------------------------------
// Print without slot-type column
// ---------------------------------------------------------------------------

void
BlockAckWindow::Print(std::ostream& os) const
{
    os << "+-----------+-----------+-------------+\n"
       << "| SeqNo     | ACK Bitmap| State       |\n"
       << "+-----------+-----------+-------------+\n";

    std::size_t runStart = 0;
    while (runStart < m_window.size())
    {
        bool ack = m_window.at((m_head + runStart) % m_window.size());
        ElementState st = m_statesWindow.at((m_head + runStart) % m_statesWindow.size());

        // Extend the run while ack/state stay the same
        std::size_t runEnd = runStart;
        while (runEnd + 1 < m_window.size() &&
               m_window.at((m_head + runEnd + 1) % m_window.size()) == ack &&
               m_statesWindow.at((m_head + runEnd + 1) % m_statesWindow.size()) == st)
        {
            ++runEnd;
        }

        uint16_t seqNoStart = static_cast<uint16_t>((m_winStart + runStart) % SEQNO_SPACE_SIZE);
        uint16_t seqNoEnd = static_cast<uint16_t>((m_winStart + runEnd) % SEQNO_SPACE_SIZE);

        std::ostringstream seqNoStr;
        if (runStart == runEnd)
        {
            seqNoStr << seqNoStart;
        }
        else
        {
            seqNoStr << seqNoStart << "-" << seqNoEnd;
        }

        os << "| " << std::setw(9) << seqNoStr.str()
           << " |     " << (ack ? '1' : '0')
           << "     | " << ElementStateToStr(st) << " |\n";

        runStart = runEnd + 1;
    }

    os << "+-----------+-----------+-------------+\n";
}

// ---------------------------------------------------------------------------
// Print with slot-type column (b / l / u / a)
// ---------------------------------------------------------------------------
void
BlockAckWindow::Print(std::ostream& os, uint8_t linkId) const
{
    NS_ASSERT_MSG(linkId <= 1, "linkId must be 0 or 1, got " << +linkId);

    const ElementState otherInflight = InflightStateForLink(1 - linkId);
    const ElementState thisInflight  = InflightStateForLink(linkId);

    auto slotType = [&](ElementState s) -> char {
        if (s == otherInflight)         return 'b';
        if (s == thisInflight)          return 'l';
        if (s == ElementState::ACKED)   return 'a';
        if (s == ElementState::UNACKED) return 'u';
        return '?';
    };

    os << "+-----------+-----------+-------------+------+\n"
       << "| SeqNo     | ACK Bitmap| State       | Type |\n"
       << "+-----------+-----------+-------------+------+\n";

    std::size_t runStart = 0;
    while (runStart < m_window.size())
    {
        bool ack = m_window.at((m_head + runStart) % m_window.size());
        ElementState st = m_statesWindow.at((m_head + runStart) % m_statesWindow.size());

        // Extend the run while ack/state stay the same (slotType follows from st)
        std::size_t runEnd = runStart;
        while (runEnd + 1 < m_window.size() &&
               m_window.at((m_head + runEnd + 1) % m_window.size()) == ack &&
               m_statesWindow.at((m_head + runEnd + 1) % m_statesWindow.size()) == st)
        {
            ++runEnd;
        }

        uint16_t seqNoStart = static_cast<uint16_t>((m_winStart + runStart) % SEQNO_SPACE_SIZE);
        uint16_t seqNoEnd = static_cast<uint16_t>((m_winStart + runEnd) % SEQNO_SPACE_SIZE);

        std::ostringstream seqNoStr;
        if (runStart == runEnd)
        {
            seqNoStr << seqNoStart;
        }
        else
        {
            seqNoStr << seqNoStart << "-" << seqNoEnd;
        }

        os << "| " << std::setw(9) << seqNoStr.str()
           << " |     " << (ack ? '1' : '0')
           << "     | " << ElementStateToStr(st)
           << " |  " << slotType(st) << "   |\n";

        runStart = runEnd + 1;
    }

    os << "+-----------+-----------+-------------+------+\n";
}

double
BlockAckWindow::ComputeEffectiveBawSize(uint8_t linkId, double otherPer, bool log)
{
    NS_LOG_FUNCTION(this << +linkId << otherPer);
    NS_ASSERT_MSG(otherPer >= 0.0 && otherPer <= 1.0,
                  "otherPer must be in [0, 1], got " << otherPer);
    NS_ASSERT_MSG(linkId <= 1, "linkId must be 0 or 1, got " << +linkId);

    // The INFLIGHT state that belongs to the *other* link (type-b)
    // and the INFLIGHT state that belongs to *this* link (type-l).
    const ElementState otherInflight = InflightStateForLink(1 - linkId);
    const ElementState thisInflight  = InflightStateForLink(linkId);

    const std::size_t winSize = m_window.size();
    const double      q       = 1.0 - otherPer; // success prob of other link

    // K[i] is the number of consecutive type-a slots after the i-th type-b
    // slot in the prefix before the first type-l or type-u slot. The paper's
    // m is the number of type-b slots in this prefix, whereas Dother counts
    // type-b slots across the complete BAW.
    std::vector<std::size_t> K;
    std::size_t Dother = 0; // all type-b slots in the window
    std::size_t L = 0;      // type-l count
    std::size_t U = 0;      // type-u count
    bool prefixOpen = true;
    bool inARunAfterB = false;
    std::size_t currentKIdx = 0;

    for (std::size_t dist = 0; dist < winSize; ++dist)
    {
        const ElementState st = GetElementState(dist);

        if (st == otherInflight)
        {
            ++Dother;
            if (prefixOpen)
            {
                K.push_back(0);
                currentKIdx = K.size() - 1;
                inARunAfterB = true;
            }
        }
        else if (st == ElementState::ACKED)
        {
            if (prefixOpen && inARunAfterB)
            {
                ++K[currentKIdx];
            }
        }
        else if (st == thisInflight)
        {
            ++L;
            prefixOpen = false;
            inARunAfterB = false;
        }
        else if (st == ElementState::UNACKED)
        {
            ++U;
            prefixOpen = false;
            inARunAfterB = false;
        }
    }

    const std::size_t m = K.size();

    // ------------------------------------------------------------------
    // Evaluate the distributed-receiver specialization of Eq. (6).
    //
    //   sum_{i=1}^{m} [ (K_i + 1) * (1-p)^i ] + Dother*p + L + U
    //
    // Only the other link's local BA can report type-b MPDUs, hence the
    // paper's D1*p1 + D2*p2 reduces to Dother*p here.
    // ------------------------------------------------------------------
    double prefixSum = 0.0;
    double qPow = q; // (1-p)^i, starts at i=1

    for (std::size_t i = 0; i < m; ++i)
    {
        prefixSum += static_cast<double>(K[i] + 1) * qPow;
        qPow *= q;
    }

    const double retransmissionTerm = static_cast<double>(Dother) * otherPer;
    const double sum =
        prefixSum + retransmissionTerm + static_cast<double>(L) + static_cast<double>(U);

    if (m_lastEffectiveBawLogTimes[linkId] != Simulator::Now() && log)
    {
        std::cout << "[EFFECTIVE_BAW_STATE]"
                  << " timeNs=" << Simulator::Now().GetNanoSeconds()
                  << " link=" << +linkId << std::endl;
        Print(std::cout, linkId);

        double geometricBase = 0.0;
        double gapContribution = 0.0;
        double qPowLog = q;
        for (std::size_t i = 0; i < m; ++i)
        {
            geometricBase += qPowLog;
            gapContribution += static_cast<double>(K[i]) * qPowLog;
            qPowLog *= q;
        }

        std::cout << "[EFFECTIVE_BAW_PREFIX]"
                  << " formula=sum((K_i+1)*q^i)"
                  << " q=" << q
                  << " m=" << m
                  << " geometricBase=" << geometricBase
                  << " gapContribution=" << gapContribution
                  << " nonZeroK=[";
        bool first = true;
        for (std::size_t i = 0; i < m; ++i)
        {
            if (K[i] == 0)
            {
                continue;
            }
            if (!first)
            {
                std::cout << ",";
            }
            std::cout << (i + 1) << ":" << K[i];
            first = false;
        }
        std::cout << "] prefixSum=" << prefixSum << std::endl;

        std::cout << "[EFFECTIVE_BAW]"
                  << " timeNs=" << Simulator::Now().GetNanoSeconds()
                  << " link=" << +linkId
                  << " winSize=" << winSize
                  << " otherPer=" << otherPer
                  << " m=" << m
                  << " Dother=" << Dother
                  << " L=" << L
                  << " U=" << U
                  << " prefixSum=" << prefixSum
                  << " retransmissionTerm=" << retransmissionTerm
                  << " value=" << sum << std::endl;
    }

    m_lastEffectiveBawLogTimes[linkId] = Simulator::Now();

    return sum;
}

} // namespace ns3