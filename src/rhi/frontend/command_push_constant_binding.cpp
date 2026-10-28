#include "rhi/frontend/command_push_constant_binding.h"

namespace arp::rhi::semantics {
ValidationResult validatePushConstantBinding(const PipelineLayout* pipelineLayout,
    ShaderStage passType,
    ShaderStage stages,
    std::uint32_t offsetBytes,
    std::uint32_t sizeBytes,
    const ShaderReflectionDesc* reflection)
{
    if (!pipelineLayout) {
        return {false,
            std::pmr::string(PushConstantDiagCode::kNoPipeline)
                + "pipeline must be bound before push constant update"};
    }
    return validatePushConstantBinding(pipelineLayout->desc().pushConstantRanges,
        passType,
        stages,
        offsetBytes,
        sizeBytes,
        reflection);
}
} // namespace arp::rhi::semantics
