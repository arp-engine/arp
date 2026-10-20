#pragma once
#include "original_graphics_instance64_data_fixture.h"
#include "original_graphics_instance64_memory_fixture.h"
#include "original_graphics_matrix_data_fixture.h"
#include "original_graphics_matrix_memory_fixture.h"
#include "original_graphics_pbr_material_data_fixture.h"
#include "original_graphics_runtime_vector_memory_fixture.h"
#include "original_graphics_skinning256_data_fixture.h"
#include "original_graphics_terrain27_data_fixture.h"
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"

namespace arp::test {
// Data only: execution, native custody, fault results and retirement remain in
// WebGpuOriginalBindlessRootExecutionFixtureImpl and the public original Root.
template <class Data> class WebGpuOriginalGraphicsMatrixDataFixtureImpl final {
public:
    using DataModel = Data;
    static constexpr std::size_t readbackBytes = sizeof(typename Data::Numeric);
    static constexpr bool terrain = std::is_same_v<Data, OriginalGraphicsTerrain27DataFixture>;
    static constexpr bool sampled =
        terrain || std::is_same_v<Data, OriginalGraphicsPbrMaterialDataFixture>;
    static constexpr std::uint32_t resultTable = terrain ? 4u : sampled ? 3u : 1u;
    static constexpr std::uint32_t resultBinding = terrain ? 28u : sampled ? 12u : 2u;
    static constexpr const char* sampledReceipt = terrain ? "TERRAIN27" : "PBR_MATERIAL";
    static constexpr std::uint32_t bindingCount = terrain ? 33u : sampled ? 17u : 16u;

    template <class Root, class Sources>
    static void inspectOriginalRoot(
        const Root& root, const Sources& sources, rhi::GpuDevice* device, bool fault)
    {
        if constexpr (std::is_same_v<Data, OriginalGraphicsInstance64DataFixture>)
            OriginalGraphicsInstance64MemoryFixture::inspect(root, sources, device, fault);
        else if constexpr (sampled)
            OriginalGraphicsRuntimeVectorMemoryFixture::inspect(root,
                sources,
                device,
                fault,
                Data::runtimeStride,
                Data::runtimeCapacity,
                Data::ordinaryBytes[1]);
        else
            OriginalGraphicsMatrixMemoryFixture::inspect(
                root, sources, device, fault, false, Data::ordinaryBytes);
    }

    static void configureRequest(rhi::BindlessRequirements& request)
    {
        if constexpr (terrain) {
            assert(request.tables.size() == 4u);
            for (auto& table : request.tables)
                table.resourceCount = table.tableId == 1u ? 25u : table.tableId == 2u ? 2u : 1u;
        } else if constexpr (sampled) {
            assert(request.tables.size() == 3u);
            for (auto& table : request.tables)
                table.resourceCount = table.tableId == 1u ? 10u : table.tableId == 2u ? 2u : 1u;
        } else {
            assert(request.tables.size() == 1u);
            request.tables.front().resourceCount = 1u;
        }
    }

    static void configureOrdinaryBuffer(
        rhi::BufferDesc& desc, std::uint32_t set, const rhi::DescriptorBindingDesc& binding)
    {
        Data::configureOrdinaryBuffer(desc, set, binding);
    }

    static void write(rhi::GpuBuffer& buffer, const void* bytes, std::size_t size)
    {
        assert(size == buffer.desc().sizeBytes);
        const auto native = rhi_webgpu::webGpuNativePayloadFor(buffer);
        assert(native && native->buffer && native->sizeBytes == size);
        const auto queue = wgpuDeviceGetQueue(native->ownerDevice);
        assert(queue);
        wgpuQueueWriteBuffer(queue, native->buffer, 0u, bytes, size);
        wgpuQueueRelease(queue);
    }

    static void initializeOrdinaryBuffer(
        rhi::GpuBuffer& buffer, std::uint32_t set, const rhi::DescriptorBindingDesc& binding)
    {
        assert(set == 0u && binding.binding <= 1u);
        const auto primary = Data::ordinary0();
        const auto secondary = Data::ordinary1();
        write(buffer,
            binding.binding == 0u ? static_cast<const void*>(&primary) : &secondary,
            binding.binding == 0u ? sizeof(primary) : sizeof(secondary));
    }

    template <class Table> static void configureBuffer(rhi::BufferDesc& desc, const Table& table)
    {
        assert(table.tableId == resultTable);
        desc.sizeBytes = readbackBytes;
    }

    template <class Table>
    static void initializeBuffer(rhi::GpuBuffer& buffer, const Table& table, std::uint32_t index)
    {
        assert(table.tableId == resultTable && index == 0u);
        typename Data::Numeric poison;
        poison.fill(-999.f);
        write(buffer, poison.data(), sizeof(poison));
    }

    static void verify(const std::array<std::uint8_t, 512u + readbackBytes>& actual, bool fault)
    {
        typename Data::Numeric numbers;
        std::memcpy(numbers.data(), actual.data() + 512u, readbackBytes);
        Data::verify(numbers);
        for (std::size_t y = 0u; y < 2u; ++y)
            for (std::size_t x = 0u; x < 2u; ++x) {
                const auto* pixel = actual.data() + y * 256u + x * 4u;
                const auto expected = [&]() {
                    if constexpr (sampled)
                        return Data::expectedColor(x);
                    else
                        return std::array<std::uint8_t, 4u>{64, 128, 191, 255};
                }();
                for (std::size_t c = 0u; c < expected.size(); ++c)
                    // UNORM quantization has one-unit tolerance. Numeric
                    // comparison above follows the selected data model.
                    assert(std::abs(int(pixel[c]) - expected[c]) <= 1);
            }
        if constexpr (sampled) {
            std::printf("BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_%s_PARITY_VALUES fault%u",
                terrain ? "TERRAIN27" : "PBR",
                fault ? 1u : 0u);
            for (const auto value : numbers)
                std::printf(" %.9g", value);
            std::printf("\nBINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_%s_PARITY_PIXELS fault%u",
                terrain ? "TERRAIN27" : "PBR",
                fault ? 1u : 0u);
            for (std::size_t y = 0u; y < 2u; ++y)
                for (std::size_t x = 0u; x < 8u; ++x)
                    std::printf(" %u", static_cast<unsigned>(actual[y * 256u + x]));
            std::puts("");
        }
        const char* receipt = terrain
            ? "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_TERRAIN27_NUMERIC_OK fault%u lanes128 tolerance2e-5 %s ordinary%llu+%llu color2x2\n"
            : sampled
            ? "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_PBR_MATERIAL_NUMERIC_OK fault%u lanes128 tolerance2e-5 %s ordinary%llu+%llu color2x2\n"
            : std::is_same_v<Data, OriginalGraphicsInstance64DataFixture>
            ? "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_INSTANCE64_NUMERIC_OK fault%u lanes176 tolerance2e-5 metadata-bits %s ordinary%llu+%llu color2x2\n"
            : std::is_same_v<Data, OriginalGraphicsSkinning256DataFixture>
            ? "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_MATRIX_NUMERIC_OK fault%u lanes64 position-exact normal-tangent2e-5 %s ordinary%llu+%llu color2x2\n"
            : "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_MATRIX_NUMERIC_OK fault%u lanes32 exact %s ordinary%llu+%llu color2x2\n";
        std::printf(receipt,
            fault ? 1u : 0u,
            Data::numericDescription,
            static_cast<unsigned long long>(Data::ordinaryBytes[0]),
            static_cast<unsigned long long>(Data::ordinaryBytes[1]));
    }
};

using WebGpuOriginalGraphicsMatrixDataFixture =
    WebGpuOriginalGraphicsMatrixDataFixtureImpl<OriginalGraphicsMatrixDataFixture>;
using WebGpuOriginalGraphicsSkinning256DataFixture =
    WebGpuOriginalGraphicsMatrixDataFixtureImpl<OriginalGraphicsSkinning256DataFixture>;
using WebGpuOriginalGraphicsInstance64DataFixture =
    WebGpuOriginalGraphicsMatrixDataFixtureImpl<OriginalGraphicsInstance64DataFixture>;
using WebGpuOriginalGraphicsPbrMaterialDataFixture =
    WebGpuOriginalGraphicsMatrixDataFixtureImpl<OriginalGraphicsPbrMaterialDataFixture>;
using WebGpuOriginalGraphicsTerrain27DataFixture =
    WebGpuOriginalGraphicsMatrixDataFixtureImpl<OriginalGraphicsTerrain27DataFixture>;
} // namespace arp::test
