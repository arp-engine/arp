# Original native allocation observation /1

Status: Contract Complete and scoped Metal Platform Validated; no production
gate grant. Exact source epochs and remaining limits are recorded in
`build/bindless-wocc-ibl-shared-metal1/current-report.json`.

An optional typed intent on OriginalTextureContentCreationBudget selects this
cold substrate only for ContentCreation3 whole sampled uploads. Existing
creation versions, unselected native paths and exact query refusals are kept.
The native ceiling is explicit, nonzero and preflighted against the device's
remaining budget before a candidate is created. The caller must select it from
its declared workload/device budget; a heap requirement is not an independent
texture allocation bound. It grants no creation/registration/content/state,
completion, sampling, update or alias authority by itself.

The backend reserves the complete ceiling before native creation and holds one
typed allocation token. It independently checks original device, texture shape,
format, storage, usage, tracked hazards, absence of an external heap and actual
allocatedSize. Zero or above-ceiling observations fail without publication;
native objects retire before the reservation. Accepted observations shrink the
same live allocation charge without allocation, release/reacquire gaps or a
new owner. Copies with stale size or foreign ledger identities cannot shrink.
The reservation peak remains observable and actual bytes retire with the last
original resource/view/command owner. No hot draw/submit behavior changes.
CPU token construction precedes the no-throw ledger publication: exhaustion
must preserve live bytes/count, historical peak and existing owners, and allow
a subsequent unpressured retry. A failed CPU owner is not a GPU budget rejection.

| Backend | Lowering /1 |
| --- | --- |
| Null | Validator only; no native candidate or creation grant |
| Vulkan | Explicit unsupported optional profile; existing exact native creation remains |
| Metal | Explicit prototype opt-in; reserve ceiling, genuine independent texture, observe then shrink original token |
| WebGPU | Explicit refusal: no native allocation size query; existing texel budgets remain separate |
| WebGL | Explicit unavailable |

The real Mac probe records Cube64-array/mip7 heap requirement297216 bytes vs
standalone allocated311296, and LUT128 requirement131584 vs allocated147456.
No constant padding, inferred alignment rule or relaxed validator is permitted.
The optional intent is the only route selecting observation; a legacy exact
heap-size budget still refuses those standalone resources.

Required validation: five-backend/version/opt-in/profile/bound tests, shared
ledger shrink/refusal/retirement tests, typed Metal allocator foreign/stale
owner tests, real actual-size clean/fault IBL, one-less creation preserving
device accounting, callback completion/retirement and Mac regressions. Capture,
shader and platform evidence must retain exact inputs. Full workload budgets,
Traditional parity, the common engine gate and renderer/WOCC remain open.
