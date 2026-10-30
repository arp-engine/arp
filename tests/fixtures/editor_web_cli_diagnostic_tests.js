/* Message-only fixture; it grants no engine or GPU execution evidence. */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

(async () => {
  const errors = [];
  let requests = 0;
  const original = 'RHI-WEBGL1-CONTEXT-LOST';
  const button = {textContent: '', addEventListener() {}};
  const module = {
    editorState: {frames: 3}, editorError: original,
    onEditorError: message => errors.push(message),
    ccall() {throw Error('Stopped editor must not execute a request');}
  };
  const context = {
    Module: module, location: {search: ''}, URLSearchParams,
    window: {addEventListener() {}},
    document: {getElementById: () => button},
    setTimeout() {throw Error('Failed bridge must not automatically reconnect');},
    clearTimeout() {},
    fetch() {++requests; throw Error('Stopped editor must not fetch a request');}
  };
  vm.runInNewContext(fs.readFileSync(process.argv[2], 'utf8'), context);
  const bridge = context.window.editorCliBridge;
  bridge.session = 'test-session';
  await bridge.poll();
  assert.equal(bridge.session, null);
  assert.equal(bridge.running, false);
  assert.equal(requests, 0);
  assert.equal(module.editorError, original);
  assert.match(errors.at(-1), /RHI-WEBGL1-CONTEXT-LOST/);
  assert.match(errors.at(-1), /CLI request was not executed/);
  await bridge.toggle();
  assert.equal(requests, 0);
  assert.match(errors.at(-1), /CLI unavailable: RHI-WEBGL1-CONTEXT-LOST/);
  assert.equal(module.editorError, original);
  console.log('PASS original editor diagnostic retained; stopped requests fail closed');
})().catch(error => {console.error(error); process.exitCode = 1;});
