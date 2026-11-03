"""Validate the published display pass ABI and actual binary descriptors."""
from pathlib import Path
import hashlib
import sys
import yaml

engine, generated = map(Path, sys.argv[1:3])
sys.path.insert(0, str(engine / 'scripts'))
from spirv_layout import SpirvLayoutFacts

stem = generated / 'display_transfer'
publication = yaml.safe_load(stem.with_suffix('.artifact-set.yaml').read_text(encoding='utf-8'))
assert set(publication['files']) == {'source', 'spirv', 'metal', 'wgsl', 'reflection'}
for record in publication['files'].values():
    path = generated / record['path']
    assert path.resolve().parent == generated.resolve()
    assert hashlib.sha256(path.read_bytes()).hexdigest() == record['sha256']
assert stem.with_suffix('.slang').read_bytes() == (engine / 'shaders/renderer/display_transfer.slang').read_bytes()
r = yaml.safe_load(stem.with_suffix('.reflection.yaml').read_text(encoding='utf-8'))
assert r['pushConstantSizeBytes'] == 16
assert {m['name']: m['offset'] for m in r['pushConstantMembers']} == {
    'inputUvScaleBias': 0, 'encodeSrgb': 8, 'reserved': 12}
assert {(b['set'], b['binding'], b['type'], b['stageMask']) for b in r['bindings']} == {
    (0, 0, 'SampledTexture', 2), (0, 1, 'Sampler', 2)}
facts = SpirvLayoutFacts(stem.with_suffix('.spv'))
assert [b['occupiedExtentBytes'] for b in facts.push_constant_layouts()] == [16]
assert not facts.buffer_layouts()
assert not r['vertexAttributes']
for target in ('metalBindingSpace', 'webGpuBindingSpace'):
    assert r[target + 'Status'] == 'Available'
    assert all(b['active'] for b in r[target])
assert r['pushConstantWebGpuBinding']['group'] not in {b['group'] for b in r['webGpuBindingSpace']}
print('SDR display transfer: sealed SPIR-V/MSL/WGSL publication, 16-byte ABI and sampled-color bindings passed')
