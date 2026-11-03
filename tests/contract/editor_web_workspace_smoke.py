"""Actual Web product -> engine workspace service -> disk, including native CLI."""
import argparse
import functools
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request

import yaml

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from browser_contract_runner import BrowserContractRunner, retire_browser_process
from editor_web_bridge import EditorWebBridge
from serve_editor_web import EditorWebHandler, EditorWebServer
sys.path.insert(0, str(ROOT / 'tests/fixtures'))
from editor_workspace_connection_profile import connection_profile, wocc_connection_profile
from editor_workspace_receipt_workflow import EditorWorkspaceReceiptWorkflow
from workspace_fault_proxy import WorkspaceFaultProxy


def run(args):
    args.output.mkdir(parents=True, exist_ok=True)
    ready, result_ready, metric_ready = threading.Event(), threading.Event(), threading.Event()
    progress = dict(action='')
    report, responses, page_results, metrics = {}, [], [], []
    configuration = {}
    bundle_names = ('arp_editor.html', 'arp_editor.js', 'arp_editor.wasm', 'arp_editor.data')
    def bundle_hashes():
        return {name: hashlib.sha256((args.directory / name).read_bytes()).hexdigest()
                for name in bundle_names}
    report['bundleHashes'] = bundle_hashes()

    class Handler(EditorWebHandler):
        def log_message(self, *_): pass
        def do_GET(self):
            if self.path in ('/workspace-smoke-config', '/workspace-smoke-finish'):
                value = configuration if self.path.endswith('config') else progress
                data = json.dumps(value).encode()
                self.send_response(200)
                self.send_header('Content-Type', 'application/json')
                self.send_header('Cache-Control', 'no-store')
                self.send_header('Content-Length', str(len(data)))
                self.end_headers(); self.wfile.write(data)
            else: super().do_GET()
        def do_POST(self):
            if self.path in ('/workspace-smoke-ready', '/workspace-smoke-result', '/workspace-smoke-metrics'):
                size = int(self.headers.get('Content-Length', '0'))
                if not 0 < size <= 65536: self.send_error(413); return
                value = json.loads(self.rfile.read(size))
                if self.path.endswith('metrics'):
                    progress['action'] = ''
                    metrics.append(value)
                    metric_ready.set()
                elif self.path.endswith('ready'):
                    progress['action'] = ''
                    report['reopening'] = value['reopening']
                    ready.set()
                else:
                    page_results.append(value)
                    result_ready.set()
                self.send_response(200); self.send_header('Content-Length', '0'); self.end_headers()
            else: super().do_POST()

    server = EditorWebServer(('127.0.0.1', 0), functools.partial(Handler, directory=str(args.directory)))
    cli_token, read_token, write_token = [secrets.token_urlsafe(32) for _ in range(3)]
    server.editor_bridge = EditorWebBridge(cli_token)
    origin = f'http://127.0.0.1:{server.server_port}'
    env = dict(os.environ, ARP_EDITOR_REMOTE_URL=origin, ARP_EDITOR_REMOTE_TOKEN=cli_token)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    profile = tempfile.TemporaryDirectory(prefix='arp-editor-product-connection-')
    browser = host = proxy = proxy_worker = None

    def command(name, **fields):
        request = urllib.request.Request(origin + '/api/v1/commands',
            json.dumps(dict(schemaVersion=1, command=name, **fields)).encode(),
            {'Content-Type':'application/json', 'Authorization':'Bearer ' + cli_token})
        with urllib.request.urlopen(request, timeout=35) as response: value = json.load(response)
        responses.append(dict(command=name, response=value))
        assert value['status'] == 'completed', value
        return value.get('snapshot')

    def cli(*words):
        process = subprocess.run([str(args.cli), *words], env=env, capture_output=True,
            text=True, encoding='utf-8', timeout=45)
        assert process.returncode == 0, process.stdout + process.stderr
        value = json.loads(process.stdout)
        responses.append(dict(command=words[0], interpreterCli=True, response=value))
        assert value['status'] == 'completed', value
        return value.get('snapshot')

    def finish_scene(state, expected='succeeded'):
        task = state['scenePersistenceTask']['taskId']
        deadline = time.monotonic() + 45
        while time.monotonic() < deadline:
            state = command('scene.task.get', taskId=task)
            if state['scenePersistenceTask']['status'] != 'pending':
                assert state['scenePersistenceTask']['status'] == expected, state
                return state
            time.sleep(.1)
        raise TimeoutError('source scene task')

    def finish_project(state):
        task = state['projectIoTask']['taskId']
        deadline = time.monotonic() + 45
        while time.monotonic() < deadline:
            state = command('editor.state.get')
            if state['projectIoTask']['status'] != 'pending':
                assert state['projectIoTask']['taskId'] == task and state['projectIoTask']['status'] == 'succeeded', state
                return state
            time.sleep(.1)
        raise TimeoutError('source project task')

    def retired_receipt(operation, saved):
        # Ordinary saves already released this exact server ticket. Explicit
        # CLI maintenance must report Unavailable and retain the saved truth.
        receipt = saved['sceneSaveReceipt']['receipt']
        state = cli('source.receipt.' + operation, 'workspace', str(receipt['scope']),
            str(receipt['operationId']), receipt['sessionVersion']['digest'],
            receipt['requestVersion']['digest'])
        task = state['sourceReceiptTask']['taskId']
        deadline = time.monotonic() + 45
        while time.monotonic() < deadline:
            state = command('editor.state.get')
            observed = state['sourceReceiptTask']
            if observed['status'] != 'pending':
                assert observed['taskId'] == task and observed['status'] == 'failed', observed
                assert observed['submitted'] and observed['result']['state'] == 'unavailable', observed
                assert state['sceneSaveReceipt'] == saved['sceneSaveReceipt']
                assert state['sceneDirty'] == saved['sceneDirty']
                return
            time.sleep(.1)
        raise TimeoutError('source receipt observation')

    def wait_page():
        deadline = time.monotonic() + (600 if args.wocc_root else 240)
        while not ready.wait(.2):
            if any(not result.get('success') for result in page_results): raise RuntimeError(page_results)
            if browser.poll() is not None or host.poll() is not None: raise RuntimeError('owned process exited')
            if time.monotonic() > deadline: raise TimeoutError('product connection readiness')

    def sample():
        metric_ready.clear()
        progress['action'] = 'measure'
        deadline = time.monotonic() + 30
        while not metric_ready.wait(.1):
            if browser.poll() is not None or any(not item.get('success') for item in page_results):
                raise RuntimeError('receipt browser stopped: ' + repr(page_results))
            if time.monotonic() > deadline: raise TimeoutError('receipt lifecycle sample')
        return metrics[-1]

    def repeat_command(name, *words):
        # The warm cycle uses the real Interpreter CLI. Repeats use the same
        # original typed command endpoint without recompiling the CLI per call.
        if name.startswith('source.receipt.'):
            source, scope, identity, session, request = words
            return command(name, source=source, receipt=dict(contractVersion=1,
                scope=int(scope), operationId=int(identity),
                sessionVersion=dict(kind='content-sha256', digest=session),
                requestVersion=dict(kind='content-sha256', digest=request)))
        if name == 'script.open': return command(name, path=words[0])
        if name == 'script.replace': return command(name, documentId=int(words[0]), text=words[1])
        if name in ('script.save', 'script.close'): return command(name, documentId=int(words[0]))
        if name == 'script.reload':
            assert words[1:] == ('discard',)
            return command(name, documentId=int(words[0]), discardChanges=True)
        raise AssertionError('unsupported repeated fixture command: ' + name)

    with tempfile.TemporaryDirectory(prefix='arp-editor-workspace-root-') as temporary:
        root = args.wocc_root.resolve(strict=True) if args.wocc_root else Path(temporary).resolve(strict=True)
        project = root / 'workspace'
        if not args.wocc_root:
            shutil.copytree(ROOT / 'editor/web/project', project)
            # Unregistered file tests source saves without claiming registered-module rebind.
            (project / 'notes.arp').write_bytes(b'# source notes\r\n')
            if args.receipt_faults:
                for name in ('receipt_retained.arp', 'receipt_unknown.arp'):
                    (project / name).write_bytes(b'# initial receipt source\r\n')
        else:
            sys.path.insert(0, str(ROOT / 'editor/tests'))
            from wocc_workspace_cli_smoke import WoccWorkspaceWorkflow
            wocc_workflow = WoccWorkspaceWorkflow(root, args.output / 'web-workflow')
        try:
            host_arguments = [str(args.host), '--root', str(root), '--id', 'fixture',
                '--origin', origin, '--write', '--port', '0']
            if args.wocc_root:
                host_arguments += ['--max-file-bytes', str(32*1024*1024),
                    '--max-request-bytes', str(64*1024*1024), '--max-response-bytes', str(64*1024*1024),
                    '--preparation-bytes', str(256*1024*1024)]
            host = subprocess.Popen(host_arguments,
                env=dict(os.environ, ARP_WORKSPACE_READ_TOKEN=read_token, ARP_WORKSPACE_WRITE_TOKEN=write_token),
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == 'win32' else 0)
            line = host.stdout.readline().strip()
            match = re.fullmatch(r'ARP workspace: http://127\.0\.0\.1:([0-9]+)/workspace/v2 id=fixture session=([a-z0-9-]+)', line)
            assert match, line
            workspace_origin = f'http://127.0.0.1:{match.group(1)}'
            if args.receipt_faults:
                proxy = WorkspaceFaultProxy()
                proxy.upstream_port = int(match.group(1))
                proxy.drop_suffixes = {'/receipt_unknown.arp'}
                proxy.bad_suffixes = {'/receipt_unknown.arp'}
                proxy.corrupt_read_suffixes.clear()
                proxy.block_forget_suffixes = {'/receipt_retained.arp'}
                proxy_worker = threading.Thread(target=proxy.serve_forever, daemon=True)
                proxy_worker.start()
                workspace_origin = f'http://127.0.0.1:{proxy.server_port}'
            connection_metadata = wocc_connection_profile(workspace_origin) if args.wocc_root else connection_profile(workspace_origin)
            missing_profile = dict(connection_metadata, projectRoot='missing')
            configuration.update(yaml=yaml.safe_dump(connection_metadata, sort_keys=False),
                missingYaml=yaml.safe_dump(missing_profile, sort_keys=False),
                readToken=read_token, writeToken=write_token,
                workflowTimeoutMs=3600000 if args.receipt_faults else 600000 if args.wocc_root else 300000 if args.reload_modules else 180000,
                connectionCycles=1 if args.wocc_root or args.receipt_faults else 20)
            (args.output / 'connection.yaml').write_text(configuration['yaml'], encoding='utf-8')
            worker.start()
            runner = BrowserContractRunner(str(args.browser), str(args.directory), '', args.output, 240,
                disable_background_timer_throttling=True)
            with (args.output / 'browser.log').open('w', encoding='utf-8') as log:
                browser = subprocess.Popen(runner.browser_command(profile.name,
                    origin + '/arp_editor.html?workspace-smoke=1'), stdout=log, stderr=log,
                    creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == 'win32' else 0)
            wait_page()
            if args.receipt_faults:
                workflow = EditorWorkspaceReceiptWorkflow(cli,
                    lambda: command('editor.state.get'), project, proxy, sample, repeat_command)
                report['receiptObservations'] = workflow.observations
                receipt_report = workflow.run(args.receipt_cycles)
                request = urllib.request.Request(f'http://127.0.0.1:{proxy.upstream_port}/workspace/v2',
                    json.dumps(dict(type='arp.workspace.request', contractVersion=2,
                        workspace='fixture', operation='info')).encode(),
                    {'Content-Type':'application/json', 'Authorization':'Bearer ' + read_token})
                with urllib.request.urlopen(request, timeout=10) as response:
                    statistics = json.load(response)
                assert statistics['saveSubmissions'] == receipt_report['logicalSaves'], statistics
                assert statistics['confirmedCommits'] == receipt_report['logicalSaves'], statistics
                receipt_report['serverSaveSubmissions'] = statistics['saveSubmissions']
                receipt_report['serverConfirmedCommits'] = statistics['confirmedCommits']
                with proxy.lock:
                    receipt_report['duplicateWireAttempts'] = proxy.counts['replace'] - len(proxy.replace_wires)
                    receipt_report['maxWireAttemptsObserved'] = max(proxy.attempts.values())
                report['receiptFaults'] = receipt_report
                for name in ('receipt_retained.arp', 'receipt_unknown.arp'):
                    shutil.copy2(project / name, args.output / name)
                result_ready.clear(); progress['action'] = 'finish'
                assert result_ready.wait(30) and len(page_results) == 1 and page_results[0]['success'], page_results
                assert report['bundleHashes'] == bundle_hashes()
                report.update(success=True, pageResults=page_results, responses=responses,
                    ordinary20Claim=args.receipt_cycles == 20, gpuVisualAcceptance=False)
                print('PASS actual Web product retained/Unknown receipt query/release, explicit reload, disk SHA and lifecycle')
                return
            if args.wocc_root:
                def full_cli(*words):
                    cli(*words)
                    return responses[-1]['response']
                wocc_workflow(full_cli, lambda: command('editor.state.get'))
                before_native = yaml.safe_load((args.output / 'web-workflow/verification.yaml').read_bytes())
                shutil.copy2(project / 'wocc-preview.scene.yaml', args.output / 'web-written.scene.yaml')
                shutil.copy2(project / before_native['script'], args.output / 'web-written.wocc_gameplay.arp')
                ready.clear(); result_ready.clear(); progress['action'] = 'reload'
                wait_page()
                assert report['reopening'] is True
                finish_scene(cli('scene.load', 'wocc-preview.scene.yaml'))
                cli('scene.hierarchy.get')
                assert before_native['savedName'] in json.dumps(responses[-1]['response'])
                finish_project(cli('script.open', before_native['script']))
                script_state = command('editor.state.get')
                assert script_state['scriptText'] == (project / before_native['script']).read_text(encoding='utf-8')
                cli('script.close', str(script_state['activeDocumentId']))
                result_ready.clear(); progress['action'] = 'finish'
                assert result_ready.wait(30), 'WOCC browser final result'
                assert len(page_results) == 2 and all(value['success'] for value in page_results)
                assert page_results[0]['cycles'] == 1
                native_output = args.output / 'native-reopen'
                native = subprocess.run([sys.executable, str(ROOT / 'editor/tests/wocc_workspace_cli_smoke.py'),
                    '--editor', str(args.native_editor), '--cli', str(args.cli), '--root', str(root),
                    '--output', str(native_output)], capture_output=True, text=True, encoding='utf-8', timeout=240)
                report['nativeReopen'] = dict(exitCode=native.returncode, stdout=native.stdout, stderr=native.stderr)
                assert native.returncode == 0, native.stdout + native.stderr
                reopened = yaml.safe_load((native_output / 'verification.yaml').read_bytes())
                assert reopened['incomingSceneSha256'] == before_native['sceneSha256']
                assert reopened['incomingScriptSha256'] == hashlib.sha256((args.output / 'web-written.wocc_gameplay.arp').read_bytes()).hexdigest()
                assert report['bundleHashes'] == bundle_hashes()
                report.update(success=True, woccSourceWorkflow=True, pageResults=page_results,
                    responses=responses, ordinary20Claim=False, gpuVisualAcceptance=False)
                print('PASS actual Web WOCC source project, terrain references, real script/Scene save, refresh/reconnect and native Editor reopen')
                return
            state = command('editor.state.get')
            assert state['projectIoTask']['diagnostic'] == '', state['projectIoTask']
            finish_scene(cli('scene.load', 'hello_world.scene.yaml'))
            command('scene.select', objectId=2)
            if args.reload_modules:
                finish_project(cli('scene.script.add', '2', '23dbb375-d4df-416e-a43c-2d61326f2d42',
                    'HelloWorld', '1', 'true', 'message: "Hello World!"'))
            cli('scene.rename', '2', 'Web source object')
            state = finish_scene(cli('scene.save', 'connected.scene.yaml'))
            assert (project / 'connected.scene.yaml').is_file()
            persisted_scene = yaml.safe_load((project / 'connected.scene.yaml').read_text(encoding='utf-8'))
            assert any(item['name'] == 'Web source object' for item in persisted_scene['objects'])
            retired_receipt('query', state)
            retired_receipt('release', state)
            cli('project.settings.set', 'Connected Web project', 'none')
            finish_project(cli('project.save'))
            assert yaml.safe_load((project / 'project.arp.yaml').read_text(encoding='utf-8'))['name'] == 'Connected Web project'
            finish_project(cli('script.open', 'notes.arp'))
            state = command('editor.state.get')
            document_id = state['activeDocumentId']
            command('script.replace', documentId=document_id, text='# saved through product workspace\n')
            finish_project(command('script.save', documentId=document_id))
            assert (project / 'notes.arp').read_bytes() == b'# saved through product workspace\r\n'
            if args.reload_modules:
                finish_project(cli('script.open', 'hello_world.arp'))
                module_state = command('editor.state.get')
                module_id = module_state['activeDocumentId']
                before_module = (project / 'hello_world.arp').read_text(encoding='utf-8')
                assert 'self.game_object.set_object_name(self.message)' in before_module
                changed_module = before_module.replace('self.game_object.set_object_name(self.message)',
                    'self.game_object.set_object_name("Runtime body reloaded")')
                command('script.replace', documentId=module_id, text=changed_module)
                finish_project(cli('script.save', str(module_id)))
                assert (project / 'hello_world.arp').read_text(encoding='utf-8') == changed_module
                # Ordinary load still rejects the stale archive and retains its
                # current contents. Only the explicit command can rebind it.
                finish_scene(cli('scene.load', 'connected.scene.yaml'), expected='failed')
                reloaded = finish_project(cli('scene.scripts.reload'))
                # The real authored toolbar uses the same typed command. A
                # second reload before saving also exercises proof composition.
                command('editor.activate', source='workspace_Script')
                command('editor.activate', source='script_ui_reload_modules')
                ui_reload = command('editor.state.get')
                assert ui_reload['projectIoTask']['taskId'] > reloaded['projectIoTask']['taskId']
                finish_project(ui_reload)
                finish_scene(cli('scene.save', 'connected.scene.yaml'))
                encoded = yaml.safe_load((project / 'connected.scene.yaml').read_text(encoding='utf-8'))
                owner = next(item for item in encoded['objects'] if item['id'] == 2)
                assert owner['name'] == 'Web source object', 'Passive reload ran user lifecycle'
                component = next(item for item in owner['components'] if item['kind'] == 'script')
                assert component['state']['fields']['message'] == 'Hello World!'
                assert component['state']['sourceFingerprint'] == 'sha256:' + hashlib.sha256(
                    (project / 'hello_world.arp').read_bytes()).hexdigest()
                report['registeredComponentId'] = component['id']
                shutil.copy2(project / 'hello_world.arp', args.output / 'hello_world.arp')
            ready.clear(); result_ready.clear(); progress['action'] = 'reload'
            wait_page()
            assert report['reopening'] is True
            finish_scene(cli('scene.load', 'connected.scene.yaml'))
            if args.reload_modules:
                cli('scene.inspector.get', '2')
                properties = responses[-1]['response']['sceneInspector']['properties']
                assert any(item['componentType'] == 'HelloWorld' for item in properties)
            finish_project(cli('script.open', 'notes.arp'))
            state = command('editor.state.get')
            assert state['projectName'] == 'Connected Web project', state
            result_ready.clear(); progress['action'] = 'finish'
            assert result_ready.wait(30), 'browser final result'
            assert len(page_results) == 2 and all(p['success'] for p in page_results), page_results
            assert page_results[0]['cycles'] == 20 and page_results[1]['reopening']
            # Preserve the actual Web-written corpus before any native assertion;
            # a failure must remain independently reproducible after temp cleanup.
            written_project = Path(tempfile.mkdtemp(prefix='written-project-', dir=args.output))
            shutil.copytree(project, written_project, dirs_exist_ok=True)
            report['nativeInput'] = str(written_project.resolve())
            native_mode = '--verify-web-module-project' if args.reload_modules else '--verify-web-project'
            native = subprocess.run([str(args.native_reader), native_mode, str(written_project.resolve())],
                capture_output=True, text=True, encoding='utf-8', timeout=60)
            report['nativeReopen'] = dict(exitCode=native.returncode, stdout=native.stdout,
                stderr=native.stderr, gpuValidationClaim=False)
            assert native.returncode == 0, native.stdout + native.stderr
            assert report['bundleHashes'] == bundle_hashes(), 'Web build changed during product verification'
            report.update(success=True, pageResults=page_results, responses=responses,
                registeredModuleReloadClaim=args.reload_modules, gpuVisualAcceptance=False)
            shutil.copy2(project / 'connected.scene.yaml', args.output / 'connected.scene.yaml')
            print('PASS actual product Web connection, ordinary20, Interpreter CLI, scene/project/script disk saves and refresh/reconnect')
        except BaseException as error:
            report.update(success=False, error=str(error), pageResults=page_results, responses=responses)
            raise
        finally:
            if browser: retire_browser_process(browser, profile)
            else: profile.cleanup()
            if host and host.poll() is None: host.terminate(); host.communicate(timeout=10)
            if proxy:
                if proxy_worker.is_alive(): proxy.shutdown(); proxy_worker.join()
                proxy.server_close()
                report['proxyCounts'] = dict(proxy.counts)
                report['memorySamples'] = metrics
            if worker.is_alive(): server.shutdown(); worker.join()
            server.server_close()
            (args.output / 'report.yaml').write_text(yaml.safe_dump(report, sort_keys=False), encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('directory', 'browser', 'host', 'cli', 'output', 'native-reader'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--reload-modules', action='store_true')
    parser.add_argument('--wocc-root', type=Path, help='Explicit staged WOCC root with adoption.yaml')
    parser.add_argument('--native-editor', type=Path)
    parser.add_argument('--receipt-faults', action='store_true')
    parser.add_argument('--receipt-cycles', type=int, choices=(1, 20), default=1)
    args = parser.parse_args()
    if args.wocc_root and (not args.native_editor or args.reload_modules):
        parser.error('WOCC requires --native-editor and a separate workflow from --reload-modules')
    if args.receipt_faults and (args.wocc_root or args.reload_modules):
        parser.error('Receipt faults require a separate workflow from WOCC/module reload')
    run(args)


if __name__ == '__main__': main()
