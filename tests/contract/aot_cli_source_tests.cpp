#include "core/hash/sha256.h"
#include "tests/fixtures/unified_io_fixture.h"
#include "tools/io/cold_tool_files.h"
#include "tools/script_aotc/aot_cli_sources.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
using namespace arp;
using namespace core::io;
using namespace tools::script_aotc;
constexpr std::uint64_t fileBudget = 1024 * 1024;
constexpr scriptvm::ScriptSourceBudget budget{16, fileBudget, 4 * fileBudget, 4 * fileBudget};
constexpr std::array<const char*, 3> names{"basic.package.yaml", "basic.arp", "math_dep.arp"};

class Fixture final {
public:
    Fixture()
    {
        tools::io::ColdToolFiles files(
            fileBudget, 0, tools::io::ColdToolFileScope::NativeHostFilesystem);
        for (std::size_t i = 0; i < names.size(); ++i) {
            FileReadRequest request;
            request.contractVersion = 1;
            request.pathScope = FilePathScope::HostFilesystem;
            request.path = core::FilePath::utf8(
                std::filesystem::path(ARP_SOURCE_DIR) / "tests/fixtures/aot" / names[i]);
            request.maxBytes = fileBudget;
            const auto read = files.read(request);
            assert(read.status == FileIoStatus::Success);
            original[i].assign(reinterpret_cast<const char*>(read.bytes.data()), read.bytes.size());
        }
        files.retire();
        restore();
    }

    void restore()
    {
        for (std::size_t i = 0; i < names.size(); ++i)
            workspace.write(names[i], original[i]);
    }

    std::filesystem::path manifest() const { return workspace.root / names.front(); }

    unified_io_fixture::Workspace workspace;
    std::array<core::pmr::String, 3> original{core::pmr::String(&core::defaultCpuMemoryResource()),
        core::pmr::String(&core::defaultCpuMemoryResource()),
        core::pmr::String(&core::defaultCpuMemoryResource())};
};

void verify(const AotCliPreparedSources& prepared)
{
    if (!prepared.ok())
        std::fprintf(stderr, "%s\n", prepared.loaded.error.message.c_str());
    assert(prepared.ok() && prepared.loaded.package.packageId == "basic");
    assert(prepared.sources->size() == 2 && prepared.telemetry.submitted == 3);
    assert(prepared.backend.kind == FileBackendKind::ThreadPoolFallbackFileBackend
        && prepared.backend.status == FileBackendStatus::PrototypeComplete
        && !prepared.backend.nativeAsync);
    for (const auto& module : prepared.loaded.package.modules) {
        const auto* source = prepared.sources->find(module.moduleId);
        assert(source && source->binding.pathScope == FilePathScope::HostFilesystem
            && source->binding.path == core::FilePath::utf8(module.source)
            && source->textHash == core::Sha256::hexDigest(source->text));
    }
}

void failures(Fixture& fixture)
{
    auto result = AotCliSourcePreparation::load(fixture.manifest(), 1, budget);
    assert(!result->ok() && !result->sources);
    for (const auto value :
        {scriptvm::ScriptSourceBudget{1, fileBudget, 4 * fileBudget, 4 * fileBudget},
            scriptvm::ScriptSourceBudget{16, 1, 4 * fileBudget, 4 * fileBudget},
            scriptvm::ScriptSourceBudget{16, fileBudget, 1, 4 * fileBudget},
            scriptvm::ScriptSourceBudget{16, fileBudget, 4 * fileBudget, 1}}) {
        result = AotCliSourcePreparation::load(fixture.manifest(), fileBudget, value);
        assert(!result->ok() && !result->sources);
    }
    fixture.workspace.write("basic.arp", "\xff");
    result = AotCliSourcePreparation::load(fixture.manifest(), fileBudget, budget);
    assert(!result->ok() && result->loaded.error.message.find("UTF8") != core::pmr::String::npos);
    fixture.workspace.write("basic.arp", core::StringView("a\0b", 3));
    result = AotCliSourcePreparation::load(fixture.manifest(), fileBudget, budget);
    assert(!result->ok() && result->loaded.error.message.find("NUL") != core::pmr::String::npos);
    fixture.restore();
    std::filesystem::remove(fixture.workspace.root / "math_dep.arp");
    result = AotCliSourcePreparation::load(fixture.manifest(), fileBudget, budget);
    assert(!result->ok() && !result->sources);
    fixture.restore();
}

void retiredBuild(Fixture& fixture)
{
    auto prepared = AotCliSourcePreparation::load(fixture.manifest(), fileBudget, budget);
    verify(*prepared);
    // The returned owner has no live IO host. Reopening any original input
    // would now encounter invalid content rather than the sealed source.
    for (const auto* name : names)
        fixture.workspace.write(name, "deliberately invalid replacement");
    const auto built = scriptvm::ScriptAotPackageBuilder{}.build(
        prepared->loaded.package, *prepared->sources, fixture.workspace.root / "output");
    if (!built.ok())
        std::fprintf(stderr, "%s\n", built.error.message.c_str());
    assert(built.ok() && built.outputs.size() == 8);
    fixture.restore();
}
} // namespace

int main(int argc, char** argv)
{
    Fixture fixture;
    {
        const auto prepared = AotCliSourcePreparation::load(fixture.manifest(), fileBudget, budget);
        verify(*prepared);
    }
    if (argc > 1 && !std::strcmp(argv[1], "--smoke")) {
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i) {
            const auto prepared =
                AotCliSourcePreparation::load(fixture.manifest(), fileBudget, budget);
            verify(*prepared);
        }
        const auto after = core::defaultCpuAllocator().stats();
        assert(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
        std::printf("AOT CLI source core %llu/%llu -> %llu/%llu\n",
            (unsigned long long)before.liveBytes,
            (unsigned long long)(before.allocationCount - before.deallocationCount),
            (unsigned long long)after.liveBytes,
            (unsigned long long)(after.allocationCount - after.deallocationCount));
    } else {
        failures(fixture);
        retiredBuild(fixture);
    }
    std::puts(
        "PASS FileService AOT input, exact host identity, retired snapshot and original builder");
}
