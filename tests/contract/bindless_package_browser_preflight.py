"""Real browser pipeline compilation for sealed Emulated shader packages.

No recording, submission, readback or RHI execution readiness is asserted.
Graphics selection is explicit, color-only and has no vertex buffers.
"""
import argparse
import hashlib
import json
import pathlib
import shutil
import sys
import yaml

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from bindless_codegen import read_bindless_publication
from browser_contract_runner import BrowserContractRunner


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--publication", type=pathlib.Path, required=True)
    parser.add_argument("--browser", required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--vertex-entry")
    parser.add_argument("--fragment-entry")
    parser.add_argument("--color-format")
    args = parser.parse_args()
    graphics = bool(args.vertex_entry or args.fragment_entry or args.color_format)
    if graphics and not (args.vertex_entry and args.fragment_entry and args.color_format):
        parser.error("Graphics preflight requires vertex entry, fragment entry and color format")
    manifest = read_bindless_publication(args.publication)
    if manifest["execution"] != "Emulated":
        raise ValueError("Emulated package required")
    args.output.mkdir(parents=True, exist_ok=True)
    for name, digest in manifest["artifacts"].items():
        if pathlib.Path(name).name != name:
            raise ValueError("Artifact path mismatch")
        data = (args.publication.parent / name).read_bytes()
        if hashlib.sha256(data).hexdigest() != digest:
            raise ValueError("Artifact hash/path mismatch")
        (args.output / name).write_bytes(data)
    modules = []
    for module in manifest["modules"]:
        reflection = yaml.safe_load((args.output / module["reflection"]).read_bytes())
        if len(reflection["entryPoints"]) != 1:
            raise ValueError("Single-entry reflection required")
        if (reflection["entryPoints"][0]["name"] != module["entry"]
                or reflection["targets"]["wgsl"] not in manifest["artifacts"]):
            raise ValueError("Entry/target mismatch")
        stage = reflection["entryPoints"][0]["stage"]
        if graphics:
            selected = {args.vertex_entry: "Vertex", args.fragment_entry: "Fragment"}
            if len(selected) != 2:
                raise ValueError("Graphics entries must be distinct")
            if module["entry"] not in selected:
                continue
            if stage != selected[module["entry"]]:
                raise ValueError("Graphics selected-entry stage mismatch")
        elif stage != "Compute":
            raise ValueError("Graphics package requires explicit selected entries and color format")
        item = dict(entry=module["entry"], path=reflection["targets"]["wgsl"], stage=stage)
        if graphics:
            item["colorFormat"] = args.color_format
        modules.append(item)
    if not modules:
        raise ValueError("Empty selected package")
    if graphics and sorted(m["stage"] for m in modules) != ["Fragment", "Vertex"]:
        raise ValueError("Exactly one selected Vertex and Fragment required")
    (args.output / "preflight.json").write_text(json.dumps(modules), encoding="utf-8")
    shutil.copyfile(ROOT / "tests/fixtures/bindless_package_preflight.html", args.output / "index.html")
    result = BrowserContractRunner(args.browser, args.output, "index.html", args.output, 60).run(dict(
        shaderPackageIdentity=manifest["shaderPackageIdentity"], scope="shader-pipeline-compilation-only"))
    print(json.dumps(result, indent=2))
    return 0 if result.get("success") and result.get("compiledEntries") == [m["entry"] for m in modules] else 1


if __name__ == "__main__":
    raise SystemExit(main())
