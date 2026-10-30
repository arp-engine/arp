"""Real MinGW Interpreter CLI -> loopback relay -> Wasm Shell -> saved v4 scene."""
import argparse
import base64
import functools
import json
import os
from pathlib import Path
import secrets
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request
import yaml

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
from browser_contract_runner import BrowserContractRunner, retire_browser_process
from serve_editor_web import EditorWebHandler, EditorWebServer
from editor_web_bridge import EditorWebBridge


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', required=True)
    parser.add_argument('--browser', required=True)
    parser.add_argument('--cli', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    ready = threading.Event()
    finished = threading.Event()
    exported = threading.Event()
    memory = []
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    class Handler(EditorWebHandler):
        def log_message(self, *unused):
            pass

        def do_POST(self):
            if self.path == '/cli-smoke-ready':
                self.rfile.read(int(self.headers['Content-Length']))
                ready.set()
                self.send_response(200)
                self.end_headers()
            elif self.path == '/cli-smoke-export':
                data = self.rfile.read(int(self.headers['Content-Length']))
                (output / 'workspace.yaml').write_bytes(data)
                exported.set()
                self.send_response(200)
                self.end_headers()
            elif self.path == '/cli-smoke-memory':
                memory.extend(json.loads(self.rfile.read(int(self.headers['Content-Length']))))
                self.send_response(200)
                self.end_headers()
            else:
                super().do_POST()

        def do_GET(self):
            if self.path == '/cli-smoke-finish':
                self.send_response(200)
                self.end_headers()
                self.wfile.write(json.dumps({'finish': finished.is_set()}).encode())
            else:
                super().do_GET()

    server = EditorWebServer(('127.0.0.1', 0), functools.partial(Handler, directory=args.directory))
    token = secrets.token_urlsafe(32)
    server.editor_bridge = EditorWebBridge(token)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    env = dict(os.environ, ARP_EDITOR_REMOTE_TOKEN=token,
               ARP_EDITOR_REMOTE_URL=f'http://127.0.0.1:{server.server_port}')
    results = []
    def rest(command, **fields):
        request = urllib.request.Request(env['ARP_EDITOR_REMOTE_URL'] + '/api/v1/commands',
            json.dumps(dict(schemaVersion=1, command=command, **fields)).encode(),
            {'Authorization': 'Bearer '+token, 'Content-Type': 'application/json'})
        with urllib.request.urlopen(request, timeout=35) as response:
            value = json.load(response)
        results.append(dict(command=command, transport='rest', response=value))
        if value.get('status') != 'completed':
            raise RuntimeError(value)
        return value

    def cli(*arguments):
        process = subprocess.run([args.cli, *arguments], env=env, capture_output=True,
                                 text=True, encoding='utf-8', timeout=45)
        if process.returncode:
            raise RuntimeError(str(arguments) + ': ' + process.stdout + process.stderr)
        value = json.loads(process.stdout)
        results.append(dict(command=arguments[0], response=value))
        if value.get('status') != 'completed':
            raise RuntimeError(value)
        return value
    def wait_io(reply):
        # State values come from the real response; no frame-count completion.
        deadline = time.monotonic() + 30
        task_id = reply['snapshot']['scenePersistenceTask']['taskId']
        while True:
            state = rest('scene.task.get', taskId=task_id)['snapshot']
            task = state['scenePersistenceTask']
            if task['status'] != 'pending':
                if task['status'] != 'succeeded':
                    raise RuntimeError(task)
                return state
            if time.monotonic() > deadline:
                raise TimeoutError(reply)
            time.sleep(.1)
    def has_script(reply):
        return any(p['componentType'] == 'HelloWorld'
                   for p in reply['sceneInspector']['properties'])
    profile = tempfile.TemporaryDirectory(prefix='arp-editor-web-cli-')
    runner = BrowserContractRunner(args.browser, args.directory, '', output, 120)
    browser = None
    try:
        with (output / 'browser.log').open('w') as log:
            browser = subprocess.Popen(runner.browser_command(profile.name,
                env['ARP_EDITOR_REMOTE_URL'] + '/arp_editor.html?cli-smoke=1'), stdout=log, stderr=log)
        if not ready.wait(90):
            raise TimeoutError('Web CLI page did not connect')
        rest('editor.state.get')
        wait_io(rest('scene.save', path='before-cli.scene.yaml'))
        wait_io(cli('scene.load', 'hello_world.scene.yaml'))
        rest('scene.select', objectId=2)
        cli('scene.script.add', '2', '23dbb375-d4df-416e-a43c-2d61326f2d42',
            'HelloWorld', '1', 'true', 'message: "Hello World!"')
        inspector = rest('scene.inspector.get', objectId=2)
        if not has_script(inspector):
            raise AssertionError('Attached script component missing')
        cli('scene.undo')
        if has_script(rest('scene.inspector.get', objectId=2)):
            raise AssertionError('Undo did not remove attachment')
        rest('scene.redo')
        wait_io(cli('scene.save', 'hello_world-attached.scene.yaml'))
        wait_io(rest('scene.load', path='hello_world-attached.scene.yaml'))
        if not has_script(rest('scene.inspector.get', objectId=2)):
            raise AssertionError('Reload did not restore script component')
        # Ordinary repeat queries after warmup, not a performance/stress run.
        for unused in range(21):
            rest('editor.state.get')
        finished.set()
        if not exported.wait(30):
            raise TimeoutError('CLI scene export did not complete')
        archive = yaml.safe_load((output / 'workspace.yaml').read_text(encoding='utf-8'))
        files = {item['path']: base64.b64decode(item['base64']) for item in archive['files']}
        scene = yaml.safe_load(files[archive['scene']])
        owner = next(item for item in scene['objects'] if item['id'] == 2)
        state = next(item['state'] for item in owner['components'] if item['kind'] == 'script')
        assert state['scriptType'] == 'HelloWorld'
        assert state['resourceId'] == '23dbb375-d4df-416e-a43c-2d61326f2d42'
        assert state['fields']['message'] == 'Hello World!'
        stable = memory[-20:]
        assert len(stable) == 20 and all(item['command'] == 'editor.state.get' for item in stable)
        assert len({(item['bytes'], item['allocations']) for item in stable}) == 1, stable
        (output / 'report.json').write_text(json.dumps(dict(success=True, results=results,
            ordinaryQueryMemory=stable), indent=2), encoding='utf-8')
        print('PASS real Interpreter CLI/Web Wasm catalog, attach, Undo/Redo, save/reload')
    except BaseException as error:
        (output / 'failed-report.json').write_text(json.dumps(dict(error=str(error), results=results), indent=2), encoding='utf-8')
        raise
    finally:
        if browser:
            retire_browser_process(browser, profile)
        else:
            profile.cleanup()
        server.shutdown()
        server.server_close()


if __name__ == '__main__':
    main()
