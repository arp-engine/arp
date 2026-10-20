# Nanite-Like Virtualized Geometry Plan

Status: Planning Record.

This document records the proposed direction for Nanite-like virtualized geometry in this project. It is an architecture proposal, not an executable task queue.

## Goal

Support static opaque high-detail meshes through an offline-built cluster hierarchy, automatic cluster-group simplification, render-time cluster selection, and later GPU-driven visibility, geometry page residency, and virtual texture integration.

The feature should reduce the need for manual render LODs for static opaque meshes, while preserving explicit manual or generated fallback assets for platform fallback, collision, navigation, proxy/HLOD, deformation, transparent or masked materials, foliage, and authored visual overrides.

## Ground Rules

- Keep the renderer, RenderGraph, GUI, samples, and gameplay layers free of Vulkan, Metal, WebGPU, Android, iOS, and backend-native handles.
- Follow the RHI/RHI order: define L2 domain semantics, define lowering contracts, then implement backend paths.
- Keep all new shader sources in Slang and generate SPIR-V, MSL, and reflection outputs with tests.
- Keep resource import, conversion, redirection, compatibility, and fallback behavior based on explicit metadata and contract tests.
- Fail with diagnostics for unknown geometry, material, seam, deformation, or residency semantics.
- Keep file IO, decompression, shader compilation, unbounded allocation, blocking waits, and per-draw pipeline creation out of frame hot paths.
- Track readiness with the project status vocabulary: Contract Complete, Prototype Complete, Backend Validated, Platform Validated, Production Ready.

## Non-Goals

- Do not claim full Unreal Nanite parity in the first implementation.
- Do not replace the terrain streaming system, collision system, navigation system, material system, or virtual texture system.
- Do not hide platform gaps behind silent fallback behavior.
- Do not implement runtime mesh simplification in frame loops.

## Architecture

The feature is split into four cooperating systems:

- `VirtualizedMeshAsset`: offline metadata and payload format for cluster hierarchy, simplified parent clusters, geometry pages, bounds, error metrics, material ranges, and fallback policy.
- `VirtualizedGeometryBuilder`: offline tool-side builders for triangle adjacency, leaf clusters, cluster groups, automatic simplification, hierarchy construction, page packing, and deterministic validation.
- `VirtualizedGeometryRuntime`: renderer-facing runtime selection and draw planning for CPU-selected clusters first, then GPU-selected clusters.
- `VirtualResourceResidency`: shared residency and streaming contract for geometry pages and future virtual texture tiles.

## Asset Contract

`prepareMeshlets` is too weak as the long-term semantic name. The asset layer should introduce an explicit virtualized geometry policy, such as `buildClusterHierarchy`, with metadata for:

- source mesh identity, source format, coordinate basis, and format version;
- material boundary policy;
- hard-edge, UV seam, normal seam, and attribute protection policy;
- leaf cluster table;
- cluster group table;
- parent-child hierarchy;
- simplification error metric and screen-error policy;
- normal cone and bounds per cluster;
- geometry page table and page dependencies;
- fallback mesh or manual LOD policy;
- unsupported-feature diagnostics.

Unknown semantics, ambiguous seams, unsupported deformation, or missing fallback data must fail deterministically unless the asset is explicitly marked Prototype or Legacy fallback.

Status target: Contract Complete.

Validation path: fast schema and invalid-metadata tests, plus a Null package-plan smoke that loads a virtualized mesh manifest.

## Leaf Cluster Build

Leaf cluster building converts source mesh triangles into small, deterministic, material-compatible clusters.

Pipeline:

1. Split the source mesh by submesh, material, topology island, and protected boundaries.
2. Build a triangle adjacency graph from shared topology edges or explicit importer weld metadata.
3. Grow clusters deterministically from stable seeds.
4. Reject candidate triangles when they would exceed vertex, triangle, material, seam, normal-cone, or bounds-growth contracts.
5. Emit cluster-local vertex remaps, index ranges, bounds, normal cone, material id, and diagnostics.

Suggested classes:

- `TriangleAdjacencyBuilder`
- `ClusterBuildPolicy`
- `LeafClusterBuilder`
- `ClusterValidationReport`

Status target: Prototype Complete.

Validation path: deterministic cluster output, material-boundary preservation, budget enforcement, and a small offline import smoke.

## Cluster Group Simplification

Automatic simplification is the core Nanite-like step. Leaf clusters are grouped, simplified, and re-clustered into parent levels.

Pipeline:

1. Group adjacent clusters using topology and explicit metadata, not guessed proximity.
2. Merge group geometry into a simplification input.
3. Run attribute-aware simplification using explicit policy values.
4. Preserve protected attributes such as material seams, hard edges, UV seams, normals, and optional area preservation.
5. Compute simplification error and parent bounds.
6. Split the simplified result back into parent clusters.
7. Repeat until root-level clusters are produced.

Suggested classes:

- `ClusterGroupBuilder`
- `ClusterSimplifier`
- `SimplificationErrorMetric`
- `ClusterHierarchyBuilder`

`meshoptimizer` may be used as an offline simplification backend when available, but its behavior must be wrapped by project-owned contracts and tests. Missing simplification support should fail with a diagnostic or produce an explicitly marked Prototype fallback artifact.

Status target: Prototype Complete.

Validation path: parent triangle count reduction, monotonic or explainable error metrics, protected-boundary preservation, and deterministic hierarchy hash.

## Geometry Pages And Residency

Cluster data should be packed into geometry pages so later streaming can operate on bounded chunks.

A page contains:

- page id and version;
- contained cluster ids;
- vertex and index payload offsets;
- compressed or uncompressed payload metadata;
- page dependencies;
- minimum parent fallback information;
- residency priority hints derived from hierarchy and screen error.

The project should introduce `VirtualResourceResidency` as a shared abstraction for future virtualized geometry pages and virtual texture tiles.

Suggested concepts:

- `VirtualResourcePageId`
- `VirtualResourcePageRequest`
- `VirtualResourceResidencyState`
- `VirtualResourceBudget`
- `BackendOwnedPageUpload`
- `ResidencyFeedbackBuffer`

Status target: Contract Complete.

Validation path: stable page ids, missing-page diagnostics, bounded request queues, eviction contract tests, and Null residency smoke.

## CPU Runtime Prototype

The first runtime path should select clusters on the CPU. This proves the hierarchy and error metric before adding GPU complexity.

Runtime flow:

1. Read camera and instance transform.
2. Traverse the cluster hierarchy.
3. Select parent or child clusters by screen-space error.
4. Ensure a parent and child are not selected simultaneously for the same hierarchy branch.
5. Build a `ClusterDrawList`.
6. Submit through existing indexed draw and RenderGraph paths.

Suggested classes:

- `VirtualizedMeshInstance`
- `ClusterSelectionContext`
- `CpuClusterSelector`
- `ClusterDrawList`

Status target: Prototype Complete.

Validation path: near camera selects finer clusters, far camera selects coarser clusters, missing pages fall back only to explicit parent/fallback data, and Null hidden-frame telemetry smoke.

## RHI L2 Semantics

Once the CPU prototype proves the model, RHI should add domain semantics for GPU-driven virtualized geometry.

Candidate L2 concepts:

- `VirtualGeometryDrawPlan`
- `ClusterDrawListBuffer`
- `GpuClusterCulling`
- `GpuGeneratedDrawList`
- `VisibilityBuffer`
- `ResidencyFeedbackBuffer`
- `BackendOwnedPageUpload`

These are engine concepts, not Vulkan, Metal, or WebGPU API shapes.

Status target: Contract Complete.

Validation path: L2 semantic tests, Null validation, unsupported-backend diagnostics, and RenderGraph frame-plan coverage.

## Backend Lowering

Lowering must define behavior for Null, Vulkan, Metal, and WebGPU before backend implementation work.

Expected lowering questions:

- Which native features are required or optional?
- How are storage buffers, descriptor tables, indirect draw, visibility buffers, and feedback buffers represented?
- Which resource states, stages, and access masks are required?
- How are page uploads staged and synchronized?
- What is native, emulated, degraded, or diagnostic failure?
- What telemetry proves fallback details stay hidden from upper layers?

Vulkan can target storage buffers, compute culling, indirect draw, and explicit barriers. Metal requires an argument-binding and indirect-command capability contract. WebGPU must report runtime capability gaps honestly and either use a bounded equivalent path or fail diagnostically. Null validates the semantics without real GPU work.

Status target: Contract Complete before backend work.

Validation path: capability matrix tests for Null, Vulkan, Metal, and WebGPU.

## GPU Cluster Culling

GPU culling should be added after the L2 and lowering contracts are in place.

Compute inputs:

- cluster hierarchy or active frontier;
- cluster bounds and normal cone;
- camera matrices and frustum;
- page residency table;
- optional occlusion data.

Compute outputs:

- visible cluster list;
- compacted draw list or indirect draw records;
- residency feedback requests.

All shader code must be Slang, with generated SPIR-V, MSL, and reflection artifacts plus tests.

Status target: Backend Validated for Vulkan first; Contract Complete for Metal and WebGPU until runtime validation.

Validation path: shader reflection tests, descriptor-layout tests, dispatch telemetry, Null diagnostics, and Vulkan hidden-window smoke.

## Visibility Buffer

The visibility-buffer path should be introduced after cluster culling is stable.

Flow:

1. Visibility pass writes visible triangle, cluster, instance, and material identifiers.
2. Material resolve pass shades visible pixels.
3. Unsupported material paths fail or route to explicit fallback renderers.

Initial support should be limited to static opaque materials with a fixed descriptor contract. Transparent, masked foliage, skinned, cloth, and strong deformation paths remain fallback-only until explicitly designed.

Status target: Prototype Complete, then Backend Validated per backend.

Validation path: visibility format tests, material-id resolve tests, unsupported material diagnostics, and image/hash smoke.

## Virtual Texture Integration

Virtualized geometry should be designed with virtual texture integration, but the first cluster mesh prototype should not be blocked on virtual textures.

Shared layer:

- `VirtualResourceResidency`
- budget accounting;
- request queues;
- upload limits;
- eviction policy;
- telemetry and diagnostics.

Virtual geometry uses geometry pages. Virtual texture uses texture tiles, mip tiles, indirection maps, and tile feedback. Both share residency policy but keep independent asset formats.

Status target: Contract Complete after geometry residency contract.

Validation path: texture tile metadata tests, feedback request tests, geometry-page and texture-tile budget interaction tests, and Null feedback smoke.

## Terrain Adapter

Nanite-like geometry can replace terrain render LOD and render geometry streaming, but it must not replace the whole terrain streaming system.

Terrain system responsibilities:

- terrain tile and world-cell lifetime;
- source heightfield or source mesh data;
- collision;
- navigation;
- water interaction;
- vegetation and scattering;
- terrain material layers;
- runtime edit or deformation;
- HLOD and gameplay queries.

Virtualized terrain render data responsibilities:

- cluster hierarchy;
- terrain geometry pages;
- render-only fallback mesh;
- screen-error selection;
- render residency feedback.

Status target: Contract Complete before runtime terrain integration.

Validation path: terrain tile metadata tests, render-data missing-page fallback tests, and diagnostics proving gameplay terrain data is not replaced by render cluster data.

## Suggested Milestones

Milestone 1: Asset contract and leaf clusters. Status: Prototype Complete locally on 2026-05-18.

Deliverables recorded: `VirtualizedGeometryBuildPolicy`, deterministic `TriangleAdjacencyBuilder`, `LeafClusterBuilder`, validation tests, and deterministic hierarchy hash coverage.

Milestone 2: Cluster group simplification. Status: Prototype Complete locally on 2026-05-18.

Deliverables recorded: `ClusterHierarchyBuilder`, deterministic prototype `ClusterSimplifier`, parent cluster generation, simplification error metric, material-boundary preservation, deterministic hierarchy hash test, and invalid-policy diagnostics.

Milestone 3: Page metadata and CPU selector.

Deliverables: geometry page table, residency contract, CPU cluster selector, draw-list builder, Null hidden-frame telemetry smoke.

Milestone 4: RHI virtual geometry contract.

Deliverables: L2 concepts, lowering contracts, capability matrix tests, RenderGraph frame-plan integration.

Milestone 5: GPU culling.

Deliverables: Slang compute shader, generated SPIR-V/MSL/reflection, descriptor tests, Vulkan hidden-window smoke.

Milestone 6: Visibility buffer.

Deliverables: visibility pass, material resolve pass, fallback material diagnostics, image/hash smoke.

Milestone 7: Residency streaming and virtual texture bridge.

Deliverables: bounded request/upload/evict pipeline, geometry page streaming, virtual texture tile contract, shared telemetry.

Milestone 8: Terrain adapter and platform gates.

Deliverables: terrain render-data adapter, mobile/WebGPU lifecycle behavior, platform validation docs, production readiness matrix.

## First Recommended Slice

The safest first slice is Milestones 1 through 3:

- asset contract;
- leaf cluster builder;
- cluster group simplification;
- hierarchy metadata;
- geometry page metadata;
- CPU cluster selection;
- Null smoke.

This proves the essential Nanite-like data model without forcing RHI indirect draw, visibility buffer, virtual texture, and streaming to land at the same time.
