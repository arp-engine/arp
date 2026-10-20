#pragma once
#include "rhi/frontend/transfer_upload_packing.h"
#include "rhi/semantics/transfer_lowering.h"

namespace arp::rhi::semantics {
// OriginalTextureUploadGeneration/1. Failure boundary only. Authored content
// generation is not a resource incarnation, state revision or completion epoch.
class TextureUploadGenerationValidation final {
public:
    struct Intent {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
    };

    struct Lowering {
        bool validatorOnly = false;
        bool executable = false;
        const char* diagnostic = "RHI-TEXTURE-UPLOAD-GENERATION-BACKEND-UNKNOWN";
    };

    static constexpr Lowering lowering(Backend backend) noexcept
    {
        return semantics::TransferLowering::TextureUploadGenerationValidation_lowering<Lowering>(
            backend);
    }

    static const char* admitOriginal(const std::shared_ptr<const TextureUploadPacking>& packing,
        Backend backend,
        Intent intent) noexcept
    {
        if (intent.contractVersion != 1u)
            return "RHI-TEXTURE-UPLOAD-GENERATION-VERSION";
        if (!intent.prototypeOptIn)
            return "RHI-TEXTURE-UPLOAD-GENERATION-OPT-IN";
        if (!packing || !packing->hasQuotaBoundSharedOwner(packing))
            return "RHI-TEXTURE-UPLOAD-GENERATION-ORIGINAL-PACKING";
        // No original content-version/native-resource authority exists yet.
        // Never echo scalarFields().resourceGeneration as successful evidence.
        return lowering(backend).diagnostic;
    }
};
} // namespace arp::rhi::semantics
