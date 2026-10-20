"""Custody and native semantic negatives; synthetic inputs never prove a backend."""
import copy
import json
from pathlib import Path
import sys
from tempfile import TemporaryDirectory
import unittest

import yaml

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT/'scripts'), str(ROOT/'tests/tools')]
from webgl_visual_validation import WebGlVisualValidation
from webgl_product_native_state import WebGlProductNativeState


class VisualCustodyTests(unittest.TestCase):
    def setUp(self):
        self.temp = TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.bundle = self.root/'build'
        self.bundle.mkdir()
        for name in WebGlVisualValidation.bundle_names:
            (self.bundle/name).write_text(name)
        (self.bundle/'pipeline.html').write_text('<script src="arp_webgl1_runtime_validation.js"></script>')
        self.manifest = self.bundle/'build-manifest.yaml'
        self.document = dict(type='WebGl1RuntimeComponentBuild', contractVersion=1,
            status='Prototype Complete', allNamedInputsMatch=True, sourceInputs={'source': 'hash'},
            sourceInputsAfter={'source': 'hash'}, steps=[dict(name='build', exitCode=0)],
            productionReady=False, gpuCompletionClaimed=False,
            artifacts=WebGlVisualValidation.hashes(self.bundle))
        self.save()
        self.tool = self.root/'tool.exe'
        self.tool.write_bytes(b'test-only-tool')

    def save(self):
        self.manifest.write_text(yaml.safe_dump(self.document))

    def driver(self, **overrides):
        options = dict(root=self.root, manifest=self.manifest, output=self.root/'evidence',
                       qrenderdoc=self.tool, browser=self.tool)
        options.update(overrides)
        return WebGlVisualValidation(**options)

    def test_valid_build_and_bundle_identity(self):
        _, actual = self.driver().validate()
        self.assertEqual(actual, self.document['artifacts'])

    def test_failed_drifted_or_promoted_builds_reject(self):
        original = copy.deepcopy(self.document)
        for key, value in (('status', 'Building'), ('allNamedInputsMatch', False),
                ('sourceInputsAfter', {'source': 'changed'}), ('productionReady', True),
                ('steps', [dict(exitCode=1)]), ('steps', []), ('sourceInputs', {})):
            self.document = dict(original, **{key: value})
            self.save()
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, 'BUILD-INCOMPLETE'):
                self.driver().validate()

    def test_one_corrupt_bundle_file_rejects_before_execution(self):
        for name in self.document['artifacts']:
            if name == 'pipeline.html':
                continue
            path = self.bundle/name
            before = path.read_bytes()
            path.write_bytes(before+b'corrupt')
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'BUNDLE-IDENTITY'):
                self.driver().validate()
            path.write_bytes(before)

    def test_distinct_or_corrupt_replay_cohort_rejects(self):
        cohort = self.root/'cohort'
        cohort.mkdir()
        (cohort/'bundle').mkdir()
        for name in WebGlVisualValidation.bundle_names:
            (cohort/'bundle'/name).write_bytes((self.bundle/name).read_bytes())
        custody = dict(rootManifest=str(self.manifest), allNamedInputsMatch=True,
                       files=WebGlVisualValidation.hashes(self.bundle))
        path = cohort/'bundle-hashes.yaml'
        path.write_text(yaml.safe_dump(custody))
        self.driver(replay_source=cohort).validate()
        for change in ({'rootManifest': str(self.root/'different.yaml')}, {'files': {}},
                       {'allNamedInputsMatch': False}):
            path.write_text(yaml.safe_dump(dict(custody, **change)))
            with self.assertRaisesRegex(ValueError, 'REPLAY-COHORT'):
                self.driver(replay_source=cohort).validate()

    def test_output_does_not_modify_original_cohort_or_sources(self):
        for output in (self.root, self.bundle/'sub', self.root/'scripts'/'sub'):
            with self.subTest(output=output), self.assertRaisesRegex(ValueError, 'OUTPUT-'):
                self.driver(output=output).validate()

    def test_process_success_is_insufficient(self):
        draw = dict(extent=[64, 64], ordinal=0, nativeState=NativeStateNegativeTests().mesh(), verifiedOutputPixels=4096,
                    verifiedPostVsVertices=6, outputTarget='test-only', outputSha256='a'*64)
        resized = copy.deepcopy(draw)
        resized.update(extent=[48, 32])
        resized['nativeState'].update(viewport=[0,0,48,32,0,1], scissor=[0,0,48,32])
        report = dict(success=True, productionReady=False, gpuCompletionClaimed=False,
                      verifiedDraws=2, frames=[dict(replayErrors=[], draws=[draw, resized])])
        self.assertEqual(WebGlVisualValidation.validate_replay(report, 'basic-forward')['verifiedDraws'], 2)
        for change in ({'success': False}, {'verifiedDraws': 3}, {'productionReady': True},
                       {'frames': []}, {'frames': [dict(replayErrors=['error'], draws=[draw])]},
                       {'frames': [dict(replayErrors=[], draws=[dict(draw, nativeState={}), draw])]}):
            with self.assertRaisesRegex(ValueError, 'REPLAY-'):
                WebGlVisualValidation.validate_replay(dict(report, **change), 'basic-forward')


class NativeStateNegativeTests(unittest.TestCase):
    def test_corrupt_and_nonfinite_post_vs_reject(self):
        expected = [[-1, -1, 0, 1], [1, 1, 0, 1]]
        self.assertEqual(WebGlProductNativeState.validate_post_vs(expected, expected), expected)
        for value in (0, float('nan'), float('inf')):
            actual = copy.deepcopy(expected)
            actual[0][0] = value
            with self.assertRaisesRegex(RuntimeError, 'POSTVS-NUMERICAL'):
                WebGlProductNativeState.validate_post_vs(actual, expected)

    def mesh(self):
        shader = dict(entryPoint='main', encoding='ShaderEncoding.DXBC', nativeByteCount=4,
                      nativeSha256='a'*64, constantBlocks=[], readOnlyResources=[])
        return dict(nativeApi='GraphicsAPI.D3D11',
            raster=dict(frontCCW=True, scissorEnabled=True, cull='CullMode.Back'),
            depth=dict(enabled=True, writeMask=True, compare='CompareFunction.LessEqual', stencilEnabled=False),
            blend=dict(enabled=False, writeMask=15, logicEnabled=False), viewport=[0, 0, 64, 64, 0, 1],
            scissor=[0, 0, 64, 64], shaders=[
                dict(shader, stage='Vertex', constantBlocks=[dict(variables=['_arpUbo_0_0'])]),
                dict(shader, stage='Fragment', constantBlocks=[dict(variables=['_arpPush'])],
                     readOnlyResources=[dict(binding=0)])])

    def test_first_opaque_mesh_contract(self):
        self.assertIs(WebGlProductNativeState.validate(self.mesh(), 'basic-forward', 0, [64, 64]).get('depth')['enabled'], True)

    def test_wrong_raster_depth_blend_viewport_and_reflection_reject(self):
        mutations = [('raster', 'frontCCW', False), ('raster', 'cull', 'CullMode.Front'),
            ('raster', 'scissorEnabled', False), ('depth', 'writeMask', False),
            ('depth', 'enabled', False), ('depth', 'compare', 'CompareFunction.Always'),
            ('depth', 'stencilEnabled', True), ('blend', 'enabled', True), ('blend', 'writeMask', 7)]
        for section, key, value in mutations:
            state = self.mesh()
            state[section][key] = value
            with self.subTest(section=section, key=key), self.assertRaises(RuntimeError):
                WebGlProductNativeState.validate(state, 'basic-forward', 0, [64, 64])
        for key, value in (('scissor', [1, 0, 63, 64]), ('viewport', [0, 0, 64, 64, 1, 0]),
                           ('nativeApi', 'GraphicsAPI.Vulkan'), ('shaders', [])):
            with self.assertRaises(RuntimeError):
                WebGlProductNativeState.validate(dict(self.mesh(), **{key: value}), 'basic-forward', 0, [64, 64])
        for key, value in (('nativeByteCount', 0), ('constantBlocks', []), ('readOnlyResources', [dict(binding=2)])):
            state = self.mesh()
            state['shaders'][1][key] = value
            with self.assertRaises(RuntimeError):
                WebGlProductNativeState.validate(state, 'basic-forward', 0, [64, 64])


if __name__ == '__main__':
    unittest.main()
