"""Tool-only ANGLE/D3D11 product audit; never issues RHI execution evidence."""
import hashlib
import math
import struct


class WebGlProductNativeState:
    @staticmethod
    def validate_post_vs(actual, expected):
        if (len(actual) != len(expected) or not actual or any(len(p) != 4 for p in actual)
                or any(not math.isfinite(a) or abs(a-b) > 1e-5
                       for p, q in zip(actual, expected) for a, b in zip(p, q))):
            raise RuntimeError('WEBGL-NATIVE-POSTVS-NUMERICAL')
        return actual

    @classmethod
    def post_vs(cls, controller, expected):
        import renderdoc as rd
        post = controller.GetPostVSData(0, 0, rd.MeshDataStage.VSOut)
        count = len(expected)
        if (post.numIndices != count or post.vertexByteStride < 16
                or post.vertexResourceId == rd.ResourceId.Null()):
            raise RuntimeError('WEBGL-NATIVE-POSTVS-UNAVAILABLE: '+str(post.status))
        indices = range(count)
        if post.indexResourceId != rd.ResourceId.Null():
            code = {2: 'H', 4: 'I'}.get(post.indexByteStride)
            if not code:
                raise RuntimeError('WEBGL-NATIVE-POSTVS-INDEX-WIDTH')
            indices = struct.unpack('<'+str(count)+code, bytes(controller.GetBufferData(
                post.indexResourceId, post.indexByteOffset, count*post.indexByteStride)))
        actual = [list(struct.unpack('<4f', bytes(controller.GetBufferData(post.vertexResourceId,
            post.vertexByteOffset+i*post.vertexByteStride, 16)))) for i in indices]
        return cls.validate_post_vs(actual, expected)

    @staticmethod
    def validate(state, profile, ordinal, extent):
        if profile not in ('scene2d', 'gui', 'basic-forward'):
            raise RuntimeError('WEBGL-NATIVE-PROFILE')
        count = {'scene2d': 3, 'gui': 4, 'basic-forward': 9}[profile]
        if not 0 <= ordinal < count or tuple(extent) not in ((64, 64), (48, 32)):
            raise RuntimeError('WEBGL-NATIVE-DRAW-IDENTITY')
        basic = profile == 'basic-forward'
        mesh = basic and ordinal < 4
        receiver = basic and ordinal == 4
        canvas = not basic or ordinal >= 5
        raster, depth, blend = (state[n] for n in ('raster', 'depth', 'blend'))
        if (state['nativeApi'] != 'GraphicsAPI.D3D11'
                or not raster['frontCCW'] or not raster['scissorEnabled']
                or raster['cull'] != ('CullMode.Back' if mesh and ordinal % 2 == 0 else 'CullMode.NoCull')):
            raise RuntimeError('WEBGL-NATIVE-RASTER')
        # D3D11 depth disabled means no depth writes, regardless of the inactive mask.
        # Preserve both raw fields; never compare an inactive compare op with an L2 intent.
        if (depth['enabled'] != mesh or depth['stencilEnabled']
                or (mesh and (depth['writeMask'] != (ordinal < 2)
                              or depth['compare'] != 'CompareFunction.LessEqual'))):
            raise RuntimeError('WEBGL-NATIVE-DEPTH')
        enabled = not basic or ordinal >= 5 or 2 <= ordinal < 4
        # The alpha:false canvas has no writable alpha. Offscreen RGBA8 has all four.
        if (blend['enabled'] != enabled or blend['logicEnabled']
                or blend['writeMask'] != (7 if canvas else 15)):
            raise RuntimeError('WEBGL-NATIVE-BLEND')
        if enabled and any(blend[n] != ['BlendMultiplier.One', 'BlendMultiplier.InvSrcAlpha',
                                       'BlendOperation.Add'] for n in ('color', 'alpha')):
            raise RuntimeError('WEBGL-NATIVE-BLEND-FACTORS')
        w, h = extent
        if state['viewport'] != [0, 0, w, h, 0, 1]:
            raise RuntimeError('WEBGL-NATIVE-VIEWPORT')
        scissor = ([4, 4, w-8, h-8] if profile == 'scene2d' else
                   [4, h//2, w//2-4, h//2-4] if profile == 'gui' and ordinal == 1 else
                   [0, 0, w, h])
        if state['scissor'] != scissor:
            raise RuntimeError('WEBGL-NATIVE-SCISSOR')
        shaders = state['shaders']
        if [s['stage'] for s in shaders] != ['Vertex', 'Fragment']:
            raise RuntimeError('WEBGL-NATIVE-SHADER-STAGES')
        for shader in shaders:
            if (not shader['entryPoint'] or shader['nativeByteCount'] <= 0
                    or len(shader['nativeSha256']) != 64 or shader['encoding'] != 'ShaderEncoding.DXBC'):
                raise RuntimeError('WEBGL-NATIVE-SHADER-PAYLOAD')
        variables = [{v for b in s['constantBlocks'] for v in b['variables']} for s in shaders]
        needed = [set(), set()]
        if mesh:
            needed = [{'_arpUbo_0_0'}, {'_arpPush'}]
        elif receiver:
            needed = [{'_arpUbo_0_0', '_arpPush'}, set()]
        else:
            needed[0] = {'_arpPush'}
        if any(not n <= v for n, v in zip(needed, variables)):
            raise RuntimeError('WEBGL-NATIVE-REFLECTION-CONSTANTS')
        if (shaders[0]['readOnlyResources'] or
                [r['binding'] for r in shaders[1]['readOnlyResources']] != [0]):
            raise RuntimeError('WEBGL-NATIVE-REFLECTION-TEXTURE')
        return state

    @classmethod
    def capture(cls, controller, pipe, profile, ordinal, extent):
        import renderdoc as rd
        if not pipe.IsCaptureD3D11():
            raise RuntimeError('WEBGL-NATIVE-API-UNSUPPORTED')
        native = controller.GetD3D11PipelineState()
        rs, ds = native.rasterizer.state, native.outputMerger.depthStencilState
        blend = pipe.GetColorBlends()[0]
        vp, sc = pipe.GetViewport(0), pipe.GetScissor(0)
        shaders = []
        for label, stage in (('Vertex', rd.ShaderStage.Vertex), ('Fragment', rd.ShaderStage.Fragment)):
            shader = pipe.GetShaderReflection(stage)
            if shader is None or not shader.rawBytes:
                raise RuntimeError('WEBGL-NATIVE-SHADER-MISSING')
            shaders.append(dict(stage=label, resource=str(shader.resourceId), entryPoint=shader.entryPoint,
                encoding=str(shader.encoding), nativeByteCount=len(shader.rawBytes),
                nativeSha256=hashlib.sha256(bytes(shader.rawBytes)).hexdigest(),
                constantBlocks=[dict(name=b.name, byteSize=b.byteSize, bufferBacked=b.bufferBacked,
                                     variables=[v.name for v in b.variables]) for b in shader.constantBlocks],
                readOnlyResources=[dict(name=r.name, binding=r.fixedBindNumber) for r in shader.readOnlyResources]))
        state = dict(contract='WebGlProductNativeState/1', nativeApi=str(controller.GetAPIProperties().pipelineType),
            raster=dict(frontCCW=rs.frontCCW, cull=str(rs.cullMode), scissorEnabled=rs.scissorEnable),
            depth=dict(enabled=ds.depthEnable, writeMask=ds.depthWrites,
                       effectiveWrite=ds.depthEnable and ds.depthWrites, compare=str(ds.depthFunction),
                       stencilEnabled=ds.stencilEnable),
            blend=dict(enabled=blend.enabled, writeMask=blend.writeMask, logicEnabled=blend.logicOperationEnabled,
                       color=[str(getattr(blend.colorBlend, n)) for n in ('source', 'destination', 'operation')],
                       alpha=[str(getattr(blend.alphaBlend, n)) for n in ('source', 'destination', 'operation')]),
            viewport=[vp.x, vp.y, vp.width, vp.height, vp.minDepth, vp.maxDepth],
            scissor=[sc.x, sc.y, sc.width, sc.height], shaders=shaders)
        return cls.validate(state, profile, ordinal, extent)
