"""Explicit ContentBindings/1 closure and raw composite identity."""
import base64
import hashlib
from pathlib import Path, PurePosixPath
import shutil
import struct

import yaml


class ContentFiles:
    def __init__(self, directory):
        self.directory = Path(directory).resolve(strict=True)
        manifest = (self.directory / 'content.yaml').read_bytes()
        node = yaml.safe_load(manifest)
        assert node['type'] == 'arp.survivors-content' and node['contractVersion'] == 1
        self.names = ['content.yaml', node['atlasMetadata'], node['atlasImage'],
                      'dungeon.tileset.yaml', 'dungeon.tilemap.yaml', 'orbit.prefab.yaml',
                      'hit.wav', 'upgrade.wav', 'menu.wav', 'music.wav']
        assert len(set(self.names)) == 10
        for name in self.names:
            path = PurePosixPath(name)
            assert name and '\\' not in name and ':' not in name and '\0' not in name
            assert not path.is_absolute() and all(p not in ('.', '..') for p in name.split('/'))
        self.files = {name: (self.directory / name).read_bytes() for name in self.names}

    def identity(self):
        value = hashlib.sha256()
        for name, data in self.files.items():
            path = name.encode('utf-8')
            value.update(struct.pack('<Q', len(path)))
            value.update(path)
            value.update(struct.pack('<Q', len(data)))
            value.update(data)
        return 'sha256-base64:' + base64.b64encode(value.digest()).decode('ascii')

    def stage(self, destination):
        destination = Path(destination)
        destination.mkdir(parents=True, exist_ok=True)
        for name in self.names:
            target = destination / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(self.directory / name, target)
