# Airtime-Aligned MPDU Allocation for IEEE 802.11be MLO

This repository accompanies our work on MPDU allocation for an IEEE 802.11be
multi-link device (MLD) operating in simultaneous transmit and receive (STR)
mode with a shared Block Acknowledgment window (BAW).

The repository provides the Python reference implementation of the analytical
model and allocation method, the fixed-point convergence and scalability
experiments, two-link and three-link ns-3 simulation sources, the supplementary
coefficient derivation, and the reported numerical outputs.

The released source code and numerical artifacts support inspection and
independent verification of the analytical model and implementation.

## Relationship to DAMLA

The nonzero-PER handling and effective-BAW calculation in the two-link ns-3
DAMLA path reproduce mechanisms introduced by Paroshin et al. in
[*Aggregation Algorithm to Increase Throughput of Multi-Link Wi-Fi 7
Devices*](https://doi.org/10.1109/LWC.2024.3474294), specifically the effective
BAW in Section III-C (Eq. (6)) and its use by DAMLA in Section III-D. These nonzero-PER and
effective-BAW mechanisms therefore originate in DAMLA and are reproduced here;
they are not contributions introduced by this repository.

The `per` input in `solve.py` has a different and more limited role. It applies
a simple `(1 - PER)` payload-delivery adjustment to this repository's
analytical allocation model. It does not model DAMLA's effective BAW, BAW
blocking, or retransmission state. This is a simplified numerical extension
for exploring nonzero-PER inputs outside the formal analytical model presented
in the accompanying paper.

## Repository contents

| Path | Description |
| --- | --- |
| `solve.py` | Analytical model, damped fixed-point solver, active-set allocation, largest-remainder integer allocation, optional exact integer enumeration, and a simplified nonzero-PER extension outside the paper's formal model |
| `fp_conv.py` | Multi-start convergence experiment for the coupled fixed-point equations |
| `scalability-experiment.py` | Runtime and allocation-validity experiment for 2 to 8 links, with optional exact integer enumeration for small link counts |
| `2link.json` | Analytical input scenarios corresponding to the two-link evaluation in Section IV-A |
| `3link.json` | Analytical input scenarios corresponding to the three-link evaluation in Section IV-B |
| `run_commands.py` | Converts `solve.py` CSV results into two-link or three-link ns-3 commands and supports batch execution |
| `docs/coefficient-derivation.pdf` | Supplementary derivation of the affine coefficients |
| `scratch/mlo-2link/link2.cc` | Two-link ns-3 simulation source |
| `scratch/mlo-3link/link3.cc` | Three-link ns-3 simulation source |
| `src/wifi/model/` | Modified ns-3.43 Wi-Fi model implementing the distributed multi-radio MLD architecture and A-MPDU control |
| `outputs/` | Reported numerical results produced by the Python scripts |

## Requirements

### Python analysis

- Python 3.10 or later
- No third-party Python packages

Run the Python scripts from the repository root.

### ns-3 simulations

The simulation sources were developed for **ns-3.43**. A working ns-3.43
environment and its normal build dependencies are required. Retain the
following paths under the ns-3 `scratch/` directory:

```text
scratch/mlo-2link/link2.cc
scratch/mlo-3link/link3.cc
```

After configuring and building ns-3, inspect the supported command-line
parameters with:

```bash
./ns3 run "link2 --PrintHelp"
./ns3 run "link3 --PrintHelp"
```

The C++ sources expose link configuration, contention settings, BAW size,
A-MPDU policy, simulation duration, random seed, and output scenario through
ns-3 command-line arguments.

## Changes to the ns-3.43 Wi-Fi source

The files under `src/wifi/model/` are not an unmodified ns-3.43 Wi-Fi
module. They implement the distributed multi-radio MLD architecture evaluated
by the two-link and three-link simulations. The changes retain ns-3's original
MPDU in-flight bookkeeping and shared MAC reordering behavior unless described
below.

### Independent controls

The distributed sender, distributed receiver, A-MPDU controller, and diagnostic
logs can be enabled independently:

| Simulation argument | ns-3 setting | Meaning |
| --- | --- | --- |
| `--distributedSender` | `Txop::Mode` bit 0 | Enable sender-side per-link BA read pointers |
| `--distributedReceiver` | `Txop::Mode` bit 1 | Enable receiver-side per-link BA scoreboards |
| `--logsender` | `Txop::Mode` bit 2 | Enable sender-side architecture and BAW logs |
| `--logreceiver` | `Txop::Mode` bit 3 | Enable receiver-side scoreboard logs |
| `--ampduLimit` | `Txop::EnableAmpduLimit` | Enable A-MPDU limiting independently of the distributed sender |

This separation permits comparisons with either side of the distributed
architecture disabled and prevents the A-MPDU policy from being implicitly
enabled by the sender mode.

### Distributed sender

The originator BA agreement maintains one read pointer for every link in
addition to the ordinary shared transmit window:

- Before transmission, the current link updates its local read pointer from
  the latest eligible per-link pointer. Sequence-number distances are evaluated
  relative to the shared transmit-window start, including the three-link case.
- MPDU selection uses the link-local read pointer when checking the BA-window
  boundary.
- After an ACK or Block Ack is received, the responding link advances its
  pointer to the shared transmit-window start and notifies other eligible
  links.
- A link that is already transmitting temporarily rejects cross-link pointer
  updates. The PHY-maintained `m_linkTxStatus` state re-enables synchronization
  when that transmission finishes.
- The standard ns-3 `WifiMpdu::SetInFlight()` and `ResetInFlight()` behavior
  is retained. The additional effective-BAW state is marked only in
  `HtFrameExchangeManager::SendPsdu()`, after RTS/CTS has succeeded and the
  data PSDU will actually be transmitted.

`WifiPhy::Send()` and the link-status-aware overload share one internal
`DoSend()` implementation. This avoids maintaining a duplicate copy of the
PHY transmission path solely for link-status notification.

### Distributed receiver

In distributed receiver mode, each affiliated STA/link maintains an independent
`BlockAckWindow` scoreboard:

- Receiving an MPDU updates only the scoreboard of the receiving link.
- A Block Ack transmitted on a link is generated from that link's local
  scoreboard, so it does not acknowledge MPDUs observed only on another link.
- The existing shared `m_bufferedMpdus` and `m_winStartB` reordering state is
  retained for in-order delivery to the upper MAC.
- The local scoreboard is updated before the global-old-packet check. This lets
  the receiving link report an MPDU in its BA even when the shared reordering
  state has already advanced past it.
- When a local scoreboard lags the shared receive state across the half
  sequence-number space, the current global window is used to identify the
  lagging local context and resynchronize it. Sequence-number window movement
  uses modulo-4096 arithmetic.
- BAR processing follows the same local-scoreboard selection and shared
  reordering-state protection. Distributed-mode flush delivers and clears the
  shared reorder buffer without deriving its boundary from one local
  scoreboard.

### A-MPDU policies and DAMLA

`AmpduLimitController` is created only when `EnableAmpduLimit` is enabled,
and its limit is applied only to QoS Data MPDUs. The implemented policies are:

| Policy | Two-link behavior | Three-link behavior |
| --- | --- | --- |
| 1, `greedy` | Both links unlimited | All links unlimited |
| 2, `damla` | Adaptive DAMLA limit | Not supported |
| 3, `only2G` | Link 0 only | Link 0 only |
| 4, `only5G` | Link 1 only | Link 1 only |
| 5, `only6G` | Not supported | Link 2 only |
| 6, `bothset` / `allset` | Fixed per-link limits | Fixed per-link limits |

For DAMLA, QoS Data PPDU timing is collected per link. The latest ten positive
inter-PPDU gaps are stored in a ring buffer with a running sum, making the mean
gap update and lookup constant-time. During the first 1.05 seconds, timing
observations are not yet considered reliable, so the controller bypasses the
timing prediction and returns `ceil(bawSize / 2)` for either link. These startup
decisions are recorded as `balanced_bootstrap`. Afterwards, the controller
predicts the next transmission opportunity from the other link's last PPDU end
and measured mean gap. Each decision also records whether it used the effective
BAW, the nominal BAW, or an expired prediction.

When all of the following hold, the nominal BAW is replaced by the expected
effective BAW used by the PER-aware part of DAMLA reproduced here:

- distributed sender is enabled;
- `EnableAmpduLimit` is enabled;
- policy 2 (DAMLA) is selected;
- at least one configured link PER is nonzero; and
- the device has exactly two links.

`BlockAckWindow` therefore maintains a parallel state window containing
unacknowledged, acknowledged, link-0-in-flight, and link-1-in-flight entries.
`ComputeEffectiveBawSize()` evaluates the distributed-receiver specialization
of the effective-BAW expression in DAMLA Eq. (6) using floating-point
arithmetic; rounding is deferred until the final MPDU limit is produced. The
implementation uses the other link's SIFS and records the source of every
adaptive decision.

### Architecture logs

The optional logs are structured around actual protocol events:

- `[MPDU_TX]`: emitted in `SendPsdu()` after successful protection, with
  link, TID, MPDU count, sequence-number ranges, and decision source;
- `[BA_TX]`: BA transmitted to the evaluated MLD, excluding unrelated SLD
  recipients;
- `[ACK_RX]` and `[BA_RX]`: transmit-window and per-link read-pointer
  updates;
- `[RPTR_BEFORE_TX]` and `[RPTR_NOTIFY]`: cross-link read-pointer
  synchronization;
- `[RX_SCOREBOARD_RESYNC]` and `[RX_GLOBAL_OLD_DROP]`: independent receiver
  scoreboard handling; and
- `[AMPDU_LIMIT]`, `[EFFECTIVE_BAW]`, and `[EFFECTIVE_BAW_PREFIX]`:
  A-MPDU decisions and the effective-BAW calculation.

`BlockAckWindow::Print()` is used at MPDU transmission and ACK/BA processing
points to show the BAW state associated with the event.

## Analytical implementation

Run the supplied two-link and three-link analytical scenarios:

```bash
python3 solve.py --scenario-file 2link.json
python3 solve.py --scenario-file 3link.json
```

The default outputs are `outputs/solve-2link.csv` and
`outputs/solve-3link.csv`, respectively. The default output name is formed as
`outputs/solve-<scenario-file-stem>.csv`.

Enable exact integer enumeration over the implemented active-link set with:

```bash
python3 solve.py --scenario-file 2link.json --exhaustive
python3 solve.py --scenario-file 3link.json --exhaustive
```

These commands write `outputs/solve-2link-exhaustive.csv` and
`outputs/solve-3link-exhaustive.csv`. The exhaustive comparison is conditional
on the active-link set selected by the proposed allocation method.

The value passed to `--output` must be relative to `outputs/`. Each JSON
scenario contains:

- `mcsValue`: per-link EHT MCS indices;
- `channelWidth`: per-link channel widths in MHz;
- `nss`: number of spatial streams, defaulting to 2 if omitted;
- `N`: numbers of coexisting single-link devices;
- `nmpdu_sld`: SLD A-MPDU sizes;
- `per`: optional per-link packet error rates for the simplified numerical
  extension described above, not the DAMLA effective-BAW model; and
- `BAW`: shared BAW size.

All list-valued fields must have the same number of entries. Their length
determines the number of MLD links.

Run the deterministic 2-to-8-link allocation experiment:

```bash
python3 solve.py --large-link-allocation
```

This writes `outputs/large-link-allocation.csv`.

## ns-3 command generation and batch execution

`run_commands.py` reads a two-link or three-link CSV produced by `solve.py`
and constructs the corresponding ns-3 commands. Inspect the generated
commands without running simulations with:

```bash
python3 run_commands.py outputs/solve-2link.csv --dry-run
python3 run_commands.py outputs/solve-3link.csv --dry-run
```

Omit `--dry-run` to execute the commands. The script uses the optimized
two-link or three-link executable under `build/scratch/` by default; use
`--executable` to override that path.

## Fixed-point convergence experiment

```bash
python3 fp_conv.py
```

The default experiment evaluates 5,184 parameter configurations from 25
initial points per configuration, for 129,600 solver runs. It writes:

```text
outputs/fixed-point-convergence.csv
outputs/fixed-point-summary.txt
```

Optional arguments are:

```bash
python3 fp_conv.py --max-iter 2000 --tol 1e-12 --output-dir outputs
```

This experiment provides numerical evidence over the declared finite
parameter domain; it is not a formal proof of global convergence for arbitrary
parameters.

## Scalability experiment

```bash
python3 scalability-experiment.py
```

By default, the script evaluates 1,000 deterministic heterogeneous trials for
each link-count and BAW-size pair and writes `outputs/scalability.csv`.
Optional arguments are:

```bash
python3 scalability-experiment.py --trials 1000 --output outputs/scalability.csv
```

The reported runtime is the single-threaded host-side runtime of the Python
reference implementation. It is platform-dependent and should not be
interpreted as the execution time of a commercial MLO chipset.

Enable exact integer enumeration with:

```bash
python3 scalability-experiment.py --exhaustive
```

In exhaustive mode, the default link counts are 2 and 3. The primary reproducibility output is the scalability-exhaustive CSV file; the script also generates a local LaTeX table that is excluded from version control.

## Reported outputs

| File | Contents |
| --- | --- |
| `fixed-point-convergence.csv` | Per-configuration multi-start convergence statistics |
| `fixed-point-summary.txt` | Aggregate convergence summary |
| `scalability.csv` | Runtime, solver effort, and allocation-validity results |
| `scalability-exhaustive.csv` | Largest-remainder versus exact-enumeration accuracy and runtime results |
| `large-link-allocation.csv` | Integer allocations for the deterministic 2-to-8-link cases |
| `solve-2link.csv` | Analytical results for the scenarios in `2link.json` |
| `solve-3link.csv` | Analytical results for the scenarios in `3link.json` |
| `solve-2link-exhaustive.csv` | Two-link analytical results with exact integer comparison columns |
| `solve-3link-exhaustive.csv` | Three-link analytical results with exact integer comparison columns |

These files document the reported analytical, convergence, and scalability
results.
