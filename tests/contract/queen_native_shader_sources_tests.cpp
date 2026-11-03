#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "core/runtime/async_runtime.h"
#include "examples/build_a_queen/native_shader_source_task.h"
#include "shader/shader_artifact_files.h"

#include <cassert>
#include <cstdio>

namespace {
using namespace arp;
using namespace core::io;
using Sources = build_a_queen::NativeShaderSources;
using Task = build_a_queen::NativeShaderSourceTask;
using Phase = build_a_queen::StartupResourcePhase;
using Corpus = core::pmr::Map<core::pmr::String, core::ByteBuffer>;
using Y = core::YamlContract;
enum class Fault { None, Missing, Hash, Version, Cancel, Destroy, Stale, Closed };

Corpus corpus(const std::filesystem::path& root)
{
    Corpus files;
    const auto add = [&](const core::pmr::String& path) {
        assert(core::FilePath::isSafeRelativePath(path));
        auto input = core::readBinaryFile(root / path.c_str(), Sources::maxFileBytes);
        assert(input.success && files.emplace(path, std::move(input.bytes)).second);
    };
    for (const auto* publication : Sources::publications) {
        add(publication);
        const auto node =
            shader::shaderArtifactYaml(shader::shaderArtifactText(files.at(publication)));
        const auto& entries = Y::required(node, "files");
        for (const auto* role : {"source", "spirv", "metal", "wgsl", "reflection"}) {
            const auto& entry = Y::required(entries, role);
            if (entry.contains("path"))
                add(Y::text(entry, "path"));
        }
    }
    return files;
}

void cycle(const Corpus& files, const std::filesystem::path& root, Fault fault)
{
    auto memory = core::makeShared<MemoryFileBackend>(MemoryFileBackendDesc{
        Sources::maxFiles, Sources::maxSourceBytes, 1, Sources::maxFileBytes, 512});
    for (const auto& [name, original] : files) {
        if (fault == Fault::Missing && name == Sources::publications[0])
            continue;
        auto bytes = original;
        if (fault == Fault::Hash && name == "queen_scene.spv")
            bytes[0] ^= 1;
        if (fault == Fault::Version && name == Sources::publications[0]) {
            auto node = shader::shaderArtifactYaml(shader::shaderArtifactText(bytes));
            node["contractVersion"] = 99;
            const auto text = Y::serialize(node);
            bytes.assign(text.begin(), text.end());
        }
        assert(memory->registerFile(name, bytes.data(), bytes.size()) == FileIoStatus::Success);
    }
    auto mounts = core::makeShared<MountedFileBackend>(
        MountedFileBackendDesc{1, 1, Sources::maxFileBytes, 32, 512});
    const auto mount = mounts->mount("programs", memory);
    assert(mount);
    core::runtime::AsyncRuntimeDesc desc;
    desc.completionQueueCapacity = desc.completionBudget = 2;
    desc.enableWork = desc.enableThreadPoolFileBackend = false;
    desc.fileBackend = mounts;
    core::runtime::AsyncRuntime runtime(std::move(desc));
    assert(runtime.valid());
    auto task = core::makeUnique<Task>(runtime.fileService(), runtime.completionQueue(), mount);
    assert(!task->sources().valid() && !task->readCount());
    if (fault == Fault::Closed)
        runtime.fileService().shutdown();
    task->advance();
    if (fault == Fault::Cancel)
        task->cancel();
    if (fault == Fault::Destroy)
        task.reset();
    bool remounted = false;
    for (unsigned n = 0; task && task->phase() == Phase::Pending && n < 10000; ++n) {
        assert(runtime.dispatch(1).success());
        if (fault == Fault::Stale && !remounted && runtime.telemetry().fileService.submitted) {
            assert(mounts->unmount(mount) && mounts->mount("programs", memory));
            remounted = true;
        }
        task->advance();
    }
    runtime.shutdown();
    assert(!runtime.completionQueue().pending() && !runtime.completionQueue().reserved());
    assert(!memory->telemetry().inFlight && !mounts->telemetry().inFlight);
    if (!task)
        return;
    if (fault != Fault::None) {
        assert(task->phase() == Phase::Failed || task->phase() == Phase::Cancelled);
        assert(!task->sources().valid() && !task->diagnostic().empty());
        return;
    }
    if (task->phase() != Phase::Ready)
        std::fprintf(stderr, "%.*s\n", int(task->diagnostic().size()), task->diagnostic().data());
    assert(task->phase() == Phase::Ready && task->readCount() == files.size());
    auto sources = task->sources();
    task.reset();
    assert(sources.valid());
    for (std::size_t i = 0; i < sources.programs.size(); ++i) {
        core::pmr::String diagnostic;
        auto original = shader::GeneratedShaderArtifactSet::load(root / Sources::publications[i],
            {Sources::maxFileBytes, Sources::maxSourceBytes, 8 * 1024 * 1024, 64, 8},
            core::makeShared<core::CpuMemoryQuota>(8 * 1024 * 1024),
            diagnostic);
        assert(original && original->identity() == sources.programs[i]->identity());
        for (const auto role : {shader::ShaderArtifactRole::Source,
                 shader::ShaderArtifactRole::Spirv,
                 shader::ShaderArtifactRole::Metal,
                 shader::ShaderArtifactRole::Wgsl,
                 shader::ShaderArtifactRole::Reflection})
            assert(original->bytes(role) == sources.programs[i]->bytes(role));
    }
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc >= 2);
    const auto files = corpus(argv[1]);
    {
        resource::ResourceManager metadata;
        resource::ResourceBase carrier(resource::ResourceReference{});
        (void)reflection::object::undefined();
    }
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < (argc > 2 ? 20u : 1u); ++i)
        for (const auto fault : {Fault::None,
                 Fault::Missing,
                 Fault::Hash,
                 Fault::Version,
                 Fault::Cancel,
                 Fault::Destroy,
                 Fault::Stale,
                 Fault::Closed})
            cycle(files, argv[1], fault);
    const auto after = core::defaultCpuAllocator().stats();
    std::printf("Native Shader inputs=%zu core=%llu/%llu -> %llu/%llu\n",
        files.size(),
        (unsigned long long)before.liveBytes,
        (unsigned long long)(before.allocationCount - before.deallocationCount),
        (unsigned long long)after.liveBytes,
        (unsigned long long)(after.allocationCount - after.deallocationCount));
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
}
