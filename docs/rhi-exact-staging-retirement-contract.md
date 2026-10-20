# ExactStagingRetirement/1

状态：Contract Complete（仅 exact staging ownership/retirement 范围）；生产 WebGL 与完整 P0-10 未完成。

## L2 不变量与实施顺序

上传 staging reservation 从录制开始属于其唯一 command closure。未提交、被拒绝和
取消的录制仅能撤销自身 exact reservation；queue accepted 只转交托管，不表示完成。
只有该 command 的真实 completion，或同一 runtime 成功 device-idle，才能回收其
accepted reservations。较大数值、另一个 command/runtime/queue/swapchain 的完成均
不构成授权。完成 epoch 只作为容量重试提示，不是可回收 serial prefix。

先建立此契约和真实乱序 negative test，再修改 backend。复用
`FrameRetiredPageSliceAllocator::cancelAllocation` 的 exact matching 算法及
`RecordedStagingUploadJournal`，不另造 serial tracker。ledger owner 必须独立于
device wrapper，并由 command 保留到真实完成；不得保存悬空 pool/allocator 引用。
staging native page payload 仍由 command 保留。无新增 wait、每帧编译或预算扩大。

## 五 backend lowering

| Backend | 结论 | 执行与边界 |
| --- | --- | --- |
| Null | Validator | 只验证 exact cancellation/ownership，不签发 native completion |
| Vulkan | Native exact retirement | 原 fence / 同 runtime 成功 idle；pending 保留，独立于 caller frameSerial |
| Metal | Native completion closure；未证明部分 DiagnosticFailure | 维持原 command callback 路线，本次不认证 Apple 运行；不得使用 Vulkan watermark |
| WebGPU | Native completion closure；未证明部分 DiagnosticFailure | 维持原 queue/callback 路线，本次不认证 browser；不得使用 Vulkan watermark |
| WebGL | DiagnosticFailure | 生产执行仍关闭，未来 ordered custody 不得签发 GPU completion |

显式 synthetic Vulkan fixture 只能作为 validator，不用其证明 native。exact completion
接口仅 backend 内部使用。Vulkan pool 的旧 prefix retirement 入口删除；core 的通用
ordered-prefix helper 保留其完整 prefix 前提，不能再用于该 pool。交换链生命周期独立审查，不能由
上传完成解锁 frame slot。其余 backend 未验证的路径不因本次改动提升 readiness。

## 测试和预算

- 真实 Vulkan：先录制 A，提交/完成 B，再录制 C，A 的 slice 必须保持 live。
  同 runtime、不同 runtime，以及 timeline 阻塞的 A 均覆盖；所有失败断言前安全
  signal/wait/清理。保留实际 red 和 green 日志及输入哈希。
- core/journal：exact completion 不回收其它 slice；双完成/错误 identity 不推进 epoch；
  cancellation 不伪造完成；pool wrapper 销毁后 ledger lease 仍安全；复用现有容量限制。
- 运行原 completion/retention/profile、Null hidden-frame；Vulkan smoke 和 RenderDoc
  回放保留报告与 AI 图像检查。未验证的平台继续列 open gate。

本增量不关闭完整 P0-10，不提升生产 WebGL 状态。全局 timing label、交换链 present
completion、全部预算/热路径和其它消费者继续按主计划推进。


## 当前实施与 CPU/native 定向证据

`verified-validator-migration/` 实际 MinGW 构建及13/13 CTest通过，38项命名输入一致。
覆盖 core allocator、owned journal、retention/profile、Null hidden-frame、旧 Vulkan
pool/device validator、原 native completion/shutdown，以及同 runtime 乱序、跨 runtime、
timeline pending 和 pool-wrapper 销毁四个真实 Vulkan 模式。A+C live slice 为2，
取消 C 后为1，A取消/真实完成后为0；取消不推进 completion epoch。pool-wrapper模式
真实GPU copy写入确定uint32值并在原fence完成后读回相等；page保留及最终释放均通过。

旧device validator中的强制清空断言和synthetic设备宣称hardware graphics/compute、
push/secondary/barriers与memory能力的断言已迁移为禁止路径；不放宽生产capability。
`verified-final/` 的根CMake变化与旧清空断言失败、`verified-reconciled/` 的旧synthetic
capability断言失败均保留，不能当作成功轮次。初始green-first因实现期间输入改变而
在执行测试前停止；其产物不作为验收输入。

native page payload保留VMA state，ledger不保留pool/allocator引用；native command
仍持有其原runtime。复用core non-recycling identity，耗尽返回
`RHI-VULKAN-STAGING-RESERVATION-IDENTITY-EXHAUSTED`，不循环复用identity。
新lease失败返回`RHI-STAGING-RECORDING-LEASE-FAILED`；非法journal移交返回
`RHI-VULKAN-STAGING-RECORDING-OWNERSHIP`。沿用原pool容量和原retirement容量，无新wait。

本地Native定向测试仅证明上述Vulkan范围，Metal host编译不认证Apple执行，WebGPU
host编译不认证浏览器。本批最终 source checkpoint 与 RenderDoc/AI 视觉回归记录以
rhi-webgl1-validation.md 及证据目录review-checkpoint.yaml为准；仍不关闭完整P0-10。
