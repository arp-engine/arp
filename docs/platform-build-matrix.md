# Platform Build Matrix

This document lists the intended platform build targets for the mobile-first roadmap. A preset may be present before the local machine has the matching SDK installed; missing SDKs should produce readable configuration diagnostics.

| Preset | Platform | Graphics API | Required SDKs | Purpose |
| --- | --- | --- | --- | --- |
| `win-vulkan-debug` / `win-vulkan-release` | Windows | Vulkan | MinGW, Ninja, Vulkan SDK (`slangc`, system `vulkan-1.dll`), Slang | Preferred local/CI Windows build; Win32 window system validated here |
| `web-webgpu-debug` / `web-webgpu-release` | Web | WebGPU | Emscripten SDK, Slang (WGSL output) | Browser canvas backend; window input bridge is Prototype |
| `macos-metal` / `mac-metal-debug` / `mac-metal-release` | macOS | Metal | Xcode command line tools, Metal framework, QuartzCore framework, Slang | Apple desktop backend validation |
| `ios-metal` | iOS arm64 | Metal | Xcode, iOS SDK, Metal framework, QuartzCore framework, Slang | Apple mobile production target (window system Planned) |
| `linux-vulkan` | Linux | Vulkan | C++17 compiler, Ninja, Vulkan SDK, `libxcb`, `libxkbcommon`, Slang | Portability and CI hardening |
| `android-arm64-vulkan` | Android arm64 | Vulkan | Android NDK, Vulkan SDK headers, Slang | First mobile production target (window system Planned) |

No preset requires SDL3 anymore: the L2 window contract (`WindowSystem/1`, see
`platform-window-contract.md`) provides native windowing per platform, and the
architecture boundary test fails on any `SDL_` token under `src/`. Windows
surfaces need only the system Vulkan loader (`vulkan-1.dll`, installed with GPU
drivers / Vulkan runtime); Linux needs the X11 client libraries `libxcb` and
`libxkbcommon` for the X11 window implementation.

## Environment Variables

- `VULKAN_SDK`: Vulkan headers, loader tools, and optional `slangc` discovery on Vulkan platforms.
- `ANDROID_NDK`: Android NDK root used by the `android-arm64-vulkan` preset.
- `SLANGC_EXECUTABLE`: explicit Slang compiler path when it is not available through `VULKAN_SDK`.

## Capability Report

The `arp_platform_capabilities` tool prints a small Platform Capability Report. It is intentionally lightweight: it reports compiled-in backend contracts and the expected platform matrix without requiring every target SDK to be installed on the current machine.

## Validation Order

1. Configure the preset.
2. Build the preset.
3. Run available contract tests.
4. Run backend smoke tests on hardware when the platform is available.
5. Record whether the feature is Contract Complete, Prototype Complete, Backend Validated, Platform Validated, or Production Ready.
