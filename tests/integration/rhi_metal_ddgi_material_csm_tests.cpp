#include "renderer/gi/ddgi_renderer.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_native_resources.h"
#include "tests/fixtures/capability_override_device.h"
#include "tests/fixtures/flat_terrain_surface.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
using namespace arp;
using namespace arp::rhi;
using Samples = std::array<std::array<float, 4>, 8>;

core::UniquePtr<GpuTexture> texture(GpuDevice& device, bool depth, bool alternate)
{
    auto result = device.createTexture({"DdgiConsumerInput",
        {4u, 4u, 1u},
        depth ? TextureFormat::D32Float : TextureFormat::Rgba16Float,
        TextureUsage::Sampled | TextureUsage::CopyDst,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined});
    assert(result);
    std::array<std::byte, 1024> bytes{};
    const float depthValue = alternate ? 0.f : 1.f;
    const std::array<std::uint16_t, 4> color = alternate
        ? std::array<std::uint16_t, 4>{0x3a00, 0x3400, 0x3800, 0x3c00}
        : std::array<std::uint16_t, 4>{0x3400, 0x3800, 0x3a00, 0x3c00};
    for (unsigned y = 0; y < 4u; ++y)
        for (unsigned x = 0; x < 4u; ++x) {
            if (depth)
                std::memcpy(bytes.data() + y * 256u + x * 4u, &depthValue, sizeof(depthValue));
            else
                std::memcpy(bytes.data() + y * 256u + x * 8u, color.data(), sizeof(color));
        }
    const TextureSubresourceRange range{
        0u, 1u, 0u, 1u, depth ? TextureAspect::Depth : TextureAspect::Color};
    auto encoder = device.createCommandEncoder({"DdgiConsumerInitialize"});
    assert(encoder->transition(*result, range, ResourceState::Undefined, ResourceState::CopyDst)
            .success);
    auto transfer = encoder->beginTransferPass({"DdgiConsumerInitialize"});
    UploadTextureDesc upload;
    upload.destinationTexture = result->id();
    upload.extent = result->desc().extent;
    upload.format = result->desc().format;
    upload.bytesPerRow = 256u;
    upload.rowsPerImage = 4u;
    upload.subresources = range;
    upload.pixelData = bytes.data();
    upload.pixelDataSize = bytes.size();
    upload.backendOwnedStaging = true;
    upload.frameSerial = 1u;
    upload.resourceGeneration = 1u;
    assert(transfer && transfer->uploadTexture(upload).success && transfer->end().success);
    assert(encoder->transition(*result, range, ResourceState::CopyDst, ResourceState::ShaderRead)
            .success);
    auto commands = encoder->finish();
    auto done = device.createFence();
    assert(device.queue().submit(*commands, done.get()).hasAcceptedWork());
    done->wait();
    return result;
}

bool verify(GpuDevice& device, unsigned category)
{
    tests::CapabilityOverrideDevice observed(device, nullptr);
    observed.observeBuffer("DdgiProbeTraceRecords", 0u);
    observed.observeBuffer("DdgiProbeState1", 1u);
    renderer::DdgiRenderer ddgi(observed);
    assert(
        ddgi.create(ARP_SHADER_DIR "/ddgi.slang",
                {core::makeShared<tests::FlatTerrainSurface>(), 64u, 64u},
                1u,
                1u,
                1u,
                4u,
                4u)
            .success);
    std::array<renderer::DdgiSceneBvhTriangle, 2> triangles{};
    const float vertices[2][3][3]{
        {{-64, 1, -64}, {-64, 1, 64}, {64, 1, -64}}, {{64, 1, 64}, {64, 1, -64}, {-64, 1, 64}}};
    for (unsigned i = 0; i < 2u; ++i) {
        std::memcpy(triangles[i].v0, vertices[i][0], sizeof(triangles[i].v0));
        std::memcpy(triangles[i].v1, vertices[i][1], sizeof(triangles[i].v1));
        std::memcpy(triangles[i].v2, vertices[i][2], sizeof(triangles[i].v2));
        triangles[i].materialIdx = category;
        triangles[i].stableId = i + 1u;
    }
    renderer::DdgiSceneBvhInput input{triangles.data(), 2u};
    input.geometryRevision = 1u;
    const auto bvh = ddgi.updateSceneBvh(input);
    assert(bvh.success);
    auto colorA = texture(device, false, false), colorB = texture(device, false, true);
    auto lit = texture(device, true, false), shadowed = texture(device, true, true);
    const auto view = [&](const GpuTexture& source, bool depth) {
        return device.createTextureView(source,
            {"DdgiConsumerView",
                source.desc().format,
                TextureUsage::Sampled,
                TextureViewDimension::Texture2D,
                0u,
                1u,
                0u,
                1u,
                depth ? TextureAspect::Depth : TextureAspect::Color});
    };
    auto colorViewA = view(*colorA, false), colorViewB = view(*colorB, false);
    auto litView = view(*lit, true), shadowView = view(*shadowed, true);
    SamplerDesc samplerDesc{"DdgiConsumerSampler",
        SamplerFilter::Nearest,
        SamplerFilter::Nearest,
        SamplerAddressMode::ClampToEdge,
        SamplerAddressMode::ClampToEdge,
        SamplerAddressMode::ClampToEdge};
    samplerDesc.mipFilter = SamplerMipFilter::Nearest;
    auto sampler = device.createSampler(samplerDesc);
    assert(colorViewA && colorViewB && litView && shadowView && sampler);
    renderer::DdgiCsmShadowDesc csm{};
    for (unsigned cascade = 0; cascade < 3u; ++cascade) {
        // Diagonal fixture is deliberately layout-insensitive; no matrix ABI claim.
        for (unsigned axis = 0; axis < 3u; ++axis)
            csm.lightViewProjection[cascade][axis * 5u] = 0.01f;
        csm.lightViewProjection[cascade][15] = 1.f;
        csm.atlasUvTransforms[cascade][0] = csm.atlasUvTransforms[cascade][1] = 1.f;
    }
    csm.shadowParams[0] = 1.f;
    csm.shadowParams[2] = 1.f;
    csm.shadowParams[3] = 4.f;
    assert(ddgi.setCsmShadow(csm).success);
    renderer::DdgiPushConstants pc{};
    pc.probeGridOriginHysteresis[1] = 3.f;
    for (unsigned axis = 0; axis < 3u; ++axis) {
        pc.probeGridSpacingStrength[axis] = 1.f;
        pc.probeCountRayCount[axis] = 1.f;
        pc.directionalLightRadiance[axis] = 1.f;
    }
    pc.directionalLightRadiance[3] = 1.f;
    pc.probeCountRayCount[3] = 8.f;
    pc.probeTextureSizeBounceMaxRay[3] = 8.f;
    pc.sunDirectionSkyAmbient[1] = 1.f;
    pc.updateParams[0] = 1.f;
    const auto counts = bvh.shaderParameters();
    std::copy(counts.begin(), counts.end(), pc.sceneBvhParams);
    const auto record = [&](const GpuTextureView& albedo,
                            const GpuTextureView& shadow,
                            unsigned slot) {
        pc.scrollOffsetFrame[3] = static_cast<float>(slot);
        ddgi.setMaterialAlbedo(category, albedo, *sampler);
        ddgi.setCsmShadowMap(shadow, *sampler);
        assert(ddgi.prepareProbeUpdateSchedule(pc, 1u).success);
        auto encoder = device.createCommandEncoder({"ProductionDdgiTrace"});
        auto* states = observed.observedBuffer(1u);
        assert(states
            && encoder->transition(*states, states->state(), ResourceState::ShaderWrite).success);
        render_graph::RenderContext context(device, *encoder);
        ddgi.recordProbeTracePass(context, *lit, *colorA, pc, slot);
        auto* traces = observed.observedBuffer(0u);
        assert(traces
            && encoder->transition(*traces, ResourceState::ShaderWrite, ResourceState::ShaderRead)
                .success);
        return encoder->finish();
    };
    const auto fence = [&] {
        auto owner = core::makeShared<core::UniquePtr<GpuFence>>(device.createFence());
        return std::shared_ptr<GpuFence>(owner, owner->get());
    };
    const auto read = [&] {
        Samples result{};
        auto* traces = observed.observedBuffer(0u);
        core::pmr::String diagnostic;
        assert(rhi_metal::readMetalRhiNativeBuffer(rhi_metal::metalRhiNativePayloadFor(*traces),
            result.data(),
            sizeof(result),
            0u,
            sizeof(result),
            diagnostic));
        return result;
    };
    const auto sample =
        [&](const GpuTextureView& albedo, const GpuTextureView& shadow, unsigned slot = 0u) {
            auto commands = record(albedo, shadow, slot);
            auto done = fence();
            assert(device.queue().submit(*commands, done.get()).hasAcceptedWork());
            assert(ddgi.acceptPreparedInput(ddgi.preparedHistoryToken(), done));
            done->wait();
            return read();
        };
    const auto a = sample(*colorViewA, *litView);
    const auto b = sample(*colorViewB, *litView);
    const auto shadow = sample(*colorViewB, *shadowView);
    const auto recovered = sample(*colorViewA, *litView);
    bool correct = true;
    const float original[3]{0.25f, 0.5f, 0.75f}, changed[3]{0.75f, 0.25f, 0.5f},
        terrain[3]{0.35f, 0.55f, 0.25f};
    for (unsigned ray = 0; ray < 8u; ++ray) {
        for (unsigned channel = 0; channel < 3u; ++channel) {
            const bool hit = ray >= 5u;
            const float expectedA = !hit ? 0.f
                : category == 0u         ? original[channel] * 0.35f + terrain[channel] * 0.65f
                                         : original[channel];
            const float expectedB = !hit ? 0.f
                : category == 0u         ? changed[channel] * 0.35f + terrain[channel] * 0.65f
                                         : changed[channel];
            const float expectedShadow = ray == 5u || ray == 6u ? 0.f : expectedB;
            correct = std::abs(a[ray][channel] - expectedA) < 1.e-5f
                && std::abs(b[ray][channel] - expectedB) < 1.e-5f
                && std::abs(shadow[ray][channel] - expectedShadow) < 1.e-5f
                && recovered[ray][channel] == a[ray][channel] && correct;
        }
        const float expectedDistance =
            ray < 5u ? 8.f : 2.f / -(1.f - 2.f * (float(ray) + 0.5f) / 8.f);
        for (const auto* samples : {&a, &b, &shadow, &recovered})
            correct = std::isfinite((*samples)[ray][3])
                && std::abs((*samples)[ray][3] - expectedDistance) < 1.e-4f && correct;
        std::fprintf(stderr,
            "category=%u ray=%u A=(%g,%g,%g) B=(%g,%g,%g) shadow=(%g,%g,%g) distance=%g\n",
            category,
            ray,
            a[ray][0],
            a[ray][1],
            a[ray][2],
            b[ray][0],
            b[ray][1],
            b[ray][2],
            shadow[ray][0],
            shadow[ray][1],
            shadow[ray][2],
            a[ray][3]);
    }
    for (unsigned firstSlot : {0u, 1u}) {
        const auto expectedA = sample(*colorViewA, *litView, firstSlot);
        const auto expectedB = sample(*colorViewB, *shadowView, 1u - firstSlot);
        auto gate = device.createTimelineSemaphore(0u);
        auto first = fence(), second = fence();
        assert(gate && first && second);
        auto commandsA = record(*colorViewA, *litView, firstSlot);
        assert(device.queue()
                .submit(*commandsA, gate.get(), 1u, nullptr, 0u, first.get())
                .hasAcceptedWork());
        assert(ddgi.acceptPreparedInput(ddgi.preparedHistoryToken(), first));
        auto commandsB = record(*colorViewB, *shadowView, 1u - firstSlot);
        assert(device.queue()
                .submit(*commandsB, gate.get(), 2u, nullptr, 0u, second.get())
                .hasAcceptedWork());
        assert(ddgi.acceptPreparedInput(ddgi.preparedHistoryToken(), second));
        const bool bothPending = !first->isSignaled() && !second->isSignaled();
        gate->signal(1u);
        first->wait();
        const auto actualA = read();
        const bool secondPending = !second->isSignaled();
        gate->signal(2u);
        second->wait();
        const auto actualB = read();
        const bool matched = actualA == expectedA && actualB == expectedB;
        correct = bothPending && secondPending && matched && correct;
        std::fprintf(stderr,
            "production DDGI category=%u firstSlot=%u bothPending=%d secondPending=%d matched=%d\n",
            category,
            firstSlot,
            bothPending,
            secondPending,
            matched);
    }
    std::fprintf(stderr, "production DDGI category=%u correct=%d\n", category, correct);
    return correct;
}
} // namespace

int main()
{
    rhi_metal::MetalRhiDeviceDesc desc;
    desc.capabilityContract = rhi::semantics::nativeBackendCapabilityContract(Backend::Metal);
    desc.capabilityContract.queriedFromNativeDevice = true;
    desc.capabilityContract.queriedExtendedFeatures = true;
    desc.memoryBudgetBytes = 64u * 1024u * 1024u;
    desc.uploadStagingPageSizeBytes = 128u * 1024u;
    desc.uploadStagingMaxPageCount = 8u;
    auto device = rhi_metal::createMetalRhiDevice(desc);
    assert(device);
    bool correct = true;
    for (unsigned category = 0; category < 4u; ++category)
        correct = verify(*device, category) && correct;
    device->waitIdle();
    const bool retired = device->memoryStats().allocatedBytes == 0u;
    std::fprintf(stderr, "production DDGI consumer correct=%d retired=%d\n", correct, retired);
    return correct && retired ? 0 : 1;
}
