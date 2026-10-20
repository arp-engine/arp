# SnapshotMetadata/1

Status: metadata ABI Contract Complete; Null metadata publication and isolated
Native/Emulated shader consumers Prototype Complete. GPU extension issuance
remains closed. This does not promote whole BindlessCross readiness.

SnapshotActivePrefix/2 removes caller-authored active/element counts from helper
arguments. A compiler-reserved read-only structured uint buffer supplies all
bounds. Its binding, total word count, stage mask and table offsets are in the
sealed lowering and plan identity; it counts against actual storage-buffer and
descriptor limits. YAML declares its physical binding explicitly.

The buffer uses 32-bit words. Header words are [version=1, tableCount,
generationLow, generationHigh]. Each table starts with [tableId, activeCount,
capacity, reserved=0], followed by capacity element counts. Only buffer table
slots have element counts; inactive slots and other resource kinds contain zero.
Offsets follow package table order, starting at word 4, with no implicit padding.
The metadata is built from validated typed writes at seal and is immutable.
Applications cannot replace the reserved descriptor or supply shader bounds.
Snapshot metadata generation is nonzero and shares publication/retirement with
the descriptor/material/instance generation. The backend owns upload and storage.

Helpers validate the metadata version/table identity/capacity and active index
before accessing slot metadata or a resource. Buffer operations then compare
the element index against the snapshot-derived count. Sampler bounds come from
the independently declared sampler table. Invalid operations set the existing
fault result and do not access the selected resource. Mandatory error collection
and GPU validation-frame completion are separate execution gates still required;
returning a bool alone never authorizes production execution.

Null validates CPU metadata, the complete buffer-write request and reserved
descriptors without claiming GPU-visible upload. Seal requires the owning
device and rejects a foreign one. Metadata preparation uses `CpuToGpu` storage
with `StorageBuffer | CopyDst` usage; the latter is required by WebGPU's explicit
queue-write contract. All four `GpuDevice::writeBuffer` implementations reject
the metadata and fault buffers after seal with
`RHI-BUFFER-HOST-WRITES-SEALED`. After publication, metadata also carries a
GPU-write seal. Null, Vulkan, Metal and WebGPU reject buffer-copy,
texture-to-buffer and staging-upload destinations and writable storage-buffer
descriptor aliases with `RHI-BUFFER-GPU-WRITES-SEALED`. Registries hold a weak
seal token instead of a buffer pointer, so resource destruction cannot leave a
dangling authorization reference. The fault latch rejects generic transfer
writes while remaining writable through its leased clear and shader fault
operations; host writes to it remain sealed.
Candidate metadata and fault-latch IDs are not exposed through public access
getters before seal. Descriptor consumption also revalidates the seal, so a
previously recorded writable alias cannot become valid after publication.
These checks cover public RHI recording paths, not backend-private commands.
Snapshot creation does not host-initialize the `GpuOnly` fault latch; only a
leased GPU clear can establish its initial value for an executable submission.
Native upload visibility remains an execution gate.
Vulkan/Metal Native and WebGPU scalar Emulated consume identical word layouts;
their respective descriptor/argument-buffer/scalar bindings remain backend-owned.
No Metal GetDimensions for structured buffers, browser extensions, guessed range,
frame-loop compilation or in-place metadata updates are allowed.

Evidence: five focused assertion-enabled MinGW CTests pass (bindless contract,
compiler/runtime package validation, Vulkan execution, command contract, Null
hidden smoke). WebGPU/Emscripten RHI and graph build; Metal Windows stub builds.
Vulkan full/partial reference metadata executes on RTX 3050, browser full/partial
and generation-zero cases execute through actual device limits. Captured shaders
match the current sealed SPIR-V hashes and exact four-pixel sampling reference:
D:/arp/build/win-vulkan-debug/artifacts/bindless-vulkan/native_metadata_capture.rdc.
Capture replay has zero validation messages. Browser report:
D:/arp/build/artifacts/bindless-webgpu/report.json.

This evidence is backend-private or standalone browser execution, not a public
native snapshot, macOS validation, RHI WebGPU integration or WOCC acceptance.
GPU completion accounting, enforced fault collection and material/instance
same-generation validation remain required before public execution is enabled.
