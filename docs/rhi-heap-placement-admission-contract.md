# HeapPlacementAdmission/1

状态：Draft，实施前契约；P0-10/L1-c/3。NativeHeapOwnership/1保持有效。
本契约规范DeviceLocal placed texture路径，不授权CPU heap、native completion或Production Ready。

当前限定实现：共享输入/Null及Vulkan native admission、即时allocation、requirements/
bind门禁、独立owner/destroy/free已实现；Metal同步共享前置与失败规则，WebGPU明确拒绝。
`heap-placement-native/implementation-cache-fixed/`统一MinGW十二target和42/42 CTest
无skip，2000命名输入一致；完整graph测试另行通过。原WSI hidden/RenderDoc/AI蓝色clear
回归通过、2001输入一致，仅native admission/owner子集的Contract/Vulkan Backend/
Windows MinGW RTX3050 Platform限定证据。完整五backend实施仍未完成，不能
将本子集标成完整Contract Complete、完整Backend/Platform Validated或Production Ready。
此前联合检查有一次timeline pending访问异常未定位；详情及全部失败见
`rhi-webgl1-validation.md`的HeapPlacementAdmission/1记录。

## L2语义

- heap是实际单一allocation的有界lease，成功createMemoryHeap即有真实native allocation
  （Null仅validator）。禁止返回等待首次bind的size-only Native wrapper。size非零，
  alignment为非零2次幂；本版本MemoryUsage只允许GpuOnly/Transient，均表示DeviceLocal。
  CpuToGpu/GpuToCpu及未知枚举明确拒绝，不能忽略调用方intent。
- texture保持原usage，不偷偷添加或删除功能。extent/layers/mips/sample/cube/format/usage
  必须合法且初始state为Undefined；CPU memory intent拒绝。共享验证复用现有logical
  footprint与core unsigned-range工具；logical bytes只用于validator/逻辑telemetry，不能
  变成native allocation或binding兼容证明。
- heap只能用于原native device；同一原runtime/device的不同creator facade可以共享，
  不接受同backend另一device或伪造heap。身份与capability门禁先于image创建。
- native requirements由与真实placed image逐字段相同的descriptor/flags得到；size、
  alignment、memoryTypeBits和requiresDedicatedAllocation都要核验。偏好dedicated不
  强制，要求dedicated则该alias路径拒绝；不创建dedicated资源冒充共享heap。
- offset同时满足heap和native alignment，完整[ offset, offset+size )位于heap lease及
  实际allocation内；复用core::containsUnsignedRange避免溢出。memory type必须出自
  实际physical memory查询，且属于native requirements位集合；不试bind猜兼容性。
- Vulkan createHeap在查询到的DeviceLocal类型中按类型索引选择首个合格类型，cache可
  复用符合该policy的原allocation。后续image不兼容时明确拒绝，不迁移memory或静默
  选择其它heap/ordinary texture。Metal使用Private placement heap与实际size/alignment。
- 所有失败输出null/{0,0}和稳定diagnostic；native query失败绝不返回logical estimate。
  unknown/unsupported请求必须在持久native allocation/bind副作用前拒绝；冷imageformat/
  image requirements查询可有临时image，其生命周期必须可追溯且在失败后销毁。

## 五backend lowering

| backend | 结果与执行门禁 |
| --- | --- |
| Null | ValidatorOnly：共享逻辑shape/range验证；heap由原device ledger标识，foreign heap拒绝；checked logical requirements明确不是native facts；placed validator保留heap accounting lease至最后texture释放，不证明physical alias |
| Vulkan | Native：真实准入runtime、ExplicitMemoryControl lowering及caller alias开关三者一致；真实allocation在createHeap前完成；原device dispatch、imageformat limits、requirements2/dedicated、绑定前全部验证。core optional/unknown能力不推断 |
| Metal | Native或DiagnosticFailure：原native heap/device与capability检查保留；同步共用intent/range前置，实际Private heap/placement query必须成功；不以Windows stub或logical estimate证明成功 |
| WebGPU | Degraded/DiagnosticFailure：无placed primitive；heap/placed/query统一失败，不返回ordinary texture或logical estimate来伪装native placement |
| WebGL | Degraded/DiagnosticFailure：生产backend未实现，无native heap alias；不以ordered issue/CPU记录替代 |

## 预算、lifetime、异常和热路径

- 所有native handle/backend dispatch在backend-private模块；上层只使用原L2接口。
  payload/heap/cache保留原runtime；image先destroy再free memory，native清理后释放ledger。
- 先分配owned wrapper、预算token及cache capacity，再调用native allocate/create；native
  失败输出垃圾handle不能adopt。成功handle必须立即进入已存在的RAII owner；后续host
  异常不得泄漏memory/image或预算，cache发布后不得再靠抛异常的分配维持成功语义。
- pool沿用原shared ledger，不另建账；native allocation数量上界来自真实limits与显式
  byte budget（每个非零heap至少占用1 byte）。该上界不是预分配请求；不得按驱动理论
  上限一次性reserve全部host槽位。cache miss只在cold acquire、native allocate之前
  为本次唯一新增entry预留槽位；失败不调用native allocate，成功native allocation后
  不再执行可能抛异常的host分配。已有entry复用不增长容器。完整steady-state仍Open。
- 实际device-loss结果进入原runtime loss latch；物理query/host注入不能冒充queue完成。
  所有拒绝/失败不签发completion、不能重用pending heap；既有原fence退休规则保留。
- 冷创建、requirements query和未命中allocation不作steady-state成功保证；完整c/3仍需
  workload/limits驱动的复用/容量/无等待/分配证据。不得为避开该门槛删除alias需求。

## 稳定诊断

共享前缀RHI-HEAP-：SIZE-ZERO、ALIGNMENT-INVALID、MEMORY-INTENT、TEXTURE-INTENT、
TEXTURE-SHAPE、TEXTURE-BYTE-OVERFLOW、REQUIREMENTS-INVALID、OFFSET-ALIGNMENT、RANGE。
backend专属前缀RHI-VULKAN-HEAP-：CAPABILITY-UNAVAILABLE、RUNTIME-UNAVAILABLE、
ENTRYPOINT-UNAVAILABLE、OWNER-MISMATCH、NATIVE-LIMITS、HOST-ALLOCATION-FAILED、
BUDGET-REJECTED、NATIVE-ALLOCATION-FAILED、IMAGE-FORMAT-UNSUPPORTED、IMAGE-CREATE-FAILED、
DEDICATED-REQUIRED、MEMORY-TYPE-MISMATCH、BIND-FAILED。device lost另有DEVICE-LOST。
共享footprint拒绝保留既有RHI-TEXTURE-DESTINATION-*诊断；Null heap另有
RHI-NULL-HEAP-BUDGET-REJECTED和RHI-NULL-HEAP-HOST-ALLOCATION-FAILED。
当前Null heap通过既有lastMemoryHeapCreationDiagnostic发布稳定诊断；placed/query的
公开接口仍仅返回null/{0,0}，详细operation诊断发布方式与native路径一并保持待完成。
拒绝路径不能新建可以执行的Dummy；恢复只允许在合法原device重建确定失败的资源，
不重试未知queue工作或宣称lost device已恢复。

## 验证和退出

先注册safe red：invalid heap alignment/intent、capability false、foreign heap（native
调用前）、invalid offset/range、query失败返回0。red不得向真实driver提交跨device或
非法bind；可拦截尚未验证的native调用并拒绝以安全复现“进入了禁止路径”，这种注入
只证明门禁缺失。正向必须走真实native create/requirements/bind/destroy/free。

快速pure contract覆盖五backend结论、边界/overflow/unknown intent及禁止路径；Null
覆盖foreign/range/lifetime/预算归零。Vulkan真实同device非零offset、多image alias、
原creator释放、cache复用、销毁次序，以及native/host故障的清理/预算/调用计数。
保留原MinGW/Null/WSI hidden/RenderDoc/AI回归、输入/工具/binary/capture身份。
Metal/WebGPU分别给出实际平台证据或明确未验证；不能因Null/Windows通过升级全平台。

## Placement requirements消费者准入（实施前补充）

`{0,0}`表示拒绝，不能再被FrameGraph解释为零字节预算。FrameGraph compile和alias
packing消费者必须验证非零size与2次幂alignment，缺失时在native资源创建/record前
以`RG-TEXTURE-PLACEMENT-REQUIREMENTS-UNAVAILABLE`失败，不改用logical estimate。
Null提供validator事实；Vulkan/Metal只消费已准入native query；WebGPU/WebGL placement
拒绝，因此依赖此查询的旧graph预算路径也必须拒绝。普通非alias资源的独立、中立预算
查询域尚待后续契约解耦；本批不承诺其WebGPU/降级Metal graph执行可用，不静默计零。
此门禁同时覆盖alias开启/关闭的Null负向fixture；它不证明任何native allocation。

## 当前剩余门槛

- cold cache理论数量上限不作为一次性host reserve量；首次约64GiB预留的真实smoke
  失败及quota安全red保留。最终实现按冷创建需求在native allocate前预留一entry；这仅
  解决准入与异常安全，不证明完整steady-state/workload热路径。
- 普通非alias预算计量域已按后续ResourceCacheAccounting/1解耦，限定证据见文末；真实
  Metal/WebGPU graph平台门槛不由metadata算术通过推导。
- BC placement仍拒绝：缺少已启用compression feature的authenticated capture。
- Metal Apple原生所有权/异常、cache持续负载/pending/alias内容验证仍Open。
- scoped WebGPU placement拒绝通过不代表完整WebGPU suite通过；旧fixture capability
  失败及早前timeline pending原生异常都保留，详见验证索引和主计划18.6。

## ResourceCacheAccounting/1后续更新（2026-10-17）

普通计量域已解耦：compiler仅真实alias plan成员消费placement query；非alias资源
使用显式LogicalDescriptor元数据算术，nativePhysicalObservation为Unknown，不将
logical bytes改名为native requirements。alias预算0保持0，手写fixture缺/不足预算拒绝。
前述“旧graph普通路径拒绝”是此前快照；本批真实Vulkan alias关闭路径已验证，WebGPU/
Metal metadata契约通过不等于其真实graph平台已通过。证据为resource-cache-accounting
的47/47与原WSI/RenderDoc/AI回归；详见该独立contract和主计划18.7。
完整heap契约仍Draft：cache/持续workload/pending/内容、总量上界与发布异常、BC enabled
feature、Apple和原native稳定性门槛继续Open，不提升完整c/3或生产WebGL状态。


## 2026-10-17 alias clear限定内容补证

AliasClearPreservation/1已取得真实新capture的A/B同memory/offset、C不同memory，
以及clear/Store/Load和B clear后C保持的4帧像素证据；原WSI/RenderDoc/AI五图通过。
具体56/56及2011/2012输入、safe red和范围见主计划18.9及独立contract。此前alias
内容gate仅此clear子集关闭；shader采样/draw/持续负载、原pending completion、全维度
容量与无分配reset、BC、Apple/完整WebGPU和native异常仍Open，完整本契约仍Draft。
