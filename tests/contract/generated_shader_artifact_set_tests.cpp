#include "core/io/filesystem_path.h"
#include "core/io/http_file_backend.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "core/runtime/async_runtime.h"
#include "renderer/shader_package_reflection.h"
#include "resource/resource_graph_executor.h"
#include "resource/shader_artifact_resource.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/frontend/pipeline_definition_identity.h"
#include "rhi/frontend/pipeline_state_copy.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {
using namespace arp;
using namespace core::io;
using Role = shader::ShaderArtifactRole;
using Y = core::YamlContract;
constexpr const char* publication = "ordinary_graphics_storage_read.artifact-set.yaml";
constexpr const char* resourceId = "1b61c69c-719e-4165-b2a2-f52051162e58";
constexpr std::size_t fileCap = 4 * 1024 * 1024, totalCap = 16 * 1024 * 1024;
constexpr std::size_t cpuCap = 16 * 1024 * 1024;

shader::GeneratedShaderArtifactSetBudget budget()
{
    return {fileCap, totalCap, cpuCap, 32, 8};
}

class Snapshot final : public shader::ShaderArtifactSource {
public:
    core::pmr::Map<core::pmr::String, core::ByteBuffer> files{&core::defaultCpuMemoryResource()};
    mutable unsigned calls = 0;
    const char* reject = nullptr;
    bool overReturn = false;

    explicit Snapshot(const std::filesystem::path& root)
    {
        auto manifest = core::readBinaryFile(root / publication, fileCap);
        assert(manifest.success);
        const auto node = shader::shaderArtifactYaml(shader::shaderArtifactText(manifest.bytes));
        files.emplace(publication, std::move(manifest.bytes));
        for (const auto* role : {"source", "spirv", "metal", "wgsl", "reflection"}) {
            const auto name = Y::text(Y::required(Y::required(node, "files"), role), "path");
            auto bytes = core::readBinaryFile(root / name.c_str(), fileCap);
            assert(bytes.success);
            files.emplace(name, std::move(bytes.bytes));
        }
    }

    shader::ShaderArtifactSourceResult read(
        const std::filesystem::path& path, std::uint64_t maxBytes) const override
    {
        ++calls;
        if (reject)
            return {{}, reject};
        // All test sources are deliberately relocated to a logical namespace.
        assert(path.parent_path() == "relocated");
        const auto name = core::FilePath::utf8(path.filename());
        const auto found = files.find(name);
        if (found == files.end())
            return {{}, "SHADER-TEST-MISSING"};
        shader::ShaderArtifactSourceResult result;
        result.bytes = found->second;
        if (overReturn)
            result.bytes.resize(static_cast<std::size_t>(maxBytes) + 1);
        return result;
    }

    Y::Node manifest() const
    {
        return shader::shaderArtifactYaml(shader::shaderArtifactText(files.at(publication)));
    }

    void save(const Y::Node& node)
    {
        const auto text = Y::serialize(node);
        files.at(publication).assign(text.begin(), text.end());
    }

    void replaceRole(const char* role, core::StringView text)
    {
        auto node = manifest();
        const auto name = Y::text(node["files"][role], "path");
        auto& bytes = files.at(name);
        bytes.assign(text.begin(), text.end());
        const auto digest = core::Sha256::hexDigest(shader::shaderArtifactText(bytes));
        node["files"][role]["sha256"] =
            std::string(digest.substr(core::StringView("sha256:").size()));
        save(node);
    }

    void unavailableWgsl()
    {
        auto node = manifest();
        node["files"]["wgsl"] = Y::Node::mapping();
        node["files"]["wgsl"]["status"] = "DiagnosticFailure";
        node["files"]["wgsl"]["diagnosticId"] = "SHADER-GENERATED-WGSL-UNAVAILABLE";
        node["files"]["wgsl"]["reason"] = "WGSL compilation failed";
        node["reflectionBindings"]["wgsl"] = nullptr;
        save(node);
        auto reflection = shader::shaderArtifactYaml(
            shader::shaderArtifactText(files.at("ordinary_graphics_storage_read.reflection.yaml")));
        reflection["targets"]["wgsl"] = nullptr;
        reflection["webGpuBindingSpaceStatus"] = "Unavailable";
        reflection["webGpuBindingSpaceUnavailableReason"] = "WGSL compilation failed";
        reflection["webGpuBindingSpace"] = Y::Node::sequence();
        reflection["webGpuBindingSpaceGroupCount"] = 0;
        reflection["pushConstantWebGpuBinding"] = nullptr;
        replaceRole("reflection", Y::serialize(reflection));
    }
};

void verifyPackage(const core::SharedPtr<const shader::GeneratedShaderArtifactSet>& owner)
{
    rhi::ShaderPackageDesc package;
    package.label = "GeneratedShaderPackageInput/1";
    package.slangModulePath = "relocated/" + owner->filename(Role::Source);
    package.artifacts.spirvPath = "relocated/" + owner->filename(Role::Spirv);
    package.artifacts.mslPath =
        owner->filename(Role::Metal).empty() ? "" : "relocated/" + owner->filename(Role::Metal);
    package.artifacts.wgslPath =
        owner->filename(Role::Wgsl).empty() ? "" : "relocated/" + owner->filename(Role::Wgsl);
    package.artifacts.reflectionPath = "relocated/" + owner->filename(Role::Reflection);
    package.entrypoints.push_back(
        {package.slangModulePath, "vertexMain", rhi::ShaderStage::Vertex});
    package.entrypoints.push_back(
        {package.slangModulePath, "fragmentMain", rhi::ShaderStage::Fragment});
    renderer::ShaderPackagePreparationDesc preparation;
    preparation.publication = std::filesystem::path("relocated") / publication;
    preparation.prepared = owner;
    core::pmr::String diagnostic;
    assert(renderer::hydrateShaderPackageReflectionFromArtifact(package, preparation, diagnostic));
    for (const auto& binding : owner->module().reflection.bindings) {
        const auto type = rhi::descriptorTypeFromShaderBinding(binding.type);
        assert(type);
        package.reflection.descriptorBindings.push_back({binding.set,
            binding.binding,
            *type,
            static_cast<rhi::ShaderStage>(binding.stageMask),
            binding.arrayCount,
            binding.subpassAttachmentIndex,
            binding.depthTexture});
    }
    assert(rhi::validateShaderPackage(package).success);
    auto copy = rhi::ShaderPackageStateCopy::copy(package);
    assert(copy.generatedArtifactSet == owner
        && rhi::PipelineDefinitionIdentity::matches(copy, package));
    assert(rhi::stableShaderPackageHash(copy) == rhi::stableShaderPackageHash(package));
    auto forged = rhi::ShaderPackageStateCopy::copy(package);
    forged.generatedArtifactSet =
        core::SharedPtr<const shader::GeneratedShaderArtifactSet>(owner.get(), [](const auto*) {});
    assert(!rhi::validateGeneratedShaderPackageInput(forged).success);
    assert(!rhi::PipelineDefinitionIdentity::matches(forged, package));
    forged = {};
    for (auto target :
        {shader::TargetBackend::SpirV, shader::TargetBackend::Msl, shader::TargetBackend::Wgsl}) {
        rhi::GeneratedShaderTargetInput input;
        const auto selected = rhi::selectGeneratedShaderPackageTarget(package, target, input);
        if (target == shader::TargetBackend::Wgsl && owner->bytes(Role::Wgsl).empty()) {
            assert(!selected.success && selected.diagnostics == owner->targetDiagnostic(Role::Wgsl)
                && !input.bytes && !input.spirvWords && input.text.empty());
            continue;
        }
        assert(selected.success && input.bytes && !input.bytes->empty());
        if (target == shader::TargetBackend::SpirV)
            assert(input.spirvWords == &owner->spirvWords());
        else
            assert(!input.text.empty());
    }
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i) {
        assert(rhi::validateGeneratedShaderPackageInput(package).success);
        assert(rhi::PipelineDefinitionIdentity::matches(copy, package));
        assert(rhi::stableShaderPackageHash(copy) == rhi::stableShaderPackageHash(package));
    }
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.allocationCount == after.allocationCount);
    const auto reject = [&](auto change, const char* id) {
        auto changed = rhi::ShaderPackageStateCopy::copy(package);
        change(changed);
        const auto valid = rhi::validateGeneratedShaderPackageInput(changed);
        assert(!valid.success && valid.diagnostics == id);
        assert(!rhi::PipelineDefinitionIdentity::matches(changed, package));
        rhi::GeneratedShaderTargetInput input;
        assert(
            !rhi::selectGeneratedShaderPackageTarget(changed, shader::TargetBackend::SpirV, input)
                .success
            && !input.bytes);
    };
    reject(
        [](auto& p) {
            p.artifacts.spirvPath = "other/ordinary_graphics_storage_read.spv";
        },
        "RHI-GENERATED-PACKAGE-ROLE-BINDING");
    reject(
        [](auto& p) {
            p.reflection.sourceSha256[0] ^= 1;
        },
        "RHI-GENERATED-PACKAGE-REFLECTION-MISMATCH");
    reject(
        [](auto& p) {
            ++p.reflection.pushConstantMetalBufferIndex;
        },
        "RHI-GENERATED-PACKAGE-REFLECTION-MISMATCH");
    reject(
        [](auto& p) {
            ++p.entrypoints[0].computeWorkgroupSizeX;
        },
        "RHI-GENERATED-PACKAGE-ENTRY-ABI");
    reject(
        [](auto& p) {
            p.generatedReflectionHydrated = false;
        },
        "RHI-GENERATED-PACKAGE-NOT-HYDRATED");
    reject(
        [](auto& p) {
            p.generatedArtifactSet.reset();
        },
        "RHI-GENERATED-PACKAGE-INPUT-MISSING");
    auto portable = rhi::ShaderPackageStateCopy::copy(package);
    portable.artifacts.spirvPath = "relocated\\ordinary_graphics_storage_read.spv";
    assert(rhi::validateGeneratedShaderPackageInput(portable).success);

    auto device = rhi::createNullDevice();
    auto layout = device->createPipelineLayout(
        rhi::buildPipelineLayoutDescFromShaderReflection(package.reflection));
    assert(layout);
    rhi::RenderPipelineDesc desc;
    desc.label = "Generated input Null validation smoke";
    desc.layout = layout.get();
    desc.shaderPackage = &package;
    desc.vertexShader = package.entrypoints[0];
    desc.fragmentShader = package.entrypoints[1];
    desc.colorTargets.push_back({rhi::TextureFormat::Rgba8Unorm});
    desc.viewport = {0, 0, 64, 64, 0, 1};
    desc.scissor = {0, 0, 64, 64};
    const auto ownersBefore = owner.use_count();
    auto pipeline = device->createRenderPipeline(desc);
    if (!pipeline) {
        const auto report = device->validateRenderPipeline(desc);
        std::fprintf(stderr, "Null generated input: %s\n", report.diagnostics.c_str());
    }
    assert(
        pipeline && pipeline->validationReport().success && owner.use_count() == ownersBefore + 1);
    pipeline.reset();
    assert(owner.use_count() == ownersBefore);
    {
        rhi::PipelineCache cache(*device);
        assert(cache.acquireRenderPipeline(desc));
        auto unhydrated = rhi::ShaderPackageStateCopy::copy(package);
        unhydrated.generatedReflectionHydrated = false;
        auto changed = desc;
        changed.shaderPackage = &unhydrated;
        assert(rhi::stableShaderPackageHash(unhydrated) == rhi::stableShaderPackageHash(package));
        assert(!cache.acquireRenderPipeline(changed));
        assert(cache.acquireRenderPipeline(desc));
    }
    package.generatedArtifactSet.reset();
    package.artifacts.publicationPath.clear();
    assert(rhi::validateShaderPackage(package).success);
    assert(!device->createRenderPipeline(
        desc)); // path/boolean declarations confer no artifact authority.
}

void verify(const core::SharedPtr<const shader::GeneratedShaderArtifactSet>& owner)
{
    assert(owner && !shader::GeneratedShaderArtifactSet::validateOriginal(owner));
    assert(owner->module().reflection.entryPoints.size() == 2);
    assert(owner->module().reflection.sourceSha256.size() == 64);
    assert(owner->spirvWords().size() * sizeof(std::uint32_t) == owner->bytes(Role::Spirv).size());
    assert(owner->spirvWords()[0] == 0x07230203u);
    assert(std::memcmp(owner->spirvWords().data(),
               owner->bytes(Role::Spirv).data(),
               owner->bytes(Role::Spirv).size())
        == 0);
    for (const auto role : {Role::Source, Role::Spirv, Role::Metal, Role::Wgsl, Role::Reflection})
        assert(!owner->bytes(role).empty() && !owner->filename(role).empty()
            && owner->targetDiagnostic(role).empty());
    assert(owner->quota()->liveBytes() > 0 && owner->quota()->liveBytes() <= cpuCap);
    verifyPackage(owner);
}

void contracts(const std::filesystem::path& root)
{
    Snapshot source(root);
    core::pmr::String diagnostic;
    auto b = budget();
    b.source = &source;
    auto quota = core::makeShared<core::CpuMemoryQuota>(cpuCap);
    auto owner = shader::GeneratedShaderArtifactSet::load(
        std::filesystem::path("relocated") / publication, b, quota, diagnostic);
    verify(owner);
    assert(source.calls == 6);
    const auto host = shader::GeneratedShaderArtifactSet::load(
        root / publication, budget(), core::makeShared<core::CpuMemoryQuota>(cpuCap), diagnostic);
    verify(host);
    assert(host->identity() == owner->identity() && host->inputBytes() == owner->inputBytes());
    for (unsigned i = 0; i < unsigned(Role::Count); ++i)
        assert(host->bytes(Role(i)) == owner->bytes(Role(i)));
    auto forged =
        core::SharedPtr<const shader::GeneratedShaderArtifactSet>(owner.get(), [](const auto*) {});
    assert(shader::GeneratedShaderArtifactSet::validateOriginal(forged));
    forged.reset();
    owner.reset();
    assert(quota->liveBytes() == 0);
    {
        Snapshot unavailable(root);
        unavailable.unavailableWgsl();
        auto limits = budget();
        limits.source = &unavailable;
        auto optional = shader::GeneratedShaderArtifactSet::load(
            std::filesystem::path("relocated") / publication, limits, quota, diagnostic);
        assert(optional && unavailable.calls == 5);
        assert(optional->bytes(Role::Wgsl).empty() && optional->filename(Role::Wgsl).empty()
            && optional->reflectedPath(Role::Wgsl).empty()
            && optional->targetDiagnostic(Role::Wgsl) == "SHADER-GENERATED-WGSL-UNAVAILABLE");
        assert(!optional->bytes(Role::Spirv).empty() && !optional->bytes(Role::Metal).empty());
        verifyPackage(optional);
    }
    assert(quota->liveBytes() == 0);

    auto expect = [&](const char* id, auto mutate) {
        Snapshot invalid(root);
        auto limits = budget();
        mutate(invalid, limits);
        limits.source = &invalid;
        auto q = core::makeShared<core::CpuMemoryQuota>(limits.maxCpuMetadataBytes);
        const auto result = shader::GeneratedShaderArtifactSet::load(
            std::filesystem::path("relocated") / publication, limits, q, diagnostic);
        if (result || diagnostic != id)
            std::fprintf(stderr, "expected %s, got %s\n", id, diagnostic.c_str());
        assert(!result && diagnostic == id && q->liveBytes() == 0);
    };
    expect("SHADER-TEST-REJECTED", [](auto& s, auto&) {
        s.reject = "SHADER-TEST-REJECTED";
    });
    expect("SHADER-ENTRY-FILE-READ-FAILED", [](auto& s, auto& b) {
        s.overReturn = true;
        b.maxFileBytes = 4096;
    });
    expect("SHADER-GENERATED-PUBLICATION-CONTRACT", [](auto& s, auto&) {
        auto n = s.manifest();
        n["contractVersion"] = 2;
        s.save(n);
    });
    expect("SHADER-GENERATED-PUBLICATION-CONTRACT", [](auto& s, auto&) {
        auto n = s.manifest();
        n["matrixLayout"] = "column-major";
        s.save(n);
    });
    expect("SHADER-GENERATED-REFLECTION-BINDING", [](auto& s, auto&) {
        auto n = s.manifest();
        n["reflectionBindings"]["source"] = "other/ordinary_graphics_storage_read.slang";
        s.save(n);
    });
    expect("SHADER-ENTRY-FILE-ROLE-PATH", [](auto& s, auto&) {
        auto n = s.manifest();
        n["files"]["spirv"]["path"] = "../outside.spv";
        s.save(n);
    });
    expect("SHADER-TEST-MISSING", [](auto& s, auto&) {
        s.files.erase("ordinary_graphics_storage_read.spv");
    });
    expect("SHADER-ENTRY-FILE-HASH", [](auto& s, auto&) {
        s.files.at("ordinary_graphics_storage_read.spv")[0] ^= 1;
    });
    expect("TEXT-INVALID-UTF8", [](auto& s, auto&) {
        s.replaceRole("source", core::StringView("\xff", 1));
    });
    expect("TEXT-NUL-REJECTED", [](auto& s, auto&) {
        s.replaceRole("metal", core::StringView("a\0b", 3));
    });
    expect("SHADER-GENERATED-SOURCE-HASH", [](auto& s, auto&) {
        s.replaceRole("source", "// modified source\n");
    });
    expect("SHADER-GENERATED-SPIRV-SIZE", [](auto& s, auto&) {
        s.replaceRole("spirv", "bad");
    });
    expect("SHADER-GENERATED-SPIRV-MAGIC", [](auto& s, auto&) {
        auto binary = s.files.at("ordinary_graphics_storage_read.spv");
        binary[0] ^= 1;
        s.replaceRole("spirv", shader::shaderArtifactText(binary));
    });
    expect("SHADER-ENTRY-BINDING-SEMANTICS", [](auto& s, auto&) {
        auto reflection = shader::shaderArtifactYaml(shader::shaderArtifactText(
            s.files.at("ordinary_graphics_storage_read.reflection.yaml")));
        reflection["bindings"][0]["stageMask"] = 4;
        s.replaceRole("reflection", Y::serialize(reflection));
    });
    expect("SHADER-GENERATED-UNAVAILABLE-CONTRACT", [](auto& s, auto&) {
        s.unavailableWgsl();
        auto n = s.manifest();
        n["files"]["wgsl"]["reason"] = "unknown";
        s.save(n);
    });
    expect("SHADER-GENERATED-UNAVAILABLE-BINDING", [](auto& s, auto&) {
        s.unavailableWgsl();
        auto n = s.manifest();
        n["reflectionBindings"]["wgsl"] = "present.wgsl";
        s.save(n);
    });
    expect("SHADER-GENERATED-ENTRY-BUDGET", [](auto&, auto& b) {
        b.maxEntryPoints = 1;
    });
    expect("SHADER-GENERATED-LOAD-BUDGET", [](auto&, auto& b) {
        b.maxTotalBytes = UINT64_MAX;
    });
    expect("SHADER-GENERATED-CPU-ALLOCATION", [](auto&, auto& b) {
        b.maxCpuMetadataBytes = 256;
    });
    // Explicit source rejection wins even though the real host publication exists.
    source.reject = "SHADER-TEST-REJECTED";
    assert(!shader::GeneratedShaderArtifactSet::load(root / publication, b, quota, diagnostic));
    assert(diagnostic == "SHADER-TEST-REJECTED");
}

core::SharedPtr<const shader::GeneratedShaderArtifactSet> graph(
    core::SharedPtr<ManagedFileBackend> backend, unsigned action = 0)
{
    auto router =
        core::makeShared<MountedFileBackend>(MountedFileBackendDesc{1, 2, totalCap, 8, 1024});
    const auto mount = router->mount("shaders", backend);
    assert(mount);
    core::runtime::AsyncRuntimeDesc desc;
    desc.completionQueueCapacity = desc.completionBudget = 8;
    desc.workerCount = 1;
    desc.workQueueCapacity = 4;
    desc.enableThreadPoolFileBackend = false;
    desc.fileBackend = router;
    core::runtime::AsyncRuntime runtime(desc);
    resource::ResourceManager manager;
    resource::ShaderArtifactResourceDesc artifact;
    artifact.publication = publication;
    artifact.origin = {FilePathScope::MountedResource, mount};
    artifact.budget = budget();
    resource::ResourceGraphExecutionRequest request;
    request.graph.rootResourceId = resourceId;
    resource::ResourceGraphNode node;
    node.resource = core::makeUnique<resource::ShaderArtifactResource>(
        resource::ResourceReference{resourceId,
            resource::ShaderArtifactResource::resourceTypeName(),
            "generated",
            "Contract Complete",
            {}},
        std::move(artifact));
    request.graph.nodes.push_back(std::move(node));
    request.budget = {1, 6, totalCap, cpuCap, 8};
    if (action == 3)
        request.budget.maxFileReads = 1;
    core::runtime::CancellationSource cancellation;
    request.cancellation = cancellation.token();

    struct Probe {
        resource::ResourceGraphExecutionResult result;
        bool done = false;
    } probe;

    resource::ResourceGraphExecutor executor(
        manager, runtime.fileService(), runtime.completionQueue());
    assert(executor.submit(std::move(request),
               {[](void* context, resource::ResourceGraphExecutionResult value) {
                    auto& p = *static_cast<Probe*>(context);
                    p.result = std::move(value);
                    p.done = true;
                },
                   &probe})
        == resource::ResourceGraphSubmitStatus::Accepted);
    assert(!probe.done);
    if (action == 1 || action == 2) {
        runtime.dispatch(1);
        if (action == 1)
            cancellation.cancel();
        else {
            assert(router->unmount(mount));
            assert(router->mount("shaders", backend));
        }
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!probe.done && std::chrono::steady_clock::now() < deadline) {
        runtime.dispatch(8);
        std::this_thread::yield();
    }
    if (!probe.result.resource.success && !action)
        std::fprintf(stderr, "graph: %s\n", probe.result.resource.diagnostics.c_str());
    assert(probe.done && probe.result.telemetry.completionCount == 1);
    const auto resource = manager.find<resource::ShaderArtifactResource>(resourceId);
    if (action) {
        assert(!probe.result.resource.success && !resource.ready());
        runtime.shutdown();
        assert(runtime.completionQueue().reserved() == 0 && router->telemetry().inFlight == 0);
        return {};
    }
    assert(probe.result.resource.success && probe.result.telemetry.submittedFileReads == 6);
    assert(resource.ready() && resource.get()->generatedSet());
    auto owner = resource.get()->generatedSet();
    assert(manager.unload(resourceId).success);
    runtime.shutdown();
    assert(runtime.completionQueue().reserved() == 0 && router->telemetry().inFlight == 0);
    verify(owner);
    return owner;
}

} // namespace

int main(int argc, char** argv)
{
    assert(argc >= 3);
    const bool memory = std::strcmp(argv[1], "--memory") == 0;
    const bool quick = argc > 3 && std::strcmp(argv[3], "--quick") == 0;
    if (memory)
        contracts(argv[2]);
    auto cycle = [&](unsigned action = 0) {
        core::SharedPtr<ManagedFileBackend> backend;
        if (memory) {
            Snapshot snapshot(argv[2]);
            auto local = core::makeShared<MemoryFileBackend>(
                MemoryFileBackendDesc{8, totalCap, 2, totalCap, 1024});
            for (const auto& [name, bytes] : snapshot.files)
                assert(
                    local->registerFile(name, bytes.data(), bytes.size()) == FileIoStatus::Success);
            backend = std::move(local);
        } else {
            HttpFileBackendDesc desc;
            desc.baseUrl = argv[2];
            desc.maxInFlight = 2;
            desc.maxBytesInFlight = totalCap;
            desc.maxPathBytes = 1024;
            desc.maxHeaderBytes = desc.ioChunkBytes = 4096;
            desc.timeoutMs = 3000;
            desc.cancellationPollMs = 2;
            backend = core::makeShared<HttpFileBackend>(desc);
        }
        auto owner = graph(std::move(backend), action);
        if (!action)
            assert(owner->inputBytes() > owner->publicationBytes().size());
    };
    if (memory)
        for (unsigned action = 1; action <= 3; ++action)
            cycle(action);
    if (quick) {
        cycle();
    } else {
        for (unsigned i = 0; i < 20; ++i)
            cycle();
        const auto baseline = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i)
            cycle();
        const auto end = core::defaultCpuAllocator().stats();
        std::printf("ordinary lifecycle live bytes %llu -> %llu\n",
            static_cast<unsigned long long>(baseline.liveBytes),
            static_cast<unsigned long long>(end.liveBytes));
        assert(baseline.liveBytes == end.liveBytes
            && baseline.allocationCount - baseline.deallocationCount
                == end.allocationCount - end.deallocationCount);
    }
    std::puts("GeneratedShaderArtifactSet/1: PASS");
}
