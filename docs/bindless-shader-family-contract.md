# BindlessShaderFamily/1 offline publication

2026-10-13 reference-owner correction: saved-family authoring validation no longer
consults Path.cwd(). Syntax checks are pure; filesystem containment remains tied
to the declared authoring/publication owner. The new expected-red regression is
preserved and the registered suites pass2/2 (entry6/family9), with Graphics Null
smoke1/1. New Native/Emulated families are preserved under
D:/wocc/build/bindless-compiler-family-cwd-smoke1. Each mode's copied five-module
reader independently verifies its publication and target bytes from an unrelated
temporary directory in a fresh process. This closes an offline ownership bug,
not the pending runtime/device/scene/backend objectives.

This P2 tool groups an explicitly authored, finite set of existing
BindlessShaderVariant/1 packages. It introduces no resource layout, shader
operation, device capability, native execution or runtime selection algorithm.
Arp's existing generator, single-package compiler and package reader remain
authoritative; startup selection continues to use selectBindlessVariant.

Authoring is YAML: type BindlessShaderFamily, contractVersion 1, name,
maxVariants and variants (relative YAML filenames). maxVariants is a positive
explicit compilation budget; the list must be nonempty, unique and within that
budget. References must resolve inside the family's authoring directory.
Each member is a complete explicit variant, including its capacities, physical
mapping, switch budget and reserved bindings. No capacities or packing are
inferred. The generator validates every member before compilation starts.
Reference syntax validation is pure and does not resolve against the caller's
current directory. Actual loading resolves only against the declared authoring
owner; the saved reader resolves packaged files only against the family owner.

Members must describe the same logical resource and helper interface. Names,
typed table IDs, resource shapes/layouts/access, stage and operation declarations,
sampler associations and optional semantic contracts are identical. Only variant
name, capacities, physical bindings and authored compilation/snapshot budgets may
vary. Different semantics are rejected, not compiled as interchangeable choices.

The caller supplies one Slang source, explicit entries, Native or Emulated mode
and compiler tools. The family compiler invokes compile_bindless_package for
each member without legacy compatibility rewrites. It verifies every package
identity and all target/source/reflection hashes. Native keeps WGSL unavailable;
Emulated preserves the existing scalar helpers and row-major target contract.

Compilation occurs in a candidate directory. All member publications and the
family payload must validate before publication. The payload binds the authoring
bytes, source, entries, mode, tool implementation identity and member package
identities; paths are relative so the completed family is relocatable. A
content-addressed directory retains the immutable candidate. The final
bindless-family.<mode>.yaml pointer is atomically replaced last. A failed
replacement removes that named success pointer and never publishes a partial
family. Existing directories are not overwritten. This is offline tooling;
these publications do not themselves authorize loading or replacing in-flight
runtime snapshots.

The reader verifies the family hash, member identities, authoring declarations,
relative owned paths and every published artifact hash. Mutated/missing targets,
rehashed family metadata that disagrees with member packages, traversal, mixed
semantics and stale success pointers must refuse. Input mutations during a build
must refuse before publication. Device limits and capability checks remain in
the existing L2 planner and runtime creation/record gates.

| Backend | Offline result |
|---|---|
| Null | Validated metadata only; no execution proof |
| Vulkan | Native package candidates; queried runtime gates still required |
| Metal | Native package candidates; ABI/ownership/completion gates still required |
| WebGPU | Emulated package candidates; public execution remains separately gated |

WOCC's --bindless-family selection is mutually exclusive with --bindless-variant.
Both use the selected engine's authority and return before ordinary shader
inventory processing. A missing selected authority refuses without another root
or Traditional retry. No renderer/backend-name branch is introduced.

Required validation includes actual Slang multi-capacity Native and Emulated
publication, direct single-package byte equivalence, relocation, member and
family corruption, invalid schema/budget/semantics and replacement failure.
This closes an offline organization subitem only. Actual scene variants,
workload/device selection, P3 execution, P4/P5 and P6 remain required.

## Validated offline checkpoint (2026-10-13)

The WOCC registered suites pass 2/2 (six existing entry cases and eight family
cases); the registered isolated Graphics Null smoke passes 1/1. Real Slang
Native/Emulated capacity-two/four families are saved under
D:/wocc/build/bindless-compiler-family-smoke1. Emulated members match direct
single-package artifacts byte-for-byte. Relocation, altered target bytes,
rehashed member identity/capacity/entry/path substitutions, invalid budgets and
mixed interfaces refuse. Source changes and a second-member failure are tested
after real compilation in a fresh authority process; no success pointer remains.
The initial missing-CLI failure and the earlier test-process import collision
remain in their logs. The compiler now explicitly refuses foreign imports.
This is an offline contract/tooling checkpoint, not native execution validation
or completion of P2's actual scene/device selection scope.
