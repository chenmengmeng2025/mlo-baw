"""Numerical implementation of the paper's airtime-aligned MPDU allocation.

The script solves Problem P1 for an L-link STR MLD using a shared Block ACK
window under the independent-scoreboard mode.  It first solves the per-link
contention fixed points (paper Eqs. (9)-(10)), then writes the mean successful
A-MPDU delivery time as the affine model

    T_bar^(l,M) = sigma * (A^(l) + B^(l) M_A^(l,M)).

For the active links, P1 is minimized by equalizing these mean delivery times
subject to sum_l M_A^(l,M) = W_BA.  The continuous solution is

    T_star/sigma = (W_BA + sum_l A^(l)/B^(l)) / sum_l 1/B^(l),
    M_A^(l,M),*  = (T_star/sigma - A^(l)) / B^(l).

A link with a non-positive allocation is deactivated and the solution is
recomputed on the reduced active set.  Finally, allocations are rounded to
integers and corrected so that their sum remains exactly W_BA.  The resulting
analytical MLO performance and the paper's single-link upper-bound baseline
are written to CSV.

Notation follows paper.txt.  Times named ``tau`` or ``T_OH`` are measured in
slots unless explicitly stated otherwise; sigma and protocol durations are in
microseconds, PHY rates are in Mbit/s, and frame lengths are in bits.
"""
from pathlib import Path
from dataclasses import dataclass, field
from enum import Enum
from typing import List
import argparse
import csv
import json
import math
import time
from functools import lru_cache

filepath = Path(__file__)
OUTPUT_DIR = filepath.parent / "outputs"

# Existing CSV results are either extended or replaced.
class CsvMode(Enum):
    OVERWRITE = 0
    APPEND = 1

CSV_MODE = CsvMode.OVERWRITE

# ---------------------------------------------------------------------------
# Explicit operating points used for the two-link numerical evaluation.
# ---------------------------------------------------------------------------
@dataclass
class ScanConfig:
    """Complete two-link scenarios; no Cartesian-product expansion is used."""

    scenarios: List[dict] = field(default_factory=list)

    def __post_init__(self):
        link1_rates = [
            206.470592,
            229.411766,
            258.088236,
            275.294120,
            286.764706,
            309.705884,
            344.117648,
            412.941180,
            458.823532,
            516.176472,
        ]
        self.scenarios = [
            {
                "R": [rate, 1080.882354],
                "N": [4, 1],
                "nmpdu_sld": [24, 232],
                "per": [0.0, 0.0],
                "BAW": 256,
            }
            for rate in link1_rates
        ]

# ---------------------------------------------------------------------------
# IEEE 802.11be/model parameters corresponding to Table I of the paper.
# ---------------------------------------------------------------------------
@dataclass
class Config:
    """
    System parameters used in the analytical model.
    """

    L: int

    sigma: float = 9.0                     # Slot duration sigma (microseconds)
    L_subf: float = (1500 + 62 + 10) * 8.0 # L_P + L_MH + 80 bits
    L_P: float = 1500.0 * 8.0              # Payload length (bits)
    T_PH_D: float = 56.0                   # PHY preamble duration for data frames

    # Complete RTS/CTS transmission durations, including control PHY headers.
    T_RTS: List[float] = field(init=False)  # T_RTS = T_{PH,C} + L_R / R_B^{(l)}
    T_CTS: List[float] = field(init=False)  # T_CTS = T_{PH,C} + L_C / R_B^{(l)}

    T_SIFS: List[float] = field(init=False)
    T_DIFS: List[float] = field(init=False)

    # EDCA contention parameters
    K_mld: List[int] = field(init=False)
    K_sld: List[int] = field(init=False)
    W_mld: List[float] = field(init=False)
    W_sld: List[float] = field(init=False)

    def __post_init__(self):

        self.T_RTS = [24.0] * self.L
        self.T_CTS = [28.0] * self.L

        self.T_SIFS = [16.0] * self.L
        self.T_DIFS = [0.0] * self.L

        self.K_mld = [6] * self.L
        self.K_sld = [6] * self.L

        self.W_mld = [16.0] * self.L
        self.W_sld = [16.0] * self.L
# ---------------------------------------------------------------------------
# One row of analytical results for the proposed policy and SL-UB baseline.
# ---------------------------------------------------------------------------
@dataclass
class Result:

    L: int

    R: List[float] = field(default_factory=list)
    N: List[int] = field(default_factory=list)

    BAW: int = 0

    nmpdu_sld: List[int] = field(default_factory=list)

    # Optimal MPDU allocation obtained by airtime alignment M_A^{(l,M),*}
    best_nmpdu: List[int] = field(default_factory=list)

    # Total MLO throughput
    D_mlo: float = 0.0

    # Per-link throughput
    D: List[float] = field(default_factory=list)

    # Average successful transmission period T̄^(l,M)
    T: List[float] = field(default_factory=list)

    # Variance of T̄^(l,M), measuring the degree of airtime alignment
    variance: float = 0.0

    # MLD conditional success probability p_A^(l,M), Eq. (9)
    p: List[float] = field(default_factory=list)

    # Steady-state idle probability α^(l)
    alpha: List[float] = field(default_factory=list)

    # Successful A-MPDU rate lambda_out^(l,M), in transmissions per slot
    lambda_out: List[float] = field(default_factory=list)

    # Single-Link Upper Bound (SL-UB)
    D_slo: float = 0.0
    lambda_slo: float = 0.0

    per: List[float] = field(default_factory=list)

    # Runtime/complexity measurements. CSV I/O and progress printing are not
    # included in these timers.
    fp_iterations: List[int] = field(default_factory=list)
    fp_converged: List[bool] = field(default_factory=list)
    fp_wall_us: List[float] = field(default_factory=list)
    fp_cpu_us: List[float] = field(default_factory=list)
    active_set_passes: int = 0
    allocation_wall_us: float = 0.0
    optimization_wall_us: float = 0.0
    point_wall_us: float = 0.0
    point_cpu_us: float = 0.0
    full_algorithm_wall_us: float = 0.0
    full_algorithm_cpu_us: float = 0.0
    empirical_work_units: int = 0


# ============================================================================
# Steady-state probability alpha^(l) that link l is idle (paper Eq. (7)).
# ============================================================================
def compute_alpha(
    pM: float,
    pS: float,
    tau_F: float,
    tau_T_M: float,
    tau_T_S: float,
    N: int,
) -> float:
    """Evaluate paper Eq. (7) for a link with ``N > 0`` SLDs."""

    pM_pow = pM ** (1.0 / N)

    denom = (
        1.0
        + tau_F
        + (tau_T_M - tau_F) * pM
        + N * (tau_T_S - tau_F) * pS
        - (tau_T_M + N * (tau_T_S - tau_F)) * pS * pM_pow
    )

    return 1.0 / denom

# ============================================================================
# Attempt-probability term shared by the fixed-point Eqs. (9)-(10).
# ============================================================================
def calc_p_part(
    p: float,
    W: float,
    K: int,
) -> float:
    """Return 4(2p-1) / {W[2p-(2-2p)^(K+1)]}."""

    if abs(p - 0.5) < 1e-12:
        return 4.0 / (W * (K + 2.0))

    numerator = 4.0 * (2.0 * p - 1.0)

    denominator = (
        W *
        (
            2.0 * p
            - (2.0 - 2.0 * p) ** (K + 1)
        )
    )

    if abs(denominator) < 1e-15:
        return 0.0

    return numerator / denominator

# ============================================================================
# Solve the per-link contention fixed points in paper Eqs. (9)-(10).
# ============================================================================
def solve_p(
    N: int,
    K_mld: int,
    K_sld: int,
    W_mld: float,
    W_sld: float,
    max_iter: int = 2000,
    tol: float = 1e-12,
    damping: float = 0.4,
    initial_pM: float = 0.9,
    initial_pS: float = 0.9,
):
    """Return ``(pM, pS, iterations, converged)`` for one link.

    Damped successive substitution is a numerical implementation detail; the
    paper establishes a unique nonzero fixed point for each contended link.
    """

    # No competing SLD exists. The MLD always succeeds in transmission.
    if N <= 0:
        return 1.0, 0.0, 0, True

    eps = 1e-14

    # Optional initial values are used by the multi-start convergence
    # experiment. Defaults preserve the original production behavior.
    pM = min(max(initial_pM, eps), 1.0 - eps)
    pS = min(max(initial_pS, eps), 1.0 - eps)

    # Damping stabilizes successive substitution near the fixed point.
    for iteration in range(1, max_iter + 1):

        # Backoff-related terms
        pS_part = calc_p_part(pS, W_sld, K_sld)
        pM_part = calc_p_part(pM, W_mld, K_mld)

        # Update pM
        pM_new_raw = max(0.0, 1.0 - pS_part) ** N
        pM_new = (1.0 - damping) * pM + damping * pM_new_raw
        pM_new = min(max(pM_new, eps), 1.0 - eps)

        # Update pS
        pS_candidate = (
            (1.0 - pM_part)
            * (max(0.0, 1.0 - pS_part) ** (N - 1))
        )
        pS_new = (1.0 - damping) * pS + damping * pS_candidate
        pS_new = min(max(pS_new, eps), 1.0 - eps)

        # Convergence check
        if (
            abs(pM_new - pM) < tol
            and
            abs(pS_new - pS) < tol
        ):
            return pM_new, pS_new, iteration, True

        pM = pM_new
        pS = pS_new

    # Preserve the original behavior: return the latest iterate if the strict
    # tolerance has not been met within max_iter iterations.
    return pM, pS, max_iter, False

# ============================================================================
# Compute the successful A-MPDU rate lambda_out^(l,M), Eqs. (6) and (8).
# ============================================================================
def compute_lambda(
    n: int,
    pM: float,
    pS: float,
    tau_T,
    tau_F: float,
    W_mld: float,
):
    """Return ``(lambdaM, alpha)`` for one link.

    ``lambdaM`` is the mean number of successful MLD A-MPDUs per slot, not a
    payload bit rate.  ``alpha`` is only needed in the coexistence model.
    """

    # No competing SLD exists. According to Eq. (6), every transmission
    # cycle consists of one average backoff period followed by one
    # successful A-MPDU transmission.
    if n == 0:
        lambdaM = 1.0 / (((W_mld + 1.0) / 2.0) + tau_T[0])
        return lambdaM, 1.0

    # Link-idle probability from Eq. (7).
    alpha = compute_alpha(
        pM,
        pS,
        tau_F,
        tau_T[0],
        tau_T[1],
        n,
    )

    # MLD successful A-MPDU rate from Eq. (8), written in factored form.
    lambdaM = (
        1.0
        - pS * (pM ** (1.0 / n - 1.0))
    ) * alpha * pM

    return lambdaM, alpha

# ============================================================================
# Convert a successful A-MPDU rate into payload throughput.
# ============================================================================
def get_throughput(
    PL: float,
    sigma: float,
    lambda_out: float,
    nmpdu: float,
    per: float = 0.0,
):
    """Return payload throughput in Mbit/s.

    ``PL`` is the payload bits in one A-MPDU (``L_P * M_A``).  Because slots
    are in microseconds, bits/microsecond is numerically Mbit/s.  The PER
    factor is an optional numerical extension; the paper's main derivation
    assumes successful MPDUs once an A-MPDU transmission succeeds.
    """

    # No MPDU allocated
    if nmpdu < 1e-9:
        return 0.0

    # lambda_out [1/slot] * PL [bit] / sigma [microsecond/slot].
    tp = lambda_out * (1.0 - per) * PL / sigma

    if tp < 0:
        raise RuntimeError(f"Invalid throughput: {tp}")

    return tp


# ============================================================================
# Successful-transmission holding time tau_T^(l,g), paper Eq. (3), in slots.
# ============================================================================
def calc_tau_T(
    nmpdu: float,
    L_subf: float,
    rate: float,
    sigma: float,
    T_OH: float,
):
    """Return the data airtime plus protocol overhead, normalized by sigma."""

    if nmpdu < 1e-9:
        return 0.0

    return ((nmpdu * L_subf) / rate) / sigma + T_OH

# ============================================================================
# Complete compressed-BA duration.  Its size changes with W_BA (Table I).
# ============================================================================
def get_T_BA(BAW: int) -> float:
    if BAW <= 256:
        return 40.0
    elif BAW <= 512:
        return 52.0
    else:
        return 72.0

# ============================================================================
# Protocol overhead T_OH^(l), paper Eq. (5), normalized by sigma.
# ============================================================================
@lru_cache(maxsize=None)
def calc_T_OH(
    BAW: int,
    T_SIFS: float,
    T_DIFS: float,
    T_RTS: float,
    T_CTS: float,
    T_PH_D: float,
    sigma: float,
) -> float:
    """Return RTS/CTS/DATA-header/BA/IFS overhead in time slots."""

    T_BA = get_T_BA(BAW)

    return (
        T_SIFS * 3
        + T_BA
        + T_DIFS
        + T_RTS
        + T_CTS
        + T_PH_D
    ) / sigma

# ============================================================================
# P1 objective: variance of mean delivery times over the active links.
# ============================================================================
def calc_variance(T):
    """Return ``mean_l (T_l - mean_j T_j)^2``."""

    if len(T) == 0:
        return 0.0

    mean = sum(T) / len(T)

    var = 0.0

    for t in T:
        var += (t - mean) ** 2

    return var / len(T)

# Analytical metrics for one active MLD link.
@dataclass
class LinkResult:

    # Steady-state successful transmission rate λ_out^(l,M)
    lambdaM: float = 0.0

    # Effective throughput D^(l,M)
    D: float = 0.0

    # Average successful transmission period T̄^(l,M)
    T: float = 0.0

    # Steady-state idle probability α^(l)
    alpha: float = 0.0

# ============================================================================
# Evaluate one link after its MLD aggregation size has been selected.
# ============================================================================
def calc_link(
    link_idx: int,
    n: int,
    nmpdu_sld_l: int,
    pM: float,
    pS: float,
    N: int,
    BAW: int,
    cfg: Config,
    tau_F_vec,
    R,
    per: float = 0.0,
):
    """Return lambda_out, data rate, mean delivery time, and idle probability."""

    T_OH = calc_T_OH(
        BAW,
        cfg.T_SIFS[link_idx],
        cfg.T_DIFS[link_idx],
        cfg.T_RTS[link_idx],
        cfg.T_CTS[link_idx],
        cfg.T_PH_D,
        cfg.sigma,
    )

    # tau_T^(l,M) and tau_T^(l,S), both in slots (paper Eq. (3)).
    tau_T = [
        calc_tau_T(
            n,
            cfg.L_subf,
            R[link_idx],
            cfg.sigma,
            T_OH,
        ),
        calc_tau_T(
            nmpdu_sld_l,
            cfg.L_subf,
            R[link_idx],
            cfg.sigma,
            T_OH,
        ),
    ]

    # Eqs. (6)-(8): successful MLD A-MPDU rate and link-idle probability.
    lambdaM, alpha = compute_lambda(
        N,
        pM,
        pS,
        tau_T,
        tau_F_vec[link_idx],
        cfg.W_mld[link_idx],
    )

    # Payload data rate and T_bar^(l,M).  The (1-PER) factor extends Eq. (2)
    # to count only error-free payload delivery in the numerical scan.
    D = get_throughput(
        cfg.L_P * n,
        cfg.sigma,
        lambdaM,
        n,
        per,
    )

    T = cfg.sigma / (lambdaM * (1.0 - per))

    return LinkResult(
        lambdaM=lambdaM,
        D=D,
        T=T,
        alpha=alpha,
    )

# Coefficients of T_bar^(l,M)/sigma = A^(l) + B^(l) M_A^(l,M).
@dataclass
class LinearCoef:
    """Link-specific intercept ``A`` and aggregation slope ``B``."""

    A: float
    B: float

# ---------------------------------------------------------------------------
# Affine reduction of the mean successful-delivery time
# ---------------------------------------------------------------------------
# This derivation is retained here because it is omitted from the paper body
# for space. Combining the holding-time, throughput, and fixed-point equations
# gives
#
#   T_bar^(l,M) = sigma [A^(l) + B^(l) M_A^(l,M)],
#
#   B^(l) = (L_P + L_MH + 80) / (R^(l) sigma),
#
# and, for n^(l,S) > 0,
#
#            1 + tau_F^(l)(1-pM)
#              + n^(l,S)[tau_T^(l,S)-tau_F^(l)] pS
#                [1-(pM)^(1/n^(l,S))]
#   A^(l) = ---------------------------------------------------- + T_OH^(l).
#                 pM - pS (pM)^(1/n^(l,S))
#
# Here pM=p_A^(l,M) and pS=p_A^(l,S). A^(l) collects every component
# independent of the MLD aggregation size: contention, collisions, SLD
# occupancy, and protocol overhead. B^(l) is the incremental airtime of one
# additional MLD MPDU. If n^(l,S)=0, the link is contention-free and
#
#   A^(l) = (W^(l,M)+1)/2 + T_OH^(l).
#
# Therefore P1 becomes a linear-system/active-set problem: equal mean delivery
# times determine the continuous allocation on all links that remain active.
# ---------------------------------------------------------------------------
def calc_AB(
    link_idx: int,
    nmpdu_sld_l: int,
    pM: float,
    pS: float,
    N: int,
    BAW: int,
    cfg: Config,
    tau_F_vec,
    T_DIFS,
    R,
):
    """Return ``(A^(l), B^(l))`` for the affine delivery-time model.

    ``A`` is measured in slots and ``B`` in slots per MPDU. Multiplication by
    ``sigma`` converts ``A + B*M_A`` to a physical time.
    """

    T_OH = calc_T_OH(
        BAW,
        cfg.T_SIFS[link_idx],
        T_DIFS[link_idx],
        cfg.T_RTS[link_idx],
        cfg.T_CTS[link_idx],
        cfg.T_PH_D,
        cfg.sigma,
    )

    B_l = cfg.L_subf / (
        R[link_idx] * cfg.sigma
    )

    # With no SLD, Eq. (6) leaves only mean backoff and fixed overhead in A.
    if N <= 0:
        A_l = (
            (cfg.W_mld[link_idx] + 1.0) / 2.0
            + T_OH
        )

        return LinearCoef(A=A_l, B=B_l)

    # With SLDs, A also captures collision time and SLD channel occupancy.

    tau_T_S = calc_tau_T(
        nmpdu_sld_l,
        cfg.L_subf,
        R[link_idx],
        cfg.sigma,
        T_OH,
    )

    tau_F = tau_F_vec[link_idx]

    pM_pow = pM ** (1.0 / N)

    delta = pM - pS * pM_pow

    if delta <= 1e-15:
        # Guard the denominator of A; an infinite intercept ensures that this
        # unusable link is removed by the active-set solver.
        return LinearCoef(
            A=float("inf"),
            B=B_l,
        )

    numerator = (
        1.0
        + tau_F * (1.0 - pM)
        + N
        * (tau_T_S - tau_F)
        * pS
        * (1.0 - pM_pow)
    )

    A_l = numerator / delta + T_OH

    return LinearCoef(
        A=A_l,
        B=B_l,
    )

# ---------------------------------------------------------------------------
# Closed-form active-set solution of Problem P1
# ---------------------------------------------------------------------------
# Inputs are L, W_BA, and the per-link coefficients A^(l), B^(l). These
# coefficients already incorporate R^(l), n^(l,S), M_A^(l,S), W^(l,g),
# K^(l,g), and the fixed-point probabilities returned by solve_p().
#
# Algorithm omitted from the paper body:
#
#   1. Initialize the active-link set A={0,...,L-1}.
#   2. On the current set, impose airtime alignment
#
#          A^(l) + B^(l) M_A^(l,M) = T_star/sigma,  l in A,
#
#      and sum_{l in A} M_A^(l,M)=W_BA. Solving this system gives
#
#          T_star/sigma =
#              [W_BA + sum_{l in A} A^(l)/B^(l)]
#              / sum_{l in A} 1/B^(l),
#
#          M_A^(l,M) = [T_star/sigma - A^(l)] / B^(l).
#
#   3. Remove links with non-positive allocations and repeat Step 2. Removed
#      links receive zero MPDUs, allowing an adaptive fallback to a useful
#      subset of links (and ultimately SLO under extreme heterogeneity).
#   4. Round positive allocations to integer MPDU counts. Assign the residual
#      W_BA-sum_l M_A^(l,M) to l*=argmin_{l in A} B^(l), which has the
#      smallest airtime increase per additional MPDU.
#
# Fixed-point solution costs O(L*I), where I is the number of iterations in
# solve_p(). Each active-set pass costs O(L), and every nonterminal pass
# removes at least one link; allocation therefore costs O(L^2) in the worst
# case. The complete method has complexity O(L*I + L^2).
# ---------------------------------------------------------------------------
def solve_closed_form(coef, BAW):
    """Return integer ``M_A^(l,M),*`` values summing to ``BAW``.

    The affine times are equalized over the current active set.  Non-positive
    continuous allocations deactivate their links, after which the reduced
    problem is solved again. This implementation removes ``real_alloc <= 0``
    because such a continuous value rounds to zero MPDUs; this is the integer
    counterpart of the non-positive test in the continuous algorithm.
    """

    L = len(coef)

    active = [True] * L
    n_alloc = [0] * L
    active_set_passes = 0

    while True:

        active_set_passes += 1

        sum_inv_B = 0.0
        sum_A_over_B = 0.0
        num_active = 0

        # Terms needed for the common aligned airtime T_star/sigma.
        for l in range(L):

            if not active[l]:
                continue

            sum_inv_B += 1.0 / coef[l].B
            sum_A_over_B += coef[l].A / coef[l].B
            num_active += 1

        if num_active == 0:
            break

        # T_star/sigma from the equality constraint sum_l M_l = W_BA.
        T_star_over_sigma = (
            BAW + sum_A_over_B
        ) / sum_inv_B

        any_negative = False

        real_alloc = [0.0] * L

        # Continuous allocation before integer rounding.
        for l in range(L):

            if not active[l]:
                continue

            real_alloc[l] = (
                T_star_over_sigma - coef[l].A
            ) / coef[l].B

            # Links receiving a non-positive allocation are removed from
            # the active set, and the closed-form solution is recomputed.
            if real_alloc[l] <= 0.0:
                active[l] = False
                any_negative = True

        # Re-solve P1 on the reduced active set.
        if any_negative:
            continue

        # Every active allocation is positive; map the solution to MPDU counts.
        total = 0
        for l in range(L):
            if active[l]:
                n_alloc[l] = round(real_alloc[l])
                if n_alloc[l] < 1:
                    n_alloc[l] = 1
                total += n_alloc[l]
            else:
                n_alloc[l] = 0

        # Restore the exact shared-window constraint after rounding.  Following
        # Algorithm 1, assign the residual to the link with minimum B^(l).
        diff = BAW - total
        if diff != 0:
            adjust_idx = -1
            best_B = float("inf")

            for l in range(L):

                if active[l] and coef[l].B < best_B:

                    best_B = coef[l].B
                    adjust_idx = l

            if adjust_idx >= 0:
                n_alloc[adjust_idx] += diff
                if n_alloc[adjust_idx] < 1:
                    n_alloc[adjust_idx] = 1

        break

    return n_alloc, active_set_passes

# ============================================================================
# Run the proposed allocation and the SL-UB baseline at one operating point.
# ============================================================================
def process_BAW(
    L: int,
    BAW: int,
    cfg: Config,
    N_vec,
    nmpdu_sld,
    p_vec,
    tau_F,
    R,
    per_vec,
    fp_iterations,
    fp_converged,
    fp_wall_us,
    fp_cpu_us,
):
    """Evaluate one combination of BAW, PHY rates, SLD loads, and PERs."""

    point_wall_start = time.perf_counter_ns()
    point_cpu_start = time.process_time_ns()

    # Per-link solutions of the contention fixed points (9)-(10).
    pM = [x[0] for x in p_vec]
    pS = [x[1] for x in p_vec]

    # Build the affine airtime model and solve P1.
    optimization_start = time.perf_counter_ns()

    coef = []

    for l in range(L):

        coef.append(
            calc_AB(
                l,
                nmpdu_sld[l],
                pM[l],
                pS[l],
                N_vec[l],
                BAW,
                cfg,
                tau_F,
                cfg.T_DIFS,
                R,
            )
        )

    allocation_start = time.perf_counter_ns()
    best_n, active_set_passes = solve_closed_form(coef, BAW)
    allocation_wall_us = (time.perf_counter_ns() - allocation_start) / 1000.0
    optimization_wall_us = (
        time.perf_counter_ns() - optimization_start
    ) / 1000.0

    # Evaluate the chosen integer allocation with the full link equations.
    best_D = [0.0] * L
    best_T = [0.0] * L
    best_alpha = [0.0] * L
    best_lambda = [0.0] * L

    active_T = []

    for l in range(L):

        if best_n[l] <= 0:
            continue

        res = calc_link(
            l,
            best_n[l],
            nmpdu_sld[l],
            pM[l],
            pS[l],
            N_vec[l],
            BAW,
            cfg,
            tau_F,
            R,
            per_vec[l],
        )

        best_D[l] = res.D
        best_T[l] = res.T
        best_alpha[l] = res.alpha
        best_lambda[l] = res.lambdaM

        active_T.append(res.T)

    min_var = calc_variance(active_T)

    # SL-UB: place the entire BAW on each link in turn, then retain the link
    # with the highest analytical payload throughput (paper Section IV).
    D_slo_vec = [0.0] * L
    lambda_slo_vec = [0.0] * L

    for l in range(L):

        try:

            res = calc_link(
                l,
                BAW,
                nmpdu_sld[l],
                pM[l],
                pS[l],
                N_vec[l],
                BAW,
                cfg,
                tau_F,
                R,
                per_vec[l],
            )

            D_slo_vec[l] = res.D
            lambda_slo_vec[l] = res.lambdaM

        except Exception:

            D_slo_vec[l] = 0.0
            lambda_slo_vec[l] = 0.0

    # Select the best-performing SLO link for comparison.
    best_slo_idx = max(
        range(L),
        key=lambda i: D_slo_vec[i]
    )

    # Assemble one CSV result row.
    res = Result(L)

    res.R = list(R)
    res.N = list(N_vec)

    res.BAW = BAW

    res.nmpdu_sld = list(nmpdu_sld)

    res.best_nmpdu = list(best_n)

    res.D = list(best_D)

    res.T = list(best_T)

    res.variance = min_var

    res.p = list(pM)

    res.alpha = list(best_alpha)

    res.lambda_out = list(best_lambda)

    # Aggregate MLO throughput
    res.D_mlo = sum(best_D)

    # Best SLO throughput
    res.D_slo = D_slo_vec[best_slo_idx]

    res.lambda_slo = lambda_slo_vec[best_slo_idx]

    res.per = list(per_vec)

    # End timing before returning to the scan loop, where printing and CSV I/O
    # occur. ``point_*`` measures the per-operating-point calculation with
    # precomputed fixed points. ``full_algorithm_*`` adds fixed-point solving
    # and represents a standalone solution of this operating point.
    res.fp_iterations = list(fp_iterations)
    res.fp_converged = list(fp_converged)
    res.fp_wall_us = list(fp_wall_us)
    res.fp_cpu_us = list(fp_cpu_us)
    res.active_set_passes = active_set_passes
    res.allocation_wall_us = allocation_wall_us
    res.optimization_wall_us = optimization_wall_us
    res.point_wall_us = (time.perf_counter_ns() - point_wall_start) / 1000.0
    res.point_cpu_us = (time.process_time_ns() - point_cpu_start) / 1000.0
    res.full_algorithm_wall_us = sum(fp_wall_us) + res.point_wall_us
    res.full_algorithm_cpu_us = sum(fp_cpu_us) + res.point_cpu_us

    # A machine-independent work indicator derived from the actual control
    # flow. It is not a count of CPU instructions: sum(I_l) represents the
    # fixed-point iterations and L*passes the worst-case link visits of the
    # active-set stage. It makes runs with different L/I directly comparable.
    res.empirical_work_units = sum(fp_iterations) + L * active_set_passes

    return res

# ============================================================================
# CSV serialization helpers.
# ============================================================================

def make_csv_header(L: int):
    """
    Generate CSV header.
    """

    header = []

    for l in range(L):
        header.append(f"R{l}")

    for l in range(L):
        header.append(f"nsld{l}")

    for l in range(L):
        header.append(f"ampdunumsld{l}")

    for l in range(L):
        header.append(f"fixedPER{l}")

    header.append("bawsize")

    for l in range(L):
        header.append(f"maxampdunum{l}")

    for l in range(L):
        header.append(f"D{l}")

    header.append("D_total")

    for l in range(L):
        header.append(f"T{l}")

    header.append("Variance")

    for l in range(L):
        header.append(f"p{l}")

    for l in range(L):
        header.append(f"alpha{l}")

    for l in range(L):
        header.append(f"lambda{l}")

    header.append("D_slo")
    header.append("lambda_slo")


    return header

# ============================================================================
def open_csv(path, L, mode=CSV_MODE):
    """
    Open csv file.

    Returns
    -------
    file object
    csv.writer
    """

    expected = make_csv_header(L)

    path = Path(path)

    # Refuse to append incompatible rows to an existing result file.
    if mode == CsvMode.APPEND and path.exists():

        with open(path, "r", newline="") as fin:

            reader = csv.reader(fin)

            try:
                first_line = next(reader)
            except StopIteration:
                first_line = []

        first_line = [x.strip() for x in first_line]
        expected_norm = [x.strip() for x in expected]

        if first_line != expected_norm:

            raise RuntimeError(
                "CSV header mismatch.\n"
                f"File header     : {first_line}\n"
                f"Expected header : {expected_norm}"
            )

        fout = open(path, "a", newline="")

        writer = csv.writer(fout)

        return fout, writer

    # New file, or explicit replacement of an existing file.
    fout = open(path, "w", newline="")

    writer = csv.writer(fout)

    writer.writerow(expected)

    return fout, writer

# ============================================================================
def _result_to_row(r: Result) -> list:
    """
    Convert one Result into a flat CSV row.
    """

    row = []

    row.extend(r.R)
    row.extend(r.N)
    row.extend(r.nmpdu_sld)
    row.extend(r.per)
    row.append(r.BAW)
    row.extend(r.best_nmpdu)
    row.extend(r.D)
    row.append(r.D_mlo)
    row.extend(r.T)
    row.append(r.variance)
    row.extend(r.p)
    row.extend(r.alpha)
    row.extend(r.lambda_out)
    row.append(r.D_slo)
    row.append(r.lambda_slo)

    return row


def write_csv_row(writer, r: Result):
    """
    Write one Result into csv.
    """

    writer.writerow(_result_to_row(r))


def write_csv_rows(writer, results: List[Result]):
    """
    Write a batch of Results.
    """

    writer.writerows(_result_to_row(r) for r in results)


def run_large_link_allocation_experiment():
    """Evaluate deterministic allocations for every L from 2 through 8."""
    rate_vector = [100.0, 200.0, 300.0, 400.0,
                   500.0, 600.0, 800.0, 1000.0]
    BAW = 1024
    output_path = OUTPUT_DIR / "large-link-allocation.csv"
    output_path.parent.mkdir(parents=True, exist_ok=True)
    rows = []

    for L in range(2, len(rate_vector) + 1):

        cfg = Config(L)
        for l in range(L):
            cfg.T_DIFS[l] = cfg.T_SIFS[l] + 2 * cfg.sigma

        R = rate_vector[:L]
        tau_F = [
            (cfg.T_RTS[l] + cfg.T_DIFS[l]) / cfg.sigma
            for l in range(L)
        ]
        per_vec = [0.0] * L
        allocations = {}

        # Scenario 1: interference-free. Scenario 2: four SLDs per link,
        # each using an A-MPDU aggregation size of 64.
        for scenario, N_value, sld_mpdu_value in (
            ("no_sld", 0, 0),
            ("with_sld", 4, 64),
        ):
            N_vec = [N_value] * L
            nmpdu_sld = [sld_mpdu_value] * L
            p_vec, fp_wall_us, fp_cpu_us = [], [], []

            for l in range(L):
                wall_start = time.perf_counter_ns()
                cpu_start = time.process_time_ns()
                fp = solve_p(
                    N_vec[l], cfg.K_mld[l], cfg.K_sld[l],
                    cfg.W_mld[l], cfg.W_sld[l],
                )
                fp_wall_us.append(
                    (time.perf_counter_ns() - wall_start) / 1000.0
                )
                fp_cpu_us.append(
                    (time.process_time_ns() - cpu_start) / 1000.0
                )
                p_vec.append(fp)

            result = process_BAW(
                L=L, BAW=BAW, cfg=cfg, N_vec=N_vec,
                nmpdu_sld=nmpdu_sld, p_vec=p_vec, tau_F=tau_F,
                R=R, per_vec=per_vec,
                fp_iterations=[x[2] for x in p_vec],
                fp_converged=[x[3] for x in p_vec],
                fp_wall_us=fp_wall_us, fp_cpu_us=fp_cpu_us,
            )
            if sum(result.best_nmpdu) != BAW:
                raise RuntimeError(
                    f"BAW constraint violated for L={L}, {scenario}"
                )
            allocations[scenario] = result.best_nmpdu

        rows.append({
            "L": L,
            "BAW": BAW,
            "R_Mbps": "{" + ",".join(f"{r:g}" for r in R) + "}",
            "W_mld": 16, "W_sld": 16, "K_mld": 6, "K_sld": 6,
            "no_sld_N": 0, "no_sld_M_A_S": 0,
            "no_sld_M_A_M_star": (
                "{" + ",".join(map(str, allocations["no_sld"])) + "}"
            ),
            "with_sld_N": 4, "with_sld_M_A_S": 64,
            "with_sld_M_A_M_star": (
                "{" + ",".join(map(str, allocations["with_sld"])) + "}"
            ),
        })
        print(
            f"L={L}: no SLD={allocations['no_sld']}; "
            f"with SLDs={allocations['with_sld']}"
        )

    with output_path.open("w", newline="") as fout:
        writer = csv.DictWriter(fout, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)

    print("Large-link allocation table saved to")
    print(output_path)
    return output_path



def load_custom_scenarios(path: Path):
    """Load and validate user-defined operating points from a JSON file."""
    with path.open(encoding="utf-8") as input_file:
        document = json.load(input_file)

    scenarios = (
        document.get("scenarios")
        if isinstance(document, dict)
        else document
    )
    if not isinstance(scenarios, list) or not scenarios:
        raise ValueError(
            "the scenario file must contain a non-empty 'scenarios' list"
        )

    required = ("R", "N", "nmpdu_sld", "per", "BAW")
    normalized = []
    number_of_links = None

    for index, scenario in enumerate(scenarios, start=1):
        if not isinstance(scenario, dict):
            raise ValueError(f"scenario {index} must be a JSON object")
        missing = [key for key in required if key not in scenario]
        if missing:
            raise ValueError(
                f"scenario {index} is missing: {', '.join(missing)}"
            )

        lengths = {
            key: len(scenario[key])
            for key in ("R", "N", "nmpdu_sld", "per")
            if isinstance(scenario[key], list)
        }
        if len(lengths) != 4 or len(set(lengths.values())) != 1:
            raise ValueError(
                f"scenario {index}: R, N, nmpdu_sld, and per "
                "must be equal-length lists"
            )

        current_links = lengths["R"]
        if current_links < 1:
            raise ValueError(f"scenario {index} contains no links")
        if number_of_links is None:
            number_of_links = current_links
        elif current_links != number_of_links:
            raise ValueError("all scenarios must use the same link count")

        R = [float(value) for value in scenario["R"]]
        N = [int(value) for value in scenario["N"]]
        nmpdu_sld = [int(value) for value in scenario["nmpdu_sld"]]
        per = [float(value) for value in scenario["per"]]
        BAW = int(scenario["BAW"])

        if any(value <= 0.0 for value in R):
            raise ValueError(f"scenario {index}: every PHY rate must be positive")
        if any(value < 0 for value in N):
            raise ValueError(f"scenario {index}: SLD counts cannot be negative")
        if any(value < 0 for value in nmpdu_sld):
            raise ValueError(
                f"scenario {index}: SLD aggregation sizes cannot be negative"
            )
        if any(value < 0.0 or value >= 1.0 for value in per):
            raise ValueError(f"scenario {index}: PER values must lie in [0,1)")
        if BAW <= 0:
            raise ValueError(f"scenario {index}: BAW must be positive")

        normalized_scenario = {
            "R": R,
            "N": N,
            "nmpdu_sld": nmpdu_sld,
            "per": per,
            "BAW": BAW,
        }
        if "tau_F" in scenario:
            tau_F = [float(value) for value in scenario["tau_F"]]
            if len(tau_F) != number_of_links:
                raise ValueError(
                    f"scenario {index}: tau_F must contain "
                    f"{number_of_links} values"
                )
            normalized_scenario["tau_F"] = tau_F
        normalized.append(normalized_scenario)

    return number_of_links, normalized


def main():

    parser = argparse.ArgumentParser(
        description="Airtime-aligned MPDU allocation"
    )
    mode_group = parser.add_mutually_exclusive_group()
    mode_group.add_argument(
        "--large-link-allocation", action="store_true",
        help="run the deterministic L=2,...,8 allocation experiment",
    )
    mode_group.add_argument(
        "--scenario-file", type=Path,
        help="load custom calculation scenarios from a JSON file",
    )
    parser.add_argument(
        "--output", type=Path,
        help="CSV filename or relative path under the outputs directory",
    )
    args = parser.parse_args()

    if args.large_link_allocation:
        run_large_link_allocation_experiment()
        return

    if args.scenario_file:
        try:
            L, scenarios = load_custom_scenarios(args.scenario_file)
        except (OSError, ValueError, json.JSONDecodeError) as error:
            parser.error(f"invalid scenario file: {error}")
        scan_cfg = ScanConfig()
        scan_cfg.scenarios = scenarios
        default_csvpath = OUTPUT_DIR / f"solve-custom-{L}link.csv"
        mode_description = "Custom scenario"
    else:
        # With no mode option, run the paper's two-link experiment directly.
        L = 2
        scan_cfg = ScanConfig()
        default_csvpath = OUTPUT_DIR / "solve-2link.csv"
        mode_description = "Paper two-link scenario"

    if args.output:
        if args.output.is_absolute():
            parser.error("--output must be relative to the outputs directory")
        csvpath = (OUTPUT_DIR / args.output).resolve()
        output_root = OUTPUT_DIR.resolve()
        if csvpath != output_root and output_root not in csvpath.parents:
            parser.error("--output cannot refer to a path outside outputs/")
    else:
        csvpath = default_csvpath

    csvpath.parent.mkdir(parents=True, exist_ok=True)
    cfg = Config(L)

    # DIFS = SIFS + 2 sigma.
    for link in range(L):
        cfg.T_DIFS[link] = cfg.T_SIFS[link] + 2 * cfg.sigma

    default_tau_F = [
        (cfg.T_RTS[l] + cfg.T_DIFS[l]) / cfg.sigma
        for l in range(L)
    ]
    try:
        csv_file, csv_writer = open_csv(csvpath, L, CSV_MODE)
    except Exception as error:
        print("[Error]", error)
        return

    results = []
    timing_wall_us = []
    timing_cpu_us = []
    timing_alloc_us = []
    timing_passes = []

    print("=" * 70)
    print(
        f"{mode_description} scan started " 
        f"({L} Links / Closed-form Solution / P1)"
    )
    print(f"Scenarios: {len(scan_cfg.scenarios)}")
    print("Theoretical time complexity per scenario: O(L*I + L^2)")
    print("Auxiliary-space complexity: O(L)")
    print("=" * 70)

    for index, scenario in enumerate(scan_cfg.scenarios, start=1):
        p_vec = []
        fp_wall_us = []
        fp_cpu_us = []

        for link in range(L):
            wall_start = time.perf_counter_ns()
            cpu_start = time.process_time_ns()
            fp_result = solve_p(
                scenario["N"][link],
                cfg.K_mld[link],
                cfg.K_sld[link],
                cfg.W_mld[link],
                cfg.W_sld[link],
            )
            fp_wall_us.append(
                (time.perf_counter_ns() - wall_start) / 1000.0
            )
            fp_cpu_us.append(
                (time.process_time_ns() - cpu_start) / 1000.0
            )
            p_vec.append(fp_result)

        try:
            result = process_BAW(
                L=L,
                BAW=scenario["BAW"],
                cfg=cfg,
                N_vec=scenario["N"],
                nmpdu_sld=scenario["nmpdu_sld"],
                p_vec=p_vec,
                tau_F=scenario.get("tau_F", default_tau_F),
                R=scenario["R"],
                per_vec=scenario["per"],
                fp_iterations=[item[2] for item in p_vec],
                fp_converged=[item[3] for item in p_vec],
                fp_wall_us=fp_wall_us,
                fp_cpu_us=fp_cpu_us,
            )
            results.append(result)
            timing_wall_us.append(result.point_wall_us)
            timing_cpu_us.append(result.point_cpu_us)
            timing_alloc_us.append(result.allocation_wall_us)
            timing_passes.append(result.active_set_passes)
        except Exception as error:
            print(f"[Error] scenario {index}:", error)
            continue

        print(
            f"[{index}/{len(scan_cfg.scenarios)}] "
            f"R={scenario['R']} N={scenario['N']} "
            f"SLD_MPDU={scenario['nmpdu_sld']} "
            f"PER={scenario['per']} BAW={scenario['BAW']}"
        )

    write_csv_rows(csv_writer, results)
    csv_file.close()

    print("=" * 70)
    print("Scenario Scan Completed")
    if timing_wall_us:
        count = len(timing_wall_us)
        print("Algorithm timing (excludes print and CSV I/O):")
        print(
            f"  operating points={count}, "
            f"mean wall={sum(timing_wall_us)/count:.3f} us, "
            f"min wall={min(timing_wall_us):.3f} us, "
            f"max wall={max(timing_wall_us):.3f} us"
        )
        print(
            f"  mean CPU={sum(timing_cpu_us)/count:.3f} us, "
            f"mean allocation={sum(timing_alloc_us)/count:.3f} us, "
            f"mean active-set passes={sum(timing_passes)/count:.3f}"
        )
    print("Results saved to")
    print(csvpath)

if __name__ == "__main__":
    main()
