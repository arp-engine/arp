#!/usr/bin/env python3
"""Run an already-built GLES2/WebGL1 feasibility probe with the shared runner."""
import argparse
from pathlib import Path
import sys

import yaml

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from browser_contract_runner import BrowserContractRunner
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tooling"))
from webgl1_probe_report_contract import WebGlProbeReportContract


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--browser", required=True)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--timeout-seconds", type=int, required=True)
    parser.add_argument("--capture-frames", type=int, default=1)
    args = parser.parse_args()
    if not 0 < args.capture_frames <= 2147483647:
        parser.error("capture-frames must be a positive signed 32-bit count")
    entry = f"index.html?captureFrames={args.capture_frames}"
    result = BrowserContractRunner(args.browser, args.directory, entry,
        args.output, args.timeout_seconds).run()
    (args.output / "runtime-report.yaml").write_text(yaml.safe_dump(result, sort_keys=False), encoding="utf-8")
    WebGlProbeReportContract.validate(result, args.capture_frames)
    print(result["diagnostic"], result["version"])


if __name__ == "__main__":
    main()
