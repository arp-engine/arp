#pragma once
#include "compute_fixed_words_wgsl_fixture.h"
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_native_ordinary_scalar_compute_range.h"
#include "webgpu_ordinary_compute_recording_fixture.h"
#include "webgpu_original_scalar_access_fixture.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class WebGpuOrdinaryScalarComputeRangeFixture final {
public:
    static void verifyContract()
    {
        using namespace rhi_webgpu;
        WebGpuOrdinaryScalarComputeRangeBudget budget;
        assert(std::string_view(WebGpuOrdinaryScalarComputeRange::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-RANGE-VERSION");
        budget.contractVersion = 1u;
        assert(std::string_view(WebGpuOrdinaryScalarComputeRange::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-RANGE-OPT-IN");
        budget.prototypeOptIn = true;
        assert(std::string_view(WebGpuOrdinaryScalarComputeRange::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-RANGE-BUDGET");
        budget.maxProofs = 1u;
        budget.maxArtifactInputBytes = 1u;
        budget.maxWgslBytes = 1u;
        budget.maxAccessBytes = 1u;
        budget.maxArtifactCpuMetadataBytes = 1u;
        budget.maxCpuMetadataBytes = 1u;
        assert(!WebGpuOrdinaryScalarComputeRange::validateBudget(budget));
        verifyCorrelation();
        WebGpuOrdinaryComputeRecordingFixture::verifyContract();
    }

    static void verifyCorrelation()
    {
        using namespace rhi_webgpu;
        using Correlation = WebGpuScalarComputeCorrelation;
        // Mutable facts below are pure contract fixtures, never native authority.
        WebGpuNativePipelineStageFact stage;
        const core::StringView source = "actual consumed bytes";
        stage.stage = rhi::ShaderStage::Compute;
        stage.entry = "computeMain";
        stage.sourceBytes = source.size();
        const auto digest = core::Sha256::digest(source);
        std::copy(digest.begin(), digest.end(), stage.sourceSha256.begin());
        const auto originalDigest = stage.sourceSha256;
        auto sourceCheck = [&] {
            return Correlation::validateSource(stage, "computeMain", source.size(), originalDigest);
        };
        assert(!sourceCheck());
        stage.stage = rhi::ShaderStage::Vertex;
        assert(sourceCheck());
        stage.stage = rhi::ShaderStage::Compute;
        stage.entry = "otherMain";
        assert(sourceCheck());
        stage.entry = "computeMain";
        ++stage.sourceBytes;
        assert(sourceCheck());
        --stage.sourceBytes;
        ++stage.sourceSha256[0];
        assert(sourceCheck());
        --stage.sourceSha256[0];
        assert(
            Correlation::validateSource(stage, "computeMain", source.size() + 1u, originalDigest));

        shader::ScalarComputeCrossTargetProof::Facts proof;
        proof.binary.set = 2u;
        proof.binary.binding = 3u;
        proof.target.group = 0u;
        proof.target.binding = 7u;
        WebGpuNativePipelineBindingFact fact;
        fact.mapping = {2u, 3u, 0u, 7u};
        fact.shape.binding = 3u;
        fact.shape.type = rhi::DescriptorType::StorageBuffer;
        fact.shape.stages = rhi::ShaderStage::Compute;
        fact.physicalType = rhi::DescriptorType::StorageBuffer;
        shader::ShaderReflection reflection;
        reflection.webGpuBindingSpaceContractVersion = kWebGpuBindingSpaceContractVersion;
        reflection.webGpuBindingSpaceStatus = "Available";
        reflection.webGpuBindingSpaceGroupCount = 1u;
        reflection.bindings.resize(1u);
        reflection.webGpuBindingSpace.resize(1u);
        auto& logical = reflection.bindings[0];
        auto& physical = reflection.webGpuBindingSpace[0];
        logical.set = 2u;
        logical.binding = 3u;
        logical.type = shader::BindingType::StorageBuffer;
        logical.stageMask = static_cast<std::uint32_t>(rhi::ShaderStage::Compute);
        physical.set = 2u;
        physical.binding = 3u;
        physical.group = 0u;
        physical.webGpuBinding = 7u;
        physical.active = true;
        physical.physicalType = shader::BindingType::StorageBuffer;
        auto bindingCheck = [&] {
            return Correlation::validateBinding(fact, 1u, reflection, proof);
        };
        assert(!bindingCheck());
        auto field = [&](auto& value) {
            const auto saved = value;
            ++value;
            assert(bindingCheck());
            value = saved;
        };
        field(fact.mapping.logicalSet);
        field(fact.mapping.logicalBinding);
        field(fact.mapping.webGpuGroup);
        field(fact.mapping.webGpuBinding);
        field(fact.shape.arrayCount);
        fact.shape.stages = rhi::ShaderStage::Fragment;
        assert(bindingCheck());
        fact.shape.stages = rhi::ShaderStage::Compute;
        fact.shape.type = rhi::DescriptorType::ReadOnlyStorageBuffer;
        assert(bindingCheck());
        fact.shape.type = rhi::DescriptorType::StorageBuffer;
        fact.physicalType = rhi::DescriptorType::UniformBuffer;
        assert(bindingCheck());
        fact.physicalType = rhi::DescriptorType::StorageBuffer;
        fact.shape.tableBacked = true;
        assert(bindingCheck());
        fact.shape.tableBacked = false;
        reflection.webGpuBindingSpaceContractVersion = "WebGpuBindingSpace/4";
        assert(bindingCheck());
        reflection.webGpuBindingSpaceContractVersion = kWebGpuBindingSpaceContractVersion;
        reflection.webGpuBindingSpaceStatus = "Unavailable";
        assert(bindingCheck());
        reflection.webGpuBindingSpaceStatus = "Available";
        field(reflection.webGpuBindingSpaceGroupCount);
        field(logical.set);
        field(logical.binding);
        field(logical.arrayCount);
        field(logical.stageMask);
        logical.type = shader::BindingType::UniformBuffer;
        assert(bindingCheck());
        logical.type = shader::BindingType::StorageBuffer;
        field(physical.set);
        field(physical.binding);
        field(physical.group);
        field(physical.webGpuBinding);
        field(physical.arrayElement);
        field(physical.capacity);
        physical.active = false;
        assert(bindingCheck());
        physical.active = true;
        physical.requiresSampler = true;
        assert(bindingCheck());
        physical.requiresSampler = false;
        physical.depthTexture = true;
        assert(bindingCheck());
        physical.depthTexture = false;
        physical.physicalType = shader::BindingType::UniformBuffer;
        assert(bindingCheck());
        physical.physicalType = shader::BindingType::StorageBuffer;
        auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        {
            core::CpuMemoryQuotaScope scope(quota);
            core::CpuMemoryQuotaLease pressure(quota, 1u);
            assert(pressure.charged() && !sourceCheck() && !bindingCheck());
            ++physical.webGpuBinding;
            assert(bindingCheck());
            --physical.webGpuBinding;
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        }
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyActual(
        std::shared_ptr<const rhi_webgpu::WebGpuOrdinaryShaderSizeValidation> validation,
        bool smallRange,
        const char* name,
        bool pending = false)
    {
        using namespace rhi_webgpu;
        using Range = WebGpuOrdinaryScalarComputeRange;
        const shader::SingleEntryShaderLoadBudget loadBudget{
            1024u * 1024u, 4u * 1024u * 1024u, 1024u * 1024u, 4u};
        auto artifactQuota = core::makeShared<core::CpuMemoryQuota>(loadBudget.maxCpuMetadataBytes);
        core::pmr::String loadDiagnostic;
        auto artifact = shader::SingleEntryShaderArtifact::load(
            "/ordinary-pipeline-fixture/ordinary_indirect_producer/ordinary_indirect_producer.computeMain.entry.yaml",
            loadBudget,
            artifactQuota,
            loadDiagnostic);
        if (!artifact)
            std::printf("RANGE_ARTIFACT_FAILURE %s\n", loadDiagnostic.c_str());
        assert(artifact);
        const auto& wgsl = artifact->bytes(shader::ShaderArtifactRole::Wgsl);
        const auto& spirv = artifact->bytes(shader::ShaderArtifactRole::Spirv);
        shader::ScalarComputeCrossTargetProof::Facts proof;
        const core::StringView source(reinterpret_cast<const char*>(wgsl.data()), wgsl.size());
        assert(!shader::ScalarComputeCrossTargetProof::verify(
            spirv.data(), spirv.size(), source, artifact->entry(), proof));
        ComputeFixedWordsWgslFixture::verify(source, proof.binary);
        WebGpuOrdinaryScalarComputeRangeBudget budget{1u,
            true,
            1u,
            artifact->inputBytes(),
            wgsl.size(),
            proof.binary.byteCount,
            loadBudget.maxCpuMetadataBytes,
            metadataBytes};
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        const char* diagnostic = nullptr;
        auto factory = [&] {
            return Range::prepare(validation, *artifact, budget, quota, diagnostic);
        };
        auto expect = [&](const char* id) {
            const auto live = quota->liveBytes();
            auto result = factory();
            if (result || !diagnostic || std::string_view(diagnostic) != id)
                std::printf("RANGE_EXPECTED %s GOT %s\n", id, diagnostic ? diagnostic : "Success");
            assert(!result && std::string_view(diagnostic) == id && quota->liveBytes() == live);
        };
        if (pending) {
            expect("RHI-WEBGPU-ORDINARY-VALIDATION-PENDING");
            return;
        }
        auto canonical = validation;
        validation =
            std::shared_ptr<const WebGpuOrdinaryShaderSizeValidation>(quota, canonical.get());
        expect("RHI-WEBGPU-ORDINARY-RANGE-VALIDATION-OWNER");
        validation = canonical;
        canonical.reset();
        budget.maxArtifactCpuMetadataBytes--;
        expect("RHI-WEBGPU-ORDINARY-RANGE-ARTIFACT-QUOTA");
        budget.maxArtifactCpuMetadataBytes++;
        budget.maxCpuMetadataBytes--;
        expect("RHI-WEBGPU-ORDINARY-RANGE-QUOTA");
        budget.maxCpuMetadataBytes++;
        budget.maxArtifactInputBytes--;
        expect("RHI-WEBGPU-ORDINARY-RANGE-INPUT-CAPACITY");
        budget.maxArtifactInputBytes++;
        budget.maxWgslBytes--;
        expect("RHI-WEBGPU-ORDINARY-RANGE-INPUT-CAPACITY");
        budget.maxWgslBytes++;
        budget.maxAccessBytes--;
        expect("RHI-WEBGPU-ORDINARY-RANGE-ACCESS-CAPACITY");
        budget.maxAccessBytes++;
        if (smallRange) {
            assert(!validation->diagnostic()); // Native static4-byte minimum passed.
            expect("RHI-WEBGPU-ORDINARY-RANGE-BUFFER-TOO-SMALL");
            std::printf("BINDLESS_RHI_ORDINARY_SCALAR_RANGE_OK %s 20 BufferTooSmall\n", name);
            return;
        }
        CpuQuotaFactoryFixture admission(quota);
        const auto peak = admission.measure(factory);
        admission.verify(peak, factory);
        {
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes());
            assert(pressure.charged());
            expect("RHI-WEBGPU-ORDINARY-RANGE-ALLOCATION");
        }
        auto result = factory();
        assert(result && !result->validate());
        assert(result->facts().byteCount == 20u && !result->facts().byteOffset);
        assert(result->facts().fixedWords == proof.binary.fixedWords && !result->physicalGroup()
            && !result->physicalBinding());
        assert(std::equal(artifact->identity().begin(),
            artifact->identity().end(),
            result->publicationIdentity().begin()));
        artifact.reset();
        assert(!artifactQuota->liveBytes());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        {
            core::CpuMemoryQuotaScope scope(quota);
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
            assert(pressure.charged() && !result->validateDispatch(1u, 1u, 1u));
            for (const auto invalid : {0u, 2u, UINT32_MAX}) {
                assert(result->validateDispatch(invalid, 1u, 1u));
                assert(result->validateDispatch(1u, invalid, 1u));
                assert(result->validateDispatch(1u, 1u, invalid));
            }
            assert(std::string_view(result->executionDiagnostic())
                == "RHI-WEBGPU-ORDINARY-GROUP-RECORDING-UNPREPARED");
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        }
        assert(!retainedRange_);
        retainedRange_ = Range::shareOriginal(std::move(result));
        retainedQuota_ = quota;
        assert(retainedRange_ && retainedRange_->hasQuotaBoundSharedOwner(retainedRange_));
        WebGpuOriginalScalarAccessFixture::verifyCompute(retainedRange_);
        WebGpuOrdinaryComputeRecordingFixture::verifyActual(retainedRange_);
        std::printf("BINDLESS_RHI_ORDINARY_SCALAR_RANGE_OK %s 20 %llu\n",
            name,
            static_cast<unsigned long long>(peak));
    }

    static const auto& originalRange() noexcept { return retainedRange_; }

    static void verifyRetainedDiagnostic(const char* id)
    {
        assert(retainedRange_ && std::string_view(retainedRange_->validate()) == id);
        assert(std::string_view(retainedRange_->validateDispatch(1u, 1u, 1u)) == id);
        assert(std::string_view(retainedRange_->executionDiagnostic()) == id);
    }

    static void retainGraphQuotas(std::array<std::shared_ptr<core::CpuMemoryQuota>, 3> quotas)
    {
        retainedGraphQuotas_ = std::move(quotas);
    }

    static void verifyActualDeviceLoss()
    {
        verifyRetainedDiagnostic("RHI-WEBGPU-ORDINARY-VALIDATION-DEVICE-LOST");
        retainedRange_.reset();
        assert(!retainedQuota_->liveBytes());
        retainedQuota_.reset();
        for (auto& quota : retainedGraphQuotas_) {
            assert(quota && !quota->liveBytes());
            quota.reset();
        }
    }
#endif
private:
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static inline std::shared_ptr<rhi_webgpu::WebGpuOrdinaryScalarComputeRange> retainedRange_;
    static inline std::shared_ptr<core::CpuMemoryQuota> retainedQuota_;
    static inline std::array<std::shared_ptr<core::CpuMemoryQuota>, 3> retainedGraphQuotas_;
#endif
    static constexpr std::uint64_t metadataBytes = 65536u;
};
} // namespace arp::test
