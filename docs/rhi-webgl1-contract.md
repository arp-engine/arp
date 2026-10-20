# WebGl1Graphics/1

领域定义和执行门禁见本文，最新阶段验收与状态以 `rhi-webgl1-validation.md` 为准。
P0 定义验收与生产 WebGL 实现、Backend/Platform Validated、Production Ready 分开计量；
completion 消费者的当前矩阵见 `rhi-completion-consumer-audit.md`。

## 1. 领域边界和事实来源

### Graphics request scope/1：本批实施前契约

本版本固定所需域：传统绑定、VS/FS、显式vertex stream、draw/drawIndexed、RGBA8 Texture2D、
单颜色附件、可选精确D24S8 renderbuffer、普通uniform/push的离线映射、有序单context访问。
Uint32 index、instancing、VAO、depth texture、derivatives是独立显式扩展域。Compute、SSBO、
storage image、indirect、bindless、alias heap、GPU completion、production readback、MRT、
多采样及可控native swapchain policy不属于Graphics/1；不得因profile兼容而启用。

`WebGlGraphicsRequestContract/1`复用immutable runtime profile、原L2 TextureDesc/SamplerDesc/
IndexFormat/PresentationMode、core unsigned range和ArrayView；只验证CPU请求语义，不认证
资源对象、反射、初始化、descriptor revision、context epoch或执行payload。所有返回值
executable=false。P1负责uniform leaf/布局，P2负责真实资源与单帧执行，P3负责托管/复用，
P4负责上层集成；这些实现不得再成为P0领域定义的循环前置。

请求不变量：版本与profile先验证；Texture2D非零、depth/layers/sample均为1，mip层数不能
超过完整链；usage只准入Sampled/RenderAttachment，CopyDst也不得借client-upload含义自动准入。
格式事实按用途分别消费。NPOT仅ClampToEdge且无mip；mip采样要求完整链；compare与anisotropy
无本版本lowering，明确拒绝。同一次draw对同一逻辑texture identity的sampler参数必须一致，
比较所有有语义的SamplerDesc字段，label不参与；绑定跨度受实际combined units约束，空指针、
超容量先拒绝。该比较只验证调用方元数据，不代替P2的原对象来源认证。

draw只准入TriangleList、显式非零count/instance，count与firstVertex受GLint/GLsizei范围约束；
indexed使用Uint16，或通过available+enabled+opt-in的Uint32；index字节范围复用core检查。
baseVertex、firstInstance、primitive restart拒绝；instanceCount>1必须独立instancing opt-in。
不验证尚未绑定的vertex fetch，也不偷偷重写mesh。Canvas仅PresentationMode::Auto的宿主调度，
imageCount必须为0（明确表示无可控native image count），禁止隐式跨帧Load；实际context属性
匹配仍由原admitContext验证，默认FBO format/颜色空间/租约由P2独立绑定。

五backend结论：Null只运行这些CPU validator fixtures；Vulkan/Metal/WebGPU不消费WebGL请求
规则，保留各自Native/Equivalent/DiagnosticFailure及原payload门禁；WebGL请求兼容是CPU
planning结果，当前实际执行为DiagnosticFailure。不存在由backend名称签发的执行权限。

诊断目录必须逐项列ID、失败阶段、原生副作用边界、恢复条件；缺少unsupported feature、
extension、uniform type/limit、sampler conflict、NPOT、format/usage、stale generation、loss、
budget、completion或presentation policy任何一类都不能关闭P0-08。请求校验是零native副作用；
P2执行中OOM/loss可能已经issue部分命令，必须保留accepted/possible-side-effect，不能按该
目录的preflight条目宣称回滚。未知诊断ID不猜测阶段或自动重试。

稳定ID、phase、sideEffects、recovery和implementation status的完整profile目录见
`rhi-webgl1-diagnostics.yaml`。ImplementedAdmission条目必须与六份WebGL semantics源中的
实际ID集合精确相等；shared stale-context复用原OrderedResourceReuse；ReservedP1/P2只是
提前固定的失败契约，不能被当成相应shader/native路径已经实现。整个P0-08可以以目录、
禁止隐式重试和请求负向验证验收；P1/P2仍必须在真实失败点发出预留诊断并独立验证。

身份兼容边界：Backend现显式固定Null=0/Vulkan=1/Metal=2/WebGPU=3/WebGL=4；
BackendPreference保留Default=0/Vulkan=1/Metal=2/WebGPU=3/Null=4/WebGL=5。两者不能互cast。
新WebGlGraphicsOperation只是本版本内的请求标签，不是持久化枚举；禁止用其ordinal写资源。
既有SceneRuntime四槽Legacy/V2五槽、UI五报告、Shader TargetBackend和历史四键publication
继续遵守各自独立schema；本增量不改变这些对象的layout或替代P0-01/04的全使用点审查。

WebGL 是第五个独立 backend，已有 Backend ordinal 0～3 不变，追加 WebGL=4。L2 公共头继续位于 `src/rhi/gpu_*.h`；frontend 承担中立验证、命令模型、预算和寿命；semantics 决定版本化 lowering；`backends/webgl` 才能持有 GL context、对象、location、native state 和调用桥。

本版本是传统资源绑定的 graphics profile，不是 WebGL2 模拟器。Compute、SSBO、storage image、indirect、bindless、native heap aliasing、GPU completion、production readback 均不由本 profile 提供。任何 Optional/Extension 的额外能力必须有独立 query、opt-in、lowering、实际 payload 和双侧测试。

规范依据：

- [WebGL 1.0 specification](https://registry.khronos.org/webgl/specs/latest/1.0/)：context、extensions、资源合法性、安全与初始化要求、context loss。
- [OpenGL ES 2.0 specification](https://registry.khronos.org/OpenGL/specs/es/2.0/es_full_spec_2.0.pdf)：命令顺序、buffer/texture 数据传入、delete、状态与限制。
- [GLSL ES 1.00 specification](https://registry.khronos.org/OpenGL/specs/es/2.0/GLSL_ES_Specification_1.00.pdf)：语言、precision、uniform/varying 和 texture 操作。
- [Emscripten WebGL API](https://emscripten.org/docs/api_reference/html5.h.html#webgl-context)：context attributes、显式扩展启用、lost/restore 和 current context。桥行为仍需对实际工具版本和生成 JS 验证，文档不能替代 payload 证据。

native query 得到的是能力事实，不是执行授权。规范保证、query 结果、调用方预算与当前已实现能力分别保存，不能以设备名、浏览器名、OS 或 Backend enum 推断。

### 1.1 RenderSystemBackendSelection/1：身份与创建路由

`BackendPreference` 的旧值 Default=0、Vulkan=1、Metal=2、WebGPU=3、Null=4 保持不变，追加 WebGL=5。它与 RHI `Backend` 是不同枚举，禁止按整数 cast 转换。由 platform 层的单一 selection contract 显式解析请求和本构建提供的 factory；它不推断 GPU 能力，不替代 device/profile/payload validation。既有 renderer deployment candidate/readiness policy 处理的是运行候选与能力，不适合替代此 factory 身份校验。

- Default 只表示允许本构建的明确 factory；Vulkan/Metal/WebGPU 显式请求只允许匹配 factory，不隐式换后端。Null 尚无 RenderSystem 呈现 factory，明确拒绝。
- WebGL 身份已可表达，但生产 factory 尚不存在：在任何 surface/device/context 创建之前返回 `RHI-WEBGL1-RENDERSYSTEM-UNAVAILABLE`。即使将 WebGL 作为 compiled-backend 参数也不产生可执行权限，P2/P4 接入后须另行更新本版本化决定和测试。
- 未知 preference 返回 `RHI-PLATFORM-BACKEND-PREFERENCE-UNKNOWN`；未知 compiled backend 返回 `RHI-PLATFORM-BACKEND-BUILD-UNKNOWN`。均不进入 default factory。非法 RenderSystem 的既有公开 `backend()` 仍为 Null sentinel，不把它解释为实际创建了 Null backend；请求身份只在 selection 结果中保存。
- 五后端决定：Null DiagnosticFailure（无呈现 factory）；Vulkan/Metal/WebGPU 仅允许路由各自已编译 factory，是否可执行继续由各自 native gates 决定；WebGL DiagnosticFailure（factory 未实现）。selection 成功不是 Native/Equivalent GPU execution 成功。
- selection 无动态分配，无 surface/native API 副作用。拒绝结果使用 core allocator 创建持有诊断的 RenderSystem implementation；既有 teardown/move 生命周期保持。若将旧默认 `std::unique_ptr<Impl>` 迁移为 `core::UniquePtr<Impl>`，公开方法源码兼容，但类对象布局改变，所有消费该头的引擎/应用必须同版本重编，不能宣称二进制 ABI 不变。

本节在实现前登记；快速测试须覆盖完整 preference×五后端矩阵、未知值和旧 ordinal，真实 factory contract 须证明无 surface 的未知/WebGL 请求也返回上述明确诊断。Metal/浏览器平台上的共用 contract 通过不替代其 native platform smoke。

### 1.2 SemanticReportBackendAdmission/1 与五后端报告

本契约只准入CPU结构检查，不是DeviceCapabilities、native payload、conformance或执行授权。已知身份按固定顺序Null/Vulkan/Metal/WebGPU/WebGL枚举；顺序独立于枚举ordinal运算。错误版本首先返回`RHI-SEMANTIC-REPORT-CONTRACT-VERSION`，未知身份返回`RHI-SEMANTIC-REPORT-BACKEND-UNKNOWN`，WebGL返回`RHI-WEBGL1-SEMANTIC-REPORT-UNAVAILABLE`。所有决定的executable恒false；身份/版本检查无分配、无native API副作用。

| backend | 结构报告准入 | GPU结论 |
| --- | --- | --- |
| Null | 允许现有validator检查，validatorOnly=true | 不证明native执行 |
| Vulkan/Metal/WebGPU | 继续现有结构/工作负载检查；push事实消费既有lowering，失败处理按1.3收紧 | 报告不提供Native/Equivalent执行，仍需真实device/payload门禁 |
| WebGL | 结构报告DiagnosticFailure，含零push情形 | 未实现路径明确拒绝，不能由普通workload字段变成Supported |
| unknown | DiagnosticFailure | 不授权任何执行 |

`SceneRuntimePipelineRhiParityResult`及原函数保留Legacy/1四槽布局和原四backend顺序。新增`SceneRuntimePipelineRhiParityResultV2`与`verifySceneRuntimePipelineRhiParityV2`，提供版本2五项结果；V2是唯一计算实现，旧函数仅移动公共字段并投影前四项。两者的success只表示CPU plan/bridge结构校验通过，不证明任何backend native工作执行或视觉parity。不得将旧四项投影用于五后端完整性声明；新增调用者使用V2。旧公开类型不添加/重排字段，新增入口不承诺旧二进制能够识别V2。

`UiRhiBackendContract/2`保持原report/result对象布局，现有三个合法atlas policy均显式返回五项；Null仅validator，原三native backend诊断保持，WebGL有上述稳定拒绝。无效policy不得按SingleTexture处理：lowering=Unspecified，所有backend拒绝并给出`RHI-UI-SEMANTIC-REPORT-POLICY-UNKNOWN`。UI报告仍不作为执行授权，readback/staging字段描述意图，不能替代实际能力。

快速测试须覆盖五身份/unknown/错版本、无分配身份检查、三种合法及非法policy、零/非零push的WebGL拒绝、原四backend结果、V2/Legacy投影、旧字段布局以及失败时无报告发布。对应Null hidden-frame保留；本增量无shader/可见输出变更，不用CPU报告代替RenderDoc或平台证据。

### 1.3 SceneRuntimeIndexedDrawMetadata/1：显式命令参数

现有SceneRuntime frame只拥有队列/资源计数，不能推导indexCount、instanceCount、firstIndex、vertexOffset或firstInstance。新增独立value-only输入`SceneRuntimeIndexedDrawMetadata`，包含版本1和借用的`core::ArrayView<RhiDrawIndexedCommand>`；不改变既有request、payload、result或planner对象布局。每个frame draw item对应一条调用方显式声明的indexed command，顺序与frame队列顺序一致；其它draw类型或拆批必须由后续独立lowering契约表达，不由本入口猜测。

planner新增双参数plan(request,metadata)，先保留原frame/预算/多pass验证，再校验metadata版本、缺失、存储及与drawItemCount的精确数量关系。稳定诊断分别为`RHI-SCENE-DRAW-METADATA-VERSION`、`RHI-SCENE-DRAW-METADATA-MISSING`、`RHI-SCENE-DRAW-METADATA-STORAGE`、`RHI-SCENE-DRAW-METADATA-COUNT`。所有检查在解引用/复制span前完成；数量受现有maxDrawCommandCount约束。成功时用core::pmr容器复制五个参数的快照，不保留借用pointer；调用方返回后修改/销毁原数组不影响plan。命令合法性继续由现有RhiFramePlanBuilder验证，不复制一套validator。

五后端共同消费相同中立元数据和验证规则：Null仅validator；Vulkan/Metal/WebGPU仍由各自真实resource/pipeline/command gate决定执行；WebGL保持DiagnosticFailure。index参数的结构有效不证明buffer范围、资源绑定、artifact、native payload、GPU执行或可见parity，资源解析与实际executor仍是P2/P4必交付项。

V2 parity和Legacy四槽投影各增加带metadata的重载；原四参数符号保留并以空metadata显式转发，非空frame得到MISSING且不发布backendReports，禁止按计数填充虚假draw。原单参数planner仍是已有CPU需求规划入口，不授权执行；其计数plan直接传给bridge依旧拒绝。成功五报告只能来自带显式metadata的重载和未改动的bridge验证。

测试必须覆盖版本优先级、null非空span、缺失/少/多命令、预算、保留非默认全部五参数、源覆盖及源销毁、非法零index/instance由原bridge拒绝、旧入口确定拒绝、五报告及原四投影一致。跨TU冻结旧result声明只验证当前MinGW/同版本依赖的返回对象互操作，不声明历史DLL或其它编译器兼容。

本增量同时修正旧报告的push准入漏洞：过去只拒绝Emulated，会将DiagnosticFailure/Degraded/ExtensionOptIn等错误计为Supported。非零push必须消费明确Native决策及对应hasExecutablePayload；Null由validatorOnly身份维持纯结构验证。非Null无法证明时返回`RHI-SCENE-PUSH-LOWERING-UNAVAILABLE`。当前通用Vulkan contract缺少必须的设备查询、WebGPU缺少runtime limits，均拒绝；Metal现有setBytes规范契约给出Native结构结论，仍不证明在Windows上执行Metal。不能沿用“Metal必为Emulated”的旧假设。零push不需要此能力，WebGL仍由前置身份门禁拒绝。这改变的是错误的CPU支持结论，不改变任何backend实际能力或执行路径。

## 2. Profile admission 与扩展

`WebGlRuntimeProbe → immutable WebGlRuntimeProfile → admit(requirements, optIn)`：

1. context 必须创建成功、实际 major=1、未 lost；WebGL2 不能冒充本期 WebGL1 验证。
2. limits、precision、extensions 必须分别已查询。缺失值不使用生产默认值。
3. 最低规范域为：attributes≥8、vertex uniform vectors≥128、fragment uniform vectors≥16、varying vectors≥8、fragment/combined texture units≥8、texture2D size≥64、cube size≥16。vertex texture units=0 合法。renderbuffer/viewport 必须有有效非零查询值；具体资源仍逐格式、usage、extent 验证。
4. workload 独立声明 attribute/uniform/varying/stage texture/combined texture/extent/precision 需求。combined texture 消耗按两个 shader stage 的活跃 sampler 使用求和；同一 unit 在两个 stage 使用仍各计一次。使用宽整数验证，不允许溢出后通过。
5. 扩展事实分为 available、enabled 与 caller opt-in。`WebGlExtensionOptIn/1` 默认未选择任何扩展；required extension 必须同时满足三者。仅查询到扩展名称不调用 GL 扩展入口，也不修改基础 capability。
6. enabled 必须是 available 的子集；启用事实必须属于当前 context generation。恢复时不得复用旧 probe 或 enabled 集合。
7. 当前 P0 admission 无论 compatible 为何，`executable=false`。P2 只能以完成测试的 backend payload 和版本化决策逐项开启受限执行，不能把 admit 的 compatible 直接复制到 graphics/nativeSync。

现有 profile 的失败诊断包括 `RHI-WEBGL1-CONTRACT-VERSION`、`CONTEXT-VERSION`、`CONTEXT-LOST`、`PROBE-INCOMPLETE`、`LIMIT-PROBE-INVALID`、`WORKLOAD-LIMIT`、`TEXTURE-UNIT-ACCOUNTING`、`FRAGMENT-PRECISION`；后几项均使用同一 `RHI-WEBGL1-` 前缀。

扩展诊断为 `RHI-WEBGL1-EXTENSION-UNAVAILABLE`、`EXTENSION-CONTRACT-VERSION`、`EXTENSION-OPT-IN-REQUIRED`、`EXTENSION-NOT-ENABLED` 和 `EXTENSION-PROBE-INCONSISTENT`。字段均为不可执行的诊断结果，不执行 try/fallback。

### 2.1 查询快照、context请求与caller ceilings

本节记录P0的schema2 limits/context检查点；当前`WebGlRuntimeProbe`已按2.2升级schema3，旧schema2确定拒绝，不能省略precision表。graphics领域版本仍为WebGl1Graphics/1。旧schema/缺失版本不以缺失字段默认为false来伪造查询。Probe追加contextAttributesQueried和六项实际context属性：alpha、depth、stencil、antialias、premultipliedAlpha、preserveDrawingBuffer。fromProbe必须分别验证limits/precision/extensions/context attributes已查询；profile拥有值拷贝，后续修改probe不影响profile。它仍无原生来源认证，只有未来backend能将查询与确切context epoch连接，compatible不授予执行。

`admitContext(WebGlContextRequest/1)`逐字段精确比较声明和实际值。请求版本缺失/错误返回CONTEXT-REQUEST-VERSION，任一不匹配返回CONTEXT-ATTRIBUTES-MISMATCH；不默认接受浏览器的antialias/depth/alpha降级，也不悄悄调整premultiplication或跨帧保留策略。context请求匹配也仍executable=false。颜色空间、默认framebuffer精确format/depth bits、canvas lease/present/loss等独立契约尚须补齐，六项匹配不代替它们。

`WebGlGraphicsRequirements`追加renderbufferWidth/Height与pushConstantSizeBytes。尺寸逐一与MAX_RENDERBUFFER_SIZE比较；零需求表示未使用该类别。push bytes先受已有L2 AtomicFullBlockPush 256B上限约束，不能因GL uniform查询更大而放宽；uniformVectors需求必须包含shader实际活跃uniform/push映射的总消耗，P1 binding plan负责证明，不能简单以pushBytes/16猜测占用。

`WebGlGraphicsBudget/1`保存caller ceilings，独立于native query；零ceiling关闭对应类别，不应用GL规范最低值，不以0代表无限。`admitWithin`首先检查有效profile与budget版本，再复用版本/查询限制/L2上限/扩展判定，最后检查caller ceilings。caller ceiling高于query值不会覆盖query，低于query时按更低预算拒绝；既无隐式clamp也无静默扩容。普通admit仍只是查询事实匹配，生产执行必须额外消费budget、完整shader/binding/payload gate，不能把普通admit当准入完成。

这十二项native limits来自GLint查询，必须处于有符号32位非负范围；转换成uint32后超过INT32_MAX的值（包括负查询值的wrap）返回LIMIT-PROBE-INVALID。caller ceilings不是GL查询，允许UINT32_MAX作为声明上限，但仍不能越过实际查询/L2上限。跨stage texture-unit求和使用uint64，不能以窄类型溢出掩盖超限。

新稳定诊断均以RHI-WEBGL1-开头：PROBE-VERSION、CONTEXT-REQUEST-VERSION、CONTEXT-ATTRIBUTES-MISMATCH、PUSH-L2-LIMIT、BUDGET-VERSION、BUDGET-LIMIT。非法probe/profile优先于请求；缺查询使用既有PROBE-INCOMPLETE。字段都是value-only metadata，不分配对象、不调用GL、不改变DeviceCapabilities。

| 本增量 | Null | Vulkan | Metal | WebGPU | WebGL |
| --- | --- | --- | --- | --- | --- |
| WebGL查询/context/ceiling admission | 仅CPU fixture/validator，不创建context | 不适用，保留原descriptor/query/payload契约 | 不适用，保留原契约 | 不适用，保留原契约，不借用WebGL事实 | 仅查询匹配；执行DiagnosticFailure，payload未实现 |

此表不裁剪原四backend能力，也不引入一个可由backend enum开启的新L2执行入口。附加的probe元数据改变C++对象布局，需要当前消费该Draft类型的代码同版本重编；不承诺旧binary ABI。

### 2.2 WebGlShaderPrecision/1：完整查询与需求

P0本增量将内部profile probe升级schema3（graphics领域版本仍为1）。新增`webgl_shader_precision.h`，用两组固定六项表保存vertex/fragment的LowFloat、MediumFloat、HighFloat、LowInt、MediumInt、HighInt。每项必须单独确认查询成功，保留有符号GLint的rangeMin、rangeMax和precision；缺项不以0补齐。依据[WebGL 1.0.3 precision interface](https://registry.khronos.org/webgl/specs/1.0.3/)及GLES2的getShaderPrecisionFormat语义，整数precision字段必须为0；零precision不能被误判成整数不支持。三个查询分量均不得为负；浮点支持依据正precision，整数支持依据非零range。全零记录是已查询但不可满足该类别需求的事实，不等同于查询缺失。

probe删除旧fragmentHighFloatPrecision标量，profile只从fragment HighFloat记录读取位数，避免两份事实漂移；原`WebGlGraphicsRequirements.fragmentHighFloatPrecision`仍是一个位数需求，0代表未使用该旧需求字段。新的`WebGlPrecisionRequirements/1`逐stage/类别声明required及三个最低分量；required=true即使三个最低值全为0，也要求该类别存在。未使用表允许version0且字段全零；非空需求必须version1，未required但夹带非零分量、负需求或整数precision需求非零均拒绝，不能静默忽略。

稳定诊断前缀RHI-WEBGL1-：PRECISION-PROBE-INCOMPLETE、PRECISION-PROBE-INVALID、PRECISION-REQUIREMENT-VERSION、PRECISION-REQUIREMENT-INVALID、PRECISION-UNSUPPORTED、PRECISION-INSUFFICIENT。先验证profile/schema及查询，再验证需求版本/结构，最后逐项比较。所有表由profile值拷贝持有；admission无动态分配且始终executable=false。P1必须由明确shader/package元数据提供precision需求，不能以mediump替换所需highp。

五backend决定：Null仅CPU validator fixture；Vulkan/Metal/WebGPU保留各自shader/native契约，不消费WebGL表；WebGL只做事实匹配，真实执行仍DiagnosticFailure。新增表不是DeviceCapabilities提升，不由backend名字推断。schema2不能默认升级；当前Draft类型的消费方必须同版本重编。浏览器原始查询报告是可行性证据，真实context身份、epoch认证和format事实仍属于P0-05/P2未完成项。

### 2.3 WebGlFormatFacts/1：用途和精确格式

本增量将内部profile probe升级schema4，新增不可变format facts。事实只覆盖明确的RGBA/UNSIGNED_BYTE Texture2D client upload、该texture的单颜色FBO完整性，以及packed depth-stencil renderbuffer的实际depth/stencil位数和与该颜色附件组合的FBO完整性。分别记录NotQueried、Observed、NativeFailure、OutOfMemory、ContextLost；后三种不能伪装成unsupported后继续准入。每项查询都必须完成；未知enum、负位数和格式事实版本不符拒绝。

`WebGlFormatRequest/1`显式声明L2 TextureFormat、用途和sampleCount=1。Rgba8Unorm的client upload与sampled用途需要上传路径验证成功，颜色附件另需FBO complete。Depth24Stencil8仅允许DepthStencilRenderbuffer用途，必须精确depthBits=24、stencilBits=8且组合FBO complete；16位或32位depth均不能替代24位。D32Float、BGRA、sRGB、浮点颜色、R8/RG、压缩等本期全部拒绝，不由扩展列表自动开放。这里的Sampled是格式合法性事实，不证明sampling shader或resource payload已实现。

用途不含通用GPU copy、readback、storage、multisample、default framebuffer或texture depth sampling。query只证明明确测试的格式组合；实际资源创建还须逐extent/mip/usage验证和FBO completeness检查，不以一个1x1测试承诺任意资源成功。原TextureUsage::CopyDst不能自动获得所有copy操作。getRenderbufferParameter的位数只属于被查询renderbuffer，不来自canvas depth属性或default framebuffer。

所有匹配仍executable=false；fromProbe只消费值快照，尚不认证native来源。Null只提供validator fixture；Vulkan/Metal/WebGPU保留各自原格式契约；WebGL仅做事实/请求匹配，执行DiagnosticFailure。稳定诊断前缀RHI-WEBGL1-FORMAT-，覆盖FACTS-VERSION、PROBE-INCOMPLETE、PROBE-INVALID、NATIVE-FAILURE、OUT-OF-MEMORY、CONTEXT-LOST、REQUEST-VERSION、ROLE、SAMPLE-COUNT、UNSUPPORTED。identity及context epoch绑定继续保持open。

`TextureFormat`的原始枚举定义独立于allocator/runtime，位于轻量公共头`gpu_texture_format.h`；原`gpu_common.h`继续包含它，保留原有include入口、枚举顺序、数值和底层类型。格式事实validator只依赖轻量头，不以链接allocator stub或禁用bootstrap绕过真实引擎分配入口。冷探针通过Emscripten的context-loss查询分类丢失；packed renderbuffer使用GLES2具名`GL_DEPTH_STENCIL_OES` token，由现有桥映射到WebGL1的DEPTH_STENCIL，不因此开启深度纹理扩展。

packed renderbuffer组合使用WebGL1具名`DEPTH_STENCIL_ATTACHMENT`（由实际context读取该常量），完整性查询的对象必须是绑定的非零FBO。没有创建成功的texture/FBO/renderbuffer不得继续其依赖操作；清理显式移除对应组合attachment再解绑/delete。depth/stencil两个独立attachment的另一条序列不是本组合的支持证明，其失败记录不能覆盖成成功。

### 2.4 WebGlRuntimeIdentity/1与context来源

本增量将内部profile probe升级schema5。身份事实记录五个独立queried字段：实际gl.VERSION、gl.SHADING_LANGUAGE_VERSION、gl.VENDOR、gl.RENDERER和navigator.userAgent。GL字符串来自当前context的查询；userAgent来自当前浏览器realm。它们仅用于诊断/证据追踪，严禁从vendor/renderer/UA猜测扩展、性能、OS能力或physical allocation。隐私屏蔽后的vendor/renderer值原样记录，不要求WEBGL_debug_renderer_info、不自动启用该扩展；已查询但为空的vendor/renderer/UA是明确的未知身份，不补默认名称。

`WebGlRuntimeIdentityContract/1`要求五项均查询完成，拒绝嵌入NUL的值；VERSION必须为WebGL 1.0，shader version必须为WebGL GLSL ES 1.0，允许规范中的空格分隔实现后缀，不接受WebGL2、伪前缀或缺失值。只读view用于瞬时验证；profile的probe以core::pmr::String按值持有五项原始字节，copy必须独立，不引用调用方临时string_view。身份存储属于cold metadata，不将cold copy算成零分配；所有admission路径仍不得动态分配。

context来源的唯一实施合同：P2的WebGlDevice拥有原始context、owner-thread和`core::LifetimeEpoch`，其backend-private capture在同一owner线程、同一原始context及同一current epoch中完成全部query；query前后验证context仍current、未lost且epoch未变化，任何失败丢弃整个候选profile。native-source绑定对象不可由public probe、名称、地址、generation数字或caller布尔值构造。profile身份文本与query结果不构成该绑定凭据。禁止在生产录制/执行入口接受单独`fromProbe`得到的profile。

身份文本的cold存储必须服从context/device metadata的显式core CPU quota；P2在query长度/总量溢出检查后才复制，预算不足或bad_alloc向外传播失败，不截断或发布部分profile。当前host test在1-byte显式quota下证明profile的pmr复制会拒绝并归还临时charge；该测试不是native context budget已实现的证明。source和报告文本的identity身份不构成额外GPU allocation。

capability链严格是：原始context capture → 不可变事实profile → 版本化lowering和显式extension opt-in → 完整shader/package/binding及backend-owned payload → DeviceCapabilities/execution gate。P0公开fromProbe只做事实验证，所有admission始终executable=false；当前没有backend-owned source/payload，因此生产入口继续返回EXECUTION-NOT-IMPLEMENTED。P2实现后必须用同一个绑定对象生成capability和执行payload，不能分别查询或缓存出不同结论。loss先撤销epoch和全部旧profile绑定；restore必须新epoch重新query，不以相同文本/GLuint恢复旧权限。

五backend结论：Null仅允许合成身份的CPU validator；Vulkan/Metal/WebGPU保留原生device身份、lifetime与capability来源，不消费WebGL文本；WebGL在P0仅事实验证，source认证/真实执行均DiagnosticFailure。此处完整定义P0-05的认证与capability连接规则，真实capture/epoch/race/loss执行证明由P2/P3验收，不提前称Backend/Platform Validated。

稳定诊断前缀RHI-WEBGL1-IDENTITY-：PROBE-INCOMPLETE、PROBE-INVALID、CONTEXT-VERSION、SHADER-VERSION。必须覆盖每项缺query、嵌入NUL、错误版本/伪前缀、空诊断身份、独立copy、伪造文本不授权和已建立profile的无分配admission。

## 3. 五后端 lowering 的边界

逐操作的版本化定义见 `rhi-webgl1-graphics-lowering.md`（WebGl1GraphicsLowering/1）：
39项规则覆盖完整请求域、既有四backend的条件映射，以及WebGL目标映射与当前执行拒绝的
区别。下表保留为概览；不能只凭其 N/E 字样跳过逐操作的资源、ABI、初始化和寿命前提。

下表为本 profile 语义的映射要求，非各平台已通过执行验证的声明。N=有相应原生机制；E=必须证明等价；D=本 profile 拒绝；V=Null validator-only。所有 N/E 仍需经过既有 runtime capability 和 payload gate。原有后端更完整的能力不被本 profile 裁剪。

| L2 intent | Null | Vulkan | Metal | WebGPU | WebGL1 目标 |
| --- | --- | --- | --- | --- | --- |
| 传统 buffer/纹理 descriptor 与 draw | V | N，既有绑定契约 | N，既有绑定契约 | N/E，既有绑定空间 | E，反射定义的 uniform/texture-unit plan |
| AtomicFullBlockPush | V | N，push constant | N/E，既有 setBytes 契约 | E，反射绑定和 bounded upload ring | E，draw-record 快照→program uniforms；尚未实现 |
| CPU 内容的只读 uniform intent | V | N | N | N | E，仅可追踪快照和明确字段映射；GPU 写 buffer 不在等价域 |
| 资源依赖和 attachment→sampled | V | N，显式 barrier | N/E，既有 resource contract | N/E，既有 usage contract | E，同 context 有序多 pass；不是 subpass/input attachment |
| Recorded 引用托管 | V | command-buffer/fence 原有路线 | encoder/completion 原有路线 | command-buffer/completion 原有路线 | backend 持有所有未 issued 引用，issue/cancel/loss 有确定转换 |
| 单 context ordered resource reuse | 仅 eligibility 验证 | 保留真实 completion 策略 | 保留真实 completion 策略 | 保留真实 completion 策略 | E 的候选，需真实托管授权；不是 GPU Completed |
| native fence/timeline/跨队列等待 | V，不提供 native 证明 | N，按实际查询与同步对象 | N，按实际查询与同步对象 | N/E，按真实 completion 契约 | D，不造 signal/wait 成功 |
| heap alias、completed publication、production readback | V | 独立既有契约 | 独立既有契约 | 独立既有契约 | D，不能继承 ordered reuse 授权 |
| surface lifecycle | V | swapchain 原有路线 | drawable/layer 原有路线 | canvas 原有路线 | canvas context 租约、非阻塞帧循环、generation 和重建契约；尚未实现 |
| context/device loss | V | 既有 loss 状态 | 既有 loss 状态 | 既有 loss 状态 | Lost 立即撤销执行；restore 重新 query 和重建；不是 completion |

当前 WebGL 通用入口统一保持拒绝：全部 EngineFeature fallback 为 DiagnosticFailure；push plan 无 executable payload；command/pipeline/lifecycle lowering 明确失败。后续新增可执行分支必须消费受限 profile 的具体 lowering；不得把 generic unsupported 改成无条件成功。

### 3.1 Query 和特殊 draw 路径的明确拒绝

`WebGl1Graphics/1` 不提供生产 query、indirect producer/draw 或 graphics storage write。共享 query 元数据、slot declaration 和 creation lowering 在 WebGL 上均返回 `RHI-WEBGL1-QUERY-UNSUPPORTED`，不授予 validator-only/native 权限；即使传入其他后端的完整 timestamp/API facts 也不能开启。Null 保留 validator-only，Vulkan/Metal 保留既有未实现结论，WebGPU creation 仍受其 timestamp/type/count/label/API 契约控制。

Indirect argument、GPU producer、graphics storage write 分别返回 `RHI-WEBGL1-INDIRECT-DRAW-UNSUPPORTED`、`RHI-WEBGL1-GPU-INDIRECT-PRODUCER-UNSUPPORTED`、`RHI-WEBGL1-GRAPHICS-STORAGE-WRITE-UNSUPPORTED`。Null 仅 Equivalent validator；原三个 native backend 保留既有 payload-missing 拒绝，不由 WebGL 扩展改变执行语义。

`OriginalIndexedDrawFetch/1` 和 `/2` 返回 `RHI-WEBGL1-ORIGINAL-INDEXED-FETCH-UNAVAILABLE`；它是独立的 original-root 内容/托管证明路径，不等同于 P2 的普通 Uint16 indexed draw。`InstanceAttributeFetch/1` 当前无 WebGL payload，返回 `RHI-WEBGL1-INSTANCE-ATTRIBUTE-FETCH-UNAVAILABLE`；即使 profile 的 instancing eligibility 成立也不能执行。后续 P3 扩展必须先定义其映射并完成真实 payload/测试才能替换该拒绝。全部决定通过真实 frontend 决策类型测试，原四后端结论同时回归。

### 3.2 Original resource/loss 路线的明确拒绝

WebGL1 context 的事件和 GL 对象寿命不能继承原有 Vulkan/Metal result-loss、WebGPU promise callback 或 original-root registry 的授权。普通 WebGL 资源、未来 context-loss callback 与下列 original 路线是独立契约；P2/P3 实现前，下表的 WebGL 决定均不可执行、非 validator-only，且不签发 logical lifetime、registry、native source、observer、recording 或 completion 权限。

| 原入口 | WebGL 稳定诊断（均以 `RHI-WEBGL1-` 开头） |
| --- | --- |
| OriginalDeviceLifetime/1 | `ORIGINAL-DEVICE-LIFETIME-UNAVAILABLE` |
| OriginalResourceRegistration/1～6 | `ORIGINAL-REGISTRATION-UNAVAILABLE` |
| OriginalSamplerRegistration/1 | `SAMPLER-REGISTRATION-UNAVAILABLE` |
| GeneralSamplerDescriptorReference/1 | `SAMPLER-REFERENCE-UNAVAILABLE` |
| DeviceLossRegistration/1 | `LOSS-SUBSCRIPTION-UNAVAILABLE` |
| OriginalDeviceNativeLoss/1 | `ORIGINAL-LOSS-SUBSCRIPTION-UNAVAILABLE` |
| OriginalDeviceNativeResultLoss/1 | `RESULT-LOSS-SOURCE-UNAVAILABLE` |
| OriginalDeviceNativeLossObserver/1 | `LOSS-OBSERVER-UNAVAILABLE` |
| OriginalTypedNativeLossJoin/1、/2 | `TYPED-LOSS-JOIN-UNAVAILABLE` |
| OriginalDescriptorNativeLossRegistration/1～3 | `DESCRIPTOR-LOSS-JOIN-UNAVAILABLE` |
| OriginalSamplerNativeLossJoin/1、/2 | `SAMPLER-LOSS-JOIN-UNAVAILABLE` |

已有 request/version/opt-in 检查优先级保持：非法版本仍返回原版本诊断；registration/7～9 必须经过独立显式 loss join，不由 generic registration 开启。合法 opt-in 也不能绕过 WebGL 拒绝。未知 backend 继续使用原 unknown 结果，不把未识别枚举误报成 WebGL。Null 保留 CPU validator/metadata 决定；Vulkan/Metal/WebGPU 保留各版本既有 result/callback/registration 结论，任何已有 `Available` 也不因此扩大到执行或 GPU completion。

回归通过真实 frontend decision 类型覆盖全部有效版本、非法/未授权输入、原四 backend、未知 backend 和无动态分配。该覆盖只关闭本表的 lowering 审查，不关闭全部 P0 入口审计，也不替代 native source/custody 实现证据。

纹理 original-content 也不能借普通 GL client upload 提升到 Completed。`TextureContentAccessPolicy`、`OriginalTextureContentCreation/1～7`、`NormalizedTextureStateCreation` 和 `OriginalTextureContentStaging` 的 WebGL 决定分别返回 `RHI-WEBGL1-ORIGINAL-TEXTURE-CONTENT-UNAVAILABLE`、`RHI-WEBGL1-ORIGINAL-TEXTURE-CREATION-UNAVAILABLE`、`RHI-WEBGL1-ORIGINAL-TEXTURE-STATE-UNAVAILABLE`、`RHI-WEBGL1-ORIGINAL-TEXTURE-STAGING-UNAVAILABLE`。所有 available/nativeExecution/validator flags 均 false，creation/staging disposition 为 DiagnosticFailure；不签发内容 revision、state control 或 staging 凭据。普通 CPU upload 未来通过独立 client-copy/ordered-access 契约实现，不能调用此 original completed-content 路线。

Null 保留 validator；Vulkan/Metal/WebGPU 各 content version 的 Prototype/DiagnosticFailure、normalized metadata 和 staging 前置决定保持原行为，尤其 Prototype 不提升为 nativeContentExecutionAvailable。未知 backend 和原非法版本检查维持既有失败结果。四类决定的全部有效版本及反例纳入同一资源生命周期测试，不以实际创建空纹理或 Dummy staging 证明成功。

### 3.3 Original descriptor routes：独立拒绝，不继承 WebGPU 方法事实

`descriptor_lowering.h` 的十五个 lowering 明确将 WebGL 分类为 DiagnosticFailure；不签发 original publication、binding capture、native group identity、render usage projection 或 write preparation。所有 available、execution、completion 等授权字段保持 false，validatorOnly=false。不能因为 WebGL 的 texture units/uniform 能力与传统绑定相关，就继承 WebGPU 的 original group/scope 或 completed-resource 契约；普通绑定的未来等价实现由 P1/P2 独立定义。

| 入口 | WebGL 稳定诊断（前缀均为 `RHI-WEBGL1-`） |
| --- | --- |
| TextureCellDescriptor/1～2 | `TEXTURE-CELL-DESCRIPTOR-UNAVAILABLE` |
| BufferRangeDescriptor/1 | `BUFFER-RANGE-DESCRIPTOR-UNAVAILABLE` |
| CompletedTextureDescriptor/1～4 | `COMPLETED-TEXTURE-DESCRIPTOR-UNAVAILABLE` |
| OriginalGeneralDescriptorBinding | `ORIGINAL-GENERAL-BINDING-UNAVAILABLE` |
| OriginalDepthTablePublication | `ORIGINAL-TABLE-PUBLICATION-UNAVAILABLE` |
| OriginalTargetMemoryInterval | `ORIGINAL-TARGET-INTERVAL-UNAVAILABLE` |
| OriginalDescriptorBufferBinding | `ORIGINAL-DESCRIPTOR-BUFFER-UNAVAILABLE` |
| Descriptor buffer interval diagnostic | `ORIGINAL-RUNTIME-INTERVAL-UNAVAILABLE` |
| OriginalFiniteDescriptorWrite | `ORIGINAL-FINITE-WRITE-UNAVAILABLE` |
| Finite source diagnostic | `ORIGINAL-FINITE-SOURCE-DIAGNOSTIC-UNAVAILABLE` |
| OriginalFiniteTargetMemoryWrite | `ORIGINAL-TARGET-FINITE-WRITE-UNAVAILABLE` |
| OriginalDescriptorNativeIdentity | `ORIGINAL-GROUP-IDENTITY-UNAVAILABLE` |
| OriginalRenderDescriptorUsage/1～4 | `ORIGINAL-RENDER-USAGE-UNAVAILABLE` |
| OriginalGeneralDescriptorWrite | `ORIGINAL-GENERAL-WRITE-UNAVAILABLE` |
| OriginalWholeDescriptorWrite | `ORIGINAL-WHOLE-WRITE-UNAVAILABLE` |

原四 backend 的逐版本决定保留：Null 的 validator-only 不作 native 证明；Vulkan/Metal 的部分 texture publication 与既有拒绝不变；WebGPU 的 preparation/projection/identity 仍只限已有独立 profile，不变成 execution。未知 backend 保持既有 unknown 诊断；非法 version 先于 backend 检查，仍返回原 VERSION 诊断。五项 WebGPU native-method facts 的全部32种组合、全部 identity profile 及未知值，都不能给 WebGL 开放入口。测试使用真实 frontend contract 的 Decision，不创建 Dummy descriptor。

### 3.4 Command state子域：输入验证不等于执行

本增量限定CompletedDepthReadInput/1～2、GeometryInputAccess/1和OriginalCommandStateAdmission/1三个已有子契约，不改变其L2语义，不代表普通graphics draw已实现。WebGL client-copy/ordered访问不能提供completed depth source、显式geometry barrier或原始注册/状态图的执行权限。三个入口分别返回稳定诊断RHI-WEBGL1-COMPLETED-DEPTH-READ-INPUT-UNAVAILABLE、RHI-WEBGL1-GEOMETRY-ACCESS-UNAVAILABLE、RHI-WEBGL1-ORIGINAL-STATE-UNAVAILABLE，全部authority flags为false。

| 子契约 | Null | Vulkan | Metal | WebGPU | WebGL |
| --- | --- | --- | --- | --- | --- |
| CompletedDepthReadInput/1～2 | ValidatorOnly；输入/native执行均false | /2仅native input validation；/1拒绝，执行始终false | 同Vulkan | /1仅native input validation；/2拒绝，执行始终false | DiagnosticFailure；非validator、无native validation/执行 |
| GeometryInputAccess/1 | Equivalent、validator-only barrier mapping | Native barrier mapping；不授权draw | DiagnosticFailure、dependency unproven | DiagnosticFailure、dependency unproven | DiagnosticFailure、无barrier mapping |
| OriginalCommandStateAdmission/1 | ValidatorOnly；无issuer/执行 | DiagnosticFailure、issuer未实现 | 同Vulkan | 同Vulkan | DiagnosticFailure、无issuer/执行 |

非法版本/不完整预算/未opt-in的原请求诊断优先于backend诊断，未知backend保留原UNKNOWN/BACKEND诊断，不能误写WebGL。Geometry barrier helper只有涉及VertexInput stage或VertexAttributeRead/IndexRead access时消费该决定；纯transfer barrier不由本子契约授权或拒绝，仍须消费其自己的lowering。五backend决定、frontend转发、相关stage/access和版本/预算优先级均需快速负向测试及Null smoke。两个既有头文件的其余版本分派见3.5节；全RHI其余lowering与执行门禁审计仍open，P0-07/09不因这两节的局部完成关闭。

### 3.5 Command state / content / admission的完整版本分派

本增量只补两个既有semantics头的WebGL决定，不重新定义原四backend的CPU planning能力。所有以下WebGL决定均为DiagnosticFailure，validator/projection/state/ownership/plan/validation/root/retainedReadback/execution flags全部false；返回支持请求的确切contractVersion。typedState保留原委托：State/2～5消费DescriptorConsumption/1～4，不单独构造成功。非法版本继续优先使用原VERSION诊断；未知backend仍使用原BACKEND/UNKNOWN诊断。原始admission intent/opt-in/budget验证继续在backend门禁前，不从profile/extension/caller booleans获得权限。

| 领域/支持版本 | Null | Vulkan | Metal | WebGPU | WebGL诊断（前缀RHI-WEBGL1-） |
| --- | --- | --- | --- | --- | --- |
| IndexedConsumption、DescriptorConsumption/1～4 | validator-only，projection关闭 | DiagnosticFailure | DiagnosticFailure | Equivalent CPU projection，无state/执行 | INDEXED-CONSUMPTION-UNAVAILABLE / ORIGINAL-CONSUMPTION-UNAVAILABLE |
| VertexStreamShaderConsumption/1 | validator-only，projection关闭 | DiagnosticFailure | DiagnosticFailure | Equivalent CPU shader projection，无state/执行 | VERTEX-SHADER-CONSUMPTION-UNAVAILABLE |
| OriginalCommandAdmission/1 | ValidatorOnly，无执行 | DiagnosticFailure | DiagnosticFailure | DiagnosticFailure | ORIGINAL-COMMAND-UNAVAILABLE |
| BindlessRootState/1～5 | Equivalent validator CPU projection | Equivalent CPU projection | /1～4 Equivalent；/5拒绝 | 同Metal | BINDLESS-STATE-UNAVAILABLE |
| TypedState/2～5 | DiagnosticFailure validator，无projection/state | DiagnosticFailure | DiagnosticFailure | Equivalent CPU projection/state；非native执行 | 委托ORIGINAL-CONSUMPTION-UNAVAILABLE |
| TypedReadbackState/1，State/3～4 | Equivalent validator-only，无state | DiagnosticFailure | DiagnosticFailure | Equivalent retained CPU state，无执行 | TYPED-READBACK-STATE-UNAVAILABLE |
| TextureTransaction/3、5 | Equivalent validator-only，无ownership/plan | DiagnosticFailure | DiagnosticFailure | Equivalent CPU ownership/plan，无执行 | TEXTURE-TRANSACTION-UNAVAILABLE |
| TextureContentSequence/2、3、4 | Equivalent validator validation | DiagnosticFailure | DiagnosticFailure | /2、3 Equivalent CPU validation；/4拒绝 | CONTENT-SEQUENCE-UNAVAILABLE |
| TextureContentPlan/2、3、7、11～16 | Equivalent validator；/2、3不提供validation，其余提供 | /7、12、13、15提供Equivalent CPU validation；其余拒绝 | /12、13、15提供Equivalent CPU validation；其余拒绝 | /2、3、11、14、16提供Equivalent CPU validation；其余拒绝 | CONTENT-PLAN-UNAVAILABLE |
| BindlessGraphicsContentPlan，geometry/0～1 | Equivalent validator validation/root | Equivalent CPU validation/root | geometry/0 Equivalent；/1拒绝 | 同Metal | BINDLESS-GRAPHICS-CONTENT-UNAVAILABLE |
| BindlessCompletedReadPlan/1 | Equivalent validator validation/root | DiagnosticFailure | DiagnosticFailure | Equivalent CPU validation/root | BINDLESS-COMPLETED-READ-UNAVAILABLE |
| FiniteGraphicsContentPlan/6 | Equivalent validator validation，无root | DiagnosticFailure | DiagnosticFailure | Equivalent CPU validation/root | FINITE-GRAPHICS-CONTENT-UNAVAILABLE |
| TextureCompletedReadPlan/4、5、8、10 | Equivalent validator validation | /4、5、8 Equivalent CPU validation；/10拒绝 | 同Vulkan | 全部Equivalent CPU validation | /4、5、8为COMPLETED-TEXTURE-READ-PLAN-UNAVAILABLE；/10委托BINDLESS-COMPLETED-READ-UNAVAILABLE |

表中所有原backend的CPU planning决定也始终execution=false，不能成为native成功证明。完整版本组合、无效版本/未知值、typed/completed-read委托、全部权限flags、请求诊断优先级、无core热分配及Null smoke必须验证；表格不关闭剩余RHI/backend/parser审计，也不将整体P0-07/09标完成。

## 4. 无 fence 的命令与资源寿命

### ConformanceEvidenceAdmission/1：证据来源先于readiness

本契约同时覆盖Null、Vulkan、Metal、WebGPU和WebGL。descriptor布尔值、按scenario/salt计算的CPU hash、仅创建资源并提交heartbeat，以及caller自报的platform flags，都不能证明对应绘制、GPU输出或目标平台执行。现有`evaluateBackendConformance`、`evaluateRhiConformanceMatrix`、`RhiConformanceRunner`和`evaluateBackendPlatformValidation`没有受认证的实际workload/capture来源，因此默认拒绝，不由输入中的hardwareValidationObserved、renderOutputHashCaptured或backendValidated打开readiness。

| 入口/来源 | Null | Vulkan | Metal | WebGPU | WebGL |
| --- | --- | --- | --- | --- | --- |
| descriptor-only matrix | DiagnosticFailure；validator fixture不能冒充native | DiagnosticFailure | DiagnosticFailure | DiagnosticFailure | DiagnosticFailure |
| 现有heartbeat runner | DiagnosticFailure；独立Null hidden-frame仍是合法validator路径 | DiagnosticFailure；未证明真实scenario payload | 同Vulkan | 同Vulkan | DiagnosticFailure；生产backend不存在 |
| caller platform flags | DiagnosticFailure；无native平台认证 | DiagnosticFailure；缺受认证的执行/输出来源 | 同Vulkan | 同Vulkan | DiagnosticFailure；无WebGL runtime profile |

WebGL统一诊断`RHI-WEBGL1-CONFORMANCE-UNAVAILABLE`，未知backend为`RHI-CONFORMANCE-BACKEND-UNKNOWN`。原四backend按来源分别为`RHI-CONFORMANCE-DESCRIPTOR-EVIDENCE-UNAUTHENTICATED`、`RHI-CONFORMANCE-RUNTIME-WORKLOAD-UNIMPLEMENTED`、`RHI-CONFORMANCE-PLATFORM-EVIDENCE-UNAUTHENTICATED`。身份路由不是能力查询；拒绝发生在device factory、resource/command/native调用之前，不调用callback、不CPUReplay、不返回Dummy。

拒绝报告不得复制caller的成功证据：success、visual/parity validated、backend readiness/validated、platform validated和真实执行flags保持false，视觉hash为0；scenario计为失败，不以skip掩盖请求拒绝。CPU deterministicConformanceScenarioHash函数仍可用于非视觉fixture身份，但不得被提升为输出hash。runtime/backend匹配仅能报告身份一致，不能授权readiness。

`RenderSystemRhiProductionMigrationGate`也不能把public aggregate report的success/parity/visual/readyCount布尔值或计数当认证；当前aggregate没有受认证的proof载体，backendParityEvidenceAccepted和visualRegressionEvidenceAccepted保持false，给出`RHI-CONFORMANCE-AGGREGATE-EVIDENCE-UNAUTHENTICATED`。即使caller手工填充所有成功字段也必须拒绝。其renderer/UI/graph结构需求仍可作独立语义诊断，不能因此返回production migration成功。

本修复收紧原四backend的旧descriptor/heartbeat facade，不改变其真实native backend或独立已验证smoke。P4-14/18仍必须实现并认证真实共用workload、artifact/profile/context身份、draw/输出和capture证据，再以新版本显式开放对应conformance路径；当前拒绝不替代该交付。相关公共描述/报告类型保留源码布局兼容，不把旧caller字段重新解释成受认证证据。所有新增运行时元数据使用core分配入口；不存在runtime GL/文件IO或热路径编译。

### 4.1 状态不变量

命令生命周期为 `Recording → Sealed → Issuing → Issued`，以及 `Cancelled / Failed / Lost` 终态。`Issued` 表示调用桥已接收 GL 工作；`QueueSubmitResult.accepted` 保持既有含义。上述所有状态都不证明 GPU Completed。

- Sealed payload 不可变；push/uniform/descriptor revision 和资源逻辑 generation 必须在 record 时固定。录制后修改不能回写旧 draw 的数据。
- backend 以有界 owner/ref 集合托管命令所需资源、host snapshot 与 shader/pipeline binding plan；上层不持 GL 对象。
- Issuing 之前预检版本、budget、资源初始化、完整绑定、context generation 和禁止操作。能预先识别的失败不得留下原生 draw 副作用。
- 一部分 GL 已发出后发生 OOM/loss，报告已接受工作及失败范围；不能宣称事务回滚、CPUReplay 成功或所有 GPU 已完成。
- Cancelled 仅能释放尚未 issue 的引用。Lost 撤销相关所有授权，不把旧 context 的工作写为完成。

### 4.1.1 OrderedCommandIssue/1（CPU custody 子契约）

本子契约定义单个序列化 owner 管理的命令 CPU 数据寿命，后续 WebGL executor 组合它；不提供 native registry、resource reuse lease 或 GPU completion。实现复用 `core::BoundedRetainer`、`BoundedSequence`、`LifetimeEpoch` 和 `CpuMemoryQuota`。不改变现有 `CommandRetentionStorage` 的 completion-retirement 路线。

- 预算明确给出 operation 数、独立 owner 数、owner reference occurrences（含重复项，限制遍历工作）、snapshot payload 字节和 allocator 实测 CPU quota。零容量关闭对应类别，CPU quota 为零无效；prepare 分配全部存储，record/issue/cancel/fail 不新增动态分配。输入错误或任一容量不足在修改 ownership/bytes/operation 前拒绝。
- Recording 逐 operation 保存不可变 client bytes 和 owner 集合；同一 owner 去重，owner 计数不能推算 CPU bytes。Sealed 后不得改写记录。空序列不能进入 Issuing。
- beginIssue 必须 join prepare 时持有的同一个仍有效 context/queue epoch ticket；任意当前的外域 ticket、旧 ticket 或相同数值 generation 不能替代它。资源 owner 的原生来源和 epoch join 仍由 backend registry 验证，此 CPU helper 不根据 shared_ptr 或 caller 布尔值推断原生身份。
- 每个 operation 先 beginOperation，再在 executor 确认相应桥调用已接受后 acceptOperation；不可跳号、重复接受或在未开始时接受。beginOperation 后就可能有 native side effect，accepted 计数只记录已确认接受。partial failure 保留 accepted 和 possible-side-effect 事实，不能将其称为取消或回滚。
- 全部 operation 接受后才能 finishIssue，进入 Issued 并释放 CPU snapshot/owners。这里释放 owner 的前提是 backend 已证明被保留对象的 GL delete/命令引用语义；CPU helper 本身不能证明该前提。Cancelled 仅从 Recording/Sealed 进入；Failed/Lost 结束 CPU 使用，不生成成功发布。context 失效使旧序列不可继续，恢复必须创建新序列。
- Native/Equivalent execution、GpuCompleted、fence/timeline signal、readback、completed publication 和 ordered resource reuse 均不由此 helper 授予。状态转换及容量测试只属于 CPU contract 证据，真实 adapter 必须另验。

| OrderedCommandIssue/1 execution | Null | Vulkan | Metal | WebGPU | WebGL |
| --- | --- | --- | --- | --- | --- |
| 当前决定 | Equivalent validator-only，不执行 | DiagnosticFailure，保留 completion 路线 | DiagnosticFailure，保留 completion 路线 | DiagnosticFailure，保留 completion 路线 | DiagnosticFailure，真实 issue payload 未实现 |

所有执行结论明确 `executable=false`。稳定诊断前缀 `RHI-ORDERED-ISSUE-`，覆盖 VERSION、OPT-IN、CPU-QUOTA、CONTEXT、CONTEXT-MISMATCH、OPERATION-CAPACITY、OWNER-CAPACITY、SNAPSHOT-CAPACITY、STATE、EMPTY、OPERATION-PENDING、OPERATIONS-REMAINING、NATIVE-FAILURE、PAYLOAD-FAILURE、CONTEXT-LOST。实际 WebGL backend 实现和测试完成前不得将此表改为可执行。

### 4.2 有序复用授权

`OrderedResourceReuse/1` 当前只是 eligibility 评估，返回 `executable=false` 和 `provesGpuCompletion=false`。调用者填写 sameQueue 等布尔值不能获得租约。

P3 的真实复用策略必须由 backend-owned registry/queue 签发并消费授权，绑定以下事实：issuer/device、context generation、queue identity、资源 identity/generation、所有未 issue 录制引用、资源占用及预算。授权不可跨 context/queue 或转为 GPU completion receipt。

只允许同 context/queue 内 GL 访问顺序已固定、旧录制引用全部 issue/cancel 的资源进入有序复用。依赖 GL 自身的命令访问排序及对象寿命规则，不能借此允许 CPU 读 GPU 结果、外部共享、native heap aliasing、readback、completed resource publication 或独立队列等待。

资源复用时旧 command/descriptor 必须仍由 retention 或逻辑 generation 保护；同一 GL 名称或 JS table slot 的重用不能造成 ABA。资源池现有 `RG-POOL-RELEASE-MISSING-COMPLETION` 拒绝保持有效，ordered lease 必须有独立入口和验证。

### 4.3 数据复制和 delete

| 数据/对象 | 必须保留到 | 可回收依据 | 不得推导 |
| --- | --- | --- | --- |
| 录制的 push/uniform/上传源 | 对应 GL 传入调用实际完成，或录制取消 | 原生 API/桥已复制 client 数据，且无剩余未 issue 使用；按实际桥验证 | GPU 已完成读取 |
| 仅 CPU 使用的 encode payload | 最后一次 executor 读取结束 | 明确 payload ownership 和 consumed 状态 | fence 可 signal |
| buffer/texture/program 等 GL 对象 | 未提交引用全部处理、访问顺序固定；必要解绑与 delete 已执行 | GL 对象命令引用/delete 契约；backend 身份仍防旧引用复活 | 物理显存已经释放，或驱动已达到某个完成时间 |
| 可恢复资源 CPU 副本 | 明确 reload policy 终止 | 稳定资源 ID/重建配方，或调用方声明不可恢复 | 可以无预算无限保存内容 |

GL deletion 不等于 host 编码结束，也不等于 GPU 完成。各类 native 绑定解除及仍被其他对象引用的 delete 行为必须按对象类型实现，不能用一个“删 ID”计数器替代。

当前 `WebGl1RuntimeProbe/2` 的三阶段数值与实际 ANGLE 六 draw 捕获，已证明此 fixture 中 BufferData/TexImage2D/uniform 和 BufferSubData/TexSubImage2D 在清零 client 源后保留传入值；发出 draw 后解绑/delete buffer、texture、program，再执行冷 readPixels，仍得到预期更新图像且 glIs* 为 false。它只支持这些 API/对象的可行性结论，不签发 backend retention/reuse 授权，也不证明物理内存释放或 GPU 完成。其余对象、未发出引用、generation/预算和丢失路径仍由 P0/P3 关闭。

## 5. 内存、热路径和 shutdown

caller workload 声明最大命令/引用/对象数、push/uniform/upload 字节、CPU metadata 和 reload 副本预算。所有动态容器与 owned object 通过 core allocator，算术使用现有 checked/budget utility。记录 exact/one-less/exhausted/overflow 证据；budget=0 不能解释为无限。

区分三种数据：实际 allocator/staging 字节；按已知 format/extent 计算的逻辑 texel/buffer 字节；不可查询的浏览器/驱动物理 allocation。最后一项必须标记 Unknown；不能给估算值贴 native GPU allocation 标签。要求精确物理预算的 L2 intent 在此 profile 拒绝。

热路径禁止阻塞 wait、finish、同步 readPixels、文件/网络 IO、shader 编译、pipeline 创建、getUniformLocation 和 limits query。GL 上传可按已批准计划执行，但不可按帧无限 orphan/扩容；驱动内部不可观测的分配如实记录限制。

shutdown 停止 acquire/record，拒绝新工作；取消未提交 payload；删除 backend 持有对象并释放 host owners。若调用方要求“deadline 内真实 GPU drain”，返回 GPU completion unavailable；不能以 gl.flush、空 wait 或立即 signal 满足 drain。测试专用 gl.readPixels/finish 不提升生产能力。

既有 `ShutdownDrain/1` 保留原版本和 API：无效 version/缺失 deadline 返回 DEADLINE-INVALID，拒绝时 deadline 已到返回 DEADLINE-EXPIRED，deadline 尚未到而实现不提供 drain 返回 UNAVAILABLE/DiagnosticFailure。共享 helper 对五 backend 均如此，不以 backend 名称推导完成。Null 的真实 validator cleanup 仍可返回其既有 Equivalent；Vulkan/Metal/WebGPU 仍需各自真实 completion 路线，WebGL 不可用。`OrderedCommandIssue::Issued`、Cancelled、Failed 或 Lost 均不能使 drain Completed。

## 6. Shader、坐标和工具链决策

P0 选定 Slang → SPIR-V → SPIRV-Cross ESSL100，使用 Emscripten GLES2 bridge。binary IR 不采用：真实 WebGL1 context、客户端上传源修改、浏览器主循环和 ANGLE 捕获均已获得可行性证据，没有限制要求替换现有桥。该决策不提升生产 capability，生产 ABI/精度/坐标和寿命仍按 P1～P3 完成。

已验证的探针工具身份在生成 reflection.yaml 中记录 executable SHA256；Slang 实际报告 2026.8，SPIRV-Cross 报告 `vulkan-sdk-1.4.341.0-67-g1a616956`，不能根据安装目录名当作编译器版本。SPIRV-Reflect CLI 无版本查询接口，使用文件哈希。

- Slang 直接 `-profile glsl_100` 在当前版本被拒绝，原始错误保存。SPIRV-Cross 能生成 ES100，但 `--flatten-ubo` 对本 fixture 的透明数组 wrapper 失败。
- 已探测的语法域只有 VS/FS、float 向量/矩阵、struct、固定长度 float4 数组、顶点属性/varying、ConstantBuffer/push 和独立 Texture2D/Sampler。其他语法不由此获得许可，必须在 P1 明确通过或拒绝。
- 探针保留 struct/array uniform，varying 名称由反射 location 映射；不手工修改生成 GLSL。生产 leaf ABI、合法类型、动态 offset、sampler pair 与 precision policy 留给 P1，不能由本探针授予。
- CPU row-major + 默认 Slang float4x4 + `-matrix-layout-row-major` 是不变契约。SPIR-V ColMajor decoration 与 GL `transpose=false` 下的数值行为必须联合验证。
- 当前 converter 的 fragment 默认 float precision 为 mediump，部分接口显式 highp；不能因此声称支持任意 float32 workload。P1 必须显式选择、反射并验证精度策略，必要时使用固定版本转换器 API 的明确编译选项；禁止后处理字符串补丁或静默降精度。
- WebGL 的 projection Y 与 attachment sample origin 不可沿用当前 helper 的统一推导：GL clip +Y 朝上，而 canonical core projection 含负 Y；同时 GL attachment 采样原点为 bottom-left。P1 必须分别定义 projection、viewport/scissor、CPU raster 与 RT sampling 的映射并做非对称多 pass 数值测试，不能复制其他 backend 的一组翻转。
- L2 clip depth [0,w] → GL [-w,w] 需要明确 compiler/backend lowering。当前 z=0 可行性 fixture 不证明完整深度范围，须在 P1/P2 加 near/far/outside clip probes。

## 7. 当前证据与未通过门槛

已有：profile/扩展 opt-in/ordered eligibility 快速测试；shader fixture 的 SPIR-V/MSL/WGSL/ES100/reflection 生成；`tests/contract/shader_tests.cpp` 中源码、ColMajor/非 RowMajor、16B push 断言；Chromium 实际 WebGL1 双 draw 数值探针；Null hidden-frame smoke 日志。客户端 buffer/texture/uniform 源传入后全部清零，后续 60/180 帧仍得到原始上传结果。

已完成当前 Chromium/ANGLE D3D11 执行链的 RenderDoc 捕获和 AI 图像检查：`build/webgl1-evidence/p0/browser-capture-attempt3/webgl1_frame10.rdc`，实际探针 draw 为 EID22 和 EID25；第二 draw 保留 450 个红像素并写入 1187 个绿像素；native vertex buffer 原始数据、1×1 纹理、三个非对称矩阵探针位置和无错误回放均由 `tests/tools/analyze_webgl1_probe_renderdoc.py` 检查。浏览器 compositor 另有 draw，不能混同为引擎工作。opaque canvas 的 ANGLE native write mask 为 RGB=7，按实际 alpha=false 属性解释，不将该值外推给 RGBA surface。

仍缺：五后端所有共享入口审计、生产 artifact/ABI、真实 backend execution/custody、完整 upload/delete 桥验证、surface/context-loss/runtime budgets、RenderSystem/FrameGraph/2D/UI/3D、生产 backend 场景 capture 和完整平台回归。全部在 P0–P4 计划保持 open，不以本文存在宣称 Contract Complete。证据索引见 `rhi-webgl1-validation.md`。
