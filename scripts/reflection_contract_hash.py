"""Check/update a plugin's versioned public generated-reflection identity.

Canonical input: contractVersion plus module/classes/enums/functions, UTF-8,
sorted keys, compact JSON. Build-local header paths are deliberately excluded.
This is generated metadata, not a resource authoring format.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re


def public_contract_hash(metadata, contract_version):
    surface = {"contractVersion": contract_version}
    for key in ("module", "classes", "enums", "functions"):
        surface[key] = metadata[key]
    encoded = json.dumps(surface, sort_keys=True, separators=(",", ":"),
                         ensure_ascii=False).encode("utf-8")
    return "sha256:" + hashlib.sha256(encoded).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reflection", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--update", action="store_true")
    args = parser.parse_args()
    metadata = json.loads(args.reflection.read_text(encoding="utf-8"))
    manifest = args.manifest.read_text(encoding="utf-8-sig")
    version = re.search(r"^contractVersion:\s*(\d+)\s*$", manifest, re.M)
    module = re.search(r"^module:\s*(\w+)\s*$", manifest, re.M)
    declared = re.search(r"^exportContractHash:\s*(sha256:[0-9a-f]{64})\s*$", manifest, re.M)
    if not version or not module or not declared or module[1] != metadata["module"]:
        raise SystemExit("Invalid reflection/manifest identity contract")
    expected = public_contract_hash(metadata, int(version[1]))
    if args.update:
        manifest = manifest[:declared.start(1)] + expected + manifest[declared.end(1):]
        args.manifest.write_text(manifest, encoding="utf-8")
    elif declared[1] != expected:
        raise SystemExit(f"Export contract hash mismatch: declared={declared[1]} expected={expected}")
    print(f"{metadata['module']}: {expected}")


if __name__ == "__main__":
    main()
