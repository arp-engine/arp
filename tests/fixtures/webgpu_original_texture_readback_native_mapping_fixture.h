#pragma once
#include "core/algorithm/strided_index_range.h"
#include "webgpu_error_scope_handoff_fixture.h"
#include "webgpu_ordinary_submission_fixture.h"
#include "webgpu_original_texture_readback_mapping_fixture.h"
#include "webgpu_original_texture_readback_publication_fixture.h"

namespace arp::test {
class WebGpuOriginalTextureReadbackNativeMappingFixture final {
public:
    using Mapping = WebGpuOriginalTextureReadbackMappingFixture::Mapping;
    using Copies = WebGpuOriginalTextureReadbackMappingFixture::Copies;
    using Recording = rhi_webgpu::WebGpuOrdinaryRecording;
    using Submission = rhi_webgpu::WebGpuOrdinarySubmission;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    using MapPromises = WebGpuMapPromiseHandoffFixture<80u>;
#endif
    using Scopes = WebGpuErrorScopeHandoffFixture<80u>;
    using LossScopes = WebGpuErrorScopeHandoffFixture<81u>;
    static constexpr auto cpuBytes = WebGpuOriginalTextureReadbackMappingFixture::cpuBytes;

    static void verifyContract()
    {
        WebGpuOriginalTextureReadbackPublicationFixture::verifyContract();
        using Result = rhi_webgpu::WebGpuOriginalTextureReadbackResult;
        assert(std::string_view(Result::validatePublishedOriginal({}))
            == "RHI-WEBGPU-READBACK-RESULT-OWNER");
        assert(Result::contractVersion == 1u
            && Result::representation() == Result::Representation::OriginalPaddedBufferBytes);
        assert(Mapping::lowering(rhi::Backend::Null).validatorOnly
            && !Mapping::lowering(rhi::Backend::Null).nativeMappingImplemented);
        assert(!Mapping::lowering(rhi::Backend::Vulkan).nativeMappingImplemented
            && !Mapping::lowering(rhi::Backend::Metal).nativeMappingImplemented);
        assert(Mapping::lowering(rhi::Backend::WebGPU).nativeMappingImplemented
            && Mapping::scopeCallbackCount == 3u && Mapping::callbackCount == 1u);
        std::puts(
            "BINDLESS_RHI_READBACK_NATIVE_MAP_CONTRACT_OK four-backends independent-map1-scopes3 Completed-only original-padded immutable-snapshot");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyRows(const unsigned char* bytes,
        const std::shared_ptr<const rhi::TextureReadbackLayout>& layout,
        const std::shared_ptr<const rhi::TextureUploadPacking>& packing)
    {
        assert(bytes && layout->mipCopies().size() == packing->mipLayouts().size());
        for (std::size_t i = 0u; i < layout->mipCopies().size(); ++i) {
            const auto& m = layout->mipCopies()[i];
            const auto& u = packing->mipLayouts()[i];
            assert(m.range == u.range && m.extent == u.extent && m.rowCount == u.rowCount
                && m.sliceCount == u.sliceCount && m.rowBytes == u.rowBytes);
            const core::StridedIndexRange readSlices(m.bufferOffsetBytes,
                m.sliceCount,
                static_cast<std::uint64_t>(m.bytesPerRow) * m.rowsPerImage),
                sourceSlices(u.sourceOffset,
                    u.sliceCount,
                    static_cast<std::uint64_t>(u.sourceBytesPerRow) * u.sourceRowsPerImage);
            for (std::uint32_t z = 0u; z < m.sliceCount; ++z) {
                std::uint64_t read = 0u, source = 0u;
                assert(readSlices.index(z, read) && sourceSlices.index(z, source));
                const core::StridedIndexRange readRows(read, m.rowCount, m.bytesPerRow),
                    sourceRows(source, u.rowCount, u.sourceBytesPerRow);
                for (std::uint32_t y = 0u; y < m.rowCount; ++y) {
                    assert(readRows.index(y, read) && sourceRows.index(y, source));
                    assert(!std::memcmp(bytes + read,
                        packing->inputOwner()->pixelBytes().data() + source,
                        m.rowBytes));
                }
            }
        }
    }

    static auto prepare(const std::shared_ptr<Recording>& recording,
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 2>& quotas)
    {
        for (auto& q : quotas)
            q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        const char* diagnostic = nullptr;
        auto mapping = Mapping::prepareOriginal(recording->originalReadbackCopies(),
            WebGpuOriginalTextureReadbackMappingFixture::budget(),
            quotas[0],
            quotas[1],
            diagnostic);
        assert(mapping);
        return mapping;
    }

    static void completeOriginal(const std::shared_ptr<Recording>& recording)
    {
        assert(!Recording::recordOriginalTextureReadback(recording));
        assert(WebGpuAsyncWaitFixture::until([&] {
            return recording->hasRecordedCommand();
        }));
        auto q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        const char* diagnostic = nullptr;
        auto submission = Submission::prepare(
            recording, {1u, true, 1u, Submission::callbackCount, cpuBytes}, q, diagnostic);
        assert(submission && !Submission::submit(submission));
        assert(WebGpuAsyncWaitFixture::until([&] {
            return submission->status() != Submission::Status::Pending;
        }));
        assert(submission->status() == Submission::Status::Completed
            && !Copies::validateNativeCompletionOriginal(recording->originalReadbackCopies()));
        submission.reset();
        assert(!q->liveBytes());
    }

    static void verifyCompleted(const std::shared_ptr<Mapping>& original,
        const std::shared_ptr<const rhi::TextureUploadPacking>& packing,
        unsigned order)
    {
        verifyCompleted(original,
            packing,
            order,
            static_cast<WebGpuOriginalTextureReadbackPublicationFixture::Context*>(nullptr));
    }

    template <class Publication>
    static void verifyCompleted(const std::shared_ptr<Mapping>& original,
        const std::shared_ptr<const rhi::TextureUploadPacking>& packing,
        unsigned order,
        Publication* publication)
    {
        auto mapping = original;
        const auto& pair = mapping->copies()
                               ->transaction()
                               ->states()
                               ->inputs()
                               ->scalars()
                               ->usage()
                               ->projection()
                               ->originalReadbacks()[0];
        const auto buffer = pair->destination()->nativeResource()->buffer;
        const auto start = [&] {
            return publication ? publication->start() : Mapping::start(mapping);
        };
        WebGpuMapHandoffFixture::verifyMapStateSeparation(
            buffer, mapping->resultDeclaration()->byteCapacity(), [&] {
                assert(start() && mapping->status() == Mapping::Status::Prepared
                    && !mapping->resultDeclaration()->publishedBytes());
            });
        MapPromises::install(buffer, 0u, true);
        Scopes::holdScopes();
        const auto previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!start());
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        assert(mapping->status() == Mapping::Status::Pending
            && wgpuBufferGetMapState(buffer) == WGPUBufferMapState_Pending);
        assert(std::string_view(Mapping::start(mapping)) == "RHI-WEBGPU-READBACK-MAP-START-STATUS");
        std::pmr::set_default_resource(previous);
        MapPromises::restoreMap();
        Scopes::restoreScopes();
        MapPromises::waitHeld();
        Scopes::waitHeldScopes();
        MapPromises::verifyArguments(mapping->resultDeclaration()->byteCapacity());
        if (order == 0u) {
            MapPromises::release();
            assert(WebGpuAsyncWaitFixture::until([&] {
                return mapping->mapCompletionObserved();
            }));
            assert(mapping->status() == Mapping::Status::AwaitingScopes
                && wgpuBufferGetMapState(buffer) == WGPUBufferMapState_Unmapped);
        } else {
            Scopes::releaseScopes();
            assert(WebGpuAsyncWaitFixture::until([&] {
                return mapping->scopeCompletionsObserved();
            }));
            assert(mapping->status() == Mapping::Status::Pending);
        }
        assert(!mapping->resultDeclaration()->publishedBytes()
            && !pair->destination()->resultPublished());
        if (publication)
            publication->verifyUnavailable();
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        if (order == 0u)
            Scopes::releaseScopes();
        else
            MapPromises::release();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return mapping->status() == Mapping::Status::Published;
        }));
        assert(!mapping->validate() && !mapping->diagnostic()
            && pair->destination()->resultPublished()
            && mapping->resultDeclaration()->publishedBytes());
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations
            && Mapping::start(mapping));
        assert(!rhi_webgpu::WebGpuOriginalTextureReadbackResult::validatePublishedOriginal(
            mapping->resultDeclaration()));
        verifyRows(mapping->resultDeclaration()->publishedBytes(),
            mapping->resultDeclaration()->layout(),
            packing);
        MapPromises::restoreAll();
        if (publication) {
            publication->verifyPublished();
            verifyRows(publication->delivery.result->publishedBytes(),
                publication->delivery.result->layout(),
                packing);
        }
        std::printf(
            "BINDLESS_RHI_READBACK_NATIVE_MAP_OK %u actual-map-read-unmap scopes4 original-mips%zu padded-bytes immutable-publication partial-closed allocation0\n",
            order,
            mapping->resultDeclaration()->layout()->mipCopies().size());
    }

    static void verifyFailure(const std::shared_ptr<Recording>& recording,
        const std::shared_ptr<const rhi::TextureUploadPacking>& packing,
        unsigned mode)
    {
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 2> quotas;
        auto mapping = prepare(recording, quotas);
        WebGpuOriginalTextureReadbackPublicationFixture::Context publication(mapping);
        completeOriginal(recording);
        const auto& pair = mapping->copies()
                               ->transaction()
                               ->states()
                               ->inputs()
                               ->scalars()
                               ->usage()
                               ->projection()
                               ->originalReadbacks()[0];
        auto& buffer = *pair->destination()->nativeResource();
        const auto bytes = buffer.sizeBytes;
        MapPromises::install(buffer.buffer, mode, true);
        Scopes::holdScopes();
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        const auto previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        assert(!publication.start());
        std::pmr::set_default_resource(previous);
        MapPromises::restoreMap();
        Scopes::restoreScopes();
        MapPromises::waitHeld();
        Scopes::waitHeldScopes();
        if (mode == 2u)
            publication.cancelPending();
        if (mode == 3u) {
            auto draft = mapping->resultDeclaration();
            std::weak_ptr<Mapping> weak = mapping;
            auto numericQuota = publication.statistics->numericalQuota;
            std::weak_ptr<WebGpuOriginalTextureReadbackStatisticsFixture::Numerical> weakNumeric =
                publication.statistics->numerical;
            publication.retire();
            mapping.reset();
            assert(!weak.expired() && !weakNumeric.expired());
            MapPromises::release();
            assert(WebGpuAsyncWaitFixture::until([&] {
                auto original = weak.lock();
                return original && original->mapCompletionObserved();
            }));
            assert(!weak.expired() && !weakNumeric.expired() && !draft->publishedBytes());
            {
                const auto original = weak.lock();
                assert(original && original->diagnostic()
                    && std::string_view(original->diagnostic())
                        == "RHI-WEBGPU-READBACK-MAP-RANGE-READ");
            }
            assert(EM_ASM_INT({
                // clang-format off
                    return Module.arpMappedRangeNativeExceptions;
                // clang-format on
            }) == 1);
            std::puts(
                "BINDLESS_RHI_READBACK_EXPECTED_RANGE_ERROR_OK actual-native-exception-recorded-once stable-rhi-rejection");
            Scopes::releaseScopes();
            assert(WebGpuAsyncWaitFixture::until([&] {
                return weak.expired();
            }));
            weak.reset();
            assert(weakNumeric.expired());
            weakNumeric.reset();
            assert(!numericQuota->liveBytes());
            std::puts(
                "BINDLESS_RHI_READBACK_NUMERICAL_DROP_OK callers-dropped callback-only-owner exact-retire allocation0");
            assert(!draft->publishedBytes());
            draft.reset();
            for (const auto& q : quotas)
                assert(!q->liveBytes());
            MapPromises::restoreAll();
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
            std::puts(
                "BINDLESS_RHI_READBACK_PUBLICATION_DROP_OK sink-map-reservation-callers-dropped actual-map-scope-callbacks retain-then-retire allocation0");
            std::printf(
                "BINDLESS_RHI_READBACK_NATIVE_MAP_FAILURE_OK %u real-native-or-current-rejection unpublished terminal exact-retire allocation0\n",
                mode);
            return;
        }
        if (mode == 4u)
            ++buffer.sizeBytes;
        MapPromises::release();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return mapping->mapCompletionObserved();
        }));
        if (mode == 2u)
            publication.verifyUnavailable();
        if (mode == 4u)
            buffer.sizeBytes = bytes;
        if (mode == 5u)
            ++buffer.sizeBytes;
        Scopes::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return mapping->scopeCompletionsObserved();
        }));
        if (mode == 5u)
            buffer.sizeBytes = bytes;
        assert(mapping->status() == Mapping::Status::Rejected && mapping->diagnostic()
            && !mapping->resultDeclaration()->publishedBytes()
            && !pair->destination()->resultPublished());
        assert(Mapping::start(mapping)
            && core::defaultCpuAllocator().stats().allocationCount == allocations);
        if (mode == 2u)
            assert(
                std::string_view(mapping->diagnostic()) == "RHI-WEBGPU-READBACK-MAP-NATIVE-ERROR");
        if (mode == 3u) {
            assert(std::string_view(mapping->diagnostic()) == "RHI-WEBGPU-READBACK-MAP-RANGE-READ");
            assert(EM_ASM_INT({
                // clang-format off
                    return Module.arpMappedRangeNativeExceptions;
                // clang-format on
            }) == 1);
            std::puts(
                "BINDLESS_RHI_READBACK_EXPECTED_RANGE_ERROR_OK actual-native-exception-recorded-once stable-rhi-rejection");
        }
        if (mode == 6u)
            assert(
                std::string_view(mapping->diagnostic()) == "RHI-WEBGPU-READBACK-MAP-NATIVE-SCOPE");
        if (mode == 7u)
            assert(std::string_view(mapping->diagnostic()) == "RHI-WEBGPU-READBACK-MAP-UNMAP");
        MapPromises::restoreAll();
        if (mode == 7u)
            wgpuBufferUnmap(buffer.buffer);
        assert(wgpuBufferGetMapState(buffer.buffer) == WGPUBufferMapState_Unmapped);
        if (mode == 2u)
            publication.verifySuppressed();
        else
            publication.verifyRejected();
        publication.retire();
        mapping.reset();
        for (const auto& q : quotas)
            assert(!q->liveBytes());
        (void)packing;
        std::printf(
            "BINDLESS_RHI_READBACK_NATIVE_MAP_FAILURE_OK %u real-native-or-current-rejection unpublished terminal exact-retire allocation0\n",
            mode);
    }

    static void prepareLoss(const std::shared_ptr<Recording>& recording,
        const std::shared_ptr<const rhi::TextureUploadPacking>& packing,
        unsigned mode)
    {
        lossMappings_[mode] = prepare(recording, lossQuotas_[mode]);
        if (mode == 1u) {
            const char* diagnostic = nullptr;
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            using Sink = WebGpuOriginalTextureReadbackPublicationFixture::Sink;
            assert(!Sink::reserveOriginal(
                lossPublications_[0]->sink, lossMappings_[mode], diagnostic));
            assert(std::string_view(diagnostic) == "RHI-READBACK-PUBLICATION-REGISTRY-MISMATCH"
                && core::defaultCpuAllocator().stats().allocationCount == allocations);
            auto& first = *lossPublications_[0];
            auto unscoped =
                Sink::prepareOriginal(first.budget(), first.quotas[0], first.quotas[1], diagnostic);
            assert(unscoped);
            auto occupied = Sink::reserveOriginal(unscoped, lossMappings_[mode], diagnostic);
            assert(occupied);
            const auto before = core::defaultCpuAllocator().stats().allocationCount;
            assert(!Sink::reserveOriginal(unscoped, lossMappings_[0], diagnostic));
            assert(std::string_view(diagnostic) == "RHI-READBACK-PUBLICATION-CAPACITY"
                && before == core::defaultCpuAllocator().stats().allocationCount);
            assert(occupied->cancel());
            occupied.reset();
            unscoped.reset();
            std::puts(
                "BINDLESS_RHI_READBACK_PUBLICATION_CAPACITY_OK distinct-original-source pending-slot-full pre-native-rejection allocation0");
        }
        lossPublications_[mode] =
            core::makeUnique<WebGpuOriginalTextureReadbackPublicationFixture::Context>(
                lossMappings_[mode], false, packing);
        if (mode == 1u) {
            const auto& context = *lossPublications_[0];
            const auto& layout = lossMappings_[0]->resultDeclaration()->layout();
            const char* diagnostic = nullptr;
            auto duplicate = rhi::TextureReadbackRequestDeclaration::prepareOriginal(layout,
                lossPublications_[0]->request->identity(),
                {1u,
                    true,
                    static_cast<std::uint32_t>(layout->mipCopies().size()),
                    layout->inputOwner()->label().size(),
                    lossMappings_[0]->resultDeclaration()->byteCapacity(),
                    context.requestQuota->limitBytes()},
                context.requestQuota,
                diagnostic);
            assert(duplicate);
            const auto a = core::defaultCpuAllocator().stats().allocationCount;
            assert(!WebGpuOriginalTextureReadbackPublicationFixture::Sink::reserveTrackedOriginal(
                lossPublications_[0]->sink,
                lossMappings_[0],
                duplicate,
                context.statistics->numerical,
                diagnostic));
            assert(std::string_view(diagnostic) == "RHI-READBACK-PUBLICATION-REQUEST-DUPLICATE"
                && a == core::defaultCpuAllocator().stats().allocationCount);
            std::puts(
                "BINDLESS_RHI_READBACK_TRACKED_DUPLICATE_OK distinct-request-owner same-pending-request-id pre-native allocation0");
        }
        completeOriginal(recording);
        auto mapping = lossMappings_[mode];
        if (mode == 1u) {
            LossScopes::holdScopes();
            assert(!lossPublications_[mode]->start());
            LossScopes::restoreScopes();
            LossScopes::waitHeldScopes();
            assert(WebGpuAsyncWaitFixture::until([&] {
                return mapping->mapCompletionObserved();
            }));
            assert(mapping->status() == Mapping::Status::AwaitingScopes);
        }
        if (mode == 2u) {
            assert(!lossPublications_[mode]->start());
            assert(WebGpuAsyncWaitFixture::until([&] {
                return mapping->status() == Mapping::Status::Published;
            }));
            lossResult_ = mapping->resultDeclaration();
            verifyRows(lossResult_->publishedBytes(), lossResult_->layout(), packing);
            lossMappings_[mode].reset();
        }
    }

    static void activatePendingLoss()
    {
        auto mapping = lossMappings_[0];
        assert(mapping && !lossPublications_[0]->start());
        const auto& pair = mapping->copies()
                               ->transaction()
                               ->states()
                               ->inputs()
                               ->scalars()
                               ->usage()
                               ->projection()
                               ->originalReadbacks()[0];
        assert(mapping->status() == Mapping::Status::Pending
            && wgpuBufferGetMapState(pair->destination()->nativeResource()->buffer)
                == WGPUBufferMapState_Pending);
    }

    static void verifyLoss(unsigned mode)
    {
        if (mode == 2u) {
            assert(lossResult_->publishedBytes()
                && !rhi_webgpu::WebGpuOriginalTextureReadbackResult::validatePublishedOriginal(
                    lossResult_));
            const auto* p = lossResult_->publishedBytes();
            assert(p && lossResult_->byteCapacity() == 1536u);
            lossPublications_[mode]->verifyPublished();
            lossResult_.reset();
        } else {
            auto mapping = lossMappings_[mode];
            assert(mapping->status() == Mapping::Status::DeviceLost && Mapping::start(mapping)
                && !mapping->resultDeclaration()->publishedBytes());
            if (mode == 1u)
                LossScopes::releaseScopes();
            assert(WebGpuAsyncWaitFixture::until([&] {
                return mapping->mapCompletionObserved() && mapping->scopeCompletionsObserved();
            }));
            lossPublications_[mode]->verifyRejected(true);
            lossMappings_[mode].reset();
        }
        lossPublications_[mode]->retire();
        lossPublications_[mode].reset();
        for (const auto& q : lossQuotas_[mode])
            assert(!q->liveBytes());
        lossQuotas_[mode] = {};
        std::printf(
            "BINDLESS_RHI_READBACK_NATIVE_MAP_LOSS_OK %u actual-Pending-post-map-snapshot-loss late-callback full-drop exact-retire\n",
            mode);
    }

private:
    static inline std::array<std::shared_ptr<Mapping>, 3> lossMappings_;
    static inline std::
        array<core::UniquePtr<WebGpuOriginalTextureReadbackPublicationFixture::Context>, 3>
            lossPublications_;
    static inline std::shared_ptr<const rhi_webgpu::WebGpuOriginalTextureReadbackResult>
        lossResult_;
    static inline std::array<std::array<std::shared_ptr<core::CpuMemoryQuota>, 2>, 3> lossQuotas_;
#endif
};
} // namespace arp::test
