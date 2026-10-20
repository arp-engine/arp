#pragma once

#include "rhi/gpu_device.h"

#include <cassert>

namespace arp::tests {

inline void verifyForeignTextureWrites(
    rhi::DescriptorArena& arena, rhi::GpuResourceId localView, rhi::GpuResourceId foreignView)
{
    using namespace rhi;
    DescriptorLayoutDesc layout;
    layout.name = "IdentityTextureSet";
    layout.bindings.push_back({0u, DescriptorType::SampledTexture, ShaderStage::Fragment, 3u});
    auto set = arena.allocateDescriptorSet({"IdentityTextureSet", layout});
    assert(set);
    const DescriptorBindingWrite good{
        0u, DescriptorType::SampledTexture, localView, DescriptorResourceKind::TextureView};
    const DescriptorBindingWrite wrong{
        0u, DescriptorType::SampledTexture, foreignView, DescriptorResourceKind::TextureView};
    assert(!set->write(wrong).success);
    assert(!set->hasWrittenBinding(0u));
    assert(set->write(good).success);
    assert(!set->write(wrong).success);
    assert(set->validateBinding(0u).success);
    for (std::uint32_t element : {1u, 2u}) {
        auto goodElement = good;
        auto wrongElement = wrong;
        goodElement.arrayElement = element;
        wrongElement.arrayElement = element;
        const auto unwrittenRevision = set->contentRevision();
        assert(!set->write(wrongElement).success);
        assert(!set->hasWrittenBindingElement(0u, element));
        assert(set->contentRevision() == unwrittenRevision);
        assert(set->write(goodElement).success);
        const auto writtenRevision = set->contentRevision();
        assert(!set->write(wrongElement).success);
        assert(set->contentRevision() == writtenRevision);
        assert(set->validateBindingElement(0u, element).success);
    }

    const DescriptorTableDesc tableDesc{
        "IdentityTextureTable", DescriptorType::SampledTexture, ShaderStage::Fragment, 1u};
    auto table = arena.allocateDescriptorTable(tableDesc);
    assert(table);
    const DescriptorTableWrite goodTable{
        0u, DescriptorType::SampledTexture, localView, DescriptorResourceKind::TextureView};
    const DescriptorTableWrite wrongTable{
        0u, DescriptorType::SampledTexture, foreignView, DescriptorResourceKind::TextureView};
    assert(!table->write(wrongTable).success);
    assert(!table->hasWrittenDescriptor(0u));
    assert(table->write(goodTable).success);
    assert(!table->write(wrongTable).success);
    assert(table->validateDescriptor(0u).success);
    assert(localView != foreignView);
}

// Use two real devices (or explicitly labelled metadata devices). Equal
// creation order used to alias their independent numeric ID counters.
inline void verifyForeignDescriptorResources(rhi::GpuDevice& local, rhi::GpuDevice& foreign)
{
    using namespace rhi;
    const BufferDesc bufferDesc{"IdentityUniform",
        256u,
        ResourceUsage::UniformBuffer,
        MemoryUsage::CpuToGpu,
        ResourceState::ShaderRead};
    auto localBuffer = local.createBuffer(bufferDesc);
    auto foreignBuffer = foreign.createBuffer(bufferDesc);
    assert(localBuffer && foreignBuffer);
    DescriptorLayoutDesc layout;
    layout.name = "IdentitySet";
    layout.bindings.push_back({0u, DescriptorType::UniformBuffer, ShaderStage::Vertex});
    auto arena = local.createDescriptorArena({"IdentityArena", 4u, 8u});
    assert(arena);
    auto set = arena->allocateDescriptorSet({"IdentitySet", layout});
    assert(set);
    const DescriptorBindingWrite good{
        0u, DescriptorType::UniformBuffer, localBuffer->id(), DescriptorResourceKind::Buffer};
    const DescriptorBindingWrite wrong{
        0u, DescriptorType::UniformBuffer, foreignBuffer->id(), DescriptorResourceKind::Buffer};
    assert(!set->write(wrong).success);
    assert(!set->hasWrittenBinding(0u));
    assert(set->write(good).success);
    assert(!set->write(wrong).success);
    assert(set->validateBinding(0u).success);
    assert(localBuffer->id() != foreignBuffer->id());

    const TextureDesc textureDesc{"IdentityTexture",
        {8u, 8u, 1u},
        TextureFormat::Rgba8Unorm,
        TextureUsage::Sampled,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderRead};
    auto localTexture = local.createTexture(textureDesc);
    auto foreignTexture = foreign.createTexture(textureDesc);
    assert(localTexture && foreignTexture);
    TextureViewDesc viewDesc;
    viewDesc.usage = TextureUsage::Sampled;
    auto localView = local.createTextureView(*localTexture, viewDesc);
    auto foreignView = foreign.createTextureView(*foreignTexture, viewDesc);
    assert(localView && foreignView);
    verifyForeignTextureWrites(*arena, localView->id(), foreignView->id());
    assert(localTexture->id() != foreignTexture->id());
    assert(localView->id() != foreignView->id());

    auto localSampler = local.createSampler({});
    auto foreignSampler = foreign.createSampler({});
    assert(localSampler && foreignSampler);
    DescriptorLayoutDesc samplerLayout;
    samplerLayout.name = "IdentitySamplerSet";
    samplerLayout.bindings.push_back({0u, DescriptorType::Sampler, ShaderStage::Fragment});
    auto samplerSet = arena->allocateDescriptorSet({"IdentitySamplerSet", samplerLayout});
    assert(samplerSet);
    const DescriptorBindingWrite goodSampler{
        0u, DescriptorType::Sampler, localSampler->id(), DescriptorResourceKind::Sampler};
    const DescriptorBindingWrite wrongSampler{
        0u, DescriptorType::Sampler, foreignSampler->id(), DescriptorResourceKind::Sampler};
    assert(!samplerSet->write(wrongSampler).success);
    assert(samplerSet->write(goodSampler).success);
    assert(!samplerSet->write(wrongSampler).success);
    assert(samplerSet->validateBinding(0u).success);
    assert(localSampler->id() != foreignSampler->id());
}

} // namespace arp::tests
