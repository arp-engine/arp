// Logical subpass input attachment interface.
//
// Physical lowering is owned by the compiler/RHI layer. Vulkan may use a
// native input attachment, Metal may use framebuffer fetch, and the FrameGraph
// lowers unsupported subpasses to separate sampled-texture passes. The Slang
// source deliberately does not select a target backend.
//
// Usage in a fragment shader:
//
//   #include <common/subpass_input.slang>
//   SUBPASS_INPUT_COLOR(SceneInput, 0);
//   float4 value = subpassLoadColor(SceneInput);
//
// AttachmentIndex remains part of the logical authoring call so compiler and
// reflection code can associate the resource with the FrameGraph attachment.
// The compiler records that value in backend-neutral reflection metadata; it
// must not become a target-specific attribute in this source file.

#define SUBPASS_INPUT_COLOR(Name, AttachmentIndex) \
    SubpassInput<float4> Name

#define SUBPASS_INPUT_DEPTH(Name, AttachmentIndex) \
    SubpassInput<float> Name

#define subpassLoadColor(Name) (Name).SubpassLoad()
#define subpassLoadDepth(Name) (Name).SubpassLoad()
