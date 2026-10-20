# Original descriptor factory and content /3

Status: Contract Complete and Prototype Complete for original CPU factory
lifetime and exact successful-content observations. Verified host15/15,
Wasm/browser8/8 and inventory180/180; both browser modes require actual Null
and WebGPU factory/content checks and genuine loss-gate rejection. Vulkan
descriptor source passes syntax checking only. Evidence:
`build/web-bindless-apple-debug/artifacts/bindless-continuation/original-descriptor-factory-content-validation.yaml`.
No typed Backend Validated, Platform Validated or Production Ready claim. This cold,
explicit Prototype prerequisite extends the existing registry, not command
recording, the canonical graph, resource state or native execution authority.

OriginalResourceRegistration/3 adds independent DescriptorArena, Descriptor and
DescriptorTable ceilings. Versions 1 and 2 reject nonzero descriptor budgets.
A child budget requires an arena budget. Arenas cost two retained owners and
children three (wrapper, record and exact core shared arena owner). Resource
and pipeline /1-/2 policies retain their semantics under /3. Non-ID wrappers use
exact L2 object/kind/issued monotonic registration generation, never allocation
indices or generated GPU IDs.

Only explicit GpuDevice original factory/allocation entry points issue records.
They pre-reserve existing kind/owner/metadata capacity and validate original
facade, registry and canonical core shared arena owner before backend calls.
Unregistered, foreign, forged, stale, missing and retired inputs reject. The
existing actual backend arena LifetimeEpoch issues the allocation ticket; each
child must expose and match the same current epoch. Reset invalidates child
registration even while a true owner retains the arena. New children after reset
get new registration generations. No duplicate generation or mirrored registry
is introduced. Original native-loss subscriptions reject descriptor creation
with `RHI-ORIGINAL-DESCRIPTOR-NATIVE-LOSS-JOIN-MISSING` until the exact join exists.

Backend factories must restore their actual logical allocation counters when
wrapper/native storage creation fails. Reuse core mutation snapshots for this
cold factory accounting only. This transaction never owns command state, sources,
root/seal or the canonical command journal. Native failed candidates release
their private ownership; consumed original registration generations never recycle.
Caller wrapper/storage allocations remain on their original core quotas, and
registration records use the original metadata quota. Factories must deep-copy
nested descriptor layout fields through core PMR; no ambient default allocator.

DescriptorContentObservation/1 returns a privately issued, fixed-size copy of
the complete successful set/table write from existing backend storage. It captures
the exact wrapper's weak lifetime and final L2 content revision. Successful writes,
including identical writes, invalidate prior observations through that revision;
failed writes preserve them. Arena reset/destruction and wrapper destruction
invalidate observations. All set fields, including resource state, array element
and enabled/disabled physical-lowering fields, are retained exactly. All table
fields are retained exactly. Never reconstruct a lost field from defaults or
resource/backend heuristics. Null must retain full successful writes; Vulkan
tables must replace their ID-only storage with complete writes. Metal/WebGPU
reuse their existing successful candidate publication.

Observation and validation are externally serialized, allocation-free, nonblocking
and perform no IO. The observation retains no wrapper/native/resource owner.
It proves content identity only; resource liveness, registered resource generation,
layout, explicit access/state/range and canonical Source ownership must be joined
separately by the typed Source adapter. Ordinary observations cannot grant original
factory identity or native authority. Unimplemented backend read/epoch accessors
return explicit missing observations/tickets and fail closed.

| Backend | Logical factory lifetime and exact content | Native loss join | Canonical typed execution |
| --- | --- | --- | --- |
| Null | Equivalent CPU registration/content, validator only | DiagnosticFailure | DiagnosticFailure |
| Vulkan | Equivalent CPU registration/content of actual wrapper | DiagnosticFailure | DiagnosticFailure |
| Metal | Equivalent CPU registration/content of actual wrapper | DiagnosticFailure | DiagnosticFailure |
| WebGPU | Equivalent CPU registration/content of actual wrapper | DiagnosticFailure | DiagnosticFailure |

Tests must cover actual factories, original allocation/control-block retention,
four outcomes and closed authority; ordinary/foreign/forged/missing inputs;
version/parent/kind/owner budgets; actual epoch reset and destruction; complete
array/table write fields, identical/failed/rewrite revision behavior and expired
weak observations; exact/one-less/exhausted quotas and failed factory counter
rollback; hot observations under exhausted quotas and disabled ambient std PMR.
Preserve fast unit/contract, two-frame Null hidden smoke and both real browser
smokes with prior genuine map/publication checks. No visible rendering or shader
change is part of this prerequisite. Typed source freezing, positional joins and
native execution remain open; CPU receipts do not imply Backend Validated,
Platform Validated or Production Ready.
