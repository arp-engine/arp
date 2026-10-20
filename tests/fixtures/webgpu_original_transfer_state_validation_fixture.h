#pragma once
#include "rhi/backends/webgpu/webgpu_original_transfer_dependency_validation.h"
#include "rhi/backends/webgpu/webgpu_original_transfer_state_validation.h"
#include "rhi/backends/webgpu/webgpu_original_transfer_stream_validation.h"
#include "rhi/frontend/command_encoder_base.h"
#include "transfer_dependency_lowering_fixture.h"
#include "webgpu_original_dependency_inputs_fixture.h"
#include "webgpu_original_state_transaction_fixture.h"
#include "webgpu_original_transfer_batch_fixture.h"
#include "webgpu_original_transfer_dependency_batch_fixture.h"

#include <array>
#include <cassert>
#include <cstdio>

namespace arp::test {
class WebGpuOriginalTransferStateValidationFixture final {
public:
    static void verifyContract()
    {
        WebGpuOriginalTransferDependencyBatchFixture::verifyContract();
        TransferDependencyLoweringFixture::verify();
        WebGpuOriginalTransferBatchFixture::verifyContract();
        WebGpuOriginalStateTransactionFixture::verifyContract();
        assert(std::string_view(rhi_webgpu::WebGpuOriginalTransferStreamValidation::validate({}))
            == "RHI-WEBGPU-ORIGINAL-STATE-OWNER");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verify(
        const std::shared_ptr<const rhi_webgpu::WebGpuOrdinaryTransferRetention>& root,
        const std::array<rhi::TransferCopyArguments, 6>& copies,
        bool retainStreamLoss = false)
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        using K = CommandUsageKind;
        using A = CommandScalarArguments;
        using Store = CommandVariableArguments;
        using Validation = WebGpuOriginalTransferStateValidation;
        auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
        auto stateQuota = core::makeShared<core::CpuMemoryQuota>(16384u);
        const char* diagnostic = nullptr;
        for (std::size_t ci = 0u; ci < 6u; ++ci) {
            const bool memoryCase = ci == 5u;
            const auto& copy = copies[memoryCase ? 0u : ci];
            const auto* source = root->find(copy.source());
            const auto* destination = root->find(copy.destination());
            assert(source && destination && source->id != destination->id);
            if (!memoryCase)
                verifyCopyOnly(root, copy, retainStreamLoss);
            if (retainStreamLoss && ci == 0u) {
                BufferCopyDesc offset;
                assert(copy.write(offset));
                offset.sourceOffsetBytes = 4u;
                offset.destinationOffsetBytes = 4u;
                offset.sizeBytes = 256u;
                verifyCopyOnly(root, *TransferCopyArguments::capture(offset), false, true, {});
            }
            const auto savedState = source->buffer ? source->buffer->recordedState().state()
                                                   : source->texture->recordedState().state();
            if (memoryCase) {
                assert(source->buffer);
                assert(!source->buffer->recordState(ResourceState::CopySrc));
            }
            const auto sourceState = source->buffer ? source->buffer->recordedState().state()
                                                    : source->texture->recordedState().state();
            const auto destinationState = destination->buffer
                ? destination->buffer->recordedState().state()
                : destination->texture->recordedState().state();
            auto sourceBarrier = makeInferredResourceBarrier(source->id,
                source->texture ? defaultTextureSubresourceRange(source->textureDesc)
                                : TextureSubresourceRange{},
                sourceState,
                ResourceState::CopySrc);
            auto destinationBarrier = makeInferredResourceBarrier(destination->id,
                destination->texture ? defaultTextureSubresourceRange(destination->textureDesc)
                                     : TextureSubresourceRange{},
                destinationState,
                ResourceState::CopyDst);
            const auto sourceKind = memoryCase ? K::BufferMemoryBarrier
                : source->buffer               ? K::BufferBarrier
                                               : K::TextureBarrier;
            ResourceMemoryBarrierDesc sourceMemory{source->id,
                PipelineStage::Transfer,
                PipelineStage::Transfer,
                ResourceAccess::TransferRead,
                ResourceAccess::TransferRead};
            const auto destinationKind = destination->buffer ? K::BufferBarrier : K::TextureBarrier;
            WebGpuOriginalDependencyInputsFixture originalInputs(
                root, copy, sourceKind, destinationKind, quota);
            const auto factory = [&] {
                return originalInputs.prepare(memoryCase
                        ? *ResourceDependencyArguments::capture(sourceKind, sourceMemory)
                        : *ResourceDependencyArguments::capture(sourceKind, sourceBarrier),
                    *ResourceDependencyArguments::capture(destinationKind, destinationBarrier),
                    stateQuota);
            };
            auto states = factory();
            assert(states);
            if (retainStreamLoss)
                WebGpuOriginalTransferBatchFixture::verifyRejected(
                    states, "RHI-WEBGPU-ORIGINAL-TRANSFER-LANGUAGE");
            const auto sourceRevision = source->buffer
                ? source->buffer->recordedState().revision()
                : source->texture->recordedState().revision();
            const auto destinationRevision = destination->buffer
                ? destination->buffer->recordedState().revision()
                : destination->texture->recordedState().revision();
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            assert(!WebGpuOriginalTransferDependencyValidation::validate(states));
            assert(std::string_view(
                       WebGpuOriginalTransferDependencyValidation::executionDiagnostic(states))
                == webGpuGenericRecordingUsageDiagnostic);
            assert(!Validation::validate(states, 0u).diagnostic && !states->validate());
            assert(std::string_view(WebGpuOriginalTransferStreamValidation::validate(states))
                == "RHI-WEBGPU-ORIGINAL-TRANSFER-LANGUAGE");
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
            assert((source->buffer ? source->buffer->recordedState().revision()
                                   : source->texture->recordedState().revision())
                == sourceRevision);
            assert((destination->buffer ? destination->buffer->recordedState().revision()
                                        : destination->texture->recordedState().revision())
                == destinationRevision);
            if (retainStreamLoss)
                std::puts(
                    "BINDLESS_RHI_ORIGINAL_DEPENDENCY_LOWERING_OK 6 2 1 exact-binding equivalent-intent execution-closed allocation0");
            if (retainStreamLoss && ci == 0u)
                WebGpuOriginalStateTransactionFixture::retainInputsForLoss(states, quota);
            auto forged = std::shared_ptr<const WebGpuOriginalResourceStates>(quota, states.get());
            assert(std::string_view(Validation::validate(forged, 0u).diagnostic)
                == "RHI-WEBGPU-ORIGINAL-STATE-OWNER");
            assert(std::string_view(WebGpuOriginalTransferDependencyValidation::validate(forged))
                == "RHI-WEBGPU-ORIGINAL-STATE-OWNER");
            forged.reset();
            states.reset();
            if (retainStreamLoss)
                WebGpuOriginalStateTransactionFixture::verify(factory);
            if (retainStreamLoss) {
                const auto currentSnapshot = [&] {
                    const auto currentSource = source->buffer
                        ? source->buffer->recordedState().state()
                        : source->texture->recordedState().state();
                    const auto currentDestination = destination->buffer
                        ? destination->buffer->recordedState().state()
                        : destination->texture->recordedState().state();
                    const auto sourceCurrent = makeInferredResourceBarrier(source->id,
                        sourceBarrier.textureSubresources,
                        currentSource,
                        ResourceState::CopySrc);
                    const auto destinationCurrent = makeInferredResourceBarrier(destination->id,
                        destinationBarrier.textureSubresources,
                        currentDestination,
                        ResourceState::CopyDst);
                    return originalInputs.prepare(memoryCase
                            ? *ResourceDependencyArguments::capture(sourceKind, sourceMemory)
                            : *ResourceDependencyArguments::capture(sourceKind, sourceCurrent),
                        *ResourceDependencyArguments::capture(destinationKind, destinationCurrent),
                        stateQuota);
                };
                WebGpuOriginalTransferDependencyBatchFixture::verify(currentSnapshot);
                assert(!(source->buffer ? source->buffer->recordState(sourceState)
                                        : source->texture->recordState(sourceState)));
                assert(
                    !(destination->buffer ? destination->buffer->recordState(destinationState)
                                          : destination->texture->recordState(destinationState)));
            }
            const auto expect = [&](const char* id, std::size_t index) {
                auto rejected = factory();
                assert(rejected);
                const auto before = core::defaultCpuAllocator().stats().allocationCount;
                const auto result = Validation::validate(rejected, 0u);
                assert(result.diagnostic && std::string_view(result.diagnostic) == id
                    && result.commandIndex == index);
                assert(
                    std::string_view(WebGpuOriginalTransferDependencyValidation::validate(rejected))
                    == id);
                assert(!rejected->validate()
                    && core::defaultCpuAllocator().stats().allocationCount == before);
            };
            if (memoryCase) {
                sourceMemory.destinationAccess = ResourceAccess::ShaderRead;
                expect("RHI-ACCESS-STATE-MISMATCH", 0u);
                sourceMemory.destinationAccess = ResourceAccess::TransferRead;
                sourceMemory.destinationStages = PipelineStage::ComputeShader;
                expect("RHI-ACCESS-STAGE-MISMATCH", 0u);
                sourceMemory.destinationStages = PipelineStage::Transfer;
                sourceMemory.sourceStages = PipelineStage::None;
                expect("RHI-RESOURCE-MEMORY-BARRIER-SOURCE-STAGE-MISSING", 0u);
                assert(!stateQuota->liveBytes());
                assert(!source->buffer->recordState(savedState));
                continue;
            }
            const auto originalSource = sourceBarrier, originalDestination = destinationBarrier;
            assert(!(source->buffer ? source->buffer->recordState(ResourceState::ShaderRead)
                                    : source->texture->recordState(ResourceState::ShaderRead)));
            sourceBarrier = makeInferredResourceBarrier(source->id,
                originalSource.textureSubresources,
                ResourceState::ShaderRead,
                ResourceState::CopySrc);
            {
                auto unsupported = factory();
                assert(unsupported && !Validation::validate(unsupported, 0u).diagnostic);
                assert(std::string_view(
                           WebGpuOriginalTransferDependencyValidation::validate(unsupported))
                    == "RHI-TRANSFER-DEPENDENCY-SCOPE-UNPROVEN");
            }
            assert(!(source->buffer ? source->buffer->recordState(sourceState)
                                    : source->texture->recordState(sourceState)));
            sourceBarrier = originalSource;
            sourceBarrier.before = sourceState == ResourceState::CopyDst ? ResourceState::CopySrc
                                                                         : ResourceState::CopyDst;
            expect("RHI-STATE-SEQUENCE-BEFORE-MISMATCH", 0u);
            sourceBarrier = originalSource;
            destinationBarrier.after = ResourceState::CopySrc;
            destinationBarrier.destinationAccess = ResourceAccess::TransferRead;
            expect("RHI-WEBGPU-ORIGINAL-STATE-COPY-MISMATCH", 3u);
            destinationBarrier = originalDestination;
            sourceBarrier.after = static_cast<ResourceState>(UINT32_MAX);
            expect("RHI-STATE-SEQUENCE-STATE-INVALID", 0u);
            sourceBarrier = originalSource;
            sourceBarrier.destinationAccess = ResourceAccess::ShaderRead;
            expect("RHI-ACCESS-STATE-MISMATCH", 0u);
            sourceBarrier = originalSource;
            sourceBarrier.destinationStages = PipelineStage::ComputeShader;
            expect("RHI-ACCESS-STAGE-MISMATCH", 0u);
            sourceBarrier = originalSource;
            if (source->texture) {
                sourceBarrier.textureSubresources = {};
                if (sourceBarrier.textureSubresources == originalSource.textureSubresources)
                    ++sourceBarrier.textureSubresources.baseMipLevel;
                expect("RHI-WEBGPU-BARRIER-SUBRESOURCE-STATE-UNPROVEN", 0u);
                sourceBarrier = originalSource;
            }
            if (destination->texture) {
                ++destinationBarrier.textureSubresources.baseArrayLayer;
                expect("RHI-WEBGPU-BARRIER-SUBRESOURCE-STATE-UNPROVEN", 1u);
                destinationBarrier = originalDestination;
            }
            assert(!stateQuota->liveBytes());
        }
        if (!retainStreamLoss)
            assert(!quota->liveBytes());
        std::puts(
            "BINDLESS_RHI_ORIGINAL_TRANSFER_STATE_OK copy5 kind4 memory1 ordered barriers before scopes ranges immutable allocation0");
    }

    static void verifyActualDeviceLoss()
    {
        using namespace rhi_webgpu;
        assert(lossCount_ == loss_.size() && webGpuNativeDeviceLost());
        for (auto& probe : loss_) {
            assert(probe.states
                && std::string_view(WebGpuOriginalTransferStreamValidation::validate(probe.states))
                    == "RHI-WEBGPU-COMMAND-PROJECTION-DEVICE-LOST");
            probe.states.reset();
            assert(!probe.stateQuota->liveBytes() && !probe.inputQuota->liveBytes());
            probe = {};
        }
        lossCount_ = 0u;
        std::puts("BINDLESS_RHI_ORIGINAL_TRANSFER_STREAM_LOSS_OK source-reject retire5");
    }

private:
    static void verifyCopyOnly(
        const std::shared_ptr<const rhi_webgpu::WebGpuOrdinaryTransferRetention>& root,
        const rhi::TransferCopyArguments& copy,
        bool retainLoss,
        bool verifyNative = false,
        std::string_view label = "传输✓")
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        using K = CommandUsageKind;
        using A = CommandScalarArguments;
        using Store = CommandVariableArguments;
        using States = WebGpuOriginalResourceStates;
        using Validation = WebGpuOriginalTransferStreamValidation;
        const char* diagnostic = nullptr;
        const auto* source = root->find(copy.source());
        const auto* destination = root->find(copy.destination());
        assert(source && destination);
        const auto sourceState = source->buffer ? source->buffer->recordedState().state()
                                                : source->texture->recordedState().state();
        const auto destinationState = destination->buffer
            ? destination->buffer->recordedState().state()
            : destination->texture->recordedState().state();
        const auto set = [&](const auto& resource,
                             ResourceState value,
                             core::MutationJournal* journal = nullptr) {
            assert(!(resource.buffer ? resource.buffer->recordState(value, journal)
                                     : resource.texture->recordState(value, journal)));
        };
        set(*source, ResourceState::CopySrc);
        set(*destination, ResourceState::CopyDst);
        auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
        auto stateQuota = core::makeShared<core::CpuMemoryQuota>(16384u);
        const std::array<WebGpuCommandReference, 2> refs{
            {{source->buffer ? CommandReferenceDomain::Buffer : CommandReferenceDomain::Texture,
                 WebGpuCommandReferenceSource::Transfer,
                 source->id},
                {destination->buffer ? CommandReferenceDomain::Buffer
                                     : CommandReferenceDomain::Texture,
                    WebGpuCommandReferenceSource::Transfer,
                    destination->id}}};
        WebGpuGenericCommandProjectionBudget budget{1u,
            true,
            1u,
            0u,
            0u,
            0u,
            2u,
            static_cast<std::uint32_t>(root->resources().size()),
            {},
            quota->limitBytes(),
            root->retainedBytes()};
        for (const auto& ref : refs)
            ++budget.maxDomainReferences[static_cast<std::size_t>(ref.domain)];
        WebGpuGenericCommandProjection::Sources sources;
        sources.transfer = root;
        auto graph =
            WebGpuGenericCommandProjection::shareOriginal(WebGpuGenericCommandProjection::prepare(
                std::move(sources), refs.data(), refs.size(), budget, quota, diagnostic));
        assert(graph);
        const std::array<CommandUsage, 4> commands{{{K::BeginTransferPass, 0u, 0u},
            {copy.kind(), 0u, 2u},
            {K::EndTransferPass, 2u, 0u},
            {K::Finish, 2u, 0u}}};
        auto usage = WebGpuGenericCommandUsage::prepareOriginal(graph,
            commands.data(),
            commands.size(),
            {1u, true, 4u, 2u, quota->limitBytes()},
            quota,
            diagnostic);
        assert(usage);
        std::array<Store::Input, 4> raw{{{*A::capture(K::BeginTransferPass, A::None{}), label},
            {*A::capture(copy.kind(), A::None{})},
            {*A::capture(K::EndTransferPass, A::None{})},
            {*A::capture(K::Finish, A::None{})}}};
        auto arguments = Store::prepareOriginal(raw.data(),
            raw.size(),
            {1u, true, 4u, 0u, 16u, 0u, quota->limitBytes()},
            quota,
            diagnostic);
        assert(arguments);
        auto scalars = WebGpuFrozenCommandScalars::prepareOriginal(
            usage, arguments, {1u, true, 4u, 0u, quota->limitBytes()}, quota, diagnostic);
        assert(scalars);
        raw[0].text = "mutated";
        WebGpuOriginalCommandInputs::OriginalDescriptor descriptor{1u, copy};
        auto inputs = WebGpuOriginalCommandInputs::prepareOriginal(
            scalars, {}, &descriptor, 1u, {1u, true, 1u, quota->limitBytes()}, quota, diagnostic);
        assert(inputs);
        descriptor.arguments = TransferCopyArguments::finish();
        assert(std::get<TransferCopyArguments>(inputs->descriptors()[0].arguments).matches(copy));
        const auto count = static_cast<std::uint32_t>(root->resources().size());
        const auto snapshot = [&] {
            return States::prepareOriginal(
                inputs, {1u, true, count, count, stateQuota->limitBytes()}, stateQuota, diagnostic);
        };
        auto states = snapshot();
        assert(states);
        const auto sourceRevision = source->buffer ? source->buffer->recordedState().revision()
                                                   : source->texture->recordedState().revision();
        const auto destinationRevision = destination->buffer
            ? destination->buffer->recordedState().revision()
            : destination->texture->recordedState().revision();
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        assert(!Validation::validate(states));
        assert((source->buffer ? source->buffer->recordedState().revision()
                               : source->texture->recordedState().revision())
            == sourceRevision);
        assert((destination->buffer ? destination->buffer->recordedState().revision()
                                    : destination->texture->recordedState().revision())
            == destinationRevision);
        auto forged = std::shared_ptr<const States>(quota, states.get());
        assert(std::string_view(Validation::validate(forged)) == "RHI-WEBGPU-ORIGINAL-STATE-OWNER");
        core::MutationJournal pending;
        set(*source, ResourceState::CopySrc, &pending);
        assert(
            std::string_view(Validation::validate(states)) == "RHI-WEBGPU-ORIGINAL-STATES-PENDING");
        pending.rollback();
        assert(
            std::string_view(Validation::validate(states)) == "RHI-WEBGPU-ORIGINAL-STATES-STALE");
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(previous);
        if (retainLoss)
            WebGpuOriginalTransferBatchFixture::verifyRejected(
                states, "RHI-WEBGPU-ORIGINAL-STATES-STALE");
        forged.reset();
        states.reset();
        set(*source, ResourceState::CopyDst);
        states = snapshot();
        assert(states);
        assert(std::string_view(Validation::validate(states))
            == "RHI-WEBGPU-ORIGINAL-STATE-COPY-MISMATCH");
        if (retainLoss)
            WebGpuOriginalTransferBatchFixture::verifyRejected(
                states, "RHI-WEBGPU-ORIGINAL-STATE-COPY-MISMATCH");
        states.reset();
        set(*source, ResourceState::CopySrc);
        set(*destination, ResourceState::CopySrc);
        states = snapshot();
        assert(states);
        assert(std::string_view(Validation::validate(states))
            == "RHI-WEBGPU-ORIGINAL-STATE-COPY-MISMATCH");
        if (retainLoss)
            WebGpuOriginalTransferBatchFixture::verifyRejected(
                states, "RHI-WEBGPU-ORIGINAL-STATE-COPY-MISMATCH");
        states.reset();
        set(*destination, ResourceState::CopyDst);
        states = snapshot();
        assert(states && !Validation::validate(states));
        if (retainLoss || verifyNative)
            WebGpuOriginalTransferBatchFixture::verify(states, retainLoss);
        if (retainLoss) {
            assert(lossCount_ < loss_.size());
            loss_[lossCount_++] = {states, stateQuota, quota};
        } else {
            states.reset();
            assert(!stateQuota->liveBytes());
        }
        set(*source, sourceState);
        set(*destination, destinationState);
        std::printf(
            "BINDLESS_RHI_ORIGINAL_TRANSFER_STREAM_OK %u 4 2 original-copy-state owner immutable allocation0\n",
            static_cast<unsigned>(copy.kind()));
    }

    struct LossProbe {
        std::shared_ptr<const rhi_webgpu::WebGpuOriginalResourceStates> states;
        std::shared_ptr<core::CpuMemoryQuota> stateQuota, inputQuota;
    };

    inline static std::array<LossProbe, 5> loss_{};
    inline static std::size_t lossCount_ = 0u;

#endif
};
} // namespace arp::test
