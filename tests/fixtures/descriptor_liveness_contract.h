#pragma once

#include "rhi/frontend/descriptor_bound_descriptor_slots.h"
#include "rhi/frontend/descriptor_resource_validation.h"
#include "rhi/gpu_device.h"
#include "tests/fixtures/descriptor_array_contract.h"

#include <cassert>
#include <cstdio>

namespace arp::tests {

inline void verifyStaleDescriptorValidation(const rhi::ValidationResult& result)
{
    assert(!result.success);
    assert(result.diagnostics.find("RHI-DESCRIPTOR-ALLOCATION-STALE") == 0u);
}

inline void verifyDescriptorConsumptionObservers(
    rhi::DescriptorArena& arena, rhi::GpuResourceId liveView)
{
    using namespace rhi;
    arena.reset();
    DescriptorLayoutDesc layout;
    layout.name = "ConsumptionObservers";
    layout.bindings.push_back({0u, DescriptorType::SampledTexture, ShaderStage::Fragment});
    auto set = arena.allocateDescriptorSet({"ConsumptionSet", layout});
    auto table = arena.allocateDescriptorTable(
        {"ConsumptionTable", DescriptorType::SampledTexture, ShaderStage::Fragment, 1u});
    assert(set && table);
    const DescriptorBindingWrite write{
        0u, DescriptorType::SampledTexture, liveView, DescriptorResourceKind::TextureView};
    assert(set->write(write).success);
    assert(table
            ->write(
                {0u, DescriptorType::SampledTexture, liveView, DescriptorResourceKind::TextureView})
            .success);
    BoundDescriptorSlots slots;
    assert(slots.bindSet(0u, *set, nullptr, 0u).success);
    assert(slots.bindTable(0u, *table).success);
    auto invalidWrite = write;
    invalidWrite.resource = {};
    const auto revision = set->contentRevision();
    assert(!set->write(invalidWrite).success);
    assert(set->contentRevision() == revision);
    assert(slots.validateLiveBindings().success);
    // Even an identical successful write starts a logical revision. Cached
    // native updates cannot silently change this public rebind requirement.
    assert(set->write(write).success);
    assert(set->contentRevision() > revision);
    assert(slots.validateLiveBindings().diagnostics.find("RHI-DESCRIPTOR-CONTENT-CHANGED") == 0u);
    assert(slots.bindSet(0u, *set, nullptr, 0u).success);
    assert(slots.validateLiveBindings().success);
    assert(table
            ->write(
                {0u, DescriptorType::SampledTexture, liveView, DescriptorResourceKind::TextureView})
            .success);
    assert(slots.validateLiveBindings().diagnostics.find("RHI-DESCRIPTOR-CONTENT-CHANGED") == 0u);
    assert(slots.bindTable(0u, *table).success);
    assert(slots.validateLiveBindings().success);
    slots.reset();
    const auto before = core::defaultCpuAllocator().stats();
    for (std::uint32_t i = 0; i < BoundDescriptorSlots::kMaxTrackedSlots; ++i) {
        // Capacity is the number of tracked slots, not the numerical index.
        assert(slots.validateSetCapacity(1000u + i).success);
        assert(slots.validateTableCapacity(2000u + i).success);
        assert(slots.bindSet(1000u + i, *set, nullptr, 0u).success);
        assert(slots.bindTable(2000u + i, *table).success);
    }
    assert(slots.validateLiveBindings().success);
    const auto after = core::defaultCpuAllocator().stats();
    assert(after.allocationCount == before.allocationCount);
    assert(!slots.validateSetCapacity(0u).success);
    assert(!slots.validateTableCapacity(0u).success);
    assert(!slots.bindSet(0u, *set, nullptr, 0u).success);
    assert(!slots.bindTable(0u, *table).success);
    assert(slots.validateLiveBindings().success);
    set.reset();
    assert(slots.validateLiveBindings().diagnostics.find("RHI-DESCRIPTOR-OBJECT-NOT-LIVE") == 0u);
    // Reset frees arena capacity; a same-shaped replacement cannot revive an
    // expired observer, even if its allocation reuses the same memory address.
    arena.reset();
    set = arena.allocateDescriptorSet({"ConsumptionReplacement", layout});
    assert(set && set->write(write).success);
    assert(!slots.validateLiveBindings().success);
    for (std::uint32_t i = 0; i < BoundDescriptorSlots::kMaxTrackedSlots; ++i)
        assert(slots.bindSet(1000u + i, *set, nullptr, 0u).success);
    assert(!slots.validateLiveBindings().success); // the old table is arena-stale
    table.reset();
    assert(slots.validateLiveBindings().diagnostics.find("RHI-DESCRIPTOR-OBJECT-NOT-LIVE") == 0u);
    slots.reset();
    assert(slots.validateLiveBindings().success);
    assert(slots.validateSetCapacity(0u).success && slots.validateTableCapacity(0u).success);
}

inline void verifyDescriptorArenaLifetime(
    core::UniquePtr<rhi::DescriptorArena> arena, rhi::GpuResourceId liveView)
{
    using namespace rhi;
    assert(arena && liveView.isValid());
    arena->reset();
    DescriptorLayoutDesc layout;
    layout.name = "ArenaLifetime";
    layout.bindings.push_back({0u, DescriptorType::SampledTexture, ShaderStage::Fragment});
    const DescriptorSetDesc setDesc{"ArenaLifetimeSet", layout};
    const DescriptorTableDesc tableDesc{
        "ArenaLifetimeTable", DescriptorType::SampledTexture, ShaderStage::Fragment, 1u};
    const DescriptorBindingWrite write{
        0u, DescriptorType::SampledTexture, liveView, DescriptorResourceKind::TextureView};
    const DescriptorTableWrite tableWrite{
        0u, DescriptorType::SampledTexture, liveView, DescriptorResourceKind::TextureView};
    auto set = arena->allocateDescriptorSet(setDesc);
    auto table = arena->allocateDescriptorTable(tableDesc);
    assert(set && table);
    assert(set->write(write).success && table->write(tableWrite).success);
    assert(set->isCurrentAllocation() && table->isCurrentAllocation());
    arena->reset();
    assert(!set->isCurrentAllocation() && !table->isCurrentAllocation());
    auto currentSet = arena->allocateDescriptorSet(setDesc);
    auto currentTable = arena->allocateDescriptorTable(tableDesc);
    assert(currentSet && currentTable);
    assert(currentSet->write(write).success && currentTable->write(tableWrite).success);
    assert(currentSet->validateBinding(0u).success);
    assert(currentTable->validateDescriptor(0u).success);
    assert(!set->isCurrentAllocation() && !table->isCurrentAllocation());
    arena.reset();
    // The view and descriptors are still alive. Losing only the arena owner
    // must revoke permission without dereferencing any destroyed arena memory.
    assert(!currentSet->isCurrentAllocation() && !currentTable->isCurrentAllocation());
    for (const auto* stale : {set.get(), currentSet.get()}) {
        assert(stale->allocation().success && stale->layout().name == layout.name);
        assert(!stale->hasWrittenBinding(0u));
        assert(!stale->hasWrittenBindingElement(0u, 0u));
        verifyStaleDescriptorValidation(stale->validateBinding(0u));
        verifyStaleDescriptorValidation(stale->validateBindingElement(0u, 0u));
        assert(stale->stats().writtenBindings == 0u);
    }
    for (const auto* stale : {table.get(), currentTable.get()}) {
        assert(stale->allocation().success && stale->desc().capacity == 1u);
        assert(!stale->hasWrittenDescriptor(0u));
        verifyStaleDescriptorValidation(stale->validateDescriptor(0u));
        assert(stale->stats().writtenDescriptors == 0u);
    }
    verifyStaleDescriptorValidation(set->write(write));
    verifyStaleDescriptorValidation(currentSet->write(write));
    verifyStaleDescriptorValidation(table->write(tableWrite));
    verifyStaleDescriptorValidation(currentTable->write(tableWrite));
}

inline void verifyDescriptorParentValidity(rhi::DescriptorArena& arena,
    core::UniquePtr<rhi::GpuTexture>& parent,
    rhi::GpuResourceId view,
    rhi::GpuResourceId replacement,
    rhi::DescriptorType type,
    bool includeTables = true)
{
    using namespace rhi;
    arena.reset();
    DescriptorLayoutDesc layout;
    layout.name = "ParentValidity";
    layout.bindings.push_back({0u, type, ShaderStage::Fragment});
    auto set = arena.allocateDescriptorSet({"ParentSet", layout});
    auto unwritten = arena.allocateDescriptorSet({"ParentUnwritten", layout});
    auto table = includeTables
        ? arena.allocateDescriptorTable({"ParentTable", type, ShaderStage::Fragment, 1u})
        : nullptr;
    auto emptyTable = includeTables
        ? arena.allocateDescriptorTable({"ParentEmptyTable", type, ShaderStage::Fragment, 1u})
        : nullptr;
    assert(parent && set && unwritten && (!includeTables || (table && emptyTable)));
    if (!includeTables) {
        assert(type == DescriptorType::InputAttachment);
        const auto allocation =
            arena.allocateTable({"UnsupportedInputTable", type, ShaderStage::Fragment, 1u});
        assert(!allocation.success && !allocation.diagnostics.empty());
    }
    const DescriptorBindingWrite write{0u, type, view, DescriptorResourceKind::TextureView};
    const DescriptorTableWrite tableWrite{0u, type, view, DescriptorResourceKind::TextureView};
    assert(set->write(write).success);
    if (table)
        assert(table->write(tableWrite).success);
    BoundDescriptorSlots setSlots, tableSlots;
    assert(setSlots.bindSet(0u, *set, nullptr, 0u).success);
    if (table)
        assert(tableSlots.bindTable(0u, *table).success);
    assert(setSlots.validateLiveBindings().success && tableSlots.validateLiveBindings().success);
    const auto setRevision = set->contentRevision(),
               tableRevision = table ? table->contentRevision() : 0u;
    const auto emptyRevision = unwritten->contentRevision(),
               emptyTableRevision = emptyTable ? emptyTable->contentRevision() : 0u;
    parent.reset(); // The view wrapper and retained storage remain alive.
    const auto rejected = [](const ValidationResult& result) {
        assert(!result.success);
        assert(result.diagnostics.find("RHI-DESCRIPTOR-PARENT-NOT-LIVE") == 0u);
    };
    rejected(set->write(write)); // identical-write cache must revalidate
    if (table)
        rejected(table->write(tableWrite));
    rejected(unwritten->write(write));
    if (emptyTable)
        rejected(emptyTable->write(tableWrite));
    assert(set->contentRevision() == setRevision
        && (!table || table->contentRevision() == tableRevision));
    assert(unwritten->contentRevision() == emptyRevision
        && (!emptyTable || emptyTable->contentRevision() == emptyTableRevision));
    rejected(set->validateBinding(0u));
    rejected(set->validateBindingElement(0u, 0u));
    if (table)
        rejected(table->validateDescriptor(0u));
    rejected(validateDescriptorResources(*set, DescriptorContentRequirement::CompleteLayout));
    if (table)
        rejected(
            validateDescriptorResources(*table, DescriptorContentRequirement::WrittenElements));
    rejected(setSlots.validateLiveBindings());
    if (table)
        rejected(tableSlots.validateLiveBindings());
    assert(set->write({0u, type, replacement, DescriptorResourceKind::TextureView}).success);
    if (table)
        assert(table->write({0u, type, replacement, DescriptorResourceKind::TextureView}).success);
    assert(setSlots.bindSet(0u, *set, nullptr, 0u).success);
    if (table)
        assert(tableSlots.bindTable(0u, *table).success);
    assert(setSlots.validateLiveBindings().success && tableSlots.validateLiveBindings().success);
    // A rejected orphan overwrite preserves the replacement's content/revision.
    const auto recoveredRevision = set->contentRevision();
    rejected(set->write(write));
    if (table)
        rejected(table->write(tableWrite));
    assert(set->contentRevision() == recoveredRevision);
    assert(setSlots.validateLiveBindings().success && tableSlots.validateLiveBindings().success);
}

inline void verifyDescriptorParentLiveness(rhi::GpuDevice& device, bool inputAttachmentTables)
{
    using namespace rhi;
    for (auto type : {DescriptorType::SampledTexture,
             DescriptorType::StorageTexture,
             DescriptorType::InputAttachment}) {
        auto usage = type == DescriptorType::SampledTexture ? TextureUsage::Sampled
            : type == DescriptorType::StorageTexture        ? TextureUsage::Storage
                                                            : TextureUsage::RenderAttachment;
        const TextureDesc desc{"ParentTexture",
            {8u, 8u, 1u},
            TextureFormat::Rgba8Unorm,
            usage,
            MemoryUsage::GpuOnly,
            ResourceState::ShaderRead};
        auto parent = device.createTexture(desc), replacement = device.createTexture(desc);
        assert(parent && replacement && parent->id() != replacement->id());
        TextureViewDesc viewDesc;
        viewDesc.usage = usage;
        auto view = device.createTextureView(*parent, viewDesc);
        auto newView = device.createTextureView(*replacement, viewDesc);
        auto arena = device.createDescriptorArena({"ParentArena", 4u, 4u});
        assert(view && newView && arena);
        verifyDescriptorParentValidity(*arena,
            parent,
            view->id(),
            newView->id(),
            type,
            type != DescriptorType::InputAttachment || inputAttachmentTables);
        auto laterParent = device.createTexture(desc);
        assert(laterParent && laterParent->id() != view->textureId());
        auto laterView = device.createTextureView(*laterParent, viewDesc);
        assert(laterView);
        arena->reset();
        DescriptorLayoutDesc layout;
        layout.name = "LaterParent";
        layout.bindings.push_back({0u, type, ShaderStage::Fragment});
        auto fresh = arena->allocateDescriptorSet({"LaterParentSet", layout});
        assert(fresh);
        const DescriptorBindingWrite orphan{
            0u, type, view->id(), DescriptorResourceKind::TextureView};
        assert(fresh->write(orphan).diagnostics.find("RHI-DESCRIPTOR-PARENT-NOT-LIVE") == 0u);
        assert(
            fresh->write({0u, type, laterView->id(), DescriptorResourceKind::TextureView}).success);
        view.reset();
        assert(fresh->write(orphan).diagnostics.find("RHI-DESCRIPTOR-RESOURCE-NOT-LIVE") == 0u);
    }
    std::puts("DescriptorParentLiveness/1 sampled/storage/input sets and supported tables: PASS");
}

inline void verifyDescriptorWrapperLiveness(
    rhi::GpuDevice& device, bool inputAttachmentTables = true)
{
    using namespace rhi;
    verifyDescriptorArrayDevice(device);
    verifyDescriptorParentLiveness(device, inputAttachmentTables);
    // Three scalar sets plus one scalar table; Null counts both in maxSets.
    auto arena = device.createDescriptorArena({"LivenessArena", 4u, 4u});
    assert(arena);
    DescriptorLayoutDesc bufferLayout;
    bufferLayout.name = "LivenessBuffer";
    bufferLayout.bindings.push_back({0u, DescriptorType::UniformBuffer, ShaderStage::Vertex});
    auto buffer = device.createBuffer({"LivenessBuffer",
        256u,
        ResourceUsage::UniformBuffer,
        MemoryUsage::CpuToGpu,
        ResourceState::ShaderRead});
    auto bufferSet = arena->allocateDescriptorSet({"LivenessBufferSet", bufferLayout});
    assert(buffer && bufferSet);
    assert(validateDescriptorResources(*bufferSet, DescriptorContentRequirement::WrittenElements)
            .success);
    assert(!validateDescriptorResources(*bufferSet, DescriptorContentRequirement::CompleteLayout)
            .success);
    const DescriptorBindingWrite bufferWrite{
        0u, DescriptorType::UniformBuffer, buffer->id(), DescriptorResourceKind::Buffer};
    assert(bufferSet->write(bufferWrite).success);
    assert(bufferSet->validateBinding(0u).success);
    assert(validateDescriptorResources(*bufferSet, DescriptorContentRequirement::CompleteLayout)
            .success);
    buffer.reset();
    // Native descriptor payloads may still retain storage, but a wrapper's
    // destroyed identity cannot authorize new recording or a cached rewrite.
    assert(!bufferSet->validateBinding(0u).success);
    assert(!bufferSet->validateBindingElement(0u, 0u).success);
    assert(!bufferSet->write(bufferWrite).success);
    assert(!validateDescriptorResources(*bufferSet, DescriptorContentRequirement::WrittenElements)
            .success);

    auto texture = device.createTexture({"LivenessTexture",
        {8u, 8u, 1u},
        TextureFormat::Rgba8Unorm,
        TextureUsage::Sampled,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderRead});
    assert(texture);
    TextureViewDesc viewDesc;
    viewDesc.usage = TextureUsage::Sampled;
    auto view = device.createTextureView(*texture, viewDesc);
    assert(view);
    DescriptorLayoutDesc textureLayout;
    textureLayout.name = "LivenessTexture";
    textureLayout.bindings.push_back({0u, DescriptorType::SampledTexture, ShaderStage::Fragment});
    auto set = arena->allocateDescriptorSet({"LivenessTextureSet", textureLayout});
    auto table = arena->allocateDescriptorTable(
        {"LivenessTextureTable", DescriptorType::SampledTexture, ShaderStage::Fragment, 1u});
    assert(set && table);
    const DescriptorBindingWrite viewWrite{
        0u, DescriptorType::SampledTexture, view->id(), DescriptorResourceKind::TextureView};
    const DescriptorTableWrite tableWrite{
        0u, DescriptorType::SampledTexture, view->id(), DescriptorResourceKind::TextureView};
    assert(set->write(viewWrite).success && table->write(tableWrite).success);
    assert(set->validateBinding(0u).success && table->validateDescriptor(0u).success);
    assert(
        validateDescriptorResources(*set, DescriptorContentRequirement::WrittenElements).success);
    assert(
        validateDescriptorResources(*table, DescriptorContentRequirement::CompleteLayout).success);
    view.reset();
    assert(!set->validateBinding(0u).success);
    assert(!set->validateBindingElement(0u, 0u).success);
    assert(!table->validateDescriptor(0u).success);
    assert(!set->write(viewWrite).success && !table->write(tableWrite).success);
    assert(
        !validateDescriptorResources(*set, DescriptorContentRequirement::WrittenElements).success);
    assert(!validateDescriptorResources(*table, DescriptorContentRequirement::WrittenElements)
            .success);
    auto replacement = device.createTextureView(*texture, viewDesc);
    assert(replacement && replacement->id() != viewWrite.resource);
    assert(!set->validateBinding(0u).success && !table->validateDescriptor(0u).success);
    assert(
        set->write({0u,
                       DescriptorType::SampledTexture,
                       replacement->id(),
                       DescriptorResourceKind::TextureView})
            .success);
    assert(table
            ->write({0u,
                DescriptorType::SampledTexture,
                replacement->id(),
                DescriptorResourceKind::TextureView})
            .success);
    assert(set->validateBinding(0u).success && table->validateDescriptor(0u).success);
    auto sampler = device.createSampler({});
    DescriptorLayoutDesc samplerLayout;
    samplerLayout.name = "LivenessSampler";
    samplerLayout.bindings.push_back({0u, DescriptorType::Sampler, ShaderStage::Fragment});
    auto samplerSet = arena->allocateDescriptorSet({"LivenessSamplerSet", samplerLayout});
    assert(sampler && samplerSet);
    const DescriptorBindingWrite samplerWrite{
        0u, DescriptorType::Sampler, sampler->id(), DescriptorResourceKind::Sampler};
    assert(samplerSet->write(samplerWrite).success);
    sampler.reset();
    assert(!samplerSet->validateBinding(0u).success);
    assert(!samplerSet->write(samplerWrite).success);
    arena->reset();
    assert(!set->validateBinding(0u).success && !table->validateDescriptor(0u).success);
    verifyDescriptorConsumptionObservers(*arena, replacement->id());
    verifyDescriptorArenaLifetime(std::move(arena), replacement->id());
}

} // namespace arp::tests
