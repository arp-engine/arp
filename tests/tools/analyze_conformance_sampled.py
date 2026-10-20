"""Independent native replay of shared sampled/indexed/uniform YAML intent."""
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import traceback
import renderdoc as rd

root = Path(os.environ['ARP_REPO_ROOT'])
sys.path.insert(0, str(root / 'tests/tools'))
if os.environ.get('ARP_HOST_PYTHON_SITE'):
    sys.path.append(os.environ['ARP_HOST_PYTHON_SITE'])
import yaml
from analyze_ui_text_renderdoc import flatten, descriptor_resource, texture_bytes, rgba8_pixels, write_bmp
from renderdoc_texture_buffer_copy import RenderDocTextureBufferCopy
from webgpu_shader_trace import WebGpuOriginalShaderTrace

output = Path(os.environ['ARP_CONFORMANCE_CAPTURE'])
result = dict(success=False, readinessPromoted=False, gpuCompletionClaimed=False, draws=[], rejectedObservations=[])


def scalar_values(variables):
    result = []
    for variable in variables:
        if variable.members:
            result.extend(scalar_values(variable.members))
        else:
            result.extend(variable.value.f32v[:variable.rows * variable.columns])
    return result


def verify_constants(controller, pipe, api, frame):
    observations = []
    for stage in (rd.ShaderStage.Vertex, rd.ShaderStage.Fragment):
        shader = pipe.GetShaderReflection(stage)
        if shader is None:
            raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-SHADER')
        found = set()
        for slot, block in enumerate(shader.constantBlocks):
            descriptor = pipe.GetConstantBlock(stage, slot, 0).descriptor
            if api == rd.GraphicsAPI.Vulkan and block.name in ('Frame', 'Push'):
                if block.name == 'Frame':
                    if not block.bufferBacked or block.byteSize != 128:
                        raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-UBO-ABI')
                    actual = list(struct.unpack('<32f', bytes(controller.GetBufferData(
                        descriptor.resource, descriptor.byteOffset, 128))))
                    expected = frame['uniforms']
                else:
                    if block.bufferBacked or block.byteSize != 32:
                        raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-PUSH-ABI')
                    actual = scalar_values(controller.GetCBufferVariableContents(
                        pipe.GetGraphicsPipelineObject(), shader.resourceId, stage,
                        pipe.GetShaderEntryPoint(stage), slot, descriptor.resource,
                        descriptor.byteOffset, descriptor.byteSize))
                    expected = frame['push']
                if actual != expected:
                    raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-CONSTANT-BYTES')
                found.add(block.name)
            elif api == rd.GraphicsAPI.D3D12:
                # Dawn maps WGSL groups/bindings to D3D12 spaces/registers.
                # Match original reflection metadata, not compiler-local names.
                matched = [name for name, binding in result['webgpuShaderTrace']['constants'].items()
                    if (block.fixedBindSetOrSpace, block.fixedBindNumber) == tuple(binding)]
                if len(matched) != 1 or matched[0] in found:
                    raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-DAWN-CONSTANT-BINDING')
                name = matched[0]
                expected = frame['uniforms'] if name == 'Frame' else frame['push']
                size = len(expected) * 4
                if (not block.bufferBacked or block.byteSize != size or descriptor.byteSize < size
                        or len(block.variables) != 1 or block.variables[0].byteOffset != 0):
                    raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-DAWN-CONSTANT-ABI')
                actual = list(struct.unpack('<' + str(len(expected)) + 'f', bytes(
                    controller.GetBufferData(descriptor.resource, descriptor.byteOffset, size))))
                if actual != expected:
                    raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-CONSTANT-BYTES')
                found.add(name)
            elif api == rd.GraphicsAPI.D3D11 and block.name == '$Globals':
                # Captured ANGLE HLSL fields identify their own offsets. The
                # ES100 mat4 is a transpose of the CPU row-major matrix and is
                # consumed by vector*matrix; D3D stores that logical matrix in
                # the captured translated cbuffer. Post-VS independently checks Mv.
                if not block.bufferBacked:
                    raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-ANGLE-CBUFFER')
                variables = {v.name: v for v in block.variables}
                if set(variables) != {'_arpUbo_0_0', '_arpPush'}:
                    raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-ANGLE-CONSTANT-ABI')
                matrix = frame['uniforms'][:16]
                expected_ubo = [matrix[c * 4 + r] for r in range(4) for c in range(4)] + frame['uniforms'][16:]
                for name, label, expected in (('_arpUbo_0_0', 'Frame', expected_ubo),
                        ('_arpPush', 'Push', frame['push'])):
                    variable = variables[name]
                    size = len(expected) * 4
                    if variable.byteOffset + size > block.byteSize:
                        raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-ANGLE-CONSTANT-RANGE')
                    actual = list(struct.unpack('<' + str(len(expected)) + 'f', bytes(
                        controller.GetBufferData(descriptor.resource,
                            descriptor.byteOffset + variable.byteOffset, size))))
                    if actual != expected:
                        raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-CONSTANT-BYTES')
                    found.add(label)
        if found != {'Frame', 'Push'}:
            raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-CONSTANT-COVERAGE')
        observations.append(dict(stage=str(stage), uniformBytes=128, pushBytes=32))
    return observations


try:
    source = (output / 'conformance-workload.yaml').read_bytes()
    workload = yaml.safe_load(source)
    if workload['type'] != 'ConformanceSampledWorkload' or workload['contractVersion'] not in (1, 2):
        raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-SCHEMA')
    version = workload['contractVersion']
    for f in workload['frames']:
        if version == 1:
            f['draws'] = [dict(sourceA='SourceA', sourceB='SourceB', target='Output',
                push=f['push'], indexed=f['indexed'], expected=f['expected'])]
    fixture = {(f['extent']['width'], f['extent']['height'], f['draws'][0]['indexed']): f for f in workload['frames']}
    if len(fixture) != len(workload['frames']):
        raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-FRAME-IDENTITY')
    expected_vertices = workload['vertices']
    seen = set()
    required_owned = os.environ.get('ARP_CONFORMANCE_REQUIRE_OWNED_COPY') == '1'
    owned_copies, presented = [], []
    result.update(ownedReadbackCopies=owned_copies, presentedOutputs=presented)
    shader_bytes = [(output / ('original-' + stage + '.spv')).read_bytes()
        for stage in ('vertex', 'fragment')] if required_owned else []
    if (output / 'original-wgsl.wgsl').is_file():
        result['webgpuShaderTrace'] = WebGpuOriginalShaderTrace.verify(output, workload)
    captures = sorted(output.glob('*.rdc'))
    if not captures:
        raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-MISSING')
    for path in captures:
        cap = rd.OpenCaptureFile()
        if cap.OpenFile(str(path), '', None).code != rd.ResultCode.Succeeded:
            raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-OPEN')
        status, controller = cap.OpenCapture(rd.ReplayOptions(), None)
        if status.code != rd.ResultCode.Succeeded:
            cap.Shutdown()
            raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-REPLAY')
        try:
            api = controller.GetAPIProperties().pipelineType
            if api not in (rd.GraphicsAPI.Vulkan, rd.GraphicsAPI.D3D11, rd.GraphicsAPI.D3D12):
                raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-API')
            if api == rd.GraphicsAPI.D3D12 and 'webgpuShaderTrace' not in result:
                raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-DAWN-ORIGINAL-SOURCE')
            textures = {t.resourceId:t for t in controller.GetTextures()}
            candidates = []
            for action in flatten(controller.GetRootActions()):
                if not action.flags & rd.ActionFlags.Drawcall or action.numIndices != 3:
                    continue
                controller.SetFrameEvent(action.eventId, True)
                pipe = controller.GetPipelineState()
                attrs = [a for a in pipe.GetVertexInputs() if a.used]
                if [a.format.compCount for a in attrs] != [3, 2, 4]:
                    continue
                targets = [descriptor_resource(d) for d in pipe.GetOutputTargets()]
                targets = [rid for rid in targets if rid in textures]
                sampled = {descriptor_resource(d.descriptor) for d in pipe.GetReadOnlyResources(rd.ShaderStage.Fragment)}
                sampled.discard(rd.ResourceId.Null())
                if len(targets) != 1 or len(sampled) != 2 or targets[0] in sampled:
                    raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-SAMPLED-OWNERS')
                candidates.append((action, targets[0], sampled))
            selected = []
            if version == 1:
                for action, target_id, sampled in candidates:
                    target = textures[target_id]
                    key = (target.width, target.height, bool(action.flags & rd.ActionFlags.Indexed))
                    if key not in fixture:
                        raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-FRAME-IDENTITY')
                    selected.append((action, key, 0, None))
            else:
                # Resource edges alone identify a complete four-draw closure,
                # before any output/oracle pixels or constant bytes are read.
                cursor = 0
                while cursor < len(candidates):
                    group = candidates[cursor:cursor + 4]
                    closed = len(group) == 4
                    if closed:
                        targets = [x[1] for x in group]
                        sources = [x[2] for x in group]
                        common = set.intersection(*sources)
                        closed = (len(common) == 1 and targets[0] == targets[2]
                            and len(set(targets)) == 3 and not common.intersection(targets))
                        if closed:
                            b = next(iter(common))
                            a = next(iter(sources[0] - common))
                            closed = (a not in targets and sources[1] == {targets[0], b}
                                and sources[2] == {targets[1], b} and sources[3] == {targets[0], b})
                    if not closed:
                        result['rejectedObservations'].append(dict(capture=str(path),
                            event=candidates[cursor][0].eventId,
                            diagnostic='CONFORMANCE-SAMPLED-CAPTURE-INCOMPLETE-RESOURCE-CLOSURE',
                            acceptedAsEvidence=False))
                        cursor += 1
                        continue
                    target = textures[targets[0]]
                    key = (target.width, target.height, bool(group[0][0].flags & rd.ActionFlags.Indexed))
                    if key not in fixture:
                        raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-FRAME-IDENTITY')
                    owners = dict(SourceA=a, SourceB=b, PingA=targets[0], PingB=targets[1], Output=targets[3])
                    for j, (action, target_id, sampled) in enumerate(group):
                        draw = fixture[key]['draws'][j]
                        if (bool(action.flags & rd.ActionFlags.Indexed) != draw['indexed']
                                or target_id != owners[draw['target']]
                                or sampled != {owners[draw['sourceA']], owners[draw['sourceB']]}):
                            raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-RESOURCE-EDGE')
                        selected.append((action, key, j, owners))
                    cursor += 4
            for action, key, draw_index, owners in selected:
                controller.SetFrameEvent(action.eventId, True)
                pipe = controller.GetPipelineState()
                attrs = [a for a in pipe.GetVertexInputs() if a.used]
                authored = fixture[key]
                draw = authored['draws'][draw_index]
                frame = dict(authored, push=draw['push'], expected=draw['expected'])
                # D3D11 Draw/DrawIndexed records numInstances=0; one instance
                # is guaranteed by those non-instanced native operations.
                if (action.flags & rd.ActionFlags.Instanced and action.numInstances != 1
                        or action.baseVertex != 0 or action.vertexOffset != 0 or action.instanceOffset != 0):
                    raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-DRAW-RANGE')
                indexed = bool(action.flags & rd.ActionFlags.Indexed)
                indices = list(range(3))
                if indexed:
                    index = pipe.GetIBuffer()
                    if index.byteStride != 2:
                        raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-INDEX-WIDTH')
                    indices = list(struct.unpack('<3H', bytes(controller.GetBufferData(
                        index.resourceId, index.byteOffset + action.indexOffset * 2, 6))))
                    if indices != workload['indices']:
                        raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-INDICES')
                streams = pipe.GetVBuffers()
                actual = []
                for vertex in range(3):
                    for attr in attrs:
                        if attr.format.compType != rd.CompType.Float or attr.format.compByteWidth != 4 or attr.perInstance:
                            raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-VERTEX-FORMAT')
                        stream = streams[attr.vertexBuffer]
                        raw = bytes(controller.GetBufferData(stream.resourceId,
                            stream.byteOffset + vertex * stream.byteStride + attr.byteOffset, attr.format.compCount * 4))
                        actual.extend(struct.unpack('<' + str(attr.format.compCount) + 'f', raw))
                if actual != expected_vertices:
                    raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-ORIGINAL-VERTICES')
                targets = [descriptor_resource(d) for d in pipe.GetOutputTargets()]
                targets = [rid for rid in targets if rid in textures]
                if len(targets) != 1:
                    raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-TARGET')
                target = textures[targets[0]]
                if (target.width, target.height) != key[:2] or indexed != draw['indexed']:
                    raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-FRAME-IDENTITY')
                constants = verify_constants(controller, pipe, api, frame)
                if 'SRGB' in target.format.Name().upper():
                    raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-COLOR-SPACE')
                pixels = rgba8_pixels(texture_bytes(controller, targets[0]), target.format.Name())
                if len(pixels) != target.width * target.height or any(list(p) != frame['expected'] for p in pixels):
                    raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-OUTPUT: ' + repr((path.name, action.eventId, pixels[:2])))
                sampled = {descriptor_resource(d.descriptor) for d in pipe.GetReadOnlyResources(rd.ShaderStage.Fragment)}
                sampled.discard(rd.ResourceId.Null())
                if len(sampled) != 2 or targets[0] in sampled:
                    raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-SAMPLED-OWNERS')
                source_colors = []
                colors_by_owner = {}
                expected_sources = dict(SourceA=authored['sourceA'], SourceB=authored['sourceB'])
                for earlier in authored['draws'][:draw_index]:
                    expected_sources[earlier['target']] = earlier['expected']
                for rid in sampled:
                    texture = textures[rid]
                    colors = rgba8_pixels(texture_bytes(controller, rid), texture.format.Name())
                    if (texture.width, texture.height) != key[:2] or len(colors) != len(pixels) or any(p != colors[0] for p in colors):
                        raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-SOURCE-PIXELS')
                    source_colors.append(tuple(colors[0]))
                    colors_by_owner[rid] = list(colors[0])
                if owners is not None:
                    for name in (draw['sourceA'], draw['sourceB']):
                        if colors_by_owner[owners[name]] != expected_sources[name]:
                            raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-SOURCE-HISTORY')
                elif set(source_colors) != {tuple(authored['sourceA']), tuple(authored['sourceB'])}:
                    raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-SOURCE-IDENTITY')
                if pipe.GetColorBlends()[0].enabled:
                    raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-BLEND')
                shaders = [pipe.GetShaderReflection(stage) for stage in (rd.ShaderStage.Vertex, rd.ShaderStage.Fragment)]
                if any(s is None or not s.rawBytes for s in shaders):
                    raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-SHADER')
                if required_owned and api == rd.GraphicsAPI.Vulkan and any(
                        bytes(s.rawBytes) != original for s, original in zip(shaders, shader_bytes)):
                    raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-ORIGINAL-SHADER-BYTES')
                if required_owned and api not in (rd.GraphicsAPI.Vulkan, rd.GraphicsAPI.D3D12):
                    raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-ORIGINAL-SHADER-PROVENANCE')
                programs = [dict(resource=str(s.resourceId), stage=str(s.stage),
                    entryPoint=s.entryPoint, encoding=str(s.encoding),
                    nativeByteCount=len(s.rawBytes),
                    nativeSha256='sha256:' + hashlib.sha256(bytes(s.rawBytes)).hexdigest(),
                    constantBlocks=[dict(name=b.name, byteSize=b.byteSize,
                        bufferBacked=b.bufferBacked) for b in s.constantBlocks]) for s in shaders]
                post = controller.GetPostVSData(0, 0, rd.MeshDataStage.VSOut)
                if post.numIndices != 3 or post.vertexByteStride < 16 or post.vertexResourceId == rd.ResourceId.Null():
                    raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-POSTVS: ' + str(post.status))
                post_indices = list(range(3))
                if post.indexResourceId != rd.ResourceId.Null():
                    code = {2:'H', 4:'I'}.get(post.indexByteStride)
                    if not code:
                        raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-POSTVS-INDICES')
                    post_indices = struct.unpack('<3' + code, bytes(controller.GetBufferData(
                        post.indexResourceId, post.indexByteOffset, 3 * post.indexByteStride)))
                matrix = frame['uniforms'][:16]
                for actual_index, original_index in zip(post_indices, indices):
                    actual_position = struct.unpack('<4f', bytes(controller.GetBufferData(post.vertexResourceId,
                        post.vertexByteOffset + actual_index * post.vertexByteStride, 16)))
                    vertex = expected_vertices[original_index * 9:original_index * 9 + 3] + [1]
                    expected_position = [sum(matrix[r * 4 + c] * vertex[c] for c in range(4)) + frame['push'][r] for r in range(4)]
                    if any(abs(a - b) > 1.e-5 for a, b in zip(actual_position, expected_position)):
                        raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-MATRIX: ' + repr((actual_position, expected_position)))
                image = path.with_name(path.stem + '-sampled-' + str(action.eventId) + '.bmp')
                write_bmp(str(image), target.width, target.height, pixels)
                result['draws'].append(dict(capture=str(path), event=action.eventId, api=str(api),
                    extent=list(key[:2]), indexed=indexed, rgba=frame['expected'], verifiedPixels=len(pixels),
                    verifiedPostVsVertices=3, sourceOwners=[str(rid) for rid in sampled],
                    drawIndex=draw_index, targetOwner=str(targets[0]), targetIntent=draw['target'],
                    originalOwners={k:str(v) for k,v in owners.items()} if owners else {},
                    constants=constants,
                    programs=programs,
                    outputSha256='sha256:' + hashlib.sha256(bytes(c for p in pixels for c in p)).hexdigest(), image=str(image)))
                if draw_index + 1 == len(authored["draws"]):
                    seen.add(key)
            if required_owned:
                buffers = {b.resourceId:b for b in controller.GetBuffers()}
                expected_outputs = {(f['extent']['width'], f['extent']['height'], tuple(f['draws'][-1]['expected']))
                    for f in workload['frames']}
                for action in flatten(controller.GetRootActions()):
                    copied = (action.flags & rd.ActionFlags.Copy and action.copySource in textures
                        and action.copyDestination in buffers)
                    present = bool(action.flags & rd.ActionFlags.Present)
                    if not copied and not present:
                        continue
                    rid = action.copySource if copied else action.copyDestination
                    if rid not in textures:
                        continue
                    texture = textures[rid]
                    if (texture.width, texture.height) not in {key[:2] for key in fixture}:
                        continue
                    controller.SetFrameEvent(action.eventId, True)
                    pixels = rgba8_pixels(texture_bytes(controller, rid), texture.format.Name())
                    if copied:
                        if api == rd.GraphicsAPI.D3D12:
                            raw, native_layout = RenderDocTextureBufferCopy.d3d12_rgba8(
                                controller, action, texture, buffers[action.copyDestination])
                        else:
                            raw = bytes(controller.GetBufferData(action.copyDestination, 0,
                                texture.width * texture.height * 4))
                            native_layout = None
                        if rgba8_pixels(raw, texture.format.Name()) != pixels:
                            raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-OWNED-BUFFER-BYTES')
                    matched = [key for key in expected_outputs if key[:2] == (texture.width, texture.height)
                        and len(pixels) == texture.width * texture.height
                        and all(tuple(p) == key[2] for p in pixels)]
                    if len(matched) != 1:
                        raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-OWNED-OUTPUT-PIXELS')
                    observation = dict(capture=str(path), event=action.eventId,
                        sourceImage=str(rid), extent=[texture.width, texture.height],
                        rgba=list(matched[0][2]), verifiedPixels=len(pixels),
                        outputSha256='sha256:' + hashlib.sha256(bytes(c for p in pixels for c in p)).hexdigest())
                    if copied:
                        observation['destinationBuffer'] = str(action.copyDestination)
                        observation['nativeCopyLayout'] = native_layout
                    (owned_copies if copied else presented).append(observation)
            errors = [m.description for m in controller.GetDebugMessages() if 'Error' in str(m.severity)]
            if errors:
                raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-REPLAY-ERRORS: ' + repr(errors))
        finally:
            controller.Shutdown()
            cap.Shutdown()
    if seen != set(fixture):
        raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-COVERAGE: ' + repr(seen))
    if required_owned:
        expected = {(f['extent']['width'], f['extent']['height'], tuple(f['draws'][-1]['expected']))
            for f in workload['frames']}
        if {(f['extent'][0], f['extent'][1], tuple(f['rgba'])) for f in owned_copies} != expected:
            raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-OWNED-COPY-COVERAGE')
        for copy in owned_copies:
            if not any(f['capture'] == copy['capture'] and f['targetIntent'] == 'Output'
                    and f['targetOwner'] == copy['sourceImage'] and f['outputSha256'] == copy['outputSha256']
                    for f in result['draws']):
                raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-ORIGINAL-DRAW-COPY-IDENTITY')
            if not any(f['capture'] == copy['capture'] and f['sourceImage'] == copy['sourceImage']
                    and f['outputSha256'] == copy['outputSha256'] for f in presented):
                raise RuntimeError('CONFORMANCE-SAMPLED-CAPTURE-OWNED-COPY-PRESENT-IDENTITY')
    result.update(success=True, workloadSha256='sha256:' + hashlib.sha256(source).hexdigest(),
        captures=len(captures), uniqueFrames=len(seen), replayErrors=[])
except Exception:
    result['error'] = traceback.format_exc()
(output / 'replay.json').write_text(json.dumps(result, indent=2))
os._exit(0 if result['success'] else 1)
