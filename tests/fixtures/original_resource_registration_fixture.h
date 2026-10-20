#pragma once

#include "cpu_quota_factory_fixture.h"
#include "original_command_buffer_upload_inputs_fixture.h"
#include "original_command_inputs_fixture.h"
#include "original_command_texture_readback_inputs_fixture.h"
#include "original_command_texture_upload_inputs_fixture.h"
#include "rhi/frontend/command_model.h"
#include "rhi/frontend/command_sources.h"
#include "rhi/gpu_device.h"
#include "rhi/semantics/command_lowering.h"

#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalResourceRegistrationFixture final {
    static constexpr std::uint64_t bytes = 65536u;
    using Budget = rhi::OriginalResourceRegistrationBudget;
    using Kind = rhi::OriginalRegisteredResourceKind;

    static void is(const char* actual, const char* expected)
    {
        assert(actual && std::string_view(actual) == expected);
    }

    static auto quota() { return core::makeShared<core::CpuMemoryQuota>(bytes); }

    static void prepareDomain(
        rhi::GpuDevice& device, const std::shared_ptr<core::CpuMemoryQuota>& domain)
    {
        assert(!device.prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, domain));
    }

public:
    template <class Factory> static void verify(Factory&& create)
    {
        using Contract = rhi::OriginalResourceRegistrationContract;
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto plan = Contract::lowering(backend);
            assert(plan.contractVersion == 1u && plan.typedLifetimeIssuerAvailable
                && plan.mode == rhi::CapabilityMode::Equivalent
                && !plan.originalStateIssuerAvailable && !plan.nativeLossObserved
                && !plan.executionAvailable);
            assert(!arp::rhi::semantics::OriginalCommandStateAdmissionLowering::lowering(backend)
                    .originalIssuerAvailable);
        }
        assert(!Contract::lowering(static_cast<rhi::Backend>(UINT32_MAX))
                .typedLifetimeIssuerAvailable);
        static_assert(!std::is_copy_constructible_v<rhi::GpuTexture>);
        static_assert(!std::is_default_constructible_v<rhi::OriginalResourceRegistration>);
        rhi::BufferDesc bd;
        bd.sizeBytes = 64u;
        bd.usage = rhi::ResourceUsage::CopySrc | rhi::ResourceUsage::CopyDst;
        rhi::TextureDesc td;
        td.extent = {2u, 2u, 1u};
        td.format = rhi::TextureFormat::Rgba8Unorm;
        td.usage =
            rhi::TextureUsage::Sampled | rhi::TextureUsage::CopySrc | rhi::TextureUsage::CopyDst;
        rhi::TextureViewDesc vd;
        vd.format = td.format;
        vd.usage = rhi::TextureUsage::Sampled;
        const Budget budget{
            1u, true, 2u, 2u, 1u, 13u, bytes}; // Five live wrappers/records + fixed3.
        auto domain = quota(), metadata = quota(), resourceQuota = quota();
        const auto registryFactory = [&] {
            auto device = create();
            assert(device);
            prepareDomain(*device, domain);
            const auto* id = device->prepareOriginalResourceRegistration(budget, metadata);
            if (id) {
                is(id, "RHI-ORIGINAL-REGISTRATION-ALLOCATION");
                device.reset();
            }
            return device;
        };
        CpuQuotaFactoryFixture pressure(metadata);
        const auto registryPeak = pressure.measure(registryFactory);
        assert(!metadata->liveBytes() && !domain->liveBytes());
        // Measure all factories before pressure probes increase historical peaks.
        auto device = registryFactory();
        assert(device);
        const auto recordFactory = [&] {
            core::CpuMemoryQuotaScope scope(resourceQuota);
            return device->createBuffer(bd);
        };
        const auto recordPeak = pressure.measure(recordFactory);
        const auto registryBaseline = metadata->liveBytes();
        pressure.verify(registryPeak, registryFactory);
        assert(metadata->liveBytes() == registryBaseline);
        pressure.verify(recordPeak, recordFactory);
        assert(metadata->liveBytes() == registryBaseline);
        {
            core::CpuMemoryQuotaLease full(metadata, bytes - registryBaseline);
            assert(full.charged());
            assert(!recordFactory());
            is(device->lastOriginalResourceRegistrationDiagnostic().data(),
                "RHI-ORIGINAL-REGISTRATION-ALLOCATION");
        }
        is(device->prepareOriginalResourceRegistration(budget, metadata),
            "RHI-ORIGINAL-REGISTRATION-ALREADY-PREPARED");
        auto foreign = create();
        assert(foreign);
        auto unregistered = foreign->createBuffer(bd);
        assert(unregistered);
        is(unregistered->originalResourceRegistration().diagnostic(),
            "RHI-ORIGINAL-REGISTRATION-EXPIRED");
        auto a = recordFactory(), b = recordFactory();
        assert(a && b);
        auto first = a->originalResourceRegistration();
        const auto generation = first.generation();
        assert(generation);
        assert(!device->validateOriginalResourceRegistration(*a));
        assert(first.matchesIdentity(a.get(), a->id().value, Kind::Buffer));
        assert(!first.matchesIdentity(b.get(),
            a->id().value,
            Kind::Buffer)); // Equal-ID impostor cannot replace exact object.
        assert(!first.matchesIdentity(a.get(), b->id().value, Kind::Buffer));
        assert(!first.matchesIdentity(a.get(), a->id().value, Kind::Texture));
        is(foreign->validateOriginalResourceRegistration(first),
            "RHI-ORIGINAL-REGISTRATION-FOREIGN-DEVICE");
        assert(!recordFactory());
        is(device->lastOriginalResourceRegistrationDiagnostic().data(),
            "RHI-ORIGINAL-REGISTRATION-KIND-CAPACITY");
        auto texture = device->createTexture(td), other = device->createTexture(td);
        assert(texture && other);
        assert(!device->createTexture(td));
        is(device->lastOriginalResourceRegistrationDiagnostic().data(),
            "RHI-ORIGINAL-REGISTRATION-KIND-CAPACITY");
        auto view = device->createTextureView(*texture, vd);
        assert(view);
        assert(view->originalResourceRegistration().matchesParent(
            texture->originalResourceRegistration()));
        assert(!view->originalResourceRegistration().matchesParent(
            other->originalResourceRegistration()));
        assert(!device->createTextureView(*texture, vd));
        is(device->lastOriginalResourceRegistrationDiagnostic().data(),
            "RHI-ORIGINAL-REGISTRATION-KIND-CAPACITY");
        // Bind the exact canonical physical Source graph to its original factory records.
        using Sources = rhi::OriginalCommandResourceSources;
        auto sourceQuota = quota();
        const rhi::OriginalCommandResourceLeaseBudget leaseBudget{1u, true, 5u, bytes};
        const auto allocation = core::inspectCpuAllocationContaining(a.get());
        assert(allocation.found && allocation.quota == resourceQuota.get());
        const auto* object = a.get();
        const char* diagnostic = nullptr;
        auto al = rhi::OriginalCommandBufferLease::prepareOriginal(
            a, leaseBudget, sourceQuota, diagnostic);
        auto bl = rhi::OriginalCommandBufferLease::prepareOriginal(
            b, leaseBudget, sourceQuota, diagnostic);
        auto tl = rhi::OriginalCommandTextureLease::prepareOriginal(
            texture, leaseBudget, sourceQuota, diagnostic);
        auto vl = rhi::OriginalCommandTextureViewLease::prepareOriginal(
            view, leaseBudget, sourceQuota, diagnostic);
        auto vs = rhi::OriginalCommandTextureViewSource::prepareOriginal(
            vl, tl, leaseBudget, sourceQuota, diagnostic);
        assert(al && bl && tl && vl && vs);
        const auto preserved = core::inspectCpuAllocationContaining(object);
        assert(preserved.allocationIdentity == allocation.allocationIdentity
            && preserved.sizeBytes == allocation.sizeBytes && preserved.quota == allocation.quota);
        std::array<Sources::Source, 4> entries{al, bl, tl, vs};
        rhi::OriginalCommandAdmissionBudget sb;
        sb.contractVersion = 1u;
        sb.prototypeOptIn = true;
        sb.maxCommands = 1u;
        sb.maxReferences = 4u;
        sb.maxGraphOwners = 10u;
        sb.maxCpuBytes[static_cast<std::size_t>(decltype(sb)::CpuDomain::Source)] = bytes;
        sb.maxCpuBytes[static_cast<std::size_t>(decltype(sb)::CpuDomain::Graph)] = bytes;
        sb.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Buffer)] = 2u;
        sb.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Texture)] = 1u;
        sb.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::TextureView)] = 1u;
        auto sources =
            Sources::prepareOriginal(entries.data(), entries.size(), sb, sourceQuota, diagnostic);
        assert(sources);
        assert(!Sources::validateRegisteredLifetime(sources, *device));
        is(Sources::validateRegisteredLifetime(sources, *foreign),
            "RHI-ORIGINAL-REGISTRATION-UNPREPARED");
        auto foreignDomain = quota(), foreignMetadata = quota();
        prepareDomain(*foreign, foreignDomain);
        assert(!foreign->prepareOriginalResourceRegistration(budget, foreignMetadata));
        is(Sources::validateRegisteredLifetime(sources, *foreign),
            "RHI-ORIGINAL-REGISTRATION-FOREIGN-DEVICE");
        // Preparation never adopts older ordinary objects into the new issuer.
        is(foreign->validateOriginalResourceRegistration(*unregistered),
            "RHI-ORIGINAL-REGISTRATION-EXPIRED");
        metadata->setLimitBytes(bytes + 1u);
        is(first.diagnostic(), "RHI-ORIGINAL-REGISTRATION-QUOTA");
        metadata->setLimitBytes(bytes);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{domain, metadata, sourceQuota}, [&]() noexcept {
                auto hot = al->originalObject()->originalResourceRegistration();
                auto copy = hot;
                return copy.diagnostic() ? copy.diagnostic()
                                         : Sources::validateRegisteredLifetime(sources, *device);
            }));
        entries = {};
        al.reset();
        bl.reset();
        tl.reset();
        vl.reset();
        vs.reset();
        assert(!first.diagnostic());
        sources.reset();
        is(first.diagnostic(), "RHI-ORIGINAL-REGISTRATION-EXPIRED");
        assert(!sourceQuota->liveBytes());
        auto replacement = recordFactory();
        assert(
            replacement && replacement->originalResourceRegistration().generation() > generation);
        auto child = device->createTextureView(*other, vd);
        assert(child);
        auto childTicket = child->originalResourceRegistration();
        other.reset();
        is(childTicket.diagnostic(), "RHI-ORIGINAL-REGISTRATION-PARENT-EXPIRED");
        child.reset();
        childTicket = {};
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{domain, metadata}, [&]() noexcept {
                device->retireOriginalDeviceLifetime();
                return std::string_view(replacement->originalResourceRegistration().diagnostic())
                        == "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED"
                    ? nullptr
                    : "TEST-REGISTRATION-RETIREMENT";
            }));
        assert(!recordFactory());
        is(device->lastOriginalResourceRegistrationDiagnostic().data(),
            "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED");
        replacement.reset();
        device.reset();
        first = {};
        assert(!metadata->liveBytes() && !domain->liveBytes());
        verifyOwnerAndIntent(create, bd, budget);
        verifyAdapterBindings(create, budget);
        std::puts(
            "BINDLESS_RHI_ORIGINAL_RESOURCE_REGISTRATION_OK factories3 backends4 exact-device-object-id-parent monotonic-lifetime independent-kind-owner-quota original-source-binding cold-exact-less-exhausted caller-drop hot-allocation0 full-state-native-issuers-closed");
    }

private:
    template <class Factory>
    static void verifyAdapterBindings(Factory&& create, const Budget& budget)
    {
        // Existing fixtures require at most two simultaneous Buffers and two
        // Textures, including independent negative readback chains. Reuse the
        // declared five-resource policy; no test-only production limit increase.
        auto device = create();
        auto domain = quota(), metadata = quota();
        assert(device);
        prepareDomain(*device, domain);
        assert(!device->prepareOriginalResourceRegistration(budget, metadata));
        OriginalCommandInputsFixture::verify(*device, true);
        OriginalCommandBufferUploadInputsFixture::verify(*device, true);
        OriginalCommandTextureUploadInputsFixture::verify(*device, true);
        OriginalCommandTextureReadbackInputsFixture::verify(*device, true);
        device.reset();
        assert(!metadata->liveBytes() && !domain->liveBytes());
    }

    template <class Factory>
    static void verifyOwnerAndIntent(Factory&& create, const rhi::BufferDesc& bd, Budget budget)
    {
        auto device = create();
        auto domain = quota(), metadata = quota();
        assert(device);
        is(device->prepareOriginalResourceRegistration(budget, metadata),
            "RHI-ORIGINAL-REGISTRATION-DEVICE-UNPREPARED");
        prepareDomain(*device, domain);
        const auto invalid = [&](auto change, const char* expected) {
            auto changed = budget;
            change(changed);
            is(device->prepareOriginalResourceRegistration(changed, metadata), expected);
            assert(!metadata->liveBytes());
        };
        invalid(
            [](auto& b) {
                b.contractVersion = 0u;
            },
            "RHI-ORIGINAL-REGISTRATION-VERSION");
        invalid(
            [](auto& b) {
                b.prototypeOptIn = false;
            },
            "RHI-ORIGINAL-REGISTRATION-OPT-IN");
        invalid(
            [](auto& b) {
                b.maxBuffers = b.maxTextures = b.maxTextureViews = 0u;
            },
            "RHI-ORIGINAL-REGISTRATION-KIND-BUDGET");
        invalid(
            [](auto& b) {
                b.maxTextures = 0u;
            },
            "RHI-ORIGINAL-REGISTRATION-PARENT-BUDGET");
        invalid(
            [](auto& b) {
                b.maxRetainedOwners = 4u;
            },
            "RHI-ORIGINAL-REGISTRATION-OWNER-BUDGET");
        invalid(
            [](auto& b) {
                b.maxCpuMetadataBytes = 0u;
            },
            "RHI-ORIGINAL-REGISTRATION-CPU-BUDGET");
        is(device->prepareOriginalResourceRegistration(budget, domain),
            "RHI-ORIGINAL-REGISTRATION-QUOTA");
        is(device->prepareOriginalResourceRegistration(budget, {}),
            "RHI-ORIGINAL-REGISTRATION-QUOTA");
        budget.maxRetainedOwners = 5u;
        assert(!device->prepareOriginalResourceRegistration(budget, metadata));
        auto a = device->createBuffer(bd);
        assert(a);
        assert(!device->createBuffer(bd));
        is(device->lastOriginalResourceRegistrationDiagnostic().data(),
            "RHI-ORIGINAL-REGISTRATION-OWNER-CAPACITY");
        auto ticket = a->originalResourceRegistration();
        device.reset();
        is(ticket.diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN");
        a.reset();
        ticket = {};
        assert(!metadata->liveBytes() && !domain->liveBytes());
    }
};
} // namespace arp::test
