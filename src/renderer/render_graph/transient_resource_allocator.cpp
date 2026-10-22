#include "renderer/render_graph/transient_resource_allocator.h"

#include "core/memory/cpu_allocator.h"

#include <algorithm>
#include <map>

namespace arp::render_graph {

namespace {

/// Compute alignment for a texture format. GPU textures are typically
/// aligned to the format's block size (bytesPerPixel for non-compressed
/// formats; bytesPerBlock for BC1/BC3/BC7 -- see rhi::isBlockCompressed).
/// A minimum of 256 bytes is used to match common GPU memory allocator
/// alignment requirements. Currently unused but retained as the canonical
/// alignment helper for future transient texture allocation paths.
[[maybe_unused]] std::uint64_t alignmentForBytesPerPixel(std::uint64_t bytesPerPixel) noexcept
{
    if (bytesPerPixel == 0) {
        return 256;
    }
    return std::max<std::uint64_t>(256, bytesPerPixel);
}

} // namespace

FrameGraphTransientAllocationReport FrameGraphTransientResourceAllocator::allocate(
    const CompiledFrameGraph& compiled) const
{
    return allocate(compiled, {});
}

FrameGraphTransientAllocationReport FrameGraphTransientResourceAllocator::allocate(
    const CompiledFrameGraph& compiled,
    const std::pmr::vector<TransientTextureSizeInfo>& sizeInfos) const
{
    FrameGraphTransientAllocationReport report;
    report.aliasedMemory = aliasedMemory_;
    report.completionGatedRetirementRequired = aliasedMemory_;

    // Build a handleId -> sizeInfo lookup for physical allocation computation.
    std::pmr::map<std::uint32_t, const TransientTextureSizeInfo*> sizeByHandleId;
    for (const auto& info : sizeInfos) {
        sizeByHandleId[info.handleId] = &info;
    }

    // Group alias entries by aliasGroup, preserving first-seen order.
    std::pmr::map<std::uint32_t, std::pmr::vector<const AliasTraceRecord*>> groupMembers;
    using ResourceLifetimeKey = std::pair<ResourceKind, std::uint32_t>;
    std::pmr::map<ResourceLifetimeKey, const ResourceLifetimeRecord*> lifetimesByHandle;
    for (const auto& lifetime : compiled.resourceLifetimes()) {
        const ResourceLifetimeKey key{lifetime.kind, lifetime.handleId};
        if (lifetime.handleId == 0u || lifetime.firstUseOrder > lifetime.lastUseOrder
            || lifetimesByHandle.find(key) != lifetimesByHandle.end()) {
            report.diagnostics = "RG-ALIAS-LIFETIME-METADATA-INVALID";
            return report;
        }
        lifetimesByHandle.emplace(key, &lifetime);
    }
    for (const auto& alias : compiled.aliasPlan()) {
        const ResourceLifetimeKey key{ResourceKind::Texture, alias.handle.id};
        if (alias.aliasGroup == 0) {
            report.diagnostics = "transient alias assignment requires a non-zero alias group";
            return report;
        }
        if (alias.handle.id == 0u || lifetimesByHandle.find(key) == lifetimesByHandle.end()) {
            report.diagnostics = "RG-ALIAS-LIFETIME-PROOF-MISSING";
            return report;
        }
        groupMembers[alias.aliasGroup].push_back(&alias);
    }

    // Aliased members may share bytes only when their compiled pass-order
    // intervals do not overlap. This proof is backend-neutral and must hold
    // before Metal/Vulkan placement is attempted.
    for (const auto& [group, members] : groupMembers) {
        for (std::size_t index = 0; index < members.size(); ++index) {
            const auto* lhs = lifetimesByHandle[{ResourceKind::Texture, members[index]->handle.id}];
            for (std::size_t other = index + 1; other < members.size(); ++other) {
                const auto* rhs =
                    lifetimesByHandle[{ResourceKind::Texture, members[other]->handle.id}];
                if (!(lhs->lastUseOrder < rhs->firstUseOrder
                        || rhs->lastUseOrder < lhs->firstUseOrder)) {
                    report.diagnostics = std::pmr::string(
                        "RG-ALIAS-LIFETIME-OVERLAP-IN-GROUP-" + std::to_string(group));
                    return report;
                }
            }
        }
    }
    report.lifetimeProofValidated = true;
    report.sizeMetadataValidated = groupMembers.empty() || !sizeInfos.empty();

    // Assign physical allocation indices (one per group, sequential).
    std::pmr::map<std::uint32_t, std::uint32_t> physicalIndexByGroup;
    std::uint32_t nextPhysicalAllocation = 0;
    for (const auto& [group, members] : groupMembers) {
        physicalIndexByGroup[group] = nextPhysicalAllocation++;
    }

    // Build per-member assignments.
    for (const auto& alias : compiled.aliasPlan()) {
        report.assignments.push_back({
            alias.resourceName,
            alias.handle,
            alias.aliasGroup,
            physicalIndexByGroup[alias.aliasGroup],
        });
    }

    // Build physical allocations with size/alignment/offsets.
    for (const auto& [group, members] : groupMembers) {
        TransientPhysicalAllocation allocation;
        allocation.aliasGroup = group;

        std::uint64_t maxSize = 0;
        std::uint64_t maxAlignment = 1;
        for (const auto* member : members) {
            auto sizeIt = sizeByHandleId.find(member->handle.id);
            std::uint64_t memberSize = 0;
            std::uint64_t memberAlignment = 256;
            if (sizeIt != sizeByHandleId.end()) {
                memberSize = sizeIt->second->sizeBytes;
                memberAlignment = sizeIt->second->alignmentBytes;
            }
            maxSize = std::max(maxSize, memberSize);
            maxAlignment = std::max(maxAlignment, memberAlignment);
        }

        // Phase 3 diagnostic: incompatible packing detection.
        // When sizeInfos is provided, every member must have a known size.
        // A member with size==0 when sizeInfos is non-empty means the
        // allocator was given incomplete metadata and cannot produce a
        // valid physical allocation.
        if (!sizeInfos.empty()) {
            for (const auto* member : members) {
                auto sizeIt = sizeByHandleId.find(member->handle.id);
                if (sizeIt == sizeByHandleId.end() || sizeIt->second->sizeBytes == 0) {
                    report.diagnostics = std::pmr::string(arp::core::toStdString(
                        std::pmr::string("transient alias group ")
                        + std::pmr::string(std::to_string(group)) + std::pmr::string(" member \"")
                        + member->resourceName + std::pmr::string("\" (handle ")
                        + std::pmr::string(std::to_string(member->handle.id))
                        + std::pmr::string(") has unknown or zero size; "
                                           "cannot compute a valid physical allocation")));
                    return report;
                }
            }

            const auto aligned = core::alignCpuAllocationOffsetUp(
                static_cast<std::size_t>(maxSize), static_cast<std::size_t>(maxAlignment));
            if (!aligned.success) {
                report.diagnostics =
                    std::pmr::string("RG-ALIAS-ALIGNMENT-INVALID: ") + aligned.diagnostics;
                return report;
            }
            maxSize = aligned.alignedValue;
        }

        allocation.sizeBytes = maxSize;
        allocation.alignmentBytes = maxAlignment;

        // Interval bin-pack: each member starts at offset 0 (they share the
        // same backing memory via aliasing, so all offsets are 0 within the
        // group). The member's individual size is recorded for diagnostics.
        for (const auto* member : members) {
            auto sizeIt = sizeByHandleId.find(member->handle.id);
            std::uint64_t memberSize = 0;
            if (sizeIt != sizeByHandleId.end()) {
                memberSize = sizeIt->second->sizeBytes;
            }
            allocation.members.push_back({
                member->handle.id,
                0, // offsetBytes: all aliased members share offset 0
                memberSize,
            });
        }

        report.physicalAllocations.push_back(std::move(allocation));
    }

    std::uint64_t physicalPeakBytes = 0u;
    for (const auto& allocation : report.physicalAllocations) {
        if (allocation.sizeBytes > UINT64_MAX - physicalPeakBytes) {
            report.diagnostics = "RG-ALIAS-PHYSICAL-BUDGET-OVERFLOW";
            return report;
        }
        physicalPeakBytes += allocation.sizeBytes;
    }
    const auto physicalBudget = compiled.transientPhysicalPeakBytes();
    if (report.sizeMetadataValidated && physicalPeakBytes > physicalBudget) {
        report.diagnostics = "RG-ALIAS-PHYSICAL-PEAK-EXCEEDS-COMPILED-BUDGET";
        return report;
    }
    report.budgetProofValidated = report.sizeMetadataValidated;

    report.transientTextureCount = static_cast<std::uint32_t>(report.assignments.size());
    report.physicalTextureAllocationCount = static_cast<std::uint32_t>(physicalIndexByGroup.size());
    if (report.transientTextureCount > report.physicalTextureAllocationCount) {
        report.aliasReuseCount =
            report.transientTextureCount - report.physicalTextureAllocationCount;
    }
    report.backendFallbacksHiddenFromUpperLayer = true;
    report.nativeHandleExposedToUpperLayer = false;
    report.success = true;
    return report;
}

} // namespace arp::render_graph
