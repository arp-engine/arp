"""Replay actual fog passes, export output and preserve binding/timing evidence."""
import json
import math
import os
import struct
import sys
import traceback

import renderdoc as rd

sys.path.insert(0, os.path.join(os.environ['ARP_REPO_ROOT'], 'tests', 'tools'))
from analyze_ui_text_renderdoc import flatten, descriptor_resource, texture_bytes, rgba8_pixels, write_bmp


def inspect_integral(controller, event):
    controller.SetFrameEvent(event, True)
    pipe = controller.GetPipelineState()
    stage = rd.ShaderStage.Compute
    reflection = pipe.GetShaderReflection(stage)
    bindings = list(pipe.GetReadWriteResources(stage))
    integral = next(b.descriptor for b in bindings
        if reflection.readWriteResources[b.access.index].fixedBindNumber == 2
        and reflection.readWriteResources[b.access.index].fixedBindSetOrSpace == 0)
    uniform = next(b.descriptor for b in pipe.GetConstantBlocks(stage)
        if reflection.constantBlocks[b.access.index].fixedBindNumber == 0
        and reflection.constantBlocks[b.access.index].fixedBindSetOrSpace == 0
        and reflection.constantBlocks[b.access.index].byteSize in (768, 784))
    constants = bytes(controller.GetBufferData(uniform.resource, uniform.byteOffset, uniform.byteSize))
    gx, gy, gz = struct.unpack_from('<3I', constants, 592)
    floats = struct.unpack('<%df' % (len(constants) // 4), constants)
    ambient = floats[192:195] if len(constants) == 784 else (0, 0, 0)
    data = bytes(controller.GetBufferData(integral.resource, integral.byteOffset, gx * gy * gz * 16))
    records = list(struct.iter_unpack('<4f', data))
    assert len(records) == gx * gy * gz
    assert all(all(math.isfinite(v) and v >= 0 for v in cell) and cell[3] <= 1.000001 for cell in records)
    assert all(records[i][3] <= records[i - gx * gy][3] + 0.000001
        for i in range(gx * gy, len(records))), 'transmittance increases along ray'
    result = {'cells': len(records), 'grid': [gx, gy, gz],
        'minTransmittance': min(r[3] for r in records),
        'maxRadiance': max(max(r[:3]) for r in records),
        'finiteNonnegative': True, 'monotoneTransmittance': True}
    if os.environ.get('ARP_FOG_HOMOGENEOUS') == '1':
        assert floats[161] == 0 and floats[167] == 0, 'fixture must be homogeneous and isotropic'
        def world(x, y, depth):
            v = [(x + 0.5) / gx * 2 - 1, (y + 0.5) / gy * 2 - 1, depth, 1]
            w = [sum(floats[r * 4 + c] * v[c] for c in range(4)) for r in range(4)]
            return [n / w[3] for n in w[:3]]
        def view_z(w):
            return abs(sum(floats[24 + c] * (w + [1])[c] for c in range(4)))
        probes = []
        for x, y in [(0, 0), (gx - 1, 0), (gx // 2, gy // 2), (0, gy - 1), (gx - 1, gy - 1)]:
            a, b = world(x, y, 0), world(x, y, 1)
            ray_scale = math.sqrt(sum((b[i] - a[i]) ** 2 for i in range(3))) / (view_z(b) - view_z(a))
            for z in [0, gz // 2, gz - 1]:
                distance = floats[152] * ((floats[153] / floats[152]) ** ((z + 1.0) / gz) - 1) * ray_scale
                transmittance = math.exp(-floats[160] * distance)
                expected = [floats[164 + c] * (floats[172 + c] * floats[171]
                    / (4 * math.pi) + ambient[c]) * (1 - transmittance) for c in range(3)] + [transmittance]
                actual = records[(z * gy + y) * gx + x]
                error = max(abs(actual[i] - expected[i]) for i in range(4))
                assert error < 0.0002, (x, y, z, actual, expected, error)
                probes.append({'cell': [x, y, z], 'actual': actual, 'expected': expected, 'error': error})
        result['homogeneousProbes'] = probes
        result['maxAbsoluteError'] = max(p['error'] for p in probes)
    return result


def main():
    path = os.environ['ARP_RENDERDOC_ANALYZE_CAPTURE']
    capture = rd.OpenCaptureFile()
    status=capture.OpenFile(path, '', None)
    assert (status.code == rd.ResultCode.Succeeded if hasattr(status,'code') else status == rd.ReplayStatus.Succeeded), str(status)
    status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
    assert (status.code == rd.ResultCode.Succeeded if hasattr(status,'code') else status == rd.ReplayStatus.Succeeded) and controller
    try:
        actions = list(flatten(controller.GetRootActions() if hasattr(controller,'GetRootActions') else controller.GetDrawcalls()))
        flags=rd.ActionFlags if hasattr(rd,'ActionFlags') else rd.DrawFlags
        work = [a for a in actions if a.flags & (flags.Drawcall | flags.Dispatch)]
        dispatches = [a for a in work if a.flags & flags.Dispatch]
        assert len(dispatches) == int(os.environ.get('ARP_FOG_EXPECT_DISPATCHES', '4')), len(dispatches)
        report = {'contract': 'VolumetricFog/1', 'capture': path, 'api': str(controller.GetAPIProperties().pipelineType), 'passes': []}
        for action in work:
            controller.SetFrameEvent(action.eventId, True)
            pipe = controller.GetPipelineState()
            stage = rd.ShaderStage.Compute if action.flags & flags.Dispatch else rd.ShaderStage.Fragment
            shader = pipe.GetShaderReflection(stage)
            assert shader
            report['passes'].append({'event': action.eventId, 'entry': shader.entryPoint,
                'dispatch': list(action.dispatchDimension) if action.flags & flags.Dispatch else None,
                'readOnlyBindings': len(pipe.GetReadOnlyResources(stage)),
                'readWriteBindings': len(pipe.GetReadWriteResources(stage))})
        if dispatches:
            report['integral'] = inspect_integral(controller, dispatches[-1].eventId)
        final = work[-1]
        controller.SetFrameEvent(final.eventId, True)
        output = descriptor_resource(controller.GetPipelineState().GetOutputTargets()[0])
        textures = {t.resourceId: t for t in controller.GetTextures()}
        target = textures[output]
        pixels = rgba8_pixels(texture_bytes(controller, output), target.format.Name())
        assert len(set(pixels)) >= 3, 'blank or constant output'
        bmp = os.path.splitext(path)[0] + '.bmp'
        write_bmp(bmp, target.width, target.height, pixels)
        errors = [m.description for m in controller.GetDebugMessages() if 'Error' in str(m.severity)]
        assert not errors, errors
        counters = controller.FetchCounters([rd.GPUCounter.EventGPUDuration])
        report.update(width=target.width, height=target.height, screenshot=bmp,
            errors=errors, gpuReplayMs=sum(c.value.d for c in counters
                if c.counter == rd.GPUCounter.EventGPUDuration) * 1000)
        report['resources'] = [{'id': str(r.resourceId), 'name': r.name} for r in controller.GetResources()]
        with open(os.path.splitext(path)[0] + '.json', 'w') as f:
            json.dump(report, f, indent=2)
    finally:
        controller.Shutdown()
        capture.Shutdown()


try:
    main()
    os._exit(0)
except Exception:
    with open(os.environ['ARP_RENDERDOC_ANALYZE_CAPTURE'] + '.error.txt', 'w') as f:
        f.write(traceback.format_exc())
    os._exit(1)
