#pragma once
#include "original_pipeline_factory_registration_fixture.h"

namespace arp::test {
class OriginalPipelineFullDefinitionFixture final {
    static constexpr std::uint64_t bytes = 65536u;

    static auto quota() { return core::makeShared<core::CpuMemoryQuota>(bytes); }

    static rhi::OriginalResourceRegistrationBudget budget()
    {
        rhi::OriginalResourceRegistrationBudget b;
        b.contractVersion = 4u;
        b.prototypeOptIn = true;
        b.maxPipelineLayouts = 1u;
        b.maxGraphicsPipelines = 1u;
        b.maxComputePipelines = 1u;
        b.maxRetainedOwners = 15u;
        b.maxCpuMetadataBytes = bytes;
        return b;
    }

    static void is(const char* actual, const char* expected)
    {
        if (!actual || std::string_view(actual) != expected)
            std::fprintf(stderr,
                "pipeline definition expected %s got %s\n",
                expected,
                actual ? actual : "null");
        assert(actual && std::string_view(actual) == expected);
    }

    template <class Factory>
    static auto device(Factory&& create,
        const std::shared_ptr<core::CpuMemoryQuota>& domain,
        const std::shared_ptr<core::CpuMemoryQuota>& metadata,
        rhi::OriginalResourceRegistrationBudget b = budget())
    {
        auto result = create();
        assert(result);
        assert(!result->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, domain));
        assert(!result->prepareOriginalResourceRegistration(b, metadata));
        return result;
    }

public:
    static void verifyContractAndCopies()
    {
        using Contract = rhi::OriginalResourceRegistrationContract;
        assert(!Contract::validateIntent(budget()));
        auto invalid = budget();
        invalid.contractVersion = 11u;
        is(Contract::validateIntent(invalid), "RHI-ORIGINAL-REGISTRATION-VERSION");
        invalid.contractVersion = 9u;
        assert(
            rhi::OriginalDescriptorNativeLossRegistrationContract::validate({1u, true}, invalid));
        assert(Contract::lowering(rhi::Backend::Vulkan, 9u).diagnostic);
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision = Contract::lowering(backend, 4u);
            assert(decision.fullPipelineDefinitionCustodyAvailable
                && decision.descriptorLifetimeIssuerAvailable
                && decision.pipelineLifetimeIssuerAvailable && !decision.executionAvailable
                && !decision.nativeLossObserved);
            assert(!Contract::lowering(backend, 3u).fullPipelineDefinitionCustodyAvailable);
            assert(Contract::ownerCount(rhi::OriginalRegisteredResourceKind::ComputePipeline, 4u)
                == 5u);
            assert(Contract::ownerCount(rhi::OriginalRegisteredResourceKind::ComputePipeline, 3u)
                == 4u);
        }
        rhi::RenderPipelineDesc graphics;
        graphics.label = "complete-authored-graphics-definition-copy";
        graphics.vertexBuffers.resize(1u);
        graphics.vertexBuffers[0].slot = 7u;
        graphics.vertexBuffers[0].strideBytes = 24u;
        graphics.vertexBuffers[0].inputRate = rhi::VertexInputRate::Instance;
        graphics.vertexBuffers[0].attributes.push_back({4u, 8u, rhi::VertexFormat::Float32x4});
        graphics.renderPassCompatibility.subpasses.resize(1u);
        graphics.renderPassCompatibility.subpasses[0].colorAttachmentIndices.push_back(2u);
        graphics.renderPassCompatibility.subpasses[0].inputAttachmentIndices.push_back(1u);
        graphics.tessellationControlShader.entryPoint = "retained-control-entry";
        graphics.tessellationEvaluationShader.entryPoint = "retained-evaluation-entry";
        graphics.patchControlPointCount = 3u;
        rhi::ComputePipelineDesc compute;
        compute.label = "complete-authored-compute-definition-copy";
        compute.computeShader.entryPoint = "retained-compute-entry";
        auto metadata = quota();
        CpuQuotaFactoryFixture pressure(metadata);
        const auto render = [&] {
            core::CpuMemoryQuotaScope scope(metadata);
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            std::shared_ptr<rhi::RenderPipelineDesc> result;
            try {
                result = core::makeShared<rhi::RenderPipelineDesc>(
                    rhi::RenderPipelineStateCopy::copy(graphics));
            } catch (const std::bad_alloc&) {
            }
            std::pmr::set_default_resource(previous);
            return result;
        };
        const auto dispatch = [&] {
            core::CpuMemoryQuotaScope scope(metadata);
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            std::shared_ptr<rhi::ComputePipelineDesc> result;
            try {
                result = core::makeShared<rhi::ComputePipelineDesc>(
                    rhi::ComputePipelineStateCopy::copy(compute));
            } catch (const std::bad_alloc&) {
            }
            std::pmr::set_default_resource(previous);
            return result;
        };
        const auto graphicsPeak = pressure.measure(render),
                   computePeak = pressure.measure(dispatch);
        pressure.verify(graphicsPeak, render);
        pressure.verify(computePeak, dispatch);
        {
            auto g = render();
            auto c = dispatch();
            assert(g && c);
            graphics.vertexBuffers[0].attributes[0].offsetBytes = 0u;
            graphics.renderPassCompatibility.subpasses[0].colorAttachmentIndices[0] = 0u;
            graphics.tessellationControlShader.entryPoint = "changed";
            compute.label = "changed";
            compute.computeShader.entryPoint = "changed";
            assert(g->vertexBuffers[0].attributes[0].offsetBytes == 8u
                && g->vertexBuffers[0].slot == 7u
                && g->vertexBuffers[0].inputRate == rhi::VertexInputRate::Instance
                && g->renderPassCompatibility.subpasses[0].colorAttachmentIndices[0] == 2u
                && g->renderPassCompatibility.subpasses[0].inputAttachmentIndices[0] == 1u
                && g->tessellationControlShader.entryPoint == "retained-control-entry"
                && g->tessellationEvaluationShader.entryPoint == "retained-evaluation-entry"
                && g->patchControlPointCount == 3u
                && c->computeShader.entryPoint == "retained-compute-entry"
                && c->label != "changed");
        }
        assert(!metadata->liveBytes());
        std::puts(
            "BINDLESS_RHI_ORIGINAL_PIPELINE_DEFINITION_CONTRACT_OK registry4 owners5 old-versions-absent full-nested-core-copy std-default-disabled four-backends-artifact-execution-closed");
    }

    template <class Factory>
    static void verifyCompute(Factory&& create,
        rhi::ShaderPackageDesc package,
        const rhi::PipelineLayoutDesc& layoutDesc,
        const rhi::ShaderEntrypoint& entry)
    {
        auto domain = quota(), metadata = quota(), objects = quota();
        auto original = device(create, domain, metadata);
        std::shared_ptr<rhi::PipelineLayout> layout;
        {
            core::CpuMemoryQuotaScope scope(objects);
            layout = core::shareUnique(original->createOriginalPipelineLayout(layoutDesc));
        }
        assert(layout);
        const auto allocation = core::inspectCpuAllocationContaining(layout.get());
        rhi::ComputePipelineDesc desc;
        desc.label = "original-compute-full-definition-factory";
        desc.layout = layout.get();
        desc.shaderPackage = &package;
        desc.computeShader = entry;
        const auto factory = [&] {
            core::CpuMemoryQuotaScope scope(objects);
            return original->createOriginalComputePipeline(desc, layout);
        };
        CpuQuotaFactoryFixture pressure(metadata);
        const auto peak = pressure.measure(factory);
        const auto baseline = metadata->liveBytes();
        pressure.verify(peak, factory);
        {
            core::CpuMemoryQuotaLease full(metadata, bytes - baseline);
            assert(full.charged());
            assert(!factory());
            is(original->lastOriginalResourceRegistrationDiagnostic().data(),
                "RHI-ORIGINAL-REGISTRATION-ALLOCATION");
        }
        const auto selected = desc.computeShader;
        desc.computeShader.entryPoint = "missing-full-definition-entry";
        assert(!factory());
        is(original->lastOriginalResourceRegistrationDiagnostic().data(),
            "RHI-ORIGINAL-TYPED-PIPELINE-ENTRY");
        assert(metadata->liveBytes() == baseline);
        desc.computeShader = selected;
        const auto reflectionPath = package.artifacts.reflectionPath;
        package.artifacts.reflectionPath = "missing-original-full-definition.reflection.yaml";
        assert(!factory());
        is(original->lastOriginalResourceRegistrationDiagnostic().data(),
            "RHI-ORIGINAL-TYPED-SHADER-DECLARATION");
        assert(metadata->liveBytes() == baseline);
        package.artifacts.reflectionPath = reflectionPath;
        auto pipeline = factory();
        assert(pipeline && !pipeline->originalFullDefinitionDiagnostic());
        auto foreign = create();
        assert(foreign);
        is(foreign->validateOriginalResourceRegistration(*pipeline),
            "RHI-ORIGINAL-REGISTRATION-FOREIGN-DEVICE");
        foreign.reset();
        core::UniquePtr<rhi::ComputePipeline> ordinary;
        {
            core::CpuMemoryQuotaScope scope(objects);
            ordinary = original->createComputePipeline(desc);
        }
        assert(ordinary);
        is(ordinary->originalFullDefinitionDiagnostic(), "RHI-ORIGINAL-REGISTRATION-EXPIRED");
        assert(!ordinary->originalComputePipelineDefinition());
        ordinary.reset();
        const auto* definition = pipeline->originalComputePipelineDefinition();
        assert(definition && !pipeline->originalRenderPipelineDefinition());
        const auto retained = core::inspectCpuAllocationContaining(definition);
        assert(retained.quota == metadata.get());
        assert(definition->shaderPackage == pipeline->originalShaderPackageMetadata()
            && definition->shaderPackage != &package);
        assert(definition->layout == layout.get()
            && definition->computeShader.entryPoint == selected.entryPoint);
        desc.label = "changed";
        desc.computeShader.entryPoint = "changed";
        package.label = "changed";
        assert(definition->label == "original-compute-full-definition-factory"
            && definition->computeShader.entryPoint == selected.entryPoint
            && definition->shaderPackage->label != "changed");
        auto parent = layout->originalResourceRegistration();
        layout.reset();
        const auto preserved = core::inspectCpuAllocationContaining(definition->layout);
        assert(
            preserved.allocationIdentity == allocation.allocationIdentity && !parent.diagnostic());
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{domain, metadata, objects}, [&]() noexcept {
                return pipeline->originalFullDefinitionDiagnostic();
            }));
        original->retireOriginalDeviceLifetime();
        is(pipeline->originalFullDefinitionDiagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED");
        pipeline.reset();
        parent = {};
        original.reset();
        assert(!domain->liveBytes() && !metadata->liveBytes() && !objects->liveBytes());
        auto policy = budget();
        policy.maxRetainedOwners = 10u; // fixed3 + layout2 + five-owner pipeline.
        original = device(create, domain, metadata, policy);
        layout = core::shareUnique(original->createOriginalPipelineLayout(layoutDesc));
        assert(layout);
        desc.layout = layout.get();
        desc.computeShader = selected;
        desc.shaderPackage = &package;
        pipeline = factory();
        assert(pipeline && !pipeline->originalFullDefinitionDiagnostic());
        pipeline.reset();
        layout.reset();
        original.reset();
        policy.maxRetainedOwners = 9u;
        original = device(create, domain, metadata, policy);
        layout = core::shareUnique(original->createOriginalPipelineLayout(layoutDesc));
        assert(layout);
        desc.layout = layout.get();
        assert(!factory());
        is(original->lastOriginalResourceRegistrationDiagnostic().data(),
            "RHI-ORIGINAL-REGISTRATION-OWNER-CAPACITY");
        layout.reset();
        original.reset();
        assert(!domain->liveBytes() && !metadata->liveBytes() && !objects->liveBytes());
        std::printf(
            "BINDLESS_RHI_ORIGINAL_PIPELINE_FULL_COMPUTE_OK metadata%llu original-factory complete-selected-definition exact-layout-package caller-mutation original-allocation owners5-one-less cold-exact-less-exhausted hot-allocation0 artifact-native-closed\n",
            static_cast<unsigned long long>(peak));
    }

    static void verifyHost()
    {
        auto package = rhi::test::makePushContractShaderPackage(true);
        rhi::PipelineLayoutDesc layoutDesc;
        layoutDesc.pushConstantRanges.push_back(
            {rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment | rhi::ShaderStage::Compute,
                0u,
                128u});
        const auto entry = package.entrypoints[2];
        verifyCompute(
            [] {
                return rhi::createNullDevice();
            },
            rhi::ShaderPackageStateCopy::copy(package),
            layoutDesc,
            entry);
        auto domain = quota(), metadata = quota(), objects = quota();
        auto original = device(
            [] {
                return rhi::createNullDevice();
            },
            domain,
            metadata);
        auto layout = core::shareUnique(original->createOriginalPipelineLayout(layoutDesc));
        assert(layout);
        rhi::RenderPipelineDesc desc;
        desc.label = "original-graphics-full-definition-factory";
        desc.layout = layout.get();
        desc.shaderPackage = &package;
        desc.vertexShader = package.entrypoints[0];
        desc.fragmentShader = package.entrypoints[1];
        desc.colorFormat = rhi::TextureFormat::Bgra8Unorm;
        desc.cullMode = rhi::CullMode::Back;
        desc.frontFace = rhi::FrontFace::Clockwise;
        desc.hasViewport = true;
        desc.viewport = {2.f, 3.f, 8.f, 9.f, 0.f, 1.f};
        desc.hasScissor = true;
        desc.scissor = {2, 3, 8u, 9u};
        desc.vertexBuffers.resize(1u);
        desc.vertexBuffers[0].strideBytes = 16u;
        desc.vertexBuffers[0].attributes.push_back({0u, 0u, rhi::VertexFormat::Float32x4});
        const auto factory = [&] {
            core::CpuMemoryQuotaScope scope(objects);
            return original->createOriginalRenderPipeline(desc, layout);
        };
        CpuQuotaFactoryFixture pressure(metadata);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        auto pipeline = factory();
        assert(pipeline && !pipeline->originalFullDefinitionDiagnostic());
        const auto* definition = pipeline->originalRenderPipelineDefinition();
        assert(definition && !pipeline->originalComputePipelineDefinition());
        assert(core::inspectCpuAllocationContaining(definition).quota == metadata.get());
        desc.vertexBuffers[0].attributes[0].offsetBytes = 4u;
        desc.viewport.width = 1.f;
        desc.scissor.width = 1u;
        desc.cullMode = rhi::CullMode::None;
        package.label = "changed";
        layout.reset();
        assert(definition->vertexBuffers[0].attributes[0].offsetBytes == 0u
            && definition->viewport.width == 8.f && definition->scissor.width == 8u
            && definition->cullMode == rhi::CullMode::Back
            && definition->frontFace == rhi::FrontFace::Clockwise
            && definition->shaderPackage == pipeline->originalShaderPackageMetadata()
            && definition->shaderPackage->label != "changed");
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{domain, metadata, objects}, [&]() noexcept {
                return pipeline->originalFullDefinitionDiagnostic();
            }));
        original.reset();
        is(pipeline->originalFullDefinitionDiagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN");
        pipeline.reset();
        assert(!domain->liveBytes() && !metadata->liveBytes() && !objects->liveBytes());
        std::printf(
            "BINDLESS_RHI_ORIGINAL_PIPELINE_FULL_GRAPHICS_OK metadata%llu genuine-Null retained-full-raster-viewport-scissor-vertex deep-package-copy cold-exact-less hot-allocation0 artifact-native-closed\n",
            static_cast<unsigned long long>(peak));
    }
};
} // namespace arp::test
