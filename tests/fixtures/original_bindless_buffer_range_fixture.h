#pragma once
#include "rhi/frontend/bindless_snapshot.h"
#include "rhi/gpu_extensions.h"

#include <algorithm>
#include <cassert>

namespace arp::test {
struct OriginalBindlessBufferRangeFixture {
    static constexpr bool enabled = false;

    template <class Table>
    rhi::DescriptorBufferBindingRange originalBufferRange(
        rhi::GpuDevice& device, const Table& table, rhi::BufferDesc& desc) const
    {
        const auto* extension = static_cast<const rhi::BindlessCrossExtension*>(
            device.queryInterface(rhi::kExtensionBindlessCross));
        assert(extension);
        const auto& caps = extension->capabilities();
        const bool uniform =
            (desc.usage & rhi::ResourceUsage::UniformBuffer) != rhi::ResourceUsage::None;
        const auto alignment =
            uniform ? caps.uniformBufferOffsetAlignment : caps.storageBufferOffsetAlignment;
        assert(alignment && caps.limitsQueried);
        const rhi::DescriptorBufferBindingRange view{2u,
            std::max<std::uint64_t>(alignment, table.bufferLayout.elementAlignmentBytes),
            desc.sizeBytes};
        desc.sizeBytes =
            view.offsetBytes + view.sizeBytes + table.bufferLayout.elementAlignmentBytes;
        return view;
    }

    template <class Table, class Snapshot, class Lease>
    static void verifyFailures(const rhi::DescriptorBindingDesc& binding,
        const Table& table,
        Snapshot& snapshot,
        rhi::BindlessResourceHandle handle,
        rhi::GpuDevice& device,
        const Lease& source,
        const rhi::DescriptorBufferBindingRange& range)
    {
        const auto previous = snapshot.bufferElementCount(handle);
        const auto reject = [&](const auto& view, const auto& request) {
            assert(
                !snapshot.writeOriginalBufferRange(handle, device, source, view, request).success);
            assert(snapshot.bufferElementCount(handle) == previous);
        };
        for (const auto& request : std::array<rhi::OriginalBufferRangeDescriptorRequest, 4u>{
                 {{0u, true, 1u}, {1u, false, 1u}, {1u, true, 0u}, {1u, true, 2u}}})
            reject(range, request);
        auto invalid = range;
        invalid.contractVersion = 1u;
        reject(invalid, rhi::OriginalBufferRangeDescriptorRequest{1u, true, 1u});
        invalid = range;
        ++invalid.offsetBytes;
        reject(invalid, rhi::OriginalBufferRangeDescriptorRequest{1u, true, 1u});
        invalid = range;
        --invalid.sizeBytes;
        reject(invalid, rhi::OriginalBufferRangeDescriptorRequest{1u, true, 1u});
        invalid = range;
        invalid.sizeBytes = 0u;
        reject(invalid, rhi::OriginalBufferRangeDescriptorRequest{1u, true, 1u});
        invalid = range;
        invalid.offsetBytes = UINT64_MAX - 15u;
        reject(invalid, rhi::OriginalBufferRangeDescriptorRequest{1u, true, 1u});
        invalid = range;
        invalid.offsetBytes = source->layout().sizeBytes;
        reject(invalid, rhi::OriginalBufferRangeDescriptorRequest{1u, true, 1u});
        assert(
            !snapshot.writeOriginalBufferRange(handle, device, {}, range, {1u, true, 1u}).success);
        rhi::DescriptorBindingWrite raw;
        raw.type = binding.type;
        raw.resource = source->resource();
        raw.resourceKind = rhi::DescriptorResourceKind::Buffer;
        raw.resourceState = source->layout().initialState;
        raw.bufferBindingRange = range;
        const auto denied = snapshot.write(handle, raw);
        assert(!denied.success
            && denied.diagnostics == "RHI-DESCRIPTOR-BUFFER-SUBRANGE-LOWERING-MISSING");
        assert(snapshot.bufferElementCount(handle) == previous);
    }
};
} // namespace arp::test
