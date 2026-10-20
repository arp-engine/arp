# Descriptor SPIR-V buffer layout facts /1

Status: Contract Complete / Prototype Complete for c2b4f2a tooling declaration
facts. Not Backend Validated / Not Platform Validated / Not Production Ready.
15 binary tests, 32 real Slang publications and 255 hostile YAML cases pass;
host22/22, final quick/Null smoke3/3, Wasm/browser8/8 and inventory204/204 pass.
Evidence: build/web-bindless-apple-debug/artifacts/bindless-continuation/
buffer-layout-facts-validation.yaml. Immutable original footprint custody,
c2b4f2, target minimum binding sizes and native slices remain open.

The existing SpirvLayoutFacts binary reader is the reusable layout boundary.
Extend it to read explicitly decorated buffer variables, pointer/block/type
relationships, descriptor cardinality, member offsets, fixed array strides and
runtime-array strides. Accepted types are scalar integers/floats, vectors,
explicit ColMajor matrices, fixed arrays and structs. Runtime arrays must be the
last direct member of the root storage block. Unknown types, missing/duplicate
layout decorations or definitions, specialization-sized arrays, cyclic types,
overlap, overflow or nested/runtime descriptor arrays fail with stable diagnostic
labels. There is no source-name, source-regex or remaining-tail inference.

Use an iterative dependency walk, with input-defined type/dependency bounds,
instead of recursive parsing. Metadata conversion is tooling-only and may use
Python containers. Runtime allocation/owner/root/seal/journal contracts do not
change. Reuse the existing SPIRV-Reflect invocation; parse its YAML aliases with
the YAML loader and require exact module descriptor membership, keys,
cardinality, storage class and root block type agreement with the binary.
Missing aliases or count mismatches must fail rather than skip a member.

Per-binding facts separately record occupied static extent, runtime-array
offset/stride/element extent and SPIRV-Reflect block size/padded size. The static
extent is the end of the last occupied byte, excluding inferred tail padding;
for a runtime tail it measures only the occupied prefix fields. The explicit
tail offset remains separate, including any gap before the tail; no element
count is invented. All values fit uint64. Descriptor count is independent of element count.
A runtime tail has an explicit positive stride and positive element extent.

These facts prove a selected SPIR-V declaration layout only. They prove neither
execution access coverage nor target minimum binding size, WGSL/MSL equivalent
layout, buffer-length behavior, dynamic-offset legality or native execution.
Null is ValidatorOnly. Vulkan can consume the SPIR-V facts only after original
custody and approved lowering exist. Metal/WebGPU require their own equivalent
layout/minimum/length proofs. All four version2 write gates remain closed.
Do not serialize an Available native footprint or open capability from this
tooling stage. f2b must carry verified facts in immutable original artifacts;
f2c must join queried original limits and target-specific footprint proofs.

Verification requires synthetic binary negative cases and real existing Slang
VS/FS/Compute publications, including scalar uniform padding, runtime storage
stride and descriptor-array cardinality. Preserve a quick test and Null
hidden-frame smoke. No shader source or visual output change; unavailable
RenderDoc is recorded as RHI-RENDERDOC-TOOL-UNAVAILABLE.
