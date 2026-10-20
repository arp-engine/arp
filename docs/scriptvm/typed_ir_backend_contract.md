# ArpScript Typed IR Backend Contract

**Specification status:** Contract Complete  
**Contract version:** 1  
**Current schema:** `TypedIrVersion = 7`, `SemanticAbiVersion = 1`
**Product status:** ScriptVM AOT remains `Implementation In Progress`

This document is the normative contract between the ArpScript frontend and
every execution backend. It takes the useful verification model from C# and
ECMA-335 CIL, but it does not make ArpScript a CLR language and does not make
Typed IR binary-compatible with CIL.

The C# reference means:

- a typed evaluation stack with a single valid stack state at each CFG join;
- explicit metadata identities instead of process pointers;
- explicit protected regions and deterministic unwinding;
- managed object identity and shared-reference semantics;
- checked conversions and arithmetic where the language requires them;
- async and generator state represented as versioned state, not a native PC.

ArpScript keeps its own type system, diagnostics, reflection model, memory
quota, module lifecycle and generator protocol. When this document and a C# or
CLR behavior differ, this document is authoritative.

---

## 1. Normative Language

`MUST`, `MUST NOT`, `REQUIRED`, `SHOULD`, `SHOULD NOT` and `MAY` are normative.

- **Frontend** means `ScriptFrontend` plus `ScriptLowerer` after semantic
  analysis.
- **IR** means a validated `TypedIrModule`, not `Program`, bytecode or generated
  C++.
- **Backend** means any consumer that executes or translates IR: VM bytecode,
  DirectNative C++, Web/Wasm, a future JIT, or an analysis-only verifier.
- **Semantic runtime** means `ScriptSemanticRuntime` and the versioned runtime
  contracts referenced by `SemanticAbiVersion`.
- **Observable behavior** includes return values, mutations, alias identity,
  host effects, diagnostics, source positions, suspension, unwinding,
  continuation ownership, resource use and no-replay behavior.

Examples in this document are explanatory. Rules containing normative terms
and the conformance tables are authoritative.

---

## 2. Scope And Non-Goals

### 2.1 In scope

This contract defines:

1. module, function, type, storage and callable identities;
2. stack-machine and CFG invariants;
3. operation and effect semantics;
4. calls, reflection, exceptions, suspension and generators;
5. memory quota, lifetime and ownership;
6. canonical identity and versioning;
7. validation, lowering outcomes and stable diagnostics;
8. the minimum differential and platform evidence for backend conformance.

### 2.2 Out of scope

This contract does not define:

- CIL assembly format, CLR metadata tokens, CTS, CLS or .NET assemblies;
- garbage collector implementation or object layout;
- a public serialized Typed IR authoring format;
- backend-private register allocation, instruction selection or optimization;
- source syntax already owned by the ArpScript language specification;
- a fallback right to run bytecode when native lowering fails.

Typed IR is currently an in-memory contract. AOT package metadata remains YAML.
Introducing a serialized IR format requires a separate versioned wire contract;
JSON authoring is not introduced by this specification.

---

## 3. Contract Hierarchy

The only legal implementation order is:

```text
ArpScript source
  -> lexer/parser
  -> semantic analysis
  -> ScriptTypedModule
  -> canonical TypedIrModule
  -> common structural and semantic verifier
  -> backend capability proof
  -> backend-private lowering
  -> execution
```

The following rules are absolute:

1. The sema-validated AST and its canonical Typed IR define language intent.
2. A backend MUST NOT derive production semantics from bytecode patterns,
   generated C++ text, platform names or native defaults.
3. A backend-private optimization MUST preserve every observable behavior.
4. Backend selection MUST finish before the first observable effect.
5. A failed or suspended execution MUST NOT be replayed on another backend.
6. Interpreter bytecode is produced only by `InterpreterBytecodeLowerer` from
   validated Typed IR. AST-to-bytecode and bytecode-to-IR adapters do not exist.
7. New language behavior is incomplete until every registered backend has an
   explicit outcome and conformance test.

For Interpreter execution the backend-private tail is fixed:

```text
validated TypedIrModule
  -> InterpreterBytecodeLowerer
  -> validated, versioned Program
  -> Vm
```

`Program`, `Instruction`, and `Opcode` are Interpreter-private contracts. They
MUST NOT appear in the IR layer or become a second source compilation path.

---

## 4. C# / CIL Reference Mapping

| C# / CIL concept | ArpScript Typed IR contract | Deliberate difference |
| --- | --- | --- |
| Evaluation stack | Typed stack bounded by `maxStack` | Runtime values use `Value`; no CLR stack types |
| Method signature | `TypedIrSignature` plus SHA-256 signature hash | No CLR calling convention or metadata token |
| Locals and arguments | Stable local slots and `LocalKind` | Captures are shared `ScriptCell` identities |
| Assembly/module identity | `moduleId`, `moduleVersion`, dependency tables | No assembly probing or implicit binding redirect |
| Metadata token | Stable function/type/binding ID | Never a pointer, RTTI hash or registration index |
| Object reference | Shared `Value` identity | Engine reflection object, not CLR object layout |
| `checked` arithmetic | Shared checked semantic helpers | Overflow behavior is always contract-selected, not compilation-mode selected |
| Exception clause | `TypedIrExceptionRegion` | Catch filters use stable Script diagnostic names |
| `async` state machine | Canonical continuation and safepoint schema | No `Task` requirement and no native-PC persistence |
| Iterator state machine | Generator frame plus delegation metadata | ArpScript send/throw/close protocol is explicit |
| Verifiable IL | Common IR validator plus backend proof | Unsupported valid IR may fail backend capability deterministically |

No backend may justify a behavior only by saying it matches C#, C++, Wasm or a
native ABI. It must match the ArpScript contract.

---

## 5. Module Contract

A `TypedIrModule` is immutable after validation and contains:

- non-empty `moduleId`;
- positive `moduleVersion`;
- exact `typedIrVersion` and `semanticAbiVersion`;
- a bounded global slot table;
- an explicit stable `moduleInitFunctionId`, or an empty ID;
- dependency, reflection dependency and host capability tables;
- callable bindings and script class schemas;
- one or more functions;
- a canonical structural fingerprint.

### 5.1 Identity

- Function IDs MUST equal `arp-fn:<moduleId>/<functionName>` for local
  functions.
- Module, function, class, callable and safepoint IDs MUST be stable across
  vector relocation, address-space changes and deterministic rebuilds.
- IDs MUST NOT contain native pointers, RTTI hashes, timestamps, absolute
  staging paths or registration-order counters.
- A dependency call MUST resolve only through the declared dependency and
  callable binding tables.
- Duplicate global slots, names, function IDs, binding IDs, class names or
  safepoint IDs are invalid IR.

### 5.2 Module initialization

`moduleInitFunctionId` is the only module initialization entry. Backends
MUST NOT infer initialization from a name such as `main` or `<module>`.

Initialization is exactly-once per module instance. A failed initializer MUST
NOT expose partial globals as a successfully initialized module. Re-entering a
failed initializer requires an explicit new module instance or retry contract;
it is not an implicit backend decision.

### 5.3 Version retention

Functions, closures, generators, awaitables and reflected exports retain the
exact module version that created them. A backend MUST NOT silently bind an old
frame or closure to a newer module version. Old versions are reclaimed only
after all owners and retained runtime state are released.

---

## 6. Type System

### 6.1 IR types

The v6 type set is:

| IR kind | Runtime meaning | Nullability |
| --- | --- | --- |
| `Any` | Dynamically checked `Value` | Encoded by the carried value |
| `Int` | Signed 32-bit integer | Non-null |
| `Int64` | Signed 64-bit integer | Non-null |
| `Float` | IEEE-754 binary64 | Non-null |
| `Bool` | Boolean | Non-null |
| `String` | Immutable script string | Reference/value carrier per runtime |
| `Bytes` | Immutable byte sequence | Reference/value carrier per runtime |
| `Object` | Reflection-backed object | May be nullable only when declared |
| `List` | Mutable ordered `Value` container | May be nullable only when declared |
| `Dict` | Mutable keyed `Value` container | May be nullable only when declared |
| `Function` | Function, closure or bound method value | May be nullable only when declared |
| `Class` | Instance of exact `className` | May be nullable only when declared |

`TypedIrTypeRef.nullable` is semantic. A backend MUST NOT accept nil for a
non-null parameter because its native representation happens to permit null.
For `Class`, `className` MUST be non-empty and resolve through the module or
reflection contract.

### 6.2 Assignment compatibility

Assignment and argument normalization use these rules:

1. exact type is accepted;
2. `Int -> Int64` is an exact widening conversion;
3. `Int -> Float` and `Int64 -> Float` use the shared conversion contract;
4. nil is accepted only for a nullable/reference contract that permits it;
5. `Any` requires runtime validation at the consuming operation;
6. class values require exact declared/reflection binding compatibility;
7. all other conversions require an explicit `Convert*` operation.

Backend-native implicit conversions are forbidden.

### 6.3 Numeric behavior

- Integer overflow returns `IntegerOverflow`; signed native overflow MUST NOT
  be used to implement wraparound.
- Division or modulo by zero returns `DivisionByZero`.
- Float representation is binary64 on every backend. A backend with a
  different native default MUST lower to an equivalent binary64 operation or
  reject the operation.
- Shift width, floor division, power, comparison and conversion semantics are
  supplied by the matching `ScriptSemanticRuntime` operation. Backends may
  inline them only after a proof and differential coverage.
- Fast-path operations have exactly the same result and diagnostic as their
  non-fast-path semantic operation.

### 6.4 Reference and alias semantics

ArpScript assignments and parameters preserve shared reference identity.
Mutable list, dict, instance and captured-cell mutations are visible through
all aliases. Backends MUST NOT introduce deep copy, copy-on-write or store-time
clone behavior unless a future IR operation explicitly requests it.

`const` is a frontend reassignment rule. It does not imply deep immutability of
the referenced object.

---

## 7. Function And Storage Contract

A `TypedIrFunction` contains an exact signature and bounded storage metadata.

- `parameterCount` MUST match the fixed/variadic signature contract.
- `localCount` includes parameters, result slots, locals and captures.
- `captureCount` MUST be within `localCount`.
- Every local slot has one `LocalKind`, type and live interval.
- `maxStack` is a hard upper bound, not telemetry.
- Reading an uninitialized local is invalid IR.
- A slot MUST NOT have incompatible types at overlapping live intervals.
- Variadic arguments are normalized into the declared list shape once, at the
  public entry boundary.
- A no-result return uses the explicit zero-result `Return` contract. A backend
  MUST NOT synthesize nil or an undefined `Value` as an observable result.

Captured variables use shared cell identity. Moving a function between
backends MUST preserve the same cells; copying the current captured values into
new independent cells is not equivalent.

---

## 8. Evaluation Stack And CFG

Typed IR v6 is a CIL-style stack IR, not SSA.

### 8.1 Stack invariants

For every reachable operation:

1. input stack depth and types are statically known;
2. pops never underflow;
3. output depth does not exceed `maxStack`;
4. all incoming edges to a basic block have the same stack depth;
5. corresponding incoming stack values have one compatible IR type;
6. handler entry stack shape follows the exception contract;
7. return stack shape matches the function signature;
8. unreachable blocks are still structurally valid and cannot smuggle an
   invalid target into metadata.

There is no implicit phi operation in v6. Equal stack positions and local
slots at a CFG join are the merge locations. If types cannot be merged by the
assignment rules, the IR is invalid.

### 8.2 Basic blocks

- Block IDs are unique and stable.
- Successors are explicit and refer to existing block IDs.
- A terminator has no implicit fallthrough.
- A conditional branch has exactly its declared target and fallthrough
  successor.
- Operation indices are the flattened canonical block/operation order used by
  safepoints and protected regions.
- Branch target operands and the successor table MUST agree.

A backend MUST consume validated targets. It MUST NOT recognize control flow
from a familiar instruction sequence.

Conditional branches consume a `Bool` under the typed contract. Native C++,
Wasm integer truthiness or pointer truthiness is not a substitute for the
ArpScript boolean operation.

### 8.3 Source positions

Source positions are 1-based; zero means unknown. Any operation that may raise,
suspend, call host code or cross a backend boundary SHOULD have a valid source
position. Safepoint switchability requires valid source metadata.

Diagnostics MUST report the operation source when it is available. A backend
may add native debug information but may not replace the canonical source.

---

## 9. Operation Contract

`TypedIrOperationRecord.opcode` (`TypedIrOpcode`) is the exact semantic opcode
identity. `operation` is its broad category. The data-driven
`TypedIrOperationDescriptor` is the single registry for name, category,
operand fields, stack inputs/outputs, control flow, call, effects and suspend
properties. Unused record fields MUST have canonical zero/empty values.

Interpreter `Opcode` is a private lowering target and MUST NOT define IR
semantics. Adding, removing or reinterpreting a `TypedIrOpcode` requires a
`TypedIrVersion` change unless the change is proven non-semantic and backward
compatible.

### 9.1 Operation families

| Family | Current operations | Required implementation source |
| --- | --- | --- |
| Constants/load | `LoadNil`, `LoadBool`, `LoadInt`, `LoadInt64`, `LoadFloat`, `LoadString`, local/capture/global loads | Exact literal/slot contract |
| Store | local/capture/global stores, `Pop` | Shared alias and initialization rules |
| Arithmetic | `Add/Sub/Mul/Div/Mod/Pow/FloorDiv`, `Neg` | `ScriptSemanticRuntime` or proven equivalent |
| Typed arithmetic | `IntAdd/IntSub/IntMul/IntDiv/IntMod` and local fast paths | Checked scalar helper plus parity proof |
| Bitwise | and/or/xor/clear/shifts | Shared semantic runtime |
| Compare | generic and typed comparisons | Shared semantic runtime |
| Convert | `ConvertInt/Int64/Float/String/Bool/Bytes` | Shared conversion runtime |
| Control flow | jumps and typed conditional fast path | Explicit CFG and stack proof |
| Calls | direct, indirect, method, reflection and host | Callable binding contract |
| Function values | function, closure and method construction | Stable target and capture provenance |
| Containers | list, dict, range, append/delete/clear | Shared semantic runtime and quota |
| Access/address | index, field, local/capture/global address, dereference/store indirect | Versioned pointer target contract |
| Module | module load and export load | Declared dependency and registry contract |
| Exception | push/pop handler and throw | Explicit protected region contract |
| Suspend | `Yield`, `Await`, range delegation | Safepoint/continuation contract |
| Return | `Return`, `ReturnInt` | Exact result arity/type |

### 9.2 Stack consumption order

Operand order is observable when conversion, index lookup or host behavior can
fail. Backends MUST use the canonical order defined by the opcode and shared
semantic helper. For example, `MakeRangeFromStack` consumes `end` before
`start`, and index store consumes value, index and base in its defined order.
A backend MUST NOT reorder potentially raising or effectful operands.

### 9.3 Operation equivalence

A backend-private instruction is equivalent only if it preserves:

- result type and value;
- integer/float edge cases;
- alias identity and mutation order;
- diagnostic code, source and failure boundary;
- effect order;
- quota charging and release;
- suspension point and no-replay state.

Matching only the successful scalar result is insufficient.

---

## 10. Calls And Callable Bindings

Every non-trivial callable edge is described by `TypedIrCallableBinding`:

- stable `bindingId` and `targetId`;
- origin: host capability, reflection member or script function;
- SHA-256 signature hash;
- fixed arity and suspension capability;
- generator bridge flag where applicable;
- nonzero lifetime version.

`TypedIrCallableOrigin::ModuleGlobal` is reserved and invalid in the v6
validator. It MUST NOT be emitted or treated as an implicit module lookup.

### 10.1 Direct calls

A local direct target must exist in the same module and its signature hash,
arity and capture contract must match. An external script target must name a
declared dependency. Vector index alone is never a stable target identity.

Arguments are evaluated left-to-right, normalized once, then committed to the
callee frame. A failed normalization MUST NOT enter the callee or publish a
partial frame.

### 10.2 Indirect calls and provenance

`CallValue` may claim an exact closure target only when dataflow proves the
same-module function ID and capture count. `kScriptMethodProvenanceCaptureCount`
identifies the explicit zero-capture ScriptMethod case.

Unknown provenance is valid dynamic IR but is not automatically eligible for
DirectNative. A backend must either execute the full dynamic contract or
return `AotSwitchUnsupported` before effects.

### 10.3 Host and reflection calls

- Host calls require a declared host capability and exact binding.
- Reflection calls require an exact canonical reflection contract and owner.
- No call binding may be resolved by source substring, similar name,
  registration order or pointer identity persisted in an artifact.
- A call marked `maySuspend = false` must not suspend.
- A call that may write host state is never replayable.
- Missing service, owner, signature or lifetime version fails before call.

---

## 11. Objects, Classes And Reflection

`TypedIrScriptClassSchema` is the authoritative schema for script classes:

- class name is unique;
- fields are unique and ordered canonically;
- method name maps to stable function ID;
- constructor ID is explicit; empty means the language default constructor.

An instance preserves reference identity across backends. Field access uses the
declared schema or exact reflection binding. A backend MUST NOT infer fields
from host layout or silently create a missing field.

Reflected/native values require a versioned callable/type binding and owner
lifetime. Opaque values that cannot satisfy memory and lifetime accounting are
rejected with the existing deterministic unsupported diagnostic.

---

## 12. Effects And Ordering

The effect mask includes:

- `ReadsGlobal`, `WritesGlobal`;
- `ReadsHost`, `WritesHost`;
- `MayRaise`, `MaySuspend`.

The frontend MUST conservatively include every possible effect. A backend may
refine an effect only with a versioned proof; it must not infer purity from an
unknown binding.

Operations with observable effects retain source evaluation order. Optimizers
MUST NOT move, duplicate or remove them across calls, branches, handlers,
safepoints or quota checks unless equivalence is proven.

After `WritesGlobal` or `WritesHost`, any backend failure is terminal for that
invocation. Retrying in another backend would replay effects and is forbidden.

---

## 13. Exceptions And Diagnostics

### 13.1 Protected regions

`TypedIrExceptionRegion` defines a half-open protected range
`[startOperation, endOperation)`, handler operation/block, optional stable catch
diagnostic name and source.

- Regions must be properly nested or disjoint; partial overlap is invalid.
- Push/pop handler operations and region metadata must be one-to-one.
- Handler targets must be valid block entries.
- A typed catch name must map through `scriptDiagnosticCodeFromName`.
- The innermost matching region handles first.
- Unmatched errors unwind callers without replay.
- Cleanup of frames, captures, child generators and quota leases occurs once.

### 13.2 Stable diagnostics

For the same validated IR and inputs, all backends MUST produce the same:

- `ScriptDiagnosticCode`;
- canonical message where the contract defines one;
- source file, line and column;
- effect commit boundary;
- terminal or suspended status.

Backends may attach private telemetry, native stack traces or symbol data, but
these do not replace the stable diagnostic and must not expose nondeterministic
pointers in the canonical message.

An unsupported backend shape uses an existing stable failure such as
`AotSwitchUnsupported`; it must not be reported as a language runtime error or
successful degradation.

---

## 14. Suspension, Await And Generators

Suspension is a semantic state transition, not an error and not a native stack
snapshot.

### 14.1 Safepoints

A `TypedIrSafepoint` has a stable ID, operation index, resume label, source and
canonical live slots.

- Every `Yield` or `Await` in a `Safepoint` function has exactly one safepoint.
- A safepoint resumes after the committed operation boundary defined by the
  opcode.
- Canonical slots contain all state required for resumption.
- A continuation records module/function/artifact identity and owner.
- Native PC, C++ stack address, Wasm stack address and pointer-sized frame
  layout are not canonical state.

### 14.2 Switchability

| Value | Contract |
| --- | --- |
| `FunctionBoundary` | Backend choice only before entry |
| `Safepoint` | Migration only at complete declared safepoints |
| `ArbitraryPc` | Every operation boundary has a validated deoptimization map |

`ArbitraryPc` MUST NOT be inferred from ordinary CFG metadata. If a backend
cannot materialize every required local, operand, handler, capture, caller and
effect-commit state, it rejects the switch.

### 14.3 Await

Await consumes one awaitable. It either:

1. completes and pushes one normalized result; or
2. commits one pending continuation and returns suspended.

Completion, exception injection, cancellation and close are delivered once.
Polling or migration must not repeat the original host call.

### 14.4 Generators

Generator state includes frame storage, resume label, pending injection,
handler state, captures, delegated child, owner and quota leases.

`yield from` is authorized only by explicit `TypedIrDelegationSite` metadata
and `hasYieldFrom`. Backends MUST NOT recognize delegation by scanning a
`RangeNext + Load + Yield + Jump` sequence.

Delegation forwards send/throw/close only when the child bridge declares the
required versioned capability. Unsupported child kinds fail closed before the
parent publishes a partial continuation.

---

## 15. Memory, Quota And Lifetime

Memory limits are semantic execution limits, not advisory telemetry.

Every backend must account for:

- invocation arguments and pending result;
- globals retained by the module instance;
- locals, operand stacks and handler stacks;
- captures and shared cells;
- strings, bytes, lists, dict nodes and contained values;
- instances and reflected/native values;
- awaitables, generators, delegated children and continuations;
- backend frame objects and external allocations retained by script state.

Rules:

1. The current quota is the single source of truth.
2. One real allocation is charged once by stable allocation identity.
3. Shared and cyclic graphs are traversed without double charging or loops.
4. External allocations require an explicit lease/descriptor.
5. Admission is transactional: quota failure exposes no partial result,
   continuation, global mutation or owner transfer.
6. Frame count and frame bytes are separate limits.
7. Telemetry counters MUST NOT be added to already charged live bytes.
8. Release, replacement and adoption transfer or resize ownership exactly
   once.

Quota failure is `ResourceLimitExceeded` unless a more specific existing
budget diagnostic applies.

---

## 16. Canonical Form And Fingerprints

### 16.1 Canonical form

Set-like collections are sorted by stable logical key. Semantic sequences keep
their language order. Strings are length-delimited in any serialized contract;
newline concatenation is not a canonical serializer.

Canonical form includes every semantic field:

- versions and module identity;
- globals, dependencies and capabilities;
- callable and class contracts;
- function signature/storage/effects/switchability;
- operation identity, operands, source and effects;
- CFG successors;
- local metadata;
- safepoints, exception regions and delegation sites.

Unknown fields are not silently ignored. A consumer either understands their
versioned semantics or rejects the IR version.

### 16.2 Two identities

The in-memory `computeTypedIrFingerprint()` fingerprint detects structural
drift and is validated before backend consumption. It is not a cryptographic
artifact identity.

Published artifacts additionally use SHA-256 over the canonical build contract,
including source, dependencies, reflection, IR fingerprint, compiler/toolchain,
host ABI and include contract. A backend MUST NOT substitute size/mtime or an
absolute path for content identity.

---

## 17. Validation Pipeline

Validation has three mandatory layers.

### 17.1 Structural validation

`validateTypedIr()` validates version, identity, table uniqueness, ranges,
references, signatures, CFG metadata, regions, safepoints, delegation metadata
and stored fingerprint.

Failure returns `TypedIrVersionMismatch` or `TypedIrInvalid`. No backend runs.

### 17.2 Semantic verification

The common verifier proves:

- stack depth/type at every operation and join;
- definite local initialization;
- slot and result compatibility;
- call signature and provenance;
- effect conservatism;
- handler entry/unwind shape;
- safepoint live-state completeness;
- resource bounds that are statically knowable.

Semantic verification is backend-neutral. A backend-specific matcher cannot
replace it.

### 17.3 Backend capability proof

After common validation, each function receives exactly one outcome:

| Outcome | Meaning |
| --- | --- |
| `Native` | Backend directly preserves the full contract |
| `Equivalent` | Versioned lowering/emulation preserves every observable behavior |
| `Prototype/Legacy` | Explicit non-production opt-in only |
| `DiagnosticFailure` | Stable fail-closed result before execution |

There is no implicit `Fallback` outcome. A valid function may be unsupported by
a backend; that is a capability result, not invalid IR.
The current source tree has no production Legacy or generic executor;
`Prototype/Legacy` is a status category, not permission to restore one.

### 17.4 Backend conformance record

Every backend MUST publish a versioned conformance record at build/test time.
It contains at least:

- stable backend ID and backend contract version;
- accepted Typed IR and semantic ABI versions;
- outcome for every opcode and constrained shape;
- numeric width/overflow and float contract;
- object identity and callable binding model;
- exception and suspension support level;
- memory quota and continuation limits;
- stable diagnostic used for each rejected shape;
- proof/test ID for every `Native` or `Equivalent` outcome.

The conformance record is derived from compiled backend capability and tests,
not from OS/backend-name guesses. Missing entries mean `DiagnosticFailure`.
Changing an entry requires the corresponding contract and differential tests.

---

## 18. Backend Obligations

### 18.1 VM / bytecode

The conforming VM backend lowers canonical Typed IR to bytecode and executes it
through the shared semantic runtime. Bytecode is backend-private output, not a
second language contract.

The current independent AST-to-bytecode compiler is transitional. It may remain
for differential tests and editor migration, but full VM conformance requires
the production path to consume the same validated Typed IR or to prove a
deterministic one-to-one lowering contract for every operation.

### 18.2 DirectNative C++ / Windows

The backend emits C++17 only after validation and capability proof. Generated
code must use explicit checked helpers, bounded frames, quota-aware allocation,
stable registrations and structured artifact loading. C++ undefined behavior,
native exception defaults and host ABI layout are never language semantics.

### 18.3 DirectNative Web / Wasm

The Web backend consumes the same IR and semantic ABI. It must preserve
binary64, checked integer behavior, fixed memory policy, source diagnostics and
continuation semantics. Wasm traps are not acceptable substitutes for Script
diagnostics unless an explicit lowering maps them before state is exposed.

### 18.4 Future JIT or other backend

A new backend must provide:

1. an operation capability table;
2. common validator consumption;
3. explicit lowering outcomes;
4. differential corpus results;
5. quota/lifetime evidence;
6. target-platform smoke;
7. package and symbol/source boundary audits.

No new backend may define language behavior by being the first implementation.

---

## 19. Optimization Rules

An optimization is legal only after common validation and must be deterministic
for the same IR and backend contract version.

Allowed examples include unboxed checked scalar operations, constant folding,
dead pure operation elimination and local slot/register allocation.

The optimizer MUST NOT:

- fold through a possibly raising conversion or host call;
- reorder global/host writes;
- remove quota checks or owner retention;
- change float width or integer overflow;
- duplicate a call, await, yield or throw;
- reconstruct source-level delegation from bytecode shape;
- change handler coverage or diagnostic source;
- weaken a dynamic type check because one test input had a narrower value.

Optimized and unoptimized execution are differential-test peers.

---

## 20. Conformance Tests

Every backend must run the same canonical corpus generated from validated IR.

### 20.1 Required unit contracts

- every type and explicit conversion;
- stack join, underflow, maxStack and definite initialization negatives;
- branch target/successor mismatch;
- direct/indirect/host/reflection binding positives and negatives;
- aliasing, shared captures and cyclic containers;
- integer overflow, division by zero and float edge cases;
- nested/disjoint/invalid exception regions;
- yield, await, send, throw, close and delegation metadata;
- quota boundary, rollback, release and shared allocation identity;
- fingerprint mutation for every semantic field;
- module version and owner mismatch.

### 20.2 Required differential observations

For each supported function, compare:

- return value and runtime type;
- global, container, instance and host side effects;
- alias/capture identity where observable;
- diagnostic code/message/source;
- yielded values and resume sequence;
- continuation identity and no-replay counters;
- live/peak/frame memory telemetry at contract checkpoints.

### 20.3 Required backend evidence

- one fast unit lane;
- one generated package smoke;
- one platform smoke per claimed platform;
- negative source/build/symbol/package inventory;
- Release tests that do not rely on `assert` side effects;
- fresh validation build output, not stale Legacy artifacts.

Visual/RenderDoc validation is not required for ScriptVM-only semantic changes.
If an IR change affects rendered output through a consumer, the consumer's
visual contract applies in addition to this specification.

---

## 21. Current Conformance And Migration

| Component | Current status | Contract note |
| --- | --- | --- |
| Typed IR v6 schema and structural validator | Contract Complete | Current canonical in-memory schema |
| `ScriptFrontend -> ScriptLowerer` | Contract Complete | Production source-to-IR path |
| Shared semantic runtime | Backend Validated for covered operations | VM and DirectNative differential coverage exists |
| Windows DirectNative bounded lane | Platform Validated | Unsupported shapes fail closed |
| Web DirectNative bounded lane | Platform Validated | Fixed-memory/package audits required |
| VM bytecode production lowering | Backend Validated | Consumes validated v7 IR through the versioned bytecode lowerer |
| AST-to-bytecode / bytecode-to-IR adapters | Deleted | Source and production targets have no compatibility path |
| Full generator/async/complex handler graph | Implementation In Progress | Only explicitly proven shapes are Native |
| Future JIT/mobile backend | Not implemented | Must satisfy Section 18.4 |

This table prevents a contract document from being mistaken for backend proof.
`Contract Complete` describes the specified interface; `Platform Validated`
requires real target evidence. The overall ScriptVM AOT product remains
`Implementation In Progress` until the open lifecycle, performance, platform
and complete-language gates close.

---

## 22. Versioning Rules

Increment `TypedIrVersion` when any change can alter validation, execution,
identity or lowering, including:

- adding or reinterpreting an operation or operand;
- changing stack effect, type compatibility or evaluation order;
- changing CFG, handler, safepoint or delegation metadata;
- adding a semantic field to a fingerprinted structure;
- changing callable, class, quota or continuation contracts.

Increment `SemanticAbiVersion` when the shared runtime operation contract or
runtime-facing ABI changes while the IR shape remains readable.

Readers reject unknown versions. There is no best-effort compatibility mode.
A migration tool may perform an explicit version-to-version conversion only
when every changed field has a deterministic mapping and contract tests.

---

## Appendix A. Example

Source:

```python
def add_one(value: int) -> int:
    return value + 1
```

Conceptual stack IR:

```text
module arp.example v1, ir=6, semantic=1
function arp-fn:arp.example/add_one (int) -> int
  locals: [0 param value:int]
  maxStack: 2
  block b0:
    LoadLocal a=0              [] -> [int]
    LoadInt i64=1              [int] -> [int,int]
    IntAdd                     [int,int] -> [int]
    ReturnInt                  [int] -> []
```

All backends must return the same `int`, produce `IntegerOverflow` at the same
checked boundary if the equivalent operation overflows, and perform no hidden
allocation or fallback that changes quota or diagnostics.

## Appendix B. Backend Review Checklist

- [ ] Consumes a validated `TypedIrModule` and exact versions.
- [ ] Publishes an explicit outcome for every operation shape.
- [ ] Uses stable IDs and signature hashes, never persisted pointers.
- [ ] Proves stack/CFG/local/handler invariants.
- [ ] Preserves shared semantic runtime results and diagnostics.
- [ ] Preserves alias, capture, module and owner identity.
- [ ] Implements effect ordering and no replay.
- [ ] Implements safepoint/continuation/delegation metadata without guessing.
- [ ] Charges and releases the complete memory graph once.
- [ ] Rejects unsupported valid IR before observable effects.
- [ ] Passes common unit, differential, package and platform smoke gates.
- [ ] Uses the repository status vocabulary without claiming Production Ready
      from contract or single-platform evidence.
