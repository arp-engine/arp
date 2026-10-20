"""Run in qrenderdoc: verify original Uint32/instanced/depth workload replay."""
import json
import os
from pathlib import Path
import struct
import sys
import traceback
import renderdoc as rd

sys.path.insert(0, str(Path(os.environ['ARP_REPO_ROOT']) / 'tests/tools'))
from analyze_ui_text_renderdoc import flatten, descriptor_resource, texture_bytes, rgba8_pixels, write_bmp


def analyze(path):
    capture = rd.OpenCaptureFile()
    if capture.OpenFile(str(path), '', None).code != rd.ResultCode.Succeeded:
        raise RuntimeError('WEBGL-EXTENSION-CAPTURE-OPEN')
    status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
    if status.code != rd.ResultCode.Succeeded or not controller:
        raise RuntimeError('WEBGL-EXTENSION-CAPTURE-REPLAY')
    report = dict(capture=str(path), nativeApi=str(controller.GetAPIProperties().pipelineType), draws=[])
    try:
        textures = {t.resourceId: t for t in controller.GetTextures()}
        for action in flatten(controller.GetRootActions()):
            if not action.flags & rd.ActionFlags.Drawcall or (action.numIndices, action.numInstances) != (6, 2):
                continue
            controller.SetFrameEvent(action.eventId, True)
            pipe = controller.GetPipelineState()
            attributes = [a for a in pipe.GetVertexInputs() if a.used]
            if [a.format.compCount for a in attributes] != [2, 2, 2, 4]:
                continue
            if not action.flags & rd.ActionFlags.Indexed:
                raise RuntimeError('WEBGL-EXTENSION-INDEXED-PAYLOAD')
            index = pipe.GetIBuffer()
            if index.byteStride != 4:
                raise RuntimeError('WEBGL-EXTENSION-UINT32-STRIDE')
            indices = struct.unpack('<6I', bytes(controller.GetBufferData(index.resourceId, index.byteOffset, 24)))
            if indices != (65536, 65537, 65538, 65536, 65538, 65539):
                raise RuntimeError('WEBGL-EXTENSION-UINT32-ORIGINAL-BYTES: ' + repr(indices))
            outputs = [descriptor_resource(d) for d in pipe.GetOutputTargets()]
            outputs = [r for r in outputs if r in textures and (textures[r].width, textures[r].height) == (64,64)]
            if len(outputs) != 1:
                raise RuntimeError('WEBGL-EXTENSION-OUTPUT')
            pixels = rgba8_pixels(texture_bytes(controller, outputs[0]), textures[outputs[0]].format.Name())
            if len(pixels) != 4096 or any(tuple(pixel) != ((255,0,0,255) if i%64<32 else (0,255,0,255))
                    for i,pixel in enumerate(pixels)):
                raise RuntimeError('WEBGL-EXTENSION-INSTANCE-DEPTH-DERIVATIVE-PIXELS')
            rates = sorted((bool(a.perInstance),int(a.instanceRate)) for a in attributes)
            if rates != [(False,0),(False,0),(True,1),(True,1)]:
                raise RuntimeError('WEBGL-EXTENSION-INSTANCE-DIVISORS: '+repr(rates))
            sampled = [descriptor_resource(d.descriptor) for d in pipe.GetReadOnlyResources(rd.ShaderStage.Fragment)]
            if len(sampled) != 1 or sampled[0] not in textures:
                raise RuntimeError('WEBGL-EXTENSION-DEPTH-DESCRIPTOR')
            depth_texture = textures[sampled[0]]
            if (depth_texture.width,depth_texture.height) != (64,64):
                raise RuntimeError('WEBGL-EXTENSION-DEPTH-EXTENT')
            depth_target = descriptor_resource(pipe.GetDepthTarget())
            if sampled[0] == depth_target:
                raise RuntimeError('WEBGL-EXTENSION-DEPTH-FEEDBACK')
            depth_bytes = texture_bytes(controller,sampled[0])
            depth_format = depth_texture.format.Name()
            if depth_format == 'D24S8_TYPELESS':
                depths = [((v & 0xffffff) / 16777215) for (v,) in struct.iter_unpack('<I',depth_bytes)]
            else:
                raise RuntimeError('WEBGL-EXTENSION-DEPTH-READBACK-FORMAT: '+depth_format)
            if len(depths) != 4096 or any(abs(v-.25) > 1/16777215 for v in depths):
                raise RuntimeError('WEBGL-EXTENSION-DEPTH-VALUES')
            shaders = [pipe.GetShaderReflection(stage) for stage in (rd.ShaderStage.Vertex, rd.ShaderStage.Fragment)]
            if any(s is None for s in shaders) or pipe.GetColorBlends()[0].enabled:
                raise RuntimeError('WEBGL-EXTENSION-SHADER-STATE')
            image = path.with_name(path.stem + '-extensions.bmp')
            write_bmp(str(image), 64,64,pixels)
            report['draws'].append(dict(event=action.eventId,indices=indices,instances=action.numInstances,
                indexByteStride=index.byteStride,sampledDepth=str(sampled[0]),depthTarget=str(depth_target),
                depthFormat=depth_format,verifiedDepthPixels=len(depths),depthValue=.25,verifiedPixels=4096,redPixels=2048,greenPixels=2048,
                attributes=[dict(name=a.name,buffer=a.vertexBuffer,offset=a.byteOffset,
                    components=a.format.compCount,perInstance=a.perInstance,instanceRate=a.instanceRate) for a in attributes],
                vertexShader=str(shaders[0].resourceId),fragmentShader=str(shaders[1].resourceId),image=str(image)))
        errors = [m.description for m in controller.GetDebugMessages() if 'Error' in str(m.severity)]
        if errors:
            raise RuntimeError('WEBGL-EXTENSION-REPLAY-ERRORS: ' + repr(errors))
        report['replayErrors'] = errors
        return report
    finally:
        controller.Shutdown()
        capture.Shutdown()


output = Path(os.environ['ARP_WEBGL_PROGRAM_CAPTURE'])
result = dict(success=False)
try:
    runtime = json.loads((output/'runtime.json').read_text())
    if not runtime.get('success') or runtime.get('type') != 'WebGlExtensionExecutionSmoke':
        raise RuntimeError('WEBGL-EXTENSION-RUNTIME-FAILED')
    frames = [analyze(path) for path in sorted(output.glob('program_frame*.rdc'))]
    draws = [draw for frame in frames for draw in frame['draws']]
    if not draws:
        raise RuntimeError('WEBGL-EXTENSION-CAPTURE-DRAW-COVERAGE')
    result.update(success=True,frames=frames,verifiedDraws=len(draws),image=draws[-1]['image'],
        scope='Original WebGL1 Prototype extension execution; platform and production remain open')
except Exception:
    result['error'] = traceback.format_exc()
(output/'replay.json').write_text(json.dumps(result,indent=2))
os._exit(0 if result['success'] else 1)
