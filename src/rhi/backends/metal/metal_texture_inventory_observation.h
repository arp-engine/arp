#pragma once
#include "core/shared_owner_projection.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/frontend/bindless_image_and_indirect_validation.h"

namespace arp::rhi_metal {
// Borrowed logical wrappers, owned native storage. Destruction and observation
// must be externally serialized; native retention cannot revive a wrapper.
class MetalRhiTextureInventoryObservation final {
public:
    using Values = rhi::BindlessTextureInventoryResource;
    using Owners = std::array<std::shared_ptr<const void>, 10u>;

    static const char* capture(const std::shared_ptr<MetalRhiResourceRegistry>& registry,
        const std::shared_ptr<void>& device,
        rhi::GpuResourceId viewId,
        MetalRhiTextureInventoryObservation& output) noexcept
    {
        output = {};
        MetalRhiTextureInventoryObservation pending;
        if (!registry || !device)
            return "RHI-METAL-TEXTURE-INVENTORY-ISSUER";
        const auto view = registry->find(viewId);
        if (!view || !view->textureView || !view->textureViewWrapper.get())
            return "RHI-METAL-TEXTURE-INVENTORY-VIEW-NOT-LIVE";
        const auto parent = registry->find(view->parentTexture);
        if (!parent || !parent->texture || !parent->textureWrapper.get()
            || !view->parentTextureLive)
            return "RHI-METAL-TEXTURE-INVENTORY-PARENT-NOT-LIVE";
        const auto* wrapper = view->textureViewWrapper.get();
        const auto* texture = parent->textureWrapper.get();
        if (wrapper->id() != viewId || texture->id() != view->parentTexture
            || !core::SharedOwnerProjection::sameOwner(
                metalRhiNativePayloadFor(*wrapper), view->textureView)
            || !core::SharedOwnerProjection::sameOwner(
                metalRhiNativePayloadFor(*texture), parent->texture))
            return "RHI-METAL-TEXTURE-INVENTORY-REGISTRY";
        if (const auto* id = Values::capture(*texture, *wrapper, pending.values_))
            return id;
        const auto& values = pending.values_;
        if (view->textureUsage != values.usage || parent->textureUsage != values.parentUsage
            || view->textureFormat != values.format || parent->textureFormat != values.parentFormat
            || view->textureViewDimension != values.dimension
            || view->textureViewAspect != values.range.aspect
            || view->textureMipLevelCount != values.range.mipLevelCount
            || view->textureArrayLayerCount != values.range.arrayLayerCount
            || view->textureSampleCount != values.sampleCount
            || parent->textureMipLevelCount != values.parentMipLevelCount
            || parent->textureArrayLayerCount != values.parentArrayLayerCount
            || parent->textureSampleCount != values.sampleCount
            || parent->textureExtent.width != values.parentExtent.width
            || parent->textureExtent.height != values.parentExtent.height
            || parent->textureExtent.depth != values.parentExtent.depth
            || view->textureExtent.width != view->textureView->extent.width
            || view->textureExtent.height != view->textureView->extent.height
            || view->textureExtent.depth != view->textureView->extent.depth)
            return "RHI-METAL-TEXTURE-INVENTORY-REGISTRY-METADATA";
        const auto& p = parent->texture->resource;
        const auto& v = view->textureView->resource;
        if (!core::SharedOwnerProjection::sameOwner(device, p.nativeDevice)
            || !core::SharedOwnerProjection::sameOwner(device, v.nativeDevice)
            || !p.allocationReleaseToken
            || !core::SharedOwnerProjection::sameOwner(
                p.allocationReleaseToken, v.allocationReleaseToken))
            return "RHI-METAL-TEXTURE-INVENTORY-NATIVE-OWNER";
        if (const auto* id = validateMetalRhiNativeSampledTextureInventory(
                *parent->texture, *view->textureView, values))
            return id;
        pending.registry_ = registry;
        pending.parent_ = *parent;
        pending.view_ = *view;
        pending.parentObservation_ = MetalRhiNativeResourceObservation(p);
        pending.viewObservation_ = MetalRhiNativeResourceObservation(v);
        pending.heap_ = parent->texture->aliasHeap;
        pending.residentBytes_ = texture->allocation().estimatedSizeBytes;
        if (!pending.residentBytes_)
            return "RHI-METAL-TEXTURE-INVENTORY-RESIDENT-BYTES";
        pending.memoryUsage_ = texture->desc().memory;
        output = std::move(pending);
        return nullptr;
    }

    const char* validate() const noexcept
    {
        // These are the only gateways to borrowed wrappers, before registry or
        // virtual calls. Retained native payload liveness alone is insufficient.
        if (!view_.textureViewWrapper.get())
            return "RHI-METAL-TEXTURE-INVENTORY-VIEW-NOT-LIVE";
        if (!parent_.textureWrapper.get())
            return "RHI-METAL-TEXTURE-INVENTORY-PARENT-NOT-LIVE";
        if (!parent_.texture || !view_.textureView
            || !parentObservation_.matches(parent_.texture->resource)
            || !viewObservation_.matches(view_.textureView->resource)
            || !core::SharedOwnerProjection::sameOwner(heap_, parent_.texture->aliasHeap))
            return "RHI-METAL-TEXTURE-INVENTORY-ORIGINAL-MISMATCH";
        MetalRhiTextureInventoryObservation current;
        if (const auto* id =
                capture(registry_, parent_.texture->resource.nativeDevice, values_.view, current))
            return id;
        if (current.view_.textureViewWrapper.get() != view_.textureViewWrapper.get()
            || current.parent_.textureWrapper.get() != parent_.textureWrapper.get()
            || !values_.matches(current.values_) || residentBytes_ != current.residentBytes_
            || memoryUsage_ != current.memoryUsage_
            || !core::SharedOwnerProjection::sameOwner(view_.textureView, current.view_.textureView)
            || !core::SharedOwnerProjection::sameOwner(parent_.texture, current.parent_.texture)
            || !core::SharedOwnerProjection::sameOwner(heap_, current.heap_)
            || !parentObservation_.matches(parent_.texture->resource)
            || !viewObservation_.matches(view_.textureView->resource))
            return "RHI-METAL-TEXTURE-INVENTORY-ORIGINAL-MISMATCH";
        return nullptr;
    }

    const auto& values() const noexcept { return values_; }

    const auto& parent() const noexcept { return parent_; }

    const auto& view() const noexcept { return view_; }

    std::uint64_t borrowedResidentBytes() const noexcept { return residentBytes_; }

    Owners owners() const noexcept
    {
        const auto parent = MetalRhiResourceOwnerProjection::owners(
            rhi::DescriptorResourceKind::TextureView, {}, parent_.texture, {}, {});
        const auto view = MetalRhiResourceOwnerProjection::owners(
            rhi::DescriptorResourceKind::TextureView, {}, {}, view_.textureView, {});
        Owners result{};
        for (std::size_t i = 0u; i < parent.size(); ++i) {
            result[i] = parent[i];
            result[parent.size() + i] = view[i];
        }
        return result;
    }

private:
    std::shared_ptr<MetalRhiResourceRegistry> registry_;
    MetalRhiRegisteredResource parent_, view_;
    MetalRhiNativeResourceObservation parentObservation_, viewObservation_;
    std::shared_ptr<void> heap_;
    Values values_;
    std::uint64_t residentBytes_ = 0u;
    rhi::MemoryUsage memoryUsage_ = rhi::MemoryUsage::GpuOnly;
};
} // namespace arp::rhi_metal
