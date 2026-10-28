"""Validate the published Slang-only unified GUI ABI and ES100 opt-in."""
import argparse
import hashlib
from pathlib import Path
import yaml


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--programs',required=True)
    args=parser.parse_args()
    root=Path(args.programs)
    def load(path):return yaml.safe_load(path.read_text(encoding='utf8'))
    reflection=load(root/'gui_unified.reflection.yaml')
    assert reflection['pushConstantSizeBytes']==64
    assert [(x['name'],x['offset'],x['size']) for x in reflection['pushConstantMembers']]==[
        ('screenSize',0,8),('rectOrigin',8,8),('rectSize',16,8),('uvOrigin',24,8),
        ('uvSize',32,8),('flags',40,8),('color',48,16)]
    for suffix in ('slang','spv','metal','wgsl'):
        assert (root/('gui_unified.'+suffix)).stat().st_size>0
    program=load(root/'gui_unified.webgl-program.yaml')
    assert len(program['targets'])==2
    for target in program['targets']:
        path=root/target['path']
        assert hashlib.sha256(path.read_bytes()).hexdigest()==target['sha256']
        stage=load(path)
        assert stage['status']=='Available'
        assert stage['slang']['matrixLayout']=='row-major'
        glsl=path.parent/stage['glsl']['path']
        assert hashlib.sha256(glsl.read_bytes()).hexdigest()==stage['glsl']['sha256']
        if stage['stage']=='Fragment':
            assert stage['requiredExtensions']==['GL_OES_standard_derivatives']
            assert 'fwidth' in glsl.read_text()
        else:
            assert stage['uniformVectors']==7
            assert stage['requiredExtensions']==[]
    print('PASS: GuiUnified 64-byte ABI, SPIR-V/MSL/WGSL/ES100 and explicit derivatives')

if __name__=='__main__':main()
