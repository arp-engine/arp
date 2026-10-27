# ScriptComponent 与幸存者迁移

目标：自定义 ArpScript 类继承 `ScriptComponent`，挂载到真实 GameObject；共享世界/模块执行环境，实例字段持有玩法状态。保留 canonical Interpreter / DirectNative V5 ABI。禁止以单个组件转调旧 Run/数值表作为完成迁移。

| 阶段 | 工作与验收 | 状态 |
|---|---|---|
| C0 | 合同、现有能力审计、注册测试骨架 | Contract Complete |
| C1 | 通用载体、共享模块、按类型挂载/查询、owner/native component 访问；双执行模式一致性 | Contract Complete / Prototype Complete |
| C2 | 固定更新、事件、enabled/active、延迟销毁、错误与失效引用；生命周期与 Null smoke | Contract Complete / Prototype Complete |
| C3 | YAML 类型/版本/字段/局部引用、场景/Prefab 恢复、原子失败 | Contract Complete / Prototype Complete |
| C4 | 幸存者按职责拆分组件，移除数字表和 arp_runtime2d/专用宿主调度 | Contract Complete / Prototype Complete |
| C5 | 玩法/重开/存档回归、Interpreter 启动、AOT parity、RenderDoc、压力与独立包 | 内存专项通过；用户暂停压力测试，其他未验收项保持开放 |
| C6 | ScriptComponent 物理碰撞/触发器方法与 2D 物理事件批量桥接 | Contract Complete / Prototype Complete |

## 合同 1

- 原生 `ScriptBehaviorComponent` 演进为唯一载体；旧 name-based callback API 保持兼容。自定义组件拥有独立 reflection instance，脚本字段无需 get/put。
- 一世界/模块一执行环境。组件缓存已验证的入口；缺少可选 hook 是无订阅，未知类型、字段、版本或预算不足均诊断失败。
- 生命周期：enable → start（一次）→ update / fixed_update / late_update；inactive 或 disabled 不更新。固定步由现有 GameClock 调度。事件在物理步后派发，不在 Box2D 回调内执行脚本。
- 更新/事件期间的删除立即标记待销毁，并在安全边界执行 disable → destroy → 引用失效。当前阶段新建组件从下一次调度开始；不在 vector 遍历期间扩容迭代。
- `game_object` 指向真实 canonical GameObject；原生组件查询返回既有 canonical reflection reference。GameObject、组件引用的失效可观察，禁止复用旧引用指向新对象。
- YAML 复用 SceneComponentCodec。显式脚本资源 ID、类型、schemaVersion、typed fields；对象/组件引用以局部 ID 恢复，未知/错型/缺失引用拒绝。所有对象/组件建立后再恢复引用。
- Interpreter 与 DirectNative 采用相同 receiver、字段、hook 参数及事件顺序；禁止自动切换或解释器 fallback。构造期间不得执行生命周期。
- 2D 物理可选方法为 `on_collision_enter(other: GameObject)`、`on_collision_exit(other: GameObject)`、`on_trigger_enter(other: GameObject)`、`on_trigger_exit(other: GameObject)`。方法签名在加载时校验并缓存；物理步完成后按事件双方各派发一次，sensor 事件映射为 trigger。脚本不在 Box2D 回调内执行，不占用通用 Scene event queue；一批事件按 GameObject ID 解析，整批处理完成后才回收回调中销毁的对象。当前不提供 contact point/normal 或 stay/persist 回调。
- 原生保留资源/渲染/物理/音频/时钟等领域系统；脚本负责输入响应、AI、伤害、武器、升级、波次、HUD 与流程。
- 不修改 GPU/RHI 语义。Null 仅证明验证语义；Windows/Vulkan 需新的真实 smoke/capture。旧迁移结果不可作为本迁移验收。

## 状态与证据

组件基础 C0–C4 已达到 Contract Complete / Prototype Complete；Windows/Vulkan 新路径的画面已由真实 RenderDoc replay 和导出图片检查验证（仅渲染路径 Backend Validated）。C5 压力、重开内存与独立包仍需分别记录，不据此宣布整体 Platform Validated 或 Production Ready。原数值表迁移报告 `survivors-script-migration.md` 保留作历史证据；其 60 FPS 压力未通过，组件化不自动解决性能。

C0：已审计 canonical 类继承/Typed IR schema、VM 持久 receiver、Scene 工厂与 codec。注册 `arp_script_component_tests` / `arp_script_component_aot_tests` / `arp_script_component_null_smoke`。首轮 Interpreter 合同测试通过（独立字段、固定更新、inactive、Scene 内自销毁）；AOT 和后续 codec 验收仍在执行。旧 `arp_scene_script_behavior_tests` 通过。尚无本迁移的 GPU/平台或性能完成声明。

C1–C3：MinGW Release 构建通过，CTest 5/5（Interpreter、DirectNative、120 tick 无图形 Scene smoke、既有 ScriptBehavior、2D codec）。验证实例身份/独立字段、重复/未知类型拒绝、固定步和暂停、事件容量、更新中挂载/销毁、失效引用拒绝、脚本错误消息、YAML scalar/list/optional/object/native component/script component 引用、round-trip、错误版本与失效局部 ID 回滚。证据：`artifacts/survivors/component-build-19.log` 与 CTest LastTest.log。

加载字段快照时不执行构造函数；所有非 SDK 字段必须齐全，生命周期等首次 Scene 调度才执行。销毁按子对象在先、组件逆挂载序执行，回调追加删除在同一安全边界收敛。Scene 工厂按显式类型注册，多模块重名拒绝，已知脚本创建失败不能降级到原生工厂。原生引用测试同时修复反射 upcast 对无动态地址元数据的处理。

当前边界：挂载类型以显式字符串指定；同一对象同类型仅允许一个实例。序列化暂不支持 dictionary / 任意嵌套脚本 value object，遇到这些字段诊断失败；热重载、脚本 ResourceManager generation 生命周期与零分配热路径未完成验收。后续 C4 使用实际原生领域组件，C5 重新验证游戏画面与性能。

## C4 实施记录

已移除 `ScriptRuntime2D`、数值表服务、未使用的手工领域服务导出，以及对应旧 façade 测试。`arp_scene` / `arp_scene2d` 使用生成的 canonical reflection/plugin 绑定，直接暴露真实 GameObject、Sprite、动画、物理、Timer、Tween、文字、粒子和音频组件。领域组件 Interpreter / AOT 测试曾通过 2/2；最新完整构建与玩法回归仍在进行。

| 脚本类型 | 挂载对象和职责 |
|---|---|
| RunFlow | 局内状态、升级队列、原生 GameClock 暂停、局外记录规则 |
| PlayerController / Enemy | 玩家和每个敌人的移动、受伤、死亡、动画 |
| Projectile / ExperiencePickup | 每个弹丸的扫掠、穿透、反弹、命中历史；经验收集 |
| AutoWeapon / OrbitBlade | 自动武器原生 Timer 事件；绕父对象旋转 |
| WaveDirector | 波次、种子随机源、Boss |
| DamageNumber / Hud | 原生文字、Tween、像素画布、菜单和 HUD |
| MovingPlatform | 运动学机关 |

宿主只提交输入/资源/配置，调度 Scene、固定时钟和物理，提取渲染并处理平台持久化。默认 Interpreter；`--script-aot` 显式选择 DirectNative，源码变更必须通过 source identity gate。新配置为 YAML v3，删除旧表容量，显式指定 Scene/组件/事件容量。脚本构造器可访问 game_object；component 与生命周期在挂载后生效，同类型构造重入拒绝，构造期间已占用实例预算。

```arp
from arp_scene import GameObject, Component, Transform

class Spinner(ScriptComponent):
    def on_update(self, dt: float):
        var t: Transform = self.game_object.local_transform()
        t.set_translation(t.translation.x + dt, t.translation.y, t.translation.z)
        self.game_object.set_local_transform(t)

# 获得真实 GameObject 后：
# var spinner: Spinner = object.add_component("Spinner")
# object.get_component("Spinner") 返回同一个实例。
```

不重编译宿主的扩展测试已通过：向资源脚本加入 ScriptOnlyBurst 并挂载，复用真实 Timer 事件发射八方向弹丸、创建精英并连续升级；显式 AOT 拒绝修改后的源码。最终 EXE SHA256 前后相同，证据：`build/survivors-release/script-extension-proof/extension-proof.yaml`。

原生值类型字段遵守反射的按值返回合同。修改嵌套属性时先取值、修改、整体写回，例如 `var filter: CollisionFilter = shape.filter`、`filter.category = 4`、`shape.filter = filter`；Transform 平移可直接使用现有 `set_translation`。不把临时副本变成无生命周期保障的借用引用。脚本类实例字段仍按实例身份访问。

最新基础回归：CTest 7/7 通过，覆盖双模式生命周期、120 tick Scene smoke、真实物理/Timer/Tween、既有 ScriptBehavior、2D codec；新增不同类同名不同参数方法、无字段访问的失效方法拒绝、同 ID 不同执行环境重绑拒绝、构造重入拒绝。证据：`artifacts/survivors/components-foundation-tests.log`。完整玩法回归仍在进行。

C4 验收：真实物理多发/穿透/反弹/命中历史、数量/半径成长及上限、暂停、固定 seed 重复输入、对象销毁全部通过（`components-game-final.log`）。无 traversal 的世界坐标与父子变换、显式碰撞过滤在 Interpreter / AOT 领域测试 2/2 通过（`components-domain-final.log`）。完整 Interpreter 模拟运行 18000 帧、300.000 秒胜利，1755 击杀、22 升级、Boss defeated，记录重读含 wins=1 / bestSeconds=300（`components-full-run.log` / `components-full-save/record.yaml`）。

Windows/Vulkan 新路径已捕获并 replay 开局、物理区、升级、死亡、暂停、resize、60 秒战斗和强化武器；AI 已逐张查看导出 BMP。证据目录 `artifacts/survivors/components/captures/`，每条有效证据包含 RDC/BMP/YAML；真实 512×256 图集、96-byte 实例、UV/alpha/混合/文字检查通过，validationErrors=0。最终构建后的双模式回归、重开、脚本扩展、独立包与正式压力在 C5 继续执行。

## C5 验收记录（2026-10-16）

- MinGW Release 已构建。选中的 18 项 CTest 首轮 17 项通过；AOT hidden smoke 首轮超过 45 秒。明确将正确性 smoke 超时调整至 240 秒后，单项重跑 119.36 秒通过（`components-final-regression.log` / `components-aot-hidden-final.log`）。这不证明 AOT 性能通过。胜利退出时机修复后，7 项宿主 smoke 与脚本扩展复测全部通过（`components-host-final-smokes.log`）。
- Interpreter 与 DirectNative 生命周期、领域组件和玩法一致性通过；暂停、死亡、连续升级、武器数量/穿透/反弹、固定 seed 与失效引用有合同或玩法测试。最终解释器是默认模式，AOT 必须显式选择且严格校验源码身份。
- Interpreter Simulation 与 Vulkan 固定步各自完成 300 秒游戏时间、Boss 胜利；不是实时 60 FPS 证据。最新真实胜利结算捕获为 `artifacts/survivors/components/settlement-captures/victory_capture.{rdc,bmp,yaml}`，AI replay 并查看 BMP：胜利面板、1755 击杀、22 点数、1 次胜利，2791 实例、254 glyph、validationErrors=0。最初过早捕获的战斗 HUD 已在 `final-captures/victory_capture.state-diagnostic.yaml` 标记无效。
- 新 Interpreter 开局/强化菜单和 AOT 开局均已真实捕获、replay 并查看图片，见 `components/final-captures/` 与 `components/aot-captures/`；图集、UV、96-byte 实例与混合检查通过。
- Vulkan 和 Simulation 各完成 20 次压力填充后重开：43 objects / 125 components / 16 script instances、11 bodies / 10 colliders / 3 joints 回归基线，GPU 与 Box2D live-byte delta 为 0；core live bytes 相对第二次基线最终增加 7616 字节（第 4/8/16 次呈阶梯增长），尚未定位。不能宣布重开内存零增长通过。日志：`components-restarts.log` / `components-restarts-sim.log`。
- 临时离线诊断工具复用已编译的真实 GameRun 和静态库，同样重现 +7616 bytes，且进程整数缓存始终为 6049 项，排除本次增长来自整数缓存新条目；不以猜测修改生产 allocator/VM。证据：`components-restart-cache-probe.log`，诊断源和构建脚本保存在 `artifacts/survivors/components/`，不属于生产模块或 CTest。

正式组件版 Interpreter 压力已执行：1920×1080、Immediate 无 VSync、30 秒预热 + 180 秒采样，1000 敌人 / 2000 投射物 / 3000 拾取物，退出码 1、`performanceAcceptance: Failed`。154 个样本，帧 P95 **1292.3531 ms** / P99 **1323.0859 ms**，CPU P95 1292.3526 ms，GPU P95 6.6744 ms（154 个 RHI 完成帧样本）；最多 1 draw / 7293 instances。采样期 217,948,691 次 core allocation，core live delta +16,598 bytes、Box2D live delta +16,384 bytes；GPU allocated 3,404,288 bytes，core peak 74,320,685 bytes、Box2D peak 6,847,648 bytes。模拟仅推进 12.083 秒，积压 198.047411 秒被如实保留，未伪装成模拟已推进。证据：`artifacts/survivors/components-stress.log`。当前瓶颈在 CPU/VM 更新侧；不能沿用最初原生玩法的性能通过状态。

独立包已验证：解压到源码目录外的 TEMP，工作目录同样在源码外，PATH 仅 `C:/Windows/System32;C:/Windows`。默认 Interpreter 的真实 Vulkan 战斗与音频启动通过；新进程运行死亡流程后，原胜利记录 wins=1 / bestKills=1755 / bestSeconds=300 / volume=0.6 保留。证据：`artifacts/survivors/components/standalone-proof.yaml`、`audio-combat-standalone.log`、`record-restart-death-standalone.log`。包为 `artifacts/survivors/package/Copperlight-Survivors-Windows-x64-components.zip`，包含脚本源码、YAML、离线 shader、资源许可证和本验证文档。

| 成果 | 验收边界 |
|---|---|
| 自定义继承、挂载/查询、实例字段、生命周期 | Contract Complete / Prototype Complete；Interpreter / DirectNative 测试通过 |
| 原生物理/Timer/Tween/动画/文字/粒子/音频组件访问 | 合同与双模式领域测试通过；幸存者实际使用 |
| YAML Scene/Prefab typed fields、局部引用与失败原子性 | Contract Complete；不包含任意嵌套脚本对象/dictionary |
| 全玩法组件迁移、无宿主重编译扩展 | Prototype Complete；旧数字表 façade 已移除 |
| Windows/Vulkan 渲染 | Backend Validated（限定上述游戏新路径）；RenderDoc replay 和 AI 图片确认 |
| 独立启动、音频、窗口 resize、暂停、存档重启恢复 | 对应 Windows platform smoke 通过 |
| 300 秒游戏流程 | 固定步 Simulation / Vulkan 胜利通过；当前版本实时录像未完成 |
| 20 次重开 | O1 修复后真实 Vulkan 通过；core/Box2D/GPU delta 均为 0，见后续记录 |
| 1080p 1000/2000/3000 压力与热路径 | Failed；CPU/VM 分配与帧耗时超标 |
| 整体 Platform Validated / Production Ready | 不声明；C5 保持未完成，Metal/WebGPU 原生执行未重新验证 |

序列化限制、脚本资源 generation/热重载与 VM 分配热路径限制仍按前文记录。后续优化应保留脚本组件权威逻辑，通过通用 typed/bulk 组件访问与 VM/反射热路径减少逐对象 boxed 分配；不能重新引入原生敌人/武器规则来冒充脚本性能通过。

## C5 后续优化

| 阶段 | 工作 | 状态 |
|---|---|---|
| O0 | 复用 Windows 原生采样器，记录 CPU 热点与分配基线 | 完成：`components/profile-before/`，分配/释放与 allocator 锁为主要 self samples；非性能验收 |
| O1 | 定位重开 retained bytes，修复通用生命周期元数据重复注册 | Contract Complete / Windows Vulkan smoke 通过；20 次重开 core/Box2D/GPU delta 全为 0 |
| O2 | 复用既有 VM/反射快路径减少组件更新分配 | Contract Complete / Prototype Complete；10 项回归通过，性能验收按用户指令暂停 |
| O3 | 普通玩法连续重开与完整世界销毁的内存回归 | Contract Complete；Windows/Vulkan 内存专项 smoke 通过，完整对局复验通过；压力暂停 |

O1：17 条原生继承边每个世界注册一次，原 `class_info::inherit` 不去重，两个进程级 vector 容量增长精确解释 +7616 bytes。相同 base/cast 绑定改为幂等，空/self 继承和同 base 不同 cast 明确诊断拒绝。反射、组件双模式、Null smoke、领域双模式、玩法测试 7/7 通过（`components-o1-tests.log`）。真实 Vulkan EXE 压力填充后重开 20 次，每次稳定点 core/Box2D/GPU delta 为 0（`components-o1-restarts.log`）；诊断程序的 bases size 始终为 1（`components-o1-probe.log`）。此前失败记录保留作历史，已由该修复关闭。

O2 诊断复用真实 GameRun、core allocator 和现有 PE 符号解析器；仅 artifacts 下的临时 allocator 包含采样，生产代码不包含插桩。4 个固定步和提取产生 1,482,057 次 core allocation，按每 1021 次抽样，1451 个有效栈；反射 setter/getter 临时 vector、默认 undefined、class_of 原生名称字符串、owned value 转指针时再装箱均存在直接栈证据。详见 `components/allocation-profile-symbols.txt`；采样不是性能验收。

O2 实现复用原有固定 arity invoke；完全匹配的小调用用栈数组，缺省参数和较大参数仍走原 variadic ABI。原生 exact owned value 转指针直接返回同一个 payload 地址；原生控制、继承和限定指针的转换路径保持原合同。已发布 class_info 复用进程生命周期缓存，注册事务中的 staged class 不进入缓存。属性查找、生命周期结果和反射调用结果使用已有 undefined，静态方法 arity 查询使用已有 parameter_count()。没有关闭 quota、降低预算门槛或把玩法移回原生。

完整 MinGW Release 重编译后的 10 项 CTest 全部通过：反射注册、原生对象、原生 Scene 生命周期 smoke、语义层、组件 Interpreter/AOT/Null、领域 Interpreter/AOT、幸存者真实物理玩法（`components-o2-final-tests.log`）。新增测试包括 warmed setter/static call/owned pointer/type lookup 零临时分配、0/5/6 参数、默认参数、参数数拒绝、失效引用、注册事务回滚后重新提交与缓存身份。

O2 最终一致构建的相同 4 固定步/提取诊断降至 **783,301** 次分配，比 1,482,057 次减少 **47.15%**；这是局部工作量对比，不是帧率验收（`components-o2-final-allocation-profile.log` / `components/allocation-profile-o2-final-symbols.txt`）。O2 当时 EXE SHA256 为 `6f816ad2fdf06b76512036f69bb672d9f7f9f00f316991ab43af8bef7ba6bdfa`。O3 宿主 CTest 9/9 通过，含 AOT/Interpreter、菜单/暂停/死亡/升级/resize、Null 和不重编译宿主扩展（`components-o3-host-tests.log`）。

### O3 内存专项（2026-10-16）

按用户“先不做压力测试，确保没有内存泄漏即可”的指令，停止后续压力填充/性能采样及优化验收。旧 `--restart-check` 会调用 `populateStress()`，本轮没有重跑；新增独立 `--normal-restart-check N`，只运行普通波次/武器/拾取，每轮 180 固定帧（3 秒）。两次预热后，在新世界菜单稳定点核对对象/组件/脚本实例和物理 body/collider/joint 计数，以及 core、Box2D、GPU 存活字节；任意偏差均失败。仅在内存检查的冷边界等待真实 GPU 提交完成。

- 普通玩法完整销毁：`arp_survivors_memory_tests` 通过。两次预热后，20 轮战斗、连续升级、暂停、死亡、世界销毁；每轮 core live bytes delta=0、outstanding allocation delta=0。
- Windows/Vulkan：`--normal-restart-check 22 --hidden --mute` 退出码 0，预热后 20 轮 core/Box2D/GPU delta 全为 0，场景与物理计数回归基线。每局 3 秒，实际 10 敌人、5 投射物、2 拾取物、2 击杀，未使用压力人口。
- CTest 快速路径 3/3 通过：内存单元、Null smoke、Vulkan 普通重开 smoke；8.41 秒。
- 完整 300 秒对局销毁：`arp_survivors_full_memory_tests` 已注册 CTest；首轮未通过。两局均 300 秒胜利、1755 击杀、22 升级、Boss defeated，但第二局销毁后相对第一局 +15,552 bytes / +189 outstanding allocations，448.69 秒后断言失败。当时未忽略增长，使用现有 allocator ledger 继续定位；诊断、修复及最终零增量复验见后文。历史失败证据 `components-full-memory-tests.log` / `components-full-memory-failure-details.log`。

证据：`artifacts/survivors/components-normal-memory-build.log`、`components-normal-memory-tests.log`、`components-normal-memory-details.log`、`components-normal-memory-restarts.log`。本轮为 Interpreter；Vulkan 回归显式静音，不据此覆盖音频设备/驱动的全部分配，也不以 Windows 工作集或运行中的世界峰值判定泄漏。无新的 shader/画面变更；中止了旧 O3 胜利捕获链，未将该次未完成捕获列为验证通过。压力性能、当前完整实时录像、新版独立包及四后端 Production Ready 保持未验收。

本轮普通重开验证 EXE SHA256：`e97b6b6996f2338d05d6c6ab8298e394a38ebf35f49b6ea06c3be798b0776074`。

完整对局诊断复用原 allocator ledger，三局均胜利。第一局销毁后 4,859,835 bytes / 43,663 allocations；第二、三局均 4,875,387 bytes / 43,852 allocations。整数缓存始终 18,002 项，第二、三局新增 0，因此不能把 +15,552 bytes 归咎于整数缓存。增长在第二局后停止，属于旧元数据被额外持有的一次性增长，未观测到持续线性泄漏。证据 `components-full-memory-probe.log`，诊断程序仅位于 artifacts，未改生产 allocator。

根因复查发现 `class_info::def` 只在特殊方法缓存为空时更新 `__new__` / `__init__` / `__del__` / `__getitem__` / `__setitem__`，重新注册时方法表更新而缓存仍保留首次绑定。已修复为缓存跟随当前定义，释放旧 callable；不会清除在用 VM 整数缓存或改 allocator 计数来抵消增长。新增合同测试覆盖五种绑定清除后捕获对象释放、构造调用与方法表一致；修复前 `previous.expired()` 必现失败（`components-special-rebind-before.log`），修复后快速 10/10 通过（`components-memory-final-tests.log` / `components-memory-final-details.log`）。

最终复验通过：两局完整流程均 300.000 秒、1755 击杀、22 升级、Boss defeated；第二局完整销毁后相对第一局 core live bytes delta=0、outstanding allocation delta=0，原 +15,552 bytes / +189 allocations 已消除。CTest `arp_survivors_full_memory_tests` 通过（459.50 秒，非性能采样），证据 `components-full-memory-fixed-tests.log` / `components-full-memory-fixed-details.log`。修复后 22 次真实 Vulkan 普通重开也通过，预热后 20 次 core/Box2D/GPU delta 全为 0，计数回归基线（`components-normal-memory-fixed-restarts.log`）；短周期 20 次完整销毁同样为零。原有多发/穿透/反弹/命中历史、成长上限、暂停和 seed replay 回归再次通过（`components-memory-final-gameplay.log`）。

汇总 `artifacts/survivors/components-memory-verification.yaml`；最终内存回归 EXE SHA256：`a7270169abc206a8c9bf45faa74c1a26f5bfdaf3d693bb6cdc6a9622c9640d1b`。本阶段内存合同与 Windows/Vulkan 内存 smoke 已完成，测试覆盖范围内未发现泄漏；按用户指令停止在该范围，不恢复压力测试/性能优化，不将专项通过升级为整个引擎 Production Ready。


### M0 场景集成文件归属整理（2026-10-16）

按用户指定，将 `script_component_module.h/.cpp` 和 `script_component_codec.h/.cpp` 移至 `src/scene/`；同步生产、测试 include 和 CMake source list。保留 `arp_script_components` 独立集成 target 及现有公开类型命名空间/API，避免将 VM 依赖引入 `arp_scene` 核心 target。`script_session.h` 与 `script_value_yaml.h` 保持原位。阶段状态：Contract Complete（本次目录迁移与集成回归）。MinGW Release 组件与领域测试目标构建成功；Interpreter/AOT 组件与领域合同、Null smoke 共 5/5 通过。证据：`artifacts/survivors/components-scene-move-focused-build.log` / `components-scene-move-tests.log`。完整 `arp_survivors` 构建尝试被本次范围外的 `src/shader/spirv_wgsl_buffer_memory_lowering.h:73` 编译错误阻断：`Plan plan = {nullptr, 0u}` 无法转换为 `Plan`（`components-scene-move-build.log`）；未修改该文件，不声明本次完整游戏构建或新增 Backend/Platform 验证通过。无行为或可视输出变更，压力测试维持暂停。

### C6 物理事件脚本方法（2026-10-17）

`ScriptComponentModule` 在模块加载时验证四个可选物理方法的 `GameObject` 参数并缓存 Interpreter/DirectNative 入口。Scene 接收有界事件批次，以对象 ID 重新解析双方；执行遵循 Scene active/enabled/started 状态和组件预算，整批派发结束后统一刷新延迟销毁。幸存者宿主在 Box2D step 与姿态同步后映射 contact/sensor begin/end，并向双方脚本分别发出回调；通用 `on_event` 队列和容量不受影响。

MinGW Release 组件及领域 Interpreter/AOT/Null smoke CTest 5/5 通过，覆盖四类回调、双侧分发和回调请求销毁时的延迟回收；`examples/survivors/game.cpp` 使用相同 Release 编译器独立编译通过。完整 `arp_survivors` 构建在 AOT 生成步骤被既有 `TweenComponent.value` 间接调用 CFG 合同拒绝，故不声明本次端到端游戏 smoke 或 Platform Validated。状态：脚本/Scene 合同 Contract Complete；幸存者宿主桥接 Prototype Complete。无 shader 或可视输出变更；不运行压力测试。
