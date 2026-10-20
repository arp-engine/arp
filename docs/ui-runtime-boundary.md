# Unified UI Runtime Boundary

Status: Contract Complete.

This document locks the module ownership contract for the unified product UI
runtime. It is a boundary contract, not a backend or platform validation claim.

## Ownership

- `src/ui is the only formal product UI runtime`. It owns `.arpui` YAML
  document contracts, document validation, retained runtime state, typed
  controls, layout, style, theme resolution, input routing, focus/capture,
  render extraction, draw plan creation, diagnostics, and UI framework status
  labels.
- `src/gui is a debug/tool/editor facade only`. It may provide builder
  ergonomics for debug overlays, editor tools, samples, and temporary
  diagnostics, but it must not own a product retained tree, product layout
  engine, product input router, product draw data contract, product resource
  contract, or product YAML/parser path.
- `src/renderer/ui is draw lowering only`. It consumes `ui::DrawPlan`-level
  intent and lowers it to RHI. It cannot include UI authoring, `.arpui`
  parsing, retained tree mutation, layout ownership, input routing ownership,
  or scene ownership.
- `scene owns only UI entry and bridge components`. Scene components may mount a
  UI document/runtime route or carry bridge handles, but they do not own
  screen-space UI nodes as scene child objects.
- `src/ui and src/2d do not depend on each other's runtime node implementations`.
  Their relationship is by explicit ids, bindings, commands, events, and anchor
  projection contracts.

## 2D/UI Contract

Allowed 2D/UI relationships:

- Resource id sharing for fonts, textures, atlases, and theme resources through
  explicit resource metadata.
- Entity id references or stable handles in binding/view-model contracts.
- Command/event contracts from UI to gameplay or gameplay to UI.
- World, screen, and 2D anchor projection bridges that reference target ids by
  value and fail deterministically when metadata is missing.

Forbidden 2D/UI relationships:

- A UI runtime node holding a sprite implementation pointer.
- A sprite component holding a UI runtime node pointer.
- A UI node becoming a 2D child node.
- The 2D renderer consuming the UI tree directly.
- The UI renderer consuming sprite node internals directly.

## Pika Product UI

- Pika product UI cannot include gui/imgui_extension.h.
- Pika product UI cannot depend on gui::GuiContext.
- Pika product UI cannot consume `gui::DrawData`.
- Pika product UI must mount through `src/ui` document/runtime entry points and
  render through `ui::RenderExtractor` and `ui::DrawPlan`.
- Debug-only Pika overlays may use `src/gui` only when explicitly marked as
  debug/tool UI and kept outside the product UI path.

## Current Source Inventory

Phase 9 source state:

- `src/gui/gui.h no longer exports gui::Node`.
- `src/gui/gui.h no longer exports gui::DrawData`.
- `src/gui/gui.h no longer exports gui::GuiContext` as a product runtime.
  The deprecated alias refers only to DebugGuiContext for existing debug callers.
- `src/gui/gui.h exposes gui::Builder as the facade over ui::Runtime`.
- Remaining retained debug helper shapes use explicit `DebugNode`,
  `DebugDrawData`, and `DebugGuiContext` names for debug/tool/editor/sample
  usage only.

Any new product path that depends on `src/gui` debug runtime shapes is a
deterministic contract failure.

## Readiness Labels

- This boundary is Contract Complete.
- Existing `src/gui` implementation is not Backend Validated, Platform
  Validated, or Production Ready for product UI.
- Passing this document/test contract does not claim renderer backend readiness
  or Pika visible acceptance.
