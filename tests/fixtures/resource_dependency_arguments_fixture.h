#pragma once

#include "core/cpu_allocator.h"
#include "rhi/frontend/command_arguments.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <type_traits>

namespace arp::test {
class ResourceDependencyArgumentsFixture final {
    using K = rhi::CommandUsageKind;
    using Arguments = rhi::ResourceDependencyArguments;

    template <class Desc, std::size_t N>
    static void mutations(const Arguments& frozen,
        K kind,
        const Desc& original,
        const std::array<void (*)(Desc&), N>& mutate)
    {
        assert(frozen.kind() == kind && frozen.matches(kind, original));
        for (const auto change : mutate) {
            auto altered = original;
            change(altered);
            assert(!frozen.matches(kind, altered));
            auto output = altered;
            assert(frozen.write(kind, output) && frozen.matches(kind, output));
        }
        auto output = original;
        assert(!frozen.matches(K::Finish, original) && !frozen.write(K::Finish, output)
            && frozen.matches(kind, output));
    }

public:
    static void verify()
    {
        using namespace rhi;
        static_assert(std::is_trivially_copyable_v<Arguments>);
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        const ResourceBarrierDesc transition{{19u},
            {2u, 3u, 5u, 7u, TextureAspect::Depth},
            ResourceState::ShaderWrite,
            ResourceState::ShaderRead,
            PipelineStage::ComputeShader,
            PipelineStage::FragmentShader,
            ResourceAccess::ShaderWrite,
            ResourceAccess::ShaderRead};
        const std::array<void (*)(ResourceBarrierDesc&), 12> transitionMutations{
            {[](auto& d) {
                 d.resource.value++;
             },
                [](auto& d) {
                    d.textureSubresources.baseMipLevel++;
                },
                [](auto& d) {
                    d.textureSubresources.mipLevelCount++;
                },
                [](auto& d) {
                    d.textureSubresources.baseArrayLayer++;
                },
                [](auto& d) {
                    d.textureSubresources.arrayLayerCount++;
                },
                [](auto& d) {
                    d.textureSubresources.aspect = TextureAspect::Stencil;
                },
                [](auto& d) {
                    d.before = ResourceState::CopyDst;
                },
                [](auto& d) {
                    d.after = ResourceState::CopySrc;
                },
                [](auto& d) {
                    d.sourceStages = PipelineStage::Transfer;
                },
                [](auto& d) {
                    d.destinationStages = PipelineStage::DrawIndirect;
                },
                [](auto& d) {
                    d.sourceAccess = ResourceAccess::TransferWrite;
                },
                [](auto& d) {
                    d.destinationAccess = ResourceAccess::IndirectCommandRead;
                }}};
        for (const auto kind : {K::BufferBarrier, K::TextureBarrier}) {
            auto authoring = transition;
            const auto frozen = Arguments::capture(kind, authoring);
            assert(frozen);
            authoring = {};
            mutations(*frozen, kind, transition, transitionMutations);
            const auto other = kind == K::BufferBarrier ? K::TextureBarrier : K::BufferBarrier;
            assert(!frozen->matches(other, transition));
            auto output = transition;
            assert(!frozen->write(other, output) && frozen->matches(kind, output));
        }
        const ResourceMemoryBarrierDesc memory{{23u},
            PipelineStage::FragmentShader,
            PipelineStage::ComputeShader,
            ResourceAccess::ShaderRead,
            ResourceAccess::ShaderWrite};
        const std::array<void (*)(ResourceMemoryBarrierDesc&), 5> memoryMutations{
            {[](auto& d) {
                 d.resource.value++;
             },
                [](auto& d) {
                    d.sourceStages = PipelineStage::Transfer;
                },
                [](auto& d) {
                    d.destinationStages = PipelineStage::DrawIndirect;
                },
                [](auto& d) {
                    d.sourceAccess = ResourceAccess::TransferWrite;
                },
                [](auto& d) {
                    d.destinationAccess = ResourceAccess::IndirectCommandRead;
                }}};
        for (const auto kind : {K::BufferMemoryBarrier, K::TextureMemoryBarrier}) {
            auto authoring = memory;
            const auto frozen = Arguments::capture(kind, authoring);
            assert(frozen);
            authoring = {};
            mutations(*frozen, kind, memory, memoryMutations);
            const auto other =
                kind == K::BufferMemoryBarrier ? K::TextureMemoryBarrier : K::BufferMemoryBarrier;
            auto output = memory;
            assert(!frozen->matches(other, memory) && !frozen->write(other, output)
                && frozen->matches(kind, output));
        }
        const AliasingBarrierDesc alias{{19u},
            {23u},
            41u,
            PipelineStage::ComputeShader,
            PipelineStage::FragmentShader,
            ResourceAccess::ShaderWrite,
            ResourceAccess::ShaderRead,
            3u,
            5u,
            true};
        const auto aliasSnapshot = Arguments::capture(alias);
        const std::array<void (*)(AliasingBarrierDesc&), 10> aliasMutations{
            {[](auto& d) {
                 d.beforeResource.value++;
             },
                [](auto& d) {
                    d.afterResource.value++;
                },
                [](auto& d) {
                    d.aliasGroup++;
                },
                [](auto& d) {
                    d.sourceStages = PipelineStage::Transfer;
                },
                [](auto& d) {
                    d.destinationStages = PipelineStage::DrawIndirect;
                },
                [](auto& d) {
                    d.sourceAccess = ResourceAccess::TransferWrite;
                },
                [](auto& d) {
                    d.destinationAccess = ResourceAccess::IndirectCommandRead;
                },
                [](auto& d) {
                    d.sourceQueueRole++;
                },
                [](auto& d) {
                    d.destinationQueueRole++;
                },
                [](auto& d) {
                    d.ownershipHandoff = false;
                }}};
        mutations(aliasSnapshot, K::AliasBarrier, alias, aliasMutations);
        const auto transitionSnapshot = *Arguments::capture(K::TextureBarrier, transition);
        const auto memorySnapshot = *Arguments::capture(K::TextureMemoryBarrier, memory);
        auto memoryOutput = memory;
        auto transitionOutput = transition;
        auto aliasOutput = alias;
        assert(!transitionSnapshot.matches(K::TextureBarrier, memory)
            && !transitionSnapshot.write(K::TextureBarrier, memoryOutput)
            && memorySnapshot.matches(K::TextureMemoryBarrier, memoryOutput));
        assert(!memorySnapshot.matches(K::TextureMemoryBarrier, transition)
            && !memorySnapshot.write(K::TextureMemoryBarrier, transitionOutput)
            && transitionSnapshot.matches(K::TextureBarrier, transitionOutput));
        assert(!transitionSnapshot.write(K::TextureBarrier, aliasOutput)
            && aliasSnapshot.matches(K::AliasBarrier, aliasOutput));
        for (std::size_t i = 0u; i < static_cast<std::size_t>(K::Count); ++i) {
            const auto kind = static_cast<K>(i);
            assert(static_cast<bool>(Arguments::capture(kind, transition))
                == (kind == K::BufferBarrier || kind == K::TextureBarrier));
            assert(static_cast<bool>(Arguments::capture(kind, memory))
                == (kind == K::BufferMemoryBarrier || kind == K::TextureMemoryBarrier));
        }
        assert(!Arguments::capture(K::Count, transition)
            && !Arguments::capture(static_cast<K>(UINT32_MAX), memory));
        // Exact storage cannot silently sanitize unknown input. Execution must
        // later reject these values through a separate semantic validator.
        auto invalid = transition;
        invalid.before = static_cast<ResourceState>(UINT32_MAX);
        invalid.sourceStages = static_cast<PipelineStage>(UINT32_MAX);
        invalid.textureSubresources.aspect = static_cast<TextureAspect>(UINT32_MAX);
        const auto stored = Arguments::capture(K::TextureBarrier, invalid);
        assert(stored && stored->matches(K::TextureBarrier, invalid));
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::puts(
            "BINDLESS_RHI_DEPENDENCY_ARGUMENTS_OK transition12 memory5 alias10 type kind caller-drop allocation0");
    }
};
} // namespace arp::test
