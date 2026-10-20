# ArpScript C++ Plugin Modules

Status: `Prototype Complete`.

The 2026-08-26 contract hardening makes reflection publication transactional.
Plugin entry code runs inside `reflection::RegistrationTransaction`; generated
classes remain isolated until entry success, explicit export validation,
canonical reflection receipt/hash verification, service validation, and DLL
owner preparation all succeed. Failure or exception discards staging and
unloads the module, so the global registry never observes a partial class or a
function pointer without its DLL owner. Auto-added `abi_version` and `domain`
metadata do not count as explicit exports.

Dynamic compilation now consumes one `ImmutableBuildInputSnapshot` containing
the manifest, source, reflection headers, include contract, generator/compiler
binaries and host ABI artifacts. The identity lock is bound to the actual
lock path, tools read only the snapshot, and generated reflection source plus
metadata plus DLL publish as one hash-verified transaction whose YAML marker
is written last. A missing companion, partial DLL, damaged marker, or input/tool
drift is a cache miss, never a compatibility fallback.

This feature lets a script import a module whose implementation is declared by
a YAML `.arpplugin` manifest. Contract v2 uses one controlled registrar ABI for
both dynamic development plugins and release/static catalogs. Plugins receive
only the registrar and declared service registry; they never receive a module
registry or a host-module builder.

## Manifest

With module-directory layout enabled, the registry looks for
`<sourceRoot>/<module>/<module>_module.arpplugin` (and accepts the legacy
`<module>.arpplugin` name as a fallback):

```yaml
type: arp.script.cpp-plugin
contractVersion: 2
module: cpp_math
source: cpp_math.cpp
entry: arp_register_cpp_math
reflectionModule: cpp_math
reflectionHeaders:
  - cpp_math.h
```

`source` is required to be a relative path below the manifest directory.
`module` must match the script import name. Optional `includeDirs` entries are
also relative to the manifest directory and are checked against `..` escape.
`reflectionHeaders` uses the existing `arp_reflect_codegen` tool; generated
registration code is compiled into the plugin and is associated with the
script module through `ModuleRegistration`.
It is mandatory and must be non-empty: every script-visible static type,
enum, property, method, and ordinary function comes from this generated code.
This is a hard boundary: a type that has a useful reflection header, such as
`SceneDiagnostic`, must never be recreated with a hand-written plugin binding.

## Plugin source

The source includes `scriptvm/runtime/cpp_plugin_api.h` and invokes the generated
registration for the headers named by `reflectionHeaders`:

```cpp
#include "scriptvm/runtime/cpp_plugin_api.h"

namespace arp::reflection::generated {
bool register_cpp_math_reflection(arp::scriptvm::ModuleRegistration*);
}

// cpp_math.h uses ASTRUCT/APROP/AFUNC and is listed in reflectionHeaders.
// The importer generates and compiles its reflection registration source.

extern "C" ARP_SCRIPT_PLUGIN_EXPORT int arp_register_cpp_math(
    ArpScriptPluginHostV2* host)
{
    if (host == nullptr || host->abiVersion != ARP_SCRIPT_PLUGIN_ABI_VERSION_V2
        || host->registrar == nullptr) return 0;
    return arp::reflection::generated::register_cpp_math_reflection(
        host->registrar->reflectionModule()) ? 1 : 0;
}
```

The entry symbol has C linkage for lookup, but the registration context is a
C++ ABI contract. The plugin must use the same C++17 toolchain and link against
the host engine library. Reflection types, constructors, methods, properties,
and ordinary module exports are therefore registered through generated code.
The registrar adds `abi_version` and `domain` metadata automatically. A plugin
entry may only hand-author the adapter implementation for a host-service
operation through `exportServiceFunction` or `exportServiceCallable`; the
operation names must also be listed in the manifest's `serviceExports` list.
These adapters do not define static script API: they only connect a declared
runtime service to the generated export contract. Any new type, enum, property,
method, or ordinary function added alongside an adapter must be marked in a
`reflectionHeaders` file and regenerated.
`exportValue`, `exportFunction`, `exportCallable`, `bindType`, and `bindClass`
are rejected in plugin entry sources by both the CMake and dynamic-importer
contract checks. The repository-level
`arp_script_plugin_codegen_contract` test applies the same rule to every
`.arpplugin` source tree, including helper implementations, and checks the
service list in both directions. Consequently a type such as `SceneDiagnostic`
is declared once with reflection macros in its header and is never re-created
by a hand-written plugin binding.

## Host setup and script usage

```cpp
arp::scriptvm::CppPluginImporterDesc desc;
desc.sourceRoot = projectRoot / "scripts" / "modules";
desc.cacheRoot = projectRoot / ".cache" / "arp-script-plugins";
desc.includeRoot = projectRoot / "src";
desc.hostLibrary = projectRoot / "build" / "win-auto-debug" / "arp_engine.dll";
arp::scriptvm::CppPluginImporter importer(std::move(desc));

arp::scriptvm::ModuleRegistry modules;
modules.setCppPluginImporter(&importer);
```

The script can then use the normal module syntax:

```text
import cpp_math
cpp_math.Point().doubleValue()
```

On Windows the default compiler command is `g++`; macOS uses `clang++` and
other native platforms use `c++`. Set `ARP_SCRIPT_CPP_COMPILER` or assign
`compilerCommand` when the toolchain is not on `PATH`. Windows builds use the
repository MinGW workflow.

Compilation output is cached by a SHA-256 content contract over the manifest,
source, compiler identity/content, host library content, reflection generator
content, and declared include trees. Absolute machine paths are excluded from
the artifact identity; stable relative labels preserve input separation.
Failures are deterministic and retain the compiler log next to the cached
dynamic library; VM load errors include the importer diagnostic.

Host-tool execution uses `core::ProcessRunner`: Windows uses `CreateProcess`
with independently quoted arguments, and native POSIX hosts use fork/exec
with direct argv passing. Emscripten rejects host-tool execution explicitly;
it does not silently fall back to a shell or an in-process compiler.

Generated source scans, cache stems, include flags and library-link arguments
are kept in `core::pmr::String`; ordinary `std::string` is limited to
unavoidable `std::filesystem` boundary conversions. The ProcessRunner contract
also covers arguments containing spaces and shell metacharacters, proving that
these values remain data rather than shell syntax.

After registration, the host and plugin libraries remain loaded for the
process lifetime. The reflection database currently has no type-unregister
contract, so unloading a registered plugin would leave its reflected method
wrappers and vtables dangling.

This status does not claim `Backend Validated`, `Platform Validated`, or
`Production Ready`: reflection-type bindings, sandboxing, hot reload, and
platform-specific packaging remain outside this prototype contract.
