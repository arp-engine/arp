"""Validate the authored mixed read-only Slang publication and buffer ABI."""
import argparse
import hashlib
import pathlib
import re
import sys
import yaml

ROOT=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from spirv_layout import SpirvLayoutFacts

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--fixture-dir',type=pathlib.Path,required=True)
    directory=parser.parse_args().fixture_dir
    name='original_general_readonly_shader_resources'
    publication=yaml.safe_load((directory/f'{name}.computeMain.entry.yaml').read_text())
    assert publication['type']=='SingleEntryShaderArtifact' and publication['contractVersion']==3
    assert publication['matrixLayout']=='row-major' and publication['entry']=='computeMain' and publication['stage']=='Compute'
    roles={}
    for role in ('source','spirv','metal','wgsl','reflection'):
        record=publication['files'][role]
        data=(directory/record['path']).read_bytes()
        assert data and hashlib.sha256(data).hexdigest()==record['sha256'],role
        roles[role]=data
    assert roles['source']==(ROOT/f'tests/fixtures/{name}.slang').read_bytes()
    reflection=yaml.safe_load(roles['reflection'])
    expected={(0,0,'ReadOnlyStorageBuffer',1,4),(0,1,'StorageTexture',1,4),
              (0,2,'SampledTexture',1,4),(0,3,'Sampler',1,4)}
    assert {(b['set'],b['binding'],b['type'],b['arrayCount'],b['stageMask']) for b in reflection['bindings']}==expected
    assert {(b['set'],b['binding'],b['group'],b['webGpuBinding'],b['physicalType']) for b in reflection['webGpuBindingSpace']}=={
        (s,b,0,b,t) for s,b,t,_,_ in expected}
    declaration=publication['bufferDeclarationLayout']
    assert declaration['status']=='DeclarationOnly' and declaration['spirvSha256']==publication['files']['spirv']['sha256']
    assert len(declaration['bindings'])==1
    buffer=declaration['bindings'][0]
    assert buffer['set']==0 and buffer['binding']==0 and buffer['hasRuntimeArray'] is True
    assert int(buffer['runtimeArrayStrideBytesHex'],16)==4
    assert int(buffer['runtimeElementOccupiedExtentBytesHex'],16)==4
    facts=SpirvLayoutFacts(directory/publication['files']['spirv']['path'])
    assert {(facts.decoration(target,34),facts.decoration(target,33)) for target,kind in facts.decorations if kind==33}=={(0,b) for b in range(4)}
    wgsl=roles['wgsl'].decode()
    assert re.search(r'var<storage,\s*read>\s+\w+\s*:\s*array<u32>',wgsl)
    assert 'textureSampleLevel' in wgsl and roles['metal'].strip()
    assert not reflection['pushConstantSizeBytes']
    print('GENERAL_READONLY_ARTIFACT_OK authored-Slang roles5 SPIRV-MSL-WGSL-reflection mixed4 read-u32-stride4 exact-physical-bindings no-execution-authority')

if __name__=='__main__':main()
