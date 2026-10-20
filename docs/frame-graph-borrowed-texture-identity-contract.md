# FrameGraphBorrowedTextureIdentity/1

L2 invariant: repeated shared imports of the same GpuTexture object into one
FrameGraph return one texture handle. The graph owns one shared lifetime lease
and one resource hazard/state chain. Reimport is not a declaration of a new
physical texture, discard, or independent initial state. Distinct objects with
equal descriptors remain distinct. Null imports keep their existing diagnostic.

Null, Vulkan, Metal and WebGPU all use this Equivalent graph-level identity
lowering before native barriers; backend implementations remain unchanged.
This permits sequential static-visibility and full-scene SSR depth-pyramid
updates without inventing two Undefined initial states for one native texture.
No native handles, additional allocations, or per-frame identity cache is used.

Fast regression asserts duplicate identity, distinct-object independence and
one retained ownership reference. Native smoke enables both vegetation Hi-Z
and SSR in WoCC with Metal validation. Other native backends remain NOT-RUN.
