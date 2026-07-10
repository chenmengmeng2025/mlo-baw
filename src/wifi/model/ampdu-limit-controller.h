#ifndef AMPDU_LIMIT_CONTROLLER_H
#define AMPDU_LIMIT_CONTROLLER_H

#include "ns3/wifi-mpdu.h"
#include "ns3/wifi-psdu.h"
#include "ns3/wifi-ppdu.h"
#include "ns3/wifi-mac-queue.h"
#include "ns3/log.h"
#include "ns3/random-variable-stream.h"
#include "ns3/wifi-mac.h"
#include "ns3/udp-client-server-helper.h"

#include <array>
#include <deque>
#include <fstream>
#include <vector>
#include <nlohmann/json.hpp>

namespace ns3
{

/**
 * \ingroup wifi
 * \brief Controls the per-link A-MPDU aggregation limit for multi-link operation (MLO).
 *
 * AmpduLimitController tracks PPDU transmission timing on each link and, based on a
 * selectable policy (e.g. greedy, DAMLA, single-link-only, or user-defined fixed
 * limits), computes the maximum number of MPDUs that may be aggregated into an
 * A-MPDU on a given link.
 */
class AmpduLimitController : public Object
{
  public:
    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId();

    /**
     * Construct an AmpduLimitController bound to the given WifiMac. Per-link
     * state (A-MPDU limits, data rates, PPDU timing) is sized according to
     * the number of links exposed by \p mac.
     *
     * \param mac the WifiMac whose links this controller manages
     */
    AmpduLimitController(Ptr<WifiMac> mac);

    /**
     * Default constructor. Delegates to AmpduLimitController(nullptr); the
     * instance must be reconstructed with a valid WifiMac before use.
     */
    AmpduLimitController();

    ~AmpduLimitController() override;

    /**
     * Notify the controller that a QoS-data PPDU has been transmitted on a
     * link, so that the per-link PPDU transmission window and inter-PPDU gap
     * statistics can be updated. Non-QoS-data frames (RTS/CTS/ACK/BlockAck)
     * are ignored.
     *
     * \param ppdu the transmitted PPDU
     * \param duration the transmission duration of the PPDU
     * \param linkId the ID of the link the PPDU was transmitted on
     */
    void NotifyPpduTxDuration(Ptr<const WifiPpdu> ppdu, Time duration, uint8_t linkId);

    /**
     * Check whether the current simulation time falls within the most recent
     * PPDU transmission window of the "other" link (i.e. 1 - linkId),
     * extended by an additional margin. Only meaningful for 2-link MLO.
     *
     * \param linkId the current link ID
     * \param extraTime additional time (in microseconds) to extend the other
     *        link's window end by, e.g. SIFS + BlockAck duration
     * \return true if the current time lies within
     *         [otherLinkWindowStart, otherLinkWindowEnd + extraTime)
     */
    bool IsWithinOtherLinkPpduWindow(uint8_t linkId, double extraTime) const;

    /**
     * Compute the maximum number of MPDUs allowed in an A-MPDU on the given
     * link, according to the selected policy.
     *
     * \param linkId the link ID
     * \param policy the A-MPDU limit control policy to apply. Supported values:
     *        - 1 (greedy):  no limit on any link
     *        - 2 (damla):   dynamically computed limit (2-link MLO only)
     *        - 3 (only2G):  link 0 unlimited, all other links disabled
     *        - 4 (only5G):  link 1 unlimited, all other links disabled
     *        - 5 (only6G):  link 2 unlimited, all other links disabled (3-link only)
     *        - 6 (bothset/allset): use the fixed per-link limits set via SetAmpduLimit()
     * \param mpduBufferSize the (effective) block ack window size, used as
     *        the nominal A-MPDU limit
     * \param logFlag if true, print the resulting A-MPDU limit to stdout
     *        whenever the simulation time advances
     * \return the computed A-MPDU limit (number of MPDUs) for the given link
     */
    uint32_t GetAmpduLimit(uint8_t linkId, uint32_t policy, uint32_t mpduBufferSize, bool logFlag = false);

    /**
     * Set the fixed per-link A-MPDU limits used by the "bothset"/"allset" policy.
     *
     * \param limit0 the A-MPDU limit for link 0
     * \param limit1 the A-MPDU limit for link 1
     * \param limit2 the A-MPDU limit for link 2 (ignored if fewer than 3 links exist)
     */
    void SetAmpduLimit(int limit0, int limit1, int limit2);

    /**
     * Set the data rate (in Mbps) assumed for each link, used by the "damla" policy.
     *
     * \param datarate0 the data rate for link 0
     * \param datarate1 the data rate for link 1
     * \param datarate2 the data rate for link 2 (ignored if fewer than 3 links exist)
     */
    void SetDatarateSetting(uint32_t datarate0, uint32_t datarate1, uint32_t datarate2);

  private:
    Ptr<WifiMac> m_mac; //!< the WifiMac whose links this controller manages

    std::vector<int> m_ampduLimits; //!< per-link fixed A-MPDU limit, used by the "bothset"/"allset" policy
    std::vector<uint32_t> m_datarateSetting; //!< per-link assumed data rate (Mbps), used by the "damla" policy
    std::vector<std::vector<double>> m_interPpduGaps; //!< per-link sliding window (up to 10 samples) of inter-PPDU gaps, in microseconds
    Time m_lastUpdateTime; //!< simulation time at which GetAmpduLimit() last printed a log line
    std::vector<std::array<double, 2>> m_ppduTimeWindow; //!< per-link [start, end] time (microseconds) of the most recent PPDU transmission
};

} // namespace ns3

#endif /* AMPDU_LIMIT_CONTROLLER_H */