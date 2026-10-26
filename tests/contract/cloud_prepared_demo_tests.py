"""Real caller rejection checks; Null is never counted as native execution."""
import argparse
import subprocess


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exe', required=True)
    ap.add_argument('--asset', required=True)
    ap.add_argument('--native', choices=('temporal', 'reconstruction'))
    args = ap.parse_args()
    if args.native:
        command = [args.exe, '--backend', 'vulkan', '--prepared-offscreen', '--hidden',
                   '--asset', args.asset, '--width', '64', '--height', '48',
                   '--frames', '3', '--divisor', '2', '--temporal']
        if args.native == 'reconstruction':
            command.append('--reconstruction')
        run = subprocess.run(command, capture_output=True, text=True, timeout=150)
        print(run.stdout, end='')
        print(run.stderr, end='')
        assert run.returncode == 0, run.returncode
        resolve = 'Temporal/1' if args.native == 'temporal' else 'Reconstruction/1'
        assert 'CloudPreparedDemo/1 Prototype submitted=3 completed=3 resolve=' + resolve in run.stdout
        assert 'hotAllocations=0 presentation=false physicalPeakValidated=false' in run.stdout
        return
    base = [args.exe, '--backend', 'null', '--asset', args.asset,
            '--width', '32', '--height', '24', '--frames', '3']
    cases = [
        (['--prepared-offscreen'], 'CLOUD-DEMO-PREPARED-OFFSCREEN-PROFILE'),
        (['--prepared-offscreen', '--hidden', '--timings'],
         'CLOUD-DEMO-PREPARED-OFFSCREEN-PROFILE'),
        (['--reconstruction'], 'CLOUD-DEMO-RECONSTRUCTION-REQUIRES-PREPARED'),
        (['--prepared-offscreen', '--hidden'],
         'RHI-RENDER-PASS-PREPARATION-NULL-VALIDATOR'),
        (['--prepared-offscreen', '--hidden', '--reconstruction', '--divisor', '1'],
         'CLOUD-RECONSTRUCTION-RESOLUTION'),
    ]
    for options, diagnostic in cases:
        run = subprocess.run(base + options, capture_output=True, text=True, timeout=60)
        assert run.returncode == 1, (options, run.returncode, run.stdout, run.stderr)
        assert diagnostic in run.stderr, (options, diagnostic, run.stdout, run.stderr)
        assert 'submitted=' not in run.stdout, (options, run.stdout)
        print('Rejected before execution:', diagnostic)
    print('CloudPreparedDemo/1 caller profile, Null and reconstruction rejection passed')


if __name__ == '__main__':
    main()
