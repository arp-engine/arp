"""Verify observed original WGSL -> native API pipeline/draw/submit arguments.

This is capture custody evidence, never execution or completion authority.
"""
import hashlib
import json

import yaml


class WebGpuOriginalShaderTrace:
    @staticmethod
    def _original(directory, workload, prefix, artifact_type, version, source_key):
        module = yaml.safe_load((directory / (prefix + 'module.yaml')).read_bytes())
        reflection_bytes = (directory / (prefix + 'reflection.yaml')).read_bytes()
        reflection = yaml.safe_load(reflection_bytes)
        original = (directory / (prefix + 'wgsl.wgsl')).read_bytes()
        digest = hashlib.sha256(original).hexdigest()
        if (module['type'] != artifact_type or module['contractVersion'] != version
                or module['matrixLayout'] != 'row-major'
                or module['files']['wgsl']['sha256'] != digest
                or module['files']['reflection']['sha256'] != hashlib.sha256(reflection_bytes).hexdigest()
                or module['files']['source']['sha256'] != workload[source_key]
                or reflection['sourceSha256'] != workload[source_key]):
            raise RuntimeError('WEBGPU-SHADER-TRACE-ORIGINAL-ARTIFACT')
        return module, reflection, original, digest

    @staticmethod
    def verify(directory, workload):
        runtime = json.loads((directory / 'runtime.json').read_text())
        if not runtime['success'] or runtime['nativeErrors']:
            raise RuntimeError('WEBGPU-SHADER-TRACE-RUNTIME')
        trace = runtime['shaderTrace']
        module, reflection, original, digest = WebGpuOriginalShaderTrace._original(
            directory, workload, 'original-', 'GraphicsShaderModuleArtifact', 1, 'shaderSourceSha256')
        version = workload['contractVersion']
        count = 1 if version == 1 else 2
        if len(trace['modules']) != count or len(trace['pipelines']) != count:
            raise RuntimeError('WEBGPU-SHADER-TRACE-CREATION-COUNT')
        entries = {entry['stage']: entry['entry'] for entry in module['entries']}
        for index, observed in enumerate(trace['modules']):
            if observed['id'] != index or observed['code'].encode() != original:
                raise RuntimeError('WEBGPU-SHADER-TRACE-ORIGINAL-WGSL')
        for index, pipeline in enumerate(trace['pipelines']):
            if pipeline != dict(id=index, vertexModule=index, fragmentModule=index,
                    vertexEntry=entries['Vertex'], fragmentEntry=entries['Fragment'],
                    format='bgra8unorm' if index == 0 else 'rgba8unorm'):
                raise RuntimeError('WEBGPU-SHADER-TRACE-PIPELINE-IDENTITY')
        cursor = 0
        if len(trace['submissions']) != len(workload['frames']):
            raise RuntimeError('WEBGPU-SHADER-TRACE-SUBMISSION-COUNT')
        for frame, submission in zip(workload['frames'], trace['submissions']):
            if len(submission) != 1:
                raise RuntimeError('WEBGPU-SHADER-TRACE-ENCODER-IDENTITY')
            for draw in frame['draws']:
                expected = dict(encoder=submission[0], pipeline=0 if draw['target'] == 'Output' else 1,
                    kind='drawIndexed' if draw['indexed'] else 'draw',
                    args=[3, 1, 0, 0, 0] if draw['indexed'] else [3, 1, 0, 0])
                if cursor >= len(trace['draws']) or trace['draws'][cursor] != expected:
                    raise RuntimeError('WEBGPU-SHADER-TRACE-DRAW-IDENTITY')
                cursor += 1
        if cursor != len(trace['draws']) or len({x[0] for x in trace['submissions']}) != len(workload['frames']):
            raise RuntimeError('WEBGPU-SHADER-TRACE-EXTRA-WORK')
        frame_bindings = [b for b in reflection['webGpuBindingSpace']
            if b['name'] == 'Frame' and b['active'] and b['physicalType'] == 'UniformBuffer']
        if len(frame_bindings) != 1 or reflection['pushConstantSizeBytes'] != 32:
            raise RuntimeError('WEBGPU-SHADER-TRACE-CONSTANT-ABI')
        frame = frame_bindings[0]
        push = reflection['pushConstantWebGpuBinding']
        return dict(wgslSha256='sha256:' + digest, originalSourceSha256=workload['shaderSourceSha256'],
            modules=count, pipelines=count, draws=cursor, submissions=len(trace['submissions']),
            constants=dict(Frame=(frame['group'], frame['webGpuBinding']),
                           Push=(push['group'], push['binding'])))

    @staticmethod
    def verify_storage(directory, workload):
        runtime = json.loads((directory / 'runtime.json').read_text())
        if not runtime['success'] or runtime['nativeErrors']:
            raise RuntimeError('WEBGPU-SHADER-TRACE-RUNTIME')
        trace = runtime['shaderTrace']
        graphics, graphics_reflection, graphics_bytes, graphics_digest = WebGpuOriginalShaderTrace._original(
            directory, workload, 'original-', 'GraphicsShaderModuleArtifact', 1, 'shaderSourceSha256')
        compute, compute_reflection, compute_bytes, compute_digest = WebGpuOriginalShaderTrace._original(
            directory, workload, 'original-compute-', 'SingleEntryShaderArtifact', 3, 'computeSourceSha256')
        entries = {entry['stage']: entry['entry'] for entry in graphics['entries']}
        if (compute['stage'] != 'Compute' or len(trace['modules']) != 2 or len(trace['pipelines']) != 2
                or compute_reflection['pushConstantSizeBytes'] or graphics_reflection['pushConstantSizeBytes']):
            raise RuntimeError('WEBGPU-SHADER-TRACE-CREATION-COUNT')
        for index, original in enumerate((graphics_bytes, compute_bytes)):
            if trace['modules'][index] != dict(id=index, code=original.decode()):
                raise RuntimeError('WEBGPU-SHADER-TRACE-ORIGINAL-WGSL')
        if trace['pipelines'] != [dict(id=0,vertexModule=0,fragmentModule=0,
                vertexEntry=entries['Vertex'],fragmentEntry=entries['Fragment'],format='bgra8unorm'),
                dict(id=1,computeModule=1,computeEntry=compute['entry'])]:
            raise RuntimeError('WEBGPU-SHADER-TRACE-PIPELINE-IDENTITY')
        if (len(trace['submissions']) != len(workload['frames'])
                or len(trace['draws']) != len(workload['frames']) * 2):
            raise RuntimeError('WEBGPU-SHADER-TRACE-SUBMISSION-COUNT')
        encoders = set()
        for index,(frame,submission) in enumerate(zip(workload['frames'],trace['submissions'])):
            if len(submission) != 1 or submission[0] in encoders:
                raise RuntimeError('WEBGPU-SHADER-TRACE-ENCODER-IDENTITY')
            encoders.add(submission[0])
            extent = frame['extent']
            if trace['draws'][index*2:index*2+2] != [
                    dict(encoder=submission[0],pipeline=1,kind='dispatch',args=[extent['width'],extent['height'],1]),
                    dict(encoder=submission[0],pipeline=0,kind='draw',args=[3,1,0,0])]:
                raise RuntimeError('WEBGPU-SHADER-TRACE-DRAW-IDENTITY')
        return dict(graphicsWgslSha256='sha256:'+graphics_digest,computeWgslSha256='sha256:'+compute_digest,
            originalGraphicsSource=workload['shaderSourceSha256'],originalComputeSource=workload['computeSourceSha256'],
            modules=2,pipelines=2,draws=3,dispatches=3,submissions=3)
