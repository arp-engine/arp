#pragma once
#include "rhi/frontend/descriptor_buffer_write_validation.h"
#include "rhi/frontend/pipeline_shader_interface_validation.h"

namespace arp::rhi {
// Independent selected publication contract. Neither ordinary array writes nor
// successful registration authorize the scalar group or a command submission.
struct OriginalDepthTablePublicationRequest {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxRetainedOwnerSlots = 0u, maxCallbacks = 0u;
    std::uint64_t maxRetainedAllocationBytes = 0u;
    std::array<std::uint64_t, 3u> maxCpuBytes{};
    static constexpr std::uint32_t maxBindings = 3u;
};

struct OriginalDepthTablePublicationWrite {
    DescriptorTableWrite write;
    std::shared_ptr<const OriginalCommandTextureViewSource> view;
};

class OriginalDepthTablePublicationContract final {
public:
    static constexpr auto callbackCount = OriginalFiniteDescriptorWriteContract::callbackCount;
    static constexpr auto quotaCount = OriginalFiniteDescriptorWriteContract::quotaCount;
    // Existing state/group, request custody, arena, both pipeline slots, quotas
    // and native callback slots; per element retains a complete original view
    // source and a native view/texture/allocation observation plus payload view.
    static constexpr std::uint32_t retainedOwnerSlots =
        OriginalFiniteDescriptorWriteContract::fixedOwnerSlots + 1u
        + OriginalDepthTablePublicationRequest::maxBindings
            * (1u + OriginalCommandTextureViewSource::completeRetainedOwnerCount + 3u + 1u);

    static constexpr const char* validate(const OriginalDepthTablePublicationRequest& r) noexcept
    {
        if (r.contractVersion != 1u)
            return "RHI-ORIGINAL-TABLE-PUBLICATION-VERSION";
        if (!r.prototypeOptIn)
            return "RHI-ORIGINAL-TABLE-PUBLICATION-OPT-IN";
        if (r.maxRetainedOwnerSlots < retainedOwnerSlots || r.maxCallbacks != callbackCount
            || !r.maxRetainedAllocationBytes)
            return "RHI-ORIGINAL-TABLE-PUBLICATION-BUDGET";
        for (const auto bytes : r.maxCpuBytes)
            if (!bytes)
                return "RHI-ORIGINAL-TABLE-PUBLICATION-CPU-BUDGET";
        return nullptr;
    }

    using Facts = OriginalFiniteDescriptorWriteContract::Facts;
    using Decision = OriginalFiniteDescriptorWriteContract::Decision;

    static constexpr Decision decide(Backend backend, Facts facts = {}) noexcept
    {
        return semantics::DescriptorLowering::OriginalDepthTablePublication_decide<Decision>(
            backend, facts);
    }

    static const char* validateWrites(const PipelineLayoutDesc& layout,
        const DescriptorTableDesc& table,
        const OriginalDepthTablePublicationWrite* writes,
        std::size_t count) noexcept
    {
        if (!writes || count != OriginalDepthTablePublicationRequest::maxBindings)
            return "RHI-ORIGINAL-TABLE-WRITE-COVERAGE";
        std::array<DescriptorTableWrite, OriginalDepthTablePublicationRequest::maxBindings> intent;
        for (std::size_t i = 0u; i < count; ++i)
            intent[i] = writes[i].write;
        if (const auto* id = PipelineShaderDeclarationContract::validateDepthTableExpansionWrites(
                {1u, true}, layout, table, intent.data(), intent.size()))
            return id;
        for (std::size_t i = 0u; i < count; ++i) {
            if (const auto* id = OriginalCommandTextureViewSource::validateOriginal(writes[i].view))
                return id;
            if (writes[i].view->resource() != writes[i].write.resource)
                return "RHI-ORIGINAL-TABLE-PUBLICATION-VIEW-IDENTITY";
            const auto& view = writes[i].view->originalObject()->desc();
            const auto& texture = writes[i].view->originalParentLease()->originalObject()->desc();
            if (view.dimension != TextureViewDimension::Texture2D
                || static_cast<std::uint32_t>(texture.usage & TextureUsage::Sampled) == 0u
                || static_cast<std::uint32_t>(view.usage & TextureUsage::Sampled) == 0u
                || view.format != texture.format)
                return "RHI-ORIGINAL-TABLE-PUBLICATION-VIEW-SHAPE";
            if (sampledDepthViewDiagnostic(true, texture.format, view.aspect))
                return "RHI-ORIGINAL-TABLE-PUBLICATION-VIEW-SHAPE";
        }
        return nullptr;
    }
};
} // namespace arp::rhi
