#pragma once
#include "rhi/semantics/bindless_attachment_clear_lowering.h"

namespace arp::rhi {
// Optional versioned intent for Clear/Store on a previously completed original
// attachment. Zero preserves fresh-only behavior. No state reset or execution
// authority is implied; the original plan/journal must authenticate each write.
struct OriginalBindlessAttachmentClearIntent {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
};

class OriginalBindlessAttachmentClearContract final {
public:
    using Intent = OriginalBindlessAttachmentClearIntent;

    static constexpr const char* validateIntent(Intent intent) noexcept
    {
        if (intent.contractVersion > 1u || (!intent.contractVersion && intent.prototypeOptIn))
            return "RHI-BINDLESS-ATTACHMENT-CLEAR-VERSION";
        return intent.contractVersion && !intent.prototypeOptIn
            ? "RHI-BINDLESS-ATTACHMENT-CLEAR-OPT-IN"
            : nullptr;
    }

    static constexpr bool same(Intent a, Intent b) noexcept
    {
        return a.contractVersion == b.contractVersion && a.prototypeOptIn == b.prototypeOptIn;
    }

    static constexpr auto lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        return semantics::OriginalBindlessAttachmentClearLowering::lowering(backend, version);
    }

    static constexpr const char* validateObservation(Intent intent,
        std::uint32_t controlVersion,
        bool uninitialized,
        bool completed,
        std::uint64_t generation) noexcept
    {
        if (const auto* id = validateIntent(intent))
            return id;
        if (controlVersion != 4u && controlVersion != 5u)
            return "RHI-BINDLESS-ATTACHMENT-CLEAR-CONTROL";
        if (controlVersion == 5u && intent.contractVersion)
            return "RHI-BINDLESS-ATTACHMENT-CLEAR-CONTROL";
        return (uninitialized && !completed && !generation)
                || (intent.contractVersion == 1u && !uninitialized && completed && generation)
            ? nullptr
            : "RHI-BINDLESS-ATTACHMENT-CLEAR-INITIAL";
    }
};
} // namespace arp::rhi
