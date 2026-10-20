#pragma once
#include "original_mixed_depth_storage_plan_fixture.h"
#include "rhi/backends/vulkan/vulkan_native_resources.h"
#include "rhi/frontend/command_sources.h"

#include <array>
#include <cstring>

namespace arp::test {
// Authored RootGraphicsBuffers inputs. Every value and byte count corresponds
// to its published Slang ABI, not a backend-specific material representation.
class VulkanOriginalGraphicsBufferDataFixture final {
public:
    bool mixedContent = false;
    bool ordinaryDepthRoot = false;
    bool retireDevice = false;

    bool originalDeviceLifetimeRetired() const noexcept { return retireDevice; }

    void configureAdditionalRegistration(rhi::OriginalResourceRegistrationBudget& b) const
    {
        if (mixedContent)
            OriginalMixedDepthStoragePlanFixture::configureRegistration(b);
        if (ordinaryDepthRoot) {
            // One new snapshot (metadata/latch/readback), Results, one upload
            // staging buffer, one completed color image/view, three samplers,
            // and snapshot/ordinary descriptor domains on the same device.
            constexpr std::uint32_t buffers = 5u, images = 2u, views = 2u, samplers = 3u,
                                    domains = 2u;
            b.maxBuffers += buffers;
            b.maxTextures += images;
            b.maxTextureViews += views;
            b.samplers.maxSamplers += samplers;
            b.maxDescriptorArenas += domains;
            b.maxDescriptorSets += domains;
            b.maxRetainedOwners += (buffers + images + views + samplers + domains)
                    * rhi::OriginalResourceRegistrationContract::perResourceOwnerCount
                + domains * rhi::OriginalResourceRegistrationContract::perDescriptorOwnerCount;
        }
    }

    static constexpr bool enabled = true;

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
        b.maxCommands = 32u;
        b.maxReferences = 32u;
        b.maxInputDescriptors = 16u;
        b.maxStateObservations = 64u;
    }

    template <class Table> void configureBuffer(rhi::BufferDesc& desc, const Table& table) const
    {
        assert(table.tableId >= 1u && table.tableId <= 3u);
        desc.memory = rhi::MemoryUsage::CpuToGpu;
        desc.sizeBytes = table.tableId == 3u ? 3u * table.bufferLayout.elementStrideBytes
                                             : table.bufferLayout.minBindingSizeBytes;
    }

    template <class Table>
    void initializeBuffer(rhi::GpuBuffer& buffer, const Table& table, std::uint32_t index) const
    {
        assert(index < 2u);
        const auto native = rhi_vulkan::vulkanRhiNativePayload(buffer);
        assert(native && native->backendMappedData());
        std::memset(native->backendMappedData(), 0, buffer.desc().sizeBytes);
        if (table.tableId == 1u) {
            const auto value = color(index);
            assert(buffer.desc().sizeBytes == sizeof(value));
            std::memcpy(native->backendMappedData(), value.data(), sizeof(value));
        } else {
            const std::uint32_t value = (table.tableId == 2u ? 10u : 100u) * (index + 1u);
            std::memcpy(native->backendMappedData(), &value, sizeof(value));
        }
        core::pmr::String diagnostic;
        assert(native->backendFlushMappedWrite(0u, buffer.desc().sizeBytes, diagnostic));
    }

    template <class Root> static void verify(const Root& root)
    {
        std::array<std::uint32_t, 3u> counts{};
        for (const auto& entry : root->elements()) {
            const auto binding = entry.declaration.write.binding;
            if (binding > 4u)
                continue; // Snapshot/fault resources have independent checks.
            const auto& lease =
                std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(entry.owner);
            const auto native = rhi_vulkan::vulkanRhiNativePayload(*lease->originalObject());
            const auto index = entry.declaration.write.arrayElement;
            assert(index < 2u && native);
            core::pmr::String diagnostic;
            const auto bytes = lease->originalObject()->desc().sizeBytes;
            assert(native->backendInvalidateMappedRead(0u, bytes, diagnostic));
            if (binding == 0u) {
                const auto expected = color(index);
                assert(bytes == sizeof(expected));
                assert(
                    !std::memcmp(native->backendMappedData(), expected.data(), sizeof(expected)));
                ++counts[0];
            } else if (binding == 2u) {
                std::uint32_t actual = 0u;
                assert(bytes == sizeof(actual));
                std::memcpy(&actual, native->backendMappedData(), sizeof(actual));
                assert(actual == 10u * (index + 1u));
                ++counts[1];
            } else {
                assert(binding == 4u && bytes == 3u * sizeof(std::uint32_t));
                std::array<std::uint32_t, 3u> actual{};
                std::memcpy(actual.data(), native->backendMappedData(), sizeof(actual));
                const std::array<std::uint32_t, 3u> expected{
                    100u * (index + 1u) + 2u, 10u * (index + 1u), 10u * (index + 1u)};
                assert(actual == expected);
                ++counts[2];
            }
        }
        assert((counts == std::array<std::uint32_t, 3u>{2u, 2u, 2u}));
    }

private:
    static std::array<float, 4u> color(std::uint32_t index) noexcept
    {
        return index ? std::array<float, 4u>{0.75f, 0.5f, 0.25f, 1.f}
                     : std::array<float, 4u>{0.25f, 0.5f, 0.75f, 1.f};
    }
};
} // namespace arp::test
