"""Verify refusal occurs before a live original journal can be touched."""
import subprocess
import sys


def main():
    assert len(sys.argv) == 2
    for mode in ("--forbid-unresolved-retirement", "--forbid-active-foreign-destruction"):
        result = subprocess.run(
            [sys.argv[1], mode], capture_output=True, text=True, timeout=10, check=False
        )
        assert result.returncode == 87, (mode, result.returncode, result.stdout, result.stderr)
        assert result.stdout.strip() == "destruction refused before journal mutation", (
            mode, result.stdout, result.stderr
        )
    print("2 forbidden destruction cases refused before journal mutation; validator only")


if __name__ == "__main__":
    main()
