# Production skinning buffer execution

Status: Vulkan/Metal/WebGPU Backend/Platform Validated for the authored palette
position arithmetic scope below. This is an additional workload
under the existing original static Graphics buffer/range/member-memory and
execution contracts. It adds no capability, executor, resource identity or mode.

The buffer mirrors WOCC's temporal palette: two renderer::GpuSkinningPalette
values, each with 256 row-memory matrices, translation/scale, quaternion and
joint count/padding. The exact occupied sizes are 16432 and 32864 bytes. A second
32-byte buffer uses renderer::GpuSkinningInfluenceVertex. Indices come from that
uploaded buffer, including 128, 219 and 255, rather than constant shader indices.
Weights, independent previous/current counts, normalization and object transforms
follow the current WOCC skinning operations. Slang uses default matrix fields
and the normal explicit row-major compiler option.

The shared CPU oracle uses the real renderer structs and core math. Poisoned
output, exact numeric readback, clean/fault, queried ordinary-buffer limits,
original completion and zero-residual retirement remain mandatory. A smaller
palette cannot stand in for this workload. The existing 320+16 workload remains
its own regression and evidence epoch.

Null validates source/range/Emulated member contracts only. Vulkan consumes exact
original SPIR-V; Metal additionally needs original selected driver-member proof;
WebGPU needs proved WGSL correspondence and the actual queried uniform limit.
Unmet limits or missing proofs refuse before recording. All runtime resource
ownership, state and completion remain in the original public Root machinery.

Passing this workload closes only its tested palette arithmetic scope. Real
material/instance tables, normal/tangent and color/depth/shadow scene equivalence,
sampling, terrain, atomic/indirect, complete budgets/platform lifecycle and the
same-commit production engine gate remain separate requirements.

Current WebGPU validation: Windows5/5, actual Mac7/7, Wasm/Node3/3,
source418/418, style6/6 and pinned formatter6 pass. The same Emulated package
has selected-entry target member proof and actual Chrome execution through
the existing public Root/lifecycle fixture:22 clean/fault flows,4 exact32-lane
numeric receipts,22 member/range receipts and real completion/retirement.
Original320+16 regression22/4 also passes. Both browser reports have no native
errors or diagnostics. Mac8629 frozen inputs/native artifacts are independently
checked; unrelated editor test drift is listed. Evidence:
`D:/arp/build/bindless-production-skinning256-web1/current-report.json`.

The original browser64KiB C++ stack failed. Overflow checks and object-frame
inspection establish91184 bytes required at the CPU oracle/readback path; the
test-only ceiling is131072 with runtime overflow checks. Asyncify remains32768
and GPU/resource/owner budgets are unchanged. Failed runs, including an early
incomplete-link browser observation, remain preserved. Baseline-only browser
Wasm compilation does not establish scene performance. No new Native capture,
independent Metal replay, common production gate or scene readiness is granted.

Historical Native validation: Windows4/4, actual Mac6/6, SPIRV-val6/6, source418/418,
style6/6 and formatter7 pass. Both Native backends preserve32 exact lanes for
clean/fault with original Root member/range admission and real completion/
retirement. Vulkan independent replay validates all32864+32 uploaded bytes,
output/color/fault and validation0; original320+16 replay remains green. Metal
submitted capture has14 hashed files (including one symlink resolved to its
captured buffer contents); no independent Metal replay. All8628 recorded Mac
inputs/native artifacts match. Current unrelated editor/CMakeLists.txt drift
is listed separately. Evidence:
`D:/arp/build/bindless-production-skinning256-1/current-report.json`.

The first replay runner path-initialization timeout and first incorrectly framed
source-graph rule attempt are retained. Source graph inspection establishes
palette root1/matrix3 and influence root0 for the actual compiled Metal module;
the driver matrix is type2. Correct explicit framing passes, while omitted
matrix proof still refuses. No refusal contract was relaxed. The new replay
workload uses an explicit `ARP_BINDLESS_GRAPHICS_ORACLE_DIR` to locate its shared
independent byte/numeric oracle in the embedded RenderDoc Python runtime.
