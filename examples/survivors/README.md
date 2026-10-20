# Copperlight Survivors

Standalone ARP 2D acceptance game for Windows x64/Vulkan. No Editor or WOCC
runtime dependency. Assets, original synthesized audio and the bitmap HUD use
one pixel-art style. Kenney Tiny Dungeon artwork is CC0; provenance and exact
source PNG digest are in `assets/PROVENANCE.yaml`.

Enter starts a run; WASD/arrows move; Escape pauses; 1/2/3 choose an upgrade;
R starts a fresh run; minus/plus adjusts master volume. Three weapons attack
automatically. Survive 300 seconds and defeat the Boss. Records, earned points
and volume persist in `user-data/record.yaml` beside the executable.

## Weapon upgrades

Choose 1/2/3 to improve Bolt / Orbit / Pulse respectively. All three retain
damage and attack-rate growth, and now change the attack shape as well:

| Weapon | Additional effect per upgrade | Ceiling |
|---|---|---|
| Bolt | +1 simultaneous shot; +1 piercing hit every 2 upgrades; +1 ricochet every 3 upgrades | 6 shots / 3 extra piercing hits / 2 ricochets |
| Orbit | +1 evenly spaced blade | 8 blades (starts with 3) |
| Pulse | +15% of initial radius | 220% initial radius |

The menu shows **current / next** values; the HUD displays actual shot/blade
counts and pulse radius percentage. Bolt volleys select distinct nearby living
targets when possible, then reuse the nearest target. The initial fan has a
short authored homing delay. Piercing precedes enemy-to-enemy ricochet, and a
projectile cannot damage the same enemy generation twice. If ricochet has no
eligible next enemy, the projectile ends. World pause freezes all these effects.
Game config v3 explicitly specifies caps, level divisors, fan angle, homing
delay and Scene/component budgets. Each actor is a real GameObject with its
own attached ScriptComponent; projectile history uses stable object IDs.

## Build and run

```powershell
$env:CMAKE_BUILD_PARALLEL_LEVEL='2'
python scripts/build.py --preset win-vulkan-release game survivors
build/win-vulkan-release/arp_survivors.exe
```

Isolated build used during parallel workspace work:

```powershell
cmake --preset win-vulkan-release -B build/survivors-release
cmake --build build/survivors-release --target arp_survivors --parallel 2
build/survivors-release/arp_survivors.exe --width 1920 --height 1080
```

The default is Interpreter mode, executing the current gameplay source:

```powershell
build/survivors-release/arp_survivors.exe --script-interpreter --assets ./examples/survivors/assets
```

This explicitly loads `.arp` and YAML at startup; restart after editing them.
Use `--script-aot` to select the generated DirectNative v5 package. That mode
rejects a source-hash mismatch. There is no automatic fallback between modes.

## Script components

Custom classes inherit `ScriptComponent` and attach with
`object.add_component("PlayerController")`. `get_component` returns the same
script instance. Instance fields store gameplay state; `game_object` and
`component` refer to the actual owner and native lifecycle carrier.

Hooks are `on_enable`, `on_start`, `on_update(dt)`, `on_fixed_update(dt)`,
`on_late_update(dt)`, `on_disable`, `on_destroy`, and `on_event(name, payload)`.
Optional physics hooks are `on_collision_enter(other: GameObject)`,
`on_collision_exit(other: GameObject)`, `on_trigger_enter(other: GameObject)`,
and `on_trigger_exit(other: GameObject)`. The host dispatches them after each
2D physics step; Scene defers destruction until the event batch finishes. The
shared Scene scheduler handles activation and deferred destruction.
`arp_scene` and `arp_scene2d` expose canonical native components and systems;
the old numeric-table `arp_runtime2d` service has been removed.

Gameplay responsibilities include run flow, player/enemy behavior, projectiles,
weapons, waves, HUD, and moving platforms. Native host code schedules Scene and
physics and supplies platform input/configuration; gameplay rules remain in
scripts. See [Script Components](../../docs/script-components-plan.md) for the
component and serialization contracts.

`--integer-scale` enables integer scaling within the 1280x720 logical canvas;
windows smaller than that canvas are explicitly rejected in integer mode.
`--mute` explicitly opts out of the audio device. Missing or invalid resources
fail with diagnostics; there is no placeholder texture or silent audio fallback.

## Automation

Build registered test executables before running the focused CTest selection:

```powershell
cmake --build build/survivors-release --target arp_physics2d_tests arp_scene_2d_resource_tests arp_scene_2d_services_tests arp_scene_2d_codec_tests arp_scene_2d_world_tests arp_scene_2d_runtime_contract_tests arp_survivors_game_tests arp_scene_2d_vulkan_hidden_smoke arp_platform_window_tests --parallel 2
ctest --test-dir build/survivors-release -R '^(arp_scene_2d_.*|arp_physics2d_tests|arp_survivors_.*|arp_platform_window_tests)$' --output-on-failure
```

- `--null-smoke`: contract validator frames only; no playable output claim.
- `--hidden --autoplay --fixed-step --frames 120 --mute`: fast native smoke.
- `--autoplay --mute`: a real wall-clock 300-second automated game.
- `--simulate --autoplay --mute`: accelerated simulation through victory/death;
  add `--script-interpreter` to use source execution. Simulation-only evidence
  does not prove real-time frame rate or visual output.
- `--scenario death|pause|upgrades`: targeted gameplay states.
- `--scenario evolution --fixed-step --frames 1200 --autoplay --mute`:
  exercise 18 scripted selections, then show another upgrade; capture states
  `evolved` / `evolved-upgrade` validate the changed attacks and menu.
- `--restart-check 20 --hidden --mute`: render pressure populations then reset;
  validate physics/pool baseline and report stable memory deltas.
- `--normal-restart-check 20 --hidden --mute`: three seconds of ordinary
  Interpreter combat per world, with no pressure population. After two warmup
  restarts, require identical scene/physics counts and core/Box2D/GPU live bytes.
  GPU submissions drain only at these cold memory-test checkpoints.
- `arp_survivors_game_tests --memory-check`: two warmups plus 20 ordinary
  combat/upgrade/pause/death cycles; compare bytes and outstanding allocations
  after complete world destruction. Registered as `arp_survivors_memory_tests`.
- `arp_survivors_game_tests --memory-full-check`: two complete fixed-seed
  300-second victory/Boss games; the first warms caches, the second must
  return to the same disposed-world memory baseline. Registered in CTest as
  `arp_survivors_full_memory_tests`; this does not measure performance.
- `--resize-check --frames 90 --autoplay --fixed-step --hidden --mute`:
  native window/swapchain resize sequence.
- `--stress --hidden --mute --width 1920 --height 1080`: 30-second warmup,
  180-second real-time sampling, VSync/limiter off and strict percentile gates.
- `--seed N`, `--assets PATH`, `--save-dir PATH`: explicit inputs/lifetimes.
- Under RenderDoc injection: `--capture-frame N --capture-path PREFIX`, or
  `--capture-state combat|upgrade|pause|death|physics|victory`.
- `--capture-series PREFIX --capture-every-ms 1000`: optional developer GPU
  recording; never combine with performance acceptance.

`GameConfig` reads versioned YAML capacities. `Content` admits typed stable-ID
resources. `Rules` composes the explicitly ordered, versioned
`assets/script-package.yaml` source list for the canonical Interpreter or
DirectNative session. Gameplay state, AI, weapons, damage, upgrades, HUD,
scene construction and flow are split by responsibility under
`assets/scripts/`. `GameRun` schedules
the script and exposes generic engine services and read-only snapshots; it
contains no native gameplay rules. Box2D, TileMap, timers/Tween, particles,
audio, resource parsing and rendering remain engine services. Frame slots defer instead of
blocking/retrying inside render recording. Fixed-step debt remains observable.

The component interface and its serialization and event boundaries are defined
in [Script Components](../../docs/script-components-plan.md). Platform support
and validation status are tracked by the relevant backend and platform
contracts.
