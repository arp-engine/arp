#pragma once

#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "core/runtime/async_runtime.h"
#include "renderer/shader_package_reflection.h"
#include "resource/resource_graph_executor.h"
#include "resource/shader_artifact_resource.h"
#include "tests/fixtures/shader_source_files.h"

#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>

namespace renderer_source_fixture {
using namespace arp;
inline constexpr std::array<const char*, 5> names{
    "fxaa", "wocc_taa", "ssao", "display_transfer", "scene_2d_sprites"};
inline constexpr std::array<const char*, 5> ids{"c84d32ce-12e4-41e3-af17-f00500000001",
    "c84d32ce-12e4-41e3-af17-f00500000002",
    "c84d32ce-12e4-41e3-af17-f00500000003",
    "c84d32ce-12e4-41e3-af17-f00500000004",
    "c84d32ce-12e4-41e3-af17-f00500000005"};
inline constexpr std::size_t fileBytes = 4 * 1024 * 1024;
inline constexpr std::size_t totalBytes = 32 * 1024 * 1024;
using Inputs = std::array<renderer::ShaderPackagePreparationDesc, names.size()>;

// Original resource graph discovers each publication's dependencies. This test
// owner deliberately destroys graph, IO and backend before returning inputs.
inline Inputs acquire(core::SharedPtr<core::io::ManagedFileBackend> backend)
{
    using namespace core::io;
    auto mounts =
        core::makeShared<MountedFileBackend>(MountedFileBackendDesc{1, 1, fileBytes, 32, 512});
    const auto mount = mounts->mount("renderer-sources", backend);
    assert(mount);
    core::runtime::AsyncRuntimeDesc desc;
    desc.enableWork = desc.enableThreadPoolFileBackend = false;
    desc.completionQueueCapacity = desc.completionBudget = 2;
    desc.fileBackend = mounts;
    core::runtime::AsyncRuntime runtime(desc);
    assert(runtime.valid());
    resource::ResourceManager resources;
    resource::ResourceGraphExecutionRequest request;
    request.graph.rootResourceId = ids.back();
    request.budget = {names.size(), names.size() * 6, totalBytes, totalBytes, 2};
    Inputs inputs;
    for (std::size_t i = 0; i < names.size(); ++i) {
        inputs[i].publication = core::pmr::String(names[i]) + ".artifact-set.yaml";
        resource::ShaderArtifactResourceDesc artifact;
        artifact.publication = inputs[i].publication.c_str();
        artifact.origin = {FilePathScope::MountedResource, mount};
        artifact.budget =
            shader::GeneratedShaderArtifactSetBudget{fileBytes, totalBytes, totalBytes, 32, 8};
        resource::ResourceGraphNode node;
        node.resource = core::makeUnique<resource::ShaderArtifactResource>(
            resource::ResourceReference{ids[i],
                resource::ShaderArtifactResource::resourceTypeName(),
                names[i],
                "Contract Complete",
                {}},
            std::move(artifact));
        // The last node is the fixture's ready barrier, not a production dependency.
        if (i + 1 == names.size())
            for (std::size_t j = 0; j < i; ++j)
                node.dependencies.push_back({ids[j], "fixture-ready"});
        request.graph.nodes.push_back(std::move(node));
    }

    struct Completion {
        bool done = false;
        resource::ResourceGraphExecutionResult result;
    } completion;

    resource::ResourceGraphExecutor graph(
        resources, runtime.fileService(), runtime.completionQueue());
    assert(graph.submit(std::move(request),
               {[](void* context, resource::ResourceGraphExecutionResult result) {
                    auto& value = *static_cast<Completion*>(context);
                    value.result = std::move(result);
                    value.done = true;
                },
                   &completion})
        == resource::ResourceGraphSubmitStatus::Accepted);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!completion.done && std::chrono::steady_clock::now() < deadline) {
        assert(runtime.dispatch(2).success());
        std::this_thread::yield();
    }
    if (!completion.result.resource.success)
        std::fprintf(
            stderr, "renderer sources: %s\n", completion.result.resource.diagnostics.c_str());
    assert(completion.done && completion.result.resource.success);
    assert(completion.result.telemetry.submittedFileReads == names.size() * 6);
    for (std::size_t i = 0; i < names.size(); ++i) {
        const auto handle = resources.find<resource::ShaderArtifactResource>(ids[i]);
        assert(handle.ready());
        inputs[i].prepared = handle.get()->generatedSet();
        assert(inputs[i].prepared);
    }
    runtime.shutdown();
    assert(!mounts->telemetry().inFlight && !runtime.completionQueue().reserved());
    return inputs;
}

inline Inputs acquire(const shader_source_fixture::Files& files)
{
    auto memory = core::makeShared<core::io::MemoryFileBackend>(
        core::io::MemoryFileBackendDesc{names.size() * 32, totalBytes, 1, fileBytes, 512});
    for (const auto& directory : files.directories)
        assert(memory->registerDirectory(directory) == core::io::FileIoStatus::Success);
    for (const auto& file : files.files)
        assert(memory->registerFile(file.path, file.bytes.data(), file.bytes.size())
            == core::io::FileIoStatus::Success);
    return acquire(std::move(memory));
}
} // namespace renderer_source_fixture
