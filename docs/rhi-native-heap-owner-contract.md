# NativeHeapOwnership/1

状态：原device/allocator owner子批限定Contract Complete、Vulkan Backend Validated及Windows/MinGW/RTX3050 Platform Validated；完整P0-10/L1-c/3仍Open。
本文件先于实现，不授权新的alias capability，不关闭完整placement正确性或热路径门槛。

后续placement限定实现见HeapPlacementAdmission/1及主计划18.6：以下第4条和Null表格
中的缺口是owner子批历史状态。当前Vulkan已即时分配真实heap、验证原device/native
requirements/range/type/dedicated，Null也已验证原ledger及range。新模块继续保留本文件
原runtime/共享ledger所有权；native destroy/free独立观察与Windows回归通过。普通预算
查询域、完整持续负载、alias内容及Apple平台仍Open，不因该后续子集关闭完整c/3。

## L2不变量与边界

1. 每个heap lease、backend pool及physical allocation必须可追溯到创建它的原device。
   creator/device wrapper可以先释放；最后native image/memory销毁必须先于实际device
   owner释放。shared runtime必须包含实际bootstrap owner，不能只是raw handle token。
2. pool的后续allocation与budget操作不能借用creator成员的allocator地址。复用现有
   VulkanRhiAllocator共享ledger/VMA/runtime状态，不复制budget计数或建立第二本账。
   allocator facade可以值复制共享state；复制发生在cold device/pool创建，不在每draw/frame。
3. placed image必须拥有physical allocation；physical allocation拥有原runtime和原ledger
   release token。native image先destroy，再free memory，最后release budget与实际device。
   heap wrapper或pool引用释放不能提前销毁仍被image/command引用的allocation。
4. 本子批不改变既有heap延迟分配时点，不将其视为完整Native heap创建验收；当前lazy
   allocation与公共createMemoryHeap承诺的差异须在c/3 placement子批显式解决。
   原device身份、offset/alignment/range、dedicated要求、memoryTypeBits、capability gate和
   native query失败时的estimate fallback均单独Open，不因本owner子批通过而获得授权。
5. 完成与所有权分开：保留device不签发completion。pool只有成为allocation唯一owner时
   才可复用；command引用必须继续依赖原completion，不能由析构或frame age提前清空。
   无新的wait、queue idle、容量增长、per-frame编译或全局保活表。

## 五backend lowering

| Backend | 原device/allocator所有权决定 |
| --- | --- |
| Null | Validator-only heap/texture与ledger；不证明native alias。现有placed路径忽略heap/offset的validator缺口留在placement子批，不能称Equivalent |
| Vulkan | Native所有权：pool拥有原shared runtime及共享allocator state；physical allocation持有原runtime；placed payload拥有allocation并报告原device。既有capacity、budget和completion规则不扩张 |
| Metal | 既有native heap/texture payload retain原nativeDevice及heap；native placed factory比较heap.device与原identity并验证范围。保持既有路线，本批无Apple执行证明 |
| WebGPU | Degraded/DiagnosticFailure：无placed-resource primitive，createMemoryHeap/createPlacedTexture返回null；不得普通allocation冒充alias |
| WebGL | Degraded/DiagnosticFailure：生产backend未实现，无native heap alias；ordered issue不能替代physical alias或native completion |

## 复用、预算与失败

现有VulkanRhiAllocator复制共享runtime/VmaState/ledger，allocation release token保留同一
ledger；优先使用这一能力，禁止重复实现账本、预算加法或release算法。现有pool容器
采用core::pmr并绑定core CPU memory resource；owned对象用core::makeShared/makeUnique。
新增引用为每pool一个runtime及allocator-state、每physical allocation一个runtime；
placed payload已有allocation引用可传递保留runtime，避免无意义的第二套owner。
原native allocation失败返回null，原budget拒绝保持拒绝，不创建Dummy；无未知语义fallback。
本子批不宣称cold pool创建可消除现有pool增长/冷分配门槛，完整热路径仍Open。

## 验证顺序和退出条件

- 先在真实Vulkan创建的heap lease及placed image上复现owner缺失。lease无native memory
  时只算owner contract red；placed模式真实创建/bind image并查询requirements，才是
  该payload的native owner red。不能混为native heap allocation完成证明。
- red只保留显式rescue runtime，creator释放后检查payload是否另有runtime owner；若缺失，
  保持rescue直到安全销毁，再报告失败。禁止为制造red实际调用悬空allocator或错误device。
- green分别覆盖heap/placed payload独立于creator；已有image及heap wrapper释放后仍
  保留allocation，真实requirements查询有效，最后两个weak owner/runtime过期。
- 必须另补共享allocator副本在原facade释放后操作同一ledger的unit/native证据，确保
  不只加入runtime引用却继续借用已析构的allocator。pool后续分配验证需backend-private
  可复用入口，不能在fixture复制生产pool算法。
- 统一MinGW focused contract/unit、Null hidden-frame及原WSI hidden smoke/RenderDoc/
  AI图像检查；锁定21.1.8格式、输入/工具/artifact hash，保留失败轮次。

只有上述owner条件满足，才能声明c/3 owner限定Contract Complete/Vulkan Backend
Validated/Windows Platform Validated。完整c/3仍需placement/capability/原native销毁
次序与pool复用预算证据，完整L1、P0-10、P0及P1～P4均不由此关闭。


## 2026-10-17 NativeHeapOwnership/1 owner子批限定验收

证据根：`build/webgl1-evidence/p0/native-heap-owner/`。完整L1-c/3仍Open。
pool改为拥有原shared runtime和值复制的VulkanRhiAllocator；复制共享原VmaState/ledger，
不借用device成员、不创建第二本预算账。physical allocation保留原runtime，placed
payload持有allocation，在image析构后才释放它；其native device身份来自该allocation。
pool动态容器显式使用core::pmr和core memory resource，没有新增wait/容量/global表。

| 记录 | 实际结果与边界 |
| --- | --- |
| red-registered | 25项中仅两项新owner测试失败，1979命名输入一致。lease retained/independent=0；真实placed image创建/bind及requirements查询成功但retained/independent/device-identity=0。显式rescue始终保留实际device直到安全释放，不调用已悬空allocator。lease未创建memory，只算owner contract red |
| implementation | 两native模式已绿；旧device源码guard仍要求allocator_->allocate借用写法而失败，保留日志，不算最终验收。迁移为pool作用域内必须持有allocator值/shared runtime，并禁止borrowed pointer；原ledger/capacity/retirement断言保留 |
| implementation-reconciled | 统一build.py七target与联合25/25 CTest通过、无skip；1979命名输入前后哈希一致，七binary hash和LastTest实际输出保留 |
| lease native模式 | 原device/bootstrap释放后heap独立保留owner；再用同一原runtime的新device facade通过存活pool首次创建placed image，实际触发原pool allocator副本，随后新facade/heap也释放；仅payload仍存活时native requirements与原查询一致，device identity正确，最后weak runtime/actual owner过期 |
| placed native模式 | 真实image/bind后释放texture wrapper/heap/device/bootstrap，仅独立payload持有实际allocation/runtime；真实requirements查询与device identity有效，最后weak runtime/actual owner过期。未提交GPU工作，不授权pending资源销毁 |
| allocator unit | 原facade与副本观察同一ledger；768/1024预算拒绝257，释放原facade后释放token并再分配512达到1024，最后bytes/live count均0，rejection count1；没有重置或复制预算计数 |
| format-final | 五份C++保存before/after；锁定21.1.8格式、dry-run、共享tokenizer和字节一致检查通过 |
| native-verified | 独立MinGW原WSI target重链接、真实hidden smoke、RenderDoc capture/replay均exit0；1980命名输入构建前后及执行后相同 |
| capture与AI | graph_capture.rdc、present.png及ai-image-inspection.yaml保留。AI实际查看320×180全幅蓝色clear；三类graph依赖各4、零validation message，两present image均BGRA[191,127,32,255]。只证明原graph/barrier/clear回归，不证明heap alias画面 |

owner子批限定Contract Complete、Vulkan Backend Validated及Windows/MinGW/RTX3050
Platform Validated。完整c/3还缺独立native free/destroy顺序观测、pool复用/预算热路径、
lazy heap创建承诺、capability与执行一致、heap原device准入、offset/alignment/range、
dedicated/memoryTypeBits及query失败时estimate fallback处理；Null placed validator
缺口也Open。c/4、L1-d、L2～L6、P0-01/02/04/07/08/09/10继续；P0仍12/19。
没有新Metal/Apple、WebGPU/browser、生产WebGL或Production Ready证明。
下一步先补完整HeapPlacementAdmission的L2与五backend契约，再测试非法请求在
持久native allocation/bind之前拒绝；未知native requirements可以有显式冷查询，但
不得将临时查询成功、逻辑estimate或wrapper非空当作兼容/绑定/完成证明。
