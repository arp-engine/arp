// Query one actual device under a validation scope. This is capability evidence,
// never an execution grant. Call before publishing the device to the RHI.
async function arpProbeWebGpuStorageFormat(device, format) {
  if (typeof GPUTextureUsage === "undefined") return false;
  let texture = null;
  let valid = false;
  device.pushErrorScope("validation");
  try {
    texture = device.createTexture({
      size: [1, 1, 1], dimension: "2d", format: format,
      mipLevelCount: 1, sampleCount: 1, usage: GPUTextureUsage.STORAGE_BINDING
    });
    valid = !!texture;
  } catch (_) {
    valid = false;
  }
  try {
    return !(await device.popErrorScope()) && valid;
  } catch (_) {
    return false;
  } finally {
    if (texture) texture.destroy();
  }
}
