"""Prepare a disposable authored fixture and execute the RenderDoc UI check."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import time
import traceback
import urllib.request
import urllib.error
import yaml


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--replay-only', action='store_true')
    parser.add_argument('--workflow', choices=('script', 'scene', 'project', 'modules'), default='script')
    parser.add_argument('--source-project', type=Path,
        help='Copy an explicit source-project fixture instead of emitting a native fixture')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if args.replay_only:
        configuration = output / 'config.json'
        assert configuration.is_file() and (output / 'saved.rdc').is_file() and (output / 'conflict.rdc').is_file()
        previous = output / 'renderdoc-report.json'
        if previous.is_file():
            failure = output / ('renderdoc-report-before-replay-' + str(time.time_ns()) + '.json')
            shutil.copyfile(previous, failure)
        environment = dict(os.environ, ARP_SOURCE_SCRIPT_CAPTURE=str(configuration),
            ARP_SOURCE_SCRIPT_REPLAY_ONLY='1')
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
        with (output / 'replay.log').open('w', encoding='utf-8') as log:
            result = subprocess.run(['C:/Program Files/RenderDoc/qrenderdoc.exe', '--python',
                str(root / 'tests/tools/capture_editor_source_save_renderdoc.py')], env=environment,
                stdout=log, stderr=log, startupinfo=startup, timeout=90)
        report = json.loads(previous.read_text(encoding='utf-8'))
        assert result.returncode == 0 and report['success'], report
        print('PASS existing native source-save captures replay: ' + str(output))
        return
    project = output / 'project'
    assert not project.exists(), 'Use a new evidence folder; do not overwrite an existing project.'
    if args.source_project:
        shutil.copytree(args.source_project.resolve(strict=True), project)
    elif args.workflow in ('scene', 'project', 'modules'):
        subprocess.run([str(args.build.resolve() / 'editor/arp_editor_source_scene_save_tests.exe'),
            '--emit', str(project)], check=True, timeout=45)
    else:
        shutil.copytree(root / 'tests/fixtures/project_resources', project)
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        port = listener.getsockname()[1]
    build = args.build.resolve()
    config = dict(root=str(root), output=str(output), project=str(project), port=port,
        build=str(build), editor=str(build / 'arp_editor.exe'))
    config['arguments'] = subprocess.list2cmdline(['--smoke', '--project-root', str(project),
        '--source-write', '--remote-port', str(port)])
    config['sceneWorkflow'] = args.workflow == 'scene'
    config['requireHdrViewport'] = args.workflow in ('scene', 'project')
    config['phases'] = ['saved', 'conflict', 'overwrite'] if config['sceneWorkflow'] else ['saved', 'conflict']
    configuration = output / 'config.json'
    configuration.write_text(json.dumps(config, indent=2), encoding='utf-8')
    environment = dict(os.environ,
        ARP_SOURCE_SCRIPT_CAPTURE=str(configuration),
        ARP_EDITOR_REMOTE_TOKEN='source-script-capture-local-token-20261031')
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = subprocess.SW_HIDE
    def request(command, **fields):
        payload = json.dumps(dict(schemaVersion=1, command=command, **fields)).encode()
        req = urllib.request.Request('http://127.0.0.1:{}/api/v1/commands'.format(port),
            data=payload, headers={'Content-Type': 'application/json',
                'Authorization': 'Bearer ' + environment['ARP_EDITOR_REMOTE_TOKEN']})
        try:
            with urllib.request.urlopen(req, timeout=5) as response:
                value = json.load(response)
        except urllib.error.HTTPError as error:
            raise AssertionError(error.read().decode('utf-8', errors='replace')) from error
        assert value['status'] == 'completed', value
        return value

    def state():
        return request('editor.state.get')['snapshot']

    def finish(expected='succeeded'):
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            value = state()
            if value['projectIoTask']['status'] != 'pending':
                assert value['projectIoTask']['status'] == expected, value
                return value
            time.sleep(0.02)
        raise AssertionError('capture source task deadline')

    def scene_finish(task, expected='succeeded'):
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            value = request('scene.task.get', taskId=task)['snapshot']
            if value['scenePersistenceTask']['status'] != 'pending':
                assert value['scenePersistenceTask']['status'] == expected, value
                return value
            time.sleep(0.02)
        raise AssertionError('capture scene task deadline')

    with (output / 'qrenderdoc.log').open('w', encoding='utf-8') as log:
        process = subprocess.Popen(['C:/Program Files/RenderDoc/qrenderdoc.exe', '--python',
            str(root / 'tests/tools/capture_editor_source_save_renderdoc.py')], env=environment,
            stdout=log, stderr=log, startupinfo=startup)
        def marker(name, redraw=False):
            deadline = time.monotonic() + 40
            while time.monotonic() < deadline:
                if (output / name).exists():
                    return
                assert process.poll() is None, (output / 'renderdoc-report.json').read_text() if (
                    output / 'renderdoc-report.json').exists() else 'RenderDoc exited before '+name
                if redraw:
                    request('editor.search', source='scene_search', value='')
                time.sleep(0.02)
            raise AssertionError('capture marker deadline: ' + name)
        try:
            marker('startup-captured')
            request('project.open', path='workspace', source='workspace')
            finish()
            if args.workflow == 'project':
                request('editor.activate', source='workspace_3D')
                loaded = request('scene.load', path='plain.yaml', discardChanges=True)
                scene_finish(loaded['snapshot']['scenePersistenceTask']['taskId'])
                request('editor.activate', source='project_ui_settings')
                request('editor.text.set', source='project_ui_name', value='Saved project through unified IO')
                request('editor.activate', source='project_ui_submit')
                saved = finish()
                assert saved['projectSaveReceipt']['published'] and not saved['projectDirty'], saved
                manifest = project / 'workspace/project.arp.yaml'
                assert bytes(saved['projectSaveReceipt']['versionDigest']) == hashlib.sha256(manifest.read_bytes()).digest()
                (output / 'saved-ready').write_text('1')
                marker('saved-armed')
                marker('saved-captured', redraw=True)
                request('editor.text.set', source='project_ui_name', value='Unsaved project draft retained')
                manifest.write_bytes(manifest.read_bytes() + b'\n# external settings change\n')
                request('editor.activate', source='project_ui_submit')
                conflict = finish('failed')
                assert conflict['projectDirty'] and not conflict['projectSaveReceipt']['published'], conflict
                assert conflict['projectSaveReceipt']['commitState'] == 'not_committed'
                (output / 'conflict-ready').write_text('1')
                marker('conflict-armed')
                marker('conflict-captured', redraw=True)
                (output / 'workflow-state.json').write_text(json.dumps(dict(saved=saved,
                    conflict=conflict), indent=2), encoding='utf-8')
                request('editor.activate', source='project_ui_cancel')
            elif args.workflow == 'scene':
                request('editor.activate', source='workspace_3D')
                loaded = request('scene.load', path='plain.yaml', discardChanges=True)
                scene_finish(loaded['snapshot']['scenePersistenceTask']['taskId'])
                hierarchy = request('scene.hierarchy.get')['sceneHierarchy']['entries']
                root_id = next(entry['objectId'] for entry in hierarchy if entry['parentId'] == 0)
                request('scene.rename', objectId=root_id, name='Saved scene through unified IO')
                accepted = request('scene.save', path='plain.yaml')
                saved = scene_finish(accepted['snapshot']['scenePersistenceTask']['taskId'])
                assert not saved['sceneDirty'] and saved['sceneSaveReceipt']['published'], saved
                scene = project / 'workspace/plain.yaml'
                assert bytes(saved['sceneSaveReceipt']['versionDigest']) == hashlib.sha256(scene.read_bytes()).digest()
                (output / 'saved-ready').write_text('1')
                marker('saved-armed')
                marker('saved-captured', redraw=True)
                request('scene.rename', objectId=root_id, name='Unsaved local scene retained')
                scene.write_bytes(scene.read_bytes() + b'\n# external scene change\n')
                accepted = request('scene.save', path='plain.yaml')
                conflict = scene_finish(accepted['snapshot']['scenePersistenceTask']['taskId'], 'failed')
                assert conflict['sceneDirty'] and not conflict['sceneSaveReceipt']['published'], conflict
                assert conflict['sceneSaveReceipt']['commitState'] == 'not_committed'
                (output / 'conflict-ready').write_text('1')
                marker('conflict-armed')
                marker('conflict-captured', redraw=True)
                loaded = request('scene.load', path='plain.yaml', discardChanges=True)
                scene_finish(loaded['snapshot']['scenePersistenceTask']['taskId'])
                request('editor.activate', source='save_scene_as')
                request('editor.text.set', source='scene_ui_path', value='scene.yaml')
                accepted = request('editor.activate', source='scene_ui_submit')
                overwrite = scene_finish(accepted['snapshot']['scenePersistenceTask']['taskId'], 'failed')
                assert overwrite['scenePersistenceTask']['diagnostic'] == 'EDITOR-SCENE-SOURCE-OVERWRITE-REQUIRED'
                (output / 'overwrite-ready').write_text('1')
                marker('overwrite-armed')
                marker('overwrite-captured', redraw=True)
                (output / 'workflow-state.json').write_text(json.dumps(dict(saved=saved,
                    conflict=conflict, overwrite=overwrite), indent=2), encoding='utf-8')
                request('editor.activate', source='scene_ui_cancel')
            elif args.workflow == 'modules':
                module_workflow(request, finish, scene_finish, marker, output, project)
            else:
                script_workflow(request, finish, marker, output, project)
            try:
                request('editor.exit', discardChanges=True)
            except OSError:
                pass
            assert process.wait(timeout=75) == 0
            report = json.loads((output / 'renderdoc-report.json').read_text(encoding='utf-8'))
            assert report['success'], report
        except Exception:
            (output / 'driver-error.log').write_text(traceback.format_exc(), encoding='utf-8')
            raise
        finally:
            if process.poll() is None:
                subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10)
                process.wait(timeout=10)
    print('PASS actual Vulkan source save/conflict captures and replay: ' + str(output))


def module_workflow(request, finish, scene_finish, marker, output, project):
    scene = project / 'workspace/scene.yaml'
    def script_state():
        encoded = yaml.safe_load(scene.read_bytes())
        return {(owner['id'], component['id']): dict(
            resourceId=component['state']['resourceId'],
            scriptType=component['state']['scriptType'],
            fields=component['state']['fields'], references=component['state']['references'])
            for owner in [encoded['root'], *encoded['objects']]
            for component in owner['components'] if component['kind'] == 'script'}
    original_fields = script_state()
    assert original_fields and any(value['references'] for value in original_fields.values())
    opened = request('scene.load', path='scene.yaml', discardChanges=True)['snapshot']
    scene_finish(opened['scenePersistenceTask']['taskId'])
    request('editor.activate', source='workspace_Script')
    request('script.open', path='primary.arp')
    document = finish()['activeDocumentId']
    text = request('editor.state.get')['snapshot']['scriptText']
    request('script.replace', documentId=document, text=text + '\n# Native explicit cold reload\n')
    request('script.save', documentId=document)
    finish()
    before = request('editor.state.get')['snapshot']['projectIoTask']['taskId']
    request('editor.activate', source='script_ui_reload_modules')
    reloaded = finish()
    assert reloaded['projectIoTask']['taskId'] > before and reloaded['sceneDirty'], reloaded
    accepted = request('scene.save', path='scene.yaml')['snapshot']
    saved = scene_finish(accepted['scenePersistenceTask']['taskId'])
    assert saved['sceneSaveReceipt']['published'] and not saved['sceneDirty'], saved
    assert saved['scene'] == 'Shell script root', 'Cold preparation ran user initializers'
    assert script_state() == original_fields, 'Module reload changed exact component fields or references'
    (output / 'saved-ready').write_text('1')
    marker('saved-armed')
    marker('saved-captured', redraw=True)
    hierarchy = request('scene.hierarchy.get')['sceneHierarchy']['entries']
    root = next(entry['objectId'] for entry in hierarchy if entry['parentId'] == 0)
    request('scene.rename', objectId=root, name='Retained local module scene')
    scene.write_bytes(scene.read_bytes() + b'\n# External scene bytes retained\n')
    accepted = request('scene.save', path='scene.yaml')['snapshot']
    conflict = scene_finish(accepted['scenePersistenceTask']['taskId'], 'failed')
    assert conflict['sceneDirty'] and not conflict['sceneSaveReceipt']['published'], conflict
    assert conflict['sceneSaveReceipt']['commitState'] == 'not_committed'
    (output / 'conflict-ready').write_text('1')
    marker('conflict-armed')
    marker('conflict-captured', redraw=True)
    (output / 'workflow-state.json').write_text(json.dumps(
        dict(reloaded=reloaded, saved=saved, conflict=conflict), indent=2), encoding='utf-8')


def script_workflow(request, finish, marker, output, project):
    request('script.open', path='scripts/gameplay.arp')
    opened = finish()
    document = opened['activeDocumentId']
    original = opened['scriptText']
    saved_text = original + '// Native source save capture\n'
    request('script.replace', documentId=document, text=saved_text)
    request('script.save', documentId=document)
    saved = finish()
    assert not saved['scriptDirty'] and saved['scriptSaveReceipt']['published']
    script = project / 'workspace/scripts/gameplay.arp'
    assert bytes(saved['scriptSaveReceipt']['versionDigest']) == hashlib.sha256(script.read_bytes()).digest()
    (output / 'saved-ready').write_text('1')
    marker('saved-armed')
    marker('saved-captured', redraw=True)
    request('script.replace', documentId=document, text=saved_text + '// Unsaved local change\n')
    external = original + '// External change kept on disk\n'
    script.write_bytes(external.replace('\n', '\r\n').encode())
    request('script.save', documentId=document)
    conflict = finish('failed')
    assert conflict['scriptDirty'] and not conflict['scriptSaveReceipt']['published']
    assert conflict['scriptSaveReceipt']['commitState'] == 'not_committed'
    (output / 'conflict-ready').write_text('1')
    marker('conflict-armed')
    marker('conflict-captured', redraw=True)
    (output / 'workflow-state.json').write_text(json.dumps(dict(saved=saved,
        conflict=conflict), indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
