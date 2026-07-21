"""Scalability experiment for reviewer Comments 2, 4, and 5.

The experiment reuses the analytical coefficient calculation from
``solve.py`` and instruments the same damped fixed-point and closed-form
active-set algorithms. It evaluates larger link counts under mixed
heterogeneous conditions for W_BA in {256, 1024} and produces one compact CSV
table suitable for a review response.

Measured regions exclude progress printing and CSV I/O. The output is kept
deliberately compact for the reviewer response: correctness, solver effort,
and complete end-to-end algorithm time are reported for each (L, W_BA).
"""

from __future__ import annotations

import argparse
import csv
import importlib.util
import math
import random
import sys
import time
from dataclasses import dataclass
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
SOLVER_PATH = SCRIPT_DIR / "solve.py"


def load_solver():
    spec = importlib.util.spec_from_file_location("solve_simp", SOLVER_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Cannot load {SOLVER_PATH}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


solver = load_solver()


@dataclass(frozen=True)
class ScalingScanConfig:
    """Extended scan configuration for the L-link scalability experiment."""

    L_values: tuple[int, ...] = (2, 3, 4, 5, 6, 7, 8)
    BAW_values: tuple[int, ...] = (256, 1024)
    trials_per_case: int = 1000
    seed: int = 20260717
    max_iter: int = 2000
    tol: float = 1e-12
    damping: float = 0.4


@dataclass
class TrialResult:
    constraint_valid: bool
    all_fp_converged: bool
    fp_iterations_max: int
    active_set_passes: int
    cached_update_us: float
    full_wall_us: float


def percentile(values, probability):
    """Linearly interpolated percentile without third-party dependencies."""
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    position = probability * (len(ordered) - 1)
    lower = int(math.floor(position))
    upper = int(math.ceil(position))
    fraction = position - lower
    return ordered[lower] * (1.0 - fraction) + ordered[upper] * fraction


def solve_fixed_point(N, K_mld, K_sld, W_mld, W_sld, max_iter, tol, damping):
    """Instrumented version of the production damped fixed-point iteration."""
    if N <= 0:
        return 1.0, 0.0, 0, True

    eps = 1e-14
    pM = 0.9
    pS = 0.9

    for iteration in range(1, max_iter + 1):
        pS_part = solver.calc_p_part(pS, W_sld, K_sld)
        pM_part = solver.calc_p_part(pM, W_mld, K_mld)

        pM_raw = max(0.0, 1.0 - pS_part) ** N
        pS_raw = (
            (1.0 - pM_part)
            * max(0.0, 1.0 - pS_part) ** (N - 1)
        )
        pM_new = min(max((1.0 - damping) * pM + damping * pM_raw, eps), 1.0 - eps)
        pS_new = min(max((1.0 - damping) * pS + damping * pS_raw, eps), 1.0 - eps)

        if abs(pM_new - pM) < tol and abs(pS_new - pS) < tol:
            return pM_new, pS_new, iteration, True
        pM, pS = pM_new, pS_new

    return pM, pS, max_iter, False


def solve_allocation(coef, BAW):
    """Instrumented active-set implementation of the paper's closed form."""
    L = len(coef)
    active = [True] * L
    allocation = [0] * L
    passes = 0

    while True:
        passes += 1
        indices = [l for l in range(L) if active[l]]
        if not indices:
            return allocation, passes

        sum_inv_B = sum(1.0 / coef[l].B for l in indices)
        sum_A_over_B = sum(coef[l].A / coef[l].B for l in indices)
        aligned_time = (BAW + sum_A_over_B) / sum_inv_B
        continuous = [0.0] * L

        removed = False
        for l in indices:
            continuous[l] = (aligned_time - coef[l].A) / coef[l].B
            if continuous[l] <= 0.0:
                active[l] = False
                removed = True
        if removed:
            continue

        for l in indices:
            allocation[l] = max(1, round(continuous[l]))

        residual = BAW - sum(allocation)
        if residual:
            adjust = min(indices, key=lambda l: coef[l].B)
            allocation[adjust] += residual
        return allocation, passes


def make_operating_point(L, BAW, trial, seed):
    """Generate deterministic but diverse per-link model parameters."""
    rng = random.Random(seed + 1000003 * L + 1009 * trial)

    R = [rng.uniform(80.0, 1200.0) for _ in range(L)]
    N = [rng.randint(0, 8) for _ in range(L)]
    sld_mpdu = [rng.choice((8, 16, 32, 64, 128)) for _ in range(L)]
    Wm = [float(rng.choice((4.0, 8.0, 16.0))) for _ in range(L)]
    Ws = [float(rng.choice((4.0, 8.0, 16.0))) for _ in range(L)]
    Km = [rng.choice((1, 3, 6)) for _ in range(L)]
    Ks = [rng.choice((1, 3, 6)) for _ in range(L)]

    return R, N, sld_mpdu, Wm, Ws, Km, Ks


def run_trial(L, BAW, trial, scan):
    R, N, sld_mpdu, Wm, Ws, Km, Ks = make_operating_point(
        L, BAW, trial, scan.seed
    )
    cfg = solver.Config(L)
    cfg.W_mld = Wm
    cfg.W_sld = Ws
    cfg.K_mld = Km
    cfg.K_sld = Ks
    tau_F = [0.0] * L
    for l in range(L):
        cfg.T_DIFS[l] = cfg.T_SIFS[l] + 2.0 * cfg.sigma
        tau_F[l] = (cfg.T_RTS[l] + cfg.T_DIFS[l]) / cfg.sigma

    full_start = time.perf_counter_ns()
    fixed_points = [
        solve_fixed_point(
            N[l], Km[l], Ks[l], Wm[l], Ws[l],
            scan.max_iter, scan.tol, scan.damping,
        )
        for l in range(L)
    ]

    # Cached update: contention parameters are unchanged, so the previously
    # obtained fixed points are reused. Only A/B coefficients and the
    # closed-form active-set allocation are recomputed.
    cached_start = time.perf_counter_ns()
    coef = [
        solver.calc_AB(
            l, sld_mpdu[l], fixed_points[l][0], fixed_points[l][1],
            N[l], BAW, cfg, tau_F, cfg.T_DIFS, R,
        )
        for l in range(L)
    ]

    allocation, passes = solve_allocation(coef, BAW)
    cached_update_us = (time.perf_counter_ns() - cached_start) / 1000.0
    full_wall_us = (time.perf_counter_ns() - full_start) / 1000.0

    return TrialResult(
        constraint_valid=(sum(allocation) == BAW and all(x >= 0 for x in allocation)),
        all_fp_converged=all(x[3] for x in fixed_points),
        fp_iterations_max=max(x[2] for x in fixed_points),
        active_set_passes=passes,
        cached_update_us=cached_update_us,
        full_wall_us=full_wall_us,
    )


def summarize(L, BAW, trials):
    """Summarize correctness, solver effort, and wall-clock runtime."""
    def values(name):
        return [getattr(x, name) for x in trials]

    cached_us = values("cached_update_us")
    full_us = values("full_wall_us")
    successful = [
        x.constraint_valid and x.all_fp_converged for x in trials
    ]
    return {
        "L": L,
        "BAW": BAW,
        "tested_points": len(trials),
        "success_rate": round(sum(successful) / len(trials), 6),
        "max_fp_iterations": max(values("fp_iterations_max")),
        "active_set_passes_max": max(values("active_set_passes")),
        "cached_update_ms_p50": round(percentile(cached_us, 0.50) / 1000.0, 6),
        "cached_update_ms_p95": round(percentile(cached_us, 0.95) / 1000.0, 6),
        "full_update_ms_p50": round(percentile(full_us, 0.50) / 1000.0, 6),
        "full_update_ms_p95": round(percentile(full_us, 0.95) / 1000.0, 6),
    }


def write_latex_table(rows, path, trials_per_case):
    """Write Table tab:scalability for direct inclusion in the response."""
    lines = [
        r"\begin{table*}[t]",
        r"\centering",
        r"\caption{Numerical scalability and host-side runtime of the proposed "
        r"allocation method.}",
        r"\label{tab:scalability}",
        r"\begin{tabular}{ccccc cc}",
        r"\hline",
        r"$L$ & $W_{BA}$ & Success & $I_{\max}$ & "
        r"$J_{\max}$ & Cached update (P50/P95) & "
        r"Full update (P50/P95) \\",
        r"\hline",
    ]
    for row in rows:
        lines.append(
            f"{row['L']} & {row['BAW']} & "
            f"{100.0 * row['success_rate']:.1f}\\% & "
            f"{row['max_fp_iterations']} & "
            f"{row['active_set_passes_max']} & "
            f"{row['cached_update_ms_p50']:.3f}/"
            f"{row['cached_update_ms_p95']:.3f} & "
            f"{row['full_update_ms_p50']:.3f}/"
            f"{row['full_update_ms_p95']:.3f} \\\\"
        )
    lines.extend([r"\hline", r"\end{tabular}", r"\end{table*}", ""])
    path.write_text("\n".join(lines), encoding="utf-8")


def run_experiment(scan, output_path):
    output_path.parent.mkdir(parents=True, exist_ok=True)
    rows = []
    total_cases = len(scan.L_values) * len(scan.BAW_values)
    case = 0

    for L in scan.L_values:
        for BAW in scan.BAW_values:
            case += 1
            trials = [
                run_trial(L, BAW, trial, scan)
                for trial in range(scan.trials_per_case)
            ]

            rows.append(summarize(L, BAW, trials))
            print(f"[{case}/{total_cases}] L={L}, BAW={BAW}")

    with output_path.open("w", newline="", encoding="utf-8") as fout:
        writer = csv.DictWriter(fout, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)

    write_latex_table(rows, output_path.parent / "scalability.tex", scan.trials_per_case)

    failures = [row for row in rows if row["success_rate"] < 1.0]
    print(f"Summary rows: {len(rows)}")
    print(f"Rows with a numerical/constraint failure: {len(failures)}")
    print(f"CSV table: {output_path}")
    return 1 if failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trials", type=int, default=1000)
    parser.add_argument(
        "--output",
        type=Path,
        default=SCRIPT_DIR / "outputs" / "scalability.csv",
    )
    args = parser.parse_args()

    scan = ScalingScanConfig(
        trials_per_case=args.trials,
    )
    return run_experiment(scan, args.output)


if __name__ == "__main__":
    raise SystemExit(main())
