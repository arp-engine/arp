# Script Components

Script components let ArpScript classes attach to canonical `GameObject`
instances while using the Scene scheduler and native engine services.

## Component contract

- A script component has its own reflected instance and fields. `game_object`,
  native component queries, and script component queries refer to canonical
  Scene objects; stale references fail deterministically.
- One world/module execution environment owns the component instances. A
  component's declared hooks are validated and cached when its module is
  loaded. Missing optional hooks mean no subscription; invalid types or
  signatures are errors.
- Lifecycle order is enable, start once, update/fixed update/late update, then
  disable and destroy. Inactive or disabled objects do not update. Construction
  does not run lifecycle hooks.
- Changes that destroy objects during update or event dispatch are deferred to
  a safe Scene boundary. Newly attached components become eligible at the next
  scheduler pass; iteration does not expand in place.
- Interpreter and DirectNative use the same receiver, fields, hook arguments,
  and event ordering. Execution mode is selected explicitly; a failed DirectNative
  identity check does not fall back to the Interpreter.

## Serialization

Scene and prefab serialization uses the shared Scene component codec and
versioned YAML. Script component records identify the script resource, type,
schema version, and typed fields. Object and component references use local IDs.
Loading creates all objects and components before resolving references; unknown
types, unsupported fields, invalid versions, and unresolved or wrong-type
references fail with diagnostics. A failed load does not publish partial state.

Arbitrary nested script value objects and dictionaries are not part of the
serialized field contract unless a type-specific codec explicitly supports
them.

## Physics events

The optional hooks are `on_collision_enter(other: GameObject)`,
`on_collision_exit(other: GameObject)`, `on_trigger_enter(other: GameObject)`,
and `on_trigger_exit(other: GameObject)`. Signatures are checked when the
module loads. Events are dispatched after the physics step, once for each
participant, and sensor events use the trigger hooks. Destruction requested by
a callback is deferred until the complete event batch has been processed.

Contact points, normals, and persistent/stay callbacks are not exposed by this
interface.

## Responsibilities

Scripts own gameplay rules such as input response, AI, damage, weapons,
upgrades, waves, HUD, and game flow. Native engine systems own rendering,
resources, physics, audio, clocks, and platform services. This interface does
not add GPU or RHI semantics; Null validation does not establish backend or
platform execution readiness.
