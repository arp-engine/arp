// Web Crypto rejects views backed by resizable Wasm memories in Chromium.
// Keep the WASI random_get contract while filling through a fixed buffer.
addToLibrary({
  random_get__deps: [],
  random_get: function(buffer, size) {
    var destination = HEAPU8.subarray(buffer, buffer + size);
    if (typeof ENVIRONMENT_IS_NODE !== 'undefined' && ENVIRONMENT_IS_NODE) {
      require('node:crypto').randomFillSync(destination);
      return 0;
    }
    var offset = 0;
    while (offset < size) {
      var count = Math.min(size - offset, 65536);
      var temporary = new Uint8Array(count);
      if (typeof crypto !== 'undefined' && crypto.getRandomValues) {
        crypto.getRandomValues(temporary);
      } else if (typeof require === 'function') {
        require('node:crypto').randomFillSync(temporary);
      } else {
        return 1;
      }
      destination.set(temporary, offset);
      offset += count;
    }
    return 0;
  },
});
