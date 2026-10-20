"""Observation-custody tamper tests; no claim of native execution."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
import yaml
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from webgpu_shader_trace import WebGpuOriginalShaderTrace


class StorageTraceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.workload = yaml.safe_load((Path(__file__).resolve().parents[1] / 'fixtures/rhi_conformance_storage_mrt.yaml').read_bytes())
        for index, prefix in enumerate(('original-', 'original-compute-')):
            code = ('original graphics' if index == 0 else 'original compute').encode()
            source = self.workload['computeSourceSha256' if index else 'shaderSourceSha256']
            reflection = yaml.safe_dump(dict(sourceSha256=source, pushConstantSizeBytes=0)).encode()
            module = dict(type='SingleEntryShaderArtifact' if index else 'GraphicsShaderModuleArtifact',
                contractVersion=3 if index else 1, matrixLayout='row-major',
                files=dict(wgsl=dict(sha256=hashlib.sha256(code).hexdigest()),
                    reflection=dict(sha256=hashlib.sha256(reflection).hexdigest()), source=dict(sha256=source)))
            module.update(dict(stage='Compute', entry='computeMain') if index else
                dict(entries=[dict(stage='Vertex', entry='vertexMain'), dict(stage='Fragment', entry='fragmentMain')]))
            (self.directory / (prefix + 'wgsl.wgsl')).write_bytes(code)
            (self.directory / (prefix + 'reflection.yaml')).write_bytes(reflection)
            (self.directory / (prefix + 'module.yaml')).write_text(yaml.safe_dump(module))
        trace = dict(modules=[dict(id=0, code='original graphics'), dict(id=1, code='original compute')],
            pipelines=[dict(id=0,vertexModule=0,fragmentModule=0,vertexEntry='vertexMain',fragmentEntry='fragmentMain',format='bgra8unorm'),
                dict(id=1,computeModule=1,computeEntry='computeMain')], submissions=[[10], [11], [12]], draws=[])
        for index, frame in enumerate(self.workload['frames']):
            trace['draws'] += [dict(encoder=10+index,pipeline=1,kind='dispatch',args=[frame['extent']['width'],frame['extent']['height'],1]),
                dict(encoder=10+index,pipeline=0,kind='draw',args=[3,1,0,0])]
        self.runtime = dict(success=True, nativeErrors=[], shaderTrace=trace)

    def verify(self, runtime=None):
        (self.directory / 'runtime.json').write_text(json.dumps(runtime or self.runtime))
        return WebGpuOriginalShaderTrace.verify_storage(self.directory, self.workload)

    def test_complete_observation(self):
        self.assertEqual(self.verify()['dispatches'], 3)

    def test_untrusted_observations(self):
        def source(t): t['modules'][1]['code'] += ' tampered'
        def module(t): t['pipelines'][1]['computeModule'] = 0
        def pipeline(t): t['draws'][0]['pipeline'] = 0
        def dimensions(t): t['draws'][0]['args'][0] -= 1
        def encoder(t): t['draws'][0]['encoder'] = 99
        def reuse(t): t['submissions'][1] = t['submissions'][0]
        def extra(t): t['draws'].append(deepcopy(t['draws'][0]))
        def dropped(t): t['draws'].pop()
        for mutate in (source, module, pipeline, dimensions, encoder, reuse, extra, dropped):
            with self.subTest(mutation=mutate.__name__):
                runtime = deepcopy(self.runtime)
                mutate(runtime['shaderTrace'])
                with self.assertRaisesRegex(RuntimeError, 'WEBGPU-SHADER-TRACE-'):
                    self.verify(runtime)

    def test_frozen_publication_rejects_changed_bytes(self):
        for suffix in ('wgsl.wgsl', 'reflection.yaml', 'module.yaml'):
            with self.subTest(file=suffix):
                path = self.directory / ('original-compute-' + suffix)
                original = path.read_bytes()
                path.write_bytes(original.replace(b'row-major', b'column-major') if suffix == 'module.yaml' else original + b'\n')
                with self.assertRaisesRegex(RuntimeError, 'WEBGPU-SHADER-TRACE-'):
                    self.verify()
                path.write_bytes(original)


if __name__ == '__main__':
    unittest.main()
