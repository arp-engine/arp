# Import Script Libraries

Status: Prototype Complete.

The import path is intentionally split into four small C++ plugin modules:

- `io`: deterministic text/binary file access, directory enumeration, and file hashes.
- `yaml`: YAML documents and node primitives used by import scripts.
- `asset`: native GLB/FBX import and image decoding, returned as reflected C++ objects.
- `arp_import_host`: explicit import-root path resolution with traversal rejection
  and deterministic path-extension queries.

Unity-specific policy and conversion remain in `.arp` scripts. The C++ plugins
do not own Unity asset semantics, metadata policy, or output package policy.

`arp_import_host` is loaded from
`Libs/arp_import_host/arp_import_host_module.arpplugin`.
The other modules use the same module-directory layout:

```text
Libs/<module>/<module>_module.arpplugin
```

`network` uses `Libs/network/network_module.arpplugin` for its ScriptVM v2
plugin binding. Its owning `io_context` is injected through
`arp.network.io-context/v1`; no host-module registration path is used.

Each v2 plugin manifest declares a positive `moduleVersion`, the real source
headers in `reflectionHeaders`, and an `exportContractHash`.
Those headers, and their `ASTRUCT`/`ACLASS`/`AENUM`/`APROP`/`AFUNC` markers, are
the only source of script-visible reflection. `arp_reflect_codegen` generates
the type, method, enum, and module-function registration source for both
dynamic and static plugin targets. The entry for module `M` must call exactly
`register_M_reflection()` from that generated source; a compatibility import
alias may set `reflectionModule` explicitly. Plugin entry points only invoke
that generated registration and bind runtime service callables through
`ScriptPluginRegistrar`; hand-written reflection or ordinary export calls are
prohibited.

The Unity script entry point is `main(input, projectRoot) -> int`. Passing
`--script-output <file>` invokes `export(input, projectRoot, output) -> int`;
the script writes a YAML `UnityImportReport` with `contractVersion: 1`.
Non-zero script status codes are failures and are propagated by the CLI.

This is not Backend Validated or Production Ready. The native plugin ABI,
allocator boundary, and platform-specific deployment still require broader
validation before that status can be claimed.

The standalone importer and plugin smoke executables link against the same
`arp_engine.dll` instance that is used as the plugin host. This keeps
reflection registrations, function wrappers, and mimalloc ownership inside one
engine ABI instance. The remaining status is still Prototype Complete because
cross-platform deployment and broader ABI compatibility validation are not yet
complete.
