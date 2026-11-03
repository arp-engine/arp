"""Stage only the executable and its explicit publications, then start offline."""
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

import yaml


def run(executable, assets, shaders):
    with tempfile.TemporaryDirectory(prefix='arp-queen-native-hud-package-') as folder:
        root = Path(folder)
        target = root / executable.name
        shutil.copyfile(executable, target)
        # Public runtime metadata, exact character files and explicit original
        # Shader/target publications; no source tree, development tools or font
        # discovery path is placed in the staged directory.
        shutil.copytree(assets, root / 'assets')
        shutil.copytree(shaders, root / 'shaders')
        before = {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                  for p in root.rglob('*') if p.is_file()}
        result = subprocess.run([str(target), '--hidden', '--autoplay', '--frames', '18'], cwd=root,
                                capture_output=True, text=True, timeout=60)
        assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
        for field in ('characterSource: NativeDirectory', 'characterReads: 3',
                      'shaderSource: NativeDirectory', 'shaderReads: 36',
                      'hudSource: NativeDirectory', 'hudReads: 3', 'hudFontFamily: Droid Sans',
                      'frames: 18', 'phase: RUNNING'):
            assert field in result.stdout, result.stdout
        after = {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                 for p in root.rglob('*') if p.is_file()}
        assert before == after, 'Runtime modified packaged inputs'
        output = executable.parent / 'queen-native-hud-package-smoke.yaml'
        report = dict(type='arp.queen.native-package-smoke', contractVersion=1, success=True,
                      status='Platform Validated', backend='Windows/Vulkan', productionReady=False,
                      sourceTreePresent=False, cwdContainedOnlyPublications=True,
                      networkSourceSelected=False, sourceHashes=before, stdout=result.stdout,
                      stderr=result.stderr,
                      limits=['Debug package; not a Release/performance/distribution acceptance.',
                              'Validates bundled font use; OS system font availability is not altered.'])
        output.write_text(yaml.safe_dump(report, sort_keys=False), encoding='utf-8')
        print(f'PASS staged Native package without source tree; exact {len(before)} input hashes')
    return 0


if __name__ == '__main__':
    raise SystemExit(run(*(Path(arg).resolve(strict=True) for arg in sys.argv[1:])))
