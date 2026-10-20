#pragma once
#include "rhi/gpu_extensions.h"
#include "rhi/semantics/transfer_lowering.h"

namespace arp::rhi {
// BorrowedTextureReadbackRetirement/1. A numerical ABI alone cannot prove
// source ownership, request identity, device namespace or callback budgets.
class BorrowedTextureReadbackRetirementContract final {
public:
    enum class Disposition { Unavailable, RetiredDiagnosticFailure, ExplicitBoundedLegacy };

    struct Decision {
        std::uint32_t contractVersion = 1u;
        Disposition disposition = Disposition::Unavailable;
        bool executionAvailable = false;
        const char* diagnostic = "RHI-READBACK-BORROWED-BACKEND-UNKNOWN";
    };

    static constexpr Decision lowering(
        Backend backend, bool originalBoundedBudgetConfigured = false) noexcept
    {
        return semantics::TransferLowering::BorrowedTextureReadbackRetirement_lowering<Decision,
            Disposition>(backend, originalBoundedBudgetConfigured);
    }
};

// Keep the borrowed ABI discoverable so old callers receive an explicit
// diagnostic. No encoder/resource inspection, output mutation or GPU work.
template <Backend BackendKind>
class RetiredTextureReadbackExtension : public TextureReadbackExtension {
    static_assert(BackendKind == Backend::Vulkan || BackendKind == Backend::WebGPU);

public:
    bool schedule(
        CommandEncoder&, const GpuTexture&, std::string_view, std::string& diagnostic) override
    {
        diagnostic = BorrowedTextureReadbackRetirementContract::lowering(BackendKind).diagnostic;
        return false;
    }

    bool poll(TextureReadbackSample&) override { return false; }
};
} // namespace arp::rhi
