#include "rhi/backends/null/null_backend.h"
#include "tests/fixtures/capability_override_device.h"
#include "tests/fixtures/terrain_vegetation_delayed_queue.h"
#include "tests/fixtures/terrain_vegetation_gpu_upload.h"

#include <cstdio>

namespace {
using namespace arp;
using Upload = renderer::TerrainVegetationGpuUpload;

void admissionContracts(rhi::GpuDevice& device)
{
    auto caps = device.capabilities();
    assert(!Upload::plan(caps).executable);
    auto d = Upload::plan(caps, true);
    assert(d.executable && d.validatorOnly && d.mode == rhi::CapabilityMode::Equivalent);
    for (auto backend : {rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
        caps.backend = backend;
        caps.computeResources.mode = rhi::CapabilityMode::Native;
        d = Upload::plan(caps);
        assert(d.executable && !d.validatorOnly && d.mode == rhi::CapabilityMode::Native);
        const auto refuses = [&](auto mutate) {
            auto missing = caps;
            mutate(missing);
            const auto denied = Upload::plan(missing);
            assert(!denied.executable && denied.mode == rhi::CapabilityMode::Degraded);
        };
        refuses([](auto& c) {
            c.memory.deviceLocal = false;
        });
        refuses([](auto& c) {
            c.memory.hostVisible = false;
        });
        refuses([](auto& c) {
            c.explicitFeatures.explicitBarriers = false;
        });
        refuses([](auto& c) {
            c.features.storageBufferBinding = false;
        });
        refuses([](auto& c) {
            c.core.graphics = false;
        });
        refuses([](auto& c) {
            c.core.compute = false;
        });
        refuses([](auto& c) {
            c.features.computeDispatch = false;
        });
        refuses([](auto& c) {
            c.computeResources.mode = rhi::CapabilityMode::DiagnosticFailure;
        });
        refuses([](auto& c) {
            c.computeResources.maxStorageBufferBindingSizeBytes = 0u;
        });
        refuses([](auto& c) {
            c.computeResources.maxStorageBufferBindingsPerStage = 0u;
        });
        // NativeSync includes semaphore ordering; standalone completion does not.
        auto standalone = caps;
        standalone.features.nativeSync = false;
        assert(Upload::plan(standalone).executable);
    }
    caps.backend = rhi::Backend::WebGL;
    assert(!Upload::plan(caps).executable);
    caps.backend = static_cast<rhi::Backend>(255);
    assert(!Upload::plan(caps).executable);
}

void preparationContracts(rhi::GpuDevice& device)
{
    auto desc = tests::terrainVegetationUploadDescFixture();
    auto source = tests::terrainVegetationDrawFixture();
    Upload ticket;
    tests::CapabilityOverrideDevice proxy(device, nullptr);
    assert(!ticket.prepare(proxy, source, desc).success && proxy.bufferCreateAttempts() == 0u);
    desc.validatorOptIn = true;
    tests::CapabilityOverrideDevice denied(device, [](auto& caps) {
        caps.memory.hostVisible = false;
    });
    assert(!ticket.prepare(denied, source, desc).success && denied.bufferCreateAttempts() == 0u);
    tests::CapabilityOverrideDevice shortRange(device, [](auto& caps) {
        caps.computeResources.maxStorageBufferBindingSizeBytes = 64u;
    });
    assert(ticket.prepare(shortRange, source, desc).diagnostics == "VEGETATION-GPU-BUFFER-RANGE");
    assert(shortRange.bufferCreateAttempts() == 0u);
    const auto refuses = [&](auto mutate) {
        auto shortBudget = desc;
        mutate(shortBudget);
        const auto attempts = proxy.bufferCreateAttempts();
        assert(!ticket.prepare(proxy, source, shortBudget).success);
        assert(proxy.bufferCreateAttempts() == attempts);
        assert(ticket.state() == Upload::State::Empty && !ticket.snapshot());
        assert(!desc.liveGenerations->used() && !desc.liveInstances->used());
    };
    refuses([](auto& d) {
        d.contractVersion = 2u;
    });
    refuses([](auto& d) {
        d.maxTiles = 2u;
    });
    refuses([](auto& d) {
        d.maxInstances = 3u;
    });
    refuses([](auto& d) {
        d.maxPrototypes = 2u;
    });
    refuses([](auto& d) {
        d.maxCopyCommands = 3u;
    });
    refuses([](auto& d) {
        d.maxCopyBytes = 63u;
    });
    refuses([](auto& d) {
        d.maxCpuBytes = 1u;
    });
    refuses([](auto& d) {
        d.liveInstances.reset();
    });
    refuses([](auto& d) {
        d.liveGenerations = core::makeShared<core::AtomicCapacityBudget>(0u);
    });
    refuses([](auto& d) {
        d.liveInstances = core::makeShared<core::AtomicCapacityBudget>(3u);
    });
    auto forged = core::makeShared<renderer::TerrainVegetationDrawSnapshot>(*source);
    forged->tiles[0].prototypes[0].instances[0].positionScale[0] += 100.0f;
    assert(ticket.prepare(proxy, forged, desc).diagnostics == "VEGETATION-GPU-RECORDS-MISMATCH");
    proxy.failBufferCreation("TerrainVegetation.WorldInstances");
    assert(!ticket.prepare(proxy, source, desc).success);
    proxy.failBufferCreation("");
    proxy.failBufferWrites("TerrainVegetation.WorldUpload", true);
    assert(!ticket.prepare(proxy, source, desc).success);
    proxy.failBufferWrites("");
    proxy.failFenceCreation(true);
    const auto beforeFenceFailure = proxy.bufferCreateAttempts();
    assert(
        ticket.prepare(proxy, source, desc).diagnostics == "VEGETATION-GPU-COMPLETION-UNAVAILABLE");
    assert(beforeFenceFailure == proxy.bufferCreateAttempts());
    proxy.failFenceCreation(false);
    assert(!desc.liveGenerations->used() && !desc.liveInstances->used());

    assert(ticket.prepare(proxy, source, desc).success);
    assert(!ticket.poll() && !ticket.snapshot());
    assert(!ticket.prepare(proxy, source, desc).success);
    assert(desc.liveGenerations->used() == 1u && desc.liveInstances->used() == 4u);
    assert(ticket.cancel());
    assert(!desc.liveGenerations->used() && !desc.liveInstances->used());

    assert(ticket.prepare(proxy, source, desc).success && ticket.submit().success);
    const auto beforeCompletedPoll = core::defaultCpuAllocator().stats();
    assert(ticket.poll() && ticket.decision().validatorOnly);
    const auto afterCompletedPoll = core::defaultCpuAllocator().stats();
    assert(beforeCompletedPoll.allocationCount == afterCompletedPoll.allocationCount);
    auto reader = ticket.snapshot();
    tests::checkTerrainVegetationGpuSnapshot(*reader, source);
    assert(reader->validatorOnly());
    const auto& identity = *reader->source()->source;
    assert(identity.terrainResourceId == tests::vegetationDrawTerrainId);
    assert(identity.catalogResourceId == tests::vegetationDrawCatalogId);
    assert(identity.terrainResourceGeneration == 11u && identity.catalogResourceGeneration == 17u);
    assert(identity.terrainGeneration == 23u && identity.translation.x == 100.0f);
    assert(ticket.cancel() && desc.liveGenerations->used() == 1u);
    Upload second, third;
    assert(second.prepare(proxy, source, desc).success);
    assert(!third.prepare(proxy, source, desc).success);
    assert(second.cancel() && desc.liveInstances->used() == 4u);
    std::weak_ptr<const renderer::TerrainVegetationDrawSnapshot> retained = source;
    forged.reset();
    source.reset();
    assert(!retained.expired());
    reader.reset();
    assert(retained.expired() && !desc.liveGenerations->used() && !desc.liveInstances->used());

    auto emptySource = tests::terrainVegetationDrawSourceFixture();
    for (auto& tile : emptySource->tiles)
        tile.instances.clear();
    emptySource->instanceCount = 0u;
    renderer::TerrainVegetationDrawPreparation preparation;
    assert(preparation.prepare(emptySource, {3u, 4u}).success);
    const auto attempts = proxy.bufferCreateAttempts();
    assert(ticket.prepare(proxy, preparation.snapshot(), desc).success && ticket.poll());
    assert(proxy.bufferCreateAttempts() == attempts && !ticket.submit().success);
    assert(ticket.cancel() && !desc.liveInstances->used());
}

void submissionContracts(rhi::GpuDevice& device)
{
    auto desc = tests::terrainVegetationUploadDescFixture();
    desc.validatorOptIn = true;
    const auto source = tests::terrainVegetationDrawFixture();
    for (unsigned mode = 0u; mode < 4u; ++mode) {
        tests::DelayedValidatorQueue queue;
        queue.rejected = mode == 1u;
        queue.acceptedError = mode == 2u;
        queue.throws = mode == 3u;
        tests::CapabilityOverrideDevice proxy(device, nullptr);
        proxy.overrideQueue(&queue);
        Upload ticket;
        assert(ticket.prepare(proxy, source, desc).success);
        try {
            const auto result = ticket.submit();
            assert(result.success == (mode == 0u));
        } catch (const core::DiagnosticError&) {
            assert(mode == 3u);
        }
        assert(!ticket.snapshot());
        if (mode == 1u) {
            assert(ticket.state() == Upload::State::Rejected && ticket.cancel());
            continue;
        }
        assert(ticket.state()
            == (mode == 0u ? Upload::State::AcceptedPending : Upload::State::AcceptedError));
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0u; i < 8u; ++i)
            assert(!ticket.poll() && !ticket.cancel());
        const auto after = core::defaultCpuAllocator().stats();
        assert(
            before.allocationCount == after.allocationCount && before.liveBytes == after.liveBytes);
        // No CPU frame counter or external fence can advance this ticket.
        assert(desc.liveInstances->used() == 4u);
        assert(!ticket.drainAndClear({}).success && !ticket.snapshot());
        queue.complete();
        assert(ticket.poll() == (mode == 0u));
        assert(bool(ticket.snapshot()) == (mode == 0u));
        assert(ticket.cancel() && !desc.liveInstances->used());
    }
}

} // namespace

int main()
{
    auto device = arp::rhi::createNullDevice();
    assert(device);
    admissionContracts(*device);
    preparationContracts(*device);
    submissionContracts(*device);
    std::puts(
        "Terrain vegetation GPU upload: admission/identity/bounds/rollback/completion/reader contracts PASS (Null validator only)");
}
