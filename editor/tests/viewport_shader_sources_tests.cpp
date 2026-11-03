#include "core/encoding/yaml_contract.h"
#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "core/io/memory_file_backend.h"
#include "editor/app/editor_viewport_shader_host.h"
#include "editor/viewport/editor_scene_viewport_environment.h"
#include "rhi/backends/null/null_backend.h"
#include "tests/fixtures/queen_source_runtime.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace {
using namespace arp;
using Limits = editor::EditorViewportShaderSources;
using Y = core::YamlContract;
using namespace core::io;

struct Corpus {
    struct File {
        core::pmr::String path;
        core::pmr::Vector<std::uint8_t> bytes;
    };

    core::pmr::Vector<File> files;
    std::array<core::SharedPtr<const shader::GeneratedShaderArtifactSet>, 3> originals;

    explicit Corpus(const std::filesystem::path& root)
    {
        auto add = [&](const std::filesystem::path& relative) {
            auto input = core::readBinaryFile(root / relative, Limits::maxFileBytes);
            assert(input.success);
            files.push_back({core::FilePath::utf8(relative), std::move(input.bytes)});
        };
        for (std::size_t i = 0; i < Limits::publications.size(); ++i) {
            const std::filesystem::path publication(Limits::publications[i]);
            add(publication);
            const auto& data = files.back().bytes;
            const auto node = Y::Node::deserialize(
                core::pmr::String(reinterpret_cast<const char*>(data.data()), data.size()));
            const auto& entries = Y::required(node, "files");
            for (const auto* role : {"source", "spirv", "metal", "wgsl", "reflection"})
                add(publication.parent_path() / Y::text(Y::required(entries, role), "path"));
            core::pmr::String diagnostic;
            originals[i] = shader::GeneratedShaderArtifactSet::load(root / publication,
                {Limits::maxFileBytes, Limits::maxSourceBytes, 8u * 1024u * 1024u, 256, 8},
                core::makeShared<core::CpuMemoryQuota>(8u * 1024u * 1024u),
                diagnostic);
            assert(originals[i]);
        }
        assert(files.size() == 18);
    }
};

void verify(const Limits& input, const Corpus& corpus)
{
    assert(input.valid());
    for (std::size_t i = 0; i < input.programs.size(); ++i) {
        assert(input.programs[i].prepared->identity() == corpus.originals[i]->identity());
        for (const auto role : {shader::ShaderArtifactRole::Source,
                 shader::ShaderArtifactRole::Spirv,
                 shader::ShaderArtifactRole::Metal,
                 shader::ShaderArtifactRole::Wgsl,
                 shader::ShaderArtifactRole::Reflection})
            assert(input.programs[i].prepared->bytes(role) == corpus.originals[i]->bytes(role));
    }
    // Logical paths do not exist on disk. Supplying immutable preparation must
    // reach the original ABI/capability gate without attempting a cold read.
    auto artifacts = input.artifacts(0);
    const auto package = editor::loadEditorSceneViewportShaderPackage(
        artifacts, rhi::TextureFormat::Rgba16Float, rhi::TextureFormat::D32Float, 1);
    assert(package.valid());
    auto device = rhi::createNullDevice();
    assert(!editor::createEditorSceneViewportDevicePipeline(*device, package).valid());
    editor::EditorSceneViewportEnvironment environment;
    core::pmr::String diagnostic;
    assert(!environment.initialize(*device, input.artifacts(2), diagnostic));
    assert(diagnostic == "ARP-EDITOR-ENVIRONMENT-EXECUTION-UNAVAILABLE");
    artifacts.preparation.prepared.reset();
    auto reused = package.package;
    assert(!editor::hydrateEditorSceneViewportShaderPackage(reused, artifacts, diagnostic));
    assert(!reused.generatedReflectionHydrated);
    assert(!editor::loadEditorSceneViewportShaderPackage(
        artifacts, rhi::TextureFormat::Rgba16Float, rhi::TextureFormat::D32Float, 1)
            .valid());
    artifacts = input.artifacts(0);
    artifacts.preparation.publication.clear();
    assert(!editor::loadEditorSceneViewportShaderPackage(
        artifacts, rhi::TextureFormat::Rgba16Float, rhi::TextureFormat::D32Float, 1)
            .valid());
    artifacts = input.artifacts(0);
    artifacts.preparation.prepared = input.programs[1].prepared;
    assert(!editor::loadEditorSceneViewportShaderPackage(
        artifacts, rhi::TextureFormat::Rgba16Float, rhi::TextureFormat::D32Float, 1)
            .valid());
}

enum class Fault { None, Missing, Hash, Version, Cancel, Destroy, Stale, Closed, Invalid, Budget };

Limits execute(queen_source_fixture::Runtime fixture, Fault fault)
{
    resource::ResourceManager manager;
    core::runtime::CancellationSource cancellation;
    resource::ResourceGraphExecutionResult result;
    bool done = false;

    struct Completion {
        bool& done;
        resource::ResourceGraphExecutionResult& result;
    } completed{done, result};

    auto work = Limits::request(
        fault == Fault::Invalid ? core::PoolHandle{} : fixture.mount, cancellation.token());
    if (fault == Fault::Budget)
        work.budget.maxFileReads = 1;
    if (fault == Fault::Closed)
        fixture.owner->fileService().shutdown();
    resource::ResourceGraphExecutor executor(
        manager, fixture.owner->fileService(), fixture.owner->completionQueue());
    const auto accepted = executor.submit(std::move(work),
        {[](void* context, resource::ResourceGraphExecutionResult value) {
             auto& target = *static_cast<Completion*>(context);
             target.result = std::move(value);
             target.done = true;
         },
            &completed});
    if (accepted == resource::ResourceGraphSubmitStatus::Accepted) {
        if (fault == Fault::Cancel || fault == Fault::Destroy)
            cancellation.cancel();
        if (fault == Fault::Stale)
            assert(fixture.mounts->unmount(fixture.mount));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        if (fault == Fault::Destroy)
            fixture.owner.reset();
        else
            while (!done && std::chrono::steady_clock::now() < deadline)
                assert(fixture.owner->dispatch().success());
        assert(done);
    } else
        assert(fault != Fault::None);
    if (fixture.owner) {
        fixture.owner->shutdown();
        assert(!fixture.owner->completionQueue().pending()
            && !fixture.owner->completionQueue().reserved());
    }
    auto prepared = Limits::capture(manager);
    if (fault == Fault::None) {
        if (!result.resource.success)
            std::fprintf(stderr, "viewport sources: %s\n", result.resource.diagnostics.c_str());
        assert(done && result.resource.success && result.telemetry.submittedFileReads == 18
            && prepared.valid());
    } else
        assert(!prepared.valid() && !result.resource.success);
    return prepared;
}

void cycle(const Corpus& corpus, Fault fault)
{
    auto memory = core::makeShared<MemoryFileBackend>(MemoryFileBackendDesc{
        Limits::maxFiles, Limits::maxSourceBytes, 1, Limits::maxFileBytes, 512});
    assert(memory->registerDirectory("editor_environment") == FileIoStatus::Success);
    for (const auto& file : corpus.files) {
        if (fault == Fault::Missing && file.path == Limits::publications[0])
            continue;
        auto bytes = file.bytes;
        if (fault == Fault::Hash && file.path == "editor_viewport_pbr.spv")
            bytes[0] ^= 1;
        if (fault == Fault::Version && file.path == Limits::publications[0]) {
            auto node = Y::Node::deserialize(
                core::pmr::String(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
            node["contractVersion"] = 99;
            const auto text = Y::serialize(node);
            bytes.assign(text.begin(), text.end());
        }
        assert(
            memory->registerFile(file.path, bytes.data(), bytes.size()) == FileIoStatus::Success);
    }
    const auto input = execute(queen_source_fixture::create(memory, Limits::maxFileBytes), fault);
    if (fault == Fault::None)
        verify(input, corpus);
    assert(!memory->telemetry().inFlight);
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc >= 2);
    const bool http = std::strcmp(argv[1], "--http") == 0;
    Corpus corpus(http ? ARP_EDITOR_SHADER_SOURCE_ROOT : argv[1]);
    if (http) {
        assert(argc == 3);
        auto input =
            execute(queen_source_fixture::create(argv[2], Limits::maxFileBytes), Fault::None);
        verify(input, corpus);
    } else {
        cycle(corpus, Fault::None);
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < (argc > 2 ? 20u : 1u); ++i)
            for (const auto fault : {Fault::None,
                     Fault::Missing,
                     Fault::Hash,
                     Fault::Version,
                     Fault::Cancel,
                     Fault::Destroy,
                     Fault::Stale,
                     Fault::Closed,
                     Fault::Invalid,
                     Fault::Budget})
                cycle(corpus, fault);
        const auto after = core::defaultCpuAllocator().stats();
        std::printf("viewport source core %llu/%llu -> %llu/%llu\n",
            (unsigned long long)before.liveBytes,
            (unsigned long long)(before.allocationCount - before.deallocationCount),
            (unsigned long long)after.liveBytes,
            (unsigned long long)(after.allocationCount - after.deallocationCount));
        assert(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
    }
    std::puts(
        "PASS Editor viewport sources18, immutable targets after IO retirement, original Null gate");
}
