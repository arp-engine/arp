# WebGL1 资源托管、复用与计量证明要求

日期：2026-10-16。状态：P0 Draft；本文是待实现的领域契约和验证清单，不是 Contract Complete 或任何 backend/platform readiness。关联 `rhi-webgl1-contract.md`、`rhi-webgl1-lifetime-audit.md`。现有 `OrderedResourceReuse/1` 仅判断 eligibility，始终 executable=false；本文不改变执行门禁。

## 阶段归属

P0 先固定语义、不变量、五后端结论、逐对象 API 证据和负向测试。P2 实现真实 registry、context/queue 和单帧执行；P3 验证多帧回收、授权、预算与 loss。P0 不以调用者填写的布尔值签发真实权限，也不要求提前实现 P3；进入 P1/P2 仍须先完成其所依赖的 L2/lowering 契约。真实托管的证明不能由本文替代。

## 三种独立寿命

1. CPU record：从 record 成功到实际桥调用消费或 cancel，必须保留不可变 snapshot 和全部资源依赖的强引用。record 失败不改变 ownership；同一 encoder 的 seal 只成功一次。
2. Native issued：桥调用成功之后，依赖 API 本身的 client-data 复制、同 context 命令顺序及对象 delete 规则；CPU issued 不是 GPU Completed。native 执行出错时不能撤销已经发生的副作用。
3. Logical identity：wrapper、descriptor revision、resource ID、context epoch 和 pool lease 分别失效。GL 名字、table slot、generation 数值或 shared_ptr 地址相同均不能认证旧引用。

每个 record operation 要 join 同一个 backend-owned registry 的 device/context/queue、确切资源及其 parent closure、逻辑 identity、descriptor revision 和内容/usage 范围。只保存 std::shared_ptr<const void> 或通过 CPU helper 的 epoch 检查不足以证明这些事实。记录后发生外部写入时，旧快照不更新；要么保留该版本的实际数据，要么按明确的 stale-revision 规则在原生调用之前拒绝。

首期只允许已验证的 owner-thread GLES2 直接调用路径。异步 JS 代理、pthread/OffscreenCanvas 代理、共享外部 context、用户在 backend 外修改 GL 状态均不在等价域；未来扩展须单独证明复制时点、线程/ordering、预算和 loss 回调。当前 browser fixture 不能证明这些代理路径。

## Host payload 的消费点

| 类别 | 必须完成的操作 | CPU 可释放的必要条件 | 现有证据 / 未完成项 |
| --- | --- | --- | --- |
| bufferData / bufferSubData client bytes | 对应 GLES2 bridge 调用实际返回且无已知错误/loss | 原生调用已复制 client 源，executor 不再读取该 snapshot；其余录制引用仍独立保留 | /2 probe 源清零及数值/RenderDoc通过；真实 command ownership、失败路径待P2/P3 |
| texImage2D / texSubImage2D client bytes | 精确format/type、extent、destination origin、row packing的调用消费 | 与buffer相同；每一块/行的source范围必须明确，不能把EMSCRIPTEN返回码当GPU completion | /2覆盖单texel及局部更新；多行pitch、mips、边界和格式矩阵待验证 |
| uniform scalar/vector/matrix arrays | 使用预先生成的leaf位置/类型/数量完成GL传入 | 每个draw的快照被复制；不能只保留指向可修改caller内存的指针 | probe双draw和matrix可行性；生产leaf ABI、数组/partial update待P1 |
| shader source / reflection / link metadata | 冷阶段program编译/link和binding-plan创建完成 | compiler/client source与后续诊断所需身份分别托管；cache按明确hash/revision持有 | probe工具链通过；生产package和热路径门禁待P1/P2 |
| descriptor metadata | 实际draw读取不可变binding plan结束 | revision与全部资源依赖仍有效；没有原生descriptor-set对象可代替该检查 | P0 original descriptor routes拒绝；普通binding plan待P2 |

WebGL规范是语义依据，Emscripten版本及实际生成桥是复制时点的实现依据，两者缺一不可。create/record时可知的错误必须先拒绝；native调用后才可知的OOM/context loss记录失败和accepted范围，不能标成“源已安全消费并且GPU成功”。数据无后续CPU读者时可以释放CPU副本，但这只终结CPU使用权。

## Native 对象逐类退休要求

| 对象 | 逻辑退休前的必需条件 | delete/解绑要求 | 禁止推导 |
| --- | --- | --- | --- |
| Buffer | 无尚未issue/cancel的record引用；所有已issue访问顺序属于同context/queue | 清理backend跟踪的vertex attribute/element/array绑定及可选VAO关联；按依赖顺序执行delete | glIsBuffer=false不证明driver物理内存已释放；不能只清GLuint表 |
| Texture2D | 原wrapper/view/descriptor/attachment引用已经处理，未产生feedback | 所有被backend使用的texture units及FBO attachment关联显式解除；再delete | 对象名失效不证明已采样完成；sampler参数变化不产生独立纹理副本 |
| Renderbuffer | parent FBO引用已解除且无未issue命令 | 解除FBO attachment和renderbuffer binding，再delete | 删除FBO不自动删除其renderbuffer/texture |
| Framebuffer | 不再是当前render target，旧record不再引用其attachment组 | 切换到明确目标/默认framebuffer，再解除关联并delete | 不授予attachment内容的CPU读取或completed publication |
| Program | 没有未issue的pipeline/descriptor/draw引用，state cache已解除current program | useProgram切换后delete，独立管理附着shader与cache ownership | deleteProgram不等于GPU完成；program不是可随draw重建的临时对象 |
| Shader | 编译/link的依赖已完成，attached program依赖已跟踪 | 固定GLES2桥采用link→验证attachments→detach→验证零attachments→delete；不得在桥清除名字后再用旧GLuint调用detach | 桥的glIsShader=false不证明native attachment已解除或物理释放；不能按program删除推断shader已删除 |
| Sampler descriptor | 所有record和binding-plan CPU引用已经处理 | WebGL1无独立native sampler对象；在draw前显式设置texture参数。同draw同texture冲突则拒绝 | 不创建dummy sampler或隐式复制纹理来假装独立状态 |
| Canvas/default framebuffer lease | 当前acquire已present/discard/failed，记录的context epoch有效 | 由platform lease控制；不制造可长期采样的swapchain image | 宿主present不证明GPU completion或真实扫描输出 |

首期不支持的cube/3D/array texture、native heap、query/sync等不通过上述Texture2D/Buffer规则获得权限。可选VAO、depth texture等只有完成独立extension opt-in和实际payload验证后才使用该关联图；否则创建/prepare阶段拒绝。

以上buffer/texture/program的特定issued/delete序列有/2可行性捕获；本次/6另验证shader source、attach/detach/delete和FBO/renderbuffer关联/解绑/逐名退休。P0-12的定义与限定可行性证据已齐全；未提交取消、native failure、generation、真实预算及loss的production registry执行证明仍由P2/P3验收，不能把表格当作生产实现已验证。

### WebGlObjectRetirementFacts/1：冷探针的逐项证据

规范依据为[Khronos WebGL 1.0.3](https://registry.khronos.org/webgl/specs/1.0.3/)及其引用的GLES2对象/输入数据规则（组合附件见FBO_ATTACHMENTS章节）；固定桥依据为当前Emscripten的`src/lib/libwebgl.js`，实际工具身份与文件hash必须随运行保存。其glDeleteShader调用WebGL deleteShader后立即清空GL.shaders[id]；glDetachShader又从该表取对象，因此桥GLuint退休和WebGL native attachment寿命必须分开。不能用已删除的桥名字补做detach，也不能用桥glIsShader的false证明native释放。

冷探针按以下顺序验证并报告每项独立布尔事实，缺失或false即失败；它们不授予生产权限：

- 两个stage分别在glShaderSource返回后覆盖client source，再compile/link；编译成功及后续数值draw验证源字节已被消费。生产诊断仍独立保留source hash/package metadata。
- 查询linked program的两个确切shader attachment，detach两个仍有效的桥名字，查询attachment count为零，再delete两个shader。分别验证名字退休和linked program仍有效；后续原有六draw验证其可执行内容。
- 查询颜色附件确切texture名和WebGL1独立DEPTH_STENCIL_ATTACHMENT的确切renderbuffer名；解除两处关联后查询各自object type为NONE；显式解除framebuffer/renderbuffer/texture binding并查询为零。WebGL1规范的“Framebuffer Object Attachments”将组合点独立定义，不能按其他API假设它是DEPTH_ATTACHMENT与STENCIL_ATTACHMENT两个查询的别名。
- delete FBO后检查FBO名字失效而texture/renderbuffer仍有效；delete renderbuffer后检查其名字失效而texture仍有效；最后delete texture并检查名字失效。这不证明driver physical memory、GPU completion或销毁中断/loss处理。

五backend边界保持不变：Null只验证相应L2 ownership/状态及测试输入；Vulkan/Metal/WebGPU继续使用各自真实native引用和completion退休规则，不套用GL delete语义；WebGL production仍DiagnosticFailure，直到P2/P3实现原registry、retention、generation和预算。P0只验收以上可行性事实、negative report/schema tests及定义；真实取消/失败/loss路径仍须由P2/P3证明。不可用的native heap/query/sync及未授权扩展不因这些事实而开启。

证据：`build/webgl1-evidence/p0/custody-contract-final/`记录MinGW8/8（含Null）、artifact4/4、真实Chromium/6的60帧及命名输入一致。`custody-capture-20261016T074404Z/webgl1_frame12.rdc`实际回放六draw、零replay error，三阶段图像由AI检查；同目录evidence.yaml记录source/artifact/bridge/spec/capture身份。捕获请求8帧在有界40秒内取得4个文件，只用实际完成的frame12做限定证明，不将未到达的捕获写成成功。组合附件错误查询的首次失败保留在custody-browser-smoke/，没有作为验收证据。

## Ordered reuse 的授权形态

`OrderedResourceReuseIntent` 是不可信的工作负载声明。即使所有布尔值填写正确，`assess`仍只返回orderingCompatible；不能修改resource状态、释放quota、发布资源或重用native对象。当前五后端决定为：Null validator-only；Vulkan/Metal/WebGPU保留真实completion路线；WebGL拒绝真实复用，等待native custody。

P2/P3的真实授权必须是backend-private、不可由caller构造的单次lease，并由同一个registry消费；字段绑定原device owner、context epoch ticket、queue identity、resource ID与generation、pool lease generation、全部未issue引用和预算owner。lease不能序列化为整数或由intent重建。public层最多持有opaque中立policy/结果，不观察native字段。

- 发放前完整检查：同context/queue、无未issue引用、没有CPU readback/外部共享/alias heap/完成发布需求、没有lost/stale epoch，且实际执行顺序已固定。
- 消费时重新join确切issuer和resource generation；不匹配、重复消费、失效owner、计数溢出、pool地址复用、context restore或旧descriptor均拒绝，且不改变账本或native状态。
- lease与池槽位是独立identity。池内可保留同一个native allocation，但新logical borrower不能被旧command访问；若logical resource重建，通过现有allocateGpuResourceId取得新ID，不能复用GL名字作为L2 ID。
- 发放和消费的可知失败发生在副作用前；一旦某native调用已发生，状态记录不可伪回滚。失效context撤销全部旧lease，绝不生成completed成功。
- 原fence池入口继续拒绝null completion；不能将ordered lease传给readback、completion observer、跨队列wait或published completed-resource接口。

该授权的实现测试必须覆盖跨registry/queue/context、相同generation但不同owner、旧引用、double-consume、lost/restore和exact-budget反例。它们属于P2/P3真实payload验证；CPU eligibility测试不能代替。

## 计量与预算责任

| 计量 | 单位/事实源 | 计入与释放 | 不可混用 |
| --- | --- | --- | --- |
| Host CPU metadata / snapshots / reload | core allocator实际charge；caller给出的CpuMemoryQuota | prepare申请预留；实际最后owner释放；snapshot有效bytes另列 | quota可能共享，不称单对象独占；CPU consumed不是GPU completed |
| Logical buffer/texture payload | 精确descriptor的byte size，或受支持format/mip/layer/sample数学 | registry成功发布后计入；同一allocation只计一次；无record借用且逻辑owner最终退休后按exact记账释放 | 不报告为driver/native allocation；views/descriptor引用不重复计payload |
| Native/driver physical allocation | WebGL1无可靠查询，明确Unknown | 不填估算值、不在delete后报告精确回收时刻 | 要求精确物理预算/alias heap的L2 intent拒绝 |
| Object/command/reference counts | 显式caller limits，core bounded sequence/retainer/atomic capacity | 容量为0关闭类别，distinct owners和reference occurrences分别计数 | 不能用count推导byte成本或默认无限增长 |

复用core账本只复用算术，不继承其clamp释放为合法double-release。prepare先验证非零/overflow和剩余quota，再创建native对象；失败的候选要按其实际所有权清理，不发布dummy。动态resize/orphan、临时纹理复制、按失败自动扩容、无限shader/program cache均不作为预算恢复策略。

稳定集合的P3压力验证同时报告live wrappers、native names、logical bytes、core live/peak、cold create/delete次数、每帧upload/bridge调用和拒绝统计；保持同工作负载规模，比较随帧数的变化。driver内部排队、隐式renaming和物理内存不可观测部分明确Unknown；本profile不保证不可观测的物理峰值。逻辑预算耗尽确定拒绝，新一轮仅在真实owner退休或caller显式调整声明后重试，不能自动扩大预算。

## WebGlMemoryBudget/1：P0-14的可执行preflight定义

本节只定义并验证预算意图，不签发native allocation、释放或复用权限。新增八个独立的uint64容量维度：cpuMetadataBytes、cpuUploadBytes、cpuReloadBytes、logicalBufferBytes、logicalTextureBytes、objectCount、commandCount、referenceCount。三个CPU维度按其原始core quota/ownership计量，不能重复把共享owner计作三份实际分配；逻辑buffer/texture计量按完整descriptor和受支持格式的确定性footprint，view/descriptor不重复计payload；后三项是计数，不是字节。WebGL未知的driver physical bytes不进入这些结构，也不由任何维度推算。

`WebGlMemoryBudget/1`零容量关闭该类别，UINT64_MAX只是caller显式容量，既不代表查询值也不代表无限；fixed policy不能隐式增长。`WebGlMemoryRequest/1`声明新增量及requiresNativePhysicalBound。preflight先验证版本，再拒绝精确native physical bound，检查每个live值不超过对应capacity，再使用core::MemoryBudgetLedger::canAdd验证加法溢出和live+新增量的容量边界。全部维度先通过才算compatible，没有部分预留。零新增量可作纯观察；不能据此发布零尺寸资源成功。此函数不修改账本、申请内存或改变capability，executable始终false。

live值在P2/P3只能由原始backend registry/CPU quota读出；公开value-only live输入不是认证。生产context/device在cold create绑定固定预算和所有权，prepare只能消费同一个policy，不能每draw传另一个更大budget绕过容量。identity文本也计入cpuMetadataBytes；host upload/reload副本必须按实际last owner保持charge。资源和命令preflight成功之后仍须真实quota预留、descriptor/usage验证及native创建；失败不得发布对象，不能用统计快照代替payload。逻辑资源只有其retained allocation owner最终退休才释放相应logical charge，未issue引用阻止退休；delete只能证明逻辑API所有权变化，不证明driver物理回收时间。

`MemoryStats`和`PhysicalBudgetLedger/2`保留既有四backend语义。WebGL未来必须另外显式报告以上logical/CPU维度，native driver physical observation保持Unknown；禁止将logicalResourceBytes写成已查询的native physical allocation。在没有真实WebGL observer时所有原接口保持Unknown/未暴露，不能制造Observed零或借用Null计数。要求精确native物理预算、heap aliasing、原生map/readback或GPU completion的请求继续走各自稳定拒绝，不从本preflight获得权限。

五backend：Null只验证value-only测试输入；Vulkan/Metal/WebGPU继续使用原始allocation/retention/physical-budget契约，不自动改用WebGL逻辑计量；WebGL仅获得预算相容性结论，execution为DiagnosticFailure直到P2/P3实现同源registry及实际charge。稳定诊断RHI-WEBGL1-MEMORY-：BUDGET-VERSION、REQUEST-VERSION、PHYSICAL-BOUND-UNAVAILABLE、LIVE-EXCEEDS-CAPACITY、OVERFLOW、CAPACITY。all-zero/exact/one-over/one-less/live非法/溢出/物理约束和无分配preflight必须有快速测试；Null smoke保留。P3才验收真实稳定集合、退还/双释放拒绝和内存曲线，P0不虚报其完成。

## Loss、shutdown 与验证入口

context loss先阻止新record/issue/present并撤销epoch，再按原ownership取消未issue工作、清理CPU引用与逻辑账本。对丢失context不发起指向新context的旧delete调用；restore重新query并创建新generation，旧command、descriptor、binding cache和lease全拒绝。释放逻辑账本不表示GPU工作完成或驱动内存已释放。

shutdown区别host cleanup和真实GPU drain；无可证明的deadline drain继续输出UNAVAILABLE。热路径不得加入finish/readPixels、等待、shader编译、location/limit查询、文件或网络IO。冷readPixels只作验收oracle，不提升生产readback能力。

当前快速入口：`arp_rhi_webgl_profile_contract_tests`、`arp_rhi_ordered_issue_contract_tests`、`arp_rhi_webgl_resource_lifetime_contract_tests`、`arp_rhi_webgl_descriptor_lowering_contract_tests`；smoke：`arp_rhi_null_hidden_frame_smoke_tests`。真实browser /2 probe与RenderDoc路径见验证索引。P2/P3须新增实际registry/lease/delete/loss测试及真实smoke，满足后才可声明Backend Validated。
