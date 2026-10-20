#pragma once
#include "core/shared_owner_projection.h"
#include "rhi/frontend/transfer_readback_input.h"
#include "rhi/semantics/transfer_lowering.h"

namespace arp::rhi::semantics {
// Frozen original metadata only. The layout already owns checked physical
// geometry, padded footprints and offsets; never recompute or round them here.
class TextureReadbackCopyLowering final {
public:
    enum class Outcome { DiagnosticFailure, Prototype };
    enum class Operation { DiagnosticFailure, TextureToBuffer };
    enum class Dimension { Unknown, Texture2D, Texture3D };

    struct Decision {
        std::uint32_t contractVersion = 0u;
        Backend backend = Backend::Null;
        Outcome outcome = Outcome::DiagnosticFailure;
        Operation operation = Operation::DiagnosticFailure;
        Dimension dimension = Dimension::Unknown;
        std::shared_ptr<const TextureReadbackLayout> layout;
        std::size_t mipIndex = 0u;
        TextureReadbackLayout::Mip original;

        bool matches(const Decision& other) const noexcept
        {
            return contractVersion == other.contractVersion && backend == other.backend
                && outcome == other.outcome && operation == other.operation
                && dimension == other.dimension && layout.get() == other.layout.get()
                && core::SharedOwnerProjection::sameOwner(layout, other.layout)
                && mipIndex == other.mipIndex && original.matches(other.original);
        }
    };

    static const char* lowerOriginal(Backend backend,
        std::uint32_t version,
        bool prototypeOptIn,
        const std::shared_ptr<const TextureReadbackLayout>& layout,
        std::size_t index,
        const TextureReadbackLayout::Mip& original,
        Decision& output) noexcept
    {
        if (version != 1u)
            return "RHI-READBACK-COPY-LOWERING-VERSION";
        if (!prototypeOptIn)
            return "RHI-READBACK-COPY-LOWERING-OPT-IN";
        if (const auto* diagnostic = TransferLowering::readbackCopyGate(backend))
            return diagnostic;
        if (const auto* id = TextureReadbackLayout::validateOriginalMip(layout, index, original))
            return id;
        Decision value;
        value.contractVersion = version;
        value.backend = backend;
        value.outcome = Outcome::Prototype;
        value.operation = Operation::TextureToBuffer;
        value.dimension =
            layout->sourceLayout().extent.depth > 1u ? Dimension::Texture3D : Dimension::Texture2D;
        value.layout = layout;
        value.mipIndex = index;
        value.original = original;
        output = std::move(value);
        return nullptr;
    }

    static const char* validateOriginal(Backend backend,
        std::uint32_t version,
        bool prototypeOptIn,
        const std::shared_ptr<const TextureReadbackLayout>& layout,
        std::size_t index,
        const TextureReadbackLayout::Mip& original,
        const Decision& decision) noexcept
    {
        Decision expected;
        if (const auto* id =
                lowerOriginal(backend, version, prototypeOptIn, layout, index, original, expected))
            return id;
        return expected.matches(decision) ? nullptr
                                          : "RHI-READBACK-COPY-LOWERING-DECISION-MISMATCH";
    }
};
} // namespace arp::rhi::semantics
