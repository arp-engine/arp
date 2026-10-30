"""Reject source preparation failures before native output publication."""
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    compiler, fixture = map(lambda value: str(Path(value).resolve()), sys.argv[1:3])
    with tempfile.TemporaryDirectory(prefix="arp-script-source-cli-") as folder:
        root = Path(folder)
        subprocess.run([fixture, "--write-fixtures", folder], check=True, timeout=30)
        manifest = root / "snapshot/package.yaml"
        output = root / "output"

        def run(expected, *options):
            result = subprocess.run([compiler, "compile", str(manifest), "--output", str(output),
                                     *options], capture_output=True, text=True, timeout=30)
            assert result.returncode == expected, (options, result.stdout, result.stderr)
            if expected:
                assert not output.exists(), options
                assert result.stderr, options
            return result

        for option in ["--max-manifest-bytes", "--max-modules", "--max-file-bytes",
                       "--max-source-bytes", "--max-text-bytes"]:
            run(1, option, "1")
        for option, value in [("--max-file-bytes", "0"), ("--max-file-bytes", "12oops"),
                              ("--max-modules", "4294967296"), ("--unknown-budget", "2")]:
            run(2, option, value)
        source = root / "snapshot/basic.arp"
        original = source.read_bytes()
        for invalid in [b"\xff", b"def run():\n\x00"]:
            source.write_bytes(invalid)
            run(1)
        source.unlink()
        run(1)
        source.write_bytes(original)
        original_manifest = manifest.read_bytes()
        manifest.write_bytes(b"\xff")
        run(1)
        manifest.write_bytes(original_manifest)
        run(0)
        assert (output / "package.aot-v5.yaml").is_file()
        assert (output / "package_registry.gen.cpp").is_file()
        print("AotCliSourceProfile/1: 13 failures preserved absent output; valid package generated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
