#pragma once
#include "rhi/gpu_common.h"
#include "rhi/semantics/transfer_lowering.h"

namespace arp::rhi {
// Content selection is metadata, never authored-content or execution evidence.
// CompletedOriginalUpload is reserved for a verified original completion graph.
class TextureReadbackContentIntent final {
public:
    enum class Mode { Unspecified, QueueOrderedSnapshot, CompletedOriginalUpload };

    struct Descriptor {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        Mode mode = Mode::Unspecified;
    };
    enum class Outcome { DiagnosticFailure, ValidatorOnly, Prototype };

    struct Decision {
        std::uint32_t contractVersion = 0u;
        Backend backend = Backend::Null;
        Mode mode = Mode::Unspecified;
        Outcome outcome = Outcome::DiagnosticFailure;
        const char* diagnostic = "RHI-READBACK-CONTENT-UNSPECIFIED";

        bool matches(const Decision& other) const noexcept
        {
            return contractVersion == other.contractVersion && backend == other.backend
                && mode == other.mode && outcome == other.outcome;
        }

        bool nativeExecutionProven() const noexcept { return false; }
    };

    static const char* validate(const Descriptor& d) noexcept
    {
        if (d.contractVersion != 1u)
            return "RHI-READBACK-CONTENT-VERSION";
        if (!d.prototypeOptIn)
            return "RHI-READBACK-CONTENT-OPT-IN";
        switch (d.mode) {
        case Mode::QueueOrderedSnapshot:
            return nullptr;
        case Mode::Unspecified:
            return "RHI-READBACK-CONTENT-UNSPECIFIED";
        case Mode::CompletedOriginalUpload:
            return "RHI-READBACK-CONTENT-COMPLETED-EVIDENCE-REQUIRED";
        }
        return "RHI-READBACK-CONTENT-MODE-UNKNOWN";
    }

    static Decision lower(Backend backend, const Descriptor& d) noexcept
    {
        Decision result;
        result.backend = backend;
        if (const auto* id = validate(d)) {
            result.diagnostic = id;
            return result;
        }
        result.contractVersion = d.contractVersion;
        result.mode = d.mode;
        return semantics::TransferLowering::TextureReadbackContentIntent_lower<Decision, Outcome>(
            backend, result);
    }

    static const char* validateDecision(
        Backend backend, const Descriptor& d, const Decision& actual) noexcept
    {
        const auto expected = lower(backend, d);
        if (expected.diagnostic)
            return expected.diagnostic;
        return expected.matches(actual) && !actual.diagnostic
            ? nullptr
            : "RHI-READBACK-CONTENT-LOWERING-MISMATCH";
    }
};
} // namespace arp::rhi
