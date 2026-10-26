"""Prepare real original-loader snapshots and run the native browser program component."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import yaml

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from browser_contract_runner import BrowserContractRunner

def prepare(directory,exe,output):
    output.mkdir(parents=True,exist_ok=True)
    snapshots=output/'snapshots.yaml'
    subprocess.run([str(exe.resolve()),str(directory.resolve()),str(snapshots.resolve())],check=True,cwd=ROOT)
    program=yaml.safe_load((directory/'webgl1_graphics.webgl-program.yaml').read_text())
    targets=[]
    for t in program['targets']:
        path=directory/t['path']; doc=yaml.safe_load(path.read_text())
        doc['glslText']=(path.parent/doc['glsl']['path']).read_text();targets.append(doc)
    extension_program=yaml.safe_load((directory/'extensions/webgl1_extensions.webgl-program.yaml').read_text())
    extension_targets=[]
    for t in extension_program['targets']:
        path=directory/'extensions'/t['path']; doc=yaml.safe_load(path.read_text())
        doc['glslText']=(path.parent/doc['glsl']['path']).read_text();extension_targets.append(doc)
    fixture=dict(program=program,targets=targets,snapshots=yaml.safe_load(snapshots.read_text()),
                 extensionProgram=extension_program,extensionTargets=extension_targets)
    (output/'fixture.json').write_text(json.dumps(fixture),encoding='utf-8') # Browser test transport only.
    shutil.copyfile(ROOT/'src/rhi/backends/webgl/webgl_shader_program.js',output/'webgl_shader_program.js')
    shutil.copyfile(ROOT/'tests/integration/webgl_shader_program.js',output/'validation.js')
    (output/'index.html').write_text('<!doctype html><canvas width="64" height="64"></canvas><script type="module" src="./validation.js"></script>',encoding='utf-8')
    return fixture

def main():
    ap=argparse.ArgumentParser()
    for n in ('directory','exe','output'):ap.add_argument('--'+n,required=True,type=Path)
    ap.add_argument('--browser');ap.add_argument('--prepare-only',action='store_true');ap.add_argument('--frames',type=int,default=2)
    args=ap.parse_args();prepare(args.directory,args.exe,args.output)
    if args.prepare_only:return
    report=BrowserContractRunner(args.browser,args.output,'index.html?frames='+str(args.frames)+'&progress=1',args.output/'run',60,capture_progress=True).run()
    pixels=report.pop('rgba',None)
    if pixels:
        from PIL import Image
        image=Image.frombytes('RGBA',(64,64),bytes(pixels)).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
        image.save(args.output/'present.png')
    (args.output/'report.yaml').write_text(yaml.safe_dump(report,sort_keys=False),encoding='utf-8')
    if not report['success']:raise RuntimeError(report.get('diagnostic','WEBGL1-PROGRAM-FAILED'))
    assert report['programIdentity']==hashlib.sha256((args.directory/'webgl1_graphics.webgl-program.yaml').read_bytes()).hexdigest()
    print('Real WebGL1 shader program: two-draw snapshots, ABI, matrix, clip, blend, CPU raster, attachment roundtrip passed')

if __name__=='__main__':main()
