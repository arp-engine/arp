# Pika Visible UI Demo Command

Status: Contract Complete with local Windows MinGW command validation. Platform Validated is not claimed. Production Ready is not claimed.

The Pika UI demo command is:

```text
arp_pika --mode ui-demo --ui-package assets/pika/ui --frames 1
```

The command contract is intentionally explicit:

- `--mode ui-demo` selects the Pika `.arpui` UI demo path.
- `--ui-package assets/pika/ui` points to the directory containing Pika `.arpui` screen, theme, and atlas metadata.
- `--frames 1` constrains the demo to one frame for local verification.

The command path uses the Pika UI render bridge to load every required Pika `.arpui` game screen, resolve layout, extract UI render records, and plan renderer-facing draw records. The draw plans must report no native backend handles.

The visible character runtime uses `PikaUiInteractiveSession` for runtime UI
input:

- `Escape` opens Pause from HUD, resumes from Pause, and backs out of Settings.
- `Tab` opens and closes the Character panel from HUD.
- `B` opens the Backpack from HUD or Character panel, and closes it back to HUD.
- `Enter` activates the currently focused interactive `.arpui` event.
- Up/Down advance focus through explicit interactive nodes.
- Left mouse click hit-tests the active `.arpui` layout and executes the hit node's explicit `command.*` event.
- Character Panel `command.select_animation_state` cycles the selected state
  through the loaded character asset's explicit animator state list and feeds
  the selected state into `CharacterRuntimeFrameRequest::stateName`.

Local evidence from 2026-06-10:

```text
C:\mingw64\bin\mingw32-make.exe -C artifacts\cmake\mingw-debug arp_pika -j1
```

The latest target-level build completed successfully without running the full
test suite.

```text
artifacts\cmake\mingw-debug\arp_pika.exe --mode ui-demo --ui-package assets/pika/ui --frames 1
```

Output:

```text
Pika UI demo contract: screens=8 records=183 textRecords=89 imageRecords=69 nativeHandles=0 uiPackage=assets/pika/ui
```

The command validates the current Windows MinGW executable path and confirms
that the complete Pika `.arpui` UI package, including the backpack screen,
produces renderer-facing records without native backend handles. The visible character runtime also submitted
the HUD overlay with `uiRecords=33`, `uiTextRecords=16`, `uiImageRecords=3`,
`uiOverlay=1`, `selectedAnimationState=Take 001`, and `animationStates=1` after
the interactive session replaced the previous static HUD-only bridge. The
current bundled internal package exposes one animator state; multi-state
packages use the same explicit state-list cycling path. Full cross-platform
Production Ready evidence is not claimed until external platform validation,
dynamic gameplay data binding, final settings application, and the remaining
migration decision are complete.
