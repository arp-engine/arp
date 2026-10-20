#pragma once
#include "rhi/backends/webgpu/webgpu_native_command_payload_impl.h"
#include "rhi/backends/webgpu/webgpu_transfer_copy_validation.h"
#include "rhi/backends/webgpu/webgpu_upload_preparation.h"
#include "rhi/frontend/transfer_upload_destination_validation.h"
#include "texture_upload_packing_fixture.h"
#include "webgpu_async_wait_fixture.h"
#include "webgpu_error_scope_handoff_fixture.h"

#include <cassert>
#include <cstdio>
#include <type_traits>

namespace arp::test {
class WebGpuOriginalTextureUploadDestinationFixture final {
    using Destination = rhi_webgpu::WebGpuOriginalTextureUploadDestination;
    using Budget = rhi_webgpu::WebGpuOriginalTextureUploadDestinationBudget;
    using Ledger = rhi::TextureUploadContentLedger;
    using Source = rhi::TextureUploadContentSource;
    static constexpr std::uint64_t cpuBytes = 65536u, sourceBytes = 1696u, texelBytes = 1360u;

public:
    class Context final {
    public:
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 9> quotas;
        std::shared_ptr<rhi_webgpu::WebGpuRhiResourceRegistry> registry =
            core::makeShared<rhi_webgpu::WebGpuRhiResourceRegistry>();
        std::shared_ptr<const Source> source;
        rhi::TextureDesc desc = TextureUploadPackingFixture::contentDestination();

        explicit Context(rhi::GpuResourceId id = rhi::allocateGpuResourceId())
        {
            for (auto& q : quotas)
                q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
            desc.usage = rhi::TextureUsage::CopyDst | rhi::TextureUsage::CopySrc
                | rhi::TextureUsage::Sampled;
            std::array<std::uint8_t, 160> pixels{};
            for (std::size_t i = 0u; i < pixels.size(); ++i)
                pixels[i] = static_cast<std::uint8_t>(i + 1u);
            auto input = TextureUploadPackingFixture::contentInput(pixels.data());
            input.destinationTexture = id;
            std::array<std::shared_ptr<core::CpuMemoryQuota>, 5> q;
            for (std::size_t i = 0u; i < q.size(); ++i)
                q[i] = quotas[i];
            const char* diagnostic = nullptr;
            auto packing =
                TextureUploadPackingFixture::prepareContentPacking(input, desc, q, diagnostic);
            assert(packing);
            source = Source::prepareOriginal(std::move(packing),
                {1u, id, 59u},
                {1u, true, sourceBytes, cpuBytes},
                quotas[5],
                diagnostic);
            assert(source);
            // All local authoring storage is destroyed on return.
        }

        void retire()
        {
            source.reset();
            registry.reset();
            for (std::size_t i = 0u; i < quotas.size(); ++i) {
                if (quotas[i]->liveBytes())
                    std::printf("TEXTURE_DESTINATION_QUOTA_RETAINED %zu %llu\n",
                        i,
                        static_cast<unsigned long long>(quotas[i]->liveBytes()));
                assert(!quotas[i]->liveBytes());
            }
        }

        void coldEmpty() const
        {
            for (std::size_t i = 6u; i < quotas.size(); ++i)
                assert(!quotas[i]->liveBytes());
        }
    };

private:
    static Budget budget()
    {
        return {1u,
            true,
            Destination::callbackCount,
            sourceBytes,
            texelBytes,
            cpuBytes,
            cpuBytes,
            {1u, true, 1u, 1u, sourceBytes, sourceBytes, cpuBytes}};
    }

    static auto prepare(Context& c, const Budget& b, const char*& diagnostic)
    {
        return Destination::prepareOriginal(
            c.source, c.registry, c.desc, b, c.quotas[6], c.quotas[7], c.quotas[8], diagnostic);
    }

public:
    static auto prepareDestination(Context& context, const char*& diagnostic)
    {
        return prepare(context, budget(), diagnostic);
    }

    static void verifyContract()
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        using Policy = semantics::TextureContentAccessPolicy;
        static_assert(
            !std::is_default_constructible_v<WebGpuNativeTexturePayload::OriginalUploadKey>);
        static_assert(!std::is_copy_assignable_v<WebGpuNativeTexturePayload>);
        assert(!Policy(Policy::Mode::Ordinary).ordinaryAccessDiagnostic());
        assert(std::string_view(
                   Policy(Policy::Mode::OriginalUploadExclusive).ordinaryAccessDiagnostic())
            == "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE");
        assert(std::string_view(Policy(static_cast<Policy::Mode>(99)).ordinaryAccessDiagnostic())
            == "RHI-TEXTURE-CONTENT-ACCESS-POLICY-UNKNOWN");
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto result = Policy::lowering(backend);
            assert(result.diagnostic);
            assert(result.validatorOnly == (backend == Backend::Null));
            assert(result.nativeCreationImplemented == (backend == Backend::WebGPU));
        }
        assert(!Policy::lowering(static_cast<Backend>(99)).nativeCreationImplemented);
        Budget b;
        assert(
            std::string_view(Destination::validateBudget(b)) == "RHI-TEXTURE-DESTINATION-VERSION");
        b.contractVersion = 1u;
        assert(
            std::string_view(Destination::validateBudget(b)) == "RHI-TEXTURE-DESTINATION-OPT-IN");
        b.prototypeOptIn = true;
        assert(
            std::string_view(Destination::validateBudget(b)) == "RHI-TEXTURE-DESTINATION-BUDGET");
        b = budget();
        assert(!Destination::validateBudget(b));
        b.content.maxDomains = 2u;
        assert(std::string_view(Destination::validateBudget(b))
            == "RHI-TEXTURE-DESTINATION-CONTENT-BUDGET");
        b = budget();
        assert(std::string_view(Destination::initialize({})) == "RHI-TEXTURE-DESTINATION-OWNER");
        Context c;
        std::uint64_t bytes = 0u;
        assert(!Destination::validateSelectedLayout(c.desc, bytes) && bytes == texelBytes);
        auto invalid = c.desc;
        for (const auto usage : {TextureUsage::None,
                 TextureUsage::CopyDst | TextureUsage::Storage,
                 TextureUsage::CopyDst | TextureUsage::RenderAttachment,
                 static_cast<TextureUsage>(UINT32_MAX)}) {
            invalid.usage = usage;
            assert(std::string_view(Destination::validateSelectedLayout(invalid, bytes))
                    == "RHI-TEXTURE-DESTINATION-SELECTED-PROFILE"
                && !bytes);
        }
        invalid = c.desc;
        invalid.memory = MemoryUsage::CpuToGpu;
        assert(Destination::validateSelectedLayout(invalid, bytes));
        invalid = c.desc;
        invalid.initialState = ResourceState::Undefined;
        assert(Destination::validateSelectedLayout(invalid, bytes));
        invalid = c.desc;
        invalid.sampleCount = 4u;
        assert(Destination::validateSelectedLayout(invalid, bytes));
        invalid = c.desc;
        invalid.mipLevelCount = 33u;
        assert(std::string_view(Destination::validateSelectedLayout(invalid, bytes))
            == "RHI-TEXTURE-DESTINATION-MIPS");
        invalid = c.desc;
        invalid.extent = {UINT32_MAX, UINT32_MAX, 1u};
        invalid.mipLevelCount = 1u;
        invalid.arrayLayerCount = 1u;
        assert(std::string_view(Destination::validateSelectedLayout(invalid, bytes))
                == "RHI-TEXTURE-DESTINATION-BYTE-OVERFLOW"
            && !bytes);
        invalid = c.desc;
        invalid.extent = {3u, 2u, 1u};
        invalid.format = TextureFormat::Bc7Unorm;
        invalid.mipLevelCount = 1u;
        invalid.arrayLayerCount = 2u;
        assert(!Destination::validateSelectedLayout(invalid, bytes) && bytes == 32u);
        invalid = c.desc;
        invalid.extent = {4u, 3u, 2u};
        invalid.mipLevelCount = 1u;
        invalid.arrayLayerCount = 1u;
        assert(!Destination::validateSelectedLayout(invalid, bytes) && bytes == 96u);
        invalid.arrayLayerCount = 2u;
        assert(std::string_view(Destination::validateSelectedLayout(invalid, bytes))
            == "RHI-TEXTURE-DESTINATION-DIMENSION");
        const char* diagnostic = nullptr;
        auto reject = [&](const char* id) {
            assert(!prepare(c, b, diagnostic) && std::string_view(diagnostic) == id);
            c.coldEmpty();
        };
        --b.maxCallbacks;
        reject("RHI-TEXTURE-DESTINATION-BUDGET");
        ++b.maxCallbacks;
        --b.maxRetainedSourceBytes;
        reject("RHI-TEXTURE-DESTINATION-SOURCE-BYTE-CAPACITY");
        ++b.maxRetainedSourceBytes;
        --b.maxGpuTexelBytes;
        reject("RHI-TEXTURE-DESTINATION-GPU-TEXEL-BUDGET");
        ++b.maxGpuTexelBytes;
        --b.maxCpuMetadataBytes;
        reject("RHI-TEXTURE-DESTINATION-QUOTA");
        ++b.maxCpuMetadataBytes;
        --b.maxCpuResourceBytes;
        reject("RHI-TEXTURE-DESTINATION-QUOTA");
        ++b.maxCpuResourceBytes;
        --b.content.maxCpuMetadataBytes;
        reject("RHI-TEXTURE-DESTINATION-QUOTA");
        ++b.content.maxCpuMetadataBytes;
        auto original = c.source;
        c.source = std::shared_ptr<const Source>(c.quotas[5], c.source.get());
        reject("RHI-TEXTURE-DESTINATION-SOURCE-OWNER");
        c.source = std::move(original);
        auto registry = c.registry;
        c.registry = std::shared_ptr<WebGpuRhiResourceRegistry>(c.quotas[5], registry.get());
        reject("RHI-TEXTURE-DESTINATION-REGISTRY-OWNER");
        c.registry = std::move(registry);
        ++c.desc.extent.width;
        reject("RHI-TEXTURE-DESTINATION-LAYOUT-MISMATCH");
        --c.desc.extent.width;
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        reject("RHI-TEXTURE-DESTINATION-NATIVE-UNAVAILABLE");
#endif
        c.retire();
        std::puts(
            "BINDLESS_RHI_TEXTURE_DESTINATION_CONTRACT_OK four-backend creation-only checked-texel-budget private-key gates-closed");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyActual()
    {
        verifyCold();
        verifyCpuIntentCannotInitialize();
        verifyReady();
        verifyErrors();
        verifyNativeLayoutMismatch();
        verifyRegistryReplacement();
        verifyPendingDrop();
        std::puts(
            "BINDLESS_RHI_TEXTURE_DESTINATION_NATIVE_OK scopes3 fresh exclusive ordinary-use-reject foreign identity drop allocation0");
    }

    static void prepareActualLoss()
    {
        for (std::size_t i = 0u; i < lossContexts_.size(); ++i) {
            lossContexts_[i] = core::makeUnique<Context>();
            const char* diagnostic = nullptr;
            lossObjects_[i] = prepare(*lossContexts_[i], budget(), diagnostic);
            assert(lossObjects_[i]);
        }
        assert(!Destination::initialize(lossObjects_[0]));
        waitReady(lossObjects_[0]);
        WebGpuErrorScopeHandoffFixture<0u>::holdScopes();
        assert(!Destination::initialize(lossObjects_[1]));
        WebGpuErrorScopeHandoffFixture<0u>::restoreScopes();
        WebGpuErrorScopeHandoffFixture<0u>::waitHeldScopes();
        assert(lossObjects_[1]->status() == Destination::Status::Pending);
        assert(lossObjects_[2]->status() == Destination::Status::Prepared);
    }

    static void verifyActualDeviceLoss()
    {
        assert(rhi_webgpu::webGpuNativeDeviceLost());
        for (const auto& object : lossObjects_) {
            assert(object->status() == Destination::Status::DeviceLost
                && std::string_view(object->validate()) == "RHI-TEXTURE-DESTINATION-DEVICE-LOST");
            assert(std::string_view(Destination::initialize(object))
                == "RHI-TEXTURE-DESTINATION-DEVICE-LOST");
            assert(object->contentLedger()->state(*object->source()->packing())
                == Ledger::ContentState::Invalidated);
        }
        std::weak_ptr<Destination> pending = lossObjects_[1];
        for (auto& object : lossObjects_)
            object.reset();
        assert(!pending.expired());
        WebGpuErrorScopeHandoffFixture<0u>::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return pending.expired();
        }));
        pending.reset();
        for (auto& context : lossContexts_) {
            context->retire();
            context.reset();
        }
        std::puts(
            "BINDLESS_RHI_TEXTURE_DESTINATION_LOSS_OK prepared-pending-ready terminal scopes-retire exact-budgets");
    }

private:
    static void waitReady(const std::shared_ptr<Destination>& object)
    {
        assert(WebGpuAsyncWaitFixture::until([&] {
            return object->status() != Destination::Status::Pending;
        }));
        assert(object->status() == Destination::Status::CreationReady
            && object->creationScopesObserved() && !object->validate());
    }

    static void verifyCold()
    {
        Context c;
        const char* diagnostic = nullptr;
        auto b = budget();
        const auto factory = [&] {
            return prepare(c, b, diagnostic);
        };
        std::array<std::uint64_t, 3> peaks{};
        for (std::size_t i = 0u; i < peaks.size(); ++i) {
            CpuQuotaFactoryFixture admission(c.quotas[6u + i]);
            peaks[i] = admission.measure(factory);
            admission.verify(peaks[i], factory);
            c.coldEmpty();
            {
                core::CpuMemoryQuotaLease pressure(c.quotas[6u + i], cpuBytes);
                assert(pressure.charged() && !factory());
            }
            c.coldEmpty();
        }
        auto object = factory();
        assert(object && object->status() == Destination::Status::Prepared && !object->texture());
        assert(std::string_view(object->validate()) == "RHI-TEXTURE-DESTINATION-UNINITIALIZED");
        assert(object->contentExpectation()
            && object->contentExpectation()->state() == Ledger::ContentState::Uninitialized);
        assert(object->memoryStats().allocatedBytes == texelBytes
            && object->memoryStats().liveResourceCount == 1u);
        auto forged = std::shared_ptr<Destination>(c.quotas[6], object.get());
        assert(
            std::string_view(Destination::initialize(forged)) == "RHI-TEXTURE-DESTINATION-OWNER");
        forged.reset();
        object.reset();
        c.retire();
        std::printf(
            "BINDLESS_RHI_TEXTURE_DESTINATION_COLD_OK %llu %llu %llu source1696 texel1360 quotas3 exact-less-exhausted\n",
            static_cast<unsigned long long>(peaks[0]),
            static_cast<unsigned long long>(peaks[1]),
            static_cast<unsigned long long>(peaks[2]));
    }

    static void verifyCpuIntentCannotInitialize()
    {
        Context c;
        const char* diagnostic = nullptr;
        auto object = prepare(c, budget(), diagnostic);
        assert(object);
        auto quota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto transaction =
            rhi::TextureUploadContentTransaction::prepareOriginal(object->contentLedger(),
                c.source,
                *object->contentExpectation(),
                {1u, true, cpuBytes},
                quota,
                diagnostic);
        assert(transaction && !rhi::TextureUploadContentTransaction::apply(transaction));
        assert(std::string_view(Destination::initialize(object))
                == "RHI-TEXTURE-CONTENT-DOMAIN-PENDING"
            && !object->texture());
        transaction->abandon();
        assert(std::string_view(Destination::initialize(object))
                == "RHI-TEXTURE-DESTINATION-CONTENT-JOIN"
            && !object->texture());
        transaction.reset();
        object.reset();
        assert(!quota->liveBytes());
        c.retire();
    }

    static void verifyReady()
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        Context c;
        const char* diagnostic = nullptr;
        {
            auto object = prepare(c, budget(), diagnostic);
            assert(object);
            // A second independently prepared equal-ID target must not replace it.
            auto competitor = prepare(c, budget(), diagnostic);
            assert(competitor);
            EM_ASM({
                // clang-format off
                    Module.arpTextureCreationCalls = 0;
                    Module.arpTextureQueueCalls = 0;
                    Module.arpTextureCreateOriginal = Module.preinitializedWebGPUDevice.createTexture;
                    Module.arpTextureWriteOriginal = Module.preinitializedWebGPUDevice.queue.writeTexture;
                    Module.arpTextureSubmitOriginal = Module.preinitializedWebGPUDevice.queue.submit;
                    Module.preinitializedWebGPUDevice.createTexture = function(d) {
                        Module.arpTextureCreationCalls++;
                        return Module.arpTextureCreateOriginal.call(this, d);
                    };
                    Module.preinitializedWebGPUDevice.queue.writeTexture = function() {
                        Module.arpTextureQueueCalls++;
                        return Module.arpTextureWriteOriginal.apply(this, arguments);
                    };
                    Module.preinitializedWebGPUDevice.queue.submit = function() {
                        Module.arpTextureQueueCalls++;
                        return Module.arpTextureSubmitOriginal.apply(this, arguments);
                    };
                // clang-format on
            });
            WebGpuErrorScopeHandoffFixture<0u>::holdScopes();
            assert(!Destination::initialize(object));
            WebGpuErrorScopeHandoffFixture<0u>::restoreScopes();
            WebGpuErrorScopeHandoffFixture<0u>::waitHeldScopes();
            assert(object->status() == Destination::Status::Pending
                && !object->creationScopesObserved());
            assert(std::string_view(object->validate()) == "RHI-TEXTURE-DESTINATION-PENDING");
            assert(std::string_view(Destination::initialize(competitor))
                == "RHI-TEXTURE-DESTINATION-ID-OCCUPIED");
            competitor.reset();
            assert(!prepare(c, budget(), diagnostic)
                && std::string_view(diagnostic) == "RHI-TEXTURE-DESTINATION-ID-OCCUPIED");
            assert(EM_ASM_INT({
                // clang-format off
                    return Module.arpTextureCreationCalls === 1 && Module.arpTextureQueueCalls === 0;
                // clang-format on
            }) == 1);
            WebGpuErrorScopeHandoffFixture<0u>::releaseScopes();
            waitReady(object);
            assert(!object->validateContentJoin(c.source, object->contentLedger()));
            auto foreignQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
            auto foreignLedger = Ledger::prepareOriginal(
                c.source->claim().destination, c.desc, budget().content, foreignQuota, diagnostic);
            assert(foreignLedger);
            assert(Ledger::prepareDomainOriginal(foreignLedger, c.source, diagnostic));
            assert(std::string_view(object->validateContentJoin(c.source, foreignLedger))
                == "RHI-TEXTURE-DESTINATION-FOREIGN-CONTENT");
            foreignLedger.reset();
            assert(!foreignQuota->liveBytes());
            auto forgedSource = std::shared_ptr<const Source>(c.quotas[5], c.source.get());
            assert(
                std::string_view(object->validateContentJoin(forgedSource, object->contentLedger()))
                == "RHI-TEXTURE-DESTINATION-FOREIGN-CONTENT");
            forgedSource.reset();
            const auto& payload = object->resource().texture;
            const auto extent = payload->extent;
            ++payload->extent.width;
            assert(
                std::string_view(object->validate()) == "RHI-TEXTURE-DESTINATION-IDENTITY-CHANGED");
            payload->extent = extent;
            auto token = payload->allocationReleaseToken;
            payload->allocationReleaseToken = core::makeShared<std::uint64_t>(texelBytes);
            assert(
                std::string_view(object->validate()) == "RHI-TEXTURE-DESTINATION-IDENTITY-CHANGED");
            payload->allocationReleaseToken = token;
            assert(!object->validate());
            assert(std::string_view(object->executionDiagnostic())
                == "RHI-TEXTURE-UPLOAD-ORIGINAL-GENERATION-OWNER-UNPROVEN");
            verifyOrdinaryUse(*object);
            assert(object->contentLedger()->state(*c.source->packing())
                == Ledger::ContentState::Uninitialized);
            EM_ASM({
                // clang-format off
                    Module.preinitializedWebGPUDevice.createTexture = Module.arpTextureCreateOriginal;
                    Module.preinitializedWebGPUDevice.queue.writeTexture = Module.arpTextureWriteOriginal;
                    Module.preinitializedWebGPUDevice.queue.submit = Module.arpTextureSubmitOriginal;
                    delete Module.arpTextureCreateOriginal;
                    delete Module.arpTextureWriteOriginal;
                    delete Module.arpTextureSubmitOriginal;
                // clang-format on
            });
            {
                core::CpuMemoryQuotaLease metadata(
                    c.quotas[6], cpuBytes - c.quotas[6]->liveBytes());
                core::CpuMemoryQuotaLease resource(
                    c.quotas[7], cpuBytes - c.quotas[7]->liveBytes());
                core::CpuMemoryQuotaLease content(c.quotas[8], cpuBytes - c.quotas[8]->liveBytes());
                assert(metadata.charged() && resource.charged() && content.charged());
                auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
                const auto before = core::defaultCpuAllocator().stats().allocationCount;
                assert(!object->validate()
                    && !object->validateContentJoin(c.source, object->contentLedger()));
                assert(core::defaultCpuAllocator().stats().allocationCount == before);
                std::pmr::set_default_resource(previous);
            }
            // Retaining the original object keeps every source and native owner alive.
            c.source.reset();
            auto retained = object;
            object.reset();
            assert(!retained->validate());
            retained.reset();
            token.reset();
            c.registry.reset();
            assert(!c.quotas[6]->liveBytes() && !c.quotas[8]->liveBytes());
        }
        c.retire();
    }

    static void verifyOrdinaryUse(Destination& original)
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        auto payload = original.resource().texture;
        WebGpuNativeBufferPayload dummyBuffer;
        WebGpuTransferResourceObservation dummy;
        const auto rejectId = "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE";
        assert(std::string_view(WebGpuTransferCopyValidation::validate(
                   dummy, original.resource(), BufferToTextureCopyDesc{}))
            == rejectId);
        assert(std::string_view(WebGpuTransferCopyValidation::validate(
                   original.resource(), dummy, TextureToBufferCopyDesc{}))
            == rejectId);
        assert(std::string_view(WebGpuTransferCopyValidation::validate(
                   original.resource(), dummy, TextureToTextureCopyDesc{}))
            == rejectId);
        assert(std::string_view(WebGpuTransferCopyValidation::validate(
                   dummy, original.resource(), TextureToTextureCopyDesc{}))
            == rejectId);
        auto stats = core::makeShared<WebGpuNativeCommandStats>();
        auto impl = core::makeUnique<WebGpuNativeCommandPayload::Impl>();
        impl->stats = stats;
        WebGpuNativeCommandPayload command(std::move(impl));
        core::pmr::String message;
        message.reserve(256u);
        assert(!command.recordTextureBarrier(*payload, {}, message) && message == rejectId);
        assert(!command.recordTextureMemoryBarrier(*payload, {}, message) && message == rejectId);
        UploadTextureDesc upload;
        assert(!command.recordTextureUpload(*payload, upload, message) && message == rejectId);
        assert(!command.recordBufferToTextureCopy(dummyBuffer, *payload, {}, message)
            && message == rejectId);
        assert(!command.recordTextureToBufferCopy(*payload, dummyBuffer, {}, message)
            && message == rejectId);
        WebGpuNativeTexturePayload ordinary;
        assert(!command.recordTextureToTextureCopy(*payload, ordinary, {}, message)
            && message == rejectId);
        assert(!command.recordTextureToTextureCopy(ordinary, *payload, {}, message)
            && message == rejectId);
        assert(!command.scheduleTextureReadback(payload,
                   original.texture()->desc().format,
                   original.texture()->desc().extent,
                   "reject",
                   message)
            && message == rejectId);
        core::pmr::Vector<const WebGpuNativeTexturePayload*> targets{payload.get()}, empty;
        assert(!command.recordRenderPassBegin({}, targets, empty, nullptr, message)
            && message == rejectId);
        assert(!command.recordRenderPassBegin({}, empty, targets, nullptr, message)
            && message == rejectId);
        assert(!command.recordRenderPassBegin({}, empty, empty, payload.get(), message)
            && message == rejectId);
        TextureViewDesc viewDesc;
        viewDesc.format = original.texture()->desc().format;
        viewDesc.usage = TextureUsage::Sampled;
        auto view = createWebGpuRhiTextureView(allocateGpuResourceId(),
            original.texture()->id(),
            *original.texture(),
            viewDesc,
            {true, true},
            original.registry());
        assert(view);
        original.registry()->registerTextureView(*view);
        auto arena = createWebGpuRhiDescriptorArena(
            {"exclusive-use negative", 3u, 3u}, original.registry(), {true, true});
        DescriptorSetDesc desc;
        desc.layout = {
            "exclusive sample", {{0u, DescriptorType::SampledTexture, ShaderStage::Compute}}};
        auto set = arena->allocateDescriptorSet(desc);
        assert(set);
        const auto revision = set->contentRevision();
        auto result = set->write(
            {0u, DescriptorType::SampledTexture, view->id(), DescriptorResourceKind::TextureView});
        assert(!result.success && result.diagnostics == rejectId
            && set->contentRevision() == revision);
        DescriptorTableDesc tableDesc;
        tableDesc.label = "exclusive table";
        tableDesc.stages = ShaderStage::Compute;
        tableDesc.type = DescriptorType::SampledTexture;
        tableDesc.capacity = 1u;
        auto table = arena->allocateDescriptorTable(tableDesc);
        assert(table);
        result = table->write(
            {0u, DescriptorType::SampledTexture, view->id(), DescriptorResourceKind::TextureView});
        assert(!result.success && result.diagnostics == rejectId);
        DescriptorSetDesc storageDesc;
        storageDesc.layout = {
            "exclusive storage", {{0u, DescriptorType::StorageTexture, ShaderStage::Compute}}};
        storageDesc.layout.bindings[0].storageTextureFormat = original.texture()->desc().format;
        storageDesc.layout.bindings[0].textureViewDimensionConstrained = true;
        auto storageSet = arena->allocateDescriptorSet(storageDesc);
        assert(storageSet);
        const auto storageRevision = storageSet->contentRevision();
        result = storageSet->write(
            {0u, DescriptorType::StorageTexture, view->id(), DescriptorResourceKind::TextureView});
        assert(!result.success && result.diagnostics == rejectId
            && storageSet->contentRevision() == storageRevision);
        auto group = core::makeShared<WebGpuNativeDescriptorGroupPayload>();
        WebGpuNativeDescriptorBindingPayload binding;
        binding.binding = desc.layout.bindings.front();
        binding.write = {
            0u, DescriptorType::SampledTexture, view->id(), DescriptorResourceKind::TextureView};
        binding.textureView = webGpuNativePayloadFor(*view);
        group->bindings.push_back(binding);
        assert(std::string_view(group->ordinaryTextureAccessDiagnostic()) == rejectId);
        assert(!createWebGpuNativeDescriptorGroupPayload(desc.layout, group->bindings));
        auto storageBinding = binding;
        storageBinding.binding = storageDesc.layout.bindings[0];
        storageBinding.write.type = DescriptorType::StorageTexture;
        core::pmr::Vector<WebGpuNativeDescriptorBindingPayload> storageBindings{storageBinding};
        assert(!createWebGpuNativeDescriptorGroupPayload(storageDesc.layout, storageBindings));
        assert(!command.recordDescriptorSetBind(0u, group, nullptr, 0u, message)
            && message == rejectId);
        assert(!command.recordRenderDescriptorSetBind(0u, group, nullptr, 0u, message)
            && message == rejectId);
        WebGpuNativeRenderPipelinePayload pipeline;
        assert(!recordWebGpuNativeRenderBundleDescriptorSet(
                   {}, 0u, pipeline, group, nullptr, 0u, message)
            && message == rejectId);
        ResourceBarrierDesc barrier;
        barrier.resource = original.texture()->id();
        barrier.before = ResourceState::CopyDst;
        barrier.after = ResourceState::CopySrc;
        result.success =
            transitionWebGpuRhiTexture(*const_cast<GpuTexture*>(original.texture()), barrier)
                .success;
        assert(!result.success && original.texture()->state() == ResourceState::CopyDst);
        assert(!stats->nativeCommandRecordCount && !stats->nativeTextureCopyRecordCount
            && !stats->nativeDescriptorBindRecordCount);
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpTextureQueueCalls === 0;
            // clang-format on
        }) == 1);
    }

    static void verifyErrors()
    {
        for (const bool exhausted : {false, true}) {
            Context c;
            const char* diagnostic = nullptr;
            auto object = prepare(c, budget(), diagnostic);
            assert(object);
            if (!exhausted)
                EM_ASM({
                    // clang-format off
                        Module.arpBadTextureCreate = Module.preinitializedWebGPUDevice.createTexture;
                        Module.preinitializedWebGPUDevice.createTexture = function(d) {
                            return Module.arpBadTextureCreate.call(this, Object.assign({}, d, {usage : 0}));
                        };
                    // clang-format on
                });
            if (exhausted) {
                core::CpuMemoryQuotaLease pressure(
                    c.quotas[7], cpuBytes - c.quotas[7]->liveBytes());
                assert(pressure.charged());
                assert(std::string_view(Destination::initialize(object))
                    == "RHI-TEXTURE-DESTINATION-RESOURCE-ALLOCATION");
            } else {
                assert(!Destination::initialize(object));
                EM_ASM({
                    // clang-format off
                        Module.preinitializedWebGPUDevice.createTexture = Module.arpBadTextureCreate;
                        delete Module.arpBadTextureCreate;
                    // clang-format on
                });
            }
            assert(WebGpuAsyncWaitFixture::until([&] {
                return object->creationScopesObserved();
            }));
            assert(object->status() == Destination::Status::Rejected);
            assert(std::string_view(object->validate())
                == (exhausted ? "RHI-TEXTURE-DESTINATION-RESOURCE-ALLOCATION"
                              : "RHI-TEXTURE-DESTINATION-NATIVE-ERROR"));
            assert(object->contentLedger()->state(*c.source->packing())
                == Ledger::ContentState::Invalidated);
            object.reset();
            c.retire();
        }
    }

    static void verifyNativeLayoutMismatch()
    {
        Context c;
        const char* diagnostic = nullptr;
        auto object = prepare(c, budget(), diagnostic);
        assert(object);
        // Valid native creation with a different immutable usage is not the
        // approved lowering, even when every error scope succeeds.
        EM_ASM({
            // clang-format off
                Module.arpWrongTextureCreate = Module.preinitializedWebGPUDevice.createTexture;
                Module.preinitializedWebGPUDevice.createTexture = function(d) {
                    return Module.arpWrongTextureCreate.call(
                        this,
                        Object.assign({}, d, {usage : GPUTextureUsage.COPY_DST | GPUTextureUsage.COPY_SRC}));
                };
            // clang-format on
        });
        assert(!Destination::initialize(object));
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.createTexture = Module.arpWrongTextureCreate;
                delete Module.arpWrongTextureCreate;
            // clang-format on
        });
        assert(WebGpuAsyncWaitFixture::until([&] {
            return object->creationScopesObserved();
        }));
        assert(object->status() == Destination::Status::Rejected
            && std::string_view(object->validate()) == "RHI-TEXTURE-DESTINATION-NATIVE-LAYOUT");
        object.reset();
        c.retire();
    }

    static void verifyRegistryReplacement()
    {
        using namespace rhi_webgpu;
        Context c;
        const char* diagnostic = nullptr;
        {
            auto object = prepare(c, budget(), diagnostic);
            assert(object && !Destination::initialize(object));
            waitReady(object);
            WebGpuRhiAllocator allocator({{}, texelBytes, false});
            auto allocation = allocator.allocate({{}, texelBytes, rhi::MemoryUsage::GpuOnly});
            assert(allocation.success);
            auto replacement = createWebGpuRhiTexture(
                c.source->claim().destination, c.desc, allocation, {true, true}, c.registry);
            assert(replacement);
            c.registry->registerTexture(*replacement);
            assert(std::string_view(object->validate())
                == "RHI-WEBGPU-TRANSFER-ORIGINAL-OWNER-OR-LAYOUT");
            object.reset();
            assert(c.registry->find(replacement->id())
                && c.registry->find(replacement->id())->texture
                    == webGpuNativePayloadFor(*replacement));
            replacement.reset();
            allocation = WebGpuRhiAllocation{};
            assert(!allocator.memoryStats().allocatedBytes);
        }
        c.retire();
    }

    static void verifyPendingDrop()
    {
        Context c;
        const char* diagnostic = nullptr;
        auto object = prepare(c, budget(), diagnostic);
        assert(object);
        WebGpuErrorScopeHandoffFixture<0u>::holdScopes();
        assert(!Destination::initialize(object));
        WebGpuErrorScopeHandoffFixture<0u>::restoreScopes();
        WebGpuErrorScopeHandoffFixture<0u>::waitHeldScopes();
        std::weak_ptr<Destination> weak = object;
        object.reset();
        c.source.reset();
        assert(!weak.expired());
        WebGpuErrorScopeHandoffFixture<0u>::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return weak.expired();
        }));
        weak.reset();
        c.retire();
    }

    static inline std::array<core::UniquePtr<Context>, 3> lossContexts_;
    static inline std::array<std::shared_ptr<Destination>, 3> lossObjects_;
#endif
};
} // namespace arp::test
