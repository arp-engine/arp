# WebGl1TransferAdmission/1

日期：2026-10-16。状态：Contract Complete（仅本契约的transfer admission范围）。范围为P0-07/08/09/10的transfer入口；
不增加74项计划任务，不将拒绝路径当生产WebGL实现。

## L2语义及不变量

request/layout/statistics/binding/coverage等CPU元数据不证明GPU copy、map、publication
或completion。客户端上传源已消费只允许已声明的host custody回收，不能发布完成资源。
当前WebGl1Graphics/1没有生产readback/native completion路径；所有本契约入口对WebGL
给出明确DiagnosticFailure，无CPU replay、虚假map或empty payload成功。

沿用现有版本与校验优先级：错误version及缺少显式opt-in先按原contract拒绝，再进行
backend准入。有效版本的WebGL拒绝保留请求版本（仅已有decision有version字段时）；
不能被误报为unknown backend。未知枚举继续使用既有unknown/invalid诊断。
拒绝不改写调用方output decision，不调用native/context callback，不分配运行时内存。

## 五backend决定

| backend | 现有语义及本次决定 |
| --- | --- |
| Null | 保留逐操作validator/metadata决定；所有nativeExecution字段false，不证明completion |
| Vulkan | 保留已选版本的CPU metadata及原有明确拒绝；buffer binding/3保留pending关联；不扩张native执行 |
| Metal | 保留CPU metadata；borrowed readback仅现有显式bounded Legacy flag可用；不提升为基础能力 |
| WebGPU | 保留现有metadata与显式Prototype门禁；owned context resolution仍须原facts校验；不以结果字段认证native来源 |
| WebGL | 以下逐项稳定拒绝；全部metadata/validator/native/execution权限false，禁止使用WebGPU或Legacy路径 |

逐操作矩阵如下。`Equivalent metadata`仅说明已有CPU描述关系，nativeExecution仍false；
`Validator`只用于Null；`DiagnosticFailure`禁止执行。`Prototype(context)`要求现有
五项context facts全部满足；`Legacy(budget)`要求已有显式bounded预算，不是基础能力。
这两项保持现有选定路径及其独立contract，不能据此认证native来源。

| 操作 | Null | Vulkan | Metal | WebGPU | WebGL |
| --- | --- | --- | --- | --- | --- |
| buffer binding/1,2 | Validator metadata | Equivalent metadata | Equivalent metadata | Equivalent metadata | DiagnosticFailure |
| buffer binding/3 | Validator pending metadata | Equivalent pending metadata | DiagnosticFailure | DiagnosticFailure | DiagnosticFailure |
| buffer request/1 | Validator metadata | Equivalent metadata | Equivalent metadata | Equivalent metadata | DiagnosticFailure |
| source lease/1,2 | Validator metadata | Equivalent metadata | Equivalent metadata | Equivalent metadata | DiagnosticFailure |
| input snapshot | Validator metadata | Equivalent metadata | Equivalent metadata | Equivalent metadata | DiagnosticFailure |
| borrowed retirement | DiagnosticFailure | DiagnosticFailure(retired) | Legacy(budget) | DiagnosticFailure(retired) | DiagnosticFailure |
| cold budget | Validator metadata | Equivalent metadata | Equivalent metadata | Equivalent metadata | DiagnosticFailure |
| content intent | Validator | DiagnosticFailure | DiagnosticFailure | Prototype(opt-in) | DiagnosticFailure |
| content coverage | Equivalent metadata | Equivalent metadata | Equivalent metadata | Equivalent metadata | DiagnosticFailure |
| statistics plan | Validator metadata | Equivalent metadata | Equivalent metadata | Equivalent metadata | DiagnosticFailure |
| owned preparation | Validator | DiagnosticFailure | DiagnosticFailure | Prototype(context) | DiagnosticFailure |
| packed copy mapping | Equivalent metadata | Equivalent metadata | Equivalent metadata | Equivalent metadata | DiagnosticFailure |
| upload generation | Validator/owner unproven | DiagnosticFailure | DiagnosticFailure | DiagnosticFailure | DiagnosticFailure |
| upload/readback copy gate | DiagnosticFailure | DiagnosticFailure | DiagnosticFailure | Prototype metadata, requires exact input | DiagnosticFailure |

source lease/input/statistics等允许CPU关系同时仍输出owner/completion/publication未证明诊断；
不能因为本表标记metadata就移除该诊断或继续native执行。WebGPU copy gate通过仅进入
下游exact layout/packing校验，空输入仍拒绝。错误版本/opt-in总是先于本矩阵求值。

## 稳定诊断目录

所有下列诊断均在lowering/prepare阶段产生，无native副作用；恢复条件为选择已经实现且
验证过的另一条明确contract，或未来版本实现对应WebGL payload并补完整证明。重试或修改
caller flags不构成恢复。unknown值和无效版本保留原有诊断优先级。

| 操作 | WebGL诊断 |
| --- | --- |
| buffer readback binding/1,2,3 | RHI-WEBGL1-BUFFER-READBACK-BINDING-UNAVAILABLE |
| buffer readback request/1 | RHI-WEBGL1-BUFFER-READBACK-REQUEST-UNAVAILABLE |
| texture readback source lease/1,2 | RHI-WEBGL1-READBACK-SOURCE-LEASE-UNAVAILABLE |
| input snapshot | RHI-WEBGL1-READBACK-INPUT-SNAPSHOT-UNAVAILABLE |
| borrowed retirement | RHI-WEBGL1-READBACK-BORROWED-UNAVAILABLE |
| cold budget plan | RHI-WEBGL1-READBACK-COLD-BUDGET-UNAVAILABLE |
| content intent | RHI-WEBGL1-READBACK-CONTENT-UNAVAILABLE |
| content coverage | RHI-WEBGL1-READBACK-COVERAGE-UNAVAILABLE |
| statistics plan | RHI-WEBGL1-READBACK-STATISTICS-UNAVAILABLE |
| owned preparation/context resolution | RHI-WEBGL1-READBACK-OWNED-UNAVAILABLE |
| packed upload copy mapping | RHI-WEBGL1-PACKED-COPY-UNAVAILABLE |
| upload generation | RHI-WEBGL1-UPLOAD-GENERATION-UNAVAILABLE |
| upload copy gate | RHI-WEBGL1-UPLOAD-COPY-UNAVAILABLE |
| readback copy gate | RHI-WEBGL1-READBACK-COPY-UNAVAILABLE |

## 实施及验证

先以真实frontend decision类型复现WebGL错误诊断，再修改semantics。逐项覆盖原四backend、
WebGL、unknown、有效/无效版本、opt-in、context facts与Legacy预算flag。对upload/readback
真实frontend lower入口断言失败时output保持不变，context validator在WebGL/unknown不调用；
纯decision矩阵验证无core allocation。保留快速MinGW测试、profile及Null hidden-frame。

未新增shader、native执行或可见输出；本增量不新增视觉完成声明。原native回归与前一批
DeviceCapabilitySeed证据分别记录，不能由transfer拒绝推导Backend/Platform Validated。
真实copy→completion→map→publication→retire的backend调用链继续审查，P0-10不因此关闭。

最终证据：`build/webgl1-evidence/p0/transfer-admission/verified-final/`实际MinGW
构建与5/5 CTest通过，49项命名输入前后完全一致并保存字节副本；包括本contract test、
profile、readback batch、readback statistics和Null hidden-frame。有效request/source
lease版本保留及非法版本优先级均由最终测试二进制验证。`format-report.yaml`记录
21.1.8两文件格式、token和字节检查；前两轮输入变化记录保留，不作最终一致性证据。
此状态不提升Backend Validated、Platform Validated或Production Ready。
