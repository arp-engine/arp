"""Execute the engine's C++ pre-device demand planner in a real browser."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from browser_contract_runner import BrowserContractRunner


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--browser", required=True)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    results = []
    for mode in ("default", "requested", "package"):
        directory = args.output / mode
        directory.mkdir(parents=True, exist_ok=True)
        inputs = {"index.html": ROOT / "tests/fixtures/bindless_device_limit_plan.html"}
        for suffix in ("js", "wasm"):
            name = f"arp_bindless_pipeline_demand_tests.{suffix}"
            inputs[name] = args.runner / name
        hashes = {}
        for name, source in inputs.items():
            shutil.copyfile(source, directory / name)
            hashes[name] = hashlib.sha256((directory / name).read_bytes()).hexdigest()
        (directory / "inputs-sha256.json").write_text(json.dumps(hashes, indent=2), encoding="utf-8")
        result = BrowserContractRunner(args.browser, directory, f"index.html?mode={mode}",
                                       directory, 60).run()
        results.append(result)
        print(json.dumps(result, indent=2))
    return 0 if all(r.get("success") and r.get("runtimeCapabilityGranted") is False
                    and not r.get("diagnostics") and not r.get("nativeErrors")
                    for r in results) and results[0].get("refusedBeforeExpandedDevice") \
        and all(r.get("createdDeviceValidated") for r in results[1:]) else 1


if __name__ == "__main__":
    raise SystemExit(main())
