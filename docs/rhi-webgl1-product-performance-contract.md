# WebGlProductPerformance/1

本契约为 P4-19 的观测路径，不授予 execution、completion 或 readiness 权限。依赖方向为 core CPU measurement → renderer graph/platform measurement → 测试采集 → tooling 报告；RHI 不依赖 renderer、浏览器报告或最终审计。

## 观测与边界

- 同一冻结 Release JS/Wasm/data/HTML，原 Scene2D、GUI、BasicForward→离屏→GUI workload，原资源和 shader publication 不变。两尺寸为 64×64、48×32；BasicForward 的原离屏/depth 资源仍为 64×64，不冒称随 canvas 缩小。
- 原 fixture 的错误参数、重复 slot、卸载被引用资源等主动负向调用留在 cold/独立回归；warmup/steady 只执行实际产品 workload。每帧明确记录 `fixtureValidationChecks`，稳态携带这些主动错误调用的报告拒绝验收。原像素 oracle 和生命周期检查继续执行。
- YAML 明确 warmup、sample、repetition、deadline 和 CPU 预算。独立新浏览器进程重复，原始逐帧数据全部保留；不剔除慢帧，不合并不同二进制。报告 median/p95/max、所有超预算样本，冷 setup/compile/首帧独立列出。
- opt-in 的固定标量 CPU measurement 复用 core allocator stats 和 steady_clock。关闭观测时不读取时钟或 allocator；观测不能改变 payload、资源状态或执行决定。
- `build` 为原 workload 建图/动态输入上传；`compile` 为 graph 校验、编译、资源解析；`prepare` 为原 issuer 准备命令存储；`record` 包含原 pass callbacks 与 finish；`submit` 仅原 queue/ordered issuer 调用；`presentRetire` 为原 present 和 CPU owner 回收；`total` 为完整 renderFrame。嵌套时间不相加冒充整帧，工作负载计划构建另列。
- 每 scope 记录 allocation/deallocation 次数、live bytes 前后和进程累计 peak；peak 是 core allocator 历史高水位，非当前 scope 峰值、非 JS heap、非物理 GPU 内存。frame/command quota 峰值单列。
- 原 WebGL telemetry 的 GL 调用、draw、uniform/update payload bytes 与独立 JS interceptor 比较。interceptor 自身和报告序列化在 CPU 计时外；interception wrapper 的执行开销仍包含于被观测 frame，明确报告。WebGL bridge 数定义为实际 WebGL API 调用数，排除 isContextLost predicate，不冒称 wasm↔JS 所有函数调用数。
- 生命周期 `delete*` 调用由 interceptor 独立计数。最终 renderer drop 帧的原生删除不混入 draw/update/present telemetry。冷 setup 的资源创建/query 与 shader compile/link 单独保留原始调用记录。
- 原 issuer prepare 中的 native resource 创建不在 execution telemetry 的既有计数域中。复用 core scope observer 标记真实 prepare 边界，独立计数 `preparationNativeCalls`；总 GL calls 精确等于 execution telemetry calls + preparationNativeCalls + nativeDeleteCalls。不根据后端名、预期固定次数或 hash 补数。
- steady 样本内的 readPixels/finish、shader compile/link、native object 创建均记录为 hotPathFindings，不作为达标结果。像素 oracle 在采样前后执行，未测每一帧 GPU 完成；requestAnimationFrame 等待与 CPU renderFrame 分开，不以 rAF 推断 GPU 时间。
- GPU timer unavailable 时报告 null。预算仅判断指定 workload 的 CPU 时间；即使所有 CPU 样本达标，也不宣称 GPU/移动端/小程序帧预算达标。
- cold setup 是原产品 artifact/resource/pipeline 准备与 fixture 冷校验的 CPU 区间；`compileCpuMs` 是 compileShader/linkProgram 同步 API 调用耗时，不能代表异步驱动全部编译成本。导航与资源获取时间另存原始 Navigation/Resource Timing，Wasm compile 无独立可靠测量则为 null，不拿下载时间充当编译时间。

## 五 backend 结论

| backend | CPU observation | GL execution counters | 输出/权限 |
|---|---|---|---|
| Null | core measurement 与 hidden-frame 路径 | NotApplicable | validator only |
| Vulkan | 同一中立 CPU scope 可用 | NotApplicable | 原 native gate 不变 |
| Metal | 同一中立 CPU scope 可用；Windows 只 host/stub | NotApplicable | Apple native 仍需独立验证 |
| WebGPU | 同一中立 CPU scope 可用 | NotApplicable | completion/copy/map 契约不变 |
| WebGL | 原 ordered Prototype renderFrame CPU scope | 原 telemetry + JS 独立观测 | 不提供 GPU completion/readiness |

所有路径均不修改 capability/fallback/lowering。非法参数、缺阶段、报告丢样、原 telemetry 与 interceptor 无法按明确边界对账、失败/部分提交或资源账本漂移必须拒绝报告验收。稳态原生创建、阻塞读回与 CPU 超预算保留为负结果，禁止宣称性能达标，不将其改写成结构错误或重测刷绿。

状态：契约定义完成后由快测、Null hidden-frame、真实 Release browser 与原 RenderDoc 产品路径验收。完整 P4-19 只能在三场景/两尺寸/多次运行的真实报告后关闭。
