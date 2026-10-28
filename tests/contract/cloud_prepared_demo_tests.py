"""Real caller rejection checks; Null is never counted as native execution."""
import argparse
import subprocess


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exe', required=True)
    ap.add_argument('--asset', required=True)
    ap.add_argument('--native', choices=('temporal', 'reconstruction', 'compact'))
    ap.add_argument('--timings', action='store_true')
    ap.add_argument('--host-device', action='store_true')
    ap.add_argument('--scene-mode', choices=('id', 'alias'))
    args = ap.parse_args()
    if args.native:
        command = [args.exe, '--backend', 'vulkan', '--prepared-offscreen', '--hidden',
                   '--asset', args.asset, '--width', '64', '--height', '48',
                   '--frames', '3', '--divisor', '2', '--temporal']
        if args.native in ('reconstruction', 'compact'):
            command.append('--reconstruction')
        if args.native == 'compact':
            command.append('--compact-history')
        if args.timings:
            command.extend(['--timings', '--warmup', '1'])
        if args.host_device:
            command.append('--host-device-buffers')
        if args.scene_mode:
            command.extend(['--scene-resource-mode', args.scene_mode])
        run = subprocess.run(command, capture_output=True, text=True, timeout=150)
        print(run.stdout, end='')
        print(run.stderr, end='')
        assert run.returncode == 0, run.returncode
        resolve = 'Temporal/1' if args.native == 'temporal' else 'Reconstruction/2' if args.native == 'compact' else 'Reconstruction/1'
        assert 'CloudPreparedDemo/1 Prototype submitted=3 completed=3 resolve=' + resolve in run.stdout
        assert 'bufferPlacement=' + ('HostVisibleDeviceLocal/1' if args.host_device else 'Default') in run.stdout
        assert 'hotAllocations=0 presentation=false physicalPeakValidated=false' in run.stdout
        if args.scene_mode:
            assert 'CloudSceneBinding/1 Prototype resource=' in run.stdout
            assert 'accepted=3 completed=3 source=visibleSceneQueue loader=CloudAssetResource' in run.stdout
        if args.timings:
            samples = [line for line in run.stdout.splitlines() if line.startswith('CloudPreparedTimingSample/1')]
            assert len(samples) == 3
            assert 'frame=0 serial=1 measured=0 history=0' in samples[0]
            assert all('measured=1 history=1' in line for line in samples[1:])
            summaries = [line for line in run.stdout.splitlines() if line.startswith('CloudPreparedTimingSummary/1')]
            assert len(summaries) == 4 and all('observations=2 warmup=1' in line for line in summaries)
            assert 'physicalPeakValidated=false includesForeground=false presentation=false' in run.stdout
        return
    base = [args.exe, '--backend', 'null', '--asset', args.asset,
            '--width', '32', '--height', '24', '--frames', '3']
    cases = [
        (['--prepared-offscreen', '--prototype-frozen-batch'], 'CLOUD-DEMO-REFERENCE-PROFILE-CONFLICT'),
        (['--prepared-offscreen', '--reference-case', 'below'], 'CLOUD-DEMO-REFERENCE-PROFILE-CONFLICT'),
        (['--reference-case', 'below', '--quality', 'medium'], 'CLOUD-DEMO-REFERENCE-QUALITY-CONFLICT'),
        (['--reference-case', 'unknown'], 'CLOUD-DEMO-REFERENCE-CASE'),
        (['--scene-resource-mode', 'guess'], 'CLOUD-DEMO-SCENE-REFERENCE-MODE'),
        (['--scene-resource-mode', 'alias'], 'CLOUD-DEMO-SCENE-REQUIRES-PREPARED'),
        (['--prepared-offscreen', '--hidden', '--scene-resource-mode', 'id'],
         'RHI-RENDER-PASS-PREPARATION-NULL-VALIDATOR'),
        (['--prepared-offscreen', '--hidden', '--scene-resource-mode', 'alias'],
         'RHI-RENDER-PASS-PREPARATION-NULL-VALIDATOR'),
        (['--compact-history'], 'CLOUD-DEMO-COMPACT-REQUIRES-RECONSTRUCTION'),
        (['--host-device-buffers'], 'CLOUD-DEMO-PLACEMENT-REQUIRES-PREPARED'),
        (['--prepared-offscreen'], 'CLOUD-DEMO-PREPARED-OFFSCREEN-PROFILE'),
        (['--prepared-offscreen', '--hidden', '--timings'],
         'CLOUD-DEMO-TIMING-WARMUP'),
        (['--reconstruction'], 'CLOUD-DEMO-RECONSTRUCTION-REQUIRES-PREPARED'),
        (['--prepared-offscreen', '--hidden'],
         'RHI-RENDER-PASS-PREPARATION-NULL-VALIDATOR'),
        (['--prepared-offscreen', '--hidden', '--reconstruction', '--divisor', '1'],
         'CLOUD-RECONSTRUCTION-RESOLUTION'),
        (['--density-evaluation-budget', '0'], 'CLOUD-DEMO-WORK-BUDGET'),
        (['--prepared-offscreen', '--hidden', '--reconstruction', '--quality', 'medium',
          '--width', '1280', '--height', '720'], 'CLOUD-RECONSTRUCTION-WORK-BUDGET'),
        (['--prepared-offscreen', '--hidden', '--reconstruction', '--compact-history', '--quality', 'medium',
          '--width', '1280', '--height', '720'], 'CLOUD-RECONSTRUCTION-WORK-BUDGET'),
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
