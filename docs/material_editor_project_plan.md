# Material Editor Project Plan

Status: Platform Validated for the standalone GLB material sphere editor with
glTF factor and extension-texture shader preview.

The material editor is a standalone project under `games/material_editor`.
It depends on engine/public sample APIs and the engine DLL, but it does not
include or call `games/project_lane` code.

## Boundaries

- `games/material_editor/content`: GLB/glTF material document loading and
  inspection models.
- `games/material_editor/app`: editor state, command line options, and runtime.
- `games/material_editor/presentation`: GUI node construction and preview mesh
  construction.
- `src/*`: engine APIs only; no editor-specific behavior is added here unless a
  later phase needs a reusable engine capability with contract tests.

## Phases

1. Contract Complete: material document, GUI, and sphere tests define the new
   project surface.
2. Prototype Complete: implement document parsing, editor state, GUI view, and
   preview sphere.
3. Prototype Complete: add standalone runtime and executable entry point.
4. Backend Validated: add split build script that links `arp_engine.dll` and
   runs fast tests plus hidden-window smoke.
5. Platform Validated: run visual capture for the Material Editor window and
   record any renderer limitations.
6. Platform Validated: wire supported glTF material extension factors and
   extension textures through importer, renderer material bindings, Slang
   shader sampling, SPIR-V/MSL generation, reflection, fast tests, hidden smoke,
   and a visible window capture.

## Verification

Each phase keeps at least one fast unit test and one hidden-window or smoke path.
The first hidden smoke target is:

```powershell
artifacts/direct/arp_material_editor.exe --hidden --frames 12
```

The current visual capture is:

```text
artifacts/direct/material_editor_shader_capture.png
```

Current shader coverage:

- Core glTF metallic-roughness inputs sample base color, normal,
  metallic-roughness, occlusion, and emissive textures.
- Supported extension factors now affect the preview shader:
  `KHR_materials_clearcoat`, `KHR_materials_specular`,
  `KHR_materials_sheen`, `KHR_materials_transmission`,
  `KHR_materials_volume`, `KHR_materials_ior`,
  `KHR_materials_iridescence`, and `KHR_materials_emissive_strength`.
- Supported extension textures now have renderer descriptor bindings and Slang
  samples: clearcoat, clearcoat roughness, clearcoat normal, specular,
  specular color, sheen color, sheen roughness, transmission, thickness,
  iridescence, and iridescence thickness.

Remaining limitation: this is a real-time preview implementation, not a full
offline glTF reference renderer. Extension texture coordinate sets, sampler
states, and every optional KHR material extension outside the supported list
remain future hardening work.
