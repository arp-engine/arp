const assert = require('node:assert/strict');
const Transport = require('../../scripts/browser_progress_transport.js');
const limits = {contractVersion: 1, batchMessages: 3, maxMessages: 512,
  maxMessageCodeUnits: 32, maxCodeUnits: 8192};

async function verify() {
  let active = 0, peak = 0, accepted = 0;
  const received = [], batches = [], failures = [], records = [];
  const send = async batch => {
    active++; peak = Math.max(peak, active);
    await new Promise(resolve => setTimeout(resolve, 1));
    assert.equal(batch.first, accepted);
    assert.ok(batch.messages.length > 0 && batch.messages.length <= limits.batchMessages);
    received.push(...batch.messages); accepted += batch.messages.length; batches.push(batch);
    active--;
    return {ok: true, json: async () => ({contractVersion: 1, acceptedThrough: accepted})};
  };
  const transport = new Transport(records, limits, send, error => failures.push(error));
  for (let i = 0; i < 120; i++) transport.append('record' + i);
  const draining = transport.flush();
  assert.equal(transport.flush(), draining);
  for (let i = 120; i < 240; i++) transport.append('record' + i);
  await draining;
  assert.equal(peak, 1);
  assert.deepEqual(received, records);
  assert.equal(received.length, 240);
  assert.equal(batches.length, 80);
  assert.equal(failures.length, 0);

  for (const reply of [{ok: false}, {ok: true, json: async () => ({contractVersion: 1, acceptedThrough: 99})}]) {
    const events = [];
    const bad = new Transport([], limits, async () => reply, error => events.push(error));
    bad.append('x');
    await assert.rejects(bad.flush(), /BROWSER-PROGRESS-(HTTP|RECEIPT)/);
    await new Promise(resolve => setTimeout(resolve, 5));
    assert.equal(events.length, 1);
    assert.equal(bad.sent, 0);
    await assert.rejects(bad.flush());
  }
  for (const constrained of [
    {...limits, maxMessages: 3}, {...limits, maxMessageCodeUnits: 1}, {...limits, maxCodeUnits: 2}]) {
    const events = [], stored = [];
    const bad = new Transport(stored, constrained, async () => {throw new Error('unexpected send');}, e => events.push(e));
    for (let i = 0; i < 4; i++) bad.append('xx');
    assert.equal(events.length, 1);
    assert.match(String(events[0]), /BROWSER-PROGRESS-CAPACITY/);
    await assert.rejects(bad.flush());
  }
  assert.throws(() => new Transport([], {...limits, batchMessages: 0}, send, () => {}), /BROWSER-PROGRESS-LIMITS/);
  console.log('BROWSER_PROGRESS_TRANSPORT_OK one-inflight ordered-batches complete-drain bounded-records errors-preserved');
}
verify().catch(error => {console.error(error); process.exitCode = 1;});
