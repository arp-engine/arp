/* Failure injection at the real browser API/file boundary, never GPU authority. */
const negativeCase=new URLSearchParams(location.search).get('negative');
if(negativeCase==='derivatives'||negativeCase==='fragment-depth'||negativeCase==='srgb'){
  const getExtension=WebGLRenderingContext.prototype.getExtension;
  const denied=negativeCase==='derivatives'?'OES_standard_derivatives':negativeCase==='srgb'?'EXT_sRGB':'EXT_frag_depth';
  WebGLRenderingContext.prototype.getExtension=function(name){return name===denied?null:getExtension.call(this,name);};
}
if(negativeCase==='shader'){
  const originalFetch=globalThis.fetch;
  const denied=new URL('editor-sources/shaders/gui_unified.webgl-program.yaml',location.href).href;
  globalThis.fetch=function(input,...options){
    const url=typeof input==='string'?input:input.url;
    if(url===denied)return Promise.resolve(new Response('missing original publication',{status:404}));
    return originalFetch.call(this,input,...options);
  };
}
(async()=>{
  const deadline=performance.now()+60000;
  const wait=async predicate=>{while(!predicate()){if(performance.now()>deadline)throw Error('EDITOR-NEGATIVE-DEADLINE');await new Promise(r=>setTimeout(r,50));}};
  const result={type:'EditorWebGlNegative',contractVersion:1,scenario:negativeCase,success:false,gpuCompletionClaimed:false,productionReady:false};
  try{
    if(negativeCase==='visibility'||negativeCase==='loss'){
      await wait(()=>Module.editorState?.frames>=3||Module.editorError);
      if(Module.editorError)throw Error(Module.editorError);
      if(negativeCase==='visibility'){
        window.dispatchEvent(new PageTransitionEvent('pagehide'));
        await new Promise(r=>setTimeout(r,250));const frame=Module.editorState.frames;
        await new Promise(r=>setTimeout(r,250));
        if(Module.editorState.frames!==frame)throw Error('EDITOR-HIDDEN-STILL-DRAWS');
        window.dispatchEvent(new PageTransitionEvent('pageshow'));
        await wait(()=>Module.editorState.frames>frame+2);
        result.success=true;
      }else{
        const extension=Module.canvas.getContext('webgl').getExtension('WEBGL_lose_context');
        if(!extension)throw Error('EDITOR-LOSS-FIXTURE-EXTENSION-UNAVAILABLE');
        extension.loseContext();await wait(()=>!!Module.editorError);
        result.diagnostic=Module.editorError;result.success=result.diagnostic.includes('CONTEXT-LOST');
      }
    }else{
      await wait(()=>!!Module.editorError);
      result.diagnostic=Module.editorError;
      result.success=!Module.editorState?.frames && (negativeCase==='derivatives'||negativeCase==='fragment-depth'||negativeCase==='srgb'
        ?result.diagnostic.startsWith('RHI-WEBGL1-EXTENSION-'):result.diagnostic.includes('IO-HTTP-STATUS'));
    }
  }catch(error){result.error=String(error);}
  await fetch('/result',{method:'POST',body:JSON.stringify(result)});
})();
