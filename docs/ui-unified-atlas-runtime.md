# Unified UI Atlas Runtime

Status: Contract Complete.

This document defines the boundary contract for the long-term product UI atlas
and glyph text runtime. It is a contract and ownership document only; it does
not claim backend validation, platform validation, or production readiness.

## Source Of Truth

Pika RuntimeScene is the only product render source of truth. Product UI is
mounted as GameObject + ui::DocumentComponent and flows through the runtime UI
system before any renderer work is visible.

Required product flow:

```text
RuntimeScene -> ui::DocumentComponent -> ui::Runtime -> ui::RenderExtractor
  -> ui::DrawPlan -> renderer::ui::TextLayoutCache
  -> renderer::ui::GlyphAtlasCache -> renderer::ui::UnifiedUiAtlas
  -> renderer::ui::DrawPlan lowering -> RHI UI pass
```

The product path has one logical UI atlas resource path for image slices and glyphs.
Pika product UI binds a stable logical atlas resource id; image panels,
icons, text glyphs, and future UI sprite slices resolve through the same logical
resource contract.

The authoring side uses `ui.atlas/v2` YAML for atlas metadata and `ui.theme/v1`
YAML for style metadata. `fontToken is the font source of truth`; text styles
must resolve a declared token before layout or glyph lookup can proceed.

## Logical Atlas And Physical Atlas

Logical Atlas means the engine-facing resource identity and semantic contract.
It describes base image slices, font faces, preload glyph sets, glyph cache
policy, page policy, diagnostics, and the logical atlas resource id.

Physical Atlas means the backend-owned texture representation chosen by
capability and lowering. A backend may store the logical atlas as one texture,
a texture array, or multiple pages, but product UI and scene code cannot observe
native handles or backend-specific allocation details.

Allowed page policies:

- `singleTexture`: all image slices and glyphs fit in one physical texture.
- `textureArray`: pages are represented by layers of one texture-array binding.
- `multiPage`: pages are represented by explicit page indices and deterministic
  page binding metadata.

Vulkan, Metal, WebGPU, and Null support must be expressed through explicit
capability records and lowering contracts. Unsupported page policy, missing
page binding support, atlas full, missing glyph, missing font token, and
unsupported dynamic glyph behavior must produce a deterministic diagnostic.

## Ownership

- `src/ui` owns UI documents, theme contracts, `ui.atlas/v2` authoring metadata,
  runtime tree state, controls, layout intent, resource ids, style ids,
  font-token resolution, document components, render extraction, and diagnostics.
- `src/renderer/ui` owns text layout lowering, glyph atlas cache, unified atlas
  packing/runtime service, page policy lowering, RHI UI draw command lowering,
  descriptor and resource-state requirements, capability checks, and renderer
  diagnostics.
- `src/renderer/gui` is a temporary bridge only. It can translate current UI
  draw commands while migration is in progress, but it must not own product text
  rasterization policy, glyph cache policy, YAML parsing, style parsing, or
  fallback UI semantics.
- `src/gui` is a debug/tool/editor facade only. Debug GUI text fallback may
  remain for tools, editor overlays, and diagnostics, but product Pika UI must
  not depend on it.
- `games/pika` is the product acceptance site. It mounts product UI through the
  runtime scene and consumes the unified atlas path.
- `assets/pika/ui` stores YAML-only UI authoring resources. Atlas, theme, and
  font metadata must declare explicit contracts and fail validation when
  required semantics are missing.

## Forbidden Behavior

The following phrases name prohibited product behavior and are intentionally
stable for contract tests:

- product UI text bypasses atlas.
- product UI uses debug GUI text fallback.
- renderer parses YAML.
- renderer reads font files per frame.
- backend silently creates missing glyphs.

Additional hot-path restrictions:

- No per-frame font file IO.
- No per-frame atlas rebuild.
- No per-frame source parsing.
- No per-frame shader compilation.
- No per-draw pipeline creation.
- No native backend handles exposed.

Renderer code consumes validated runtime data and lowered draw intent. YAML
parsing, compatibility conversion, resource redirection, and semantic validation
belong to `src/ui` or asset/tooling contracts before renderer submission.

## Status Labels

- Contract Complete: the semantic boundary, ownership, diagnostics, and tests
  exist. Contract Complete is not Backend Validated.
- Prototype Complete: a local implementation exists for a limited path, but it
  may still lack complete backend or platform validation.
- Backend Validated: Null plus required native backend behavior is covered by
  focused tests or deterministic capability diagnostics.
- Platform Validated: lifecycle and platform behavior are validated for the
  relevant OS/device class. Platform Validated is not Production Ready.
- Production Ready: Pika product UI acceptance, backend capability behavior,
  platform lifecycle handling, hot-path constraints, deterministic diagnostics,
  and documentation are all complete.

Passing this document contract only allows later implementation phases to start.
It does not claim Vulkan, Metal, WebGPU, mobile, or Pika visible production
acceptance.
