"""Independent original compute->sampled->MRT closure and full pixel replay."""
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
required_owned = os.environ.get('ARP_CONFORMANCE_REQUIRE_OWNED_COPY') == '1'
result = dict(ownedReadbackCopies=[], presentedOutputs=[], success=False, readinessPromoted=False, gpuCompletionClaimed=False, frames=[], nativePresentationAlphaPasses=[], excludedBrowserDraws=[], excludedBrowserCaptures=[], excludedBrowserCopiesAndPresents=[])


def require(condition, label):
    if not condition:
        raise RuntimeError('CONFORMANCE-STORAGE-MRT-CAPTURE-' + label)


def resources(pipe, stage, writable=None):
    entries = (list(pipe.GetReadOnlyResources(stage)) + list(pipe.GetReadWriteResources(stage))) if writable is None else (pipe.GetReadWriteResources(stage) if writable else pipe.GetReadOnlyResources(stage))
    return [entry.descriptor for entry in entries if descriptor_resource(entry.descriptor) != rd.ResourceId.Null()]


def program(pipe, stage, expected, api):
    shader = pipe.GetShaderReflection(stage)
    require(shader is not None and bool(shader.rawBytes), 'SHADER-BYTES')
    if api == rd.GraphicsAPI.Vulkan:
        require(bytes(shader.rawBytes) == expected, 'SHADER-BYTES')
    else:
        require(api == rd.GraphicsAPI.D3D12 and result.get('webgpuShaderTrace'), 'ORIGINAL-WGSL-TRACE')
        expected = bytes(shader.rawBytes)
    return dict(stage=str(stage), entry=shader.entryPoint, bytes=len(shader.rawBytes),
        sha256='sha256:' + hashlib.sha256(expected).hexdigest())


try:
    source = (output / 'conformance-workload.yaml').read_bytes()
    workload = yaml.safe_load(source)
    require(workload['type'] == 'ConformanceStorageMrtWorkload' and workload['contractVersion'] == 1
        and workload['requireTransientAliasing'] is False, 'SCHEMA')
    graphics = (output / 'program/conformance_storage_mrt.spv').read_bytes()
    compute = (output / 'program/Compute/conformance_storage_fill.spv').read_bytes()
    require(hashlib.sha256((output / 'program/conformance_storage_mrt.slang').read_bytes()).hexdigest()
        == workload['shaderSourceSha256'], 'GRAPHICS-SOURCE')
    require(hashlib.sha256((output / 'program/Compute/conformance_storage_fill.slang').read_bytes()).hexdigest()
        == workload['computeSourceSha256'], 'COMPUTE-SOURCE')
    vertex_bytes = (output / 'original-vertex.spv').read_bytes() if required_owned else graphics
    fragment = (output / 'original-fragment.spv').read_bytes() if required_owned else graphics
    if required_owned:
        compute = (output / 'original-compute.spv').read_bytes()
    if (output / 'original-compute-wgsl.wgsl').exists():
        result['webgpuShaderTrace'] = WebGpuOriginalShaderTrace.verify_storage(output, workload)
    captures = sorted(output.glob('*.rdc'))
    require(bool(captures), 'MISSING')
    cursor = 0
    for path in captures:
        cap = rd.OpenCaptureFile()
        require(cap.OpenFile(str(path), '', None).code == rd.ResultCode.Succeeded, 'OPEN')
        status, controller = cap.OpenCapture(rd.ReplayOptions(), None)
        require(status.code == rd.ResultCode.Succeeded, 'REPLAY')
        try:
            api = controller.GetAPIProperties().pipelineType
            if api == rd.GraphicsAPI.D3D11 and result.get('webgpuShaderTrace'):
                require(not any(a.flags & rd.ActionFlags.Dispatch for a in flatten(controller.GetRootActions())), 'COMPOSITOR-COMPUTE')
                result['excludedBrowserCaptures'].append(dict(capture=str(path),api=str(api),reason='D3D11 browser compositor; original WebGPU work is D3D12 with authenticated dispatch/MRT closure'))
                continue
            require(api in (rd.GraphicsAPI.Vulkan, rd.GraphicsAPI.D3D12), 'API')
            textures = {t.resourceId:t for t in controller.GetTextures()}
            pending = None
            capture_frames = []
            for action in flatten(controller.GetRootActions()):
                if action.flags & rd.ActionFlags.Dispatch:
                    require(pending is None, 'DISPATCH-ORDER')
                    controller.SetFrameEvent(action.eventId, True)
                    pipe = controller.GetPipelineState()
                    bindings = resources(pipe, rd.ShaderStage.Compute)
                    reads = [d for d in bindings if descriptor_resource(d) not in textures]
                    writes = [d for d in bindings if descriptor_resource(d) in textures]
                    require(len(reads) == len(writes) == 1, 'COMPUTE-BINDINGS')
                    rid = descriptor_resource(writes[0])
                    require(rid in textures and descriptor_resource(reads[0]) not in textures, 'COMPUTE-OWNERS')
                    pending = dict(event=action.eventId, input=reads[0], storage=rid,
                        groups=list(action.dispatchDimension), programs=[program(pipe, rd.ShaderStage.Compute, compute, api)])
                    continue
                if not action.flags & rd.ActionFlags.Drawcall:
                    continue
                if pending is None and api == rd.GraphicsAPI.D3D12:
                    controller.SetFrameEvent(action.eventId, True)
                    observed = [descriptor_resource(d) for d in controller.GetPipelineState().GetOutputTargets()]
                    observed = [rid for rid in observed if rid in textures]
                    originals = {o['owner'] for f in capture_frames for o in f['outputs']}
                    if originals.intersection(map(str, observed)):
                        canvases = [(f,o) for f in capture_frames for o in f['outputs'][:1] if o['owner'] in set(map(str,observed))]
                        require(len(observed) == len(canvases) == 1, 'EXTRA-ORIGINAL-DRAW')
                        f, canvas = canvases[0]
                        native_pipe = controller.GetPipelineState()
                        blends = native_pipe.GetColorBlends()
                        require(action.numIndices == 3 and not action.flags & rd.ActionFlags.Indexed
                            and blends and blends[0].writeMask == 8 and not blends[0].enabled
                            and not blends[0].logicOperationEnabled
                            and not resources(native_pipe,rd.ShaderStage.Fragment), 'PRESENT-ALPHA-STATE')
                        after = texture_bytes(controller, observed[0])
                        controller.SetFrameEvent(action.eventId - 1, True)
                        before = texture_bytes(controller, observed[0])
                        require(before == after and 'sha256:' + hashlib.sha256(after).hexdigest() == canvas['nativeSha256'], 'PRESENT-ALPHA-CONTENT-PRESERVED')
                        result['nativePresentationAlphaPasses'].append(dict(capture=str(path),event=action.eventId,
                            frameIndex=f['frameIndex'],owner=canvas['owner'],writeMask=8,bytesPreserved=len(after),nativeSha256=canvas['nativeSha256']))
                    else:
                        require(len(observed) != 2, 'EXTRA-MRT-DRAW')
                        result['excludedBrowserDraws'].append(dict(capture=str(path),event=action.eventId,
                            targets=list(map(str,observed)),reason='outside original compute/MRT owners; authenticated WebGPU trace contains no such work'))
                    continue
                require(pending is not None and action.numIndices == 3 and not action.flags & rd.ActionFlags.Indexed, 'DRAW-ORDER')
                require(cursor < len(workload['frames']), 'EXTRA-FRAME')
                frame = workload['frames'][cursor]
                extent = (frame['extent']['width'], frame['extent']['height'])
                controller.SetFrameEvent(action.eventId, True)
                pipe = controller.GetPipelineState()
                targets = [descriptor_resource(d) for d in pipe.GetOutputTargets()]
                targets = [rid for rid in targets if rid in textures]
                reads = resources(pipe, rd.ShaderStage.Fragment)
                read_owners = {descriptor_resource(d) for d in reads}
                input_owner = descriptor_resource(pending['input'])
                storage_owner = pending['storage']
                # Match the actual native closure before reading any oracle or output.
                require(len(targets) == 2 and len(set(targets)) == 2
                    and read_owners == {input_owner, storage_owner}
                    and not set(targets).intersection(read_owners), 'RESOURCE-EDGE')
                require(pending['groups'] == [extent[0], extent[1], 1], 'DISPATCH-EXTENT')
                raw = bytes(controller.GetBufferData(input_owner, pending['input'].byteOffset, 48))
                require(raw == struct.pack('<12f', *frame['values']), 'INPUT-BYTES')
                for descriptor in reads:
                    if descriptor_resource(descriptor) == input_owner:
                        require(descriptor.byteOffset == pending['input'].byteOffset
                            and bytes(controller.GetBufferData(input_owner, descriptor.byteOffset, 48)) == raw, 'BUFFER-CLOSURE')
                programs = pending['programs'] + [program(pipe, rd.ShaderStage.Vertex, vertex_bytes, api), program(pipe, rd.ShaderStage.Fragment, fragment, api)]
                observations = []
                for index, rid in enumerate(targets + [storage_owner]):
                    texture = textures[rid]
                    require((texture.width, texture.height) == extent, 'OUTPUT-EXTENT')
                    payload = texture_bytes(controller, rid)
                    if index < 2:
                        require('SRGB' not in texture.format.Name().upper(), 'COLOR-SPACE')
                        pixels = rgba8_pixels(payload, texture.format.Name())
                        expected = frame['expectedColor' + str(index)]
                        result['lastOutputObservation'] = dict(index=index, owner=str(rid), format=texture.format.Name(), first=list(pixels[0]) if pixels else [], expected=expected, unique=list(map(list, set(pixels))))
                        require(len(pixels) == extent[0] * extent[1]
                            and all(list(p) == expected for p in pixels), 'OUTPUT-' + str(index))
                    else:
                        require(texture.format.compByteWidth == 2 and texture.format.compCount == 4, 'STORAGE-FORMAT')
                        require(len(payload) == extent[0] * extent[1] * 8, 'STORAGE-SIZE')
                        expected = struct.pack('<4e', *frame['expectedStorage'])
                        require(all(payload[i:i+8] == expected for i in range(0, len(payload), 8)), 'STORAGE-OUTPUT')
                        pixels = [tuple(int(v * 255 + .5) for v in struct.unpack('<4e', payload[i:i+8]))
                            for i in range(0, len(payload), 8)]
                    image = output / (path.stem + '-frame' + str(cursor) + '-output' + str(index) + '.bmp')
                    write_bmp(str(image), extent[0], extent[1], pixels)
                    observations.append(dict(owner=str(rid), format=texture.format.Name(),
                        pixels=len(pixels), outputSha256='sha256:' + hashlib.sha256(payload if index == 2 else bytes(c for p in pixels for c in p)).hexdigest(), nativeSha256='sha256:' + hashlib.sha256(payload).hexdigest(), image=str(image)))
                post = controller.GetPostVSData(0, 0, rd.MeshDataStage.VSOut)
                require(post.numIndices == 3 and post.vertexByteStride >= 16, 'POSTVS')
                expected_positions = [(-1, -1, 0, 1), (3, -1, 0, 1), (-1, 3, 0, 1)]
                for vertex, expected in enumerate(expected_positions):
                    value = struct.unpack('<4f', bytes(controller.GetBufferData(post.vertexResourceId,
                        post.vertexByteOffset + vertex * post.vertexByteStride, 16)))
                    require(value == expected, 'POSTVS-POSITION')
                result['frames'].append(dict(capture=str(path), frameIndex=cursor,
                    dispatchEvent=pending['event'], drawEvent=action.eventId, extent=list(extent),
                    inputOwner=str(input_owner), inputBytes=48, programs=programs,
                    outputs=observations, verifiedPostVsVertices=3))
                capture_frames.append(result['frames'][-1])
                pending = None
                cursor += 1
            if required_owned:
                buffers = {b.resourceId:b for b in controller.GetBuffers()}
                for action in flatten(controller.GetRootActions()):
                    copied = (action.flags & rd.ActionFlags.Copy and action.copySource in textures
                        and action.copyDestination in buffers)
                    present = bool(action.flags & rd.ActionFlags.Present)
                    if not copied and not present:
                        continue
                    rid = action.copySource if copied else action.copyDestination
                    matches = [(f, role, o) for f in capture_frames for role, o in enumerate(f['outputs'])
                        if o['owner'] == str(rid) and (copied or role == 0)]
                    if not matches and api == rd.GraphicsAPI.D3D12:
                        result['excludedBrowserCopiesAndPresents'].append(dict(capture=str(path),event=action.eventId,
                            owner=str(rid),reason='outside authenticated original output owners'))
                        continue
                    require(len(matches) == 1, 'COPY-PRESENT-ORIGINAL-OWNER')
                    frame, role, observed = matches[0]
                    require(frame['dispatchEvent'] < frame['drawEvent'] < action.eventId, 'COPY-PRESENT-ORDER')
                    controller.SetFrameEvent(action.eventId, True)
                    native = texture_bytes(controller, rid)
                    texture = textures[rid]
                    canonical = native if role == 2 else bytes(c for p in rgba8_pixels(native, texture.format.Name()) for c in p)
                    require('sha256:' + hashlib.sha256(canonical).hexdigest() == observed['outputSha256'], 'COPY-PRESENT-OUTPUT')
                    entry = dict(capture=str(path), event=action.eventId, frameIndex=frame['frameIndex'],
                        outputRole=role, sourceImage=str(rid), outputSha256=observed['outputSha256'], bytes=len(native))
                    if copied:
                        if api == rd.GraphicsAPI.D3D12:
                            raw, footprint = RenderDocTextureBufferCopy.d3d12_color(controller, action, texture,
                                buffers[action.copyDestination])
                            entry['recordedFootprint'] = footprint
                        else:
                            raw = bytes(controller.GetBufferData(action.copyDestination, 0, len(native)))
                        require(raw == native, 'OWNED-BUFFER-BYTES')
                        entry['destinationBuffer'] = str(action.copyDestination)
                        result['ownedReadbackCopies'].append(entry)
                    else:
                        result['presentedOutputs'].append(entry)
                for frame in capture_frames:
                    copies = [c for c in result['ownedReadbackCopies'] if c['capture'] == str(path)
                        and c['frameIndex'] == frame['frameIndex']]
                    require(len(copies) == 3 and {c['outputRole'] for c in copies} == {0,1,2}, 'OWNED-COPY-COVERAGE')
                    require(len({c['destinationBuffer'] for c in copies}) == 3, 'OWNED-BUFFER-DISTINCT')
                    presents = [p for p in result['presentedOutputs'] if p['capture'] == str(path)
                        and p['frameIndex'] == frame['frameIndex']]
                    require(bool(presents) and all(p['event'] > max(c['event'] for c in copies) for p in presents), 'COPY-PRESENT-JOIN')
            require(pending is None, 'INCOMPLETE-CLOSURE')
            errors = [m.description for m in controller.GetDebugMessages() if 'Error' in str(m.severity)]
            require(not errors, 'REPLAY-ERRORS: ' + repr(errors))
        finally:
            controller.Shutdown()
            cap.Shutdown()
    require(cursor == len(workload['frames']), 'COVERAGE')
    result.update(success=True, captures=len(captures), uniqueFrames=cursor, replayErrors=[],
        workloadSha256='sha256:' + hashlib.sha256(source).hexdigest())
except Exception:
    result['error'] = traceback.format_exc()
(output / 'replay.json').write_text(json.dumps(result, indent=2))
os._exit(0 if result['success'] else 1)
