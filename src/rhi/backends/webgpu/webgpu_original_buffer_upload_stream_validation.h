#pragma once
#include "rhi/backends/webgpu/webgpu_original_transfer_state_validation.h"

namespace arp::rhi_webgpu {
class WebGpuOriginalBufferUploadStreamValidation final {
public:
    static const char* validate(
        const std::shared_ptr<const WebGpuOriginalResourceStates>& states) noexcept
    {
        if (!states || !states->hasQuotaBoundSharedOwner(states))
            return "RHI-WEBGPU-ORIGINAL-STATE-OWNER";
        if (const auto* id = states->validate())
            return id;
        const auto& inputs = *states->inputs();
        const auto& usage = *inputs.scalars()->usage();
        const auto& graph = *usage.projection();
        const auto& commands = usage.commands();
        using K = rhi::CommandUsageKind;
        if (commands.size() != 4u || commands[0].kind != K::BeginTransferPass
            || commands[1].kind != K::BufferUpload || commands[2].kind != K::EndTransferPass
            || commands[3].kind != K::Finish)
            return "RHI-WEBGPU-ORIGINAL-UPLOAD-LANGUAGE";
        if (!graph.transfer() || graph.resources() || !graph.pipelines().empty()
            || !graph.physicalGroups().empty() || graph.originalUploads().size() != 1u
            || graph.transfer()->resources().size() != 2u || graph.references().size() != 3u
            || inputs.passes() || inputs.descriptors().size() != 1u
            || inputs.descriptors()[0].commandIndex != 1u || states->sourceReferences() != 2u
            || states->states().size() != 2u
            || !std::holds_alternative<rhi::BufferUploadArguments>(
                inputs.descriptors()[0].arguments))
            return "RHI-WEBGPU-ORIGINAL-UPLOAD-SOURCE-COVERAGE";
        return WebGpuOriginalTransferStateValidation::validate(states, 0u).diagnostic;
    }
};
} // namespace arp::rhi_webgpu
