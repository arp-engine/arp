#include "rhi/frontend/frame_resources.h"

#include "core/diagnostics/diagnostic_builder.h"
#include "core/memory/cpu_allocator.h"

#include <cstddef>
#include <limits>

namespace arp::rhi {

namespace {

FrameResourceOperationResult makeSuccess()
{
    FrameResourceOperationResult result;
    result.success = true;
    return result;
}

FrameResourceOperationResult makeFailure(const char* diagnostics)
{
    FrameResourceOperationResult result;
    result.success = false;
    result.diagnostics = diagnostics;
    return result;
}

bool byteAdditionOverflows(std::uint64_t lhs, std::uint64_t rhs)
{
    return rhs > std::numeric_limits<std::uint64_t>::max() - lhs;
}

FrameResourceOperationResult validateReleasedBytes(
    std::uint64_t currentReleasedBytes, std::uint64_t additionalReleasedBytes)
{
    if (byteAdditionOverflows(currentReleasedBytes, additionalReleasedBytes)) {
        return makeFailure("released byte accounting overflow");
    }
    return makeSuccess();
}

} // namespace

FrameResourceRecycler::FrameResourceRecycler(
    const FrameResourceRecyclerDesc& desc, std::pmr::memory_resource* memoryResource)
    : desc_(desc),
      memoryResource_(memoryResource ? memoryResource : &core::defaultCpuMemoryResource()),
      deferred_(memoryResource_), transient_(memoryResource_),
      configurationDiagnostics_(memoryResource_)
{
    if (desc_.maxDeferredDestroys > kMaxDeferredFrameResourceDestroys) {
        desc_.maxDeferredDestroys = kMaxDeferredFrameResourceDestroys;
        arp::core::appendDiagnostic(
            configurationDiagnostics_, "frame resource recycler deferred capacity limit exceeded");
    }
    if (desc_.maxTransientResources > kMaxTransientFrameResources) {
        desc_.maxTransientResources = kMaxTransientFrameResources;
        arp::core::appendDiagnostic(
            configurationDiagnostics_, "frame resource recycler transient capacity limit exceeded");
    }
    deferred_.reserve(desc_.maxDeferredDestroys);
    transient_.reserve(desc_.maxTransientResources);
}

std::uint32_t FrameResourceRecycler::logicalFrameSlots() const
{
    return desc_.logicalFrameSlots;
}

FrameResourceOperationResult FrameResourceRecycler::deferDestroy(
    const FrameResourceRecord& record, std::uint64_t lastUsedFrameSerial)
{
    auto configuration = validateConfiguration();
    if (!configuration.success) {
        return configuration;
    }

    auto validation = validateRecord(record);
    if (!validation.success) {
        return validation;
    }

    if (deferred_.size() >= desc_.maxDeferredDestroys) {
        return makeFailure("deferred destroy capacity exhausted");
    }
    std::uint64_t pendingBytes = 0;
    for (const auto& entry : deferred_) {
        if (byteAdditionOverflows(pendingBytes, entry.record.sizeBytes)) {
            return makeFailure("deferred destroy byte accounting overflow");
        }
        pendingBytes += entry.record.sizeBytes;
    }
    if (byteAdditionOverflows(pendingBytes, record.sizeBytes)) {
        return makeFailure("deferred destroy byte accounting overflow");
    }
    if (byteAdditionOverflows(lastUsedFrameSerial, desc_.logicalFrameSlots)) {
        return makeFailure("deferred destroy frame serial overflow");
    }

    DeferredResource deferred;
    deferred.record = record;
    deferred.frameSlot = frameSlotForSerial(lastUsedFrameSerial);
    deferred.retireAfterFrameSerial = lastUsedFrameSerial + desc_.logicalFrameSlots;
    deferred_.push_back(deferred);

    auto result = makeSuccess();
    result.frameSlot = deferred.frameSlot;
    return result;
}

FrameResourceOperationResult FrameResourceRecycler::retireCompletedFrames(
    std::uint64_t completedFrameSerial)
{
    auto configuration = validateConfiguration();
    if (!configuration.success) {
        return configuration;
    }

    if (desc_.logicalFrameSlots != 2) {
        return makeFailure("frame resource recycler requires two logical frame slots");
    }

    std::uint32_t releaseCount = 0;
    std::uint64_t releaseBytes = 0;
    for (const auto& entry : deferred_) {
        if (entry.retireAfterFrameSerial <= completedFrameSerial) {
            if (byteAdditionOverflows(releaseBytes, entry.record.sizeBytes)) {
                return makeFailure("released byte accounting overflow");
            }
            ++releaseCount;
            releaseBytes += entry.record.sizeBytes;
        }
    }
    auto releaseValidation = validateReleasedBytes(releasedBytes_, releaseBytes);
    if (!releaseValidation.success) {
        return releaseValidation;
    }

    for (std::size_t index = 0; index < deferred_.size();) {
        const auto& entry = deferred_[index];
        if (entry.retireAfterFrameSerial <= completedFrameSerial) {
            deferred_.erase(deferred_.begin() + static_cast<std::ptrdiff_t>(index));
        } else {
            ++index;
        }
    }

    auto result = makeSuccess();
    result.releasedCount = releaseCount;
    result.releasedBytes = releaseBytes;
    releasedBytes_ += releaseBytes;
    return result;
}

FrameResourceOperationResult FrameResourceRecycler::trackTransient(
    const FrameResourceRecord& record, std::uint64_t frameSerial)
{
    auto configuration = validateConfiguration();
    if (!configuration.success) {
        return configuration;
    }

    auto validation = validateRecord(record);
    if (!validation.success) {
        return validation;
    }

    if (transient_.size() >= desc_.maxTransientResources) {
        return makeFailure("transient resource capacity exhausted");
    }
    std::uint64_t transientBytes = 0;
    for (const auto& entry : transient_) {
        if (byteAdditionOverflows(transientBytes, entry.record.sizeBytes)) {
            return makeFailure("transient resource byte accounting overflow");
        }
        transientBytes += entry.record.sizeBytes;
    }
    if (byteAdditionOverflows(transientBytes, record.sizeBytes)) {
        return makeFailure("transient resource byte accounting overflow");
    }

    TransientResource transient;
    transient.record = record;
    transient.frameSlot = frameSlotForSerial(frameSerial);
    transient_.push_back(transient);

    auto result = makeSuccess();
    result.frameSlot = transient.frameSlot;
    return result;
}

FrameResourceOperationResult FrameResourceRecycler::releaseTransientForFrameSlot(
    std::uint32_t frameSlot)
{
    auto configuration = validateConfiguration();
    if (!configuration.success) {
        return configuration;
    }

    auto validation = validateFrameSlot(frameSlot);
    if (!validation.success) {
        return validation;
    }

    std::uint32_t releaseCount = 0;
    std::uint64_t releaseBytes = 0;
    for (const auto& entry : transient_) {
        if (entry.frameSlot == frameSlot) {
            if (byteAdditionOverflows(releaseBytes, entry.record.sizeBytes)) {
                return makeFailure("released byte accounting overflow");
            }
            ++releaseCount;
            releaseBytes += entry.record.sizeBytes;
        }
    }
    auto releaseValidation = validateReleasedBytes(releasedBytes_, releaseBytes);
    if (!releaseValidation.success) {
        return releaseValidation;
    }

    for (std::size_t index = 0; index < transient_.size();) {
        const auto& entry = transient_[index];
        if (entry.frameSlot == frameSlot) {
            transient_.erase(transient_.begin() + static_cast<std::ptrdiff_t>(index));
        } else {
            ++index;
        }
    }

    auto result = makeSuccess();
    result.frameSlot = frameSlot;
    result.releasedCount = releaseCount;
    result.releasedBytes = releaseBytes;
    releasedBytes_ += releaseBytes;
    return result;
}

FrameResourceOperationResult FrameResourceRecycler::releaseAllTransientResources()
{
    auto configuration = validateConfiguration();
    if (!configuration.success) {
        return configuration;
    }

    if (desc_.logicalFrameSlots != 2) {
        return makeFailure("frame resource recycler requires two logical frame slots");
    }

    std::uint32_t releaseCount = 0;
    std::uint64_t releaseBytes = 0;
    for (const auto& entry : transient_) {
        if (byteAdditionOverflows(releaseBytes, entry.record.sizeBytes)) {
            return makeFailure("released byte accounting overflow");
        }
        ++releaseCount;
        releaseBytes += entry.record.sizeBytes;
    }
    auto releaseValidation = validateReleasedBytes(releasedBytes_, releaseBytes);
    if (!releaseValidation.success) {
        return releaseValidation;
    }

    transient_.clear();
    auto result = makeSuccess();
    result.releasedCount = releaseCount;
    result.releasedBytes = releaseBytes;
    releasedBytes_ += releaseBytes;
    return result;
}

FrameResourceOperationResult FrameResourceRecycler::releaseSurfaceScopedResources()
{
    auto configuration = validateConfiguration();
    if (!configuration.success) {
        return configuration;
    }

    if (desc_.logicalFrameSlots != 2) {
        return makeFailure("frame resource recycler requires two logical frame slots");
    }

    std::uint32_t releaseCount = 0;
    std::uint64_t releaseBytes = 0;
    for (const auto& entry : deferred_) {
        if (entry.record.scope == FrameResourceScope::Surface) {
            if (byteAdditionOverflows(releaseBytes, entry.record.sizeBytes)) {
                return makeFailure("released byte accounting overflow");
            }
            ++releaseCount;
            releaseBytes += entry.record.sizeBytes;
        }
    }
    for (const auto& entry : transient_) {
        if (entry.record.scope == FrameResourceScope::Surface) {
            if (byteAdditionOverflows(releaseBytes, entry.record.sizeBytes)) {
                return makeFailure("released byte accounting overflow");
            }
            ++releaseCount;
            releaseBytes += entry.record.sizeBytes;
        }
    }
    auto releaseValidation = validateReleasedBytes(releasedBytes_, releaseBytes);
    if (!releaseValidation.success) {
        return releaseValidation;
    }

    for (std::size_t index = 0; index < deferred_.size();) {
        const auto& entry = deferred_[index];
        if (entry.record.scope == FrameResourceScope::Surface) {
            deferred_.erase(deferred_.begin() + static_cast<std::ptrdiff_t>(index));
        } else {
            ++index;
        }
    }
    for (std::size_t index = 0; index < transient_.size();) {
        const auto& entry = transient_[index];
        if (entry.record.scope == FrameResourceScope::Surface) {
            transient_.erase(transient_.begin() + static_cast<std::ptrdiff_t>(index));
        } else {
            ++index;
        }
    }

    auto result = makeSuccess();
    result.releasedCount = releaseCount;
    result.releasedBytes = releaseBytes;
    releasedBytes_ += releaseBytes;
    return result;
}

FrameResourceRecyclerStats FrameResourceRecycler::stats() const
{
    FrameResourceRecyclerStats current;
    current.logicalFrameSlots = desc_.logicalFrameSlots;
    current.pendingDestroyCount = static_cast<std::uint32_t>(deferred_.size());
    current.transientResourceCount = static_cast<std::uint32_t>(transient_.size());
    current.releasedBytes = releasedBytes_;

    for (const auto& entry : deferred_) {
        current.pendingDestroyBytes += entry.record.sizeBytes;
    }
    for (const auto& entry : transient_) {
        current.transientBytes += entry.record.sizeBytes;
    }
    return current;
}

FrameResourceOperationResult FrameResourceRecycler::validateConfiguration() const
{
    if (!configurationDiagnostics_.empty()) {
        return makeFailure(configurationDiagnostics_.c_str());
    }
    return makeSuccess();
}

FrameResourceOperationResult FrameResourceRecycler::validateRecord(
    const FrameResourceRecord& record) const
{
    if (desc_.logicalFrameSlots != 2) {
        return makeFailure("frame resource recycler requires two logical frame slots");
    }
    if (!record.id.isValid()) {
        return makeFailure("frame resource record requires a valid resource id");
    }
    if (record.sizeBytes == 0) {
        return makeFailure("frame resource record requires non-zero size");
    }
    if (record.nativeHandleExposed) {
        return makeFailure("frame resource record must not expose native handle");
    }
    return makeSuccess();
}

FrameResourceOperationResult FrameResourceRecycler::validateFrameSlot(std::uint32_t frameSlot) const
{
    if (desc_.logicalFrameSlots != 2) {
        return makeFailure("frame resource recycler requires two logical frame slots");
    }
    if (frameSlot >= desc_.logicalFrameSlots) {
        return makeFailure("frame slot is outside the double-buffer range");
    }
    return makeSuccess();
}

std::uint32_t FrameResourceRecycler::frameSlotForSerial(std::uint64_t frameSerial) const
{
    return static_cast<std::uint32_t>(frameSerial % desc_.logicalFrameSlots);
}

} // namespace arp::rhi
