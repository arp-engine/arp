#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/native_validation.h"
#include "rhi/backends/webgpu/webgpu_pipeline_preparation.h"
#include "webgpu_async_wait_fixture.h"

#include <cstdio>

namespace arp::test {
class OriginalDepthTableExpansionNativeEmissionFixture final {
    using Definition = rhi_webgpu::WebGpuOriginalDepthTableExpansionPipelineDefinition;
    using Contract = rhi::OriginalDepthTableExpansionNativeEmissionContract;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    // Verification-only scope observer; never an original creation actor/seal.
    class ScopeProbe final {
        friend class rhi_webgpu::WebGpuArtifactCreationScopeCallbacks<ScopeProbe>;

    public:
        explicit ScopeProbe(const std::shared_ptr<core::CpuMemoryQuota>& quota)
        {
            callbacks.prepare(quota);
            assert(lifecycle.begin());
        }

        core::runtime::CreationReceiptLifecycle lifecycle{
            rhi_webgpu::WebGpuNativeErrorScopeBatch::count};
        rhi_webgpu::WebGpuArtifactCreationScopeCallbacks<ScopeProbe> callbacks;

    private:
        void observeCreationScope(std::uint32_t i, bool success) noexcept
        {
            lifecycle.observe(
                i, success, "TEST-TABLE-EMISSION-NATIVE-SCOPE", []() noexcept -> const char* {
                    return nullptr;
                });
        }
    };
#endif
public:
    template <class Factory, class Desc>
    static void verify(const std::shared_ptr<const Definition::Space>& space,
        const std::shared_ptr<rhi::PipelineLayout>& layout,
        const Desc& desc,
        const Definition::Preflight::Budget& preflight,
        bool nativeEmissionRequested)
    {
        constexpr bool compute = std::is_same_v<Desc, rhi::ComputePipelineDesc>;
        const char* id = nullptr;
        Definition::Budget definitionBudget{1u, true, 65536u, 256u, preflight};
        auto definitionQuota =
            core::makeShared<core::CpuMemoryQuota>(definitionBudget.maxCpuMetadataBytes);
        auto definition = Definition::prepareOriginal(
            nativeEmissionRequested ? rhi::Backend::WebGPU : rhi::Backend::Null,
            space,
            layout,
            desc,
            definitionBudget,
            definitionQuota,
            id);
        assert(definition);
        Contract::Budget budget{1u, true, definition->requirements().shaderSourceBytes, 65536u};
        auto nativeQuota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
        core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
        diagnostic.reserve(256u);
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            auto decision = Contract::lowering(backend, definition->metadataDecision());
            assert(!decision.acceptanceAvailable && !decision.executionAvailable);
            assert(decision.validatorOnly == (backend == rhi::Backend::Null));
            assert(decision.emissionAvailable
                == (nativeEmissionRequested && backend == rhi::Backend::WebGPU));
        }
        if constexpr (!compute) {
            assert(!Contract::validateState(desc));
            for (std::uint32_t mutation = 0u; mutation < 10u; ++mutation) {
                auto changed = rhi::RenderPipelineStateCopy::copy(desc);
                if (mutation == 0u)
                    changed.cullMode = static_cast<rhi::CullMode>(-1);
                if (mutation == 1u)
                    changed.frontFace = static_cast<rhi::FrontFace>(2);
                if (mutation == 2u)
                    changed.colorTargets[0].writeMask = static_cast<rhi::ColorWriteMask>(16u);
                if (mutation == 3u)
                    changed.colorTargets[0].colorBlend.sourceFactor =
                        static_cast<rhi::BlendFactor>(-1);
                if (mutation == 4u)
                    changed.colorTargets[0].alphaBlend.destinationFactor =
                        static_cast<rhi::BlendFactor>(6);
                if (mutation == 5u)
                    changed.colorTargets[0].colorBlend.operation = static_cast<rhi::BlendOp>(5);
                if (mutation == 6u)
                    changed.depthStencil.depthCompare = static_cast<rhi::CompareOp>(8);
                if (mutation == 7u)
                    changed.depthStencil.front.compare = static_cast<rhi::CompareOp>(-1);
                if (mutation == 8u)
                    changed.depthStencil.back.passOp = static_cast<rhi::StencilOp>(6);
                if (mutation == 9u)
                    changed.depthStencil.front.failOp = static_cast<rhi::StencilOp>(-1);
                assert(Contract::validateState(changed));
            }
        }
        auto failed = [&](const auto& d, const auto& b, const auto& q, core::StringView expected) {
            assert(!Factory::createTableExpansion(d, b, q, diagnostic) && diagnostic == expected
                && !nativeQuota->liveBytes());
        };
        auto invalid = budget;
        invalid.contractVersion = 0u;
        failed(definition, invalid, nativeQuota, "RHI-ORIGINAL-TABLE-EMISSION-VERSION");
        invalid = budget;
        invalid.prototypeOptIn = false;
        failed(definition, invalid, nativeQuota, "RHI-ORIGINAL-TABLE-EMISSION-OPT-IN");
        invalid = budget;
        invalid.maxCpuMetadataBytes = 0u;
        failed(definition, invalid, nativeQuota, "RHI-ORIGINAL-TABLE-EMISSION-BUDGET");
        auto forged = std::shared_ptr<const Definition>(nativeQuota, definition.get());
        failed(forged, budget, nativeQuota, "RHI-ORIGINAL-TABLE-DEFINITION-OWNER");
        failed(definition,
            budget,
            std::shared_ptr<core::CpuMemoryQuota>{},
            nativeEmissionRequested ? "RHI-ORIGINAL-TABLE-EMISSION-QUOTA"
                                    : "RHI-ORIGINAL-TABLE-EMISSION-DEFINITION-BACKEND");
        if (!nativeEmissionRequested) {
            failed(
                definition, budget, nativeQuota, "RHI-ORIGINAL-TABLE-EMISSION-DEFINITION-BACKEND");
            return;
        }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        const auto& input = [&]() -> const auto& {
            if constexpr (compute)
                return space->computeInput();
            else
                return space->graphicsInput();
        }();
        for (const auto& domain : std::array{definitionQuota,
                 space->quota(),
                 input->quota(),
                 input->tableExpansion()->quota()}) {
            invalid = budget;
            invalid.maxCpuMetadataBytes = domain->limitBytes();
            failed(definition, invalid, domain, "RHI-ORIGINAL-TABLE-EMISSION-QUOTA-DOMAIN");
        }
        invalid = budget;
        --invalid.maxTargetBytes;
        failed(definition, invalid, nativeQuota, "RHI-ORIGINAL-TABLE-EMISSION-TARGET-BUDGET");
        auto callbackQuota = core::makeShared<core::CpuMemoryQuota>(65536u);
        auto native = [&](bool rejectedScope = false) {
            auto probe = core::makeShared<ScopeProbe>(callbackQuota);
            auto device = emscripten_webgpu_get_device();
            assert(device);
            rhi_webgpu::WebGpuNativeErrorScopeBatch::push(device);
            auto result =
                Factory::createTableExpansion(definition, budget, nativeQuota, diagnostic);
            probe->callbacks.pop(device, probe);
            wgpuDeviceRelease(device);
            assert(WebGpuAsyncWaitFixture::until([&] {
                return probe->lifecycle.evidence().allReceived();
            }));
            assert(probe->lifecycle.phase()
                == (rejectedScope ? core::runtime::CreationReceiptLifecycle::Phase::Rejected
                                  : core::runtime::CreationReceiptLifecycle::Phase::Ready));
            return result;
        };
        CpuQuotaFactoryFixture pressure(nativeQuota);
        const auto peak = pressure.measureSingleUse([] {},
            [&] {
                return native();
            });
        pressure.verifySingleUse(
            peak,
            [] {},
            [&] {
                return native();
            });
        assert(!nativeQuota->liveBytes() && !callbackQuota->liveBytes());
        for (unsigned failure = 1u; failure <= 3u; ++failure) {
            EM_ASM(
                {
                    // clang-format off
                    Module.tableEmissionFailure = $0;
                    // clang-format on
                },
                failure);
            {
                auto unaccepted = native(true);
            }
            EM_ASM({
                // clang-format off
                    Module.tableEmissionFailure = 0;
                // clang-format on
            });
            assert(!nativeQuota->liveBytes() && !callbackQuota->liveBytes());
        }
        auto pipeline = native();
        if (!pipeline)
            std::fprintf(stderr, "emission %s\n", diagnostic.c_str());
        assert(pipeline);
        assert(!Factory::validateConsumedTableExpansion(pipeline));
        assert(Factory::validateConsumedOriginal(
            pipeline)); // Input3 cannot enter ordinary consumption.
        rhi_webgpu::WebGpuOrdinaryPipelineBudget ordinary{2u,
            true,
            2u,
            2u,
            4u,
            1u,
            budget.maxCpuMetadataBytes,
            65536u,
            2u,
            2u,
            1u,
            budget.maxTargetBytes};
        auto ordinaryQuota = core::makeShared<core::CpuMemoryQuota>(ordinary.maxCpuMetadataBytes);
        const char* ordinaryDiagnostic = nullptr;
        assert(!rhi_webgpu::WebGpuOrdinaryPipelineObservation::prepare(
            pipeline, ordinary, ordinaryQuota, ordinaryDiagnostic));
        assert(ordinaryDiagnostic
            && core::StringView(ordinaryDiagnostic)
                == "RHI-WEBGPU-ORIGINAL-ARTIFACT-PIPELINE-ORDINARY-BRIDGE"
            && !ordinaryQuota->liveBytes());
        auto forgedPipeline =
            std::shared_ptr<typename decltype(pipeline)::element_type>(nativeQuota, pipeline.get());
        assert(Factory::validateConsumedTableExpansion(forgedPipeline));
        forgedPipeline.reset();
        const auto& issued = *pipeline->issuedContract();
        assert(core::SharedOwnerProjection::sameOwner(issued.expansionDefinition(), definition));
        assert(issued.facts().size() == space->entryCount() && issued.tables()[0].capacity == 3u);
        for (std::size_t i = 0u; i < 3u; ++i) {
            const auto joined = issued.expandedBindingFact(i);
            assert(&joined.original == &space->entry(i) && &joined.native == &issued.facts()[i]);
            assert(joined.original.arrayElement == i && joined.native.shape.arrayCount == 1u
                && joined.native.shape.depthTexture
                && joined.native.shape.textureViewDimension
                    == rhi::TextureViewDimension::Texture2D);
        }
        const auto& file = input->tableExpansion()
                               ->entry(0u)
                               .files[static_cast<std::size_t>(shader::ShaderArtifactRole::Wgsl)];
        assert(core::Sha256::matchesRawHexDigest(issued.stages()[0].sourceSha256, file.sha256));
        auto changedDigest = issued.stages()[0].sourceSha256;
        changedDigest[0] ^= 1u;
        assert(!core::Sha256::matchesRawHexDigest(changedDigest, file.sha256));
        const auto version = pipeline->sourceContractVersion;
        pipeline->sourceContractVersion = 1u;
        assert(Factory::validateConsumedTableExpansion(pipeline));
        pipeline->sourceContractVersion = version;
        for (auto& binding : pipeline->descriptorBindings) {
            ++binding.webGpuBinding;
            assert(Factory::validateConsumedTableExpansion(pipeline));
            --binding.webGpuBinding;
        }
        EM_ASM({
            // clang-format off
                Module.tableEmissionOriginalDevice = Module.preinitializedWebGPUDevice;
                Module.preinitializedWebGPUDevice = Module.tableEmissionForeignDevice;
            // clang-format on
        });
        assert(Factory::validateConsumedTableExpansion(pipeline));
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice = Module.tableEmissionOriginalDevice;
            // clang-format on
        });
        const auto limit = nativeQuota->limitBytes();
        nativeQuota->setLimitBytes(limit - 1u);
        assert(Factory::validateConsumedTableExpansion(pipeline));
        nativeQuota->setLimitBytes(limit);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{nativeQuota,
                                                                  definitionQuota,
                                                                  space->quota(),
                                                                  input->quota(),
                                                                  input->tableExpansion()->quota()},
            [&]() noexcept {
                return Factory::validateConsumedTableExpansion(pipeline);
            }));
        std::weak_ptr<const Definition> weak = definition;
        forged.reset();
        definition.reset();
        assert(!weak.expired() && !Factory::validateConsumedTableExpansion(pipeline));
        bool nativeLossObserved = false;
        auto lostDevice = emscripten_webgpu_get_device();
        assert(lostDevice);
        assert(rhi_webgpu::observeWebGpuNativeDeviceLoss(
            lostDevice, &nativeLossObserved, [](void* context) {
                *static_cast<bool*>(context) = true;
            }));
        // Emdawn imported devices have no C device-lost future. Destroy the
        // actual browser-owned device, as the existing loss fixtures do.
        wgpuDeviceRelease(lostDevice);
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.destroy();
            // clang-format on
        });
        assert(WebGpuAsyncWaitFixture::until([&] {
            return nativeLossObserved;
        }));
        assert(rhi_webgpu::webGpuNativeDeviceLost()
            && Factory::validateConsumedTableExpansion(pipeline));
        const auto live = nativeQuota->liveBytes();
        assert(!Factory::createTableExpansion(
                   issued.expansionDefinition(), budget, nativeQuota, diagnostic)
            && diagnostic == "RHI-ORIGINAL-TABLE-EMISSION-DEVICE-LOST"
            && nativeQuota->liveBytes() == live);
        pipeline.reset();
        assert(weak.expired() && !nativeQuota->liveBytes());
        weak.reset();
        assert(!definitionQuota->liveBytes() && !callbackQuota->liveBytes());
        std::printf(
            "ORIGINAL_TABLE_EXPANSION_EMISSION_OK %s physical-scalar3 logical-capacity3 ordered-element-join modules%u independent-native-peak%llu exact-less-exhausted scope-receipts native-scope-refusals3 foreign-device real-loss hot0 caller-drop creation-actor-closed\n",
            compute ? "Compute" : "Graphics",
            compute ? 1u : 2u,
            static_cast<unsigned long long>(peak));
#endif
    }
};
} // namespace arp::test
