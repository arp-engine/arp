# ARP Domain Plugin Boundary

This is the prerequisite boundary for moving WOCC and other low-frequency
business rules into `.arp` scripts. The order is intentional: an engine
capability is not considered script-ready because a C++ phase can be called;
it must have a stable value/reference contract, a service adapter, generated
reflection, and dynamic/static parity coverage first.

## Rules

- Module names are identifiers without dots: `arp_core`, `arp_scene`,
  `arp_scene3d`, `arp_resources`, and `arp_network`.
- `HostModule`, `HostModuleBuilder`, and direct module registration are not
  part of the boundary.
- Static types, methods, enums, and fields come only from manifest
  `reflectionHeaders` and `arp_reflect_codegen` output.
- Runtime services are the only hand-written bridge. They accept stable
  descriptors or generation-checked references and never expose native
  pointers, renderer handles with hidden ownership, or private WOCC objects.
- Dynamic loading and static catalog registration invoke the same entry point
  and are required to produce the same export table and diagnostics.

## Completion gate

A domain is complete only when all rows below are true:

| Domain | Script-facing surface | Native implementation retained | Gate |
| --- | --- | --- | --- |
| `arp_core` | math, `ScriptResult`, basic value types | allocation, SIMD, numeric hot paths | codegen + reflection smoke |
| `arp_scene` | object identity, hierarchy, active state, transforms, component references, behavior binding/attach/detach | storage, traversal, generation/lifetime checks, Hybrid dispatcher ownership | stale-reference + lifecycle + binding tests |
| `arp_scene3d` | mesh/light/camera/collider descriptors, add/update/remove/query whitelist | extraction, culling, physics/render backends, GPU upload | whitelist + lifecycle + dynamic/static parity |
| `arp_resources` | load/release/state/alias and async result values | IO, decode, dependency graph, upload | async failure/ownership tests |
| `arp_network` | TCP/UDP/HTTP/WebSocket async wrappers and result values | sockets, protocol parsing, TLS, event loop | await/error/TLS tests |

Only after a domain passes this gate may WOCC scripts take ownership of
orchestration or low-frequency rules in that domain. PCG, physics stepping,
animation skinning, batch AI, rendering, resource IO, and wire protocol hot
paths remain native.

## Current state

Plugin ABI v2 and codegen enforcement are complete. `arp_scene` and
`arp_scene3d` now expose generation-checked object/component references,
script-safe resource-id descriptors, add/update/remove/query surfaces and
dynamic/static parity tests. The behavior binding/attach/detach contract is
injected through the per-world Hybrid dispatcher. WOCC `wocc_scene.arp` now
owns the scene object tree, component creation and role registration; native
code retains resource import, PCG, physics, animation, rendering and GPU
preparation hot paths.
