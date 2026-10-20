# RuntimeArrayLengthOperations/1

Status: Contract Complete / Prototype Complete for f2c2c1. Verified 2026-10-05
host43/43 including Null hidden-frame, final quick4/4, Wasm and both actual browser
modes11/11, inventory204/204. Evidence: build/web-bindless-apple-debug/artifacts/
bindless-continuation/runtime-length-operations-validation.yaml. Actual WebGPU
test-only finite bindings returned counts1/2/2/3/3 and stride16 with base/dynamic
offsets and partial tails; this validates that API observation only, not original
RHI length custody or execution. Not Platform Validated / Not Production Ready.

This cold operation inventory is a required part of f2c2c;
it does not complete effective native binding, request2 or execution proofs.

Extend the existing WGSL declaration parser to return borrowed exact variable
and optional final runtime member names alongside its physical declaration.
Names originate from the original target bytes, never a naming convention. The
same parser validates aliases, final runtime placement and full typed graphs.
All reference outputs clear on failure. No extra parser for declarations or
second layout calculator is allowed.

The operation verifier consumes exact original SPIR-V bytes, previously verified
binary declaration facts and target reference facts. Audit every OpArrayLength
and every target arrayLength invocation. The version1 language admits direct
global storage roots and direct optional final runtime members only. Source
result type must be uint32, source structure operand must be the exact original
global variable, and the literal member must select its final RuntimeArray type.
Join source set/binding through the verified physical map to the exact target
root, preserving declared runtime prefix and stride. Indirect pointers,
function parameters, aliases of resource pointers and unresolved references
produce deterministic diagnostics; they cannot be resolved by variable names.

Target arrayLength must take &global or &global.finalRuntimeMember, with exact
token framing. Local/parameter/global shadows of resource/builtin names reject.
Count complete source and target inventories per logical binding, with authored
operation/token budgets and core PMR storage. Comments are lexical trivia. An
inventory does not certify use of returned results, whole-program equivalence,
control-flow execution counts, race safety or any native descriptor interval.
No successful prefix of a failed inventory may issue facts.

Freeze operation facts only in existing artifact owners after original memory
and map joins. Returned copies confer no authority. The initial reusable
verifier is a cold processor; original artifact integration and native bound
interval validation are separate remaining f2c2c requirements.

Null is validator-only. Vulkan, Metal and WebGPU remain DiagnosticFailure for
original runtime length/native slices until their effective descriptor payload
and operation contracts are consumed. Slang2026.17 rejects GetDimensions MSL
with E36107 for structured and byte-address read/write buffers. Keep that
diagnostic; do not manufacture MSL or infer a pointer length. Only Slang may
generate target shader code.

Tests cover real Slang OpArrayLength/arrayLength output, direct and final-member
roots, alias declaration types, runtime prefixes/padded strides, wrong roots,
wrong member/type/result, signedness/physical maps, missing/extra invocations,
shadow names, pointer indirection, malformed framing, complete output clearing,
authored budgets, hostile PMR and exhausted quotas. Keep quick contract and Null
hidden-frame paths; applicable real-browser operation observations must retain
all prior gates. No Backend/Platform/Production promotion follows.

The four Slang compiler fixtures generate SPIR-V/WGSL and exact reflected YAML;
all MSL attempts retain E36107 under the explicit test diagnostic contract, with
no fabricated target bytes or capability bypass. These are compiler test programs,
not production shader resources. The existing original publication contract
cannot yet retain such unavailable-MSL programs. Continue
original-runtime-length-operation-custody-contract.md; the enclosing native
operation/effective-bound-region and original request2 requirements remain open.
