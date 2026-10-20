# Original pipeline full-definition custody /4

Status: Contract Complete / Prototype Complete for the CPU custody below,
verified 2026-10-05 host15/15, Wasm/browser8/8 and inventory183/183. No canonical Pipeline Source, immutable
artifact consumption, typed native execution or readiness promotion.

The existing original registration registry gains explicit version 4. Versions
1–3 retain their current contracts. Version 4 keeps descriptor /3 issuance and
adds full pipeline definition custody at the original factory boundary. A
pipeline reserves five owners: wrapper, registration record, exact layout,
copied package and complete definition. Layout/resource/descriptor accounting
stays as declared by the corresponding existing contracts. The same registry
capacity/generation machinery owns these units; no second registry is created.

Before any backend pipeline factory call, freeze the complete authored graphics
or compute descriptor under the registry metadata quota. Use the existing
allocator-safe copy boundary, including labels, selected entries, vertex
attributes, color targets, depth/stencil, raster/front-face, topology, MSAA,
viewport/scissor, subpass compatibility and tessellation fields. The definition
points to the exact retained original layout and the separately copied package.
The existing selected-entry declaration validator is shared by the typed Source
contract and version 4 factory preflight. Missing/mismatched selected entries,
stages, workgroup reflection or layout reject before backend calls.
The backend receives that same immutable definition. It cannot supply missing
L2 fields or decide what gets retained. Ordinary factories never publish custody.

Cold allocation or backend creation failure returns no candidate and releases
all reserved units/metadata. Published objects retain the exact factory
definition, layout and package after caller mutation/release. No caller-provided
descriptor may be attached retrospectively. Accessors expose const L2 data only;
validation checks original ticket, exact layout/control block, package and
definition relationships without allocation, native waits or file IO.

| Backend | Version 4 definition custody | Canonical artifact/typed execution |
| --- | --- | --- |
| Null | Equivalent over original validator object | DiagnosticFailure |
| Vulkan | Equivalent over successful original wrapper | DiagnosticFailure |
| Metal | Equivalent over successful original wrapper | DiagnosticFailure |
| WebGPU | Equivalent over successful original wrapper | DiagnosticFailure |

The existing native-loss gate remains mandatory before backend creation.
Full definitions and successful backend validation do not prove that the factory
consumed immutable generated artifact bytes. Paths, reflection hydration and
cache hashes remain metadata. The subsequent artifact contract must bind an
existing validated publication to actual backend consumption before canonical
Pipeline Source admission. No dummy pipeline, ordering bypass, graph/journal
replacement or additional command-proof friend is permitted.

Tests cover /2–/3 absence versus explicit /4 presence, ordinary/foreign/stale
objects, five-owner exact/one-less capacity, cold exact/one-less/exhausted quotas,
backend failure rollback, deep caller mutation, true parent allocation retention,
hot allocation0 and final quotas0. Keep fast contract, Null hidden-frame and both
browser feature modes with all prior mandatory native map/publication checks.
No shaders or visible output change; RenderDoc tool unavailability remains an
explicit diagnostic, with no Backend/Platform/Production readiness promotion.

The quick contract and Null hidden-frame verify real Null compute and graphics
wrappers; both browser modes verify a genuine WebGPU L2 compute factory using
the existing validated single-entry publication/interface builder. Host registry
metadata peaks are compute2904/graphics3636; WebGPU compute1616. Exact/one-less/
exhausted metadata, owners10/9, foreign/ordinary/missing-selected/backend-failure
rollback, caller mutation, original layout allocation retention, hot validation0
and final quotas0 pass. Version 4 descriptor issuance passes the canonical
assembly tests. Both reports preserve all mandatory map/publication flags with
empty diagnostics/nativeErrors. Evidence: `build/web-bindless-apple-debug/artifacts/bindless-continuation/pipeline-full-definition-validation.yaml`.
