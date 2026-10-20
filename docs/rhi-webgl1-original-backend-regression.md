# P4-15 原后端回归与平台边界

本包关闭 P4-15。以下为本包验收时的历史计数；后续 P4-19 产品性能已验收，当前主进度见 `rhi-webgl1-product-performance.md`。当时 P0 19/19、P1 11/11、P2 13/13、P3 11/11、P4 17/20，总计 71/74。原任务 P4-14、P4-19、P4-20 继续开放。修复范围为测试链接、fixture 直接依赖、嵌入 JavaScript 保护和回归断言；没有修改生产 RHI capability、lowering 或执行门禁。

## 验证结果

| 路径 | 结果 | 证据边界 |
|---|---|---|
| MinGW Debug / Release | 各 4/4，无 skip | Vulkan、Metal、WebGPU device contract 与 Null hidden-frame；Metal 是 Windows host/stub contract |
| Vulkan Release 原生 smoke | 4/4，无 skip | clear、sampled、四 draw ping-pong、storage → compute → sampled → MRT；真实原 device/queue 执行 |
| WebGPU Release / Chrome 155 | 最终定向 3/3 | uniform static 数值读回、uniform static caller-drop、readonly-storage dynamic caller-drop；真实 queue completion、copy/map 和原 callback drain |
| 快速源码与嵌入 JS 检查 | 7/7 | 识别 EM_ASYNC_JS，Node 拒绝破坏后的箭头和严格相等运算符；仓库级 formatter boundary 检查 |
| 报告校验器复放 | 8 份正向，32 个负向拒绝 | 6 份原数值报告与 2 份最终 drop 报告；错误像素、缺 completion、重复内容、错误内容失效声明、假发布及 native error；只属工具验证 |

WebGPU 最终三用例使用同一 `accepted-bundle/`。2,976 项命名源码输入在最终构建前后、浏览器执行后完全一致；JS/Wasm/data/HTML/report JS/progress transport 各自保留 SHA-256。正常用例逐通道验证真实 RGBA16F；drop 用例不发布像素，验证原 wrapper 已销毁、native callback 排空后 command/storage 释放、quota 归零且禁止重试。所有公开 execution/readiness、NativeSync、platform、production 字段保持 false。

证据索引：`build/webgl1-evidence/p4/p4c-original-backend-regression-checkpoint.yaml`。最终浏览器结果、独立冻结包、命名输入和日志在 `p4c-webgpu-release-1/`；host 回归日志为 `p4c-original-backend-{debug,release}-ctest-accepted.log`；Vulkan 为 `p4c-original-vulkan-smoke-ctest.log`。

## 修复与保留的失败

共享 fixture 实例化 WebGPU native-loss callback，因此 Vulkan/Metal **测试 executable** 显式链接 `arp_rhi_webgpu`；生产库依赖没有改变。直接使用 `WebGpuAsyncWaitFixture` 的 fixture 增加直接 include。旧 formatter 已破坏的两处 EM_ASYNC_JS 恢复正确 JS 运算符并增加 off/on；共享扫描器同步识别该宏。额外 WGSL minimum fixture 在本包验证到 Node 语法层，不冒称其独立 GPU 场景已重新运行。

取消只读 readback 的契约区分三件事：上游已完成 draw 的原始身份仍有效；下游取消使当前资源状态失效，因此完成/再次读回拒绝；只读源的已完成内容仍保留。真实 device loss 同时使身份、内容与状态失效。fixture 和报告校验器按这些既有生产语义修复，不通过改变生产行为迎合断言。最终 drop 实测诊断是原 transaction 的 `RHI-ORIGINAL-PLAN-FINAL-MISMATCH`；断言同时核对诊断来源和 committed state 不存在。

首轮八用例 **6/8**，两个 drop 在原 fixture 的身份断言处失败。后续保留了固定 snapshot 错误码断言失败、旧内容失效断言失败的独立报告及 runtime bundle；最终修复后定向 **3/3**。不同 cohort 不合算成“最终包完整八用例 8/8”。Release 链接失败、缺宿主 AOT 工具、Windows 长路径、缺直接 include、JS optimizer 拒绝损坏运算符的原日志也均保留。

## 后端状态与下一整包

Null 只提供 validator；Vulkan 是上述限定 workload 的 Backend Validated；WebGPU 是既有 private Prototype output/queue/lifetime 路径的 Backend Validated；Metal 仍缺 Apple native smoke。WebGL1 沿用此前独立冻结包的浏览器与 RenderDoc 验收，本包不新增画面或 shader 声明。Apple/mobile/小程序 Platform Validated 和整体 Production Ready 继续开放。

P4-14 继续实现同 YAML → 共享 recorder → 原 execution payload → 实际 output → 封闭证据准入。这里的 WebGPU fixture 和外部报告不能签发共享场景认证；旧 descriptor/heartbeat facade、普通 production factory 和 NativeSync 不开放。P4-19 的完整产品性能观测可独立推进，不以最终 readiness 为前提；P4-20 只消费已完成的执行、性能与平台证据。三项保持单向依赖，不让最终审计反过来授权执行。
