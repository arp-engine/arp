#!/usr/bin/env python3
"""Verify reflection codegen rejects ambiguous export names."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) not in (4, 5):
        print("usage: reflection_codegen_negative_contract.py CODEGEN HEADER OUT_DIR [EXPECTED_DIAGNOSTIC]", file=sys.stderr)
        return 2
    codegen = Path(sys.argv[1])
    header = Path(sys.argv[2])
    out_dir = Path(sys.argv[3])
    expected = sys.argv[4] if len(sys.argv) == 5 else "duplicate reflected type"
    out_dir.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(
        [
            str(codegen),
            "--module",
            "reflection_codegen_negative",
            "--out-json",
            str(out_dir / "negative.json"),
            "--out-cpp",
            str(out_dir / "negative.cpp"),
            "--header",
            str(header),
        ],
        text=True,
        capture_output=True,
        check=False,
    )
    diagnostic = result.stdout + result.stderr
    if result.returncode == 0:
        print("codegen unexpectedly accepted invalid reflected contract", file=sys.stderr)
        return 1
    if expected not in diagnostic:
        print(diagnostic, file=sys.stderr)
        return 1
    print("reflection codegen negative contract passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
