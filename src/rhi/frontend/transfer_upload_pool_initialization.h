#pragma once

#include "rhi/frontend/command_transfer_validation.h"

namespace arp::rhi {

struct OriginalTexturePoolInitializationBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint64_t maxPackedBytes = 0u, maxMipMappings = 0u;
};

// OriginalTexturePoolInitialization/1 authenticates existing frozen packing at
// one original upload ordinal. /2 additionally requires the exact registered
// destination and native device. It grants page initialization, never recording,
// resource-state mutation, texture-content publication or command execution.
class OriginalTexturePoolInitializationContract final {
public:
    using Budget = OriginalTexturePoolInitializationBudget;

    struct Decision {
        bool validatorOnly = false, initializationAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-TEXTURE-POOL-INITIALIZATION-BACKEND";
        bool registeredDestinationRequired = false;
    };

    static constexpr const char* validate(const Budget& budget) noexcept
    {
        if (budget.contractVersion != 1u && budget.contractVersion != 2u)
            return "RHI-TEXTURE-POOL-INITIALIZATION-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-TEXTURE-POOL-INITIALIZATION-OPT-IN";
        return budget.maxPackedBytes && budget.maxMipMappings
            ? nullptr
            : "RHI-TEXTURE-POOL-INITIALIZATION-BUDGET";
    }

    static constexpr Decision lowering(Backend backend, const Budget& budget) noexcept
    {
        if (const auto* id = validate(budget))
            return {false, false, false, id};
        switch (backend) {
        case Backend::Null:
            return {true,
                false,
                false,
                "RHI-TEXTURE-POOL-INITIALIZATION-NULL-VALIDATOR",
                budget.contractVersion == 2u};
        case Backend::Vulkan:
            return {false, true, false, nullptr, budget.contractVersion == 2u};
        case Backend::Metal:
            return {false,
                false,
                false,
                "RHI-TEXTURE-POOL-INITIALIZATION-METAL-PAYLOAD-MISSING",
                budget.contractVersion == 2u};
        case Backend::WebGPU:
            return {false,
                false,
                false,
                "RHI-TEXTURE-POOL-INITIALIZATION-WEBGPU-PAYLOAD-MISSING",
                budget.contractVersion == 2u};
        case Backend::WebGL:
            return {false,
                false,
                false,
                "RHI-TEXTURE-POOL-INITIALIZATION-WEBGL-UNSUPPORTED",
                budget.contractVersion == 2u};
        }
        return {};
    }

    static const char* inspect(const std::shared_ptr<const OriginalCommandInputs>& inputs,
        const OriginalCommandStreamBudget& stream,
        std::size_t commandIndex,
        const std::shared_ptr<const OriginalCommandTextureUploadPackingSource>& packing,
        const Budget& budget) noexcept
    {
        if (const auto* id = validate(budget))
            return id;
        if (const auto* id = OriginalCommandTextureUploadPackingSource::validateOriginal(packing))
            return id;
        if (packing->packing()->packedBytes().size() > budget.maxPackedBytes
            || packing->packing()->mipLayouts().size() > budget.maxMipMappings)
            return "RHI-TEXTURE-POOL-INITIALIZATION-CAPACITY";
        bool found = false;
        const auto* diagnostic = OriginalCommandStreamProgram::visitOriginal(inputs,
            stream,
            [&](const OriginalCommandStreamProgram::Operation& operation) noexcept -> const char* {
                if (operation.commandIndex != commandIndex)
                    return nullptr;
                if (operation.family != OriginalCommandStreamProgram::Family::TextureUpload)
                    return "RHI-TEXTURE-POOL-INITIALIZATION-ORDINAL";
                const auto& usage = *inputs->originalScalars()->originalUsage();
                const auto sourceIndex =
                    usage.references()[operation.firstReference + 1u].sourceIndex;
                const auto& source =
                    std::get<std::shared_ptr<const OriginalCommandTextureUploadSource>>(
                        usage.originalSources()->sources()[sourceIndex]);
                if (!core::SharedOwnerProjection::sameOwner(source, packing->originalSource()))
                    return "RHI-TEXTURE-POOL-INITIALIZATION-SOURCE-JOIN";
                const auto& input = std::get<TextureUploadOriginalInputArguments>(
                    inputs->descriptors()[operation.descriptorIndex].arguments)
                                        .originalInput();
                if (!core::SharedOwnerProjection::sameOwner(input, packing->originalInput()))
                    return "RHI-TEXTURE-POOL-INITIALIZATION-INPUT-JOIN";
                found = true;
                return nullptr;
            });
        return diagnostic ? diagnostic
            : found       ? nullptr
                          : "RHI-TEXTURE-POOL-INITIALIZATION-ORDINAL";
    }
};

} // namespace arp::rhi
