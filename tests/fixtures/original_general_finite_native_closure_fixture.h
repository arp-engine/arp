#pragma once
#include "cpu_quota_factory_fixture.h"
#include "original_descriptor_buffer_binding_fixture.h"
#include "rhi/backends/webgpu/webgpu_canonical_issuer_preparation.h"
#include "rhi/backends/webgpu/webgpu_canonical_native_closure.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class OriginalGeneralFiniteNativeClosureFixture final {
    using Closure = rhi_webgpu::WebGpuCanonicalNativeClosure;
    using Budget = rhi::OriginalCommandNativeClosureBudget;
    using Contract = rhi::OriginalCommandNativeClosureContract;

    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "General Closure9: %s\n", id);
        assert(!id);
    }

public:
    static void verifyContract()
    {
        Budget budget{9u, true, 6u, 8u, 128u, 1024u, 65536u, 1u, 1u};
        budget.generalShaderResourcesVersion = 2u;
        budget.generalShaderResourcesOptIn = true;
        budget.shaderBufferContent = {2u, true, 1u, 16u, 528u};
        budget.shaderTextureContent = {1u, true, 1u, 64u, 20u, 2u, 2u, 5u};
        require(Contract::validateIntent(budget));
        for (const auto version : {0u, 1u, 3u, 4u, UINT32_MAX}) {
            auto bad = budget;
            bad.shaderBufferContent.contractVersion = version;
            assert(Contract::validateIntent(bad));
        }
        for (std::uint32_t field = 0u; field < 12u; ++field) {
            auto bad = budget;
            switch (field) {
            case 0u:
                bad.contractVersion = 8u;
                break;
            case 1u:
                bad.generalShaderResourcesVersion = 1u;
                break;
            case 2u:
                bad.generalShaderResourcesOptIn = false;
                break;
            case 3u:
                bad.shaderBufferContent = {};
                break;
            case 4u:
                bad.shaderBufferContent.prototypeOptIn = false;
                break;
            case 5u:
                bad.shaderBufferContent.maxSources = 0u;
                break;
            case 6u:
                bad.shaderBufferContent.maxRetainedOwners = 0u;
                break;
            case 7u:
                bad.shaderBufferContent.maxInitializedBytes = 0u;
                break;
            case 8u:
                bad.maxReadbackSources = 1u;
                break;
            case 9u:
                bad.maxReadbackCopies = 1u;
                break;
            case 10u:
                bad.indexContent = {1u, true, 1u, 1u, 1u};
                break;
            case 11u:
                bad.shaderTextureContent.contractVersion = 2u;
                break;
            }
            assert(Contract::validateIntent(bad));
        }
        auto legacy = budget;
        legacy.contractVersion = 8u;
        legacy.generalShaderResourcesVersion = 1u;
        legacy.shaderBufferContent.contractVersion = 1u;
        require(Contract::validateIntent(legacy));
        auto missingTexture = budget;
        missingTexture.shaderTextureContent = {};
        assert(Contract::validateIntent(missingTexture));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision = rhi::semantics::OriginalCommandNativeClosureLowering::
                generalResourcesLossBoundLowering(backend, 2u);
            assert(decision.resourceClosureAvailable == (backend == rhi::Backend::WebGPU)
                && !decision.recordingIssuerAvailable && !decision.executionAvailable
                && decision.diagnostic);
            assert((decision.disposition
                       == rhi::semantics::OriginalCommandNativeClosureLowering::Disposition::
                           ValidatorOnly)
                == (backend == rhi::Backend::Null));
            assert(!rhi::semantics::OriginalCommandNativeClosureLowering::
                    generalResourcesLossBoundLowering(backend, 3u)
                        .resourceClosureAvailable);
        }
        assert(Contract::lossJoinVersion(9u, false, true) == 1u);
        assert(!Contract::lossJoinVersion(9u, true, false)
            && !Contract::lossJoinVersion(9u, true, true));
        for (std::uint32_t freeze = 1u; freeze <= 6u; ++freeze)
            assert(
                !rhi::OriginalTypedDependencyProjectionContract::closureProfileMatches(freeze, 9u));
        assert(rhi::OriginalTypedDependencyProjectionContract::closureProfileMatches(7u, 9u));
        std::puts(
            "BINDLESS_RHI_GENERAL_CLOSURE9_CONTRACT_OK exact-finite-profile custody2 legacy8-isolated four-backends freeze-native-closed");
    }

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
    static Budget budgetFor(const std::shared_ptr<const rhi::OriginalCommandStateTransaction>& root,
        const std::shared_ptr<const rhi_webgpu::WebGpuCanonicalBufferUploadSource>& initialized,
        const Closure::ShaderTextureContentInput& texture,
        std::uint64_t retainedBytes,
        std::uint32_t ownerCeiling,
        bool graphics = false)
    {
        using Initialized = rhi_webgpu::WebGpuCanonicalBufferUploadSource;
        Budget budget{graphics ? 10u : 9u,
            true,
            graphics ? 8u : 6u,
            graphics ? 10u : 8u,
            ownerCeiling,
            retainedBytes,
            root->originalMetadataQuota()->limitBytes(),
            1u,
            1u};
        budget.generalShaderResourcesVersion = 2u;
        budget.generalShaderResourcesOptIn = true;
        budget.shaderBufferContent = {2u,
            true,
            1u,
            Initialized::initializedIndexOwnerBound,
            initialized->initializedPayloadBytes()};
        budget.shaderTextureContent = {1u,
            true,
            1u,
            static_cast<std::uint32_t>(texture.issuer->retainedOwnerCount()
                + rhi_webgpu::WebGpuCanonicalTextureContentUploadSource::retainedOwnerBound + 1u),
            20u,
            2u,
            2u,
            5u};
        return budget;
    }

    template <class Texture>
    static auto verifyActual(const std::shared_ptr<rhi::OriginalCommandStateTransaction>& root,
        const std::shared_ptr<const rhi::OriginalCommandStreamProgram>& program,
        const rhi::GpuDevice& device,
        const rhi::OriginalDeviceNativeLossObservation& loss,
        const std::shared_ptr<const rhi_webgpu::WebGpuCanonicalBufferUploadSource>& initialized,
        const Texture& texture,
        std::uint64_t retainedBytes,
        std::uint32_t ownerCeiling,
        bool graphics = false)
    {
        using Initialized = rhi_webgpu::WebGpuCanonicalBufferUploadSource;
        const auto recording = root->originalMetadataQuota();
        const auto baseline = recording->liveBytes();
        const char* id = nullptr;
        Budget budget =
            budgetFor(root, initialized, texture, retainedBytes, ownerCeiling, graphics);
        std::array<Closure::ShaderBufferContentInput, 1u> contents{{{0u, initialized}}};
        std::array<Closure::ShaderTextureContentInput, 1u> textures{{texture}};
        const auto prepare = [&](const Budget& b,
                                 const Closure::ShaderBufferContentInput* buffers,
                                 std::size_t bufferCount,
                                 const Closure::ShaderTextureContentInput* sampled,
                                 std::size_t sampledCount) {
            return graphics ? Closure::prepareOriginalFiniteGraphicsLossBound(root,
                                  program,
                                  device,
                                  b,
                                  loss,
                                  id,
                                  buffers,
                                  bufferCount,
                                  sampled,
                                  sampledCount)
                            : Closure::prepareOriginalGeneralResourcesLossBound(root,
                                  program,
                                  device,
                                  b,
                                  loss,
                                  id,
                                  buffers,
                                  bufferCount,
                                  sampled,
                                  sampledCount);
        };
        const auto make = [&](const Budget& b) {
            return prepare(b, contents.data(), contents.size(), textures.data(), textures.size());
        };
        const auto reject = [&](const Budget& b) {
            assert(!make(b) && id && recording->liveBytes() == baseline && root->journalEmpty());
            require(root->validate());
        };
        auto bad = budget;
        bad.contractVersion = 8u;
        bad.generalShaderResourcesVersion = 1u;
        bad.shaderBufferContent.contractVersion = 1u;
        reject(bad);
        bad = budget;
        bad.shaderBufferContent.contractVersion = 1u;
        reject(bad);
        bad = budget;
        --bad.shaderBufferContent.maxInitializedBytes;
        reject(bad);
        bad = budget;
        --bad.shaderBufferContent.maxRetainedOwners;
        reject(bad);
        bad = budget;
        bad.shaderBufferContent.maxSources = 0u;
        reject(bad);
        bad = budget;
        bad.shaderBufferContent.prototypeOptIn = false;
        reject(bad);
        contents[0].sourceIndex = 1u;
        reject(budget);
        contents[0].sourceIndex = 0u;
        contents[0].source = std::shared_ptr<const Initialized>(recording, initialized.get());
        reject(budget);
        contents[0].source = initialized;
        const std::array duplicate{contents[0], contents[0]};
        auto duplicateBudget = budget;
        duplicateBudget.shaderBufferContent.maxSources = 2u;
        duplicateBudget.shaderBufferContent.maxRetainedOwners =
            2u * Initialized::initializedIndexOwnerBound;
        assert(!prepare(duplicateBudget,
                   duplicate.data(),
                   duplicate.size(),
                   textures.data(),
                   textures.size())
            && id && recording->liveBytes() == baseline);
        assert(core::StringView(id) == "RHI-WEBGPU-GENERAL-CONTENT-DUPLICATE");
        assert(!prepare(budget, nullptr, 0u, textures.data(), textures.size()) && id
            && recording->liveBytes() == baseline);
        assert(!prepare(budget, contents.data(), contents.size(), nullptr, 0u) && id
            && recording->liveBytes() == baseline);
        textures[0].completed.reset();
        reject(budget);
        textures[0].completed.emplace(*texture.completed);
        textures[0].uploadRoot = root;
        reject(budget);
        textures[0].uploadRoot = texture.uploadRoot;
        textures[0].stages =
            graphics ? rhi::PipelineStage::ComputeShader : rhi::PipelineStage::FragmentShader;
        reject(budget);
        textures[0].stages = texture.stages;
        if (graphics) {
            auto computeBudget = budget;
            computeBudget.contractVersion = 9u;
            reject(computeBudget);
            assert(!Closure::prepareOriginalGeneralResourcesLossBound(root,
                       program,
                       device,
                       budget,
                       loss,
                       id,
                       contents.data(),
                       contents.size(),
                       textures.data(),
                       textures.size())
                && core::StringView(id) == "RHI-WEBGPU-CANONICAL-CLOSURE-GENERAL-VERSION");
            for (std::uint32_t freeze = 1u; freeze <= 7u; ++freeze)
                assert(!rhi::OriginalTypedDependencyProjectionContract::closureProfileMatches(
                    freeze, 10u));
        }
        const auto create = [&] {
            return make(budget);
        };
        auto admitted = create();
        require(id);
        assert(admitted);
        admitted.reset();
        assert(recording->liveBytes() == baseline);
        CpuQuotaFactoryFixture pressure(recording);
        const auto exact = pressure.measureSingleUse([] {}, create);
        pressure.verify(exact, create);
        {
            core::CpuMemoryQuotaLease full(recording, recording->limitBytes() - baseline);
            assert(full.charged() && !create() && id);
        }
        auto closure = create();
        require(id);
        assert(closure);
        assert(closure->closureContractVersion() == (graphics ? 10u : 9u)
            && closure->resourceBindingCount() == (graphics ? 8u : 6u)
            && closure->pipelineBindingCount() == 1u && closure->descriptorBindingCount() == 1u
            && closure->nativePayloadCount() == (graphics ? 10u : 8u)
            && closure->retainedResourceBytes() == retainedBytes
            && !closure->nativeExecutionProven() && closure->executionDiagnostic());
        budget.maxRetainedOwners = closure->retainedOwnerCount();
        const auto live = recording->liveBytes();
        for (const auto member : {&Budget::maxResourceBindings,
                 &Budget::maxNativePayloadObjects,
                 &Budget::maxRetainedOwners,
                 &Budget::maxPipelineSources,
                 &Budget::maxDescriptorSources}) {
            bad = budget;
            --(bad.*member);
            assert(!make(bad) && id && recording->liveBytes() == live);
        }
        bad = budget;
        --bad.maxRetainedResourceBytes;
        assert(!make(bad) && id && recording->liveBytes() == live);
        bad = budget;
        --bad.maxCpuMetadataBytes;
        assert(!make(bad) && id && recording->liveBytes() == live);
        auto forged = std::shared_ptr<const Closure>(recording, closure.get());
        assert(Closure::validateOriginal(forged));
        forged.reset();
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            require(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{recording,
                    program->originalMetadataQuota(),
                    root->originalGraph()->originalMetadataQuota(),
                    root->originalGraph()->originalInputs()->originalMetadataQuota()},
                [&]() noexcept -> const char* {
                    return closure->validate();
                }));
        });
        std::printf(graphics
                ? "BINDLESS_RHI_GENERAL_GRAPHICS_CLOSURE10_OK sources10 resources8 pipeline1 descriptor1 custody2 complete-backing%llu closureBytes%llu retainedBytes%llu owners%u exact-less-exhausted no-root-journal-mutation hot0-query0 freeze-native-closed\n"
                : "BINDLESS_RHI_GENERAL_CLOSURE9_OK sources8 resources6 pipeline1 descriptor1 custody2 complete-backing%llu closureBytes%llu retainedBytes%llu owners%u exact-less-exhausted no-root-journal-mutation hot0-query0 freeze-native-closed\n",
            static_cast<unsigned long long>(initialized->initializedPayloadBytes()),
            static_cast<unsigned long long>(exact),
            static_cast<unsigned long long>(retainedBytes),
            closure->retainedOwnerCount());
        return closure;
    }
#endif
};
} // namespace arp::test
