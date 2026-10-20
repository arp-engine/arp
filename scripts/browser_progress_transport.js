/* ProgressBatch/1: tooling transport; no GPU execution or test-result authority. */
class BrowserProgressTransport {
  constructor(records, limits, send, onFailure) {
    if (limits.contractVersion !== 1 || !Number.isSafeInteger(limits.batchMessages)
        || limits.batchMessages <= 0 || limits.batchMessages > limits.maxMessages
        || !Number.isSafeInteger(limits.maxMessages) || limits.maxMessages <= 0
        || !Number.isSafeInteger(limits.maxMessageCodeUnits) || limits.maxMessageCodeUnits <= 0
        || !Number.isSafeInteger(limits.maxCodeUnits) || limits.maxCodeUnits <= 0)
      throw new Error('BROWSER-PROGRESS-LIMITS');
    this.records = records;
    this.limits = limits;
    this.send = send;
    this.onFailure = onFailure;
    this.sent = 0;
    this.codeUnits = 0;
    this.running = null;
    this.scheduled = false;
    this.failure = null;
  }
  append(message) {
    if (this.failure) return;
    if (typeof message !== 'string' || this.records.length >= this.limits.maxMessages
        || message.length > this.limits.maxMessageCodeUnits
        || message.length > this.limits.maxCodeUnits - this.codeUnits) {
      this.fail(new Error('BROWSER-PROGRESS-CAPACITY'));
      return;
    }
    this.records.push(message);
    this.codeUnits += message.length;
    if (this.running || this.scheduled) return;
    this.scheduled = true;
    setTimeout(() => {
      this.scheduled = false;
      this.flush().catch(error => this.fail(error));
    }, 0);
  }
  fail(error) {
    if (this.failure) return;
    this.failure = error;
    this.onFailure(error);
  }
  flush() {
    if (this.failure) return Promise.reject(this.failure);
    if (this.running) return this.running;
    const pump = async () => {
      while (this.sent < this.records.length) {
        const first = this.sent;
        const messages = this.records.slice(first, first + this.limits.batchMessages);
        const response = await this.send({contractVersion: 1, first, messages});
        if (!response.ok) throw new Error('BROWSER-PROGRESS-HTTP');
        const receipt = await response.json();
        if (receipt.contractVersion !== 1 || receipt.acceptedThrough !== first + messages.length)
          throw new Error('BROWSER-PROGRESS-RECEIPT');
        this.sent += messages.length;
      }
    };
    this.running = pump().finally(() => { this.running = null; });
    return this.running;
  }
}
if (typeof module !== 'undefined' && module.exports) module.exports = BrowserProgressTransport;
