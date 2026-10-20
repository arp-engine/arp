# Pika `.arpui` Authoring Workflow

Status: Contract Complete. Backend Validated, Platform Validated, and Production Ready are not claimed.

Pika UI is authored by editing `.arpui` files under `assets/pika/ui`. Runtime, renderer, RHI, samples, and gameplay code consume explicit `.arpui` metadata only.

## Schema Example

```json
{
  "type": "ArpUiDocument",
  "contractVersion": "arpui.v1",
  "canvasId": "pika.hud.canvas",
  "screenId": "pika.hud",
  "viewId": "hud.default",
  "stateId": "state.in_game",
  "safeAreaPolicy": "RespectPlatformSafeArea",
  "modalLayer": "Base",
  "focusScope": "hud.focus",
  "commandRole": "command.pause",
  "gameBinding": "pika.session.pause",
  "dataBinding": "binding.player.health",
  "canvas": { "width": 1280, "height": 720, "dpi": 96 },
  "textures": [
    {
      "resourceId": "ui/pika/hud_bar",
      "layer": "hud_bar",
      "atlas": "pika_ui_atlas",
      "x": 0,
      "y": 96,
      "width": 256,
      "height": 32,
      "pivotX": 0.0,
      "pivotY": 0.5,
      "sliceLeft": 8,
      "sliceTop": 8,
      "sliceRight": 8,
      "sliceBottom": 8,
      "alpha": "straight",
      "srgb": true,
      "mips": "none",
      "compression": "ui-lossless",
      "sampler": "linear-clamp"
    }
  ],
  "nodes": [
    {
      "id": 1,
      "parent": 0,
      "type": "Panel",
      "layer": "hud_root",
      "x": 24,
      "y": 24,
      "width": 360,
      "height": 96,
      "style": "hud.panel",
      "state": "state.in_game",
      "event": "none"
    }
  ]
}
```

## Lint

Run source-contract checks after editing Pika UI assets:

```text
artifacts/pika_ui_asset_set_tests_manual.exe
artifacts/pika_ui_theme_contract_tests_manual.exe
artifacts/pika_ui_hidden_window_smoke_tests_manual.exe
```

The checks require explicit screen ids, command roles, game bindings, data bindings, texture resource ids, atlas slice metadata, and theme tokens.

## Import

The production runtime path does not import PSD, Illustrator, Figma, screenshots, or other design-source files. External tools may generate `.arpui` plus texture assets before the engine sees the result. Any source manifest import must provide explicit node ids, parent ids, control types, resource bindings, texture slices, and game bindings.

## Package

Package planning treats `.arpui` and referenced texture resources as explicit runtime resources. Texture upload, descriptor allocation, native handles, and backend-specific memory policy remain backend-owned.

## Runtime Load

Use the command contract:

```text
arp_pika --mode ui-demo --ui-package assets/pika/ui --frames 1
```

Managed-mode evidence currently validates the command wiring and hidden renderer-facing draw planning. A visible Windows run of a current executable build is required before Platform Validated can be claimed.

## Diagnostics

Expected deterministic diagnostics include:

- `MissingRequiredResourceBinding` for required texture/resource bindings that are absent.
- `InvalidTextureMetadata` for incomplete atlas, rect, pivot, nine-slice, alpha, color-space, mip, compression, or sampler metadata.
- `MissingCommandBinding` for input actions without explicit command bindings.
- `MissingGameBinding` for UI commands or data bindings that do not declare Pika game bindings.

## Forbidden Fallback Rules

Do not infer semantics from layer names.

Do not parse PSD, Illustrator, Figma, or screenshots at runtime.

Do not silently guess missing texture, atlas, command, state, safe-area, or data-binding semantics.

If an authoring-source conversion cannot determine semantics, fail with diagnostics or explicitly mark the result as `Prototype/Legacy fallback`.
