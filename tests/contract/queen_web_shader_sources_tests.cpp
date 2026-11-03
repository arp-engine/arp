#include "core/encoding/yaml_contract.h"
#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "core/runtime/async_runtime.h"
#include "examples/build_a_queen/web_shader_source_task.h"
#include "shader/webgl_shader_program_artifact.h"
#include "tests/fixtures/shader_source_files.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>

namespace {
using namespace arp;
using namespace core::io;
using Task = build_a_queen::WebShaderSourceTask;
using Phase = build_a_queen::WebShaderSourcePhase;

using Corpus = shader_source_fixture::Files;

enum class Fault {
    None,
    Missing,
    Hash,
    Version,
    CancelBefore,
    CancelQueued,
    DestroyQueued,
    Remount,
    External,
    Closed
};

void cycle(const Corpus& corpus, const std::filesystem::path& root, Fault fault)
{
    auto backend = core::makeShared<MemoryFileBackend>(
        MemoryFileBackendDesc{256, Task::maxSourceBytes, 1, Task::maxFileBytes, 512});
    for (const auto& directory : corpus.directories)
        assert(backend->registerDirectory(directory) == FileIoStatus::Success);
    for (const auto& file : corpus.files) {
        if (fault == Fault::Missing && file.path == Task::publications[0])
            continue;
        auto bytes = file.bytes;
        if (fault == Fault::Hash
            && file.path == "Vertex/queen_portable_sky.vertexMain.entry.glsl-es-100")
            bytes[0] ^= 1;
        if (fault == Fault::Version && file.path == Task::publications[0]) {
            auto node = core::YamlContract::parseSingleDocument(
                {reinterpret_cast<const char*>(bytes.data()), bytes.size()});
            node["contractVersion"] = 99;
            const auto text = core::YamlContract::serialize(node);
            bytes.assign(text.begin(), text.end());
        }
        assert(
            backend->registerFile(file.path, bytes.data(), bytes.size()) == FileIoStatus::Success);
    }
    auto mounts = core::makeShared<MountedFileBackend>(
        MountedFileBackendDesc{1, 1, Task::maxFileBytes, 32, 512});
    const auto mount = mounts->mount("shaders", backend);
    assert(mount);
    core::runtime::AsyncRuntimeDesc desc;
    desc.completionQueueCapacity = desc.completionBudget = 2;
    desc.enableWork = false;
    desc.enableThreadPoolFileBackend = false;
    desc.fileBackend = mounts;
    core::runtime::AsyncRuntime runtime(std::move(desc));
    assert(runtime.valid());
    core::runtime::CancellationSource external;
    auto task = core::makeUnique<Task>(
        runtime.fileService(), runtime.completionQueue(), mount, external.token());
    assert(task->phase() == Phase::Pending && !task->source()
        && !runtime.telemetry().fileService.submitted);
    if (fault == Fault::CancelBefore)
        task->cancel();
    if (fault == Fault::Closed)
        runtime.fileService().shutdown();
    task->advance();
    if (fault == Fault::CancelQueued)
        task->cancel();
    if (fault == Fault::DestroyQueued)
        task.reset();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    bool remounted = false;
    while (task && task->phase() == Phase::Pending && std::chrono::steady_clock::now() < deadline) {
        assert(runtime.dispatch(1).success());
        if (fault == Fault::External)
            external.cancel();
        if (fault == Fault::Remount && !remounted && runtime.telemetry().fileService.submitted) {
            assert(mounts->unmount(mount));
            const auto replacement = mounts->mount("shaders", backend);
            assert(replacement && !(replacement == mount));
            remounted = true;
        }
        task->advance();
        std::this_thread::yield();
    }
    runtime.shutdown();
    assert(runtime.completionQueue().pending() == 0 && runtime.completionQueue().reserved() == 0);
    assert(!backend->telemetry().inFlight && !mounts->telemetry().inFlight);
    if (!task)
        return;
    if (fault != Fault::None) {
        assert(task->phase() == Phase::Failed || task->phase() == Phase::Cancelled);
        assert(!task->source() && !task->diagnostic().empty());
        return;
    }
    assert(task->phase() == Phase::Ready && task->source());
    assert(task->readCount() == task->fileCount() && task->readCount() > Task::publications.size());
    assert(task->bytesRead() > 0 && task->bytesRead() <= Task::maxSourceBytes);
    const auto missing = task->source()->read("not-acquired.yaml", Task::maxFileBytes);
    assert(missing.diagnostic && missing.bytes.empty());
    // IO is already closed. Both paths must produce identical immutable shader
    // identity and target bytes, including the existing matrix contract.
    for (const auto* publication : Task::publications) {
        shader::WebGlShaderLoadBudget budget{
            {Task::maxFileBytes, Task::maxMetadataBytes, Task::maxMetadataBytes, 32},
            256,
            32,
            1024};
        core::pmr::String diagnostic;
        auto reference = shader::WebGlShaderProgramArtifact::load(root / publication,
            budget,
            core::makeShared<core::CpuMemoryQuota>(Task::maxMetadataBytes),
            diagnostic);
        assert(reference);
        budget.original.source = task->source();
        auto acquired = shader::WebGlShaderProgramArtifact::load(publication,
            budget,
            core::makeShared<core::CpuMemoryQuota>(Task::maxMetadataBytes),
            diagnostic);
        assert(acquired && reference->identity() == acquired->identity());
        assert(reference->vertex().glsl == acquired->vertex().glsl
            && reference->fragment().glsl == acquired->fragment().glsl);
    }
}

void run(const std::filesystem::path& root, unsigned cycles)
{
    Corpus corpus(root, Task::maxFileBytes, 256);
    const auto exercise = [&] {
        for (const auto fault : {Fault::None,
                 Fault::Missing,
                 Fault::Hash,
                 Fault::Version,
                 Fault::CancelBefore,
                 Fault::CancelQueued,
                 Fault::DestroyQueued,
                 Fault::Remount,
                 Fault::External,
                 Fault::Closed})
            cycle(corpus, root, fault);
    };
    exercise();
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < cycles; ++i)
        exercise();
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
    std::printf("Queen shader sources: %u ordinary cycles; core %llu -> %llu bytes\n",
        cycles,
        (unsigned long long)before.liveBytes,
        (unsigned long long)after.liveBytes);
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc >= 2);
    // db::rawInstance deliberately retains builtin/native type metadata for
    // process lifetime. Initialize only those types before measuring owners.
    {
        resource::ResourceManager metadata;
        resource::ResourceBase carrier(resource::ResourceReference{});
        (void)reflection::object::undefined();
    }
    const auto before = core::defaultCpuAllocator().stats();
    run(argv[1], argc > 2 ? 20 : 1);
    const auto after = core::defaultCpuAllocator().stats();
    std::fprintf(stderr,
        "Queen shader final stats: before=%llu/%llu after=%llu/%llu\n",
        (unsigned long long)before.liveBytes,
        (unsigned long long)(before.allocationCount - before.deallocationCount),
        (unsigned long long)after.liveBytes,
        (unsigned long long)(after.allocationCount - after.deallocationCount));
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
    std::printf("Queen shader owners retired: core %llu bytes, %llu live\n",
        (unsigned long long)after.liveBytes,
        (unsigned long long)(after.allocationCount - after.deallocationCount));
}
