# ShutdownCompletionRetention/1

日期：2026-10-16。状态：Contract Complete（仅shutdown cleanup不伪造完成的范围）。范围为P0-10已存在的completion/lifetime不变量，
不新增GPU能力、不改变L2 submit结果、shutdown deadline或公共ABI。

## L2与lowering不变量

销毁device wrapper、调用CPU cleanup、停止接收新请求或释放caller owner都不是GPU
completion。已接受且可能访问native资源的command必须由backend保留原closure与charge，
直到该command的真实completion来源满足既有契约；不能按是否使用某一pipeline或
originalBindlessCustody决定保留。CPU cleanup不签发completed serial、publication或
staging可复用权限。未完成工作继续占用已有有界retirement容量，不临时扩大预算。

非阻塞shutdown cleanup先回收真实已完成工作，再保留所有其余工作；不增加GPU wait、
无限host wait、frame-age推断或Dummy completion。deadline drain继续走独立原API并
传播真实失败。device loss不是成功完成，不因loss发布成功结果；各已有loss闭包按其
独立版本化契约处理。这里不重新定义任何尚未证明的loss回收路径。

## 五backend决定及验证边界

| Backend | 本不变量的lowering结论 | 本增量处理 |
| --- | --- | --- |
| Null | Validator；不签发native完成 | 复用Null hidden-frame与既有retention测试，不用其证明native |
| Vulkan | Native完成仅来自原fence或匹配runtime的成功device-idle；否则DiagnosticFailure/pending retention | 修复void shutdown cleanup对非custody pending command的清空及serial推进；不更改真实完成issuer |
| Metal | Native完成需要原command状态/handler；CPU析构无等价完成来源 | 保留既有路径；本增量无Apple-native验证，不提升平台状态 |
| WebGPU | Native完成需要原queue/error-scope及相应map callback证据；CPU cleanup无等价完成来源 | 保留既有callback custody；host编译不构成browser证明 |
| WebGL | 当前生产执行及native GPU drain为DiagnosticFailure | 保持拒绝；未来ordered host custody不发布GPU完成 |

Vulkan无返回值cleanup只保留pending，不伪造成功report。已有deadline API中的
`RHI-VULKAN-COMPLETION-DEADLINE-EXPIRED`、`RHI-VULKAN-SHUTDOWN-DRAIN-INCOMPLETE`
等诊断保持原含义；caller必须继续使用该API取得有界drain结果。恢复条件是获得原
command的真实完成后重新退休；不能靠重复cleanup或caller数值推进恢复。

## 测试和实施顺序

复用`tests/fixtures/vulkan_headless_runtime.h`及现有
`tests/integration/rhi_vulkan_completion_tests.cpp`，构造真实timeline阻塞的
non-custody command。先验证shutdown cleanup不能推进completed serial、不能丢失
backend retention，然后signal真实timeline、等原fence并安全清理；失败断言在native
清理之后执行，避免负向测试自身丢弃仍被GPU访问的资源。

先保留真实red，再修复native实现。最终需要MinGW contract/retention/Null测试、该
真实Vulkan定向测试及hidden smoke；如复用可见clear smoke，则继续保存RenderDoc
捕获/回放及AI图像检查。所有源、测试、构建输入与旧失败证据分开记录。
本契约不关闭整个P0-10，不提升生产WebGL或Metal/WebGPU目标平台状态。

## 实施结果与证据

证据根目录：`build/webgl1-evidence/p0/shutdown-completion/`。

- `red/`：实际RTX 3050/Vulkan pending submission，shutdown前serial为0，错误路径后为1，
  backend retention为false而GPU仍pending；安全signal/等待/清理后断言退出3221226505。
  实际MinGW构建成功，29命名输入相同。未以mock/Dummy代替native。
- 修复仅让cleanup在正常退休之后保留所有剩余command；删除非custody分支的completed
  serial推进和强制清空，不新增等待/分配或改变公共API。
- `verified-reconciled/`：真实pending测试变为before=0/after=0/retained=1/pending=1，
  原timeline释放后正常完成；5/5 CTest含完整Vulkan completion、新注册shutdown模式、
  retention、profile及Null hidden-frame通过。29项命名输入完全一致，保存副本和binary hash。
- `native-reconciled/`：真实Vulkan hidden smoke、RenderDoc捕获、回放均exit0，70命名
  输入在build前后及执行后相同；三类依赖各4次、validation message为0。
  AI已查看`present.png`，320×180全幅蓝色clear；`ai-image-inspection.yaml`绑定图像、
  capture、report和manifest哈希。原`native-final/`虽四步exit0，但根CMake运行中变化，
  保留为未通过一致性门槛的历史轮次。
- `format-report.yaml`：clang-format21.1.8两份本批C++文件`-i`/dry-run通过，token和
  字节不变；scope及接受检查点见`review-checkpoint.yaml`。

Backend Validated仅覆盖Vulkan pending shutdown retention；Platform Validated仅覆盖
上述Windows/MinGW/RTX 3050的定向执行与smoke。Production Ready为false，未交付新
WebGL执行路径。全局completed serial对多runtime/queue的完成水位、其它loss/cancel
消费者、可选feature来源和全部资源预算仍需独立审计，不能由本次cleanup修复推导通过。
