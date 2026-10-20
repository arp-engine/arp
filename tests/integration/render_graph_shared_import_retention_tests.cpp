#include "renderer/render_graph/compiled_frame_graph.h"
#include "renderer/render_graph/frame_graph_compiler.h"
#include "renderer/render_graph/frame_graph_executor.h"
#include "renderer/render_graph/render_graph.h"
#include "rhi/backends/null/null_backend.h"

#ifdef ARP_TEST_VULKAN
#include "tests/fixtures/vulkan_heap_owner_fixture.h"
#endif

#include <cassert>
#include <cstdio>
#include <string_view>

namespace {
using namespace arp;
using Pool = render_graph::FrameGraphResourcePool;
using Graph = render_graph::FrameGraph;
using Compiled = render_graph::CompiledFrameGraph;
using Request = rhi::semantics::FrameGraphSharedImportsRequest;
constexpr std::uint64_t cpuBytes = 65536u;

auto quota(std::uint64_t bytes = cpuBytes)
{
    return core::makeShared<core::CpuMemoryQuota>(bytes);
}

void expect(const char* actual, const char* expected)
{
    assert(actual && std::string_view(actual) == expected);
}

void prepare(rhi::GpuDevice& device)
{
    assert(!device.prepareOriginalDeviceLifetime({1u, true, 1u, 2u, cpuBytes}, quota()));
    assert(!device.prepareOriginalResourceRegistration(
        {1u, true, 4u, 4u, 0u, 19u, cpuBytes}, quota()));
}

auto poolFor(rhi::GpuDevice& device)
{
    auto pool = core::makeShared<Pool>(device);
    assert(!pool->prepareRetirementMetadata({1u, true, 2u, cpuBytes}, quota()));
    return pool;
}

auto storage(Pool& pool, std::uint32_t references = 2u)
{
    auto owner = quota();
    assert(!pool.prepareRetirementFrameStorage(
        {1u, true, 2u, 1u, 1u, 0u, references, cpuBytes}, owner));
    return owner;
}

auto budget(bool retain = true, std::uint64_t bytes = cpuBytes)
{
    rhi::semantics::FrameGraphCompiledRetirementSourceBudget result{1u, true, 4u, bytes};
    if (retain)
        result.sharedImports = {1u, true};
    return result;
}

rhi::TextureDesc textureDesc()
{
    rhi::TextureDesc result;
    result.extent = {2u, 2u, 1u};
    result.format = rhi::TextureFormat::Rgba8Unorm;
    result.usage = rhi::TextureUsage::Sampled;
    return result;
}

rhi::BufferDesc bufferDesc()
{
    return {"retained import",
        64u,
        rhi::ResourceUsage::CopySrc,
        rhi::MemoryUsage::CpuToGpu,
        rhi::ResourceState::Undefined};
}

template <class F> void rejects(F&& operation, const char* expected)
{
    bool rejected = false;
    try {
        operation();
    } catch (const std::exception& error) {
        rejected = true;
        expect(error.what(), expected);
    }
    assert(rejected);
}

void contract()
{
    using Contract = rhi::semantics::FrameGraphSharedImportsContract;
    static_assert(std::is_base_of_v<core::SharedObjectIdentity, rhi::GpuTexture>);
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    assert(!Request{}.requested() && !Request{}.diagnostic());
    expect((Request{2u, true}).diagnostic(), "RG-SHARED-IMPORTS-VERSION");
    expect((Request{1u, false}).diagnostic(), "RG-SHARED-IMPORTS-OPT-IN");
    for (auto backend : {rhi::Backend::Null,
             rhi::Backend::Vulkan,
             rhi::Backend::Metal,
             rhi::Backend::WebGPU,
             rhi::Backend::WebGL,
             static_cast<rhi::Backend>(255)}) {
        auto d = Contract::lowering(backend);
        assert(!d.nativeExecutionProven);
        if (backend == rhi::Backend::Null || backend == rhi::Backend::Vulkan) {
            assert(!d.diagnostic && d.retentionAvailable);
            assert(d.validatorOnly == (backend == rhi::Backend::Null));
        } else {
            assert(!d.retentionAvailable && d.mode == rhi::CapabilityMode::DiagnosticFailure);
            expect(d.diagnostic,
                backend == static_cast<rhi::Backend>(255)
                    ? "RG-SHARED-IMPORTS-BACKEND"
                    : "RG-SHARED-IMPORTS-BACKEND-UNAVAILABLE");
        }
    }
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
}

void lifetime(rhi::GpuDevice& device)
{
    prepare(device);
    for (bool enabled : {false, true}) {
        auto pool = poolFor(device);
        auto persistent = storage(*pool);
        Compiled result, copy;
        std::weak_ptr<rhi::GpuTexture> observedTexture;
        std::weak_ptr<rhi::GpuBuffer> observedBuffer;
        {
            Graph graph(device, pool);
            auto texture = core::shareUnique(device.createTexture(textureDesc()));
            auto buffer = core::shareUnique(device.createBuffer(bufferDesc()));
            assert(texture && buffer);
            assert(texture->sharedOwnerMatches(texture) && buffer->sharedOwnerMatches(buffer));
            observedTexture = texture;
            observedBuffer = buffer;
            assert(graph.importTextureRef(texture).id == graph.importTextureRef(texture).id);
            assert(graph.importBufferRef(buffer).id == graph.importBufferRef(buffer).id);
            assert(!graph.prepareCompiledRetirementSource(budget(enabled), quota()));
            result = graph.compile();
            copy = result;
            assert(!graph.validateCompiledRetirementSource(result));
            if (enabled) {
                {
                    auto another = graph.compile();
                    assert(pool->retirementFrameStorageReport().retainedReferences == 4u);
                    rejects(
                        [&] {
                            graph.compile();
                        },
                        "RG-FRAME-STORAGE-FRAME-CAPACITY");
                }
                assert(pool->retirementFrameStorageReport().retainedReferences == 2u);
                rejects(
                    [&] {
                        graph.execute(result);
                    },
                    "RG-SHARED-IMPORTS-COMMAND-CUSTODY-UNAVAILABLE");
                rejects(
                    [&] {
                        graph.execute(Compiled{});
                    },
                    "RG-SHARED-IMPORTS-COMMAND-CUSTODY-UNAVAILABLE");
                rejects(
                    [&] {
                        render_graph::FrameGraphExecutor(graph).execute(result);
                    },
                    "RG-SHARED-IMPORTS-COMMAND-CUSTODY-UNAVAILABLE");
            }
            assert(pool->retirementFrameStorageReport().retainedReferences == (enabled ? 2u : 0u));
            graph.reset();
            expect(result.retirementSourceDiagnostic(), "RG-COMPILED-SOURCE-EPOCH");
        }
        assert(observedTexture.expired() == !enabled && observedBuffer.expired() == !enabled);
        pool->closeRetirementMetadata();
        assert(pool->retirementFrameStorageReport().reservedFrames == (enabled ? 1u : 0u));
        auto oldCharge = persistent->liveBytes();
        assert(oldCharge);
        assert(!pool->prepareRetirementMetadata({1u, true, 2u, cpuBytes}, quota()));
        auto replacementStorage = storage(*pool);
        const char* diagnostic = nullptr;
        auto replacement = pool->reserveRetirementMetadata(diagnostic);
        assert(!diagnostic && !replacement.frameStorageDiagnostic());
        // The old metadata state already retains its storage in the default
        // source-only contract. Shared-import opt-in additionally retains the
        // actual resource owners, distinguished by the weak-owner assertions.
        assert(persistent->liveBytes() == oldCharge);
        result = {};
        assert(observedTexture.expired() == !enabled);
        copy = {};
        assert(observedTexture.expired() && observedBuffer.expired());
        assert(persistent->liveBytes() == 0u);
        assert(pool->retirementFrameStorageReport().reservedFrames == 1u);
        assert(!replacement.frameStorageDiagnostic());
    }
    auto pool = poolFor(device);
    auto persistent = storage(*pool);
    Graph graph(device, pool);
    assert(!graph.prepareCompiledRetirementSource(budget(), quota()));
    auto retained = graph.compile();
    assert(!graph.prepareCompiledRetirementSource(budget(false), quota()));
    rejects(
        [&] {
            graph.execute(retained);
        },
        "RG-SHARED-IMPORTS-COMMAND-CUSTODY-UNAVAILABLE");
}

void rejection(rhi::GpuDevice& device, rhi::GpuDevice& foreign)
{
    prepare(device);
    prepare(foreign);
    for (unsigned failure = 0; failure != 13u; ++failure) {
        auto pool = poolFor(device);
        auto persistent = storage(*pool, failure == 4u ? 1u : 2u);
        Graph graph(device, pool);
        auto texture = core::shareUnique(device.createTexture(textureDesc()));
        auto buffer = core::shareUnique(device.createBuffer(bufferDesc()));
        auto unrelated = core::makeShared<unsigned>(0u);
        if (failure == 0u)
            graph.importTextureRef(std::shared_ptr<rhi::GpuTexture>(unrelated, texture.get()));
        else if (failure == 1u)
            graph.importBufferRef(std::shared_ptr<rhi::GpuBuffer>(unrelated, buffer.get()));
        else if (failure == 2u)
            graph.importTextureRef(
                std::shared_ptr<rhi::GpuTexture>(std::shared_ptr<void>{}, texture.get()));
        else if (failure == 3u)
            graph.importBufferRef(core::shareUnique(foreign.createBuffer(bufferDesc())));
        else if (failure == 4u) {
            graph.importTextureRef(texture);
            graph.importBufferRef(buffer);
        } else if (failure == 5u)
            graph.createTransientTexture(textureDesc());
        else if (failure == 6u)
            graph.createBuffer(bufferDesc());
        else if (failure == 7u)
            graph.importTextureRef(core::shareUnique(foreign.createTexture(textureDesc())));
        else if (failure == 8u)
            graph.importBufferRef(
                std::shared_ptr<rhi::GpuBuffer>(std::shared_ptr<void>{}, buffer.get()));
        else if (failure <= 10u) {
            auto unregistered = rhi::createNullDevice();
            if (failure == 9u)
                graph.importTextureRef(
                    core::shareUnique(unregistered->createTexture(textureDesc())));
            else
                graph.importBufferRef(core::shareUnique(unregistered->createBuffer(bufferDesc())));
        } else if (failure == 11u) {
            graph.importTextureRef(std::shared_ptr<rhi::GpuTexture>(unrelated, texture.get()));
            texture.reset();
        } else {
            graph.importBufferRef(std::shared_ptr<rhi::GpuBuffer>(unrelated, buffer.get()));
            buffer.reset();
        }
        assert(!graph.prepareCompiledRetirementSource(budget(), quota()));
        const auto textureOwners = texture.use_count(), bufferOwners = buffer.use_count();
        if (failure >= 11u)
            rejects(
                [&] {
                    render_graph::FrameGraphCompiler{}.compile(graph);
                },
                "RG-SHARED-IMPORTS-SHARED-OWNER");
        rejects(
            [&] {
                graph.compile();
            },
            failure < 3u || failure == 8u || failure >= 11u ? "RG-SHARED-IMPORTS-SHARED-OWNER"
                : failure == 3u || failure == 7u ? "RHI-ORIGINAL-REGISTRATION-FOREIGN-DEVICE"
                : failure >= 9u                  ? "RHI-ORIGINAL-REGISTRATION-EXPIRED"
                : failure == 4u                  ? "RG-FRAME-STORAGE-REFERENCE-CAPACITY"
                                                 : "RG-SHARED-IMPORTS-OWNER-UNAVAILABLE");
        assert(texture.use_count() == textureOwners && buffer.use_count() == bufferOwners);
        const auto report = pool->retirementFrameStorageReport();
        assert(!report.reservedFrames && !report.retainedReferences);
    }
    auto pool = poolFor(device);
    Graph missing(device, pool);
    expect(missing.prepareCompiledRetirementSource(budget(), quota()), "RG-FRAME-STORAGE-SOURCE");
    auto persistent = storage(*pool);
    auto invalid = budget();
    invalid.sharedImports = {2u, true};
    expect(missing.prepareCompiledRetirementSource(invalid, quota()), "RG-SHARED-IMPORTS-VERSION");
    invalid.sharedImports = {1u, false};
    expect(missing.prepareCompiledRetirementSource(invalid, quota()), "RG-SHARED-IMPORTS-OPT-IN");
    auto stale = core::shareUnique(device.createBuffer(bufferDesc()));
    missing.importBufferRef(stale);
    assert(!missing.prepareCompiledRetirementSource(budget(), quota()));
    device.retireOriginalDeviceLifetime();
    rejects(
        [&] {
            missing.compile();
        },
        "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED");
    assert(!pool->retirementFrameStorageReport().retainedReferences);
}

#ifdef ARP_TEST_VULKAN
void nativeOwners()
{
    auto native = core::makeUnique<tests::VulkanHeadlessRuntime>();
    assert(native->runtime() && native->runtime()->valid());
    auto device = tests::VulkanHeapOwnerFixture::makeDevice(native->runtime(), false);
    prepare(*device);
    auto pool = poolFor(*device);
    auto persistent = storage(*pool);
    const std::weak_ptr<rhi_vulkan::VulkanRhiRuntime> observedRuntime = native->runtime();
    const std::weak_ptr<rhi_vulkan::VulkanRhiNativeRuntimeOwner> observedNative =
        native->nativeOwner();
    std::weak_ptr<rhi_vulkan::VulkanRhiNativeResourcePayload> observedTexture, observedBuffer;
    Compiled result;
    {
        Graph graph(*device, pool);
        auto texture = core::shareUnique(device->createTexture(textureDesc()));
        auto buffer = core::shareUnique(device->createBuffer(bufferDesc()));
        observedTexture = rhi_vulkan::vulkanRhiNativePayload(*texture);
        observedBuffer = rhi_vulkan::vulkanRhiNativePayload(*buffer);
        graph.importTextureRef(texture);
        graph.importBufferRef(buffer);
        assert(!graph.prepareCompiledRetirementSource(budget(), quota()));
        result = graph.compile();
    }
    pool->closeRetirementMetadata();
    pool.reset();
    device.reset();
    native.reset();
    assert(!observedRuntime.expired() && !observedNative.expired());
    assert(persistent->liveBytes());
    {
        auto texture = observedTexture.lock();
        auto buffer = observedBuffer.lock();
        assert(texture && buffer);
        assert(texture->backendVulkanImageHandle() && buffer->backendVulkanBufferHandle());
        assert(texture->backendVulkanDeviceHandle() == buffer->backendVulkanDeviceHandle());
        VkMemoryRequirements imageRequirements{}, bufferRequirements{};
        vkGetImageMemoryRequirements(texture->backendVulkanDeviceHandle(),
            texture->backendVulkanImageHandle(),
            &imageRequirements);
        vkGetBufferMemoryRequirements(buffer->backendVulkanDeviceHandle(),
            buffer->backendVulkanBufferHandle(),
            &bufferRequirements);
        assert(imageRequirements.size && bufferRequirements.size >= bufferDesc().sizeBytes);
    }
    result = {};
    assert(observedTexture.expired() && observedBuffer.expired());
    assert(observedRuntime.expired() && observedNative.expired());
    assert(!persistent->liveBytes());
    std::puts(
        "original Vulkan image/buffer survive graph, pool, facade and runtime bootstrap: PASS");
}
#endif

void allocationRollback()
{
    auto device = rhi::createNullDevice();
    prepare(*device);
    auto pool = poolFor(*device);
    auto persistent = storage(*pool);
    auto texture = core::shareUnique(device->createTexture(textureDesc()));
    auto buffer = core::shareUnique(device->createBuffer(bufferDesc()));
    auto measured = quota();
    {
        Graph graph(*device, pool);
        graph.importTextureRef(texture);
        graph.importBufferRef(buffer);
        assert(!graph.prepareCompiledRetirementSource(budget(), measured));
        auto result = graph.compile();
        assert(!graph.validateCompiledRetirementSource(result));
    }
    const auto exact = measured->peakBytes();
    assert(exact && exact < cpuBytes && !measured->liveBytes());
    for (std::uint64_t bytes = 1; bytes <= exact; ++bytes) {
        auto boundary = quota(bytes);
        {
            Graph graph(*device, pool);
            graph.importTextureRef(texture);
            graph.importBufferRef(buffer);
            auto id = graph.prepareCompiledRetirementSource(budget(true, bytes), boundary);
            if (id)
                expect(id, "RG-COMPILED-SOURCE-ALLOCATION");
            else if (bytes < exact)
                rejects(
                    [&] {
                        graph.compile();
                    },
                    "RG-COMPILED-SOURCE-ALLOCATION");
            else {
                auto result = graph.compile();
                assert(pool->retirementFrameStorageReport().retainedReferences == 2u);
            }
        }
        assert(texture.use_count() == 1u && buffer.use_count() == 1u);
        assert(!boundary->liveBytes() && boundary->peakBytes() <= bytes);
        assert(!pool->retirementFrameStorageReport().reservedFrames);
        assert(!pool->retirementFrameStorageReport().retainedReferences);
    }
    std::printf("shared import source measured exact CPU quota: %llu\n",
        static_cast<unsigned long long>(exact));
}
} // namespace

int main(int argc, char** argv)
{
    const std::string_view mode = argc == 2 ? argv[1] : "contract";
    if (mode == "contract")
        contract();
    else if (mode == "quota")
        allocationRollback();
#ifdef ARP_TEST_VULKAN
    else if (mode == "native") {
        tests::VulkanHeadlessRuntime native;
        if (!native.runtime() || !native.runtime()->valid())
            return 77;
        auto original = tests::VulkanHeapOwnerFixture::makeDevice(native.runtime(), false);
        lifetime(*original);
        original = tests::VulkanHeapOwnerFixture::makeDevice(native.runtime(), false);
        auto foreign = tests::VulkanHeapOwnerFixture::makeDevice(native.runtime(), false);
        rejection(*original, *foreign);
        nativeOwners();
    }
#endif
    else {
        auto original = rhi::createNullDevice();
        if (mode == "lifetime")
            lifetime(*original);
        else if (mode == "rejection") {
            auto foreign = rhi::createNullDevice();
            rejection(*original, *foreign);
        } else
            return 2;
    }
    std::puts("FrameGraphCompiledSharedImports/1 Prototype owner retention: PASS");
}
