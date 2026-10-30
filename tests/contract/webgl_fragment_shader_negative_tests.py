"""Rehashed fragment-target downgrades and depth spelling must fail closed."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile
import yaml


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', required=True)
    parser.add_argument('--directory', required=True, type=Path)
    args = parser.parse_args()
    cases = [('version', 'SHADER-WEBGL1-TARGET-CONTRACT'),
             ('extension', 'SHADER-WEBGL1-EXTENSION-BINARY-MISMATCH'),
             ('spelling', 'SHADER-WEBGL1-FRAGMENT-DEPTH-LOWERING')]
    with tempfile.TemporaryDirectory(prefix='arp-fragment-contract-') as temporary:
        for case, expected in cases:
            root = Path(temporary) / case
            shutil.copytree(args.directory, root)
            program_path = root / 'editor_viewport_environment.webgl-program.yaml'
            program = yaml.safe_load(program_path.read_text(encoding='utf8'))
            target_path = root / program['targets'][1]['path']
            target = yaml.safe_load(target_path.read_text(encoding='utf8'))
            if case == 'version':
                target['contractVersion'] = 1
            elif case == 'extension':
                target['requiredExtensions'].remove('GL_EXT_frag_depth')
            else:
                glsl = target_path.parent / target['glsl']['path']
                glsl.write_text(glsl.read_text(encoding='utf8').replace(
                    'gl_FragDepthEXT', 'gl_FragDepth'), encoding='utf8')
                target['glsl']['sha256'] = hashlib.sha256(glsl.read_bytes()).hexdigest()
            target_path.write_text(yaml.safe_dump(target, sort_keys=False), encoding='utf8')
            program['targets'][1]['sha256'] = hashlib.sha256(target_path.read_bytes()).hexdigest()
            program_path.write_text(yaml.safe_dump(program, sort_keys=False), encoding='utf8')
            subprocess.run([args.exe, str(program_path), expected], check=True)
    print('PASS: fragment target version, extension and ES100 spelling attacks rejected')


if __name__ == '__main__':
    main()
