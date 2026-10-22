#pragma once
#include "original_graphics_color_gi_data_fixture.h"
#include "renderer/gi/ibl_bake.h"
#include "rhi/gpu_commands.h"

#include <cstring>
#include <filesystem>
#include <fstream>

namespace arp::test {
// Shared authored WOCC IBL bake/upload metadata and independent unfiltered
// oracle. Backends retain their original upload, journal and completion paths.
class OriginalGraphicsWoccIblDataFixture final {
public:
    static constexpr std::uint32_t resultBinding = 3u;
    static constexpr const char* packageName = "GraphicsWoccIbl";
    static constexpr std::size_t wordsPerPixel = 106u, pixels = 4u;
    static constexpr std::uint64_t cubePixelBytes =
        (4096u + 1024u + 256u + 64u + 16u + 4u + 1u) * 6u * 8u;
    static constexpr std::uint64_t originalRootCpuBytes = 65536u + cubePixelBytes;
    using Numeric = std::array<float, wordsPerPixel * pixels>;

    class Inputs final {
    public:
        struct Frozen {};

        std::array<core::ibl::PrefilteredCubeData, 2u> cubes;
        core::pmr::Vector<std::uint16_t> lut;

        explicit Inputs(Frozen) {}

        Inputs()
        {
            auto* allocator = &core::defaultCpuMemoryResource();
            for (std::uint32_t index = 0u; index < 2u; ++index)
                cubes[index] = core::ibl::bakePrefilteredSpecularCube(
                    [index](core::Vec3 direction) {
                        const float zenith = std::pow(std::clamp(direction.y, 0.f, 1.f), 0.6f);
                        const float value = index ? zenith : 1.f - zenith;
                        return core::Vec3{value, value, value};
                    },
                    64u,
                    7u,
                    allocator);
            lut = core::ibl::bakeBrdfIntegrationLut(128u, allocator);
        }

        float center(std::uint32_t cube, std::uint32_t face, std::uint32_t mip) const
        {
            const auto& data = cubes[cube];
            const auto resolution = data.baseResolution >> mip;
            const auto low = (resolution - 1u) / 2u, high = resolution / 2u;
            float value = 0.f;
            for (const auto y : {low, high})
                for (const auto x : {low, high})
                    value += core::ibl::halfToFloat(data.storage[data.mips[mip].texelOffset
                        + ((face * resolution + y) * resolution + x) * 4u]);
            return value * 0.25f;
        }

        void exportCold(const std::filesystem::path& directory) const
        {
            std::filesystem::create_directories(directory);
            const auto write = [&](const char* name, const auto& data) {
                std::ofstream stream(directory / name, std::ios::binary);
                stream.write(reinterpret_cast<const char*>(data.data()),
                    static_cast<std::streamsize>(data.size() * sizeof(std::uint16_t)));
                assert(stream.good());
            };
            write("horizon.rgba16f", cubes[0].storage);
            write("zenith.rgba16f", cubes[1].storage);
            write("brdf.rgba16f", lut);
        }
    };

    static rhi::UploadTextureDesc textureUpload(
        const Inputs& inputs, rhi::TextureDesc& desc, const rhi::DescriptorBindingDesc& binding)
    {
        using namespace rhi;
        assert(!binding.depthTexture
            && (binding.binding == 0u || binding.binding == 1u || binding.binding == 3u));
        desc.format = TextureFormat::Rgba16Float;
        desc.initialState = ResourceState::Undefined;
        desc.usage = TextureUsage::CopyDst | TextureUsage::Sampled;
        const bool cube = binding.textureViewDimension == TextureViewDimension::TextureCube;
        desc.extent = cube ? Extent3D{64u, 64u, 1u} : Extent3D{128u, 128u, 1u};
        desc.mipLevelCount = cube ? 7u : 1u;
        UploadTextureDesc upload;
        upload.extent = desc.extent;
        upload.format = desc.format;
        upload.subresources = defaultTextureSubresourceRange(desc);
        upload.bytesPerRow = desc.extent.width * 8u;
        upload.rowsPerImage = desc.extent.height;
        if (cube) {
            const auto& data = inputs.cubes[binding.binding == 3u ? 1u : 0u];
            upload.pixelData = data.storage.data();
            upload.pixelDataSize = data.storage.size() * sizeof(std::uint16_t);
            for (std::uint32_t mip = 0u; mip < data.mipCount; ++mip) {
                const auto size = data.baseResolution >> mip;
                upload.mipLayouts.push_back({mip,
                    {size, size, 1u},
                    size * 8u,
                    size,
                    data.mips[mip].texelOffset * sizeof(std::uint16_t)});
            }
        } else {
            assert(binding.binding == 1u);
            upload.pixelData = inputs.lut.data();
            upload.pixelDataSize = inputs.lut.size() * sizeof(std::uint16_t);
            upload.mipLayouts.push_back(
                {0u, desc.extent, upload.bytesPerRow, upload.rowsPerImage, 0u});
        }
        return upload;
    }

    static auto analyticalSamples(const Inputs& inputs)
    {
        std::array<float, wordsPerPixel> expected{};
        for (std::uint32_t cube = 0u; cube < 2u; ++cube)
            for (std::uint32_t face = 0u; face < 6u; ++face) {
                for (std::uint32_t mip = 0u; mip < 7u; ++mip)
                    expected[cube * 48u + face * 8u + mip] = 17.f + inputs.center(cube, face, mip);
                expected[cube * 48u + face * 8u + 7u] =
                    17.f + (inputs.center(cube, face, 2u) + inputs.center(cube, face, 3u)) * 0.5f;
            }
        const std::array<std::array<std::uint32_t, 2u>, 4u> texels{
            {{7u, 15u}, {64u, 64u}, {120u, 96u}, {127u, 0u}}};
        for (std::uint32_t probe = 0u; probe < texels.size(); ++probe)
            for (std::uint32_t channel = 0u; channel < 2u; ++channel)
                expected[96u + probe * 2u + channel] = 17.f
                    + core::ibl::halfToFloat(
                        inputs.lut[(texels[probe][1] * 128u + texels[probe][0]) * 4u + channel]);
        expected[104] = expected[7];
        expected[105] = expected[55];
        return expected;
    }

    static void verifyUnfiltered(const Numeric& actual, const Inputs& inputs)
    {
        const auto analytical = analyticalSamples(inputs);
        for (std::size_t i = 0u; i < actual.size(); ++i) {
            assert(std::isfinite(actual[i]));
            const auto lane = i % wordsPerPixel;
            if ((lane < 96u && lane % 8u == 6u) || (lane >= 96u && lane < 104u))
                assert(actual[i] == analytical[lane]);
        }
    }

    inline static std::shared_ptr<const Inputs> inputs;
    inline static Numeric directReference;
    inline static bool directReferenceReady = false;

    static void verifyReference(const Numeric& actual)
    {
        assert(inputs);
        verifyUnfiltered(actual, *inputs);
        directReference = actual;
        directReferenceReady = true;
    }

    static void verify(const Numeric& actual)
    {
        assert(directReferenceReady);
        assert(!std::memcmp(actual.data(), directReference.data(), sizeof(actual)));
    }

    static constexpr auto expectedColor(std::size_t x) noexcept
    {
        return OriginalGraphicsColorGiDataFixture::expectedColor(x);
    }
};
} // namespace arp::test
