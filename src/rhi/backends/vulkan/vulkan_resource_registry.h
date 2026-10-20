#pragma once

#include "core/cpu_allocator.h"
#include "rhi/backends/vulkan/vulkan_native_resources.h"

#include <algorithm>
#include <list>
#include <memory>
#include <mutex>
#include <utility>

namespace arp::rhi_vulkan {

class VulkanRhiResourceRegistry {
public:
    /// Construct with an explicit CPU memory resource. When omitted, the
    /// process-wide default resource is used. Threaded through to the pmr
    /// entries list so a backend can bind a per-instance intent/budget
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
        for (auto& entry : entries_) {
            if (entry.id == id) {
                entry.payload = std::move(payload);
                entry.parentTexture = parentTexture;
                entry.textureObject = textureObject;
                return;
            }
        }
        entries_.push_back({id, std::move(payload), parentTexture, textureObject});
    }

    void registerTexture(
        rhi::GpuResourceId id, std::shared_ptr<VulkanRhiNativeResourcePayload> payload)
    {
        registerResource(id, std::move(payload), {}, true);
    }

    void registerBufferObject(rhi::GpuResourceId id, const rhi::GpuBuffer& buffer)
    {
        std::scoped_lock lock(mutex_);
        for (auto& entry : entries_)
            if (entry.id == id) {
                entry.gpuWriteSeal = buffer.gpuWriteSealToken();
                return;
            }
    }

    bool bufferGpuWritesSealed(rhi::GpuResourceId id) const
    {
        std::scoped_lock lock(mutex_);
        for (const auto& entry : entries_)
            if (entry.id == id) {
                const auto seal = entry.gpuWriteSeal.lock();
                return seal && seal->writableDescriptorSealed();
            }
        return false;
    }

    bool bufferGpuTransferWritesSealed(rhi::GpuResourceId id) const
    {
        std::scoped_lock lock(mutex_);
        for (const auto& entry : entries_)
            if (entry.id == id) {
                const auto seal = entry.gpuWriteSeal.lock();
                return seal && seal->transferWritesSealed();
            }
        return false;
    }

    bool textureViewParentIsLive(rhi::GpuResourceId id) const
    {
        std::scoped_lock lock(mutex_);
        rhi::GpuResourceId parent;
        for (const auto& entry : entries_) {
            if (entry.id != id)
                continue;
            const auto view = entry.payload.lock();
            if (!view || !view->backendVulkanTextureViewDescAvailable())
                return false;
            parent = entry.parentTexture;
            break;
        }
        if (!parent.isValid())
            return false;
        for (const auto& entry : entries_) {
            if (entry.id != parent)
                continue;
            const auto texture = entry.payload.lock();
            // Object registration, not native handle presence, is public ID
            // life. Synthetic fixtures retain explicit typed metadata too.
            return texture && entry.textureObject;
        }
        return false;
    }

    std::shared_ptr<VulkanRhiNativeResourcePayload> payload(rhi::GpuResourceId id) const
    {
        std::scoped_lock lock(mutex_);
        pruneExpired();
        for (const auto& entry : entries_) {
            if (entry.id == id) {
                return entry.payload.lock();
            }
        }
        return {};
    }

    void unregisterResource(rhi::GpuResourceId id)
    {
        std::scoped_lock lock(mutex_);
        entries_.remove_if([id](const Entry& entry) {
            return entry.id == id;
        });
    }

private:
    void pruneExpired() const
    {
        entries_.remove_if([](const Entry& entry) {
            return entry.payload.expired();
        });
    }

    struct Entry {
        rhi::GpuResourceId id;
        std::weak_ptr<VulkanRhiNativeResourcePayload> payload;
        rhi::GpuResourceId parentTexture;
        bool textureObject = false;
        std::weak_ptr<const rhi::GpuBufferWriteSealState> gpuWriteSeal;
    };

    // Resource creation/destruction can occur while a command encoder resolves
    // native payloads. Keep pruning and list mutation atomic with lookups;
    // otherwise erasure may race weak_ptr access in payload().
    mutable std::mutex mutex_;
    // Each cold registration owns its node; removal returns its exact CPU
    // charge without retaining capacity under a retired resource quota.
    mutable core::pmr::List<Entry> entries_;
};

} // namespace arp::rhi_vulkan
