"""Actual Interpreter CLI opens a mounted project and its terrain preview Scene."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import time

from project_io_cli_smoke import run


def workflow(invoke, state):
    invoke('activate', 'workspace_3D')
    accepted = invoke('scene.load', 'scene.yaml')
    task = accepted['snapshot']['scenePersistenceTask']['taskId']
    assert task > 0, accepted
    deadline = time.monotonic() + 25
    while time.monotonic() < deadline:
        receipt = invoke('scene.task.get', str(task))['snapshot']['scenePersistenceTask']
        if receipt['status'] != 'pending':
            assert receipt['status'] == 'succeeded', receipt
            break
        time.sleep(0.02)
    else:
        raise AssertionError('terrain Scene load timeout')
    current = state()
    assert current['projectSource'] == 'workspace' and current['scenePath'] == 'scene.yaml', current
    assert not current['sceneDirty']
    hierarchy = invoke('scene.hierarchy.get')
    assert 'Source Terrain' in json.dumps(hierarchy), hierarchy
    assert 'Source Camera' in json.dumps(hierarchy), hierarchy
    inspector = invoke('scene.inspector.get')
    assert 'TerrainComponent' in json.dumps(inspector), inspector


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('editor', type=Path)
    parser.add_argument('cli', type=Path)
    parser.add_argument('fixture', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='arp-terrain-cli-') as folder:
        subprocess.run([str(args.fixture.resolve()), '--write-scene-fixture', folder],
                       check=True, timeout=30)
        run(args.editor.resolve(), args.cli.resolve(), Path(folder), args.output, workflow)
    print('PASS mounted terrain project, Scene and Inspector through the real Editor/Interpreter CLI')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
