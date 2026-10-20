#pragma once
#include "cpu_quota_factory_fixture.h"
#include "graphics_triangle_storage_proof_fixture.h"
#include "rhi/backends/webgpu/webgpu_native_ordinary_graphics_triangle_range.h"
#include "webgpu_ordinary_graphics_recording_fixture.h"
#include "webgpu_original_scalar_access_fixture.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class WebGpuOrdinaryGraphicsTriangleRangeFixture final {
public:
    static void verifyContract()
    {
        WebGpuOrdinaryGraphicsRecordingFixture::verifyContract();
        using namespace rhi_webgpu;
        WebGpuOrdinaryGraphicsTriangleRangeBudget budget;
        assert(std::string_view(WebGpuOrdinaryGraphicsTriangleRange::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-RANGE-VERSION");
        budget.contractVersion = 1u;
        assert(std::string_view(WebGpuOrdinaryGraphicsTriangleRange::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-RANGE-OPT-IN");
        budget.prototypeOptIn = true;
        assert(std::string_view(WebGpuOrdinaryGraphicsTriangleRange::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-RANGE-BUDGET");
        budget.maxProofs = 1u;
        budget.maxArtifactInputBytes = 1u;
        budget.maxWgslBytes = 1u;
        budget.maxAccessBytes = 1u;
        budget.maxArtifactCpuMetadataBytes = 1u;
        budget.maxCpuMetadataBytes = 1u;
        assert(!WebGpuOrdinaryGraphicsTriangleRange::validateBudget(budget));
        verifyCorrelation();
    }

    static void verifyCorrelation()
    {
        using namespace rhi_webgpu;
        using Correlation = WebGpuGraphicsTriangleCorrelation;
        // Pure mutable contract facts; never substitute for original native objects.
        core::pmr::Vector<WebGpuNativePipelineStageFact> stages(&core::defaultCpuMemoryResource());
        stages.resize(2u);
        int originalModule = 0, otherModule = 0;
        const core::StringView source = "combined original source";
        const auto digest = core::Sha256::digest(source);
        std::array<std::uint8_t, core::Sha256::digestByteCount> expected;
        std::copy(digest.begin(), digest.end(), expected.begin());
        for (auto& stage : stages) {
            stage.moduleIdentity = &originalModule;
            stage.sourceBytes = source.size();
            stage.sourceSha256 = expected;
        }
        stages[0].stage = rhi::ShaderStage::Vertex;
        stages[0].entry = "vertexMain";
        stages[1].stage = rhi::ShaderStage::Fragment;
        stages[1].entry = "fragmentMain";
        auto sourceCheck = [&] {
            return Correlation::validateSource(
                stages, "vertexMain", "fragmentMain", source.size(), expected);
        };
        assert(!sourceCheck());
        for (auto& stage : stages) {
            ++stage.sourceBytes;
            assert(sourceCheck());
            --stage.sourceBytes;
            ++stage.sourceSha256[0];
            assert(sourceCheck());
            --stage.sourceSha256[0];
            stage.moduleIdentity = &otherModule;
            assert(sourceCheck());
            stage.moduleIdentity = &originalModule;
            stage.moduleIdentity = nullptr;
            assert(sourceCheck());
            stage.moduleIdentity = &originalModule;
            auto saved = stage.stage;
            stage.stage = rhi::ShaderStage::Compute;
            assert(sourceCheck());
            stage.stage = saved;
        }
        stages[1].stage = rhi::ShaderStage::Vertex;
        assert(sourceCheck());
        stages[1].stage = rhi::ShaderStage::Fragment;
        stages[1].entry = "otherMain";
        assert(sourceCheck());
        stages[1].entry = "fragmentMain";
        std::swap(stages[0], stages[1]);
        assert(!sourceCheck());
        std::swap(stages[0], stages[1]);
        shader::GraphicsTriangleStorageCrossTargetProof::Facts proof;
        proof.fragment.set = 2u;
        proof.fragment.binding = 3u;
        proof.target.group = 0u;
        proof.target.binding = 7u;
        WebGpuNativePipelineBindingFact fact;
        fact.mapping = {2u, 3u, 0u, 7u};
        fact.shape.binding = 3u;
        fact.shape.type = rhi::DescriptorType::ReadOnlyStorageBuffer;
        fact.shape.stages = rhi::ShaderStage::Fragment;
        fact.physicalType = rhi::DescriptorType::ReadOnlyStorageBuffer;
        shader::ShaderReflection vertex, fragment;
        for (auto* reflection : {&vertex, &fragment}) {
            reflection->webGpuBindingSpaceContractVersion = kWebGpuBindingSpaceContractVersion;
            reflection->webGpuBindingSpaceStatus = "Available";
            reflection->webGpuBindingSpace.resize(1u);
            reflection->webGpuBindingSpace[0].set = 2u;
            reflection->webGpuBindingSpace[0].binding = 3u;
        }
        fragment.webGpuBindingSpaceGroupCount = 1u;
        fragment.bindings.resize(1u);
        auto& logical = fragment.bindings[0];
        auto& physical = fragment.webGpuBindingSpace[0];
        logical.set = 2u;
        logical.binding = 3u;
        logical.type = shader::BindingType::ReadOnlyStorageBuffer;
        logical.stageMask = static_cast<std::uint32_t>(rhi::ShaderStage::Fragment);
        physical.group = 0u;
        physical.webGpuBinding = 7u;
        physical.active = true;
        physical.physicalType = shader::BindingType::ReadOnlyStorageBuffer;
        auto bindingCheck = [&] {
            return Correlation::validateBinding(fact, 1u, vertex, fragment, proof);
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
        field(fact.shape.binding);
        field(fact.shape.arrayCount);
        fact.shape.stages = rhi::ShaderStage::Vertex;
        assert(bindingCheck());
        fact.shape.stages = rhi::ShaderStage::Fragment;
        fact.shape.type = rhi::DescriptorType::StorageBuffer;
        assert(bindingCheck());
        fact.shape.type = rhi::DescriptorType::ReadOnlyStorageBuffer;
        fact.physicalType = rhi::DescriptorType::StorageBuffer;
        assert(bindingCheck());
        fact.physicalType = rhi::DescriptorType::ReadOnlyStorageBuffer;
        fact.shape.tableBacked = true;
        assert(bindingCheck());
        fact.shape.tableBacked = false;
        for (auto* reflection : {&vertex, &fragment}) {
            reflection->webGpuBindingSpaceContractVersion = "WebGpuBindingSpace/4";
            assert(bindingCheck());
            reflection->webGpuBindingSpaceContractVersion = kWebGpuBindingSpaceContractVersion;
            reflection->webGpuBindingSpaceStatus = "Unavailable";
            assert(bindingCheck());
            reflection->webGpuBindingSpaceStatus = "Available";
            field(reflection->webGpuBindingSpaceGroupCount);
            field(reflection->pushConstantSizeBytes);
        }
        field(logical.set);
        field(logical.binding);
        field(logical.arrayCount);
        field(logical.stageMask);
        logical.type = shader::BindingType::StorageBuffer;
        assert(bindingCheck());
        logical.type = shader::BindingType::ReadOnlyStorageBuffer;
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
        physical.physicalType = shader::BindingType::StorageBuffer;
        assert(bindingCheck());
        physical.physicalType = shader::BindingType::ReadOnlyStorageBuffer;
        auto& dead = vertex.webGpuBindingSpace[0];
        dead.active = true;
        assert(bindingCheck());
        dead.active = false;
        field(dead.set);
        field(dead.binding);
        vertex.bindings.resize(1u);
        assert(bindingCheck());
        vertex.bindings.clear();
        vertex.vertexAttributes.resize(1u);
        assert(bindingCheck());
        vertex.vertexAttributes.clear();
        auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
        core::CpuMemoryQuotaScope scope(quota);
        core::CpuMemoryQuotaLease pressure(quota, 1u);
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!sourceCheck() && !bindingCheck());
        field(dead.binding);
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(previous);
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyActual(
        std::shared_ptr<const rhi_webgpu::WebGpuOrdinaryShaderSizeValidation> validation,
        const char* name,
        bool pending = false,
        const char* expectedFailure = nullptr)
    {
        using namespace rhi_webgpu;
        using Range = WebGpuOrdinaryGraphicsTriangleRange;
        const shader::GraphicsShaderModuleLoadBudget loadBudget{
            1024u * 1024u, 4u * 1024u * 1024u, 1024u * 1024u, 4u, 4u, 512u, 2u};
        auto artifactQuota = core::makeShared<core::CpuMemoryQuota>(loadBudget.maxCpuMetadataBytes);
        core::pmr::String loadDiagnostic;
        auto artifact = shader::GraphicsShaderModuleArtifact::load(
            "/ordinary-pipeline-fixture/ordinary_graphics_storage_read/ordinary_graphics_storage_read.graphics.module.yaml",
            loadBudget,
            artifactQuota,
            loadDiagnostic);
        if (!artifact)
            std::printf("RANGE_ARTIFACT_FAILURE %s\n", loadDiagnostic.c_str());
        assert(artifact);
        const auto& wgsl = artifact->bytes(shader::ShaderArtifactRole::Wgsl);
        const auto& vertex = artifact->vertex()->bytes(shader::ShaderArtifactRole::Spirv);
        const auto& fragment = artifact->fragment()->bytes(shader::ShaderArtifactRole::Spirv);
        shader::GraphicsTriangleStorageCrossTargetProof::Facts proof;
        const core::StringView source(reinterpret_cast<const char*>(wgsl.data()), wgsl.size());
        assert(!shader::GraphicsTriangleStorageCrossTargetProof::verify(vertex.data(),
            vertex.size(),
            fragment.data(),
            fragment.size(),
            source,
            artifact->vertex()->entry(),
            artifact->fragment()->entry(),
            proof));
        GraphicsTriangleStorageProofFixture::verify(vertex, fragment, source);
        WebGpuOrdinaryGraphicsTriangleRangeBudget budget{1u,
            true,
            1u,
            artifact->inputBytes(),
            wgsl.size(),
            proof.fragment.byteCount,
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
        if (expectedFailure) {
            expect(expectedFailure);
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
        assert(result->facts().fragment.byteCount == 4u && !result->facts().fragment.byteOffset);
        assert(result->facts().vertex.positions == proof.vertex.positions
            && !result->facts().target.group && !result->facts().target.binding);
        assert(std::equal(artifact->identity().begin(),
            artifact->identity().end(),
            result->publicationIdentity().begin()));
        artifact.reset();
        assert(!artifactQuota->liveBytes());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        {
            core::CpuMemoryQuotaScope scope(quota);
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
            assert(pressure.charged() && !result->validateDraw(3u, 1u, 0u, 0u));
            for (const auto invalid : {0u, 2u, UINT32_MAX}) {
                assert(result->validateDraw(invalid, 1u, 0u, 0u));
                assert(result->validateDraw(3u, invalid, 0u, 0u));
            }
            assert(result->validateDraw(3u, 1u, 1u, 0u) && result->validateDraw(3u, 1u, 0u, 1u)
                && result->validateDraw(3u, 1u, 0u, 0u, true));
            assert(std::string_view(result->executionDiagnostic())
                == "RHI-WEBGPU-ORDINARY-GROUP-RECORDING-UNPREPARED");
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        }
        assert(!retainedRange_);
        retainedRange_ = Range::shareOriginal(std::move(result));
        retainedQuota_ = quota;
        WebGpuOriginalScalarAccessFixture::verifyGraphics(retainedRange_);

        std::printf("BINDLESS_RHI_ORDINARY_GRAPHICS_RANGE_OK %s 4 %llu\n",
            name,
            static_cast<unsigned long long>(peak));
    }

    static const auto& originalRange() noexcept { return retainedRange_; }

    static void verifyRetainedDiagnostic(const char* id)
    {
        assert(retainedRange_ && std::string_view(retainedRange_->validate()) == id);
        assert(std::string_view(retainedRange_->validateDraw(3u, 1u, 0u, 0u)) == id);
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
        std::puts("BINDLESS_RHI_ORDINARY_GRAPHICS_RANGE_LOSS_OK");
    }
#endif
private:
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static inline std::shared_ptr<rhi_webgpu::WebGpuOrdinaryGraphicsTriangleRange> retainedRange_;
    static inline std::shared_ptr<core::CpuMemoryQuota> retainedQuota_;
    static inline std::array<std::shared_ptr<core::CpuMemoryQuota>, 3> retainedGraphQuotas_;
#endif
    static constexpr std::uint64_t metadataBytes = 65536u;
};
} // namespace arp::test
