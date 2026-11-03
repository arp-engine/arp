"""Test-only original WebEditorStartupSources/1 closure and SHA oracle."""
import hashlib
from pathlib import Path, PurePosixPath
import yaml

from ui_source_files import UiSourceFiles


class WebStartupFiles:
    profile = 'web-ui.sources.yaml'
    programs = ('gui_unified.webgl-program.yaml',
                'environment/editor_viewport_environment.webgl-program.yaml',
                'sprite/editor_viewport_sprite.webgl-program.yaml',
                'pbr/editor_viewport_pbr.webgl-program.yaml')
    modules = ('sprite/editor_viewport_sprite.graphics.module.yaml',
               'pbr/editor_viewport_pbr.graphics.module.yaml')

    def __init__(self, root, shaders, mono, cjk):
        root, shaders = Path(root).resolve(strict=True), Path(shaders).resolve(strict=True)
        self.paths, self.roles = {}, {}
        self.add(self.profile, root / 'editor/resources' / self.profile, 0)
        node = yaml.safe_load(self.paths[self.profile].read_bytes())
        assert node['type'] == 'arp.editor-ui-sources' and node['contractVersion'] == 1
        assert node['guiShader'] == self.programs[0]
        self.document, self.atlas = node['document'], node['atlas']
        for name in (self.document, self.atlas):
            self.add(name, root / 'editor/resources' / name, 0)
        font_paths = {'editor.ttf': root / '3rd/JoltPhysics-5.6.0/Assets/Fonts/Roboto-Regular.ttf',
                      'code.ttf': Path(mono), 'cjk.ttf': Path(cjk)}
        assert [f['token'] for f in node['fonts']] == [
            'font.editor', 'font.editor.semibold', 'font.code', 'font.code.cjk']
        self.fonts = [f['path'] for f in node['fonts']]
        for face in node['fonts']:
            self.add(face['path'], font_paths[face['path']], 1)
            assert 0 < self.paths[face['path']].stat().st_size <= face['maxBytes']
        for name in self.programs + self.modules:
            self.publication(shaders, name)
        assert len(self.paths) <= 128

    def add(self, name, path, role):
        UiSourceFiles.safe(name)
        path = path.resolve(strict=True)
        if name in self.paths:
            assert self.paths[name] == path and self.roles[name] == role, name
            return False
        self.paths[name], self.roles[name] = path, role
        return True

    def publication(self, root, name):
        if not self.add(name, root / name, 2):
            return
        node = yaml.safe_load((root / name).read_bytes())
        kind = node['type']
        if kind == 'WebGlShaderProgramArtifact':
            refs = [(entry, True) for entry in node['targets']]
        elif kind == 'WebGlShaderTargetArtifact':
            assert node['target'] == 'glsl-es-100' and node['status'] == 'Available'
            refs = [(node['original'], True), (node['glsl'], False)]
        elif kind in ('SingleEntryShaderArtifact', 'GraphicsShaderModuleArtifact'):
            assert node['matrixLayout'] == 'row-major'
            refs = [(node['files'][role], False) for role in
                    ('source', 'spirv', 'metal', 'wgsl', 'reflection')]
            if kind == 'GraphicsShaderModuleArtifact':
                refs += [(entry['publication'], True) for entry in node['entries']]
        else:
            raise AssertionError('Unexpected original publication: ' + kind)
        for ref, recurse in refs:
            child = (PurePosixPath(name).parent / UiSourceFiles.safe(ref['path'])).as_posix()
            data = (root / child).read_bytes()
            assert hashlib.sha256(data).hexdigest() == ref['sha256'], child
            if recurse:
                self.publication(root, child)
            else:
                self.add(child, root / child, 2)
