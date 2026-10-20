# Original target runtime length and subrange prerequisites

Current closure audit (2026-10-06): the selected original operation,
available-target input and actual effective finite-interval lineage are Contract
Complete / Prototype Complete and WebGPU Backend Validated. Four deterministic
backend conclusions, complete source/target custody and native measurements are
mapped in `original-finite-subrange-closure-audit.yaml`. Selected finite execution
is separately validated by the original typed issuer contracts. Generic
OriginalRequest2, uniform/table/general ranges, NativeSync, visual, Platform and
Production remain unpromoted. The paragraphs below preserve earlier checkpoints
and their then-missing native joins.

Status: f2c2c1 cold operation correspondence and f2c2c2 original operation custody
are Contract Complete / Prototype Complete. Latest host49/49 with Null hidden-frame,
Wasm/both-browser11/11 and inventory204/204 passed; hostile113/deletion5 pass.
Enclosing available-target original pipeline input and native effective binding
requirements remain incomplete. The WebGPU finite-binding experiment remains
test-only API evidence; no original RHI, Platform or Production promotion follows.
See runtime-array-length-operation-contract.md and
original-runtime-length-operation-custody-contract.md. Pipeline input preparation
still requires complete cross-target packages, so unavailable-Metal artifacts fail
with SHADER-METAL-RUNTIME-LENGTH-UNSUPPORTED before input publication. The next
independently versioned available-target input contract must preserve ordinary
package validation and all original ownership/creation gates.

The original declaration facts from
OriginalTargetMemoryLoadRequest/1 do not satisfy this boundary. No execution,
request2, NativeSync or subrange write is opened by this document.

L2 distinguishes the original resource extent, the authored descriptor interval,
the frozen dynamic displacement, the effective bound interval, and the declared
access interval. Only the effective descriptor interval determines a runtime
array length. Access coverage cannot supply its size. Uniform and fixed-size
declarations have a minimum but no runtime length query. For a proved runtime
array, the declaration arithmetic is floor((bindingBytes - runtimeOffset) /
runtimeStride). The prefix and at least one target element must fit the target
minimum first. Tail bytes smaller than a stride do not add an element.

The existing WgslBufferDeclarationLayout.runtimeArrayLength implements the scalar
arithmetic. Calling it on copied facts is not a native or original receipt. Reuse
it instead of adding another calculator. DescriptorBufferBindingRangeContract
already resolves static and dynamically displaced intervals without overflow.
DescriptorWholeBufferBinding::Limits already distinguishes queried alignment
and maxima. These utilities must not create a second owner or proof inventory.

Before a native length conclusion, join the exact original buffer declaration,
its complete memory representation, active pipeline/layout/physical binding and
all selected stages. Separately prove the source length operation and target
length operation refer to that same runtime member, and that the native descriptor
payload consumes the exact effective offset and unchanged authored size. Do not
infer the operation from the presence of a runtime declaration or an identifier
spelling. Hashes and equal stride alone do not prove the executable mapping.
The declaration parsers intentionally do not validate function bodies.

No runtime length operation is accepted by default. Define a versioned bounded
operation lowering or a complete-program contract before it can become available.
It must retain source/target operation and member identities under the existing
artifact owner; unsupported control/data-flow or ambiguous roots diagnose. A
test-only native API probe can substantiate an approved mapping, but cannot issue
production authority. No generated WGSL/MSL editing or name-based substitutions
may bypass Slang as the only shader source.

| Backend | Current deterministic conclusion | Required evidence |
| --- | --- | --- |
| Null | ValidatorOnly; native length unavailable | Resolve/length negatives on actual retained resource extents; no dummy native proof |
| Vulkan | DiagnosticFailure; original slice/operation join absent | Exact descriptor offset/range and OpArrayLength binding/member mapping, with genuine payload results |
| Metal | DiagnosticFailure; arbitrary pointer length absent | Approved explicit length ABI or complete-program equivalence; encoder offset alone is insufficient |
| WebGPU | DiagnosticFailure; original slice/operation join absent | Exact buffer offset/size, frozen dynamic displacement and mapped arrayLength operation, with genuine payload results |

Slang2026.17 StructuredBuffer.GetDimensions currently reports E36107 for MSL.
Retain that explicit target diagnostic; do not derive a length from a pointer,
resource size, unrelated quota or host replay. When an approved backend is absent,
the corresponding capability remains false with a stable diagnostic. A native
API experiment cannot promote an L2 capability.

Only after memory and native length requirements are satisfied may independently
opted-in OriginalDescriptorBufferBinding request2 extend the existing accepted
wrapper's original validation. Preserve request1's whole-buffer and nonzero
dynamic rejection. Validate authored base and dynamic displacement separately
against the frozen queried alignment; validate unchanged authored size against
the target minimum and queried maximum; validate both intervals against the
actual original buffer extent. Caller limit/fact copies supply no authority.
Consume the exact creation/layout/device lineage already retained by the accepted
wrapper and the exact original shader artifact; do not expose native handles or
add an owner/root/seal/journal/source family/native issuer. This prerequisite
alone does not grant descriptor Source/Inputs/state/recording authority.

Tests must include prefix offsets, padded vector3 elements, one/multiple elements,
partial stride tails, independently aligned static/dynamic offsets, end/overflow,
same-size incompatible types, foreign pipelines/buffers, both graphics stages,
copied/legacy/unavailable facts, mutation/loss/retirement, hot allocation0 and cold
quota cleanup. A Slang length probe must publish SPIR-V, MSL or its diagnostic,
WGSL and reflection and be verified on genuine applicable backends. Keep quick
contract and Null hidden-frame paths, both actual browser modes and explicit
four-backend negative assertions. RenderDoc captures or the deterministic tool
unavailable diagnostic remain required; no Platform/Production promotion follows
from declaration or API-only observations.
