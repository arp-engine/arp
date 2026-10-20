"""Run in qrenderdoc --python. Paths are supplied through environment variables."""
import json
import hashlib
import os
import struct
import traceback
import renderdoc as rd

root = os.environ["ARP_BINDLESS_CAPTURE_OUTPUT"]
report = {"scope": "backend-private-native-shaders-not-WOCC"}
cap = None
controller = None
try:
    cap = rd.OpenCaptureFile()
    status = cap.OpenFile(os.environ["ARP_BINDLESS_CAPTURE_PATH"], "", None)
    report["openFile"] = str(status)
    if status.code != rd.ResultCode.Succeeded:
        raise RuntimeError(str(status))
    status, controller = cap.OpenCapture(rd.ReplayOptions(), None)
    report["replay"] = str(status)
    if status.code != rd.ResultCode.Succeeded:
        raise RuntimeError(str(status))
    actions = []
    def walk(nodes):
        for action in nodes:
            actions.append(action)
            walk(action.children)
    walk(controller.GetRootActions())
    draws = [a for a in actions if a.flags & rd.ActionFlags.Drawcall]
    dispatches = [a for a in actions if a.flags & rd.ActionFlags.Dispatch]
    report["draws"] = len(draws)
    report["dispatches"] = len(dispatches)
    assert len(draws) == 2 and len(dispatches) == 1
    controller.SetFrameEvent(dispatches[0].eventId, True)
    pipe = controller.GetPipelineState()
    def check_shader(stage, entry):
        reflected = pipe.GetShaderReflection(stage)
        assert reflected.entryPoint == entry
        captured = hashlib.sha256(bytes(reflected.rawBytes)).hexdigest()
        with open(os.path.join(root, "BindlessFixture.native." + entry + ".spv"), "rb") as shader:
            expected_hash = hashlib.sha256(shader.read()).hexdigest()
        assert captured == expected_hash
        return {"entry": entry, "sha256": captured}
    report["computeShader"] = check_shader(rd.ShaderStage.Compute, "computeMain")
    controller.SetFrameEvent(draws[0].eventId, True)
    pipe = controller.GetPipelineState()
    report["fragmentShader"] = check_shader(rd.ShaderStage.Fragment, "fragmentMain")
    report["vertexShader"] = check_shader(rd.ShaderStage.Vertex, "vertexMain")
    target = pipe.GetOutputTargets()[0].resource
    data = bytes(controller.GetTextureData(target, rd.Subresource(0, 0, 0)))
    report["pixels"] = [list(data[i:i+4]) for i in range(0, 16, 4)]
    expected = [[27,53,80,255],[87,53,80,255],[38,72,79,255],[96,42,60,255]]
    assert report["pixels"] == expected
    save = rd.TextureSave()
    save.resourceId = target
    save.destType = rd.FileType.PNG
    controller.SaveTexture(save, os.path.join(root, "indexed-sampling.png"))
    controller.SetFrameEvent(draws[1].eventId, True)
    pipe = controller.GetPipelineState()
    report["depthVertexShader"] = check_shader(rd.ShaderStage.Vertex, "vertexMain")
    assert pipe.GetShaderReflection(rd.ShaderStage.Fragment) is None
    depth_target = pipe.GetDepthTarget().resource
    depth_data = bytes(controller.GetTextureData(depth_target, rd.Subresource(0, 0, 0)))
    report["depthPixels"] = list(struct.unpack_from("<4f", depth_data))
    assert all(abs(value) < 0.00001 for value in report["depthPixels"])
    report["validationMessages"] = [m.description for m in controller.GetDebugMessages()]
    report["success"] = True
except Exception:
    report["error"] = traceback.format_exc()
    report["success"] = False
finally:
    with open(os.path.join(root, "capture-report.json"), "w") as output:
        json.dump(report, output, indent=2)
    if controller:
        controller.Shutdown()
    if cap:
        cap.Shutdown()
    os._exit(0 if report.get("success") else 1)
