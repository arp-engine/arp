"""Replay the shipped Survivors draw stream and export its final color target."""
import os,sys,math,struct
sys.path.insert(0,os.path.join(os.environ.get('ARP_REPO_ROOT','D:/arp'),'tests/tools'))
import renderdoc as rd
from analyze_ui_text_renderdoc import flatten,action_name,descriptor_resource,texture_bytes,rgba8_pixels,write_bmp

def main():
    path=os.environ['ARP_RENDERDOC_ANALYZE_CAPTURE']
    capture=rd.OpenCaptureFile()
    assert capture.OpenFile(path,'',None).code==rd.ResultCode.Succeeded
    status,controller=capture.OpenCapture(rd.ReplayOptions(),None)
    assert status.code==rd.ResultCode.Succeeded and controller
    try:
        roots=controller.GetRootActions() if hasattr(controller,'GetRootActions') else controller.GetDrawcalls()
        draws=[a for a in flatten(roots) if 'Drawcall' in str(a.flags)]
        assert draws and all(a.numIndices==6 for a in draws)
        textures={t.resourceId:t for t in controller.GetTextures()}
        atlases=[t for t in textures.values() if t.width==512 and t.height==256]
        assert len(atlases)==1
        assert len(set(texture_bytes(controller,atlases[0].resourceId)))>32
        instances=0;glyphs=0
        for a in draws:
            controller.SetFrameEvent(a.eventId,True);pipe=controller.GetPipelineState()
            blend=pipe.GetColorBlends()[0];assert blend.enabled and blend.writeMask==15
            vb=pipe.GetVBuffers()[0];assert vb.byteStride==96
            raw=bytes(controller.GetBufferData(vb.resourceId,vb.byteOffset,96*a.numInstances))
            assert len(raw)==96*a.numInstances
            for j in range(a.numInstances):
                fields=struct.unpack_from('<24f',raw,j*96)
                assert all(math.isfinite(x) for x in fields)
                u,v,w,h=fields[12:16]
                assert 0<=u<=1 and 0<=v<=1 and 0<w<=1 and 0<h<=1 and u+w<=1.000001 and v+h<=1.000001
                assert 0<=fields[19]<=1
                if abs(w-6/512.0)<.00001 and abs(h-8/256.0)<.00001:glyphs+=1
            instances+=a.numInstances
            shader=pipe.GetShaderReflection(rd.ShaderStage.Fragment)
            assert shader and shader.entryPoint=='fragmentMain'
        assert instances>100 and glyphs>60,(instances,glyphs)
        output=descriptor_resource(controller.GetPipelineState().GetOutputTargets()[0]);target=textures[output]
        pixels=rgba8_pixels(texture_bytes(controller,output),target.format.Name())
        colored=sum(max(r,g,b)-min(r,g,b)>15 for r,g,b,a in pixels)
        bright=sum(max(r,g,b)>150 for r,g,b,a in pixels)
        assert colored>5000 and bright>1000,(colored,bright)
        bmp=os.path.splitext(path)[0]+'.bmp';write_bmp(bmp,target.width,target.height,pixels)
        errors=[m for m in controller.GetDebugMessages() if 'Error' in str(m.severity)]
        assert not errors,[m.description for m in errors]
        counters=controller.FetchCounters([rd.GPUCounter.EventGPUDuration])
        gpuMs=sum(c.value.d for c in counters if c.counter==rd.GPUCounter.EventGPUDuration)*1000
        with open(os.path.splitext(path)[0]+'.yaml','w',encoding='utf-8') as f:
            f.write('type: arp.survivors-renderdoc-evidence\ncontractVersion: 1\nbackend: Vulkan\nwidth: %d\nheight: %d\ndraws: %d\ninstances: %d\nglyphs: %d\natlases: 1\nstride: 96\nvalidationErrors: 0\ngpuReplayDrawMs: %.6f\nscreenshot: %s\n' %(target.width,target.height,len(draws),instances,glyphs,gpuMs,bmp))
    finally:
        controller.Shutdown();capture.Shutdown()
try:
    main();os._exit(0)
except Exception:
    import traceback
    with open(os.environ['ARP_RENDERDOC_ANALYZE_CAPTURE']+'.error.txt','w') as f:f.write(traceback.format_exc())
    os._exit(1)
