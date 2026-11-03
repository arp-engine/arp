"""Exercise default gameplay IO from a source-free, offline Debug package."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

import yaml


def run(executable, assets, shaders, output):
    with tempfile.TemporaryDirectory(prefix='arp-survivors-gameplay-package-') as folder:
        root = Path(folder)
        target = root / executable.name
        shutil.copyfile(executable, target)
        shutil.copytree(assets, root / 'assets')
        (root / 'shaders').mkdir()
        for suffix in ('.slang', '.spv', '.metal', '.wgsl', '.reflection.yaml', '.artifact-set.yaml'):
            name = 'scene_2d_sprites' + suffix
            shutil.copyfile(shaders / name, root / 'shaders' / name)
        before = {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                  for p in root.rglob('*') if p.is_file()}
        environment = os.environ.copy()
        environment['PATH'] = str(Path(os.environ['SystemRoot']) / 'System32')
        environment.pop('VULKAN_SDK', None)
        result = subprocess.run([str(target), '--hidden', '--autoplay', '--fixed-step',
                                 '--frames', '140', '--mute'], cwd=root, env=environment,
                                capture_output=True, text=True, timeout=60)
        assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
        for field in ('gameplaySource: NativeDirectory', 'gameplayReads: 9',
                      'gameplayIoRetired: true', 'scriptExecution: Interpreter',
                      'ready: true', 'frames: 140'):
            assert field in result.stdout, (field, result.stdout)
        after = {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in before}
        assert before == after, 'Runtime modified packaged inputs'
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(yaml.safe_dump(dict(
            type='arp.survivors-gameplay-package-smoke', contractVersion=1,
            status='Platform Validated', backend='Windows/Vulkan', productionReady=False,
            sourceTreePresent=False, developmentPathPresent=False,
            shaderPublications=6, gameplayFiles=9, sourceHashes=before,
            stdout=result.stdout, stderr=result.stderr,
            limits=['Debug startup only; not a Release/distribution/performance acceptance.',
                    'OS Vulkan driver remains required. Audio device is explicitly muted.']),
            sort_keys=False), encoding='utf-8')
        print('PASS default nine-text startup and Vulkan140 from source-free offline directory')
    return 0


if __name__ == '__main__':
    raise SystemExit(run(*(Path(arg).resolve() for arg in sys.argv[1:])))
