#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_native_ordinary_recording.h"
#include "rhi/backends/webgpu/webgpu_native_ordinary_submission.h"
#include "rhi/backends/webgpu/webgpu_native_transfer_copy_lowering.h"
#include "webgpu_async_wait_fixture.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class WebGpuOriginalTransferBatchFixture final {
public:
    static void verifyContract()
    {
        using namespace rhi_webgpu;
        using Recording = WebGpuOrdinaryRecording;
        WebGpuOriginalTransferRecordingBudget b;
        assert(std::string_view(Recording::validateBudget(b))
            == "RHI-WEBGPU-ORIGINAL-TRANSFER-VERSION");
        b.contractVersion = 1u;
        assert(std::string_view(Recording::validateBudget(b))
            == "RHI-WEBGPU-ORIGINAL-TRANSFER-OPT-IN");
        b.prototypeOptIn = true;
        assert(std::string_view(Recording::validateBudget(b))
            == "RHI-WEBGPU-ORIGINAL-TRANSFER-COMMAND-CAPACITY");
        b.maxOriginalCommands = 4u;
        assert(std::string_view(Recording::validateBudget(b))
            == "RHI-WEBGPU-ORIGINAL-TRANSFER-BUDGET");
        b.maxCommands = b.maxEncoders = b.maxDebugScopes = b.maxCommandBuffers = 1u;
        b.maxCallbacks = Recording::callbackCount;
        b.maxCpuMetadataBytes = 65536u;
        assert(!Recording::validateBudget(b));
        assert(std::string_view(Recording::recordOriginalTransfer({}))
            == "RHI-WEBGPU-ORDINARY-RECORDING-OWNER");
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        const char* diagnostic = nullptr;
        auto quota = core::makeShared<core::CpuMemoryQuota>(b.maxCpuMetadataBytes);
        assert(!Recording::prepareOriginalTransfer({}, {}, b, quota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-TRANSFER-NATIVE-UNAVAILABLE");
        assert(!quota->liveBytes());
#endif
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyRejected(
        const std::shared_ptr<const rhi_webgpu::WebGpuOriginalResourceStates>& states,
        const char* expected)
    {
        using namespace rhi_webgpu;
        const char* diagnostic = nullptr;
        auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
        const WebGpuOriginalTransferRecordingBudget budget{1u,
            true,
            4u,
            1u,
            1u,
            1u,
            1u,
            WebGpuOrdinaryRecording::callbackCount,
            quota->limitBytes()};
        installNativeObserver();
        assert(!WebGpuOrdinaryRecording::prepareOriginalTransfer(
            states, {}, budget, quota, diagnostic));
        assert(std::string_view(diagnostic) == expected && !quota->liveBytes());
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpOriginalTransferEvents.length;
            // clang-format on
        }) == 0);
        restoreNativeObserver();
        std::printf(
            "BINDLESS_RHI_ORIGINAL_TRANSFER_BATCH_REJECT_OK %s encoder0 reservation0\n", expected);
    }

    static void verify(
        const std::shared_ptr<const rhi_webgpu::WebGpuOriginalResourceStates>& states,
        bool retainLoss = true)
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        using Recording = WebGpuOrdinaryRecording;
        using Submission = WebGpuOrdinarySubmission;
        const char* diagnostic = nullptr;
        auto recordQuota = core::makeShared<core::CpuMemoryQuota>(65536u);
        auto ownerQuota = core::makeShared<core::CpuMemoryQuota>(65536u);
        auto submitQuota = core::makeShared<core::CpuMemoryQuota>(65536u);
        auto signalQuota = core::makeShared<core::CpuMemoryQuota>(65536u);
        auto owner = prepareWebGpuOrdinaryCompletionOwner(
            {1u, true, 1u, ownerQuota->limitBytes()}, ownerQuota, diagnostic);
        assert(owner);
        const WebGpuOriginalTransferRecordingBudget budget{
            1u, true, 4u, 1u, 1u, 1u, 1u, Recording::callbackCount, recordQuota->limitBytes()};
        std::shared_ptr<WebGpuNativeCompletionState> signal;
        const auto prepare = [&] {
            return Recording::prepareOriginalTransfer(
                states, owner, budget, recordQuota, diagnostic, signal);
        };
        const auto& copy =
            std::get<TransferCopyArguments>(states->inputs()->descriptors()[0].arguments);
        const auto& root = *states->inputs()->scalars()->usage()->projection()->transfer();
        installNativeObserver();
        verifyLabelRejection(states, owner, budget, recordQuota);
        verifyTextureFacts(root);
        auto shortBudget = budget;
        --shortBudget.maxOriginalCommands;
        assert(
            !Recording::prepareOriginalTransfer(states, owner, shortBudget, recordQuota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-TRANSFER-COMMAND-CAPACITY");
        auto forgedStates =
            std::shared_ptr<const WebGpuOriginalResourceStates>(recordQuota, states.get());
        assert(!Recording::prepareOriginalTransfer(
                   forgedStates, owner, budget, recordQuota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-STATE-OWNER");
        forgedStates.reset();
        CpuQuotaFactoryFixture pressure(recordQuota);
        const auto peak = pressure.measure(prepare);
        pressure.verify(peak, prepare);
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpOriginalTransferEvents.length;
            // clang-format on
        }) == 0);
        signal = WebGpuNativeCompletionState::prepareOrdinarySignal(
            {1u, true, 1u, signalQuota->limitBytes()}, signalQuota, diagnostic);
        assert(signal);
        auto recording = prepare();
        assert(recording && recording->status() == Recording::Status::Prepared);
        assert(!prepare()
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-COMPLETION-CAPACITY");
        auto forged = std::shared_ptr<Recording>(recordQuota, recording.get());
        assert(std::string_view(Recording::recordOriginalTransfer(forged))
            == "RHI-WEBGPU-ORDINARY-RECORDING-OWNER");
        forged.reset();
        assert(std::string_view(Recording::record(recording, 1u, 1u, 1u))
            == "RHI-WEBGPU-ORIGINAL-TRANSFER-MODE");
        assert(std::string_view(Recording::recordOriginalCompute(recording))
            == "RHI-WEBGPU-ORIGINAL-COMPUTE-MODE");
        assert(std::string_view(Recording::recordOriginalGraphics(recording))
            == "RHI-WEBGPU-ORIGINAL-GRAPHICS-MODE");
        seedNativeSource(root, copy);
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        const auto groupsBefore = EM_ASM_INT({
            // clang-format off
                return Module.webGpuNativeGroupCreateCount;
            // clang-format on
        });
        {
            core::CpuMemoryQuotaScope scope(recordQuota);
            core::CpuMemoryQuotaLease exhaust(
                recordQuota, recordQuota->limitBytes() - recordQuota->liveBytes());
            assert(exhaust.charged());
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            assert(!Recording::recordOriginalTransfer(recording)
                && recording->status() == Recording::Status::RecordingPending);
            assert(
                !recording->hasRecordedCommand() && Recording::recordOriginalTransfer(recording));
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
            std::pmr::set_default_resource(previous);
        }
        const auto frozenLabel = states->inputs()->scalars()->arguments()->text(0u);
        assert(EM_ASM_INT(
                   {
                       // clang-format off
                           return Module.arpOriginalTransferEvents.length === 5 &&
                                  Module.arpOriginalTransferEvents[0] === "encoder" &&
                                  Module.arpOriginalTransferEvents[1] === "pushDebugGroup" &&
                                  Module.arpOriginalTransferEvents[3] === "popDebugGroup" &&
                                  Module.arpOriginalTransferEvents[4] === "finish" &&
                                  Module.arpOriginalTransferLabel === UTF8ToString($0, $1);
                       // clang-format on
                   },
                   frozenLabel.data(),
                   frozenLabel.size())
            == 1);
        verifyNativeArguments(root, copy);
        assert(EM_ASM_INT({
            // clang-format off
                return Module.webGpuNativeGroupCreateCount;
            // clang-format on
        }) == groupsBefore);
        restoreNativeObserver();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return recording->status() != Recording::Status::RecordingPending;
        }));
        assert(recording->hasRecordedCommand() && !recording->diagnostic());
        auto submission = Submission::prepare(recording,
            {1u, true, 1u, Submission::callbackCount, submitQuota->limitBytes()},
            submitQuota,
            diagnostic);
        assert(submission);
        const auto submitAllocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!Submission::submit(submission) && !signal->isComplete());
        assert(core::defaultCpuAllocator().stats().allocationCount == submitAllocations);
        std::weak_ptr<Recording> weak = recording;
        recording.reset();
        assert(!weak.expired());
        assert(WebGpuAsyncWaitFixture::until([&] {
            return submission->status() != Submission::Status::Pending;
        }));
        assert(submission->status() == Submission::Status::Completed
            && submission->queueCompletionObserved() && signal->isComplete());
        submission.reset();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return weak.expired();
        }));
        weak.reset();
        assert(!recordQuota->liveBytes() && !submitQuota->liveBytes());
        signal.reset();
        assert(!signalQuota->liveBytes());
        verifyNativeReadback(root, copy);
        verifyNativeErrors(states, owner, budget, recordQuota, submitQuota);
        recording = prepare();
        assert(recording && !Recording::recordOriginalTransfer(recording));
        assert(WebGpuAsyncWaitFixture::until([&] {
            return recording->status() != Recording::Status::RecordingPending;
        }));
        assert(recording->hasRecordedCommand());
        auto staleSubmission = Submission::prepare(recording,
            {1u, true, 1u, Submission::callbackCount, submitQuota->limitBytes()},
            submitQuota,
            diagnostic);
        assert(staleSubmission);
        const auto& source = *root.find(copy.source());
        core::MutationJournal pending;
        assert(!(source.buffer ? source.buffer->recordState(ResourceState::CopySrc, &pending)
                               : source.texture->recordState(ResourceState::CopySrc, &pending)));
        assert(std::string_view(Submission::submit(staleSubmission))
                == "RHI-WEBGPU-ORIGINAL-STATES-PENDING"
            && staleSubmission->status() == Submission::Status::Ready);
        pending.rollback();
        assert(std::string_view(Submission::submit(staleSubmission))
                == "RHI-WEBGPU-ORIGINAL-STATES-STALE"
            && !staleSubmission->queueCompletionObserved());
        staleSubmission.reset();
        assert(!submitQuota->liveBytes());
        if (retainLoss) {
            assert(lossCount_ < loss_.size());
            loss_[lossCount_++] = {recording, owner, recordQuota, ownerQuota};
        } else {
            std::weak_ptr<Recording> last = recording;
            recording.reset();
            assert(WebGpuAsyncWaitFixture::until([&] {
                return last.expired();
            }));
            last.reset();
            assert(!recordQuota->liveBytes() && !owner->retained.pending());
            extraOwner_ = owner;
            owner.reset();
            assert(!extraOwner_.expired());
            // The existing device-loss observer retains bounded owner metadata
            // until actual device loss; its pool contains no command/resource graph.
            assert(!extraOwnerQuota_);
            extraOwnerQuota_ = ownerQuota;
        }
        std::printf(
            "BINDLESS_RHI_ORIGINAL_TRANSFER_BATCH_OK %u %llu 4 1 frozen scopes readback submission drop allocation0\n",
            static_cast<unsigned>(copy.kind()),
            static_cast<unsigned long long>(peak));
    }

    static void verifyActualDeviceLoss()
    {
        assert(lossCount_ == loss_.size() && rhi_webgpu::webGpuNativeDeviceLost());
        for (auto& p : loss_) {
            assert(
                p.recording->status() == rhi_webgpu::WebGpuOrdinaryRecording::Status::DeviceLost);
            p.recording.reset();
            p.owner.reset();
            assert(!p.recordQuota->liveBytes() && !p.ownerQuota->liveBytes());
            p = {};
        }
        assert(extraOwner_.expired());
        extraOwner_.reset();
        assert(extraOwnerQuota_ && !extraOwnerQuota_->liveBytes());
        extraOwnerQuota_.reset();
        lossCount_ = 0u;
        std::puts("BINDLESS_RHI_ORIGINAL_TRANSFER_BATCH_LOSS_OK source-reject retire5");
    }

private:
    friend class WebGpuOriginalTransferDependencyBatchFixture;
    friend class WebGpuOriginalBufferUploadSourceFixture;

    static void verifyLabelRejection(
        const std::shared_ptr<const rhi_webgpu::WebGpuOriginalResourceStates>& states,
        const std::shared_ptr<rhi_webgpu::WebGpuOrdinaryCompletionOwner>& owner,
        const rhi_webgpu::WebGpuOriginalTransferRecordingBudget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& quota)
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        using K = CommandUsageKind;
        using A = CommandScalarArguments;
        const char* diagnostic = nullptr;
        auto inputQuota = core::makeShared<core::CpuMemoryQuota>(65536u);
        auto stateQuota = core::makeShared<core::CpuMemoryQuota>(16384u);
        const auto& original = *states->inputs();
        for (const auto label : {std::string_view("a\0b", 3u), std::string_view("\xc0\x80", 2u)}) {
            const auto kind = original.scalars()->usage()->commands()[1].kind;
            const std::array<CommandVariableArguments::Input, 4> raw{
                {{*A::capture(K::BeginTransferPass, A::None{}), label},
                    {*A::capture(kind, A::None{})},
                    {*A::capture(K::EndTransferPass, A::None{})},
                    {*A::capture(K::Finish, A::None{})}}};
            auto args = CommandVariableArguments::prepareOriginal(raw.data(),
                raw.size(),
                {1u, true, 4u, 0u, 3u, 0u, inputQuota->limitBytes()},
                inputQuota,
                diagnostic);
            assert(args);
            auto scalars = WebGpuFrozenCommandScalars::prepareOriginal(original.scalars()->usage(),
                args,
                {1u, true, 4u, 0u, inputQuota->limitBytes()},
                inputQuota,
                diagnostic);
            assert(scalars);
            auto inputs = WebGpuOriginalCommandInputs::prepareOriginal(scalars,
                {},
                original.descriptors().data(),
                1u,
                {1u, true, 1u, inputQuota->limitBytes()},
                inputQuota,
                diagnostic);
            assert(inputs);
            const auto count = static_cast<std::uint32_t>(states->states().size());
            auto rejected = WebGpuOriginalResourceStates::prepareOriginal(
                inputs, {1u, true, count, count, stateQuota->limitBytes()}, stateQuota, diagnostic);
            assert(rejected);
            assert(!WebGpuOrdinaryRecording::prepareOriginalTransfer(
                       rejected, owner, budget, quota, diagnostic)
                && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-TRANSFER-LABEL-ENCODING"
                && !quota->liveBytes() && !owner->retained.pending());
        }
        assert(!inputQuota->liveBytes() && !stateQuota->liveBytes());
        std::puts("BINDLESS_RHI_ORIGINAL_TRANSFER_LABEL_REJECT_OK 2 encoder0 reservation0");
    }

    static void verifyTextureFacts(const rhi_webgpu::WebGpuOrdinaryTransferRetention& root)
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        const auto& texture =
            *root.resources()[2]
                 .texture; // Existing Transfer fixture declares the texture at position2.
        const auto range = defaultTextureSubresourceRange(root.resources()[2].textureDesc);
        assert(!WebGpuNativeTransferCopyLowering::textureDiagnostic(texture, range, {2u, 2u, 1u}));
        auto wrong = range;
        wrong.aspect = static_cast<TextureAspect>(UINT32_MAX);
        assert(std::string_view(WebGpuNativeTransferCopyLowering::textureDiagnostic(
                   texture, wrong, {2u, 2u, 1u}))
            == "RHI-WEBGPU-TRANSFER-ASPECT-UNSUPPORTED");
        const auto format = texture.rhiFormat;
        const auto dimension = texture.dimension;
        auto& mutableTexture = *root.resources()[2].texture;
        mutableTexture.rhiFormat = TextureFormat::Bc7Unorm;
        assert(std::string_view(WebGpuNativeTransferCopyLowering::textureDiagnostic(
                   texture, range, {UINT32_MAX, 1u, 1u}))
            == "RHI-WEBGPU-TRANSFER-EXTENT-OVERFLOW");
        mutableTexture.rhiFormat = static_cast<TextureFormat>(UINT32_MAX);
        assert(std::string_view(WebGpuNativeTransferCopyLowering::textureDiagnostic(
                   texture, range, {2u, 2u, 1u}))
            == "RHI-WEBGPU-TRANSFER-FORMAT-UNSUPPORTED");
        mutableTexture.rhiFormat = format;
        mutableTexture.dimension = WGPUTextureDimension_Undefined;
        assert(std::string_view(WebGpuNativeTransferCopyLowering::textureDiagnostic(
                   texture, range, {2u, 2u, 1u}))
            == "RHI-WEBGPU-TRANSFER-DIMENSION-UNSUPPORTED");
        mutableTexture.dimension = dimension;
        assert(root.matches());
    }

    static void installNativeObserver()
    {
        EM_ASM({
            // clang-format off
                Module.arpOriginalTransferEvents = [];
                Module.arpOriginalTransferCreate = Module.preinitializedWebGPUDevice.createCommandEncoder;
                Module.preinitializedWebGPUDevice.createCommandEncoder = function(desc) {
                    Module.arpOriginalTransferEvents.push("encoder");
                    var encoder = Module.arpOriginalTransferCreate.call(this, desc);
                    for (var method
                             of ["pushDebugGroup", "copyBufferToBuffer", "copyBufferToTexture",
                                 "copyTextureToBuffer", "copyTextureToTexture", "popDebugGroup", "finish"]) {
                        (function(name, fn) {
                            encoder[name] = function() {
                                Module.arpOriginalTransferEvents.push(name);
                                if (name === "pushDebugGroup")
                                    Module.arpOriginalTransferLabel = arguments[0];
                                if (name.startsWith("copy")) {
                                    Module.arpOriginalTransferCopyName = name;
                                    Module.arpOriginalTransferCopyArgs = Array.from(arguments);
                                }
                                return fn.apply(this, arguments);
                            };
                        })(method, encoder[method]);
                    }
                    return encoder;
                };
            // clang-format on
        });
    }

    static void restoreNativeObserver()
    {
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.createCommandEncoder = Module.arpOriginalTransferCreate;
                delete Module.arpOriginalTransferCreate;
            // clang-format on
        });
    }

    static void seedNativeSource(const rhi_webgpu::WebGpuOrdinaryTransferRetention& root,
        const rhi::TransferCopyArguments& copy)
    {
        const auto& source = *root.find(copy.source());
        if (source.buffer) {
            EM_ASM(
                {
                    // clang-format off
                        var b = WebGPU.getJsObject(Number($0));
                        var data = new Uint8Array(Number($1));
                        for (var i = 0; i < data.length; ++i)
                            data[i] = (i * 17 + 31) % 256;
                        Module.arpOriginalTransferSource = data;
                        Module.preinitializedWebGPUDevice.queue.writeBuffer(b, 0, data);
                    // clang-format on
                },
                source.buffer->buffer,
                source.bufferDesc.sizeBytes);
        } else {
            EM_ASM(
                {
                    // clang-format off
                        var t = WebGPU.getJsObject(Number($0));
                        var data =
                            new Uint8Array([ 11, 22, 33, 255, 44, 55, 66, 255, 77, 88, 99, 255, 111, 122, 133, 255 ]);
                        Module.arpOriginalTransferSource = data;
                        Module.preinitializedWebGPUDevice.queue.writeTexture(
                            {texture : t}, data, {bytesPerRow : 8, rowsPerImage : 2},
                            {width : 2, height : 2, depthOrArrayLayers : 1});
                    // clang-format on
                },
                source.texture->texture);
        }
    }

    static void verifyNativeArguments(const rhi_webgpu::WebGpuOrdinaryTransferRetention& root,
        const rhi::TransferCopyArguments& copy)
    {
        using namespace rhi;
        const auto& source = *root.find(copy.source());
        const auto& destination = *root.find(copy.destination());
        if (copy.kind() == CommandUsageKind::BufferCopy) {
            BufferCopyDesc d;
            copy.write(d);
            assert(EM_ASM_INT(
                       {
                           // clang-format off
                               var a = Module.arpOriginalTransferCopyArgs;
                               return Module.arpOriginalTransferCopyName === "copyBufferToBuffer" &&
                                      a[0] === WebGPU.getJsObject(Number($0)) && a[1] === Number($1) &&
                                      a[2] === WebGPU.getJsObject(Number($2)) && a[3] === Number($3) && a[4] === Number($4);
                           // clang-format on
                       },
                       source.buffer->buffer,
                       d.sourceOffsetBytes,
                       destination.buffer->buffer,
                       d.destinationOffsetBytes,
                       d.sizeBytes)
                == 1);
        } else {
            assert(EM_ASM_INT({
                // clang-format off
                    var a = Module.arpOriginalTransferCopyArgs;
                    return a[2].width === 2 && a[2].height === 2 && a[2].depthOrArrayLayers === 1;
                // clang-format on
            }) == 1);
            if (copy.kind() == CommandUsageKind::BufferToTextureCopy) {
                BufferToTextureCopyDesc d;
                copy.write(d);
                assert(EM_ASM_INT(
                           {
                               // clang-format off
                                   var a = Module.arpOriginalTransferCopyArgs;
                                   return Module.arpOriginalTransferCopyName === "copyBufferToTexture" &&
                                          a[0].buffer === WebGPU.getJsObject(Number($0)) && a[0].offset === Number($1) &&
                                          a[0].bytesPerRow === Number($2) && a[0].rowsPerImage === Number($3) &&
                                          a[1].texture === WebGPU.getJsObject(Number($4)) && a[1].mipLevel === 0 &&
                                          a[1].aspect === "all" && a[1].origin.z === 0;
                               // clang-format on
                           },
                           source.buffer->buffer,
                           d.bufferOffsetBytes,
                           d.bytesPerRow,
                           d.rowsPerImage,
                           destination.texture->texture)
                    == 1);
            } else if (copy.kind() == CommandUsageKind::TextureToBufferCopy) {
                TextureToBufferCopyDesc d;
                copy.write(d);
                assert(EM_ASM_INT(
                           {
                               // clang-format off
                                   var a = Module.arpOriginalTransferCopyArgs;
                                   return Module.arpOriginalTransferCopyName === "copyTextureToBuffer" &&
                                          a[0].texture === WebGPU.getJsObject(Number($0)) && a[0].mipLevel === 0 &&
                                          a[0].aspect === "all" && a[0].origin.z === 0 &&
                                          a[1].buffer === WebGPU.getJsObject(Number($1)) && a[1].offset === Number($2) &&
                                          a[1].bytesPerRow === Number($3) && a[1].rowsPerImage === Number($4);
                               // clang-format on
                           },
                           source.texture->texture,
                           destination.buffer->buffer,
                           d.bufferOffsetBytes,
                           d.bytesPerRow,
                           d.rowsPerImage)
                    == 1);
            } else
                assert(EM_ASM_INT(
                           {
                               // clang-format off
                                   var a = Module.arpOriginalTransferCopyArgs;
                                   return Module.arpOriginalTransferCopyName === "copyTextureToTexture" &&
                                          a[0].texture === WebGPU.getJsObject(Number($0)) &&
                                          a[1].texture === WebGPU.getJsObject(Number($1)) && a[0].mipLevel === 0 &&
                                          a[1].mipLevel === 0 && a[0].aspect === "all" && a[1].aspect === "all" &&
                                          a[0].origin.z === 0 && a[1].origin.z === 0;
                               // clang-format on
                           },
                           source.texture->texture,
                           destination.texture->texture)
                    == 1);
        }
    }

    static void verifyNativeReadback(const rhi_webgpu::WebGpuOrdinaryTransferRetention& root,
        const rhi::TransferCopyArguments& copy)
    {
        using namespace rhi;
        const auto& destination = *root.find(copy.destination());
        std::uint64_t offset = 0u, bytes = 0u, sourceOffset = 0u;
        bool bufferCopy = copy.kind() == CommandUsageKind::BufferCopy;
        if (bufferCopy) {
            BufferCopyDesc d;
            copy.write(d);
            offset = d.destinationOffsetBytes;
            bytes = d.sizeBytes;
            sourceOffset = d.sourceOffsetBytes;
        } else if (destination.buffer) {
            TextureToBufferCopyDesc d;
            copy.write(d);
            offset = d.bufferOffsetBytes;
            bytes = 264u;
        } else
            bytes = 264u;
        // Readback fixture uses real mapAsync. L2 destination GpuToCpu buffers
        // have MAP_READ without COPY_SRC; map them directly after completion.
        EM_ASM(
            {
                // clang-format off
                    var device = Module.preinitializedWebGPUDevice;
                    var original = WebGPU.getJsObject(Number($0));
                    var isBuffer = Number($1);
                    var mapDirect = isBuffer && Number($2);
                    var offset = Number($3);
                    var bytes = Number($4);
                    var readback = mapDirect ? original : device.createBuffer({
                        size : Math.ceil(bytes / 4) * 4,
                        usage : GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ
                    });
                    if (!mapDirect) {
                        var encoder = device.createCommandEncoder();
                        if (isBuffer)
                            encoder.copyBufferToBuffer(original, offset, readback, 0, bytes);
                        else
                            encoder.copyTextureToBuffer({texture : original},
                                                        {buffer : readback, bytesPerRow : 256, rowsPerImage : 2},
                                                        {width : 2, height : 2, depthOrArrayLayers : 1});
                        device.queue.submit([ encoder.finish() ]);
                    }
                    Module.arpOriginalTransferReadbackDone = false;
                    readback.mapAsync(GPUMapMode.READ).then(function() {
                        var data = new Uint8Array(readback.getMappedRange().slice(0));
                        var start = mapDirect ? offset : 0;
                        var expected = Module.arpOriginalTransferSource;
                        var good = true;
                        if (Number($5)) {
                            for (var i = 0; i < bytes; ++i)
                                good = good && data[start + i] === expected[Number($6) + i];
                        } else {
                            for (var y = 0; y < 2; ++y)
                                for (var x = 0; x < 8; ++x)
                                    good = good &&
                                           data[start + y * 256 + x] === expected[y * (Number($7) ? 256 : 8) + x];
                        }
                        Module.arpOriginalTransferReadbackPassed = good;
                        readback.unmap();
                        if (!mapDirect)
                            readback.destroy();
                        Module.arpOriginalTransferReadbackDone = true;
                    });
                // clang-format on
            },
            destination.buffer ? reinterpret_cast<std::uintptr_t>(destination.buffer->buffer)
                               : reinterpret_cast<std::uintptr_t>(destination.texture->texture),
            bool(destination.buffer),
            destination.buffer && destination.bufferDesc.memory == MemoryUsage::GpuToCpu,
            offset,
            bytes,
            bufferCopy,
            sourceOffset,
            copy.kind() == CommandUsageKind::BufferToTextureCopy);
        assert(WebGpuAsyncWaitFixture::until([] {
            return EM_ASM_INT({
                // clang-format off
                    return Module.arpOriginalTransferReadbackDone;
                // clang-format on
            }) == 1;
        }));
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpOriginalTransferReadbackPassed;
            // clang-format on
        }) == 1);
    }

    static void verifyNativeErrors(
        const std::shared_ptr<const rhi_webgpu::WebGpuOriginalResourceStates>& states,
        const std::shared_ptr<rhi_webgpu::WebGpuOrdinaryCompletionOwner>& owner,
        const rhi_webgpu::WebGpuOriginalTransferRecordingBudget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& recordQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& submitQuota)
    {
        using namespace rhi_webgpu;
        using Recording = WebGpuOrdinaryRecording;
        using Submission = WebGpuOrdinarySubmission;
        const char* diagnostic = nullptr;
        for (bool finishEncoder : {true, false}) {
            auto invalid =
                Recording::prepareOriginalTransfer(states, owner, budget, recordQuota, diagnostic);
            assert(invalid);
            EM_ASM(
                {
                    // clang-format off
                        Module.arpTransferInvalidCreate = Module.preinitializedWebGPUDevice.createCommandEncoder;
                        Module.preinitializedWebGPUDevice.createCommandEncoder = function(desc) {
                            var encoder = Module.arpTransferInvalidCreate.call(this, desc);
                            if (Number($0))
                                encoder.finish();
                            else {
                                var pop = encoder.popDebugGroup;
                                encoder.popDebugGroup = function() {
                                    pop.call(this);
                                    pop.call(this);
                                };
                            }
                            return encoder;
                        };
                    // clang-format on
                },
                finishEncoder);
            assert(!Recording::recordOriginalTransfer(invalid));
            EM_ASM({
                // clang-format off
                    Module.preinitializedWebGPUDevice.createCommandEncoder = Module.arpTransferInvalidCreate;
                    delete Module.arpTransferInvalidCreate;
                // clang-format on
            });
            assert(WebGpuAsyncWaitFixture::until([&] {
                return invalid->status() != Recording::Status::RecordingPending;
            }));
            assert(
                invalid->status() == Recording::Status::Rejected && !invalid->hasRecordedCommand());
            assert(std::string_view(invalid->diagnostic())
                == "RHI-WEBGPU-ORDINARY-VALIDATION-NATIVE-ERROR");
            assert(!Submission::prepare(invalid,
                       {1u, true, 1u, Submission::callbackCount, submitQuota->limitBytes()},
                       submitQuota,
                       diagnostic)
                && std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-VALIDATION-NATIVE-ERROR");
            invalid.reset();
            assert(!recordQuota->liveBytes() && !submitQuota->liveBytes()
                && !owner->retained.pending());
        }
        std::puts(
            "BINDLESS_RHI_ORIGINAL_TRANSFER_BATCH_NATIVE_ERRORS_OK 2 no-recorded-proof no-submit");
    }

    struct LossProbe {
        std::shared_ptr<rhi_webgpu::WebGpuOrdinaryRecording> recording;
        std::shared_ptr<rhi_webgpu::WebGpuOrdinaryCompletionOwner> owner;
        std::shared_ptr<core::CpuMemoryQuota> recordQuota, ownerQuota;
    };

    inline static std::array<LossProbe, 5> loss_{};
    inline static std::size_t lossCount_ = 0u;
    inline static std::shared_ptr<core::CpuMemoryQuota> extraOwnerQuota_;
    inline static std::weak_ptr<rhi_webgpu::WebGpuOrdinaryCompletionOwner> extraOwner_;
#endif
};
} // namespace arp::test
