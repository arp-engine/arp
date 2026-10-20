"""Run the C++ workload resource planner and native resource creation in Chrome."""
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
    args.output.mkdir(parents=True, exist_ok=True)
    inputs = {"index.html": ROOT / "tests/fixtures/bindless_workload_resources.html"}
    for suffix in ("js", "wasm"):
        name = f"arp_bindless_workload_resources_tests.{suffix}"
        inputs[name] = args.runner / name
    hashes = {}
    for name, source in inputs.items():
        shutil.copyfile(source, args.output / name)
        hashes[name] = hashlib.sha256((args.output / name).read_bytes()).hexdigest()
    (args.output / "inputs-sha256.json").write_text(json.dumps(hashes, indent=2))
    result = BrowserContractRunner(args.browser, args.output, "index.html", args.output, 60).run()
    print(json.dumps(result, indent=2))
    return 0 if result.get("success") and result.get("createdDeviceValidated") \
        and result.get("finiteBindingsValidated") and result.get("bufferAllocations") == 4 \
        and result.get("textureAllocations") == 32 and result.get("runtimeCapabilityGranted") is False \
        and result.get("samplerAllocations") == 1 and result.get("physicalBindings") == 36 \
        and not result.get("diagnostics") and not result.get("nativeErrors") else 1


if __name__ == "__main__":
    raise SystemExit(main())
