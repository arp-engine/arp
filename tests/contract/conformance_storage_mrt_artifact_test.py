"""Real Slang storage->MRT publications, exact stage ABI and target custody."""
import argparse
import hashlib
from pathlib import Path
import re
import sys
import tempfile
import yaml

from original_general_graphics_artifact_test import read_roles

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from spirv_layout import SpirvLayoutFacts
from compile_shaders import apply_wgsl_storage_texture_declarations


def verify(directory):
    workload = yaml.safe_load((ROOT / 'tests/fixtures/rhi_conformance_storage_mrt.yaml').read_text())
    cases = [('', 'conformance_storage_mrt', 'graphics.module', None),
             ('Vertex', 'conformance_storage_mrt', 'vertexMain.entry', 'Vertex'),
             ('Fragment', 'conformance_storage_mrt', 'fragmentMain.entry', 'Fragment'),
             ('Compute', 'conformance_storage_fill', 'computeMain.entry', 'Compute')]
    for child, name, suffix, stage in cases:
        folder = directory / child
        publication = yaml.safe_load((folder / (name + '.' + suffix + '.yaml')).read_text())
        assert publication['matrixLayout'] == 'row-major'
        roles = read_roles(folder, publication)
        source = (ROOT / 'tests/fixtures' / (name + '.slang')).read_bytes()
        assert roles['source'] == source
        key = 'computeSourceSha256' if stage == 'Compute' else 'shaderSourceSha256'
        assert hashlib.sha256(source).hexdigest() == workload[key]
        reflection = yaml.safe_load(roles['reflection'])
        assert reflection['sourceSha256'] == workload[key]
        assert not reflection['pushConstantSizeBytes']
        expected = set() if stage == 'Vertex' else {
            (0, 'ReadOnlyStorageBuffer', 4 if stage == 'Compute' else 2),
            (1, 'StorageTexture' if stage == 'Compute' else 'SampledTexture', 4 if stage == 'Compute' else 2)}
        assert {(b['binding'], b['type'], b['stageMask']) for b in reflection['bindings']} == expected
        assert all(b['set'] == 0 and b['arrayCount'] == 1 for b in reflection['bindings'])
        assert reflection['webGpuBindingSpaceStatus'] == reflection['metalBindingSpaceStatus'] == 'Available'
        facts = SpirvLayoutFacts(folder / publication['files']['spirv']['path'])
        models = [o[0] for _, opcode, o in facts.instructions if opcode == 15]
        assert sorted(models) == ({None: [0, 4], 'Vertex': [0], 'Fragment': [4], 'Compute': [5]}[stage])
        writes = [o for _, opcode, o in facts.instructions if opcode == 99]
        assert len(writes) == (1 if stage == 'Compute' else 0)
        if stage in ('Fragment', None):
            locations = {facts.decorations.get((v[1], 30)) for v in facts.variables if v[2] == 3}
            assert (0,) in locations and (1,) in locations
            wgsl = roles['wgsl'].decode()
            assert 'textureLoad' in wgsl and '@location(0)' in wgsl and '@location(1)' in wgsl
            assert 'textureStore' not in wgsl
        if stage == 'Compute':
            wgsl = roles['wgsl'].decode().replace('\r\n', '\n')
            assert re.search(r'texture_storage_2d<rgba16float,\s*write>', wgsl)
            assert 'textureStore' in wgsl and '@workgroup_size(1, 1, 1)' in wgsl
            with tempfile.TemporaryDirectory(prefix='arp-storage-mrt-') as temp:
                path = Path(temp) / 'selected.wgsl'
                path.write_text(wgsl)
                declarations = [('StorageOutput', 'rgba16float', 'write')]
                apply_wgsl_storage_texture_declarations(path, source.decode(), declarations, {(0, 0): 4, (0, 1): 4})
                assert path.read_text() == wgsl
                try:
                    apply_wgsl_storage_texture_declarations(path, source.decode().replace('StorageOutput', 'Unknown'), declarations, {(0, 0): 4, (0, 1): 4})
                except RuntimeError as error:
                    assert str(error) == 'SHADER-WGSL-STORAGE-TEXTURE-DECLARATION'
                else:
                    raise AssertionError('Unregistered storage rewrite accepted')
    print('CONFORMANCE_STORAGE_MRT_ARTIFACT_OK: 4 publications, SPIR-V/MSL/WGSL/reflection, 3 selected stages')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    verify(parser.parse_args().directory)
