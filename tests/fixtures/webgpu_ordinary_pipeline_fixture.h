#pragma once
#include "core/file_io.h"
#include "cpu_quota_factory_fixture.h"
#include "graphics_shader_module_artifact_fixture.h"
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "rhi/backends/webgpu/webgpu_pipeline_preparation.h"
#include "rhi/frontend/bindless_shader_package.h"
#include "single_storage_contract_fixture.h"
#include "webgpu_ordinary_physical_groups_fixture.h"

#include <cassert>
#include <cstdio>
#include <string_view>
#include <type_traits>

namespace arp::test {
class WebGpuOrdinaryPipelineFixture final {
public:
    static void verifyContract()
    {
        using namespace rhi_webgpu;
        WebGpuOrdinaryPipelineBudget budget;
        assert(std::string_view(WebGpuOrdinaryPipelineObservation::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-PIPELINE-VERSION");
        budget.contractVersion = 2u;
        assert(std::string_view(WebGpuOrdinaryPipelineObservation::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-PIPELINE-OPT-IN");
        budget.prototypeOptIn = true;
        assert(std::string_view(WebGpuOrdinaryPipelineObservation::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-PIPELINE-BUDGET");
        budget.maxGroups = 1u;
        budget.maxOriginalMetadataBytes = metadataBytes;
        budget.maxCpuMetadataBytes = metadataBytes;
        budget.maxShaderStages = 1u;
        budget.maxShaderModules = 1u;
        budget.maxShaderSourceBytes = 1u;
        assert(!WebGpuOrdinaryPipelineObservation::validateBudget(budget));
        budget.contractVersion = 1u;
        assert(std::string_view(WebGpuOrdinaryPipelineObservation::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-PIPELINE-VERSION");
        budget.contractVersion = 2u;
        verifySourceInventory();
        WebGpuOrdinaryShaderValidationFixture::verifyContract();
        const char* diagnostic = nullptr;
        const std::shared_ptr<WebGpuNativeComputePipelinePayload> empty;
        assert(!WebGpuOrdinaryPipelineObservation::prepare(empty, budget, {}, diagnostic));
        assert(std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-PIPELINE-QUOTA");
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto unissued = core::makeShared<WebGpuNativeComputePipelinePayload>();
        assert(!WebGpuOrdinaryPipelineObservation::prepare(unissued, budget, quota, diagnostic));
        assert(std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-PIPELINE-NATIVE-UNAVAILABLE"
            && !quota->liveBytes());
#endif
    }

    static void verifySourceInventory()
    {
        using namespace rhi_webgpu;
        using rhi::ShaderStage;
        static_assert(!std::is_default_constructible_v<WebGpuNativeShaderModulePayload>);
        core::pmr::Vector<WebGpuNativePipelineStageFact> stages;
        WebGpuNativePipelineShaderSourceContract::Inventory result;
        auto rejected = [&](bool graphics) {
            result = {9u, 99u};
            assert(std::string_view(
                       WebGpuNativePipelineShaderSourceContract::inspect(stages, graphics, result))
                == "RHI-WEBGPU-ORDINARY-PIPELINE-SOURCE-FACTS");
            assert(!result.modules && !result.bytes);
        };
        rejected(false);
        rejected(true);
        // Host stamps are metadata fixtures only, never native proof/handles.
        std::uint32_t first = 0u, second = 0u;
        stages.push_back({ShaderStage::Compute, "computeMain", {}, 24u, &first});
        assert(!WebGpuNativePipelineShaderSourceContract::inspect(stages, false, result));
        assert(result.modules == 1u && result.bytes == 24u);
        rejected(true);
        stages[0].stage = ShaderStage::None;
        rejected(false);
        stages[0].stage = ShaderStage::Compute | ShaderStage::Vertex;
        rejected(false);
        stages[0].stage = ShaderStage::Compute;
        stages[0].entry.clear();
        rejected(false);
        stages[0].entry = "computeMain";
        stages[0].sourceBytes = 0u;
        rejected(false);
        stages[0].sourceBytes = 24u;
        stages[0].moduleIdentity = nullptr;
        rejected(false);
        stages[0].moduleIdentity = &first;
        stages.push_back(stages[0]);
        rejected(false);
        stages[0].stage = ShaderStage::Vertex;
        stages[0].entry = "vertexMain";
        stages[1].stage = ShaderStage::Fragment;
        stages[1].entry = "fragmentMain";
        assert(!WebGpuNativePipelineShaderSourceContract::inspect(stages, true, result));
        assert(result.modules == 1u && result.bytes == 24u);
        ++stages[1].sourceBytes;
        rejected(true);
        --stages[1].sourceBytes;
        ++stages[1].sourceSha256[0];
        rejected(true);
        --stages[1].sourceSha256[0];
        stages[1].moduleIdentity = &second;
        assert(!WebGpuNativePipelineShaderSourceContract::inspect(stages, true, result));
        assert(result.modules == 2u && result.bytes == 48u);
        stages[0].sourceBytes = UINT64_MAX;
        rejected(true);
        stages[0].sourceBytes = UINT64_MAX - 24u;
        assert(!WebGpuNativePipelineShaderSourceContract::inspect(stages, true, result));
        assert(result.bytes == UINT64_MAX);
        rejected(false);
    }

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    // Cold factories return before entering each independent async profile;
    // suspended continuations never retain unrelated profile construction locals.
    static core::UniquePtr<rhi_webgpu::WebGpuOrdinaryPipelineObservation> verifyActual(
        const rhi::BindlessShaderPackage& package)
    {
        auto lossProbe = verifyBindless(package);
        verifyCompute();
        verifyGraphics();
        verifyStaticGraphics();
        verifyEmptyGraphics();
        std::puts("BINDLESS_RHI_ORDINARY_PIPELINE_OWNERSHIP_OK 7");
        return lossProbe;
    }

private:
    static core::UniquePtr<rhi_webgpu::WebGpuOrdinaryPipelineObservation> verifyBindless(
        const rhi::BindlessShaderPackage& package)
    {
        using namespace rhi_webgpu;
        using namespace rhi;
        core::pmr::String message;
        message.reserve(512u);
        auto state = graphicsState();
        auto bindlessCompute = [&] {
            return createWebGpuNativeBindlessComputePipelinePayload(
                package, "computeMain", message);
        };
        auto lossProbe = verifyFactory(bindlessCompute, "bindless-compute", false, &package);
        auto bindlessGraphics = [&] {
            return createWebGpuNativeBindlessRenderPipelinePayload(
                package, "vertexMain", "fragmentBufferMain", state, message);
        };
        verifyFactory(bindlessGraphics, "bindless-graphics", false, &package);

        return lossProbe;
    }

    static void verifyCompute()
    {
        using namespace rhi_webgpu;
        using namespace rhi;
        SingleStorageContractFixture computeFacts(ShaderStage::Compute);
        computeFacts.layout.descriptorLayouts[0].bindings[0].dynamicOffset = true;
        auto computePackage =
            ordinaryPackage("ordinary_indirect_producer", computeFacts.reflection);
        auto computeLayout = createWebGpuRhiPipelineLayout(computeFacts.layout);
        assert(computeLayout);
        ComputePipelineDesc compute;
        compute.label = "ordinary issued compute";
        compute.layout = computeLayout.get();
        compute.shaderPackage = &computePackage;
        compute.computeShader.entryPoint = "computeMain";
        compute.computeShader.stage = ShaderStage::Compute;
        auto genericCompute = [&] {
            return createWebGpuNativeComputePipelinePayload(compute);
        };
        WebGpuOrdinaryPhysicalGroupsFixture::verifyActual(
            verifyFactory(genericCompute, "ordinary-compute", false, nullptr, &computePackage),
            "ordinary-compute");
        WebGpuOrdinaryPhysicalGroupsFixture::verifyActual(
            verifyFactory(
                genericCompute, "ordinary-compute-small-range", false, nullptr, &computePackage),
            "ordinary-compute-small-range",
            4u,
            false,
            true);
    }

    static void verifyGraphics()
    {
        using namespace rhi_webgpu;
        using namespace rhi;
        auto state = graphicsState();
        GraphicsShaderModuleArtifactFixture::verify(
            "/ordinary-pipeline-fixture/ordinary_graphics_storage_read/ordinary_graphics_storage_read.graphics.module.yaml");
        SingleStorageContractFixture graphicsFacts(
            ShaderStage::Fragment, DescriptorType::ReadOnlyStorageBuffer);
        auto graphicsPackage =
            ordinaryPackage("ordinary_graphics_storage_read", graphicsFacts.reflection);
        auto graphicsLayout = createWebGpuRhiPipelineLayout(graphicsFacts.layout);
        assert(graphicsLayout);
        state.layout = graphicsLayout.get();
        state.shaderPackage = &graphicsPackage;
        state.vertexShader.entryPoint = "vertexMain";
        state.vertexShader.stage = ShaderStage::Vertex;
        state.fragmentShader.entryPoint = "fragmentMain";
        state.fragmentShader.stage = ShaderStage::Fragment;
        auto genericGraphics = [&] {
            return createWebGpuNativeRenderPipelinePayload(state);
        };
        WebGpuOrdinaryPhysicalGroupsFixture::verifyActual(
            verifyFactory(genericGraphics, "ordinary-graphics", false, nullptr, &graphicsPackage),
            "ordinary-graphics");
    }

    static void verifyStaticGraphics()
    {
        using namespace rhi_webgpu;
        using namespace rhi;
        auto state = graphicsState();
        state.vertexShader.entryPoint = "vertexMain";
        state.vertexShader.stage = ShaderStage::Vertex;
        state.fragmentShader.entryPoint = "fragmentMain";
        state.fragmentShader.stage = ShaderStage::Fragment;
        SingleStorageContractFixture staticFacts(
            ShaderStage::Vertex | ShaderStage::Fragment, DescriptorType::ReadOnlyStorageBuffer);
        staticFacts.addBinding(8u, DescriptorType::ReadOnlyStorageBuffer, ShaderStage::Fragment);
        auto staticPackage = ordinaryPackage("ordinary_graphics_storage", staticFacts.reflection);
        auto staticLayout = createWebGpuRhiPipelineLayout(staticFacts.layout);
        assert(staticLayout);
        state.layout = staticLayout.get();
        state.shaderPackage = &staticPackage;
        auto staticGraphics = [&] {
            return createWebGpuNativeRenderPipelinePayload(state);
        };
        WebGpuOrdinaryPhysicalGroupsFixture::verifyActual(
            verifyFactory(staticGraphics, "ordinary-static-size", false, nullptr, &staticPackage),
            "ordinary-static-size",
            4u,
            true);
    }

    static void verifyEmptyGraphics()
    {
        using namespace rhi_webgpu;
        using namespace rhi;
        auto state = graphicsState();
        state.vertexShader.entryPoint = "vertexMain";
        state.vertexShader.stage = ShaderStage::Vertex;
        state.fragmentShader.entryPoint = "fragmentMain";
        state.fragmentShader.stage = ShaderStage::Fragment;
        auto emptyPackage = ordinaryPackage("vertex_stream_instancing", {});
        auto emptyLayout = createWebGpuRhiPipelineLayout({});
        assert(emptyLayout);
        state.layout = emptyLayout.get();
        state.shaderPackage = &emptyPackage;
        // Existing Slang source declares POSITION0, POSITION1, COLOR0 in this order.
        state.vertexBuffers = {
            {0u, 8u, VertexInputRate::Vertex, {{0u, 0u, VertexFormat::Float32x2}}},
            {1u,
                24u,
                VertexInputRate::Instance,
                {{1u, 0u, VertexFormat::Float32x2}, {2u, 8u, VertexFormat::Float32x4}}}};
        auto zeroGroups = [&] {
            return createWebGpuNativeRenderPipelinePayload(state);
        };
        WebGpuOrdinaryPhysicalGroupsFixture::verifyActual(
            verifyFactory(zeroGroups, "ordinary-zero-groups", true, nullptr, &emptyPackage),
            "ordinary-zero-groups");
    }

private:
public:
    static rhi::RenderPipelineDesc graphicsState()
    {
        rhi::RenderPipelineDesc result;
        result.label = "ordinary issued graphics";
        result.colorFormat = rhi::TextureFormat::Rgba8Unorm;
        result.hasViewport = true;
        result.viewport = {0, 0, 2, 2, 0, 1};
        result.hasScissor = true;
        result.scissor = {0, 0, 2, 2};
        return result;
    }

public:
    static rhi::ShaderPackageDesc ordinaryPackage(
        const char* fixture, const rhi::ShaderReflectionDesc& reflection)
    {
        rhi::ShaderPackageDesc result;
        result.label = "DescriptorFixture";
        core::pmr::String prefix("/ordinary-pipeline-fixture/");
        prefix += fixture;
        prefix += '/';
        prefix += fixture;
        result.slangModulePath = prefix + ".slang";
        result.artifacts = {
            prefix + ".spv", prefix + ".metal", prefix + ".wgsl", prefix + ".reflection.yaml"};
        result.reflection = reflection;
        // Test-authored logical facts are independently checked against the
        // generated source/target YAML by loadWebGpuBindingSpaceLowering.
        return result;
    }

private:
    template <class Factory>
    static core::UniquePtr<rhi_webgpu::WebGpuOrdinaryPipelineObservation> verifyFactory(
        Factory& factory,
        const char* name,
        bool zeroGroups = false,
        const rhi::BindlessShaderPackage* bindless = nullptr,
        rhi::ShaderPackageDesc* ordinary = nullptr)
    {
        using namespace rhi_webgpu;
        using Pipeline = typename decltype(factory())::element_type;
        auto originalQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto originalFactory = [&] {
            core::CpuMemoryQuotaScope scope(originalQuota);
            try {
                return factory();
            } catch (const std::bad_alloc&) {
                return std::shared_ptr<Pipeline>();
            }
        };
        CpuQuotaFactoryFixture originalAdmission(originalQuota);
        const auto originalPeak = originalAdmission.measure(originalFactory);
        originalAdmission.verify(originalPeak, originalFactory);
        assert(!originalQuota->liveBytes());
        {
            core::CpuMemoryQuotaLease pressure(originalQuota, originalQuota->limitBytes());
            assert(pressure.charged() && !originalFactory());
        }
        auto pipeline = originalFactory();
        assert(pipeline && pipeline->issuedContract());
        const auto& issued = *pipeline->issuedContract();
        assert(!issued.artifactInput());
        if constexpr (std::is_same_v<Pipeline, WebGpuNativeComputePipelinePayload>) {
            assert(std::string_view(
                       WebGpuOriginalArtifactComputeFactory::validateConsumedOriginal(pipeline))
                == "RHI-ORIGINAL-ARTIFACT-CONSUMPTION-RECEIPT-MISSING");
        }
        assert(issued.quota() == originalQuota && pipeline->pendingBindingFacts.empty()
            && pipeline->pendingShaderFacts.empty());
        assert(issued.semantics().groupCount == pipeline->bindGroupCount);
        if (zeroGroups)
            assert(!pipeline->bindGroupCount && issued.facts().empty() && issued.sets().empty());
        else
            assert(pipeline->bindGroupCount && !issued.facts().empty());
        for (const auto& fact : issued.facts()) {
            assert(fact.mapping.logicalBinding == fact.shape.binding && fact.shape.arrayCount);
            assert(fact.nativeDeclaredMinimumBytes
                == 0u); // Deferred shader-size check, never a range proof.
        }
        WebGpuOrdinaryPipelineBudget budget{2u,
            true,
            pipeline->bindGroupCount + 1u,
            static_cast<std::uint32_t>(issued.sets().size()),
            static_cast<std::uint32_t>(issued.bindings().size()),
            static_cast<std::uint32_t>(issued.tables().size()),
            metadataBytes,
            metadataBytes};
        WebGpuNativePipelineShaderSourceContract::Inventory inventory;
        assert(!WebGpuNativePipelineShaderSourceContract::inspect(
            issued.stages(), issued.graphics().graphics, inventory));
        assert(inventory.modules == (bindless && issued.graphics().graphics ? 2u : 1u));
        assert(issued.stages().size() == (issued.graphics().graphics ? 2u : 1u));
        auto sourceFile = ordinary ? core::readTextFile(ordinary->artifacts.wgslPath)
                                   : core::TextFileReadResult{};
        if (ordinary)
            assert(sourceFile.success);
        for (const auto& stage : issued.stages()) {
            const auto* entry = stage.stage == rhi::ShaderStage::Compute ? "computeMain"
                : stage.stage == rhi::ShaderStage::Vertex                ? "vertexMain"
                : bindless                                               ? "fragmentBufferMain"
                                                                         : "fragmentMain";
            assert(stage.entry == entry);
            core::StringView source;
            core::pmr::String sourceDiagnostic;
            if (bindless)
                assert(rhi::selectBindlessEmulatedWgsl(
                    *bindless, entry, stage.stage, source, sourceDiagnostic));
            else
                source = {sourceFile.text.data(), sourceFile.text.size()};
            const auto digest = core::Sha256::digest(source);
            assert(stage.sourceBytes == source.size()
                && std::equal(digest.begin(), digest.end(), stage.sourceSha256.begin()));
        }
        budget.maxShaderStages = issued.stages().size();
        budget.maxShaderModules = inventory.modules;
        budget.maxShaderSourceBytes = inventory.bytes;
        budget.maxColorTargets = issued.graphics().colorFormats.size();
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        const char* diagnostic = nullptr;
        auto observe = [&] {
            return WebGpuOrdinaryPipelineObservation::prepare(pipeline, budget, quota, diagnostic);
        };
        auto expect = [&](const char* id) {
            const auto before = quota->liveBytes();
            assert(
                !observe() && std::string_view(diagnostic) == id && quota->liveBytes() == before);
        };
        budget.contractVersion = 1u;
        expect("RHI-WEBGPU-ORDINARY-PIPELINE-VERSION");
        budget.contractVersion = 2u;
        --budget.maxShaderStages;
        expect(budget.maxShaderStages ? "RHI-WEBGPU-ORDINARY-PIPELINE-SOURCE-CAPACITY"
                                      : "RHI-WEBGPU-ORDINARY-PIPELINE-BUDGET");
        ++budget.maxShaderStages;
        --budget.maxShaderModules;
        expect(budget.maxShaderModules ? "RHI-WEBGPU-ORDINARY-PIPELINE-SOURCE-CAPACITY"
                                       : "RHI-WEBGPU-ORDINARY-PIPELINE-BUDGET");
        ++budget.maxShaderModules;
        --budget.maxShaderSourceBytes;
        expect("RHI-WEBGPU-ORDINARY-PIPELINE-SOURCE-CAPACITY");
        ++budget.maxShaderSourceBytes;
        if (budget.maxColorTargets) {
            --budget.maxColorTargets;
            expect("RHI-WEBGPU-ORDINARY-PIPELINE-SOURCE-CAPACITY");
            ++budget.maxColorTargets;
        }
        budget.maxCpuMetadataBytes--;
        expect("RHI-WEBGPU-ORDINARY-PIPELINE-QUOTA");
        budget.maxCpuMetadataBytes++;
        budget.maxOriginalMetadataBytes--;
        expect("RHI-WEBGPU-ORDINARY-PIPELINE-ORIGINAL-QUOTA");
        budget.maxOriginalMetadataBytes++;
        if (!zeroGroups) {
            budget.maxGroups = pipeline->bindGroupCount - 1u;
            expect(budget.maxGroups ? "RHI-WEBGPU-ORDINARY-PIPELINE-CAPACITY"
                                    : "RHI-WEBGPU-ORDINARY-PIPELINE-BUDGET");
            budget.maxGroups = pipeline->bindGroupCount;
            budget.maxSets--;
            expect("RHI-WEBGPU-ORDINARY-PIPELINE-CAPACITY");
            budget.maxSets++;
            budget.maxBindings--;
            expect("RHI-WEBGPU-ORDINARY-PIPELINE-CAPACITY");
            budget.maxBindings++;
        }
        if (budget.maxTables) {
            budget.maxTables--;
            expect("RHI-WEBGPU-ORDINARY-PIPELINE-CAPACITY");
            budget.maxTables++;
        }
        CpuQuotaFactoryFixture admission(quota);
        const auto snapshotPeak = admission.measure(observe);
        admission.verify(snapshotPeak, observe);
        {
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes());
            assert(pressure.charged());
            expect("RHI-WEBGPU-ORDINARY-PIPELINE-PREPARATION-ALLOCATION");
        }
        auto canonical = pipeline;
        pipeline = std::shared_ptr<Pipeline>(quota, canonical.get());
        expect("RHI-WEBGPU-ORDINARY-PIPELINE-ORIGINAL-OWNER");
        pipeline = core::makeShared<Pipeline>();
        expect("RHI-WEBGPU-ORDINARY-PIPELINE-UNISSUED");
        pipeline.reset();
        expect("RHI-WEBGPU-ORDINARY-PIPELINE-ORIGINAL-OWNER");
        pipeline = factory(); // Legacy, without a source quota, remains unadmitted.
        assert(pipeline && !pipeline->issuedContract()->quota());
        expect("RHI-WEBGPU-ORDINARY-PIPELINE-ORIGINAL-QUOTA");
        pipeline = std::move(canonical);
        auto observed = observe();
        assert(observed && !observed->validateForDevice(issued.nativeDevice()));
        assert(std::string_view(observed->validateForDevice(nullptr))
            == "RHI-WEBGPU-ORDINARY-PIPELINE-DEVICE-MISMATCH");
        assert(std::string_view(observed->validateForDevice(issued.nativeObject()))
            == "RHI-WEBGPU-ORDINARY-PIPELINE-DEVICE-MISMATCH");
        if (ordinary) {
            const auto originalPath = ordinary->artifacts.wgslPath;
            const auto originalHash = issued.stages()[0].sourceSha256;
            ordinary->artifacts.wgslPath = "/not-the-consumed-original.wgsl";
            assert(!observed->validate() && issued.stages()[0].sourceSha256 == originalHash);
            ordinary->artifacts.wgslPath = originalPath;
        }
        for (std::uint32_t group = 0u; group < pipeline->bindGroupCount; ++group) {
            WGPUBindGroupLayout native = nullptr;
            if constexpr (std::is_same_v<Pipeline, WebGpuNativeComputePipelinePayload>)
                native = wgpuComputePipelineGetBindGroupLayout(pipeline->pipeline, group);
            else
                native = wgpuRenderPipelineGetBindGroupLayout(pipeline->pipeline, group);
            assert(native);
            wgpuBindGroupLayoutRelease(native);
        }
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        {
            core::CpuMemoryQuotaScope scope(quota);
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
            assert(pressure.charged());
            auto changed = [&] {
                assert(std::string_view(observed->validate())
                    == "RHI-WEBGPU-ORDINARY-PIPELINE-IDENTITY-CHANGED");
            };
            const auto object = pipeline->pipeline;
            pipeline->pipeline = nullptr;
            changed();
            pipeline->pipeline = object;
            const auto layout = pipeline->layout;
            pipeline->layout = nullptr;
            changed();
            pipeline->layout = layout;
            ++pipeline->shaderPackageHash;
            changed();
            --pipeline->shaderPackageHash;
            ++pipeline->bindGroupCount;
            changed();
            --pipeline->bindGroupCount;
            ++pipeline->pushConstantGroup;
            changed();
            --pipeline->pushConstantGroup;
            ++pipeline->pushConstantBinding;
            changed();
            --pipeline->pushConstantBinding;
            pipeline->usesPushConstantGroup = !pipeline->usesPushConstantGroup;
            changed();
            pipeline->usesPushConstantGroup = !pipeline->usesPushConstantGroup;
            pipeline->hasValidatedInputAttachmentLowering =
                !pipeline->hasValidatedInputAttachmentLowering;
            changed();
            pipeline->hasValidatedInputAttachmentLowering =
                !pipeline->hasValidatedInputAttachmentLowering;
            if (!pipeline->descriptorBindings.empty()) {
                ++pipeline->descriptorBindings[0].webGpuBinding;
                changed();
                --pipeline->descriptorBindings[0].webGpuBinding;
                assert(issued.facts()[0].mapping == issued.bindings()[0]);
            }
            if (!pipeline->descriptorSetGroups.empty()) {
                ++pipeline->descriptorSetGroups[0].logicalSet;
                changed();
                --pipeline->descriptorSetGroups[0].logicalSet;
            }
            if (!pipeline->descriptorTables.empty()) {
                ++pipeline->descriptorTables[0].capacity;
                changed();
                --pipeline->descriptorTables[0].capacity;
            }
            if constexpr (std::is_same_v<Pipeline, WebGpuNativeRenderPipelinePayload>) {
                pipeline->hasViewport = !pipeline->hasViewport;
                changed();
                pipeline->hasViewport = !pipeline->hasViewport;
                pipeline->hasScissor = !pipeline->hasScissor;
                changed();
                pipeline->hasScissor = !pipeline->hasScissor;
                auto field = [&](auto& value) {
                    const auto saved = value;
                    ++value;
                    changed();
                    value = saved;
                };
                field(pipeline->viewport.x);
                field(pipeline->viewport.y);
                field(pipeline->viewport.width);
                field(pipeline->viewport.height);
                field(pipeline->viewport.minDepth);
                field(pipeline->viewport.maxDepth);
                field(pipeline->scissor.x);
                field(pipeline->scissor.y);
                field(pipeline->scissor.width);
                field(pipeline->scissor.height);
                field(pipeline->vertexBufferCount);
                field(pipeline->sampleCount);
                const auto topology = pipeline->primitiveTopology;
                pipeline->primitiveTopology = rhi::PrimitiveTopology::PointList;
                changed();
                pipeline->primitiveTopology = topology;
                const auto depth = pipeline->depthStencilFormat;
                pipeline->depthStencilFormat = rhi::TextureFormat::D32Float;
                changed();
                pipeline->depthStencilFormat = depth;
                if (!pipeline->colorFormats.empty()) {
                    const auto color = pipeline->colorFormats[0];
                    pipeline->colorFormats[0] = rhi::TextureFormat::Rgba16Float;
                    changed();
                    pipeline->colorFormats[0] = color;
                }
            }
            assert(!observed->validate()
                && core::defaultCpuAllocator().stats().allocationCount == allocations);
        }
        std::weak_ptr<Pipeline> original = pipeline;
        pipeline.reset();
        assert(!original.expired() && originalQuota->liveBytes() && !observed->validate());
        observed.reset();
        assert(original.expired() && !quota->liveBytes());
        original.reset();
        assert(!originalQuota->liveBytes());
        pipeline = originalFactory();
        observed = observe();
        pipeline.reset();
        assert(observed);
        std::printf("BINDLESS_RHI_ORDINARY_PIPELINE_CASE_OK %s %llu %llu\n",
            name,
            static_cast<unsigned long long>(originalPeak),
            static_cast<unsigned long long>(snapshotPeak));
        return observed;
    }
#endif
private:
    // Explicit fixture ceiling; exact/one-less values are measured from each
    // real factory and cold observation, never interpreted as driver GPU bytes.
    static constexpr std::uint64_t metadataBytes = 1024u * 1024u;
};
} // namespace arp::test
