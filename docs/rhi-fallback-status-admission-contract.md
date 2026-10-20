# FallbackStatusAdmission/1

日期：2026-10-16。状态：Contract Complete（仅下述状态一致性和只收紧权限范围）。归属WebGL计划P0-07/08/09；
本契约只约束fallback状态一致性及公开renderer桥的收紧权限，不提供device/context认证。

## L2语义与不变量

1. 基础能力只接受已知的Native/Emulated模式和executable=true的结构状态。
   这只是状态一致性条件，不是实际payload存在或语义等价的证明。
2. ExtensionOptIn永不由基础apply开启；CpuReplay不得发布成基础能力，显式command
   replay/Prototype契约保持独立。Degraded和DiagnosticFailure的能力均为false。
   未知mode、未知feature和未知backend有稳定诊断，不能从排除列表推导可执行。
3. 旧plan中只有decisions而无executionStatuses时，不再猜测支持状态：每个声明的
   feature输出STATUS-MISSING拒绝。完全空plan不声明任何feature，不制造成功状态。
   同一feature重复出现时全部拒绝，不能以最后一条覆盖前面的失败。
4. apply保留未请求feature和query状态；替换本次feature execution-status报告。
   applyRestrictionsTo只允许已存在的能力保持或关闭，不允许false变true。其输出
   状态同时与最终布尔值一致。公开evaluateAndApplyFallback必须使用此收紧入口。
   收紧入口还要求source plan与destination capability身份相同且均已知；不匹配为
   RHI-FALLBACK-CAPABILITY-BACKEND-MISMATCH。旧applyTo的完整来源认证仍Open。
5. synchronizeExecutionStatuses只能撤销能力或状态，不能将executable=false仅凭
   capability=true改为true。失效后要重新查询/构建计划，不能靠重复同步恢复权限。
6. 现有applyTo仍供四个device的初始化使用，Native/Emulated结构通过不认证来源。
   真实query/payload/issuer链审查、完整CPU预算和所有执行门禁仍为Open；不能将
   本契约称为完成受控能力发布或P0/P2/P4完成。
7. 输入status/decision数量不得超过既有kMaxEngineFallbackDecisions；超限不遍历输入，
   撤销全部映射feature及degraded-subpass标志，输出一个STATUS-CAPACITY诊断状态，
   保留query与其它独立事实。该上限不是完整CPU字节预算；嵌套字符串预算仍Open。
   结果先在destination显式memory_resource上构造，分配失败前不改变原snapshot。
   空plan和普通限定输入仍按未请求feature保持规则处理，不借此制造额外能力。

## 五backend lowering

| backend | 本契约结果 | 实际执行边界 |
| --- | --- | --- |
| Null | 同一状态校验；合法结构只用于validator | 不产生native执行证明 |
| Vulkan | 同一状态校验与只收紧桥 | device原有query/lowering/payload gate保持必需 |
| Metal | 同上 | Windows stub不作为Apple-native证据 |
| WebGPU | 同上 | 需真实browser/device执行证据，不认证profileStatus |
| WebGL | 基础执行状态拒绝 | RHI-WEBGL1-FALLBACK-EXECUTION-UNAVAILABLE；生产backend未交付 |

未知backend为RHI-FALLBACK-BACKEND-UNKNOWN。Native/Emulated两个标签本身不能
认证上述任何backend；公共桥不能通过更换backend名称获得能力。

## 当前调用来源分类

| 调用点 | 实际输入与后续消费者 | 本批权限及剩余边界 |
| --- | --- | --- |
| renderer/deployment/native_renderer_deployment_bridge.cpp | caller可写contract、framePlan和capability snapshot | 使用applyRestrictionsTo，不签发false→true权限；不是native probe入口 |
| backends/null/null_backend.cpp capabilities() | 版本化Null vocabulary；后续push lowering及status/query同步 | 保留validator初始化；没有native证据 |
| backends/vulkan/vulkan_device.cpp capabilities() | desc.capabilityContract；随后runtime timeline、physical format/properties和push plan校验 | 保留applyTo及后续撤销；desc/query/payload完整来源认证仍Open |
| backends/metal/metal_device.cpp capabilities() | desc.capabilityContract；随后真实runtime/nativeFeatures、sync/secondary、format与push校验 | 非Apple stub保持关闭；本CPU测试不证明Apple执行 |
| backends/webgpu/webgpu_device.cpp capabilities() | desc.capabilityContract/runtimeProfile；随后limits、optional features、push与query同步 | 保留真实browser前置要求；可写profileStatus不是认证来源 |
| frontend/device_validation.h makeSharedDeviceCapabilities() | 共享构造器先设置部分core/feature默认值，再由上述device覆盖 | 本批未改动；其完整默认值、未知身份和实际payload来源仍需P0审查 |

applyTo和status-sync的当前生产调用点限于上表；测试fixture独立分类，不作native
证明。此清单是本次源码范围内的来源分类，不关闭完整执行门禁或全仓库调用链审计。

## 诊断与恢复

稳定ID：RHI-FALLBACK-MODE-UNKNOWN、RHI-FALLBACK-FEATURE-UNKNOWN、
RHI-FALLBACK-STATUS-MISSING、RHI-FALLBACK-STATUS-DUPLICATE、
RHI-FALLBACK-STATUS-CAPACITY、RHI-FALLBACK-CAPABILITY-BACKEND-MISMATCH、
RHI-FALLBACK-EXECUTION-UNAVAILABLE、RHI-FALLBACK-EXTENSION-OPT-IN-REQUIRED、
RHI-FALLBACK-CPU-REPLAY-EXPLICIT-CONTRACT-REQUIRED、
RHI-FALLBACK-RESTRICTION-CANNOT-GRANT。

校验先检查backend身份、feature、重复feature，再检查mode/executable和收紧条件。
拒绝在CPU snapshot更新时发生，无native API、副作用或重试/隐式降级。保留已有
诊断并补稳定ID；未知mode归为DiagnosticFailure，已知不可执行模式保留其标签。
只有重新经过原有合法device查询/计划构建才可能恢复；修改flags不能认证来源。

## 测试与实现约束

先动态复现现有真实helper/bridge的失败，再实施：未知mode、冲突status、缺失status、
重复feature、同步重新提升和公开renderer桥。覆盖五backend/unknown与原四backend
合法结构决策，保留profile/fallback/格式回归和Null hidden-frame。

复用现有isFeatureSupported、core诊断/allocator和planner fixture；集中feature
read/write映射，避免apply与sync各维护一份switch。纯mode/身份决策无分配；报告
分配用core资源。没有新增shader或可视输出；若真实native回归发生输出变化，
仍按仓库要求捕获RenderDoc。本契约不声称报告热路径完整预算或全ABI已验证。

## 当前实现与证据

证据根目录：`build/webgl1-evidence/p0/fallback-status-admission/`。

- `red/`为真实MinGW构建及九个原始断言失败；不以mock证明native成功。
- 最终CPU运行`verified-final/`：实际MinGW构建与6/6 CTest通过，含本契约、
  deployment evidence、WebGL profile、fallback、RG16F及Null hidden-frame。
  33项列名输入前后hash一致，六binary及GCC/CMake/branch/revision身份保留；
  scope=NamedInputsOnly，不冻结整个共享工作区或完整传递依赖。
- 直接测试覆盖7168组apply/restrict、49组source/destination身份、224组sync×100，
  exact1024/over1025、重复/缺失/unknown feature、空plan、显式null PMR配额失败，
  以及原query/独立feature/source不变。纯policy及受测重复sync无core动态分配；
  不能把此结论扩大为所有报告构造/字符串的完整字节预算。
- `abi/`编译Git HEAD两个旧公开头和当前头，在相同当前依赖/MinGW下提取COFF
  `.arpabi`；14类型/101项size、align、关键offset和enum值相等，八项列名输入
  一致。保留aggregate和旧公开签名；不认证全ABI、跨DLL、其它compiler/platform，
  更不修复旧预编译inline代码，private out-of-line实现要求正常同版本重编。
- `verified-matrix/`保留fixture标识符遮蔽的编译失败；`native-verified/`保留共享
  shader头C++17默认参数编译失败。该共享头由其它工作修正，本批未改其语义。
  `native-current/`的真实smoke/capture退出0，但offscreen Qt插件缺失且构建期间
  一个列名shader头改变，故其manifest保持false；不能作为最终native回归证明。
  `native-replay-windows/`保留输入前置校验拒绝，未执行回放。

最终原Vulkan回归`native-final/`已以Windows Qt插件和SW_HIDE重建/重捕获/回放通过；
41项列名输入从build前、build后到执行后相同。三类barrier/dependency各4次，两个
swapchain image均为全幅BGRA约[191,127,32,255]，零validation message。
AI通过view_image检查320×180的`present.png`：全幅蓝色clear，无缺失/黑区或可见
破损，与数值断言相符。独立`ai-image-inspection.yaml`保留图像/capture/report/
manifest哈希；原runner的aiImageInspected=false保留原始含义，由该独立记录补足。
这只验证原Vulkan clear/barrier/lifecycle回归，不证明WebGL执行或完整renderer画面。
生产WebGL、Apple-native、
真实browser backend、source认证/受控发布、完整consumer/预算审查均未由本批交付。
整个P0仍12/19；本范围Contract Complete不等于Backend/Platform/Production Ready。
