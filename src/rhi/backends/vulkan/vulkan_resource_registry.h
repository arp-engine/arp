#pragma once

#include "core/memory/cpu_allocator.h"
#include "core/ownership/shared_owner_projection.h"
#include "rhi/backends/vulkan/vulkan_native_resources.h"

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <utility>

namespace arp::rhi_vulkan {

class VulkanRhiResourceRegistry {
public:
    /// Construct with an explicit CPU memory resource. When omitted, the
    /// process-wide default resource is used. Threaded through to the pmr
    /// entries map so a backend can bind a per-instance intent/budget
    /// instead of silently falling back to the global default.
    explicit VulkanRhiResourceRegistry(
        std::pmr::memory_resource* memoryResource = &core::defaultCpuMemoryResource())
        : entries_(memoryResource)
    {}

    void registerResource(rhi::GpuResourceId id,
        std::shared_ptr<VulkanRhiNativeResourcePayload> payload,
        rhi::GpuResourceId parentTexture = {},
        bool textureObject = false)
    {
        if (!id.isValid() || !payload) {
            return;
        }
        std::scoped_lock lock(mutex_);
        pruneExpired();
        auto& entry = entries_[id.value];
        entry.payload = std::move(payload);
        entry.parentTexture = parentTexture;
        entry.textureObject = textureObject;
    }

    void registerTexture(
        rhi::GpuResourceId id, std::shared_ptr<VulkanRhiNativeResourcePayload> payload)
    {
        registerResource(id, std::move(payload), {}, true);
    }

    void registerBufferObject(rhi::GpuResourceId id, const rhi::GpuBuffer& buffer)
    {
        std::scoped_lock lock(mutex_);
        if (auto entry = entries_.find(id.value); entry != entries_.end())
            entry->second.gpuWriteSeal = buffer.gpuWriteSealToken();
    }

    void registerTextureObject(rhi::GpuResourceId id, const rhi::GpuTexture& texture)
    {
        std::scoped_lock lock(mutex_);
        if (auto entry = entries_.find(id.value); entry != entries_.end()
            && entry->second.textureObject && !entry->second.textureObjectIdentity)
            entry->second.textureObjectIdentity = &texture;
    }

    bool textureObjectMatches(const rhi::GpuTexture& texture) const noexcept
    {
        std::scoped_lock lock(mutex_);
        const auto entry = entries_.find(texture.id().value);
        return entry != entries_.end() && entry->second.textureObject
            && entry->second.textureObjectIdentity == &texture;
    }

    bool bufferGpuWritesSealed(rhi::GpuResourceId id) const
    {
        std::scoped_lock lock(mutex_);
        if (auto entry = entries_.find(id.value); entry != entries_.end()) {
            const auto seal = entry->second.gpuWriteSeal.lock();
            return seal && seal->writableDescriptorSealed();
        }
        return false;
    }

    bool bufferObjectMatches(const rhi::GpuBuffer& buffer) const noexcept
    {
        std::scoped_lock lock(mutex_);
        const auto entry = entries_.find(buffer.id().value);
        if (entry == entries_.end())
            return false;
        const auto registered = entry->second.gpuWriteSeal.lock();
        const auto actual = buffer.gpuWriteSealToken().lock();
        return registered && registered == actual
            && core::SharedOwnerProjection::sameOwner(registered, actual);
    }

    bool bufferGpuTransferWritesSealed(rhi::GpuResourceId id) const
    {
        std::scoped_lock lock(mutex_);
        if (auto entry = entries_.find(id.value); entry != entries_.end()) {
            const auto seal = entry->second.gpuWriteSeal.lock();
            return seal && seal->transferWritesSealed();
        }
        return false;
    }

    bool textureViewParentIsLive(
        rhi::GpuResourceId id, rhi::GpuResourceId expectedParent = {}) const
    {
        std::scoped_lock lock(mutex_);
        const auto entry = entries_.find(id.value);
        if (entry == entries_.end())
            return false;
        if (expectedParent.isValid() && entry->second.parentTexture != expectedParent)
            return false;
        const auto view = entry->second.payload.lock();
        if (!view || !view->backendVulkanTextureViewDescAvailable())
            return false;
        const auto parent = entries_.find(entry->second.parentTexture.value);
        if (parent == entries_.end())
            return false;
        // Object registration, not native handle presence, is public ID life.
        const auto texture = parent->second.payload.lock();
        return texture && parent->second.textureObject;
    }

    std::shared_ptr<VulkanRhiNativeResourcePayload> payload(rhi::GpuResourceId id) const
    {
        std::scoped_lock lock(mutex_);
        const auto found = entries_.find(id.value);
        if (found == entries_.end())
            return {};
        const auto& entry = found->second;
        auto resolved = entry.payload.lock();
        if (!resolved)
            entries_.erase(found);
        return resolved;
    }

    void unregisterResource(rhi::GpuResourceId id)
    {
        std::scoped_lock lock(mutex_);
        entries_.erase(id.value);
    }

private:
    void pruneExpired() const
    {
        for (auto entry = entries_.begin(); entry != entries_.end();) {
            if (entry->second.payload.expired())
                entry = entries_.erase(entry);
            else
                ++entry;
        }
    }

    struct Entry {
        std::weak_ptr<VulkanRhiNativeResourcePayload> payload;
        rhi::GpuResourceId parentTexture;
        bool textureObject = false;
        const rhi::GpuTexture* textureObjectIdentity = nullptr;
        std::weak_ptr<const rhi::GpuBufferWriteSealState> gpuWriteSeal;
    };

    // Preserve weak ownership and atomic lookup/unregistration. Per-draw
    // descriptor validation must resolve only the requested ID, not scan and
    // prune every live resource. Global expiry cleanup is a cold registration
    // operation; ordinary wrapper destruction unregisters its node directly.
    mutable std::mutex mutex_;
    // Each cold registration owns its node; removal returns its exact CPU
    // charge without retaining capacity under a retired resource quota.
    mutable core::pmr::Map<std::uint64_t, Entry> entries_;
};

} // namespace arp::rhi_vulkan
