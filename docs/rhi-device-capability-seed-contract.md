# DeviceCapabilitySeed/1

日期：2026-10-16。状态：Contract Complete，仅下述限定范围。范围为WebGL计划P0-01/07/08/09，
不改变74项任务计数，不替代完整native feature provenance或P2/P3/P4。

## L2不变量

共享makeSharedDeviceCapabilities只构造backend/deviceName元数据；不查询设备、
不发布core/explicit/runtime feature、memory intent或swapchain工作容量。
所有能力初始false；request/frame-slot均为0（未绑定策略）。名字不能签发能力。
label为空时使用调用方显式提供的显示名称，不从名称推导任何语义。
owned默认字段使用core allocator，保留公开签名、字段顺序和布局。
两份公开头在同一组当前传递依赖、MinGW x64条件下做限定布局比较；
旧预编译inline调用仍需要同版本重编，不承诺跨工具链或完整二进制ABI。

validateSharedDeviceInvariants先检查expected身份是否可构造，再检查contract身份、
budget和旧安全选项。WebGL未实现为RHI-WEBGL1-DEVICE-CONSTRUCTION-UNAVAILABLE，
unknown为RHI-DEVICE-CONSTRUCTION-BACKEND-UNKNOWN；extensionEnabled不旁路身份。
身份准入只确认构造入口，不认证device/source或赋予任何可执行feature。

## 五backend lowering及来源

| backend | seed/构造 | 实际发布责任 |
| --- | --- | --- |
| Null | neutral seed，构造validator允许 | 显式NullValidatorProfile/1发布旧validator core/memory/default request；从不作为native证据 |
| Vulkan | neutral seed | backend-owned runtime须有效且非synthetic；core来自实际graphics queue family flags，memory来自已查询memory properties；barrier来自该真实queue的版本化实现 |
| Metal | neutral seed | describeMetalRhiNativeRuntimeBoundary真实MTLDevice查询；仅真实runtime发布mandatory render/compute、Private/Shared intent及barrier lowering；非Apple stub全关闭 |
| WebGPU | neutral seed | backend-private当前device available/not-lost查询；core compute来自实际创建GPUDevice的WebGPU规范保证，profile只能收紧；memory为WebGPU resource intent，非物理内存保证；barrier为ordered equivalent |
| WebGL | neutral seed，不可构造 | stable diagnostic failure，不能由名字、profile或extension flag获得生产能力 |

Vulkan队列事实仅冷查询/缓存；通过vkGetDeviceQueue核对声明family的queue0与runtime
持有queue相同；查询函数缺失、数量/索引/实际queue不一致时关闭core并
输出稳定诊断，不从graphics queue名称推导compute。native三后端缺真实runtime时
关闭所有runtime/explicit/core/memory及push payload声明；Null validator独立。
四后端frame request使用既有显式L2默认策略或实际WebGPU profile，不把request
称为实际swapchain image count/GPU completion。shared seed不会自动选择该策略。

VulkanRhiRuntimeDesc.queueFamilyCount在现有platform与headless fixture中表示选中queue
的旧元数据，不能当物理设备family总数。新冷查询的临时core容器容量取实际native
首次查询count，二次查询count变化拒绝；不按旧selected count裁剪或伪造原生事实。
该owned冷数组有明确count×sizeof预算来源，不声明为完整runtime字节配额已验证。

非synthetic runtime在检查native查询来源前即清空descriptor memory flags。
缺instance、physical device、resolver或memory查询，及零/越界memory type数量，
均不能保留descriptor声称的deviceLocal/hostVisible。显式synthetic fixture独立，
不能向生产DeviceCapabilities发布这些事实。

## 失败、恢复和边界

无native runtime的冷capability snapshot不是成功执行；状态同步仅撤销权限。
诊断通过现有RhiErrorHandler及execution-status输出；恢复需要真实设备/查询重新
建立，修改caller flags不认证来源。现有public feature contract的完整来源链、
每个resource/command gate及device loss后的所有撤销消费者仍须独立审查。
本契约不承诺精确native physical allocation或将单个capability bit当完成证明。

## 实施与验证顺序

先让既有device-validation测试真实复现neutral seed及身份拒绝的失败，再改共享
helper，最后迁移四backend发布者。快速验证包含全部身份/extension/options、
Null真实capability和fallback/profile；native无runtime保持拒绝。原Vulkan真实
hidden smoke及RenderDoc回放/AI图像检查验证合法runtime，Metal/WebGPU缺目标平台
不冒充通过。记录MinGW命令/退出码和列名source/tool/binary/capture hash。

## 当前证据与未关闭项

证据根目录：`build/webgl1-evidence/p0/device-capability-seed/`。
`red/`保留neutral seed及身份准入的两个真实断言失败。
`verified-final/`为最终MinGW构建和8/8 CTest（含Null hidden-frame），45项列名
输入前后相同；包含1568组身份/选项、实际Vulkan队列callback控制流及缓存读取、
六种native memory来源失败、实际无runtime device拒绝caller claims与零native
buffer/submit副作用。callback fixture不作为native执行证明。

`native-final/`为真实Vulkan hidden smoke、RenderDoc捕获和回放，49项列名输入一致。
真实设备报告graphics/compute/deviceLocal/hostVisible为true，request/slots为2；
三类依赖各4次、零validation message；AI已检查320×180全幅蓝色clear，独立
`ai-image-inspection.yaml`保留图像/capture/manifest hash。只验证原Vulkan回归。
Metal Windows stub和WebGPU host路径已编译，均不构成目标平台验证。

`abi/`仅比较两份公开头、同组当前依赖下的2类型/26布局值及helper签名，结果一致；
非完整ABI证明。`verified/`虽构建及8/8测试通过，但七项输入运行中改变，保留为
中间记录，不用于最终结论。`review-checkpoint.yaml`记录上述范围与剩余门槛。

Contract Complete只指neutral seed、身份准入、无runtime撤销及所列core/memory
来源。完整可选feature查询/启用/payload来源、全部execution/completion consumer、
嵌套CPU报告预算、旧fixtures及全ABI/序列化仍Open。P0保持12/19，生产WebGL
P1～P4未完成；不提升WebGL Backend/Platform Validated或Production Ready。

后续共享修改后的复验：`verified-current/`与`verified-serial/`均实际构建和8/8测试
通过，但前者有一份共享shader头变化，后者有该头与根CMake变化，manifest均保持
allNamedInputsMatch=false。`verified-stable/`在系统可用物理内存约5MB时由本任务
结束自己的双worker Ninja，未运行测试；随后单worker复验保留于verified-serial。
此前verified-final/native-final只作为其已记录输入快照的通过证据，不能冒充当前
工作区完整复验通过。当前源复验仍Open，细目见revalidation-history.yaml；不因
并行修改重解释、覆盖或忽略输入变化。

2026-10-16续验已闭合上述Open项：`format-current/`记录锁定clang-format 21.1.8、
12个本批C++文件dry-run通过、token逐项相同且无文件变化。`verified-resumed/`的
8/8通过但仅根CMake变化，保留而不采纳；随后`verified-reconciled/`构建/8项测试
全部通过，48项命名输入前后相同，并保存输入字节副本。`native-reconciled/`真实
Vulkan hidden smoke/捕获/回放通过，52项输入相同，三类依赖各4次、无validation
message；AI已检查320×180蓝色clear并独立记录hash。review-checkpoint已转向这两个
最新快照，currentWorkspaceRevalidationComplete=true只覆盖该时点的命名范围。
随后实施新批次仍需其自身验证，不延伸为生产WebGL或其它平台完成。
