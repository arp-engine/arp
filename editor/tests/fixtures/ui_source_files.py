"""Explicit NativeEditorUiSources/1 paths; original system fonts stay in place."""
from pathlib import Path, PurePosixPath

import yaml


class UiSourceFiles:
    profile = 'windows-ui.sources.yaml'

    @staticmethod
    def safe(name):
        assert isinstance(name, str) and name and '\0' not in name and '\\' not in name and ':' not in name
        assert not PurePosixPath(name).is_absolute()
        assert all(part not in ('', '.', '..') for part in name.split('/'))
        return name

    def __init__(self, ui_root, font_root, shader_root):
        ui_root, font_root, shader_root = (Path(path).resolve(strict=True) for path in (ui_root, font_root, shader_root))
        self.paths = {}
        self.add(ui_root, self.profile)
        node = yaml.safe_load(self.paths[self.profile].read_bytes())
        assert node['type'] == 'arp.editor-ui-sources' and node['contractVersion'] == 1
        self.document = self.safe(node['document'])
        self.atlas = self.safe(node['atlas'])
        self.add(ui_root, self.document)
        self.add(ui_root, self.atlas)
        assert len(node['fonts']) == 4
        for face in node['fonts']:
            self.add(font_root, face['path'])
        self.publication = self.safe(node['guiShader'])
        self.add(shader_root, self.publication)
        shader = yaml.safe_load(self.paths[self.publication].read_bytes())
        assert shader['type'] == 'GeneratedShaderArtifactSet' and shader['contractVersion'] == 1
        for role in ('source', 'spirv', 'metal', 'wgsl', 'reflection'):
            self.add(shader_root, (PurePosixPath(self.publication).parent /
                                  self.safe(shader['files'][role]['path'])).as_posix())
        assert len(self.paths) == 13

    def add(self, directory, name):
        name = self.safe(name)
        assert name not in self.paths
        self.paths[name] = (directory / name).resolve(strict=True)

    def requests(self):
        return {'/' + name: 1 for name in self.paths}
