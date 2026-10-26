#include "core/io/file_io.h"
#include "scene/script_component_persistence.h"
#include "tests/fixtures/script_component_fixture.h"

#include <cassert>
#include <cstdio>
#include <stdexcept>
#include <string_view>

namespace {
using namespace arp;
using namespace arp::scriptvm;
using Transaction = scene::Scene::IdentityRestoration;
using Oid = scene::GameObjectId;
using Cid = scene::ComponentId;
constexpr const char* primaryId = "00000000-0000-4000-8000-000000000096";
constexpr const char* secondaryId = "00000000-0000-4000-8000-000000000097";

void check(bool value, const core::pmr::String& diagnostic = {})
{
    if (!value)
        std::fprintf(stderr, "FAIL: %s\n", diagnostic.c_str());
    assert(value);
}

template <class F> void rejects(const char* diagnostic, F&& action)
{
    bool rejected = false;
    try {
        action();
    } catch (const std::logic_error& error) {
        rejected = std::string_view(error.what()) == diagnostic;
        if (!rejected)
            std::fprintf(stderr, "expected %s, got %s\n", diagnostic, error.what());
    }
    check(rejected);
}

class Fixture final {
public:
    Fixture()
    {
        check(domains.install());
        const auto load = [&](ScriptComponentModule& module,
                              const char* file,
                              const char* id,
                              const char* resource) {
            const auto source = core::readTextFile(
                std::filesystem::path(ARP_SOURCE_DIR) / "tests/fixtures" / file, 65536);
            check(source.success, source.diagnostics);
            check(module.load(source.text,
                      id,
                      resource,
                      1,
                      domains.scope.modules(),
                      {"arp_scene"},
                      tests::ScriptComponentFixture::budget(),
                      32),
                module.diagnostic());
        };
        load(primary, "script_authoring_primary.arp", "identity_primary", primaryId);
        load(secondary, "script_authoring_secondary.arp", "identity_secondary", secondaryId);
    }

    static ScriptComponentIdentity identity(const char* resource = primaryId)
    {
        return {resource, "Mover", 1};
    }

    void untouched()
    {
        for (auto* module : {&primary, &secondary}) {
            Value result;
            check(module->session().call("read_probes", {}, result), module->diagnostic());
            check(result.cast<int>() == 0);
        }
    }

    tests::ScriptComponentFixture domains;
    ScriptComponentModule primary, secondary;
};

void coldFields(ScriptComponentModule& module,
    scene::Component& component,
    scene::GameObject& target,
    scene::Component& other)
{
    auto receiver = component.scriptInstance();
    auto* fields = receiver.ptr<reflection::instance>();
    fields->slot("amount") = makeInt(7);
    fields->slot("speed") = makeFloat(2.5);
    fields->slot("labels") = reflection::object(reflection::hlist{});
    fields->slot("target") = target.nativeReflectionObject();
    fields->slot("native") = other.nativeReflectionObject();
    fields->slot("optional") = makeNil();
    const auto descriptor = scriptComponentFieldDescriptor(module, *module.find("Mover"));
    check(bool(descriptor));
    // Exercise the same archive/descriptor accessors as persistence. Reference
    // identity is checked separately through the transaction's exact resolver.
    check(descriptor->get(receiver, "amount").cast<int>() == 7);
}

void identities(Fixture& f)
{
    scene::Scene world(
        "identity", scene::SceneId::fromValue(6), scene::SceneExecutionMode::Passive);
    auto& live = world.createObject("live owner");
    const auto aId = Cid::fromValue(200), bId = Cid::fromValue(201);
    const auto objectId = Oid::fromValue(100);
    const auto transformId = Cid::fromValue(199);
    reflection::object oldReceiver, oldNative, oldObject;
    for (unsigned round = 0; round < 2; ++round) {
        {
            Transaction transaction(world, {1, 3});
            auto& candidate =
                transaction.restoreObject(objectId, transformId, "candidate", world.rootId(), 0);
            auto& a = transaction.restoreScriptComponent(
                live.id(), aId, f.primary, Fixture::identity(), 1);
            auto& b = transaction.restoreScriptComponent(
                candidate.id(), bId, f.secondary, Fixture::identity(secondaryId), 1);
            check(!live.findComponent(aId) && !world.findObject(objectId));
            check(transaction.resolveComponent(live.id(), aId) == &a
                && transaction.resolveComponent(candidate.id(), bId) == &b
                && transaction.resolveComponent(candidate.id(), transformId)
                    == &candidate.transform()
                && transaction.resolveComponent(live.id(), live.transform().id())
                    == &live.transform());
            check(!transaction.resolveComponent(candidate.id(), aId)
                && !transaction.resolveComponent(Oid::fromValue(999), bId));
            coldFields(f.primary, a, candidate, *transaction.resolveComponent(candidate.id(), bId));
            b.scriptInstance().ptr<reflection::instance>()->slot("message") =
                makeString(std::string_view("restored"));
            // Both directions can resolve unpublished components on live/new owners.
            auto aReceiver = a.scriptInstance();
            auto bReceiver = b.scriptInstance();
            check(aReceiver.ptr<reflection::instance>()
                    ->__getitem__("component")
                    .native_reference_alive());
            check(a.gameObject() == &live && b.gameObject() == &candidate);
            check(!oldNative.native_reference_alive());
            if (round)
                check(!oldReceiver.ptr<reflection::instance>()->alive()
                    && !oldObject.native_reference_alive());
            transaction.prepare();
            auto quota = core::makeShared<core::CpuMemoryQuota>(1);
            const auto before = core::defaultCpuAllocator().stats();
            {
                core::CpuMemoryQuotaScope scope(quota);
                transaction.commit();
            }
            const auto after = core::defaultCpuAllocator().stats();
            check(before.allocationCount == after.allocationCount);
            check(live.findComponent(aId) == &a && world.findObject(objectId) == &candidate);
            check(aReceiver.ptr<reflection::instance>()
                    ->__getitem__("native")
                    .native_reference_alive());
            oldReceiver = aReceiver;
            oldNative = a.nativeReflectionObject();
            oldObject = candidate.nativeReflectionObject();
            check(f.primary.liveInstances() == 1 && f.secondary.liveInstances() == 1);
        }
        check(live.removeComponent(aId) && world.destroyObject(objectId).ok());
        check(!oldReceiver.ptr<reflection::instance>()->alive()
            && !oldNative.native_reference_alive() && !oldObject.native_reference_alive());
        check(f.primary.liveInstances() == 0 && f.secondary.liveInstances() == 0);
    }
    auto& next = world.createObject("next");
    check(next.id().value() > objectId.value() && next.transform().id().value() > bId.value());
    f.untouched();
}

void probesAndGates(Fixture& f)
{
    scene::Scene runtime;
    rejects("ARP-SCENE-IDENTITY-PASSIVE-ONLY", [&] {
        Transaction t(runtime, {1, 1});
    });
    scene::Scene world("gates", scene::SceneId::fromValue(7), scene::SceneExecutionMode::Passive);
    auto& owner = world.createObject("source");
    auto source = f.primary.create(owner, "Mover", false);
    check(bool(source));
    const auto liveReceiver = source->scriptInstance();
    owner.addComponent(std::move(source));
    // Normal create still refuses duplicates. The private transaction path permits
    // isolated validation but prepare must refuse publication of either module.
    check(!f.primary.create(owner, "Mover", false));
    for (bool otherModule : {false, true}) {
        reflection::object probe, probeNative;
        {
            Transaction t(world, {0, 1});
            auto& candidate = t.createScriptComponent(owner.id(),
                otherModule ? f.secondary : f.primary,
                Fixture::identity(otherModule ? secondaryId : primaryId),
                2);
            probe = candidate.scriptInstance();
            probeNative = candidate.nativeReflectionObject();
            check(owner.components().size() == 2 && probe.ptr<reflection::instance>()->alive());
            rejects("ARP-SCENE-IDENTITY-SCRIPT-DUPLICATE", [&] {
                t.prepare();
            });
            rejects("ARP-SCENE-IDENTITY-CONSUMED", [&] {
                t.commit();
            });
        }
        check(!probe.ptr<reflection::instance>()->alive() && !probeNative.native_reference_alive());
        check(liveReceiver.ptr<reflection::instance>()->alive() && f.primary.liveInstances() == 1
            && f.secondary.liveInstances() == 0);
    }
    for (unsigned error = 0; error < 6; ++error) {
        auto identity = Fixture::identity();
        if (error == 0)
            identity.scriptResourceId = secondaryId;
        if (error == 1)
            identity.moduleSchemaVersion = 2;
        if (error == 2)
            identity.scriptType = "missing";
        Transaction t(world, {0, error == 5 ? 0u : 1u});
        const char* expected = error < 3 ? "ARP-SCENE-IDENTITY-SCRIPT-IDENTITY"
            : error == 3                 ? "ARP-SCENE-IDENTITY-FACTORY-UNSUPPORTED"
            : error == 4                 ? "ARP-SCENE-IDENTITY-COMPONENT-INDEX"
                                         : "ARP-SCENE-IDENTITY-COMPONENT-CAPACITY";
        rejects(expected, [&] {
            t.restoreScriptComponent(error == 3 ? Oid::fromValue(999) : owner.id(),
                Cid::fromValue(77),
                f.primary,
                identity,
                error == 4 ? 0u : 2u);
        });
        rejects("ARP-SCENE-IDENTITY-CONSUMED", [&] {
            t.commit();
        });
    }
    // Two candidates of a bare type on a new owner are equally forbidden.
    {
        Transaction t(world, {1, 3});
        auto& candidate = t.createObject("duplicate", world.rootId(), 0);
        t.createScriptComponent(candidate.id(), f.primary, Fixture::identity(), 1);
        t.createScriptComponent(candidate.id(), f.secondary, Fixture::identity(secondaryId), 2);
        rejects("ARP-SCENE-IDENTITY-SCRIPT-DUPLICATE", [&] {
            t.prepare();
        });
    }
    check(owner.components().size() == 2 && world.objects().size() == 1);
    check(owner.removeComponent(owner.component_at(1)->id()));
    check(f.primary.liveInstances() == 0 && f.secondary.liveInstances() == 0);
    f.untouched();
}

// This fixture supplies exact external identities to the real archive. It does
// not define a production resource format or resolve references by class/order.
class CandidateReferences final : public reflection::SerializationContext {
public:
    explicit CandidateReferences(Transaction& transaction) : transaction_(transaction) {}

    bool writeReference(const reflection::property&,
        const reflection::object&,
        core::YamlContract::Node&,
        core::pmr::String&) const override
    {
        return false;
    }

    bool readReference(const reflection::property&,
        const core::YamlContract::Node&,
        reflection::object&,
        core::pmr::String&) const override
    {
        return false;
    }

    bool readReference(const reflection::SerializationDescriptor& descriptor,
        const core::YamlContract::Node& node,
        reflection::object& value,
        core::pmr::String& diagnostic) const override
    {
        using Y = core::YamlContract;
        Y::onlyKeys(node, {"owner", "component"});
        const auto oid = Oid::fromValue(Y::uint64(Y::required(node, "owner")));
        const auto cid = Cid::fromValue(Y::uint64(Y::required(node, "component")));
        if (descriptor.referenceType == "GameObject") {
            if (auto* object = transaction_.resolveObject(oid); object && !cid.valid()) {
                value = object->nativeReflectionObject();
                return true;
            }
        } else if (descriptor.referenceType == "Component") {
            if (auto* component = transaction_.resolveComponent(oid, cid)) {
                value = component->nativeReflectionObject();
                return true;
            }
        }
        diagnostic = "FIXTURE-EXACT-REFERENCE-NOT-FOUND";
        return false;
    }

private:
    Transaction& transaction_;
};

void referencesAndFieldRollback(Fixture& f)
{
    scene::Scene world(
        "references", scene::SceneId::fromValue(9), scene::SceneExecutionMode::Passive);
    auto& a = world.createObject("live A");
    auto& b = world.createObject("live B");
    reflection::object rolledBack;
    for (bool invalid : {true, false}) {
        Transaction t(world, {0, 2});
        auto& x =
            t.restoreScriptComponent(a.id(), Cid::fromValue(70), f.primary, Fixture::identity(), 1);
        auto& y =
            t.restoreScriptComponent(b.id(), Cid::fromValue(71), f.primary, Fixture::identity(), 1);
        CandidateReferences references(t);
        const auto descriptor = scriptComponentFieldDescriptor(f.primary, *f.primary.find("Mover"));
        const auto fields = [&](Oid owner, Cid component) {
            auto node = core::YamlContract::parseSingleDocument(
                "amount: 3\nspeed: 1.5\nlabels: [verified]\noptional: null\n"
                "target: {owner: 0, component: 0}\nnative: {owner: 0, component: 0}\n");
            node["target"]["owner"] = owner.value();
            node["native"]["owner"] = owner.value();
            node["native"]["component"] = component.value();
            return node;
        };
        auto xFields = fields(b.id(), y.id());
        auto yFields = fields(a.id(), x.id());
        auto xReceiver = x.scriptInstance(), yReceiver = y.scriptInstance();
        reflection::SerializationArchive first({}, &references), second({}, &references);
        check(first.readValue(*descriptor, xFields, xReceiver), first.diagnostics());
        if (invalid)
            yFields["amount"] = "not an int";
        const bool restored = second.readValue(*descriptor, yFields, yReceiver);
        if (invalid) {
            check(!restored && !second.diagnostics().empty());
            rolledBack = xReceiver;
            continue; // Destroy both candidates; no partially restored fields publish.
        }
        check(restored, second.diagnostics());
        check(!rolledBack.ptr<reflection::instance>()->alive());
        check(xReceiver.ptr<reflection::instance>()->__getitem__("native").cast<scene::Component*>()
                == &y
            && yReceiver.ptr<reflection::instance>()
                    ->__getitem__("native")
                    .cast<scene::Component*>()
                == &x);
        check(xReceiver.ptr<reflection::instance>()
                    ->__getitem__("component")
                    .cast<scene::Component*>()
                == &x
            && xReceiver.ptr<reflection::instance>()
                    ->__getitem__("game_object")
                    .cast<scene::GameObject*>()
                == &a);
        check(a.components().size() == 1 && b.components().size() == 1);
        t.prepare();
        t.commit();
    }
    check(a.components().size() == 2 && b.components().size() == 2);
    check(a.removeComponent(Cid::fromValue(70)) && b.removeComponent(Cid::fromValue(71)));
    check(f.primary.liveInstances() == 0);
    f.untouched();
}

void allocations(Fixture& f)
{
    unsigned failures = 0, successes = 0;
    for (std::size_t bytes = 1; bytes <= 65536; bytes *= 2) {
        scene::Scene world(
            "quota", scene::SceneId::fromValue(8), scene::SceneExecutionMode::Passive);
        auto& live = world.createObject("live");
        const auto quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        reflection::object receiver;
        bool committed = false;
        {
            core::CpuMemoryQuotaScope scope(quota);
            try {
                Transaction t(world, {1, 3});
                auto& object = t.restoreObject(
                    Oid::fromValue(44), Cid::fromValue(44), "candidate", world.rootId(), 0);
                auto& component = t.restoreScriptComponent(
                    live.id(), Cid::fromValue(45), f.primary, Fixture::identity(), 1);
                receiver = component.scriptInstance();
                auto& other = t.restoreScriptComponent(object.id(),
                    Cid::fromValue(46),
                    f.secondary,
                    Fixture::identity(secondaryId),
                    1);
                coldFields(f.primary, component, object, other);
                t.prepare();
                t.commit();
                committed = true;
            } catch (const std::bad_alloc&) {
                ++failures;
            }
        }
        if (committed) {
            ++successes;
            check(world.objects().size() == 2 && live.components().size() == 2);
            check(live.removeComponent(Cid::fromValue(45))
                && world.destroyObject(Oid::fromValue(44)).ok());
        } else {
            check(world.objects().size() == 1 && live.components().size() == 1);
            auto& next = world.createObject("retry");
            check(next.id().value() == 3 && next.transform().id().value() == 3);
        }
        if (receiver.is<reflection::instance>())
            check(!receiver.ptr<reflection::instance>()->alive());
        check(f.primary.liveInstances() == 0 && f.secondary.liveInstances() == 0);
    }
    check(failures && successes);
    f.untouched();
}

void instanceCapacity(Fixture& f)
{
    scene::Scene world(
        "instances", scene::SceneId::fromValue(10), scene::SceneExecutionMode::Passive);
    core::pmr::Vector<Oid> owners;
    for (unsigned i = 0; i < 33; ++i)
        owners.push_back(world.createObject("owner").id());
    {
        Transaction t(world, {0, 33});
        for (unsigned i = 0; i < 32; ++i)
            t.createScriptComponent(owners[i], f.primary, Fixture::identity(), 1);
        check(f.primary.liveInstances() == 32);
        rejects("ARP-SCENE-IDENTITY-SCRIPT-CANDIDATE", [&] {
            t.createScriptComponent(owners.back(), f.primary, Fixture::identity(), 1);
        });
        check(f.primary.diagnostic() == "ARP-SCRIPT-COMPONENT-CAPACITY");
        rejects("ARP-SCENE-IDENTITY-CONSUMED", [&] {
            t.commit();
        });
    }
    for (const auto owner : owners)
        check(world.findObject(owner)->components().size() == 1);
    check(f.primary.liveInstances() == 0);
    f.untouched();
}

void run()
{
    Fixture fixture;
    identities(fixture);
    probesAndGates(fixture);
    referencesAndFieldRollback(fixture);
    allocations(fixture);
    instanceCapacity(fixture);
}
} // namespace

int main(int argc, char** argv)
{
    const bool smoke = argc == 2 && std::string_view(argv[1]) == "--smoke";
    core::CpuMemoryStats baseline;
    for (unsigned cycle = 0; cycle <= (smoke ? 20u : 0u); ++cycle) {
        run();
        const auto now = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = now;
        check(now.liveBytes == baseline.liveBytes
            && now.allocationCount - now.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(smoke
            ? "PASS script identity ordinary warmed20 prepare/restore/rollback/close; core live bytes/count delta=0; script liveInstances=0"
            : "PASS script exact identity, cold probes, scoped component resolution, duplicate/identity gates, quota rollback, allocation-free commit, old handles retired");
}
