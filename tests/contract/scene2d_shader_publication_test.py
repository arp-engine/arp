"""Real selected-entry liveness and Scene2D vertex shader publication contract."""
import argparse
import hashlib
from pathlib import Path
import sys
import tempfile
from unittest.mock import patch

import yaml

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import compile_shaders as compiler


def verify_roles(publication):
    node = yaml.safe_load(publication.read_text(encoding='utf-8'))
    for role in node['files'].values():
        path = publication.parent / role['path']
        assert hashlib.sha256(path.read_bytes()).hexdigest() == role['sha256'], path
    reflection = yaml.safe_load((publication.parent / node['files']['reflection']['path']).read_text())
    assert reflection['sourceSha256'] == node['files']['source']['sha256']
    assert node['matrixLayout'] == 'row-major'
    return node, reflection


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--directory', required=True, type=Path)
    parser.add_argument('--reflect', required=True)
    parser.add_argument('--module', default='scene_2d_vertices')
    parser.add_argument('--push-bytes', type=int, default=16)
    parser.add_argument('--vertex-entry', default='vertexMainExpanded')
    parser.add_argument('--push-stage', choices=('VS', 'FS'), default='VS')
    parser.add_argument('--forbid-required-extensions', action='store_true')
    args = parser.parse_args()
    name = args.module
    module, combined = verify_roles(args.directory / (name + '.graphics.module.yaml'))
    assert combined['pushConstantSizeBytes'] == args.push_bytes
    assert combined['pushConstantStages'] == args.push_stage
    assert [(e['stage'], e['entry']) for e in module['entries']] == [
        ('Vertex', args.vertex_entry), ('Fragment', 'fragmentMain')]
    for entry in module['entries']:
        reference = entry['publication']
        publication = args.directory / reference['path']
        assert hashlib.sha256(publication.read_bytes()).hexdigest() == reference['sha256']
        selected, reflection = verify_roles(publication)
        if args.forbid_required_extensions:
            target = yaml.safe_load(publication.with_suffix('.glsl-es-100.yaml').read_text())
            assert target['requiredExtensions'] == []
        spv = publication.parent / selected['files']['spirv']['path']
        active = entry['stage'] == ('Vertex' if args.push_stage == 'VS' else 'Fragment')
        assert compiler.selected_entry_has_push_constants(spv, args.reflect) == active
        assert compiler.selected_entry_push_stage(spv, args.reflect) == (args.push_stage if active else None)
        assert reflection['pushConstantSizeBytes'] == (args.push_bytes if active else 0)
        assert reflection['pushConstantStages'] == (args.push_stage if active else None)
        assert bool(reflection.get('pushConstantMembers')) == active
        assert (reflection['pushConstantWebGpuBinding'] is not None) == active
        assert (reflection['pushConstantMetalBufferIndex'] is not None) == active
        assert reflection['sourceSha256'] == combined['sourceSha256']
        # Real binaries disagreeing with reflector output must fail closed.
        with patch.object(compiler._bindings_codegen, 'reflect_push_constant_block',
                          return_value=None if active else object()):
            try:
                compiler.selected_entry_has_push_constants(spv, args.reflect)
            except RuntimeError as error:
                assert str(error) == 'SHADER-ENTRY-PUSH-REFLECTION-MISMATCH'
            else:
                raise AssertionError('contradictory binary/reflection accepted')
    with tempfile.TemporaryDirectory(prefix='arp-entry-push-') as temporary:
        malformed = Path(temporary) / 'bad.spv'
        malformed.write_bytes(b'not spirv')
        try:
            compiler.selected_entry_has_push_constants(malformed, args.reflect)
        except RuntimeError:
            pass
        else:
            raise AssertionError('malformed binary treated as inactive push')
    print(name + ' SPV/MSL/WGSL/reflection custody, selected VS/FS liveness and disagreement rejection passed')


if __name__ == '__main__':
    main()
