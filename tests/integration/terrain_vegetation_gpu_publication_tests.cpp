#include "renderer/terrain_vegetation_gpu_publication.h"
#include "rhi/backends/null/null_backend.h"
#include "tests/fixtures/capability_override_device.h"
#include "tests/fixtures/terrain_vegetation_delayed_queue.h"
#include "tests/fixtures/terrain_vegetation_gpu_upload.h"

#include <cstdio>

namespace {
using namespace arp;
using Publication = renderer::TerrainVegetationGpuPublication;
using Status = Publication::Status;

void identityContracts()
{
    const auto source = tests::terrainVegetationDrawSourceFixture();
    const auto different = [&](auto mutate) {
        auto copy = core::makeShared<renderer::TerrainVegetationSnapshot>(*source);
        assert(copy->equivalent(*source));
        mutate(*copy);
        assert(!copy->equivalent(*source) && !source->equivalent(*copy));
    };
    different([](auto& s) {
        s.terrainResourceId.back() = '2';
    });
    different([](auto& s) {
        s.catalogResourceId.back() = '2';
    });
    different([](auto& s) {
        ++s.terrainResourceGeneration;
    });
    different([](auto& s) {
        ++s.catalogResourceGeneration;
    });
    different([](auto& s) {
        ++s.terrainGeneration;
    });
    different([](auto& s) {
        s.catalog = core::makeShared<resource::VegetationCatalogResourceData>(*s.catalog);
    });
    different([](auto& s) {
        s.catalog = std::shared_ptr<const resource::VegetationCatalogResourceData>(
            s.catalog.get(), [](auto*) {});
    });
    different([](auto& s) {
        s.translation.x += 1;
    });
    different([](auto& s) {
        s.translation.y += 1;
    });
    different([](auto& s) {
        s.translation.z += 1;
    });
    different([](auto& s) {
        ++s.instanceCount;
    });
    different([](auto& s) {
        s.tiles.pop_back();
    });
    different([](auto& s) {
        ++s.tiles.back().coord.x;
    });
    different([](auto& s) {
        ++s.tiles.back().coord.z;
    });
    different([](auto& s) {
        s.tiles.back().instances.pop_back();
    });
    different([](auto& s) {
        ++s.tiles.back().instances[0].prototypeId;
    });
    different([](auto& s) {
        s.tiles.back().instances[0].position.x += 1;
    });
    different([](auto& s) {
        s.tiles.back().instances[0].position.y += 1;
    });
    different([](auto& s) {
        s.tiles.back().instances[0].position.z += 1;
    });
    for (std::size_t i = 0; i < 4; ++i)
        different([i](auto& s) {
            s.tiles.back().instances[0].rotation[i] += 1;
        });
    different([](auto& s) {
        s.tiles.back().instances[0].scale.x += 1;
    });
    different([](auto& s) {
        s.tiles.back().instances[0].scale.y += 1;
    });
    different([](auto& s) {
        s.tiles.back().instances[0].scale.z += 1;
    });
    different([](auto& s) {
        ++s.tiles.back().instances[0].variationSeed;
    });
}

void publishWithoutAllocation(Publication& owner)
{
    const auto before = core::defaultCpuAllocator().stats();
    owner.publishPrepared();
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.allocationCount == after.allocationCount && before.liveBytes == after.liveBytes);
}

void publicationContracts(rhi::GpuDevice& device)
{
    auto desc = tests::terrainVegetationUploadDescFixture();
    desc.validatorOptIn = true;
    tests::DelayedValidatorQueue queue;
    tests::CapabilityOverrideDevice proxy(device, nullptr);
    proxy.overrideQueue(&queue);
    Publication owner(proxy, desc);
    auto source = tests::terrainVegetationDrawSourceFixture();
    auto copy = core::makeShared<renderer::TerrainVegetationSnapshot>(*source);
    assert(owner.stage(source).status == Status::Pending && !owner.snapshot());
    assert(owner.stage(copy).status == Status::Pending);
    assert(proxy.bufferCreateAttempts() == 6u);
    auto next = core::makeShared<renderer::TerrainVegetationSnapshot>(*source);
    ++next->terrainResourceGeneration;
    next->tiles.back().instances[0].position.x += 13.0f;
    assert(owner.stage(next).diagnostics == "VEGETATION-GPU-PUBLICATION-BUSY");
    queue.complete();
    assert(owner.stage(copy).status == Status::Ready && owner.preparedSource() == source);
    assert(!owner.snapshot()); // A failed downstream participant need not commit.
    assert(owner.stage(copy).status == Status::Ready);
    publishWithoutAllocation(owner);
    auto first = owner.snapshot();
    assert(first && first->source()->source == source);
    assert(owner.stage(next).status == Status::Pending && owner.snapshot() == first);
    assert(desc.liveGenerations->used() == 2u);
    // Restore the published source without discarding outstanding candidate work.
    assert(owner.stage(copy).status == Status::Ready && owner.preparedSource() == source);
    publishWithoutAllocation(owner);
    assert(owner.snapshot() == first && queue.pending);
    assert(owner.stage(nullptr).status == Status::Ready);
    publishWithoutAllocation(owner);
    assert(!owner.snapshot() && queue.pending && desc.liveGenerations->used() == 2u);
    assert(!owner.drainAndClear({}).success && queue.pending);
    queue.complete();
    assert(owner.stage(next).status == Status::Ready && !owner.snapshot());
    publishWithoutAllocation(owner);
    auto second = owner.snapshot();
    auto third = core::makeShared<renderer::TerrainVegetationSnapshot>(*next);
    ++third->catalogResourceGeneration;
    const auto exhausted = owner.stage(third);
    assert(exhausted.status == Status::Failed
        && exhausted.diagnostics == "VEGETATION-GPU-LIVE-GENERATION-BUDGET");
    assert(owner.snapshot() == second);
    first.reset();
    assert(owner.stage(third).status == Status::Pending);
    queue.complete();
    assert(owner.stage(third).status == Status::Ready);
    publishWithoutAllocation(owner);
    assert(owner.drainAndClear({}).success);
    assert(!owner.snapshot() && desc.liveGenerations->used() == 1u);
    second.reset();
    assert(!desc.liveGenerations->used() && !desc.liveInstances->used());
}

void rejectionContracts(rhi::GpuDevice& device)
{
    const auto source = tests::terrainVegetationDrawSourceFixture();
    for (unsigned mode = 0; mode < 4; ++mode) {
        auto desc = tests::terrainVegetationUploadDescFixture();
        desc.validatorOptIn = true;
        tests::DelayedValidatorQueue queue;
        queue.rejected = mode == 0;
        queue.acceptedError = mode == 1;
        queue.throws = mode == 2;
        if (mode == 3)
            desc.maxCpuBytes = 1u;
        tests::CapabilityOverrideDevice proxy(device, nullptr);
        proxy.overrideQueue(&queue);
        Publication owner(proxy, desc);
        assert(owner.stage(source).status == Status::Failed && !owner.snapshot());
        if (mode == 1 || mode == 2) {
            assert(desc.liveInstances->used() == 4u);
            assert(owner.stage(source).status == Status::Failed);
            assert(!owner.drainAndClear({}).success);
            queue.complete();
        } else {
            assert(!desc.liveInstances->used());
        }
        assert(owner.drainAndClear({}).success && !desc.liveInstances->used());
    }
}
} // namespace

int main()
{
    auto device = arp::rhi::createNullDevice();
    assert(device);
    identityContracts();
    publicationContracts(*device);
    rejectionContracts(*device);
    std::puts("Terrain vegetation CPU/GPU publication contracts PASS (Null validator only)");
}
