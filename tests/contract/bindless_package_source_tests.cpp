#include "core/container/generation_pool.h"
#include "core/encoding/yaml_contract.h"
#include "core/io/http_file_backend.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "core/runtime/async_runtime.h"
#include "renderer/bindless_shader_package_resource.h"
#include "resource/resource_graph_executor.h"
#include "resource/shader_artifact_source.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/gpu_extensions.h"
#include "tests/fixtures/bindless_package_request_fixture.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string_view>

namespace {
using namespace arp;
constexpr std::size_t fileBudget = 2u * 1024u * 1024u;
constexpr std::size_t totalBudget = 16u * 1024u * 1024u;
constexpr std::size_t metadataBudget = 32u * 1024u * 1024u;
constexpr const char* id = "2c5c0a6c-4d02-40c0-8a93-e71877132943";
constexpr const char* publications[]{
    "BindlessFixture.native.bindless.yaml", "BindlessFixture.emulated.bindless.yaml"};

renderer::BindlessShaderPackageResourceDesc descriptor(
    unsigned mode, resource::ResourceFileOrigin origin)
{
    renderer::BindlessShaderPackageResourceDesc desc;
    desc.publication = publications[mode];
    desc.origin = origin;
    desc.budget = {fileBudget, totalBudget, 64, 16, 256, 64};
    desc.maxCpuMetadataBytes = metadataBudget;
    return desc;
}

core::UniquePtr<renderer::BindlessShaderPackageResource> resourceFor(
    unsigned mode, resource::ResourceFileOrigin origin)
{
    return core::makeUnique<renderer::BindlessShaderPackageResource>(
        resource::ResourceReference{id,
            renderer::BindlessShaderPackageResource::resourceTypeName(),
            "shader/bindless",
            "Contract Complete",
            {}},
        descriptor(mode, origin));
}

class Fixture final {
public:
    explicit Fixture(const std::filesystem::path& root)
    {
        for (const auto& entry : std::filesystem::directory_iterator(root)) {
            assert(entry.is_regular_file());
            auto bytes = core::readBinaryFile(entry.path(), fileBudget);
            assert(bytes.success);
            resource::ResourceSourceBuffer source;
            source.path = core::FilePath::utf8(entry.path().filename());
            source.bytes = std::move(bytes.bytes);
            files.push_back(std::move(source));
        }
        assert(files.size() <= 128);
    }

    resource::ResourceSourceSet sources(resource::ResourceFileOrigin origin) const
    {
        resource::ResourceSourceSet result;
        for (auto source : files) {
            source.pathScope = origin.pathScope;
            source.mount = origin.mount;
            assert(result.add(std::move(source)));
        }
        return result;
    }

    core::SharedPtr<core::io::MemoryFileBackend> backend() const
    {
        auto backend = core::makeShared<core::io::MemoryFileBackend>(
            core::io::MemoryFileBackendDesc{128, 2 * totalBudget, 1, fileBudget, 256});
        for (const auto& file : files)
            assert(backend->registerFile(file.path, file.bytes.data(), file.bytes.size())
                == core::io::FileIoStatus::Success);
        return backend;
    }

    core::pmr::Vector<resource::ResourceSourceBuffer> files;
};

void compare(const rhi::BindlessShaderPackage& expected, const rhi::BindlessShaderPackage& actual)
{
    assert(expected.sha256() == actual.sha256());
    assert(expected.artifact().shaderPackageIdentity == actual.artifact().shaderPackageIdentity);
    assert(expected.artifact().execution == actual.artifact().execution);
    assert(expected.files().size() == actual.files().size());
    assert(expected.modules().size() == actual.modules().size());
    assert(expected.sourceBytes() == actual.sourceBytes());
    for (std::size_t i = 0; i < expected.files().size(); ++i) {
        const auto& a = expected.files()[i];
        const auto& b = actual.files()[i];
        assert(a.name == b.name && a.sha256 == b.sha256 && a.bytes == b.bytes);
    }
}

void contracts(unsigned mode, const Fixture& fixture, const std::filesystem::path& hostRoot)
{
    core::GenerationPool<unsigned> generations(1);
    resource::ResourceFileOrigin origin{
        core::io::FilePathScope::MountedResource, generations.emplace(1)};
    auto sources = fixture.sources(origin);
    resource::ResourceLoadContext context;
    context.requirePreloadedSources = true;
    context.sources = &sources;
    context.maxDecodedBytes = metadataBudget;
    auto resource = resourceFor(mode, origin);
    resource::ResourceManager manager;
    const auto loaded = manager.loadResource(std::move(resource), context);
    if (!loaded.success)
        std::fprintf(stderr, "Bindless source: %s\n", loaded.diagnostics.c_str());
    assert(loaded.success);
    auto package = manager.find<renderer::BindlessShaderPackageResource>(id).get()->package();
    const auto budget = descriptor(mode, origin).budget;
    core::pmr::String diagnostic;
    const auto host =
        rhi::BindlessShaderPackage::load(hostRoot / publications[mode], budget, diagnostic);
    assert(host);
    compare(*host, *package);
    // The original Null planner remains validator-only. Loading remote bytes
    // neither changes the plan nor creates backend execution authority.
    auto device = rhi::createNullDevice();
    const auto* extension = static_cast<rhi::BindlessCrossExtension*>(
        device->queryInterface(rhi::kExtensionBindlessCross));
    assert(extension);
    const auto request = test::BindlessPackageRequestFixture::make(
        package->artifact(), extension->capabilities().deviceIdentity);
    const auto plan = extension->plan(request, package->artifact());
    const auto hostPlan = extension->plan(request, host->artifact());
    assert(plan.success && !plan.executable && plan.execution == rhi::BindlessExecution::Validator);
    assert(rhi::bindlessPlansMatch(plan, hostPlan));
    assert(manager.unload(id).success);
    assert(!package->files().empty() && package->quota() && package->quota()->liveBytes());

    for (unsigned mutation = 0; mutation < 7; ++mutation) {
        auto changed = fixture;
        const auto entry =
            std::find_if(changed.files.begin(), changed.files.end(), [&](const auto& file) {
                return file.path == publications[mode];
            });
        assert(entry != changed.files.end());
        const char* expected = "UTF8";
        if (mutation == 0)
            entry->bytes.insert(entry->bytes.begin(), 0xff);
        if (mutation == 1) {
            entry->bytes.push_back(0);
            expected = "NUL";
        }
        if (mutation == 2) {
            entry->bytes.insert(entry->bytes.begin(), {0xef, 0xbb, 0xbf});
            expected = "BOM";
        }
        if (mutation == 3 || mutation == 4) {
            const auto binary = std::find_if(
                package->files().begin(), package->files().end(), [](const auto& file) {
                    return std::filesystem::path(file.name.c_str()).extension() == ".spv";
                });
            assert(binary != package->files().end());
            const auto target =
                std::find_if(changed.files.begin(), changed.files.end(), [&](const auto& file) {
                    return file.path == binary->name;
                });
            assert(target != changed.files.end());
            if (mutation == 3) {
                target->bytes[0] ^= 1;
                expected = "RHI-BINDLESS-PACKAGE-HASH-MISMATCH";
            } else {
                changed.files.erase(target);
                expected = "SHADER-IO-SOURCE-MISSING";
            }
        }
        if (mutation == 5) {
            core::pmr::String text(
                reinterpret_cast<const char*>(entry->bytes.data()), entry->bytes.size());
            const auto version = text.find("contractVersion: 1");
            assert(version != core::pmr::String::npos);
            text.replace(
                version, std::string_view("contractVersion: 1").size(), "contractVersion: 99");
            entry->bytes.assign(text.begin(), text.end());
            expected = "RHI-BINDLESS-PACKAGE-PUBLICATION-VERSION";
        }
        if (mutation == 6) {
            using Y = core::YamlContract;
            auto envelope = Y::parseSingleDocument(
                {reinterpret_cast<const char*>(entry->bytes.data()), entry->bytes.size()});
            const auto payloadName = Y::text(envelope, "payload");
            const auto payloadFile =
                std::find_if(changed.files.begin(), changed.files.end(), [&](const auto& file) {
                    return file.path == payloadName;
                });
            assert(payloadFile != changed.files.end());
            auto payload =
                Y::parseSingleDocument({reinterpret_cast<const char*>(payloadFile->bytes.data()),
                    payloadFile->bytes.size()});
            auto& lowering = payload["loweringYaml"].get_value_ref<Y::Node::string_type&>();
            lowering.push_back('\0');
            payload["loweringSha256"] =
                core::toStdString(core::Sha256::hexDigest({lowering.data(), lowering.size()})
                        .substr(std::string_view("sha256:").size()));
            const auto serialized = Y::serialize(payload);
            payloadFile->bytes.assign(serialized.begin(), serialized.end());
            envelope["shaderPackageIdentity"] = core::toStdString(
                core::Sha256::hexDigest(serialized).substr(std::string_view("sha256:").size()));
            const auto publication = Y::serialize(envelope);
            entry->bytes.assign(publication.begin(), publication.end());
            const auto roundtrip = Y::parseSingleDocument(publication);
            assert(core::Sha256::matchesHexDigest(
                {reinterpret_cast<const char*>(payloadFile->bytes.data()),
                    payloadFile->bytes.size()},
                Y::text(roundtrip, "shaderPackageIdentity")));
            const auto parsedPayload = Y::parseSingleDocument(serialized);
            for (const auto& declared : Y::required(parsedPayload, "artifacts")
                     .get_value_ref<const Y::Node::mapping_type&>()) {
                const auto name = Y::text(declared.first);
                const auto file = std::find_if(
                    changed.files.begin(), changed.files.end(), [&](const auto& value) {
                        return value.path == name;
                    });
                assert(file != changed.files.end());
                const bool valid = core::Sha256::matchesHexDigest(
                    {reinterpret_cast<const char*>(file->bytes.data()), file->bytes.size()},
                    Y::text(declared.second));
                if (!valid)
                    std::fprintf(stderr, "Rehashed fixture file mismatch: %s\n", name.c_str());
                assert(valid);
            }
            // Raw YAML is valid UTF-8 with an escaped NUL, and both hashes agree.
            // The decoded nested lowering must still pass Text/1 before parsing.
            expected = "NUL";
        }
        auto input = changed.sources(origin);
        auto changedContext = context;
        changedContext.sources = &input;
        resource::AcquiredShaderArtifactSource projected(changedContext, origin, nullptr);
        auto selected = budget;
        selected.source = &projected;
        assert(!rhi::BindlessShaderPackage::load(publications[mode], selected, diagnostic));
        if (diagnostic.find(expected) == core::pmr::String::npos)
            std::fprintf(stderr,
                "Bindless bytes %u expected=%s got=%s\n",
                mutation,
                expected,
                diagnostic.c_str());
        assert(diagnostic.find(expected) != core::pmr::String::npos);
    }

    // Even an existing host publication must fail when a supplied source is absent.
    resource::ResourceSourceSet missing;
    auto missingContext = context;
    missingContext.sources = &missing;
    resource::AcquiredShaderArtifactSource absent(missingContext, {}, nullptr);
    auto strictBudget = budget;
    strictBudget.source = &absent;
    assert(
        !rhi::BindlessShaderPackage::load(hostRoot / publications[mode], strictBudget, diagnostic));
    assert(diagnostic == "SHADER-IO-SOURCE-MISSING");

    class OversizedSource final : public shader::ShaderArtifactSource {
        shader::ShaderArtifactSourceResult read(
            const std::filesystem::path&, std::uint64_t) const override
        {
            shader::ShaderArtifactSourceResult result;
            result.bytes = {1, 2};
            return result;
        }
    } oversized;

    strictBudget.maxFileBytes = strictBudget.maxTotalBytes = 1;
    strictBudget.source = &oversized;
    assert(
        !rhi::BindlessShaderPackage::load(hostRoot / publications[mode], strictBudget, diagnostic));
    assert(diagnostic == "RHI-BINDLESS-PACKAGE-BYTE-BUDGET");

    for (unsigned mutation = 0; mutation < 11; ++mutation) {
        auto candidate = descriptor(mode, origin);
        auto input = context;
        const char* expected = "SHADER-IO-LOAD-BUDGET";
        if (mutation == 0)
            candidate.maxCpuMetadataBytes = 0;
        if (mutation == 1)
            candidate.maxCpuMetadataBytes = SIZE_MAX;
        if (mutation == 2)
            input.maxDecodedBytes = metadataBudget - 1;
        if (mutation == 3)
            candidate.budget.maxFileBytes = 0;
        if (mutation == 4)
            candidate.budget.maxTotalBytes = UINT64_MAX;
        if (mutation == 5) {
            input.requirePreloadedSources = false;
            expected = "SHADER-IO-SOURCE-CONTEXT";
        }
        if (mutation == 6) {
            ++candidate.origin.mount.generation;
            expected = "SHADER-IO-SOURCE-LOCATION";
        }
        if (mutation == 7) {
            candidate.budget.source = &absent;
            expected = "SHADER-IO-CONFIGURATION-OWNER";
        }
        rhi::BindlessPackagePushLoadRequest push;
        if (mutation == 8) {
            candidate.budget.push = &push;
            expected = "SHADER-IO-CONFIGURATION-OWNER";
        }
        if (mutation == 9) {
            input.sources = &missing;
            expected = "SHADER-IO-SOURCE-MISSING";
        }
        if (mutation == 10) {
            candidate.budget.maxTotalBytes = 1;
            expected = "SHADER-IO-SOURCE-BUDGET";
        }
        auto rejected = core::makeUnique<renderer::BindlessShaderPackageResource>(
            resource::ResourceReference{id,
                renderer::BindlessShaderPackageResource::resourceTypeName(),
                "shader/bindless",
                "Contract Complete",
                {}},
            candidate);
        core::pmr::Vector<core::UniquePtr<resource::ResourceBase>> batch;
        batch.push_back(std::move(rejected));
        const auto* previous = manager.recordSnapshot(id);
        const auto generation = manager.generationOf(id);
        const auto result = manager.loadResourcesAtomically(std::move(batch), input);
        if (result.success || result.diagnostics.find(expected) == core::pmr::String::npos)
            std::fprintf(stderr,
                "Bindless mutation %u expected=%s got=%s\n",
                mutation,
                expected,
                result.diagnostics.c_str());
        assert(!result.success && result.diagnostics.find(expected) != core::pmr::String::npos);
        assert(manager.recordSnapshot(id) == previous && manager.generationOf(id) == generation);
    }
}

void graph(
    unsigned mode, core::SharedPtr<core::io::ManagedFileBackend> backend, bool cancel = false)
{
    auto mounts = core::makeShared<core::io::MountedFileBackend>(
        core::io::MountedFileBackendDesc{1, 1, fileBudget, 32, 256});
    resource::ResourceFileOrigin origin{
        core::io::FilePathScope::MountedResource, mounts->mount("shader", backend)};
    core::runtime::AsyncRuntimeDesc desc;
    desc.fileBackend = mounts;
    desc.enableWork = desc.enableThreadPoolFileBackend = false;
    desc.completionQueueCapacity = desc.completionBudget = 4;
    core::runtime::AsyncRuntime runtime(desc);
    resource::ResourceManager manager;
    assert(manager.declareResource(id, renderer::BindlessShaderPackageResource::resourceTypeName())
            .success);
    const auto* previous = manager.recordSnapshot(id);
    const auto generation = manager.generationOf(id);
    resource::ResourceGraphExecutor executor(
        manager, runtime.fileService(), runtime.completionQueue());
    resource::ResourceGraphExecutionRequest request;
    request.graph.rootResourceId = id;
    resource::ResourceGraphNode node;
    node.resource = resourceFor(mode, origin);
    request.graph.nodes.push_back(std::move(node));
    request.budget = {1, 64, totalBudget, metadataBudget, 4};
    core::runtime::CancellationSource cancellation;
    if (cancel)
        request.cancellation = cancellation.token();

    struct Completion {
        unsigned count = 0;
        resource::ResourceGraphExecutionResult result;
    } completion;

    assert(executor.submit(std::move(request),
               {[](void* p, resource::ResourceGraphExecutionResult result) {
                    auto& state = *static_cast<Completion*>(p);
                    ++state.count;
                    state.result = std::move(result);
                },
                   &completion})
        == resource::ResourceGraphSubmitStatus::Accepted);
    if (cancel)
        cancellation.cancel();
    unified_io_fixture::wait(runtime.completionQueue(), [&] {
        return completion.count != 0;
    });
    assert(completion.count == 1);
    if (!cancel && !completion.result.resource.success)
        std::fprintf(stderr,
            "Bindless graph: %s / %s\n",
            completion.result.diagnosticId ? completion.result.diagnosticId : "none",
            completion.result.resource.diagnostics.c_str());
    assert(completion.result.resource.success == !cancel);
    if (cancel) {
        assert(manager.recordSnapshot(id) == previous && manager.generationOf(id) == generation);
        assert(previous->state == resource::ResourceState::Declared);
    } else {
        auto package = manager.find<renderer::BindlessShaderPackageResource>(id).get()->package();
        assert(package
            && completion.result.telemetry.submittedFileReads == package->files().size() + 2);
        assert(completion.result.telemetry.bytesRead == package->sourceBytes());
        assert(runtime.workService().telemetry().submitted == 0);
        assert(manager.unload(id).success && !package->modules().empty());
    }
    runtime.shutdown();
    assert(!runtime.completionQueue().pending() && !runtime.completionQueue().reserved()
        && !mounts->telemetry().inFlight);
}

void cycle(const char* url, const Fixture* fixture, const std::filesystem::path& hostRoot)
{
    for (unsigned mode = 0; mode < 2; ++mode) {
        if (url) {
            core::io::HttpFileBackendDesc desc;
            desc.baseUrl = url;
            desc.maxInFlight = 1;
            // Explicit host budget includes bounded transport/header storage;
            // request payload alone is not the complete HTTP reservation.
            desc.maxBytesInFlight = totalBudget;
            desc.maxPathBytes = 256;
            desc.maxHeaderBytes = 8 * 1024;
            desc.ioChunkBytes = 4096;
            desc.timeoutMs = 3000;
            desc.cancellationPollMs = 10;
            graph(mode, core::makeShared<core::io::HttpFileBackend>(desc));
        } else {
            contracts(mode, *fixture, hostRoot);
            graph(mode, fixture->backend());
            graph(mode, fixture->backend(), true);
        }
    }
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc == 3 || (argc == 4 && std::string_view(argv[3]) == "--quick"));
    const bool http = std::string_view(argv[1]) == "--http";
    assert(http || std::string_view(argv[1]) == "--memory");
    core::UniquePtr<Fixture> fixture;
    if (!http)
        fixture = core::makeUnique<Fixture>(std::filesystem::u8path(argv[2]));
    if (argc == 4) {
        cycle(http ? argv[2] : nullptr, fixture.get(), std::filesystem::u8path(argv[2]));
        std::puts("Bindless source quick contract and Null/graph smoke passed");
        return 0;
    }
    for (unsigned i = 0; i < 20; ++i)
        cycle(http ? argv[2] : nullptr, fixture.get(), std::filesystem::u8path(argv[2]));
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i)
        cycle(http ? argv[2] : nullptr, fixture.get(), std::filesystem::u8path(argv[2]));
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes);
    assert(before.allocationCount - before.deallocationCount
        == after.allocationCount - after.deallocationCount);
    std::printf(
        "Bindless source: 20 warmup +20 measured cycles; baseline=%llu end=%llu peak=%llu; core counts returned to baseline\n",
        static_cast<unsigned long long>(before.liveBytes),
        static_cast<unsigned long long>(after.liveBytes),
        static_cast<unsigned long long>(after.peakBytes));
}
