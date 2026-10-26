#include "core/io/file_io.h"
#include "scene/scene_asset_package.h"
#include "scene/scene_asset_resource.h"
#include "scene/script_component_persistence.h"
#include "tests/fixtures/script_component_fixture.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <limits>

namespace {
using namespace arp;
using namespace arp::scriptvm;
using Y = core::YamlContract;
using Node = Y::Node;
constexpr const char* assetId = "00000000-0000-4000-8000-000000000095";
constexpr const char* primaryId = "00000000-0000-4000-8000-000000000096";
constexpr const char* secondaryId = "00000000-0000-4000-8000-000000000097";

void check(bool ok, const core::pmr::String& diagnostic = {})
{
    if (!ok)
        std::fprintf(stderr, "FAIL: %s\n", diagnostic.c_str());
    assert(ok);
}

class Fixture final {
public:
    Fixture()
        : root(std::filesystem::path(ARP_SOURCE_DIR) / "build"
              / ("script-authoring-"
                  + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
    {
        check(domains.install());
        check(scene::initializeSceneAuthoringReflection());
        const auto load = [&](const char* file, const char* id, const char* resource) {
            auto module = core::makeShared<ScriptComponentModule>();
            const auto source = core::readTextFile(
                std::filesystem::path(ARP_SOURCE_DIR) / "tests/fixtures" / file, 65536);
            check(source.success, source.diagnostics);
            const auto loaded = module->load(source.text,
                id,
                resource,
                1,
                domains.scope.modules(),
                {"arp_scene"},
                tests::ScriptComponentFixture::budget(),
                32);
            check(loaded, module->diagnostic());
            return module;
        };
        primary = load("script_authoring_primary.arp", "authoring_primary", primaryId);
        secondary = load("script_authoring_secondary.arp", "authoring_secondary", secondaryId);
        factory = core::makeShared<scene::SceneComponentFactory>();
        check(registerScriptComponentPersistence(*factory, {primary, secondary}, 2, diagnostic),
            diagnostic);
        registry = core::makeShared<resource::AssetPackageCodecRegistry>(2);
        check(scene::registerSceneAssetPackageCodecs(*registry, factory, diagnostic), diagnostic);
        check(registry->freeze(diagnostic), diagnostic);
        std::filesystem::create_directories(root);

        scene::Scene authored(
            "two modules", scene::SceneId::fromValue(3), scene::SceneExecutionMode::Passive);
        auto& a = authored.createObject("primary");
        auto& b = authored.createObject("secondary");
        auto aComponent = primary->create(a, "Mover", false);
        // Passive candidates do not evaluate initializers or invoke constructors.
        check(bool(aComponent));
        auto receiver = aComponent->scriptInstance();
        a.addComponent(std::move(aComponent));
        auto* fields = receiver.ptr<reflection::instance>();
        fields->slot("amount") = makeInt(7);
        fields->slot("speed") = makeFloat(2.5);
        fields->slot("labels") = reflection::object(reflection::hlist{
            makeString(std::string_view("one")), makeString(std::string_view("two"))});
        fields->slot("target") = b.nativeReflectionObject();
        fields->slot("native") = a.transform().nativeReflectionObject();
        fields->slot("optional") = Value::undefined();
        auto bComponent = secondary->create(b, "Mover", false);
        check(bool(bComponent));
        bComponent->scriptInstance().ptr<reflection::instance>()->slot("message") =
            makeString(std::string_view("authored"));
        b.addComponent(std::move(bComponent));
        check(scene::SceneDocumentResource::encode(
                  authored, *factory, "arp.scene-asset", assetId, yaml, diagnostic),
            diagnostic);
        document = Y::parseSingleDocument(yaml);
        auto envelope = Node::mapping();
        envelope["resourceId"] = assetId;
        envelope["resourceType"] = "SceneAssetResource";
        auto payload = Node::mapping();
        payload["type"] = "arp.scene-package";
        payload["contractVersion"] = 1;
        payload["documentPath"] = "scene.yaml";
        payload["maxDocumentBytes"] = 131072;
        payload["maxObjects"] = 8;
        envelope["payload"] = std::move(payload);
        entry.resourceId = assetId;
        entry.resourceType = "SceneAssetResource";
        entry.envelopeYaml = Y::serialize(envelope);
    }

    ~Fixture()
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
        check(!error);
    }

    int probes(ScriptComponentModule& module)
    {
        Value result;
        check(module.session().call("read_probes", {}, result));
        return result.cast<int>();
    }

    void store(const Node& value) { storeText(Y::serialize(value)); }

    void storeText(std::string_view text)
    {
        std::ofstream stream(root / "scene.yaml", std::ios::binary | std::ios::trunc);
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        check(stream.good());
    }

    resource::AssetPackagePayloadIndex inspect(const Node& value)
    {
        return inspectText(Y::serialize(value));
    }

    resource::AssetPackagePayloadIndex inspectText(std::string_view text)
    {
        const auto a = primary->liveInstances(), b = secondary->liveInstances();
        const auto pa = probes(*primary), pb = probes(*secondary);
        storeText(text);
        const auto result = registry->find("SceneAssetResource")->inspect(entry, root);
        check(primary->liveInstances() == a && secondary->liveInstances() == b);
        check(probes(*primary) == pa && probes(*secondary) == pb);
        return result;
    }

    static Node& primaryRecord(Node& value)
    {
        for (auto& object : value["objects"].get_value_ref<Node::sequence_type&>())
            for (auto& component : object["components"].get_value_ref<Node::sequence_type&>())
                if (Y::text(component["configuration"], "scriptResourceId") == primaryId)
                    return component;
        assert(false);
        return value;
    }

    tests::ScriptComponentFixture domains;
    core::SharedPtr<ScriptComponentModule> primary, secondary;
    core::SharedPtr<scene::SceneComponentFactory> factory;
    core::SharedPtr<resource::AssetPackageCodecRegistry> registry;
    std::filesystem::path root;
    resource::InternalAssetPackageEntry entry;
    core::pmr::String yaml, diagnostic;
    Node document;
};

void contracts(Fixture& f)
{
    check(f.primary->liveInstances() == 0 && f.secondary->liveInstances() == 0);
    check(f.probes(*f.primary) == 0 && f.probes(*f.secondary) == 0);
    const auto valid = f.inspect(f.document);
    check(valid.success, valid.diagnostics);
    check(valid.files.size() == 1 && valid.dependencies.empty());
    const auto* entry = f.factory->persistence("arp.script-component/1");
    scene::SceneComponentFactory::Inspection output;
    output.componentType = "preserved";
    auto unknown = reflection::object(ScriptComponentIdentity{primaryId, "UnsupportedFields", 1});
    check(!entry->inspectConfiguration(unknown, output, f.diagnostic));
    check(output.componentType == "preserved"
        && f.diagnostic == "ARP-SCRIPT-COMPONENT-INSPECTION-FIELD-TYPE");

    for (unsigned negative = 0; negative < 12; ++negative) {
        std::fprintf(stderr, "Checking negative case %u\n", negative);
        auto value = f.document;
        auto& record = Fixture::primaryRecord(value);
        auto& fields = record["fields"];
        switch (negative) {
        case 0:
            record["configuration"]["scriptResourceId"] = "missing";
            break;
        case 1:
            record["configuration"]["moduleSchemaVersion"] = 99;
            break;
        case 2:
            record["configuration"]["scriptType"] = "Missing";
            break;
        case 3:
            fields["amount"] = "wrong integer";
            break;
        case 4:
            fields["extra"] = 1;
            break;
        case 5:
            fields.get_value_ref<Node::mapping_type&>().erase("speed");
            break;
        case 6:
            break; // The strict writer rejects infinity; inject explicit YAML below.
        case 7:
            fields["target"]["object"] = 999;
            break;
        case 8:
            fields["native"]["ordinal"] = 1;
            break;
        case 9:
            fields["native"]["componentType"] = "Missing";
            break;
        case 10:
            fields["target"]["extra"] = 1;
            break;
        case 11:
            fields["labels"] = Node::sequence();
            for (unsigned i = 0; i <= 4096; ++i)
                fields["labels"].get_value_ref<Node::sequence_type&>().push_back("x");
            break;
        }
        auto text = Y::serialize(value);
        if (negative == 6) {
            const auto offset = text.find("speed: 2.5");
            check(offset != core::pmr::String::npos);
            text.replace(offset, std::string_view("speed: 2.5").size(), "speed: .inf");
        }
        const auto rejected = f.inspectText(text);
        check(!rejected.success && !rejected.diagnostics.empty());
    }
    for (const auto limit : {0u, 1u}) {
        scene::SceneComponentFactory empty;
        check(!registerScriptComponentPersistence(
            empty, {f.primary, f.secondary}, limit, f.diagnostic));
        check(!empty.persistence("arp.script-component/1"));
    }
    scene::SceneComponentFactory duplicate;
    check(!registerScriptComponentPersistence(duplicate, {f.primary, f.primary}, 2, f.diagnostic));
    check(!duplicate.persistence("arp.script-component/1"));
    std::puts(
        "PASS cold multi-module identity/field/reference/budget checks, unsupported class and zero user-code/receiver side effects");
}

void roundtrip(Fixture& f)
{
    f.store(f.document);
    resource::ResourceManager resources;
    check(resources
            .load<scene::SceneAssetResource>({},
                assetId,
                core::pmr::String((f.root / "scene.yaml").generic_string().c_str()),
                131072,
                8)
            .success);
    const auto asset = resources.find<scene::SceneAssetResource>(assetId);
    {
        scene::Scene alreadyBound(
            "legacy alias", scene::SceneId::fromValue(5), scene::SceneExecutionMode::Passive);
        check(f.primary->bind(alreadyBound), f.primary->diagnostic());
        auto& retained = alreadyBound.createObject("retained");
        check(!asset.get()
                ->instantiateInto(
                    alreadyBound, alreadyBound.rootId(), resources, *f.factory, f.diagnostic)
                .valid());
        check(!f.diagnostic.empty() && alreadyBound.objects().size() == 1);
        check(f.primary->liveInstances() == 0 && f.secondary->liveInstances() == 0);
        check(
            !alreadyBound.hasRegisteredScriptComponentType(f.primary->qualifiedTypeName("Mover")));
        // Rollback preserves the original explicitly bound single-module API.
        check(retained.add_component("Mover").is<reflection::instance>());
        check(f.probes(*f.primary) == 0 && f.probes(*f.secondary) == 0);
    }
    for (auto mode : {scene::SceneExecutionMode::Passive, scene::SceneExecutionMode::Runtime}) {
        auto world = asset.get()->instantiate(resources, *f.factory, f.diagnostic, mode);
        check(bool(world), f.diagnostic);
        auto* a = world->findObjectByName("primary");
        auto* b = world->findObjectByName("secondary");
        check(a && b);
        check(
            a->get_component("Mover").ptr<reflection::instance>()->slot("amount").cast<int>() == 7);
        check(b->get_component("Mover")
                  .ptr<reflection::instance>()
                  ->slot("message")
                  .cast<core::pmr::String>()
            == "authored");
        core::pmr::String yaml;
        check(scene::SceneDocumentResource::encode(
                  *world, *f.factory, "arp.scene-asset", assetId, yaml, f.diagnostic),
            f.diagnostic);
        check(yaml == f.yaml);
        // A fresh scene has no ambiguous bare alias. Explicit module keys work.
        auto& extra = world->createObject("qualified registration");
        check(extra.add_component("Mover").is_undefined());
        const auto receiver = extra.add_component(f.primary->qualifiedTypeName("Mover"));
        check(receiver.is<reflection::instance>());
        check(extra.add_component("OnlyPrimary").is<reflection::instance>());
        if (mode == scene::SceneExecutionMode::Passive)
            check(f.probes(*f.primary) == 0 && !world->update({}).ok());
        else {
            check(world->update({0.1f, 0, 32}).ok());
            check(f.probes(*f.primary) == 121 && f.probes(*f.secondary) == 20);
        }
    }
    check(f.primary->liveInstances() == 0 && f.secondary->liveInstances() == 0);
    check(f.probes(*f.primary) == 121 && f.probes(*f.secondary) == 20);
    std::puts(
        "PASS two modules with same class name: Passive/Runtime exact YAML, qualified registration, unique legacy alias and actual Runtime constructor");
}

void lifetime(Fixture& f)
{
    core::CpuMemoryStats baseline;
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        const auto checked = f.inspect(f.document);
        check(checked.success, checked.diagnostics);
        const auto now = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = now;
        check(now.liveBytes == baseline.liveBytes
            && now.allocationCount - now.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
        check(f.primary->liveInstances() == 0 && f.secondary->liveInstances() == 0);
    }
    std::puts(
        "PASS ordinary warmed20 cold inspection cycles; core live bytes/count delta=0, script liveInstances=0");
}

// Admission probes preserve the current native-only transaction contract.
void identityAdmission(Fixture& f)
{
    for (bool registered : {false, true}) {
        scene::Scene world("script identity admission",
            scene::SceneId::fromValue(4),
            scene::SceneExecutionMode::Passive);
        auto& owner = world.createObject("owner");
        if (registered)
            check(f.primary->bind(world), f.primary->diagnostic());
        unsigned calls = 0;
        reflection::object candidateReceiver;
        bool rejected = false;
        {
            scene::Scene::IdentityRestoration transaction(
                world, {0, 1}, [&](scene::GameObject& object, const core::pmr::String& type) {
                    ++calls;
                    auto value = f.primary->create(object, type, false);
                    if (value)
                        candidateReceiver = value->scriptInstance();
                    return value;
                });
            try {
                transaction.createComponent(owner.id(), "Mover", 1);
            } catch (const std::invalid_argument& error) {
                rejected =
                    std::string_view(error.what()) == "ARP-SCENE-IDENTITY-FACTORY-UNSUPPORTED";
            }
        }
        check(rejected && calls == (registered ? 0u : 1u));
        check(owner.components().size() == 1 && f.primary->liveInstances() == 0);
        if (!registered)
            check(candidateReceiver.is<reflection::instance>()
                && !candidateReceiver.ptr<reflection::instance>()->alive());
        check(f.probes(*f.primary) == 0 && f.probes(*f.secondary) == 0);
    }
    std::puts(
        "PASS current identity gates: registered script rejected before factory; unregistered receiver rejected and retired without publication");
}

void moduleLifetime()
{
    core::CpuMemoryStats baseline;
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            Fixture fixture;
            const auto checked = fixture.inspect(fixture.document);
            check(checked.success, checked.diagnostics);
            {
                resource::ResourceManager resources;
                check(resources
                        .load<scene::SceneAssetResource>({},
                            assetId,
                            core::pmr::String(
                                (fixture.root / "scene.yaml").generic_string().c_str()),
                            131072,
                            8)
                        .success);
                const auto asset = resources.find<scene::SceneAssetResource>(assetId);
                auto world = asset.get()->instantiate(resources,
                    *fixture.factory,
                    fixture.diagnostic,
                    scene::SceneExecutionMode::Passive);
                check(bool(world), fixture.diagnostic);
                check(fixture.primary->liveInstances() == 1
                    && fixture.secondary->liveInstances() == 1);
            }
            // Fail after candidates exist, then verify all leases retire.
            auto malformed = fixture.document;
            Fixture::primaryRecord(malformed)["fields"]["amount"] = "invalid";
            fixture.store(malformed);
            {
                resource::ResourceManager resources;
                check(resources
                        .load<scene::SceneAssetResource>({},
                            assetId,
                            core::pmr::String(
                                (fixture.root / "scene.yaml").generic_string().c_str()),
                            131072,
                            8)
                        .success);
                const auto asset = resources.find<scene::SceneAssetResource>(assetId);
                check(!asset.get()->instantiate(resources,
                    *fixture.factory,
                    fixture.diagnostic,
                    scene::SceneExecutionMode::Passive));
                check(!fixture.diagnostic.empty());
            }
            check(fixture.primary->liveInstances() == 0 && fixture.secondary->liveInstances() == 0);
            check(fixture.probes(*fixture.primary) == 0 && fixture.probes(*fixture.secondary) == 0);
        }
        const auto now = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = now;
        check(now.liveBytes == baseline.liveBytes
            && now.allocationCount - now.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS ordinary warmed20 module prepare/Passive restore/failed restore/close cycles; core live bytes/count delta=0, script liveInstances=0");
}
} // namespace

int main(int argc, char** argv)
{
    try {
        Fixture fixture;
        if (argc == 2 && std::string_view(argv[1]) == "--smoke") {
            lifetime(fixture);
            moduleLifetime();
        } else if (argc == 2 && std::string_view(argv[1]) == "--identity-gates")
            identityAdmission(fixture);
        else {
            contracts(fixture);
            roundtrip(fixture);
        }
    } catch (const core::YamlContractError& error) {
        std::fprintf(stderr, "Unexpected YAML failure: %s\n", error.diagnostic);
        return 1;
    }
}
