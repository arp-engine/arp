"""Capture/replay the real WebGL Editor using a dedicated Chrome profile."""
import argparse
import functools
import hashlib
import json
import os
from pathlib import Path
import subprocess
import shutil
import threading
from browser_contract_runner import create_browser_http_server
from serve_editor_web import EditorWebHandler

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--directory',default='build/editor-webgl')
    parser.add_argument('--output',required=True)
    parser.add_argument('--browser',default='C:/Program Files/Google/Chrome/Application/chrome.exe')
    parser.add_argument('--qrenderdoc',default='C:/Program Files/RenderDoc/qrenderdoc.exe')
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[1]
    output=Path(args.output).resolve();output.mkdir(parents=True,exist_ok=False)
    bundle=output/'bundle';bundle.mkdir()
    for name in ('arp_editor.html','arp_editor.js','arp_editor.wasm','arp_editor.data','editor_webgl_capture.js'):
        shutil.copy2(Path(args.directory)/name,bundle/name)
    hashes={path.name:hashlib.sha256(path.read_bytes()).hexdigest() for path in bundle.iterdir()}
    class Handler(EditorWebHandler):
        def log_message(self,*unused):pass
        def do_POST(self):
            data=self.rfile.read(int(self.headers['Content-Length']))
            names={'/editor-ready':'ready','/editor-script-ready':'script-ready','/editor-error':'error'}
            if self.path not in names:self.send_error(404);return
            (output/names[self.path]).write_bytes(data)
            self.send_response(200);self.end_headers()
        def do_GET(self):
            if self.path=='/editor-capture-phase':
                self.send_response(200);self.end_headers()
                self.wfile.write(b'script' if (output/'scene-captured').exists() else b'scene');return
            super().do_GET()
    server=create_browser_http_server(functools.partial(Handler,directory=str(bundle)))
    threading.Thread(target=server.serve_forever,daemon=True).start()
    (output/'server.json').write_text(json.dumps({'root':str(root),'port':server.server_port,'browser':args.browser,'bundleHashes':hashes}))
    env=dict(os.environ,ARP_EDITOR_WEB_EVIDENCE=str(output))
    try:
        with (output/'qrenderdoc.log').open('w') as log:
            subprocess.run([args.qrenderdoc,'--python',str(root/'tests/tools/editor_webgl_renderdoc.py')],
                env=env,stdout=log,stderr=log,timeout=180,check=True)
    finally:server.shutdown();server.server_close()
    result=json.loads((output/'renderdoc-report.json').read_text())
    if not result.get('success'):raise RuntimeError(result)
    print('PASS: Editor WebGL scene/script captures, replay, atlas, blend and scissor:',output)

if __name__=='__main__':main()
