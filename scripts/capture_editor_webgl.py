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
import re
import secrets
import sys
import yaml
from browser_contract_runner import create_browser_http_server
from serve_editor_web import EditorWebHandler

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--directory',default='build/editor-webgl')
    parser.add_argument('--output',required=True)
    parser.add_argument('--browser',default='C:/Program Files/Google/Chrome/Application/chrome.exe')
    parser.add_argument('--qrenderdoc',default='C:/Program Files/RenderDoc/qrenderdoc.exe')
    parser.add_argument('--workspace', help='Explicit exported YAML workspace to inspect')
    parser.add_argument('--source-project', type=Path, help='Explicit fixture root to copy and open through the actual workspace service')
    parser.add_argument('--workspace-host', type=Path, default=Path('build/win-vulkan-debug/editor/arp_editor_workspace_service.exe'))
    parser.add_argument('--source-scene', default='plain.yaml')
    parser.add_argument('--source-script', default='primary.arp')
    parser.add_argument('--reload-modules', action='store_true', help='Capture actual source module save, explicit cold reload and original v4 scene save')
    parser.add_argument('--viewport-smoke', action='store_true', help='Explicit shared synthetic viewport fixtures')
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[1]
    if args.workspace and args.source_project:
        parser.error('--workspace and --source-project describe distinct input contracts')
    output=Path(args.output).resolve();output.mkdir(parents=True,exist_ok=False)
    bundle=output/'bundle';bundle.mkdir()
    for name in ('arp_editor.html','arp_editor.js','arp_editor.wasm','arp_editor.data','editor_cli_bridge.js',
                 'editor_workspace_connection.js','editor_webgl_capture.js'):
        shutil.copy2(Path(args.directory)/name,bundle/name)
    if args.workspace:
        shutil.copy2(args.workspace,bundle/'capture-workspace.yaml')
    hashes={path.name:hashlib.sha256(path.read_bytes()).hexdigest() for path in bundle.iterdir()}
    connection={}
    class Handler(EditorWebHandler):
        def log_message(self,*unused):pass
        def do_POST(self):
            data=self.rfile.read(int(self.headers['Content-Length']))
            names={'/editor-ready':'ready','/editor-script-ready':'script-ready','/editor-error':'error'}
            if self.path not in names:self.send_error(404);return
            (output/names[self.path]).write_bytes(data)
            self.send_response(200);self.end_headers()
        def do_GET(self):
            if self.path=='/editor-capture-connection':
                data=json.dumps(connection).encode()
                self.send_response(200);self.send_header('Cache-Control','no-store')
                self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data);return
            if self.path=='/editor-capture-phase':
                self.send_response(200);self.end_headers()
                self.wfile.write(b'script' if (output/'scene-captured').exists() else b'scene');return
            super().do_GET()
    server=create_browser_http_server(functools.partial(Handler,directory=str(bundle)))
    host=None
    if args.source_project:
        sys.path.insert(0,str(root/'tests/fixtures'))
        from editor_workspace_connection_profile import connection_profile
        source=output/'source-root'
        shutil.copytree(args.source_project,source)
        token=secrets.token_urlsafe(32)
        host=subprocess.Popen([str(args.workspace_host.resolve(strict=True)), '--root',str(source),
            '--id','fixture','--origin',f'http://127.0.0.1:{server.server_port}','--write','--port','0'],
            env=dict(os.environ,ARP_WORKSPACE_READ_TOKEN=secrets.token_urlsafe(32),ARP_WORKSPACE_WRITE_TOKEN=token),
            stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,
            creationflags=subprocess.CREATE_NO_WINDOW if sys.platform=='win32' else 0)
        line=host.stdout.readline().strip()
        match=re.fullmatch(r'ARP workspace: http://127\.0\.0\.1:([0-9]+)/workspace/v2 id=fixture session=([a-z0-9-]+)',line)
        if not match:
            host.terminate();host.communicate(timeout=10);server.server_close()
            raise RuntimeError('EDITOR-CAPTURE-WORKSPACE-HOST: '+line)
        profile=connection_profile(f'http://127.0.0.1:{match.group(1)}',file_bytes=1024*1024,preparation_bytes=32*1024*1024)
        connection.update(yaml=yaml.safe_dump(profile,sort_keys=False),token=token,
            scene=args.source_scene,script=args.source_script,reloadModules=args.reload_modules)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    (output/'server.json').write_text(json.dumps({'root':str(root),'port':server.server_port,'browser':args.browser,'bundleHashes':hashes,'workspace':bool(args.workspace),'sourceWorkspace':bool(args.source_project),'viewportSmoke':args.viewport_smoke}))
    env=dict(os.environ,ARP_EDITOR_WEB_EVIDENCE=str(output))
    try:
        with (output/'qrenderdoc.log').open('w') as log:
            subprocess.run([args.qrenderdoc,'--python',str(root/'tests/tools/editor_webgl_renderdoc.py')],
                env=env,stdout=log,stderr=log,timeout=180,check=True)
    finally:
        server.shutdown();server.server_close()
        if host and host.poll() is None:host.terminate();host.communicate(timeout=10)
    result=json.loads((output/'renderdoc-report.json').read_text())
    if not result.get('success'):raise RuntimeError(result)
    print('PASS: Editor WebGL scene/script captures, replay, atlas, blend and scissor:',output)

if __name__=='__main__':main()
