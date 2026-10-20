# Original depth Table pipeline creation preflight

Status: Contract Complete / Prototype Complete for cold preflight only. Host6/6
(including Null hidden-frame), Wasm3/3 and actual created-device query smoke for
Compute/Graphics pass; see `original-depth-table-expansion-preflight-validation.yaml`.
Not Backend Validated native actor / Not Platform Validated / Not Production Ready. This
child validates creation prerequisites; it does not issue a pipeline, creation
receipt, Table registration, positional input or command proof.

OriginalDepthTableExpansionPipelinePreflight/1 requires explicit Prototype
opt-in. The original factories gain separately named definition-validation
entries. Ordinary factory requests and validation remain unchanged. Reuse the
genuine immutable input3, private binding projection1, original shared L2 layout,
PipelineShaderDeclarationContract, original graphics profile and complete shared
pipeline-state validators. A projection from a different input owner is invalid
even when bytes match. The package and layout pointers must select those exact
owners. Graphics has no vertex buffers, tessellation entries or patch points;
its two real target modules remain separate. Compute selects its exact entry.

Derive required groups, total physical bindings, maximum entries per group,
maximum sampled textures and storage buffers per consuming stage, selected
stages/modules and summed original WGSL bytes from the retained projection and
targets. Cardinality is one per physical entry, logical Table capacity stays
three. Output remains its separate original binding. Count ceilings and source
bytes are independent authored budgets, never device facts. Compute's exact
reflected workgroup X/Y/Z and checked invocation product are required.

Before any native creation, use webGpuNativeDeviceLimits on the created live
device and query existing shader/pipeline/layout/error-scope methods. Add the
missing maxComputeWorkgroupSizeZ query with the same strict integer sanitizer.
All used dimensions/counts require nonzero queried limits and exact boundary
comparisons; missing/malformed values or one-less limits fail deterministically.
No adapter limits, specification minima, feature flags or authored budgets can
replace those queries. Graphics additionally checks actual maxColorAttachments.
Unused Compute limits on Graphics are not required. The preflight performs no
shader/layout/pipeline/group creation, queue operation or scope push/pop.

Null supplies validator-only metadata, never native creation. Vulkan and Metal
fail with stable selected-payload diagnostics. WebGPU can validate native
preconditions only after actual device/method/limit queries; creation and command
execution remain closed in every result until the independently defined genuine
payload, candidate custody, error scopes and observed-loss registration are
implemented. Positive synthetic facts in unit tests prove this decision contract
only, never Backend Validated payload semantics.

Require exact/one-less count and source budgets, each used zero/one-less limit,
owner/package/layout/entry/full-state/profile negatives, distinct equivalent input
owners, hostile default PMR and atomic empty requirements on failure. Preserve
the complete legacy reports without exclusions; this unused cold entry cannot
alter ordinary paths. Retain quick Host/Wasm contracts and Null hidden-frame.
An optional real browser query smoke must explicitly confirm no native creations
and no command authority. RenderDoc unavailable is a diagnostic, never visual or
platform promotion. Next: retain this preflight in original creation candidates
and factor native layout emission without treating expansion as BindingSpace/5.
