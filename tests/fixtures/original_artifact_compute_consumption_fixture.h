#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "rhi/backends/webgpu/webgpu_pipeline_preparation.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalArtifactComputeConsumptionFixture final {
    using Contract = rhi::OriginalPipelineArtifactConsumptionContract;
    using Factory = rhi_webgpu::WebGpuOriginalArtifactComputeFactory;
    using Input = rhi::OriginalSingleEntryPipelineInput;

    static void is(const char* actual, const char* expected)
    {
        assert(actual && std::string_view(actual) == expected);
    }

public:
    static void verifyContract()
    {
        Contract::Budget budget;
        is(Contract::validateBudget(budget), "RHI-ORIGINAL-ARTIFACT-CONSUMPTION-VERSION");
        budget.contractVersion = 1u;
        is(Contract::validateBudget(budget), "RHI-ORIGINAL-ARTIFACT-CONSUMPTION-OPT-IN");
        budget.prototypeOptIn = true;
        is(Contract::validateBudget(budget), "RHI-ORIGINAL-ARTIFACT-CONSUMPTION-BUDGET");
        budget.maxTargetBytes = 1u;
        budget.maxCpuMetadataBytes = 1u;
        assert(!Contract::validateBudget(budget));
        for (const auto backend : std::array{rhi::Backend::Null,
                 rhi::Backend::Vulkan,
                 rhi::Backend::Metal,
                 rhi::Backend::WebGPU}) {
            const auto decision = Contract::lowering(backend);
            assert(!decision.executionAvailable);
            if (backend == rhi::Backend::Null)
                assert(decision.mode == rhi::CapabilityMode::Equivalent && decision.validatorOnly
                    && !decision.diagnostic);
            else
                assert(
                    decision.mode == rhi::CapabilityMode::DiagnosticFailure && decision.diagnostic);
        }
        const auto native = Contract::lowering(rhi::Backend::WebGPU, true);
        assert(native.mode == rhi::CapabilityMode::Native && !native.executionAvailable
            && !native.validatorOnly);
        is(Contract::lowering(static_cast<rhi::Backend>(255u), true).diagnostic,
            "RHI-ORIGINAL-ARTIFACT-CONSUMPTION-BACKEND");
        is(Factory::validateConsumedOriginal({}),
            "RHI-ORIGINAL-ARTIFACT-CONSUMPTION-PIPELINE-OWNER");
        is(Contract::creationValidationDiagnostic(),
            "RHI-ORIGINAL-ARTIFACT-CREATION-RECEIPT-MISSING");
        auto unissued = core::makeShared<rhi_webgpu::WebGpuNativeComputePipelinePayload>();
        is(Factory::validateConsumedOriginal(unissued),
            "RHI-ORIGINAL-ARTIFACT-CONSUMPTION-RECEIPT-MISSING");
        std::puts(
            "BINDLESS_RHI_ORIGINAL_ARTIFACT_CONSUMPTION_CONTRACT_OK four-backends-explicit validator-only-or-native-required unissued-rejected canonical-closed");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyActual(
        const std::shared_ptr<const Input>& input, const rhi::PipelineLayout& layout)
    {
        assert(!loss_);
        const auto& bytes = input->artifact()->bytes(shader::ShaderArtifactRole::Wgsl);
        Contract::Budget budget{1u, true, bytes.size(), 65536u};
        auto quota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
        core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
        diagnostic.reserve(512u);
        auto factory = [&] {
            return Factory::create(
                input, layout, "original immutable compute", budget, quota, diagnostic);
        };
        auto expect = [&](auto original, auto limits, auto domain, const char* expected) {
            const auto before = quota->liveBytes();
            auto result = Factory::create(
                original, layout, "original immutable compute", limits, domain, diagnostic);
            if (result || diagnostic != expected)
                std::fprintf(
                    stderr, "consumption expected %s got %s\n", expected, diagnostic.c_str());
            assert(!result && diagnostic == expected && quota->liveBytes() == before);
        };
        auto invalid = budget;
        --invalid.maxTargetBytes;
        expect(input, invalid, quota, "RHI-ORIGINAL-ARTIFACT-CONSUMPTION-TARGET-BUDGET");
        auto forgedInput = std::shared_ptr<const Input>(input->quota(), input.get());
        expect(forgedInput, budget, quota, "RHI-ORIGINAL-ARTIFACT-INPUT-OWNER");
        expect(std::shared_ptr<const Input>{}, budget, quota, "RHI-ORIGINAL-ARTIFACT-INPUT-OWNER");
        invalid = budget;
        invalid.maxCpuMetadataBytes = input->quota()->limitBytes();
        expect(input, invalid, input->quota(), "RHI-ORIGINAL-ARTIFACT-CONSUMPTION-QUOTA-DOMAIN");
        invalid.maxCpuMetadataBytes = input->artifact()->quota()->limitBytes();
        expect(input,
            invalid,
            input->artifact()->quota(),
            "RHI-ORIGINAL-ARTIFACT-CONSUMPTION-QUOTA-DOMAIN");
        CpuQuotaFactoryFixture pressure(quota);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        {
            core::CpuMemoryQuotaLease exhausted(quota, quota->limitBytes());
            assert(exhausted.charged());
            expect(input, budget, quota, "RHI-ORIGINAL-ARTIFACT-CONSUMPTION-ALLOCATION");
        }
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        auto pipeline = factory();
        std::pmr::set_default_resource(previous);
        if (!pipeline)
            std::fprintf(stderr, "original memory compute failed %s\n", diagnostic.c_str());
        assert(pipeline && !Factory::validateConsumedOriginal(pipeline));
        const auto& issued = *pipeline->issuedContract();
        assert(core::SharedOwnerProjection::sameOwner(issued.artifactInput(), input));
        assert(core::SharedOwnerProjection::sameOwner(issued.quota(), quota));
        assert(issued.stages().size() == 1u && issued.stages()[0].sourceBytes == bytes.size());
        const auto digest = core::Sha256::digest(shader::shaderArtifactText(bytes));
        assert(std::equal(digest.begin(), digest.end(), issued.stages()[0].sourceSha256.begin()));
        auto observationQuota = core::makeShared<core::CpuMemoryQuota>(65536u);
        const rhi_webgpu::WebGpuOrdinaryPipelineBudget ordinary{
            2u, true, 1u, 1u, 1u, 0u, 65536u, 65536u, 1u, 1u, 0u, bytes.size()};
        const char* bridgeDiagnostic = nullptr;
        const auto bridgeAllocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!rhi_webgpu::WebGpuOrdinaryPipelineObservation::prepare(
            pipeline, ordinary, observationQuota, bridgeDiagnostic));
        is(bridgeDiagnostic, "RHI-WEBGPU-ORIGINAL-ARTIFACT-PIPELINE-ORDINARY-BRIDGE");
        assert(!observationQuota->liveBytes()
            && bridgeAllocations == core::defaultCpuAllocator().stats().allocationCount);
        auto forged =
            std::shared_ptr<rhi_webgpu::WebGpuNativeComputePipelinePayload>(quota, pipeline.get());
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota, input->quota(), input->artifact()->quota()}, [&]() noexcept {
                is(Factory::validateConsumedOriginal(forged),
                    "RHI-ORIGINAL-ARTIFACT-CONSUMPTION-PIPELINE-OWNER");
                quota->setLimitBytes(0u);
                is(Factory::validateConsumedOriginal(pipeline),
                    "RHI-ORIGINAL-ARTIFACT-CONSUMPTION-QUOTA-LIMIT");
                quota->setLimitBytes(budget.maxCpuMetadataBytes - 1u);
                is(Factory::validateConsumedOriginal(pipeline),
                    "RHI-ORIGINAL-ARTIFACT-CONSUMPTION-QUOTA-LIMIT");
                quota->setLimitBytes(budget.maxCpuMetadataBytes);
                ++pipeline->shaderPackageHash;
                is(Factory::validateConsumedOriginal(pipeline),
                    "RHI-WEBGPU-ORDINARY-PIPELINE-IDENTITY-CHANGED");
                --pipeline->shaderPackageHash;
                return Factory::validateConsumedOriginal(pipeline);
            }));
        forged.reset();
        pipeline.reset();
        assert(!quota->liveBytes());
        loss_ = factory();
        assert(loss_);
        std::printf(
            "BINDLESS_RHI_ORIGINAL_ARTIFACT_NATIVE_COMPUTE_CONSUMPTION_OK %llu source%llu exact-memory-digest original-input-retained quota-exact-less-exhausted hot-allocation0 canonical-closed\n",
            static_cast<unsigned long long>(peak),
            static_cast<unsigned long long>(bytes.size()));
    }

    static void verifyActualLoss()
    {
        assert(loss_);
        auto nativeQuota = loss_->issuedContract()->quota();
        auto inputQuota = loss_->issuedContract()->artifactInput()->quota();
        auto artifactQuota = loss_->issuedContract()->artifactInput()->artifact()->quota();
        assert(nativeQuota->liveBytes() && inputQuota->liveBytes() && artifactQuota->liveBytes());
        is(Factory::validateConsumedOriginal(loss_), "RHI-WEBGPU-ORDINARY-PIPELINE-DEVICE-LOST");
        loss_.reset();
        assert(
            !nativeQuota->liveBytes() && !inputQuota->liveBytes() && !artifactQuota->liveBytes());
        std::puts(
            "BINDLESS_RHI_ORIGINAL_ARTIFACT_NATIVE_COMPUTE_LOSS_OK actual-device-loss-rejected retained-input-released quotas3-zero");
    }

private:
    inline static std::shared_ptr<rhi_webgpu::WebGpuNativeComputePipelinePayload> loss_;
#endif
};
} // namespace arp::test
