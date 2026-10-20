# Original completed whole texture read plan /4

Contract Complete / Prototype Complete for bounded CPU read planning and selected
cold custody. Scoped Backend Validated covers the actual completed-input/cold
composition only, not general shader execution. This prerequisite belongs to P3;
public WebGPU Emulated, Metal public Graphics and renderer/WOCC P4-P6 remain incomplete.

ContentPlan4 explicitly opts into bounded CPU planning of sampled reads from
ContentCreation1 whole textures whose authentic content control is Completed.
The immutable plan retains the original texture/control and its exact generation
and revision. It reuses the existing ContentCell inventory and state transaction;
it creates no content domain, journal, generation, upload or completion receipt.

The selected domain is whole ordinary sampled 2D RGBA8/RGBA16 views. Each content
texture must have at least one declared shader read. A resource without original
content may coexist, but a cell-profile content texture, attachment, storage write,
upload, readback or partial sampled range cannot enter this read-only profile.
Existing typed Inputs and State/Stream enforce descriptor and access correspondence
and the explicit CopyDst-to-ShaderRead transition. A content observation never
performs or grants that transition. Genuine upstream completion/issuer provenance
is independently required by the native closure, not inferred from this plan.

Budgets explicitly bound controls, shader read steps, command visits and projected
observation visits. Upload, content-cell and readback budgets must be zero. Each
distinct retained content control is counted once against the original root's
owner ceiling. No Sources ceiling, CPU quota or native custody ceiling grows
implicitly; preflight failure publishes nothing and performs no mutations.

Cold Prepared validation compares the original Completed value/revision. During
Pending, the same texture's original state controls are owned by the existing
transaction journal, excluding a competing original upload/state transaction.
Content values remain committed and observable: no content control is enlisted,
proposed, completed or rolled back for a read. Every validation, including native
completion, must still match the captured completed value/revision. Abandoning
state ownership can invalidate state observations but preserves content
generation/revision. Device loss invalidates the original retained controls.
External serialization follows the existing state/control contract.

| Backend | CPU read-plan inspection | Execution authority |
|---|---|---|
| Null | Equivalent validator only | DiagnosticFailure |
| Vulkan | Equivalent shared values; original resource admission unchanged | DiagnosticFailure for this selected join |
| Metal | Equivalent shared values; original resource admission unchanged | DiagnosticFailure for this selected join |
| WebGPU | Equivalent shared values and existing private original resource domain | DiagnosticFailure until separate cold/record/submit/completion joins |

ContentPlan1 remains upload-only; plans2/3 remain fixed-cell attachment/readback
profiles. Unknown versions, omitted opt-in, uninitialized/pending/stale/lost
content, malformed ranges, writes and under-budget requests must refuse. A read
plan cannot mint GPU texel validity or promote a public capability.

Validation checkpoint (2026-10-10): source344/344, rebuilt assertion-enabled MinGW
Host/Null/Vulkan synthetic/Metal stub4/4, package/root2/2 and Release Wasm/artifact2/2
pass. Genuine Chrome general composition, completed texture/view input and ordinary
geometry numerical readback/completion/retirement each pass. Both full feature
profiles2/2 pass119.27s; affected shader-usage/cancel/submitted-loss/map-loss4/4
pass19.38s. Generic completed texture custody reaches Closure8/Freeze6/Payload6.
CPU Pending ownership excludes committed-state observation; rollback preserves
the content generation/revision and restores CopyDst. One-less root/content/work
budgets, legacy lifecycle refusal, caller-drop and final quota retirement pass.
Closure216/232, Freeze217/233 and Payload218 retain independently authored native
custody; root/plan46 remains within Sources48, with812 allocation bytes including
initializer staging. Hot revalidation and CPU apply/abandon allocate0.

The first compile include-order failure and first browser view-source failure are
preserved. The shared validator uses dependent stage/access enum parameters to
avoid introducing a gpu_resources/gpu_commands include cycle. Texture ranges
retain separate parent sourceIndex and viewSourceIndex; their correspondence is
explicitly checked. No production source ceiling, quota, deadline or shader changed.
Public execution and native general shader execution remain false. Next exact cold
and lifecycle composition is DefinedOnly in original-general-native-cold-next-contract.md.
