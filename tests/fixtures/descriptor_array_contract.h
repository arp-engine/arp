#pragma once

#include "rhi/frontend/descriptor_resource_validation.h"
#include "rhi/gpu_device.h"

#include <cassert>
#include <cstdio>

namespace arp::tests {

inline rhi::DescriptorLayoutDesc descriptorArrayFixtureLayout()
{
    rhi::DescriptorLayoutDesc layout;
    layout.name = "ArrayElements";
    // Noncontiguous, unsorted binding numbers must not become element offsets.
    layout.bindings.push_back(
        {7u, rhi::DescriptorType::SampledTexture, rhi::ShaderStage::Fragment, 3u});
    layout.bindings.push_back(
        {2u, rhi::DescriptorType::SampledTexture, rhi::ShaderStage::Fragment});
    return layout;
}

inline void verifyDescriptorArrayElements(rhi::DescriptorArena& arena,
    core::UniquePtr<rhi::GpuTextureView>& expiringView,
    rhi::GpuResourceId stableView,
    core::UniquePtr<rhi::GpuTexture>& expiringParent)
{
    using namespace rhi;
    arena.reset();
    auto layout = descriptorArrayFixtureLayout();
    std::uint32_t count = 99u;
    DescriptorLayoutDesc empty;
    empty.name = "EmptyArrayLayout";
    assert(countDescriptorElements(empty, count).success && count == 0u);
    assert(validateDescriptorLayout(empty).success);
    assert(countDescriptorElements(layout, count).success && count == 4u);
    assert(descriptorElementIndex(layout, 7u, 2u) == 2u);
    assert(descriptorElementIndex(layout, 2u, 0u) == 3u);
    assert(descriptorElementIndex(layout, 8u, 0u) == UINT32_MAX);
    auto zeroCount = layout;
    zeroCount.bindings[0].arrayCount = 0u;
    assert(!countDescriptorElements(zeroCount, count).success && count == 0u);
    assert(!validateDescriptorLayout(zeroCount).success);
    auto set = arena.allocateDescriptorSet({"ArrayElements", layout});
    assert(set && expiringView && expiringParent);
    assert(!set->hasWrittenBinding(7u) && !set->validateBinding(7u).success);
    DescriptorBindingWrite write{7u,
        DescriptorType::SampledTexture,
        expiringView->id(),
        DescriptorResourceKind::TextureView};
    write.arrayElement = 2u;
    assert(set->write(write).success);
    assert(set->hasWrittenBindingElement(7u, 2u));
    assert(!set->hasWrittenBindingElement(7u, 0u));
    assert(!set->hasWrittenBindingElement(7u, 1u));
    assert(set->validateBindingElement(7u, 2u).success);
    assert(
        !validateDescriptorResources(*set, DescriptorContentRequirement::CompleteLayout).success);
    assert(
        validateDescriptorResources(*set, DescriptorContentRequirement::WrittenElements).success);
    write.resource = stableView;
    for (auto element : {0u, 1u}) {
        write.arrayElement = element;
        assert(set->write(write).success);
    }
    write.binding = 2u;
    write.arrayElement = 0u;
    assert(set->write(write).success);
    assert(validateDescriptorResources(*set, DescriptorContentRequirement::CompleteLayout).success);
    assert(set->stats().bindingCount == 2u && set->stats().writtenBindings == 4u);
    const auto revision = set->contentRevision();
    write.arrayElement = 1u;
    assert(!set->write(write).success); // scalar element must be zero
    write.binding = 7u;
    write.arrayElement = 3u;
    assert(!set->write(write).success);
    assert(set->contentRevision() == revision);
    assert(validateDescriptorResources(*set, DescriptorContentRequirement::CompleteLayout).success);
    const auto oldView = expiringView->id();
    expiringParent.reset();
    assert(set->validateBindingElement(7u, 0u).success);
    assert(set->validateBindingElement(7u, 1u).success);
    assert(set->validateBindingElement(2u, 0u).success);
    assert(!set->validateBindingElement(7u, 2u).success);
    auto orphanWrite = write;
    orphanWrite.arrayElement = 2u;
    orphanWrite.resource = oldView;
    assert(!set->write(orphanWrite).success && set->contentRevision() == revision);
    expiringView.reset();
    assert(set->validateBindingElement(7u, 0u).success);
    assert(set->validateBindingElement(7u, 1u).success);
    assert(set->validateBindingElement(2u, 0u).success);
    assert(!set->validateBindingElement(7u, 2u).success);
    assert(!set->validateBinding(7u).success);
    assert(
        !validateDescriptorResources(*set, DescriptorContentRequirement::WrittenElements).success);
    write.arrayElement = 2u;
    assert(set->write(write).success);
    assert(validateDescriptorResources(*set, DescriptorContentRequirement::CompleteLayout).success);
    assert(set->allocation().descriptorCapacity == 4u);
    assert(arena.stats().allocatedDescriptors == 4u);
    arena.reset();
    assert(!set->validateBindingElement(7u, 2u).success);

    // Neither capacity failure nor arithmetic overflow may consume a set slot.
    auto tooLarge = layout;
    tooLarge.bindings[0].arrayCount = arena.stats().maxDescriptors;
    const auto before = arena.stats();
    assert(!arena.allocateSet(tooLarge).success);
    tooLarge.bindings[0].arrayCount = UINT32_MAX;
    assert(!arena.allocateSet(tooLarge).success);
    assert(arena.stats().allocatedSets == before.allocatedSets);
    assert(arena.stats().allocatedDescriptors == before.allocatedDescriptors);
    const auto exact = arena.allocateSet(layout);
    assert(exact.success && exact.descriptorCapacity == 4u);
    const auto full = arena.stats();
    assert(!arena.allocateSet(layout).success);
    assert(!arena
            .allocateTable({"OverflowTable",
                DescriptorType::SampledTexture,
                ShaderStage::Fragment,
                UINT32_MAX})
            .success);
    assert(arena.stats().allocatedSets == full.allocatedSets);
    assert(arena.stats().allocatedDescriptors == full.allocatedDescriptors);
    std::puts(
        "DescriptorArrayElements/1 independent elements, liveness, bounds and capacity: PASS");
}

inline void verifyDescriptorArrayDevice(rhi::GpuDevice& device)
{
    using namespace rhi;
    const TextureDesc desc{"ArrayTexture",
        {8u, 8u, 1u},
        TextureFormat::Rgba8Unorm,
        TextureUsage::Sampled,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderRead};
    auto texture = device.createTexture(desc);
    auto stableTexture = device.createTexture(desc);
    assert(texture && stableTexture);
    TextureViewDesc viewDesc;
    viewDesc.usage = TextureUsage::Sampled;
    auto view = device.createTextureView(*texture, viewDesc);
    auto stable = device.createTextureView(*stableTexture, viewDesc);
    auto arena = device.createDescriptorArena({"ArrayArena", 2u, 4u});
    assert(view && stable && arena);
    verifyDescriptorArrayElements(*arena, view, stable->id(), texture);
}

} // namespace arp::tests
