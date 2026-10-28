#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_device.h"
#include "rhi/backends/vulkan/vulkan_original_texture_upload.h"
#include "rhi/backends/vulkan/vulkan_query_pool.h"
#include "rhi/backends/vulkan/vulkan_recording_storage.h"
#include "rhi/backends/vulkan/vulkan_retained_transfer_command.h"
#include "rhi/backends/vulkan/vulkan_staging_upload_pool.h"
#include "rhi/frontend/command_recorded_state.h"
#include "rhi/frontend/command_stream_program.h"
#include "rhi/frontend/memory_upload_staging_budget.h"
#include "rhi/frontend/transfer_upload_pool_initialization.h"
#include "rhi/semantics/query_lowering.h"
#include "tests/fixtures/cpu_quota_factory_fixture.h"
#include "tests/fixtures/original_command_texture_upload_inputs_fixture.h"
#include "tests/fixtures/renderdoc_capture.h"
#include "tests/fixtures/vulkan_core_capability_query_fixture.h"
#include "tests/fixtures/vulkan_headless_runtime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>
#include <thread>
#include <type_traits>

namespace {
using namespace arp;
using namespace rhi;
using namespace rhi_vulkan;
using Storage = common::CommandRetentionStorage;
static_assert(!std::is_copy_constructible_v<VulkanRhiRetainedTransferCommand>
    && !std::is_move_constructible_v<VulkanRhiRetainedTransferCommand>);

constexpr std::uint64_t kMetadataBytes = 64u * 1024u;

void texturePoolInitializationContract()
{
    using Ordinary = OriginalOrdinaryTextureUploadContract;
    const Ordinary::Budget ordinary{1u, true, 1u, 2u, 768u};
    TextureDesc ordinaryDestination;
    ordinaryDestination.format = TextureFormat::Rgba8Unorm;
    assert(!Ordinary::inspectDestination(ordinaryDestination));
    ordinaryDestination.format = TextureFormat::Bc1Unorm;
    assert(Ordinary::inspectDestination(ordinaryDestination));
    for (const auto backend : {Backend::Null,
             Backend::Vulkan,
             Backend::Metal,
             Backend::WebGPU,
             Backend::WebGL,
             static_cast<Backend>(255u)}) {
        const auto decision = Ordinary::lowering(backend, ordinary);
        assert(decision.validatorOnly == (backend == Backend::Null)
            && decision.privateRecordingAvailable == (backend == Backend::Vulkan)
            && decision.completionPublicationRequired
            && (decision.diagnostic == nullptr) == (backend == Backend::Vulkan));
    }
    auto invalidOrdinary = ordinary;
    invalidOrdinary.contractVersion = 0u;
    assert(Ordinary::validate(invalidOrdinary));
    invalidOrdinary = ordinary;
    invalidOrdinary.prototypeOptIn = false;
    assert(Ordinary::validate(invalidOrdinary));
    invalidOrdinary = ordinary;
    invalidOrdinary.maxMipMappings = 0u;
    assert(Ordinary::validate(invalidOrdinary));
    using C = OriginalTexturePoolInitializationContract;
    const C::Budget budget{1u, true, 768u, 2u};
    for (const auto backend : {Backend::Null,
             Backend::Vulkan,
             Backend::Metal,
             Backend::WebGPU,
             Backend::WebGL,
             static_cast<Backend>(255u)}) {
        const auto decision = C::lowering(backend, budget);
        assert(decision.validatorOnly == (backend == Backend::Null)
            && decision.initializationAvailable == (backend == Backend::Vulkan)
            && !decision.executionAvailable && !decision.registeredDestinationRequired
            && (decision.diagnostic == nullptr) == (backend == Backend::Vulkan));
        auto registered = budget;
        registered.contractVersion = 2u;
        const auto joined = C::lowering(backend, registered);
        assert(joined.validatorOnly == decision.validatorOnly
            && joined.initializationAvailable == decision.initializationAvailable
            && !joined.executionAvailable
            && joined.registeredDestinationRequired == (backend != static_cast<Backend>(255u)));
    }
    auto bad = budget;
    bad.contractVersion = 0u;
    assert(std::string_view(C::validate(bad)) == "RHI-TEXTURE-POOL-INITIALIZATION-VERSION");
    bad.contractVersion = 3u;
    assert(std::string_view(C::validate(bad)) == "RHI-TEXTURE-POOL-INITIALIZATION-VERSION");
    bad = budget;
    bad.prototypeOptIn = false;
    assert(std::string_view(C::validate(bad)) == "RHI-TEXTURE-POOL-INITIALIZATION-OPT-IN");
    bad = budget;
    bad.maxPackedBytes = 0u;
    assert(std::string_view(C::validate(bad)) == "RHI-TEXTURE-POOL-INITIALIZATION-BUDGET");
    bad = budget;
    bad.maxMipMappings = 0u;
    assert(std::string_view(C::validate(bad)) == "RHI-TEXTURE-POOL-INITIALIZATION-BUDGET");
    assert(C::inspect({}, {}, 0u, {}, budget));

    class UnboundedFlusher final : public VulkanRhiMappedBufferWriteFlusher {
    public:
        bool flushMappedWrite(std::uint64_t, std::uint64_t, core::pmr::String&) override
        {
            assert(false);
            return false;
        }
    } flusher;

    assert(std::string_view(flusher.flushMappedWriteBounded(0u, 1u))
        == "RHI-VULKAN-MAPPED-WRITE-BOUNDED-UNAVAILABLE");
}

class OriginalQueryCreationFixture final {
public:
    OriginalQueryCreationFixture()
    {
        for (auto& quota : quotas)
            quota = core::makeShared<core::CpuMemoryQuota>(kMetadataBytes);
        input = snapshot(desc);
        assert(input);
    }

    std::shared_ptr<const QueryPoolInputSnapshot> snapshot(const QueryPoolDesc& source)
    {
        const QueryPoolInputSnapshotBudget b{
            1u, true, 1u, desc.label.size(), kMetadataBytes, kMetadataBytes, kMetadataBytes};
        return QueryPoolInputSnapshot::prepareOriginal(
            source, b, quotas[0], quotas[1], quotas[2], diagnostic);
    }

    VulkanOriginalQueryPoolBudget budget{
        1u, true, 1u, 2u, 1u, std::string_view("Timestamp.Original.原始").size(), kMetadataBytes};
    std::array<std::uint64_t, 1u> ids{0x7182u};
    QueryPoolDesc desc{"Timestamp.Original.原始", QueryType::Timestamp, 2u, ids.data(), 1u};
    std::array<std::shared_ptr<core::CpuMemoryQuota>, 4u> quotas;
    std::shared_ptr<const QueryPoolInputSnapshot> input;
    const char* diagnostic = nullptr;
};

void originalQueryCreationContract()
{
    for (const auto backend :
        {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
        using Lowering = semantics::QueryLowering;
        const auto decision = Lowering::originalRecording(backend, 1u, true);
        assert(decision.validatorOnly == (backend == Backend::Null)
            && decision.privateRecordingAvailable == (backend == Backend::Vulkan)
            && bool(decision.diagnostic) == (backend != Backend::Vulkan));
        assert(std::string_view(Lowering::originalRecording(backend, 0u, true).diagnostic)
                == "RHI-ORIGINAL-QUERY-RECORDING-VERSION"
            && std::string_view(Lowering::originalRecording(backend, 2u, true).diagnostic)
                == "RHI-ORIGINAL-QUERY-RECORDING-VERSION"
            && std::string_view(Lowering::originalRecording(backend, 1u, false).diagnostic)
                == "RHI-ORIGINAL-QUERY-RECORDING-OPT-IN");
    }
    using QueryStorage = OriginalQueryRecordingStorageContract;
    for (const auto backend :
        {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
        const auto decision = QueryStorage::lowering(backend);
        assert(!decision.executionAvailable && decision.diagnostic);
        assert(decision.validatorOnly == (backend == Backend::Null));
        assert(decision.nativeStorageImplemented == (backend == Backend::Vulkan));
    }
    const CommandRetentionBudget storageBudget{1u, true, 0u, 1u, 2u, 0u, kMetadataBytes};
    assert(QueryStorage::inspect({}, storageBudget, {}).diagnostic);
    using Lowering = semantics::OriginalQueryCreationLowering;
    using Facts = semantics::VulkanTimestampQueryFacts;
    static_assert(!std::is_copy_constructible_v<VulkanRhiQueryPool>);
    OriginalQueryCreationFixture f;
    semantics::OriginalQueryCreationFacts facts;
    facts.vulkanRuntimeDiagnostic = nullptr;
    facts.vulkanTimestamp = {true, true, true, true, true, true, true, true, 64u, 1.0};
    const auto decide = [&](const auto& actual) {
        return Lowering::decide(Backend::Vulkan, 1u, true, f.input, actual);
    };
    const auto valid = decide(facts);
    assert(
        valid.creationEligible() && !valid.executionSupported() && valid.matchesOriginal(f.input));
    for (auto member : {&Facts::propertiesQueried,
             &Facts::apiCore12,
             &Facts::featuresQueried,
             &Facts::hostResetSupported,
             &Facts::enablementReported,
             &Facts::hostResetEnabled,
             &Facts::queueVerified,
             &Facts::entrypointsAvailable}) {
        auto missing = facts;
        missing.vulkanTimestamp.*member = false;
        assert(!decide(missing).creationEligible());
    }
    auto closed = facts;
    closed.vulkanRuntimeDiagnostic = "RHI-VULKAN-TIMESTAMP-RUNTIME-UNAVAILABLE";
    assert(!decide(closed).creationEligible());
    auto forged = std::shared_ptr<const QueryPoolInputSnapshot>(f.quotas[0], f.input.get());
    assert(!Lowering::decide(Backend::Vulkan, 1u, true, forged, facts).creationEligible());
    assert(!Lowering::decide(Backend::Vulkan, 0u, true, f.input, facts).creationEligible()
        && !Lowering::decide(Backend::Vulkan, 1u, false, f.input, facts).creationEligible());
    for (const auto backend : {Backend::Null, Backend::Metal, Backend::WebGL})
        assert(!Lowering::decide(backend, 1u, true, f.input, facts).creationEligible());
    // The Vulkan facts cannot stand in for WebGPU's own feature/API evidence.
    assert(!Lowering::decide(Backend::WebGPU, 1u, true, f.input, facts).creationEligible());
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
        f.quotas, [&]() noexcept -> const char* {
            assert(decide(facts).creationEligible());
            assert(!VulkanRhiQueryPool::prepareOriginal(
                {}, f.input, f.budget, f.quotas[3], f.diagnostic));
            assert(std::string_view(f.diagnostic) == "RHI-VULKAN-TIMESTAMP-RUNTIME-UNAVAILABLE");
            assert(!VulkanRhiQueryPool::validateOriginalBudget(f.budget));
            return nullptr;
        }));
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    std::puts("original Vulkan query contract: facts/identity/opt-in and four-backend "
              "creation-only decisions PASS; command execution gates closed");
}

void originalQueryRegistrationContract()
{
    using Contract = OriginalResourceRegistrationContract;
    OriginalResourceRegistrationBudget b{1u, true, 0u, 0u, 0u, 5u, kMetadataBytes};
    b.queries = {1u, true, 1u};
    assert(!Contract::validateIntent(b));
    --b.maxRetainedOwners;
    assert(
        std::string_view(Contract::validateIntent(b)) == "RHI-ORIGINAL-REGISTRATION-OWNER-BUDGET");
    ++b.maxRetainedOwners;
    const auto vulkan = Contract::queryLowering(Backend::Vulkan, b.queries);
    assert(vulkan.mode == CapabilityMode::Equivalent && vulkan.typedLifetimeIssuerAvailable
        && !vulkan.executionAvailable && !vulkan.diagnostic);
    for (const auto backend : {Backend::Null, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
        const auto rejected = Contract::queryLowering(backend, b.queries);
        assert(rejected.diagnostic && !rejected.typedLifetimeIssuerAvailable
            && !rejected.executionAvailable);
    }
    for (auto version : {7u, 8u, 9u}) {
        b.contractVersion = version;
        assert(std::string_view(Contract::validateIntent(b))
            == "RHI-ORIGINAL-QUERY-NATIVE-LOSS-JOIN-UNAVAILABLE");
    }
    b.contractVersion = 1u;
    b.queries.contractVersion = 2u;
    assert(Contract::validateIntent(b));
    b.queries.contractVersion = 1u;
    b.queries.prototypeOptIn = false;
    assert(Contract::validateIntent(b));
    b.queries.prototypeOptIn = true;
    b.queries.maxQueryPools = 0u;
    assert(Contract::validateIntent(b));
    OriginalCommandInputsBudget inputs;
    inputs.contractVersion = 9u;
    inputs.prototypeOptIn = true;
    assert(OriginalCommandPipelineBindingContract::validateBudget(inputs));
    std::puts(
        "query registration contract: four-backend refusal/issuer and explicit loss join PASS");
}

void commandProfilingContract()
{
    using Contract = CommandProfilingContract;
    using Facts = semantics::VulkanTimestampQueryFacts;
    CommandProfilingBudget budget{1u, true, 1u, 2u, 2u};
    const Facts valid{true, true, true, true, true, true, true, true, 64u, 1.0};
    assert(!Contract::validateIntent(budget));
    for (const auto backend :
        {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
        const auto decision = Contract::lower(backend, budget, valid);
        assert(decision.executable() == (backend == Backend::Vulkan)
            && decision.validatorOnly == (backend == Backend::Null)
            && !decision.nativeAllocationFootprintKnown);
        assert(bool(decision.diagnostic) == (backend != Backend::Vulkan));
        auto invalid = budget;
        invalid.contractVersion = 0u;
        assert(!Contract::lower(backend, invalid, valid).executable());
        invalid = budget;
        invalid.prototypeOptIn = false;
        assert(!Contract::lower(backend, invalid, valid).executable());
    }
    for (const auto field : {&CommandProfilingBudget::maxInternalPools,
             &CommandProfilingBudget::maxInternalTimestampWrites,
             &CommandProfilingBudget::maxPublishedScopes}) {
        auto shortBudget = budget;
        shortBudget.*field = 0u;
        assert(Contract::validateIntent(shortBudget));
    }
    auto shortWrites = budget;
    --shortWrites.maxInternalTimestampWrites;
    assert(Contract::validateIntent(shortWrites));
    auto overflow = budget;
    overflow.maxPublishedScopes = GpuTimingSample::kMaxScopes + 1u;
    assert(Contract::validateIntent(overflow));
    budget.requireNativeAllocationFootprint = true;
    assert(std::string_view(Contract::lower(Backend::Vulkan, budget, valid).diagnostic)
        == "RHI-COMMAND-PROFILING-NATIVE-FOOTPRINT-UNAVAILABLE");
    budget.requireNativeAllocationFootprint = false;
    for (const auto field : {&Facts::propertiesQueried,
             &Facts::apiCore12,
             &Facts::featuresQueried,
             &Facts::hostResetSupported,
             &Facts::enablementReported,
             &Facts::hostResetEnabled,
             &Facts::queueVerified,
             &Facts::entrypointsAvailable}) {
        auto missing = valid;
        missing.*field = false;
        const auto rejected = Contract::lower(Backend::Vulkan, budget, missing);
        assert(!rejected.executable() && rejected.mode == CapabilityMode::Degraded
            && rejected.diagnostic);
    }
    assert(!Contract::lower(Backend::Vulkan, budget, valid, "runtime closed").executable());
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
    assert(Contract::lower(Backend::Vulkan, budget, valid).executable());
    std::pmr::set_default_resource(previous);
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    std::puts("internal profiling contract: independent pool/write/publication limits, "
              "four-backend disposition, native facts and strict footprint refusal PASS");
}

void timestampContract()
{
    using Facts = semantics::VulkanTimestampQueryFacts;
    using Lowering = semantics::QueryLowering;
    assert(Lowering::timestampDuration(7u, 11u, 2.5).nanoseconds == 10u);
    assert(!Lowering::timestampDuration(7u, 7u, 2.5).diagnostic);
    assert(std::string_view(Lowering::timestampDuration(11u, 7u, 1.0).diagnostic)
        == "RHI-TIMESTAMP-DURATION-NONMONOTONIC");
    assert(std::string_view(
               Lowering::timestampDuration(0u, std::numeric_limits<std::uint64_t>::max(), 1.0)
                   .diagnostic)
        == "RHI-TIMESTAMP-DURATION-OVERFLOW");
    for (const auto period : {0.0,
             -1.0,
             std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::quiet_NaN()})
        assert(Lowering::timestampDuration(1u, 2u, period).diagnostic);
    const Facts valid{true, true, true, true, true, true, true, true, 64u, 1.0};
    assert(!Lowering::vulkanTimestampDiagnostic(valid));
    auto oneBit = valid;
    oneBit.timestampValidBits = 1u;
    assert(!Lowering::vulkanTimestampDiagnostic(oneBit));
    VulkanRhiRuntimeDesc syntheticDesc;
    syntheticDesc.enableSyntheticRuntime = true;
    const auto synthetic = createVulkanRhiRuntime(syntheticDesc);
    assert(synthetic && synthetic->syntheticRuntime() && !synthetic->timestampQueriesEnabled());
    for (auto member : {&Facts::propertiesQueried,
             &Facts::apiCore12,
             &Facts::featuresQueried,
             &Facts::hostResetSupported,
             &Facts::enablementReported,
             &Facts::hostResetEnabled,
             &Facts::queueVerified,
             &Facts::entrypointsAvailable}) {
        auto invalid = valid;
        invalid.*member = false;
        assert(Lowering::vulkanTimestampDiagnostic(invalid));
    }
    for (const auto bits : {0u, 65u}) {
        auto invalid = valid;
        invalid.timestampValidBits = bits;
        assert(std::string_view(Lowering::vulkanTimestampDiagnostic(invalid))
            == "RHI-VULKAN-TIMESTAMP-QUEUE-UNAVAILABLE");
    }
    for (const double period : {0.0,
             -1.0,
             std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::quiet_NaN()}) {
        auto invalid = valid;
        invalid.timestampPeriodNanoseconds = period;
        assert(std::string_view(Lowering::vulkanTimestampDiagnostic(invalid))
            == "RHI-VULKAN-TIMESTAMP-PERIOD-INVALID");
    }
    tests::VulkanCoreCapabilityQueryFixture fixture;
    fixture.propertiesQueryAvailable = fixture.featuresQueryAvailable = true;
    fixture.timestampEntrypointsAvailable = fixture.hostResetSupported = true;
    fixture.families[0].timestampValidBits = 64u;
    const auto descriptor = [&] {
        auto desc = fixture.descriptor();
        desc.hostQueryResetEnablementReported = desc.hostQueryResetEnabled = true;
        return desc;
    };
    auto runtime = createVulkanRhiRuntime(descriptor());
    assert(runtime && runtime->timestampQueriesEnabled());
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    for (unsigned i = 0; i < 64u; ++i)
        assert(runtime->timestampQueriesEnabled());
    assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
    // Injected functions validate decisions only; never execute native work.
    for (auto member : {&tests::VulkanCoreCapabilityQueryFixture::propertiesQueryAvailable,
             &tests::VulkanCoreCapabilityQueryFixture::featuresQueryAvailable,
             &tests::VulkanCoreCapabilityQueryFixture::timestampEntrypointsAvailable,
             &tests::VulkanCoreCapabilityQueryFixture::hostResetSupported,
             &tests::VulkanCoreCapabilityQueryFixture::queueQueryAvailable}) {
        fixture.*member = false;
        auto rejected = createVulkanRhiRuntime(descriptor());
        assert(rejected && !rejected->timestampQueriesEnabled());
        fixture.*member = true;
    }
    auto desc = descriptor();
    desc.hostQueryResetEnablementReported = false;
    assert(!createVulkanRhiRuntime(desc)->timestampQueriesEnabled());
    desc = descriptor();
    desc.hostQueryResetEnabled = false;
    assert(!createVulkanRhiRuntime(desc)->timestampQueriesEnabled());
    fixture.queriedQueue = syntheticHandle<VkQueue>(99u);
    assert(!createVulkanRhiRuntime(descriptor())->timestampQueriesEnabled());
    fixture.queriedQueue = syntheticHandle<VkQueue>(4u);
    fixture.families[0].timestampValidBits = 0u;
    assert(!createVulkanRhiRuntime(descriptor())->timestampQueriesEnabled());
    fixture.families[0].timestampValidBits = 64u;
    fixture.timestampPeriod = 0.0f;
    assert(!createVulkanRhiRuntime(descriptor())->timestampQueriesEnabled());
    fixture.timestampPeriod = 1.0f;
    fixture.apiVersion = VK_API_VERSION_1_1;
    assert(!createVulkanRhiRuntime(descriptor())->timestampQueriesEnabled());
    fixture.apiVersion = VK_API_VERSION_1_2;
    fixture.timestampEntrypointsAvailable = false;
    assert(!runtime->timestampQueriesEnabled());
    fixture.timestampEntrypointsAvailable = true;
    fixture.loseDeviceOnQueryCreate = true;
    desc = descriptor();
    auto lost = createVulkanRhiRuntime(desc);
    assert(lost && lost->timestampQueriesEnabled());
    assert(!createVulkanRhiQueryPool(lost, {"Lost", QueryType::Timestamp, 2u})
        && !lost->timestampQueriesEnabled());
    std::puts(
        "timestamp contract: queried/reset-enabled/queue/API gates PASS; no native payload claim");
}

core::UniquePtr<Storage> storage(
    const std::shared_ptr<core::CpuMemoryQuota>& quota, std::uint32_t owners = 2u)
{
    const char* diagnostic = nullptr;
    auto result =
        Storage::prepare({1u, true, owners, 0u, 0u, 0u, kMetadataBytes}, quota, diagnostic);
    assert(result && !diagnostic);
    return result;
}

void contract()
{
    auto quota = core::makeShared<core::CpuMemoryQuota>(kMetadataBytes);
    auto registry = core::makeShared<VulkanRhiResourceRegistry>();
    VulkanRhiNativeResourceAllocator resources;
    core::pmr::String diagnostic;
    diagnostic.reserve(512u);
    auto prepared = storage(quota);
    assert(!VulkanRhiRetainedTransferCommand::prepare(
        resources, registry, std::move(prepared), diagnostic));
    assert(diagnostic == "RHI-VULKAN-RETENTION-NATIVE-RUNTIME-REQUIRED" && !quota->liveBytes());
    prepared = storage(quota);
    assert(prepared->sealForSubmission());
    assert(!VulkanRhiRetainedTransferCommand::prepare(
        resources, registry, std::move(prepared), diagnostic));
    assert(diagnostic == "RHI-VULKAN-RETENTION-PREPARATION-STATE" && !quota->liveBytes());
    prepared = storage(quota);
    quota->setLimitBytes(kMetadataBytes + 1u);
    assert(!VulkanRhiRetainedTransferCommand::prepare(
        resources, registry, std::move(prepared), diagnostic));
    assert(diagnostic == "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH" && !quota->liveBytes());
    CommandEncoderDesc request;
    request.retentionBudget = {1u, true, 2u, 0u, 0u, 0u, kMetadataBytes};
    assert(!createVulkanRhiNativeCommandPayload(resources, diagnostic, request));
    assert(diagnostic == "RHI-COMMAND-RETENTION-UNSUPPORTED"
        && !resources.stats().nativeCommandPoolCreateCount);
    std::puts(
        "retention contract: explicit public admission remains closed; invalid private preparation rejected");
}

bool failSubmit = false;
unsigned queueSubmitCalls = 0u, fenceStatusCalls = 0u;
unsigned debugLabelProcedureLookups = 0u;
bool failFenceObservation = false;
unsigned clearCreationFailure = 0u;
bool missingClearRecord = false;
bool missingCommandEnd = false;
const char* missingQueryProcedure = nullptr;
bool failQueryCreation = false;
bool loseQueryResetAfterCreation = false;
unsigned queryCreateCalls = 0u, queryLivePools = 0u, queryResets = 0u;
VkQueryPool observedQueryPool = VK_NULL_HANDLE;
unsigned observedQueryReads = 0u, queryWriteCalls = 0u;
bool refuseObservedQueryResult = false;
bool refuseInternalQueryResult = false;

VKAPI_ATTR void VKAPI_CALL queryWrite(
    VkCommandBuffer command, VkPipelineStageFlagBits stage, VkQueryPool pool, std::uint32_t index)
{
    ++queryWriteCalls;
    vkCmdWriteTimestamp(command, stage, pool, index);
}

VKAPI_ATTR VkResult VKAPI_CALL queryResults(VkDevice device,
    VkQueryPool pool,
    std::uint32_t first,
    std::uint32_t count,
    std::size_t bytes,
    void* data,
    VkDeviceSize stride,
    VkQueryResultFlags flags)
{
    if (pool == observedQueryPool) {
        ++observedQueryReads;
        assert(count == 1u && !(flags & VK_QUERY_RESULT_WAIT_BIT));
        if (refuseObservedQueryResult) {
            refuseObservedQueryResult = false;
            return VK_NOT_READY;
        }
    } else if (refuseInternalQueryResult) {
        assert(!(flags & VK_QUERY_RESULT_WAIT_BIT));
        return VK_NOT_READY;
    }
    return vkGetQueryPoolResults(device, pool, first, count, bytes, data, stride, flags);
}

VKAPI_ATTR VkResult VKAPI_CALL queryCreate(VkDevice device,
    const VkQueryPoolCreateInfo* info,
    const VkAllocationCallbacks* allocation,
    VkQueryPool* pool)
{
    ++queryCreateCalls;
    if (failQueryCreation)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    const auto result = vkCreateQueryPool(device, info, allocation, pool);
    if (result == VK_SUCCESS)
        ++queryLivePools;
    if (result == VK_SUCCESS && loseQueryResetAfterCreation)
        missingQueryProcedure = "vkResetQueryPool";
    return result;
}

VKAPI_ATTR void VKAPI_CALL queryDestroy(
    VkDevice device, VkQueryPool pool, const VkAllocationCallbacks* allocation)
{
    assert(queryLivePools);
    --queryLivePools;
    vkDestroyQueryPool(device, pool, allocation);
}

VKAPI_ATTR void VKAPI_CALL queryReset(
    VkDevice device, VkQueryPool pool, std::uint32_t first, std::uint32_t count)
{
    ++queryResets;
    vkResetQueryPool(device, pool, first, count);
}

std::array<unsigned, 3u> clearNativeLive{};

VKAPI_ATTR VkResult VKAPI_CALL clearCreatePass(VkDevice device,
    const VkRenderPassCreateInfo* info,
    const VkAllocationCallbacks* allocation,
    VkRenderPass* pass)
{
    if (clearCreationFailure == 1u) {
        clearCreationFailure = 0u;
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    const auto result = vkCreateRenderPass(device, info, allocation, pass);
    if (result == VK_SUCCESS)
        ++clearNativeLive[0];
    return result;
}

VKAPI_ATTR void VKAPI_CALL clearDestroyPass(
    VkDevice device, VkRenderPass pass, const VkAllocationCallbacks* allocation)
{
    assert(clearNativeLive[0]);
    --clearNativeLive[0];
    vkDestroyRenderPass(device, pass, allocation);
}

VKAPI_ATTR VkResult VKAPI_CALL clearCreateView(VkDevice device,
    const VkImageViewCreateInfo* info,
    const VkAllocationCallbacks* allocation,
    VkImageView* view)
{
    if (clearCreationFailure == 2u) {
        clearCreationFailure = 0u;
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    const auto result = vkCreateImageView(device, info, allocation, view);
    if (result == VK_SUCCESS)
        ++clearNativeLive[1];
    return result;
}

VKAPI_ATTR void VKAPI_CALL clearDestroyView(
    VkDevice device, VkImageView view, const VkAllocationCallbacks* allocation)
{
    assert(clearNativeLive[1]);
    --clearNativeLive[1];
    vkDestroyImageView(device, view, allocation);
}

VKAPI_ATTR VkResult VKAPI_CALL clearCreateFrame(VkDevice device,
    const VkFramebufferCreateInfo* info,
    const VkAllocationCallbacks* allocation,
    VkFramebuffer* frame)
{
    if (clearCreationFailure == 3u) {
        clearCreationFailure = 0u;
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    const auto result = vkCreateFramebuffer(device, info, allocation, frame);
    if (result == VK_SUCCESS)
        ++clearNativeLive[2];
    return result;
}

VKAPI_ATTR void VKAPI_CALL clearDestroyFrame(
    VkDevice device, VkFramebuffer frame, const VkAllocationCallbacks* allocation)
{
    assert(clearNativeLive[2]);
    --clearNativeLive[2];
    vkDestroyFramebuffer(device, frame, allocation);
}

VKAPI_ATTR VkResult VKAPI_CALL queueSubmit(
    VkQueue queue, std::uint32_t count, const VkSubmitInfo* info, VkFence fence)
{
    ++queueSubmitCalls;
    if (failSubmit) {
        failSubmit = false;
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    return vkQueueSubmit(queue, count, info, fence);
}

VKAPI_ATTR VkResult VKAPI_CALL fenceStatus(VkDevice device, VkFence fence)
{
    ++fenceStatusCalls;
    return failFenceObservation ? VK_ERROR_OUT_OF_HOST_MEMORY : vkGetFenceStatus(device, fence);
}

class SubmissionRetirementCapacityFixture;
SubmissionRetirementCapacityFixture* submissionCapacityRace = nullptr;
void consumeLastRetirementSlot();

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolve(VkDevice device, const char* name)
{
    if (std::strcmp(name, "vkCmdBeginDebugUtilsLabelEXT") == 0
        || std::strcmp(name, "vkCmdEndDebugUtilsLabelEXT") == 0
        || std::strcmp(name, "vkCmdInsertDebugUtilsLabelEXT") == 0)
        ++debugLabelProcedureLookups;
    if (missingQueryProcedure && std::strcmp(name, missingQueryProcedure) == 0)
        return nullptr;
    if (std::strcmp(name, "vkCreateQueryPool") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(queryCreate);
    if (std::strcmp(name, "vkDestroyQueryPool") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(queryDestroy);
    if (std::strcmp(name, "vkResetQueryPool") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(queryReset);
    if (std::strcmp(name, "vkGetQueryPoolResults") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(queryResults);
    if (std::strcmp(name, "vkCmdWriteTimestamp") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(queryWrite);
    if (missingCommandEnd && std::strcmp(name, "vkEndCommandBuffer") == 0)
        return nullptr;
    if (missingClearRecord
        && (std::strcmp(name, "vkCmdBeginRenderPass") == 0
            || std::strcmp(name, "vkCmdEndRenderPass") == 0))
        return nullptr;
    if (std::strcmp(name, "vkQueueSubmit") == 0) {
        if (submissionCapacityRace)
            consumeLastRetirementSlot();
        return reinterpret_cast<PFN_vkVoidFunction>(queueSubmit);
    }
    if (std::strcmp(name, "vkGetFenceStatus") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(fenceStatus);
    if (std::strcmp(name, "vkCreateRenderPass") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(clearCreatePass);
    if (std::strcmp(name, "vkDestroyRenderPass") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(clearDestroyPass);
    if (std::strcmp(name, "vkCreateImageView") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(clearCreateView);
    if (std::strcmp(name, "vkDestroyImageView") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(clearDestroyView);
    if (std::strcmp(name, "vkCreateFramebuffer") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(clearCreateFrame);
    if (std::strcmp(name, "vkDestroyFramebuffer") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(clearDestroyFrame);
    return vkGetDeviceProcAddr(device, name);
}

class CopyFixture final {
public:
    explicit CopyFixture(std::shared_ptr<VulkanRhiRuntime> runtime)
        : runtime(std::move(runtime)),
          allocator({"retained transfer", 16u * 1024u * 1024u}, this->runtime),
          resources(this->runtime, &allocator),
          registry(core::makeShared<VulkanRhiResourceRegistry>()),
          quota(core::makeShared<core::CpuMemoryQuota>(kMetadataBytes))
    {
        diagnostic.reserve(512u);
        source = buffer(ResourceUsage::CopySrc, MemoryUsage::CpuToGpu, ResourceState::CopySrc);
        destination = buffer(ResourceUsage::CopyDst, MemoryUsage::GpuToCpu, ResourceState::CopyDst);
        for (std::uint32_t i = 0; i < expected.size(); ++i)
            expected[i] = 0x3ab90000u + i;
        const auto src = vulkanRhiNativePayload(*source);
        const auto dst = vulkanRhiNativePayload(*destination);
        assert(src->backendMappedData() && dst->backendMappedData());
        std::memcpy(src->backendMappedData(), expected.data(), sizeof(expected));
        std::memset(dst->backendMappedData(), 0, sizeof(expected));
        assert(src->backendFlushMappedWrite(0u, sizeof(expected), diagnostic));
        assert(dst->backendFlushMappedWrite(0u, sizeof(expected), diagnostic));
        copy = {source->id(), destination->id(), 0u, 0u, sizeof(expected)};
    }

    core::UniquePtr<GpuBuffer> buffer(
        ResourceUsage usage, MemoryUsage memory, ResourceState state, std::uint64_t bytes = 256u)
    {
        BufferDesc desc{"retained buffer", bytes, usage, memory, state};
        auto native = resources.createBuffer(desc);
        assert(native.success && native.payload && native.allocation.success);
        const GpuResourceId id{nextId++};
        auto object = createVulkanRhiBuffer(id, desc, native.allocation, native.payload, registry);
        assert(object);
        registry->registerResource(id, native.payload);
        registry->registerBufferObject(id, *object);
        return object;
    }

    core::UniquePtr<VulkanRhiRetainedTransferCommand> command(std::uint32_t owners = 2u)
    {
        auto result = VulkanRhiRetainedTransferCommand::prepare(
            resources, registry, storage(quota, owners), diagnostic);
        if (!result)
            std::fprintf(stderr, "retained preparation: %s\n", diagnostic.c_str());
        assert(result);
        return result;
    }

    void verify(bool copied = true)
    {
        const auto dst = vulkanRhiNativePayload(*destination);
        assert(dst->backendInvalidateMappedRead(0u, sizeof(expected), diagnostic));
        std::array<std::uint32_t, 64> actual{};
        std::memcpy(actual.data(), dst->backendMappedData(), sizeof(actual));
        if (copied)
            assert(actual == expected);
        else
            for (const auto value : actual)
                assert(!value);
    }

    std::shared_ptr<VulkanRhiRuntime> runtime;
    VulkanRhiAllocator allocator;
    VulkanRhiNativeResourceAllocator resources;
    std::shared_ptr<VulkanRhiResourceRegistry> registry;
    std::shared_ptr<core::CpuMemoryQuota> quota;
    core::pmr::String diagnostic;
    std::array<std::uint32_t, 64> expected{};
    core::UniquePtr<GpuBuffer> source, destination;
    BufferCopyDesc copy;
    std::uint64_t nextId = 1u;
};

// Real timeline-gated copies occupy the existing finite native retirement
// queue. The procedure resolver schedules one competing genuine submission
// between the main submit's initial capacity check and its later reservation.
class SubmissionRetirementCapacityFixture final {
public:
    explicit SubmissionRetirementCapacityFixture(const std::shared_ptr<VulkanRhiRuntime>& runtime)
        : copy_(runtime)
    {
        retireCompletedVulkanRhiSubmittedCommands(*runtime);
        gate_ = VulkanRhiTimelineSemaphorePayload::create(runtime, 0u, copy_.diagnostic);
        assert(gate_);
        const auto source = vulkanRhiNativePayload(*copy_.source);
        const auto destination = vulkanRhiNativePayload(*copy_.destination);
        const ResourceBarrierDesc barrier{copy_.destination->id(),
            {},
            ResourceState::CopyDst,
            ResourceState::CopyDst,
            PipelineStage::Transfer,
            PipelineStage::Transfer,
            ResourceAccess::TransferWrite,
            ResourceAccess::TransferWrite};
        for (auto& command : commands_) {
            command = createVulkanRhiNativeCommandPayload(copy_.resources, copy_.diagnostic);
            assert(command);
            command->retainResource(source);
            command->retainResource(destination);
            assert(command->recordBufferBarrier(*destination, barrier, copy_.diagnostic)
                && command->recordBufferCopy(*source, *destination, copy_.copy, copy_.diagnostic)
                && command->end(copy_.diagnostic));
        }
    }

    void arm()
    {
        assert(!submissionCapacityRace);
        for (std::size_t i = 0u; i + 1u < commands_.size(); ++i)
            assert(commands_[i]->submit({gate_, 1u}, copy_.diagnostic)
                && !commands_[i]->submittedFenceCompleted());
        submissionCapacityRace = this;
    }

    void consumeLastSlot()
    {
        assert(submissionCapacityRace == this && !competingSubmitInvoked_);
        submissionCapacityRace = nullptr; // Disarm before recursive resolution.
        assert(commands_.back()->submit({gate_, 1u}, copy_.diagnostic)
            && !commands_.back()->submittedFenceCompleted());
        competingSubmitInvoked_ = true;
    }

    void complete()
    {
        assert(competingSubmitInvoked_ && !submissionCapacityRace
            && gate_->hostSignal(1u) == VK_SUCCESS
            && vkQueueWaitIdle(copy_.runtime->graphicsQueue()) == VK_SUCCESS);
        retireCompletedVulkanRhiSubmittedCommands(*copy_.runtime);
        for (const auto& command : commands_)
            assert(command->submittedFenceCompleted());
        copy_.verify();
        commands_.fill(nullptr);
    }

private:
    CopyFixture copy_;
    std::shared_ptr<VulkanRhiTimelineSemaphorePayload> gate_;
    std::array<std::shared_ptr<VulkanRhiNativeCommandPayload>, kVulkanMaxAsyncSubmittedCommands>
        commands_;
    bool competingSubmitInvoked_ = false;
};

void consumeLastRetirementSlot()
{
    assert(submissionCapacityRace);
    submissionCapacityRace->consumeLastSlot();
}

void ordinaryFinishedStorage(const std::shared_ptr<VulkanRhiRuntime>& runtime)
{
    CopyFixture f(runtime);
    const rhi::semantics::NativeBackendCapabilityContract capabilities;
    auto makeEncoder = [&] {
        return createVulkanRhiCommandEncoder(
            f.resources, f.registry, nullptr, capabilities, false, true, {});
    };
    auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
    const auto pools = f.resources.stats().nativeCommandPoolCreateCount;
    bool refused = false;
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto rejected = makeEncoder();
        assert(!rejected);
    } catch (const std::bad_alloc&) {
        refused = true;
    }
    assert(refused && !quota->liveBytes()
        && f.resources.stats().nativeCommandPoolCreateCount == pools);

    // One original intrusive state control, observed across cancellation and
    // genuine accepted native submission. A new journal cannot pass rollback.
    f.source = f.buffer(ResourceUsage::StorageBuffer | ResourceUsage::CopySrc,
        MemoryUsage::CpuToGpu,
        ResourceState::ShaderWrite);
    const ResourceBarrierDesc barrier{f.source->id(),
        {},
        ResourceState::ShaderWrite,
        ResourceState::CopySrc,
        PipelineStage::ComputeShader,
        PipelineStage::Transfer,
        ResourceAccess::ShaderWrite,
        ResourceAccess::TransferRead};
    for (const unsigned terminal : {0u, 1u, 3u, 2u}) {
        quota->setLimitBytes(0u);
        core::UniquePtr<CommandEncoder> encoder;
        {
            core::CpuMemoryQuotaScope scope(quota);
            encoder = makeEncoder();
        }
        assert(encoder && vulkanRhiCommandPayload(*encoder));
        assert(encoder->transition(*f.source, barrier).success);
        assert(!vulkanRhiBufferStateCommitted(*f.source));
        if (!terminal) {
            encoder.reset();
            assert(vulkanRhiBufferStateCommitted(*f.source)
                && f.source->state() == ResourceState::ShaderWrite && !quota->liveBytes());
            continue;
        }
        const auto payload = vulkanRhiCommandPayload(*encoder);
        if (terminal == 3u) {
            missingCommandEnd = true;
            assert(!encoder->finish());
            missingCommandEnd = false;
            assert(encoder->lastValidation().diagnostics
                == "Vulkan RHI native command buffer missing vkEndCommandBuffer");
            assert(!encoder->finish());
            encoder.reset();
            assert(vulkanRhiBufferStateCommitted(*f.source)
                && f.source->state() == ResourceState::ShaderWrite);
            continue;
        }
        const auto nativeBefore = f.resources.stats();
        core::UniquePtr<CommandBuffer> commands;
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        quota->setLimitBytes(quota->liveBytes());
        {
            core::CpuMemoryQuotaScope scope(quota);
            commands = encoder->finish();
        }
        assert(commands && commands->commandCount() == 1u
            && vulkanRhiCommandPayload(*commands) == payload
            && core::defaultCpuAllocator().stats().allocationCount == allocations);
        const auto charge = core::inspectCpuAllocationContaining(commands.get());
        assert(charge.found && charge.quota == quota.get());
        assert(f.resources.stats().nativeCommandPoolCreateCount
                == nativeBefore.nativeCommandPoolCreateCount
            && f.resources.stats().nativeCommandBufferEndCount
                == nativeBefore.nativeCommandBufferEndCount + 1u);
        // Diagnostics may allocate; the successful finish above is the hot gate.
        assert(!encoder->finish() && !encoder->lastValidation().success);
        encoder.reset();
        assert(!vulkanRhiBufferStateCommitted(*f.source));
        if (terminal == 2u) {
            assert(submitVulkanRhiCommandBuffer(*commands).workAccepted);
            assert(vulkanRhiBufferStateCommitted(*f.source)
                && f.source->state() == ResourceState::CopySrc);
            assert(payload->waitSubmittedFenceUntil(
                std::chrono::steady_clock::now() + std::chrono::seconds(5), f.diagnostic));
            retireCompletedVulkanRhiSubmittedCommands(*runtime);
        }
        commands.reset();
        assert(vulkanRhiBufferStateCommitted(*f.source)
            && f.source->state()
                == (terminal == 2u ? ResourceState::CopySrc : ResourceState::ShaderWrite));
    }
    assert(!quota->liveBytes());
    std::puts(
        "ordinary native finish: cold quota refusal, zero hot allocation, original payload/journal, native failure unpublished, cancellation/accepted submission, one-shot PASS");
}

std::shared_ptr<const OriginalCommandStreamProgram> emptyPassProgram(bool bound = true)
{
    using K = CommandUsageKind;
    using Scalar = CommandScalarArguments;
    using Domain = OriginalCommandAdmissionBudget::CpuDomain;
    constexpr std::array<K, 7u> kinds{K::BeginComputePass,
        K::EndComputePass,
        K::BeginTransferPass,
        K::EndTransferPass,
        K::BeginComputePass,
        K::EndComputePass,
        K::Finish};
    const auto quota = [] {
        return core::makeShared<core::CpuMemoryQuota>(kMetadataBytes);
    };
    const auto sourceQuota = quota(), graphQuota = quota(), recordingQuota = quota();
    OriginalCommandAdmissionBudget budget;
    budget.contractVersion = 1u;
    budget.prototypeOptIn = true;
    budget.maxCommands = kinds.size();
    budget.maxGraphOwners = 16u;
    for (const auto domain : {Domain::Source, Domain::Graph, Domain::Recording})
        budget.maxCpuBytes[static_cast<std::size_t>(domain)] = kMetadataBytes;
    const char* diagnostic = nullptr;
    auto sources = OriginalCommandResourceSources::prepareOriginal(
        nullptr, 0u, budget, sourceQuota, diagnostic);
    assert(sources && !diagnostic);
    std::array<CommandUsage, kinds.size()> commands{};
    core::pmr::Vector<CommandVariableArguments::Input> fields;
    fields.reserve(kinds.size());
    CommandRecordingBudget recording{1u, true, kinds.size()};
    for (std::size_t i = 0u; i < kinds.size(); ++i) {
        commands[i] = {kinds[i], 0u, 0u};
        fields.push_back({*Scalar::capture(kinds[i], Scalar::None{})});
        assert(!recording.limits.appendArguments(fields[i]));
    }
    auto usage = OriginalCommandUsage::prepareOriginal(
        sources, commands.data(), commands.size(), nullptr, 0u, graphQuota, diagnostic);
    assert(usage && !diagnostic);
    auto arguments = CommandVariableArguments::prepareOriginal(fields.data(),
        fields.size(),
        {1u, true, kinds.size(), 0u, 0u, 0u, kMetadataBytes},
        graphQuota,
        diagnostic);
    assert(arguments && !diagnostic);
    auto scalars =
        OriginalCommandScalars::prepareOriginal(usage, arguments, graphQuota, diagnostic);
    assert(scalars && !diagnostic);
    auto inputs =
        OriginalCommandInputs::prepareOriginal(scalars, nullptr, 0u, graphQuota, diagnostic);
    assert(inputs && !diagnostic);
    OriginalCommandStreamBudget stream;
    stream.contractVersion = 1u;
    stream.prototypeOptIn = true;
    stream.maxCommands = kinds.size();
    stream.maxRetainedOwners = inputs->retainedOwnerCount() + 1u;
    stream.maxCpuMetadataBytes = kMetadataBytes;
    auto program = bound
        ? OriginalCommandStreamProgram::prepareRecordingBoundOriginal(
              inputs, stream, recording, recordingQuota, diagnostic)
        : OriginalCommandStreamProgram::prepareOriginal(inputs, stream, recordingQuota, diagnostic);
    assert(program && !diagnostic);
    return program;
}

void coldPassContract()
{
    {
        using K = CommandUsageKind;
        const char* id = nullptr;
        CommandRecordingBudget budget{1u, true, 1u};
        budget.limits.append(K::BufferBarrier, 2u);
        budget.limits.append(K::BufferMemoryBarrier, 3u);
        budget.limits.append(K::TextureBarrier, 7u);
        budget.limits.append(K::TextureMemoryBarrier, 11u);
        budget.limits.append(K::AliasBarrier, 13u);
        budget.limits.resourceReferences = 29u;
        const auto projected = VulkanRhiRecordingStorageLimits::project(budget, id);
        assert(projected && !id && projected->bufferBarriers == 5u && projected->imageBarriers == 7u
            && projected->memoryBarriers == 24u && projected->resourceReferences == 29u);
        budget.contractVersion = 0u;
        assert(!VulkanRhiRecordingStorageLimits::project(budget, id)
            && std::string_view(id) == "RHI-COMMAND-RECORDING-BUDGET-VERSION");
        budget.contractVersion = 1u;
        budget.prototypeOptIn = false;
        assert(!VulkanRhiRecordingStorageLimits::project(budget, id)
            && std::string_view(id) == "RHI-COMMAND-RECORDING-BUDGET-OPT-IN");
        budget.prototypeOptIn = true;
        budget.limits.commands[static_cast<std::size_t>(K::BufferBarrier)] = UINT64_MAX;
        assert(!VulkanRhiRecordingStorageLimits::project(budget, id)
            && std::string_view(id) == "RHI-VULKAN-COLD-BARRIER-STORAGE-OVERFLOW");
        budget.limits.commands[static_cast<std::size_t>(K::BufferBarrier)] = UINT32_MAX;
        assert(!VulkanRhiRecordingStorageLimits::project(budget, id)
            && std::string_view(id) == "RHI-VULKAN-COLD-RECORDING-STORAGE-RANGE");
        budget.limits.commands[static_cast<std::size_t>(K::BufferBarrier)] = 0u;
        budget.limits.commands[static_cast<std::size_t>(K::TextureMemoryBarrier)] = UINT64_MAX;
        assert(!VulkanRhiRecordingStorageLimits::project(budget, id)
            && std::string_view(id) == "RHI-VULKAN-COLD-BARRIER-STORAGE-OVERFLOW");
        budget.limits.commands[static_cast<std::size_t>(K::TextureMemoryBarrier)] = 0u;
        budget.limits.commands[static_cast<std::size_t>(K::TextureBarrier)] =
            std::uint64_t{UINT32_MAX} + 1u;
        assert(!VulkanRhiRecordingStorageLimits::project(budget, id)
            && std::string_view(id) == "RHI-VULKAN-COLD-RECORDING-STORAGE-RANGE");
    }
    auto original = emptyPassProgram();
    const auto baseline = original->originalMetadataQuota()->liveBytes();
    {
        const char* id = nullptr;
        auto storage = VulkanRhiRecordingStorageLimits::project(*original->recordingBudget(), id);
        assert(storage && !id);
        storage->originalProgram = original;
        assert(!storage->diagnostic());
        ++storage->resourceReferences;
        assert(std::string_view(storage->diagnostic()) == "RHI-VULKAN-COLD-PROGRAM-STORAGE-JOIN");
        --storage->resourceReferences;
        storage->originalQueries.emplace();
        assert(std::string_view(storage->diagnostic())
            == "RHI-VULKAN-COLD-PROGRAM-QUERY-JOIN-UNAVAILABLE");
        storage->originalQueries.reset();
        storage->originalProgram = emptyPassProgram(false);
        assert(std::string_view(storage->diagnostic())
            == "RHI-VULKAN-COLD-PASS-RECORDING-BUDGET-REQUIRED");
        auto foreignOwner = core::makeShared<unsigned>(0u);
        storage->originalProgram =
            std::shared_ptr<const OriginalCommandStreamProgram>(foreignOwner, original.get());
        assert(std::string_view(storage->diagnostic()) == "RHI-ORIGINAL-STREAM-OWNER");
    }
    VulkanRhiNativeResourceAllocator resources;
    const rhi::semantics::NativeBackendCapabilityContract capabilities;
    core::pmr::String diagnostic;
    assert(!createVulkanRhiCommandEncoderWithColdPassStorage(resources,
        core::makeShared<VulkanRhiResourceRegistry>(),
        nullptr,
        capabilities,
        original,
        diagnostic));
    assert(diagnostic == "Vulkan RHI native command buffer requires a native device"
        && original->originalMetadataQuota()->liveBytes() == baseline
        && !resources.stats().nativeCommandPoolCreateCount);
    VulkanRhiRuntimeDesc syntheticDesc;
    syntheticDesc.enableSyntheticRuntime = true;
    auto synthetic = core::makeShared<VulkanRhiRuntime>(syntheticDesc);
    assert(synthetic->valid() && synthetic->syntheticRuntime());
    VulkanRhiNativeResourceAllocator syntheticResources(synthetic);
    assert(!createVulkanRhiCommandEncoderWithColdPassStorage(syntheticResources,
        core::makeShared<VulkanRhiResourceRegistry>(),
        nullptr,
        capabilities,
        original,
        diagnostic));
    assert(diagnostic == "RHI-VULKAN-COLD-PASS-SYNTHETIC-RUNTIME"
        && original->originalMetadataQuota()->liveBytes() == baseline
        && !syntheticResources.stats().nativeCommandPoolCreateCount);
    const VulkanRhiRecordingStorageLimits limits;
    assert(!createVulkanRhiNativeCommandPayload(syntheticResources, diagnostic, {}, &limits));
    assert(diagnostic == "RHI-VULKAN-COLD-RECORDING-SYNTHETIC-RUNTIME"
        && !syntheticResources.stats().nativeCommandPoolCreateCount);
    const VulkanRhiRecordingStorageLimits invalid{std::size_t{UINT32_MAX} + 1u};
    assert(!createVulkanRhiNativeCommandPayload(resources, diagnostic, {}, &invalid)
        && diagnostic == "RHI-VULKAN-COLD-RECORDING-STORAGE-RANGE");
    std::puts("cold pass storage contract: real original stream; missing native device "
              "and synthetic runtime fail closed, original quota restored PASS");
}

void ordinaryColdPassStorage(const std::shared_ptr<VulkanRhiRuntime>& runtime)
{
    CopyFixture f(runtime);
    const rhi::semantics::NativeBackendCapabilityContract capabilities;
    auto original = emptyPassProgram();
    const auto quota = original->originalMetadataQuota();
    auto make = [&] {
        return createVulkanRhiCommandEncoderWithColdPassStorage(
            f.resources, f.registry, nullptr, capabilities, original, f.diagnostic);
    };
    const auto baseline = quota->liveBytes();
    assert(!createVulkanRhiCommandEncoderWithColdPassStorage(
        f.resources, f.registry, nullptr, capabilities, {}, f.diagnostic));
    assert(f.diagnostic == "RHI-ORIGINAL-STREAM-OWNER");
    auto unbounded = emptyPassProgram(false);
    assert(!createVulkanRhiCommandEncoderWithColdPassStorage(
        f.resources, f.registry, nullptr, capabilities, unbounded, f.diagnostic));
    assert(f.diagnostic == "RHI-VULKAN-COLD-PASS-RECORDING-BUDGET-REQUIRED");
    test::CpuQuotaFactoryFixture pressure(quota);
    const auto peak = pressure.measure(make);
    pressure.verify(peak, make);
    assert(quota->liveBytes() == baseline);
    const auto pools = f.resources.stats().nativeCommandPoolCreateCount;
    {
        core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - baseline);
        assert(pressure.charged() && !make());
        assert(f.diagnostic == "RHI-VULKAN-COLD-PASS-STORAGE-ALLOCATION");
        assert(f.resources.stats().nativeCommandPoolCreateCount == pools);
    }
    for (const bool accepted : {false, true}) {
        auto encoder = make();
        assert(encoder && f.diagnostic.empty());
        const auto payload = vulkanRhiCommandPayload(*encoder);
        core::UniquePtr<ComputePassEncoder> first, second;
        core::UniquePtr<TransferPassEncoder> transfer;
        core::UniquePtr<CommandBuffer> commands;
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        {
            core::CpuMemoryQuotaLease full(quota, quota->limitBytes() - quota->liveBytes());
            assert(full.charged());
            core::CpuMemoryQuotaScope scope(quota);
            first = encoder->beginComputePass({});
            assert(first && first->end().success);
            transfer = encoder->beginTransferPass({});
            assert(transfer && transfer->end().success);
            second = encoder->beginComputePass({});
            assert(second && second.get() != first.get() && second->end().success);
            commands = encoder->finish();
            assert(commands && commands->commandCount() == 6u
                && vulkanRhiCommandPayload(*commands) == payload);
        }
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        for (const void* object : {static_cast<const void*>(first.get()),
                 static_cast<const void*>(second.get()),
                 static_cast<const void*>(transfer.get())}) {
            const auto allocation = core::inspectCpuAllocationContaining(object);
            assert(allocation.found && allocation.quota == quota.get());
        }
        first.reset();
        second.reset();
        transfer.reset();
        encoder.reset();
        if (accepted) {
            assert(submitVulkanRhiCommandBuffer(*commands).workAccepted);
            assert(payload->waitSubmittedFenceUntil(
                std::chrono::steady_clock::now() + std::chrono::seconds(5), f.diagnostic));
            retireCompletedVulkanRhiSubmittedCommands(*runtime);
        }
        commands.reset();
    }
    for (const unsigned mode : {0u, 1u, 2u}) {
        auto encoder = make();
        std::uint32_t count = 0u;
        if (mode == 0u)
            for (unsigned i = 0u; i < 2u; ++i) {
                auto pass = encoder->beginComputePass({});
                assert(pass && pass->end().success);
                count += 2u;
            }
        if (mode == 1u) {
            auto pass = encoder->beginTransferPass({});
            assert(pass && pass->end().success);
            count += 2u;
        }
        const auto nativeCalls = f.resources.stats().nativeCommandBufferRecordCount;
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        {
            core::CpuMemoryQuotaLease full(quota, quota->limitBytes() - quota->liveBytes());
            assert(full.charged());
            core::CpuMemoryQuotaScope scope(quota);
            if (mode == 0u)
                assert(!encoder->beginComputePass({}));
            if (mode == 1u)
                assert(!encoder->beginTransferPass({}));
            if (mode == 2u)
                assert(!encoder->beginRenderPass({}));
            assert(!encoder->beginComputePass({}) && !encoder->beginTransferPass({})
                && !encoder->beginRenderPass({}) && !encoder->finish());
            encoder->pushDebugGroup("closed");
            encoder->popDebugGroup();
            encoder->insertDebugMarker("closed");
            assert(!encoder->transition(*f.source, ResourceState::CopySrc, ResourceState::CopySrc)
                    .success);
            assert(!encoder->memoryBarrier(*f.source, {}).success);
            assert(!encoder->aliasBarrier({}).success);
        }
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations
            && f.resources.stats().nativeCommandBufferRecordCount == nativeCalls);
        const char* expected = mode == 0u ? "RHI-VULKAN-COLD-COMPUTE-PASS-CAPACITY"
            : mode == 1u                  ? "RHI-VULKAN-COLD-TRANSFER-PASS-CAPACITY"
                                          : "RHI-VULKAN-COLD-RENDER-PASS-STORAGE-UNAVAILABLE";
        assert(encoder->lastValidation().diagnostics == expected
            && encoder->lastValidation().recordedCommandCount == count);
    }
    for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU})
        assert(original->executionDiagnostic(backend));
    std::printf("ordinary cold pass storage: genuine original 2-compute/1-transfer, "
                "independent-exhaustion exact/one-short CPU=%llu, hot-allocation0, "
                "distinct caller owners, real submit/cancellation, gates4-closed PASS\n",
        static_cast<unsigned long long>(peak));
}

// The original resource/argument closure is retained unchanged. The private
// adapter below tests storage admission only, never original-stream execution.
class OrdinaryRecordingStorageFixture final {
public:
    explicit OrdinaryRecordingStorageFixture(
        const std::shared_ptr<VulkanRhiRuntime>& runtime, bool extraBufferCapacity = false)
        : f(runtime)
    {
        using K = CommandUsageKind;
        using Scalar = CommandScalarArguments;
        f.source = f.buffer(ResourceUsage::StorageBuffer | ResourceUsage::CopySrc,
            MemoryUsage::CpuToGpu,
            ResourceState::ShaderWrite);
        const auto nativeSource = vulkanRhiNativePayload(*f.source);
        std::memcpy(nativeSource->backendMappedData(), f.expected.data(), sizeof(f.expected));
        assert(nativeSource->backendFlushMappedWrite(0u, sizeof(f.expected), f.diagnostic));
        f.copy.sourceBuffer = f.source->id();
        source = f.source.get();
        destination = f.destination.get();
        TextureDesc desc;
        desc.label = "ColdRecording.StorageImage";
        desc.extent = {2u, 2u, 1u};
        desc.format = TextureFormat::Rgba16Float;
        desc.usage = TextureUsage::CopySrc | TextureUsage::Sampled;
        const auto nativeTexture = f.resources.createTexture(desc);
        assert(nativeTexture.success && nativeTexture.payload);
        const GpuResourceId textureId{f.nextId++};
        f.registry->registerTexture(textureId, nativeTexture.payload);
        auto texture = createVulkanRhiTexture(
            textureId, desc, nativeTexture.allocation, nativeTexture.payload, f.registry);
        image = texture.get();
        assert(image);
        bufferEnter = {source->id(),
            {},
            ResourceState::ShaderWrite,
            ResourceState::CopySrc,
            PipelineStage::ComputeShader,
            PipelineStage::Transfer,
            ResourceAccess::ShaderWrite,
            ResourceAccess::TransferRead};
        bufferLeave = bufferEnter;
        std::swap(bufferLeave.before, bufferLeave.after);
        std::swap(bufferLeave.sourceStages, bufferLeave.destinationStages);
        std::swap(bufferLeave.sourceAccess, bufferLeave.destinationAccess);
        imageEnter = {image->id(),
            {},
            ResourceState::Undefined,
            ResourceState::CopySrc,
            PipelineStage::TopOfPipe,
            PipelineStage::Transfer,
            ResourceAccess::None,
            ResourceAccess::TransferRead};
        imageLeave = {image->id(),
            {},
            ResourceState::CopySrc,
            ResourceState::ShaderRead,
            PipelineStage::Transfer,
            PipelineStage::ComputeShader,
            ResourceAccess::TransferRead,
            ResourceAccess::ShaderRead};
        bufferMemory = {destination->id(),
            PipelineStage::Transfer,
            PipelineStage::Transfer,
            ResourceAccess::TransferWrite,
            ResourceAccess::TransferWrite};
        imageMemory = {image->id(),
            PipelineStage::Transfer,
            PipelineStage::Transfer,
            ResourceAccess::TransferRead,
            ResourceAccess::TransferRead};
        const auto quota = [] {
            return core::makeShared<core::CpuMemoryQuota>(kMetadataBytes);
        };
        const auto sourceQuota = quota(), graphQuota = quota(), recordingQuota = quota();
        const char* id = nullptr;
        const std::array<OriginalCommandResourceSources::Source, 3u> leases{
            OriginalCommandBufferLease::prepareOriginal(
                f.source, {1u, true, 2u, kMetadataBytes}, sourceQuota, id),
            OriginalCommandBufferLease::prepareOriginal(
                f.destination, {1u, true, 2u, kMetadataBytes}, sourceQuota, id),
            OriginalCommandTextureLease::prepareOriginal(
                texture, {1u, true, 2u, kMetadataBytes}, sourceQuota, id)};
        assert(!id);
        const std::array<CommandUsage, 12u> commands{{{K::BufferBarrier, 0u, 1u},
            {K::TextureBarrier, 1u, 1u},
            {K::BeginTransferPass, 2u, 0u},
            {K::BufferCopy, 2u, 2u},
            {K::EndTransferPass, 4u, 0u},
            {K::BufferMemoryBarrier, 4u, 1u},
            {K::TextureMemoryBarrier, 5u, 1u},
            {K::BufferBarrier, 6u, 1u},
            {K::TextureBarrier, 7u, 1u},
            {K::BeginComputePass, 8u, 0u},
            {K::EndComputePass, 8u, 0u},
            {K::Finish, 8u, 0u}}};
        const std::array<OriginalCommandUsage::Reference, 8u> references{
            {{0u}, {2u}, {0u}, {1u}, {1u}, {2u}, {0u}, {2u}}};
        OriginalCommandAdmissionBudget budget;
        budget.contractVersion = 1u;
        budget.prototypeOptIn = true;
        budget.maxCommands = commands.size();
        budget.maxReferences = references.size();
        budget.maxGraphOwners = 16u;
        budget.maxInputDescriptors = 7u;
        budget.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::Buffer)] = 2u;
        budget.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::Texture)] = 1u;
        using Domain = OriginalCommandAdmissionBudget::CpuDomain;
        for (const auto domain : {Domain::Source, Domain::Graph, Domain::Recording})
            budget.maxCpuBytes[static_cast<std::size_t>(domain)] = kMetadataBytes;
        auto sources = OriginalCommandResourceSources::prepareOriginal(
            leases.data(), leases.size(), budget, sourceQuota, id);
        assert(sources && !id);
        auto usage = OriginalCommandUsage::prepareOriginal(sources,
            commands.data(),
            commands.size(),
            references.data(),
            references.size(),
            graphQuota,
            id);
        assert(usage && !id);
        core::pmr::Vector<CommandVariableArguments::Input> fields;
        fields.reserve(commands.size());
        CommandRecordingBudget recording{1u, true, commands.size()};
        for (const auto& command : commands) {
            fields.push_back({*Scalar::capture(command.kind, Scalar::None{})});
            assert(!recording.limits.appendArguments(fields.back()));
        }
        recording.limits.resourceReferences = references.size();
        if (extraBufferCapacity)
            recording.limits.append(K::BufferBarrier, references.size());
        auto arguments = CommandVariableArguments::prepareOriginal(fields.data(),
            fields.size(),
            {1u, true, static_cast<std::uint32_t>(fields.size()), 0u, 0u, 0u, kMetadataBytes},
            graphQuota,
            id);
        assert(arguments && !id);
        auto scalars = OriginalCommandScalars::prepareOriginal(usage, arguments, graphQuota, id);
        assert(scalars && !id);
        const std::array<OriginalCommandInputs::OriginalDescriptor, 7u> descriptors{
            {{0u, *ResourceDependencyArguments::capture(K::BufferBarrier, bufferEnter)},
                {1u, *ResourceDependencyArguments::capture(K::TextureBarrier, imageEnter)},
                {3u, *TransferCopyArguments::capture(f.copy)},
                {5u, *ResourceDependencyArguments::capture(K::BufferMemoryBarrier, bufferMemory)},
                {6u, *ResourceDependencyArguments::capture(K::TextureMemoryBarrier, imageMemory)},
                {7u, *ResourceDependencyArguments::capture(K::BufferBarrier, bufferLeave)},
                {8u, *ResourceDependencyArguments::capture(K::TextureBarrier, imageLeave)}}};
        auto inputs = OriginalCommandInputs::prepareOriginal(
            scalars, descriptors.data(), descriptors.size(), graphQuota, id);
        assert(inputs && !id);
        OriginalCommandStreamBudget stream;
        stream.contractVersion = 1u;
        stream.prototypeOptIn = true;
        stream.maxCommands = commands.size();
        stream.maxDescriptors = descriptors.size();
        stream.maxReferences = references.size();
        stream.maxCopyElements = 1u;
        stream.maxRetainedOwners = inputs->retainedOwnerCount() + 1u;
        stream.maxCpuMetadataBytes = kMetadataBytes;
        original = OriginalCommandStreamProgram::prepareRecordingBoundOriginal(
            inputs, stream, recording, recordingQuota, id);
        if (!original)
            std::fprintf(stderr, "cold recording graph: %s\n", id);
        assert(original && !id);
    }

    core::UniquePtr<CommandEncoder> encoder()
    {
        return createVulkanRhiCommandEncoderWithColdPassStorage(
            f.resources, f.registry, nullptr, capabilities, original, f.diagnostic);
    }

    CopyFixture f;
    GpuBuffer* source = nullptr;
    GpuBuffer* destination = nullptr;
    GpuTexture* image = nullptr;
    ResourceBarrierDesc bufferEnter, bufferLeave, imageEnter, imageLeave;
    ResourceMemoryBarrierDesc bufferMemory, imageMemory;
    std::shared_ptr<const OriginalCommandStreamProgram> original;
    const rhi::semantics::NativeBackendCapabilityContract capabilities;
};

void ordinaryOriginalProgramCustody(const std::shared_ptr<VulkanRhiRuntime>& runtime)
{
    for (unsigned mode = 0u; mode < 4u; ++mode) {
        OrdinaryRecordingStorageFixture t(runtime);
        const auto& inputs = t.original->originalInputs();
        const auto& sources = inputs->originalScalars()->originalUsage()->originalSources();
        const std::array quotas{sources->originalMetadataQuota(),
            inputs->originalMetadataQuota(),
            t.original->originalMetadataQuota()};
        std::weak_ptr<const OriginalCommandStreamProgram> original = t.original;
        std::weak_ptr<const OriginalCommandInputs> inputOwner = inputs;
        std::weak_ptr<const OriginalCommandBufferLease> sourceOwner =
            std::get<std::shared_ptr<const OriginalCommandBufferLease>>(sources->sources()[0]);
        auto encoder = t.encoder();
        assert(encoder);
        auto native = vulkanRhiCommandPayload(*encoder);
        const auto destination = vulkanRhiNativePayload(*t.destination);
        core::UniquePtr<TransferPassEncoder> transfer;
        core::UniquePtr<ComputePassEncoder> compute;
        core::UniquePtr<CommandBuffer> finished;
        assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
            quotas, [&]() noexcept -> const char* {
                assert(encoder->transition(*t.source, t.bufferEnter).success);
                assert(encoder->transition(*t.image, t.imageEnter).success);
                transfer = encoder->beginTransferPass({});
                assert(transfer && transfer->copyBufferToBuffer(t.f.copy).success
                    && transfer->end().success);
                assert(encoder->memoryBarrier(*t.destination, t.bufferMemory).success);
                assert(encoder->memoryBarrier(*t.image, t.imageMemory).success);
                assert(encoder->transition(*t.source, t.bufferLeave).success);
                assert(encoder->transition(*t.image, t.imageLeave).success);
                compute = encoder->beginComputePass({});
                assert(compute && compute->end().success);
                if (mode) {
                    finished = encoder->finish();
                    assert(finished && vulkanRhiCommandPayload(*finished) == native);
                }
                return nullptr;
            }));
        transfer.reset();
        compute.reset();
        encoder.reset();
        t.original.reset();
        // None of these weak observations or raw pointers retains a second
        // graph. Only the ordinary native owner keeps the original alive now.
        assert(!original.expired() && !inputOwner.expired() && !sourceOwner.expired()
            && vulkanRhiBufferStateCommitted(*t.source) == (mode == 0u));
        if (mode < 2u) {
            // Observe rollback without keeping the Program/Inputs alive. The
            // sole source lease remains only for this post-destruction check.
            const auto source = sourceOwner.lock();
            assert(source);
            finished.reset();
            assert(vulkanRhiBufferStateCommitted(*source->originalObject())
                && source->originalObject()->state() == ResourceState::ShaderWrite);
            const auto submits = queueSubmitCalls;
            assert(!native->submit(t.f.diagnostic) && queueSubmitCalls == submits);
            native.reset();
            assert(original.expired() && inputOwner.expired()
                && vulkanRhiBufferStateCommitted(*source->originalObject())
                && source->originalObject()->state() == ResourceState::ShaderWrite);
        } else if (mode == 2u) {
            auto gate = VulkanRhiTimelineSemaphorePayload::create(runtime, 0u, t.f.diagnostic);
            assert(gate);
            assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
                quotas, [&]() noexcept -> const char* {
                    const auto result = submitVulkanRhiCommandBuffer(*finished, {gate, 1u});
                    assert(result.success && result.workAccepted);
                    return nullptr;
                }));
            assert(vulkanRhiBufferStateCommitted(*t.source));
            std::weak_ptr<VulkanRhiNativeCommandPayload> retained = native;
            finished.reset();
            native.reset();
            retireCompletedVulkanRhiSubmittedCommands(*runtime);
            assert(!original.expired() && !inputOwner.expired() && !sourceOwner.expired()
                && !retained.expired());
            assert(gate->hostSignal(1u) == VK_SUCCESS
                && vkQueueWaitIdle(runtime->graphicsQueue()) == VK_SUCCESS);
            assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
                quotas, [&]() noexcept -> const char* {
                    retireCompletedVulkanRhiSubmittedCommands(*runtime);
                    return nullptr;
                }));
            assert(original.expired() && inputOwner.expired() && sourceOwner.expired()
                && retained.expired());
            assert(
                destination->backendInvalidateMappedRead(0u, sizeof(t.f.expected), t.f.diagnostic)
                && !std::memcmp(
                    destination->backendMappedData(), t.f.expected.data(), sizeof(t.f.expected)));
        } else {
            failSubmit = true;
            const auto invocations = queueSubmitCalls;
            const auto result = submitVulkanRhiCommandBuffer(*finished);
            assert(!result.success && !result.workAccepted && queueSubmitCalls == invocations + 1u
                && result.diagnostics == "RHI-VULKAN-ORIGINAL-STREAM-SUBMIT-UNPROVEN"
                && !vulkanRhiBufferStateAvailable(*t.source, nullptr));
            const auto fences = fenceStatusCalls;
            assert(!native->submittedFenceCompleted() && fenceStatusCalls == fences);
            assert(!native->waitSubmittedFenceUntil(
                       std::chrono::steady_clock::now() + std::chrono::seconds(1), t.f.diagnostic)
                && t.f.diagnostic == "RHI-VULKAN-ORIGINAL-STREAM-QUEUE-QUARANTINED"
                && fenceStatusCalls == fences);
            std::weak_ptr<VulkanRhiNativeCommandPayload> retained = native;
            finished.reset();
            native.reset();
            retireCompletedVulkanRhiSubmittedCommands(*runtime);
            assert(!original.expired() && !sourceOwner.expired() && !retained.expired());
            assert(!waitIdleAndRetireVulkanRhiSubmittedCommands(runtime) && original.expired()
                && inputOwner.expired() && sourceOwner.expired() && retained.expired());
        }
        // These weak observers retain the quota-charged shared control blocks
        // after their objects expire. Drop the test observations before checking
        // total metadata reclamation; production graph ownership is already gone.
        original.reset();
        inputOwner.reset();
        sourceOwner.reset();
        for (const auto& quota : quotas)
            assert(!quota->liveBytes());
    }
    std::puts(
        "ordinary original Program custody: same native owner and sole journal, "
        "caller-drop cancellation/rollback and timeline-gated copy/readback, "
        "invoked-error exact-device idle quarantine, record/finish/submit/retire core-allocation0 PASS; "
        "original texture execution remains closed");
}

void ordinaryUnadmittedDebugLabels(const std::shared_ptr<VulkanRhiRuntime>& runtime)
{
    constexpr std::string_view expected = "RHI-VULKAN-COLD-DEBUG-LABEL-STORAGE-UNAVAILABLE";
    for (unsigned mode = 0u; mode < 6u; ++mode) {
        OrdinaryRecordingStorageFixture t(runtime);
        auto encoder = t.encoder();
        assert(encoder);
        auto native = vulkanRhiCommandPayload(*encoder);
        assert(native && encoder->transition(*t.source, t.bufferEnter).success);
        const auto state = t.source->state();
        const auto records = t.f.resources.stats().nativeCommandBufferRecordCount;
        const auto lookups = debugLabelProcedureLookups, submits = queueSubmitCalls;
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 1u> quotas{
            t.original->originalMetadataQuota()};
        assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
            quotas, [&]() noexcept -> const char* {
                // This view does not end at the underlying string's terminator.
                constexpr std::string_view label("Cloud.unadmitted", 5u);
                core::pmr::String diagnostic;
                if (mode == 0u)
                    encoder->pushDebugGroup(label);
                else if (mode == 1u)
                    encoder->popDebugGroup();
                else if (mode == 2u)
                    encoder->insertDebugMarker(label);
                else if (mode == 3u)
                    assert(!native->recordPushDebugGroup(label, diagnostic));
                else if (mode == 4u)
                    assert(!native->recordPopDebugGroup(diagnostic));
                else
                    assert(!native->recordInsertDebugMarker(label, diagnostic));
                assert(diagnostic.empty() && native->recordingStorageDiagnostic()
                    && expected == native->recordingStorageDiagnostic());
                // Every subsequent operation observes the same terminal latch.
                encoder->insertDebugMarker(label);
                encoder->pushDebugGroup(label);
                encoder->popDebugGroup();
                assert(!encoder->beginTransferPass({}) && !encoder->finish()
                    && native->recordingStorageDiagnostic() == expected
                    && debugLabelProcedureLookups == lookups && queueSubmitCalls == submits
                    && t.f.resources.stats().nativeCommandBufferRecordCount == records
                    && t.source->state() == state);
                return nullptr;
            }));
        // The public validation accessor returns an owned copy. Inspect it
        // after restoring PMR; the hot path above reads the native static latch.
        assert(encoder->lastValidation().diagnostics == expected);
        encoder.reset();
        native.reset();
        assert(vulkanRhiBufferStateCommitted(*t.source)
            && t.source->state() == ResourceState::ShaderWrite);
    }
    std::puts("bounded debug labels: ordinary/native push/pop/marker refused before API lookup, "
              "terminal finish and cancellation with exhausted quota allocation0 PASS");
}

template <bool Public = false>
void ordinaryTextureUpload(const std::shared_ptr<VulkanRhiRuntime>& runtime)
{
    using C = OriginalOrdinaryTextureUploadContract;
    using K = CommandUsageKind;
    using Scalar = CommandScalarArguments;
    using Content = common::OriginalTextureContentControl;
    for (unsigned shape = 0u; shape < 3u; ++shape)
        for (unsigned mode = 0u; mode < 4u; ++mode) {
            CopyFixture f(runtime);
            std::array<std::shared_ptr<core::CpuMemoryQuota>, 13u> q;
            for (auto& quota : q)
                quota = core::makeShared<core::CpuMemoryQuota>(kMetadataBytes);
            const char* id = nullptr;
            VulkanRhiDeviceDesc dd;
            dd.runtime = runtime;
            dd.capabilityContract.backend = Backend::Vulkan;
            dd.memoryBudgetBytes = kMetadataBytes;
            if constexpr (Public)
                dd.uploadStagingBudget =
                    semantics::UploadStagingBudgetV1{1u, 2048u, 2048u, 1u, 2048u, 256u};
            auto device = createVulkanRhiDevice(dd);
            assert(device
                && !device->prepareOriginalDeviceLifetime(
                    {1u, true, 1u, 2u, kMetadataBytes}, q[0]));
            assert(!device->prepareOriginalDeviceNativeResultLoss(
                {1u, true, 1u, 2u, kMetadataBytes}, q[11]));
            assert(!device->prepareOriginalResourceRegistration(
                {1u, true, 0u, 1u, Public ? 1u : 0u, Public ? 9u : 5u, kMetadataBytes}, q[1]));
            TextureDesc td;
            td.label = "Ordinary.Original.Upload.Target";
            td.extent = {2u, 2u, shape == 2u ? 2u : 1u};
            td.arrayLayerCount = shape == 1u ? 2u : 1u;
            td.mipLevelCount = 2u;
            td.format = TextureFormat::Rgba8Unorm;
            td.usage = TextureUsage::CopyDst | TextureUsage::Sampled | TextureUsage::CopySrc;
            const auto sourceBytes = shape == 0u ? 20u : shape == 1u ? 40u : 36u;
            const auto nativeBytes = device->queryOriginalContentTextureAllocation(td, id);
            assert(nativeBytes && !id);
            core::UniquePtr<GpuTexture> texture;
            {
                core::CpuMemoryQuotaScope scope(q[2]);
                texture = device->createOriginalContentTexture(td,
                    {3u, true, 1u, 1u, sourceBytes, 2u, kMetadataBytes, 0u, nativeBytes},
                    q[3],
                    id);
            }
            if (id)
                std::fprintf(stderr, "ordinary upload creation: %s\n", id);
            assert(texture && !id);
            auto* object = texture.get();
            const auto fresh = *object->observeOriginalContent();
            auto lease = OriginalCommandTextureLease::prepareOriginal(
                texture, {1u, true, 2u, kMetadataBytes}, q[4], id);
            assert(lease && !id);
            std::shared_ptr<const OriginalCommandTextureViewSource> sampled;
            if constexpr (Public) {
                TextureViewDesc viewDesc;
                viewDesc.format = td.format;
                viewDesc.dimension = shape == 2u ? TextureViewDimension::Texture3D
                    : shape == 1u                ? TextureViewDimension::Texture2DArray
                                                 : TextureViewDimension::Texture2D;
                viewDesc.usage = TextureUsage::Sampled;
                viewDesc.aspect = TextureAspect::Color;
                viewDesc.mipLevelCount = td.mipLevelCount;
                viewDesc.arrayLayerCount = td.arrayLayerCount;
                auto view = device->createTextureView(*object, viewDesc);
                assert(view);
                auto viewLease = OriginalCommandTextureViewLease::prepareOriginal(
                    view, {1u, true, 2u, kMetadataBytes}, q[4], id);
                sampled = OriginalCommandTextureViewSource::prepareOriginal(viewLease,
                    lease,
                    {1u,
                        true,
                        OriginalCommandTextureViewSource::completeRetainedOwnerCount,
                        kMetadataBytes},
                    q[4],
                    id);
                assert(sampled && !id);
            }
            std::array<std::uint8_t, 40u> authored{};
            for (std::size_t i = 0u; i < sourceBytes; ++i)
                authored[i] = static_cast<std::uint8_t>(i + 1u + shape * 40u);
            UploadTextureDesc upload;
            upload.destinationTexture = object->id();
            upload.extent = td.extent;
            upload.format = td.format;
            upload.subresources = defaultTextureSubresourceRange(td);
            upload.bytesPerRow = 8u;
            upload.rowsPerImage = 2u;
            upload.pixelData = authored.data();
            upload.pixelDataSize = sourceBytes;
            upload.mipLayouts = {
                {0u, td.extent, 8u, 2u, 0u}, {1u, {1u, 1u, 1u}, 4u, 1u, shape == 0u ? 16u : 32u}};
            auto input = TextureUploadInputSnapshot::prepareOriginal(upload,
                {1u, true, 2u, 0u, sourceBytes, kMetadataBytes, kMetadataBytes, kMetadataBytes},
                q[5],
                q[5],
                q[4],
                id);
            assert(input && !id);
            auto source = OriginalCommandTextureUploadSource::prepareOriginal(
                input, lease, {1u, true, 5u, kMetadataBytes}, q[4], id);
            assert(source && !id);
            auto packed = TextureUploadPacking::prepareOriginal(
                input, td, {1u, true, 2u, 1536u, kMetadataBytes, kMetadataBytes}, q[6], q[7], id);
            assert(packed && !id);
            auto packing = OriginalCommandTextureUploadPackingSource::prepareOriginal(source,
                packed,
                {1u,
                    true,
                    2u,
                    OriginalCommandTextureUploadPackingSource::retainedOwnerCount,
                    sourceBytes,
                    1536u,
                    kMetadataBytes},
                q[8],
                id);
            assert(packing && !id);
            ResourceBarrierDesc enter{object->id(),
                defaultTextureSubresourceRange(td),
                ResourceState::Undefined,
                ResourceState::CopyDst,
                PipelineStage::TopOfPipe,
                PipelineStage::Transfer,
                ResourceAccess::None,
                ResourceAccess::TransferWrite};
            ResourceBarrierDesc leave{object->id(),
                defaultTextureSubresourceRange(td),
                ResourceState::CopyDst,
                ResourceState::ShaderRead,
                PipelineStage::Transfer,
                PipelineStage::ComputeShader,
                ResourceAccess::TransferWrite,
                ResourceAccess::ShaderRead};
            const std::array<CommandUsage, 6u> commands{{{K::TextureBarrier, 0u, 1u},
                {K::BeginTransferPass, 1u, 0u},
                {K::TextureUpload, 1u, 2u},
                {K::EndTransferPass, 3u, 0u},
                {K::TextureBarrier, 3u, 1u},
                {K::Finish, 4u, 0u}}};
            OriginalCommandAdmissionBudget admission;
            admission.contractVersion = 1u;
            admission.prototypeOptIn = true;
            admission.maxCommands = commands.size();
            admission.maxReferences = 4u;
            admission.maxInputDescriptors = 3u;
            admission.maxGraphOwners = 12u;
            admission.maxMipMappings = 2u;
            admission.maxRetainedUploadBytes = sourceBytes;
            admission.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::Texture)] =
                1u;
            admission.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::Upload)] =
                1u;
            for (auto& bytes : admission.maxCpuBytes)
                bytes = kMetadataBytes;
            const std::array<OriginalCommandResourceSources::Source, 2u> owners{lease, source};
            auto sources = OriginalCommandResourceSources::prepareOriginal(
                owners.data(), owners.size(), admission, q[4], id);
            assert(sources && !id);
            const std::array<OriginalCommandUsage::Reference, 4u> references{
                {{0u}, {0u}, {1u}, {0u}}};
            auto usage = OriginalCommandUsage::prepareOriginal(sources,
                commands.data(),
                commands.size(),
                references.data(),
                references.size(),
                q[5],
                id);
            assert(usage && !id);
            core::pmr::Vector<CommandVariableArguments::Input> fields;
            CommandRecordingBudget recording{1u, true, commands.size()};
            for (const auto& command : commands) {
                fields.push_back({*Scalar::capture(command.kind, Scalar::None{})});
                assert(!recording.limits.appendArguments(fields.back()));
            }
            recording.limits.resourceReferences = references.size();
            auto arguments = CommandVariableArguments::prepareOriginal(fields.data(),
                fields.size(),
                {1u, true, static_cast<std::uint32_t>(fields.size()), 0u, 0u, 0u, kMetadataBytes},
                q[5],
                id);
            assert(arguments && !id);
            auto scalars = OriginalCommandScalars::prepareOriginal(usage, arguments, q[5], id);
            assert(scalars && !id);
            const std::array<OriginalCommandInputs::OriginalDescriptor, 3u> descriptors{
                {{0u, *ResourceDependencyArguments::capture(K::TextureBarrier, enter)},
                    {2u, *TextureUploadOriginalInputArguments::captureOriginal(input)},
                    {4u, *ResourceDependencyArguments::capture(K::TextureBarrier, leave)}}};
            auto inputs = OriginalCommandInputs::prepareOriginal(
                scalars, descriptors.data(), descriptors.size(), q[5], id);
            assert(inputs && !id);
            OriginalCommandStreamBudget stream;
            stream.contractVersion = 1u;
            stream.prototypeOptIn = true;
            stream.maxCommands = commands.size();
            stream.maxDescriptors = descriptors.size();
            stream.maxReferences = references.size();
            stream.maxTextureUploadMips = 2u;
            stream.maxTextureUploadPixelBytes = sourceBytes;
            stream.maxRetainedOwners = inputs->retainedOwnerCount() + 1u;
            stream.maxCpuMetadataBytes = kMetadataBytes;
            auto program = OriginalCommandStreamProgram::prepareRecordingBoundOriginal(
                inputs, stream, recording, q[9], id);
            if (id)
                std::fprintf(stderr, "ordinary upload graph: %s\n", id);
            assert(program && !id);
            const C::Budget budget{1u, true, 1u, 2u, packed->packedBytes().size()};
            C::Binding binding{2u, packing};
            assert(!C::inspect(program, budget, &binding, 1u));
            auto shortBudget = budget;
            --shortBudget.maxMipMappings;
            assert(C::inspect(program, shortBudget, &binding, 1u));
            shortBudget = budget;
            --shortBudget.maxPackedBytes;
            assert(C::inspect(program, shortBudget, &binding, 1u));
            auto wrong = binding;
            wrong.commandIndex = 1u;
            assert(C::inspect(program, budget, &wrong, 1u));
            wrong.packing.reset();
            core::UniquePtr<VulkanRhiStagingUploadPool> pool;
            OriginalTextureUploadDeviceExtension* extension = nullptr;
            OriginalTextureUploadPreparationRequest publicRequest{budget,
                {1u,
                    true,
                    1u,
                    OriginalTextureUploadPublication::requiredOwners(*program, 1u),
                    kMetadataBytes}};
            std::shared_ptr<const OriginalTextureUploadPublication> publication;
            bool retainPublication = false;
            const semantics::UploadStagingColdStorageBudget storageBudget{
                1u, true, {1u, 2048u, 2048u, 1u, 2048u, 256u}, 1u, kMetadataBytes};
            if constexpr (Public) {
                extension = OriginalTextureUploadDeviceExtension::query(*device, publicRequest, id);
                assert(extension && !id);
                {
                    auto refused = extension->prepareUploads(
                        program, publicRequest, &binding, 1u, q[12], f.diagnostic);
                    assert(!refused.encoder && refused.publications.empty());
                    assert(f.diagnostic == "RHI-ORDINARY-TEXTURE-UPLOAD-COLD-POOL");
                }
                auto badStorage = storageBudget;
                --badStorage.capacity.maxRequestBytes;
                assert(extension->prepareStorage(badStorage, q[10]));
                {
                    core::CpuMemoryQuotaLease pressure(q[10], q[10]->limitBytes());
                    assert(pressure.charged() && extension->prepareStorage(storageBudget, q[10]));
                }
                assert(inspectVulkanRhiNativeResourceStats(*device).nativeUploadStagingPoolPageCount
                    == 0u);
                assert(!extension->prepareStorage(storageBudget, q[10]));
                assert(extension->prepareStorage(storageBudget, q[10]));
                const char* threadDiagnostic = nullptr;
                std::thread wrongThread([&] {
                    threadDiagnostic = extension->prepareStorage(storageBudget, q[10]);
                });
                wrongThread.join();
                assert(std::string_view(threadDiagnostic) == "RHI-TEXTURE-UPLOAD-DEVICE-THREAD");
                auto badPublic = publicRequest;
                --badPublic.publications.maxRetainedOwners;
                auto refused = extension->prepareUploads(
                    program, badPublic, &binding, 1u, q[12], f.diagnostic);
                assert(!refused.encoder && refused.publications.empty());
                refused = extension->prepareUploads(
                    program, publicRequest, &binding, 1u, q[9], f.diagnostic);
                assert(!refused.encoder && refused.publications.empty());
            } else {
                pool = VulkanRhiStagingUploadPool::prepareCold(
                    {"Ordinary.Texture.ColdPool", 2048u, 1u, 256u, 2048u},
                    f.allocator,
                    storageBudget,
                    q[10],
                    id);
                assert(pool && !id);
            }
            const auto sliceCount = [&] {
                if constexpr (Public)
                    return inspectVulkanRhiNativeResourceStats(*device)
                        .nativeUploadStagingLiveSliceCount;
                else
                    return pool->stats().liveSliceCount;
            };
            const auto copyCount = [&] {
                if constexpr (Public)
                    return inspectVulkanRhiNativeResourceStats(*device)
                        .nativeBufferToImageCopyRecordCount;
                else
                    return f.resources.stats().nativeBufferToImageCopyRecordCount;
            };
            const auto capabilities = semantics::nativeBackendCapabilityContract(Backend::Vulkan);
            const auto factory = [&] {
                if constexpr (Public) {
                    auto prepared = extension->prepareUploads(
                        program, publicRequest, &binding, 1u, q[12], f.diagnostic);
                    assert(prepared.encoder || prepared.publications.empty());
                    if (prepared.encoder) {
                        assert(prepared.publications.size() == 1u);
                        assert(OriginalTextureUploadPublication::validateOriginal(
                            prepared.publications[0]));
                        if (retainPublication)
                            publication = prepared.publications[0];
                    }
                    return std::move(prepared.encoder);
                }
                return createVulkanRhiCommandEncoderWithColdTextureUploads(f.resources,
                    f.registry,
                    pool.get(),
                    capabilities,
                    program,
                    *device,
                    budget,
                    &binding,
                    1u,
                    f.diagnostic);
            };
            if (shape == 0u && mode == 0u) {
                test::CpuQuotaFactoryFixture pressure(q[9]);
                const auto peak = pressure.measure(factory);
                pressure.verify(peak, factory);
                if constexpr (Public) {
                    test::CpuQuotaFactoryFixture publicationPressure(q[12]);
                    const auto publicationPeak = publicationPressure.measure(factory);
                    publicationPressure.verify(publicationPeak, factory);
                }
                assert(!sliceCount());
                for (unsigned negative = 0u; negative < 4u; ++negative) {
                    auto candidate = factory();
                    assert(candidate);
                    auto observer = vulkanRhiCommandPayload(*candidate);
                    const auto calls = copyCount();
                    auto invalid = upload;
                    --invalid.pixelDataSize;
                    core::UniquePtr<TransferPassEncoder> pass;
                    assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
                        q, [&]() noexcept -> const char* {
                            if (negative)
                                assert(candidate->transition(*object, enter).success);
                            pass = candidate->beginTransferPass({});
                            assert(pass);
                            if (negative == 0u)
                                assert(!pass->uploadTexture(upload).success); // wrong ordinal
                            else if (negative == 1u)
                                assert(!pass->uploadTexture(invalid).success);
                            else if (negative == 2u) {
                                assert(pass->uploadTexture(upload).success);
                                assert(!pass->uploadTexture(upload).success);
                            } else {
                                assert(pass->end().success);
                                assert(!candidate->finish()); // missing authored upload
                            }
                            return nullptr;
                        }));
                    assert(copyCount() == calls + (negative == 2u ? 2u : 0u));
                    pass.reset();
                    candidate.reset();
                    assert(vulkanRhiTextureStateCommitted(*object)
                        && object->state() == ResourceState::Undefined
                        && object->observeOriginalContent()->state()
                            == Content::State::Uninitialized
                        && !sliceCount());
                    assert(!observer->submit(f.diagnostic));
                }
            }
            retainPublication = true;
            auto encoder = factory();
            if (!encoder)
                std::fprintf(stderr, "ordinary upload encoder: %s\n", f.diagnostic.c_str());
            assert(encoder && sliceCount() == 1u);
            if constexpr (Public) {
                assert(publication && publication->originalProgram().get() == program.get()
                    && core::SharedOwnerProjection::sameOwner(
                        publication->originalProgram(), program)
                    && publication->originalPacking().get() == packing.get()
                    && core::SharedOwnerProjection::sameOwner(
                        publication->originalPacking(), packing)
                    && publication->commandIndex() == binding.commandIndex);
            }
            auto native = vulkanRhiCommandPayload(*encoder);
            if (shape == 0u && mode == 0u) {
                core::MutationJournal foreign;
                assert(!transitionVulkanRhiTexture(*object, enter, &foreign, native.get()).success
                    && !transitionVulkanRhiTexture(*object, enter, nullptr, native.get()).success
                    && !transitionVulkanRhiTexture(*object, enter).success
                    && vulkanRhiTextureStateCommitted(*object)
                    && object->state() == ResourceState::Undefined);
            }
            const auto copies = copyCount();
            core::UniquePtr<TransferPassEncoder> transfer;
            core::UniquePtr<CommandBuffer> finished;
            assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
                q, [&]() noexcept -> const char* {
                    assert(encoder->transition(*object, enter).success);
                    transfer = encoder->beginTransferPass({});
                    assert(transfer && transfer->uploadTexture(upload).success
                        && transfer->end().success);
                    assert(encoder->transition(*object, leave).success);
                    if (mode)
                        finished = encoder->finish();
                    assert(!mode || finished);
                    return nullptr;
                }));
            assert(copyCount() == copies + 2u && !object->observeOriginalContent()
                && !vulkanRhiTextureStateCommitted(*object));
            transfer.reset();
            encoder.reset();
            std::weak_ptr<const OriginalCommandStreamProgram> retainedProgram = program;
            std::weak_ptr<const OriginalCommandTextureUploadPackingSource> retainedPacking =
                packing;
            program.reset();
            packing.reset();
            packed.reset();
            binding.packing.reset();
            assert(!retainedProgram.expired() && !retainedPacking.expired());
            if (mode < 2u) {
                finished.reset();
                assert(object->state() == ResourceState::Undefined
                    && vulkanRhiTextureStateCommitted(*object)
                    && object->observeOriginalContent()->state() == Content::State::Uninitialized
                    && object->validateOriginalContent(fresh)); // rollback changes revision
                assert(sliceCount() == 0u);
            } else if (mode == 2u) {
                if constexpr (Public) {
                    bool refused = false;
                    const auto calls = queueSubmitCalls;
                    std::thread submitter([&] {
                        core::pmr::String diagnostic;
                        refused = !native->submit(diagnostic)
                            && diagnostic == "RHI-TEXTURE-UPLOAD-PUBLICATION-THREAD";
                    });
                    submitter.join();
                    assert(refused && queueSubmitCalls == calls);
                }
                assert(!acceptRecordedCommandOwnership(*finished));
                auto gate = VulkanRhiTimelineSemaphorePayload::create(runtime, 0u, f.diagnostic);
                assert(gate);
                const auto submissions = queueSubmitCalls;
                missingQueryProcedure = "vkQueueSubmit";
                const auto refused = submitVulkanRhiCommandBuffer(*finished, {gate, 1u});
                missingQueryProcedure = nullptr;
                assert(!refused.success && !refused.workAccepted && queueSubmitCalls == submissions
                    && !native->submittedFenceCompleted()
                    && !acceptRecordedCommandOwnership(*finished)
                    && !object->observeOriginalContent() && !vulkanRhiTextureStateCommitted(*object)
                    && sliceCount() == 1u);
                assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    q, [&]() noexcept -> const char* {
                        const auto submitted = submitVulkanRhiCommandBuffer(*finished, {gate, 1u});
                        assert(submitted.success && submitted.workAccepted);
                        assert(acceptRecordedCommandOwnership(*finished));
                        return nullptr;
                    }));
                assert(!object->observeOriginalContent() && !vulkanRhiTextureStateCommitted(*object)
                    && sliceCount() == 1u && !native->submittedFenceCompleted());
                finished.reset();
                assert(gate->hostSignal(1u) == VK_SUCCESS
                    && vkQueueWaitIdle(runtime->graphicsQueue()) == VK_SUCCESS);
                assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    q, [&]() noexcept -> const char* {
                        assert(native->submittedFenceCompleted());
                        retireCompletedVulkanRhiSubmittedCommands(*runtime);
                        return nullptr;
                    }));
                const auto completed = object->observeOriginalContent();
                assert(completed && completed->state() == Content::State::Completed
                    && completed->contentGeneration() && vulkanRhiTextureStateCommitted(*object)
                    && object->state() == ResourceState::ShaderRead && sliceCount() == 0u);
            } else {
                failSubmit = true;
                const auto submitted = submitVulkanRhiCommandBuffer(*finished);
                assert(!submitted.success && !submitted.workAccepted);
                finished.reset();
                assert(!object->observeOriginalContent() && !vulkanRhiTextureStateCommitted(*object)
                    && sliceCount() == 1u && !native->submittedFenceCompleted());
                assert(!waitIdleAndRetireVulkanRhiSubmittedCommands(runtime));
                assert(!object->observeOriginalContent() && sliceCount() == 0u);
            }
            std::printf(
                "ordinary texture upload shape=%u mode=%u: two genuine mip copies, same journal, completion publication PASS\n",
                shape,
                mode);
            if constexpr (Public) {
                assert((OriginalTextureUploadPublication::validateOriginal(publication) == nullptr)
                    == (mode == 2u));
                assert((publication->validateView(sampled) == nullptr) == (mode == 2u));
                if (mode == 2u) {
                    std::shared_ptr<const OriginalTextureUploadPublication> forged(
                        publication.get(), [](const OriginalTextureUploadPublication*) {});
                    assert(
                        std::string_view(OriginalTextureUploadPublication::validateOriginal(forged))
                        == "RHI-TEXTURE-UPLOAD-PUBLICATION-OWNER");
                    const char* wrongThreadResult = nullptr;
                    std::thread observer([&] {
                        wrongThreadResult = publication->validate();
                    });
                    observer.join();
                    assert(std::string_view(wrongThreadResult)
                        == "RHI-TEXTURE-UPLOAD-PUBLICATION-THREAD");
                    assert(publication->validateView({}));
                }
            }
            native.reset();
            publication.reset();
            assert(retainedProgram.expired() && retainedPacking.expired());
        }
}

template <bool Registered = false>
void originalTexturePoolInitialization(const std::shared_ptr<VulkanRhiRuntime>& runtime)
{
    using C = OriginalTexturePoolInitializationContract;
    using PackingSource = OriginalCommandTextureUploadPackingSource;
    CopyFixture f(runtime);
    std::array<std::shared_ptr<core::CpuMemoryQuota>, Registered ? 10u : 6u> quotas;
    for (auto& quota : quotas)
        quota = core::makeShared<core::CpuMemoryQuota>(kMetadataBytes);
    core::UniquePtr<GpuDevice> device, peer;
    core::UniquePtr<tests::VulkanHeadlessRuntime> foreign;
    if constexpr (Registered) {
        VulkanRhiDeviceDesc dd;
        dd.runtime = runtime;
        dd.capabilityContract.backend = Backend::Vulkan;
        dd.memoryBudgetBytes = kMetadataBytes;
        device = createVulkanRhiDevice(dd);
        peer = createVulkanRhiDevice(dd);
        const auto prepare = [&](GpuDevice& target, std::size_t index) {
            assert(!target.prepareOriginalDeviceLifetime(
                {1u, true, 1u, 2u, kMetadataBytes}, quotas[index]));
            const OriginalResourceRegistrationBudget b{1u, true, 0u, 1u, 0u, 5u, kMetadataBytes};
            assert(!target.prepareOriginalResourceRegistration(b, quotas[index + 1u]));
        };
        assert(device && peer);
        prepare(*device, 6u);
        prepare(*peer, 8u);
        foreign = core::makeUnique<tests::VulkanHeadlessRuntime>(false, resolve);
        assert(foreign->runtime() && foreign->runtime()->valid()
            && foreign->runtime()->logicalDevice() != runtime->logicalDevice());
    }
    const char* diagnostic = nullptr;
    TextureDesc physical;
    physical.extent = {2u, 2u, 1u};
    physical.format = TextureFormat::Rgba8Unorm;
    physical.usage = TextureUsage::CopyDst;
    physical.mipLevelCount = 2u;
    auto texture = [&] {
        if constexpr (Registered) {
            return device->createTexture(physical);
        } else {
            auto native = f.resources.createTexture(physical);
            assert(native.success && native.payload);
            const GpuResourceId id{f.nextId++};
            f.registry->registerTexture(id, native.payload);
            return createVulkanRhiTexture(
                id, physical, native.allocation, native.payload, f.registry);
        }
    }();
    assert(texture);
    const auto textureId = texture->id();
    if constexpr (Registered)
        assert(!device->validateOriginalResourceRegistration(*texture));
    auto lease = OriginalCommandTextureLease::prepareOriginal(
        texture, {1u, true, 2u, kMetadataBytes}, quotas[0], diagnostic);
    assert(lease);
    std::array<std::uint8_t, 20u> authored;
    for (std::size_t i = 0u; i < authored.size(); ++i)
        authored[i] = static_cast<std::uint8_t>(i + 1u);
    UploadTextureDesc upload;
    upload.destinationTexture = textureId;
    upload.extent = physical.extent;
    upload.format = physical.format;
    upload.bytesPerRow = 8u;
    upload.rowsPerImage = 2u;
    upload.subresources.mipLevelCount = 2u;
    upload.pixelData = authored.data();
    upload.pixelDataSize = authored.size();
    upload.backendOwnedStaging = true;
    upload.stagingIntent = StagingIntent::Upload;
    upload.mipLayouts = {{0u, {2u, 2u, 1u}, 8u, 2u, 0u}, {1u, {1u, 1u, 1u}, 4u, 1u, 16u}};
    auto input = TextureUploadInputSnapshot::prepareOriginal(upload,
        {1u, true, 2u, 0u, 20u, kMetadataBytes, kMetadataBytes, kMetadataBytes},
        quotas[1],
        quotas[1],
        quotas[0],
        diagnostic);
    assert(input);
    auto source = OriginalCommandTextureUploadSource::prepareOriginal(
        input, lease, {1u, true, 5u, kMetadataBytes}, quotas[0], diagnostic);
    assert(source);
    auto inputs =
        test::OriginalCommandTextureUploadInputsFixture::prepareForSource(source, diagnostic);
    assert(inputs);
    const OriginalCommandStreamBudget stream{1u,
        true,
        5u,
        2u,
        4u,
        0u,
        0u,
        4u,
        0u,
        0u,
        0u,
        0u,
        40u,
        0u,
        inputs->retainedOwnerCount() + 1u,
        kMetadataBytes};
    const TextureUploadPackingBudget packingBudget{
        1u, true, 2u, 768u, kMetadataBytes, kMetadataBytes};
    auto packed = TextureUploadPacking::prepareOriginal(
        input, physical, packingBudget, quotas[2], quotas[3], diagnostic);
    assert(packed && packed->packedBytes().size() == 768u);
    const PackingSource::Budget bindingBudget{
        1u, true, 2u, PackingSource::retainedOwnerCount, 20u, 768u, kMetadataBytes};
    auto packing =
        PackingSource::prepareOriginal(source, packed, bindingBudget, quotas[4], diagnostic);
    assert(packing);
    std::array<std::uint8_t, 768u> expected{};
    std::memcpy(expected.data(), authored.data(), 8u);
    std::memcpy(expected.data() + 256u, authored.data() + 8u, 8u);
    std::memcpy(expected.data() + 512u, authored.data() + 16u, 4u);
    assert(std::equal(expected.begin(), expected.end(), packed->packedBytes().begin()));
    authored.fill(0u); // Only frozen original bytes may reach the native page.
    const C::Budget initialization{Registered ? 2u : 1u, true, 768u, 2u};
    assert(!C::inspect(inputs, stream, 1u, packing, initialization));
    auto twinSource = OriginalCommandTextureUploadSource::prepareOriginal(
        input, lease, {1u, true, 5u, kMetadataBytes}, quotas[0], diagnostic);
    auto twinPacking =
        PackingSource::prepareOriginal(twinSource, packed, bindingBudget, quotas[4], diagnostic);
    assert(twinSource && twinPacking);
    std::shared_ptr<const OriginalCommandInputs> unregisteredInputs;
    std::shared_ptr<const PackingSource> unregisteredPacking;
    if constexpr (Registered) {
        const auto* original = lease->originalObject();
        auto alias =
            createVulkanRhiTexture(textureId, physical, {}, vulkanRhiNativePayload(*original));
        auto aliasLease = OriginalCommandTextureLease::prepareOriginal(
            alias, {1u, true, 2u, kMetadataBytes}, quotas[0], diagnostic);
        auto aliasSource = OriginalCommandTextureUploadSource::prepareOriginal(
            input, aliasLease, {1u, true, 5u, kMetadataBytes}, quotas[0], diagnostic);
        assert(aliasSource);
        unregisteredInputs = test::OriginalCommandTextureUploadInputsFixture::prepareForSource(
            aliasSource, diagnostic);
        unregisteredPacking = PackingSource::prepareOriginal(
            aliasSource, packed, bindingBudget, quotas[4], diagnostic);
        assert(unregisteredInputs && unregisteredPacking);
    }
    const VulkanRhiStagingUploadPoolDesc poolDesc{"Original.Texture.Pool", 2048u, 1u, 256u, 2048u};
    const semantics::UploadStagingColdStorageBudget storageBudget{
        1u, true, {1u, 2048u, 2048u, 1u, 2048u, 256u}, 2u, kMetadataBytes};
    auto pool = VulkanRhiStagingUploadPool::prepareCold(
        poolDesc, f.allocator, storageBudget, quotas[5], diagnostic);
    assert(pool && !diagnostic);
    core::UniquePtr<VulkanRhiAllocator> foreignAllocator;
    core::UniquePtr<VulkanRhiStagingUploadPool> foreignPool;
    if constexpr (Registered) {
        foreignAllocator = core::makeUnique<VulkanRhiAllocator>(
            VulkanRhiAllocatorDesc{"Foreign.Texture.Pool", kMetadataBytes}, foreign->runtime());
        foreignPool = VulkanRhiStagingUploadPool::prepareCold(
            poolDesc, *foreignAllocator, storageBudget, quotas[5], diagnostic);
        assert(foreignPool && !diagnostic);
    }
    VulkanRhiStagingUploadPool unprepared(poolDesc, f.allocator);
    auto journal = core::makeUnique<RecordedStagingUploadJournal>();
    RecordedStagingUploadJournal zero, unbounded;
    {
        core::CpuMemoryQuotaScope scope(quotas[5]);
        assert(!journal->prepareBounded(2u) && !zero.prepareBounded(0u));
    }
    VulkanRhiStagingUploadSlice first, second;
    assert(
        !test::CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept -> const char* {
            core::CpuMemoryQuotaScope scope(quotas[5]);
            const auto reject = [&](const auto& originalInputs,
                                    const auto& b,
                                    std::size_t ordinal,
                                    const auto& binding,
                                    RecordedStagingUploadJournal& target,
                                    const char* expectedId) {
                const auto before = pool->stats();
                const auto size = target.size();
                const auto result = pool->initializeOriginalTexture(
                    originalInputs, stream, ordinal, binding, b, target, 91u, device.get());
                assert(!result.success && result.storageDiagnostic
                    && std::string_view(result.storageDiagnostic) == expectedId
                    && result.diagnostics.empty() && result.diagnosticId.empty()
                    && pool->stats().liveSliceCount == before.liveSliceCount
                    && target.size() == size);
            };
            auto bad = initialization;
            bad.prototypeOptIn = false;
            reject(inputs, bad, 1u, packing, *journal, "RHI-TEXTURE-POOL-INITIALIZATION-OPT-IN");
            bad = initialization;
            --bad.maxPackedBytes;
            reject(inputs, bad, 1u, packing, *journal, "RHI-TEXTURE-POOL-INITIALIZATION-CAPACITY");
            bad = initialization;
            --bad.maxMipMappings;
            reject(inputs, bad, 1u, packing, *journal, "RHI-TEXTURE-POOL-INITIALIZATION-CAPACITY");
            reject(inputs,
                initialization,
                0u,
                packing,
                *journal,
                "RHI-TEXTURE-POOL-INITIALIZATION-ORDINAL");
            reject(inputs,
                initialization,
                SIZE_MAX,
                packing,
                *journal,
                "RHI-TEXTURE-POOL-INITIALIZATION-ORDINAL");
            reject(inputs,
                initialization,
                1u,
                twinPacking,
                *journal,
                "RHI-TEXTURE-POOL-INITIALIZATION-SOURCE-JOIN");
            auto forged = std::shared_ptr<const PackingSource>(quotas[4], packing.get());
            reject(inputs,
                initialization,
                1u,
                forged,
                *journal,
                "RHI-CANONICAL-TEXTURE-PACKING-OWNER");
            auto forgedInputs =
                std::shared_ptr<const OriginalCommandInputs>(quotas[1], inputs.get());
            assert(C::inspect(forgedInputs, stream, 1u, packing, initialization));
            if constexpr (Registered) {
                reject(unregisteredInputs,
                    initialization,
                    1u,
                    unregisteredPacking,
                    *journal,
                    "RHI-ORIGINAL-REGISTRATION-EXPIRED");
                const auto missing = pool->initializeOriginalTexture(
                    inputs, stream, 1u, packing, initialization, *journal, 91u);
                assert(!missing.success
                    && std::string_view(missing.storageDiagnostic)
                        == "RHI-TEXTURE-POOL-INITIALIZATION-DEVICE");
                const auto unrelated = pool->initializeOriginalTexture(
                    inputs, stream, 1u, packing, initialization, *journal, 91u, peer.get());
                assert(!unrelated.success
                    && std::string_view(unrelated.storageDiagnostic)
                        == "RHI-ORIGINAL-REGISTRATION-FOREIGN-DEVICE");
                assert(std::string_view(VulkanOriginalTextureUploadStaging::inspectDestination(
                           packing, *device, *foreign->runtime()))
                    == "RHI-VULKAN-TEXTURE-STAGING-DEVICE");
                const auto foreignPage = foreignPool->initializeOriginalTexture(
                    inputs, stream, 1u, packing, initialization, *journal, 91u, device.get());
                assert(!foreignPage.success
                    && std::string_view(foreignPage.storageDiagnostic)
                        == "RHI-VULKAN-TEXTURE-STAGING-DEVICE"
                    && !foreignPool->stats().liveSliceCount && journal->empty());
                auto legacy = initialization;
                legacy.contractVersion = 1u;
                reject(inputs,
                    legacy,
                    1u,
                    packing,
                    *journal,
                    "RHI-TEXTURE-POOL-INITIALIZATION-LEGACY-DEVICE");
                assert(journal->empty() && !pool->stats().liveSliceCount);
            }
            const auto unpreparedResult = unprepared.initializeOriginalTexture(
                inputs, stream, 1u, packing, initialization, *journal, 91u, device.get());
            assert(!unpreparedResult.success
                && std::string_view(unpreparedResult.storageDiagnostic)
                    == "RHI-TEXTURE-POOL-INITIALIZATION-COLD-STORAGE"
                && journal->empty());
            reject(inputs, initialization, 1u, packing, zero, "RHI-STAGING-JOURNAL-ENTRY-CAPACITY");
            reject(inputs,
                initialization,
                1u,
                packing,
                unbounded,
                "RHI-TEXTURE-POOL-INITIALIZATION-BOUNDED-JOURNAL");
            first = pool->initializeOriginalTexture(
                inputs, stream, 1u, packing, initialization, *journal, 91u, device.get());
            second = pool->initializeOriginalTexture(
                inputs, stream, 2u, packing, initialization, *journal, 91u, device.get());
            assert(first.success && second.success && first.buffer == second.buffer
                && !first.offsetBytes && second.offsetBytes == 768u && journal->size() == 2u
                && pool->stats().liveSliceCount == 2u);
            for (const auto* slice : {&first, &second})
                assert(!std::memcmp(
                    static_cast<const std::uint8_t*>(slice->mappedData) + slice->offsetBytes,
                    expected.data(),
                    expected.size()));
            assert(std::string_view(first.writeFlusher->flushMappedWriteBounded(2048u, 1u))
                == "RHI-VULKAN-MAPPED-WRITE-RANGE");
            assert(std::string_view(first.writeFlusher->flushMappedWriteBounded(UINT64_MAX, 1u))
                == "RHI-VULKAN-MAPPED-WRITE-RANGE");
            assert(std::string_view(first.writeFlusher->flushMappedWriteBounded(0u, 0u))
                == "RHI-VULKAN-MAPPED-WRITE-RANGE");
            reject(inputs,
                initialization,
                1u,
                packing,
                *journal,
                "RHI-STAGING-JOURNAL-ENTRY-CAPACITY");
            return nullptr;
        }));
    f.destination = f.buffer(
        ResourceUsage::CopyDst, MemoryUsage::GpuToCpu, ResourceState::CopyDst, expected.size());
    const auto destination = vulkanRhiNativePayload(*f.destination);
    auto command = createVulkanRhiNativeCommandPayload(f.resources, f.diagnostic);
    assert(command);
    command->retainResource(destination);
    assert(command->retainRecordedStagingUploads(journal, f.diagnostic) && !journal
        && command->recordUploadStagingBufferCopy(
            second.buffer, second.offsetBytes, *destination, expected.size(), f.diagnostic)
        && command->end(f.diagnostic));
    auto gate = VulkanRhiTimelineSemaphorePayload::create(runtime, 0u, f.diagnostic);
    assert(gate && command->submit({gate, 1u}, f.diagnostic) && !command->submittedFenceCompleted()
        && pool->stats().liveSliceCount == 2u);
    assert(gate->hostSignal(1u) == VK_SUCCESS
        && vkQueueWaitIdle(runtime->graphicsQueue()) == VK_SUCCESS);
    assert(
        !test::CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept -> const char* {
            assert(command->submittedFenceCompleted() && !pool->stats().liveSliceCount
                && pool->stats().retiredSliceCount == 2u);
            return nullptr;
        }));
    assert(destination->backendInvalidateMappedRead(0u, expected.size(), f.diagnostic)
        && !std::memcmp(destination->backendMappedData(), expected.data(), expected.size()));
    command.reset();
    retireCompletedVulkanRhiSubmittedCommands(*runtime);
    std::puts(
        "original texture pool initialization: exact Inputs/Source ordinal, two mips/768 packed bytes, "
        "two same-page offsets, frozen caller input, exhausted quotas/null PMR allocation0, "
        "genuine flush and timeline-gated GPU copy/readback PASS; original execution remains closed");
    if constexpr (Registered)
        std::puts("registered texture pool initialization /2: exact logical device and genuine "
                  "native device join, same-runtime peer/foreign-runtime/missing device refusal "
                  "before mutation, exhausted10 quotas PASS; texture recording remains closed");
}

void coldStagingPool(const std::shared_ptr<VulkanRhiRuntime>& runtime)
{
    CopyFixture f(runtime);
    const VulkanRhiStagingUploadPoolDesc desc{"Cold.Staging.RealPages", 512u, 2u, 256u, 512u};
    semantics::UploadStagingColdStorageBudget budget{
        1u, true, {1u, 512u, 512u, 2u, 1024u, 256u}, 1u, kMetadataBytes};
    auto quota = core::makeShared<core::CpuMemoryQuota>(kMetadataBytes);
    const char* diagnostic = nullptr;
    const auto baseline = f.allocator.memoryStats();
    const auto make = [&] {
        return VulkanRhiStagingUploadPool::prepareCold(
            desc, f.allocator, budget, quota, diagnostic);
    };
    const auto clean = [&] {
        const auto actual = f.allocator.memoryStats();
        assert(actual.allocatedBytes == baseline.allocatedBytes
            && actual.liveResourceCount == baseline.liveResourceCount && !quota->liveBytes());
    };
    const auto reject = [&](const char* id) {
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!make() && diagnostic && std::string_view(diagnostic) == id);
        clean();
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
    };
    budget.contractVersion = 0u;
    reject("RHI-STAGING-COLD-STORAGE-VERSION");
    budget.contractVersion = 1u;
    budget.prototypeOptIn = false;
    reject("RHI-STAGING-COLD-STORAGE-OPT-IN");
    budget.prototypeOptIn = true;
    --budget.capacity.maxRequestBytes;
    reject("RHI-STAGING-COLD-STORAGE-POLICY-MISMATCH");
    ++budget.capacity.maxRequestBytes;
    --budget.maxCpuMetadataBytes;
    reject("RHI-STAGING-COLD-STORAGE-CPU-QUOTA");
    ++budget.maxCpuMetadataBytes;
    assert(!VulkanRhiStagingUploadPool::prepareCold(desc, f.allocator, budget, {}, diagnostic)
        && std::string_view(diagnostic) == "RHI-STAGING-COLD-STORAGE-CPU-QUOTA");
    auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
    reject("RHI-STAGING-COLD-STORAGE-CPU-RESOURCE");
    std::pmr::set_default_resource(previous);
    {
        VulkanRhiAllocator synthetic({"Cold.synthetic", 4096u});
        assert(!VulkanRhiStagingUploadPool::prepareCold(desc, synthetic, budget, quota, diagnostic)
            && std::string_view(diagnostic) == core::kFrameRetiredPageSliceDiagnosticPageCreate
            && !synthetic.memoryStats().allocatedBytes && !quota->liveBytes());
    }
    test::CpuQuotaFactoryFixture pressure(quota);
    const auto peak = pressure.measure(make);
    clean();
    pressure.verify(peak, make);
    clean();
    {
        core::CpuMemoryQuotaLease full(quota, kMetadataBytes);
        assert(full.charged() && !make()
            && std::string_view(diagnostic) == "RHI-STAGING-COLD-STORAGE-CPU-ALLOCATION"
            && f.allocator.memoryStats().allocatedBytes == baseline.allocatedBytes);
    }
    // Derive native byte pressure from a genuine one-page allocation rather
    // than assuming device-specific buffer memory requirements.
    auto singleDesc = desc;
    singleDesc.maxPageCount = 1u;
    auto singleBudget = budget;
    singleBudget.capacity.maxPageCount = 1u;
    singleBudget.capacity.totalCapacityBytes = singleBudget.capacity.pageSizeBytes;
    std::uint64_t pageBytes = 0u;
    {
        auto single = VulkanRhiStagingUploadPool::prepareCold(
            singleDesc, f.allocator, singleBudget, quota, diagnostic);
        assert(single && !diagnostic);
        pageBytes = f.allocator.memoryStats().allocatedBytes - baseline.allocatedBytes;
        assert(pageBytes);
    }
    clean();
    {
        VulkanRhiAllocator limited({"Cold.partial", pageBytes}, runtime);
        assert(!VulkanRhiStagingUploadPool::prepareCold(desc, limited, budget, quota, diagnostic)
            && std::string_view(diagnostic) == core::kFrameRetiredPageSliceDiagnosticPageCreate
            && !limited.memoryStats().allocatedBytes && !limited.memoryStats().liveResourceCount
            && !quota->liveBytes());
    }
    auto pool = make();
    assert(pool && !diagnostic && pool->coldStoragePrepared()
        && pool->stats().allocatedPageCount == 2u && pool->stats().allocatedBytes == 1024u
        && f.allocator.memoryStats().allocatedBytes == baseline.allocatedBytes + 2u * pageBytes
        && f.allocator.memoryStats().liveResourceCount == baseline.liveResourceCount + 2u);
    auto journal = core::makeUnique<RecordedStagingUploadJournal>();
    RecordedStagingUploadJournal unrelated;
    {
        core::CpuMemoryQuotaScope scope(quota);
        assert(!journal->prepareBounded(1u) && !unrelated.prepareBounded(1u));
    }
    const auto reservation = [](const auto& value) {
        return RecordedStagingUploadSlice{
            value.pageIndex, value.offsetBytes, value.sizeBytes, value.frameSerial};
    };
    VulkanRhiStagingUploadSlice first, second;
    const VulkanRhiStagingUploadRequest a{"Cold.A", 256u, 256u, 81u};
    const VulkanRhiStagingUploadRequest b{"Cold.B", 256u, 256u, 82u};
    const auto hot = [&](auto&& action) {
        assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota}, [&]() noexcept -> const char* {
                core::CpuMemoryQuotaScope scope(quota);
                action();
                return nullptr;
            }));
    };
    hot([&] {
        first = pool->allocate(a);
        second = pool->allocate(b);
        assert(first.success && second.success && first.buffer && second.buffer
            && first.buffer != second.buffer && first.mappedData && first.writeFlusher
            && first.payload && second.payload && first.pageIndex == 0u && second.pageIndex == 1u);
        const auto full = pool->allocate(a);
        assert(!full.success && full.diagnostics.empty() && full.diagnosticId.empty()
            && std::string_view(full.storageDiagnostic) == "RHI-STAGING-COLD-SLICE-CAPACITY");
        assert(pool->enlist(*journal, reservation(first))
            && pool->enlist(unrelated, reservation(second)));
        assert(!pool->enlist(*journal, reservation(second)) && journal->size() == 1u);
        assert(pool->stats().liveSliceCount == 2u && !pool->stats().retiredSliceCount);
    });
    std::memcpy(static_cast<std::byte*>(first.mappedData) + first.offsetBytes,
        f.expected.data(),
        sizeof(f.expected));
    assert(
        first.writeFlusher->flushMappedWrite(first.offsetBytes, sizeof(f.expected), f.diagnostic));
    auto command = createVulkanRhiNativeCommandPayload(f.resources, f.diagnostic);
    const auto destination = vulkanRhiNativePayload(*f.destination);
    assert(command);
    command->retainResource(destination);
    assert(command->retainRecordedStagingUploads(journal, f.diagnostic) && !journal);
    assert(command->recordUploadStagingBufferCopy(
               first.buffer, first.offsetBytes, *destination, sizeof(f.expected), f.diagnostic)
        && command->end(f.diagnostic));
    auto gate = VulkanRhiTimelineSemaphorePayload::create(runtime, 0u, f.diagnostic);
    assert(
        gate && command->submit({gate, 1u}, f.diagnostic) && !command->submittedFenceCompleted());
    hot([&] {
        assert(!pool->allocate(a).success && pool->stats().liveSliceCount == 2u
            && !pool->stats().retiredSliceCount && !pool->completedSubmissionEpoch());
    });
    assert(gate->hostSignal(1u) == VK_SUCCESS
        && vkQueueWaitIdle(runtime->graphicsQueue()) == VK_SUCCESS);
    hot([&] {
        assert(command->submittedFenceCompleted() && pool->stats().liveSliceCount == 1u
            && pool->stats().retiredSliceCount == 1u && pool->completedSubmissionEpoch() == 1u
            && unrelated.size() == 1u);
        auto reused = pool->allocate(a);
        assert(reused.success && reused.buffer == first.buffer && !reused.offsetBytes
            && reused.pageIndex == first.pageIndex && pool->cancel(reused));
        assert(unrelated.rollback() && !pool->stats().liveSliceCount
            && pool->stats().retiredSliceCount == 1u);
    });
    f.verify();
    command.reset();
    retireCompletedVulkanRhiSubmittedCommands(*runtime);
    first = {};
    second = {};
    pool.reset();
    // Journal vectors retain quota charges until their own destruction.
    assert(f.allocator.memoryStats().allocatedBytes == baseline.allocatedBytes
        && f.allocator.memoryStats().liveResourceCount == baseline.liveResourceCount);
    std::printf(
        "cold staging pool: CPU peak=%llu native page charge=%llu; exact/short/zero, "
        "partial failure cleanup, hot allocation0, two pages/one slice each, "
        "timeline-gated 256-byte GPU copy/readback and exact reuse PASS; original gates closed\n",
        static_cast<unsigned long long>(peak),
        static_cast<unsigned long long>(pageBytes));
}

void coldStagingJournal(const std::shared_ptr<VulkanRhiRuntime>& runtime)
{
    // Genuine backend page/ledger custody, without claiming native submission
    // or initialized-upload proof from this metadata-only ownership check.
    VulkanRhiAllocator allocator({"Journal.native", 64u * 1024u, true, false}, runtime);
    auto pool = core::makeUnique<VulkanRhiStagingUploadPool>(
        VulkanRhiStagingUploadPoolDesc{"Journal.native", 1024u, 1u, 256u}, allocator);
    auto first = pool->allocate({"first", 256u, 256u, 1u});
    auto second = pool->allocate({"second", 256u, 256u, 2u});
    auto third = pool->allocate({"third", 256u, 256u, 3u});
    assert(first.success && second.success && third.success && first.buffer && first.mappedData);
    const auto slice = [](const auto& value) {
        return RecordedStagingUploadSlice{
            value.pageIndex, value.offsetBytes, value.sizeBytes, value.frameSerial};
    };
    auto quota = core::makeShared<core::CpuMemoryQuota>(kMetadataBytes);
    RecordedStagingUploadJournal journal;
    {
        core::CpuMemoryQuotaScope scope(quota);
        assert(!journal.prepareBounded(2u));
    }
    assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
        std::array{quota}, [&]() noexcept -> const char* {
            core::CpuMemoryQuotaScope scope(quota);
            assert(pool->enlist(journal, slice(first)) && pool->enlist(journal, slice(second)));
            assert(!pool->enlist(journal, slice(third)) && journal.size() == 2u
                && pool->stats().liveSliceCount == 3u && !pool->stats().retiredSliceCount);
            assert(pool->cancel(third) && journal.rollbackLast()
                && pool->stats().liveSliceCount == 1u && !pool->stats().retiredSliceCount);
            return nullptr;
        }));
    std::weak_ptr<void> page = first.payload;
    first.payload.reset();
    first.writeFlusher.reset();
    second.payload.reset();
    second.writeFlusher.reset();
    third.payload.reset();
    third.writeFlusher.reset();
    pool.reset();
    assert(!page.expired());
    assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
        std::array{quota}, [&]() noexcept -> const char* {
            core::CpuMemoryQuotaScope scope(quota);
            assert(journal.retireCompleted() && page.expired() && journal.empty());
            return nullptr;
        }));

    for (unsigned mode = 0u; mode < 4u; ++mode) {
        OrdinaryRecordingStorageFixture t(runtime);
        auto encoder = t.encoder();
        assert(encoder && encoder->transition(*t.source, t.bufferEnter).success);
        auto pass = encoder->beginTransferPass({});
        assert(pass);
        auto native = vulkanRhiCommandPayload(*encoder);
        const auto records = t.f.resources.stats().nativeCommandBufferRecordCount;
        const auto submits = queueSubmitCalls;
        const std::string_view expected = mode < 2u
            ? "RHI-STAGING-JOURNAL-ENTRY-CAPACITY"
            : VulkanRhiRecordingStorageLimits::unsupportedStagingPoolDiagnostic();
        assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{t.original->originalMetadataQuota()}, [&]() noexcept -> const char* {
                core::pmr::String diagnostic;
                if (mode < 2u)
                    assert(!(mode ? pass->uploadTexture({}) : pass->uploadBuffer({})).success);
                else if (mode == 2u)
                    assert(!native->recordUploadStagingTextureCopy(
                        VK_NULL_HANDLE, 0u, *t.f.registry->payload(t.image->id()), {}, diagnostic));
                else
                    assert(!native->recordUploadStagingBufferCopy(VK_NULL_HANDLE,
                        0u,
                        *t.f.registry->payload(t.destination->id()),
                        256u,
                        diagnostic));
                assert(diagnostic.empty());
                assert(native->recordingStorageDiagnostic()
                    && std::string_view(native->recordingStorageDiagnostic()) == expected);
                // A raw native call cannot overwrite an earlier capacity latch.
                assert(
                    !native->recordUploadStagingTextureCopy(
                        VK_NULL_HANDLE, 0u, *t.f.registry->payload(t.image->id()), {}, diagnostic)
                    && diagnostic.empty()
                    && std::string_view(native->recordingStorageDiagnostic()) == expected);
                assert(!native->recordUploadStagingBufferCopy(VK_NULL_HANDLE,
                           0u,
                           *t.f.registry->payload(t.destination->id()),
                           256u,
                           diagnostic)
                    && diagnostic.empty()
                    && std::string_view(native->recordingStorageDiagnostic()) == expected);
                assert(!pass->end().success && !encoder->finish()
                    && t.f.resources.stats().nativeCommandBufferRecordCount == records
                    && queueSubmitCalls == submits);
                return nullptr;
            }));
        assert(encoder->lastValidation().diagnostics == expected);
        pass.reset();
        encoder.reset();
        native.reset();
        assert(vulkanRhiBufferStateCommitted(*t.source)
            && t.source->state() == ResourceState::ShaderWrite);
    }
    std::puts("cold staging journal: genuine page custody, exact capacity, owner-drop completion; "
              "ordinary unadmitted buffer/texture uploads and native raw staging entry "
              "terminally refused allocation0 PASS");
}

void ordinaryRecordingStorage(const std::shared_ptr<VulkanRhiRuntime>& runtime)
{
    std::uint64_t measured = 0u;
    for (unsigned mode = 0u; mode < 6u; ++mode) {
        OrdinaryRecordingStorageFixture t(runtime, mode == 5u);
        const auto quota = t.original->originalMetadataQuota();
        auto make = [&] {
            return t.encoder();
        };
        if (!mode) {
            test::CpuQuotaFactoryFixture pressure(quota);
            measured = pressure.measure(make);
            pressure.verify(measured, make);
        }
        auto encoder = make();
        assert(encoder);
        auto native = vulkanRhiCommandPayload(*encoder);
        core::UniquePtr<TransferPassEncoder> transfer;
        core::UniquePtr<ComputePassEncoder> compute;
        core::UniquePtr<CommandBuffer> commands;
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        {
            core::CpuMemoryQuotaLease full(quota, quota->limitBytes() - quota->liveBytes());
            assert(full.charged());
            core::CpuMemoryQuotaScope scope(quota);
            assert(encoder->transition(*t.source, t.bufferEnter).success);
            assert(encoder->transition(*t.image, t.imageEnter).success);
            transfer = encoder->beginTransferPass({}); // Flush the first arrays.
            assert(transfer && transfer->copyBufferToBuffer(t.f.copy).success
                && transfer->end().success);
            assert(encoder->memoryBarrier(*t.destination, t.bufferMemory).success);
            assert(encoder->memoryBarrier(*t.image, t.imageMemory).success);
            assert(encoder->transition(*t.source, t.bufferLeave).success);
            assert(encoder->transition(*t.image, t.imageLeave).success);
            compute = encoder->beginComputePass({}); // Flush again, no new allowance.
            assert(compute && compute->end().success);
            if (mode < 2u) {
                commands = encoder->finish();
                assert(commands && vulkanRhiCommandPayload(*commands) == native);
            } else {
                const auto records = t.f.resources.stats().nativeCommandBufferRecordCount;
                if (mode == 2u || mode == 5u)
                    assert(!encoder->transition(*t.source, t.bufferEnter).success);
                if (mode == 3u)
                    assert(!encoder->transition(*t.image, t.imageLeave).success);
                if (mode == 4u)
                    assert(!encoder->memoryBarrier(*t.image, t.imageMemory).success);
                assert(!encoder->finish() && !transfer->end().success && !compute->end().success);
                assert(t.f.resources.stats().nativeCommandBufferRecordCount == records);
                assert(t.source->state() == ResourceState::ShaderWrite
                    && t.image->subresourceState({}) == ResourceState::ShaderRead);
            }
        }
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        if (mode >= 2u) {
            const char* expected = mode == 2u ? "RHI-VULKAN-COLD-BUFFER-BARRIER-CAPACITY"
                : mode == 3u                  ? "RHI-VULKAN-COLD-IMAGE-BARRIER-CAPACITY"
                : mode == 4u                  ? "RHI-VULKAN-COLD-MEMORY-BARRIER-CAPACITY"
                                              : "RHI-VULKAN-COLD-RESOURCE-REFERENCE-CAPACITY";
            assert(encoder->lastValidation().diagnostics == expected);
        }
        transfer.reset();
        compute.reset();
        encoder.reset();
        if (mode == 1u) {
            assert(submitVulkanRhiCommandBuffer(*commands).workAccepted);
            assert(native->waitSubmittedFenceUntil(
                std::chrono::steady_clock::now() + std::chrono::seconds(5), t.f.diagnostic));
            retireCompletedVulkanRhiSubmittedCommands(*runtime);
            const auto destination = vulkanRhiNativePayload(*t.destination);
            assert(
                destination->backendInvalidateMappedRead(0u, sizeof(t.f.expected), t.f.diagnostic));
            assert(std::memcmp(
                       destination->backendMappedData(), t.f.expected.data(), sizeof(t.f.expected))
                == 0);
        }
        commands.reset();
        assert(vulkanRhiBufferStateCommitted(*t.source) && vulkanRhiTextureStateCommitted(*t.image)
            && t.source->state() == ResourceState::ShaderWrite
            && t.image->subresourceState({})
                == (mode == 1u ? ResourceState::ShaderRead : ResourceState::Undefined));
    }
    // One slot remains, but a copy needs two. Preflight must retain neither
    // original payload and must happen before any native copy or staging work.
    {
        OrdinaryRecordingStorageFixture t(runtime, true);
        auto encoder = t.encoder();
        auto quota = t.original->originalMetadataQuota();
        auto same = t.bufferEnter;
        assert(encoder->transition(*t.source, same).success);
        same.before = same.after;
        same.sourceStages = same.destinationStages;
        same.sourceAccess = same.destinationAccess;
        for (unsigned i = 0u; i < 6u; ++i)
            assert(encoder->transition(*t.source, same).success);
        auto transfer = encoder->beginTransferPass({});
        assert(transfer);
        const auto source = vulkanRhiNativePayload(*t.source);
        const auto destination = vulkanRhiNativePayload(*t.destination);
        const auto sourceOwners = source.use_count(), destinationOwners = destination.use_count();
        const auto records = t.f.resources.stats().nativeBufferCopyRecordCount;
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        {
            core::CpuMemoryQuotaLease full(quota, quota->limitBytes() - quota->liveBytes());
            assert(full.charged());
            core::CpuMemoryQuotaScope scope(quota);
            assert(!transfer->copyBufferToBuffer(t.f.copy).success);
            assert(!transfer->end().success && !encoder->finish());
        }
        const auto hotAllocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(
            encoder->lastValidation().diagnostics == "RHI-VULKAN-COLD-RESOURCE-REFERENCE-CAPACITY"
            && source.use_count() == sourceOwners && destination.use_count() == destinationOwners
            && records == t.f.resources.stats().nativeBufferCopyRecordCount
            && allocations == hotAllocations);
    }
    {
        OrdinaryRecordingStorageFixture t(runtime);
        // Negative fixture: no native query payload. This storage prerequisite
        // must reject before inspecting a pool or allocating query metadata;
        // it establishes no native timestamp/reset capability.
        VulkanRhiQueryPool query({}, {"ColdStorage.TimestampGate", QueryType::Timestamp, 1u});
        auto encoder = t.encoder();
        auto compute = encoder->beginComputePass({});
        const auto quota = t.original->originalMetadataQuota();
        const auto records = t.f.resources.stats().nativeCommandBufferRecordCount;
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        {
            core::CpuMemoryQuotaLease full(quota, quota->limitBytes() - quota->liveBytes());
            assert(full.charged());
            core::CpuMemoryQuotaScope scope(quota);
            assert(!compute->writeTimestamp(query, 0u).success && !compute->end().success
                && !encoder->finish());
        }
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount
            && records == t.f.resources.stats().nativeCommandBufferRecordCount);
        assert(
            encoder->lastValidation().diagnostics == "RHI-VULKAN-COLD-QUERY-STORAGE-UNAVAILABLE");
    }
    std::printf("ordinary original-derived barrier/reference storage: 3 buffer/2 image/1 memory, "
                "8 repeated references/3 original resources, cold exact/one-short CPU=%llu, "
                "flush cannot replenish, atomic copy refusal, zero hot allocation, "
                "native copy/completion and original journal cancellation PASS\n",
        static_cast<unsigned long long>(measured));
}

void nativeRecordingStorage(const std::shared_ptr<VulkanRhiRuntime>& runtime)
{
    OrdinaryRecordingStorageFixture t(runtime);
    auto quota = t.original->originalMetadataQuota();
    const auto source = vulkanRhiNativePayload(*t.source);
    const auto destination = vulkanRhiNativePayload(*t.destination);
    const auto image = vulkanRhiNativePayload(*t.image);
    const auto pools = t.f.resources.stats().nativeCommandPoolCreateCount;
    const VulkanRhiRecordingStorageLimits impossible{0u, 0u, 0u, SIZE_MAX};
    assert(!createVulkanRhiNativeCommandPayload(t.f.resources, t.f.diagnostic, {}, &impossible)
        && t.f.diagnostic == "RHI-VULKAN-COLD-RECORDING-STORAGE-ALLOCATION"
        && t.f.resources.stats().nativeCommandPoolCreateCount == pools);
    // These directly exercise backend array guards, without claiming original
    // executor authority. Real resources and native command payloads are used.
    for (unsigned category = 0u; category < 4u; ++category) {
        for (const std::size_t capacity : {0u, 1u}) {
            VulkanRhiRecordingStorageLimits limits;
            if (category == 0u)
                limits.bufferBarriers = capacity;
            if (category == 1u)
                limits.imageBarriers = capacity;
            if (category >= 2u)
                limits.memoryBarriers = capacity;
            std::shared_ptr<VulkanRhiNativeCommandPayload> native;
            {
                core::CpuMemoryQuotaScope scope(quota);
                native =
                    createVulkanRhiNativeCommandPayload(t.f.resources, t.f.diagnostic, {}, &limits);
            }
            assert(native && native->recordingStorageBound());
            assert(std::string_view(
                       native->recordingBarrierStorageDiagnostic(CommandUsageKind::Finish))
                == "RHI-VULKAN-COLD-BARRIER-STORAGE-KIND");
            auto record = [&] {
                if (category == 0u)
                    return native->recordBufferBarrier(*source, t.bufferEnter, t.f.diagnostic);
                if (category == 1u)
                    return native->recordImageBarrier(*image, t.imageEnter, t.f.diagnostic);
                if (category == 2u)
                    return native->recordResourceMemoryBarrier(t.imageMemory, t.f.diagnostic);
                AliasingBarrierDesc alias;
                alias.aliasGroup = 1u; // Test-authored barrier, never submitted.
                alias.beforeResource = t.source->id();
                alias.afterResource = t.destination->id();
                alias.sourceStages = alias.destinationStages = PipelineStage::Transfer;
                alias.sourceAccess = ResourceAccess::TransferRead;
                alias.destinationAccess = ResourceAccess::TransferWrite;
                return native->recordAliasingBarrier(alias, t.f.diagnostic);
            };
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            {
                core::CpuMemoryQuotaLease full(quota, quota->limitBytes() - quota->liveBytes());
                assert(full.charged());
                core::CpuMemoryQuotaScope scope(quota);
                if (capacity) {
                    assert(record() && native->flushPendingBarriers(t.f.diagnostic));
                }
                const auto records = t.f.resources.stats().nativeCommandBufferRecordCount;
                assert(!record() && native->recordingStorageDiagnostic()
                    && !native->end(t.f.diagnostic)
                    && records == t.f.resources.stats().nativeCommandBufferRecordCount);
            }
            assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        }
    }
    for (unsigned mode = 0u; mode < 4u; ++mode) {
        const VulkanRhiRecordingStorageLimits limits{0u, 0u, 0u, mode == 0u ? 1u : 2u};
        auto native =
            createVulkanRhiNativeCommandPayload(t.f.resources, t.f.diagnostic, {}, &limits);
        assert(native);
        const auto ownersBefore = source.use_count();
        const auto otherBefore = destination.use_count();
        const std::shared_ptr<VulkanRhiNativeResourcePayload> owners[]{source, destination};
        const std::shared_ptr<VulkanRhiNativeResourcePayload> missing[]{source, {}};
        if (mode == 0u)
            assert(native->retainResources(owners, 2u));
        if (mode == 1u) {
            assert(!native->retainResources(&source, 1u) && !native->retainResources(&source, 1u));
            assert(
                source.use_count() == ownersBefore + 2u + 2u); // Two local arrays plus two slots.
            assert(native->retainResources(&source, 1u));
        }
        if (mode == 2u)
            assert(native->retainResources(missing, 2u));
        if (mode == 3u)
            assert(native->retainResources(nullptr, 1u));
        if (mode != 1u)
            assert(source.use_count() == ownersBefore + 2u
                && destination.use_count() == otherBefore + 1u);
        assert(native->recordingStorageDiagnostic() && !native->end(t.f.diagnostic));
    }
    std::puts(
        "native recording storage guards: independent exact/one-short arrays, checked kind, "
        "cumulative flush, repeated owners, atomic missing/overflow batch and terminal end PASS");
}

void recordingBudget(const std::shared_ptr<VulkanRhiRuntime>& runtime)
{
    CopyFixture f(runtime);
    CommandRecordingBudget budget{1u, true, 3u};
    for (const auto kind : {CommandUsageKind::BufferCopy,
             CommandUsageKind::BufferMemoryBarrier,
             CommandUsageKind::Finish})
        budget.limits.append(kind);
    budget.limits.resourceReferences = 3u;
    const ResourceMemoryBarrierDesc dependency{f.destination->id(),
        PipelineStage::Transfer,
        PipelineStage::Transfer,
        ResourceAccess::TransferWrite,
        ResourceAccess::TransferWrite};
    for (unsigned variant = 0u; variant < 3u; ++variant) {
        auto limit = budget;
        const char* expected = nullptr;
        if (variant == 0u) {
            limit.maxCommands = 1u;
            expected = "RHI-COMMAND-RECORDING-BUDGET-COMMAND-CAPACITY";
        } else if (variant == 1u) {
            limit.limits.commands[static_cast<std::size_t>(CommandUsageKind::BufferMemoryBarrier)] =
                0u;
            expected = "RHI-COMMAND-RECORDING-BUDGET-OPCODE-CAPACITY";
        } else {
            --limit.limits.resourceReferences;
            expected = "RHI-COMMAND-RECORDING-BUDGET-REFERENCE-CAPACITY";
        }
        auto c = f.command();
        assert(c->enableRecordingBudget(limit, f.diagnostic));
        ResourceBarrierDesc same;
        same.resource = f.source->id();
        same.before = same.after = ResourceState::CopySrc;
        same.sourceStages = same.destinationStages = PipelineStage::Transfer;
        same.sourceAccess = same.destinationAccess = ResourceAccess::TransferRead;
        const auto before = f.resources.stats().nativeCommandBufferRecordCount;
        assert(!c->transition(*f.source, same, f.diagnostic)
            && f.diagnostic == "RHI-COMMAND-RECORDING-BUDGET-OPCODE-CAPACITY"
            && vulkanRhiBufferStateCommitted(*f.source)
            && !c->retentionStorage()->resourceOwnerCount()
            && f.resources.stats().nativeCommandBufferRecordCount == before);
        assert(c->copy(*f.source, *f.destination, f.copy, f.diagnostic));
        const auto used = *c->recordingUsage();
        const auto records = f.resources.stats().nativeCommandBufferRecordCount;
        assert(!c->dependency(*f.destination, dependency, f.diagnostic) && f.diagnostic == expected
            && c->recordingUsage()->matches(used)
            && c->retentionStorage()->resourceOwnerCount() == 2u
            && f.resources.stats().nativeCommandBufferRecordCount == records);
    }
    f.verify(false); // Cancelled commands cannot write staging.
    auto c = f.command();
    auto invalid = budget;
    invalid.contractVersion = 2u;
    assert(!c->enableRecordingBudget(invalid, f.diagnostic)
        && f.diagnostic == "RHI-COMMAND-RECORDING-BUDGET-VERSION");
    assert(c->enableRecordingBudget(budget, f.diagnostic));
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    assert(c->copy(*f.source, *f.destination, f.copy, f.diagnostic));
    const auto once = *c->recordingUsage();
    const auto records = f.resources.stats().nativeCommandBufferRecordCount;
    assert(!c->copy(*f.source, *f.destination, f.copy, f.diagnostic)
        && f.diagnostic == "RHI-COMMAND-RECORDING-BUDGET-OPCODE-CAPACITY"
        && c->recordingUsage()->matches(once)
        && f.resources.stats().nativeCommandBufferRecordCount == records);
    assert(c->dependency(*f.destination, dependency, f.diagnostic) && c->finish(f.diagnostic));
    assert(c->recordingUsage()->matches(budget.limits)
        && core::defaultCpuAllocator().stats().allocationCount == allocations);
    assert(c->submit({}, f.diagnostic));
    assert(c->waitUntil(std::chrono::steady_clock::now() + std::chrono::seconds(5), f.diagnostic));
    f.verify();
    c.reset();
    retireCompletedVulkanRhiSubmittedCommands(*runtime);
    assert(!f.quota->liveBytes());
    std::puts(
        "recording budget: real buffer copy/dependency exact, short, repeated, cancellation PASS");
}

void rejection(const std::shared_ptr<VulkanRhiRuntime>& runtime)
{
    CopyFixture f(runtime);
    for (const std::uint32_t capacity : {0u, 1u}) {
        auto command = f.command(capacity);
        const auto before = f.resources.stats().nativeBufferCopyRecordCount;
        assert(!command->copy(*f.source, *f.destination, f.copy, f.diagnostic));
        assert(f.diagnostic == "RHI-COMMAND-RETENTION-OWNER-CAPACITY"
            && !command->retentionStorage()->resourceOwnerCount()
            && f.resources.stats().nativeBufferCopyRecordCount == before);
        if (capacity == 1u) {
            const ResourceMemoryBarrierDesc barrier{f.destination->id(),
                PipelineStage::Transfer,
                PipelineStage::Transfer,
                ResourceAccess::TransferWrite,
                ResourceAccess::TransferWrite};
            assert(command->dependency(*f.destination, barrier, f.diagnostic));
            assert(command->retentionStorage()->resourceOwnerCount() == 1u);
            assert(!command->copy(*f.source, *f.destination, f.copy, f.diagnostic));
            assert(command->retentionStorage()->resourceOwnerCount() == 1u
                && f.diagnostic == "RHI-COMMAND-RETENTION-OWNER-CAPACITY"
                && f.resources.stats().nativeBufferCopyRecordCount == before);
        }
        assert(command->finish(f.diagnostic) && command->submit({}, f.diagnostic));
        assert(command->waitUntil(
            std::chrono::steady_clock::now() + std::chrono::seconds(5), f.diagnostic));
        f.verify(false);
        retireCompletedVulkanRhiSubmittedCommands(*runtime);
        command.reset();
        assert(!f.quota->liveBytes());
    }
    auto command = f.command();
    const auto before = f.resources.stats().nativeBufferCopyRecordCount;
    for (unsigned invalid = 0u; invalid < 7u; ++invalid) {
        auto desc = f.copy;
        if (invalid == 0u)
            desc.sizeBytes = 0u;
        if (invalid == 1u)
            desc.sizeBytes = sizeof(f.expected) + 4u;
        if (invalid == 2u)
            desc.sourceOffsetBytes = 1u;
        if (invalid == 3u)
            desc.destinationOffsetBytes = std::numeric_limits<std::uint64_t>::max() - 3u;
        if (invalid == 4u)
            desc.sourceBuffer = {};
        if (invalid == 5u)
            desc.sizeBytes = 3u;
        if (invalid == 6u)
            desc.destinationBuffer = desc.sourceBuffer;
        assert(!command->copy(*f.source, *f.destination, desc, f.diagnostic));
        assert(f.diagnostic == "RHI-VULKAN-RETENTION-BUFFER-COPY-DESCRIPTOR"
            && !command->retentionStorage()->resourceOwnerCount());
    }
    auto wrongUsage =
        f.buffer(ResourceUsage::UniformBuffer, MemoryUsage::CpuToGpu, ResourceState::CopySrc);
    auto desc = f.copy;
    desc.sourceBuffer = wrongUsage->id();
    assert(!command->copy(*wrongUsage, *f.destination, desc, f.diagnostic));
    auto wrongState =
        f.buffer(ResourceUsage::CopySrc, MemoryUsage::CpuToGpu, ResourceState::ShaderRead);
    desc.sourceBuffer = wrongState->id();
    assert(!command->copy(*wrongState, *f.destination, desc, f.diagnostic));
    {
        core::MutationJournal pending;
        ResourceBarrierDesc state;
        state.resource = f.source->id();
        state.before = state.after = ResourceState::CopySrc;
        assert(transitionVulkanRhiBuffer(*f.source, state, &pending).success);
        assert(!command->copy(*f.source, *f.destination, f.copy, f.diagnostic)
            && f.diagnostic == "RHI-VULKAN-RETENTION-BUFFER-STATE-UNCOMMITTED"
            && !command->retentionStorage()->resourceOwnerCount());
    }
    assert(f.source->state() == ResourceState::CopySrc);
    const auto original = vulkanRhiNativePayload(*f.source);
    auto otherRegistry = core::makeShared<VulkanRhiResourceRegistry>();
    otherRegistry->registerResource(f.source->id(), original);
    auto foreign = VulkanRhiRetainedTransferCommand::prepare(
        f.resources, otherRegistry, storage(f.quota), f.diagnostic);
    assert(foreign && !foreign->copy(*f.source, *f.destination, f.copy, f.diagnostic)
        && f.diagnostic == "RHI-VULKAN-RETENTION-BUFFER-REGISTRATION");
    auto unrelated = core::makeShared<int>(0);
    f.registry->registerResource(f.source->id(), {unrelated, original.get()});
    assert(!command->copy(*f.source, *f.destination, f.copy, f.diagnostic)
        && f.diagnostic == "RHI-VULKAN-RETENTION-BUFFER-REGISTRATION");
    f.registry->registerResource(f.source->id(), original);
    f.destination->sealWrites();
    assert(!command->copy(*f.source, *f.destination, f.copy, f.diagnostic)
        && f.diagnostic == "RHI-BUFFER-GPU-WRITES-SEALED");
    assert(!command->retentionStorage()->resourceOwnerCount()
        && f.resources.stats().nativeBufferCopyRecordCount == before);
    std::puts(
        "native rejection: atomic 0/1 owner ceilings, descriptor/usage/state/registry/control-block/write-seal gates PASS");
}

void completion(const std::shared_ptr<VulkanRhiRuntime>& runtime, bool dropWrapper)
{
    CopyFixture f(runtime);
    auto command = f.command();
    f.quota->setLimitBytes(kMetadataBytes + 1u);
    assert(!command->copy(*f.source, *f.destination, f.copy, f.diagnostic)
        && f.diagnostic == "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH");
    f.quota->setLimitBytes(kMetadataBytes);
    ResourceMemoryBarrierDesc barrier{f.destination->id(),
        PipelineStage::Transfer,
        PipelineStage::Transfer,
        ResourceAccess::TransferWrite,
        ResourceAccess::TransferWrite};
    auto invalidBarrier = barrier;
    invalidBarrier.sourceStages = PipelineStage::ComputeShader;
    assert(!command->dependency(*f.destination, invalidBarrier, f.diagnostic)
        && !command->retentionStorage()->resourceOwnerCount());
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    for (unsigned i = 0u; i < 32u; ++i) {
        assert(command->copy(*f.source, *f.destination, f.copy, f.diagnostic));
        assert(command->dependency(*f.destination, barrier, f.diagnostic));
        assert(command->retentionStorage()->resourceOwnerCount() == 2u);
    }
    assert(command->finish(f.diagnostic));
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    assert(!command->copy(*f.source, *f.destination, f.copy, f.diagnostic));
    const std::weak_ptr<void> source = vulkanRhiNativePayload(*f.source);
    f.source.reset();
    auto gate = VulkanRhiTimelineSemaphorePayload::create(runtime, 0u, f.diagnostic);
    assert(gate);
    VulkanRhiTimelineSubmitInfo wait{gate, 1u};
    f.quota->setLimitBytes(kMetadataBytes + 1u);
    assert(!command->submit(wait, f.diagnostic)
        && f.diagnostic == "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH");
    f.quota->setLimitBytes(kMetadataBytes);
    const auto beforeSubmit = core::defaultCpuAllocator().stats().allocationCount;
    assert(command->submit(wait, f.diagnostic));
    assert(core::defaultCpuAllocator().stats().allocationCount == beforeSubmit);
    assert(!command->completed() && !source.expired());
    assert(!command->waitUntil(std::chrono::steady_clock::now(), f.diagnostic));
    failFenceObservation = true;
    assert(!command->completed() && !source.expired());
    failFenceObservation = false;
    if (dropWrapper)
        command.reset();
    retireCompletedVulkanRhiSubmittedCommands(*runtime);
    assert(!source.expired() && f.quota->liveBytes());
    assert(gate->hostSignal(1u) == VK_SUCCESS);
    if (command) {
        assert(command->waitUntil(
            std::chrono::steady_clock::now() + std::chrono::seconds(5), f.diagnostic));
        assert(command->completed() && !command->retentionStorage());
        assert(f.quota->liveBytes());
    } else {
        assert(!waitIdleAndRetireVulkanRhiSubmittedCommands(runtime));
    }
    retireCompletedVulkanRhiSubmittedCommands(*runtime);
    assert(source.expired());
    f.verify();
    command.reset();
    assert(!f.quota->liveBytes());
    std::printf(
        "native completion: 32 copies, no core record/finish/submit allocations, exact readback, delayed retirement, wrapper-dropped=%d PASS\n",
        dropWrapper);
}

void cancellationAndFailures(const std::shared_ptr<VulkanRhiRuntime>& runtime)
{
    CopyFixture f(runtime);
    auto prepared = storage(f.quota);
    const auto remaining = kMetadataBytes - f.quota->liveBytes();
    assert(f.quota->tryReserve(remaining));
    assert(!VulkanRhiRetainedTransferCommand::prepare(
        f.resources, f.registry, std::move(prepared), f.diagnostic));
    assert(f.diagnostic == "RHI-COMMAND-RETENTION-PREPARATION-ALLOCATION-FAILED"
        && f.quota->liveBytes() == remaining);
    f.quota->release(remaining);
    assert(!f.quota->liveBytes());
    auto command = f.command();
    assert(command->copy(*f.source, *f.destination, f.copy, f.diagnostic));
    const std::weak_ptr<void> source = vulkanRhiNativePayload(*f.source);
    f.source.reset();
    assert(!source.expired());
    command.reset();
    assert(source.expired() && !f.quota->liveBytes());
    f.verify(false);

    f.source = f.buffer(ResourceUsage::CopySrc, MemoryUsage::CpuToGpu, ResourceState::CopySrc);
    f.copy.sourceBuffer = f.source->id();
    command = f.command();
    assert(command->copy(*f.source, *f.destination, f.copy, f.diagnostic));
    const std::weak_ptr<void> uncertain = vulkanRhiNativePayload(*f.source);
    f.source.reset();
    assert(command->finish(f.diagnostic));
    failSubmit = true;
    assert(!command->submit({}, f.diagnostic) && !failSubmit);
    assert(f.diagnostic == "RHI-VULKAN-RETENTION-SUBMIT-UNPROVEN"
        && command->retentionStorage()->state() == Storage::State::Submitted);
    assert(!command->completed() && !uncertain.expired());
    command.reset();
    retireCompletedVulkanRhiSubmittedCommands(*runtime);
    assert(!uncertain.expired() && f.quota->liveBytes());
    assert(!waitIdleAndRetireVulkanRhiSubmittedCommands(runtime));
    assert(uncertain.expired() && !f.quota->liveBytes());
    f.verify(false);
    std::puts(
        "native lifetime: preparation exhaustion, abandonment, failed queue quarantine and real idle retirement PASS");
}

void stateTransactions(const std::shared_ptr<VulkanRhiRuntime>& runtime, bool unproven)
{
    CopyFixture f(runtime);
    f.source = f.buffer(ResourceUsage::StorageBuffer | ResourceUsage::CopySrc,
        MemoryUsage::CpuToGpu,
        ResourceState::ShaderWrite);
    f.copy.sourceBuffer = f.source->id();
    {
        const auto native = vulkanRhiNativePayload(*f.source);
        std::memcpy(native->backendMappedData(), f.expected.data(), sizeof(f.expected));
        assert(native->backendFlushMappedWrite(0u, sizeof(f.expected), f.diagnostic));
    }
    ResourceBarrierDesc barrier;
    barrier.resource = f.source->id();
    barrier.before = ResourceState::ShaderWrite;
    barrier.after = ResourceState::CopySrc;
    barrier.sourceStages = PipelineStage::ComputeShader;
    barrier.destinationStages = PipelineStage::Transfer;
    barrier.sourceAccess = ResourceAccess::ShaderWrite;
    barrier.destinationAccess = ResourceAccess::TransferRead;
    {
        auto zero = f.command(0u);
        const auto before = f.resources.stats().nativeBufferBarrierRecordCount;
        assert(!zero->transition(*f.source, barrier, f.diagnostic)
            && f.diagnostic == "RHI-COMMAND-RETENTION-OWNER-CAPACITY"
            && !zero->retentionStorage()->resourceOwnerCount()
            && f.source->state() == ResourceState::ShaderWrite
            && vulkanRhiBufferStateCommitted(*f.source)
            && f.resources.stats().nativeBufferBarrierRecordCount == before);
    }
    for (const bool sealed : {false, true}) {
        auto cancelled = f.command();
        const auto before = f.resources.stats().nativeBufferBarrierRecordCount;
        for (unsigned i = 0u; i < 5u; ++i) {
            auto invalid = barrier;
            if (i == 0u)
                invalid.sourceStages = PipelineStage::None;
            if (i == 1u)
                invalid.destinationAccess = ResourceAccess::None;
            if (i == 2u)
                invalid.before = ResourceState::CopyDst;
            if (i == 3u)
                invalid.destinationStages = PipelineStage::ComputeShader;
            if (i == 4u)
                invalid.resource = {};
            assert(!cancelled->transition(*f.source, invalid, f.diagnostic)
                && !cancelled->retentionStorage()->resourceOwnerCount()
                && vulkanRhiBufferStateCommitted(*f.source)
                && f.resources.stats().nativeBufferBarrierRecordCount == before);
        }
        {
            core::MutationJournal other;
            auto same = barrier;
            same.after = same.before;
            assert(transitionVulkanRhiBuffer(*f.source, same, &other).success);
            assert(!cancelled->transition(*f.source, barrier, f.diagnostic)
                && !cancelled->retentionStorage()->resourceOwnerCount());
        }
        assert(cancelled->transition(*f.source, barrier, f.diagnostic));
        assert(!vulkanRhiBufferStateCommitted(*f.source)
            && f.source->state() == ResourceState::CopySrc);
        auto competing = f.command();
        assert(!competing->copy(*f.source, *f.destination, f.copy, f.diagnostic)
            && f.diagnostic == "RHI-VULKAN-RETENTION-BUFFER-STATE-UNCOMMITTED");
        if (sealed)
            assert(cancelled->finish(f.diagnostic));
        cancelled.reset();
        assert(vulkanRhiBufferStateCommitted(*f.source)
            && f.source->state() == ResourceState::ShaderWrite);
    }
    {
        const auto payload = vulkanRhiNativePayload(*f.source);
        {
            auto alias =
                createVulkanRhiBuffer(f.source->id(), f.source->desc(), {}, payload, f.registry);
            auto rejected = f.command();
            assert(!rejected->transition(*alias, barrier, f.diagnostic)
                && f.diagnostic == "RHI-VULKAN-RETENTION-BUFFER-REGISTRATION"
                && !rejected->retentionStorage()->resourceOwnerCount());
        }
        f.registry->registerResource(f.source->id(), payload);
        f.registry->registerBufferObject(f.source->id(), *f.source);
    }
    auto command = f.command();
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    assert(command->transition(*f.source, barrier, f.diagnostic)
        && command->copy(*f.source, *f.destination, f.copy, f.diagnostic)
        && command->finish(f.diagnostic));
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    f.quota->setLimitBytes(kMetadataBytes + 1u);
    assert(!command->submit({}, f.diagnostic) && !vulkanRhiBufferStateCommitted(*f.source));
    f.quota->setLimitBytes(kMetadataBytes);
    if (unproven) {
        failSubmit = true;
        assert(!command->submit({}, f.diagnostic) && !failSubmit
            && f.diagnostic == "RHI-VULKAN-RETENTION-SUBMIT-UNPROVEN");
        assert(!vulkanRhiBufferStateCommitted(*f.source));
        command.reset();
        assert(!waitIdleAndRetireVulkanRhiSubmittedCommands(runtime));
        auto retry = barrier;
        retry.before = retry.after = ResourceState::CopySrc;
        assert(!vulkanRhiBufferStateCommitted(*f.source)
            && !transitionVulkanRhiBuffer(*f.source, retry).success);
        f.verify(false);
    } else {
        auto gate = VulkanRhiTimelineSemaphorePayload::create(runtime, 0u, f.diagnostic);
        const auto beforeSubmit = core::defaultCpuAllocator().stats().allocationCount;
        assert(gate && command->submit({gate, 1u}, f.diagnostic));
        assert(core::defaultCpuAllocator().stats().allocationCount == beforeSubmit
            && vulkanRhiBufferStateCommitted(*f.source)
            && f.source->state() == ResourceState::CopySrc && !command->completed());
        const std::weak_ptr<void> source = vulkanRhiNativePayload(*f.source);
        f.source.reset();
        command.reset();
        assert(!source.expired() && gate->hostSignal(1u) == VK_SUCCESS);
        assert(!waitIdleAndRetireVulkanRhiSubmittedCommands(runtime));
        assert(source.expired());
        f.verify();
    }
    assert(!f.quota->liveBytes());
    std::printf(
        "native state transactions: atomic gates, exact journal, cancellation, queue commit/terminal invalidation, no hot allocations, unproven=%d PASS\n",
        unproven);
}

// The ordinary allocating clear is cold fixture preparation. Only the retained
// barriers/copy/finish/submit below establish the bounded private texture slice.
class TextureFixture final {
public:
    TextureFixture(const std::shared_ptr<VulkanRhiRuntime>& runtime, TextureFormat format)
        : f(runtime), depth(format == TextureFormat::D32Float)
    {
        TextureDesc desc;
        desc.label = "Retained.Texture.KnownTexels";
        desc.extent = {5u, 3u, 1u};
        desc.format = format;
        desc.mipLevelCount = 2u;
        desc.arrayLayerCount = 2u;
        desc.usage = TextureUsage::RenderAttachment | TextureUsage::Sampled | TextureUsage::CopySrc;
        texture = makeTexture(desc);
        range.aspect = depth ? TextureAspect::Depth : TextureAspect::Color;
        untouched = range;
        untouched.baseMipLevel = untouched.baseArrayLayer = 1u;
        seed();
        f.destination =
            f.buffer(ResourceUsage::CopyDst, MemoryUsage::GpuToCpu, ResourceState::CopyDst, 1024u);
        const auto staging = vulkanRhiNativePayload(*f.destination);
        std::memset(staging->backendMappedData(), 0xa5, 1024u);
        assert(staging->backendFlushMappedWrite(0u, 1024u, f.diagnostic));
        copy.sourceTexture = texture->id();
        copy.destinationBuffer = f.destination->id();
        copy.textureSubresources = range;
        copy.bufferOffsetBytes = 256u;
        copy.bytesPerRow = 256u;
        copy.rowsPerImage = 4u;
        copy.extent = desc.extent;
        read.resource = texture->id();
        read.textureSubresources = range;
        read.before = ResourceState::ShaderRead;
        read.after = ResourceState::CopySrc;
        read.sourceStages = PipelineStage::ComputeShader;
        read.destinationStages = PipelineStage::Transfer;
        read.sourceAccess = ResourceAccess::ShaderRead;
        read.destinationAccess = ResourceAccess::TransferRead;
        write.resource = f.destination->id();
        write.before = write.after = ResourceState::CopyDst;
        write.sourceStages = write.destinationStages = PipelineStage::Transfer;
        write.sourceAccess = write.destinationAccess = ResourceAccess::TransferWrite;
    }

    core::UniquePtr<GpuTexture> makeTexture(const TextureDesc& desc)
    {
        const auto native = f.resources.createTexture(desc);
        assert(native.success && native.payload && native.allocation.success);
        const GpuResourceId id{f.nextId++};
        f.registry->registerTexture(id, native.payload);
        auto result =
            createVulkanRhiTexture(id, desc, native.allocation, native.payload, f.registry);
        assert(result && f.registry->textureObjectMatches(*result));
        return result;
    }

    ResourceBarrierDesc restore() const
    {
        auto result = read;
        std::swap(result.before, result.after);
        std::swap(result.sourceStages, result.destinationStages);
        std::swap(result.sourceAccess, result.destinationAccess);
        return result;
    }

    RenderPassDesc clearDescriptor(bool alternate = false) const
    {
        RenderPassDesc pass;
        if (depth) {
            pass.hasDepthStencilAttachment = true;
            pass.depthStencilAttachment.target = texture->id();
            pass.depthStencilAttachment.format = texture->desc().format;
            pass.depthStencilAttachment.depthLoadOp = LoadOp::Clear;
            pass.depthStencilAttachment.depthClearValue = alternate ? 0.75f : 0.25f;
        } else {
            RenderPassColorAttachmentDesc attachment;
            attachment.target = texture->id();
            attachment.format = texture->desc().format;
            attachment.loadOp = LoadOp::Clear;
            attachment.clearColor =
                alternate ? Color{0.5f, 0.25f, 1.0f, 0.75f} : Color{0.25f, 0.5f, 0.75f, 1.0f};
            pass.colorAttachments.push_back(attachment);
        }
        return pass;
    }

    void verify(const std::shared_ptr<VulkanRhiNativeResourcePayload>& staging,
        bool copied,
        bool alternate = false)
    {
        assert(staging->backendInvalidateMappedRead(0u, 1024u, f.diagnostic));
        const auto* data = static_cast<const unsigned char*>(staging->backendMappedData());
        const std::array<std::uint16_t, 4u> rgba = alternate
            ? std::array<std::uint16_t, 4u>{0x3800u, 0x3400u, 0x3c00u, 0x3a00u}
            : std::array<std::uint16_t, 4u>{0x3400u, 0x3800u, 0x3a00u, 0x3c00u};
        const float z = alternate ? 0.75f : 0.25f;
        const auto* pixel = depth ? reinterpret_cast<const unsigned char*>(&z)
                                  : reinterpret_cast<const unsigned char*>(rgba.data());
        const std::size_t bpp = depth ? 4u : 8u;
        for (std::size_t i = 0u; i < 1024u; ++i) {
            const bool texel =
                copied && i >= 256u && (i - 256u) / 256u < 3u && (i - 256u) % 256u < 5u * bpp;
            assert(data[i] == (texel ? pixel[((i - 256u) % 256u) % bpp] : 0xa5u));
        }
    }

    CopyFixture f;
    bool depth;
    core::UniquePtr<GpuTexture> texture;
    TextureSubresourceRange range, untouched;
    ResourceBarrierDesc read, write;
    TextureToBufferCopyDesc copy;

private:
    void seed()
    {
        const auto image = vulkanRhiNativePayload(*texture);
        auto command = createVulkanRhiNativeCommandPayload(f.resources, f.diagnostic);
        assert(command);
        ResourceBarrierDesc barrier;
        barrier.resource = texture->id();
        barrier.textureSubresources = range;
        barrier.after = depth ? ResourceState::DepthAttachment : ResourceState::ColorAttachment;
        barrier.sourceStages = PipelineStage::TopOfPipe;
        barrier.destinationStages =
            depth ? PipelineStage::DepthStencil : PipelineStage::ColorAttachmentOutput;
        barrier.destinationAccess =
            depth ? ResourceAccess::DepthStencilWrite : ResourceAccess::ColorAttachmentWrite;
        assert(transitionVulkanRhiTexture(*texture, barrier).success
            && command->recordImageBarrier(*image, barrier, f.diagnostic)
            && command->flushPendingBarriers(f.diagnostic));
        auto pass = clearDescriptor();
        core::pmr::Vector<std::shared_ptr<VulkanRhiNativeResourcePayload>> colors;
        if (!depth)
            colors.push_back(image);
        assert(command->recordRenderPassBegin(
                   pass, colors, depth ? image.get() : nullptr, f.diagnostic)
            && command->recordRenderPassEnd(f.diagnostic));
        barrier.before = barrier.after;
        barrier.after = ResourceState::ShaderRead;
        barrier.sourceStages = barrier.destinationStages;
        barrier.sourceAccess = barrier.destinationAccess;
        barrier.destinationStages = PipelineStage::ComputeShader;
        barrier.destinationAccess = ResourceAccess::ShaderRead;
        assert(transitionVulkanRhiTexture(*texture, barrier).success
            && command->recordImageBarrier(*image, barrier, f.diagnostic)
            && command->flushPendingBarriers(f.diagnostic) && command->end(f.diagnostic)
            && command->submit(f.diagnostic));
        assert(vkDeviceWaitIdle(f.runtime->logicalDevice()) == VK_SUCCESS
            && command->submittedFenceCompleted());
        assert(vulkanRhiTextureStateCommitted(*texture)
            && texture->subresourceState(range) == ResourceState::ShaderRead
            && texture->subresourceState(untouched) == ResourceState::Undefined);
    }
};

void textureRejection(const std::shared_ptr<VulkanRhiRuntime>& runtime, TextureFormat format)
{
    TextureFixture t(runtime, format);
    auto& f = t.f;
    for (const auto capacity : {0u, 1u}) {
        auto command = f.command(capacity);
        const auto before = f.resources.stats();
        if (!capacity) {
            assert(!command->transition(*t.texture, t.read, f.diagnostic)
                && vulkanRhiTextureStateCommitted(*t.texture)
                && !command->retentionStorage()->resourceOwnerCount());
        } else {
            assert(command->transition(*t.texture, t.read, f.diagnostic));
            assert(!command->copy(*t.texture, *f.destination, t.copy, f.diagnostic)
                && command->retentionStorage()->resourceOwnerCount() == 1u);
        }
        assert(f.diagnostic == "RHI-COMMAND-RETENTION-OWNER-CAPACITY"
            && f.resources.stats().nativeImageToBufferCopyRecordCount
                == before.nativeImageToBufferCopyRecordCount);
    }
    for (const bool sealed : {false, true}) {
        auto command = f.command();
        const auto before = f.resources.stats().nativeImageBarrierRecordCount;
        for (unsigned i = 0u; i < 13u; ++i) {
            auto bad = t.read;
            if (i == 0u)
                bad.resource = {};
            if (i == 1u)
                bad.textureSubresources.baseMipLevel = 2u;
            if (i == 2u)
                bad.textureSubresources.baseArrayLayer = 2u;
            if (i == 3u)
                bad.textureSubresources.mipLevelCount = 0u;
            if (i == 4u)
                bad.textureSubresources.aspect = TextureAspect::Stencil;
            if (i == 5u)
                bad.before = ResourceState::CopyDst;
            if (i == 6u)
                bad.after = ResourceState::Undefined;
            if (i == 7u)
                bad.destinationStages = PipelineStage::ComputeShader;
            if (i == 8u)
                bad.destinationAccess = ResourceAccess::None;
            if (i == 9u)
                bad.sourceStages = PipelineStage::VertexShader;
            if (i == 10u)
                bad.after =
                    t.depth ? ResourceState::ColorAttachment : ResourceState::DepthAttachment;
            if (i == 11u)
                bad.after = ResourceState::ShaderWrite;
            if (i == 12u)
                bad.textureSubresources.arrayLayerCount = UINT32_MAX;
            assert(!command->transition(*t.texture, bad, f.diagnostic)
                && !command->retentionStorage()->resourceOwnerCount()
                && vulkanRhiTextureStateCommitted(*t.texture)
                && f.resources.stats().nativeImageBarrierRecordCount == before);
        }
        {
            core::MutationJournal foreign;
            auto same = t.read;
            same.after = same.before;
            assert(transitionVulkanRhiTexture(*t.texture, same, &foreign).success);
            assert(!command->transition(*t.texture, t.read, f.diagnostic)
                && !command->retentionStorage()->resourceOwnerCount());
        }
        assert(command->transition(*t.texture, t.read, f.diagnostic));
        const auto copies = f.resources.stats().nativeImageToBufferCopyRecordCount;
        for (unsigned i = 0u; i < 15u; ++i) {
            auto bad = t.copy;
            if (i == 0u)
                bad.sourceTexture = {};
            if (i == 1u)
                bad.destinationBuffer = {};
            if (i == 2u)
                bad.bytesPerRow = 40u;
            if (i == 3u)
                bad.rowsPerImage = 2u;
            if (i == 4u)
                bad.bufferOffsetBytes = 1u;
            if (i == 5u)
                bad.bufferOffsetBytes = UINT64_MAX - 7u;
            if (i == 6u)
                bad.extent.width = 6u;
            if (i == 7u)
                bad.extent.depth = 2u;
            if (i == 8u)
                bad.textureSubresources.mipLevelCount = 2u;
            if (i == 9u)
                bad.textureSubresources.arrayLayerCount = 2u;
            if (i == 10u)
                bad.textureSubresources.aspect = TextureAspect::Stencil;
            if (i == 11u)
                bad.mipLayouts.push_back({});
            if (i == 12u)
                bad.bufferOffsetBytes = 768u;
            if (i == 13u)
                bad.extent.height = 0u;
            if (i == 14u)
                bad.bufferOffsetBytes = t.depth ? 2u : 260u;
            assert(!command->copy(*t.texture, *f.destination, bad, f.diagnostic)
                && command->retentionStorage()->resourceOwnerCount() == 1u
                && f.resources.stats().nativeImageToBufferCopyRecordCount == copies);
        }
        auto competitor = f.command();
        assert(!competitor->copy(*t.texture, *f.destination, t.copy, f.diagnostic));
        if (sealed)
            assert(command->finish(f.diagnostic));
        command.reset();
        assert(vulkanRhiTextureStateCommitted(*t.texture)
            && t.texture->subresourceState(t.range) == ResourceState::ShaderRead
            && t.texture->subresourceState(t.untouched) == ResourceState::Undefined);
    }
    const auto image = vulkanRhiNativePayload(*t.texture);
    {
        auto alias =
            createVulkanRhiTexture(t.texture->id(), t.texture->desc(), {}, image, f.registry);
        auto command = f.command();
        assert(!command->transition(*alias, t.read, f.diagnostic)
            && f.diagnostic == "RHI-VULKAN-RETENTION-TEXTURE-REGISTRATION"
            && !command->retentionStorage()->resourceOwnerCount());
    }
    f.registry->registerTexture(t.texture->id(), image);
    f.registry->registerTextureObject(t.texture->id(), *t.texture);
    auto command = f.command();
    auto foreignRegistry = core::makeShared<VulkanRhiResourceRegistry>();
    foreignRegistry->registerTexture(t.texture->id(), image);
    auto foreign = VulkanRhiRetainedCommand::prepare(
        f.resources, foreignRegistry, storage(f.quota), f.diagnostic);
    assert(foreign && !foreign->transition(*t.texture, t.read, f.diagnostic)
        && f.diagnostic == "RHI-VULKAN-RETENTION-TEXTURE-REGISTRATION");
    foreign.reset();
    auto unrelated = core::makeShared<int>(0);
    f.registry->registerTexture(t.texture->id(), {unrelated, image.get()});
    assert(!command->transition(*t.texture, t.read, f.diagnostic)
        && f.diagnostic == "RHI-VULKAN-RETENTION-TEXTURE-REGISTRATION"
        && !command->retentionStorage()->resourceOwnerCount());
    f.registry->registerTexture(t.texture->id(), image);
    auto uninitialized = t.read;
    uninitialized.textureSubresources = t.untouched;
    uninitialized.before = ResourceState::Undefined;
    uninitialized.sourceStages = PipelineStage::TopOfPipe;
    uninitialized.sourceAccess = ResourceAccess::None;
    assert(!command->transition(*t.texture, uninitialized, f.diagnostic));
    uninitialized.after = t.depth ? ResourceState::DepthAttachment : ResourceState::ColorAttachment;
    uninitialized.destinationStages =
        t.depth ? PipelineStage::DepthStencil : PipelineStage::ColorAttachmentOutput;
    uninitialized.destinationAccess =
        t.depth ? ResourceAccess::DepthStencilWrite : ResourceAccess::ColorAttachmentWrite;
    assert(command->transition(*t.texture, uninitialized, f.diagnostic));
    command.reset();
    assert(vulkanRhiTextureStateCommitted(*t.texture)
        && t.texture->subresourceState(t.untouched) == ResourceState::Undefined);
    auto wrongDesc = t.texture->desc();
    wrongDesc.usage = TextureUsage::Sampled;
    auto wrongUsage = t.makeTexture(wrongDesc);
    command = f.command();
    uninitialized.resource = wrongUsage->id();
    assert(!command->transition(*wrongUsage, uninitialized, f.diagnostic)
        && !command->retentionStorage()->resourceOwnerCount());
    wrongDesc.format = TextureFormat::Rgba8Unorm;
    auto wrongFormat = t.makeTexture(wrongDesc);
    uninitialized.resource = wrongFormat->id();
    assert(!command->transition(*wrongFormat, uninitialized, f.diagnostic)
        && f.diagnostic == "RHI-VULKAN-RETENTION-TEXTURE-SHAPE"
        && !command->retentionStorage()->resourceOwnerCount());
    assert(command->transition(*t.texture, t.read, f.diagnostic));
    {
        core::MutationJournal pending;
        assert(transitionVulkanRhiBuffer(*f.destination, t.write, &pending).success);
        assert(!command->copy(*t.texture, *f.destination, t.copy, f.diagnostic)
            && f.diagnostic == "RHI-VULKAN-RETENTION-BUFFER-STATE-UNCOMMITTED");
    }
    f.destination->sealWrites();
    assert(!command->copy(*t.texture, *f.destination, t.copy, f.diagnostic)
        && f.diagnostic == "RHI-BUFFER-GPU-WRITES-SEALED");
    t.verify(vulkanRhiNativePayload(*f.destination), false);
    std::printf("native texture negatives/cancellation: format=%u PASS\n", unsigned(format));
}

void textureCompletion(const std::shared_ptr<VulkanRhiRuntime>& runtime,
    TextureFormat format,
    bool dropWrapper,
    bool unproven,
    bool preparedClear = false)
{
    TextureFixture t(runtime, format);
    auto& f = t.f;
    auto command = f.command();
    if (preparedClear) {
        const auto desc = t.clearDescriptor(true);
        assert(command->prepareClearStorePass(desc, *t.texture, t.range, f.diagnostic));
    }
    const auto cold = f.resources.stats();
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    if (preparedClear) {
        auto enter = t.read;
        enter.after = t.depth ? ResourceState::DepthAttachment : ResourceState::ColorAttachment;
        enter.destinationStages =
            t.depth ? PipelineStage::DepthStencil : PipelineStage::ColorAttachmentOutput;
        enter.destinationAccess =
            t.depth ? ResourceAccess::DepthStencilWrite : ResourceAccess::ColorAttachmentWrite;
        assert(command->transition(*t.texture, enter, f.diagnostic)
            && command->clearPreparedPass(*t.texture, f.diagnostic));
        assert(!command->clearPreparedPass(*t.texture, f.diagnostic)
            && f.diagnostic == "RHI-VULKAN-RETENTION-CLEAR-PREPARED-PASS");
        enter.before = enter.after;
        enter.sourceStages = enter.destinationStages;
        enter.sourceAccess = enter.destinationAccess;
        enter.after = ResourceState::ShaderRead;
        enter.destinationStages = PipelineStage::ComputeShader;
        enter.destinationAccess = ResourceAccess::ShaderRead;
        assert(command->transition(*t.texture, enter, f.diagnostic));
    }
    assert(command->transition(*t.texture, t.read, f.diagnostic)
        && command->transition(*f.destination, t.write, f.diagnostic)
        && command->copy(*t.texture, *f.destination, t.copy, f.diagnostic)
        && command->transition(*t.texture, t.restore(), f.diagnostic)
        && command->finish(f.diagnostic));
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations
        && !vulkanRhiTextureStateCommitted(*t.texture)
        && !vulkanRhiBufferStateCommitted(*f.destination)
        && command->retentionStorage()->resourceOwnerCount() == 2u);
    assert(f.resources.stats().nativeRenderPassCreateCount == cold.nativeRenderPassCreateCount
        && f.resources.stats().nativeFramebufferCreateCount == cold.nativeFramebufferCreateCount
        && f.resources.stats().nativeRenderAttachmentViewCreateCount
            == cold.nativeRenderAttachmentViewCreateCount);
    f.quota->setLimitBytes(kMetadataBytes + 1u);
    assert(!command->submit({}, f.diagnostic) && !vulkanRhiTextureStateCommitted(*t.texture));
    f.quota->setLimitBytes(kMetadataBytes);
    if (unproven) {
        failSubmit = true;
        assert(!command->submit({}, f.diagnostic) && !failSubmit
            && f.diagnostic == "RHI-VULKAN-RETENTION-SUBMIT-UNPROVEN");
        assert(!vulkanRhiTextureStateCommitted(*t.texture)
            && !vulkanRhiBufferStateCommitted(*f.destination));
        command.reset();
        retireCompletedVulkanRhiSubmittedCommands(*runtime);
        assert(f.quota->liveBytes());
        if (preparedClear)
            assert((clearNativeLive == std::array<unsigned, 3u>{1u, 1u, 1u}));
        assert(!waitIdleAndRetireVulkanRhiSubmittedCommands(runtime));
        auto retry = t.read;
        retry.after = retry.before;
        assert(!transitionVulkanRhiTexture(*t.texture, retry).success
            && !transitionVulkanRhiBuffer(*f.destination, t.write).success);
        t.verify(vulkanRhiNativePayload(*f.destination), false);
    } else {
        auto gate = VulkanRhiTimelineSemaphorePayload::create(runtime, 0u, f.diagnostic);
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        assert(gate && command->submit({gate, 1u}, f.diagnostic));
        assert(core::defaultCpuAllocator().stats().allocationCount == before
            && vulkanRhiTextureStateCommitted(*t.texture)
            && vulkanRhiBufferStateCommitted(*f.destination) && !command->completed()
            && t.texture->subresourceState(t.range) == ResourceState::ShaderRead
            && t.texture->subresourceState(t.untouched) == ResourceState::Undefined);
        const std::weak_ptr<void> image = vulkanRhiNativePayload(*t.texture);
        const auto staging = vulkanRhiNativePayload(*f.destination);
        t.texture.reset();
        f.destination.reset();
        if (dropWrapper)
            command.reset();
        retireCompletedVulkanRhiSubmittedCommands(*runtime);
        assert(!image.expired() && f.quota->liveBytes());
        if (preparedClear)
            assert((clearNativeLive == std::array<unsigned, 3u>{1u, 1u, 1u}));
        assert(gate->hostSignal(1u) == VK_SUCCESS);
        if (command) {
            assert(command->waitUntil(
                std::chrono::steady_clock::now() + std::chrono::seconds(5), f.diagnostic));
            command.reset();
        }
        assert(!waitIdleAndRetireVulkanRhiSubmittedCommands(runtime) && image.expired());
        t.verify(staging, true, preparedClear);
    }
    assert(!f.quota->liveBytes());
    assert((clearNativeLive == std::array<unsigned, 3u>{}));
    std::printf(
        "native texture transaction/copy: format=%u dropped=%d unproven=%d preparedClear=%d padded exact bytes/no hot allocations PASS\n",
        unsigned(format),
        dropWrapper,
        unproven,
        preparedClear);
}

void clearPreparation(const std::shared_ptr<VulkanRhiRuntime>& runtime, TextureFormat format)
{
    TextureFixture t(runtime, format);
    auto& f = t.f;
    const auto desc = t.clearDescriptor(true);
    const auto original = clearNativeLive;
    {
        auto command = f.command(0u);
        assert(!command->prepareClearStorePass(desc, *t.texture, t.range, f.diagnostic)
            && f.diagnostic == "RHI-COMMAND-RETENTION-OWNER-CAPACITY"
            && clearNativeLive == original);
    }
    {
        auto command = f.command(1u);
        assert(command->prepareClearStorePass(desc, *t.texture, t.range, f.diagnostic)
            && command->transition(*t.texture, t.read, f.diagnostic));
        const auto copies = f.resources.stats().nativeImageToBufferCopyRecordCount;
        assert(!command->copy(*t.texture, *f.destination, t.copy, f.diagnostic)
            && f.diagnostic == "RHI-COMMAND-RETENTION-OWNER-CAPACITY"
            && command->retentionStorage()->resourceOwnerCount() == 1u
            && f.resources.stats().nativeImageToBufferCopyRecordCount == copies);
    }
    assert(clearNativeLive == original && vulkanRhiTextureStateCommitted(*t.texture));
    for (unsigned failure = 1u; failure <= 3u; ++failure) {
        auto command = f.command();
        clearCreationFailure = failure;
        assert(!command->prepareClearStorePass(desc, *t.texture, t.range, f.diagnostic)
            && !clearCreationFailure && f.diagnostic == "RHI-VULKAN-RETENTION-CLEAR-NATIVE-CREATION"
            && !command->retentionStorage()->resourceOwnerCount() && clearNativeLive == original);
        assert(command->prepareClearStorePass(desc, *t.texture, t.range, f.diagnostic));
        assert(!command->prepareClearStorePass(desc, *t.texture, t.range, f.diagnostic)
            && f.diagnostic == "RHI-VULKAN-RETENTION-CLEAR-PREPARATION-STATE");
        assert(!command->clearPreparedPass(*t.texture, f.diagnostic)
            && f.diagnostic == "RHI-VULKAN-RETENTION-CLEAR-TARGET-STATE");
        command.reset();
        assert(clearNativeLive == original && !f.quota->liveBytes());
    }
    {
        auto command = f.command();
        for (unsigned i = 0u; i < 11u; ++i) {
            auto bad = desc;
            auto range = t.range;
            if (i == 0u)
                bad.layeredRendering = true;
            if (i == 1u)
                bad.subpasses.push_back({});
            if (i == 2u)
                bad.subpassDependencies.push_back({});
            if (i == 3u)
                range.arrayLayerCount = 2u;
            if (i == 4u)
                range.baseMipLevel = 2u;
            if (i == 10u)
                range.mipLevelCount = 2u;
            if (t.depth) {
                auto& a = bad.depthStencilAttachment;
                if (i == 5u)
                    a.depthLoadOp = LoadOp::Load;
                if (i == 6u)
                    a.depthStoreOp = StoreOp::DontCare;
                if (i == 7u)
                    a.target = {};
                if (i == 8u)
                    a.depthClearValue = std::numeric_limits<float>::quiet_NaN();
                if (i == 9u)
                    a.depthReadOnly = true;
            } else {
                auto& a = bad.colorAttachments.front();
                if (i == 5u)
                    a.loadOp = LoadOp::Load;
                if (i == 6u)
                    a.storeOp = StoreOp::DontCare;
                if (i == 7u)
                    a.target = {};
                if (i == 8u)
                    a.clearColor.r = std::numeric_limits<float>::quiet_NaN();
                if (i == 9u)
                    a.hasResolveTarget = true;
            }
            assert(!command->prepareClearStorePass(bad, *t.texture, range, f.diagnostic)
                && !command->retentionStorage()->resourceOwnerCount()
                && clearNativeLive == original);
        }
        assert(command->transition(*t.texture, t.read, f.diagnostic));
        assert(!command->prepareClearStorePass(desc, *t.texture, t.range, f.diagnostic)
            && f.diagnostic == "RHI-VULKAN-RETENTION-CLEAR-PREPARATION-STATE");
    }
    const auto clearBudget = [] {
        CommandRecordingBudget b{1u, true, 4u};
        for (const auto kind : {CommandUsageKind::TextureBarrier,
                 CommandUsageKind::BeginRenderPass,
                 CommandUsageKind::EndRenderPass,
                 CommandUsageKind::Finish})
            b.limits.append(kind);
        b.limits.resourceReferences = 2u;
        return b;
    }();
    for (unsigned variant = 0u; variant < 3u; ++variant) {
        auto b = clearBudget;
        const char* expected = nullptr;
        if (variant == 0u) {
            b.maxCommands = 2u;
            expected = "RHI-COMMAND-RECORDING-BUDGET-COMMAND-CAPACITY";
        } else if (variant == 1u) {
            b.limits.commands[static_cast<std::size_t>(CommandUsageKind::EndRenderPass)] = 0u;
            expected = "RHI-COMMAND-RECORDING-BUDGET-OPCODE-CAPACITY";
        } else {
            --b.limits.resourceReferences;
            expected = "RHI-COMMAND-RECORDING-BUDGET-REFERENCE-CAPACITY";
        }
        auto command = f.command();
        assert(command->enableRecordingBudget(b, f.diagnostic));
        assert(command->prepareClearStorePass(desc, *t.texture, t.range, f.diagnostic));
        auto enter = t.read;
        enter.after = t.depth ? ResourceState::DepthAttachment : ResourceState::ColorAttachment;
        enter.destinationStages =
            t.depth ? PipelineStage::DepthStencil : PipelineStage::ColorAttachmentOutput;
        enter.destinationAccess =
            t.depth ? ResourceAccess::DepthStencilWrite : ResourceAccess::ColorAttachmentWrite;
        assert(command->transition(*t.texture, enter, f.diagnostic));
        const auto used = *command->recordingUsage();
        const auto begins = f.resources.stats().nativeRenderPassBeginRecordCount;
        assert(!command->clearPreparedPass(*t.texture, f.diagnostic) && f.diagnostic == expected
            && command->recordingUsage()->matches(used)
            && f.resources.stats().nativeRenderPassBeginRecordCount == begins
            && t.texture->subresourceState(t.range) == enter.after);
    }
    for (const bool sealed : {false, true}) {
        auto command = f.command();
        assert(command->enableRecordingBudget(clearBudget, f.diagnostic));
        assert(command->prepareClearStorePass(desc, *t.texture, t.range, f.diagnostic));
        auto enter = t.read;
        enter.after = t.depth ? ResourceState::DepthAttachment : ResourceState::ColorAttachment;
        enter.destinationStages =
            t.depth ? PipelineStage::DepthStencil : PipelineStage::ColorAttachmentOutput;
        enter.destinationAccess =
            t.depth ? ResourceAccess::DepthStencilWrite : ResourceAccess::ColorAttachmentWrite;
        {
            core::MutationJournal foreign;
            assert(transitionVulkanRhiTexture(*t.texture, enter, &foreign).success);
            assert(!command->clearPreparedPass(*t.texture, f.diagnostic)
                && f.diagnostic == "RHI-VULKAN-RETENTION-CLEAR-TARGET-STATE");
        }
        assert(command->transition(*t.texture, enter, f.diagnostic));
        const auto nativeBegins = f.resources.stats().nativeRenderPassBeginRecordCount;
        missingClearRecord = true;
        assert(!command->clearPreparedPass(*t.texture, f.diagnostic)
            && f.diagnostic == "RHI-VULKAN-RETENTION-CLEAR-RECORD-ENTRYPOINT"
            && f.resources.stats().nativeRenderPassBeginRecordCount == nativeBegins);
        missingClearRecord = false;
        assert(command->clearPreparedPass(*t.texture, f.diagnostic));
        if (sealed) {
            assert(command->finish(f.diagnostic));
            assert(command->recordingUsage()->matches(clearBudget.limits));
        }
        command.reset();
        assert(clearNativeLive == original && !f.quota->liveBytes()
            && vulkanRhiTextureStateCommitted(*t.texture)
            && t.texture->subresourceState(t.range) == ResourceState::ShaderRead);
    }
    std::printf(
        "native cold Clear/Store preparation: format=%u descriptor/creation failures/cancellation PASS\n",
        unsigned(format));
}

void timestampNative(const std::shared_ptr<VulkanRhiRuntime>& runtime)
{
    assert(runtime->timestampQueriesEnabled() && !runtime->timestampQueryDiagnostic());
    QueryPoolDesc desc{"Timestamp.HostReset", QueryType::Timestamp, 2u};
    std::array<std::uint64_t, 1u> scopeIds{0x1234u};
    desc.timingScopeIds = scopeIds.data();
    desc.timingScopeIdCount = static_cast<std::uint32_t>(scopeIds.size());
    std::array<char, 128u> reported{};
    RhiErrorHandler::get().setCallback(
        [](RhiSeverity, const char* message, void* data) {
            auto& buffer = *static_cast<std::array<char, 128u>*>(data);
            std::snprintf(buffer.data(), buffer.size(), "%s", message);
        },
        &reported);
    const auto baseline = queryLivePools;
    for (const char* procedure : {"vkCreateQueryPool",
             "vkDestroyQueryPool",
             "vkResetQueryPool",
             "vkCmdWriteTimestamp",
             "vkGetQueryPoolResults"}) {
        const auto creates = queryCreateCalls;
        missingQueryProcedure = procedure;
        assert(!runtime->timestampQueriesEnabled() && !createVulkanRhiQueryPool(runtime, desc)
            && queryCreateCalls == creates
            && std::string_view(reported.data()) == "RHI-VULKAN-TIMESTAMP-ENTRYPOINTS");
        missingQueryProcedure = nullptr;
    }
    failQueryCreation = true;
    assert(!createVulkanRhiQueryPool(runtime, desc) && queryLivePools == baseline
        && std::string_view(reported.data()) == "RHI-VULKAN-TIMESTAMP-CREATE-FAILED");
    failQueryCreation = false;
    loseQueryResetAfterCreation = true;
    assert(!createVulkanRhiQueryPool(runtime, desc) && queryLivePools == baseline
        && std::string_view(reported.data()) == "RHI-VULKAN-TIMESTAMP-ENTRYPOINTS");
    loseQueryResetAfterCreation = false;
    missingQueryProcedure = nullptr;
    auto quota = core::makeShared<core::CpuMemoryQuota>(kMetadataBytes);
    const auto factory = [&] {
        core::CpuMemoryQuotaScope scope(quota);
        return createVulkanRhiQueryPool(runtime, desc);
    };
    const auto peak = test::CpuQuotaFactoryFixture(quota).measureRetained(factory);
    {
        core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - peak);
        auto exact = factory();
        assert(exact && queryLivePools == baseline + 1u);
    }
    for (const auto available : {peak - 1u, std::uint64_t{0u}}) {
        const auto creates = queryCreateCalls;
        core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - available);
        assert(!factory() && queryCreateCalls == creates && queryLivePools == baseline
            && std::string_view(reported.data()) == "RHI-VULKAN-TIMESTAMP-CPU-ALLOCATION-FAILED");
    }
    RhiErrorHandler::get().setCallback(nullptr);
    assert(!quota->liveBytes());
    auto pool = createVulkanRhiQueryPool(runtime, desc);
    assert(pool && pool->executionStatus().state == QueryExecutionState::NotRecorded);
    auto* nativePool = asVulkanRhiQueryPool(*pool);
    assert(nativePool && nativePool->nativeHandle());
    scopeIds[0] = 0x5678u;
    assert(nativePool->payload()->scopeIds().size() == 1u
        && nativePool->payload()->scopeIds()[0] == 0x1234u);
    const std::weak_ptr<VulkanRhiQueryPoolPayload> retained = nativePool->payload();
    core::pmr::String diagnostic;
    diagnostic.reserve(512u);
    VulkanRhiNativeResourceAllocator resources(runtime);
    {
        auto command = createVulkanRhiNativeCommandPayload(resources, diagnostic);
        const auto resets = queryResets;
        {
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes());
            core::CpuMemoryQuotaScope scope(quota);
            assert(command && !command->recordTimestamp(*pool, 0u, diagnostic));
        }
        assert(diagnostic == "RHI-VULKAN-TIMESTAMP-RECORD-ALLOCATION-FAILED"
            && pool->executionStatus().state == QueryExecutionState::NotRecorded
            && queryResets == resets);
    }
    {
        auto command = createVulkanRhiNativeCommandPayload(resources, diagnostic);
        assert(command && command->recordTimestamp(*pool, 0u, diagnostic));
        const auto resets = queryResets;
        assert(!pool->reset().success && queryResets == resets
            && pool->executionStatus().state == QueryExecutionState::Recorded);
        assert(!command->recordTimestamp(*pool, 0u, diagnostic));
        assert(!command->recordTimestamp(*pool, 2u, diagnostic));
        assert(command->end(diagnostic));
        failSubmit = true;
        const auto reads = observedQueryReads;
        assert(!command->submit(diagnostic)
            && pool->executionStatus().state == QueryExecutionState::DiagnosticFailure
            && !pool->reset().success && queryResets == resets);
        assert(diagnostic == "RHI-VULKAN-QUERY-SUBMIT-UNPROVEN");
        command.reset();
        assert(
            !waitIdleAndRetireVulkanRhiSubmittedCommands(runtime) && observedQueryReads == reads);
    }
    assert(pool->executionStatus().state == QueryExecutionState::DiagnosticFailure);
    assert(pool->reset().success);
    {
        tests::VulkanHeadlessRuntime foreign(false, resolve);
        assert(foreign.runtime() && foreign.runtime()->timestampQueriesEnabled());
        VulkanRhiNativeResourceAllocator other(foreign.runtime());
        assert(nativePool->payload()->completedTimingSample(*foreign.runtime(), 1u).availability
            == GpuTimingAvailability::DiagnosticFailure);
        auto command = createVulkanRhiNativeCommandPayload(other, diagnostic);
        assert(command && !command->recordTimestamp(*pool, 0u, diagnostic)
            && pool->executionStatus().state == QueryExecutionState::NotRecorded);
    }
    for (bool dropCaller : {false, true}) {
        auto command = createVulkanRhiNativeCommandPayload(resources, diagnostic);
        assert(command && command->recordTimestamp(*pool, 0u, diagnostic)
            && command->recordTimestamp(*pool, 1u, diagnostic) && command->end(diagnostic));
        auto gate = VulkanRhiTimelineSemaphorePayload::create(runtime, 0u, diagnostic);
        assert(gate);
        assert(command->submit({gate, 1u}, diagnostic));
        const auto resets = queryResets;
        assert(!command->submittedFenceCompleted()
            && pool->executionStatus().state == QueryExecutionState::Pending
            && !pool->reset().success && queryResets == resets);
        if (dropCaller) {
            pool.reset();
            command.reset();
            retireCompletedVulkanRhiSubmittedCommands(*runtime);
        }
        assert(!retained.expired());
        assert(gate->hostSignal(1u) == VK_SUCCESS);
        if (command) {
            assert(command->waitSubmittedFenceUntil(
                std::chrono::steady_clock::now() + std::chrono::seconds(5), diagnostic));
        } else {
            assert(!waitIdleAndRetireVulkanRhiSubmittedCommands(runtime));
        }
        if (pool) {
            const auto status = pool->executionStatus();
            assert(status.state == QueryExecutionState::Available && status.resetSupported
                && status.asynchronousReadbackSupported && pool->reset().success);
            assert(pool->executionStatus().state == QueryExecutionState::NotRecorded);
        }
        command.reset();
        retireCompletedVulkanRhiSubmittedCommands(*runtime);
    }
    assert(retained.expired() && queryLivePools == baseline);
    std::printf("native timestamp: host reset, exact/short CPU=%llu, missing APIs/create failure, "
                "record/cancel/pending/complete/reuse and caller-release lifetime PASS\n",
        static_cast<unsigned long long>(peak));
}

void querySlotsNative(const std::shared_ptr<VulkanRhiRuntime>& runtime)
{
    using Phase = VulkanRhiQueryPoolPayload::SlotControl::Phase;
    OriginalQueryCreationFixture f;
    auto owner =
        VulkanRhiQueryPool::prepareOriginal(runtime, f.input, f.budget, f.quotas[3], f.diagnostic);
    auto foreign =
        VulkanRhiQueryPool::prepareOriginal(runtime, f.input, f.budget, f.quotas[3], f.diagnostic);
    assert(owner && foreign);
    const auto payload = owner->payload();
    core::pmr::String diagnostic;
    diagnostic.reserve(512u);
    const auto initial = payload->observeSlot(0u);
    const auto otherSlot = payload->observeSlot(1u);
    const auto foreignSlot = foreign->payload()->observeSlot(0u);
    assert(initial && otherSlot && foreignSlot && !payload->observeSlot(2u)
        && !payload->matchesSlot(*foreignSlot));
    core::MutationJournal journal, otherJournal;
    assert(!payload->markRecorded(*foreignSlot, journal, diagnostic) && journal.empty());
    const auto resets = queryResets;
    assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
        f.quotas, [&]() noexcept -> const char* {
            core::CpuMemoryQuotaScope scope(f.quotas[3]);
            assert(payload->markRecorded(*initial, journal, diagnostic));
            const auto pending = payload->observeSlot(0u, &journal);
            assert(pending && !payload->observeSlot(0u) && payload->matchesSlot(*pending, &journal)
                && !payload->matchesSlot(*pending, &otherJournal)
                && payload->matchesSlot(*otherSlot));
            assert(!payload->markSubmitted(0u, diagnostic, &otherJournal) && otherJournal.empty()
                && queryResets == resets);
            assert(payload->markSubmitted(0u, diagnostic, &journal)
                && !payload->matchesSlot(*pending, &journal));
            assert(payload->rollbackSubmitted(0u, diagnostic, &journal));
            journal.rollback();
            assert(!payload->matchesSlot(*initial) && !payload->matchesSlot(*pending, &journal)
                && payload->observeSlot(0u)->phase() == Phase::NotRecorded);
            auto fresh = payload->observeSlot(0u);
            assert(fresh && payload->markRecorded(*fresh, journal, diagnostic));
            const auto committing = payload->observeSlot(0u, &journal);
            journal.commit();
            assert(committing && !payload->matchesSlot(*committing, &journal));
            const auto recorded = payload->observeSlot(0u);
            assert(!payload->markSubmitted(0u, diagnostic, &journal) && journal.empty()
                && payload->matchesSlot(*recorded));
            assert(recorded && payload->rollbackRecorded(0u) && !payload->matchesSlot(*recorded));
            fresh = payload->observeSlot(0u);
            assert(fresh && owner->reset().success && !payload->matchesSlot(*fresh));
            return nullptr;
        }));
    // An unready later slot refuses the whole reset before the first revision.
    {
        const auto first = payload->observeSlot(0u);
        const auto second = payload->observeSlot(1u);
        const auto resetsBefore = queryResets;
        assert(first && second && payload->markRecorded(*second, journal, diagnostic));
        assert(
            !owner->reset().success && queryResets == resetsBefore && payload->matchesSlot(*first));
        journal.rollback();
    }
    // Pre-invocation failure rolls back earlier Submit transitions; no queue call.
    VulkanRhiNativeResourceAllocator resources(runtime);
    {
        auto refused = createVulkanRhiNativeCommandPayload(resources, diagnostic);
        assert(refused && refused->recordTimestamp(*owner, 0u, diagnostic)
            && refused->recordTimestamp(*owner, 1u, diagnostic) && refused->end(diagnostic));
        const auto first = payload->observeSlot(0u);
        assert(payload->markSubmitted(1u, diagnostic));
        const auto invocations = queueSubmitCalls;
        assert(!refused->submit(diagnostic) && queueSubmitCalls == invocations
            && payload->observeSlot(0u)->phase() == Phase::Recorded
            && !payload->matchesSlot(*first));
        assert(payload->rollbackSubmitted(1u, diagnostic));
    }
    assert(owner->executionStatus().state == QueryExecutionState::NotRecorded);
    // The same controls now observe genuine command execution and completion.
    auto command = createVulkanRhiNativeCommandPayload(resources, diagnostic);
    assert(command);
    const auto beforeRecord = payload->observeSlot(0u);
    assert(beforeRecord && command->recordTimestamp(*owner, 0u, diagnostic)
        && command->recordTimestamp(*owner, 1u, diagnostic) && !payload->matchesSlot(*beforeRecord)
        && command->end(diagnostic));
    auto gate = VulkanRhiTimelineSemaphorePayload::create(runtime, 0u, diagnostic);
    assert(gate && command->submit({gate, 1u}, diagnostic));
    const auto submitted = payload->observeSlot(0u);
    observedQueryPool = payload->nativeHandle();
    observedQueryReads = 0u;
    assert(submitted && submitted->phase() == Phase::Submitted
        && !command->submittedFenceCompleted() && !observedQueryReads && !owner->reset().success);
    assert(gate->hostSignal(1u) == VK_SUCCESS
        && vkQueueWaitIdle(runtime->graphicsQueue()) == VK_SUCCESS);
    assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
        f.quotas, [&]() noexcept -> const char* {
            core::CpuMemoryQuotaScope scope(f.quotas[3]);
            assert(command->submittedFenceCompleted() && observedQueryReads == 2u
                && !payload->matchesSlot(*submitted));
            const auto available = payload->observeSlot(0u);
            assert(available && available->phase() == Phase::SnapshotDeclared
                && owner->reset().success && !payload->matchesSlot(*available));
            return nullptr;
        }));
    command.reset();
    retireCompletedVulkanRhiSubmittedCommands(*runtime);
    for (const bool timeline : {false, true}) {
        command = createVulkanRhiNativeCommandPayload(resources, diagnostic);
        assert(command && command->recordTimestamp(*owner, 0u, diagnostic)
            && command->end(diagnostic));
        const auto before = payload->observeSlot(0u);
        const auto reads = observedQueryReads;
        const auto resetCount = queryResets;
        failSubmit = true;
        assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
            f.quotas, [&]() noexcept -> const char* {
                core::CpuMemoryQuotaScope scope(f.quotas[3]);
                assert(!command->submit(timeline ? VulkanRhiTimelineSubmitInfo{gate, 2u}
                                                 : VulkanRhiTimelineSubmitInfo{},
                    diagnostic));
                return nullptr;
            }));
        const auto fenceProbes = fenceStatusCalls;
        assert(!command->submittedFenceCompleted()
            && !command->waitSubmittedFenceUntil(
                std::chrono::steady_clock::now() + std::chrono::seconds(5), diagnostic)
            && diagnostic == "RHI-VULKAN-QUERY-QUEUE-QUARANTINED"
            && fenceStatusCalls == fenceProbes);
        auto quarantined = payload->observeSlot(0u);
        assert(before && quarantined && quarantined->phase() == Phase::Quarantined
            && !payload->matchesSlot(*before) && !owner->reset().success
            && queryResets == resetCount && observedQueryReads == reads);
        command.reset();
        assert(!waitIdleAndRetireVulkanRhiSubmittedCommands(runtime));
        assert(observedQueryReads == reads && payload->observeSlot(0u)->phase() == Phase::FailedIdle
            && !payload->matchesSlot(*quarantined)
            && payload->completedTimingSample(*runtime, 700u).availability
                == GpuTimingAvailability::DiagnosticFailure
            && owner->reset().success);
    }
    // An unproven queue invocation retains the original payload after caller drop.
    {
        auto released = VulkanRhiQueryPool::prepareOriginal(
            runtime, f.input, f.budget, f.quotas[3], f.diagnostic);
        assert(released);
        std::weak_ptr<VulkanRhiQueryPoolPayload> retained = released->payload();
        auto failed = createVulkanRhiNativeCommandPayload(resources, diagnostic);
        assert(failed && failed->recordTimestamp(*released, 0u, diagnostic)
            && failed->end(diagnostic));
        failSubmit = true;
        assert(!failed->submit(diagnostic));
        released.reset();
        failed.reset();
        assert(!retained.expired());
        assert(!waitIdleAndRetireVulkanRhiSubmittedCommands(runtime));
        assert(retained.expired());
    }
    observedQueryPool = VK_NULL_HANDLE;
    std::puts("native query slots: original pool, single versioned state, exact journal, "
              "cancel/reset ABA, pending/commit/rollback, delayed native readback, "
              "queue failure with/without timeline quarantined until idle, hot allocation0 PASS");
}

void timestampDisabled()
{
    tests::VulkanHeadlessRuntime disabled(false, resolve, false, false);
    assert(disabled.runtime() && !disabled.runtime()->timestampQueriesEnabled());
    const auto creates = queryCreateCalls, resets = queryResets;
    assert(std::string_view(disabled.runtime()->timestampQueryDiagnostic())
        == "RHI-VULKAN-TIMESTAMP-HOST-RESET-DISABLED");
    assert(!createVulkanRhiQueryPool(disabled.runtime(), {"Disabled", QueryType::Timestamp, 2u})
        && queryCreateCalls == creates && queryResets == resets);
}

void timestampCompletion(const std::shared_ptr<VulkanRhiRuntime>& runtime)
{
    tests::VulkanHeadlessRuntime independent;
    assert(independent.runtime());
    auto quota = core::makeShared<core::CpuMemoryQuota>(kMetadataBytes);
    VulkanRhiNativeResourceAllocator resources(runtime);
    core::pmr::String diagnostic;
    diagnostic.reserve(512u);
    std::array<std::uint64_t, GpuTimingSample::kMaxScopes> ids{};
    for (std::size_t i = 0u; i < ids.size(); ++i)
        ids[i] = 0x1000u + i;
    const auto baseline = queryLivePools;
    for (const auto names : {1u, GpuTimingSample::kMaxScopes}) {
        QueryPoolDesc desc{
            "Timestamp.Completion", QueryType::Timestamp, names * 2u, ids.data(), names};
        const auto factory = [&] {
            core::CpuMemoryQuotaScope scope(quota);
            return createVulkanRhiQueryPool(runtime, desc);
        };
        const auto peak = test::CpuQuotaFactoryFixture(quota).measureRetained(factory);
        test::CpuQuotaFactoryFixture(quota).verify(peak, factory);
        auto pool = factory();
        assert(pool);
        const auto payload = asVulkanRhiQueryPool(*pool)->payload();
        observedQueryPool = payload->nativeHandle();
        observedQueryReads = 0u;
        assert(payload->completedTimingSample(*runtime, 1u).availability
                == GpuTimingAvailability::Pending
            && !observedQueryReads);
        auto command = createVulkanRhiNativeCommandPayload(resources, diagnostic);
        assert(command);
        const auto serial = 100u + names;
        command->observeFrameSerial(serial);
        for (std::uint32_t i = 0u; i < desc.count; ++i)
            assert(command->recordTimestamp(*pool, i, diagnostic));
        assert(command->end(diagnostic));
        auto gate = VulkanRhiTimelineSemaphorePayload::create(runtime, 0u, diagnostic);
        assert(gate && command->submit({gate, 1u}, diagnostic));
        assert(!command->submittedFenceCompleted()
            && payload->completedTimingSample(*runtime, serial).availability
                == GpuTimingAvailability::Pending
            && !observedQueryReads);
        assert(gate->hostSignal(1u) == VK_SUCCESS);
        // Native idle is orchestration only. The exhausted hot probe below
        // observes the genuine fence and must not allocate or wait for work.
        assert(vkQueueWaitIdle(runtime->graphicsQueue()) == VK_SUCCESS);
        assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota}, [&]() noexcept -> const char* {
                core::CpuMemoryQuotaScope scope(quota);
                assert(command->submittedFenceCompleted());
                const auto sample = payload->completedTimingSample(*runtime, serial);
                assert(sample.availability == GpuTimingAvailability::Available
                    && sample.completedFrameSerial == serial && sample.scopeCount == names);
                for (std::uint32_t i = 0u; i < names; ++i)
                    assert(sample.scopes[i].scopeId == ids[i]
                        && std::isfinite(sample.scopes[i].elapsedMs)
                        && sample.scopes[i].elapsedMs >= 0.0f);
                const auto isolated = latestVulkanRhiGpuTimingSample(*independent.runtime());
                assert(isolated.availability == GpuTimingAvailability::Pending
                    && isolated.completedFrameSerial == 0u && isolated.scopeCount == 0u);
                const auto published = latestVulkanRhiGpuTimingSample(*runtime);
                const auto prefix = names < GpuTimingSample::kMaxScopes ? 1u : 0u;
                assert(published.completedFrameSerial == serial
                    && published.scopeCount == names + prefix);
                for (std::uint32_t i = 0u; i < names; ++i)
                    assert(published.scopes[i + prefix].scopeId == ids[i]
                        && published.scopes[i + prefix].elapsedMs == sample.scopes[i].elapsedMs);
                return nullptr;
            }));
        assert(observedQueryReads == desc.count);
        missingQueryProcedure = "vkGetQueryPoolResults";
        assert(payload->completedTimingSample(*runtime, serial).availability
            == GpuTimingAvailability::DiagnosticFailure);
        missingQueryProcedure = nullptr;
        assert(pool->reset().success
            && payload->completedTimingSample(*runtime, serial).availability
                == GpuTimingAvailability::Pending);
        command.reset();
        retireCompletedVulkanRhiSubmittedCommands(*runtime);
        std::printf("timestamp completed scope cache: names=%u cold CPU=%llu, exact/short and "
                    "zero-allocation retirement/publication PASS\n",
            names,
            static_cast<unsigned long long>(peak));
    }
    assert(!quota->liveBytes() && queryLivePools == baseline);
    QueryPoolDesc desc{"Timestamp.Partial", QueryType::Timestamp, 2u, ids.data(), 1u};
    auto pool = createVulkanRhiQueryPool(runtime, desc);
    assert(pool);
    const auto payload = asVulkanRhiQueryPool(*pool)->payload();
    observedQueryPool = payload->nativeHandle();
    observedQueryReads = 0u;
    // Complete the pair across two genuine submissions. Never read its
    // unwritten end slot on the first completion or reread its cached begin.
    for (std::uint32_t i = 0u; i < 2u; ++i) {
        auto command = createVulkanRhiNativeCommandPayload(resources, diagnostic);
        assert(command && command->recordTimestamp(*pool, i, diagnostic));
        command->observeFrameSerial(200u + i);
        assert(command->end(diagnostic) && command->submit(diagnostic)
            && command->waitSubmittedFenceUntil(
                std::chrono::steady_clock::now() + std::chrono::seconds(5), diagnostic));
        const auto sample = payload->completedTimingSample(*runtime, 200u + i);
        assert(sample.availability
                == (i ? GpuTimingAvailability::Available : GpuTimingAvailability::Pending)
            && observedQueryReads == i + 1u);
        command.reset();
        retireCompletedVulkanRhiSubmittedCommands(*runtime);
    }
    assert(pool->reset().success);
    // No internal profiling pool: a named result must not acquire a stale
    // ID-zero full-frame result from the preceding command.
    for (const bool refuse : {false, true}) {
        missingQueryProcedure = "vkCmdResetQueryPool";
        auto command = createVulkanRhiNativeCommandPayload(resources, diagnostic);
        missingQueryProcedure = nullptr;
        assert(command && command->recordTimestamp(*pool, 0u, diagnostic)
            && command->recordTimestamp(*pool, 1u, diagnostic));
        const auto serial = refuse ? 302u : 301u;
        command->observeFrameSerial(serial);
        refuseObservedQueryResult = refuse;
        assert(command->end(diagnostic) && command->submit(diagnostic)
            && command->waitSubmittedFenceUntil(
                std::chrono::steady_clock::now() + std::chrono::seconds(5), diagnostic));
        const auto sample = payload->completedTimingSample(*runtime, serial);
        const auto published = latestVulkanRhiGpuTimingSample(*runtime);
        if (refuse) {
            assert(sample.availability == GpuTimingAvailability::DiagnosticFailure
                && sample.scopeCount == 0u && published.completedFrameSerial == 301u
                && pool->executionStatus().state == QueryExecutionState::DiagnosticFailure);
        } else {
            assert(sample.availability == GpuTimingAvailability::Available
                && published.completedFrameSerial == serial && published.scopeCount == 1u
                && published.scopes[0].scopeId == ids[0]);
        }
        assert(pool->reset().success);
        command.reset();
        retireCompletedVulkanRhiSubmittedCommands(*runtime);
    }
    {
        // An injected queue error proves neither execution nor initialization.
        // Native idle authorizes retirement, never a result read/publication.
        auto command = createVulkanRhiNativeCommandPayload(resources, diagnostic);
        assert(command && command->recordTimestamp(*pool, 0u, diagnostic)
            && command->recordTimestamp(*pool, 1u, diagnostic));
        command->observeFrameSerial(400u);
        assert(command->end(diagnostic));
        auto gate = VulkanRhiTimelineSemaphorePayload::create(runtime, 0u, diagnostic);
        assert(gate);
        const auto reads = observedQueryReads;
        failSubmit = true;
        assert(!command->submit({gate, 1u}, diagnostic)
            && pool->executionStatus().state == QueryExecutionState::DiagnosticFailure
            && !pool->reset().success);
        command.reset();
        assert(!waitIdleAndRetireVulkanRhiSubmittedCommands(runtime) && observedQueryReads == reads
            && payload->completedTimingSample(*runtime, 400u).availability
                == GpuTimingAvailability::DiagnosticFailure
            && latestVulkanRhiGpuTimingSample(*runtime).completedFrameSerial == 301u
            && pool->executionStatus().state == QueryExecutionState::DiagnosticFailure
            && pool->reset().success);
    }
    observedQueryPool = VK_NULL_HANDLE;
    std::puts("timestamp completion: partial pair/reset/reuse/refused readback and "
              "no stale full-frame result, unproven-submit idle retirement/reset PASS");
}

void originalMixedQueryJournalNative(const std::shared_ptr<VulkanRhiRuntime>& runtime,
    GpuDevice& device,
    const std::shared_ptr<QueryPool>& pool,
    const std::shared_ptr<const OriginalCommandQueryPoolSource>& source,
    OriginalQueryCreationFixture& f,
    const std::array<std::shared_ptr<core::CpuMemoryQuota>, 4u>& q,
    bool verifySubmitRollback)
{
    using K = CommandUsageKind;
    using A = CommandScalarArguments;
    using Inputs = OriginalCommandInputs;
    using Sources = OriginalCommandResourceSources;
    using Phase = VulkanRhiQueryPoolPayload::SlotControl::Phase;
    const auto nativePool = std::dynamic_pointer_cast<VulkanRhiQueryPool>(pool);
    assert(nativePool);
    const auto payload = nativePool->payload();
    VulkanRhiNativeResourceAllocator resources(runtime);
    core::pmr::String diagnostic;
    diagnostic.reserve(512u);
    auto capabilities = semantics::nativeBackendCapabilityContract(Backend::Vulkan);
    capabilities.queriedFromNativeDevice = !runtime->coreQueueCapabilityDiagnostic();
    capabilities.runtimeFeaturesQueried = true;
    capabilities.runtimeFeatures.clear();
    assert(runtime->timestampQueriesEnabled());
    capabilities.features.push_back(semantics::NativeBackendFeature::TimestampQueries);
    capabilities.runtimeFeatures.push_back(semantics::NativeBackendFeature::TimestampQueries);
    observedQueryPool = payload->nativeHandle();
    observedQueryReads = 0u;
    for (unsigned mode = verifySubmitRollback ? 5u : 0u; mode < (verifySubmitRollback ? 6u : 5u);
        ++mode) {
        // Each case owns one authentic common-device buffer. Failure invalidates
        // its state permanently, so the next case creates a fresh resource.
        BufferDesc desc;
        desc.label = "Original.MixedQueryJournal";
        desc.sizeBytes = sizeof(std::uint32_t);
        desc.usage = ResourceUsage::CopySrc | ResourceUsage::CopyDst;
        auto owned = device.createBuffer(desc);
        assert(owned && !device.validateOriginalResourceRegistration(*owned));
        auto* buffer = owned.get();
        const ResourceBarrierDesc enter{buffer->id(),
            {},
            ResourceState::Undefined,
            ResourceState::CopySrc,
            PipelineStage::TopOfPipe,
            PipelineStage::Transfer,
            ResourceAccess::None,
            ResourceAccess::TransferRead};
        const ResourceBarrierDesc leave{buffer->id(),
            {},
            ResourceState::CopySrc,
            ResourceState::CopyDst,
            PipelineStage::Transfer,
            PipelineStage::Transfer,
            ResourceAccess::TransferRead,
            ResourceAccess::TransferWrite};
        const std::array<Sources::Source, 1u> leases{OriginalCommandBufferLease::prepareOriginal(
            owned, {1u, true, 2u, kMetadataBytes}, q[2], f.diagnostic)};
        assert(!owned && !f.diagnostic);
        const std::array<CommandUsage, 7u> commands{{{K::BufferBarrier, 0u, 1u},
            {K::BeginComputePass, 1u, 0u},
            {K::Timestamp, 1u, 1u},
            {K::Timestamp, 2u, 1u},
            {K::EndComputePass, 3u, 0u},
            {K::BufferBarrier, 3u, 1u},
            {K::Finish, 4u, 0u}}};
        const std::array<OriginalCommandUsage::Reference, 4u> refs{{{0u}, {1u}, {1u}, {0u}}};
        OriginalCommandAdmissionBudget ab;
        ab.contractVersion = 1u;
        ab.prototypeOptIn = true;
        ab.maxCommands = commands.size();
        ab.maxReferences = refs.size();
        ab.maxInputDescriptors = 4u;
        ab.maxGraphOwners = 11u;
        ab.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::Buffer)] = 1u;
        ab.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::Query)] = 1u;
        using Domain = OriginalCommandAdmissionBudget::CpuDomain;
        for (const auto domain : {Domain::Source, Domain::Graph, Domain::Recording})
            ab.maxCpuBytes[static_cast<std::size_t>(domain)] = kMetadataBytes;
        auto sources = Sources::prepareColdOriginalTyped(
            leases.data(), leases.size(), ab, {7u, true, 1u}, q[2], f.diagnostic);
        assert(sources && !Sources::appendOriginalTyped(sources, {source}, device));
        auto usage = OriginalCommandUsage::prepareOriginal(sources,
            commands.data(),
            commands.size(),
            refs.data(),
            refs.size(),
            f.quotas[0],
            f.diagnostic);
        assert(usage && usage->retainedOwnerCount() == 7u);
        const auto none = [](K kind) {
            return *A::capture(kind, A::None{});
        };
        const std::array<CommandVariableArguments::Input, 7u> args{{{none(K::BufferBarrier)},
            {none(K::BeginComputePass)},
            {*A::capture(K::Timestamp, A::Query{0u})},
            {*A::capture(K::Timestamp, A::Query{1u})},
            {none(K::EndComputePass)},
            {none(K::BufferBarrier)},
            {none(K::Finish)}}};
        auto stream = CommandVariableArguments::prepareOriginal(args.data(),
            args.size(),
            {1u, true, commands.size(), 0u, 0u, 0u, kMetadataBytes},
            f.quotas[0],
            f.diagnostic);
        auto scalars =
            OriginalCommandScalars::prepareOriginal(usage, stream, f.quotas[0], f.diagnostic);
        assert(scalars && scalars->retainedOwnerCount() == 9u);
        const auto begin =
            *QueryPoolOriginalInputArguments::captureOriginal(K::Timestamp, 0u, f.input);
        const auto end =
            *QueryPoolOriginalInputArguments::captureOriginal(K::Timestamp, 1u, f.input);
        const std::array<QueryCommandPoints::Input, 2u> pointInputs{{{2u, begin}, {3u, end}}};
        const QueryCommandPointsBudget pb{1u,
            true,
            commands.size(),
            1u,
            2u,
            0u,
            0u,
            0u,
            0u,
            4u,
            2u,
            f.desc.label.size() * 2u,
            kMetadataBytes};
        auto points = QueryCommandPoints::prepareOriginal(
            stream, pointInputs.data(), pointInputs.size(), pb, f.quotas[0], f.diagnostic);
        assert(points);
        OriginalCommandInputsBudget ib;
        ib.contractVersion = 9u;
        ib.prototypeOptIn = true;
        ib.maxQueryBindings = 2u;
        ib.queryPoints = points;
        const auto generation = pool->originalResourceRegistration().generation();
        const std::array<Inputs::OriginalDescriptor, 4u> bindings{
            {{0u, *ResourceDependencyArguments::capture(K::BufferBarrier, enter)},
                {2u, OriginalCommandQueryBinding{1u, generation, begin}},
                {3u, OriginalCommandQueryBinding{1u, generation, end}},
                {5u, *ResourceDependencyArguments::capture(K::BufferBarrier, leave)}}};
        auto inputs = Inputs::prepareOriginal(
            scalars, bindings.data(), bindings.size(), ib, f.quotas[0], f.diagnostic);
        if (!inputs)
            std::fprintf(stderr, "mixed query inputs: %s\n", f.diagnostic);
        assert(inputs && inputs->retainedOwnerCount() == 11u);
        auto recordingQuota = core::makeShared<core::CpuMemoryQuota>(kMetadataBytes);
        VulkanRhiRecordingStorageLimits limits;
        limits.originalQueries = VulkanRhiRecordingStorageLimits::OriginalQueries{
            inputs, {1u, true, 0u, 1u, 2u, 0u, kMetadataBytes}, recordingQuota};
        auto& request = *limits.originalQueries;
        request.recordingContractVersion = 1u;
        request.recordingOptIn = true;
        request.commandBudget = {1u, true, commands.size()};
        for (const auto& argument : args)
            assert(!request.commandBudget.limits.appendArguments(argument));
        request.commandBudget.limits.resourceReferences = refs.size();
        auto encoder = createVulkanRhiCommandEncoderWithColdPassStorage(
            resources, {}, nullptr, capabilities, device, limits, diagnostic);
        if (!encoder)
            std::fprintf(stderr, "mixed query encoder: %s\n", diagnostic.c_str());
        assert(encoder);
        auto native = vulkanRhiCommandPayload(*encoder);
        native->observeFrameSerial(800u + mode);
        core::UniquePtr<ComputePassEncoder> pass;
        core::UniquePtr<CommandBuffer> finished;
        const auto reads = observedQueryReads, writes = queryWriteCalls;
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 9u> hotQuotas{{f.quotas[0],
            f.quotas[1],
            f.quotas[2],
            f.quotas[3],
            q[0],
            q[1],
            q[2],
            q[3],
            recordingQuota}};
        assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
            hotQuotas, [&]() noexcept -> const char* {
                core::CpuMemoryQuotaScope scope(recordingQuota);
                assert(encoder->transition(*buffer, enter).success);
                pass = encoder->beginComputePass({});
                assert(pass && pass->writeTimestamp(*pool, 0u).success
                    && pass->writeTimestamp(*pool, 1u).success && pass->end().success);
                assert(encoder->transition(*buffer, leave).success);
                finished = encoder->finish();
                assert(finished && finished->commandCount() == commands.size() - 1u
                    && vulkanRhiCommandPayload(*finished) == native
                    && !vulkanRhiBufferStateCommitted(*buffer)
                    && buffer->state() == ResourceState::CopyDst && !payload->observeSlot(0u)
                    && !payload->observeSlot(1u));
                if (mode == 1u)
                    assert(submitVulkanRhiCommandBuffer(*finished).workAccepted);
                return nullptr;
            }));
        assert(queryWriteCalls == writes + 2u);
        if (mode == 5u) {
            SubmissionRetirementCapacityFixture race(runtime);
            race.arm();
            const auto submits = queueSubmitCalls;
            const auto refused = submitVulkanRhiCommandBuffer(*finished);
            assert(!refused.success && !refused.workAccepted && !refused.submittedCommandCount
                && refused.diagnostics == "RHI-VULKAN-SUBMISSION-RETIREMENT-CAPACITY"
                && queueSubmitCalls == submits + 1u // The competitor alone invoked the queue.
                && !submissionCapacityRace && !native->submittedFenceCompleted()
                && !payload->observeSlot(0u) && !payload->observeSlot(1u)
                && !vulkanRhiBufferStateCommitted(*buffer)
                && buffer->state() == ResourceState::CopyDst && observedQueryReads == reads
                && !pool->reset().success);
            race.complete();
            assert(observedQueryReads == reads && !payload->observeSlot(0u)
                && !payload->observeSlot(1u) && !vulkanRhiBufferStateCommitted(*buffer));
            const auto retried = submitVulkanRhiCommandBuffer(*finished);
            assert(retried.success && retried.workAccepted && queueSubmitCalls == submits + 2u
                && vulkanRhiBufferStateCommitted(*buffer)
                && buffer->state() == ResourceState::CopyDst
                && native->waitSubmittedFenceUntil(
                    std::chrono::steady_clock::now() + std::chrono::seconds(5), diagnostic));
            retireCompletedVulkanRhiSubmittedCommands(*runtime);
            assert(observedQueryReads == reads + 2u
                && payload->completedTimingSample(*runtime, 800u + mode).availability
                    == GpuTimingAvailability::Available);
        } else if (mode == 1u || mode == 4u) {
            auto gate = mode == 4u
                ? VulkanRhiTimelineSemaphorePayload::create(runtime, 0u, diagnostic)
                : nullptr;
            if (gate)
                assert(submitVulkanRhiCommandBuffer(*finished, {gate, 1u}).workAccepted
                    && !native->submittedFenceCompleted() && !pool->reset().success);
            assert(vulkanRhiBufferStateCommitted(*buffer)
                && buffer->state() == ResourceState::CopyDst);
            std::weak_ptr<VulkanRhiNativeCommandPayload> retained = native;
            if (gate) {
                pass.reset();
                encoder.reset();
                finished.reset();
                native.reset();
                retireCompletedVulkanRhiSubmittedCommands(*runtime);
                assert(!retained.expired() && observedQueryReads == reads);
                assert(gate->hostSignal(1u) == VK_SUCCESS
                    && vkQueueWaitIdle(runtime->graphicsQueue()) == VK_SUCCESS);
            } else {
                assert(native->waitSubmittedFenceUntil(
                    std::chrono::steady_clock::now() + std::chrono::seconds(5), diagnostic));
            }
            retireCompletedVulkanRhiSubmittedCommands(*runtime);
            assert(observedQueryReads == reads + 2u
                && payload->completedTimingSample(*runtime, 800u + mode).availability
                    == GpuTimingAvailability::Available);
            if (gate)
                assert(retained.expired());
        } else if (mode == 2u || mode == 3u) {
            auto gate = mode == 3u
                ? VulkanRhiTimelineSemaphorePayload::create(runtime, 1u, diagnostic)
                : nullptr;
            const auto submits = queueSubmitCalls;
            failSubmit = true;
            const auto result = submitVulkanRhiCommandBuffer(*finished,
                gate ? VulkanRhiTimelineSubmitInfo{gate, 1u} : VulkanRhiTimelineSubmitInfo{});
            assert(!result.success && !result.workAccepted && queueSubmitCalls == submits + 1u
                && !vulkanRhiBufferStateCommitted(*buffer)
                && !vulkanRhiBufferStateAvailable(*buffer, nullptr)
                && payload->observeSlot(0u)->phase() == Phase::Quarantined
                && payload->observeSlot(1u)->phase() == Phase::Quarantined
                && observedQueryReads == reads && !pool->reset().success);
            std::weak_ptr<VulkanRhiNativeCommandPayload> retained = native;
            pass.reset();
            encoder.reset();
            finished.reset();
            native.reset();
            retireCompletedVulkanRhiSubmittedCommands(*runtime);
            assert(!retained.expired() && observedQueryReads == reads);
            assert(!waitIdleAndRetireVulkanRhiSubmittedCommands(runtime) && retained.expired()
                && !vulkanRhiBufferStateAvailable(*buffer, nullptr)
                && payload->observeSlot(0u)->phase() == Phase::FailedIdle
                && payload->observeSlot(1u)->phase() == Phase::FailedIdle
                && observedQueryReads == reads);
        }
        pass.reset();
        encoder.reset();
        finished.reset();
        native.reset();
        if (!mode)
            assert(vulkanRhiBufferStateCommitted(*buffer)
                && buffer->state() == ResourceState::Undefined
                && payload->observeSlot(0u)->phase() == Phase::NotRecorded
                && payload->observeSlot(1u)->phase() == Phase::NotRecorded
                && observedQueryReads == reads);
        assert(pool->reset().success);
    }
    observedQueryPool = VK_NULL_HANDLE;
    if (verifySubmitRollback) {
        std::puts("original mixed query Submit rollback: real finite retirement capacity race, "
                  "no main queue invocation, original Record custody preserved, real copy "
                  "completion and same-command retry/readback PASS");
        return;
    }
    std::puts(
        "original mixed buffer/query ordinary journal: same Inputs9/journal, hot allocation0, "
        "cancel rollback/accepted commit/delayed completion/invoked-error invalidation "
        "and query quarantine until real idle PASS; public stream gate closed");
}

void originalQuerySourceNative(const std::shared_ptr<VulkanRhiRuntime>& runtime,
    bool verifyColdStorage = false,
    bool verifyJournal = false,
    bool verifyMixedJournal = false,
    bool verifySubmitRollback = false,
    bool verifyProfiling = false,
    bool verifyScopeCollision = false)
{
    using Sources = OriginalCommandResourceSources;
    using Source = OriginalCommandQueryPoolSource;
    using Inputs = OriginalCommandInputs;
    using A = CommandScalarArguments;
    using K = CommandUsageKind;
    using Stream = CommandVariableArguments;
    OriginalQueryCreationFixture f;
    if (verifyScopeCollision) {
        f.ids[0] = CommandProfilingContract::wholeCommandScopeId;
        f.input = f.snapshot(f.desc);
        assert(f.input);
    }
    std::array<std::shared_ptr<core::CpuMemoryQuota>, 4u> q;
    for (auto& quota : q)
        quota = core::makeShared<core::CpuMemoryQuota>(kMetadataBytes);
    VulkanRhiDeviceDesc dd;
    dd.runtime = runtime;
    dd.capabilityContract.backend = Backend::Vulkan;
    dd.memoryBudgetBytes = kMetadataBytes;
    auto device = createVulkanRhiDevice(dd);
    assert(
        device && !device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, kMetadataBytes}, q[0]));
    OriginalResourceRegistrationBudget registration{1u, true, 0u, 0u, 0u, 5u, kMetadataBytes};
    if (verifyMixedJournal || verifySubmitRollback) {
        registration.maxBuffers = 1u;
        registration.maxRetainedOwners = 7u;
    }
    registration.queries = {1u, true, 1u};
    assert(!device->prepareOriginalResourceRegistration(registration, q[1]));
    const auto live = queryLivePools;
    const auto create = [&] {
        return device->createOriginalQueryPool(f.input, f.budget, f.quotas[3], f.diagnostic);
    };
    // Registry storage and native-owner storage are independent cold domains.
    const auto registryBaseline = q[1]->liveBytes();
    for (auto quota : {q[1], f.quotas[3]}) {
        const auto creates = queryCreateCalls;
        core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
        assert(pressure.charged() && !create() && f.diagnostic && queryCreateCalls == creates
            && queryLivePools == live);
    }
    failQueryCreation = true;
    assert(!create() && f.diagnostic && q[1]->liveBytes() == registryBaseline
        && !f.quotas[3]->liveBytes() && queryLivePools == live);
    failQueryCreation = false;
    std::uint64_t registrationBytes = 0u;
    {
        auto measured = create();
        assert(measured);
        registrationBytes = q[1]->liveBytes() - registryBaseline;
        assert(registrationBytes);
    }
    for (const auto available : {registrationBytes - 1u, registrationBytes}) {
        const auto creates = queryCreateCalls;
        core::CpuMemoryQuotaLease pressure(q[1], kMetadataBytes - registryBaseline - available);
        auto candidate = create();
        assert(bool(candidate) == (available == registrationBytes));
        if (!candidate)
            assert(f.diagnostic && queryCreateCalls == creates);
    }
    const auto ownerBytes = test::CpuQuotaFactoryFixture(f.quotas[3]).measureRetained(create);
    for (const auto available : {ownerBytes - 1u, ownerBytes}) {
        const auto creates = queryCreateCalls;
        core::CpuMemoryQuotaLease pressure(f.quotas[3], kMetadataBytes - available);
        auto candidate = create();
        assert(bool(candidate) == (available == ownerBytes));
        if (!candidate)
            assert(f.diagnostic && queryCreateCalls == creates);
        else
            assert(!device->validateOriginalResourceRegistration(*candidate));
    }
    auto pool = create();
    assert(pool && !f.diagnostic && !device->validateOriginalResourceRegistration(*pool));
    const auto creates = queryCreateCalls;
    assert(!create() && f.diagnostic && queryCreateCalls == creates);
    const auto ticket = pool->originalResourceRegistration();
    OriginalCommandQueryPoolSourceBudget sb{1u, true, ticket.generation(), kMetadataBytes, 3u};
    const auto sourceFactory = [&] {
        return Source::prepareOriginal(pool, sb, q[2], *device, f.diagnostic);
    };
    const auto sourceBytes = test::CpuQuotaFactoryFixture(q[2]).measureRetained(sourceFactory);
    for (auto available : {sourceBytes - 1u, sourceBytes}) {
        core::CpuMemoryQuotaLease pressure(q[2], kMetadataBytes - available);
        assert(bool(sourceFactory()) == (available == sourceBytes));
    }
    --sb.maxRetainedOwners;
    assert(!sourceFactory() && f.diagnostic);
    ++sb.maxRetainedOwners;
    assert(!Source::prepareOriginal(pool, sb, f.quotas[3], *device, f.diagnostic));
    --sb.registrationGeneration;
    assert(!sourceFactory() && f.diagnostic);
    ++sb.registrationGeneration;
    auto originalPool = pool;
    pool = std::shared_ptr<QueryPool>(q[3], originalPool.get());
    assert(!sourceFactory()
        && std::string_view(f.diagnostic) == "RHI-ORIGINAL-QUERY-SOURCE-WRAPPER-OWNER");
    pool = originalPool;
    originalPool.reset();
    auto source = sourceFactory();
    assert(source && !Source::validateOriginal(source, *device));
    auto forgedSource = std::shared_ptr<const Source>(q[3], source.get());
    assert(Source::validateOriginal(forgedSource));
    forgedSource.reset();
    {
        auto ordinary = createVulkanRhiQueryPool(runtime, f.desc);
        assert(ordinary && device->validateOriginalResourceRegistration(*ordinary));
        auto borrowed = std::shared_ptr<const QueryPool>(q[3], ordinary.get());
        assert(!Source::prepareOriginal(borrowed, sb, q[2], *device, f.diagnostic));
    }
    auto peer = createVulkanRhiDevice(dd);
    assert(peer && !peer->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, kMetadataBytes}, q[3]));
    assert(Source::validateOriginal(source, *peer)); // Same runtime is not the same issuer.
    if (verifyMixedJournal || verifySubmitRollback) {
        originalMixedQueryJournalNative(runtime, *device, pool, source, f, q, verifySubmitRollback);
        return;
    }
    OriginalCommandAdmissionBudget ab;
    ab.contractVersion = 1u;
    ab.prototypeOptIn = true;
    ab.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::Query)] = 1u;
    ab.maxCommands = 5u;
    ab.maxReferences = ab.maxInputDescriptors = 2u;
    ab.maxGraphOwners = 9u;
    ab.maxCpuBytes[static_cast<std::size_t>(OriginalCommandAdmissionBudget::CpuDomain::Source)] =
        kMetadataBytes;
    ab.maxCpuBytes[static_cast<std::size_t>(OriginalCommandAdmissionBudget::CpuDomain::Graph)] =
        kMetadataBytes;
    ab.maxCpuBytes[static_cast<std::size_t>(OriginalCommandAdmissionBudget::CpuDomain::Recording)] =
        kMetadataBytes;
    auto sources =
        Sources::prepareColdOriginalTyped(nullptr, 0u, ab, {7u, true, 1u}, q[2], f.diagnostic);
    assert(sources && !Sources::appendOriginalTyped(sources, {source}, *device));
    assert(Sources::appendOriginalTyped(sources, {source}, *device));
    const std::array<CommandUsage, 5u> commands{{{K::BeginComputePass, 0u, 0u},
        {K::Timestamp, 0u, 1u},
        {K::Timestamp, 1u, 1u},
        {K::EndComputePass, 2u, 0u},
        {K::Finish, 2u, 0u}}};
    const std::array<OriginalCommandUsage::Reference, 2u> refs{{{0u}, {0u}}};
    auto usage = OriginalCommandUsage::prepareOriginal(sources,
        commands.data(),
        commands.size(),
        refs.data(),
        refs.size(),
        f.quotas[0],
        f.diagnostic);
    assert(usage && usage->retainedOwnerCount() == 5u);
    std::uint32_t sourceOwners = 0u;
    assert(!sources->cpuOwnerCount(2u, 5u, sourceOwners) && sourceOwners == 5u
        && sources->cpuOwnerCount(2u, 4u, sourceOwners));
    const auto none = [](K k) {
        return *A::capture(k, A::None{});
    };
    const std::array<Stream::Input, 5u> args{{{none(K::BeginComputePass)},
        {*A::capture(K::Timestamp, A::Query{0u})},
        {*A::capture(K::Timestamp, A::Query{1u})},
        {none(K::EndComputePass)},
        {none(K::Finish)}}};
    const CommandVariableArgumentsBudget vb{1u, true, 5u, 0u, 0u, 0u, kMetadataBytes};
    auto stream = Stream::prepareOriginal(args.data(), args.size(), vb, f.quotas[0], f.diagnostic);
    auto scalars =
        OriginalCommandScalars::prepareOriginal(usage, stream, f.quotas[0], f.diagnostic);
    assert(scalars && scalars->retainedOwnerCount() == 7u);
    const auto begin = *QueryPoolOriginalInputArguments::captureOriginal(K::Timestamp, 0u, f.input);
    const auto end = *QueryPoolOriginalInputArguments::captureOriginal(K::Timestamp, 1u, f.input);
    const std::array<QueryCommandPoints::Input, 2u> pointInputs{{{1u, begin}, {2u, end}}};
    const QueryCommandPointsBudget pb{
        1u, true, 5u, 1u, 2u, 0u, 0u, 0u, 0u, 4u, 2u, f.desc.label.size() * 2u, kMetadataBytes};
    auto points = QueryCommandPoints::prepareOriginal(
        stream, pointInputs.data(), pointInputs.size(), pb, f.quotas[0], f.diagnostic);
    assert(points);
    OriginalCommandInputsBudget ib;
    ib.contractVersion = 9u;
    ib.prototypeOptIn = true;
    ib.maxQueryBindings = 2u;
    ib.queryPoints = points;
    std::array<Inputs::OriginalDescriptor, 2u> bindings{
        {{1u, OriginalCommandQueryBinding{0u, ticket.generation(), begin}},
            {2u, OriginalCommandQueryBinding{0u, ticket.generation(), end}}}};
    const auto inputsFactory = [&] {
        return Inputs::prepareOriginal(
            scalars, bindings.data(), bindings.size(), ib, f.quotas[0], f.diagnostic);
    };
    auto inputs = inputsFactory();
    if (!inputs)
        std::fprintf(stderr, "query inputs: %s\n", f.diagnostic);
    assert(inputs && inputs->retainedOwnerCount() == 9u);
    const std::array<Inputs::IncomingReference, 1u> incomingReferences{{pool.get()}};
    Inputs::Incoming incoming{args[1], begin, incomingReferences.data(), incomingReferences.size()};
    assert(!inputs->compareOriginalInput(1u, incoming));
    incoming.descriptor = end;
    assert(inputs->compareOriginalInput(1u, incoming));
    assert(!Inputs::prepareOriginal(scalars, bindings.data(), 1u, ib, f.quotas[0], f.diagnostic));
    std::uint32_t owners = 0u, registrations = 0u;
    assert(!Sources::registeredOwnerUnion(
               sources, inputs->retainedOwnerCount(), 13u, 1u, owners, registrations)
        && owners == 13u && registrations == 1u);
    const auto reject = [&] {
        assert(!inputsFactory() && f.diagnostic);
    };
    --std::get<OriginalCommandQueryBinding>(bindings[0].arguments).registrationGeneration;
    reject();
    ++std::get<OriginalCommandQueryBinding>(bindings[0].arguments).registrationGeneration;
    std::get<OriginalCommandQueryBinding>(bindings[0].arguments).sourceIndex = 1u;
    reject();
    std::get<OriginalCommandQueryBinding>(bindings[0].arguments).sourceIndex = 0u;
    const std::array<Inputs::OriginalDescriptor, 2u> reordered{{bindings[1], bindings[0]}};
    assert(!Inputs::prepareOriginal(
        scalars, reordered.data(), reordered.size(), ib, f.quotas[0], f.diagnostic));
    bindings[1].commandIndex = 1u;
    reject();
    bindings[1].commandIndex = 2u;
    auto sameFields = f.snapshot(f.desc);
    const std::array<Inputs::OriginalDescriptor, 2u> wrongInput{
        {{1u,
             OriginalCommandQueryBinding{0u,
                 ticket.generation(),
                 *QueryPoolOriginalInputArguments::captureOriginal(K::Timestamp, 0u, sameFields)}},
            bindings[1]}};
    assert(!Inputs::prepareOriginal(
        scalars, wrongInput.data(), wrongInput.size(), ib, f.quotas[0], f.diagnostic));
    auto foreignStream =
        Stream::prepareOriginal(args.data(), args.size(), vb, f.quotas[0], f.diagnostic);
    ib.queryPoints = QueryCommandPoints::prepareOriginal(
        foreignStream, pointInputs.data(), pointInputs.size(), pb, f.quotas[0], f.diagnostic);
    assert(ib.queryPoints);
    reject();
    ib.queryPoints = points;
    ib.maxTableBindings = 1u;
    reject();
    ib.maxTableBindings = 0u;
    --ib.maxQueryBindings;
    reject();
    ++ib.maxQueryBindings;
    assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
        f.quotas, [&]() noexcept -> const char* {
            assert(!inputs->validate() && !usage->compareOriginalReference(0u, *pool)
                && !Source::validateOriginal(source, *device));
            for (const auto backend :
                {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU})
                assert(usage->executionDiagnostic(backend));
            assert(sources->typedStateJoinDiagnostic() && !source->nativeExecutionProven()
                && !inputs->nativeExecutionProven());
            return nullptr;
        }));
    // Prerequisite native replay uses the exact registered wrapper and frozen
    // point slots. The original stream executor and bounded recording stay shut.
    auto nativePool = std::dynamic_pointer_cast<VulkanRhiQueryPool>(pool);
    assert(nativePool);
    VulkanRhiNativeResourceAllocator resources(runtime);
    core::pmr::String diagnostic;
    diagnostic.reserve(512u);
    if (verifyJournal || verifyProfiling) {
        using Phase = VulkanRhiQueryPoolPayload::SlotControl::Phase;
        auto recordingQuota = core::makeShared<core::CpuMemoryQuota>(kMetadataBytes);
        VulkanRhiRecordingStorageLimits limits;
        limits.originalQueries = VulkanRhiRecordingStorageLimits::OriginalQueries{
            inputs, {1u, true, 0u, 1u, 2u, 0u, kMetadataBytes}, recordingQuota};
        auto& request = *limits.originalQueries;
        request.recordingContractVersion = 1u;
        request.recordingOptIn = true;
        request.commandBudget = {1u, true, commands.size()};
        for (const auto& argument : args)
            assert(!request.commandBudget.limits.appendArguments(argument));
        request.commandBudget.limits.resourceReferences = refs.size();
        auto capabilities = semantics::nativeBackendCapabilityContract(Backend::Vulkan);
        capabilities.queriedFromNativeDevice = !runtime->coreQueueCapabilityDiagnostic();
        capabilities.runtimeFeaturesQueried = true;
        capabilities.runtimeFeatures.clear();
        if (runtime->timestampQueriesEnabled()) {
            capabilities.features.push_back(semantics::NativeBackendFeature::TimestampQueries);
            capabilities.runtimeFeatures.push_back(
                semantics::NativeBackendFeature::TimestampQueries);
        }
        const auto makeFor = [&](const GpuDevice& issuer) {
            return createVulkanRhiCommandEncoderWithColdPassStorage(
                resources, {}, nullptr, capabilities, issuer, limits, diagnostic);
        };
        const auto make = [&] {
            return makeFor(*device);
        };
        if (verifyProfiling) {
            request.internalProfilingRequested = true;
            request.profilingBudget = CommandProfilingBudget{1u, true, 1u, 2u, 2u};
            if (verifyScopeCollision) {
                const auto creates = queryCreateCalls, writes = queryWriteCalls;
                assert(!make() && diagnostic == "RHI-COMMAND-PROFILING-SCOPE-IDENTITY"
                    && !recordingQuota->liveBytes() && queryCreateCalls == creates
                    && queryWriteCalls == writes);
                std::puts("internal profiling native admission: reserved whole-command scope "
                          "collision rejected before native creation PASS");
                return;
            }
            const auto creates = queryCreateCalls;
            const auto commandsCreated = resources.stats().nativeCommandPoolCreateCount;
            for (auto field : {&CommandProfilingBudget::maxInternalPools,
                     &CommandProfilingBudget::maxInternalTimestampWrites,
                     &CommandProfilingBudget::maxPublishedScopes}) {
                --((*request.profilingBudget).*field);
                assert(!make() && !diagnostic.empty());
                ++((*request.profilingBudget).*field);
            }
            request.profilingBudget->requireNativeAllocationFootprint = true;
            assert(!make() && diagnostic == "RHI-COMMAND-PROFILING-NATIVE-FOOTPRINT-UNAVAILABLE");
            request.profilingBudget->requireNativeAllocationFootprint = false;
            assert(queryCreateCalls == creates
                && resources.stats().nativeCommandPoolCreateCount == commandsCreated);
            for (const char* api : {"vkCreateQueryPool",
                     "vkDestroyQueryPool",
                     "vkResetQueryPool",
                     "vkCmdWriteTimestamp",
                     "vkGetQueryPoolResults"}) {
                missingQueryProcedure = api;
                assert(!make() && !diagnostic.empty() && queryCreateCalls == creates
                    && !recordingQuota->liveBytes());
                missingQueryProcedure = nullptr;
            }
            const auto pools = queryLivePools;
            failQueryCreation = true;
            assert(!make() && diagnostic == "RHI-COMMAND-PROFILING-CREATE-FAILED"
                && queryLivePools == pools && !recordingQuota->liveBytes());
            failQueryCreation = false;
            loseQueryResetAfterCreation = true;
            assert(!make() && diagnostic == "RHI-VULKAN-TIMESTAMP-ENTRYPOINTS"
                && queryLivePools == pools && !recordingQuota->liveBytes());
            loseQueryResetAfterCreation = false;
            missingQueryProcedure = nullptr;
        }
        const auto nativeCreates = resources.stats().nativeCommandPoolCreateCount;
        const auto writesBefore = queryWriteCalls;
        assert(!makeFor(*peer) && !diagnostic.empty());
        request.recordingOptIn = false;
        assert(!make() && diagnostic == "RHI-ORIGINAL-QUERY-RECORDING-OPT-IN");
        request.recordingOptIn = true;
        request.recordingContractVersion = 2u;
        assert(!make() && diagnostic == "RHI-ORIGINAL-QUERY-RECORDING-VERSION");
        request.recordingContractVersion = 1u;
        --request.commandBudget.limits.commands[static_cast<std::size_t>(K::Timestamp)];
        assert(!make() && !diagnostic.empty());
        ++request.commandBudget.limits.commands[static_cast<std::size_t>(K::Timestamp)];
        --request.budget.maxTimestampWrites;
        assert(!make() && diagnostic == "RHI-ORIGINAL-QUERY-STORAGE-WRITE-CAPACITY");
        ++request.budget.maxTimestampWrites;
        capabilities.runtimeFeatures.clear();
        assert(!make() && diagnostic == "RHI-VULKAN-TIMESTAMP-QUERY-LOWERING-UNAVAILABLE");
        capabilities.runtimeFeatures.push_back(semantics::NativeBackendFeature::TimestampQueries);
        assert(resources.stats().nativeCommandPoolCreateCount == nativeCreates
            && queryWriteCalls == writesBefore && !recordingQuota->liveBytes());
        test::CpuQuotaFactoryFixture quotaProbe(recordingQuota);
        const auto coldBytes = quotaProbe.measure(make);
        quotaProbe.verify(coldBytes, make);
        {
            const auto createsBefore = resources.stats().nativeCommandPoolCreateCount;
            core::CpuMemoryQuotaLease full(recordingQuota, kMetadataBytes);
            assert(full.charged() && !make() && recordingQuota->liveBytes() == kMetadataBytes
                && resources.stats().nativeCommandPoolCreateCount == createsBefore);
        }
        const auto payload = nativePool->payload();
        auto foreignPool = createVulkanRhiQueryPool(runtime, f.desc);
        assert(foreignPool);
        observedQueryPool = payload->nativeHandle();
        observedQueryReads = 0u;
        // Each attempt uses the ordinary encoder and its one original journal.
        // The exhausted domains cover hot recording, finish and successful submit.
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 9u> hotQuotas{{f.quotas[0],
            f.quotas[1],
            f.quotas[2],
            f.quotas[3],
            q[0],
            q[1],
            q[2],
            q[3],
            recordingQuota}};
        std::atomic<bool> readTimings{verifyProfiling}, readerReady{false};
        std::thread timingReader;
        if (verifyProfiling) {
            timingReader = std::thread([&] {
                do {
                    const auto sample = latestVulkanRhiGpuTimingSample(*runtime);
                    if (sample.availability != GpuTimingAvailability::Pending) {
                        const auto serial = sample.completedFrameSerial;
                        if (serial == 711u || serial == 712u || serial == 713u) {
                            assert(sample.availability == GpuTimingAvailability::DiagnosticFailure
                                && sample.scopeCount == 0u && sample.diagnostic);
                        } else {
                            assert((serial == 704u || serial == 705u || serial == 716u)
                                && sample.availability == GpuTimingAvailability::Available
                                && sample.scopeCount == 2u && sample.scopes[0].scopeId == 0u
                                && sample.scopes[1].scopeId == f.ids[0]);
                        }
                    }
                    readerReady.store(true, std::memory_order_release);
                } while (readTimings.load(std::memory_order_acquire));
            });
            while (!readerReady.load(std::memory_order_acquire))
                std::this_thread::yield();
        }
        for (unsigned mode = 0u; mode < (verifyProfiling ? 17u : 11u); ++mode) {
            auto encoder = make();
            if (!encoder)
                std::fprintf(stderr, "query journal encoder: %s\n", diagnostic.c_str());
            assert(encoder);
            auto native = vulkanRhiCommandPayload(*encoder);
            native->observeFrameSerial(mode == 16u ? 0u : 700u + mode);
            core::UniquePtr<ComputePassEncoder> pass;
            core::UniquePtr<CommandBuffer> finished;
            const auto writes = queryWriteCalls;
            const auto reads = observedQueryReads;
            const auto submits = queueSubmitCalls;
            auto first = payload->observeSlot(0u);
            auto second = payload->observeSlot(1u);
            assert(first && second);
            core::MutationJournal externalJournal;
            if (mode == 10u)
                assert(payload->markRecorded(1u, diagnostic, &externalJournal));
            const auto externalSlot =
                mode == 10u ? payload->observeSlot(1u, &externalJournal) : std::nullopt;
            if (mode == 2u)
                assert(pool->reset().success); // Captured epoch is stale.
            if (mode == 3u)
                missingQueryProcedure = "vkCmdWriteTimestamp";
            assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
                hotQuotas, [&]() noexcept -> const char* {
                    core::CpuMemoryQuotaScope scope(recordingQuota);
                    pass = encoder->beginComputePass({});
                    if (mode == 2u || mode == 10u) {
                        assert(!pass && !encoder->finish() && queryWriteCalls == writes
                            && native->queryRecordingStorage()->timestamps().empty()
                            && native->queryRecordingStorage()->queryPoolOwnerCount() == 0u);
                        return nullptr;
                    }
                    assert(pass);
                    const auto recorded = pass->writeTimestamp(
                        mode == 9u ? *foreignPool : *pool, mode == 1u ? 1u : 0u);
                    if ((mode >= 1u && mode <= 3u) || mode == 9u) {
                        assert(!recorded.success && !encoder->finish() && queryWriteCalls == writes
                            && native->queryRecordingStorage()->timestamps().empty()
                            && native->queryRecordingStorage()->queryPoolOwnerCount() == 0u);
                        if (mode != 2u)
                            assert(payload->matchesSlot(*first) && payload->matchesSlot(*second));
                        return nullptr;
                    }
                    assert(recorded.success && pass->writeTimestamp(*pool, 1u).success
                        && pass->end().success);
                    if (mode == 14u || mode == 15u) {
                        if (mode == 14u)
                            missingQueryProcedure = "vkCmdWriteTimestamp";
                        else
                            missingCommandEnd = true;
                        assert(!encoder->finish() && queryWriteCalls == writes + 2u);
                        return nullptr;
                    }
                    finished = encoder->finish();
                    assert(finished && finished->commandCount() == 4u
                        && vulkanRhiCommandPayload(*finished) == native
                        && native->queryRecordingStorage()->timestamps().size() == 2u
                        && native->queryRecordingStorage()->queryPoolOwnerCount() == 1u
                        && !payload->observeSlot(0u) && !payload->observeSlot(1u));
                    if (mode == 4u) {
                        const auto result = submitVulkanRhiCommandBuffer(*finished);
                        assert(result.success && result.workAccepted
                            && queueSubmitCalls == submits + 1u);
                    }
                    return nullptr;
                }));
            missingQueryProcedure = nullptr;
            missingCommandEnd = false;
            if (mode == 14u || mode == 15u) {
                pass.reset();
                encoder.reset();
                native.reset();
                assert(payload->observeSlot(0u)->phase() == Phase::NotRecorded
                    && payload->observeSlot(1u)->phase() == Phase::NotRecorded
                    && observedQueryReads == reads && queueSubmitCalls == submits);
                assert(pool->reset().success);
                continue;
            }
            if ((mode >= 1u && mode <= 3u) || mode == 9u || mode == 10u) {
                pass.reset();
                encoder.reset();
                native.reset();
                if (mode == 10u) {
                    assert(externalSlot && payload->matchesSlot(*externalSlot, &externalJournal)
                        && payload->matchesSlot(*first));
                    externalJournal.rollback();
                }
                assert(pool->reset().success);
                continue;
            }
            if (!mode) {
                assert(queryWriteCalls == writes + (verifyProfiling ? 3u : 2u)
                    && observedQueryReads == reads);
                pass.reset();
                encoder.reset();
                finished.reset();
                native.reset();
                assert(payload->observeSlot(0u)->phase() == Phase::NotRecorded
                    && payload->observeSlot(1u)->phase() == Phase::NotRecorded);
            } else if (mode == 4u) {
                if (verifyProfiling) {
                    assert(vkQueueWaitIdle(runtime->graphicsQueue()) == VK_SUCCESS);
                    assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
                        hotQuotas, [&]() noexcept -> const char* {
                            core::CpuMemoryQuotaScope scope(recordingQuota);
                            assert(native->submittedFenceCompleted());
                            const auto published = latestVulkanRhiGpuTimingSample(*runtime);
                            assert(!native->internalTimingDiagnostic()
                                && published.availability == GpuTimingAvailability::Available
                                && published.completedFrameSerial == 704u
                                && published.scopeCount == 2u && published.scopes[0].scopeId == 0u
                                && published.scopes[1].scopeId == f.ids[0]);
                            return nullptr;
                        }));
                } else {
                    assert(native->waitSubmittedFenceUntil(
                        std::chrono::steady_clock::now() + std::chrono::seconds(5), diagnostic));
                }
                retireCompletedVulkanRhiSubmittedCommands(*runtime);
                assert(acceptRecordedCommandOwnership(
                    *finished)); // Alias remains alive after completion.
                assert(observedQueryReads == reads + 2u);
                const auto sample = payload->completedTimingSample(*runtime, 704u);
                assert(sample.availability == GpuTimingAvailability::Available
                    && sample.scopeCount == 1u && sample.scopes[0].scopeId == f.ids[0]);
            } else if (mode == 5u) {
                auto gate = VulkanRhiTimelineSemaphorePayload::create(runtime, 0u, diagnostic);
                assert(gate && submitVulkanRhiCommandBuffer(*finished, {gate, 1u}).workAccepted
                    && !native->submittedFenceCompleted() && !pool->reset().success
                    && observedQueryReads == reads);
                std::weak_ptr<VulkanRhiNativeCommandPayload> retained = native;
                pass.reset();
                encoder.reset();
                finished.reset();
                native.reset();
                assert(!retained.expired() && gate->hostSignal(1u) == VK_SUCCESS
                    && vkQueueWaitIdle(runtime->graphicsQueue()) == VK_SUCCESS);
                retireCompletedVulkanRhiSubmittedCommands(*runtime);
                assert(retained.expired() && observedQueryReads == reads + 2u);
            } else if (mode == 6u || mode == 7u) {
                auto gate = mode == 7u
                    ? VulkanRhiTimelineSemaphorePayload::create(runtime, 1u, diagnostic)
                    : nullptr;
                failSubmit = true;
                const auto result = submitVulkanRhiCommandBuffer(*finished,
                    gate ? VulkanRhiTimelineSubmitInfo{gate, 1u} : VulkanRhiTimelineSubmitInfo{});
                assert(!result.success && !result.workAccepted && queueSubmitCalls == submits + 1u
                    && payload->observeSlot(0u)->phase() == Phase::Quarantined
                    && payload->observeSlot(1u)->phase() == Phase::Quarantined
                    && observedQueryReads == reads && !pool->reset().success);
                std::weak_ptr<VulkanRhiNativeCommandPayload> retained = native;
                pass.reset();
                encoder.reset();
                finished.reset();
                native.reset();
                retireCompletedVulkanRhiSubmittedCommands(*runtime);
                assert(!retained.expired() && observedQueryReads == reads);
                assert(!waitIdleAndRetireVulkanRhiSubmittedCommands(runtime) && retained.expired()
                    && observedQueryReads == reads
                    && payload->observeSlot(0u)->phase() == Phase::FailedIdle
                    && payload->observeSlot(1u)->phase() == Phase::FailedIdle);
            } else if (mode == 16u) {
                native->observeFrameSerial(0u);
                const auto result = submitVulkanRhiCommandBuffer(*finished);
                assert(!result.success && !result.workAccepted
                    && result.diagnostics == "RHI-COMMAND-PROFILING-FRAME-SERIAL"
                    && queueSubmitCalls == submits && observedQueryReads == reads
                    && !payload->observeSlot(0u) && !payload->observeSlot(1u));
                native->observeFrameSerial(716u);
                assert(submitVulkanRhiCommandBuffer(*finished).workAccepted
                    && native->waitSubmittedFenceUntil(
                        std::chrono::steady_clock::now() + std::chrono::seconds(5), diagnostic));
                retireCompletedVulkanRhiSubmittedCommands(*runtime);
                const auto published = latestVulkanRhiGpuTimingSample(*runtime);
                assert(published.availability == GpuTimingAvailability::Available
                    && published.completedFrameSerial == 716u && published.scopeCount == 2u);
            } else if (mode >= 11u) {
                assert(submitVulkanRhiCommandBuffer(*finished).workAccepted
                    && vkQueueWaitIdle(runtime->graphicsQueue()) == VK_SUCCESS);
                refuseObservedQueryResult = mode == 11u;
                refuseInternalQueryResult = mode == 12u;
                if (mode == 13u)
                    missingQueryProcedure = "vkGetQueryPoolResults";
                assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    hotQuotas, [&]() noexcept -> const char* {
                        core::CpuMemoryQuotaScope scope(recordingQuota);
                        assert(native->submittedFenceCompleted());
                        const auto published = latestVulkanRhiGpuTimingSample(*runtime);
                        assert(native->internalTimingDiagnostic()
                            && published.availability == GpuTimingAvailability::DiagnosticFailure
                            && published.completedFrameSerial == 700u + mode
                            && published.scopeCount == 0u);
                        return nullptr;
                    }));
                missingQueryProcedure = nullptr;
                refuseObservedQueryResult = refuseInternalQueryResult = false;
                retireCompletedVulkanRhiSubmittedCommands(*runtime);
                const auto published = latestVulkanRhiGpuTimingSample(*runtime);
                assert(published.completedFrameSerial == 700u + mode
                    && published.availability == GpuTimingAvailability::DiagnosticFailure);
            } else {
                // Premature external journal acceptance cannot silently re-enlist
                // this recording's slots on submit or mutate the earlier slot.
                assert(acceptRecordedCommandOwnership(*finished));
                const auto committedFirst = payload->observeSlot(0u);
                const auto committedSecond = payload->observeSlot(1u);
                assert(committedFirst && committedSecond);
                assert(!submitVulkanRhiCommandBuffer(*finished).success
                    && queueSubmitCalls == submits && observedQueryReads == reads
                    && payload->matchesSlot(*committedFirst)
                    && payload->matchesSlot(*committedSecond));
            }
            const auto beforeRelease = latestVulkanRhiGpuTimingSample(*runtime);
            pass.reset();
            encoder.reset();
            finished.reset();
            native.reset();
            if (verifyProfiling && (mode == 4u || mode == 5u || mode >= 11u)) {
                const auto afterRelease = latestVulkanRhiGpuTimingSample(*runtime);
                assert(afterRelease.completedFrameSerial == beforeRelease.completedFrameSerial
                    && afterRelease.availability == beforeRelease.availability
                    && afterRelease.scopeCount == beforeRelease.scopeCount
                    && afterRelease.diagnostic == beforeRelease.diagnostic);
                for (std::uint32_t i = 0u; i < beforeRelease.scopeCount; ++i)
                    assert(afterRelease.scopes[i].scopeId == beforeRelease.scopes[i].scopeId
                        && afterRelease.scopes[i].elapsedMs == beforeRelease.scopes[i].elapsedMs);
            }
            if (mode == 8u) {
                // The explicit external acceptance owns its committed Record
                // declarations even though native submission was refused.
                assert(!pool->reset().success && payload->rollbackRecorded(0u)
                    && payload->rollbackRecorded(1u));
            }
            assert(pool->reset().success);
        }
        if (verifyProfiling) {
            readTimings.store(false, std::memory_order_release);
            timingReader.join();
        }
        observedQueryPool = VK_NULL_HANDLE;
        std::printf(
            "original query ordinary journal: cold CPU=%llu exact/short/zero, issuer/lowering/"
            "ordinal/ABA/API refusal atomic, record/finish/submit allocation0, cancellation/"
            "delayed completion/alias lifetime/unproven idle PASS; public stream gate closed\n",
            static_cast<unsigned long long>(coldBytes));
        if (verifyProfiling)
            std::puts("internal profiling native: independent cold pool/write/publication "
                      "capacity, queried runtime/API and strict footprint refusal, native creation "
                      "cleanup, original journal cancellation/delayed submit, allocation0 "
                      "completion/publication PASS; native driver footprint remains unknown");
        return;
    }
    if (verifyColdStorage) {
        auto recordingQuota = core::makeShared<core::CpuMemoryQuota>(kMetadataBytes);
        VulkanRhiRecordingStorageLimits storageLimits;
        storageLimits.originalQueries = VulkanRhiRecordingStorageLimits::OriginalQueries{
            inputs, {1u, true, 0u, 1u, 2u, 0u, kMetadataBytes}, recordingQuota};
        auto& request = *storageLimits.originalQueries;
        const auto inspect = [&] {
            return OriginalQueryRecordingStorageContract::inspect(
                request.inputs, request.budget, request.recordingQuota);
        };
        assert(
            !inspect().diagnostic && inspect().poolOwners == 1u && inspect().timestampWrites == 2u);
        const auto make = [&] {
            return createVulkanRhiNativeCommandPayload(resources, diagnostic, {}, &storageLimits);
        };
        const auto nativeCreates = resources.stats().nativeCommandPoolCreateCount;
        const auto nativeBegins = resources.stats().nativeCommandBufferBeginCount;
        const auto queryCreates = queryCreateCalls;
        --request.budget.maxTimestampWrites;
        assert(!make() && diagnostic == "RHI-ORIGINAL-QUERY-STORAGE-WRITE-CAPACITY");
        ++request.budget.maxTimestampWrites;
        request.budget.maxQueryPools = request.budget.maxTimestampWrites = 0u;
        assert(!make() && diagnostic == "RHI-ORIGINAL-QUERY-STORAGE-POOL-CAPACITY");
        request.budget.maxQueryPools = 1u;
        request.budget.maxTimestampWrites = 2u;
        request.internalProfilingRequested = true;
        assert(!make() && diagnostic == "RHI-VULKAN-COLD-PROFILING-BUDGET-UNAVAILABLE");
        request.internalProfilingRequested = false;
        recordingQuota->setLimitBytes(kMetadataBytes - 1u);
        assert(!make() && diagnostic == "RHI-ORIGINAL-QUERY-STORAGE-RECORDING-QUOTA");
        recordingQuota->setLimitBytes(kMetadataBytes);
        for (auto quota : {f.quotas[0], f.quotas[1], f.quotas[2], f.quotas[3], q[0], q[1], q[2]}) {
            request.recordingQuota = quota;
            assert(!make() && !diagnostic.empty());
        }
        request.recordingQuota = recordingQuota;
        request.inputs = std::shared_ptr<const Inputs>(recordingQuota, inputs.get());
        assert(!make() && diagnostic == "RHI-ORIGINAL-INPUTS-OWNER");
        request.inputs = inputs;
        assert(resources.stats().nativeCommandPoolCreateCount == nativeCreates
            && resources.stats().nativeCommandBufferBeginCount == nativeBegins
            && queryCreateCalls == queryCreates && !recordingQuota->liveBytes());
        // Distinct immutable ordinals may still name the same original slot.
        // Preserve canonical Inputs9 construction; storage admission must reject.
        {
            auto duplicateArgs = args;
            duplicateArgs[2] = args[1];
            auto duplicateStream = Stream::prepareOriginal(
                duplicateArgs.data(), duplicateArgs.size(), vb, f.quotas[0], f.diagnostic);
            auto duplicateScalars = OriginalCommandScalars::prepareOriginal(
                usage, duplicateStream, f.quotas[0], f.diagnostic);
            const std::array<QueryCommandPoints::Input, 2u> duplicatePoints{
                {{1u, begin}, {2u, begin}}};
            auto duplicateBudget = ib;
            duplicateBudget.queryPoints = QueryCommandPoints::prepareOriginal(duplicateStream,
                duplicatePoints.data(),
                duplicatePoints.size(),
                pb,
                f.quotas[0],
                f.diagnostic);
            const std::array<Inputs::OriginalDescriptor, 2u> duplicateBindings{
                {{1u, OriginalCommandQueryBinding{0u, ticket.generation(), begin}},
                    {2u, OriginalCommandQueryBinding{0u, ticket.generation(), begin}}}};
            auto duplicateInputs = Inputs::prepareOriginal(duplicateScalars,
                duplicateBindings.data(),
                duplicateBindings.size(),
                duplicateBudget,
                f.quotas[0],
                f.diagnostic);
            assert(duplicateInputs && !f.diagnostic);
            request.inputs = duplicateInputs;
            assert(!make() && diagnostic == "RHI-ORIGINAL-QUERY-STORAGE-DUPLICATE-SLOT"
                && resources.stats().nativeCommandPoolCreateCount == nativeCreates
                && resources.stats().nativeCommandBufferBeginCount == nativeBegins
                && !recordingQuota->liveBytes());
            request.inputs = inputs;
        }
        const auto payload = nativePool->payload();
        // A later slot refusal must not change or cancel the earlier slot or
        // steal a different recording's pending declaration.
        for (const bool pending : {false, true}) {
            const auto first = payload->observeSlot(0u);
            core::MutationJournal journal;
            assert(first && payload->markRecorded(1u, diagnostic, pending ? &journal : nullptr));
            const auto second = payload->observeSlot(1u, pending ? &journal : nullptr);
            const auto creates = resources.stats().nativeCommandPoolCreateCount;
            const auto begins = resources.stats().nativeCommandBufferBeginCount;
            assert(second && !make()
                && diagnostic
                    == (pending ? "RHI-VULKAN-QUERY-SLOT-OBSERVATION"
                                : "RHI-VULKAN-QUERY-MUST-BE-RESET-FIRST")
                && payload->matchesSlot(*first)
                && payload->matchesSlot(*second, pending ? &journal : nullptr)
                && resources.stats().nativeCommandPoolCreateCount == creates
                && resources.stats().nativeCommandBufferBeginCount == begins
                && !recordingQuota->liveBytes());
            if (pending)
                journal.rollback();
            else
                assert(payload->rollbackRecorded(1u));
        }
        const auto coldBytes = test::CpuQuotaFactoryFixture(recordingQuota).measureRetained(make);
        for (const auto available : {coldBytes - 1u, std::uint64_t{0u}, coldBytes}) {
            const auto creates = resources.stats().nativeCommandPoolCreateCount;
            const auto begins = resources.stats().nativeCommandBufferBeginCount;
            {
                core::CpuMemoryQuotaLease pressure(recordingQuota, kMetadataBytes - available);
                assert(pressure.charged());
                auto cold = make();
                assert(bool(cold) == (available == coldBytes));
                if (!cold) {
                    assert(!diagnostic.empty()
                        && resources.stats().nativeCommandPoolCreateCount == creates
                        && resources.stats().nativeCommandBufferBeginCount == begins);
                    continue;
                }
                pressure.release();
                const auto* storage = cold->queryRecordingStorage();
                assert(storage && storage->budget().maxQueryPools == 1u
                    && storage->budget().maxTimestampWrites == 2u && !storage->queryPoolOwnerCount()
                    && storage->timestamps().empty());
                assert(std::string_view(cold->internalTimingDiagnostic())
                    == "RHI-VULKAN-COLD-INTERNAL-PROFILING-DISABLED");
                assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    std::array{recordingQuota, f.quotas[0], q[2]}, [&]() noexcept -> const char* {
                        assert(!inspect().diagnostic);
                        assert(!cold->originalQuerySlotDiagnostic());
                        assert(!cold->recordTimestamp(*nativePool, 0u, diagnostic)
                            && diagnostic == "RHI-VULKAN-COLD-QUERY-STORAGE-UNAVAILABLE");
                        assert(!storage->queryPoolOwnerCount() && storage->timestamps().empty());
                        return nullptr;
                    }));
                assert(nativePool->executionStatus().state == QueryExecutionState::NotRecorded);
                std::weak_ptr<const Inputs> originalInputs = inputs;
                request.inputs.reset();
                inputs.reset();
                assert(!originalInputs.expired());
                inputs = originalInputs.lock();
                request.inputs = inputs;
            }
            assert(!recordingQuota->liveBytes());
        }
        {
            auto cold = make();
            assert(cold && !cold->originalQuerySlotDiagnostic());
            const auto first = payload->observeSlot(0u);
            const auto second = payload->observeSlot(1u);
            const auto reads = observedQueryReads;
            assert(first && second && nativePool->reset().success && !payload->matchesSlot(*first)
                && !payload->matchesSlot(*second));
            const auto afterReset = payload->observeSlot(0u);
            assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{recordingQuota, f.quotas[3]}, [&]() noexcept -> const char* {
                    assert(std::string_view(cold->originalQuerySlotDiagnostic())
                        == "RHI-VULKAN-QUERY-SLOT-OBSERVATION");
                    assert(!cold->end(diagnostic)
                        && diagnostic == "RHI-VULKAN-QUERY-SLOT-OBSERVATION"
                        && payload->matchesSlot(*afterReset) && observedQueryReads == reads);
                    return nullptr;
                }));
        }
        assert(queryCreateCalls == queryCreates);
        // Destroying unwritten observations cannot cancel another command's
        // actual timestamp write, and cannot publish a timing sample.
        {
            auto cold = make();
            auto other = createVulkanRhiNativeCommandPayload(resources, diagnostic);
            assert(cold && other && other->recordTimestamp(*nativePool, 0u, diagnostic));
            const auto written = payload->observeSlot(0u);
            const auto reads = observedQueryReads;
            cold.reset();
            assert(written && payload->matchesSlot(*written) && observedQueryReads == reads);
            other.reset();
            assert(nativePool->executionStatus().state == QueryExecutionState::NotRecorded);
        }
        assert(!recordingQuota->liveBytes());
        std::printf("original query cold storage: Recording CPU=%llu; exact/one-short/zero, "
                    "one owner/two writes, duplicate/pending/reset ABA, unwritten cancellation, "
                    "independent quotas and disabled profiling PASS; "
                    "recording gate remains closed\n",
            static_cast<unsigned long long>(coldBytes));
    }
    auto command = createVulkanRhiNativeCommandPayload(resources, diagnostic);
    assert(command);
    for (const auto& point : points->points()) {
        assert(!nativePool->validateOriginalInput(point.original));
        assert(command->recordTimestamp(
            *nativePool, point.original.scalar().get<A::Query>()->index, diagnostic));
    }
    assert(command->end(diagnostic));
    command->observeFrameSerial(601u);
    auto gate = VulkanRhiTimelineSemaphorePayload::create(runtime, 0u, diagnostic);
    assert(gate && command->submit({gate, 1u}, diagnostic) && !command->submittedFenceCompleted());
    assert(nativePool->executionStatus().state == QueryExecutionState::Pending);
    assert(!nativePool->reset().success);
    std::weak_ptr<const QueryPool> retainedPool = pool;
    pool.reset();
    source.reset();
    nativePool.reset();
    assert(!retainedPool.expired() && !inputs->validate());
    assert(gate->hostSignal(1u) == VK_SUCCESS
        && vkQueueWaitIdle(runtime->graphicsQueue()) == VK_SUCCESS);
    nativePool = std::dynamic_pointer_cast<VulkanRhiQueryPool>(
        std::const_pointer_cast<QueryPool>(retainedPool.lock()));
    assert(nativePool && command->submittedFenceCompleted());
    const auto sample = nativePool->payload()->completedTimingSample(*runtime, 601u);
    assert(sample.availability == GpuTimingAvailability::Available && sample.scopeCount == 1u
        && sample.scopes[0].scopeId == f.ids[0] && nativePool->reset().success);
    std::printf(
        "original query source: native owner CPU=%llu source CPU=%llu registry CPU=%llu; one pool/two literal writes, "
        "registration/owner/ordinal/stream negatives and exhausted hot quotas PASS; execution gates closed\n",
        static_cast<unsigned long long>(ownerBytes),
        static_cast<unsigned long long>(sourceBytes),
        static_cast<unsigned long long>(registrationBytes));
}

void originalQueryCreationNative(const std::shared_ptr<VulkanRhiRuntime>& runtime)
{
    OriginalQueryCreationFixture f;
    const auto baseline = queryLivePools;
    const auto factory = [&] {
        return VulkanRhiQueryPool::prepareOriginal(
            runtime, f.input, f.budget, f.quotas[3], f.diagnostic);
    };
    const auto reject = [&](const char* expected) {
        const auto creates = queryCreateCalls;
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!factory() && f.diagnostic && std::string_view(f.diagnostic) == expected
            && !f.quotas[3]->liveBytes() && queryCreateCalls == creates
            && queryLivePools == baseline
            && core::defaultCpuAllocator().stats().allocationCount == allocations);
    };
    auto& b = f.budget;
    b.contractVersion = 0u;
    reject("RHI-VULKAN-ORIGINAL-QUERY-VERSION");
    b.contractVersion = 1u;
    b.prototypeOptIn = false;
    reject("RHI-VULKAN-ORIGINAL-QUERY-OPT-IN");
    b.prototypeOptIn = true;
    --b.maxNativeQueryPools;
    reject("RHI-VULKAN-ORIGINAL-QUERY-POOL-CAPACITY");
    ++b.maxNativeQueryPools;
    --b.maxQuerySlots;
    reject("RHI-VULKAN-ORIGINAL-QUERY-SLOT-CAPACITY");
    ++b.maxQuerySlots;
    --b.maxScopeIds;
    reject("RHI-VULKAN-ORIGINAL-QUERY-SCOPE-CAPACITY");
    ++b.maxScopeIds;
    --b.maxLabelBytes;
    reject("RHI-VULKAN-ORIGINAL-QUERY-LABEL-CAPACITY");
    ++b.maxLabelBytes;
    --b.maxCpuOwnerBytes;
    reject("RHI-VULKAN-ORIGINAL-QUERY-QUOTA");
    ++b.maxCpuOwnerBytes;
    auto originalInput = f.input;
    f.input = std::shared_ptr<const QueryPoolInputSnapshot>(f.quotas[0], originalInput.get());
    reject("RHI-QUERY-CREATION-INPUT");
    f.input = originalInput;
    auto invalidDesc = f.desc;
    invalidDesc.label[0] = '\0';
    f.input = f.snapshot(invalidDesc);
    assert(f.input);
    reject("RHI-QUERY-CREATION-LABEL");
    invalidDesc = f.desc;
    invalidDesc.count = 0u;
    f.input = f.snapshot(invalidDesc);
    assert(f.input);
    reject("RHI-QUERY-POOL-COUNT-MUST-BE-NONZERO");
    invalidDesc = {};
    invalidDesc.type = QueryType::PipelineStatistics;
    invalidDesc.count = 1u;
    invalidDesc.pipelineStatisticsMask = kAllPipelineStatistics;
    f.input = f.snapshot(invalidDesc);
    assert(f.input);
    reject("RHI-VULKAN-ORIGINAL-QUERY-STATISTICS-UNSUPPORTED");
    f.input = originalInput;
    assert(!VulkanRhiQueryPool::prepareOriginal(runtime, f.input, b, f.quotas[0], f.diagnostic)
        && std::string_view(f.diagnostic) == "RHI-VULKAN-ORIGINAL-QUERY-QUOTA-DOMAIN");
    for (const char* procedure : {"vkCreateQueryPool",
             "vkDestroyQueryPool",
             "vkResetQueryPool",
             "vkCmdWriteTimestamp",
             "vkGetQueryPoolResults"}) {
        missingQueryProcedure = procedure;
        reject("RHI-VULKAN-TIMESTAMP-ENTRYPOINTS");
        missingQueryProcedure = nullptr;
    }
    failQueryCreation = true;
    assert(!factory() && std::string_view(f.diagnostic) == "RHI-VULKAN-TIMESTAMP-CREATE-FAILED"
        && !f.quotas[3]->liveBytes() && queryLivePools == baseline);
    failQueryCreation = false;
    loseQueryResetAfterCreation = true;
    assert(!factory() && std::string_view(f.diagnostic) == "RHI-VULKAN-TIMESTAMP-ENTRYPOINTS"
        && !f.quotas[3]->liveBytes() && queryLivePools == baseline);
    loseQueryResetAfterCreation = false;
    missingQueryProcedure = nullptr;
    const auto peak = test::CpuQuotaFactoryFixture(f.quotas[3]).measureRetained(factory);
    {
        core::CpuMemoryQuotaLease pressure(f.quotas[3], kMetadataBytes - peak);
        auto exact = factory();
        assert(exact && !f.diagnostic && !VulkanRhiQueryPool::validateOriginal(exact, *runtime)
            && queryLivePools == baseline + 1u);
    }
    for (const auto available : {peak - 1u, std::uint64_t{0u}}) {
        const auto creates = queryCreateCalls;
        core::CpuMemoryQuotaLease pressure(f.quotas[3], kMetadataBytes - available);
        assert(!factory()
            && std::string_view(f.diagnostic) == "RHI-VULKAN-ORIGINAL-QUERY-CPU-ALLOCATION"
            && queryCreateCalls == creates && queryLivePools == baseline);
    }
    assert(!f.quotas[3]->liveBytes());
    auto owner = factory();
    assert(owner);
    auto forgedOwner = std::shared_ptr<const VulkanRhiQueryPool>(f.quotas[3], owner.get());
    assert(std::string_view(VulkanRhiQueryPool::validateOriginal(forgedOwner, *runtime))
        == "RHI-VULKAN-ORIGINAL-QUERY-OWNER");
    forgedOwner.reset();
    // Even an authenticated ordinary shared wrapper cannot acquire an original
    // creation decision by copying equal descriptor fields or a native payload.
    {
        core::CpuMemoryQuotaScope scope(f.quotas[3]);
        auto ordinary = core::QuotaBoundSharedObjectIdentity::shareOriginal(
            core::makeUnique<VulkanRhiQueryPool>(owner->payload(), f.desc));
        assert(ordinary && ordinary->hasQuotaBoundSharedOwner(ordinary)
            && std::string_view(VulkanRhiQueryPool::validateOriginal(ordinary, *runtime))
                == "RHI-VULKAN-ORIGINAL-QUERY-OWNER");
    }
    {
        tests::VulkanHeadlessRuntime other(false, resolve);
        assert(other.runtime()
            && std::string_view(VulkanRhiQueryPool::validateOriginal(owner, *other.runtime()))
                == "RHI-VULKAN-ORIGINAL-QUERY-RUNTIME");
    }
    auto sameFields = f.snapshot(f.desc);
    auto authentic =
        QueryPoolOriginalInputArguments::captureOriginal(CommandUsageKind::Timestamp, 0u, f.input);
    const auto copied = QueryPoolOriginalInputArguments::captureOriginal(
        CommandUsageKind::Timestamp, 0u, sameFields);
    assert(authentic && copied && !owner->validateOriginalInput(*authentic)
        && std::string_view(owner->validateOriginalInput(*copied))
            == "RHI-VULKAN-ORIGINAL-QUERY-INPUT");
    sameFields.reset();
    // Mutating the caller's source array/label cannot change the frozen input.
    f.ids[0] = 99u;
    f.desc.label = "changed";
    assert(owner->originalInput()->scopeIds()[0] == 0x7182u
        && owner->originalInput()->label() == "Timestamp.Original.原始"
        && !VulkanRhiQueryPool::validateOriginal(owner, *runtime));
    std::weak_ptr<const QueryPoolInputSnapshot> retainedInput = f.input;
    std::weak_ptr<VulkanRhiQueryPool> retainedWrapper = owner;
    std::weak_ptr<VulkanRhiQueryPoolPayload> retainedPayload = owner->payload();
    f.input.reset();
    originalInput.reset();
    authentic.reset();
    VulkanRhiNativeResourceAllocator resources(runtime);
    core::pmr::String diagnostic;
    diagnostic.reserve(512u);
    for (const bool dropCaller : {false, true}) {
        auto command = createVulkanRhiNativeCommandPayload(resources, diagnostic);
        assert(command && command->recordTimestamp(*owner, 0u, diagnostic)
            && command->recordTimestamp(*owner, 1u, diagnostic) && command->end(diagnostic));
        command->observeFrameSerial(dropCaller ? 502u : 501u);
        auto gate = VulkanRhiTimelineSemaphorePayload::create(runtime, 0u, diagnostic);
        assert(gate && command->submit({gate, 1u}, diagnostic)
            && !command->submittedFenceCompleted()
            && owner->executionStatus().state == QueryExecutionState::Pending);
        if (dropCaller) {
            owner.reset();
            command.reset();
            assert(retainedWrapper.expired() && !retainedPayload.expired()
                && !retainedInput.expired());
        }
        assert(gate->hostSignal(1u) == VK_SUCCESS);
        assert(vkQueueWaitIdle(runtime->graphicsQueue()) == VK_SUCCESS);
        assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
            f.quotas, [&]() noexcept -> const char* {
                core::CpuMemoryQuotaScope scope(f.quotas[3]);
                if (command)
                    assert(command->submittedFenceCompleted());
                else
                    retireCompletedVulkanRhiSubmittedCommands(*runtime);
                if (owner) {
                    assert(!VulkanRhiQueryPool::validateOriginal(owner, *runtime));
                    const auto sample = owner->payload()->completedTimingSample(*runtime, 501u);
                    assert(sample.availability == GpuTimingAvailability::Available
                        && sample.scopeCount == 1u && sample.scopes[0].scopeId == 0x7182u);
                    assert(owner->reset().success);
                }
                return nullptr;
            }));
        command.reset();
        retireCompletedVulkanRhiSubmittedCommands(*runtime);
    }
    assert(retainedWrapper.expired() && retainedPayload.expired() && retainedInput.expired()
        && queryLivePools == baseline);
    // The observers still own shared control blocks after object destruction.
    // Release them before requiring the original CPU quota to return to zero.
    retainedWrapper.reset();
    retainedPayload.reset();
    retainedInput.reset();
    assert(!f.quotas[3]->liveBytes());
    std::printf("original Vulkan query creation: cold CPU=%llu exact/short, "
                "authentic input/runtime/wrapper binding, delayed completion/reset/reuse, "
                "released wrapper and retained payload PASS; original execution still closed\n",
        static_cast<unsigned long long>(peak));
}
} // namespace

int main(int argc, char** argv)
{
    contract();
    coldPassContract();
    timestampContract();
    originalQueryCreationContract();
    originalQueryRegistrationContract();
    commandProfilingContract();
    texturePoolInitializationContract();
    if (argc == 2 && std::string_view(argv[1]) == "--contract")
        return 0;
    tests::VulkanHeadlessRuntime native(false, resolve);
    if (!native.runtime() || !native.runtime()->valid())
        return 77;
    tests::RenderDocCaptureScope capture;
    if (argc == 2 && std::string_view(argv[1]) == "--ordinary-texture-upload") {
        ordinaryTextureUpload(native.runtime());
        drainVulkanRhiSubmittedCommandsForShutdown(*native.runtime());
        capture.end();
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--public-texture-upload") {
        ordinaryTextureUpload<true>(native.runtime());
        drainVulkanRhiSubmittedCommandsForShutdown(*native.runtime());
        capture.end();
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--original-program-custody") {
        ordinaryOriginalProgramCustody(native.runtime());
        drainVulkanRhiSubmittedCommandsForShutdown(*native.runtime());
        capture.end();
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--staging-pool-registered") {
        originalTexturePoolInitialization<true>(native.runtime());
        drainVulkanRhiSubmittedCommandsForShutdown(*native.runtime());
        capture.end();
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--staging-pool-initialization") {
        originalTexturePoolInitialization(native.runtime());
        drainVulkanRhiSubmittedCommandsForShutdown(*native.runtime());
        capture.end();
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--staging-pool") {
        coldStagingPool(native.runtime());
        drainVulkanRhiSubmittedCommandsForShutdown(*native.runtime());
        capture.end();
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--staging-journal") {
        coldStagingJournal(native.runtime());
        drainVulkanRhiSubmittedCommandsForShutdown(*native.runtime());
        capture.end();
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--ordinary-debug-labels") {
        ordinaryUnadmittedDebugLabels(native.runtime());
        drainVulkanRhiSubmittedCommandsForShutdown(*native.runtime());
        capture.end();
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--query-slots") {
        if (!native.runtime()->timestampQueriesEnabled())
            return 77;
        querySlotsNative(native.runtime());
        drainVulkanRhiSubmittedCommandsForShutdown(*native.runtime());
        capture.end();
        return 0;
    }
    if (argc == 2
        && (std::string_view(argv[1]) == "--query-source"
            || std::string_view(argv[1]) == "--query-storage"
            || std::string_view(argv[1]) == "--query-journal"
            || std::string_view(argv[1]) == "--query-mixed-journal"
            || std::string_view(argv[1]) == "--query-submit-rollback"
            || std::string_view(argv[1]) == "--query-profiling")) {
        if (!native.runtime()->timestampQueriesEnabled())
            return 77;
        if (std::string_view(argv[1]) == "--query-profiling")
            originalQuerySourceNative(native.runtime(), false, false, false, false, true, true);
        originalQuerySourceNative(native.runtime(),
            std::string_view(argv[1]) == "--query-storage",
            std::string_view(argv[1]) == "--query-journal",
            std::string_view(argv[1]) == "--query-mixed-journal",
            std::string_view(argv[1]) == "--query-submit-rollback",
            std::string_view(argv[1]) == "--query-profiling");
        drainVulkanRhiSubmittedCommandsForShutdown(*native.runtime());
        capture.end();
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--query-reset") {
        if (!native.runtime()->timestampQueriesEnabled()) {
            std::fprintf(stderr, "%s\n", native.runtime()->timestampQueryDiagnostic());
            return 77;
        }
        timestampDisabled();
        timestampNative(native.runtime());
        drainVulkanRhiSubmittedCommandsForShutdown(*native.runtime());
        capture.end();
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--query-completion") {
        if (!native.runtime()->timestampQueriesEnabled())
            return 77;
        timestampCompletion(native.runtime());
        drainVulkanRhiSubmittedCommandsForShutdown(*native.runtime());
        capture.end();
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--query-original") {
        if (!native.runtime()->timestampQueriesEnabled())
            return 77;
        originalQueryCreationNative(native.runtime());
        drainVulkanRhiSubmittedCommandsForShutdown(*native.runtime());
        capture.end();
        return 0;
    }
    ordinaryFinishedStorage(native.runtime());
    if (argc == 2 && std::string_view(argv[1]) == "--ordinary-finish") {
        drainVulkanRhiSubmittedCommandsForShutdown(*native.runtime());
        capture.end();
        return 0;
    }
    ordinaryColdPassStorage(native.runtime());
    if (argc == 2 && std::string_view(argv[1]) == "--ordinary-passes") {
        drainVulkanRhiSubmittedCommandsForShutdown(*native.runtime());
        capture.end();
        return 0;
    }
    ordinaryRecordingStorage(native.runtime());
    nativeRecordingStorage(native.runtime());
    if (argc == 2 && std::string_view(argv[1]) == "--ordinary-storage") {
        drainVulkanRhiSubmittedCommandsForShutdown(*native.runtime());
        capture.end();
        return 0;
    }
    const bool onlyClear = argc == 2 && std::string_view(argv[1]) == "--clear";
    const bool onlyTexture = argc == 2 && std::string_view(argv[1]) == "--texture";
    for (const auto format : {TextureFormat::Rgba16Float, TextureFormat::D32Float}) {
        if (!onlyTexture) {
            clearPreparation(native.runtime(), format);
            textureCompletion(native.runtime(), format, false, false, true);
            textureCompletion(native.runtime(), format, true, false, true);
            textureCompletion(native.runtime(), format, false, true, true);
        }
        if (onlyClear)
            continue;
        textureRejection(native.runtime(), format);
        textureCompletion(native.runtime(), format, false, false);
        textureCompletion(native.runtime(), format, true, false);
        textureCompletion(native.runtime(), format, false, true);
    }
    if (onlyClear || (argc == 2 && std::string_view(argv[1]) == "--texture")) {
        drainVulkanRhiSubmittedCommandsForShutdown(*native.runtime());
        return 0;
    }
    recordingBudget(native.runtime());
    rejection(native.runtime());
    completion(native.runtime(), false);
    completion(native.runtime(), true);
    cancellationAndFailures(native.runtime());
    stateTransactions(native.runtime(), false);
    stateTransactions(native.runtime(), true);
    drainVulkanRhiSubmittedCommandsForShutdown(*native.runtime());
    capture.end();
    return 0;
}
