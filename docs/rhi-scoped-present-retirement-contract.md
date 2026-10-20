# ScopedPresentRetirement/1

状态：限定 Contract Complete；Vulkan scoped completion/idle gate 为 Backend Validated，
Windows/MinGW/RTX 3050 具限定 Platform Validated 证据。完整 P0-10、P0/P1–P4、
生产 WebGL 和 Production Ready 不因本项关闭。无 maintenance 的真实 WSI 未被本批证明。

## L2 不变量

frame slot 的可复用性必须同时满足该帧 graphics 引用结束和该帧 present 对同步对象的消费结束。
Recorded、Accepted、Presented、GpuCompleted 是不同事实。上传 reservation identity、CPU
frame age、其它 command/runtime/swapchain 的完成及全局最大 serial 不授权本帧回收。
同一 acquired frame 的 binary acquire/render-finished 对只允许一次 graphics submission。
第二份已录制 command 也必须在原生提交前拒绝；重复取得 image wrapper 不得重置提交事实。

## 五 backend lowering

| backend | 本契约决定 |
| --- | --- |
| Null | DiagnosticFailure for native completion；Validator-only，现有逻辑 slot 规则保持 |
| Vulkan | maintenance 路线保留原 exact present fence；无 maintenance 路线见下述精确 graphics + 成功 present queue idle |
| Metal | 原生 MTL completion 为 Native；无原生 payload 则 DiagnosticFailure。本增量不改变 drawable 路线，不消费 Vulkan frame state；不得由 present/CPU cleanup 推导 completion |
| WebGPU | 原 queue callback 与 backend presentation 托管的既有等价路径为 Equivalent；缺原 payload/完成则 DiagnosticFailure。本增量不改变该路线；callback 注册和 present 不等于完成 |
| WebGL | 未实现生产执行，DiagnosticFailure；OrderedIssue 不能签发本契约 completion |

本表不为 Metal/WebGPU 或 WebGL 增加可用能力或平台验证。

## Vulkan private payload

每个 acquired frame 保留独立 FrameSyncState，绑定原 runtime owner。仅原 native command
可以记录 graphics completion；观察来自该 command 的真实 fence 成功或匹配 runtime 的
成功 device idle。synthetic runtime 不写 native graphics completion。状态保留在 slot/image
闭包中，command 以 weak reference 回指，避免循环；command 销毁后已签发的完成事实仍保留。
state不可复制或赋值，防止复制完成事实或重置已提交身份。同一 owner/queue 的访问继续
遵守已有外部串行化要求，不新增线程安全承诺。变更只涉及backend-private类型，需同版本
重编；不增加L2 native handle、公共native completion setter或旧binary兼容承诺。

无 maintenance 的 backend-private retirement poll：

1. runtime 为空、synthetic、owner 不匹配时 Failed，绝不执行 queue idle。
2. 尚未提交或原 graphics completion 未证明时 Pending；可非阻塞查询原 command fence。
3. graphics completion 只准进入 present 消费检查，不直接释放 slot。
4. 缺 `vkQueueWaitIdle` 或 present queue 时 Failed；原 queue idle 返回非成功时 Failed。
5. 只有原 present queue 的 `VK_SUCCESS` 才返回 Ready，由 swapchain 清理该 slot。

失败保留 slot、sync state、semaphore 与 present ownership；acquire 传播稳定诊断。
不因另一个空闲 slot 存在而掩盖 retirement error。frame semaphore 的未提交取消、present
recoverable failure、surface loss、resize/destructor 的全路径另需审查，不能据本项宣称完整。

诊断：`RHI-VULKAN-PRESENT-RETIREMENT-OWNER-MISMATCH`、
`RHI-VULKAN-PRESENT-RETIREMENT-NATIVE-UNAVAILABLE`、
`RHI-VULKAN-PRESENT-RETIREMENT-IDLE-UNAVAILABLE`、
`RHI-VULKAN-PRESENT-RETIREMENT-IDLE-FAILED`、
`RHI-VULKAN-PRESENT-RETIREMENT-DEVICE-LOST`、
`RHI-VULKAN-SWAPCHAIN-SYNC-ALREADY-SUBMITTED`。
失败创建/record/submit 不伪造 accepted/completed；重试只在原 runtime/slot 存活且原失败
条件消除后进行。device loss 不重试为成功，恢复必须走既有 device/surface 生命周期。

## 预算与限制

每 frame state 的固定字段随已有 core::makeShared allocation 计量；不新增 queue、延迟 N
帧、无界对象表或热路径分配。原无 maintenance 路线仍使用阻塞 queue idle，这不满足热路径
无阻塞要求；本项只纠正其完成凭据及失败处理，不将它提升为 Production Ready。
后续须评估按真实 swapchain image 持有 present semaphore、reacquire 证明及完整 discard/
present-failure/resize/loss 契约，不能以禁用基础呈现或伪 fence 代替。

## 验证计划

- 先在现有 device contract 中禁止 swapchain 消费全局 completed serial，保留真实 red。
- native headless fixture 使用真实 binary semaphore、timeline 和 command fence：原帧 pending
  时其它 runtime/command 完成不得签发 graphics completion；原 fence 完成后保留事实。
- dispatch fault injection 只注入失败，不作为 native 成功证明；缺入口、idle error/device lost、
  错 owner、重复 frame submit 必须在副作用前拒绝，成功 idle 调用真实 queue。
- MinGW focused contract/native、原 staging 回归、profile、Null hidden-frame；原 Vulkan WSI
  hidden smoke 和 RenderDoc capture/replay/AI image inspection。headless 测试不证明 WSI
  present engine 或 no-maintenance WSI 平台全覆盖。

## 验收记录（2026-10-17）

证据根目录：`D:/arp/build/webgl1-evidence/p0/scoped-present-retirement/`。
最终接受`verified-after-wsi-build/`与`native-reconciled/`，检查点见
`review-checkpoint.yaml`。实际MinGW构建、定向native和14/14 CTest通过，52命名输入
一致；真实WSI hidden smoke、RenderDoc捕获/回放通过，93命名输入在构建前后及执行后
一致。三类依赖各4次，零validation message。AI实际查看新`present.png`，320×180全幅
蓝色clear，无黑边或局部覆盖；独立`ai-image-inspection.yaml`绑定图像/capture/report哈希。

定向native使用两个真实runtime和timeline阻塞command，证明错误owner、重复已录制
command在native submit前拒绝、其它runtime完成不授权原帧、原command销毁后完成事实
仍在，以及缺失/失败idle拒绝。成功idle调用真实queue；lost注入只证明失败门禁，不是
真实device loss或presentation engine测试。synthetic完成不写native graphics completion。

`red/`是禁止全局serial消费的真实源码contract red，不是GPU red。fixture退出崩溃、
旧断言、stale archive、共享checkout缺文件和输入变化轮次均保留。首次`native-final/`
四步均exit0并已查看图像，但根CMake在build中变化，未被接受为稳定最终快照。

9份C++的锁定21.1.8格式/dry-run及此前token保留记录齐全。实际构建位于`D:/arp`，
未移动CMake cache。隔离worktree的87项命名源码/配置比较中，81份C-family文本在
CRLF/LF规范化后相同；根CMake另新增独立push-memory测试注册，实际差异与patch保留。
因此不宣称隔离worktree整体已独立构建。详见`source-bridge-after.yaml`。
完整取消/失败/surface/析构、真实native owner及阻塞热路径按主计划第17节L1～L6继续。
