# ScriptVM packages

ScriptVM source is grouped by responsibility:

- `runtime`: values, semantic operations, module/host/plugin contracts, and
  shared diagnostics.
- `compile`: syntax, semantic analysis, and frontend lowering to canonical IR.
- `ir`: backend-neutral Typed IR and its validator/fingerprint contract.
- `interpreter`: IR-to-bytecode lowering, bytecode validation, frames,
  suspension, and the interpreter VM.
- `editor`: explicit Interpreter-only hot-update replacement boundary; this
  package is never linked by production AOT targets.
- `aot`: generated native control flow, package loading, and the
  DirectNative v5 dispatcher.

There are no root forwarding headers. Production consumers must use
package-qualified headers such as
`scriptvm/runtime/value.h`, `scriptvm/interpreter/vm.h`, or
`scriptvm/aot/aot_v5.h`, and link the package targets they consume explicitly:

- `arp_scriptvm_runtime`
- `arp_scriptvm_ir`
- `arp_scriptvm_frontend`
- `arp_scriptvm_interpreter`
- `arp_scriptvm_editor_runtime` (editor-only)
- `arp_scriptvm_aot_tooling`

Only ABI-v2 DirectNative objects are built and exported by production targets.
The former Legacy runtime, Hybrid target, v1 descriptor and generic executor
have been retired. Unsupported shapes use the explicit Interpreter-only editor
contract or deterministic v5 rejection; no compatibility target is linked by
engine, editor, or AOT-only closures.

Some implementation files currently have intentional cross-package type
dependencies. The package targets are object libraries so a consumer can link
the required set without introducing an aggregate facade. Strict release
targets use the smaller AOT-only runtime libraries and never link the
interpreter package.
