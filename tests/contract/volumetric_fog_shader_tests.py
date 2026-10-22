"""Binary ABI checks shared by all four fog lowerings."""
import argparse
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
from spirv_layout import SpirvLayoutFacts


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--directory', type=Path, required=True)
    args=parser.parse_args()
    expected={'fog_inject': {1:128,2:32},'fog_scatter': {1:32,3:16},
        'fog_temporal': {1:32,2:16,3:32,5:32},'fog_integrate': {1:32,2:16},
        'fog_composite': {1:16},'fog_demo_glass': {1:16}}
    for name,strides in expected.items():
        facts=SpirvLayoutFacts(args.directory/(name+'.spv'))
        layouts=facts.buffer_layouts()
        assert layouts[(0,0)]['staticOccupiedExtentBytes']==784, (name,layouts)
        for binding,stride in strides.items():
            assert layouts[(0,binding)]['runtimeArrayStrideBytes']==stride, (name,binding)
        names={}
        member_names={}
        for _,opcode,operands in facts.instructions:
            if opcode in (5,6):
                prefix=1 if opcode==5 else 2
                raw=struct.pack('<'+'I'*len(operands[prefix:]),*operands[prefix:])
                text=raw.split(b'\0')[0].decode()
                if opcode==5:names[operands[0]]=text
                else:member_names[(operands[0],operands[1])]=text
        for (identifier,index),member in member_names.items():
            if member in ('inverseViewProjection','view','previousViewProjection','previousView','worldToLocal'):
                facts.member_decoration(identifier,index,5,True)
                assert (identifier,index,4) not in facts.members
                assert facts.member_decoration(identifier,index,7)==16
        constants=next(i for i,n in names.items() if n=='FogConstants_std140')
        offsets=[0,64,128,192,256,512,576,592,608,624,640,656,672,688,704,720,736,752,768]
        for index,offset in enumerate(offsets):
            assert facts.member_decoration(constants,index,35)==offset,(name,index)
        assert not facts.push_constant_layouts()
        assert 'pushConstantSizeBytes: 0' in (args.directory/(name+'.reflection.yaml')).read_text()
        for suffix in ('.metal','.wgsl'):
            assert (args.directory/(name+suffix)).stat().st_size>0
    print('VolumetricFog/1 shader ABI: 784B uniform, 128/32/16B strides, matrix layout, three targets')


if __name__=='__main__':main()
