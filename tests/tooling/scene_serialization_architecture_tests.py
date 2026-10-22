"""Keep persistence traversal and dependency ownership in their declared layers."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ArchitectureTests(unittest.TestCase):
    def test_no_old_codec_interface_or_consumers(self):
        for directory in ('src', 'editor', 'modules', 'examples'):
            for path in (ROOT / directory).rglob('*'):
                if path.suffix not in ('.h', '.cpp', '.cmake'): continue
                text = path.read_text(encoding='utf-8-sig')
                self.assertNotRegex(text, r'\b(SceneComponentCodec|SceneCodecRegistry|SceneSpriteCodec|ScenePhysicsCodec|ObjectLinksCodec|ScriptComponentCodec)\b', str(path))

    def test_reflection_has_no_upward_dependencies(self):
        for path in (ROOT / 'src/reflection').rglob('*'):
            if path.suffix not in ('.cpp', '.h'): continue
            self.assertNotRegex(path.read_text(encoding='utf-8-sig'),
                                r'#include\s+["<](scene|2d|3d|physics|resource|renderer|scriptvm)/', str(path))

    def test_domain_bindings_do_not_walk_yaml(self):
        for name in ('src/2d/scene_component_persistence.cpp', 'src/scene/script_component_persistence.cpp',
                     'src/scriptvm/runtime/serialization_descriptor.h'):
            text = (ROOT / name).read_text(encoding='utf-8-sig')
            self.assertNotRegex(text, r'YamlContract|fkyaml::|Node::|\["fields"\]', name)

    def test_scene_scheduler_has_no_physics_phases(self):
        text = (ROOT / 'src/scene/scene_reflection_persistence.cpp').read_text(encoding='utf-8-sig')
        self.assertNotRegex(text, r'RigidBody2D|Collider2D|Joint2D|physics2d|InitializationPhase')
        self.assertIn('DependencyOrder::resolve', text)

    def test_editor_and_vm_use_common_archive(self):
        for name in ('editor/workspace/editor_scene_authoring_codec.cpp', 'src/scriptvm/runtime/script_value_yaml.h'):
            text = (ROOT / name).read_text(encoding='utf-8-sig')
            self.assertIn('SerializationArchive', text)
        text = (ROOT / 'editor/workspace/editor_scene_document.cpp').read_text(encoding='utf-8-sig')
        self.assertNotIn('parseResourceReferenceMetadata', text)
        self.assertNotIn('parseSceneObjectReferenceMetadata', text)


if __name__ == '__main__': unittest.main()
