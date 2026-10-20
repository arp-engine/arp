#include "rhi/backends/null/null_backend.h"
#include "tests/fixtures/capability_override_device.h"
#include "tests/fixtures/ddgi_uniform_inputs.h"
#include "tests/fixtures/flat_terrain_surface.h"

#include <cassert>
#include <cstdio>
#include <limits>
#include <string_view>

namespace {
using namespace arp;

renderer::RendererResult create(renderer::DdgiRenderer& ddgi)
{
    return ddgi.create(ARP_SHADER_DIR "/ddgi.slang",
        {core::makeShared<tests::FlatTerrainSurface>(), 2u, 2u},
        2u,
        2u,
        2u,
        4u,
        4u);
}

std::shared_ptr<rhi::GpuFence> fence(rhi::GpuDevice& device)
{
    auto owner = core::makeShared<core::UniquePtr<rhi::GpuFence>>(device.createFence());
    return std::shared_ptr<rhi::GpuFence>(owner, owner->get());
}

void ownership(bool sky)
{
    auto device = rhi::createNullDevice();
    tests::CapabilityOverrideDevice proxy(*device, nullptr);
    renderer::DdgiRenderer ddgi(proxy);
    tests::DdgiUniformInputs a(0.25f), b(0.75f);
    renderer::DdgiPushConstants pc{};
    for (unsigned axis = 0; axis < 3; ++axis)
        pc.probeGridSpacingStrength[axis] = 1;
    pc.updateParams[0] = 2;
    assert(!a.write(ddgi, sky).success);
    assert(!ddgi.prepareProbeUpdateSchedule(pc, 1u).success);
    assert(create(ddgi).success);
    const auto initializedWrites = proxy.bufferWriteAttempts();
    assert(ddgi.setSkyEnvironment({}).success && ddgi.setCsmShadow({}).success);
    assert(proxy.bufferWriteAttempts() == initializedWrites);

    for (unsigned firstSlot : {0u, 1u}) {
        assert(a.write(ddgi, sky).success);
        pc.scrollOffsetFrame[3] = static_cast<float>(firstSlot);
        assert(ddgi.prepareProbeUpdateSchedule(pc, 1u).success);
        auto first = fence(*device), second = fence(*device);
        assert(ddgi.acceptPreparedInput(ddgi.preparedHistoryToken(), first));
        pc.scrollOffsetFrame[3] = static_cast<float>(1u - firstSlot);
        assert(ddgi.prepareProbeUpdateSchedule(pc, 1u).success);
        const auto token = ddgi.preparedHistoryToken();
        assert(ddgi.acceptPreparedInput(token, second));
        const auto writes = proxy.bufferWriteAttempts();
        const auto allocations = proxy.bufferCreateAttempts();
        const auto schedule = ddgi.acceptedProbeScheduleGeneration();
        const auto history = ddgi.currentReadIndex();
        for (unsigned completed = 0; completed < 2; ++completed) {
            assert(a.write(ddgi, sky).success);
            const auto rejected = b.write(ddgi, sky);
            assert(!rejected.success
                && rejected.diagnostics == tests::DdgiUniformInputs::busyDiagnostic(sky));
            assert(proxy.bufferWriteAttempts() == writes
                && proxy.bufferCreateAttempts() == allocations);
            assert(ddgi.preparedHistoryToken().generation == token.generation);
            assert(ddgi.acceptedProbeScheduleGeneration() == schedule
                && ddgi.currentReadIndex() == history);
            if (completed == 0)
                first->signal(); // Null-only explicit completion.
        }
        second->signal();
        assert(b.write(ddgi, sky).success);
        assert(proxy.bufferWriteAttempts() == writes + 1u);
        assert(ddgi.preparedHistoryToken().generation == 0u);
        assert(!ddgi.acceptPreparedInput(token, second));
        assert(ddgi.prepareProbeUpdateSchedule(pc, 1u).success);
        std::fprintf(stderr,
            "Null shared uniform sky=%d firstSlot=%u two-readers=1 resumed=1\n",
            sky,
            firstSlot);
    }

    for (bool afterWrite : {false, true}) {
        for (bool oldValue : {false, true}) {
            assert(a.write(ddgi, sky).success);
            assert(ddgi.prepareProbeUpdateSchedule(pc, 1u).success);
            const auto token = ddgi.preparedHistoryToken();
            const auto schedule = ddgi.acceptedProbeScheduleGeneration();
            const auto history = ddgi.currentReadIndex();
            auto invalid = b;
            if (sky)
                invalid.sky.horizonRadiance[0] = std::numeric_limits<float>::quiet_NaN();
            else
                invalid.csm.lightViewProjection[0][0] = std::numeric_limits<float>::quiet_NaN();
            const auto writes = proxy.bufferWriteAttempts();
            assert(!invalid.write(ddgi, sky).success);
            assert(proxy.bufferWriteAttempts() == writes && ddgi.isPreparedInput(token));
            proxy.failBufferWrites(tests::DdgiUniformInputs::label(sky), afterWrite);
            const auto failed = b.write(ddgi, sky);
            assert(!failed.success
                && failed.diagnostics.find(afterWrite ? "TEST-BUFFER-UPLOAD-FAILED-AFTER-WRITE"
                                                      : "TEST-BUFFER-UPLOAD-FAILED")
                    != std::string_view::npos);
            assert(ddgi.preparedHistoryToken().generation == 0u && !ddgi.isPreparedInput(token));
            assert(ddgi.acceptedProbeScheduleGeneration() == schedule
                && ddgi.currentReadIndex() == history);
            // Updating the other domain cannot repair this failed physical block.
            assert(b.write(ddgi, !sky).success);
            const auto rejected = ddgi.prepareProbeUpdateSchedule(pc, 1u);
            assert(!rejected.success
                && rejected.diagnostics == tests::DdgiUniformInputs::invalidDiagnostic(sky));
            // Even the old cache key must attempt a real upload while invalid.
            assert(!a.write(ddgi, sky).success);
            proxy.failBufferWrites({});
            const auto beforeRecovery = proxy.bufferWriteAttempts();
            assert((oldValue ? a : b).write(ddgi, sky).success);
            assert(proxy.bufferWriteAttempts() == beforeRecovery + 1u);
            assert(ddgi.prepareProbeUpdateSchedule(pc, 1u).success);
            assert(!ddgi.isPreparedInput(token));
            std::fprintf(stderr,
                "Null shared uniform sky=%d afterWrite=%d oldValue=%d recovered=1\n",
                sky,
                afterWrite,
                oldValue);
        }
    }
    ddgi.destroy();
    assert(!a.write(ddgi, sky).success && !ddgi.prepareProbeUpdateSchedule(pc, 1u).success);
    assert(create(ddgi).success);
    assert(a.write(ddgi, sky).success && ddgi.prepareProbeUpdateSchedule(pc, 1u).success);
    ddgi.destroy();
    device->waitIdle();
    assert(device->memoryStats().allocatedBytes == 0u);
}

void failedCreation(bool sky)
{
    auto device = rhi::createNullDevice();
    tests::CapabilityOverrideDevice proxy(*device, nullptr);
    renderer::DdgiRenderer ddgi(proxy);
    tests::DdgiUniformInputs input(0.25f);
    for (unsigned failure = 0; failure < 3; ++failure) {
        if (failure == 0)
            proxy.failBufferCreation(tests::DdgiUniformInputs::label(sky));
        else
            proxy.failBufferWrites(tests::DdgiUniformInputs::label(sky), failure == 2);
        assert(!create(ddgi).success);
        assert(!input.write(ddgi, sky).success);
        proxy.failBufferCreation({});
        proxy.failBufferWrites({});
        assert(create(ddgi).success && input.write(ddgi, sky).success);
        ddgi.destroy();
        device->waitIdle();
        assert(device->memoryStats().allocatedBytes == 0u);
    }
    std::fprintf(
        stderr, "Null shared uniform sky=%d create-failures=3 recreated=1 retired=1\n", sky);
}
} // namespace

int main()
{
    for (bool sky : {true, false}) {
        ownership(sky);
        failedCreation(sky);
    }
}
