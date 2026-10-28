#include "core/io/file_io.h"
#include "core/math/half_float.h"
#include "renderer/volumetric_cloud/cloud_gpu_contract.h"
#include "renderer/volumetric_cloud/cloud_reference.h"
#include "resource/cloud_asset_cooked.h"
#include "tests/fixtures/cloud/cloud_reference_cases.h"
#include "tests/fixtures/cloud/cloud_sequence_oracle.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>

using namespace arp;
namespace cloud = renderer::cloud;

namespace {
void read(const std::filesystem::path& path, void* destination, std::size_t bytes)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    assert(stream && stream.tellg() == std::streamoff(bytes));
    stream.seekg(0);
    assert(stream.read(static_cast<char*>(destination), bytes));
}

core::Vec3 xyz(core::Vec4 v)
{
    return {v.x, v.y, v.z};
}
} // namespace

// Offline acceptance tool only: consumes exported real GPU bytes and the exact
// cooked asset. No backend capability, upload or rendering authority is granted.
int main(int argc, char** argv)
{
    assert(argc == 3 || argc == 4);
    const auto* selected = argc == 4 ? tests::cloud::ReferenceCases::find(argv[3]) : nullptr;
    assert(argc != 4 || selected);
    const std::filesystem::path directory(argv[1]);
    cloud::CloudGpuConstants p;
    read(directory / "constants.bin", &p, sizeof(p));
    const auto width = p.dimensions[0], height = p.dimensions[1];
    assert(width && height && width <= 128u && height <= 96u);
    assert(width == p.dimensions[2] && height == p.dimensions[3] && p.previousCamera.w == 0.f);
    const auto count = std::size_t(width) * height;
    core::pmr::Vector<cloud::CloudGpuSample> raw(count), resolved(count);
    read(directory / "raw.bin", raw.data(), count * sizeof(raw[0]));
    read(directory / "resolved.bin", resolved.data(), count * sizeof(resolved[0]));
    assert(!std::memcmp(raw.data(), resolved.data(), count * sizeof(raw[0])));
    const auto pitch = (width * 8u + 255u) / 256u * 256u;
    core::pmr::Vector<std::uint16_t> hdr(pitch / 2u * height);
    read(directory / "hdr.bin", hdr.data(), hdr.size() * 2u);
    const auto doc = core::readTextFile(argv[2], 4u * 1024u * 1024u);
    assert(doc.success);
    assets::cloud::CloudAsset asset;
    assert(
        resource::CloudAssetCookedCodec::decode(doc.text, 4u * 1024u * 1024u, 1024u * 1024u, asset)
            .success);
    scene3d::CloudLayerDesc layer;
    layer.planetCenter = xyz(p.cameraTime) - xyz(p.cameraRadial);
    layer.planetRadius = p.shell.z;
    layer.baseAltitude = p.shell.x;
    layer.thickness = p.shell.y;
    layer.extinction = p.shell.w;
    layer.scatteringAlbedo = xyz(p.albedoAnisotropy);
    layer.anisotropy = p.albedoAnisotropy.w;
    layer.weatherPeriod = p.periodsCoverage.x;
    layer.shapePeriod = p.periodsCoverage.y;
    layer.detailPeriod = p.periodsCoverage.z;
    layer.coverage = p.periodsCoverage.w;
    layer.windVelocity = xyz(p.windErosion);
    layer.erosion = p.windErosion.w;
    cloud::CloudReference oracle;
    assert(oracle.configure(asset, layer).success);
    tests::cloud::SequenceOracle geometry;
    geometry.parameters = p;
    float maxSample = 0.f, maxHdr = 0.f, maxMoment = 0.f;
    unsigned nonVacuum = 0;
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x) {
            const auto index = y * width + x;
            auto ray = geometry.ray((x + p.jitter.x) / width, (y + p.jitter.y) / height);
            const tests::cloud::ForegroundFixture triangle{selected && selected->foreground,
                .25f,
                .25f,
                float(width) - .5f,
                float(height) - .5f};
            const bool covered = triangle.covers(x, height - 1u - y);
            if (covered) {
                const auto surface = core::transformPoint(p.inverseViewProjection,
                    {2 * (x + p.jitter.x) / width - 1, 2 * (y + p.jitter.y) / height - 1, .5f});
                ray.maxDistance = std::min(ray.maxDistance,
                    double(std::max(0.f, core::dot(surface - ray.origin, ray.direction))));
            }
            renderer::volume::MediumIntegral expected;
            float moment = p.temporal.z;
            assert(oracle
                    .integrate(ray,
                        p.sampling[0],
                        p.sampling[1],
                        xyz(p.sunDirection),
                        xyz(p.sunRadiance),
                        xyz(p.ambientRadiance),
                        p.cameraTime.w,
                        expected,
                        p.jitter.z,
                        &moment)
                    .success);
            const std::array<float, 4> e{expected.radiance.x,
                expected.radiance.y,
                expected.radiance.z,
                expected.transmittance};
            const auto actual =
                tests::cloud::SequenceOracle::components(raw[index].radianceTransmittance);
            nonVacuum += expected.transmittance < 1.f;
            for (unsigned c = 0; c < 4u; ++c) {
                assert(std::isfinite(actual[c]));
                maxSample = std::max(maxSample, std::fabs(actual[c] - e[c]));
            }
            maxMoment = std::max(maxMoment,
                std::fabs(raw[index].metadata.x
                    - (expected.transmittance < 1.f ? moment : p.temporal.z)));
            const std::array<float, 4> background = selected
                ? std::array<float, 4>{0, 0, 0, 1}
                : std::array<float, 4>{.15f, .35f, .65f, 1};
            const auto scene =
                covered ? std::array<float, 4>{.125f, .375f, .75f, .625f} : background;
            const std::array<float, 4> expectedHdr{
                e[0] + e[3] * scene[0], e[1] + e[3] * scene[1], e[2] + e[3] * scene[2], scene[3]};
            for (unsigned c = 0; c < 4u; ++c) {
                const auto value = core::halfToFloat(hdr[y * pitch / 2u + x * 4u + c]);
                assert(std::isfinite(value) && value >= 0.f);
                maxHdr = std::max(maxHdr, std::fabs(value - expectedHdr[c]));
            }
        }
    std::cout << "{\"width\":" << width << ",\"height\":" << height
              << ",\"maxSampleError\":" << maxSample << ",\"maxHdrError\":" << maxHdr
              << ",\"maxMomentErrorMeters\":" << maxMoment << ",\"nonVacuum\":" << nonVacuum
              << ",\"rawEqualsResolved\":true,\"noHistory\":true}" << std::endl;
    assert(maxSample < .002f && maxHdr < .002f && (layer.coverage == 0.f || nonVacuum));
}
