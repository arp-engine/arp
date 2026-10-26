"""Reject malformed/overflow/under-budget probe requests before shader IO."""
import argparse
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--executable', required=True)
    executable = parser.parse_args().executable
    invalid = [[], ['0', '6', '1', '16777216'], ['8', '0', '1', '16777216'],
               ['8', '6', '0', '16777216'], ['8', '6', '1', '0'],
               ['4294967296', '6', '1', '16777216'],
               ['4294967295', '4294967295', '4294967295', '16777216'],
               ['8x', '6', '1', '16777216'], ['-1', '6', '1', '16777216'],
               ['8', '6', '1', '1535'], ['160', '90', '1', '5207039']]
    for arguments in invalid:
        result = subprocess.run([executable, 'missing-publication-for-request-validation', *arguments],
                                capture_output=True, text=True, timeout=10)
        assert result.returncode == 2, (arguments, result.returncode, result.stdout, result.stderr)
        assert result.stderr.strip() == 'METAL-DISPATCH-PROBE-REQUEST', (arguments, result.stderr)
        assert not result.stdout, (arguments, result.stdout)
    print('DISPATCH_GRID_PROBE_REQUEST_PASS cases=' + str(len(invalid)))


if __name__ == '__main__':
    main()
