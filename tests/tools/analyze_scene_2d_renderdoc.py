"""Replay sprite smoke: actual instance attributes, PNGs, blend and final pixels."""
import os
import sys
sys.path.insert(0, os.path.join(os.environ.get("ARP_REPO_ROOT","D:/arp"),"tests/tools"))
import renderdoc as rd
from analyze_ui_text_renderdoc import flatten, action_name, descriptor_resource, texture_bytes, rgba8_pixels, write_bmp

def main():
    path = os.environ["ARP_RENDERDOC_ANALYZE_CAPTURE"]
    mode=os.environ.get("ARP_2D_CAPTURE_MODE","sprites")
    expected={"sprites":[2,1,2],"animation":[5],"physics":[7],"tiles":[4,4,4]}[mode]
    capture = rd.OpenCaptureFile()
    assert capture.OpenFile(path, "", None).code == rd.ResultCode.Succeeded
    status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
    assert status.code == rd.ResultCode.Succeeded and controller
    try:
        roots = controller.GetRootActions() if hasattr(controller,"GetRootActions") else controller.GetDrawcalls()
        draws = [a for a in flatten(roots) if "Drawcall" in str(a.flags)]
        assert len(draws) == len(expected), [(action_name(a,controller),str(a.flags)) for a in draws]
        assert [a.numInstances for a in draws] == expected
        assert all(a.numIndices == 6 for a in draws)
        textures = {t.resourceId:t for t in controller.GetTextures()}
        atlases = [t for t in textures.values() if t.width==16 and t.height==8]
        assert len(atlases)==(2 if mode in ("sprites","tiles") else 1)
        for a in draws:
            controller.SetFrameEvent(a.eventId,True)
            pipe = controller.GetPipelineState()
            blend = pipe.GetColorBlends()[0]
            assert blend.enabled and blend.writeMask==15
            assert len(pipe.GetVBuffers())>=1
            stream=pipe.GetVBuffers()[0]
            assert stream.byteStride==96
            raw=bytes(controller.GetBufferData(stream.resourceId,stream.byteOffset,96*a.numInstances))
            assert len(raw)==96*a.numInstances
            shader=pipe.GetShaderReflection(rd.ShaderStage.Fragment)
            assert shader and shader.entryPoint=="fragmentMain"
        output=descriptor_resource(controller.GetPipelineState().GetOutputTargets()[0])
        image=textures[output]
        pixels=rgba8_pixels(texture_bytes(controller,output),image.format.Name())
        red=sum(r>180 and g<150 and b<130 for r,g,b,a in pixels)
        green=sum(g>150 and r<100 and b<130 for r,g,b,a in pixels)
        blue=sum(b>120 and r<100 and g<140 for r,g,b,a in pixels)
        purple=sum(r>100 and g<100 and b>100 for r,g,b,a in pixels)
        teal=sum(g>100 and b>100 and r<80 for r,g,b,a in pixels)
        screenshot=os.path.splitext(path)[0]+".bmp"
        write_bmp(screenshot,image.width,image.height,pixels)
        if mode=="tiles":
            assert purple>1000 and teal>1000,(purple,teal)
        elif mode=="sprites":
            assert red>1000 and green>1000 and blue>1000,(red,green,blue)
        elif mode=="animation":
            assert blue>1000,blue
        else:
            assert red>1000 and green>1000,(red,green)
        errors=[m for m in controller.GetDebugMessages() if "Error" in str(m.severity)]
        assert not errors,[m.description for m in errors]
        report=os.path.splitext(path)[0]+".yaml"
        with open(report,"w",encoding="utf-8") as f:
            f.write(f"type: arp.scene2d-capture-evidence\ncontractVersion: 1\nstatus: Backend Validated\nmode: {mode}\ndraws: {len(draws)}\ninstances: {sum(expected)}\natlases: {len(atlases)}\nstride: 96\nredPixels: {red}\ngreenPixels: {green}\nbluePixels: {blue}\nvalidationErrors: 0\nscreenshot: {screenshot}\n")
    finally:
        controller.Shutdown()
        capture.Shutdown()

try:
    main()
    os._exit(0)
except Exception:
    import traceback
    with open(os.environ["ARP_RENDERDOC_ANALYZE_CAPTURE"]+".error.txt","w") as error:
        error.write(traceback.format_exc())
    os._exit(1)
