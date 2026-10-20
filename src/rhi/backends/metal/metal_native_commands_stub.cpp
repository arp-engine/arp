#include "core/cpu_allocator.h"
#include "rhi/backends/metal/metal_closed_consumer_native_preparation.h"
#include "rhi/backends/metal/metal_graphics_preparation.h"
#include "rhi/backends/metal/metal_native_commands.h"
#include "rhi/backends/metal/metal_native_descriptors.h"
#include "rhi/backends/metal/metal_native_pipelines.h"
#include "rhi/backends/metal/metal_native_resources.h"
#include "rhi/backends/metal/metal_ordinary_binding.h"
#include "rhi/backends/metal/metal_ordinary_graphics_write_execution.h"
#include "rhi/backends/metal/metal_original_bindless_command.h"
#include "rhi/backends/metal/metal_original_bindless_graphics_pass.h"
#include "rhi/backends/metal/metal_original_device_native_loss.h"
#include "rhi/backends/metal/metal_original_texture_upload.h"
#include "rhi/semantics/push_constant_validation.h"

namespace arp::rhi_metal {
std::shared_ptr<MetalRhiNativeCommandPayload> MetalOriginalTextureUploadCommand::prepareOriginal(
    std::shared_ptr<const Reservation>,
    std::shared_ptr<const Program>,
    std::shared_ptr<const Staging>,
    rhi::GpuDevice&,
    core::pmr::String& diagnostic)
{
    diagnostic = "RHI-METAL-UPLOAD-COMMAND-NATIVE-RUNTIME-UNAVAILABLE";
    return {};
}

bool MetalOriginalTextureUploadCommand::record(
    MetalRhiNativeCommandPayload&, core::pmr::String& diagnostic)
{
    diagnostic = "RHI-METAL-UPLOAD-COMMAND-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalOriginalBindlessCommand::nativeCompleted(bool) noexcept
{
    return false;
}

bool MetalOriginalBindlessGraphicsPass::createNativeObjects(core::pmr::String& diagnostic)
{
    diagnostic = "RHI-METAL-ROOT-GRAPHICS-PASS-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool metalRhiNativeDeviceRemovalResultsAvailable() noexcept
{
    return false;
}

std::shared_ptr<MetalRhiNativeCommandPayload> MetalOriginalBindlessCommand::prepareOriginal(
    std::shared_ptr<const Closure>,
    std::shared_ptr<const Reservation>,
    std::shared_ptr<const Binding>,
    rhi::GpuDevice&,
    core::pmr::String& diagnostic,
    bool,
    std::shared_ptr<const MetalOriginalBindlessGraphicsPass>,
    std::shared_ptr<const Closure>,
    std::shared_ptr<core::AtomicCapacityBudget>,
    std::shared_ptr<const Closure>,
    const rhi::OriginalIndexedDrawFetchContract::Budget&,
    const rhi::OriginalBindlessRootReadbackSequence::Input&,
    const rhi::OriginalBindlessRootNativeSequence<Closure>::Input&,
    const PassInput&,
    const rhi::OriginalBindlessProducedGeometryInputs&)
{
    diagnostic = "RHI-METAL-ROOT-COMMAND-NATIVE-RUNTIME-UNAVAILABLE";
    return {};
}

bool MetalOriginalBindlessCommand::record(
    MetalRhiNativeCommandPayload&, core::pmr::String& diagnostic)
{
    diagnostic = "RHI-METAL-ROOT-COMMAND-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

const char* MetalRhiClosedConsumerNativePreparation::validate() const noexcept
{
    return "RHI-METAL-CLOSED-CONSUMER-NATIVE-PREPARATION-RUNTIME-UNAVAILABLE";
}

const char* MetalRhiClosedConsumerNativePreparation::validateRecorded(bool) const noexcept
{
    return "RHI-METAL-CLOSED-CONSUMER-NATIVE-PREPARATION-RUNTIME-UNAVAILABLE";
}

const char* MetalRhiClosedConsumerNativePreparation::validateStorage(bool, bool) const noexcept
{
    return "RHI-METAL-CLOSED-CONSUMER-NATIVE-PREPARATION-RUNTIME-UNAVAILABLE";
}

bool MetalRhiNativeCommandPayload::recordClosedConsumer(
    const std::shared_ptr<const MetalRhiClosedConsumerNativePreparation>&,
    core::pmr::String& diagnostic)
{
    diagnostic = "RHI-METAL-CLOSED-CONSUMER-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

std::shared_ptr<MetalRhiNativeCommandPayload> createMetalRhiClosedConsumerNativeCommandPayload(
    const rhi::CanonicalClosedConsumerRecordingContract::Budget&,
    const core::pmr::String&,
    std::shared_ptr<MetalRhiResourceRegistry>,
    core::UniquePtr<rhi::common::CommandRetentionStorage>,
    core::pmr::String& diagnostic)
{
    diagnostic = "RHI-METAL-CLOSED-CONSUMER-NATIVE-RUNTIME-UNAVAILABLE";
    return {};
}

std::shared_ptr<const MetalRhiClosedConsumerNativePreparation>
MetalRhiClosedConsumerNativePreparation::prepare(std::shared_ptr<const Reservation>,
    rhi::GpuDevice&,
    Contract::Input,
    Budget,
    core::pmr::String& diagnostic)
{
    diagnostic = "RHI-METAL-CLOSED-CONSUMER-NATIVE-PREPARATION-RUNTIME-UNAVAILABLE";
    return {};
}

bool MetalRhiNativeCommandPayload::recordCanonicalProducer(
    const std::shared_ptr<const MetalRhiCanonicalProducerRecordingPreparation>&,
    core::pmr::String& diagnostic)
{
    diagnostic = "RHI-METAL-CANONICAL-PRODUCER-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

std::shared_ptr<MetalRhiNativeCommandPayload> createMetalRhiCanonicalProducerNativeCommandPayload(
    const rhi::CanonicalZeroBaseIndirectProducerRecordingContract::Budget&,
    const core::pmr::String&,
    std::shared_ptr<MetalRhiResourceRegistry>,
    core::UniquePtr<rhi::common::CommandRetentionStorage>,
    core::pmr::String& diagnostic)
{
    diagnostic = "RHI-METAL-CANONICAL-PRODUCER-NATIVE-RUNTIME-UNAVAILABLE";
    return {};
}

void MetalRhiNativeCommandPayload::cancelCanonicalProducer(bool) noexcept
{}

bool MetalRhiNativeCommandPayload::recordProtectedIndirectExecution(
    const std::shared_ptr<const MetalRhiIndirectExecutionPreparation>&,
    const rhi::BindlessResourceAccess&,
    const rhi::ResourceBarrierDesc&,
    const rhi::BindlessResourceAccess&,
    core::pmr::String& diagnostic)
{
    diagnostic = "RHI-METAL-INDIRECT-EXECUTION-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

std::shared_ptr<MetalRhiNativeCommandPayload> createMetalRhiIndirectOutputNativeCommandPayload(
    const rhi::GpuIndirectOutputRecordingBudget&,
    const core::pmr::String&,
    std::shared_ptr<MetalRhiResourceRegistry>,
    core::UniquePtr<rhi::common::CommandRetentionStorage>,
    core::pmr::String& diagnostic)
{
    diagnostic = "RHI-METAL-INDIRECT-EXECUTION-NATIVE-RUNTIME-UNAVAILABLE";
    return {};
}

std::shared_ptr<MetalRhiNativeCommandPayload> createMetalRhiOrdinaryIndirectNativeCommandPayload(
    const rhi::IndirectRecordingBudget&,
    const core::pmr::String&,
    std::shared_ptr<MetalRhiResourceRegistry>,
    core::UniquePtr<rhi::common::CommandRetentionStorage>,
    core::pmr::String& diagnostic)
{
    diagnostic = "RHI-METAL-ORDINARY-INDIRECT-NATIVE-RUNTIME-UNAVAILABLE";
    return {};
}

bool MetalRhiNativeCommandPayload::recordOrdinaryPreparedIndirectDraw(
    const std::shared_ptr<const MetalRhiOrdinaryPreparedIndirectArguments>&,
    const rhi::BindlessResourceAccess&,
    core::pmr::String& diagnostic)
{
    diagnostic = "RHI-METAL-ORDINARY-INDIRECT-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

std::shared_ptr<MetalRhiNativeCommandPayload> createMetalRhiOrdinaryWriteNativeCommandPayload(
    const MetalRhiOrdinaryWritePrototypeIntent&,
    const core::pmr::String&,
    std::shared_ptr<MetalRhiResourceRegistry>,
    core::UniquePtr<rhi::common::CommandRetentionStorage>,
    std::shared_ptr<core::MutationJournal>,
    core::pmr::String& diagnostic)
{
    diagnostic = "RHI-METAL-ORDINARY-WRITE-NATIVE-RUNTIME-UNAVAILABLE";
    return {};
}

bool MetalRhiNativeCommandPayload::recordOrdinaryPreparedWriteRenderPassBegin(
    const std::shared_ptr<const MetalRhiOrdinaryPreparedRenderPass>&,
    const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsWrite>&,
    core::pmr::String& diagnostic)
{
    diagnostic = "RHI-METAL-ORDINARY-WRITE-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordOrdinaryPreparedWriteDependency(
    const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsWrite>&,
    core::pmr::String& diagnostic)
{
    diagnostic = "RHI-METAL-ORDINARY-WRITE-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordOrdinaryPreparedWriteReadback(
    const std::shared_ptr<MetalRhiOrdinaryPreparedWriteReadback>&, core::pmr::String& diagnostic)
{
    diagnostic = "RHI-METAL-ORDINARY-WRITE-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordOrdinaryPreparedReadConsumerBegin(
    const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsReadConsumer>&,
    core::pmr::String& diagnostic)
{
    diagnostic = "RHI-METAL-ORDINARY-WRITE-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordOrdinaryPreparedIndexedDraw(
    const std::shared_ptr<const MetalRhiOrdinaryPreparedIndexedDraw>&,
    core::pmr::String& diagnostic)
{
    diagnostic = "RHI-METAL-ORDINARY-INDEX-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordOrdinaryPreparedVertexBinding(
    const std::shared_ptr<const MetalRhiOrdinaryPreparedVertexBinding>&,
    core::pmr::String& diagnostic)
{
    diagnostic = "RHI-METAL-ORDINARY-VERTEX-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordOrdinaryPreparedRenderPassBegin(
    const std::shared_ptr<const MetalRhiOrdinaryPreparedRenderPass>&,
    const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding>&,
    core::pmr::String& diagnostic,
    const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsPush>&)
{
    diagnostic = "RHI-METAL-ORDINARY-GRAPHICS-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordOrdinaryPreparedGraphicsBinding(
    const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding>&,
    core::pmr::String& diagnostic,
    const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsPush>&)
{
    diagnostic = "RHI-METAL-ORDINARY-GRAPHICS-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

std::shared_ptr<const MetalRhiOrdinaryPreparedRenderPass>
MetalRhiOrdinaryPreparedRenderPass::prepare(const rhi::RenderPassDesc&,
    std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider>,
    std::shared_ptr<MetalRhiResourceRegistry>,
    const MetalRhiOrdinaryBindingBudget&,
    std::shared_ptr<core::CpuMemoryQuota>,
    core::pmr::String& diagnostic)
{
    diagnostic = "RHI-METAL-ORDINARY-PASS-NATIVE-RUNTIME-UNAVAILABLE";
    return {};
}

std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding>
MetalRhiOrdinaryPreparedGraphicsBinding::prepare(
    std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider>,
    const rhi::DescriptorSet&,
    std::uint32_t,
    std::shared_ptr<MetalRhiResourceRegistry>,
    const MetalRhiOrdinaryBindingBudget&,
    std::shared_ptr<core::CpuMemoryQuota>,
    core::pmr::String& diagnostic,
    const std::uint32_t*,
    std::uint32_t)
{
    diagnostic = "RHI-METAL-ORDINARY-PREPARED-NATIVE-RUNTIME-UNAVAILABLE";
    return {};
}

bool validateMetalRhiNativeGraphicsBindings(
    const std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings>&,
    core::pmr::String& diagnostic)
{
    diagnostic = "RHI-METAL-ORDINARY-PREPARED-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

std::shared_ptr<const MetalRhiOrdinaryPreparedComputePush> prepareMetalRhiOrdinaryComputePush(
    const rhi::ComputePipeline&,
    const rhi::PushConstantsDesc&,
    const MetalRhiOrdinaryBindingBudget&,
    std::shared_ptr<core::CpuMemoryQuota>,
    core::pmr::String& diagnostic)
{
    diagnostic = "RHI-METAL-ORDINARY-PREPARED-NATIVE-RUNTIME-UNAVAILABLE";
    return {};
}

std::shared_ptr<const MetalRhiOrdinaryPreparedComputeBinding> prepareMetalRhiOrdinaryComputeBinding(
    const rhi::ComputePipeline&,
    const rhi::DescriptorSet&,
    std::uint32_t,
    std::shared_ptr<MetalRhiResourceRegistry>,
    const MetalRhiOrdinaryBindingBudget&,
    std::shared_ptr<core::CpuMemoryQuota>,
    core::pmr::String& diagnostic,
    const std::uint32_t*,
    std::uint32_t)
{
    diagnostic = "RHI-METAL-ORDINARY-PREPARED-NATIVE-RUNTIME-UNAVAILABLE";
    return {};
}

bool MetalRhiNativeCommandPayload::recordOrdinaryPreparedComputeBinding(
    const std::shared_ptr<const MetalRhiOrdinaryPreparedComputeBinding>&,
    core::pmr::String& diagnostic,
    const std::shared_ptr<const MetalRhiOrdinaryPreparedComputePush>&)
{
    diagnostic = "RHI-METAL-ORDINARY-PREPARED-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

std::shared_ptr<MetalRhiCompletionState>
MetalRhiNativeCommandPayload::preparedCompletionState() const
{
    return {};
}

bool MetalRhiNativeCommandPayload::prepareGraphicsPassScope(
    std::shared_ptr<const rhi::BindlessPreparedGraphicsBinding>,
    const rhi::BindlessAccessDeclarations&,
    core::pmr::String& diagnostic)
{
    diagnostic = "RHI-METAL-BINDLESS-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::prepareGraphicsPassScope(
    std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings>,
    std::shared_ptr<const rhi::BindlessFaultDomain>,
    const rhi::BindlessAccessDeclarations&,
    core::pmr::String& diagnostic)
{
    diagnostic = "RHI-METAL-BINDLESS-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordScopedGraphicsBinding(
    const std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings>&,
    const rhi::BindlessAccessDeclarations&,
    core::pmr::String& diagnostic)
{
    diagnostic = "RHI-METAL-BINDLESS-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordPreparedGraphicsBinding(
    const std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings>&,
    core::pmr::String& diagnostic)
{
    diagnostic = "RHI-METAL-PREPARED-GRAPHICS-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::validatePreparedGraphicsBinding(
    const std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings>&,
    core::pmr::String& diagnostic) const
{
    diagnostic = "RHI-METAL-PREPARED-GRAPHICS-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings> prepareMetalRhiNativeGraphicsBindings(
    std::shared_ptr<MetalRhiNativePipelinePayload>,
    std::shared_ptr<MetalRhiNativeDescriptorGroupPayload>,
    std::uint32_t,
    const std::array<std::shared_ptr<const MetalRhiPreparedGraphicsStageLayout>, 2>&,
    const rhi::BindlessGraphicsBindingBudget&,
    core::pmr::String& diagnostic)
{
    diagnostic = "RHI-METAL-PREPARED-GRAPHICS-NATIVE-RUNTIME-UNAVAILABLE";
    return {};
}

std::shared_ptr<const MetalRhiNativePreparedComputeBindings> prepareMetalRhiNativeComputeBindings(
    std::shared_ptr<MetalRhiNativePipelinePayload>,
    std::shared_ptr<MetalRhiNativeDescriptorGroupPayload>,
    std::uint32_t,
    const rhi::BindlessComputeBindingBudget&,
    core::pmr::String& diagnostic)
{
    diagnostic = "RHI-METAL-PREPARED-COMPUTE-NATIVE-RUNTIME-UNAVAILABLE";
    return {};
}

std::shared_ptr<const MetalRhiNativePreparedComputeBindings>
prepareMetalRhiNativeScalarComputeBindings(std::shared_ptr<MetalRhiNativePipelinePayload>,
    std::shared_ptr<MetalRhiNativeDescriptorGroupPayload>,
    std::uint32_t,
    core::pmr::String& diagnostic)
{
    diagnostic = "RHI-METAL-PREPARED-COMPUTE-NATIVE-RUNTIME-UNAVAILABLE";
    return {};
}

bool validateMetalRhiNativePreparedComputeBindings(
    const std::shared_ptr<const MetalRhiNativePreparedComputeBindings>&,
    core::pmr::String& diagnostic)
{
    diagnostic = "RHI-METAL-PREPARED-COMPUTE-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordPreparedComputeBinding(
    const std::shared_ptr<const MetalRhiNativePreparedComputeBindings>&,
    const std::shared_ptr<const rhi::BindlessPreparedComputeBinding>&,
    core::pmr::String& diagnostic)
{
    diagnostic = "RHI-METAL-PREPARED-COMPUTE-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::validatePreparedComputeBinding(
    const std::shared_ptr<const MetalRhiNativePreparedComputeBindings>&,
    const std::shared_ptr<const rhi::BindlessPreparedComputeBinding>&,
    core::pmr::String& diagnostic) const
{
    diagnostic = "RHI-METAL-PREPARED-COMPUTE-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

MetalReadbackCompletion MetalRhiNativeCommandPayload::readbackCompletionStatus() const
{
    // Mirror the native ordering: an abandoned payload keeps its terminal
    // Abandoned classification for bounded readback finalization instead of
    // degrading into the generic diagnostic-only failure below.
    if (abandoned_)
        return MetalReadbackCompletion::Abandoned;
    return MetalReadbackCompletion::Failed;
}

MetalRhiNativeCommandPayload::MetalRhiNativeCommandPayload(
    std::shared_ptr<MetalRhiNativeCommandStats> stats,
    std::shared_ptr<void> nativeQueue,
    std::shared_ptr<void> nativeCommandBuffer,
    std::shared_ptr<MetalRhiResourceRegistry> resourceRegistry,
    std::shared_ptr<core::AtomicCapacityLease> commandCapacity,
    std::uint32_t maxBindlessPipelines,
    std::uint32_t maxBindlessSnapshots,
    std::uint32_t maxBindlessGraphicsAccesses)
    : stats_(std::move(stats)), nativeQueue_(std::move(nativeQueue)),
      commandCapacity_(std::move(commandCapacity)),
      nativeCommandBuffer_(std::move(nativeCommandBuffer)),
      resourceRegistry_(std::move(resourceRegistry)),
      retainedResourcePayloads_(&core::defaultCpuMemoryResource()),
      retainedResourcePayloadIdentities_(&core::defaultCpuMemoryResource()),
      retainedStagingPayloads_(&core::defaultCpuMemoryResource()),
      retainedArgumentBuffers_(&core::defaultCpuMemoryResource()),
      retainedBindlessPipelines_(maxBindlessPipelines),
      retainedBindlessSnapshots_(maxBindlessSnapshots),
      retainedPreparedBindings_(maxBindlessSnapshots),
      graphicsAccesses_(maxBindlessGraphicsAccesses, rhi::BindlessAccessRetention::GraphicsPass),
      leasedBindlessFaults_(&core::defaultCpuMemoryResource()),
      pendingBindlessFaultReadbacks_(&core::defaultCpuMemoryResource()),
      bindlessFaultOutcomes_(&core::defaultCpuMemoryResource()),
      preparedFaultOutcomes_(&core::defaultCpuMemoryResource()),
      maxBindlessPipelines_(maxBindlessPipelines), maxBindlessSnapshots_(maxBindlessSnapshots),
      cachedArgumentBuffers_(&core::defaultCpuMemoryResource()),
      retainedQueryPools_(&core::defaultCpuMemoryResource()),
      recordedTimestampQueries_(&core::defaultCpuMemoryResource()),
      recordedPipelineStatisticsQueries_(&core::defaultCpuMemoryResource())
{}

bool MetalRhiNativeCommandPayload::recordBufferBarrier(std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-BUFFER-BARRIER-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordTextureBarrier(std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-TEXTURE-BARRIER-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordAliasingBarrier(const rhi::AliasingBarrierDesc&,
    const std::shared_ptr<MetalRhiNativeTexturePayload>&,
    const std::shared_ptr<MetalRhiNativeTexturePayload>&,
    std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-ALIAS-BARRIER-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordRenderPassBegin(
    const rhi::RenderPassDesc&, std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-RENDER-PASS-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordGraphicsPipelineBind(
    const std::shared_ptr<MetalRhiNativePipelinePayload>&, std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-RENDER-PIPELINE-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordVertexBufferBind(std::uint32_t,
    const std::shared_ptr<MetalRhiNativeBufferPayload>&,
    std::uint64_t,
    std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-VERTEX-BUFFER-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::validateVertexStreams(std::pmr::string& diagnostics) const
{
    if (!renderPipeline_ || !renderPipeline_->vertexStreams.success) {
        diagnostics = "RHI-METAL-VERTEX-STREAM-LOWERING-MISSING";
        return false;
    }
    for (std::uint32_t slot = 0; slot < vertexStreams_.size(); ++slot) {
        if (renderPipeline_->vertexStreams.slots[slot] != UINT32_MAX
            && !vertexStreams_[slot].buffer) {
            diagnostics = "RHI-METAL-VERTEX-STREAM-BINDING-MISSING";
            return false;
        }
    }
    return true;
}

bool MetalRhiNativeCommandPayload::recordIndexBufferBind(
    const std::shared_ptr<MetalRhiNativeBufferPayload>&,
    rhi::IndexFormat,
    std::uint64_t,
    std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-INDEX-BUFFER-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordTessellationFactorBufferBind(
    const std::shared_ptr<MetalRhiNativeBufferPayload>&,
    std::uint64_t,
    std::uint32_t,
    std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-TESSELLATION-FACTOR-BUFFER-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::validateTessellationFactorBufferRange(
    std::uint32_t, std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-TESSELLATION-FACTOR-BUFFER-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordDescriptorSetBind(std::uint32_t,
    const std::shared_ptr<MetalRhiNativeDescriptorGroupPayload>&,
    rhi::ShaderStage,
    const std::uint32_t*,
    std::uint32_t,
    std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-DESCRIPTOR-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordDescriptorTableBind(std::uint32_t,
    const std::shared_ptr<MetalRhiNativeDescriptorTablePayload>&,
    rhi::ShaderStage,
    std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-DESCRIPTOR-TABLE-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordSetViewport(
    const rhi::ViewportState& viewport, std::pmr::string& diagnostics)
{
    (void)viewport;
    diagnostics = "RHI-METAL-VIEWPORT-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordSetScissor(
    const rhi::ScissorState& scissor, std::pmr::string& diagnostics)
{
    (void)scissor;
    diagnostics = "RHI-METAL-SCISSOR-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordPushConstants(rhi::ShaderStage,
    std::uint32_t,
    std::uint32_t sizeBytes,
    const void*,
    std::pmr::string& diagnostics)
{
    // Non-Apple builds have no executable Metal lowering.
    if (sizeBytes == 0) {
        return false;
    }
    diagnostics = std::pmr::string(rhi::semantics::PushConstantDiagCode::kNativeRuntime)
        + "Metal push constants require an Apple Metal runtime";
    return false;
}

bool MetalRhiNativeCommandPayload::recordDrawIndexed(std::uint32_t indexCount,
    std::uint32_t,
    std::uint32_t,
    std::int32_t,
    std::uint32_t,
    std::pmr::string& diagnostics)
{
    (void)indexCount;
    diagnostics = "RHI-METAL-DRAW-INDEXED-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordDraw(std::uint32_t vertexCount,
    std::uint32_t,
    std::uint32_t,
    std::uint32_t,
    std::pmr::string& diagnostics)
{
    (void)vertexCount;
    diagnostics = "RHI-METAL-DRAW-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordRenderPassEnd(std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-RENDER-PASS-END-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordNextSubpass(std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-NEXT-SUBPASS-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordDrawIndexedIndirect(
    const std::shared_ptr<MetalRhiNativeBufferPayload>&,
    std::uint64_t,
    std::uint32_t,
    std::uint32_t,
    std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-DRAW-INDEXED-INDIRECT-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordGpuCountedIndirectCountBuffer(
    const std::shared_ptr<MetalRhiNativeBufferPayload>&,
    std::uint64_t,
    std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-GPU-COUNTED-INDIRECT-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordDrawIndexedIndirectCount(
    const std::shared_ptr<MetalRhiNativeBufferPayload>&,
    std::uint64_t,
    const std::shared_ptr<MetalRhiNativeBufferPayload>&,
    std::uint64_t,
    std::uint32_t,
    std::uint32_t,
    std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-DRAW-INDEXED-INDIRECT-COUNT-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordTimestamp(
    const std::shared_ptr<MetalRhiNativeQueryPoolPayload>&,
    std::uint32_t,
    std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-TIMESTAMP-QUERY-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordPipelineStatistics(
    const std::shared_ptr<MetalRhiNativeQueryPoolPayload>&,
    std::uint32_t,
    std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-PIPELINE-STATISTICS-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::executeSecondary(
    const std::shared_ptr<MetalRhiNativeSecondaryCommandPayload>&, std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-SECONDARY-ICB-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::executeSecondaryCommand(
    const std::shared_ptr<MetalRhiNativeSecondaryCommandPayload>&,
    std::uint32_t,
    std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-SECONDARY-ICB-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordComputePassBegin(std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-COMPUTE-PASS-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::beginPreparedNativeComputePass(
    const std::shared_ptr<core::ExternalObjectSlot>&, core::pmr::String& diagnostics)
{
    diagnostics = "RHI-METAL-COMPUTE-PASS-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordComputePipelineBind(
    const std::shared_ptr<MetalRhiNativePipelinePayload>&, std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-COMPUTE-PIPELINE-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::retainIndexedPipeline(
    const std::shared_ptr<const void>&, std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-BINDLESS-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::retainIndexedSnapshot(
    const std::shared_ptr<const void>&, std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-BINDLESS-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordBindlessFaultLatchClear(
    std::shared_ptr<const rhi::BindlessSnapshot>, rhi::ResourceState, std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-BINDLESS-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordBindlessFaultLatchClearBeforePass(
    std::shared_ptr<const rhi::BindlessFaultDomain>,
    rhi::ResourceState,
    core::pmr::String& diagnostics)
{
    diagnostics = "RHI-METAL-BINDLESS-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

std::shared_ptr<MetalRhiBindlessFaultReadback>
MetalRhiNativeCommandPayload::scheduleBindlessFaultReadback(
    std::shared_ptr<const rhi::BindlessFaultDomain>, std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-BINDLESS-NATIVE-RUNTIME-UNAVAILABLE";
    return {};
}

rhi::BindlessFaultReport MetalRhiNativeCommandPayload::bindlessFaultReport(
    std::uint64_t generation) const noexcept
{
    return {generation, rhi::BindlessFaultValidationStatus::Unavailable, 0u};
}

bool MetalRhiNativeCommandPayload::recordDispatch(
    std::uint32_t x, std::uint32_t y, std::uint32_t z, std::pmr::string& diagnostics)
{
    (void)x;
    (void)y;
    (void)z;
    diagnostics = "RHI-METAL-DISPATCH-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordComputePassEnd(std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-COMPUTE-PASS-END-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordDispatchIndirect(
    const std::shared_ptr<MetalRhiNativeBufferPayload>&,
    std::uint64_t,
    std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-DISPATCH-INDIRECT-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordBlitPassBegin(std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-BLIT-PASS-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::beginPreparedNativeBlitPass(
    const std::shared_ptr<core::ExternalObjectSlot>&, core::pmr::String& diagnostics)
{
    diagnostics = "RHI-METAL-BLIT-PASS-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordBlitCopy(
    const rhi::BufferCopyDesc&, std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-BLIT-COPY-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordBlitBufferToTexture(
    const rhi::BufferToTextureCopyDesc&, std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-BLIT-BUFFER-TEXTURE-COPY-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordUploadStagingBufferCopy(const std::shared_ptr<void>&,
    std::uint64_t,
    std::uint64_t,
    rhi::GpuResourceId,
    std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-STAGING-BUFFER-COPY-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordUploadStagingTextureCopy(const std::shared_ptr<void>&,
    std::uint64_t,
    const rhi::UploadTextureDesc&,
    std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-STAGING-TEXTURE-COPY-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordBlitTextureToBuffer(
    const rhi::TextureToBufferCopyDesc&, std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-BLIT-TEXTURE-BUFFER-COPY-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::preflightBlitTextureToBuffer(
    const rhi::TextureToBufferCopyDesc&, std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-BLIT-TEXTURE-BUFFER-COPY-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordBlitTextureCopy(
    const rhi::TextureToTextureCopyDesc&, std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-BLIT-TEXTURE-COPY-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordBlitPassEnd(std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-BLIT-PASS-END-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::finish(std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-COMMAND-BUFFER-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::submit(MetalRhiNativeQueueStats& queueStats,
    const std::shared_ptr<MetalRhiCompletionState>&,
    const std::shared_ptr<MetalRhiNativeSharedEventPayload>&,
    std::uint64_t,
    const std::shared_ptr<MetalRhiNativeSharedEventPayload>&,
    std::uint64_t,
    std::pmr::string& diagnostics)
{
    (void)queueStats;
    diagnostics = "RHI-METAL-COMPLETION-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

void MetalRhiNativeCommandPayload::abandonUnsubmitted()
{
    // Mirrors the Apple lowering: submitted GPU work is never abandoned, the
    // abandon state stays observable through readbackCompletionStatus(), and
    // the payload releases every handle it owns. Diagnostic-only payloads
    // hold no native objects, so the resets are structurally faithful no-ops.
    if (readbackWasSubmitted() || abandoned_) {
        return;
    }
    abandoned_ = true;
    stagingUploads_.reset();
    cancelClosedConsumerReservation(false);
    nativeRenderEncoder_.reset();
    nativeComputeEncoder_.reset();
    nativeBlitEncoder_.reset();
    nativeCommandBuffer_.reset();
    commandCapacity_.reset();
    retainedBindlessPipelines_.clear();
    retainedBindlessSnapshots_.clear();
}

void MetalRhiNativeCommandPayload::markStagingUpload(
    std::shared_ptr<std::atomic<std::uint64_t>> completedEpoch)
{
    stagingCompletedEpoch_ = std::move(completedEpoch);
}

std::shared_ptr<MetalRhiNativeCommandPayload> createMetalRhiNativeCommandPayload(
    const std::pmr::string&,
    std::shared_ptr<MetalRhiResourceRegistry> resourceRegistry,
    std::pmr::string* diagnostics,
    std::uint32_t maxBindlessPipelines,
    std::uint32_t maxBindlessSnapshots,
    std::uint32_t maxBindlessGraphicsAccesses,
    core::UniquePtr<rhi::common::CommandRetentionStorage> preparedRetention)
{
    if (preparedRetention) {
        if (diagnostics)
            *diagnostics = "RHI-METAL-COMMAND-RETENTION-NATIVE-RUNTIME-UNAVAILABLE";
        return {};
    }
    auto stats = arp::core::makeShared<MetalRhiNativeCommandStats>();
    stats->backendPayloadPresent = true;
    stats->nativeRuntimeDiagnosticOnly = true;
    stats->nativeCommandBufferCreated = true;
    return arp::core::makeShared<MetalRhiNativeCommandPayload>(stats,
        nullptr,
        nullptr,
        std::move(resourceRegistry),
        std::shared_ptr<core::AtomicCapacityLease>{},
        maxBindlessPipelines,
        maxBindlessSnapshots,
        maxBindlessGraphicsAccesses);
}

std::shared_ptr<MetalRhiNativeQueueStats> createMetalRhiNativeQueueStats()
{
    auto stats = arp::core::makeShared<MetalRhiNativeQueueStats>();
    stats->backendPayloadPresent = true;
    stats->nativeRuntimeDiagnosticOnly = true;
    return stats;
}

std::shared_ptr<core::AtomicCapacityLease> reserveSharedMetalRhiCommandCapacity(
    std::uint64_t, std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-COMMAND-CAPACITY-NATIVE-RUNTIME-UNAVAILABLE";
    return {};
}

MetalRhiCommandCapacityStats inspectSharedMetalRhiCommandCapacity() noexcept
{
    return {};
}

bool waitMetalRhiNativeQueueIdle(std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-WAIT-IDLE-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

MetalRhiNativeQueueDrainState drainMetalRhiNativeQueueForShutdown(
    const rhi::semantics::ShutdownDrainRequest&, std::pmr::string& diagnostics)
{
    diagnostics = "ARP-RHI-METAL-SHUTDOWN-DRAIN-NATIVE-RUNTIME-UNAVAILABLE";
    return MetalRhiNativeQueueDrainState::DiagnosticFailure;
}

void MetalRhiNativeCommandPayload::poisonRecording(std::string_view)
{
    cancelClosedConsumerReservation(true);
    recordingPoisoned_ = true;
}

bool MetalRhiNativeCommandPayload::recordPushDebugGroup(
    std::string_view, std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-DEBUG-GROUP-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

std::shared_ptr<void> sharedMetalRhiNativeCommandQueue()
{
    return {};
}

bool MetalRhiNativeCommandPayload::recordPopDebugGroup(std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-DEBUG-GROUP-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiNativeCommandPayload::recordInsertDebugMarker(
    std::string_view, std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-DEBUG-MARKER-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool metalRhiNativeCompletionAndAliasingCommandsAvailable()
{
    return false;
}

} // namespace arp::rhi_metal

namespace arp::rhi_metal {
bool MetalOriginalBindlessCommand::prepareStoragePasses(rhi::GpuDevice&,
    const std::shared_ptr<core::AtomicCapacityBudget>&,
    core::pmr::String& diagnostic)
{
    diagnostic = "RHI-METAL-ROOT-STORAGE-NATIVE-UNAVAILABLE";
    return false;
}

const char* MetalOriginalBindlessCommand::validateStoragePasses() const noexcept
{
    return "RHI-METAL-ROOT-STORAGE-NATIVE-UNAVAILABLE";
}
} // namespace arp::rhi_metal
