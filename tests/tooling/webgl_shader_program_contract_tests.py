"""Rehashed metadata attacks and ES100 subset refusal against real artifacts."""
import argparse
import copy
import hashlib
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import yaml

sys.path.insert(0,str(Path(__file__).resolve().parents[2] / 'scripts'))
from webgl_shader_artifact import WebGlShaderTargetCompiler, publish_program
from spirv_layout import SpirvLayoutFacts


def main():
    ap=argparse.ArgumentParser()
    for name in ('exe','directory','library','slangc','reflect'): ap.add_argument('--'+name,required=True,type=Path)
    args=ap.parse_args()
    template=args.directory.resolve()
    cases=[
        ('target-version', lambda p:p.update(contractVersion=2),'SHADER-WEBGL1-TARGET-CONTRACT'),
        ('precision',lambda p:p.update(precision='Mediump'),'SHADER-WEBGL1-TARGET-CONTRACT'),
        ('coordinates',lambda p:p.update(coordinates='Guessed'),'SHADER-WEBGL1-TARGET-CONTRACT'),
        ('matrix',lambda p:p.update(matrixLayout='column-major'),'SHADER-WEBGL1-TARGET-CONTRACT'),
        ('offset',lambda p:p['blocks'][0]['leaves'][0].update(offset=16),'SHADER-WEBGL1-LEAF-BINARY-MISMATCH'),
        ('stride',lambda p:p['blocks'][0]['leaves'][0].update(matrixStride=32),'SHADER-WEBGL1-LEAF-BINARY-MISMATCH'),
        ('array-name',lambda p:p['blocks'][0]['leaves'][2].update(name='arpUbo_0_0.m2[0]'),'SHADER-WEBGL1-LEAF-BINARY-MISMATCH'),
        ('block-size',lambda p:p['blocks'][0].update(size=160),'SHADER-WEBGL1-BLOCK-BINARY-MISMATCH'),
        ('leaf-missing',lambda p:p['blocks'][0]['leaves'].pop(),'SHADER-WEBGL1-LEAF-COVERAGE'),
        ('attribute',lambda p:p['interface']['inputs'][0].update(location=9),'SHADER-WEBGL1-INTERFACE-BINARY-MISMATCH'),
        ('vector-count',lambda p:p.update(uniformVectors=1),'SHADER-WEBGL1-UNIFORM-VECTOR-COUNT'),
        ('options',lambda p:p['options'].update(fixupDepth=False),'SHADER-WEBGL1-OPTIONS'),
        ('converter',lambda p:p['converter'].update(apiVersion=[0,67,0]),'SHADER-WEBGL1-CONVERTER-IDENTITY'),
        ('unknown-extension',lambda p:p.update(requiredExtensions=['GL_FAKE']),'SHADER-WEBGL1-EXTENSION-UNSUPPORTED'),
        ('schema',lambda p:p.update(nativeLocation=7),'SHADER-WEBGL1-SCHEMA'),
    ]
    with tempfile.TemporaryDirectory(prefix='arp-webgl-program-') as temporary:
        root=Path(temporary)/'program'
        def fresh():
            if root.exists(): shutil.rmtree(root)
            shutil.copytree(template,root)
            program=root/'webgl1_graphics.webgl-program.yaml'
            doc=yaml.safe_load(program.read_text())
            target=root/doc['targets'][0]['path']
            return program,doc,target,yaml.safe_load(target.read_text())
        def reject(program,expected):
            result=subprocess.run([str(args.exe.resolve()),str(root),'unused',expected],capture_output=True,text=True)
            if result.returncode: raise AssertionError(result.stdout+result.stderr)
            print(result.stdout.strip())
        for name,mutate,expected in cases:
            program,doc,target,p=fresh(); mutate(p)
            target.write_text(yaml.safe_dump(p,sort_keys=False))
            doc['targets'][0]['sha256']=hashlib.sha256(target.read_bytes()).hexdigest()
            program.write_text(yaml.safe_dump(doc,sort_keys=False)); reject(program,expected)
        program,doc,target,p=fresh(); target.write_bytes(target.read_bytes()+b'\n# tampered\n'); reject(program,'SHADER-ENTRY-FILE-HASH')
        program,doc,target,p=fresh(); doc['targets'][0]['path']='../Vertex/escape.yaml'
        program.write_text(yaml.safe_dump(doc)); reject(program,'SHADER-WEBGL1-TARGET-PATH')
        program,doc,target,p=fresh(); doc['textureUnits'][0]['unit']=7
        program.write_text(yaml.safe_dump(doc)); reject(program,'SHADER-WEBGL1-TEXTURE-UNIT-MAPPING')
        program,doc,target,p=fresh(); glsl=target.parent/p['glsl']['path']; glsl.unlink(); reject(program,'SHADER-ENTRY-FILE-READ-FAILED')
        # A paired-sampler forgery is not hidden behind wrapper hashes.
        program,doc,_,_=fresh(); target=root/doc['targets'][1]['path']; p=yaml.safe_load(target.read_text())
        p['samplerPairs'][0]['samplerBinding']=4
        target.write_text(yaml.safe_dump(p)); doc['targets'][1]['sha256']=hashlib.sha256(target.read_bytes()).hexdigest()
        program.write_text(yaml.safe_dump(doc)); reject(program,'SHADER-WEBGL1-PAIR-BINARY-MISMATCH')
        compiler=WebGlShaderTargetCompiler(args.library,args.slangc)
        original=template/'Vertex/webgl1_graphics.vertexMain.entry.yaml'
        try: compiler.generate(original,['notAUniform'])
        except RuntimeError as e: assert str(e)=='SHADER-WEBGL1-OPTIONAL-UNIFORM-UNKNOWN'
        else: raise AssertionError('optional unknown accepted')
        facts=SpirvLayoutFacts(template/'Vertex/webgl1_graphics.spv')
        assert not compiler.validate_binary(facts,'Vertex')
        for op in (29,1240,197,246,227,100):
            mutated=copy.copy(facts); mutated.instructions=list(facts.instructions)+[(0,op,(1,2,3,4))]
            try: compiler.validate_binary(mutated,'Vertex')
            except RuntimeError as e: assert str(e).startswith('SHADER-WEBGL1-OPCODE-UNSUPPORTED:')
            else: raise AssertionError(f'opcode accepted {op}')
        for op,args_tuple,expected in ((71,(20,11,15),'SHADER-WEBGL1-BUILTIN-UNSUPPORTED'),
                (88,(1,2,3,4,4,5,6),'SHADER-WEBGL1-EXPLICIT-SAMPLE-OPERANDS')):
            mutated=copy.copy(facts);mutated.instructions=list(facts.instructions)+[(0,op,args_tuple)]
            try:compiler.validate_binary(mutated,'Vertex')
            except RuntimeError as error:assert str(error)==expected
            else:raise AssertionError('coordinate-sensitive operation accepted')
        extension_facts=SpirvLayoutFacts(template/'extensions/Fragment/webgl1_extensions.spv')
        assert not compiler.validate_binary(extension_facts,'Fragment')
        derivative=next(a for _,op,a in extension_facts.instructions if op==208)
        absolute=next(a for _,op,a in extension_facts.instructions if op==12 and a[3]==4 and a[4:]==(derivative[1],))
        # Cover typed arithmetic, non-FAbs ExtInst, wrong import, Store value,
        # ReturnValue, and branch condition. These operand positions differ.
        consumers=[(129,(derivative[0],absolute[1],derivative[1],derivative[1])),
            (12,(*absolute[:3],8,derivative[1])),
            (12,(*absolute[:2],derivative[1],4,derivative[1])),
            (62,(absolute[1],derivative[1])), (254,(derivative[1],)),
            (250,(derivative[1],absolute[1],absolute[1]))]
        for consumer,operands in consumers:
            mutated=copy.copy(extension_facts)
            mutated.instructions=list(extension_facts.instructions)+[(0,consumer,operands)]
            try:compiler.validate_binary(mutated,'Fragment')
            except RuntimeError as error:assert str(error)=='SHADER-WEBGL1-SIGNED-DDY-UNPROVEN'
            else:raise AssertionError(f'signed ddy accepted by {consumer}')
            # Independently exercise the C++ loader after rehashing every
            # publication boundary. The original framing/layout stays valid;
            # a denied computational instruction is appended as hostile input.
            if root.exists():shutil.rmtree(root)
            shutil.copytree(template/'extensions',root)
            program=root/'webgl1_graphics.webgl-program.yaml'
            doc=yaml.safe_load((root/'webgl1_extensions.webgl-program.yaml').read_text())
            target=root/doc['targets'][1]['path'];p=yaml.safe_load(target.read_text())
            original=target.parent/p['original']['path'];entry=yaml.safe_load(original.read_text())
            binary=original.parent/entry['files']['spirv']['path']
            binary.write_bytes(binary.read_bytes()+struct.pack('<'+'I'*(len(operands)+1),
                (len(operands)+1)<<16|consumer,*operands))
            entry['files']['spirv']['sha256']=hashlib.sha256(binary.read_bytes()).hexdigest()
            entry['bufferDeclarationLayout']['spirvSha256']=entry['files']['spirv']['sha256']
            original.write_text(yaml.safe_dump(entry,sort_keys=False))
            p['original']['sha256']=hashlib.sha256(original.read_bytes()).hexdigest()
            target.write_text(yaml.safe_dump(p,sort_keys=False))
            doc['targets'][1]['sha256']=hashlib.sha256(target.read_bytes()).hexdigest()
            program.write_text(yaml.safe_dump(doc,sort_keys=False))
            reject(program,'SHADER-WEBGL1-SIGNED-DDY-UNPROVEN')
        sampled=next(a for _,op,a in extension_facts.instructions if op==86)
        mutated=copy.copy(extension_facts)
        mutated.instructions=[(offset,op,(*a[:2],sampled[1],a[3]) if op==86 else a)
            for offset,op,a in extension_facts.instructions]
        try:compiler.validate_binary(mutated,'Fragment')
        except RuntimeError as error:assert str(error)=='SHADER-WEBGL1-SAMPLER-DATAFLOW'
        else:raise AssertionError('indirect sampler dataflow accepted')
        # OpExtInst has a typed result despite its low opcode number.
        program,doc,target,p=fresh();original=target.parent/p['original']['path']
        entry=yaml.safe_load(original.read_text());binary=original.parent/entry['files']['spirv']['path']
        words=list(struct.unpack('<'+'I'*(binary.stat().st_size//4),binary.read_bytes()))
        uint_type=words[3];uint_result=uint_type+1;words[3]+=2
        words += [(4<<16)|21,uint_type,32,0,(6<<16)|12,uint_type,uint_result,1,4,1]
        binary.write_bytes(struct.pack('<'+'I'*len(words),*words))
        mutated=SpirvLayoutFacts(binary)
        try:compiler.validate_binary(mutated,'Vertex')
        except RuntimeError as error:assert str(error)=='SHADER-WEBGL1-UINT-UNSUPPORTED'
        else:raise AssertionError('unsigned ExtInst accepted')
        entry['files']['spirv']['sha256']=hashlib.sha256(binary.read_bytes()).hexdigest()
        entry['bufferDeclarationLayout']['spirvSha256']=entry['files']['spirv']['sha256']
        original.write_text(yaml.safe_dump(entry,sort_keys=False))
        p['original']['sha256']=hashlib.sha256(original.read_bytes()).hexdigest()
        target.write_text(yaml.safe_dump(p,sort_keys=False))
        doc['targets'][0]['sha256']=hashlib.sha256(target.read_bytes()).hexdigest()
        program.write_text(yaml.safe_dump(doc,sort_keys=False))
        reject(program,'SHADER-WEBGL1-UINT-UNSUPPORTED')
        program,doc,target,p=fresh(); original=target.parent/p['original']['path']
        entry=yaml.safe_load(original.read_text()); binary=original.parent/entry['files']['spirv']['path']
        binary.write_bytes(binary.read_bytes()+struct.pack('<I',1<<16|197))
        entry['files']['spirv']['sha256']=hashlib.sha256(binary.read_bytes()).hexdigest(); original.write_text(yaml.safe_dump(entry))
        stale=target.parent/(original.stem+'.glsl-es-100'); assert stale.exists()
        refused=compiler.publish(original); failure=yaml.safe_load(refused.read_text())
        assert failure['status']=='DiagnosticFailure' and not stale.exists()
        try: publish_program(refused,root/doc['targets'][1]['path'],program)
        except RuntimeError: assert not program.exists()
        else: raise AssertionError('failed composition accepted')
        refused_dir=Path(temporary)/'unsupported-existing-shader'
        reflect=args.reflect.resolve()
        subprocess.run([sys.executable,'-X','utf8',str(Path(__file__).resolve().parents[2]/'scripts/compile_shaders.py'),
            '--slangc',str(args.slangc),'--spirv-reflect',str(reflect),'--module','clear','--single-entry','vertexMain',
            '--glsl-es-100','--spirv-cross-library',str(args.library),'--out-dir',str(refused_dir)],check=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        refused=yaml.safe_load((refused_dir/'clear.vertexMain.entry.glsl-es-100.yaml').read_text())
        assert refused['status']=='DiagnosticFailure' and refused['diagnosticId'].startswith('SHADER-WEBGL1-')
        assert all((refused_dir/('clear'+suffix)).is_file() for suffix in ('.spv','.metal','.wgsl','.reflection.yaml'))
        assert not (refused_dir/'clear.vertexMain.entry.glsl-es-100').exists()
        print('Existing clear shader rejected:',refused['diagnosticId'])
    print(f'{len(cases)+5} metadata attacks, {len(consumers)} signed derivative uses, unsigned ExtInst, direct sampler and real shader refusals passed')

if __name__=='__main__': main()
