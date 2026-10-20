# P4-16 / P4-17 浏览器与产品场景验收

本记录按原任务验收条件关闭 P4-16、P4-17；同一批 storage/compute/MRT 实现在 `68fa126e`。本检查点当时为全计划 68/74、P4 14/20；当前计数以实施计划顶部为准。此结算没有认证旧 descriptor/heartbeat conformance facade，也没有打开 production factory、NativeSync 或平台 readiness。

## P4-16：两种浏览器、基础与扩展配置

当前 root：`build/webgl1-evidence/p4/p4c-storage-root-release-1/20261019T080542Z-3d226f2d/build-manifest.yaml`。8,037 个命名输入构建前后匹配；后续文档差异独立列出。同一四文件 frozen bundle 为 `p4c-storage-browser-release-1/bundle/`，Chrome 与 Firefox 使用其相同 bytes；不是将两次独立构建冒充同一输入。

| 验收项 | 真实结果与边界 |
|---|---|
| 浏览器 | Chrome 155 / ANGLE D3D11；Firefox 158 / WebGL1；各 8 个入口通过 |
| profile | MIN/MAX_WEBGL_VERSION=1，无 WebGL2 fallback；测试中隔离 navigator.gpu |
| 入口 | pipeline、extension_execution、ordered_graphics、scene2d、gui、basic_forward、conformance_clear、conformance_sampled |
| 扩展 | 五种显式 opt-in 原 domain 执行；每浏览器 10 种缺 native/缺 consent 负向；不宣称硬件物理缺失 |
| 生命周期 | 真实 rAF、controlled loss/restore、重查询拒绝、预算拒绝、surface pause/resize 与 CPU 退休 |
| 原始环境 | 各 report 的 runtimeEnvironment；物理 Windows GPU/driver 清单 `p4c-windows-gpu.json` |
| GPU | Windows inventory NVIDIA GeForce RTX 3050，driver 32.0.15.9579；Firefox privacy-masked renderer 不当作实际显卡型号 |
| shader identity | 原 root sourceInputs 的 artifact/Slang/publication SHA-256，冻结 bundle custody；扩展 report 两个原 domain 的 programIdentity |

原始报告在 root 对应 `*-browser/`、`browser/` 和 `p4c-storage-browser-release-1/firefox-*/`。审计复用原 `webgl_pipeline_browser.validate_raf/validate_quality`；读取实际报告，不把通过字段变成 RHI 原始输出签发权。

## P4-17：产品场景与 AI 视觉检查

当前 frozen root 的 Scene2D/GUI/BasicForward 各 6 帧像素回归在两浏览器通过。RenderDoc 证据使用独立先前冻结的 `p4c-release-native-1/bundle/`，哈希由其 `bundle-hashes.yaml` 明确记录；不能说其 JS/Wasm/data 与当前 root 相同。

| 场景 | 捕获 / draw | 主要验收 |
|---|---|---|
| Scene2D | 24 / 117 | 原 atlas、A/B/A、透明、flip、clip、两尺寸完整输出 |
| GUI | 24 / 144 | 原 image/glyph、bitmap font、clip、premultiplied alpha、离屏 source、两尺寸完整输出 |
| BasicForward | 24 / 324 | 非对称相机原顶点、1,080 Post-VS 顶点、depth 遮挡与透明不写深度、fullscreen/UI、原 depth sampled、两尺寸完整输出 |

累计 72 个 capture、585 次 draw、零 replay error。AI 已复查 `build/webgl1-evidence/p4/p4c-release-native-1/verified-contact-sheet.png`：两尺寸 Scene2D 分区/clip、GUI 白 glyph 和 alpha image、基础 3D 的遮挡/透明分区、文字及右下 depth 预览均与自动像素 oracle 一致。动态 buffer/texture 更新、context 恢复前后实际执行由 pipeline 入口和已保存 lifecycle capture 证据补充；WEBGL_lose_context 是受控丢失，未声明真实硬件 reset。

## 可复查证据与仍开放门槛

机器审计：`build/webgl1-evidence/p4/p4c-browser-scene-acceptance-audit.yaml`。核对 32 份原始 JSON 报告、72 个 capture SHA-256、所有 draw 的两尺寸覆盖/非空输出、原 replayErrors、两套独立 bundle custody，以及 AI 已查看的图像 SHA-256。它是工具层审计，不作为 RHI sealed admission issuer。

本检查点状态为 Contract Complete / Prototype Complete / bounded Backend Validated。以下为该检查点当时的开放项；后续 P4-13、15、18 的关闭见当前实施计划。P4-13 的完整入口分类审计、P4-14 的同 YAML 五 backend 原始执行/输出封闭认证、P4-15 真实 WebGPU 回归、P4-18 完整 program/reflection/FBO/depth/blend/front-face 审计、P4-19 产品 Release 性能、P4-20 最终能力/状态审计仍开放。Apple/mobile/小程序 Platform Validated 与 Production Ready 未授予。
