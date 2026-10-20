# WOCC ordinary IBL workload /1

Status: scoped Vulkan/Metal/WebGPU actual-size IBL Platform Validated with frozen
identical inputs and424 bitwise equal outputs at their recorded epochs.
Traditional parity and the common production engine gate remain open. Browser
continuation evidence: `build/bindless-webgpu-wocc-ibl1/current-report.json`.

This workload uses the existing OriginalRoot/2, upload staging/2, publication/5,
completed Graphics read/2 (read-plan17/pending7) and the same state journal and
real completion. It introduces no new L2 execution profile or backend authority.
The source of dimensions and binding roles is the current WOCC
`app/wocc_pbr_pipeline.cpp` IBL setup: two RGBA16F Cube64 mip7 horizon/zenith
bases, one RGBA16F 128x128 single-mip BRDF LUT and an independent linear sampler.
It reuses the same core IBL baker and horizon/zenith radiance functions.

Both selected stages consume the same original Cube/sampler closure. Fragment
samples every face and mip and fractional LOD2.5, plus both BRDF coefficient
channels at four exact texel centers. Vertex samples both Cube bases. The
original result buffer holds106 words per pixel,424 numerical lanes for2x2;
clean/fault, genuine completion and retirement remain mandatory.

Upload shapes, mip byte offsets and pixel bytes come from the baked metadata.
The shared normative upload mapper computes packed bytes; the device queries
native allocation and staging requirements. CPU byte stores are bounded by
authored pixel/packed payload plus the existing metadata ceiling. One-less
pixel/packing budgets must reject before allocation/publication; exact budgets
must succeed without changing any native query or test deadline.

| Backend | Current mapping for this workload |
| --- | --- |
| Null | Existing validator/refusal contracts; no executable payload |
| Vulkan | Existing native HDR profile, subject to real queried limits/format/allocation and public Graphics execution checks |
| Metal | Existing ordinary HDR profile and original staging/journal/completion; actual-size reference/clean/fault IBL passes on Mac;424 same-input words match Vulkan/WebGPU bitwise at recorded epochs |
| WebGPU | Existing ordinary HDR Preparation11/Execution13 with genuine canonical uploads, independent CPU domains and original callbacks; actual Chrome validates22 clean/fault lifecycle flows, five424-word readbacks and retirement |

The first strict CPU filtering check is preserved in `execution4.log`: one
filtered lane was17.9589844 on GPU versus17.9587402 from FP32 averaging of the
baked half values. This does not establish a general permitted error and no
tolerance was enlarged. A separate direct-bound Slang sampler reference writes
the424 values to ordinary set0/binding4. It still executes through the public
Root, with a receipt-only indexed marker; it is not a Traditional execution
path and does not close Traditional scene parity.

The replay authenticates this reference's selected Vertex/Fragment binaries,
bindings, UBO, all uploaded face/mip bytes, RGBA16F shapes, color and fault0
before consuming its output. All80 unfiltered lanes (single-texel Cube mip6
and exact LUT texel centers) must match the independently computed CPU values
exactly. The remaining analytical CPU differences are recorded, not accepted as
CPU parity. Both main clean/fault draws must match all424 reference words
bit-for-bit. Main fault0/1 and zero validation messages remain mandatory.
Cold export is test evidence preparation; no file IO enters runtime recording
or submission. The replay rejects missing/duplicate/out-of-order reference
draws, wrong binaries/bindings/inputs and nonfinite or corrupted output.

Shared fixture CPU owners retain an explicit65536-byte metadata allowance plus
the largest262128-byte upload; pixel snapshot, destination lease and upload
Source share the same original quota owner. Packing uses normative byte counts
plus its explicit metadata allowance. Graphics observers consume the original
canonical inventory's authored Graph budget, rather than a separate fixed64K
quota. Measured main content CPU peak is53464 bytes; increasing a declared
ceiling is not an allocation measurement or a complete in-flight budget proof.

Evidence: `build/bindless-wocc-ibl-capacity1/current-report.json` records the
affected input epoch, package/executable hashes, registered regression,
Native/Emulated shader contracts, SPIR-V12/12, hostile oracle tests and independent
RenderDoc capture/replay. Earlier quota/capacity/CPU-analytical failures remain
in the same directory. Scoped seals are historical once affected sources change.

This closes only IBL resource capacity and sampling for this workload after
validation. It does not prove full WOCC lighting equivalence, complete retained
bindings/in-flight device budgets, ordinary depth/comparison, three-backend
production acceptance, renderer migration or Release performance.

## Shared workload and scoped Metal validation (2026-10-19)

The original Vulkan bake, shape/face/mip upload declarations and unfiltered
analytical oracle now live in `OriginalGraphicsWoccIblDataFixture`. The Vulkan
runner consumes that same data without changing its original execution path.
The existing Metal upload fixture accepts authored mip metadata, sizes packing
through the normative mapper and derives source/pixel/packed ceilings from the
declared payload plus metadata budget. Exact and one-less pixel/packing cases
remain mandatory; the native allocation/staging query is unchanged.

The registered Metal workload uses the existing public Root2 ordinary HDR
Graphics path: one independent direct-bound reference, then clean/fault
indexed-result executions, all with genuine completion and retirement. It
requires80 unfiltered CPU-exact values,424 reference-matching words, exact
color and fault0/1. Its reference still uses a Root receipt marker; it is not
a Traditional execution path. Genuine Mac15/15, direct-reference/clean/fault
readback and native capture pass; Apple9243 sources/230 artifacts match.
Optional allocation observation1 preserves strict legacy refusal while holding
and shrinking the original reservation. CPU token construction failure cannot
leave ledger bytes/count/peak changed. Windows8/8, codegen/oracle3/3,
Node2/Chrome4, source422/style6 and formatting23 pass; fresh Vulkan replay
checks424 lanes/655328 uploaded bytes/validation0. Evidence:
`build/bindless-wocc-ibl-shared-metal1/current-report.json`.

Independent Mac/Windows bake exports match horizon but differ in zenith/LUT.
Shared code/shape metadata therefore does not prove identical baked inputs or
cross-backend numerical parity. A subsequent increment must consume an explicit
common frozen asset set and preserve each platform's differing exports as
historical evidence; do not widen numerical tolerances or alter the baker to
hide platform arithmetic. Package-layout construction dispatch also remains an
open integrated architecture check. Full production budgets, Traditional,
WebGPU and the common engine gate remain unpassed.

## Frozen common Native inputs (2026-10-19)

The historical platform bake mismatch is closed for this validation workload
by explicitly loading the same versioned frozen assets, not changing the baker
or accepting different resource bytes. The cold loader and strict metadata/hash
checks are specified in `bindless-wocc-ibl-frozen-inputs-contract.md`.
Independent completed Metal numeric exports and Vulkan replay match all424
words, with80 CPU anchors each, exact color/fault and no new tolerance. Windows10,
Mac17, Node3/Chrome4 plus Wasm device matrix, source422/style6/layout4 and
formatting28 pass. Fresh captures and precise source/artifact hashes are recorded
in `build/bindless-wocc-ibl-common-inputs1/current-report.json`. The earlier
per-platform failure/export reports retain their own historical scopes.
