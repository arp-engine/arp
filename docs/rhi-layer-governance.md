# RHI Frontend、Semantics 与 Backends 治理规则

`L2` 表示架构层级，不作为代码包名。`src/rhi` 保留现有 14 个 `gpu_*.h` 公共入口，内部只有 `frontend`、`semantics`、`backends` 和 `conformance` 四个目录。前三个共享包及每个后端内部全部平铺；领域用文件前缀表达。

| 位置 | 职责 | 边界 |
|---|---|---|
| `gpu_*.h` | 公共对象、接口以及上层需要的 cache、frame validation、authorization、reflection、slot lease 声明 | 保留符号、namespace、对象布局；不包含 native 头 |
| `frontend` | L2 值模型、纯校验、预算、布局计算、对象关联、资源保活、owned preparation、共享录制机制 | 不定义四后端能力矩阵，不调用 native API |
| `semantics` | 能力解释与版本化 lowering、fallback、同步、生命周期和 timing 决策 | 输入为值语义；不持有资源、不分配 owned object、不访问运行时对象图、不执行 GPU 工作 |
| `backends/{null,vulkan,metal,webgpu}` | native 查询、payload、准备、录制、提交、完成和回收 | 消费明确的 lowering；不得反向定义 L2 或向上泄露 native 细节 |
| `conformance` | 四后端契约验证和证据工具 | 不替代真实 native 或目标平台证据 |

依赖方向：`Frontend 类型与纯校验 ← Semantics 决策 ← Frontend 运行机制`。Backend 可以消费三者。Renderer 与 RenderSystem 通过公共 API 表达 intent；Semantics 不包含 renderer/platform/native 头。平台生命周期值模型保留 `arp::platform` 的既有类型名，其定义与状态转换不依赖平台运行时。

Frontend 用 `device_*`、`resource_*`、`memory_*`、`descriptor_*`、`pipeline_*`、`command_*`、`transfer_*`、`frame_*`、`bindless_*`、`query_*`、`telemetry_*` 前缀组织。Command 的主要聚合为 model、arguments、sources、usage、state、recording；resource lease 归资源生命周期。Transfer 按 copy、upload、readback 组织。必要的叶子模型保留独立文件，避免聚合引入 include 循环。

公共 Device 与 Pipeline 保留原有声明、访问权限、默认参数和存储。inline/template 算法分别位于 `frontend/device_device.inl` 与 `frontend/pipeline_interface_validation.inl`，由公共头引入，上层不得直接包含实现片段。cache 等类的声明归公共头，算法仍在 Frontend `.cpp` 或 `.inl`。

四后端必须保留 Native、Equivalent/Emulated、显式 Prototype/Legacy opt-in 或 DiagnosticFailure 的确定性结果。Null Dummy sync 仅用于验证；Metal `.mm` 与非 Apple stub 分离；准备、录制、提交、完成回收不得混为一个生命周期。allocator、预算、ownership、稳定诊断、Shader/矩阵契约均不因移动或合并改变。

机械守护使用 `tests/contract/rhi_package_layout_test.py`、`rhi_abstract_root_inventory_test.py` 与 `command_recording_api_inventory_test.py`。行为源码测试读取职责区段并仅跟随该区段实际调用的决策函数；分层负向断言独立读取原始源码。迁移来源、合并区段及拆分决策记录在 `rhi-package-file-map.yaml`。

## 验证状态

当前执行计划与本次证据以 [rhi-package-reorganization-plan.md](rhi-package-reorganization-plan.md) 为准。结构验收满足后声明 Contract Complete。Prototype Complete、Backend Validated、Platform Validated 和 Production Ready 必须由各自的实际证据支持，不能由源码结构或 Null 通过推导。

历史 2026-10-07 验收记录为 252 项源码检查、Host 173/173 与 Wasm 38/38，证据目录 `build/mac-metal-debug/artifacts/rhi-cleanup/abstract-root/`。这属于历史证据，不代表本次 Windows 迁移已完成 Apple 或浏览器验证。

## 扩展机制

`GpuDevice::queryInterface(extensionId)` 为显式可选扩展入口。ExtensionOptIn 不属于基础能力，不得通过默认 descriptor 或 capability apply 自动开启。
