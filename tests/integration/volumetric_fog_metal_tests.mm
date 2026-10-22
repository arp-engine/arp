#include "core/math/half_float.h"
#include "renderer/volumetric_fog/volumetric_fog_feature.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_native_resources.h"
#include "rhi/frontend/transfer_upload_layout_validation.h"
#include "rhi/semantics/render_target_coordinates.h"
#include "tests/fixtures/metal_native_texture_readback_fixture.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>

namespace {
using namespace arp;
using namespace rhi;
using namespace renderer::fog;

class FogMetalFixture final {
public:
    FogMetalFixture()
    {
        rhi_metal::MetalRhiDeviceDesc desc;
        desc.capabilityContract = semantics::nativeBackendCapabilityContract(Backend::Metal);
        desc.capabilityContract.queriedFromNativeDevice = true;
        desc.capabilityContract.queriedExtendedFeatures = true;
        desc.memoryBudgetBytes = 32u * 1024u * 1024u;
        desc.uploadStagingPageSizeBytes = 16u * 1024u;
        desc.uploadStagingMaxPageCount = 2;
        device_ = rhi_metal::createMetalRhiDevice(desc);
        assert(device_);
    }

    void run(bool temporal, const char* output)
    {
        constexpr Extent2D extent{64, 48};
        FogConfig config;
        config.distance = 50;
        config.depthSlices = 16;
        config.pixelsPerCell = 8;
        config.temporal = temporal;
        VolumetricFogFeature feature;
        const auto created = feature.create({*device_, extent, config, ARP_FOG_SHADER_DIR});
        if (!created.success)
            std::cerr << created.diagnostic << '\n';
        assert(created.success);
        scene3d::Scene3DRenderQueue scene;
        scene3d::VolumetricFogDesc medium;
        medium.density = 0.04f;
        medium.heightFalloff = 0;
        medium.scatteringColor = {0.6f, 0.7f, 0.8f};
        medium.anisotropy = 0;
        scene.setFog(medium);
        const core::Vec3 eye{1, 3, -8};
        const auto view = core::lookAt(eye, {2, 3, 5});
        auto projection = core::perspective(60, 64.0f / 48, 0.1f, 100);
        rhi::semantics::applyProjectionYConvention(projection.m.data(), device_->backend());
        auto samplingVP = core::multiply(projection, view);
        rhi::semantics::applyRenderTargetSamplingYConvention(
            samplingVP.m.data(), device_->backend());
        const auto inverse = core::inverse(samplingVP);
        const auto grid = feature.grid();
        const auto bytes = grid.cells * sizeof(core::Vec4);
        auto staging = core::shareUnique(device_->createBuffer(
            {"Fog.TestReadback", bytes, ResourceUsage::CopyDst, MemoryUsage::GpuToCpu}));
        assert(staging);
        float maxIntegralError = 0, maxCompositeError = 0;
        for (unsigned frame = 0; frame != 3; ++frame) {
            render_graph::FrameGraph graph(*device_);
            const auto color = graph.createTransientTexture({"Fog.TestColor",
                {64, 48, 1},
                TextureFormat::Rgba16Float,
                TextureUsage::RenderAttachment | TextureUsage::Sampled,
                MemoryUsage::GpuOnly});
            const auto depth = graph.createTransientTexture({"Fog.TestDepth",
                {64, 48, 1},
                TextureFormat::D32Float,
                TextureUsage::RenderAttachment | TextureUsage::Sampled,
                MemoryUsage::GpuOnly});
            graph.addPass("Fog.TestClear")
                .writeAttachment(color,
                    ResourceState::ColorAttachment,
                    LoadOp::Clear,
                    StoreOp::Store,
                    {},
                    {0.3f, 0.5f, 0.8f, 1})
                .writeAttachment(
                    depth, ResourceState::DepthAttachment, LoadOp::Clear, StoreOp::Store)
                .executeRenderPass([](render_graph::RenderPassContext&) {});
            FogGraphContext context{graph, scene, color, depth};
            context.view = view;
            context.projection = projection;
            context.cameraPosition = eye;
            context.time = float(frame) / 60;
            context.primaryLight.objectId = scene::GameObjectId::fromValue(1);
            context.primaryLight.type = scene3d::LightType::Directional;
            context.primaryLight.intensity = 12;
            context.primaryLight.color = {1, 1, 1};
            context.primaryLight.castShadow = false;
            FogOutputs result;
            assert(feature.contributeGraph(context, result).success);
            assert(result.historyUsed == (temporal && frame > 0));
            const auto readback = graph.importBufferRef(staging);
            graph.addPass("Fog.TestCopyIntegral")
                .read(result.integrated, ResourceState::CopySrc)
                .write(readback, ResourceState::CopyDst)
                .externalSideEffects()
                .execute([&](render_graph::RenderContext& c) {
                    auto copy = c.commands().beginTransferPass({"Fog.TestCopyIntegral"});
                    assert(copy
                        && copy
                            ->copyBufferToBuffer(
                                {graph.buffer(result.integrated).id(), staging->id(), 0, 0, bytes})
                            .success);
                    assert(copy->end().success);
                });
            graph.addPass("Fog.TestKeepColor")
                .read(result.color, ResourceState::ShaderRead)
                .externalSideEffects()
                .execute([](render_graph::RenderContext&) {});
            graph.execute();
            device_->waitIdle();
            core::pmr::Vector<core::Vec4> actual(grid.cells);
            core::pmr::String diagnostic;
            assert(
                rhi_metal::readMetalRhiNativeBuffer(rhi_metal::metalRhiNativePayloadFor(*staging),
                    actual.data(),
                    bytes,
                    0,
                    bytes,
                    diagnostic));
            for (unsigned y = 0; y != grid.height; ++y)
                for (unsigned x = 0; x != grid.width; ++x) {
                    const float u = (x + 0.5f) / grid.width, v = (y + 0.5f) / grid.height;
                    const auto a = core::transformPoint(inverse, {u * 2 - 1, v * 2 - 1, 0});
                    const auto b = core::transformPoint(inverse, {u * 2 - 1, v * 2 - 1, 1});
                    const float scale = core::length(b - a)
                        / (core::transformPoint(view, b).z - core::transformPoint(view, a).z);
                    for (unsigned z = 0; z != grid.depth; ++z) {
                        const float distance =
                            0.1f * (std::pow(500.0f, float(z + 1) / grid.depth) - 1) * scale;
                        const float t = std::exp(-medium.density * distance);
                        const std::array<float, 4> expected{
                            0.6f * 12 / (4 * 3.141592653589793f) * (1 - t),
                            0.7f * 12 / (4 * 3.141592653589793f) * (1 - t),
                            0.8f * 12 / (4 * 3.141592653589793f) * (1 - t),
                            t};
                        std::array<float, 4> observed;
                        std::memcpy(
                            observed.data(), &actual[(z * grid.height + y) * grid.width + x], 16);
                        for (unsigned c = 0; c != 4; ++c) {
                            assert(std::isfinite(observed[c]));
                            maxIntegralError =
                                std::max(maxIntegralError, std::fabs(observed[c] - expected[c]));
                        }
                    }
                }
            semantics::TextureUploadPackedMip mip;
            mip.range = defaultTextureSubresourceRange(graph.texture(result.color).desc());
            mip.extent = {64, 48, 1};
            mip.packedBytesPerRow = 64 * 8;
            mip.rowCount = 48;
            const auto pixels = test::MetalNativeTextureReadbackFixture::read(
                graph.texture(result.color), std::array{mip}, mip.packedBytesPerRow * mip.rowCount);
            for (unsigned y = 0; y != 48; ++y)
                for (unsigned x = 0; x != 64; ++x) {
                    const float sx = std::clamp(
                        (x + 0.5f) / 64 * grid.width - 0.5f, 0.0f, float(grid.width - 1));
                    const float sy = std::clamp(
                        (y + 0.5f) / 48 * grid.height - 0.5f, 0.0f, float(grid.height - 1));
                    const unsigned ix = unsigned(sx), iy = unsigned(sy);
                    const float fx = sx - ix, fy = sy - iy;
                    std::array<float, 4> fog{};
                    for (unsigned dy = 0; dy != 2; ++dy)
                        for (unsigned dx = 0; dx != 2; ++dx) {
                            const auto at = ((grid.depth - 1) * grid.height
                                                + std::min(iy + dy, grid.height - 1))
                                    * grid.width
                                + std::min(ix + dx, grid.width - 1);
                            std::array<float, 4> value;
                            std::memcpy(value.data(), &actual[at], 16);
                            const float weight = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy);
                            for (unsigned c = 0; c != 4; ++c)
                                fog[c] += value[c] * weight;
                        }
                    const std::array<float, 3> background{0.3f, 0.5f, 0.8f};
                    for (unsigned c = 0; c != 3; ++c) {
                        std::uint16_t bits;
                        std::memcpy(
                            &bits, pixels.data() + y * mip.packedBytesPerRow + x * 8 + c * 2, 2);
                        const float value = core::halfToFloat(bits);
                        assert(std::isfinite(value));
                        maxCompositeError = std::max(maxCompositeError,
                            std::fabs(value - (background[c] * fog[3] + fog[c])));
                    }
                }
            if (output && frame == 2) {
                std::ofstream file(output, std::ios::binary);
                file.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
                assert(file);
            }
        }
        assert(maxIntegralError < 0.0002f && maxCompositeError < 0.001f);
        std::cout << "VolumetricFog/1 Metal temporal=" << temporal << " cells=" << grid.cells
                  << " integralError=" << maxIntegralError
                  << " compositeError=" << maxCompositeError << '\n';
    }

private:
    core::UniquePtr<GpuDevice> device_;
};
} // namespace

int main(int argc, char** argv)
{
    @autoreleasepool {
        FogMetalFixture fixture;
        fixture.run(false, nullptr);
        fixture.run(true, argc > 1 ? argv[1] : nullptr);
    }
}
