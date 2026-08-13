/*
 * Copyright (c) 2019 Universita' degli Studi di Napoli Federico II
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Author: Stefano Avallone <stavallo@unina.it>
 */

#ifndef BLOCK_ACK_WINDOW_H
#define BLOCK_ACK_WINDOW_H
#include "ns3/assert.h"
#include "ns3/nstime.h"

#include <array>
#include <cstdint>
#include <ostream>
#include <vector>

namespace ns3
{

/**
 * \ingroup wifi
 * \brief Block ack window
 *
 * This class provides the basic functionalities of a window sliding over a
 * bitmap: accessing any element in the bitmap and moving the window forward
 * a given number of positions. This class can be used to implement both
 * an originator's window and a recipient's window.
 *
 * The window is implemented as a vector of bool and managed as a circular
 * queue. The window is moved forward by advancing the head of the queue and
 * clearing the elements that become part of the tail of the queue. Hence,
 * no element is required to be shifted when the window moves forward.
 *
 * Example:
 *
 * |0|1|1|0|1|1|1|0|1|1|1|1|1|1|1|0|
 *                      ^
 *                      |
 *                     HEAD
 *
 * After moving the window forward three positions:
 *
 * |0|1|1|0|1|1|1|0|1|1|0|0|0|1|1|0|
 *                            ^
 *                            |
 *                           HEAD
 *
 * Each element in the window also tracks one of five transmission states via
 * ElementState, maintained in a parallel circular vector m_statesWindow:
 *
 *   UNACKED      - element not yet acknowledged (default/reset state)
 *   ACKED        - MPDU has been positively acknowledged
 *   INFLIGHT_2G  - MPDU is in flight on the 2.4 GHz link (linkId = 0)
 *   INFLIGHT_5G  - MPDU is in flight on the 5 GHz link (linkId = 1)
 *   INFLIGHT_6G  - MPDU is in flight on the 6 GHz link (linkId = 2)
 */
class BlockAckWindow
{
  public:
    /**
     * \brief Per-element transmission state.
     *
     * Encodes whether the corresponding MPDU is unacknowledged, acknowledged,
     * or in flight on the 2.4, 5, or 6 GHz link (link IDs 0, 1, and 2,
     * respectively).
     */
    enum class ElementState : uint8_t
    {
        UNACKED = 0, ///< Not yet acknowledged (initial / reset state)
        ACKED = 1, ///< Positively acknowledged
        INFLIGHT_2G = 2, ///< In-flight on 2.4 GHz link (linkId = 0)
        INFLIGHT_5G = 3, ///< In-flight on 5 GHz link   (linkId = 1)
        INFLIGHT_6G = 4, ///< In-flight on 6 GHz link   (linkId = 2)
    };

    /**
     * Convert a linkId (0/1/2) to the corresponding INFLIGHT ElementState.
     * \param linkId  the link identifier (must be 0, 1, or 2)
     * \return        INFLIGHT_2G / INFLIGHT_5G / INFLIGHT_6G
     */
    static ElementState InflightStateForLink(uint8_t linkId)
    {
        static constexpr ElementState table[] = {
            ElementState::INFLIGHT_2G, // linkId 0: 2.4 GHz
            ElementState::INFLIGHT_5G, // linkId 1: 5 GHz
            ElementState::INFLIGHT_6G, // linkId 2: 6 GHz
        };
        NS_ASSERT_MSG(linkId < 3, "Invalid link ID " << +linkId);
        return table[linkId];
    }

    /**
     * Constructor
     */
    BlockAckWindow();
    /**
     * Initialize the window with the given starting sequence number and size
     *
     * \param winStart the window start
     * \param winSize the window size
     */
    void Init(uint16_t winStart, uint16_t winSize);
    /**
     * Reset the window by clearing all the elements and setting winStart to the
     * given value.
     *
     * \param winStart the window start
     */
    void Reset(uint16_t winStart);
    /**
     * Get the current winStart value.
     *
     * \return the current winStart value
     */
    uint16_t GetWinStart() const;
    /**
     * Get the current winEnd value.
     *
     * \return the current winEnd value
     */
    uint16_t GetWinEnd() const;
    /**
     * Get the window size.
     *
     * \return the window size
     */
    std::size_t GetWinSize() const;
    /**
     * Get a reference to the element in the window having the given distance from
     * the current winStart. Note that the given distance must be less than the
     * window size.
     *
     * \param distance the given distance
     * \return a reference to the element in the window having the given distance
     *         from the current winStart
     */
    std::vector<bool>::reference At(std::size_t distance);
    /**
     * Get a const reference to the element in the window having the given distance from
     * the current winStart. Note that the given distance must be less than the
     * window size.
     *
     * \param distance the given distance
     * \return a const reference to the element in the window having the given distance
     *         from the current winStart
     */
    std::vector<bool>::const_reference At(std::size_t distance) const;
    /**
     * Advance the current winStart by the given number of positions.
     *
     * \param count the number of positions the current winStart must be advanced by
     */
    void Advance(std::size_t count);

    /**
     * Set the ElementState of the element at the given distance from winStart.
     *
     * \param distance distance from winStart; must be less than the window size
     * \param state    the new ElementState to assign
     */
    void SetElementState(std::size_t distance, ElementState state);

    /**
     * Get the ElementState of the element at the given distance from winStart.
     *
     * \param distance distance from winStart; must be less than the window size
     * \return the ElementState of the requested element
     */
    ElementState GetElementState(std::size_t distance) const;


    /**
     * Compute the effective BAW (Block Ack Window) size seen by \p linkId,
     * taking into account that MPDUs in flight on the *other* link may or
     * may not be ACKed before this link needs to advance its window.
     *
     * Definitions (all measured from winStart, in logical distance order):
     *   - type-b : slots whose state is INFLIGHT on the OTHER link
     *   - type-l : slots whose state is INFLIGHT on THIS  link      (count = L)
     *   - type-u : slots whose state is UNACKED                     (count = U)
     *   - type-a : slots whose state is ACKED
     *   - m      : number of type-b slots before the first type-l or type-u slot
     *   - Dother : total number of type-b slots in the complete window
     *   - K_i    : number of consecutive type-a slots immediately AFTER
     *              the i-th prefix type-b slot
     *
     * Distributed-receiver specialization of Eq. (6):
     *   EffBAW = sum_{i=1}^{m} [ (K_i + 1) * (1-p)^i ]
     *             + Dother*p + L + U
     *
     * where p is the fixed PER of the other link. Because a local BA only
     * reports MPDUs received on that link, D1*p1 + D2*p2 reduces to Dother*p.
     *
     * \param linkId     this link's identifier (0 or 1)
     * \param otherPer   fixed PER of the other link (must be in [0.0, 1.0])
     * \return           the effective BAW size as a double
     */
    double ComputeEffectiveBawSize(uint8_t linkId, double otherPer, bool log);

        /**
     * Print the window contents to \p os (three columns).
     *
     * Columns: SeqNo | ACK Bitmap | State
     *
     * \param os  output stream (e.g. std::cout or an ostringstream)
     */
    void Print(std::ostream& os) const;

    /**
     * Print the window contents to \p os with an extra slot-type column.
     *
     * Columns: SeqNo | ACK Bitmap | State | Type
     *
     * Type is classified relative to \p linkId:
     *   a – ACKED
     *   u – UNACKED
     *   l – in-flight on THIS  link  (linkId)
     *   b – in-flight on the OTHER link (1 - linkId)
     *
     * \param os      output stream
     * \param linkId  this link's identifier (0 or 1)
     */
    void Print(std::ostream& os, uint8_t linkId) const;

  private:
    uint16_t m_winStart;                  ///< window start (sequence number)
    std::vector<bool> m_window;           ///< ACK bitmap (circular queue)
    std::vector<ElementState> m_statesWindow; ///< per-element transmission state
    std::size_t m_head;                   ///< physical index of winStart in both vectors
    std::array<Time, 2> m_lastEffectiveBawLogTimes{Time::Min(), Time::Min()};
};

} // namespace ns3

#endif /* BLOCK_ACK_WINDOW_H */