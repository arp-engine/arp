#include "editor/viewport/editor_scene_viewport_composite.h"

namespace arp::editor {

bool EditorSceneViewportComposite::create(
    rhi::GpuDevice& device, const rhi::Extent2D extent, EditorSceneViewportSurfaceProfile profile)
{
    if (!profile.valid() || extent.width == 0u || extent.height == 0u
        || (ownerDevice_ != nullptr && ownerDevice_ != &device))
        return false;

    rhi::TextureDesc colorDesc;
    colorDesc.label = "ArpEditorSceneViewportColor";
    colorDesc.extent = {extent.width, extent.height, 1u};
    colorDesc.format = profile.color;
    colorDesc.usage = rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Sampled;
    colorDesc.initialState = rhi::ResourceState::Undefined;
    auto color = device.createTexture(colorDesc);

    rhi::TextureDesc depthDesc;
    depthDesc.label = "ArpEditorSceneViewportDepth";
    depthDesc.extent = colorDesc.extent;
    depthDesc.format = profile.depth;
    depthDesc.usage = rhi::TextureUsage::RenderAttachment;
    depthDesc.initialState = rhi::ResourceState::Undefined;
    auto depth = device.createTexture(depthDesc);
    if (!color || !depth)
        return false;

    rhi::TextureViewDesc viewDesc;
    viewDesc.label = "ArpEditorSceneViewportColorView";
    viewDesc.format = colorDesc.format;
    viewDesc.usage = rhi::TextureUsage::Sampled;
    auto view = device.createTextureView(*color, viewDesc);
    if (!view)
        return false;

    rhi::SamplerDesc samplerDesc;
    samplerDesc.label = "ArpEditorSceneViewportSampler";
    samplerDesc.minFilter = rhi::SamplerFilter::Linear;
    samplerDesc.magFilter = rhi::SamplerFilter::Linear;
    samplerDesc.mipFilter = rhi::SamplerMipFilter::None;
    auto sampler = device.createSampler(samplerDesc);
    if (!sampler)
        return false;

    auto arena = device.createDescriptorArena({"ArpEditorSceneViewportGuiArena", 1u, 2u});
    if (!arena)
        return false;
    rhi::DescriptorSetDesc setDesc;
    setDesc.label = "ArpEditorSceneViewportGuiSet";
    setDesc.layout = renderer::detail::guiAtlasDescriptorLayout();
    auto set = arena->allocateDescriptorSet(setDesc);
    if (!set)
        return false;

    rhi::DescriptorBindingWrite textureWrite;
    textureWrite.binding = 0u;
    textureWrite.type = rhi::DescriptorType::SampledTexture;
    textureWrite.resource = view->id();
    textureWrite.resourceKind = rhi::DescriptorResourceKind::TextureView;
    textureWrite.resourceState = rhi::ResourceState::ShaderRead;
    if (!set->write(textureWrite).success)
        return false;

    rhi::DescriptorBindingWrite samplerWrite;
    samplerWrite.binding = 1u;
    samplerWrite.type = rhi::DescriptorType::Sampler;
    samplerWrite.resource = sampler->id();
    samplerWrite.resourceKind = rhi::DescriptorResourceKind::Sampler;
    if (!set->write(samplerWrite).success)
        return false;

    ownerDevice_ = &device;
    colorTarget_ = std::move(color);
    depthTarget_ = std::move(depth);
    colorView_ = std::move(view);
    sampler_ = std::move(sampler);
    descriptorArena_ = std::move(arena);
    guiDescriptorSet_ = std::move(set);
    return valid();
}

bool EditorSceneViewportComposite::valid() const noexcept
{
    return ownerDevice_ != nullptr && colorTarget_ && depthTarget_ && colorView_ && sampler_
        && descriptorArena_ && guiDescriptorSet_
        && renderer::guiAtlasLayoutCompatible(guiDescriptorSet_->layout());
}

rhi::Extent2D EditorSceneViewportComposite::extent() const noexcept
{
    if (!colorTarget_)
        return {};
    const auto extent3d = colorTarget_->desc().extent;
    return {extent3d.width, extent3d.height};
}

renderer::GuiExternalTextureSetBinding EditorSceneViewportComposite::guiBinding() noexcept
{
    renderer::GuiExternalTextureSetBinding binding;
    binding.resourceId = kEditorSceneViewportTextureId;
    binding.descriptorSet = guiDescriptorSet_.get();
    return binding;
}

} // namespace arp::editor
