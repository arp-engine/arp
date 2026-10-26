#pragma once

#include "rhi/frontend/transfer_upload_content_validation.h"
#include "rhi/frontend/transfer_upload_pool_initialization.h"

namespace arp::rhi {

// Private ordinary whole-upload profile. The original Program remains the
// command vocabulary and owner graph; these entries bind its existing packing.
class OriginalOrdinaryTextureUploadContract final {
public:
    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t maxUploads = 0u, maxMipMappings = 0u, maxPackedBytes = 0u;
    };

    struct Binding {
        std::size_t commandIndex = 0u;
        std::shared_ptr<const OriginalCommandTextureUploadPackingSource> packing;
    };

    struct Decision {
        bool validatorOnly = false, privateRecordingAvailable = false;
        bool completionPublicationRequired = true;
        const char* diagnostic = "RHI-ORDINARY-TEXTURE-UPLOAD-BACKEND";
    };

    static constexpr const char* validate(const Budget& budget) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RHI-ORDINARY-TEXTURE-UPLOAD-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-ORDINARY-TEXTURE-UPLOAD-OPT-IN";
        return budget.maxUploads && budget.maxMipMappings && budget.maxPackedBytes
            ? nullptr
            : "RHI-ORDINARY-TEXTURE-UPLOAD-BUDGET";
    }

    static const char* inspectDestination(const TextureDesc& destination) noexcept
    {
        // Packed row counts for BC need an independently verified texel-height
        // lowering. Do not feed block rows to native bufferImageHeight.
        return isBlockCompressed(destination.format)
            ? "RHI-ORDINARY-TEXTURE-UPLOAD-COMPRESSED-PAYLOAD-MISSING"
            : nullptr;
    }

    static constexpr Decision lowering(Backend backend, const Budget& budget) noexcept
    {
        if (const auto* id = validate(budget))
            return {false, false, true, id};
        switch (backend) {
        case Backend::Null:
            return {true, false, true, "RHI-ORDINARY-TEXTURE-UPLOAD-NULL-VALIDATOR"};
        case Backend::Vulkan:
            return {false, true, true, nullptr};
        case Backend::Metal:
            return {false, false, true, "RHI-ORDINARY-TEXTURE-UPLOAD-METAL-PAYLOAD-MISSING"};
        case Backend::WebGPU:
            return {false, false, true, "RHI-ORDINARY-TEXTURE-UPLOAD-WEBGPU-PAYLOAD-MISSING"};
        case Backend::WebGL:
            return {false, false, true, "RHI-ORDINARY-TEXTURE-UPLOAD-WEBGL-UNSUPPORTED"};
        }
        return {};
    }

    static const char* inspect(const std::shared_ptr<const OriginalCommandStreamProgram>& program,
        const Budget& budget,
        const Binding* bindings,
        std::size_t count) noexcept
    {
        if (const auto* id = validate(budget))
            return id;
        if (const auto* id = OriginalCommandStreamProgram::validateOriginal(program))
            return id;
        if (!program->recordingBudget())
            return "RHI-ORDINARY-TEXTURE-UPLOAD-RECORDING-BUDGET";
        if (!bindings || !count || count > budget.maxUploads)
            return "RHI-ORDINARY-TEXTURE-UPLOAD-BINDINGS";
        const OriginalTexturePoolInitializationBudget initialization{
            2u, true, budget.maxPackedBytes, budget.maxMipMappings};
        std::uint64_t mips = 0u, bytes = 0u;
        for (std::size_t i = 0u; i < count; ++i) {
            const auto& entry = bindings[i];
            if (i && bindings[i - 1u].commandIndex >= entry.commandIndex)
                return "RHI-ORDINARY-TEXTURE-UPLOAD-ORDER";
            if (const auto* id =
                    OriginalTexturePoolInitializationContract::inspect(program->originalInputs(),
                        program->originalBudget(),
                        entry.commandIndex,
                        entry.packing,
                        initialization))
                return id;
            const auto& packing = *entry.packing->packing();
            if (packing.mipLayouts().size() > budget.maxMipMappings - mips
                || packing.packedBytes().size() > budget.maxPackedBytes - bytes)
                return "RHI-ORDINARY-TEXTURE-UPLOAD-CAPACITY";
            mips += packing.mipLayouts().size();
            bytes += packing.packedBytes().size();
            const auto* destination =
                entry.packing->originalSource()->originalDestinationLease()->originalObject();
            if (const auto* id = inspectDestination(destination->desc()))
                return id;
            if (const auto* id = semantics::OriginalTextureContentUploadValidation::inspect(
                    *entry.packing->originalInput(), destination->desc()))
                return id;
            for (std::size_t j = 0u; j < i; ++j)
                if (bindings[j]
                        .packing->originalSource()
                        ->originalDestinationLease()
                        ->originalObject()
                    == destination)
                    return "RHI-ORDINARY-TEXTURE-UPLOAD-DUPLICATE-DESTINATION";
        }
        std::size_t uploads = 0u;
        for (const auto& operation : program->operations())
            uploads += operation.family == OriginalCommandStreamProgram::Family::TextureUpload;
        return uploads == count ? nullptr : "RHI-ORDINARY-TEXTURE-UPLOAD-COVERAGE";
    }
};

} // namespace arp::rhi
