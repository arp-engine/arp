# ArpScript AOT 与 Interpreter 混合运行设计

**文档状态**：`Implementation In Progress`（历史迁移记录，非当前实现契约）

所有当前及未来 backend 的规范性语义以
[ArpScript Typed IR Backend Contract](typed_ir_backend_contract.md) 为准；本文仅保留
历史迁移设计与证据，不得覆盖该 contract。

**2026-08-26 全局契约整改**：v5 package仍只允许
`ScriptFrontend -> Typed IR -> DirectNative codegen`。兼容 `Program` adapter仅供
Interpreter/tooling tests使用；其 `yield from` 信息来自 compiler写入的
`Function::delegationSites(rangePc, yieldPc)`，lowering只校验显式 metadata的边界、
opcode、slot、唯一性和 `hasYieldFrom` 一致性。固定 bytecode序列扫描已删除，
缺失或冲突 metadata稳定返回 `TypedIrInvalid`。运行时 retained Value graph由
`ScriptValueMemoryAccountant` 和 `CpuMemoryQuotaLease` 计费；
`continuationFrameBytes` 仅是 telemetry，不参与 quota预算求和。

**当前架构事实（2026-08-26）**：生产 AOT 只有 v5 `DirectNative`；旧
contract-v1 descriptor、generic executor、Hybrid dispatcher 和相关 source/target
已经从源码树及构建图删除。编辑器热更新使用独立的
`InterpreterHotUpdateService`，不链接旧 AOT。本文后续主体保留旧 Hybrid 设计、
shape 迁移和验收记录，所有 `legacy/*.cpp`、`HybridExecutionDispatcher`、
`IrExecutorPrototypeLegacy` 与旧测试 target 名称均是历史快照，不能作为当前
source ownership、可链接 ABI 或支持范围的依据。当前权威状态以
`aot_legacy_retirement_matrix.md`、`aot_v5_windows_web_status.md` 和源码/build
boundary audit 为准；未迁移能力必须确定性拒绝或走显式 Interpreter-only 路径。

**历史实现状态（归档）**：当前为 `Implementation In Progress`。Native Value-frame、显式 scalar
direct-call frame-machine、非 tail inline scalar entry 与显式 `ArbitraryPc` self-tail/mutual-tail entry 的 cooperative operation-boundary OSR 为 `Contract Complete`，其余优化 Native 入口的 OSR 仍未完成。已经落地版本化 Typed IR lowering、
共享语义 helper、C++17 AOT codegen、带签名的 YAML manifest、source map、静态 registration、
函数边界 Hybrid dispatcher、Interpreter/AOT artifact identity binding，以及跨 backend 的共享
instruction budget/recursion session。native scalar entry 现写入 backend-neutral logical traceback，
并通过 descriptor 的 `AotFunctionExecutionKind` 明确区分真正的 `Native` lowering 与
`IrExecutorPrototypeLegacy` 通用 IR executor。该分类现写入 manifest、纳入 canonical
signature，且为避免将 v1/v2/v3 工件按新 Native 规则解释，manifest contract 已升级为 v4；
严格 `AotOnly` 默认拒绝后者，除非模块显式
opt-in。Windows MinGW 下已有独立生成源编译与 differential smoke。仍不声明
`Prototype Complete`、`Backend Validated`、`Platform Validated` 或 `Production Ready`：native
scalar 子集已有普通 CFG 的 stack-depth 与 definite-assignment proof。受限 Native exception
lowering 覆盖无捕获纯整数函数中受 `PushHandler`/`PopHandler` 保护的 `IntDiv`/`IntMod`
除零、`INT_MIN / -1` 溢出、`Neg`，以及由整数/bool scalar stack 值触发的 `Throw`；`Neg`
在 `INT_MIN` 边界显式回到同帧通用 executor，保持 `ScriptSemanticRuntime` 契约。后者通过
`makeInt`/`makeBool` 和 `tryGetString` 复用 Interpreter 的反射字符串转换契约。其它
exception/safepoint edge 尚未进入 Native lowering proof。
Native generator 的无 await/受限 handler `Yield`、固定 `Await -> Yield`、以及
bounded delegated-yield operation-boundary OSR 已具备 `ArbitraryPc` source-map
contract；pending delegated child 会在父级边界 deterministic fail-closed，保留 child
continuation，避免递归恢复消费错误或重放副作用。该增量在 Windows MinGW AOT/Hybrid
focused、19 项 ScriptVM CTest 和 Wasm AOT smoke 中验证为 `Contract Complete`。
复杂动态 delegation、任意 handler-side CFG、optimized native-PC deoptimization、
跨平台实机验证和长期性能/fuzz 门禁仍未完成。

Native handler frame-machine 另有一个固定 handler-side branch contract：
`PushHandler(5) -> LoadInt -> Yield -> PopHandler -> Jump(10)`，handler 以
`StoreLocal(0) -> LoadLocal(0) -> JumpIfFalse(10)` 选择可选的第二次 yield，随后
两条路径共享 terminal return。该 shape 的 operation-boundary OSR 会保留 active
handler；`throw(bool)` 通过同一 canonical local 值进入 branch，AOT、Interpreter 和
Hybrid 双向迁移保持一致。Typed IR eligibility、source-map/manifest、Native runtime
和负向 shape 校验均已覆盖，状态为 `Contract Complete`。它不扩展为任意 handler CFG；
动态异常 payload、嵌套 rethrow 和其它 branch 形状仍 deterministic fail-closed。

在该 branch contract 之后，Native yield frame-machine 又增加了一个固定双层
rethrow shape：`PushHandler(10) -> PushHandler(7) -> LoadInt -> Yield`，内层
handler 执行 `StoreLocal(0) -> LoadLocal(0) -> Throw`，外层执行
`StoreLocal(0) -> LoadInt -> Yield -> LoadInt -> Return`。Native 路径只消费内层
handler，并把同一个 caught Value 交给外层；正常路径按相反顺序清理两个 handler。
operation-boundary OSR 保存两个 handler 的 canonical stack，之后由 generic executor
继续执行时不重放首个 yield。该精确 shape 已有 AOT codegen/runtime、AOT/Interpreter
和 Hybrid 双向 contract，状态为 `Contract Complete`；三层以上 rethrow、异步
handler-side rethrow、任意异常 CFG 和优化 native-PC deoptimization 仍拒绝执行。

异常区域额外具备 contract 验证：每个 region 必须与唯一的 `PushHandler`/`PopHandler`
配对，普通 CFG 边必须保持相同的动态 handler 链，而每个 catch handler 以其外层链作为
异常入口状态。该验证禁止 protected path 绕过 unwind 直接以不兼容状态进入 handler；它不是
完整 Native exception frame-machine lowering。当前仅无捕获 pure-int handler、且 handler
首操作为 `Pop` 的 `IntDiv`/`IntMod` 算术异常、静态 `LoadString; Throw`，以及 scalar
int/bool expression `Throw` 可标记 `Native`。另有独立 value-preserving Native slice 支持
单一 `string` 参数的直接 `raise value`，可带丢弃异常值的 `except: return <int>`，或
`except as error: return error`。后者会将捕获消息重新 materialize 为 `Value`，并由独立
artifact smoke 与 Interpreter 对比验证。该 slice 复用 `tryGetString`，不重编码字符串或
反射转换。动态 object raise、一般异常绑定变量使用、call/host 异常和 suspend 仍为 Legacy。
`Throw` 作为 scalar CFG 的终止指令，会消费一个
operand 且不会流入前端插入的不可达 `LoadNil; Return` 尾块；Native eligibility 同时拒绝
任何跳转进入该死尾块。独立编译 artifact smoke 覆盖单层和嵌套 protected
region，验证内层 Native unwind 只消费内层 handler。受保护 Native caller 调用已证明
Native 的 direct callee 时，callee 的 `VmError` 也会在 caller handler 栈中 unwind；任何可
进入 Legacy、host 或递归 callee 的调用仍不具备该 Native 分类资格。

纯整数 self-tail recursion 已使用生成的 loop trampoline lowering：每个逻辑 tail call 仍
消耗 `recursionLimit` 和共享 execution session frame，但不消耗 C++ 调用栈。受限 Native
exception region 内的 self-tail call 会在 frame replacement 时清空旧 handler 链，再从入口
重建新 frame 的 handlers；其深递归 artifact smoke 已验证不会积累 handler stack。
另有模块级 Native mutual-tail SCC dispatcher：仅当 SCC 至少包含两个函数、每个 direct call
都留在该 SCC 内且为同 basic block 的 `Call; Return`、并满足现有无捕获纯 `int` scalar proof、
不含 exception region、safepoint 或外部 call 时，tail call 才写入 transient next-function/args
request 并由循环 dispatcher 执行。dispatcher 通过共享 `AotNativeTailRecursionScope` 记录每个
逻辑 frame，因此保持 recursion limit 与 session frame 契约，同时不增长 C++ stack。独立
MinGW generated-artifact smoke 覆盖 `even/odd` 10000 次切换和限制递归深度。无 suspend、
外部 call 或 capture 的 pure-int direct-call graph（包括 non-tail SCC）现在由
`AotExecutionFrame` 显式帧机执行：caller 的 pc、locals 和 operand stack 保留在 PMR 栈中，
child return 只回填 caller slot，不消耗 C++ 调用栈。独立生成工件覆盖互递归非尾调用 10000 层、
Native classification、recursion limit 与 logical traceback。对 `ArbitraryPc` mutual-tail
transfer，dispatcher-owned caller chain 在弹出 tail arguments 后记录 parent 的 post-`Call`
frame；下一个目标函数的 IR label 将该链与 active frame 一起 canonicalize，因此 Interpreter
从 child return 回填 caller operand stack 后恢复，绝不重放 tail call。`FunctionBoundary`
mutual-tail entry 不记录 caller history，保持 dispatcher 的紧凑、无额外分配路径。帧机还维护 scalar
`PushHandler`/`PopHandler`、静态或参数 string `Throw` 与跨 direct-callee 的算术错误 unwind；
另有独立的受限 `Value` 帧机覆盖 `LoadNil`/`LoadBool`/`LoadString`、`LoadLocal`/
`StoreLocal`/`Pop`/`LoadInt`、`Add`/`Sub`/`Mul`/`Div`/`Mod`
及其整数变体、direct `Call`/`Await`/`Return`
，以及经 Typed IR CFG stack-depth / definite-assignment contract 证明的
`Cmp*`/`IntCmp*`、`Jump`、`JumpIfFalse`/`JumpIfTrue`。未捕获 `Throw` 会在
释放 native frame 链前固化 logical traceback，并复用 Interpreter 的 `tryGetString`
异常文本转换。`PushHandler`/`PopHandler` 的 handler PC 和 operand-stack depth 使用 canonical
frame representation 持久化；同帧、direct child frame、以及 pending `await` 恢复后的失败都会
从最内层 handler 开始 unwind。显式 cell-bearing Native Value entry 现支持与函数 metadata
精确匹配的 `LoadCapture`/`StoreCapture`：捕获始终保留 `ScriptCell` identity，pending `await`
snapshot 与 AOT -> Interpreter adoption 也携带 `captureCells`，不会把 alias 降级为值复制。该
收窄契约还支持 Interpreter 同构的 `AddrLocal`/`AddrCapture`/`MakeClosure`：local slot 原地提升为
`ScriptPointer{ScriptCell}`，新 closure 因此与 caller 共用同一 cell，并携带 module/retention/bridge
metadata。生成端仅在 `MakeClosure` 紧邻 `CallValue`、target/capture/arity 一致且目标完整通过
Native Value proof 时将该图标记为 Native；零捕获 `MakeFuncValue` 使用相同的紧邻调用与
target/arity proof；`MakeMethodValue` 则额外验证 receiver 占用目标首个参数。其它动态构造或
动态 callee 仍保持 Legacy。
受限 Native Value 还支持 `LoadGlobal`/`StoreGlobal`，并只通过共享
`ScriptModuleGlobalStorage` 观察模块状态；pending await 后不会重放已完成的 global 写入。地址化
global 保留独立 contract，不能因普通读写支持而自动标记为 Native。带完整
safepoint map 的 global await frame 可由 Interpreter 接管，并继续观察同一 shared storage。
`AddrGlobal`、`Deref` 与 `StoreIndirect` 现仅覆盖共享 `ScriptCell` global：地址操作提升并复用
该 cell，读写经 `ScriptSemanticRuntime` 执行。若 Native AOT 在 `await` 后交由 Interpreter
恢复，canonical frame 中 local 保存的 `ScriptPointer` 仍指向同一 shared-global cell，因而后续
`StoreIndirect` 与 `LoadGlobal` 保持别名一致。Native Value 也支持 `AddrIndex` 与 `AddrField`：
它先按 Interpreter 规则用 `ScriptSemanticRuntime` 验证目标，再将 container/object、key/field
作为 `ScriptPointer` 的 owned `Value` 状态保存在 canonical frame；恢复后的 `Deref`/
`StoreIndirect` 仍经同一 helper。跨模块地址尚未进入 Native proof。
普通 `LoadIndex` 与 `StoreIndex` 已进入 Native Value frame，分别经
`ScriptSemanticRuntime::loadIndex` 与 `ScriptSemanticRuntime::storeIndex` 处理 list/dict/bytes
语义及诊断。Native 写入沿用解释器的栈顺序和对象 identity；字典仅在新增键且达到
`maxContainerValues` 时拒绝，更新既有键仍允许，并在整个 Native 帧机执行期间绑定
`CpuMemoryQuotaScope`。list/dict/bytes 的边界和 AOT -> Interpreter 迁移仍须由回归测试持续守护；
`values[1] = 9` 后 await 的 ArpScript source artifact 已验证保持 `Native` Value-frame 分类。
`StoreField` 同样进入 Native Value frame，以非空 field metadata 为前提并直接调用
`ScriptSemanticRuntime::storeField`；反射实例/class attribute 的写入和 AOT -> Interpreter
continuation 均复用同一对象 identity。同步 `StoreField -> Return(0)` 及其单层 handler
fallback 已由 generic/native parity 和 generated manifest 验证为 `Native`；未证明的动态
setter 调用仍保持 Legacy。
独立的同步 `LoadField -> Return` 也已由反射实例回归和 generated manifest 验证为 `Native`，
单层 handler 中的错误 receiver/getter failure 也复用共享 helper；动态成员调用和未证明的反射
形状继续使用显式 `IrExecutorPrototypeLegacy`。
`LoadModule`/`LoadModuleExport` 现作为受限 Native Value contract 支持：每个导入模块必须由
append-only `AotModuleDependencyV1` descriptor table 明确声明，Native executor 仅按该表调用
`ModuleRegistry`，缺失、重复或未声明的 binding 一律在 artifact validation/execution 阶段确定性失败。
模块和 export 在 await 前被 materialize 后作为 canonical local/operand state 保存；Native resume 与
AOT -> Interpreter adoption 均继续消费该状态而不重放 module loading。未声明依赖、缺失 registry/
module 以及跨模块地址语义不属于该 Native proof，保持显式诊断或 `IrExecutorPrototypeLegacy`；
已 materialize 的 `ModuleObject` export lookup 另有独立的纯读异常 slice。
无 `PushHandler`/`PopHandler`/`Throw` 的同步 `LoadModule -> LoadModuleExport -> Return` 也已由
generated manifest 验证为 `Native`，仍要求同一 append-only dependency binding；动态模块名、
未声明 dependency 和跨模块地址仍失败关闭。`LoadModule` 的失败路径现在额外允许固定
单层 catch-all handler：缺失 registry、module not found、source resolver 抛出异常都由
`ScriptSemanticRuntime::loadModule` 统一为 `HostError`，Native 与 Interpreter 均只尝试
一次 materialization；resolver 异常会清理 registry 的 resolving 状态，不会污染后续请求。
单层 catch-all handler 中的 `LoadModuleExport`（错误 target 或缺失 export）现在复用
`ScriptSemanticRuntime::loadModuleExport`，由 generic AOT、Native Value 与 source manifest
共同验证；它不重放 module loading。字段读写另有同步 helper/异常 contract，动态成员调用不在
该 module slice 内。

脚本类动态成员现在有独立的 append-only 元数据 contract：`TypedIrScriptClassSchema` 和
`AotScriptClassSchemaV1` 同时保存 method name 到稳定 function id 的映射（含前端/旧
`Program::methodSets` 的继承展平），并纳入 Typed IR fingerprint。descriptor validation
会拒绝空、重复或不存在的 method target。Interpreter、generic AOT 和 Native Value 的
`LoadField` 在字段读取返回 `NoMember` 时按同一顺序解析该表并构造 `ScriptMethod`，因此
`instance.method()` 不再依赖运行时猜测或反射 class 的隐式方法。当前 contract 已由
手工 descriptor、source lowering、generated source 和 generic/native parity 测试覆盖。对
已由 Sema 确定为本模块脚本类的方法调用，Typed-IR frontend 现在直接 lowering 为
`MakeMethodValue -> CallValue`，并复用稳定 target/capture/receiver-arity provenance；其
前置 `MakeInstance` 由共享 provenance CFG 按消费参数并产生未知实例值处理。这样源码级
`counter.increment()` 可进入 Native Value frame，反射类、动态 receiver 和未解析继承链仍
保留 `LoadField` 运行时解析路径。
旧 `compiler.cpp` compatibility adapter 现在也复用同一判定：只有 sema 已证明 receiver 是
本模块脚本类、`Program::methodSets` 给出唯一稳定 target 且方法 receiver/参数 arity 匹配时，才
改写为 `MakeMethodValue -> CallValue`；该路径与 Typed-IR frontend 共用
`ScriptMethod` provenance。反射类、动态 receiver、跨模块 method value、方法别名/容器传播、
未解析继承链、容器/指针传播和未证明的复杂 `LoadField -> CallValue` 仍保持
`IrExecutorPrototypeLegacy`，不会因存在 metadata 而自动升级 Native。对同一函数内
`MakeMethodValue -> StoreLocal/StoreGlobal/StoreCapture -> Load* -> CallValue` 的静态别名，
Typed-IR provenance 会继续验证 exact target、zero-capture 和 receiver arity；纯脚本类方法
child 即使没有 await，也会由 Native Value frame 承载 boxed receiver，已证明的纯整数 scalar
child 不会被误判为动态 callable。该 compatibility lowering、继承顺序、alias provenance
和生成 manifest 的 Native 分类已由 `testLegacyScriptMethodCallNativeLowering` 与
`testScriptMethodValueAliasNativeLowering` 覆盖，状态为 `Contract Complete`；不等同于
Backend/Platform/Production Ready。
依赖还可通过独立的 append-only identity table 携带模块版本与导出契约 hash；当 identity
存在时，Native `LoadModule` 必须在 materialize 后逐项匹配 `ModuleExports`，版本或契约
不一致统一返回 `AotModuleUnavailable`，不会把新版本/旧版本的 export table 静默绑定到
当前 artifact。原有 name-only dependency table 的元素布局保持不变，但不宣称版本化
动态加载完成。
`BuiltinAppend` 已收敛到 `ScriptSemanticRuntime::append`，由 Interpreter、通用 AOT 与 Native
Value 共同调用。该 contract 固定 list type diagnostic、`maxContainerValues` 容量诊断、返回 list
identity 和 pending `await` 后的跨 backend 可见性；单层 catch-all handler 的 receiver/type
和容量失败也已由 generic AOT、Native Value 与源码 manifest 验证为同一 fallback。
`BuiltinDelete` 与 `BuiltinClear` 也已复用 `ScriptSemanticRuntime` 的 dict/list mutation
contract。它们在 Native Value 中只允许已定义的 dict delete 与 list/dict clear，沿用
Interpreter 的无返回值、类型诊断和跨 backend object identity；单层 handler unwind 已覆盖
错误 receiver，失败前不改变容器。其它 builtin 仍为 Legacy。
ArpScript authoring 使用确定的命名 builtin：`append(list, value)` 返回同一个 list，
`delete(dict, key)` 与 `clear(list_or_dict)` 返回 void。sema 在 lowering 前验证容器及
element/key 类型和固定 arity；传统 bytecode compiler 与 Typed-IR frontend 都分别 lowering 到
同一个 `BuiltinAppend`、`BuiltinDelete`、`BuiltinClear` opcode，禁止把未知 callable 或方法语法
猜测为容器 mutation。
无异常的 `StoreIndex -> Return(0)`、`BuiltinDelete -> Return(0)`、`BuiltinClear -> Return(0)`
以及 `BuiltinAppend -> Pop -> Return(0)` 已分别由 generated manifest 验证为 `Native`；这不扩展到
别名经 container/pointer 传播、动态 callable 或复杂异常控制流；当前 mutation exceptional
slice 只接受固定单层 handler tail。
生成 artifact loader 还会按稳定 FunctionId 从注册后的 descriptor 解析十个 source-level bounded
Native case：`native_sync_module_import() -> int`、`native_sync_field_read() -> int`、
`native_sync_field_write() -> int`、
`native_sync_append(values: [int]) -> int`、
`native_sync_delete(values: {int: int}) -> int`、
`native_sync_clear(values: [int]) -> int`、
`native_guarded_sync_index(values: [int]) -> int`、
`native_sync_store_index(values: [int]) -> int`、
`native_sync_read_index(values: [int]) -> int` 和
`native_sync_dict_write(values: {int: int}) -> int`。它检查各自 `Native` execution kind，并分别以
真实 module registry、script instance、list/dict argument 执行模块初始化后的 export 读取、字段读取、字段更新、
append/delete/clear 后的长度读取、`len` 加索引读取和 dictionary 更新。loader 按 descriptor 的 `globalCount`
分配共享 storage 并执行版本化 `moduleInitFunctionIndex`，因此不会跳过 import lifecycle。每个 case 的同一 source
与输入先经 Interpreter 得到 `73`、`42`、`42`、`2`、`0`、`0`、`42`、`42`、`8`、`42` 基线，再由独立编译/加载的
Native entry 返回相同值，避免只以 manifest 文本或常量结果代替跨执行器语义对照。
`BuiltinLen` 作为无副作用 Native Value 操作复用 `ScriptSemanticRuntime::length`，并已验证
在 safepoint migration 后由 Interpreter 消费 AOT 保存的长度结果，不重复执行该操作。
当 `BuiltinLen` 是无 `PushHandler`/`PopHandler`/`Throw` 的同步读取路径中的唯一 Native Value
资格触发器时，生成 manifest 明确标记为 `Native`。现在同一 helper 的 `BadCast` 等诊断也可
在编译器生成的单层 catch-all handler 中由 Native Value frame unwind，且与 Interpreter 和
generic AOT 返回相同 fallback；嵌套 handler、动态异常 payload 与其它未证明 value operation
仍失败关闭为 `IrExecutorPrototypeLegacy`。
`ConvertInt`、`ConvertInt64`、`ConvertFloat`、`ConvertString`、`ConvertBool` 与
`ConvertBytes` 已进入 Native Value frame，按版本化 opcode 一一映射到
`ScriptSemanticRuntime::convert`。代表性 continuation regression 验证转换结果写入
canonical local 后可由 Interpreter 接管；非法转换仍经共享诊断路径处理。`ConvertBytes` 的
ArpScript `string as bytes -> len -> await` artifact 已验证为 Native Value frame；其它
conversion family 继续由相同的 one-pop/one-push CFG contract 约束。全部 conversion
opcode 的 `BadCast` 已纳入同一受限异常 slice：`ConvertInt`、`ConvertInt64`、
`ConvertFloat`、`ConvertString`、`ConvertBool`、`ConvertBytes` 均由 generic AOT、
Native Value frame 和真实源码 manifest 验证单层 catch-all handler 的 unwind 与
Interpreter fallback 等价。该 slice 仍只允许一个 conversion operation 和固定
handler-exit tail；嵌套 handler、动态异常 payload 及 conversion 与其它未证明
exceptional value operation 的组合继续保持 Legacy gate。
`MakeList` 与 `MakeDict` 已通过共享 transactional builder 进入 Native Value：容量或 operand
stack 不足会在任何 pop 前失败，成功时保持 source-order list 元素与 key/value dict 配对。两者均有
Native AOT 直接与 AOT -> Interpreter safepoint migration regression，禁止以部分构造结果作为 fallback。
`MakeList`、`MakeDict`、`MakeRange` 与 `MakeRangeFromStack` 现在也允许同一固定单层
catch-all handler shape；capacity、stack-underflow、malformed-bound 和 bad-cast 诊断由
generic AOT 与 Native Value frame unwind 到同一个 fallback，真实源码 `range` artifact
同时验证为 `Native`。该 construction slice 只接受一个构造 operation、固定 `Pop` handler
entry 和不可达 handler-exit tail，复杂容器 alias 或其它异常控制流仍失败关闭。
Typed IR CFG stack proof 现明确记录 `MakeList` 的 `B` pop、`MakeDict` 的 `2*B` pop，以及
`LoadIndex`、`LoadField`、store、`BuiltinLen`、container builtin 和 conversion 的固定栈效应；带
await safepoint 的 list construction + `len` 和 dict construction + index ArpScript 函数已由
generated manifest 验证为 `Native` Value-frame artifact。
独立的同步 `LoadIndex -> Return` 也已验证为 `Native`。`LoadIndex` 与 `StoreIndex` 是目前仅有
允许携带 `PushHandler`/`PopHandler` 的同步 Value 操作之一：越界诊断由 Native frame 走相同的 script
handler unwind，且 source artifact `try: return values[1] except: return 42` 与
`try: values[1] = 42; return 0 except: return 42` 已分别与 Interpreter 差分验证。
shape validator 只接受编译器产生的不可达 `Return; PopHandler; Jump(end)` handler-exit tail，
不放宽一般跳转到函数末尾。container mutation、aliasing 与其它未证明的 exceptional Value
shape 继续保持 `IrExecutorPrototypeLegacy`。
`Neg` 与 `Not` 也已进入 Native Value frame，并逐一调用
`ScriptSemanticRuntime::negate` 与 `ScriptSemanticRuntime::logicalNot`。Native Value 回归以
Generic AOT 的同一 opcode 作为结果、类型与诊断基线，覆盖 `INT_MIN` 的现行取负契约；`Not`
在写入 canonical local 后的 pending-await continuation 可由 Interpreter 接管，且不会重放该操作。
动态 `MakeRangeFromStack` 也已进入 Native Value frame：共享 helper 先按解释器既有顺序消费
end、再消费双参数形式的 start，随后调用 `makeRange`；错误路径因此保持相同的 operand
consumption、diagnostic 与 `maxContainerValues` 门禁。单参/双参直接差分和 AOT -> Interpreter
pending-await continuation 均已覆盖。未被该 frame-machine shape proof 覆盖的 unary/dynamic Value
操作仍为 `IrExecutorPrototypeLegacy`。
反射类 `MakeInstance` 已进入 Native Value frame：class metadata、`__new__` constructor、参数上限、
参数栈消费、host exception 与 undefined construction result 都通过
`ScriptSemanticRuntime::makeReflectedInstance` 统一。直接差分及 pending-await AOT -> Interpreter
adoption 验证了构造后的反射对象 identity 与属性读取，构造不会重放。脚本声明类的 schema 现已
作为 append-only `AotScriptClassSchemaV1` descriptor table 传递，保留类名、字段顺序和可选
constructor function ID；registration 会拒绝不完整、重复或引用未知 constructor 的 schema。
无显式 script constructor 的 schema 已由 `ScriptSemanticRuntime::makeScriptInstance` 统一分配，
Native Value frame 可在实例写字段后 suspend 并保持同一 object state；该路径不走反射 `__new__`。
带 script constructor 的 schema 现支持无 capture、参数精确匹配且满足 Native Value graph proof 的 child frame。
构造函数可在 `Await` 处 suspend；`Yield` 仍不属于构造器 contract。构造函数 `Return a=0` 的实现返回值会
由 canonical active-frame `replaceReturnValue`/`returnValue` contract 替换为预分配 receiver，因此写字段后可
继续 await 并进行跨 backend 迁移；constructor child frame 的 `Throw` 会先移除该 child，再按外层 caller
handler 传播，绝不会把 receiver 作为成功结果发布。AOT -> Interpreter 与 Interpreter -> AOT adoption 都已
验证构造器只执行一次且继续读取同一 receiver field；不满足 graph proof 的构造仍保持
`IrExecutorPrototypeLegacy`，绝不得误走反射构造或静默回退。
Typed IR CFG proof 同时验证 `MakeInstance` 的 nonnegative constructor arity、argument pop 和
result push；带注册 reflection dependency 的 artifact 已生成 `Native` Value-frame entry。该证据
不将 host reflection metadata 缺失或任意 script class 的构造降级为隐式兼容路径。
`LoadInt64`、`LocalAdd/SubIntConst`、`LocalAdd/SubInt64Const` 与
`LocalAdd/SubFloatConst` 已进入 Native Value frame。局部更新使用
`ScriptSemanticRuntime::binary`，保留整数溢出、float NaN/inf 和类型提升契约；int64 上界更新在
pending-await 后可由 Interpreter 从 canonical local 恢复，不重放已执行的更新。
`LocalAddLocal` 与 `JumpIfLocalIntLtConstFalse` 也已进入该帧机：前者复用相同的 binary
语义，后者通过 `ScriptSemanticRuntime::compare(Less)` 决定跳转。两项在 await 后的 Native resume
与 AOT -> Interpreter adoption 均有回归，因而局部整数循环的已完成算术和分支不会因切换被重放。
编译器仅在 destination 与右侧 identifier 都是非 capture 的 `int` local 时，将 `+=` lowering 为
`LocalAddLocal`；`while <int-local> < <int-literal>` 使用对应的局部条件跳转。真实源码 artifact
必须同时包含这两个 opcode 并被 manifest 分类为 Native，其他赋值/比较形式继续按各自契约选择路径。
`LoadFloat` 已进入 Native Value frame，按 Typed IR opcode 保存的 IEEE 位模式 materialize `double`；
直接差分和 pending-await AOT -> Interpreter adoption 均验证 float local 不依赖 AOT 栈重放。
Typed IR CFG stack proof 现将 `LoadInt64`、`LoadFloat`、`Not` 识别为单值栈操作，并验证上述
local constant update 的 definite-local 前提；真实 ArpScript float/int64 literal、`not` 和
float/int64 `+=` await functions 因此可生成 `Native` Value-frame artifact。
`Pow`、`FloorDiv`、`BitAnd`、`BitOr`、`BitXor`、`BitClear`、`ShiftLeft` 与 `ShiftRight`
已进入 Native Value frame，并统一经 `ScriptSemanticRuntime::binary` 保持数值、位运算、溢出和
诊断契约。直接差分逐项覆盖八种操作；`Pow` 在写入 canonical local 后的 pending-await
continuation 可由 Interpreter 接管，验证已执行的 AOT 运算不会重放。Typed IR 的 straight-line
及 CFG stack proof 也将这八种操作按 two-pop/one-push 处理，因此带完整 await safepoint 的
ArpScript `**`、`//`、`&`、`|`、`^`、`<<`、`>>` 源码函数可生成 `Native` Value-frame
artifact；当前语法未暴露的 `BitClear` 由版本化 descriptor 差分覆盖。其它未经 frame-machine
shape proof 的二元 Value 操作仍为 `IrExecutorPrototypeLegacy`。
`RangeInit`/`RangeNext` 已进入 Native Value frame。list、dict、bytes 的 cursor/key/value 读取通过
`ScriptSemanticRuntime::rangeNextCollection` 与 Interpreter/Generic AOT 共用；Script/AOT generator
resume 保留既有 owner/diagnostic contract。list cursor 在已消费首项后 pending-await 迁移至
Interpreter 的 continuation 已验证继续消费第二项，不重放第一项。Typed IR CFG proof 现验证
`RangeInit` 的 source/index local 初始化，并对 `RangeNext` 验证 source/index 读取、packed
key/value slot 边界与 one-bool push；`for value in range(3)` 后 await 的 source artifact 已生成
`Native` Value-frame entry。该证据不放宽 Script/AOT generator 或其它 iterator 的既有 runtime
contract。
另有受限 `CallValue` bridge：它接受当前 AOT module 创建的 `ScriptClosure`，以及 source
import contract 明确声明、位于 dependency table 的外部 `arp-fn:` closure，并在运行时验证
descriptor identity/version、目标 function id/signature、fixed arity、capture count、cell
有效性和 Native Value 操作形状；满足条件后作为带 captures 的 Native child frame 运行。该 child
的 pending await 会 canonicalize caller chain 与 `captureCells`，因此可由 Interpreter 恢复。同步
`HostCallable` 与静态 reflection function 复用独立 synchronous bridge，保持
Interpreter 的参数顺序与异常转换。`ScriptMethod` 以 receiver 作为第一个参数进入无 capture 的
Native child frame；`ReflectedMethod` 与 reflected property getter 通过同一同步 bridge 保持 receiver、
参数顺序和 script-handler unwind 语义。未声明 dependency、缺失 registry/descriptor、超出
bounded shape 的异步 import callable 和未证明目标均以稳定诊断失败，不会回落通用 executor。
当前 source-import ScriptFunction 只把显式 metadata 完整的固定 arity、零 capture export
纳入 contract；缺 metadata 时仍保留 sema/Interpreter 路径而不推断 AOT binding。
受限同步 `CallHost` 已通过该 Value frame bridge 运行：参数顺序、缺失 registry/entry、host
抛出的错误以及 active script handler 都与 Interpreter 共用相同语义；已完成的 host side effect
在后续 await resume 时不会重放。受限异步 host 子集现允许 `CallHost` 返回
`ScriptAwaitable` 并由紧随其后的 `Await` 消费：pending continuation 保留 host-owned awaitable，
Native AOT -> Interpreter adoption 后不会重放 `CallHost`。反射异步 callable 与其它未证明的异步
协议仍未进入该 contract。
调用图：pending child await 会把 immediate-to-outer caller 链固化为 `AotExecutionFrame`，并由
`NativeValueFrameMachine` 在同一 Native 引擎恢复和 unwind，不会回落通用 executor。该路径要求
除已证明的 shared-global、`CallHost`、same-module closure 以及 metadata-bound HostCallable
外无 capture、无 variadic、完整 await safepoint map；后者仅允许显式导入的
`LoadGlobal -> CallValue -> Await`，并以完整 runtime contract 验证。通用动态 Value、未标注的
外部/间接 call 仍为 `IrExecutorPrototypeLegacy`。
非尾 Native frame-machine 的 contract 是：每次 direct call 先将 caller 的 function identity、
下一条 operation index、locals、operand stack、handler chain 与 captures 固化为
`AotExecutionFrame`，再在同一个循环中进入 child frame；child normal return 只能向该 caller
operand stack 写入一个结果，不能重放 caller 或重新执行 call。frame push/pop 必须分别进入/离开
共享 `ScriptExecutionSession`，并更新 logical traceback；任何 budget exhaustion、OSR request、
exception 或 suspension 都只能在完整 frame 链已可 materialize 时离开循环。当前片接受本地
scalar handler、无 suspend、无 global/host effect 的 pure-int direct-call graph；其它形态继续 Legacy，
直到其 unwind/continuation contract 也由同一帧链证明。
每个 safepoint 的 `resumeLabel` 必须精确等于 suspend operation 的 `blockId:offset`，并与
该 operation 的 file/line/column source map 一致；缺失或伪造标签不能成为跨 backend 恢复点。

## 1. 目标

本设计定义 ArpScript 的两种执行 backend：

- `Interpreter`：执行版本化 bytecode，保留编辑器、调试、热更新和动态模块能力。
- `AOT`：把同一份 typed IR 生成 C++17，交给 MinGW、Clang 或 Emscripten 编译并静态链接。

两个 backend 必须满足以下硬目标：

1. 在相同脚本、输入、模块版本、host 环境和执行预算下，产生相同的语言可观察结果。
2. Hybrid runtime 可以在执行前按模块或函数选择 AOT/Interpreter。
3. 第一阶段支持在函数调用边界切换；支持在具有完整状态映射的 `yield`/`await`
   safepoint 切换。
4. 任意指令位置的切换只在显式 `ArbitraryPc` cooperative OSR contract 下承诺；优化
   native-PC deoptimization 仍属于后续能力。
5. fallback 必须在执行前固化并可诊断；禁止 AOT 执行失败后静默使用 Interpreter
   重放已经产生副作用的调用。

## 2. 非目标

- 不实现运行时 JIT，也不在目标设备上调用 C++ 编译器。
- 不要求 AOT 与 Interpreter 具有相同的执行耗时、物理内存地址或 C++ 调用栈。
- 不把现有 `Program` 内存对象直接序列化为 AOT contract。`Program` 包含运行时索引、
  PMR 容器和 host function 指针，不是稳定的跨构建 IR。
- 不以动态 C++ plugin importer 代替 AOT。插件 importer 的 manifest、缓存和模块注册
  思路可以复用，但 iOS/Web 等平台的 AOT 产物必须静态生成和静态注册。
- 首版不允许 AOT backend 使用未被语义 contract 证明等价的 C++ 原生行为进行优化。

## 3. 语言可观察等价

### 3.1 等价范围

同一测试向量在两个 backend 上执行后，以下内容必须相同：

- 返回值的类型、值、`nil` 状态和对象 identity 关系。
- assignment、参数传递和闭包捕获形成的引用别名关系。
- list、dict、instance、bytes 和 reflected object 的可见修改及迭代顺序。
- 全局变量、模块初始化顺序、模块 export 和 host 可见副作用。
- 整数宽度、转换、溢出、除零、移位和比较语义。
- 浮点运算的语言级结果，包括 NaN、无穷、正负零和比较规则。
- direct call、indirect call、method call、reflection call 和 variadic call 的参数检查。
- `try`、`except`、`raise`、host error 的 diagnostic code、脚本 source position 和 traceback。
- closure、generator、`yield`、`await`、resume 和完成状态。
- 执行预算耗尽、递归深度、取消和 host capability 拒绝的确定性结果。

物理分配地址、原生符号名、C++ stack frame、耗时和内存峰值不属于语言可观察结果；
但由 host API 明确暴露给脚本的 identity、销毁或顺序行为仍属于等价范围。

### 3.2 唯一语义事实源

禁止 Interpreter 和 AOT 各自实现一套算术、转换、容器、反射或错误语义。新增
`ScriptSemanticRuntime` 作为唯一事实源，两种 backend 都只能调用其受版本控制的操作：

```text
                         +-> BytecodeBackend -> Interpreter -+
ArpScript -> Typed IR ---+                                  +-> ScriptSemanticRuntime
                         +-> CppAotBackend  -> native code --+
```

典型共享操作包括：

- `add/sub/mul/div/mod/compare/convert`
- `loadIndex/storeIndex/loadField/storeField`
- `makeList/makeDict/makeInstance`
- `invokeHost/invokeReflectedFunction`
- `raise/enterHandler/buildTraceback`
- `captureLocal/loadCapture/storeCapture`
- `suspendAwait/resumeAwait/yieldValue`

共享操作必须使用 `core::pmr` 容器和 `core::makeUnique`/`core::makeShared` owned
allocation。生产运行时不得新增默认 STL 动态容器、裸 `new/delete` 或未受预算约束的
临时分配。

### 3.3 禁止依赖的 C++ 行为

AOT 生成代码不得直接依赖以下行为定义 ArpScript 语义：

- C++ 有符号整数溢出或实现相关的 shift。
- C++ exception 传播顺序作为脚本异常模型。
- 原生递归深度作为脚本递归限制。
- 默认 STL container 的迭代、allocator 或异常行为。
- 原生对象地址作为稳定脚本 identity。
- C++ 静态初始化顺序作为脚本模块初始化顺序。

这些行为必须经共享 helper 或显式 `ScriptExecutionContext` 处理。

## 4. 总体架构

### 4.1 编译链路

```text
.arp source
  -> ScriptFrontend (lexer/parser/sema)
  -> ScriptLowerer
  -> ArpScriptTypedIr/4
       -> BytecodeBackend -> versioned bytecode
       -> CppAotBackend   -> .gen.h/.gen.cpp
  -> ArpScriptAotModule/1 YAML manifest
  -> platform C++ compiler/linker
  -> statically registered module
```

Typed IR 必须保留：

- 稳定 module/function/type/symbol ID。
- 完整函数签名、nullable、variadic 和 result contract。
- typed locals、captures、globals、basic blocks 和 source positions。
- call kind、target binding、异常区域、suspend point 和 side-effect 标记。
- module dependency、reflection dependency 和 host capability dependency。
- 确定的整数/浮点/container 操作语义，不允许 codegen 猜测。

bytecode 与 C++ AOT 必须由同一个 lowered module 生成。不得继续让 AST 分别进入两个
相互独立的 compiler，从而形成长期语义漂移。

### 4.2 面向对象模块边界

建议的生产类边界如下：

- `ScriptFrontend`：源码到 sema-validated typed module。
- `ScriptLowerer`：typed module 到 `TypedIrModule`，负责公共 lowering 和不变量验证。
- `IScriptCodegenBackend`：codegen backend 接口，不拥有语言语义。
- `BytecodeBackend`：从 typed IR 生成版本化 bytecode。
- `CppAotBackend`：从 typed IR 生成 C++17 和 source map。
- `ScriptSemanticRuntime`：共享值、操作、异常、反射和 suspend 语义。
- `HybridExecutionDispatcher`：根据已固化的 execution plan 选择函数入口。
- `ScriptModuleInstance`：拥有特定 module version 的 globals、exports 和依赖绑定。
- `ScriptExecutionContext`：拥有预算、脚本 frame、traceback、取消和当前 module version。
- `AotArtifactValidator`：验证 manifest、hash、ABI、reflection 和静态注册表。

算法、CFG、ID、hash、source-map builder 和 validator 应优先复用已有 core/tooling 设施；
确需新增的通用实现沉淀到合适的 core/runtime/tooling 模块，不能散落在 game、sample
或 importer 路径。

## 5. AOT 产物契约

### 5.1 YAML manifest

生产 authoring 和 manifest 统一使用 YAML。每个 AOT module 至少生成：

```yaml
type: arp.script.aot-module
contractVersion: 5
module: gameplay
moduleVersion: 7
semanticAbiVersion: 1
typedIrVersion: 3
sourceHash: sha256:...
dependencyGraphHash: sha256:...
reflectionRegistryHash: sha256:...
generatedHeader: gameplay.aot.gen.h
generatedSource: gameplay.aot.gen.cpp
bytecodeArtifact: gameplay.bytecode
registrationSymbol: arp_register_script_aot_gameplay_v5
functions:
  - id: arp-fn:gameplay/main
    signatureHash: sha256:...
    aotEntry: arp_aot_gameplay_main
    bytecodeIndex: 0
    switchability: function-boundary
    executionKind: native
```

所有路径、ID、hash 和 binding 必须由输入及 contract 确定生成。缺失 source、dependency、
reflection signature 或 ABI metadata 时构建失败，不允许按文件名或顺序猜测。

### 5.2 静态注册

AOT module 通过版本化 C-linkage symbol 注册，但注册 payload 使用引擎定义的稳定 descriptor：

```cpp
extern "C" int arp_register_script_aot_gameplay_v5(
    arp::scriptvm::AotRegistrationHostV5* host);
```

descriptor 为每个函数提供稳定 `FunctionId`、signature hash、AOT entry、bytecode entry、
source map 和 safepoint map。静态注册不得持久化 reflection pointer；加载时通过稳定 ID
解析并验证 signature hash，失败时给出确定性 diagnostic。

### 5.3 Artifact freshness

构建和加载阶段必须验证：

- source hash 与输入源码一致。
- dependency graph hash 与构建期闭合图一致。
- generated C++、bytecode、manifest 来自同一 typed IR fingerprint。
- semantic ABI、typed IR 和 manifest 版本受当前 runtime 支持。
- reflection/host function signature 与生成时一致。
- function ID、AOT entry 和 bytecode entry 一一对应且无重复。
- 每个函数的 `executionKind` 必须为 `direct-native`，并参与 manifest signature；未知值和旧
  contract 缺字段均失败关闭。
- 在注册后的加载校验中，manifest 必须与实际 `AotModuleDescriptorV5` 的 module/version、
  ABI、所有 artifact fingerprint、函数 ID/signature/index/switchability/execution kind 逐项一致；
  同 module/version 的不同 descriptor 不能复用该 manifest。

## 6. Hybrid 执行模型

### 6.1 执行策略

支持以下显式 policy：

- `InterpreterOnly`
- `AotOnly`
- `HybridPreferAot`
- `HybridPreferInterpreter`

每次函数调用前，`HybridExecutionDispatcher` 生成或消费已缓存的
`FunctionExecutionPlan`。合法 outcome 只有：

- `Aot`
- `InterpreterEquivalent`
- `DiagnosticFailure`

plan 至少记录 module version、function ID、selected backend、reason、semantic ABI、
signature hash、policy epoch、diagnostic ID 和 `AotFunctionExecutionKind`。切换必须在执行入口
前完成；telemetry 与实际入口必须一致。

每个 AOT descriptor 必须声明一种 execution kind：

- `Native`：entry 包含受 Typed IR contract 证明的 native lowering。
- `IrExecutorPrototypeLegacy`：entry 仅转发到通用 Typed-IR executor；它不是原生 AOT。

严格 `AotOnly` 默认只接受 `Native`。若为了迁移工具或旧 artifact 显式启用
`allowPrototypeLegacyAotExecutor`，plan 和 telemetry 必须保留 Legacy 分类；不得把它改写为
`Native` 或隐式改走 Interpreter。
同一 opt-in 也适用于 `HybridPreferAot`：未显式允许的
`IrExecutorPrototypeLegacy` entry 不会被优先 AOT 策略选中；若存在匹配的
Interpreter entry，plan 必须选择 `InterpreterEquivalent`，否则返回稳定的
`DiagnosticFailure`。这样 legacy executor 只存在于明确标记的迁移/差分 fixture。
manifest parser 和静态 descriptor registration 都只接受这两个已版本化的分类；未知 enum
或文本值必须以 `AotArtifactInvalid` 失败，不能由 dispatcher 猜测其降级语义。

当前 native scalar lowering 对 direct-call graph 执行完整形状验证。pure-int self-tail
recursion（含受限 exception region）使用有界 loop trampoline；满足模块级 mutual-tail SCC
contract 的互尾递归使用循环 dispatcher；其余无 exception/suspend/capture/外部调用的 pure-int
direct-call SCC 使用显式 `AotExecutionFrame` 帧机。任何不满足该 proof 的递归仍标记为
`IrExecutorPrototypeLegacy`；不得以 nested generic fallback 作为将其标为 `Native` 的依据。

### 6.2 统一调用入口

```text
script call
  -> resolve FunctionId in current ScriptModuleInstance
  -> validate signature/module version/policy epoch
  -> consume FunctionExecutionPlan
       -> AOT entry
       -> bytecode entry
       -> deterministic failure
```

为了保留函数级切换能力，跨函数 script call 默认经过 dispatcher。只有当 module policy
明确冻结、call target 与 module version 已验证且优化仍保持切换 contract 时，才允许生成
受 guard 保护的 direct AOT call；guard 失效必须在调用前回到 dispatcher。

### 6.3 函数边界切换

首版必须支持函数或模块粒度的 policy 更新：

1. host 提交切换请求并增加 policy epoch。
2. 当前正在运行的普通 frame 保持原 backend 直到返回。
3. 后续函数调用读取新 epoch 并生成新 execution plan。
4. module-owned globals、objects、captures 和 exports 不复制迁移；它们由
   `ScriptModuleInstance`/`ScriptSemanticRuntime` 持有，并通过经过身份校验的
   module binding 继续共享。
5. 切换事件写入稳定 telemetry；不得修改脚本可观察状态。

从 Interpreter 切回 AOT 使用同一流程。

### 6.4 Safepoint 切换

`yield`、pending `await` 等位置只有在 typed IR 明确生成完整 safepoint map 时才可切换。
map 必须包含：

- stable safepoint ID 与对应 bytecode PC/AOT resume label。
- locals、captures、operand values 和 handler stack 的 canonical slot mapping。
- yielded/awaitable value、resume result slot 和 module version。
- source position、traceback frame 和执行预算状态。

暂停 frame 统一存储为 backend-neutral `SuspendedScriptFrame`。若 artifact 未提供完整映射，
该函数的 `switchability` 必须是 `function-boundary`，safepoint 切换请求确定性失败或等待
该 frame 完成，不能猜测迁移。

### 6.5 任意位置 OSR

普通函数执行中途的即时 AOT -> Interpreter 切换需要 deoptimization map：native PC、
optimized locals、临时值、handler state 到 canonical frame/bytecode PC 的完整映射。
当前已落地一个受限的 cooperative OSR contract：通用 AOT executor、显式
`NativeValueFrameMachine`、显式 `NativeScalarFrameMachine`、非 tail inline scalar
entry、受限 Native `Yield` generator 与显式 `ArbitraryPc` self-tail/mutual-tail entry
都会在 host/dispatcher 请求后的下一条 operation 之前物化
locals、captures、operand stack、handler stack、caller frames 和 source map，并以
`SuspendedScriptFrame` 迁移到 Interpreter。Native Value-frame 只导出其显式
`AotExecutionFrame`，不读取或推断 C++ native PC。inline scalar entry 在每个生成的 IR label
前导出其带显式 `int`/`bool` kind 的 locals、同样带 kind 的 operand stack 与 handler arrays；
`StoreLocal` 会同步更新 local kind，避免切换后 `LoadLocal` 改变语言类型。mutual-tail
dispatcher 仅在 `ArbitraryPc` transfer 时保存 post-`Call` caller frames；host 调用已经完成后切换时，Interpreter
从下一条 operation 恢复，不能重放该 host side effect。只有 artifact 将函数声明为
`ArbitraryPc` 且每个 operation 都有完整、与 IR source 一致的 source map 时才允许该路径；
缺少或不一致映射时确定性返回 `AotSwitchUnsupported`。

这不等同于优化 native code 的无协作 deoptimization。当前仍然：

- 不从不完整的 AOT native frame 猜测或复制 arbitrary PC。
- 不从函数开头重新执行 Interpreter。
- 不把 `Safepoint` 的 yield/await map 当作普通函数的 `ArbitraryPc` map。

Native generator 的 OSR 当前覆盖三种 operation-boundary frame：无 await/无 handler
的 Native `Yield` frame、精确单层 handler Native `Yield` frame，以及专用 Native
`Await -> Yield` frame 在 operation 开始前的
切换；精确 `CallHost -> Await -> Yield` shape 还允许 callback 创建 awaitable 后立即
切换。三者都要求 generator 自身声明 `ArbitraryPc`，当前 operation 必须有与 IR source 一致的
完整 map，且请求发生在 operation 尚未执行前。单层 handler 的 canonical handler stack
会随 OSR 一起保留，后续 `throw()` 仍按同一 handler/unwind contract 执行。实现只把
generator 的现有状态切换为
`IrExecutor`，不把 OSR 伪装成 pending await，也不读取 native C++ PC。delegated-yield、
非精确 host callback 形状、任意 handler-side CFG 和复杂 handler 组合继续 deterministic
fail-closed；它们必须先
补充独立的 canonical state contract，不能复用该窄化路径。

## 7. 状态、异常与异步语义

### 7.1 Value 与对象状态

首版 AOT 的 public boundary 和所有可能逃逸的值继续使用
`Value = reflection::object`。这保证 mutable container、instance、host object 和闭包共享
既有 identity/aliasing 语义。

后续允许对 sema 证明为非捕获、非逃逸且不经过 reflection/host boundary 的标量进行
unboxing。发生调用、捕获、返回、容器写入或 suspend 时按 typed IR contract boxing。
无法证明安全时保持 `Value`，不得以性能为理由改变语义。

### 7.2 调用栈与 traceback

`ScriptExecutionContext` 维护 backend-neutral script frame view。AOT 不使用物理 C++ stack
生成脚本 traceback。每个 entry、call、return、handler 和 safepoint 更新 canonical frame
metadata，使两个 backend 输出相同的脚本函数名、文件、行列和异常顺序。

严格等价 profile 下，递归限制和 execution budget 来自 context；不得由平台原生 stack
overflow 决定。需要深递归等价时，AOT call 使用 dispatcher/trampoline，而不是无界 C++
递归。

### 7.3 异常

脚本异常使用显式 `ScriptExecutionResult`/unwind state，不用 C++ exception 作为语言 ABI。
host C++ exception 若被 host bridge 允许捕获，必须先转换为稳定 `HostError`，再进入共同的
脚本 handler/unwind 路径。

### 7.4 Generator 与 await

Generator/await 的 canonical state 属于 `ScriptSemanticRuntime`，而不是 VM `Frame*` 或
AOT native stack。当前实现对具有完整 safepoint map 且声明 `Safepoint` 的 generator，支持
`yield` 和 generator-local pending `await` 的 Interpreter/AOT 双向迁移；locals、captures、
operand stack、handler stack、awaitable/yielded value 和 module identity 都进入
`SuspendedScriptFrame`。Interpreter 的 await continuation 现在可携带并恢复多层 AOT caller
frames；同 backend 的 AOT nested await 也会记录并恢复 caller stack。受限 Native Value direct-call
图在 same-context resume 时保留其 Native frame-machine owner；跨 backend adoption 继续消费同一
canonical frame 并按目标 backend contract 选择执行器。具有完整 source map
的跨 backend nested await 已支持双向迁移；缺少 safepoint、caller slot 或 source metadata
时仍确定性拒绝。嵌套 generator caller 迁移已支持：generator-owned continuation 会保存
ordinary callee 与外层 caller，并按 immediate caller 到 outer caller 顺序恢复。任意 PC OSR
仅支持下方定义的 cooperative `ArbitraryPc` 子集。能力必须由 artifact metadata 明确声明。
无 await/无 handler 的 Native `Yield` generator、精确单层 handler Native `Yield` generator，
以及专用 Native `Await -> Yield` generator 在 operation 开始前，现在可在
operation-boundary 请求后切换到 `IrExecutor`；
该路径保留原 generator 状态，不把切换伪装成 pending await。精确
`CallHost -> Await -> Yield` shape 在 callback 已产生 awaitable 后也会把 awaitable
放入 canonical operand stack，再从 `Await` 进入 Interpreter；已有 Native pending
await continuation、非精确 host callback、delegated yield 和任意复杂 handler CFG 仍不在此
能力范围内。
Generator protocol 的当前增量是 backend-neutral 的 `send()`/`close()` 加上 bounded
iterator-value `yield from`：legacy VM 与 generic AOT 将委托源 lowering 为
`RangeInit -> RangeNext -> Yield`，逐个转发 child generator/iterable 的 yielded value，
child 耗尽后继续 parent。这个子集为 `Contract Complete`，不声明 Native frame-machine
或完整协议等价。send 在没有委托的既有 yield frame 上注入一个值并只消费一次，pending
await 或首次 resume 会确定性拒绝；含 `yield from` 的 generator 会把 send 转发给
resumable child，无法证明 child protocol 时确定性拒绝。close 切换完成状态并转发
给 child。Hybrid dispatcher 现在提供对称的 `resumeGeneratorSendOn`：跨 backend
迁移先只 adoption canonical yield frame，再由目标 backend 消费 send 值，保证不重放
已经产生的 yield。canonical frame 额外保存 root generator 的 delegated child 槽位与
parent yield PC，因此 bounded AOT/Interpreter parent adoption 可在保留 AOT child 的
情况下双向消费 send/throw。已证明的 Native await-yield frame（含 handler 形状）在 terminal yield
boundary 消费 send 后直接完成固定 terminal path；pending await、复杂 Native frame 和
未证明/超过两层 delegated-await child、复杂 Native frame 仍确定性拒绝。generic AOT 的 bounded
`RangeNext` delegated child 若在 parent yield 前进入 pending `Await`，现在会保存 parent
locals/stack/handlers/captures、RangeNext successor/key/value/index，并在后续 resume 只推进
child 一次：child yield 注入 `true` 和 value，child 完成注入 `false`，child error 注入
parent `pendingThrow` 进入既有 handler。该状态现在可在 AOT parent 与 Interpreter parent 之间
双向迁移：canonical frame 把 parent 固化到显式 `RangeNext` retry point，保留已校验的 child
Value/backend ownership，并允许最多两层 pending child 及受预算约束的 caller chain；超过深度、循环或
无法证明 ownership 的图仍失败关闭。另有一个固定参数前缀 Native `yield from`
frame-machine shape 已完成：它接受
`LoadLocal(0..P-1) -> Call(P) -> RangeInit -> RangeNext -> JumpIfFalse ->
LoadLocal -> Yield -> Jump -> LoadInt -> Return`（`P = 0` 保持兼容），支持
AOT/Interpreter generator child ownership、pending-await migration、send/throw/close，
以及静态可证明构造的 `list`/`dict`/`range`/`bytes` child；另支持带完整 provenance
的零 capture `MakeFuncValue -> CallValue` child。集合 child 不产生
pending-await，迭代复用共享 `rangeNextCollection` 语义；任意动态 child、表达式/容器参数传播、
第六级 Native delegation 和复杂 Native frame 仍 fail-closed；五级
`Native root -> Native middle -> Native inner -> ... -> proven leaf` 已由 AOT/Hybrid 双向迁移回归覆盖。`throw()` 在 Interpreter 与 generic AOT
`IrExecutor` 上于当前 yield 边界注入 `UserRaise`，复用既有 handler/filter/unwind
语义；首次 resume、pending await、未证明的 Native handler/复杂 frame 的 throw 以及不支持的
委托 child 组合继续保持 `AotSwitchUnsupported` 的 fail-closed 结果；无 handler 的 Native
generator 对未捕获 throw 返回 `UserRaise`，已证明的单层/嵌套 await-handler Native
generator 复用既有 handler unwind。已声明 safepoint 的 generic AOT/Interpreter
generator 可先完成不推进的 frame adoption，再在目标 backend 注入 throw；Native/复杂
frame 不得借助 native PC 猜测。
通用 AOT `Await` 通过单一 `makeAotAwaitContinuation` helper 构造
`AotSuspendedContinuation` 与 canonical frame，避免同一 safepoint contract 在不同执行路径
各自复制。已验证的单帧 `LoadLocal -> Await -> Return`（无 capture、无 caller、完整
safepoint map）由 Native `Value` frame machine 执行；pending continuation 带有非序列化的
native resume kind，保证同一 AOT context 恢复时仍回到该 Native machine。`Yield`、多帧
async caller chain 已有受限 Native `Value` direct-call 图实现；其 canonical frame 会同步
保存 caller chain，Native AOT -> Interpreter 与 Interpreter -> Native 的迁移均由接收端
artifact contract 决定执行器。containers/closures/reflection、handler async frame 和其它
未证明 async 形态仍显式标为 `IrExecutorPrototypeLegacy`，不得宣称为 Native。该帧机的
locals、operand stack、direct-call 参数和恢复状态均受 `maxFrameValues`/
`maxCallArguments`、`maxContinuationFrames` 与函数 metadata 限额校验；不足以证明的
continuation 会以稳定的 `ResourceLimitExceeded` 失败关闭。`maxContinuationFrames` 由
Interpreter、AOT 和 Hybrid module instance 统一配置；AOT 生成 native entry 在该值偏离
默认 contract 时显式回到受管执行器，避免 native path 绕过 caller-chain budget。

Generic `IrExecutorPrototypeLegacy` 已验证任意固定深度的 direct-call async chain：
每个 pending child 只保留一次 `AotExecutionFrame`，resume 按 immediate caller 到 outer
caller 的顺序逐层注入结果，不重放已完成的 await。四层 caller fixture 已覆盖 pending、
single-poll resume、结果返回和超出 `maxContinuationFrames` 的 `ResourceLimitExceeded`。
这证明了受预算约束的 generic AOT/Interpreter continuation 语义。Native Value
frame-machine 现在对同模块、静态 provenance 的 closure-await graph 使用数据驱动的
caller promotion：leaf await ready 后逐层提升到 caller，复用每层 capture/result
contract，不重放已完成的 CallValue 或 await；深度由 `maxContinuationFrames` 固化。
该 Native arbitrary-depth 能力只覆盖已证明的 closure-await graph；声明 dependency
registry、逐帧 module/owner/artifact identity 和固定 closure-await shape 的跨模块切片
同样进入该 contract。动态 receiver、动态/无界跨模块图、异常/生成器子图仍保持 Legacy
或确定性失败。

真实源码 lowering 现在会在 generator/await 的 source position 完整时自动生成
`Safepoint` switchability 和对应 metadata；source position 不完整时保留
`FunctionBoundary`，避免把不可验证的函数伪装成可迁移函数。capture migration 保留
共享 `ScriptCell` identity，已经由一个 generator closure 和一个外部 modifier closure
跨 Interpreter/AOT/Interpreter 的 `7 -> 8 -> 9` 差分 fixture 验证。带多层 caller frames 的
Interpreter await continuation 现在可被 AOT 恢复并逐层返回，同时原 Interpreter
continuation 仍保持可恢复；AOT optimized-native-PC snapshot 仍是后续边界。

## 8. Module、Reflection 与动态能力

### 8.1 构建期依赖闭包

纯 AOT 发布模式要求 import graph 在构建期闭合。运行时请求图外 `.arp` 模块时，
`AotOnly` 必须给出确定性 diagnostic。

Hybrid 发布模式可以携带 interpreter 和 bytecode/source package 处理构建期图外模块，但该
能力必须由 packaging metadata 显式声明。不得因找不到 AOT entry 就静默搜索任意源码路径。

### 8.2 Reflection binding

Typed IR 和 AOT artifact 只记录稳定 type/function/property ID 与 signature hash。加载阶段通过
reflection registry 解析 native binding；不保存跨进程 pointer，不按类名相似度或注册顺序
fallback。binding 缺失或冲突时 module validation 失败。

### 8.3 Host side effect

host call 在 typed IR 中标记 effect contract。AOT 调用返回错误后不得自动调用 Interpreter
重试，因为第一次调用可能已写文件、提交资源、发送网络请求或修改 host object。

## 9. Hot Reload 与版本化

每次脚本更新创建新的 immutable `moduleVersion`：

```text
AOT v1 active
  -> load bytecode/module v2
  -> new calls use Interpreter v2
  -> existing v1 frames finish on v1
  -> build/load AOT v2
  -> new calls use AOT v2
```

closure、function value、generator 和 suspended frame 固定引用创建它们的 module version。
禁止把旧 frame 的 function ID 静默重绑定到新代码。只有显式、版本化且通过 state migration
contract 的迁移才能跨 module version；首版不提供该迁移。

旧 module version 必须在其 frame、closure、generator、exports 和 native lifetime 全部释放后
确定性回收。缓存和版本保留数量必须由明确预算控制。

## 10. 失败与诊断契约

最低限度新增稳定 diagnostic 类别：

- AOT artifact/ABI/IR/manifest version mismatch。
- source/dependency/reflection fingerprint mismatch。
- missing/duplicate AOT or bytecode function entry。
- policy forbids selected backend。
- dynamic module unavailable in `AotOnly`。
- function/safepoint/OSR switch unsupported。
- module version mismatch。
- execution budget/recursion limit exceeded。

诊断必须包含 module、module version、function ID、requested backend、selected outcome、
policy epoch 和稳定 diagnostic ID。禁止只记录日志后继续执行另一条路径。

## 11. 性能与热路径约束

- AOT entry、dispatcher、semantic helper 和 frame transition 不得执行文件/网络 IO。
- function plan、binding 和 signature validation 在 module load 或 policy epoch 变化时缓存；
  不得每次调用重新解析 YAML 或 reflection name。
- frame、argument、temporary value 和 safepoint storage 必须有 bounded allocation 策略。
- 不做 per-call C++ symbol lookup、source compilation 或动态库加载。
- 性能测试必须分别报告 VM dispatch、shared semantic helper、reflection bridge、allocation 和
  host call 成本，不能只用总耗时推断 AOT 优化来源。

## 12. 验证策略

### 12.1 Differential contract test

同一 typed IR 同时生成 bytecode 和 AOT fixture，并运行以下矩阵：

| 执行路径 | 必须验证 |
|---|---|
| Interpreter only | baseline 语义与 diagnostic |
| AOT only | 与 baseline 完全一致 |
| AOT caller -> Interpreter callee | 参数、identity、异常、traceback |
| Interpreter caller -> AOT callee | 参数、identity、异常、traceback |
| AOT -> Interpreter -> AOT | globals、closure、host side effect 次数 |
| suspend on AOT, resume on Interpreter | 仅对声明 safepoint map 的函数 |
| suspend on Interpreter, resume on AOT | 仅对声明 safepoint map 的函数 |

比较器至少检查：

- canonical value graph 和 alias/identity edges。
- globals/module exports/container contents。
- ordered host-call log 和 side-effect count。
- result、diagnostic、source position 和 traceback。
- suspension/resume state、handler stack、module version。
- execution budget consumption contract。

随机程序或 property-based 测试只能生成 typed IR contract 允许的程序；发现 mismatch 时必须
保存最小化源码、typed IR fingerprint、两个 backend trace 和 artifact manifest。

### 12.2 每阶段最低验证

每个实施阶段至少保留：

- 一个快速 unit/contract test：IR validator、manifest validator、dispatcher 或 differential
  corpus 中的一个 focused slice。
- 一个 headless/hidden-window smoke：静态链接生成的 AOT fixture，实际执行 AOT、Interpreter
  和至少一次函数边界切换。

脚本驱动可视化、渲染或 UI 时，除 differential test 外还必须按仓库要求使用 RenderDoc
捕获验证，并记录 capture 路径、关键截图或确定性诊断。纯语言/runtime 阶段不以 RenderDoc
作为 contract test 的替代品。

### 12.3 平台矩阵

- Windows：统一使用 MinGW/Ninja，验证 generated C++ 静态链接和 hybrid smoke。
- macOS/iOS：使用 Clang/Xcode toolchain，验证静态注册、pause/resume 和平台打包。
- Web：使用 Emscripten，验证无动态库依赖的 AOT/Wasm 与 interpreter bytecode package。
- Android：验证 Clang/NDK 静态注册、lifecycle 和确定性资源回收。

单个平台通过只能声明对应 `Platform Validated`，不能据此声明全平台或
`Production Ready`。

## 13. 实施阶段与状态门禁

### Phase A0：共享语义与 Typed IR

目标状态：`Contract Complete`；当前为 `Completed`。

- 定义 `ArpScriptTypedIr/7`、ID、signature、effect、exception 和 safepoint schema。
- 抽取 `ScriptSemanticRuntime`，让现有 VM 先消费共享 helper。
- 建立 bytecode 前后行为回归和 IR validator。

当前切片：`src/scriptvm/ir/typed_ir.h/.cpp` 定义 `ArpScriptTypedIr/7` 的函数签名、操作、
CFG、locals/captures、effect、依赖、异常区域和 safepoint 元数据，并提供稳定 ID、签名
hash、模块 fingerprint 与 validator。现有 bytecode 通过不序列化 `Program` 运行时容器的
`src/scriptvm/frontend.*` 现在提供 `ScriptFrontend` -> `ScriptTypedModule` ->
`ScriptLowerer` 的生产入口，前端结果携带 sema-validated AST 和稳定 module identity。
`ScriptLowerer` 现在直接从该 sema-validated AST 生成 Typed IR 的函数、作用域、捕获、CFG、
异常区域和 safepoint metadata，不创建或序列化 `Program`。Interpreter 仅通过
`InterpreterBytecodeLowerer` 消费验证后的 IR；AST-to-bytecode 与 reverse adapter 已删除。

`src/scriptvm/semantic_runtime.*` 已被 VM、AOT runtime 共同用于 arithmetic、comparison、
negation、logical-not、conversion、length、container/index 和 field lookup/store；exception
与 suspend 的 backend-neutral 迁移仍按 A3 逐项补齐。

### Phase A1：基础 AOT

目标状态：`Prototype Complete`；当前为 `In Progress`。

- 生成 C++17、YAML manifest、source map 和静态 registration。
- 覆盖 scalar、control flow、direct/indirect call、container 和 host call。
- Windows MinGW 下通过 AOT-only differential unit 与 hybrid headless smoke。

当前切片：`src/scriptvm/aot.*` 已提供 typed IR 到 C++17、YAML manifest、manifest
signature、source map、静态 registration ABI 和 IR runtime。manifest 会校验 source、依赖、
reflection、Typed IR fingerprint 与函数 entry，并可在加载阶段从 YAML 文件读取和验证；
Typed IR exception region 的 protected range、handler operation/block 和 source metadata
会进入生成的 `AotExceptionRegionV1` 表，并在 descriptor 注册时做范围、identity、重复项和
table pointer/count 校验；
manifest function 的 `bytecodeIndex` 现在必须在函数表范围内且一对一，不允许重复映射。
带 fingerprint 的 generated descriptor 在 registration 时必须同时提供合法的
source、dependency、reflection 和 Typed IR fingerprint；旧版全空字段的 descriptor
仍按 ABI 兼容路径接受。
生成的 `.gen.cpp` 已在测试中由 MinGW 独立编译。AOT 已支持 generator/yield 的可恢复状态
和 `RangeNext`，`Pow`/`FloorDiv` 也已纳入共享数值 contract；AOT 测试还会从生成的
`.gen.cpp` 编译独立 loader、注册 descriptor 并执行生成 entry。仍需补齐更广泛的产物
覆盖和跨平台编译证据。

### Phase A2：函数级 Hybrid

目标状态：`Backend Validated`；当前为 `In Progress`，仅限已实际验证的 backend。

- 引入 dispatcher、policy epoch、function execution plan 和 telemetry。
- 验证 AOT/Interpreter 双向调用、函数边界切换、禁止失败后重放。
- 完成 module version 和热更新生命周期。

当前切片：dispatcher 已固定 policy epoch，选择发生在 entry 前，AOT 执行失败不会重放
Interpreter；`ScriptModuleInstance` 复用 backend context 以保留 globals。已补齐模块级
instruction budget、recursion limit、frame/container/call-argument 资源限制，以及跨 backend
caller/callee 的 generator `RangeNext` 差分。旧版本 frame/closure/generator 的 owner 保留、
模块替换后的 generator 生命周期和 closure 拒绝已有测试；旧版本 closure/generator 的
retention lease、显式数量预算、超预算替换拒绝和释放后回收已有测试。当前预算约束的是
可观察的 retained script values；新增的 `ScriptModuleInstance::maxMemoryBytes` 或共享
`CpuMemoryQuota` 会按 `defaultCpuMemoryResource()` 的实际 allocation bytes 进行嵌套调用共享的
门禁，allocation header 保留 quota 以支持 continuation/closure 延迟释放。该 quota 不是
进程全局预算，也不替代 host 外部 allocator 的独立配额。`core::CpuExternalAllocationLease`
现在为 host-owned external allocation 提供显式 quota contract，并在 VM/AOT host callback
中验证了跨 dispatcher 生命周期的 charge/release。当前 quota corpus 已覆盖
list、dict、长 string、bytes 和 nested call 的 Interpreter/AOT 成功与超额失败路径；
未使用 lease 的任意 host `new`/外部 allocator 仍不属于可观测 quota。
具有完整 source map 的 nested
cross-backend await 现在会在 safepoint canonicalize 后迁移并恢复 caller；不完整状态仍
返回确定性的 `AotSwitchUnsupported`，不会把 callee continuation 冒充为可恢复的外层 frame。

VM 与 AOT execution context 已支持共享的 instruction budget 和显式 recursion/budget
diagnostic；预算默认关闭，模块实例可显式配置限制。
Hybrid corpus 现在在相同的耗尽边界和足够预算下比较 Interpreter/AOT 的 diagnostic
与返回值；component benchmark 则以固定迭代数检查 allocation count 与 live-byte
delta budget，作为 native CTest gate。

### Phase A3：Closure、异常与异步

目标状态：`Backend Validated`；当前为 `In Progress`，按实际功能逐项声明。

- closure/capture、class/reflection、try/raise/traceback 全量差分。
- generator/await canonical suspended frame。
- 有完整 map 的 `yield`/`await` safepoint 双向切换。

当前切片：`src/scriptvm/suspended_frame.*` 定义并校验 backend-neutral suspended frame；
VM 已支持嵌套 caller、capture、operand、handler 和 source position snapshot，AOT 已支持
带 source-map resume label 的 await continuation。对声明 `Safepoint` 且具有完整 source map
的函数，已实现 AOT -> Interpreter 与 Interpreter -> AOT 的 pending await 迁移，并拒绝
重放函数入口。generator/yield、generator-local pending await 和 `RangeNext` 已完成
双向/跨 caller 差分；模块替换后的旧 closure 也会确定性返回
`HybridModuleVersionMismatch`。完整 source map 的 nested
cross-backend await 已完成双向迁移，缺失 contract 时仍确定性拒绝。嵌套 generator caller
迁移也已完成：generator-owned continuation 保存 ordinary callee 与外层 caller，并按
immediate caller 到 outer caller 顺序恢复。`ArbitraryPc` cooperative OSR 已覆盖通用 AOT
、Native Value-frame 的 host-request operation-boundary continuation，以及 scalar
frame-machine、non-tail inline scalar、self-tail 与 mutual-tail entry 的显式请求
operation-boundary continuation 和 Interpreter 无重放恢复。self-tail scope 与 mutual-tail
dispatcher request 都会在每次 `ArbitraryPc` tail transfer 后保留 parent 的 post-`Call`
canonical frame；默认 `FunctionBoundary` tail entry 不分配该历史链。Hybrid 测试已覆盖 Interpreter/AOT 的
throw-and-catch 返回值、直接未捕获异常的 code/message/traceback，
以及跨 backend 的多帧 traceback；嵌套 handler 的 rethrow、handler 顺序和 operand-stack
unwind 也已有差分覆盖。异常区域的 lowering、Typed IR validator、AOT artifact table
和 deterministic malformed-descriptor rejection 已完成；生成式 artifact corpus 现在覆盖
nested rethrow、non-throwing protected region、adjacent protected regions 以及被调函数抛出后
由调用函数捕获的 cross-function region，并逐项与 Interpreter baseline 比较；另有生成式
mutable-capture closure fixture，验证 `make_counter`/`read` 的 `ScriptCell` 更新、AOT
`CallValue` 和独立 loader 执行结果与 Interpreter 的 `8` 基线一致。长循环
cross-host exception fixture 现在重复执行 128 次抛出 host callable，比较两 backend
的返回值和 side-effect count；更广泛的 host API 类型仍可继续扩展。

### Phase A4：优化与平台验证

目标状态：按平台分别 `Platform Validated`；当前为 `In Progress`，全部门禁完成后才评估
`Production Ready`。

- broader proven-safe scalar unboxing；direct-call 的 fixed-arity/target guard
  和当前支持的 signature/type guard（数值归一化、nullable nil、class、variadic
  element）已完成；uncaptured `int`/`int64` local constant `+=`/`-=` fast path
  已由 compiler 与 direct typed lowering 发出并完成 VM/AOT checked parity；float scalar
  `+=`/`-=` fast path 也已完成同样的 parity，
  更广泛的 unboxing/type 场景仍开放。
- bounded frame allocation and runtime operand-stack limits。
- byte-level module quota 已覆盖 default CPU PMR 的实际 allocation/deallocation；跨类型、
  retention、nested call 和失败诊断 corpus 已覆盖。`CpuExternalAllocationLease` 已提供
  host-owned external allocation 的显式 charge/release contract；未注册 lease 的外部
  allocator 仍开放。
- Windows、macOS/iOS、Android、Web 平台构建和 smoke；当前已有 Windows 三配置与
  Emscripten ScriptVM package contract，另有 WebGPU 浏览器 rendering smoke。
- 性能、内存预算、热路径、错误恢复和长期 differential corpus 证据；当前 Release
  timing/resource gate 仅是 Windows MinGW/Ninja 单机基线，不是跨平台阈值。

任意阶段若 AOT 与 Interpreter 出现语义 mismatch，必须停止提升状态。修复应落到 typed IR、
共享 semantic helper 或明确 backend lowering，不得添加基于脚本名、平台名或经验参数的
兼容特判。

## 14. 关键决策汇总

1. AOT 和 Interpreter 是同一语言 contract 的两个 backend，不是主路径与降级副本。
2. 两者从同一个 typed IR 生成，并共同使用 `ScriptSemanticRuntime`。
3. Hybrid 首版支持函数边界切换和完整 map 的 safepoint；`ArbitraryPc` 只支持明确声明
   的 cooperative operation-boundary OSR，优化 native-PC deoptimization 后置。
4. AOT 失败后禁止 Interpreter 重放。backend 选择必须在执行前确定。
5. module instance、Value heap、globals、closure 和异步状态归共享 runtime 所有。
6. AOT artifact 使用 YAML manifest、稳定 ID、signature hash 和静态注册。
7. 编辑器可优先 Interpreter；发布包可使用 AOT-only 或显式携带双产物的 Hybrid。
8. VM/AOT differential test 是长期合入门禁，不是一次性验收脚本。

## 15. Implementation Update (2026-08-16)

The following slices are implemented and covered by the current Windows
ScriptVM validation matrix:

- Function-level hybrid policy overrides and direct-call bridges in both
  directions: AOT caller to Interpreter callee and Interpreter caller to AOT
  callee, including shared-global and host-side-effect ordering coverage.
- Direct AOT entries now enforce the same fixed-arity contract as the
  Interpreter, and AOT direct-call targets are range-checked before argument
  materialization; wrong-arity parity is covered by the Hybrid test corpus.
- AOT descriptors now carry ABI-compatible fixed-parameter and variadic
  metadata; variadic calls pack trailing arguments into the same list local
  shape as the Interpreter, with Typed IR and generated-source coverage.
- `ScriptFrontend`, `ScriptTypedModule`, and `ScriptLowerer` now expose the
  sema-validated typed-module production boundary with stable module identity.
  `ScriptLowerer` directly emits Typed IR from the sema-validated AST, including
  function signatures, local/capture slots, CFG blocks, direct-call target IDs,
  exception regions, and source-mapped suspend points. Interpreter bytecode is
  now a private lowering of the same validated IR; no reverse adapter remains.
- Interpreter and AOT share argument normalization for int/int64/float,
  nullable nil, reflected class names, and variadic element contracts; the
  generated-loader test executes a wrong-type call to verify runtime metadata.
- Module resource limits are enforced by both backends for frame values,
  containers, call arguments, operand/handler storage, captures, and range
  materialization.
- Compiler stack accounting now includes the `RangeNext` condition value and
  emits the runtime `Pop` for discarded compound-assignment operands, so the
  compiled `maxStack` contract remains valid for ordinary iteration paths.
- VM and AOT now enforce both compiled `maxStack` and module-level
  `maxFrameValues` at runtime; the same overflowing frame is rejected with
  `ResourceLimitExceeded` by both backends.
- Integer local fast paths and typed arithmetic use shared checked
  intermediates; proven in-range `int` values and checked `int64` local
  constants are unboxed, while overflow falls back to the shared
  `IntegerOverflow` diagnostic. Literal `range` lowering now emits the
  runtime `Pop` for consumed bounds, keeping compiled `maxStack` and the
  runtime operand stack aligned in long iteration loops.
- Float local `+=`/`-=` constants now have the same typed fast-path boundary
  through `LocalAddFloatConst` and `LocalSubFloatConst`; the VM and generated
  AOT artifact share the operation contract, and the float fixture preserves
  the Interpreter baseline (`3.25`). Typed IR version is now `2` so loaders
  reject artifacts produced before this operation set was introduced.
- Pointer semantics are shared through `ScriptSemanticRuntime` for cell,
  index, field, and global targets. `AddrIndex`, `AddrField`, writable
  `AddrGlobal`, `Deref`, and `StoreIndirect` now have VM/AOT parity tests.
- Hybrid diagnostics carry module identity/version, function identity,
  requested and selected backend, policy epoch, and stable diagnostic ID.
- Interpreter generators retain their immutable `Program` owner through
  dispatcher-managed module instances.
- Interpreter and AOT generators created by dispatcher-owned modules retain
  their original VM/context and immutable module descriptor across a module
  replacement; dynamically loaded AOT descriptors still require an explicit
  module owner token from the loader.
- Generator safepoint migration now covers both directions for `yield` and
  generator-local pending `await`, preserving canonical slots and rejecting
  unsupported nested caller state.
- Source lowering now declares a generator/await function `Safepoint` only when
  its source positions and suspend metadata are complete; incomplete source
  metadata remains `FunctionBoundary` and is rejected for safepoint migration.
- Capture migration preserves shared `ScriptCell` identity instead of copying
  capture values into new cells. A cross-closure differential fixture verifies
  an external Interpreter closure update is observed after Interpreter -> AOT
  migration and again after the AOT -> Interpreter round trip.
- Retired module versions now have an explicit retained-value budget. Closure
  and generator leases are counted, module replacement fails deterministically
  with `ModuleRetentionBudgetExceeded` when the budget is exceeded, and the
  retired version becomes reclaimable after the last lease is released.
- AOT adoption of an Interpreter await continuation preserves multiple caller
  frames and resumes them in order; the original Interpreter continuation
  remains resumable. Generator-owned nested caller migration preserves the
  immediate-to-outer caller order in both backend directions.
- Same-backend AOT nested await captures the active caller frames before the
  bridge returns `Suspended`, and the AOT resume path restores the nested chain
  in order; complete-source-map cross-backend nested await uses the matching
  canonical adoption path in both directions.
- Cross-backend await coverage now includes immediate failure and pending
  continuation failure after migration in both directions; `HostError` code and
  diagnostic text match the Interpreter baseline and no failed continuation is
  replayed.
- Closures retained from an old Interpreter or AOT module are rejected with a
  deterministic module-version diagnostic after replacement; they are never
  rebound to the new function table.
- Nested cross-backend await with incomplete source/state metadata is rejected
  deterministically; the incomplete callee continuation is never resumed as
  an outer continuation. Complete-source-map nested await uses the migration
  path described above.
- The AOT test now compiles a generated `.gen.cpp` artifact with a registration
  loader in a separate process, registers the descriptor, executes its normal
  generated entry, and executes both a single protected region and a nested
  catch-and-reraise entry to verify generated handler unwind. The nested entry
  is compared with an Interpreter baseline and its two lowered exception regions
  are checked before codegen. This artifact path starts from `ScriptFrontend` and
  `ScriptLowerer`, so it exercises the typed-module entry rather than bypassing
  it with a test-only compiler pipeline. The same independently compiled loader
  now executes fixed-arity pure-int direct-call and bounded non-tail recursive
  entries. Generated-artifact coverage proves the explicit scalar frame machine
  for a 10,000-layer mutually recursive non-tail SCC and its recursion-limit
  behavior; exception/suspend/dynamic recursive fixtures remain Legacy.
- Hybrid exception coverage includes Interpreter/AOT throw-and-catch return
  values and direct uncaught exception code, message, and traceback parity.
  The handler diagnostic corpus also covers shared-runtime type, division,
  resource, index, append, and missing-host-registry failures; AOT host
  preflight diagnostics now use the same stable messages as Interpreter.
  Ordinary host/reflected C++ callable exceptions are converted to the stable
  `HostError` diagnostic by both backends, while `bad_alloc` remains on the
  quota path. The focused corpus now includes a real registered reflected
  member function (both direct and receiver-bound dispatch), property getter,
  and constructor fixture, including the member receiver contract in AOT
  `CallValue`. Matching `std::bad_alloc` fixtures now cover reflected method,
  property, and constructor paths and verify the quota diagnostic.
  Multi-frame cross-backend traceback parity plus nested-handler rethrow and
  operand-stack unwind parity are covered; long-running cross-host exception
  regions now have a 128-iteration result/side-effect differential fixture.
  Broader host API type coverage remains open. Typed IR lowering now records balanced
  protected ranges, and generated AOT descriptors carry and validate the
  corresponding exception-region table. Descriptor validation now permits
  properly nested protected ranges but rejects partially overlapping ranges and
  handlers that fall inside their own protected range.
- The generated artifact exception corpus now also covers a non-throwing
  protected region and adjacent protected regions after the nested rethrow
  fixture, plus a cross-function throw/catch path. Each case is compared with an
  Interpreter baseline, lowered through
  `ScriptFrontend`/`ScriptLowerer`, and executed through the independently
  compiled registration loader. Registration validation additionally rejects
  unknown-function and duplicate exception-region entries before loading.
  The same generated artifact loader now executes a 1,000-iteration protected
  loop and checks its 143 caught-exception result, so long-running exception
  lowering is covered beyond the in-memory descriptor corpus.
- The Hybrid exception corpus also covers a host exception raised in a callee and
  caught by a protected region in its caller. Interpreter and AOT return the
  same captured `host function threw` diagnostic text without replaying the host
  side effect.
- The host-exception corpus now includes independently registered
  `std::runtime_error`, `std::logic_error`, and `std::out_of_range` callables;
  Interpreter and AOT normalize all three to the same `HostError` code and
  diagnostic without replaying the callable.
- `ArbitraryPc` AOT descriptors now validate every operation's source map at
  registration time, including exact file/line/column correspondence and
  duplicate-entry rejection; incomplete maps fail deterministically before
  cooperative OSR can be requested.

- Module byte budgets now use a shared `core::CpuMemoryQuota` scope. Interpreter
  and AOT nested calls consume the same quota, allocator headers retain quota
  ownership for escaped values/continuations, and quota failures return the
  deterministic `ResourceLimitExceeded` diagnostic without replay. Coverage
  now also destroys the dispatcher while retaining a returned script value, then
  verifies that the value keeps and later releases its quota ownership. Coverage
  currently targets the default CPU PMR; the corpus covers list, dict, long
  string, bytes, and nested-call values in both backend policies. Host-owned
  external allocations can now use `core::CpuExternalAllocationLease`; coverage
  verifies a lease retained past dispatcher destruction and deterministic quota
  rejection. Host allocations that bypass this explicit contract remain outside
  the evidence.

`**` and `//` now have a backend-neutral numeric contract shared by semantic
runtime, VM, AOT operation dispatch, compiler, and Typed IR. Integer power and
floor division are checked; negative integer powers produce floating-point
results, and division by zero or integer overflow produces a deterministic
diagnostic.

The repeatable component benchmark is `tests/bench/scriptvm_component_benchmark.cpp`
with the reporting driver `tests/bench/scriptvm_component_benchmark.py`. A run of
100,000 iterations repeated three times on the Windows MinGW/Ninja Debug `auto`,
Debug Vulkan, and Release `auto` configurations is recorded in
`artifacts/scriptvm_component_benchmark_2026-08-16.md`; it reports VM dispatch,
shared AOT operation dispatch, semantic helper, reflection bridge, allocation,
and host-call time plus allocator counts. `aot_dispatch` continues to measure the
shared generic AOT runtime executor through the same entry-selection helper used
by Hybrid; its explicitly migration-only legacy descriptor has no generated entry,
so it intentionally
exercises the compatibility fallback. Pure, side-effect-free `int` functions now
also have a restricted native scalar entry: generated artifacts emit direct
typed C++ control flow, while in-memory AOT descriptors use the same checked
native-int executor. Hybrid and direct AOT calls dispatch through a generated
`AotFunctionDescriptorV5::entry` when present and retain the generic executor for
legacy descriptors. Fixed-arity pure-int direct calls, pure-int self-tail
recursion, the restricted mutually-tail-recursive SCC slice, and proven pure-int
non-tail direct-call SCCs and local scalar handlers are included in the generated Native entry.
Suspension, capture, host, indirect-call and dynamic-Value recursive graphs remain
`IrExecutorPrototypeLegacy` until their bounded frame-machine lowering exists. The native frame scope
accounts for the caller before a same-frame generic semantic fallback. Entry
selection rejects budgets, suspension, captures, globals, exceptions, host
effects, unsupported types, and unsupported targets before execution. The
in-memory checked executor caches immutable native-shape validation per AOT
context and uses predecoded operation identities. Unsupported targets retain the
explicit generic path. This is a first native lowering slice, not yet optimized
generated native code for the whole language.
The stress corpus in `tests/bench/scriptvm_stress_benchmark.cpp`
and `tests/bench/scriptvm_stress_benchmark.py` covers integer loops, function
calls, recursion, list indexing/building, dictionary indexing, string
concatenation, ranges, multi-branch control flow, power/floor division, list
mutation, and long-running exception regions. Each case is compiled through
both the legacy Interpreter path
and the production `ScriptFrontend`/`ScriptLowerer` Typed IR path; the Python
driver checks both ArpScript checksums against the equivalent Python case.
The corpus caught and now covers the `MakeRangeFromStack` opcode name and
integer-element lowering, plus complete end-jump patching for `elif` chains in
both compiler paths. These are single-host baselines and differential probes,
not a platform-wide performance gate. The checked-in `release_resource` profile
also applies host-specific wall-clock limits to the Release CTest gate; those
limits are not a cross-platform performance claim. A
bounded `--quick` profile now runs the same twelve checksum cases and is
registered as the native `arp_scriptvm_stress_differential` CTest. The current
Debug `auto` run measures the native scalar path for `int_loop` and
`branch_mix` (0.239 ms and 0.217 ms for 2,000 and 500 iterations respectively);
`function_calls` is 0.263 ms versus Interpreter 7.498 ms. Historical recursive
figures measure the generic AOT executor, not a Native trampoline, and must not
be used as Native-recursion evidence. Container, exception, recursive, and
unsupported numeric cases remain on the generic executor. On
2026-08-16, this twelve-case bounded corpus passed on Debug `auto`, Debug
Vulkan, and Release `auto`; the earlier seven-case full `--scale 1` corpus
also passed on those configurations. A twelve-case full attempt exceeded the
10-minute execution limit because the recursive-fib case is intentionally
unoptimized; keep that full profile in manual/nightly validation while the
bounded profile provides the per-commit differential gate. The component benchmark also has
a checked-in JSON budget
with separate `resource` and `release_resource` profiles for allocation count
and live-byte delta. The runner verifies the executable's `debug`/`release`
profile marker; Debug configurations use `arp_scriptvm_component_budget`, while
A Release configuration registers
`arp_scriptvm_component_release_performance_resource_budget`. Both resource gates cover all
six component rows at the fixed 100,000-iteration profile, and the Release
resource/timing gate passed on 2026-08-16. The timing limits remain scoped to the
recorded Windows MinGW/Ninja host.

On 2026-08-20 the Debug `win-auto-debug` budget gate was rerun after removing
per-operation undefined reflection-carrier construction from the shared
semantic result, AOT IR dispatch temporaries, and the benchmark's measured VM
instances. Reflection type identity now caches the registered `typeinfo` per
C++ type, so failed RTTI probes do not rebuild PMR type names in the hot path.
The six component rows passed the checked-in allocation and live-byte limits
(`vm_dispatch` 333,737 allocations, `aot_dispatch` 833,746,
`semantic_helper` 0, `reflection_bridge` 500,000, `allocation` 675,740,
`host_call` 168,930). This is `Backend Validated` evidence for bounded
runtime allocation behavior; it does not promote the overall AOT/Hybrid design
to `Platform Validated` or `Production Ready`.

The generated-artifact loader also passed on 2026-08-20 after separating
reflection typeinfo construction from builtin registration. `db::rawInstance()`
now provides a registration-free storage path for `type_of<T>()`, while the
public class-registration path retains the builtin bootstrap; a deterministic
`Registering` state and pre-seeded `void` type prevent recursive static-cache
initialization in standalone generated AOT processes. This removes a Windows
MinGW loader deadlock and is `Backend Validated` for the tested artifact path;
it is not a platform-wide reflection initialization claim.

2026-08-21 增量：独立生成工件 loader 现在实际注册并执行源码生成的
`yield await` Native generator 及其精确单层 handler shape。loader 验证 descriptor
的 `Native` 分类、首次 pending continuation、重复 pending 不重放已有 awaitable、
ready yield、terminal `send()` 返回，以及失败 await 注入 handler 后的 fallback
返回；这些路径与静态 descriptor generator contract 共用
`NativeAwaitFrameMachine`。这是 Windows MinGW 独立工件的 `Backend Validated`
证据，不提升 Native generator 子集为 `Platform Validated` 或整体
`Production Ready`。

Current evidence covers Windows MinGW/Ninja `auto` and Vulkan configurations,
Android NDK/Clang cross-compilation, and the Emscripten ScriptVM unit targets.
The Debug `auto`, Debug Vulkan, and Release `auto` runs all pass the 14-test
ScriptVM validation matrix, including the focused VM/AOT
pointer and scalar-fast-path tests, Typed IR/source-lowering validation, semantic-runtime parity,
checked direct-call arity guards, generator safepoint migration, cooperative
operation-boundary OSR, nested caller migration, generator caller rejection,
capture alias preservation, and the bounded twelve-case Python differential
gate. The Emscripten build successfully compiles the `arp_scriptvm_runtime`,
`arp_scriptvm_ir`, `arp_scriptvm_frontend`, `arp_scriptvm_interpreter`, and
`arp_scriptvm_aot_tooling`
packages and links
`arp_scriptvm_typed_ir_tests.js`,
`arp_scriptvm_semantic_runtime_tests.js`, `arp_scriptvm_aot_tests.js`, and
`arp_scriptvm_hybrid_tests.js`. `scripts/verify_scriptvm_wasm_package.mjs` verifies
the four non-empty `.js/.wasm` pairs, Wasm magic, Emscripten loader references,
and absence of `.dll/.so/.dylib`; this is package/build evidence only. After fixing
Wasm recursive direct-call scalar comparison, moving Hybrid session setup inside
the quota exception boundary, and correcting cross-module Native Value frame
stack metadata lookup, all four Wasm targets pass under Node and the 19-test
Windows MinGW ScriptVM matrix also passes. The browser smoke
`build/web-scriptvm-debug/scriptvm-browser-smoke.html`, served over local HTTP,
now executes Typed IR, Semantic Runtime, AOT, and Hybrid successfully with no
browser console warnings or errors. The four ScriptVM Web targets are therefore
`Platform Validated`; the target-local `-sALLOW_MEMORY_GROWTH=0` contract is
required because browser `TextDecoder` and Web Crypto reject views backed by
resizable Wasm memory. This does not promote the overall design: real-device
execution is still open. The
WebGPU browser rendering smoke recorded in
`artifacts/ddgi/ddgi-webgpu-browser-20260816-scriptvm-plan.json` and its PNG
passes at 1920x1080 for 8 frames with finite readbacks, successful presentation
readback, 2.455552 ms GPU timestamp, and no page/request errors. This is runtime
rendering evidence, not full ScriptVM application packaging evidence. Clang
and macOS/iOS Xcode toolchains remain unvalidated. Web package execution is
covered by the Wasm package contract and the ScriptVM browser smoke above. Android
package structure is covered by the NDK contract below, while application/device
runtime evidence remains open. The Android contract currently has a real NDK r26d/API 33
`arm64-v8a` CMake build for both
`arp_scriptvm_aot_tests` and `arp_scriptvm_hybrid_tests`; ELF inspection
confirms AArch64 artifacts with only system `libc.so`, `libm.so`, and `libdl.so`
dependencies. No Android device or emulator was available for execution.
Broader exception-region corpus coverage, optimized-native-PC deoptimization,
cross-platform performance thresholds, and long-term differential validation
remain open; the design is not `Production Ready`.

### Native lowering remaining work

#### 权威未完成清单（2026-08-21）

以下清单与 `feature_plan.md` 的“未完成内容总览”同步；本节只列未关闭的能力，已
通过测试的 exact shape 仍按各自段落声明为 `Contract Complete` 或
`Backend Validated`，不因局部证据提升整体状态。

- **Native CFG 泛化**：复杂/分叉 handler CFG、typed filter、动态 handler target、
  handler-side `await`/`yield` 和更广泛异步 control-flow 仍未进入 Native proof；动态
  调用、动态值/容器类型和复杂 delegation 继续 fail-closed。
- **完整 deoptimization**：当前只允许有完整 source map 和 canonical metadata 的
  cooperative operation-boundary OSR；任意优化 native PC 的即时切换、寄存器/栈恢复、
  handler/capture/caller chain 逆向 materialization 尚未实现，禁止复制或猜测 native PC。
- **语义等价验证**：复杂异常区域、动态异常 payload、完整 generator
  `send`/`throw`/`close` 交叉组合、深层 delegated-await/cross-backend 组合，以及长期
  randomized/fuzz differential corpus 尚未完成。
- **Hybrid 迁移泛化**：复杂 handler 的 AOT ↔ Interpreter 双向 adoption、深层/循环/无界
  delegated-await、动态或无界 cross-module async graph，以及优化 Native frame 的即时
  切换仍未完成；现有实现只覆盖声明过的 bounded graph。
- **平台验证**：Android 实机或 emulator、iOS/Xcode、macOS 实机，以及 pause/resume、
  surface/layer/device loss、swapchain/drawable 和生命周期资源回收仍无完整运行证据。
- **质量与发布门禁**：跨平台性能阈值、热路径 allocation/阻塞证明、长期 fuzz/perf、
  更完整异常/generator corpus 和 optimized native-PC deoptimization 的真实 backend
  证据仍未完成；因此本文档整体不能标记 `Production Ready`。

**关闭规则**：每个条目必须同时具备 contract/negative test、Native 与 Interpreter
differential、对应 backend 或平台 smoke，并记录稳定 capability/diagnostic。缺少其中
任一项时，状态只能保持 `Implementation In Progress`、`Contract Complete` 或已验证的
局部 `Backend Validated`，不得向上提升。

2026-08-20 增量：inline scalar cooperative OSR 的 canonical snapshot 现在同时
携带 locals 与 operand stack 的 `int`/`bool` kind。`StoreLocal`、tail caller
记录和 Interpreter adoption 共用该 metadata；非法 kind 确定性返回
`AotSwitchUnsupported` 且不留下 continuation。该修复已通过 AOT/Hybrid 迁移、
malformed metadata 负向测试及 19 项 ScriptVM CTest，仍不改变本文档对
optimized native-PC deoptimization 和跨平台验证的未完成声明。

2026-08-20 增量：固定 `LoadLocal -> Await -> Return` Native Value 入口现在在
`ArbitraryPc` 且 source map 完整时支持 cooperative operation-boundary OSR；请求会
materialize 为 `IrExecutor` continuation，绝不复制 native PC。若请求发生在 awaitable
poll 过程中，则由已经创建的 await safepoint 吸收，避免重新创建 awaitable。Hybrid
AOT -> Interpreter 门禁同时接受该完整 metadata 的 `ArbitraryPc` await continuation；
迁移只重新 poll 现有 awaitable，不重放 host/call 副作用。该切片已通过 AOT 与真实
Hybrid dispatcher 回归（含 poll 计数和 no-replay 断言），状态仍为 `Contract Complete`。

2026-08-21 增量：专用 Native `CallHost -> Await -> Yield` generator 在 host callback
已经创建 awaitable 后也支持 cooperative OSR。callback 请求会把 awaitable 放入
canonical operand stack，将 resume PC 固化到 `Await`，再由 Interpreter/generic AOT
消费；host callback 只调用一次。Native await generator 入口现在建立
`AotNativeFrameScope`，因此真实 Hybrid dispatcher 的 `requestOsr(Interpreter)` 在
callback 内可观察。该精确 shape 已由 Windows MinGW AOT/Hybrid 回归验证，复杂 host
形状、delegated generator、复杂 handler 和 optimized native-PC deoptimization 仍保持
fail-closed。

2026-08-21 增量：Native Value eligibility 现在允许闭合 typed scalar result
（`int64`、`float`、`bool`、`string`、`bytes`）在没有 `await` 或 handler 的同步
函数中进入 Value frame machine；`Any`、object 和 container result 仍不会因这个
gate 被推断为 Native。新增的 AOT/Native 与 generic executor differential 覆盖了
`int64`、`float`、`bool` 返回值以及 source-level manifest 分类，并修正 Native
Value `LoadBool` 与 Interpreter/AOT descriptor 的 `a` 字段编码契约。该切片为
`Contract Complete`，不扩大动态值、复杂 scalar lowering 或 optimized native-PC
deoptimization 的支持范围。

2026-08-21 增量：Native Value frame machine 的 root、`Call`、closure、method 和
script-constructor 子 frame 现在统一调用 `validateAotArgumentCount` 与
`normalizeAotArguments`。因此 typed `Int64`/`Float` 等参数的 int->typed promotion、
错误类型和错误 arity 均与 Generic AOT/Interpreter 使用同一
`ScriptSemanticRuntime::normalizeArgument` contract；此前 Native Value 初始化只检查
原始 `Value` 数量，可能绕过子 frame 的参数校验。新增 root 与 `Call` child 的成功/失败
differential 测试，状态为 `Contract Complete`，不扩大动态调用或未声明类型 metadata
的 Native 范围。

同一参数 contract 也已接入固定 Native `LoadLocal -> Await -> Return` 入口；首次
执行会在 poll 前完成 arity/type normalization，错误类型不会被误报为 awaitable
`HostError`，也不会创建或重放 awaitable。Native await 的 typed-argument 负向
differential 已覆盖 Generic AOT/Interpreter parity，仍仅适用于已有固定 await
shape。

The following items remain `Implementation In Progress`; passing scalar Native
artifacts must not be used as evidence for them.

- The proven Native `Await` slice and the bounded direct-call `Value` graph
  serialize locals, operand stack, handler stack, resume PC, canonical source-map
label and caller chain. Direct-call graph migration has ready/pending/failed
and Interpreter/AOT differential coverage, including an active handler adopted
from Native Value AOT by the Interpreter. An explicit cell-bearing root entry
now covers `LoadCapture`/`StoreCapture`; its pending-await snapshot preserves
`captureCells`, and AOT -> Interpreter adoption is differentially tested with
an alias mutation while suspended. Native generator `Yield` now uses its own
`NativeYieldFrameMachine`, with canonical generator snapshot/adoption and
Hybrid migration coverage. Its narrow Native shape admits the allocation-free
literal loads `LoadNil`, `LoadBool`, `LoadInt`, `LoadInt64`, and `LoadFloat`,
plus `LoadString` under the same `CpuMemoryQuotaScope` and deterministic
allocation-failure contract as Native Value execution, any metadata-valid
`LoadLocal`, and explicit metadata-valid `LoadCapture` before
`Yield`/terminal `Return`.
The code generator applies the same shape to capture-bearing Typed IR
functions, so their generated descriptors are published as `Native`. The
capture vector is validated against function metadata and retains the same
`ScriptCell` across resumes. Interpreter-to-AOT adoption reselects that Native
resume kind from the generated Native descriptor and validated shape, so the
dispatcher migration path does not silently fall back to the generic executor.
Focused dispatcher migration coverage includes a quota-scoped `LoadString`
generator, in addition to capture aliases and nonzero local slots.
The Native generator machine also admits one exact single-handler shape:
`PushHandler(5) -> LoadInt -> Yield -> PopHandler -> Jump(8) ->
StoreLocal(0) -> LoadInt -> Yield -> LoadInt -> Return(1)`. The protected
yield has handler depth zero; normal resume removes the handler and returns
the terminal literal, while `throw()` removes the handler, converts the
injected value to the canonical diagnostic string, stores it in local 0 and
resumes at the handler yield. AOT and Hybrid tests cover normal, throw,
AOT-to-Interpreter and Interpreter-to-AOT migration. This is `Contract
Complete` for this exact shape only; nested handlers, typed filters, dynamic
handler targets and other Native exception CFGs remain deterministic
`AotSwitchUnsupported` failures.
The Native generator machine now also admits one exact three-level nested-rethrow
shape:
`PushHandler(15) -> PushHandler(12) -> PushHandler(9) -> LoadInt(7) -> Yield ->
PopHandler -> PopHandler -> PopHandler -> Jump(18)`, followed by two handler
blocks that store/load/`Throw`, and an outer handler block
`StoreLocal(0) -> LoadInt(8) -> Yield -> LoadInt(9) -> Return(1)`.
The Typed IR lowering contract records the protected ranges as
`(1,7,15)`, `(2,6,12)`, and `(3,5,9)`; codegen rejects any other operation order,
target, stack/capture/parameter metadata, or safepoint count and emits
`IrExecutorPrototypeLegacy`. Native `throw()` injects at the inner, middle, then
outer handler in order, preserving the canonical handler stack. `ArbitraryPc`
OSR snapshots retain all three handlers, and AOT/Interpreter adoption is covered
in both directions without replaying the first yield. This is `Contract Complete`
for the exact shape only. A second exact four-level shape is also admitted:
`PushHandler(20) -> PushHandler(17) -> PushHandler(14) -> PushHandler(11) ->
LoadInt(7) -> Yield -> PopHandler*4 -> Jump(23)`, with rethrow blocks at
`11/14/17` and the terminal outer handler at `20`; its Typed IR ranges are
`(1,9,20)/(2,8,17)/(3,7,14)/(4,6,11)`. The same Native/Legacy proof, canonical
handler-stack OSR and bidirectional adoption rules apply. Five-level-or-deeper
rethrow, asynchronous handler-side rethrow, arbitrary handler CFGs, and optimized
native-PC deoptimization remain fail-closed.
An exact Native generator shape now admits fixed-parameter or deterministic
capture-argument delegated iteration:
`LoadLocal(0..P-1) -> LoadCapture(0..C-1) -> Call(P+C) -> RangeInit -> RangeNext -> JumpIfFalse ->
LoadLocal -> Yield -> Jump -> LoadInt -> Return`, with the existing
zero-argument form as `P = C = 0`. The parent capture suffix is read directly
from validated `ScriptCell` entries and is the only supported capture source.
The child target must be a same-module,
fixed-arity, zero-capture generator, an exact synchronous collection child,
or a provenance-checked zero-capture `MakeFuncValue -> CallValue` child with
the same restrictions; accepted collection construction is limited to proven
`list`, `dict`, `range`, and `bytes` results. Arguments are loaded from the
contiguous parent parameter prefix and then the deterministic capture suffix,
with one additional proven container-
expression form: literal/known-local loads followed by `MakeList(count)`,
`MakeDict(pairCount)`, or the bounded `MakeRangeFromStack(1|2)` may supply one
fixed child argument; a literal `MakeRange` is also accepted. The construction
uses `ScriptSemanticRuntime::makeList`/`makeDict`/`makeRange`/
`makeRangeFromStack`, so source order, bound consumption, capacity limits and
stack-underflow diagnostics are shared with Interpreter/generic AOT. The
Native machine retains an AOT or
Interpreter child, preserves the parameter-dependent RangeNext retry point
across pending-await migration, and forwards the proven generator child's
`send`/`throw`/`close` protocol. Collection children have no pending-await
state and reuse `ScriptSemanticRuntime::rangeNextCollection`; an accidental
pending marker is rejected. The generator and exact collection forms,
including a capture-bearing parent, are `Contract Complete` only. A bounded five-level Native delegation graph
(`Native root -> Native middle -> Native inner -> ... -> proven synchronous
collection/generator leaf`) is also `Contract Complete`: code generation starts
the depth budget at the root, runtime/adoption count active Native generator
nodes, and AOT/Hybrid tests cover five active Native levels and terminal parity
in both migration directions. A sixth Native delegation level, arbitrary dynamic child calls, computed
expression/container arguments, unsupported container opcodes, and complex
handler graphs remain on
the generic executor or deterministic failure path. The bound is explicit and
is not a claim of general recursive `yield from` equivalence.
The separately admitted Native await-yield generator slice is one or more exact
`LoadLocal(valid slot) | LoadCapture(valid slot) -> Await -> Yield` groups,
followed by `LoadInt -> Return(1)`, with one stack slot, no handlers, two
declared suspend safepoints per group, and a complete await source-map label.
Its pending continuation is explicitly tagged
`NativeAwaitFrameMachine`; it polls the existing awaitable, yields the ready
value without replaying it, and AOT adoption reselects the same machine from
that canonical pending frame while preserving capture-cell identity. AOT and
Hybrid tests cover Native selection, pending continuation kind, repeated
pending polls, ready yield, direct capture source, and migration; a two-await
regression verifies that the second canonical safepoint adopts and resumes only
its own awaitable. This narrow slice is `Contract Complete`; it is not Backend
Validated, Platform Validated, or Production Ready.
The Native code generator and descriptor-runtime layer also have a separately
tested exact handler slice:
`PushHandler(8) -> LoadLocal(0) | LoadCapture(valid slot) -> Await -> Yield ->
LoadInt -> Return(1) -> PopHandler -> Jump(end)`, with
`Pop -> LoadInt | LoadLocal(valid slot) -> Return(1)` at handler target 8. A
failed pending await restores the serialized handler stack and original capture
cells, pushes the failure diagnostic as the handler value, and returns the
declared handler result after AOT adoption. The codegen predicate consumes the
compiler's exact 11-operation `try` lowering, including its unreachable
`PopHandler -> Jump(end)` tail and `ReturnInt` form; source-level
frontend/lowering/manifest tests verify both local and captured await sources
are published as `Native`. This slice is `Contract Complete` only for that
single await source and catch-all `LoadInt | LoadLocal` return shape.
The descriptor runtime also admits `CallHost(binding, 0) -> Await -> Yield ->
LoadInt -> Return(1)` through the same Native generator state machine. The
host result must be a `ScriptAwaitable`; its first pending continuation owns
that result, so repeated pending and ready resume never invoke the host again.
Focused runtime coverage verifies this no-replay rule. This is `Contract
Complete` only for the zero-argument host-registry descriptor contract; source
authoring and arbitrary dynamic `CallValue` remain Legacy. Nested handlers,
capture transforms around await, and other generator
shapes remain on their declared executor. Capture-bearing immediate `MakeClosure -> CallValue`

The same zero-argument host-registry contract admits the exact handler form
`PushHandler(8) -> CallHost(host-index, 0) -> Await -> Yield -> LoadInt ->
Return(1) -> PopHandler -> Jump(end)`, with
`Pop -> LoadLocal(valid fallback) -> Return(1)` at target 8. A missing host
registry/function, host exception, non-awaitable result, or failed await is
unwound through that script handler; a pending continuation owns the first
host result so AOT adoption resumes it without replay. Typed-IR codegen and
runtime tests cover the Native classification, all four failure routes, and
the no-replay adoption path. This handler slice is `Contract Complete` only
for fixed zero arity, one handler, and the exact terminal/fallback shape;
parameterized host calls are admitted only by the ordered fixed-argument
contract below. Nested handlers outside the linear depth-derived await-yield shape
described below remain `IrExecutorPrototypeLegacy`.

The same handler slice additionally accepts one fixed ordered argument layout:
`PushHandler(A+8) -> LoadLocal(0..P-1) -> LoadCapture(0..C-1) ->
CallHost(host-index, A) -> Await -> Yield -> LoadInt -> Return(1) ->
PopHandler -> Jump(end)`, with `A = P + C` and
`Pop -> LoadLocal(fallback) -> Return(1)` at target `A+8`. `A` must be
positive; the local prefix and capture suffix must each be contiguous and in
slot order, and the fallback remains a distinct local parameter. Native builds
the host argument vector in that exact order, snapshots the returned awaitable
and its original capture cells, and preserves the same handler/no-replay
semantics across adoption. Descriptor tests cover local-only, capture-only, and
mixed local/capture pending/failure paths, including capture-cell identity;
Typed-IR codegen tests classify all three exact shapes as `Native`. This is `Contract
Complete` only for this ordered fixed shape; arbitrary stack expressions,
capture mutation before the call, reordered arguments, and nested handlers
remain `IrExecutorPrototypeLegacy`. A metadata-valid but unavailable capture
enters the declared handler without invoking the host; malformed host-argument
state fails deterministically with `AotSwitchUnsupported`. Hybrid migration
coverage also transfers a pending local-argument `CallHost` handler in both
directions: Native AOT to the Interpreter generator and Interpreter to the
Native AOT generator. Both paths resume the existing awaitable and complete
the terminal return without replaying the host. The reverse path materializes
the Interpreter generator through a non-generator wrapper, preserving the
same generator function identity in the canonical frame.

The generator frame machine additionally admits one exact two-level handler
shape: `PushHandler(outer) -> PushHandler(inner) ->
LoadLocal|LoadCapture|CallHost(0) -> Await -> Yield -> LoadInt -> Return`,
followed by the inner `Pop -> LoadLocal -> Return` handler, the outer
`PopHandler -> Jump`, and its `Pop -> LoadLocal -> Return` fallback. Both
handler targets and zero stack depths are serialized in the canonical pending
frame; failed awaits are consumed by the inner handler, while successful
resume pops both handlers before the terminal return. AOT and Hybrid tests
cover source lowering, pending snapshot/adoption, and both migration
directions. This exact two-level shape is `Contract Complete` only; nested
handler-side control flow, capture mutation/aliases, and other exception
graphs remain `IrExecutorPrototypeLegacy`.

2026-08-21 增量：同一双 handler await-yield frame-machine 现在额外接受一个
固定内层 `LoadString -> Throw` rethrow shape。await 失败或 yield 边界注入的
异常先消费内层 handler，再按 canonical handler 栈转移到外层 fallback；动态
异常 payload、任意 handler-side CFG 和 rethrow 后继续执行仍 fail-closed。AOT
runtime、source lowering/manifest、AOT -> Interpreter failed-await unwind、
Interpreter -> AOT reverse migration 和失败/throw regression 已由 Windows
MinGW 实际 Hybrid dispatcher 与 19 项 ScriptVM CTest 覆盖；该精确 shape 现为
`Backend Validated`（Windows MinGW），尚未提升为 `Platform Validated`。复杂
handler、动态异常 payload、任意 handler-side CFG 和 rethrow 后继续执行仍保持
`IrExecutorPrototypeLegacy` 或 deterministic fail-closed。

2026-08-21 增量：上述异步 nested-handler contract 已从固定两层提升为数据派生深度
模板。对任意 `depth >= 2`，Native 只接受
`PushHandler* -> LoadLocal|LoadCapture|CallHost(0) -> Await -> Yield ->
LoadInt -> Return(1)`，随后每个 inner handler 必须是固定的
`PopHandler -> Jump(next) -> Pop -> LoadString -> Throw`，最外层必须是
`PopHandler -> Jump(end) -> Pop -> LoadLocal -> Return(1)`；handler target、
canonical stack depth、fallback local 和 operation count 均由 depth 计算并逐项校验。
两层旧的 inner `LoadLocal -> Return` fallback 兼容路径保留，三层及以上只接受
rethrow 模板。Native await failure、yield 后 `throw()`、AOT adoption 均逐层消费
handler，不重放 awaitable；三层正向回归已通过 Windows MinGW AOT 测试。该扩展为
`Backend Validated`（Windows MinGW），复杂/分叉 handler CFG、typed filter、动态
handler target、handler-side await/yield 仍 deterministic fail-closed，尚未提升为
`Platform Validated` 或 `Production Ready`。

Interpreter generator failed-await unwind now has a direct contract regression for
the caller-bearing case: the await is owned by an ordinary callee below a generator
caller with a handler. A failed poll discards the child frame, injects the diagnostic
into the caller handler, preserves the outer continuation ownership, and never
replays the awaitable. The fixture is covered by the VM target and the existing
AOT/Hybrid/stress matrix; it is a semantic guard, not a widening of Native generator
eligibility.

An independent non-handler capture-mutation slice admits exactly
`LoadInt -> StoreCapture(m) -> LoadCapture(a) -> Await -> Yield ->
LoadInt -> Return(1)`, with distinct, metadata-valid capture slots `m` and
`a`. The mutation executes once before the await; its `ScriptCell` identity is
serialized in the pending frame, and a resumed/adopted generator observes
external updates without replaying the mutation. Source lowering, Native
snapshot/adoption, and the terminal return are covered by regression tests.
This is `Contract Complete` only for this fixed shape; mutation before a
host call, pointer/container aliases, cross-function mutation, and ambiguous
capture merges remain `IrExecutorPrototypeLegacy`.

The provenance-bound dynamic closure generator is now a separate exact slice.
For a parent with `P >= 1` parameters, the fixed stack order is
`LoadLocal(1) ... LoadLocal(P-1) -> LoadLocal(0) ->
CallValue(proven closureTargetFunctionId, P-1) -> Await -> Yield ->
LoadInt -> Return(1)`; local zero is the closure and the remaining locals are
forwarded child arguments. The target must resolve inside the same AOT module,
have exactly `P-1` parameters, exactly `closureCaptureCount` captures, a
`Native` descriptor, and one of the synchronous child shapes: direct
`LoadCapture -> Return(1)`, direct `LoadLocal(valid parameter) -> Return(1)`,
or `LoadInt -> StoreCapture(m) -> LoadCapture(a) -> Return(1)` with distinct
capture slots. The parameter-return form proves the fixed argument forwarding;
the mutation form makes cross-function `ScriptCell` mutation explicit without
introducing a child safepoint. At runtime the closure module pointer/version,
function index/ID, capture count, non-null capture cells, child arity, and
forwarded argument slots are checked before the child is entered. The child is
invoked once through `executeAotFunctionWithCaptures`; pending resume only
polls the saved `ScriptAwaitable`, so AOT adoption and Interpreter/AOT hybrid
migration cannot replay the dynamic call. Unknown, cross-module, generator,
asynchronous-child, or malformed provenance remains
`IrExecutorPrototypeLegacy`/diagnostic failure. AOT and Hybrid tests cover
pending snapshots, fixed argument forwarding, capture replacement while
suspended, both migration directions, terminal return, a mismatched function-ID
rejection, and the cross-function capture-mutation/no-replay path. This slice
is `Contract Complete` only; it is not `Backend Validated`, `Platform
Validated`, or `Production Ready`.

The generic IR executor now has an explicit regression contract for the
unsupported asynchronous-child case: a Legacy parent `CallValue` may suspend
inside a child closure, retain the child continuation and parent caller frame,
then resume the child exactly once before suspending on the parent's own
awaitable. The resumed parent yield and terminal return are covered by
`testLegacyClosureCallAsyncChildGenerator`; this validates the Legacy
continuation semantics without widening the Native eligibility proof. Hybrid
coverage now adopts the same pending chain in both directions. When a child
frame returns and the parent yields, the generic AOT generator also replaces
its active capture vector with the parent frame's cells, so the next terminal
resume cannot retain the child's capture contract. This is `Contract Complete`
for the tested two-await closure chain only; arbitrary dynamic async graphs
remain `IrExecutorPrototypeLegacy`.

The Native dynamic-closure slice now admits one additional asynchronous-child
shape. The child must be a same-module `Native` function with the exact
operations `LoadCapture(awaitable) -> Await -> Pop ->
LoadCapture(resultAwaitable) -> Return(1)`, one await safepoint, matching
parameter/local/capture metadata, and distinct valid await/result capture
slots. The parent remains the fixed provenance-bound
`LoadLocal(closure) -> CallValue(target) -> Await -> Yield -> LoadInt ->
Return(1)` generator shape. The Native frame machine records the child
continuation plus the parent caller frame, polls the child once, then polls
the parent awaitable once; capture vectors are switched back to the parent
before the terminal return. AOT adoption and Interpreter/AOT hybrid migration
are covered in both directions, including pending snapshots and no-replay.
Failure parity is also covered: a failed child await produces `HostError`
without polling the parent, while a failed parent result await is reported
after exactly one child and one parent poll. Neither failure path re-enters
the child or parent callable; the child-failure result is checked independently
on AOT and Interpreter backends.
An interpreter-origin closure without an owner is admitted only when its
function index, capture count, and same-module target contract prove the same
shape during AOT adoption. This is `Contract Complete` for this exact shape
only; arbitrary asynchronous graphs, generator children, cross-module or
owner-bound closures, complex exception/unwind paths, and broader dynamic
`CallValue` remain `IrExecutorPrototypeLegacy` or deterministic diagnostic
failure. It is not `Backend Validated`, `Platform Validated`, or `Production
Ready`.

The Native asynchronous-child proof now has one explicitly bounded nested
slice. The child must have the exact six-operation shape
`LoadCapture(nestedClosure) -> CallValue(grandchild, 0) -> Await -> Pop ->
LoadCapture(parentAwaitable) -> Return(1)`. The nested closure target must be a
same-module `Native`, zero-parameter, no-safepoint synchronous function with
the exact `LoadCapture -> Return(1)` shape; its capture count and function ID
are checked both by code generation and at runtime. The parent remains the
fixed `LoadLocal(closure) -> CallValue(child) -> Await -> Yield -> LoadInt ->
Return(1)` generator. The frame machine enters the grandchild once, retains
its awaitable in the child continuation, then resumes the child and polls the
parent awaitable without replaying either call. AOT -> Interpreter and
Interpreter -> AOT tests cover pending migration, terminal result parity, and
child/parent poll counts. This three-function nested graph is `Contract
Complete` only; dynamic/ambiguous async graphs, generator grandchildren,
cross-module provenance, complex exception/unwind propagation, and general
dynamic `CallValue` remain `IrExecutorPrototypeLegacy` or deterministic
diagnostic failure. It is not `Backend Validated`, `Platform Validated`, or
`Production Ready`.

The nested proof also admits one further fixed async-grandchild form. The
grandchild must be a same-module `Native`, zero-parameter function with the
exact five operations `LoadCapture(awaitable) -> Await -> Pop ->
LoadCapture(resultAwaitable) -> Return(1)`, one safepoint, and two validated
capture slots. The child keeps the six-operation nested shape above and its
`CallValue` metadata must name that grandchild with the exact capture count.
The native machine consumes the provenance-bound grandchild awaitable once. When
that await resolves, the child result capture is treated as a second, explicit
await stage; only after that stage resolves does the machine poll the child's
parent-await capture. The continuation records this intermediate stage and
keeps the canonical caller chain as exactly the child frame followed by the root
generator frame, with fixed PCs, slot counts, capture counts, and empty
operand/handler stacks. AOT resume, AOT -> Interpreter adoption, and
Interpreter -> AOT adoption all preserve this state without replaying either
closure or any already-polled awaitable. Tests cover pending, ready, failed,
no-replay poll counts, terminal result parity, and result-await failure without
polling the parent awaitable. This is a bounded
three-function async-grandchild contract and remains `Contract Complete` only;
dynamic/ambiguous graphs, generator grandchildren, cross-module
closures, complex unwind, and general `CallValue` are still
`IrExecutorPrototypeLegacy` or deterministic diagnostic failure. It is not
`Backend Validated`, `Platform Validated`, or `Production Ready`.

The same-module arbitrary-depth closure-await graph now has a complete
Interpreter -> Native Hybrid migration regression. The Interpreter first
exports the leaf frame plus four canonical caller frames; AOT adoption restores
the Native chain and promotes one caller per resume. Each leaf/caller/root
awaitable is polled exactly once, the completed `CallValue` edges are not
replayed, and the root yield/return values remain equivalent. This is still
`Contract Complete` only; dynamic receivers, ambiguous provenance, and complex
exception/generator subgraphs remain fail-closed. The bounded cross-module
arbitrary-depth closure-await slice is also complete: each closure frame is
resolved through the declared dependency registry, per-frame module/owner/artifact
identity survives AOT -> Interpreter -> AOT snapshot/adoption, and every awaitable
is polled exactly once. Missing foreign Interpreter artifacts fail closed with a
stable diagnostic. This bounded slice is `Contract Complete`; dynamic, unbounded,
or otherwise unproven cross-module graphs remain outside the Native proof.

#### Cross-module closure and cross-child unwind status

The runtime now has a complete identity boundary for the bounded mixed-module
Native closure shape. `AotExecutionFrame` and canonical caller frames carry
module ID/version, descriptor identity, deterministic artifact identity
(source/dependency/reflection/Typed-IR fingerprints), and an optional owner
lifetime token. `AotExecutionContext` binds an `AotModuleRegistry`; foreign
closures and retained continuations resolve through that registry, and missing,
stale, artifact-mismatched, or owner-mismatched entries fail deterministically.

The admitted Native slice is deliberately exact: a Native caller invokes a
foreign Native closure whose frame is `LoadCapture -> Await -> LoadInt -> Add ->
Return`, with a single pending await and a caller frame retained in the
canonical chain. The Native Value frame machine dispatches each frame using its
own module descriptor, preserves the original awaitable/capture cells and
source map, and resumes the same machine after registry-backed adoption. The
caller and foreign child are validated independently for Native Value-frame
shape; no generic executor replay is used. Focused AOT and Hybrid tests cover
pending state, owner/artifact identity, per-frame module provenance, AOT
adoption, terminal result parity, and exactly-once polling. This bounded
mixed-module async closure contract is `Contract Complete` only; it is not
`Backend Validated`, `Platform Validated`, or `Production Ready`.

The bounded cross-module Native closure-call generator contract is now
`Contract Complete`. A root Native generator may use a `CallValue` closure
target whose `arp-fn:<module>/...` identity is declared in the root module's
dependency table. Registry resolution validates the foreign descriptor,
artifact identity, and owner token before the foreign Native child is entered.
The exact child shape is `LoadCapture -> Await -> Pop -> LoadCapture ->
Return`; its pending continuation records the foreign active frame and the
root caller with independent module provenance. Resume polls the child and
parent awaitables exactly once, supports the terminal yield/return sequence,
and rejects owner mismatch deterministically. This slice is `Contract
Complete` only; it is not `Backend Validated`, `Platform Validated`, or
`Production Ready`.

The bounded cross-module capture-cell mutation slice is now also
`Contract Complete`. The foreign child must have the exact shape
`LoadInt -> StoreCapture(m) -> LoadCapture(awaitable) -> Await -> Pop ->
LoadCapture(result) -> Return`, with three distinct validated capture slots.
The mutation is performed once before the child await; the pending canonical
frame retains the same foreign `ScriptCell` vector, and resume consumes the
child and parent awaitables without replaying the store. AOT and Hybrid tests
cover the foreign active frame, alias visibility, exactly-once polling,
AOT-to-Interpreter adoption, and terminal return parity. This remains a
bounded descriptor/runtime contract only; pointer/container aliases, dynamic
module loading, and other capture graphs remain outside Native eligibility. It
is not `Backend Validated`, `Platform Validated`, or `Production Ready`.

The first bounded cross-module module-global contract is now `Contract
Complete`. A Native caller may enter a foreign Native child with the exact
global round-trip shape `LoadInt -> StoreGlobal(slot) -> LoadGlobal(slot) ->
Return(1)`. `AotExecutionContext` carries a root module binding plus an
artifact-identity keyed registry of foreign `ScriptModuleGlobalStorage`
objects; the frame machine and generic executor resolve globals from the
active frame's module provenance. `InterpreterModuleBinding` carries the
corresponding storage and `Vm` selects it by the active `Program` owner. Root
and foreign stores are therefore isolated, and an absent or mismatched foreign
binding fails with `HybridModuleVersionMismatch` instead of aliasing the root
vector. The contract has focused AOT/Interpreter unit coverage, including the
negative missing-binding path. This is `Contract Complete` only; it is not
`Backend Validated`, `Platform Validated`, or `Production Ready`.

The bounded cross-module pointer-alias contract is now also `Contract
Complete`. A foreign child may return `AddrGlobal(slot)`, `AddrIndex(container,
index)`, or `AddrField(instance, field)`; the root stores that pointer in its
own global slot and uses `StoreIndirect`/`Deref` to mutate and read it. The
binding registry guarantees that foreign global storage is selected by the
validated module identity, while index/field pointers retain their owned base
value and target kind. Focused Native Value coverage exercises both index and
field aliases and a missing-binding negative path; the Interpreter path keeps
the equivalent same-runtime address semantics. This remains `Contract Complete`
only and is not `Backend Validated`, `Platform Validated`, or `Production Ready`.

The bounded cross-module container-alias contract is also `Contract Complete`.
The foreign child may only return `LoadGlobal(slot)` where the value is a
validated list/dictionary container; the root performs one `StoreIndex` and
reads the result back. This proves shared container identity without admitting
dynamic pointer graphs, arbitrary nested containers, or aliases crossing
unregistered modules. Both bounded alias shapes remain `Contract Complete`
only; they are not `Backend Validated`, `Platform Validated`, or `Production
Ready`.

Synchronous cross-module exception propagation now has a bounded `Contract
Complete` slice. A foreign child may use `LoadString -> Throw` (with an
unreachable typed return for frame validation), and the root may install one
`PushHandler` around the provenance-validated `CallValue`. Native unwind drops
the foreign frame, restores the root handler's operand depth, and exposes the
same stable error string that Interpreter produces. Repeated execution and
owner/module identity checks are covered. Awaiting children and nested handlers
remain outside this synchronous slice; it is not `Backend Validated`,
`Platform Validated`, or `Production Ready`.

Typed catch filters now have a bounded `Contract Complete` slice. One or more
ordered `except <ScriptDiagnosticCode>[: as name]` clauses are accepted, where
each name must be one of the stable runtime diagnostics (`DivisionByZero`,
`IndexError`, `HostError`, `UserRaise`, and the other codes declared by
`scriptDiagnosticCodeName`). Unknown names, duplicate typed filters, and a
catch-all clause that is not last fail deterministically during sema.
Interpreter and AOT generic/Value-frame unwind resolve filters from the same
`PushHandler` metadata; a non-matching diagnostic continues the same unwind
without executing try-body cleanup, and a matching clause removes only the
remaining sibling handlers before entering its body. Native specialized
entries with filters use the Value-frame/generic contract. This slice is not
`Backend Validated`, `Platform Validated`, or `Production Ready`.

The synchronous typed-filter shape is now explicitly admitted when its body
only uses proven synchronous operations such as `IntDiv`: absence of `Await`
no longer forces `IrExecutorPrototypeLegacy` when the function has an
exception region and a return. The Native Value frame consumes the same
handler targets, normal-return cleanup, and sibling-handler cleanup marker
emitted by Typed IR. The source-lowered `multi_divide` manifest and a matching
20-operation descriptor are covered by the AOT contract test; this remains
`Contract Complete` only and is not `Backend Validated`, `Platform Validated`,
or `Production Ready`.

The same unwind machinery now admits one asynchronous-child failure shape:
the foreign Native child is `LoadCapture -> Await -> Return`, the root has one
`PushHandler` around the provenance-validated `CallValue`, and the child await
must have a complete safepoint/source-map contract. A pending continuation
records the foreign child and root caller; a failed poll is injected into the
restored Value frame chain, so the child is not replayed and the root handler
catches the stable `HostError` message exactly once. The matching successful
poll shape is also covered: the awaited value is pushed into the foreign child,
returned through the restored root caller, and the root completes with exactly
two polls and no retained continuation. These bounded cross-module async
success/failure paths are `Contract Complete` only; nested handlers,
arbitrary async graphs, and unbounded migration while unwinding remain
outside the proof. A further fixed two-level root-handler form is now covered
for the failure case: the foreign child continuation restores the root's inner
and outer handler metadata, the inner catch receives the injected `HostError`,
and the child is still polled exactly once. The bounded cross-backend migration
case is also covered: an AOT root continuation with a registered foreign AOT
child can be adopted by the Interpreter, and a failed child poll is delivered
to the root handler without replaying the child. Handler-side rethrow/control
flow and arbitrary exception graphs remain outside this slice.

The synchronous nested-rethrow exception graph is now a separate bounded
Native Value contract: it permits only value loads/stores, `PushHandler` /
`PopHandler`, jumps, `Throw`, and terminal return operations, with no await,
yield, host call, indirect call, resource access, or dynamic mutation. The
generic Native frame machine consumes the inner handler, rethrows into the
outer handler, restores each recorded operand-stack depth, and preserves the
same diagnostic text as the Interpreter. Generic AOT, Native Value, source
lowering, and generated-manifest tests cover the two-level path. This is
`Contract Complete` only for the synchronous graph; arbitrary-depth async
rethrow and handler-side control flow remain `IrExecutorPrototypeLegacy`.

The following remain outside the Native eligibility proof and retain explicit
`IrExecutorPrototypeLegacy` or deterministic diagnostic failure:

1. Dynamically shaped or unbounded cross-module async graphs. The bounded static
   closure-await chain above is admitted, including its AOT/Interpreter Hybrid
   migration; this does not generalize to arbitrary dynamic call graphs.
2. Cross-module aliases outside the bounded registered-cell, global/index/field
   pointer, and single-container shapes; broader capture graphs remain outside
   the proof. Dynamic module replacement now has its own bounded generation and
   retained-value budget contract, but arbitrary live-object graph migration is
   still rejected deterministically.
3. Cross-child exception propagation outside the bounded synchronous and
   single-async success/failure slices, including the exact two-level root
   handler case above. Handler-side rethrow/control flow and arbitrary async
   graphs still require per-frame exception-region stack, handler
   target/catch type, operand-stack depth, and exact transfer-point
   contracts.
4. Cross-backend migration for foreign frames outside the bounded registered
   Interpreter-program and closure-await slices above. The current contract
   requires a multi-module program binding with matching module/version/artifact
   identity and per-frame program ownership; missing or mismatched bindings are
   rejected deterministically. Dynamically shaped or unproven migration remains
   rejected.

These boundaries keep module identity and lifetime deterministic without
classifying the generic cross-module executor as Native.

The separately admitted metadata-bound indirect-call generator has two exact
forms: `LoadLocal(0) -> CallValue(host-binding, 0) -> Await -> Yield ->
LoadInt -> Return(1)`, or `LoadGlobal(valid slot) -> CallValue(host-binding,
0) -> Await -> Yield -> LoadInt -> Return(1)`, plus the exact one-argument
global form `LoadLocal(0) -> LoadGlobal(valid slot) -> CallValue(host-binding,
1) -> Await -> Yield -> LoadInt -> Return(1)`. The latter generalizes to a
fixed nonzero parameter list: `LoadLocal(0..N-1) -> LoadGlobal(valid slot) ->
CallValue(host-binding, N) -> Await -> Yield -> LoadInt -> Return(1)`, where
`localCount == parameterCount == N` and `maxStack == N + 1`. Its binding must
be a `HostCapability` with the matching fixed arity and `maySuspend=true`;
before the first call, Native execution compares every runtime
`HostCallableContract` field (binding id, signature hash, arity, suspension
flag, and lifetime version) against the descriptor. A pending continuation owns the returned
`ScriptAwaitable`, so Native resume and AOT -> Interpreter generator migration
poll it without replaying the callable. The global form consumes the same
shared global storage on both backends. Descriptor validation permits local
provenance only for its exact first form; all other metadata-bound and arbitrary
local/global `CallValue` flows remain `IrExecutorPrototypeLegacy`. Typed-IR
codegen has matching Native eligibility, and an imported `HostCapability`
frontend/lowering/manifest fixture verifies the global form is source-authorable.
This slice is `Contract Complete` only; it is not Backend Validated, Platform
Validated, or Production Ready.

The Native generator handler contract also admits the exact metadata-bound
global HostCapability form `PushHandler(A+9) -> LoadLocal(0..P-1) ->
LoadCapture(0..C-1) -> LoadGlobal -> CallValue(host-binding, A) -> Await ->
Yield -> LoadInt -> Return(1) -> PopHandler -> Jump(end)`, where `A = P + C`,
the local argument prefix is contiguous, the capture suffix is contiguous, and
the function retains at least one local parameter for the handler fallback.
The handler is `Pop -> LoadLocal(valid fallback) -> Return(1)` at target
`A+9`; the binding fixed arity must equal `A`, and `maxStack` must equal
`A + 1`. The same full HostCallable metadata check occurs before its single
invocation. Pending AOT adoption restores the canonical handler stack and the
original capture cells; a failed await consumes that handler and returns the
fallback local without replaying the callable. Focused descriptor tests cover
the local-only, capture-only, and local-plus-capture forms, and source
frontend/lowering/manifest tests classify each matching imported-host source
shape as `Native`. This is `Contract Complete` only for this exact ordered
global-call handler shape; nested handlers, capture mutation/aliases, reordered
arguments, and other exception control flow remain
`IrExecutorPrototypeLegacy`; the exact two-level generator await-yield shape
is covered separately above, while nested global-call handlers remain Legacy.

The non-handler HostCapability await-yield contract additionally accepts a
closure-only fixed capture argument list: `LoadCapture(0..N-1) -> LoadGlobal
-> CallValue(host-binding, N) -> Await -> Yield -> LoadInt -> Return(1)`, with
zero formal parameters and `maxStack == N + 1`. Unused compiler-reserved local
slots remain in the canonical frame but do not participate in the call. The
Native state machine reads each `ScriptCell` at the single invocation and
retains the cells in its pending snapshot; AOT adoption preserves their
identity and resumes the already-created awaitable without replay. An imported
HostCapability nested-generator source fixture verifies this Native lowering.
This is `Contract Complete` only for the exact capture-load form; capture
mutation before invocation and capture handler flows remain
`IrExecutorPrototypeLegacy`.

The same non-handler contract admits a deterministic mixed list when locals
precede captures: `LoadLocal(0..P-1) -> LoadCapture(0..C-1) -> LoadGlobal ->
CallValue(host-binding, P+C) -> Await -> Yield -> LoadInt -> Return(1)`, where
`P` and `C` are both nonzero and `maxStack == P + C + 1`. Native passes locals
first, then reads each capture cell in order. Pending snapshots and AOT
adoption retain those cells and poll the original awaitable. An imported nested
generator source fixture and a runtime `(4, 38) -> 438` order check cover this
slice. Other source orderings and handler-bearing mixed capture flows remain
`IrExecutorPrototypeLegacy`.

Capture-bearing immediate `MakeClosure -> CallValue`
children also preserve caller chains and `captureCells` across suspension and
cross-backend adoption. Same-module closures stored into a local and later
loaded for `CallValue` are now `Contract Complete`: Typed IR CFG dataflow
records the exact target function ID and capture count only when all incoming
paths agree, includes the fact in its fingerprint, serializes it in the AOT
descriptor, and validates it again against the runtime closure's module,
target and captures. Overwrites, ambiguous control-flow merges, cross-module
values, and arbitrary async call graphs remain outside this proof and stay
`IrExecutorPrototypeLegacy`. A separate, equally
failure-closed extension now admits a same-function
`MakeClosure`/`MakeFuncValue -> StoreGlobal -> LoadGlobal -> CallValue` flow:
the CFG state records each global slot, requires every incoming path to agree,
and clears all global facts after an indirect call, await, or yield. A global
overwrite removes the fact. This slice is `Contract Complete`; initial globals,
cross-function writes, module exports, and all ambiguous merges
remain `IrExecutorPrototypeLegacy`. Focused Native execution coverage also
verifies a capture-bearing closure loaded from an initialized module-global
slot suspends and resumes through its original capture cell without rebuilding
the closure. The same data-flow contract now covers a same-function
`MakeClosure`/`MakeFuncValue -> StoreCapture -> LoadCapture -> CallValue`
flow. Capture facts are initialized as unknown, retained only when every CFG
predecessor agrees, invalidated on overwrite, and cleared after indirect calls,
`await`, and `yield`; no capture alias is inferred. The closure-reachable root
entry is `Native`, while a capture-bearing function is not published as a
zero-capture standalone Native entry. Focused Native runtime coverage verifies
that this path suspends in its leaf and resumes through the captured callable
without replay. Its canonical continuation carries the middle caller's capture
cell and supports both Native AOT -> Interpreter and Interpreter -> Native AOT
adoption; the receiving Native machine revalidates every caller with that
frame's actual capture count rather than assuming zero captures. This
capture-slot slice is `Contract Complete`; incoming
capture values, cross-function capture mutation, aliases through pointers or
containers, and ambiguous merges remain `IrExecutorPrototypeLegacy`.

Imported `ScriptFunction` bindings use a separate static provenance channel. A direct import
may be copied through a typed local, a qualified module field, a constant-index local callable list, a module-initializer global, or a closure capture and then
called through `CallValue` with the original binding id. The frontend clears that fact on
compound assignment, overwrite, capture mutation, and writes in conditional/loop/handler
regions; unresolved or conflicting values therefore remain Interpreter/Legacy. Equal binding
 facts across all reachable branches are retained, and loop facts are retained only when the
 zero-iteration and one-iteration states agree; a missing or conflicting path clears the fact.
 A capture-bearing
child is admitted only from its closure-reachable Native root and is never advertised as a
zero-capture standalone entry. A local or module-init global list literal whose elements are all proven bindings may
also be read through a nonnegative constant index; an equivalent local/module-init global/
capture dictionary is admitted only for string-literal keys. Unknown elements, dynamic indices,
list/dict mutation, index writes, call-like operations, suspension points, and container/pointer
transforms clear the fact and remain fail-closed. This bounded local/global/capture/
constant-index-container alias slice is `Contract Complete`; closure provenance also models the
container/index stack effects needed to retain the enclosing closure target. Arbitrary dynamic
expressions and unbounded async graphs remain excluded.

#### Module-initializer global closure slice: `Contract Complete`

`TypedIrModule::moduleInitFunctionIndex` and the append-only
`AotModuleDescriptorV5::moduleInitFunctionIndex` identify the lifecycle entry
without inferring it from a function name. The index is fingerprinted and
validated; an AOT initializer must be a zero-capture, fixed-arity function and
may not contain `Await` or `Yield`. `HybridExecutionDispatcher` executes that
AOT initializer before every public AOT entry, while internal AOT calls use the
ordinary entry path and do not repeat it. An initializer failure aborts the
requested public entry even when its caller supplied no `VmError` object.

The shared Typed-IR CFG provenance engine is used by both `ScriptFrontend` and
the bytecode compatibility lowering. Its cross-function module-init input is
deliberately narrow and failure-closed: it accepts only a deterministic initializer
`MakeFuncValue -> StoreGlobal`, the exact local forwarding form
`MakeFuncValue -> StoreLocal -> LoadLocal -> StoreGlobal`, or an ordered
read of an already-proven initializer global slot
`StoreGlobal -> LoadGlobal -> StoreGlobal`, followed by a consumer's adjacent
`LoadGlobal -> CallValue`, or the fixed local forwarding form
`LoadGlobal -> StoreLocal -> LoadLocal(argument) -> LoadLocal(callee) -> CallValue`,
with a zero-capture target and exact arity. The
production source fixture proves the complete path through `ScriptFrontend`,
Typed IR, descriptor validation, AOT execution, pending await, and both
AOT -> Interpreter and Interpreter -> AOT migration. Canonical caller frames
require function ID, PC, operand/capture/handler metadata; only the actually
suspended frame requires a source position because callers are not safepoint
lookup keys.

This is not general module-global function-value analysis. Reads of an
unproven initializer global, branches, calls, additional local arguments,
non-adjacent stack values,
locals/captures transforms, CFG merges, aliases through pointers or
containers, cross-module values, exports, arbitrary global mutation, and
dynamic `CallValue` remain `IrExecutorPrototypeLegacy`. A missing or
unproven initializer merely supplies no cross-function global facts; it does
not suppress the same shared engine's function-local provenance analysis. A
linear initializer with operations after `Return` is also rejected for
cross-function facts, so an unreachable store cannot manufacture a Native
call contract.

#### Dynamic callable metadata contract (partial Native slice)

`ArpScriptTypedIr/4` now carries immutable callable bindings for explicitly
declared `from <module> import <name>` and `import <module>; <module>.<name>`
host exports. A binding records a stable
`bindingId`, `HostCapability` origin, module-qualified target ID, signature
hash, fixed arity, `maySuspend`, and nonzero lifetime version. The Typed IR
fingerprint includes both the binding table and each `CallValue` reference.
The generated descriptor carries the same table, and registration rejects an
incomplete, duplicate, unknown, wrong-arity, or unproven
`LoadGlobal -> CallValue` / `LoadGlobal -> LoadField -> CallValue` use before
execution. For the qualified form, the field name must match the binding target.

The Native Value frame machine accepts such a call only when the runtime value
is a `HostCallable` whose complete contract exactly matches the generated
binding. A mismatch, an undeclared awaitable return, or any other callable
family fails closed with `AotSwitchUnsupported`; it does not replay through
`executeAotFunction`. A metadata-bound host call becomes Native only in a frame
with an explicit `Await` contract. `maySuspend` does not itself create a
safepoint.

The sema gate accepts this authoring form without a reflection signature only
for the declared fixed arity, using `any` for argument and result types. It
continues to reject unannotated dynamic host exports. Hosts that require static
parameter/result type checking must still export a reflection function; the
Native AOT binding does not manufacture type information from its hash.

**Explicit ReflectionMember registry slice: `Contract Complete`.** A generated
binding may use `ReflectionMember` only when its host supplies an
`AotReflectionCallableRegistry` entry with the exact binding ID, target ID,
signature hash, fixed arity, suspension flag, nonzero lifetime version, and the
opaque `reflection::function::identity()` of the registered wrapper. The raw
identity is runtime-only and is never serialized. Native execution compares all
of those fields before invocation; a missing registry, metadata drift, or a
same-name function with a different wrapper fails closed with
`AotSwitchUnsupported`. Focused descriptor, code-generation, and runtime
tests cover the admitted invocation and those rejection paths.

The authoring path is also `Contract Complete`: a plugin uses the explicit
`ScriptPluginRegistrar::exportFunction(name, function,
ReflectionCallableContract)` overload, which rejects incomplete metadata or a
wrapper identity that differs from the exported function, a foreign target ID,
or a duplicate binding ID. `ModuleRegistry`
retains that contract, and `ScriptFrontend` converts an imported matching
export into the versioned `ReflectionMember` Typed IR binding. The ordinary
reflection signature remains the sema source of parameter/result types; the
new contract only authorizes Native execution. Typed IR rejects every callable
origin except the explicit `HostCapability` and `ReflectionMember` contracts.
No binding is inferred from a reflection function name, arity, or runtime
value.

The registered reflection slice also covers a `ScriptAwaitable` return when
`maySuspend` is true and the call is followed by the existing Native `Await`
contract. Its pending path snapshots the canonical continuation and its resume
path never re-invokes the reflection function. Focused tests verify that
single-invocation rule, the Native resume kind, canonical-frame validation, and
the resolved value. The same pending canonical frame can be adopted by the
Interpreter, which resumes the already-created awaitable without replaying the
reflection side effect. Conversely, an Interpreter snapshot with the same
function/source-map contract is reconstructed as the receiving Native Value
machine even when it has no caller frames; it does not silently downgrade to
the generic AOT executor. This remains `Contract Complete` only for the
explicitly registered callable and the explicit await shape.

This is not general dynamic-call support. Locals, captures, arbitrary globals,
expression results, unregistered reflection members, and module/script
callables without their own explicit metadata remain
`IrExecutorPrototypeLegacy`. Arbitrary dynamic `CallValue` requires separate
versioned metadata, descriptor validation, Native gates, and
differential/migration coverage.

An explicit `TypedIrCallableOrigin::ScriptFunction` binding now covers two
bounded shapes: a `CallValue` loaded from a directly held local/global closure
value may target a zero-capture function in the same AOT module, or an external
`arp-fn:<module>/<name>` target whose module is declared in the dependency table.
Source module exports opt into this path only with complete
`bindingId/targetId/signatureHash/fixedArity/lifetimeVersion` metadata; sema and
Interpreter still accept exports without that metadata, but AOT does not infer
it. The external path resolves the closure descriptor through
`AotModuleRegistry` and revalidates module identity, function id, signature and
arity before entering the child. The caller remains on the Native Value frame
machine, so the admitted child does not replay or silently fall back. A bounded
foreign zero-capture child with a declared pending `Await` now also has
AOT/Interpreter/Hybrid migration coverage. This is `Contract Complete` for the
bounded source-import shape; arbitrary expression results,
container/pointer-transformed callees,
dynamic/ambiguous async graphs, and general cross-module dynamic calls remain
`IrExecutorPrototypeLegacy`.

The same-module `ScriptMethod` slice now preserves a distinct, versioned
provenance marker for `MakeMethodValue -> StoreLocal -> LoadLocal -> CallValue`,
`MakeMethodValue -> StoreGlobal -> LoadGlobal -> CallValue`, and the equivalent
single-function capture slot path.
It records an exact zero-capture target while keeping the receiver only in the
runtime method value; descriptor validation and the Native frame machine
revalidate the target and receiver-parameter arity. Dynamic `LoadField` now
resolves the same stable method table before this call-shape proof runs. CFG
merges retain this fact only when every incoming method target agrees; focused
alias fixtures verify that matching targets remain `Native`. A pure script-class
method child with a boxed receiver uses the Native Value frame even when its body
is otherwise scalar. The global-slot fact
survives only while every reachable predecessor proves the same target and the
slot has not been overwritten; any indirect call, await, yield, conflicting
CFG merge, or overwrite clears it. Focused codegen and Native pending-await
fixtures verify the Typed IR target/capture marker, Native manifest
classification, stored receiver, canonical caller, and no-replay resume for
this global path. The same exact-target fact is also admitted through one
function's `StoreCapture -> LoadCapture` path: Native pending-await coverage
verifies the shared capture cell, canonical caller capture, and no-replay
resume. Capture aliases through pointers or containers, transformed captures,
cross-function capture mutation, cross-function globals, cross-module methods,
generators, and ambiguous merges remain outside this slice and stay
`IrExecutorPrototypeLegacy`.
The stored-method await regression additionally verifies Native suspension,
canonical caller capture, and AOT-to-Interpreter resumption without replacing
the bound receiver or replaying the call.

Class schema lowering now rejects cyclic script-class inheritance before either
the legacy bytecode adapter or the Typed-IR frontend publishes fields or
methods. Both paths carry a visited-class set through inherited field and
method expansion and return the same deterministic `TypeMismatch` diagnostic;
they never recurse until stack exhaustion or emit a partial AOT schema. This
is `Contract Complete` for malformed inheritance input only and does not widen
the supported cross-module class model.

- The Native Value frame machine now admits the synchronous, non-exceptional
  `LoadString -> Return`, `MakeList -> Return`, `MakeDict -> Return`,
  `MakeRange -> Return`, `MakeRangeFromStack -> Return`, `LoadIndex -> Return`,
  `BuiltinLen -> Return`, `LoadField -> Return`, and declared
  `LoadModule -> LoadModuleExport -> Return` shapes and
  `StoreIndex/StoreField/BuiltinDelete/BuiltinClear -> Return(0)` and
  `BuiltinAppend -> Pop -> Return(0)` shapes and their bounded single-handler
  receiver/capacity diagnostic unwinds and
  `ConvertInt/Int64/Float/String/Bool/Bytes -> Return` shapes and generated
  AOT marks them `Native`; these are narrow value slices, not general
  string/container lowering. A synchronous value frame with
  handler/throw control flow remains on its established specialized or Legacy
  path, except the separately proven `BuiltinLen`/`LoadIndex`/`LoadField`/`StoreField`,
  `LoadModuleExport`, conversion, construction, and container-mutation handler-unwind
  shapes. General
  strings, containers, closures, pointers, reflected values and dynamic
  exception payloads still require per-operation Interpreter/AOT differential
  and generated-artifact smoke coverage; unsupported operations remain
  explicit `IrExecutorPrototypeLegacy`.
- Synchronous `CallHost` in the proven Value graph has a declared native bridge
  and unwind contract. Same-module `ScriptClosure` `CallValue` has a separate
  verified bridge. Synchronous `HostCallable`, reflection-function calls,
  receiver-bound `ScriptMethod`, `ReflectedMethod`, and reflected property
  getter/setter calls have separate verified bridges. Explicitly registered
  `ReflectionMember` bindings also have a versioned Native gate with exact
  wrapper identity; unregistered reflection calls remain Legacy. A registry `CallHost`
  may return `ScriptAwaitable` for a directly following `Await`; its Native
  execution, AOT -> Interpreter migration, no-replay behavior and manifest
  classification are covered. Explicit imported HostCallable metadata now
  covers its own versioned Native gate; explicitly registered reflection
  callables also cover the matching `ScriptAwaitable` return shape. Arbitrary
  dynamic `CallValue` still requires separate metadata and bridges. It may not
  be classified Native by falling back through
  `executeAotFunction` or by replaying a side effect.
- Explicitly valid `HostCallableContract` and registry metadata now share a
  pre-invocation `fixedArity` gate across Interpreter `CallHost`/`CallValue`,
  Generic AOT `CallHost`/`CallValue`, and Native Value bridges. A mismatch returns
  deterministic `WrongArgCount` before user host code runs; an empty legacy
  contract remains dynamically typed. This is `Contract Complete` for the
  arity boundary only, with a focused negative test and the full ScriptVM
  regression passing. The specialized Native generator `CallHost -> Await`
  state machines consume the same registry gate before creating/polling an
  awaitable, including their bounded handler-unwind paths. It does not add
  parameter-type metadata to HostCallable or widen dynamic/reflection call
  lowering.
- Closed dynamic-value returns are now admitted for `Any` literals (`nil`,
  `bool`, `int`, `int64`, `float`), a single-parameter Any identity, the exact
  single-parameter `LoadLocal + Neg/Not + Return` shapes, and the exact
  two-Any-parameter `LoadLocal + LoadLocal + <generic binary> + Return` shapes.
  `<generic binary>` is limited to the versioned arithmetic, bitwise, and
  comparison opcodes implemented by `ScriptSemanticRuntime`; typed-int-only
  opcode variants are not inferred from Any metadata.
  Their execution uses the existing canonical Value constructors,
  `ScriptSemanticRuntime::binary`, `ScriptSemanticRuntime::compare`,
  `ScriptSemanticRuntime::negate`, and `ScriptSemanticRuntime::logicalNot`.
  Generic AOT/Native differential coverage includes string equality, inequality,
  mixed Any comparison, unary results and unary diagnostics, plus source-manifest
  eligibility. A fixed three-block `CmpEq + JumpIfFalse +
  (LoadLocal -> Return)*2` conditional-return CFG is also admitted, with both
  branches covered by Generic AOT/Native differential and manifest tests. This
  slice also admits the exact Any local forwarding shape
  `LoadLocal -> StoreLocal -> LoadLocal -> Return`, with identity differential
  and manifest coverage. This is a bounded `Contract Complete` slice; other dynamic CFG locals,
  object/container propagation, reflection, and arbitrary expressions remain
  `IrExecutorPrototypeLegacy`.
- The first bounded object/container propagation slice is now explicit: a two-Any
  parameter function stores the first Value into local 2, awaits the second
  parameter, performs `LoadLocal(2) -> LoadInt(0) -> LoadInt(42) -> StoreIndex`,
  and returns local 2. The proof requires one linear block, the exact local
  slot, literal index/value, and a single return. Native Value, Generic AOT,
  and Hybrid AOT<->Interpreter adoption all retain the same `reflection::object`
  carrier, so the mutation is visible without cloning or replay. Dynamic index,
  computed values, nested containers, pointer graphs, and non-agreeing CFG paths
  remain `IrExecutorPrototypeLegacy`. This is `Contract Complete` only.
- The nested object/container propagation proof is now generalized to any finite
  linear depth described by Typed IR: each level uses a literal `LoadIndex` and a
  unique non-parameter local carrier, followed by one literal `StoreIndex` into
  the deepest carrier and a return of the original outer carrier. Generic AOT,
  Native Value and the existing Hybrid adoption contract share the same
  `reflection::object` identities; two- and three-level differential plus source
  manifest coverage pass. Dynamic indices/values, branch merges, pointer graphs
  and cross-function alias transforms remain
  `IrExecutorPrototypeLegacy`. This is `Contract Complete` only.
- Same-module cross-function container mutation is now covered by a bounded
  caller/child contract: a fixed-arity child receives the original container,
  performs a literal `StoreIndex`, and the caller crosses an independent
  `await` before returning its original carrier. Generic AOT and Native Value
  differential execution, source manifest classification for both entries, and
  both Hybrid adoption directions pass without replaying the child mutation;
  dynamic callables, cross-module container transforms, pointer graphs and
  dynamic indices remain `IrExecutorPrototypeLegacy`. This is `Contract Complete`
  only.
- Object-field-to-container aliasing is covered by a separate closed shape:
  the object carrier is saved in a unique local before `await`, a fixed `items`
  field is loaded after resumption, and a literal `StoreIndex` mutates that
  field's container before returning the original carrier. Generic AOT, Native
  Value, source-manifest Native classification, and both Hybrid adoption
  directions pass; the canonical continuation preserves object and container
  identity without replaying the field load or mutation. Dynamic field names,
  pointer/container graphs, dynamic index/value expressions and non-linear CFG
  remain `IrExecutorPrototypeLegacy`. This is `Contract Complete` only.
- A fixed-seed generated-artifact differential corpus now emits thirty-two
  independent bounded integer functions: sixteen twelve-operation arithmetic
  cases and sixteen local-counter loop cases. It runs every function through
  the Interpreter baseline, requires a `Native` manifest entry, and compiles,
  registers, and executes the generated C++17 artifact independently. This is
  deterministic regression coverage, not a general fuzzing claim: randomized corpus expansion, Native-PC
  deoptimization/OSR, and platform performance thresholds remain unvalidated.
  Windows MinGW evidence does not constitute macOS/iOS or Android-device
  validation.

### Cross-build package and lifecycle contract evidence

The Android package contract is now a first-class CMake target,
`arp_scriptvm_android_package_contract`. It depends on the AOT, Hybrid, and
Android platform contract executables, then runs
`scripts/verify_scriptvm_android_package.py`. The verifier checks that each
artifact is a non-empty AArch64 `ELF64/DYN`, rejects non-system shared
dependencies, and rejects `libc++_shared.so`; its JSON report records the
artifact sizes, NEEDED entries, and static-STL result. The latest NDK r26d/API
33 `arm64-v8a` run passed for all three artifacts and is recorded in
`build/android-vulkan-debug/scriptvm-android-package-contract.json`. This is a
cross-compiled package/lifecycle contract only: no Android device or emulator
was available, so runtime lifecycle execution remains open.

Cross builds do not execute target-format reflection tools. Emscripten and
Android reflection generation therefore require the host-native
`arp_reflect_codegen`, supplied through `ARP_NATIVE_REFLECT_CODEGEN` or found
in the known native build locations; configuration fails closed when it
is unavailable. The Android package verifier locates both Unix and Windows
NDK `llvm-readelf` names, so the contract is not tied to the host executable
suffix.

The cross-platform CMake path also keeps optional host dependencies optional:
host Vulkan SDK and SDL3 discovery is disabled during cross compilation,
Vulkan/VMA header targets are linked only when they exist, and SDL/Vulkan
window sources are emitted only when SDL3 development headers are available.
This allows ScriptVM Android and Wasm targets to validate their own package
contracts without accidentally consuming host Windows libraries. These fixes
are configuration/linkage contracts, not Apple toolchain or device-runtime
validation; macOS/iOS and Android on-device evidence remain open.

### Fixed five-level nested-rethrow contract

The Native generator frame-machine now admits one additional exact shape with
five properly nested catch-all handlers. The operation sequence is fixed at 30
operations: five `PushHandler` entries targeting `25/22/19/16/13`, a protected
`LoadInt -> Yield`, five handler pops and `Jump(28)`, four handler-side
`StoreLocal(0) -> LoadLocal(0) -> Throw` blocks, then the outer handler's
`StoreLocal(0) -> LoadInt(8) -> Yield -> LoadInt(9) -> Return(1)`.

Typed IR lowering requires five regions
`(1,11,25)/(2,10,22)/(3,9,19)/(4,8,16)/(5,7,13)`, two safepoints, zero
parameters/captures and one stack slot. Any target, operation order, region
metadata or source-position deviation remains `IrExecutorPrototypeLegacy`.
Native resume and injected `throw()` validate the handler stack at each unwind
step; operation-boundary OSR retains all five canonical handlers. Windows
MinGW AOT runtime, Typed IR/codegen positive and negative cases, and both
AOT -> Interpreter -> AOT and Interpreter -> AOT Hybrid migrations pass. This
increment is `Contract Complete` only; it does not claim arbitrary-depth CFG,
async handler-side rethrow, optimized native-PC deoptimization,
Backend/Platform/Production readiness, or device validation.

### Fixed six-level nested-rethrow contract

The Native generator frame-machine now admits a six-handler exact shape. The
35-operation sequence is six `PushHandler` entries targeting `30/27/24/21/18/15`,
`LoadInt -> Yield`, six handler pops and `Jump(33)`, five handler-side
`StoreLocal(0) -> LoadLocal(0) -> Throw` blocks, then the outer handler's
`StoreLocal(0) -> LoadInt(8) -> Yield -> LoadInt(9) -> Return(1)`.

Typed IR lowering requires six regions
`(1,13,30)/(2,12,27)/(3,11,24)/(4,10,21)/(5,9,18)/(6,8,15)`, two safepoints,
zero parameters/captures and one stack slot. Native resume and injected
`throw()` validate all six handler entries and each unwind target; an
`ArbitraryPc` operation-boundary OSR retains the complete canonical handler
stack before continuing in the IR executor. AOT codegen rejects any target,
operation order, region metadata or source contract deviation into
`IrExecutorPrototypeLegacy`. Windows MinGW Native frame-machine, Typed
IR/codegen positive and negative cases, and AOT -> Interpreter -> AOT plus
Interpreter -> AOT Hybrid migration tests pass. This increment is
`Contract Complete` only; seven-level and deeper graphs, arbitrary handler CFG,
async handler-side rethrow, optimized native-PC deoptimization, and
Android/iOS/macOS device validation remain open, so the overall feature is not
`Backend Validated`, `Platform Validated`, or `Production Ready`.

### Fixed seven-level nested-rethrow contract

The Native generator frame-machine now admits a seven-handler exact shape. The
40-operation sequence is seven `PushHandler` entries targeting
`35/32/29/26/23/20/17`, `LoadInt -> Yield`, seven handler pops and `Jump(38)`,
six handler-side `StoreLocal(0) -> LoadLocal(0) -> Throw` blocks, then the
outer handler's `StoreLocal(0) -> LoadInt(8) -> Yield -> LoadInt(9) -> Return(1)`.

Typed IR lowering requires seven regions
`(1,15,35)/(2,14,32)/(3,13,29)/(4,12,26)/(5,11,23)/(6,10,20)/(7,9,17)`,
two safepoints, zero parameters/captures and one stack slot. Native resume and
injected `throw()` validate all seven handler entries and each unwind target;
an `ArbitraryPc` operation-boundary OSR retains the complete canonical handler
stack before continuing in the IR executor. AOT codegen rejects any target,
operation order, region metadata or source contract deviation into
`IrExecutorPrototypeLegacy`. Windows MinGW Native frame-machine, Typed
IR/codegen positive and negative cases, and AOT -> Interpreter -> AOT plus
Interpreter -> AOT Hybrid migration tests cover this shape. This increment is
`Contract Complete` only; eight-level and deeper graphs, arbitrary handler CFG,
async handler-side rethrow, optimized native-PC deoptimization, and
Android/iOS/macOS device validation remain open, so the overall feature is not
`Backend Validated`, `Platform Validated`, or `Production Ready`.

### Fixed eight-level nested-rethrow contract

The Native generator frame-machine now admits an eight-handler exact shape. The
45-operation sequence is eight `PushHandler` entries targeting
`40/37/34/31/28/25/22/19`, `LoadInt -> Yield`, eight handler pops and `Jump(43)`,
seven handler-side `StoreLocal(0) -> LoadLocal(0) -> Throw` blocks, then the
outer handler's `StoreLocal(0) -> LoadInt(8) -> Yield -> LoadInt(9) -> Return(1)`.

Typed IR lowering requires eight regions
`(1,17,40)/(2,16,37)/(3,15,34)/(4,14,31)/(5,13,28)/(6,12,25)/(7,11,22)/(8,10,19)`,
two safepoints, zero parameters/captures and one stack slot. Native resume and
injected `throw()` validate all eight handler entries and each unwind target;
an `ArbitraryPc` operation-boundary OSR retains the complete canonical handler
stack before continuing in the IR executor. AOT codegen rejects any target,
operation order, region metadata or source contract deviation into
`IrExecutorPrototypeLegacy`. Windows MinGW Native frame-machine, Typed
IR/codegen positive and negative cases, and AOT -> Interpreter -> AOT plus
Interpreter -> AOT Hybrid migration tests cover this shape. This increment is
`Contract Complete` only; arbitrary handler CFG,
async handler-side rethrow, optimized native-PC deoptimization, and
Android/iOS/macOS device validation remain open, so the overall feature is not
`Backend Validated`, `Platform Validated`, or `Production Ready`.

### Generic exact nested-rethrow template for depth nine and above

The fixed eight-level table is followed by a data-derived proof for the same
linear template at depth nine and above. The validator derives `depth` from
`operationCount = 5 * depth + 5`, then requires handler targets
`5 * depth - 3 * index`, protected range boundaries
`(index + 1, 2 * depth + 1 - index, 5 * depth - 3 * index)`, the fixed pop/jump
layout, and exactly `depth - 1` rethrow blocks before the outer terminal handler.
The Native frame-machine and injected `throw()` use the same derived handler
stack and unwind target formula, while `ArbitraryPc` OSR preserves that stack.

This is still a bounded semantic contract, not arbitrary exception CFG support:
dynamic targets, typed filters, branch merges, asynchronous handler-side
control flow, and any operation outside the template fail closed. Depth-nine
Typed IR/codegen positive and target-mutation negative tests plus AOT/Hybrid
migration smoke pass. The generic slice is `Contract Complete` only; complex
handler CFG, async handler-side rethrow, optimized native-PC deoptimization,
and Android/iOS/macOS device validation remain open.

### Bounded Hybrid generator close forwarding (2026-08-25)

`HybridExecutionDispatcher::closeGeneratorOn` is the explicit migration-only
close contract. Closing on the current backend recursively closes a delegated
child. A requested backend first adopts the canonical yielded/await frame
without resuming; only a validated adoption may replace the value before the
target backend closes it. A retained AOT child is closed through
`LegacyGeneratorBridge`, preserving its owner and avoiding a fabricated
Interpreter state. A pending-await close only discards the continuation; it
does not poll or claim to cancel the awaitable. Invalid values, missing owners, unavailable safepoints and
unproved graphs return deterministic diagnostics and leave the original
continuation unchanged. Windows Debug covers AOT-parent to Interpreter close
and child forwarding; Release/Web and broader generator protocol evidence are
still open, so this does not change the overall `Implementation In Progress`
status.
