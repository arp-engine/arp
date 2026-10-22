#include "core/memory/cpu_allocator.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_native_commands.h"
#include "rhi/backends/metal/metal_staging_upload_pool.h"
#include "rhi/semantics/native_backend_capabilities.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace {

constexpr std::uint32_t kTextureWidth = 16u;
constexpr std::uint32_t kTextureHeight = 16u;
constexpr std::uint32_t kUploadRowBytes = 256u;
constexpr std::uint32_t kUploadBytes = kUploadRowBytes * kTextureHeight;
constexpr std::uint64_t kFrameSerial = 1u;
constexpr std::uint64_t kTextureGeneration = 1u;
constexpr std::uint64_t kAbandonAttempts = arp::rhi_metal::kMetalNativeCommandBufferCapacity + 8u;

bool allCommandCapacityAvailable()
{
    std::pmr::string diagnostics;
    const auto capacity = arp::rhi_metal::reserveSharedMetalRhiCommandCapacity(
        arp::rhi_metal::kMetalNativeCommandBufferCapacity, diagnostics);
    return capacity && capacity->charged();
}

struct RecordingFixture {
    explicit RecordingFixture(std::uint64_t stagingBytes = kUploadBytes)
        : registry(arp::core::makeShared<arp::rhi_metal::MetalRhiResourceRegistry>()),
          staging({"RecordingAbandonStaging", stagingBytes, 1u, 256u, true})
    {
        arp::rhi::TextureDesc textureDesc;
        textureDesc.label = "RecordingAbandonTexture";
        textureDesc.extent = {kTextureWidth, kTextureHeight, 1u};
        textureDesc.format = arp::rhi::TextureFormat::Rgba8Unorm;
        textureDesc.usage = arp::rhi::TextureUsage::CopyDst;
        textureDesc.memory = arp::rhi::MemoryUsage::GpuOnly;
        textureDesc.initialState = arp::rhi::ResourceState::Undefined;
        texture = arp::rhi_metal::createMetalRhiTexture(
            {1u}, textureDesc, {}, std::shared_ptr<void>{}, registry);
        if (texture)
            registry->registerTexture(*texture);

        capability = arp::rhi::semantics::nativeBackendCapabilityContract(arp::rhi::Backend::Metal);
    }

    arp::rhi::UploadTextureDesc upload(std::string_view label) const
    {
        arp::rhi::UploadTextureDesc desc;
        desc.label.assign(label.data(), label.size());
        desc.destinationTexture = texture->id();
        desc.extent = {kTextureWidth, kTextureHeight, 1u};
        desc.format = arp::rhi::TextureFormat::Rgba8Unorm;
        desc.bytesPerRow = kUploadRowBytes;
        desc.rowsPerImage = kTextureHeight;
        desc.subresources = range;
        desc.pixelData = pixels.data();
        desc.pixelDataSize = static_cast<std::uint32_t>(pixels.size());
        desc.backendOwnedStaging = true;
        desc.frameSerial = kFrameSerial;
        desc.resourceGeneration = kTextureGeneration;
        return desc;
    }

    std::shared_ptr<arp::rhi_metal::MetalRhiResourceRegistry> registry;
    arp::rhi_metal::MetalRhiStagingUploadPool staging;
    arp::rhi::semantics::NativeBackendCapabilityContract capability;
    arp::core::UniquePtr<arp::rhi::GpuTexture> texture;
    std::array<std::uint8_t, kUploadBytes> pixels{};
    arp::rhi::TextureSubresourceRange range{0u, 1u, 0u, 1u, arp::rhi::TextureAspect::Color};
};

bool prepareExhaustedPartialRecording(RecordingFixture& fixture)
{
    bool transitioned = false;
    bool began = false;
    bool firstSucceeded = false;
    bool secondFailedWithBudget = false;
    bool ended = false;
    std::pmr::string secondDiagnostic;
    {
        auto encoder = arp::rhi_metal::createMetalRhiCommandEncoder(
            {"RecordingAbandonPartial"}, fixture.capability, &fixture.staging, fixture.registry);
        if (!encoder)
            return false;
        transitioned = encoder
                           ->transition(*fixture.texture,
                               fixture.range,
                               arp::rhi::ResourceState::Undefined,
                               arp::rhi::ResourceState::CopyDst)
                           .success;
        if (!transitioned)
            return false;
        auto transfer = encoder->beginTransferPass({"RecordingAbandonPartial"});
        began = transfer != nullptr;
        if (!transfer)
            return false;
        firstSucceeded = transfer->uploadTexture(fixture.upload("PartialFirst")).success;
        const auto second = transfer->uploadTexture(fixture.upload("PartialSecond"));
        secondDiagnostic = second.diagnostics;
        secondFailedWithBudget = !second.success
            && second.diagnostics.find("ARP-RHI-STAGING-PAGE-COUNT-EXCEEDED")
                != std::pmr::string::npos;
        ended = transfer->end().success;
        // Deliberately do not call finish(). Destruction must abandon the
        // partially recorded native command and roll back recorded state.
    }

    const auto stats = fixture.staging.stats();
    const bool stateRolledBack = fixture.texture->state() == arp::rhi::ResourceState::Undefined;
    const bool capacityReturned = allCommandCapacityAvailable();
    const bool stagingRolledBack = stats.liveSliceCount == 0u;
    const bool valid = transitioned && began && firstSucceeded && secondFailedWithBudget && ended
        && stateRolledBack && capacityReturned && stagingRolledBack
        && stats.allocatedPageCount == 1u && stats.failedAllocationCount == 1u;
    std::fprintf(stderr,
        "recording-abandon partial transitioned=%d began=%d first=%d secondBudget=%d "
        "ended=%d stateRollback=%d stagingRollback=%d capacity=%d pages=%u live=%u "
        "failed=%u diagnostic=%s\n",
        transitioned,
        began,
        firstSucceeded,
        secondFailedWithBudget,
        ended,
        stateRolledBack,
        stagingRolledBack,
        capacityReturned,
        stats.allocatedPageCount,
        stats.liveSliceCount,
        stats.failedAllocationCount,
        secondDiagnostic.c_str());
    std::fflush(stderr);
    return valid;
}

bool repeatExhaustedRecordingAbandon(RecordingFixture& fixture)
{
    if (fixture.staging.stats().liveSliceCount == 0u) {
        const auto primed =
            fixture.staging.allocate({"ExhaustedRetryPrime", kUploadBytes, 256u, kFrameSerial});
        if (!primed.success)
            return false;
    }
    bool valid = true;
    std::uint64_t completedAttempts = 0u;
    for (std::uint64_t attempt = 0u; attempt < kAbandonAttempts; ++attempt) {
        if ((attempt % 8u) == 0u) {
            std::fprintf(stderr,
                "recording-abandon attempt-begin=%llu\n",
                static_cast<unsigned long long>(attempt));
            std::fflush(stderr);
        }
        bool iterationValid = false;
        {
            auto encoder =
                arp::rhi_metal::createMetalRhiCommandEncoder({"RecordingAbandonExhausted"},
                    fixture.capability,
                    &fixture.staging,
                    fixture.registry);
            if (encoder) {
                const auto transitioned = encoder->transition(*fixture.texture,
                    fixture.range,
                    arp::rhi::ResourceState::Undefined,
                    arp::rhi::ResourceState::CopyDst);
                auto transfer = transitioned.success
                    ? encoder->beginTransferPass({"RecordingAbandonExhausted"})
                    : nullptr;
                if (transfer) {
                    const auto upload = transfer->uploadTexture(fixture.upload("ExhaustedRetry"));
                    const auto ended = transfer->end();
                    iterationValid = !upload.success && ended.success
                        && upload.diagnostics.find("ARP-RHI-STAGING-PAGE-COUNT-EXCEEDED")
                            != std::pmr::string::npos;
                }
                // Deliberately abandon without finish(), as the WOCC upload
                // helper does after a staging allocation failure.
            }
        }
        const bool stateRolledBack = fixture.texture->state() == arp::rhi::ResourceState::Undefined;
        const bool capacityReturned = allCommandCapacityAvailable();
        valid = valid && iterationValid && stateRolledBack && capacityReturned;
        if (!valid)
            break;
        ++completedAttempts;
        if ((completedAttempts % 8u) == 0u) {
            std::fprintf(stderr,
                "recording-abandon attempt-end=%llu\n",
                static_cast<unsigned long long>(completedAttempts));
            std::fflush(stderr);
        }
    }

    const auto stats = fixture.staging.stats();
    std::fprintf(stderr,
        "recording-abandon exhausted attempts=%llu/%llu valid=%d pages=%u live=%u "
        "failed=%u capacity=%d\n",
        static_cast<unsigned long long>(completedAttempts),
        static_cast<unsigned long long>(kAbandonAttempts),
        valid,
        stats.allocatedPageCount,
        stats.liveSliceCount,
        stats.failedAllocationCount,
        allCommandCapacityAvailable());
    std::fflush(stderr);
    return valid && completedAttempts == kAbandonAttempts;
}

bool repeatOpenPassAbandon(RecordingFixture& fixture)
{
    std::uint64_t completedAttempts = 0u;
    for (std::uint64_t attempt = 0u; attempt < kAbandonAttempts; ++attempt) {
        {
            auto encoder =
                arp::rhi_metal::createMetalRhiCommandEncoder({"RecordingAbandonOpenPass"},
                    fixture.capability,
                    &fixture.staging,
                    fixture.registry);
            if (!encoder)
                return false;
            auto transfer = encoder->beginTransferPass({"RecordingAbandonOpenPass"});
            if (!transfer)
                return false;
            // Deliberately omit end(). Encoder abandonment must close and
            // release the native blit encoder before returning capacity.
        }
        if (!allCommandCapacityAvailable())
            return false;
        ++completedAttempts;
    }
    std::fprintf(stderr,
        "recording-abandon open-pass attempts=%llu/%llu capacity=%d\n",
        static_cast<unsigned long long>(completedAttempts),
        static_cast<unsigned long long>(kAbandonAttempts),
        allCommandCapacityAvailable());
    std::fflush(stderr);
    return completedAttempts == kAbandonAttempts;
}

bool verifyRecoveryAfterRetirement(RecordingFixture& fixture)
{
    // Unsubmitted journals cancel exact slices; a frame serial cannot retire them.
    const auto retiredStats = fixture.staging.stats();
    if (retiredStats.liveSliceCount != 0u)
        return false;

    arp::rhi_metal::MetalRhiDeviceDesc desc;
    desc.label = "RecordingAbandonRecovery";
    desc.capabilityContract =
        arp::rhi::semantics::nativeBackendCapabilityContract(arp::rhi::Backend::Metal);
    desc.capabilityContract.queriedFromNativeDevice = true;
    desc.capabilityContract.queriedExtendedFeatures = true;
    desc.uploadStagingPageSizeBytes = kUploadBytes;
    desc.uploadStagingMaxPageCount = 1u;
    desc.memoryBudgetBytes = 1024u * 1024u;
    auto device = arp::rhi_metal::createMetalRhiDevice(desc);
    if (!device)
        return false;
    auto texture = device->createTexture({
        "RecordingAbandonRecoveryTexture",
        {kTextureWidth, kTextureHeight, 1u},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::CopyDst | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });
    if (!texture)
        return false;
    auto encoder = device->createCommandEncoder({"RecordingAbandonRecovery"});
    if (!encoder
        || !encoder
            ->transition(*texture,
                fixture.range,
                arp::rhi::ResourceState::Undefined,
                arp::rhi::ResourceState::CopyDst)
            .success) {
        return false;
    }
    auto transfer = encoder->beginTransferPass({"RecordingAbandonRecovery"});
    if (!transfer)
        return false;
    auto upload = fixture.upload("RecoveryUpload");
    upload.destinationTexture = texture->id();
    upload.frameSerial = 2u;
    upload.resourceGeneration = 2u;
    const auto uploaded = transfer->uploadTexture(upload);
    const auto ended = transfer->end();
    if (!uploaded.success || !ended.success
        || !encoder
            ->transition(*texture,
                fixture.range,
                arp::rhi::ResourceState::CopyDst,
                arp::rhi::ResourceState::ShaderRead)
            .success) {
        return false;
    }
    auto commands = encoder->finish();
    if (!commands)
        return false;
    const auto submitted = device->queue().submit(*commands);
    if (!submitted.success)
        return false;
    device->waitIdle();
    const bool capacityReturned = allCommandCapacityAvailable();
    std::fprintf(stderr,
        "recording-abandon recovery uploaded=%d submitted=%d capacity=%d retired=%u\n",
        uploaded.success,
        submitted.success,
        capacityReturned,
        retiredStats.retiredSliceCount);
    std::fflush(stderr);
    return capacityReturned;
}

bool verifyAcceptedCompletionRetiresStaging()
{
    arp::rhi_metal::MetalRhiDeviceDesc desc;
    desc.label = "AcceptedCompletionRetiresStaging";
    desc.capabilityContract =
        arp::rhi::semantics::nativeBackendCapabilityContract(arp::rhi::Backend::Metal);
    desc.capabilityContract.queriedFromNativeDevice = true;
    desc.capabilityContract.queriedExtendedFeatures = true;
    desc.uploadStagingPageSizeBytes = kUploadBytes;
    desc.uploadStagingMaxPageCount = 1u;
    desc.memoryBudgetBytes = 1024u * 1024u;
    auto device = arp::rhi_metal::createMetalRhiDevice(desc);
    if (!device)
        return false;
    auto texture = device->createTexture({
        "AcceptedCompletionTexture",
        {kTextureWidth, kTextureHeight, 1u},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::CopyDst | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Undefined,
    });
    if (!texture)
        return false;

    std::array<std::uint8_t, kUploadBytes> pixels{};
    const arp::rhi::TextureSubresourceRange range{0u, 1u, 0u, 1u, arp::rhi::TextureAspect::Color};
    const auto submitUpload =
        [&](arp::rhi::ResourceState before, std::uint64_t generation, arp::rhi::GpuFence* fence) {
            auto encoder = device->createCommandEncoder({"AcceptedCompletionUpload"});
            if (!encoder
                || !encoder->transition(*texture, range, before, arp::rhi::ResourceState::CopyDst)
                    .success) {
                return false;
            }
            auto transfer = encoder->beginTransferPass({"AcceptedCompletionUpload"});
            if (!transfer)
                return false;
            arp::rhi::UploadTextureDesc upload;
            upload.label = "AcceptedCompletionUpload";
            upload.destinationTexture = texture->id();
            upload.extent = {kTextureWidth, kTextureHeight, 1u};
            upload.format = arp::rhi::TextureFormat::Rgba8Unorm;
            upload.bytesPerRow = kUploadRowBytes;
            upload.rowsPerImage = kTextureHeight;
            upload.subresources = range;
            upload.pixelData = pixels.data();
            upload.pixelDataSize = static_cast<std::uint32_t>(pixels.size());
            upload.backendOwnedStaging = true;
            upload.resourceGeneration = generation;
            const auto uploaded = transfer->uploadTexture(upload);
            const auto ended = transfer->end();
            if (!uploaded.success || !ended.success
                || !encoder
                    ->transition(*texture,
                        range,
                        arp::rhi::ResourceState::CopyDst,
                        arp::rhi::ResourceState::ShaderRead)
                    .success) {
                return false;
            }
            auto commands = encoder->finish();
            return commands && device->queue().submit(*commands, fence).success;
        };

    auto firstFence = device->createFence();
    if (!firstFence || firstFence->isSignaled())
        return false;
    bool hostSignalRejected = false;
    try {
        firstFence->signal();
    } catch (const std::logic_error& error) {
        hostSignalRejected =
            std::string_view(error.what()) == "RHI-METAL-FENCE-HOST-SIGNAL-UNAUTHENTICATED";
    }
    if (!hostSignalRejected || firstFence->isSignaled())
        return false;
    const auto readinessBefore = device->uploadStagingReadiness();
    if (!firstFence || !submitUpload(arp::rhi::ResourceState::Undefined, 1u, firstFence.get())) {
        return false;
    }
    firstFence->wait();
    const auto readinessAfterFirst = device->uploadStagingReadiness();
    const bool completedFirst = firstFence->isSignaled();
    const bool rebindingRejected =
        !submitUpload(arp::rhi::ResourceState::ShaderRead, 2u, firstFence.get());
    firstFence->reset();
    const bool resetUnbound = !firstFence->isSignaled();
    const bool second = submitUpload(arp::rhi::ResourceState::ShaderRead, 2u, firstFence.get());
    device->waitIdle();
    std::fprintf(stderr,
        "recording-abandon completion-retirement firstCompleted=%d second=%d\n",
        firstFence->isSignaled(),
        second);
    std::fflush(stderr);
    return completedFirst && rebindingRejected && resetUnbound && firstFence->isSignaled() && second
        && readinessAfterFirst.available() && readinessAfterFirst.advancedSince(readinessBefore);
}

bool verifyReverseSubmissionRetiresExactStaging()
{
    using namespace arp;
    using namespace rhi_metal;
    RecordingFixture fixture(2u * kUploadBytes);
    if (!fixture.texture)
        return false;
    core::pmr::String diagnostic;
    auto record = [&](const char* label) {
        auto command = createMetalRhiNativeCommandPayload(label, fixture.registry, &diagnostic);
        if (!command)
            return command;
        const auto slice = fixture.staging.allocate({label, kUploadBytes, 256u, 1u});
        if (!slice.success || !slice.mappedData)
            return std::shared_ptr<MetalRhiNativeCommandPayload>{};
        std::memcpy(slice.mappedData, fixture.pixels.data(), kUploadBytes);
        auto journal = core::makeUnique<rhi::RecordedStagingUploadJournal>();
        if (!fixture.staging.enlist(
                *journal, {slice.pageIndex, slice.offsetBytes, slice.sizeBytes, slice.frameSerial}))
            return std::shared_ptr<MetalRhiNativeCommandPayload>{};
        if (!prepareMetalRhiNativeStagingBufferForGpu(
                slice.payload, slice.offsetBytes, kUploadBytes, diagnostic)
            || !command->recordBlitPassBegin(diagnostic)
            || !command->recordUploadStagingTextureCopy(
                slice.payload, slice.offsetBytes, fixture.upload(label), diagnostic)
            || !command->recordBlitPassEnd(diagnostic) || !command->finish(diagnostic))
            return std::shared_ptr<MetalRhiNativeCommandPayload>{};
        command->adoptStagingUploads(std::move(journal));
        command->markStagingUpload(fixture.staging.completedSubmissionEpoch());
        return command;
    };
    auto first = record("RecordedFirst");
    auto second = record("SubmittedFirst");
    if (!first || !second)
        return false;
    MetalRhiNativeQueueStats queue;
    auto completed = second->preparedCompletionState();
    if (!second->submit(queue, completed, {}, 0u, {}, 0u, diagnostic))
        return false;
    completed->wait();
    const auto afterSecond = fixture.staging.stats();
    const auto tail = fixture.staging.allocate({"ExactTail", kUploadBytes, 256u, 1u});
    const bool exact = afterSecond.liveSliceCount == 1u && afterSecond.retiredSliceCount == 1u
        && tail.success && tail.offsetBytes == kUploadBytes;
    if (tail.success)
        fixture.staging.cancel(tail);
    completed = first->preparedCompletionState();
    if (!first->submit(queue, completed, {}, 0u, {}, 0u, diagnostic))
        return false;
    completed->wait();
    const auto afterFirst = fixture.staging.stats();
    std::fprintf(stderr,
        "metal-staging reverse-submission retained=%u retired=%u finalLive=%u exact=%d\n",
        afterSecond.liveSliceCount,
        afterSecond.retiredSliceCount,
        afterFirst.liveSliceCount,
        exact);
    return exact && afterFirst.liveSliceCount == 0u && afterFirst.retiredSliceCount == 2u;
}

} // namespace

int main()
{
    @autoreleasepool {
        RecordingFixture fixture;
        if (!fixture.texture || !allCommandCapacityAvailable())
            return 1;
        const bool partial = prepareExhaustedPartialRecording(fixture);
        const bool repeated = repeatExhaustedRecordingAbandon(fixture);
        const bool openPass = repeated && repeatOpenPassAbandon(fixture);
        const bool recovered = openPass && verifyRecoveryAfterRetirement(fixture);
        const bool completionRetired = recovered && verifyAcceptedCompletionRetiresStaging();
        const bool reverseRetired =
            completionRetired && verifyReverseSubmissionRetiresExactStaging();
        return partial && repeated && openPass && recovered && completionRetired && reverseRetired
            ? 0
            : 1;
    }
}
