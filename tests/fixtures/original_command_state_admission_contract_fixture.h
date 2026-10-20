#pragma once

#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/command_model.h"
#include "rhi/semantics/command_lowering.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalCommandStateAdmissionContractFixture final {
public:
    static void verify()
    {
        using Contract = rhi::OriginalCommandStateAdmissionContract;
        using Budget = rhi::OriginalCommandStateAdmissionBudget;
        using D = rhi::CommandReferenceDomain;
        using R = Contract::Requirement;
        constexpr Budget intent{1u, true, 1u, 4u, 6u, 3u, 12u, 65536u};
        assert(!Contract::validateIntent(intent));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision =
                arp::rhi::semantics::OriginalCommandStateAdmissionLowering::lowering(backend);
            assert(decision.contractVersion == 1u && !decision.originalIssuerAvailable
                && !decision.executionAvailable && decision.diagnostic
                && std::string_view(arp::rhi::semantics::OriginalCommandStateAdmissionLowering::
                           admissionDiagnostic(backend, intent))
                    == decision.diagnostic);
            assert(decision.disposition
                == (backend == rhi::Backend::Null
                        ? arp::rhi::semantics::OriginalCommandStateAdmissionLowering::Disposition::
                              ValidatorOnly
                        : arp::rhi::semantics::OriginalCommandStateAdmissionLowering::Disposition::
                              DiagnosticFailure));
        }
        assert(
            std::string_view(arp::rhi::semantics::OriginalCommandStateAdmissionLowering::lowering(
                static_cast<rhi::Backend>(UINT32_MAX))
                    .diagnostic)
            == "RHI-ORIGINAL-STATE-BACKEND-UNKNOWN");
        assert(!arp::rhi::semantics::OriginalCommandStateAdmissionLowering::lowering(
            static_cast<rhi::Backend>(UINT32_MAX))
                .originalIssuerAvailable);
        const auto rejects = [&](auto mutate, const char* expected) {
            auto changed = intent;
            mutate(changed);
            assert(std::string_view(Contract::validateIntent(changed)) == expected);
            for (const auto backend : {rhi::Backend::Null,
                     rhi::Backend::Vulkan,
                     rhi::Backend::Metal,
                     rhi::Backend::WebGPU})
                assert(std::string_view(arp::rhi::semantics::OriginalCommandStateAdmissionLowering::
                               admissionDiagnostic(backend, changed))
                    == expected);
        };
        rejects(
            [](auto& b) {
                b.contractVersion = 0u;
            },
            "RHI-ORIGINAL-STATE-VERSION");
        rejects(
            [](auto& b) {
                b.contractVersion = 6u;
            },
            "RHI-ORIGINAL-STATE-VERSION");
        auto typed = intent;
        typed.contractVersion = 2u;
        assert(Contract::validateIntent(typed)); // Empty footprint never admits State2.
        typed.descriptorConsumption = {1u, true, 1u, 1u, true, true, 3u, 6u};
        assert(!Contract::validateIntent(typed));
        auto legacy = typed;
        legacy.contractVersion = 1u;
        assert(std::string_view(Contract::validateIntent(legacy))
            == "RHI-ORIGINAL-STATE-LEGACY-SCHEMA");
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto selected =
                arp::rhi::semantics::OriginalCommandStateAdmissionLowering::typedLowering(backend);
            assert(selected.projectionAvailable == (backend == rhi::Backend::WebGPU)
                && !selected.executionAvailable);
        }
        auto table = typed;
        table.contractVersion = 3u;
        table.descriptorConsumption = {2u, true, 0u, 0u, false, false, 1u, 3u, 1u, true, 3u};
        table.renderDescriptorUsage = {2u, true, 0u, 0u, false, false, 8u, 2u, 6u, 1u, true, 6u};
        assert(!Contract::validateIntent(table));
        auto readbackTable = table;
        readbackTable.retainedReadbackStateVersion = 1u;
        assert(std::string_view(Contract::validateIntent(readbackTable))
            == "RHI-ORIGINAL-STATE-READBACK-OPT-IN");
        readbackTable.retainedReadbackStateOptIn = true;
        assert(!Contract::validateIntent(readbackTable));
        auto badReadback = readbackTable;
        badReadback.retainedReadbackStateVersion = 2u;
        assert(std::string_view(Contract::validateIntent(badReadback))
            == "RHI-ORIGINAL-STATE-READBACK-VERSION");
        badReadback = readbackTable;
        badReadback.retainedReadbackStateVersion = 0u;
        assert(std::string_view(Contract::validateIntent(badReadback))
            == "RHI-ORIGINAL-STATE-READBACK-VERSION");
        badReadback = typed;
        badReadback.retainedReadbackStateVersion = 1u;
        badReadback.retainedReadbackStateOptIn = true;
        assert(std::string_view(Contract::validateIntent(badReadback))
            == "RHI-ORIGINAL-STATE-READBACK-PROFILE");
        badReadback = intent;
        badReadback.retainedReadbackStateVersion = 1u;
        badReadback.retainedReadbackStateOptIn = true;
        assert(std::string_view(Contract::validateIntent(badReadback))
            == "RHI-ORIGINAL-STATE-READBACK-PROFILE");
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto selected =
                rhi::semantics::OriginalCommandStateAdmissionLowering::typedReadbackLowering(
                    backend);
            assert(selected.contractVersion == 3u && !selected.executionAvailable
                && selected.validatorOnly == (backend == rhi::Backend::Null)
                && selected.retainedReadbackStateAvailable == (backend == rhi::Backend::WebGPU)
                && selected.stateAvailable == selected.retainedReadbackStateAvailable
                && selected.projectionAvailable == selected.stateAvailable);
            assert(
                !rhi::semantics::OriginalCommandStateAdmissionLowering::typedLowering(backend, 3u)
                    .retainedReadbackStateAvailable);
            assert(!rhi::semantics::OriginalCommandStateAdmissionLowering::typedReadbackLowering(
                backend, 2u)
                    .stateAvailable);
        }
        assert(!rhi::semantics::OriginalCommandStateAdmissionLowering::typedReadbackLowering(
            static_cast<rhi::Backend>(UINT32_MAX))
                .stateAvailable);
        auto badTable = table;
        badTable.renderDescriptorUsage = {};
        assert(std::string_view(Contract::validateIntent(badTable))
            == "RHI-ORIGINAL-STATE-RENDER-PROFILE");
        badTable = table;
        badTable.descriptorConsumption = typed.descriptorConsumption;
        assert(std::string_view(Contract::validateIntent(badTable))
            == "RHI-ORIGINAL-STATE-CONSUMPTION-PROFILE");
        badTable = typed;
        badTable.renderDescriptorUsage = table.renderDescriptorUsage;
        assert(std::string_view(Contract::validateIntent(badTable))
            == "RHI-ORIGINAL-STATE-RENDER-LEGACY-SCHEMA");
        badTable = table;
        badTable.renderDescriptorUsage.prototypeOptIn = false;
        assert(std::string_view(Contract::validateIntent(badTable))
            == "RHI-ORIGINAL-RENDER-USAGE-OPT-IN");
        badTable = table;
        badTable.renderDescriptorUsage.maxTextureAccesses = 0u;
        assert(std::string_view(Contract::validateIntent(badTable))
            == "RHI-ORIGINAL-CONSUMPTION-TEXTURE-BUDGET");
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto selected =
                arp::rhi::semantics::OriginalCommandStateAdmissionLowering::typedLowering(
                    backend, 3u);
            assert(selected.contractVersion == 3u && !selected.executionAvailable
                && selected.validatorOnly == (backend == rhi::Backend::Null)
                && selected.stateAvailable == (backend == rhi::Backend::WebGPU)
                && selected.projectionAvailable == selected.stateAvailable);
        }
        assert(arp::rhi::semantics::OriginalCommandStateAdmissionLowering::typedLowering(
            rhi::Backend::WebGPU, 6u)
                .diagnostic);
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto selected = arp::rhi::semantics::OriginalCommandStateAdmissionLowering::
                textureTransactionLowering(backend);
            assert(selected.contractVersion == 3u && !selected.executionAvailable
                && selected.diagnostic && selected.validatorOnly == (backend == rhi::Backend::Null)
                && selected.ownershipAvailable == (backend == rhi::Backend::WebGPU)
                && selected.planAvailable == selected.ownershipAvailable);
        }
        rhi::OriginalCommandStateSequenceBudget sequence{3u, true, 12u, 0u, 14u, 14u, 9u};
        assert(!rhi::OriginalCommandStateSequenceContract::validateIntent(sequence));
        auto badSequence = sequence;
        badSequence.contractVersion = 6u;
        assert(
            std::string_view(rhi::OriginalCommandStateSequenceContract::validateIntent(badSequence))
            == "RHI-ORIGINAL-SEQUENCE-VERSION");
        badSequence = sequence;
        badSequence.maxImplicitAccesses = 0u;
        assert(
            std::string_view(rhi::OriginalCommandStateSequenceContract::validateIntent(badSequence))
            == "RHI-ORIGINAL-SEQUENCE-IMPLICIT-BUDGET");
        badSequence = sequence;
        badSequence.prototypeOptIn = false;
        assert(
            std::string_view(rhi::OriginalCommandStateSequenceContract::validateIntent(badSequence))
            == "RHI-ORIGINAL-SEQUENCE-OPT-IN");
        rejects(
            [](auto& b) {
                b.prototypeOptIn = false;
            },
            "RHI-ORIGINAL-STATE-OPT-IN");
        rejects(
            [](auto& b) {
                b.maxDeviceDomains = 0u;
            },
            "RHI-ORIGINAL-STATE-DEVICE-DOMAIN");
        rejects(
            [](auto& b) {
                b.maxDeviceDomains = 2u;
            },
            "RHI-ORIGINAL-STATE-DEVICE-DOMAIN");
        rejects(
            [](auto& b) {
                b.maxResourceRegistrations = 0u;
            },
            "RHI-ORIGINAL-STATE-REGISTRATION-BUDGET");
        rejects(
            [](auto& b) {
                b.maxRetainedOwners = 0u;
            },
            "RHI-ORIGINAL-STATE-OWNER-CPU-BUDGET");
        rejects(
            [](auto& b) {
                b.maxCpuMetadataBytes = 0u;
            },
            "RHI-ORIGINAL-STATE-OWNER-CPU-BUDGET");
        rejects(
            [](auto& b) {
                b.maxStateObservations = 0u;
            },
            "RHI-ORIGINAL-STATE-RANGE-OBSERVATION-BUDGET");
        // A registered sampler/pipeline has no resource-state observation. Its
        // device/registration/lifetime requirement and closed issuer remain.
        auto stateFree = intent;
        stateFree.maxStateObservations = 0u;
        stateFree.maxTextureRanges = 0u;
        assert(!Contract::validateIntent(stateFree));
        const auto verifyDomains = []() noexcept -> const char* {
            for (std::uint32_t i = 0u; i < static_cast<std::uint32_t>(D::Count); ++i) {
                const auto domain = static_cast<D>(i);
                const auto requirement = Contract::requirement(domain);
                switch (domain) {
                case D::Buffer:
                    if (requirement != R::BufferCommittedRevision)
                        return "TEST-BUFFER-REQUIREMENT";
                    break;
                case D::Texture:
                    if (requirement != R::TextureRangeCommittedRevision)
                        return "TEST-TEXTURE-REQUIREMENT";
                    break;
                case D::TextureView:
                    if (requirement != R::CanonicalParentRange)
                        return "TEST-VIEW-REQUIREMENT";
                    break;
                case D::Sampler:
                case D::Pipeline:
                    if (requirement != R::StateFreeRegisteredLifetime)
                        return "TEST-STATE-FREE-REGISTRATION";
                    break;
                case D::Count:
                    return "TEST-COUNT-DOMAIN";
                default:
                    if (requirement != R::CanonicalRegisteredClosure)
                        return "TEST-CLOSURE-REQUIREMENT";
                }
                if (std::string_view(Contract::scalarAuthorityDiagnostic(domain))
                    != "RHI-ORIGINAL-STATE-REGISTRATION-UNPROVEN")
                    return "TEST-SCALAR-AUTHORITY-ACCEPTED";
            }
            for (const auto domain : {D::Count, static_cast<D>(UINT32_MAX)})
                if (Contract::requirement(domain) != R::Unsupported
                    || std::string_view(Contract::scalarAuthorityDiagnostic(domain))
                        != "RHI-ORIGINAL-STATE-SOURCE-UNSUPPORTED")
                    return "TEST-UNKNOWN-DOMAIN-ACCEPTED";
            return nullptr;
        };
        assert(!verifyDomains());
        auto quota = core::makeShared<core::CpuMemoryQuota>(intent.maxCpuMetadataBytes);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{quota}, [&]() noexcept {
            if (const auto* id = verifyDomains())
                return id;
            for (const auto backend : {rhi::Backend::Null,
                     rhi::Backend::Vulkan,
                     rhi::Backend::Metal,
                     rhi::Backend::WebGPU})
                if (!arp::rhi::semantics::OriginalCommandStateAdmissionLowering::
                        admissionDiagnostic(backend, intent)
                    || arp::rhi::semantics::OriginalCommandStateAdmissionLowering::lowering(backend)
                        .originalIssuerAvailable
                    || arp::rhi::semantics::OriginalCommandStateAdmissionLowering::lowering(backend)
                        .executionAvailable
                    || arp::rhi::semantics::OriginalCommandAdmissionLowering::lowering(backend)
                        .executionAvailable)
                    return "TEST-ORIGINAL-STATE-GATE-OPEN";
            return static_cast<const char*>(nullptr);
        }));
        assert(!quota->liveBytes());
        std::printf(
            "BINDLESS_RHI_GENERIC_ORIGINAL_STATE_CONTRACT_OK domains15 backends4 exact-device-registration-revision-range-lifetime independent-budgets scalar-authority-reject issuers4-closed execution4-closed hot-allocation0\n");
    }
};
} // namespace arp::test
