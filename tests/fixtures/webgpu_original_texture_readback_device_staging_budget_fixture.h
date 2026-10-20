#pragma once
#include "rhi/backends/webgpu/webgpu_readback_preparation.h"
#include "rhi/frontend/transfer_texture_readback_batch.h"
#include "webgpu_original_texture_readback_source_fixture.h"

namespace arp::test {
class WebGpuOriginalTextureReadbackDeviceStagingBudgetFixture final {
public:
    using Owner = rhi_webgpu::WebGpuOriginalTextureReadbackDeviceStagingBudget;
    using Source = rhi_webgpu::WebGpuOriginalTextureReadbackSource;
    using Destination = rhi_webgpu::WebGpuOriginalTextureReadbackDestination;
    using Recipe = TextureReadbackColdBudgetFixture::Recipe;
    static constexpr std::uint64_t bytes = 512u;

    static Recipe prepareRecipe(std::uint64_t aggregate = bytes)
    {
        auto b = TextureReadbackColdBudgetFixture::budget();
        b.maxSourceAllocationBytes = 16u;
        b.maxStagingBytesPerRequest = b.maxResultBytesPerRequest = b.maxDeviceRetainedResultBytes =
            bytes;
        b.maxDeviceStagingBytes = aggregate;
        b.maxNativeBytesPerRequest = b.maxDeviceRetainedNativeBytes = 16u + bytes;
        return TextureReadbackColdBudgetFixture::prepare(b);
    }

    static void verifyContract()
    {
        const char* diagnostic = nullptr;
        auto recipe = prepareRecipe();
        const auto q = recipe.originalPlan->quotas();
        auto registry = core::makeShared<rhi_webgpu::WebGpuRhiResourceRegistry>();
        auto plan = recipe.originalPlan;
        const auto factory = [&] {
            return Owner::prepareOriginal(plan, registry, diagnostic);
        };
        CpuQuotaFactoryFixture pressure(q[0]);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        {
            core::CpuMemoryQuotaLease full(q[0], q[0]->limitBytes() - q[0]->liveBytes());
            assert(full.charged() && !factory());
        }
        auto saved = plan;
        plan = std::shared_ptr<const rhi::TextureReadbackColdBudgetPlan>(q[1], saved.get());
        assert(!factory());
        plan = saved;
        saved.reset();
        auto original = registry;
        registry = std::shared_ptr<rhi_webgpu::WebGpuRhiResourceRegistry>(q[0], original.get());
        assert(!factory());
        registry = original;
        original.reset();
        auto owner = factory();
        assert(
            owner && !owner->nativeExecutionProven() && !owner->usedBytes() && !owner->peakBytes());
        auto forged = std::shared_ptr<const Owner>(q[0], owner.get());
        assert(Owner::validateOriginal(forged) && !Owner::reserveOriginal(forged, {}, diagnostic));
        const auto previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto a = core::defaultCpuAllocator().stats().allocationCount;
        assert(!Owner::reserveOriginal(owner, {}, diagnostic)
            && !Owner::retainAllocationOriginal(owner, {}, {}, diagnostic));
        assert(!Owner::validateOriginal(owner)
            && a == core::defaultCpuAllocator().stats().allocationCount);
        std::pmr::set_default_resource(previous);
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto l = Owner::lowering(backend);
            assert(l.implemented == (backend == rhi::Backend::WebGPU)
                && l.validatorOnly == (backend == rhi::Backend::Null) && l.diagnostic);
        }
        forged.reset();
        owner.reset();
        plan.reset();
        recipe = {};
        for (const auto& quota : q)
            assert(!quota->liveBytes());
        std::printf(
            "BINDLESS_RHI_READBACK_DEVICE_STAGING_CONTRACT_OK %llu exact-less-exhausted Device-domain forged-original backends4 allocation0 execution-closed\n",
            static_cast<unsigned long long>(peak));
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    class Context final {
    public:
        WebGpuOriginalTextureReadbackSourceFixture::Context caller{bytes, "device staging 原始"};
        Recipe recipe;
        rhi::TextureReadbackColdBudgetPlan::Quotas quotas;
        std::shared_ptr<const Owner> owner;
        std::shared_ptr<const Source> source;

        explicit Context(std::uint64_t aggregate = bytes, bool snapshot = false)
            : recipe(prepareRecipe(aggregate)), quotas(recipe.originalPlan->quotas())
        {
            caller.makeTexture(true);
            const char* diagnostic = nullptr;
            if (snapshot) {
                rhi::ReadbackTextureDesc input;
                caller.layout->inputOwner()->scalarFields().writeScalarFields(input);
                input.label = caller.layout->inputOwner()->label();
                for (const auto& mip : caller.layout->inputOwner()->mipLayouts())
                    input.mipLayouts.push_back(mip);
                caller.layout.reset();
                caller.quotas.fill(quotas[1]);
                auto original = rhi::TextureReadbackInputSnapshot::prepareOriginal(
                    input, recipe.input, quotas[1], quotas[1], diagnostic);
                assert(original);
                caller.layout = rhi::TextureReadbackLayout::prepareOriginal(
                    original, caller.desc, recipe.layout, quotas[1], diagnostic);
                assert(caller.layout);
            }
            auto request = rhi::TextureReadbackRequestDeclaration::prepareOriginal(caller.layout,
                {rhi::allocateTextureReadbackRequestId(), 37u, 43u},
                recipe.request,
                quotas[1],
                diagnostic);
            assert(request);
            auto lease = rhi::TextureReadbackSourceLease::prepareOriginal(
                caller.texture, request, recipe.sourceLease, quotas[1], diagnostic);
            assert(lease);
            source = snapshot
                ? Source::prepareLeasedSnapshotOriginal(caller.layout,
                      caller.registry,
                      lease,
                      TextureReadbackContentIntentFixture::descriptor(),
                      recipe.source,
                      quotas[1],
                      diagnostic)
                : Source::prepareLeasedOriginal(
                      caller.layout, caller.registry, lease, recipe.source, quotas[1], diagnostic);
            assert(source);
            owner = Owner::prepareOriginal(recipe.originalPlan, caller.registry, diagnostic);
            assert(owner);
        }

        auto prepare(const char*& diagnostic)
        {
            return Destination::prepareDeviceOriginal(
                source, recipe.destination, owner, diagnostic);
        }

        void retire()
        {
            source.reset();
            owner.reset();
            recipe = {};
            caller.retire();
            for (const auto& q : quotas)
                assert(!q->liveBytes());
        }
    };

    static void verifyActual()
    {
        verifyPressure();
        verifyAggregate(2u * bytes, true);
        verifyAggregate(2u * bytes - 1u, false);
        verifyAggregate(bytes, false);
        verifyLastNativeOwner();
        std::puts(
            "BINDLESS_RHI_READBACK_DEVICE_STAGING_NATIVE_OK exact-less-exhausted aggregate pre-native CPU-rollback foreign-registry lowering-mismatch last-native-retention domains4");
    }

    static void prepareActualLoss()
    {
        loss_ = core::makeUnique<Context>();
        const char* diagnostic = nullptr;
        lossDestination_ = loss_->prepare(diagnostic);
        assert(lossDestination_);
        initialize(lossDestination_);
        assert(loss_->owner->usedBytes() == bytes);
    }

    static void verifyActualDeviceLoss()
    {
        assert(rhi_webgpu::webGpuNativeDeviceLost()
            && lossDestination_->status() == Destination::Status::DeviceLost
            && Destination::initialize(lossDestination_));
        auto native = lossDestination_->nativeResource();
        auto owner = loss_->owner;
        lossDestination_.reset();
        assert(owner->usedBytes() == bytes);
        native.reset();
        assert(!owner->usedBytes());
        owner.reset();
        loss_->retire();
        loss_.reset();
        std::puts(
            "BINDLESS_RHI_READBACK_DEVICE_STAGING_LOSS_OK actual-loss default-reject retained-native-charge exact-retire");
    }

private:
    static void initialize(const std::shared_ptr<Destination>& destination)
    {
        assert(!Destination::initialize(destination));
        assert(WebGpuAsyncWaitFixture::until([&] {
            return destination->status() != Destination::Status::Pending;
        }));
        assert(destination->status() == Destination::Status::CreationReady
            && !destination->validate());
    }

    static void verifyPressure()
    {
        Context c;
        const char* diagnostic = nullptr;
        const auto factory = [&] {
            return c.prepare(diagnostic);
        };
        for (const auto index : {1u, 2u, 3u}) {
            CpuQuotaFactoryFixture pressure(c.quotas[index]);
            const auto peak = pressure.measure(factory);
            pressure.verify(peak, factory);
            {
                core::CpuMemoryQuotaLease full(
                    c.quotas[index], c.quotas[index]->limitBytes() - c.quotas[index]->liveBytes());
                assert(full.charged() && !factory());
            }
            assert(!c.owner->usedBytes());
        }
        auto forgedSource = std::shared_ptr<const Source>(c.quotas[0], c.source.get());
        assert(!Owner::reserveOriginal(c.owner, forgedSource, diagnostic)
            && std::string_view(diagnostic) == "RHI-READBACK-DEVICE-STAGING-SOURCE-OWNER");
        forgedSource.reset();
        auto charge = Owner::reserveOriginal(c.owner, c.source, diagnostic);
        assert(charge && charge->bytes() == bytes);
        auto forgedCharge = std::shared_ptr<const Owner::Charge>(c.quotas[0], charge.get());
        assert(!Owner::retainAllocationOriginal(c.owner,
            c.source->resource().texture->allocationReleaseToken,
            forgedCharge,
            diagnostic));
        forgedCharge.reset();
        charge.reset();
        assert(!c.owner->usedBytes());
        auto exact = c.recipe.destination;
        --c.recipe.destination.maxGpuBufferBytes;
        assert(!factory()
            && std::string_view(diagnostic) == "RHI-READBACK-DEVICE-STAGING-LOWERING-MISMATCH");
        c.recipe.destination = exact;
        auto foreign = core::makeShared<rhi_webgpu::WebGpuRhiResourceRegistry>();
        auto foreignOwner = Owner::prepareOriginal(c.recipe.originalPlan, foreign, diagnostic);
        assert(foreignOwner);
        assert(!Destination::prepareDeviceOriginal(
                   c.source, c.recipe.destination, foreignOwner, diagnostic)
            && std::string_view(diagnostic) == "RHI-READBACK-DEVICE-STAGING-REGISTRY-MISMATCH");
        foreignOwner.reset();
        foreign.reset();
        c.retire();
    }

    static void verifyAggregate(std::uint64_t capacity, bool secondSucceeds)
    {
        Context c(capacity);
        const char* diagnostic = nullptr;
        EM_ASM({
            // clang-format off
                Module.arpDeviceStagingCreateCount = 0;
                Module.arpDeviceStagingOriginalCreate = Module.preinitializedWebGPUDevice.createBuffer;
                Module.preinitializedWebGPUDevice.createBuffer = function() {
                    Module.arpDeviceStagingCreateCount++;
                    return Module.arpDeviceStagingOriginalCreate.apply(this, arguments);
                };
            // clang-format on
        });
        auto first = c.prepare(diagnostic);
        assert(first && c.owner->usedBytes() == bytes);
        auto second = c.prepare(diagnostic);
        assert(bool(second) == secondSucceeds
            && c.owner->usedBytes() == (secondSucceeds ? 2u * bytes : bytes));
        if (!secondSucceeds)
            assert(std::string_view(diagnostic) == "RHI-READBACK-DEVICE-STAGING-CAPACITY"
                && c.owner->capacityRejections() == 1u);
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpDeviceStagingCreateCount;
            // clang-format on
        }) == 0);
        initialize(first);
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpDeviceStagingCreateCount;
            // clang-format on
        }) == 1);
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.createBuffer = Module.arpDeviceStagingOriginalCreate;
                delete Module.arpDeviceStagingOriginalCreate;
            // clang-format on
        });
        first.reset();
        second.reset();
        assert(!c.owner->usedBytes());
        c.retire();
    }

    static void verifyLastNativeOwner()
    {
        Context c;
        const char* diagnostic = nullptr;
        auto destination = c.prepare(diagnostic);
        assert(destination);
        initialize(destination);
        auto native = destination->nativeResource();
        auto token = native->allocationReleaseToken;
        auto owner = c.owner;
        auto quotas = c.quotas;
        destination.reset();
        c.source.reset();
        c.owner.reset();
        c.recipe = {};
        c.caller.layout.reset();
        c.caller.registry.reset();
        assert(owner->usedBytes() == bytes && !quotas[3]->liveBytes());
        native.reset();
        assert(owner->usedBytes() == bytes);
        token.reset();
        assert(!owner->usedBytes());
        owner.reset();
        for (const auto& q : quotas)
            assert(!q->liveBytes());
        c.caller.retire();
    }

    static inline core::UniquePtr<Context> loss_;
    static inline std::shared_ptr<Destination> lossDestination_;
#endif
};
} // namespace arp::test
