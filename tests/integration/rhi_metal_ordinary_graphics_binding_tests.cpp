#include "../fixtures/metal_native_identity_fixture.h"
#include "../fixtures/metal_ordinary_graphics_fixture.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_graphics_preparation.h"

#include <cassert>
#include <cstdio>

#ifdef NDEBUG
#error Native preparation tests require assertions
#endif

namespace {
using namespace arp;
using namespace arp::rhi;
using namespace arp::rhi_metal;
using Fixture = arp::test::OrdinaryGraphicsFixture;

void check(Fixture& f)
{
    auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
    auto prepared = f.prepare();
    assert(prepared->validateObservation().success);
    std::pmr::set_default_resource(previous);
    const auto& p = *prepared->payload();
    assert(p.pipeline == f.provider->native() && p.set == f.setIndex());
    assert(p.descriptors != metalRhiNativePayloadFor(*f.set));
    assert(
        p.layouts[0] != f.provider->stages()[0] && p.layouts[0]->matches(*f.provider->stages()[0]));
    assert(p.argumentCount == (f.arrays ? 1u : 0u));
    assert(f.arrays ? p.encodedArgumentBytes > 0u && p.argumentBytes >= p.encodedArgumentBytes
                    : p.encodedArgumentBytes == 0u && p.argumentBytes == 0u);
    const auto allocation = core::inspectCpuAllocationContaining(prepared.get());
    assert(allocation.quota == f.quota.get());
    const auto owns = [&](const std::shared_ptr<const void>& owner) {
        for (std::size_t i = 0; i < prepared->childOwnerCount(); ++i) {
            auto actual = prepared->childOwnerAt(i);
            if (actual == owner && !actual.owner_before(owner) && !owner.owner_before(actual))
                return true;
        }
        return false;
    };
    assert(owns(f.provider) && owns(p.pipeline) && owns(p.pipeline->pipelineState)
        && owns(p.descriptors));
    for (const auto& binding : p.descriptors->bindings) {
        assert(owns(binding.payloadOwner()));
        const auto& resource =
            binding.textureView ? binding.textureView->resource : binding.sampler->resource;
        assert(owns(resource.nativeObject));
        if (resource.allocationReleaseToken)
            assert(owns(resource.allocationReleaseToken));
    }
    for (const auto& stage : p.stages) {
        assert(owns(stage) && owns(stage->function));
        for (const auto& array : stage->arrays)
            assert(owns(array.buffer));
    }
    auto replacement = f.device->createTexture({"IdentityReplacement",
        {1u, 1u, 1u},
        TextureFormat::Rgba8Unorm,
        TextureUsage::Sampled,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderRead});
    assert(replacement);
    auto replacementView = f.device->createTextureView(
        *replacement, {"IdentityView", TextureFormat::Rgba8Unorm, TextureUsage::Sampled});
    assert(replacementView);
    auto replacementSampler = f.device->createSampler({});
    assert(replacementSampler);
    for (const auto& binding : p.descriptors->bindings) {
        auto& resource =
            binding.textureView ? binding.textureView->resource : binding.sampler->resource;
        const auto& other = binding.textureView
            ? metalRhiNativePayloadFor(*replacementView)->resource
            : metalRhiNativePayloadFor(*replacementSampler)->resource;
        test::MetalNativeIdentityMutationProbe::verify(resource, other, [&] {
            const auto invalid = prepared->validateObservation();
            assert(!invalid.success
                && invalid.diagnostics.find("RESOURCE-NATIVE-IDENTITY-MISMATCH")
                    != invalid.diagnostics.npos);
        });
        assert(prepared->validateObservation().success);
    }
    auto disabled = f.budget();
    disabled.optIn = false;
    f.reject(disabled, "BUDGET-INVALID");
    f.reject(f.budget(), "slot is not declared", {}, f.setIndex() + 1u);
    f.reject(f.budget(), "REGISTRY-MISMATCH", core::makeShared<MetalRhiResourceRegistry>());
    if (f.arrays) {
        auto zero = f.budget();
        zero.maxArgumentBuffers = 0;
        zero.maxArgumentBufferBytes = 0;
        f.reject(zero, "ARGUMENT-COUNT");
        auto tooSmall = f.budget();
        tooSmall.maxArgumentBufferBytes = p.argumentBytes - 1u;
        f.reject(tooSmall, "ARGUMENT");
    }
    auto nativeDescriptors =
        std::const_pointer_cast<MetalRhiNativeDescriptorGroupPayload>(p.descriptors);
    auto originalView = nativeDescriptors->bindings[0].textureView;
    nativeDescriptors->bindings[0].textureView =
        core::makeShared<MetalRhiNativeTextureViewPayload>(*originalView);
    assert(prepared->validateObservation().diagnostics.find("RESOURCE-IDENTITY-MISMATCH")
        != core::pmr::String::npos);
    nativeDescriptors->bindings[0].textureView = originalView;
    auto foreignQuota = core::makeShared<core::CpuMemoryQuota>(f.cpuBytes);
    assert(!MetalRhiOrdinaryPreparedGraphicsBinding::prepare(
        f.provider, *f.set, f.setIndex(), f.registry, f.budget(), foreignQuota, f.diagnostic));
    assert(f.diagnostic.find("PROVIDER-QUOTA-MISMATCH") != f.diagnostic.npos);
    auto wrongShape = f.inputs;
    wrongShape.bindings[0].stages = ShaderStage::Vertex | ShaderStage::Fragment;
    auto wrongSet = f.arena->allocateDescriptorSet({"Wrong", wrongShape});
    assert(wrongSet);
    assert(!MetalRhiOrdinaryPreparedGraphicsBinding::prepare(
        f.provider, *wrongSet, f.setIndex(), f.registry, f.budget(), f.quota, f.diagnostic));
    assert(f.diagnostic.find("LAYOUT-CONTRACT-MISMATCH") != f.diagnostic.npos);
    {
        core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - f.quota->liveBytes());
        f.reject(f.budget(), "ALLOCATION-FAILED");
    }
    auto mutableLayout = std::const_pointer_cast<MetalRhiPreparedGraphicsStageLayout>(p.layouts[1]);
    mutableLayout->entry += "Wrong";
    assert(!prepared->validateObservation().success);
    mutableLayout->entry.resize(mutableLayout->entry.size() - 5u);
    auto mutableStage =
        std::const_pointer_cast<MetalRhiNativePreparedFunctionBindings>(p.stages[1]);
    auto function = mutableStage->function;
    mutableStage->function = p.stages[0]->function;
    assert(!prepared->validateObservation().success);
    mutableStage->function = function;
    assert(prepared->validateObservation().success);
    f.write();
    assert(prepared->validateObservation().diagnostics.find("CONTENT-CHANGED")
        != core::pmr::String::npos);
    prepared = f.prepare();
    f.arena->reset();
    assert(!prepared->validateObservation().success);
    f.set.reset();
    assert(prepared->validateObservation().diagnostics.find("OBJECT-NOT-LIVE")
        != core::pmr::String::npos);
    f.provider.reset();
    assert(prepared->payload()->pipeline->pipelineState);
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc == 5);
    for (unsigned i = 0; i < 2; ++i) {
        std::shared_ptr<arp::core::CpuMemoryQuota> quota;
        {
            Fixture f(argv[1 + i * 2], argv[2 + i * 2], i == 0);
            quota = f.quota;
            check(f);
        }
        assert(!quota->liveBytes());
    }
    {
        Fixture f(argv[1], argv[2], true);
        auto p = f.prepare();
        f.view.reset();
        assert(!p->validateObservation().success);
    }
    {
        Fixture f(argv[1], argv[2], true);
        auto p = f.prepare();
        f.texture.reset();
        assert(!p->validateObservation().success);
    }
    {
        Fixture f(argv[1], argv[2], true);
        PipelineLayoutDesc desc;
        desc.descriptorLayouts = {f.inputs};
        desc.descriptorSets = {{0u, f.inputs.name}};
        auto layout = f.device->createPipelineLayout(desc);
        assert(layout);
        RenderPipelineDesc state;
        state.layout = layout.get();
        state.dynamicViewportScissor = true;
        state.depthStencil.format = TextureFormat::D32Float;
        state.depthStencil.depthTestEnabled = true;
        state.depthStencil.depthWriteEnabled = true;
        auto contract = semantics::nativeBackendCapabilityContract(Backend::Metal);
        contract.queriedFromNativeDevice = true;
        contract.queriedExtendedFeatures = true;
        auto budget = f.budget();
        budget.maxArgumentBuffers = 0;
        budget.maxArgumentBufferBytes = 0;
        auto provider = MetalRhiOrdinaryGraphicsProvider::create(f.provider->artifacts()[0],
            {},
            std::move(state),
            contract,
            budget,
            f.quota,
            f.diagnostic);
        assert(provider);
        layout.reset();
        auto prepared = MetalRhiOrdinaryPreparedGraphicsBinding::prepare(
            provider, *f.set, 0u, f.registry, budget, f.quota, f.diagnostic);
        if (!prepared)
            std::fprintf(stderr, "%s\n", f.diagnostic.c_str());
        assert(prepared && prepared->validateObservation().success);
        assert(!prepared->payload()->stages[1] && !prepared->payload()->layouts[1]
            && prepared->payload()->argumentCount == 0u);
    }
    std::puts(
        "ordinary graphics preparation: real scalar zero-budget and array encoders, exact copied maps/layouts, full ownership, quota cleanup and fail-closed lifecycle negatives pass; no recording gate");
}
