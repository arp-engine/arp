"""Real Slang finite Graphics packages: stage custody, ABI and paired outputs."""
import argparse
import hashlib
from pathlib import Path
import re
import sys
import tempfile

import yaml

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from spirv_layout import SpirvLayoutFacts
from compile_shaders import apply_wgsl_storage_texture_declarations


def verify_storage_rewrite(source, wgsl, facts, stage):
    wgsl = wgsl.replace('\r\n', '\n')
    masks = {}
    for variable in facts.variables:
        descriptor_set = facts.decorations.get((variable[1], 34))
        binding = facts.decorations.get((variable[1], 33))
        if descriptor_set is not None and binding is not None:
            masks[(descriptor_set[0], binding[0])] = 1 if stage == 'Vertex' else 2
    declarations = [('StorageOutput', 'rgba16float', 'write')]
    with tempfile.TemporaryDirectory(prefix='arp-graphics-rewrite-') as temporary:
        path = Path(temporary) / 'selected.wgsl'
        path.write_text(wgsl, encoding='utf-8')
        apply_wgsl_storage_texture_declarations(path, source, declarations, masks)
        assert path.read_text(encoding='utf-8') == wgsl
        if stage == 'Vertex':
            assert (0, 1) not in masks
            return
        assert masks[(0, 1)] == 2
        missing, removed = re.subn(r'^.*var\s+StorageOutput\w*\s*:.*\n', '', wgsl, flags=re.M)
        assert removed == 1
        path.write_text(missing, encoding='utf-8')
        try:
            apply_wgsl_storage_texture_declarations(path, source, declarations, masks)
        except RuntimeError as error:
            assert 'storage texture format/access rewrite failed' in str(error)
        else:
            raise AssertionError('Active SPIR-V storage texture missing from WGSL was accepted')
        assert path.read_text(encoding='utf-8') == missing
        path.write_text(wgsl, encoding='utf-8')
        try:
            apply_wgsl_storage_texture_declarations(
                path, source.replace('StorageOutput', 'UnregisteredOutput'), declarations, masks)
        except RuntimeError as error:
            assert str(error) == 'SHADER-WGSL-STORAGE-TEXTURE-DECLARATION'
        else:
            raise AssertionError('Missing authored storage declaration was accepted')

def read_roles(directory, publication):
    assert set(publication['files']) == {'source', 'spirv', 'metal', 'wgsl', 'reflection'}
    roles = {}
    for role, record in publication['files'].items():
        path = (directory / record['path']).resolve()
        path.relative_to(directory.resolve())
        value = path.read_bytes()
        assert value and hashlib.sha256(value).hexdigest() == record['sha256'], (path, role)
        roles[role] = value
    return roles

def verify(directory, readonly):
    name = directory.name
    module_path = directory / (name + '.graphics.module.yaml')
    module = yaml.safe_load(module_path.read_text(encoding='utf-8'))
    assert module['type'] == 'GraphicsShaderModuleArtifact' and module['contractVersion'] == 1
    assert module['matrixLayout'] == 'row-major'
    roles = read_roles(directory, module)
    authored = (ROOT / 'tests/fixtures' / (name + '.slang')).read_bytes()
    assert roles['source'] == authored
    source_sha = hashlib.sha256(authored).hexdigest()
    reflection = yaml.safe_load(roles['reflection'])
    assert reflection['sourceSha256'] == source_sha and not reflection['pushConstantSizeBytes']
    expected = {(0, 0, 'ReadOnlyStorageBuffer' if readonly else 'UniformBuffer', 1, 2),
        (0, 1, 'StorageTexture', 1, 2), (0, 2, 'SampledTexture', 1, 2), (0, 3, 'Sampler', 1, 2)}
    assert {(b['set'], b['binding'], b['type'], b['arrayCount'], b['stageMask'])
            for b in reflection['bindings']} == expected
    assert [(entry['stage'], entry['entry']) for entry in module['entries']] == [
        ('Vertex', 'vertexMain'), ('Fragment', 'fragmentMain')]
    for entry in module['entries']:
        stage = entry['stage']
        path = (directory / entry['publication']['path']).resolve()
        path.relative_to(directory.resolve())
        content = path.read_bytes()
        assert hashlib.sha256(content).hexdigest() == entry['publication']['sha256']
        child = yaml.safe_load(content)
        assert child['type'] == 'SingleEntryShaderArtifact' and child['contractVersion'] == 3
        assert child['entry'] == entry['entry'] and child['stage'] == stage and child['matrixLayout'] == 'row-major'
        selected = read_roles(path.parent, child)
        assert selected['source'] == authored
        facts = SpirvLayoutFacts(path.parent / child['files']['spirv']['path'])
        entry_models = [operands[0] for _, opcode, operands in facts.instructions if opcode == 15]
        assert entry_models == ([0] if stage == 'Vertex' else [4])
        child_reflection = yaml.safe_load(selected['reflection'])
        assert child_reflection['sourceSha256'] == source_sha
        assert child_reflection['webGpuBindingSpaceStatus'] == 'Available'
        assert child_reflection['metalBindingSpaceStatus'] == 'Available'
        declaration = child['bufferDeclarationLayout']
        assert declaration['status'] == 'DeclarationOnly'
        assert declaration['spirvSha256'] == child['files']['spirv']['sha256']
        wgsl = selected['wgsl'].decode('utf-8')
        verify_storage_rewrite(authored.decode('utf-8'), wgsl, facts, stage)
        if stage == 'Vertex':
            assert not child_reflection['bindings'] and not declaration['bindings']
            assert child_reflection['webGpuBindingSpaceGroupCount'] == 0
            assert all(not record['active'] for record in child_reflection['webGpuBindingSpace'])
            assert '@vertex' in wgsl and '@fragment' not in wgsl
            assert 'texture_storage_2d' not in wgsl and 'textureStore' not in wgsl
            assert not [opcode for _, opcode, _ in facts.instructions if opcode == 99]
            continue
        assert {(b['set'], b['binding'], b['type'], b['arrayCount'], b['stageMask'])
                for b in child_reflection['bindings']} == expected
        assert child_reflection['webGpuBindingSpaceGroupCount'] == 1
        assert {(b['set'], b['binding'], b['group'], b['webGpuBinding'], b['physicalType'])
                for b in child_reflection['webGpuBindingSpace']} == {(s, b, 0, b, t) for s, b, t, _, _ in expected}
        assert all(record['active'] for record in child_reflection['webGpuBindingSpace'])
        assert len(declaration['bindings']) == 1
        buffer = declaration['bindings'][0]
        assert buffer['set'] == buffer['binding'] == 0
        assert buffer['hasRuntimeArray'] is readonly
        if readonly:
            assert int(buffer['runtimeArrayStrideBytesHex'], 16) == 4
            assert int(buffer['runtimeElementOccupiedExtentBytesHex'], 16) == 4
            assert re.search(r'var<storage,\s*read>\s+\w+\s*:\s*array<u32>', wgsl)
        else:
            assert int(buffer['staticOccupiedExtentBytesHex'], 16) == 16
            assert 'var<uniform>' in wgsl and 'vec4<f32>' in wgsl
        assert '@fragment' in wgsl and '@vertex' not in wgsl
        assert 'textureSampleLevel' in wgsl and 'textureStore' in wgsl
        assert re.search(r'texture_storage_2d<rgba16float,\s*write>', wgsl)
        # The executable SPIR-V writes exactly the same computed SSA value to
        # the storage image and the location-zero Fragment attachment output.
        writes = [operands for _, opcode, operands in facts.instructions if opcode == 99]
        outputs = [value[1] for value in facts.variables if value[2] == 3
                   and facts.decorations.get((value[1], 30)) == (0,)]
        assert len(writes) == len(outputs) == 1
        stores = [operands for _, opcode, operands in facts.instructions if opcode == 62 and operands[0] == outputs[0]]
        assert len(stores) == 1 and writes[0][2] == stores[0][1]
    return dict(bufferType='readonly-storage' if readonly else 'uniform', selectedStages=2,
        fragmentBindings=4, vertexBindings=0, sharedStorageAndAttachmentValueProven=True,
        activeStorageRewriteRefusals=2, inactiveStorageRewriteVerified=True)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture-root', type=Path, required=True)
    root = parser.parse_args().fixture_root
    for name, readonly in [('original_general_graphics_shader_resources', False),
                           ('original_general_graphics_readonly_shader_resources', True)]:
        result = verify(root / name, readonly)
        print('GENERAL_FINITE_GRAPHICS_ARTIFACT_OK', result)
    print('Two actual Slang modules, four selected entries and five roles each verified; native draw/readback remains unvalidated')

if __name__ == '__main__':
    main()
