/* Explicit connection input; credentials never enter URLs or browser storage. */
(() => {
  'use strict';
  const panel = document.getElementById('workspace-connection');
  const toggle = document.getElementById('workspace-connect');
  const fileInput = document.getElementById('workspace-profile');
  const fileName = document.getElementById('workspace-profile-name');
  const tokenInput = document.getElementById('workspace-token');
  let configuration = '';
  let selection = 0;
  const maxConfigurationBytes = 64 * 1024;

  window.editorConnectWorkspace = (yaml, token) => {
    if (typeof yaml !== 'string' || typeof token !== 'string' || !yaml ||
        yaml.includes('\0') || token.includes('\0') || token.length > 256 ||
        new TextEncoder().encode(yaml).length > maxConfigurationBytes ||
        !Module.editorState || Module.editorState.busy || Module.editorError)
      return false;
    return !!Module.ccall('editor_connect_workspace', 'number', ['string', 'string'], [yaml, token]);
  };

  toggle.onclick = () => {
    panel.hidden = !panel.hidden;
    toggle.setAttribute('aria-expanded', String(!panel.hidden));
  };
  document.getElementById('workspace-profile-select').onclick = () => fileInput.click();
  fileInput.onchange = async () => {
    const currentSelection = ++selection;
    configuration = '';
    fileName.textContent = 'No configuration selected';
    const file = fileInput.files[0];
    fileInput.value = '';
    if (!file) return;
    try {
      if (!file.size || file.size > maxConfigurationBytes) throw new Error();
      const text = new TextDecoder('utf-8', {fatal:true}).decode(await file.arrayBuffer());
      if (text.includes('\0')) throw new Error();
      if (currentSelection !== selection) return;
      configuration = text;
      fileName.textContent = file.name;
    } catch {
      if (currentSelection !== selection) return;
      Module.onEditorError('Connection configuration must be UTF-8 YAML within 64 KiB.');
    }
  };
  document.getElementById('workspace-connection-form').onsubmit = event => {
    event.preventDefault();
    const token = tokenInput.value;
    tokenInput.value = '';
    if (!window.editorConnectWorkspace(configuration, token)) {
      Module.onEditorError('Connection rejected. Choose a valid configuration and resolve pending or unsaved changes.');
      return;
    }
    document.getElementById('error').textContent = '';
  };
  window.addEventListener('pagehide', () => { tokenInput.value = ''; });
})();
