# Render Extract — 组件多态提取契约

**状态：Prototype Complete**（P1 基础抽象 + P2 引擎 extractor 重构 + P3 WoCC collect 重构 + P4 HUD → `arp::ui::Canvas`/`RenderExtractor` 全链路已落地，所有相关 contract/unit 测试通过）。当前未覆盖：backend 渲染链路回归与 RenderDoc 截帧验证（Backend Validated/Platform Validated/Production Ready 待后续验收）。

## 背景

引擎的渲染提取层（`Scene3DRenderExtractor` / `Scene2DStreamingPlanner`）原本在 extractor 内部用 `dynamic_cast` 级联 + `is3DComponent()` 谓词特判具体组件类型，导致：

1. 每新增一种可渲染组件就要改 extractor 的类型分支；
2. extractor 与具体组件类型强耦合，违反"组件自描述"的面向对象原则；
3. 同一模式在 game 层（WoCC `GameScene::collectRenderables` 等）被重复实现。

本契约在引擎基础层 `arp::scene` 落地一个**领域无关**的多态提取抽象，让每个可渲染组件自己 override `extract()` 描述自身载荷，extractor 只负责遍历 + dispatch + 收集，不再 `dynamic_cast`。

## 契约

### 文件

- `src/scene/render_extract.h` — 定义 `RenderExtractKind`、`RenderExtractRect`、`RenderExtractPayload`、`RenderExtractContext`、`RenderExtractSink`。
- `src/scene/render_extract.cpp` — anchor TU（当前为空，预留给未来的领域无关提取 helper）。
- `src/scene/component.h` — `Component` 基类新增 `virtual void extract(const RenderExtractContext&, RenderExtractSink&) const`，默认空实现。

### 类型

```cpp
enum class RenderExtractKind {
    Box, Text, Image,           // 领域中立 UI 原语
    Mesh, Terrain, Animation,   // 3D 场景原语（具体字段经 Custom 通道承载）
    Sprite, TileMapChunk,       // 2D 场景原语（具体字段经 Custom 通道承载）
    Custom,                     // 不透明扩展点（WoCC resourceId 模型、sky dome、HUD bar 等）
};

struct RenderExtractPayload {
    RenderExtractKind kind;
    GameObjectId objectId;       // 由 extractor 在 dispatch 前填充
    Transform worldTransform;
    uint64_t sortKey;
    RenderExtractRect rect;      // UI/2D 空间字段
    RenderExtractRect clipRect;
    bool hasClipRect;
    float color[4];              // RGBA，默认不透明白
    arp::core::pmr::String text; // Text kind
    arp::core::pmr::String typeId; // Custom 通道的契约名
    const void* data;            // Custom 通道的不透明数据指针
};

struct RenderExtractContext {
    const GameObject* owner;     // 组件所属对象（forward-declared，避免循环 include）
    const Transform* worldTransform;
    bool activeInHierarchy;
};

class RenderExtractSink {
public:
    virtual ~RenderExtractSink() = default;
    virtual void accept(const RenderExtractContext&, const RenderExtractPayload&) = 0;
};
```

### 硬约束

1. **分层**：`render_extract.h` 位于 `arp::scene` 基础层，只依赖 `arp_core`，**禁止** include `3d/*`、`2d/*`、`ui/*`、`renderer/*`。需要传递领域特定数据（handle/bounds/metadata/resourceId）的组件走 `Custom` kind + `typeId` + `data` 指针，由对应 sink 按 typeId 契约解释。
2. **确定性，禁止猜测 fallback**：sink 收到未知 `typeId` 必须 deterministic diagnostic failure，禁止静默兼容或猜测解释。
3. **默认空实现**：`Component::extract()` 默认 no-op；数据/逻辑组件（TerrainComponent heightfield 持有者、AnimationComponent driver 等）无需 override 也合法。
4. **生命周期**：`data` 指针的生命周期必须包住 sink 调用；组件通常传 `this` 或栈上 POD 字段地址。
5. **内存**：所有 `arp::core::pmr::String` / 动态容器走 `core::defaultCpuMemoryResource()`，禁止默认 STL 容器。
6. **矩阵主序**：本契约不涉及 push-constant/矩阵传递；`worldTransform` 字段是 `arp::scene::Transform`（translation+scale），不引入矩阵 layout 问题。

### typeId 契约注册表（随实现阶段补充）

| typeId | 产出组件 | sink 解释 | 阶段 |
|---|---|---|---|
| `scene3d.mesh` | 引擎 `arp::scene3d::MeshComponent` | `Scene3DRenderQueue` 项 | P2 |
| `scene3d.terrain` | 引擎 `arp::scene3d::TerrainComponent` | `Scene3DRenderQueue` 项 | P2 |
| `scene3d.animation` | 引擎 `arp::scene3d::AnimatorComponent` | `Scene3DRenderQueue` animation binding | P2 |
| `scene2d.sprite` | 引擎 `arp::scene2d::SpriteRendererComponent` | `Scene2DRenderQueue` 项 | P2 |
| `scene2d.tilemap` | 引擎 `arp::scene2d::TileMapComponent` | `Scene2DRenderQueue` 项 + chunk plan | P2 |
| `wocc.mesh` | WoCC `MeshComponent`/`VegetationComponent`/`CritterComponent` | `SceneRenderEntry`/`MeshUploadEntry` | P3 |
| `wocc.sky` | WoCC `SkyboxComponent` | `MeshUploadEntry`（不进 PBR mesh list） | P3 |
| `wocc.hudbar` | WoCC `UiBarComponent` | `WoccHudCanvasBuilder` → `arp::ui::Canvas` Box Widget（bg=`bar.track` + fill=组件 styleId，宽度=rect.width×fillRatio） | P4 |
| `wocc.hudtext` | WoCC `UiTextComponent` | `WoccHudCanvasBuilder` → `arp::ui::Canvas` Text Widget（styleId + fontToken `font.ui.body`） | P4 |

## 验收（P1）

- `tests/scene_render_extract_contract_tests.cpp` 通过：
  - `Component::extract` 默认不调 sink；
  - override 后 payload 经虚函数 dispatch 进 sink，字段逐项正确；
  - payload 默认构造（Custom kind + 白色 + data=null）；
  - payload 可拷贝可移动；
  - Custom 通道承载 typeId + 不透明 data，sink 按 typeId 解释；
  - sink 多态 dispatch；
  - `RenderExtractKind` 所有枚举值两两不等。
- 既有 `arp_scene_3d_visibility_tests`、`arp_scene_2d_streaming_tests` 通过（默认空实现 = 行为无变化）。
- mingw 编译通过：`arp_scene`、`arp_3d`、`arp_2d`、`arp_scene_render_extract_contract_tests`。
- `arp_scene_renderer_extraction_tests` 当前在 master 上即有 exit-127 问题（与本次改动无关，stash 验证确认），不在本契约验收范围内。

## 后续阶段

- **P2**（已完成）：重构 `Scene3DRenderExtractor` / `Scene2DStreamingPlanner` 去 `dynamic_cast`，走 `Component::extract()` + 领域 sink；更新 `scene_renderer_extraction_tests` 源码断言。验收：`arp_scene_render_extract_contract_tests`、`arp_scene_renderer_extraction_tests`、`arp_scene_3d_visibility_tests`、`arp_scene_2d_streaming_tests` 全绿。
- **P3**（已完成）：WoCC `collectRenderables`/`collectMeshesForUpload` 改为 `WoccRenderExtractor` + 组件 `extract()`，消除 `dynamic_cast` 级联。验收：
  - `arp_wocc_render_extractor_tests` 全绿（`wocc.mesh`/`wocc.sky` 收集、renderables 排除 sky、upload 含 sky 去重、多组件收集）；
  - `app/scene.cpp` 的 `collect*` 函数体不含 `findComponentByType` / `dynamic_cast`（grep 验证通过）；
  - `app/scene.cpp`、`app/components.cpp`、`app/wocc_render_extractor.cpp` mingw 独立编译通过；
  - `arp_wocc_movement_camera_tests` 全绿（唯一非 codegen-blocked 的 WoCC 测试）；
  - 完整 `world_of_claudecraft_game` lib 构建曾受 master 已存在的 `arp_reflect_codegen` DLL `0xC0000139`（DLL_INIT_FAILED）问题阻塞——根因为经查实为 build 目录中 MinGW 运行时 DLL（`libgcc_s_seh-1.dll`/`libstdc++-6.dll`/`libwinpthread-1.dll`）与系统 g++ 16.1.0 工具链版本不匹配；用系统 DLL 覆盖后 codegen 恢复正常，完整 lib+exe 构建链路恢复。
- **P4**（已完成）：WoCC HUD 切 `arp::ui::Canvas` + `arp::ui::RenderExtractor` 产出 `ui::RenderQueue`，经 `DrawPlanner` → `UiDrawPlanLowerer`（styleId → theme.styleColors → color）→ `UnifiedUiAtlasRuntime.resolve`（Text → GlyphQuad）产出 `UiDrawPlanLowerResult`；`render_session` 接 `CustomUiGraphPayload`（替代旧 `CustomGuiGraphPayload`/`DebugDrawData`），`GuiRendererDesc` 带 atlas 纹理。验收：
  - `arp_wocc_hud_canvas_tests` 全绿（6 tests）：theme+atlas 确定性加载（5 styleColors / 5 styleFonts）、bundle 构建 + glyph cache 预热（glyphMisses>0）、builder 产出 lowered Box+GlyphQuad 命令且 hp 颜色 `{1.0,0.2,0.2,1.0}`、builder 对未知 styleId 确定性失败、bar fillRatio 驱动 fill widget 宽度（200×0.5=100）、**动态字形上传回归守护**（`wocc_hud_dynamic_upload_schedules_cache_miss_glyph`：断言 cache-miss 字形 'W' 在 runtime `combinedAtlas_` 有非零 alpha 像素且 `combinedAtlasPendingUploads()` 调度覆盖该区域）；
  - `arp_ui_atlas_runtime_cache_tests` 全绿（含新增 `combined_atlas_pending_uploads_offset_by_glyph_page_origin` contract test：断言 `combinedAtlasPendingUploads()` 的 `atlasRect.y` 被 `glyphPageOriginY_` 偏移到 combined-atlas 绝对坐标）；
  - `arp_wocc_render_extractor_tests` 全绿（P3 回归无回归）；
  - `app/hud_ui_bundle.cpp`、`app/hud_canvas_builder.cpp`、`app/components.cpp`、`app/scene.cpp`、`app/render_session.cpp`、`app/app.cpp` mingw 独立编译通过；
  - **HUD 动态字形上传根因修复**：引擎 `UnifiedUiAtlasRuntime` 完整支持动态字形——`resolve()` 在 cache-miss 时调用 `writeGlyphPixels` 把字形 alpha 光栅化进 runtime 的 `mutable combinedAtlas_.rgba8`（CPU 侧），`GlyphAtlasCache::resolveGlyph` 同步 push `GlyphAtlasUploadSchedule` 到 `pendingUploads_`；`RenderSystem::updateGuiAtlasTexture` + `GuiRenderer::updateAtlasTexture` 实现了按 rect 的 GPU 纹理子区域上传（所有 backend）。但调用链断开：WoCC 从未调用 `updateGuiAtlasTexture`，pika 的调用块被注释掉（"temporarily disabled while debugging staging capacity"）。**真正的 root cause 是坐标错配**：`GlyphAtlasUploadSchedule.atlasRect` 存的是 glyph-page-local 坐标（原点 0,0），而 `GuiRenderer::updateAtlasTexture` 把它当 combined-atlas 绝对坐标读取，导致从 base-atlas 区域（空白）取像素——这就是 pika 注释里的 "staging capacity" 问题。修复分两部分：
    - **引擎侧**（`src/renderer/ui/ui_atlas_runtime.h`）：新增 `combinedAtlasPendingUploads()` 方法，返回 `atlasRect.y` 已被 `glyphPageOriginY_` 偏移的 schedule 副本（combined-atlas 绝对坐标），与 `updateAtlasTexture` 的读取空间对齐。`recordUploadsCompleted` 改为 `const`（`telemetry_` 已是 `mutable`）以便从 `const WoccHudUiBundle*` 调用。`pendingUploads()` 保留（page-local 坐标）供诊断/contract test 使用。
    - **WoCC 侧**（`app/render_session.cpp::buildAndSubmitFrame`）：在 HUD lowering 之后、`renderGraphFrame` 之前，调用 `renderSystem_.updateGuiAtlasTexture(hudBundle_->atlasRuntime.atlas(), hudBundle_->atlasRuntime.combinedAtlasPendingUploads())`，把当帧 cache-miss 字形像素上传到 GPU `fallbackAtlasTexture_`。seed 文本恢复为最小集（`"Hero Lv.10 300/300 50/100 0%"`）——它是性能提示（减少首帧 rasterization），不再是正确性依赖。
  - RenderDoc 截帧验证待后续：完整 `world_of_claudecraft_game` lib+exe 构建与窗口化截帧需 codegen DLL 修复后在本地环境回归（codegen 已恢复，待窗口化运行验证 HUD 视觉正确性）。
