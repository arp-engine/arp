# StaticViewportScissor/1

L2 `RenderPipelineDesc.hasViewport/hasScissor` makes the corresponding
descriptor rectangle authoritative on every pipeline bind. A later pipeline
bind must restore its own declared static state. Explicit dynamic commands
retain their existing semantics; mixing static descriptors with
`dynamicViewportScissor` is rejected by VS-E017. This revision addresses
declared static rectangles only, without changing absent-field semantics.

| Backend | Lowering |
| --- | --- |
| Null | Equivalent validation; no native pixels claimed |
| Vulkan | Native baked VkPipelineViewportStateCreateInfo rectangles |
| Metal | Equivalent encoder viewport/scissor commands on pipeline bind |
| WebGPU | Equivalent render-pass encoder viewport/scissor commands on pipeline bind |

Metal and WebGPU must retain these descriptor values in backend-owned pipeline
payloads. They cannot rely on an attachment-sized native default for declared
static state. Existing L2 validation rejects invalid rectangles and conflicting
dynamic/static intent before payload creation. Backend-private lowering does
not expose native handles or add allocations to draw/bind hot paths.

Validation: four-backend lowering assertions plus a native Metal two-pass
readback whose first static viewport covers the upper half and whose static
scissor covers the left half. The second pass must preserve the top-left
quadrant. The dynamic coordinate fixture remains a separate regression.
WebGPU/Vulkan native execution requires their own platform runs; source parity
does not establish Platform Validated or Production Ready.
