# ArpScript Typed IR Backend 契约

**规范状态：** Contract Complete  
**契约版本：** 1  
**当前 Schema：** `TypedIrVersion = 7`，`SemanticAbiVersion = 1`
**产品状态：** ScriptVM AOT 仍为 `Implementation In Progress`

[English specification](typed_ir_backend_contract.md)

本文档是 ArpScript frontend 与所有执行 backend 之间的规范性契约。它借鉴 C# 和
ECMA-335 CIL 中有价值的验证模型，但不会把 ArpScript 变成 CLR 语言，也不要求
Typed IR 与 CIL 二进制兼容。

本文所说的“参考 C#”具体指：

- 使用带类型的求值栈，并要求 CFG 汇合点只有一种合法栈状态；
- 使用显式 metadata identity，而不是进程内指针；
- 使用显式受保护区域和确定性异常展开；
- 保持托管对象 identity 和共享引用语义；
- 在语言要求的边界执行受检查的转换和算术；
- 将 async 和 generator 状态表示为版本化状态，而不是 native PC。

ArpScript 仍使用自己的类型系统、diagnostic、reflection 模型、内存配额、模块生命周期和
generator protocol。当本文档与 C# 或 CLR 行为不一致时，以本文档为准。

---

## 1. 规范性措辞

本文中的“必须”对应 `MUST`，“禁止”对应 `MUST NOT`，“应当”对应 `SHOULD`，
“不应”对应 `SHOULD NOT`，“可以”对应 `MAY`，均具有规范性含义。

- **Frontend** 指 semantic analysis 之后的 `ScriptFrontend` 与 `ScriptLowerer`。
- **IR** 指通过验证的 `TypedIrModule`，不指 `Program`、bytecode 或生成的 C++。
- **Backend** 指任何执行或翻译 IR 的 consumer，包括 VM bytecode、DirectNative C++、
  Web/Wasm、未来 JIT 或只做分析的 verifier。
- **Semantic runtime** 指 `ScriptSemanticRuntime` 以及由 `SemanticAbiVersion` 引用的
  版本化 runtime contract。
- **可观察行为** 包括返回值、mutation、alias identity、host effect、diagnostic、
  source position、suspension、unwind、continuation ownership、资源使用和
  no-replay 行为。

本文示例仅用于解释。包含规范性措辞的规则和 conformance 表是强制契约。

---

## 2. 范围与非目标

### 2.1 范围内

本契约定义：

1. module、function、type、storage 和 callable identity；
2. stack machine 与 CFG 不变量；
3. operation 与 effect 语义；
4. call、reflection、exception、suspension 和 generator；
5. memory quota、lifetime 和 ownership；
6. canonical identity 与版本管理；
7. validation、lowering outcome 和稳定 diagnostic；
8. backend conformance 所需的最低 differential 与平台证据。

### 2.2 非目标

本契约不定义：

- CIL assembly 格式、CLR metadata token、CTS、CLS 或 .NET assembly；
- garbage collector 实现或对象内存布局；
- 公开的 Typed IR 序列化 authoring 格式；
- backend-private register allocation、instruction selection 或 optimization；
- 已由 ArpScript 语言规范负责的源码语法；
- native lowering 失败后自动运行 bytecode 的 fallback 权利。

Typed IR 当前是内存内契约。AOT package metadata 继续使用 YAML。引入序列化 IR
必须另行制定版本化 wire contract；本规范不引入 JSON authoring。

---

## 3. 契约层级

唯一合法的实现顺序为：

```text
ArpScript source
  -> lexer/parser
  -> semantic analysis
  -> ScriptTypedModule
  -> canonical TypedIrModule
  -> common structural and semantic verifier
  -> backend capability proof
  -> backend-private lowering
  -> execution
```

以下规则不可绕过：

1. 通过 sema 验证的 AST 及其 canonical Typed IR 定义语言意图。
2. Backend 禁止从 bytecode pattern、生成的 C++ 文本、平台名称或 native 默认值推导
   生产语义。
3. Backend-private optimization 必须保持所有可观察行为。
4. Backend 选择必须在首次可观察 effect 发生前完成。
5. 失败或 suspended 的执行禁止在另一个 backend 上 replay。
6. Interpreter bytecode 只能由 `InterpreterBytecodeLowerer` 从验证通过的
   Typed IR 生成；AST-to-bytecode 与 bytecode-to-IR adapter 均不存在。
7. 新语言行为只有在每个已注册 backend 都给出显式 outcome 和 conformance test 后才算完成。

Interpreter 执行的 backend-private 尾部固定为：

```text
validated TypedIrModule
  -> InterpreterBytecodeLowerer
  -> validated, versioned Program
  -> Vm
```

`Program`、`Instruction` 与 `Opcode` 是 Interpreter 私有契约，禁止出现在 IR 层，
也禁止构成第二条源码编译路径。

---

## 4. C# / CIL 参考映射

| C# / CIL 概念 | ArpScript Typed IR 契约 | 有意保留的差异 |
| --- | --- | --- |
| Evaluation stack | 受 `maxStack` 约束的 typed stack | Runtime value 使用 `Value`，不使用 CLR stack type |
| Method signature | `TypedIrSignature` 加 SHA-256 signature hash | 不使用 CLR calling convention 或 metadata token |
| Local 与 argument | 稳定 local slot 和 `LocalKind` | Capture 是共享 `ScriptCell` identity |
| Assembly/module identity | `moduleId`、`moduleVersion`、dependency table | 不做 assembly probing 或隐式 binding redirect |
| Metadata token | 稳定 function/type/binding ID | 绝不是 pointer、RTTI hash 或 registration index |
| Object reference | 共享 `Value` identity | 使用引擎 reflection object，不使用 CLR object layout |
| `checked` arithmetic | 共享 checked semantic helper | Overflow 行为始终由契约选择，不受编译模式影响 |
| Exception clause | `TypedIrExceptionRegion` | Catch filter 使用稳定 Script diagnostic name |
| `async` state machine | Canonical continuation 与 safepoint schema | 不要求 `Task`，不持久化 native PC |
| Iterator state machine | Generator frame 加 delegation metadata | ArpScript send/throw/close protocol 是显式契约 |
| Verifiable IL | Common IR validator 加 backend proof | 合法 IR 仍可因 backend 不支持而确定性拒绝 |

任何 backend 都不能仅以“符合 C#、C++、Wasm 或 native ABI”为理由证明自身行为正确，
它必须符合 ArpScript 契约。

---

## 5. Module 契约

一个 `TypedIrModule` 在验证通过后不可变，并包含：

- 非空 `moduleId`；
- 正数 `moduleVersion`；
- 精确匹配的 `typedIrVersion` 与 `semanticAbiVersion`；
- 有界 global slot table；
- 显式稳定 `moduleInitFunctionId`，或空 ID；
- dependency、reflection dependency 和 host capability table；
- callable binding 与 script class schema；
- 一个或多个 function；
- canonical structural fingerprint。

### 5.1 Identity

- 本地 function ID 必须等于 `arp-fn:<moduleId>/<functionName>`。
- Module、function、class、callable 和 safepoint ID 必须在 vector relocation、
  address-space 变化和确定性重建后保持稳定。
- ID 禁止包含 native pointer、RTTI hash、timestamp、绝对 staging path 或
  registration-order counter。
- Dependency call 只能通过已声明的 dependency 和 callable binding table 解析。
- 重复的 global slot、name、function ID、binding ID、class name 或 safepoint ID
  都属于非法 IR。

### 5.2 Module 初始化

`moduleInitFunctionId` 是唯一 module initialization entry。Backend 禁止根据
`main` 或 `<module>` 等名称推断初始化函数。

每个 module instance 只初始化一次。失败的 initializer 禁止把部分 global 暴露为已成功
初始化的 module。再次进入失败 initializer 必须创建显式的新 module instance 或使用明确的
retry contract，不能由 backend 隐式决定。

### 5.3 Version retention

Function、closure、generator、awaitable 和 reflected export 保留创建它们的精确
module version。Backend 禁止把旧 frame 或 closure 静默绑定到新 module version。
只有在全部 owner 和 retained runtime state 释放后才能回收旧版本。

---

## 6. 类型系统

### 6.1 IR 类型

v6 类型集合如下：

| IR kind | Runtime 含义 | Nullability |
| --- | --- | --- |
| `Any` | 运行时检查的 `Value` | 由实际 carrier value 表示 |
| `Int` | 有符号 32-bit integer | 不可为 null |
| `Int64` | 有符号 64-bit integer | 不可为 null |
| `Float` | IEEE-754 binary64 | 不可为 null |
| `Bool` | Boolean | 不可为 null |
| `String` | 不可变 script string | carrier 语义由 runtime contract 定义 |
| `Bytes` | 不可变 byte sequence | carrier 语义由 runtime contract 定义 |
| `Object` | Reflection-backed object | 仅在声明允许时可为 null |
| `List` | 可变有序 `Value` container | 仅在声明允许时可为 null |
| `Dict` | 可变 keyed `Value` container | 仅在声明允许时可为 null |
| `Function` | Function、closure 或 bound method value | 仅在声明允许时可为 null |
| `Class` | 精确 `className` 的 instance | 仅在声明允许时可为 null |

`TypedIrTypeRef.nullable` 具有语义意义。Backend 不能因为 native representation
允许 null，就为 non-null parameter 接受 nil。对于 `Class`，`className` 必须非空，
并通过 module 或 reflection contract 解析。

### 6.2 Assignment compatibility

Assignment 和 argument normalization 使用以下规则：

1. 接受精确类型；
2. `Int -> Int64` 是精确 widening conversion；
3. `Int -> Float` 和 `Int64 -> Float` 使用共享 conversion contract；
4. 只有允许 nil 的 nullable/reference contract 才接受 nil；
5. `Any` 必须在消费它的 operation 处做 runtime validation；
6. class value 必须精确符合声明或 reflection binding；
7. 其他 conversion 必须通过显式 `Convert*` operation。

禁止使用 backend-native implicit conversion。

### 6.3 Numeric behavior

- Integer overflow 返回 `IntegerOverflow`；禁止依赖 signed native overflow 实现
  wraparound。
- 除数或模数为零返回 `DivisionByZero`。
- 所有 backend 的 Float representation 都是 binary64。native 默认值不同时，
  backend 必须 lower 为等价 binary64 operation，否则拒绝该 operation。
- Shift width、floor division、power、comparison 和 conversion 语义由对应的
  `ScriptSemanticRuntime` operation 提供。只有在具备 proof 和 differential coverage
  时 backend 才能 inline。
- Fast-path operation 必须与对应的非 fast-path semantic operation 产生完全相同的
  result 和 diagnostic。

### 6.4 Reference 与 alias 语义

ArpScript assignment 和 parameter 传递保持共享 reference identity。所有 alias 都能
观察 mutable list、dict、instance 和 captured cell 的 mutation。除非未来 IR operation
显式要求，否则 backend 禁止引入 deep copy、copy-on-write 或 store-time clone。

`const` 是 frontend 的禁止重新赋值规则，不代表被引用对象具有深层不可变性。

---

## 7. Function 与 storage 契约

一个 `TypedIrFunction` 包含精确 signature 和有界 storage metadata。

- `parameterCount` 必须与 fixed/variadic signature contract 一致。
- `localCount` 包含 parameter、result slot、local 和 capture。
- `captureCount` 必须位于 `localCount` 范围内。
- 每个 local slot 只有一个 `LocalKind`、type 和 live interval。
- `maxStack` 是硬上限，不是 telemetry。
- 读取未初始化 local 属于非法 IR。
- 同一 slot 在重叠 live interval 中禁止具有不兼容类型。
- Variadic argument 在 public entry boundary 只做一次 normalization，并生成声明的
  list shape。
- 无结果返回使用显式 zero-result `Return` contract。Backend 禁止合成 nil 或 undefined
  `Value` 作为可观察结果。

Captured variable 使用共享 cell identity。Function 在 backend 间移动时必须保留相同 cell；
把当前 capture value 复制到新的独立 cell 不具有等价语义。

---

## 8. Evaluation stack 与 CFG

Typed IR v6 是 CIL 风格的 stack IR，不是 SSA。

### 8.1 Stack 不变量

对每个 reachable operation：

1. input stack depth 和 type 必须静态可知；
2. pop 禁止 underflow；
3. output depth 禁止超过 `maxStack`；
4. basic block 所有 incoming edge 必须具有相同 stack depth；
5. incoming stack 对应位置必须具有一种 compatible IR type；
6. handler entry stack shape 必须符合 exception contract；
7. return stack shape 必须符合 function signature；
8. unreachable block 仍必须结构合法，不能通过 metadata 携带非法 target。

v6 没有隐式 phi operation。CFG 汇合点上相同的 stack position 和 local slot 就是 merge
位置。如果类型无法通过 assignment rule 合并，则 IR 非法。

### 8.2 Basic block

- Block ID 必须唯一且稳定。
- Successor 必须显式记录并引用已有 block ID。
- Terminator 没有隐式 fallthrough。
- Conditional branch 只能具有声明的 target 与 fallthrough successor。
- Operation index 使用 flattened canonical block/operation order，并由 safepoint 与
  protected region 引用。
- Branch target operand 必须与 successor table 一致。

Backend 必须消费已验证 target，禁止从熟悉的 instruction sequence 识别 control flow。

Conditional branch 在 typed contract 下只消费 `Bool`。Native C++、Wasm integer
truthiness 或 pointer truthiness 都不能替代 ArpScript boolean operation。

### 8.3 Source position

Source position 从 1 开始；零表示 unknown。任何可能 raise、suspend、调用 host code 或
跨 backend boundary 的 operation 都应当具有有效 source position。Safepoint
switchability 要求有效 source metadata。

Diagnostic 必须在 source 可用时报告 operation source。Backend 可以附加 native debug
information，但不能替换 canonical source。

---

## 9. Operation 契约

`TypedIrOperationRecord.opcode`（`TypedIrOpcode`）是精确 semantic opcode identity。
`operation` 是其大类。数据驱动的 `TypedIrOperationDescriptor` 是 operation name、
category、operand field、stack input/output、control flow、call、effect 与 suspend
属性的唯一 registry。未使用字段必须保持 canonical zero/empty value。

Interpreter `Opcode` 只是私有 lowering target，禁止定义 IR 语义。增加、删除或重新解释
`TypedIrOpcode` 必须提升 `TypedIrVersion`，除非能证明变化不影响语义且向后兼容。

### 9.1 Operation family

| Family | 当前 operation | 必须采用的实现来源 |
| --- | --- | --- |
| Constant/load | `LoadNil`、`LoadBool`、`LoadInt`、`LoadInt64`、`LoadFloat`、`LoadString`、local/capture/global load | 精确 literal/slot contract |
| Store | local/capture/global store、`Pop` | 共享 alias 和 initialization rule |
| Arithmetic | `Add/Sub/Mul/Div/Mod/Pow/FloorDiv`、`Neg` | `ScriptSemanticRuntime` 或已证明的等价实现 |
| Typed arithmetic | `IntAdd/IntSub/IntMul/IntDiv/IntMod` 和 local fast path | Checked scalar helper 加 parity proof |
| Bitwise | and/or/xor/clear/shift | 共享 semantic runtime |
| Compare | generic 与 typed comparison | 共享 semantic runtime |
| Convert | `ConvertInt/Int64/Float/String/Bool/Bytes` | 共享 conversion runtime |
| Control flow | jump 与 typed conditional fast path | 显式 CFG 和 stack proof |
| Call | direct、indirect、method、reflection、host | Callable binding contract |
| Function value | function、closure、method construction | 稳定 target 与 capture provenance |
| Container | list、dict、range、append/delete/clear | 共享 semantic runtime 与 quota |
| Access/address | index、field、local/capture/global address、dereference/store indirect | 版本化 pointer target contract |
| Module | module load 与 export load | 已声明 dependency 与 registry contract |
| Exception | push/pop handler、throw | 显式 protected region contract |
| Suspend | `Yield`、`Await`、range delegation | Safepoint/continuation contract |
| Return | `Return`、`ReturnInt` | 精确 result arity/type |

### 9.2 Stack consumption order

当 conversion、index lookup 或 host behavior 可能失败时，operand order 是可观察行为。
Backend 必须使用 opcode 和共享 semantic helper 定义的 canonical order。例如，
`MakeRangeFromStack` 先消费 `end` 再消费 `start`；index store 按已定义顺序消费
value、index 和 base。Backend 禁止重新排序可能 raise 或带 effect 的 operand。

### 9.3 Operation equivalence

Backend-private instruction 只有在保持以下全部内容时才算等价：

- result type 与 value；
- integer/float edge case；
- alias identity 与 mutation order；
- diagnostic code、source 和 failure boundary；
- effect order；
- quota charge 与 release；
- suspension point 与 no-replay state。

只匹配成功的 scalar result 不足以证明等价。

---

## 10. Call 与 callable binding

每条非平凡 callable edge 由 `TypedIrCallableBinding` 描述：

- 稳定 `bindingId` 和 `targetId`；
- origin：host capability、reflection member 或 script function；
- SHA-256 signature hash；
- fixed arity 与 suspension capability；
- 适用时声明 generator bridge flag；
- 非零 lifetime version。

`TypedIrCallableOrigin::ModuleGlobal` 在 v6 validator 中保留但非法。Frontend 禁止
emit 它，backend 也禁止把它解释为隐式 module lookup。

### 10.1 Direct call

本地 direct target 必须存在于同一 module，并匹配 signature hash、arity 和 capture
contract。External script target 必须引用已声明 dependency。Vector index 不能单独作为
稳定 target identity。

Argument 按从左到右顺序求值，只 normalize 一次，然后提交 callee frame。Normalization
失败时禁止进入 callee 或发布部分 frame。

### 10.2 Indirect call 与 provenance

`CallValue` 只有在 dataflow 证明同 module function ID 和 capture count 时，才能声明
精确 closure target。`kScriptMethodProvenanceCaptureCount` 表示显式 zero-capture
ScriptMethod 情况。

Unknown provenance 是合法 dynamic IR，但不自动具备 DirectNative 资格。Backend 必须
实现完整 dynamic contract，或在任何 effect 发生前返回 `AotSwitchUnsupported`。

### 10.3 Host 与 reflection call

- Host call 必须引用已声明 host capability 和精确 binding。
- Reflection call 必须引用精确 canonical reflection contract 和 owner。
- 禁止通过 source substring、相似名称、registration order 或 artifact 中持久化的
  pointer identity 解析 call binding。
- 标记 `maySuspend = false` 的 call 禁止 suspend。
- 可能写入 host state 的 call 永远不可 replay。
- 缺少 service、owner、signature 或 lifetime version 时必须在调用前失败。

---

## 11. Object、class 与 reflection

`TypedIrScriptClassSchema` 是 script class 的权威 schema：

- class name 唯一；
- field 唯一且按 canonical order 排列；
- method name 映射到稳定 function ID；
- constructor ID 显式记录；空值表示语言默认 constructor。

Instance 在 backend 间保持 reference identity。Field access 使用声明的 schema 或精确
reflection binding。Backend 禁止从 host layout 推断 field，也不能静默创建缺失 field。

Reflected/native value 需要版本化 callable/type binding 和 owner lifetime。无法满足 memory
与 lifetime accounting 的 opaque value 使用现有稳定 unsupported diagnostic 拒绝。

---

## 12. Effect 与顺序

Effect mask 包括：

- `ReadsGlobal`、`WritesGlobal`；
- `ReadsHost`、`WritesHost`；
- `MayRaise`、`MaySuspend`。

Frontend 必须保守包含所有可能 effect。Backend 只有在存在版本化 proof 时才能细化 effect；
禁止从 unknown binding 推断 purity。

具有可观察 effect 的 operation 保持源码求值顺序。除非已经证明等价，否则 optimizer 禁止
跨 call、branch、handler、safepoint 或 quota check 移动、复制或删除它们。

执行 `WritesGlobal` 或 `WritesHost` 后，任何 backend failure 都是该 invocation 的终止结果。
重试其他 backend 会 replay effect，因此被禁止。

---

## 13. Exception 与 diagnostic

### 13.1 Protected region

`TypedIrExceptionRegion` 定义半开 protected range
`[startOperation, endOperation)`、handler operation/block、可选稳定 catch diagnostic
name 和 source。

- Region 必须正确嵌套或完全不相交；部分重叠属于非法 IR。
- Push/pop handler operation 与 region metadata 必须一一对应。
- Handler target 必须是有效 block entry。
- Typed catch name 必须通过 `scriptDiagnosticCodeFromName` 映射。
- 最内层匹配 region 最先处理。
- 未匹配 error 展开 caller，禁止 replay。
- Frame、capture、child generator 和 quota lease 只清理一次。

### 13.2 稳定 diagnostic

对于相同的已验证 IR 和 input，所有 backend 必须产生相同的：

- `ScriptDiagnosticCode`；
- 契约已定义时的 canonical message；
- source file、line 和 column；
- effect commit boundary；
- terminal 或 suspended status。

Backend 可以附加 private telemetry、native stack trace 或 symbol data，但它们不能替代
稳定 diagnostic，也不能在 canonical message 中暴露 nondeterministic pointer。

Backend 不支持的 shape 使用现有稳定 failure，例如 `AotSwitchUnsupported`；不能伪装为
language runtime error 或成功降级。

---

## 14. Suspension、await 与 generator

Suspension 是 semantic state transition，不是 error，也不是 native stack snapshot。

### 14.1 Safepoint

`TypedIrSafepoint` 包含稳定 ID、operation index、resume label、source 和 canonical
live slot。

- `Safepoint` function 中每个 `Yield` 或 `Await` 必须恰好对应一个 safepoint。
- Safepoint 在 opcode 定义的已提交 operation boundary 之后 resume。
- Canonical slot 必须包含 resume 所需的全部 state。
- Continuation 记录 module/function/artifact identity 和 owner。
- Native PC、C++ stack address、Wasm stack address 和 pointer-sized frame layout
  都不是 canonical state。

### 14.2 Switchability

| Value | 契约 |
| --- | --- |
| `FunctionBoundary` | 只能在 entry 前选择 backend |
| `Safepoint` | 只能在完整声明的 safepoint 迁移 |
| `ArbitraryPc` | 每个 operation boundary 都有已验证 deoptimization map |

禁止从普通 CFG metadata 推断 `ArbitraryPc`。Backend 如果无法 materialize 全部所需
local、operand、handler、capture、caller 和 effect-commit state，就必须拒绝切换。

### 14.3 Await

Await 消费一个 awaitable，并执行以下两种结果之一：

1. 完成并 push 一个 normalized result；
2. 提交一个 pending continuation 并返回 suspended。

Completion、exception injection、cancellation 和 close 各自只交付一次。Poll 或 migration
禁止重复原始 host call。

### 14.4 Generator

Generator state 包含 frame storage、resume label、pending injection、handler state、
capture、delegated child、owner 和 quota lease。

`yield from` 只能由显式 `TypedIrDelegationSite` metadata 和 `hasYieldFrom` 授权。
Backend 禁止扫描 `RangeNext + Load + Yield + Jump` 序列识别 delegation。

只有 child bridge 声明所需版本化 capability 时，delegation 才转发 send/throw/close。
不支持的 child kind 必须在 parent 发布部分 continuation 前失败关闭。

---

## 15. Memory、quota 与 lifetime

Memory limit 是 semantic execution limit，不是 advisory telemetry。

每个 backend 必须计费：

- invocation argument 和 pending result；
- module instance 持有的 global；
- local、operand stack 和 handler stack；
- capture 和 shared cell；
- string、bytes、list、dict node 及其 contained value；
- instance 和 reflected/native value；
- awaitable、generator、delegated child 和 continuation；
- backend frame object 和 script state 持有的 external allocation。

规则：

1. 当前 quota 是唯一事实源。
2. 同一真实 allocation 按稳定 allocation identity 只计费一次。
3. 遍历 shared/cyclic graph 时禁止重复计费或无限循环。
4. External allocation 必须提供显式 lease/descriptor。
5. Admission 必须事务化：quota failure 禁止暴露部分 result、continuation、global
   mutation 或 owner transfer。
6. Frame count 与 frame byte 是独立 limit。
7. Telemetry counter 禁止再次加入已经计费的 live byte。
8. Release、replacement 和 adoption 只 transfer 或 resize ownership 一次。

Quota failure 使用 `ResourceLimitExceeded`，除非已有更精确的 budget diagnostic。

---

## 16. Canonical form 与 fingerprint

### 16.1 Canonical form

Set-like collection 按稳定 logical key 排序。Semantic sequence 保留语言顺序。任何序列化
contract 中的 string 都使用 length-delimited encoding；换行拼接不是 canonical serializer。

Canonical form 包含所有 semantic field：

- version 和 module identity；
- global、dependency 和 capability；
- callable 和 class contract；
- function signature/storage/effect/switchability；
- operation identity、operand、source 和 effect；
- CFG successor；
- local metadata；
- safepoint、exception region 和 delegation site。

禁止静默忽略 unknown field。Consumer 必须理解其版本化语义，否则拒绝 IR version。

### 16.2 两种 identity

内存内 `computeTypedIrFingerprint()` fingerprint 用于检测 structural drift，并在 backend
消费前验证。它不是 cryptographic artifact identity。

发布 artifact 还必须对 canonical build contract 使用 SHA-256，其中包括 source、
dependency、reflection、IR fingerprint、compiler/toolchain、host ABI 和 include
contract。Backend 禁止使用 size/mtime 或绝对路径替代 content identity。

---

## 17. Validation pipeline

Validation 包含三个强制层级。

### 17.1 Structural validation

`validateTypedIr()` 验证 version、identity、table uniqueness、range、reference、
signature、CFG metadata、region、safepoint、delegation metadata 和 stored fingerprint。

失败返回 `TypedIrVersionMismatch` 或 `TypedIrInvalid`，所有 backend 都不得运行。

### 17.2 Semantic verification

Common verifier 证明：

- 每个 operation 与汇合点的 stack depth/type；
- definite local initialization；
- slot 与 result compatibility；
- call signature 与 provenance；
- effect conservatism；
- handler entry/unwind shape；
- safepoint live-state completeness；
- 静态可知的 resource bound。

Semantic verification 与 backend 无关。Backend-specific matcher 不能替代它。

### 17.3 Backend capability proof

Common validation 之后，每个 function 恰好得到一种 outcome：

| Outcome | 含义 |
| --- | --- |
| `Native` | Backend 直接保持完整契约 |
| `Equivalent` | 版本化 lowering/emulation 保持所有可观察行为 |
| `Prototype/Legacy` | 仅允许显式 non-production opt-in |
| `DiagnosticFailure` | 执行前返回稳定 fail-closed 结果 |

不存在隐式 `Fallback` outcome。合法 function 可能不被某个 backend 支持；这是 capability
result，不是非法 IR。当前源码树没有生产 Legacy 或 generic executor；
`Prototype/Legacy` 是状态类别，不代表允许恢复它们。

### 17.4 Backend conformance record

每个 backend 必须在 build/test 阶段发布版本化 conformance record，至少包含：

- 稳定 backend ID 和 backend contract version；
- 接受的 Typed IR 与 semantic ABI version；
- 每个 opcode 和受限 shape 的 outcome；
- numeric width/overflow 与 float contract；
- object identity 与 callable binding model；
- exception 与 suspension support level；
- memory quota 与 continuation limit；
- 每个 rejected shape 使用的稳定 diagnostic；
- 每个 `Native` 或 `Equivalent` outcome 的 proof/test ID。

Conformance record 必须来自已编译 backend capability 和 test，禁止根据 OS/backend name
猜测。缺失 entry 等价于 `DiagnosticFailure`。修改 entry 必须同步修改对应 contract 和
differential test。

---

## 18. Backend 义务

### 18.1 VM / bytecode

符合契约的 VM backend 从 canonical Typed IR lower 到 bytecode，并通过共享 semantic
runtime 执行。Bytecode 是 backend-private output，不是第二套语言 contract。

当前独立 AST-to-bytecode compiler 属于过渡路径。它可以保留给 differential test 和
editor migration，但完整 VM conformance 要求生产路径消费同一份已验证 Typed IR，或为
每个 operation 提供确定性一对一 lowering contract proof。

### 18.2 DirectNative C++ / Windows

Backend 只有在 validation 和 capability proof 后才能 emit C++17。生成代码必须使用显式
checked helper、有界 frame、quota-aware allocation、稳定 registration 和结构化 artifact
loading。C++ undefined behavior、native exception default 和 host ABI layout 永远不是
语言语义。

### 18.3 DirectNative Web / Wasm

Web backend 消费相同 IR 和 semantic ABI。它必须保持 binary64、checked integer、
fixed-memory policy、source diagnostic 和 continuation semantics。除非在 state 暴露前
由显式 lowering 映射，否则 Wasm trap 不能替代 Script diagnostic。

### 18.4 未来 JIT 或其他 backend

新 backend 必须提供：

1. operation capability table；
2. common validator consumption；
3. 显式 lowering outcome；
4. differential corpus result；
5. quota/lifetime evidence；
6. target-platform smoke；
7. package 与 symbol/source boundary audit。

新 backend 不能因为自己是首个实现就定义语言行为。

---

## 19. Optimization 规则

Optimization 只有在 common validation 后才合法，并且对相同 IR 和 backend contract
version 必须确定性执行。

允许的例子包括 unboxed checked scalar operation、constant folding、dead pure operation
elimination 和 local slot/register allocation。

Optimizer 禁止：

- 穿过可能 raise 的 conversion 或 host call 做 folding；
- 重新排序 global/host write；
- 删除 quota check 或 owner retention；
- 改变 float width 或 integer overflow；
- 复制 call、await、yield 或 throw；
- 从 bytecode shape 重建 source-level delegation；
- 改变 handler coverage 或 diagnostic source；
- 因为某个 test input 更窄就削弱 dynamic type check。

Optimized 与 unoptimized execution 是 differential-test peer。

---

## 20. Conformance test

每个 backend 必须运行从已验证 IR 生成的同一 canonical corpus。

### 20.1 必需 unit contract

- 每种 type 与显式 conversion；
- stack join、underflow、maxStack 和 definite initialization negative；
- branch target/successor mismatch；
- direct/indirect/host/reflection binding positive 与 negative；
- aliasing、shared capture 和 cyclic container；
- integer overflow、division by zero 和 float edge case；
- nested/disjoint/invalid exception region；
- yield、await、send、throw、close 和 delegation metadata；
- quota boundary、rollback、release 和 shared allocation identity；
- 每个 semantic field 的 fingerprint mutation；
- module version 与 owner mismatch。

### 20.2 必需 differential observation

对每个受支持 function 比较：

- return value 与 runtime type；
- global、container、instance 和 host side effect；
- 可观察时的 alias/capture identity；
- diagnostic code/message/source；
- yielded value 与 resume sequence；
- continuation identity 与 no-replay counter；
- contract checkpoint 的 live/peak/frame memory telemetry。

### 20.3 必需 backend evidence

- 一个快速 unit lane；
- 一个 generated package smoke；
- 每个声明平台一个 platform smoke；
- negative source/build/symbol/package inventory；
- 不依赖 `assert` side effect 的 Release test；
- 来自全新 validation build 的输出，不能使用陈旧 Legacy artifact。

只改动 ScriptVM 语义时不要求 visual/RenderDoc validation。如果 IR 改动通过 consumer
影响 rendered output，则还必须满足 consumer 的 visual contract。

---

## 21. 当前 conformance 与迁移状态

| Component | 当前状态 | 契约说明 |
| --- | --- | --- |
| Typed IR v6 schema 与 structural validator | Contract Complete | 当前 canonical 内存 schema |
| `ScriptFrontend -> ScriptLowerer` | Contract Complete | 生产 source-to-IR 路径 |
| Shared semantic runtime | 覆盖 operation 已 Backend Validated | VM 与 DirectNative 已有 differential coverage |
| Windows DirectNative bounded lane | Platform Validated | 不支持 shape 失败关闭 |
| Web DirectNative bounded lane | Platform Validated | 要求 fixed-memory/package audit |
| VM bytecode production lowering | Backend Validated | 通过版本化 bytecode lowerer 消费验证后的 v7 IR |
| AST-to-bytecode / bytecode-to-IR adapter | 已删除 | Source 与生产 target 不存在 compatibility 路径 |
| 完整 generator/async/complex handler graph | Implementation In Progress | 只有显式证明 shape 为 Native |
| 未来 JIT/mobile backend | 未实现 | 必须满足 18.4 节 |

该表防止把 contract 文档误认为 backend proof。`Contract Complete` 描述接口规范；
`Platform Validated` 需要真实 target evidence。ScriptVM AOT 整体继续保持
`Implementation In Progress`，直到 lifecycle、performance、platform 和完整语言门禁
全部关闭。

---

## 22. Versioning 规则

以下任何可能改变 validation、execution、identity 或 lowering 的变化都必须提升
`TypedIrVersion`：

- 增加或重新解释 operation/operand；
- 改变 stack effect、type compatibility 或 evaluation order；
- 改变 CFG、handler、safepoint 或 delegation metadata；
- 向 fingerprinted structure 增加 semantic field；
- 改变 callable、class、quota 或 continuation contract。

如果共享 runtime operation contract 或 runtime-facing ABI 变化，但 IR shape 仍可读取，
则提升 `SemanticAbiVersion`。

Reader 必须拒绝 unknown version。不存在 best-effort compatibility mode。Migration tool
只有在每个变化字段都有确定性 mapping 和 contract test 时，才能执行显式
version-to-version conversion。

---

## 附录 A：示例

源码：

```python
def add_one(value: int) -> int:
    return value + 1
```

概念 stack IR：

```text
module arp.example v1, ir=6, semantic=1
function arp-fn:arp.example/add_one (int) -> int
  locals: [0 param value:int]
  maxStack: 2
  block b0:
    LoadLocal a=0              [] -> [int]
    LoadInt i64=1              [int] -> [int,int]
    IntAdd                     [int,int] -> [int]
    ReturnInt                  [int] -> []
```

所有 backend 必须返回相同 `int`；等价 operation overflow 时必须在相同 checked
boundary 产生 `IntegerOverflow`；禁止引入会改变 quota 或 diagnostic 的隐藏 allocation
或 fallback。

## 附录 B：Backend review checklist

- [ ] 消费已验证 `TypedIrModule` 和精确 version。
- [ ] 为每个 operation shape 发布显式 outcome。
- [ ] 使用稳定 ID 与 signature hash，不持久化 pointer。
- [ ] 证明 stack/CFG/local/handler 不变量。
- [ ] 保持共享 semantic runtime 的 result 与 diagnostic。
- [ ] 保持 alias、capture、module 和 owner identity。
- [ ] 实现 effect ordering 与 no replay。
- [ ] 不通过猜测实现 safepoint/continuation/delegation metadata。
- [ ] 对完整 memory graph 只 charge/release 一次。
- [ ] 在可观察 effect 前拒绝不支持的合法 IR。
- [ ] 通过 common unit、differential、package 与 platform smoke gate。
- [ ] 使用仓库状态术语，不能以 contract 或单平台证据宣称 Production Ready。
