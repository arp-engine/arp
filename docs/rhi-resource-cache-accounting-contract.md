# ResourceCacheAccounting/1

状态：Contract Complete（仅本文metadata计量与消费范围），P0-10/L1-c/3的普通资源预算解耦。契约先于实施；不授权GPU执行、alias、
completion或physical VRAM保证。复用PhysicalBudgetLedger/2的Unknown状态和既有checked
texture footprint、core unsigned arithmetic；不新增device vtable、native handle或第二账本。

## L2语义及不变量

- request显式包含contractVersion与计量域。默认Unspecified、未知域、错误版本均拒绝。
  LogicalDescriptor域是描述符精确的texel/block×mip×layer×sample字节或buffer size。
  它不含driver padding、tiling、alignment、allocation/cache overhead，也不证明descriptor
  能在任意backend创建。原资源creation/capability/loss门禁继续独立生效。
- NativePhysical域本版本明确拒绝；不能以logical bytes或placement requirements替代
  真实resident/allocation上界。成功LogicalDescriptor结果的nativePhysicalObservation始终
  Unknown且所有physical scalar为0；0只作Unknown载体，不表示零占用。
- texture复用TextureUploadDestinationValidation::footprint，检查非零且合法sample数和
  checked sample乘法；format/extent/mip/layer缺失或overflow失败。此计算不是执行shape
  validator，不重写usage/memory/state或放宽原backend shape验证。buffer size必须非零。
- 查询无分配、无native dispatch、无device/queue引用、无文件IO；失败结果logicalBytes=0，
  具有稳定diagnostic，不能作为成功计费。恢复只允许修正metadata/request后重新冷查询。

## 五backend lowering

| backend | LogicalDescriptor | NativePhysical及执行权限 |
| --- | --- | --- |
| Null | Equivalent arithmetic，validator-only | DiagnosticFailure，physical Unknown；无native执行证明 |
| Vulkan | Equivalent arithmetic，和placement capability独立 | DiagnosticFailure，physical Unknown；实际allocation由原backend ledger约束 |
| Metal | Equivalent arithmetic，和heap capability独立 | DiagnosticFailure，physical Unknown；无Apple验证推断 |
| WebGPU | Equivalent arithmetic，不查询placed heap | DiagnosticFailure，physical Unknown；不改变graphics gate |
| WebGL | Equivalent arithmetic，只是metadata计算 | DiagnosticFailure，physical Unknown；生产backend仍不可用 |

unknown backend确定拒绝。所有Equivalent只指算术保持语义，不是GPU执行lowering。

## FrameGraph及pool消费

- compiler显式选择LogicalDescriptor，累加全部graph-owned texture和buffer的logical
  bytes，不将alias texture折叠为physical allocation；所有加法/count溢出必须失败关闭。
  同一物理alias group的多个逻辑texture各计一次，因为它们分别占用cache对象。
- 只有compiled aliasPlan中的texture才调用placement查询；非alias texture不调用该API。
  alias query缺失/非法alignment仍以RG-TEXTURE-PLACEMENT-REQUIREMENTS-UNAVAILABLE拒绝。
  alias placement budget是各组max(native requirement)按该组alignment对齐的checked和，
  单独供packing验证；Null给出validator requirements，不能报告native physical总占用。
- transientPeakBytes保持logical cache预算；transientPhysicalPeakBytes只表示alias placement
  requirements预算，0表示没有alias placement量，不再回填logical值。保留已有API名称，
  以注释和显式domain/version/Unknown observation消除physical总占用误解。
- pool的ordinary cached/pending texture计量使用同一checked logical query；buffer按size。
  alias group的cache charge为所有member texture的logical和，heap不另加，避免混合域及
  双计费。实际native heap allocation及其budget/retirement仍由backend拥有和执行。
- FrameGraphResourcePoolReport显式记录contractVersion、LogicalDescriptor域及Unknown
  nativePhysicalObservation；不得用cachedBytes/pendingBytes表示VRAM。caller的logical
  frame-slot multiplier保留，physical driver预算不因本更改被放宽或冒称已证明。
- completion/fence、resource identity、group topology、cache key、loss和无fence拒绝不变。
  不以本计量迁移关闭其它pending销毁、热路径或平台门槛。

## 测试、证据及退出

保留无placement时普通compile被拒绝的真实red。pure test覆盖五
backend/unknown、缺失domain/version、NativePhysical拒绝、精确mip/layer/block/sample、
overflow、无core CPU分配及Unknown observation。graph fixture证明非alias query调用数0、
alias缺metadata仍拒绝、logical与placement不同数值不串用及聚合溢出；pool以descriptor
logical charge覆盖ordinary/group、pending/cache转换，不能用Null证明native成功。

每批至少pure/contract与Null hidden-frame；完整graph/pool相关回归、真实Vulkan
alias-disabled普通query/graph路径及原WSI hidden/RenderDoc/AI检查，记录源码/工具/二进制。
Metal Windows仅contract；WebGPU host测试不变成真实GPUDevice/browser证明；WebGL只metadata。
完成上述范围才能标限定Contract Complete；全部c/3和P0仍另有退出条件。

## 已验证范围及未关闭门槛（2026-10-17）

`build/webgl1-evidence/p0/resource-cache-accounting/implementation-native-include/`记录
MinGW十七target及47/47 CTest无skip，2006命名输入一致；真实Vulkan普通非alias路径
query=0、3次提交、2次缓存命中，logical cache128B。waitIdle只在该cold测试中显式使用。
`native-verified/`原WSI/RenderDoc回放及AI实际图像检查通过，2007输入一致、三类图依赖
各4、零validation message、320×180蓝色clear；它不证明详细alias内容或WebGL shader。
11份C++经clang-format21.1.8 dry-run及token/string/include集合检查通过。

新增report字段要求所有调用方同版本重编，不声明旧二进制ABI兼容。本次不新增shader。
pool既有总量/计数累加、nextSequence、pending发布异常安全及持续负载需后续独立
contract/负向测试；本文只保证新query、compile聚合与group member sum的checked算术，
不宣称完整pool有界热路径。原completion、BC enabled feature、Apple平台、完整WebGPU
fixture的graphics失败和早前timeline pending异常仍Open。完整c/3、P0与Production Ready
均未关闭；Null受控fence仅证明validator转换，不能代替真实GPU completion。
