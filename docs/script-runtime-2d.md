# ArpScript 2D 运行服务 v1

> Legacy design: this page describes the removed numeric-table facade and is
> retained only to explain older assets. The current interface attaches custom
> `ScriptComponent` classes to canonical `GameObject` instances; see
> [Script Components](script-components-plan.md) and
> [幸存者使用说明](../examples/survivors/README.md)。下文仅保留旧迁移上下文。

`arp_runtime2d` 通过标准 ModuleRegistry / reflection Value ABI 提供服务。实例绑定一个世界；脚本可以创建自己的类、表结构、系统和流程。`examples/survivors/assets/rules.arp` 是完整使用实例。

## 边界

脚本决定对象属性、移动方向、寻敌、发射模式、伤害、掉落、升级、胜负、暂停、界面内容和存档奖励。服务负责有界存储、Box2D 求解和查询、引擎计时器、动画/Tween、资源、文字排版、粒子和绘制。服务不接受“敌人”“武器”“伤害”等玩法命令。

宿主按 Bootstrap → Update → Fixed（0 到 4 次）→ Extract 调用。只有 Fixed 可以执行 `physics_step`，每次 Fixed 最多一次；游戏适配器验证实际步数。Update 推进 UI 时间，Fixed 推进游戏时间。暂停和升级选择由脚本决定是否进入 Fixed。绘制只允许 Bootstrap/Extract。

## 状态与生命周期

- `table_create(rows, columns)` 定义数值记录 schema；预算来自 YAML，不自动扩容。`acquire` 返回 ticket，满池返回 -1，由脚本处理容量不足。
- `get/put(table, ticket, column)` 验证 generation、索引和有限数值。`release` 后旧 ticket 不再有效，槽位复用清零。
- 记录身份为 `(table, ticket)`，ticket 不可脱离 table 使用。不同世界的服务实例不可混用；外泄服务函数在世界销毁后报 `ARP-SCRIPT2D-EXPIRED-WORLD`。
- `records(table)` 返回当前存活 ticket 的快照；遍历时可以释放对象。快照存在 boxed 分配，不能视为零分配接口。
- 查询结果仅在下次查询或物理步之前使用；需长期保留时由脚本复制对象 ticket 等数据。`hit` 提供 objectId、point、normal、fraction。
- `timer_create(ownerTable, owner, domain, seconds, repeat, event)` 绑定记录 generation；失效 owner 在下一次 advance 前取消。domain 0 为游戏时间，1 为 UI 时间。事件缓冲有界。
- `animation_*`、`tween_*`、`body_*` 分别管理各自 ticket；显式销毁或随世界整体销毁。声音由世界持有，销毁世界停止所属 voices。

## 执行与资源合同

生产默认使用现有 DirectNative v5 离线编译产物。绑定验证源码 SHA256、模块版本、导出与反射合同、binding/target/arity 和 callable identity。源码不匹配直接失败，不回退解释器。`--script-interpreter` 是显式开发入口；修改玩法 .arp/YAML 无需重编译 C++ 宿主即可开发验证。正式 AOT 发布需要重新生成并链接脚本包。

`ScriptSession` 不依赖某个特定领域模块，依赖由构建请求或包 manifest 明确指定；执行预算来自 YAML。所有服务容器和 owned object 使用 core allocator。预算包含脚本指令、调用/递归/容器容量、AOT 内存，以及物理、表、动画、粒子、绘制、文字和计时器容量。脚本和反射仍会进行 boxed 分配，最终性能必须以迁移后的二进制实测。

不新增 GPU 语义：精灵仍走既有 Scene2DRenderer、相邻兼容批处理和 L2 lowering。Null 是验证器；Windows/Vulkan 以外的实际执行状态不能由本例推断。
