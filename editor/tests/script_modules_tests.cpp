#include "core/io/file_io.h"
#include "editor/workspace/editor_script_modules.h"
#include "tests/fixtures/script_component_fixture.h"

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
            write(module.source, input.text);
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
        return EditorScriptModules::prepare(
            root, manifest, domains.scope.modules(), snapshot, diagnostic);
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
    check(snapshot->verifySources(f.root, diagnostic), diagnostic);
    const auto good = f.manifest;
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
} // namespace

int main(int argc, char** argv)
{
    Fixture fixture;
    if (argc == 2 && std::string_view(argv[1]) == "--smoke")
        lifecycle(fixture);
    else {
        contract(fixture);
        std::puts(
            "PASS explicit authoring budgets, two independent modules, exact source/import snapshots, failures preserve owner, no construction or gameplay execution");
    }
}
