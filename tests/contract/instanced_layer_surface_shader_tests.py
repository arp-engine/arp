"""Verify InstancedLayerSurface/1 against its actual three compiled targets."""
import hashlib
from pathlib import Path
import sys

import yaml

engine, generated = map(Path, sys.argv[1:3])
sys.path.insert(0, str(engine/'scripts'))
from spirv_layout import SpirvLayoutFacts

stem=generated/'instanced_layer_surface'
publication=yaml.safe_load(stem.with_suffix('.artifact-set.yaml').read_text(encoding='utf-8'))
assert publication['matrixLayout']=='row-major'
assert set(publication['files'])=={'source','spirv','metal','wgsl','reflection'}
for record in publication['files'].values():
    path=generated/record['path']
    assert path.resolve().parent==generated.resolve()
    assert hashlib.sha256(path.read_bytes()).hexdigest()==record['sha256']
assert stem.with_suffix('.slang').read_bytes()==(engine/'shaders/renderer/instanced_layer_surface.slang').read_bytes()
r=yaml.safe_load(stem.with_suffix('.reflection.yaml').read_text(encoding='utf-8'))
assert r['pushConstantSizeBytes']==128
assert {m['name']:m['offset'] for m in r['pushConstantMembers']}=={
    'viewProjection':0,'tintAndAlpha':64,'surfaceParameters':80,'lightDirectionAndAmbient':96,'lightColorAndIntensity':112}
matrix,=[m for m in r['pushConstantMembers'] if m['name']=='viewProjection']
assert matrix['rowMajor'] and matrix['matrixStride']==16 and matrix['size']==64
assert {(b['set'],b['binding'],b['type'],b['stageMask']) for b in r['bindings']}=={
    (0,0,'ReadOnlyStorageBuffer',1),(1,0,'SampledTexture',2),(1,1,'Sampler',2)}
formats=['Float3','Float3','Float2']+['Float4']*9+['Float2']
assert [(a['location'],a['format']) for a in r['vertexAttributes']]==list(enumerate(formats))
facts=SpirvLayoutFacts(stem.with_suffix('.spv'))
assert [b['occupiedExtentBytes'] for b in facts.push_constant_layouts()]==[128]
buffers=facts.buffer_layouts()
assert set(buffers)=={(0,0)}
assert buffers[(0,0)]['hasRuntimeArray'] and buffers[(0,0)]['runtimeArrayOffsetBytes']==0
assert buffers[(0,0)]['runtimeArrayStrideBytes']==buffers[(0,0)]['runtimeElementOccupiedExtentBytes']==16
for target in ['metalBindingSpace','webGpuBindingSpace']:
    assert r[target+'Status']=='Available' and all(b['active'] for b in r[target])
assert r['pushConstantMetalBufferIndex'] not in {b['metalSlot'] for b in r['metalBindingSpace'] if b['physicalType']=='ReadOnlyStorageBuffer'}
assert r['pushConstantWebGpuBinding']['group'] not in {b['group'] for b in r['webGpuBindingSpace']}
print('Instanced surface SPIR-V/MSL/WGSL: 128-byte push, 13 attributes, readonly pose rows with 16-byte stride verified')
