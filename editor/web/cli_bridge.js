/* Transport only. All editor behavior goes through EditorRemoteProtocol. */
class EditorCliBridge {
  constructor(module, button) {
    this.module = module;
    this.button = button;
    this.session = null;
    this.running = false;
    this.timer = null;
    // An explicit local CLI launch link is an alternative to the toolbar click.
    // Ordinary pages never claim a session, and failures never auto-reconnect.
    this.launchRequested = new URLSearchParams(location.search).get('cli') === '1';
    this.launchAttempted = false;
    button.addEventListener('click', () => this.toggle());
    window.addEventListener('pagehide', () => this.disconnect());
  }

  onEditorReady(state) {
    if (this.launchRequested && !this.launchAttempted && state.frames > 0) {
      this.launchAttempted = true;
      this.toggle();
    }
  }

  async request(path, value) {
    const headers = {'X-ARP-Editor-Bridge': '1'};
    if (this.session) headers['X-ARP-Editor-Session'] = this.session;
    if (value !== undefined) headers['Content-Type'] = 'application/json';
    const response = await fetch(path, {
      method: value === undefined ? 'GET' : 'POST', headers,
      body: value === undefined ? undefined : JSON.stringify(value),
      cache: 'no-store', keepalive: path === '/bridge/disconnect'
    });
    if (!response.ok) throw Error(`CLI bridge: HTTP ${response.status} ${await response.text()}`);
    return response.json();
  }

  async toggle() {
    try {
      if (this.session) {
        await this.disconnect();
        return;
      }
      if (this.module.editorError)
        throw Error(`CLI unavailable: ${this.module.editorError}`);
      if (!this.module.editorState) throw Error('Editor is not ready');
      const connection = await this.request('/bridge/connect', {});
      if (connection.contractVersion !== 1 || typeof connection.session !== 'string')
        throw Error('Unsupported CLI bridge contract');
      this.session = connection.session;
      this.button.textContent = 'Disconnect CLI';
      await this.poll();
    } catch (error) {
      this.module.onEditorError(String(error));
    }
  }

  async poll() {
    if (!this.session || this.running) return;
    this.running = true;
    try {
      if (this.module.editorError)
        throw Error(`CLI request was not executed because the editor reported: ${this.module.editorError}`);
      const request = await this.request('/bridge/poll');
      if (request) {
        if (typeof request.id !== 'string' || typeof request.body !== 'string'
            || typeof request.accept !== 'string' || typeof request.protocolVersion !== 'string'
            || !['GET', 'POST'].includes(request.method)
            || !['/api/v1/commands', '/api/v1/discovery', '/mcp'].includes(request.path))
          throw Error('Invalid CLI bridge request');
        const containsNul = request.body.includes('\0');
        const body = containsNul ? '{"error":"web_bridge_invalid_utf8_text"}'
          : this.module.ccall('editor_remote_request', 'string',
            ['string', 'string', 'string', 'string', 'string', 'string'],
            [request.method, request.path, request.body, this.session, request.accept, request.protocolVersion]);
        const status = containsNul ? 400 : this.module.ccall('editor_remote_status', 'number', [], []);
        // Never retry a delivered request: the server retains an unknown outcome.
        await this.request('/bridge/result', {id: request.id, status, body});
      }
    } catch (error) {
      this.session = null;
      this.button.textContent = 'Connect CLI';
      this.module.onEditorError(String(error));
    } finally {
      this.running = false;
      if (this.session) this.timer = setTimeout(() => this.poll(), 200);
    }
  }

  async disconnect() {
    clearTimeout(this.timer);
    if (!this.session) return;
    // Retain the session on refusal so an in-flight request can finish.
    await this.request('/bridge/disconnect', {});
    this.session = null;
    this.button.textContent = 'Connect CLI';
  }
}

window.editorCliBridge = new EditorCliBridge(Module, document.getElementById('cli'));
