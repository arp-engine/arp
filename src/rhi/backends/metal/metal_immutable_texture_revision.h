#pragma once
#include "rhi/frontend/bindless_image_and_indirect_validation.h"

namespace arp::rhi_metal {
class MetalRhiClosedConsumerImages;

// Native completion-issued private marker. It owns no texture/device cycle.
class MetalRhiImmutableTextureRevision final : public core::SharedObjectIdentity {
    class ConstructionKey {
        friend class MetalRhiClosedConsumerImages;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Revision = rhi::CanonicalConsumerImageContentContract::Revision;

    MetalRhiImmutableTextureRevision(
        ConstructionKey, Revision revision, rhi::TextureSubresourceRange range)
        : revision_(revision), range_(range)
    {}

    MetalRhiImmutableTextureRevision(const MetalRhiImmutableTextureRevision&) = delete;

    const auto& revision() const noexcept { return revision_; }

    const auto& range() const noexcept { return range_; }

private:
    friend class MetalRhiClosedConsumerImages;
    Revision revision_;
    rhi::TextureSubresourceRange range_;
};
} // namespace arp::rhi_metal
