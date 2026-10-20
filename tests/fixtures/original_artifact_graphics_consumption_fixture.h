#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "rhi/backends/webgpu/webgpu_pipeline_preparation.h"
#include "rhi/frontend/pipeline_state_copy.h"

#include <cassert>
#include <cstdio>
#include <string_view>
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
#include <emscripten/emscripten.h>
#endif
namespace arp::test {
class OriginalArtifactGraphicsConsumptionFixture final {
    using Contract = rhi::OriginalGraphicsArtifactConsumptionContract;
    using Factory = rhi_webgpu::WebGpuOriginalArtifactGraphicsFactory;
    using Input = rhi::OriginalGraphicsPipelineInput;

    static void is(const char* actual, const char* expected)
    {
        assert(actual && std::string_view(actual) == expected);
    }

public:
    static void verifyContract()
    {
        Contract::Budget budget;
        is(Contract::validateBudget(budget), "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-VERSION");
        budget.contractVersion = 1u;
        is(Contract::validateBudget(budget), "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-OPT-IN");
        budget.prototypeOptIn = true;
        is(Contract::validateBudget(budget), "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-BUDGET");
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
            "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-BACKEND");
        is(Factory::validateConsumedOriginal({}),
            "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-PIPELINE-OWNER");
        is(Contract::creationValidationDiagnostic(),
            "RHI-ORIGINAL-GRAPHICS-CREATION-RECEIPT-MISSING");
        rhi::RenderPipelineDesc profile;
        assert(!Contract::validateProfile(profile));
        profile.topology = rhi::PrimitiveTopology::TriangleStrip;
        is(Contract::validateProfile(profile),
            "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-TOPOLOGY-PROFILE");
        profile.topology = rhi::PrimitiveTopology::TriangleList;
        profile.multisample.sampleCount = 2u;
        is(Contract::validateProfile(profile), "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-MSAA-PROFILE");
        profile.multisample.sampleCount = 4u;
        assert(!Contract::validateProfile(profile));
        profile.depthStencil.depthBiasEnabled = true;
        is(Contract::validateProfile(profile),
            "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-DEPTH-BIAS-PROFILE");
        profile.depthStencil.depthBiasEnabled = false;
        profile.subpassIndex = 1u;
        is(Contract::validateProfile(profile), "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-SUBPASS-PROFILE");
        profile.subpassIndex = 0u;
        profile.vertexBuffers.resize(1u);
        profile.vertexBuffers[0].slot = 1u;
        is(Contract::validateProfile(profile),
            "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-VERTEX-SLOT-PROFILE");
        profile.vertexBuffers[0].slot = 0u;
        assert(!Contract::validateProfile(profile));
        budget.contractVersion = 2u;
        assert(!Contract::validateBudget(budget));
        profile.vertexBuffers[0].slot = 3u;
        assert(!Contract::validateProfile(profile, 2u));
        assert(Contract::validateProfile(profile));
        std::uint32_t count = 0;
        assert(
            !rhi::VertexStreamSlotLayout::count(profile.vertexBuffers, 4u, count) && count == 4u);
        assert(rhi::VertexStreamSlotLayout::count(profile.vertexBuffers, 3u, count));
        profile.vertexBuffers.push_back({});
        profile.vertexBuffers.back().slot = 3u;
        assert(Contract::validateProfile(profile, 2u));
        profile.vertexBuffers.pop_back();
        profile.vertexBuffers[0].slot = UINT32_MAX;
        assert(Contract::validateProfile(profile, 2u));
        assert(Contract::validateProfile(profile, 3u));
        auto unissued = core::makeShared<rhi_webgpu::WebGpuNativeRenderPipelinePayload>();
        is(Factory::validateConsumedOriginal(unissued),
            "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-RECEIPT-MISSING");
        std::puts(
            "BINDLESS_RHI_ORIGINAL_GRAPHICS_CONSUMPTION_CONTRACT_OK four-backends-explicit validator-only-or-native-required unissued-rejected canonical-closed");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyActual(
        const std::shared_ptr<const Input>& input, const rhi::RenderPipelineDesc& state)
    {
        assert(!loss_);
        const auto& bytes = input->artifact()->bytes(shader::ShaderArtifactRole::Wgsl);
        Contract::Budget budget{1u, true, bytes.size(), 65536u};
        auto quota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
        core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
        diagnostic.reserve(512u);
        auto factory = [&] {
            return Factory::create(input, state, budget, quota, diagnostic);
        };
        auto expect = [&](auto original, auto limits, auto domain, const char* expected) {
            const auto before = quota->liveBytes();
            auto result = Factory::create(original, state, limits, domain, diagnostic);
            if (result || diagnostic != expected)
                std::fprintf(
                    stderr, "consumption expected %s got %s\n", expected, diagnostic.c_str());
            assert(!result && diagnostic == expected && quota->liveBytes() == before);
        };
        auto invalid = budget;
        --invalid.maxTargetBytes;
        expect(input, invalid, quota, "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-TARGET-BUDGET");
        auto forgedInput = std::shared_ptr<const Input>(input->quota(), input.get());
        expect(forgedInput, budget, quota, "RHI-ORIGINAL-GRAPHICS-INPUT-OWNER");
        expect(std::shared_ptr<const Input>{}, budget, quota, "RHI-ORIGINAL-GRAPHICS-INPUT-OWNER");
        invalid = budget;
        invalid.maxCpuMetadataBytes = input->quota()->limitBytes();
        expect(input, invalid, input->quota(), "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-QUOTA-DOMAIN");
        invalid.maxCpuMetadataBytes = input->artifact()->quota()->limitBytes();
        expect(input,
            invalid,
            input->artifact()->quota(),
            "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-QUOTA-DOMAIN");
        EM_ASM({
            // clang-format off
                const d = Module.preinitializedWebGPUDevice;
                Module.arpGraphicsNegativeNativeCalls = 0;
                Module.arpGraphicsNegativeModule = d.createShaderModule;
                Module.arpGraphicsNegativePipeline = d.createRenderPipeline;
                d.createShaderModule = function(desc) {
                    ++Module.arpGraphicsNegativeNativeCalls;
                    return Module.arpGraphicsNegativeModule.call(this, desc);
                };
                d.createRenderPipeline = function(desc) {
                    ++Module.arpGraphicsNegativeNativeCalls;
                    return Module.arpGraphicsNegativePipeline.call(this, desc);
                };
            // clang-format on
        });
        const auto rejectedState = [&](auto change, const char* expected) {
            auto changed = rhi::RenderPipelineStateCopy::copy(state);
            change(changed);
            const auto before = quota->liveBytes();
            assert(!Factory::create(input, changed, budget, quota, diagnostic));
            if (diagnostic != expected)
                std::fprintf(
                    stderr, "graphics state expected %s got %s\n", expected, diagnostic.c_str());
            assert(diagnostic == expected && before == quota->liveBytes());
        };
        rejectedState(
            [](auto& p) {
                p.shaderPackage = nullptr;
            },
            "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-PACKAGE");
        rejectedState(
            [](auto& p) {
                p.layout = nullptr;
            },
            "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-LAYOUT");
        rejectedState(
            [](auto& p) {
                p.fragmentShader.entryPoint = "missing-selected-fragment";
            },
            "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-SELECTED-ENTRIES");
        rejectedState(
            [](auto& p) {
                p.vertexShader.stage = rhi::ShaderStage::Fragment;
            },
            "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-SELECTED-ENTRIES");
        rejectedState(
            [](auto& p) {
                std::swap(p.vertexShader, p.fragmentShader);
            },
            "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-SELECTED-ENTRIES");
        rejectedState(
            [](auto& p) {
                p.vertexBuffers.resize(1u);
                p.vertexBuffers[0].strideBytes = 16u;
                p.vertexBuffers[0].attributes.push_back({0u, 0u, rhi::VertexFormat::Float32x4});
            },
            "RHI-VERTEX-INPUT-ARTIFACT-COUNT");
        rejectedState(
            [](auto& p) {
                p.multisample.sampleCount = 2u;
            },
            "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-MSAA-PROFILE");
        rejectedState(
            [](auto& p) {
                p.depthStencil.depthBiasEnabled = true;
            },
            "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-DEPTH-BIAS-PROFILE");
        rejectedState(
            [](auto& p) {
                p.subpassIndex = 1u;
            },
            "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-SUBPASS-PROFILE");
        rejectedState(
            [](auto& p) {
                p.topology = rhi::PrimitiveTopology::PatchList;
            },
            "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-TOPOLOGY-PROFILE");
        auto copiedPackage = rhi::ShaderPackageStateCopy::copy(input->package());
        rejectedState(
            [&](auto& p) {
                p.shaderPackage = &copiedPackage;
            },
            "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-PACKAGE");
        auto badLayoutDesc = rhi::PipelineLayoutStateCopy::copy(state.layout->desc());
        badLayoutDesc.descriptorLayouts[0].bindings[0].type = rhi::DescriptorType::StorageBuffer;
        auto badLayout = rhi_webgpu::createWebGpuRhiPipelineLayout(badLayoutDesc);
        assert(badLayout);
        rejectedState(
            [&](auto& p) {
                p.layout = badLayout.get();
            },
            "RHI-ORIGINAL-TYPED-PIPELINE-REFLECTION");
        badLayoutDesc = rhi::PipelineLayoutStateCopy::copy(state.layout->desc());
        badLayoutDesc.pushConstantRanges.push_back({rhi::ShaderStage::Fragment, 0u, 16u});
        auto wrongPush = rhi_webgpu::createWebGpuRhiPipelineLayout(badLayoutDesc);
        assert(wrongPush);
        rejectedState(
            [&](auto& p) {
                p.layout = wrongPush.get();
            },
            "WebGPU lowering push-constant group is outside artifact capacity");
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpGraphicsNegativeNativeCalls;
            // clang-format on
        }) == 0);
        EM_ASM({
            // clang-format off
                const d = Module.preinitializedWebGPUDevice;
                d.createShaderModule = Module.arpGraphicsNegativeModule;
                d.createRenderPipeline = Module.arpGraphicsNegativePipeline;
                delete Module.arpGraphicsNegativeModule;
                delete Module.arpGraphicsNegativePipeline;
                delete Module.arpGraphicsNegativeNativeCalls;
            // clang-format on
        });
        CpuQuotaFactoryFixture pressure(quota);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        {
            core::CpuMemoryQuotaLease exhausted(quota, quota->limitBytes());
            assert(exhausted.charged());
            expect(input, budget, quota, "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-ALLOCATION");
        }
        EM_ASM({
            // clang-format off
                const d = Module.preinitializedWebGPUDevice;
                Module.arpGraphicsActualCalls = 0;
                Module.arpGraphicsActualModule = d.createShaderModule;
                Module.arpGraphicsActualRender = d.createRenderPipeline;
                d.createShaderModule = function(desc) {
                    ++Module.arpGraphicsActualCalls;
                    Module.arpGraphicsActualCode = desc.code;
                    const shader = Module.arpGraphicsActualModule.call(this, desc);
                    Module.arpGraphicsActualShader = shader;
                    return shader;
                };
                d.createRenderPipeline = function(desc) {
                    ++Module.arpGraphicsActualCalls;
                    Module.arpGraphicsActualDescriptor = desc;
                    return Module.arpGraphicsActualRender.call(this, desc);
                };
            // clang-format on
        });
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        auto pipeline = factory();
        std::pmr::set_default_resource(previous);
        if (!pipeline)
            std::fprintf(stderr, "original memory graphics failed %s\n", diagnostic.c_str());
        assert(pipeline && !Factory::validateConsumedOriginal(pipeline));
        assert(EM_ASM_INT(
                   {
                       // clang-format off
                           const actual = new TextEncoder().encode(Module.arpGraphicsActualCode);
                           const desc = Module.arpGraphicsActualDescriptor;
                           return Module.arpGraphicsActualCalls === 2 && actual.length === Number($1) &&
                                  actual.every((value, index) => value === HEAPU8[Number($0) + index]) &&
                                  desc.vertex.module === Module.arpGraphicsActualShader &&
                                  desc.fragment.module === desc.vertex.module &&
                                  desc.vertex.entryPoint === UTF8ToString($2) &&
                                  desc.fragment.entryPoint === UTF8ToString($3) &&
                                  desc.primitive.topology === "triangle-list" &&
                                  desc.fragment.targets[0].format === "rgba8unorm";
                       // clang-format on
                   },
                   bytes.data(),
                   bytes.size(),
                   state.vertexShader.entryPoint.c_str(),
                   state.fragmentShader.entryPoint.c_str())
            == 1);
        EM_ASM({
            // clang-format off
                const d = Module.preinitializedWebGPUDevice;
                d.createShaderModule = Module.arpGraphicsActualModule;
                d.createRenderPipeline = Module.arpGraphicsActualRender;
                delete Module.arpGraphicsActualModule;
                delete Module.arpGraphicsActualRender;
                delete Module.arpGraphicsActualCalls;
                delete Module.arpGraphicsActualCode;
                delete Module.arpGraphicsActualDescriptor;
                delete Module.arpGraphicsActualShader;
            // clang-format on
        });
        std::puts(
            "BINDLESS_RHI_ORIGINAL_GRAPHICS_NATIVE_ARGUMENTS_OK actual-wgsl-bytes actual-two-entries shared-native-module authored-render-state");
        const auto& issued = *pipeline->issuedContract();
        assert(core::SharedOwnerProjection::sameOwner(issued.graphicsInput(), input));
        assert(core::SharedOwnerProjection::sameOwner(issued.quota(), quota));
        assert(!issued.artifactInput() && issued.graphics().graphics
            && issued.graphics().matches(*pipeline));
        assert(issued.stages()[0].moduleIdentity == issued.stages()[1].moduleIdentity);
        assert(issued.stages()[0].entry == input->artifact()->vertex()->entry());
        assert(issued.stages()[1].entry == input->artifact()->fragment()->entry());
        assert(issued.stages()[0].sourceSha256 == issued.stages()[1].sourceSha256);
        assert(issued.stages().size() == 2u && issued.stages()[0].sourceBytes == bytes.size());
        const auto digest = core::Sha256::digest(shader::shaderArtifactText(bytes));
        assert(std::equal(digest.begin(), digest.end(), issued.stages()[0].sourceSha256.begin()));
        auto observationQuota = core::makeShared<core::CpuMemoryQuota>(65536u);
        const rhi_webgpu::WebGpuOrdinaryPipelineBudget ordinary{
            2u, true, 1u, 1u, 1u, 0u, 65536u, 65536u, 2u, 1u, 1u, bytes.size()};
        const char* bridgeDiagnostic = nullptr;
        const auto bridgeAllocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!rhi_webgpu::WebGpuOrdinaryPipelineObservation::prepare(
            pipeline, ordinary, observationQuota, bridgeDiagnostic));
        is(bridgeDiagnostic, "RHI-WEBGPU-ORIGINAL-ARTIFACT-PIPELINE-ORDINARY-BRIDGE");
        assert(!observationQuota->liveBytes()
            && bridgeAllocations == core::defaultCpuAllocator().stats().allocationCount);
        auto forged =
            std::shared_ptr<rhi_webgpu::WebGpuNativeRenderPipelinePayload>(quota, pipeline.get());
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota, input->quota(), input->artifact()->quota()}, [&]() noexcept {
                is(Factory::validateConsumedOriginal(forged),
                    "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-PIPELINE-OWNER");
                quota->setLimitBytes(0u);
                is(Factory::validateConsumedOriginal(pipeline),
                    "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-QUOTA-LIMIT");
                quota->setLimitBytes(budget.maxCpuMetadataBytes - 1u);
                is(Factory::validateConsumedOriginal(pipeline),
                    "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-QUOTA-LIMIT");
                quota->setLimitBytes(budget.maxCpuMetadataBytes);
                ++pipeline->shaderPackageHash;
                is(Factory::validateConsumedOriginal(pipeline),
                    "RHI-WEBGPU-ORDINARY-PIPELINE-IDENTITY-CHANGED");
                --pipeline->shaderPackageHash;
                EM_ASM({
                    // clang-format off
                        Module.arpSavedGraphicsDevice = Module.preinitializedWebGPUDevice;
                        Module.preinitializedWebGPUDevice = null;
                    // clang-format on
                });
                is(Factory::validateConsumedOriginal(pipeline),
                    "RHI-ORIGINAL-GRAPHICS-CONSUMPTION-DEVICE-CHANGED");
                EM_ASM({
                    // clang-format off
                        Module.preinitializedWebGPUDevice = Module.arpSavedGraphicsDevice;
                        delete Module.arpSavedGraphicsDevice;
                    // clang-format on
                });
                return Factory::validateConsumedOriginal(pipeline);
            }));
        forged.reset();
        pipeline.reset();
        assert(!quota->liveBytes());
        loss_ = factory();
        assert(loss_);
        EM_ASM(
            {
                // clang-format off
                    const pipeline = WebGPU.getJsObject(Number($0));
                    const layout = WebGPU.getJsObject(Number($1));
                    Module.arpOriginalGraphicsLossObjects = [];
                    Module.arpOriginalGraphicsLossObjects.push(pipeline);
                    Module.arpOriginalGraphicsLossObjects.push(layout);
                    const wrappers = Object.values(WebGPU.Internals.jsObjects);
                    if (!Module.arpOriginalGraphicsLossObjects.every(p => wrappers.includes(p)))
                        throw new Error("graphics loss entries missing");
                // clang-format on
            },
            loss_->pipeline,
            loss_->layout);
        std::printf(
            "BINDLESS_RHI_ORIGINAL_GRAPHICS_NATIVE_CONSUMPTION_OK %llu source%llu exact-memory-digest original-input-retained quota-exact-less-exhausted hot-allocation0 canonical-closed\n",
            static_cast<unsigned long long>(peak),
            static_cast<unsigned long long>(bytes.size()));
    }

    static void verifyActualLoss()
    {
        assert(loss_);
        auto nativeQuota = loss_->issuedContract()->quota();
        auto inputQuota = loss_->issuedContract()->graphicsInput()->quota();
        auto artifactQuota = loss_->issuedContract()->graphicsInput()->artifact()->quota();
        assert(nativeQuota->liveBytes() && inputQuota->liveBytes() && artifactQuota->liveBytes());
        is(Factory::validateConsumedOriginal(loss_), "RHI-WEBGPU-ORDINARY-PIPELINE-DEVICE-LOST");
        loss_.reset();
        assert(
            !nativeQuota->liveBytes() && !inputQuota->liveBytes() && !artifactQuota->liveBytes());
        EM_ASM({
            // clang-format off
                const remaining = Object.values(WebGPU.Internals.jsObjects);
                if (Module.arpOriginalGraphicsLossObjects.some(p => remaining.includes(p)))
                    throw new Error("graphics wrappers leaked after loss");
                Module.arpOriginalGraphicsLossObjects = null;
            // clang-format on
        });
        std::puts(
            "BINDLESS_RHI_ORIGINAL_GRAPHICS_NATIVE_CONSUMPTION_LOSS_OK actual-device-loss-rejected retained-input-released quotas3-zero native-wrappers2-zero");
    }

private:
    inline static std::shared_ptr<rhi_webgpu::WebGpuNativeRenderPipelinePayload> loss_;
#endif
};
} // namespace arp::test
