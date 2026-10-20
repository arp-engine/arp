# RHI Frontend package reorganization

Status: Contract Complete (package structure). MinGW Debug/Release, named Vulkan Windows paths and both WebGPU browser profiles are validated. P4 remains open solely for Apple-native platform validation. Production Ready is not claimed.

## Contract and layout

Keep the fourteen `gpu_*.h` public entries and existing public symbols, object layout, target names, allocation ownership, budgets, extension opt-in, diagnostics, and four-backend execution gates. The only source subpackages are flat `frontend`, flat `semantics`, `backends/{null,vulkan,metal,webgpu}`, and flat `conformance`.

Frontend owns L2 models, pure validation and shared object mechanisms. Semantics owns versioned capability/lowering decisions and consumes value models, never owned runtime objects. Backend owns native queries, payloads, recording, submission, completion, and reclamation. Models must not consume decisions; shared mechanisms may consume them. No native headers escape backends. No compatibility forwarding headers are added.

Files are consolidated around device, resources/lifetime, memory budgets, descriptor, pipeline/reflection, command models/arguments/sources/usage/state/recording, transfer copy/upload/readback, frame, bindless, query and telemetry. Leaf types remain separate when an aggregate would create an include cycle. Preparation, recording, submission and completion remain distinct; Metal native and non-Apple stubs remain distinct. Merging code never merges independent contracts or opt-ins.

## Execution stages

- [x] P1: Recorded source/API/diagnostic baseline, migration tool and structural RED checks. Rebuilt MinGW baseline; RenderGraph RHI execution/Null hidden-frame CTest passed 1/1. Large fixture's pre-existing quota assertion is recorded separately.
- [x] P2: Flatten and consolidate Frontend/Semantics; separate mixed decision/object code; update public entry consumers, build lists and source assertions; run quick contracts and freshly built Null smoke.
- [x] P3: Consolidate Null, Vulkan, Metal and WebGPU in that order, preserving native/stub boundaries; run quick contracts and Null smoke after each backend slice.
- [x] P3-N: Null command validation consolidation.
- [x] P3-V: Vulkan native utility consolidation.
- [x] P3-M: Metal graphics, indirect preparation and binding facts consolidation.
- [x] P3-W: WebGPU pipeline/input/transfer preparation consolidation.
- [ ] P4: Run MinGW Debug/Release contracts, WebGPU browser regression and available target-platform checks; capture Vulkan with RenderDoc; report exact evidence and remaining platform limitations.
- [x] P4-Structure: Original 252 source assertions plus four package/dependency assertions; migration map, diagnostic inventory and public size/alignment audit.
- [x] P4-Release: MinGW Release builds all 54 selected targets; refreshed RHI/Shader/Null CTest passes 57/57.
- [x] P4-Vulkan: Real NVIDIA RTX 3050 execution, hidden-window swapchain smoke and AI RenderDoc replay/readback inspection.
- [x] P4-Debug: Refreshed all affected Debug targets after the upload-layout lifetime repair; all 57 selected tests pass.
- [x] P4-WebGPU: Rebuilt with explicit host Slang/reflection tools; both real-browser feature profiles pass (2/2).
- [x] P4-WebGPU-Normal: Full assertion-enabled real-browser contract/smoke passes in 233.33s; report success=true with no unexpected diagnostics.
- [x] P4-WebGPU-Restricted: Complete browser contract/smoke passes with indirect-first-instance explicitly disabled in 240.01s; unavailable execution stays closed.
- [ ] P4-Apple: Apple Objective-C++ compile, native execution/readback/completion and target-platform smoke. No Apple toolchain/device is available on this Windows host; host stubs do not satisfy this item.

## Evidence

Evidence is stored under `build/rhi-package-reorganization/`. The baseline source inventories pass 4 + 248 assertions with `python -X utf8`. Windows default GBK decoding requires UTF-8 test invocations or explicit UTF-8 reads. Existing macOS evidence in older documents is historical, not evidence from this Windows run.

An early isolated baseline large Null fixture failed before its frame loop at `cpu_quota_factory_fixture.h:15` (`assert(object)`), after typed-source assembly checks. The full current MinGW rebuild passes the main Null hidden-frame suite; the early fixture failure is retained as historical evidence, without an unproven cause attribution. GCC 16 MinGW requires COFF big-object compilation for ownership-heavy translation units and consumers; `arp_rhi` propagates `-Wa,-mbig-obj` only on MinGW. RenderGraph RHI execution also supplies an independent Null hidden-frame path.

Each completed stage updates this file before continuing. Target-platform unavailability is recorded as a validation gap and cannot promote Backend Validated, Platform Validated or Production Ready.

P2 evidence: 252 original source checks plus 3 new package checks passed; freshly rebuilt MinGW Debug RenderGraph/Null hidden-frame CTest passed. Public declarations were relocated without changing data members; explicit snapshot/package includes replace the former authorization header transitively supplying them. `phase2-build.log` contains build evidence.

P3-N evidence: Null validators now precede command recording in `null_commands.cpp`; separate validator files were removed. Quick source/package contracts and freshly rebuilt Null hidden-frame CTest passed 4/4. Dummy sync remains exclusively under Null.

P3-V evidence: handle conversion and bounded frame waits now live with native runtime definitions in `vulkan_runtime.h`; all existing native command/resource/swapchain units remain independent. MinGW Debug Vulkan target rebuilt; quick contracts/Null hidden-frame CTest passed 4/4. Diagnostic inventory still matches all 5704 IDs and occurrence counts exactly.

P3-M evidence: 21 Metal headers consolidated into four flat units; preparation/recording/submission and native `.mm`/non-Apple stub boundaries remain separate. MinGW non-Apple Metal target rebuilt and quick/Null CTest passed 4/4. This validates host stubs and contracts, not Apple-native payloads.

P3-W evidence: 32 WebGPU headers consolidated into pipeline preflight/preparation, command inputs, upload preparation, readback preparation and readback results. Native command preparation/recording/submission/completion remain independent. MinGW non-browser WebGPU target rebuilt; source/package/Null CTest passed 4/4 after adjusting include-evidence assertions to the new preparation path. Browser-native validation follows in P4.

Final audit: 547 original RHI files map to 466 current files (14 public entries retained); 42 public types have identical MinGW sizeof/alignof against the saved baseline. No RHI include cycles or unresolved internal imports were found. Legacy unregistered L1 test sources already refer to removed APIs; they are outside the active target graph and are preserved. All references to the 547 migrated source files are audited separately; no compatibility headers are introduced for removed L1 APIs.

P4 audit: `final-audit.json` confirms zero removed-path references and identical 5704 stable diagnostic IDs / 8208 occurrences. The original 252 assertions remain green, with four added package and include-graph guards (256 total). `abi-baseline.txt` and `abi-current.txt` match for all 42 sampled public types.

P4 regression repairs: policy tests now distinguish static Metal vocabulary from queried device execution and preserve explicit CPU replay opt-in refusals. Windows source-reading tests use logical text lines and match the existing explicit allocator signatures. Release exposed a pre-existing dangling reference in whole-texture upload validation: its mapper now refers to a local captured layout that survives the complete validation pass, without changing public object storage. The existing exact whole-range/mip fixture exercises this repair with assertions enabled. Fingerprinted depth Slang fixtures are checked out with LF bytes using `.gitattributes`, preserving the authored SHA-256 identities instead of weakening identity validation. No shader program or matrix contract is changed.

P4 Release: `win-vulkan-release-final-build.log` records all 54 selected target builds after the repair, including the engine DLL; `win-vulkan-release-final-ctest.log` records 57/57 passes. Assertion-enabled contracts and Debug behavior evidence are distinguished from ordinary Release targets that use NDEBUG. `vulkan-release-hidden-window.log` records the real native swapchain smoke (exit 0).

P4 Debug: `win-vulkan-debug-final-build.log` records all 54 selected target builds, including the engine DLL, after the lifetime repair; `win-vulkan-debug-final-ctest.log` records 57/57 passes with Debug assertions and the freshly rebuilt Null hidden-frame fixture.

P4 browser proof repair: the installed Vulkan SDK Slang identifies itself as 2026.8. A SHA-256-verified official 2026.17 compiler reproduced the same existing `SHADER-VERTEX-TRIANGLE-CONDITION` rejection. The closed proof incorrectly required VertexIndex to be loaded before BaseVertex, although both artifacts preserve the required subtraction data edge. `VertexIndexTriangleProof` now identifies the two exact required sources independently of load scheduling and still rejects reversed subtraction and all existing CFG/reference/type/literal mutations. The reused binary mutation fixture tests all four independent load order combinations and reversed subtraction in either branch. Assertion-enabled native proof tests pass against actual complete 2026.8 and 2026.17 SPIR-V, combined WGSL and their cross-target agreement. Browser regression uses the configured preset compiler rather than replacing artifacts with unverified binaries. This changes neither shader source nor the proven mathematical domain.

P4 WebGPU normal profile: `webgpu-normal-report.json` records success=true, 386 passing boolean facts, and no unexpected diagnostics. The entire Debug suite completes in 233.33s after genuine native preparation, original upload/readback, callback custody, quota and actual device-loss checks. The former 180s budget stopped a still-progressing suite at typed dependency/payload publication; CMake now authors a 360s Debug harness deadline, keeping the historical 180s Release default and explicit cache overrides. These are test-process deadlines, separate from unchanged production resource budgets. This current suite validates closed execution gates: `publicRhi*FramePassed` stays false by design. The 2026-09-30 historical report exercised an older public-frame scope and is not a migration baseline for the present contract. Browser Platform Validated claims cover only the named current paths, not public GPU-frame execution or all-backend Production Ready.

P4 WebGPU restricted profile: `webgpu-restricted-report.json` records success=true, no unexpected diagnostics, and indirect-first-instance disabled. `web-final-ctest.log` records 2/2 complete browser passes (233.33s / 240.01s); `webgpu-validation-summary.json` records their exact scope and bounded 360s harness deadline. Expected capability/Prototype refusals are retained rather than counted as public GPU-frame success.

Final local result: the original 252 source assertions plus four structural guards pass; each MinGW preset builds the same 54 selected targets and passes 57/57 selected tests, including assertion-enabled behavior contracts and Null hidden-frame checks. Vulkan native hidden-window and RenderDoc readback evidence is present. Browser validation passes both profiles. The only outstanding P4 requirement is fresh Apple-native compilation/execution/readback/completion and platform smoke; unavailable Apple hardware/toolchain is a recorded validation gap, not a successful stub substitute.

P4 Vulkan evidence: `vulkan-renderdoc/report.json` records real NVIDIA GeForce RTX 3050 compute, graphics, indexed indirect, fault, fence and pixel readback. `vulkan-hidden-window.log` records successful native hidden-window swapchain smoke (exit 0). Capture `vulkan-renderdoc/public-bindless-vulkan_capture.rdc` replays successfully; `renderdoc-report.json` records 60 actions, three readable graphics outputs (events 89/118/169) and zero debug messages. AI inspected `vulkan-renderdoc/outputs.png`; output pixels match the runner readback. This is Backend Validated / Windows Platform Validated for these tested paths, not all-backend Production Ready.
