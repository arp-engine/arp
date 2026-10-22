"""Exercise the real CLI with a fresh, test-owned output for each invocation."""

import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--tool", required=True)
    parser.add_argument("--reader", required=True)
    parser.add_argument("--authoring", required=True)
    parser.add_argument("--replacement-authoring")
    parser.add_argument("--workspace", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="terrain-cli-", dir=args.workspace) as directory:
        output = Path(directory) / "terrain.arpkg"
        command = [args.tool, "build", args.authoring, str(output)]
        subprocess.run(command, check=True, timeout=30)
        reader = [args.reader, str(output)]
        if args.replacement_authoring:
            replacement = Path(directory) / "replacement.arpkg"
            subprocess.run([args.tool, "build", args.replacement_authoring,
                            str(replacement)], check=True, timeout=30)
            reader.append(str(replacement))
        subprocess.run(reader, check=True, timeout=30)
        before = {p.relative_to(output): p.read_bytes() for p in output.rglob("*") if p.is_file()}
        conflict = subprocess.run(command, capture_output=True, text=True, timeout=30)
        if conflict.returncode != 1 or "ARP-DIRECTORY-DESTINATION-EXISTS" not in conflict.stderr:
            raise RuntimeError(f"CLI did not reject existing output: {conflict.stderr}")
        after = {p.relative_to(output): p.read_bytes() for p in output.rglob("*") if p.is_file()}
        if after != before or Path(str(output) + ".arp-staging").exists():
            raise RuntimeError("CLI changed the existing package or left staging files")
        subprocess.run(reader, check=True, timeout=30)
    print("Terrain CLI atomic build/load/conflict smoke passed")


if __name__ == "__main__":
    main()
