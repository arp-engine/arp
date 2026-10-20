# arp-engine

> AI Render Pipeline — A contract-driven, cross-platform, mobile-first 3D rendering library.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![Build Status](https://img.shields.io/badge/build-MinGW%20%7C%20Ninja-success)](CMakePresets.json)
[![Slang](https://img.shields.io/badge/shader-Slang-8A2BE2)](https://shader-slang.com/)
[![Vulkan](https://img.shields.io/badge/Vulkan-1.3-red.svg)](https://www.vulkan.org/)
[![Metal](https://img.shields.io/badge/Metal-supported-8C8C8C.svg)](https://developer.apple.com/metal/)
[![WebGPU](https://img.shields.io/badge/WebGPU-experimental-green.svg)](https://www.w3.org/TR/webgpu/)
[![Platforms](https://img.shields.io/badge/platform-Windows%20%7C%20Android%20%7C%20macOS%20%7C%20iOS%20%7C%20Linux%20%7C%20Web-blue)](docs/platform-build-matrix.md)

---

---

<p align="center">
  <a href="#en"><b>English</b></a> •
  <a href="#zh"><b>中文</b></a> •
  <a href="#ja"><b>日本語</b></a> •
  <a href="#de"><b>Deutsch</b></a> •
  <a href="#ru"><b>Русский</b></a> •
  <a href="#ja"><b>日本語</b></a> •
  <a href="#de"><b>Deutsch</b></a> •
  <a href="#ru"><b>Русский</b></a>
</p>

---

<a id="en"></a>

arp-engine is a contract-driven, cross-platform rendering library for stylized 3D rendering. Rendering decisions are expressed through versioned, verifiable contracts rather than implicit fallback paths.

**Project status**: Contract Complete / Prototype Complete on Vulkan; active mobile-first cross-platform migration.

**Standard editor**: [ARP Editor](editor/README.md), maintained in the root
`editor/` directory. Build with `python scripts/build.py editor`; the `engine`
and `all` commands include it by default. Launch `build/win-vulkan-debug/arp_editor.exe`
with `--project-root <project>` to browse files, edit ArpScript and preview a game.
Editor functionality is currently Prototype Complete.

---

## Design Philosophy

### Data-driven

Rendering and resource behavior is expressed through explicit, versioned data and contracts:

- **Structured authoring** — ARP Editor uses the engine's UI and resource contracts; rendering decisions come from structured, versioned data (C++ contracts, YAML configs, JSON reflection, SPIR-V decoration assertions).
- **Deterministic over convenient** — every fallback path, capability gate, and lowering decision is declared before execution. There is no "silent success with different behavior."
- **Contract-first** — interfaces and tests describe intended behavior *before* implementation. Backend code only executes approved lowering decisions.

### Contract-driven RHI

The rendering hardware interface (RHI) follows a strict 4-layer governance model:

```
platform → renderer → rhi::semantics → rhi backend ← backend
```

1. **Define** L2 domain semantics (descriptors, resource states, commands, synchronization)
2. **Contract** the lowering per backend (Native / Equivalent / Degraded / DiagnosticFailure)
3. **Implement** backend-private paths
4. **Consume** via RenderGraph and renderer — upper layers express only L2 intent, never hold native handles

### Four backends, one contract

Null, Vulkan, Metal, and WebGPU are four mandatory implementations of the same L2 contract. A feature is not "done" until all four backends have an explicit answer.

| Backend | Status | Target |
|---------|--------|--------|
| Null | Reference validation | Contract testing, CI |
| Vulkan | Primary, backend-validated | Windows (P0), Android (P0) |
| Metal | Contract complete, platform unvalidated | macOS (P1), iOS (P1) |
| WebGPU | Prototype, experimental | Web (P2) |

See the [RHI Capability Matrix](RHI_CAPABILITY_MATRIX.md) for per-backend lowering decisions.

---

## Key Features

### Rendering

- **Deferred + Forward rendering** via self-authored FrameGraph / RenderGraph
- **DDGI** (Dynamic Diffuse Global Illumination) — probe-grid-based, GPU-compute-driven GI
- **Advanced shadowing** — CSM cascaded shadow maps, PCF filtering, shadow atlas, contact shadows
- **Post-processing chain** — FXAA, SSAO, Bloom, LUT tonemapping
- **Particle system** — particles, mesh particles, trail/beam effects
- **Procedural generation** — 33+ engine-level modules for terrain, erosion, rivers, vegetation, biomes, weather, roads, settlements, factions, and procedural textures
- **Async compute** — compute-graphics overlap with semaphore-based synchronization
- **Subpass / framebuffer fetch** — native on Vulkan, probe-gated native on Metal, emulated on WebGPU

### Cross-platform Support

| Platform | API | Priority | Status |
|----------|-----|----------|--------|
| Windows | Vulkan | P0 | Native, backend-validated |
| Android | Vulkan | P0 | Contract complete, active migration |
| macOS | Metal | P1 | Contract complete, platform unvalidated |
| iOS | Metal | P1 | Contract complete, platform unvalidated |
| Linux | Vulkan | P2 | Build configured |
| Web | WebGPU | P2 | Prototype, experimental |

### Architecture

- **FrameGraph / RenderGraph** — compiled DAG-based pass composition with transitive culling, transient aliasing, and compile-time barrier planning
- **Self-authored retained GUI** (`src/ui/`) — zero external GUI dependencies (no ImGui)
- **Physics** (`src/physics/`) — Jolt Physics integration with backend abstraction
- **Animation** (`src/animation/`) — skeletal animation, blend trees, animator controllers, IK (inverse kinematics), foot IK, pose blending, CPU skinning
- **Networking** (`src/network/`) — HTTP, WebSocket, TLS with cross-platform IO service
- **2D / 3D modules** — scene representation, bounds, colliders, components
- **Resource system** (`src/resource/`) — async loading, resource graph, lifecycle, reload
- **Input** (`src/input/`) — cross-platform input abstraction
- **Reflection / codegen** (`src/reflection/`) — compile-time reflection and code generation
- **Slang-only shader pipeline** — single source generates SPIR-V, MSL, and WGSL
- **Reflection-driven validation** — automatic shader reflection contract tests for binding layout, push-constant size, and SPIR-V decoration correctness
- **Bounded allocation** — all runtime memory through `core::pmr` allocators; no hidden STL allocations on hot paths
- **Mobile-quality awareness** — 4-tier quality profiles (Low / Medium / High / Desktop) with explicit capability degradation

---

## Project Structure

```
arp-engine/
├── editor/             # Standard ARP Editor application, YAML UI and resources
├── src/
│   ├── core/            # Foundation: math, allocators, file I/O, logging, async runtime
│   ├── rhi/
│   │   ├── backends/    # Backend implementations (null, vulkan, metal, webgpu)
│   │   └── semantics/   # Contract lowering, fallback plans, capability queries
│   ├── renderer/
│   │   ├── render_graph/# FrameGraph / RenderGraph compiler and executor
│   │   ├── postprocess/ # FXAA, SSAO, bloom
│   │   ├── particles/   # Particle/VFX system
│   │   ├── pipeline/    # Render pipeline orchestration
│   │   └── ...          # Scene, GUI, materials, shadow, GI
│   ├── physics/         # Physics module with Jolt backend
│   ├── animation/       # Skeletal animation, blend trees, animator controllers
│   ├── network/         # HTTP, WebSocket, TLS, cross-platform IO
│   ├── assets/          # Mesh, material, import contracts
│   ├── resource/        # Async resource loading, lifecycle, graph
│   ├── shader/          # Shader compiler interface + reflection validation
│   ├── pcg/             # Procedural generation (33 modules)
│   ├── scene/           # Scene graph, camera, game object
│   ├── engine/          # Engine DLL interface
│   ├── platform/        # RenderSystem, window, lifecycle (Win/Android/iOS/macOS)
│   ├── ui/              # Self-authored retained GUI
│   ├── gui/             # ImGui integration (tooling)
│   ├── input/           # Cross-platform input abstraction
│   ├── reflection/      # Compile-time reflection and code generation
│   ├── 2d/              # 2D scene primitives
│   └── 3d/              # 3D scene primitives
├── games/
│   └── world_of_claudecraft/  # WoCC demo game
│       ├── assets/shaders/    # Game Slang shaders (14 files)
│       ├── app/               # Game application
│       ├── gameplay/          # Gameplay logic
│       ├── content/           # Procedural content configs
│       ├── presentation/      # UI / screen flow
│       ├── tools/             # Editor tooling
│       └── tests/             # Game-specific tests
├── tools/               # Engine tools
│   ├── ai_asset_generator/   # Meshy AI 3D asset generation
│   ├── asset_importer/       # Asset pipeline importer
│   ├── cluster_previewer/    # Cluster asset preview
│   ├── platform_capabilities/ # Platform capability reporting
│   ├── reflect_codegen/      # Reflection code generator
│   ├── unity_import/         # Unity asset conversion pipeline
│   └── wocc_asset_importer/  # WoCC asset importer
├── shaders/             # Core Slang shader sources
├── tests/
│   ├── contract/        # RHI contract verification tests
│   ├── smoke/           # Hidden-window smoke tests
│   ├── unit/            # Unit tests
│   └── visual/          # Visual regression tests
├── docs/                # Architecture documentation (90+ documents)
├── 3rd/                 # Third-party libraries
│   ├── SDL3-3.4.8/      # Platform abstraction
│   ├── JoltPhysics-5.6.0/ # Physics engine
│   ├── assimp-6.0.5/    # Asset import
│   ├── mimalloc/        # Memory allocator
│   ├── freetype/        # Font rendering
│   ├── fkYAML/          # YAML parsing
│   ├── stb/             # Single-file utilities
│   └── VulkanMemoryAllocator/ # GPU memory allocator
├── CMakeLists.txt       # Build configuration
```

---

## Build

### Prerequisites

- **Compiler**: MinGW-w64 (g++ with C++17 support)
- **Build system**: Ninja (recommended) or GNU Make
- **Graphics SDK**: Vulkan SDK 1.3+ (with `slangc`)
- **Libraries**: SDL3 (bundled in `3rd/`)
- **Python**: 3.x (for build scripts and code generation)

### Quick Start (Windows + MinGW)

```powershell
# Build everything (shaders, DLLs, and executables)
.\scripts\build.py

# Run the World of Claudecraft demo game
.\artifacts\direct\arp_world_of_claudecraft.exe
```

### CMake Preset Build

```powershell
cmake --preset ninja-mingw-debug
cmake --build --preset ninja-mingw-debug
ctest --preset ninja-mingw-debug
```

### Shader Compilation

```powershell
.\scripts\compile_shaders.py
```

Outputs SPIR-V (`.spv`), Metal (`.metal`), WebGPU (`.wgsl`) + reflection JSON to `artifacts/direct/shaders/`.

### Platform-Specific Builds

| Target | Command |
|--------|---------|
| Windows Vulkan | `cmake --preset ninja-mingw-debug` |
| macOS Metal | `cmake --preset macos-metal` |
| iOS Metal | `cmake --preset ios-metal` |
| Android Vulkan | `cmake --preset android-arm64-vulkan` |
| Web WebGPU | `scripts/build_character_viewer_web.ps1` (Emscripten) |
| Linux Vulkan | `cmake --preset linux-vulkan` |

See [Platform Build Matrix](docs/platform-build-matrix.md) for detailed SDK requirements.

---

## Run

### World of Claudecraft (demo game)

The project ships one demo game: **World of Claudecraft (WoCC)** — a stylized open-world experience.

```powershell
.\artifacts\direct\arp_world_of_claudecraft.exe
```

Opens a Vulkan window with heightfield terrain, procedurally placed vegetation and props, a hero character, time-of-day lighting, cascaded shadow maps, DDGI global illumination, post-processing (FXAA/SSAO/Bloom/tone-mapping), a particle system, and a retained GUI status panel.

### Smoke tests

Headless validation runs are available for CI:

```powershell
.\build\win-auto-debug\tests\smoke\arp_xxxx_smoke.exe
```

---

## Documentation

The `docs/` directory contains 90+ documents covering:

| Category | Documents |
|----------|-----------|
| Architecture | RHI governance, renderer data model, module governance, migration boundary |
| Roadmap | Project roadmap, mobile-first cross-platform, FrameGraph v2, GI roadmap |
| Capabilities | [RHI Capability Matrix](RHI_CAPABILITY_MATRIX.md), platform build matrix |
| Rendering | GI (DDGI), lighting & shadows, fog, god rays, bloom, post-processing |
| Mobile | Mobile rendering baseline, quality profiles, Android/iOS lifecycle contracts |
| Procedural | Terrain, erosion, vegetation, biome generation plans |

Key architecture documents:
- [RHI Layer Governance](docs/rhi-layer-governance.md) — The 4-layer model
- [RHI Capability Matrix](RHI_CAPABILITY_MATRIX.md) — Per-backend lowering decisions
- [Platform Build Matrix](docs/platform-build-matrix.md) — Supported build targets
- [FrameGraph v2 Roadmap](docs/framegraph-v2-roadmap.md) — RenderGraph evolution

---

## Status Vocabulary

| Status | Meaning |
|--------|---------|
| **Contract Complete** | Interfaces and tests describe intended behavior |
| **Prototype Complete** | Feature runs in at least one sample path |
| **Backend Validated** | Implemented and tested on a real graphics backend |
| **Platform Validated** | Passed on a named hardware platform |
| **Production Ready** | Meets runtime, memory, and lifecycle requirements for shipping |

---

## License

MIT License. See [LICENSE](LICENSE).

---

## Acknowledgments

- [Slang](https://shader-slang.com/) — Cross-platform shader compilation
- [SDL3](https://libsdl.org/) — Platform abstraction layer
- [Vulkan SDK](https://www.lunarg.com/vulkan-sdk/) — Graphics API
- [Assimp](https://github.com/assimp/assimp) — Asset import
- [Jolt Physics](https://github.com/jrouwe/JoltPhysics) — Physics simulation
- [mimalloc](https://github.com/microsoft/mimalloc) — Memory allocator

---

<p align="center"><a href="#en">Back to top ↑</a></p>

---

<a id="zh"></a>

# arp-engine

> AI Render Pipeline — 契约驱动、跨平台、移动优先的 3D 渲染库。

<p align="center">
  <a href="#en"><b>English</b></a> •
  <a href="#zh"><b>中文</b></a> •
  <a href="#ja"><b>日本語</b></a> •
  <a href="#de"><b>Deutsch</b></a> •
  <a href="#ru"><b>Русский</b></a>
</p>

---

arp-engine 是一个契约驱动的跨平台渲染库，面向风格化 3D 渲染。渲染决策通过可版本化、可验证的契约表达，避免隐式 fallback 路径。

**项目状态**：Vulkan 后端达到 Contract Complete / Prototype Complete；移动优先跨平台迁移进行中。

---

## 设计哲学

### 数据驱动

渲染和资源行为通过显式、版本化的数据与合同表达：

- **无 Shader Graph、无可视化编辑器**——所有渲染决策来自结构化、可版本化的数据（C++ 契约、YAML 配置、JSON reflection、SPIR-V decoration 断言）
- **确定性优于便利性**——每条 fallback 路径、每个能力门控、每个降级决策都在执行前声明，不存在"静默成功但行为不同"
- **契约优先**——接口和测试在实现*之前*描述预期行为；后端代码只执行已批准的降级决策

### 契约驱动的 RHI

渲染硬件接口遵循严格的 4 层治理模型：

```
platform → renderer → rhi::semantics → rhi backend ← backend
```

1. **定义** L2 领域语义（描述符、资源状态、命令、同步）
2. **契约化** 各后端的降级方案（原生 / 等价 / 降级 / 诊断失败）
3. **实现** 后端私有路径
4. **消费** 通过 RenderGraph 和 renderer——上层只表达 L2 意图，绝不持有 native handle

### 四后端、一契约

Null、Vulkan、Metal、WebGPU 是同一份 L2 契约的四个强制实现。一个特性在四个后端都有明确结论之前，不算"完成"。

| 后端 | 状态 | 目标平台 |
|------|------|---------|
| Null | 参考验证 | 契约测试、CI |
| Vulkan | 主后端，已验证 | Windows (P0), Android (P0) |
| Metal | 契约完整，平台未验证 | macOS (P1), iOS (P1) |
| WebGPU | 原型，实验性 | Web (P2) |

详见 [RHI Capability Matrix](RHI_CAPABILITY_MATRIX.md) 中各后端的降级决策。

---

## 核心特性

### 渲染

- **延迟 + 前向渲染**，基于自研 FrameGraph / RenderGraph
- **DDGI**（动态漫反射全局光照）——基于探针网格、GPU 计算驱动的全局光照
- **高级阴影**——CSM 级联阴影贴图、PCF 滤波、阴影图集、接触阴影
- **后处理链**——FXAA、SSAO、Bloom、LUT 色调映射
- **粒子系统**——粒子、网格粒子、拖尾/光束效果
- **程序化生成**——33+ 引擎级模块，涵盖地形、侵蚀、河流、植被、生物群落、天气、道路、定居点、派系和程序化纹理
- **异步计算**——基于信号量的计算-图形重叠同步
- **子通道 / framebuffer fetch**——Vulkan 原生、Metal 探测门控原生、WebGPU 模拟

### 跨平台支持

| 平台 | API | 优先级 | 状态 |
|------|-----|--------|------|
| Windows | Vulkan | P0 | 原生，已验证 |
| Android | Vulkan | P0 | 契约完整，迁移中 |
| macOS | Metal | P1 | 契约完整，平台未验证 |
| iOS | Metal | P1 | 契约完整，平台未验证 |
| Linux | Vulkan | P2 | 构建已配置 |
| Web | WebGPU | P2 | 原型，实验性 |

### 架构

- **FrameGraph / RenderGraph**——基于编译 DAG 的 Pass 组合，支持传递性剔除、瞬态资源别名和编译时 barrier 规划
- **自研保留模式 GUI**（`src/ui/`）——零外部 GUI 依赖（无 ImGui）
- **物理**（`src/physics/`）——Jolt Physics 集成，带后端抽象
- **动画**（`src/animation/`）——骨骼动画、混合树、动画控制器
- **网络**（`src/network/`）——HTTP、WebSocket、TLS，跨平台 IO 服务
- **2D / 3D 模块**——场景表示、包围盒、碰撞体、组件
- **资源系统**（`src/resource/`）——异步加载、资源图、生命周期、热重载
- **输入**（`src/input/`）——跨平台输入抽象
- **反射 / 代码生成**（`src/reflection/`）——编译时反射和代码生成
- **Slang 唯一着色器管线**——单一源码生成 SPIR-V、MSL 和 WGSL
- **反射驱动验证**——自动着色器反射契约测试，验证 binding layout、push-constant size 和 SPIR-V decoration 正确性
- **有界分配**——所有运行时内存通过 `core::pmr` 分配器，热路径无隐式 STL 分配
- **移动端质量感知**——4 级质量档位（低 / 中 / 高 / 桌面），带显式能力降级

---

## 项目结构

```
arp-engine/
├── src/
│   ├── core/            # 基础：数学、分配器、文件 IO、日志、异步运行时
│   ├── rhi/
│   │   ├── backends/    # 后端实现 (null, vulkan, metal, webgpu)
│   │   └── semantics/   # 契约降级、fallback 方案、能力查询
│   ├── renderer/
│   │   ├── render_graph/# FrameGraph / RenderGraph 编译器和执行器
│   │   ├── postprocess/ # FXAA, SSAO, bloom
│   │   ├── particles/   # 粒子/VFX 系统
│   │   ├── pipeline/    # 渲染管线编排
│   │   └── ...          # 场景, GUI, 材质, 阴影, GI
│   ├── physics/         # 物理模块 (Jolt 后端)
│   ├── animation/       # 骨骼动画、混合树、动画控制器
│   ├── network/         # HTTP, WebSocket, TLS, 跨平台 IO
│   ├── assets/          # 网格、材质、导入契约
│   ├── resource/        # 异步资源加载、生命周期、资源图
│   ├── shader/          # 着色器编译器接口 + 反射验证
│   ├── pcg/             # 程序化生成 (33 个模块)
│   ├── scene/           # 场景图、相机、游戏对象
│   ├── engine/          # 引擎 DLL 接口
│   ├── platform/        # RenderSystem, 窗口, 生命周期 (Win/Android/iOS/macOS)
│   ├── ui/              # 自研保留模式 GUI
│   ├── gui/             # ImGui 集成 (工具用)
│   ├── input/           # 跨平台输入抽象
│   ├── reflection/      # 编译时反射和代码生成
│   ├── 2d/              # 2D 场景原语
│   └── 3d/              # 3D 场景原语
├── games/
│   └── world_of_claudecraft/  # WoCC 演示游戏
│       ├── assets/shaders/    # 游戏 Slang 着色器 (14 个文件)
│       ├── app/               # 游戏应用
│       ├── gameplay/          # 游戏逻辑
│       ├── content/           # 程序化内容配置
│       ├── presentation/      # UI / 界面流程
│       ├── tools/             # 编辑器工具
│       └── tests/             # 游戏专用测试
├── tools/               # 引擎工具
│   ├── ai_asset_generator/   # Meshy AI 3D 资产生成
│   ├── asset_importer/       # 资产管线导入
│   ├── cluster_previewer/    # 集群资产预览
│   ├── platform_capabilities/ # 平台能力报告
│   ├── reflect_codegen/      # 反射代码生成器
│   ├── unity_import/         # Unity 资产转换管线
│   └── wocc_asset_importer/  # WoCC 资产导入
├── shaders/             # 核心 Slang 着色器源码
├── tests/
│   ├── contract/        # RHI 契约验证测试
│   ├── smoke/           # 隐藏窗口烟雾测试
│   ├── unit/            # 单元测试
│   └── visual/          # 视觉回归测试
├── docs/                # 架构文档 (90+ 文档)
├── 3rd/                 # 第三方库
│   ├── SDL3-3.4.8/      # 平台抽象层
│   ├── JoltPhysics-5.6.0/ # 物理引擎
│   ├── assimp-6.0.5/    # 资产导入
│   ├── mimalloc/        # 内存分配器
│   ├── freetype/        # 字体渲染
│   ├── fkYAML/          # YAML 解析
│   ├── stb/             # 单文件工具库
│   └── VulkanMemoryAllocator/ # GPU 内存分配器
├── CMakeLists.txt       # 构建配置
```

---

## 构建

### 前置依赖

- **编译器**：MinGW-w64（支持 C++17 的 g++）
- **构建系统**：Ninja（推荐）或 GNU Make
- **图形 SDK**：Vulkan SDK 1.3+（含 `slangc`）
- **库**：SDL3（位于 `3rd/` 目录）
- **Python**：3.x（用于构建脚本和代码生成）

### 快速开始（Windows + MinGW）

```powershell
# 构建全部（着色器、DLL 和可执行文件）
.\scripts\build.py

# 运行 World of Claudecraft 演示游戏
.\artifacts\direct\arp_world_of_claudecraft.exe
```

### CMake Preset 构建

```powershell
cmake --preset ninja-mingw-debug
cmake --build --preset ninja-mingw-debug
ctest --preset ninja-mingw-debug
```

### 着色器编译

```powershell
.\scripts\compile_shaders.py
```

输出 SPIR-V (`.spv`)、Metal (`.metal`)、WebGPU (`.wgsl`) + reflection JSON 到 `artifacts/direct/shaders/`。

### 平台特定构建

| 目标 | 命令 |
|------|------|
| Windows Vulkan | `cmake --preset ninja-mingw-debug` |
| macOS Metal | `cmake --preset macos-metal` |
| iOS Metal | `cmake --preset ios-metal` |
| Android Vulkan | `cmake --preset android-arm64-vulkan` |
| Web WebGPU | `scripts/build_character_viewer_web.ps1` (Emscripten) |
| Linux Vulkan | `cmake --preset linux-vulkan` |

详细 SDK 要求请参考 [Platform Build Matrix](docs/platform-build-matrix.md)。

---

## 运行

### World of Claudecraft（演示游戏）

项目只附带一个演示游戏：**World of Claudecraft（WoCC）**——风格化开放世界体验。

```powershell
.\artifacts\direct\arp_world_of_claudecraft.exe
```

打开 Vulkan 窗口，包含高度场地形、程序化放置的植被和道具、主角角色、昼夜光照、级联阴影贴图、DDGI 全局光照、后处理（FXAA/SSAO/Bloom/色调映射）、粒子系统和保留模式 GUI 状态面板。

### 烟雾测试

CI 可用的无头验证：

```powershell
.\build\win-auto-debug\tests\smoke\arp_xxxx_smoke.exe
```

---

## 文档

`docs/` 目录包含 90+ 份文档：

| 类别 | 文档 |
|------|------|
| 架构 | RHI 治理、渲染器数据模型、模块治理、迁移边界 |
| 路线图 | 项目路线图、移动优先跨平台、FrameGraph v2、GI 路线图 |
| 能力矩阵 | [RHI Capability Matrix](RHI_CAPABILITY_MATRIX.md)、平台构建矩阵 |
| 渲染 | GI (DDGI)、光照与阴影、雾、辉光、泛光、后处理 |
| 移动端 | 移动渲染基线、质量档位、Android/iOS 生命周期契约 |
| 程序化 | 地形、侵蚀、植被、生物群落生成方案 |

关键架构文档：
- [RHI Layer Governance](docs/rhi-layer-governance.md) — 4 层治理模型
- [RHI Capability Matrix](RHI_CAPABILITY_MATRIX.md) — 各后端降级决策
- [Platform Build Matrix](docs/platform-build-matrix.md) — 支持的构建目标
- [FrameGraph v2 Roadmap](docs/framegraph-v2-roadmap.md) — RenderGraph 演进

---

## 状态定义

| 状态 | 含义 |
|------|------|
| **Contract Complete** | 接口和测试描述了预期行为 |
| **Prototype Complete** | 功能在至少一个示例路径中运行 |
| **Backend Validated** | 在真实图形后端上实现并测试 |
| **Platform Validated** | 在指定的硬件平台上通过验证 |
| **Production Ready** | 满足发布所需的运行时、内存和生命周期要求 |

---

## 许可

MIT License。详见 [LICENSE](LICENSE)。

---

## 致谢

- [Slang](https://shader-slang.com/) — 跨平台着色器编译
- [SDL3](https://libsdl.org/) — 平台抽象层
- [Vulkan SDK](https://www.lunarg.com/vulkan-sdk/) — 图形 API
- [Assimp](https://github.com/assimp/assimp) — 资产导入
- [Jolt Physics](https://github.com/jrouwe/JoltPhysics) — 物理仿真
- [mimalloc](https://github.com/microsoft/mimalloc) — 内存分配器

---

<p align="center"><a href="#zh">回到顶部 ↑</a></p>

---

<a id="ja"></a>

# arp-engine

> AI Render Pipeline — 契約駆動、クロスプラットフォーム、モバイルファーストの 3D レンダリングライブラリ。

<p align="center">
  <a href="#en"><b>English</b></a> •
  <a href="#zh"><b>中文</b></a> •
  <a href="#ja"><b>日本語</b></a> •
  <a href="#de"><b>Deutsch</b></a> •
  <a href="#ru"><b>Русский</b></a>
</p>

---

---

arp-engine は、スタイライズド 3D レンダリング向けの契約駆動型クロスプラットフォーム・レンダリングライブラリです。レンダリングの決定は、バージョン管理可能で検証可能な契約を通じて表現されます。

**プロジェクトステータス**: Vulkan 上で Contract Complete / Prototype Complete。モバイルファーストのクロスプラットフォーム移行を進行中。

---

## 設計哲学

### データ駆動

レンダリングとリソースの動作は、明示的でバージョン管理されたデータと契約で定義されます。

- **シェーダーグラフもビジュアルエディターも不要** — すべてのレンダリング決定は構造化されたバージョン管理可能なデータ（C++ 契約、YAML 設定、JSON リフレクション、SPIR-V デコレーションアサーション）から得られます
- **利便性よりも決定論** — すべてのフォールバックパス、機能ゲート、低減決定は実行前に宣言されます。「異なる動作で静かに成功する」ことはありません
- **契約ファースト** — インターフェースとテストは実装*前*に期待される動作を記述します。バックエンドコードは承認された低減決定のみを実行します

### 契約駆動の RHI

レンダリングハードウェアインターフェースは厳格な 4 層ガバナンスモデルに従います：

```
platform → renderer → rhi::semantics → rhi backend ← backend
```

1. L2 ドメインセマンティクスの**定義**（デスクリプタ、リソース状態、コマンド、同期）
2. バックエンドごとの低減の**契約化**（Native / Equivalent / Degraded / DiagnosticFailure）
3. バックエンドプライベートパスの**実装**
4. RenderGraph とレンダラーを介した**消費** — 上位層は L2 インテントのみを表現し、ネイティブハンドルを保持しない

### 4 つのバックエンド、1 つの契約

Null、Vulkan、Metal、WebGPU は同じ L2 契約の 4 つの必須実装です。機能は 4 つすべてのバックエンドが明示的な回答を持つまで「完了」しません。

| バックエンド | ステータス | ターゲット |
|-------------|-----------|-----------|
| Null | リファレンス検証 | 契約テスト、CI |
| Vulkan | プライマリ、バックエンド検証済み | Windows (P0), Android (P0) |
| Metal | 契約完了、プラットフォーム未検証 | macOS (P1), iOS (P1) |
| WebGPU | プロトタイプ、実験的 | Web (P2) |

詳細は [RHI Capability Matrix](RHI_CAPABILITY_MATRIX.md) を参照。

---

## 主要機能

### レンダリング

- **ディファード + フォワードレンダリング** — セルフオーサードの FrameGraph / RenderGraph による
- **DDGI**（動的拡散グローバルイルミネーション）— プローブグリッドベース、GPU コンピュート駆動
- **高度なシャドウ** — CSM カスケードシャドウマップ、PCF フィルタリング、シャドウアトラス、コンタクトシャドウ
- **ポストプロセスチェーン** — FXAA、SSAO、ブルーム、LUT トーンマッピング
- **パーティクルシステム** — パーティクル、メッシュパーティクル、トレイル/ビームエフェクト
- **プロシージャル生成** — 地形、侵食、河川、植生、バイオーム、天候、道路、集落、派閥、プロシージャルテクスチャのための 33+ エンジンレベルモジュール
- **非同期コンピュート** — セマフォベースの同期によるコンピュート-グラフィックスオーバーラップ
- **サブパス / フレームバッファフェッチ** — Vulkan でネイティブ、Metal で probe によりネイティブ化、WebGPU でエミュレート

### クロスプラットフォームサポート

| プラットフォーム | API | 優先度 | ステータス |
|----------------|-----|--------|-----------|
| Windows | Vulkan | P0 | ネイティブ、バックエンド検証済み |
| Android | Vulkan | P0 | 契約完了、移行中 |
| macOS | Metal | P1 | 契約完了、プラットフォーム未検証 |
| iOS | Metal | P1 | 契約完了、プラットフォーム未検証 |
| Linux | Vulkan | P2 | ビルド設定済み |
| Web | WebGPU | P2 | プロトタイプ、実験的 |

### アーキテクチャ

- **FrameGraph / RenderGraph** — コンパイル済み DAG ベースのパス合成、推移的カリング、一時リソースエイリアシング、コンパイル時バリア計画
- **セルフオーサード保持モード GUI**（`src/ui/`）— 外部 GUI 依存関係ゼロ（ImGui なし）
- **物理**（`src/physics/`）— Jolt Physics 統合、バックエンド抽象化
- **アニメーション**（`src/animation/`）— スケルタルアニメーション、ブレンドツリー、アニメーターコントローラー、IK（逆運動学）、フット IK、ポーズブレンディング、CPU スキニング
- **ネットワーキング**（`src/network/`）— HTTP、WebSocket、TLS、クロスプラットフォーム IO サービス
- **2D / 3D モジュール** — シーン表現、バウンド、コライダー、コンポーネント
- **リソースシステム**（`src/resource/`）— 非同期読み込み、リソースグラフ、ライフサイクル、リロード
- **入力**（`src/input/`）— クロスプラットフォーム入力抽象化
- **リフレクション / コード生成**（`src/reflection/`）— コンパイル時リフレクションとコード生成
- **Slang 唯一のシェーダーパイプライン** — 単一ソースから SPIR-V、MSL、WGSL を生成
- **リフレクション駆動検証** — バインディングレイアウト、プッシュ定数サイズ、SPIR-V デコレーションの正確性を自動検証
- **制限付き割り当て** — すべてのランタイムメモリは `core::pmr` アロケータを通じて割り当てられ、ホットパスに暗黙の STL 割り当てなし
- **モバイル品質認識** — 4 段階の品質プロファイル（低 / 中 / 高 / デスクトップ）、明示的な能力低下付き

---

## プロジェクト構成

```
arp-engine/
├── src/
│   ├── core/            # 基盤：数学、アロケータ、ファイル I/O、ロギング、非同期ランタイム
│   ├── rhi/
│   │   ├── backends/    # バックエンド実装 (null, vulkan, metal, webgpu)
│   │   └── semantics/   # 契約低減、フォールバック計画、能力クエリ
│   ├── renderer/
│   │   ├── render_graph/# FrameGraph / RenderGraph コンパイラと実行器
│   │   ├── postprocess/ # FXAA, SSAO, bloom
│   │   ├── particles/   # パーティクル/VFX システム
│   │   ├── pipeline/    # レンダーパイプラインオーケストレーション
│   │   └── ...          # シーン, GUI, マテリアル, シャドウ, GI
│   ├── physics/         # 物理モジュール (Jolt バックエンド)
│   ├── animation/       # スケルタルアニメーション、ブレンドツリー、アニメーターコントローラー
│   ├── network/         # HTTP, WebSocket, TLS, クロスプラットフォーム IO
│   ├── assets/          # メッシュ、マテリアル、インポート契約
│   ├── resource/        # 非同期リソース読み込み、ライフサイクル、リソースグラフ
│   ├── shader/          # シェーダーコンパイラインターフェース + リフレクション検証
│   ├── pcg/             # プロシージャル生成 (33 モジュール)
│   ├── scene/           # シーングラフ、カメラ、ゲームオブジェクト
│   ├── engine/          # エンジン DLL インターフェース
│   ├── platform/        # RenderSystem, ウィンドウ, ライフサイクル (Win/Android/iOS/macOS)
│   ├── ui/              # セルフオーサード保持モード GUI
│   ├── gui/             # ImGui 統合 (ツール用)
│   ├── input/           # クロスプラットフォーム入力抽象化
│   ├── reflection/      # コンパイル時リフレクションとコード生成
│   ├── 2d/              # 2D シーン基本要素
│   └── 3d/              # 3D シーン基本要素
├── games/
│   └── world_of_claudecraft/  # WoCC デモゲーム
│       ├── assets/shaders/    # ゲーム Slang シェーダー (14 ファイル)
│       ├── app/               # ゲームアプリケーション
│       ├── gameplay/          # ゲームプレイロジック
│       ├── content/           # プロシージャルコンテンツ設定
│       ├── presentation/      # UI / 画面フロー
│       ├── tools/             # エディターツール
│       └── tests/             # ゲーム固有テスト
├── tools/               # エンジンツール
│   ├── ai_asset_generator/   # Meshy AI 3D アセット生成
│   ├── asset_importer/       # アセットパイプラインインポーター
│   ├── cluster_previewer/    # クラスターアセットプレビュー
│   ├── platform_capabilities/ # プラットフォーム能力レポート
│   ├── reflect_codegen/      # リフレクションコードジェネレーター
│   ├── unity_import/         # Unity アセット変換パイプライン
│   └── wocc_asset_importer/  # WoCC アセットインポーター
├── shaders/             # コア Slang シェーダーソース
├── tests/
│   ├── contract/        # RHI 契約検証テスト
│   ├── smoke/           # 隠しウィンドウスモークテスト
│   ├── unit/            # ユニットテスト
│   └── visual/          # ビジュアルリグレッションテスト
├── docs/                # アーキテクチャドキュメント (90+ 文書)
├── 3rd/                 # サードパーティライブラリ
│   ├── SDL3-3.4.8/      # プラットフォーム抽象化層
│   ├── JoltPhysics-5.6.0/ # 物理エンジン
│   ├── assimp-6.0.5/    # アセットインポート
│   ├── mimalloc/        # メモリアロケータ
│   ├── freetype/        # フォントレンダリング
│   ├── fkYAML/          # YAML パーサー
│   ├── stb/             # 単一ファイルユーティリティ
│   └── VulkanMemoryAllocator/ # GPU メモリアロケータ
├── CMakeLists.txt       # ビルド設定
```

---

## ビルド

### 前提条件

- **コンパイラ**: MinGW-w64（C++17 対応 g++）
- **ビルドシステム**: Ninja（推奨）または GNU Make
- **グラフィックス SDK**: Vulkan SDK 1.3+（`slangc` 付属）
- **ライブラリ**: SDL3（`3rd/` にバンドル）
- **Python**: 3.x（ビルドスクリプトとコード生成用）

### クイックスタート（Windows + MinGW）

```powershell
# 全てをビルド（シェーダー、DLL、実行ファイル）
.\scripts\build.py

# World of Claudecraft デモゲームを実行
.\artifacts\direct\arp_world_of_claudecraft.exe
```

### CMake プリセットビルド

```powershell
cmake --preset ninja-mingw-debug
cmake --build --preset ninja-mingw-debug
ctest --preset ninja-mingw-debug
```

### シェーダーコンパイル

```powershell
.\scripts\compile_shaders.py
```

SPIR-V (`.spv`)、Metal (`.metal`)、WebGPU (`.wgsl`) + リフレクション JSON を `artifacts/direct/shaders/` に出力。

### プラットフォーム固有ビルド

| ターゲット | コマンド |
|-----------|---------|
| Windows Vulkan | `cmake --preset ninja-mingw-debug` |
| macOS Metal | `cmake --preset macos-metal` |
| iOS Metal | `cmake --preset ios-metal` |
| Android Vulkan | `cmake --preset android-arm64-vulkan` |
| Web WebGPU | `scripts/build_character_viewer_web.ps1` (Emscripten) |
| Linux Vulkan | `cmake --preset linux-vulkan` |

詳細な SDK 要件は [Platform Build Matrix](docs/platform-build-matrix.md) を参照。

---

## 実行

### World of Claudecraft（デモゲーム）

プロジェクトには 1 つのデモゲームが付属しています：**World of Claudecraft（WoCC）** — スタイライズドオープンワールド体験。

```powershell
.\artifacts\direct\arp_world_of_claudecraft.exe
```

Vulkan ウィンドウを開き、高さマップ地形、プロシージャルに配置された植生とプロップ、ヒーローキャラクター、時間帯ライティング、カスケードシャドウマップ、DDGI グローバルイルミネーション、ポストプロセス（FXAA/SSAO/ブルーム/トーンマッピング）、パーティクルシステム、保持モード GUI ステータスパネルを表示。

### スモークテスト

CI 用のヘッドレス検証：

```powershell
.\build\win-auto-debug\tests\smoke\arp_xxxx_smoke.exe
```

---

## ドキュメント

`docs/` ディレクトリには 90+ の文書があります：

| カテゴリ | 文書 |
|----------|------|
| アーキテクチャ | RHI ガバナンス、レンダラーデータモデル、モジュールガバナンス、移行境界 |
| ロードマップ | プロジェクトロードマップ、モバイルファーストクロスプラットフォーム、FrameGraph v2、GI ロードマップ |
| 能力 | [RHI Capability Matrix](RHI_CAPABILITY_MATRIX.md)、プラットフォームビルドマトリックス |
| レンダリング | GI (DDGI)、ライティング＆シャドウ、フォグ、ゴッドレイ、ブルーム、ポストプロセス |
| モバイル | モバイルレンダリングベースライン、品質プロファイル、Android/iOS ライフサイクル契約 |
| プロシージャル | 地形、侵食、植生、バイオーム生成計画 |

主要アーキテクチャ文書：
- [RHI Layer Governance](docs/rhi-layer-governance.md) — 4 層モデル
- [RHI Capability Matrix](RHI_CAPABILITY_MATRIX.md) — バックエンド別低減決定
- [Platform Build Matrix](docs/platform-build-matrix.md) — 対応ビルドターゲット
- [FrameGraph v2 Roadmap](docs/framegraph-v2-roadmap.md) — RenderGraph の進化

---

## ステータス用語

| ステータス | 意味 |
|-----------|------|
| **Contract Complete** | インターフェースとテストが期待される動作を記述 |
| **Prototype Complete** | 機能が少なくとも 1 つのサンプルパスで動作 |
| **Backend Validated** | 実際のグラフィックスバックエンドで実装・テスト済み |
| **Platform Validated** | 指定されたハードウェアプラットフォームで合格 |
| **Production Ready** | 出荷に必要なランタイム、メモリ、ライフサイクル要件を満たす |

---

## ライセンス

MIT License。詳細は [LICENSE](LICENSE) を参照。

---

## 謝辞

- [Slang](https://shader-slang.com/) — クロスプラットフォームシェーダーコンパイル
- [SDL3](https://libsdl.org/) — プラットフォーム抽象化層
- [Vulkan SDK](https://www.lunarg.com/vulkan-sdk/) — グラフィックス API
- [Assimp](https://github.com/assimp/assimp) — アセットインポート
- [Jolt Physics](https://github.com/jrouwe/JoltPhysics) — 物理シミュレーション
- [mimalloc](https://github.com/microsoft/mimalloc) — メモリアロケータ

---

<p align="center"><a href="#ja">トップに戻る ↑</a></p>

---

<a id="de"></a>

# arp-engine

> AI Render Pipeline — Eine vertragsgesteuerte, plattformübergreifende, mobile-first 3D-Rendering-Bibliothek.

<p align="center">
  <a href="#en"><b>English</b></a> •
  <a href="#zh"><b>中文</b></a> •
  <a href="#ja"><b>日本語</b></a> •
  <a href="#de"><b>Deutsch</b></a> •
  <a href="#ru"><b>Русский</b></a>
</p>

---

---

arp-engine ist eine vertragsgesteuerte, plattformübergreifende Rendering-Bibliothek für stilisiertes 3D-Rendering. Rendering-Entscheidungen werden durch versionierte, überprüfbare Verträge ausgedrückt.

**Projektstatus**: Contract Complete / Prototype Complete auf Vulkan; aktive Mobile-First Cross-Plattform-Migration.

---

## Design-Philosophie

### KI-zuerst, datengetrieben

Rendering- und Ressourcenverhalten wird durch explizite, versionierte Daten und Verträge definiert.

- **Kein Shader-Graph, keine visuellen Editoren** — alle Rendering-Entscheidungen stammen aus strukturierten, versionierten Daten (C++-Verträge, YAML-Konfigurationen, JSON-Reflection, SPIR-V-Dekorations-Assertions)
- **Deterministisch vor bequem** — jeder Fallback-Pfad, jedes Fähigkeitstor und jede Herabstufungsentscheidung wird vor der Ausführung deklariert. Es gibt kein "stilles Erfolgen mit anderem Verhalten"
- **Vertrag zuerst** — Schnittstellen und Tests beschreiben das erwartete Verhalten *vor* der Implementierung. Backend-Code führt nur genehmigte Herabstufungsentscheidungen aus

### Vertragsgesteuertes RHI

Die Rendering-Hardware-Schnittstelle folgt einem strengen 4-Schichten-Governance-Modell:

```
platform → renderer → rhi::semantics → rhi backend ← backend
```

1. **Definition** der L2-Domänensemantik (Deskriptoren, Ressourcenzustände, Befehle, Synchronisation)
2. **Vertrag** der Herabstufung pro Backend (Native / Equivalent / Degraded / DiagnosticFailure)
3. **Implementierung** Backend-privater Pfade
4. **Konsum** über RenderGraph und Renderer — obere Schichten drücken nur L2-Intention aus, halten niemals native Handles

### Vier Backends, ein Vertrag

Null, Vulkan, Metal und WebGPU sind vier obligatorische Implementierungen desselben L2-Vertrags. Ein Feature ist erst "fertig", wenn alle vier Backends eine explizite Antwort haben.

| Backend | Status | Ziel |
|---------|--------|------|
| Null | Referenzvalidierung | Vertragstests, CI |
| Vulkan | Primär, Backend-validiert | Windows (P0), Android (P0) |
| Metal | Vertrag vollständig, Plattform nicht validiert | macOS (P1), iOS (P1) |
| WebGPU | Prototyp, experimentell | Web (P2) |

Siehe [RHI Capability Matrix](RHI_CAPABILITY_MATRIX.md) für die Herabstufungsentscheidungen pro Backend.

---

## Hauptmerkmale

### Rendering

- **Deferred + Forward-Rendering** über selbstentwickelten FrameGraph / RenderGraph
- **DDGI** (Dynamic Diffuse Global Illumination) — Sonden-gitterbasiertes, GPU-Compute-getriebenes GI
- **Fortschrittliche Schatten** — CSM-Kaskadenschattenkarten, PCF-Filterung, Schattenatlas, Kontaktschatten
- **Post-Processing-Kette** — FXAA, SSAO, Bloom, LUT-Tonemapping
- **Partikelsystem** — Partikel, Mesh-Partikel, Trail/Beam-Effekte
- **Prozedurale Generierung** — 33+ Engine-Module für Gelände, Erosion, Flüsse, Vegetation, Biome, Wetter, Straßen, Siedlungen, Fraktionen und prozedurale Texturen
- **Asynchrones Compute** — Compute-Graphics-Überlappung mit Semaphor-basierter Synchronisation
- **Subpass / Framebuffer-Fetch** — nativ auf Vulkan, probe-gesteuert nativ auf Metal, emuliert auf WebGPU

### Plattformunterstützung

| Plattform | API | Priorität | Status |
|-----------|-----|-----------|--------|
| Windows | Vulkan | P0 | Nativ, Backend-validiert |
| Android | Vulkan | P0 | Vertrag vollständig, aktive Migration |
| macOS | Metal | P1 | Vertrag vollständig, Plattform nicht validiert |
| iOS | Metal | P1 | Vertrag vollständig, Plattform nicht validiert |
| Linux | Vulkan | P2 | Build konfiguriert |
| Web | WebGPU | P2 | Prototyp, experimentell |

### Architektur

- **FrameGraph / RenderGraph** — kompilierte DAG-basierte Pass-Komposition mit transitivem Culling, temporärem Ressourcen-Aliasing und Compile-Zeit-Barriere-Planung
- **Selbsterstellte retained-mode GUI** (`src/ui/`) — keine externen GUI-Abhängigkeiten (kein ImGui)
- **Physik** (`src/physics/`) — Jolt Physics-Integration mit Backend-Abstraktion
- **Animation** (`src/animation/`) — Skelettanimation, Blend-Trees, Animator-Controller, IK (inverse Kinematik), Foot-IK, Pose-Blending, CPU-Skinning
- **Netzwerk** (`src/network/`) — HTTP, WebSocket, TLS, plattformübergreifender IO-Dienst
- **2D / 3D-Module** — Szenendarstellung, Bounds, Kollider, Komponenten
- **Ressourcensystem** (`src/resource/`) — asynchrones Laden, Ressourcengraph, Lebenszyklus, Neuladen
- **Eingabe** (`src/input/`) — plattformübergreifende Eingabeabstraktion
- **Reflection / Codegenerierung** (`src/reflection/`) — Compile-Zeit-Reflection und Codegenerierung
- **Slang-Only-Shader-Pipeline** — einzelne Quelle erzeugt SPIR-V, MSL und WGSL
- **Reflection-getriebene Validierung** — automatische Shader-Reflection-Vertragstests für Binding-Layout, Push-Konstanten-Größe und SPIR-V-Dekorationskorrektheit
- **Begrenzte Allokation** — gesamter Laufzeitspeicher über `core::pmr`-Allokatoren; keine versteckten STL-Allokationen auf heißen Pfaden
- **Mobile-Qualitätsbewusstsein** — 4-stufige Qualitätsprofile (Niedrig / Mittel / Hoch / Desktop) mit expliziter Fähigkeitsherabstufung

---

## Projektstruktur

```
arp-engine/
├── src/
│   ├── core/            # Grundlagen: Mathematik, Allokatoren, Datei-I/O, Logging, asynchrone Laufzeit
│   ├── rhi/
│   │   ├── backends/    # Backend-Implementierungen (null, vulkan, metal, webgpu)
│   │   └── semantics/   # Vertragsherabstufung, Fallback-Pläne, Fähigkeitsabfragen
│   ├── renderer/
│   │   ├── render_graph/# FrameGraph / RenderGraph Compiler und Ausführer
│   │   ├── postprocess/ # FXAA, SSAO, bloom
│   │   ├── particles/   # Partikel/VFX-System
│   │   ├── pipeline/    # Render-Pipeline-Orchestrierung
│   │   └── ...          # Szene, GUI, Materialien, Schatten, GI
│   ├── physics/         # Physikmodul (Jolt-Backend)
│   ├── animation/       # Skelettanimation, Blend-Trees, Animator-Controller
│   ├── network/         # HTTP, WebSocket, TLS, plattformübergreifendes IO
│   ├── assets/          # Mesh, Material, Import-Verträge
│   ├── resource/        # Asynchrones Ressourcenladen, Lebenszyklus, Ressourcengraph
│   ├── shader/          # Shader-Compiler-Schnittstelle + Reflexionsvalidierung
│   ├── pcg/             # Prozedurale Generierung (33 Module)
│   ├── scene/           # Szenengraph, Kamera, Spielobjekt
│   ├── engine/          # Engine-DLL-Schnittstelle
│   ├── platform/        # RenderSystem, Fenster, Lebenszyklus (Win/Android/iOS/macOS)
│   ├── ui/              # Selbsterstellte retained-mode GUI
│   ├── gui/             # ImGui-Integration (für Werkzeuge)
│   ├── input/           # Plattformübergreifende Eingabeabstraktion
│   ├── reflection/      # Compile-Zeit-Reflexion und Codegenerierung
│   ├── 2d/              # 2D-Szenenprimitive
│   └── 3d/              # 3D-Szenenprimitive
├── games/
│   └── world_of_claudecraft/  # WoCC-Demospiel
│       ├── assets/shaders/    # Spiel-Slang-Shader (14 Dateien)
│       ├── app/               # Spielanwendung
│       ├── gameplay/          # Spiel-Logik
│       ├── content/           # Prozedurale Inhaltskonfigurationen
│       ├── presentation/      # UI / Bildschirmfluss
│       ├── tools/             # Editor-Werkzeuge
│       └── tests/             # Spielspezifische Tests
├── tools/               # Engine-Werkzeuge
│   ├── ai_asset_generator/   # Meshy KI-3D-Asset-Generierung
│   ├── asset_importer/       # Asset-Pipeline-Import
│   ├── cluster_previewer/    # Cluster-Asset-Vorschau
│   ├── platform_capabilities/ # Plattform-Fähigkeitsbericht
│   ├── reflect_codegen/      # Reflexions-Codegenerator
│   ├── unity_import/         # Unity-Asset-Konvertierungspipeline
│   └── wocc_asset_importer/  # WoCC-Asset-Import
├── shaders/             # Kern-Slang-Shader-Quellen
├── tests/
│   ├── contract/        # RHI-Vertragsvalidierungstests
│   ├── smoke/           # Versteckte-Fenster-Rauchtests
│   ├── unit/            # Unit-Tests
│   └── visual/          # Visuelle Regressionstests
├── docs/                # Architekturdokumentation (90+ Dokumente)
├── 3rd/                 # Drittanbieter-Bibliotheken
│   ├── SDL3-3.4.8/      # Plattformabstraktionsschicht
│   ├── JoltPhysics-5.6.0/ # Physik-Engine
│   ├── assimp-6.0.5/    # Asset-Import
│   ├── mimalloc/        # Speicher-Allokator
│   ├── freetype/        # Schriftart-Rendering
│   ├── fkYAML/          # YAML-Parser
│   ├── stb/             # Einzeldatei-Dienstprogramme
│   └── VulkanMemoryAllocator/ # GPU-Speicher-Allokator
├── CMakeLists.txt       # Build-Konfiguration
```

---

## Build

### Voraussetzungen

- **Compiler**: MinGW-w64 (g++ mit C++17-Unterstützung)
- **Build-System**: Ninja (empfohlen) oder GNU Make
- **Grafik-SDK**: Vulkan SDK 1.3+ (mit `slangc`)
- **Bibliotheken**: SDL3 (in `3rd/` gebündelt)
- **Python**: 3.x (für Build-Skripte und Codegenerierung)

### Kurzstart (Windows + MinGW)

```powershell
# Alles bauen (Shader, DLLs und ausführbare Dateien)
.\scripts\build.py

# Das World of Claudecraft-Demospiel ausführen
.\artifacts\direct\arp_world_of_claudecraft.exe
```

### CMake-Preset-Build

```powershell
cmake --preset ninja-mingw-debug
cmake --build --preset ninja-mingw-debug
ctest --preset ninja-mingw-debug
```

### Shader-Kompilierung

```powershell
.\scripts\compile_shaders.py
```

Ausgabe: SPIR-V (`.spv`), Metal (`.metal`), WebGPU (`.wgsl`) + Reflection-JSON nach `artifacts/direct/shaders/`.

### Plattformspezifische Builds

| Ziel | Befehl |
|------|--------|
| Windows Vulkan | `cmake --preset ninja-mingw-debug` |
| macOS Metal | `cmake --preset macos-metal` |
| iOS Metal | `cmake --preset ios-metal` |
| Android Vulkan | `cmake --preset android-arm64-vulkan` |
| Web WebGPU | `scripts/build_character_viewer_web.ps1` (Emscripten) |
| Linux Vulkan | `cmake --preset linux-vulkan` |

Detailierte SDK-Anforderungen finden Sie in der [Platform Build Matrix](docs/platform-build-matrix.md).

---

## Ausführung

### World of Claudecraft (Demospiel)

Das Projekt enthält ein Demospiel: **World of Claudecraft (WoCC)** — ein stilisiertes Open-World-Erlebnis.

```powershell
.\artifacts\direct\arp_world_of_claudecraft.exe
```

Öffnet ein Vulkan-Fenster mit Höhenfeldgelände, prozedural platzierten Pflanzen und Requisiten, einem Heldencharakter, Tageszeit-Beleuchtung, kaskadierten Schattenkarten, DDGI-Globalbeleuchtung, Post-Processing (FXAA/SSAO/Bloom/Tonemapping), einem Partikelsystem und einer retained-mode GUI-Statusleiste.

### Rauchtests

Kopflose Validierung für CI:

```powershell
.\build\win-auto-debug\tests\smoke\arp_xxxx_smoke.exe
```

---

## Dokumentation

Das `docs/`-Verzeichnis enthält 90+ Dokumente:

| Kategorie | Dokumente |
|-----------|-----------|
| Architektur | RHI-Governance, Renderer-Datenmodell, Modul-Governance, Migrationsgrenze |
| Roadmap | Projekt-Roadmap, Mobile-First-Cross-Plattform, FrameGraph v2, GI-Roadmap |
| Fähigkeiten | [RHI Capability Matrix](RHI_CAPABILITY_MATRIX.md), Plattform-Build-Matrix |
| Rendering | GI (DDGI), Beleuchtung & Schatten, Nebel, God Rays, Bloom, Post-Processing |
| Mobil | Mobile-Rendering-Baseline, Qualitätsprofile, Android/iOS-Lebenszyklus-Verträge |
| Prozedural | Gelände-, Erosions-, Vegetations-, Biomerzeugungspläne |

Wichtige Architekturdokumente:
- [RHI Layer Governance](docs/rhi-layer-governance.md) — Das 4-Schichten-Modell
- [RHI Capability Matrix](RHI_CAPABILITY_MATRIX.md) — Herabstufungsentscheidungen pro Backend
- [Platform Build Matrix](docs/platform-build-matrix.md) — Unterstützte Build-Ziele
- [FrameGraph v2 Roadmap](docs/framegraph-v2-roadmap.md) — RenderGraph-Evolution

---

## Status-Vokabular

| Status | Bedeutung |
|--------|-----------|
| **Contract Complete** | Schnittstellen und Tests beschreiben das erwartete Verhalten |
| **Prototype Complete** | Funktion läuft in mindestens einem Beispielpfad |
| **Backend Validated** | Auf einem echten Grafik-Backend implementiert und getestet |
| **Platform Validated** | Auf einer benannten Hardware-Plattform bestanden |
| **Production Ready** | Erfüllt die Laufzeit-, Speicher- und Lebenszyklusanforderungen für den Versand |

---

## Lizenz

MIT License. Siehe [LICENSE](LICENSE).

---

## Danksagungen

- [Slang](https://shader-slang.com/) — Plattformübergreifende Shader-Kompilierung
- [SDL3](https://libsdl.org/) — Plattformabstraktionsschicht
- [Vulkan SDK](https://www.lunarg.com/vulkan-sdk/) — Grafik-API
- [Assimp](https://github.com/assimp/assimp) — Asset-Import
- [Jolt Physics](https://github.com/jrouwe/JoltPhysics) — Physik-Simulation
- [mimalloc](https://github.com/microsoft/mimalloc) — Speicher-Allokator

---

<p align="center"><a href="#de">Zum Anfang ↑</a></p>

---

<a id="ru"></a>

# arp-engine

> AI Render Pipeline — Управляемая контрактами, кроссплатформенная, mobile-first библиотека 3D-рендеринга.

<p align="center">
  <a href="#en"><b>English</b></a> •
  <a href="#zh"><b>中文</b></a> •
  <a href="#ja"><b>日本語</b></a> •
  <a href="#de"><b>Deutsch</b></a> •
  <a href="#ru"><b>Русский</b></a>
</p>

---

---

arp-engine — это **собственная, управляемая контрактами библиотека рендеринга**, созданная с нуля для стилизованного 3D-рендеринга. Архитектура построена вокруг одной предпосылки: **основной потребитель данных движка — ИИ-агент, а не художник.** Каждое решение о рендеринге выражается через версионируемые, проверяемые контракты — а не через панели GUI или неявные пути отката.

**Статус проекта**: Contract Complete / Prototype Complete на Vulkan; активная mobile-first кроссплатформенная миграция.

---

## Философия дизайна

### ИИ в приоритете, управление данными

arp-engine создан для мира, где конвейеры рендеринга пишутся, проверяются и валидируются ИИ.

- **Нет шейдерных графов, нет визуальных редакторов** — все решения о рендеринге поступают из структурированных, версионируемых данных (C++ контракты, YAML конфигурации, JSON отражение, SPIR-V ассерты декораций)
- **Детерминированность важнее удобства** — каждый путь отката, шлюз возможностей и решение о понижении объявляются до выполнения. Нет "тихого успеха с другим поведением"
- **Сначала контракт** — интерфейсы и тесты описывают ожидаемое поведение *до* реализации. Код бэкенда выполняет только утверждённые решения о понижении

### Управляемый контрактами RHI

Интерфейс рендерингового оборудования следует строгой 4-уровневой модели управления:

```
platform → renderer → rhi::semantics → rhi backend ← backend
```

1. **Определение** семантики домена L2 (дескрипторы, состояния ресурсов, команды, синхронизация)
2. **Контракт** понижения для каждого бэкенда (Native / Equivalent / Degraded / DiagnosticFailure)
3. **Реализация** частных путей бэкенда
4. **Потребление** через RenderGraph и рендерер — верхние уровни выражают только намерения L2, никогда не содержат нативных дескрипторов

### Четыре бэкенда, один контракт

Null, Vulkan, Metal и WebGPU — четыре обязательных реализации одного контракта L2. Функция не считается "готовой", пока все четыре бэкенда не дадут явного ответа.

| Бэкенд | Статус | Цель |
|--------|--------|------|
| Null | Эталонная валидация | Контрактные тесты, CI |
| Vulkan | Основной, бэкенд-валидированный | Windows (P0), Android (P0) |
| Metal | Контракт завершён, платформа не валидирована | macOS (P1), iOS (P1) |
| WebGPU | Прототип, экспериментальный | Web (P2) |

Подробнее в [RHI Capability Matrix](RHI_CAPABILITY_MATRIX.md).

---

## Ключевые возможности

### Рендеринг

- **Отложенный + прямой рендеринг** через собственный FrameGraph / RenderGraph
- **DDGI** (Dynamic Diffuse Global Illumination) — глобальное освещение на основе сетки зондов, управляемое GPU-вычислениями
- **Продвинутые тени** — CSM каскадные карты теней, PCF фильтрация, атлас теней, контактные тени
- **Цепочка постобработки** — FXAA, SSAO, Bloom, LUT тональная коррекция
- **Система частиц** — частицы, меш-частицы, эффекты следов/лучей
- **Процедурная генерация** — 33+ модуля уровня движка для рельефа, эрозии, рек, растительности, биомов, погоды, дорог, поселений, фракций и процедурных текстур
- **Асинхронные вычисления** — перекрытие вычислений и графики с синхронизацией на семафорах
- **Subpass / framebuffer fetch** — нативно на Vulkan, нативно на Metal при прохождении probe, эмулировано на WebGPU

### Кроссплатформенная поддержка

| Платформа | API | Приоритет | Статус |
|-----------|-----|-----------|--------|
| Windows | Vulkan | P0 | Нативно, бэкенд-валидировано |
| Android | Vulkan | P0 | Контракт завершён, активная миграция |
| macOS | Metal | P1 | Контракт завершён, платформа не валидирована |
| iOS | Metal | P1 | Контракт завершён, платформа не валидирована |
| Linux | Vulkan | P2 | Сборка настроена |
| Web | WebGPU | P2 | Прототип, экспериментальный |

### Архитектура

- **FrameGraph / RenderGraph** — скомпилированная DAG-композиция проходов с транзитивным отсечением, временным алиасингом ресурсов и планированием барьеров на этапе компиляции
- **Собственная GUI с сохранением состояния** (`src/ui/`) — нулевая зависимость от внешних GUI (без ImGui)
- **Физика** (`src/physics/`) — интеграция Jolt Physics с абстракцией бэкенда
- **Анимация** (`src/animation/`) — скелетная анимация, деревья смешивания, контроллеры анимации, IK (обратная кинематика), Foot IK, смешивание поз, CPU-скиннинг
- **Сеть** (`src/network/`) — HTTP, WebSocket, TLS, кроссплатформенный IO-сервис
- **2D / 3D модули** — представление сцены, границы, коллайдеры, компоненты
- **Система ресурсов** (`src/resource/`) — асинхронная загрузка, граф ресурсов, жизненный цикл, перезагрузка
- **Ввод** (`src/input/`) — кроссплатформенная абстракция ввода
- **Отражение / генерация кода** (`src/reflection/`) — отражение на этапе компиляции и генерация кода
- **Конвейер шейдеров только на Slang** — единый источник генерирует SPIR-V, MSL и WGSL
- **Валидация на основе отражения** — автоматические контрактные тесты отражения шейдеров для привязки layout, размера push-констант и корректности декораций SPIR-V
- **Ограниченное выделение памяти** — вся память времени выполнения через аллокаторы `core::pmr`; без скрытых STL-аллокаций на горячих путях
- **Осведомлённость о мобильном качестве** — 4-уровневые профили качества (Низкий / Средний / Высокий / Десктоп) с явным понижением возможностей

---

## Структура проекта

```
arp-engine/
├── src/
│   ├── core/            # Основа: математика, аллокаторы, файловый ввод-вывод, логирование, асинхронная среда
│   ├── rhi/
│   │   ├── backends/    # Реализации бэкендов (null, vulkan, metal, webgpu)
│   │   └── semantics/   # Понижение контрактов, планы отката, запросы возможностей
│   ├── renderer/
│   │   ├── render_graph/# FrameGraph / RenderGraph компилятор и исполнитель
│   │   ├── postprocess/ # FXAA, SSAO, bloom
│   │   ├── particles/   # Система частиц/VFX
│   │   ├── pipeline/    # Оркестровка конвейера рендеринга
│   │   └── ...          # Сцена, GUI, материалы, тени, GI
│   ├── physics/         # Модуль физики (бэкенд Jolt)
│   ├── animation/       # Скелетная анимация, деревья смешивания, контроллеры анимации
│   ├── network/         # HTTP, WebSocket, TLS, кроссплатформенный IO
│   ├── assets/          # Контракты мешей, материалов, импорта
│   ├── resource/        # Асинхронная загрузка ресурсов, жизненный цикл, граф ресурсов
│   ├── shader/          # Интерфейс компилятора шейдеров + валидация отражения
│   ├── pcg/             # Процедурная генерация (33 модуля)
│   ├── scene/           # Граф сцены, камера, игровой объект
│   ├── engine/          # DLL-интерфейс движка
│   ├── platform/        # RenderSystem, окно, жизненный цикл (Win/Android/iOS/macOS)
│   ├── ui/              # Собственная GUI с сохранением состояния
│   ├── gui/             # Интеграция ImGui (для инструментов)
│   ├── input/           # Кроссплатформенная абстракция ввода
│   ├── reflection/      # Отражение на этапе компиляции и генерация кода
│   ├── 2d/              # Примитивы 2D-сцены
│   └── 3d/              # Примитивы 3D-сцены
├── games/
│   └── world_of_claudecraft/  # Демо-игра WoCC
│       ├── assets/shaders/    # Шейдеры игры на Slang (14 файлов)
│       ├── app/               # Игровое приложение
│       ├── gameplay/          # Игровая логика
│       ├── content/           # Конфигурации процедурного контента
│       ├── presentation/      # UI / поток экранов
│       ├── tools/             # Инструменты редактора
│       └── tests/             # Специфические тесты игры
├── tools/               # Инструменты движка
│   ├── ai_asset_generator/   # Генерация 3D-ассетов Meshy AI
│   ├── asset_importer/       # Импорт конвейера ассетов
│   ├── cluster_previewer/    # Предпросмотр кластерных ассетов
│   ├── platform_capabilities/ # Отчёт о возможностях платформы
│   ├── reflect_codegen/      # Генератор кода отражения
│   ├── unity_import/         # Конвейер конвертации ассетов Unity
│   └── wocc_asset_importer/  # Импорт ассетов WoCC
├── shaders/             # Исходные коды основных шейдеров Slang
├── tests/
│   ├── contract/        # Тесты валидации контрактов RHI
│   ├── smoke/           # Дымовые тесты со скрытым окном
│   ├── unit/            # Модульные тесты
│   └── visual/          # Визуальные регрессионные тесты
├── docs/                # Документация по архитектуре (90+ документов)
├── 3rd/                 # Сторонние библиотеки
│   ├── SDL3-3.4.8/      # Уровень абстракции платформы
│   ├── JoltPhysics-5.6.0/ # Физический движок
│   ├── assimp-6.0.5/    # Импорт ассетов
│   ├── mimalloc/        # Аллокатор памяти
│   ├── freetype/        # Рендеринг шрифтов
│   ├── fkYAML/          # Парсер YAML
│   ├── stb/             # Одномодульные утилиты
│   └── VulkanMemoryAllocator/ # Аллокатор памяти GPU
├── CMakeLists.txt       # Конфигурация сборки
```

---

## Сборка

### Предварительные требования

- **Компилятор**: MinGW-w64 (g++ с поддержкой C++17)
- **Система сборки**: Ninja (рекомендуется) или GNU Make
- **Графический SDK**: Vulkan SDK 1.3+ (с `slangc`)
- **Библиотеки**: SDL3 (в комплекте в `3rd/`)
- **Python**: 3.x (для скриптов сборки и генерации кода)

### Быстрый старт (Windows + MinGW)

```powershell
# Собрать всё (шейдеры, DLL и исполняемые файлы)
.\scripts\build.py

# Запустить демо-игру World of Claudecraft
.\artifacts\direct\arp_world_of_claudecraft.exe
```

### Сборка через CMake Preset

```powershell
cmake --preset ninja-mingw-debug
cmake --build --preset ninja-mingw-debug
ctest --preset ninja-mingw-debug
```

### Компиляция шейдеров

```powershell
.\scripts\compile_shaders.py
```

Вывод SPIR-V (`.spv`), Metal (`.metal`), WebGPU (`.wgsl`) + JSON отражения в `artifacts/direct/shaders/`.

### Сборка для конкретных платформ

| Цель | Команда |
|------|---------|
| Windows Vulkan | `cmake --preset ninja-mingw-debug` |
| macOS Metal | `cmake --preset macos-metal` |
| iOS Metal | `cmake --preset ios-metal` |
| Android Vulkan | `cmake --preset android-arm64-vulkan` |
| Web WebGPU | `scripts/build_character_viewer_web.ps1` (Emscripten) |
| Linux Vulkan | `cmake --preset linux-vulkan` |

Подробные требования к SDK см. в [Platform Build Matrix](docs/platform-build-matrix.md).

---

## Запуск

### World of Claudecraft (демо-игра)

Проект включает одну демо-игру: **World of Claudecraft (WoCC)** — стилизованный опыт открытого мира.

```powershell
.\artifacts\direct\arp_world_of_claudecraft.exe
```

Открывает окно Vulkan с ландшафтом на основе карты высот, процедурно размещённой растительностью и реквизитом, главным персонажем, освещением по времени суток, каскадными картами теней, глобальным освещением DDGI, постобработкой (FXAA/SSAO/Bloom/тональная коррекция), системой частиц и панелью состояния GUI с сохранением состояния.

### Дымовые тесты

Безголовая валидация для CI:

```powershell
.\build\win-auto-debug\tests\smoke\arp_xxxx_smoke.exe
```

---

## Документация

Директория `docs/` содержит 90+ документов:

| Категория | Документы |
|-----------|-----------|
| Архитектура | Управление RHI, модель данных рендерера, управление модулями, граница миграции |
| Дорожная карта | Дорожная карта проекта, mobile-first кроссплатформенность, FrameGraph v2, дорожная карта GI |
| Возможности | [RHI Capability Matrix](RHI_CAPABILITY_MATRIX.md), матрица сборки платформ |
| Рендеринг | GI (DDGI), освещение и тени, туман, лучи бога, bloom, постобработка |
| Мобильное | Базовая линия мобильного рендеринга, профили качества, контракты жизненного цикла Android/iOS |
| Процедурное | Планы генерации рельефа, эрозии, растительности, биомов |

Ключевые архитектурные документы:
- [RHI Layer Governance](docs/rhi-layer-governance.md) — 4-уровневая модель
- [RHI Capability Matrix](RHI_CAPABILITY_MATRIX.md) — Решения о понижении для каждого бэкенда
- [Platform Build Matrix](docs/platform-build-matrix.md) — Поддерживаемые цели сборки
- [FrameGraph v2 Roadmap](docs/framegraph-v2-roadmap.md) — Эволюция RenderGraph

---

## Словарь статусов

| Статус | Значение |
|--------|----------|
| **Contract Complete** | Интерфейсы и тесты описывают ожидаемое поведение |
| **Prototype Complete** | Функция работает хотя бы в одном примере |
| **Backend Validated** | Реализовано и протестировано на реальном графическом бэкенде |
| **Platform Validated** | Пройдено на указанной аппаратной платформе |
| **Production Ready** | Соответствует требованиям времени выполнения, памяти и жизненного цикла для выпуска |

---

## Лицензия

MIT License. Подробнее в [LICENSE](LICENSE).

---

## Благодарности

- [Slang](https://shader-slang.com/) — Кроссплатформенная компиляция шейдеров
- [SDL3](https://libsdl.org/) — Уровень абстракции платформы
- [Vulkan SDK](https://www.lunarg.com/vulkan-sdk/) — Графический API
- [Assimp](https://github.com/assimp/assimp) — Импорт ассетов
- [Jolt Physics](https://github.com/jrouwe/JoltPhysics) — Физическое моделирование
- [mimalloc](https://github.com/microsoft/mimalloc) — Аллокатор памяти

---

<p align="center"><a href="#ru">К началу ↑</a></p>
