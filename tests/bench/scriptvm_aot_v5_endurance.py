#!/usr/bin/env python3
"""Run the bounded v5 differential endurance harness on Windows or Node/Wasm."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    parser.add_argument("--duration-seconds", type=int, required=True)
    parser.add_argument("--seed", default="0xA075EED")
    parser.add_argument("--node", type=Path)
    args = parser.parse_args()
    if args.duration_seconds < 1:
        parser.error("duration-seconds must be at least 1")
    if not args.executable.is_file():
        parser.error(f"executable does not exist: {args.executable}")
    command = []
    if args.node is not None:
        command.append(str(args.node))
    command.extend(
        [
            str(args.executable),
            "--duration-seconds",
            str(args.duration_seconds),
            "--seed",
            args.seed,
        ]
    )
    try:
        completed = subprocess.run(
            command,
            check=False,
            text=True,
            capture_output=True,
            timeout=args.duration_seconds + 120,
        )
    except subprocess.TimeoutExpired as error:
        raise RuntimeError("AOT endurance harness exceeded its timeout") from error
    output = completed.stdout + completed.stderr
    print(output, end="")
    if completed.returncode != 0:
        raise RuntimeError(
            f"AOT endurance harness exited with {completed.returncode}"
        )
    if "AOT_ENDURANCE status=passed" not in output:
        raise RuntimeError("AOT endurance harness did not report a passing status")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as error:
        print(f"endurance failure: {error}", file=sys.stderr)
        raise SystemExit(1) from error
