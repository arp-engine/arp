#include "core/io/file_io.h"
#include "editor/workspace/editor_script_component_catalog.h"
#include "editor/workspace/editor_script_modules.h"
#include "tests/fixtures/script_component_fixture.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>

namespace {
using namespace arp;
using namespace arp::editor;
constexpr const char* primaryId = "00000000-0000-4000-8000-000000000096";
constexpr const char* secondaryId = "00000000-0000-4000-8000-000000000097";

void check(bool value, const core::pmr::String& diagnostic = {})
{
    if (!value)
        std::fprintf(stderr, "FAIL: %s\n", diagnostic.c_str());
    assert(value);
}

class Fixture final {
public:
    Fixture()
        : root(std::filesystem::path(ARP_SOURCE_DIR) / "build"
              / ("editor-script-modules-"
                  + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
    {
        check(domains.install());
        std::filesystem::create_directories(root);
        manifest.id = "00000000-0000-4000-8000-000000000094";
        manifest.name = "Script authoring";
        manifest.engineContract = "arp.scene-asset/2";
        for (bool secondary : {false, true}) {
            ProjectScriptModule module;
            module.id = secondary ? "secondary" : "primary";
            module.resourceId = secondary ? secondaryId : primaryId;
            module.source = secondary ? "secondary.arp" : "primary.arp";
            module.schemaVersion = 1;
            module.authoring = ProjectScriptAuthoring{};
            module.authoring->maxSourceBytes = 65536;
            module.authoring->preparationBytes = 16 * 1024 * 1024;
            module.authoring->maxInstances = 32;
            module.authoring->execution = tests::ScriptComponentFixture::budget();
            module.authoring->imports = {"arp_scene"};
            const auto input =
                core::readTextFile(std::filesystem::path(ARP_SOURCE_DIR) / "tests/fixtures"
                        / (secondary ? "script_authoring_secondary.arp"
                                     : "script_authoring_primary.arp"),
                    65536);
            check(input.success, input.diagnostics);
            auto source = input.text;
            if (!secondary)
                source = "from arp_scene import main_scene\n" + source
                    + "\nmain_scene.set_object_name(\"UserInitializerExecuted\")\n";
            write(module.source, source);
            manifest.scriptModules.push_back(std::move(module));
        }
    }

    ~Fixture()
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
        check(!error);
    }

    void write(std::string_view name, std::string_view text)
    {
        std::ofstream output(
            root / std::filesystem::u8path(name), std::ios::binary | std::ios::trunc);
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        output.close();
        check(bool(output));
    }

    bool prepare(
        core::SharedPtr<const EditorScriptModules>& snapshot, core::pmr::String& diagnostic)
    {
        const auto before = domains.mainScene.name();
        const bool result = EditorScriptModules::prepare(
            root, manifest, domains.scope.modules(), snapshot, diagnostic);
        check(domains.mainScene.name() == before);
        return result;
    }

    tests::ScriptComponentFixture domains;
    std::filesystem::path root;
    ProjectManifest manifest;
};

void contract(Fixture& f)
{
    core::SharedPtr<const EditorScriptModules> snapshot;
    core::pmr::String diagnostic;
    check(f.prepare(snapshot, diagnostic), diagnostic);
    check(snapshot->entries().size() == 2 && snapshot->sources().size() == 2);
    auto* a = snapshot->find({primaryId, "Mover", 1});
    auto* b = snapshot->find({secondaryId, "Mover", 1});
    check(a && b && a != b && a->find("Mover")->fields.size() != b->find("Mover")->fields.size());
    check(!snapshot->find({primaryId, "Mover", 2}) && !snapshot->find({primaryId, "missing", 1}));
    for (const auto& entry : snapshot->entries()) {
        const auto disk =
            core::readTextFile(f.root / std::filesystem::u8path(entry.declaration.source), 65536);
        check(disk.success && entry.source == disk.text);
        check(entry.module->liveInstances() == 0 && entry.imports.size() == 1);
        check(!entry.imports[0].exportContractHash.empty()
            && !entry.imports[0].reflectionRegistryHash.empty());
        scriptvm::Value probe;
        check(entry.module->session().call("read_probes", {}, probe));
        check(probe.cast<int>() == 0);
    }
    // This positive probe proves the module initializer really can mutate the
    // supplied native Scene when explicitly invoked, but not during prepare.
    check(f.domains.mainScene.name() == "UserInitializerExecuted");
    f.domains.mainScene.setName("scene");
    check(snapshot->verifySources(f.root, diagnostic), diagnostic);
    const auto good = f.manifest;
    {
        auto empty = good;
        empty.scriptModules.clear();
        const auto* before = snapshot.get();
        for (const auto& invalidRoot : {f.root / "missing", f.root / "primary.arp"}) {
            check(!EditorScriptModules::prepare(
                invalidRoot, empty, f.domains.scope.modules(), snapshot, diagnostic));
            check(diagnostic == "PROJECT-INPUT-ROOT" && snapshot.get() == before);
        }
        core::SharedPtr<const EditorScriptModules> emptySnapshot;
        check(EditorScriptModules::prepare(
            f.root, empty, f.domains.scope.modules(), emptySnapshot, diagnostic));
        check(
            emptySnapshot && emptySnapshot->entries().empty() && emptySnapshot->sources().empty());
    }
    const auto rejected = [&](const char* prefix) {
        const auto* before = snapshot.get();
        check(!f.prepare(snapshot, diagnostic) && snapshot.get() == before);
        if (diagnostic.find(prefix) != 0)
            std::fprintf(stderr, "unexpected diagnostic %s\n", diagnostic.c_str());
        check(diagnostic.find(prefix) == 0);
        check(a->liveInstances() == 0 && b->liveInstances() == 0);
        f.manifest = good;
    };
    f.manifest.scriptModules[0].authoring.reset();
    rejected("EDITOR-SCRIPT-AUTHORING-CONFIGURATION-REQUIRED");
    f.manifest.scriptModules[0].authoring->maxSourceBytes = 1;
    rejected("EDITOR-SCRIPT-SOURCE-READ-OR-BUDGET");
    f.manifest.scriptModules[0].authoring->preparationBytes = 1;
    rejected("EDITOR-SCRIPT-PREPARATION-MEMORY-BUDGET");
    f.manifest.scriptModules[0].authoring->imports = {"missing"};
    rejected("EDITOR-SCRIPT-IMPORT-CONTRACT-UNAVAILABLE");
    f.manifest.scriptModules[0].dependencies = {"secondary"};
    rejected("EDITOR-SCRIPT-SOURCE-IMPORT-CONTRACT-REQUIRED");
    f.manifest.scriptModules[0].source = "../escape.arp";
    rejected("PROJECT-MODULE-SOURCE-INVALID");
    const auto original = snapshot->entries()[1].source;
    f.write("secondary.arp", "invalid syntax !!!\n");
    rejected("EDITOR-SCRIPT-MODULE-PREPARATION-FAILED");
    check(!snapshot->verifySources(f.root, diagnostic));
    check(snapshot->entries()[1].source == original);
    f.write("secondary.arp", original);
    check(snapshot->verifySources(f.root, diagnostic), diagnostic);
    // Exact source bytes are retained; changing only a newline is observable.
    f.write("secondary.arp", original + "\n");
    check(!snapshot->verifySources(f.root, diagnostic));
    f.write("secondary.arp", original);
}

void lifecycle(Fixture& f)
{
    core::CpuMemoryStats baseline;
    const auto goodSource = core::readTextFile(f.root / "secondary.arp", 65536);
    check(goodSource.success);
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            core::SharedPtr<const EditorScriptModules> snapshot;
            core::pmr::String diagnostic;
            check(f.prepare(snapshot, diagnostic), diagnostic);
            f.write("secondary.arp", "invalid syntax !!!\n");
            const auto* before = snapshot.get();
            check(!f.prepare(snapshot, diagnostic) && snapshot.get() == before);
            f.write("secondary.arp", goodSource.text);
            for (const auto& entry : snapshot->entries())
                check(entry.module->liveInstances() == 0);
        }
        const auto now = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = now;
        check(now.liveBytes == baseline.liveBytes
            && now.allocationCount - now.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS Editor script modules ordinary warmed20 prepare/failed candidate/close; core live bytes/count delta=0; script liveInstances=0");
}

constexpr EditorScriptCatalogBudget catalogBudget{4, 1024 * 1024};

void catalogContract(Fixture& f)
{
    core::SharedPtr<const EditorScriptModules> snapshot;
    core::SharedPtr<const EditorScriptComponentCatalog> catalog;
    core::pmr::String diagnostic;
    check(f.prepare(snapshot, diagnostic), diagnostic);
    check(EditorScriptComponentCatalog::prepare(snapshot, catalogBudget, catalog, diagnostic),
        diagnostic);
    check(catalog->entries().size() == 4);
    const auto* a = catalog->find({primaryId, "Mover", 1});
    const auto* b = catalog->find({secondaryId, "Mover", 1});
    const auto* unsupported = catalog->find({primaryId, "UnsupportedFields", 1});
    check(a && b && a->available() && b->available());
    check(a->displayName == "primary / Mover" && b->displayName == "secondary / Mover");
    check(a->moduleId == "primary" && a->source == "primary.arp" && a->fields->fields.size() == 6
        && b->fields->fields.size() == 1);
    check(unsupported && !unsupported->available()
        && unsupported->diagnostic == "ARP-SCRIPT-COMPONENT-INSPECTION-FIELD-TYPE");
    check(!catalog->find({primaryId, "PlainValue", 1}) && !catalog->find({primaryId, "Mover", 2})
        && !catalog->find({"", "Mover", 1}));
    check(catalog->find({primaryId, "OnlyPrimary", 1})->available());
    const auto& fields = a->fields->fields;
    const auto field = [&](std::string_view name) -> const reflection::SerializationDescriptor& {
        const auto found = std::find_if(fields.begin(), fields.end(), [&](const auto& item) {
            return item.name == name;
        });
        check(found != fields.end());
        return *found->type;
    };
    using Kind = reflection::SerializationDescriptor::Kind;
    check(field("amount").scalarType == reflection::type_of<int>()
        && field("labels").kind == Kind::Sequence && field("optional").kind == Kind::Optional
        && field("native").kind == Kind::Reference
        && field("native").referenceClass == reflection::class_of<scene::Component>());
    const auto* before = catalog.get();
    const auto reject = [&](core::SharedPtr<const EditorScriptModules> owner,
                            EditorScriptCatalogBudget budget,
                            const char* code) {
        check(!EditorScriptComponentCatalog::prepare(owner, budget, catalog, diagnostic));
        check(catalog.get() == before && diagnostic == code, diagnostic);
    };
    reject({}, catalogBudget, "EDITOR-SCRIPT-CATALOG-OWNER-OR-BUDGET");
    reject(snapshot, {}, "EDITOR-SCRIPT-CATALOG-OWNER-OR-BUDGET");
    reject(snapshot, {3, catalogBudget.preparationBytes}, "EDITOR-SCRIPT-CATALOG-CAPACITY");
    reject(snapshot, {4, 1}, "EDITOR-SCRIPT-CATALOG-MEMORY-BUDGET");

    // Source declaration order is not catalog identity or presentation order.
    std::reverse(f.manifest.scriptModules.begin(), f.manifest.scriptModules.end());
    core::SharedPtr<const EditorScriptModules> reversed;
    core::SharedPtr<const EditorScriptComponentCatalog> reversedCatalog;
    check(f.prepare(reversed, diagnostic), diagnostic);
    check(
        EditorScriptComponentCatalog::prepare(reversed, catalogBudget, reversedCatalog, diagnostic),
        diagnostic);
    for (std::size_t i = 0; i < catalog->entries().size(); ++i) {
        const auto& first = catalog->entries()[i];
        const auto& second = reversedCatalog->entries()[i];
        check(first.identity.scriptResourceId == second.identity.scriptResourceId
            && first.identity.scriptType == second.identity.scriptType
            && first.identity.moduleSchemaVersion == second.identity.moduleSchemaVersion
            && first.displayName == second.displayName);
    }
    // A catalog owns its original source/module snapshot even after the caller
    // releases it. Editing disk does not silently mutate the published schema.
    std::weak_ptr<const EditorScriptModules> lifetime = snapshot;
    const auto original = snapshot->entries()[0].source;
    snapshot.reset();
    check(!lifetime.expired());
    f.write("primary.arp", original + "\n");
    check(!catalog->modules().verifySources(f.root, diagnostic));
    check(catalog->find({primaryId, "Mover", 1}) == a && a->fields->fields.size() == 6);
    f.write("primary.arp", original);
    check(catalog->modules().verifySources(f.root, diagnostic), diagnostic);
    for (const auto& entry : catalog->modules().entries())
        check(entry.module->liveInstances() == 0);
    check(f.domains.mainScene.name() == "scene");
    catalog.reset();
    check(lifetime.expired());
    f.manifest.scriptModules.clear();
    core::SharedPtr<const EditorScriptModules> empty;
    check(f.prepare(empty, diagnostic), diagnostic);
    check(EditorScriptComponentCatalog::prepare(empty, catalogBudget, catalog, diagnostic),
        diagnostic);
    check(catalog->entries().empty() && catalog->modules().sources().empty());
    std::puts(
        "PASS Editor script catalog exact module/schema/type identities, shared cold descriptors, explicit unsupported rows, deterministic order and retained snapshot ownership");
}

void catalogLifecycle(Fixture& f)
{
    core::CpuMemoryStats baseline;
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            core::SharedPtr<const EditorScriptModules> snapshot;
            core::SharedPtr<const EditorScriptComponentCatalog> catalog;
            core::pmr::String diagnostic;
            check(f.prepare(snapshot, diagnostic), diagnostic);
            check(
                EditorScriptComponentCatalog::prepare(snapshot, catalogBudget, catalog, diagnostic),
                diagnostic);
            const auto* before = catalog.get();
            check(!EditorScriptComponentCatalog::prepare(snapshot, {4, 1}, catalog, diagnostic));
            check(catalog.get() == before && diagnostic == "EDITOR-SCRIPT-CATALOG-MEMORY-BUDGET");
            snapshot.reset();
            for (const auto& entry : catalog->modules().entries())
                check(entry.module->liveInstances() == 0);
        }
        const auto now = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = now;
        check(now.liveBytes == baseline.liveBytes
            && now.allocationCount - now.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
        check(f.domains.mainScene.name() == "scene");
    }
    std::puts(
        "PASS Editor script catalog ordinary warmed20 module prepare/catalog/quota rejection/close; core live bytes/count delta=0; script liveInstances=0");
}
} // namespace

int main(int argc, char** argv)
{
    Fixture fixture;
    if (argc == 2 && std::string_view(argv[1]) == "--smoke")
        lifecycle(fixture);
    else if (argc == 2 && std::string_view(argv[1]) == "--catalog")
        catalogContract(fixture);
    else if (argc == 2 && std::string_view(argv[1]) == "--catalog-smoke")
        catalogLifecycle(fixture);
    else {
        contract(fixture);
        std::puts(
            "PASS explicit authoring budgets, two independent modules, exact source/import snapshots, failures preserve owner, no construction or gameplay execution");
    }
}
