"""Genuine shader numeric evidence; deliberately no original actor receipt."""
import argparse
import pathlib
import json
import sys

import yaml

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from original_depth_table_expansion import compile_expansion, read_expansion
from browser_contract_runner import BrowserContractRunner


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--slangc", required=True)
    parser.add_argument("--spirv-reflect", required=True)
    parser.add_argument("--browser", required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--timeout-seconds", type=int, default=180)
    parser.add_argument("--disable-indirect-first-instance", action="store_true")
    args = parser.parse_args()
    for profile in ("Compute", "Graphics"):
        schema = yaml.safe_load((ROOT / "tests/fixtures" /
            ("original_depth_table_" + profile.lower() + "_expansion.yaml")).read_text())
        source = ROOT / "tests/fixtures" / ("descriptor_depth_compute.slang" if profile == "Compute" else "descriptor_depth.slang")
        path = compile_expansion(schema, source.read_bytes(), args.output / profile.lower(), args.slangc, args.spirv_reflect)
        # JSON is only the test HTTP observation bridge, never production authoring/IR.
        (path.parent / "probe.json").write_text(json.dumps(read_expansion(path)))
    (args.output / "index.html").write_bytes((ROOT / "tests/fixtures/original_depth_table_expansion_runner.html").read_bytes())
    entry = "index.html" + ("?disable-indirect-first-instance" if args.disable_indirect_first_instance else "")
    result = BrowserContractRunner(args.browser, args.output, entry, args.output, args.timeout_seconds).run()
    print(json.dumps(result, indent=2))
    return 0 if result.get("success") else 1


if __name__ == "__main__":
    raise SystemExit(main())
