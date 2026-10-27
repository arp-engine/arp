# Physics package organization

## Plan

- [x] Phase 1: inventory implementation ownership, consumers and validation paths.
- [x] Phase 2: consolidate physics source into `physics/3d` and `physics/2d`.
- [x] Phase 3: format, build and run physics contracts and smoke validation.
- [x] Phase 4: audit upward dependencies and identify scene/application adapters.
- [x] Phase 5: move adapters to their owning domains and remove upward physics dependencies.
- [x] Phase 6: enforce dependency boundaries and rerun focused contracts/smoke.

## Package boundaries

```text
src/physics/
  3d/
    physics_module.h/.cpp
    physics_types.h/.cpp
    physics_world.h
    physics_event_listener.h
    backends/jolt/
  2d/
    physics_world_2d.h/.cpp
src/2d/
  physics_components.h
  physics_script_domain.h
  scene_component_persistence.h/.cpp
src/terrain/
  terrain_surface_physics_adapter.h
```

Headers and implementations move together. Consumers, reflection/plugin
manifests, editor integration and build source selection use the new paths.
Existing `arp::physics` / `arp::physics2d` namespaces, serialized identifiers,
diagnostics and `arp_physics` / `arp_physics2d` CMake targets retain their
behavior. The generator's 3D source scan must stay inside `physics/3d` so that
it cannot accidentally compile Box2D sources into the Jolt target.

Scene components, object synchronization, scene serialization and script-domain
adaptation belong to `src/2d`, which consumes the physics world. The terrain
adapter belongs to `src/terrain`. Physics headers and source may only include
first-party `core`, `reflection` and `physics` code. Both physics targets depend
on the core/reflection foundation and their native backends, with no scene,
resource, renderer, editor, game or application dependency.

The application-facing classes retain their existing namespaces, script type
names, serialized identifiers and service contracts. No compatibility forwarding
headers remain under `src/physics`. `physics_world_2d.h` includes reflection
directly instead of an unused scene transform header. The 2D script-memory
adapter explicitly includes the scene transform it describes.

## Validation

Phase 1 baseline in `build/win-vulkan-debug`: `arp_physics_l2_contract_tests`,
`arp_physics_jolt_backend_tests`, `arp_physics2d_tests` and
`arp_survivors_null_smoke` pass (4/4).

Phase 2: all 19 physics source/header files moved; source consumers, the editor
reflection input, both plugin manifests and build references updated.

Phase 3 validation uses MinGW GCC 16.1.0 in `build/platform-package-debug`:

- clang-format 21.1.8 formatting and `--dry-run --Werror` pass for all 42
  affected C++ files. Token/include audit passes after declared path changes
  and the two test repairs described below. Active source/build/plugin
  references contain no old physics include paths.
- Physics, collider, scene codec/world, script component domains (interpreter
  and AOT), both static scene plugins and four editor test targets build.
- Focused CTest coverage: 14/14 pass, including Jolt/L2, Box2D, scene codec/world,
  script domains, editor scene/viewport/server/resources, Vulkan physics smoke
  and Vulkan tile smoke. Log: `build/physics-package-organization/ctest-final.log`.
- Web build-selection tooling: 26/26 pass. Build generator audit confirms the
  Jolt target contains exactly the five `physics/3d` source files and excludes
  `physics/2d`; both scene plugin manifests resolve their reflection headers.
- RenderDoc capture and replay: 1 draw, 7 instances, 1 atlas, 96-byte stride,
  61,312 red pixels, 61,312 green pixels and 0 validation errors. AI inspected
  the exported image: all seven red/green sprites are visible, including the
  floor and simulated bodies. Evidence:
  `build/physics-package-organization/physics_capture.rdc`, matching `.yaml`
  report and `.bmp` image.

Verification exposed existing test wiring/fixture mismatches, repaired without
changing production physics behavior: the TileMap runtime accessor needs a
pointer guard/member access; the physics showcase needs unit-scale rigid-body
roots with explicitly scaled visual children; codec/world tests need the
resource/ScriptVM registration libraries in a rescan link group.

Additional Survivors integration build remains blocked by missing synchronous
reflection call contracts for `Joint2DComponent.initialize_joint` and
`TweenComponent.value`. Those existing declarations have no `AFUNC` annotations;
this migration does not change their exports. The AOT generator rejects them
instead of silently lowering indirect calls. Details are retained in
`build/physics-package-organization/build-final.log`. The pre-migration Survivors
Null smoke result above is baseline evidence only, not post-migration validation.

Status: package path migration is Contract Complete. The focused Windows/Vulkan
smoke is Backend Validated and Platform Validated for that fixture only. This
structural migration does not promote existing physics or renderer status to
Production Ready or validate additional platforms.

## Dependency boundary verification

Phases 4–6 remove the upward scene/terrain dependencies discovered after the
initial package move. `arp_physics` links `arp_core`, `arp_reflection` and
`arp_jolt_headers`; `arp_physics2d` links `arp_core`, `arp_reflection` and Box2D.
The generated build definition uses the same 3D dependency list.

`tests/tooling/physics_dependency_contract_tests.py` recursively checks the
first-party include closure (46 source/header files), both CMake dependency
lists and the build generator. It rejects dependencies outside core, reflection
and physics. This guards the architecture rather than just the new filenames.

Final MinGW build passes. Focused CTest results are 15/15, including the new
dependency contract, the prior physics/scene/script/editor tests and both Vulkan
smokes. All 10 plugin manifest contracts pass. All 17 affected C++ files pass
clang-format 21.1.8; the migration audit preserves tokens/include sets except
the declared dependency changes. Logs and audit reports are under
`build/physics-layer-boundary/`.

AI replayed and inspected `build/physics-layer-boundary/physics_capture.rdc`
and its exported `.bmp`: 7 instances, 1 draw, 1 atlas and 0 validation errors.
The bitmap SHA-256 equals the Phase 3 baseline:
`80DA5FBF8228C0C888F3B103B691EA0035CA0BF6AA93375696EF3150B114F268`.
The dependency boundary is Contract Complete; the Windows/Vulkan fixture has
Backend Validated / Platform Validated evidence. This does not change the
Survivors AOT limitation recorded above or establish Production Ready status.
