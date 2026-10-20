(function () {
  Module.printErr = function (...args) {
    const message = args.join(" ");
    console.error(message);
    const output = document.getElementById("output");
    if (output) output.value += message + "\n";
  };
  Module.onAbort = function (reason) {
    const status = document.getElementById("status");
    if (status) status.textContent = "Editor startup failed: " + reason;
  };
  window.addEventListener("error", function (event) {
    const status = document.getElementById("status");
    if (!status || !event.message) return;
    status.textContent = "Editor startup failed: " + event.message;
  });
}());
