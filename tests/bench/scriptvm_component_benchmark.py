#!/usr/bin/env python3
"""Run and summarize the ScriptVM component benchmark.

Each executable is run several times. The report uses the median elapsed time
and allocation count, while preserving checksums as a correctness guard.
"""

from __future__ import annotations

import argparse
import json
import statistics
import subprocess
from pathlib import Path


COMPONENTS = (
    "vm_dispatch",
    "aot_dispatch",
    "semantic_helper",
    "reflection_bridge",
    "allocation",
    "host_call",
)


def load_budget(
    path: Path, iterations: int, profile: str
) -> dict[str, object]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(f"could not read component budget {path}: {error}") from error
    if not isinstance(document, dict):
        raise RuntimeError("component budget root must be an object")
    if document.get("iterations") != iterations:
        raise RuntimeError(
            f"component budget iterations {document.get('iterations')} "
            f"do not match {iterations}"
        )
    profiles = document.get("profiles")
    if not isinstance(profiles, dict):
        raise RuntimeError("component budget must contain profiles")
    selected = profiles.get(profile)
    if not isinstance(selected, dict):
        raise RuntimeError(f"component budget has no profile: {profile}")
    components = selected.get("components")
    if not isinstance(components, dict) or set(components) != set(COMPONENTS):
        raise RuntimeError("component budget does not cover the benchmark components")
    budget_components: dict[str, dict[str, int]] = {}
    for component in COMPONENTS:
        limits = components[component]
        if not isinstance(limits, dict):
            raise RuntimeError(f"component budget entry is not an object: {component}")
        try:
            allocations = int(limits["max_allocations"])
            live_bytes = int(limits["max_live_bytes_delta"])
        except (KeyError, TypeError, ValueError) as error:
            raise RuntimeError(f"component budget entry is incomplete: {component}") from error
        if allocations < 0 or live_bytes < 0:
            raise RuntimeError(f"component budget entry is negative: {component}")
        budget_components[component] = {
            "max_allocations": allocations,
            "max_live_bytes_delta": live_bytes,
        }
    budget = dict(selected)
    budget["components"] = budget_components
    return budget


def check_budget(
    executable: Path,
    results: dict[str, tuple[int, float, int, int]],
    profile: dict[str, object],
    check_timing: bool,
) -> None:
    components = profile["components"]
    assert isinstance(components, dict)
    timing = profile.get("timing")
    if check_timing and not isinstance(timing, dict):
        raise RuntimeError("selected budget profile has no timing limits")
    for component in COMPONENTS:
        _, milliseconds, allocations, live_bytes_delta = results[component]
        limits = components[component]
        assert isinstance(limits, dict)
        if allocations > limits["max_allocations"]:
            raise RuntimeError(
                f"{executable} {component}: allocations {allocations} exceed "
                f"budget {limits['max_allocations']}"
            )
        if live_bytes_delta > limits["max_live_bytes_delta"]:
            raise RuntimeError(
                f"{executable} {component}: live bytes delta {live_bytes_delta} exceed "
                f"budget {limits['max_live_bytes_delta']}"
            )
        if check_timing:
            assert isinstance(timing, dict)
            timing_limits = timing.get(component)
            if not isinstance(timing_limits, dict):
                raise RuntimeError(f"timing budget does not cover component: {component}")
            max_milliseconds = float(timing_limits["max_milliseconds"])
            if milliseconds > max_milliseconds:
                raise RuntimeError(
                    f"{executable} {component}: {milliseconds:.3f} ms exceed "
                    f"timing budget {max_milliseconds:.3f} ms"
                )


def run_once(
    executable: Path, iterations: int, expected_build_profile: str | None
) -> tuple[str, dict[str, tuple[int, float, int, int]]]:
    completed = subprocess.run(
        [str(executable), str(iterations)],
        check=True,
        text=True,
        capture_output=True,
    )
    build_profile = None
    results: dict[str, tuple[int, float, int, int]] = {}
    for line in completed.stdout.splitlines():
        profile_fields = line.split()
        if len(profile_fields) == 2 and profile_fields[0] == "PROFILE":
            if build_profile is not None:
                raise RuntimeError("benchmark reported multiple build profiles")
            build_profile = profile_fields[1]
            continue
        fields = line.split()
        if len(fields) != 7 or fields[0] != "RESULT":
            continue
        _, name, reported_iterations, checksum, milliseconds, allocations, live_delta = fields
        results[name] = (
            int(checksum),
            float(milliseconds),
            int(allocations),
            int(live_delta),
        )
        if int(reported_iterations) != iterations:
            raise RuntimeError(f"{name}: iteration count mismatch")
    if set(results) != set(COMPONENTS):
        missing = sorted(set(COMPONENTS) - set(results))
        raise RuntimeError(f"benchmark did not report components: {missing}")
    if build_profile is None:
        raise RuntimeError("benchmark did not report a build profile")
    if expected_build_profile and build_profile != expected_build_profile:
        raise RuntimeError(
            f"{executable}: build profile {build_profile!r} does not match "
            f"expected {expected_build_profile!r}"
        )
    return build_profile, results


def render(
    executable: Path,
    profile: str,
    runs: list[dict[str, tuple[int, float, int, int]]],
) -> str:
    lines = [
        f"## {executable} ({profile})",
        "",
        "| component | checksum | median ms | median allocations | median live bytes delta |",
        "|---|---:|---:|---:|---:|",
    ]
    for component in COMPONENTS:
        samples = [run[component] for run in runs]
        checksums = {sample[0] for sample in samples}
        if len(checksums) != 1:
            raise RuntimeError(f"{component}: checksum changed between runs")
        lines.append(
            f"| `{component}` | {samples[0][0]} | "
            f"{statistics.median(sample[1] for sample in samples):.3f} | "
            f"{statistics.median(sample[2] for sample in samples):.0f} | "
            f"{statistics.median(sample[3] for sample in samples):.0f} |"
        )
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("executables", type=Path, nargs="+")
    parser.add_argument("--iterations", type=int, default=100000)
    parser.add_argument("--repeat", type=int, default=3)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--budget", type=Path)
    parser.add_argument("--profile", default="resource")
    parser.add_argument("--expected-build-profile")
    parser.add_argument("--timing", action="store_true")
    args = parser.parse_args()
    if not 1 <= args.iterations <= 10_000_000:
        parser.error("iterations must be in [1, 10000000]")
    if not 1 <= args.repeat <= 20:
        parser.error("repeat must be in [1, 20]")
    budget = (
        load_budget(args.budget, args.iterations, args.profile)
        if args.budget
        else None
    )

    reports = []
    for executable in args.executables:
        if not executable.is_file():
            parser.error(f"benchmark executable does not exist: {executable}")
        measured = [
            run_once(executable, args.iterations, args.expected_build_profile)
            for _ in range(args.repeat)
        ]
        profiles = {profile for profile, _ in measured}
        if len(profiles) != 1:
            raise RuntimeError(f"{executable}: build profile changed between runs")
        profile = profiles.pop()
        runs = [results for _, results in measured]
        if budget is not None:
            for run in runs:
                check_budget(executable, run, budget, args.timing)
        reports.append(render(executable, profile, runs))
    output = "\n\n".join(reports) + "\n"
    if args.output:
        args.output.write_text(output, encoding="utf-8")
    print(output, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
