// Minimal browser fixture bootstrap for the ARP WebGPU device contract.
// Device acquisition is the only browser-owned responsibility here; RHI
// capability promotion and lowering remain in the backend/runtime probe.
(function () {
  Module.webGpuTimestampQuery = false;
  Module.webGpuRgba16FloatStorage = false;
  Module.webGpuRg16FloatStorage = false;
  Module.preRun = Module.preRun || [];
  Module.preRun.push(function () {
    var dependency = "arp-webgpu-device-contract-bootstrap";
    addRunDependency(dependency);
    var dependencyOutstanding = true;
    function finishBootstrap() {
      dependencyOutstanding = false;
      removeRunDependency(dependency);
    }
    if (!navigator.gpu) {
      console.error("[arp-rhi-contract] navigator.gpu is unavailable");
      finishBootstrap();
      return;
    }
    function acceptDevice(device) {
      Module.webGpuUncapturedErrors = [];
      Module.webGpuNativeGroupCreateCount = 0;
      var originalCreateBindGroup = device.createBindGroup;
      device.createBindGroup = function (descriptor) {
        Module.webGpuNativeGroupCreateCount++;
        return originalCreateBindGroup.call(this, descriptor);
      };
      device.addEventListener("uncapturederror", function (event) {
        var message = String(event.error && event.error.message || event.error || "unknown WebGPU error");
        Module.webGpuUncapturedErrors.push(message);
        console.error("[arp-rhi-contract] uncaptured WebGPU error: " + message);
      });
      Module.preinitializedWebGPUDevice = device;
      Module.webGpuTimestampQuery = !!(device.features
        && typeof device.features.has === "function"
        && device.features.has("timestamp-query"));

      // WebGPU exposes storage-texture binding as a separate capability from
      // the per-format storage usage contract. Probe optional float formats
      // behind validation error scopes before the RHI promotes them. This
      // keeps unsupported formats fail-closed without emitting uncaptured
      // validation errors into the browser console.
      function probeStorageFormat(format, key) {
        return arpProbeWebGpuStorageFormat(device, format).then(function (supported) {
          Module[key] = supported;
        });
      }
      return Promise.all([
        probeStorageFormat("rgba16float", "webGpuRgba16FloatStorage"),
        probeStorageFormat("rg16float", "webGpuRg16FloatStorage")
      ]);
    }
    navigator.gpu.requestAdapter().then(function (adapter) {
      if (!adapter) throw new Error("WebGPU adapter is unavailable");
      // DDGI's descriptor ABI has thirteen compute-stage storage buffers.
      // Request the higher optional limit when the adapter advertises it;
      // otherwise the device remains valid but the workload fails closed.
      var requiredLimits = {};
      var requiredFeatures = [];
      if (adapter.features
        && typeof adapter.features.has === "function"
        && adapter.features.has("timestamp-query")) {
        requiredFeatures.push("timestamp-query");
      }
      if (adapter.features
        && typeof adapter.features.has === "function"
        && adapter.features.has("indirect-first-instance")
        && !(typeof location !== "undefined"
          && new URLSearchParams(location.search).has("disable-indirect-first-instance"))) {
        requiredFeatures.push("indirect-first-instance");
      }
      if (adapter.limits
        && adapter.limits.maxStorageBuffersPerShaderStage >= 16) {
        requiredLimits.maxStorageBuffersPerShaderStage = 16;
      }
      Module.arpWebGpuAdapter = adapter;
      Module.arpRequestPlannedWebGpuDevice = function (plannedLimits) {
        if (Module.preinitializedWebGPUDevice)
          throw new Error("Planned fixture device must precede all RHI work");
        return adapter.requestDevice({requiredFeatures: requiredFeatures,
          requiredLimits: plannedLimits}).then(acceptDevice);
      };
      // Terrain loads its authenticated package and runs the C++ planner in
      // main before any GPUDevice exists. Other cohorts retain their bootstrap.
      if (typeof location !== "undefined"
        && new URLSearchParams(location.search).has("original-root-terrain27")) {
        finishBootstrap();
        return null;
      }
      return adapter.requestDevice({
        requiredFeatures: requiredFeatures,
        requiredLimits: requiredLimits
      });
    }).then(function (device) {
      if (!device) return;
      return acceptDevice(device).then(finishBootstrap);
    }).catch(function (error) {
      // Removing the last dependency can enter main synchronously. Preserve
      // its exception rather than removing an already consumed dependency.
      if (!dependencyOutstanding) throw error;
      console.error("[arp-rhi-contract] device bootstrap failed: " + error);
      finishBootstrap();
    });
  });
}());
