"""RenderDoc evidence for the original BasicForward -> fullscreen -> GUI graph."""
import json
import hashlib
import math
import os
from pathlib import Path
import struct
import sys
import traceback
import renderdoc as rd

sys.path.insert(0, str(Path(os.environ['ARP_REPO_ROOT']) / 'tests/tools'))
from analyze_ui_text_renderdoc import flatten, descriptor_resource, texture_bytes, rgba8_pixels, write_bmp
from webgl_product_native_state import WebGlProductNativeState

OUTPUT = Path(os.environ['ARP_WEBGL_PROGRAM_CAPTURE'])
SOURCE = json.loads((OUTPUT / 'gui-inputs.json').read_text())
CAMERA = (.5,.125,0,.125,.0625,-.5,0,.0625,0,0,.5,.25,0,0,0,1)
CORNERS = ((0,0),(1,0),(1,1),(0,0),(1,1),(0,1))


def expected_pixel(ordinal, x, top, w, h):
    if ordinal == 4:
        value = 64 if x < w/2 else 191
        return [value,value,value,255]
    if ordinal == 8 and x >= w-16 and top >= h-16:
        value = 64 if x < w-8 else 191
        return [value,value,value,255]
    p = [0,0,0,255]
    if ordinal >= 0 and x < w/2:
        texel = SOURCE['image']['rgba'][4*16+2]
        p = [math.floor(texel[c]*.75*(.8 if c == 0 else 1)+.5) for c in range(3)]+[255]
    if ordinal >= 1 and x >= w/2:
        texel = SOURCE['image']['rgba'][4*16+12]
        p = [math.floor(texel[c]*.75*(.8 if c == 1 else 1)+.5) for c in range(3)]+[255]
    if ordinal >= 2 and top >= h/2:
        p[:3] = [math.floor((.375*255 if c == 2 else 0) + p[c]*.5 + .5) for c in range(3)]
    if ordinal >= 3 and x >= w/2:
        p[:3] = [math.floor(.1875*255+p[c]*.75+.5) for c in range(3)]
    if ordinal >= 6 and x >= w-16 and top < 8:
        texel = SOURCE['image']['rgba'][top*16+x-(w-16)]
        alpha = texel[3]/255
        p[:3] = [math.floor(math.floor(texel[c]*alpha+.5)+p[c]*(1-alpha)+.5) for c in range(3)]
    if ordinal >= 7 and 4 <= x < 10 and 4 <= top < 12:
        if SOURCE['glyph']['rgba'][(top-4)*8+x-4][3]:
            p = [255,255,255,255]
    return p


def analyze(path):
    cap = rd.OpenCaptureFile()
    if cap.OpenFile(str(path), '', None).code != rd.ResultCode.Succeeded:
        raise RuntimeError('BASIC-CAPTURE-OPEN')
    status, controller = cap.OpenCapture(rd.ReplayOptions(), None)
    if status.code != rd.ResultCode.Succeeded or not controller:
        cap.Shutdown()
        raise RuntimeError('BASIC-CAPTURE-REPLAY')
    report = dict(capture=str(path), nativeApi=str(controller.GetAPIProperties().pipelineType), draws=[])
    try:
        textures = {t.resourceId:t for t in controller.GetTextures()}
        ordinal = 0
        for action in flatten(controller.GetRootActions()):
            if not action.flags & rd.ActionFlags.Drawcall or action.numIndices != 6:
                continue
            controller.SetFrameEvent(action.eventId, True)
            pipe = controller.GetPipelineState()
            attrs = [a for a in pipe.GetVertexInputs() if a.used]
            if len(attrs) not in (1,3):
                continue
            if ordinal < 4 and len(attrs) != 3 or ordinal >= 4 and len(attrs) != 1:
                raise RuntimeError('BASIC-CAPTURE-DRAW-ORDER')
            if ordinal < 4:
                index = pipe.GetIBuffer()
                if index.byteStride != 2:
                    raise RuntimeError('BASIC-CAPTURE-UINT16')
                values = struct.unpack('<6H', bytes(controller.GetBufferData(index.resourceId,
                    index.byteOffset + action.indexOffset*2,12)))
                if len(set(values)) != 6:
                    raise RuntimeError('BASIC-CAPTURE-INDEX-VALUES: '+repr(values))
                if sorted(a.format.compCount for a in attrs) != [2,3,3]:
                    raise RuntimeError('BASIC-CAPTURE-VERTEX-INTERFACE')
                by_name = {a.name:a for a in attrs}
                if set(by_name) != {'TEXCOORD0','TEXCOORD1','TEXCOORD2'}:
                    raise RuntimeError('BASIC-CAPTURE-NATIVE-ATTRIBUTE-MAPPING')
                for vertex, corner in zip(values,CORNERS):
                    components = []
                    for name in ('TEXCOORD0','TEXCOORD1','TEXCOORD2'):
                        attribute = by_name[name]
                        stream = pipe.GetVBuffers()[attribute.vertexBuffer]
                        size = attribute.format.compCount*4
                        raw = bytes(controller.GetBufferData(stream.resourceId,
                            stream.byteOffset+(vertex+action.baseVertex)*stream.byteStride+attribute.byteOffset,size))
                        components.extend(struct.unpack('<'+str(attribute.format.compCount)+'f',raw))
                    px,py,pz,nx,ny,nz,u,v = components
                    world = (px,py,pz,1)
                    actual = [sum(CAMERA[r*4+c]*world[c] for c in range(4)) for r in range(4)]
                    x = corner[0]*.5 if ordinal == 0 else corner[0]
                    y = .5+corner[1]*.5 if ordinal == 2 else corner[1]
                    expected = (x*2-1,y*2-1,(.25,.75,.125,.5)[ordinal],1)
                    if any(abs(a-b)>1e-5 for a,b in zip(actual,expected)) or (nx,ny,nz)!=(0,0,1):
                        raise RuntimeError('BASIC-CAPTURE-WORLD-MATRIX')
                    if (u,v) != ((.125 if ordinal == 0 else .75),.5):
                        raise RuntimeError('BASIC-CAPTURE-MESH-UV')
                z = descriptor_resource(pipe.GetDepthTarget())
                if z not in textures:
                    raise RuntimeError('BASIC-CAPTURE-DEPTH-TARGET')
                fmt = textures[z].format.Name()
                if fmt != 'D24S8_TYPELESS':
                    raise RuntimeError('BASIC-CAPTURE-DEPTH-FORMAT: '+fmt)
                depths = [(n & 0xffffff)/16777215 for (n,) in struct.iter_unpack('<I',texture_bytes(controller,z))]
                for i,d in enumerate(depths):
                    expected = .25 if i%64 < 32 else (.75 if ordinal >= 1 else 1)
                    if abs(d-expected)>2/16777215:
                        raise RuntimeError('BASIC-CAPTURE-DEPTH-VALUES: '+repr((ordinal,i,d,expected)))
                if pipe.GetColorBlends()[0].enabled != (ordinal >= 2):
                    raise RuntimeError('BASIC-CAPTURE-BLEND-STATE')
            if ordinal <= 4:
                post = controller.GetPostVSData(0,0,rd.MeshDataStage.VSOut)
                if post.numIndices != 6 or post.vertexByteStride < 16 or post.vertexResourceId == rd.ResourceId.Null():
                    raise RuntimeError('BASIC-CAPTURE-POSTVS-UNAVAILABLE: '+str(post.status))
                post_indices = range(6)
                if post.indexResourceId != rd.ResourceId.Null():
                    formats = {2:'H',4:'I'}
                    if post.indexByteStride not in formats:
                        raise RuntimeError('BASIC-CAPTURE-POSTVS-INDEX-WIDTH')
                    post_indices = struct.unpack('<6'+formats[post.indexByteStride],bytes(controller.GetBufferData(
                        post.indexResourceId,post.indexByteOffset,6*post.indexByteStride)))
                for index,corner in zip(post_indices,CORNERS):
                    actual = struct.unpack('<4f',bytes(controller.GetBufferData(post.vertexResourceId,
                        post.vertexByteOffset+index*post.vertexByteStride,16)))
                    x = corner[0]*.5 if ordinal == 0 else corner[0]
                    y = .5+corner[1]*.5 if ordinal == 2 else corner[1]
                    expected = (x*2-1,y*2-1,(.25,.75,.125,.5,.75)[ordinal],1)
                    if any(abs(a-b)>1e-5 for a,b in zip(actual,expected)):
                        raise RuntimeError('BASIC-CAPTURE-POSTVS-MATRIX: '+repr((ordinal,actual,expected)))
            sampled = {descriptor_resource(d.descriptor) for d in pipe.GetReadOnlyResources(rd.ShaderStage.Fragment)}
            sampled.discard(rd.ResourceId.Null())
            if len(sampled) != 1:
                raise RuntimeError('BASIC-CAPTURE-TEXTURE-BINDING')
            source_id = next(iter(sampled))
            source = textures[source_id]
            source_pixels = rgba8_pixels(texture_bytes(controller,source_id),source.format.Name())
            if ordinal not in (4,5,8):
                entry = SOURCE['glyph' if ordinal == 7 else 'image']
                if (source.width,source.height) != tuple(entry['extent']) or [list(p) for p in source_pixels] != entry['rgba']:
                    raise RuntimeError('BASIC-CAPTURE-ORIGINAL-SOURCE')
            elif ordinal == 4:
                if source.format.Name() != 'D24S8_TYPELESS':
                    raise RuntimeError('BASIC-RECEIVER-DEPTH-FORMAT')
                depths = [(n & 0xffffff)/16777215 for (n,) in struct.iter_unpack('<I',texture_bytes(controller,source_id))]
                if len(depths) != 4096 or any(abs(d-(.25 if i%64<32 else .75))>2/16777215 for i,d in enumerate(depths)):
                    raise RuntimeError('BASIC-RECEIVER-DEPTH-VALUES')
                vertex = attrs[0]
                if vertex.format.compCount != 3:
                    raise RuntimeError('BASIC-RECEIVER-VERTEX-INTERFACE')
                stream = pipe.GetVBuffers()[vertex.vertexBuffer]
                for i,corner in enumerate(CORNERS):
                    world = struct.unpack('<3f',bytes(controller.GetBufferData(stream.resourceId,
                        stream.byteOffset+i*stream.byteStride+vertex.byteOffset,12)))+(1,)
                    actual = [sum(CAMERA[r*4+c]*world[c] for c in range(4)) for r in range(4)]
                    expected = (corner[0]*2-1,corner[1]*2-1,.75,1)
                    if any(abs(a-b)>1e-5 for a,b in zip(actual,expected)):
                        raise RuntimeError('BASIC-RECEIVER-SHARED-CAMERA: '+repr((i,world,actual,expected,stream.byteStride)))
            elif ordinal == 8:
                if (source.width,source.height) != (64,64) or any(any(abs(p[c]-(64 if i%64<32 else 191))>1 for c in range(3)) or p[3]!=255 for i,p in enumerate(source_pixels)):
                    raise RuntimeError('BASIC-RECEIVER-GUI-SOURCE')
            else:
                if (source.width,source.height)!=(64,64):
                    raise RuntimeError('BASIC-CAPTURE-OFFSCREEN-EXTENT')
                for y in range(64):
                    for x in range(64):
                        if any(abs(source_pixels[y*64+x][c]-expected_pixel(3,x,63-y,64,64)[c])>2 for c in range(4)):
                            raise RuntimeError('BASIC-CAPTURE-OFFSCREEN-CONTENT')
            targets = [descriptor_resource(d) for d in pipe.GetOutputTargets()]
            targets = [t for t in targets if t in textures]
            if len(targets)!=1 or targets[0]==source_id:
                raise RuntimeError('BASIC-CAPTURE-OUTPUT')
            target = textures[targets[0]]
            w,h = target.width,target.height
            native_state = WebGlProductNativeState.capture(controller, pipe, 'basic-forward', ordinal, [w,h])
            overlay_post = []
            if ordinal >= 5:
                left, top, width, height = ((0,0,w,h), (w-16,0,16,8), (4,4,6,8), (w-16,h-16,16,16))[ordinal-5]
                overlay_post = WebGlProductNativeState.post_vs(controller,
                    [[(left+x*width)/w*2-1, (top+y*height)/h*2-1, 0, 1] for x,y in CORNERS])
            pixels = rgba8_pixels(texture_bytes(controller,targets[0]),target.format.Name())
            for y in range(h):
                for x in range(w):
                    expect = expected_pixel(ordinal,x,h-1-y,w,h)
                    actual = pixels[y*w+x]
                    if any(abs(actual[c]-expect[c])>2 for c in range(4)):
                        raise RuntimeError('BASIC-CAPTURE-PIXEL: '+repr((ordinal,x,h-1-y,actual,expect)))
            image = path.with_name(path.stem+'-basic-'+str(action.eventId)+'.bmp')
            write_bmp(str(image),w,h,[p for y in range(h-1,-1,-1) for p in pixels[y*w:(y+1)*w]])
            report['draws'].append(dict(event=action.eventId,ordinal=ordinal,extent=[w,h],source=str(source_id),
                nativeState=native_state, outputTarget=str(targets[0]), outputFormat=target.format.Name(),
                depthTarget=str(descriptor_resource(pipe.GetDepthTarget())),
                outputSha256=hashlib.sha256(bytes(c for p in pixels for c in p)).hexdigest(),
                verifiedDepthPixels=4096 if ordinal<4 else 0,verifiedOutputPixels=w*h,
                verifiedSourcePixels=len(source_pixels),verifiedVertices=6 if ordinal<=4 else 0,
                verifiedPostVsVertices=6,overlayPostVsPositions=overlay_post,image=str(image)))
            ordinal = (ordinal+1)%9
        if ordinal:
            raise RuntimeError('BASIC-CAPTURE-INCOMPLETE-GROUP')
        errors = [m.description for m in controller.GetDebugMessages() if 'Error' in str(m.severity)]
        if errors:
            raise RuntimeError('BASIC-CAPTURE-REPLAY-ERRORS: '+repr(errors))
        report['replayErrors']=errors
        return report
    finally:
        controller.Shutdown()
        cap.Shutdown()


result = dict(success=False)
try:
    runtime = json.loads((OUTPUT/'runtime.json').read_text())
    if not runtime.get('success') or runtime.get('frames') != 6 or runtime.get('type') != 'BasicForwardProductSmoke':
        raise RuntimeError('BASIC-CAPTURE-RUNTIME')
    frames = [analyze(p) for p in sorted(OUTPUT.glob('program_frame*.rdc'))]
    result['frames']=frames
    draws = [d for frame in frames for d in frame['draws']]
    if {tuple(d['extent']) for d in draws}!={(64,64),(48,32)} or not draws:
        raise RuntimeError('BASIC-CAPTURE-RESIZE-COVERAGE')
    result.update(success=True,verifiedDraws=len(draws),gpuCompletionClaimed=False,productionReady=False)
except Exception:
    result['error']=traceback.format_exc()
(OUTPUT/'replay.json').write_text(json.dumps(result,indent=2))
os._exit(0 if result['success'] else 1)
