#!/usr/bin/env python3
"""Repeatable headless ArpScript v5 / bytecode / CPython benchmark matrix."""
from __future__ import annotations

import argparse
import csv
import ctypes
import hashlib
import json
import math
import platform
import random
import shlex
import statistics
import subprocess
import sys
import time
from dataclasses import asdict, dataclass
from datetime import datetime, timedelta, timezone
from pathlib import Path

from scriptvm_python_corpus import CASE_NAMES, SOURCE, PythonCorpus

MODULUS = 1_000_000_007
MODES = ("interpreter", "aot", "python")


def expected_checksum(name: str, n: int) -> int:
    """Independent arithmetic oracle; never executes the measured workload."""
    triangular = n * (n - 1) // 2
    def cycle(values):
        q, r = divmod(n, len(values))
        return q * sum(values) + sum(values[:r])
    q, r = divmod(n, 4)
    formulas = {
        "int_loop": triangular, "function_calls": triangular + n,
        "recursive_fib": 75025 * n, "list_index": cycle(range(1, 9)),
        "list_build": 2 * triangular + 3 * n,
        "dict_index": cycle((3, 5, 7, 11)), "string_concat": n,
        "range_sum": 3 * triangular + n,
        "power_floor": cycle((0, 0, 2, 4, 8)),
        "list_mutate": 2 * q * (q + 1) + r * (q + 1),
        "dict_mutate": 2 * q * (q + 1) + r * (q + 1),
        "exception_loop": (n + 6) // 7, "float_math": max(0, n - 1),
        "bitwise": cycle(tuple(i ^ 85 for i in range(256))),
        "nested_loop": cycle(range(32)) * 120,
        "dict_build": 2 * triangular + 3 * n,
        "dict_string": cycle((3, 5, 7, 11)),
        "string_compare": (n + 1) // 4, "string_build": 29 * n,
        "list_iterate": 36 * n, "object_method": triangular + 2 * n,
        "generator_sum": triangular, "closure_calls": triangular + 3 * n,
    }
    if name == "branch_mix":
        q, r = divmod(n, 3)
        return (9*q*(q-1) + 8*q + sum((3*q+i)*(i+1) for i in range(r))) % MODULUS
    value = formulas[name]
    # These cases deliberately do not apply the modular checksum in source.
    return value if name in {"string_concat", "exception_loop", "float_math", "string_compare"} else value % MODULUS


def peak_working_set() -> int | None:
    if sys.platform != "win32":
        return None
    class Counters(ctypes.Structure):
        _fields_ = [("cb", ctypes.c_ulong), ("PageFaultCount", ctypes.c_ulong)] + [
            (name, ctypes.c_size_t) for name in (
                "PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage",
                "QuotaPagedPoolUsage", "QuotaPeakNonPagedPoolUsage", "QuotaNonPagedPoolUsage",
                "PagefileUsage", "PeakPagefileUsage")]
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.GetCurrentProcess.restype = ctypes.c_void_p
    kernel.K32GetProcessMemoryInfo.argtypes = [ctypes.c_void_p, ctypes.POINTER(Counters), ctypes.c_ulong]
    counters = Counters()
    counters.cb = ctypes.sizeof(counters)
    if not kernel.K32GetProcessMemoryInfo(kernel.GetCurrentProcess(), ctypes.byref(counters), counters.cb):
        raise ctypes.WinError(ctypes.get_last_error())
    return counters.PeakWorkingSetSize


def python_worker(name: str, n: int, target_ms: int, memory: bool) -> int:
    corpus = PythonCorpus()
    start = time.perf_counter_ns()
    code = corpus.compile()
    compile_ms = (time.perf_counter_ns() - start) / 1e6
    start = time.perf_counter_ns()
    corpus.initialize(code)
    setup_ms = (time.perf_counter_ns() - start) / 1e6
    start = time.perf_counter_ns()
    checksum = corpus.invoke(name, n)
    cold_ms = (time.perf_counter_ns() - start) / 1e6
    batches = min(10000, max(1, int(target_ms / max(cold_ms, 1e-6)) + 1))
    start = time.perf_counter_ns()
    for _ in range(batches):
        warm_checksum = corpus.invoke(name, n)
        if warm_checksum != checksum:
            raise RuntimeError("Python first/warm checksum changed")
    execution_ms = (time.perf_counter_ns() - start) / 1e6 / batches
    peak = peak_working_set()
    trace_peak = None
    if memory:
        import tracemalloc
        tracemalloc.start()
        baseline, _ = tracemalloc.get_traced_memory()
        if corpus.invoke(name, n) != checksum:
            raise RuntimeError("instrumented checksum changed")
        _, peak_trace = tracemalloc.get_traced_memory()
        trace_peak = peak_trace - baseline
        tracemalloc.stop()
    print(json.dumps(dict(checksum=checksum, execution_ms=execution_ms, cold_ms=cold_ms,
        compile_ms=compile_ms, setup_ms=setup_ms, batches=batches,
        process_peak_working_set=peak, python_trace_peak_bytes=trace_peak)))
    return 0


@dataclass(frozen=True)
class Sample:
    case: str
    size: str
    iterations: int
    mode: str
    repeat: int
    checksum: int
    execution_ms: float
    cold_ms: float
    compile_ms: float
    setup_ms: float
    wall_ms: float
    batches: int
    allocations_per_call: float | None
    live_bytes_delta: int | None
    process_peak_working_set: int | None
    python_trace_peak_bytes: int | None = None


def parse_native(output: str, name: str, mode: str, n: int) -> dict:
    records = {}
    for line in output.splitlines():
        fields = line.split()
        if not fields:
            continue
        if fields[0] in {"PROFILE", "SETUP", "COLD", "SAMPLE", "BATCH", "PROCESS_PEAK_WORKING_SET"}:
            if fields[0] in records:
                raise ValueError(f"duplicate {fields[0]} record")
            records[fields[0]] = fields[1:]
    if records.get("PROFILE") != ["Release"]:
        raise ValueError("performance benchmark requires a reported Release profile")
    for key in ("SETUP", "COLD", "SAMPLE", "BATCH"):
        if key not in records:
            raise ValueError(f"missing {key} record")
    for key in ("COLD", "SAMPLE"):
        fields = records[key]
        if len(fields) != 7 or fields[:3] != [name, mode, str(n)]:
            raise ValueError(f"{key} case/mode/iterations mismatch")
    batches_record = records["BATCH"]
    if len(batches_record) != 3 or batches_record[:2] != [name, mode]:
        raise ValueError("BATCH identity mismatch")
    batches = int(batches_record[2])
    if not 1 <= batches <= 10000:
        raise ValueError("invalid batch count")
    cold, warm = records["COLD"], records["SAMPLE"]
    if int(cold[3]) != int(warm[3]):
        raise ValueError("first/warm checksum mismatch")
    values = dict(checksum=int(warm[3]), execution_ms=float(warm[4]), cold_ms=float(cold[4]),
        compile_ms=float(records["SETUP"][0]), setup_ms=float(records["SETUP"][1]),
        batches=batches, allocations_per_call=int(warm[5])/batches,
        live_bytes_delta=int(warm[6]),
        process_peak_working_set=int(records["PROCESS_PEAK_WORKING_SET"][0])
        if "PROCESS_PEAK_WORKING_SET" in records else None)
    validate_measurement(values)
    return values


def validate_measurement(values: dict):
    for key in ("execution_ms", "cold_ms", "compile_ms", "setup_ms"):
        if not math.isfinite(values[key]) or values[key] < 0 or (key == "execution_ms" and values[key] == 0):
            raise ValueError(f"invalid {key}")


class BenchmarkSuite:
    def __init__(self, executable: Path, python: Path, output: Path, repeats: int, target_ms: int):
        self.executable = executable.resolve()
        self.python = python.resolve()
        self.output = output
        self.repeats = repeats
        self.target_ms = target_ms
        self.samples: list[Sample] = []
        self.diagnostics: list[dict] = []

    def restore(self, environment: dict):
        # Read our explicit JSON-quoted YAML scalars without an extra dependency.
        saved={}
        for line in (self.output/"environment.yaml").read_text(encoding="utf-8").splitlines():
            key,separator,value=line.partition(": ")
            if separator and key!='type':
                saved[key]=json.loads(value)
        for key in ("executable_sha256","corpus_sha256","python_adapter_sha256","suite_sha256",
            "python_version","repetitions","target_ms","sizes","cases","input_counts"):
            if saved.get(key)!=environment.get(key):
                raise RuntimeError(f"resume metadata mismatch: {key}")
        with (self.output/"samples.csv").open(encoding="utf-8",newline="") as stream:
            for row in csv.DictReader(stream):
                for key in ("iterations","repeat","checksum","batches"):
                    row[key]=int(row[key])
                for key in ("execution_ms","cold_ms","compile_ms","setup_ms","wall_ms"):
                    row[key]=float(row[key])
                for key in ("allocations_per_call",):
                    row[key]=float(row[key]) if row[key] else None
                for key in ("live_bytes_delta","process_peak_working_set","python_trace_peak_bytes"):
                    row[key]=int(row[key]) if row[key] else None
                if row['iterations']!=iterations_for(row['case'],row['size']):
                    raise RuntimeError("resume workload input mismatch")
                if row['case'] not in environment['cases'] or row['size'] not in environment['sizes'] or not 0<=row['repeat']<self.repeats:
                    raise RuntimeError("resume sample outside requested matrix")
                validate_measurement(row)
                if row['checksum']!=expected_checksum(row['case'],row['iterations']):
                    raise RuntimeError("corrupt resumed checksum")
                self.samples.append(Sample(**row))
        identities=[(s.case,s.size,s.mode,s.repeat) for s in self.samples]
        if len(identities)!=len(set(identities)):
            raise RuntimeError("duplicate resumed sample")

    def write_environment(self, environment: dict):
        self.output.mkdir(parents=True,exist_ok=True)
        (self.output/"environment.yaml").write_text("type: arp.script.benchmark-result/v1\ncontractVersion: 1\n"+
            "\n".join(f"{key}: {json.dumps(value,ensure_ascii=False)}" for key,value in environment.items()
                if key not in {"type", "contractVersion"})+"\n",encoding="utf-8")

    def launch(self, command: list[str]) -> tuple[subprocess.CompletedProcess, float]:
        start = time.perf_counter_ns()
        result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", timeout=120)
        return result, (time.perf_counter_ns()-start)/1e6

    def run(self, sizes: list[str], names: list[str]):
        self.output.mkdir(parents=True, exist_ok=True)
        randomizer = random.Random(20261014)
        completed_keys={(s.case,s.size,s.mode,s.repeat) for s in self.samples}
        for name in names:
            for size in sizes:
                n = iterations_for(name, size)
                for repeat in range(self.repeats):
                    modes = list(MODES)
                    randomizer.shuffle(modes)
                    for mode in modes:
                        if mode == "aot" and name == "generator_sum":
                            if repeat == 0:
                                command = [str(self.executable), "--mode", mode, "--case", name, "--iterations", str(n)]
                                failed, _ = self.launch(command)
                                # A newly supported implementation must cause this contract to be updated.
                                if failed.returncode == 0 or "range over non-iterable" not in failed.stderr:
                                    raise RuntimeError(f"AOT generator capability changed: {failed.stdout} {failed.stderr}")
                                self.diagnostics.append(dict(case=name, size=size, mode=mode,
                                    status="DiagnosticFailure", diagnostic=failed.stderr.strip()))
                            continue
                        if (name,size,mode,repeat) in completed_keys:
                            continue
                        if mode == "python":
                            command = [str(self.python), str(Path(__file__).resolve()), "--worker", name,
                                str(n), "--target-ms", str(self.target_ms)]
                        else:
                            command = [str(self.executable), "--mode", mode, "--case", name,
                                "--iterations", str(n), "--target-ms", str(self.target_ms)]
                        completed, wall_ms = self.launch(command)
                        if completed.returncode:
                            raise RuntimeError(f"benchmark failed: {command}\n{completed.stdout}\n{completed.stderr}")
                        values = json.loads(completed.stdout) if mode == "python" else parse_native(completed.stdout, name, mode, n)
                        validate_measurement(values)
                        expected = expected_checksum(name, n)
                        if values["checksum"] != expected:
                            raise RuntimeError(f"{name}/{mode}/{size}: checksum {values['checksum']} != oracle {expected}")
                        values.setdefault("allocations_per_call", None)
                        values.setdefault("live_bytes_delta", None)
                        sample = Sample(name, size, n, mode, repeat, wall_ms=wall_ms, **values)
                        self.samples.append(sample)
                        # Preserve partial raw evidence even if a later workload fails.
                        self.write_csv()
                print(f"validated {name}/{size} n={n}", flush=True)
        # Instrumented Python memory is a separate invocation, excluded from timing summaries.
        for name in names:
            n = iterations_for(name, sizes[-1])
            completed, _ = self.launch([str(self.python), str(Path(__file__).resolve()),
                "--worker", name, str(n), "--target-ms", "1", "--memory"])
            if completed.returncode:
                raise RuntimeError(completed.stderr)
            values = json.loads(completed.stdout)
            if values["checksum"] != expected_checksum(name, n):
                raise RuntimeError("memory worker checksum mismatch")
            for index, sample in enumerate(self.samples):
                if sample.case == name and sample.mode == "python" and sample.size == sizes[-1]:
                    self.samples[index] = Sample(**(asdict(sample) | {
                        "python_trace_peak_bytes": values["python_trace_peak_bytes"]}))
        self.write_csv()

    def write_csv(self):
        with (self.output / "samples.csv").open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(Sample.__dataclass_fields__))
            writer.writeheader()
            writer.writerows(asdict(sample) for sample in self.samples)

    def offline_build(self, count: int) -> list[dict]:
        """Generate, compile and link in scratch paths; never alter build outputs."""
        repo = SOURCE.parents[3]
        build = self.executable.parent
        compiler = Path("C:/mingw64/bin/g++.exe")
        aotc = build / "arp_script_aotc.exe"
        if not aotc.is_file():
            raise RuntimeError("offline measurement requires adjacent freshly built arp_script_aotc.exe")
        commands = subprocess.check_output(["C:/mingw64/bin/ninja.exe", "-C", str(build),
            "-t", "commands", "arp_scriptvm_stress_benchmark"], text=True)
        link_lines = [line for line in commands.splitlines() if " -o arp_scriptvm_stress_benchmark.exe " in line]
        if len(link_lines) != 1:
            raise RuntimeError("cannot identify the versioned CMake benchmark link command")
        # The first nested command is the native link; post-build package audit
        # is performed separately. No shell text is executed.
        link_tokens = shlex.split(link_lines[0].split(" && ")[1], posix=False)
        rows = []
        for repeat in range(count):
            scratch = (self.output / "offline" / str(repeat)).resolve()
            scratch.mkdir(parents=True, exist_ok=True)
            start = time.perf_counter_ns()
            generated, codegen_ms = self.launch([str(aotc), "package", str(SOURCE.with_name("stress.package.yaml")),
                "--output", str(scratch)])
            if generated.returncode:
                raise RuntimeError(generated.stderr)
            objects = {}
            compile_start = time.perf_counter_ns()
            for stem in ("stress", "package_registry"):
                obj = scratch / f"{stem}.obj"
                args = [str(compiler), "-O3", "-DNDEBUG", "-std=c++17",
                    "-DARP_STATIC_LINK=1", "-DARP_HAS_MIMALLOC=1",
                    f"-I{repo / 'src'}", f"-I{repo / '3rd/mimalloc/include'}",
                    f"-I{repo / '3rd/fkYAML/include'}", f"-I{scratch}",
                    "-c", str(scratch / f"{stem}.gen.cpp"), "-o", str(obj)]
                compiled, _ = self.launch(args)
                if compiled.returncode:
                    raise RuntimeError(compiled.stderr)
                objects[stem] = obj
            compile_ms = (time.perf_counter_ns()-compile_start)/1e6
            link = list(link_tokens)
            for index, token in enumerate(link):
                if token.endswith("/stress.gen.cpp.obj"):
                    link[index] = str(objects["stress"])
                if token.endswith("/package_registry.gen.cpp.obj"):
                    link[index] = str(objects["package_registry"])
                if token.startswith("-Wl,--out-implib,"):
                    link[index] = f"-Wl,--out-implib,{scratch / 'bench.dll.a'}"
            output_index = link.index("-o")+1
            linked_exe = scratch / "benchmark.exe"
            link[output_index] = str(linked_exe)
            link_start = time.perf_counter_ns()
            linked = subprocess.run(link,cwd=build,capture_output=True,text=True,timeout=120)
            link_ms = (time.perf_counter_ns()-link_start)/1e6
            if linked.returncode:
                raise RuntimeError(linked.stderr)
            total_ms = (time.perf_counter_ns()-start)/1e6
            smoke, _ = self.launch([str(linked_exe), "--case", "int_loop", "--iterations", "17", "--mode", "aot"])
            if smoke.returncode:
                raise RuntimeError(smoke.stderr)
            if parse_native(smoke.stdout,"int_loop","aot",17)["checksum"] != expected_checksum("int_loop",17):
                raise RuntimeError("fresh offline build smoke mismatch")
            rows.append(dict(repeat=repeat,codegen_ms=codegen_ms,native_compile_ms=compile_ms,
                link_ms=link_ms,total_ms=total_ms,generated_cpp_bytes=sum((scratch/f"{stem}.gen.cpp").stat().st_size for stem in objects),
                generated_object_bytes=sum(obj.stat().st_size for obj in objects.values()),
                executable_bytes=linked_exe.stat().st_size))
            print(f"offline generation/compile/link validated {repeat+1}/{count}",flush=True)
        with (self.output/"offline-build.csv").open("w",encoding="utf-8",newline="") as stream:
            writer=csv.DictWriter(stream,fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)
        return rows

    def render(self, environment: dict):
        lines = ["# ArpScript AOT / Interpreter / CPython benchmark", "",
            f"Date: {environment['timestamp']}. Status: **Windows Platform Validated for this corpus**.",
            "This is not a cross-platform or Production Ready claim.", "",
            f"CPython: `{environment['python_version']}`. MinGW: `{environment['compiler']}`.",
            f"CPU: `{environment['cpu']}`. OS: `{environment['os']}`.",
            f"Executable SHA256: `{environment['executable_sha256']}`.",
            f"Corpus SHA256: `{environment['corpus_sha256']}`.",
            f"{self.repeats} independent processes per case/size/runtime, randomized runtime order (fixed seed).",
            f"One first call warms each process; steady-state batches target {self.target_ms} ms (cap 10000 calls).",
            "All reported results match an independent checksum oracle. Raw observations: `samples.csv`.", "",
            "## Steady-state execution", "",
            "Times are median ms per workload invocation. Speedup = Python time / ArpScript time; >1 is faster than Python.",
            "Throughput counts outer workload iterations, not bytecodes, allocations or inner-loop operations.", "",
            "| case | size | n | Interpreter ms | AOT ms | Python ms | Int speedup | AOT speedup |",
            "|---|---|---:|---:|---:|---:|---:|---:|"]
        grouped = {}
        for sample in self.samples:
            grouped.setdefault((sample.case, sample.size, sample.mode), []).append(sample)
        pairs = list(dict.fromkeys((s.case, s.size) for s in self.samples))
        summary = []
        for case, size in pairs:
            medians = {mode: statistics.median(s.execution_ms for s in grouped[case,size,mode])
                for mode in MODES if (case,size,mode) in grouped}
            n = next(s.iterations for s in self.samples if s.case == case and s.size == size)
            def cell(mode):
                return f"{medians[mode]:.6f}" if mode in medians else "DiagnosticFailure"
            def ratio(mode):
                return f"{medians['python']/medians[mode]:.3f}x" if mode in medians else "—"
            lines.append(f"| {case} | {size} | {n} | {cell('interpreter')} | {cell('aot')} | {cell('python')} | {ratio('interpreter')} | {ratio('aot')} |")
            for mode in medians:
                runs = grouped[case,size,mode]
                times = [s.execution_ms for s in runs]
                median = medians[mode]
                summary.append(dict(case=case,size=size,mode=mode,iterations=n,
                    median_ms=median,min_ms=min(times),max_ms=max(times),
                    cv=statistics.pstdev(times)/statistics.mean(times),
                    iterations_per_second=n*1000/median,
                    python_speedup=medians['python']/median,
                    cold_median_ms=statistics.median(s.cold_ms for s in runs),
                    compile_median_ms=statistics.median(s.compile_ms for s in runs),
                    setup_median_ms=statistics.median(s.setup_ms for s in runs),
                    wall_median_ms=statistics.median(s.wall_ms for s in runs),
                    allocations_per_call=statistics.median(s.allocations_per_call for s in runs) if mode!='python' else None,
                    live_bytes_delta=statistics.median(s.live_bytes_delta for s in runs) if mode!='python' else None,
                    peak_working_set_bytes=max(s.process_peak_working_set or 0 for s in runs),
                    python_trace_peak_bytes=runs[0].python_trace_peak_bytes))
        with (self.output / "summary.csv").open("w",encoding="utf-8",newline="") as stream:
            writer=csv.DictWriter(stream,fieldnames=list(summary[0]))
            writer.writeheader()
            writer.writerows(summary)
        lines += ["", "## Compilation, first call and process cost", "",
            "Interpreter compilation includes the entire shared corpus: frontend, semantic analysis, Typed IR and bytecode lowering.",
            "Python compile and module exec are reported separately. AOT runtime setup includes package validation/registration/module construction; offline code generation/native compilation is separate.",
            "The first call includes runtime initialization. Process wall time includes process creation, imports, compilation/setup, first call, warm batches, output and exit; it is not a steady-state speed comparison.", "",
            "| runtime | median compile ms | median setup ms | median first-call ms (small) | median process wall ms (small) |",
            "|---|---:|---:|---:|---:|"]
        for mode in MODES:
            runs=[s for s in self.samples if s.mode==mode]
            small=[s for s in runs if s.size==pairs[0][1]]
            med=lambda key,rows: statistics.median(getattr(s,key) for s in rows)
            lines.append(f"| {mode} | {med('compile_ms',runs):.6f} | {med('setup_ms',runs):.6f} | {med('cold_ms',small):.6f} | {med('wall_ms',small):.6f} |")
        lines += ["", "## Memory and variability (largest size)", "",
            "Arp allocation counts cover core/cpu_allocator during warm calls, normalized by batch count. Live-byte delta is the net retained allocation across the whole batch, not peak memory or a leak verdict.",
            "Process peak working set includes executable/runtime, setup and workload. Native modes use the same combined binary; this is not the deployment footprint of an AOT-only executable.",
            "Python traced peak measures one additional warmed call, excluding setup, in a separate instrumented process. It is not directly comparable to Arp allocation count/live-byte delta.", "",
            "| case | mode | min/max ms | CV | iterations/s | allocations/call | live delta B | process peak MiB | Python traced peak B |",
            "|---|---|---|---:|---:|---:|---:|---:|---:|"]
        largest=pairs[-1][1]
        for row in summary:
            if row['size']!=largest: continue
            lines.append(f"| {row['case']} | {row['mode']} | {row['min_ms']:.6f}/{row['max_ms']:.6f} | {row['cv']:.3f} | {row['iterations_per_second']:.0f} | {row['allocations_per_call']} | {row['live_bytes_delta']} | {row['peak_working_set_bytes']/1048576:.2f} | {row['python_trace_peak_bytes']} |")
        if environment.get("offline_build_samples"):
            offline=environment["offline_build_samples"]
            lines += ["", "## AOT offline build cost", "",
                "Each sample generates the complete corpus into a fresh scratch directory, compiles the two generated C++ files sequentially with MinGW -O3 -DNDEBUG -std=c++17, then links using the existing CMake runtime/harness objects and libraries. Every resulting executable passes a headless checksum smoke.",
                "Runtime libraries and the benchmark harness are prebuilt; these figures describe script-package rebuild cost, not a clean engine build. Filesystem/compiler caches are not flushed.", "",
                "| phase | median ms | min ms | max ms |", "|---|---:|---:|---:|"]
            for key in ("codegen_ms","native_compile_ms","link_ms","total_ms"):
                values=[row[key] for row in offline]
                lines.append(f"| {key} | {statistics.median(values):.3f} | {min(values):.3f} | {max(values):.3f} |")
            lines += ["",f"Generated C++ bytes: {offline[0]['generated_cpp_bytes']}; object bytes: {offline[0]['generated_object_bytes']}; combined executable bytes: {offline[0]['executable_bytes']}."]
        lines += ["", "## Capability gaps and comparison limits", "",
            "- AOT generator iteration: DirectNative package generation succeeds, but execution fails with `range over non-iterable`. Interpreter and Python are measured; AOT is explicitly DiagnosticFailure, with no legacy fallback.",
            "- `range(n)` materializes a container in the current Arp runtime. The AOT manifest caps container values at 65536; n=100000 fails with `range exceeds the container limit`. The published large range case uses the existing corpus's explicit n=50000, within that budget.",
            "- String slices are rejected by the parser (`expected ']' after index`); string indexing is rejected by semantic analysis (`type 'string' is not indexable`). String concatenation, bounded construction and comparison are measured.",
            "- The benchmark-only Python syntax adapter removes explicitly listed typed local declarations and maps `raise \"periodic\"` / `except as error` to RuntimeError. Loop/call/container/object shapes otherwise share the same source.",
            "- CPython uses binary64 float and arbitrary-precision integers; selected inputs avoid overflow. The floating workload uses exactly representable dyadic constants and a branch checksum; this does not prove all numeric semantics are equivalent.",
            "- This suite measures CPU script execution, not disk/network/GPU throughput, engine reflection/plugin ABI, await scheduling, OSR/deoptimization, fuzz correctness, Unicode equivalence or application end-to-end frame time.",
            "- GC/allocator policies are the runtime defaults. CPU affinity/frequency are not pinned; background load and sub-millisecond samples may vary. Min/max/CV are supplied instead of a universal performance guarantee.",
            "- ArpScript runs with its current execution defaults: AOT consumes the manifest's explicit budgets, while Interpreter uses the VM's default limits/instruction policy. These policy costs are part of each measured execution mode; this is not a benchmark with safety checks disabled in both runtimes.",
            "- No geometric mean combines unlike workloads; each row is an independent result.", ""]
        lines += ["## Execution-path evidence", "",
            "The v5 generated package report contains 31 DirectNative functions and zero generic executor entries. DirectNative does not imply every value is unboxed.",
            "`src/scriptvm/aot/aot_codegen_v5.cpp` admits only Int/Bool to `scalarValueType`; `scalarOperation` excludes bitwise opcodes. Proven integer loops/calls lower to generated int64 scalar helpers. Float/bitwise/container paths use generated Value frames and ScriptSemanticRuntime helpers, with observable core allocator activity.",
            "The timing and allocation rows show the cost of those paths. Allocation count alone does not prove which helper dominates; a dedicated CPU profile is needed for precise attribution.", ""]
        (self.output / "report.md").write_text("\n".join(lines),encoding="utf-8")
        # YAML metadata is emitted with JSON-quoted scalars, which YAML accepts.
        environment['diagnostics']=self.diagnostics
        self.write_environment(environment)


def iterations_for(name: str, size: str) -> int:
    index = ("small", "medium", "large").index(size)
    if name == "recursive_fib": return (1, 2, 4)[index]
    if name == "generator_sum": return (32, 128, 512)[index]
    if name in {"nested_loop", "list_iterate"}: return (128, 1024, 8192)[index]
    if name == "string_concat": return (256, 1024, 4096)[index]
    if name == "range_sum": return (1000, 10000, 50000)[index]
    return (1000, 10000, 100000)[index]


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable",type=Path,nargs="?")
    parser.add_argument("--python",type=Path,default=Path(sys.executable))
    parser.add_argument("--output",type=Path,default=Path("artifacts/scriptvm-python-benchmark"))
    parser.add_argument("--repeat",type=int,default=5)
    parser.add_argument("--target-ms",type=int,default=20)
    parser.add_argument("--sizes",nargs="+",choices=("small","medium","large"),default=["small","medium","large"])
    parser.add_argument("--cases",nargs="+",choices=CASE_NAMES,default=list(CASE_NAMES))
    parser.add_argument("--worker",nargs=2,metavar=("CASE","N"))
    parser.add_argument("--memory",action="store_true")
    parser.add_argument("--offline-repeat",type=int,default=0)
    parser.add_argument("--resume",action="store_true")
    args=parser.parse_args()
    if not 1<=args.repeat<=20 or not 1<=args.target_ms<=1000 or not 0<=args.offline_repeat<=10:
        parser.error("repeat [1,20], target-ms [1,1000], offline-repeat [0,10]")
    if args.worker:
        return python_worker(args.worker[0],int(args.worker[1]),args.target_ms,args.memory)
    if not args.executable or not args.executable.is_file(): parser.error("benchmark executable is required")
    suite=BenchmarkSuite(args.executable,args.python,args.output,args.repeat,args.target_ms)
    cache = (suite.executable.parent / "CMakeCache.txt").read_text(encoding="utf-8")
    if "CMAKE_BUILD_TYPE:STRING=Release" not in cache:
        raise RuntimeError("CMake cache must confirm a Release build")
    environment=dict(timestamp=datetime.now(timezone(timedelta(hours=8))).isoformat(),
        python_version=subprocess.check_output([str(args.python),"-c","import sys; print(sys.version)"],text=True).strip(),
        compiler=subprocess.check_output(["C:/mingw64/bin/g++.exe","--version"],text=True).splitlines()[0],
        cpu=platform.processor(),os=platform.platform(),
        executable=str(suite.executable),python=str(suite.python),
        executable_sha256=hashlib.sha256(suite.executable.read_bytes()).hexdigest(),
        corpus_sha256=hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
        python_adapter_sha256=hashlib.sha256(Path(__file__).with_name("scriptvm_python_corpus.py").read_bytes()).hexdigest(),
        suite_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        repetitions=args.repeat,target_ms=args.target_ms,sizes=args.sizes,cases=args.cases,
        command=subprocess.list2cmdline([sys.executable,*sys.argv]),
        clock_resolution_seconds=time.get_clock_info("perf_counter").resolution)
    if sys.platform == "win32":
        environment['cpu']=subprocess.check_output(["powershell", "-NoProfile", "-Command",
            "(Get-CimInstance Win32_Processor).Name"],text=True).strip()
    environment['git_revision']=subprocess.check_output(["git", "rev-parse", "HEAD"],cwd=SOURCE.parents[3],text=True).strip()
    environment['build_type']="Release"
    environment['native_flags']="-O3 -DNDEBUG -std=c++17"
    environment['input_counts']={name:{size:iterations_for(name,size) for size in args.sizes} for name in args.cases}
    if args.resume:
        suite.restore(environment)
    suite.write_environment(environment)
    suite.run(args.sizes,args.cases)
    if args.offline_repeat:
        environment['offline_build_samples']=suite.offline_build(args.offline_repeat)
    suite.render(environment)
    print(f"Validated {len(suite.samples)} samples; report: {args.output.resolve() / 'report.md'}")
    return 0


if __name__=="__main__":
    raise SystemExit(main())
