#include "3d/animator_component.h"
#include "3d/components.h"
#include "tests/fixtures/prefab_runtime_package_fixture.h"

#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace {
using namespace arp;
using namespace resource;
namespace fixture = tests::prefab_runtime_package_fixture;
constexpr const char* leafId = "d0fffa12-09bb-490f-9810-bab0a3f09415";

void check(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}

struct Observations {
    core::WeakPtr<const assets::AnimatedMeshAsset> mesh;
    core::WeakPtr<const animation::AnimatorControllerAsset> controller;
    unsigned loads = 0;
};

class Payload final : public PrefabRuntimePayloadResource {
public:
    Payload(Observations& observations, unsigned revision = 1, bool dependency = false)
        : observations_(observations), revision_(revision)
    {
        configureMetadata(
            {fixture::kRuntimePayloadId, resourceTypeName(), {}, "Contract Complete", {}});
        if (dependency)
            addDependency({leafId, "animation-input"});
        defaultStateName = "idle";
        runtimeAsset = core::makeShared<assets::AnimatedMeshAsset>(fixture::makeRuntimeAsset());
        controllerAsset =
            core::makeShared<animation::AnimatorControllerAsset>(fixture::makeController());
    }

    ResourceLoadResult load(ResourceManager& manager, const ResourceLoadContext& context) override
    {
        auto result = ResourceBase::load(manager, context);
        if (result.success) {
            observations_.mesh = runtimeAsset;
            observations_.controller = controllerAsset;
            ++observations_.loads;
        }
        return result;
    }

    void appendContentFingerprint(core::StableHash64& hash) const noexcept override
    {
        hash.appendU64(revision_);
    }

private:
    Observations& observations_;
    unsigned revision_;
};

ResourceDomainBudget budget()
{
    return {8, 8, 8, 2, {8, 8, 1024 * 1024, 1024 * 1024, 8}};
}

ResourceIdentity payloadIdentity()
{
    return {fixture::kRuntimePayloadId, PrefabRuntimePayloadResource::resourceTypeName(), "v1"};
}

void define(ResourceDomain& domain, Observations& observations)
{
    check(domain
              .registerResource({leafId, "AnimatorFixtureInput", "v1"},
                  [] {
                      return core::makeUnique<ResourceBase>(ResourceReference{
                          leafId, "AnimatorFixtureInput", {}, "Contract Complete", {}});
                  })
              .success(),
        "leaf registration failed");
    check(domain
              .registerResource(payloadIdentity(),
                  [&] {
                      return core::makeUnique<Payload>(observations, 1, true);
                  })
              .success(),
        "payload registration failed");
}

struct Sink final : scene::RenderExtractSink {
    core::pmr::String type{&core::defaultCpuMemoryResource()};
    const animation::AnimationClip* clip = nullptr;

    void accept(
        const scene::RenderExtractContext&, const scene::RenderExtractPayload& payload) override
    {
        type = payload.typeId;
        if (type == "scene3d.animation")
            clip = static_cast<const scene3d::Scene3DAnimationExtractPayload*>(payload.data)->clip;
    }
};

void leasedLifetime()
{
    Observations observations;
    ResourceManager manager;
    core::runtime::AsyncRuntime runtime({32, 2, 16, 16});
    ResourceDomain domain(manager, runtime, budget());
    define(domain, observations);
    auto acquired = domain.acquire(payloadIdentity());
    check(acquired.result.success(), "lease acquisition failed");
    scene3d::AnimatorComponent animator({});
    scene3d::AnimatorComponent other({});
    check(animator.bindResource(acquired.lease) && other.bindResource(acquired.lease),
        "typed binding failed");
    acquired.lease.reset();
    domain.collect();
    check(observations.loads == 1 && !observations.mesh.expired(),
        "animators lost or duplicated resource");
    check(manager.stateOf(leafId) == ResourceState::Ready,
        "animator did not retain dependency closure");
    scene3d::AnimatorEvaluationRequest request;
    request.deltaSeconds = .25f;
    check(animator.evaluate(request).ok(), "leased animation failed");
    const auto* expected = animator.desc().character;
    check(!animator.bindResource({}, "idle") && animator.desc().character == expected,
        "invalid lease damaged prior binding");
    auto wrongType = domain.acquire({leafId, "AnimatorFixtureInput", "v1"});
    check(!animator.bindResource(wrongType.lease), "wrong Resource type accepted");
    wrongType.lease.reset();
    auto another = domain.acquire(payloadIdentity());
    check(!animator.bindResource(another.lease, "missing-state")
            && animator.desc().character == expected,
        "invalid state damaged prior binding");
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    bool rejected = false;
    try {
        core::CpuMemoryQuotaScope scope(quota);
        animator.bindResource(another.lease, "run");
    } catch (const std::bad_alloc&) {
        rejected = true;
    }
    check(rejected && !quota->liveBytes() && animator.desc().character == expected
            && animator.evaluate({}).frame().clipTimeSeconds() == .25f,
        "quota failure damaged existing leased binding or playback");
    check(animator.bindResource(another.lease, "run"), "explicit resource state rejected");
    request.deltaSeconds = .1f;
    const auto run = animator.evaluate(request);
    check(run.ok() && run.frame().stateName() == "run" && run.frame().clipName() == "run"
            && std::abs(run.frame().clipTimeSeconds() - .15f) < .0001f,
        "same clip/state rebind did not reset playback");
    another.lease.reset();
    Sink sink;
    animator.extract({}, sink);
    check(sink.type == "scene3d.animation" && sink.clip == animator.desc().clip,
        "leased extraction differs from Resource clip");
    animator.clearResourceBinding();
    domain.collect();
    check(!observations.mesh.expired(), "one animator released another's resource");
    other.clearResourceBinding();
    domain.collect();
    check(observations.mesh.expired() && observations.controller.expired()
            && manager.stateOf(leafId) == ResourceState::Unloaded,
        "animator leaked Resource, Asset or dependency after final clear");
}

void closedAndOwnedDomain()
{
    Observations observations;
    scene3d::AnimatorComponent animator({});
    {
        ResourceDomain domain(core::runtime::AsyncRuntimeDesc{32, 2, 16, 16}, budget());
        define(domain, observations);
        auto acquired = domain.acquire(payloadIdentity());
        check(animator.bindResource(std::move(acquired.lease)), "owned domain bind failed");
    }
    check(animator.evaluate({}).ok() && !observations.mesh.expired(),
        "owned domain facade destruction killed leased animator");
    animator.clearResourceBinding();
    check(observations.mesh.expired(), "last owned-domain animator retained asset");
    ResourceDomain closing(core::runtime::AsyncRuntimeDesc{32, 2, 16, 16}, budget());
    define(closing, observations);
    auto acquired = closing.acquire(payloadIdentity());
    check(animator.bindResource(std::move(acquired.lease)), "closing domain bind failed");
    closing.close();
    check(observations.mesh.expired() && observations.controller.expired(),
        "Animator independently owns Asset after domain close");
    Sink sink;
    animator.extract({}, sink);
    check(
        sink.type == "scene3d.animation.unavailable", "closed Resource emitted stale render data");
    check(!animator.desc().character && !animator.desc().controller && !animator.desc().clip
            && !animator.evaluate({}).ok(),
        "closed Resource left usable Asset views");
}

auto prefab()
{
    auto value = core::makeUnique<PrefabResource>();
    value->configureMetadata(
        {fixture::kPrefabId, PrefabResource::resourceTypeName(), {}, "Contract Complete", {}});
    value->runtimePayloadId = fixture::kRuntimePayloadId;
    value->addDependency({fixture::kRuntimePayloadId, "runtime-payload"});
    return value;
}

void managerBinding()
{
    Observations observations;
    auto manager = core::makeShared<ResourceManager>();
    core::WeakPtr<ResourceManager> retained = manager;
    check(manager->loadResource(core::makeUnique<Payload>(observations)).success
            && manager->loadResource(prefab()).success,
        "manager fixture load failed");
    auto animator = core::makeUnique<scene3d::AnimatorComponent>(
        scene3d::AnimatorComponentDesc{}, [owner = manager] {
            return owner.get();
        });
    scene3d::AnimatorEditorProperties properties;
    properties.prefabResourceId = fixture::kPrefabId;
    check(animator->applyEditorAuthoringProperties(reflection::object(properties)),
        "editor binding failed");
    scene3d::AnimatorEvaluationRequest request;
    request.deltaSeconds = .4f;
    check(animator->evaluate(request).ok(), "editor animation failed");
    auto switchProperties = properties;
    switchProperties.defaultStateName = "run";
    const auto switchObject = reflection::object(switchProperties);
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    bool rejected = false;
    try {
        core::CpuMemoryQuotaScope scope(quota);
        animator->applyEditorAuthoringProperties(switchObject);
    } catch (const std::bad_alloc&) {
        rejected = true;
    }
    check(rejected && !quota->liveBytes()
            && animator->editorAuthoringProperties()
                .cast<const scene3d::AnimatorEditorProperties&>()
                .defaultStateName.empty()
            && animator->evaluate({}).frame().clipTimeSeconds() == .4f,
        "quota failure changed editor properties or reset existing playback");
    const auto previousMesh = observations.mesh;
    const auto previousController = observations.controller;
    check(manager->reloadResource(core::makeUnique<Payload>(observations, 2)).success,
        "runtime reload failed");
    manager->releaseRetiredResources();
    check(previousMesh.expired() && previousController.expired(),
        "editor Animator kept obsolete Asset alive outside Resource owner");
    check(!animator->desc().character && !animator->evaluate({}).ok(),
        "stale prefab dependency accepted after runtime reload");
    check(manager->unload(fixture::kPrefabId).success && manager->loadResource(prefab()).success,
        "prefab dependency refresh failed");
    check(animator->desc().character == observations.mesh.lock().get(),
        "descriptor did not refresh Resource generation");
    const auto reset = animator->evaluate({});
    check(reset.ok() && reset.frame().clipTimeSeconds() == 0, "reload did not restart playback");
    auto invalid = properties;
    invalid.defaultStateName = "unknown";
    const auto* old = animator->desc().character;
    check(!animator->applyEditorAuthoringProperties(reflection::object(invalid))
            && animator->desc().character == old,
        "invalid editor binding replaced valid state");
    check(manager->unload(fixture::kRuntimePayloadId).success, "runtime unload failed");
    check(observations.mesh.expired() && !animator->desc().character && !animator->desc().clip,
        "unload left independently owned Asset or stale descriptor");
    Sink sink;
    animator->extract({}, sink);
    check(sink.type == "scene3d.animation.unavailable",
        "unloaded editor Resource emitted render data");
    manager.reset();
    check(!retained.expired(), "frozen manager provider did not retain Resource owner");
    animator.reset();
    check(retained.expired(), "Animator leaked frozen manager provider");
}

void legacyBorrowed()
{
    const auto mesh = fixture::makeRuntimeAsset();
    const auto controller = fixture::makeController();
    scene3d::AnimatorComponentDesc desc;
    desc.character = &mesh;
    desc.controller = &controller;
    desc.defaultStateName = "idle";
    scene3d::AnimatorComponent animator(desc);
    check(animator.desc().character == &mesh && animator.evaluate({}).ok(),
        "documented Legacy borrowed-Asset adapter regressed");
}
} // namespace

int main(int argc, char** argv)
{
    try {
        registerPrefabAssetReflection();
        (void)prefabResourceFactory();
        const bool smoke = argc == 2 && core::StringView(argv[1]) == "--smoke";
        // Reflection lazily registers AnimatorEditorProperties on first use.
        // Warm that real route before measuring repeated residency cycles.
        leasedLifetime();
        closedAndOwnedDomain();
        managerBinding();
        legacyBorrowed();
        const auto baseline = core::defaultCpuAllocator().stats();
        for (unsigned cycle = 0; cycle < (smoke ? 20u : 1u); ++cycle) {
            leasedLifetime();
            closedAndOwnedDomain();
            managerBinding();
            legacyBorrowed();
            check(core::defaultCpuAllocator().stats().liveBytes == baseline.liveBytes,
                "animator cycle retained core allocations after warmup");
        }
        const auto after = core::defaultCpuAllocator().stats();
        check(after.liveBytes == baseline.liveBytes, "animator cycles retained core allocations");
        std::puts(
            "PASS Animator Resource leases/dependencies, manager generation/reload/unload, "
            "closed/owned domain, failed switches, extraction, Legacy adapter and core allocation baseline");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
