# ScriptVM AOT Modules

The top-level directory contains the current contract-v5 AOT implementation:

- `aot_v5.*`: the DirectNative runtime, registry, and dispatcher.
- `aot_codegen_v5.*`: deterministic DirectNative C++ generation.
- `aot_package.*`: YAML package validation and build-time packaging.
- `aot_dynamic_package.*`: Windows host-tool package compilation and loading.
- `typed_ir.*`: the shared typed lowering contract.

The normative contract for every execution backend is
[`docs/scriptvm/typed_ir_backend_contract.md`](../../../docs/scriptvm/typed_ir_backend_contract.md).
Backend-private bytecode, generated C++, Wasm, or future JIT instructions may
not redefine the Typed IR semantics or introduce an implicit fallback.

`arp_script_aotc` is a v5-only compiler tool: its link closure contains the
v5 package/codegen and compiler frontend.

The engine target follows the same rule in Debug and Release. Contract-v1,
Legacy and Hybrid executors have been retired from the source and build graph;
unsupported shapes use the explicit Interpreter-only editor contract or a
deterministic v5 rejection.
Unsupported v5 source shapes continue to fail during package generation; they
must not be routed to the legacy executor by a production runtime fallback.

`src/scriptvm/editor/editor_hot_update.h` provides the explicit editor-only
Interpreter replacement boundary. It requires a complete artifact identity
plus host ABI identity, rejects stale or incomplete replacements, and applies
the existing retention and memory budgets without linking Hybrid. It is not
linked by production v5 targets.

The v5 generator contract is deliberately bounded: `yield` supports resume,
send, throw, and close. A pending `await` supports poll/resume and close; its
`send`/`throw` behavior is available only when the host supplies the explicit
`ScriptAwaitable::inject` callback. The callback reports `Rejected`, `Pending`,
`Ready`, or `Failed`, and the generated frame consumes the result at the
original await operation boundary without replaying the poll. Awaitables
without that callback still return the stable `AotSwitchUnsupported`
diagnostic. `yield from` has one additional bounded DirectNative shape: a
statically proven DirectNative child can be resumed and receive send/throw/close
at the child boundary with owner validation, RAII close, and no replay.
Same-module children use the owning execution context; a cross-module child is
admitted only when its explicit `generatorBridge` binding, fixed arity, and
module/version/artifact identity match a declared package dependency. A
statically proven collection child (typed `List`, or an `Any`
function whose every return is an immediate `MakeList`) additionally supports
nil `send()` continuation and parent-handler `throw()` injection without
replaying the preceding yield; non-nil collection sends remain a stable
non-consuming `AotSwitchUnsupported`. Dynamic and cyclic/unbounded delegation
remain fail-closed migration work. The optional `GeneratorBridgeV2` provider
permits the bounded AOT-child to Interpreter-parent direction to forward
resume, `send()`, and `close()` with no replay. `GeneratorBridgeV3` is a
separate, versioned provider contract for the reverse direction: a DirectNative
parent may forward an explicitly admitted Interpreter child only after both
providers return complete, matching module/version/artifact identity. The V3
ABI remains at version 1; pending-await injection is reported through the
independent, versioned `GeneratorBridgeV3Capability` extension. Generated
parents never infer it from a generic `Suspended` result. Missing or mismatched
identity or capability is the stable `AotSwitchUnsupported` result; V2
providers are never guessed as V3 providers. Delegated await/throw and the
complete cross-backend protocol remain bounded or Interpreter-only contracts.
Arbitrary handler CFGs and optimized-PC adoption outside the
operation-boundary contract remain explicit deterministic rejections; they are
never production fallbacks.

Persistent generated frames pass through
`DirectNativeExecutionContext::beginContinuationFrame`, which checks the
module's cumulative frame-count and byte budgets before allocation. A rejected
frame is never published; the existing continuation remains the only live
state. The count and cumulative-byte negative paths are covered by the v5 unit
and generated-package smoke tests.

Dynamic Windows packages hash stable role labels, binary tool/host/include
contents, and relative include paths with SHA-256. Absolute machine paths are
excluded from the identity so relocating an identical toolchain preserves the
cache contract. The Windows dynamic-package smoke exercises both in-process
and separate-process contention for one identity; only one completed DLL and
marker may be published, and the subsequent load must be an audited cache hit.
