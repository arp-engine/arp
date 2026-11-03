"""Launch the native Editor under RenderDoc and replay saved/conflicted UI states.

Executed by qrenderdoc --python; the companion runner supplies an explicit
isolated project, loopback port, output folder and executable in a config file.
"""
import os
import sys

try:
    with open(os.environ['ARP_SOURCE_SCRIPT_CAPTURE'] + '.bootstrap', 'w') as boot:
        boot.write('entered\n')
    import json
    import math
    from pathlib import Path
    import shutil
    import struct
    import time
    import traceback
    import renderdoc as rd
except Exception as error:
    with open(os.environ['ARP_SOURCE_SCRIPT_CAPTURE'] + '.bootstrap', 'a') as boot:
        boot.write(repr(error))
    os._exit(1)

config = json.loads(Path(os.environ['ARP_SOURCE_SCRIPT_CAPTURE']).read_text())
output = Path(config['output'])
with open(os.environ['ARP_SOURCE_SCRIPT_CAPTURE'] + '.bootstrap', 'a') as boot:
    boot.write('imports complete\n')
sys.path.insert(0, str(Path(config['root']) / 'tests/tools'))
from analyze_ui_text_renderdoc import (
    descriptor_resource, flatten, rgba8_pixels, texture_bytes, write_bmp,
)


def replay(path, phase):
    capture = rd.OpenCaptureFile()
    controller = None
    try:
        assert capture.OpenFile(str(path), '', None).code == rd.ResultCode.Succeeded
        status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
        assert status.code == rd.ResultCode.Succeeded and controller
        assert controller.GetAPIProperties().pipelineType == rd.GraphicsAPI.Vulkan
        draws = [action for action in flatten(controller.GetRootActions())
                 if action.flags & rd.ActionFlags.Drawcall]
        assert draws, 'SOURCE-SAVE-CAPTURE-NO-DRAWS'
        textures = {texture.resourceId: texture for texture in controller.GetTextures()}
        samples = []
        sampled = set()
        non_gui = []
        for index in (0, len(draws) // 2, len(draws) - 1):
            while index < len(draws):
                action = draws[index]
                controller.SetFrameEvent(action.eventId, True)
                pipe = controller.GetPipelineState()
                shader = pipe.GetShaderReflection(rd.ShaderStage.Fragment)
                if shader and any(resource.name == 'GuiAtlasTexture'
                                  for resource in shader.readOnlyResources):
                    break
                non_gui.append(dict(event=action.eventId, shader=shader.entryPoint if shader else None))
                index += 1
            assert index < len(draws), 'SOURCE-SAVE-CAPTURE-NO-GUI-SAMPLE'
            assert shader and any(resource.name == 'GuiAtlasTexture'
                                  for resource in shader.readOnlyResources)
            blend = pipe.GetColorBlends()[0]
            assert blend.enabled and blend.writeMask == 15
            # Native GuiRenderer::enableGuiAlphaBlend uses straight color alpha.
            assert str(blend.colorBlend.source) == 'BlendMultiplier.SrcAlpha'
            assert str(blend.colorBlend.destination) == 'BlendMultiplier.InvSrcAlpha'
            assert str(blend.alphaBlend.source) == 'BlendMultiplier.One'
            assert str(blend.alphaBlend.destination) == 'BlendMultiplier.InvSrcAlpha'
            for access in pipe.GetReadOnlyResources(rd.ShaderStage.Fragment):
                resource = descriptor_resource(access.descriptor)
                if resource in textures:
                    sampled.add(resource)
            samples.append(dict(event=action.eventId, indices=action.numIndices,
                blend=[str(blend.colorBlend.source), str(blend.colorBlend.destination)]))
        viewport_samples = []
        if config.get('requireHdrViewport', config.get('sceneWorkflow')):
            # The native viewport's explicit default profile is RGBA16Float.
            # Its event-driven renderer may reuse a cached target in this frame;
            # verify the GUI actually samples that populated GPU texture.
            for action in draws:
                controller.SetFrameEvent(action.eventId, True)
                pipe = controller.GetPipelineState()
                shader = pipe.GetShaderReflection(rd.ShaderStage.Fragment)
                if not shader or not any(resource.name == 'GuiAtlasTexture'
                                         for resource in shader.readOnlyResources):
                    continue
                for access in pipe.GetReadOnlyResources(rd.ShaderStage.Fragment):
                    resource = descriptor_resource(access.descriptor)
                    texture = textures.get(resource)
                    if not texture or texture.format.compByteWidth != 2 or texture.format.compCount != 4:
                        continue
                    assert texture.format.compType == rd.CompType.Float
                    data = texture_bytes(controller, resource)
                    assert len(data) == texture.width * texture.height * 8
                    step = max(1, texture.width * texture.height // 2048)
                    pixels = [struct.unpack_from('<4e', data, index * 8)[:3]
                              for index in range(0, texture.width * texture.height, step)]
                    assert all(math.isfinite(value) for pixel in pixels for value in pixel)
                    assert len(set(pixels)) > 1 and any(any(value > 0 for value in pixel) for pixel in pixels)
                    viewport_samples.append(dict(event=action.eventId, texture=str(resource),
                        extent=[texture.width, texture.height], format=texture.format.Name(),
                        sampledPixels=len(pixels)))
                if viewport_samples:
                    break
            assert viewport_samples, 'SOURCE-SAVE-CAPTURE-VIEWPORT-TEXTURE-NOT-SAMPLED'
        assert sampled and any(textures[resource].width > 1 and textures[resource].height > 1
            and len(set(texture_bytes(controller, resource))) > 1 for resource in sampled)
        controller.SetFrameEvent(draws[-1].eventId, True)
        target = descriptor_resource(controller.GetPipelineState().GetOutputTargets()[0])
        texture = textures[target]
        data = texture_bytes(controller, target)
        assert len(data) == texture.width * texture.height * 4
        pixels = rgba8_pixels(data, texture.format.Name())
        assert len(set(pixels)) > 1, 'SOURCE-SAVE-CAPTURE-CONSTANT-FRAME'
        image = output / (phase + '.bmp')
        write_bmp(str(image), texture.width, texture.height, pixels)
        messages = [dict(severity=str(message.severity), text=str(message.description))
                    for message in controller.GetDebugMessages()]
        assert not [message for message in messages if message['severity'] == 'MessageSeverity.High']
        return dict(phase=phase, capture=str(path), image=str(image), draws=len(draws),
            extent=[texture.width, texture.height], format=texture.format.Name(),
            nativeApi='Vulkan', samples=samples, nonGuiDraws=non_gui, viewportSamples=viewport_samples,
            sampledTextures=[dict(id=str(resource), width=textures[resource].width,
                height=textures[resource].height) for resource in sampled],
            replayMessages=messages)
    finally:
        if controller:
            controller.Shutdown()
        capture.Shutdown()


report = dict(success=False, productionReady=False, phases=[])
target = None
pid = 0
try:
    if os.environ.get('ARP_SOURCE_SCRIPT_REPLAY_ONLY') != '1':
        options = rd.GetDefaultCaptureOptions()
        launch = rd.ExecuteAndInject(config['editor'], config['build'], config['arguments'], [],
            str(output / 'editor'), options, False)
        assert launch.result.code == rd.ResultCode.Succeeded, str(launch.result)
        target = rd.CreateTargetControl('', launch.ident, 'ARP Source Script Save', True)
        assert target
        pid = target.GetPID()
        (output / 'target-pid').write_text(str(pid))
        phases = tuple(config.get('phases', ['saved', 'conflict']))
        phase = 0
        armed = False
        deadline = time.monotonic() + 100
        while target.Connected() and time.monotonic() < deadline:
            if not armed and (output / (phases[phase] + '-ready')).exists():
                target.TriggerCapture(1)
                armed = True
                (output / (phases[phase] + '-armed')).write_text('1')
            message = target.ReceiveMessage(None)
            if message.type == rd.TargetControlMessageType.NewCapture:
                captured = Path(str(message.newCapture.path))
                if not armed:
                    report['startupCapture'] = str(captured)
                    (output / 'startup-captured').write_text('1')
                else:
                    shutil.copyfile(str(captured), str(output / (phases[phase] + '.rdc')))
                    (output / (phases[phase] + '-captured')).write_text('1')
                    phase += 1
                    armed = False
                    if phase == len(phases):
                        break
            time.sleep(0.02)
        assert phase == len(phases), 'SOURCE-SAVE-CAPTURE-FRAME-DEADLINE'
        target.Shutdown()
        target = None
    for phase in config.get('phases', ['saved', 'conflict']):
        report['phases'].append(replay(output / (phase + '.rdc'), phase))
    report['success'] = True
except Exception:
    report['error'] = traceback.format_exc()
finally:
    if target:
        target.Shutdown()
    if pid:
        os.system('taskkill /PID {} /T /F >NUL 2>&1'.format(pid))
    (output / 'renderdoc-report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
os._exit(0)
