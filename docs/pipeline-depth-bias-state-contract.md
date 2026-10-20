# PipelineDepthBiasState/1

Each graphics pipeline bind establishes the complete L2 depth-bias state.
Enabled uses the descriptor factors; disabled means zero effective bias,
regardless of a preceding pipeline or the disabled descriptor's stored factors.
No pipeline may inherit native encoder bias from an earlier bind.

| Backend | Lowering |
| --- | --- |
| Null | Equivalent validation; no pixel claim |
| Vulkan | Native rasterization state's depthBiasEnable gates factors |
| Metal | Equivalent setDepthBias on every bind, explicitly zero when disabled |
| WebGPU | Native baked depth state, zero all bias factors when disabled |

Existing capability/descriptor validation still gates supported enabled bias.
No resource, shader layout, synchronization or allocation changes. Test the
four-backend lowering and a real Metal enabled→disabled bind in one encoder:
the enabled factor would push depth to the far plane; disabled rendering must
pass a Less comparison against far clear and produce the expected pixels.
Native evidence validates that Metal slice only.
