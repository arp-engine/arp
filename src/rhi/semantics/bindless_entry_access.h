#pragma once
#include "rhi/semantics/bindless_pipeline_interface.h"

namespace arp::rhi::semantics {
// EntryAccess/1 projects shader consumption, independently of the full retained
// snapshot and of the native API's bound-resource usage/alias requirements.
// /2 additionally projects selected Graphics stages and ordinary declarations
// of the compiler-pinned visibility consumer. It grants neither recording nor
// completion authority. Bound-resource API alias validation is independent.
// /3 projects the same pinned family's Compute entries, including ordinary
// aliases retained for Graphics but explicitly inactive in Compute reflection.
// /4 uses the same Graphics projection for authenticated SceneContent/1
// color/depth/shadow entry pairs. It does not authorize scene execution.
class OriginalBindlessEntryAccessContract final {
public:
    struct Decision {
        bool projectionAvailable = false, validatorOnly = false, executionAvailable = false;
        const char* diagnostic = "RHI-BINDLESS-ENTRY-ACCESS-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version != 1u && version != 2u && version != 3u && version != 4u)
            return {false, false, false, "RHI-BINDLESS-ENTRY-ACCESS-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {true, true, false, nullptr};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {true, false, false, nullptr};
        case Backend::WebGL:
            return {false, false, false, "RHI-WEBGL1-BINDLESS-ENTRY-ACCESS-UNSUPPORTED"};
        }
        return {};
    }

    // The backend selector authenticates each issued stage against immutable
    // package bytes. Every stage must prove activity or explicit inactivity;
    // an active Vertex binding cannot hide missing Fragment evidence.
    template <class Stages, class Select>
    static const char* selectedBindings(const Stages& stages,
        BindlessExecution execution,
        std::uint32_t set,
        std::uint32_t index,
        Select&& select,
        bool& active) noexcept
    {
        active = false;
        if (stages.size() != 1u && stages.size() != 2u)
            return "RHI-BINDLESS-ENTRY-ACCESS-PIPELINE-STAGES";
        ShaderStage seen = ShaderStage::None;
        bool pending = false;
        for (const auto& stage : stages) {
            if ((stages.size() == 1u && stage.stage != ShaderStage::Compute)
                || (stages.size() == 2u && stage.stage != ShaderStage::Vertex
                    && stage.stage != ShaderStage::Fragment)
                || (seen & stage.stage) != ShaderStage::None)
                return "RHI-BINDLESS-ENTRY-ACCESS-PIPELINE-STAGES";
            seen = seen | stage.stage;
            const auto* reflection = select(stage);
            if (!reflection)
                return "RHI-BINDLESS-ENTRY-ACCESS-ISSUED-MODULE";
            bool consumed = false;
            if (const auto* id = binding(*reflection, execution, set, index, stage.stage, consumed))
                return id;
            pending = pending || consumed;
        }
        active = pending;
        return nullptr;
    }

    template <class Reflection>
    static const char* binding(const Reflection& reflection,
        BindlessExecution execution,
        std::uint32_t set,
        std::uint32_t binding,
        ShaderStage stages,
        bool& active) noexcept
    {
        active = false;
        if (execution != BindlessExecution::Native && execution != BindlessExecution::Emulated)
            return "RHI-BINDLESS-ENTRY-ACCESS-EXECUTION-MODE";
        std::uint32_t count = 0u;
        for (const auto& logical : reflection.bindings) {
            if (logical.set != set || logical.binding != binding)
                continue;
            if (!logical.stageMask || static_cast<ShaderStage>(logical.stageMask) != stages)
                return "RHI-BINDLESS-ENTRY-ACCESS-STAGES";
            ++count;
        }
        if (count > 1u)
            return "RHI-BINDLESS-ENTRY-ACCESS-DUPLICATE";
        if (count == 1u) {
            active = true;
            return nullptr;
        }
        // Logical absence alone never proves inactivity. The verified target
        // maps must explicitly and uniquely identify the declaration as unused.
        const bool inactive = execution == BindlessExecution::Emulated
            ? BindlessPipelineInterfaceContract::explicitWebGpuInactiveBinding(
                  reflection, set, binding)
            : execution == BindlessExecution::Native
                && BindlessPipelineInterfaceContract::explicitMetalInactiveBinding(
                    reflection, set, binding);
        return inactive ? nullptr : "RHI-BINDLESS-ENTRY-ACCESS-INACTIVE-UNPROVEN";
    }
};
} // namespace arp::rhi::semantics
