#pragma once

#include "rhi/backends/metal/metal_native_resources.h"
#include "rhi/gpu_descriptors.h"

#include <array>

namespace arp::rhi_metal {

// Backend-owned observation of one native resource's complete identity. The
// logical payload can stay alive while an internal native object is replaced;
// that replacement must not bypass a prepared immutable owner projection.
class MetalRhiNativeResourceObservation final {
public:
    MetalRhiNativeResourceObservation() = default;

    explicit MetalRhiNativeResourceObservation(const MetalRhiNativeResourcePayload& resource)
        : MetalRhiNativeResourceObservation(
              resource.nativeDevice, resource.nativeObject, resource.allocationReleaseToken)
    {
        observesHazards_ = true;
        hazardsTracked_ = resource.hazardsExplicitlyTracked;
    }

    MetalRhiNativeResourceObservation(std::shared_ptr<void> device,
        std::shared_ptr<void> object,
        std::shared_ptr<void> allocation = {})
        : allocation_(std::move(allocation)), device_(std::move(device)), object_(std::move(object))
    {}

    bool matches(const MetalRhiNativeResourcePayload& resource) const noexcept
    {
        return matches(
                   resource.nativeDevice, resource.nativeObject, resource.allocationReleaseToken)
            && (!observesHazards_ || hazardsTracked_ == resource.hazardsExplicitlyTracked);
    }

    bool matches(const std::shared_ptr<void>& device,
        const std::shared_ptr<void>& object,
        const std::shared_ptr<void>& allocation = {}) const noexcept
    {
        return device_ && object_ && same(device_, device) && same(object_, object)
            && same(allocation_, allocation);
    }

private:
    static bool same(const std::shared_ptr<void>& a, const std::shared_ptr<void>& b) noexcept
    {
        return a == b && !a.owner_before(b) && !b.owner_before(a);
    }

    // Release the native object before its final allocation-accounting token.
    std::shared_ptr<void> allocation_, device_, object_;
    bool observesHazards_ = false, hazardsTracked_ = false;
};

// Kind and exact payload shape are metadata, never a preferred fallback order.
inline std::shared_ptr<const void> metalRhiCanonicalResourceOwner(rhi::DescriptorResourceKind kind,
    const std::shared_ptr<MetalRhiNativeBufferPayload>& buffer,
    const std::shared_ptr<MetalRhiNativeTexturePayload>& texture,
    const std::shared_ptr<MetalRhiNativeTextureViewPayload>& view,
    const std::shared_ptr<MetalRhiNativeSamplerPayload>& sampler) noexcept
{
    switch (kind) {
    case rhi::DescriptorResourceKind::Buffer:
        return buffer && !texture && !view && !sampler ? std::shared_ptr<const void>(buffer)
                                                       : nullptr;
    case rhi::DescriptorResourceKind::Sampler:
        return sampler && !buffer && !texture && !view ? std::shared_ptr<const void>(sampler)
                                                       : nullptr;
    case rhi::DescriptorResourceKind::TextureView:
        if (buffer || sampler || static_cast<bool>(texture) == static_cast<bool>(view))
            return {};
        return texture ? std::shared_ptr<const void>(texture) : std::shared_ptr<const void>(view);
    default:
        return {};
    }
}

inline const MetalRhiNativeResourcePayload* metalRhiCanonicalNativeResource(
    rhi::DescriptorResourceKind kind,
    const std::shared_ptr<MetalRhiNativeBufferPayload>& buffer,
    const std::shared_ptr<MetalRhiNativeTexturePayload>& texture,
    const std::shared_ptr<MetalRhiNativeTextureViewPayload>& view,
    const std::shared_ptr<MetalRhiNativeSamplerPayload>& sampler) noexcept
{
    if (!metalRhiCanonicalResourceOwner(kind, buffer, texture, view, sampler))
        return nullptr;
    if (buffer)
        return &buffer->resource;
    if (texture)
        return &texture->resource;
    if (view)
        return &view->resource;
    return &sampler->resource;
}

class MetalRhiResourceOwnerProjection final {
public:
    // Independent tracked buffers have these four mandatory owners: allocation
    // token, native device, native object and payload. The heap role is absent.
    static constexpr std::size_t independentResourceOwnerCount = 4u;
    static constexpr std::size_t independentBufferOwnerCount = independentResourceOwnerCount;
    using Owners = std::array<std::shared_ptr<const void>, 5>;

    static auto owners(rhi::DescriptorResourceKind kind,
        const std::shared_ptr<MetalRhiNativeBufferPayload>& buffer,
        const std::shared_ptr<MetalRhiNativeTexturePayload>& texture,
        const std::shared_ptr<MetalRhiNativeTextureViewPayload>& view,
        const std::shared_ptr<MetalRhiNativeSamplerPayload>& sampler) noexcept
    {
        const auto payload = metalRhiCanonicalResourceOwner(kind, buffer, texture, view, sampler);
        const auto* native = metalRhiCanonicalNativeResource(kind, buffer, texture, view, sampler);
        Owners result{};
        if (native)
            result = {native->allocationReleaseToken,
                native->nativeDevice,
                texture ? texture->aliasHeap : nullptr,
                native->nativeObject,
                payload};
        return result;
    }
};

} // namespace arp::rhi_metal
