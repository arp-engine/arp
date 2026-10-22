#pragma once
#include "3d/vegetation/template.h"
#include "original_bindless_buffer_range_fixture.h"
#include "vulkan_original_graphics_buffer_data_fixture.h"

#include <cstddef>

namespace arp::test {
// Uses the existing production foliage record, including its packed metadata
// helper. It supplies data to the shared original Root fixture, never an issuer.
class VulkanOriginalGraphicsRecordDataFixture final {
    struct Material {
        std::array<float, 4u> color;
        std::array<std::uint32_t, 4u> resources;
    };

    struct Output {
        std::array<float, 4u> color;
        std::array<std::uint32_t, 4u> metadata;
        std::array<std::int32_t, 4u> signedValues;
    };

    static_assert(sizeof(Material) == 32u && offsetof(Material, resources) == 16u);
    static_assert(sizeof(vegetation::InstanceDataGpu) == 64u
        && offsetof(vegetation::InstanceDataGpu, positionScale) == 0u
        && offsetof(vegetation::InstanceDataGpu, yawPhaseIdLod) == 16u
        && offsetof(vegetation::InstanceDataGpu, extraTransform) == 32u
        && offsetof(vegetation::InstanceDataGpu, speciesLodState) == 48u);
    static_assert(sizeof(Output) == 48u && offsetof(Output, metadata) == 16u
        && offsetof(Output, signedValues) == 32u);

public:
    static constexpr bool enabled = true;
    bool finiteRanges = false;

    template <class Table>
    rhi::DescriptorBufferBindingRange originalBufferRange(
        rhi::GpuDevice& device, const Table& table, rhi::BufferDesc& desc) const
    {
        if (!finiteRanges)
            return {};
        const auto range =
            OriginalBindlessBufferRangeFixture{}.originalBufferRange(device, table, desc);
        ranges_[table.tableId - 1u] = range;
        return range;
    }

    template <class Result, class Request>
    void verifyRetiredResult(const Result& result, const Request& request) const
    {
        assert(!result && !request);
    }

    template <class Root, class Inventory, class Binding, class Transaction>
    void operator()(
        const Root&, const Inventory&, const Binding&, const Transaction&, rhi::GpuDevice&) const
    {
        assert(false && "Graphics executes through the shared original Graphics observer");
    }

    void configureAdmission(rhi::OriginalCommandAdmissionBudget& b) const
    {
        VulkanOriginalGraphicsBufferDataFixture{}.configureAdmission(b);
    }

    template <class Table> void configureBuffer(rhi::BufferDesc& desc, const Table& table) const
    {
        assert(table.tableId >= 1u && table.tableId <= 3u);
        const auto stride = table.tableId == 1u ? sizeof(Material)
            : table.tableId == 2u               ? sizeof(vegetation::InstanceDataGpu)
                                                : sizeof(Output);
        assert(table.bufferLayout.elementStrideBytes == stride
            && table.bufferLayout.minBindingSizeBytes == stride);
        desc.memory = rhi::MemoryUsage::CpuToGpu;
        desc.sizeBytes = stride * (table.tableId == 1u ? 1u : 2u);
    }

    template <class Table>
    void initializeBuffer(rhi::GpuBuffer& buffer, const Table& table, std::uint32_t index) const
    {
        assert(index < 2u);
        const auto native = rhi_vulkan::vulkanRhiNativePayload(buffer);
        assert(native && native->backendMappedData());
        auto* data = static_cast<std::uint8_t*>(native->backendMappedData());
        const auto range = finiteRanges
            ? ranges_[table.tableId - 1u]
            : rhi::DescriptorBufferBindingRange{2u, 0u, buffer.desc().sizeBytes};
        assert(range.contractVersion == 2u && range.sizeBytes
            && range.offsetBytes + range.sizeBytes <= buffer.desc().sizeBytes);
        std::memset(data, finiteRanges ? 0xcdu : 0u, buffer.desc().sizeBytes);
        data += range.offsetBytes;
        std::memset(data, 0, range.sizeBytes);
        if (table.tableId == 1u) {
            const auto value = material(index);
            assert(range.sizeBytes == sizeof(value));
            std::memcpy(data, &value, sizeof(value));
        } else if (table.tableId == 2u) {
            assert(range.sizeBytes == 2u * sizeof(vegetation::InstanceDataGpu));
            for (std::uint32_t element = 0u; element < 2u; ++element) {
                const auto value = instance(index, element);
                std::memcpy(data + element * sizeof(value), &value, sizeof(value));
            }
        } else
            assert(range.sizeBytes == 2u * sizeof(Output));
        core::pmr::String diagnostic;
        assert(native->backendFlushMappedWrite(0u, buffer.desc().sizeBytes, diagnostic));
    }

    template <class Root> static void verify(const Root& root)
    {
        std::array<std::uint32_t, 3u> counts{};
        for (const auto& entry : root->elements()) {
            const auto binding = entry.declaration.write.binding;
            if (binding > 4u)
                continue;
            const auto& lease =
                std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(entry.owner);
            const auto native = rhi_vulkan::vulkanRhiNativePayload(*lease->originalObject());
            const auto index = entry.declaration.write.arrayElement;
            assert(index < 2u && native);
            const auto allocationBytes = lease->originalObject()->desc().sizeBytes;
            core::pmr::String diagnostic;
            assert(native->backendInvalidateMappedRead(0u, allocationBytes, diagnostic));
            rhi::DescriptorBufferBindingRangeContract::Resolved view;
            assert(!rhi::DescriptorBufferBindingRangeContract::resolve(
                entry.declaration.write.bufferBindingRange, allocationBytes, 0u, view));
            const auto* base = static_cast<const std::uint8_t*>(native->backendMappedData());
            for (std::size_t i = 0u; i < view.offsetBytes; ++i)
                assert(base[i] == 0xcdu);
            for (std::size_t i = view.offsetBytes + view.sizeBytes; i < allocationBytes; ++i)
                assert(base[i] == 0xcdu);
            const auto* data = base + view.offsetBytes;
            const auto bytes = view.sizeBytes;
            if (binding == 0u) {
                const auto expected = material(index);
                assert(bytes == sizeof(expected));
                assert(!std::memcmp(data, &expected, sizeof(expected)));
                ++counts[0];
            } else if (binding == 2u) {
                assert(bytes == 2u * sizeof(vegetation::InstanceDataGpu));
                for (std::uint32_t element = 0u; element < 2u; ++element) {
                    const auto expected = instance(index, element);
                    assert(!std::memcmp(
                        data + element * sizeof(expected), &expected, sizeof(expected)));
                }
                ++counts[1];
            } else {
                assert(binding == 4u && bytes == 2u * sizeof(Output));
                for (std::uint32_t element = 0u; element < 2u; ++element) {
                    const Output expected{material(index).color,
                        {0x00040302u + index + element, 0x87654321u, 0u, 0u},
                        {-1 - static_cast<std::int32_t>(index),
                            -2 - static_cast<std::int32_t>(element),
                            7,
                            -9}};
                    assert(!std::memcmp(
                        data + element * sizeof(expected), &expected, sizeof(expected)));
                }
                ++counts[2];
            }
        }
        assert((counts == std::array<std::uint32_t, 3u>{2u, 2u, 2u}));
    }

private:
    mutable std::array<rhi::DescriptorBufferBindingRange, 3u> ranges_{};

    static Material material(std::uint32_t index) noexcept
    {
        return {index ? std::array<float, 4u>{0.75f, 0.5f, 0.25f, 1.f}
                      : std::array<float, 4u>{0.25f, 0.5f, 0.75f, 1.f},
            {index, 5u, 7u, 9u}};
    }

    static vegetation::InstanceDataGpu instance(std::uint32_t index, std::uint32_t element) noexcept
    {
        vegetation::InstanceDataGpu value{
            {static_cast<float>(index + 1u), static_cast<float>(element + 2u), 3.f, 1.f},
            {0.125f, 0.25f, static_cast<float>(index), static_cast<float>(element)},
            {5.f, 6.f, 7.f, 8.f},
            {}};
        vegetation::packInstanceMetadataFloat(
            value.speciesLodState[0], 0x00040302u + index + element);
        vegetation::packInstanceMetadataFloat(value.speciesLodState[1], 0x87654321u);
        return value;
    }
};
} // namespace arp::test
