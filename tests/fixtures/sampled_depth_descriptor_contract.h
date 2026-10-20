#pragma once

#include "rhi/frontend/descriptor_resource_validation.h"
#include "rhi/gpu_device.h"

#include <cassert>
#include <cstdio>

namespace arp::tests {

inline void verifySampledDepthTableWrites(rhi::DescriptorArena& arena,
    const rhi::GpuTextureView& wrongView,
    const rhi::GpuTextureView& depthView)
{
    using namespace rhi;
    arena.reset();
    DescriptorTableDesc desc{
        "SampledDepthTable", DescriptorType::SampledTexture, ShaderStage::Fragment, 3u};
    desc.depthTexture = true;
    desc.textureViewDimensionConstrained = true;
    auto table = arena.allocateDescriptorTable(desc);
    assert(table);
    DescriptorTableWrite wrong{
        2u, DescriptorType::SampledTexture, wrongView.id(), DescriptorResourceKind::TextureView};
    DescriptorTableWrite right{
        2u, DescriptorType::SampledTexture, depthView.id(), DescriptorResourceKind::TextureView};
    const auto before = table->contentRevision();
    assert(!table->write(wrong).success);
    assert(table->contentRevision() == before && !table->hasWrittenDescriptor(2u));
    assert(table->write(right).success && table->write(right).success);
    assert(
        validateDescriptorResources(*table, DescriptorContentRequirement::WrittenElements).success);
    assert(
        !validateDescriptorResources(*table, DescriptorContentRequirement::CompleteLayout).success);
    for (const auto index : {0u, 1u}) {
        right.index = index;
        assert(table->write(right).success);
    }
    const auto validRevision = table->contentRevision();
    assert(!table->write(wrong).success && table->contentRevision() == validRevision);
    wrong = right;
    wrong.index = 3u;
    assert(!table->write(wrong).success && table->contentRevision() == validRevision);
    assert(
        validateDescriptorResources(*table, DescriptorContentRequirement::CompleteLayout).success);
    assert(table->stats().writtenDescriptors == 3u);
    arena.reset();
    assert(!table->validateDescriptor(2u).success);
    assert(!table->write(right).success);
    const auto stats = arena.stats();
    desc.type = DescriptorType::UniformBuffer;
    assert(!arena.allocateTable(desc).success);
    assert(arena.stats().allocatedDescriptors == stats.allocatedDescriptors);
    assert(arena.stats().allocatedTables == stats.allocatedTables);
}

inline void verifySampledDepthWrites(rhi::DescriptorArena& arena,
    const rhi::GpuTextureView& colorView,
    const rhi::GpuTextureView& depthView)
{
    using namespace rhi;
    DescriptorLayoutDesc layout;
    layout.name = "SampledDepthViewContract";
    layout.bindings.push_back({0, DescriptorType::SampledTexture, ShaderStage::Fragment});
    layout.bindings[0].depthTexture = true;
    layout.bindings[0].sampledTextureFilterable = false;
    auto set = arena.allocateDescriptorSet({"SampledDepthSet", layout});
    assert(set);
    const DescriptorBindingWrite wrong{
        0, DescriptorType::SampledTexture, colorView.id(), DescriptorResourceKind::TextureView};
    const DescriptorBindingWrite right{
        0, DescriptorType::SampledTexture, depthView.id(), DescriptorResourceKind::TextureView};
    const auto rejected = set->write(wrong);
    assert(!rejected.success);
    assert(rejected.diagnostics.find("RHI-SAMPLED-DEPTH-VIEW-MISMATCH") != core::pmr::String::npos);
    assert(!set->hasWrittenBinding(0));
    assert(set->write(right).success);
    assert(set->write(right).success); // Native descriptor cache must preserve validation.
    assert(set->validateBinding(0).success);
    assert(!set->write(wrong).success);
    assert(set->validateBinding(0).success); // Rejected update preserves the prior valid write.
    assert(set->stats().writtenBindings == 1u);
    arena.reset();
    assert(!set->write(right).success);
    assert(!set->validateBinding(0).success);
}

inline void verifySampledDepthDescriptors(rhi::GpuDevice& device)
{
    using namespace rhi;
    auto color = device.createTexture({"SampledDepthColor",
        {8, 8, 1},
        TextureFormat::Rgba8Unorm,
        TextureUsage::Sampled,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderRead});
    auto depth = device.createTexture({"SampledDepthDepth",
        {8, 8, 1},
        TextureFormat::D32Float,
        TextureUsage::Sampled | TextureUsage::RenderAttachment,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderRead});
    assert(color && depth);
    TextureViewDesc colorDesc;
    colorDesc.usage = TextureUsage::Sampled;
    auto colorView = device.createTextureView(*color, colorDesc);
    auto depthDesc = colorDesc;
    depthDesc.aspect = TextureAspect::Depth;
    auto depthView = device.createTextureView(*depth, depthDesc);
    assert(colorView && depthView);
    auto attachmentDesc = depthDesc;
    attachmentDesc.usage = TextureUsage::RenderAttachment;
    auto attachmentView = device.createTextureView(*depth, attachmentDesc);
    assert(attachmentView);
    auto arena = device.createDescriptorArena({"SampledDepthArena", 1, 3});
    assert(arena);
    verifySampledDepthWrites(*arena, *colorView, *depthView);
    verifySampledDepthTableWrites(*arena, *colorView, *depthView);
    verifySampledDepthTableWrites(*arena, *attachmentView, *depthView);

    arena->reset();
    DescriptorTableDesc tableDesc{
        "DepthLifetime", DescriptorType::SampledTexture, ShaderStage::Fragment, 3u};
    tableDesc.depthTexture = true;
    auto table = arena->allocateDescriptorTable(tableDesc);
    assert(table);
    DescriptorTableWrite depthWrite{
        2u, DescriptorType::SampledTexture, depthView->id(), DescriptorResourceKind::TextureView};
    assert(table->write(depthWrite).success);
    const auto revision = table->contentRevision();
    depth.reset();
    assert(!table->validateDescriptor(2u).success);
    assert(!table->write(depthWrite).success && table->contentRevision() == revision);
    auto replacement = device.createTexture({"DepthReplacement",
        {8u, 8u, 1u},
        TextureFormat::D32Float,
        TextureUsage::Sampled,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderRead});
    assert(replacement);
    auto replacementView = device.createTextureView(*replacement, depthDesc);
    assert(replacementView);
    assert(!table->validateDescriptor(2u).success); // same-shaped parent cannot revive an old view
    depthWrite.resource = replacementView->id();
    assert(table->write(depthWrite).success && table->validateDescriptor(2u).success);
    replacementView.reset();
    assert(!table->validateDescriptor(2u).success);
    std::puts("DescriptorTableTextureShape/1 table depth/usage/liveness/recovery: PASS");
}

} // namespace arp::tests
