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
    // NS_ASSERT(distance < m_statesWindow.size());
    m_statesWindow.at((m_head + distance) % m_statesWindow.size()) = state;
}

BlockAckWindow::ElementState
BlockAckWindow::GetElementState(std::size_t distance) const
{
    // NS_ASSERT(distance < m_statesWindow.size());
    return m_statesWindow.at((m_head + distance) % m_statesWindow.size());
}

std::size_t
BlockAckWindow::CountByState(ElementState state) const
{
    std::size_t count = 0;
    for (std::size_t i = 0; i < m_statesWindow.size(); ++i)
    {
        if (m_statesWindow[i] == state)
        {
            ++count;
        }
    }
    return count;
}
// ---------------------------------------------------------------------------
// Shared helper: convert ElementState to a fixed-width display string
// ---------------------------------------------------------------------------
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

    // ------------------------------------------------------------------
    // First pass: collect K_i values.
    //
    // Walk slots from distance 0 to winSize-1 (logical order from winStart).
    // Whenever we see a type-b element we record the number of consecutive
    // type-a elements that immediately follow it (up to the next non-a slot
    // or the window end).  We do this in a single forward scan by keeping
    // a pointer to the "pending b" slot and counting a-runs after it.
    // ------------------------------------------------------------------

    // K[i] = number of consecutive type-a slots after the i-th type-b slot
    std::vector<std::size_t> K;
    std::size_t L = 0; // type-l count
    std::size_t U = 0; // type-u count

    // Index of the last seen type-b slot in the logical order (-1 = none yet)
    // We will count the a-run after it as we scan forward.
    bool        inARunAfterB = false;
    std::size_t currentKIdx  = 0; // index into K[] we are currently filling

    for (std::size_t dist = 0; dist < winSize; ++dist)
    {
        ElementState st = GetElementState(dist);

        if (st == otherInflight)
        {
            // New type-b element found.
            // If we were counting a-run for a previous b, that run is now closed.
            // Start a new K entry (initially 0; will be incremented by a-slots that follow).
            K.push_back(0);
            currentKIdx  = K.size() - 1;
            inARunAfterB = true; // next a-slots go into K[currentKIdx]
        }
        else if (st == ElementState::ACKED)
        {
            if (inARunAfterB)
            {
                // Still in the a-run immediately after a b-slot
                ++K[currentKIdx];
            }
            // type-a slots are neither L nor U; nothing else to do
        }
        else
        {
            // type-l or type-u: breaks the a-run after the last b
            inARunAfterB = false;

            if (st == thisInflight)
            {
                ++L;
            }
            else if (st == ElementState::UNACKED)
            {
                ++U;
            }
        }
    }

    const std::size_t m = K.size(); // number of type-b elements

    // ------------------------------------------------------------------
    // Second pass: evaluate the summation.
    //
    //   sum_{i=1}^{m} [ (K_i + 1) * (1-p)^i ]  +  m*p  +  L  +  U
    //
    // We accumulate q^i iteratively to avoid repeated pow() calls.
    // ------------------------------------------------------------------
    double sum  = 0.0;
    double qPow = q; // (1-p)^i, starts at i=1

    for (std::size_t i = 0; i < m; ++i)
    {
        sum  += static_cast<double>(K[i] + 1) * qPow;
        qPow *= q;
    }

    sum += static_cast<double>(m) * otherPer;
    sum += static_cast<double>(L);
    sum += static_cast<double>(U);

    if(m_lastUpdateTime != Simulator::Now() && log) {
        Print(std::cout, linkId);
        std::cout << "=== ComputeEffectiveBawSize ===\n";
        std::cout << "  linkId=" << +linkId
                << "  otherPer(p)=" << otherPer
                << "  q=(1-p)=" << q
                << "  winSize=" << winSize << "\n";
        std::cout << "  Counts: m(type-b)=" << m
                << "  L(type-l)=" << L
                << "  U(type-u)=" << U << "\n";

        std::cout << "  Summation terms: sum_{i=1}^{m} [ (K_i+1) * (1-p)^i ]\n";
        {
            double qPowDbg   = q;
            double partialSum = 0.0;
            for (std::size_t i = 0; i < m; ++i)
            {
                double term = static_cast<double>(K[i] + 1) * qPowDbg;
                partialSum += term;
                std::cout << "    i=" << (i + 1)
                        << "  K_" << (i + 1) << "=" << K[i]
                        << "  (K_i+1)=" << (K[i] + 1)
                        << "  q^" << (i + 1) << "=" << qPowDbg
                        << "  term=" << term
                        << "  partial_sum=" << partialSum << "\n";
                qPowDbg *= q;
            }
            std::cout << "  sum term total=" << partialSum << "\n";
        }
        std::cout << "  m*p=" << static_cast<double>(m) * otherPer
                << "  L=" << L
                << "  U=" << U << "\n";
        std::cout << "  Final EffBAW = " << sum
                << " = (sum)" << (sum - static_cast<double>(m) * otherPer - static_cast<double>(L) - static_cast<double>(U))
                << " + (m*p)" << static_cast<double>(m) * otherPer
                << " + (L)" << L
                << " + (U)" << U << "\n";
        std::cout << "==============================\n";
    }

    
    m_lastUpdateTime = Simulator::Now(); // for debugging/logging

    return sum;
}

} // namespace ns3