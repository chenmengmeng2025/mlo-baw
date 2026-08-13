import subprocess
import sys
import re
import os
from datetime import datetime
import argparse
import csv
import shlex
from pathlib import Path

def generate_log_filename(cmd):
    """Build a log name and derive its scenario directory from the executable."""

    def get_value(name, default, value_type):
        match = re.search(rf"--{name}=([^\s]+)", cmd)
        return value_type(match.group(1)) if match else default

    def get_bool(name, default):
        match = re.search(rf"--{name}=([^\s]+)", cmd)
        if not match:
            return default
        return match.group(1).lower() in {"1", "true", "yes"}

    def has_value(name):
        return re.search(rf"--{name}=([^\s]+)", cmd) is not None

    def format_number(value):
        return format(value, "g") if isinstance(value, float) else str(value)

    def get_scenario_folder(scenario):
        executable = Path(shlex.split(cmd)[0])
        executable_parts = executable.parts
        if "scratch" in executable_parts:
            scratch_index = executable_parts.index("scratch")
            source_directory = Path(*executable_parts[scratch_index:-1])
        else:
            source_directory = Path("scratch") / executable.parent.name
        return str(source_directory / scenario)

    def append_optional_title_fields(title):
        optional_fields = (
            ("period", "dt", float),
            ("logsender", "ls", bool),
            ("logreceiver", "lr", bool),
            ("distributedSender", "ds", bool),
            ("distributedReceiver", "dr", bool),
            ("ampduLimit", "al", bool),
        )
        for name, short_name, value_type in optional_fields:
            if not has_value(name):
                continue
            value = (
                int(get_bool(name, False))
                if value_type is bool
                else format_number(get_value(name, 0.0, value_type))
            )
            title += f"-{short_name}{value}"
        return title

    if "mlo-3link" in cmd or "--nsld2=" in cmd:
        policy_id = get_value("policy", 6, int)
        policy = {
            1: "greedy",
            3: "only2G",
            4: "only5G",
            5: "only6G",
            6: "allset",
        }.get(policy_id, "unknown")
        baw_size = get_value("bawsize", 1024, int)
        bw = [get_value(f"bw{i}", default, int) for i, default in zip((1, 2, 3), (20, 80, 160))]
        mcs = [get_value(f"mcs{i}", 10, int) for i in (1, 2, 3)]
        nsld = [get_value(f"nsld{i}", 1, int) for i in range(3)]
        sld_ampdu = [
            get_value(f"ampdunumsld{i}", 1, int) if nsld[i] else 0
            for i in range(3)
        ]
        nss = get_value("nss", 2, int)
        seed = get_value("seed", 1, int)
        scenario = get_value("scenario", "default", str)
        title = (
            f"{policy}-w{baw_size}-bw{'x'.join(map(str, bw))}"
            f"-m{'x'.join(map(str, mcs))}-nss{nss}"
            f"-sld{'x'.join(map(str, nsld))}"
            f"-sa{'x'.join(map(str, sld_ampdu))}"
        )
        if policy_id == 6:
            allocation = [
                get_value(f"maxampdunum{i}", 0, int) for i in range(3)
            ]
            title += f"-ma{'x'.join(map(str, allocation))}"
        title = append_optional_title_fields(f"{title}-s{seed}")
        folder_name = get_scenario_folder(scenario)
        return folder_name, f"{title}.log"

    policy_id = get_value("policy", 6, int)
    policy = {
        1: "greedy",
        2: "damla",
        3: "only2G",
        4: "only5G",
        6: "bothset",
    }.get(policy_id, "unknown")

    baw_size = get_value("bawsize", 512, int)
    bw0 = get_value("bw0", 20, int)
    bw1 = get_value("bw1", 80, int)
    mcs0 = get_value("mcs0", 13, int)
    mcs1 = get_value("mcs1", 10, int)
    nss = get_value("nss", 2, int)
    nsld0 = get_value("nsld0", 1, int)
    nsld1 = get_value("nsld1", 1, int)
    sld_ampdu0 = get_value("ampdunumsld0", 1, int) if nsld0 else 0
    sld_ampdu1 = get_value("ampdunumsld1", 1, int) if nsld1 else 0
    fixed_per0 = get_value("fixedPER0", 0.0, float)
    fixed_per1 = get_value("fixedPER1", 0.0, float)
    max_ampdu0 = get_value("maxampdunum0", 10, int)
    max_ampdu1 = get_value("maxampdunum1", 10, int)
    sim_time = get_value("simt", 5.5, float)
    seed = get_value("seed", 1, int)
    scenario = get_value("scenario", "default", str)

    title = (
        f"{policy}-w{baw_size}-bw{bw0}x{bw1}-m{mcs0}x{mcs1}"
        f"-nss{nss}-sld{nsld0}x{nsld1}-sa{sld_ampdu0}x{sld_ampdu1}"
        f"-per{format_number(fixed_per0)}x{format_number(fixed_per1)}"
    )
    if policy_id == 6:
        title += f"-ma{max_ampdu0}x{max_ampdu1}"

    title += f"-t{format_number(sim_time)}-s{seed}"
    title = append_optional_title_fields(title)

    folder_name = get_scenario_folder(scenario)
    return folder_name, f"{title}.log"


def run_command(cmd, enable_log=True):
    """Run one command and wait for it to finish.

    :param cmd: command string to execute
    :param enable_log:
        - True: save command output to the corresponding .log file
        - False: discard command output without creating or printing a log
    """
    # Remove any existing shell redirection from the input command.
    base_cmd = cmd.split("&>")[0].strip() if "&>" in cmd else cmd

    print(f"Executing command: {base_cmd}")

    if enable_log:
        # Generate the log directory and filename.
        folder_name, log_filename = generate_log_filename(base_cmd)

        if not os.path.exists(folder_name):
            os.makedirs(folder_name)
            print(f"Created directory: {folder_name}")

        log_file_path = os.path.join(folder_name, log_filename)
        print(f"Log file: {log_file_path}")
        final_cmd = f"{base_cmd} &> {log_file_path}"
    else:
        # Preserve all command arguments and discard stdout and stderr.
        print("Log file: disabled")
        final_cmd = f"{base_cmd} &> /dev/null"

    print("-" * 50)

    try:
        # Run synchronously and wait for the command to finish.
        return_code = subprocess.call(final_cmd, shell=True, executable="/bin/bash")

        if return_code != 0:
            print(f"Command failed with exit code {return_code}")
            sys.exit(1)

    except Exception as e:
        print(f"Error while executing command: {e}")
        sys.exit(1)

# ===== Default command settings =====
# Logging policy: True saves log files; False disables log creation.
DEFAULT_COMMAND_SETTINGS = {
    2: {
        "executable": "build/scratch/mlo-2link/ns3.43-link2-optimized",
        "policies": [6, 2, 4, 1],
        "simt": 16.5,
        "seed": 1,
        "scenario": "2link-sim",
    },
    3: {
        "executable": "build/scratch/mlo-3link/ns3.43-link3-optimized",
        "policies": [6, 5, 1],
        "simt": 21.5,
        "seed": 1,
        "scenario": "3link-sim",
    },
}


def _csv_int(value, field, row_number):
    """Accept integer fields formatted as either 2 or 2.000000."""
    try:
        number = float(value)
    except (TypeError, ValueError) as error:
        raise ValueError(
            f"row {row_number}: {field} must be an integer"
        ) from error
    if not number.is_integer():
        raise ValueError(f"row {row_number}: {field} must be an integer")
    return int(number)


def _csv_float(value, field, row_number):
    try:
        return float(value)
    except (TypeError, ValueError) as error:
        raise ValueError(
            f"row {row_number}: {field} must be numeric"
        ) from error


def _compact_number(value):
    return format(value, "g") if isinstance(value, float) else str(value)


def _parse_policies(value):
    try:
        policies = [
            int(item.strip()) for item in value.split(",") if item.strip()
        ]
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "policies must be comma-separated integers"
        ) from error
    if not policies:
        raise argparse.ArgumentTypeError("at least one policy is required")
    return policies


def _parse_binary_option(value):
    if value not in {"0", "1"}:
        raise argparse.ArgumentTypeError("value must be 0 or 1")
    return int(value)


def _read_solve_csv(csv_path):
    with csv_path.open(newline="", encoding="utf-8-sig") as input_file:
        reader = csv.reader(input_file)
        try:
            header = [field.strip() for field in next(reader)]
        except StopIteration as error:
            raise ValueError(f"empty CSV file: {csv_path}") from error

        link_count = 0
        while f"bw{link_count}" in header:
            link_count += 1
        if link_count not in DEFAULT_COMMAND_SETTINGS:
            raise ValueError(
                "expected a current 2-link or 3-link solve.py CSV "
                "containing bw0, bw1, ... columns"
            )

        required = ["nss", "bawsize"]
        for link in range(link_count):
            required.extend(
                [
                    f"bw{link}",
                    f"mcs{link}",
                    f"nsld{link}",
                    f"ampdunumsld{link}",
                    f"fixedPER{link}",
                    f"maxampdunum{link}",
                ]
            )
        missing = [field for field in required if field not in header]
        if missing:
            raise ValueError("missing CSV columns: " + ", ".join(missing))

        rows = []
        for row_number, values in enumerate(reader, start=2):
            if not values or all(not value.strip() for value in values):
                continue
            if len(values) != len(header):
                raise ValueError(
                    f"row {row_number}: expected {len(header)} values, "
                    f"found {len(values)}"
                )
            rows.append(
                {
                    field: value.strip()
                    for field, value in zip(header, values)
                }
            )

    if not rows:
        raise ValueError(f"CSV contains no result rows: {csv_path}")
    return rows, link_count


def _command_from_solve_row(row, row_number, link_count, settings, policy):
    parts = [settings["executable"]]

    for link in range(link_count):
        field = f"nsld{link}"
        value = _csv_int(row[field], field, row_number)
        parts.append(f"--{field}={value}")

    parts.extend(
        [
            f"--simt={_compact_number(settings['simt'])}",
            f"--bawsize={_csv_int(row['bawsize'], 'bawsize', row_number)}",
            f"--policy={policy}",
        ]
    )

    # link2.cc uses bw0/mcs0; link3.cc starts at bw1/mcs1.
    phy_offset = 0 if link_count == 2 else 1
    for link in range(link_count):
        field = f"bw{link}"
        value = _csv_int(row[field], field, row_number)
        parts.append(f"--bw{link + phy_offset}={value}")
    for link in range(link_count):
        field = f"mcs{link}"
        value = _csv_int(row[field], field, row_number)
        parts.append(f"--mcs{link + phy_offset}={value}")

    parts.append(
        f"--nss={_csv_int(row['nss'], 'nss', row_number)}"
    )

    for link in range(link_count):
        field = f"ampdunumsld{link}"
        value = _csv_int(row[field], field, row_number)
        parts.append(f"--{field}={value}")

    # Only policy 6 uses solve.py's optimal MLD allocation.
    if policy == 6:
        for link in range(link_count):
            field = f"maxampdunum{link}"
            value = _csv_int(row[field], field, row_number)
            parts.append(f"--{field}={value}")

    for link in range(link_count):
        field = f"fixedPER{link}"
        value = _csv_float(row[field], field, row_number)
        parts.append(f"--{field}={_compact_number(value)}")

    parts.extend(
        [
            f"--seed={settings['seed']}",
            f"--scenario={settings['scenario']}",
        ]
    )
    optional_arguments = (
        ("period", "period"),
        ("logsender", "logsender"),
        ("logreceiver", "logreceiver"),
        ("distributed_sender", "distributedSender"),
        ("distributed_receiver", "distributedReceiver"),
        ("ampdu_limit", "ampduLimit"),
    )
    for setting_name, argument_name in optional_arguments:
        if setting_name in settings:
            value = _compact_number(settings[setting_name])
            parts.append(f"--{argument_name}={value}")
    return shlex.join(parts)


def generate_commands_from_csv(
    csv_path,
    policies=None,
    executable=None,
    simt=None,
    seed=None,
    scenario=None,
    period=None,
    logsender=None,
    logreceiver=None,
    distributed_sender=None,
    distributed_receiver=None,
    ampdu_limit=None,
    start_row=1,
    limit=None,
):
    rows, link_count = _read_solve_csv(csv_path)
    settings = dict(DEFAULT_COMMAND_SETTINGS[link_count])
    if policies is not None:
        settings["policies"] = policies
    if executable is not None:
        settings["executable"] = executable
    if simt is not None:
        settings["simt"] = simt
    if seed is not None:
        settings["seed"] = seed
    if scenario is not None:
        settings["scenario"] = scenario
    optional_overrides = {
        "period": period,
        "logsender": logsender,
        "logreceiver": logreceiver,
        "distributed_sender": distributed_sender,
        "distributed_receiver": distributed_receiver,
        "ampdu_limit": ampdu_limit,
    }
    for name, value in optional_overrides.items():
        if value is not None:
            settings[name] = value

    if start_row < 1:
        raise ValueError("start-row must be at least 1")
    selected = rows[start_row - 1:]
    if limit is not None:
        if limit < 1:
            raise ValueError("limit must be at least 1")
        selected = selected[:limit]
    if not selected:
        raise ValueError("the selected CSV row range is empty")

    generated = []
    for result_index, row in enumerate(selected, start=start_row):
        csv_row_number = result_index + 1
        for policy in settings["policies"]:
            generated.append(
                _command_from_solve_row(
                    row,
                    csv_row_number,
                    link_count,
                    settings,
                    policy,
                )
            )
    return generated, link_count, len(selected)


def build_argument_parser():
    parser = argparse.ArgumentParser(
        description="Generate ns-3 commands from a solve.py result CSV."
    )
    parser.add_argument("csv_path", type=Path, help="solve.py output CSV")
    parser.add_argument(
        "--policies",
        type=_parse_policies,
        help="defaults: 6,2,4,1 for 2 links; 6,5,1 for 3 links",
    )
    parser.add_argument("--executable")
    parser.add_argument("--simt", type=float)
    parser.add_argument("--seed", type=int)
    parser.add_argument("--scenario")
    parser.add_argument("--period", type=float)
    parser.add_argument("--logsender", type=_parse_binary_option)
    parser.add_argument("--logreceiver", type=_parse_binary_option)
    parser.add_argument(
        "--distributedSender",
        dest="distributed_sender",
        type=_parse_binary_option,
    )
    parser.add_argument(
        "--distributedReceiver",
        dest="distributed_receiver",
        type=_parse_binary_option,
    )
    parser.add_argument(
        "--ampduLimit",
        dest="ampdu_limit",
        type=_parse_binary_option,
    )
    parser.add_argument("--start-row", type=int, default=1)
    parser.add_argument("--limit", type=int)
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="print generated commands without running simulations",
    )
    parser.add_argument(
        "--no-log",
        action="store_true",
        help="discard simulator output instead of writing logs",
    )
    return parser


ENABLE_LOG = True


def main():
    args = build_argument_parser().parse_args()
    try:
        commands, link_count, row_count = generate_commands_from_csv(
            args.csv_path,
            policies=args.policies,
            executable=args.executable,
            simt=args.simt,
            seed=args.seed,
            scenario=args.scenario,
            period=args.period,
            logsender=args.logsender,
            logreceiver=args.logreceiver,
            distributed_sender=args.distributed_sender,
            distributed_receiver=args.distributed_receiver,
            ampdu_limit=args.ampdu_limit,
            start_row=args.start_row,
            limit=args.limit,
        )
    except (OSError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 2

    print(
        f"Read {row_count} {link_count}-link result rows; "
        f"generated {len(commands)} commands."
    )
    if args.dry_run:
        for command in commands:
            print(command)
        return 0

    for index, command in enumerate(commands, start=1):
        print(f"\n[{index}/{len(commands)}]")
        run_command(
            command,
            enable_log=ENABLE_LOG and not args.no_log,
        )

    print("\nAll commands completed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
