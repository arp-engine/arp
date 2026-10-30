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
        environment=[]
        pbr=[]
        sprites=[]
        # Check representative first/middle/last original UI draws, then read
        # the final attachment after every UI draw has executed.
        for action in actions:
            controller.SetFrameEvent(action.eventId,True)
            pipe=controller.GetPipelineState()
            descriptors=list(pipe.GetReadOnlyResources(rd.ShaderStage.Fragment))
            sources={descriptor_resource(d.descriptor) for d in descriptors}
            if action.numIndices==36 and len(descriptors)==5 and not sources&atlas_ids:
                depth=descriptor_resource(pipe.GetDepthTarget())
                formats=[d.descriptor.format.Name() for d in descriptors]
                if not any('SRGB' in f for f in formats):raise RuntimeError('EDITOR-CAPTURE-PBR-SRGB-VIEW')
                pbr.append({'event':action.eventId,'indices':action.numIndices,'textures':len(descriptors),
                    'viewFormats':formats,'depth':str(depth)})
                continue
            if action.numIndices!=6:continue
            if not sources&atlas_ids:
                depth=descriptor_resource(pipe.GetDepthTarget())
                if sources and any((textures[rid].width,textures[rid].height)==(16,8)
                                   for rid in sources if rid in textures):
                    state=controller.GetD3D11PipelineState()
                    blend=state.outputMerger.blendState.blends[0]
                    if not blend.enabled or str(blend.colorBlend.source)!='BlendMultiplier.One':
                        raise RuntimeError('EDITOR-CAPTURE-SPRITE-ALPHA')
                    # ANGLE lowers the two logical streams to one D3D11 input
                    # binding per attribute, and can repack them into a shared
                    # translated buffer. Validate the actual input rates and
                    # bytes instead of treating native slot count as L2 count.
                    inputs=list(pipe.GetVertexInputs())
                    instance_inputs=[v for v in inputs if v.perInstance]
                    corners=[v for v in inputs if not v.perInstance]
                    if (len(inputs)!=7 or len(instance_inputs)!=6 or len(corners)!=1
                        or any(v.instanceRate!=1 or v.format.compCount!=4
                               or v.format.compByteWidth!=4 for v in instance_inputs)
                        or corners[0].format.compCount!=2 or corners[0].format.compByteWidth!=4):
                        raise RuntimeError('EDITOR-CAPTURE-SPRITE-INPUT-LAYOUT')
                    corner=corners[0];buffer=pipe.GetVBuffers()[corner.vertexBuffer]
                    data=controller.GetBufferData(buffer.resourceId,
                        buffer.byteOffset+corner.byteOffset+action.vertexOffset*buffer.byteStride,
                        5*buffer.byteStride+8)
                    values=[struct.unpack_from('<2f',data,i*buffer.byteStride) for i in range(6)]
                    if values!=[(0,0),(1,0),(1,1),(0,0),(1,1),(0,1)]:
                        raise RuntimeError('EDITOR-CAPTURE-SPRITE-CORNER-BYTES: '+repr(values))
                    sprites.append({'event':action.eventId,'vertices':6,'instanceAttributes':6,
                        'vertexAttributes':1,
                        'nativeBindings':sum(v.resourceId!=rd.ResourceId.Null() for v in pipe.GetVBuffers()),
                        'corners':values,
                        'atlasExtent':[16,8],'depth':str(depth)})
                elif depth != rd.ResourceId.Null() and not sources:
                    color=descriptor_resource(pipe.GetOutputTargets()[0])
                    state=controller.GetD3D11PipelineState()
                    blend=state.outputMerger.blendState.blends[0]
                    if not blend.enabled or str(blend.colorBlend.source)!='BlendMultiplier.SrcAlpha':
                        raise RuntimeError('EDITOR-CAPTURE-ENVIRONMENT-BLEND')
                    if 'D24' not in textures[depth].format.Name():
                        raise RuntimeError('EDITOR-CAPTURE-ENVIRONMENT-DEPTH-FORMAT')
                    pixels=rgba8_pixels(texture_bytes(controller,color),textures[color].format.Name())
                    environment.append((action.eventId,color,depth,pixels))
                continue
            draws.append(action)
        if len(draws)<500:raise RuntimeError('EDITOR-CAPTURE-DRAW-COVERAGE')
        environment_report=None
        if phase=='scene':
            if config.get('viewportSmoke') and not pbr:
                raise RuntimeError('EDITOR-CAPTURE-PBR-MISSING')
            if config.get('viewportSmoke') and not sprites:
                raise RuntimeError('EDITOR-CAPTURE-SPRITE-MISSING')
            if len(environment)!=2 or environment[0][1:3]!=environment[1][1:3]:
                raise RuntimeError('EDITOR-CAPTURE-ENVIRONMENT-PASSES')
            a,b=environment
            controller.SetFrameEvent(b[0]-1,True)
            before_grid=rgba8_pixels(texture_bytes(controller,b[1]),textures[b[1]].format.Name())
            changed=sum(x!=y for x,y in zip(before_grid,b[3]))
            if changed<100:raise RuntimeError('EDITOR-CAPTURE-GRID-NO-VISIBLE-PIXELS')
            t=textures[b[1]]
            pixels=[p for y in range(t.height-1,-1,-1) for p in b[3][y*t.width:(y+1)*t.width]]
            image=output/'viewport.bmp'
            write_bmp(str(image),t.width,t.height,pixels)
            environment_report={'events':[a[0],b[0]],'extent':[t.width,t.height],
                'colorFormat':t.format.Name(),'depthFormat':textures[b[2]].format.Name(),
                'gridChangedPixels':changed,'image':str(image)}
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
            'extent':[texture.width,texture.height],'atlasExtent':[2048,3072],'atlasDraws':len(draws),
            'environment':environment_report,'pbr':pbr,'sprites':sprites,'samples':samples,'replayErrors':errors}
    finally:
        controller.Shutdown();cap.Shutdown()

try:
    options=rd.GetDefaultCaptureOptions();options.hookIntoChildren=True
    query='capture=1&capture-workspace=1' if config.get('workspace') else 'capture=1'
    if config.get('viewportSmoke'):query+='&viewport-smoke=1'
    command='--no-sandbox --disable-gpu-sandbox --in-process-gpu --no-first-run --no-default-browser-check --disable-background-networking --disable-renderer-backgrounding --disable-background-timer-throttling --disable-backgrounding-occluded-windows --window-size=1440,960 --user-data-dir="{}" --app="http://127.0.0.1:{}/arp_editor.html?{}"'.format(output/'browser-profile',config['port'],query)
    launch=rd.ExecuteAndInject(config['browser'],config['root'],command,[],str(output/'editor'),options,False)
    report['launch']=str(launch.result)
    target=rd.CreateTargetControl('',launch.ident,'ARP Web Editor',True)
    if not target:raise RuntimeError('EDITOR-CAPTURE-TARGET')
    pid=target.GetPID()
    captures=[];phase=0;armed=False;deadline=time.monotonic()+120
    while target.Connected() and time.monotonic()<deadline:
        if (output/'error').exists():
            raise RuntimeError('EDITOR-CAPTURE-BROWSER: '+(output/'error').read_text())
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
