"""qrenderdoc --python: export a real-time GPU capture series to PNG frames."""
import os,sys,glob,traceback
sys.path.insert(0,os.path.join(os.environ.get('ARP_REPO_ROOT','D:/arp'),'tests/tools'))
import renderdoc as rd
from analyze_ui_text_renderdoc import flatten,descriptor_resource
root=os.environ['ARP_SURVIVORS_SERIES'];paths=sorted(glob.glob(os.path.join(root,'frame-*_capture.rdc')))
try:
    assert paths
    with open(os.path.join(root,'replay-progress.log'),'w') as progress:
        for index,path in enumerate(paths):
            capture=rd.OpenCaptureFile();assert capture.OpenFile(path,'',None).code==rd.ResultCode.Succeeded
            status,controller=capture.OpenCapture(rd.ReplayOptions(),None);assert status.code==rd.ResultCode.Succeeded
            try:
                actions=controller.GetRootActions() if hasattr(controller,'GetRootActions') else controller.GetDrawcalls()
                draws=[a for a in flatten(actions) if 'Drawcall' in str(a.flags)];assert draws
                controller.SetFrameEvent(draws[-1].eventId,True)
                target=descriptor_resource(controller.GetPipelineState().GetOutputTargets()[0])
                save=rd.TextureSave();save.resourceId=target;save.destType=rd.FileType.PNG
                result=controller.SaveTexture(save,os.path.join(root,'image-%05d.png'%index));assert result.code==rd.ResultCode.Succeeded
                assert not [m for m in controller.GetDebugMessages() if 'Error' in str(m.severity)]
            finally:controller.Shutdown();capture.Shutdown()
            progress.write('%d/%d exported\n'%(index+1,len(paths)));progress.flush()
    with open(os.path.join(root,'series.yaml'),'w') as report:
        report.write('type: arp.survivors-capture-series\ncontractVersion: 1\nsource: real-time-GPU-frame-captures\nframesPerSecond: 1\nframes: %d\nvalidationErrors: 0\n'%len(paths))
    os._exit(0)
except Exception:
    with open(os.path.join(root,'series-error.log'),'w') as error:error.write(traceback.format_exc())
    os._exit(1)
