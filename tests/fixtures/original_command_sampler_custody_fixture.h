#pragma once

#include "original_command_resource_lease_fixture.h"
#include "original_descriptor_buffer_binding_fixture.h"
#include "original_general_descriptor_binding_fixture.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/webgpu/webgpu_original_device_native_loss.h"
#include "rhi/frontend/command_usage.h"
#include "webgpu_foreign_device_fixture.h"

namespace arp::test {
class OriginalCommandSamplerCustodyFixture final {
    static constexpr std::uint64_t bytes = 65536u;
    using C = rhi::OriginalResourceRegistrationContract;
    using L = rhi::OriginalCommandSamplerLease;
    using Sources = rhi::OriginalCommandResourceSources;

    static auto quota() { return core::makeShared<core::CpuMemoryQuota>(bytes); }

    static rhi::OriginalResourceRegistrationBudget nativeBudget(std::uint32_t version = 7u)
    {
        rhi::OriginalResourceRegistrationBudget b;
        b.contractVersion = version;
        b.prototypeOptIn = true;
        b.maxPipelineLayouts = b.maxComputePipelines = 1u;
        b.maxRetainedOwners = C::fixedOwnerCount + C::perResourceOwnerCount;
        b.maxCpuMetadataBytes = bytes;
        b.samplers = {1u, true, 1u};
        return b;
    }

    static void is(const char* actual, const char* expected)
    {
        assert(actual && std::string_view(actual) == expected);
    }

    static void verifyDescriptorReference(const std::shared_ptr<const Sources>& sources,
        rhi::GpuDevice& device,
        rhi::GpuDevice& foreign,
        const std::shared_ptr<const L>& lease,
        const std::shared_ptr<core::CpuMemoryQuota>& source)
    {
        using T = rhi::OriginalCommandTypedSourceContract;
        using D = rhi::OriginalCommandDescriptorDependencies;
        rhi::DescriptorBindingDesc binding;
        binding.type = rhi::DescriptorType::Sampler;
        binding.stages = rhi::ShaderStage::Compute;
        rhi::DescriptorLayoutDesc layout;
        layout.name = "original sampler reference";
        layout.bindings.push_back(binding);
        rhi::OriginalCommandDescriptorSetElement element;
        element.write.type = binding.type;
        element.write.resourceKind = rhi::DescriptorResourceKind::Sampler;
        element.write.resource = lease->resource();
        element.write.resourceState = rhi::ResourceState::Undefined;
        element.sourceIndex = 0u;
        element.registrationGeneration =
            lease->originalObject()->originalResourceRegistration().generation();
        element.stages = rhi::PipelineStage::ComputeShader;
        const auto validate = [&](const auto& e) {
            return T::validateSetElements(layout, &e, 1u, sources, device);
        };
        assert(!validate(element));
        assert(T::validateSetElements(layout, &element, 1u, sources, foreign));
        assert(!T::validateGeneralSamplerFootprint(element, binding, lease->layout()));
        assert(T::validateWholeBoundView(element, binding, 0u, 1u));
        assert(T::validateFullBoundView(element, binding, 0u, 1u));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision = T::samplerReferenceLowering(backend);
            assert(decision.contractVersion == 1u && decision.referenceAvailable
                && decision.mode == rhi::CapabilityMode::Equivalent && !decision.executionAvailable
                && !decision.diagnostic);
        }
        assert(
            !T::samplerReferenceLowering(static_cast<rhi::Backend>(UINT32_MAX)).referenceAvailable);
        for (std::uint32_t field = 0u; field < 12u; ++field) {
            auto bad = element;
            switch (field) {
            case 0u:
                ++bad.registrationGeneration;
                break;
            case 1u:
                bad.sourceIndex = 1u;
                break;
            case 2u:
                bad.write.resourceKind = rhi::DescriptorResourceKind::Buffer;
                break;
            case 3u:
                bad.write.resourceState = rhi::ResourceState::ShaderRead;
                break;
            case 4u:
                bad.access = rhi::ResourceAccess::ShaderRead;
                break;
            case 5u:
                bad.bufferAccessBytes = 1u;
                break;
            case 6u:
                bad.bufferAccessOffset = 1u;
                break;
            case 7u:
                bad.dynamicOffset = 1u;
                break;
            case 8u:
                bad.write.bufferBindingRange.contractVersion = 2u;
                break;
            case 9u:
                bad.write.physicalLowering.enabled = true;
                break;
            case 10u:
                bad.stages = rhi::PipelineStage::FragmentShader;
                break;
            case 11u:
                bad.write.resource = {};
                break;
            }
            assert(validate(bad));
        }
        auto comparison = binding;
        comparison.comparisonSampler = true;
        assert(T::validateGeneralSamplerFootprint(element, comparison, lease->layout()));
        auto comparisonLayout = lease->layout();
        comparisonLayout.compareOp = rhi::CompareOp::Less;
        assert(!T::validateGeneralSamplerFootprint(element, comparison, comparisonLayout));
        assert(T::validateGeneralSamplerFootprint(element, binding, comparisonLayout));
        for (std::uint32_t field = 0u; field < 4u; ++field) {
            auto bad = binding;
            if (field == 0u)
                bad.arrayCount = 2u;
            if (field == 1u)
                bad.tableBacked = true;
            if (field == 2u)
                bad.dynamicOffset = true;
            if (field == 3u)
                bad.stages = static_cast<rhi::ShaderStage>(0x80000000u);
            assert(T::validateGeneralSamplerFootprint(element, bad, lease->layout()));
        }
        binding.stages = rhi::ShaderStage::Compute | rhi::ShaderStage::Fragment;
        assert(T::validateGeneralSamplerFootprint(element, binding, lease->layout()));
        auto combined = element;
        combined.stages = combined.stages | rhi::PipelineStage::FragmentShader;
        assert(!T::validateGeneralSamplerFootprint(combined, binding, lease->layout()));
        rhi::DescriptorTableDesc table;
        table.label = "unapproved sampler table";
        table.type = rhi::DescriptorType::Sampler;
        table.capacity = 1u;
        table.stages = rhi::ShaderStage::Compute;
        is(T::validateTableElements(table, nullptr, nullptr, nullptr, 0u, sources, device),
            "RHI-ORIGINAL-TYPED-SAMPLER-TABLE-ADAPTER-MISSING");
        auto owner = D::capture(*sources, 0u);
        assert(owner && D::matches(*sources, 0u, *owner));
        assert(!D::validateOwner(*owner, element.registrationGeneration, source));
        assert(D::validateOwner(*owner, element.registrationGeneration + 1u, source));
        assert(D::validateOwner(*owner, element.registrationGeneration, quota()));
        assert(!D::validateRegistered(*owner, device) && D::validateRegistered(*owner, foreign));
        D::Owner forged = std::shared_ptr<const L>(source, lease.get());
        assert(!D::matches(*sources, 0u, forged));
        assert(D::validateOwner(forged, element.registrationGeneration, source));
        const auto hot = [&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{source}, [&]() noexcept -> const char* {
                    if (const auto* id = validate(element))
                        return id;
                    return D::validateOwner(*owner, element.registrationGeneration, source);
                }));
        };
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries(hot);
#else
        hot();
#endif
        std::puts(
            "BINDLESS_RHI_ORIGINAL_SAMPLER_DESCRIPTOR_REFERENCE_OK exact-owner-generation comparison stages nonmemory table-refused hot0 no-native-authority");
    }

public:
    static void verifyContract()
    {
        OriginalGeneralDescriptorBindingFixture::verifyContract();
        using Request = rhi::OriginalSamplerRegistrationRequest;
        assert(!Request{}.diagnostic());
        is(Request{0u, true, 1u}.diagnostic(), "RHI-ORIGINAL-SAMPLER-REGISTRATION-VERSION");
        is(Request{2u, true, 1u}.diagnostic(), "RHI-ORIGINAL-SAMPLER-REGISTRATION-VERSION");
        is(Request{1u, false, 1u}.diagnostic(), "RHI-ORIGINAL-SAMPLER-REGISTRATION-OPT-IN");
        is(Request{1u, true, 0u}.diagnostic(), "RHI-ORIGINAL-SAMPLER-REGISTRATION-CAPACITY");
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision = C::samplerLowering(backend, {1u, true, 1u});
            assert(decision.mode == rhi::CapabilityMode::Equivalent
                && decision.typedLifetimeIssuerAvailable && !decision.originalStateIssuerAvailable
                && !decision.nativeLossObserved && !decision.executionAvailable
                && !decision.diagnostic);
            assert(!C::samplerLowering(backend, {}).typedLifetimeIssuerAvailable);
        }
        assert(!C::samplerLowering(static_cast<rhi::Backend>(UINT32_MAX), {1u, true, 1u})
                .typedLifetimeIssuerAvailable);
        static_assert(L::domain == rhi::CommandReferenceDomain::Sampler);
        static_assert(!std::is_copy_constructible_v<rhi::GpuSampler>);
        using Loss = rhi::OriginalSamplerNativeLossJoinContract;
        assert(!Loss::validateRegistration(nativeBudget()));
        for (std::uint32_t version = 1u; version <= 8u; ++version)
            if (version != 7u)
                is(Loss::validateRegistration(nativeBudget(version)),
                    "RHI-ORIGINAL-SAMPLER-LOSS-REGISTRATION-PROFILE");
        auto invalid = nativeBudget();
        invalid.samplers = {};
        is(Loss::validateRegistration(invalid), "RHI-ORIGINAL-SAMPLER-LOSS-OPT-IN");
        invalid = nativeBudget();
        invalid.samplers.prototypeOptIn = false;
        assert(Loss::validateRegistration(invalid));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision = Loss::lowering(backend);
            assert(decision.nativeSourceAvailable == (backend == rhi::Backend::WebGPU)
                && decision.validatorOnly == (backend == rhi::Backend::Null)
                && !decision.recordingAvailable && !decision.executionAvailable
                && bool(decision.diagnostic) == !decision.nativeSourceAvailable);
        }
        assert(Loss::lowering(rhi::Backend::WebGPU, 0u).diagnostic);
        assert(!Loss::lowering(static_cast<rhi::Backend>(UINT32_MAX)).nativeSourceAvailable);
    }

    template <class Factory> static void verifyNativeLossJoin(Factory&& create)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        auto domain = quota(), subscription = quota(), registration = quota(), resource = quota(),
             source = quota();
        const auto prepare = [&](std::uint32_t version = 7u) {
            auto device = create();
            assert(device);
            assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, domain));
            assert(!device->prepareOriginalDeviceNativeLoss(
                {{1u, true, 1u, 1u, bytes}, 1u}, {1u, true, 3u, 4u, bytes}, subscription));
            const char* id = nullptr;
            auto observed = device->observeOriginalDeviceNativeLoss(id);
            assert(observed && !id);
            if (version == 7u)
                assert(!device->prepareOriginalTypedNativeLossRegistration(
                    {1u, true}, nativeBudget(), registration, *observed));
            else
                assert(!device->prepareOriginalResourceRegistration(
                    nativeBudget(version), registration));
            return device;
        };
        {
            auto legacy = prepare(6u);
            assert(!legacy->createSampler({}));
            is(legacy->lastOriginalResourceRegistrationDiagnostic().data(),
                "RHI-ORIGINAL-SAMPLER-LOSS-REGISTRATION-PROFILE");
        }
        assert(!domain->liveBytes() && !subscription->liveBytes() && !registration->liveBytes());
        auto device = prepare();
        rhi_webgpu::WebGpuOriginalDeviceLossSource nativeSource;
        const auto factory = [&] {
            core::CpuMemoryQuotaScope scope(resource);
            return device->createSampler({});
        };
        CpuQuotaFactoryFixture pressure(registration);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        {
            core::CpuMemoryQuotaLease full(registration, bytes - registration->liveBytes());
            assert(full.charged() && !factory());
            is(device->lastOriginalResourceRegistrationDiagnostic().data(),
                "RHI-ORIGINAL-REGISTRATION-ALLOCATION");
        }
        {
            WebGpuForeignDeviceFixture foreign;
            core::UniquePtr<rhi::GpuDevice> foreignDevice;
            core::UniquePtr<rhi::GpuSampler> foreignSampler;
            {
                WebGpuForeignDeviceFixture::Scope scope(foreign);
                foreignDevice = create();
                assert(foreignDevice);
                foreignSampler = foreignDevice->createSampler({});
                assert(foreignSampler);
                assert(!factory());
                is(device->lastOriginalResourceRegistrationDiagnostic().data(),
                    "RHI-ORIGINAL-SAMPLER-LOSS-NATIVE-SOURCE");
            }
            is(nativeSource.validate(*foreignSampler), "RHI-ORIGINAL-SAMPLER-LOSS-NATIVE-SOURCE");
            foreignSampler.reset();
            foreignDevice.reset();
        }
        auto nullDevice = rhi::createNullDevice();
        auto absent = nullDevice->createSampler({});
        assert(absent);
        is(nativeSource.validate(*absent), "RHI-ORIGINAL-SAMPLER-LOSS-NATIVE-SOURCE");
        absent.reset();
        nullDevice.reset();
        auto sampler = factory();
        assert(sampler && !nativeSource.validate(*sampler));
        assert(!factory());
        is(device->lastOriginalResourceRegistrationDiagnostic().data(),
            "RHI-ORIGINAL-REGISTRATION-KIND-CAPACITY");
        auto payload = rhi_webgpu::webGpuNativePayloadFor(*sampler);
        assert(payload && payload->sampler && payload->ownerDevice);
        const auto handle = payload->sampler;
        payload->sampler = nullptr;
        is(nativeSource.validate(*sampler), "RHI-ORIGINAL-SAMPLER-LOSS-NATIVE-SOURCE");
        payload->sampler = handle;
        const auto ownerDevice = payload->ownerDevice;
        payload->ownerDevice = nullptr;
        is(nativeSource.validate(*sampler), "RHI-ORIGINAL-SAMPLER-LOSS-NATIVE-SOURCE");
        payload->ownerDevice = ownerDevice;
        payload.reset();
        auto ticket = sampler->originalResourceRegistration();
        assert(!ticket.diagnostic());
        const char* id = nullptr;
        auto lease =
            L::prepareOriginal(sampler, {1u, true, L::retainedOwnerCount, bytes}, source, id);
        assert(lease && !id && !sampler);
        auto foreignLogicalDevice = create();
        assert(foreignLogicalDevice);
        verifyDescriptorReference(
            [&] {
                rhi::OriginalCommandAdmissionBudget b;
                b.contractVersion = 1u;
                b.prototypeOptIn = true;
                b.maxCommands = b.maxReferences = 1u;
                b.maxGraphOwners = 1u + L::retainedOwnerCount;
                b.maxCpuBytes.fill(bytes);
                b.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Sampler)] =
                    1u;
                const Sources::Source entry = lease;
                return Sources::prepareOriginal(&entry, 1u, b, source, id);
            }(),
            *device,
            *foreignLogicalDevice,
            lease,
            source);
        foreignLogicalDevice.reset();
        auto observed = device->observeOriginalDeviceNativeLoss(id);
        assert(observed && !id);
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{domain, subscription, registration, source, resource},
                [&]() noexcept -> const char* {
                    if (const auto* result = device->validateOriginalTypedNativeLossJoin(*observed))
                        return result;
                    return device->validateOriginalResourceRegistration(*lease->originalObject());
                }));
        });
        // CPU shutdown preserves the existing terminal distinction.
        device.reset();
        is(ticket.diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN");
        lease.reset();
        ticket = {};
        observed.reset();
        assert(!domain->liveBytes() && !subscription->liveBytes() && !registration->liveBytes()
            && !source->liveBytes() && !resource->liveBytes());

        // Real independent GPUDevice.destroy delivers the existing authentic
        // loss callback; no test calls an original terminal receiver.
        std::shared_ptr<const Sources> retained;
        {
            WebGpuForeignDeviceFixture foreign;
            {
                WebGpuForeignDeviceFixture::Scope scope(foreign);
                device = prepare();
                sampler = factory();
                assert(sampler);
                const auto candidateObservation = device->observeOriginalDeviceNativeLoss(id);
                assert(candidateObservation && !id);
                observed.emplace(*candidateObservation);
                ticket = sampler->originalResourceRegistration();
                lease = L::prepareOriginal(
                    sampler, {1u, true, L::retainedOwnerCount, bytes}, source, id);
                assert(lease && !id);
                rhi::OriginalCommandAdmissionBudget b;
                b.contractVersion = 1u;
                b.prototypeOptIn = true;
                b.maxCommands = b.maxReferences = 1u;
                b.maxGraphOwners = 1u + L::retainedOwnerCount;
                b.maxCpuBytes.fill(bytes);
                b.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Sampler)] =
                    1u;
                Sources::Source entry = lease;
                retained = Sources::prepareOriginal(&entry, 1u, b, source, id);
                assert(retained && !id);
                entry = std::shared_ptr<const L>{};
                lease.reset();
                assert(!Sources::validateRegisteredLifetime(retained, *device));
            }
        }
        assert(WebGpuAsyncWaitFixture::until([&] {
            return observed->diagnostic() != nullptr;
        }));
        is(observed->diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
        is(ticket.diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
        is(Sources::validateRegisteredLifetime(retained, *device),
            "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
        assert(source->liveBytes() && resource->liveBytes());
        assert(!observed->nativeExecutionProven());
        assert(!factory());
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{domain, subscription, registration, source, resource},
                [&]() noexcept -> const char* {
                    retained.reset();
                    device.reset();
                    ticket = {};
                    observed.reset();
                    return nullptr;
                }));
        });
        assert(!domain->liveBytes() && !subscription->liveBytes() && !registration->liveBytes()
            && !source->liveBytes() && !resource->liveBytes());
        std::puts(
            "BINDLESS_RHI_ORIGINAL_SAMPLER_NATIVE_LOSS_JOIN_OK registry7 exact-native-source legacy6 foreign missing-refused authentic-independent-loss caller-drop original-quotas-zero hot0 no-execution-authority");
#else
        (void)create;
#endif
    }

    template <class Factory> static void verify(Factory&& create)
    {
        verifyContract();
        auto device = create(), foreign = create();
        assert(device && foreign);
        OriginalGeneralDescriptorWriteFixture::verifyUnsupported(*device);
        OriginalCommandResourceLeaseFixture::verifySamplerLease(*device);
        auto domain = quota(), registration = quota(), source = quota(), resource = quota();
        assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, domain));
        rhi::OriginalResourceRegistrationBudget budget;
        budget.contractVersion = 1u;
        budget.prototypeOptIn = true;
        budget.maxCpuMetadataBytes = bytes;
        budget.maxRetainedOwners = C::fixedOwnerCount + C::perResourceOwnerCount;
        budget.samplers = {1u, true, 1u};
        auto legacy = device->createSampler({});
        assert(legacy);
        auto invalid = budget;
        invalid.samplers.prototypeOptIn = false;
        is(device->prepareOriginalResourceRegistration(invalid, registration),
            "RHI-ORIGINAL-SAMPLER-REGISTRATION-OPT-IN");
        invalid = budget;
        --invalid.maxRetainedOwners;
        is(device->prepareOriginalResourceRegistration(invalid, registration),
            "RHI-ORIGINAL-REGISTRATION-OWNER-BUDGET");
        assert(!registration->liveBytes());
        assert(!device->prepareOriginalResourceRegistration(budget, registration));
        // Existing profiles retain their legacy sampler behavior after prepare.
        auto foreignDomain = quota(), foreignRegistration = quota();
        assert(!foreign->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, foreignDomain));
        auto legacyBudget = budget;
        legacyBudget.samplers = {};
        legacyBudget.maxBuffers = 1u;
        assert(!foreign->prepareOriginalResourceRegistration(legacyBudget, foreignRegistration));
        auto legacyAfterPrepare = foreign->createSampler({});
        assert(legacyAfterPrepare);
        is(legacyAfterPrepare->originalResourceRegistration().diagnostic(),
            "RHI-ORIGINAL-REGISTRATION-EXPIRED");
        is(device->validateOriginalResourceRegistration(*legacy),
            "RHI-ORIGINAL-REGISTRATION-EXPIRED");
        const auto factory = [&] {
            core::CpuMemoryQuotaScope scope(resource);
            return device->createSampler({});
        };
        CpuQuotaFactoryFixture pressure(registration);
        const auto baseline = registration->liveBytes();
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        assert(registration->liveBytes() == baseline);
        {
            core::CpuMemoryQuotaLease full(registration, bytes - baseline);
            assert(full.charged());
            assert(!factory());
            is(device->lastOriginalResourceRegistrationDiagnostic().data(),
                "RHI-ORIGINAL-REGISTRATION-ALLOCATION");
        }
        auto object = factory();
        assert(object);
        auto ticket = object->originalResourceRegistration();
        const auto generation = ticket.generation();
        assert(generation);
        assert(ticket.matchesIdentity(
            object.get(), object->id().value, rhi::OriginalRegisteredResourceKind::Sampler));
        assert(!ticket.matchesIdentity(
            object.get(), object->id().value, rhi::OriginalRegisteredResourceKind::TextureView));
        assert(!device->validateOriginalResourceRegistration(*object));
        is(foreign->validateOriginalResourceRegistration(ticket),
            "RHI-ORIGINAL-REGISTRATION-FOREIGN-DEVICE");
        assert(!factory());
        is(device->lastOriginalResourceRegistrationDiagnostic().data(),
            "RHI-ORIGINAL-REGISTRATION-KIND-CAPACITY");
        const auto* original = object.get();
        const auto allocation = core::inspectCpuAllocationContaining(original);
        const char* diagnostic = nullptr;
        auto lease = L::prepareOriginal(
            object, {1u, true, L::retainedOwnerCount, bytes}, source, diagnostic);
        assert(lease && !diagnostic && !object && lease->originalObject() == original);
        const auto adopted = core::inspectCpuAllocationContaining(original);
        assert(adopted.found && adopted.allocationIdentity == allocation.allocationIdentity
            && adopted.quota == allocation.quota);
        rhi::OriginalCommandAdmissionBudget sb;
        sb.contractVersion = 1u;
        sb.prototypeOptIn = true;
        sb.maxCommands = sb.maxReferences = 1u;
        sb.maxGraphOwners = 1u + L::retainedOwnerCount;
        sb.maxCpuBytes.fill(bytes);
        sb.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Sampler)] = 1u;
        Sources::Source entry = lease;
        auto sources = Sources::prepareOriginal(&entry, 1u, sb, source, diagnostic);
        assert(sources && !diagnostic && !Sources::validateRegisteredLifetime(sources, *device));
        assert(Sources::domain(entry) == rhi::CommandReferenceDomain::Sampler);
        assert(Sources::resource(entry) == original->id());
        verifyDescriptorReference(sources, *device, *foreign, lease, source);
        auto altered = sb;
        altered.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Sampler)] =
            0u;
        assert(!Sources::prepareOriginal(&entry, 1u, altered, source, diagnostic));
        is(diagnostic, "RHI-ORIGINAL-SOURCES-DOMAIN-CAPACITY");
        auto duplicateBudget = sb;
        duplicateBudget
            .maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Sampler)] = 2u;
        {
            const std::array<Sources::Source, 2u> duplicates{lease, lease};
            assert(!Sources::prepareOriginal(
                duplicates.data(), duplicates.size(), duplicateBudget, source, diagnostic));
            is(diagnostic, "RHI-ORIGINAL-SOURCES-DUPLICATE");
        }
        {
            Sources::Source forged = std::shared_ptr<const L>(source, lease.get());
            assert(!Sources::prepareOriginal(&forged, 1u, sb, source, diagnostic));
            is(diagnostic, "RHI-ORIGINAL-SOURCE-LEASE-OWNER");
        }
        std::weak_ptr<const L> observed = lease;
        entry = std::shared_ptr<const L>{};
        lease.reset();
        assert(!observed.expired());
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{domain, registration, source, resource}, [&]() noexcept {
                return Sources::validateRegisteredLifetime(sources, *device);
            }));
        sources.reset();
        assert(observed.expired());
        observed.reset();
        is(ticket.diagnostic(), "RHI-ORIGINAL-REGISTRATION-EXPIRED");
        assert(!source->liveBytes());
        auto replacement = factory();
        assert(replacement);
        assert(replacement->originalResourceRegistration().generation() > generation);
        device->retireOriginalDeviceLifetime();
        is(replacement->originalResourceRegistration().diagnostic(),
            "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED");
        assert(!factory());
        replacement.reset();
        legacy.reset();
        device.reset();
        ticket = {};
        legacyAfterPrepare.reset();
        foreign.reset();
        assert(!domain->liveBytes() && !registration->liveBytes() && !source->liveBytes()
            && !resource->liveBytes());
        assert(!foreignDomain->liveBytes() && !foreignRegistration->liveBytes());
        std::puts(
            "BINDLESS_RHI_ORIGINAL_SAMPLER_CUSTODY_OK opt-in1 backends4 exact-device-generation full-layout caller-drop source-inventory quotas hot0 no-native-closure-or-execution-authority");
    }
};
} // namespace arp::test
