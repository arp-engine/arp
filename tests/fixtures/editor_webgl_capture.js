(async () => {
  const wait=async predicate=>{while(!predicate()){if(Module.editorError)throw Error(Module.editorError);await new Promise(r=>setTimeout(r,100));}};
  try {
    await wait(()=>Module.editorState?.frames>=3);
    const workspace=new URLSearchParams(location.search).has('capture-workspace');
    if(workspace){
      const archive=await (await fetch('/capture-workspace.yaml')).text();
      if(Module.ccall('editor_import_workspace','number',['string'],[archive])!==1)throw Error('EDITOR-CAPTURE-WORKSPACE-IMPORT');
      await wait(()=>Module.editorState.message.includes('Imported into'));
    }
    await fetch('/editor-ready',{method:'POST',body:JSON.stringify(Module.editorState)});
    await wait(asyncPredicate);
    const frame=Module.editorState.frames;
    if(editorAction('script',workspace?'hello_world.arp':'example.arp')!==1)throw Error('EDITOR-CAPTURE-SCRIPT-OPEN');
    await wait(()=>Module.editorState.frames>frame+3);
    await fetch('/editor-script-ready',{method:'POST',body:JSON.stringify(Module.editorState)});
  } catch(error){await fetch('/editor-error',{method:'POST',body:String(error)});}
  function asyncPredicate(){return scriptPhase;}
})();
let scriptPhase=false;
const phaseTimer=setInterval(async()=>{
  if((await (await fetch('/editor-capture-phase')).text())==='script'){scriptPhase=true;clearInterval(phaseTimer);}
},250);
