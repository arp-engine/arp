# Push Constant 调用点审计 — Track A P0 Entry Checkpoint

> 日期：2026-07-12
> 状态：Checked-in audit fixture（Track A P0 Entry checkpoint）
> 范围：src/, games/, tools/, tests/, shaders/

## 1. 生产 push 调用点（24 个，全部 atomic full-block）

所有生产调用点使用 `offset=0` + `sizeof(StructType)` 整块写入模式，符合 L2 `AtomicFullBlockPush` profile。

### Renderer 核心（9 个）

| # | 文件:行 | size 来源 | stages | 关联 draw/dispatch | 管线布局 range |
|---|---------|-----------|--------|-------------------|---------------|
| 1 | `frame_graph_rhi_active_executor.cpp:605` | `pass.pushConstantSizeBytes` | V\|F | `drawIndexed(3,1,0,0,0)` | `{V\|F, 0, pass.pushConstantSizeBytes}` |
| 2 | `frame_graph_rhi_active_executor.cpp:656` | `pass.pushConstantSizeBytes` | Compute | `dispatch(1,1,1)` | `{Compute, 0, pass.pushConstantSizeBytes}` |
| 3 | `rhi_production_frame.cpp:578` | `min(pushConstantSizeBytes, kSceneRuntimeDemoPushConstantBytes)` | V\|F | `drawIndexed(...)` | `{V\|F, 0, pushConstantBytes}` |
| 4 | `gui_rhi_draw_commands.h:256` | `sizeof(GuiPushConstants)` | V\|F | `drawIndexed(kGuiQuadIndexCount,...)` | `{V\|F, 0, sizeof(GuiPushConstants)}` |
| 5 | `particle_rhi_draw_commands.h:106` | `sizeof(ParticlePushConstants)` | V\|F | `draw(...)` | `{V\|F, 0, sizeof(ParticlePushConstants)}` |
| 6 | `particle_renderer.h:1267` | `sizeof(TrailBeamPushConstants)` | V\|F | `draw(...)` | `{V\|F, 0, sizeof(MeshParticlePushConstants)}` |
| 7 | `particle_renderer.h:1418` | `sizeof(MeshParticlePushConstants)` | V\|F | `drawIndexed(...)` | `{V\|F, 0, sizeof(MeshParticlePushConstants)}` |
| 8 | `fxaa_rhi_pass.h:94` | `sizeof(FxaaPushConstants)` | Fragment | `draw(3,1,0,0)` | `{Fragment, 0, sizeof(FxaaPushConstants)}` |
| 9 | `ssao_rhi_pass.h:106` | `sizeof(SsaoPushConstants)` | Fragment | `draw(3,1,0,0)` | `{Fragment, 0, sizeof(SsaoPushConstants)}` |

### Game WoCC（15 个）

| # | 文件:行 | size 来源 | stages | 关联 draw/dispatch | 管线布局 range |
|---|---------|-----------|--------|-------------------|---------------|
| 10 | `wocc_pbr_pipeline.cpp:2053` | `sizeof(WoccShadowDepthPushConstants)` | V\|F | `drawIndexed(...)` | `{V\|F, 0, sizeof(WoccShadowDepthPushConstants)}` |
| 11 | `wocc_pbr_pipeline.cpp:2130` | `sizeof(WoccSkyPushConstants)` | V\|F | `drawIndexed(...)` | `{V\|F, 0, sizeof(WoccSkyPushConstants)}` |
| 12 | `wocc_pbr_pipeline.cpp:2216` | `sizeof(WoccPbrPushConstants)` | V\|F | `drawIndexed(...)` | `{V\|F, 0, sizeof(WoccPbrPushConstants)}` |
| 13 | `wocc_pbr_pipeline.cpp:2249` | `sizeof(WoccPbrPushConstants)` | V\|F | `drawIndexed(...)` | `{V\|F, 0, sizeof(WoccPbrPushConstants)}` |
| 14-22 | `wocc_pbr_pipeline.cpp:2718-2991` | `sizeof(WoccPostPushConstants)` / `sizeof(WoccPostFxPushConstants)` | V\|F | `draw(3,1,0,0)` | post/postFx ranges |
| 23 | `ddgi_renderer.h:395` | `sizeof(WoccDdgiPushConstants)` | Compute | `dispatch(...)` | `{Compute, 0, sizeof(WoccDdgiPushConstants)}` |
| 24 | `ddgi_renderer.h:462` | `sizeof(WoccDdgiPushConstants)` | Compute | `dispatch(...)` | 同上 |

## 2. 四后端实现状态

| Backend | 文件 | 实现状态 | 问题 |
|---------|------|---------|------|
| Vulkan | `vulkan_native_graphics_commands.cpp:827-869` | **Native** — `vkCmdPushConstants` | 无 |
| Metal | `metal_native_commands.mm:79-86` | **Stat-only stub** — 仅设 flag + 累加 bytes | **P0 关键问题：无 `setVertexBytes`/`setFragmentBytes`，虚假 success** |
| WebGPU | `webgpu_native_commands.cpp:574-615` | **B1 Emulation** — per-push bind group + uniform ring | ring-size 固定猜测值；无 B2 explicit layout |
| Null | `null_command_validation.cpp:155-189` | **Validation only** — 纯校验 | partial 覆盖被允许（P1.3 需收紧） |

## 3. Shader 声明（16 个 Slang push constant 块）

| Shader | 结构体 | 反射 size |
|--------|--------|-----------|
| `shaders/samples/clear.slang` | `SceneRuntimeDemoPushConstants` | 16 |
| `shaders/gui/overlay.slang` | `GuiPushConstants` | 64 |
| `pbr_scene.slang` | `WoccPbrPushConstants` | 256 |
| `pbr_scene_terrain.slang` | `WoccPbrPushConstants` | 256 |
| `ddgi.slang` | `WoccDdgiPushConstants` | 160 |
| `depth_prepass.slang` | `WoccDepthPrepassPushConstants` | 256 |
| `fxaa.slang` | `FxaaPushConstants` | 24 |
| `ssao.slang` | `SsaoPushConstants` | 32 |
| `particles.slang` | `ParticlePushConstants` | 100 |
| `mesh_particle.slang` | `MeshParticlePushConstants` | 80 |
| `trail_beam.slang` | `TrailBeamPushConstants` | 80 |
| `shadow_depth.slang` | `ShadowDepthPushConstants` | 80 |
| `sky.slang` | `WoccSkyPushConstants` | 144 |
| `post.slang` | `WoccPostPushConstants` | 112 |
| `post_fx.slang` | `WoccPostFxPushConstants` | 112 |
| `postprocess.slang` | (push_constant marker) | 80 |

## 4. 反射/校验代码

- `shader_compiler.cpp:145-179` — `reflectedPushConstantSize()`：**已知缺陷：不跳过 `//` 注释**
- `shader_pipeline_reflection_validator.cpp:11-18` — 管线反射 size 匹配校验
- `gpu_descriptors.cpp:118-136` — 管线布局 range 创建时校验
- `null_command_validation.cpp:155-189` — draw 时运行时校验（允许 partial 覆盖，需收紧）
- `rhi_frame_plan.cpp:52` — 256 字节硬上限强制执行

## 5. P0 待修问题汇总

1. **P0.1**：`scripts/compile_shaders.py:721` — `prefix == "pbr_scene"` 漏 `pbr_scene_terrain` — **已修复**
2. **P0.2**：`native_backend_capabilities.cpp:58-65` — L2 limit 与 native limit 未分离 — **已修复**
3. **P0.3**：Metal `recordPushConstants` 是 stat-only stub，报告 success 但无 executable payload — **已标记 PrototypeLegacy**
4. **P0.4**：WebGPU B1 ring-size 固定猜测值，无 B2 policy；per-push bind group 是 B1 路径 — **已文档化 policy**
5. **P0.5**：Null `validatePushConstantBinding` 允许 partial 覆盖（range.sizeBytes 可大于 push size）

## 6. P0 修复记录

### P0.1 — pbr_scene_terrain codegen 修复
- `scripts/compile_shaders.py:721`: `prefix == "pbr_scene"` → `prefix in ("pbr_scene", "pbr_scene_terrain")`
- 新增 `validate_wgsl_reserved_group()` 碰撞检测器，在所有 WGSL 后处理后确定性失败
- 重新生成 pbr_scene_terrain.wgsl（gLights 从 group(3) 移到 group(0)）

### P0.2 — L2/native limit 分离
- `gpu_common.h`: 新增 `kL2AtomicPushConstantLimitBytes = 256`（L2 contract ceiling）
- `native_backend_capabilities.cpp`: 为每个 backend 填充 `maxPushConstantSizeBytes`
  - Vulkan: 256（fallback，运行时由 device query 覆盖）
  - Metal: 4096（setBytes limit）
  - WebGPU: 256（uniform buffer lowering limit）
  - Null: 256（reference）
- `rhi_frame_plan.cpp`: magic `256u` → `rhi::kL2AtomicPushConstantLimitBytes`
- `frame_graph_rhi_active_executor.cpp`: stack buffer `std::array<uint8_t, 256>` → 命名常量
- `rhi_production_frame.cpp`: 同上
- `metal_backend_objects.cpp`: `> 256u` → `> rhi::kL2AtomicPushConstantLimitBytes`

### P0.3 — 虚假 success 路径 + WebGPU policy
- Metal `recordPushConstants`（.mm + stub）: 标记为 PrototypeLegacy，添加注释说明无 executable payload
- Metal stub（非 Apple）: 修复 `sizeBytes == 0` 不返回 false 的 bug
- WebGPU `recordPushConstants`: 文档化 B1 lowering policy（per-push bind group）
- WebGPU ring capacity: 文档化 1MB 默认值的推导依据（256B × 4096 commands）
- WebGPU alignment: 文档化 256 来自 WebGPU spec minimum，B2 时应从 device query 获取

### WebGPU B1/B2 Policy 决策
- **当前路径**: B1（per-push bind group + uniform ring）— EquivalentLowering
- **B2 目标**: explicit layout + dynamic offset，消除 per-push bind group creation
- **Ring capacity**: 1MB（256B L2 ceiling × 4096 per-frame push commands）
- **Alignment**: 256（WebGPU spec minimum minUniformBufferOffsetAlignment）
- **B2 实施条件**: P3-A（B1 baseline）完成后，P3-B 独立推进
