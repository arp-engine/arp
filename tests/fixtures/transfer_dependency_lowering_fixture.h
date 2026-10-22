#pragma once
#include "core/memory/cpu_allocator.h"
#include "rhi/semantics/transfer_dependency_lowering.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class TransferDependencyLoweringFixture final {
public:
    static void verify()
    {
        using namespace rhi;
        using Lowering = semantics::TransferDependencyLowering;
        ResourceBarrierDesc desc{{7u},
            {},
            ResourceState::CopyDst,
            ResourceState::CopySrc,
            PipelineStage::Transfer,
            PipelineStage::Transfer,
            ResourceAccess::TransferWrite,
            ResourceAccess::TransferRead};
        const auto allocationCount = core::defaultCpuAllocator().stats().allocationCount;
        const auto query = [&](Backend backend, std::uint32_t version = 1u, bool optIn = true) {
            return Lowering::transition(backend, version, optIn, desc, desc.before, false);
        };
        const auto fail = [](Lowering::Decision d, const char* id) {
            assert(d.outcome == CapabilityMode::DiagnosticFailure
                && d.operation == Lowering::Operation::DiagnosticFailure);
            assert(d.diagnostic && std::string_view(d.diagnostic) == id);
        };
        fail({}, "RHI-TRANSFER-DEPENDENCY-UNDECIDED");
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            fail(query(backend, 0u), "RHI-TRANSFER-DEPENDENCY-VERSION");
            fail(query(backend, 2u), "RHI-TRANSFER-DEPENDENCY-VERSION");
            fail(query(backend, 1u, false), "RHI-TRANSFER-DEPENDENCY-OPT-IN");
            const auto d = query(backend);
            if (backend == Backend::WebGPU)
                assert(d.outcome == CapabilityMode::Equivalent
                    && d.operation == Lowering::Operation::ImplicitOrderedUsage && !d.diagnostic);
            else
                fail(d,
                    backend == Backend::Null ? "RHI-TRANSFER-DEPENDENCY-NULL-VALIDATOR-ONLY"
                                             : "RHI-TRANSFER-DEPENDENCY-BACKEND-PAYLOAD-UNPROVEN");
        }
        fail(query(static_cast<Backend>(UINT32_MAX)), "RHI-TRANSFER-DEPENDENCY-BACKEND-INVALID");
        desc.resource = {};
        fail(query(Backend::WebGPU), "RHI-STATE-SEQUENCE-RESOURCE");
        desc.resource = {7u};
        desc.sourceStages = PipelineStage::Transfer | PipelineStage::ComputeShader;
        fail(query(Backend::WebGPU), "RHI-ACCESS-STAGE-MISMATCH");
        desc.sourceStages = PipelineStage::Transfer;
        desc.destinationAccess = ResourceAccess::TransferWrite;
        fail(query(Backend::WebGPU), "RHI-ACCESS-STATE-MISMATCH");
        desc.destinationAccess = ResourceAccess::TransferRead;
        desc.before = ResourceState::ShaderRead;
        desc.sourceStages = PipelineStage::ComputeShader;
        desc.sourceAccess = ResourceAccess::ShaderRead;
        fail(query(Backend::WebGPU), "RHI-TRANSFER-DEPENDENCY-SCOPE-UNPROVEN");
        desc.before = ResourceState::Undefined;
        desc.sourceStages = PipelineStage::TopOfPipe;
        desc.sourceAccess = ResourceAccess::None;
        assert(!query(Backend::WebGPU).diagnostic);
        desc.sourceStages = PipelineStage::None;
        fail(query(Backend::WebGPU), "RHI-STATE-SEQUENCE-DISCARD-SCOPE");
        desc.before = ResourceState::CopyDst;
        desc.sourceStages = PipelineStage::Transfer;
        desc.sourceAccess = ResourceAccess::TransferWrite;
        const TextureSubresourceRange range{0u, 3u, 0u, 2u, TextureAspect::Color};
        desc.textureSubresources = range;
        fail(query(Backend::WebGPU), "RHI-TRANSFER-DEPENDENCY-RANGE-UNPROVEN");
        assert(!Lowering::transition(Backend::WebGPU, 1u, true, desc, desc.before, true, range)
                .diagnostic);
        --desc.textureSubresources.mipLevelCount;
        fail(Lowering::transition(Backend::WebGPU, 1u, true, desc, desc.before, true, range),
            "RHI-TRANSFER-DEPENDENCY-RANGE-UNPROVEN");
        for (const auto state : {ResourceState::CopySrc, ResourceState::CopyDst}) {
            const auto access = state == ResourceState::CopySrc ? ResourceAccess::TransferRead
                                                                : ResourceAccess::TransferWrite;
            const ResourceMemoryBarrierDesc memory{
                {7u}, PipelineStage::Transfer, PipelineStage::Transfer, access, access};
            assert(!Lowering::memory(Backend::WebGPU, 1u, true, memory, state).diagnostic);
            fail(Lowering::memory(Backend::Null, 1u, true, memory, state),
                "RHI-TRANSFER-DEPENDENCY-NULL-VALIDATOR-ONLY");
            fail(Lowering::memory(Backend::Vulkan, 1u, true, memory, state),
                "RHI-TRANSFER-DEPENDENCY-BACKEND-PAYLOAD-UNPROVEN");
            fail(Lowering::memory(Backend::Metal, 1u, true, memory, state),
                "RHI-TRANSFER-DEPENDENCY-BACKEND-PAYLOAD-UNPROVEN");
        }
        const ResourceMemoryBarrierDesc shader{{7u},
            PipelineStage::ComputeShader,
            PipelineStage::FragmentShader,
            ResourceAccess::ShaderRead,
            ResourceAccess::ShaderRead};
        fail(Lowering::memory(Backend::WebGPU, 1u, true, shader, ResourceState::ShaderRead),
            "RHI-TRANSFER-DEPENDENCY-SCOPE-UNPROVEN");
        assert(core::defaultCpuAllocator().stats().allocationCount == allocationCount);
        std::puts(
            "BINDLESS_RHI_TRANSFER_DEPENDENCY_CONTRACT_OK all4 version opt-in transfer scope whole-range discard metadata-only allocation0");
    }
};
} // namespace arp::test
