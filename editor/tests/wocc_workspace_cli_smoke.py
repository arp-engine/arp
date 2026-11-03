"""Actual staged WOCC inputs through native Editor, source IO and Interpreter CLI."""
import argparse
import hashlib
import json
from pathlib import Path
import time

import yaml

from project_io_cli_smoke import run


class WoccWorkspaceWorkflow:
    def __init__(self, root, output):
        self.root, self.output = root, output
        adoption = yaml.safe_load((root.parent / 'adoption.yaml').read_bytes())
        assert adoption['stage'] == 'I7 explicit profile adoption'
        assert Path(adoption['workspace']).resolve() == root / 'workspace'
        assert adoption['originalWoccModified'] is False
        self.workspace = root / 'workspace'
        preview = yaml.safe_load((self.workspace / 'assets/editor/terrain-preview.yaml').read_bytes())
        assert preview['contractVersion'] == 'editor.terrain-preview/v2'
        assert preview['vegetation']['input'] == 'source/v2'
        self.terrain_id, self.material_id = preview['terrainId'], preview['resourceId']

    def __call__(self, invoke, state):
        def command(*arguments):
            response = invoke(*arguments)
            assert response['status'] == 'completed', response
            return response

        def finish_project():
            deadline = time.monotonic() + 120
            while time.monotonic() < deadline:
                value = state()
                if value['projectIoTask']['status'] != 'pending':
                    assert value['projectIoTask']['status'] == 'succeeded', value
                    return value
                time.sleep(.02)
            raise AssertionError('WOCC source task deadline')

        def finish_scene(accepted):
            task = accepted['snapshot']['scenePersistenceTask']['taskId']
            assert task > 0
            deadline = time.monotonic() + 120
            while time.monotonic() < deadline:
                value = command('scene.task.get', str(task))['snapshot']
                if value['scenePersistenceTask']['status'] != 'pending':
                    assert value['scenePersistenceTask']['status'] == 'succeeded', value
                    return value
                time.sleep(.02)
            raise AssertionError('WOCC Scene task deadline')

        def hierarchy():
            return command('scene.hierarchy.get')['sceneHierarchy']['entries']

        def create(name):
            before = {entry['objectId'] for entry in hierarchy()}
            command('activate', 'scene_object_add')
            created = [entry['objectId'] for entry in hierarchy() if entry['objectId'] not in before]
            assert len(created) == 1
            command('scene.rename', str(created[0]), name)
            return created[0]

        command('activate', 'workspace_3D')
        scene = self.workspace / 'wocc-preview.scene.yaml'
        incoming_scene_hash = hashlib.sha256(scene.read_bytes()).hexdigest() if scene.exists() else None
        incoming_script_hash = hashlib.sha256((self.workspace / 'content/scripts/wocc_gameplay.arp').read_bytes()).hexdigest()
        if scene.exists():
            finish_scene(command('scene.load', scene.name))
            terrain = next(entry['objectId'] for entry in hierarchy()
                           if entry['name'].startswith('WOCC Terrain'))
        else:
            terrain = create('WOCC Terrain A')
            properties = [dict(property='resourceId', kind='resourceReference', value=self.terrain_id),
                dict(property='materialResourceId', kind='resourceReference', value=self.material_id)]
            command('scene.component.add', str(terrain), 'TerrainComponent', json.dumps(properties))
            camera = create('WOCC Preview Camera')
            command('scene.component.add', str(camera), 'CameraComponent')
            command('scene.component.add', str(camera), 'SkyComponent')
        command('scene.select', str(terrain))
        command('scene.selection.focus')
        inspector = command('scene.inspector.get', str(terrain))['sceneInspector']['properties']
        references = {item['path']: item['value'] for item in inspector
                      if item['componentType'] == 'TerrainComponent'}
        assert references['resourceId'] == self.terrain_id and references['materialResourceId'] == self.material_id
        current_name = next(entry['name'] for entry in hierarchy() if entry['objectId'] == terrain)
        next_name = 'WOCC Terrain B' if current_name == 'WOCC Terrain A' else 'WOCC Terrain A'
        command('scene.rename', str(terrain), next_name)
        saved = finish_scene(command('scene.save', scene.name))
        assert not saved['sceneDirty'] and saved['sceneSaveReceipt']['published']
        assert bytes(saved['sceneSaveReceipt']['versionDigest']) == hashlib.sha256(scene.read_bytes()).digest()

        relative_script = 'content/scripts/wocc_gameplay.arp'
        command('script.open', relative_script)
        opened = finish_project()
        text, document = opened['scriptText'], str(opened['activeDocumentId'])
        tag_a, tag_b = '\n# I7 workspace roundtrip A\n', '\n# I7 workspace roundtrip B\n'
        changed = text[:-len(tag_a)] + tag_b if text.endswith(tag_a) else (
            text[:-len(tag_b)] + tag_a if text.endswith(tag_b) else text + tag_a)
        command('script.replace', document, changed)
        command('script.save', document)
        script_saved = finish_project()
        assert script_saved['scriptSaveReceipt']['published'] and not script_saved['scriptDirty']
        assert (self.workspace / relative_script).read_text(encoding='utf-8') == changed
        command('script.reload', document, 'discard')
        assert finish_project()['scriptText'] == changed
        command('script.close', document)
        loaded = finish_scene(command('scene.load', scene.name))
        assert not loaded['sceneDirty']
        assert next(entry['name'] for entry in hierarchy() if entry['objectId'] == terrain) == next_name
        report = dict(success=True, projectSource=loaded['projectSource'], terrainId=self.terrain_id,
            previewMaterialId=self.material_id, scene=str(scene), script=relative_script,
            scriptRole='Actual WOCC script text; no registered-module or gameplay execution claim',
            sceneSha256=hashlib.sha256(scene.read_bytes()).hexdigest(), originalWoccModified=False,
            incomingSceneSha256=incoming_scene_hash, incomingScriptSha256=incoming_script_hash,
            savedName=next_name,
            graphicsValidationClaim=False, productionReady=False)
        self.output.mkdir(parents=True, exist_ok=True)
        (self.output / 'verification.yaml').write_text(yaml.safe_dump(report, sort_keys=False), encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('editor', 'cli', 'root', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve(strict=True)
    run(args.editor.resolve(strict=True), args.cli.resolve(strict=True), root, args.output,
        WoccWorkspaceWorkflow(root, args.output), source_writes=True, project_timeout=120)


if __name__ == '__main__':
    main()
