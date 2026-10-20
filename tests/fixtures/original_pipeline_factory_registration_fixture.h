#pragma once
#include "../rhi_shader_package_test_helpers.h"
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/gpu_device.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalPipelineFactoryRegistrationFixture final {
    using Budget = rhi::OriginalResourceRegistrationBudget;
    using Kind = rhi::OriginalRegisteredResourceKind;
    static constexpr std::uint64_t bytes = 65536u;

    static auto quota() { return core::makeShared<core::CpuMemoryQuota>(bytes); }

    static Budget budget()
    {
        Budget result;
        result.contractVersion = 2u;
        result.prototypeOptIn = true;
        result.maxPipelineLayouts = 2u;
        result.maxGraphicsPipelines = 1u;
        result.maxComputePipelines = 1u;
        result.maxRetainedOwners = 15u;
        result.maxCpuMetadataBytes = bytes;
        return result;
    }

    static void is(const char* actual, const char* expected)
    {
        if (!actual || std::string_view(actual) != expected)
            std::fprintf(stderr,
                "pipeline registration expected %s, got %s\n",
                expected,
                actual ? actual : "success");
        assert(actual && std::string_view(actual) == expected);
    }

    static auto device(const std::shared_ptr<core::CpuMemoryQuota>& domain,
        const std::shared_ptr<core::CpuMemoryQuota>& metadata,
        Budget policy = budget())
    {
        auto result = rhi::createNullDevice();
        assert(result);
        assert(!result->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, domain));
        assert(!result->prepareOriginalResourceRegistration(policy, metadata));
        return result;
    }

public:
    static void verifyLayout()
    {
        using Contract = rhi::OriginalResourceRegistrationContract;
        auto policy = budget();
        assert(!Contract::validateIntent(policy));
        policy.contractVersion = 1u;
        is(Contract::validateIntent(policy), "RHI-ORIGINAL-REGISTRATION-TYPED-VERSION");
        policy = budget();
        policy.maxPipelineLayouts = 0u;
        is(Contract::validateIntent(policy), "RHI-ORIGINAL-REGISTRATION-PARENT-BUDGET");
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto plan = Contract::lowering(backend, 2u);
            assert(plan.contractVersion == 2u && plan.pipelineLifetimeIssuerAvailable
                && plan.typedLifetimeIssuerAvailable && plan.mode == rhi::CapabilityMode::Equivalent
                && !plan.executionAvailable && !plan.nativeLossObserved);
            assert(!Contract::lowering(backend).pipelineLifetimeIssuerAvailable);
        }
        static_assert(!std::is_copy_constructible_v<rhi::PipelineLayout>);
        static_assert(!std::is_copy_constructible_v<rhi::RenderPipeline>);
        static_assert(!std::is_copy_constructible_v<rhi::ComputePipeline>);
        auto domain = quota(), metadata = quota(), objects = quota();
        auto original = device(domain, metadata);
        rhi::PipelineLayoutDesc desc;
        desc.pushConstantRanges.push_back(
            {rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment | rhi::ShaderStage::Compute,
                0u,
                128u});
        const auto factory = [&] {
            core::CpuMemoryQuotaScope scope(objects);
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            auto result = original->createOriginalPipelineLayout(desc);
            std::pmr::set_default_resource(previous);
            return result;
        };
        CpuQuotaFactoryFixture pressure(metadata);
        const auto peak = pressure.measure(factory);
        const auto baseline = metadata->liveBytes();
        pressure.verify(peak, factory);
        assert(metadata->liveBytes() == baseline && !objects->liveBytes());
        {
            core::CpuMemoryQuotaLease full(metadata, bytes - baseline);
            assert(full.charged());
            assert(!factory());
            is(original->lastOriginalResourceRegistrationDiagnostic().data(),
                "RHI-ORIGINAL-REGISTRATION-ALLOCATION");
        }
        auto a = factory(), b = factory();
        assert(a && b);
        auto first = a->originalResourceRegistration();
        const auto generation = first.generation();
        assert(generation);
        assert(!original->validateOriginalResourceRegistration(*a));
        assert(first.matchesObjectIdentity(a.get(), Kind::PipelineLayout));
        assert(!first.matchesObjectIdentity(b.get(), Kind::PipelineLayout));
        assert(!first.matchesObjectIdentity(a.get(), Kind::GraphicsPipeline));
        assert(!first.matchesIdentity(a.get(), 0u, Kind::PipelineLayout));
        assert(!first.nativeExecutionProven());
        assert(!factory());
        is(original->lastOriginalResourceRegistrationDiagnostic().data(),
            "RHI-ORIGINAL-REGISTRATION-KIND-CAPACITY");
        auto ordinary = original->createPipelineLayout(desc);
        assert(ordinary);
        is(original->validateOriginalResourceRegistration(*ordinary),
            "RHI-ORIGINAL-REGISTRATION-EXPIRED");
        auto foreign = rhi::createNullDevice();
        assert(foreign);
        is(foreign->validateOriginalResourceRegistration(first),
            "RHI-ORIGINAL-REGISTRATION-FOREIGN-DEVICE");
        assert(!foreign->createOriginalPipelineLayout(desc));
        is(foreign->lastOriginalResourceRegistrationDiagnostic().data(),
            "RHI-ORIGINAL-REGISTRATION-UNPREPARED");
        a.reset();
        is(first.diagnostic(), "RHI-ORIGINAL-REGISTRATION-EXPIRED");
        auto replacement = factory();
        assert(
            replacement && replacement->originalResourceRegistration().generation() > generation);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{domain, metadata, objects}, [&]() noexcept {
                return original->validateOriginalResourceRegistration(*replacement);
            }));
        original->retireOriginalDeviceLifetime();
        assert(!factory());
        is(original->lastOriginalResourceRegistrationDiagnostic().data(),
            "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED");
        replacement.reset();
        b.reset();
        ordinary.reset();
        first = {};
        original.reset();
        assert(!domain->liveBytes() && !metadata->liveBytes() && !objects->liveBytes());
        policy = budget();
        policy.maxRetainedOwners = 5u;
        original = device(domain, metadata, policy);
        a = factory();
        assert(a);
        assert(!factory());
        is(original->lastOriginalResourceRegistrationDiagnostic().data(),
            "RHI-ORIGINAL-REGISTRATION-OWNER-CAPACITY");
        first = a->originalResourceRegistration();
        original.reset();
        is(first.diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN");
        a.reset();
        first = {};
        assert(!domain->liveBytes() && !metadata->liveBytes() && !objects->liveBytes());
        // /1 never adopts typed candidates or enables a pipeline budget.
        original = device(domain, metadata, {1u, true, 1u, 0u, 0u, 5u, bytes});
        assert(!factory());
        is(original->lastOriginalResourceRegistrationDiagnostic().data(),
            "RHI-ORIGINAL-REGISTRATION-TYPED-VERSION");
        original.reset();
        assert(!domain->liveBytes() && !metadata->liveBytes());
        std::printf(
            "BINDLESS_RHI_ORIGINAL_PIPELINE_FACTORY_LAYOUT_OK metadata%llu version2 exact-object-kind original-factory foreign-ordinary expired monotonic kind-owner-quota hot-allocation0 four-backends-execution-closed\n",
            static_cast<unsigned long long>(peak));
    }

    // Real Null backend wrappers and generated reflection; CPU lifetime evidence
    // only. Browser exercises preloaded native artifacts in its own backend path.
    static void verifyPipelines()
    {
        auto domain = quota(), metadata = quota(), objects = quota();
        auto original = device(domain, metadata);
        rhi::PipelineLayoutDesc desc;
        desc.pushConstantRanges.push_back(
            {rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment | rhi::ShaderStage::Compute,
                0u,
                128u});
        core::UniquePtr<rhi::PipelineLayout> unique;
        {
            core::CpuMemoryQuotaScope scope(objects);
            unique = original->createOriginalPipelineLayout(desc);
        }
        assert(unique);
        const auto allocation = core::inspectCpuAllocationContaining(unique.get());
        auto layout = core::shareUnique(std::move(unique));
        assert(layout->sharedOwnerMatches(layout));
        auto package = rhi::test::makePushContractShaderPackage();
        rhi::RenderPipelineDesc graphics;
        graphics.layout = layout.get();
        graphics.shaderPackage = &package;
        graphics.vertexShader = package.entrypoints[0];
        graphics.fragmentShader = package.entrypoints[1];
        graphics.colorFormat = rhi::TextureFormat::Bgra8Unorm;
        rhi::ComputePipelineDesc compute;
        compute.layout = layout.get();
        compute.shaderPackage = &package;
        compute.computeShader = package.entrypoints[2];
        const auto render = [&] {
            core::CpuMemoryQuotaScope scope(objects);
            return original->createOriginalRenderPipeline(graphics, layout);
        };
        const auto dispatch = [&] {
            core::CpuMemoryQuotaScope scope(objects);
            return original->createOriginalComputePipeline(compute, layout);
        };
        CpuQuotaFactoryFixture pressure(metadata);
        const auto renderPeak = pressure.measure(render), computePeak = pressure.measure(dispatch);
        const auto baseline = metadata->liveBytes();
        pressure.verify(renderPeak, render);
        pressure.verify(computePeak, dispatch);
        assert(metadata->liveBytes() == baseline);
        const auto entry = compute.computeShader.entryPoint;
        compute.computeShader.entryPoint = "absentOriginalEntry";
        assert(!dispatch());
        is(original->lastOriginalResourceRegistrationDiagnostic().data(),
            "RHI-ORIGINAL-PIPELINE-FACTORY-FAILED");
        assert(metadata->liveBytes() == baseline);
        compute.computeShader.entryPoint = entry;
        compute.shaderPackage = nullptr;
        assert(!dispatch());
        is(original->lastOriginalResourceRegistrationDiagnostic().data(),
            "RHI-ORIGINAL-PIPELINE-PACKAGE-MISSING");
        compute.shaderPackage = &package;
        compute.layout = nullptr;
        assert(!dispatch());
        is(original->lastOriginalResourceRegistrationDiagnostic().data(),
            "RHI-ORIGINAL-PIPELINE-LAYOUT-MISMATCH");
        compute.layout = layout.get();
        auto forged = std::shared_ptr<rhi::PipelineLayout>(objects, layout.get());
        assert(!original->createOriginalComputePipeline(compute, forged));
        is(original->lastOriginalResourceRegistrationDiagnostic().data(),
            "RHI-ORIGINAL-PIPELINE-LAYOUT-OWNER");
        assert(!original->createOriginalComputePipeline(compute, {}));
        is(original->lastOriginalResourceRegistrationDiagnostic().data(),
            "RHI-ORIGINAL-PIPELINE-LAYOUT-OWNER");
        auto ordinary = original->createPipelineLayout(desc);
        assert(ordinary);
        auto ordinaryOwner = core::shareUnique(std::move(ordinary));
        compute.layout = ordinaryOwner.get();
        assert(!original->createOriginalComputePipeline(compute, ordinaryOwner));
        is(original->lastOriginalResourceRegistrationDiagnostic().data(),
            "RHI-ORIGINAL-REGISTRATION-EXPIRED");
        compute.layout = layout.get();
        auto foreignDomain = quota(), foreignMetadata = quota();
        auto foreign = device(foreignDomain, foreignMetadata);
        assert(!foreign->createOriginalComputePipeline(compute, layout));
        is(foreign->lastOriginalResourceRegistrationDiagnostic().data(),
            "RHI-ORIGINAL-REGISTRATION-FOREIGN-DEVICE");
        auto g = render();
        auto c = dispatch();
        assert(g && c);
        is(g->originalFullDefinitionDiagnostic(), "RHI-ORIGINAL-PIPELINE-FULL-DEFINITION-MISSING");
        is(c->originalFullDefinitionDiagnostic(), "RHI-ORIGINAL-PIPELINE-FULL-DEFINITION-MISSING");
        assert(!original->validateOriginalResourceRegistration(*g)
            && !original->validateOriginalResourceRegistration(*c));
        assert(g->originalResourceRegistration().matchesParent(
            layout->originalResourceRegistration()));
        assert(c->originalResourceRegistration().matchesParent(
            layout->originalResourceRegistration()));
        assert(!c->originalResourceRegistration().matchesParent(g->originalResourceRegistration()));
        const auto* frozen = c->originalShaderPackageMetadata();
        assert(frozen && frozen != &package && frozen->label == package.label);
        package.label = "caller-mutated";
        assert(frozen->label != package.label);
        assert(!render());
        is(original->lastOriginalResourceRegistrationDiagnostic().data(),
            "RHI-ORIGINAL-REGISTRATION-KIND-CAPACITY");
        assert(!dispatch());
        is(original->lastOriginalResourceRegistrationDiagnostic().data(),
            "RHI-ORIGINAL-REGISTRATION-KIND-CAPACITY");
        auto parent = layout->originalResourceRegistration();
        auto child = c->originalResourceRegistration();
        layout.reset();
        forged.reset();
        assert(!parent.diagnostic() && !child.diagnostic());
        const auto preserved = core::inspectCpuAllocationContaining(c->layout());
        assert(preserved.allocationIdentity == allocation.allocationIdentity
            && preserved.quota == objects.get());
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{domain, metadata, objects}, [&]() noexcept {
                return original->validateOriginalResourceRegistration(*c);
            }));
        g.reset();
        assert(!parent.diagnostic());
        c.reset();
        is(parent.diagnostic(), "RHI-ORIGINAL-REGISTRATION-EXPIRED");
        is(child.diagnostic(), "RHI-ORIGINAL-REGISTRATION-EXPIRED");
        ordinaryOwner.reset();
        original.reset();
        foreign.reset();
        parent = {};
        child = {};
        assert(!domain->liveBytes() && !metadata->liveBytes() && !objects->liveBytes());
        assert(!foreignDomain->liveBytes() && !foreignMetadata->liveBytes());
        auto policy = budget();
        policy.maxRetainedOwners = 8u; // fixed3 + layout2 leaves pipeline one owner short.
        original = device(domain, metadata, policy);
        layout = core::shareUnique(original->createOriginalPipelineLayout(desc));
        assert(layout);
        compute.layout = layout.get();
        assert(!dispatch());
        is(original->lastOriginalResourceRegistrationDiagnostic().data(),
            "RHI-ORIGINAL-REGISTRATION-OWNER-CAPACITY");
        layout.reset();
        original.reset();
        assert(!domain->liveBytes() && !metadata->liveBytes());
        std::printf(
            "BINDLESS_RHI_ORIGINAL_PIPELINE_FACTORY_OWNERS_OK graphics%llu compute%llu genuine-Null reflection-artifact exact-layout-control-block retained-original-allocation deep-package-copy cold-one-less-exhausted hot-allocation0 execution-closed\n",
            static_cast<unsigned long long>(renderPeak),
            static_cast<unsigned long long>(computePeak));
    }

    template <class Factory>
    static void verifyWebGpuFacadeAndLossGate(Factory&& create, std::uint32_t version = 2u)
    {
        auto domain = quota(), metadata = quota(), subscription = quota(), objects = quota();
        auto policy = budget();
        policy.contractVersion = version;
        auto original = create();
        assert(original && original->backend() == rhi::Backend::WebGPU);
        assert(!original->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, domain));
        assert(!original->prepareOriginalResourceRegistration(policy, metadata));
        rhi::PipelineLayoutDesc desc;
        desc.pushConstantRanges.push_back({rhi::ShaderStage::Compute, 0u, 64u});
        core::UniquePtr<rhi::PipelineLayout> layout;
        {
            core::CpuMemoryQuotaScope scope(objects);
            layout = original->createOriginalPipelineLayout(desc);
        }
        assert(layout && !original->validateOriginalResourceRegistration(*layout));
        auto receipt = layout->originalResourceRegistration();
        assert(!receipt.nativeExecutionProven());
        auto peer = create();
        assert(peer);
        is(peer->validateOriginalResourceRegistration(*layout),
            "RHI-ORIGINAL-REGISTRATION-FOREIGN-DEVICE");
        original.reset();
        is(receipt.diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN");
        layout.reset();
        receipt = {};
        assert(!domain->liveBytes() && !metadata->liveBytes() && !objects->liveBytes());
        original = create();
        assert(original);
        assert(!original->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, domain));
        assert(!original->prepareOriginalDeviceNativeLoss(
            {{1u, true, 1u, 1u, bytes}, 1u}, subscription));
        assert(!original->prepareOriginalResourceRegistration(policy, metadata));
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{domain, metadata, objects}, [&]() noexcept {
                assert(!original->createOriginalPipelineLayout(desc));
                is(original->lastOriginalResourceRegistrationDiagnostic().data(),
                    "RHI-ORIGINAL-PIPELINE-NATIVE-LOSS-JOIN-MISSING");
                assert(!original->createOriginalComputePipeline({}, {}));
                is(original->lastOriginalResourceRegistrationDiagnostic().data(),
                    "RHI-ORIGINAL-PIPELINE-NATIVE-LOSS-JOIN-MISSING");
                assert(!original->createOriginalRenderPipeline({}, {}));
                is(original->lastOriginalResourceRegistrationDiagnostic().data(),
                    "RHI-ORIGINAL-PIPELINE-NATIVE-LOSS-JOIN-MISSING");
                return static_cast<const char*>(nullptr);
            }));
        original.reset();
        assert(!domain->liveBytes() && !metadata->liveBytes() && !subscription->liveBytes());
        std::puts(
            "BINDLESS_RHI_ORIGINAL_PIPELINE_FACTORY_WEBGPU_OK genuine-L2-layout exact-logical-facade shutdown native-loss-join-rejected-before-factory hot-allocation0 typed-execution-closed");
    }
};
} // namespace arp::test
