# ScriptVM IR -> Bytecode -> Interpreter Migration

**Status:** Implementation In Progress  
**Target Typed IR version:** 7  
**Product status:** ScriptVM AOT remains `Implementation In Progress`

The only accepted source execution path for the Interpreter is:

```text
Source -> ScriptFrontend -> ScriptLowerer -> validated Typed IR
       -> InterpreterBytecodeLowerer -> validated Program -> Vm
```

Direct AST-to-bytecode compilation and bytecode-to-IR reconstruction are
retired by this migration. Hand-authored `Program` values remain test fixtures
for the VM backend and are not source compilation inputs.

## Progress

| Phase | Status | Evidence |
| --- | --- | --- |
| R1 Independent IR contract | Contract Complete | `arp_scriptvm_typed_ir_tests`; `arp_scriptvm_aot_v5_generated_smoke`; IR source boundary audit contains no `Opcode`, `Instruction`, or `Program` |
| R2 IR-to-bytecode lowering | Backend Validated | `arp_scriptvm_bytecode_lowering_tests`; `arp_scriptvm_interpreter_pipeline_smoke`; bytecode version/fingerprint and VM load validation |
| R3 Caller migration | Backend Validated | E2E, fast-path and editor hidden-window smoke pass; asset importer production caller uses `buildPrepared`; asset importer unit and smoke pass with one importer owner reused across a runner session |
| R4 Legacy path removal | Platform Validated (bounded Windows/Web migration lane) | `compiler.cpp/.h`, `CompileResult`, `compile(Module)` and reverse adapter deleted; five-target build graph configures; fresh Windows MinGW Release migration gate is 36/36; Web Debug/Release lanes are 15/15; clean package/source/symbol/import closure passes |

## Current validation evidence

- `arp_asset_importer_script_unit_tests`: passed in 734.68 seconds.
- `arp_asset_importer_script_smoke`: passed in 428.42 seconds.
- Windows Release quick IR/bytecode/pipeline and source-boundary gates: 6/6
  passed in 2.90 seconds.
- Windows Release Plugin unit/smoke: passed in 61.34 and 42.03 seconds.
- Windows Release AOT v5 unit, AOT-only link, generated package smoke, and
  asset AOT smoke: passed; asset AOT smoke completed in 88.75 seconds.
- Windows Release migration validation subset: 36/36 passed, including stress
  release performance corpus, C++ Plugin unit/smoke, and Plugin codegen
  contract (total 554.33 seconds in the final rerun).
- Final Windows Release closure audits passed from
  `build/aot-validation-final-win-release-20260828`: all six generated AOT
  packages, the Unity source inventory, the ScriptVM source/build boundary,
  and the strict symbol/import audit for the seven production AOT-only
  artifacts. The differential smoke and `arp_script_aotc` are audited under
  their explicit Frontend/Interpreter test contract rather than the strict
  AOT-only rule.
- Web Debug build and smoke evidence: build graph, source boundary, Typed IR,
  and generated AOT smoke passed 4/4 in 0.70 seconds; generated package audit
  passed with fixed 134217728-byte memory.
- Web Release configured successfully; its CTest list includes Typed IR,
  generated/cross-backend AOT, fixed-memory, release-audit, and browser gates,
  and its build graph, source boundary, Typed IR, generated AOT smoke, and
  `arp_scriptvm_aot_v5_release_audit` passed 5/5 (0.51s plus 0.12s audit).
- `ScriptToolRunner` owns one Interpreter-mode `CppPluginImporter` per runner
  session; each invocation still owns an independent `ModuleRegistry` and
  source resolver.
- Asset importer targets receive the selected CMake C++ compiler as an explicit
  tool identity, rather than relying on `PATH` or an existing cache hit.

These asset targets exercise real FBX/GLB, texture, particle, character
package, plugin, and script paths. They are slow integration/system evidence,
not the quick unit gate. The quick gate remains the focused IR, bytecode
lowering, pipeline, and build-graph tests. Windows Release AOT and Plugin
The final clean validation tree now provides the complete migration
source/build/symbol/import/package closure without relying on stale build
artifacts. R4 is therefore `Platform Validated` only for the bounded
Windows/Web migration lane represented by these tests. This does not close
uncovered language features, unvalidated platforms, or full product lifecycle
and performance obligations.

Each phase is marked complete only after its quick unit and smoke path pass.
Passing this migration does not imply `Production Ready`.
