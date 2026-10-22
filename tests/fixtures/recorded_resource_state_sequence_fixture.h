#pragma once
#include "core/memory/cpu_allocator.h"
#include "rhi/frontend/resource_state_sequence.h"

#include <array>
#include <cassert>
#include <cstdio>

namespace arp::test {
class RecordedResourceStateSequenceFixture final {
public:
    static void verify()
    {
        using namespace rhi;
        using Sequence = semantics::RecordedResourceStateSequence;
        using T = Sequence::Transition;
        std::array<std::optional<T>, 4> transitions{
            {{T{{1u}, ResourceState::Undefined, ResourceState::CopyDst}},
                {},
                {T{{2u}, ResourceState::CopySrc, ResourceState::ShaderRead}},
                {T{{1u}, ResourceState::CopyDst, ResourceState::CopySrc}}}};
        const auto initial = [](GpuResourceId id) -> std::optional<ResourceState> {
            if (id.value == 1u)
                return ResourceState::Undefined;
            if (id.value == 2u)
                return ResourceState::CopySrc;
            return {};
        };
        ResourceState output = ResourceState::Present;
        const auto resolve = [&](GpuResourceId id, std::size_t i) {
            return Sequence::resolve(
                id,
                i,
                transitions.size(),
                initial,
                [&](std::size_t index) {
                    return transitions[index];
                },
                output);
        };
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!resolve({1u}, 0u) && output == ResourceState::Undefined);
        assert(!resolve({1u}, 3u) && output == ResourceState::CopyDst);
        assert(!resolve({1u}, 4u) && output == ResourceState::CopySrc);
        assert(!resolve({2u}, 4u) && output == ResourceState::ShaderRead);
        assert(resolve({3u}, 4u) && output == ResourceState::ShaderRead);
        assert(resolve({1u}, 5u) && output == ResourceState::ShaderRead);
        transitions[3]->before = ResourceState::CopySrc;
        assert(std::string_view(resolve({1u}, 4u)) == "RHI-STATE-SEQUENCE-BEFORE-MISMATCH"
            && output == ResourceState::ShaderRead);
        transitions[3]->before = ResourceState::CopyDst;
        transitions[3]->after = static_cast<ResourceState>(UINT32_MAX);
        assert(resolve({1u}, 4u) && output == ResourceState::ShaderRead);
        transitions[3]->after = ResourceState::Undefined;
        assert(resolve({1u}, 4u) && output == ResourceState::ShaderRead);
        ResourceBarrierDesc barrier{{1u},
            {},
            ResourceState::Undefined,
            ResourceState::CopyDst,
            PipelineStage::TopOfPipe,
            PipelineStage::Transfer,
            ResourceAccess::None,
            ResourceAccess::TransferWrite};
        assert(!Sequence::validateBarrier(barrier, ResourceState::Undefined));
        barrier.sourceStages = PipelineStage::Transfer;
        assert(std::string_view(Sequence::validateBarrier(barrier, ResourceState::Undefined))
            == "RHI-STATE-SEQUENCE-DISCARD-SCOPE");
        barrier.before = ResourceState::CopySrc;
        barrier.sourceAccess = ResourceAccess::TransferRead;
        assert(!Sequence::validateBarrier(barrier, ResourceState::CopySrc));
        assert(Sequence::validateBarrier(barrier, ResourceState::CopyDst));
        barrier.destinationAccess = ResourceAccess::ShaderRead;
        assert(Sequence::validateBarrier(barrier, ResourceState::CopySrc));
        barrier.destinationAccess = ResourceAccess::TransferWrite;
        barrier.destinationStages = PipelineStage::ComputeShader;
        assert(Sequence::validateBarrier(barrier, ResourceState::CopySrc));
        ResourceMemoryBarrierDesc memory{{1u},
            PipelineStage::Transfer,
            PipelineStage::Transfer,
            ResourceAccess::TransferWrite,
            ResourceAccess::TransferWrite};
        assert(!Sequence::validateMemoryBarrier(memory, ResourceState::CopyDst));
        assert(Sequence::validateMemoryBarrier(memory, ResourceState::Undefined));
        memory.destinationStages = PipelineStage::ComputeShader;
        memory.destinationAccess = ResourceAccess::ShaderRead;
        assert(std::string_view(Sequence::validateMemoryBarrier(memory, ResourceState::CopyDst))
            == "RHI-ACCESS-STATE-MISMATCH");
        memory.resource = {};
        assert(Sequence::validateMemoryBarrier(memory, ResourceState::ShaderWrite));
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::puts(
            "BINDLESS_RHI_STATE_SEQUENCE_OK original-order transactional before discard access enums allocation0");
    }
};
} // namespace arp::test
