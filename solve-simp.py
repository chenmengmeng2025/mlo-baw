"""
Corresponding to Problem P1 (Section II-C / Section III-B)

Objective:
     min_{M_A^(l,M)}
         (1/L) Σ_{l=1}^{L} ( T̄^(l,M) − (1/L)Σ_j T̄^(j,M) )²
Subject to:
     Σ_{l=1}^{L} M_A^(l,M) = W_BA
where M_A^(l,M) denotes the maximum A-MPDU aggregation size assigned to
link l and W_BA is the shared Block ACK window size.

By expressing the average successful transmission period as
     T̄^(l,M) = σ(A^(l) + B^(l)M_A^(l,M)),
where
     B^(l) = (L_P + L_MH + 80)/(R^(l)σ),
and
             1 + τ_F^(l)(1-p^(l,M))
           + n^(l,S)(τ_T^(l,S)-τ_F^(l))p^(l,S)
             (1-(p^(l,M))^(1/n^(l,S)))
A^(l) = ------------------------------------- + T_OH^(l).
         p^(l,M)-p^(l,S)(p^(l,M))^(1/n^(l,S))

When n^(l,S)=0 (interference-free case),
     A^(l) = (W^(l,M)+1)/2 + T_OH^(l).

The optimization problem becomes a constrained linear allocation problem.
Airtime alignment is achieved by equalizing T̄^(l,M) among all active links,
thereby mitigating the BAW stall caused by the shared Block ACK mechanism.

The resulting closed-form solution is
     M_A^(l,M),* = (T*/σ − A^(l)) / B^(l)
     T*/σ = (W_BA + Σ_l A^(l)/B^(l)) / Σ_l (1/B^(l))

If the computed allocation of a link is non-positive, that link is removed
from the active set and the closed-form solution is recomputed iteratively.
"""
from pathlib import Path
from dataclasses import dataclass, field
from enum import Enum
from typing import List
import csv
import math
import numpy as np

# Number of links.
# Change NUM_LINKS from 2 to 3 to switch between the two-link and three-link
# scenarios considered in the paper.
NUM_LINKS = 2

filepath = Path(__file__)
csv_name = f"solve-{NUM_LINKS}link.csv"
csvpath = filepath.parent / csv_name

# CSV output mode
class CsvMode(Enum):
    OVERWRITE = 0
    APPEND = 1

CSV_MODE = CsvMode.APPEND

# ---------------------------------------------------------------------------
# Parameter scanning configuration used for numerical evaluation.
# 
# Each vector corresponds to one system parameter investigated in the paper,
# including
#      n^(l,S)      : number of coexisting SLDs,
#      M_A^(l,S)    : SLD aggregation size,
#      W_BA         : shared Block ACK window,
#      R^(l)        : PHY transmission rates (Mbps),
#      PER          : packet error rate.
# ---------------------------------------------------------------------------
@dataclass
class ScanConfig:
    """
    Parameter scanning configuration.

    N_values:
        Number of coexisting SLDs on each link.

    nmpdu_sld_values:
        Aggregation size of SLDs.

    BAW_values:
        Shared Block ACK window size.

    R_values:
        PHY transmission rates (Mbps).

    per:
        Packet error rate.
    """

    L: int

    N_values: List[int] = field(default_factory=list)
    nmpdu_sld_values: List[List[int]] = field(default_factory=list)
    BAW_values: List[int] = field(default_factory=lambda: [256])
    R_values: List[List[float]] = field(default_factory=list)
    per: List[List[float]] = field(default_factory=list)

    def __post_init__(self):

        self.N_values = [0] * self.L
        self.nmpdu_sld_values = [[0] for _ in range(self.L)]
        self.R_values = [[100.0] for _ in range(self.L)]
        self.per = [[0.0] for _ in range(self.L)]

        # Default configuration corresponding to the simulation setup
        # presented in Fig. 4 of the paper.
        # 
        # Two-link Wi-Fi 7 network:
        #      Link 1 : four coexisting SLDs
        #      Link 2 : one coexisting SLD
        # 
        # SLD aggregation sizes are fixed as
        #      M_A^(1,S)=24,
        #      M_A^(2,S)=232,
        # with W_BA = 256.
        if self.L == 2:

            self.N_values = [4, 1]

            self.nmpdu_sld_values = [
                [24],
                [232]
            ]

            self.BAW_values = [256]

            self.R_values = [
                [
                    206.470592,
                    229.411766,
                    258.088236,
                    275.294120,
                    286.764706,
                    309.705884,
                    344.117648,
                    412.941180,
                    458.823532,
                    516.176472
                ],
                [
                    1080.882354
                ]
            ]

            self.per = [
                [0.0],
                [0.0]
            ]

# ---------------------------------------------------------------------------
# System parameters following IEEE 802.11be and the simulation settings
# adopted in the paper.
# ---------------------------------------------------------------------------
@dataclass
class Config:
    """
    System parameters used in the analytical model.
    """

    L: int

    sigma: float = 9.0                     # Slot duration σ (μs)
    L_subf: float = (1500 + 62 + 10) * 8.0 # Number of transmitted bits contained in one MPDU, including
                                           #  payload, MAC header and aggregation overhead.
    L_P: float = 1500.0 * 8.0              # Payload length (bits)
    T_PH_D: float = 56.0                   # PHY preamble duration for data frames

    # Control frame transmission durations
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
# Result container storing the optimal MPDU allocation together with the
# corresponding analytical performance metrics.
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

    # Fixed-point success probability p_A^(l,M)
    p: List[float] = field(default_factory=list)

    # Steady-state idle probability α^(l)
    alpha: List[float] = field(default_factory=list)

    # Steady-state throughput λ_out^(l,M)
    lambda_out: List[float] = field(default_factory=list)

    # Single-Link Upper Bound (SL-UB)
    D_slo: float = 0.0
    lambda_slo: float = 0.0

    per: List[float] = field(default_factory=list)


# ============================================================================
# Compute the steady-state idle probability α^(l) (Eq. (9)).
# ============================================================================
def compute_alpha(
    pM: float,
    pS: float,
    tau_F: float,
    tau_T_M: float,
    tau_T_S: float,
    N: int,
) -> float:
    """
    Compute alpha^(l)
    """

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
# Compute the common backoff-related term appearing in the fixed-point
# equations (Eq. (11)-(12)).
# ============================================================================
def calc_p_part(
    p: float,
    W: float,
    K: int,
) -> float:
    """
    Eq. (11)-(12)
    """

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
# Section III-A-3: Solve the fixed-point equations (Eq. (11)-(12)).
# 
# This function computes the conditional success probabilities
# 
#      p_A^(l,M) : MLD success probability,
#      p_A^(l,S) : SLD success probability,
# 
# by iteratively solving the coupled nonlinear equations derived from the
# IEEE 802.11 contention model.
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
):
    """
    Solve the coupled fixed-point equations.

    Returns
    -------
    (pM, pS)

    pM : conditional success probability of the MLD
    pS : conditional success probability of each SLD
    """

    # No competing SLD exists. The MLD always succeeds in transmission.
    if N <= 0:
        return 1.0, 0.0

    eps = 1e-14

    pM = 0.9
    pS = 0.9

    # Solve the coupled fixed-point equations using successive iteration.
    # A damping factor is applied to improve numerical stability and
    # accelerate convergence.
    for _ in range(max_iter):

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
            return pM_new, pS_new

        pM = pM_new
        pS = pS_new

    # Maximum iteration reached
    return pM, pS

# ============================================================================
# Compute the steady-state throughput of the MLD.
# 
# According to the analytical model, this function evaluates
#      λ_out^(l,M) : steady-state throughput of the MLD,
# together with the steady-state idle probability α^(l).
# ============================================================================
def compute_lambda(
    n: int,
    pM: float,
    pS: float,
    tau_T,
    tau_F: float,
    W_mld: float,
):
    """
    Compute

        λ_out^(l,M)

    together with

        α^(l)
    """

    # No competing SLD exists. According to Eq. (6), every transmission
    # cycle consists of one average backoff period followed by one
    # successful A-MPDU transmission.
    if n == 0:
        lambdaM = 1.0 / (((W_mld + 1.0) / 2.0) + tau_T[0])
        return lambdaM, 1.0

    # Compute the steady-state idle probability α^(l) using Eq. (7).
    alpha = compute_alpha(
        pM,
        pS,
        tau_F,
        tau_T[0],
        tau_T[1],
        n,
    )

    # Compute the aggregate SLD throughput and the MLD throughput
    # according to Eq. (8).
    lambdaM = (
        1.0
        - pS * (pM ** (1.0 / n - 1.0))
    ) * alpha * pM

    return lambdaM, alpha

# ============================================================================
# Compute the effective data throughput.
# 
# According to the analytical model, the throughput is obtained from the
# steady-state successful transmission rate λ_out by accounting for the
# packet error rate (PER) and the payload size.
# ============================================================================
def get_throughput(
    PL: float,
    sigma: float,
    lambda_out: float,
    nmpdu: float,
    per: float = 0.0,
):
    """
    Compute the effective throughput.

    Parameters
    ----------
    PL : payload length (bit)
    sigma : slot duration (μs)
    lambda_out : successful transmission rate
    nmpdu : aggregation size
    per : packet error rate
    """

    # No MPDU allocated
    if nmpdu < 1e-9:
        return 0.0

    # Compute the effective throughput according to Eq. (13).
    tp = lambda_out * (1.0 - per) * PL / sigma

    if tp < 0:
        raise RuntimeError(f"Invalid throughput: {tp}")

    return tp


# ============================================================================
# Compute the duration of one successful transmission τ_T^(l) (Eq. (3)).
# ============================================================================
def calc_tau_T(
    nmpdu: float,
    L_subf: float,
    rate: float,
    sigma: float,
    T_OH: float,
):
    """
    τ_T
    """

    if nmpdu < 1e-9:
        return 0.0

    return ((nmpdu * L_subf) / rate) / sigma + T_OH

# ============================================================================
# Get the transmission duration of the Block ACK (BA) frame.
# T_{BA} = T_{PH,C} + L_{BA} / R_{B}^{(l)}
# ============================================================================
def get_T_BA(BAW: int) -> float:
    if BAW <= 256:
        return 40.0
    elif BAW <= 512:
        return 52.0
    else:
        return 72.0

# ============================================================================
# Compute the airtime variance among all active links.
# 
# The average successful transmission period T̄^(l,M) is first calculated
# for every link, after which the variance is evaluated as
#      (1/L) Σ_l (T̄^(l,M) − T̄_avg)^2.
# This is exactly the optimization objective of Problem P1.
# ============================================================================
def calc_variance(T):
    """
    Compute

        (1/L) Σ (T_i - mean(T))²
    """

    if len(T) == 0:
        return 0.0

    mean = sum(T) / len(T)

    var = 0.0

    for t in T:
        var += (t - mean) ** 2

    return var / len(T)

# Link-specific analytical performance metrics.
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
# Evaluate the analytical performance of one MLO link.
# 
# This function computes the analytical performance metrics of the current
# link, including the successful transmission rate, throughput, average
# transmission period, and steady-state idle probability.
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
    """
    Compute

        λ_out
        throughput
        average transmission period
        idle probability
    """

    T_BA_mld = get_T_BA(BAW)
    T_BA_sld = get_T_BA(BAW)

    T_OH_mld = (
        cfg.T_SIFS[link_idx] * 3
        + T_BA_mld
        + cfg.T_DIFS[link_idx]
        + cfg.T_RTS[link_idx]
        + cfg.T_CTS[link_idx]
        + cfg.T_PH_D
    ) / cfg.sigma

    T_OH_sld = (
        cfg.T_SIFS[link_idx] * 3
        + T_BA_sld
        + cfg.T_DIFS[link_idx]
        + cfg.T_RTS[link_idx]
        + cfg.T_CTS[link_idx]
        + cfg.T_PH_D
    ) / cfg.sigma

    # Compute the successful transmission durations τ_T^(l) of the MLD
    # and the competing SLD.
    tau_T = [
        calc_tau_T(
            n,
            cfg.L_subf,
            R[link_idx],
            cfg.sigma,
            T_OH_mld,
        ),
        calc_tau_T(
            nmpdu_sld_l,
            cfg.L_subf,
            R[link_idx],
            cfg.sigma,
            T_OH_sld,
        ),
    ]

    # Compute the steady-state successful transmission rate λ_out^(l,M)
    # and the idle probability α^(l).
    lambdaM, alpha = compute_lambda(
        N,
        pM,
        pS,
        tau_T,
        tau_F_vec[link_idx],
        cfg.W_mld[link_idx],
    )

    # Compute the effective throughput and the average successful
    # transmission period of the current link.
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

# Linear coefficients of the airtime model.
@dataclass
class LinearCoef:
    """
    Linear airtime model

        T = σ (A + B * M_A)
    """

    A: float
    B: float

# ============================================================================
# Compute the coefficients A^(l) and B^(l) in the linear airtime model.
# ============================================================================
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
    """
    Compute the linear airtime coefficients

        T = σ(A + B M)
    """

    T_BA_mld = get_T_BA(BAW)

    T_OH_mld = (
        cfg.T_SIFS[link_idx] * 3
        + T_BA_mld
        + T_DIFS[link_idx]
        + cfg.T_RTS[link_idx]
        + cfg.T_CTS[link_idx]
        + cfg.T_PH_D
    ) / cfg.sigma

    B_l = cfg.L_subf / (
        R[link_idx] * cfg.sigma
    )

    # No competing SLD
    if N <= 0:
        # No competing SLD exists. The average successful transmission period
        # reduces to one average backoff period plus the protocol overhead.
        A_l = (
            (cfg.W_mld[link_idx] + 1.0) / 2.0
            + T_OH_mld
        )

        return LinearCoef(A=A_l, B=B_l)

    # Competing SLD exists. Compute the fixed airtime coefficient A^(l) 
    # according to the analytical model.

    T_BA_sld = get_T_BA(BAW)

    T_OH_sld = (
        cfg.T_SIFS[link_idx] * 3
        + T_BA_sld
        + T_DIFS[link_idx]
        + cfg.T_RTS[link_idx]
        + cfg.T_CTS[link_idx]
        + cfg.T_PH_D
    ) / cfg.sigma

    tau_T_S = calc_tau_T(
        nmpdu_sld_l,
        cfg.L_subf,
        R[link_idx],
        cfg.sigma,
        T_OH_sld,
    )

    tau_F = tau_F_vec[link_idx]

    pM_pow = pM ** (1.0 / N)

    delta = pM - pS * pM_pow

    if delta <= 1e-15:
        # Degenerate case. A non-positive denominator indicates that the
        # current link is no longer suitable for airtime allocation.
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

    A_l = numerator / delta + T_OH_mld

    return LinearCoef(
        A=A_l,
        B=B_l,
    )

# ============================================================================
# Solve the airtime alignment problem using the closed-form solution.
# This function computes the optimal MPDU allocation for all links 
# while satisfying the shared Block ACK window constraint.
# ============================================================================
def solve_closed_form(coef, BAW):
    """
    Closed-form solution of

        min Σ (T_l - T_avg)^2

    s.t.

        Σ M_A^(l,M) = BAW

    Parameters
    ----------
    coef : List[LinearCoef]
    BAW : int

    Returns
    -------
    List[int]
        Optimal MPDU allocation on every link.
    """

    L = len(coef)

    active = [True] * L
    n_alloc = [0] * L

    while True:

        sum_inv_B = 0.0
        sum_A_over_B = 0.0
        num_active = 0

        # ----------------------------------------------------------
        # Statistics of currently active links
        # ----------------------------------------------------------

        for l in range(L):

            if not active[l]:
                continue

            sum_inv_B += 1.0 / coef[l].B
            sum_A_over_B += coef[l].A / coef[l].B
            num_active += 1

        if num_active == 0:
            break

        # Closed-form optimal common airtime T*/σ
        T_star_over_sigma = (
            BAW + sum_A_over_B
        ) / sum_inv_B

        any_negative = False

        real_alloc = [0.0] * L

        # ompute the continuous MPDU allocation of each active link.
        for l in range(L):

            if not active[l]:
                continue

            real_alloc[l] = (
                T_star_over_sigma - coef[l].A
            ) / coef[l].B

            # Links receiving a non-positive allocation are removed from
            # the active set, and the closed-form solution is recomputed.
            if real_alloc[l] <= 0.5:
                active[l] = False
                any_negative = True

        # Resolve reduced optimization problem
        if any_negative:
            continue

        # Convergence: All activation link assignments are positive, perform 
        # integer rounding and correct the rounding error
        total = 0
        for l in range(L):
            if active[l]:
                n_alloc[l] = round(real_alloc[l])
                if n_alloc[l] < 1:
                    n_alloc[l] = 1
                total += n_alloc[l]
            else:
                n_alloc[l] = 0

        # Compensate for the rounding error so that
        # 
        #      Σ_l M_A^(l,M) = W_BA.
        # 
        # The adjustment is applied to the link with the smallest B^(l),
        # which has the smallest marginal impact on the objective.
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

    return n_alloc

# ============================================================================
# Evaluate the analytical performance for one Block ACK window size.
# 
# This function performs the complete analytical evaluation of the proposed
# airtime alignment scheme for a given Block ACK window size and returns
# all performance metrics required by the numerical evaluation.
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
):
    """
    Evaluate one operating point.

    Parameters
    ----------
    L : int
        Number of links.

    BAW : int
        Shared Block ACK window size.

    cfg : Config
        System configuration.

    N_vec : list[int]
        Number of competing SLDs on each link.

    nmpdu_sld : list[int]
        Aggregation size of SLDs.

    p_vec : list[(pM,pS)]
        Fixed-point solutions.

    tau_F : list[float]

    R : list[float]

    per_vec : list[float]

    Returns
    -------
    Result
    """

    # ------------------------------------------------------------
    # Extract pM and pS
    # ------------------------------------------------------------

    pM = [x[0] for x in p_vec]
    pS = [x[1] for x in p_vec]

    # Compute the linear airtime coefficients A^(l) and B^(l) used by
    # the closed-form airtime alignment algorithm.

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

    best_n = solve_closed_form(coef, BAW)

    # Re-evaluate every active link using the complete analytical model.
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

    # Evaluate SLO baseline
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

    # Assemble all analytical results for the current operating point.

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

    return res

# ============================================================================
# CSV header
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
# Open csv file
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

    # --------------------------------------------------------
    # append mode
    # --------------------------------------------------------

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

    # --------------------------------------------------------
    # overwrite mode
    # --------------------------------------------------------

    fout = open(path, "w", newline="")

    writer = csv.writer(fout)

    writer.writerow(expected)

    return fout, writer

# ============================================================================
# Write one result
# ============================================================================

def write_csv_row(writer, r: Result):
    """
    Write one Result into csv.
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

    writer.writerow(row)


def main():

    L = NUM_LINKS

    # Initialize configuration
    cfg = Config(L)
    scan_cfg = ScanConfig(L)

    # DIFS = SIFS + 2σ
    for i in range(L):
        cfg.T_DIFS[i] = cfg.T_SIFS[i] + 2 * cfg.sigma

    # Solve the fixed-point equations
    global_p_vec = []

    for i in range(L):

        global_p_vec.append(
            solve_p(
                scan_cfg.N_values[i],
                cfg.K_mld[i],
                cfg.K_sld[i],
                cfg.W_mld[i],
                cfg.W_sld[i],
            )
        )

    # Create the CSV file for storing all analytical results.
    try:

        csv_file, csv_writer = open_csv(
            csvpath,
            L,
            CSV_MODE,
        )

    except Exception as e:

        print("[Error]", e)

        return

    print("=" * 70)
    print(
        f"Parameter Scan Started "
        f"({L} Links / Closed-form Solution / P1)"
    )

    print(
        "CSV Mode:",
        "Append"
        if CSV_MODE == CsvMode.APPEND
        else "Overwrite",
    )

    for l in range(L):

        print(
            f"Link {l}: "
            f"N={scan_cfg.N_values[l]} "
            f"SLD_MPDU={scan_cfg.nmpdu_sld_values[l]} "
            f"R({len(scan_cfg.R_values[l])} values) "
            f"PER({len(scan_cfg.per[l])} values)"
        )

    print("BAW values:", scan_cfg.BAW_values)

    # Count combinations
    total = len(scan_cfg.BAW_values)

    for l in range(L):
        total *= len(scan_cfg.R_values[l])

    for l in range(L):
        total *= len(scan_cfg.nmpdu_sld_values[l])

    for l in range(L):
        total *= len(scan_cfg.per[l])

    print("Total combinations:", total)
    print("=" * 70)

    cur = 0

    R_cur = [0.0] * L
    sld_cur = [0] * L
    per_cur = [0.0] * L

    # Recursive scan
    def scan_links(link):

        nonlocal cur

        if link == L:

            tau_F = [0.0] * L

            for baw in scan_cfg.BAW_values:

                cur += 1

                print(f"[{cur}/{total}] ", end="")

                for i in range(L):
                    print(f"R{i+1}={R_cur[i]} ", end="")

                print(
                    f"SLD={sld_cur} "
                    f"PER={per_cur} "
                    f"BAW={baw}"
                )

                try:

                    result = process_BAW(
                        L=L,
                        BAW=baw,
                        cfg=cfg,
                        N_vec=scan_cfg.N_values,
                        nmpdu_sld=sld_cur.copy(),
                        p_vec=global_p_vec,
                        tau_F=tau_F,
                        R=R_cur.copy(),
                        per_vec=per_cur.copy(),
                    )

                    write_csv_row(
                        csv_writer,
                        result,
                    )

                except Exception as e:

                    print("[Error]", e)

            return


        for R in scan_cfg.R_values[link]:

            R_cur[link] = R

            for s in scan_cfg.nmpdu_sld_values[link]:

                sld_cur[link] = s

                for p in scan_cfg.per[link]:

                    per_cur[link] = p

                    scan_links(link + 1)

    # Start scan

    scan_links(0)

    csv_file.close()

    print("=" * 70)
    print("Parameter Scan Completed")
    print("Results saved to")
    print(csvpath)

if __name__ == "__main__":
    main()