# ShaderCompileTargetAdmission/1

日期：2026-10-16。状态：Contract Complete（仅本 target 准入契约）。本契约限定现有
`ShaderCompiler::compile` 的 target 身份准入，不发布生产 shader artifact。

## 身份与不变量

`shader::TargetBackend` 与 `rhi::Backend`、platform backend preference 是不同的
身份域，不允许按整数互转以选择 shader target。保留现有 ordinal 和公开类型布局：
SpirV=0、Msl=1、Wgsl=2。值3不是预留的可执行 ES100 target，值4也不是 WebGL target。
新增 ES100 角色、reflection/package/cache 身份仍须按 WebGL P1 单独版本化。
这是传入枚举值的准入，不认证调用方的转换来源；显式 cast 成0/1/2之后无法从值恢复
原身份域。调用链仍须单独审计，不得把本门禁当跨域强制转换的完整证明。

compile 必须先穷举 target，再检查 module/source/entry 和提取任何 binding/reflection。
未知值统一返回 `success=false`，diagnostics 精确为
`SHADER-COMPILE-TARGET-UNKNOWN`，保留原始 result.target 用于诊断；bytecode、bindings、
reflection.entryPoints/bindings/vertexAttributes/pushConstantMembers 均为空，push size 为0。
即使请求同时缺少其它字段，未知 target 的诊断仍优先。不得默认选择 WGSL。

三个已知值分别且仅分别选择 `spirv`、`msl`、`wgsl`。本次保留其已有行为：返回
`stub-compiled:<module>:<target>` 及源码推导的结构元数据。这些字节是历史 synthetic
metadata，不是 Slang 输出、SPIR-V/MSL/WGSL 可执行代码，也不认证 reflection 或 native
capability。compile.success 不能作为生产 artifact/pipeline 准入依据。

准入不改变任何 shader 源、编译选项、矩阵表示、生成产物或生产 package schema；不新增
native 创建、记录、提交或呈现路径。不修改 ShaderCompileRequest/Result 的成员及布局。

## 五 backend 边界

| RHI backend | 此 compile 身份检查的结论 | 执行边界 |
| --- | --- | --- |
| Null | Validator-only，不能由 RHI ordinal 推导 target | 无 native payload |
| Vulkan | 独立显式 SpirV 请求可保留历史 metadata 行为 | synthetic bytecode 不是 Native 证明 |
| Metal | 独立显式 Msl 请求可保留历史 metadata 行为 | synthetic bytecode 不是 Native 证明 |
| WebGPU | 独立显式 Wgsl 请求可保留历史 metadata 行为 | synthetic bytecode 不是 Native 证明 |
| WebGL | 当前枚举无 ES100 target，不能用3/4或其它数值代替 | DiagnosticFailure；生产角色由 P1 交付 |

## 失败与恢复

未知值在 CPU 结构提取前拒绝，不发布部分反射，不保留跨请求状态。调用方必须提供已知
target 才能重试；不尝试另一个后端。三个已知 target 的既有缺 module/source/entry
诊断顺序保持不变。诊断字符串仍使用现有 core allocator；不声称拒绝路径零分配。

## 验证门槛

- 快速 `arp_shader_compile_target_contract_tests`：未知正/负/边界值，包括3/4、缺字段
  组合、零输出、请求不变、已知三值正确标签、拒绝后有效请求、旧输入诊断。
- `arp_shader_tests`：已有完整 shader/reflection/矩阵回归，不以 stub 证明真实编译。
- `arp_rhi_null_hidden_frame_smoke_tests`：既有 Null hidden-frame 回归。
- 本次没有可视输出变更，不产生新的 RenderDoc、Backend Validated、Platform Validated
  或 Production Ready 声明。整个 P0-01/04/09 保持 Open，不能凭单个身份门禁关闭。

当前证据：`build/webgl1-evidence/p0/shader-target-admission/verified/`。实际 MinGW 构建
与上述三个入口及 WebGL profile 共4/4 CTest通过；8项命名输入前后hash一致，四个测试
binary hash保留。`../red.log`记录修复前未知值仍返回success的断言失败，工具版本见
`../tool-identity.yaml`。NamedInputsOnly不认证全仓、全部传递依赖或目标平台。
