#pragma once
#include "rhi/backends/webgpu/webgpu_readback_preparation.h"
#include "texture_readback_cold_budget_fixture.h"
#include "texture_readback_content_intent_fixture.h"
#include "texture_readback_layout_fixture.h"
#include "texture_readback_source_lease_fixture.h"
#include "webgpu_original_texture_upload_destination_fixture.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class WebGpuOriginalTextureReadbackSourceFixture final {
    using Source = rhi_webgpu::WebGpuOriginalTextureReadbackSource;
    using Budget = rhi_webgpu::WebGpuOriginalTextureReadbackSourceBudget;
    static constexpr std::uint64_t cpuBytes = 65536u, textureBytes = 16u;

    static Budget budget() { return {1u, true, 3u, 1u, textureBytes, cpuBytes}; }

public:
    class Context final {
    public:
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 4> quotas;
        std::shared_ptr<rhi_webgpu::WebGpuRhiResourceRegistry> registry =
            core::makeShared<rhi_webgpu::WebGpuRhiResourceRegistry>();
        rhi_webgpu::WebGpuRhiAllocator allocator{{{}, 2u * textureBytes, false}};
        const rhi::GpuResourceId id = rhi::allocateGpuResourceId();
        rhi::TextureDesc desc;
        core::UniquePtr<rhi::GpuTexture> texture;
        std::shared_ptr<const rhi::TextureReadbackLayout> layout;

        explicit Context(std::uint64_t capacity = 512u, std::string_view label = {})
        {
            for (auto& q : quotas)
                q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
            desc.extent = {2u, 2u, 1u};
            desc.format = rhi::TextureFormat::Rgba8Unorm;
            desc.usage = rhi::TextureUsage::CopySrc | rhi::TextureUsage::CopyDst;
            desc.memory = rhi::MemoryUsage::GpuOnly;
            desc.initialState = rhi::ResourceState::CopySrc;
            rhi::ReadbackTextureDesc d;
            d.label = label;
            d.sourceResource = id;
            d.destinationReadbackBuffer = rhi::allocateGpuResourceId();
            d.sizeBytes = capacity;
            d.sourceSubresources = {0u, 1u, 0u, 1u, rhi::TextureAspect::Color};
            d.extent = desc.extent;
            d.bytesPerRow = 256u;
            d.rowsPerImage = 2u;
            const char* diagnostic = nullptr;
            auto input = rhi::TextureReadbackInputSnapshot::prepareOriginal(
                d, {1u, true, 1u, 128u, cpuBytes, cpuBytes}, quotas[0], quotas[1], diagnostic);
            assert(input);
            layout = rhi::TextureReadbackLayout::prepareOriginal(
                input, desc, {1u, true, 1u, capacity, cpuBytes}, quotas[2], diagnostic);
            assert(layout);
        }

        void makeTexture(bool native)
        {
            texture.reset();
            auto allocation = allocator.allocate({{}, textureBytes, rhi::MemoryUsage::GpuOnly});
            assert(allocation.success);
            texture = rhi_webgpu::createWebGpuRhiTexture(
                id, desc, allocation, {native, native}, registry);
            assert(texture);
            registry->registerTexture(*texture);
        }

        auto prepare(const Budget& b, const char*& diagnostic)
        {
            return Source::prepareOriginal(layout, registry, texture, b, quotas[3], diagnostic);
        }

        void retire()
        {
            texture.reset();
            layout.reset();
            registry.reset();
            assert(!allocator.memoryStats().allocatedBytes);
            for (const auto& q : quotas)
                assert(!q->liveBytes());
        }
    };

    static void verifyContract()
    {
        TextureReadbackContentIntentFixture::verify();
        {
            Context leaseContext(512u, "source lease 原始");
            TextureReadbackSourceLeaseFixture::verifyCpu(leaseContext);
        }
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto result = Source::lowering(backend);
            assert(result.validatorOnly == (backend == rhi::Backend::Null));
            assert(result.nativeBindingImplemented == (backend == rhi::Backend::WebGPU)
                && result.diagnostic);
        }
        assert(!Source::lowering(static_cast<rhi::Backend>(99)).nativeBindingImplemented);
        Budget b;
        assert(std::string_view(Source::validateBudget(b)) == "RHI-READBACK-SOURCE-VERSION");
        b.contractVersion = 1u;
        assert(std::string_view(Source::validateBudget(b)) == "RHI-READBACK-SOURCE-OPT-IN");
        b.prototypeOptIn = true;
        assert(std::string_view(Source::validateBudget(b)) == "RHI-READBACK-SOURCE-BUDGET");
        b = budget();
        assert(!Source::validateBudget(b));
        Context c;
        const char* diagnostic = nullptr;
        auto reject = [&](const char* expected) {
            assert(!c.prepare(b, diagnostic) && std::string_view(diagnostic) == expected
                && !c.quotas[3]->liveBytes());
        };
        --b.maxCpuMetadataBytes;
        reject("RHI-READBACK-SOURCE-QUOTA");
        ++b.maxCpuMetadataBytes;
        auto layout = c.layout;
        c.layout = std::shared_ptr<const rhi::TextureReadbackLayout>(c.quotas[0], layout.get());
        reject("RHI-READBACK-SOURCE-LAYOUT-OWNER");
        c.layout = std::move(layout);
        auto registry = c.registry;
        c.registry =
            std::shared_ptr<rhi_webgpu::WebGpuRhiResourceRegistry>(c.quotas[0], registry.get());
        reject("RHI-READBACK-SOURCE-REGISTRY-OWNER");
        c.registry = std::move(registry);
        reject("RHI-READBACK-SOURCE-OBJECT-MISSING");
        c.makeTexture(false);
        const auto* original = c.texture.get();
        assert(!Source::prepareOriginal(c.layout, c.registry, c.texture, b, {}, diagnostic)
            && std::string_view(diagnostic) == "RHI-READBACK-SOURCE-QUOTA");
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        reject("RHI-READBACK-SOURCE-NATIVE-UNAVAILABLE");
#else
        reject("RHI-WEBGPU-TRANSFER-NATIVE-OWNER-MISSING");
#endif
        assert(c.texture.get() == original && c.registry->find(c.id));
        c.retire();
        std::puts(
            "BINDLESS_RHI_READBACK_SOURCE_CONTRACT_OK four-backend original-object failure-preserves-owner execution-closed");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyActual()
    {
        verifyLeased();
        verifyCold();
        verifyLifetimeAndMutation();
        verifyStateAndReplacement();
        verifyExclusive();
        std::puts(
            "BINDLESS_RHI_READBACK_SOURCE_NATIVE_OK registered original-object owners3 native-identity state-revision allocation0 caller-drop execution-closed");
    }

    static void prepareActualLoss()
    {
        ordinaryLossContext_ = core::makeUnique<Context>();
        ordinaryLossContext_->makeTexture(true);
        const char* ordinaryDiagnostic = nullptr;
        ordinaryLossSource_ = ordinaryLossContext_->prepare(budget(), ordinaryDiagnostic);
        assert(ordinaryLossSource_ && !ordinaryLossSource_->validate());
        lossContext_ = core::makeUnique<Context>();
        lossContext_->makeTexture(true);
        const char* diagnostic = nullptr;
        lossLeaseQuotas_[0] = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        lossLeaseQuotas_[1] = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto request =
            TextureReadbackSourceLeaseFixture::request(lossContext_->layout, lossLeaseQuotas_[0]);
        auto lease = rhi::TextureReadbackSourceLease::prepareOriginal(lossContext_->texture,
            request,
            TextureReadbackSourceLeaseFixture::budget(request),
            lossLeaseQuotas_[1],
            diagnostic);
        assert(lease);
        auto b = budget();
        b.maxSourceOwners = 5u;
        lossSource_ = Source::prepareLeasedOriginal(lossContext_->layout,
            lossContext_->registry,
            lease,
            b,
            lossContext_->quotas[3],
            diagnostic);
        assert(lossSource_ && !lossSource_->validate());
    }

    static void verifyActualDeviceLoss()
    {
        assert(rhi_webgpu::webGpuNativeDeviceLost());
        assert(
            std::string_view(ordinaryLossSource_->validate()) == "RHI-READBACK-SOURCE-DEVICE-LOST");
        ordinaryLossSource_.reset();
        ordinaryLossContext_->retire();
        ordinaryLossContext_.reset();
        assert(std::string_view(lossSource_->validate()) == "RHI-READBACK-SOURCE-DEVICE-LOST");
        assert(std::string_view(lossSource_->executionDiagnostic())
            == "RHI-READBACK-SOURCE-DEVICE-LOST");
        std::weak_ptr<rhi_webgpu::WebGpuNativeTexturePayload> native =
            lossSource_->resource().texture;
        lossSource_.reset();
        assert(native.expired());
        native.reset();
        lossContext_->retire();
        lossContext_.reset();
        for (const auto& q : lossLeaseQuotas_)
            assert(!q->liveBytes());
        lossLeaseQuotas_ = {};
        std::puts(
            "BINDLESS_RHI_READBACK_SOURCE_LEASE_LOSS_OK actual-device-loss native-binding-rejected exact-source-quota-retire");
        std::puts(
            "BINDLESS_RHI_READBACK_SOURCE_LOSS_OK unsubmitted reject exact-native-cpu-retire");
    }

private:
    static void verifyLeased()
    {
        Context c(512u, "public native source lease");
        c.makeTexture(true);
        const auto* original = c.texture.get();
        auto publicBudget = TextureReadbackColdBudgetFixture::budget();
        publicBudget.maxSourceAllocationBytes = textureBytes;
        auto recipe = TextureReadbackColdBudgetFixture::prepare(publicBudget);
        c.quotas[3] = recipe.originalPlan->quotas()[1];
        auto requestQuota = c.quotas[3], leaseQuota = c.quotas[3];
        const char* diagnostic = nullptr;
        auto request = rhi::TextureReadbackRequestDeclaration::prepareOriginal(
            c.layout, {17u, 23u, 29u}, recipe.request, requestQuota, diagnostic);
        assert(request);
        auto lease = rhi::TextureReadbackSourceLease::prepareOriginal(
            c.texture, request, recipe.sourceLease, leaseQuota, diagnostic);
        assert(lease && !c.texture);
        auto b = recipe.source;
        b.maxSourceOwners = rhi::TextureReadbackSourceLease::retainedOwnerCount + 3u;
        const auto factory = [&] {
            return Source::prepareLeasedOriginal(
                c.layout, c.registry, lease, b, c.quotas[3], diagnostic);
        };
        CpuQuotaFactoryFixture pressure(c.quotas[3]);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        --b.maxSourceOwners;
        assert(!factory() && std::string_view(diagnostic) == "RHI-READBACK-SOURCE-CAPACITY");
        ++b.maxSourceOwners;
        --b.maxRetainedAllocationBytes;
        assert(!factory());
        ++b.maxRetainedAllocationBytes;
        {
            core::CpuMemoryQuotaLease full(
                c.quotas[3], c.quotas[3]->limitBytes() - c.quotas[3]->liveBytes());
            assert(full.charged() && !factory());
        }
        auto saved = lease;
        lease = std::shared_ptr<const rhi::TextureReadbackSourceLease>(leaseQuota, saved.get());
        assert(!factory());
        lease = saved;
        saved.reset();
        Context foreign(512u, "public native source lease");
        auto layout = c.layout;
        c.layout = foreign.layout;
        assert(!factory()
            && std::string_view(diagnostic) == "RHI-READBACK-SOURCE-LEASE-LAYOUT-MISMATCH");
        c.layout = layout;
        layout.reset();
        foreign.retire();
        auto bound = factory();
        assert(bound && !bound->validate() && bound->sourceTexture() == original
            && bound->retainedSourceOwnerCount() == 5u
            && core::SharedOwnerProjection::sameOwner(bound->publicSourceLease(), lease)
            && !bound->hasCompletedUpload());
        const auto a = core::defaultCpuAllocator().stats().allocationCount;
        auto& payload = *bound->resource().texture;
        auto token = payload.allocationReleaseToken;
        payload.allocationReleaseToken = {};
        assert(bound->validate());
        payload.allocationReleaseToken = token;
        assert(!bound->validate());
        assert(a == core::defaultCpuAllocator().stats().allocationCount);
        core::MutationJournal journal;
        assert(!payload.recordState(rhi::ResourceState::CopySrc, &journal));
        assert(std::string_view(bound->validate()) == "RHI-READBACK-SOURCE-STATE-PENDING");
        journal.rollback();
        assert(std::string_view(bound->validate()) == "RHI-READBACK-SOURCE-STATE-STALE");
        bound.reset();
        bound = factory();
        assert(bound && !bound->validate());
        std::weak_ptr<const rhi::TextureReadbackSourceLease> weak = lease;
        std::weak_ptr<rhi_webgpu::WebGpuNativeTexturePayload> native = bound->resource().texture;
        c.layout.reset();
        c.registry.reset();
        request.reset();
        lease.reset();
        assert(!weak.expired() && !bound->validate());
        bound.reset();
        assert(weak.expired() && native.expired());
        weak.reset();
        native.reset();
        token.reset();
        assert(!requestQuota->liveBytes() && !leaseQuota->liveBytes());
        c.retire();
        std::puts(
            "BINDLESS_RHI_READBACK_COLD_BUDGET_SOURCE_OK exact-recipe Request-domain original-leased-native allocation0");
        std::printf(
            "BINDLESS_RHI_READBACK_SOURCE_LEASE_NATIVE_OK %llu original-registered lease-retained source5 native-mutation pending-stale caller-drop allocation0\n",
            static_cast<unsigned long long>(peak));
    }

    static void verifyCold()
    {
        Context c;
        auto b = budget();
        const char* diagnostic = nullptr;
        const auto factory = [&] {
            c.makeTexture(true);
            const auto* ptr = c.texture.get();
            auto object = c.prepare(b, diagnostic);
            assert(object ? !c.texture : c.texture.get() == ptr);
            return object;
        };
        CpuQuotaFactoryFixture admission(c.quotas[3]);
        const auto peak = admission.measure(factory);
        admission.verify(peak, factory);
        {
            core::CpuMemoryQuotaLease full(c.quotas[3], cpuBytes);
            assert(full.charged() && !factory());
        }
        assert(!c.quotas[3]->liveBytes());
        c.makeTexture(true);
        const auto* ptr = c.texture.get();
        const auto reject = [&](const char* expected) {
            assert(!c.prepare(b, diagnostic) && std::string_view(diagnostic) == expected
                && c.texture.get() == ptr && c.registry->find(c.id) && !c.quotas[3]->liveBytes());
        };
        --b.maxSourceOwners;
        reject("RHI-READBACK-SOURCE-CAPACITY");
        ++b.maxSourceOwners;
        --b.maxRetainedAllocationBytes;
        reject("RHI-READBACK-SOURCE-CAPACITY");
        ++b.maxRetainedAllocationBytes;
        b.maxMipCopies = 0u;
        reject("RHI-READBACK-SOURCE-BUDGET");
        b.maxMipCopies = 1u;
        auto payload = rhi_webgpu::webGpuNativePayloadFor(*c.texture);
        core::MutationJournal journal;
        assert(!payload->recordState(rhi::ResourceState::CopySrc, &journal));
        reject("RHI-READBACK-SOURCE-STATE-PENDING");
        journal.rollback();
        payload->invalidateRecordedState();
        reject("RHI-READBACK-SOURCE-STATE-INVALIDATED");
        payload.reset();
        c.retire();
        Context foreign;
        ++foreign.desc.extent.width;
        foreign.makeTexture(true);
        const auto* foreignPtr = foreign.texture.get();
        assert(!foreign.prepare(budget(), diagnostic)
            && std::string_view(diagnostic) == "RHI-READBACK-SOURCE-INPUT-MISMATCH"
            && foreign.texture.get() == foreignPtr);
        foreign.retire();
        std::printf(
            "BINDLESS_RHI_READBACK_SOURCE_COLD_OK %llu exact-less-exhausted failure-preserves-registration\n",
            static_cast<unsigned long long>(peak));
    }

    static void verifyLifetimeAndMutation()
    {
        Context c;
        c.makeTexture(true);
        const auto* ptr = c.texture.get();
        const char* diagnostic = nullptr;
        EM_ASM({
            // clang-format off
                Module.arpReadbackBindingCalls = 0;
                Module.arpReadbackBindingOriginals = {};
                for (let name of ['createTexture', 'createBuffer', 'createCommandEncoder']) {
                    Module.arpReadbackBindingOriginals[name] = Module.preinitializedWebGPUDevice[name];
                    Module.preinitializedWebGPUDevice[name] = function() {
                        Module.arpReadbackBindingCalls++;
                        return Module.arpReadbackBindingOriginals[name].apply(this, arguments);
                    };
                }
            // clang-format on
        });
        auto object = c.prepare(budget(), diagnostic);
        assert(object && !c.texture && object->sourceTexture() == ptr && !object->validate());
        assert(object->resource().ownerCount() == 2u
            && object->resource().allocationBytes == textureBytes);
        auto& payload = *object->resource().texture;
        auto extent = payload.extent;
        ++payload.extent.width;
        assert(object->validate());
        payload.extent = extent;
        auto device = payload.ownerDevice;
        payload.ownerDevice = nullptr;
        assert(object->validate());
        payload.ownerDevice = device;
        auto native = payload.texture;
        payload.texture = nullptr;
        assert(object->validate());
        payload.texture = native;
        auto allocation = payload.allocationReleaseToken;
        payload.allocationReleaseToken = core::makeShared<std::uint64_t>(textureBytes);
        assert(object->validate());
        payload.allocationReleaseToken = allocation;
        const auto previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        {
            core::CpuMemoryQuotaLease full(c.quotas[3], cpuBytes - c.quotas[3]->liveBytes());
            assert(full.charged());
            assert(!object->validate()
                && std::string_view(object->executionDiagnostic())
                    == "RHI-READBACK-SOURCE-EXECUTION-UNPROVEN");
        }
        assert(core::defaultCpuAllocator().stats().allocationCount == before);
        std::pmr::set_default_resource(previous);
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpReadbackBindingCalls;
            // clang-format on
        }) == 0);
        EM_ASM({
            // clang-format off
                for (var name of Object.keys(Module.arpReadbackBindingOriginals))
                    Module.preinitializedWebGPUDevice[name] = Module.arpReadbackBindingOriginals[name];
                delete Module.arpReadbackBindingOriginals;
            // clang-format on
        });
        std::weak_ptr<rhi_webgpu::WebGpuRhiResourceRegistry> registry = c.registry;
        std::weak_ptr<rhi_webgpu::WebGpuNativeTexturePayload> weak = object->resource().texture;
        c.layout.reset();
        c.registry.reset();
        assert(!object->validate() && !registry.expired());
        object.reset();
        allocation.reset();
        assert(registry.expired() && weak.expired());
        registry.reset();
        weak.reset();
        c.retire();
    }

    static void verifyStateAndReplacement()
    {
        Context c;
        c.makeTexture(true);
        const char* diagnostic = nullptr;
        auto object = c.prepare(budget(), diagnostic);
        assert(object);
        auto& payload = *object->resource().texture;
        core::MutationJournal journal;
        assert(!payload.recordState(payload.recordedState().state(), &journal));
        assert(std::string_view(object->validate()) == "RHI-READBACK-SOURCE-STATE-PENDING");
        journal.rollback();
        assert(std::string_view(object->validate()) == "RHI-READBACK-SOURCE-STATE-STALE");
        object.reset();
        c.makeTexture(true);
        object = c.prepare(budget(), diagnostic);
        assert(object);
        c.makeTexture(true);
        assert(
            std::string_view(object->validate()) == "RHI-WEBGPU-TRANSFER-ORIGINAL-OWNER-OR-LAYOUT");
        object.reset();
        assert(c.registry->find(c.id)->texture == rhi_webgpu::webGpuNativePayloadFor(*c.texture));
        c.retire();
    }

    static void verifyExclusive()
    {
        // Negative alias fixture only: uses an actual exclusive upload target's
        // immutable policy, never represents a second successful source owner.
        WebGpuOriginalTextureUploadDestinationFixture::Context upload;
        const char* diagnostic = nullptr;
        auto target =
            WebGpuOriginalTextureUploadDestinationFixture::prepareDestination(upload, diagnostic);
        assert(target);
        assert(!rhi_webgpu::WebGpuOriginalTextureUploadDestination::initialize(target));
        assert(WebGpuAsyncWaitFixture::until([&] {
            return target->status()
                != rhi_webgpu::WebGpuOriginalTextureUploadDestination::Status::Pending;
        }));
        assert(!target->validate());
        Context c;
        c.desc = target->texture()->desc();
        rhi::ReadbackTextureDesc d;
        d.sourceResource = c.id;
        d.destinationReadbackBuffer = rhi::allocateGpuResourceId();
        d.sizeBytes = 512u;
        d.sourceSubresources = {0u, 1u, 0u, 1u, rhi::TextureAspect::Color};
        d.extent = {2u, 2u, 1u};
        d.bytesPerRow = 256u;
        d.rowsPerImage = 2u;
        auto input = rhi::TextureReadbackInputSnapshot::prepareOriginal(
            d, {1u, true, 1u, 128u, cpuBytes, cpuBytes}, c.quotas[0], c.quotas[1], diagnostic);
        assert(input);
        c.layout = rhi::TextureReadbackLayout::prepareOriginal(
            input, c.desc, {1u, true, 1u, 512u, cpuBytes}, c.quotas[2], diagnostic);
        assert(c.layout);
        input.reset();
        rhi_webgpu::WebGpuRhiAllocation allocation;
        allocation.success = true;
        allocation.sizeBytes = target->resource().allocationBytes;
        allocation.releaseToken = target->resource().texture->allocationReleaseToken;
        c.texture = rhi_webgpu::createWebGpuRhiTexture(
            c.id, c.desc, allocation, target->resource().texture, c.registry);
        assert(c.texture);
        c.registry->registerTexture(*c.texture);
        const auto* ptr = c.texture.get();
        assert(!c.prepare(budget(), diagnostic)
            && std::string_view(diagnostic) == "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE"
            && c.texture.get() == ptr);
        auto rq = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto request = TextureReadbackSourceLeaseFixture::request(c.layout, rq);
        auto lease = rhi::TextureReadbackSourceLease::prepareOriginal(
            c.texture, request, TextureReadbackSourceLeaseFixture::budget(request), rq, diagnostic);
        assert(lease);
        auto sb = budget();
        sb.maxSourceOwners = 5u;
        assert(!Source::prepareLeasedSnapshotOriginal(c.layout,
                   c.registry,
                   lease,
                   TextureReadbackContentIntentFixture::descriptor(),
                   sb,
                   c.quotas[3],
                   diagnostic)
            && std::string_view(diagnostic) == "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE");
        lease.reset();
        request.reset();
        assert(!rq->liveBytes());
        c.retire();
        allocation.releaseToken.reset();
        target.reset();
        upload.retire();
    }

    static inline core::UniquePtr<Context> ordinaryLossContext_;
    static inline std::shared_ptr<const Source> ordinaryLossSource_;
    static inline core::UniquePtr<Context> lossContext_;
    static inline std::shared_ptr<const Source> lossSource_;
    static inline std::array<std::shared_ptr<core::CpuMemoryQuota>, 2> lossLeaseQuotas_;
#endif
};
} // namespace arp::test
