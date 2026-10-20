#!/usr/bin/env python3
"""Run the same stress cases in ArpScript and Python.

Usage:
    python tests/bench/scriptvm_stress_benchmark.py \
        build/win-vulkan-release/arp_scriptvm_stress_benchmark.exe --scale 1
    python tests/bench/scriptvm_stress_benchmark.py \
        build/win-vulkan-release/arp_scriptvm_stress_benchmark.exe --quick
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import time
import statistics
import math
from dataclasses import dataclass
from pathlib import Path
from scriptvm_python_corpus import PythonCorpus

@dataclass(frozen=True)
class Result:
    iterations: int
    interpreter_checksum: int
    interpreter_milliseconds: float
    aot_checksum: int
    aot_milliseconds: float


@dataclass(frozen=True)
class PythonResult:
    iterations: int
    checksum: int
    milliseconds: float


# Use identical loop/call shapes, including the twelve extended workloads.
# This replaces the older for-loop and nested-function Python approximations.
_corpus = PythonCorpus()
_corpus.initialize(_corpus.compile())
CASE_FUNCTIONS = _corpus.functions
CASE_FUNCTIONS = {name: function for name, function in CASE_FUNCTIONS.items()
                  if name != "generator_sum"}


def run_python(name: str, iterations: int) -> PythonResult:
    function = CASE_FUNCTIONS[name]
    function(1)
    start = time.perf_counter()
    checksum = function(iterations)
    milliseconds = (time.perf_counter() - start) * 1000.0
    return PythonResult(iterations, checksum, milliseconds)


def run_arpscript_once(
    executable: Path, scale: int, quick: bool, node: Path | None
) -> dict[str, Result]:
    benchmark_args = ["--quick"] if quick else ["--scale", str(scale)]
    command = [str(node), str(executable)] if node is not None else [str(executable)]
    command.extend(benchmark_args)
    completed = subprocess.run(
        command,
        check=True,
        text=True,
        capture_output=True,
    )
    results: dict[str, Result] = {}
    for line in completed.stdout.splitlines():
        fields = line.split()
        if len(fields) != 7 or fields[0] != "RESULT":
            continue
        _, name, iterations, interpreter_checksum, interpreter_ms, aot_checksum, aot_ms = fields
        results[name] = Result(
            int(iterations),
            int(interpreter_checksum),
            float(interpreter_ms),
            int(aot_checksum),
            float(aot_ms),
        )
    if set(results) != set(CASE_FUNCTIONS):
        missing = sorted(set(CASE_FUNCTIONS) - set(results))
        raise RuntimeError(f"ArpScript benchmark did not report cases: {missing}")
    return results


def run_arpscript(
    executable: Path, scale: int, quick: bool, repeat: int, node: Path | None
) -> dict[str, Result]:
    samples = [
        run_arpscript_once(executable, scale, quick, node)
        for _ in range(repeat)
    ]
    for name in CASE_FUNCTIONS:
        checksums = {
            (sample[name].interpreter_checksum, sample[name].aot_checksum)
            for sample in samples
        }
        if len(checksums) != 1:
            raise RuntimeError(f"checksum changed between benchmark runs: {name}")
    return {
        name: Result(
            samples[0][name].iterations,
            samples[0][name].interpreter_checksum,
            statistics.median(sample[name].interpreter_milliseconds for sample in samples),
            samples[0][name].aot_checksum,
            statistics.median(sample[name].aot_milliseconds for sample in samples),
        )
        for name in CASE_FUNCTIONS
    }


def enforce_performance_gate(results: dict[str, Result]) -> None:
    critical = {"int_loop", "function_calls", "recursive_fib", "branch_mix"}
    original_gate_cases = {"int_loop", "function_calls", "recursive_fib", "list_index",
        "list_build", "dict_index", "string_concat", "range_sum", "branch_mix",
        "power_floor", "list_mutate", "exception_loop"}
    failures = []
    for name, result in results.items():
        if name not in original_gate_cases:
            continue
        if not all(math.isfinite(value) and value > 0.0 for value in
                (result.aot_milliseconds, result.interpreter_milliseconds)):
            failures.append(
                f"{name}: invalid steady-state timing"
            )
            continue
        # Only proven native scalar lowering has an AOT speed contract here.
        # Value-frame AOT paths may legitimately trail inline Interpreter slots;
        # improving Interpreter must not turn unchanged AOT into a regression.
        if name in critical and (
            result.aot_milliseconds <= 0.0
            or result.interpreter_milliseconds / result.aot_milliseconds < 2.0
        ):
            ratio = (
                result.interpreter_milliseconds / result.aot_milliseconds
                if result.aot_milliseconds > 0.0 else 0.0
            )
            failures.append(f"{name}: scalar speedup {ratio:.3f}x is below 2x")
    if failures:
        raise RuntimeError("AOT stress performance gate failed:\n" + "\n".join(failures))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    parser.add_argument("--scale", type=int, default=1)
    parser.add_argument("--quick", action="store_true")
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument("--gate", action="store_true")
    parser.add_argument("--node", type=Path)
    args = parser.parse_args()
    if not args.quick and (args.scale < 1 or args.scale > 1000):
        parser.error("scale must be in [1, 1000]")
    if not args.executable.is_file():
        parser.error(f"benchmark executable does not exist: {args.executable}")
    if not 1 <= args.repeat <= 20:
        parser.error("repeat must be in [1, 20]")

    arpscript = run_arpscript(
        args.executable, args.scale, args.quick, args.repeat, args.node
    )
    print("case             iterations Interpreter ms      AOT ms Python ms Int/Python AOT/Python")
    print("---------------- ----------- --------------- ---------- --------- ---------- ----------")
    for name in CASE_FUNCTIONS:
        arp = arpscript[name]
        python_samples = [run_python(name, arp.iterations) for _ in range(args.repeat)]
        if len({sample.checksum for sample in python_samples}) != 1:
            raise RuntimeError(f"Python checksum changed between runs: {name}")
        python = PythonResult(arp.iterations, python_samples[0].checksum,
            statistics.median(sample.milliseconds for sample in python_samples))
        if arp.interpreter_checksum != python.checksum:
            raise RuntimeError(
                f"checksum mismatch for {name}: "
                f"Interpreter={arp.interpreter_checksum}, Python={python.checksum}"
            )
        if arp.aot_checksum != python.checksum:
            raise RuntimeError(
                f"AOT checksum mismatch for {name}: "
                f"AOT={arp.aot_checksum}, Python={python.checksum}"
            )
        interpreter_ratio = (
            arp.interpreter_milliseconds / python.milliseconds
            if python.milliseconds else float("inf")
        )
        aot_ratio = (
            arp.aot_milliseconds / python.milliseconds
            if python.milliseconds else float("inf")
        )
        print(
            f"{name:<16} {arp.iterations:>11d} "
            f"{arp.interpreter_milliseconds:>10.3f} {arp.aot_milliseconds:>10.3f} "
            f"{python.milliseconds:>10.3f} "
            f"{interpreter_ratio:>8.2f}x {aot_ratio:>8.2f}x"
        )
    if args.gate:
        enforce_performance_gate(arpscript)
        print("\nAOT scalar lowering performance gate passed.")
    print("\nAll checksums match.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as error:
        print(error.stderr, file=sys.stderr, end="")
        raise
