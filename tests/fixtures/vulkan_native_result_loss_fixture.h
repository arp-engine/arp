#pragma once
#include "device_idle_failure_fixture.h"
#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_original_device_native_loss.h"
#include "tests/fixtures/cpu_quota_factory_fixture.h"
#include "tests/fixtures/vulkan_headless_runtime.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class VulkanNativeResultLossFixture final {
    static constexpr std::uint64_t bytes = 65536u;

    static auto quota() { return core::makeShared<core::CpuMemoryQuota>(bytes); }

    static void expect(const char* id, const char* wanted)
    {
        if (!id || std::string_view(id) != wanted)
            std::fprintf(
                stderr, "Vulkan ResultLoss actual=%s expected=%s\n", id ? id : "<none>", wanted);
        assert(id && std::string_view(id) == wanted);
    }

public:
    template <class Inject, class Restore>
    static void verify(const rhi_vulkan::VulkanRhiDeviceDesc& input, Inject inject, Restore restore)
    {
        using namespace rhi;
        using namespace rhi_vulkan;
        tests::VulkanHeadlessRuntime testNative(true, input.runtime->getDeviceProcAddr());
        assert(testNative.runtime());
        auto base = input;
        base.runtime = testNative.runtime();
        const OriginalDeviceNativeResultLossBudget budget{1u, true, 1u, 2u, bytes};
        const auto prepareDomain = [&](const auto& desc, const auto& domain) {
            auto device = createVulkanRhiDevice(desc);
            assert(device);
            assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, domain));
            return device;
        };
        auto domain = quota(), source = quota(), registry = quota(), wrappers = quota();
        auto device = prepareDomain(base, domain);
        expect(device->prepareOriginalDeviceNativeResultLoss(budget, domain),
            "RHI-ORIGINAL-DEVICE-LOSS-QUOTA-DOMAIN");
        {
            core::CpuMemoryQuotaLease pressure(source, bytes);
            assert(pressure.charged());
            expect(device->prepareOriginalDeviceNativeResultLoss(budget, source),
                "RHI-ORIGINAL-RESULT-LOSS-ALLOCATION");
            const char* id = nullptr;
            assert(!device->observeOriginalDeviceNativeLoss(id));
            expect(id, "RHI-ORIGINAL-DEVICE-LOSS-UNPREPARED");
        }
        assert(
            !source->liveBytes() && !device->prepareOriginalDeviceNativeResultLoss(budget, source));
        const char* id = nullptr;
        auto observation = device->observeOriginalDeviceNativeLoss(id);
        assert(observation && !id && !observation->diagnostic()
            && !observation->nativeExecutionProven());
        auto duplicateDomain = quota(), duplicateSource = quota();
        auto duplicate = prepareDomain(base, duplicateDomain);
        expect(duplicate->prepareOriginalDeviceNativeResultLoss(budget, duplicateSource),
            "RHI-VULKAN-RESULT-LOSS-SOURCE-CAPACITY");
        assert(!duplicateSource->liveBytes() && !observation->diagnostic());
        duplicate.reset();
        assert(!duplicateDomain->liveBytes());
        assert(!device->prepareOriginalResourceRegistration(
            {1u, true, 1u, 1u, 1u, 9u, bytes}, registry));
        core::UniquePtr<GpuBuffer> buffer;
        core::UniquePtr<GpuTexture> texture;
        core::UniquePtr<GpuTextureView> view;
        {
            core::CpuMemoryQuotaScope scope(wrappers);
            BufferDesc bufferDesc;
            bufferDesc.sizeBytes = 16u;
            bufferDesc.memory = MemoryUsage::CpuToGpu;
            bufferDesc.usage = ResourceUsage::StorageBuffer;
            buffer = device->createBuffer(bufferDesc);
            TextureDesc textureDesc;
            textureDesc.extent = {2u, 2u, 1u};
            textureDesc.format = TextureFormat::Rgba8Unorm;
            textureDesc.usage = TextureUsage::Sampled;
            textureDesc.initialState = ResourceState::Undefined;
            texture = device->createTexture(textureDesc);
            assert(buffer && texture);
            TextureViewDesc viewDesc;
            viewDesc.format = textureDesc.format;
            viewDesc.usage = TextureUsage::Sampled;
            view = device->createTextureView(*texture, viewDesc);
            assert(view);
        }
        assert(!device->validateOriginalResourceRegistration(*buffer)
            && !device->validateOriginalResourceRegistration(*texture)
            && !device->validateOriginalResourceRegistration(*view));
        const std::array domains{domain, source, registry, wrappers};
        source->setLimitBytes(bytes - 1u);
        expect(observation->diagnostic(), "RHI-ORIGINAL-RESULT-LOSS-QUOTA");
        source->setLimitBytes(bytes);
        assert(
            !CpuQuotaFactoryFixture::verifyHotNoAllocation(domains, [&]() noexcept -> const char* {
                auto copy = device->observeOriginalDeviceNativeLoss(id);
                if (!copy || id)
                    return id ? id : "TEST-VULKAN-LOSS-OBSERVATION";
                if (const auto* failure = device->validateOriginalDeviceNativeLoss(*copy))
                    return failure;
                return copy->compareOriginal(*observation);
            }));
        // Logical detach actually removes the weak source subscription. A new
        // facade may bind the healthy native runtime, but old observations stay
        // permanently Retired and can never observe the new logical domain.
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(domains, [&]() noexcept {
            return device->detachOriginalDeviceNativeLoss();
        }));
        expect(observation->diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED");
        view.reset();
        texture.reset();
        buffer.reset();
        device.reset();
        assert(!registry->liveBytes() && !wrappers->liveBytes());
        observation.reset();
        assert(!domain->liveBytes() && !source->liveBytes());

        // A second real VkDevice provides independent healthy source identity.
        tests::VulkanHeadlessRuntime foreign(true);
        assert(foreign.runtime());
        auto foreignDesc = base;
        foreignDesc.runtime = foreign.runtime();
        auto foreignDomain = quota(), foreignSource = quota();
        auto other = prepareDomain(foreignDesc, foreignDomain);
        assert(!other->prepareOriginalDeviceNativeResultLoss(budget, foreignSource));
        auto otherObservation = other->observeOriginalDeviceNativeLoss(id);
        assert(otherObservation);
        device = prepareDomain(base, domain);
        assert(!device->prepareOriginalDeviceNativeResultLoss(budget, source));
        {
            const auto recaptured = device->observeOriginalDeviceNativeLoss(id);
            assert(recaptured);
            observation.emplace(*recaptured);
        }
        expect(observation->compareOriginal(*otherObservation),
            "RHI-ORIGINAL-DEVICE-LOSS-OBSERVATION-FOREIGN");
        expect(device->validateOriginalDeviceNativeLoss(*otherObservation),
            "RHI-ORIGINAL-DEVICE-LOSS-OBSERVATION-FOREIGN");
        device->waitIdle();
        assert(!observation->diagnostic());
        // Negative VkResult injection after real driver idle. This proves the
        // result consumer and terminal identity, not actual hardware device loss.
        inject(base.runtime->logicalDevice());
        tests::DeviceIdleFailureFixture::verify(*device, "RHI-VULKAN-IDLE-COMPLETION-DEVICE-LOST");
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{domain, source, foreignDomain, foreignSource},
            [&]() noexcept -> const char* {
                expect(observation->diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
                assert(!otherObservation->diagnostic());
                return nullptr;
            }));
        restore();
        device->waitIdle();
        expect(observation->diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
        device.reset();
        expect(observation->diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
        observation.reset();
        assert(!domain->liveBytes() && !source->liveBytes());
        auto replacement = prepareDomain(base, domain);
        expect(replacement->prepareOriginalDeviceNativeResultLoss(budget, source),
            "RHI-VULKAN-RESULT-LOSS-RUNTIME-LOST");
        assert(!source->liveBytes());
        replacement.reset();
        assert(!domain->liveBytes());
        other.reset();
        expect(otherObservation->diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN");
        otherObservation.reset();
        assert(!foreignDomain->liveBytes() && !foreignSource->liveBytes());
        std::puts("BINDLESS_VULKAN_NATIVE_RESULT_LOSS_SOURCE 1");
    }
};
} // namespace arp::test
