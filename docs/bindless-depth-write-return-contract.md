# Completed sampled depth return to its producer /1

Status: Platform Validated for authored WebGPU clean/fault sampled consumers,
real producer reclear and stale-proof rejection. No production engine grant.
Common engine gate=false; WOCC=Traditional; renderer migration has not started.

The original sampled consumer must genuinely complete and release its state
transaction before the producer can claim the same texture. The existing
canonical Root/state/content journal remains the only owner of state and
generation changes. No second executor, synthetic completion or host state
mutation is introduced.

An explicit `OriginalCompletedDepthWriteReturnContract::Intent` declares version1,
opt-in, exact preceding Vertex/Fragment/Compute read stages and a bounded count
of return transitions. The sole new transition is whole, single-layer,
single-sample D32 Sampled|RenderAttachment from ShaderRead/ShaderRead access to
DepthAttachment/exact DepthStencil read-write scope. The original state journal
must independently match committed before-state. Color, storage, partial-range,
wrong scope and unselected legacy producer paths continue refusing.

The existing CopyPassLowering2 may consume this optional intent for its ordered
empty attachment producer. Cold admission adds no command or execution grant.
Lowering counts actual return barriers, rejects unused/over-capacity intents,
checks the original texture metadata, and consumes the immutable selected intent
on revalidation. WebGPU normative ordered usages map the transition; genuine
queue callbacks still decide publication. Null validates only; Vulkan/Metal and
WebGL explicitly refuse this canonical producer profile. Their separate Native
execution profiles receive no new authority.

Required evidence: quick exact/negative five-backend contract; real Chrome clean
and fault comparison consumers, full callback retirement, genuine producer
reclear, stale completed depth/Root/Closure20 rejection, independent preservation
of the untouched mip generation and quota return. Failed producer preparation
must preserve the existing proof and content. Rebuild affected native contracts
and Mac/WebGPU regressions; update the single current delivery table only with
the resulting evidence.

## Scoped checkpoint (2026-10-19)

Actual Chrome completes two Execution14 clean/fault comparisons through the
original callbacks, releases both reader transactions, retains valid Closure20
proofs, and executes the original producer's next clear through its genuine
queue completion. The unselected old lowering and one-less return intent refuse
without publication or quota change. The completed write invalidates the old
depth proof, Root and Closure20; a fresh proof sees a new mip0 generation while
mip1 keeps its independently completed generation. All source quotas return.

The first browser attempt is preserved. Its per-mip +1 generation assumption
contradicted the original control's shared monotonic allocator: both mip
publications consume IDs, and rollback never recycles them. The fixture now
checks strict advancement beyond both old IDs and exact untouched-mip identity;
no content validator or runtime generation rule was weakened.

Final Windows5/5, codegen1/1, Node2/2 + Chrome4/4, Mac13/13,
source422/style6 and pinned formatting10 files pass. The previous22 execution
lifecycle flows/four comparison receipts still pass alongside the two new
consumer/reclear flows. Apple9236 source inputs/166 native artifacts and78
explicit affected local inputs are verified at their recorded epochs. Native
Vulkan comparison capture/replay remains clean. This is not a dependency-closure
or whole integrated-tree seal.

This checkpoint validates producer completion, state/generation custody and
stale rejection. It does not independently read post-rewrite depth texels or
establish complete depth/shadow visual parity. Those numerical/visual checks,
remaining production operations/budgets/lifecycle, the common engine gate and
renderer/WOCC acceptance remain open.

Evidence: `D:/arp/build/bindless-depth-write-return1/current-report.json`.


## Independent post-rewrite texel observation (2026-10-19)

Actual Chrome clean/fault flows now independently observe physical D32 contents
after the original canonical producer's completed reclear: four mip0 texels
are byte-exact0.125 and untouched mip1 is byte-exact0.5. The test-only observer
requires the original committed CopySrc state and existing scalar transfer
geometry, then makes a genuine native DepthOnly copy/map. It issues no journal,
content, publication or completion proof and does not write/seed the texture.
It follows the existing native depth-attachment diagnostic fixture; the public
color-only readback-layout D32 refusal remains unchanged. Its first attempt
incorrectly using that layout is retained in browser1, without weakening it.

Windows5/5, codegen1/1, Node2/2 + Chrome4/4, Mac13/13, source422/style6
and pinned formatting2 pass. Native runtime/shader inputs match the preceding
Vulkan capture epoch; that capture is retained, not freshly executed. The
observer closes this specific physical readback gap, not complete depth/shadow
scene parity, production budgets/lifecycle or the common engine gate.
Evidence: `D:/arp/build/bindless-depth-write-return-texels1/current-report.json`.
