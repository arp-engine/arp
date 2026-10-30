#include "core/io/http_file_backend.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "resource/resource_graph_executor.h"
#include "resource/shader_artifact_resource.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {
using namespace arp;
using namespace core::io;
constexpr std::size_t fileBytes = 4 * 1024 * 1024;
constexpr std::size_t totalBytes = 64 * 1024 * 1024;
constexpr std::size_t metadataBytes = 32 * 1024 * 1024;
constexpr const char* resourceId = "d706da5d-cb46-43a5-a196-39d4a3a5ba86";
constexpr const char* publications[]{"single/ordinary_graphics_storage_read.vertexMain.entry.yaml",
    "graphics/ordinary_graphics_storage_read.graphics.module.yaml",
    "webgl/webgl1_graphics.webgl-program.yaml",
    "depth/original-depth-table.expansion.yaml"};
#if defined(ARP_SHADER_SOURCE_DEPTH_AVAILABLE)
constexpr unsigned kindCount = 4;
#else
constexpr unsigned kindCount = 3;
#endif

resource::ShaderArtifactResourceDesc describe(unsigned kind, resource::ResourceFileOrigin origin)
{
    resource::ShaderArtifactResourceDesc desc;
    desc.publication = publications[kind];
    desc.origin = origin;
    if (kind == 0)
        desc.budget = shader::SingleEntryShaderLoadBudget{fileBytes, totalBytes, metadataBytes, 32};
    else if (kind == 1)
        desc.budget = shader::GraphicsShaderModuleLoadBudget{
            fileBytes, totalBytes, metadataBytes, 32, 32, 256, 2};
    else if (kind == 2)
        desc.budget = shader::WebGlShaderLoadBudget{
            {fileBytes, totalBytes, metadataBytes, 32}, 256, 32, 1024};
    if (kind == 3)
        desc.budget = shader::OriginalDepthTableExpansionArtifact::Budget{
            1, true, fileBytes, totalBytes, metadataBytes, 2, 4, 3, 2};
    return desc;
}

core::UniquePtr<resource::ShaderArtifactResource> makeResource(
    unsigned kind, resource::ResourceFileOrigin origin)
{
    return core::makeUnique<resource::ShaderArtifactResource>(
        resource::ResourceReference{resourceId,
            resource::ShaderArtifactResource::resourceTypeName(),
            "shader",
            "Contract Complete",
            {}},
        describe(kind, origin));
}

enum class Mutation { None, MissingTarget, BadHash, BadUtf8, EmbeddedNul, UnknownVersion, Bom };

// Fixture inventory is confined to the test's explicitly copied, finite tree.
// Runtime HTTP acquisition receives neither this list nor a host source root.
struct FixtureFiles {
    core::pmr::Vector<resource::ResourceSourceBuffer> files;
    core::pmr::Vector<core::pmr::String> directories;

    explicit FixtureFiles(const std::filesystem::path& root)
    {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
            const auto name = core::FilePath::utf8(entry.path().lexically_relative(root));
            if (entry.is_directory())
                directories.push_back(name);
            if (entry.is_regular_file()) {
                auto bytes = core::readBinaryFile(entry.path(), fileBytes);
                assert(bytes.success);
                resource::ResourceSourceBuffer source;
                source.path = name;
                source.bytes = std::move(bytes.bytes);
                files.push_back(std::move(source));
            }
        }
        assert(files.size() + directories.size() <= 512);
        std::sort(directories.begin(), directories.end(), [](const auto& a, const auto& b) {
            return a.size() == b.size() ? a < b : a.size() < b.size();
        });
    }

    core::SharedPtr<MemoryFileBackend> memory(
        unsigned kind, Mutation mutation = Mutation::None) const
    {
        auto backend = core::makeShared<MemoryFileBackend>(
            MemoryFileBackendDesc{512, totalBytes, 4, totalBytes, 1024});
        for (const auto& directory : directories)
            assert(backend->registerDirectory(directory) == FileIoStatus::Success);
        const auto prefix = std::filesystem::path(publications[kind]).parent_path();
        for (auto file : files) {
            const auto relative = std::filesystem::u8path(file.path.begin(), file.path.end());
            const auto prefixText = core::FilePath::utf8(prefix) + "/";
            const bool selected = file.path.compare(0, prefixText.size(), prefixText) == 0;
            if (selected && relative.extension() == ".spv") {
                if (mutation == Mutation::MissingTarget)
                    continue;
                if (mutation == Mutation::BadHash)
                    file.bytes[0] ^= 1;
            }
            if (file.path == publications[kind]) {
                if (mutation == Mutation::Bom)
                    file.bytes.insert(file.bytes.begin(), {0xEF, 0xBB, 0xBF});
                if (mutation == Mutation::BadUtf8)
                    file.bytes.push_back(0xFF);
                if (mutation == Mutation::EmbeddedNul)
                    file.bytes.push_back(0);
                if (mutation == Mutation::UnknownVersion) {
                    const core::StringView raw(
                        reinterpret_cast<const char*>(file.bytes.data()), file.bytes.size());
                    auto node = core::YamlContract::parseSingleDocument(raw);
                    node["contractVersion"] = 99;
                    const auto text = core::YamlContract::serialize(node);
                    file.bytes.assign(text.begin(), text.end());
                }
            }
            assert(backend->registerFile(file.path, file.bytes.data(), file.bytes.size())
                == FileIoStatus::Success);
        }
        return backend;
    }

    resource::ResourceSourceSet snapshot(resource::ResourceFileOrigin origin) const
    {
        resource::ResourceSourceSet set;
        for (auto source : files) {
            source.pathScope = origin.pathScope;
            source.mount = origin.mount;
            assert(set.add(std::move(source)));
        }
        return set;
    }
};

enum class Action { None, Cancel, Remount, ReadBudget, MetadataBudget };

void verifyArtifact(const resource::ShaderArtifactResource& value, unsigned kind)
{
    if (kind == 0) {
        const auto& artifact = value.singleEntry();
        assert(artifact && !shader::SingleEntryShaderArtifact::validateOriginal(artifact));
        assert(
            !artifact->nativeExecutionProven() && artifact->stage() == shader::ShaderStage::Vertex);
        assert(artifact->available(shader::ShaderArtifactRole::Spirv)
            && artifact->available(shader::ShaderArtifactRole::Metal)
            && artifact->available(shader::ShaderArtifactRole::Wgsl));
    } else if (kind == 1) {
        const auto& artifact = value.graphicsModule();
        assert(artifact && !shader::GraphicsShaderModuleArtifact::validateOriginal(artifact));
        assert(artifact->vertex()->quota() == artifact->quota()
            && artifact->fragment()->quota() == artifact->quota());
        assert(artifact->module().reflection.entryPoints.size() == 2);
    } else if (kind == 2) {
        const auto& artifact = value.webGlProgram();
        assert(artifact && !shader::WebGlShaderProgramArtifact::validateOriginal(artifact));
        assert(!artifact->nativeExecutionProven());
        assert(!artifact->vertex().glsl.empty() && !artifact->fragment().glsl.empty());
    }
    if (kind == 3) {
        const auto& artifact = value.depthTableExpansion();
        assert(
            artifact && !shader::OriginalDepthTableExpansionArtifact::validateOriginal(artifact));
        assert(artifact->entryCount() == 1 && !artifact->originalSource().empty());
    }
    assert(value.processedByteCount() > 0 && value.processedByteCount() <= metadataBytes);
}

core::SharedPtr<HttpFileBackend> http(const char* url)
{
    HttpFileBackendDesc desc;
    desc.baseUrl = url;
    desc.maxInFlight = 2;
    desc.maxBytesInFlight = totalBytes;
    desc.maxPathBytes = 1024;
    desc.maxHeaderBytes = desc.ioChunkBytes = 4096;
    desc.timeoutMs = 3000;
    desc.cancellationPollMs = 2;
    return core::makeShared<HttpFileBackend>(desc);
}

void run(core::SharedPtr<ManagedFileBackend> backend,
    unsigned kind,
    bool expected = true,
    Action action = Action::None)
{
    auto router =
        core::makeShared<MountedFileBackend>(MountedFileBackendDesc{1, 4, totalBytes, 32, 1024});
    const auto mount = router->mount("shaders", backend);
    assert(mount);
    core::runtime::AsyncRuntimeDesc desc;
    desc.completionQueueCapacity = desc.completionBudget = 8;
    desc.workerCount = 1;
    desc.workQueueCapacity = 4;
    desc.enableThreadPoolFileBackend = false;
    desc.fileBackend = router;
    core::runtime::AsyncRuntime runtime(desc);
    assert(runtime.valid());
    resource::ResourceManager manager;
    resource::ResourceGraphExecutionRequest request;
    request.graph.rootResourceId = resourceId;
    resource::ResourceGraphNode node;
    node.resource = makeResource(kind, {FilePathScope::MountedResource, mount});
    request.graph.nodes.push_back(std::move(node));
    request.budget = {1, 128, totalBytes, metadataBytes, 8};
    if (action == Action::ReadBudget)
        request.budget.maxFileReads = 1;
    if (action == Action::MetadataBudget)
        request.budget.maxDecodedBytes = 1;
    core::runtime::CancellationSource cancel;
    request.cancellation = cancel.token();

    struct Probe {
        unsigned calls = 0;
        resource::ResourceGraphExecutionResult result;
    } probe;

    resource::ResourceGraphExecutor executor(
        manager, runtime.fileService(), runtime.workService(), runtime.completionQueue());
    assert(executor.submit(std::move(request),
               {[](void* context, resource::ResourceGraphExecutionResult result) {
                    auto& probe = *static_cast<Probe*>(context);
                    ++probe.calls;
                    probe.result = std::move(result);
                },
                   &probe})
        == resource::ResourceGraphSubmitStatus::Accepted);
    assert(probe.calls == 0);
    if (action == Action::Cancel || action == Action::Remount) {
        runtime.dispatch(1);
        if (action == Action::Cancel)
            cancel.cancel();
        else {
            assert(router->unmount(mount));
            assert(router->mount("shaders", backend));
        }
    }
    unified_io_fixture::wait(runtime.completionQueue(), [&] {
        return probe.calls != 0;
    });
    if (probe.result.resource.success != expected)
        std::fprintf(stderr, "shader %u: %s\n", kind, probe.result.resource.diagnostics.c_str());
    assert(probe.calls == 1 && probe.result.telemetry.completionCount == 1);
    assert(probe.result.resource.success == expected);
    if (expected) {
        const auto handle = manager.find<resource::ShaderArtifactResource>(resourceId);
        assert(handle.ready());
        verifyArtifact(*handle.get(), kind);
        assert(probe.result.telemetry.submittedFileReads >= 6);
    } else
        assert(!manager.find<resource::ShaderArtifactResource>(resourceId).ready());
    runtime.shutdown();
    assert(runtime.completionQueue().pending() == 0 && runtime.completionQueue().reserved() == 0);
    assert(router->telemetry().inFlight == 0);
    if (const auto* source = dynamic_cast<const HttpFileBackend*>(backend.get()))
        assert(source->telemetry().inFlight == 0 && source->telemetry().activeNativeTransfers == 0);
}

void sourceContracts(const FixtureFiles& files, const std::filesystem::path& root)
{
    const auto reflectionPath = root / "single/ordinary_graphics_storage_read.reflection.yaml";
    const auto reflectionBytes = core::readTextFile(reflectionPath, fileBytes);
    assert(reflectionBytes.success);
    const auto cached =
        shader::ShaderReflectionCache::loadFromFile(core::FilePath::utf8(reflectionPath));
    assert(cached.success);
    const auto bom = shader::ShaderReflectionCache::loadFromYaml(
        core::pmr::String("\xEF\xBB\xBF") + reflectionBytes.text);
    assert(
        bom.success && bom.module.reflection.sourceSha256 == cached.module.reflection.sourceSha256);
    auto badUtf8 = reflectionBytes.text;
    badUtf8.push_back(static_cast<char>(0xFF));
    assert(shader::ShaderReflectionCache::loadFromYaml(badUtf8).diagnostics == "TEXT-INVALID-UTF8");
    auto nul = reflectionBytes.text;
    nul.push_back('\0');
    assert(shader::ShaderReflectionCache::loadFromYaml(nul).diagnostics == "TEXT-NUL-REJECTED");
    const resource::ResourceFileOrigin origin{FilePathScope::MountedResource, {&files, 0, 1}};
    auto sources = files.snapshot(origin);
    resource::ResourceLoadContext context{{}, &sources, true, metadataBytes};
#if !defined(ARP_SHADER_SOURCE_DEPTH_AVAILABLE)
    resource::ResourceManager depthManager;
    const auto unavailable = depthManager.loadResource(makeResource(3, origin), context);
    assert(!unavailable.success
        && unavailable.diagnostics == "SHADER-ORIGINAL-TABLE-EXPANSION-COMPILER-PROOF-UNAVAILABLE");
#endif
    // An explicit byte source must win even when a valid host file exists.
    class RejectingSource final : public shader::ShaderArtifactSource {
    public:
        mutable unsigned calls = 0;

        shader::ShaderArtifactSourceResult read(
            const std::filesystem::path&, std::uint64_t) const override
        {
            ++calls;
            return {{}, "SHADER-TEST-EXPLICIT-SOURCE-REJECTED"};
        }
    } rejecting;

    auto rejectingBudget =
        std::get<shader::SingleEntryShaderLoadBudget>(describe(0, origin).budget);
    rejectingBudget.source = &rejecting;
    auto rejectingQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
    core::pmr::String rejectingDiagnostic;
    assert(!shader::SingleEntryShaderArtifact::load(
        root / publications[0], rejectingBudget, rejectingQuota, rejectingDiagnostic));
    assert(rejecting.calls == 1 && rejectingDiagnostic == "SHADER-TEST-EXPLICIT-SOURCE-REJECTED"
        && rejectingQuota->liveBytes() == 0);
    for (unsigned kind = 0; kind < kindCount; ++kind) {
        resource::ResourceManager manager;
        assert(manager.loadResource(makeResource(kind, origin), context).success);
        const auto old = manager.find<resource::ShaderArtifactResource>(resourceId);
        verifyArtifact(*old.get(), kind);
        const auto publication = root / publications[kind];
        const auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        core::pmr::String nativeDiagnostic;
        const auto descriptor = describe(kind, origin);
        if (kind == 0) {
            const auto native = shader::SingleEntryShaderArtifact::load(publication,
                std::get<shader::SingleEntryShaderLoadBudget>(descriptor.budget),
                quota,
                nativeDiagnostic);
            assert(native && native->identity() == old.get()->singleEntry()->identity());
            assert(native->inputBytes() == old.get()->singleEntry()->inputBytes());
            for (unsigned role = 0; role < static_cast<unsigned>(shader::ShaderArtifactRole::Count);
                ++role)
                assert(native->bytes(static_cast<shader::ShaderArtifactRole>(role))
                    == old.get()->singleEntry()->bytes(
                        static_cast<shader::ShaderArtifactRole>(role)));
        } else if (kind == 1) {
            const auto native = shader::GraphicsShaderModuleArtifact::load(publication,
                std::get<shader::GraphicsShaderModuleLoadBudget>(descriptor.budget),
                quota,
                nativeDiagnostic);
            assert(native && native->identity() == old.get()->graphicsModule()->identity());
            assert(native->inputBytes() == old.get()->graphicsModule()->inputBytes());
            assert(
                native->vertex()->identity() == old.get()->graphicsModule()->vertex()->identity());
            assert(native->fragment()->identity()
                == old.get()->graphicsModule()->fragment()->identity());
        } else if (kind == 2) {
            const auto native = shader::WebGlShaderProgramArtifact::load(publication,
                std::get<shader::WebGlShaderLoadBudget>(descriptor.budget),
                quota,
                nativeDiagnostic);
            assert(native && native->identity() == old.get()->webGlProgram()->identity());
            assert(native->vertex().glsl == old.get()->webGlProgram()->vertex().glsl);
            assert(native->fragment().glsl == old.get()->webGlProgram()->fragment().glsl);
        } else {
            const auto native = shader::OriginalDepthTableExpansionArtifact::load(publication,
                std::get<shader::OriginalDepthTableExpansionArtifact::Budget>(descriptor.budget),
                quota,
                nativeDiagnostic);
            assert(native && native->identity() == old.get()->depthTableExpansion()->identity());
            assert(native->inputBytes() == old.get()->depthTableExpansion()->inputBytes());
        }
        const resource::ResourceFileOrigin wrong{FilePathScope::MountedResource, {&files, 0, 2}};
        const auto rejected = manager.loadResource(makeResource(kind, wrong), context);
        assert(!rejected.success
            && rejected.diagnostics.find("SHADER-IO-SOURCE-LOCATION") != rejected.diagnostics.npos);
        assert(old.ready() && manager.generationOf(resourceId) == old.generation());
        assert(manager.find<resource::ShaderArtifactResource>(resourceId).get() == old.get());
        resource::ResourceSourceSet empty;
        auto missing = context;
        missing.sources = &empty;
        assert(!manager.loadResource(makeResource(kind, origin), missing).success);
        assert(old.ready());
        if (kind == 0) {
            auto invalid = describe(kind, origin);
            constexpr char malformed[] = "single/bad\0file.entry.yaml";
            invalid.publication =
                std::filesystem::u8path(malformed, malformed + sizeof(malformed) - 1);
            invalid.origin = {FilePathScope::HostFilesystem, {}};
            auto hostSources = files.snapshot(invalid.origin);
            resource::ResourceSourceBuffer malformedPublication;
            malformedPublication.path = core::FilePath::utf8(invalid.publication);
            malformedPublication.pathScope = FilePathScope::HostFilesystem;
            malformedPublication.bytes = hostSources.find(publications[0])->bytes;
            assert(hostSources.add(std::move(malformedPublication)));
            auto invalidContext = context;
            invalidContext.sources = &hostSources;
            auto candidate = core::makeUnique<resource::ShaderArtifactResource>(
                old.get()->reference(), std::move(invalid));
            const auto invalidPath = manager.loadResource(std::move(candidate), invalidContext);
            assert(!invalidPath.success && invalidPath.diagnostics == "SHADER-IO-SOURCE-PATH");
            assert(old.ready());
        }
        if (kind == 0) {
            for (const bool total : {false, true}) {
                auto invalid = describe(kind, origin);
                auto& budget = std::get<shader::SingleEntryShaderLoadBudget>(invalid.budget);
                if (total)
                    budget.maxTotalBytes = UINT64_MAX;
                else
                    budget.maxFileBytes = UINT64_MAX;
                auto candidate = core::makeUnique<resource::ShaderArtifactResource>(
                    old.get()->reference(), std::move(invalid));
                const auto rejectedBudget = manager.loadResource(std::move(candidate), context);
                assert(!rejectedBudget.success
                    && rejectedBudget.diagnostics == "SHADER-IO-LOAD-BUDGET");
                assert(old.ready());
            }
            auto descriptor = describe(kind, origin);
            shader::NativeBufferMemoryLoadRequest proof;
            std::get<shader::SingleEntryShaderLoadBudget>(descriptor.budget).nativeMemory = &proof;
            auto candidate = core::makeUnique<resource::ShaderArtifactResource>(
                old.get()->reference(), std::move(descriptor));
            const auto invalidOwner = manager.loadResource(std::move(candidate), context);
            assert(!invalidOwner.success
                && invalidOwner.diagnostics == "SHADER-IO-CONFIGURATION-OWNER");
            assert(old.ready());
        }
    }
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc == 3);
    if (core::StringView(argv[1]) == "--http") {
        for (unsigned i = 0; i < 20; ++i)
            run(http(argv[2]), i % kindCount);
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i)
            run(http(argv[2]), i % kindCount);
        const auto after = core::defaultCpuAllocator().stats();
        assert(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
        std::puts("Shader real HTTP: 20 warmup + 20 ordinary cycles, core bytes/count delta 0");
        return 0;
    }
    const FixtureFiles files(std::filesystem::u8path(argv[2]));
    sourceContracts(files, std::filesystem::u8path(argv[2]));
    for (unsigned kind = 0; kind < kindCount; ++kind) {
        run(files.memory(kind), kind);
        run(files.memory(kind, Mutation::Bom), kind);
        for (auto mutation : {Mutation::MissingTarget,
                 Mutation::BadHash,
                 Mutation::BadUtf8,
                 Mutation::EmbeddedNul,
                 Mutation::UnknownVersion}) {
            run(files.memory(kind, mutation), kind, false);
        }
        for (auto action :
            {Action::Cancel, Action::Remount, Action::ReadBudget, Action::MetadataBudget})
            run(files.memory(kind), kind, false, action);
    }
    for (unsigned i = 0; i < 20; ++i)
        run(files.memory(i % kindCount), i % kindCount);
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i)
        run(files.memory(i % kindCount), i % kindCount);
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
    std::puts(
        "Shader pure source, atomic replacement and graph contracts PASS; core bytes/count delta 0");
}
