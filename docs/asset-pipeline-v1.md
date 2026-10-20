# Asset Pipeline v1

Status: Contract Complete.

Runtime ownership: `src/assets` is a bottom-layer support module. Renderer, GUI, platform, and samples may consume asset-domain data, but renderer code must not own importer dependencies or native file format parsing.

The offline asset pipeline now has a runtime package planning contract in
`tools/asset_importer/asset_importer.h`. It describes the runtime-ready package
shape expected by mobile and desktop samples.

Runtime packages include:

- meshes with optimization, bounds, and optional meshlet preparation flags
- materials
- textures with mip generation and mobile compression choice
- Slang shader references plus SPIR-V, MSL, and reflection outputs
- UI layouts
- scene manifests

Offline importer dependency plan:

- Assimp: required offline importer dependency for broad mesh/scene format coverage.
- meshoptimizer: required offline mesh optimization dependency.
- KTX-Software: required offline texture container/compression dependency.
- nlohmann/json: required manifest and metadata dependency.
- DirectXTex, stb_image, and cgltf: optional compatibility helpers.

Assimp and other importer libraries belong in assets/tools/offline paths only. They must not become renderer runtime dependencies.
When `3rd/assimp-*` is present, CMake builds it as a vendored static dependency with FBX, GLTF/GLB, OBJ, and Collada importers enabled and tools/tests/samples/exporters disabled.

FBX and Unity character import status:

- Contract Complete: FBX character import is represented as an asset-layer contract that produces `AnimatedMeshAsset` and Unity animator state metadata.
- Prototype Complete: `src/assets/fbx_character_importer.*` imports FBX through Assimp when configured, and `src/assets/unity_character_importer.*` parses Unity `.controller`/`.meta` folders such as `D:\UnityProj\Assets\ArtWorks\Characters\Player\Human\male`.
- Backend Validated: Not applicable yet. Imported assets still reach rendering through existing mesh upload/draw plan contracts.
- Platform Validated: Windows MinGW parser path validated. Android/iOS lifecycle, surface, swapchain, pause/resume, and memory-budget impact are unchanged because source asset import remains offline.
- Production Ready: No. Production readiness requires an Assimp-enabled build artifact, larger FBX corpus coverage, texture transcoding/package output, and hidden-window validation with imported character rendering.

The package plan produces a deterministic cache key from the declared inputs and
rejects packages that omit required runtime outputs such as shader reflection or
mobile texture compression.

Future implementation work can replace the contract-only package planner with a
binary writer without changing the runtime-facing manifest shape.

## UI Document Contract

Status:

- Contract Complete: `src/ui/ui_document.*` defines the `.arpui` document
  contract consumed by the long-term `src/ui` runtime. It records explicit
  canvas metadata, node ids, parent ids, retained control type, layout rect,
  style/text/state ids, resource bindings, PSD layer binding metadata, stable
  document hash, and deterministic validation diagnostics.
- Contract Complete: `tools/asset_importer` accepts deterministic authoring
  layer-tree fixtures plus explicit UI manifests and writes `.arpui` documents.
  This is compatibility evidence for explicit metadata validation, not a
  production PSD/AI parser. Production `.arpui` is expected to be emitted by an
  external AI/tool pipeline before entering the engine.
- Contract Complete: `.arpui` texture entries carry explicit source layer id,
  source rect, pivot, nine-slice border, alpha mode, sRGB flag, mip intent,
  compression intent, sampler intent, atlas id, and runtime resource id.
  Renderer/RHI upload, descriptor allocation, and backend texture policy remain
  backend-owned and are not exposed through the UI document.
- Contract Complete: `UiArpuiRuntimeLoader` loads `.arpui` files into
  `UiDocument` plus `UiControlRuntime`, validates explicit metadata before
  building the control tree, and keeps render extraction free of native backend
  handles.
- Contract Complete: `UiFrameworkContract` records explicit framework
  completeness capabilities for layout, style, interaction, text, image,
  resource binding, render extraction, diagnostics, and hot-path constraints.
  It declares status labels separately from backend/platform readiness and
  rejects runtime design-source parsing, native backend handles, per-frame
  source import, per-frame shader compilation, or unbounded runtime allocation
  expectations.
- Contract Complete: `UiLayoutStyleThemeDocument`,
  `UiLayoutStyleThemeResolver`, and related theme/layout/style records define
  explicit anchors, spacing, size constraints, stack/grid layout intent,
  z-order, clipping, style states, and theme token lookup for `.arpui`-derived
  UI. Invalid anchors, impossible constraints, missing theme tokens, and
  invalid style states produce deterministic diagnostics.
- Contract Complete: `UiControlInteraction` and `UiControlRuntime::interact`
  define explicit retained-control interaction semantics for button click,
  checkbox toggle, slider drag, text edit, numeric stepping, scroll deltas,
  dropdown/tab/tree selection, color pick, image hit testing, keyboard focus
  navigation, and pointer capture release. Disabled controls, missing option
  metadata, text overflow, invalid selection index, invalid value kind, and
  unsupported interactions fail deterministically.
- Contract Complete: `UiArpuiResourcePackageBuilder` and
  `UiArpuiPackageRuntimeLoader` make `.arpui` a first-class
  `ArpUiDocumentResource` package entry. The envelope records UUID resource id,
  alias, status label, content hash, texture dependencies, and explicit
  `documentJson` payload. Runtime loading reads only package metadata and the
  `.arpui` payload; it does not parse PSD/AI/design-source files.
- Contract Complete: `UIRendererDrawPlanner` converts `.arpui` runtime
  extraction output into backend-neutral renderer-facing draw records with
  record kind, clipping, z-order, text payload, texture resource binding id,
  and renderer resource handles. The plan exposes no native backend handles.
- Contract Complete: `UiGameScreenDocument` and `UiGameScreenValidator`
  define explicit game UI screen semantics for screen/view/state ids, input
  actions, command roles, game/data bindings, visibility conditions,
  navigation, modal layer, focus scope, safe-area policy, and transitions. Pika
  game UI scope is documented in `docs/pika-ui-scope.md`.
- Contract Complete: Pika `.arpui` Authoring Workflow is documented in
  `docs/pika-arpui-authoring-workflow.md`. It defines the edit/lint/import/
  package/runtime-load workflow, diagnostics, and forbidden fallback rules for
  Pika `.arpui` assets.
- Prototype Complete: Not claimed. No PSD, Illustrator, screenshot, Figma, or
  other design-source parser is implemented in the engine. Real authoring
  source analysis is outside the engine and must emit explicit `.arpui` plus
  texture assets.
- Backend Validated: Not applicable. The document contract does not create GPU
  resources, descriptor bindings, native handles, or backend-owned allocations.
- Platform Validated: Limited to local Windows MinGW focused tests and the
  Pika UI demo command. Full `ninja-mingw-debug` platform evidence is blocked
  by the current CMake/Ninja build-directory placeholder-file issue.
- Production Ready: No. Production readiness requires migration policy,
  full Ninja/build-directory stability, backend/platform validation, and
  external platform evidence.

`.arpui` production semantics come from explicit metadata, not authoring-source
layer names or source-format heuristics. Missing canvas metadata, duplicate node
id, invalid parent id, unsupported control type, missing required resource
binding, and missing source-layer binding fail with stable diagnostics.

## Runtime Resource Management

Status:

- Contract Complete: `src/resource` defines runtime resource identity,
  lifecycle states, dependency metadata, deterministic `ArpRuntimeResource/v1`
  YAML envelope validation, content hash checks, reflected payload loading, and
  typed handle lookup.
- Prototype Complete: `resource_manager_smoke` registers a reflected resource
  type, declares a dependency, loads an internal YAML envelope, queries a typed
  handle, unloads the resource, and verifies a deterministic failure path.
- Backend Validated: Not applicable. This layer does not create GPU resources,
  descriptor bindings, native handles, or backend-owned allocations.
- Platform Validated: Limited to local Windows MinGW/Ninja focused tests.
- Production Ready: No. Production readiness requires binary package/chunk
  format decisions, migration policy, async/streaming and hot-reload contracts,
  larger resource corpus coverage, platform validation, and telemetry evidence.

Runtime resources are loaded from explicit metadata, not guessed source-file
semantics. The initial internal envelope stores `format`, `version`,
`resourceId`, `resourceType`, `status`, `contentHash`, `dependencies`, and a
reflection typed `payload`. Missing metadata, type mismatch, unresolved
dependencies, hash mismatch, duplicate ids, unknown reflected types, and
deserialization failures produce deterministic diagnostics.

Internal asset package v1:

- Contract Complete: `src/resource/internal_asset_package.*` defines
  `ArpInternalAssetPackage/v1` as a YAML package directory with
  `manifest.arpkg.yaml` plus one reflected `ArpRuntimeResource/v1`
  `.arpres.yaml` envelope per resource. Manifest entries record resource id,
  type, alias, relative envelope path, dependencies, status, and content hash.
- Prototype Complete: `arp_asset_importer --unity-prefab-folder <dir>
  --output-package <dir>` converts the existing Unity character folder source
  contract into the internal YAML package shape. The root runtime resource is
  `PrefabResource`; no internal resource type uses Unity-specific naming. The
  tool accepts explicit UUID, alias, and shader Slang path overrides so packages
  do not depend on guessed identities. Imported runtime data is indexed by a
  reflected `PrefabRuntimePayloadResource` and stored as a deterministic sidecar
  blob under `blobs/`, with envelope metadata recording blob path, byte size,
  hash, default state, default clip, and binding counts. `--self-test` also
  writes and validates a synthetic package without external Unity files.
- Backend Validated: Not applicable. Package writing and reading do not create
  GPU resources, descriptor bindings, native handles, or backend-owned
  allocations.
- Platform Validated: Limited to local Windows MinGW/Ninja focused tests.
- Production Ready: No. Production readiness still requires larger corpus
  validation, migration/version policy, streaming/chunking policy, platform
  validation, telemetry, and backend/platform readiness evidence.

Third-party asset conversion now has a Unity character Prototype path. Importers
must emit the runtime resource envelope or the internal package contract
explicitly; they must not bypass the runtime resource manager with native
handles or silent compatibility fallbacks.

## Prefab Runtime Package

Status:

- Contract Complete: resource identity is UUID-only for the internal prefab
  resource graph. Aliases such as `prefab/unitychan-pika-visible` resolve to
  UUIDs, and dependencies reference UUIDs rather than path-like ids.
- Prototype Complete: Pika can load `ArpInternalAssetPackage/v1`, resolve the
  `PrefabResource` root, validate its `PrefabRuntimePayloadResource`
  dependency, verify the sidecar blob hash/byte size, reconstruct
  `AnimatedMeshAsset` plus animator state bindings, and render a hidden
  one-frame Vulkan smoke without a `package/source` mirror.
- Backend Validated: Not claimed. GPU buffers, textures, descriptors,
  samplers, pipelines, render targets, and backend-native handles remain owned
  by renderer/RHI/backend layers.
- Platform Validated: Not claimed beyond focused local Windows MinGW/Ninja
  tests.
- Production Ready: No. Production readiness still requires offline internal
  package output, UUID registry/tooling, migration/version policy, broader
  corpus validation, platform validation, and telemetry evidence.

`ShaderPackageResource` records Slang source plus deterministic SPIR-V, MSL,
and reflection artifact paths. Runtime shader resources do not accept guessed
source formats or missing artifact metadata as production results.
