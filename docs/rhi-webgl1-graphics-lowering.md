# WebGl1GraphicsLowering/1

日期：2026-10-17。定义版本：1；验收状态和证据见 rhi-webgl1-validation.md。
这是 L2 intent 到执行计划的版本化规则；不发布 DeviceCapabilities，不签发执行凭据。
当前 WebGL 创建、pipeline、record/submit 仍为 DiagnosticFailure。下述目标映射的实现和
真实测试分别属于 P1～P4，不能由本文件存在而宣称 Backend Validated。

## 1. 判定规则

输入依次为完整 L2 descriptor/command、不可变 shader artifact/reflection、原 device/context
查询、调用方有限预算、原资源身份/generation/初始化与命令引用。前一步失败不得用后一步
的字段修补。未知 enum、未知版本、缺字段/查询/目标产物/实际 payload 均确定拒绝。

矩阵中 V=Equivalent 的 Null validator-only；N=满足既有 contract 和实际 capability/payload
时 Native，否则 DiagnosticFailure；E=满足本文件逐行前提和实际 payload 时 Equivalent，
否则 DiagnosticFailure；F=DiagnosticFailure。N/E 不是 backend 名称推导的能力。
Degraded 仅能表示 capability=false，本域没有允许继续执行的 Degraded 路线。

WebGL 列是**目标规则**，其当前执行结果全部为 F。禁止以目标 E/N、CPU compatible 或
profile/扩展查询通过直接开放 record/submit。其它 backend 列沿用其现有独立契约，
不将 WebGL 限制反向裁剪其它 backend；既有特殊 original/prototype 路线按原版本拒绝。

所有准入检查在 issue 前完成；执行计划不可变并由 executor 消费。原对象变化、loss、
generation 失效会撤销计划，不重选 fallback。发生部分 issue 后的失败保留已接受工作范围，
只清理尚未消费的 host 数据，不宣称回滚原生命令或 GPU completion。

## 2. 逐操作五 backend 映射

| ID / L2 intent | Null | Vulkan | Metal | WebGPU | WebGL1 目标与精确前提 |
| --- | --- | --- | --- | --- | --- |
| R01 vertex/index buffer | V | N buffer/bind | N buffer/bind | N buffer/bind | N GL buffer；明确 VertexBuffer/IndexBuffer usage、有限 size、字节边界和逻辑 identity；不含 storage/indirect/readback |
| R02 CPU uniform-only buffer | V | N UBO | N buffer | N uniform buffer | E CPU snapshot→反射 leaf uniforms；只允许可追踪 CPU 内容，无 GPU 写者、无原生 map/coherent 语义；见 §3 |
| R03 RGBA8 Texture2D | V | N image/view | N texture/view | N texture/view | N RGBA/UNSIGNED_BYTE；单层、单采样、非 cube，按用途查询 sampleable/renderable/filterable，mip 完整与初始化独立证明 |
| R04 depth/stencil renderbuffer | V | N exact format | N exact format | N exact format | N 精确 Depth24Stencil8，消费实际 24/8 格式事实；缺失则 F，不以 D16/D32 替换 |
| R05 sampled depth view | V | N depth view | N depth view | N depth sample type | N 仅显式 depth-texture 扩展及匹配 format/type；无 compare sampler；缺少独立 artifact/格式证明则 F |
| R06 sampler | V | N sampler | N sampler | N sampler | E texture object 参数计划；同 draw 同一 texture 的参数必须一致，NPOT/完整 mip/过滤条件见 §3；不能 clone texture 隐藏冲突 |
| R07 resource memory intent | V | N queried allocation | N storage mode | E declared usage/map | E GpuOnly 或明确 CPU upload 快照；GpuToCpu、精确物理 placement/budget、host coherence、alias heap 均 F；Transient 仅逻辑短寿命且独立 owned allocation，不能证明 alias |
| D01 sampled texture+sampler descriptor | V | N set | N reflected slots | N bind group | E immutable reflection 的 set/binding→sampler pair→unit；实际 texture/sampler 与 stages 精确匹配，冲突/缺失 F |
| D02 uniform descriptor | V | N UBO range | N buffer range | N uniform range | E 按反射 offset/stride/type 解码 CPU snapshot；range/dynamic offset 在复制前校验，不把任意 GPU buffer 当 uniform 内容 |
| D03 descriptor rewrite/reset | V | N original lifetime gate | N original lifetime gate | N original lifetime gate | E 新 revision 不改已录制 draw；未 issued 引用继续持有旧快照及资源；无空间则 F，不等待或静默覆盖 |
| D04 storage/table/input attachment | V | N 原独立 contract | N/E 原独立 contract | N/E 原独立 contract | F；不得因 sampled texture 支持而把 input attachment/subpass/table 隐式改为普通采样 |
| U01 AtomicFullBlockPush | V | N push constants | N/E reflected setBytes | E reflected bounded ring | E offset=0、完整块、stage/size 精确匹配；record 时复制，逐 draw 对应 program uniforms；受 L2 ceiling 与实际 vectors/caller budget 双约束 |
| U02 uniform leaf/matrix | V | N reflected layout | N reflected layout | N reflected layout | E 仅 P1 明确定义的 leaf 类型/数组/矩阵 ABI；不支持类型 F；CPU row-major、Slang 默认矩阵、显式编译布局及非对称数值探针；GL transpose=false 的转换由反射计划确定 |
| G01 vertex stream | V | N attributes/bindings | N reflected slots | N vertex layouts | N Float32x1～4、显式 location/stride/offset/buffer span，与 descriptor/push 槽独立；缺 stream F，禁止用 VertexID fixture 补 geometry |
| G02 draw | V | N direct draw | N direct draw | N direct draw | N TriangleList、instanceCount=1、非零 count、firstVertex/GLsizei 有界，vertex fetch 完整边界证明；其它 topology 本版本 F |
| G03 drawIndexed | V | N indexed draw | N indexed draw | N indexed draw | N Uint16、对齐/offset/count/index range/vertex fetch 完整证明；baseVertex/firstInstance/restart F；不自动将 Uint32 mesh 拆成 Uint16 |
| G04 Uint32/instancing/VAO | V | N 既有合同 | N 既有合同 | N 既有合同 | N 仅 available+enabled+显式 opt-in、正确扩展入口和 payload；VAO 只保存已验证的流，不增加其它能力 |
| G05 compute/indirect/secondary/query | V | N/E 既有逐项合同 | N/E 既有逐项合同 | N/E/F 既有逐项合同 | F；无 CPUReplay、Dummy、counter 模拟或 timestamp 替代 |
| P01 graphics program | V | N SPIR-V pipeline | N MSL pipeline | N WGSL pipeline | N 生产 ES100 VS/FS 离线包；完整 source/target/entry/reflection/hash 身份；真实 compile/link 和 active interface 检查只在冷创建；ProbeOnly F |
| P02 raster/front face/cull | V | N explicit state | N explicit state | N explicit state | N 逐字段显式设置；坐标/orientation lowering 由 P1 版本化计划消费，不能从 backend 名称额外翻转；缺失/非法值 F |
| P03 blend/write mask | V | N explicit state | N explicit state | N explicit state | N 单目标 Add/Subtract/ReverseSubtract、原 BlendFactor、独立 color/alpha 与 write mask；Min/Max 本版 F，禁止静默替换；alpha/colorspace 必须匹配 target |
| P04 depth/stencil | V | N explicit state | N explicit state | N explicit state | N compare/write、front/back ops、masks、动态 reference 全量来自 L2；精确附件匹配；depth bias 本版本 F；read-only attachment 未有完整 proof 则 F |
| P05 sample/resolve/layer/subpass | V | N queried contract | N/E queried contract | N/E queried contract | N sampleCount=1、单层、subpass0、无 resolve、无 alpha-to-coverage；改变可见结果的 sample mask、多采样、layered 和多 subpass 全 F |
| P06 viewport/scissor | V | N static/dynamic | N encoder state | N encoder state | E 原 L2 viewport/scissor→GL 显式整数/浮点范围及原点映射；有限值、bounds、depth range 与动态状态必须完整，不能沿用前 pipeline 状态 |
| A01 color/depth attachments | V | N pass/image views | N pass/texture views | N pass/views | N 一颜色 RGBA8+可选精确 D24S8，FBO 完整且与 pipeline 的 format/sample/extent/aspect 匹配；无 active sampled feedback；canvas 使用独立租约 |
| A02 clear | V | N attachment clear | N load-action clear | N load-op clear | E 明确 attachment/subresource/clear value；执行前设置完整 clear masks/scissor，使语义覆盖整个目标；随后恢复计划中的 draw state，不能依赖旧 GL mask/scissor |
| A03 Load/Store/DontCare | V | N explicit ops | N explicit ops | N explicit ops | E 离屏 Load 要求已初始化、Store 保留内容；DontCare 使逻辑内容无效且禁止后续 read，允许物理内容仍存在；canvas 跨帧 Load F，不因 preserveDrawingBuffer 推断已初始化 |
| A04 attachment→sampled dependency | V | N stage/access barrier | E ordered encoder/resource | E ordered pass/usage | E 同 context 单 queue、结束写 pass 后按明确读依赖访问，禁止 feedback/跨 queue/alias/input attachment；无显式 GL barrier 不等于无状态/lifetime 验证 |
| T01 CPU buffer upload | V | N staging/transfer | N staging/blit | N write/staging | E bounded record snapshot→bufferData/SubData client-copy；目标 range/usage/初始化/revision 明确；桥实际返回且无未 issue 引用后才释放 host 源，不代表 GPU 完成 |
| T02 CPU texture upload | V | N staging/copy | N staging/blit | N write/staging | E RGBA8 明确 mip/region/row pitch/byte span→texImage/SubImage2D；pixel-store 显式计划，必要 repack 用既有有界工具；无猜测 flip/premultiply/colorspace，CopyDst 不由 texture() 自动准入 |
| T03 GPU copy/readback/publication | V | N 既有 completion 合同 | N 既有 completion 合同 | N/E 既有 completion 合同 | F；T01/T02 的 client-copy 返回不能满足 copy command、readPixels 生产路径或 completed 内容授权 |
| S01 canvas acquire/present/discard | V | N original swapchain | N original drawable | N original canvas | E 同 context/generation 的单帧逻辑 lease；host Auto、无可控 image count；present 仅提交宿主，discard 不伪造 completion；零尺寸/丢失拒绝 acquire |
| S02 canvas attributes/alpha/color | V | N exact surface contract | N exact layer contract | N exact canvas contract | E 显式 context attributes 与实际查询逐项匹配；精确 drawing-buffer format/color contract 缺证据 F；不能仅凭 alpha=false 猜 RGB/RGBA 转换或 sRGB |
| S03 resize/hide/pause/restore | V | N 既有 lifecycle | N 既有 lifecycle | N 既有 lifecycle | E surface revision/generation 绑定；零尺寸不造1×1、隐藏不空转；loss 撤销全部旧授权，restore 重查/重建，首帧初始化；无 reload recipe F |
| L01 recorded/issued custody | V | N original completion owners | N original completion owners | N original completion owners | E backend 有界保留所有未 issue 引用与 snapshots；Issued/Accepted、Failed/Lost 分开，取消仅释放未 issue 消费者，状态机见原 OrderedCommandIssue/1 |
| L02 unbind/delete/object retirement | V | N exact completion contract | N exact completion contract | N exact completion contract | E 对象类型专用的解绑/attachment/detach/delete 与引用规则；逻辑 tombstone/generation 防 ABA；不宣称驱动物理释放、完成时间或 completion serial |
| L03 same-context ordered reuse | V | F 保留原 completion 路线 | F 保留原 completion 路线 | F 保留原 completion 路线 | E 仅真实 backend registry/queue 签发并消费的 lease，全部未 issue 引用已处理、同 epoch/queue/order；CPU eligibility/调用方 bool 不能授权 |
| Y01 fence/timeline/completed wait | V | N actual primitive | N/E actual completion | N/E actual completion | F；flush、JS return、Issued、delete、CPU signal 均不能满足 GPU completion |
| Y02 shutdown deadline drain | V | N/E/F original drain | N/E/F original drain | N/E/F original drain | F 对真实 GPU deadline drain；可执行 host cleanup 只报告取消/释放自身 owners，不能写 Completed |

其它 L2 operation 均不在 WebGL 本版本域内，默认 F。上表不为原 native backend 新增能力；
尤其 N/E/F 条目的结果必须继续消费各自 query、版本、explicit opt-in 和 payload，不能以
本表代替其 contract decision。Null 的 V 不运行任何 GL，也不产生可提交的 native payload。

## 3. 必需绑定与预算

texture()/sampledBindings() 是 shape/usage/format/采样参数的 CPU 请求检查，**不检查完整
MemoryUsage/initialState、资源来源、内容、pipeline 或 execution plan**。P2 的资源创建和
执行 gate 必须另查 R07/A03/L01；任何调用方不能以 compatible=true 跳过。纹理的 CopyDst
是独立 L2 意图，T02 只描述未来经独立上传 lowering 批准的路径，不重解释当前拒绝。

CPU uniform-only 路线只允许快照语义：源 buffer range、dynamic offset、内容 revision 和
shader 反射 leaf 可确定；不可映射 GPU 写内容、persistent map、共享内存或 native UBO handle。
缺少 CPU-authoritative 数据就 F。record 保存 draw 所需字节和资源 revision，issue 前不读
调用者可变地址；GL locations 仅冷 link 后解析，按 program generation 缓存，不用名字猜绑定。

sampler pair 来自实际 shader 反射，unit 分配受 vertex/fragment/combined 三类 queried limits
和 caller ceiling 约束；同 texture 多个 unit 不能同时设置不同 texture parameters。label 不参与
语义；差异不能通过修改另一个 draw 的已封存快照解决。NPOT 要求 clamp/no mip，mip 采样
要求完整且初始化的 mip 链。比较采样、anisotropy>1 和未定义 enum 本版 F。

command 数、独立 owners、引用出现次数、snapshot/upload/uniform bytes、对象/缓存和重建
副本分别设有限上界并用 core allocator 计费；0 关闭类别，不表示无限。物理 GPU bytes 不可
查询时标 Unknown；要求精确物理上界、native heap 或跨资源 alias 的 intent 直接 F。
失败不得临时无界扩容、per-draw 创建 pipeline、每帧编译/query/location 或阻塞等待。

## 4. 诊断、恢复与验收责任

稳定 profile/request 诊断、失败阶段、副作用和恢复条件见 rhi-webgl1-diagnostics.yaml；
原 lowering 的专用诊断保留，不改为 generic success。P1/P2 预留诊断只定义未来失败契约。
恢复是调用方显式修正 descriptor/预算/工作负载，或以新 generation 重查重建，绝非自动重试。

| 责任 | 必须验收的结果 |
| --- | --- |
| P0-07 定义 | 上述八个领域逐项规则、五 backend 条件结果、未知/缺失输入默认拒绝、诊断/预算/寿命边界；快速 request/原 lowering 负向测试及 Null smoke |
| P1 | 生产 shader ABI、uniform leaf、矩阵/坐标/precision 数值测试；所有 E 的 shader 转换必须由实际 artifact/reflection 证明 |
| P2 | 原 context/device/resources 与完整执行计划消费，真实单帧 draw、状态/上传/附件与拒绝副作用测试，AI RenderDoc 捕获 |
| P3 | 真实 lease、预算峰值/热路径、delete/loss/restore/resize/取消压力测试；不能用固定延迟帧数代替授权 |
| P4 | 同一 L2 上层入口集成、实际 renderer 工作负载、目标平台回归、RenderDoc 与 Release 性能 |

完成 P0 定义不改变当前五 backend 运行状态，也不关闭 P0-01 的全使用点审计、P0-04 的
ABI/schema 审查、P0-09 的全部执行 gate 审查或 P0-10 的既有 completion 消费者缺口。
