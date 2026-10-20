#include "core/cpu_allocator.h"
#include "rhi/gpu_descriptors.h"
#include "rhi/gpu_device.h"

#include <stdexcept>

namespace arp::rhi {

DescriptorCache::DescriptorCache(GpuDevice& device, std::pmr::memory_resource* memoryResource)
    : device_(device),
      memoryResource_(memoryResource ? memoryResource : &core::defaultCpuMemoryResource()),
      arenaCache_(memoryResource_), setCache_(memoryResource_)
{}

DescriptorCache::~DescriptorCache() = default;

bool DescriptorCache::configureBudget(const semantics::CacheBudget& budget) noexcept
{
    if (!budget.valid() || entryCount() != 0u) {
        lastBudgetDiagnostic_ = !budget.valid() ? "RHI-DESCRIPTOR-CACHE-BUDGET-INVALID"
                                                : "RHI-DESCRIPTOR-CACHE-BUDGET-CONFIGURE-AFTER-USE";
        return false;
    }
    budget_ = budget;
    lastBudgetDiagnostic_ = nullptr;
    return true;
}

semantics::CacheBudgetStats DescriptorCache::budgetStats() const noexcept
{
    return {budget_.valid(),
        budget_.valid() ? budget_.capacity : 0u,
        chargedDescriptors_,
        peakChargedDescriptors_,
        budgetRejectionCount_};
}

std::uint64_t DescriptorCache::hashArenaDesc(const DescriptorArenaDesc& desc) noexcept
{
    // Hash combines maxSets and maxDescriptors. The label is excluded so
    // that different labels with the same config produce the same hash.
    std::uint64_t h = 0;
    h ^= std::hash<std::uint32_t>{}(desc.maxSets) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<std::uint32_t>{}(desc.maxDescriptors) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

std::uint64_t DescriptorCache::hashLayoutDesc(const DescriptorLayoutDesc& desc) noexcept
{
    // Hash the layout name and all bindings.
    std::uint64_t h = 0;
    const auto& name = desc.name;
    for (char c : name) {
        h ^= std::hash<char>{}(c) + 0x9e3779b9 + (h << 6) + (h >> 2);
    }
    for (const auto& b : desc.bindings) {
        h ^= std::hash<std::uint32_t>{}(b.binding) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<std::uint32_t>{}(static_cast<std::uint32_t>(b.type)) + 0x9e3779b9 + (h << 6)
            + (h >> 2);
        h ^= std::hash<std::uint32_t>{}(static_cast<std::uint32_t>(b.stages)) + 0x9e3779b9
            + (h << 6) + (h >> 2);
        h ^= std::hash<std::uint32_t>{}(b.arrayCount) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<bool>{}(b.tableBacked) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<bool>{}(b.dynamicOffset) + 0x9e3779b9 + (h << 6) + (h >> 2);
    }
    return h;
}

DescriptorArena* DescriptorCache::acquireDescriptorArena(const DescriptorArenaDesc& desc)
{
    const auto key = hashArenaDesc(desc);
    // Cache hit: return the existing arena WITHOUT resetting it. We must NOT
    // reset the underlying native descriptor pool here because a previous
    // frame's command buffer may still be in flight on the GPU — resetting
    // the pool would invalidate every descriptor set allocated from it while
    // those sets may still be referenced by an in-flight command buffer
    // (VUID-vkCmdBindPipeline ... descriptor set destroyed/updated without
    // UPDATE_AFTER_BIND). Returning the arena as-is keeps every prior set
    // valid (no invalidation occurs), so acquireDescriptorSet() never returns
    // a stale/destroyed set — this is the root-cause fix for the P0-4
    // "reset-on-hit returns invalidated sets" bug. Pool capacity is reclaimed
    // by evictStale() once an arena ages out, which destroys the arena AND
    // erases all of its sets from setCache_.
    auto it = arenaCache_.find(key);
    if (it != arenaCache_.end()) {
        it->second.lastUsedFrameSerial = currentFrameSerial_;
        ++arenaHitCount_;
        return it->second.arena.get();
    }

    if (budget_.valid() && (desc.maxDescriptors > budget_.capacity - chargedDescriptors_)) {
        ++budgetRejectionCount_;
        lastBudgetDiagnostic_ = "RHI-DESCRIPTOR-CACHE-BUDGET-EXHAUSTED";
        return nullptr;
    }

    auto arena = device_.createDescriptorArena(desc);
    if (!arena) {
        return nullptr;
    }

    auto* raw = arena.get();
    arenaCache_.emplace(
        key, ArenaEntry{std::move(arena), currentFrameSerial_, desc.maxDescriptors});
    chargedDescriptors_ += desc.maxDescriptors;
    peakChargedDescriptors_ = std::max(peakChargedDescriptors_, chargedDescriptors_);
    ++arenaMissCount_;
    return raw;
}

DescriptorSet* DescriptorCache::acquireDescriptorSet(
    const DescriptorSetDesc& desc, DescriptorArena* arena)
{
    if (!arena) {
        return nullptr;
    }

    const auto key = hashLayoutDesc(desc.layout);
    auto it = setCache_.find(key);
    if (it != setCache_.end()) {
        it->second.lastUsedFrameSerial = currentFrameSerial_;
        ++setHitCount_;
        return it->second.set.get();
    }

    auto set = arena->allocateDescriptorSet(desc);
    if (!set) {
        return nullptr;
    }

    auto* raw = set.get();
    setCache_.emplace(key, SetEntry{arena, std::move(set), currentFrameSerial_});
    ++setMissCount_;
    return raw;
}

void DescriptorCache::onFrameComplete()
{
    ++currentFrameSerial_;
}

void DescriptorCache::evictStale(std::uint64_t currentFrameSerial, std::uint32_t maxUnusedFrames)
{
    // Evict stale arenas. When an arena is evicted, all descriptor sets
    // owned by that arena must be evicted too (the set's parent arena is
    // being destroyed).
    for (auto it = arenaCache_.begin(); it != arenaCache_.end();) {
        const auto framesUnused = currentFrameSerial - it->second.lastUsedFrameSerial;
        if (framesUnused >= maxUnusedFrames) {
            auto* evictedArena = it->second.arena.get();
            // Evict all sets owned by this arena.
            for (auto sit = setCache_.begin(); sit != setCache_.end();) {
                if (sit->second.owningArena == evictedArena) {
                    sit = setCache_.erase(sit);
                } else {
                    ++sit;
                }
            }
            chargedDescriptors_ -= it->second.chargedDescriptors;
            it = arenaCache_.erase(it);
        } else {
            ++it;
        }
    }

    // Evict stale descriptor sets (sets whose owning arena still exists
    // but the set itself is stale).
    for (auto it = setCache_.begin(); it != setCache_.end();) {
        const auto framesUnused = currentFrameSerial - it->second.lastUsedFrameSerial;
        if (framesUnused >= maxUnusedFrames) {
            it = setCache_.erase(it);
        } else {
            ++it;
        }
    }
}

std::uint32_t DescriptorCache::entryCount() const noexcept
{
    return static_cast<std::uint32_t>(arenaCache_.size() + setCache_.size());
}

void DescriptorCache::clearForShutdown()
{
    setCache_.clear();
    arenaCache_.clear();
    chargedDescriptors_ = 0;
}

void DescriptorCache::resetTelemetry()
{
    arenaHitCount_ = 0;
    arenaMissCount_ = 0;
    setHitCount_ = 0;
    setMissCount_ = 0;
}

} // namespace arp::rhi
