# Production instance64 buffer workload /1

Status: Vulkan/Metal/WebGPU scoped Backend/Platform Validated.
No new L2 execution capability or production
gate is granted. This workload uses the existing public Graphics Root, runtime
member query2, Range2, selected-module ownership and real completion/retirement.

The source ABI is `pcg::InstanceDataGpu`: four float4 fields at offsets 0,16,32,48,
stride64. Set0/binding0 is a read-only runtime array of four real host records
(256 bytes). Set0/binding1 is a workload-specific80-byte probe, not a production
material ABI. It supplies position, normal, tangent and uploaded instance
selectors. Base1 plus offsets0/1/2/1 exercises nonzero base and the last element;
record0 is a distinct sentinel. There is no clamping or hidden batching.

The reusable shader transform follows current WOCC PBR semantics: XZ uniform
scale, positive explicit Y scale or uniform scale, yaw about Y, then pitch X and
roll Z when either absolute angle exceeds0.001 radians, translation and burialY.
Normal/tangent rotate without inverse scale; normal is normalized and tangent.w
is preserved. This intentionally preserves current scene semantics. Wind,
skinning and color/depth/shadow pass equivalence remain separate requirements.

Four pixels each write24 lanes: vertex transformed position, fragment transformed
position, normal plus zero, tangent, four raw metadata words, yaw/phase/id/LOD.
Metadata is compared as uint32 bits (including NaN payload, negative zero and
subnormal representations), never by floating-point equality or conversion.
Arithmetic uses a documented absolute-plus-relative float32 tolerance of2e-5;
it cannot be reported as exact32. CPU oracle composes existing core rotations,
independently of shader component formulas. Poisoned output, clean/fault bounds,
capacity4, wrong owner/stage/version refusal and zero-residual retirement apply.

| Backend | Mapping and mandatory refusal |
| --- | --- |
| Null | Original Emulated declarations/member/range validation only; no GPU success. Missing correspondence and undersized ranges refuse. |
| Vulkan | Original SPIR-V runtime element64 and exact original256-byte range, original Graphics execution/completion; unsupported limits refuse. |
| Metal | Original Native request2 RuntimeElement plus selected MTLPointerType element graph/stride/alignment, Block probe proof and Range2 capacity; missing/mismatched driver proof refuses before publication/record. |
| WebGPU | Original SPIR-V/WGSL StorageRuntimeArray element relation and explicit uniform wrapper proof; bounded existing Results switch lowering and actual queried storage/uniform limits. Missing proof/limits refuse before recording. |

Acceptance requires fast host/declaration/Null tests, actual same-workload
Vulkan/Metal/Chrome numeric/fault/lifetime tests, capture/readback evidence and
regression of existing matrix/skinning workloads. Shader source remains Slang,
compiled with explicit row-major layout into SPIR-V/MSL/WGSL/reflection. This
scope is part of the production engine work, not a replacement for material,
sampling, terrain27, atomic/indirect, budgets or platform lifecycle acceptance.

Offline source composition/1: the compiler accepts an explicit ordered list of
Slang source libraries. It joins their exact text before authored entry text and
runs the existing whole-source resource/directive/operation checks on that joined
input. This is not preprocessor support: all includes/macros remain refused,
including inside libraries. Duplicate resolved library inputs refuse. Sealed
source artifacts and authored source hash cover the joined text; library content
hashes identify the explicit inputs. Existing callers with no libraries retain
their source identity and behavior. All four backends consume the same sealed
composition; no runtime IO or shader compilation is added.

Current evidence: Windows7/7, actual Mac8/8, Wasm/Node3/3; source418/style6 pass.
Vulkan independent replay verifies96 lanes against an independent frozen NumPy
reference, exact metadata bits, both ordinary buffers, color and fault; validation0.
Metal submitted capture12 files;8642 source inputs/38 native artifacts independently
match the recorded epoch. No independent Metal replay, draw firstInstance, wind,
scene pass equivalence or common production engine gate is granted. Actual Chrome
passes22 lifecycle flows,4 numeric96 receipts and22 runtime/member/range receipts,
with empty diagnostics/nativeErrors. Original matrix/skinning browser regressions
each pass22/4 (66 workflows/12 numeric receipts across all three cohorts). The59
evidence hashes and77 Windows/Wasm/browser artifact hashes are sealed/verified.
Evidence: `D:/arp/build/bindless-production-instance64-1/current-report.json`.
