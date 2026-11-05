"""SourceTerrainArray/1: compiled target and shared surface ABI verification."""
from pathlib import Path
import hashlib
import sys
import yaml

engine, generated = map(Path, sys.argv[1:3])
sys.path.insert(0, str(engine / 'scripts'))
from spirv_layout import SpirvLayoutFacts
stem = generated / 'terrain_layer_surface'
publication = yaml.safe_load(stem.with_suffix('.artifact-set.yaml').read_text(encoding='utf8'))
assert publication['matrixLayout'] == 'row-major'
assert set(publication['files']) == {'source', 'spirv', 'metal', 'wgsl', 'reflection'}
for record in publication['files'].values():
    path = generated / record['path']
    assert path.resolve().parent == generated.resolve()
    assert hashlib.sha256(path.read_bytes()).hexdigest() == record['sha256']
source = (engine/'shaders/renderer/terrain_layer_surface.slang').read_text(encoding='utf8')
assert stem.with_suffix('.slang').read_text(encoding='utf8') == source
shared = (engine/'shaders/renderer/layer_surface_body.slang').read_text(encoding='utf8')
for name in ['terrain_layer_surface', 'skinned_layer_surface']:
    text = (engine/f'shaders/renderer/{name}.slang').read_text(encoding='utf8')
    assert '#include "layer_surface_body.slang"' in text
    assert 'row_major float4x4' not in text and '    float4x4 viewProjection;' in text
assert 'mul(gSurface.viewProjection, float4(worldPosition, 1.0))' in shared
assert 'float3(input.uv, input.layer)' in source and 'nointerpolation uint layer' in source
r = yaml.safe_load(stem.with_suffix('.reflection.yaml').read_text(encoding='utf8'))
assert r['pushConstantSizeBytes'] == 128
assert {m['name']:m['offset'] for m in r['pushConstantMembers']} == {
    'viewProjection':0, 'tintAndAlpha':64, 'surfaceParameters':80,
    'lightDirectionAndAmbient':96, 'lightColorAndIntensity':112}
assert [(a['location'],a['format']) for a in r['vertexAttributes']] == list(enumerate(
    ['Float3','Float3','Float2','Float4','Float4','Float']))
assert {(b['set'],b['binding'],b['type'],b['stageMask']) for b in r['bindings']} == {
    (0,0,'UniformBuffer',1), (1,0,'SampledTexture',2), (1,1,'Sampler',2)}
facts = SpirvLayoutFacts(stem.with_suffix('.spv'))
push, = facts.push_constant_layouts()
assert push['occupiedExtentBytes'] == 128
assert (push['blockTypeId'], 0, 5) in facts.members
assert (push['blockTypeId'], 0, 4) not in facts.members
images = [args for _, op, args in facts.instructions if op == 25]
assert len(images) == 1 and images[0][2] == 1 and images[0][3] != 1 and images[0][4] == 1, 'SPIR-V must sample a color 2D array'
assert any(op == 71 and args[1] == 14 for _,op,args in facts.instructions), 'Layer must be flat'
for target in ['metalBindingSpace','webGpuBindingSpace']:
    assert r[target+'Status'] == 'Available' and all(b['active'] for b in r[target])
assert 'texture_2d_array<f32>' in stem.with_suffix('.wgsl').read_text(encoding='utf8')
assert 'texture2d_array<float' in stem.with_suffix('.metal').read_text(encoding='utf8')
print('Terrain array shader: shared math, camera ABI, flat scalar layer, SPIR-V/MSL/WGSL arrays passed')
