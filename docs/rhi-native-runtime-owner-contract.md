# NativeRuntimeOwner/1

状态：完整L1仍为Draft，P0-10 Open。runtime准入、bootstrap实际owner和command/cache
传递保留的限定增量已完成Contract Complete、Vulkan Backend Validated，以及Windows/
MinGW/RTX3050范围的Platform Validated；独立resource/sync/heap及失败析构仍未完成。
整个L1、P0及Production Ready未关闭，生产WebGL未实现。

## L2不变量与现有契约

复用OriginalDeviceLifetime/1、command retention、exact staging及ScopedPresentRetirement/1。
任何能执行native dispatch或销毁native子对象的closure，必须持有原native device及其
instance的实际owner，且最后一次native操作先于owner释放。包装对象存活不等于设备存活。
保留owner不证明Accepted、GpuCompleted、present消费或恢复成功；idle失败/loss仍不能
回收pending工作。上层API不新增native handle、native lifetime token或backend enum分支。

## 五backend lowering

| backend | 结果与边界 |
| --- | --- |
| Null | Validator-only；不能签发native ownership或completion，不新增硬件能力 |
| Vulkan | 原生bootstrap实际owner经backend-private typed shared owner保留；身份缺失/不符为DiagnosticFailure，synthetic仅validator |
| Metal | 原MTL对象retain与command托管路线保持；本增量不改变执行payload，缺实际owner不能据包装对象推导可用，不新增Apple验证 |
| WebGPU | 原device/resource引用及callback托管路线保持；本增量不改变执行payload，callback注册不证明完成，不新增browser验证 |
| WebGL | 生产backend未实现，DiagnosticFailure；未来context owner/generation继续按P2/P3实施，不接受typed-array/CPU报告替代 |

本契约不增加capability或NativeSync，不修改公共L2布局；backend-private descriptor新增字段
需要同版本重编。真实native bootstrap属于受信任创建边界；任意C++ fixture派生类不能
作为Native成功证明，query fixture的合成owner只用于CPU契约测试。

## Vulkan admission与所有权

1. `VulkanRhiNativeRuntimeOwner`为backend-private OOP边界，提供instance、physical device
   与logical device的只读身份。生产实现就是实际持有destroy流程的平台bootstrap对象，
   不另创建空壳或仅含boolean的owner。owner不得反向持有runtime，避免循环。
2. native descriptor必须持有该owner且三项非空身份逐项一致。缺失诊断为
   `RHI-VULKAN-RUNTIME-OWNER-MISSING`，不匹配为`RHI-VULKAN-RUNTIME-OWNER-MISMATCH`。
   两者在任何native查询、资源创建或提交前拒绝；失败runtime不得在析构时解析device入口。
   `admissionDiagnostic()`及inspection返回稳定ID，factory拒绝时同步报告该ID。
   拒绝的native runtime不发布handle/resolver、memory/format/surface默认事实；synthetic
   validator不返回或调用native resolver，也不报告native device/surface可用。
3. runtime的共享Impl保留owner直到自己的native清理结束。command与现有有界reusable
   cache保留原runtime，因而传递保留真实owner；不新增全局表、延迟帧数或等待。
4. 生产bootstrap创建runtime时显式传入自身shared owner。外部bootstrap引用释放后，
   pending command/cache不能失去真实设备；runtime销毁最后释放owner。owner析构不是
   completion issuer，真实device loss仍保持独立失败状态和托管规则。
5. 独立descriptor、sampler、pipeline、texture view、VMA state、alias heap、timeline
   payload须分别证明所有权；不能仅因上述runtime修复就宣布这些路径完成。借用runtime
   引用或raw VkDevice的长寿命对象必须迁移到原owner链，并补析构顺序负向/native证据。

## 分配、预算和恢复

### L1-c/1：独立VMA、descriptor与sampler

沿用上述L2不变量及五backend结论，不增加capability或改变completion语义。
VMA共享state必须拥有原runtime，直到最后buffer/image allocation及allocator自身的
native销毁结束；allocator facade、resource factory和bootstrap可先释放。
descriptor set/layout/pool与sampler payload必须拥有原runtime，禁止借用creator的
runtime引用。原runtime持有真实device/instance owner；所有销毁必须先于owner释放，
不得用另一个device、延迟帧数、全局保活或等待替代。引用链不得形成循环。

本子批复用现有core共享分配和原资源预算，只增加每个现有对象的共享引用；不新增
hot-path对象、容器、等待或shader编译。Null仅验证；Metal/WebGPU原retain链不变且
无新增目标平台证明；WebGL仍DiagnosticFailure。失败创建继续原确定性诊断和清理，
不创建占位资源。本子批不关闭timeline、alias heap、独立view/pipeline/query、WSI或loss。

四类资源必须独立运行真实native测试，避免一个正确owner掩盖另一资源的缺失：
释放factory/allocator/bootstrap后，仅保留显式rescue runtime观测引用，检查payload
确实另有拥有者；red路径先在rescue存活期间释放payload，再报告失败，禁止故意UAF。
green路径释放rescue后，buffer/image执行真实memory-requirements查询，descriptor/
sampler保留原native身份；销毁dispatch必须观察原device、runtime及实际owner仍存活，
最后payload释放后weak runtime/owner均过期。VMA还要覆盖最终device memory释放。
该测试不提交GPU工作，因此不签发completion或证明pending资源可以提前销毁。

复用原bootstrap/core::makeShared对象，只新增共享引用；不新增每draw/frame分配、IO、
编译或等待。引用带来的存活时长随原command/cache/resource预算计量，不扩大容量。
全局pending、cache及最后owner释放顺序仍须审查；无法证明的路径保持Open而非无界隔离。
缺失/错配可在未执行native工作时用正确owner重建runtime；不对lost runtime原地换owner，
不重试不确定提交，不将保留引用改称设备恢复或有界GPU drain。

## 必须保留的验证

### L1-c/2：NativeTimelineOwnership/1（实施前契约）

沿用L2原device/lifetime、NativeSync与completion硬边界。`GpuTimelineSemaphore::signal`
是显式host signal，`wait/hostWait`是显式host等待，均不是queue完成凭据；GPU侧wait/
signal只由GpuQueue timeline overload记录。修正注释不改变现有公共签名或行为。

| backend | NativeTimelineOwnership/1 lowering |
| --- | --- |
| Null | Validator-only counter，不签发native完成；原validator timeline路径保留 |
| Vulkan | Native：typed共享semaphore payload拥有原runtime/真实device owner；提交仅消费该payload，验证命令device身份，固定两个引用保留至原fence或原runtime真实idle证明退休 |
| Metal | 原shared-event payload已拥有nativeDevice/event/listener；submit检查commandDeviceMatches，native command托管引用；保持既有Native路线，本批不新增Apple执行证据 |
| WebGPU | Degraded：core无queue timeline primitive，创建null、带timeline submit在native调用前DiagnosticFailure；callback counter不能替代 |
| WebGL | Degraded/DiagnosticFailure：生产backend未实现，WebGL1无native timeline；不得用ordered issue或CPU counter替代 |

Vulkan独立payload在creator/device/bootstrap释放后仍必须可真实signal/query/wait/destroy；
最后native destroy先于runtime/actual owner释放。所有入口来自被准入且启用timeline的原
runtime；缺失、synthetic、未启用、缺entrypoint及native创建失败均返回明确诊断，无Dummy。
backend-private submit info改为typed payload引用，禁止裸semaphore进入生产提交路径。
wrong-device/无效payload在fence创建、retirement注册及vkQueueSubmit前拒绝，不能凭
backend类型、caller布尔值或raw handle非零推导合法。

提交前将wait/signal两个共享引用放入既有command固定字段，并预留既有有界pending
slot；accepted后caller wrapper销毁不能销毁native semaphore。原fence成功或受原runtime
认证的真实device idle可释放引用；另一device完成、timeout、失败idle、wrapper析构、
host signal和cleanup均不能授权退休。无每submit分配、额外等待、全局新表或容量增长。
队列调用失败时timeline command终止重试并保持unproven/pending引用，诊断为
`RHI-VULKAN-TIMELINE-SUBMIT-UNPROVEN`或`RHI-VULKAN-TIMELINE-SUBMIT-DEVICE-LOST`；
不能把未知执行当未执行回滚。该失败托管沿用既有64 command上限，耗尽拒绝，最后
原runtime真实idle或原fence是退休依据；device-loss无成功idle的最终清理仍属L1-d。
这里只保留有界引用，不承诺新的deadline drain或GPU完成。

必须验证独立wrapper owner、真实timeline-blocked提交后释放两个wrapper、错误device
在native调用前拒绝，以及失败queue/失败idle/另一device idle不释放、原真实idle释放。
red测试用显式rescue runtime及有界fixture native-destroy guard安全清理；guard若触发
就失败，不能当native成功证明。green必须无guard延期，完成和销毁来自真实native结果。
另保留CPU admission contract、Null hidden-frame及原WSI/RenderDoc/AI回归。

- 快速unit：owner admission的缺失、三项逐一错配、正确身份及synthetic分离；失败不查询。
- 实际runtime gate：缺失/错配时valid/capability关闭、native resolver不被调用，析构也不调用。
- 真实双runtime：pending timeline command在bootstrap/wrapper释放后仍保留原owner；另一
  runtime完成不授权它；原GPU fence完成后，cache清理与最后runtime释放的次序可观察。
- 已完成command的reusable cache、VMA/独立payload逐类有native lifetime证据；失败idle/
  lost只作失败注入，不将注入成功当Native证明。所有测试退出前安全释放真实GPU等待。
- Null hidden-frame、原Vulkan WSI hidden smoke及RenderDoc捕获/回放/AI图像检查；目标
  代码必须在本隔离worktree自己的MinGW构建目录验证，不移用原目录cache身份。

每个子批更新主计划、lifetime audit、validation和TODO。runtime子批通过不关闭整个L1，
更不提升完整P0、Metal/WebGPU目标平台、生产WebGL或Production Ready。

## 2026-10-17 限定实施与证据

证据位于本隔离worktree的`build/webgl1-evidence/p0/native-runtime-owner/`。

- `red-registered/`为11输入一致的源码contract red，不是native GPU red。
- `implementation-first/`六个定向目标通过；联合正则选中尚未构建的staging-upload-pool
  executable导致Not Run，保留失败。补建后`implementation-verified/`实际MinGW七个目标
  及联合回归通过、1392输入一致。未放宽生产门禁或测试断言。
- `direct-native/`真实双RTX3050 device的timeline阻塞命令：释放bootstrap/wrapper后仍有
  backend owner，另一device完成不退休它；原fence完成后cache继续保留owner，清理后
  weak owner/runtime均过期。此直接检查在factory诊断补充前执行，最终版本由下项复验。
- 最终`verified-reconciled/`在依赖补齐及原WSI构建后执行14/14 CTest，无skip，1970项
  命名输入前后相同；保存7个binary hash及包含真实native输出的LastTest日志。
- `native-verified/`在物理模块编译时因空Jolt gitlink目录失败，未执行smoke/capture。
  官方v5.6.0与仓库锁定e77f175595e64cb44218cc9d9d56fc365ad0e36a相同；只在本目录
  检出精确依赖并重新configure。`dependency-checkout.yaml`记录来源和clean identity。
- `native-verified-dependencies/`实际MinGW构建、原Vulkan WSI hidden smoke、RenderDoc
  捕获及回放均exit0，1970命名输入一致。三类graph依赖各4次，零validation message；
  AI已查看320×180全幅蓝色clear，两个present image为BGRA[191,127,32,255]。
  capture、present.png和独立AI检查记录均保留。该图不证明WebGL、独立资源或无maintenance WSI。
- `verified-final/`只保留一次Ninja dry-run就绪判定失败：glob检查显示CMake重新生成步骤，
  未运行CTest，不当作编译/native失败。最终接受上述真实执行的`verified-reconciled/`。
- 11份C++由21.1.8格式化并通过dry-run及共享tokenizer检查；`format-final/`确认源码字节不变。

本增量不新增每帧等待或分配，不签发completion，不改变cache容量。
下一子批必须处理VMA state、descriptor/sampler借用、timeline和alias heap等独立owner链；
八份源审查在`next-resource-review/`，仅为源码证据。pipeline/query pool已持有shared runtime，
应复用该链并增加真实跨wrapper析构测试，不重复添加空壳owner。

## 2026-10-17 L1-c/1 独立资源限定验收

证据位于本隔离worktree的`build/webgl1-evidence/p0/native-resource-owner/`。
VmaState现在保留原shared runtime，allocation保留state；vmaDestroyBuffer/Image及
最后vmaDestroyAllocator/free memory均在runtime/device owner释放前执行。
descriptor set/layout/pool及sampler payload由借用runtime改为拥有shared runtime。
只新增原对象内共享引用，不新增等待、容器、cache容量或执行路径。

- `red/`：1972命名输入一致；18项CTest中仅四项真实资源owner断言失败。
  buffer/image/descriptor/sampler均`retained=0 independent=0`；rescue保留原wrapper
  和设备直到native销毁结束，`owned-destroy=1 destroyed=1 released=1`，不是UAF崩溃。
  build.py首次查询仍选中旧八项别名，后续联合CTest已实际执行新注册四项并记录red。
- `implementation/`：七个target经统一build.py重建/测试，最终联合18/18无skip，
  1972命名输入一致。四种模式全部`retained=1 independent=1 native-use=1
  owned-destroy=1 destroyed=1 released=1`；buffer/image均记录一次最终memory free。
- `native-verified/`：本worktree MinGW重链接、原WSI hidden smoke、RenderDoc捕获/
  回放均exit0，1973命名输入在构建前后及执行后相同。三类图依赖各4次，零validation
  message，两present image均BGRA[191,127,32,255]。AI已实际查看320×180蓝色clear；
  `graph_capture.rdc`、`present.png`、`ai-image-inspection.yaml`保留。
- `format/`：四份C++经21.1.8格式化、dry-run及共享tokenizer验证；review checkpoint
  绑定最终source/tool/binary/capture，单列验收后的文档状态更新。

仅本子批Contract Complete、Vulkan Backend Validated、Windows/MinGW/RTX3050范围
Platform Validated。测试未提交GPU工作，不证明pending对象可销毁；WSI截图只证明原
graph/barrier/clear回归。pipeline/query/view、timeline、heap/pool借用allocator、失败idle/
loss及独立swapchain仍Open，完整L1/P0-10及P0计数12/19不变。无新增Metal/Apple、
WebGPU/browser、生产WebGL或Production Ready声明。


## 2026-10-17 L1-c/2：NativeTimelineOwnership/1 限定验收

证据根为本隔离worktree的`build/webgl1-evidence/p0/native-timeline-owner/`。
私有typed payload拥有原runtime/真实device owner；公共timeline wrapper和command均
持有该payload。提交前核对device身份，先占既有有界退休slot，再固化两个共享引用；
只有原fence或原runtime真实idle可退休。失败queue调用保留unproven引用并禁止重试，
没有裸semaphore提交入口。host signal/wait注释已与实际行为一致，公共签名不变。

| 记录 | 结果与边界 |
| --- | --- |
| red | 首轮仅为测试helper名称错误导致编译失败，不算native red |
| red-registered | 四项真实native red：独立owner缺失、pending时两semaphore提前销毁、错误device进入native submit并创建fence、失败submit未托管。固定两handle rescue安全释放GPU等待后失败；16项中仅新增四项失败 |
| implementation | 首次实现编译因private loss capture缺friend失败；未运行native测试，保留原日志 |
| implementation-reconciled | 统一build.py七target及联合23/23 CTest，无skip，1976命名输入前后哈希一致，七binary hash和实际native输出保留 |
| 独立与pending | 独立payload跨creator/bootstrap后真实signal/query/wait/destroy；pending提交释放两个wrapper仍保留，另一device idle无权退休；原fence完成才销毁，销毁前观察GPU signal值2。两模式最后weak owner/runtime均过期 |
| wrong-device与unproven | wrong-device拒绝时native fence/submit均0；注入queue OOM只证明失败托管，另一device idle与注入原idle失败都不释放，原真实idle才释放；不冒充真实GPU故障证据 |
| admission与预算 | CPU缺runtime/synthetic/未启用拒绝且零resolver调用；native五entrypoint缺失、创建失败垃圾输出拒绝；真实初值7→signal/wait/query9。成功pending submit断言core CPU新增分配0，绿色路径rescue延期0 |
| source-contract | command recording源码契约417/417通过，保存输入哈希；不替代native测试 |
| format | 初次reconciled检查因include排序改变token顺序失败，未保存before字节，明确不采信该轮。最终format-final为13份C++保存before/after，21.1.8 dry-run、token及字节一致检查全通过 |
| native-verified | 独立MinGW重建、真实原WSI hidden smoke、RenderDoc捕获/回放均exit0，1977命名输入前后相同；三类graph依赖各4、validationMessages为空 |
| capture与AI | graph_capture.rdc、present.png与ai-image-inspection.yaml保留；AI实际查看320×180全幅蓝色clear，两present image均BGRA[191,127,32,255]。只证明原graph/barrier/clear回归，不由图片推导timeline所有权 |

本子批限定Contract Complete、Vulkan Backend Validated及Windows/MinGW/RTX3050范围
Platform Validated。Metal保留既有shared-event原device检查；WebGPU/core timeline关闭，
WebGL生产backend未实现；没有新增Apple/browser宿主或Production Ready证明。
完整L1/P0-10和P0计数12/19不变；c/3 heap/pool/placed image、c/4 pipeline/query/view、
失败idle/loss最终teardown、非timeline失败submit、query回滚及独立swapchain仍Open。
next-heap-review/保存10份命名源码与复用审查：pool借用device成员allocator，allocation
缺runtime；wrong-device/offset/alignment/native query fallback还需独立contract和负向。
下一批先固定这些L2及五backend结论，再修改heap路径，不把本批通过扩张为整个P0。


## L1-c/3 owner子批续接

原heap/pool借用缺口已有限定修复与真实两模式证据：pool拥有shared runtime和共享
allocator facade；allocation持有原runtime，placed image传递保留它。共享ledger unit
通过；25/25 CTest、1979输入一致，原WSI/RenderDoc/AI检查与1980输入一致。
证据`build/webgl1-evidence/p0/native-heap-owner/`，详细边界及失败见
`rhi-native-heap-owner-contract.md`。完整c/3 placement、native销毁顺序观测、热路径、
c/4、失败shutdown和L2～L6仍Open，P0计数12/19不变。
