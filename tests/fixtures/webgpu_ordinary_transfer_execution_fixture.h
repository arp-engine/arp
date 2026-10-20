#pragma once

#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_native_command_payload_impl.h"
#include "webgpu_frozen_transfer_arguments_fixture.h"
#include "webgpu_original_command_inputs_fixture.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
namespace arp::test {

class WebGpuOrdinaryTransferExecutionFixture final {
public:
    static void prepareOriginalDependencyIsolatedProbes()
    {
        using Native = WebGpuOriginalTransferDependencyBatchFixture;
        for (const auto mode : {Native::IsolatedMode::NativeError,
                 Native::IsolatedMode::SignalMutation,
                 Native::IsolatedMode::PendingLoss,
                 Native::IsolatedMode::SubmittedLoss}) {
            auto fixture = core::makeShared<WebGpuOrdinaryTransferExecutionFixture>();
            fixture->initialize();
            const char* diagnostic = nullptr;
            auto root = rhi_webgpu::WebGpuOrdinaryTransferRetention::shareOriginal(
                rhi_webgpu::WebGpuOrdinaryTransferRetention::prepare(*fixture->registry_,
                    fixture->ids_.data(),
                    fixture->ids_.size(),
                    fixture->budget(),
                    fixture->quota_,
                    diagnostic));
            assert(root);
            Native::verifyIsolated(root, fixture->copyArguments()[0], mode);
            fixture->dropWrappers();
            root.reset();
            assert(fixture->allocator_.memoryStats().allocatedBytes == resourceBytes);
            isolatedFixtures_[static_cast<std::size_t>(mode)] = std::move(fixture);
        }
    }

    static void verifyOriginalDependencyIsolatedRetired()
    {
        for (auto& fixture : isolatedFixtures_) {
            assert(fixture && !fixture->allocator_.memoryStats().allocatedBytes
                && !fixture->quota_->liveBytes());
            fixture.reset();
        }
    }

    WebGpuOrdinaryTransferExecutionFixture()
        : registry_(core::makeShared<rhi_webgpu::WebGpuRhiResourceRegistry>()),
          allocator_({"original transfer ledger", resourceBytes, false}),
          quota_(core::makeShared<core::CpuMemoryQuota>(65536u)),
          stats_(core::makeShared<rhi_webgpu::WebGpuNativeCommandStats>())
    {
        diagnostic_.reserve(256u);
    }

    static std::shared_ptr<rhi_webgpu::WebGpuProducerCompletionOwner> prepareCompletionOwner()
    {
        core::pmr::String diagnostic;
        diagnostic.reserve(256u);
        auto quota = core::makeShared<core::CpuMemoryQuota>(4096u);
        const rhi_webgpu::WebGpuOrdinaryTransferCompletionBudget budget{
            1u, true, 2u, quota->limitBytes()};
        {
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes());
            assert(pressure.charged()
                && !rhi_webgpu::prepareWebGpuOrdinaryTransferCompletionOwner(
                    budget, quota, diagnostic));
            assert(diagnostic == "RHI-WEBGPU-TRANSFER-PREPARATION-ALLOCATION");
        }
        assert(!quota->liveBytes());
        auto owner =
            rhi_webgpu::prepareWebGpuOrdinaryTransferCompletionOwner(budget, quota, diagnostic);
        assert(
            owner && owner->ordinaryTransfers.capacity() == 2u && owner->retained.capacity() == 0u);
        std::printf("BINDLESS_RHI_TRANSFER_COMPLETION_PREPARATION_OK %llu\n",
            static_cast<unsigned long long>(quota->liveBytes()));
        return owner;
    }

    template <class QueueWait, class MapRead>
    static bool verify(const std::shared_ptr<rhi_webgpu::WebGpuProducerCompletionOwner>& owner,
        QueueWait&& wait,
        MapRead&& mapRead)
    {
        for (unsigned mask = 0u; mask < 8u; ++mask) {
            WebGpuOrdinaryTransferExecutionFixture fixture;
            fixture.initialize();
            if (!fixture.execute(owner, mask, wait, mapRead))
                return false;
        }
        std::puts("BINDLESS_RHI_TRANSFER_EXECUTION_OK 4");
        return true;
    }

    class LossProbe final {
    public:
        std::weak_ptr<rhi_webgpu::WebGpuProducerCompletionOwner> owner;
        std::shared_ptr<core::CpuMemoryQuota> quota;
        std::shared_ptr<WebGpuOrdinaryTransferExecutionFixture> fixture;

        bool retiredAfterActualLoss()
        {
            if (fixture->originalInputsLoss_.plan)
                fixture->originalInputsLoss_.retiredAfterActualLoss();
            assert(!fixture->originalInputsResourceQuota_->liveBytes());
            const bool expired = owner.expired();
            // An expired weak observer still owns the allocate_shared control
            // block (104 bytes on this Wasm ABI). Release the test observer
            // before asserting complete CPU quota retirement.
            owner.reset();
            return expired && !quota->liveBytes()
                && !fixture->allocator_.memoryStats().allocatedBytes
                && !fixture->quota_->liveBytes() && !fixture->frozenResourceQuota_->liveBytes()
                && !fixture->frozenProjectionQuota_->liveBytes()
                && !fixture->frozenUsageQuota_->liveBytes()
                && !fixture->frozenArgumentsQuota_->liveBytes();
        }
    };

    static LossProbe prepareLossProbe(bool frozen = false)
    {
        auto fixture = core::makeShared<WebGpuOrdinaryTransferExecutionFixture>();
        fixture->initialize();
        auto owner = prepareCompletionOwner();
        auto prepared = frozen ? fixture->makeFrozenCommands(owner) : fixture->makeCommands(owner);
        assert(prepared);
        if (frozen) {
            const char* inputDiagnostic = nullptr;
            auto inputBudget = fixture->budget();
            inputBudget.retention.maxCpuMetadataBytes =
                fixture->originalInputsResourceQuota_->limitBytes();
            auto originalInputTransfer = rhi_webgpu::WebGpuOrdinaryTransferRetention::shareOriginal(
                rhi_webgpu::WebGpuOrdinaryTransferRetention::prepare(*fixture->registry_,
                    fixture->ids_.data(),
                    fixture->ids_.size(),
                    inputBudget,
                    fixture->originalInputsResourceQuota_,
                    inputDiagnostic));
            assert(originalInputTransfer && !inputDiagnostic);
            fixture->originalInputsLoss_ = WebGpuOriginalCommandInputsFixture::verifyTransfer(
                originalInputTransfer, fixture->copyArguments(), true);
            fixture->recordFrozenCopies(*prepared);
        }
        assert(prepared->finish(fixture->diagnostic_));
        const char* diagnostic = nullptr;
        auto original = rhi_webgpu::WebGpuOrdinaryTransferRetention::prepare(*fixture->registry_,
            fixture->ids_.data(),
            fixture->ids_.size(),
            fixture->budget(),
            fixture->quota_,
            diagnostic);
        const auto ticket = owner->ordinaryTransfers.reserve(original);
        auto* root = owner->ordinaryTransfers.get(ticket);
        // Inject an unproven ownership result only; this grants no native success.
        assert(ticket && root && root->storage().sealForSubmission()
            && root->storage().markSubmitted());
        assert(owner->ordinaryTransfers.submit(ticket)
            && owner->ordinaryTransfers.resolve(ticket, false));
        assert(owner->ordinaryTransfers.pending() == 2u && !owner->ordinaryTransfers.accepting());
        assert(!fixture->makeCommands(owner)
            && fixture->diagnostic_ == "RHI-WEBGPU-TRANSFER-COMPLETION-CLOSED");
        assert(!prepared->submit(fixture->diagnostic_)
            && fixture->diagnostic_ == "RHI-WEBGPU-TRANSFER-COMPLETION-CLOSED");
        assert(!fixture->stats_->nativeQueueSubmitCount);
        prepared.reset();
        fixture->dropWrappers();
        assert(owner->ordinaryTransfers.pending() == 1u
            && fixture->allocator_.memoryStats().allocatedBytes == resourceBytes);
        LossProbe probe{owner, owner->transferPreparationQuota, std::move(fixture)};
        owner.reset();
        assert(!probe.owner.expired());
        return probe;
    }

private:
    inline static std::array<std::shared_ptr<WebGpuOrdinaryTransferExecutionFixture>, 4>
        isolatedFixtures_;
    static constexpr std::uint64_t stagingBytes = 264u;
    static constexpr std::uint64_t readbackBytes = 528u;
    static constexpr std::uint64_t resourceBytes = stagingBytes * 2u + readbackBytes + 16u * 2u;

    rhi_webgpu::WebGpuOrdinaryTransferBudget budget() const
    {
        return {1u,
            true,
            {rhi::kCommandRetentionBudgetContractVersion,
                true,
                13u,
                0u,
                0u,
                0u,
                quota_->limitBytes()},
            5u,
            resourceBytes};
    }

    void initialize()
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        const auto createBuffer =
            [&](std::size_t index, std::uint64_t bytes, ResourceUsage usage, MemoryUsage memory) {
                const BufferDesc desc{
                    "ordinary manifest buffer with a long uncopied label", bytes, usage, memory};
                const auto allocation = allocator_.allocate({desc.label, bytes, memory});
                assert(allocation.success);
                buffers_[index] = createWebGpuRhiBuffer(
                    allocateGpuResourceId(), desc, allocation, {true, true}, registry_);
                assert(buffers_[index]);
                registry_->registerBuffer(*buffers_[index]);
            };
        createBuffer(0u,
            stagingBytes,
            ResourceUsage::CopySrc | ResourceUsage::CopyDst,
            MemoryUsage::CpuToGpu);
        createBuffer(1u,
            stagingBytes,
            ResourceUsage::CopySrc | ResourceUsage::CopyDst,
            MemoryUsage::GpuOnly);
        createBuffer(2u, readbackBytes, ResourceUsage::CopyDst, MemoryUsage::GpuToCpu);
        for (auto& texture : textures_) {
            const TextureDesc desc{"ordinary manifest texture with a long uncopied label",
                {2u, 2u, 1u},
                TextureFormat::Rgba8Unorm,
                TextureUsage::CopySrc | TextureUsage::CopyDst};
            const auto allocation =
                allocator_.allocate({desc.label, estimateWebGpuRhiTextureBytes(desc), desc.memory});
            assert(allocation.success);
            texture = createWebGpuRhiTexture(
                allocateGpuResourceId(), desc, allocation, {true, true}, registry_);
            assert(texture);
            registry_->registerTexture(*texture);
        }
        ids_ = {buffers_[0]->id(),
            buffers_[1]->id(),
            textures_[0]->id(),
            textures_[1]->id(),
            buffers_[2]->id()};
        expected_.fill(0xa5u);
        const std::array<std::uint8_t, 8> first{11u, 22u, 33u, 255u, 44u, 55u, 66u, 255u};
        const std::array<std::uint8_t, 8> last{77u, 88u, 99u, 255u, 111u, 122u, 133u, 255u};
        std::memcpy(expected_.data(), first.data(), first.size());
        std::memcpy(expected_.data() + 256u, last.data(), last.size());
        const auto source = webGpuNativePayloadFor(*buffers_[0]);
        assert(source
            && writeWebGpuNativeBufferPayload(
                *source, expected_.data(), expected_.size(), 0u, diagnostic_));
        assert(allocator_.memoryStats().allocatedBytes == resourceBytes);
    }

    std::shared_ptr<rhi_webgpu::WebGpuNativeCommandPayload> makeCommands(
        const std::shared_ptr<rhi_webgpu::WebGpuProducerCompletionOwner>& owner)
    {
        return rhi_webgpu::createWebGpuNativeOrdinaryTransferCommandPayload(
            *registry_, ids_.data(), ids_.size(), budget(), quota_, owner, stats_, diagnostic_);
    }

    std::array<rhi::TransferCopyArguments, 6> copyArguments() const
    {
        using namespace rhi;
        BufferToTextureCopyDesc upload;
        upload.sourceBuffer = ids_[1];
        upload.destinationTexture = ids_[2];
        upload.bytesPerRow = 256u;
        upload.rowsPerImage = 2u;
        upload.extent = {2u, 2u, 1u};
        TextureToTextureCopyDesc image;
        image.sourceTexture = ids_[2];
        image.destinationTexture = ids_[3];
        image.extent = {2u, 2u, 1u};
        TextureToBufferCopyDesc download;
        download.sourceTexture = ids_[3];
        download.destinationBuffer = ids_[4];
        download.bufferOffsetBytes = 264u;
        download.bytesPerRow = 256u;
        download.rowsPerImage = 2u;
        download.extent = {2u, 2u, 1u};
        return {
            *TransferCopyArguments::capture(BufferCopyDesc{ids_[0], ids_[1], 0u, 0u, stagingBytes}),
            *TransferCopyArguments::capture(BufferCopyDesc{ids_[1], ids_[4], 0u, 0u, stagingBytes}),
            *TransferCopyArguments::capture(upload),
            *TransferCopyArguments::capture(image),
            *TransferCopyArguments::capture(download),
            TransferCopyArguments::finish()};
    }

    std::shared_ptr<const rhi_webgpu::WebGpuFrozenTransferArguments> makeFrozenArguments()
    {
        using namespace rhi_webgpu;
        const char* id = nullptr;
        auto rootBudget = budget();
        rootBudget.retention.maxCpuMetadataBytes = frozenResourceQuota_->limitBytes();
        auto transfer =
            WebGpuOrdinaryTransferRetention::shareOriginal(WebGpuOrdinaryTransferRetention::prepare(
                *registry_, ids_.data(), ids_.size(), rootBudget, frozenResourceQuota_, id));
        assert(transfer);
        auto arguments = copyArguments();
        using Domain = WebGpuCommandReferenceDomain;
        std::array<WebGpuCommandReference, 10> references{};
        std::array<rhi::CommandUsage, 6> commands{};
        for (std::size_t i = 0u; i < 5u; ++i) {
            const auto& a = arguments[i];
            const auto* s = transfer->find(a.source());
            const auto* d = transfer->find(a.destination());
            references[2u * i] = {s->buffer ? Domain::Buffer : Domain::Texture,
                WebGpuCommandReferenceSource::Transfer,
                a.source()};
            references[2u * i + 1u] = {d->buffer ? Domain::Buffer : Domain::Texture,
                WebGpuCommandReferenceSource::Transfer,
                a.destination()};
            commands[i] = {a.kind(), static_cast<std::uint32_t>(2u * i), 2u};
        }
        commands[5] = {rhi::CommandUsageKind::Finish, 10u, 0u};
        WebGpuGenericCommandProjectionBudget projectionBudget{1u,
            true,
            1u,
            0u,
            0u,
            0u,
            10u,
            5u,
            {},
            frozenProjectionQuota_->limitBytes(),
            resourceBytes};
        projectionBudget.maxDomainReferences = {6u, 4u};
        WebGpuGenericCommandProjection::Sources sources;
        sources.transfer = transfer;
        auto projection = WebGpuGenericCommandProjection::shareOriginal(
            WebGpuGenericCommandProjection::prepare(std::move(sources),
                references.data(),
                references.size(),
                projectionBudget,
                frozenProjectionQuota_,
                id));
        assert(projection);
        auto usage = WebGpuGenericCommandUsage::prepareOriginal(projection,
            commands.data(),
            commands.size(),
            {1u, true, 6u, 10u, frozenUsageQuota_->limitBytes()},
            frozenUsageQuota_,
            id);
        assert(usage);
        using Plan = WebGpuFrozenTransferArguments;
        WebGpuFrozenTransferArgumentsBudget planBudget{
            1u, true, 6u, frozenArgumentsQuota_->limitBytes()};
        auto factory = [&] {
            return Plan::prepareOriginal(
                usage, arguments.data(), arguments.size(), planBudget, frozenArgumentsQuota_, id);
        };
        if (!frozenArgumentsPeak_) {
            CpuQuotaFactoryFixture admission(frozenArgumentsQuota_);
            frozenArgumentsPeak_ = admission.measure(factory);
            admission.verify(frozenArgumentsPeak_, factory);
        }
        assert(!frozenArgumentsQuota_->liveBytes());
        --planBudget.maxCommands;
        assert(!factory() && std::string_view(id) == "RHI-WEBGPU-TRANSFER-ARGUMENTS-CAPACITY");
        ++planBudget.maxCommands;
        const auto original = arguments[0];
        arguments[0] =
            *rhi::TransferCopyArguments::capture(rhi::BufferCopyDesc{ids_[0], ids_[1], 0u, 0u, 0u});
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        assert(!factory() && std::string_view(id) == "RHI-WEBGPU-TRANSFER-BUFFER-COPY-CONTRACT");
        assert(core::defaultCpuAllocator().stats().allocationCount == before);
        arguments[0] = original;
        arguments[0] = arguments[1];
        const auto referenceBefore = core::defaultCpuAllocator().stats().allocationCount;
        assert(!factory()
            && std::string_view(id) == "RHI-WEBGPU-COMMAND-PROJECTION-REFERENCE-UNCOVERED");
        assert(core::defaultCpuAllocator().stats().allocationCount == referenceBefore);
        arguments[0] = original;
        auto result = factory();
        assert(result && !id);
        arguments[0] = rhi::TransferCopyArguments::finish();
        assert(result->arguments()[0].kind() == rhi::CommandUsageKind::BufferCopy);
        assert(std::string_view(result->executionDiagnostic())
            == webGpuGenericRecordingUsageDiagnostic);
        WebGpuOriginalCommandInputsFixture::verifyTransfer(transfer, copyArguments());
        return result;
    }

    std::shared_ptr<rhi_webgpu::WebGpuNativeCommandPayload> makeFrozenCommands(
        const std::shared_ptr<rhi_webgpu::WebGpuProducerCompletionOwner>& owner)
    {
        auto plan = makeFrozenArguments();
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        assert(!rhi_webgpu::createWebGpuNativeFrozenTransferCommandPayload(*registry_,
                   ids_.data(),
                   ids_.size(),
                   budget(),
                   quota_,
                   owner,
                   stats_,
                   diagnostic_,
                   {})
            && diagnostic_ == "RHI-WEBGPU-TRANSFER-ARGUMENTS-ORIGINAL-PLAN");
        auto alias = std::shared_ptr<const rhi_webgpu::WebGpuFrozenTransferArguments>(
            frozenArgumentsQuota_, plan.get());
        assert(!rhi_webgpu::createWebGpuNativeFrozenTransferCommandPayload(*registry_,
                   ids_.data(),
                   ids_.size(),
                   budget(),
                   quota_,
                   owner,
                   stats_,
                   diagnostic_,
                   alias)
            && diagnostic_ == "RHI-WEBGPU-TRANSFER-ARGUMENTS-ORIGINAL-PLAN");
        assert(core::defaultCpuAllocator().stats().allocationCount == before);
        return rhi_webgpu::createWebGpuNativeFrozenTransferCommandPayload(*registry_,
            ids_.data(),
            ids_.size(),
            budget(),
            quota_,
            owner,
            stats_,
            diagnostic_,
            std::move(plan));
    }

    void verifyFrozenCold(const std::shared_ptr<rhi_webgpu::WebGpuProducerCompletionOwner>& owner)
    {
        auto plan = makeFrozenArguments();
        const auto factory = [&] {
            return rhi_webgpu::createWebGpuNativeFrozenTransferCommandPayload(*registry_,
                ids_.data(),
                ids_.size(),
                budget(),
                quota_,
                owner,
                stats_,
                diagnostic_,
                plan);
        };
        CpuQuotaFactoryFixture admission(quota_);
        const auto peak = admission.measure(factory);
        admission.verify(peak, factory);
        assert(!quota_->liveBytes() && !owner->ordinaryTransfers.pending());
        auto prepared = factory();
        assert(prepared);
        auto source = rhi_webgpu::webGpuNativePayloadFor(*buffers_[0]);
        const auto original = source->allocationReleaseToken;
        source->allocationReleaseToken =
            rhi_webgpu::webGpuNativePayloadFor(*textures_[0])->allocationReleaseToken;
        const auto encoders = stats_->nativeCommandEncoderCreateCount;
        assert(!factory() && diagnostic_ == "RHI-WEBGPU-TRANSFER-REGISTERED-IDENTITY-CHANGED");
        assert(stats_->nativeCommandEncoderCreateCount == encoders);
        assert(!prepared->recordRegisteredTransfer(
            rhi::BufferCopyDesc{ids_[0], ids_[1], 0u, 0u, stagingBytes}, diagnostic_));
        assert(diagnostic_ == "RHI-WEBGPU-TRANSFER-IDENTITY-CHANGED");
        source->allocationReleaseToken = original;
        assert(!prepared->finish(diagnostic_)
            && diagnostic_ == "RHI-WEBGPU-TRANSFER-UNSUPPORTED-COMMAND");
        prepared.reset();
        plan.reset();
        assert(!quota_->liveBytes() && !owner->ordinaryTransfers.pending());
        assert(!frozenResourceQuota_->liveBytes() && !frozenArgumentsQuota_->liveBytes());
    }

    void verifyFrozenNegative(rhi_webgpu::WebGpuNativeCommandPayload& commands)
    {
        const auto before = stats_->nativeCommandRecordCount;
        const auto finishes = stats_->nativeCommandBufferCreateCount;
        assert(!commands.finish(diagnostic_) && diagnostic_ == "RHI-WEBGPU-COMMAND-USAGE-ORDER");
        assert(stats_->nativeCommandBufferCreateCount == finishes);
        assert(!commands.recordRegisteredTransfer(
            rhi::BufferCopyDesc{ids_[0], ids_[1], 4u, 4u, 4u}, diagnostic_));
        assert(diagnostic_ == "RHI-WEBGPU-TRANSFER-ARGUMENTS-MISMATCH");
        rhi::TextureToTextureCopyDesc image;
        image.sourceTexture = ids_[2];
        image.destinationTexture = ids_[3];
        image.extent = {2u, 2u, 1u};
        assert(!commands.recordRegisteredTransfer(image, diagnostic_)
            && diagnostic_ == "RHI-WEBGPU-TRANSFER-ARGUMENTS-MISMATCH");
        assert(stats_->nativeCommandRecordCount == before && !stats_->nativeQueueSubmitCount);
    }

    void recordFrozenCopies(rhi_webgpu::WebGpuNativeCommandPayload& commands)
    {
        using namespace rhi;
        const auto arguments = copyArguments();
        for (std::size_t i = 0u; i < 5u; ++i) {
            const auto record = [&](auto& d) {
                assert(arguments[i].write(d) && commands.recordRegisteredTransfer(d, diagnostic_));
            };
            switch (arguments[i].kind()) {
            case CommandUsageKind::BufferCopy: {
                BufferCopyDesc d;
                record(d);
                break;
            }
            case CommandUsageKind::BufferToTextureCopy: {
                BufferToTextureCopyDesc d;
                record(d);
                break;
            }
            case CommandUsageKind::TextureToBufferCopy: {
                TextureToBufferCopyDesc d;
                record(d);
                break;
            }
            case CommandUsageKind::TextureToTextureCopy: {
                TextureToTextureCopyDesc d;
                record(d);
                break;
            }
            default:
                assert(false);
            }
        }
    }

    void verifyCold(const std::shared_ptr<rhi_webgpu::WebGpuProducerCompletionOwner>& owner)
    {
        CpuQuotaFactoryFixture admission(quota_);
        const auto factory = [&] {
            return makeCommands(owner);
        };
        const auto peak = admission.measure(factory);
        admission.verify(peak, factory);
        assert(!quota_->liveBytes() && owner->ordinaryTransfers.pending() == 0u);
        auto first = factory();
        auto second = factory();
        assert(first && second && !factory()
            && diagnostic_ == "RHI-WEBGPU-TRANSFER-COMPLETION-CAPACITY");
        assert(owner->ordinaryTransfers.pending() == 2u);
        first.reset();
        second.reset();
        assert(!quota_->liveBytes() && owner->ordinaryTransfers.pending() == 0u);
        {
            auto invalid = factory();
            auto source = rhi_webgpu::webGpuNativePayloadFor(*buffers_[0]);
            auto replacement = rhi_webgpu::webGpuNativePayloadFor(*textures_[0]);
            const auto original = source->allocationReleaseToken;
            source->allocationReleaseToken = replacement->allocationReleaseToken;
            assert(!invalid->recordRegisteredTransfer(
                rhi::BufferCopyDesc{ids_[0], ids_[1], 0u, 0u, 4u}, diagnostic_));
            assert(diagnostic_ == "RHI-WEBGPU-TRANSFER-IDENTITY-CHANGED");
            source->allocationReleaseToken = original;
            assert(!invalid->finish(diagnostic_)
                && diagnostic_ == "RHI-WEBGPU-TRANSFER-UNSUPPORTED-COMMAND");
        }
        assert(!quota_->liveBytes() && owner->ordinaryTransfers.pending() == 0u);
        std::printf("BINDLESS_RHI_TRANSFER_COMMAND_PREPARATION_OK %llu\n",
            static_cast<unsigned long long>(peak));
    }

    void verifyNegative(rhi_webgpu::WebGpuNativeCommandPayload& commands)
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        const auto initial = stats_->nativeCommandRecordCount;
        const auto source = webGpuNativePayloadFor(*buffers_[0]);
        const auto target = webGpuNativePayloadFor(*textures_[0]);
        assert(!commands.recordBufferCopy(
            *source, *source, {ids_[0], ids_[0], 0u, 0u, 4u}, diagnostic_));
        assert(diagnostic_ == "RHI-WEBGPU-TRANSFER-UNSUPPORTED-COMMAND");
        assert(!commands.recordRegisteredTransfer(
            BufferCopyDesc{ids_[0], ids_[1], 0u, 0u, 0u}, diagnostic_));
        assert(!commands.recordRegisteredTransfer(
            BufferCopyDesc{ids_[0], ids_[1], 1u, 0u, 4u}, diagnostic_));
        assert(!commands.recordRegisteredTransfer(
            BufferCopyDesc{ids_[0], ids_[1], 260u, 0u, 8u}, diagnostic_));
        assert(!commands.recordRegisteredTransfer(
            BufferCopyDesc{ids_[0], ids_[0], 0u, 0u, 4u}, diagnostic_));
        assert(!commands.recordRegisteredTransfer(
            BufferCopyDesc{ids_[0], {}, 0u, 0u, 4u}, diagnostic_));
        buffers_[0]->sealWrites();
        assert(!commands.recordRegisteredTransfer(
            BufferCopyDesc{ids_[1], ids_[0], 0u, 0u, 4u}, diagnostic_));
        assert(diagnostic_ == "RHI-BUFFER-GPU-WRITES-SEALED");
        assert(!commands.recordComputePassBegin({}, diagnostic_));
        assert(!commands.recordDescriptorSetBind(0u, {}, nullptr, 0u, diagnostic_));
        assert(!commands.recordDispatch(1u, 1u, 1u, diagnostic_));
        assert(!commands.recordDraw(3u, 1u, 0u, 0u, diagnostic_));
        assert(!commands.recordTextureUpload(*target, {}, diagnostic_));
        assert(!commands.scheduleTextureReadback(
            target, TextureFormat::Rgba8Unorm, {2u, 2u, 1u}, "unsupported map", diagnostic_));
        assert(!commands.scheduleBindlessFaultReadback(source, diagnostic_));
        assert(!commands.recordBufferBarrier(*source, {}, diagnostic_));
        assert(diagnostic_ == "RHI-WEBGPU-TRANSFER-UNSUPPORTED-COMMAND");
        assert(!commands.recordTextureBarrier(*target, {}, diagnostic_));
        assert(diagnostic_ == "RHI-WEBGPU-TRANSFER-UNSUPPORTED-COMMAND");
        assert(!commands.recordBufferMemoryBarrier(*source, {}, diagnostic_));
        assert(diagnostic_ == "RHI-WEBGPU-TRANSFER-UNSUPPORTED-COMMAND");
        assert(!commands.recordTextureMemoryBarrier(*target, {}, diagnostic_));
        assert(diagnostic_ == "RHI-WEBGPU-TRANSFER-UNSUPPORTED-COMMAND");
        BufferToTextureCopyDesc bad;
        bad.sourceBuffer = ids_[0];
        bad.destinationTexture = ids_[2];
        bad.extent = {2u, 2u, 1u};
        bad.bytesPerRow = 8u;
        bad.rowsPerImage = 2u;
        assert(!commands.recordRegisteredTransfer(bad, diagnostic_));
        assert(stats_->nativeCommandRecordCount == initial);
        assert(!stats_->nativeQueueSubmitCount && !stats_->nativePushConstantUploadRingWriteCount);
    }

    void dropWrappers()
    {
        for (auto& buffer : buffers_)
            buffer.reset();
        for (auto& texture : textures_)
            texture.reset();
    }

    template <class QueueWait, class MapRead>
    bool execute(const std::shared_ptr<rhi_webgpu::WebGpuProducerCompletionOwner>& owner,
        unsigned mask,
        QueueWait& wait,
        MapRead& mapRead)
    {
        const bool frozen = (mask & 4u) != 0u;
        if (frozen)
            verifyFrozenCold(owner);
        else
            verifyCold(owner);
        auto commands = frozen ? makeFrozenCommands(owner) : makeCommands(owner);
        assert(commands && commands->ordinaryTransferOnly());
        verifyNegative(*commands);
        if (frozen)
            verifyFrozenNegative(*commands);
        const std::weak_ptr<rhi_webgpu::WebGpuNativeBufferPayload> source =
            rhi_webgpu::webGpuNativePayloadFor(*buffers_[0]);
        const auto readback = rhi_webgpu::webGpuNativePayloadFor(*buffers_[2]);
        if (mask & 1u)
            dropWrappers();
        assert(allocator_.memoryStats().allocatedBytes == resourceBytes && !source.expired());
        if (!recordAndSubmit(*commands, frozen))
            return false;
        const auto queue = static_cast<WGPUQueue>(commands->nativeQueue());
        wgpuQueueAddRef(queue);
        if (mask & 2u)
            commands.reset();
        assert(owner->ordinaryTransfers.pending() == 1u
            && allocator_.memoryStats().allocatedBytes == resourceBytes);
        const bool completed = wait(queue);
        wgpuQueueRelease(queue);
        if (!completed || owner->ordinaryTransfers.pending() != 0u
            || owner->ordinaryTransfers.failed())
            return false;
        if ((mask & 1u) && (!frozen || (mask & 2u))
            && (!source.expired() || allocator_.memoryStats().allocatedBytes != readbackBytes))
            return false;
        if (frozen && !(mask & 2u))
            assert(!source.expired() && allocator_.memoryStats().allocatedBytes == resourceBytes);
        if (!mapRead(readback->buffer, readbackBytes))
            return false;
        const auto* bytes = static_cast<const std::uint8_t*>(
            wgpuBufferGetConstMappedRange(readback->buffer, 0u, readbackBytes));
        const bool matched = bytes && !std::memcmp(bytes, expected_.data(), expected_.size())
            && !std::memcmp(bytes + 264u, expected_.data(), 8u)
            && !std::memcmp(bytes + 520u, expected_.data() + 256u, 8u);
        wgpuBufferUnmap(readback->buffer);
        commands.reset();
        dropWrappers();
        assert(allocator_.memoryStats().allocatedBytes == readbackBytes && !quota_->liveBytes());
        assert(!frozenResourceQuota_->liveBytes() && !frozenProjectionQuota_->liveBytes()
            && !frozenUsageQuota_->liveBytes() && !frozenArgumentsQuota_->liveBytes());
        if (frozen)
            std::printf("BINDLESS_RHI_FROZEN_TRANSFER_NATIVE_BYTES_OK %u %u %llu\n",
                mask & 3u,
                matched ? 1u : 0u,
                static_cast<unsigned long long>(frozenArgumentsPeak_));
        else
            std::printf("BINDLESS_RHI_TRANSFER_NATIVE_BYTES_OK %u %u\n", mask, matched ? 1u : 0u);
        return matched;
    }

    bool recordAndSubmit(rhi_webgpu::WebGpuNativeCommandPayload& commands, bool frozen)
    {
        using namespace rhi;
        BufferToTextureCopyDesc upload;
        upload.sourceBuffer = ids_[1];
        upload.destinationTexture = ids_[2];
        upload.bytesPerRow = 256u;
        upload.rowsPerImage = 2u;
        upload.extent = {2u, 2u, 1u};
        TextureToTextureCopyDesc imageCopy;
        imageCopy.sourceTexture = ids_[2];
        imageCopy.destinationTexture = ids_[3];
        imageCopy.extent = {2u, 2u, 1u};
        TextureToBufferCopyDesc download;
        download.sourceTexture = ids_[3];
        download.destinationBuffer = ids_[4];
        download.bufferOffsetBytes = 264u;
        download.bytesPerRow = 256u;
        download.rowsPerImage = 2u;
        download.extent = {2u, 2u, 1u};
        core::CpuMemoryQuotaScope scope(quota_);
        core::CpuMemoryQuotaLease pressure(quota_, quota_->limitBytes() - quota_->liveBytes());
        assert(pressure.charged());
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        const auto records = stats_->nativeCommandRecordCount;
        const auto record = [&](const auto& original) {
            if (frozen) {
                auto different = original;
                using Desc = std::decay_t<decltype(original)>;
                if constexpr (std::is_same_v<Desc, BufferCopyDesc>)
                    different.sizeBytes = 4u;
                else
                    different.extent.width = 1u;
                const auto count = stats_->nativeCommandRecordCount;
                assert(!commands.recordRegisteredTransfer(different, diagnostic_)
                    && diagnostic_ == "RHI-WEBGPU-TRANSFER-ARGUMENTS-MISMATCH");
                assert(!commands.finish(diagnostic_)
                    && diagnostic_ == "RHI-WEBGPU-COMMAND-USAGE-ORDER");
                assert(stats_->nativeCommandRecordCount == count);
            }
            return commands.recordRegisteredTransfer(original, diagnostic_);
        };
        if (!record(BufferCopyDesc{ids_[0], ids_[1], 0u, 0u, stagingBytes})
            || !record(BufferCopyDesc{ids_[1], ids_[4], 0u, 0u, stagingBytes}) || !record(upload)
            || !record(imageCopy) || !record(download) || !commands.finish(diagnostic_)
            || !commands.submit(diagnostic_)) {
            std::fprintf(stderr, "RHI-WEBGPU-TRANSFER-FIXTURE: %s\n", diagnostic_.c_str());
            return false;
        }
        assert(stats_->nativeCommandRecordCount == records + 5u
            && stats_->nativeQueueSubmitCount == 1u);
        assert(
            !commands.submit(diagnostic_) && diagnostic_ == "RHI-WEBGPU-TRANSFER-SUBMISSION-STATE");
        assert(!commands.recordRegisteredTransfer(upload, diagnostic_)
            && diagnostic_ == "RHI-WEBGPU-TRANSFER-RECORDING-CLOSED");
        assert(core::defaultCpuAllocator().stats().allocationCount == before);
        std::puts(frozen ? "BINDLESS_RHI_FROZEN_TRANSFER_HOT_ALLOCATION_ZERO"
                         : "BINDLESS_RHI_TRANSFER_HOT_ALLOCATION_ZERO");
        return true;
    }

    std::shared_ptr<rhi_webgpu::WebGpuRhiResourceRegistry> registry_;
    rhi_webgpu::WebGpuRhiAllocator allocator_;
    std::shared_ptr<core::CpuMemoryQuota> quota_;
    std::shared_ptr<rhi_webgpu::WebGpuNativeCommandStats> stats_;
    std::shared_ptr<core::CpuMemoryQuota> frozenResourceQuota_ =
        core::makeShared<core::CpuMemoryQuota>(65536u);
    std::shared_ptr<core::CpuMemoryQuota> frozenProjectionQuota_ =
        core::makeShared<core::CpuMemoryQuota>(65536u);
    std::shared_ptr<core::CpuMemoryQuota> frozenUsageQuota_ =
        core::makeShared<core::CpuMemoryQuota>(65536u);
    std::shared_ptr<core::CpuMemoryQuota> frozenArgumentsQuota_ =
        core::makeShared<core::CpuMemoryQuota>(65536u);
    WebGpuOriginalCommandInputsFixture::LossProbe originalInputsLoss_{};
    std::shared_ptr<core::CpuMemoryQuota> originalInputsResourceQuota_ =
        core::makeShared<core::CpuMemoryQuota>(65536u);
    std::uint64_t frozenArgumentsPeak_ = 0u;
    std::array<core::UniquePtr<rhi::GpuBuffer>, 3> buffers_;
    std::array<core::UniquePtr<rhi::GpuTexture>, 2> textures_;
    std::array<rhi::GpuResourceId, 5> ids_;
    std::array<std::uint8_t, stagingBytes> expected_;
    core::pmr::String diagnostic_;
};

} // namespace arp::test
#endif
