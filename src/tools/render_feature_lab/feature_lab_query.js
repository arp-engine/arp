(function () {
  if (typeof URLSearchParams === 'undefined' || typeof location === 'undefined') return;
  var query = new URLSearchParams(location.search);
  var args = Module['arguments'] || [];
  ['case', 'backend', 'extent', 'frames', 'capture', 'output', 'ddgi-parity-mode'].forEach(function (name) {
    if (query.has(name)) args.push('--' + name, query.get(name));
  });
  ['hidden', 'list-cases', 'update-golden'].forEach(function (name) {
    if (query.get(name) === '1' || query.get(name) === 'true') args.push('--' + name);
  });
  Module['arguments'] = args;
})();
