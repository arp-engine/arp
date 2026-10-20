"""Run the finite public depth producer/publication workload on a real Vulkan device."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--package-dir", type=Path, required=True,
                        help="Existing Slang packages produced by bindless_vulkan_smoke.py")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--renderdoccmd", type=Path)
    parser.add_argument("--terminal", type=int, choices=(1,2,3,4))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    packages = [args.package_dir / "BindlessFixture.native.bindless.yaml",
                args.package_dir / "public-compute/BindlessFixture.native.bindless.yaml",
                args.package_dir / "public-graphics/BindlessFixture.native.bindless.yaml"]
    for path in [args.runner, *packages]:
        if not path.is_file():
            parser.error("Missing built input: " + str(path))
    command = [str(path.resolve()) for path in [args.runner, *packages]]
    env = os.environ.copy()
    env["ARP_VULKAN_PUBLIC_DEPTH_ONLY"] = "1"
    if args.terminal:
        env["ARP_VULKAN_PUBLIC_DEPTH_EXECUTION_TERMINAL"] = str(args.terminal)
    if args.renderdoccmd:
        env["ARP_RENDERDOC_CAPTURE_PATH"] = str((args.output / "public-depth").resolve())
        command = [str(args.renderdoccmd), "capture", "--wait-for-exit", "--capture-file",
                   env["ARP_RENDERDOC_CAPTURE_PATH"], "--opt-api-validation", *command]
    start = time.monotonic()
    result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=120)
    marker = "BINDLESS_VULKAN_PUBLIC_DEPTH_PUBLICATION4 "
    public = "BINDLESS_VULKAN_PUBLIC_ROOT_GRAPHICS1 "
    prepared = "BINDLESS_VULKAN_PUBLIC_DEPTH_ROOT6 "
    read_plan = "BINDLESS_VULKAN_PUBLIC_DEPTH_READ_PLAN12 "
    execution = "BINDLESS_VULKAN_PUBLIC_DEPTH_EXECUTION6 "
    terminal = "BINDLESS_VULKAN_PUBLIC_DEPTH_EXECUTION6_TERMINAL "
    report = dict(success=result.returncode == 0 and result.stdout.count(marker) == 1
                  and result.stdout.count(public) == 4 and result.stdout.count(prepared) == 2
                  and result.stdout.count(read_plan) == 2 and result.stdout.count(execution) == 2
                  and result.stdout.count(terminal) == (1 if args.terminal else 0),
                  returnCode=result.returncode, elapsedSeconds=time.monotonic()-start,
                  producerPasses=2 if result.stdout.count(marker) == 1 else 0,
                  publicViews=result.stdout.count(public),
                  nativeColdDepthRoots=result.stdout.count(prepared),
                  nativeDepthReadPlans=result.stdout.count(read_plan),
                  nativeDepthExecutions=result.stdout.count(execution),
                  nativeDepthTerminal=args.terminal,
                  scope="Source2/Publication4 single D32Float cell; public Graphics producer plus independent Native Compute Execution6/Content12/Pending2/Lifecycle14/Completion8 on the same canonical inventory and original journal/fence; exact/one-less/quota, cancellation, non-uniform depth load/sampleLevel/query numerical output, fault1 and retirement; no production sampling/workload claim",
                  runnerSha256=hashlib.sha256(args.runner.read_bytes()).hexdigest(),
                  inputs={str(path.resolve()): hashlib.sha256(path.read_bytes()).hexdigest() for path in packages},
                  captureRequested=bool(args.renderdoccmd), stdout=result.stdout, stderr=result.stderr)
    (args.output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({key: value for key, value in report.items() if key not in ("stdout", "stderr", "inputs")}, indent=2))
    if not report["success"]:
        print(result.stdout, result.stderr)
    return 0 if report["success"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
