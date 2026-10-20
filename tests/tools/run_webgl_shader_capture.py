"""Host driver for dedicated RenderDoc browser capture with original input custody."""
import argparse
import functools
import http.server
import json
import hashlib
import io
import os
from pathlib import Path
import subprocess
import sys
import re
import shutil
import tempfile
import threading
import yaml

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from browser_contract_runner import cleanup_browser_profile
from emscripten_preload_package import EmscriptenPreloadPackage

def snapshot_product_inputs(directory, entry, output, gui):
    from PIL import Image
    scripts=re.findall(r'<script src="([^"]+\.js)"></script>',(directory/entry).read_text())
    if len(scripts)!=1:raise ValueError('WEBGL-CAPTURE-SCRIPT-IDENTITY')
    package=EmscriptenPreloadPackage(directory/scripts[0])
    snapshots={}
    for name,filename in ({'image':'atlas.png','glyph':'glyph.png'} if gui else {'atlas':'atlas.png'}).items():
        path='/tests/fixtures/scene_2d/'+filename
        data=package.read(path);pixels=Image.open(io.BytesIO(data))
        if pixels.mode!='RGBA':raise ValueError('WEBGL-CAPTURE-SOURCE-FORMAT')
        snapshots[name]=dict(source=path,sha256=hashlib.sha256(data).hexdigest(),
            extent=list(pixels.size),rgba=list(pixels.getdata()))
    if gui:(output/'gui-inputs.json').write_text(json.dumps(snapshots))
    else:(output/'scene2d-inputs.json').write_text(json.dumps(snapshots['atlas']))
def main():
    ap=argparse.ArgumentParser()
    for n in ('directory','output','browser','qrenderdoc'):ap.add_argument('--'+n,required=True,type=Path)
    ap.add_argument('--recovery-gate', action='store_true')
    ap.add_argument('--extensions-only', action='store_true')
    ap.add_argument('--ordered-only', action='store_true')
    ap.add_argument('--scene2d-only', action='store_true')
    ap.add_argument('--gui-only', action='store_true')
    ap.add_argument('--basic-forward-only', action='store_true')
    ap.add_argument('--conformance-clear-only', action='store_true')
    ap.add_argument('--conformance-sampled-only', action='store_true')
    ap.add_argument('--conformance-pingpong-only', action='store_true')
    ap.add_argument('--conformance-storage-only', action='store_true')
    ap.add_argument('--entry', choices=('index.html', 'pipeline.html',
        'webgpu_shared_conformance_runner.html'), default='index.html')
    ap.add_argument('--capture-frames', type=int, choices=(4, 24), default=4)
    args=ap.parse_args();output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    capture_inputs={}
    if args.entry=='webgpu_shared_conformance_runner.html':
        scripts=re.findall(r'<script src="([^"]+\.js)"></script>',(args.directory/args.entry).read_text())
        if len(scripts)!=1:raise ValueError('WEBGPU-CAPTURE-SCRIPT-IDENTITY')
        module=Path(scripts[0]).stem
        bundle=output/'bundle';bundle.mkdir()
        for name in (args.entry,module+'.js',module+'.wasm',module+'.data'):
            original=(args.directory/name).resolve()
            shutil.copyfile(original,bundle/name)
            capture_inputs[str(original)]=hashlib.sha256(original.read_bytes()).hexdigest()
            if hashlib.sha256((bundle/name).read_bytes()).hexdigest()!=capture_inputs[str(original)]:
                raise ValueError('WEBGPU-CAPTURE-BUNDLE-CUSTODY')
        sources=subprocess.check_output(['git','ls-files','-m','-o','--exclude-standard'],cwd=ROOT,text=True).splitlines()
        for relative in sources:
            original=ROOT/relative
            if original.is_file() and not relative.startswith('docs/') and relative!='TODO.md':
                capture_inputs[str(original)]=hashlib.sha256(original.read_bytes()).hexdigest()
    if args.conformance_clear_only or args.conformance_sampled_only or args.conformance_pingpong_only or args.conformance_storage_only:
        scripts=re.findall(r'<script src="([^"]+\.js)"></script>',(args.directory/args.entry).read_text())
        if len(scripts)!=1:raise ValueError('CONFORMANCE-CLEAR-SCRIPT-IDENTITY')
        fixture=('rhi_conformance_storage_mrt.yaml' if args.conformance_storage_only else
            'rhi_conformance_pingpong.yaml' if args.conformance_pingpong_only else
            'rhi_conformance_sampled.yaml' if args.conformance_sampled_only else 'rhi_conformance_clear.yaml')
        virtual_path=(('/conformance-storage.yaml' if args.conformance_storage_only else
            '/conformance-pingpong.yaml' if args.conformance_pingpong_only else
            '/conformance-sampled.yaml' if args.conformance_sampled_only else '/conformance-clear.yaml')
            if args.entry=='webgpu_shared_conformance_runner.html'
            else '/tests/fixtures/'+fixture)
        preload=EmscriptenPreloadPackage(args.directory/scripts[0])
        source=preload.read(virtual_path)
        (output/'conformance-workload.yaml').write_bytes(source)
        if args.conformance_storage_only:
            if args.entry!='webgpu_shared_conformance_runner.html':raise ValueError('CONFORMANCE-STORAGE-CAPTURE-ENTRY')
            for virtual in preload.entries:
                if virtual.startswith('/shared-storage/'):
                    target=output/'program'/virtual.removeprefix('/shared-storage/')
                    target.parent.mkdir(parents=True,exist_ok=True)
                    target.write_bytes(preload.read(virtual))
            originals={'original-vertex.spv':'Vertex/conformance_storage_mrt.spv',
                'original-fragment.spv':'Fragment/conformance_storage_mrt.spv',
                'original-compute.spv':'Compute/conformance_storage_fill.spv',
                'original-wgsl.wgsl':'conformance_storage_mrt.wgsl',
                'original-reflection.yaml':'conformance_storage_mrt.reflection.yaml',
                'original-module.yaml':'conformance_storage_mrt.graphics.module.yaml',
                'original-compute-wgsl.wgsl':'Compute/conformance_storage_fill.wgsl',
                'original-compute-reflection.yaml':'Compute/conformance_storage_fill.reflection.yaml',
                'original-compute-module.yaml':'Compute/conformance_storage_fill.computeMain.entry.yaml'}
            for name,virtual in originals.items(): (output/name).write_bytes(preload.read('/shared-storage/'+virtual))
        elif args.entry=='webgpu_shared_conformance_runner.html' and not args.conformance_clear_only:
            for name,virtual in (('original-vertex.spv','Vertex/webgl1_graphics.spv'),
                    ('original-fragment.spv','Fragment/webgl1_graphics.spv'),
                    ('original-wgsl.wgsl','webgl1_graphics.wgsl'),
                    ('original-reflection.yaml','webgl1_graphics.reflection.yaml'),
                    ('original-module.yaml','webgl1_graphics.graphics.module.yaml')):
                (output/name).write_bytes(preload.read('/shared-graphics/'+virtual))
    if args.gui_only or args.scene2d_only or args.basic_forward_only:
        snapshot_product_inputs(args.directory.resolve(),args.entry,output,args.gui_only or args.basic_forward_only)
    class Handler(http.server.SimpleHTTPRequestHandler):
        def log_message(self,*unused):pass
        def do_GET(self):
            if self.path=='/armed':
                self.send_response(200 if (output/'armed').exists() else 404);self.end_headers();return
            super().do_GET()
        def do_POST(self):
            if self.path=='/ready':(output/'ready').write_text('ready')
            elif self.path=='/result':
                (output/'runtime.json').write_bytes(self.rfile.read(int(self.headers['Content-Length'])))
            else:self.send_error(404);return
            self.send_response(200);self.end_headers();self.wfile.write(b'ok')
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),functools.partial(Handler,directory=str(args.directory.resolve())))
    threading.Thread(target=server.serve_forever,daemon=True).start()
    env=dict(os.environ,ARP_WEBGL_PROGRAM_CAPTURE=str(output))
    try:
        profile=tempfile.TemporaryDirectory(prefix='arp-webgl-rdc-')
        try:
            (output/'server.json').write_text(json.dumps(dict(profile=profile.name,port=server.server_port,browser=str(args.browser),root=str(ROOT),recoveryGate=args.recovery_gate,captureFrames=args.capture_frames,extensionsOnly=args.extensions_only,orderedOnly=args.ordered_only,scene2dOnly=args.scene2d_only,guiOnly=args.gui_only,basicForwardOnly=args.basic_forward_only,conformanceClearOnly=args.conformance_clear_only,conformanceSampledOnly=args.conformance_sampled_only or args.conformance_pingpong_only,conformanceSampledVersion=2 if args.conformance_pingpong_only else 1,entry=args.entry)))
            server_state=json.loads((output/'server.json').read_text())
            server_state['conformanceStorageOnly']=args.conformance_storage_only
            (output/'server.json').write_text(json.dumps(server_state))
            with (output/'qrenderdoc.log').open('w') as log:
                subprocess.run([str(args.qrenderdoc),'--python',str(ROOT/'tests/tools/capture_webgl_shader_program.py')],env=env,stdout=log,stderr=log,timeout=60,check=True)
        finally:
            cleanup_browser_profile(profile, timeout_seconds=30)
    finally:
        server.shutdown();server.server_close()
        if capture_inputs:
            unchanged=all(Path(path).is_file() and hashlib.sha256(Path(path).read_bytes()).hexdigest()==digest
                for path,digest in capture_inputs.items())
            (output/'capture-custody.yaml').write_text(yaml.safe_dump(dict(
                type='WebGpuSharedCaptureCustody',contractVersion=1,success=unchanged,
                sourceAndBundleInputs=capture_inputs,bundleDirectory=str(bundle)),sort_keys=False))
            if not unchanged:raise ValueError('WEBGPU-CAPTURE-SOURCE-OR-BUNDLE-DRIFT')
    runtime=json.loads((output/'runtime.json').read_text())
    (output/'runtime.yaml').write_text(yaml.safe_dump({k:v for k,v in runtime.items() if k!='rgba'},sort_keys=False))
    assert runtime['success'],runtime.get('diagnostic')
    assert list(output.glob('*.rdc'))
    print('RenderDoc captured real browser program; replay/visual inspection remains required')

if __name__=='__main__':main()
