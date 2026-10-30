#include "core/encoding/yaml_contract.h"
#include "core/hash/sha256.h"
#include "core/io/file_io.h"
#include "core/io/http_file_backend.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "core/runtime/async_runtime.h"
#include "scriptvm/aot/aot_package.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <cassert>
#include <cstdio>

namespace {
using namespace arp;
using namespace scriptvm;
using namespace core::io;
constexpr std::uint64_t fileBytes = 65536;
constexpr ScriptSourceBudget budget{2, fileBytes, 2 * fileBytes, 2 * fileBytes};
constexpr const char* mainText =
    "from math_dep import multiply\n\ndef run(value: int) -> int:\n    return multiply(value, 3)\n";
constexpr const char* dependencyText = "def multiply(a: int, b: int) -> int:\n    return a * b\n";

FileReadRequest binding(core::StringView path = "snapshot/package.yaml")
{
    FileReadRequest request;
    request.pathScope = FilePathScope::HostFilesystem;
    request.path = path;
    request.contractVersion = 1;
    request.maxBytes = fileBytes;
    return request;
}

FileReadResult bytes(core::StringView text)
{
    FileReadResult result;
    result.bytes.assign(text.begin(), text.end());
    return result;
}

class Fixture final {
public:
    Fixture()
    {
        auto input = core::readTextFile(ARP_SCRIPT_SOURCE_MANIFEST, fileBytes);
        assert(input.success);
        auto root = core::YamlContract::parseSingleDocument(input.text);
        root["modules"][0]["entries"] = core::YamlContract::Node::sequence({"run"});
        manifest = core::YamlContract::serialize(root);
        auto loaded = ScriptAotPackageBuilder::load(binding(), bytes(manifest), fileBytes);
        if (!loaded.ok())
            std::fprintf(stderr, "%s\n", loaded.error.message.c_str());
        assert(loaded.ok());
        package = std::move(loaded.package);
        assert(package.modules.size() == 2);
        assert(!std::filesystem::exists(package.modules[0].source));
    }

    ScriptSourceSnapshot snapshot(bool bom = false)
    {
        ScriptSourceSnapshot result(budget);
        const char* diagnostic = nullptr;
        for (std::size_t i = 0; i < package.modules.size(); ++i) {
            core::pmr::String source = bom ? "\xEF\xBB\xBF" : "";
            source += i ? dependencyText : mainText;
            assert(result.add(package.modules[i].moduleId,
                binding(core::FilePath::utf8(package.modules[i].source)),
                bytes(source),
                diagnostic));
        }
        assert(result.seal(diagnostic));
        return result;
    }

    DirectNativeGeneratedModuleV5 compile(
        const ScriptAotPackageDefinition& definition, const ScriptSourceSnapshot& sources)
    {
        // Build once to obtain the real existing package identity, then verify
        // compileModule consumes exactly that identity and the sealed bytes.
        unified_io_fixture::Workspace workspace;
        const auto built =
            ScriptAotPackageBuilder().build(definition, sources, workspace.root / "out");
        if (!built.ok())
            std::fprintf(stderr, "%s\n", built.error.message.c_str());
        assert(built.ok() && !built.packageIdentity.empty() && !built.outputs.empty());
        auto generated = ScriptAotCompiler().compileModule(definition,
            definition.modules.front().moduleId,
            sources,
            built.packageIdentity,
            "basic.gen.h");
        if (!generated.ok())
            std::fprintf(stderr, "%s\n", generated.error.message.c_str());
        assert(generated.ok());
        assert(generated.manifest.modules[0].sourceHash == core::Sha256::hexDigest(mainText));
        return generated;
    }

    core::pmr::String manifest;
    ScriptAotPackageDefinition package;
};

void contracts(Fixture& fixture)
{
    auto sources = fixture.snapshot();
    const auto original = fixture.compile(fixture.package, sources);
    auto withBom = fixture.snapshot(true);
    const auto decoded = fixture.compile(fixture.package, withBom);
    assert(original.header == decoded.header && original.source == decoded.source);
    assert(original.manifest.packageIdentity == decoded.manifest.packageIdentity);
    assert(withBom.sourceBytes() == sources.sourceBytes() + 6);
    assert(withBom.textBytes() == sources.textBytes());
    const char* diagnostic = nullptr;
    assert(!sources.add("late", binding(), bytes("late"), diagnostic));
    assert(core::StringView(diagnostic) == "ARP-SCRIPT-SOURCE-SEALED");
    assert(!ScriptAotPackageBuilder::load("does-not-exist.yaml").ok());
    assert(!ScriptAotPackageBuilder::load("does-not-exist.yaml", UINT64_MAX).ok());
    unified_io_fixture::Workspace workspace;
    const auto output = workspace.root / "must-not-exist";
    assert(!ScriptAotPackageBuilder().build(fixture.package, output).ok());
    assert(!ScriptAotCompiler()
            .compileModule(fixture.package, fixture.package.modules[0], "id", "header")
            .ok());
    {
        ScriptSourceSnapshot wrongPath(budget);
        for (const auto& module : fixture.package.modules)
            assert(wrongPath.add(module.moduleId,
                binding("explicit-wrong-source.arp"),
                bytes(module.moduleId == "basic" ? mainText : dependencyText),
                diagnostic));
        assert(wrongPath.seal(diagnostic));
        assert(!ScriptAotPackageBuilder().build(fixture.package, wrongPath, output).ok());
        assert(!std::filesystem::exists(output));
    }
    assert(!std::filesystem::exists(output));
    ScriptSourceSnapshot incomplete(budget);
    assert(!ScriptAotPackageBuilder().build(fixture.package, incomplete, output).ok());
    assert(!std::filesystem::exists(output));
    assert(!ScriptAotCompiler()
            .compileModule(fixture.package, "basic", sources, "wrong", "header")
            .ok());
    for (const auto text : {core::StringView("\xFF"), core::StringView("a\0b", 3)}) {
        ScriptSourceSnapshot invalid(budget);
        assert(!invalid.add("basic", binding(), bytes(text), diagnostic) && diagnostic);
        assert(invalid.size() == 0 && invalid.sourceBytes() == 0 && invalid.textBytes() == 0);
    }
    for (unsigned scenario = 0; scenario < 11; ++scenario) {
        auto desc = budget;
        auto request = binding();
        auto input = bytes(mainText);
        core::runtime::CancellationSource cancellation;
        if (scenario == 0)
            desc.maxFiles = 0;
        if (scenario == 1)
            desc.maxFileBytes = UINT64_MAX;
        if (scenario == 2)
            desc.maxSourceBytes = 1;
        if (scenario == 3)
            desc.maxTextBytes = 1;
        if (scenario == 4)
            request.contractVersion = 0;
        if (scenario == 5)
            request.range = FileReadRange::exactRange(0, 1);
        if (scenario == 6) {
            cancellation.cancel();
            request.cancellation = cancellation.token();
        }
        if (scenario == 7) {
            input.status = FileIoStatus::StaleLocation;
            input.diagnosticId = "IO-MOUNT-STALE";
        }
        if (scenario == 8) {
            request.expectedVersion.kind = FileVersionKind::ContentSha256;
            request.maxVersionBytes = fileBytes;
        }
        if (scenario == 9)
            input.version.kind = FileVersionKind::ContentSha256;
        if (scenario == 10)
            request.path.assign("bad\0path", 8);
        ScriptSourceSnapshot invalid(desc);
        assert(!invalid.add("basic", request, std::move(input), diagnostic) && diagnostic);
        assert(invalid.size() == 0 && invalid.sourceBytes() == 0 && invalid.textBytes() == 0);
    }
    for (bool strongVersion : {false, true}) {
        ScriptSourceSnapshot candidate(budget);
        auto input = bytes(mainText);
        auto request = binding();
        if (strongVersion) {
            const auto digest = core::Sha256::digest(mainText);
            input.version.kind = FileVersionKind::ContentSha256;
            std::copy(digest.begin(), digest.end(), input.version.digest.begin());
            request.expectedVersion = input.version;
            request.maxVersionBytes = fileBytes;
        }
        auto quota = core::makeShared<core::CpuMemoryQuota>(1);
        {
            core::CpuMemoryQuotaScope scope(quota);
            assert(!candidate.add("basic", request, std::move(input), diagnostic));
        }
        assert(candidate.size() == 0 && candidate.sourceBytes() == 0 && quota->liveBytes() == 0);
    }
    {
        ScriptSourceSnapshot candidate(budget);
        const auto request = binding();
        assert(candidate.add("basic", request, bytes(mainText), diagnostic));
        assert(!candidate.find("basic"));
        const auto retained = candidate.sourceBytes();
        assert(!candidate.add("basic", request, bytes(dependencyText), diagnostic));
        assert(candidate.size() == 1 && candidate.sourceBytes() == retained);
        assert(candidate.seal(diagnostic));
        assert(!ScriptAotPackageBuilder().build(fixture.package, candidate, output).ok());
        assert(!std::filesystem::exists(output));
    }
    {
        ScriptSourceSnapshot candidate(budget);
        core::runtime::CancellationSource cancellation;
        auto request = binding();
        request.cancellation = cancellation.token();
        assert(candidate.add("basic", request, bytes(mainText), diagnostic));
        cancellation.cancel();
        assert(!candidate.seal(diagnostic) && !candidate.sealed());
    }
    // Real class import merging uses the same captured dependency text too.
    const core::StringView classMain =
        "from math_dep import Counter\n\ndef run(value: int) -> int:\n    var item: Counter = Counter()\n    return item.step(value)\n";
    const core::StringView classDep =
        "class Counter:\n    def step(self, value: int) -> int:\n        return value + 3\n\ndef multiply(a: int, b: int) -> int:\n    return a * b\n";
    ScriptSourceSnapshot classes(budget);
    for (unsigned i = 0; i < 2; ++i)
        assert(classes.add(fixture.package.modules[i].moduleId,
            binding(core::FilePath::utf8(fixture.package.modules[i].source)),
            bytes(i ? classDep : classMain),
            diagnostic));
    assert(classes.seal(diagnostic));
    const auto built =
        ScriptAotPackageBuilder().build(fixture.package, classes, workspace.root / "classes");
    if (!built.ok())
        std::fprintf(stderr, "class import: %s\n", built.error.message.c_str());
    assert(built.ok() && built.packageIdentity != original.manifest.packageIdentity);
}

struct ReadCompletion {
    unsigned calls = 0;
    FileReadResult result;

    static void complete(void* user, FileReadResult value)
    {
        auto& self = *static_cast<ReadCompletion*>(user);
        ++self.calls;
        self.result = std::move(value);
    }
};

void cycle(Fixture& fixture, core::SharedPtr<ManagedFileBackend> backend)
{
    auto mounts =
        core::makeShared<MountedFileBackend>(MountedFileBackendDesc{1, 1, fileBytes, 32, 256});
    const auto mount = mounts->mount("script", backend);
    assert(mount);
    core::runtime::AsyncRuntimeDesc desc;
    desc.enableWork = desc.enableThreadPoolFileBackend = false;
    desc.fileBackend = mounts;
    desc.completionBudget = desc.completionQueueCapacity = 2;
    core::runtime::AsyncRuntime runtime(desc);
    assert(runtime.valid());
    const auto read = [&](core::StringView path) {
        auto request = binding(path);
        request.pathScope = FilePathScope::MountedResource;
        request.mount = mount;
        ReadCompletion completion;
        assert(runtime.fileService().read(request, {ReadCompletion::complete, &completion})
            == FileIoStatus::Accepted);
        assert(completion.calls == 0);
        unified_io_fixture::wait(runtime.completionQueue(), [&] {
            return completion.calls == 1;
        });
        assert(completion.result.status == FileIoStatus::Success);
        return std::make_pair(std::move(request), std::move(completion.result));
    };
    auto manifest = read("snapshot/package.yaml");
    auto loaded =
        ScriptAotPackageBuilder::load(manifest.first, std::move(manifest.second), fileBytes);
    assert(loaded.ok());
    ScriptSourceSnapshot sources(budget);
    const char* diagnostic = nullptr;
    for (const auto& module : loaded.package.modules) {
        auto acquired = read(core::FilePath::utf8(module.source));
        assert(
            sources.add(module.moduleId, acquired.first, std::move(acquired.second), diagnostic));
    }
    assert(sources.seal(diagnostic));
    const auto generated = fixture.compile(loaded.package, sources);
    auto expected = fixture.snapshot();
    const auto direct = fixture.compile(fixture.package, expected);
    assert(generated.source == direct.source && generated.header == direct.header);
    assert(generated.manifest.packageIdentity == direct.manifest.packageIdentity);
    runtime.shutdown();
    assert(runtime.completionQueue().pending() == 0 && runtime.completionQueue().reserved() == 0);
    assert(!mounts->telemetry().inFlight);
}

core::SharedPtr<MemoryFileBackend> memory(Fixture& fixture)
{
    auto backend = core::makeShared<MemoryFileBackend>(
        MemoryFileBackendDesc{4, 3 * fileBytes, 1, fileBytes, 256});
    assert(backend->registerDirectory("snapshot") == FileIoStatus::Success);
    for (const auto& item : {std::make_pair(core::StringView("snapshot/package.yaml"),
                                 core::StringView(fixture.manifest)),
             std::make_pair(core::StringView("snapshot/basic.arp"), core::StringView(mainText)),
             std::make_pair(
                 core::StringView("snapshot/math_dep.arp"), core::StringView(dependencyText))})
        assert(backend->registerFile(item.first,
                   reinterpret_cast<const std::uint8_t*>(item.second.data()),
                   item.second.size())
            == FileIoStatus::Success);
    return backend;
}

void native(Fixture& fixture)
{
    unified_io_fixture::Workspace workspace;
    std::filesystem::create_directory(workspace.root / "snapshot");
    workspace.write("snapshot/package.yaml", fixture.manifest);
    workspace.write("snapshot/basic.arp", mainText);
    workspace.write("snapshot/math_dep.arp", dependencyText);
    cycle(fixture, core::makeShared<NativeDirectoryFileBackend>(workspace.descriptor()));
    const auto loaded =
        ScriptAotPackageBuilder::load(workspace.root / "snapshot/package.yaml", fileBytes);
    assert(loaded.ok());
    ScriptSourceSnapshot captured(budget);
    assert(ScriptAotPackageBuilder::captureHostSources(loaded.package, captured).ok());
    workspace.write("snapshot/basic.arp", "invalid edited source");
    std::filesystem::remove(workspace.root / "snapshot/math_dep.arp");
    assert(ScriptAotPackageBuilder::validate(loaded.package).ok());
    // No repeated stat/read can observe these now changed/missing backing files.
    const auto output = fixture.compile(loaded.package, captured);
    assert(output.ok());
    ScriptSourceSnapshot missing(budget);
    assert(!ScriptAotPackageBuilder::captureHostSources(loaded.package, missing).ok());
    assert(!missing.sealed());
}
} // namespace

int main(int argc, char** argv)
{
    Fixture fixture;
    const bool quick = argc == 2 && core::StringView(argv[1]) == "--quick";
    const char* url = argc == 3 && core::StringView(argv[1]) == "--http" ? argv[2] : nullptr;
    if (argc == 3 && core::StringView(argv[1]) == "--write-fixtures") {
        std::filesystem::create_directory(std::filesystem::path(argv[2]) / "snapshot");
        for (const auto& item :
            {std::make_pair("snapshot/package.yaml", core::StringView(fixture.manifest)),
                std::make_pair("snapshot/basic.arp", core::StringView(mainText)),
                std::make_pair("snapshot/math_dep.arp", core::StringView(dependencyText))}) {
            std::ofstream file(std::filesystem::path(argv[2]) / item.first, std::ios::binary);
            file.write(item.second.data(), item.second.size());
            assert(file.good());
        }
        return 0;
    }
    assert(argc == 1 || quick || url);
    if (!url) {
        contracts(fixture);
#if !defined(__EMSCRIPTEN__)
        native(fixture);
#endif
    }
    const auto run = [&] {
        if (!url) {
            cycle(fixture, memory(fixture));
            return;
        }
        HttpFileBackendDesc desc;
        desc.baseUrl = url;
        desc.maxInFlight = 1;
        // One source output plus HTTP header/chunk/path/transfer reservations.
        desc.maxBytesInFlight = 2 * fileBytes;
        desc.maxPathBytes = 256;
        desc.maxHeaderBytes = 8192;
        desc.ioChunkBytes = 1024;
        desc.timeoutMs = 3000;
        desc.cancellationPollMs = 2;
        auto backend = core::makeShared<HttpFileBackend>(desc);
        cycle(fixture, backend);
        assert(!backend->telemetry().inFlight && !backend->telemetry().activeNativeTransfers);
    };
    if (quick) {
        run();
        std::puts("ScriptPackageSource/1 quick PASS");
        return 0;
    }
    for (unsigned i = 0; i < 20; ++i)
        run();
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i)
        run();
    const auto after = core::defaultCpuAllocator().stats();
    std::fprintf(stderr,
        "Script source 20+20: core bytes %llu -> %llu; live %llu -> %llu\n",
        static_cast<unsigned long long>(before.liveBytes),
        static_cast<unsigned long long>(after.liveBytes),
        static_cast<unsigned long long>(before.allocationCount - before.deallocationCount),
        static_cast<unsigned long long>(after.allocationCount - after.deallocationCount));
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
}
