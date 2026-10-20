# Renderer Data Model v2

Status: Contract Complete.

`src/renderer/runtime_scene.h` defines the runtime-facing scene data model:

- mesh, texture, material, light, and instance handles
- material and light descriptors
- renderable instances
- material-keyed renderable batches

This module is intentionally independent from sample/demo content. Future work
should move import-time conversion and generated demo meshes into tools or
samples, then feed compact runtime handles into renderer draw planning.
