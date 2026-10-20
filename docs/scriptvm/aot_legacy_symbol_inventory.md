# Legacy AOT Symbol Inventory

Status: `Retired` (2026-08-26).

The former `src/scriptvm/aot/legacy/` implementation has been deleted. The
Legacy runtime, Hybrid target, v1 descriptor/codegen, generic executor and
migration-only tests are absent from the source and build graph. This file is
the final negative inventory used by source, symbol, package and import audits;
it is not a compatibility or migration ownership list.

## Retired responsibilities

| Responsibility | Replacement or decision | Current result |
| --- | --- | --- |
| v1 manifest/descriptor and code generation | v5 package/codegen contract | No v1 public entry or descriptor remains |
| Generic IR executor and dynamic call dispatch | Typed-IR DirectNative lowering; Interpreter-only for unsupported shapes | No production generic fallback |
| Scalar/native-int frame machine | DirectNative scalar lowering | Bounded v5 path only |
| Dynamic Value/container/reflection execution | Interpreter-only dynamic execution | Not a production AOT fallback |
| Yield/await/send/throw/close and delegation | Bounded DirectNative bridge plus Interpreter-only protocol | Unproved graphs return `AotSwitchUnsupported` |
| Hybrid adoption and arbitrary native-PC deoptimization | Canonical operation-boundary continuation contract | Non-canonical requests fail closed |
| Exact native shape matchers | Data-driven DirectNative shape proof | No legacy matcher implementation |
| Legacy registries and module ownership | v5 registry and deterministic RAII owner | No legacy registry target |

## Required negative assertions

- `src/scriptvm/aot/legacy/` does not exist.
- `arp_scriptvm_legacy_runtime` and `arp_scriptvm_hybrid` do not occur in
  CMake targets, test registrations or root build dependencies.
- Public Runtime/Interpreter headers do not expose v1 descriptors or generic
  executor types.
- Production source and symbols do not contain `AotModuleDescriptorV1`,
  `AotFunctionDescriptorV1`, `AotExecutionContext`, `AotGenerator`,
  `executeAotFunction`, `resumeAotGenerator`, `closeAotGenerator`, or
  `HybridExecutionDispatcher`.
- Final AOT packages contain no `.arp`, bytecode, Interpreter or generic
  executor artifact/symbol.
- Typed-IR lowering contains no fixed bytecode delegation shape scanner;
  compatibility `Program` values require explicit compiler-owned
  `delegationSites` metadata and fail `TypedIrInvalid` when it is absent or
  conflicting.

## Verification entry points

```text
python tests/tooling/scriptvm_build_graph_contract.py .
cmake -DSOURCE_ROOT=. -P cmake/VerifyScriptVmSourceBoundary.cmake
cmake -D... -P cmake/VerifyScriptAotOnlySymbols.cmake
cmake -D... -P cmake/VerifyScriptAotPackage.cmake
```

The machine-readable CTest registrations invoke the same checks for Windows
MinGW and Web Wasm Debug/Release. Historical acceptance notes live in
`aot_legacy_retirement_matrix.md`; they do not reopen retired ownership.
