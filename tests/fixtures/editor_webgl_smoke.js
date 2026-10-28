/* Real WebGL editor, original commands and virtual file roundtrip. */
(async () => {
  const result={type:'EditorWebGlSmoke',contractVersion:1,success:false,checks:[],errors:[],gpuCompletionClaimed:false,productionReady:false};
  const deadline=performance.now()+(new URLSearchParams(location.search).get('cycles')==='20'?480000:180000);
  const wait=async predicate => {
    while (!predicate()) {
      if (Module.editorError) throw Error(Module.editorError);
      if(performance.now()>deadline) throw Error('EDITOR-WEB-SMOKE-DEADLINE '+JSON.stringify(Module.editorState));
      await new Promise(r=>setTimeout(r,50));
    }
  };
  const check=(value,name)=>{if(!value)throw Error(name+' '+JSON.stringify(Module.editorState));result.checks.push(name);};
  const action=(name,arg='')=>check(editorAction(name,arg)===1,'action:'+name);
  try {
    await wait(()=>Module.editorState?.frames>=3);
    check(crossOriginIsolated,'worker-isolation');
    check(Module.editorState.backend==='WebGL1'&&Module.editorState.draws>1000,'real-editor-frame');
    const before=Module.editorState.objects;
    action('create','Browser roundtrip');
    await wait(()=>Module.editorState.objects===before+1);
    action('undo'); await wait(()=>Module.editorState.objects===before);
    action('redo'); await wait(()=>Module.editorState.objects===before+1);
    action('component','LightComponent');
    action('script','example.arp');
    const script='// Browser saved source\n\n\nclass Fibonacci:\n    def fib(self, n: int) -> int:\n        return n\n';
    action('replace-script',script);
    action('control','workspace_3D');
    // Center of workspace_Script from editor.arpui: (992,8,66,26),
    // mapped through the actual browser canvas client rect and platform input.
    const canvasRect=Module.canvas.getBoundingClientRect();
    const pointer={bubbles:true,button:0,clientX:canvasRect.left+1025*canvasRect.width/1920,
      clientY:canvasRect.top+21*canvasRect.height/1080};
    Module.canvas.dispatchEvent(new MouseEvent('mousedown',pointer));
    Module.canvas.dispatchEvent(new MouseEvent('mouseup',pointer));
    Module.canvas.dispatchEvent(new KeyboardEvent('keydown',{key:' ',code:'Space',bubbles:true}));
    Module.canvas.dispatchEvent(new KeyboardEvent('keyup',{key:' ',code:'Space',bubbles:true}));
    await wait(()=>Module.ccall('editor_script_text','string',[],[]).length===script.length+1);
    check(true,'browser-keyboard-input');
    check(true,'browser-pointer-input');
    const edited=Module.ccall('editor_script_text','string',[],[]);
    action('save-script');
    check(Module.FS.readFile('/workspace/default/example.arp',{encoding:'utf8'})===edited,'saved-script-bytes');
    // No download side effect in automation; the same production callback is invoked.
    Module.downloadWorkspace=()=>{};
    action('export');
    await wait(()=>!!Module.editorLastExport && !Module.editorState.busy && !Module.editorState.dirty);
    const archive=Module.editorLastExport;
    check(archive.includes('arp.editor.workspace'),'yaml-export');
    const originalObjects=Module.editorState.objects;
    const rejected=archive.replace('contractVersion: 1','contractVersion: 99');
    check(Module.ccall('editor_import_workspace','number',['string'],[rejected])===0,'version-rejected');
    check(Module.ccall('editor_import_workspace','number',['string'],[archive])===1,'import-accepted');
    await wait(()=>Module.editorState.message.includes('Imported into'));
    check(Module.editorState.objects===originalObjects,'scene-roundtrip');
    action('script','example.arp');
    check(Module.ccall('editor_script_text','string',[],[])===edited,'imported-script-bytes');
    const exportFrame=Module.editorState.frames;
    action('export');
    await wait(()=>Module.editorState.frames>exportFrame&&!Module.editorState.busy&&Module.editorState.message.includes('Export download requested'));
    check(!Module.editorState.scriptDirty,'script-roundtrip');
    const frame=Module.editorState.frames;
    Module.canvas.style.width='1100px';Module.canvas.style.height='660px';
    await wait(()=>Module.editorState.frames>frame+3 && Module.canvas.width===1100 && Module.canvas.height===660);
    check(true,'resize');
    if(new URLSearchParams(location.search).get('cycles')==='20'){
      const memory=()=>[Module.ccall('editor_core_live_bytes','number',[],[]),Module.ccall('editor_core_live_allocations','number',[],[])];
      let baseline;
      result.memory=[];
      // First iteration warms stable presentation/allocator caches. Subsequent
      // identical workspaces must release all old Shell/VM/worker owners.
      for(let cycle=0;cycle<=20;++cycle){
        const frame=Module.editorState.frames;
        check(Module.ccall('editor_import_workspace','number',['string'],[archive])===1,'lifecycle-import:'+cycle);
        await wait(()=>Module.editorState.frames>frame&&!Module.editorState.busy&&Module.editorState.message.includes('Imported into'));
        action('script','example.arp');
        const scriptFrame=Module.editorState.frames;
        await wait(()=>Module.editorState.frames>scriptFrame);
        const current=memory();
        check(Module.FS.readdir('/workspace').filter(name=>name.startsWith('import-')).length===1,'virtual-directory-baseline:'+cycle);
        if(cycle===0)baseline=current;
        result.memory.push(current);
        check(current[0]===baseline[0]&&current[1]===baseline[1],'core-baseline:'+cycle);
      }
    }
    result.success=true;result.frames=Module.editorState.frames;result.draws=Module.editorState.draws;
  } catch(error) {result.errors.push(String(error?.stack||error));}
  await fetch('/result',{method:'POST',body:JSON.stringify(result)});
})();
