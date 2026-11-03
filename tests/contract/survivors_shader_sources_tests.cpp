#include "core/io/memory_file_backend.h"
#include "core/io/native_directory_file_backend.h"
#include "examples/survivors/shader_source_task.h"
#include "renderer/renderer_common.h"
#include "tests/fixtures/queen_source_runtime.h"
#include "tests/fixtures/shader_source_files.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {
using namespace arp;
using namespace core::io;
using Task = survivors::ShaderSourceTask;
using Limits = survivors::SceneShaderSources;
using Phase = examples::StartupResourcePhase;
enum class Fault {
    None,
    Missing,
    Hash,
    Version,
    CancelBefore,
    CancelQueued,
    Destroy,
    External,
    Stale,
    Closed,
    InvalidMount
};

void verify(const renderer::ShaderPackagePreparationDesc& input, const std::filesystem::path& root)
{
    core::pmr::String diagnostic;
    const auto original = shader::GeneratedShaderArtifactSet::load(root / Limits::publication,
        {Limits::maxFileBytes, Limits::maxSourceBytes, Limits::maxSourceBytes, 8, 4},
        core::makeShared<core::CpuMemoryQuota>(Limits::maxSourceBytes),
        diagnostic);
    assert(original && input.prepared && input.prepared->identity() == original->identity());
    for (const auto role : {shader::ShaderArtifactRole::Source,
             shader::ShaderArtifactRole::Spirv,
             shader::ShaderArtifactRole::Metal,
             shader::ShaderArtifactRole::Wgsl,
             shader::ShaderArtifactRole::Reflection})
        assert(input.prepared->bytes(role) == original->bytes(role));
    rhi::ShaderPackageDesc package;
    assert(renderer::detail::prepareGraphicsShaderPackage(package,
        "SurvivorsPrepared",
        Limits::module,
        16,
        input,
        rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment)
            .success);
    for (const auto target :
        {shader::TargetBackend::SpirV, shader::TargetBackend::Msl, shader::TargetBackend::Wgsl}) {
        rhi::GeneratedShaderTargetInput selected;
        assert(rhi::selectGeneratedShaderPackageTarget(package, target, selected).success);
        assert(selected.bytes && !selected.bytes->empty());
    }
}

void run(queen_source_fixture::Runtime fixture, const std::filesystem::path& root, Fault fault)
{
    auto& runtime = *fixture.owner;
    core::runtime::CancellationSource external;
    auto task = core::makeUnique<Task>(runtime.fileService(),
        runtime.completionQueue(),
        fault == Fault::InvalidMount ? core::PoolHandle{} : fixture.mount,
        external.token());
    assert(task->phase() == Phase::Pending && !task->readCount() && !task->preparation().prepared);
    if (fault == Fault::CancelBefore)
        task->cancel();
    if (fault == Fault::Closed)
        runtime.fileService().shutdown();
    task->advance();
    if (fault == Fault::CancelQueued)
        task->cancel();
    if (fault == Fault::Destroy)
        task.reset();
    bool unmounted = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (task && task->phase() == Phase::Pending && std::chrono::steady_clock::now() < deadline) {
        assert(runtime.dispatch(1).success());
        if (fault == Fault::External)
            external.cancel();
        if (fault == Fault::Stale && !unmounted) {
            assert(fixture.mounts->unmount(fixture.mount));
            unmounted = true;
        }
        task->advance();
        std::this_thread::yield();
    }
    runtime.shutdown();
    assert(!runtime.completionQueue().pending() && !runtime.completionQueue().reserved());
    assert(!fixture.mounts->telemetry().inFlight);
    if (!task)
        return;
    if (fault != Fault::None) {
        assert(task->phase() == Phase::Failed || task->phase() == Phase::Cancelled);
        assert(!task->preparation().prepared && !task->diagnostic().empty());
        if (task->phase() == Phase::Cancelled)
            assert(task->diagnostic() == "SURVIVORS-STARTUP-RESOURCE-CANCELLED");
        return;
    }
    if (task->phase() != Phase::Ready)
        std::fprintf(stderr,
            "survivors shader: %.*s\n",
            int(task->diagnostic().size()),
            task->diagnostic().data());
    assert(task->phase() == Phase::Ready && task->readCount() == Limits::maxFiles);
    const auto input = task->preparation();
    task.reset();
    fixture.owner.reset();
    fixture.mounts.reset();
    // No task, ResourceManager, mount or IO runtime is needed to consume input.
    verify(input, root);
}

void cycle(
    const shader_source_fixture::Files& corpus, const std::filesystem::path& root, Fault fault)
{
    auto memory = core::makeShared<MemoryFileBackend>(
        MemoryFileBackendDesc{32, Limits::maxSourceBytes, 1, Limits::maxFileBytes, 512});
    for (const auto& directory : corpus.directories)
        assert(memory->registerDirectory(directory) == FileIoStatus::Success);
    for (const auto& file : corpus.files) {
        if (fault == Fault::Missing && file.path == Limits::publication)
            continue;
        auto bytes = file.bytes;
        if (fault == Fault::Hash && file.path == "scene_2d_sprites.spv")
            bytes[0] ^= 1;
        if (fault == Fault::Version && file.path == Limits::publication) {
            auto node = shader::shaderArtifactYaml(shader::shaderArtifactText(bytes));
            node["contractVersion"] = 99;
            const auto text = core::YamlContract::serialize(node);
            bytes.assign(text.begin(), text.end());
        }
        assert(
            memory->registerFile(file.path, bytes.data(), bytes.size()) == FileIoStatus::Success);
    }
    run(queen_source_fixture::create(memory, Limits::maxFileBytes), root, fault);
    assert(!memory->telemetry().inFlight);
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc >= 2);
    const bool http = std::strcmp(argv[1], "--http") == 0;
    const bool native = argc > 2 && std::strcmp(argv[2], "--native") == 0;
    const std::filesystem::path root = http ? ARP_SURVIVORS_SOURCE_SHADER_DIR : argv[1];
    assert(!std::filesystem::exists(Limits::module));
    if (http) {
        assert(argc == 3);
        run(queen_source_fixture::create(argv[2], Limits::maxFileBytes), root, Fault::None);
    } else if (native) {
        NativeDirectoryFileBackendDesc desc;
        desc.rootPath = core::FilePath::utf8(root);
        desc.maxInFlight = 1;
        desc.maxBytesInFlight = Limits::maxFileBytes;
        desc.maxPathBytes = 512;
        desc.ioChunkBytes = 16384;
        desc.directoryBufferBytes = 65536;
        desc.maxScanEntries = 32;
        desc.maxScanNameBytes = 32 * desc.maxPathBytes;
        run(queen_source_fixture::create(
                core::makeShared<NativeDirectoryFileBackend>(desc), Limits::maxFileBytes),
            root,
            Fault::None);
    } else {
        shader_source_fixture::Files corpus(root, Limits::maxFileBytes, 32);
        cycle(corpus, root, Fault::None);
        {
            auto memory = core::makeShared<MemoryFileBackend>(
                MemoryFileBackendDesc{1, Limits::maxFileBytes, 1, Limits::maxFileBytes, 512});
            auto fixture = queen_source_fixture::create(memory, Limits::maxFileBytes);
            const auto before = core::defaultCpuAllocator().stats();
            bool rejected = false;
            try {
                examples::StartupResourceTask invalid(
                    fixture.owner->fileService(), fixture.owner->completionQueue(), {}, {}, {});
            } catch (const core::DiagnosticError& error) {
                rejected = std::strcmp(error.what(), "EXAMPLE-STARTUP-DIAGNOSTICS-INVALID") == 0;
            }
            assert(rejected);
            const auto after = core::defaultCpuAllocator().stats();
            assert(before.liveBytes == after.liveBytes);
        }
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < (argc > 2 ? 20u : 1u); ++i)
            for (const auto fault : {Fault::None,
                     Fault::Missing,
                     Fault::Hash,
                     Fault::Version,
                     Fault::CancelBefore,
                     Fault::CancelQueued,
                     Fault::Destroy,
                     Fault::External,
                     Fault::Stale,
                     Fault::Closed,
                     Fault::InvalidMount})
                cycle(corpus, root, fault);
        const auto after = core::defaultCpuAllocator().stats();
        std::printf("Survivors shader core=%llu/%llu -> %llu/%llu\n",
            (unsigned long long)before.liveBytes,
            (unsigned long long)(before.allocationCount - before.deallocationCount),
            (unsigned long long)after.liveBytes,
            (unsigned long long)(after.allocationCount - after.deallocationCount));
        assert(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
    }
    std::puts(
        "PASS original Survivors shader, six explicit reads, IO retired before consumption; no GPU claim");
}
