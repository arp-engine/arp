#pragma once
#include "rhi/frontend/command_sources.h"

#include <cassert>

namespace arp::test {
// Shared physical declaration assembly for a real prepared binding. It uses
// successful descriptor observations and the same canonical inventory on both
// Native arrays and Emulated scalar expansion; no extra resource identities.
class OriginalBindlessRootAssemblyFixture final {
public:
    template <class Binding>
    static auto prepare(const std::shared_ptr<const rhi::BindlessSnapshot>& snapshot,
        const std::shared_ptr<const Binding>& binding,
        const std::shared_ptr<rhi::OriginalCommandResourceSources>& inventory,
        rhi::GpuDevice& device,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const char*& id,
        const rhi::OriginalCommandBindlessRootSource::OrdinaryDomain* ordinary = nullptr,
        std::size_t ordinaryCount = 0u)
    {
        using namespace rhi;
        using Sources = OriginalCommandResourceSources;
        using Root = OriginalCommandBindlessRootSource;
        const auto& artifact = binding->authorization()->package().artifact();
        core::pmr::Vector<OriginalCommandDescriptorSetElement> elements;
        for (const auto& shape : artifact.physicalLayout.bindings) {
            for (std::uint32_t index = 0u; index < shape.arrayCount; ++index) {
                const auto written = snapshot->descriptorSet()->observeSuccessfulBindingWrite(
                    shape.binding, index, id);
                assert(written && !id);
                OriginalCommandDescriptorSetElement element;
                element.write = written->write();
                element.stages = bindlessPipelineStagesForShader(shape.stages);
                element.access = shape.type == DescriptorType::Sampler ? ResourceAccess::None
                    : shape.type == DescriptorType::StorageTexture
                        && shape.storageTextureAccess == StorageTextureAccess::WriteOnly
                    ? ResourceAccess::ShaderWrite
                    : shape.type == DescriptorType::StorageBuffer
                    ? ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite
                    : ResourceAccess::ShaderRead;
                for (const auto& table : artifact.tables) {
                    const auto count =
                        artifact.execution == BindlessExecution::Emulated ? table.capacity : 1u;
                    if (shape.binding >= table.firstBinding
                        && shape.binding - table.firstBinding < count
                        && shape.type == DescriptorType::StorageBuffer
                        && table.operations == BindlessOperation::Store)
                        element.access = ResourceAccess::ShaderWrite;
                }
                bool found = false;
                for (std::size_t i = 0u; i < inventory->sources().size(); ++i) {
                    const auto& source = inventory->sources()[i];
                    if (Sources::resource(source) != element.write.resource)
                        continue;
                    std::visit(
                        [&](const auto& owner) {
                            using T = std::remove_const_t<
                                typename std::decay_t<decltype(owner)>::element_type>;
                            if constexpr (T::domain == CommandReferenceDomain::Buffer
                                || T::domain == CommandReferenceDomain::TextureView
                                || T::domain == CommandReferenceDomain::Sampler) {
                                element.sourceIndex = i;
                                element.registrationGeneration =
                                    owner->originalObject()
                                        ->originalResourceRegistration()
                                        .generation();
                                if constexpr (T::domain == CommandReferenceDomain::Buffer) {
                                    DescriptorBufferBindingRangeContract::Resolved range;
                                    assert(!DescriptorBufferBindingRangeContract::resolve(
                                        element.write.bufferBindingRange,
                                        owner->layout().sizeBytes,
                                        0u,
                                        range));
                                    element.bufferAccessOffset = range.offsetBytes;
                                    element.bufferAccessBytes = range.sizeBytes;
                                }
                                found = true;
                            }
                        },
                        source);
                    if (found)
                        break;
                }
                assert(found);
                elements.push_back(element);
            }
        }
        std::size_t ordinaryElements = 0u;
        for (std::size_t i = 0u; i < ordinaryCount; ++i)
            ordinaryElements += ordinary[i].source->frozenElements().size();
        assert(ordinaryCount <= UINT32_MAX && ordinaryElements <= UINT32_MAX);
        auto root = Root::prepareOriginal(snapshot,
            binding,
            inventory,
            elements.data(),
            elements.size(),
            ordinary,
            ordinaryCount,
            {ordinaryCount ? 2u : 1u,
                true,
                static_cast<std::uint32_t>(elements.size()),
                Root::retainedOwnerCount + Root::cpuOwnerSlots,
                quota->limitBytes(),
                static_cast<std::uint32_t>(ordinaryCount),
                static_cast<std::uint32_t>(ordinaryElements)},
            quota,
            device,
            id);
        if (root)
            id = Sources::appendOriginalTyped(inventory, root, device);
        return root;
    }
};
} // namespace arp::test
