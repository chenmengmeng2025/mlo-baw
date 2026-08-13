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
    execution_time_us: float
    exhaustive_evaluated: bool = False
    exhaustive_combinations: int = 0
    same_allocation: int = 0
    normalized_variance_gap: float = 0.0
    relative_throughput_gap: float = 0.0
    exhaustive_time_us: float = 0.0


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
    """Instrumented active-set and O(L log L) largest-remainder implementation."""
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
            allocation[l] = math.floor(continuous[l])

        residual = BAW - sum(allocation)
        ranked = sorted(
            indices,
            key=lambda l: (-(continuous[l] - allocation[l]), l),
        )
        if not 0 <= residual <= len(ranked):
            raise RuntimeError("invalid largest-remainder residual")
        for l in ranked[:residual]:
            allocation[l] += 1
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


def run_trial(L, BAW, trial, scan, exhaustive=False):
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

    coef = [
        solver.calc_AB(
            l, sld_mpdu[l], fixed_points[l][0], fixed_points[l][1],
            N[l], BAW, cfg, tau_F, cfg.T_DIFS, R,
        )
        for l in range(L)
    ]

    allocation, passes = solve_allocation(coef, BAW)
    execution_time_us = (time.perf_counter_ns() - full_start) / 1000.0

    exhaustive_evaluated = False
    exhaustive_combinations = 0
    same_allocation = 0
    normalized_variance_gap = 0.0
    relative_throughput_gap = 0.0
    exhaustive_time_us = 0.0

    if exhaustive:
        exhaustive_combinations = solver.exhaustive_combination_count(
            BAW, allocation
        )
        exhaustive_start = time.perf_counter_ns()
        comparison = solver.compare_with_exhaustive(
            coef=coef,
            BAW=BAW,
            proposed_allocation=allocation,
            sigma=cfg.sigma,
            payload_bits=cfg.L_P,
            per_vec=[0.0] * L,
            max_combinations=exhaustive_combinations,
        )
        exhaustive_time_us = (
            time.perf_counter_ns() - exhaustive_start
        ) / 1000.0
        exhaustive_evaluated = True
        same_allocation = comparison.same_allocation
        normalized_variance_gap = comparison.normalized_variance_gap
        relative_throughput_gap = comparison.relative_throughput_gap

    return TrialResult(
        constraint_valid=(sum(allocation) == BAW and all(x >= 0 for x in allocation)),
        all_fp_converged=all(x[3] for x in fixed_points),
        fp_iterations_max=max(x[2] for x in fixed_points),
        active_set_passes=passes,
        execution_time_us=execution_time_us,
        exhaustive_evaluated=exhaustive_evaluated,
        exhaustive_combinations=exhaustive_combinations,
        same_allocation=same_allocation,
        normalized_variance_gap=normalized_variance_gap,
        relative_throughput_gap=relative_throughput_gap,
        exhaustive_time_us=exhaustive_time_us,
    )


def summarize(L, BAW, trials, exhaustive=False):
    """Summarize correctness, solver effort, and wall-clock runtime."""
    def values(name):
        return [getattr(x, name) for x in trials]

    full_us = values("execution_time_us")
    successful = [
        x.constraint_valid and x.all_fp_converged for x in trials
    ]
    summary = {
        "L": L,
        "BAW": BAW,
        "tested_points": len(trials),
        "success_rate": round(sum(successful) / len(trials), 6),
        "max_fp_iterations": max(values("fp_iterations_max")),
        "active_set_passes_max": max(values("active_set_passes")),
        "execution_time_ms_p50": round(percentile(full_us, 0.50) / 1000.0, 6),
        "execution_time_ms_p95": round(percentile(full_us, 0.95) / 1000.0, 6),
    }

    if exhaustive:
        compared = [trial for trial in trials if trial.exhaustive_evaluated]
        summary["exhaustive_tested_points"] = len(compared)
        if compared:
            summary["same_allocation_rate"] = round(
                sum(trial.same_allocation for trial in compared) / len(compared),
                6,
            )
            summary["normalized_variance_gap_mean"] = round(
                sum(trial.normalized_variance_gap for trial in compared)
                / len(compared),
                12,
            )
            summary["throughput_gap_pct_mean"] = round(
                100.0
                * sum(trial.relative_throughput_gap for trial in compared)
                / len(compared),
                9,
            )
            exhaustive_us = [
                trial.exhaustive_time_us for trial in compared
            ]
            summary["exhaustive_time_ms_p95"] = round(
                percentile(exhaustive_us, 0.95) / 1000.0,
                6,
            )
            summary["exhaustive_time_ms_p99"] = round(
                percentile(exhaustive_us, 0.99) / 1000.0,
                6,
            )
        else:
            summary["same_allocation_rate"] = ""
            summary["normalized_variance_gap_mean"] = ""
            summary["throughput_gap_pct_mean"] = ""
            summary["exhaustive_time_ms_p95"] = ""
            summary["exhaustive_time_ms_p99"] = ""

    return summary


def write_latex_table(rows, path, trials_per_case, exhaustive=False):
    """Write Table tab:scalability for direct inclusion in the response."""

    if exhaustive:
        column_spec = "ccccccc"
        runtime_header = (
            r"$J_{\max}$ & Proposed (P50/P95, ms) & "
            r"Enumeration (P95/P99, ms) \\"
        )
    else:
        column_spec = "cccccc"
        runtime_header = (
            r"$J_{\max}$ & Execution Time (P50/P95, ms) \\"
        )

    lines = [
        r"\begin{table*}[t]",
        r"\centering",
        r"\caption{Numerical scalability and host-side runtime of the proposed "
        r"allocation method.}",
        r"\label{tab:scalability}",
        rf"\begin{{tabular}}{{{column_spec}}}",
        r"\hline",
        r"$L$ & $W_{BA}$ & Success & $I_{\max}$ & " + runtime_header,
        r"\hline",
    ]
    for row in rows:
        line = (
            f"{row['L']} & {row['BAW']} & "
            f"{100.0 * row['success_rate']:.1f}\\% & "
            f"{row['max_fp_iterations']} & "
            f"{row['active_set_passes_max']} & "
            f"{row['execution_time_ms_p50']:.3f}/"
            f"{row['execution_time_ms_p95']:.3f}"
        )
        if exhaustive:
            line += (
                f" & {row['exhaustive_time_ms_p95']:.3f}/"
                f"{row['exhaustive_time_ms_p99']:.3f}"
            )
        lines.append(line + r" \\")
    lines.extend([r"\hline", r"\end{tabular}", r"\end{table*}", ""])
    path.write_text("\n".join(lines), encoding="utf-8")


def run_experiment(
    scan,
    output_path,
    exhaustive=False,
):
    output_path.parent.mkdir(parents=True, exist_ok=True)
    rows = []
    total_cases = len(scan.L_values) * len(scan.BAW_values)
    case = 0

    for L in scan.L_values:
        for BAW in scan.BAW_values:
            case += 1
            trials = [
                run_trial(
                    L,
                    BAW,
                    trial,
                    scan,
                    exhaustive=exhaustive,
                )
                for trial in range(scan.trials_per_case)
            ]

            rows.append(summarize(L, BAW, trials, exhaustive=exhaustive))
            print(f"[{case}/{total_cases}] L={L}, BAW={BAW}")

    with output_path.open("w", newline="", encoding="utf-8") as fout:
        writer = csv.DictWriter(fout, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)

    latex_filename = (
        "scalability-exhaustive.tex"
        if exhaustive
        else "scalability.tex"
    )
    write_latex_table(
        rows,
        output_path.parent / latex_filename,
        scan.trials_per_case,
        exhaustive=exhaustive,
    )

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
    parser.add_argument(
        "--exhaustive",
        action="store_true",
        help=(
            "fully enumerate every trial; defaults to L=2,3 unless "
            "--links is specified"
        ),
    )
    parser.add_argument(
        "--links",
        nargs="+",
        type=int,
        help=(
            "link counts to evaluate; defaults to 2,...,8 normally and "
            "to 2,3 with --exhaustive"
        ),
    )
    args = parser.parse_args()

    if args.trials <= 0:
        parser.error("--trials must be positive")
    if args.links and any(value <= 0 for value in args.links):
        parser.error("--links values must be positive")

    if args.links:
        link_values = tuple(dict.fromkeys(args.links))
    elif args.exhaustive:
        link_values = (2, 3)
    else:
        link_values = ScalingScanConfig().L_values

    output_path = args.output
    if args.exhaustive and not output_path.stem.endswith("-exhaustive"):
        output_path = output_path.with_name(
            f"{output_path.stem}-exhaustive{output_path.suffix}"
        )

    scan = ScalingScanConfig(
        L_values=link_values,
        trials_per_case=args.trials,
    )
    return run_experiment(
        scan,
        output_path,
        exhaustive=args.exhaustive,
    )


if __name__ == "__main__":
    raise SystemExit(main())
