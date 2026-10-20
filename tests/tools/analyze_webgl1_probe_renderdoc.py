"""Replay the GLES2 compiler/bridge probe; this does not validate an RHI backend.

Run in qrenderdoc with ARP_WEBGL1_CAPTURE_PATH set to the actual capture.
Raw JSON is test transport, normalized to the evidence YAML by the host driver.
"""
import hashlib
import json
import os
import struct
import sys
import traceback

import renderdoc as rd

sys.path.insert(0, os.path.join(os.environ["ARP_REPO_ROOT"], "tests", "tools"))
from analyze_ui_text_renderdoc import (
    flatten, action_name, descriptor_resource, texture_bytes, rgba8_pixels, write_bmp)
sys.path.insert(0, os.path.join(os.environ["ARP_REPO_ROOT"], "tests", "tooling"))
from webgl1_probe_report_contract import WebGlProbeReportContract


def analyze(path):
    with open(os.path.join(os.path.dirname(path), "runtime.json"), "r") as stream:
        runtime = json.load(stream)
    version = runtime.get("contractVersion")
    if version == WebGlProbeReportContract.VERSION:
        WebGlProbeReportContract.validate(runtime)
    elif version not in (1, 2):
        raise RuntimeError("RHI-WEBGL1-PROBE-CAPTURE-RUNTIME-VERSION")
    if (runtime.get("type") != "WebGl1RuntimeProbe"
            or runtime.get("success") is not True
            or not runtime.get("version", "").startswith("WebGL 1.0")
            or runtime.get("uploadSourceMutationProbe") is not True
            or runtime.get("contextAttributes", {}).get("alpha") is not False):
        raise RuntimeError("RHI-WEBGL1-PROBE-CAPTURE-RUNTIME-IDENTITY")
    if version != 1 and any(runtime.get(field) is not True for field in (
            "textureSubImageSourceMutationProbe", "bufferSubDataSourceMutationProbe", "issuedObjectDeletionProbe")):
        raise RuntimeError("RHI-WEBGL1-PROBE-CAPTURE-LIFETIME-RESULT")
    capture = rd.OpenCaptureFile()
    if capture.OpenFile(path, "", None).code != rd.ResultCode.Succeeded:
        raise RuntimeError("RHI-WEBGL1-PROBE-CAPTURE-OPEN")
    status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
    if status.code != rd.ResultCode.Succeeded or not controller:
        capture.Shutdown()
        raise RuntimeError("RHI-WEBGL1-PROBE-CAPTURE-REPLAY: " + str(status))
    try:
        if controller.GetAPIProperties().pipelineType != rd.GraphicsAPI.D3D11:
            raise RuntimeError("RHI-WEBGL1-PROBE-CAPTURE-NATIVE-API-UNVALIDATED")
        report = {"type": "WebGl1ProbeCapture", "contractVersion": 2, "runtimeContractVersion": version, "status": "ProbeOnly",
            "productionBackendValidated": False, "gpuCompletionClaimed": False,
            "capture": path, "nativeApi": str(controller.GetAPIProperties().pipelineType), "draws": []}
        textures = {t.resourceId: t for t in controller.GetTextures()}
        expected_vertices = (-1., -1., 0., 0., 0., 3., -1., 0., 1., 0., -1., 3., 0., 0., 1.)
        for action in flatten(controller.GetRootActions()):
            if not action.flags & rd.ActionFlags.Drawcall:
                continue
            controller.SetFrameEvent(action.eventId, True)
            pipe = controller.GetPipelineState()
            outputs = [descriptor_resource(d) for d in pipe.GetOutputTargets()]
            outputs = [rid for rid in outputs if rid in textures
                       and textures[rid].width == 64 and textures[rid].height == 64
                       and textures[rid].format.Name() == "R8G8B8A8_UNORM"]
            if not outputs:
                continue  # Browser compositor draws are outside this fixture.
            # RenderDoc's D3D11 non-instanced Draw action carries numInstances=0.
            # The Instanced flag, rather than an invented instance count, is
            # the semantic distinction required by this non-instanced fixture.
            if action.numIndices != 3 or action.flags & rd.ActionFlags.Instanced:
                raise RuntimeError("RHI-WEBGL1-PROBE-CAPTURE-DRAW-ARGS")
            streams = pipe.GetVBuffers()
            if not streams or streams[0].byteStride != 20:
                raise RuntimeError("RHI-WEBGL1-PROBE-CAPTURE-VERTEX-STRIDE")
            stream = streams[0]
            draw_index = len(report["draws"])
            vertices = expected_vertices if draw_index < 4 else expected_vertices[:10] + (-1., -5., 0., 0., 1.)
            data = bytes(controller.GetBufferData(stream.resourceId, stream.byteOffset, 60))
            if len(data) != 60 or struct.unpack("<15f", data) != vertices:
                raise RuntimeError("RHI-WEBGL1-PROBE-CAPTURE-UPLOAD-COPY")
            blend = pipe.GetColorBlends()[0]
            # The explicitly alpha=false canvas has RGB semantics. ANGLE's
            # D3D11 path masks native alpha writes (7); an RGBA backing with
            # all writes (15) is also valid for that same opaque canvas.
            if blend.enabled or blend.writeMask not in (7, 15):
                raise RuntimeError("RHI-WEBGL1-PROBE-CAPTURE-BLEND: "
                    + repr((blend.enabled, blend.writeMask)))
            pixels = rgba8_pixels(texture_bytes(controller, outputs[0]), textures[outputs[0]].format.Name())
            red_value, green_value = (128, 255) if draw_index < 2 else (64, 128)
            red = sum(1 for r,g,b,a in pixels if (r,g,b) == (red_value,0,0))
            green = sum(1 for r,g,b,a in pixels if (r,g,b) == (0,green_value,0))
            # This ANGLE opaque-canvas fixture's native backing rows equal
            # readPixels y. The off-center stage-2 probes distinguish that
            # mapping from a 63-y flip; center-only probes cannot prove it.
            # This does not define a backend-neutral attachment convention.
            samples = [pixels[32 * 64 + x][:3] for x in (28, 40, 18)]
            bottom_samples = [pixels[10 * 64 + x][:3] for x in (28, 40)]
            expected_texel = bytes((128, 255, 64, 255) if draw_index < 2 else (64, 128, 255, 255))
            texels = [texture_bytes(controller, t.resourceId) for t in textures.values()
                      if t.width == 1 and t.height == 1 and t.format.Name() == "R8G8B8A8_UNORM"]
            if expected_texel not in texels:
                raise RuntimeError("RHI-WEBGL1-PROBE-CAPTURE-TEXTURE-COPY")
            shader = pipe.GetShaderReflection(rd.ShaderStage.Vertex)
            report["draws"].append({"event": action.eventId, "name": action_name(action, controller),
                "vertices": action.numIndices, "strideBytes": stream.byteStride,
                "colorWriteMask": blend.writeMask,
                "redPixels": red, "greenPixels": green,
                "samplesRgb": samples,
                "bottomSamplesRgb": bottom_samples, "textureRgba": list(bytearray(expected_texel)),
                "vertexShaderSha256": hashlib.sha256(bytes(shader.rawBytes)).hexdigest() if shader else None})
            if red and green:
                screenshot = os.path.splitext(path)[0] + ".stage{}.bmp".format(draw_index // 2)
                write_bmp(screenshot, 64, 64, pixels)
                report["image"] = screenshot
        if len(report["draws"]) != (2 if version == 1 else 6):
            raise RuntimeError("RHI-WEBGL1-PROBE-CAPTURE-DRAW-COUNT")
        for stage in range(1 if version == 1 else 3):
            first, second = report["draws"][stage * 2: stage * 2 + 2]
            if not first["redPixels"] or first["greenPixels"] or not second["greenPixels"]:
                raise RuntimeError("RHI-WEBGL1-PROBE-CAPTURE-UNIFORM-SNAPSHOT")
            if first["redPixels"] != second["redPixels"]:
                raise RuntimeError("RHI-WEBGL1-PROBE-CAPTURE-SCISSOR")
            expected = [(128,0,0), (0,255,0), (0,0,0)] if stage == 0 else [(64,0,0), (0,128,0), (0,0,0)]
            if stage == 2:
                expected = [(0,0,0)] * 3
                if second["bottomSamplesRgb"] != [(64,0,0), (0,128,0)]:
                    raise RuntimeError("RHI-WEBGL1-PROBE-CAPTURE-SUBDATA-NUMERICAL")
            if second["samplesRgb"] != expected:
                raise RuntimeError("RHI-WEBGL1-PROBE-CAPTURE-MATRIX-NUMERICAL")
        errors = [m.description for m in controller.GetDebugMessages() if "Error" in str(m.severity)]
        if errors:
            raise RuntimeError("RHI-WEBGL1-PROBE-CAPTURE-ERROR: " + repr(errors))
        report.update(success=True, replayErrors=errors,
            fixtureRasterRows="nativeRowEqualsGlReadPixelsY" if version != 1 else "centerProbesOnly")
        return report
    finally:
        controller.Shutdown()
        capture.Shutdown()


if __name__ == "__main__":
    path = os.environ["ARP_WEBGL1_CAPTURE_PATH"]
    try:
        report = analyze(path)
        exit_code = 0
    except Exception:
        report = {"success": False, "status": "ProbeOnly", "error": traceback.format_exc()}
        exit_code = 1
    with open(path + ".analysis.json", "w", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2)
    os._exit(exit_code)
