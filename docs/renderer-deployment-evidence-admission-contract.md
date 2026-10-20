# RendererDeploymentEvidenceAdmission/1

日期：2026-10-16。状态：Contract Complete（仅本旧报告拒绝契约）。对应 WebGL P0-01/04/08/09 的
renderer deployment 报告与消费者审查；不关闭整个 P0。

## L2 边界及不变量

现有 NativeBackendCapabilityContract、NativeRendererDeploymentReadinessReport、
WebGpuRuntimeTelemetryReport、RendererBackendDeploymentPlan 和 frame lifecycle
plan/telemetry 都是调用方可写的值。queriedFromNativeDevice、queriedExtendedFeatures、
profileStatus、success、canStartFrame、backendValidated 或 platformValidated 不携带
device/context/generation、实际 workload/payload 或完成证据，不能授权 GPU 工作。

既有结构需求仍由 renderer fallback/frame planner 分析；本契约拒绝将这些结构结果
升级成 Backend Validated、Platform Validated 或 frame-start permission。不为旧报告
添加可写的 authenticated 布尔值、magic token 或 backend 名称白名单来认证执行。

各入口须同步遵守：

1. Native deployment report 不从两个 queried flags 产生 readiness：success、
   contractComplete、backendValidated、canStartFrame 均为 false，status 为
   DiagnosticFailure，保留结构计数和诊断但明确它们不是执行计数。
   默认构造的 Native/WebGPU 报告也使用 DiagnosticFailure；未选择的 candidate index
   默认是 UINT32_MAX。默认值不构成任何 backend 的能力声明。
2. WebGPU deployment report 不从可写 telemetry/profileStatus 声明 platform ready；
   readiness 四个布尔值同样关闭，保留 maintenance intent 与原诊断。
3. 候选 selector 先验证 preference 与候选容量，不对不受认证报告打分。每个候选产生
   稳定拒绝原因；selectedCandidateIndex 为 UINT32_MAX，selectedStatus 为
   DiagnosticFailure，success 为 false。不得把 WebGL/unknown 当作非 WebGPU native。
4. frame-start gate 及 lifecycle telemetry collector/readiness gate 都独立拒绝
   直接伪造的 aggregate，不只依赖上游 producer 已经返回 false。
5. planner/bridge 的组合结果沿同一失败传播；没有 acquire/record/submit/present
   许可。保留 surface/transient release、resize 和 budget refresh 的诊断 intent，
   不把它们算作已执行维护。

本契约只适用于这些旧值入口。真实 GpuDevice、queue 和资源执行仍由既有 frontend/
backend contract 验证；Null hidden-frame 走实际 Null validator，不用本报告代替它。
不新增 Native payload 或 CPUReplay。保留公开类型成员、enum ordinal 和函数签名；
owned diagnostics/container 明确使用 core::pmr/core CPU resource。

## 五 backend lowering 与诊断

复用 SemanticReportBackendAdmission/1 的身份集合；该表只识别结构身份，不提供
capability。本次全部旧入口均为 DiagnosticFailure：

| backend | 理由与稳定 diagnostic |
| --- | --- |
| Null | validator-only，`RHI-RENDERER-DEPLOYMENT-NULL-VALIDATOR-ONLY` |
| Vulkan | native report/selection/frame-start/lifecycle 的可写声明不认证执行 |
| Metal | 同上；不把此 Windows CPU 测试作为 Apple-native 证据 |
| WebGPU | 同上；profileStatus 也不能认证 browser/context/workload |
| WebGL | 生产 deployment 尚未实现，`RHI-WEBGL1-RENDERER-DEPLOYMENT-UNAVAILABLE` |

未知 backend：`RHI-RENDERER-DEPLOYMENT-BACKEND-UNKNOWN`。
未知 contract version：`RHI-RENDERER-DEPLOYMENT-CONTRACT-VERSION`。
未知 evidence stage：`RHI-RENDERER-DEPLOYMENT-STAGE-UNKNOWN`。
未知 preference：`RHI-RENDERER-DEPLOYMENT-PREFERENCE-UNKNOWN`，必须在遍历候选前失败。
已知 preference 不再按 backend 名称、score 或 backendValidated 字段猜测可执行性。
候选容量失败：`RHI-RENDERER-DEPLOYMENT-CANDIDATE-CAPACITY`；数量无法用既有uint32
报告表示时为 `RHI-RENDERER-DEPLOYMENT-CANDIDATE-COUNT-UNREPRESENTABLE`，不进行截断，
失败报告的candidateCount置0且必须携带该诊断。count helper通过只代表数值有效。
无已认证候选：`RHI-RENDERER-DEPLOYMENT-NO-AUTHENTICATED-CANDIDATE`，保留原解释文本。

Vulkan/Metal/WebGPU 各阶段拒绝 ID 分别按 stage 固定为：

- NativeReport：`RHI-RENDERER-NATIVE-REPORT-EVIDENCE-UNAUTHENTICATED`。
- WebGpuReport：`RHI-RENDERER-WEBGPU-REPORT-EVIDENCE-UNAUTHENTICATED`；已知非
  WebGPU native 身份为 `RHI-RENDERER-WEBGPU-REPORT-BACKEND-MISMATCH`。
- CandidateSelection：`RHI-RENDERER-CANDIDATE-EVIDENCE-UNAUTHENTICATED`。
- FrameStart：`RHI-RENDERER-FRAME-START-EVIDENCE-UNAUTHENTICATED`。
- LifecycleCollection：`RHI-RENDERER-LIFECYCLE-PLAN-EVIDENCE-UNAUTHENTICATED`。
- LifecycleReadiness：`RHI-RENDERER-LIFECYCLE-TELEMETRY-EVIDENCE-UNAUTHENTICATED`。

准入 helper 为无分配、无 native 副作用的版本化决定。报告构造可以为 diagnostics
分配 core 内存，不宣称整个旧报告热路径无分配或完整字节预算已通过。

## 恢复与后续实现

修改 flags、重试或改选其它 backend 都不能恢复这些缺失的证据。P4 必须建立与真实
device/context generation、workload/artifact/profile、提交 payload 及实际输出关联
的受控执行入口，独立验证后才能打开其权限；不能重新启用旧可写报告作为授权。
该真实执行入口仍是 P4 必交付物，当前拒绝不是完成 P2/P3/P4 的替代方案。

## 测试与 smoke

先用真实旧入口复现未经认证报告被接受，再实现门禁。快速测试须覆盖五 backend/
unknown、各 evidence stage/版本、未知 preference、伪造 queried/profile/status/
success/canStart 字段、容量、直接 aggregate 注入，以及 planner/bridge 失败传播。
保留结构诊断与维护 intent，拒绝后不能出现成功 readiness/selected index。
每批运行 profile/fallback 与 Null hidden-frame；未修改 shader 或可视输出，不新增
RenderDoc/Backend Validated/Platform Validated/Production Ready 声明。

## 当前证据与范围

`build/webgl1-evidence/p0/deployment-evidence/red/`保留修复前八个真实入口的断言失败：
native/WebGPU report、未知preference、WebGL candidate、frame-start、collector、
readiness和完整bridge。没有mock native成功；这些是对可写输入的负向测试。

最终MinGW构建/4项CTest及31项列名输入、四binary hash见validation索引。快速测试还
编译实际`src/platform/render_system_frame_loop.cpp`，验证四类frame-work许可全关闭；
WebGPU profile经过真实candidate builder/map后也保持拒绝。Null hidden-frame是独立
回归，不是上述旧报告的执行证明。整个P0仍12/19，生产控制入口仍未完成。

`abi/`使用同一MinGW、相同当前传递依赖，分别编译Git HEAD的四份旧头声明与当前声明，
从独立COFF数据section提取31种类型的141项size/alignment/关键offset/enum值；全部
相等。八项列名输入一致。它只证明这四份声明在该编译环境下的所列布局值，不覆盖
所有公共ABI、其它编译器/平台、跨DLL返回或默认状态的语义兼容。

旧结构planner仍可分析需求；capability apply、完整CPU字节预算、所有其它序列化/
switch及completion消费者仍需各自审查。本契约不将这些未完成项标为通过。
