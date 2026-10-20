# 闭合标量调用区间：后续审查草案

状态：设计审查，尚未 Contract Complete、尚未实现或计时。Round62列表候选冻结期间只整理此草案，不改变接受基线。

## 已核实的执行链

`executeScalarDispatch` 的原 `Call` → `runStaticFunctionSlots` → `runFunctionInternalBoundary<true>` → `runFunctionInternalImpl<true,true>`。

现有 ProvenStaticCall 已将不可变metadata检查降为Debug断言，但每个实际调用仍重新读取mutable frame/argument/recursion/session/quota准入。它们在任意宿主或native/cv回放边界之后必须保留；不能仅因源语言强类型或静态Call就删除。

上一轮两种trampoline、固定整数driver模板、参数batch与frame prepareEmpty均已拒绝。不能重做这些变体并宣称新方向，也不能从源码推断收益。现有采样仅self-IP，不能当inclusive CPU占比。

## 可继续研究的独立契约

只在不可变调用图证明整个同步闭合区间没有host/provider、capture、generator、异常handler、容器/字符串/native边界，并且入口实际载荷全部为owned int时，才可能把mutable准入快照延长到整个区间。应复用现有CFG/调用依赖证明与原准入实现；不能新建另一套调用器或按函数名/递归形状识别benchmark。

需要逐项证明：

1. 静态Call的每个可达callee均满足相同闭合条件，参数/返回值真实表示不会通过未知分支变化。未闭合的依赖默认拒绝，不能仅证明当前函数。
2. 动态argument/locals释放全部为已知primitive，不会运行宿主析构回调、改变limits/session/quota或Program owner。
3. 原冷Frame allocation、真实Frame/PC/traceback、logical recursion depth、指令预算、public carrier身份和释放顺序保持。内存失败必须在原PC诊断，不得把分配失败当成功。
4. 每次实际调用仍检查递归深度、扣原指令、维护真实Frame；只可能复用已证明在区间内不变的准入数据。
5. 任意不满足的实际载荷或预算/overflow回放必须在原PC退出区间，恢复所有原状态后进入共享原准入，不能重执行已完成指令。
6. Debug核对快照与动态原准入，Release只剔除已证明不变的判断。实际类型和原错误语义不能由Debug独占。

实现前先查找并复用现有call-dependency/CFG/helper，确认是否已有相同候选历史；新增通用图算法必须放core。先设计逐budget、shared session、native/cv、host析构换session、冷quota、recursion、异常后恢复的差分测试和headless路径。独立冻结五轮紧邻控制通过后，仍须固定完整72组和全部负输入验收。此方向尚未获得性能证据。

## 第64轮后续审查证据

`InterpreterBytecodeLowerer::analyzeProgramStacks` 已在同一模块收集静态依赖，并用greatest fixed point传播单结果CFG证明；后续闭合调用资格应扩展该受测试的依赖边界，不能另建业务侧图或识别Fibonacci形状。该单结果证明本身不证明没有回调或primitive释放，仍需单独的操作集合与实际载荷准入。

冻结Round64递归标量执行器在地址归一化后与Round52指令一致；GDB真实recursive_fib/1命中该执行器且未命中新增生成器入口flag测试。不能据新增flag源码推断每次递归多一次判断，亦不能把静态一致性当作耗时相同的证明。生产源码已撤回，本草案仍未实现、未Contract Complete。
