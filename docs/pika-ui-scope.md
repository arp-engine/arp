# Pika `.arpui` Game UI Scope

Status: Contract Complete target. Backend Validated, Platform Validated, and
Production Ready are not claimed by this scope document.

Pika UI is authored as explicit `.arpui` documents and loaded by the engine UI
runtime. Runtime, renderer, RHI, samples, and gameplay code must not parse PSD,
Figma, screenshots, or other design-source files. Authoring source names may be
diagnostic labels only.

## Required Screens

- Main menu: start, continue when available, settings, exit.
- HUD: health, energy, current action/state, objective prompt, transient status.
- Pause menu: resume, restart, settings, return to main menu.
- Settings: audio volume, graphics preset, shadow toggles, input display,
  language placeholder.
- Character/status panel: current character, animation state, material/skin
  selection, runtime feature toggles.
- Toast/dialog layer: short messages, confirmation dialog, error diagnostic.
- Loading UI: progress text, package/resource status, deterministic failure
  message.
- Development-only debug UI: hidden by default and gated by explicit debug state.

## Required `.arpui` Semantics

Every game screen must declare explicit screen, view, state, input, command,
navigation, modal, focus, safe-area, and data-binding metadata. The runtime must
fail deterministically when semantics are missing or inconsistent. It must not
infer gameplay meaning from layer names, control names, or ordering.

Status labels remain separate from validation evidence:

- Contract Complete: `.arpui` semantics and deterministic diagnostics are
  defined and tested.
- Backend Validated: a named renderer/RHI backend path validates rendering.
- Platform Validated: a named visible platform demo is run and recorded.
- Production Ready: requires migration, telemetry, lifecycle, and final smoke
  evidence.
