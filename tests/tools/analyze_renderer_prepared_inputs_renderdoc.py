"""Replay actual cold/prepared Shader consumers; never infer from debug labels."""
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import sys
import traceback

import renderdoc as rd

root = Path(os.environ['ARP_REPO_ROOT'])
sys.path.insert(0, str(root / 'tests/tools'))
from analyze_ui_text_renderdoc import descriptor_resource, flatten, rgba8_pixels, texture_bytes, write_bmp


def main():
    path = Path(os.environ['ARP_RENDERER_PREPARED_CAPTURE'])
    report = dict(success=False, productionReady=False, capture=str(path), draws=[])
    cap = rd.OpenCaptureFile()
    controller = None
    try:
        assert cap.OpenFile(str(path), '', None).code == rd.ResultCode.Succeeded
        status, controller = cap.OpenCapture(rd.ReplayOptions(), None)
        assert status.code == rd.ResultCode.Succeeded
        assert controller.GetAPIProperties().pipelineType == rd.GraphicsAPI.Vulkan
        names = ('fxaa', 'wocc_taa', 'ssao', 'display_transfer', 'scene_2d_sprites')
        modules = {}
        for directory in (root / 'build/win-vulkan-debug/shaders',
                          root / 'build/win-vulkan-debug/renderer-prepared-input-fixture'):
            for name in names:
                modules[hashlib.sha256((directory / (name + '.spv')).read_bytes()).hexdigest()] = name
        textures = {t.resourceId: t for t in controller.GetTextures()}
        expected_push = dict(fxaa=24, wocc_taa=64, ssao=40, display_transfer=16, scene_2d_sprites=16)
        seen = {name: [] for name in names}
        for action in flatten(controller.GetRootActions()):
            if not action.flags & rd.ActionFlags.Drawcall:
                continue
            controller.SetFrameEvent(action.eventId, True)
            pipe = controller.GetPipelineState()
            shaders = [pipe.GetShaderReflection(s) for s in (rd.ShaderStage.Vertex, rd.ShaderStage.Fragment)]
            if not all(shaders):
                continue
            shader_names = [modules.get(hashlib.sha256(bytes(s.rawBytes)).hexdigest()) for s in shaders]
            if shader_names[1] is None:
                continue
            name = shader_names[1]
            assert shader_names == [name, name], shader_names
            fragment = shaders[1]
            blocks = [b for shader in shaders for b in shader.constantBlocks if not b.bufferBacked]
            assert blocks and all(b.byteSize == expected_push[name] for b in blocks), (
                name, [b.byteSize for b in blocks])
            sampled = []
            for used in pipe.GetReadOnlyResources(rd.ShaderStage.Fragment):
                declaration = fragment.readOnlyResources[used.access.index]
                rid = descriptor_resource(used.descriptor)
                assert rid in textures
                sampled.append(dict(name=declaration.name, set=declaration.fixedBindSetOrSpace,
                                    binding=declaration.fixedBindNumber, resource=str(rid)))
            outputs = []
            for index, attachment in enumerate(pipe.GetOutputTargets()):
                rid = descriptor_resource(attachment)
                if rid not in textures:
                    continue
                texture = textures[rid]
                raw = texture_bytes(controller, rid)
                fmt = texture.format
                item = dict(extent=[texture.width, texture.height], format=fmt.Name(),
                            sha256=hashlib.sha256(raw).hexdigest(), finite=True)
                if fmt.compType == rd.CompType.Float:
                    code = {2: 'e', 4: 'f'}[fmt.compByteWidth]
                    values = struct.unpack('<' + str(len(raw) // fmt.compByteWidth) + code, raw)
                    assert all(math.isfinite(v) for v in values), (name, action.eventId)
                    item.update(min=min(values), max=max(values))
                elif fmt.compCount in (1, 4) and fmt.compByteWidth == 1:
                    pixels = (rgba8_pixels(raw, fmt.Name()) if fmt.compCount == 4
                              else [(v, v, v, 255) for v in raw])
                    assert len(pixels) == texture.width * texture.height
                    filename = path.with_suffix('.' + name + '-' + str(len(seen[name])) + '-' + str(index) + '.bmp')
                    write_bmp(str(filename), texture.width, texture.height, pixels)
                    levels = {p[:3] for p in pixels}
                    item.update(image=str(filename), rgbLevels=len(levels))
                    if name in ('fxaa', 'wocc_taa', 'ssao'):
                        assert len(levels) > 1, (name, len(levels))
                    if name == 'display_transfer':
                        assert len(levels) == 1
                        pixel = pixels[0]
                        # Input is an explicit R16G16B16A16 clear. Derive the
                        # sRGB oracle from its actual half-float sampled bytes.
                        source_id, = [descriptor_resource(d.descriptor)
                            for d in pipe.GetReadOnlyResources(rd.ShaderStage.Fragment)
                            if fragment.readOnlyResources[d.access.index].name == 'LinearSceneColor']
                        source = struct.unpack_from('<4e', texture_bytes(controller, source_id))
                        encoded = [round((12.92 * v if v <= .0031308 else 1.055 * v ** (1/2.4) - .055) * 255)
                                   for v in source[:3]]
                        assert all(abs(a-b) <= 1 for a, b in zip(pixel[:3], encoded)), (pixel, encoded)
                        item['srgbPixel'] = list(pixel)
                else:
                    raise AssertionError((name, fmt.Name()))
                outputs.append(item)
            assert outputs
            draw = dict(module=name, event=action.eventId, vertices=action.numIndices,
                        instances=action.numInstances, sampled=sampled, outputs=outputs)
            seen[name].append(draw)
            report['draws'].append(draw)
        assert {name: len(draws) for name, draws in seen.items()} == dict(
            fxaa=1, wocc_taa=3, ssao=1, display_transfer=1, scene_2d_sprites=3)
        assert sum(d['instances'] for d in seen['scene_2d_sprites']) == 5
        assert seen['wocc_taa'][0]['outputs'][0]['sha256'] == seen['wocc_taa'][2]['outputs'][0]['sha256']
        errors = [str(m) for m in controller.GetDebugMessages()
                  if 'Error' in str(m.severity) or 'High' in str(m.severity)]
        assert not errors, errors
        report.update(success=True, nativeModules=5, guiOrCpuReplayUsed=False,
                      hostReadbackMetricsUsed=False, measurementsFromRenderDoc=True,
                      taaResetMatchesFirst=True, replayErrors=errors)
    except Exception:
        report['error'] = traceback.format_exc()
    finally:
        if controller:
            controller.Shutdown()
        cap.Shutdown()
        path.with_suffix('.replay.json').write_text(json.dumps(report, indent=2))
    return report['success']


if __name__ == '__main__':
    os._exit(0 if main() else 1)
