(async () => {
  const wait=async predicate=>{while(!predicate()){if(Module.editorError)throw Error(Module.editorError);await new Promise(r=>setTimeout(r,100));}};
  try {
    await wait(()=>Module.editorState?.frames>=3);
    const workspace=new URLSearchParams(location.search).has('capture-workspace');
    const source=new URLSearchParams(location.search).has('capture-source');
    let connection;
    const sourceRequest = command => {
      const response=JSON.parse(Module.ccall('editor_remote_request','string',
        ['string','string','string','string','string','string'],
        ['POST','/api/v1/commands',JSON.stringify({schemaVersion:1,...command}),
          'renderdoc-source-workspace-01234567890123456789','application/json','']));
      if(response.status!=='completed')throw Error('EDITOR-CAPTURE-SOURCE-COMMAND: '+JSON.stringify(response));
      return response;
    };
    if(source){
      connection=await (await fetch('/editor-capture-connection',{cache:'no-store'})).json();
      if(!window.editorConnectWorkspace(connection.yaml,connection.token))throw Error('EDITOR-CAPTURE-SOURCE-CONNECT');
      await wait(()=>Module.editorState.connectedWorkspace&&!Module.editorState.busy);
      const session='renderdoc-source-workspace-01234567890123456789';
      const request=JSON.stringify({schemaVersion:1,command:'scene.load',path:connection.scene});
      const response=JSON.parse(Module.ccall('editor_remote_request','string',
        ['string','string','string','string','string','string'],
        ['POST','/api/v1/commands',request,session,'application/json','']));
      if(response.status!=='completed')throw Error('EDITOR-CAPTURE-SOURCE-SCENE');
      const frame=Module.editorState.frames;
      await wait(()=>Module.editorState.frames>frame+3&&!Module.editorState.busy);
    }
    if(workspace){
      const archive=await (await fetch('/capture-workspace.yaml')).text();
      if(Module.ccall('editor_import_workspace','number',['string'],[archive])!==1)throw Error('EDITOR-CAPTURE-WORKSPACE-IMPORT');
      await wait(()=>Module.editorState.message.includes('Imported into'));
    }
    await fetch('/editor-ready',{method:'POST',body:JSON.stringify(Module.editorState)});
    await wait(asyncPredicate);
    if(source&&connection.reloadModules){
      const accepted=sourceRequest({command:'scene.load',path:'scene.yaml',discardChanges:true});
      const frame=Module.editorState.frames;
      await wait(()=>Module.editorState.frames>frame+3&&!Module.editorState.busy);
      const state=sourceRequest({command:'scene.task.get',taskId:accepted.snapshot.scenePersistenceTask.taskId}).snapshot;
      if(state.scenePersistenceTask.status!=='succeeded')throw Error('EDITOR-CAPTURE-RELOAD-V4-SCENE: '+JSON.stringify(state.scenePersistenceTask));
    }
    const frame=Module.editorState.frames;
    if(editorAction('script',source?connection.script:workspace?'hello_world.arp':'example.arp')!==1)throw Error('EDITOR-CAPTURE-SCRIPT-OPEN');
    await wait(()=>Module.editorState.frames>frame+3&&!Module.editorState.busy);
    if(source&&connection.reloadModules){
      const text=Module.ccall('editor_script_text','string',[],[]);
      if(!editorAction('replace-script',text+'\n# RenderDoc explicit module reload\n'))throw Error('EDITOR-CAPTURE-RELOAD-EDIT');
      let frame=Module.editorState.frames;
      if(!editorAction('save-script'))throw Error('EDITOR-CAPTURE-RELOAD-SCRIPT-SAVE');
      await wait(()=>Module.editorState.frames>frame+3&&!Module.editorState.busy);
      frame=Module.editorState.frames;
      if(!editorAction('reload-scripts'))throw Error('EDITOR-CAPTURE-RELOAD-ACCEPT');
      await wait(()=>Module.editorState.frames>frame+3&&!Module.editorState.busy);
      const reloaded=sourceRequest({command:'editor.state.get'}).snapshot;
      if(reloaded.projectIoTask.status!=='succeeded'||!reloaded.sceneDirty)throw Error('EDITOR-CAPTURE-RELOAD-TERMINAL: '+JSON.stringify(reloaded.projectIoTask));
      frame=Module.editorState.frames;
      if(!editorAction('save-scene'))throw Error('EDITOR-CAPTURE-RELOAD-SCENE-SAVE');
      await wait(()=>Module.editorState.frames>frame+3&&!Module.editorState.busy);
      const saved=sourceRequest({command:'editor.state.get'}).snapshot;
      if(saved.sceneDirty||!saved.sceneSaveReceipt?.published)throw Error('EDITOR-CAPTURE-RELOAD-SCENE-PUBLISH');
      Module.editorColdReloadValidated=true;
    }
    await fetch('/editor-script-ready',{method:'POST',body:JSON.stringify(Module.editorState)});
  } catch(error){await fetch('/editor-error',{method:'POST',body:String(error)});}
  function asyncPredicate(){return scriptPhase;}
})();
let scriptPhase=false;
const phaseTimer=setInterval(async()=>{
  if((await (await fetch('/editor-capture-phase')).text())==='script'){scriptPhase=true;clearInterval(phaseTimer);}
},250);
