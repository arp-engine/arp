# Private range 优化边界（P47 / P152，契约草案）

当前状态是待实现的契约草案，尚非 Contract Complete、Prototype Complete 或 Production Ready。
生产路径仍由 `ScriptSemanticRuntime::makeRange` 完整创建公开的 hlist；没有启用惰性 range。
接受的性能基线仍为 Round52，固定 72 组中 61 组达到同轮 CPython 3.12.9。

## 当前成本与必须复用的实现

`MakeRangeFromStack` 通过 `ExecutionSlotBuffer::applyValueOperation` 装箱边界、弹出 bound，调用
`makeRangeFromStack` / `makeRange`，在任何循环体操作之前检查容器上限并创建完整 hlist。
`IntegerValueBuilder::appendReservedRange` 已共享构造算法，区分负数、线程整数缓存和超出缓存的 owned Value。
`RangeInit` 将真实 source 存入 local，`RangeNext` 通过共享 `CollectionIteration::advance` 获取元素。
已有 `ScalarExpressionPlan::Entry::rangeBody` 仅证明循环体标量区间，不能当作 source 不逃逸的证明。

优先扩展这些入口与不可变 bytecode 验证边界；不得在 benchmark、sample 或 VM opcode 中复制 range 算法。
运行时新增容器及 owned object 继续使用 core allocator/PMR。

## 不能将 range 元素直接替换为序号

线程整数缓存保存真实、可被别名修改的 Value。例如，宿主持有 `makeInt(31)` 并把其 int payload
改为 301，随后 `range(30, 33)` 的中间元素必须是该真实 carrier 及实际值 301，不能重新合成 31。
现有 `testIntegerValueRangeBuilder` 已覆盖该契约，并覆盖负界、反向/空范围、int 极值、缓存边界、
冷缓存不计入脚本 quota、公开 list carrier identity、reserve 失败及释放顺序。
`testIntegerValueCacheBorrow` 已覆盖实际载荷及线程隔离，应继续复用。

任何新表示必须读取原缓存的实际载荷；cold cache、负数及非缓存元素的构造/ownership 也必须有独立结论。
不能只根据静态声明或循环索引推导实际元素值，也不能缓存函数结果或对循环求闭式和。

## 初始准入范围

1. 由私有不可变 bytecode 派生完整 def/use 与 CFG 证明，识别实际 range producer、RangeInit、RangeNext、
   循环出口及所有 source/key/value local 的使用。拒绝 source 的返回、capture、AddrLocal、容器存储、
   host/module/动态 call、await/yield、canonical snapshot、跨 backend continuation 及无法证明的别名。
2. 初始实现只考虑无 handler、无 callback、无 suspension 的私有区间。不能以当前 `rangeBody` 的存在
   或源码中出现 `for` 当作上述证明。source 及元素在出口之后的使用也必须分析。
3. 动态 bounds 仍从真实载荷读取；native/cv、转换及载荷不匹配保留现有 public 语义和原错误 PC。
   容器上限、共享 instruction/session budget、可变 limits 及内存 quota 不能省略。
4. 可先评估完全位于现有缓存区间、全部 entry 已 warm 的独立路径。区间由既有缓存契约导出，
   不增设经验阈值；cold、negative、超缓存、active VM/TLS quota 保持原路径。
   这仅是候选范围，尚未证明或启用，也不能据此声明整个 private range 已完成。

## 表示与回放必须先定案

不能用 nil 或不完整 list 冒充公开 source。若采用私有 ExecutionSlot 表示，必须完整定义 copy/move、
析构、ABI materialization、预算边界、checked arithmetic failure、实际 PC/traceback 以及变更后的 slot 布局成本。
若采用独立 Frame workspace，必须有 source-slot 绑定、嵌套区间预算、容量上限和清理/复制/恢复契约。
不得挪用现有 scratch 的未声明字段或依赖静默 placeholder。

原输入的短预算、溢出或 payload miss 必须停在原指令及其原副作用边界；不能执行部分循环后从 producer
重新开始。若需要 materialization，必须先证明实际 Value、quota/limit、分配及释放顺序的等价性。
这部分仍未解决，生产优化不得先行。

## 分阶段验证

先为 producer/use/escape 分析建立独立正负 contract tests；再实现最小表示与真实 cached-alias、
budget、overflow、limits/quota、native/cv、copy/move/最后 owner 的差分测试。
每阶段保留 Release/Debug 快速 unit 与 hidden/headless smoke。
正确性通过后独立冻结源码和 binary，在构建、测试、原生采样退出后进行同核紧邻控制。
保留所有负样本并复查原输入；有效候选再执行固定全矩阵/offline 和最终 72 组控制。
未通过性能接受的候选不得替换 Round52。
