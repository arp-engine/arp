# FrameGraph v2 Roadmap

Status: Contract Complete for compiled frame planning; Backend Validated locally for existing Null/Vulkan execution paths and Custom RenderGraph RHI pass recording. Platform Validated applies only to named hidden-window/runtime smokes with current evidence; this roadmap does not mark the FrameGraph path Production Ready.

## Goal

Upgrade the current lightweight `FrameGraph` from an ordered pass helper into a compiled frame execution plan. The graph owns frame-level pass dependencies, resource lifetimes, barriers, transient aliasing plans, load/store policy, queue planning, and imported resource protection. Renderer code continues to own domain pass declarations and `DrawPlan` contents; RHI backends execute compiled plans without exposing native handles above backend or platform layers.

## Boundary

- `render_graph` owns frame-level resources, dependency analysis, synchronization plans, transient lifetime, and execution order.
- `renderer` owns scene, GUI, material, lighting, world, postprocess, debug pass declaration and per-pass draw planning.
- `rhi` owns L2 command recording contracts, backend lowering, and native API translation inside backend-private implementation directories.
- `platform` owns surfaces, swapchains, acquire/present lifecycle, and backend selection.

`render_graph` must not include Vulkan, Metal, WebGPU, Android, or iOS native graphics headers. Backend-specific terms may appear only as capability labels, deterministic diagnostics, or backend-private implementation details; FrameGraph plans use RHI descriptors, resource states, and command records.

## Phases

| Phase | Name | Target Status | Deliverable |
| --- | --- | --- | --- |
| 1 | Boundary & Terminology | Contract Complete | Documented FrameGraph v2 ownership and locked native-header boundaries. |
| 2 | Builder / Compiler / Executor Split | Contract Complete | Added `FrameGraphBuilder`, `FrameGraphCompiler`, `CompiledFrameGraph`, and `FrameGraphExecutor`; `FrameGraph::execute()` routes through compiled execution. |
| 3 | Dependency DAG | Contract Complete | Generated pass order from resource read/write dependencies and detects dependency cycles. |
| 4 | Resource Lifetime | Contract Complete | Emits first-use, last-use, producer, and consumer lists for textures and buffers. |
| 5 | Barrier Plan | Contract Complete | Compiles texture/buffer transitions into `CompiledFrameGraph::barrierPlan()`; backend execution records commands through RHI command encoders and barrier descriptors. |
| 6 | Transient Alias Plan | Contract Complete | Builds lifetime-aware alias groups for transient textures; native memory aliasing remains deferred. |
| 7 | Load/Store & Tile GPU Plan | Contract Complete | Compiles attachment load/store, clear/dont-care, memoryless, and tile preservation hints. |
| 8 | Queue & External Resource Plan | Contract Complete | Compiles queue roles and imported resource protection for graphics, compute, transfer, and present. |
| 9 | Renderer Integration & Validation | Backend Validated locally | Scene/GUI composition executes through `CompiledFrameGraph`; Null/Vulkan tests and hidden smokes validate RHI lowering and command recording. |
| 10 | Custom Main Pipeline Cutover | Backend Validated locally | Samples build `CustomRenderFrameWorkload` and call `renderGraphFrame`; RHI Null/Vulkan pass recording is validated, while Metal/WebGPU runtime validation remains external. |

## Long-Running Execution Rule

Each phase must leave at least one focused unit test and one smoke or hidden-window validation path. Any new RHI capability, resource state, descriptor type, or backend behavior must start with an RHI contract test before Null/Vulkan/Metal/WebGPU implementation.

## Implemented Surface

- `FrameGraph::compile()` returns a `CompiledFrameGraph`.
- `CompiledFrameGraph` exposes pass order, executable order, resource lifetimes, barrier plan, transient alias plan, load/store plan, queue plan, and external resources.
- `FrameGraphExecutor` executes the compiled graph while preserving existing `executionTrace()`, `resourceTrace()`, `passTrace()`, `attachmentTrace()`, `aliasTrace()`, and `barrierTrace()` diagnostics.
- Dependency ordering preserves declared order for same-resource hazards while allowing simple producer/consumer reorder when a consumer is declared before its producer.
- Cycle detection rejects mutually dependent resource chains.

## Deferred Production Work

- Keep backend-owned barrier emission behind the RHI compiled frame plan and command-encoder contract.
- Extend native Vulkan/Metal/WebGPU/Null lowering evidence for barrier execution without exposing native handles above backend/platform code.
- Implement native transient memory aliasing after backend allocator contracts exist.
- Add multi-queue submission and semaphore/fence scheduling behind `queuePlan()`.
- Promote the Custom main pipeline to Production Ready only after backend parity, platform lifecycle/runtime smoke, memory lifetime, debug telemetry, migration-boundary scans, and visual/hash evidence are current.
