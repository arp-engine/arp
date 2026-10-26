#pragma once

#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/command_sources.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalGeneralDescriptorSourceFixture final {
    using Set = rhi::OriginalCommandDescriptorSetSource;
    using Table = rhi::OriginalCommandDescriptorTableSource;
    using Sources = rhi::OriginalCommandResourceSources;

    static void is(const char* actual, const char* expected)
    {
        if (!actual || std::string_view(actual) != expected)
            std::fprintf(stderr,
                "General Source expected %s, got %s\n",
                expected,
                actual ? actual : "success");
        assert(actual && std::string_view(actual) == expected);
    }

public:
    static void verifyContract()
    {
        static_assert(
            std::is_same_v<Set::FrozenElement, rhi::OriginalCommandDescriptorSetFrozenElement>);
        // Sets additionally retain completed depth provenance. Table storage
        // keeps its own element type; neither profile promises identical size.
        static_assert(std::is_same_v<decltype(Set::FrozenElement::completedDepth),
            std::shared_ptr<const rhi::OriginalCompletedDepthTextureSource>>);
        static_assert(std::is_same_v<Table::FrozenElement, Table::FrozenTableElement>);
        static_assert(!Set::finiteVersion(5u) && Set::generalFiniteVersion(5u));
        static_assert(Set::finiteVersion(3u) && Set::finiteVersion(4u));
        static_assert(Set::finiteExclusiveRetainedOwnerCount == 6u);
        rhi::OriginalGeneralDescriptorSourceDiagnosticRequest request{1u, true, 1u, 65536u};
        assert(!request.diagnostic());
        auto badRequest = request;
        badRequest.contractVersion = 2u;
        is(badRequest.diagnostic(), "RHI-ORIGINAL-GENERAL-SOURCE-DIAGNOSTIC-VERSION");
        badRequest = request;
        badRequest.prototypeOptIn = false;
        is(badRequest.diagnostic(), "RHI-ORIGINAL-GENERAL-SOURCE-DIAGNOSTIC-OPT-IN");
        badRequest = request;
        badRequest.expectedContentRevision = 0u;
        is(badRequest.diagnostic(), "RHI-ORIGINAL-GENERAL-SOURCE-REVISION");
        badRequest = request;
        badRequest.sourceCpuBytes = 0u;
        is(badRequest.diagnostic(), "RHI-ORIGINAL-GENERAL-SOURCE-CPU-BUDGET");
        rhi::OriginalCommandDescriptorSourceBudget budget{
            5u, true, 4u, 4u, 6u, 65536u, 6u, 0u, 2u, 1u, true};
        assert(!Set::validateBudget(budget));
        is(Table::validateBudget(budget), "RHI-ORIGINAL-FINITE-SOURCE-TABLE-PROFILE");
        for (std::uint32_t field = 0u; field < 7u; ++field) {
            auto bad = budget;
            const char* expected = nullptr;
            switch (field) {
            case 0u:
                bad.finitePipelineSourceIndex = SIZE_MAX;
                expected = "RHI-ORIGINAL-FINITE-SOURCE-PIPELINE-POSITION";
                break;
            case 1u:
                bad.finiteFootprintContractVersion = 1u;
                expected = "RHI-ORIGINAL-FINITE-SOURCE-FOOTPRINT-VERSION";
                break;
            case 2u:
                bad.finiteFullBoundViewOptIn = false;
                expected = "RHI-ORIGINAL-FINITE-SOURCE-FOOTPRINT-OPT-IN";
                break;
            case 3u:
                bad.maxFrozenDynamicOffsets = 5u;
                expected = "RHI-ORIGINAL-FINITE-SOURCE-DYNAMIC-CAPACITY";
                break;
            case 4u:
                --bad.maxExclusiveRetainedOwners;
                expected = "RHI-ORIGINAL-DESCRIPTOR-SOURCE-OWNER-CAPACITY";
                break;
            case 5u:
                bad.prototypeOptIn = false;
                expected = "RHI-ORIGINAL-DESCRIPTOR-SOURCE-OPT-IN";
                break;
            case 6u:
                bad.contractVersion = 8u;
                expected = "RHI-ORIGINAL-DESCRIPTOR-SOURCE-VERSION";
                break;
            }
            is(Set::validateBudget(bad), expected);
        }
        for (std::uint32_t version = 1u; version <= 4u; ++version) {
            auto old = budget;
            old.contractVersion = version;
            assert(Set::validateBudget(old));
            if (version >= 3u) {
                old.finiteFootprintContractVersion = 1u;
                assert(!Set::validateBudget(old));
            }
        }
        // Source6 is the completed-depth set profile, not an unknown version.
        auto depth = budget;
        depth.contractVersion = 6u;
        is(Set::validateBudget(depth), "RHI-COMPLETED-DEPTH-READ-INPUT-VERSION");
        is(Table::validateBudget(depth), "RHI-ORIGINAL-COMPLETED-DEPTH-SOURCE-TABLE-PROFILE");
        depth.completedDepthInputs.contractVersion = 3u;
        is(Set::validateBudget(depth), "RHI-COMPLETED-DEPTH-READ-INPUT-OPT-IN");
        depth.completedDepthInputs.prototypeOptIn = true;
        is(Set::validateBudget(depth), "RHI-COMPLETED-DEPTH-READ-INPUT-BUDGET");
        depth.completedDepthInputs.maxCells = 1u;
        depth.completedDepthInputs.maxTextureTexelBytes = 4u;
        depth.completedDepthInputs.maxRetainedOwners =
            rhi::OriginalCompletedDepthTextureSource::completeRetainedOwnerCount;
        is(Set::validateBudget(depth), "RHI-ORIGINAL-FINITE-SOURCE-LEGACY-SCHEMA");
        depth.finitePipelineSourceIndex = SIZE_MAX;
        depth.finiteFootprintContractVersion = 0u;
        depth.maxFrozenDynamicOffsets = 0u;
        depth.finiteFullBoundViewOptIn = false;
        assert(!Set::validateBudget(depth));
        for (std::uint32_t assembly = 0u; assembly <= 8u; ++assembly)
            for (std::uint32_t source = 1u; source <= 8u; ++source)
                assert(Sources::allowsDescriptorSourceVersion(assembly, source)
                    == ((assembly >= 1u && assembly <= 8u && source == 2u)
                        || (assembly >= 3u && assembly <= 5u && source == assembly)
                        || (assembly == 6u && (source == 5u || source == 6u))
                        || (assembly == 7u && source == 5u)
                        || (assembly == 8u && (source == 5u || source == 7u))));
        const std::array backends{
            rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU};
        const std::array<const char*, 4u> diagnostics{
            {"RHI-ORIGINAL-GENERAL-SOURCE-NULL-VALIDATOR-ONLY", nullptr, nullptr, nullptr}};
        for (std::size_t i = 0u; i < backends.size(); ++i) {
            const auto d =
                rhi::semantics::OriginalCommandDescriptorSourceLowering::generalFiniteLowering(
                    backends[i]);
            assert(!d.positionalAdapterAvailable && !d.executionAvailable);
            assert(d.freezeAvailable == (i == 1u || i == 2u || i == 3u));
            assert(d.mode
                == (i == 1u || i == 2u || i == 3u ? rhi::CapabilityMode::Equivalent
                                                  : rhi::CapabilityMode::DiagnosticFailure));
            if (i == 1u || i == 2u || i == 3u)
                assert(!d.diagnostic);
            else
                is(d.diagnostic, diagnostics[i]);
        }
        is(rhi::semantics::OriginalCommandDescriptorSourceLowering::generalFiniteLowering(
               static_cast<rhi::Backend>(255u))
                .diagnostic,
            "RHI-ORIGINAL-GENERAL-SOURCE-BACKEND");
        std::puts(
            "BINDLESS_RHI_GENERAL_SOURCE5_CONTRACT_OK footprint2 owners6 exact-assembly legacy-isolated four-backends execution-closed");
    }

    static void verifyUnsupported(rhi::GpuDevice& device)
    {
        assert(device.backend() == rhi::Backend::Null);
        auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
        auto lifetime = core::makeShared<core::CpuMemoryQuota>(65536u);
        auto registry = core::makeShared<core::CpuMemoryQuota>(65536u);
        assert(!device.prepareOriginalDeviceLifetime({1u, true, 1u, 2u, 65536u}, lifetime));
        rhi::OriginalResourceRegistrationBudget registrations;
        registrations.contractVersion = 3u;
        registrations.prototypeOptIn = true;
        registrations.maxDescriptorArenas = registrations.maxDescriptorSets = 1u;
        registrations.maxRetainedOwners = rhi::OriginalResourceRegistrationContract::fixedOwnerCount
            + rhi::OriginalResourceRegistrationContract::perResourceOwnerCount
            + rhi::OriginalResourceRegistrationContract::perDescriptorOwnerCount;
        registrations.maxCpuMetadataBytes = 65536u;
        assert(!device.prepareOriginalResourceRegistration(registrations, registry));
        {
            core::CpuMemoryQuotaScope scope(quota);
            rhi::DescriptorArenaDesc ad;
            ad.maxSets = ad.maxDescriptors = 1u;
            auto arena = core::shareUnique(device.createOriginalDescriptorArena(ad));
            assert(arena);
            rhi::DescriptorSetDesc sd;
            sd.layout.name = "General Source Null validator";
            rhi::DescriptorBindingDesc b;
            b.type = rhi::DescriptorType::UniformBuffer;
            b.stages = rhi::ShaderStage::Compute;
            sd.layout.bindings.push_back(b);
            auto object = device.allocateOriginalDescriptorSet(arena, sd);
            if (!object)
                std::fprintf(stderr,
                    "General Source Null allocation: %s\n",
                    device.lastOriginalResourceRegistrationDiagnostic().data());
            assert(object);
            const auto* original = object.get();
            const auto revision = object->contentRevision();
            const char* diagnostic = nullptr;
            const rhi::OriginalCommandDescriptorSourceBudget budget{
                5u, true, 1u, 1u, 6u, 65536u, 0u, 0u, 2u, 0u, true};
            assert(!Set::prepareOriginal(
                object, arena, {}, nullptr, 0u, budget, quota, device, diagnostic));
            is(diagnostic, "RHI-ORIGINAL-GENERAL-SOURCE-NULL-VALIDATOR-ONLY");
            assert(object.get() == original && object->contentRevision() == revision
                && !object->originalGeneralContentContractVersion());
            is(object->diagnoseOriginalGeneralSource({1u, true, revision, 65536u},
                   std::shared_ptr<const rhi::ComputePipeline>{},
                   {},
                   0u,
                   nullptr,
                   0u,
                   quota),
                "RHI-ORIGINAL-GENERAL-SOURCE-PAYLOAD-MISSING");
        }
        assert(!quota->liveBytes());
    }
};
} // namespace arp::test
