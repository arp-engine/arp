/* Test-only actual product connection and warm lifecycle probe. */
(async () => {
  const pause = () => new Promise(resolve => setTimeout(resolve, 50));
  const wait = async (predicate, label) => {
    const deadline = performance.now() + 90000;
    while (!predicate()) {
      if (Module.editorError || performance.now() > deadline)
        throw Error(Module.editorError || label);
      await pause();
    }
  };
  const report = {success:false, gpuCompletionClaimed:false, cycles:0};
  try {
    await wait(() => Module.editorState?.frames > 2, 'workspace startup');
    const config = await (await fetch('/workspace-smoke-config', {cache:'no-store'})).json();
    const reopening = new URLSearchParams(location.search).has('workspace-reopen');
    const connect = async (yaml, token, success) => {
      const frame = Module.editorState.frames;
      if (!window.editorConnectWorkspace(yaml, token)) throw Error('connection not accepted');
      await wait(() => Module.editorState.frames > frame + 1 && !Module.editorState.busy,
        'workspace candidate terminal');
      if (success !== Module.editorState.message.startsWith('Connected workspace'))
        throw Error('connection terminal: ' + Module.editorState.message);
    };
    if (!reopening) {
      const before = Module.editorState.objects;
      if (window.editorConnectWorkspace(config.yaml + '\0bad', config.writeToken))
        throw Error('NUL configuration accepted');
      await connect(config.yaml, 'wrong-token-012345678901234567890123', false);
      if (Module.editorState.objects !== before || Module.editorState.connectedWorkspace)
        throw Error('failed handshake replaced original');
      await connect(config.missingYaml, config.writeToken, false);
      if (Module.editorState.objects !== before || Module.editorState.connectedWorkspace)
        throw Error('failed project replaced original');
      await connect(config.yaml, config.readToken, true);
      if (!Module.editorState.workspaceReadOnly || Module.editorState.persistent)
        throw Error('readonly capability projection');
      // Exercise the actual file selector/form, with a separately entered token.
      document.getElementById('workspace-connect').click();
      const selected = new DataTransfer();
      selected.items.add(new File([config.yaml], 'connection.yaml', {type:'application/yaml'}));
      const input = document.getElementById('workspace-profile');
      input.files = selected.files;
      input.dispatchEvent(new Event('change'));
      await wait(() => document.getElementById('workspace-profile-name').textContent === 'connection.yaml',
        'connection file preparation');
      const frame = Module.editorState.frames;
      const credential = document.getElementById('workspace-token');
      credential.value = config.writeToken;
      document.getElementById('workspace-connection-form').requestSubmit();
      if (credential.value) throw Error('credential retained in form');
      await wait(() => Module.editorState.frames > frame + 1 && !Module.editorState.busy,
        'connection form terminal');
      if (!Module.editorState.persistent) throw Error('connection form not connected');
      document.getElementById('workspace-connect').click();
      report.formVerified = true;
      const memory = [];
      for (let i = 0; i < config.connectionCycles; ++i) {
        await connect(config.yaml, config.writeToken, true);
        memory.push({bytes:Module.ccall('editor_core_live_bytes','number',[],[]),
          allocations:Module.ccall('editor_core_live_allocations','number',[],[]),
          gpuBytes:Module.ccall('editor_gpu_logical_bytes','number',[],[]),
          gpuResources:Module.ccall('editor_gpu_live_resources','number',[],[])});
        report.cycles++;
      }
      if (memory.some(item => JSON.stringify(item) !== JSON.stringify(memory[0])))
        throw Error('workspace connection lifecycle counts: ' + JSON.stringify(memory));
      report.ordinaryMemory = memory;
    } else {
      await connect(config.yaml, config.writeToken, true);
    }
    if (!Module.editorState.persistent || Module.editorState.workspaceReadOnly)
      throw Error('write capability projection');
    document.getElementById('cli').click();
    await wait(() => !!editorCliBridge.session, 'workspace CLI connection');
    await fetch('/workspace-smoke-ready', {method:'POST',body:JSON.stringify({reopening})});
    let finish;
    const deadline = performance.now() + config.workflowTimeoutMs;
    do {
      finish = await (await fetch('/workspace-smoke-finish', {cache:'no-store'})).json();
      if (Module.editorError || performance.now() > deadline)
        throw Error(Module.editorError || 'workspace CLI timeout');
      if (finish.action === 'measure') {
        const frame = Module.editorState.frames;
        await wait(() => Module.editorState.frames > frame + 1 && !Module.editorState.busy,
          'receipt stable sample');
        await fetch('/workspace-smoke-metrics', {method:'POST', body:JSON.stringify({
          bytes:Module.ccall('editor_core_live_bytes','number',[],[]),
          allocations:Module.ccall('editor_core_live_allocations','number',[],[]),
          gpuBytes:Module.ccall('editor_gpu_logical_bytes','number',[],[]),
          gpuResources:Module.ccall('editor_gpu_live_resources','number',[],[])})});
        finish = {action:''};
      }
      if (!finish.action) await pause();
    } while (!finish.action);
    report.success = true;
    report.reopening = reopening;
    await fetch('/workspace-smoke-result', {method:'POST',body:JSON.stringify(report)});
    if (finish.action === 'reload') {
      await editorCliBridge.disconnect();
      location.replace('/arp_editor.html?workspace-smoke=1&workspace-reopen=1');
    }
  } catch (error) {
    report.error = String(error);
    await fetch('/workspace-smoke-result', {method:'POST',body:JSON.stringify(report)});
  }
})();
