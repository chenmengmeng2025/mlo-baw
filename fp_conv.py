"""Multi-start convergence experiment for the paper's fixed-point solver.

This script provides numerical evidence for Comment 1. Mathematical
uniqueness is established separately in the cited analytical proof; this
experiment tests whether the implemented damped fixed-point iteration
converges reliably over a declared finite parameter domain.

For every parameter configuration, the solver is started from a grid of
initial points in (0,1)^2. The script records convergence, iteration count,
fixed-point residual and agreement among initial points. Results should be described as "all runs
converged within the tested domain", not as a proof of global convergence.
"""

from __future__ import annotations

import argparse
import csv
import importlib.util
import itertools
import math
import sys
import time
from dataclasses import dataclass
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
SOLVER_PATH = SCRIPT_DIR / "solve.py"


def load_solver():
    """Load solve.py despite the hyphen in its filename."""
    spec = importlib.util.spec_from_file_location("solve_simp", SOLVER_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Cannot load solver: {SOLVER_PATH}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


solver = load_solver()


@dataclass(frozen=True)
class ExperimentGrid:
    N_values: tuple[int, ...]
    # MLD and SLD use independent contention-parameter grids. Their values
    # need not be equal; run_experiment() evaluates their Cartesian product.
    W_mld_values: tuple[float, ...]
    W_sld_values: tuple[float, ...]
    K_mld_values: tuple[int, ...]
    K_sld_values: tuple[int, ...]
    damping_values: tuple[float, ...]
    initial_values: tuple[float, ...]


def make_grid() -> ExperimentGrid:
    """Return the finite domain stated in Repl. 1.2."""
    return ExperimentGrid(N_values=tuple(range(1,65)), W_mld_values=(4.0,8.0,16.0), W_sld_values=(4.0,8.0,16.0), K_mld_values=(1,3,6), K_sld_values=(1,3,6), damping_values=(0.4,), initial_values=(0.1,0.3,0.5,0.7,0.9))


def raw_map(pM, pS, N, K_mld, K_sld, W_mld, W_sld):
    """Undamped right-hand side of the paper's coupled equations."""
    qS = solver.calc_p_part(pS, W_sld, K_sld)
    qM = solver.calc_p_part(pM, W_mld, K_mld)
    gM = max(0.0, 1.0 - qS) ** N
    gS = (1.0 - qM) * max(0.0, 1.0 - qS) ** (N - 1)
    return gM, gS


def fixed_point_residual(pM, pS, N, K_mld, K_sld, W_mld, W_sld):
    """Infinity-norm residual of the original, undamped fixed-point system."""
    gM, gS = raw_map(pM, pS, N, K_mld, K_sld, W_mld, W_sld)
    return max(abs(gM - pM), abs(gS - pS))


def run_experiment(grid, output_dir, max_iter, tol):
    output_dir.mkdir(parents=True, exist_ok=True)
    csv_path = output_dir / "fixed-point-convergence.csv"
    summary_path = output_dir / "fixed-point-summary.txt"

    configurations = list(itertools.product(
        grid.N_values,
        grid.W_mld_values,
        grid.W_sld_values,
        grid.K_mld_values,
        grid.K_sld_values,
        grid.damping_values,
    ))
    starts = list(itertools.product(grid.initial_values, repeat=2))
    total_runs = len(configurations) * len(starts)

    total_converged = 0
    maximum_iterations = 0
    maximum_residual = 0.0
    maximum_solution_spread = 0.0
    nonfinite_runs = 0
    start_time = time.perf_counter()

    header = [
        "N", "W_mld", "W_sld", "K_mld", "K_sld", "damping",
        "starts", "converged_runs", "all_converged", "max_iterations",
        "max_residual", "solution_spread_inf",
    ]

    with csv_path.open("w", newline="", encoding="utf-8") as fout:
        writer = csv.writer(fout)
        writer.writerow(header)

        for index, config in enumerate(configurations, start=1):
            N, Wm, Ws, Km, Ks, damping = config
            solutions = []
            iterations = []
            residuals = []
            converged_count = 0

            for initial_pM, initial_pS in starts:
                pM, pS, count, converged = solver.solve_p(
                    N=N,
                    K_mld=Km,
                    K_sld=Ks,
                    W_mld=Wm,
                    W_sld=Ws,
                    max_iter=max_iter,
                    tol=tol,
                    damping=damping,
                    initial_pM=initial_pM,
                    initial_pS=initial_pS,
                )
                finite = math.isfinite(pM) and math.isfinite(pS)
                if not finite:
                    nonfinite_runs += 1
                residual = (
                    fixed_point_residual(pM, pS, N, Km, Ks, Wm, Ws)
                    if finite else float("inf")
                )
                accepted = converged and finite and residual <= max(10.0 * tol, 1e-10)
                converged_count += int(accepted)
                total_converged += int(accepted)
                solutions.append((pM, pS))
                iterations.append(count)
                residuals.append(residual)

            spread = max(
                max(p[0] for p in solutions) - min(p[0] for p in solutions),
                max(p[1] for p in solutions) - min(p[1] for p in solutions),
            )
            config_max_iterations = max(iterations)
            config_max_residual = max(residuals)
            maximum_iterations = max(maximum_iterations, config_max_iterations)
            maximum_residual = max(maximum_residual, config_max_residual)
            maximum_solution_spread = max(maximum_solution_spread, spread)

            writer.writerow([
                N, Wm, Ws, Km, Ks, damping, len(starts), converged_count,
                converged_count == len(starts), config_max_iterations,
                config_max_residual, spread,
            ])

            if index == 1 or index == len(configurations) or index % 100 == 0:
                print(f"[{index}/{len(configurations)}] configurations completed")

    elapsed = time.perf_counter() - start_time
    all_converged = total_converged == total_runs
    summary = (
        "Fixed-point multi-start convergence experiment\n"
        "================================================\n"
        f"Configurations: {len(configurations)}\n"
        f"W_mld values: {grid.W_mld_values}\n"
        f"W_sld values: {grid.W_sld_values}\n"
        f"K_mld values: {grid.K_mld_values}\n"
        f"K_sld values: {grid.K_sld_values}\n"
        f"Initial points per configuration: {len(starts)}\n"
        f"Total solver runs: {total_runs}\n"
        f"Accepted converged runs: {total_converged}\n"
        f"All runs converged in tested domain: {all_converged}\n"
        f"Maximum iteration count: {maximum_iterations}\n"
        f"Maximum fixed-point residual: {maximum_residual:.6e}\n"
        f"Maximum multi-start solution spread: {maximum_solution_spread:.6e}\n"
        f"Tolerance: {tol:.3e}\n"
        f"Maximum allowed iterations: {max_iter}\n"
        f"Elapsed experiment time: {elapsed:.3f} s\n"
    )
    summary_path.write_text(summary, encoding="utf-8")
    print(summary)
    print(f"Detailed results: {csv_path}")
    print(f"Summary: {summary_path}")
    return 0 if all_converged else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--max-iter", type=int, default=2000)
    parser.add_argument("--tol", type=float, default=1e-12)
    parser.add_argument(
        "--output-dir", type=Path,
        default=SCRIPT_DIR / "outputs",
    )
    args = parser.parse_args()
    return run_experiment(
        make_grid(), args.output_dir, args.max_iter, args.tol
    )


if __name__ == "__main__":
    raise SystemExit(main())
