import {WebGlShaderProgram} from './webgl_shader_program.js';
const canvas = document.querySelector('canvas');
const report = {type:'WebGlShaderProgramValidation',contractVersion:1,success:false,productionBackendValidated:false};
const need = (condition,id) => { if (!condition) throw Error(id); };
try {
    const fixture = await (await fetch('./fixture.json')).json();
    const gl = canvas.getContext('webgl',{alpha:false,depth:true,stencil:false,antialias:false,preserveDrawingBuffer:true});
    need(gl,'WEBGL1-CONTEXT'); report.version = gl.getParameter(gl.VERSION);
    report.renderer = gl.getParameter(gl.RENDERER); report.programIdentity = fixture.snapshots.programIdentity;
    const program = new WebGlShaderProgram(gl,fixture.targets,fixture.program.textureUnits,{maxDraws:8,maxBytes:1280});
    need(gl.getAttachedShaders(program.program).length===0,'WEBGL1-LINKED-SHADER-RETIREMENT');
    report.linkedShaderAttachmentsRetired=true;
    const snapshots = fixture.snapshots.draws.map(blocks => program.prepareSnapshot(blocks.map(b => ({...b,bytes:new Uint8Array(b.bytes)}))));
    // Explicit ABI failures use the real GL linked active interface.
    let rejected=0;
    function mustReject(targets,fragment) {
        const changed=structuredClone(targets); fragment(changed);
        try { const bad=new WebGlShaderProgram(gl,changed,fixture.program.textureUnits,{maxDraws:1,maxBytes:144}); bad.destroy(); }
        catch(error) { ++rejected; return; }
        throw Error('WEBGL1-NEGATIVE-ACCEPTED');
    }
    mustReject(fixture.targets,t=>t[0].interface.inputs[0].name='missingAttribute');
    mustReject(fixture.targets,t=>{t[0].blocks[0].leaves[0].name='missingRequired';});
    mustReject(fixture.targets,t=>{t[0].blocks[0].leaves[0].components=3;});
    mustReject(fixture.targets,t=>{t[1].requiredExtensions=['GL_FAKE'];});
    mustReject(fixture.targets,t=>{t[1].uniformVectors=gl.getParameter(gl.MAX_FRAGMENT_UNIFORM_VECTORS)+1;});
    const limitedGl = new Proxy(gl,{get(target,key) {
        if(key==='getShaderPrecisionFormat') return ()=>({precision:0,rangeMin:0,rangeMax:0});
        const value=Reflect.get(target,key,target);return typeof value==='function'?value.bind(target):value;
    }});
    try {new WebGlShaderProgram(limitedGl,fixture.targets,fixture.program.textureUnits,{maxDraws:1,maxBytes:160});throw Error('WEBGL1-HIGHP-GATE');}
    catch(e) {need(e.message==='SHADER-WEBGL1-HIGHP-UNAVAILABLE',e.message);++rejected;}
    // Clone typed bytes, prepare once, then overwrite the caller's source.
    const whiteBlocks = fixture.snapshots.draws[0].map(b=>({...b,bytes:new Uint8Array(b.bytes)}));
    const uniform = whiteBlocks.find(b=>!b.push), push=whiteBlocks.find(b=>b.push);
    const u = new Float32Array(uniform.bytes.buffer), p = new Float32Array(push.bytes.buffer);
    u.fill(0); for (const index of [0,5,10,15]) u[index]=1;
    u.fill(1,16,24); p.fill(0); p[4]=p[5]=p[6]=1;
    const copySnapshot=program.prepareSnapshot(whiteBlocks);
    u[11]=-.25;
    const clippedSnapshot=program.prepareSnapshot(whiteBlocks);
    u[11]=0; const cpuSnapshot=program.prepareSnapshot(whiteBlocks);
    for (const b of whiteBlocks) b.bytes.fill(0);
    try { program.applySnapshot({}); throw Error('WEBGL1-FORGED-SNAPSHOT'); } catch(e) { need(e.message==='SHADER-WEBGL1-SNAPSHOT-PROGRAM',e.message); ++rejected; }
    const vertexBuffer=gl.createBuffer(); need(vertexBuffer,'WEBGL1-BUFFER'); gl.bindBuffer(gl.ARRAY_BUFFER,vertexBuffer);
    const triangle=new Float32Array([-.5,-.5,0,0,0,.5,.75,1,1, .5,-.5,0,1,0,.5,.75,1,1, 0,.5,0,.5,1,.5,.75,1,1]);
    const fullscreen=new Float32Array([-1,-1,0,0,1,1,1,1,1, 3,-1,0,2,1,1,1,1,1, -1,3,0,0,-1,1,1,1,1]);
    const cpuVertices=new Float32Array(fullscreen); cpuVertices[4]=0;cpuVertices[13]=0;cpuVertices[22]=2;
    function vertices(data) {
        gl.bindBuffer(gl.ARRAY_BUFFER,vertexBuffer);gl.bufferData(gl.ARRAY_BUFFER,data,gl.STATIC_DRAW);
        for (const a of fixture.targets[0].interface.inputs) {
            const offset = a.location===0 ? 0 : a.location===1 ? 12 : 20;
            gl.enableVertexAttribArray(a.location);gl.vertexAttribPointer(a.location,a.components,gl.FLOAT,false,36,offset);
        }
    }
    function texture(width,height,bytes) {
        const t=gl.createTexture(); need(t,'WEBGL1-TEXTURE');gl.bindTexture(gl.TEXTURE_2D,t);
        gl.pixelStorei(gl.UNPACK_ALIGNMENT,1);gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL,false);
        gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL,false);gl.pixelStorei(gl.UNPACK_COLORSPACE_CONVERSION_WEBGL,gl.NONE);
        gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,gl.NEAREST);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER,gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_S,gl.CLAMP_TO_EDGE);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_T,gl.CLAMP_TO_EDGE);
        gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,width,height,0,gl.RGBA,gl.UNSIGNED_BYTE,bytes);return t;
    }
    const a=texture(1,1,new Uint8Array([128,64,0,128])),b=texture(1,1,new Uint8Array([64,128,0,128]));
    const white=texture(1,1,new Uint8Array([255,255,255,255]));
    const raster=texture(2,2,new Uint8Array([255,0,0,255,255,0,0,255,0,255,0,255,0,255,0,255]));
    const rt=texture(64,64,null), fbo=gl.createFramebuffer(), depth=gl.createRenderbuffer();
    gl.bindFramebuffer(gl.FRAMEBUFFER,fbo);gl.framebufferTexture2D(gl.FRAMEBUFFER,gl.COLOR_ATTACHMENT0,gl.TEXTURE_2D,rt,0);
    gl.bindRenderbuffer(gl.RENDERBUFFER,depth);gl.renderbufferStorage(gl.RENDERBUFFER,gl.DEPTH_COMPONENT16,64,64);
    gl.framebufferRenderbuffer(gl.FRAMEBUFFER,gl.DEPTH_ATTACHMENT,gl.RENDERBUFFER,depth);
    need(gl.checkFramebufferStatus(gl.FRAMEBUFFER)===gl.FRAMEBUFFER_COMPLETE,'WEBGL1-FBO');
    function bindTextures(first,second) {
        gl.activeTexture(gl.TEXTURE0);gl.bindTexture(gl.TEXTURE_2D,first);
        gl.activeTexture(gl.TEXTURE1);gl.bindTexture(gl.TEXTURE_2D,second);
    }
    function state() {
        gl.viewport(0,0,64,64); gl.scissor(0,0,64,64);gl.enable(gl.SCISSOR_TEST);
        gl.colorMask(true,true,true,true);gl.depthMask(true);gl.depthFunc(gl.LESS);gl.enable(gl.DEPTH_TEST);
        gl.depthRange(0,1);gl.clearDepth(1);gl.clearColor(0,0,0,0);gl.stencilMask(0);gl.disable(gl.STENCIL_TEST);
        gl.disable(gl.POLYGON_OFFSET_FILL);gl.disable(gl.SAMPLE_ALPHA_TO_COVERAGE);gl.disable(gl.SAMPLE_COVERAGE);gl.disable(gl.DITHER);
        gl.frontFace(gl.CCW);gl.cullFace(gl.BACK);gl.enable(gl.CULL_FACE);
        gl.blendEquationSeparate(gl.FUNC_ADD,gl.FUNC_ADD);gl.blendFuncSeparate(gl.SRC_ALPHA,gl.ONE_MINUS_SRC_ALPHA,gl.ONE,gl.ONE_MINUS_SRC_ALPHA);
    }
    const pixels=new Uint8Array(64*64*4),final=new Uint8Array(64*64*4);
    const sample=(image,x,y)=>Array.from(image.subarray((y*64+x)*4,(y*64+x)*4+3));
    function render() {
        state();gl.bindFramebuffer(gl.FRAMEBUFFER,fbo);gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);
        vertices(triangle);bindTextures(a,b);gl.enable(gl.BLEND);
        program.applySnapshot(snapshots[0]);gl.drawArrays(gl.TRIANGLES,0,3);
        program.applySnapshot(snapshots[1]);gl.drawArrays(gl.TRIANGLES,0,3);
        gl.readPixels(0,0,64,64,gl.RGBA,gl.UNSIGNED_BYTE,pixels);
        need(JSON.stringify(sample(pixels,24,27))==='[32,0,0]','WEBGL1-FIRST-SNAPSHOT: '+sample(pixels,24,27));
        need(JSON.stringify(sample(pixels,46,27))==='[0,48,0]','WEBGL1-SECOND-SNAPSHOT: '+sample(pixels,46,27));
        need(JSON.stringify(sample(pixels,24,36))==='[0,0,0]','WEBGL1-MATRIX-ASYMMETRY');
        // Same shader, explicit attachment UV inversion, no extra matrix flip.
        gl.bindFramebuffer(gl.FRAMEBUFFER,null);gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);
        gl.disable(gl.BLEND);gl.disable(gl.CULL_FACE);vertices(fullscreen);bindTextures(rt,rt);
        program.applySnapshot(copySnapshot);gl.drawArrays(gl.TRIANGLES,0,3);
        gl.readPixels(0,0,64,64,gl.RGBA,gl.UNSIGNED_BYTE,final);
        for(let i=0;i<final.length;i+=4) for(let c=0;c<3;++c) need(final[i+c]===pixels[i+c],'WEBGL1-ATTACHMENT-ROUNDTRIP');
        need(gl.getError()===gl.NO_ERROR,'WEBGL1-GL-ERROR');
    }
    // Separate deterministic clipping, CPU raster and top-left scissor checks.
    state();gl.bindFramebuffer(gl.FRAMEBUFFER,null);gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);gl.disable(gl.CULL_FACE);gl.disable(gl.BLEND);
    vertices(fullscreen);bindTextures(a,b);program.applySnapshot(clippedSnapshot);gl.drawArrays(gl.TRIANGLES,0,3);
    gl.readPixels(0,0,64,64,gl.RGBA,gl.UNSIGNED_BYTE,final);need(final.every((v,i)=>i%4===3||v===0),'WEBGL1-CLIP-Z');
    vertices(cpuVertices);bindTextures(raster,raster);program.applySnapshot(cpuSnapshot);gl.scissor(3,44,11,13);gl.drawArrays(gl.TRIANGLES,0,3);
    gl.readPixels(0,0,64,64,gl.RGBA,gl.UNSIGNED_BYTE,final);
    need(JSON.stringify(sample(final,7,50))==='[255,0,0]' && JSON.stringify(sample(final,7,40))==='[0,0,0]','WEBGL1-CPU-RASTER-SCISSOR');
    // Real derivative + explicit-LOD package: queried extension gates and execution.
    const extTargets=fixture.extensionTargets,extUnits=fixture.extensionProgram.textureUnits;
    const missingExtensionGl=new Proxy(gl,{get(target,key) {
        if(key==='getExtension')return ()=>null;
        const value=Reflect.get(target,key,target);return typeof value==='function'?value.bind(target):value;
    }});
    try {new WebGlShaderProgram(missingExtensionGl,extTargets,extUnits,{maxDraws:1,maxBytes:1});throw Error('WEBGL1-EXTENSION-GATE');}
    catch(e) {need(e.message==='SHADER-WEBGL1-REQUIRED-EXTENSION',e.message);++rejected;}
    const extensionProgram=new WebGlShaderProgram(gl,extTargets,extUnits,{maxDraws:1,maxBytes:1});
    const extSnapshot=extensionProgram.prepareSnapshot([]);
    gl.scissor(0,0,64,64);gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);bindTextures(white,white);
    gl.bindBuffer(gl.ARRAY_BUFFER,vertexBuffer);gl.bufferData(gl.ARRAY_BUFFER,new Float32Array([-1,-1,0,1,3,-1,2,1,-1,3,0,-1]),gl.STATIC_DRAW);
    for(const attr of extTargets[0].interface.inputs) {
        gl.enableVertexAttribArray(attr.location);gl.vertexAttribPointer(attr.location,2,gl.FLOAT,false,16,attr.location*8);
    }
    extensionProgram.applySnapshot(extSnapshot);gl.drawArrays(gl.TRIANGLES,0,3);
    gl.readPixels(0,0,64,64,gl.RGBA,gl.UNSIGNED_BYTE,final);
    need(JSON.stringify(sample(final,32,32))==='[8,8,8]','WEBGL1-DERIVATIVE-LOD-NUMERICAL: '+sample(final,32,32));
    extensionProgram.destroy();report.derivativeLod=true;
    const query=new URLSearchParams(location.search);
    if(query.get('captureGate')==='1') {
        await fetch('/ready',{method:'POST'});
        for(let attempt=0;attempt<400;++attempt) {
            if((await fetch('/armed')).status===200) break;
            if(attempt===399) throw Error('WEBGL1-CAPTURE-GATE-TIMEOUT');
            await new Promise(resolve=>setTimeout(resolve,100));
        }
    }
    const frames=Number(query.get('frames')||2);
    for(let frame=0;frame<frames;++frame) { render(); await new Promise(requestAnimationFrame); }
    report.success=true;report.activeInterfaceValidated=true;report.negativeCases=rejected;
    report.immutableTwoDraws=true;report.twoTextures=true;report.matrixNumerical=true;report.clipDepth=true;
    report.blendVertexColor=true;report.cpuRasterScissor=true;report.attachmentRoundtrip=true;report.frames=frames;
    report.samples=[sample(pixels,24,27),sample(pixels,46,27),sample(pixels,24,36)];
    report.rgba=Array.from(final);report.width=64;report.height=64;
    program.destroy();gl.deleteBuffer(vertexBuffer);gl.deleteFramebuffer(fbo);gl.deleteRenderbuffer(depth);
    for(const t of [a,b,white,raster,rt])gl.deleteTexture(t);
} catch(error) { report.diagnostic=String(error.stack||error); }
await fetch('/result',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(report)});
