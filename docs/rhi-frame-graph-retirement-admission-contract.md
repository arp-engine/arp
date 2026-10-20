# FrameGraphRetirementAdmission/1

状态：Draft，P0-10/L1-c/3后续实施前契约。当前没有此版本的执行授权；
ResourceCacheAccounting/1的限定计量结果保持有效，不能推导完整pool有界或异常安全。

## L2 intent与不变量

- frame retirement是graph-owned ordinary texture、buffer和不可拆alias group的托管意图。
  logical charge沿用ResourceCacheAccounting/1；native physical observation仍Unknown。
  原device、pool generation、compiled topology、resource identity和实际submit结果必须绑定。
- cold prepare在任何不可撤销GPU工作前核对全部checked bytes/count/slot/CPU metadata预算，
  并预留之后publish/reset/retire所需存储。容量取自版本化caller workload及实际已配置
  frame slots；零/缺失预算明确拒绝，不采用UINT_MAX默认无限量或理论驱动上限reserve。
  同一pool的in-use/reserved、pending和cached都必须受声明的有界策略约束，不能仅evict cache。
- admission结果是原pool generation的单次lease，必须由执行器消费。它只保证托管容量，
  不签发GPU completion、external publication、alias复用或host readback权限。
- 准入失败在queue副作用前返回稳定诊断，原资源所有权和所有计数不变。部分accepted/
  submit-unproven必须进入原RHI已声明的保留路径；不能因submit.success=false取消托管。
  取消仅适用于确定没有accepted工作且未发布的lease，不得从host返回速度推导此事实。
- publish只能做已预留容量内的nonthrowing move和checked状态迁移。计数在所有权成功转移
  时一次提交；不能先增pendingBytes再尝试可抛异常的vector.push_back，或先扣pending再
  尝试可失败的cache节点创建。错误路径不能遗留count/bytes/owner矛盾。
- retirement只消费与原工作绑定的真实completion。不同device/pool的fence不能授权；
  Null受控fence仅validator，不能证明native等价。无fence不等于完成，未知完成不得释放。
- cache发布是可选复用，只有真实完成后才能在明确策略下直接销毁而不缓存；不能用
  “不缓存”绕过pending容量或提前销毁。alias group保持textures先于heaps释放。
- completed normalization失败是稳定诊断；状态不确定时不得缓存。prepare/recycle/reset/
  destructor的异常和返回路径必须逐条核对，析构不能依赖新分配、阻塞idle或可抛reset。
- budget shrink/topology change不能驱逐未完成资源；新工作必须被拒绝直到合法资源归还。
  LRU sequence与报告计数必须有明确overflow语义；不能静默绕回或把统计overflow当执行成功。

## 五backend lowering

| backend | 托管与completion结论 |
| --- | --- |
| Null | ValidatorOnly；精确metadata、原pool lease与预算状态机；无native完成证明 |
| Vulkan | 原native payload/heap与原fence的Native托管；必须结合command-owned引用和原runtime身份，失败idle不退休 |
| Metal | 原command completion及payload owner的Native托管候选；缺证据或payload时DiagnosticFailure，Windows源码通过不算Apple验证 |
| WebGPU | 原GPUDevice/queue callback的Native托管候选；异步callback须保持owner/generation，host无GPUDevice明确拒绝 |
| WebGL | 尚无生产backend；DiagnosticFailure。未来OrderedReuse必须独立显式版本与调用方准入，不伪造fence |

unknown backend确定拒绝。此Draft没有新增capability或放开任意既有execution gate。

## 复用与实施顺序

### C2第一批：FrameGraphRetirementBudgetPreflight/1

此条定义完整准入之前必须使用的纯预算投影，不是lease issuer。实现位置为RHI
semantics，复用core::BudgetVectorPreflight的多维checked算术；WebGlMemoryBudget/1
同时迁移同一算法，保留原诊断优先级。GenerationPool复用审查结果保留，opaque
单次lease及其pool私有预留在后续C2/C3实现；本批没有token或native execution入口。

- 输入budget、request各自显式version=1，domain必须LogicalDescriptor；所有默认
  version=0均拒绝。budget的零维度表示关闭该维度，不能当缺省无限容量。
- 九个独立uint64维度为logicalTextureBytes、logicalBufferBytes、logicalAliasBytes、
  textureCount、bufferCount、aliasHeapCount、frameBundleCount、cpuMetadataBytes、
  referenceCount。alias bytes只记不可拆group backing一次，不重复记入ordinary bytes；
  group的texture/heap实际owner计入对应count。CPU metadata按真实保留容量计量，
  referenceCount是retention slot数，不猜测shared_ptr引用计数。来源仍须C3原pool认证。
- usage分为inUse/reserved/pending/cached。每个owner/存储在任一时刻只占一个partition；
  请求addition是新保留的总量，不是把已有owner从一个partition移到另一个的增量。
  投影不承诺eviction；需要trim时由原pool先完成合法trim，再取新快照重试。
- 固定优先级：budget version、request version、backend、domain、physical requirement、
  所有维度的live aggregate overflow、live exceeds capacity、addition overflow、capacity。
  core结果同时给出失败维度index，先类别再按声明维度顺序选择；graph诊断如下。
- 拒绝不修改输入、不返回部分projected counters；成功仅返回九维完整projected总量。
  全路径noexcept、无分配、无native query/wait/fence poll/资源转移。caller可伪造值，
  因而fits及projected不能被任何执行入口当作owner认证、lease或completion证据。

| 稳定诊断（前缀RG-RETIREMENT-BUDGET-） | 恢复规则 |
| --- | --- |
| VERSION / REQUEST-VERSION | 使用匹配版本重新构造输入 |
| BACKEND / DOMAIN | 修正显式身份或计量域；unknown不可推断 |
| PHYSICAL-BOUND-UNAVAILABLE | 使用允许logical域的另一显式workload；不能伪造物理上界 |
| LIVE-OVERFLOW | 修复原pool账目，禁止饱和/回绕；未知owner不得释放 |
| LIVE-EXCEEDS-CAPACITY | 等待原completion后合法退休/trim，或cold显式重配预算 |
| OVERFLOW / CAPACITY | 调整显式request或cold预算后重新准入；不得隐藏分配/驱逐pending |

| backend | 本纯CPU操作 / 实际retirement权限 |
| --- | --- |
| Null | ValidatorOnly；fits不证明native完成 |
| Vulkan | Equivalent CPU metadata；仍要求原native payload/issuer/completion |
| Metal | Equivalent CPU metadata；Apple native门槛保留，缺payload失败 |
| WebGPU | Equivalent CPU metadata；仍要求原GPUDevice/queue callback，host stub不授权 |
| WebGL | Equivalent纯CPU算术定义；生产retirement backend仍DiagnosticFailure |

快速测试覆盖每维/每partition的exact、one-under/over、零关闭、live总和/请求溢出、
跨维诊断优先级、未知版本/backend/domain及物理上界拒绝，检查输入不变、失败输出清零
和quota下无分配。保留WebGL八维既有行为回归及Null hidden-frame；不新增可视输出，
不将原Vulkan capture或Null通过当本批native lease实现证明。完整契约继续Draft。

#### C2下一批：opaque reservation及原issuer绑定（待实现）

纯request/result保留为可复制metadata，不增加authenticated/executable/completed布尔值。
只有原pool-owned冷构建state可发行move-only reservation；原GenerationPool的owner/index/
generation ticket保持private，公开调用者不能从PoolHandle、指针、fence、submit result或
纯fits结果构造reservation。pool wrapper、lease及原command保留同一quota-owned state；
wrapper先销毁不能留下悬空pool pointer。明确owner graph以避免state与内部ticket循环。

复用GenerationPool的fixed slot identity，FrameBundle的预分配owner/metadata容器独立于
短期ticket payload并持续存活；erase ticket不能销毁下一轮要复用的nested capacity。
不得另造free list，不能通过reserve(UINT_MAX)满足无分配。cold storage失败不发布state、
reservation或部分预算。具体slot/capacity布局及真实memory charge在C3实现前审查。

单次状态机是Reserved → Consumed → Pending/Quarantined → Retired；Reserved在确定
尚未进入执行器时可以Cancelled。Consumed之前必须绑定原pool generation、graph
topology revision、原device/queue身份及不可变资源集合；不能仅绑定bytes/count。
submit入口在可能接受native工作前消费reservation。执行器之后确认完全未接受才允许
取消；accepted或无法证明是否接受均保留owner。false返回、异常、host取消或callback
缺失都不是无accepted工作的证据。move后的空lease、双consume/cancel、foreign/stale
ticket、topology/资源集合变化必须拒绝且不改账，错误分类不能签发可重用token。

Reserved lease析构仅可撤销尚未进入执行器的预留；Consumed lease不拥有提前销毁
pending对象的权力。Pending只有原执行链的私有completion来源可退休，Quarantined
保持保留并关闭对应新准入，直到既有loss/drain契约证明合法释放；不允许public
GpuFence::signal或可写QueueSubmitResult把它改为Retired。Null fixture的受控完成仅
测试状态机；Vulkan原runtime/queue、Metal原command callback、WebGPU原GPUDevice/queue
callback分别由backend-private adapter认证。WebGL无生产adapter仍拒绝；未来ordered
lease是独立权限，不能接入本native completion退休入口。

下一批新增的稳定诊断应覆盖reservation source/owner/generation、topology/resource
mismatch、capacity、already consumed、cancel after consume和completion source。
源码实现前固定具体ID及优先级；上述待实现规则不称Contract Complete。测试必须包括
无法从public metadata构造授权的编译期边界、双pool/跨wrapper寿命、move/stale/double/
cancel、GenerationPool重用容量及真实pending由另一device完成仍保留；纯值测试不
替代后者。C2整体仍Open，C3/C4不能绕过原issuer接入。

#### C2实施前子契约：SingleUseReservationPool/1

复用core::GenerationPool作为唯一slot/free-list/generation来源，新增core组合器只存
trivially-copyable metadata；不得把GpuResource owner、FrameBundle或completion primitive
放进此metadata池。它管理单次CPU预留身份，不是native完成证明。FrameBundle持久容量、
原pool九维账本和实际device/queue/source认证仍由C3/C4接入，当前生产入口保持原门禁。

- 冷prepare必须给出非零slot capacity和显式有限CpuMemoryQuota；null/limit=0拒绝。
  state、GenerationPool两个容器及shared control block统一走core分配并由原quota计费；
  任一bad_alloc全部回滚，不发布部分wrapper或reservation，不采用默认无限quota。
  目标平台container max_size拒绝映射为CAPACITY，同样不发布部分state。
- Wrapper不可复制、可以move；reservation只能由原wrapper发放，不可复制，只能move。
  不公开PoolHandle或slot构造器，不接受metadata、整数、fence或submit result构造token。
  wrapper、reservation共享原state；wrapper先析构后reservation仍可安全查询/取消。
  state不持有reservation，共享关系无环。owner比较以私有原state为准，不读取caller ID。
- Reserved → Consumed单次迁移；先检查原owner、ticket generation、stage和closed，
  再调用无分配noexcept metadata validator。validator拒绝则保留原ticket及stage；
  未知/陈旧/foreign/重复消费在调用validator前拒绝。validation只供adapter绑定原事实，
  true本身不被解释为GPU授权。
- Reserved可显式cancel或在reservation析构/move覆盖时取消；Consumed不得cancel。
  Consumed被析构/move覆盖时改为Quarantined并关闭新reserve/consume，记录仍在原池，
  不当作完成或可复用slot。close失效所有Reserved并隔离Consumed，不释放retained记录。
- 只有原wrapper可用仍存活的opaque token显式eraseConsumedMetadata；它仅删除CPU
  metadata，既不处理GPU对象也不证明native completion。native adapter未来必须在其
  私有认证completion/loss路径调用，不能暴露给renderer或把此方法当readback许可。
  已被遗弃且无token的隔离记录不提供按ID回收入口；最终CPU state销毁不释放GPU owner，
  因为本类型禁止存放owned payload。C3的外部owner graph必须独立满足真实退休契约。
- 所有热操作noexcept、不分配、不等待、不轮询；冷生成的容量不会随erase重建/扩张。
  必须外部单线程串行，validator不可重入本池、销毁wrapper或修改metadata来源。

诊断优先级：cold capacity → quota → allocation；reserve source → closed → 原core
generation/capacity诊断；consume/cancel/erase为source → empty token → foreign state →
stale generation → wrong stage → closed（consume）→ metadata validator（consume）。
新增ID为CORE-RESERVATION-CAPACITY/QUOTA/ALLOCATION/SOURCE/EMPTY/FOREIGN/STALE/
STAGE/CLOSED/METADATA，GenerationPool内部耗尽仍保留CORE-POOL-GENERATION-EXHAUSTED。
拒绝不消耗token、不改liveCount，不调用validator；只有METADATA拒绝调用一次validator。
budget耗尽在原完成后才可由上层重新准备；closed不自动恢复，必须按原owner流程重建。

五backend对此core CPU操作均为Equivalent metadata机制，Null仍ValidatorOnly；Vulkan/
Metal/WebGPU的native completion权来自原adapter，WebGL生产路径仍DiagnosticFailure。
本批不改变任何DeviceCapabilities或FrameGraph执行路径。测试至少覆盖不可伪造的构造
边界、双pool、wrapper销毁后存活、move/self-move/覆盖、cancel/consume/close/quarantine、
validator拒绝、slot重用与generation不复用、cold每分配边界回滚及quota1热路径；RHI
契约验证无法由public budget结果/fence/submit result构造token。Null hidden-frame必跑，
原Vulkan WSI为回归，不能证明本metadata池已接入真实retirement。完整C2继续Open。

### 当前限定批次：FrameGraphPoolBudgetConfiguration/1

本条先限定既有pool配置API的事务语义，不签发完整retirement lease。输入为compiled
logical bytes、compiled resource count及logical frame slots；计量域沿用
ResourceCacheAccounting/1，native physical observation仍Unknown。

1. slots必须非零。先用core::canMultiplyUnsigned独立验证uint64 bytes与uint32 count
   两个乘积，再生成完整投影；slots、compiled bytes/count、byte/resource budget五项
   必须在所有输入校验成功后一起提交。不能先写slots再调用可能拒绝的配置入口。
2. 保留setLogicalFrameSlots/configureForCompiledGraph公开签名和既有稳定诊断：
   slots为零优先RG-POOL-LOGICAL-FRAME-SLOTS-INVALID；任一乘积overflow为
   RG-POOL-BUDGET-OVERFLOW。拒绝时五项配置、cache/pending charge/count、owner、
   LRU/diagnostic/telemetry均不变，不轮询completion，不提交native工作。修正参数后可重试。
3. 合法更新只驱逐已经completed的cache；pending即使超过新cache预算也继续保留，
   不因budget shrink、host返回或frame计数退休。零compiled bytes/count是显式零cache
   预算，不能恢复成默认无限值。空graph改变slots仍必须提交新slots。
4. 配置成功不申请存储、不调用normalization/pipeline/native query/wait；cache逐出
   沿用已经验证的非分配owner销毁与slot重建。snapshot/report测试在quota外，避免把
   报告字符串复制算进配置热路径。异常对象构造不作为系统allocator零分配的证明。
5. 构造时未配置的UINT_MAX默认值、in-use/reserved/pending总量、global计数器、
   pre-submit lease与无分配reset/destructor仍由C2～C6修复；本条不使它们合法或已完成。

| backend | 此配置操作的确定结论 |
| --- | --- |
| Null | ValidatorOnly：共享CPU metadata投影/拒绝/完成cache销毁；不是native completion |
| Vulkan | Equivalent CPU metadata策略；仅原真实completion已认证的cache可释放，native payload/门禁不变 |
| Metal | Equivalent CPU metadata策略；原completion/payload必须成立，否则原执行入口DiagnosticFailure；本机无Apple证据 |
| WebGPU | Equivalent CPU metadata策略；原GPUDevice/queue completion必须成立，否则原执行入口DiagnosticFailure |
| WebGL | 此CPU定义不认证context或capability；生产backend/heap路径仍DiagnosticFailure |

所有调用复用pool同一private配置实现，未知backend不能由该操作获得新权限。
快速测试复用现有resource-management fixture与ControlledFence：byte/count setter
overflow各自安全red、configure overflow/zero拒绝、最大可表示乘积/one-over/retry，
以及pending texture/buffer时合法缩容/零预算。检查完整report和真实Null owner；合法
路径在1-byte quota中运行，保留原序/LRU与fence poll次数。真实原WSI/RenderDoc回归
及AI检查另行完成，不能把Null拒绝/释放测试当成native pending完成证明。

### 后续限定批次：stable linear retirement compaction

复核core现有bounded container和erase/compact入口，没有可直接复用的、允许predicate
抛异常且保留未处理owner的线性erase helper。新增core::StableVectorErase，面向core::pmr
vector复用：保持survivor顺序、容量不变、helper无分配，成功或异常路径至多N次move
construction。元素move constructor/destructor必须noexcept，否则编译期拒绝。
初次集成编译证明PMR vector成员的默认move assignment不是noexcept；不得强加虚假
noexcept或假定所有allocator相同。复用cpu_allocator现有placement construction边界，
在该模块增加已分配slot的对象lifetime替换入口，先destroy目标再用noexcept move
constructor重建，不申请/释放storage；原allocator identity随对象保留。尾部pop_back
只销毁对象，不调用可能分配的memberwise assignment。

predicate返回true表示该项可以移除，返回false表示保留；若predicate抛异常，它必须
保留当前项的ownership/charge。helper不会回滚predicate外部副作用，但必须移除之前
已成功消费的项，将当前失败项及尚未访问项按原序保留，再传播原异常。每项predicate
最多调用一次，不对失败项重试，不依赖size_t回绕或额外shadow vector。

pool三种pending队列复用此helper，继续用上一批transactional cache helper。true之前
完成所有可能抛异常的normalize/cache发布；失败不扣当前项账。已完成且不缓存的alias
group显式先清textures再清heaps，避免vector元素move assignment的成员赋值次序破坏
backing-owner关系。普通未缓存资源也在完成后确定释放；retention/fence/profile不变。
五backend均为相同中立CPU算法，原Native/ValidatorOnly/DiagnosticFailure结论不变。

同一调用链的completed alias cache逐出也不得使用跨allocator的memberwise move
assignment：它可能在已消费pending owner之后分配/抛异常，并先释放heap。
逐出复用同一slot重建工具，先销毁被逐出项的textures再销毁heaps，保留其余group
的allocator identity及原序。不同fixed-buffer allocator、不同group大小的Null安全
red必须验证缩容不申请新存储且owner/charge正确；不授权释放未完成group。

快速unit穷举removal mask和每个throw位置，验证owner恰好释放一次、原序、未访问余项、
线性move上界和无core分配；另用两个独立fixed-buffer PMR resource及禁止上游分配，
验证成功/异常压缩均保留source allocator identity与原inner data pointer，且self replacement
不改变对象。pool quota/retry、完整graph及Null/native/WSI/RenderDoc回归。
这一批只关闭退休容器的二次搬移问题，不关闭预算/lease、析构、持续workload或生产状态。

### 先行限定批次：publication exception consistency

本批只修复现有pool发布的异常一致性，不签发上述未来lease，也不宣称完整有界。
ordinary deferred发布先验证checked局部bytes/count，再成功入容器，最后提交计数。
caller已经按旧by-value API转移的cold资源在失败后可以销毁；此条只用于未提交的
Null safe red，不能据此授权销毁accepted GPU工作。既有command-owned native retention
仍独立有效，其完整五backend生命周期证明保持Open。

completed资源的缓存helper按引用接收pending owner，在move前完成bucket/entry预留；
任何可抛异常均保留当前pending owner和原bytes/count。遍历逐项erase已成功退休的entry，
不用可能中途抛异常的remove_if留下已消费的空entry。alias group同样先预留cache entry
再整体move，仍保证texture先于heap销毁。计费或sequence无法表示时在move前稳定拒绝。
失败normalize的资源只在真实完成后销毁，并发布既有诊断；异常不能提前扣账。

本批测试包括quota失败后的report/真实Null ownership一致性、解除quota后的retry以及
后续普通资源复用。真实Vulkan/Null和原WSI/RenderDoc按同一源码快照回归。新诊断为
RG-POOL-ACCOUNTING-OVERFLOW与RG-POOL-SEQUENCE-OVERFLOW；恢复只允许释放已完成资源
或重建cold pool，不回绕sequence、不改变logical/native计量域。五backend metadata
和pool算法一致；native创建、completion及caps仍走原门禁，不由该局部修复新增授权。

完整pre-submit预算/lease、无分配reset/destructor、global counters/workload和原completion
认证仍为Draft后续工作，不能从这组quota测试关闭完整FrameGraphRetirementAdmission/1。

1. 先审查MemoryBudgetLedger::canAdd、core unsigned range、BoundedSequence、BoundedRetainer、
   CpuMemoryQuotaLease及已有command/submit保留链。通用算术/容器沉淀core，不在pool再造。
2. 先以Null真实allocator quota复现pending publication和completed-cache publication的
   count/bytes/ownership不一致；不提交GPU工作，不以提前销毁native对象制造red。
3. 定义pre-submit request/result/lease与原pool generation，pure tests覆盖exact/one-over/
   overflow/缺预算/重复消费/取消/不同owner/无分配准入；再实现private预留与事务发布。
4. 逐条接入execute、submit的accepted/unproven、reset、recycle和destructor；旧公共签名
   的支持边界与ABI变更必须明确，不能加一个boolean默认成功而旁路lease。
5. 用版本化YAML workload验证多frame slots、不同尺寸/预算收缩、持续缓存与pending
   故障；真实Vulkan timeline控制原提交pending，另一device完成不得回收本组。
   记录cold/setup和steady-state allocation/wait，alias内容由真实GPU输出单独证明。
6. 快速unit/contract、Null hidden-frame、完整graph/pool及受影响五backend回归；原WSI
   hidden/RenderDoc/AI视觉回归；Apple/WebGPU缺真实证据时保留相应platform gate。

稳定diagnostic的完整枚举及恢复/重试条件在实现request/result前补齐，Draft阶段不得
声称Contract Complete。本阶段不关闭BC enabled-feature、早前native异常或P0其它任务。

## 限定publication验证检查点（2026-10-17）

publication异常一致性限定修复：FrameGraphRetirementAdmission/1先写Draft及五backend
结论；Null quota安全复现3项bytes/count/owner矛盾，测试不提交GPU工作。修复为pending
成功入容器后记账，completed cache在move前预留；逐项退休避免异常留下已消费空entry。
ordinary texture/buffer和alias group共6条quota失败/retry/归零测试通过。统一MinGW十八
target与53/53 CTest无skip，2008命名输入一致；原WSI hidden/RenderDoc/AI蓝色clear回归
通过、2009输入一致，三类依赖各4、零validation message。3份C++锁定21.1.8格式/token
通过。完整pre-submit容量/lease、global counters、无分配reset/destructor、持续负载、
原completion认证、alias内容与已有平台/稳定性gate仍Open；P0仍12/19，P1～P4未完成。

限定异常一致性子契约及Null测试通过，真实Vulkan现有路径与WSI为回归证据，不能
冒称native publication fault或pending原fence认证已验证。完整本契约仍Draft。
safe red原始数据：texture/buffer pendingBytes128但count0/实际owner0；recycle
pendingBytes0但count1/实际owner0。新的recycle quota失败保留128B/1对象，解除quota后
成功缓存并trim归零。failure/retry期间没有GPU submit，没有依赖Dummy native成功。
当前逐项vector erase保持既有顺序，最坏批量搬移成本仍待后续线性收敛/有界workload证明。


## 2026-10-17 线性退休限定验证

线性退休限定增量：core::StableVectorErase与cpu_allocator slot内重建支持不同PMR
allocator，保持原序、每项至多一次predicate、异常保留失败项/未访问后缀，helper至多N次
move construction且无分配。4,097组mask/throw、两条不同fixed-buffer allocator路径、
mixed publication失败/retry及alias cache逐出通过。逐出safe red真实抛std::bad_alloc，
随后复用同一core工具修复，避免owner消费后发生memberwise move assignment分配。
MinGW十九target与56/56 CTest无skip，2011命名输入一致；原WSI hidden/RenderDoc回放
通过、2012输入一致、三类依赖各4、零validation message。AI打开present/A/C/B/C-after-B
五图检查：320x180蓝色clear，A蓝/C棕/B绿，B clear后C仍为原棕色。新capture四帧
实际A/B绑定同device/memory/offset，C为另一memory；末帧离屏1x1仍通过。5份C++
锁定21.1.8格式检查通过，无新增shader。证据为resource-cache-accounting的
implementation-stable-eviction、native-stable-eviction及red-alias-eviction。

本批仅限定算法和AliasClearPreservation/1的Vulkan Backend/Windows Platform证据；
fixture实际draw为0，不证明shader采样/着色、持续负载或完整pool热路径。LRU搜索及
多次逐出的总成本另验。完整FrameGraphRetirementAdmission/1和HeapPlacementAdmission/1
仍Draft；pre-submit lease、全维度预算、reset/destructor无分配、原completion pending、
BC、Apple、完整WebGPU及此前native异常保持Open。P0仍12/19，P1～P4未完成。

完整本契约仍Draft。C1配置事务到C6持续workload的顺序详见主计划18.10。


## 2026-10-17 C1 配置事务限定验收

FrameGraphPoolBudgetConfiguration/1已按先契约后实现收口。两个公开配置入口共用private
configureBudget；先验证slots及uint64 bytes/uint32 count乘积，再一次提交五项配置。
byte/count setter的safe red均复现拒绝后slots从1变2，原始58/60结果保留；修复后
新增四条configuration模式与原回归共60/60通过、无skip，十九target通过，2011命名
输入一致。覆盖最大乘积/one-over/zero/retry、空graph slots、已有pending texture/buffer
时合法缩容及零cache预算；完整report/owner/MemoryStats和fence poll次数保持契约，
合法配置在quota1内无core分配。公开签名与已有diagnostic保持。

native-configuration原Vulkan WSI hidden、capture/replay均通过，2012输入一致；三类
barrier各4、零validation message。AI通过tools.view_image重新打开present/A/C/B/C-after-B
五图：呈现320x180蓝色，A蓝/C棕/B绿，B clear后C仍棕色。四帧alias观察通过，actualDraw=0，
仅证明clear/Load/Store/alias及原WSI回归。三份C++通过clang-format21.1.8 dry-run。
证据：build/webgl1-evidence/p0/resource-cache-accounting/下red-configuration、
implementation-configuration、native-configuration、configuration-review-checkpoint.yaml。

只关闭C1配置事务，完整FrameGraphRetirementAdmission/1仍Draft；默认无限预算、
in-use/reserved/pending总量、单次lease、reset/destructor无分配及持续workload仍由C2～C6
实现。P0保持12/19，生产WebGL、BC、Apple、完整WebGPU和既有native异常门槛不变。
下一批按18.10/C2定义逐维准入request/result与稳定诊断，复用core预算算术及GenerationPool；
纯决策不认证caller提供的fence/result，不签发completion或native执行权限。


## 2026-10-17 C2九维预算预检限定验收

FrameGraphRetirementBudgetPreflight/1先补L2定义、诊断优先级、恢复规则及五backend
metadata结论，再实现纯request/result投影。复用WebGlMemoryBudget/1已有字段表算法，
提取core::BudgetVectorPreflight；WebGL八维原诊断顺序保持，FrameGraph九维统一计算
inUse/reserved/pending/cached的总量。全部维度先检查live求和overflow，再检查live容量、
addition overflow和最终容量；失败输出不含部分projected值，不修改输入，不签发lease、
owner认证、completion或native执行权限。零维度预算关闭；physical bound明确拒绝。

core unit独立小整数oracle穷举46,656组四partition组合，另测UINT64边界和跨维优先级；
RHI contract覆盖五backend的9维x4partition共180组边界序列、全部维度总投影、版本/
backend/domain/physical拒绝及quota1无core分配。现有WebGL profile regression通过。
最终retirement-budget-3由MinGW构建/测试22target、联合63/63无skip，2015命名输入
一致。首次C++20测试写法编译失败、Windows长路径setup失败、文档变更导致输入不稳定
的retirement-budget-2均保留；最终C++17实现及五份C++的21.1.8格式检查通过。

native-budget-3原Vulkan WSI hidden/capture/replay通过，2016输入一致；三类barrier各4，
零validation message，AI通过tools.view_image打开本轮present/A/C/B/C-after-B五图。
呈现蓝色、A蓝/C棕/B绿，B clear后C保持棕色，四帧alias观察通过。actualDraw=0，
这只是原clear/Load/Store/alias和WSI回归，不证明本预算预检已接入真实lease/执行器。
证据目录为build/webgl1-evidence/p0/resource-cache-accounting/，最终复核记录
budget-review-checkpoint.yaml。仅文档在验证后更新；最终复核校验代码、工具与binary身份。

完整FrameGraphRetirementAdmission/1与C2仍Open/Draft。下一批继续opaque move-only
reservation、原pool owner/generation、单次消费/取消与issuer绑定；复用GenerationPool，
持久FrameBundle容量不能随短期ticket erase销毁。C3再接入原pool预算/真实存储预留，
C4前不得从pure fits打开旧执行入口。P0仍12/19，P1～P4和BC/Apple/完整WebGPU/既有
native异常门槛保持未完成。


## SingleUseReservationPool/1限定检查点

MinGW reservation-stable联合69/69无skip；2593 CPU/2594 native命名输入稳定，
原WSI smoke/capture/replay/AI五图回归通过，actualDraw=0。三份C++格式检查通过。
core原语仅CPU metadata范围验收；原pool、持久FrameBundle、原queue/payload及completion
认证未接入，完整C2和本契约仍Open/Draft。具体证据及失败轮次见主计划18.13。

## C2实施前：FrameGraphPoolMetadataSource/1

原FrameGraphResourcePool冷构建私有metadata state；复用SingleUseReservationPool，
token同时保留原state及私有slot，不公开ticket、raw pool/device/queue指针或构造key。
此步仅预留CPU metadata slot，不接受caller topology/resource/fence/result，不提供consume、
pending、retire或GPU owner入口，不能作为FrameGraphRetirementAdmission/1的完整lease。
后续必须另接原graph/topology/resource及backend-private实际issuer，不能扩大本token权限。

- 显式budget contractVersion=1、prototypeOptIn=true、非零maxReservations及
  maxCpuMetadataBytes；quota非空且limit必须等于声明CPU budget。所有state/control block及
  fixed slots由同一core quota计费；与device既有CPU owner重叠的quota拒绝。
- prepare先验证version、opt-in、slot/CPU budget、quota，再拒绝原state仍有live token；
  再验证原device logical lifetime及quota domain。缺失/retired/expired原ticket均失败关闭。
  未准备device不自动创建lifetime，不从backend身份或caller ticket取得授权。
- prepare在本地完成全部分配后才替换原state；bad_alloc/length_error保持原state、token、
  原cache/pending/配置/report不变。零live允许显式重建；旧state地址即使复用也不能授权旧token。
- reserve仅由原pool发放Reserved slot；validate/cancel先检查原pool source、空token及
  foreign state。validate再查closed、原device ticket及slot。cancel只撤销CPU预留，允许
  device已retired时清理；不可把cancel返回值当作无accepted GPU工作的证据。
- pool析构或显式close关闭并失效所有Reserved slot，无分配/等待/查询。token可跨pool或
  device wrapper析构安全存活：仅保留CPU state，不保留或访问已销毁GpuDevice。观察旧device
  ticket先失败；reserve/validate热操作不访问device facade或native API。
- token不可复制、可move；Reserved析构自动撤销。准备、发放、取消和观察必须外部单线程
  串行；预算、core slot generation及冷分配失败诊断直接沿用既有稳定ID。

新增RG-RETIREMENT-METADATA-前缀诊断为VERSION/OPT-IN/CAPACITY/CPU-BUDGET/QUOTA/
BUSY/SOURCE/EMPTY/FOREIGN/CLOSED/STALE/QUOTA-DOMAIN/ALLOCATION。恢复为修正声明、
取消尚未消费的CPU slot或显式cold重建；不能自动扩容或触碰pending GPU资源。
原device lifetime及core耗尽诊断原样传递，拒绝不改变资源或cache统计。

五backend结论：Null为ValidatorOnly；Vulkan/Metal/WebGPU为Equivalent CPU source绑定，
不证明native payload；WebGL及unknown在原device logical lifetime gate处拒绝，本步无
生产WebGL adapter。必须补五backend决定、source缺失、foreign pool、reprepare/失败原子性、
device退休/销毁、pool销毁后token、quota逐边界回滚与热操作无分配测试；Null hidden-frame
及原Vulkan smoke回归。完整C2、C3～C6和所有native/platform门槛仍Open。

公开FrameGraphResourcePool增加实验API与私有state成员，既有方法签名保持；不声明二进制
布局兼容。pool及其consumer必须同revision重新编译，不能混用旧engine DLL或旧对象布局。


### FrameGraphPoolMetadataSource/1限定检查点

MinGW pool-reservation-final联合70/70无skip，2594 CPU/2595 native输入稳定；四份C++
格式及原WSI/RenderDoc/AI回归通过，actualDraw=0。仅CPU source子契约/Prototype验收，
完整C2、原graph/topology/resource、持久FrameBundle及native issuer join仍Open。
准备/编译失败与中间轮次均保留；具体证据、ABI重编边界及下一步见主计划18.14。

## C2实施前：FrameGraphCompiledRetirementSource/1

在实际FrameGraph的显式cold prototype配置下，由FrameGraph::compile使用原编译器并在
全部编译结束后签发private source。它绑定原graph epoch、原pool metadata slot、原device
logical lifetime和当时完整texture/buffer成员集合。不是native frame lease，不提供consume/
submit/completion/owner退休；持久FrameBundle、真实四partition账本及private custody join仍Open。

- 请求contractVersion=1、prototypeOptIn=true、显式maxResourceBindings和非零
  maxCpuMetadataBytes；0 resource ceiling只允许空资源集合。source quota须与device和
  pool metadata quota独立，limit与声明一致；state/epoch/source/control block及snapshot
  storage全部core计费。原编译器现有拓扑容器不归此source预算，不能宣称整个compile有界。
- 原pool必须已prepare，且其原logical device必须与graph device完全一致。不得自动准备
  lifetime或pool。cold失败保留原graph source state；成功显式重配失效旧来源。
- v1仅签发flat graph来源；存在subgraph/subpass graph时明确拒绝SOURCE-COMPOSITION，
  绝不忽略其后续authoring。该限制是显式Prototype边界，不能关闭完整graph集成。
- source只由原graph的compile wrapper签发；公开FrameGraphCompiler的纯结果、手工构造
  CompiledFrameGraph、caller hash/ID/fence/result均无来源。公开compiled拷贝共享同一不可变
  source/slot；任一副本经mutator修改时只清除该副本的source，不能污染原副本。
- 全部compiled mutator（含encoder desc、subgraph metadata及budget）在可能修改前清除
  source。失败修改也允许保守失效；禁止原地修改后沿用旧source。
- compiled copy assignment必须事务性替换完整值：任何拓扑复制分配失败，原目标的拓扑和
  source均保持不变，不能提前换source或留下部分新拓扑与旧source。move assignment须
  无分配且noexcept，完整转移拓扑及source；self-copy/self-move保持原值。复用core现有
  replaceCpuObjectByMove提交已成功构造的替代值，不新增局部swap/allocator工具。
- graph资源创建/导入、pass authoring入口、encoder/timing/cull配置和reset在可能修改前
  advance原epoch。nonconst pass accessor统一覆盖PassBuilder全部修改，所有资源handle分配
  统一覆盖新成员。idempotent import可保留generation；readonly/final-state等修改单独失效。
  epoch exhaustion失败关闭，不能回绕。v1 composition的新增也失效旧epoch。
- snapshot只存原成员kind/handle、L2对象地址和id，绝不dereference旧地址或拥有GPU对象。
  deferred transient texture可记录明确null声明；真实materialization改变成员绑定后验证
  必须拒绝。此来源不认证尚未创建的GPU payload，后续physical join须由原graph重新绑定。
- validate先检查private source、原graph state identity/epoch、原pool/device，再比较完整
  有序成员集合；不同graph（即使同pool/device）、reset/重配、old pool state、成员替换均
  拒绝。成员地址不作为单独授权；graph epoch与pool slot共同防止ABA。
- CompiledFrameGraph自身的source diagnostic仅观察弱graph epoch及原pool/device token；
  FrameGraph::validateCompiledRetirementSource另检查当前原graph与实际成员集合。两者都不是
  当前authoring以外的资源状态、native payload或执行能力证明。
- source弱观察graph，不持有graph/pool wrapper；source持有CPU slot及原source quota。
  graph/pool/device销毁后安全拒绝，无悬空访问或循环。来源shared owner复制/观察/失效/取消
  不分配或等待；完整compiled value复制仍有原有拓扑容器的cold复制成本。
  source发放为cold操作，失败自动撤销临时slot/CPU存储，保留已有来源。

新增RG-COMPILED-SOURCE-前缀ID：VERSION/OPT-IN/CPU-BUDGET/QUOTA/QUOTA-DOMAIN/
COMPOSITION/MISSING/EXPIRED/GRAPH/EPOCH/RESOURCE-CAPACITY/RESOURCE-MISMATCH/ALLOCATION。
优先级为请求version/opt-in/CPU/quota、composition、原pool/device、quota domain、cold
allocation；发放先composition/epoch/resource capacity、原pool slot、source allocation。
验证先missing、原graph identity/epoch、pool/device及完整resource membership；原pool/core
诊断沿用。修正配置或显式cold重编恢复；不得静默重签改过的compiled对象。

五backend：Null ValidatorOnly；Vulkan/Metal/WebGPU Equivalent CPU provenance，仍无
native权限；WebGL与unknown由原logical device gate拒绝。测试覆盖同pool不同graph、
不同device pool、全部compiled mutator、graph authoring/reset、source copy/move/lifetime、
slots/quota失败原子性与逐边界回滚、无分配validation和真实Null smoke。Vulkan capture
仅回归；不凭来源验证关闭完整C2/C3或任何native/backend/platform gate。新增私有成员
改变FrameGraph/CompiledFrameGraph布局，全部consumer必须同revision重编。

### FrameGraphCompiledRetirementSource/1限定检查点

本子契约/Prototype已完成限定验收，操作遵守原graph/epoch外部串行约束。默认copy
assignment在拓扑复制配额失败后错误替换source的安全red已复现并修复：先完整构造替代值，
再复用core原位move提交。完整copy仍是cold/fallible；move/self操作无分配，不能据此
宣称原编译器全部有界。实际Null materialization后旧成员快照拒绝，未签发physical join。

MinGW25 target、74/74无skip，CPU2595/native2596输入稳定，八份C++格式通过；原WSI/
RenderDoc回放与AI五图检查通过，actualDraw=0。完整FrameGraphRetirementAdmission/1、
C2、c/3仍Draft/Open；P0保持12/19。证据与下一阶段持久FrameBundle/账本/private issuer
要求见主计划18.15及compiled-source-review-checkpoint.yaml，不能用本source开放执行。

## C2/C3实施前：FrameGraphPersistentFrameStorage/1

此子契约在原pool metadata state中持久保留FrameBundle的真实CPU owner槽位容量，
不接受GPU owner、不发放submit/consume/completion权限。完整四partition账本、physical
join和private native custody仍须后续实现；不得把此存储预留等同完整retirement admission。

- cold请求version=1、显式Prototype opt-in、非零maxFrames/CPU预算，以及每frame的texture/
  buffer/alias heap/shared reference容量。各owner类别可为0，表示关闭；预算不取理论
  driver上限。复用core::BoundedSequence及BoundedRetainer，不新增allocator或通用容器。
- 必须有未关闭的原pool/device metadata source，且没有live metadata reservation才允许
  重配。storage quota与原device及metadata quota独立；声明limit必须匹配。失败保留原
  storage及原cache/pending/report，先在独立replacement中完成所有cold分配再发布。
- 原pool state拥有固定FrameBundle集合；各bundle预留heaps、textures、buffers和references。
  heaps声明在依赖textures之前，未来清理必须先texture后heap。短期ticket只绑定一个
  原bundle；metadata擦除/取消保留bundle及其全部capacity，禁止把nested容量随entry析构。
- bundle reserved位只描述CPU槽位，不是native接受/完成状态。与core单次slot在同一
  外部串行操作中发布；core reserve失败不得占用bundle。自动取消、move overwrite、
  close须同步撤销未消费CPU预留；没有公开消费入口，不能向bundle放入GPU owner。
- configured storage成为原compile source的附加约束：原graph计算owned texture/buffer、
  imported shared reference及不同alias group的精确数量，再由原token对比固定容量。
  unique import仍计owned texture，不能仅凭imported标记把独占owner当shared reference。
  使用core::pmr set计算distinct alias groups，cold scratch计入source quota。缺容量
  拒绝并自动撤销临时slot；不截断成员，不用caller计数扩大权限。未配置storage保持既有
  CPU source-only路径，但不声明具有FrameBundle存储。
- source quota若与已准备storage重叠，prepare及publication均拒绝；publication再次检查
  防止graph先配置、pool后配置的顺序旁路。独立CPU配额和声明owner槽位不是GPU内存账本。
- 只读report提供声明容量、已预留frame数量和本storage实际cold CPU charge；不返回原生
  地址、物理显存估算或完成证据。reserve/cancel/report/close无分配、无查询/等待。
  pool wrapper关闭而token存活时，CPU state/storage保持到最后holder释放，无悬空访问。

RG-FRAME-STORAGE-诊断：VERSION/OPT-IN/CAPACITY/CPU-BUDGET/QUOTA/QUOTA-DOMAIN/BUSY/
ALLOCATION/SOURCE/FRAME-CAPACITY/TEXTURE-CAPACITY/BUFFER-CAPACITY/HEAP-CAPACITY/
REFERENCE-CAPACITY。优先级为intent、quota、原source/device、BUSY、quota domain、cold
allocation；编译发放为原source/device、空frame、core slot、source quota/成员容量。
修正显式预算、释放尚未消费的CPU来源或cold重配恢复；不驱逐pending GPU对象。

五backend：Null ValidatorOnly；Vulkan/Metal/WebGPU Equivalent CPU存储，不证明native
执行；WebGL/unknown由原device gate拒绝。快速测试覆盖全部维度、零域、frame/slot耗尽、
copy/move/自动取消、close/wrapper寿命、重配事务及逐byte回滚，辅以Null hidden-frame及
原Vulkan WSI/capture回归。实际owner转移/完成证据不能由这些CPU测试替代。

### Native fixture诊断边界（存储批次回归调查）

native bootstrap失败或缺少所需能力仍返回既有77，不得解释为验收通过。fixture必须输出
实际失败的Vulkan调用及VkResult，或已查询但不满足的能力；timeline用例在原native
创建及销毁边界输出阶段，便于定位无stdout的进程异常。诊断不能增加重试/延迟、改变
native ownership/提交/完成语义、启用替代device或移除测试。五backend lowering不变，
这里只改善Vulkan测试证据；先前SEGFAULT及skip必须保留且稳定性gate继续Open。

### FrameGraphPersistentFrameStorage/1限定检查点

本CPU存储子契约/Prototype限定验收通过：MinGW25 target、77/77无skip，CPU2595/native2596
输入稳定，七份C++格式通过。原WSI/RenderDoc及AI五图回归通过，actualDraw=0。原失败轮
SEGFAULT/skip、Windows事件及未复现GDB均保留；native根因未确定、稳定性gate仍Open。
详见主计划18.16及persistent-storage-review-checkpoint。未接收GPU owner，未提供consume/
submit/completion授权；完整C2/C3和FrameGraphRetirementAdmission/1仍Open/Draft。

## C3-a前置：实际allocation owner的创建事务

本批先验证原有allocator/factory的异常安全和实际owner寿命，不新增retention facade、
device vtable、capability或执行权限。原registration弱ticket与实际allocation charge是
两个独立寿命：wrapper消亡立即使ticket失效；native payload被命令、view或其它既有
backend owner持有时，原allocation charge必须继续保留，直到实际allocation owner销毁。

- 冷创建的CPU release token或payload分配失败，必须完整撤销本次live bytes/count及
  native object；已有allocation的charge不可减少。预算耗尽和host allocation失败不是
  GPU完成，不触发cache复用或任何pending资源释放。允许原有bad_alloc传播，不返回假成功。
- release token创建、native object创建与CPU owner发布之间必须有明确的异常边界。
  只释放本次已经成功创建、尚未转交RAII owner的native对象；driver失败输出不做清理。
  创建失败后可由调用方在冷路径修正quota并重试；禁止自动重试或替代allocation。
- 实际native allocation大小仍由原allocator/VMA给出；metadata-only allocator测试不
  证明physical VRAM边界。wrapper注册记录不延长为强owner，ticket过期不减native账。
  此前置不提供pool receipt、四partition守恒、heap provenance或private submit/completion join。

| backend | 本批结论与执行边界 |
| --- | --- |
| Null | Equivalent CPU事务，ValidatorOnly；原factory配额失败不得泄漏live账目，无native证明 |
| Vulkan | 原allocator charge及真实buffer/image owner寿命；CPU token/payload失败清理本次对象；native证据单独记录 |
| Metal | Equivalent CPU allocator事务；token失败不发布charge；Apple native寿命本批未验证，保持平台gate |
| WebGPU | Equivalent CPU allocator事务；原先token先于record顺序保持；真实GPUDevice/browser寿命仍需独立证据 |
| WebGL | DiagnosticFailure：生产factory尚不可用；不由其它backend的CPU测试提升能力 |

先补可复用quota fixture测试，覆盖已有live allocation、短/长label、每byte host quota、
精确成功及失败回滚、重复shared owner最后释放；辅以Null hidden-frame及原生Vulkan
dispatch销毁计数。原device factory测试须绑定原registration，覆盖同native runtime的
foreign logical device、wrapper/ticket消亡、payload继续存活、原device退休和最后owner释放。
不改变同步或画面，不以本批宣称新增draw/视觉通过；原native稳定性gate继续Open。

### Placed texture publication失败的前置边界

同一创建事务还适用于placed texture。原heap/offset/requirements/preflight已成功并不允许
提前把尚不存在的wrapper发布到resource registry。wrapper或其state/registration创建
失败，必须撤销本次weak registry记录及control block，保持原heap与已有资源的所有权和
charge；本次成功创建的image仅销毁一次。后续无关创建不能承担清理失败记录的责任。

不增加heap provenance权限或放宽placement gate。Null保留原ledger/range validator和
MemoryLedgerLease；Vulkan用实际heap/image及独立CPU quota验证原factory；Metal现有
wrapper先于registry publication的顺序保持，Apple native证据仍Open；WebGPU沿用
RHI-WEBGPU-HEAP-PLACEMENT-UNAVAILABLE拒绝；WebGL生产入口仍DiagnosticFailure。
缺native能力/错误heap/offset不得进入driver bind，原有负向路径继续保留。

先以CpuQuotaFactoryFixture验证真实placed factory的精确/不足quota、失败后原CPU quota
归零及全部image create/destroy平衡，再实现必要的backend-private修复；重试必须由测试
或caller在冷路径显式发起。Null hidden-frame、原placement负向及graph/storage回归必检。
# C3-a ordinary pool allocation receipt /1 (implementation batch)

This explicit Prototype is an acquire-time logical charge, not submission,
completion, reuse admission, a physical memory bound, or the full nine-dimensional
retirement ledger. Existing APIs remain opt-out unless explicitly prepared.

The original pool creates each sealed receipt only for an ordinary resource
returned by its own acquire. The original device must validate the exact original
factory registration and current logical lifetime. A receipt binds that exact
registration, pool accounting state and resource kind. No caller ID, public fence,
submit result, expired weak ticket or same-device foreign pool authenticates it.

The receipt is retained by a single fixed slot in the original allocation charge
owner. That owner must survive every independently retained native allocation
role. Wrapper destruction invalidates its registration but does not release this
charge while a native allocation role survives. The receipt retains CPU accounting
state only; it must never retain a wrapper or native payload. Duplicate attachment
rejects. No receipt detachment/rebinding is exposed.

Cold preparation requires an empty original pool before any legacy acquire, a separate finite CPU quota,
explicit version/opt-in and finite texture/buffer count and logical-byte budgets.
Outstanding receipt owners prevent replacement even after wrappers expire.
Acquire preflights logical bytes/count before the native factory; allocation,
registration or receipt failure publishes neither a returned owner nor a miss.
Actual receipt/control-block allocation is charged to the provided CPU quota.
Reuse, cache returns, deferred returns, alias groups and graph budget integration
are deliberately unavailable in this prepared acquire-only Prototype until C3-e/f
establish their provenance and private native completion join. They fail with
RG-POOL-ALLOCATION-RETURN-UNAVAILABLE before legacy fence/caching operations.
By-value legacy release calls still consume their argument on rejection; the
original allocation charge remains alive through independent native owners.

Five-backend lowering for this sub-contract:

| Backend | Result | Required execution payload / boundary |
| --- | --- | --- |
| Null | Equivalent, ValidatorOnly | Original MemoryLedgerLease fixed slot; never native proof |
| Vulkan | Equivalent retention only | Genuine ordinary allocation token plus real native resource; deepest VMA owner retains token |
| Metal | DiagnosticFailure | RG-POOL-ALLOCATION-METAL-OWNER-UNAVAILABLE; no inferred Apple proof |
| WebGPU | DiagnosticFailure | RG-POOL-ALLOCATION-WEBGPU-OWNER-UNAVAILABLE; no inferred browser proof |
| WebGL | DiagnosticFailure | RG-POOL-ALLOCATION-WEBGL-OWNER-UNAVAILABLE; no production backend yet |

Physical observation remains Unknown; no capability is enabled by preparation.
Pool destruction removes its validation endpoint. Original device retirement
invalidates device observations; outstanding charges remain until the last
allocation owner is destroyed.
All pool operations are externally serialized. Counter release is atomic and
allocation-free; a consistent multi-counter report requires serialized owner
release. This does not close C3, P0 or the separate native stability gate.

Acceptance for the acquire-only /1 Prototype is indexed by plan section 18.21
and pool-allocation-receipt-review-checkpoint.yaml: 38/38 combined CTest without
skip, six receipt modes, 417 source contracts, and a Null hidden-frame smoke.
The lowering report describes the versioned mapping; each successful acquire
must authenticate its own original allocation slot. It is not a device capability
probe. The device receipt accessor is a non-owning borrow valid only while the
referenced resource is alive; callers must not retain the raw pointer.
Full C3 and FrameGraphRetirementAdmission/1 remain Draft/Open. No native submit,
completion, visual, browser, Apple or production claim is added by this batch.

### OriginalHeapRegistration/1 (C3-b factory slice; Prototype Complete)

This independent Prototype request extends the existing original registry;
it never creates a second issuer. `MemoryHeap` is appended after `Sampler`
without renumbering existing kinds. An absent request preserves legacy heap
creation and same-native-runtime placement. A requested domain requires /1,
explicit opt-in, a positive heap count, the original registry CPU/owner budgets,
and the original logical device lifetime. It authorizes factory provenance only,
not native completion, alias reuse, pool accounting or execution capabilities.

Reserve the existing kind/owner capacities, generation and CPU record before
calling the native factory. The unpublished record has no object/id; publish
only after an authentic backend factory returns a valid, unregistered candidate
and registry/lifetime validation still succeeds. CPU/quota/factory failures
publish nothing and release the reservation. A private heap publication fills
the original id once; callers cannot adopt or rebind an existing heap.

| Backend | Factory identity outcome | Placement in requested domain |
| --- | --- | --- |
| Null | Equivalent, ValidatorOnly | Original logical device only; no native claim |
| Vulkan | Equivalent CPU identity on actual heap factory | Original logical device only; native placement checks remain mandatory |
| Metal | DiagnosticFailure, `RHI-ORIGINAL-HEAP-METAL-ISSUER-UNAVAILABLE` | Reject registered foreign heap before native work; Apple implementation remains open |
| WebGPU | DiagnosticFailure, `RHI-ORIGINAL-HEAP-WEBGPU-ISSUER-UNAVAILABLE` | Existing placement rejection remains in force |
| WebGL | DiagnosticFailure, `RHI-ORIGINAL-HEAP-WEBGL-ISSUER-UNAVAILABLE` | No production heap implementation |

Either a requested consuming domain or an already registered heap requires an
exact live registration from that logical device. Thus an opt-out facade cannot
consume a registered foreign heap, even on the same native Vulkan runtime.
The old unregistered same-runtime path remains unchanged. Native-loss joined
registries/observers are explicitly rejected for this request until their heap
join contract exists. Weak registration expiration rejects future placement;
it never proves GPU completion or releases physical memory.

This slice deliberately does not attach pool receipts to cached physical heap
allocations. Logical lease custody, physical cache accounting, pool provenance,
the nine dimensions/four partitions and private completion joins remain open.
Required evidence: five-backend decisions, negative intent/identity/lifetime,
capacity and every CPU publication boundary, Null hidden frame, and real Vulkan
placement including legacy cross-facade regression and retained placed payload.

Acceptance for this factory identity slice is indexed by plan section 18.22 and
heap-registration-review-checkpoint.yaml: 44/44 combined CTest without skip,
four registration modes, eleven placement cases, 417 source contracts, and a
Null hidden frame. Contract Complete / Prototype Complete applies only to this
slice. Null remains ValidatorOnly; Vulkan evidence covers real allocation and
placement provenance/owner lifetime, not command issue/completion. Metal/WebGPU
public request rejection is tested; Apple/browser platform execution remains open.

The heap count bounds simultaneously registered wrappers, not physical cache
allocations or retained placed payloads. A failed logical publication can leave
an already-created allocation in the existing backend cache, still charged by
that allocator. Tests verify record/capacity/CPU rollback separately from this
physical custody, plus retry without another native allocation. Generations are
consumed monotonically even on failure. No lease accounting, cache reuse right,
native completion, or production readiness is inferred from weak ticket expiry.

### FrameGraphPoolHeapLease/1 (C3-b owner integration; limited acceptance)

An independent `heapLeases` request on the existing pool allocation budget
requires /1, explicit Prototype opt-in, positive maximum live logical leases and
positive declared heap reservation bytes. It uses that pool's independent CPU
metadata quota and requires OriginalHeapRegistration/1 on the original device.
Neither the old ordinary allocation request nor a device/backend name opts in.
The new pool `acquireMemoryHeap` entry rejects an absent request before factory
work; it authenticates the original returned heap before attaching a receipt.

The charge unit is one logical placement lease and its caller-declared heap
size. It is not resident/native physical bytes. Vulkan creates a distinct lease
for every heap wrapper acquisition, even when reusing an existing physical cache
allocation. The wrapper and all placed image payloads retain that lease; the
lease owns its physical allocation, while the backend cache owns only physical
allocations. The fixed non-rebindable pool receipt belongs to the logical lease,
never to the cached physical allocation. Image destruction precedes lease
release. The last lease owner releases the pool charge, even after wrapper,
pool or device destruction; an expired factory ticket cannot release it early.

Null uses the original shared validator ledger lease and binds its receipt slot
while that lease is mutable at construction. Placed validator resources retain
the same const lease. No const_cast, second ledger, Dummy heap or native claim
is permitted. Vulkan retains the original cache/allocator physical accounting;
failed logical publication can leave physical cache bytes allocated and charged.
Receipt/CPU/quota failure rolls back logical charges and registration ownership,
and retry receives a new generation/lease, including physical cache reuse.

| Backend | Requested heap lease accounting |
| --- | --- |
| Null | Equivalent, ValidatorOnly, original shared ledger lease |
| Vulkan | Equivalent CPU accounting, distinct backend logical lease on real heap allocation |
| Metal | DiagnosticFailure, `RG-POOL-HEAP-METAL-OWNER-UNAVAILABLE` |
| WebGPU | DiagnosticFailure, `RG-POOL-HEAP-WEBGPU-OWNER-UNAVAILABLE` |
| WebGL | DiagnosticFailure, `RG-POOL-HEAP-WEBGL-OWNER-UNAVAILABLE` |

No execution/completion/reuse capability changes. Existing device placement
still validates original device and exact native range/type/requirements. It
retains the heap lease but does not claim a pool receipt for a placed image's
own logical texture bytes; that accounting joins C3-c/d separately. All old
return/cache/alias/compiled-graph paths reject this new pool domain, including
charged heaps passed to opt-out pools with empty texture lists. By-value return
failure consumes its wrapper; independent placed payloads must keep the charge.

Required tests cover explicit/absent/malformed request, five backend outcomes,
missing registration, wrong pool/device/resource, duplicate receipt, independent
count/byte gates, wrapper loss with retained placed payload, fresh logical lease
on warm physical cache, per-byte metadata and factory CPU failures, cold setup
rollback and busy reconfiguration, shutdown, and allocation-free observations/
final release. Preserve Null hidden-frame, original native placement/owner
regressions and actual Vulkan allocation/image destruction instrumentation.
Full nine-dimensional/four-partition accounting, graph imports/materialization,
return storage, private submit/completion joins and production readiness remain
open; this slice cannot certify native completion from reference counts.

Acceptance is indexed by plan section 18.23 and
pool-heap-lease-review-checkpoint.yaml. Thirteen MinGW targets and 51/51 combined
CTest cases passed without skip, including five pool lease modes, thirteen
placement cases, the original completion/owner regression and Null hidden frame.
All 2606 named validation inputs matched. Actual RTX 3050 allocation/image
observations closed with no unsafe binds; cold metadata/factory quota failures
rolled back logical charges while physical cache accounting remained charged.
Contract Complete / Prototype Complete applies only to this original owner and
acquire accounting slice. Null is ValidatorOnly, Vulkan has limited real heap/
placement lifetime evidence, and no submit/completion/reuse or new visual claim
is added. The full admission contract and earlier native stability gate stay
open. Public layout changes require same-revision consumers to be rebuilt.

### FrameGraphPoolPartitionOwners/1 (C3-c original acquire slice; Contract Complete / Prototype Complete)

An independent `partitions` /1 Prototype request on the original allocation
budget declares a positive maximum resource-owner count and the existing nine
counter capacities. All zero capacities close their dimensions. CPU metadata
capacity must be positive and no larger than the original explicit pool quota.
The old allocation and heap requests do not enable this domain implicitly.
This request neither adopts external resources nor opens old return/graph APIs.

The original acquire path reserves one complete source-owned charge before
calling a device factory. Ordinary texture/buffer bytes and counts come from
ResourceCacheAccounting/1 and the exact requested descriptor. An individual
heap lease contributes one heap owner; its existing declared reservation-byte
budget stays separate. It cannot populate logicalAliasBytes without the later
exact indivisible-group join. Frame bundles and retained-reference slots are
also charged only when their original ownership joins exist. The generic ledger
supports all nine fields and four partitions; those joins remain C3-d/e work.

Preparation retains fixed owner slots using the original GenerationPool and
core allocator. Slot and generation capacity come from the explicit request,
never native theoretical limits. A dedicated Reserved storage charge contains
the exact original allocation-state and ledger storage bytes, queried during
cold creation from the core allocator with the original quota ownership. Every
receipt adds its actual shared-allocation footprint before publication. External
pressure on a caller quota is not reported as pool-owned metadata. Missing or
foreign allocation observations reject publication rather than guess sizes.

Acquire/charge amendment/partition transfer/report run on the cold-preparation
owner thread. A foreign thread receives a stable diagnostic before factory or
pool mutations. A final receipt allocation may be freed on another thread: it
only atomically marks its exact preallocated generation as released, without
locks, allocation, native calls or mutation of the GenerationPool. Notification
occurs after the receipt's shared allocation is deallocated, not merely at its
object destructor. A stale/duplicate notification cannot release a reused slot.
The next serialized observation/admission reclaims released entries as whole
nine-field records. Concurrent notifications may leave a conservative retained
snapshot for that observation; they cannot grant GPU completion or release a
partially updated dimensional record. Owner-thread snapshots remain bounded by
the explicit slot count; they do not inspect allocator internals in the hot path.

Initial acquire transitions Reserved -> InUse after exact original registration
and receipt checks, before returning the wrapper. Any factory, receipt allocation,
CPU charge or binding failure releases the whole reservation; reports and retries
reclaim it without partially published counters. Existing per-resource limits
and physical allocator charges remain enforced independently. The ledger and
its fixed storage outlive the pool wrapper through the actual receipt allocation.
They contain metadata only and never own the receipt or native command back.

| Backend | Original acquire owner partition accounting |
| --- | --- |
| Null | Equivalent, ValidatorOnly, original ledger leases |
| Vulkan | Equivalent CPU owner ledger on original retained native allocations/heap leases |
| Metal | DiagnosticFailure through the original pool-owner-unavailable gate |
| WebGPU | DiagnosticFailure through the original pool-owner-unavailable gate |
| WebGL | DiagnosticFailure through the original pool-owner-unavailable gate |

No DeviceCapabilities or execution/completion/reuse right changes. All old return,
cache, alias-group and compiled-graph paths keep their explicit refusal until
the corresponding original ownership transaction is implemented. Core ledger
diagnostics (CORE-PARTITION-*) describe source/thread/field/partition/capacity/
version-independent metadata errors; RG-POOL-PARTITION-* describe request,
CPU provenance and source-join errors. Both are deterministic failures, not
permission tokens. A public counter snapshot cannot issue a resource receipt.

Required evidence: all nine dimensions and four partitions, exact/overflow/
zero limits, failed amendment and transfer unchanged, foreign/stale/double
release, generation reuse, cold quota rollback, cross-thread final allocation
notification and wrong-thread admission refusal; the same batch must integrate
the ledger into original pool acquires and test real Null/Vulkan retained owners.
Null hidden-frame and original graph/completion/placement regressions remain
mandatory. Complete C3-c/d/e/f and Production Ready are not established by this
acquire slice; native submit/completion joins and graph return storage stay open.

Acquire-slice evidence: `partition-owners-1` and
`partition-owners-review-checkpoint.yaml` record 57/57 CTest with no skips,
417 source assertions, 2610 stable named inputs, nine formatted C++ files and
real Null/Vulkan retained-owner integration. Weak shared control blocks retain
the CPU charge until allocation deallocation. Each record permits only one
tracked shared allocation; a duplicate attempt fails deterministically. Native
Vulkan placement/owner validation does not establish GPU completion or full
C3-c/d/e/f. Existing native stability and graph custody gates remain open.

### FrameGraphPoolAcquireOnlyGraphExclusion/1 (Contract Complete / Prototype Complete)

Until C3-d/e/f joins actual graph ownership to the original allocation receipt,
frame storage and canonical native custody, acquire-only accounting and a live
FrameGraph on the same pool are mutually exclusive. This applies independently
of the optional partition request. Constructor admission must reject an already
configured pool with RG-POOL-ALLOCATION-GRAPH-UNAVAILABLE before authoring,
resource creation or reset/destructor paths can adopt it. Conversely, accounting
preparation rejects a pool retained by any live graph with
RG-POOL-ALLOCATION-GRAPH-ACTIVE before allocating or replacing the source.
Reset does not end graph membership; only destruction of the last graph ends
its registration. Multiple ordinary graphs on a legacy pool remain supported.

Use the original core AtomicCapacityBudget/Lease for graph membership: the
lease aliases and pins the exact existing shared pool, has no allocation of its
own, cannot be copied, and unwinds on constructor failure. The count's only
bound is its uint64 representation; exhaustion is a deterministic
RG-POOL-GRAPH-OWNER-CAPACITY failure. The count neither owns GPU resources nor
issues any execution/completion/reuse permission. No public graph or native
handle registry is introduced. Pool mutation retains its existing serialized
caller requirement; this does not make late configuration concurrent-safe.

Null and Vulkan reject graph/acquire-only coexistence at these CPU boundaries;
Metal/WebGPU/WebGL continue refusing the allocation source through their
original backend owner diagnostic. Existing legacy graph execution/lowering
and old unjoined return refusals remain unchanged. This is an early failure
contract, not completion of graph owner integration. Required evidence covers
both configuration orders, partition opt-in/out, several graphs and reset,
last-graph release, unchanged source/quota on refusal, Null hidden-frame and
original Vulkan/graph regressions. No new visual-output claim is made.

Boundary evidence: `graph-exclusion-1` has 60/60 CTest with no skips, 417 source
checks and 2611 stable named inputs. The original registered red passed nine
receipt modes and failed three missing-early-refusal assertions. It did not
show an empty-graph destructor crash: legacy empty alias-group return exits
before its domain gate. The new early exclusion closes both construction
orders and tests reset/multiple graph membership, while retaining full original
owner/custody integration and native stability as open gates. See
`graph-exclusion-review-checkpoint.yaml` for actual executable identities.

### FrameGraphCompiledSharedImports/1 (scoped Prototype; validation indexed separately)

An independent `sharedImports` request on the compiled-source budget opts into
retention of authentic shared texture/buffer imports by the original persistent
RetirementFrameBundle. Default source-only behavior is unchanged. Version 1
admits import-only graphs, including empty graphs; unique/desc-only/alias owners
are refused with RG-SHARED-IMPORTS-OWNER-UNAVAILABLE until their actual transfer
exists. This is a CPU Prototype ownership transaction, not native command
custody, queue admission, completion or resource reuse authority.
Both FrameGraph and its directly callable FrameGraphExecutor reject execution
with RG-SHARED-IMPORTS-COMMAND-CUSTODY-UNAVAILABLE before recording. Either the
graph's active request or the compiled source's immutable request suffices;
reconfiguration cannot silently turn a retained source into command custody.

Before retaining any owner, validate the entire original graph inventory against
the exact original logical device/registration and the resource's canonical core
shared control block. Missing, forged, non-owning, foreign-device and stale
registrations fail deterministically; identical pointers on unrelated control
blocks do not authenticate ownership. GpuTexture joins the existing
SharedObjectIdentity contract used by GpuBuffer; core makeShared/shareUnique bind
it without a new allocation. This changes C++ object layout and requires a
coherent rebuild, without promising compatibility with old binaries.
Import records capture a private negative canonical-owner observation while the
object is alive. Both compilation entries validate it before traversing resource
wrappers: an invented alias may remain after its genuine owner has died. A false
observation refuses without dereference; a true observation never suffices for
admission, which still revalidates the canonical core control block and original
registration/device. Existing snapshot-owned aliases remain outside this opt-in
profile. Default graph behavior is preserved.

Five-backend lowering: Null permits validator-only Prototype retention; Vulkan
permits Prototype retention of genuinely registered original resources. Metal,
WebGPU and WebGL refuse with RG-SHARED-IMPORTS-BACKEND-UNAVAILABLE until their
original registration/owner join exists. Unknown backends refuse with
RG-SHARED-IMPORTS-BACKEND. No DeviceCapabilities or native execution gate changes.

Use the original cold BoundedRetainer and maxReferencesPerFrame. Finish all
fallible source/binding allocations before retaining; validate every owner and
capacity before committing the full mapped batch. A reservation pins its exact
original storage/bundle when retention is enabled. Close revokes validation but
does not release its owners or storage; replacement cannot redirect an old
token's cleanup into a reused bundle. Cancel or final compiled-source destruction
clears its original retainer exactly once. Compiled copies share one source and
one reservation. Graph mutation/destruction invalidates the source but does not
shorten the retained-owner lifetime. No command owns this source in this slice,
so cancellation never claims GPU safety. Ordinary unique/alias ownership,
partition transitions and private native command composition remain open.

Required tests: default versus opt-in lifetime; five-backend/unknown decisions;
canonical and forged shared control blocks for both resource kinds; foreign and
retired registration; mixed owner rejection; exact/insufficient reference
capacity; compile allocation rollback at every byte boundary; close/replacement,
copies/moves and final cleanup; original Null hidden-frame and Vulkan native
resource lifetime smoke. No changed draw or visual-output acceptance is claimed.

### FrameGraphCompiledOwnedResources/1 (implementation contract; not execution admission)

The next transaction joins actual ordinary graph-owned texture/buffer acquires
to the original persistent FrameBundle. Independent pool `graphOwners/1` and
compiled-source `ownedResources/1` requests require explicit Prototype opt-in.
Pool admission requires the original partition source and a caller-declared
maximum live graph count; acquire-only/default behavior remains excluded.
Accounting cannot be reconfigured while any graph retains the pool.

Null supports validator-only owner transfer; Vulkan supports transfer of real
original resources. Metal/WebGPU/WebGL refuse with
RG-OWNED-RESOURCES-BACKEND-UNAVAILABLE; unknown backends refuse with
RG-OWNED-RESOURCES-BACKEND. No capability, queue or completion is enabled.
Both execution entry points refuse RG-OWNED-RESOURCES-COMMAND-CUSTODY-UNAVAILABLE
before recording, including a source-less directly compiled graph.

Version 1 admits ordinary createTexture/createBuffer/createTransientBuffer,
including empty graphs. Shared/unique imports, desc-only transient images,
alias materialization and mixed sharedImports/ownedResources requests are
explicitly unavailable. Existing shared-import retention remains independent.
These boundaries must fail before dereferencing unauthenticated wrappers or
creating desc-only native payloads. Composition remains unavailable.

The real AllocationReceipt retains a non-releasing identity of its exact
original partition generation. Only the original pool consumes this private
identity after authenticating the original device, registration and pool.
Validation requires the same live InUse record, on the preparation thread.
Identity copies/destruction neither release charges nor keep the allocation
alive, and stale/foreign/closed/incorrect-partition identities fail. This core
metadata primitive grants no L2 resource or native execution authority.

Compile first finishes all fallible source/binding allocations and validates
every receipt and original storage capacity. It then moves the actual unique
owners into the already allocated bundle without allocation, and installs
non-allocating shared aliases in graph records. Aliases own the compiled source,
which owns its original reservation/bundle; the source does not own graph records.
No command/source self-ownership cycle or parallel inventory is introduced.
Resource charges stay InUse: CPU transfer is neither Pending nor GPU completion.
Source publication is one-shot for those records until reset; failed publication
keeps the original graph owners, charges and frame reservation unchanged.

Reset of this never-executable profile destroys uncompiled unique owners or
removes graph aliases directly; it cannot enter a legacy cache/fence return.
External compiled copies retain the original owners until their final release.
Close/replacement invalidate validation but cannot redirect cleanup to new
storage or shorten retained lifetimes. The final reservation clears references,
textures/buffers, then heaps before freeing the original storage. Native physical
allocator cache accounting remains separate from logical pool charges.

Required evidence: core stale/foreign/thread/closed/partition identity tests;
all five backend decisions; missing source/opt-in/capacity and import/transient
refusals; genuine Null/Vulkan authoring, both execution gates, one-shot/reset,
compile byte-quota rollback, close/replacement/copy/final cleanup, and original
native payload survival after graph/pool/facade/bootstrap destruction. Keep Null
hidden-frame, original shared imports, allocation receipts, placement and native
completion regressions. No shader/draw or RenderDoc claim is made. Alias groups,
cache returns, native custody composition and full C3/C4-C6 remain open.

### FrameGraphCompiledOwnedResources/2: mixed original owner transaction

Version 2 of the independent ownedResources request requires sharedImports/1
as well as the original graphOwners/1 partition-enabled pool. Version 1 still
rejects a mixed request; unknown versions and absent opt-in fail. There is no
implicit upgrade. Prepare version 2 before authoring shared imports. Unique
imports, snapshot-owned aliases, desc-only transient images, composition and
alias materialization remain unavailable. Downgrading a source with live shared
imports fails without replacing it. Null is validator-only; Vulkan permits
original owner retention. Metal/WebGPU/WebGL and unknown backends retain the
original owned-resource backend refusal. Both execution entries remain closed.

The original graph records are the sole inventory. Before either compiler entry
traverses resource wrappers, reject invented/non-owning/dead aliases using the
private negative import observation, then authenticate canonical shared owner,
original registration and exact device. Unique members additionally require
the exact original pool receipt and live InUse partition generation. Shared
imports may have an external or another pool owner on the same logical device;
they do not transfer their allocation charge into this pool or get counted as
new unique bytes. Their reference entries are counted once per compiled source,
while graph import deduplication preserves a single identity/hazard chain.

At actual publication, first finish source/binding allocations, validate all
unique and shared owners and all original bundle capacities, and reserve one
whole record from the original partition ledger: frameBundleCount=1 and
referenceCount equal to the actual shared-import inventory. Reserved -> InUse
occurs only at the no-failure owner commit. maxResourceOwners is the total cold
ledger record capacity: this explicit version also consumes one slot for each
bundle that actually owns these resources/references. Existing allocation
receipts keep their own slots and logical/CPU charges. No second ledger or
caller-supplied owner/reference inventory is permitted.

The lease lives in the original FrameBundle. Cold storage/source CPU quota
ownership remains in its original independent domains; the new frame/reference
record does not claim those bytes as allocation-ledger metadata, driver memory
or a complete combined CPU bound. Full C3-c CPU accounting is still open. A zero
or exhausted frame/reference/owner-slot budget refuses publication atomically.
Reserve failure, source allocation failure, invalid owner or capacity failure
leaves graph unique owners, reference counts, original charges and reservation
unchanged. Do not retain imports first and subsequently attempt a fallible
unique-owner admission. The existing bounded containers do all owner retention;
there is no temporary owner array, repeated native creation or hot growth.

The full no-allocation commit retains genuine shared owners and moves unique
owners into that same bundle. Graph unique aliases own the original source;
shared import records retain their canonical creator owners and their existing
reset/import semantics. No source owns graph records or its own command. A
shared-only/empty graph can publish several independent bundles within declared
capacities; transferred unique records remain one-shot until reset. Each bundle
owns exactly one frame/reference lease even across compiled copies. Final source
release clears references/resources/heaps before publishing that lease's release
notification; a remote final owner only notifies the original generation and
the preparation thread collects the whole record. Close/replacement cannot
redirect it into a new accounting/storage generation. Import-only retained
bundles also block original accounting replacement until their lease retires.
No event here authorizes Pending, Cached, GPU completion or resource reuse.

Required tests extend the actual owner fixture: mixed Null and genuine Vulkan
owners; source version/opt-in/downgrade and both execution gates; dead/forged/
foreign shared texture and buffer owners before both compiler traversals;
independent storage/reference/frame/owner-slot limits; exact source-byte quota
rollback; copies/reset/consecutive epochs/import-only bundles/close/replacement;
remote final release and original ledger collection; four real native payloads
surviving graph/pool/facade/bootstrap destruction and final quota/runtime release.
Preserve version-1 shared/unique, core, placement, native completion and Null
hidden-frame regressions. This CPU ownership contract adds no visual claim.

### FrameGraphCompiledOwnedResources/3: non-aliased desc-only materialization

Version 3 explicitly includes version 2's sharedImports/1 and graphOwners/1
requirements. Versions 1 and 2 keep rejecting desc-only authoring. Configure /3
before authoring; downgrading a source with desc-only records fails unchanged.
The compiler continues to derive the actual alias plan from capabilities and
lifetimes. A nonempty plan or existing transient heap refuses publication with
RG-OWNED-RESOURCES-ALIAS-MATERIALIZATION-UNAVAILABLE before creating resources.
No plan is erased, capability overridden or independent allocation silently
substituted for aliasing. Indivisible alias-group admission remains separate.

Null permits only validator resource ownership; Vulkan uses genuine original
device factories. Metal, WebGPU, WebGL and unknown backends fail with the
existing owned-resource lowering diagnostic. No new backend capability, native
execution, completion, cache return or reuse authority is introduced.

Finish graph validation, original slot/storage checks and source/binding
allocation before materialization. The existing persistent FrameBundle is the
only provisional resource owner: create missing non-imported transient textures
there using the existing pool acquire/factory/receipt path. Their backend
initial state is Undefined, matching existing graph-owned image creation.
The source metadata quota scope ends before factories run; persistent factory
registry allocations must not inherit that temporary source scope. Resource
creation uses the same original allocation domains as ordinary pool acquire.
Revalidate each actual original receipt and InUse partition identity. Each
independent allocation is charged once by its original receipt; no new owner
inventory, resource array or accounting ledger is introduced.

The existing frame/reference lease is reserved before factory calls and becomes
InUse only after all materialization and validation succeeds. Until then the
graph's original owners, import refcounts and desc-only records stay unchanged.
Failure clears provisional bundle resources, cancels the original reservation
and releases their original logical/receipt charges. Native allocation attempts
and the original allocator's independently owned physical cache are observable
and need not roll back their historical counters or cached backing. This is not
a combined CPU or physical VRAM bound. All prior owner/slot budgets still apply.

At the no-allocation commit install source aliases for the new resources,
retain imports and transfer the preexisting unique owners into the same bundle.
Populate already allocated binding storage from these actual objects. Reset,
copies, close, storage replacement and remote final destruction preserve /2's
original-generation cleanup rules. Both execution entries still fail before
record. Every failure after the first successful factory call must leave the
graph retryable, with no exposed provisional object or retained bundle slot.

Required evidence extends the existing owner fixture: genuine Null/Vulkan
desc-only compilation mixed with original unique/shared owners; no native
creation at authoring or standalone compiler traversal; version/downgrade and
alias-plan refusal; storage/source/frame/reference/owner-slot/resource quota
failures; partial materialization rollback, retry and byte-quota boundaries;
reset/copy/close/remote cleanup and native payload survival after graph, pool,
facade and bootstrap destruction. Keep original owner, placement, completion
and Null hidden-frame regressions. No visual or RenderDoc claim is added.
