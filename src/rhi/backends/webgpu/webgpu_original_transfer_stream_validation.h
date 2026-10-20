#pragma once
#include "rhi/backends/webgpu/webgpu_original_transfer_state_validation.h"

namespace arp::rhi_webgpu {
// OriginalTransferStreamValidation/1: full copy-only profile, no execution authority.
class WebGpuOriginalTransferStreamValidation final {
public:
    static const char* validate(
        const std::shared_ptr<const WebGpuOriginalResourceStates>& states) noexcept
    {
        if (!states || !states->hasQuotaBoundSharedOwner(states))
            return "RHI-WEBGPU-ORIGINAL-STATE-OWNER";
        if (const auto* id = states->validate())
            return id;
        using K = rhi::CommandUsageKind;
        const auto& inputs = *states->inputs();
        const auto& usage = *inputs.scalars()->usage();
        const auto& graph = *usage.projection();
        const auto& commands = usage.commands();
        if (commands.size() != 4u || commands[0].kind != K::BeginTransferPass
            || commands[2].kind != K::EndTransferPass || commands[3].kind != K::Finish)
            return "RHI-WEBGPU-ORIGINAL-TRANSFER-LANGUAGE";
        switch (commands[1].kind) {
        case K::BufferCopy:
        case K::BufferToTextureCopy:
        case K::TextureToBufferCopy:
        case K::TextureToTextureCopy:
            break;
        default:
            return "RHI-WEBGPU-ORIGINAL-TRANSFER-LANGUAGE";
        }
        if (!graph.transfer() || graph.resources() || !graph.pipelines().empty()
            || !graph.physicalGroups().empty() || graph.references().size() != 2u || inputs.passes()
            || inputs.descriptors().size() != 1u || inputs.descriptors()[0].commandIndex != 1u
            || states->sourceReferences() != graph.transfer()->resources().size()
            || states->states().size() != graph.transfer()->resources().size())
            return "RHI-WEBGPU-ORIGINAL-TRANSFER-SOURCE-COVERAGE";
        // Original inputs already bind the exact copied descriptor to ordered
        // original references and validate its format/ranges/write seal. The
        // shared state validator additionally checks complete order/current state.
        return WebGpuOriginalTransferStateValidation::validate(states, 0u).diagnostic;
    }
};
} // namespace arp::rhi_webgpu
