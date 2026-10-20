#pragma once
#include "rhi/gpu_common.h"
#include "rhi/semantics/resource_lowering.h"

namespace arp::rhi::semantics {
// Original upload or attachment content domain. Immutable creation intent; never a
// privileged write grant, readiness flag or completed content revision.
class TextureContentAccessPolicy final {
public:
    enum class Mode { Ordinary, OriginalUploadExclusive, OriginalAttachmentExclusive };

    explicit constexpr TextureContentAccessPolicy(Mode mode) noexcept : mode_(mode) {}

    bool originalUploadExclusive() const noexcept { return mode_ == Mode::OriginalUploadExclusive; }

    bool originalAttachmentExclusive() const noexcept
    {
        return mode_ == Mode::OriginalAttachmentExclusive;
    }

    const char* ordinaryAccessDiagnostic() const noexcept
    {
        return mode_ == Mode::Ordinary ? nullptr
            : originalUploadExclusive() || originalAttachmentExclusive()
            ? "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE"
            : "RHI-TEXTURE-CONTENT-ACCESS-POLICY-UNKNOWN";
    }

    struct Lowering {
        bool validatorOnly = false;
        bool nativeCreationImplemented = false;
        const char* diagnostic = "RHI-TEXTURE-DESTINATION-BACKEND-UNKNOWN";
    };

    static constexpr Lowering lowering(Backend backend) noexcept
    {
        return semantics::ResourceLowering::TextureContentAccessPolicy_lowering<Lowering>(backend);
    }

private:
    const Mode mode_;
};
} // namespace arp::rhi::semantics
