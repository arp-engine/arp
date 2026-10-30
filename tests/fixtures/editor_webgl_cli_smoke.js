/* Test-only startup. Production pages require an explicit Connect CLI click. */
(async () => {
  const deadline = performance.now() + 90000;
  while (!Module.editorState?.frames) {
    if (Module.editorError || performance.now() > deadline) throw Error(Module.editorError || 'CLI smoke startup timeout');
    await new Promise(resolve => setTimeout(resolve, 50));
  }
  const samples=[];
  let command='';
  const request=editorCliBridge.request.bind(editorCliBridge);
  editorCliBridge.request=async (path,value)=>{
    const result=await request(path,value);
    if(path==='/bridge/poll'&&result){
      try{command=JSON.parse(result.body).command||'';}catch{command='';}
    }
    if(path==='/bridge/result'){
      samples.push({command,bytes:Module.ccall('editor_core_live_bytes','number',[],[]),
        allocations:Module.ccall('editor_core_live_allocations','number',[],[])});
      if(samples.length>64)samples.shift();
    }
    return result;
  };
  document.getElementById('cli').click();
  while (!editorCliBridge.session) {
    if (Module.editorError || performance.now() > deadline) throw Error(Module.editorError || 'CLI smoke connect timeout');
    await new Promise(resolve => setTimeout(resolve, 50));
  }
  await fetch('/cli-smoke-ready', {method: 'POST', body: '{}'});
  while (!(await (await fetch('/cli-smoke-finish')).json()).finish) {
    if (Module.editorError || performance.now() > deadline + 270000) throw Error(Module.editorError || 'CLI smoke finish timeout');
    await new Promise(resolve => setTimeout(resolve, 200));
  }
  await fetch('/cli-smoke-memory',{method:'POST',body:JSON.stringify(samples)});
  Module.downloadWorkspace = text => fetch('/cli-smoke-export', {method: 'POST', body: text});
  if (editorAction('export') !== 1) throw Error('CLI smoke export rejected');
})();
