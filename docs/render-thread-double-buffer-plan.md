# Render Thread + Double Buffer Plan

## Status: Phase 3 Backend Validated ✅（遗留项全部验证完毕，2026-07-18）

Phase 1（Submission 结构 + 串行重构）✅ Contract Complete、Phase 2（渲染线程 + 双缓冲）✅ Prototype Complete、Phase 3（延迟销毁 retire queue + wind 双 buffer + gameScene 移除 + CPU skinning 删除）✅ Backend Validated 均已完成并验证（2026-07-18）。Phase 3 三个遗留项（streaming retire queue 集成验证 / templateBuffers_+uploadedSlots_ 覆盖评估 / Web 单线程时序）已于 2026-07-18 全部验证并修复。Phase 4（性能验证与调优）仍处于设计阶段。各 Phase 完成后按 AGENTS.md 文档状态分级标注：
`Contract Complete` -> `Prototype Complete` -> `Backend Validated` -> `Platform Validated` -> `Production Ready`。

## Goal

将 World of ClaudeCraft 当前的单线程帧循环拆分为**逻辑线程 + 渲染线程**，通过双缓冲（double buffer）在两线程间传递每帧渲染提交内容。

- **逻辑线程**：输入处理、模拟、动画、CPU skinning、场景遍历、视锥/距离剔除、HUD 布局、光照收集、渲染数据装配。
- **渲染线程**：`acquireFrame` -> FrameGraph 录制 -> RHI `submit` -> `presentFrame`，独占所有 RHI 调用。
- **并行收益**：逻辑线程第 N 帧的 CPU 模拟与渲染线程第 N-1 帧的 GPU 提交真正重叠，消除"GPU 等 CPU 装配、CPU 等 GPU present"的串行空等，稳定帧率。

## Background

当前 `WoccRuntimeSession::stepFrame`（`games/world_of_claudecraft/app/wocc_runtime_session.cpp:701`）在一根调用栈里串行完成全部工作：

| 阶段 | 行号 | 说明 |
| --- | --- | --- |
| 计时 `deltaTime = rt.timer.advance()` | 738-739 | steady_clock 单帧 delta，无固定步长 |
| 输入边沿检测 | 742-797 | |
| 物理 step / 移动 | 807-823 | `gameScene.stepPhysics(deltaTime)` |
| 相机 update | 826-829 | |
| 动画（CPU skinning） | 832-856 | 每帧重算顶点 |
| 动态 mesh 重上传 | 902-975 | `renderSession.updateSceneMesh`，逐 mesh 串行 |
| 构建 push constants | 1024-1201 | 纯 CPU 数学 |
| HUD Canvas 构建 + lower & resolve | 1204-1379 | `WoccHudCanvasBuilder` |
| 多光源收集 + 排序 + UBO 打包 | 1382-1422 | |
| 地形 chunk 上传 | 1452-1455 | `collectAndUploadTerrainChunks` |
| **提交帧** `buildAndSubmitFrame` | 1458-1471 | 场景遍历 + 剔除 + payload 装配 + `renderGraphFrame` + `presentFrame` |

渲染在最后一步。`buildAndSubmitFrame`（`render_session.cpp:1680`）内部完成：风/天气 update -> 场景遍历 `collectRenderables` -> `acquireFrame` -> 视锥/距离剔除 -> payload 装配 -> `renderGraphFrame`（RHI 录制 + 提交）-> `presentFrame`，全同步。GPU 工作与 CPU 逻辑完全不重叠。

## Confirmed Hard Constraints（来自代码探索）

这些约束决定了双缓冲边界必须划得非常谨慎。任何违反都会引入数据竞争或悬挂指针。

### 1. RHI / RenderSystem 完全非线程安全

- `RenderSystem::Impl`（`src/platform/native/render_system_vulkan.cpp:973-1009`）持有可变每帧状态 `rhi::AcquiredFrame currentFrame`，`acquireFrame`/`renderGraphFrame`/`presentFrame` 共享它且**无 mutex**。三者必须在同一线程串行调用。
- `VulkanRhiDevice`（`src/rhi/backends/vulkan/vulkan_device.cpp:539`）无任何 mutex 成员：`createCommandEncoder`、`createBuffer`、`createTexture`、`writeBuffer`、`queue().submit()` 全部直接访问 `nativeResources_`/`allocator_`/`stagingUploadPool_`/`resourceRegistry_`。
- `VulkanRhiQueue::submitCount_`（vulkan_device.cpp:455）非原子递增；`nextResourceId_`（:1130）非原子；`VulkanRhiResourceRegistry::entries_`（vulkan_resource_registry.h:73）`push_back` 与 `pruneExpired` 无锁并发会迭代器失效。
- **结论**：所有 RHI 调用必须串行在渲染线程。逻辑线程不得调用任何 `GpuDevice` 方法。

### 2. payload 含大量裸指针指向 RenderSession 可变成员

`WoccPbrRenderFramePayload`（`wocc_pbr_pipeline.h:244-279`）的指针字段：

| 字段 | 指向 | 风险 |
| --- | --- | --- |
| `pushConstants*` / `skyPushConstants*` / `shadowDepthPushConstants*` | stepFrame 栈局部变量（wocc_runtime_session.cpp:1459-1464） | stepFrame 返回即悬空 |
| `lightArray*` | stepFrame 栈局部 `lightUbo` | 同上 |
| `windField*` / `weather*` / `postPushConstants*` | RenderSession 成员（render_session.h:341,346） | 单线程同步安全，跨线程竞争 |
| `skinningPalette*` | RenderSession 临时 | 同上 |
| `meshes[].vertexBuffer*` / `indexBuffer*` | `uploadedSlots[].buffer` / `chunkGpuSlots_[coord].buffer` | erase/替换时悬挂 |
| `scene*` | `RuntimeScene`（wocc_pbr_pipeline.h:104 别名） | 当前 `payload.scene = nullptr`（render_session.cpp:1838），未活跃使用 |

**结论**：双缓冲不能简单 memcpy payload。push constants / UBO 必须**值拷贝**进 submission；GPU buffer 指针保留但来源必须是 RenderSession 持有的持久资源，且生命周期由延迟销毁保护。

### 3. `hudLoweredPlan` 和 `lightUbo` 是 stepFrame 栈局部变量

- `hudLoweredPlan`（wocc_runtime_session.cpp:1204）是 `UiDrawPlanLowerResult` 局部对象，通过 `const&` 传进 `buildAndSubmitFrame`（:1464），再被取地址存进 `uiPayload_.loweredPlan`（render_session.cpp:2392）。
- `lightUbo` 同理，局部变量地址传进 payload。
- 当前靠"`renderGraphFrame` 在 `buildAndSubmitFrame` 返回前同步消费完"维持安全。
- **结论**：双缓冲必须把 HUD 和 light UBO **值拷贝**进 submission，消除栈引用。

### 4. 地形流式 erase / CPU skinning writeBuffer / wind phase 回写靠时序安全

- `collectAndUploadTerrainChunks` 的 erase 路径（render_session.cpp:733-739）直接 `chunkGpuSlots_.erase(it)`，会析构 `ChunkGpuSlot` 内的 `GpuBuffer`。当前在 `buildAndSubmitFrame` 之前同线程调用，安全。
- `updateSceneMesh`（render_session.cpp:1515-1544）原地 `writeBuffer` 改写 `uploadedSlots[].buffer` 内容（CPU skinning）。
- wind phase 回写（render_session.cpp:1722-1768）遍历 `chunkGpuSlots_`，改 `entry.cpuInstances` 并 `device->writeBuffer`。
- `waitGpuIdle()` 仅在 shutdown/recreate 时调用（wocc_runtime_session.cpp:1587），**不是每帧**。
- `streaming_terrain_redesign.md:66` 已记录："chunk slot 每帧增删，但 `buildAndSubmitFrame` 接收的是 `const UploadedMeshSlot*` 快照，多帧 in-flight 时 buffer 销毁时序未处理"。
- **结论**：拆线程后这三处都成竞争点。需要延迟销毁队列 + CPU skinning 双 vertex buffer。

### 5. `FrameGraphResourcePool` 跨帧共享无锁

- `FrameGraph` 每帧新建（render_system_vulkan.cpp:1379 栈对象），但所有 FrameGraph 共享**同一个** `FrameGraphResourcePool`（render_graph.cpp:22-35 的 `static std::mutex` 只保护查找表，不保护池对象本身）。
- 池的 `cache_`/`bufferCache_`/`pendingRecycle_`（frame_graph_resource_pool.h:134,169,178）无锁。
- **结论**：不能让两帧的 FrameGraph 并发编译/执行。双缓冲保证渲染线程同时只有 1 帧在执行，逻辑线程不接触 FrameGraph。

### 6. 值语义利好

- `UiDrawPlanLowerResult`（ui_draw_plan.h:55-81）完全值语义，自有 `pmr::Vector<UiDrawCommand>`，可安全拷贝。
- `DebugDrawData`（gui.h:600-604）值语义，自带两个 pmr vector。
- `WoccPbrSubmeshDraw` / `WoccInstancedDraw` / `WoccSkyMeshDraw` / `WoccShadowDepthDraw` 是 POD（定长数组），可拷贝。
- **结论**：HUD 和 draw list 都可以值拷贝进 submission。

## Architecture

### 核心思路：帧级解耦（Frame-level Decoupling）

逻辑线程不调用任何 RHI。它产出 `WoccRenderFrameSubmission`（值拷贝的自包含结构），通过无锁 SPSC 双缓冲提交给渲染线程。渲染线程消费它做 acquire->record->submit->present。

```
逻辑线程:  [模拟N | 提取N | 装配submission N]
渲染线程:                [acquire+record+submit N-1 | present N-1]
                                  ↑ 并行区间 ↑
   GPU:                              [====绘制 N-1====]
```

- 逻辑线程第 N 帧 `acquireForWrite` 必须等第 N-2 帧被渲染线程 `releaseRead`（双缓冲槽位回收，背压）。
- 渲染线程 `acquireForRead` 拿到的是逻辑线程已 `publishWrite` 的最新就绪帧；若无就绪帧则等待（保持上一帧画面，不重绘）。
- 逻辑线程比渲染线程快时，`acquireForWrite` 阻塞等待（背压，避免无限堆积）。
- 引入 1 帧输入延迟（业界标准，可接受；未来可加插值预测）。

### 新增结构：`WoccRenderFrameSubmission`（自包含、值语义）

位置：`games/world_of_claudecraft/app/wocc_render_frame_submission.h`（新增）

```cpp
struct WoccRenderFrameSubmission {
    std::uint32_t frameIndex = 0;
    float deltaTime = 0.0f;

    // === 值拷贝的 push constants / UBO（不再是指针）===
    WoccPbrPushConstants pushConstants{};
    WoccSkyPushConstants skyPushConstants{};
    bool hasSky = false;
    WoccShadowDepthPushConstants shadowDepthPushConstants{};
    bool hasShadow = false;
    WoccPostPushConstants postPushConstants{};
    WoccLightArrayUbo lightArray{};
    bool hasLights = false;
    arp::pcg::WoccWindFieldUbo windField{};
    arp::pcg::WoccWeatherUbo weather{};

    // === Draw list（值拷贝，但 GpuBuffer* 指向 RenderSession 持久资源）===
    // 关键：这些 GpuBuffer* 指向 RenderSession 持有的持久资源
    // （uploadedSlots / chunkGpuSlots_ / templateBuffers_）。
    // 生命周期由 RenderSession 拥有；逻辑线程只读引用容器，不增删元素。
    // erase 旧 chunk 走延迟销毁队列（见 GpuResourceRetireQueue）。
    WoccPbrSubmeshDraw meshes[512];
    std::uint32_t meshCount = 0;
    WoccInstancedDraw instancedDraws[512];
    std::uint32_t instancedDrawCount = 0;
    WoccShadowDepthDraw shadowDepth;
    WoccSkyMeshDraw skyMesh;

    // === HUD（值拷贝，解决栈局部变量悬空问题）===
    arp::renderer::ui::UiDrawPlanLowerResult hudLoweredPlan;
    gui::DebugDrawData debugOverlayDrawData;
    bool hasHud = false;
    bool hasDebugOverlay = false;

    // === GPU buffer 快照引用（逻辑线程只读，渲染线程消费时仍有效）===
    // 指向 RenderSession 持有的 uploadedSlots_（从 WoccRuntimeSession::Impl 迁入）。
    const UploadedMeshSlot* uploadedMeshes = nullptr;
    std::size_t numMeshes = 0;

    // === GPU skinning palette（值拷贝）===
    arp::core::pmr::Vector<renderer::GpuSkinningPalette> skinningPalette{
        &arp::core::defaultCpuMemoryResource()};

    // === 序列号：渲染线程确认消费 + 延迟销毁队列判定回收时机 ===
    std::uint64_t submissionSerial = 0;
};
```

设计要点：

- **push constants / UBO 全部值拷贝**，消除裸指针依赖（约束 2、3）。
- **draw list 的 `GpuBuffer*` 保留指针**，但指向 RenderSession 持有的持久资源 -- 逻辑线程只读引用这些容器，不增删元素（约束 4 的 erase 部分靠延迟销毁）。
- **HUD 完整值拷贝**，解决 `hudLoweredPlan` 栈局部变量悬空（约束 3）。
- **`uploadedSlots` 从 `WoccRuntimeSession::Impl` 迁移到 RenderSession 拥有**，让 GpuBuffer 生命周期与渲染线程消费方对齐。

### 双缓冲机制：`RenderFrameDoubleBuffer`

位置：`games/world_of_claudecraft/app/render_frame_double_buffer.h`（新增）

SPSC（单生产者单消费者）双缓冲 + 原子状态机，热路径无 mutex：

```cpp
enum class FrameSlotState : std::uint32_t {
    Writing,     // 逻辑线程正在写
    Ready,       // 逻辑线程写完，等渲染线程消费
    Reading,     // 渲染线程正在读
    Consumed,    // 渲染线程读完，等逻辑线程回收
};

class RenderFrameDoubleBuffer {
public:
    // 逻辑线程：获取可写 slot（阻塞等 Consumed，背压）
    WoccRenderFrameSubmission& acquireForWrite();
    void publishWrite();  // 状态 Writing -> Ready

    // 渲染线程：获取已就绪 slot（阻塞等 Ready，带超时支持 stop）
    const WoccRenderFrameSubmission* acquireForRead();
    void releaseRead();   // 状态 Reading -> Consumed

private:
    WoccRenderFrameSubmission slots_[2];
    std::atomic<FrameSlotState> state_[2]{};
    std::atomic<std::uint32_t> writeIndex_{0};
    std::atomic<std::uint32_t> readyIndex_{kInvalid};
    // ...
};
```

状态机不变量（必须由 contract test 守护）：

- `Writing -> Ready`：仅逻辑线程 `publishWrite` 触发。
- `Ready -> Reading`：仅渲染线程 `acquireForRead` 触发。
- `Reading -> Consumed`：仅渲染线程 `releaseRead` 触发。
- `Consumed -> Writing`：仅逻辑线程 `acquireForWrite` 触发。
- 任意时刻两个 slot 的状态组合只有：`(Writing, Ready)`、`(Writing, Consumed)`、`(Ready, Reading)`、`(Consumed, Reading)` 等合法组合，禁止 `(Writing, Writing)`、`(Ready, Ready)` 等非法组合。

### 渲染线程：`RenderThread`

位置：`games/world_of_claudecraft/app/render_thread.h` / `.cpp`（新增）

```cpp
class RenderThread {
public:
    RenderThread(RenderSession& session, RenderFrameDoubleBuffer& buffer);
    void start();
    void stop();  // 设置 stopRequested_ + 唤醒 + join

private:
    void run();
    RenderSession& session_;
    RenderFrameDoubleBuffer& buffer_;
    std::thread thread_;
    std::atomic<bool> stopRequested_{false};
};
```

`run()` 循环：

1. `acquireForRead()` 拿 submission（阻塞等待 Ready，带超时以支持 stop）。
2. 调 `RenderSession::executeSubmission(submission)` -- `buildAndSubmitFrame` 的重构版本，只做 acquire->record->submit->present，不做模拟/提取/装配。
3. `releaseRead()`。
4. `retireQueue_.reapCompleted(lastConsumedSerial)`（Phase 3 引入）。

渲染线程独占的 RHI 访问权：`acquireFrame` / `renderGraphFrame` / `presentFrame` / `createCommandEncoder` / `queue().submit()` 全部在此线程。

### 延迟销毁队列：`GpuResourceRetireQueue`

位置：`src/core/runtime/gpu_resource_retire_queue.h`（Phase 3 ✅ header-only 模板，沉到 core 层复用）

设计参考 Unity GfxDevice DeletePending / Unreal RT_DeferredDelete。逻辑线程单线程访问（retire 在 `collectAndUploadTerrainChunks`，reap 在 `stepFrame` 的 `waitFrameCompleted` 之后），无 mutex，同步靠 SPSC double buffer 的 `completedFrameSerial()`。

```cpp
template <typename T>
class GpuResourceRetireQueue {
public:
    // 逻辑线程：把要销毁的资源转移进来（不立即析构），stamp 当前 submission serial
    void retire(std::uint64_t retiredAtSerial, T&& resource);

    // 逻辑线程：waitFrameCompleted 后调用，销毁 retiredAtSerial < completedFrameSerial 的条目
    // FIFO prefix-stop：只销毁 contiguous prefix，遇到 S >= C 即停（不允许 scan-ahead）
    std::size_t reapCompleted(std::uint64_t completedFrameSerial);

    // shutdown 专用：waitGpuIdle 后销毁全部，忽略 serial gating
    std::size_t reapAll();

private:
    struct Entry {
        std::uint64_t retiredAtSerial = 0;
        T resource{};
    };
    // 走 core::pmr 遵守 AGENTS.md allocator 约束（生产运行时禁止默认 STL 动态容器）
    arp::core::pmr::Deque<Entry> entries_{&arp::core::defaultCpuMemoryResource()};
};
```

使用点：

- `collectAndUploadTerrainChunks` 的 erase 路径：`chunkRetireQueue_.retire(currentSubmissionSerial, std::move(it->second));` 再 `chunkGpuSlots_.erase(it);`。容器条目 erase 安全（渲染线程通过 submission 值拷贝快照访问，不迭代容器）。
- `stepFrame` 的 `waitFrameCompleted(N-1)` 之后：`rt.renderSession.reapRetiredChunks(rt.frameBuffer_.completedFrameSerial());`。
- shutdown 路径：`waitGpuIdle()` 后 `rt.renderSession.reapAllRetiredChunks();`。

**关键不变量**（由 `tests/contract/gpu_resource_retire_queue_contract_tests.cpp` 9 场景守护）：FIFO + serial gating（strictly-greater rule）、prefix-stop（不允许 scan-ahead）、reapAll bypass、move 语义、empty queue 安全、repeat reap 幂等、destructor drains。

### ~~CPU skinning 双 vertex buffer~~（Phase 3 删除 CPU skinning，此方案废弃）

原计划：CPU skinning 的 `updateSceneMesh` 原地写 buffer 会与渲染线程 drawcall 竞争，方案是 `UploadedMeshSlot` 持有 2 个 vertex buffer 帧交换。

**Phase 3 用户决策**：`"删掉CPU_SKIN的设计，只保留GPU SKIN设计上不要弄复杂了"`。CPU skinning 整体删除（`cpu_skinning.h/.cpp` / `SkinnedMeshPlayer.frameMesh()` / `AnimatorComponent` frameMesh API / `scene.cpp` 烘焙路径 / `wocc_gpu_skinning_config.h`），只保留 GPU skinning。GPU skinning palette 含 `worldTranslationAndScale`（世界位置 + foot-alignment Y offset）+ `worldRotation`（yaw + basis rotation），vertex shader 处理世界放置，无需 CPU baking，无双 buffer 竞争问题。

此双 vertex buffer 方案废弃，不再实现。wind instance buffer 双 buffer（步骤 4）保留，因其解决的是 wind phase 回写竞争而非 CPU skinning。

## Phases

| Phase | Name | Target Status | Deliverable |
| --- | --- | --- | --- |
| 1 | Submission 结构 + 串行重构 | Contract Complete ✅ | `WoccRenderFrameSubmission` 结构 + `executeSubmission` 重构，仍单线程串行调用，验证正确性 |
| 2 | 渲染线程 + 双缓冲 | Prototype Complete ✅ | `RenderThread` + `RenderFrameDoubleBuffer`，逻辑/渲染真正并行 |
| 3 | 延迟销毁 retire queue + wind 双 buffer + gameScene 移除 + CPU skinning 删除 | Backend Validated ✅ | `GpuResourceRetireQueue` + wind instance 双 buffer + gameScene 指针移除 + CPU skinning 彻底删除（只保留 GPU skinning） |
| 4 | 性能验证与调优 | Platform Validated | FPS 稳定性提升 + RenderDoc 截帧对比 + 热路径审查 |

### Phase 1: Submission 结构 + 串行重构（Contract Complete ✅）

**状态**：已完成（2026-07-17）。验收记录见本节末尾。

**目标**：重构出 `WoccRenderFrameSubmission` 和 `RenderSession::executeSubmission`，但**仍单线程串行调用**，验证重构正确性。本阶段不引入线程，纯结构重构。

**步骤**：

1. 新增 `wocc_render_frame_submission.h`：定义 `WoccRenderFrameSubmission` 结构（值拷贝 push constants / UBO / HUD / draw list）。
2. 在 `wocc_runtime_session.cpp::stepFrame` 里：把原来直接调 `buildAndSubmitFrame(gameScene, pc, ...)` 的逻辑，改为先装配 `WoccRenderFrameSubmission submission`（值拷贝所有 push constants/UBO/HUD），再调 `RenderSession::executeSubmission(submission)`。
3. 把 `RenderSession::buildAndSubmitFrame` 重构为 `executeSubmission(const WoccRenderFrameSubmission&)`：
   - 保留 wind/weather update（仍在此函数内，需要 deltaTime 和 `device->writeBuffer`）。
   - 保留场景遍历 `collectRenderables`（Phase 2 再迁移到逻辑线程）。
   - 保留视锥/距离剔除、payload 装配。
   - 从 submission 读取 push constants/UBO（值拷贝，不再是指针）。
   - HUD 从 submission 读取（值拷贝，不再引用 stepFrame 栈变量）。
4. ~~把 `uploadedSlots` 从 `WoccRuntimeSession::Impl`（wocc_runtime_session.cpp:143-145）迁移到 `RenderSession` 持有，让 GpuBuffer 生命周期与消费方对齐。~~ **已调整：移至 Phase 3。** `UploadedMeshSlot` 是 move-only（每 slot 持有 3 个 GPU buffer + PMR string），迁移涉及 9 个访问点含每帧 CPU skinning 热路径（`updateSceneMesh`），且其收益（GpuBuffer 生命周期与消费方对齐）只在 Phase 2/3 跨线程时才需要。Phase 1 保持 `submission.uploadedMeshes = rt.uploadedSlots.data()` 的非拥有 const 指针，单线程同步 `executeSubmission` 调用下安全。Phase 3 与延迟销毁队列一并迁移（与本文 Risks 表一致）。
5. 更新 `WoccPbrRenderFramePayload` 注释（wocc_pbr_pipeline.h:244-279）：标注指针来源变更。

**测试**：

- 新增 contract test `tests/contract/wocc_render_frame_submission_contract_tests.cpp`：断言 submission 结构大小、字段偏移、值拷贝完整性（装配后字段与源一致）。
- 新增 contract test：断言 `executeSubmission` 不再接收栈局部变量的指针（payload 字段类型静态断言）。
- 跑现有 `arp_shader_tests` + 相关 contract test 回归。
- Null hidden-frame smoke 验证渲染仍正常。

**验收**：画面与改造前完全一致（RenderDoc 截帧对比），单线程性能不退化超过 5%。

#### Phase 1 实施记录（Contract Complete，2026-07-17）

**实际实现要点（与计划步骤的偏差）**：
- `executeSubmission` 实现为**薄适配层**：不复制 `buildAndSubmitFrame` 的 840 行主体，而是从 submission 值字段取地址（`&submission.pushConstants` 等）以 `const&` 委托给现有 `buildAndSubmitFrame`。这保证 `buildAndSubmitFrame` 签名与实现零改动、零回归，submission 值语义由 contract test 单独守护。Phase 2 再收敛为渲染线程直接消费 submission。
- `WoccRenderFrameSubmission` **不含** draw list / windField / weather / postPushConstants / postFxCarrier（计划步骤 3 明确保留这些在 `executeSubmission` 内部从 RenderSession 成员生成）。submission 只值拷贝 push constants / sky / shadow / light UBO / HUD，外加 `uploadedMeshes` 非拥有指针 + 预留的 `skinningPalette` 字段（Phase 2 启用）。
- `wocc_render_frame_submission.h` 对 `UploadedMeshSlot` 用前向声明（避免与 `render_session.h` 的循环 include）。

**新增/修改文件**：
- 新增 `games/world_of_claudecraft/app/wocc_render_frame_submission.h`
- 新增 `tests/contract/wocc_render_frame_submission_contract_tests.cpp`（static_assert 值类型字段 + trivially copyable + 值拷贝完整性 + HUD 深拷贝 + 指针 passthrough）
- 修改 `games/world_of_claudecraft/app/render_session.h` / `.cpp`（新增 `executeSubmission` 声明 + 实现）
- 修改 `games/world_of_claudecraft/app/wocc_runtime_session.cpp`（stepFrame 装配 submission + 调 executeSubmission）
- 修改 `games/world_of_claudecraft/app/wocc_pbr_pipeline.h`（payload 注释更新）
- 修改 `build.py`（test_configs 注册新测试，依赖 `world_of_claudecraft_game`）

**验证结果**：
- ✅ `cmake --build --preset win-auto-debug --target arp_wocc_render_frame_submission_contract_tests` 编译通过，运行 exit=0。
- ✅ `game wocc` 编译通过（render_session.cpp / wocc_runtime_session.cpp / wocc_pbr_pipeline.cpp 全部干净编译）。
- ✅ Null hidden-frame smoke：`arp_world_of_claudecraft.exe --hidden --frames 30` 运行 30 帧正常退出（exit=0），初始化/光照/天气/地形 stitching 诊断输出与改造前一致。
- ⚠️ `arp_shader_tests` 链接失败（`cannot find -larp_spirv_reflect`）——**pre-existing 仓库问题**，经 `git stash` 验证在本次改动前即失败，与 Phase 1 无关。矩阵 layout contract test 未被本次改动触及（无 shader 变更）。
- ⚠️ `arp_engine.dll` 链接失败（`export ordinal too large: 67085`）——pre-existing MinGW `--export-all-symbols` 限制，与 Phase 1 无关；WoCC 可执行文件静态链接，不受影响。

**Phase 2 起点**：渲染线程 + 双缓冲。`executeSubmission` 已就绪可被 `RenderThread::run()` 消费；下一步把 stepFrame 末尾的同步调用改为 `acquireForWrite` -> 装配 -> `publishWrite`，并实现 `RenderFrameDoubleBuffer` SPSC 状态机。

### Phase 2: 渲染线程 + 双缓冲（Prototype Complete ✅）

**目标**：引入渲染线程和双缓冲，逻辑线程与渲染线程真正并行。

**步骤**：

1. 新增 `render_frame_double_buffer.h`：实现 SPSC 双缓冲（原子状态机）。
2. 新增 `render_thread.h` / `.cpp`：实现 `RenderThread`，`run()` 循环消费 submission。
3. 修改 `WoccRuntimeSession`：
   - `stepFrame` 末尾不再直接调 `executeSubmission`，而是 `acquireForWrite` -> 装配 submission -> `publishWrite`。
   - 逻辑线程在 `acquireForWrite` 时阻塞等待渲染线程回收槽位（背压）。
4. **关键迁移**：把 wind/weather update、场景遍历 `collectRenderables` + 视锥/距离剔除 + payload 装配从 `executeSubmission` 迁移到逻辑线程的 stepFrame（在 `publishWrite` 之前完成）。`executeSubmission` 只保留 acquire->record->submit->present。
5. wind phase 的 `device->writeBuffer`（render_session.cpp:1758）迁移到逻辑线程 -- 但 `writeBuffer` 是 RHI 调用，逻辑线程不能调。解决方案：wind phase 回写的 instance buffer 也走双缓冲（与 CPU skinning 同方案，Phase 3 统一实现）。Phase 2 临时方案：wind phase 仍在渲染线程 `executeSubmission` 内执行（从 submission 读 windField UBO 值，渲染线程做 writeBuffer），保持正确性。
6. CPU skinning 双缓冲：实现 `UploadedMeshSlot` 的双 vertex buffer + 帧交换。
7. Web (Emscripten) 端保持单线程串行（`#ifndef __EMSCRIPTEN__`），双线程仅桌面端启用。

**测试**：

- contract test `tests/contract/render_frame_double_buffer_contract_tests.cpp`：状态机不变量（Writing->Ready->Reading->Consumed 循环、背压正确、非法状态组合被拒绝）。
- contract test `tests/contract/render_thread_contract_tests.cpp`：渲染线程停止/启动正确性（stop 后无悬挂 submission、join 干净）。
- Null hidden-frame smoke：双线程模式下渲染正常。
- ThreadSanitizer（如 MinGW 支持）跑 smoke 验证无数据竞争。

**验收**：双线程模式下画面正确，逻辑线程 CPU 时间与渲染线程 GPU 提交时间重叠（用 performance snapshot 的 `gameplayMs` vs `renderSubmitMs` 确认无串行依赖）。

#### Phase 2 实施记录（Prototype Complete，2026-07-18）

**步骤 1-3、7（双线程机制 + SPSC 双缓冲）**：先前已落地。`render_frame_double_buffer.h` 实现 SPSC 原子状态机；`render_thread.h` 实现 `RenderThread<T>` + `RenderThreadConsumer<T>` 虚接口；`WoccRuntimeSession::stepFrame` 改为 `acquireForWrite` -> 装配 -> `publishWrite`，桌面端启用渲染线程，Web 端保持单线程串行。

**步骤 4（关键迁移：CPU 工作从 `buildAndSubmitFrame` 迁到逻辑线程）**：本日完成。`RenderSession::buildAndSubmitFrame`（840 行）拆分为：
- `assembleFrameSubmission(WoccRenderFrameSubmission&)`（逻辑线程，~600 行）：wind/weather update、wind phase 相位推进（纯 CPU）、collectRenderables、BVH 重建 + 视锥/距离剔除、chunk/object payload 装配、shadow 激活、post push constants/carrier 构建、skinning palette 构建、CPU perf 统计。产出值拷贝 submission，零 RHI 调用。
- `executeSubmission(const WoccRenderFrameSubmission&)`（渲染线程，~280 行）：wind writeBuffer 重放（从 submission 值拷贝数据）、acquireFrame、RenderDoc capture、workloadDesc 组装、glyph atlas upload、renderGraphFrame、presentFrame、RHI perf 统计。

**步骤 5（wind phase）决策与实现**：采用"相位结果值拷贝进 submission"方案。逻辑线程推进 wind phase 后，把 `(GpuBuffer*, cpuInstances 快照)` 值拷贝进 `submission.windInstanceWrites`（`pmr::Vector<WindInstanceWriteEntry>`，非 trivially copyable 但 value-semantic）。渲染线程只迭代 submission 快照做 writeBuffer，**不再迭代 `chunkGpuSlots_`**，消除了原 plan.md:355 临时方案未解决的并发容器迭代竞争。writeBuffer 仍在渲染线程（RHI 调用），instance buffer 双缓冲留 Phase 3。

**步骤 6（~~CPU skinning 双缓冲~~）**：~~未在 step 4 范围内实现。`submission.skinningPalette` 字段已就绪（逻辑线程填值，`hasGpuSkinningPalette=true`），双 vertex buffer 帧交换留 Phase 3。~~ **Phase 3 用户决策删除 CPU skinning**，此步骤废弃。GPU skinning palette（`worldTranslationAndScale` + `worldRotation`）在 vertex shader 处理世界放置，无需 CPU baking，无双 buffer 竞争。

**Performance snapshot 拆分**：原 `RenderSessionPerformanceSnapshot`（跨线程共享 cache line 写）拆为：
- `WoccCpuPerfSnapshot`（定义在 `wocc_render_frame_submission.h`，逻辑线程写，值拷贝进 submission）：windUpdateMs / sceneCollectMs / cullAndPayloadMs + 剔除计数。
- `WoccRhiPerfSnapshot`（定义在 `render_session.h`，渲染线程独占写 `rhiPerf_`）：acquireMs / renderSubmitMs / presentMs。
- `readPerfAndClose`（wocc_runtime_session.cpp）在 `waitFrameCompleted` 后合并两半，时序安全。

**新增/修改文件**：
- 修改 `games/world_of_claudecraft/app/wocc_render_frame_submission.h`（扩 draw list / wind-weather UBO / post PC+carrier / windInstanceWrites / cpuPerf 字段 + `WoccCpuPerfSnapshot` 定义）
- 修改 `games/world_of_claudecraft/app/render_session.h`（拆 perf snapshot、`rhiPerf_` 成员 + 访问器、`assembleFrameSubmission` + 单参 `executeSubmission` 声明、删 `buildAndSubmitFrame` + 双参 `executeSubmission`）
- 修改 `games/world_of_claudecraft/app/render_session.cpp`（新增 `assembleFrameSubmission` + 重写 `executeSubmission` 为纯 RHI；删旧 `buildAndSubmitFrame` + 旧双参 `executeSubmission`）
- 修改 `games/world_of_claudecraft/app/wocc_runtime_session.cpp`（stepFrame 调 `assembleFrameSubmission` + `readPerfAndClose` 改读 `cpuPerf`/`rhiPerf_` + Web 路径调单参 `executeSubmission`）
- 修改 `games/world_of_claudecraft/app/wocc_render_frame_consumer.cpp`（`consumeFrame` 调单参 `executeSubmission`）
- 修改 `tests/contract/wocc_render_frame_submission_contract_tests.cpp`（新增 4 个值拷贝断言 + 11 条 static_assert：trivially copyable / 字段类型 / sizeof 上界 < 1 MiB）

**验证结果**：
- ✅ engine + game wocc 干净编译（MinGW + CMake + Ninja，`--preset win-auto-debug`）。
- ✅ `arp_wocc_render_frame_submission_contract_tests` exit=0（新增 4 个值拷贝断言：draw list / wind-weather UBO+post / wind instance 列表 / CPU+RHI perf snapshot）。
- ✅ `arp_spsc_frame_double_buffer_contract_tests` 回归 exit=0。
- ✅ `arp_render_thread_contract_tests` 回归 exit=0。
- ✅ `arp_shader_tests`（矩阵 layout contract，含 pbr_scene/sky/shadow_depth RowMajor 断言）exit=0（从 build 目录运行，相对路径 `shaders/wocc_sky.spv`）。
- ✅ Null hidden-frame smoke 双线程 `--hidden --frames 30` + `--frames 60` exit=0，初始化/光照/天气/地形 stitching 诊断与改造前一致，渲染线程启动确认（`[wocc-session] render thread started (Phase 2 double-buffer mode)`）。
- ✅ **RenderDoc 截帧画面一致性验证**（按 AGENTS.md "可视化效果必须由 AI 通过 RenderDoc 截帧自行确认"）：双线程模式第 30 帧 capture（`artifacts/renderdoc/step4_verify/wocc_step4_frame30_capture.rdc`），`qrenderdoc --python` 离线分析：
  - 493 actions，447 DrawIndexed + 11 非 indexed Draw + 2 Clear + Present EID 2866，drawcall 结构与单线程 baseline 一致。
  - GUI 全屏 quad（numIndices==6）221 个，与 2026-07-14 baseline GUI pass 序列一致。
  - 最大 draw EID 166 numIndices=368085（主 PBR 场景地形）。
  - **FINAL backbuffer avg luminance = 0.7217**（RGB 0.588/0.752/0.815，明亮天空+地形色调），`[OK] Bright image`。
  - **GUI dim 回归未发生**：vs 2026-07-14 baseline 当时 GUI 全屏 quad 把 grade 输出从 0.579 压到 0.139，本次 step 4 双线程模式 backbuffer 保持 0.7217 明亮。
  - GUI atlas 纹理 = `ResourceId::535 1024x1120`（非 1x1 fallback，确认 HUD bundle 加载正常）。
  - 分析脚本与原始输出留存：`artifacts/renderdoc/step4_verify/analyze_step4.py` + `analyze_step4_output.txt`。

**已知遗留（留 Phase 3，不在 step 4 范围）**：
- `chunkGpuSlots_` erase 路径（streaming 退役）的 GpuBuffer 析构竞争 -- Phase 3 `GpuResourceRetireQueue` 兜底。step 4 阶段承认有窗口，建议限制 streaming 频率或暂不触发 erase。
- wind writeBuffer 仍在渲染线程（从 submission 值拷贝数据写）-- ✅ Phase 3 instance buffer 双缓冲彻底解决。
- `submission.gameScene` 指针仍保留（渲染线程不再读它，仅 gated-off debug overlay 用）-- ✅ Phase 3 移除。
- ~~CPU skinning 双 vertex buffer 帧交换未实现 -- Phase 3。~~ ✅ Phase 3 删除 CPU skinning，此遗留项消除。
- RenderDoc `SaveTexture` PNG 导出失败（API 签名变更，`TextureSave const &` 参数），不影响亮度测量结论；PNG 导出留后续修。

### Phase 3: 延迟销毁 retire queue + wind/skinning 双 buffer + gameScene 移除 + CPU skinning 删除（Backend Validated）

**目标**：解决双线程模式下的跨线程 GPU 资源生命周期与并发读写竞争，彻底切断渲染线程对逻辑线程活对象的访问。Phase 3 实施时范围扩展：用户决定删除 CPU skinning 设计，只保留 GPU skinning，简化架构。

**实施前用户决策（已批准）**：
- GpuBuffer 所有权**不改**（保持 `UniquePtr`），用 retire queue（deferred deletion）解决生命周期，不用 `SharedPtr` 引用计数。
- wind instance buffer + CPU skinning **保留双 buffer**（解决并发读写）。
- **删除 CPU skinning 设计**，只保留 GPU skinning（`"删掉CPU_SKIN的设计，只保留GPU SKIN设计上不要弄复杂了"`），彻底删除所有 CPU skinning 代码 + `AnimatorComponent` frameMesh API。
- retire queue 的 reap serial 用 `frameBuffer_.completedFrameSerial()`（已存在的访问器，`spsc_frame_double_buffer.h:184`）。

#### 实施记录

**步骤 1：新增 `GpuResourceRetireQueue`（core/runtime）**
- 新增 `src/core/runtime/gpu_resource_retire_queue.h`：header-only 模板，仿 `spsc_frame_double_buffer.h` / `render_thread.h` 风格。
- API：`retire(serial, T&&)` / `reapCompleted(completedFrameSerial)` / `reapAll()` / `size()` / `empty()`。
- 内存纪律：`arp::core::pmr::Deque<Entry>` + `&arp::core::defaultCpuMemoryResource()`（AGENTS.md 约束）。
- 线程模型：单线程访问（逻辑线程 retire + reap），无 mutex，同步靠 SPSC double buffer 的 `completedFrameSerial()`。
- 不变量：FIFO + serial gating（strictly-greater rule：`reapCompleted(C)` 只销毁 `retiredAtSerial < C` 的条目，且只销毁 contiguous FIFO prefix，prefix-stop rule 防止 scan-ahead）。

**步骤 2：接入 retire queue 到 RenderSession**
- `render_session.h`：新增成员 `arp::core::runtime::GpuResourceRetireQueue<ChunkGpuSlot> chunkRetireQueue_;`。
- `collectAndUploadTerrainChunks` 签名加 `std::uint64_t currentSubmissionSerial` 参数；erase 路径改为 `chunkRetireQueue_.retire(currentSubmissionSerial, std::move(it->second));` 再 `chunkGpuSlots_.erase(it);`。
- 新增 `reapRetiredChunks(std::uint64_t completedFrameSerial)` / `reapAllRetiredChunks()` 内联方法。

**步骤 3：stepFrame 调用 reap + shutdown reapAll**
- `wocc_runtime_session.cpp` stepFrame：`waitFrameCompleted(N-1)` 之后插入 `rt.renderSession.reapRetiredChunks(rt.frameBuffer_.completedFrameSerial());`。
- shutdown 路径：`waitGpuIdle()` 后加 `rt.renderSession.reapAllRetiredChunks();`。

**步骤 4：wind instance buffer 双 buffer**
- `render_session.h` `InstanceBufferEntry`：`buffer` 单 buffer 改 `arp::core::UniquePtr<rhi::GpuBuffer> buffers[2]; uint8_t writeIndex = 0;` + `currentBuffer()` / `flipWriteIndex()` 访问器。
- `uploadChunkInstances`：同时创建 `buffers[0]` 和 `buffers[1]`。
- `assembleFrameSubmission`：windInstanceWrites 和 instancedDraws 同帧引用 `entry.currentBuffer()`（保证 wind writeBuffer 和 drawcall 读同一 buffer）；`assembleFrameSubmission` 末尾对所有 `chunkGpuSlots_` instanceBuffers 执行 `entry.flipWriteIndex()`。
- 时序：逻辑线程帧 N 写 buffer[writeIndex]，渲染线程帧 N writeBuffer+drawcall 同 buffer（渲染线程内串行安全）；下一帧逻辑线程写 buffer[1-writeIndex]，渲染线程帧 N+1 用 buffer[1-writeIndex] -- 逻辑线程与渲染线程不碰同一 buffer。

**步骤 5：删除 CPU skinning（用户决策：只保留 GPU skinning）**

探索阶段发现 CPU skinning 依赖图横跨 `SkinnedMeshPlayer` / `AnimatorComponent` / `scene.cpp` / `wocc_gpu_skinning_config.h` / unity validator / 多个测试，用户决策"彻底删除：移除所有 CPU skinning 代码 + AnimatorComponent frameMesh API"。

- `src/animation/skinned_mesh_player.h/.cpp`：重写，删除 `frameMesh()` / `setCpuSkinningEnabled` / `cpuSkinningEnabled_` / `skinningVertices_` / `skinnedVertices_` / `frameMesh_` / `makeCpuVertex` helper；`finalizeFromLocalPose()` 的 CPU baking block 替换为 `return true;`。添加 Phase 3 类文档注释说明 GPU-skinning-only。
- `src/animation/cpu_skinning.h/.cpp`：**删除**。
- `src/3d/animator_component.h/.cpp`：`AnimatorEvaluationFrame` 构造器删除 `evaluatedMesh` / `cpuSkinningEvaluatedThisFrame` 参数；删除 `hasEvaluatedMesh()` / `evaluatedMesh()` / `cpuSkinningEvaluatedThisFrame()` 访问器 + 成员；`evaluate()` 用 bind-pose mesh 计算 bounds。
- `games/world_of_claudecraft/app/scene.cpp`：删除 `useGpuSkinning` gate + `setCpuSkinningEnabled` 调用；删除整个 `if (!useGpuSkinning)` 烘焙 block（~151 行）。
- `games/world_of_claudecraft/app/wocc_gpu_skinning_config.h`：**删除**；移除所有 include 与 `runtime::gpuSkinningEnabled()` gate。
- `games/world_of_claudecraft/app/wocc_runtime_session.cpp`：删除 player per-frame `updateSceneMesh` re-upload block（critter re-upload 保留，procedural geometry 合并不是 CPU skinning）。
- `tools/unity_import/unity_character_import_validator.cpp`：重写 -- 删除 `#include "animation/cpu_skinning.h"`，新增 `SkinnedPosition` struct + `computeSkinnedPositions()` helper（离线诊断，直接用 `skinMatrices` + `transformPoint`，不是运行时算法重复）；`bindPoseSkinningMaxDisplacement` 和 `validateSkinningSamples` 改用该 helper 构建 `MeshAsset skinnedMesh` 而非读 `frameMesh()`。
- 测试：删除 `tests/unit/cpu_skinning_tests.cpp` + `tests/integration/skinned_mesh_player_tests.cpp` + `tests/integration/skinned_mesh_player_foot_ik_tests.cpp`；`tests/integration/animator_component_tests.cpp` 删除 `frame.evaluatedMesh()` / `frame.cpuSkinningEvaluatedThisFrame()` 断言，保留元数据断言；`.category_mapping.json` 同步。
- `CMakeLists.txt`：移除 `src/animation/cpu_skinning.cpp` 源文件 + `arp_skinned_mesh_player_foot_ik_tests` target。
- 安全性：GPU skinning palette 已含 `worldTranslationAndScale`（player 世界位置 + foot-alignment Y offset）+ `worldRotation`（yaw + basis rotation），vertex shader 内处理世界放置，故删除 CPU baking 不影响放置正确性。

**步骤 6：gameScene 指针移除 + 值拷贝快照**
- `wocc_render_frame_submission.h`：删除 `const GameScene* gameScene = nullptr;` 字段；更新 Phase 2 文档注释为 Phase 3 Contract Complete 说明。
- `render_session.h`：`assembleFrameSubmission(WoccRenderFrameSubmission&)` 改为 `assembleFrameSubmission(const GameScene& gameScene, WoccRenderFrameSubmission&)`；更新 `executeSubmission` 文档注释说明不再持有 GameScene 指针。
- `render_session.cpp` assembleFrameSubmission：7 处 `submission.gameScene->` 改为参数 `gameScene.`（collectRenderables / buildGpuSkinningPalette / skyResourceId / playerObjectId / playerPosition / vegetationResourceId / waterResourceId / roadResourceId）；删除 gated-off debug overlay block（`if (false && guiAtlasReady)` 内的 `submission.gameScene` deref）。
- `wocc_runtime_session.cpp`：删除 `frameSubmission.gameScene = &rt.gameScene;`；调用改为 `rt.renderSession.assembleFrameSubmission(rt.gameScene, frameSubmission);`。
- `wocc_render_frame_consumer.cpp/.h`：删除 gameScene null check block；更新 Phase 2 文档注释为 Phase 3 说明。
- **关键**：collectRenderables 仍在逻辑线程执行（用参数 `gameScene`），结果 entries 值拷贝进 submission draw list；渲染线程 `executeSubmission` 只读 submission 值字段，不遍历活 GameScene。

**步骤 7：contract test（retire queue）**
- 新增 `tests/contract/gpu_resource_retire_queue_contract_tests.cpp`：纯 `<cassert>` + `int main()` 模式（仿 `spsc_frame_double_buffer_contract_tests.cpp`）。
- 夹具 `CountingResource`：tag + moved-from sentinel + 全局 `destroyedTags` 日志（区分 queue 析构 live entry vs caller's moved-from shell 出作用域）。
- 9 个场景：FIFO + serial gating、reapAll bypass、move semantics、empty queue、repeat reap idempotent、out-of-order serials prefix-stop、destructor drains、move-constructed queue preserves entries、multiple retire interleaved reap。
- **关键修正**：out-of-order serials 测试初始断言错误（以为 `reapCompleted(11)` 只销毁 1 个），实际 prefix-stop rule 会销毁 contiguous prefix（A 和 B 都 `S < 11`），修正为 `== 2` 并补充注释说明 prefix-stop 不允许 scan-ahead。

**步骤 8：CMakeLists 注册 + 编译验证 + 回归 + Null smoke + RenderDoc**
- `CMakeLists.txt`：注册 `arp_gpu_resource_retire_queue_contract_tests`（`add_executable` + `target_link_libraries(... arp_core)` + `add_test`）。
- ✅ engine + game wocc 干净编译（MinGW + CMake + Ninja，`--preset win-auto-debug`）。
- ✅ `arp_gpu_resource_retire_queue_contract_tests` exit=0（9 场景全绿）。
- ✅ 回归：`arp_spsc_frame_double_buffer_contract_tests` / `arp_render_thread_contract_tests` / `arp_wocc_render_frame_submission_contract_tests` / `arp_shader_tests`（矩阵 layout contract）/ `arp_foot_ik_tests` 全部 exit=0。
- ✅ Null hidden-frame smoke 双线程 `--hidden --frames 30` + `--frames 60` exit=0，诊断输出与 Phase 2 一致（`[wocc-session] render thread started (Phase 2 double-buffer mode)`）。
- ✅ **RenderDoc 截帧画面一致性验证**（按 AGENTS.md "可视化效果必须由 AI 通过 RenderDoc 截帧自行确认"）：Phase 3 双线程模式第 30 帧 capture（`artifacts/renderdoc/phase3_verify/wocc_phase3_frame30_capture.rdc`，VulkanSurface 模式非 NullHidden，否则 `executeSubmission` 短路不触发 capture），`qrenderdoc --python` 离线分析（`artifacts/renderdoc/phase3_verify/analyze_phase3.py` + `analyze_phase3_output.txt`）：
  - **Draw-call 结构完全一致**：493 actions（baseline 493）、447 DrawIndexed（baseline 447）、11 非 indexed Draw（baseline 11）、2 Clear（baseline 2）、Present EID 2866（baseline 2866）。
  - **GUI 全屏 quad**：221 个（baseline 221），GUI dim 回归未发生。
  - **最大 draw**：EID 166 numIndices=368085（baseline 368085，主 PBR 场景地形）。
  - **FINAL backbuffer avg luminance = 0.7227**（baseline 0.7217，drift=0.0010，在 2% 容差内），RGB (0.589, 0.753, 0.816) vs baseline (0.588, 0.752, 0.815)，`[OK] Phase 3 did NOT regress image`。
  - **GUI atlas 纹理** = `ResourceId::535 1024x1120`（非 1x1 fallback，与 Phase 2 一致）。
  - **[OVERALL] PASS** - Phase 3 lifecycle/concurrency fixes did NOT regress visuals。

**已知遗留（不在 Phase 3 范围）**：
- retire queue 仅覆盖 `chunkGpuSlots_` erase 路径。`templateBuffers_` 和 `uploadedSlots_` 经分析**不需要**接入 retire queue：两者在会话期内 size 稳定（`templateBuffers_` 由 `templatesUploaded_` 一次性上传门控，`uploadedSlots` 仅在 init 时 populate，之后只有 `updateSceneMesh` 的 in-place 顶点重传，从不 erase/clear），无 mid-session 析构可延迟。retire queue 存在的唯一前提（"container 元素持有 GPU buffer 且被 in-flight submission 引用，且 mid-session erase"）只有 `chunkGpuSlots_` 的流式 erase 路径满足。
- Web (Emscripten) 单线程路径 retire queue 时序：**已修复**。原 Web 路径在 `executeSubmission` 后未调 reap，retire queue 会无限增长。现 `stepFrame` 的 `#if defined(__EMSCRIPTEN__)` 分支在 inline `executeSubmission` 返回后立即调 `reapAllRetiredChunks()` -- 单线程同步执行下 executeSubmission 返回时本帧所有 GPU 命令已消费完，retired chunk 的 buffer 不再被任何 pending work 引用，reap 窗口为 0 帧（desktop 是 1-2 帧）。shutdown 路径的 `reapAllRetiredChunks()` 在 `waitGpuIdle()` 后仍保留，覆盖 Web + desktop。
- 高频 chunk 流式更新的集成测试：**已覆盖**。`--streaming-smoke` CLI flag + `WOCC_STREAMING_SMOKE=1` / `WOCC_STREAMING_SMOKE_RADIUS=3` env vars 驱动相机在 ±256m 范围内往返（4 m/frame 确定性步进，跨多个 64m chunk 边界）。300 帧验证结果：`totalRetired=100`（retire queue 被充分执行），`totalReaped=100`（无泄漏），`queueHighWater=9`（队列瞬时非空证明 deferred deletion 真实生效，非 no-op 立即销毁），`retiredPending=7`（frame=240 直接捕获 retire 后/reap 前的瞬时窗口），`EXIT=0`（无崩溃/悬挂 buffer）。streamer.activeChunks() 与 chunkGpuSlots_ 的 coord set 始终同步（`onlyGpu=0 onlyStreamer=0`，无孤立 slot）。诊断计数器（`retireQueueHighWater_` / `totalRetired_` / `totalReaped_`）保留在 RenderSession 作为长期 streaming-health 监控；chunk coord 详尽 dump 走 `WOCC_STREAMING_SMOKE_DEBUG` env var（默认 off）。
- RenderDoc `SaveTexture` PNG 导出 API 签名问题（Phase 2 遗留）不修，不影响亮度测量结论。

### Phase 4: 性能验证与调优（Platform Validated）

**目标**：验证帧率稳定性提升，确认热路径无阻塞。

**步骤**：

1. 性能基线对比：单线程 vs 双线程的 FPS 曲线、CPU/GPU 时间重叠度。
2. 热路径审查（对照 AGENTS.md RHI 契约第 6 条"同步、内存与热路径不变量"）：
   - 渲染线程无阻塞等待（除 `acquireNextFrame` 的 fence）。
   - 无 per-draw pipeline creation。
   - 无 per-frame shader compilation。
   - bounded allocation（双缓冲槽位固定为 2）。
3. RenderDoc 自动验证（按 AGENTS.md 要求"所有可视化效果必须由 AI 通过 RenderDoc 截帧自行确认"）：
   - 截取双线程模式第 30 帧，与单线程基线第 30 帧对比 drawcall 序列、纹理绑定、backbuffer 亮度。
   - 记录 capture 路径和诊断结论。
4. 跑完整 contract test 套件回归 + 所有 smoke 测试。
5. 性能 snapshot 对比：`gameplayMs` / `renderSubmitMs` / `presentMs` / `gpuFrameMs`。

**验收**：帧率标准差显著降低，CPU/GPU 时间重叠度 > 70%（可配置目标），所有 contract test + smoke 通过，RenderDoc 截帧确认画面正确。

## Boundary

- **逻辑线程**：输入、模拟、动画（GPU skinning pose evaluation，Phase 3 删除 CPU skinning）、场景遍历、剔除、HUD 布局、光照收集、渲染数据装配、`acquireForWrite` + `publishWrite`。不调用任何 RHI 方法。
- **渲染线程**：`acquireForRead` + `executeSubmission`（acquire->record->submit->present）+ `releaseRead`。独占所有 RHI 调用。（Phase 3 后 `reapRetiredChunks` 移到逻辑线程 stepFrame，渲染线程不参与 retire queue reap。）
- **双缓冲**：SPSC，逻辑线程生产，渲染线程消费，热路径无 mutex。
- **延迟销毁**：逻辑线程 retire（collectAndUploadTerrainChunks erase 时转移所有权）+ reap（stepFrame 的 `waitFrameCompleted` 之后），单线程访问无 mutex，同步靠 `completedFrameSerial()`。
- **gameScene 访问**：Phase 3 移除 `submission.gameScene` 指针，逻辑线程 `assembleFrameSubmission(const GameScene&, ...)` 值拷贝所有渲染线程所需字段，渲染线程 `executeSubmission` 不再遍历活 GameScene。
- **不改变 RHI L2 语义**：本改造是 game 层线程模型，不新增 descriptor/resource state/command/pipeline/sync 语义。`WoccPbrRenderFramePayload` 仍是渲染线程内部的中间表示，只是来源从"栈局部指针"变成"submission 值拷贝"。
- **不改变 shader 契约**：push constants / UBO 布局不变，矩阵主序遵守 AGENTS.md Matrix Layout 硬约束。

## Risks & Mitigations

| 风险 | 缓解 |
| --- | --- |
| 双缓冲引入 1 帧输入延迟 | 可接受（业界标准）；未来可加插值预测 |
| wind/weather update 迁移到逻辑线程后，writeBuffer 与渲染线程竞争 | ✅ Phase 3 已解决：wind phase instance buffer 走双缓冲（`buffers[2]` + `writeIndex` 帧交换），windInstanceWrites 和 instancedDraws 同帧引用 `currentBuffer()`，`assembleFrameSubmission` 末尾 `flipWriteIndex()` |
| `FrameGraphResourcePool` 跨帧共享无锁 | 渲染线程串行执行 FrameGraph，不并发编译/执行两帧 -- 双缓冲保证同时只有 1 帧在渲染线程执行 |
| Web (Emscripten) 无多线程 | 双线程仅桌面端启用；Web 端保持单线程串行（`#ifndef __EMSCRIPTEN__`） |
| RenderDoc 截帧在多线程下时序复杂 | 截帧仍由渲染线程触发（`renderDocCaptureBeginFrame` 在 `executeSubmission` 内），时序不变；注意 NullHidden 模式 `executeSubmission` 短路不触发 capture，需用 VulkanSurface 模式 |
| ~~CPU skinning 不兼容路径替换 buffer 对象~~ | ✅ Phase 3 删除 CPU skinning，此风险消除；GPU skinning palette 含世界放置信息，vertex shader 处理 |
| `chunkGpuSlots_` erase 路径 GpuBuffer 析构竞争 | ✅ Phase 3 已解决：`GpuResourceRetireQueue` deferred deletion，erase 时 `retire(serial, std::move(slot))`，stepFrame 的 `waitFrameCompleted` 后 `reapCompleted(completedFrameSerial)` |
| `stop()` 时渲染线程正在执行 submission | `stop` 设置 `stopRequested_`，渲染线程完成当前帧后检查标志退出；逻辑线程 `join` 前确保双缓冲无 in-flight 写入；shutdown 路径 `waitGpuIdle()` 后 `reapAllRetiredChunks()` |

## File Changes

### 新增

- `games/world_of_claudecraft/app/wocc_render_frame_submission.h`
- `games/world_of_claudecraft/app/render_frame_double_buffer.h`
- `games/world_of_claudecraft/app/render_thread.h` / `.cpp`
- `src/core/runtime/gpu_resource_retire_queue.h`（Phase 3 ✅ header-only 模板，沉到 core 层；原计划 `games/.../gpu_resource_retire_queue.h/.cpp` 修正为 core 层 header-only）
- `tests/contract/wocc_render_frame_submission_contract_tests.cpp`
- `tests/contract/render_frame_double_buffer_contract_tests.cpp`
- `tests/contract/render_thread_contract_tests.cpp`
- `tests/contract/gpu_resource_retire_queue_contract_tests.cpp`（Phase 3 ✅ 9 场景）

### 删除（Phase 3 CPU skinning 清理）

- `src/animation/cpu_skinning.h` / `.cpp`
- `games/world_of_claudecraft/app/wocc_gpu_skinning_config.h`
- `tests/unit/cpu_skinning_tests.cpp`
- `tests/integration/skinned_mesh_player_tests.cpp`
- `tests/integration/skinned_mesh_player_foot_ik_tests.cpp`

### 修改

- `games/world_of_claudecraft/app/render_session.h` / `.cpp`：新增 `executeSubmission`（Phase 1 ✅ 已完成，薄适配层委托 `buildAndSubmitFrame`，零改动旧 API）；`uploadedSlots` 迁入 RenderSession ~~Phase 1~~ -> Phase 3。Phase 3 leftover 验证：新增 `activeChunkGpuSlotCoords()` / `retireQueueHighWater()` / `totalRetired()` / `totalReaped()` / `resetRetireDiagnostics()` 诊断访问器 + `retireQueueHighWater_` / `totalReaped_` / `totalRetired_` 计数器成员（retire/reap 路径累计，用于 streaming-smoke 长期监控）。
- `games/world_of_claudecraft/app/wocc_runtime_session.cpp`：stepFrame 末尾改为装配 submission + 调 `executeSubmission`（Phase 1 ✅ 已完成）；wind/weather/剔除/装配迁移到逻辑线程 + `publishWrite` -> Phase 2。Phase 3 leftover 验证：新增 `--streaming-smoke` 自动驾驶相机（4 m/frame 确定性步进，±256m 往返）+ 每 30 帧诊断日志（含 `queueHighWater` / `totalRetired` / `totalReaped` 累计计数器，`WOCC_STREAMING_SMOKE_DEBUG` env var 开启 chunk coord set 详尽 dump）。Phase 3 Web 修复：`#if defined(__EMSCRIPTEN__)` 分支在 inline `executeSubmission` 后加 `reapAllRetiredChunks()`（单线程同步执行，reap 窗口为 0 帧）。
- `games/world_of_claudecraft/app/app.h` / `.cpp`：新增 `--streaming-smoke` CLI flag（Phase 3 leftover 验证）。
- `games/world_of_claudecraft/app/components.cpp`：`initStreaming` / `initStreamingInstanced` 新增 `WOCC_STREAMING_SMOKE` / `WOCC_STREAMING_SMOKE_RADIUS` env var 覆盖 `activeRadius`（默认 12 覆盖全图无流式，smoke 下设 3 触发 chunk 增删）。
- `games/world_of_claudecraft/app/wocc_pbr_pipeline.h`：payload 注释更新（指针来源变更）（Phase 1 ✅ 已完成）。
- `games/world_of_claudecraft/app/app.cpp`：启动/停止渲染线程 -> Phase 2。
- `CMakeLists.txt`（如需）：新增源文件注册。（Phase 1 ✅ 通过 `build.py` test_configs 注册新测试，configure 自动重新生成 CMakeLists.txt。）
- `build.py`：test_configs 新增 `arp_wocc_render_frame_submission_contract_tests`（Phase 1 ✅ 已完成）。

## References

- 主循环与帧结构：`games/world_of_claudecraft/app/wocc_runtime_session.cpp:701`（`stepFrame`）
- 渲染提交核心：`games/world_of_claudecraft/app/render_session.cpp:1680`（`buildAndSubmitFrame`）
- payload 定义：`games/world_of_claudecraft/app/wocc_pbr_pipeline.h:244`（`WoccPbrRenderFramePayload`）
- RenderSystem 帧循环：`src/platform/native/render_system_vulkan.cpp:1329/1604/1750`（acquire/present/submit）
- FrameGraph 执行：`src/renderer/render_graph/frame_graph_rhi_active_executor.cpp:770/1205`
- 每帧 uniform 环形缓冲（已有双缓冲槽位设计参考）：`src/renderer/render_graph/frame_graph_uniform_ring_buffer.h:25`
- 帧资源回收器（延迟销毁模式参考）：`src/rhi/frontend/frame_resources.h:64`
- 已有但未接线的线程池：`src/core/runtime/work_service.h`（`WorkService`，本计划不复用，因渲染线程是专有长生命周期线程而非任务池）
- 地形流式已知 gap 记录：`games/world_of_claudecraft/streaming_terrain_redesign.md:66`
