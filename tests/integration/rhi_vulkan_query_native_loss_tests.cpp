#include "rhi/backends/vulkan/vulkan_device.h"
#include "rhi/backends/vulkan/vulkan_query_pool.h"
#include "rhi/frontend/query_source.h"
#include "tests/fixtures/cpu_quota_factory_fixture.h"
#include "tests/fixtures/original_query_native_loss_fixture.h"
#include "tests/fixtures/vulkan_headless_runtime.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

#ifdef NDEBUG
#error Native query loss tests require assertions
#endif

namespace {
using namespace arp;
using namespace arp::rhi;
using namespace arp::rhi_vulkan;
using ContractFixture = test::OriginalQueryNativeLossFixture;
constexpr auto bytes = ContractFixture::bytes;
VkResult nextCreateResult = VK_SUCCESS;
unsigned creates = 0u, live = 0u, resets = 0u;

VKAPI_ATTR VkResult VKAPI_CALL createQuery(VkDevice device,
    const VkQueryPoolCreateInfo* info,
    const VkAllocationCallbacks* allocation,
    VkQueryPool* output)
{
    ++creates;
    const auto injected = nextCreateResult;
    nextCreateResult = VK_SUCCESS;
    if (injected != VK_SUCCESS)
        return injected;
    const auto result = vkCreateQueryPool(device, info, allocation, output);
    if (result == VK_SUCCESS)
        ++live;
    return result;
}

VKAPI_ATTR void VKAPI_CALL destroyQuery(
    VkDevice device, VkQueryPool pool, const VkAllocationCallbacks* allocation)
{
    assert(live);
    --live;
    vkDestroyQueryPool(device, pool, allocation);
}

VKAPI_ATTR void VKAPI_CALL resetQuery(
    VkDevice device, VkQueryPool pool, std::uint32_t first, std::uint32_t count)
{
    ++resets;
    vkResetQueryPool(device, pool, first, count);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolve(VkDevice device, const char* name)
{
    if (!std::strcmp(name, "vkCreateQueryPool"))
        return reinterpret_cast<PFN_vkVoidFunction>(createQuery);
    if (!std::strcmp(name, "vkDestroyQueryPool"))
        return reinterpret_cast<PFN_vkVoidFunction>(destroyQuery);
    if (!std::strcmp(name, "vkResetQueryPool"))
        return reinterpret_cast<PFN_vkVoidFunction>(resetQuery);
    return vkGetDeviceProcAddr(device, name);
}

class Fixture final {
public:
    std::array<std::shared_ptr<core::CpuMemoryQuota>, 9u> quotas;
    core::UniquePtr<GpuDevice> device;
    std::optional<OriginalDeviceNativeLossObservation> loss;
    std::shared_ptr<const QueryPoolInputSnapshot> input;
    OriginalResourceRegistrationBudget registration = ContractFixture::budget();
    OriginalQueryPoolCreationBudget creation;
    const char* diagnostic = nullptr;

    explicit Fixture(const std::shared_ptr<VulkanRhiRuntime>& runtime, std::uint32_t pools = 1u)
    {
        for (auto& q : quotas)
            q = core::makeShared<core::CpuMemoryQuota>(bytes);
        VulkanRhiDeviceDesc desc;
        desc.runtime = runtime;
        desc.capabilityContract.backend = Backend::Vulkan;
        desc.memoryBudgetBytes = bytes;
        device = createVulkanRhiDevice(desc);
        assert(
            device && !device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, quotas[0]));
        assert(
            !device->prepareOriginalDeviceNativeResultLoss({1u, true, 1u, 2u, bytes}, quotas[1]));
        const auto observed = device->observeOriginalDeviceNativeLoss(diagnostic);
        assert(observed && !diagnostic);
        loss.emplace(*observed);
        registration = ContractFixture::budget(pools);
        const std::uint64_t scopeId = 51u;
        const std::string_view label = "QueryLoss.Original";
        const QueryPoolDesc query{core::pmr::String(label), QueryType::Timestamp, 2u, &scopeId, 1u};
        const QueryPoolInputSnapshotBudget snapshot{
            1u, true, 1u, label.size(), bytes, bytes, bytes};
        input = QueryPoolInputSnapshot::prepareOriginal(
            query, snapshot, quotas[3], quotas[4], quotas[5], diagnostic);
        assert(input && !diagnostic);
        creation = {1u, true, 1u, 2u, 1u, label.size(), bytes};
    }

    void prepare()
    {
        assert(!device->prepareOriginalTypedNativeLossRegistration(
            {3u, true}, registration, quotas[2], *loss));
        assert(!device->validateOriginalTypedNativeLossJoin(*loss, {3u, true}));
        assert(!device->validateOriginalTypedNativeLossJoin(*loss, {4u, true}));
    }

    std::shared_ptr<QueryPool> create()
    {
        return device->createOriginalQueryPool(input, creation, quotas[6], diagnostic);
    }
};

void verifyNative(const std::shared_ptr<VulkanRhiRuntime>& runtime)
{
    Fixture f(runtime);
    const auto initialLive = live;
    auto invalid = f.registration;
    invalid.queryNativeLoss = {};
    ContractFixture::expect(f.device->prepareOriginalTypedNativeLossRegistration(
                                {3u, true}, invalid, f.quotas[2], *f.loss),
        "RHI-ORIGINAL-QUERY-NATIVE-LOSS-JOIN-UNAVAILABLE");
    assert(!f.quotas[2]->liveBytes());
    ContractFixture::expect(f.device->prepareOriginalTypedNativeLossRegistration(
                                {3u, true}, f.registration, f.quotas[1], *f.loss),
        "RHI-ORIGINAL-TYPED-LOSS-QUOTA-DOMAIN");
    {
        tests::VulkanHeadlessRuntime otherNative(false, resolve);
        assert(otherNative.runtime());
        Fixture other(otherNative.runtime());
        ContractFixture::expect(f.device->prepareOriginalTypedNativeLossRegistration(
                                    {3u, true}, f.registration, f.quotas[2], *other.loss),
            "RHI-ORIGINAL-DEVICE-LOSS-OBSERVATION-FOREIGN");
        assert(!f.quotas[2]->liveBytes());
    }
    f.prepare();
    const auto registryBaseline = f.quotas[2]->liveBytes();
    for (auto q : {f.quotas[2], f.quotas[6]}) {
        const auto calls = creates;
        core::CpuMemoryQuotaLease pressure(q, bytes - q->liveBytes());
        assert(pressure.charged() && !f.create() && f.diagnostic && calls == creates
            && live == initialLive);
    }
    nextCreateResult = VK_ERROR_OUT_OF_HOST_MEMORY;
    assert(!f.create() && f.diagnostic && live == initialLive && !f.quotas[6]->liveBytes()
        && f.quotas[2]->liveBytes() == registryBaseline && !f.loss->diagnostic());
    auto pool = f.create();
    assert(pool && !f.diagnostic && live == initialLive + 1u);
    const auto* native = asVulkanRhiQueryPool(*pool);
    assert(native && native->nativeHandle() && native->payload()->belongsTo(*runtime));
    assert(!VulkanRhiQueryPool::validateOriginal(
        std::dynamic_pointer_cast<const VulkanRhiQueryPool>(pool), *runtime));
    assert(!f.device->validateOriginalResourceRegistration(*pool));
    {
        core::CpuMemoryQuotaScope scope(f.quotas[8]);
        BufferDesc desc;
        desc.sizeBytes = 16u;
        desc.memory = MemoryUsage::CpuToGpu;
        desc.usage = ResourceUsage::StorageBuffer;
        auto buffer = f.device->createBuffer(desc);
        assert(buffer && !f.device->validateOriginalResourceRegistration(*buffer));
    }
    const auto firstGeneration = pool->originalResourceRegistration().generation();
    const auto calls = creates;
    assert(!f.create() && creates == calls);
    ContractFixture::expect(f.diagnostic, "RHI-ORIGINAL-REGISTRATION-KIND-CAPACITY");
    using Source = OriginalCommandQueryPoolSource;
    const OriginalCommandQueryPoolSourceBudget sb{
        1u, true, firstGeneration, bytes, Source::cpuOwnerSlots};
    auto source = Source::prepareOriginal(pool, sb, f.quotas[7], *f.device, f.diagnostic);
    assert(source && !f.diagnostic && !Source::validateOriginal(source, *f.device));
    assert(!test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
        f.quotas, [&]() noexcept -> const char* {
            if (const auto* id = Source::validateOriginal(source, *f.device))
                return id;
            if (const auto* id = f.device->validateOriginalTypedNativeLossJoin(*f.loss, {3u, true}))
                return id;
            const auto reset = pool->reset();
            assert(reset.success);
            return nullptr;
        }));
    auto weak = std::weak_ptr<QueryPool>(pool);
    pool.reset();
    assert(!weak.expired() && !f.create() && creates == calls);
    source.reset();
    assert(weak.expired() && live == initialLive);
    weak.reset();
    assert(!f.quotas[6]->liveBytes() && !f.quotas[7]->liveBytes()
        && f.quotas[2]->liveBytes() == registryBaseline);
    pool = f.create();
    assert(pool && pool->originalResourceRegistration().generation() > firstGeneration);
    const auto resetCalls = resets;
    assert(!f.device->detachOriginalDeviceNativeLoss());
    ContractFixture::expect(
        pool->originalResourceRegistration().diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED");
    assert(!pool->reset().success && resets == resetCalls);
    assert(pool->executionStatus().state == QueryExecutionState::DiagnosticFailure);
    f.device.reset();
    assert(!pool->reset().success && resets == resetCalls);
    pool.reset();
    assert(live == initialLive && !f.quotas[6]->liveBytes());
    f.loss.reset();
    f.input.reset();
    for (const auto& q : f.quotas)
        assert(!q->liveBytes());
    std::puts(
        "QUERY_LOSS_NATIVE real-payload same-registry source-custody capacity-rollback foreign-device retirement hot0 PASS");
}

void verifyNativeLoss()
{
    tests::VulkanHeadlessRuntime native(false, resolve);
    assert(native.runtime() && native.runtime()->timestampQueriesEnabled());
    Fixture f(native.runtime(), 2u);
    f.prepare();
    auto pool = f.create();
    assert(pool);
    const auto liveBefore = live;
    const auto registryBefore = f.quotas[2]->liveBytes();
    nextCreateResult = VK_ERROR_DEVICE_LOST;
    assert(!f.create() && live == liveBefore && f.quotas[2]->liveBytes() == registryBefore);
    ContractFixture::expect(f.diagnostic, "RHI-VULKAN-TIMESTAMP-CREATE-DEVICE-LOST");
    ContractFixture::expect(f.loss->diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
    ContractFixture::expect(pool->originalResourceRegistration().diagnostic(),
        "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
    const auto calls = creates, resetCalls = resets;
    assert(!f.create() && creates == calls && !pool->reset().success && resets == resetCalls);
    ContractFixture::expect(f.device->validateOriginalTypedNativeLossJoin(*f.loss, {3u, true}),
        "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
    std::puts(
        "QUERY_LOSS_RESULT injected-VK_ERROR_DEVICE_LOST original-subscription revoked-registry no-publication PASS");
}
} // namespace

int main()
{
    ContractFixture::verifyContract();
    tests::VulkanHeadlessRuntime native(false, resolve);
    if (!native.runtime() || !native.runtime()->timestampQueriesEnabled())
        return 77;
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(native.runtime()->physicalDevice(), &properties);
    std::printf("QUERY_LOSS_DEVICE name=%s vendor=%u device=%u driver=%u api=%u\n",
        properties.deviceName,
        properties.vendorID,
        properties.deviceID,
        properties.driverVersion,
        properties.apiVersion);
    verifyNative(native.runtime());
    verifyNativeLoss();
    assert(live == 0u);
}
