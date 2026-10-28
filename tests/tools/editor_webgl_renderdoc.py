"""qrenderdoc script: capture real browser UI, then replay its actual draws."""
import json
import os
from pathlib import Path
import struct
import sys
import time
import traceback
import renderdoc as rd

output=Path(os.environ['ARP_EDITOR_WEB_EVIDENCE'])
config=json.loads((output/'server.json').read_text())
sys.path.insert(0,str(Path(config['root'])/'tests/tools'))
from analyze_ui_text_renderdoc import flatten,descriptor_resource,texture_bytes,rgba8_pixels,write_bmp
report={'success':False,'gpuCompletionClaimed':False,'productionReady':False,'captures':[]}
report['bundleHashes']=config.get('bundleHashes',{})
target=None
pid=0

def replay(path,phase):
    cap=rd.OpenCaptureFile()
    if cap.OpenFile(str(path),'',None).code!=rd.ResultCode.Succeeded:raise RuntimeError('EDITOR-CAPTURE-OPEN')
    status,controller=cap.OpenCapture(rd.ReplayOptions(),None)
    if status.code!=rd.ResultCode.Succeeded:raise RuntimeError('EDITOR-CAPTURE-REPLAY')
    try:
        textures={t.resourceId:t for t in controller.GetTextures()}
        atlas_ids={rid for rid,t in textures.items() if (t.width,t.height)==(2048,3072)}
        if not atlas_ids:raise RuntimeError('EDITOR-CAPTURE-ATLAS-MISSING')
        actions=[a for a in flatten(controller.GetRootActions()) if a.flags&rd.ActionFlags.Drawcall]
        draws=[]
        # Check representative first/middle/last original UI draws, then read
        # the final attachment after every UI draw has executed.
        for action in actions:
            if action.numIndices!=6:continue
            controller.SetFrameEvent(action.eventId,True)
            pipe=controller.GetPipelineState()
            sources={descriptor_resource(d.descriptor) for d in pipe.GetReadOnlyResources(rd.ShaderStage.Fragment)}
            if not sources&atlas_ids:continue
            draws.append(action)
        if len(draws)<500:raise RuntimeError('EDITOR-CAPTURE-DRAW-COVERAGE')
        samples=[]
        for action in (draws[0],draws[len(draws)//2],draws[-1]):
            controller.SetFrameEvent(action.eventId,True)
            pipe=controller.GetPipelineState()
            state=controller.GetD3D11PipelineState()
            blend=state.outputMerger.blendState.blends[0]
            if not blend.enabled or str(blend.colorBlend.source)!='BlendMultiplier.One' or str(blend.colorBlend.destination)!='BlendMultiplier.InvSrcAlpha':
                raise RuntimeError('EDITOR-CAPTURE-BLEND')
            sc=state.rasterizer.scissors[0]
            if not state.rasterizer.state.scissorEnable or sc.width<=0 or sc.height<=0:raise RuntimeError('EDITOR-CAPTURE-CLIP')
            samples.append({'event':action.eventId,'scissor':[sc.x,sc.y,sc.width,sc.height],
                'blend':[str(blend.colorBlend.source),str(blend.colorBlend.destination)],'vertices':action.numIndices})
        controller.SetFrameEvent(draws[-1].eventId,True)
        pipe=controller.GetPipelineState()
        rid=descriptor_resource(pipe.GetOutputTargets()[0])
        texture=textures[rid]
        pixels=rgba8_pixels(texture_bytes(controller,rid),texture.format.Name())
        image=output/(phase+'.bmp')
        # Original WebGlShaderCoordinates/1: the native ANGLE attachment is
        # reversed vertically before the browser compositor consumes it.
        pixels=[p for y in range(texture.height-1,-1,-1) for p in pixels[y*texture.width:(y+1)*texture.width]]
        write_bmp(str(image),texture.width,texture.height,pixels)
        errors=[str(m.description) for m in controller.GetDebugMessages()
            if m.severity in (rd.MessageSeverity.High,rd.MessageSeverity.Medium)]
        if errors:raise RuntimeError('EDITOR-CAPTURE-REPLAY-DIAGNOSTIC: '+repr(errors))
        return {'phase':phase,'capture':str(path),'image':str(image),'nativeApi':str(controller.GetAPIProperties().pipelineType),
            'extent':[texture.width,texture.height],'atlasExtent':[2048,3072],'atlasDraws':len(draws),'samples':samples,'replayErrors':errors}
    finally:
        controller.Shutdown();cap.Shutdown()

try:
    options=rd.GetDefaultCaptureOptions();options.hookIntoChildren=True
    command='--no-sandbox --disable-gpu-sandbox --in-process-gpu --no-first-run --no-default-browser-check --disable-background-networking --disable-renderer-backgrounding --disable-background-timer-throttling --disable-backgrounding-occluded-windows --window-size=1440,960 --user-data-dir="{}" --app="http://127.0.0.1:{}/arp_editor.html?capture=1"'.format(output/'browser-profile',config['port'])
    launch=rd.ExecuteAndInject(config['browser'],config['root'],command,[],str(output/'editor'),options,False)
    report['launch']=str(launch.result)
    target=rd.CreateTargetControl('',launch.ident,'ARP Web Editor',True)
    if not target:raise RuntimeError('EDITOR-CAPTURE-TARGET')
    pid=target.GetPID()
    captures=[];phase=0;armed=False;deadline=time.monotonic()+120
    while target.Connected() and time.monotonic()<deadline:
        message=target.ReceiveMessage(None)
        if message.type==rd.TargetControlMessageType.NewCapture:
            captures.append(Path(str(message.newCapture.path)))
            if phase==0:
                (output/'scene-captured').write_text('script');phase=1;armed=False
            else:break
        if not armed and (output/('ready' if phase==0 else 'script-ready')).exists():
            target.TriggerCapture(1);armed=True
    if len(captures)!=2:raise RuntimeError('EDITOR-CAPTURE-COUNT: '+str(captures))
    target.Shutdown();target=None
    for path,name in zip(captures,('scene','script')):report['captures'].append(replay(path,name))
    report['success']=True
except Exception:report['error']=traceback.format_exc()
finally:
    if target:target.Shutdown()
    if pid:os.system('taskkill /PID {} /T /F >NUL 2>&1'.format(pid))
    (output/'renderdoc-report.json').write_text(json.dumps(report,indent=2))
os._exit(0)
