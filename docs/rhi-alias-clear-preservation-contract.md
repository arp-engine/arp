# AliasClearPreservation/1

状态：限定验收定义 Contract Complete；Vulkan Backend Validated、Windows Platform Validated。
完整HeapPlacementAdmission/1及FrameGraphRetirementAdmission/1仍Draft，非Production Ready。
P0/L1-c/3的限定GPU内容验收，不改变任何L2能力或执行授权。

## 意图与范围

复用现有RenderGraphResourceManagementSmokeRunner及真实Vulkan WSI fixture，验证
ResourceManagementProduceA、PreserveC、ProduceB、Composite四个attachment pass。
ConsumeA及Composite的callback没有shader draw；本契约不宣称采样、着色、buffer运算
或长期压力正确，只证明clear/Store/Load及alias handoff后的非alias内容保存。

- 每个frame的A/B/C是不同image。A和B的native bind必须具有相同原device、memory、
  offset；C必须绑定另一memory。不能只依赖graph统计、名称或逻辑alias plan。
- ProduceA清A为(0.125,0.25,0.5,1)，同时清C为(0.5,0.25,0.125,1)。
  PreserveC使用Load/Store；ProduceB清B为(0.25,0.5,0.125,1)。
- 对应事件读取完整RGBA8图像：A/C初始clear、C Load、B clear、B clear之后的C。
  所有像素必须与声明颜色一致；UNORM转换允许相邻一个量化值。这不是图像模糊容差。
  B启用后A已离开生命周期，不读取/要求A保留旧内容。
- 四frame严格消费已登记fixture顺序与attachment数量(2,1,1,1)，不得猜测event id。
  前三frame离屏64x64，预算收缩frame离屏1x1；swapchain保持320x180。
  Composite原有BGRA蓝色clear与三类显式barrier检查继续成立。
- 绑定证据来自capture structured vkBindImageMemory及实际output resource identity。
  只接受该fixture当前使用的绑定API；缺失、重复、未知API/schema或不匹配均验收失败，
  不通过名称、顺序或相似颜色替代真实native绑定关系。
- 每frame保存A、C、B及C-after-B的PNG；AI必须打开关键图，记录capture/图像hash。
  新源码只能使用相同输入快照的新binary/capture验收，旧capture仅用于工具开发。

## 五backend结论

| backend | 本限定验收 |
| --- | --- |
| Null | ValidatorOnly：原graph/lease元数据测试；不提供physical alias或GPU内容证据 |
| Vulkan | Native：上述真实bind、pass及像素证据限定验收通过；仅列明fixture的Backend Validated |
| Metal | Native lowering沿用HeapPlacementAdmission/1，缺capability/payload则DiagnosticFailure；本批无Apple执行证据 |
| WebGPU | placement/alias明确DiagnosticFailure；本验收不启用或模拟该能力 |
| WebGL | 尚无生产backend且native heap alias不在profile内，DiagnosticFailure |

## 实施与退出

先登记此契约，再扩展既有tests/tools/analyze_graph_sync_renderdoc.py，不另造capture
runner。保留快速core/pool/placement contract及Null hidden-frame；真实WSI smoke及
RenderDoc replay同时保留原barrier、swapchain像素与validation-message断言。

仅在新capture及AI检查通过后关闭本限定clear内容门槛。持续workload、原timeline
pending、不同device completion、容量准入、BC enabled-feature、Apple/完整WebGPU及
早前native异常保持各自Open；完整HeapPlacementAdmission/1及P0仍不能由此关闭。


## 2026-10-17 限定验证结果

resource-cache-accounting/native-stable-eviction的新binary/capture回放通过，2012命名输入
一致，原三类barrier各4次且零validation message。4帧每帧5个完整离屏像素观测通过：
A/B为同device10、memory128、offset0，C为memory130；最后1x1帧亦成立。实际draw0，
每帧C-after-B为RGBA[127,64,32,255]。工具读取完整像素，而非只检查该首像素。
AI已通过tools.view_image打开present及首帧A/C/B/C-after-B五图，确认均匀声明颜色；
ai-image-inspection.yaml记录各图/capture hash。CPU回归56/56含Null与pool，2011输入
一致。限定contract/backend/platform状态只覆盖本条定义的原fixture和当前Windows环境，
不覆盖Apple/mobile、shader sampling/draw或持续workload。
