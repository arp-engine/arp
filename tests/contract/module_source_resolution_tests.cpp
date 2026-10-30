#include "core/io/file_io.h"
#include "scriptvm/plugin/cpp_plugin_api.h"
#include "scriptvm/plugin/cpp_plugin_importer.h"
#include "scriptvm/runtime/module_registry.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <cassert>
#include <cstdio>
#include <stdexcept>

namespace {
using namespace arp;
using namespace scriptvm;
unsigned staticCalls = 0;

int staticEntry(ArpScriptPluginHostV2* host)
{
    ++staticCalls;
    return host->registrar->exportValue("answer", makeInt(42)) ? 1 : 0;
}

core::SharedPtr<ModuleExports> sourceExports()
{
    ScriptPluginRegistrar registrar("example");
    assert(registrar.exportValue("answer", makeInt(7)));
    return registrar.build();
}

void resolution()
{
    StaticPluginCatalog catalog;
    assert(catalog.add({"example", 2, 1, &staticEntry, "source-resolution-static/1", {}, {}}));
    auto ready = sourceExports();
    for (unsigned scenario = 0; scenario < 9; ++scenario) {
        ModuleRegistry registry;
        registry.setStaticPluginCatalog(&catalog);
        unsigned calls = 0;
        const auto beforeStatic = staticCalls;
        registry.setDeclaredSourceResolver([&](const core::pmr::String&) -> ModuleResolution {
            ++calls;
            switch (scenario) {
            case 0:
                return {};
            case 1:
                return {ModuleResolutionStatus::Ready, ready, {}};
            case 2:
                return {ModuleResolutionStatus::Failed, {}, "IO-EXACT-SOURCE-MISSING"};
            case 3:
                return {ModuleResolutionStatus::Ready, {}, {}};
            case 4:
                return {ModuleResolutionStatus::Absent, ready, {}};
            case 5:
                return {ModuleResolutionStatus::Failed, ready, "bad"};
            case 6:
                return {static_cast<ModuleResolutionStatus>(99), {}, {}};
            case 7:
                throw std::runtime_error("source failed");
            default:
                throw std::bad_alloc();
            }
        });
        assert(!registry.findLoadedModule("example") && calls == 0);
        if (scenario == 8) {
            bool threw = false;
            try {
                (void)registry.loadModule("example");
            } catch (const std::bad_alloc&) {
                threw = true;
            }
            assert(threw && calls == 1 && staticCalls == beforeStatic);
        } else {
            auto result = registry.loadModule("example");
            assert(calls == 1);
            if (scenario < 2) {
                assert(result && registry.lastDiagnostics().empty());
                assert(registry.findLoadedModule("example") == result && calls == 1);
                assert(result->symbols.at("answer").cast<int>() == (scenario ? 7 : 42));
                assert(staticCalls == beforeStatic + (scenario == 0));
                assert(registry.loadModule("example") == result && calls == 1);
                registry.setDeclaredSourceResolver([](const auto&) -> ModuleResolution {
                    return {ModuleResolutionStatus::Failed, {}, "must not replace cache"};
                });
                assert(registry.loadModule("example") == result);
            } else {
                assert(
                    !result && !registry.lastDiagnostics().empty() && staticCalls == beforeStatic);
                if (scenario == 2)
                    assert(registry.lastDiagnostics() == "IO-EXACT-SOURCE-MISSING");
                assert(!registry.loadModule("example") && calls == 2);
            }
        }
        registry.setDeclaredSourceResolver([&](const auto&) -> ModuleResolution {
            return {ModuleResolutionStatus::Ready, ready, {}};
        });
        assert(
            registry.loadModule("example")); // Retry after exceptions/failures restores the stack.
    }
    {
        ModuleRegistry registry;
        registry.setStaticPluginCatalog(&catalog);
        const auto before = staticCalls;
        registry.setSourceResolver([](const auto&) -> core::SharedPtr<ModuleExports> {
            throw std::runtime_error("legacy resolver failed");
        });
        assert(!registry.loadModule("example") && staticCalls == before);
        registry.setSourceResolver([](const auto&) -> core::SharedPtr<ModuleExports> {
            return {};
        });
        assert(registry.loadModule("example") && staticCalls == before + 1);
    }
    {
        ModuleRegistry registry;
        registry.setStaticPluginCatalog(&catalog);
        const auto before = staticCalls;
        registry.setDeclaredSourceResolver([&](const auto& name) -> ModuleResolution {
            auto nested = registry.loadModule(name);
            assert(!nested);
            return {ModuleResolutionStatus::Failed, {}, registry.lastDiagnostics()};
        });
        assert(!registry.loadModule("example") && staticCalls == before);
        assert(registry.lastDiagnostics().find("circular") != core::pmr::String::npos);
        registry.setDeclaredSourceResolver({});
        assert(registry.loadModule("example"));
    }
    {
        ModuleRegistry registry;
        unsigned calls = 0;
        registry.setDeclaredSourceResolver([&](const auto&) -> ModuleResolution {
            ++calls;
            return {ModuleResolutionStatus::Ready, ready, {}};
        });
        assert(registry.loadModule("warm"));
        auto quota = core::makeShared<core::CpuMemoryQuota>(1);
        bool threw = false;
        {
            core::CpuMemoryQuotaScope scope(quota);
            try {
                (void)registry.loadModule("example");
            } catch (const std::bad_alloc&) {
                threw = true;
            }
        }
        assert(threw && quota->liveBytes() == 0);
        assert(registry.loadModule("example") == ready);
        assert(calls == 3); // The failed cache publication was not retained.
    }
}

CppPluginImporterDesc descriptor(const unified_io_fixture::Workspace& workspace)
{
    CppPluginImporterDesc desc;
    desc.sourceRoot = workspace.root;
    desc.cacheRoot = workspace.root / "cache";
    desc.includeRoot = workspace.root / "include";
    desc.hostLibrary = workspace.root / "host.dll";
    desc.reflectionCodegenCommand = workspace.root / "no-codegen.exe";
    desc.compilerCommand.clear(); // Stop at a named native tool boundary after source preparation.
    desc.sourceBudget = {8, 4096, 8192, 8192};
    desc.sourceBindings.push_back({"example",
        "bound.arpplugin",
        {{"src/header.h", CppPluginHeaderRoot::IncludeRoot, "header.h"}}});
    return desc;
}

void bindings()
{
    unified_io_fixture::Workspace workspace;
    std::filesystem::create_directory(workspace.root / "include");
    const core::StringView manifest =
        "type: arp.script.cpp-plugin\ncontractVersion: 2\nmodule: example\nsource: source.cpp\nentry: register_example\nexportContractHash: test/1\nreflectionHeaders: [src/header.h]\n";
    workspace.write("bound.arpplugin", manifest);
    workspace.write("source.cpp", "explicit source\n");
    workspace.write("include/header.h", "explicit header\n");
    workspace.write("example.arpplugin", manifest); // Traps for the old discovery paths.
    workspace.write("example_module.arpplugin", manifest);
    {
        auto invalid = descriptor(workspace);
        invalid.hostTextBudget.maxMetadataBytes = 0;
        CppPluginImporter rejected(std::move(invalid));
        const auto failure = rejected.resolveModule("example");
        assert(failure.status == ModuleResolutionStatus::Failed
            && failure.diagnostic == "ARP-PLUGIN-HOST-TEXT-CONFIG");
        assert(!std::filesystem::exists(workspace.root / "cache"));
        assert(rejected.resolveModule("unbound").status == ModuleResolutionStatus::Absent);
    }
    for (unsigned scenario = 0; scenario < 16; ++scenario) {
        auto desc = descriptor(workspace);
        if (scenario == 0)
            desc.sourceBindings.clear();
        if (scenario == 1)
            desc.sourceBindings.push_back(desc.sourceBindings[0]);
        if (scenario == 2)
            desc.sourceBindings[0].manifest = "../bound.arpplugin";
        if (scenario == 3)
            desc.sourceBindings[0].manifest = "missing.arpplugin";
        if (scenario == 4)
            desc.sourceBindings[0].headers.clear();
        if (scenario == 5)
            desc.sourceBindings[0].headers[0].authoredPath = "different.h";
        if (scenario == 6)
            desc.sourceBindings[0].headers[0].relativePath = "../header.h";
        if (scenario == 7)
            desc.sourceBindings[0].headers[0].relativePath = "missing.h";
        if (scenario == 8)
            desc.sourceBudget.maxFileBytes = 1;
        if (scenario == 9)
            desc.sourceBudget.maxFiles = 1;
        if (scenario == 10)
            desc.sourceProfile = static_cast<CppPluginSourceProfile>(99);
        if (scenario == 11)
            desc.sourceBindings[0].headers.push_back(desc.sourceBindings[0].headers[0]);
        if (scenario == 13)
            desc.sourceBudget.maxFiles = 5;
        const auto sourceSize = manifest.size() + core::StringView("explicit source\n").size()
            + core::StringView("explicit header\n").size();
        if (scenario == 14)
            desc.sourceBudget.maxSourceBytes = sourceSize;
        if (scenario == 15)
            desc.sourceBudget.maxTextBytes = sourceSize;
        // Scenario12 reaches the explicit compiler boundary using an include-root binding.
        CppPluginImporter importer(std::move(desc));
        const auto result = importer.resolveModule("example");
        assert(result.status
            == (scenario == 0 ? ModuleResolutionStatus::Absent : ModuleResolutionStatus::Failed));
        assert(!result.exports);
        if (scenario != 0)
            assert(!result.diagnostic.empty());
        if (scenario == 12)
            assert(result.diagnostic == "C++ plugin compilerCommand is empty");
        assert(!std::filesystem::exists(workspace.root / "cache"));
        const auto absent = importer.resolveModule("unbound");
        if (scenario != 10)
            assert(absent.status == ModuleResolutionStatus::Absent);
    }
    // A declared dynamic failure blocks an otherwise usable static module.
    StaticPluginCatalog catalog;
    assert(catalog.add({"example", 2, 1, &staticEntry, "source-resolution-static/1", {}, {}}));
    CppPluginImporter importer(descriptor(workspace));
    ModuleRegistry registry;
    registry.setCppPluginImporter(&importer);
    registry.setStaticPluginCatalog(&catalog);
    const auto before = staticCalls;
    assert(!registry.loadModule("example") && staticCalls == before);
    assert(registry.lastDiagnostics() == "C++ plugin compilerCommand is empty");
    auto legacy = descriptor(workspace);
    legacy.sourceProfile = CppPluginSourceProfile::LegacyDiscoveryV1;
    legacy.sourceBindings.clear();
    CppPluginImporter legacyImporter(std::move(legacy));
    assert(legacyImporter.sourceProfile() == CppPluginSourceProfile::LegacyDiscoveryV1);
    assert(!legacyImporter.loadModule("example"));
    // It reaches the historical header path, which the explicit IncludeRoot
    // binding above deliberately resolved without probing a src/ alternative.
    assert(legacyImporter.lastDiagnostics() == "C++ plugin compilerCommand is empty");
    {
        workspace.write(u8"源.cpp", "explicit source\n");
        workspace.write(u8"include/头.h", "explicit header\n");
        core::pmr::String unicode(manifest);
        const auto at = unicode.find("source.cpp");
        unicode.replace(at, core::StringView("source.cpp").size(), u8"源.cpp");
        workspace.write("bound.arpplugin", unicode);
        auto desc = descriptor(workspace);
        desc.sourceBindings[0].headers[0].relativePath = std::filesystem::u8path(u8"头.h");
        CppPluginImporter named(std::move(desc));
        assert(named.resolveModule("example").diagnostic == "C++ plugin compilerCommand is empty");
    }
    for (const auto field : {"source", "includeDirs", "reflectionHeaders"}) {
        core::pmr::String escaped(manifest);
        if (core::StringView(field) == "source") {
            const auto at = escaped.find("source: source.cpp");
            escaped.replace(at,
                core::StringView("source: source.cpp").size(),
                "source: \"source.cpp\\u0000ignored\"");
        } else if (core::StringView(field) == "includeDirs") {
            escaped += "includeDirs: [\"include\\u0000ignored\"]\n";
        } else {
            const auto at = escaped.find("reflectionHeaders: [src/header.h]");
            escaped.replace(at,
                core::StringView("reflectionHeaders: [src/header.h]").size(),
                "reflectionHeaders: [\"src/header.h\\u0000ignored\"]");
        }
        workspace.write("bound.arpplugin", escaped);
        CppPluginImporter truncated(descriptor(workspace));
        const auto result = truncated.resolveModule("example");
        assert(result.status == ModuleResolutionStatus::Failed && !result.diagnostic.empty());
        assert(result.diagnostic != "C++ plugin compilerCommand is empty");
        assert(!std::filesystem::exists(workspace.root / "cache"));
    }
    workspace.write("bound.arpplugin", core::StringView("bad\0yaml", 8));
    CppPluginImporter invalid(descriptor(workspace));
    assert(invalid.resolveModule("example").status == ModuleResolutionStatus::Failed);
    assert(!std::filesystem::exists(workspace.root / "cache"));
}
} // namespace

int main(int argc, char** argv)
{
    const bool quick = argc == 2 && arp::core::StringView(argv[1]) == "--quick";
    assert(argc == 1 || quick);
    if (quick) {
        resolution();
        bindings();
        std::puts("ModuleSourceResolution/1 quick PASS");
        return 0;
    }
    const auto run = [] {
        resolution();
        bindings();
    };
    for (unsigned i = 0; i < 20; ++i)
        run();
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i)
        run();
    const auto after = core::defaultCpuAllocator().stats();
    std::printf("Module sources 20+20: core bytes %llu -> %llu; live %llu -> %llu\n",
        static_cast<unsigned long long>(before.liveBytes),
        static_cast<unsigned long long>(after.liveBytes),
        static_cast<unsigned long long>(before.allocationCount - before.deallocationCount),
        static_cast<unsigned long long>(after.allocationCount - after.deallocationCount));
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
}
