"""Capture the actual engine WebGPU cloud workload through Chrome Dawn/D3D12."""
import functools
import http.server
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts'))
from browser_contract_runner import create_browser_http_server,cleanup_browser_profile

root=Path(__file__).resolve().parents[2]
build=(root/sys.argv[1]).resolve();out=(root/sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=False)
class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self,*unused):pass
    def do_GET(self):
        if self.path=='/armed':
            self.send_response(200 if (out/'armed').exists() else 404);self.end_headers();return
        super().do_GET()
    def do_POST(self):
        if self.path=='/ready':(out/'ready').write_text('ready')
        elif self.path=='/result':(out/'runtime.json').write_bytes(self.rfile.read(int(self.headers['Content-Length'])))
        else:self.send_error(404);return
        self.send_response(200);self.end_headers()
server=create_browser_http_server(functools.partial(Handler,directory=str(build)))
threading.Thread(target=server.serve_forever,daemon=True).start()
try:
    profile_directory=tempfile.TemporaryDirectory(prefix='arp-cloud-renderdoc-')
    try:
        profile=profile_directory.name
        (out/'server.json').write_text(json.dumps(dict(browser='C:/Program Files/Google/Chrome/Application/chrome.exe',root=str(build),profile=profile,port=server.server_port,entry='arp_volumetric_cloud.html',captureFrames=4)))
        env=dict(os.environ,ARP_WEBGL_PROGRAM_CAPTURE=str(out))
        with (out/'qrenderdoc.log').open('w') as log:
            run=subprocess.run(['C:/Program Files/RenderDoc/qrenderdoc.exe','--python',str(root/'tests/tools/capture_webgl_shader_program.py')],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=90)
        assert run.returncode==0
    finally:
        cleanup_browser_profile(profile_directory)
    print((out/'capture-attempt.json').read_text())
finally:
    server.shutdown();server.server_close()
