# ArpScript 7 项语言特性实现计划

**状态**：Implementation In Progress（本文档）；各特性实施后按实际验证状态更新。

**2026-08-25 AOT reverse delegation contract**：新增真实跨模块
DirectNative `yield from` fixture。只有显式 `generatorBridge` 的
ScriptFunction binding、固定 arity 和 package dependency module/version/
artifact identity 全部匹配时才生成 child forwarding；普通可挂起
`CallValue`、未声明依赖和不完整 identity 均确定性返回
`AotSwitchUnsupported`。Windows Debug/Release 与 Web Debug 的生成、动态包
和差分相关测试通过。该增量只关闭 bounded DirectNative dependency slice；
静态循环 delegation 现在由显式 recursion budget 约束并允许生成，Interpreter
child、动态 source typing、delegated await/throw、完整 generator protocol
和整体 Production Ready 门禁仍保持开放。

**当前实施进度**：F1-F6 `Backend Validated`；F7 仍为 `Prototype Complete`，但
已关闭同进程 DirectNative delegated-child canonical ownership/adoption 的
bounded slice；整体 AOT/Hybrid 继续保持 `Implementation In Progress`。

**AOT 架构清理边界（2026-08-25）**：旧 contract-v1 descriptor、generic
executor、Hybrid 实现及其聚合构建目标已从当前源码树和构建图移除；生产与编辑器
目标只链接 v5 DirectNative、Typed IR 或明确的 Interpreter 编辑器服务。本文后续
历史增量中出现的 `legacy/*.cpp`、`arp_scriptvm_aot_tests`、
`arp_scriptvm_hybrid_tests` 等名称仅表示当时的验证证据，不表示当前仍存在的源码
或 target。未迁移的 generator protocol、复杂 adoption 和 optimized-PC
deoptimization 没有隐式兼容路径，当前继续保持 `Implementation In Progress`，
由 v5 确定性拒绝或 Interpreter-only 编辑器路径承接。

## 未完成内容总览（权威清单）

**截至 2026-08-25。** 本节是当前未完成项的集中入口；下面的增量记录用于说明已
验证的精确切片，不能替代本清单中的泛化能力、平台或质量门禁。精确 Native
handler/generator/OSR shape 已达到 `Contract Complete`，部分 Windows MinGW
工件达到 `Backend Validated`，Wasm 浏览器目标达到其目标范围内的
`Platform Validated`；AOT/Hybrid 整体仍为 `Implementation In Progress`，不能标记
`Production Ready`。

| 类别 | 仍未完成内容 | 关闭条件/目标状态 |
|---|---|---|
| F7 generator | F7 整体仍为 `Prototype Complete`；完整 Python generator protocol、任意 `yield from` child 和未证明的 Native generator frame 仍未完成。v5 对带显式 `ScriptAwaitable::inject` contract 的 pending-await 已支持 `send`/`throw` 的 bounded 注入；同包、静态证明的 DirectNative child 现在可由 `yield from` 消费并转发 send/throw/close；显式 `Any` source 也进入 bounded runtime contract，仅接受 DirectNative、带完整 V3 identity 的 bridge generator 或 canonical collection，其余值确定性失败。父 `RangeNext`/`Yield` 的同进程 DirectNative canonical ownership、child pending-await adoption 和 no-replay 已有正向/负向 smoke；动态/Interpreter delegated-await 的完整组合和未证明 Native frame 仍未完成；没有 callback 的 awaitable 仍确定性拒绝。pending-await `close` 仍按现有语言契约丢弃 continuation | 完整协议的 Interpreter/AOT/Hybrid differential、异常/生命周期 corpus 和平台 smoke；目标至少 `Backend Validated` |
| Native lowering | 任意复杂或分叉 handler CFG、typed filter、动态 handler target、handler-side `await`/`yield` 以及更广泛的异步 handler control-flow；动态调用、动态值/容器类型和复杂 delegation 的 Native lowering | 为每类 CFG 建立 Typed IR/source-map/manifest proof、四态 capability gate、Native 与 Interpreter differential；按实际证据达到 `Backend Validated` |
| Native deoptimization | 从任意优化 native PC 即时切回 Interpreter 的完整 deoptimization；当前仅支持已证明的 cooperative operation-boundary OSR，不能复制或猜测任意 native PC | 真实 optimized native frame、locals/operand stack/handler/capture/caller chain 的可逆 snapshot，且无副作用重放；目标 `Backend Validated` 后再做平台验证 |
| 语义等价 corpus | 更广泛 AOT/Interpreter differential corpus：复杂异常区域和动态异常 payload、完整 generator `send`/`throw`/`close` 组合、跨 backend delegated-await/close/throw 组合，以及长期随机/fuzz corpus | 固定回归 corpus、负向 fail-closed 断言、长时 fuzz/perf 结果和稳定诊断；目标 `Contract Complete`/`Backend Validated` |
| Hybrid migration | 复杂 handler CFG 的双向 adoption；深层、循环或无界 delegated-await graph；动态/无界 cross-module async graph；优化 Native frame 的即时切换 | 每个迁移点都保存并恢复 canonical continuation，验证 ownership、handler 链、pending await 和 no-replay；目标 `Backend Validated` |
| 平台验证 | Android 实机或 emulator runtime smoke；iOS 实机/Xcode；macOS 实机；pause/resume、surface/layer/device loss、swapchain/drawable 和生命周期资源回收 | 各目标平台真实运行证据及失败诊断；按平台标记 `Platform Validated`，不能由 Windows/Wasm 证据代替 |
| 质量门禁 | 跨平台性能阈值、热路径 bounded-allocation/无阻塞证据、长期 differential/fuzz、复杂异常和 generator corpus 的持续门禁；optimized native-PC deoptimization 的真实 backend 证据 | 预算、吞吐/延迟阈值、长时稳定性和四 backend contract 全部有可复现记录；才可评估 `Production Ready` |

### 当前状态边界

- 已完成的 exact shape 只对声明的 operation、metadata、stack/capture/parameter 和
  handler target 生效；不满足 proof 时必须走显式 Interpreter-only 编辑器路径或
  deterministic `AotSwitchUnsupported`，不能静默扩大支持范围或恢复已删除的 generic
  executor。
- Windows MinGW 的 AOT/Hybrid CTest、Typed IR/codegen 测试和独立工件 smoke 是
  `Backend Validated` 证据；Wasm package/browser smoke 是 Web 目标的
  `Platform Validated` 证据；Android 目前只有 NDK/package contract，没有设备运行证据。
- 在上述 Native、Hybrid、平台和质量项关闭前，不能把“Interpreter 与 AOT 语义等价”
  宣称为任意脚本的全覆盖，只能宣称已验证 corpus 和精确 contract 的等价。

**AOT legacy shape-proof cleanup (2026-08-24)**：历史 2 至 8 层
nested-rethrow ABI 查询现在全部是 `NativeShapeProof::matchNestedRethrow`
的精确深度包装器，删除了重复的手写 CFG matcher。depth 2、depth 8 和
handler-target 篡改拒绝均由 `arp_scriptvm_aot_tests` 覆盖，并在 Windows
Debug/Release 与 Web Debug/Release 通过。该清理不扩大 Native eligibility；
未证明的 CFG 仍确定性拒绝，F7/Hybrid/平台/长期质量门禁状态不变。

**AOT source-boundary audit tightening (2026-08-24)**：生产边界检查现在同时
扫描 `runtime/` 与 `interpreter/` 下的头文件和实现文件，并拒绝 v1 descriptor、
legacy execution context/generator、generic executor 和 Hybrid 类型引用；
二进制/package audit 继续检查完整 generic-executor/Interpreter 禁止符号集合。
Windows Debug/Release 与 Web Debug/Release 的 source-boundary 和 v5 focused
测试通过。该检查强化了链接边界，不改变 legacy 迁移矩阵的开放状态。

**AOT allocator cleanup (2026-08-24)**：v5 package builder 与 legacy artifact
manifest reader 已移除默认 `std::string` 临时容器，路径转换直接使用
`core::pmr::String`；Windows Debug 的 v5、legacy AOT/Hybrid、source-boundary、
manifest validation/report 和 deterministic-output 测试通过。该阶段不改变
manifest wire format 或公开 ABI。

**AOT bounded delegation differential (2026-08-24)**：DirectNative 同包、静态证明
的 `yield from` child 现在有 Interpreter/DirectNative 随机差分覆盖，包含首次
yield、`send` 转发、child `throw`、terminal result 和完成后的 no-replay；生成包
smoke 同步覆盖 owner identity source contract。Interpreter 对源码级
`yield; return expr` 的 `send` 注入改为在无 `StoreLocal` 消费点时丢弃，避免违反
静态 `maxStack`，显式 `StoreLocal` 接收 send 的既有 fixture 保持不变。Windows
Debug/Release 与 Web Debug 的 v5/VM/Hybrid focused CTest 均通过。一次 Web 失败
被定位为外部 Release `arp_script_aotc` 旧产物，重建 host 后重新生成 package 即
恢复；这确认 Web 生成边界依赖最新 host compiler，不改变支持范围。完整 Python
generator protocol、动态/Interpreter/集合 child、跨 backend close forwarding、复杂 Hybrid
adoption 与 optimized native-PC deoptimization 仍保持开放。

**AOT bounded delegation close forwarding (2026-08-24)**：生成的
`DirectNative` parent frame 在析构时通过 RAII 关闭仍持有的同包 child；unit
contract、Windows Debug/Release generated smoke、dynamic-package smoke 和
Web Debug generated smoke 均通过。该增量只关闭静态证明的 same-package Native
child close，不扩大到动态、Interpreter、集合或跨 backend delegation；完整
generator protocol 与 Hybrid 退休门禁仍保持开放。

**AOT bounded delegation cycle proof (2026-08-24)**：DirectNative codegen 在
shape 校验后以 PMR 状态向量遍历静态 `yield from` delegation graph，确定性拒绝
自递归和互递归（`AotArtifactInvalid`），避免 resume 时形成无界 native 调用链。
self/mutual recursion 的 v5 contract 负向单测、生成包 smoke 和 dynamic package
smoke 均通过；该证明不扩大动态、Interpreter、集合或跨 backend child 的支持范围。

**AOT bounded collection delegation protocol (2026-08-24)**：DirectNative codegen
现在只准入可证明的 collection child：返回类型为 `List`，或 `Any` 函数的每个
返回点都紧邻 `MakeList`。Interpreter、Legacy AOT 和 DirectNative 对该切片统一支持
nil `send()` 继续迭代、父 generator handler 的 `throw()` 注入，以及非 nil send 的
非消费式 `AotSwitchUnsupported`；生成包和 dynamic-package smoke 覆盖 no-replay。
动态 collection、Interpreter/cross-backend child、完整 Python generator protocol
和其它未证明 shape 仍保持确定性拒绝。

**AOT v5 dynamic package and importer hardening (2026-08-24)**：DirectNative
package/module invocation now treats cumulative `CpuMemoryQuota` failure as a
terminal result: generated return values and retained continuations are
discarded on `ResourceLimitExceeded`. Dynamic package artifacts use SHA-256
content identity, identity-scoped staging, process locking, atomic DLL/marker
publication, and deterministic DLL owner RAII. The C++ plugin importer now
uses structured `core::ProcessRunner` arguments for reflection generation and
plugin compilation, and its toolchain fingerprints are content SHA-256 rather
than size/mtime. Toolchain allocations are isolated from the active script
quota; the reflection generator resolves declared aliases and the parent-
namespace `Value` contract instead of emitting an invalid nested qualified
type. Windows Debug focused AOT/Hybrid acceptance is 17/17, with importer and
plugin tests rerun after this final hardening. This remains bounded
`Platform Validated` evidence and does
not close the legacy migration matrix or promote overall AOT/Hybrid to
`Production Ready`.

**AOT dynamic staging repeatability and concurrency (2026-08-25)**：Windows
dynamic package staging now acquires the per-artifact process lock before
publishing identity-scoped staging, uses a monotonic OS tick in temporary names,
and copies generated contents into the identity directory under that lock before
atomic DLL/marker publication. This avoids PID/TID reuse collisions and the
Windows directory-rename `Permission denied` path observed on repeated smoke
runs. The dynamic-package smoke now also launches two concurrent loaders against
one cache identity; repeated Release runs passed, including truncated-DLL repair,
marker recovery, lexical relocation, budget-identity variation, and concurrent
generation. This remains bounded Windows evidence only.

**AOT v5 allocator/ownership follow-up (2026-08-24)**：动态包 cache identity
现在把原始 manifest 内容纳入 SHA-256 contract，预算或其它 lowering 控制的
变化不会复用旧 DLL。DirectNative context 为所有 module 持有 package owner，
generator 即使晚于 dispatcher 销毁也不会执行已卸载的 DLL；bound-call/module-
export 回调改为 context-owned weak runtime bridge，owner 消失后稳定返回
`AotModuleUnavailable`。Windows Debug v5 unit、generated smoke 和 dynamic
package smoke 通过；该增量保持 bounded Windows/Web `Platform Validated`，不
关闭完整 generator/Hybrid/mobile/Production Ready 门禁。

**AOT Windows Release completion evidence (2026-08-25)**：补建此前未生成的
domain-plugin 与 stress benchmark 目标后，Windows Release 完整
`arp_scriptvm_*` CTest selection 通过 25/25，包含 dynamic-package、editor
hidden-window、plugin、differential、endurance 与 release performance corpus。
dynamic-package smoke 同时验证了 MinGW host DLL 对可选
`GeneratorBridgeV2` provider 的显式导出，避免生成包链接到未导出的 ABI
符号。该阶段完成 Windows Release 验收证据，不改变 v5 支持边界；完整
generator protocol、Hybrid adoption、任意-PC deoptimization、移动平台和
八小时 endurance 仍保持 `Implementation In Progress`。

**AOT GeneratorBridgeV3 identity contract (2026-08-25)**：在不改变
`GeneratorBridgeV2` ABI 的前提下新增独立的版本化 V3 provider contract，携带
结构体大小、contract version 和 module/version/artifact identity query。
Interpreter 只有在 VM 显式绑定 artifact token 时才暴露完整身份；DirectNative
从已注册 module descriptor 固定读取身份。生成的 `yield from` 反向分支在
身份缺失或不一致时确定性返回 `AotSwitchUnsupported`，不会把 V2 provider
猜测为 V3。Windows Debug 的 VM/AOT unit、generated smoke 和 dynamic-package
smoke 已通过；静态 source proof、跨 module dependency identity、delegated
await/throw 和完整 generator protocol 仍开放。

**AOT continuation budget contract (2026-08-24)**：持久化 DirectNative frame
的 admission 统一由 `DirectNativeExecutionContext::beginContinuationFrame`
执行，同时检查累计 `maxContinuationFrames` 与累计字节预算，再由生成 frame
完成 owner/count 发布。数量超限不会发布部分 frame，也不会破坏已有 continuation；
v5 unit 与 generated-package smoke 已覆盖该负向路径。该证据只强化 bounded
budget contract，不关闭完整 generator protocol、任意-PC deoptimization 或
Legacy retirement matrix。

**AOT package-owner initialization contract (2026-08-24)**：动态 package 的
owner 现在在 `DirectNativePackageRuntimeV5::initialize()` 之前注入，并由
runtime 保存后传递给初始化期间创建的每个 module context。initializer unit
contract、generated smoke 和 dynamic-package smoke 均通过；因此 closure/
generator 在 module initializer 中创建时也不会丢失 DLL owner。该修复不扩大
Native shape 或 Hybrid 迁移范围。

**Legacy shape-validator audit (2026-08-24)**：复核确认 nested-rethrow、yield、
await-yield、handler、closure-call、yield-from 和 bound-call exact proof 已迁入
`NativeShapeProof`；dynamic closure-child shape handling、Interpreter bridge 和
generator/snapshot orchestration 仍位于 `legacy/continuation_orchestration.cpp`。
矩阵和 README 已按此事实修正，避免将部分拆分误报为完整
`native_shape_proof` 模块化；剩余部分仍属于 editor/differential migration debt。

**AOT handler shape-proof extraction (2026-08-24)**：固定单层 yield-handler 与
handler-side branch 两个 exact validator 已迁入 `NativeShapeProof`，原
`continuation_orchestration.cpp` 入口保留为 ABI 不变的薄 wrapper。正向 frame-machine
和 codegen 测试，以及 handler/branch target 篡改负向 proof 测试均通过；该迁移不扩大
Native eligibility。bound-call family 已集中到 proof class；dynamic closure-child
shape handling 仍保留在 legacy orchestration，继续按退休矩阵作为 editor/differential
migration debt 管理。

**AOT await-yield proof extraction (2026-08-24)**：无 handler 的线性 await-yield
exact validator 已迁入 `NativeShapeProof::matchAwaitYield`，旧 orchestration
入口保持为薄 wrapper。LoadLocal/LoadCapture/零参 CallHost source、Await/Yield
顺序及 terminal return 的正向 proof，以及 Await opcode 篡改负向 proof 均有快速
unit 覆盖；此阶段不扩大 Native eligibility。dynamic closure-child validator 仍属于
后续 legacy migration debt。

该阶段随后补齐 capture-mutation 与 closure-call await-yield 两个局部 proof，
分别验证 capture slot 不别名和 zero-capture closure provenance；旧入口均保留为
薄 wrapper。正向/slot aliasing/provenance omission 负向单测、legacy/Hybrid 与
generated smoke 均通过，不扩大动态调用或复杂 handler 的 Native 准入范围。

随后，yield-from 的 call-layout、canonical `RangeNext` loop、await-handler 和
depth-derived nested-handler proof 也已集中到 `NativeShapeProof`；运行时 resume
通过同一 `NativeYieldFromCallLayout` 消费 call PC/arity，避免 proof 与执行路径
使用不同的 locator。yield-from key-slot、handler target 和已有 nested-handler
迁移/OSR 测试保持通过；当前主要遗留为 dynamic closure-child 与
generator/snapshot orchestration。

**AOT bound-call proof extraction (2026-08-24)**：六个 metadata-bound
`CallValue -> Await -> Yield` 变体（单参数、global、local arguments、capture
arguments、local+capture arguments、handler variant）现在共享
`NativeShapeProof` 的 call-tail validator；历史 orchestration 函数仅保留 ABI wrapper。
现有绑定 identity、参数布局、handler target 与 no-replay AOT/Hybrid 测试均通过；
dynamic closure-child variants 和 generic generator/snapshot orchestration 仍未迁移。

**Legacy generator protocol admission extraction (2026-08-24)**：
`nativeDelegatedDepth` 与 `validateAotGeneratorProtocolState` 已从
`continuation_orchestration.cpp` 移入 `generator_runtime.cpp`，通过
`generator.h` 的 legacy-internal 声明共享。Native yield-from depth 现在直接消费
`NativeShapeProof::matchYieldFrom`，并在缺失 module/function/operation metadata、
非法 delegated continuation 或超出 bounded await depth 时保持原有稳定诊断并失败关闭。
`arp_scriptvm_aot_tests`、`arp_scriptvm_hybrid_tests`、generated-package smoke 与
source-boundary audit 均通过；该结构迁移不扩大 Native eligibility，dynamic/
Interpreter/cross-backend generator protocol 仍按退休矩阵保持开放。

**Legacy generator public-entry boundary extraction (2026-08-24)**：薄的
`resumeAotGenerator` ABI wrapper 已从 `continuation_orchestration.cpp` 移入
`generator_runtime.cpp`，内部恢复机通过 `execution_internal.h` 的显式
legacy-internal 声明调用；`send`/`throw` 仍与 native frame-machine 状态判断
同 TU，避免复制 shape 依赖。最新 Windows MinGW Debug/Release AOT、Hybrid、
generated-package 与 dynamic-package smoke，以及 source-boundary audit 均通过。
该迁移只改变职责边界，不扩大 generator protocol 或 Native shape 准入。

随后，native yield/await gate 以及 `send`/`throw` 的公开 ABI 入口也集中到
`generator_runtime.cpp`；orchestration 仅保留带 `Impl` 后缀的 shape-dependent
状态机，并通过 `generator.h` 的 legacy-internal contract 调用。Debug AOT、Hybrid
与 generated-package smoke 通过；动态/Interpreter/cross-backend generator
protocol 与完整状态机迁移仍按退休矩阵开放。

**Legacy generic generator executor extraction (2026-08-25)**：generic
`resumeAotGeneratorImpl` continuation executor 已物理迁入
`legacy/generator_execution.cpp`，只通过 `generator.h`、
`execution_internal.h` 和 `NativeShapeProof::matchYieldFrom` 访问共享契约；
`continuation_orchestration.cpp` 不再持有该 generic continuation loop。Debug
AOT/Hybrid 与 generated-package smoke 通过，未扩大 Native shape 或 delegation
支持范围；native frame-machine、dynamic closure-child、完整 generator protocol
和 snapshot orchestration 仍保持开放。

同一阶段又将 `isAotGeneratorFunction` 迁入该模块，并让预解码 `Yield` opcode
与 legacy name path 共用判定；缺失 operation metadata 时确定性返回 false。该
分类调整只收紧 malformed artifact 的处理，不扩大 generator 或 Native shape
准入范围，AOT/Hybrid unit、generated smoke 与 source-boundary audit 保持通过。

**Legacy execution-entry boundary extraction (2026-08-25)**：公开的
`executeAotFunction*`、`executeAotFunctionEntry` 和 `resumeAotFunction` 已从
`continuation_orchestration.cpp` 移入独立的 `legacy/execution_entry.cpp`。
内部 frame machine 只保留 `executeAotFunctionWithStateImpl`，入口通过
`execution_internal.h` 的显式 bridge 消费 native-await machine 和 canonical
caller append 语义，未复制 continuation 算法或改变 ABI/diagnostic code。
Windows Debug/Release 与 Web Debug/Release 的 AOT、Hybrid、generator-close
smoke、build-graph/source-boundary contract 均通过。该阶段只收敛职责边界，
不扩大 generator protocol、Hybrid adoption 或 arbitrary-PC deoptimization。

**Legacy continuation-builder extraction (2026-08-24)**：
`makeAotAwaitContinuation` 与 `appendCanonicalAotCaller` 已迁入
`continuation_adoption.cpp`，通过 `execution_internal.h` 的 legacy-internal
接口供 generator、scalar/value executor 和 Hybrid bridge 使用；原
`makeAotOperationBoundaryContinuation` 也已迁入同一模块，OSR 入口只保留
调用，仍只接受完整 source-map 的 operation boundary。AOT/Hybrid unit、generated
smoke、dynamic-package smoke（73.2 秒）和 source-boundary audit 均通过；该迁移不
扩大 deoptimization 或 generator protocol 支持范围。

随后，`adoptAotGeneratorFrame` 已在显式 include 和统一 resume-kind classifier
基础上迁入 `continuation_adoption.cpp`；其 Native shape 依赖直接消费
`NativeShapeProof`，registry、delegated `RangeNext` metadata、Value-frame proof
和 closure-chain reconstruction 的原有诊断保持不变。Debug AOT/Hybrid、generated
package、dynamic-package 与 source-boundary 验证均通过。剩余 orchestration 主要是
generator execution、snapshot production 和 dynamic closure-child execution，尚未
达到 legacy 删除门槛。

`makeAotGenerator` 随后迁入 `generator_runtime.cpp`，参数规范化、runtime type
contract、retention lease、variadic locals 和 classifier 现在由 generator runtime
单一负责；orchestration 只保留调用。Windows Debug/Release AOT/Hybrid、generated
package、source-boundary 和 Debug dynamic-package smoke 均通过，未改变 v1 ABI 或
manifest wire format。

**Legacy generator resume-kind contract (2026-08-24)**：generator creation 与
Interpreter/Hybrid adoption 现在共同消费 `generator_runtime.cpp` 的
`classifyAotGeneratorResumeKind`。该 classifier 先判断 await Native proof，再按
当前 frame 是否处于 await safepoint决定是否允许 yield Native proof，保留原有
await-shape 优先级和 `IrExecutor` 默认失败关闭语义；不再由 orchestration 各自维护
重复的 shape 列表。AOT/Hybrid unit、generated loader、dynamic-package smoke 和
source-boundary audit 均通过。adoption 的 registry、Value-frame 和 delegated
metadata 依赖已纳入 continuation contract；下一步集中处理 generator execution、
dynamic closure-child 和完整 Python generator protocol。

**AOT/Hybrid 三层 Native handler 增量（2026-08-21）**：在单层与双层
`Yield`/`throw()` frame-machine 之上，新增一个固定三层 nested-rethrow contract：
`PushHandler(15) -> PushHandler(12) -> PushHandler(9) -> LoadInt -> Yield ->
PopHandler -> PopHandler -> PopHandler -> Jump(18)`，三个 handler 分别执行
`StoreLocal(0) -> LoadLocal(0) -> Throw`、同形的中层 rethrow，以及
`StoreLocal(0) -> LoadInt -> Yield -> LoadInt -> Return(1)`。Typed IR lowering
明确记录 `(start,end,handler) = (1,7,15)/(2,6,12)/(3,5,9)`（`end` 为
对应 `PopHandler` 的 operation index）；codegen 只在该精确 CFG、两处 safepoint、
无 capture/参数且 bounded stack 证明成立时发布 `Native`。AOT frame-machine、
`throw()` handler 注入、`ArbitraryPc` OSR handler-stack 保留，以及 AOT/Interpreter
双向 Hybrid adoption 均已覆盖。改变 handler target 不能满足 proof 时会稳定回退
`IrExecutorPrototypeLegacy`；该切片为 `Contract Complete`，不扩大到任意复杂 CFG、
五层以上 rethrow、异步 handler-side rethrow 或 optimized native-PC deoptimization。

**AOT/Hybrid 四层 Native handler 增量（2026-08-21）**：在三层 exact shape
之上新增固定四层 nested-rethrow contract：四个 handler 包围首个 yield，内层、
中层和次外层依次 `StoreLocal(0) -> LoadLocal(0) -> Throw`，最外层执行
`StoreLocal(0) -> LoadInt(8) -> Yield -> LoadInt(9) -> Return(1)`。Typed IR
region 精确为 `(1,9,20)/(2,8,17)/(3,7,14)/(4,6,11)`，Native codegen
要求 25 个 operation、两处 safepoint、无参数/capture、单槽 stack 和固定 target；
任何 target/order/metadata 偏差均回退 `IrExecutorPrototypeLegacy`。AOT
frame-machine、`throw()` 四层 unwind、OSR handler-stack 保留和 AOT/Interpreter
双向 Hybrid migration 均已验证，状态为 `Contract Complete`。五层以上、任意复杂
handler CFG、异步 handler-side rethrow 和 optimized native-PC deoptimization
仍未完成。

**AOT/Hybrid 五层 Native handler 增量（2026-08-21）**：在四层 exact shape
之上新增固定五层 nested-rethrow contract：五个 handler 包围首个 yield，内层、
中层、次外层和第四层依次执行 `StoreLocal(0) -> LoadLocal(0) -> Throw`，
最外层执行 `StoreLocal(0) -> LoadInt(8) -> Yield -> LoadInt(9) -> Return(1)`。
Typed IR region 精确为 `(1,11,25)/(2,10,22)/(3,9,19)/(4,8,16)/(5,7,13)`；
Native codegen 只接受 30 个 operation、两处 safepoint、无参数/capture、单槽
stack 和固定 handler target。AOT Native frame-machine、五层 `throw()` unwind、
ArbitraryPc handler-stack 保留、Typed IR/manifest 正向与 target 偏差 Legacy fallback，
以及 Windows MinGW AOT/Hybrid 双向 migration 测试均已通过，状态为
`Contract Complete`。七层以上、任意复杂 handler CFG、异步 handler-side rethrow、
optimized native-PC deoptimization、Android/iOS/macOS 实机和长期 fuzz/perf 门禁仍未完成。

**AOT/Hybrid 六层 Native handler 增量（2026-08-21）**：在五层 exact shape
之上新增固定六层 nested-rethrow contract：六个 handler 包围首个 yield，前五层
依次执行 `StoreLocal(0) -> LoadLocal(0) -> Throw`，最外层执行
`StoreLocal(0) -> LoadInt(8) -> Yield -> LoadInt(9) -> Return(1)`。Typed IR
region 精确为 `(1,13,30)/(2,12,27)/(3,11,24)/(4,10,21)/(5,9,18)/(6,8,15)`；
Native codegen 只接受 35 个 operation、两处 safepoint、无参数/capture、单槽
stack 和固定 handler target。Native frame-machine、六层 `throw()` unwind、
ArbitraryPc handler-stack OSR、Typed IR target 偏差 Legacy fallback，以及
Windows MinGW AOT/Hybrid 双向 migration 测试均已通过，状态为
`Contract Complete`。七层以上、任意复杂 handler CFG、异步 handler-side rethrow、
optimized native-PC deoptimization、Android/iOS/macOS 实机和长期 fuzz/perf 门禁
仍未完成；整体仍不能标记 `Backend Validated`、`Platform Validated` 或
`Production Ready`。

**AOT/Hybrid 七层 Native handler 增量（2026-08-21）**：在六层 exact shape
之上新增固定七层 nested-rethrow contract：七个 handler 包围首个 yield，前六层
依次执行 `StoreLocal(0) -> LoadLocal(0) -> Throw`，最外层执行
`StoreLocal(0) -> LoadInt(8) -> Yield -> LoadInt(9) -> Return(1)`。Typed IR
region 精确为 `(1,15,35)/(2,14,32)/(3,13,29)/(4,12,26)/(5,11,23)/(6,10,20)/(7,9,17)`；
Native codegen 只接受 40 个 operation、两处 safepoint、无参数/capture、单槽
stack 和固定 handler target。Native frame-machine、七层 `throw()` unwind、
ArbitraryPc handler-stack OSR、Typed IR target 偏差 Legacy fallback，以及
Windows MinGW AOT/Hybrid 双向 migration 测试均已通过，状态为
`Contract Complete`。八层以上、任意复杂 handler CFG、异步 handler-side rethrow、
optimized native-PC deoptimization、Android/iOS/macOS 实机和长期 fuzz/perf 门禁
仍未完成；整体仍不能标记 `Backend Validated`、`Platform Validated` 或
`Production Ready`。

**AOT/Hybrid 八层 Native handler 增量（2026-08-21）**：在七层 exact shape
之上新增固定八层 nested-rethrow contract：八个 handler 包围首个 yield，前七层
依次执行 `StoreLocal(0) -> LoadLocal(0) -> Throw`，最外层执行
`StoreLocal(0) -> LoadInt(8) -> Yield -> LoadInt(9) -> Return(1)`。Typed IR
region 精确为 `(1,17,40)/(2,16,37)/(3,15,34)/(4,14,31)/(5,13,28)/(6,12,25)/(7,11,22)/(8,10,19)`；
Native codegen 只接受 45 个 operation、两处 safepoint、无参数/capture、单槽
stack 和固定 handler target。Native frame-machine、八层 `throw()` unwind、
ArbitraryPc handler-stack OSR、Typed IR target 偏差 Legacy fallback，以及
Windows MinGW AOT/Hybrid 双向 migration 测试均已通过，状态为
`Contract Complete`。任意复杂 handler CFG、异步 handler-side rethrow、
optimized native-PC deoptimization、Android/iOS/macOS 实机和长期 fuzz/perf 门禁
仍未完成；整体仍不能标记 `Backend Validated`、`Platform Validated` 或
`Production Ready`。

**AOT/Hybrid generic exact nested-rethrow 增量（2026-08-21）**：将九层及以上
的同一线性 nested-rethrow 模板改为按 operation 数量、handler target 公式和
Typed IR region 公式进行 data-derived proof。仅接受
`PushHandler -> LoadInt -> Yield -> PopHandler* -> Jump`、连续
`StoreLocal(0) -> LoadLocal(0) -> Throw` handler blocks 和固定 terminal handler；
复杂分支、动态 target、typed filter、异步 handler-side control-flow 仍拒绝。
Native/throw/ArbitraryPc OSR、Typed IR/codegen 和 depth-nine AOT/Hybrid smoke
均已通过，状态为 `Contract Complete`。任意复杂 handler CFG、异步 handler-side
rethrow、optimized native-PC deoptimization、Android/iOS/macOS 实机和长期
fuzz/perf 门禁仍未完成；整体仍不能标记 `Backend Validated`、`Platform Validated`
或 `Production Ready`。

**AOT/Hybrid 最新增量（2026-08-20）**：固定 Native `Await` Value 入口在完整
`ArbitraryPc` source-map contract 下支持 operation-boundary OSR，并可迁移到
Interpreter；poll 期间的切换请求复用已创建的 awaitable，不重放调用。AOT/Hybrid
focused tests 与 19 项 ScriptVM CTest 已通过。该增量为 `Contract Complete`，不提升
整体到 `Backend Validated`、`Platform Validated` 或 `Production Ready`。

**AOT/Hybrid Native generator OSR 增量（2026-08-21）**：无 await、无复杂 handler
的 Native `Yield` generator 在函数声明为 `ArbitraryPc` 且每个 operation 都有完整
source map 时，支持在下一条 operation 前消费 `osrRequested`，保留 canonical locals、
operand stack、handlers 与 captures，并切换到 `IrExecutor` 继续执行。该路径不创建
pending await continuation；缺少或不一致 metadata 时确定性返回 `AotSwitchUnsupported`。
Windows MinGW AOT/Hybrid focused、完整 19 项 ScriptVM CTest 与 Wasm AOT smoke 均通过，
该精确 shape 为 `Contract Complete`。复杂 delegated-yield 组合、复杂 handler CFG
和 optimized native-PC deoptimization 仍未完成。

**AOT/Hybrid delegated-yield OSR 增量（2026-08-21）**：Native delegated-yield
父 generator 在 `RangeNext` operation boundary 具备完整 `ArbitraryPc` source map
时，可保留 locals、operand stack、delegation metadata 并切换到 `IrExecutor`，继续
执行而不重放 child 的首个 yield。若 child 已拥有 pending await/delegated continuation，
OSR 在父级边界 deterministic fail-closed，且不消费或破坏 child continuation；await
ready 后仍可恢复并完成 parent。AOT/Hybrid 双向 migration、pending-child negative
contract、19 项 ScriptVM CTest 与 Wasm AOT smoke 均通过，该 bounded shape 为
`Contract Complete`。复杂动态 delegation、复杂 handler CFG 和 optimized native-PC
deoptimization 仍未完成。

**AOT Native await-generator OSR 增量（2026-08-21）**：专用 Native
`Await -> Yield` generator frame-machine 现在也消费 operation 开始前的
`osrRequested`，在完整 `ArbitraryPc` source map 下切换到 `IrExecutor`，再由通用
executor 创建并保存同一个 awaitable；已有 Native pending continuation 不允许被 OSR
替换。该切片已覆盖首轮切换、pending 状态拒绝和结果恢复，状态为 `Contract Complete`。
精确 `CallHost -> Await -> Yield` shape 还通过真实 Hybrid dispatcher 验证了 callback
完成后保存 awaitable、切换到 Interpreter 和 callback 不重放；复杂 handler/delegated
generator 和 optimized native-PC deoptimization 仍未完成。

**AOT/Hybrid 最新增量（2026-08-21）**：Native Value frame machine 现在允许
`int64`、`float`、`bool`、`string`、`bytes` 等闭合 typed scalar result 在无
`await`/handler 的同步函数中进入 Native；`Any`、object/container 仍保持
`IrExecutorPrototypeLegacy`。AOT/Native 与 generic executor differential、source
manifest eligibility 已覆盖 `int64`/`float`/`bool`，并修正 `LoadBool` 的 descriptor
字段编码 parity。该切片为 `Contract Complete`；optimized native-PC deoptimization、
动态值类型 lowering、跨平台实机和长期 fuzz/perf 证据仍未完成。

**AOT/Hybrid 参数 contract 增量（2026-08-21）**：Native Value frame 的 root 与
所有已证明的 child frame 统一执行 AOT 参数数量和 Typed runtime type normalization，
覆盖 int->int64 等合法 promotion、错误类型和错误 arity；root 与 `Call` child 的
differential regression 已通过。该切片为 `Contract Complete`，不把未声明 metadata
的动态调用提升为 Native。

固定 Native await 入口也已接入同一参数 normalization；类型错误在 poll 前返回与
Generic AOT/Interpreter 相同的 `TypeMismatch`，不触发 awaitable。该修复仍是
`Contract Complete`，不扩大固定 await shape 之外的 OSR 或动态调用范围。

**AOT/Hybrid host callable contract 增量（2026-08-21）**：显式有效的
`HostCallableContract` 与 `HostRegistry` metadata 现在在 Interpreter、Generic AOT
和 Native Value 的 `CallHost`/`CallValue` 调用边界统一校验 `fixedArity`；错误 arity
在执行 host side effect 前返回稳定的 `WrongArgCount`。未声明 metadata 的旧动态
HostCallable 保持 Legacy 行为；Native generator 的 `CallHost -> Await` 状态机也在
创建 awaitable 前消费同一 registry gate。新增 wrong-arity negative differential
coverage，且完整 19 项 ScriptVM CTest 通过。该切片为 `Contract Complete`，不提升整体状态，
optimized native-PC deoptimization、动态值类型 lowering、跨平台实机和长期
fuzz/perf 证据仍未完成。

**AOT 生成工件 loader 增量（2026-08-21）**：独立编译并注册的 AOT 工件现在
包含源码生成的 `yield await` Native generator 及精确单层 handler smoke。loader
验证 Native descriptor、pending continuation、重复 pending 不重放 awaitable、
ready yield、terminal `send()` 和失败 await 的 handler fallback，状态为
Windows MinGW 独立工件的 `Backend Validated`。
这不扩大 Native generator lowering shape，也不提升整体到 `Platform Validated`
或 `Production Ready`；optimized native-PC deoptimization、复杂 generator
protocol、跨平台实机和长期 fuzz/perf 仍未完成。

**AOT/Hybrid Native handler 增量（2026-08-21）**：双 handler await-yield
frame-machine 现在额外接受固定内层 `LoadString -> Throw` rethrow；失败 await
和 yield 边界注入都会先消费内层 handler，再按 canonical handler 栈进入外层
fallback。动态异常 payload、任意 handler-side CFG 和 rethrow 后继续执行仍
保持 fail-closed。AOT runtime、source lowering/manifest regression、AOT ->
Interpreter failed-await unwind 和 Interpreter -> AOT reverse migration 已通过
Windows MinGW 的实际 Hybrid dispatcher 与 19 项 ScriptVM CTest；该精确 shape
现为 `Backend Validated`（Windows MinGW），尚未提升为 `Platform Validated`。
复杂 handler、动态异常 payload、任意 handler-side CFG 和 rethrow 后继续执行仍
保持 `IrExecutorPrototypeLegacy` 或 deterministic fail-closed。

**AOT/Hybrid Native handler OSR 增量（2026-08-21）**：精确单层
`PushHandler(5) -> LoadInt -> Yield -> PopHandler -> Jump` frame-machine 现在允许
显式完整 `ArbitraryPc` source map。首个 protected yield 后切换到 `IrExecutor` 时，
canonical handler stack 会被保留；随后注入 `throw()` 仍只消费该 handler，并得到与
Interpreter 相同的 handler yield/terminal 结果。AOT codegen/manifest、AOT runtime 与
Hybrid 双向 migration contract 已通过 Windows MinGW 测试，状态为 `Contract Complete`。
任意 handler-side CFG、动态异常 payload、多层 rethrow 和 optimized native-PC
deoptimization 仍保持 deterministic fail-closed。

**AOT/Hybrid handler-side branch 增量（2026-08-21）**：在上述单层 handler
基础上，新增一个显式 12-op contract：捕获值写入 local 0，经 `LoadLocal ->
JumpIfFalse` 分为“再次 yield”与共享 terminal return 两条路径。Typed IR
eligibility、Native frame-machine、ArbitraryPc OSR、`throw(bool)` 值保留以及
AOT/Interpreter/Hybrid 双向差分均已覆盖，Windows MinGW AOT/Hybrid focused tests
通过，状态为 `Contract Complete`。除该精确 branch shape 外，动态异常 payload、
嵌套或任意 handler-side CFG、rethrow 后继续执行仍 deterministic fail-closed。

**AOT/Hybrid Native yield nested-rethrow 增量（2026-08-21）**：新增固定双层
15-op frame-machine contract：外层/内层 handler 包围首个 yield，内层将捕获值
原样 `Throw` 到外层，外层再 yield 并共享 terminal return。Native 执行、双层
canonical handler stack、ArbitraryPc OSR、AOT/Interpreter/Hybrid 双向 migration、
Typed IR eligibility 和未证明 target 的 Legacy fallback 均已覆盖，Windows MinGW
AOT/Hybrid focused tests 通过，状态为 `Contract Complete`。动态/任意 handler-side
CFG、三层以上 rethrow、异步 rethrow 和 optimized native-PC deoptimization 仍未完成。

**Interpreter generator unwind regression（2026-08-21）**：新增普通 callee
挂起 await、generator caller 持有 handler、failed poll 后跨 caller unwind 的
VM contract test。该测试断言 child frame 被丢弃、caller handler 获得诊断、
awaitable 只 poll 两次（pending + failed）且 generator 终止；AOT、Hybrid、stress
和 Wasm AOT smoke 均保持通过。该项补强既有 failed-await 语义，不扩大 Native
generator lowering shape。

**AOT/Hybrid 动态值 lowering 增量（2026-08-21）**：Native Value eligibility 现在
允许闭合 `Any` 返回中的 `nil`、`bool`、`int`、`int64`、`float` 字面量，单参数
`LoadLocal + Neg/Not + Return`，以及两个 `Any` 参数的精确
`LoadLocal + LoadLocal + <通用 binary> + Return` 形状和单参数 Any identity；
其中 `<通用 binary>` 只允许已由 `ScriptSemanticRuntime` 定义的算术、位运算和比较
opcode，不包含 typed-int 专用变体。
帧机复用既有 Value 构造器与 `ScriptSemanticRuntime::binary`，Generic AOT/Native 成功
和 equality 结果差分、source manifest eligibility 已覆盖；比较语义统一复用
`ScriptSemanticRuntime::compare`，一元操作复用 `negate/logicalNot`，涵盖字符串相等、不等、
混合 Any 比较和 unary diagnostics。固定三块
`CmpEq + JumpIfFalse + (LoadLocal -> Return)*2` conditional-return CFG 也已覆盖
两个分支的 Generic AOT/Native differential 和 manifest。其它动态局部 CFG、
固定 `LoadLocal -> StoreLocal -> LoadLocal -> Return` Any local forwarding 也已覆盖
identity differential 和 manifest；其它动态局部 CFG、
对象/容器传播、反射和任意表达式仍保持 `IrExecutorPrototypeLegacy`。该切片为
`Contract Complete`，
不提升整体状态。

**AOT/Hybrid 容器别名 lowering 增量（2026-08-21）**：新增一个固定的 Any
容器别名 contract：`LoadLocal(0) -> StoreLocal(2) -> LoadLocal(1) -> Await ->
Pop -> LoadLocal(2) -> LoadInt(0) -> LoadInt(42) -> StoreIndex ->
LoadLocal(2) -> Return(1)`。局部槽只保存同一个 `reflection::object` Value
carrier；AOT/Interpreter adoption 迁移的是槽和 continuation，不复制或重建容器。
Generic AOT、Native Value 和 Hybrid AOT<->Interpreter 双向差分已覆盖，source
manifest 也要求该闭合形状为 `Native`。动态索引、计算型值、嵌套容器、指针图和
不一致 CFG 仍保持 `IrExecutorPrototypeLegacy`；该切片为 `Contract Complete`，
不提升整体状态。

**AOT/Hybrid 嵌套容器别名 lowering 增量（2026-08-21）**：在上述 contract
之上新增线性、有限深度的嵌套形状：外层 `Any` 参数保存到局部槽并跨越 `await`，
恢复后重复以字面量索引读取下一级 `Any` carrier 到唯一局部槽，最后以字面量
索引和值写入最内层并返回外层。Generic AOT、Native Value 和 AOT<->Interpreter
双向 adoption 均保留原始 `reflection::object` identity；source manifest 与
三层 differential 已验证该泛化 proof。动态索引/动态值、分支合流、指针图和
跨函数 alias 继续 `IrExecutorPrototypeLegacy`。该切片为 `Contract Complete`，
不提升整体状态。

**AOT/Hybrid 跨函数容器 mutation 增量（2026-08-21）**：已补齐同模块、固定
arity 的 child mutation contract。child 通过容器参数执行固定字面量
`StoreIndex`，caller 在独立 `await` 后返回原始参数 carrier；Generic AOT、Native
Value differential 与 source manifest 的 child/caller `Native` 分类均已验证。
双向 Hybrid adoption 也验证了 child mutation 只执行一次且 caller 从 await 继续。
未声明的动态 callable、跨模块容器变换、指针图和动态索引仍保持
`IrExecutorPrototypeLegacy`。该切片为 `Contract Complete`，不提升整体状态。

**AOT/Hybrid 对象字段容器 alias 增量（2026-08-21）**：新增闭合的字段读取形状：
对象 carrier 先保存到唯一 local，跨越 `await` 后通过固定字段名读取 `items`，再以
字面量索引和值执行一次 `StoreIndex`，最后返回原始对象 carrier。Generic AOT、Native
Value、source manifest Native 分类以及 AOT<->Interpreter 双向 adoption 均已验证，
且 migration 不重放字段读取或容器 mutation。动态字段名、字段/容器 pointer 图、
动态索引和值及非线性 CFG 继续保持 `IrExecutorPrototypeLegacy`。该切片为
`Contract Complete`，不提升整体状态。

**跨平台验证增量（2026-08-21）**：修复 Wasm AOT 递归 direct-call 的标量比较
语义错误，并将 Hybrid session setup 纳入配额异常边界；同时修正跨模块 Native
Value frame 收尾校验错误引用根模块栈元数据的问题。四个 Wasm ScriptVM 目标现在
在 Node CTest 和本地 HTTP 浏览器 smoke 中均通过；浏览器 smoke 的 AOT/Hybrid
目标还覆盖了 Emscripten `TextDecoder`/Web Crypto 对可调整 Wasm 内存视图的约束，
通过目标级 `-sALLOW_MEMORY_GROWTH=0` 固定 512 MiB 内存契约解决。Windows MinGW
的 19 项 ScriptVM CTest 也全部通过。因此这四个 Web ScriptVM 目标可标记为
`Platform Validated`，但整体项目仍不是 `Production Ready`：Android/iOS/macOS
实机、optimized native-PC deoptimization、跨平台性能门禁和长期 fuzz/perf
证据仍未完成。

**AOT 兼容 lowering 增量**：旧 `Program` compiler 对 sema 已证明的本模块脚本类方法调用
已采用 `MakeMethodValue -> CallValue`，并通过 `Program::methodSets` 生成稳定
`ScriptMethod` provenance；同函数内经 local/global/capture 保存再调用的静态方法值别名
也已进入该 contract，容器/指针传播及动态/反射 receiver 仍为
`IrExecutorPrototypeLegacy`。
该切片为 `Contract Complete`，不代表跨平台或生产验证完成。

继承链校验也已统一：legacy compiler 与 Typed-IR frontend 在展开脚本类字段、方法 schema
前使用访问集检测 cycle，并以确定性的 `TypeMismatch` 失败关闭；非法继承不会生成部分
AOT metadata。该边界为 `Contract Complete`，不改变合法单继承模型的既有语义。

动态调用仍默认 Legacy；`TypedIrCallableOrigin::ScriptFunction` 现在开放一个显式
source-level contract：导出表必须提供完整 `bindingId/targetId/signatureHash/fixedArity/
lifetimeVersion` metadata，且 target 为零 capture、固定 arity 的 `arp-fn:` 函数。对外部
source module，target 所属模块还必须出现在 Typed IR/AOT dependency table，并由
`AotModuleRegistry` 按 descriptor identity/version 解析。同步和 bounded pending-await
imported-function 路径已有 AOT/Interpreter/Hybrid focused regression；受声明依赖、静态
closure provenance 和 `maxContinuationFrames` 约束的 cross-module arbitrary-depth
closure-await graph 已完成 AOT/Interpreter/Hybrid 回归。任意动态 expression、未经证明的
容器/指针传播、动态或无界 cross-module async graph、完整 generator protocol 仍未完成；
已证明的 Native generator 子集与 generic cross-backend generator migration 已完成；
canonical continuation 现在同时携带 root generator 的 delegated child 槽位/恢复 PC，
因此 AOT parent -> Interpreter parent -> AOT parent 的 bounded child send/throw
组合已完成回归。固定 bounded `Native root -> Native middle -> Native inner -> ... -> proven leaf` 的五级
delegation 已完成 AOT/Hybrid 正反向迁移回归；第六级 Native delegation、复杂
Native handler/超过两层的 delegated-await 图仍继续 fail-closed。
generic AOT 与 Interpreter parent 的 bounded delegated child-await continuation 已补齐：parent 的 RangeNext
后继状态会在 child 首次 pending 时保存，后续 resume 对 child 单步推进并分别覆盖
yield、done、error 三条路径；canonical frame 会固化 RangeNext retry point 并保留已校验
child ownership，AOT -> Interpreter -> AOT -> Interpreter 两层迁移已回归；三层及以上
会在执行期和 adoption 期统一以 `AotSwitchUnsupported` 失败关闭。bounded caller-bearing
child chain 已纳入快照回归；固定参数前缀或确定性 capture-load 参数的 Native `yield from` frame-machine
（含零参数兼容形态、AOT/Interpreter generator child pending-await 迁移、参数化
direct `Call`、capture-bearing parent、带完整 provenance 的零 capture `MakeFuncValue -> CallValue`，以及静态可证明的
`list`/`dict`/`range`/`bytes` child）已完成
Contract Complete。参数必须按父 frame 的连续参数槽加载，child 必须是同模块、固定
arity、零 capture 的 generator 或同步集合构造；parent capture 只能通过 `LoadCapture`
作为确定性 child 参数来源，不能改变 child 的零 capture 要求；集合 child 不允许 pending-await。
现在额外允许一个固定 list/dict/range 参数表达式（字面量/已知 local loads +
`MakeList`/`MakeDict`/`MakeRange`/受限 `MakeRangeFromStack(1|2)`），并复用
`ScriptSemanticRuntime` 保持栈顺序、bound 消费、容量与诊断一致；任意动态 child、
计算型表达式/容器参数传播、更宽泛的 Native delegated-await 和复杂 frame 仍失败关闭。
`throw()` 已在 Interpreter 与 generic `IrExecutorPrototypeLegacy` generator 上完成
bounded handler/unwind contract；Native yield frame-machine 现在额外完成一个固定的
单层 handler shape（`PushHandler(5) -> ... -> Yield -> ... -> Return(1)`）及其
AOT/Interpreter 双向迁移，复杂 handler 仍以 `AotSwitchUnsupported` fail-closed。无 handler 的
Native generator 已能报告未捕获 `UserRaise`；已证明的单层/嵌套 await-handler
Native generator 已接入 throw unwind。generic safepoint generator migration
会先完成目标 backend adoption，再注入 `throw()`，避免重复推进 frame；其它
Native handler/复杂 frame、Native frame-machine child 与未证明的跨 backend 组合仍
fail-closed。
Interpreter 与 AOT 已补齐受限 `send()`/`close()` API，值只在已发生的 yield 边界消费；
Hybrid dispatcher 通过 `resumeGeneratorSendOn` 支持跨 backend 的非推进 adoption 后再消费
send 值；canonical frame 会保留 root delegated child 状态，已覆盖 AOT/Interpreter parent
与 AOT child 的双向 send/throw。Native await-yield terminal boundary（含已证明的 handler
形状）接受 send 并完成固定 terminal path；pending await、复杂 Native frame 和未证明/超过两层
delegated-await 图继续 deterministic fail-closed；两层 parent/child/grandchild pending-await 迁移已由
AOT -> Interpreter -> AOT -> Interpreter 双向回归覆盖。
异步 nested-handler await-yield 另有独立的线性 depth contract：`depth >= 2` 时只接受
固定 `PushHandler* -> await -> yield -> terminal`，inner handler 必须逐层
`LoadString -> Throw`，outer handler 必须固定 fallback return；handler target、栈深度、
fallback slot 和 await source-map label 均由 descriptor/Typed IR 确定性校验。两层旧
inner fallback 兼容路径、三层 rethrow、await failure、yield 后 `throw()` 和 AOT/Interpreter
adoption 已由 Windows MinGW AOT/Hybrid 回归覆盖。复杂或分叉 handler、typed filter、动态
target、handler-side await/yield 仍 fail-closed；该切片为 `Backend Validated`，不是
`Platform Validated` 或 `Production Ready`。
Generic `IrExecutorPrototypeLegacy` 的多层 async
caller chain 已按 `maxContinuationFrames` 完成语义回归；当前已补齐保守的静态别名
provenance：imported binding 可沿
local、module-init global、qualified module-field、静态 callable-list/callable-dict 常量索引和 closure capture 传播；复合赋值、覆盖和不一致控制流写入会清除事实，
条件分支及循环在所有可达路径写入同一 binding 时保留 Native，冲突路径不会发布为 Native；调用、索引写入以及 `await`/`yield` 会清除 callable-list/dict
事实，避免别名修改后的误判；Typed-IR closure provenance 对 list/dict/index 的栈效应保持 unknown，避免容器表达式截断外层 closure contract。capture-bearing child 仅通过已验证的 closure root 进入
Native，不作为零 capture standalone entry 发布。该切片为 `Contract Complete`，不代表
Backend/Platform/Production Ready。

异步 caller chain 现在有统一的 `maxContinuationFrames` contract：Interpreter snapshot、
AOT generator/await adoption 以及 Hybrid 配置共享同一上限；超过上限统一以
`ResourceLimitExceeded` 失败，native 生成入口在非默认预算下回到受管执行器。该边界是
`Contract Complete`；同模块及声明依赖内、静态 provenance 且满足固定 closure-await
shape 的 arbitrary-depth chain 已进入受预算约束的 Native frame-machine，并已有
AOT -> Interpreter -> AOT Hybrid caller-promotion/no-replay 回归；动态、无界和未证明
shape 仍不宣称完成。

2026-08-20：inline scalar cooperative OSR 的 locals snapshot 增加显式
`int`/`bool` kind，并覆盖 `StoreLocal` 写入、tail caller 保存、Interpreter
adoption 及 malformed metadata fail-closed 测试；该增量仍为 `Contract Complete`，
不提升跨平台或生产状态。

2026-08-25：Hybrid generator close forwarding 收敛为显式
`closeGeneratorOn` contract。当前 backend 的 close 会递归关闭 delegated child；
跨 backend close 只在 canonical yielded/await safepoint adoption 成功后执行，
不会推进或重放 generator。跨 backend adoption 后保留的 AOT child 通过
`LegacyGeneratorBridge` 关闭并验证 owner 生命周期；无效值、无 owner 或未证明
的 safepoint 均返回稳定诊断并保留原 continuation；pending-await close 只丢弃
continuation，不 poll 或声称 awaitable cancellation。Windows Debug Hybrid unit
已覆盖 AOT parent -> Interpreter close 及 child forwarding；Windows
Debug/Release 与 Web Debug/Release 均在显式重建后通过对应 Hybrid CTest。
专用 `arp_scriptvm_hybrid_generator_close_smoke` 也在四个 lane 通过；完整
Python protocol、复杂 adoption 和整体 Production Ready 状态保持开放。

2026-08-25：运行时 generator bridge 收敛为固定容量的 composite provider
registry。DirectNative 通过可选 weak hook 注册 provider，保持
`arp_scriptvm_aot_only_link_smoke` 的最小链接闭包；Interpreter 同时注册自身
provider。Windows Debug/Release 与 Web Debug/Release semantic/v5 smoke 验证了
AOT child -> Interpreter parent 的 resume、send、handler-caught throw、child
completion、close 和 no-replay。Interpreter child -> DirectNative、跨 module
identity negotiation、
delegated await/throw 以及完整 Python protocol 仍未关闭，因此 F7、Hybrid 和
Production Ready 状态不变。

2026-08-25：反射 Value 的大对象和非平凡数组 storage policy 统一改用
`core::defaultCpuMemoryResource()`，移除裸 `new/delete`。因此 DirectNative
执行期间由 `CpuMemoryQuotaScope` 产生或持有的 `Value`、容器元素和
`ObjectRef` 控制块都进入同一 live/peak 计费域；配额失败通过 `bad_alloc` 传播到
稳定的 `ResourceLimitExceeded`，释放后 quota 归零。Windows Debug/Release
reflection unit、v5 contract、AOT-only link 和 generated-package smoke 均通过。
该修复补齐内存契约，不扩大 generator、Hybrid 或任意-PC deoptimization 能力。

**范围**：补齐 ArpScript 对标 goes VM 尚未覆盖的 7 项语言特性（接口类型除外）。

**目标状态**：
- F1–F6：`Backend Validated`（真实 VM 执行 + e2e 测试）
- F7：`Prototype Complete`（基本 yield + for 迭代；bounded `yield from`、`send()`/`close()`、generic `throw()`、delegated-child forwarding、已证明 Native await-handler 子集与 generic cross-backend migration 为 Contract Complete 子集；复杂 Native/delegated 组合仍 fail-closed）

**实施顺序**：F1 → F2 → F3 → F4 → F5 → F6 → F7（从简到繁，每 Phase 独立可测试）。

---

## 调研结论总览

| 特性 | 已有基础设施 | 需要新增 |
|------|-------------|---------|
| F1 String methods | `Field -> LoadField -> CallValue` 分发链完整；`LoadField` 已支持 reflection instance 方法查找 | `reflection.cpp` str 块注册命名方法 |
| F2 Type assertions | `ConvertInt/Int64/Float/String/Bool` opcode + handler 全部已实现（`vm.cpp:1231-1292`），但 compiler 从不 emit | parser `as` 表达式 + compiler emit |
| F3 Dict iteration | `RangeNext` dict path 已完整（`vm.cpp:1214-1226`），keySlot+valSlot 通过 packed i64 传递 | parser 元组绑定 + compiler 独立 keySlot |
| F4 Lambda | `ExprKind::Lambda` AST 已存在（`ast.h:81`），有 paramNames/paramTypes/retType 字段；compiler/sema 有 stub | parser 入口 + compiler 实现（复用 FuncDecl capture/closure 机制） |
| F5 Variadic | 无 | parser `*` + AST 标志 + sema 规则 + VM 参数打包 |
| F6 try/except | `VmError` + `error_exit` 路径 + `ScriptDiagnosticCode` 完整 | KwTry/KwExcept/KwRaise + AST + PushHandler/PopHandler/Throw opcode + Frame handler stack |
| F7 Generators | `Yield` opcode + existing suspension machinery | KwYield + parser + `ScriptGenerator` Value 类型 + generator frame resume + RangeNext generator path |

---

## F1: String Methods

**实际状态**：Backend Validated（core 工具单元、ScriptVM e2e contract、e2e smoke 已通过）。

**状态目标**：Backend Validated

### 原理

ArpScript 的方法调用经 `Field -> LoadField -> CallValue` 分发。`LoadField`（`vm.cpp`）已支持 reflection instance 的方法查找。只需在 reflection 层为 `str` 类型注册命名方法，`"hello".upper()` 即可工作。

### 语法

```python
"Hello World".upper()          # -> "HELLO WORLD"
"  trim  ".strip()             # -> "trim"
"a,b,c".split(",")             # -> ["a", "b", "c"]
"-".join(["a", "b", "c"])      # -> "a-b-c"
"hello".find("ll")             # -> 2 (未找到 -1)
"hello".replace("l", "L")      # -> "heLLo"
"hello".contains("ell")        # -> true
"hello".startswith("he")       # -> true
"hello".endswith("lo")         # -> true
"{0} + {1}".format(1, 2)      # -> "1 + 2"
```

### 改动文件

1. **`src/reflection/reflection.cpp`** — `registerBuiltinReflectionTypes()` 的 str 块（line 1104-1130），在现有 `.method(...)` 链末尾追加：
   - `upper` / `lower`：std::transform 转大小写
   - `strip`：去首尾空白
   - `split(sep)`：返回 `SharedPtr<hlist>`（PMR allocator）
   - `join(parts)`：self 是分隔符，join list
   - `find(sub)`：返回 int 索引，未找到 -1
   - `replace(old, new)`：返回新 String
   - `contains(sub)`：返回 bool
   - `startswith(sub)` / `endswith(sub)`：返回 bool
   - `format(args...)`：简化版 `{0}/{1}` 占位符替换

   **关键约束**：
   - 返回的新 `core::pmr::String` 必须用 `&core::defaultCpuMemoryResource()` 构造
   - 返回 list 用 `SharedPtr<arp::reflection::hlist>`
   - 参考 list 块（line 1085-1102）的命名方法注册模式

2. **`src/core/string_utils.h` / `src/core/string_utils.cpp`**（可能新增）— 先查 `src/core/` 是否已有 split/join/trim 等工具。若无，在此沉淀可复用算法（engine-wide constraints 约束：算法类代码必须沉淀到 core）。

### 测试

`tests/contract/scriptvm_e2e_contract_tests.cpp` 追加 `testStringMethods()`：
- `"hello".upper()` -> `"HELLO"`
- `"a,b,c".split(",")` -> `["a","b","c"]`
- `"-".join(["a","b"])` -> `"a-b"`
- `"hello".replace("l","L")` -> `"heLLo"`
- `"hello".contains("ell")` -> `true`

---

## F2: Type Assertions / Conversions

**实际状态**：Backend Validated（parser unit、真实 VM e2e contract、BadCast 负向断言、e2e smoke 已通过）。

**状态目标**：Backend Validated

### 原理

`ConvertInt/ConvertInt64/ConvertFloat/ConvertString/ConvertBool` opcode 及 handler 已全部实现（`vm.cpp:1231-1292`），但 compiler 从不 emit。只需在 parser 层添加 `expr as Type` 语法，compiler 根据 Type 选择对应 opcode。

### 语法

```python
"42" as int          # -> 42
3.14 as int          # -> 3
42 as string         # -> "42"
0 as bool            # -> false
100 as int64         # -> 100 (int64)
"abc" as int         # -> BadCast 运行时错误
```

### 改动文件

1. **`src/scriptvm/compile/ast.h`** — `ExprKind` 新增 `AsCast`（在 `Assign` 之后）；`Expr` struct 新增 `TypePtr castType`。

2. **`src/scriptvm/compile/parser.cpp`** — 在 `parseBinary` 与 `parseUnary` 之间插入 `parseAsCast()`：
   ```
   parseAsCast() {
       expr = parseUnary()
       if (check(KwAs)) {
           advance()
           type = parseType()  // int/int64/float/string/bool
           node = makeExpr(ExprKind::AsCast)
           node->left = expr
           node->castType = type
           return node
       }
       return expr
   }
   ```
   **注意**：`as` 在 import 语句中已用作 alias（`from X import a as b`）。但 import 解析在 `parseFromImport()` 内局部消费 `KwAs`，不会进入表达式解析。`as` 在表达式上下文中是类型断言。两者不冲突。

3. **`src/scriptvm/compile/compiler.cpp`** — `compileExpr` 新增 `case ExprKind::AsCast`：
   ```cpp
   case ExprKind::AsCast: {
       compileExpr(comp, fc, expr->left);  // push value
       if (name == "int")        emit(Opcode::ConvertInt);
       else if (name == "int64") emit(Opcode::ConvertInt64);
       else if (name == "float") emit(Opcode::ConvertFloat);
       else if (name == "string") emit(Opcode::ConvertString);
       else if (name == "bool")   emit(Opcode::ConvertBool);
       break;
   }
   ```

4. **`src/scriptvm/compile/sema.cpp`** — `inferExpr` 新增 `case ExprKind::AsCast`：返回 `expr->castType`。

### 测试

- `"42" as int` -> 42
- `3.14 as int` -> 3
- `42 as string` -> "42"
- `0 as bool` -> false
- `"abc" as int` -> BadCast 运行时错误

---

## F3: Dict Iteration — `for k, v in dict:`

**实际状态**：Backend Validated（parser/sema unit、真实 VM e2e contract、e2e smoke 已通过）。

**状态目标**：Backend Validated

### 原理

VM 的 `RangeNext`（`vm.cpp:1214-1226`）已完整支持 dict 迭代，keySlot 和 valSlot 通过 `packedRangeTargets(inst.i64, keySlot, valSlot)` 传递。当前 compiler 把 keySlot 和 valSlot 都设为 iterSlot（`compiler.cpp:1077`），即只取 value 不取 key。需要 parser 支持元组绑定，compiler 分配独立 keySlot。

### 语法

```python
# 元组绑定：同时获取 key 和 value
for k, v in my_dict:
    print(k, v)

# 单变量：只获取 value（现有行为，保持兼容）
for v in my_dict:
    print(v)

# list 也支持元组绑定：key 是索引
for i, v in my_list:
    print(i, v)
```

### 改动文件

1. **`src/scriptvm/compile/ast.h`** — `Stmt` struct 新增：
   ```cpp
   core::pmr::String iterKeyName { &core::defaultCpuMemoryResource() };  // for k, v in: k 的名字
   TypePtr keyType;  // 可选类型注解
   ```

2. **`src/scriptvm/compile/parser.cpp`** — `parseForStmt()`（line 955）修改：
   ```
   // 解析 for 后的第一个标识符
   ident = current().text; advance()
   // 检查是否是 "for k, v in" 模式
   if (check(Tok::Comma)) {
       advance()
       // 第一个是 keyName，解析第二个标识符作为 iterName (value)
       s->iterKeyName = ident
       if (!check(Tok::Ident)) error
       s->iterName = current().text; advance()
   } else {
       // 单变量模式: for v in (现有行为)
       s->iterName = ident
   }
   ```

3. **`src/scriptvm/compile/compiler.cpp`** — `StmtKind::For` case（line 1055）修改：
   ```cpp
   int rangeSlot = fc.allocSlot("__range");
   int indexSlot = fc.allocSlot("__index");
   int iterSlot = fc.allocSlot(stmt->iterName);  // value slot
   int keySlot = -1;
   if (!stmt->iterKeyName.empty()) {
       keySlot = fc.allocSlot(stmt->iterKeyName);  // 独立 key slot
   }
   // RangeNext: keySlot 和 valSlot 不再相同
   int valSlot = iterSlot;
   std::int64_t packed = (static_cast<std::int64_t>(keySlot >= 0 ? keySlot : valSlot) << 32)
                       | static_cast<std::uint32_t>(valSlot);
   fc.emit(Opcode::RangeNext, rangeSlot, indexSlot, packed);
   ```
   当 `keySlot >= 0` 时，list 迭代也会把索引填入 keySlot（`vm.cpp:1209` 已有此行为），dict 迭代会把 key 填入 keySlot（`vm.cpp:1222`）。

### 测试

- `for k, v in {1: "a", 2: "b"}:` 收集 k+v 对
- 单变量 `for v in dict:` 仍只取 value（回归测试）
- list 元组绑定 `for i, v in [10, 20]:` -> i=0,v=10 / i=1,v=20

---

## F4: Lambda Expressions

**实际状态**：Backend Validated（parser unit、直接/捕获 lambda e2e、e2e smoke 已通过）。

**状态目标**：Backend Validated

### 原理

`ExprKind::Lambda` AST 节点已存在，有 paramNames/paramTypes/retType 字段。Compiler 已有完整的嵌套函数编译机制（FuncDecl case，`compiler.cpp:1113+`）：capture analysis -> AddrLocal 提升 -> CreateClosure。Lambda 本质是匿名 FuncDecl，可直接复用这套机制。

### 语法

```python
# 基本 lambda
var f = lambda(x: int) -> int: x * 2
# f(21) -> 42

# 多参数
var add = lambda(a: int, b: int) -> int: a + b

# 闭包捕获
def makeAdder(n: int) -> any:
    return lambda(x: int) -> int: x + n
# adder = makeAdder(10); adder(5) -> 15
```

**语法设计**：
- `lambda` 关键字 + `(...)` 参数列表（带类型）+ `-> RetType` + `:` + 单表达式 body
- body 是单表达式，值即返回值（无 return 关键字）
- 类型可省略时由 sema 推断（Phase F4 先要求显式类型，推断作为后续优化）

### 改动文件

1. **`src/scriptvm/compile/lexer.h` / `lexer.cpp`** — 新增 `KwLambda`：
   - `Tok` enum 新增 `KwLambda`
   - keyword table: `{"lambda", Tok::KwLambda}`

2. **`src/scriptvm/compile/parser.cpp`** — `parsePrimary()` 新增 `case Tok::KwLambda:` -> `parseLambda()`：
   ```
   parseLambda() {
       advance() // consume 'lambda'
       expect '('
       parse params (name: type, ...)  // 复用 parseParamList 逻辑
       expect ')'
       expect '->'
       retType = parseType()
       expect ':'
       body = parseExpr()  // 单表达式
       // 构造 Lambda AST: paramNames, paramTypes, retType, left = body expr
   }
   ```
   注意：`:` 后的 body 是单行表达式，不触发 INDENT/DEDENT（因为 `:` 后同行有内容，lexer 不会生成 NEWLINE）。

3. **`src/scriptvm/compile/ast.h`** — Lambda 已有字段（paramNames/paramTypes/retType），body 存入 `left`（表达式）。

4. **`src/scriptvm/compile/sema.cpp`** — `inferExpr` 新增 `case ExprKind::Lambda`：
   - 创建新 scope，声明参数
   - 推断 body 表达式类型，验证与 retType 一致
   - 返回 Func 类型
   - 弹出 scope

5. **`src/scriptvm/compile/compiler.cpp`** — `compileExpr` 新增 `case ExprKind::Lambda`：
   - 生成匿名函数名 `__lambda_N`
   - 复用 FuncDecl 的编译逻辑：
     1. 创建新 Function，设置 params/locals
     2. 编译 body 表达式 + emit Return
     3. Capture analysis：扫描 body 引用的外层变量
     4. emit AddrLocal（提升捕获的 local 为 cell）
     5. emit CreateClosure（创建 ScriptClosure Value）
   - 将 Lambda 函数注册到 `Program::functions`

### 测试

- `var f = lambda(x: int) -> int: x * 2`；`f(21)` -> 42
- 闭包捕获：`def makeAdder(n: int) -> any: return lambda(x: int) -> int: x + n`

---

## F5: Variadic Functions — `*args`

**实际状态**：Backend Validated（parser unit、真实 VM e2e、非法参数位置负向断言、e2e smoke 已通过）。

**状态目标**：Backend Validated

### 原理

ArpScript 当前函数调用的参数传递在 `Call`/`CallValue` handler 中逐个复制到 frame.locals。Variadic 函数的最后一个命名参数 `*args` 收集所有超出固定参数的参数为一个 list。

### 语法

```python
def sum_all(base: int, *args: int) -> int:
    var total: int = base
    for v in args:
        total += v
    return total

# sum_all(1, 2, 3, 4) -> 10
# sum_all(1) -> 1  (args 为空 list)
```

**设计约束**：
- `*args` 必须是最后一个参数
- `*args` 的类型是 `list<T>`（T 为注解类型），运行时收集为 `hlist`
- 不支持 `**kwargs`（需要关键字参数机制，标记为未来工作）

### 改动文件

1. **`src/scriptvm/compile/lexer.h` / `lexer.cpp`** — `*` 已是 `Tok::Star`，无需新增 token。但在参数解析中识别 `*` 前缀。

2. **`src/scriptvm/compile/ast.h`** — `Stmt` struct 新增：
   ```cpp
   bool variadicParam = false;        // 是否有 *args 参数
   int variadicParamIndex = -1;       // *args 在 paramNames 中的索引
   ```

3. **`src/scriptvm/compile/parser.cpp`** — `parseFuncDecl()` 的参数列表解析中：
   ```
   if (check(Tok::Star)) {
       advance()
       // 下一个 Ident 是 *args 的名字
       s->variadicParam = true
       s->variadicParamIndex = paramCount
   }
   ```

4. **`src/scriptvm/compile/sema.cpp`** — 函数声明分析中：
   - 验证 `*args` 是最后一个参数
   - `*args` 的类型是 `list<T>`
   - 调用时允许参数数 >= 固定参数数

5. **`src/scriptvm/interpreter/bytecode.h`** — `Function` struct 新增：
   ```cpp
   int fixedParams = 0;       // 固定参数数量
   bool isVariadic = false;   // 是否 variadic
   ```

6. **`src/scriptvm/compile/compiler.cpp`** — 函数编译时设置 `fn.fixedParams` 和 `fn.isVariadic`。

7. **`src/scriptvm/interpreter/vm.cpp`** — `runFunctionInternal()`（line 281）参数复制逻辑修改：
   ```cpp
   int n = args.size();
   int fixedN = fn.fixedParams;
   // 复制固定参数
   for (int i = 0; i < min(n, fixedN); ++i)
       frame.locals[i] = args[i];
   // 如果 variadic，收集剩余参数为 list
   if (fn.isVariadic) {
       auto variadicList = core::makeShared<arp::reflection::hlist>();
       for (int i = fixedN; i < n; ++i)
           variadicList->push_back(args[i]);
       frame.locals[fixedN] = Value { variadicList };  // *args slot
   }
   ```
   `Call`/`CallValue` handler 的参数数量检查也需要调整：variadic 函数允许 args >= fixedParams。

### 测试

- `def sum_all(base: int, *args: int) -> int:` 收集求和：`sum_all(1, 2, 3, 4)` -> 10
- 空变参：`sum_all(1)` -> 1（args 为空 list）

---

## F6: try/except/raise Error Handling

**实际状态**：Backend Validated（parser unit、真实 VM e2e contract、嵌套 re-raise、e2e smoke 已通过）。

**状态目标**：Backend Validated

### 原理

VM 当前通过 `VmError` + `goto error_exit` 传播错误，不可被脚本捕获。设计：在 `Frame` 上新增 handler stack，`try` 块入口 emit `PushHandler handlerPc`，出口 emit `PopHandler`。`error_exit` 路径检查 handler stack：有匹配 handler 则清除 err、push 错误值、jump 到 handler PC。

### 语法

```python
# 基本捕获
try:
    risky()
except as e:
    handle(e)   # e 绑定为错误消息字符串

# 无绑定捕获
try:
    risky()
except:
    handle_any()

# 主动抛出
raise "something went wrong"
raise 42

# 嵌套 try
try:
    try:
        risky_inner()
    except as e:
        raise "re-thrown: " + e
except as e:
    handle_outer(e)
```

**设计约束**：
- Phase F6 实现 `except as e:`（catch all，e=错误消息字符串）和 `except:`（不绑定）
- 错误码匹配（如 `except IndexError:`）已实现为稳定
  `ScriptDiagnosticCode` filter，支持按源顺序匹配多个 clause；未知名称、
  重复 typed filter 和非末尾 catch-all 在 sema 阶段确定性失败。任意异常
  对象类型和任意 handler-side rethrow 仍属于后续增强；AOT 已额外提供同步
  两层 catch-all rethrow 的受限 Native Value contract，复杂/异步图仍保持 Legacy。

### 改动文件

1. **`src/scriptvm/compile/lexer.h` / `lexer.cpp`** — 新增 `KwTry`、`KwExcept`、`KwRaise`：
   - keyword table: `{"try", KwTry}, {"except", KwExcept}, {"raise", KwRaise}`

2. **`src/scriptvm/compile/ast.h`**：
   - `StmtKind` 新增 `Try`、`Raise`
   - `Stmt` struct 新增：
     ```cpp
     // For TryStmt
     core::pmr::Vector<StmtPtr> tryBody { &core::defaultCpuMemoryResource() };
     struct ExceptClause {
         core::pmr::String errorName;     // "IndexError" 等，空 = catch all
         core::pmr::String varName;       // "as e" 中的 e，空 = 不绑定
         core::pmr::Vector<StmtPtr> body { &core::defaultCpuMemoryResource() };
     };
     core::pmr::Vector<ExceptClause> exceptClauses { &core::defaultCpuMemoryResource() };
     // For RaiseStmt (reuses expr)
     ```

3. **`src/scriptvm/compile/parser.cpp`** — `parseStmt()` 新增 `case Tok::KwTry:` -> `parseTryStmt()`，`case Tok::KwRaise:` -> `parseRaiseStmt()`。
   - `parseTryStmt()`：`try : block except [ErrorName] [as Ident] : block (elif except...)*`
   - `parseRaiseStmt()`：`raise expr`

4. **`src/scriptvm/runtime/diagnostics.h`** — 新增 `ScriptDiagnosticCode::UserRaise`（用户主动抛出的错误）。

5. **`src/scriptvm/interpreter/bytecode.h` / `bytecode.cpp`** — 新增 3 个 opcode：
   ```cpp
   PushHandler,   // A = handler PC; push handler onto frame's handler stack
   PopHandler,    // pop one handler from stack (try block exited normally)
   Throw,         // pop value, set err = UserRaise + value, goto error_exit
   ```

6. **`src/scriptvm/interpreter/frame.h`** — `Frame` struct 新增：
   ```cpp
   struct HandlerInfo {
       int handlerPc = 0;
       int stackDepth = 0;  // operand stack depth at push time (for unwinding)
   };
   arp::core::pmr::Vector<HandlerInfo> handlers { &arp::core::defaultCpuMemoryResource() };
   ```

7. **`src/scriptvm/interpreter/vm.cpp`**：
   - `PushHandler` handler：记录 handler PC + 当前 stack depth，push 到 `frame.handlers`
   - `PopHandler` handler：`frame.handlers.pop_back()`
   - `Throw` handler：pop value，设置 `err.code = UserRaise`，`err.message = value 的字符串表示`，`goto error_exit`
   - **`error_exit` 路径修改**（关键改动）：
     ```cpp
     error_exit:
         if (!frame.handlers.empty() && err.code != ScriptDiagnosticCode::None) {
             auto handler = frame.handlers.back();
             frame.handlers.pop_back();
             // Restore operand stack to depth at push time
             while (frame.stack.size() > handler.stackDepth)
                 frame.pop();
             // Push error value for except block
             Value errVal = makeString(err.message);
             frame.push(errVal);
             // Clear error
             err.code = ScriptDiagnosticCode::None;
             err.message.clear();
             // Jump to handler
             frame.ip = handler.handlerPc;
             goto dispatch;  // re-enter loop
         }
         buildTraceback(program, err);
         return results;
     ```
   - **跨 frame 传播**：当 `Call`/`CallValue` 调用 callee 返回错误时（`vm.cpp:583-584`），caller 的 `error_exit` 会检查 caller 的 handler stack。如果 callee 无 handler，错误传播到 caller；caller 有 handler 则捕获。这是正确的 unwinding 行为。

8. **`src/scriptvm/compile/compiler.cpp`** — `StmtKind::Try` 编译：
   ```
   // try block
   int handlerPc = <placeholder>
   fc.emit(PushHandler, handlerPc);  // A = handler PC (patch later)
   compileBlock(tryBody)
   fc.emit(PopHandler);
   fc.emit(Jump, afterTry);

   // handler (except clauses)
   patchPushHandler(handlerPc, current_pc);
   // 编译 except 匹配逻辑：
   //   绑定 errVal 到 varName (StoreLocal)
   //   执行 except body
   //   PopHandler (try 块从 handler 角度已结束)
   //   Jump afterTry
   ```

9. **`src/scriptvm/compile/sema.cpp`** — 分析 Try/Raise：
   - Try body 在新 scope 中分析
   - except 子句的 varName 在新 scope 中声明为 string 类型
   - Raise expr 可以是任意类型

### 测试

- `try: a/b except as e: return -1`（DivisionByZero 捕获）
- `raise "msg"` + try 捕获
- 嵌套 try 的 re-raise

---

## F7: Generators / Coroutines

**实际状态**：Prototype Complete（parser unit、真实 VM e2e generator contract、hidden smoke 已通过）。

**状态目标**：Prototype Complete

### 原理

`Yield` opcode 和基础 suspension machinery 已存在。F7 让 generator 函数创建可恢复的 `ScriptGenerator` frame，新增 `resumeGenerator`，让 `RangeNext` 消费 generator，并让 generator 函数调用返回 generator 对象而不是直接执行。

**状态声明**：`Prototype Complete` — 基本的 `yield` + `for v in gen():` 迭代可用。
`yield from` 已完成一个 bounded iterator-value delegation 子集：Interpreter 与 generic
AOT 都通过 `RangeInit -> RangeNext -> Yield` 转发 child generator/iterable 的 yielded
values，并在 child 耗尽后继续执行 parent；该子集为 `Contract Complete`，不等同于完整
generator protocol。`send()`/`close()` 已在 Interpreter 与 AOT 暴露受限、可测试的 API：
send 作用于已经 yield 且没有 pending await 的 generator；有 resumable delegated child
时会转发给 child，否则确定性失败；Hybrid canonical adoption 也会保留该 root delegated
状态并完成 AOT/Interpreter 双向 send/throw。close 使其确定性耗尽并转发给 child；pending-await
close 只丢弃 continuation，不暗示 awaitable cancellation。`throw()` 已支持 Interpreter 与 generic AOT 的当前 frame
在 yield 边界注入 `UserRaise`，并复用既有 handler/filter/unwind 语义；首次 resume、
pending await、Native handler/复杂 frame 的 throw 以及不支持的委托 child 组合继续以
`AotSwitchUnsupported`/等价诊断失败关闭。

### 语法

```python
def gen() -> int:
    yield 1
    yield 2
    yield 3

def test() -> int:
    var total: int = 0
    for v in gen():
        total += v
    return total  # -> 6
```

### 改动文件

1. **`src/scriptvm/compile/lexer.h` / `lexer.cpp`** — 新增 `KwYield`：
   - keyword table: `{"yield", Tok::KwYield}`

2. **`src/scriptvm/compile/ast.h`**：
   - `StmtKind` 新增 `Yield`（`yield expr` 作为语句）
   - Phase F7 先实现语句级 yield（`yield expr`），不支持 `x = yield` 表达式

3. **`src/scriptvm/interpreter/bytecode.h`** — `Function` struct 新增 `bool isGenerator = false`

4. **`src/scriptvm/compile/parser.cpp`** — `parseStmt()` 新增 `case Tok::KwYield:` -> `parseYieldStmt()`：
   ```
   yield expr?
   ```

5. **`src/scriptvm/compile/sema.cpp`** — 函数分析中，如果函数体含 Yield 语句，标记 `func->isGenerator = true`。Generator 函数的返回类型注解是 yield 的元素类型（如 `-> int` 表示 yield int）。

6. **`src/scriptvm/runtime/value.h` / `value.cpp`** — 新增 `ScriptGenerator`：
   ```cpp
   struct ScriptGenerator {
       Frame frame;              // suspended frame (ip, locals, captures, stack)
       const Program* program = nullptr;
       int funcIndex = -1;
       bool done = false;
       bool started = false;
   };
   ```
   Value 新增 `isGenerator()` / `ptr<ScriptGenerator>()` 访问器。
   reflection 注册 ScriptGenerator 类型（在 `registerScriptObjectReflection()` 或新函数中）。

7. **`src/scriptvm/interpreter/vm.cpp`**：
   - **Call/CallValue 修改**：当 callee 是 generator 函数（`fn.isGenerator`），不执行，而是创建 ScriptGenerator 对象返回：
     ```cpp
     if (calleeFn.isGenerator) {
         auto gen = core::makeShared<ScriptGenerator>();
         gen->program = &program;
         gen->funcIndex = funcIndex;
         gen->frame = Frame(&calleeFn);
         // copy args to gen->frame.locals
         gen->started = false;
         frame.push(Value { gen });
         break;  // don't call runFunctionInternal
     }
     ```
   - **Yield handler修改**（`vm.cpp:1364`）：
     ```cpp
     case Opcode::Yield: {
         Value v = frame.pop();
         results.push_back(v);
         err.code = ScriptDiagnosticCode::Suspended;
         err.message = "yielded";
         goto normal_exit;
     }
     ```
     关键：frame 是 by-value 参数，调用者（resumeGenerator）持有 frame 的副本。`normal_exit` 返回后，frame 的 ip 已更新（ip 指向 Yield 之后的指令）。

   - **新增 `executeFrameRef(Frame& frame, ...)`**：by-reference 版本，仅 generator resume 使用（解决 executeFrame by-value 导致 ip 丢失问题）。普通调用路径不变。

   - **`resumeGenerator(ScriptGenerator& gen, VmError* err)`**：
     ```cpp
     Value resumeGenerator(ScriptGenerator& gen, VmError* err) {
         if (gen.done) return makeNil();
         if (!gen.started) {
             gen.started = true;
         }
         // 继续执行 gen.frame（by-reference）
         auto results = executeFrameRef(gen.frame, *gen.program, *err);
         if (err->code == ScriptDiagnosticCode::Suspended) {
             err->code = ScriptDiagnosticCode::None;  // yield 不是错误
             gen.done = false;
             return results.front();  // yielded value
         }
         gen.done = true;
         return results.empty() ? makeNil() : results.front();
     }
     ```

   - **RangeNext generator path**（`vm.cpp:1185` 之前添加）：
     ```cpp
     if (src.is<SharedPtr<ScriptGenerator>>()) {
         auto& gen = *src.ptr<SharedPtr<ScriptGenerator>>();
         VmError yieldErr;
         Value yielded = resumeGenerator(gen, &yieldErr);
         if (gen.done) {
             hasMore = false;
         } else {
             if (valSlot >= 0 && valSlot < frame.locals.size())
                 frame.locals[valSlot] = yielded;
             if (keySlot >= 0 && keySlot < frame.locals.size())
                 frame.locals[keySlot] = makeInt(idx);  // 序号作为 key
             hasMore = true;
         }
         break;
     }
     ```

8. **`src/scriptvm/compile/compiler.cpp`**：
   - `StmtKind::Yield` 编译为 `emit(Opcode::Yield)` + push 值到 stack
   - 函数编译时，如果 sema 标记了 isGenerator，设置 `fn.isGenerator = true`

### 测试

- `def gen() -> int: yield 1; yield 2; yield 3` + `for v in gen(): total += v` -> 6

---

## 文件改动总览矩阵

| 文件 | F1 | F2 | F3 | F4 | F5 | F6 | F7 |
|------|----|----|----|----|----|----|----|
| `src/reflection/reflection.cpp` | ✏️ | | | | | | |
| `src/scriptvm/compile/lexer.h/.cpp` | | ✏️ | | ✏️ | | ✏️ | ✏️ |
| `src/scriptvm/compile/ast.h` | | ✏️ | ✏️ | | ✏️ | ✏️ | ✏️ |
| `src/scriptvm/compile/parser.cpp` | | ✏️ | ✏️ | ✏️ | ✏️ | ✏️ | ✏️ |
| `src/scriptvm/compile/sema.cpp` | | ✏️ | | ✏️ | ✏️ | ✏️ | ✏️ |
| `src/scriptvm/compile/compiler.cpp` | | ✏️ | ✏️ | ✏️ | ✏️ | ✏️ | ✏️ |
| `src/scriptvm/interpreter/bytecode.h/.cpp` | | | | | ✏️ | ✏️ | ✏️ |
| `src/scriptvm/interpreter/frame.h` | | | | | | ✏️ | |
| `src/scriptvm/interpreter/vm.cpp` | | | | ✏️ | ✏️ | ✏️ | ✏️ |
| `src/scriptvm/runtime/value.h/.cpp` | | | | | | | ✏️ |
| `src/scriptvm/runtime/diagnostics.h` | | | | | | ✏️ | |
| `tests/contract/scriptvm_e2e_contract_tests.cpp` | ✏️ | ✏️ | ✏️ | ✏️ | ✏️ | ✏️ | ✏️ |
| `tests/unit/scriptvm_parser_tests.cpp` | | ✏️ | ✏️ | ✏️ | ✏️ | ✏️ | ✏️ |
| `docs/scriptvm/design.md` | ✏️ | ✏️ | ✏️ | ✏️ | ✏️ | ✏️ | ✏️ |
| `src/core/string_utils.h/.cpp` (可能新增) | ✏️ | | | | | | |

---

## 风险与对策

1. **try/except Frame by-value 问题**：`executeFrame` 接收 Frame by value/move（为避免 `frames_` reallocate 悬空引用）。try/except 的 handler stack 存在 Frame 上，by-value 传递时 handler stack 随 frame 移动，不影响正确性。error_exit 检查的是 executeFrame 内的 frame 副本的 handlers — 这与当前 frame 一致（move 后只有一个所有者）。✅ 无风险。

2. **Generator 的 frame 持久化**：Yield 时需保存 frame ip/locals/captures/stack。`executeFrame` by-value 导致 ip 更新丢失。**对策**：F7 新增 `executeFrameRef(Frame& frame, ...)` by-reference 版本，仅 generator resume 使用。普通调用路径不变。

3. **Lambda 闭包与 capture analysis 复用**：Lambda 编译复用 FuncDecl 的 capture analysis 逻辑。需确保匿名函数的 capture 扫描正确（与命名嵌套函数一致）。**对策**：提取 capture analysis 为独立函数，FuncDecl 和 Lambda 共用。

4. **`as` 语法歧义**：`as` 在 import 中用作 alias，在表达式中用作类型断言。**对策**：parser 在 import 语句内局部消费 `KwAs`，表达式解析中 `KwAs` 只可能是类型断言。两者在不同 parser 路径中，不冲突。✅ 无风险。

5. **向后兼容**：所有新增功能是独立的语法/语义扩展，不影响现有 14 个测试套件。新 opcode 不与现有 opcode 冲突。✅ 无风险。

6. **core::pmr 分配约束**：所有新增的动态容器、字符串、shared_ptr 对象必须走 `core::pmr` / `core::makeShared` / `core::makeUnique`。`ScriptGenerator` 作为 `reflection::object` 的值存储，底层 variant storage 仍通过 `core::makeShared` 分配。

---

## 验收标准

- [x] F1: `"hello".upper()` 等 10+ 字符串方法端到端可用
- [x] F2: `"42" as int` -> 42，5 种类型转换端到端可用
- [x] F3: `for k, v in dict:` 元组绑定端到端可用
- [x] F4: `lambda(x: int) -> int: x*2` 匿名闭包 + 捕获端到端可用
- [x] F5: `def foo(a: int, *args: int):` 变参收集端到端可用
- [x] F6: `try/except/raise` 错误捕获端到端可用
- [x] F7: `yield` + `for v in gen():` generator 迭代端到端可用
- [x] 现有 14 个测试套件全部继续通过（无回归）
- [x] `build.py` 编译 + 全部测试通过（MinGW + Ninja）
- [x] `docs/scriptvm/design.md` 更新，每个特性标注状态

---

## 执行顺序

F1 → F2 → F3 → F4 → F5 → F6 → F7

从简单到复杂，每个 Phase 独立可测试。每完成一个 Phase：编译 + 跑测试 + 更新 design.md + 继续下一 Phase。

**2026-08-25：bounded reverse pending-await injection slice**：DirectNative
parent 通过显式 `GeneratorBridgeV3` module-export contract 消费 Interpreter
child 时，现可在 child 的 pending-await boundary 转发 `send()`/`throw()`。
Interpreter awaitable 必须提供 `ScriptAwaitable::inject` callback；注入结果在
原 await boundary 直接消费，避免重复 poll/replay，并在 adoption、close、child
completion 时清理 capability/result 状态。该 slice 已由真实 VM-owned child 的
no-replay unit 覆盖；不扩大到 delegated await/throw、动态或循环 graph、完整
generator protocol、任意-PC deoptimization，整体 F7/Hybrid 仍为
`Implementation In Progress`。

**2026-08-25：DirectNative delegated-child canonical fail-closed**：审计发现
`yield from` 父帧在 child 产出后会先推进父侧 resume PC，而最近的
operation-boundary canonical snapshot 仍停在 `RangeNext`，且当前 canonical
contract 未携带完整 child ownership/adoption state。生成代码在发布 delegated
yield 前使该 canonical state 失效；snapshot/adoption 稳定返回
`AotSwitchUnsupported`，保留 live child 且禁止 stale-PC replay。该边界是明确的
fail-closed 迁移门禁，不关闭完整 generator protocol、Hybrid adoption 或
arbitrary-PC deoptimization，整体继续保持 `Implementation In Progress`。

同日补齐 DirectNative V2/V3 provider 的 delegated-await overflow 契约：移除
历史固定 sentinel，按 caller remaining budget 返回确定性溢出值；AOT unit
覆盖零预算边界，未扩大静态 DirectNative child graph 的准入范围。

随后收紧 Interpreter generator adoption：普通 continuation 只能从声明的
`Yield` 后恢复，delegated child-await 只能从声明的 `RangeNext` retry 边界
恢复；in-range arbitrary PC 现在确定性返回 `AotSwitchUnsupported`。该负向
边界测试不等同于 arbitrary-PC deoptimization 或一般 Hybrid adoption。

**2026-08-25：reverse pending-await cross-backend acceptance**：新增的
`arp_scriptvm_aot_v5_cross_backend_smoke` 使用真实生成的 `basic` DirectNative
package，并通过显式 module-export callback 消费 VM-owned Interpreter child；它
覆盖 pending `send()`/`throw()`、无 injection capability 的确定性拒绝、poll
no-replay 和 close。Windows Debug/Release 与 Web Debug/Release（Wasm）均通过。
四个 preset 的完整 `arp_scriptvm_*` CTest selection 也全部通过（27/27、26/26、
13/13、13/13），并验证了 `scripts/build.py test --target` 只运行指定 CTest。
这只是 bounded reverse pending-await evidence；完整 generator protocol、一般
Hybrid adoption、任意 optimized-PC deoptimization、移动平台和长期 endurance
门禁仍开放，整体状态保持 `Implementation In Progress`。

**2026-08-25：V3 pending-await capability truth source**：发现生成的
DirectNative `yield from` 在桥接 child 返回 `Suspended` 时无条件宣称
`awaitInjectionSupported`，会把没有 `ScriptAwaitable::inject` 的 child 误报为可
注入。保持 `GeneratorBridgeV3` 版本 1 和原始 ABI 不变，新增独立、版本化的
`GeneratorBridgeV3Capability` extension；Interpreter 和 DirectNative provider 都
从真实 suspended await state 返回 capability，生成器只消费该 extension 查询。无
capability 的 child 在不消耗 continuation、不重复 poll 的前提下确定性返回
`AotSwitchUnsupported`。四 lane AOT unit、cross-backend generated smoke、VM/
semantic runtime 定向测试通过；完整 generator protocol 和 delegated
await/throw 仍保持开放。

**2026-08-25：V3 ABI compatibility correction**：审计确认 pending-await 能力不应
追加到公开 `GeneratorBridgeV3` 结构体。现已恢复 V3 contract version 1 和原有
字段布局，能力查询迁移到独立的 `GeneratorBridgeV3Capability` version 1
extension；无 extension provider 时默认 false。Windows Debug/Release、Web
Debug/Release 的 AOT unit 与 cross-backend smoke、Windows 动态包 smoke、source
boundary/build graph/package audit 均通过。总体仍为 `Implementation In Progress`。

同日修正 `scripts/build.py test` 的 smoke registry：Windows 使用现存 Null/
RenderGraph/AOT smoke，Web 只选择已注册的 Wasm AOT smoke，并同时匹配
`_wasm` CTest 后缀；四个 preset 的默认 smoke 均通过。

**2026-08-25：dynamic staging cleanup retry hardening**：四 lane 全量回归发现
Windows Release 并发 dynamic-package smoke 偶发在编译器退出后约 180ms 的文件
共享窗口内清理 staging 失败。`removeDirectoryTree` 现在使用有界 10 秒 retry
window，仍在超时后返回 `AotArtifactInvalid`，不把残留目录或部分 DLL 当作
cache hit。Windows Debug/Release dynamic-package smoke（Release 连续三次）和
完整 ScriptVM CTest 均通过。

**2026-08-25：generator terminal-state no-replay guard**：`Vm::snapshotGeneratorFrame`
现在只允许已启动且仍处于可恢复边界的 generator 导出 continuation。未启动、已
完成或已 close 的 generator 均稳定返回 `AotSwitchUnsupported`；完整 program
artifact identity 也必须与请求的 module identity 一致，不能由调用方参数覆盖。
Windows Debug/Release semantic runtime、editor hidden-window、AOT unit、generated
cross-backend smoke 和 Web Debug semantic runtime 通过。该修复只收紧 bounded
continuation contract，不关闭完整 generator protocol、Hybrid adoption 或任意
optimized-PC deoptimization。

**2026-08-25：generated recursive delegation budget evidence**：`basic` AOT
fixture 新增递归 `yield from` entry。Windows Debug generated smoke 验证浅层
递归的 yield、父级 return 和 terminal/no-replay；深度 100 在 continuation
frame 预算高于 recursion 预算的明确配置下稳定返回
`RecursionLimitExceeded`，并清理全部 native frame。dynamic-package smoke
也通过，连同 partial DLL、completion marker、staging lock 和并发生成门禁。
该增量只证明 runtime budget 约束，不关闭动态 source typing 的更广泛
await/throw/close 或完整 generator protocol，整体仍为 `Implementation In
Progress`。

同轮最终复核：Windows Debug/Release 与 Web Debug/Release 的 generated
package smoke、四 lane `arp_scriptvm_aot_v5_tests`、Windows Release editor
hidden-window smoke、四 lane source-boundary 和 Android/iOS contract tests
均通过；Windows Debug/Release dynamic-package smoke 也通过。MuMu
`emulator-5554` 当前为 `offline`，本机没有 Android NDK 或 Apple toolchain，
因此不宣称移动 runtime/platform evidence；八小时 endurance、完整
generator protocol、一般 Hybrid adoption 和 arbitrary optimized-PC
deoptimization 继续保持开放。

**2026-08-25：Interpreter delegated-await budget alignment**：Interpreter
delegation depth no longer uses a fixed depth-two constant. Traversal is bounded
by the owning VM's `maxContinuationFrames` budget, bridge overflow is derived
from the caller's remaining budget, and saturated arithmetic prevents integer
wraparound. A real four-parent pending-await unit passes with budget 8, sends
through the chain with one poll, and fails closed with budget 2; the generated
DirectNative recursion smoke remains green. This is
bounded Interpreter protocol evidence only，general Hybrid adoption、arbitrary-PC
deoptimization、mobile runtime validation and complete generator protocol仍保持
`Implementation In Progress`。
