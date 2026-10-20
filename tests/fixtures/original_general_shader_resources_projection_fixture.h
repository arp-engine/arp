#pragma once
#include "original_accepted_compute_pipeline_factory_fixture.h"
#include "original_completed_sampled_input_fixture.h"
#include "original_general_descriptor_binding_fixture.h"
#include "original_general_descriptor_source_browser_fixture.h"
#include "original_general_finite_claim_terminal_fixture.h"
#include "original_general_finite_consumption_fixture.h"
#include "original_general_finite_content_plan_fixture.h"
#include "original_general_finite_execution_fixture.h"
#include "original_general_finite_graphics_content_root_fixture.h"
#include "original_general_finite_graphics_content_sources_fixture.h"
#include "original_general_finite_graphics_inputs_fixture.h"
#include "original_general_finite_graphics_recording_fixture.h"
#include "original_general_finite_graphics_state_stream_fixture.h"
#include "original_general_finite_graphics_usage_fixture.h"
#include "original_general_finite_initialized_content_browser_fixture.h"
#include "original_general_finite_inputs_fixture.h"
#include "original_general_finite_native_closure_fixture.h"
#include "original_general_finite_native_projection_fixture.h"
#include "original_general_finite_native_recording_fixture.h"
#include "original_general_finite_physical_loss_fixture.h"
#include "original_general_finite_state_stream_fixture.h"
#include "original_general_finite_submission_terminal_fixture.h"
#include "original_general_finite_usage_fixture.h"
#include "original_general_output_readback_inputs_fixture.h"
#include "original_target_memory_fixture.h"
#include "original_typed_callback_custody_fixture.h"
#include "original_typed_cold_admission_fixture.h"
#include "original_typed_issuer_preparation_fixture.h"
#include "original_typed_native_payload_fixture.h"
#include "original_typed_usage_projection_fixture.h"
#include "rhi/backends/webgpu/webgpu_canonical_native_closure.h"
#include "rhi/backends/webgpu/webgpu_original_typed_callback_custody.h"
#include "rhi/backends/webgpu/webgpu_original_typed_dependency_decision.h"
#include "rhi/backends/webgpu/webgpu_original_typed_issuer_preparation.h"
#include "webgpu_async_wait_fixture.h"
#include "webgpu_canonical_texture_upload_source_fixture.h"

namespace arp::test {
class OriginalGeneralShaderResourcesProjectionFixture final {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
    using Sources = rhi::OriginalCommandResourceSources;
    using A = rhi::OriginalCommandComputePipelineSource;
    using P = rhi::common::OriginalTypedUsageProjection;
    using D = rhi::common::OriginalTypedDependencyProjection;
    using S = rhi::semantics::TypedShaderUsageScopeContract;
    enum class RecordingTerminal { None, Cancel, Drop, NativeError, ReverseScopes };
    enum class SubmissionTerminal {
        None,
        Cancel,
        Retired,
        Shutdown,
        ThrowBefore,
        ThrowAfter,
        NativeError,
        ReverseScopes,
        PartialScope
    };
    enum class PhysicalLoss {
        None,
        QueueUncertain,
        RecordingShutdown,
        PartialPush,
        RecordingException
    };
    static constexpr std::uint64_t bytes = 65536u;

    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "general original resource projection: %s\n", id);
        assert(!id);
    }

    static auto texture(rhi::GpuDevice& device,
        const std::shared_ptr<core::CpuMemoryQuota>& source,
        const std::shared_ptr<core::CpuMemoryQuota>& resource,
        bool storage,
        bool outputSnapshot = false,
        bool renderAttachment = false)
    {
        assert(!renderAttachment || (!storage && !outputSnapshot));
        core::UniquePtr<rhi::GpuTexture> object;
        core::UniquePtr<rhi::GpuTextureView> view;
        {
            core::CpuMemoryQuotaScope scope(resource);
            rhi::TextureDesc d;
            d.extent = {1u, 1u, 1u};
            d.format = rhi::TextureFormat::Rgba16Float;
            d.usage = renderAttachment ? rhi::TextureUsage::RenderAttachment
                : storage              ? rhi::TextureUsage::Storage
                                       : rhi::TextureUsage::Sampled;
            if (outputSnapshot)
                d.usage = d.usage | rhi::TextureUsage::CopySrc;
            d.initialState = renderAttachment ? rhi::ResourceState::ColorAttachment
                : storage                     ? rhi::ResourceState::ShaderWrite
                                              : rhi::ResourceState::ShaderRead;
            object = device.createTexture(d);
            assert(object);
            rhi::TextureViewDesc v;
            v.format = d.format;
            v.usage = renderAttachment ? rhi::TextureUsage::RenderAttachment
                : storage              ? rhi::TextureUsage::Storage
                                       : rhi::TextureUsage::Sampled;
            view = device.createTextureView(*object, v);
            assert(view);
        }
        const char* id = nullptr;
        auto parent = rhi::OriginalCommandTextureLease::prepareOriginal(
            object, {1u, true, 2u, bytes}, source, id);
        require(id);
        assert(parent);
        auto lease = rhi::OriginalCommandTextureViewLease::prepareOriginal(
            view, {1u, true, 2u, bytes}, source, id);
        require(id);
        assert(lease);
        auto original = rhi::OriginalCommandTextureViewSource::prepareOriginal(
            lease, parent, {1u, true, 5u, bytes}, source, id);
        require(id);
        assert(original);
        return std::array<Sources::Source, 2u>{{parent, original}};
    }

    template <class Context>
    static auto project(Context& c,
        bool submitGeneral = false,
        RecordingTerminal terminal = RecordingTerminal::None,
        SubmissionTerminal submissionTerminal = SubmissionTerminal::None,
        PhysicalLoss physicalLoss = PhysicalLoss::None)
    {
        using FiniteSubmission = OriginalGeneralFiniteSubmissionTerminalFixture;
        using FiniteLoss = OriginalGeneralFinitePhysicalLossFixture;
        const auto finitePhysicalTerminal = FiniteLoss::selectedTerminal();
        const bool finitePhysical = finitePhysicalTerminal.has_value();
        const auto finiteSubmissionTerminal = FiniteSubmission::selectedTerminal();
        const bool finiteSubmission = finiteSubmissionTerminal != FiniteSubmission::Terminal::None;
        assert(!finitePhysical || !finiteSubmission);
        using FiniteReadback = OriginalGeneralOutputReadbackInputsFixture;
        const auto finiteMapTerminal = FiniteReadback::selectedMapTerminal();
        const bool finiteMap =
            finiteMapTerminal != FiniteReadback::MapTerminal::None && EM_ASM_INT({
                // clang-format off
                        return new URLSearchParams(location.search).has("general-finite-lifecycle");
                // clang-format on
            });
        const bool finiteMapPhysical =
            finiteMap && FiniteReadback::mapUsesPhysicalLoss(finiteMapTerminal);
        assert(!finiteMap || (!finitePhysical && !finiteSubmission));
        using FiniteClaim = OriginalGeneralFiniteClaimTerminalFixture;
        const auto finiteClaimTerminal = FiniteClaim::selectedTerminal();
        const bool finiteClaim = finiteClaimTerminal != FiniteClaim::Terminal::None;
        const bool finiteClaimPhysical =
            finiteClaim && FiniteClaim::physicalLoss(finiteClaimTerminal);
        const bool finiteClaimLogical =
            finiteClaim && FiniteClaim::logicalTerminal(finiteClaimTerminal);
        assert(!finiteClaim || (!finiteMap && !finitePhysical && !finiteSubmission));
        const bool finitePostSuppression =
            finiteSubmissionTerminal == FiniteSubmission::Terminal::PostSuppressionCommit
            || finiteMapTerminal == FiniteReadback::MapTerminal::PostSuppressionPublication;
        const bool finiteInvalidates = finiteClaimPhysical || finitePhysical
            || FiniteSubmission::invalidates(finiteSubmissionTerminal);
        const bool finiteExecution =
            finiteClaim || finiteMap || finitePhysical || finiteSubmission || EM_ASM_INT({
                // clang-format off
                        return new URLSearchParams(location.search).has("general-finite-execution");
                // clang-format on
            });
        const bool finiteLifecycle = EM_ASM_INT({
            // clang-format off
                    return new URLSearchParams(location.search).has("general-finite-lifecycle");
            // clang-format on
        });
        const bool finiteRecording = finiteLifecycle || finiteExecution || EM_ASM_INT({
            // clang-format off
                return new URLSearchParams(location.search).has("general-finite-native-recording");
            // clang-format on
        });
        const bool finitePayload = finiteRecording || EM_ASM_INT({
            // clang-format off
                return new URLSearchParams(location.search).has("general-finite-native-payload");
            // clang-format on
        });
        const bool finiteClosure = finitePayload || EM_ASM_INT({
            // clang-format off
                return new URLSearchParams(location.search).has("general-finite-native-closure");
            // clang-format on
        });
        const bool finitePlan = finiteClosure || EM_ASM_INT({
            // clang-format off
                return new URLSearchParams(location.search).has("general-finite-content-plan");
            // clang-format on
        });
        const bool wholePlan = finitePlan && EM_ASM_INT({
            // clang-format off
                return new URLSearchParams(location.search).has("general-source-whole");
            // clang-format on
        });
        const bool dynamicPlan = finitePlan && EM_ASM_INT({
            // clang-format off
                return new URLSearchParams(location.search).has("general-content-plan-dynamic");
            // clang-format on
        });
        assert(!finitePlan
            || (!submitGeneral && terminal == RecordingTerminal::None
                && submissionTerminal == SubmissionTerminal::None
                && physicalLoss == PhysicalLoss::None));
        assert(!wholePlan || !dynamicPlan);
        assert(submissionTerminal == SubmissionTerminal::None
            || (submitGeneral && terminal == RecordingTerminal::None));
        const bool submissionProbe = submissionTerminal >= SubmissionTerminal::ThrowBefore;
        const bool submissionTerminated = submissionTerminal != SubmissionTerminal::None
            && submissionTerminal != SubmissionTerminal::ReverseScopes;
        const bool physical = physicalLoss != PhysicalLoss::None;
        assert(!physical
            || (terminal == RecordingTerminal::None
                && submissionTerminal == SubmissionTerminal::None
                && submitGeneral == (physicalLoss == PhysicalLoss::QueueUncertain)));
        const bool outputPhysicalLoss = submitGeneral
            && OriginalGeneralOutputReadbackInputsFixture::mapUsesPhysicalLoss(
                OriginalGeneralOutputReadbackInputsFixture::selectedMapTerminal());
        const bool lifetimeTerminated = physical || outputPhysicalLoss
            || submissionTerminal == SubmissionTerminal::Retired
            || submissionTerminal == SubmissionTerminal::Shutdown;
        const auto resource = c.quotas[2];
        auto source = core::makeShared<core::CpuMemoryQuota>(bytes),
             inputQuota = core::makeShared<core::CpuMemoryQuota>(bytes),
             stateQuota = core::makeShared<core::CpuMemoryQuota>(bytes),
             streamQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        const std::array initializationQuotas{core::makeShared<core::CpuMemoryQuota>(bytes),
            core::makeShared<core::CpuMemoryQuota>(bytes),
            core::makeShared<core::CpuMemoryQuota>(bytes)};
        const char* id = nullptr;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 18u> textureQuotas;
        {
            auto storage =
                texture(*c.device, source, resource, true, submitGeneral || finiteExecution);
            WebGpuCanonicalTextureUploadSourceFixture::BorrowedContext sampledUpload(
                *c.device, source, resource);
            using TextureContent = rhi_webgpu::WebGpuCanonicalTextureContentUploadSource;
            using Issuer = rhi_webgpu::WebGpuCanonicalIssuerPreparation;
            auto sampledInitialization =
                OriginalCompletedSampledInputFixture::prepare(sampledUpload, textureQuotas);
            auto& textureStaging = std::get<0>(sampledInitialization);
            auto& textureContent = std::get<1>(sampledInitialization);
            auto& uploadInputs = std::get<2>(sampledInitialization);
            auto& uploadRoot = std::get<3>(sampledInitialization);
            auto& uploadIssuer = std::get<4>(sampledInitialization);
            auto& completedTexture = std::get<5>(sampledInitialization);
            auto parent = sampledUpload.binding->originalDestinationLease();
            core::UniquePtr<rhi::GpuTextureView> sampledView;
            {
                core::CpuMemoryQuotaScope scope(resource);
                rhi::TextureViewDesc desc;
                desc.format = parent->originalObject()->desc().format;
                desc.usage = rhi::TextureUsage::Sampled;
                desc.mipLevelCount = 2u;
                sampledView = c.device->createTextureView(*parent->originalObject(), desc);
                assert(sampledView);
            }
            auto sampledLease = rhi::OriginalCommandTextureViewLease::prepareOriginal(
                sampledView, {1u, true, 2u, bytes}, source, id);
            require(id);
            assert(sampledLease);
            auto sampledSource = rhi::OriginalCommandTextureViewSource::prepareOriginal(
                sampledLease, parent, {1u, true, 5u, bytes}, source, id);
            require(id);
            assert(sampledSource);
            std::array<Sources::Source, 2u> sampled{{parent, sampledSource}};
            using Initialized = rhi_webgpu::WebGpuCanonicalBufferUploadSource;
            using Upload = rhi::OriginalCommandInitializedBufferStagingContract;
            const auto bufferType = c.layout->desc().descriptorLayouts[0].bindings[0].type;
            const bool readOnly = bufferType == rhi::DescriptorType::ReadOnlyStorageBuffer;
            assert(bufferType == rhi::DescriptorType::UniformBuffer || (finitePlan && readOnly));
            const auto alignment = readOnly
                ? rhi_webgpu::webGpuNativeDeviceLimits().storageBufferOffsetAlignment
                : rhi_webgpu::webGpuNativeDeviceLimits().uniformBufferOffsetAlignment;
            assert(alignment && alignment <= UINT32_MAX / 3u);
            const auto displacement = dynamicPlan ? alignment : 0u;
            const auto bufferOffset =
                finitePlan && !wholePlan ? std::uint64_t(alignment) + displacement : 0u;
            const auto extent =
                finitePlan && !wholePlan ? std::uint64_t(alignment) * 2u + 16u : 16u;
            Upload::Budget initialize{4u,
                true,
                1u,
                Upload::callbackCount,
                Upload::retainedOwnerBound,
                extent,
                bytes,
                bytes,
                bytes,
                bytes,
                bytes};
            initialize.shaderContent = {1u, true, bufferType, extent};
            const std::array<std::uint8_t, 16u> values{
                {0u, 0u, 128u, 63u, 0u, 0u, 0u, 64u, 0u, 0u, 64u, 64u, 0u, 0u, 128u, 64u}};
            core::ByteBuffer initialBytes(&core::defaultCpuMemoryResource());
            {
                core::CpuMemoryQuotaScope scope(resource);
                initialBytes.resize(static_cast<std::size_t>(extent));
            }
            std::fill(initialBytes.begin(), initialBytes.end(), std::uint8_t{0xa7u});
            std::memcpy(initialBytes.data() + bufferOffset, values.data(), values.size());
            if (finiteExecution) {
                // Distinct aligned candidates distinguish a missing or doubled
                // dynamic offset. Untouched backing bytes retain the sentinel.
                const auto writeCandidate = [&](std::uint64_t offset, std::uint32_t first) {
                    for (std::uint32_t channel = 0u; channel < 4u; ++channel) {
                        const auto word = first + channel;
                        if (readOnly)
                            std::memcpy(initialBytes.data() + offset + channel * sizeof(word),
                                &word,
                                sizeof(word));
                        else {
                            const auto value = float(word);
                            std::memcpy(initialBytes.data() + offset + channel * sizeof(value),
                                &value,
                                sizeof(value));
                        }
                    }
                };
                writeCandidate(0u, wholePlan ? 1u : 9u);
                if (!wholePlan) {
                    writeCandidate(alignment, 1u);
                    writeCandidate(std::uint64_t(alignment) * 2u, 5u);
                }
            }
            core::UniquePtr<rhi::GpuBuffer> object;
            {
                core::CpuMemoryQuotaScope scope(resource);
                rhi::BufferDesc d;
                d.sizeBytes = extent;
                d.usage = Upload::expectedUsage(initialize);
                d.memory = rhi::MemoryUsage::CpuToGpu;
                d.initialState = Upload::expectedState(initialize);
                object = c.device->createBuffer(d);
                assert(object);
            }
            auto initializationLoss = c.device->observeOriginalDeviceNativeLoss(id);
            require(id);
            assert(initializationLoss);
            const std::array uploadQuotas{initializationQuotas[0],
                initializationQuotas[1],
                resource,
                source,
                initializationQuotas[2]};
            auto initialized = Initialized::prepareOriginal(*c.device,
                object,
                *initializationLoss,
                initialBytes.data(),
                initialBytes.size(),
                1u,
                initialize,
                uploadQuotas,
                id);
            require(id);
            assert(initialized && !object);
            auto uniform = initialized->originalLease();
            const auto ready = [](const auto& content) noexcept {
                return content.readyDiagnostic();
            };
            assert(Initialized::validateInitializedShaderBufferWithReady(
                initialized, uniform, bufferType, 0u, extent, ready));
            require(Initialized::initialize(initialized));
            assert(WebGpuAsyncWaitFixture::until([&] {
                return initialized->callbacksDrained();
            }));
            require(initialized->readyDiagnostic());
            require(
                initialized->compareFrozenPayload(initialBytes.data(), initialBytes.size(), 1u));
            require(Initialized::validateInitializedShaderBufferWithReady(
                initialized, uniform, bufferType, 0u, extent, ready));
            assert(Initialized::validateInitializedShaderBufferWithReady(initialized,
                uniform,
                readOnly ? rhi::DescriptorType::UniformBuffer
                         : rhi::DescriptorType::ReadOnlyStorageBuffer,
                bufferOffset,
                16u,
                ready));
            auto forgedInitialized =
                std::shared_ptr<const Initialized>(initializationQuotas[0], initialized.get());
            assert(Initialized::validateInitializedShaderBufferWithReady(
                forgedInitialized, uniform, rhi::DescriptorType::UniformBuffer, 0u, 16u, ready));
            forgedInitialized.reset();
            core::UniquePtr<rhi::GpuSampler> samplerObject;
            {
                core::CpuMemoryQuotaScope scope(resource);
                samplerObject = c.device->createSampler({});
                assert(samplerObject);
            }
            auto sampler = rhi::OriginalCommandSamplerLease::prepareOriginal(
                samplerObject, {1u, true, 2u, bytes}, source, id);
            require(id);
            assert(sampler);
            std::array<Sources::Source, 6u> prefix{
                {uniform, storage[0], storage[1], sampled[0], sampled[1], sampler}};
            auto pipeline = rhi::OriginalRegisteredPipeline::shareAcceptedOriginal(c.publish(), id);
            require(id);
            assert(pipeline);
            auto adapter = A::prepareOriginal(pipeline,
                {1u,
                    true,
                    A::kind,
                    pipeline->originalResourceRegistration().generation(),
                    bytes,
                    A::exclusiveOwnerCount},
                source,
                *c.device,
                id);
            require(id);
            assert(adapter);
            rhi::OriginalCommandAdmissionBudget authored;
            authored.contractVersion = 1u;
            authored.prototypeOptIn = true;
            // One pre-pass whole two-mip transition, the compute pass and
            // three post-pass dependencies. The initialized texture remains
            // committed CopyDst until this same original State/Stream executes.
            authored.maxCommands = 10u;
            authored.maxReferences = authored.maxInputDescriptors = 6u;
            authored.maxStateObservations = 6u;
            authored.maxTextureRanges = 5u;
            // Exact authored domain: seven CPU roots (Sources/Usage/Scalars/
            // variable arguments/Inputs/State/Stream), uniform and sampler leases, two
            // complete view-parent closures, pipeline closure, set and arena.
            // State additionally retains fixed registration owners and the
            // eight registered sources plus their layout and descriptor arena.
            constexpr auto sourceCount = 8u, associatedRegistrations = 2u, cpuRoots = 7u;
            authored.maxGraphOwners = A::exclusiveOwnerCount + cpuRoots
                + rhi::OriginalCommandBufferLease::retainedOwnerCount
                + rhi::OriginalCommandSamplerLease::retainedOwnerCount
                + 2u * rhi::OriginalCommandTextureViewSource::completeRetainedOwnerCount
                + rhi::OriginalCommandDescriptorSetSource::canonicalExclusiveRetainedOwnerCount + 1u
                + rhi::OriginalResourceRegistrationContract::fixedOwnerCount + sourceCount
                + associatedRegistrations;
            authored.maxCpuBytes.fill(bytes);
            if (finitePlan) {
                authored.maxDynamicOffsets = wholePlan ? 0u : 1u;
                authored.maxGraphOwners +=
                    rhi::OriginalCommandDescriptorSetSource::finiteExclusiveRetainedOwnerCount
                    - rhi::OriginalCommandDescriptorSetSource::canonicalExclusiveRetainedOwnerCount;
            }
            // This workload now retains its one original transaction and all
            // eight actual native payloads; CPU owner/byte ceilings stay fixed.
            authored.maxStateTransactions = 1u;
            // Native resource custody plus the explicitly authored single
            // encoder/finished-command and original eight callback participants.
            using Cold = rhi_webgpu::WebGpuOriginalTypedColdAdmission;
            const auto coldCapacity = Cold::lifecycleRequirements(finiteRecording ? 7u : 6u);
            authored.maxNativePayloadObjects =
                sourceCount + coldCapacity.encoders + coldCapacity.commandBuffers;
            rhi::OriginalTypedNativeColdWorkCounts coldWork;
            coldWork.computePasses = coldWork.pipelineBinds = coldWork.descriptorBinds =
                coldWork.dispatches = 1u;
            coldWork.dependencies = coldWork.implicitAccesses = 4u;
            require(rhi::OriginalTypedNativeColdContract::inspectWorkTotals(
                coldWork, coldWork.commands, coldWork.nativeCalls));
            assert(coldWork.commands == authored.maxCommands);
            authored.maxTypedNativeCommandCalls = coldWork.nativeCalls;
            authored.maxPendingSubmissions = coldCapacity.capacity.pendingSubmissions;
            authored.maxCallbackTickets = coldCapacity.capacity.callbackTickets;
            authored.maxCallbackRegistrations = coldCapacity.capacity.callbackRegistrations;
            authored
                .maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Buffer)] =
                1u;
            authored
                .maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Sampler)] =
                1u;
            authored
                .maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Texture)] =
                2u;
            authored.maxSourceOwners[static_cast<std::size_t>(
                rhi::CommandReferenceDomain::TextureView)] = 2u;
            authored
                .maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Pipeline)] =
                1u;
            authored.maxSourceOwners[static_cast<std::size_t>(
                rhi::CommandReferenceDomain::Descriptor)] = 1u;
            auto sources = Sources::prepareColdOriginalTyped(prefix.data(),
                prefix.size(),
                authored,
                {finitePlan ? 5u : 2u, true, 2u},
                source,
                id);
            require(id);
            assert(sources);
            require(Sources::appendOriginalTyped(sources, {adapter}, *c.device));
            std::shared_ptr<rhi::DescriptorArena> arena;
            core::UniquePtr<rhi::DescriptorSet> set;
            {
                core::CpuMemoryQuotaScope scope(resource);
                rhi::DescriptorArenaDesc d;
                d.maxSets = 1u;
                d.maxDescriptors = 4u;
                arena = core::shareUnique(c.device->createOriginalDescriptorArena(d));
                assert(arena);
                rhi::DescriptorSetDesc sd;
                sd.layout = rhi::DescriptorStateCopy::copy(c.layout->desc().descriptorLayouts[0]);
                set = c.device->allocateOriginalDescriptorSet(arena, sd);
                assert(set);
            }
            std::array<rhi::OriginalCommandDescriptorSetElement, 4u> elements;
            for (std::size_t i = 0u; i < 3u; ++i) {
                auto& e = elements[i];
                e.write.binding = static_cast<std::uint32_t>(i);
                e.sourceIndex = i == 0u ? 0u : i == 1u ? 2u : 4u;
                e.write.resource = Sources::resource(sources->sources()[e.sourceIndex]);
                e.write.type = i == 0u ? bufferType
                    : i == 1u          ? rhi::DescriptorType::StorageTexture
                                       : rhi::DescriptorType::SampledTexture;
                e.write.resourceKind = i == 0u ? rhi::DescriptorResourceKind::Buffer
                                               : rhi::DescriptorResourceKind::TextureView;
                e.write.resourceState =
                    i == 1u ? rhi::ResourceState::ShaderWrite : rhi::ResourceState::ShaderRead;
                e.stages = rhi::PipelineStage::ComputeShader;
                e.access =
                    i == 1u ? rhi::ResourceAccess::ShaderWrite : rhi::ResourceAccess::ShaderRead;
                e.bufferAccessBytes = i == 0u ? 16u : 0u;
                if (i == 0u && finitePlan && !wholePlan) {
                    e.write.bufferBindingRange = {2u, alignment, 16u};
                    e.dynamicOffset = displacement;
                    e.bufferAccessOffset = bufferOffset;
                }
                e.registrationGeneration = i == 0u
                    ? uniform->originalObject()->originalResourceRegistration().generation()
                    : std::get<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                          sources->sources()[e.sourceIndex])
                          ->originalObject()
                          ->originalResourceRegistration()
                          .generation();
            }
            {
                auto& e = elements[3];
                e.write.binding = 3u;
                e.sourceIndex = 5u;
                e.write.type = rhi::DescriptorType::Sampler;
                e.write.resourceKind = rhi::DescriptorResourceKind::Sampler;
                e.write.resource = sampler->resource();
                e.write.resourceState = rhi::ResourceState::Undefined;
                e.stages = rhi::PipelineStage::ComputeShader;
                e.registrationGeneration =
                    sampler->originalObject()->originalResourceRegistration().generation();
            }
            const auto ordinarySampled = set->write(elements[2].write);
            assert(!ordinarySampled.success
                && ordinarySampled.diagnostics == "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE");
            if (!finitePlan)
                OriginalGeneralDescriptorBindingFixture::verifyActual(
                    *c.device, pipeline, sources, elements, source);
            const auto descriptorAllocationBytes =
                uniform->originalObject()->allocation().estimatedSizeBytes
                + std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(storage[0])
                      ->originalObject()
                      ->allocation()
                      .estimatedSizeBytes
                + parent->originalObject()->allocation().estimatedSizeBytes;
            std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource> descriptor;
            std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u> finiteWriteQuotas;
            if (finitePlan) {
                auto accepted =
                    OriginalGeneralDescriptorSourceBrowserFixture::verifyActual(*c.device,
                        pipeline,
                        sources,
                        arena,
                        set,
                        elements,
                        resource,
                        descriptorAllocationBytes);
                descriptor = std::move(accepted.first);
                finiteWriteQuotas = std::move(accepted.second);
            } else {
                OriginalGeneralDescriptorWriteFixture::verifyActual(
                    *c.device, pipeline, sources, *set, elements, descriptorAllocationBytes);
                descriptor = rhi::OriginalCommandDescriptorSetSource::prepareOriginal(set,
                    arena,
                    sources,
                    elements.data(),
                    elements.size(),
                    {2u,
                        true,
                        4u,
                        4u,
                        rhi::OriginalCommandDescriptorSetSource::
                            canonicalExclusiveRetainedOwnerCount,
                        bytes},
                    source,
                    *c.device,
                    id);
                require(id);
                assert(descriptor);
                require(Sources::appendOriginalTyped(sources, {descriptor}, *c.device));
            }
            using K = rhi::CommandUsageKind;
            using Scalar = rhi::CommandScalarArguments;
            using Inputs = rhi::OriginalCommandInputs;
            const std::array<rhi::CommandUsage, 10u> commands{{{K::TextureBarrier, 0u, 1u},
                {K::BeginComputePass, 1u, 0u},
                {K::BindComputePipeline, 1u, 1u},
                {K::BindComputeDescriptor, 2u, 1u},
                {K::Dispatch, 3u, 0u},
                {K::EndComputePass, 3u, 0u},
                {K::BufferMemoryBarrier, 3u, 1u},
                {K::TextureMemoryBarrier, 4u, 1u},
                {K::TextureMemoryBarrier, 5u, 1u},
                {K::Finish, 6u, 0u}}};
            const std::array<rhi::OriginalCommandUsage::Reference, 6u> refs{
                {{3u}, {6u}, {7u}, {0u}, {1u}, {3u}}};
            auto usage = rhi::OriginalCommandUsage::prepareOriginal(sources,
                commands.data(),
                commands.size(),
                refs.data(),
                refs.size(),
                inputQuota,
                id);
            require(id);
            assert(usage);
            const auto field = [&](std::size_t i) -> rhi::CommandVariableArguments::Input {
                const auto value = i == 3u
                    ? Scalar::capture(commands[i].kind, Scalar::DescriptorBinding{0u})
                    : i == 4u ? Scalar::capture(commands[i].kind, Scalar::Dispatch{1u, 1u, 1u})
                              : Scalar::capture(commands[i].kind, Scalar::None{});
                assert(value);
                return {*value, {}};
            };
            auto fields = std::array<rhi::CommandVariableArguments::Input, 10u>{{field(0u),
                field(1u),
                field(2u),
                field(3u),
                field(4u),
                field(5u),
                field(6u),
                field(7u),
                field(8u),
                field(9u)}};
            if (finitePlan && !wholePlan) {
                fields[3].offsets = &displacement;
                fields[3].offsetCount = 1u;
            }
            auto arguments = rhi::CommandVariableArguments::prepareOriginal(fields.data(),
                fields.size(),
                {1u, true, 10u, finitePlan && !wholePlan ? 1u : 0u, 0u, 0u, bytes},
                inputQuota,
                id);
            require(id);
            assert(arguments);
            auto scalars =
                rhi::OriginalCommandScalars::prepareOriginal(usage, arguments, inputQuota, id);
            require(id);
            assert(scalars);
            std::array<rhi::ResourceMemoryBarrierDesc, 3u> dependencies;
            for (std::size_t i = 0u; i < dependencies.size(); ++i) {
                auto& memory = dependencies[i];
                memory.resource = Sources::resource(sources->sources()[i == 0u ? 0u
                        : i == 1u                                              ? 1u
                                                                               : 3u]);
                memory.sourceStages = memory.destinationStages = rhi::PipelineStage::ComputeShader;
                memory.sourceAccess = memory.destinationAccess =
                    i == 1u ? rhi::ResourceAccess::ShaderWrite : rhi::ResourceAccess::ShaderRead;
                // Memory barriers cover the complete resource by the existing
                // L2 contract; texture transition barriers own explicit ranges.
            }
            rhi::ResourceBarrierDesc sampledTransition;
            sampledTransition.resource = parent->resource();
            sampledTransition.before = rhi::ResourceState::CopyDst;
            sampledTransition.after = rhi::ResourceState::ShaderRead;
            sampledTransition.sourceStages = rhi::PipelineStage::Transfer;
            sampledTransition.sourceAccess = rhi::ResourceAccess::TransferWrite;
            sampledTransition.destinationStages = rhi::PipelineStage::ComputeShader;
            sampledTransition.destinationAccess = rhi::ResourceAccess::ShaderRead;
            sampledTransition.textureSubresources = {0u, 2u, 0u, 1u, rhi::TextureAspect::Color};
            const std::array<Inputs::OriginalDescriptor, 6u> records{
                {{0u,
                     *rhi::ResourceDependencyArguments::capture(
                         K::TextureBarrier, sampledTransition)},
                    {2u,
                        rhi::OriginalCommandPipelineBinding{K::BindComputePipeline,
                            6u,
                            1u,
                            pipeline->originalResourceRegistration().generation()}},
                    {3u,
                        rhi::OriginalCommandDescriptorBinding{K::BindComputeDescriptor,
                            7u,
                            6u,
                            1u,
                            descriptor->originalObject()
                                ->originalResourceRegistration()
                                .generation(),
                            descriptor->frozenContentRevision()}},
                    {6u,
                        *rhi::ResourceDependencyArguments::capture(
                            K::BufferMemoryBarrier, dependencies[0])},
                    {7u,
                        *rhi::ResourceDependencyArguments::capture(
                            K::TextureMemoryBarrier, dependencies[1])},
                    {8u,
                        *rhi::ResourceDependencyArguments::capture(
                            K::TextureMemoryBarrier, dependencies[2])}}};
            // Ordinary sets use Inputs3; Inputs4 independently requires tables.
            assert(rhi::OriginalCommandPipelineBindingContract::validateBudget({4u, true, 1u, 1u}));
            auto inputs = Inputs::prepareOriginal(scalars,
                records.data(),
                records.size(),
                {finitePlan ? 6u : 3u, true, 1u, 1u, 0u, finitePlan && !wholePlan ? 1u : 0u},
                inputQuota,
                id);
            require(id);
            assert(inputs);
            std::uint32_t owners = 0u, registrations = 0u;
            require(Sources::registeredOwnerUnion(sources,
                inputs->retainedOwnerCount() + 1u,
                authored.maxGraphOwners,
                authored.maxGraphOwners,
                owners,
                registrations));
            std::uint32_t shortOwners = 0u, shortRegistrations = 0u;
            const auto* ownerRefusal = Sources::registeredOwnerUnion(sources,
                inputs->retainedOwnerCount() + 1u,
                owners - 1u,
                registrations,
                shortOwners,
                shortRegistrations);
            assert(ownerRefusal
                && core::StringView(ownerRefusal) == "RHI-ORIGINAL-STATE-OWNER-CAPACITY");
            std::printf(
                "BINDLESS_RHI_GENERAL_ORIGINAL_OWNER_INVENTORY_OK sources%u associated%u cpu%u registrations%u state%u ceiling%u one-less-refused\n",
                sourceCount,
                associatedRegistrations,
                inputs->retainedOwnerCount() + 1u,
                registrations,
                owners,
                authored.maxGraphOwners);
            rhi::OriginalCommandStateAdmissionBudget state{3u,
                true,
                1u,
                registrations,
                6u,
                5u,
                owners,
                bytes,
                {2u, true, 0u, 1u, false, true, 1u, 4u, 1u, true, 3u},
                {2u, true, 0u, 1u, false, true, 10u, 1u, 4u, 1u, true, 3u}};
            auto legacy = state;
            assert(!rhi::OriginalCommandResourceStates::prepareOriginal(
                inputs, *c.device, legacy, stateQuota, id));
            assert(id && !stateQuota->liveBytes());
            state.descriptorConsumption.generalShaderResourcesVersion = 1u;
            state.descriptorConsumption.generalShaderResourcesOptIn = true;
            state.renderDescriptorUsage.generalShaderResourcesVersion = 1u;
            state.renderDescriptorUsage.generalShaderResourcesOptIn = true;
            if (finitePlan) {
                state.contractVersion = 5u;
                state.descriptorConsumption = OriginalGeneralFiniteConsumptionFixture::budget();
                state.renderDescriptorUsage =
                    OriginalGeneralFiniteConsumptionFixture::renderBudget(10u);
            }
            // Public cold walkers and captured State walkers consume one profile.
            const auto consumed = rhi::common::OriginalCommandDescriptorConsumption::inspect(
                inputs, *c.device, state.descriptorConsumption);
            require(consumed.diagnostic);
            assert(consumed.consumptions == 1u && consumed.logicalAccesses == 4u
                && consumed.textureAccesses == 2u && consumed.samplerReferences == 1u);
            std::size_t samplerReferences = 0u;
            const auto visited = rhi::common::OriginalCommandDescriptorConsumption::visit(inputs,
                *c.device,
                state.descriptorConsumption,
                [&](const auto& access) noexcept -> const char* {
                    if (access.kind
                        == rhi::common::OriginalCommandDescriptorConsumption::Access::Kind::
                            Sampler) {
                        ++samplerReferences;
                        assert(access.samplerSourceIndex == 5u
                            && access.bufferSourceIndex == SIZE_MAX
                            && access.textureViewSourceIndex == SIZE_MAX
                            && access.state == rhi::ResourceState::Undefined
                            && access.access == rhi::ResourceAccess::None
                            && access.stages == rhi::PipelineStage::ComputeShader);
                    }
                    return nullptr;
                });
            require(visited.diagnostic);
            assert(samplerReferences == 1u);
            auto shortConsumption = state.descriptorConsumption;
            --shortConsumption.maxLogicalAccesses;
            std::size_t leaked = 0u;
            assert(rhi::common::OriginalCommandDescriptorConsumption::visit(inputs,
                       *c.device,
                       shortConsumption,
                       [&](const auto&) noexcept -> const char* {
                           ++leaked;
                           return nullptr;
                       })
                       .diagnostic
                && !leaked);
            const auto rendered =
                rhi::common::OriginalCommandDescriptorConsumption::inspectRenderBindings(
                    inputs, *c.device, state.renderDescriptorUsage);
            require(rendered.diagnostic);
            assert(!rendered.bindings && !rendered.logicalAccesses && !rendered.textureAccesses);
            assert(rhi::common::OriginalCommandDescriptorConsumption::inspect(
                inputs, *c.device, legacy.descriptorConsumption)
                    .diagnostic);
            assert(rhi::common::OriginalCommandDescriptorConsumption::inspectRenderBindings(
                inputs, *c.device, legacy.renderDescriptorUsage)
                    .diagnostic);
            auto graph = rhi::OriginalCommandResourceStates::prepareOriginal(
                inputs, *c.device, state, stateQuota, id);
            require(id);
            assert(graph);
            rhi::OriginalCommandStreamBudget stream;
            stream.contractVersion = finitePlan ? 5u : 3u;
            stream.prototypeOptIn = true;
            stream.maxCommands = 10u;
            stream.maxDescriptors = stream.maxReferences = 6u;
            stream.maxRetainedOwners = graph->retainedOwnerCount() + 1u;
            stream.maxCpuMetadataBytes = bytes;
            stream.maxPipelineBinds = stream.maxDescriptorBinds = stream.maxConsumptions = 1u;
            stream.maxImplicitAccesses = 4u;
            stream.stateSequence = {finitePlan ? 5u : 3u, true, 10u, 0u, 9u, 10u, 4u};
            stream.descriptorConsumption = state.descriptorConsumption;
            stream.renderDescriptorUsage = state.renderDescriptorUsage;
            const auto sequence =
                rhi::common::OriginalCommandStateSequence::inspect(graph, stream.stateSequence);
            require(sequence.diagnostic);
            assert(sequence.steps == 10u);
            auto shortStream = stream;
            --shortStream.maxImplicitAccesses;
            assert(!rhi::OriginalCommandStreamProgram::prepareOriginalTyped(
                       graph, shortStream, streamQuota, id)
                && id && !streamQuota->liveBytes());
            auto program = rhi::OriginalCommandStreamProgram::prepareOriginalTyped(
                graph, stream, streamQuota, id);
            require(id);
            assert(program);
            if (finitePlan) {
                rhi::DescriptorBufferBindingRangeContract::Resolved resolved;
                require(Initialized::validateGeneralFiniteInitializedContent(*c.device,
                    {1u, true, Initialized::initializedIndexOwnerBound, extent, extent},
                    initialized,
                    descriptor,
                    0u,
                    initialBytes.data(),
                    initialBytes.size(),
                    1u,
                    resolved));
                assert(resolved.offsetBytes == bufferOffset && resolved.sizeBytes == 16u);
                auto recording = core::makeShared<core::CpuMemoryQuota>(bytes);
                auto root = OriginalGeneralFiniteContentPlanFixture::verifyActual(
                    graph, *c.device, state, stream.stateSequence, recording);
                const auto captured = parent->originalObject()->observeOriginalCommittedState(
                    sampledTransition.textureSubresources);
                assert(captured);
                if (finiteClosure) {
                    using Closure = rhi_webgpu::WebGpuCanonicalNativeClosure;
                    const auto loss = c.device->observeOriginalDeviceNativeLoss(id);
                    require(id);
                    assert(loss);
                    const Closure::ShaderTextureContentInput textureInput{4u,
                        uploadRoot,
                        uploadIssuer,
                        textureContent,
                        *completedTexture,
                        rhi::ResourceState::ShaderRead,
                        rhi::PipelineStage::ComputeShader,
                        rhi::ResourceAccess::ShaderRead};
                    const auto textureOwners = uploadIssuer->retainedOwnerCount()
                        + TextureContent::retainedOwnerBound + 1u;
                    const auto ownerCeiling = root->retainedOwnerCount() + 3u
                        + sourceCount * (1u + rhi::OriginalWholeDescriptorWriteContract::quotaCount)
                        + Initialized::initializedIndexOwnerBound + textureOwners
                        + rhi::OriginalTypedIssuerPreparationContract::additionalOwners
                        + (finiteRecording
                                ? OriginalTypedColdAdmissionFixture::executionOwnerDemand(7u)
                                : 0u)
                        + (finiteExecution
                                ? rhi::OriginalGeneralOutputSnapshotContract::additionalOwnerCount
                                : 0u);
                    const auto retainedBytes =
                        uniform->originalObject()->allocation().estimatedSizeBytes
                        + std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(
                            storage[0])
                              ->originalObject()
                              ->allocation()
                              .estimatedSizeBytes
                        + parent->originalObject()->allocation().estimatedSizeBytes
                        + sampledUpload.buffer->originalLease()
                              ->originalObject()
                              ->allocation()
                              .estimatedSizeBytes;
                    const auto rootBytes = recording->liveBytes();
                    auto closure = OriginalGeneralFiniteNativeClosureFixture::verifyActual(root,
                        program,
                        *c.device,
                        *loss,
                        initialized,
                        textureInput,
                        retainedBytes,
                        ownerCeiling);
                    std::weak_ptr<const Initialized> retainedInitializer = initialized;
                    if (finitePayload) {
                        auto payload =
                            OriginalGeneralFiniteNativeProjectionFixture::verifyActual(closure,
                                std::array{recording, source, inputQuota, stateQuota, streamQuota},
                                ownerCeiling);
                        std::weak_ptr<const Closure> retainedClosure = closure;
                        if (finiteRecording) {
                            const auto freezeRequest =
                                payload->originalDecision()->originalRequest();
                            const auto payloadRequest = payload->originalRequest();
                            const auto rebuildPayload = [&] {
                                payload.reset();
                                closure.reset();
                                root.reset();
                                assert(!sources->originalStateTransactionCount());
                                auto rootState = state;
                                rootState.maxRetainedOwners = graph->retainedOwnerCount() + 2u;
                                root =
                                    rhi::OriginalCommandStateTransaction::prepareOriginalOwnership(
                                        graph,
                                        *c.device,
                                        rootState,
                                        stream.stateSequence,
                                        OriginalGeneralFiniteContentPlanFixture::budget(),
                                        recording,
                                        id);
                                require(id);
                                assert(root);
                                const auto closureBudget =
                                    OriginalGeneralFiniteNativeClosureFixture::budgetFor(root,
                                        initialized,
                                        textureInput,
                                        retainedBytes,
                                        ownerCeiling);
                                const std::array initializedInputs{
                                    Closure::ShaderBufferContentInput{0u, initialized}};
                                const std::array textureInputs{textureInput};
                                closure = Closure::prepareOriginalGeneralResourcesLossBound(root,
                                    program,
                                    *c.device,
                                    closureBudget,
                                    *loss,
                                    id,
                                    initializedInputs.data(),
                                    initializedInputs.size(),
                                    textureInputs.data(),
                                    textureInputs.size());
                                require(id);
                                assert(closure);
                                auto frozen = rhi_webgpu::WebGpuOriginalTypedDependencyDecision::
                                    prepareOriginal(closure, freezeRequest, id);
                                require(id);
                                assert(frozen);
                                payload =
                                    rhi_webgpu::WebGpuOriginalTypedNativePayload::prepareOriginal(
                                        frozen, payloadRequest, id);
                                require(id);
                                assert(payload);
                            };
                            const auto dropCaller = [&] {
                                initialized.reset();
                                closure.reset();
                                payload.reset();
                                root.reset();
                            };
                            // Quota-owned weak control blocks must not survive repeated
                            // original candidates during exact admission probes.
                            retainedClosure.reset();
                            if (finiteExecution) {
                                const auto expected =
                                    OriginalGeneralFiniteExecutionFixture::expectedOutput(
                                        bufferType,
                                        initialBytes,
                                        bufferOffset,
                                        sampledUpload.binding->originalInput()->pixelBytes(),
                                        sampler->originalObject()->desc());
                                const auto output = std::get<
                                    std::shared_ptr<const rhi::OriginalCommandTextureLease>>(
                                    storage[0]);
                                std::shared_ptr<
                                    const rhi_webgpu::WebGpuOriginalGeneralOutputSnapshot>
                                    snapshot;
                                const auto completion =
                                    [&](const auto& issuer, const auto& liveRoot, auto stage) {
                                        OriginalGeneralFiniteExecutionFixture::verifyStage(snapshot,
                                            issuer,
                                            liveRoot,
                                            output,
                                            payload,
                                            *c.device,
                                            resource,
                                            expected,
                                            stage);
                                    };
                                if (finiteClaim) {
                                    const auto claimDriver = [&](auto& issuer,
                                                                 auto& custody,
                                                                 const auto& liveRoot,
                                                                 const auto& execution,
                                                                 const auto& recordingQuota,
                                                                 const auto& submissionQuota,
                                                                 const auto& callbackQuota,
                                                                 auto nativeDevice,
                                                                 const auto& lifecycle) {
                                        assert(snapshot
                                            && rhi_webgpu::WebGpuOriginalGeneralOutputSnapshot::
                                                validateCompletedOriginal(snapshot));
                                        snapshot
                                            .reset(); // Drop must release the real issuer facade.
                                        return FiniteClaim::verifyActual(finiteClaimTerminal,
                                            c.device,
                                            nativeDevice,
                                            issuer,
                                            custody,
                                            liveRoot,
                                            execution,
                                            lifecycle,
                                            recordingQuota,
                                            submissionQuota,
                                            callbackQuota);
                                    };
                                    root =
                                        OriginalGeneralFiniteNativeRecordingFixture::verifyActual(
                                            payload,
                                            root,
                                            *c.device,
                                            sampler,
                                            retainedInitializer,
                                            *parent->originalObject(),
                                            *completedTexture,
                                            rebuildPayload,
                                            dropCaller,
                                            completion,
                                            nullptr,
                                            nullptr,
                                            claimDriver);
                                } else if (finitePhysical) {
                                    const auto preparedDriver = [&](const auto& issuer,
                                                                    const auto& custody,
                                                                    const auto& liveRoot,
                                                                    const auto& execution,
                                                                    const auto& recordingQuota,
                                                                    const auto& submissionQuota,
                                                                    const auto& callbackQuota,
                                                                    auto nativeDevice) {
                                        assert(snapshot
                                            && rhi_webgpu::WebGpuOriginalGeneralOutputSnapshot::
                                                validateCompletedOriginal(snapshot));
                                        FiniteLoss::verifyActual(*finitePhysicalTerminal,
                                            c.device,
                                            nativeDevice,
                                            issuer,
                                            custody,
                                            liveRoot,
                                            execution,
                                            recordingQuota,
                                            submissionQuota,
                                            callbackQuota);
                                        assert(!issuer->hasOriginalCompletedSubmission()
                                            && rhi_webgpu::WebGpuOriginalGeneralOutputSnapshot::
                                                validateCompletedOriginal(snapshot));
                                        snapshot.reset();
                                    };
                                    root =
                                        OriginalGeneralFiniteNativeRecordingFixture::verifyActual(
                                            payload,
                                            root,
                                            *c.device,
                                            sampler,
                                            retainedInitializer,
                                            *parent->originalObject(),
                                            *completedTexture,
                                            rebuildPayload,
                                            dropCaller,
                                            completion,
                                            nullptr,
                                            preparedDriver);
                                } else if (finiteSubmission) {
                                    const auto terminalDriver = [&](const auto& issuer,
                                                                    const auto& custody,
                                                                    const auto& liveRoot,
                                                                    const auto& execution,
                                                                    const auto& recordingQuota,
                                                                    const auto& submissionQuota,
                                                                    const auto& callbackQuota,
                                                                    auto nativeDevice) {
                                        FiniteSubmission::verifyActual(finiteSubmissionTerminal,
                                            c.device,
                                            nativeDevice,
                                            issuer,
                                            custody,
                                            liveRoot,
                                            execution,
                                            recordingQuota,
                                            submissionQuota,
                                            callbackQuota,
                                            *completedTexture,
                                            [&](bool drained) {
                                                assert(snapshot);
                                                if (!drained) {
                                                    assert(!issuer->hasOriginalCompletedSubmission()
                                                        && rhi_webgpu::
                                                            WebGpuOriginalGeneralOutputSnapshot::
                                                                validateCompletedOriginal(
                                                                    snapshot));
                                                } else if (FiniteSubmission::invalidates(
                                                               finiteSubmissionTerminal)) {
                                                    assert(liveRoot->status()
                                                            == rhi::
                                                                OriginalCommandStateTransaction::
                                                                    Status::Invalidated
                                                        && !issuer->hasOriginalCompletedSubmission()
                                                        && rhi_webgpu::
                                                            WebGpuOriginalGeneralOutputSnapshot::
                                                                validateCompletedOriginal(
                                                                    snapshot));
                                                    snapshot.reset();
                                                } else {
                                                    OriginalGeneralFiniteExecutionFixture::verifyStage(
                                                        snapshot,
                                                        issuer,
                                                        liveRoot,
                                                        output,
                                                        payload,
                                                        *c.device,
                                                        resource,
                                                        expected,
                                                        OriginalGeneralFiniteNativeRecordingFixture::
                                                            CompletionStage::Committed);
                                                }
                                            });
                                    };
                                    root =
                                        OriginalGeneralFiniteNativeRecordingFixture::verifyActual(
                                            payload,
                                            root,
                                            *c.device,
                                            sampler,
                                            retainedInitializer,
                                            *parent->originalObject(),
                                            *completedTexture,
                                            rebuildPayload,
                                            dropCaller,
                                            completion,
                                            terminalDriver);
                                } else
                                    root =
                                        OriginalGeneralFiniteNativeRecordingFixture::verifyActual(
                                            payload,
                                            root,
                                            *c.device,
                                            sampler,
                                            retainedInitializer,
                                            *parent->originalObject(),
                                            *completedTexture,
                                            rebuildPayload,
                                            dropCaller,
                                            completion);
                                assert(!snapshot);
                            } else {
                                root = OriginalGeneralFiniteNativeRecordingFixture::verifyActual(
                                    payload,
                                    root,
                                    *c.device,
                                    sampler,
                                    retainedInitializer,
                                    *parent->originalObject(),
                                    *completedTexture,
                                    rebuildPayload,
                                    dropCaller);
                            }
                            assert(retainedInitializer.expired());
                            retainedInitializer.reset();
                            assert(recording->liveBytes() == rootBytes);
                            if (finiteClaim)
                                std::printf(
                                    "BINDLESS_RHI_GENERAL_FINITE_CLAIM4_RETENTION_OK %s %s caller-payload-root-closure-initializer-dropped claim-terminal-drained recording-quota-root-only public-closed\n",
                                    readOnly ? "readonly-storage" : "uniform",
                                    wholePlan         ? "whole"
                                        : dynamicPlan ? "dynamic"
                                                      : "static");
                            else if (finiteMap)
                                std::printf(
                                    "BINDLESS_RHI_GENERAL_FINITE_MAP4_RETENTION_OK %s %s caller-payload-root-closure-initializer-dropped original-upstream-and-output-drained recording-quota-root-only public-closed\n",
                                    readOnly ? "readonly-storage" : "uniform",
                                    wholePlan         ? "whole"
                                        : dynamicPlan ? "dynamic"
                                                      : "static");
                            else if (finitePhysical)
                                std::printf(
                                    "BINDLESS_RHI_GENERAL_FINITE_LOSS4_RETENTION_OK %s %s caller-payload-root-closure-initializer-dropped actual-loss-drained recording-quota-root-only public-closed\n",
                                    readOnly ? "readonly-storage" : "uniform",
                                    wholePlan         ? "whole"
                                        : dynamicPlan ? "dynamic"
                                                      : "static");
                            else if (finiteSubmission)
                                std::printf(
                                    "BINDLESS_RHI_GENERAL_FINITE_SUBMISSION4_RETENTION_OK %s %s caller-payload-root-closure-initializer-dropped terminal-drained recording-quota-root-only public-closed\n",
                                    readOnly ? "readonly-storage" : "uniform",
                                    wholePlan         ? "whole"
                                        : dynamicPlan ? "dynamic"
                                                      : "static");
                            else if (finiteLifecycle)
                                std::printf(
                                    "BINDLESS_RHI_GENERAL_FINITE_RECORDING4_RETENTION_OK %s %s caller-payload-root-closure-initializer-dropped terminal-drained recording-quota-root-only public-closed\n",
                                    readOnly ? "readonly-storage" : "uniform",
                                    wholePlan         ? "whole"
                                        : dynamicPlan ? "dynamic"
                                                      : "static");
                            else if (finiteExecution)
                                std::printf(
                                    "BINDLESS_RHI_GENERAL_FINITE_EXECUTION7_RETENTION_OK %s %s caller-payload-root-closure-initializer-dropped original-submitted-output-mapped recording-quota-root-only public-closed\n",
                                    readOnly ? "readonly-storage" : "uniform",
                                    wholePlan         ? "whole"
                                        : dynamicPlan ? "dynamic"
                                                      : "static");
                            else
                                std::printf(
                                    "BINDLESS_RHI_GENERAL_NATIVE_RECORDING7_RETENTION_OK %s %s caller-payload-root-closure-initializer-dropped original-recorded-cancelled recording-quota-root-only public-submission-closed\n",
                                    readOnly ? "readonly-storage" : "uniform",
                                    wholePlan         ? "whole"
                                        : dynamicPlan ? "dynamic"
                                                      : "static");
                        } else {
                            initialized.reset();
                            closure.reset();
                            assert(!retainedInitializer.expired() && !retainedClosure.expired());
                            OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                                require(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                                    std::array{
                                        recording, source, inputQuota, stateQuota, streamQuota},
                                    [&]() noexcept -> const char* {
                                        return payload->validate();
                                    }));
                            });
                            payload.reset();
                            assert(retainedInitializer.expired() && retainedClosure.expired());
                            // Weak observers keep quota-owned control blocks alive.
                            // Prove object retirement first, then release observers
                            // before checking that only the original root is charged.
                            retainedClosure.reset();
                            retainedInitializer.reset();
                            assert(recording->liveBytes() == rootBytes);
                            std::printf(
                                "BINDLESS_RHI_GENERAL_FROZEN7_RETENTION_OK %s %s caller-closure-initializer-dropped original-freeze-entry-owned full-backing-charged hot0-query0 recording-quota-root-only native-closed\n",
                                readOnly ? "readonly-storage" : "uniform",
                                wholePlan         ? "whole"
                                    : dynamicPlan ? "dynamic"
                                                  : "static");
                        }
                    } else {
                        initialized.reset();
                        assert(!retainedInitializer.expired());
                        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                            require(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                                std::array{recording, source, inputQuota, stateQuota, streamQuota},
                                [&]() noexcept -> const char* {
                                    return closure->validate();
                                }));
                        });
                        closure.reset();
                        assert(
                            retainedInitializer.expired() && recording->liveBytes() == rootBytes);
                        std::printf(
                            "BINDLESS_RHI_GENERAL_CLOSURE9_RETENTION_OK %s %s caller-initializer-dropped original-entry-owned full-backing-charged hot0-query0 recording-quota-root-only native-closed\n",
                            readOnly ? "readonly-storage" : "uniform",
                            wholePlan         ? "whole"
                                : dynamicPlan ? "dynamic"
                                              : "static");
                    }
                }
                const auto contentGeneration = completedTexture->contentGeneration(),
                           contentRevision = completedTexture->revision();
                if (finiteRecording)
                    assert(root->status()
                            == (finiteInvalidates
                                    ? rhi::OriginalCommandStateTransaction::Status::Invalidated
                                    : finiteClaim
                                    ? rhi::OriginalCommandStateTransaction::Status::Abandoned
                                    : finiteExecution
                                    ? rhi::OriginalCommandStateTransaction::Status::Committed
                                    : rhi::OriginalCommandStateTransaction::Status::Abandoned)
                        && root->journalEmpty());
                else {
                    OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                        require(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                            std::array{recording, source, inputQuota, stateQuota, streamQuota},
                            [&]() noexcept -> const char* {
                                if (const auto* failure = root->applyOriginalPrototypePlan())
                                    return failure;
                                assert(root->status()
                                        == rhi::OriginalCommandStateTransaction::Status::Pending
                                    && !root->journalEmpty());
                                assert(!parent->originalObject()->observeOriginalCommittedState(
                                    sampledTransition.textureSubresources));
                                require(completedTexture->diagnostic());
                                assert(root->commitOriginalPendingOwnership());
                                return root->abandonOriginalPendingOwnership();
                            }));
                    });
                }
                assert(root->journalEmpty());
                root.reset();
                assert(!recording->liveBytes() && !sources->originalStateTransactionCount());
                if (finitePostSuppression)
                    std::printf(
                        "BINDLESS_RHI_GENERAL_POST_SUPPRESSION4_RETIRE_OK %s %s %s caller-payload-root-closure-initializer-dropped original-records-reservations-counts-quotas-zero no-publication public-closed\n",
                        readOnly ? "readonly-storage" : "uniform",
                        wholePlan         ? "whole"
                            : dynamicPlan ? "dynamic"
                                          : "static",
                        finiteMap ? "publication" : "commit");
                if (finiteClaim && (finiteClaimPhysical || finiteClaimLogical)) {
                    assert(!parent->originalObject()->observeOriginalCommittedState(
                               sampledTransition.textureSubresources)
                        && !parent->originalObject()->observeOriginalContent()
                        && completedTexture->diagnostic());
                    std::printf(
                        "BINDLESS_RHI_GENERAL_FINITE_CLAIM4_CONTENT_OK %s %s %s journal-count-quota-zero no-recording-no-output public-closed\n",
                        readOnly ? "readonly-storage" : "uniform",
                        wholePlan         ? "whole"
                            : dynamicPlan ? "dynamic"
                                          : "static",
                        finiteClaimPhysical
                            ? "actual-device-loss state-content-invalidated"
                            : "logical-device-terminal observations-refused physical-loss-undelivered");
                } else if (finiteMapPhysical) {
                    assert(!parent->originalObject()->observeOriginalCommittedState(
                               sampledTransition.textureSubresources)
                        && !parent->originalObject()->observeOriginalContent()
                        && completedTexture->diagnostic());
                    std::printf(
                        "BINDLESS_RHI_GENERAL_FINITE_MAP4_UPSTREAM_LOSS_OK %s %s %s original-upstream-committed physical-controls-invalidated journal-count-quota-zero published-bytes-policy-checked public-closed\n",
                        readOnly ? "readonly-storage" : "uniform",
                        wholePlan         ? "whole"
                            : dynamicPlan ? "dynamic"
                                          : "static",
                        FiniteReadback::mapTerminalName(finiteMapTerminal));
                } else if (finiteInvalidates) {
                    assert(!parent->originalObject()->observeOriginalCommittedState(
                               sampledTransition.textureSubresources)
                        && !parent->originalObject()->observeOriginalContent()
                        && completedTexture->diagnostic());
                    std::printf(
                        "BINDLESS_RHI_GENERAL_CONTENT_PLAN5_INVALIDATED_OK %s %s %s original-upload-root-issuer state-content-invalidated journal-count-quota-zero no-rollback no-output public-closed\n",
                        readOnly ? "readonly-storage" : "uniform",
                        wholePlan         ? "whole"
                            : dynamicPlan ? "dynamic"
                                          : "static",
                        finitePhysical ? FiniteLoss::terminalName(*finitePhysicalTerminal)
                                       : FiniteSubmission::terminalName(finiteSubmissionTerminal));
                } else {
                    const auto after = parent->originalObject()->observeOriginalCommittedState(
                        sampledTransition.textureSubresources);
                    assert(after);
                    assert(after->state()
                        == (finiteExecution && !finiteClaim ? rhi::ResourceState::ShaderRead
                                                            : rhi::ResourceState::CopyDst));
                    assert(finiteClaim ? after->revision() == captured->revision()
                                       : after->revision() > captured->revision());
                    const auto contentAfter = parent->originalObject()->observeOriginalContent();
                    assert(contentAfter);
                    assert(contentAfter->contentGeneration() == contentGeneration
                        && contentAfter->revision() == contentRevision);
                    require(Issuer::validateOriginalCompletedShaderTextureInput(uploadIssuer,
                        textureContent,
                        *contentAfter,
                        {1u, true, 2u, 2u, 20u, 5u},
                        uploadRoot));
                    if (finiteClaim)
                        std::printf(
                            "BINDLESS_RHI_GENERAL_FINITE_CLAIM4_CONTENT_OK %s %s no-recording-state-revision-unchanged original-upload-content-unchanged journal-count-quota-zero no-output public-closed\n",
                            readOnly ? "readonly-storage" : "uniform",
                            wholePlan         ? "whole"
                                : dynamicPlan ? "dynamic"
                                              : "static");
                    else if (finiteExecution)
                        std::printf(
                            "BINDLESS_RHI_GENERAL_CONTENT_PLAN5_COMMIT_OK %s %s offset%llu size16 extent%llu original-upload-root-issuer pending-state-exclusion content-generation-revision-unchanged journal-count-quota-zero hot0-query0 public-closed\n",
                            readOnly ? "readonly-storage" : "uniform",
                            wholePlan         ? "whole"
                                : dynamicPlan ? "dynamic"
                                              : "static",
                            static_cast<unsigned long long>(bufferOffset),
                            static_cast<unsigned long long>(extent));
                    else
                        std::printf(
                            "BINDLESS_RHI_GENERAL_CONTENT_PLAN5_ROLLBACK_OK %s %s offset%llu size16 extent%llu original-upload-root-issuer pending-state-exclusion content-generation-revision-unchanged journal-count-quota-zero hot0-query0 native-closed\n",
                            readOnly ? "readonly-storage" : "uniform",
                            wholePlan         ? "whole"
                                : dynamicPlan ? "dynamic"
                                              : "static",
                            static_cast<unsigned long long>(bufferOffset),
                            static_cast<unsigned long long>(extent));
                }
            } else {
                P::Budget budget{5u,
                    true,
                    state.descriptorConsumption,
                    state.renderDescriptorUsage,
                    10u,
                    0u,
                    3u,
                    2u,
                    3u};
                const P::StateBoundRequest request{5u, true, budget};
                std::array<S::BufferUse, 1u> buffers;
                std::array<S::TextureUse, 2u> textures;
                std::size_t bc = 0u, tc = 0u;
                OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                    assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                        std::array{source, inputQuota, stateQuota, streamQuota},
                        [&]() noexcept -> const char* {
                            const auto projected = P::visitStateBound(program,
                                graph,
                                request,
                                [&](const P::Point& point) noexcept -> const char* {
                                    if (const auto* b = std::get_if<P::BufferPoint>(&point))
                                        buffers[bc++] = b->use;
                                    else if (const auto* t = std::get_if<P::TexturePoint>(&point))
                                        textures[tc++] = t->use;
                                    else
                                        assert(false);
                                    return nullptr;
                                });
                            assert(!projected.diagnostic && projected.counts.contributions == 3u
                                && bc == 1u && tc == 2u && projected.counts.shaderBuffers == 1u
                                && projected.counts.shaderSamplers == 1u);
                            assert(buffers[0].usage == S::BufferUsage::UniformRead
                                && textures[0].usage == S::TextureUsage::Storage
                                && textures[1].usage == S::TextureUsage::SampledRead);
                            assert(
                                !S::inspect(program->operations(), buffers, {5u, true, 10u, 1u, 0u})
                                    .diagnostic);
                            assert(!S::inspect(
                                program->operations(), textures, {5u, true, 10u, 2u, 1u})
                                    .diagnostic);
                            auto shortBudget = budget;
                            --shortBudget.maxContributions;
                            std::size_t emitted = 0u;
                            const auto refused = P::visitStateBound(program,
                                graph,
                                {5u, true, shortBudget},
                                [&](const auto&) noexcept -> const char* {
                                    ++emitted;
                                    return nullptr;
                                });
                            assert(refused.diagnostic && !emitted && !refused.counts.contributions);
                            shortBudget = budget;
                            --shortBudget.maxTextureCells;
                            assert(P::inspectStateBound(program, graph, {5u, true, shortBudget})
                                    .diagnostic);
                            auto mismatch = stream;
                            mismatch.descriptorConsumption.generalShaderResourcesOptIn = false;
                            assert(!rhi::OriginalCommandStreamProgram::prepareOriginalTyped(
                                graph, mismatch, streamQuota, id));
                            return nullptr;
                        }));
                });
                // Three contributions need four pair-validation walks; each of the
                // five dependency texture cells and the buffer adds two walks.
                const D::Request dependency{5u, true, request, 4u, 16u, 48u, 3u, 6u, 5u};
                std::array<D::Record, 4u> frozen;
                std::size_t emitted = 0u;
                OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                    assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                        std::array{source, inputQuota, stateQuota, streamQuota},
                        [&]() noexcept -> const char* {
                            const auto projected = D::visit(program,
                                graph,
                                dependency,
                                [&](const D::Record& record) noexcept -> const char* {
                                    assert(emitted < frozen.size());
                                    frozen[emitted++] = record;
                                    const auto& endpoint = record.commandIndex == 0u
                                        ? record.following
                                        : record.preceding;
                                    assert(record.clause
                                        == (record.commandIndex == 0u
                                                ? D::Clause::InitialBookkeeping
                                                : D::Clause::FinalBookkeeping));
                                    assert(endpoint
                                        && (record.commandIndex == 0u ? !record.preceding
                                                                      : !record.following));
                                    assert(endpoint->scope.kind == S::Kind::ComputeDispatch
                                        && endpoint->scope.first == 4u);
                                    return nullptr;
                                });
                            require(projected.diagnostic);
                            assert(emitted == 4u && projected.counts.dependencies == 4u
                                && projected.counts.final == 3u && projected.counts.initial == 1u
                                && projected.counts.projectionPasses == 16u
                                && projected.counts.contributionVisits == 48u
                                && projected.counts.pairChecks == 3u
                                && projected.counts.endpointContributions == 6u
                                && !projected.synchronizationProven()
                                && !projected.nativeExecutionProven());
                            assert(frozen[0].texture && !frozen[1].texture && frozen[2].texture
                                && frozen[3].texture);
                            D::Counts frozenCounts;
                            for (const auto& record : frozen)
                                require(D::validateFrozenBinding(
                                    program, graph, dependency, record, frozenCounts));
                            auto changed = frozen[2];
                            changed.preceding->access.stages = rhi::PipelineStage::FragmentShader;
                            D::Counts invalidCounts;
                            assert(D::validateCoverage(changed, 2u)
                                && D::validateFrozenBinding(
                                    program, graph, dependency, changed, invalidCounts));
                            for (const auto member : {&D::Request::maxDependencies,
                                     &D::Request::maxProjectionPasses,
                                     &D::Request::maxContributionVisits,
                                     &D::Request::maxPairChecks,
                                     &D::Request::maxEndpointContributions}) {
                                auto less = dependency;
                                --(less.*member);
                                std::size_t leaked = 0u;
                                const auto refused = D::visit(
                                    program, graph, less, [&](const auto&) noexcept -> const char* {
                                        ++leaked;
                                        return nullptr;
                                    });
                                assert(
                                    refused.diagnostic && !leaked && !refused.counts.dependencies);
                            }
                            auto legacy = dependency;
                            legacy.contractVersion = 4u;
                            assert(D::inspect(program, graph, legacy).diagnostic);
                            assert(rhi::OriginalTypedDependencyProjectionContract::
                                    validateFreezeIntent({5u, true, dependency, owners, bytes}));
                            return nullptr;
                        }));
                });
                {
                    using Root = rhi::OriginalCommandStateTransaction;
                    using Closure = rhi_webgpu::WebGpuCanonicalNativeClosure;
                    auto recording = core::makeShared<core::CpuMemoryQuota>(bytes);
                    auto rootBudget = state;
                    rootBudget.maxRetainedOwners = graph->retainedOwnerCount() + 2u;
                    auto root =
                        Root::prepareOriginalOwnership(graph, *c.device, rootBudget, recording, id);
                    require(id);
                    assert(root);
                    const auto textureObservations = graph->textureEntries().size();
                    rhi::OriginalTextureContentPlanBudget readPlan{4u,
                        true,
                        1u,
                        0u,
                        0u,
                        1u,
                        commands.size(),
                        textureObservations * (textureObservations + 2u)};
                    const auto readPreflight =
                        root->preflightOriginalCompletedContentReads(readPlan);
                    require(readPreflight.diagnostic);
                    assert(readPreflight.controls == 1u && readPreflight.steps == 1u
                        && readPreflight.cells == 0u);
                    readPlan.maxProjectionVisits = readPreflight.projectionVisits;
                    root.reset();
                    auto shortRootBudget = rootBudget;
                    --shortRootBudget.maxRetainedOwners;
                    auto shortRoot = Root::prepareOriginalOwnership(
                        graph, *c.device, shortRootBudget, recording, id);
                    require(id);
                    assert(shortRoot);
                    const auto shortRead =
                        shortRoot->preflightOriginalCompletedContentReads(readPlan);
                    assert(shortRead.diagnostic
                        && core::StringView(shortRead.diagnostic)
                            == "RHI-CANONICAL-CONTENT-PLAN-CONTROL-CAPACITY");
                    assert(
                        shortRoot->materializeOriginalPrototypePlan(stream.stateSequence, readPlan)
                        && !shortRoot->hasOriginalPrototypePlan() && shortRoot->journalEmpty());
                    shortRoot.reset();
                    root =
                        Root::prepareOriginalOwnership(graph, *c.device, rootBudget, recording, id);
                    require(id);
                    assert(root);
                    const auto rejectRead = [&](const auto& intent) {
                        assert(root->preflightOriginalCompletedContentReads(intent).diagnostic);
                        assert(
                            root->materializeOriginalPrototypePlan(stream.stateSequence, intent));
                        assert(!root->hasOriginalPrototypePlan() && root->journalEmpty()
                            && !completedTexture->diagnostic());
                    };
                    auto badRead = readPlan;
                    badRead.prototypeOptIn = false;
                    rejectRead(badRead);
                    badRead = readPlan;
                    badRead.contractVersion = 5u;
                    rejectRead(badRead);
                    badRead = readPlan;
                    badRead.maxUploadCommands = 1u;
                    rejectRead(badRead);
                    badRead = readPlan;
                    badRead.maxContentCells = 1u;
                    rejectRead(badRead);
                    badRead = readPlan;
                    badRead.maxReadbackCommands = 1u;
                    rejectRead(badRead);
                    badRead = readPlan;
                    --badRead.maxContentControls;
                    rejectRead(badRead);
                    badRead = readPlan;
                    --badRead.maxContentSteps;
                    rejectRead(badRead);
                    badRead = readPlan;
                    --badRead.maxCommandVisits;
                    rejectRead(badRead);
                    badRead = readPlan;
                    --badRead.maxProjectionVisits;
                    rejectRead(badRead);
                    require(root->materializeOriginalPrototypePlan(stream.stateSequence, readPlan));
                    assert(root->plannedAccessStepCount() == 10u && root->journalEmpty()
                        && root->plannedContentControlCount() == 1u
                        && root->plannedContentUploadCount() == 0u
                        && root->retainedOwnerCount() <= authored.maxGraphOwners
                        && !completedTexture->diagnostic());
                    auto loss = c.device->observeOriginalDeviceNativeLoss(id);
                    require(id);
                    assert(loss);
                    // Independent cold native custody bound: existing root, closure,
                    // loss subscription, Stream and the original per-entry slots.
                    // This does not enlarge Sources' authored CPU owner ceiling48.
                    std::array<Closure::ShaderTextureContentInput, 1u> textureContents{{{4u,
                        uploadRoot,
                        uploadIssuer,
                        textureContent,
                        *completedTexture,
                        rhi::ResourceState::ShaderRead,
                        rhi::PipelineStage::ComputeShader,
                        rhi::ResourceAccess::ShaderRead}}};
                    const auto textureOwnerBound = std::uint64_t{uploadIssuer->retainedOwnerCount()}
                        + TextureContent::retainedOwnerBound + 1u;
                    assert(textureOwnerBound && textureOwnerBound <= UINT32_MAX);
                    const auto textureOwners = static_cast<std::uint32_t>(textureOwnerBound);
                    const auto closureOwnerCeiling = root->retainedOwnerCount() + 3u
                        + sourceCount * (1u + rhi::OriginalWholeDescriptorWriteContract::quotaCount)
                        + Initialized::initializedIndexOwnerBound + textureOwners
                        + rhi::OriginalTypedIssuerPreparationContract::additionalOwners;
                    // Author the existing issuer overhead before this native graph
                    // is published; CPU Sources48 and all byte quotas are unchanged.
                    const auto retainedBytes =
                        uniform->originalObject()->allocation().estimatedSizeBytes
                        + std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(
                            storage[0])
                              ->originalObject()
                              ->allocation()
                              .estimatedSizeBytes
                        + std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(
                            sampled[0])
                              ->originalObject()
                              ->allocation()
                              .estimatedSizeBytes
                        + sampledUpload.buffer->originalLease()
                              ->originalObject()
                              ->allocation()
                              .estimatedSizeBytes;
                    rhi::OriginalCommandNativeClosureBudget custody{8u,
                        true,
                        6u,
                        sourceCount,
                        closureOwnerCeiling,
                        retainedBytes,
                        bytes,
                        1u,
                        1u};
                    custody.generalShaderResourcesVersion = 1u;
                    custody.generalShaderResourcesOptIn = true;
                    custody.shaderBufferContent = {
                        1u, true, 1u, Initialized::initializedIndexOwnerBound, 16u};
                    custody.shaderTextureContent = {1u, true, 1u, textureOwners, 20u, 2u, 2u, 5u};
                    std::array<Closure::ShaderBufferContentInput, 1u> contents{{{0u, initialized}}};
                    const auto make = [&](const auto& b, bool includeTexture = true) {
                        return Closure::prepareOriginalGeneralResourcesLossBound(root,
                            program,
                            *c.device,
                            b,
                            *loss,
                            id,
                            contents.data(),
                            contents.size(),
                            includeTexture ? textureContents.data() : nullptr,
                            includeTexture ? textureContents.size() : 0u);
                    };
                    auto wrong = custody;
                    wrong.generalShaderResourcesOptIn = false;
                    assert(!make(wrong) && id);
                    wrong = custody;
                    wrong.shaderBufferContent = {};
                    assert(!make(wrong) && id);
                    wrong = custody;
                    --wrong.shaderBufferContent.maxSources;
                    assert(!make(wrong) && id);
                    wrong = custody;
                    --wrong.shaderBufferContent.maxRetainedOwners;
                    assert(!make(wrong) && id);
                    wrong = custody;
                    --wrong.shaderBufferContent.maxInitializedBytes;
                    assert(!make(wrong) && id);
                    wrong = custody;
                    wrong.shaderBufferContent.prototypeOptIn = false;
                    assert(!make(wrong) && id);
                    assert(!make(custody, false) && id
                        && core::StringView(id) == "RHI-WEBGPU-GENERAL-TEXTURE-SOURCE-CAPACITY");
                    wrong = custody;
                    wrong.shaderTextureContent = {};
                    assert(!make(wrong) && id);
                    wrong = custody;
                    wrong.shaderTextureContent.prototypeOptIn = false;
                    assert(!make(wrong) && id);
                    wrong = custody;
                    --wrong.shaderTextureContent.maxRetainedOwners;
                    assert(!make(wrong) && id);
                    wrong = custody;
                    --wrong.shaderTextureContent.maxInitializedBytes;
                    assert(!make(wrong) && id);
                    wrong = custody;
                    --wrong.shaderTextureContent.maxUploads;
                    assert(!make(wrong) && id);
                    wrong = custody;
                    --wrong.shaderTextureContent.maxMipLevels;
                    assert(!make(wrong) && id);
                    wrong = custody;
                    --wrong.shaderTextureContent.maxJoinVisits;
                    assert(!make(wrong) && id);
                    textureContents[0].sourceIndex = 2u;
                    assert(!make(custody) && id);
                    textureContents[0].sourceIndex = 4u;
                    textureContents[0].completed.reset();
                    assert(!make(custody) && id);
                    textureContents[0].completed.emplace(*completedTexture);
                    textureContents[0].shaderState = rhi::ResourceState::CopyDst;
                    assert(!make(custody) && id);
                    textureContents[0].shaderState = rhi::ResourceState::ShaderRead;
                    textureContents[0].stages = rhi::PipelineStage::FragmentShader;
                    assert(!make(custody) && id);
                    textureContents[0].stages = rhi::PipelineStage::ComputeShader;
                    textureContents[0].uploadRoot = root;
                    assert(!make(custody) && id);
                    textureContents[0].uploadRoot = uploadRoot;
                    assert(!Closure::prepareOriginalGeneralResourcesLossBound(
                               root, program, *c.device, custody, *loss, id)
                        && id);
                    contents[0].sourceIndex = 1u;
                    assert(!make(custody) && id);
                    contents[0].sourceIndex = 0u;
                    auto originalContent = contents[0].source;
                    contents[0].source = std::shared_ptr<const Initialized>(
                        initializationQuotas[0], initialized.get());
                    assert(!make(custody) && id);
                    contents[0].source = originalContent;
                    originalContent.reset();
                    wrong = custody;
                    wrong.contractVersion = 2u;
                    wrong.generalShaderResourcesVersion = 0u;
                    wrong.generalShaderResourcesOptIn = false;
                    assert(!Closure::prepareOriginalTypedLossBound(
                               root, program, *c.device, wrong, *loss, id)
                        && id);
                    auto closure = make(custody);
                    require(id);
                    assert(closure);
                    assert(closure->resourceBindingCount() == 6u
                        && closure->pipelineBindingCount() == 1u
                        && closure->descriptorBindingCount() == 1u
                        && closure->nativePayloadCount() == sourceCount
                        && closure->retainedResourceBytes() == retainedBytes
                        && !closure->nativeExecutionProven());
                    custody.maxRetainedOwners = closure->retainedOwnerCount();
                    for (const auto member :
                        {&rhi::OriginalCommandNativeClosureBudget::maxResourceBindings,
                            &rhi::OriginalCommandNativeClosureBudget::maxNativePayloadObjects,
                            &rhi::OriginalCommandNativeClosureBudget::maxRetainedOwners}) {
                        wrong = custody;
                        --(wrong.*member);
                        assert(!make(wrong) && id);
                    }
                    wrong = custody;
                    --wrong.maxRetainedResourceBytes;
                    assert(!make(wrong) && id);
                    wrong = custody;
                    --wrong.maxCpuMetadataBytes;
                    assert(!make(wrong) && id);
                    {
                        core::CpuMemoryQuotaLease full(recording, bytes - recording->liveBytes());
                        assert(full.charged() && !make(custody) && id);
                    }
                    auto forged = std::shared_ptr<const Closure>(recording, closure.get());
                    assert(Closure::validateOriginal(forged));
                    forged.reset();
                    using Freeze = rhi_webgpu::WebGpuOriginalTypedDependencyDecision;
                    const Freeze::Request freezeBudget{
                        6u, true, dependency, closureOwnerCeiling + 1u, bytes};
                    auto shortFreeze = freezeBudget;
                    shortFreeze.maxRetainedOwners = closure->retainedOwnerCount();
                    assert(!Freeze::prepareOriginal(closure, shortFreeze, id) && id);
                    shortFreeze = freezeBudget;
                    shortFreeze.maxRetainedOwners = closureOwnerCeiling + 2u;
                    assert(!Freeze::prepareOriginal(closure, shortFreeze, id) && id);
                    shortFreeze = freezeBudget;
                    --shortFreeze.maxCpuMetadataBytes;
                    assert(!Freeze::prepareOriginal(closure, shortFreeze, id) && id);
                    for (std::uint32_t old = 1u; old <= 5u; ++old) {
                        shortFreeze = freezeBudget;
                        shortFreeze.contractVersion = old;
                        assert(!Freeze::prepareOriginal(closure, shortFreeze, id) && id);
                    }
                    {
                        core::CpuMemoryQuotaLease full(recording, bytes - recording->liveBytes());
                        assert(full.charged() && !Freeze::prepareOriginal(closure, freezeBudget, id)
                            && id);
                    }
                    auto frozenDecision = Freeze::prepareOriginal(closure, freezeBudget, id);
                    require(id);
                    assert(frozenDecision);
                    assert(frozenDecision->counts().dependencies == 4u
                        && frozenDecision->counts().final == 3u
                        && frozenDecision->counts().initial == 1u
                        && frozenDecision->records().size() == 4u
                        && !frozenDecision->synchronizationProven()
                        && !frozenDecision->nativeExecutionProven());
                    auto forgedFreeze =
                        std::shared_ptr<const Freeze>(recording, frozenDecision.get());
                    assert(Freeze::validateOriginal(forgedFreeze));
                    forgedFreeze.reset();
                    auto peer = make(custody);
                    require(id);
                    assert(peer);
                    assert(frozenDecision->compareOriginalClosure(peer));
                    peer.reset();
                    using Payload = rhi_webgpu::WebGpuOriginalTypedNativePayload;
                    auto payloadBudget =
                        OriginalTypedNativePayloadFixture::requestFor(frozenDecision, 6u);
                    payloadBudget.maxRetainedOwners = freezeBudget.maxRetainedOwners + 1u;
                    assert(payloadBudget.maxWork.commands == 10u
                        && payloadBudget.maxWork.computePasses == 1u
                        && payloadBudget.maxWork.pipelineBinds == 1u
                        && payloadBudget.maxWork.descriptorBinds == 1u
                        && payloadBudget.maxWork.dispatches == 1u
                        && payloadBudget.maxWork.dependencies == 4u
                        && payloadBudget.maxWork.implicitAccesses == 4u);
                    for (std::uint32_t old = 1u; old <= 5u; ++old) {
                        auto legacy = payloadBudget;
                        legacy.contractVersion = old;
                        assert(!Payload::prepareOriginal(frozenDecision, legacy, id) && id);
                    }
                    auto overPayload = payloadBudget;
                    ++overPayload.maxRetainedOwners;
                    assert(!Payload::prepareOriginal(frozenDecision, overPayload, id) && id);
                    auto payload = OriginalTypedNativePayloadFixture::verify(frozenDecision,
                        std::array{source, inputQuota, stateQuota, streamQuota, recording},
                        "general-original8",
                        6u,
                        payloadBudget.maxRetainedOwners);
                    assert(payload && payload->records().size() == 10u && payload->passes().empty()
                        && !payload->nativeExecutionProven() && !payload->synchronizationProven());
                    // The same initialized original graph now enters independent
                    // Cold6/Lifecycle3 custody with a bounded downstream margin.
                    auto coldRequest = OriginalTypedColdAdmissionFixture::requestFor(payload, 6u);
                    coldRequest.maxRetainedOwners =
                        payloadBudget.maxRetainedOwners + Cold::additionalOwners;
                    const auto coldInspection = Cold::inspectOriginalPayload(payload, coldRequest);
                    require(coldInspection.diagnostic);
                    assert(coldInspection.required.work.matches(coldWork)
                        && coldInspection.required.retainedOwners
                            == payload->retainedOwnerCount() + Cold::additionalOwners
                        && coldInspection.required.capacity.callbackTickets
                            == coldCapacity.capacity.callbackTickets);
                    const auto rejectCold = [&](auto b) {
                        const auto result = Cold::inspectOriginalPayload(payload, b);
                        assert(result.diagnostic && !result.required.contractVersion
                            && !result.required.work.commands);
                    };
                    auto badCold = coldRequest;
                    badCold.maxRetainedOwners = coldInspection.required.retainedOwners - 1u;
                    rejectCold(badCold);
                    badCold = coldRequest;
                    badCold.maxRetainedOwners =
                        payload->originalRequest().maxRetainedOwners + Cold::additionalOwners + 1u;
                    rejectCold(badCold);
                    badCold = coldRequest;
                    --badCold.maxWork.nativeCalls;
                    rejectCold(badCold);
                    badCold = coldRequest;
                    --badCold.maxCallbacks.recordingScopes;
                    rejectCold(badCold);
                    badCold = coldRequest;
                    badCold.prototypeOptIn = false;
                    rejectCold(badCold);
                    for (const auto old : {1u, 2u, 3u, 4u, 5u}) {
                        badCold = coldRequest;
                        badCold.contractVersion = old;
                        rejectCold(badCold);
                    }
                    for (const auto member :
                        {&rhi::OriginalCommandAdmissionBudget::maxNativePayloadObjects,
                            &rhi::OriginalCommandAdmissionBudget::maxCallbackTickets,
                            &rhi::OriginalCommandAdmissionBudget::maxCallbackRegistrations}) {
                        auto less = authored;
                        --(less.*member);
                        assert(rhi::OriginalTypedNativeColdContract::validateSourceAuthority(
                            coldInspection.required, closure->nativePayloadCount(), less));
                    }
                    auto fewerCalls = authored;
                    --fewerCalls.maxTypedNativeCommandCalls;
                    assert(rhi::OriginalTypedNativeColdContract::validateSourceAuthority(
                        coldInspection.required, closure->nativePayloadCount(), fewerCalls));
                    auto submission = core::makeShared<core::CpuMemoryQuota>(bytes),
                         callback = core::makeShared<core::CpuMemoryQuota>(bytes);
                    const auto coldOwners = coldInspection.required.retainedOwners;
                    rhi::OriginalCommandNativeLifecycleBudget lifecycle{3u,
                        true,
                        coldCapacity.capacity.pendingSubmissions,
                        coldCapacity.capacity.callbackTickets,
                        coldCapacity.capacity.callbackRegistrations,
                        coldRequest.maxRetainedOwners,
                        coldRequest.maxRetainedOwners,
                        coldRequest.maxRetainedOwners,
                        bytes,
                        bytes,
                        bytes};
                    using Seal = rhi::OriginalCommandRecordingSeal;
                    using Reservation = rhi::OriginalCommandLifecycleReservation;
                    for (const auto old : {1u, 2u}) {
                        auto legacy = lifecycle;
                        legacy.contractVersion = old;
                        assert(!Seal::prepareOriginal(
                                   root, *c.device, legacy, submission, callback, id)
                            && id);
                    }
                    assert(!Seal::prepareOriginal(
                               root, *c.device, lifecycle, submission, submission, id)
                        && id);
                    auto seal =
                        Seal::prepareOriginal(root, *c.device, lifecycle, submission, callback, id);
                    require(id);
                    assert(seal);
                    assert(root->applyOriginalPrototypePlan());
                    auto shortDemand = coldInspection.required.capacity;
                    --shortDemand.callbackTickets;
                    auto shortReservation = Reservation::prepareOriginal(seal, shortDemand, id);
                    require(id);
                    assert(shortReservation);
                    assert(
                        Cold::inspectOriginal(payload, shortReservation, coldRequest).diagnostic);
                    shortReservation.reset();
                    auto reservation =
                        Reservation::prepareOriginal(seal, coldInspection.required.capacity, id);
                    require(id);
                    assert(reservation);
                    auto second =
                        Reservation::prepareOriginal(seal, coldInspection.required.capacity, id);
                    assert(!second && id);
                    const auto makeCold = [&] {
                        return Cold::prepareOriginal(payload, reservation, coldRequest, id);
                    };
                    CpuQuotaFactoryFixture coldPressure(recording);
                    const auto coldPeak = coldPressure.measureRetained(makeCold);
                    coldPressure.verify(coldPeak, makeCold);
                    {
                        core::CpuMemoryQuotaLease full(recording, bytes - recording->liveBytes());
                        assert(full.charged() && !makeCold() && id);
                    }
                    auto cold = makeCold();
                    require(id);
                    assert(cold);
                    auto forgedCold = std::shared_ptr<const Cold>(recording, cold.get());
                    assert(Cold::validateOriginal(forgedCold));
                    forgedCold.reset();
                    OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                        require(CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{source,
                                                                                  inputQuota,
                                                                                  stateQuota,
                                                                                  streamQuota,
                                                                                  recording,
                                                                                  submission,
                                                                                  callback},
                            [&]() noexcept {
                                return cold->validate();
                            }));
                    });
                    assert(!cold->nativeExecutionProven() && cold->executionDiagnostic());
                    for (const auto version : {1u, 2u, 3u, 4u, 5u, 7u}) {
                        rhi::OriginalTypedNativeColdCommandRequest unavailable{
                            version, true, 1u, coldOwners, bytes};
                        const auto allocations =
                            core::defaultCpuAllocator().stats().allocationCount;
                        assert(!rhi_webgpu::WebGpuOriginalTypedColdCommand::prepareOriginal(
                                   cold, unavailable, id)
                            && id);
                        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
                    }
                    using Command = rhi_webgpu::WebGpuOriginalTypedColdCommand;
                    using CommandContract = rhi::OriginalTypedNativeColdCommandContract;
                    const auto commandOwners = coldOwners + CommandContract::additionalOwners;
                    const Command::Request commandRequest{
                        6u, true, 1u, coldRequest.maxRetainedOwners, bytes};
                    const auto rejectCommand = [&](auto b) {
                        const auto allocations =
                            core::defaultCpuAllocator().stats().allocationCount;
                        assert(!Command::prepareOriginal(cold, b, id) && id);
                        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
                    };
                    auto badCommand = commandRequest;
                    badCommand.maxRetainedOwners = commandOwners - 1u;
                    rejectCommand(badCommand);
                    badCommand = commandRequest;
                    ++badCommand.maxRetainedOwners;
                    rejectCommand(badCommand);
                    badCommand = commandRequest;
                    --badCommand.maxCpuMetadataBytes;
                    rejectCommand(badCommand);
                    badCommand = commandRequest;
                    badCommand.prototypeOptIn = false;
                    rejectCommand(badCommand);
                    auto forgedAdmission = std::shared_ptr<const Cold>(callback, cold.get());
                    assert(!Command::prepareOriginal(forgedAdmission, commandRequest, id) && id);
                    forgedAdmission.reset();
                    const auto makeCommand = [&] {
                        return Command::prepareOriginal(cold, commandRequest, id);
                    };
                    // Rebuild genuine original candidates between single-use quota
                    // probes. Never reset or replace a seal claim on a live root.
                    const auto rebindCommand = [&] {
                        cold.reset();
                        reservation.reset();
                        seal.reset();
                        payload.reset();
                        frozenDecision.reset();
                        closure.reset();
                        root.reset();
                        assert(!sources->originalStateTransactionCount()
                            && !sources->originalReservedSubmissionCount());
                        root = Root::prepareOriginalOwnership(
                            graph, *c.device, rootBudget, recording, id);
                        require(id);
                        assert(root);
                        require(
                            root->materializeOriginalPrototypePlan(stream.stateSequence, readPlan));
                        auto fullCustody = custody;
                        fullCustody.maxRetainedOwners = closureOwnerCeiling;
                        closure = make(fullCustody);
                        require(id);
                        assert(closure);
                        frozenDecision = Freeze::prepareOriginal(closure, freezeBudget, id);
                        require(id);
                        assert(frozenDecision);
                        payload = Payload::prepareOriginal(frozenDecision, payloadBudget, id);
                        require(id);
                        assert(payload);
                        seal = Seal::prepareOriginal(
                            root, *c.device, lifecycle, submission, callback, id);
                        require(id);
                        assert(seal);
                        assert(root->applyOriginalPrototypePlan());
                        reservation = Reservation::prepareOriginal(
                            seal, coldInspection.required.capacity, id);
                        require(id);
                        assert(reservation);
                        cold = makeCold();
                        require(id);
                        assert(cold);
                    };
                    const auto commandPeak =
                        coldPressure.measureSingleUse(rebindCommand, makeCommand);
                    coldPressure.verifySingleUse(commandPeak, rebindCommand, makeCommand);
                    auto command = makeCommand();
                    require(id);
                    assert(command && !command->validate());
                    assert(command->retainedOwnerCount() == commandOwners
                        && !command->nativeExecutionProven());
                    assert(!makeCommand() && id
                        && core::StringView(id) == "RHI-WEBGPU-TYPED-COLD-COMMAND-DUPLICATE");
                    auto peerCold = makeCold();
                    require(id);
                    assert(peerCold);
                    assert(!Command::prepareOriginal(peerCold, commandRequest, id) && id);
                    peerCold.reset();
                    auto forgedCommand = std::shared_ptr<const Command>(callback, command.get());
                    assert(Command::validateOriginal(forgedCommand));
                    forgedCommand.reset();
                    using Callback = rhi_webgpu::WebGpuOriginalTypedCallbackCustody;
                    using CallbackContract = rhi::OriginalTypedCallbackCustodyContract;
                    using TypedIssuer = rhi_webgpu::WebGpuOriginalTypedIssuerPreparation;
                    const Callback::Request callbackRequest{6u,
                        true,
                        rhi::OriginalTypedLossObserverPolicy::RetainPhysicalLossUntilLocalDrain1,
                        1u,
                        8u,
                        1u,
                        commandRequest.maxRetainedOwners,
                        bytes,
                        bytes};
                    const auto callbackOwners =
                        commandOwners + CallbackContract::additionalOwners(callbackRequest);
                    const auto rejectCallback = [&](auto b) {
                        const auto allocations =
                            core::defaultCpuAllocator().stats().allocationCount;
                        assert(!Callback::prepareOriginal(command, b, id) && id);
                        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
                    };
                    for (const auto version : {1u, 2u, 3u, 4u, 5u, 7u}) {
                        auto legacy = callbackRequest;
                        legacy.contractVersion = version;
                        rejectCallback(legacy);
                    }
                    auto badCallback = callbackRequest;
                    badCallback.maxRetainedOwners = callbackOwners - 1u;
                    rejectCallback(badCallback);
                    badCallback = callbackRequest;
                    ++badCallback.maxRetainedOwners;
                    rejectCallback(badCallback);
                    badCallback = callbackRequest;
                    badCallback.readbackMap = {1u, true};
                    rejectCallback(badCallback);
                    badCallback = callbackRequest;
                    --badCallback.maxCallbackSlots;
                    rejectCallback(badCallback);
                    badCallback = callbackRequest;
                    badCallback.prototypeOptIn = false;
                    rejectCallback(badCallback);
                    badCallback = callbackRequest;
                    --badCallback.maxCpuSubmissionBytes;
                    rejectCallback(badCallback);
                    badCallback = callbackRequest;
                    --badCallback.maxCpuCallbackBytes;
                    rejectCallback(badCallback);
                    const auto makeCallback = [&] {
                        return Callback::prepareOriginal(command, callbackRequest, id);
                    };
                    const auto rebindCallback = [&] {
                        command.reset();
                        rebindCommand();
                        command = makeCommand();
                        require(id);
                        assert(command);
                    };
                    CpuQuotaFactoryFixture submissionPressure(submission),
                        callbackPressure(callback);
                    const auto submissionPeak =
                        submissionPressure.measureSingleUse(rebindCallback, makeCallback);
                    submissionPressure.verifySingleUse(
                        submissionPeak, rebindCallback, makeCallback);
                    const auto callbackPeak =
                        callbackPressure.measureSingleUse(rebindCallback, makeCallback);
                    callbackPressure.verifySingleUse(callbackPeak, rebindCallback, makeCallback);
                    auto callbackCustody = makeCallback();
                    require(id);
                    assert(callbackCustody && !callbackCustody->validate());
                    assert(callbackCustody->pendingOriginalRecords() == 1u
                        && callbackCustody->physicalLossArmed()
                        && !callbackCustody->physicalLossDelivered()
                        && !callbackCustody->nativeExecutionProven());
                    assert(callbackCustody->executionDiagnostic());
                    assert(!makeCallback() && id
                        && core::StringView(id) == "RHI-WEBGPU-TYPED-CALLBACK-DUPLICATE");
                    auto forgedCallback =
                        std::shared_ptr<const Callback>(recording, callbackCustody.get());
                    assert(Callback::validateOriginal(forgedCallback));
                    forgedCallback.reset();
                    for (const auto version : {1u, 2u, 3u, 4u, 5u, 7u}) {
                        const auto allocations =
                            core::defaultCpuAllocator().stats().allocationCount;
                        assert(!TypedIssuer::prepareOriginal(callbackCustody,
                                   {version,
                                       true,
                                       1u,
                                       1u,
                                       7u,
                                       callbackRequest.maxRetainedOwners,
                                       bytes,
                                       bytes,
                                       bytes},
                                   id)
                            && id);
                        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
                    }
                    const auto issuerOwners = callbackOwners
                        + rhi::OriginalTypedIssuerPreparationContract::additionalOwners;
                    const TypedIssuer::Request issuerRequest{6u,
                        true,
                        1u,
                        1u,
                        7u,
                        callbackRequest.maxRetainedOwners,
                        bytes,
                        bytes,
                        bytes};
                    auto shortIssuer = issuerRequest;
                    shortIssuer.maxRetainedOwners = issuerOwners - 1u;
                    const auto refusedAllocations =
                        core::defaultCpuAllocator().stats().allocationCount;
                    assert(!TypedIssuer::prepareOriginal(callbackCustody, shortIssuer, id) && id);
                    shortIssuer = issuerRequest;
                    ++shortIssuer.maxRetainedOwners;
                    assert(!TypedIssuer::prepareOriginal(callbackCustody, shortIssuer, id) && id);
                    assert(
                        core::defaultCpuAllocator().stats().allocationCount == refusedAllocations);
                    std::shared_ptr<const TypedIssuer> typedIssuers;
                    OriginalTypedIssuerPreparationFixture::verifyGeneral(command,
                        callbackCustody,
                        typedIssuers,
                        callbackRequest,
                        recording,
                        submission,
                        callback,
                        rebindCallback);
                    require(typedIssuers->validate());
                    assert(typedIssuers->issuersClosedForInspection()
                        && !typedIssuers->nativeExecutionProven());
                    for (const auto version : {1u, 2u, 3u, 4u, 5u, 7u}) {
                        rhi::OriginalTypedNativeExecutionRequest unavailable{version,
                            true,
                            rhi::OriginalTypedSynchronizationPolicy::
                                FrozenOriginalScopeVisibility1};
                        if (version == 5u)
                            unavailable.readbackCompletion = {1u, true};
                        const auto allocations =
                            core::defaultCpuAllocator().stats().allocationCount;
                        assert(TypedIssuer::recordOriginal(typedIssuers, unavailable)
                            && TypedIssuer::submitOriginal(typedIssuers, unavailable));
                        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
                    }
                    std::weak_ptr<const Initialized> retainedContent = initialized;
                    contents[0].source.reset();
                    initialized.reset();
                    assert(!retainedContent.expired() && !payload->validate());
                    // The authored shader samples the center of mip0 with
                    // explicit LOD0. Reuse strided channel statistics for its
                    // four equally weighted texels, then quantize the uniform sum.
                    const auto& pixels = sampledUpload.binding->originalInput()->pixelBytes();
                    assert(pixels.size() == 20u);
                    const auto& sampledDesc = sampler->originalObject()->desc();
                    assert(sampledDesc.minFilter == rhi::SamplerFilter::Linear
                        && sampledDesc.magFilter == rhi::SamplerFilter::Linear
                        && sampledDesc.addressU == rhi::SamplerAddressMode::ClampToEdge
                        && sampledDesc.addressV == rhi::SamplerAddressMode::ClampToEdge
                        && sampledDesc.compareOp == rhi::CompareOp::Never
                        && sampledDesc.maxAnisotropy <= 1.0f);
                    const auto reference = core::analyzeStridedImageChannels(pixels.data(),
                        pixels.size(),
                        2u,
                        2u,
                        8u,
                        4u,
                        4u,
                        [](const std::uint8_t* pixel, std::uint32_t channel) {
                            return double(pixel[channel]) / 255.0;
                        });
                    assert(reference.valid);
                    std::array<float, 4u> uniforms;
                    std::memcpy(uniforms.data(), initialBytes.data(), initialBytes.size());
                    std::array<std::uint16_t, 4u> expected;
                    for (std::size_t channel = 0u; channel < expected.size(); ++channel)
                        expected[channel] = core::ibl::floatToHalf(
                            uniforms[channel] + float(reference.channels[channel].meanValue));
                    std::weak_ptr<const TextureContent> retainedTextureContent = textureContent;
                    std::weak_ptr<const Issuer> retainedTextureIssuer = uploadIssuer;
                    textureContents[0].source.reset();
                    textureContents[0].issuer.reset();
                    textureContents[0].uploadRoot.reset();
                    textureContent.reset();
                    uploadIssuer.reset();
                    uploadRoot.reset();
                    uploadInputs.reset();
                    textureStaging.reset();
                    sampledUpload.release();
                    assert(!retainedTextureContent.expired() && !retainedTextureIssuer.expired()
                        && !payload->validate());
                    auto committedState = parent->originalObject()->observeOriginalCommittedState(
                        sampledTransition.textureSubresources);
                    assert(committedState);
                    assert(committedState->state() == rhi::ResourceState::CopyDst);
                    auto peerFreeze = Freeze::prepareOriginal(closure, freezeBudget, id);
                    require(id);
                    assert(peerFreeze);
                    assert(payload->compareOriginalDecision(peerFreeze));
                    peerFreeze.reset();
                    for (const auto& record : payload->records())
                        if (record.original.kind == rhi::CommandUsageKind::BindComputeDescriptor) {
                            std::uint32_t group = UINT32_MAX;
                            require(Payload::originalPhysicalGroup(record, group));
                            assert(group == 0u && record.binding->sourceIndex == 7u
                                && record.pipeline->sourceIndex == 6u);
                            auto& mutableRecord = const_cast<Payload::Record&>(record);
                            const auto* savedPipeline = record.pipeline;
                            mutableRecord.pipeline = nullptr;
                            assert(payload->validate());
                            mutableRecord.pipeline = savedPipeline;
                            require(payload->validate());
                        }
                    auto nativeSampler =
                        rhi_webgpu::webGpuNativePayloadFor(*sampler->originalObject());
                    assert(nativeSampler);
                    const auto savedSampler = nativeSampler->sampler;
                    nativeSampler->sampler = nullptr;
                    assert(
                        closure->validate() && frozenDecision->validate() && payload->validate());
                    assert(cold->validate() && command->validate() && callbackCustody->validate()
                        && typedIssuers->validate());
                    nativeSampler->sampler = savedSampler;
                    require(closure->validate());
                    const auto savedDevice = nativeSampler->ownerDevice;
                    nativeSampler->ownerDevice = nullptr;
                    assert(
                        closure->validate() && frozenDecision->validate() && payload->validate());
                    nativeSampler->ownerDevice = savedDevice;
                    require(closure->validate());
                    OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{source,
                                                                                  inputQuota,
                                                                                  stateQuota,
                                                                                  streamQuota,
                                                                                  recording,
                                                                                  submission,
                                                                                  callback},
                            [&]() noexcept {
                                return typedIssuers->validate();
                            }));
                    });
                    using OutputSnapshot = rhi_webgpu::WebGpuOriginalGeneralOutputSnapshot;
                    using OutputContract = rhi::OriginalGeneralOutputSnapshotContract;
                    std::shared_ptr<const OutputSnapshot> outputSnapshot;
                    if (submitGeneral) {
                        const auto output =
                            std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(
                                storage[0]);
                        const auto snapshotProjection =
                            P::inspectStateBound(closure->originalStream(),
                                graph,
                                frozenDecision->originalRequest().projection.usages);
                        require(snapshotProjection.diagnostic);
                        OutputSnapshot::Budget snapshotBudget{1u,
                            true,
                            OutputContract::Provenance::QueueOrderedSnapshot,
                            rhi::defaultTextureSubresourceRange(output->originalObject()->desc()),
                            issuerOwners + 1u,
                            usage->commands().size() + snapshotProjection.counts.contributions,
                            output->originalObject()->allocation().estimatedSizeBytes,
                            bytes};
                        const auto makeSnapshot = [&] {
                            return OutputSnapshot::prepareOriginal(
                                typedIssuers, root, output, snapshotBudget, source, id);
                        };
                        CpuQuotaFactoryFixture snapshotPressure(source);
                        const auto snapshotPeak = snapshotPressure.measureRetained(makeSnapshot);
                        snapshotPressure.verify(snapshotPeak, makeSnapshot);
                        auto invalid = snapshotBudget;
                        --invalid.maxRetainedOwners;
                        assert(!OutputSnapshot::prepareOriginal(
                            typedIssuers, root, output, invalid, source, id));
                        invalid = snapshotBudget;
                        --invalid.maxJoinVisits;
                        assert(!OutputSnapshot::prepareOriginal(
                            typedIssuers, root, output, invalid, source, id));
                        invalid = snapshotBudget;
                        --invalid.maxSourceBytes;
                        assert(!OutputSnapshot::prepareOriginal(
                            typedIssuers, root, output, invalid, source, id));
                        invalid = snapshotBudget;
                        invalid.maxRetainedOwners = issuerRequest.maxRetainedOwners + 1u;
                        assert(!OutputSnapshot::prepareOriginal(
                            typedIssuers, root, output, invalid, source, id));
                        auto forgedRoot = std::shared_ptr<const Root>(callback, root.get());
                        assert(!OutputSnapshot::prepareOriginal(
                            typedIssuers, forgedRoot, output, snapshotBudget, source, id));
                        forgedRoot.reset();
                        auto forgedIssuer =
                            std::shared_ptr<const TypedIssuer>(callback, typedIssuers.get());
                        assert(!OutputSnapshot::prepareOriginal(
                            forgedIssuer, root, output, snapshotBudget, source, id));
                        forgedIssuer.reset();
                        auto forgedOutput = std::shared_ptr<const rhi::OriginalCommandTextureLease>(
                            callback, output.get());
                        assert(!OutputSnapshot::prepareOriginal(
                            typedIssuers, root, forgedOutput, snapshotBudget, source, id));
                        forgedOutput.reset();
                        assert(!OutputSnapshot::prepareOriginal(
                            typedIssuers, root, parent, snapshotBudget, source, id));
                        outputSnapshot = makeSnapshot();
                        require(id);
                        assert(outputSnapshot);
                        assert(OutputSnapshot::validateCompletedOriginal(outputSnapshot));
                        std::printf(
                            "BINDLESS_RHI_GENERAL_OUTPUT_SNAPSHOT_PREPARED_OK original-issuer-root-output owners%u ceiling%u visits%llu bytes%llu metadata%llu exact-less-exhausted forged-refused numerical-unproven\n",
                            snapshotBudget.maxRetainedOwners,
                            issuerRequest.maxRetainedOwners,
                            static_cast<unsigned long long>(snapshotBudget.maxJoinVisits),
                            static_cast<unsigned long long>(snapshotBudget.maxSourceBytes),
                            static_cast<unsigned long long>(snapshotPeak));
                    }
                    std::weak_ptr<Root> retained = root;
                    root.reset();
                    assert(!retained.expired() && !closure->validate());
                    const auto closureOwners = closure->retainedOwnerCount();
                    std::weak_ptr<const Closure> retainedClosure = closure;
                    closure.reset();
                    assert(!retainedClosure.expired() && !retained.expired()
                        && !frozenDecision->validate());
                    std::weak_ptr<const Freeze> retainedFreeze = frozenDecision;
                    frozenDecision.reset();
                    assert(!retainedFreeze.expired() && !retainedClosure.expired()
                        && !retained.expired() && !payload->validate());
                    const auto payloadOwners = payload->retainedOwnerCount();
                    const auto payloadCalls = payload->work().nativeCalls;
                    payload.reset();
                    reservation.reset();
                    seal.reset();
                    assert(!retainedFreeze.expired() && !retainedClosure.expired()
                        && !retained.expired() && !cold->validate());
                    assert(cold->originalRoot()->status() == Root::Status::Prepared
                        && cold->originalRoot()->journalEmpty());
                    const auto coldAllocationCount =
                        core::defaultCpuAllocator().stats().allocationCount;
                    std::uint64_t readbackPreparationAllocations = 0u;
                    std::weak_ptr<const Cold> retainedCold = cold;
                    cold.reset();
                    assert(!retainedCold.expired() && !command->validate());
                    assert(command->originalAdmission()->originalRoot()->status()
                        == Root::Status::Prepared);
                    std::weak_ptr<const Command> retainedCommand = command;
                    command.reset();
                    assert(!retainedCommand.expired() && !retainedCold.expired()
                        && !callbackCustody->validate());
                    std::weak_ptr<const Callback> retainedCallback = callbackCustody;
                    callbackCustody.reset();
                    assert(!retainedCallback.expired() && !typedIssuers->validate());
                    const auto contentGeneration = completedTexture->contentGeneration(),
                               contentRevision = completedTexture->revision();
                    {
                        const rhi::OriginalTypedNativeExecutionRequest recording6{6u,
                            true,
                            rhi::OriginalTypedSynchronizationPolicy::FrozenOriginalScopeVisibility1,
                            {},
                            {submitGeneral ? 1u : 0u, submitGeneral}};
                        auto refusedRecording = recording6;
                        refusedRecording.prototypeOptIn = false;
                        assert(TypedIssuer::recordOriginal(typedIssuers, refusedRecording));
                        refusedRecording = recording6;
                        refusedRecording.readbackCompletion = {1u, true};
                        assert(TypedIssuer::recordOriginal(typedIssuers, refusedRecording));
                        assert(TypedIssuer::submitOriginal(typedIssuers, recording6));
                        if (physical) {
                            using Probe = WebGpuNativeSubmissionProbeFixture;
                            using Held = WebGpuErrorScopeHandoffFixture<1023u>;
                            const auto physicalRoot = retained.lock();
                            assert(physicalRoot);
                            const auto physicalCustody = retainedCallback.lock();
                            assert(physicalCustody);
                            const auto hotQuotas = std::array{source,
                                inputQuota,
                                stateQuota,
                                streamQuota,
                                recording,
                                submission,
                                callback};
                            const auto record = [&](const char* expectedDiagnostic = nullptr) {
                                OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                                    require(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                                        hotQuotas, [&]() noexcept -> const char* {
                                            const auto* failure = TypedIssuer::recordOriginal(
                                                typedIssuers, recording6);
                                            if (expectedDiagnostic) {
                                                assert(failure
                                                    && core::StringView(failure)
                                                        == expectedDiagnostic);
                                                return nullptr;
                                            }
                                            return failure;
                                        }));
                                });
                            };
                            if (physicalLoss == PhysicalLoss::QueueUncertain) {
                                record();
                                assert(WebGpuAsyncWaitFixture::until([&] {
                                    return typedIssuers->hasOriginalRecordedCommand();
                                }));
                                auto queue = wgpuDeviceGetQueue(savedDevice);
                                assert(queue);
                                Probe::instrumentLoss(savedDevice, queue, true);
                                OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                                    require(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                                        hotQuotas, [&]() noexcept -> const char* {
                                            const auto* failure = TypedIssuer::submitOriginal(
                                                typedIssuers, recording6);
                                            assert(failure
                                                && core::StringView(failure)
                                                    == "RHI-WEBGPU-CANONICAL-SUBMISSION-REGISTRATION-FAILED");
                                            return nullptr;
                                        }));
                                });
                                Probe::restoreLoss();
                                wgpuQueueRelease(queue);
                            } else if (physicalLoss == PhysicalLoss::RecordingShutdown) {
                                Held::holdScopes();
                                record();
                                Held::restoreScopes();
                                Held::waitHeldScopes();
                                assert(physicalRoot->status() == Root::Status::Pending
                                    && !physicalRoot->journalEmpty());
                                c.device.reset();
                            } else if (physicalLoss == PhysicalLoss::PartialPush) {
                                // This poisons the real native scope stack. All resources
                                // and candidates are already prepared; no further native
                                // work may be recorded on this device before destruction.
                                Probe::instrumentScopePush(savedDevice);
                                record("RHI-WEBGPU-TYPED-RECORDING-SCOPE-PUSH-UNCERTAIN");
                                Probe::restoreScopePush();
                            } else {
                                Probe::instrumentRecordingException(savedDevice);
                                record("RHI-WEBGPU-TYPED-RECORDING-INVOCATION-UNCERTAIN");
                                Probe::restoreRecordingException();
                            }
                            assert(physicalCustody->pendingOriginalRecords() == 1u
                                && physicalCustody->physicalLossArmed()
                                && !physicalCustody->physicalLossDelivered()
                                && !typedIssuers->hasOriginalCompletedSubmission());
                            assert(EM_ASM_INT(
                                {
                                    // clang-format off
                                        const d = WebGPU.getJsObject($0);
                                        if (d !== Module.preinitializedWebGPUDevice)
                                            return 0;
                                        d.destroy();
                                        return 1;
                                    // clang-format on
                                },
                                savedDevice));
                            assert(WebGpuAsyncWaitFixture::until([&] {
                                return physicalCustody->physicalLossDelivered();
                            }));
                            if (physicalLoss == PhysicalLoss::RecordingShutdown) {
                                assert(physicalCustody->pendingOriginalRecords() == 1u
                                    && !typedIssuers->hasOriginalRecordedCommand()
                                    && physicalRoot->status() == Root::Status::Invalidated
                                    && physicalRoot->journalEmpty());
                                Held::releaseScopes();
                            }
                            assert(WebGpuAsyncWaitFixture::until([&] {
                                return !physicalCustody->pendingOriginalRecords();
                            }));
                            assert(physicalRoot->status() == Root::Status::Invalidated
                                && physicalRoot->journalEmpty()
                                && physicalCustody->physicalLossDelivered()
                                && !physicalCustody->physicalLossArmed()
                                && !typedIssuers->hasOriginalCompletedSubmission());
                            assert(TypedIssuer::recordOriginal(typedIssuers, recording6)
                                && TypedIssuer::submitOriginal(typedIssuers, recording6));
                            if (outputSnapshot)
                                assert(OutputSnapshot::validateCompletedOriginal(outputSnapshot));
                        } else {
                            const auto pendingAdmission = retainedCold.lock();
                            assert(pendingAdmission);
                            const auto pendingRoot = retained.lock();
                            assert(pendingRoot);
                            const auto capturedGeneration = completedTexture->contentGeneration(),
                                       capturedRevision = completedTexture->revision();
                            require(CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{source,
                                                                                      inputQuota,
                                                                                      stateQuota,
                                                                                      streamQuota,
                                                                                      recording,
                                                                                      submission,
                                                                                      callback},
                                [&]() noexcept -> const char* {
                                    nativeSampler->sampler = nullptr;
                                    assert(TypedIssuer::recordOriginal(typedIssuers, recording6));
                                    nativeSampler->sampler = savedSampler;
                                    nativeSampler->ownerDevice = nullptr;
                                    assert(TypedIssuer::recordOriginal(typedIssuers, recording6));
                                    nativeSampler->ownerDevice = savedDevice;
                                    assert(pendingRoot->status() == Root::Status::Prepared
                                        && pendingRoot->journalEmpty()
                                        && !typedIssuers->hasOriginalRecordedCommand());
                                    return typedIssuers->validate();
                                }));
                            using Scopes = WebGpuErrorScopeHandoffFixture<1019u>;
                            using Probe = WebGpuNativeSubmissionProbeFixture;
                            const bool recordingProbe = terminal == RecordingTerminal::NativeError
                                || terminal == RecordingTerminal::ReverseScopes;
                            if (recordingProbe)
                                Probe::instrumentRecording(
                                    savedDevice, terminal == RecordingTerminal::ReverseScopes);
                            Scopes::holdScopes(terminal != RecordingTerminal::NativeError);
                            OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                                require(
                                    CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{source,
                                                                                      inputQuota,
                                                                                      stateQuota,
                                                                                      streamQuota,
                                                                                      recording,
                                                                                      submission,
                                                                                      callback},
                                        [&]() noexcept {
                                            return TypedIssuer::recordOriginal(
                                                typedIssuers, recording6);
                                        }));
                            });
                            Scopes::restoreScopes();
                            if (recordingProbe)
                                Probe::restoreRecording();
                            Scopes::waitHeldScopes();
                            assert(pendingRoot->status() == Root::Status::Pending
                                && !pendingRoot->journalEmpty()
                                && !typedIssuers->hasOriginalRecordedCommand()
                                && !typedIssuers->hasOriginalCompletedSubmission());
                            if (outputSnapshot)
                                assert(OutputSnapshot::validateCompletedOriginal(outputSnapshot));
                            assert(typedIssuers
                                    ->validate()); // Prepared-only public validation stays strict.
                            assert(!parent->originalObject()->observeOriginalCommittedState(
                                sampledTransition.textureSubresources));
                            const auto retainedInitialized = retainedContent.lock();
                            assert(retainedInitialized && retainedInitialized->readyDiagnostic());
                            require(CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{source,
                                                                                      inputQuota,
                                                                                      stateQuota,
                                                                                      streamQuota,
                                                                                      recording,
                                                                                      submission,
                                                                                      callback},
                                [&]() noexcept -> const char* {
                                    using Preparation = rhi_webgpu::WebGpuNativeCommandPreparation;
                                    if (const auto* failure =
                                            Preparation::validateTypedAdmissionRetained(
                                                pendingAdmission))
                                        return failure;
                                    auto forgedPending = std::shared_ptr<const Cold>(
                                        callback, pendingAdmission.get());
                                    assert(
                                        Preparation::validateTypedAdmissionRetained(forgedPending));
                                    nativeSampler->sampler = nullptr;
                                    assert(Preparation::validateTypedAdmissionRetained(
                                        pendingAdmission));
                                    nativeSampler->sampler = savedSampler;
                                    nativeSampler->ownerDevice = nullptr;
                                    assert(Preparation::validateTypedAdmissionRetained(
                                        pendingAdmission));
                                    nativeSampler->ownerDevice = savedDevice;
                                    assert(!completedTexture->diagnostic()
                                        && completedTexture->contentGeneration()
                                            == capturedGeneration
                                        && completedTexture->revision() == capturedRevision);
                                    assert(TypedIssuer::recordOriginal(typedIssuers, recording6));
                                    assert(TypedIssuer::submitOriginal(typedIssuers, recording6));
                                    return Preparation::validateTypedAdmissionRetained(
                                        pendingAdmission);
                                }));
                            if (terminal != RecordingTerminal::None) {
                                auto terminalCustody = retainedCallback.lock();
                                assert(terminalCustody);
                                if (terminal == RecordingTerminal::Cancel) {
                                    const auto* cancelled = terminalCustody->cancelOriginal();
                                    assert(cancelled
                                        && core::StringView(cancelled)
                                            == "RHI-WEBGPU-CANONICAL-CALLBACK-CANCEL-CALLBACKS-PENDING");
                                } else if (terminal == RecordingTerminal::Drop) {
                                    typedIssuers.reset();
                                    terminalCustody.reset();
                                }
                                assert(pendingRoot->status() == Root::Status::Pending
                                    && !pendingRoot->journalEmpty());
                                if (terminal == RecordingTerminal::ReverseScopes) {
                                    // The existing probe reverses the three genuine native
                                    // promises. Deliver the held receipts one at a time;
                                    // no recording proof may appear before the last one.
                                    for (std::uint32_t i = 0u; i < 2u; ++i) {
                                        Scopes::releaseScope(i);
                                        emscripten_sleep(0);
                                        assert(pendingRoot->status() == Root::Status::Pending
                                            && !pendingRoot->journalEmpty()
                                            && !typedIssuers->hasOriginalRecordedCommand()
                                            && terminalCustody->pendingOriginalRecords() == 1u);
                                    }
                                    Scopes::releaseScope(2u);
                                    assert(WebGpuAsyncWaitFixture::until([&] {
                                        return typedIssuers->hasOriginalRecordedCommand();
                                    }));
                                    require(typedIssuers->recordingDiagnostic());
                                    assert(!typedIssuers->hasOriginalCompletedSubmission());
                                    require(terminalCustody->cancelOriginal());
                                } else
                                    Scopes::releaseScopes();
                                assert(WebGpuAsyncWaitFixture::until([&] {
                                    return pendingRoot->status() != Root::Status::Pending;
                                }));
                                if (terminal == RecordingTerminal::NativeError) {
                                    const auto* failure = typedIssuers->recordingDiagnostic();
                                    assert(failure
                                        && core::StringView(failure)
                                            == "RHI-WEBGPU-TYPED-RECORDING-NATIVE-ERROR"
                                        && !typedIssuers->hasOriginalRecordedCommand()
                                        && !typedIssuers->hasOriginalCompletedSubmission());
                                    assert(TypedIssuer::recordOriginal(typedIssuers, recording6));
                                    assert(TypedIssuer::submitOriginal(typedIssuers, recording6));
                                }
                                assert(pendingRoot->status() == Root::Status::Abandoned
                                    && pendingRoot->journalEmpty());
                                if (terminalCustody) {
                                    assert(!terminalCustody->pendingOriginalRecords()
                                        && !terminalCustody->physicalLossArmed()
                                        && !terminalCustody->physicalLossDelivered());
                                }
                                // This diagnostic observer still owns the original cold
                                // admission and its reservation. Retirement is checked
                                // again below after the observer leaves this scope.
                                assert(sources->originalReservedSubmissionCount()
                                        == coldCapacity.capacity.pendingSubmissions
                                    && sources->originalReservedCallbackTicketCount()
                                        == coldCapacity.capacity.callbackTickets
                                    && sources->originalReservedCallbackRegistrationCount()
                                        == coldCapacity.capacity.callbackRegistrations);
                                terminalCustody.reset();
                                const auto* kind = terminal == RecordingTerminal::Cancel ? "cancel"
                                    : terminal == RecordingTerminal::Drop                ? "drop"
                                    : terminal == RecordingTerminal::NativeError ? "native-error"
                                                                                 : "reverse-scopes";
                                std::printf(
                                    "BINDLESS_RHI_GENERAL_RECORDING_TERMINAL1_OK %s real-scopes3 held-Pending original-rollback %s quotas-zero public-closed\n",
                                    kind,
                                    terminal == RecordingTerminal::Drop ? "caller-drop"
                                        : terminal == RecordingTerminal::NativeError
                                        ? "genuine-validation-error-no-retry"
                                        : terminal == RecordingTerminal::ReverseScopes
                                        ? "native-reverse-one-at-a-time-recorded-after-drain-cancel"
                                        : "caller-retained");
                            } else {
                                Scopes::releaseScopes();
                                assert(WebGpuAsyncWaitFixture::until([&] {
                                    return typedIssuers->hasOriginalRecordedCommand();
                                }));
                                require(typedIssuers->recordingDiagnostic());
                                assert(!typedIssuers->hasOriginalCompletedSubmission()
                                    && !pendingRoot->validate());
                                auto changedSubmission = recording6;
                                changedSubmission.generalSubmission = {1u, true};
                                if (!submitGeneral)
                                    assert(TypedIssuer::submitOriginal(
                                        typedIssuers, changedSubmission));
                                else {
                                    auto cancelOnly = recording6;
                                    cancelOnly.generalSubmission = {};
                                    assert(TypedIssuer::submitOriginal(typedIssuers, cancelOnly));
                                    using SubmissionScopes = WebGpuErrorScopeHandoffFixture<1020u>;

                                    struct InvocationObserver {
                                        const TypedIssuer* issuer;
                                        const Root* root;
                                        bool observed = false;

                                        static void observe(void* userdata) noexcept
                                        {
                                            auto& self =
                                                *static_cast<InvocationObserver*>(userdata);
                                            assert(!self.observed
                                                && self.root->status() == Root::Status::Pending
                                                && !self.root->journalEmpty());
                                            const auto* diagnostic =
                                                self.issuer->submissionDiagnostic();
                                            assert(diagnostic
                                                && core::StringView(diagnostic)
                                                    == "RHI-WEBGPU-TYPED-SUBMISSION-PENDING"
                                                && !self.issuer->hasOriginalCompletedSubmission());
                                            self.observed = true;
                                        }
                                    } invocation{typedIssuers.get(), pendingRoot.get()};

                                    WGPUQueue observedQueue = nullptr;
                                    const bool earlyQuarantine =
                                        submissionTerminal == SubmissionTerminal::ThrowBefore
                                        || submissionTerminal == SubmissionTerminal::ThrowAfter
                                        || submissionTerminal == SubmissionTerminal::PartialScope;
                                    const bool partialRegistration =
                                        submissionTerminal == SubmissionTerminal::PartialScope;
                                    if (partialRegistration)
                                        SubmissionScopes::holdScopes(true);
                                    if (submissionProbe) {
                                        const auto probe =
                                            submissionTerminal == SubmissionTerminal::ThrowBefore
                                            ? Probe::Probe::ThrowBefore
                                            : submissionTerminal == SubmissionTerminal::ThrowAfter
                                            ? Probe::Probe::ThrowAfter
                                            : submissionTerminal == SubmissionTerminal::NativeError
                                            ? Probe::Probe::NativeError
                                            : submissionTerminal
                                                == SubmissionTerminal::ReverseScopes
                                            ? Probe::Probe::Reverse
                                            : Probe::Probe::PartialScope;
                                        observedQueue = wgpuDeviceGetQueue(savedDevice);
                                        assert(observedQueue);
                                        Probe::instrument(savedDevice,
                                            observedQueue,
                                            probe,
                                            &invocation,
                                            &InvocationObserver::observe);
                                    }
                                    if (!partialRegistration)
                                        SubmissionScopes::holdScopes(
                                            submissionTerminal != SubmissionTerminal::NativeError);
                                    OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                                        require(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                                            std::array{source,
                                                inputQuota,
                                                stateQuota,
                                                streamQuota,
                                                recording,
                                                submission,
                                                callback},
                                            [&]() noexcept -> const char* {
                                                const auto* failure = TypedIssuer::submitOriginal(
                                                    typedIssuers, recording6);
                                                if (submissionTerminal
                                                        == SubmissionTerminal::ThrowBefore
                                                    || submissionTerminal
                                                        == SubmissionTerminal::ThrowAfter) {
                                                    assert(failure
                                                        && core::StringView(failure)
                                                            == "RHI-WEBGPU-CANONICAL-SUBMISSION-INVOCATION-UNCERTAIN");
                                                    return nullptr;
                                                }
                                                if (submissionTerminal
                                                    == SubmissionTerminal::PartialScope) {
                                                    assert(failure
                                                        && core::StringView(failure)
                                                            == "RHI-WEBGPU-CANONICAL-SUBMISSION-REGISTRATION-FAILED");
                                                    return nullptr;
                                                }
                                                return failure;
                                            }));
                                    });
                                    // Preserve the probe's throwing then() boundary; an outer
                                    // held Promise would hide the partial-registration failure.
                                    if (partialRegistration)
                                        Probe::restore();
                                    SubmissionScopes::restoreScopes();
                                    if (submissionProbe) {
                                        if (!partialRegistration)
                                            Probe::restore();
                                        assert(invocation.observed);
                                        wgpuQueueRelease(observedQueue);
                                    }
                                    SubmissionScopes::waitHeldScopes();
                                    assert(pendingRoot->status()
                                            == (earlyQuarantine ? Root::Status::Invalidated
                                                                : Root::Status::Pending)
                                        && pendingRoot->journalEmpty() == earlyQuarantine
                                        && !typedIssuers->hasOriginalCompletedSubmission());
                                    assert(
                                        OutputSnapshot::validateCompletedOriginal(outputSnapshot));
                                    auto forgedIssuer = std::shared_ptr<const TypedIssuer>(
                                        callback, typedIssuers.get());
                                    assert(TypedIssuer::submitOriginal(forgedIssuer, recording6));
                                    forgedIssuer.reset();
                                    assert(TypedIssuer::submitOriginal(typedIssuers, recording6));
                                    if (earlyQuarantine) {
                                        const auto* failure = completedTexture->diagnostic();
                                        assert(failure
                                            && core::StringView(failure)
                                                == "RHI-CANONICAL-CONTENT-CONTROL-INVALIDATED"
                                            && !parent->originalObject()->observeOriginalContent());
                                    } else {
                                        assert(!completedTexture->diagnostic()
                                            && completedTexture->contentGeneration()
                                                == contentGeneration
                                            && completedTexture->revision() == contentRevision);
                                    }
                                    if (submissionTerminated) {
                                        const auto terminalCustody = retainedCallback.lock();
                                        assert(terminalCustody);
                                        if (submissionTerminal == SubmissionTerminal::Cancel) {
                                            const auto* failure = terminalCustody->cancelOriginal();
                                            assert(failure
                                                && core::StringView(failure)
                                                    == "RHI-WEBGPU-CANONICAL-CALLBACK-QUARANTINED");
                                        } else if (submissionTerminal
                                            == SubmissionTerminal::Retired)
                                            c.device->retireOriginalDeviceLifetime();
                                        else if (submissionTerminal == SubmissionTerminal::Shutdown)
                                            c.device.reset();
                                        assert(terminalCustody->pendingOriginalRecords() == 1u
                                            && terminalCustody->physicalLossArmed()
                                            && !terminalCustody->physicalLossDelivered()
                                            && !typedIssuers->hasOriginalCompletedSubmission());
                                        assert(OutputSnapshot::validateCompletedOriginal(
                                            outputSnapshot));
                                        // Logical termination does not stand in for queue/scope
                                        // delivery, and cannot detach their native userdata.
                                        SubmissionScopes::releaseScopes();
                                        assert(WebGpuAsyncWaitFixture::until([&] {
                                            return !terminalCustody->pendingOriginalRecords();
                                        }));
                                        assert(pendingRoot->status() == Root::Status::Invalidated
                                            && pendingRoot->journalEmpty()
                                            && !typedIssuers->hasOriginalCompletedSubmission()
                                            && typedIssuers->submissionDiagnostic()
                                            && !terminalCustody->physicalLossDelivered());
                                        const auto* failure = typedIssuers->submissionDiagnostic();
                                        if (submissionTerminal == SubmissionTerminal::NativeError)
                                            assert(core::StringView(failure)
                                                == "RHI-WEBGPU-TYPED-SUBMISSION-NATIVE-ERROR");
                                        if (submissionTerminal == SubmissionTerminal::ThrowBefore
                                            || submissionTerminal == SubmissionTerminal::ThrowAfter)
                                            assert(core::StringView(failure)
                                                == "RHI-WEBGPU-CANONICAL-SUBMISSION-INVOCATION-UNCERTAIN");
                                        if (submissionTerminal == SubmissionTerminal::PartialScope)
                                            assert(core::StringView(failure)
                                                == "RHI-WEBGPU-CANONICAL-SUBMISSION-REGISTRATION-FAILED");
                                        assert(OutputSnapshot::validateCompletedOriginal(
                                            outputSnapshot));
                                        assert(
                                            TypedIssuer::submitOriginal(typedIssuers, recording6));
                                    } else {
                                        SubmissionScopes::releaseScopes();
                                        assert(WebGpuAsyncWaitFixture::until([&] {
                                            return typedIssuers->hasOriginalCompletedSubmission();
                                        }));
                                        require(typedIssuers->submissionDiagnostic());
                                        assert(pendingRoot->status() == Root::Status::Committed
                                            && pendingRoot->journalEmpty()
                                            && !pendingRoot->validate());
                                        require(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                                            std::array{source,
                                                inputQuota,
                                                stateQuota,
                                                streamQuota,
                                                recording,
                                                submission,
                                                callback},
                                            [&]() noexcept {
                                                return OutputSnapshot::validateCompletedOriginal(
                                                    outputSnapshot);
                                            }));
                                        readbackPreparationAllocations =
                                            OriginalGeneralOutputReadbackInputsFixture::
                                                verifyPrepared(
                                                    *c.device, outputSnapshot, resource, expected);
                                        assert(
                                            TypedIssuer::submitOriginal(typedIssuers, recording6));
                                    }
                                }
                            }
                        }
                    }
                    if (terminal != RecordingTerminal::None || physical) {
                        assert(!sources->originalReservedSubmissionCount()
                            && !sources->originalReservedCallbackTicketCount()
                            && !sources->originalReservedCallbackRegistrationCount());
                    }
                    if (!physical && !submitGeneral && terminal == RecordingTerminal::None)
                        require(CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{source,
                                                                                  inputQuota,
                                                                                  stateQuota,
                                                                                  streamQuota,
                                                                                  recording,
                                                                                  submission,
                                                                                  callback},
                            [&]() noexcept -> const char* {
                                const auto retiredRoot = retained.lock();
                                assert(retiredRoot);
                                const auto retiredCallback = retainedCallback.lock();
                                assert(retiredCallback);
                                if (const auto* failure = retiredCallback->cancelOriginal())
                                    return failure;
                                assert(!retiredCallback->pendingOriginalRecords()
                                    && !retiredCallback->physicalLossArmed()
                                    && !retiredCallback->physicalLossDelivered());
                                assert(!sources->originalReservedSubmissionCount()
                                    && !sources->originalReservedCallbackTicketCount()
                                    && !sources->originalReservedCallbackRegistrationCount());
                                assert(retiredRoot->status() == Root::Status::Abandoned
                                    && retiredRoot->journalEmpty());
                                return nullptr;
                            }));
                    else if (submitGeneral) {
                        const auto retiredCallback = retainedCallback.lock();
                        assert(retiredCallback);
                        assert(!retiredCallback->pendingOriginalRecords()
                            && !retiredCallback->physicalLossArmed());
                        assert(!sources->originalReservedSubmissionCount()
                            && !sources->originalReservedCallbackTicketCount()
                            && !sources->originalReservedCallbackRegistrationCount());
                    }
                    if (outputSnapshot) {
                        std::weak_ptr<const TypedIssuer> keptIssuer = typedIssuers;
                        typedIssuers.reset();
                        assert(!keptIssuer.expired());
                        assert(OutputSnapshot::validateCompletedOriginal(outputSnapshot));
                        typedIssuers = keptIssuer.lock();
                        outputSnapshot.reset();
                        keptIssuer.reset();
                    }
                    typedIssuers.reset();
                    assert(core::defaultCpuAllocator().stats().allocationCount
                        == coldAllocationCount + readbackPreparationAllocations);
                    OriginalTypedIssuerPreparationFixture::verifyGeneralReceiptsRetired();
                    assert(retainedFreeze.expired() && retainedClosure.expired()
                        && retained.expired());
                    assert(retainedCallback.expired());
                    retainedCallback.reset();
                    assert(!submission->liveBytes() && !callback->liveBytes());
                    assert(retainedContent.expired());
                    retainedContent.reset();
                    assert(retainedTextureContent.expired() && retainedTextureIssuer.expired());
                    retainedTextureContent.reset();
                    retainedTextureIssuer.reset();
                    assert(retainedCold.expired());
                    retainedCold.reset();
                    assert(retainedCommand.expired());
                    retainedCommand.reset();
                    // Quota-bound shared allocations outlive their objects while a
                    // weak control block exists. Retire the test observers too.
                    retainedFreeze.reset();
                    retainedClosure.reset();
                    retained.reset();
                    assert(!recording->liveBytes());
                    // The actual original recording above exercised Pending exclusion
                    // and its own cancellation rolled back the same journal. Rollback
                    // restores values but advances state revisions; the old frozen
                    // graph cannot be reused to create another valid transaction.
                    assert(committedState->diagnostic() && graph->validate());
                    const auto afterRead = parent->originalObject()->observeOriginalContent();
                    if (lifetimeTerminated || submissionTerminated) {
                        assert(completedTexture->diagnostic() && !afterRead);
                        if (!lifetimeTerminated)
                            assert(core::StringView(completedTexture->diagnostic())
                                == "RHI-CANONICAL-CONTENT-CONTROL-INVALIDATED");
                    } else {
                        assert(!completedTexture->diagnostic() && afterRead);
                        assert(afterRead->contentGeneration() == contentGeneration
                            && afterRead->revision() == contentRevision);
                    }
                    const auto afterState = parent->originalObject()->observeOriginalCommittedState(
                        sampledTransition.textureSubresources);
                    if (submissionTerminated || physical || outputPhysicalLoss)
                        assert(!afterState);
                    else
                        assert(afterState
                            && afterState->state()
                                == (submitGeneral ? rhi::ResourceState::ShaderRead
                                                  : rhi::ResourceState::CopyDst)
                            && afterState->revision() > committedState->revision());
                    assert(!recording->liveBytes());
                    std::puts(
                        "BINDLESS_RHI_COMPLETED_TEXTURE_READ_PLAN4_OK completed-whole original-state-exclusion pending-rollback content-stamp-unchanged owner-less-refused cpu-ceiling48 hot0 no-native-execution-authority");
                    std::printf(
                        "BINDLESS_RHI_GENERAL_NATIVE_CLOSURE8_OK original8 resource6 memory3 sampler-nonmemory1 pipeline1 descriptor1 payload8 owners%u ceiling%u bytes%llu exact-less-exhausted canonical-owner-identity caller-drop hot0-query0 cold-custody-only\n",
                        closureOwners,
                        closureOwnerCeiling,
                        static_cast<unsigned long long>(retainedBytes));
                    std::printf(
                        "BINDLESS_RHI_GENERAL_DEPENDENCY_FREEZE6_OK closure8 projection5 dependencies4 initial1 final3 owners%u native-ceiling%u cpu-ceiling48 exact-less-exhausted original-owner-loss-identity caller-drop full-quota-retirement hot0-query0 cold-custody-only\n",
                        closureOwners + 1u,
                        closureOwnerCeiling + 1u);
                    std::printf(
                        "BINDLESS_RHI_GENERAL_NATIVE_PAYLOAD6_OK freeze6 closure8 commands10 calls%llu compute1 pipeline1 group1 dispatch1 dependencies4 implicit4 owners%u exact-less-exhausted canonical-native-mutation caller-drop full-quota-retirement hot0-query0 cold-custody-only\n",
                        static_cast<unsigned long long>(payloadCalls),
                        payloadOwners);
                    std::puts(
                        "BINDLESS_RHI_GENERAL_SHADER_BUFFER_CONTENT_OK Staging4 uniform16 same-original-lease native-write scopes3 queue1 completed frozen-bytes caller-drop closure8 payload6 retirement hot0 shader-execution-closed");
                    std::puts(
                        "BINDLESS_RHI_GENERAL_SHADER_TEXTURE_CONTENT_OK same-device original-upload20 mips2 completed-root-issuer whole-view Closure8 Freeze6 Payload6 explicit-CopyDst-ShaderRead initial1 final3 caller-drop hot0 committed-CopyDst shader-execution-closed");
                    std::printf(
                        "BINDLESS_RHI_GENERAL_NATIVE_COLD6_OK Payload6 Lifecycle3 ContentPlan4 commands10 native%llu owners%u cpu-ceiling48 exact-less-exhausted original-seal-reservation caller-drop final-release hot0-query0 no-encoder-or-execution-authority\n",
                        static_cast<unsigned long long>(payloadCalls),
                        coldOwners);
                    std::printf(
                        "BINDLESS_RHI_GENERAL_NATIVE_COMMAND6_OK Cold6 Lifecycle3 ContentPlan4 commands10 owners%u ceiling%u cpu-ceiling48 exact-less-exhausted%llu sticky-claim forged-native-mutation caller-drop final-release hot0-query0 no-encoder-or-execution-authority\n",
                        commandOwners,
                        commandRequest.maxRetainedOwners,
                        static_cast<unsigned long long>(commandPeak));
                    std::printf(
                        "BINDLESS_RHI_GENERAL_NATIVE_CALLBACK6_OK Command6 Cold6 Lifecycle3 owners%u ceiling%u participants8 submission-exact%llu callback-exact%llu one-less-exhausted sticky-claim forged-native-mutation caller-drop cancel-final-release hot0-query0 no-encoder-or-execution-authority\n",
                        callbackOwners,
                        callbackRequest.maxRetainedOwners,
                        static_cast<unsigned long long>(submissionPeak),
                        static_cast<unsigned long long>(callbackPeak));
                    std::printf(
                        "BINDLESS_RHI_GENERAL_NATIVE_ISSUER6_OK Callback6 Command6 Cold6 Lifecycle3 owners%u ceiling%u cpu-ceiling48 receipts7 partial-failure-cleared-retry exact-less-exhausted sticky-claim native-receipt-mutation caller-drop cancel-final-release hot0-query0 no-encoder-or-execution-authority\n",
                        issuerOwners,
                        issuerRequest.maxRetainedOwners);
                    if (!physical && !submitGeneral && terminal == RecordingTerminal::None)
                        std::puts(
                            "BINDLESS_RHI_GENERAL_PENDING_READ_RECORDING6_OK original-seal-journal uniform2 completed-texture-read1 whole2mips CopyDst-ShaderRead state-revision-plus1 content-stamp-unchanged real-recording-scopes3 caller-drop forged-native-mutation strict-public cancel-rollback quotas-zero hot0-query0 submission-closed");
                    else if (!physical && submitGeneral && !submissionTerminated) {
                        std::puts(
                            "BINDLESS_RHI_GENERAL_SUBMISSION1_OK explicit-recorded-opt-in original-seal-journal real-recording3 submission3 queue1 same-root-commit sampled-stamp-unchanged caller-drop forged-duplicate-refused quotas-zero hot0-query0 numerical-readback-unproven public-closed");
                        std::puts(
                            "BINDLESS_RHI_GENERAL_OUTPUT_SNAPSHOT1_OK Storage-CopySrc original-issuer-root-output Prepared-Pending-refused genuine-completion ShaderWrite whole-range native-identity caller-drop quotas-zero hot0-query0 copy-map-numerical-public-closed");
                    }
                }
                std::puts(
                    "BINDLESS_RHI_GENERAL_ORIGINAL_DEPENDENCY5_OK original8 uniform1 storage1 sampled1 sampler-nonmemory1 dependencies4 initial1 final3 exact-endpoints frozen-replay less-refused hot0-query0 no-sync-freeze-native-authority");
                std::puts(
                    "BINDLESS_RHI_GENERAL_ORIGINAL_PROJECTION5_OK original8 Sources-Inputs3-State3-Stream3 uniform1 storage1 sampled1 sampler1 exact-parents ranges stages legacy-refused less-refused hot0-query0 no-native-record-submit-authority");
            }
        }
        assert(!source->liveBytes() && !inputQuota->liveBytes() && !stateQuota->liveBytes()
            && !streamQuota->liveBytes());
        for (const auto& quota : initializationQuotas)
            assert(!quota->liveBytes());
        if (finitePlan)
            std::puts(
                "BINDLESS_RHI_GENERAL_CONTENT_PLAN5_RETIRE_OK original-source-input-state-stream-initializer-quotas-zero native-public-closed");
        if (physical) {
            const auto* kind = physicalLoss == PhysicalLoss::QueueUncertain ? "queue-uncertain"
                : physicalLoss == PhysicalLoss::RecordingShutdown           ? "recording-shutdown"
                : physicalLoss == PhysicalLoss::PartialPush                 ? "partial-push"
                                                                            : "recording-exception";
            std::printf(
                "BINDLESS_RHI_GENERAL_PHYSICAL_LOSS1_OK %s actual-device-destroy original-loss-delivered genuine-callback-drain original-invalidated no-completion-no-retry quotas-zero public-closed\n",
                kind);
        } else if (submissionProbe) {
            const auto* kind = submissionTerminal == SubmissionTerminal::ThrowBefore
                ? "throw-before"
                : submissionTerminal == SubmissionTerminal::ThrowAfter    ? "throw-after"
                : submissionTerminal == SubmissionTerminal::NativeError   ? "native-error"
                : submissionTerminal == SubmissionTerminal::ReverseScopes ? "reverse-scopes"
                                                                          : "partial-scope";
            std::printf(
                "BINDLESS_RHI_GENERAL_SUBMISSION_PROBE1_OK %s original-GeneralSubmission1 invocation-observed native-queue-scopes4 genuine-drain %s quotas-zero public-closed\n",
                kind,
                submissionTerminated ? "invalidated-no-output-no-retry"
                                     : "committed-numerical-output");
        } else if (submissionTerminated) {
            const auto* kind = submissionTerminal == SubmissionTerminal::Cancel ? "cancel"
                : submissionTerminal == SubmissionTerminal::Retired             ? "retired"
                                                                                : "shutdown";
            std::printf(
                "BINDLESS_RHI_GENERAL_SUBMISSION_TERMINAL1_OK %s real-scopes3 queue1 held-native-owners original-invalidated no-output-publication no-retry no-physical-loss quotas-zero public-closed\n",
                kind);
        }
        return textureQuotas;
    }

public:
    template <class Context>
    static void verifyFiniteBindings(Context& c,
        bool sourceMode = false,
        bool wholeProfile = false,
        bool contentMode = false,
        bool renderProjection = false,
        bool graphicsStateStream = false,
        bool graphicsUsage = false,
        bool graphicsContents = false,
        bool initializedStream = false,
        bool mixedRoot = false,
        bool graphicsShaderContent = false,
        bool graphicsNativeCustody = false,
        bool graphicsDependencyFreeze = false,
        bool graphicsNativePayload = false,
        bool graphicsNativeCold = false,
        bool graphicsNativeCommand = false,
        bool graphicsNativeCallback = false,
        bool graphicsNativeIssuer = false,
        bool graphicsNativeRecording = false)
    {
        using Pipeline = std::remove_reference_t<decltype(*c.publish())>;
        constexpr bool graphics = std::is_same_v<Pipeline, rhi::RenderPipeline>;
        static_assert(graphics || std::is_same_v<Pipeline, rhi::ComputePipeline>);
        assert(!renderProjection || (graphics && sourceMode && !contentMode));
        assert(!graphicsStateStream || renderProjection);
        assert(!graphicsUsage || graphicsStateStream);
        assert(!graphicsContents
            || (graphics && sourceMode && !contentMode
                && (!renderProjection || initializedStream)));
        assert(!initializedStream
            || (graphicsContents && graphicsUsage && graphicsStateStream && renderProjection));
        assert(!mixedRoot || initializedStream);
        assert(!graphicsShaderContent || (graphics && mixedRoot));
        assert(!graphicsNativeCustody || graphicsShaderContent);
        assert(!graphicsDependencyFreeze || graphicsNativeCustody);
        assert(!graphicsNativePayload || graphicsDependencyFreeze);
        assert(!graphicsNativeCold || graphicsNativePayload);
        assert(!graphicsNativeCommand || graphicsNativeCold);
        assert(!graphicsNativeCallback || graphicsNativeCommand);
        assert(!graphicsNativeIssuer || graphicsNativeCallback);
        assert(!graphicsNativeRecording || graphicsNativeIssuer);
        const auto graphicsSubmissionTerminal =
            OriginalGeneralFiniteSubmissionTerminalFixture::selectedTerminal();
        const auto graphicsPhysicalTerminal =
            OriginalGeneralFinitePhysicalLossFixture::selectedTerminal();
        const bool graphicsPhysicalLoss =
            graphicsNativeRecording && graphicsPhysicalTerminal.has_value();
        const bool graphicsPhysicalShutdown = graphicsPhysicalLoss
            && *graphicsPhysicalTerminal
                == OriginalGeneralFinitePhysicalLossFixture::Terminal::RecordingShutdown;
        const bool graphicsLifetimeTerminal = graphicsNativeRecording
            && (OriginalGeneralFiniteSubmissionTerminalFixture::lifetimeTerminates(
                    graphicsSubmissionTerminal)
                || EM_ASM_INT({
                       // clang-format off
                        const value = new URLSearchParams(location.search).get("general-graphics-recording-terminal");
                        return value === "retired" || value === "shutdown";
                       // clang-format on
                   }));
        const bool graphicsLifetimeRetired = graphicsSubmissionTerminal
                == OriginalGeneralFiniteSubmissionTerminalFixture::Terminal::Retired
            || EM_ASM_INT({
                   // clang-format off
                    return new URLSearchParams(location.search).get("general-graphics-recording-terminal") ===
                           "retired";
                   // clang-format on
               });
        // Explicit issuer workload demand is declared before every Source publication.
        const auto graphicsIssuerOwners = graphicsNativeIssuer
            ? rhi::OriginalTypedIssuerPreparationContract::additionalOwners
            : 0u;
        const auto graphicsOutputOwners = graphicsNativeRecording
            ? rhi::OriginalGeneralOutputSnapshotContract::additionalOwnerCount
            : 0u;
        // Selected shell workload intent precedes every Source publication;
        // the existing Cold-only workload does not acquire this owner demand.
        const auto graphicsCommandOwners = graphicsNativeCommand
            ? rhi::OriginalTypedNativeColdCommandContract::additionalOwners
            : 0u;
        rhi::OriginalTypedCallbackCustodyRequest graphicsCallbackIntent;
        graphicsCallbackIntent.maxCallbackSlots = 8u;
        const auto graphicsCallbackOwners = graphicsNativeCallback
            ? rhi::OriginalTypedCallbackCustodyContract::additionalOwners(graphicsCallbackIntent)
            : 0u;
        const bool initializedBuffer = contentMode || graphicsShaderContent;
        using PipelineSource = std::conditional_t<graphics,
            rhi::OriginalCommandGraphicsPipelineSource,
            rhi::OriginalCommandComputePipelineSource>;
        const bool inputsMode = EM_ASM_INT({
            // clang-format off
                    return new URLSearchParams(location.search).has("general-finite-inputs");
            // clang-format on
        });
        const bool consumptionMode = EM_ASM_INT({
            // clang-format off
                    return new URLSearchParams(location.search).has("general-finite-consumption");
            // clang-format on
        });
        const bool stateStreamMode = EM_ASM_INT({
            // clang-format off
                    return new URLSearchParams(location.search).has("general-finite-state-stream");
            // clang-format on
        });
        const bool usageMode = EM_ASM_INT({
            // clang-format off
                    return new URLSearchParams(location.search).has("general-finite-usage");
            // clang-format on
        });
        assert(!usageMode || stateStreamMode);
        assert(!inputsMode || (sourceMode && (!contentMode || usageMode)));
        assert(!contentMode || !usageMode
            || OriginalGeneralFiniteInitializedContentBrowserFixture::selectedTerminal()
                == OriginalGeneralFiniteInitializedContentBrowserFixture::InitializationTerminal::
                    None);
        assert(!consumptionMode || inputsMode);
        assert(!stateStreamMode || consumptionMode);
        // Graphics binding/source custody is independent of the Compute-only
        // Inputs/Consumption/State/Stream composition below.
        assert(!graphics || (sourceMode && !inputsMode && !contentMode));
        auto inputQuota = (inputsMode || renderProjection)
            ? core::makeShared<core::CpuMemoryQuota>(bytes)
            : nullptr;
        const bool anyStateStream = stateStreamMode || graphicsStateStream;
        auto stateQuota = anyStateStream ? core::makeShared<core::CpuMemoryQuota>(bytes) : nullptr;
        auto streamQuota = anyStateStream ? core::makeShared<core::CpuMemoryQuota>(bytes) : nullptr;
        const auto resource = c.quotas[2];
        auto source = core::makeShared<core::CpuMemoryQuota>(bytes);
        const auto bufferType = c.layout->desc().descriptorLayouts[0].bindings[0].type;
        const bool readOnly = bufferType == rhi::DescriptorType::ReadOnlyStorageBuffer;
        assert(bufferType == rhi::DescriptorType::UniformBuffer
            || (readOnly && sourceMode && (contentMode || graphics)));
        const auto alignment = readOnly
            ? rhi_webgpu::webGpuNativeDeviceLimits().storageBufferOffsetAlignment
            : rhi_webgpu::webGpuNativeDeviceLimits().uniformBufferOffsetAlignment;
        assert(alignment && alignment <= UINT32_MAX / 3u);
        const auto extent = wholeProfile ? 16u : std::uint64_t(alignment) * 2u + 16u;
        {
            using Initialized = rhi_webgpu::WebGpuCanonicalBufferUploadSource;
            using Upload = rhi::OriginalCommandInitializedBufferStagingContract;
            Upload::Budget initialize{4u,
                true,
                1u,
                Upload::callbackCount,
                Upload::retainedOwnerBound,
                extent,
                bytes,
                bytes,
                bytes,
                bytes,
                bytes};
            initialize.shaderContent = {1u, true, bufferType, extent};
            std::array<std::shared_ptr<core::CpuMemoryQuota>, 5u> initializeQuotas;
            std::shared_ptr<const Initialized> initialized;
            bool initializationAttempted = false;
            const bool pendingContentLoss = contentMode
                && OriginalGeneralFiniteInitializedContentBrowserFixture::physicalLoss(
                    OriginalGeneralFiniteInitializedContentBrowserFixture::selectedTerminal());
            assert(!pendingContentLoss || wholeProfile);
            core::ByteBuffer initialBytes(&core::defaultCpuMemoryResource());
            if (initializedBuffer) {
                assert(sourceMode && extent <= bytes);
                initializeQuotas = {core::makeShared<core::CpuMemoryQuota>(bytes),
                    core::makeShared<core::CpuMemoryQuota>(bytes),
                    resource,
                    source,
                    core::makeShared<core::CpuMemoryQuota>(bytes)};
                {
                    core::CpuMemoryQuotaScope scope(resource);
                    initialBytes.resize(static_cast<std::size_t>(extent));
                }
                std::fill(initialBytes.begin(), initialBytes.end(), std::uint8_t{0xa7u});
                if (readOnly) {
                    const std::array<std::uint32_t, 4u> first{{1u, 2u, 3u, 4u}},
                        second{{5u, 6u, 7u, 8u}};
                    std::memcpy(
                        initialBytes.data() + (wholeProfile ? 0u : alignment), first.data(), 16u);
                    if (!wholeProfile)
                        std::memcpy(initialBytes.data() + 2u * alignment, second.data(), 16u);
                } else {
                    const std::array<float, 4u> first{{1.f, 2.f, 3.f, 4.f}},
                        second{{5.f, 6.f, 7.f, 8.f}};
                    std::memcpy(
                        initialBytes.data() + (wholeProfile ? 0u : alignment), first.data(), 16u);
                    if (!wholeProfile)
                        std::memcpy(initialBytes.data() + 2u * alignment, second.data(), 16u);
                }
                EM_ASM(
                    {
                        // clang-format off
                            const d = Module.preinitializedWebGPUDevice;
                            Module.arpFiniteContentCreate = d.createBuffer;
                            d.createBuffer = function(desc) {
                                const b = Module.arpFiniteContentCreate.call(this, desc);
                                if (desc.size === Number($0) &&
                                    (desc.usage & ($1 ? GPUBufferUsage.STORAGE : GPUBufferUsage.UNIFORM)))
                                    Module.arpFiniteContentBuffer = b;
                                return b;
                            };
                        // clang-format on
                    },
                    extent,
                    readOnly);
            }
            auto storage = texture(*c.device, source, resource, true);
            std::optional<OriginalGeneralFiniteGraphicsContentSourcesFixture>
                graphicsContentSources;
            if (graphicsContents)
                graphicsContentSources.emplace(*c.device, source, resource);
            auto sampled = graphicsContents ? graphicsContentSources->sampledSources()
                                            : texture(*c.device, source, resource, false);
            const char* id = nullptr;
            core::UniquePtr<rhi::GpuBuffer> object;
            core::UniquePtr<rhi::GpuSampler> samplerObject;
            {
                core::CpuMemoryQuotaScope scope(resource);
                rhi::BufferDesc desc;
                desc.sizeBytes = extent;
                desc.usage = initializedBuffer ? Upload::expectedUsage(initialize)
                    : readOnly                 ? rhi::ResourceUsage::StorageBuffer
                                               : rhi::ResourceUsage::UniformBuffer;
                desc.memory =
                    initializedBuffer ? rhi::MemoryUsage::CpuToGpu : rhi::MemoryUsage::GpuOnly;
                desc.initialState = rhi::ResourceState::ShaderRead;
                object = c.device->createBuffer(desc);
                assert(object);
                samplerObject = c.device->createSampler({});
                assert(samplerObject);
            }
            std::shared_ptr<const rhi::OriginalCommandBufferLease> uniform;
            if (initializedBuffer) {
                EM_ASM({
                    // clang-format off
                        Module.preinitializedWebGPUDevice.createBuffer = Module.arpFiniteContentCreate;
                        delete Module.arpFiniteContentCreate;
                    // clang-format on
                });
                const auto loss = c.device->observeOriginalDeviceNativeLoss(id);
                require(id);
                assert(loss);
                initialized = Initialized::prepareOriginal(*c.device,
                    object,
                    *loss,
                    initialBytes.data(),
                    initialBytes.size(),
                    1u,
                    initialize,
                    initializeQuotas,
                    id);
                require(id);
                assert(initialized && !object);
                uniform = initialized->originalLease();
            } else
                uniform = rhi::OriginalCommandBufferLease::prepareOriginal(
                    object, {1u, true, 2u, bytes}, source, id);
            require(id);
            assert(uniform);
            auto sampler = rhi::OriginalCommandSamplerLease::prepareOriginal(
                samplerObject, {1u, true, 2u, bytes}, source, id);
            require(id);
            assert(sampler);
            // Every lease in this original inventory is allocated in its
            // existing canonical source quota; attachment custody cannot borrow
            // a separately allocated source domain.
            const bool attachmentSources = renderProjection || graphicsContents;
            auto attachments = graphicsContents ? graphicsContentSources->attachmentSources()
                : renderProjection ? texture(*c.device, source, resource, false, false, true)
                                   : std::array<Sources::Source, 2u>{};
            std::array<Sources::Source, 8u> prefix{{uniform,
                storage[0],
                storage[1],
                sampled[0],
                sampled[1],
                sampler,
                attachments[0],
                attachments[1]}};
            const std::size_t prefixCount = attachmentSources ? 8u : 6u;
            rhi::OriginalCommandAdmissionBudget budget;
            budget.contractVersion = 1u;
            budget.prototypeOptIn = true;
            budget.maxCommands = budget.maxReferences = budget.maxInputDescriptors = 1u;
            budget.maxCpuBytes.fill(bytes);
            // CPU-root profiles intentionally author no native payload capacity.
            // The independent explicit custody workload declares its complete
            // finite inventory before publishing any Sources: prefix resources
            // plus the accepted Graphics pipeline and one descriptor. Existing
            // CPU/Compute profiles and all memory/generation ceilings stay fixed.
            if (graphicsNativeCustody)
                budget.maxNativePayloadObjects = prefixCount + 2u;
            if (graphicsNativeCold) {
                // Explicit selected workload intent, before Sources publication.
                // This changes native demand only; CPU/root/byte budgets stay fixed.
                const auto capacity =
                    rhi_webgpu::WebGpuOriginalTypedColdAdmission::lifecycleRequirements(8u);
                budget.maxNativePayloadObjects += capacity.encoders + capacity.commandBuffers;
                rhi::OriginalTypedNativeColdWorkCounts work;
                work.renderPasses = work.pipelineBinds = work.descriptorBinds = work.draws = 1u;
                work.viewports = work.scissors = work.attachmentReferences = 1u;
                work.dependencies = 6u;
                work.implicitAccesses = 8u;
                require(rhi::OriginalTypedNativeColdContract::inspectWorkTotals(
                    work, work.commands, work.nativeCalls));
                budget.maxTypedNativeCommandCalls = work.nativeCalls;
                budget.maxPendingSubmissions = capacity.capacity.pendingSubmissions;
                budget.maxCallbackTickets = capacity.capacity.callbackTickets;
                budget.maxCallbackRegistrations = capacity.capacity.callbackRegistrations;
            }
            budget.maxGraphOwners = 1u + rhi::OriginalCommandBufferLease::retainedOwnerCount
                + rhi::OriginalCommandSamplerLease::retainedOwnerCount
                + 2u * rhi::OriginalCommandTextureViewSource::completeRetainedOwnerCount;
            budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Buffer)] =
                1u;
            budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Texture)] =
                2u;
            budget.maxSourceOwners[static_cast<std::size_t>(
                rhi::CommandReferenceDomain::TextureView)] = 2u;
            budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Sampler)] =
                1u;
            if (attachmentSources) {
                ++budget.maxSourceOwners[static_cast<std::size_t>(
                    rhi::CommandReferenceDomain::Texture)];
                ++budget.maxSourceOwners[static_cast<std::size_t>(
                    rhi::CommandReferenceDomain::TextureView)];
                budget.maxGraphOwners +=
                    rhi::OriginalCommandTextureViewSource::completeRetainedOwnerCount;
            }
            if (renderProjection) {
                budget.maxCommands = 8u;
                budget.maxReferences = budget.maxInputDescriptors = 3u;
                budget.maxPassColorAttachments = budget.maxPassAttachmentReferences = 1u;
                budget.maxLabelBytes = OriginalGeneralFiniteGraphicsInputsFixture::passTextBytes;
                budget.maxDynamicOffsets = 1u;
                // The original attachment parent/view plus Scalars2, Inputs1
                // and one immutable render-pass argument owner.
                budget.maxGraphOwners += 4u;
                if (graphicsStateStream) {
                    const auto stateBudget =
                        OriginalGeneralFiniteGraphicsStateStreamFixture::stateBudget(
                            graphicsUsage, initializedStream);
                    budget.maxStateObservations = stateBudget.maxStateObservations;
                    budget.maxTextureRanges = stateBudget.maxTextureRanges;
                    if (graphicsUsage) {
                        budget.maxCommands = initializedStream ? 14u : 12u;
                        budget.maxReferences = budget.maxInputDescriptors =
                            initializedStream ? 9u : 7u;
                    }
                    // State and Stream plus the original registration closure:
                    // ten canonical sources and the layout/descriptor arena.
                    budget.maxGraphOwners +=
                        2u + rhi::OriginalResourceRegistrationContract::fixedOwnerCount + 10u + 2u;
                    if (mixedRoot) {
                        // Explicit new workload intent: one original transaction
                        // and two distinct content controls beyond the dry Stream.
                        budget.maxStateTransactions = 1u;
                        budget.maxGraphOwners += 3u;
                    }
                }
            }
            auto sources = Sources::prepareOriginal(prefix.data(), prefixCount, budget, source, id);
            require(id);
            assert(sources);
            auto pipeline = rhi::OriginalRegisteredPipeline::shareAcceptedOriginal(c.publish(), id);
            require(id);
            assert(pipeline);
            std::shared_ptr<const PipelineSource> adapter;
            if (sourceMode) {
                budget.maxSourceOwners[static_cast<std::size_t>(
                    rhi::CommandReferenceDomain::Pipeline)] = 1u;
                budget.maxSourceOwners[static_cast<std::size_t>(
                    rhi::CommandReferenceDomain::Descriptor)] = 1u;
                budget.maxGraphOwners += PipelineSource::exclusiveOwnerCount
                    + rhi::OriginalCommandDescriptorSetSource::finiteExclusiveRetainedOwnerCount;
                if (inputsMode) {
                    budget.maxCommands = 6u;
                    budget.maxReferences = budget.maxInputDescriptors = 2u;
                    // The static whole negative also constructs one extra
                    // scalar offset, then requires Inputs6 to reject its shape.
                    // Author this capacity before Sources publication; accepted
                    // whole inputs still freeze and consume zero offsets.
                    budget.maxDynamicOffsets = 1u;
                    budget.maxGraphOwners += 4u;
                    if (stateStreamMode) {
                        budget.maxStateObservations = 3u;
                        budget.maxTextureRanges = 2u;
                        if (usageMode) {
                            budget.maxCommands = 9u;
                            budget.maxReferences = budget.maxInputDescriptors = 5u;
                            budget.maxStateObservations = 5u;
                            budget.maxTextureRanges = 4u;
                        }
                        // State/Stream roots, the fixed device registry closure,
                        // eight canonical resource records, and their pipeline
                        // layout/descriptor arena records. CPU source owners
                        // above do not include these registration owners.
                        constexpr auto stateStreamRoots = 2u, canonicalRegistrations = 8u,
                                       associatedRegistrations = 2u;
                        budget.maxGraphOwners += stateStreamRoots
                            + rhi::OriginalResourceRegistrationContract::fixedOwnerCount
                            + canonicalRegistrations + associatedRegistrations;
                    }
                }
                adapter = PipelineSource::prepareOriginal(pipeline,
                    {1u,
                        true,
                        PipelineSource::kind,
                        pipeline->originalResourceRegistration().generation(),
                        bytes,
                        PipelineSource::exclusiveOwnerCount},
                    source,
                    *c.device,
                    id);
                require(id);
                assert(adapter);
            }
            shader::TargetBufferMemoryLayout bufferMemory;
            if constexpr (graphics) {
                require(shader::SingleEntryShaderArtifact::originalTargetBufferMemory(
                    pipeline->originalGraphicsCreation()->originalInput()->artifact()->fragment(),
                    0u,
                    0u,
                    bufferMemory));
            } else {
                require(shader::SingleEntryShaderArtifact::originalTargetBufferMemory(
                    pipeline->originalComputeCreation()->originalInput()->artifact(),
                    0u,
                    0u,
                    bufferMemory));
            }
            assert(bufferMemory.target.minimumBindingBytes > 1u);
            std::shared_ptr<rhi::DescriptorArena> arena;
            core::UniquePtr<rhi::DescriptorSet> set;
            {
                core::CpuMemoryQuotaScope scope(resource);
                rhi::DescriptorArenaDesc desc;
                desc.maxSets = 1u;
                desc.maxDescriptors = 4u;
                arena = core::shareUnique(c.device->createOriginalDescriptorArena(desc));
                assert(arena);
                rhi::DescriptorSetDesc sd;
                sd.layout = rhi::DescriptorStateCopy::copy(c.layout->desc().descriptorLayouts[0]);
                set = c.device->allocateOriginalDescriptorSet(arena, sd);
                assert(set);
            }
            std::array<rhi::OriginalCommandDescriptorSetElement, 4u> elements;
            for (std::size_t i = 0u; i < 4u; ++i) {
                auto& e = elements[i];
                e.write.binding = static_cast<std::uint32_t>(i);
                e.sourceIndex = i == 0u ? 0u : i == 1u ? 2u : i == 2u ? 4u : 5u;
                e.write.resource = Sources::resource(sources->sources()[e.sourceIndex]);
                e.write.type = c.layout->desc().descriptorLayouts[0].bindings[i].type;
                e.write.resourceKind = i == 0u ? rhi::DescriptorResourceKind::Buffer
                    : i == 3u                  ? rhi::DescriptorResourceKind::Sampler
                                               : rhi::DescriptorResourceKind::TextureView;
                e.write.resourceState = i == 1u ? rhi::ResourceState::ShaderWrite
                    : i == 3u                   ? rhi::ResourceState::Undefined
                                                : rhi::ResourceState::ShaderRead;
                e.stages = graphics ? rhi::PipelineStage::FragmentShader
                                    : rhi::PipelineStage::ComputeShader;
                e.access = i == 1u ? rhi::ResourceAccess::ShaderWrite
                    : i == 3u      ? rhi::ResourceAccess::None
                                   : rhi::ResourceAccess::ShaderRead;
                const auto owner =
                    rhi::OriginalCommandDescriptorDependencies::capture(*sources, e.sourceIndex);
                assert(owner);
                e.registrationGeneration = std::visit(
                    [](const auto& lease) {
                        return lease->originalObject()->originalResourceRegistration().generation();
                    },
                    *owner);
            }
            if (!wholeProfile)
                elements[0].write.bufferBindingRange = {2u, alignment, 16u};
            using Lowering = rhi_webgpu::WebGpuOriginalGeneralBindingLowering;
            const auto check = [&](std::uint32_t version, const auto& e) {
                WGPUBindGroupEntry entry{};
                std::uint32_t group = 0u;
                const auto* error = Lowering::lower(
                    {version, true}, *c.device, pipeline, sources, 0u, e, entry, group);
                if (error)
                    assert(!entry.buffer && !entry.textureView && !entry.sampler && !entry.offset
                        && !entry.size && !group);
                else if (e.write.type == bufferType)
                    assert(entry.buffer && entry.offset == (wholeProfile ? 0u : alignment)
                        && entry.size == 16u && entry.binding == 0u && group == 0u);
                return error;
            };
            // Observe the arguments of every genuine candidate creation, including
            // rejected replacements. This hook never creates its own native work.
            EM_ASM({
                // clang-format off
                    const d = Module.preinitializedWebGPUDevice;
                    Module.arpGeneralFiniteCreate = d.createBindGroup;
                    Module.arpGeneralFiniteEntries = [];
                    d.createBindGroup = function(desc) {
                        const e = desc.entries.find(e => e.binding === 0);
                        if (e && e.resource && e.resource.buffer)
                            Module.arpGeneralFiniteEntries.push([ e.resource.offset, e.resource.size ]);
                        return Module.arpGeneralFiniteCreate.call(this, desc);
                    };
                // clang-format on
            });
            for (const auto displacement : std::array<std::uint32_t, 2u>{{0u, alignment}}) {
                if (wholeProfile && displacement)
                    continue;
                // One independently selected /6 transaction per original
                // single-generation attachment. Static/dynamic and rollback
                // terminal coverage run in separate browser cases.
                if (mixedRoot && !wholeProfile) {
                    const bool dynamic = EM_ASM_INT({
                        // clang-format off
                            return new URLSearchParams(location.search).has("general-content-plan-dynamic");
                        // clang-format on
                    });
                    if ((displacement != 0u) != dynamic)
                        continue;
                }
                auto& e = elements[0];
                e.dynamicOffset = displacement;
                e.bufferAccessOffset = wholeProfile ? 0u : std::uint64_t(alignment) + displacement;
                e.bufferAccessBytes = 16u;
                require(check(2u, e));
                if (wholeProfile)
                    require(check(1u, e));
                else
                    assert(check(1u, e));
                for (std::uint32_t field = 0u; field < 6u; ++field) {
                    auto bad = e;
                    switch (field) {
                    case 0u:
                        ++bad.write.bufferBindingRange.offsetBytes;
                        ++bad.bufferAccessOffset;
                        break;
                    case 1u:
                        bad.dynamicOffset = 1u;
                        bad.bufferAccessOffset = std::uint64_t(alignment) + 1u;
                        break;
                    case 2u:
                        bad.write.bufferBindingRange.sizeBytes =
                            bufferMemory.target.minimumBindingBytes - 1u;
                        bad.bufferAccessBytes = bad.write.bufferBindingRange.sizeBytes;
                        break;
                    case 3u:
                        bad.dynamicOffset = alignment * 2u;
                        bad.bufferAccessOffset = std::uint64_t(alignment) * 3u;
                        break;
                    case 4u:
                        --bad.bufferAccessBytes;
                        break;
                    case 5u:
                        bad.write.bufferBindingRange = wholeProfile
                            ? rhi::DescriptorBufferBindingRange{2u, 0u, 12u}
                            : rhi::DescriptorBufferBindingRange{};
                        break;
                    }
                    assert(check(2u, bad));
                }
                OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                    assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                        std::array{source}, [&]() noexcept {
                            return check(2u, e);
                        }));
                });
                const auto retained = uniform->originalObject()->allocation().estimatedSizeBytes
                    + std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(storage[0])
                          ->originalObject()
                          ->allocation()
                          .estimatedSizeBytes
                    + std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(sampled[0])
                          ->originalObject()
                          ->allocation()
                          .estimatedSizeBytes;
                OriginalGeneralDescriptorWriteFixture::verifyActual(
                    *c.device, pipeline, sources, *set, elements, retained, 2u);
                if (sourceMode) {
                    sources.reset();
                    auto typedSources = Sources::prepareColdOriginalTyped(
                        prefix.data(), prefixCount, budget, {5u, true, 2u}, source, id);
                    require(id);
                    assert(typedSources);
                    require(Sources::appendOriginalTyped(typedSources, {adapter}, *c.device));
                    auto acceptedSource =
                        OriginalGeneralDescriptorSourceBrowserFixture::verifyActual(*c.device,
                            pipeline,
                            typedSources,
                            arena,
                            set,
                            elements,
                            resource,
                            retained,
                            prefixCount);
                    auto descriptor = std::move(acceptedSource.first);
                    assert(descriptor && !set);
                    if (graphicsContents)
                        graphicsContentSources->verifyAssociation(descriptor, *c.device);
                    if (graphicsShaderContent) {
                        // GeneralFiniteInitializedContent/1 already authenticates
                        // stage-matched full views. Reuse its actual Staging4
                        // source and original callbacks before capturing Graphics.
                        OriginalGeneralFiniteInitializedContentBrowserFixture::verifyActual(
                            *c.device,
                            initialized,
                            descriptor,
                            initialBytes,
                            initializationAttempted,
                            initializeQuotas);
                        assert(descriptor->frozenElements()[0].declaration.stages
                            == rhi::PipelineStage::FragmentShader);
                        std::printf(
                            "BINDLESS_RHI_GENERAL_GRAPHICS_SHADER_BUFFER_INITIALIZED_OK %s %s original-Staging4 Source5 Fragment frozen-backing scopes3 queue1 callbacks-drained hot0-query0 no-draw\n",
                            readOnly ? "readonly-storage" : "uniform",
                            wholeProfile       ? "whole"
                                : displacement ? "dynamic"
                                               : "static");
                    }
                    std::shared_ptr<const rhi::OriginalCommandInputs> inputs;
                    if (inputsMode)
                        inputs = OriginalGeneralFiniteInputsFixture::verifyActual(
                            typedSources, descriptor, inputQuota, usageMode);
                    if constexpr (graphics)
                        if (graphicsUsage) {
                            // The same authored Source ceiling can admit the genuine
                            // eight-command input under exact independent State/Stream
                            // bounds. It must refuse dependency authority without barriers.
                            auto emptyInputs =
                                OriginalGeneralFiniteGraphicsInputsFixture::verifyActual(
                                    typedSources,
                                    descriptor,
                                    *c.device,
                                    inputQuota,
                                    false,
                                    initializedStream);
                            auto emptyState =
                                OriginalGeneralFiniteGraphicsStateStreamFixture::verifyActual(
                                    emptyInputs,
                                    *c.device,
                                    stateQuota,
                                    streamQuota,
                                    false,
                                    initializedStream);
                            OriginalGeneralFiniteGraphicsUsageFixture::verifyEmpty(
                                emptyState, initializedStream);
                            emptyState = {};
                            emptyInputs.reset();
                            assert(!stateQuota->liveBytes() && !streamQuota->liveBytes()
                                && !inputQuota->liveBytes());
                        }
                    if constexpr (graphics)
                        if (renderProjection)
                            inputs = OriginalGeneralFiniteGraphicsInputsFixture::verifyActual(
                                typedSources,
                                descriptor,
                                *c.device,
                                inputQuota,
                                graphicsUsage,
                                initializedStream);
                    if (consumptionMode)
                        OriginalGeneralFiniteConsumptionFixture::verifyActual(
                            inputs, *c.device, inputQuota, usageMode);
                    OriginalGeneralFiniteStateStreamFixture::Retained stateStream;
                    if constexpr (graphics)
                        if (graphicsStateStream)
                            stateStream =
                                OriginalGeneralFiniteGraphicsStateStreamFixture::verifyActual(
                                    inputs,
                                    *c.device,
                                    stateQuota,
                                    streamQuota,
                                    graphicsUsage,
                                    initializedStream);
                    if constexpr (graphics)
                        if (graphicsUsage)
                            OriginalGeneralFiniteGraphicsUsageFixture::verifyActual(
                                stateStream, *c.device, initializedStream);
                    if (initializedStream)
                        graphicsContentSources->verifyCapturedContent();
                    if constexpr (graphics)
                        if (mixedRoot) {
                            using GraphicsFreeze =
                                rhi_webgpu::WebGpuOriginalTypedDependencyDecision;
                            std::shared_ptr<const GraphicsFreeze> heldGraphicsFreeze;
                            std::shared_ptr<const rhi_webgpu::WebGpuOriginalTypedNativePayload>
                                heldGraphicsPayload;
                            std::uint64_t freezeBaseline = 0u;
                            bool coldCandidateDone = false;
                            const auto inspectCustodyWithRebuild = [&](auto& root,
                                                                       const auto& stream,
                                                                       auto&& rebuildRoot) {
                                if (!graphicsNativeCustody)
                                    return;
                                if (graphicsNativeCold && coldCandidateDone)
                                    return;
                                const auto loss = c.device->observeOriginalDeviceNativeLoss(id);
                                require(id);
                                assert(loss);
                                const auto textureInput =
                                    graphicsContentSources->completedShaderInput();
                                const auto textureOwners = textureInput.issuer->retainedOwnerCount()
                                    + rhi_webgpu::WebGpuCanonicalTextureContentUploadSource::
                                        retainedOwnerBound
                                    + 1u;
                                const auto sourceCount = typedSources->sources().size();
                                const auto ownerCeiling = root->retainedOwnerCount() + 3u
                                    + sourceCount
                                        * (1u
                                            + rhi::OriginalWholeDescriptorWriteContract::quotaCount)
                                    + Initialized::initializedIndexOwnerBound + textureOwners
                                    + graphicsCommandOwners + graphicsCallbackOwners
                                    + graphicsIssuerOwners + graphicsOutputOwners;
                                const auto retainedBytes = retained
                                    + std::get<
                                        std::shared_ptr<const rhi::OriginalCommandTextureLease>>(
                                        attachments[0])
                                          ->originalObject()
                                          ->allocation()
                                          .estimatedSizeBytes
                                    + textureInput.source->originalStaging()
                                          ->initializedBuffer()
                                          ->originalLease()
                                          ->originalObject()
                                          ->allocation()
                                          .estimatedSizeBytes;
                                const auto baseline = root->originalMetadataQuota()->liveBytes();
                                auto closure =
                                    OriginalGeneralFiniteNativeClosureFixture::verifyActual(root,
                                        stream,
                                        *c.device,
                                        *loss,
                                        initialized,
                                        textureInput,
                                        retainedBytes,
                                        static_cast<std::uint32_t>(ownerCeiling),
                                        true);
                                if (graphicsDependencyFreeze) {
                                    freezeBaseline = baseline;
                                    heldGraphicsFreeze =
                                        OriginalGeneralFiniteNativeProjectionFixture::
                                            verifyGraphicsFreezeActual(
                                                closure, static_cast<std::uint32_t>(ownerCeiling));
                                    require(heldGraphicsFreeze->compareOriginalClosure(closure));
                                    if (graphicsNativePayload)
                                        heldGraphicsPayload =
                                            OriginalGeneralFiniteNativeProjectionFixture::
                                                verifyGraphicsPayloadActual(heldGraphicsFreeze,
                                                    std::array{root->originalMetadataQuota(),
                                                        stateQuota,
                                                        streamQuota,
                                                        inputQuota});
                                }
                                if (graphicsNativeCold) {
                                    std::array<std::uint64_t, 2u> callbackQuotaCreationBaseline{};
                                    auto cold = OriginalTypedColdAdmissionFixture::verifyGraphics8(
                                        heldGraphicsPayload,
                                        *c.device,
                                        {},
                                        {},
                                        graphicsNativeCallback ? &callbackQuotaCreationBaseline
                                                               : nullptr);
                                    auto command = graphicsNativeCommand
                                        ? OriginalTypedColdCommandFixture::verifyComposed(
                                              cold, true)
                                        : std::shared_ptr<
                                              const rhi_webgpu::WebGpuOriginalTypedColdCommand>{};
                                    std::array<std::shared_ptr<core::CpuMemoryQuota>, 2u>
                                        callbackQuotas;
                                    std::array<std::uint64_t, 2u> callbackQuotaBaseline{};
                                    if (graphicsNativeCallback) {
                                        const auto& seal =
                                            cold->originalReservation()->originalSeal();
                                        callbackQuotas = {seal->originalSubmissionQuota(),
                                            seal->originalCallbackQuota()};
                                        for (std::size_t i = 0u; i < callbackQuotas.size(); ++i)
                                            callbackQuotaBaseline[i] =
                                                callbackQuotas[i]->liveBytes();
                                    }
                                    auto custody = graphicsNativeCallback
                                        ? OriginalTypedCallbackCustodyFixture::verifyGraphics8(
                                              command, graphicsIssuerOwners + graphicsOutputOwners)
                                        : std::shared_ptr<const rhi_webgpu::
                                                  WebGpuOriginalTypedCallbackCustody>{};
                                    std::shared_ptr<
                                        const rhi_webgpu::WebGpuOriginalTypedIssuerPreparation>
                                        typedIssuers;
                                    if (graphicsNativeIssuer) {
                                        const auto& seal =
                                            cold->originalReservation()->originalSeal();
                                        const auto recordingQuota = root->originalMetadataQuota();
                                        const rhi::OriginalTypedCallbackCustodyRequest
                                            issuerCallbackRequest{8u,
                                                true,
                                                rhi::OriginalTypedLossObserverPolicy::
                                                    RetainPhysicalLossUntilLocalDrain1,
                                                1u,
                                                8u,
                                                1u,
                                                command->retainedOwnerCount()
                                                    + rhi::OriginalTypedCallbackCustodyContract::
                                                        additionalOwners(
                                                            rhi::OriginalTypedCallbackCustodyRequest{
                                                                8u,
                                                                true,
                                                                rhi::OriginalTypedLossObserverPolicy::
                                                                    RetainPhysicalLossUntilLocalDrain1,
                                                                1u,
                                                                8u,
                                                                1u})
                                                    + graphicsIssuerOwners + graphicsOutputOwners,
                                                seal->originalSubmissionQuota()->limitBytes(),
                                                seal->originalCallbackQuota()->limitBytes()};
                                        const auto rebuildIssuerChain = [&] {
                                            // verifyGeneral already drops issuer/callback; release every remaining old root owner.
                                            assert(!typedIssuers && !custody);
                                            command.reset();
                                            cold.reset();
                                            heldGraphicsPayload.reset();
                                            heldGraphicsFreeze.reset();
                                            closure.reset();
                                            root.reset();
                                            rebuildRoot();
                                            closure = OriginalGeneralFiniteNativeClosureFixture::
                                                verifyActual(root,
                                                    stream,
                                                    *c.device,
                                                    *loss,
                                                    initialized,
                                                    textureInput,
                                                    retainedBytes,
                                                    static_cast<std::uint32_t>(ownerCeiling),
                                                    true);
                                            heldGraphicsFreeze =
                                                OriginalGeneralFiniteNativeProjectionFixture::
                                                    verifyGraphicsFreezeActual(closure,
                                                        static_cast<std::uint32_t>(ownerCeiling));
                                            heldGraphicsPayload =
                                                OriginalGeneralFiniteNativeProjectionFixture::
                                                    verifyGraphicsPayloadActual(heldGraphicsFreeze,
                                                        std::array{root->originalMetadataQuota(),
                                                            stateQuota,
                                                            streamQuota,
                                                            inputQuota});
                                            cold =
                                                OriginalTypedColdAdmissionFixture::verifyGraphics8(
                                                    heldGraphicsPayload,
                                                    *c.device,
                                                    callbackQuotas[0],
                                                    callbackQuotas[1]);
                                            command =
                                                OriginalTypedColdCommandFixture::verifyComposed(
                                                    cold, true);
                                            assert(command
                                                && root->originalMetadataQuota() == recordingQuota);
                                        };
                                        OriginalTypedIssuerPreparationFixture::verifyGeneral(
                                            command,
                                            custody,
                                            typedIssuers,
                                            issuerCallbackRequest,
                                            recordingQuota,
                                            callbackQuotas[0],
                                            callbackQuotas[1],
                                            rebuildIssuerChain);
                                        require(typedIssuers->validate());
                                        assert(typedIssuers->issuersClosedForInspection()
                                            && !typedIssuers->nativeExecutionProven());
                                        assert(typedIssuers->retainedOwnerCount()
                                            == command->retainedOwnerCount()
                                                + rhi::OriginalTypedCallbackCustodyContract::
                                                    additionalOwners(issuerCallbackRequest)
                                                + graphicsIssuerOwners);
                                        std::puts(
                                            "BINDLESS_RHI_GENERAL_GRAPHICS_ISSUER8_PREPARE_OK Callback8 Command8 Cold8 Lifecycle5 ContentPlan6 explicit-owner-add3 original-root-recapture exact-less-exhausted partial-rollback sticky-claim native-receipts7 no-record-submit-output");
                                    }
                                    if (graphicsNativeRecording) {
                                        using OutputC = rhi::OriginalGeneralOutputSnapshotContract;
                                        using OutputS =
                                            rhi_webgpu::WebGpuOriginalGeneralOutputSnapshot;
                                        using UsageP = rhi::common::OriginalTypedUsageProjection;
                                        const auto output = std::get<std::shared_ptr<
                                            const rhi::OriginalCommandTextureLease>>(
                                            attachments[0]);
                                        const auto usageIntent =
                                            OriginalGeneralFiniteGraphicsUsageFixture::budget(
                                                true, true);
                                        const rhi::OriginalTypedStateBoundUsageContract::Request
                                            outputUsage{6u, true, usageIntent};
                                        OutputC::Budget outputIntent;
                                        outputIntent.contractVersion = 3u;
                                        outputIntent.prototypeOptIn = true;
                                        outputIntent.provenance =
                                            OutputC::Provenance::QueueOrderedSnapshot;
                                        outputIntent.range = rhi::defaultTextureSubresourceRange(
                                            output->originalObject()->desc());
                                        outputIntent.maxRetainedOwners =
                                            typedIssuers->retainedOwnerCount()
                                            + OutputC::additionalOwnerCount;
                                        outputIntent.maxJoinVisits =
                                            usageIntent.maxCommands + usageIntent.maxContributions;
                                        outputIntent.maxSourceBytes = retainedBytes;
                                        outputIntent.maxCpuMetadataBytes =
                                            output->originalMetadataQuota()->limitBytes();
                                        const auto metadataBeforeAssociation =
                                            output->originalMetadataQuota()->liveBytes();
                                        auto association = OutputS::prepareOriginal(typedIssuers,
                                            root,
                                            output,
                                            outputIntent,
                                            output->originalMetadataQuota(),
                                            id);
                                        require(id);
                                        assert(association);
                                        require(OutputS::validateOriginal(association));
                                        assert(OutputS::validateCompletedOriginal(association));
                                        auto forged = std::shared_ptr<const OutputS>(
                                            source, association.get());
                                        assert(OutputS::validateOriginal(forged));
                                        forged.reset();
                                        std::puts(
                                            "BINDLESS_RHI_GENERAL_GRAPHICS_OUTPUT3_PREPARED_OK authentic-snapshot exact-attachment completion-refused");
                                        const auto nativeSampler =
                                            rhi_webgpu::webGpuNativePayloadFor(
                                                *sampler->originalObject());
                                        assert(nativeSampler && nativeSampler->ownerDevice);
                                        const auto sources = root->originalGraph()
                                                                 ->originalInputs()
                                                                 ->originalScalars()
                                                                 ->originalUsage()
                                                                 ->originalSources();
                                        std::weak_ptr<rhi::OriginalCommandStateTransaction>
                                            rootObserver = root;
                                        std::weak_ptr<const Initialized> initializerObserver =
                                            initialized;
                                        std::weak_ptr<
                                            const rhi_webgpu::WebGpuOriginalTypedCallbackCustody>
                                            custodyObserver = custody;
                                        const auto dropCaller = [&] {
                                            heldGraphicsPayload.reset();
                                            heldGraphicsFreeze.reset();
                                            closure.reset();
                                            command.reset();
                                            cold.reset();
                                            custody.reset();
                                            root.reset();
                                            initialized.reset();
                                            assert(!rootObserver.expired()
                                                && !initializerObserver.expired());
                                        };
                                        const auto recoverCaller = [&] {
                                            root = rootObserver.lock();
                                            initialized = initializerObserver.lock();
                                            assert(root && initialized);
                                            custody = custodyObserver.lock();
                                            assert(custody);
                                        };
                                        OriginalGeneralFiniteGraphicsRecordingFixture::verifyActual(
                                            typedIssuers,
                                            custody,
                                            root,
                                            nativeSampler->ownerDevice,
                                            c.device,
                                            *textureInput.completed,
                                            std::array{root->originalMetadataQuota(),
                                                callbackQuotas[0],
                                                callbackQuotas[1]},
                                            dropCaller,
                                            recoverCaller,
                                            [&] {
                                                graphicsContentSources->verifyPendingContent();
                                                assert(OutputS::validateOriginal(association)
                                                    && OutputS::validateCompletedOriginal(
                                                        association));
                                            },
                                            [&] {
                                                require(OutputS::validateOriginal(association));
                                                require(OutputS::validateCompletedOriginal(
                                                    association));
                                                UsageP::AttachmentPoint capture{};
                                                rhi_webgpu::WebGpuTransferResourceObservation
                                                    native;
                                                require(rhi_webgpu::observeOriginalWebGpuResource(
                                                    *output->originalObject(), native));
                                                require(OutputC::inspectCompletedGraphicsAttachment<
                                                    UsageP>(root,
                                                    output,
                                                    outputIntent,
                                                    outputUsage,
                                                    typedIssuers->retainedOwnerCount(),
                                                    native.allocationBytes,
                                                    capture));
                                                assert(capture.resource == output->resource()
                                                    && capture.original.range
                                                        == outputIntent.range);
                                                assert(OutputC::inspectPreparedGraphicsAttachment<
                                                    UsageP>(root,
                                                    stream,
                                                    output,
                                                    outputIntent,
                                                    outputUsage,
                                                    typedIssuers->retainedOwnerCount(),
                                                    native.allocationBytes,
                                                    capture));
                                                auto wrong = outputIntent;
                                                wrong.maxJoinVisits = 1u;
                                                const auto frozen = capture.registrationGeneration;
                                                assert(OutputC::inspectCompletedGraphicsAttachment<
                                                    UsageP>(root,
                                                    output,
                                                    wrong,
                                                    outputUsage,
                                                    typedIssuers->retainedOwnerCount(),
                                                    native.allocationBytes,
                                                    capture));
                                                assert(capture.registrationGeneration == frozen);
                                                wrong = outputIntent;
                                                --wrong.maxRetainedOwners;
                                                assert(OutputC::inspectCompletedGraphicsAttachment<
                                                    UsageP>(root,
                                                    output,
                                                    wrong,
                                                    outputUsage,
                                                    typedIssuers->retainedOwnerCount(),
                                                    native.allocationBytes,
                                                    capture));
                                                std::puts(EM_ASM_INT({
                                                    // clang-format off
                                                        return new URLSearchParams(location.search).has("general-graphics-output-readback");
                                                    // clang-format on
                                                })
                                                        ? "BINDLESS_RHI_GENERAL_GRAPHICS_OUTPUT3_COMPLETED_OK authentic-queue-drain immutable-attachment strict-ColorAttachment forged-budget-refused readback3-ready"
                                                        : "BINDLESS_RHI_GENERAL_GRAPHICS_OUTPUT3_COMPLETED_OK authentic-queue-drain immutable-attachment strict-ColorAttachment forged-budget-refused copy-map-closed");
                                                if (EM_ASM_INT({
                                                        // clang-format off
                                                        return new URLSearchParams(location.search).has("general-graphics-output-readback");
                                                        // clang-format on
                                                    })) {
                                                    const auto& pixels =
                                                        textureInput.source->originalStaging()
                                                            ->originalPackingSource()
                                                            ->originalInput()
                                                            ->pixelBytes();
                                                    assert(pixels.size() == 20u);
                                                    const auto& sd =
                                                        sampler->originalObject()->desc();
                                                    assert(
                                                        sd.minFilter == rhi::SamplerFilter::Linear
                                                        && sd.magFilter
                                                            == rhi::SamplerFilter::Linear
                                                        && sd.addressU
                                                            == rhi::SamplerAddressMode::ClampToEdge
                                                        && sd.addressV
                                                            == rhi::SamplerAddressMode::ClampToEdge
                                                        && sd.compareOp == rhi::CompareOp::Never);
                                                    const auto reference =
                                                        core::analyzeStridedImageChannels(
                                                            pixels.data(),
                                                            pixels.size(),
                                                            2u,
                                                            2u,
                                                            8u,
                                                            4u,
                                                            4u,
                                                            [](const std::uint8_t* pixel,
                                                                std::uint32_t channel) {
                                                                return double(pixel[channel])
                                                                    / 255.0;
                                                            });
                                                    assert(reference.valid);
                                                    const auto& selected =
                                                        descriptor->frozenElements()[0].declaration;
                                                    rhi::DescriptorBufferBindingRangeContract::
                                                        Resolved interval;
                                                    require(
                                                        rhi::DescriptorBufferBindingRangeContract::
                                                            resolve(
                                                                selected.write.bufferBindingRange,
                                                                extent,
                                                                selected.dynamicOffset,
                                                                interval));
                                                    assert(interval.sizeBytes == 16u
                                                        && interval.offsetBytes + 16u
                                                            <= initialBytes.size());
                                                    std::array<std::uint16_t, 4u> expected;
                                                    for (std::size_t channel = 0u;
                                                        channel < expected.size();
                                                        ++channel) {
                                                        float value = 0.f;
                                                        if (readOnly) {
                                                            std::uint32_t word = 0u;
                                                            std::memcpy(&word,
                                                                initialBytes.data()
                                                                    + interval.offsetBytes
                                                                    + 4u * channel,
                                                                4u);
                                                            value = float(word);
                                                        } else
                                                            std::memcpy(&value,
                                                                initialBytes.data()
                                                                    + interval.offsetBytes
                                                                    + 4u * channel,
                                                                4u);
                                                        expected[channel] =
                                                            core::ibl::floatToHalf(value
                                                                + float(reference.channels[channel]
                                                                        .meanValue));
                                                    }
                                                    OriginalGeneralOutputReadbackInputsFixture::
                                                        verifyPrepared(*c.device,
                                                            association,
                                                            resource,
                                                            expected);
                                                    const auto* completionDiagnostic =
                                                        OutputS::validateCompletedOriginal(
                                                            association);
                                                    assert(completionDiagnostic);
                                                    using Readback =
                                                        OriginalGeneralOutputReadbackInputsFixture;
                                                    const auto terminal =
                                                        Readback::selectedMapTerminal();
                                                    // Readback cancellation invalidates current
                                                    // attachment state, not the completed draw's
                                                    // original identity. Physical loss invalidates
                                                    // both. Neither case grants another readback.
                                                    if (Readback::mapUsesPhysicalLoss(terminal))
                                                        assert(
                                                            OutputS::validateOriginal(association));
                                                    else
                                                        require(
                                                            OutputS::validateOriginal(association));
                                                    if (terminal == Readback::MapTerminal::Drop) {
                                                        const auto* originalStateDiagnostic =
                                                            association->originalRoot()->validate();
                                                        assert(originalStateDiagnostic
                                                            && core::StringView(
                                                                   completionDiagnostic)
                                                                == originalStateDiagnostic);
                                                        assert(!output->originalObject()
                                                                ->observeOriginalCommittedState(
                                                                    outputIntent.range));
                                                        std::printf(
                                                            "BINDLESS_RHI_GENERAL_GRAPHICS_OUTPUT3_DROP_STATE_REFUSED %s\n",
                                                            completionDiagnostic);
                                                    }
                                                    if (terminal == Readback::MapTerminal::None)
                                                        std::puts(
                                                            "BINDLESS_RHI_GENERAL_GRAPHICS_OUTPUT3_READBACK_OK same-snapshot ColorAttachment-CopySrc canonical-copy1-map1 published-half4 strict-upstream-stale-refused private-only");
                                                    else
                                                        std::printf(
                                                            "BINDLESS_RHI_GENERAL_GRAPHICS_OUTPUT3_TERMINAL_OK %s same-snapshot canonical-copy1-map1 %s %s strict-upstream-stale-refused private-only\n",
                                                            Readback::mapTerminalName(terminal),
                                                            Readback::mapCompletes(terminal)
                                                                ? "published-half4"
                                                                : "invalidated-unpublished-no-retry",
                                                            Readback::mapUsesPhysicalLoss(terminal)
                                                                ? "actual-device-loss"
                                                                : "no-device-loss");
                                                }
                                                association.reset();
                                                assert(output->originalMetadataQuota()->liveBytes()
                                                    == metadataBeforeAssociation);
                                            },
                                            [&] {
                                                const auto status = root->status();
                                                const bool droppedRecording = EM_ASM_INT({
                                                    // clang-format off
                                                        const p = new URLSearchParams(location.search);
                                                        return !p.has("general-graphics-native-submission") &&
                                                               p.get("general-graphics-recording-terminal") === "drop";
                                                    // clang-format on
                                                });
                                                assert(
                                                    (status
                                                            == rhi::
                                                                OriginalCommandStateTransaction::
                                                                    Status::Invalidated
                                                        || status
                                                            == rhi::
                                                                OriginalCommandStateTransaction::
                                                                    Status::Abandoned
                                                        || (droppedRecording
                                                            && status
                                                                == rhi::
                                                                    OriginalCommandStateTransaction::
                                                                        Status::Pending))
                                                    && OutputS::validateOriginal(association)
                                                    && OutputS::validateCompletedOriginal(
                                                        association));
                                                association.reset();
                                                assert(output->originalMetadataQuota()->liveBytes()
                                                    == metadataBeforeAssociation);
                                                if (status
                                                        == rhi::OriginalCommandStateTransaction::
                                                            Status::Invalidated
                                                    || graphicsLifetimeTerminal)
                                                    std::puts(
                                                        "BINDLESS_RHI_GENERAL_GRAPHICS_OUTPUT3_FAILED_CANDIDATE_RETIRE_OK original-snapshot-invalidated issuer-owner-released no-readback no-retry");
                                            });
                                        custodyObserver.reset();
                                        for (std::size_t i = 0u; i < callbackQuotas.size(); ++i)
                                            assert(callbackQuotas[i]->liveBytes()
                                                == callbackQuotaCreationBaseline[i]);
                                        assert(
                                            root->originalMetadataQuota()->liveBytes() == baseline
                                            && !sources->originalReservedSubmissionCount()
                                            && !sources->originalReservedCallbackTicketCount()
                                            && !sources
                                                ->originalReservedCallbackRegistrationCount());
                                        if (EM_ASM_INT({
                                                // clang-format off
                                                return new URLSearchParams(location.search).has("general-graphics-native-submission");
                                                // clang-format on
                                            }))
                                            std::puts(EM_ASM_INT({
                                                // clang-format off
                                                    return new URLSearchParams(location.search).has("general-graphics-output-readback");
                                                // clang-format on
                                            })
                                                    ? "BINDLESS_RHI_GENERAL_GRAPHICS_SUBMISSION3_RETIRE_OK original-reservations0 quota-baseline readback3 no-public-authority"
                                                    : "BINDLESS_RHI_GENERAL_GRAPHICS_SUBMISSION3_RETIRE_OK original-reservations0 quota-baseline no-readback no-public-authority");
                                        else
                                            std::puts(
                                                "BINDLESS_RHI_GENERAL_GRAPHICS_RECORDING8_RETIRE_OK original-reservations0 quota-baseline no-output no-public-authority");
                                        coldCandidateDone = true;
                                        return;
                                    }
                                    std::weak_ptr<const rhi_webgpu::WebGpuOriginalTypedColdCommand>
                                        commandObserver = command;
                                    const auto sources = root->originalGraph()
                                                             ->originalInputs()
                                                             ->originalScalars()
                                                             ->originalUsage()
                                                             ->originalSources();
                                    std::weak_ptr<rhi::OriginalCommandStateTransaction>
                                        rootObserver = root;
                                    std::weak_ptr<const Initialized> initializerObserver =
                                        initialized;
                                    heldGraphicsPayload.reset();
                                    heldGraphicsFreeze.reset();
                                    closure.reset();
                                    if (command)
                                        cold.reset();
                                    if (custody)
                                        command.reset();
                                    root.reset();
                                    initialized.reset();
                                    assert(
                                        !rootObserver.expired() && !initializerObserver.expired());
                                    if (typedIssuers) {
                                        std::weak_ptr<
                                            const rhi_webgpu::WebGpuOriginalTypedCallbackCustody>
                                            custodyObserver = custody;
                                        custody.reset();
                                        assert(!custodyObserver.expired());
                                        require(typedIssuers->validate());
                                        custody = custodyObserver.lock();
                                        std::puts(
                                            "BINDLESS_RHI_GENERAL_GRAPHICS_ISSUER8_RETAIN_OK issuer-only-original-callback-command-root-initializers-retained no-native-execution");
                                    }
                                    require(custody   ? custody->validate()
                                            : command ? command->validate()
                                                      : cold->validate());
                                    if (custody) {
                                        assert(!commandObserver.expired()
                                            && custody->physicalLossArmed()
                                            && custody->pendingOriginalRecords() == 1u);
                                        for (std::size_t i = 0u; i < callbackQuotas.size(); ++i)
                                            assert(callbackQuotas[i]->liveBytes()
                                                > callbackQuotaBaseline[i]);
                                        std::puts(
                                            "BINDLESS_RHI_GENERAL_GRAPHICS_CALLBACK8_RETAIN_OK same-original-Prepared-root Command8-Cold8-caller-drop original-observer-seal-reservation-initializers-retained pending1 armed1 no-native-execution");
                                    }
                                    std::puts(
                                        "BINDLESS_RHI_GENERAL_GRAPHICS_COLD8_RETAIN_OK same-original-Prepared-root payload-freeze-closure-initializer-seal-reservation-caller-drop sealed-CPU-refused no-record-or-completion");
                                    if (graphicsNativeCommand)
                                        std::puts(
                                            "BINDLESS_RHI_GENERAL_GRAPHICS_COMMAND8_RETAIN_OK same-original-Prepared-root original-Cold8-caller-drop payload-freeze-closure-initializer-seal-reservation-retained sealed-CPU-refused no-encoder-or-completion");
                                    root = rootObserver.lock();
                                    initialized = initializerObserver.lock();
                                    if (custody)
                                        command = commandObserver.lock();
                                    heldGraphicsPayload = command
                                        ? command->originalAdmission()->originalPayload()
                                        : cold->originalPayload();
                                    heldGraphicsFreeze = heldGraphicsPayload->originalDecision();
                                    if (custody) {
                                        require(custody->cancelOriginal());
                                        if (typedIssuers) {
                                            assert(typedIssuers->validate()
                                                && !typedIssuers->nativeExecutionProven());
                                            OriginalTypedIssuerPreparationFixture::
                                                verifyGeneralReceiptsRetired();
                                        }
                                        assert(!custody->pendingOriginalRecords()
                                            && !custody->physicalLossArmed()
                                            && !custody->physicalLossDelivered());
                                        assert(custody->validate());
                                        const auto& seal = command->originalAdmission()
                                                               ->originalReservation()
                                                               ->originalSeal();
                                        const rhi::OriginalTypedCallbackCustodyRequest
                                            retiredRequest{8u,
                                                true,
                                                rhi::OriginalTypedLossObserverPolicy::
                                                    RetainPhysicalLossUntilLocalDrain1,
                                                1u,
                                                8u,
                                                1u,
                                                command->originalRequest().maxRetainedOwners,
                                                seal->originalSubmissionQuota()->limitBytes(),
                                                seal->originalCallbackQuota()->limitBytes()};
                                        assert(!rhi_webgpu::WebGpuOriginalTypedCallbackCustody::
                                                prepareOriginal(command, retiredRequest, id));
                                        assert(id
                                            && core::StringView(id)
                                                == "RHI-WEBGPU-TYPED-CALLBACK-DUPLICATE");
                                        typedIssuers.reset();
                                        custody.reset();
                                        assert(command && !commandObserver.expired());
                                        for (std::size_t i = 0u; i < callbackQuotas.size(); ++i)
                                            assert(callbackQuotas[i]->liveBytes()
                                                == callbackQuotaBaseline[i]);
                                    }
                                    command.reset();
                                    cold.reset();
                                    commandObserver.reset();
                                    if (graphicsNativeCallback)
                                        for (std::size_t i = 0u; i < callbackQuotas.size(); ++i)
                                            assert(callbackQuotas[i]->liveBytes()
                                                == callbackQuotaCreationBaseline[i]);
                                    assert(root->status()
                                            == rhi::OriginalCommandStateTransaction::Status::
                                                Abandoned
                                        && root->journalEmpty()
                                        && !sources->originalReservedSubmissionCount()
                                        && !sources->originalReservedCallbackTicketCount()
                                        && !sources->originalReservedCallbackRegistrationCount());
                                    OriginalTypedNativePayloadFixture::verifyUnavailable(
                                        heldGraphicsPayload, "graphics-Cold-abandoned-root");
                                    heldGraphicsPayload.reset();
                                    heldGraphicsFreeze.reset();
                                    assert(root->originalMetadataQuota()->liveBytes() == baseline);
                                    std::puts(
                                        "BINDLESS_RHI_GENERAL_GRAPHICS_COLD8_RETIRE_OK same-original-Abandoned-root original-seal-drop reservations0 callbacks0 quota-baseline no-record-or-completion");
                                    if (graphicsNativeCommand)
                                        std::puts(
                                            "BINDLESS_RHI_GENERAL_GRAPHICS_COMMAND8_RETIRE_OK same-original-Abandoned-root original-shell-Cold8-seal-drop reservations0 callbacks0 quota-baseline no-record-or-completion");
                                    if (graphicsNativeCallback)
                                        std::puts(
                                            "BINDLESS_RHI_GENERAL_GRAPHICS_CALLBACK8_RETIRE_OK same-original-Abandoned-root original-observer-suppressed sticky-claim reservations0 callbacks0 quota-baseline no-completion-or-public-execution");
                                    if (graphicsNativeIssuer)
                                        std::puts(
                                            "BINDLESS_RHI_GENERAL_GRAPHICS_ISSUER8_RETIRE_OK original-seven-receipts-suppressed observer-retired reservations0 quota-baseline no-record-completion-output");
                                    coldCandidateDone = true;
                                    return;
                                }
                                std::weak_ptr<const Initialized> initializerObserver = initialized;
                                std::weak_ptr<rhi::OriginalCommandStateTransaction> rootObserver =
                                    root;
                                initialized.reset();
                                root.reset();
                                assert(!initializerObserver.expired() && !rootObserver.expired());
                                require(closure->validate());
                                root = closure->originalRoot();
                                initialized = initializerObserver.lock();
                                initializerObserver.reset();
                                rootObserver.reset();
                                closure.reset();
                                if (graphicsNativePayload) {
                                    heldGraphicsFreeze.reset();
                                    require(heldGraphicsPayload->validate());
                                    heldGraphicsFreeze = heldGraphicsPayload->originalDecision();
                                    std::puts(
                                        "BINDLESS_RHI_GENERAL_GRAPHICS_PAYLOAD8_RETAIN_OK same-original-Prepared-root freeze-closure-initializer-caller-drop original-native-inputs-retained no-record-or-completion");
                                }
                                assert(root->journalEmpty());
                                if (graphicsDependencyFreeze) {
                                    require(heldGraphicsFreeze->validate());
                                    assert(root->originalMetadataQuota()->liveBytes() > baseline);
                                } else
                                    assert(root->originalMetadataQuota()->liveBytes() == baseline);
                                if (graphicsDependencyFreeze)
                                    std::puts(
                                        "BINDLESS_RHI_GENERAL_GRAPHICS_CLOSURE10_RETAIN_OK same-original-Prepared-root initializer-caller-drop Freeze8-retained no-draw-or-completion");
                                else
                                    std::puts(
                                        "BINDLESS_RHI_GENERAL_GRAPHICS_CLOSURE10_RETIRE_OK same-original-Prepared-root initializer-caller-drop native-owners-retained closure-quota-baseline no-draw-or-completion");
                            };
                            const auto inspectCustody = [&](auto& root, const auto& stream) {
                                assert(!graphicsNativeIssuer || coldCandidateDone);
                                inspectCustodyWithRebuild(root, stream, [] {
                                    assert(false
                                        && "Issuer recapture needs the independent cold candidate");
                                });
                            };
                            const auto inspectPending = [&](const auto& root) {
                                if (graphicsNativeCold)
                                    return;
                                if (!graphicsDependencyFreeze)
                                    return;
                                const auto* stale =
                                    GraphicsFreeze::validateOriginal(heldGraphicsFreeze);
                                assert(stale
                                    && core::StringView(stale)
                                        == "RHI-WEBGPU-CANONICAL-CLOSURE-PLAN-STATUS");
                                if (graphicsNativePayload) {
                                    OriginalTypedNativePayloadFixture::verifyUnavailable(
                                        heldGraphicsPayload, "graphics-Pending-root");
                                    heldGraphicsPayload.reset();
                                    std::puts(
                                        "BINDLESS_RHI_GENERAL_GRAPHICS_PAYLOAD8_RETIRE_OK same-original-Pending-root stale-inputs-refused payload-child-drop no-record-or-completion");
                                }
                                heldGraphicsFreeze.reset();
                                assert(
                                    root->originalMetadataQuota()->liveBytes() == freezeBaseline);
                                std::puts(
                                    "BINDLESS_RHI_GENERAL_GRAPHICS_CLOSURE10_RETIRE_OK same-original-Pending-root Freeze8-child-drop closure-quota-baseline no-draw-or-completion");
                                std::puts(
                                    "BINDLESS_RHI_GENERAL_GRAPHICS_FREEZE8_RETIRE_OK same-original-Pending-root stale-candidates-refused caller-drop quota-baseline no-draw-or-completion");
                            };
                            OriginalGeneralFiniteGraphicsContentRootFixture::
                                verifyActualWithRebuild(stateStream,
                                    *c.device,
                                    *graphicsContentSources,
                                    inspectCustody,
                                    inspectPending,
                                    inspectCustodyWithRebuild,
                                    graphicsNativeCold,
                                    graphicsNativeRecording);
                        }
                    if (stateStreamMode)
                        stateStream = OriginalGeneralFiniteStateStreamFixture::verifyActual(
                            inputs, *c.device, stateQuota, streamQuota, usageMode);
                    if (usageMode)
                        OriginalGeneralFiniteUsageFixture::verifyActual(stateStream, *c.device);
                    else if (stateStreamMode)
                        OriginalGeneralFiniteUsageFixture::verifyEmpty(stateStream);
                    if (contentMode)
                        OriginalGeneralFiniteInitializedContentBrowserFixture::verifyActual(
                            *c.device,
                            initialized,
                            descriptor,
                            initialBytes,
                            initializationAttempted,
                            initializeQuotas);
                    // Dropping the caller's descriptor reference preserves the
                    // exact inventory-owned Source; dropping the weak parent
                    // cannot turn retained resources into a replacement graph.
                    const auto descriptorIndex = typedSources->originalLeaseIndex(descriptor);
                    descriptor.reset();
                    descriptor =
                        std::get<std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>>(
                            typedSources->sources()[descriptorIndex]);
                    using ReadbackTerminal = OriginalGeneralOutputReadbackInputsFixture;
                    const bool completedReadbackLoss = graphics && EM_ASM_INT({
                        // clang-format off
                            return new URLSearchParams(location.search).has("general-graphics-output-terminal");
                        // clang-format on
                    })
                        && ReadbackTerminal::mapUsesPhysicalLoss(
                            ReadbackTerminal::selectedMapTerminal());
                    const bool terminalDeviceLoss =
                        pendingContentLoss || completedReadbackLoss || graphicsPhysicalLoss;
                    const auto validateDescriptor = [&]() noexcept -> const char* {
                        return c.device
                            ? rhi::OriginalCommandDescriptorSetSource::validateOriginal(
                                  descriptor, *c.device)
                            : rhi::OriginalCommandDescriptorSetSource::validateOriginal(descriptor);
                    };
                    const auto expectedLifetime = graphicsPhysicalShutdown
                        ? "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN"
                        : terminalDeviceLoss      ? "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST"
                        : graphicsLifetimeRetired ? "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED"
                                                  : "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN";
                    if (terminalDeviceLoss || graphicsLifetimeTerminal)
                        assert(core::StringView(validateDescriptor()) == expectedLifetime);
                    else
                        require(validateDescriptor());
                    if (inputsMode || renderProjection) {
                        if (terminalDeviceLoss || graphicsLifetimeTerminal)
                            assert(core::StringView(
                                       rhi::OriginalCommandInputs::validateOriginal(inputs))
                                == expectedLifetime);
                        else
                            require(rhi::OriginalCommandInputs::validateOriginal(inputs));
                    }
                    if (displacement || wholeProfile || mixedRoot) {
                        // The canonical inventory and frozen dependencies must
                        // retain the real resources after all caller aliases go.
                        uniform.reset();
                        sampler.reset();
                        prefix = {};
                        storage = {};
                        sampled = {};
                        attachments = {};
                        adapter.reset();
                        pipeline.reset();
                        auto* retainedArena = arena.get();
                        arena.reset();
                        if (terminalDeviceLoss || graphicsLifetimeTerminal)
                            assert(core::StringView(validateDescriptor()) == expectedLifetime);
                        else
                            require(validateDescriptor());
                        const auto terminal = graphicsLifetimeTerminal
                            ? (graphicsLifetimeRetired ? 1 : 3)
                            : terminalDeviceLoss
                            ? 2
                            : EM_ASM_INT({
                                  // clang-format off
                                    const value = new URLSearchParams(location.search).get("general-source-terminal");
                                    return value === null || value === "reset" ? 0
                                           : value === "retired"               ? 1
                                           : value === "device-loss"           ? 2
                                                                               : 3;
                                  // clang-format on
                              });
                        assert(terminal <= 2 || graphicsLifetimeTerminal);
                        if (graphicsLifetimeTerminal) {
                            assert(bool(c.device) == (terminal == 1));
                            // The original facade already terminated while native
                            // receipts were held. Do not reset its arena or invent loss.
                        } else if (terminal == 0)
                            retainedArena->reset();
                        else if (terminal == 1)
                            c.device->retireOriginalDeviceLifetime();
                        else if (!terminalDeviceLoss) {
                            EM_ASM({
                                // clang-format off
                                    Module.preinitializedWebGPUDevice.destroy();
                                // clang-format on
                            });
                            assert(WebGpuAsyncWaitFixture::until([] {
                                return rhi_webgpu::webGpuNativeDeviceLost();
                            }));
                        }
                        const auto* terminalId = validateDescriptor();
                        assert(terminalId);
                        if (graphicsContents)
                            graphicsContentSources->verifyTerminal(descriptor,
                                c.device.get(),
                                graphicsLifetimeTerminal || graphicsPhysicalLoss);
                        if (inputsMode) {
                            assert(rhi::OriginalCommandInputs::validateOriginal(inputs));
                            std::puts(
                                "BINDLESS_RHI_GENERAL_INPUTS6_TERMINAL_OK original-inputs-retained invalid-no-execution");
                        }
                        if (consumptionMode)
                            OriginalGeneralFiniteConsumptionFixture::verifyInvalidated(
                                inputs, *c.device);
                        if constexpr (graphics)
                            if (renderProjection)
                                OriginalGeneralFiniteGraphicsInputsFixture::verifyInvalidated(
                                    inputs, c.device.get());
                        if constexpr (graphics)
                            if (graphicsStateStream)
                                OriginalGeneralFiniteGraphicsStateStreamFixture::verifyInvalidated(
                                    stateStream);
                        if constexpr (graphics)
                            if (graphicsUsage)
                                OriginalGeneralFiniteGraphicsUsageFixture::verifyInvalidated(
                                    stateStream, c.device.get(), initializedStream);
                        if (stateStreamMode)
                            OriginalGeneralFiniteStateStreamFixture::verifyInvalidated(stateStream);
                        if (usageMode)
                            OriginalGeneralFiniteUsageFixture::verifyInvalidated(
                                stateStream, *c.device);
                        std::printf(
                            "BINDLESS_RHI_GENERAL_SOURCE5_TERMINAL_OK %s %s retained-original invalid-no-execution\n",
                            terminal == 0       ? "reset"
                                : terminal == 1 ? "retired"
                                : terminal == 3 ? "shutdown"
                                                : "device-loss",
                            terminalId);
                        if (initializedBuffer) {
                            rhi::DescriptorBufferBindingRangeContract::Resolved interval{1u, 1u};
                            if (c.device)
                                assert(
                                    Initialized::validateGeneralFiniteInitializedContent(*c.device,
                                        {1u,
                                            true,
                                            Initialized::initializedIndexOwnerBound,
                                            extent,
                                            extent},
                                        initialized,
                                        descriptor,
                                        0u,
                                        initialBytes.data(),
                                        initialBytes.size(),
                                        1u,
                                        interval));
                            else {
                                assert((graphicsLifetimeTerminal || graphicsPhysicalShutdown)
                                    && initialized->readyDiagnostic() && validateDescriptor());
                                interval = {};
                            }
                            assert(!interval.offsetBytes && !interval.sizeBytes);
                            std::printf(
                                "BINDLESS_RHI_GENERAL_FINITE_CONTENT1_TERMINAL_OK %s original-association-invalid no-execution\n",
                                terminal == 0       ? "reset"
                                    : terminal == 1 ? "retired"
                                    : terminal == 3 ? "shutdown"
                                                    : "device-loss");
                        }
                    }
                    typedSources.reset();
                    if (inputsMode || renderProjection) {
                        // The original Inputs graph retains its own canonical
                        // inventory after caller release, until this graph retires.
                        if (!displacement && !wholeProfile && !mixedRoot) {
                            require(rhi::OriginalCommandInputs::validateOriginal(inputs));
                            if (consumptionMode)
                                assert(!rhi::common::OriginalCommandDescriptorConsumption::inspect(
                                    inputs,
                                    *c.device,
                                    OriginalGeneralFiniteConsumptionFixture::budget())
                                        .diagnostic);
                        }
                        if (anyStateStream) {
                            stateStream = {};
                            assert(!stateQuota->liveBytes() && !streamQuota->liveBytes());
                            if (graphicsStateStream)
                                std::puts(
                                    "BINDLESS_RHI_GENERAL_GRAPHICS_STATE5_STREAM5_RETIRE_OK original-attachment-bind-draw-graph stream-quotas-zero no-native");
                            else
                                std::puts(
                                    "BINDLESS_RHI_GENERAL_STATE5_STREAM5_RETIRE_OK original-graph-stream-quotas-zero no-native");
                            if (usageMode)
                                std::puts(
                                    "BINDLESS_RHI_GENERAL_USAGE6_DEPENDENCY6_RETIRE_OK original-state-stream-quotas-zero native-closed");
                            if (graphicsUsage)
                                std::puts(
                                    "BINDLESS_RHI_GENERAL_GRAPHICS_USAGE6_DEPENDENCY6_RETIRE_OK original-attachment-state-stream-quotas-zero native-closed");
                        }
                        inputs.reset();
                        assert(!inputQuota->liveBytes());
                        std::puts(
                            "BINDLESS_RHI_GENERAL_INPUTS6_RETIRE_OK caller-drop original-inventory graph-quota-zero execution-closed");
                        if (consumptionMode)
                            std::puts(
                                "BINDLESS_RHI_GENERAL_CONSUMPTION4_RETIRE_OK original-inputs graph-quota-zero no-state-execution");
                    }
                    assert(validateDescriptor());
                    descriptor.reset();
                    for (const auto& q : acceptedSource.second)
                        assert(!q->liveBytes());
                    // A fresh canonical inventory is needed for the next
                    // independently authored write candidate.
                    if (!displacement && !wholeProfile && !mixedRoot) {
                        sources = Sources::prepareOriginal(
                            prefix.data(), prefixCount, budget, source, id);
                        require(id);
                        assert(sources);
                        core::CpuMemoryQuotaScope scope(resource);
                        arena->reset();
                        rhi::DescriptorSetDesc sd;
                        sd.layout =
                            rhi::DescriptorStateCopy::copy(c.layout->desc().descriptorLayouts[0]);
                        set = c.device->allocateOriginalDescriptorSet(arena, sd);
                        assert(set);
                    }
                }
            }
            assert(EM_ASM_INT(
                {
                    // clang-format off
                        return Module.arpGeneralFiniteEntries.length > 0 &&
                               Module.arpGeneralFiniteEntries.every(e => (e[0] === $0 && e[1] === 16) ||
                                                                         ($1 && e[0] === 0 && e[1] === Number($2)));
                    // clang-format on
                },
                alignment,
                sourceMode,
                extent));
            EM_ASM({
                // clang-format off
                    Module.preinitializedWebGPUDevice.createBindGroup = Module.arpGeneralFiniteCreate;
                    delete Module.arpGeneralFiniteCreate;
                    delete Module.arpGeneralFiniteEntries;
                // clang-format on
            });
            if (graphicsContents)
                graphicsContentSources->retire();
            if (initializedBuffer) {
                const auto initializationTerminal =
                    OriginalGeneralFiniteInitializedContentBrowserFixture::selectedTerminal();
                if (OriginalGeneralFiniteInitializedContentBrowserFixture::callbackOnly(
                        initializationTerminal))
                    OriginalGeneralFiniteInitializedContentBrowserFixture::finishCallbackOnlyDrain(
                        initializationTerminal, initialized, initializeQuotas);
                initialized.reset();
                initialBytes.clear();
                initialBytes.shrink_to_fit();
                assert(!initializeQuotas[0]->liveBytes() && !initializeQuotas[1]->liveBytes()
                    && !initializeQuotas[4]->liveBytes());
                EM_ASM({
                    // clang-format off
                        delete Module.arpFiniteContentBuffer;
                    // clang-format on
                });
                std::puts(
                    "BINDLESS_RHI_GENERAL_FINITE_CONTENT1_RETIRE_OK original-initializer-source-quotas-zero no-second-owner-graph execution-closed");
                if (graphicsShaderContent)
                    std::puts(
                        "BINDLESS_RHI_GENERAL_GRAPHICS_SHADER_BUFFER_RETIRE_OK original-initializer-callbacks-drained source-quotas-zero no-native-draw");
            }
        }
        assert(!source->liveBytes());
        if (readOnly)
            std::puts(wholeProfile
                    ? "BINDLESS_RHI_GENERAL_WHOLE_READONLY_SOURCE5_COMPOSITION_OK actual-whole-readonly16 static-layout mixed-storage-sampled-sampler all-quotas-zero execution-public-closed"
                    : "BINDLESS_RHI_GENERAL_FINITE_READONLY_COMPOSITION2_OK actual-readonly-static-dynamic16 mixed-storage-sampled-sampler frozen-native-entries exact-queried-alignment full-allocation quotas-zero hot0-query0 recording-public-closed");
        else if (wholeProfile)
            std::puts(
                "BINDLESS_RHI_GENERAL_WHOLE_SOURCE5_COMPOSITION_OK actual-whole-uniform16 static-layout mixed-storage-sampled-sampler all-quotas-zero execution-public-closed");
        else
            std::puts(
                "BINDLESS_RHI_GENERAL_FINITE_COMPOSITION2_OK actual-uniform-static-dynamic16 mixed-storage-sampled-sampler frozen-native-entries exact-queried-alignment full-allocation quotas-zero hot0-query0 recording-public-closed");
        if (sourceMode)
            std::printf(
                "BINDLESS_RHI_GENERAL_SOURCE5_RETIRE_OK %s caller-drop weak-inventory terminal-invalid all-quotas-zero execution-public-closed\n",
                wholeProfile ? "whole" : "static-dynamic");
        if constexpr (graphics)
            std::printf(
                "BINDLESS_RHI_GENERAL_GRAPHICS_SOURCE5_COMPOSITION_OK %s %s accepted-RenderPipeline Fragment4 real-GeneralWrite2 frozen-Source5 original-terminal quotas-zero draw-readback-closed\n",
                readOnly ? "readonly-storage" : "uniform",
                wholeProfile ? "whole" : "static-dynamic");
    }

    template <class Factory> static void verifyActual(Factory&& create)
    {
        const shader::SingleEntryShaderLoadBudget load{
            1024u * 1024u, 4u * 1024u * 1024u, 1024u * 1024u, 4u};
        auto artifactQuota = core::makeShared<core::CpuMemoryQuota>(load.maxCpuMetadataBytes),
             inputQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        core::pmr::String diagnostic;
        const bool readOnly = EM_ASM_INT({
            // clang-format off
                return new URLSearchParams(location.search).get('general-content-buffer-type') ===
                       'readonly-storage';
            // clang-format on
        }) != 0;
        assert(!readOnly || EM_ASM_INT({
            // clang-format off
                const p = new URLSearchParams(location.search);
                return p.has('general-finite-content') || p.has('general-finite-content-plan') ||
                       p.has('general-finite-native-closure') || p.has('general-finite-native-payload') ||
                       p.has('general-finite-native-recording') || p.has('general-finite-execution') ||
                       p.has('general-finite-lifecycle');
            // clang-format on
        }));
        const char* artifactPath = readOnly
            ? "/ordinary-pipeline-fixture/original_general_readonly_shader_resources/original_general_readonly_shader_resources.computeMain.entry.yaml"
            : "/ordinary-pipeline-fixture/original_general_shader_resources/original_general_shader_resources.computeMain.entry.yaml";
        auto artifact =
            shader::SingleEntryShaderArtifact::load(artifactPath, load, artifactQuota, diagnostic);
        if (!artifact)
            std::fprintf(stderr, "general shader artifact: %s\n", diagnostic.c_str());
        assert(artifact);
        shader::TargetBufferMemoryLayout targetFacts;
        const auto* unrequested = shader::SingleEntryShaderArtifact::originalTargetBufferMemory(
            artifact, 0u, 0u, targetFacts);
        assert(unrequested
            && core::StringView(unrequested) == "SHADER-ENTRY-TARGET-MEMORY-NOT-REQUESTED"
            && !targetFacts.target.minimumBindingBytes);
        TargetMemoryRequestFixture targetRequest;
        targetRequest.prepare(artifact);
        artifact.reset();
        assert(!artifactQuota->liveBytes());
        auto requestedLoad = load;
        requestedLoad.targetMemory = &targetRequest.request();
        artifact = shader::SingleEntryShaderArtifact::load(
            artifactPath, requestedLoad, artifactQuota, diagnostic);
        if (!artifact)
            std::fprintf(stderr, "general target-memory artifact: %s\n", diagnostic.c_str());
        assert(artifact);
        assert(!shader::SingleEntryShaderArtifact::originalTargetBufferMemory(
                   artifact, 0u, 0u, targetFacts)
            && targetFacts.target.minimumBindingBytes == (readOnly ? 4u : 16u)
            && targetFacts.target.group == 0u && targetFacts.target.binding == 0u);
        assert(targetFacts.type
            == (readOnly ? shader::BindingType::ReadOnlyStorageBuffer
                         : shader::BindingType::UniformBuffer));
        if (readOnly)
            std::puts(
                "BINDLESS_RHI_GENERAL_DESCRIPTOR_TARGET_MEMORY_READONLY_OK original-StructuredUint minimum4 selected16 group0-binding0");
        else
            std::puts(
                "BINDLESS_RHI_GENERAL_DESCRIPTOR_TARGET_MEMORY_ADMISSION_OK unrequested-refused explicit-original-uniform16 group0-binding0");
        auto input = rhi::OriginalSingleEntryPipelineInput::prepare(
            artifact, {1u, bytes}, inputQuota, diagnostic);
        assert(input);
        rhi::PipelineLayoutDesc layout;
        rhi::DescriptorLayoutDesc set;
        set.name = "original general shader resources";
        for (std::uint32_t i = 0u; i < 4u; ++i) {
            rhi::DescriptorBindingDesc b;
            b.binding = i;
            b.stages = rhi::ShaderStage::Compute;
            b.type = i == 0u ? (readOnly ? rhi::DescriptorType::ReadOnlyStorageBuffer
                                         : rhi::DescriptorType::UniformBuffer)
                : i == 1u    ? rhi::DescriptorType::StorageTexture
                : i == 2u    ? rhi::DescriptorType::SampledTexture
                             : rhi::DescriptorType::Sampler;
            if (i == 0u && EM_ASM_INT({
                    // clang-format off
                    const p = new URLSearchParams(location.search);
                    return (p.has("general-finite-binding") || p.has("general-finite-source") ||
                            p.has("general-finite-content") || p.has("general-finite-content-plan") ||
                            p.has("general-finite-native-closure") || p.has("general-finite-native-payload") ||
                            p.has("general-finite-native-recording") || p.has("general-finite-execution") ||
                            p.has("general-finite-lifecycle")) &&
                           !p.has("general-source-whole");
                    // clang-format on
                }))
                b.dynamicOffset = true;
            if (i == 1u) {
                b.storageTextureFormat = rhi::TextureFormat::Rgba16Float;
                b.storageTextureAccess = rhi::StorageTextureAccess::WriteOnly;
            }
            if (i == 1u || i == 2u) {
                b.textureViewDimensionConstrained = true;
                b.textureViewDimension = rhi::TextureViewDimension::Texture2D;
            }
            set.bindings.push_back(b);
        }
        layout.descriptorLayouts.push_back(set);
        layout.descriptorSets.push_back({0u, set.name});
        using Context = OriginalAcceptedComputePipelineFactoryFixture::PreparedContext;
        const rhi::OriginalDeviceNativeLossObserverRequest observedRequest{1u, true, 3u, 4u, bytes};
        // Uniform input, upload staging and the explicitly admitted readback destination.
        auto c = core::makeUnique<Context>(std::forward<Factory>(create),
            input,
            layout,
            11u,
            7u,
            1u,
            3u,
            1u,
            false,
            &observedRequest,
            2u,
            1u);
        if (EM_ASM_INT({
                // clang-format off
                const p = new URLSearchParams(location.search);
                return p.has("general-finite-content-plan") || p.has("general-finite-native-closure") ||
                       p.has("general-finite-native-payload") || p.has("general-finite-native-recording") ||
                       p.has("general-finite-execution") || p.has("general-finite-lifecycle");
                // clang-format on
            })) {
            c->ready();
            const auto textureQuotas = project(*c);
            c->retire();
            c.reset();
            input.reset();
            artifact.reset();
            for (const auto& quota : textureQuotas)
                assert(!quota->liveBytes());
            assert(!artifactQuota->liveBytes() && !inputQuota->liveBytes());
            std::puts(
                "BINDLESS_RHI_GENERAL_CONTENT_PLAN5_DEVICE_RETIRE_OK original-device-pipeline-texture-upload-quotas-zero public-closed");
            return;
        }
        if (EM_ASM_INT({
                // clang-format off
                const p = new URLSearchParams(location.search);
                return p.has("general-finite-binding") || p.has("general-finite-source") ||
                       p.has("general-finite-content");
                // clang-format on
            })) {
            const bool contentMode = EM_ASM_INT({
                // clang-format off
                    return new URLSearchParams(location.search).has("general-finite-content");
                // clang-format on
            }) != 0;
            const bool sourceMode = contentMode || EM_ASM_INT({
                // clang-format off
                    return new URLSearchParams(location.search).has("general-finite-source");
                // clang-format on
            }) != 0;
            const bool wholeProfile = EM_ASM_INT({
                // clang-format off
                    return new URLSearchParams(location.search).has("general-source-whole");
                // clang-format on
            }) != 0;
            c->ready();
            verifyFiniteBindings(*c, sourceMode, wholeProfile, contentMode);
            c->retire();
            c.reset();
            input.reset();
            artifact.reset();
            assert(!artifactQuota->liveBytes() && !inputQuota->liveBytes());
            std::puts(
                "BINDLESS_RHI_GENERAL_FINITE_COMPOSITION2_RETIRE_OK original-device-pipeline-resources-quotas-zero public-closed");
            return;
        }
        const auto terminalChoice = EM_ASM_INT({
            // clang-format off
                const value = new URLSearchParams(location.search).get("general-recording-terminal");
                return value === null               ? 0
                       : value === "cancel"         ? 1
                       : value === "drop"           ? 2
                       : value === "native-error"   ? 3
                       : value === "reverse-scopes" ? 4
                                                    : 5;
            // clang-format on
        });
        assert(terminalChoice >= 0 && terminalChoice <= 4);
        const auto terminal = static_cast<RecordingTerminal>(terminalChoice);
        const auto submissionChoice = EM_ASM_INT({
            // clang-format off
                const value = new URLSearchParams(location.search).get("general-submission-terminal");
                return value === null               ? 0
                       : value === "cancel"         ? 1
                       : value === "retired"        ? 2
                       : value === "shutdown"       ? 3
                       : value === "throw-before"   ? 4
                       : value === "throw-after"    ? 5
                       : value === "native-error"   ? 6
                       : value === "reverse-scopes" ? 7
                       : value === "partial-scope"  ? 8
                                                    : 9;
            // clang-format on
        });
        assert(submissionChoice >= 0 && submissionChoice <= 8
            && (!submissionChoice || terminal == RecordingTerminal::None));
        const auto submissionTerminal = static_cast<SubmissionTerminal>(submissionChoice);
        const auto physicalChoice = EM_ASM_INT({
            // clang-format off
                const value = new URLSearchParams(location.search).get("general-physical-loss");
                return value === null                    ? 0
                       : value === "queue-uncertain"     ? 1
                       : value === "recording-shutdown"  ? 2
                       : value === "partial-push"        ? 3
                       : value === "recording-exception" ? 4
                                                         : 5;
            // clang-format on
        });
        assert(physicalChoice >= 0 && physicalChoice <= 4
            && (!physicalChoice || (!submissionChoice && terminal == RecordingTerminal::None)));
        const auto physicalLoss = static_cast<PhysicalLoss>(physicalChoice);
        assert(OriginalGeneralOutputReadbackInputsFixture::selectedMapTerminal()
                == OriginalGeneralOutputReadbackInputsFixture::MapTerminal::None
            || (!physicalChoice && !submissionChoice && terminal == RecordingTerminal::None));
        c->ready();
        const auto textureQuotas = physicalChoice
            ? project(*c, physicalChoice == 1, terminal, submissionTerminal, physicalLoss)
            : submissionChoice ? project(*c, true, terminal, submissionTerminal)
                               : project(*c, false, terminal);
        const auto submissionTextureQuotas =
            !physicalChoice && !submissionChoice && terminal == RecordingTerminal::None
            ? project(*c, true)
            : decltype(textureQuotas){};
        c->retire();
        c.reset();
        input.reset();
        artifact.reset();
        for (const auto& quota : textureQuotas)
            assert(!quota->liveBytes());
        for (const auto& quota : submissionTextureQuotas)
            if (quota)
                assert(!quota->liveBytes());
        assert(!artifactQuota->liveBytes() && !inputQuota->liveBytes());
        const auto mapTerminal = OriginalGeneralOutputReadbackInputsFixture::selectedMapTerminal();
        if (mapTerminal != OriginalGeneralOutputReadbackInputsFixture::MapTerminal::None)
            std::printf(
                "BINDLESS_RHI_GENERAL_OUTPUT_MAP1_RETIRE_OK %s original-quotas-reservations-zero public-closed\n",
                OriginalGeneralOutputReadbackInputsFixture::mapTerminalName(mapTerminal));
    }
#endif
};
} // namespace arp::test
