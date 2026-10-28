"""SceneOpaqueProducer/1 selected-entry ABI, including original matrix byte mapping."""
import argparse
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
from spirv_layout import SpirvLayoutFacts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--directory', type=Path, required=True)
    directory = ap.parse_args().directory
    source = (Path(__file__).resolve().parents[2]/'shaders/renderer/scene_opaque.slang').read_text(encoding='utf-8')
    assert '    float4x4 viewProjection;' in source
    assert 'row_major float4x4' not in source and 'column_major float4x4' not in source
    assert 'mul(SceneFrame.viewProjection, vertex.position)' in source
    for stage in ('', 'Vertex', 'Fragment'):
        prefix = directory/stage/'scene_opaque'
        facts = SpirvLayoutFacts(prefix.with_suffix('.spv'))
        assert not facts.push_constant_layouts()
        layouts = facts.buffer_layouts()
        assert layouts[(0,0)]['staticOccupiedExtentBytes'] == 112, layouts
        names, members = {}, {}
        for _,opcode,operands in facts.instructions:
            if opcode in (5,6):
                start=1 if opcode==5 else 2
                name=struct.pack('<'+'I'*len(operands[start:]),*operands[start:]).split(b'\0')[0].decode()
                if opcode==5: names[operands[0]]=name
                else: members[(operands[0],operands[1])]=name
        constants=next(i for i,n in names.items() if n=='SceneOpaqueFrameConstants_std140')
        for i,offset in enumerate((0,64,80,96)):
            assert facts.member_decoration(constants,i,35)==offset
        assert [members[(constants,i)] for i in range(4)] == ['viewProjection','directionIntensity','lightColor','ambientColor']
        facts.member_decoration(constants,0,5,True)
        assert (constants,0,4) not in facts.members
        assert facts.member_decoration(constants,0,7)==16
        if stage != 'Fragment':
            assert set(layouts)=={(0,0),(0,1)}
            assert layouts[(0,1)]['runtimeArrayStrideBytes']==48
            vertex=next(i for i,n in names.items() if n=='SceneOpaqueVertex_std430')
            for i,offset in enumerate((0,16,32)):
                assert facts.member_decoration(vertex,i,35)==offset
            assert [members[(vertex,i)] for i in range(3)]==['position','normal','albedo']
        else:
            assert set(layouts)=={(0,0)}
        for suffix in ('.metal','.wgsl','.reflection.yaml'):
            assert prefix.with_suffix(suffix).stat().st_size>0
        reflection=prefix.with_suffix('.reflection.yaml').read_text(encoding='utf-8')
        assert 'pushConstantSizeBytes: 0' in reflection
    print('SceneOpaqueProducer/1: UBO112, storage48, matrix ColMajor/stride16, no push; SPIR-V/MSL/WGSL')


if __name__=='__main__': main()
