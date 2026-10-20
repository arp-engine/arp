#pragma once
#include "rhi/backends/webgpu/webgpu_original_texture_readback_graph_preparation.h"
#include "webgpu_original_device_loss_registration_fixture.h"
#include "webgpu_original_texture_readback_device_staging_budget_fixture.h"
#include "webgpu_original_texture_readback_native_mapping_fixture.h"
#include "webgpu_original_texture_readback_recording_fixture.h"

namespace arp::test {
class WebGpuTextureReadbackSnapshotFixture final {
public:
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    using Intent = rhi::TextureReadbackContentIntent;
    using Source = rhi_webgpu::WebGpuOriginalTextureReadbackSource;
    using Pair = rhi_webgpu::WebGpuOriginalTextureReadbackPairProjection;
    using Destination = rhi_webgpu::WebGpuOriginalTextureReadbackDestination;
    using Mapping = rhi_webgpu::WebGpuOriginalTextureReadbackMapping;
    using Sink = rhi_webgpu::WebGpuOriginalTextureReadbackPublication;
    using Recording = rhi_webgpu::WebGpuOrdinaryRecording;
    using Submission = rhi_webgpu::WebGpuOrdinarySubmission;

    class Probe final {
    public:
        WebGpuOriginalTextureReadbackDeviceStagingBudgetFixture::Context caller{512u, true};
        std::shared_ptr<Sink> sink;
        std::shared_ptr<const rhi::TextureReadbackStatisticsPlan> statisticsPlan;
        std::optional<Sink::Reservation> reservation;
        std::shared_ptr<Destination> destination;
        std::shared_ptr<const Pair> pair;
        std::shared_ptr<const rhi_webgpu::WebGpuOriginalResourceStates> states;
        std::shared_ptr<rhi_webgpu::WebGpuOrdinaryCompletionOwner> completion;
        std::shared_ptr<Recording> recording;
        bool completionPrepared = false;
        std::shared_ptr<Submission> submission;
        std::shared_ptr<Mapping> mapping;
        std::optional<WebGpuOriginalTextureReadbackStatisticsFixture::Context> statistics;
        Sink::Delivery delivery;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 5> oracleQuotas;
        std::shared_ptr<const rhi::TextureUploadPacking> packing;

        Probe()
        {
            const char* diagnostic = nullptr;
            const auto& r = caller.recipe;
            const auto& q = caller.quotas;
            statisticsPlan = rhi::TextureReadbackStatisticsPlan::prepareOriginal(
                caller.source->layout(), r.statistics, q[1], diagnostic);
            assert(statisticsPlan);
            sink = Sink::prepareForDevicePlan(
                r.originalPlan, caller.caller.registry, r.publication, diagnostic);
            assert(sink);
            reservation = Sink::reserveRequestOriginal(sink,
                caller.source->publicSourceLease()->originalRequest(),
                statisticsPlan,
                diagnostic);
            assert(reservation);
            assert(!caller.source->hasCompletedUpload()
                && !caller.source->retainedCompletedGraphCount()
                && !caller.source->retainedContentBytes());
            assert(caller.source->contentMode() == Intent::Mode::QueueOrderedSnapshot
                && !caller.source->validateReadbackContentProfile());
            for (auto& quota : oracleQuotas)
                quota = core::makeShared<core::CpuMemoryQuota>(65536u);
        }

        void writeOracle(std::uint8_t first, bool issue = true)
        {
            std::array<std::uint8_t, 16> pixels;
            for (std::size_t i = 0u; i < pixels.size(); ++i)
                pixels[i] = first + static_cast<std::uint8_t>(i);
            rhi::UploadTextureDesc d;
            d.destinationTexture = caller.caller.id;
            d.format = caller.caller.desc.format;
            d.extent = caller.caller.desc.extent;
            d.subresources = {0u, 1u, 0u, 1u, rhi::TextureAspect::Color};
            d.bytesPerRow = 8u;
            d.rowsPerImage = 2u;
            d.pixelData = pixels.data();
            d.pixelDataSize = pixels.size();
            d.resourceGeneration = 43u;
            d.frameSerial = 37u;
            const char* diagnostic = nullptr;
            packing = TextureUploadPackingFixture::prepareContentPacking(
                d, caller.caller.desc, oracleQuotas, diagnostic);
            assert(packing);
            // Real native test producer only. This grants no production upload
            // completion/provenance and does not alter the original state proof.
            if (!issue)
                return;
            EM_ASM(
                {
                    // clang-format off
                        var texture = WebGPU.getJsObject($0);
                        Module.preinitializedWebGPUDevice.queue.writeTexture(
                            {texture : texture, mipLevel : 0, origin : {x : 0, y : 0, z : 0}, aspect : 'all'},
                            HEAPU8.slice($1, $1 + $2), {offset : 0, bytesPerRow : 8, rowsPerImage : 2},
                            {width : 2, height : 2, depthOrArrayLayers : 1});
                    // clang-format on
                },
                caller.source->resource().texture->texture,
                pixels.data(),
                pixels.size());
        }

        void prepare(const std::shared_ptr<rhi_webgpu::WebGpuOrdinaryCompletionOwner>& owner,
            bool execute = true,
            bool graphBounds = false,
            bool originalLoss = false)
        {
            const char* diagnostic = nullptr;
            const auto& r = caller.recipe;
            const auto& q = caller.quotas;
            auto intent = TextureReadbackContentIntentFixture::descriptor();
            const auto& lease = caller.source->publicSourceLease();
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            for (const auto mode : {Intent::Mode::Unspecified,
                     Intent::Mode::CompletedOriginalUpload,
                     static_cast<Intent::Mode>(99u)}) {
                intent.mode = mode;
                assert(!Source::prepareLeasedSnapshotOriginal(caller.source->layout(),
                    caller.caller.registry,
                    lease,
                    intent,
                    r.source,
                    q[1],
                    diagnostic));
            }
            assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
            auto ordinary = Source::prepareLeasedOriginal(
                caller.source->layout(), caller.caller.registry, lease, r.source, q[1], diagnostic);
            assert(ordinary);
            assert(std::string_view(ordinary->validateReadbackContentProfile())
                == "RHI-READBACK-PAIR-SOURCE-PROFILE");
            ordinary.reset();
            assert(!Destination::prepareCompletedOriginal(caller.source,
                       {1u,
                           true,
                           1u,
                           r.originalPlan->budget().maxContentBytesPerRequest,
                           r.destination},
                       q[1],
                       q[2],
                       q[3],
                       diagnostic)
                && std::string_view(diagnostic) == "RHI-READBACK-DESTINATION-SOURCE-PROFILE");
            writeOracle(17u);
            destination = caller.prepare(diagnostic);
            assert(destination && caller.owner->usedBytes() == 512u);
            assert(!reservation->bindDestinationOriginalCold(destination));
            const auto creationAllocations = core::defaultCpuAllocator().stats().allocationCount;
            // Preserve Request high-water headroom for the subsequent graph
            // factory measurement. Dedicated creation probes exhaust both domains.
            {
                core::CpuMemoryQuotaLease request(
                    q[1], graphBounds ? 0u : q[1]->limitBytes() - q[1]->liveBytes()),
                    resource(q[2], q[2]->limitBytes() - q[2]->liveBytes());
                assert(request.charged() && resource.charged());
                assert(!reservation->initializeBoundDestinationOriginal());
            }
            assert(creationAllocations == core::defaultCpuAllocator().stats().allocationCount);
            assert(WebGpuAsyncWaitFixture::until([&] {
                return destination->creationScopesObserved();
            }));
            assert(!destination->validate());
            // The complete production cold composition consumes sealed plan
            // counts/text/quotas, with explicit caller-authored state/label intent.
            rhi::ResourceBarrierDesc barrier;
            barrier.resource = caller.source->resource().id;
            barrier.textureSubresources = rhi::defaultTextureSubresourceRange(caller.caller.desc);
            barrier.before = caller.source->stateObservation().value;
            barrier.after = rhi::ResourceState::CopySrc;
            barrier.sourceStages = barrier.destinationStages = rhi::PipelineStage::Transfer;
            barrier.sourceAccess = barrier.destinationAccess = rhi::ResourceAccess::TransferRead;
            assert(barrier.before == rhi::ResourceState::CopySrc);
            using Graph = rhi_webgpu::WebGpuOriginalTextureReadbackGraphPreparation;
            const auto graph = [&] {
                return Graph::prepareOriginal(
                    r.originalPlan, destination, barrier, "原始读回", diagnostic);
            };
            if (graphBounds) {
                {
                    WebGpuOriginalTextureReadbackDeviceStagingBudgetFixture::Context externalInput;
                    const auto& er = externalInput.recipe;
                    auto snapshot =
                        Source::prepareLeasedSnapshotOriginal(externalInput.source->layout(),
                            externalInput.caller.registry,
                            externalInput.source->publicSourceLease(),
                            TextureReadbackContentIntentFixture::descriptor(),
                            er.source,
                            externalInput.quotas[1],
                            diagnostic);
                    assert(snapshot);
                    auto externalDestination = Destination::prepareDeviceOriginal(
                        snapshot, er.destination, externalInput.owner, diagnostic);
                    assert(externalDestination);
                    assert(!Destination::initialize(externalDestination));
                    assert(WebGpuAsyncWaitFixture::until([&] {
                        return externalDestination->creationScopesObserved();
                    }));
                    auto externalBarrier = barrier;
                    externalBarrier.resource = externalInput.caller.id;
                    assert(!Graph::prepareOriginal(er.originalPlan,
                               externalDestination,
                               externalBarrier,
                               "原始读回",
                               diagnostic)
                        && std::string_view(diagnostic) == "RHI-READBACK-GRAPH-REQUEST-QUOTA");
                    externalDestination.reset();
                    snapshot.reset();
                    externalInput.retire();
                }
                auto forgedPlan = std::shared_ptr<const rhi::TextureReadbackColdBudgetPlan>(
                    q[1], r.originalPlan.get());
                assert(!Graph::prepareOriginal(
                    forgedPlan, destination, barrier, "原始读回", diagnostic));
                forgedPlan.reset();
                auto foreign = TextureReadbackColdBudgetFixture::prepare(r.originalPlan->budget());
                assert(!Graph::prepareOriginal(
                           foreign.originalPlan, destination, barrier, "原始读回", diagnostic)
                    && std::string_view(diagnostic) == "RHI-READBACK-GRAPH-PLAN-MISMATCH");
                foreign = {};
                auto forgedDestination =
                    std::shared_ptr<const Destination>(q[1], destination.get());
                assert(!Graph::prepareOriginal(
                    r.originalPlan, forgedDestination, barrier, "原始读回", diagnostic));
                forgedDestination.reset();
                assert(!Graph::prepareOriginal(
                           r.originalPlan, destination, barrier, "1234567890123", diagnostic)
                    && std::string_view(diagnostic) == "RHI-READBACK-GRAPH-PASS-LABEL-CAPACITY");
                const char invalid[]{char(0xff)};
                assert(!Graph::prepareOriginal(
                           r.originalPlan, destination, barrier, {invalid, 1u}, diagnostic)
                    && std::string_view(diagnostic) == "RHI-READBACK-GRAPH-PASS-LABEL-ENCODING");
                const auto original = barrier;
                barrier.after = rhi::ResourceState::CopyDst;
                assert(!graph());
                barrier = original;
                barrier.resource = destination->resourceId();
                assert(!graph());
                barrier = original;
                barrier.before = rhi::ResourceState::CopyDst;
                assert(!graph());
                barrier = original;
                CpuQuotaFactoryFixture pressure(q[1]);
                const auto peak = pressure.measure(graph);
                pressure.verify(peak, graph);
                {
                    core::CpuMemoryQuotaLease full(q[1], q[1]->limitBytes() - q[1]->liveBytes());
                    assert(full.charged() && !graph());
                }
                assert(destination->status() == Destination::Status::CreationReady
                    && !destination->resultPublished());
                std::printf(
                    "BINDLESS_RHI_READBACK_GRAPH_COLD_NATIVE_OK %llu exact-less-exhausted Request-domain forged-foreign explicit-label-barrier plan-recipe-only\n",
                    static_cast<unsigned long long>(peak));
            }
            states = graph();
            if (!states)
                std::printf("READBACK_COMPLETE_GRAPH_DIAGNOSTIC %s\n", diagnostic);
            assert(states);
            pair = states->inputs()->scalars()->usage()->projection()->originalReadbacks()[0];
            assert(pair->retainedNativeAllocationBytes() == 528u);
            assert(core::SharedOwnerProjection::sameOwner(states->originalMetadataQuota(), q[1]));
            auto foreignRecording = Recording::prepareOriginalTextureReadback(
                states, owner, r.recording, q[1], q[1], q[1], diagnostic);
            assert(foreignRecording);
            assert(std::string_view(reservation->bindRecordingOriginal(foreignRecording))
                == "RHI-READBACK-PUBLICATION-EXECUTION-DEVICE-OWNER");
            foreignRecording.reset();
            completion = originalLoss
                ? rhi_webgpu::prepareWebGpuOriginalReadbackCompletionOwner(
                      r.originalPlan, r.completion, r.deviceLoss, q[0], diagnostic)
                : rhi_webgpu::prepareWebGpuOrdinaryCompletionOwner(r.completion, q[0], diagnostic);
            assert(completion);
            completionPrepared = true;
            recording = Recording::prepareOriginalTextureReadback(
                states, completion, r.recording, q[1], q[1], q[1], diagnostic);
            assert(recording);
            auto forgedRecording = std::shared_ptr<Recording>(q[0], recording.get());
            assert(reservation->bindRecordingOriginal(forgedRecording));
            forgedRecording.reset();
            {
                core::CpuMemoryQuotaLease full(q[1], q[1]->limitBytes() - q[1]->liveBytes());
                assert(full.charged());
                assert(std::string_view(reservation->bindRecordingOriginal(recording))
                    == "RHI-READBACK-PUBLICATION-EXECUTION-ALLOCATION");
            }
            mapping = Mapping::prepareOriginal(
                recording->originalReadbackCopies(), r.mapping, q[1], q[1], diagnostic);
            assert(mapping);
            assert(mapping->resultDeclaration()->contentMode() == Intent::Mode::QueueOrderedSnapshot
                && !mapping->resultDeclaration()->publishedBytes());
            writeOracle(113u, false);
            statistics.emplace(mapping->resultDeclaration(), packing, false, &r, statisticsPlan);
            statistics->attachNumerical(mapping, false);
            const auto bindingBaseline = q[1]->liveBytes();
            auto forgedMap = std::shared_ptr<Mapping>(q[0], mapping.get());
            assert(std::string_view(reservation->bindRecordingAndMappingOriginalCold(
                       recording, forgedMap, statistics->numerical))
                == "RHI-READBACK-PUBLICATION-MAP-OWNER");
            forgedMap.reset();
            auto forgedNumerical =
                std::shared_ptr<rhi_webgpu::WebGpuOriginalTextureReadbackNumericalResult>(
                    q[0], statistics->numerical.get());
            assert(reservation->bindRecordingAndMappingOriginalCold(
                recording, mapping, forgedNumerical));
            forgedNumerical.reset();
            assert(reservation->bindRecordingAndMappingOriginalCold(
                recording, {}, statistics->numerical));
            assert(q[1]->liveBytes() == bindingBaseline && sink->pending() == 1u
                && caller.owner->usedBytes() == 512u);
            assert(mapping->status() == Mapping::Status::Prepared
                && recording->status() == Recording::Status::Prepared
                && !recording->readbackRecordingCallbacksIssued());
            const auto rollbackHot = core::defaultCpuAllocator().stats().allocationCount;
            assert(std::string_view(reservation->recordOriginal())
                == "RHI-READBACK-PUBLICATION-EXECUTION-NOT-BOUND");
            assert(rollbackHot == core::defaultCpuAllocator().stats().allocationCount);
            {
                core::CpuMemoryQuotaLease full(q[1], q[1]->limitBytes() - q[1]->liveBytes());
                assert(full.charged());
                assert(std::string_view(reservation->bindRecordingAndMappingOriginalCold(
                           recording, mapping, statistics->numerical))
                    == "RHI-READBACK-PUBLICATION-EXECUTION-ALLOCATION");
            }
            assert(!reservation->bindRecordingAndMappingOriginalCold(
                recording, mapping, statistics->numerical));
            assert(reservation->bindRecordingAndMappingOriginalCold(
                recording, mapping, statistics->numerical));
            std::puts(
                "BINDLESS_RHI_READBACK_ATOMIC_EXECUTION_BINDING_NATIVE_OK same-ticket recording-map-numerical forged-null quota-exhausted rollback-retry pending-charges-preserved native-unissued");
            if (!execute)
                return;
            record();
            // A later queue write, after lease creation AND command recording,
            // is the snapshot oracle. Native copy is not submitted yet.
            writeOracle(113u);
            prepareSubmission();
            submit();
            assert(WebGpuAsyncWaitFixture::until([&] {
                return submission->callbacksDrained();
            }));
            assert(submission->status() == Submission::Status::Completed);
        }

        void record()
        {
            const auto a = core::defaultCpuAllocator().stats().allocationCount;
            assert(!reservation->recordOriginal());
            assert(core::defaultCpuAllocator().stats().allocationCount == a);
            assert(WebGpuAsyncWaitFixture::until([&] {
                return recording->readbackRecordingCallbacksDrained();
            }));
            assert(recording->hasRecordedCommand());
        }

        void prepareSubmission()
        {
            const char* diagnostic = nullptr;
            const auto& r = caller.recipe;
            const auto& q = caller.quotas;
            auto otherQuota = core::makeShared<core::CpuMemoryQuota>(q[1]->limitBytes());
            auto foreign = Submission::prepare(recording, r.submission, otherQuota, diagnostic);
            assert(foreign);
            assert(std::string_view(reservation->bindSubmissionOriginal(foreign))
                == "RHI-READBACK-PUBLICATION-EXECUTION-REQUEST-QUOTA");
            foreign.reset();
            assert(!otherQuota->liveBytes());
            submission = Submission::prepare(recording, r.submission, q[1], diagnostic);
            assert(submission);
            auto forged = std::shared_ptr<Submission>(q[0], submission.get());
            assert(reservation->bindSubmissionOriginal(forged));
            forged.reset();
            {
                core::CpuMemoryQuotaLease full(q[1], q[1]->limitBytes() - q[1]->liveBytes());
                assert(full.charged());
                assert(std::string_view(reservation->bindSubmissionOriginal(submission))
                    == "RHI-READBACK-PUBLICATION-EXECUTION-ALLOCATION");
            }
            assert(!reservation->bindSubmissionOriginal(submission)
                && reservation->bindSubmissionOriginal(submission));
        }

        void submit()
        {
            const auto a = core::defaultCpuAllocator().stats().allocationCount;
            assert(!reservation->submitOriginal());
            assert(a == core::defaultCpuAllocator().stats().allocationCount);
        }

        const char* start() { return reservation->start(); }

        void verifyUnavailable()
        {
            const char* diagnostic = nullptr;
            assert(!Sink::pollOriginal(sink, delivery, diagnostic) && !diagnostic);
            statistics->verifyUnavailable();
        }

        void verifyPublished()
        {
            const char* diagnostic = nullptr;
            assert(Sink::pollOriginal(sink, delivery, diagnostic) && !diagnostic
                && delivery.outcome == Sink::Outcome::Published);
            assert(delivery.result->contentMode() == Intent::Mode::QueueOrderedSnapshot
                && !sink->pending());
            assert(core::SharedOwnerProjection::sameOwner(
                delivery.originalRequest, caller.source->publicSourceLease()->originalRequest()));
            statistics->verifyPublished(delivery.result);
        }

        void dropExecutionCallers()
        {
            reservation.reset();
            sink.reset();
            statistics.reset();
            mapping.reset();
            submission.reset();
            recording.reset();
            completion.reset();
            states.reset();
            pair.reset();
            destination.reset();
            statisticsPlan.reset();
            packing.reset();
            caller.source.reset();
            caller.owner.reset();
            caller.recipe = {};
            caller.caller.layout.reset();
            caller.caller.registry.reset();
        }

        void retire()
        {
            const bool detached = completion && completion->originalReadbackPlan()
                && !WebGpuOriginalDeviceLossRegistrationFixture::armed(completion);
            const bool deviceRetained =
                completionPrepared && !detached && !rhi_webgpu::webGpuNativeDeviceLost();
            const bool quarantined = deviceRetained && completion && completion->retained.failed();
            delivery = {};
            reservation.reset();
            statistics.reset();
            mapping.reset();
            submission.reset();
            recording.reset();
            states.reset();
            pair.reset();
            destination.reset();
            statisticsPlan.reset();
            sink.reset();
            completion.reset();
            packing.reset();
            for (const auto& q : oracleQuotas)
                assert(!q->liveBytes());
            if (!deviceRetained) {
                assert(!caller.owner || !caller.owner->usedBytes());
                caller.retire();
                return;
            }
            // The existing real native loss promise retains its persistent owner.
            // Failed submitted graphs stay quarantined until actual device loss.
            assert(deferredCount_ < deferredQuotas_.size());
            deferredQuotas_[deferredCount_++] = caller.quotas;
            assert(caller.quotas[0]->liveBytes());
            if (!quarantined)
                assert(!caller.owner || !caller.owner->usedBytes());
            caller.source.reset();
            if (quarantined) {
                assert(quarantineCount_ < quarantineAllocators_.size());
                const auto i = quarantineCount_++;
                quarantineAllocators_[i] = caller.caller.allocator;
                quarantineSourceQuotas_[i] = caller.caller.quotas;
                quarantineStaging_[i] = caller.owner;
                caller.caller.texture.reset();
                caller.caller.layout.reset();
                caller.caller.registry.reset();
                assert(quarantineAllocators_[i]->memoryStats().allocatedBytes == 16u
                    && quarantineStaging_[i]->usedBytes() == 512u);
            } else
                caller.caller.retire();
            caller.source.reset();
            caller.owner.reset();
            caller.recipe = {};
            if (!quarantined)
                for (std::size_t i = 1u; i < caller.quotas.size(); ++i)
                    assert(!caller.quotas[i]->liveBytes());
        }
    };

    using CreationScopes = WebGpuErrorScopeHandoffFixture<84u>;

    static void verifyColdCreationBinding()
    {
        for (unsigned mode = 0u; mode < 3u; ++mode) {
            Probe p;
            const char* diagnostic = nullptr;
            p.destination = p.caller.prepare(diagnostic);
            assert(p.destination);
            EM_ASM({
                // clang-format off
                    Module.arpColdReadbackCreate = Module.preinitializedWebGPUDevice.createBuffer;
                    Module.arpColdReadbackCreateCalls = 0;
                    Module.preinitializedWebGPUDevice.createBuffer = function(d) {
                        ++Module.arpColdReadbackCreateCalls;
                        return Module.arpColdReadbackCreate.call(this, d);
                    };
                // clang-format on
            });
            assert(std::string_view(p.reservation->initializeBoundDestinationOriginal())
                == "RHI-READBACK-PUBLICATION-PREPARATION-NOT-BOUND");
            const auto& q = p.caller.quotas[1];
            const auto baseline = q->liveBytes();
            assert(!p.reservation->bindDestinationOriginalCold(p.destination));
            const auto peak = q->liveBytes() - baseline;
            assert(peak);
            assert(!p.destination->nativeResource() && !p.destination->creationCallbacksIssued()
                && p.destination->status() == Destination::Status::Prepared);
            assert(p.sink->pending() == 1u && !Sink::pollOriginal(p.sink, p.delivery, diagnostic)
                && !diagnostic);
            if (mode == 0u) {
                assert(p.reservation->cancel());
                p.reservation.reset();
                assert(!p.sink->pending());
                p.reservation = Sink::reserveRequestOriginal(p.sink,
                    p.caller.source->publicSourceLease()->originalRequest(),
                    p.statisticsPlan,
                    diagnostic);
                assert(p.reservation);
                {
                    core::CpuMemoryQuotaLease pressure(
                        q, q->limitBytes() - q->liveBytes() - peak + 1u);
                    assert(pressure.charged());
                    assert(
                        std::string_view(p.reservation->bindDestinationOriginalCold(p.destination))
                        == "RHI-READBACK-PUBLICATION-PREPARATION-ALLOCATION");
                }
                {
                    core::CpuMemoryQuotaLease pressure(q, q->limitBytes() - q->liveBytes());
                    assert(pressure.charged()
                        && p.reservation->bindDestinationOriginalCold(p.destination));
                }
                assert(p.sink->pending() == 1u && !p.destination->creationCallbacksIssued());
                {
                    core::CpuMemoryQuotaLease pressure(q, q->limitBytes() - q->liveBytes() - peak);
                    assert(pressure.charged()
                        && !p.reservation->bindDestinationOriginalCold(p.destination));
                }
                const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
                assert(p.reservation->cancel() && !p.sink->pending());
                assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
            } else if (mode == 1u) {
                std::weak_ptr<Sink> sink = p.sink;
                std::weak_ptr<Destination> destination = p.destination;
                p.dropExecutionCallers();
                assert(sink.expired() && destination.expired());
                sink.reset();
                destination.reset();
            } else {
                assert(p.reservation->rejectBeforeAttachment(
                    Sink::PreparationFailure::SourceContract));
                const auto a = core::defaultCpuAllocator().stats().allocationCount;
                assert(Sink::pollOriginal(p.sink, p.delivery, diagnostic) && !diagnostic
                    && p.delivery.outcome == Sink::Outcome::Rejected);
                assert(!p.delivery.result && !p.delivery.numericalResult && !p.sink->pending());
                assert(a == core::defaultCpuAllocator().stats().allocationCount);
            }
            assert(EM_ASM_INT({
                // clang-format off
                    return Module.arpColdReadbackCreateCalls;
                // clang-format on
            }) == 0);
            EM_ASM({
                // clang-format off
                    Module.preinitializedWebGPUDevice.createBuffer = Module.arpColdReadbackCreate;
                    delete Module.arpColdReadbackCreate;
                    delete Module.arpColdReadbackCreateCalls;
                // clang-format on
            });
            p.retire();
            std::printf(
                "BINDLESS_RHI_READBACK_CREATION_COLD_NATIVE_OK %u %llu exact-less-exhausted cold-bind-only creation0 unbound-repeated cancel-drop-reject cycle-retired quotas4\n",
                mode,
                static_cast<unsigned long long>(peak));
        }
    }

    static void verifyPreparationRetention()
    {
        for (unsigned mode = 0u; mode < 4u; ++mode) {
            Probe p;
            const char* diagnostic = nullptr;
            p.destination = p.caller.prepare(diagnostic);
            assert(p.destination);
            auto forged = std::shared_ptr<Destination>(p.caller.quotas[0], p.destination.get());
            assert(p.reservation->bindDestinationOriginalCold(forged)
                && p.destination->status() == Destination::Status::Prepared);
            forged.reset();
            {
                const auto& q = p.caller.quotas[1];
                core::CpuMemoryQuotaLease full(q, q->limitBytes() - q->liveBytes());
                assert(full.charged());
                assert(std::string_view(p.reservation->bindDestinationOriginalCold(p.destination))
                    == "RHI-READBACK-PUBLICATION-PREPARATION-ALLOCATION");
            }
            assert(p.sink->pending() == 1u && !p.destination->creationCallbacksIssued());
            if (mode == 0u) {
                const auto& r = p.caller.recipe;
                const auto& q = p.caller.quotas;
                auto wrongRequest = rhi::TextureReadbackRequestDeclaration::prepareOriginal(
                    p.caller.source->layout(),
                    {rhi::allocateTextureReadbackRequestId(), 37u, 43u},
                    r.request,
                    q[1],
                    diagnostic);
                assert(wrongRequest);
                auto wrongSink = Sink::prepareForDevicePlan(
                    r.originalPlan, p.caller.caller.registry, r.publication, diagnostic);
                assert(wrongSink);
                auto wrongTicket = Sink::reserveRequestOriginal(
                    wrongSink, wrongRequest, p.statisticsPlan, diagnostic);
                assert(wrongTicket);
                assert(std::string_view(wrongTicket->bindDestinationOriginalCold(p.destination))
                    == "RHI-READBACK-PUBLICATION-PREPARATION-REQUEST");
                wrongTicket->cancel();
                wrongTicket.reset();
                wrongSink.reset();
                wrongRequest.reset();
                auto otherRecipe =
                    WebGpuOriginalTextureReadbackDeviceStagingBudgetFixture::prepareRecipe();
                auto otherBudget =
                    WebGpuOriginalTextureReadbackDeviceStagingBudgetFixture::Owner::prepareOriginal(
                        otherRecipe.originalPlan, p.caller.caller.registry, diagnostic);
                assert(otherBudget);
                auto otherDestination = Destination::prepareDeviceOriginal(
                    p.caller.source, otherRecipe.destination, otherBudget, diagnostic);
                assert(otherDestination);
                assert(
                    std::string_view(p.reservation->bindDestinationOriginalCold(otherDestination))
                    == "RHI-READBACK-PUBLICATION-PREPARATION-PLAN");
                assert(!otherDestination->creationCallbacksIssued()
                    && !p.destination->creationCallbacksIssued());
                otherDestination.reset();
                assert(!otherBudget->usedBytes());
                otherBudget.reset();
                otherRecipe = {};
            }
            assert(!p.reservation->bindDestinationOriginalCold(p.destination)
                && p.reservation->bindDestinationOriginalCold(p.destination));
            CreationScopes::holdScopes();
            if (mode == 1u || mode == 3u)
                EM_ASM({
                    // clang-format off
                        Module.arpPreparationCreate = Module.preinitializedWebGPUDevice.createBuffer;
                        Module.preinitializedWebGPUDevice.createBuffer = function(d) {
                            return Module.arpPreparationCreate.call(this, Object.assign({}, d, {usage : 0}));
                        };
                    // clang-format on
                });
            const auto initializeAllocations = core::defaultCpuAllocator().stats().allocationCount;
            {
                const auto& q = p.caller.quotas;
                core::CpuMemoryQuotaLease request(q[1], q[1]->limitBytes() - q[1]->liveBytes()),
                    resource(q[2], q[2]->limitBytes() - q[2]->liveBytes());
                assert(request.charged() && resource.charged());
                assert(!p.reservation->initializeBoundDestinationOriginal());
            }
            assert(initializeAllocations == core::defaultCpuAllocator().stats().allocationCount);
            if (mode == 1u || mode == 3u)
                EM_ASM({
                    // clang-format off
                        Module.preinitializedWebGPUDevice.createBuffer = Module.arpPreparationCreate;
                        delete Module.arpPreparationCreate;
                    // clang-format on
                });
            CreationScopes::restoreScopes();
            CreationScopes::waitHeldScopes();
            assert(p.destination->creationCallbacksIssued()
                && !p.destination->creationScopesObserved() && p.sink->pending() == 1u);
            assert(p.reservation->initializeBoundDestinationOriginal());
            if (mode == 0u || mode == 2u || mode == 3u)
                assert(p.reservation->cancel());
            else
                assert(p.reservation->rejectBeforeAttachment(
                    Sink::PreparationFailure::DestinationCreation));
            const auto a = core::defaultCpuAllocator().stats().allocationCount;
            assert(!Sink::pollOriginal(p.sink, p.delivery, diagnostic) && !diagnostic
                && p.sink->pending() == 1u);
            const auto& request = p.caller.source->publicSourceLease()->originalRequest();
            assert(!Sink::reserveRequestOriginal(p.sink, request, p.statisticsPlan, diagnostic)
                && std::string_view(diagnostic) == "RHI-READBACK-PUBLICATION-REQUEST-DUPLICATE");
            std::weak_ptr<Sink> weakSink = p.sink;
            std::weak_ptr<Destination> weakDestination = p.destination;
            if (mode == 2u) {
                p.reservation.reset();
                p.sink.reset();
                p.destination.reset();
                p.statisticsPlan.reset();
                p.caller.source.reset();
                p.caller.owner.reset();
                p.caller.recipe = {};
                p.caller.caller.layout.reset();
                p.caller.caller.registry.reset();
                assert(!weakSink.expired() && !weakDestination.expired());
            }
            CreationScopes::releaseScope(0u);
            assert(WebGpuAsyncWaitFixture::until([&] {
                auto original = weakDestination.lock();
                return original && original->creationScopeObserved(0u);
            }));
            if (mode != 2u)
                assert(!Sink::pollOriginal(p.sink, p.delivery, diagnostic) && !diagnostic
                    && p.sink->pending() == 1u);
            assert(!weakSink.expired() && !weakDestination.expired());
            CreationScopes::releaseScopes();
            if (mode == 2u) {
                assert(WebGpuAsyncWaitFixture::until([&] {
                    return weakSink.expired() && weakDestination.expired();
                }));
                weakSink.reset();
                weakDestination.reset();
                p.caller.retire();
                for (const auto& q : p.oracleQuotas)
                    assert(!q->liveBytes());
            } else {
                assert(WebGpuAsyncWaitFixture::until([&] {
                    return p.destination->creationScopesObserved();
                }));
                if (mode == 1u) {
                    assert(Sink::pollOriginal(p.sink, p.delivery, diagnostic) && !diagnostic
                        && p.delivery.outcome == Sink::Outcome::Rejected && !p.delivery.result
                        && !p.delivery.numericalResult);
                    assert(std::string_view(p.delivery.diagnostic)
                            == "RHI-READBACK-PUBLICATION-PREPARATION-DESTINATION"
                        && core::SharedOwnerProjection::sameOwner(
                            p.delivery.originalRequest, request));
                } else
                    assert(!Sink::pollOriginal(p.sink, p.delivery, diagnostic) && !diagnostic);
                assert(!p.sink->pending());
                weakSink.reset();
                weakDestination.reset();
                p.retire();
            }
            assert(a == core::defaultCpuAllocator().stats().allocationCount);
            std::printf(
                "BINDLESS_RHI_READBACK_PREPARATION_RETENTION_OK %u pre-native-owner cancel-or-error-or-drop partial-scopes no-early-retire original-request capacity quotas4 allocation0\n",
                mode);
            std::printf(
                "BINDLESS_RHI_READBACK_CREATION_HOT_NATIVE_OK %u cold-bound scopes3 original-pending cancel-error-drop full-drain Request-Resource-exhausted allocation0\n",
                mode);
        }
    }

    using RecordingScopes = WebGpuErrorScopeHandoffFixture<85u>;

    struct ExecutionDomain {
        static constexpr unsigned graphics = 86u;
    };

    using ExecutionPromises = WebGpuOrdinarySubmissionFixture<ExecutionDomain>;

    static void verifyExecutionRetention(
        const std::shared_ptr<rhi_webgpu::WebGpuOrdinaryCompletionOwner>& owner)
    {
        for (unsigned mode = 0u; mode < 4u; ++mode) {
            Probe p;
            p.prepare(owner, false);
            const char* diagnostic = nullptr;
            assert(p.start() && !p.mapping->mapCompletionObserved());
            if (mode == 3u) {
                assert(p.reservation->cancel() && !p.sink->pending()
                    && !p.recording->readbackRecordingCallbacksIssued());
                p.retire();
            } else {
                RecordingScopes::holdScopes();
                if (mode == 1u)
                    EM_ASM({
                        // clang-format off
                            Module.arpExecutionEncoder = Module.preinitializedWebGPUDevice.createCommandEncoder;
                            Module.preinitializedWebGPUDevice.createCommandEncoder = function(d) {
                                const encoder = Module.arpExecutionEncoder.call(this, d);
                                const copy = encoder.copyTextureToBuffer;
                                encoder.copyTextureToBuffer = function(s, t, e) {
                                    return copy.call(this, s, Object.assign({}, t, {offset : 1}), e);
                                };
                                return encoder;
                            };
                        // clang-format on
                    });
                assert(!p.reservation->recordOriginal());
                if (mode == 1u)
                    EM_ASM({
                        // clang-format off
                            Module.preinitializedWebGPUDevice.createCommandEncoder = Module.arpExecutionEncoder;
                            delete Module.arpExecutionEncoder;
                        // clang-format on
                    });
                RecordingScopes::restoreScopes();
                RecordingScopes::waitHeldScopes();
                assert(p.recording->readbackRecordingCallbacksIssued()
                    && !p.recording->readbackRecordingCallbacksDrained());
                if (mode != 1u)
                    assert(p.reservation->cancel());
                std::weak_ptr<Sink> sink = p.sink;
                std::weak_ptr<Recording> recording = p.recording;
                if (mode == 2u)
                    p.dropExecutionCallers();
                const auto a = core::defaultCpuAllocator().stats().allocationCount;
                assert(!sink.expired() && !recording.expired());
                RecordingScopes::releaseScope(0u);
                assert(WebGpuAsyncWaitFixture::until([&] {
                    auto original = recording.lock();
                    return original
                        && original->readbackRecordingCallbacksRemaining()
                        == rhi_webgpu::WebGpuNativeErrorScopeBatch::count - 1u;
                }));
                assert(!sink.expired() && !recording.expired());
                if (mode != 2u)
                    assert(!Sink::pollOriginal(p.sink, p.delivery, diagnostic) && !diagnostic
                        && p.sink->pending() == 1u);
                RecordingScopes::releaseScopes();
                if (mode == 2u)
                    assert(WebGpuAsyncWaitFixture::until([&] {
                        return sink.expired() && recording.expired();
                    }));
                else {
                    assert(WebGpuAsyncWaitFixture::until([&] {
                        return p.recording->readbackRecordingCallbacksDrained();
                    }));
                    if (mode == 1u) {
                        assert(p.recording->status() == Recording::Status::Rejected);
                        assert(Sink::pollOriginal(p.sink, p.delivery, diagnostic) && !diagnostic
                            && p.delivery.outcome == Sink::Outcome::Rejected);
                        assert(!p.delivery.result && !p.delivery.numericalResult
                            && std::string_view(p.delivery.diagnostic)
                                == "RHI-READBACK-PUBLICATION-PREPARATION-RECORDING");
                        assert(core::SharedOwnerProjection::sameOwner(p.delivery.originalRequest,
                            p.caller.source->publicSourceLease()->originalRequest()));
                    } else
                        assert(!Sink::pollOriginal(p.sink, p.delivery, diagnostic) && !diagnostic);
                    assert(!p.sink->pending() && !p.mapping->mapCompletionObserved());
                    p.statistics->verifyUnavailable();
                }
                assert(a == core::defaultCpuAllocator().stats().allocationCount);
                sink.reset();
                recording.reset();
                p.retire();
            }
            std::printf(
                "BINDLESS_RHI_READBACK_EXECUTION_RECORDING_OK %u actual-scopes cancel-error-drop-unissued full-drain identity-only allocation0\n",
                mode);
        }
        for (unsigned mode = 0u; mode < 6u; ++mode) {
            Probe p;
            p.prepare(owner, false);
            p.record();
            p.prepareSubmission();
            const char* diagnostic = nullptr;
            if (mode == 5u) {
                assert(p.reservation->cancel() && !p.sink->pending()
                    && !p.submission->callbacksIssued());
                p.retire();
            } else {
                ExecutionPromises::holdActualPromises();
                if (mode == 2u || mode == 3u)
                    EM_ASM({
                        // clang-format off
                            Module.arpExecutionSubmit = Module.preinitializedWebGPUDevice.queue.submit;
                            Module.preinitializedWebGPUDevice.queue.submit = function(buffers) {
                                Module.arpExecutionSubmit.call(this, [ buffers[0], buffers[0] ]);
                            };
                        // clang-format on
                    });
                p.submit();
                if (mode == 2u || mode == 3u)
                    EM_ASM({
                        // clang-format off
                            Module.preinitializedWebGPUDevice.queue.submit = Module.arpExecutionSubmit;
                            delete Module.arpExecutionSubmit;
                        // clang-format on
                    });
                ExecutionPromises::restorePromiseMethods();
                ExecutionPromises::waitHeldPromises();
                assert(p.submission->callbacksIssued() && !p.submission->callbacksDrained());
                if (mode < 2u || mode == 4u)
                    assert(p.reservation->cancel());
                std::weak_ptr<Sink> sink = p.sink;
                std::weak_ptr<Submission> submission = p.submission;
                if (mode == 4u)
                    p.dropExecutionCallers();
                const auto a = core::defaultCpuAllocator().stats().allocationCount;
                if (mode % 2u == 0u) {
                    ExecutionPromises::releaseQueue();
                    assert(WebGpuAsyncWaitFixture::until([&] {
                        auto original = submission.lock();
                        return original && original->queueCompletionObserved();
                    }));
                } else {
                    ExecutionPromises::releaseScopes();
                    assert(WebGpuAsyncWaitFixture::until([&] {
                        auto original = submission.lock();
                        return original && original->scopeCompletionsObserved();
                    }));
                }
                assert(!sink.expired() && !submission.expired());
                if (mode != 4u)
                    assert(!Sink::pollOriginal(p.sink, p.delivery, diagnostic) && !diagnostic
                        && p.sink->pending() == 1u && !p.submission->callbacksDrained());
                if (mode % 2u == 0u)
                    ExecutionPromises::releaseScopes();
                else
                    ExecutionPromises::releaseQueue();
                if (mode == 4u)
                    assert(WebGpuAsyncWaitFixture::until([&] {
                        return sink.expired() && submission.expired();
                    }));
                else {
                    assert(WebGpuAsyncWaitFixture::until([&] {
                        return p.submission->callbacksDrained();
                    }));
                    if (mode >= 2u) {
                        assert(p.submission->status() == Submission::Status::Unproven
                            && p.completion->retained.failed()
                            && p.completion->retained.pending() == 1u);
                        assert(Sink::pollOriginal(p.sink, p.delivery, diagnostic) && !diagnostic
                            && p.delivery.outcome == Sink::Outcome::Rejected);
                        assert(!p.delivery.result && !p.delivery.numericalResult
                            && std::string_view(p.delivery.diagnostic)
                                == "RHI-READBACK-PUBLICATION-EXECUTION-SUBMISSION");
                        assert(core::SharedOwnerProjection::sameOwner(p.delivery.originalRequest,
                            p.caller.source->publicSourceLease()->originalRequest()));
                        // Publication retirement cannot clear native quarantine.
                        assert(p.completion->retained.failed()
                            && p.completion->retained.pending() == 1u);
                    } else
                        assert(!Sink::pollOriginal(p.sink, p.delivery, diagnostic) && !diagnostic);
                    assert(!p.sink->pending() && !p.mapping->mapCompletionObserved());
                    p.statistics->verifyUnavailable();
                }
                assert(a == core::defaultCpuAllocator().stats().allocationCount);
                sink.reset();
                submission.reset();
                p.retire();
            }
            std::printf(
                "BINDLESS_RHI_READBACK_EXECUTION_SUBMISSION_OK %u actual-queue-scopes both-orders cancel-error-drop-unissued full-drain quarantine allocation0\n",
                mode);
        }
    }

    static void prepareLoss(const std::shared_ptr<rhi_webgpu::WebGpuOrdinaryCompletionOwner>& owner)
    {
        for (unsigned phase = 0u; phase < 2u; ++phase) {
            auto& p = executionLoss_[phase];
            p = core::makeUnique<Probe>();
            p->prepare(owner, false, false, true);
            WebGpuOriginalDeviceLossRegistrationFixture::rememberNative(p->completion, 4u + phase);
            if (!phase) {
                RecordingScopes::holdScopes();
                assert(!p->reservation->recordOriginal());
                RecordingScopes::restoreScopes();
                RecordingScopes::waitHeldScopes();
            } else {
                p->record();
                p->prepareSubmission();
                ExecutionPromises::holdActualPromises();
                p->submit();
                ExecutionPromises::restorePromiseMethods();
                ExecutionPromises::waitHeldPromises();
            }
        }
        loss_ = core::makeUnique<Probe>();
        const char* diagnostic = nullptr;
        loss_->destination = loss_->caller.prepare(diagnostic);
        assert(loss_->destination);
        assert(!loss_->reservation->bindDestinationOriginalCold(loss_->destination));
        const auto a = core::defaultCpuAllocator().stats().allocationCount;
        CreationScopes::holdScopes();
        assert(!loss_->reservation->initializeBoundDestinationOriginal());
        CreationScopes::restoreScopes();
        CreationScopes::waitHeldScopes();
        assert(a == core::defaultCpuAllocator().stats().allocationCount);
    }

    static void verifyLoss()
    {
        assert(rhi_webgpu::webGpuNativeDeviceLost());
        assert(WebGpuAsyncWaitFixture::until([] {
            return quarantineRegistration_.expired();
        }));
        quarantineRegistration_.reset();
        registrationQuarantine_->retire();
        registrationQuarantine_.reset();
        for (unsigned phase = 0u; phase < 2u; ++phase) {
            auto& p = *executionLoss_[phase];
            const char* diagnostic = nullptr;
            assert(p.sink->pending() == 1u && p.caller.owner->usedBytes() == 512u);
            assert(!Sink::pollOriginal(p.sink, p.delivery, diagnostic) && !diagnostic);
            const auto a = core::defaultCpuAllocator().stats().allocationCount;
            if (!phase) {
                RecordingScopes::releaseScope(0u);
                assert(WebGpuAsyncWaitFixture::until([&] {
                    return p.recording->readbackRecordingCallbacksRemaining()
                        == rhi_webgpu::WebGpuNativeErrorScopeBatch::count - 1u;
                }));
            } else {
                ExecutionPromises::releaseQueue();
                assert(WebGpuAsyncWaitFixture::until([&] {
                    return p.submission->queueCompletionObserved();
                }));
            }
            assert(!Sink::pollOriginal(p.sink, p.delivery, diagnostic) && !diagnostic
                && p.sink->pending() == 1u);
            if (!phase) {
                RecordingScopes::releaseScopes();
                assert(WebGpuAsyncWaitFixture::until([&] {
                    return p.recording->readbackRecordingCallbacksDrained();
                }));
            } else {
                ExecutionPromises::releaseScopes();
                assert(WebGpuAsyncWaitFixture::until([&] {
                    return p.submission->callbacksDrained();
                }));
            }
            assert(Sink::pollOriginal(p.sink, p.delivery, diagnostic) && !diagnostic
                && p.delivery.outcome == Sink::Outcome::DeviceLost);
            assert(!p.delivery.result && !p.delivery.numericalResult
                && !p.mapping->mapCompletionObserved());
            assert(core::SharedOwnerProjection::sameOwner(p.delivery.originalRequest,
                p.caller.source->publicSourceLease()->originalRequest()));
            assert(std::string_view(p.delivery.diagnostic)
                == "RHI-READBACK-PUBLICATION-PREPARATION-DEVICE-LOST");
            p.retire();
            executionLoss_[phase].reset();
            assert(a == core::defaultCpuAllocator().stats().allocationCount);
            std::printf(
                "BINDLESS_RHI_READBACK_EXECUTION_LOSS_OK %u actual-loss partial-callbacks pending-retained identity-only final-drain\n",
                phase);
        }
        for (std::size_t i = 0u; i < quarantineCount_; ++i) {
            assert(!quarantineAllocators_[i]->memoryStats().allocatedBytes
                && !quarantineStaging_[i]->usedBytes());
            for (const auto& q : quarantineSourceQuotas_[i])
                assert(!q->liveBytes());
            quarantineAllocators_[i].reset();
            quarantineStaging_[i].reset();
            quarantineSourceQuotas_[i] = {};
        }
        quarantineCount_ = 0u;
        for (std::size_t i = 0u; i < deferredCount_; ++i) {
            for (const auto& q : deferredQuotas_[i])
                assert(!q->liveBytes());
            deferredQuotas_[i] = {};
        }
        deferredCount_ = 0u;
        std::puts(
            "BINDLESS_RHI_READBACK_EXECUTION_DEVICE_RETIRE_OK persistent-loss-owner quarantine-until-actual-loss quotas4-final-zero");
        auto& p = *loss_;
        const char* diagnostic = nullptr;
        assert(p.destination->status() == Destination::Status::DeviceLost && p.sink->pending() == 1u
            && p.caller.owner->usedBytes() == 512u);
        assert(!Sink::pollOriginal(p.sink, p.delivery, diagnostic) && !diagnostic);
        CreationScopes::releaseScope(0u);
        assert(WebGpuAsyncWaitFixture::until([&] {
            return p.destination->creationScopeObserved(0u);
        }));
        assert(!Sink::pollOriginal(p.sink, p.delivery, diagnostic) && !diagnostic
            && p.sink->pending() == 1u);
        CreationScopes::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return p.destination->creationScopesObserved();
        }));
        assert(Sink::pollOriginal(p.sink, p.delivery, diagnostic) && !diagnostic
            && p.delivery.outcome == Sink::Outcome::DeviceLost && !p.delivery.result
            && !p.delivery.numericalResult);
        assert(core::SharedOwnerProjection::sameOwner(
            p.delivery.originalRequest, p.caller.source->publicSourceLease()->originalRequest()));
        assert(std::string_view(p.delivery.diagnostic)
            == "RHI-READBACK-PUBLICATION-PREPARATION-DEVICE-LOST");
        p.retire();
        loss_.reset();
        std::puts(
            "BINDLESS_RHI_READBACK_PREPARATION_RETENTION_LOSS_OK actual-loss partial-scopes pending-staging-retained identity-only final-callback-retire");
        WebGpuOriginalDeviceLossRegistrationFixture::verifyDelayedLoss();
    }

    static void verifyActual(
        const std::shared_ptr<rhi_webgpu::WebGpuOrdinaryCompletionOwner>& owner)
    {
        verifyColdCreationBinding();
        verifyOriginalLossRegistration(owner);
        verifyPreparationRetention();
        verifyExecutionRetention(owner);
        for (unsigned order = 0u; order < 2u; ++order) {
            Probe p;
            p.prepare(owner, true, order == 0u);
            WebGpuOriginalTextureReadbackNativeMappingFixture::verifyCompleted(
                p.mapping, p.packing, order, &p);
            p.retire();
            std::printf(
                "BINDLESS_RHI_READBACK_SNAPSHOT_NATIVE_OK %u actual-queue-write-after-record copy-submit-map nonzero-bytes statistics provenance pending-staging quotas4 allocation0\n",
                order);
        }
    }

    static void verifyOriginalLossRegistration(
        const std::shared_ptr<rhi_webgpu::WebGpuOrdinaryCompletionOwner>& foreignOwner)
    {
        using Loss = WebGpuOriginalDeviceLossRegistrationFixture;
        using Owner = rhi_webgpu::WebGpuOrdinaryCompletionOwner;
        Loss::verifyIdleCold();
        Probe p;
        p.prepare(foreignOwner, false, false, true);
        Loss::rememberNative(p.completion, 1u);
        const auto reject = [&] {
            const auto a = core::defaultCpuAllocator().stats().allocationCount;
            assert(std::string_view(Owner::detachOriginalDeviceLoss(p.completion))
                == "RHI-DEVICE-LOSS-REGISTRATION-PENDING-GRAPHS");
            assert(a == core::defaultCpuAllocator().stats().allocationCount
                && Loss::armed(p.completion));
        };
        reject();
        RecordingScopes::holdScopes();
        assert(!p.reservation->recordOriginal());
        RecordingScopes::restoreScopes();
        RecordingScopes::waitHeldScopes();
        reject();
        RecordingScopes::releaseScope(0u);
        assert(WebGpuAsyncWaitFixture::until([&] {
            return p.recording->readbackRecordingCallbacksRemaining() == 2u;
        }));
        reject();
        RecordingScopes::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return p.recording->readbackRecordingCallbacksDrained();
        }));
        reject();
        p.writeOracle(113u);
        p.prepareSubmission();
        ExecutionPromises::holdActualPromises();
        p.submit();
        ExecutionPromises::restorePromiseMethods();
        ExecutionPromises::waitHeldPromises();
        reject();
        ExecutionPromises::releaseQueue();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return p.submission->queueCompletionObserved();
        }));
        reject();
        ExecutionPromises::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return p.submission->callbacksDrained();
        }));
        assert(p.submission->status() == Submission::Status::Completed);
        WebGpuOriginalTextureReadbackNativeMappingFixture::verifyCompleted(
            p.mapping, p.packing, 0u, &p);
        const auto a = core::defaultCpuAllocator().stats().allocationCount;
        assert(!Owner::detachOriginalDeviceLoss(p.completion));
        p.retire();
        assert(a == core::defaultCpuAllocator().stats().allocationCount);
        std::puts(
            "BINDLESS_RHI_DEVICE_LOSS_REGISTRATION_COMPLETED_NATIVE_OK original-copy-submit-map-statistics pending-partials-reject detach closes-admission quotas4-zero allocation0");
        registrationQuarantine_ = core::makeUnique<Probe>();
        auto& q = *registrationQuarantine_;
        q.prepare(foreignOwner, false, false, true);
        Loss::rememberNative(q.completion, 3u);
        q.record();
        q.prepareSubmission();
        EM_ASM({
            // clang-format off
                Module.arpOriginalLossSubmit = Module.preinitializedWebGPUDevice.queue.submit;
                Module.preinitializedWebGPUDevice.queue.submit = function(buffers) {
                    Module.arpOriginalLossSubmit.call(this, [ buffers[0], buffers[0] ]);
                };
            // clang-format on
        });
        q.submit();
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.queue.submit = Module.arpOriginalLossSubmit;
                delete Module.arpOriginalLossSubmit;
            // clang-format on
        });
        assert(WebGpuAsyncWaitFixture::until([&] {
            return q.submission->callbacksDrained();
        }));
        assert(q.submission->status() == Submission::Status::Unproven);
        assert(q.completion->retained.failed() && q.completion->retained.pending() == 1u
            && Owner::detachOriginalDeviceLoss(q.completion));
        quarantineRegistration_ = q.completion;
        q.delivery = {};
        q.dropExecutionCallers();
        q.caller.caller.texture.reset();
        assert(!quarantineRegistration_.expired() && q.caller.quotas[0]->liveBytes()
            && q.caller.caller.allocator.memoryStats().allocatedBytes == 16u);
        std::puts(
            "BINDLESS_RHI_DEVICE_LOSS_REGISTRATION_QUARANTINE_NATIVE_OK actual-duplicate-submit-error detach-reject all-callers-drop original-native-graph-until-loss");
    }

    // Twelve executed/canceled probes have real persistent loss subscriptions:
    // four recordings, six submissions (including unissued), two snapshots.
    static inline std::array<rhi::TextureReadbackColdBudgetPlan::Quotas, 12> deferredQuotas_;
    static inline std::size_t deferredCount_ = 0u, quarantineCount_ = 0u;
    static inline std::array<std::optional<rhi_webgpu::WebGpuRhiAllocator>, 2>
        quarantineAllocators_;
    static inline std::array<std::array<std::shared_ptr<core::CpuMemoryQuota>, 4>, 2>
        quarantineSourceQuotas_;
    static inline std::array<
        std::shared_ptr<const WebGpuOriginalTextureReadbackDeviceStagingBudgetFixture::Owner>,
        2>
        quarantineStaging_;
    static inline core::UniquePtr<Probe> loss_;
    static inline std::array<core::UniquePtr<Probe>, 2> executionLoss_;
    static inline core::UniquePtr<Probe> registrationQuarantine_;
    static inline std::weak_ptr<rhi_webgpu::WebGpuOrdinaryCompletionOwner> quarantineRegistration_;
#endif
};
} // namespace arp::test
