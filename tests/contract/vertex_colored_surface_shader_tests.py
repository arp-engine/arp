"""Verify static vertex RGBA surface ABI in the actual generated targets."""
from pathlib import Path
import hashlib
import sys
import yaml

engine, generated = map(Path, sys.argv[1:3])
sys.path.insert(0, str(engine / 'scripts'))
from spirv_layout import SpirvLayoutFacts

stem = generated / 'vertex_colored_surface'
publication = yaml.safe_load(stem.with_suffix('.artifact-set.yaml').read_text(encoding='utf-8'))
assert publication['matrixLayout'] == 'row-major'
assert set(publication['files']) == {'source', 'spirv', 'metal', 'wgsl', 'reflection'}
for record in publication['files'].values():
    path = generated / record['path']
    assert path.resolve().parent == generated.resolve()
    assert hashlib.sha256(path.read_bytes()).hexdigest() == record['sha256']
assert stem.with_suffix('.slang').read_bytes() == (engine / 'shaders/renderer/vertex_colored_surface.slang').read_bytes()
r = yaml.safe_load(stem.with_suffix('.reflection.yaml').read_text(encoding='utf-8'))
assert r['pushConstantSizeBytes'] == 128
assert {m['name']: m['offset'] for m in r['pushConstantMembers']} == {
    'viewProjection': 0, 'worldBounds': 64, 'directionToLightAndAlpha': 80,
    'diffuseAndBorderWidth': 96, 'ambientAndMinimumBorder': 112}
matrix, = [m for m in r['pushConstantMembers'] if m['name'] == 'viewProjection']
assert matrix['matrixStride'] == 16 and matrix['rowMajor'] and matrix['size'] == 64
assert {(b['set'], b['binding'], b['type'], b['stageMask']) for b in r['bindings']} == {
    (0,0,'SampledTexture',2), (0,1,'Sampler',2)}
assert [(a['location'],a['format']) for a in r['vertexAttributes']] == [(0,'Float3'),(1,'Float3'),(2,'Float2'),(3,'Float4')]
facts = SpirvLayoutFacts(stem.with_suffix('.spv'))
assert [b['occupiedExtentBytes'] for b in facts.push_constant_layouts()] == [128]
assert not facts.buffer_layouts()
for target in ('metalBindingSpace','webGpuBindingSpace'):
    assert r[target+'Status'] == 'Available' and all(b['active'] for b in r[target])
assert r['pushConstantWebGpuBinding']['group'] not in {b['group'] for b in r['webGpuBindingSpace']}
print('Vertex RGBA surface: SPIR-V/MSL/WGSL publication, 128-byte matrix/color ABI and texture bindings passed')
