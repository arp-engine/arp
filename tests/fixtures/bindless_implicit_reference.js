// Test-only independent Traditional oracle. It grants no RHI execution capability.
// The Root pixels below come from public C++ prepare/record/submit/completion.
async function runBindlessImplicitReference(progress, array, material = false, terrain = false) {
  const require = (ok, id) => { if (!ok) throw new Error(id); };
  const numeric = material || terrain;
  const textureCount = terrain ? 27 : material ? 10 : 2;
  const samplerCount = terrain ? 1 : 2;
  const ordinaryBytes = terrain ? 96 : 288;
  const resultBinding = textureCount + samplerCount;
  const name = terrain ? 'ordinary_terrain27' : material ? 'ordinary_pbr_material' : array ? 'ordinary_implicit_array' : 'ordinary_implicit_2d';
  const folder = 'bindless-implicit-reference/' + name + '/';
  const response = await fetch(folder + name + '.wgsl');
  require(response.ok, 'IMPLICIT-REFERENCE-SHADER-FETCH');
  const code = await response.text();
  require((code.match(/\btextureSample\s*\(/g) || []).length === (terrain ? 27 : material ? 12 : 2)
    && !/textureSampleGrad|diagnostic\s*\(\s*off/.test(code)
    && (!material || (/\bdiscard\s*;/.test(code) && !/\btextureSampleLevel\s*\(/.test(code))), 'IMPLICIT-REFERENCE-DERIVATIVES');
  const hash = async text => Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', new TextEncoder().encode(text))))
    .map(byte => byte.toString(16).padStart(2, '0')).join('');
  const sha = await hash(code);
  let inputs, inputSha;
  if (numeric) {
    const inputResponse = await fetch(folder + 'inputs.json');
    require(inputResponse.ok, 'PBR-REFERENCE-INPUT-FETCH');
    const text = await inputResponse.text();
    inputs = JSON.parse(text); inputSha = await hash(text);
    require(inputs.type === (terrain ? 'Terrain27ImplicitReferenceInputs' : 'PbrImplicitReferenceInputs') && inputs.contractVersion === (terrain ? 1 : 2)
      && inputs.ordinary.length === 2 && inputs.ordinary[0].length === ordinaryBytes && inputs.ordinary[1].length === 16
      && inputs.textureMips.length === textureCount && inputs.expected.length === 128 && inputs.pixels.length === 16,
      'PBR-REFERENCE-INPUT');
  }
  const adapter = await navigator.gpu.requestAdapter();
  require(adapter, 'IMPLICIT-REFERENCE-ADAPTER');
  const requiredLimits = terrain ? {maxSampledTexturesPerShaderStage: textureCount, maxBindingsPerBindGroup: resultBinding + 1} : {};
  require(Object.entries(requiredLimits).every(([name, value]) => adapter.limits[name] >= value), 'IMPLICIT-REFERENCE-ADAPTER-LIMITS');
  const device = await adapter.requestDevice({requiredLimits});
  const errors = [], resources = [];
  device.addEventListener('uncapturederror', event => errors.push(String(event.error.message)));
  for (const filter of ['out-of-memory', 'internal', 'validation']) device.pushErrorScope(filter);
  let readback;
  try {
    require(device.limits.maxSampledTexturesPerShaderStage >= textureCount
      && device.limits.maxSamplersPerShaderStage >= samplerCount
      && (!numeric || (device.limits.maxBindGroups >= 2 && device.limits.maxStorageBuffersPerShaderStage >= 2
        && device.limits.maxStorageBufferBindingSize >= 512 && device.limits.maxUniformBufferBindingSize >= 16)),
      'IMPLICIT-REFERENCE-DEVICE-LIMITS');
    const layerCount = index => terrain ? (index >= 25 ? 2 : 1) : array ? 2 : 1;
    const viewDimension = index => layerCount(index) === 2 ? '2d-array' : '2d';
    const textures = Array.from({length: textureCount}, (_, index) => {
      const layers = layerCount(index);
      const texture = device.createTexture({label: 'independent implicit reference ' + index,
        size: [2, 2, layers], format: 'rgba8unorm', dimension: '2d', mipLevelCount: 2,
        sampleCount: 1, usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST});
      resources.push(texture);
      // Same documented upload input as the Root, with mip-major layer packing.
      for (let mip = 0; mip < 2; ++mip) {
        const extent = mip ? 1 : 2, length = extent * extent * 4 * layers;
        const offset = mip ? 16 * layers : 0;
        const bytes = numeric ? Uint8Array.from(inputs.textureMips[index][mip])
          : Uint8Array.from({length}, (_, n) => index * 32 + offset + n + 1);
        require(bytes.length === length, 'IMPLICIT-REFERENCE-TEXTURE-SIZE');
        device.queue.writeTexture({texture, mipLevel: mip, origin: [0, 0, 0]}, bytes,
          {offset: 0, bytesPerRow: extent * 4, rowsPerImage: extent}, [extent, extent, layers]);
      }
      return texture;
    });
    const samplers = (terrain ? ['nearest'] : ['nearest', 'linear']).map(filter => device.createSampler({minFilter: filter,
      magFilter: filter, mipmapFilter: 'linear', addressModeU: 'clamp-to-edge', addressModeV: 'clamp-to-edge',
      addressModeW: 'clamp-to-edge', lodMinClamp: 0, lodMaxClamp: 32, maxAnisotropy: 1}));
    const makeBuffer = (bytes, usage) => {
      const buffer = device.createBuffer({size: bytes.length, usage});
      resources.push(buffer); device.queue.writeBuffer(buffer, 0, Uint8Array.from(bytes));
      return buffer;
    };
    const resultBuffer = numeric ? makeBuffer(Array(512).fill(0xcd), GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST | GPUBufferUsage.COPY_SRC) : null;
    const ordinaryLayout = numeric ? device.createBindGroupLayout({entries: [
      {binding: 0, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, buffer: {type: 'read-only-storage', minBindingSize: ordinaryBytes}},
      {binding: 1, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, buffer: {type: 'uniform', minBindingSize: 16}}]}) : null;
    const ordinaryGroup = numeric ? device.createBindGroup({layout: ordinaryLayout, entries: [
      {binding: 0, resource: {buffer: makeBuffer(inputs.ordinary[0], GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST), offset: 0, size: ordinaryBytes}},
      {binding: 1, resource: {buffer: makeBuffer(inputs.ordinary[1], GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST), offset: 0, size: 16}}]}) : null;
    const layout = device.createBindGroupLayout({entries: [
      ...textures.map((_, binding) => ({binding, visibility: GPUShaderStage.FRAGMENT,
        texture: {sampleType: 'float', viewDimension: viewDimension(binding), multisampled: false}})),
      ...Array.from({length: samplerCount}, (_, i) => textureCount + i).map(binding => ({binding, visibility: GPUShaderStage.FRAGMENT, sampler: {type: 'filtering'}})),
      ...(numeric ? [{binding: resultBinding, visibility: GPUShaderStage.FRAGMENT, buffer: {type: 'storage', minBindingSize: 512}}] : [])]});
    const module = device.createShaderModule({code});
    const info = await module.getCompilationInfo();
    require(!info.messages.some(message => message.type === 'error'), 'IMPLICIT-REFERENCE-SHADER-VALIDATION');
    const pipeline = await device.createRenderPipelineAsync({
      layout: device.createPipelineLayout({bindGroupLayouts: numeric ? [ordinaryLayout, layout] : [layout]}),
      vertex: {module, entryPoint: 'vertexMain', buffers: []},
      fragment: {module, entryPoint: 'fragmentMain', targets: [{format: 'rgba8unorm', writeMask: GPUColorWrite.ALL}]},
      primitive: {topology: 'triangle-list', frontFace: 'ccw', cullMode: 'none', unclippedDepth: false},
      multisample: {count: 1, mask: 0xffffffff, alphaToCoverageEnabled: false}});
    const group = device.createBindGroup({layout, entries: [
      ...textures.map((texture, binding) => ({binding, resource: texture.createView({format: 'rgba8unorm',
        dimension: viewDimension(binding), aspect: 'all', baseMipLevel: 0, mipLevelCount: 2, baseArrayLayer: 0, arrayLayerCount: layerCount(binding)})})),
      ...samplers.map((resource, index) => ({binding: index + textureCount, resource})),
      ...(numeric ? [{binding: resultBinding, resource: {buffer: resultBuffer, offset: 0, size: 512}}] : [])]});
    const target = device.createTexture({size: [2, 2, 1], dimension: '2d', format: 'rgba8unorm',
      mipLevelCount: 1, sampleCount: 1, usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC});
    resources.push(target);
    const readbackSize = numeric ? 1024 : 512;
    readback = device.createBuffer({size: readbackSize, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ});
    resources.push(readback);
    const encoder = device.createCommandEncoder();
    const pass = encoder.beginRenderPass({colorAttachments: [{view: target.createView(),
      loadOp: 'clear', storeOp: 'store', clearValue: {r: 0, g: 0, b: 0, a: 1}}]});
    pass.setPipeline(pipeline);
    if (numeric) pass.setBindGroup(0, ordinaryGroup);
    pass.setBindGroup(numeric ? 1 : 0, group);
    pass.setViewport(0, 0, 2, 2, 0, 1); pass.setScissorRect(0, 0, 2, 2); pass.draw(3, 1, 0, 0); pass.end();
    encoder.copyTextureToBuffer({texture: target}, {buffer: readback, bytesPerRow: 256, rowsPerImage: 2}, [2, 2, 1]);
    if (numeric) encoder.copyBufferToBuffer(resultBuffer, 0, readback, 512, 512);
    device.queue.submit([encoder.finish()]);
    await device.queue.onSubmittedWorkDone();
    await readback.mapAsync(GPUMapMode.READ, 0, readbackSize);
    const bytes = new Uint8Array(readback.getMappedRange());
    const pixels = [...bytes.slice(0, 8), ...bytes.slice(256, 264)];
    const values = numeric ? Array.from(new Float32Array(bytes.buffer, bytes.byteOffset + 512, 128)) : null;
    readback.unmap();
    if (numeric) {
      const close = (actual, expected) => actual.length === expected.length && actual.every((v, i) =>
        Number.isFinite(v) && Math.abs(v - expected[i]) <= 2e-5 * (1 + Math.abs(expected[i])));
      require(close(values, inputs.expected), 'PBR-REFERENCE-CPU-NUMERIC');
      require(pixels.every((v, i) => Math.abs(v - inputs.pixels[i]) <= 1), 'PBR-REFERENCE-DISCARD-PIXELS');
      const prefix = 'BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_' + (terrain ? 'TERRAIN27' : 'PBR') + '_PARITY_';
      const numeric = progress.filter(message => message.startsWith(prefix + 'VALUES fault0 '));
      const color = progress.filter(message => message.startsWith(prefix + 'PIXELS fault0 '));
      require(numeric.length === 2 && numeric.every(message => close(message.slice((prefix + 'VALUES fault0 ').length).split(' ').map(Number), values)),
        'PBR-REFERENCE-ROOT-NUMERIC');
      require(color.length === 2 && color.every(message => message === prefix + 'PIXELS fault0 ' + pixels.join(' ')),
        'PBR-REFERENCE-ROOT-PIXELS');
      if (!terrain) require([0,1,2,3].every(p => Math.abs(values[p*32+24] - values[p*32+25]) > .01), 'PBR-REFERENCE-FILTERS');
      if (terrain) {
        require(inputs.textureLayers.join(',') === Array.from({length: 27}, (_, i) => i < 25 ? 1 : 2).join(','), 'TERRAIN27-REFERENCE-LAYERS');
        require([0,1,2,3].every(p => Math.abs(values[p*32] - (255-inputs.textureMips[25][1][(p>>1)*4])/255) > .01), 'TERRAIN27-REFERENCE-MIP-CONTROL');
        return {type: 'BindlessTerrain27ImplicitSamplingReference', contractVersion: 1, success: true,
          shaderSha256: sha, inputSha256: inputSha, pixels, values,
          cleanRootReceipts: numeric.length, cleanPixelReceipts: color.length,
          realImplicitDerivatives: true, mipZeroSubstitutionRejected: true, arrayLayersValidated: true,
          publicExecutionGranted: false, nativeErrors: errors};
      }
      return {type: 'BindlessPbrImplicitSamplingReference', contractVersion: 2, success: true,
        shaderSha256: sha, inputSha256: inputSha, pixels, values,
        cleanRootReceipts: numeric.length, cleanPixelReceipts: color.length,
        realImplicitDerivatives: true, actualDiscardValidated: true, distinctFiltersValidated: true,
        publicExecutionGranted: false, nativeErrors: errors};
    }
    const prefix = 'BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_' + (array ? 'ARRAY' : 'SAMPLED') + '_PIXELS fault0 rgba ';
    const root = progress.filter(message => message.startsWith(prefix));
    require(root.length === 2 && root.every(message => message === prefix + pixels.join(' ')), 'IMPLICIT-REFERENCE-ROOT-PIXEL-MISMATCH');
    // At pixel (0,0), Image1 uses Nearest at UV (0.5,0.5). Mip0
    // addresses texel (1,1): index*32 + layer*16 + texel*4 + redByte.
    // Derive the counterexample from the actual uploaded input, not Load(0,0).
    const baseProbe = 32 + 0 * 16 + 3 * 4 + 1;
    const baseLayerProbe = 32 + 1 * 16 + 3 * 4 + 1;
    require(pixels[2] !== baseProbe && (!array || pixels[10] !== baseLayerProbe), 'IMPLICIT-REFERENCE-MIP-CONTROL');
    return {type: 'BindlessImplicitSamplingReference', contractVersion: 1, success: true,
      dimension: array ? 'Texture2DArray' : 'Texture2D', shaderSha256: sha, pixels,
      cleanRootReceipts: root.length, realImplicitDerivatives: true, mipZeroSubstitutionRejected: true,
      publicExecutionGranted: false, nativeErrors: errors};
  } finally {
    if (readback && readback.mapState === 'mapped') readback.unmap();
    for (let i = 0; i < 3; ++i) { const error = await device.popErrorScope(); if (error) errors.push(String(error.message)); }
    for (const resource of resources) resource.destroy();
    device.destroy();
    require(errors.length === 0, 'IMPLICIT-REFERENCE-NATIVE-ERROR: ' + errors.join('; '));
  }
}
