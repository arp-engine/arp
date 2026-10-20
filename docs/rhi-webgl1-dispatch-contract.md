# WebGlBackendDispatch/1

日期：2026-10-17。定义与实施前约束；完成状态以 validation 记录为准。

本批覆盖当前 Backend/BackendPreference 的逐使用点身份分派；不新增 WebGL 执行能力。
原 Null/Vulkan/Metal/WebGPU 的分支、返回类型、版本先后顺序和 payload gate 保留。
Null 仍是 validator；原 native 分支的 Native/Equivalent/DiagnosticFailure 继续取决于其
独立契约与实际资源，不由本表或测试中的值对象认证。

| 域 | WebGL 决定 | 其它四 backend |
| --- | --- | --- |
| 原 command/bindless/pipeline/query/upload/readback 专用路线 | 显式分支退出到原稳定 DiagnosticFailure 结果；不签发 preparation、recording、execution、completion、publication 或 validator 权限 | 保留原逐版本决定；实际发行者与执行 payload 独立验证 |
| 2D executor 与 terrain uploader | 明确拒绝未实现的 WebGL 路线，不以 metadata 或 backend 名授予能力 | 保留原路径；真实集成由 P4 按 capability 改造 |
| 既有 render-target/CPU-raster/UI 坐标 helper | 尚无生产 WebGL 坐标 lowering；抛出 RHI-WEBGL1-COORDINATE-LOWERING-UNAVAILABLE，任何输出修改前失败 | 保留原坐标数值；Null 只做数学验证 |
| 未知身份坐标请求 | 抛出 RHI-COORDINATE-BACKEND-UNKNOWN；不得返回有效 origin/flip | 不适用 |
| editor viewport lowering | 独立 BackendUnsupported 诊断，未知身份同样拒绝；伪造 push 计划不能旁路 | 原 Null validator 与三个 native 决定不变 |

显式退出到原拒绝结果，是保持已有稳定诊断和原公开 Decision layout 的拒绝实现，不是新的
fallback。它不得 fall-through 到任何 native case；模板 Result 必须使用真实 consumer 类型
验证其默认值不授予权限。未审查的类型或默认结果不能据此宣布完成。

历史 `OriginalCommandDescriptorSourceLowering::Decision` 是未被执行consumer使用的四字段
metadata projection，仅有failure mode而没有diagnostic字段；本批保持其布局，测试明确
限定这一真实类型例外。实际finite/general source使用有诊断的独立Decision，不接受这份
metadata作为执行凭据。其余本批测试路线必须携带非空diagnostic或diagnosticId。

CPU profile/descriptor 兼容不构成执行身份。Backend、BackendPreference、shader TargetBackend、
历史四键 artifact schema 各自独立；禁止 ordinal 互 cast。旧值不得移动，新增身份不隐式
扩展冻结 schema 或旧公开数组；需要五项的调用者使用明确版本化的新入口。

验证需同时保留：全部 switch 定位清单（不是批准清单）、真实 decision 的 WebGL/unknown/
错版本负向、原四 backend 结果比较、禁止 native 副作用与坐标输出不变检查，以及 Null
hidden-frame。无新增渲染输出，不以本批 CPU 测试替代 P1/P2 的坐标数值或 RenderDoc 验收。
当前未完成原生路径保持拒绝，不能把这一审计重新依赖于未来 P3/P4 的完整实现。
