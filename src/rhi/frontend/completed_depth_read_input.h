#pragma once
#include "rhi/frontend/completed_depth_texture_source.h"
#include "rhi/gpu_commands.h"
#include "rhi/semantics/command_state_lowering.h"

namespace arp::rhi {
// Immutable input facts only. A shader-access declaration does not authorize
// its state transition, shader consumption, recording or native completion.
class ARP_API OriginalCompletedDepthReadInputContract final {
public:
    struct Input {
        std::shared_ptr<const OriginalCompletedDepthTextureSource> source;
        ResourceState state = ResourceState::Undefined;
        PipelineStage stages = PipelineStage::None;
        ResourceAccess access = ResourceAccess::None;
    };

    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t maxCells = 0u, maxTextureTexelBytes = 0u;
        std::uint32_t maxRetainedOwners = 0u;
    };

    struct Inspection {
        const char* diagnostic = nullptr;
        std::uint64_t cells = 0u, textureTexelBytes = 0u;

        constexpr bool nativeExecutionProven() const noexcept { return false; }
    };

    static constexpr auto lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        return semantics::CompletedDepthReadInputLowering::lowering(backend, version);
    }

    static constexpr const char* validateIntent(const Budget& b) noexcept
    {
        if (b.contractVersion != 1u && b.contractVersion != 2u && b.contractVersion != 3u)
            return "RHI-COMPLETED-DEPTH-READ-INPUT-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-COMPLETED-DEPTH-READ-INPUT-OPT-IN";
        return b.maxCells && b.maxTextureTexelBytes
                && b.maxRetainedOwners
                    >= OriginalCompletedDepthTextureSource::completeRetainedOwnerCount
            ? nullptr
            : "RHI-COMPLETED-DEPTH-READ-INPUT-BUDGET";
    }

    static constexpr const char* validateAccess(
        ResourceState state, PipelineStage stages, ResourceAccess access) noexcept
    {
        return state == ResourceState::ShaderRead && access == ResourceAccess::ShaderRead
                && (stages == PipelineStage::ComputeShader
                    || stages == PipelineStage::FragmentShader)
            ? nullptr
            : "RHI-COMPLETED-DEPTH-READ-INPUT-ACCESS";
    }

    // Input3 validates ordinary native depth read facts for selected graphics
    // and compute entries. Input2 deliberately retains its compute-only gate.
    static constexpr const char* validateAccess(ResourceState state,
        PipelineStage stages,
        ResourceAccess access,
        std::uint32_t version) noexcept
    {
        if (version != 3u)
            return validateAccess(state, stages, access);
        const auto supported = PipelineStage::FragmentShader | PipelineStage::ComputeShader;
        return state == ResourceState::ShaderRead && access == ResourceAccess::ShaderRead
                && stages != PipelineStage::None && (stages & supported) == stages
            ? nullptr
            : "RHI-COMPLETED-DEPTH-READ-INPUT-ACCESS";
    }

    static Inspection inspect(const Input&,
        const std::shared_ptr<const OriginalCommandTextureViewSource>& canonicalView,
        const Budget&) noexcept;
};
} // namespace arp::rhi
