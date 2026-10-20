#include "core/bounded_retainer.h"
#include "core/cpu_allocator.h"
#include "null_backend_internal.h"
#include "null_bindless.h"
#include "rhi/frontend/command_attachment_validation.h"
#include "rhi/frontend/command_recorded_state.h"
#include "rhi/frontend/command_retention_validation.h"
#include "rhi/frontend/descriptor_bound_descriptor_slots.h"
#include "rhi/frontend/descriptor_resource_validation.h"
#include "rhi/frontend/descriptor_set_validation.h"
#include "rhi/frontend/resource_access_usage.h"
#include "rhi/frontend/resource_memory_barrier.h"
#include "rhi/gpu_extensions.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/semantics/push_constant_validation.h"
#include "rhi/semantics/viewport_scissor_validation.h"

#include <array>
#include <memory>
#include <string>
#include <utility>

// region Null command validation
namespace arp::rhi::null_backend {

namespace {

bool coversStages(ShaderStage available, ShaderStage required)
{
    return (available & required) == required;
}

bool pushConstantUpdateEndOverflows(std::uint32_t offsetBytes, std::uint32_t sizeBytes)
{
    return offsetBytes + sizeBytes < offsetBytes;
}

} // namespace

ValidationResult validateDescriptorTableBinding(
    const PipelineLayout* pipelineLayout, std::uint32_t set, const DescriptorTable& descriptorTable)
{
    if (!pipelineLayout) {
        return {false, "pipeline must be bound before descriptor table"};
    }
    if (!descriptorTable.isCurrentAllocation()) {
        return {false, "descriptor table bind rejected after descriptor arena reset"};
    }

    const auto slot =
        validateDescriptorTableSlot(pipelineLayout->desc(), set, descriptorTable.desc());
    if (!slot.success)
        return slot;
    return validateDescriptorResources(
        descriptorTable, DescriptorContentRequirement::WrittenElements);
}

ValidationResult validatePushConstantBinding(const PipelineLayout* pipelineLayout,
    ShaderStage allowedStages,
    ShaderStage stages,
    std::uint32_t offsetBytes,
    std::uint32_t sizeBytes)
{
    if (!pipelineLayout) {
        return {false, "pipeline must be bound before push constant update"};
    }
    if (stages == ShaderStage::None) {
        return {false, "push constant update requires shader stage visibility"};
    }
    if (!coversStages(allowedStages, stages)) {
        return {false, "push constant shader stage does not match command pass"};
    }
    if (sizeBytes == 0) {
        return {false, "push constant update size is empty"};
    }
    if (pushConstantUpdateEndOverflows(offsetBytes, sizeBytes)) {
        return {false, "push constant update offset and size overflow"};
    }

    const auto requestedEnd =
        static_cast<std::uint64_t>(offsetBytes) + static_cast<std::uint64_t>(sizeBytes);
    for (const auto& range : pipelineLayout->desc().pushConstantRanges) {
        const auto rangeEnd = static_cast<std::uint64_t>(range.offsetBytes)
            + static_cast<std::uint64_t>(range.sizeBytes);
        if (coversStages(range.stages, stages) && offsetBytes >= range.offsetBytes
            && requestedEnd <= rangeEnd) {
            return {true, ""};
        }
    }

    return {false, "push constant update is not covered by pipeline layout"};
}

// endregion Null command validation

// region Null commands

namespace {

enum class ActivePassKind {
    None,
    Render,
    Compute,
    Transfer,
};

struct RecorderState {
    RecorderState(const CommandEncoderDesc& desc, const BindlessCrossExtension* owner)
        : issuer(owner), retainedPipelines(desc.maxBindlessPipelines),
          retainedSnapshots(desc.maxBindlessSnapshots),
          indirectAccesses(desc.maxBindlessIndirectAccesses),
          retainedBindings(desc.maxBindlessSnapshots),
          graphicsAccesses(desc.maxBindlessGraphicsAccesses, BindlessAccessRetention::GraphicsPass)
    {}

    const BindlessCrossExtension* issuer = nullptr;
    std::uint64_t recordingIdentity = allocateGpuResourceId().value;
    core::BoundedRetainer<BindlessPipelineState> retainedPipelines;
    core::BoundedRetainer<BindlessSnapshot> retainedSnapshots;
    BindlessIndirectDeclarations indirectAccesses;
    core::BoundedRetainer<BindlessPreparedBinding> retainedBindings;
    BindlessRetainedDeclarations graphicsAccesses;
    std::shared_ptr<const BindlessPreparedGraphicsBinding> pendingGraphics;
    std::uint32_t commandCount = 0;
    bool finished = false;
    ActivePassKind activePass = ActivePassKind::None;
    CommandValidationResult lastValidation{true, "", 0, CommandValidationStatus::Success, ""};

    CommandValidationResult succeed()
    {
        lastValidation = {true, "", commandCount, CommandValidationStatus::Success, ""};
        return lastValidation;
    }

    CommandValidationResult record()
    {
        ++commandCount;
        return succeed();
    }

    CommandValidationResult fail(std::pmr::string diagnostics)
    {
        lastValidation = {false, std::move(diagnostics), commandCount};
        return lastValidation;
    }

    bool hasActivePass() const { return activePass != ActivePassKind::None; }
};

using RecorderStatePtr = std::shared_ptr<RecorderState>;

ResourceBarrierResult transitionStatefulResource(core::MutationJournal& journal,
    StatefulResource* resource,
    ResourceState before,
    ResourceState after)
{
    if (!resource) {
        return {false, "resource state transition rejected non-Null resource"};
    }
    if (auto* texture = dynamic_cast<StatefulTextureResource*>(resource))
        return texture->transitionState(before, after, &journal);
    if (!resource->beginStateRecording(journal))
        return {false, "RHI-RESOURCE-STATE-RECORDING-CONFLICT"};
    return resource->transitionState(before, after, &journal);
}

ResourceBarrierResult transitionStatefulTextureSubresource(core::MutationJournal& journal,
    StatefulTextureResource* resource,
    const TextureSubresourceRange& range,
    ResourceState before,
    ResourceState after)
{
    if (!resource) {
        return {false, "texture subresource state transition rejected non-Null resource"};
    }
    return resource->transitionSubresourceState(range, before, after, &journal);
}

bool isValidExtent(const Extent3D& extent)
{
    return extent.width > 0 && extent.height > 0 && extent.depth > 0;
}

bool isValidRange(const TextureSubresourceRange& range)
{
    return range.mipLevelCount > 0 && range.arrayLayerCount > 0;
}

ValidationResult validateRenderPassDesc(const RenderPassDesc& desc)
{
    const auto operations = semantics::validateRenderPassAttachmentOperations(desc);
    if (!operations.success)
        return operations;
    if (desc.layeredRendering) {
        return {false, "RHI-NULL-LAYERED-RENDERING-NOT-MODELED"};
    }
    if (desc.colorTargetName.empty() && desc.colorAttachments.empty()
        && !desc.hasDepthStencilAttachment) {
        return {false, "render pass requires at least one attachment"};
    }
    if (desc.colorAttachments.size() > 8) {
        return {false, "render pass color attachment capacity exceeded"};
    }
    for (const auto& attachment : desc.colorAttachments) {
        if (!attachment.target.isValid()) {
            return {false, "render pass color attachment target is invalid"};
        }
        if (attachment.format == TextureFormat::Unknown) {
            return {false, "render pass color attachment format is unknown"};
        }
        if (attachment.hasResolveTarget && !attachment.resolveTarget.isValid()) {
            return {false, "render pass resolve target is invalid"};
        }
    }
    if (desc.hasDepthStencilAttachment) {
        if (!desc.depthStencilAttachment.target.isValid()) {
            return {false, "render pass depth/stencil target is invalid"};
        }
        if (!isDepthStencilFormat(desc.depthStencilAttachment.format)) {
            return {false, "render pass depth/stencil attachment requires a depth/stencil format"};
        }
    }
    // Validate multi-subpass structure: attachment indices must be in range,
    // and dependency subpass indices must be in range. AGENTS.md: deterministic
    // diagnostic failure instead of silently accepting malformed subpass layout.
    const auto colorCount = static_cast<std::uint32_t>(desc.colorAttachments.size());
    const auto subpassCount = static_cast<std::uint32_t>(desc.subpasses.size());
    for (std::uint32_t sp = 0; sp < subpassCount; ++sp) {
        const auto& sd = desc.subpasses[sp];
        for (const auto ci : sd.colorAttachmentIndices) {
            if (ci >= colorCount) {
                return {false, "subpass color attachment index out of range"};
            }
        }
        for (const auto ii : sd.inputAttachmentIndices) {
            if (ii >= colorCount) {
                return {false, "subpass input attachment index out of range"};
            }
        }
    }
    for (const auto& dep : desc.subpassDependencies) {
        if (dep.srcSubpass >= subpassCount || dep.dstSubpass >= subpassCount) {
            return {false, "subpass dependency references out-of-range subpass"};
        }
    }
    return {true, ""};
}

ValidationResult validateBarrierDesc(
    const ResourceBarrierDesc& desc, GpuResourceId resource, bool textureBarrier)
{
    if (!resource.isValid() || !desc.resource.isValid() || desc.resource != resource) {
        return {false, "resource barrier target does not match resource"};
    }
    if (desc.before == desc.after) {
        return {false, "resource barrier rejected no-op state transition"};
    }
    if (desc.destinationStages == PipelineStage::None) {
        return {false, "resource barrier requires destination stage metadata"};
    }
    if (desc.after != ResourceState::Undefined && desc.destinationAccess == ResourceAccess::None) {
        return {false, "resource barrier requires destination access metadata"};
    }
    if (textureBarrier && !isValidRange(desc.textureSubresources)) {
        return {false, "resource barrier texture subresource range is invalid"};
    }
    return {true, ""};
}

ResourceBarrierResult applyBarrierMetadata(
    ResourceBarrierResult result, const ResourceBarrierDesc& desc, bool subresourceAware)
{
    result.subresourceAware = subresourceAware;
    result.stageAccessAware = true;
    result.sourceStages = desc.sourceStages;
    result.destinationStages = desc.destinationStages;
    result.sourceAccess = desc.sourceAccess;
    result.destinationAccess = desc.destinationAccess;
    return result;
}

CommandValidationResult validatePassOpen(
    const RecorderStatePtr& state, bool ended, ActivePassKind expectedPass)
{
    if (ended) {
        return state->fail("command pass already ended");
    }
    if (state->finished) {
        return state->fail("command encoder already finished");
    }
    if (state->activePass != expectedPass) {
        return state->fail("command pass is not active");
    }
    return state->succeed();
}

class NullCommandBuffer final : public RecordedStateCommandBuffer,
                                public BindlessCommandBufferExtension {
public:
    NullCommandBuffer(std::uint32_t commandCount,
        core::UniquePtr<core::MutationJournal> states,
        core::BoundedRetainer<BindlessPipelineState> pipelines,
        core::BoundedRetainer<BindlessSnapshot> snapshots,
        core::BoundedRetainer<BindlessPreparedBinding> bindings)
        : RecordedStateCommandBuffer(std::move(states)), commandCount_(commandCount),
          pipelines_(std::move(pipelines)), snapshots_(std::move(snapshots)),
          bindings_(std::move(bindings))
    {}

    std::uint32_t commandCount() const override { return commandCount_; }

    void* queryInterface(std::string_view extension) noexcept override
    {
        return extension == kExtensionBindlessCross
            ? static_cast<BindlessCommandBufferExtension*>(this)
            : nullptr;
    }

    BindlessFaultReport faultReport(std::uint64_t generation) const noexcept override
    {
        for (const auto& snapshot : snapshots_.values())
            if (snapshot->generation() == generation)
                return {generation, BindlessFaultValidationStatus::NotExecuted, 0u};
        return {generation, BindlessFaultValidationStatus::Unavailable, 0u};
    }

    ValidationResult validateBindless(const BindlessCrossExtension* issuer) const
    {
        for (const auto& pipeline : pipelines_.values())
            if (!pipeline->authorization->issuedBy(issuer, pipeline->authorization))
                return {false, "RHI-BINDLESS-SUBMIT-AUTHORIZATION-INVALID"};
        for (const auto& snapshot : snapshots_.values()) {
            const auto live = snapshot->validate();
            if (!live.success)
                return live;
        }
        for (const auto& binding : bindings_.values()) {
            const auto live = binding->validateOwners();
            if (!live.success)
                return live;
            if (!binding->authorization()
                || !binding->authorization()->issuedBy(issuer, binding->authorization()))
                return {false, "RHI-BINDLESS-SUBMIT-AUTHORIZATION-INVALID"};
        }
        return {true, {}};
    }

private:
    std::uint32_t commandCount_ = 0;
    core::BoundedRetainer<BindlessPipelineState> pipelines_;
    core::BoundedRetainer<BindlessSnapshot> snapshots_;
    core::BoundedRetainer<BindlessPreparedBinding> bindings_;
};

class NullRenderBundle final : public RenderBundle {
public:
    NullRenderBundle(std::uint32_t commandCount,
        bool patchList,
        std::uint32_t patchControlPointCount,
        bool tessellationFactorBufferBound)
        : commandCount_(commandCount), patchList_(patchList),
          patchControlPointCount_(patchControlPointCount),
          tessellationFactorBufferBound_(tessellationFactorBufferBound)
    {}

    std::uint32_t commandCount() const override { return commandCount_; }

    bool patchList() const { return patchList_; }

    std::uint32_t patchControlPointCount() const { return patchControlPointCount_; }

    bool tessellationFactorBufferBound() const { return tessellationFactorBufferBound_; }

private:
    std::uint32_t commandCount_ = 0;
    bool patchList_ = false;
    std::uint32_t patchControlPointCount_ = 0u;
    bool tessellationFactorBufferBound_ = false;
};

class NullSecondaryCommandBuffer final : public SecondaryCommandBuffer {
public:
    NullSecondaryCommandBuffer(std::uint32_t commandCount,
        bool patchList,
        std::uint32_t patchControlPointCount,
        bool tessellationFactorBufferBound)
        : commandCount_(commandCount), patchList_(patchList),
          patchControlPointCount_(patchControlPointCount),
          tessellationFactorBufferBound_(tessellationFactorBufferBound)
    {}

    std::uint32_t commandCount() const override { return commandCount_; }

    bool softSecondary() const override { return true; }

    bool patchList() const { return patchList_; }

    std::uint32_t patchControlPointCount() const { return patchControlPointCount_; }

    bool tessellationFactorBufferBound() const { return tessellationFactorBufferBound_; }

private:
    std::uint32_t commandCount_ = 0;
    bool patchList_ = false;
    std::uint32_t patchControlPointCount_ = 0u;
    bool tessellationFactorBufferBound_ = false;
};

CommandValidationResult bindIndexedSnapshot(const RecorderStatePtr& state,
    const std::shared_ptr<const BindlessPipelineState>& indexed,
    std::shared_ptr<const BindlessSnapshot>& boundSnapshot,
    const PipelineLayout* pipelineLayout,
    BoundDescriptorSlots& boundDescriptors,
    std::shared_ptr<const BindlessSnapshot> snapshot,
    const BindlessLoweringPlan& selected,
    std::uint64_t generation,
    const BindlessAccessDeclarations& accesses)
{
    boundSnapshot.reset();
    if (!indexed || !indexed->authorization->issuedBy(state->issuer, indexed->authorization))
        return state->fail("RHI-BINDLESS-RECORD-AUTHORIZATION-INVALID");
    if (!snapshot)
        return state->fail("RHI-BINDLESS-RECORD-SNAPSHOT-MISSING");
    if (!snapshot->sharedOwnerMatches(snapshot))
        return state->fail("RHI-BINDLESS-RECORD-SNAPSHOT-OWNER-MISMATCH");
    auto valid = snapshot->validateForRecord(selected, generation, accesses, indexed->activeStages);
    if (!valid.success)
        return state->fail(valid.diagnostics);
    valid = snapshot->validateForRecord(
        indexed->authorization->plan(), generation, accesses, indexed->activeStages);
    if (!valid.success)
        return state->fail(valid.diagnostics);
    if (!state->retainedSnapshots.canRetain(snapshot))
        return state->fail("RHI-BINDLESS-COMMAND-SNAPSHOT-BUDGET");
    const auto* descriptors = snapshot->descriptorSet();
    if (!descriptors)
        return state->fail("RHI-BINDLESS-RECORD-SNAPSHOT-INVALID");
    valid = validateDescriptorSetBinding(pipelineLayout, snapshot->setIndex(), *descriptors);
    if (!valid.success)
        return state->fail(valid.diagnostics);
    valid = boundDescriptors.bindSet(snapshot->setIndex(), *descriptors, nullptr, 0);
    if (!valid.success)
        return state->fail(valid.diagnostics);
    state->retainedSnapshots.retain(snapshot);
    boundSnapshot = std::move(snapshot);
    return state->record();
}

ValidationResult validateIndexedUse(const RecorderStatePtr& state,
    const std::shared_ptr<const BindlessPipelineState>& indexed,
    const std::shared_ptr<const BindlessSnapshot>& snapshot,
    bool scopeSatisfied = true)
{
    if (!scopeSatisfied)
        return {false, "RHI-BINDLESS-GRAPHICS-PASS-SCOPE-NOT-BOUND"};
    if (!indexed)
        return {true, {}};
    if (!indexed->authorization->issuedBy(state->issuer, indexed->authorization))
        return {false, "RHI-BINDLESS-RECORD-AUTHORIZATION-INVALID"};
    if (!snapshot)
        return {false, "RHI-BINDLESS-RECORD-SNAPSHOT-REQUIRED"};
    return snapshot->validate();
}

class NullRenderPassEncoder final : public RenderPassEncoder, public BindlessPassExtension {
public:
    CommandValidationResult bindPreparedGraphicsBinding(
        std::shared_ptr<const BindlessPreparedGraphicsBinding> binding,
        const BindlessAccessDeclarations& accesses) override
    {
        const auto pass = validatePassOpen(state_, ended_, ActivePassKind::Render);
        if (!pass.success)
            return pass;
        if (!binding || !binding->sharedOwnerMatches(binding)
            || !isPreparedGraphicsBinding(*binding))
            return state_->fail("RHI-BINDLESS-PREPARED-GRAPHICS-PAYLOAD-MISMATCH");
        if (scope_ && (binding != scope_ || !state_->graphicsAccesses.matches(accesses)))
            return state_->fail("RHI-BINDLESS-GRAPHICS-PASS-SCOPE-MISMATCH");
        if (!activePipeline_ || binding->owners()->desc().pipeline.get() != activePipeline_)
            return state_->fail("RHI-BINDLESS-PREPARED-GRAPHICS-PIPELINE-MISMATCH");
        const auto valid = binding->validateForRecord(*activePipeline_, accesses);
        if (!valid.success)
            return state_->fail(valid.diagnostics);
        if (!state_->retainedBindings.canRetain(binding))
            return state_->fail("RHI-BINDLESS-PREPARED-GRAPHICS-COMMAND-BUDGET");
        const auto& desc = binding->owners()->desc();
        const auto result =
            bindSnapshotImpl(desc.snapshot, desc.plan, desc.generation, accesses, true);
        if (result.success) {
            state_->retainedBindings.retain(std::move(binding));
            scopeBound_ = true;
        }
        return result;
    }

    std::uint64_t recordingIdentity() const noexcept override { return state_->recordingIdentity; }

    explicit NullRenderPassEncoder(RecorderStatePtr state,
        std::uint32_t subpassCount,
        const RenderPassDesc& desc,
        std::shared_ptr<const BindlessPreparedGraphicsBinding> scope)
        : state_(std::move(state)), passDesc_(desc), subpassCount_(subpassCount),
          scope_(std::move(scope))
    {}

    void* queryInterface(std::string_view extension) noexcept override
    {
        return extension == kExtensionBindlessCross ? static_cast<BindlessPassExtension*>(this)
                                                    : nullptr;
    }

    CommandValidationResult bindSnapshot(std::shared_ptr<const BindlessSnapshot> snapshot,
        const BindlessLoweringPlan& selected,
        std::uint64_t generation,
        const BindlessAccessDeclarations& accesses) override
    {
        return bindSnapshotImpl(std::move(snapshot), selected, generation, accesses, false);
    }

    CommandValidationResult bindSnapshotImpl(std::shared_ptr<const BindlessSnapshot> snapshot,
        const BindlessLoweringPlan& selected,
        std::uint64_t generation,
        const BindlessAccessDeclarations& accesses,
        bool prepared)
    {
        const auto pass = validatePassOpen(state_, ended_, ActivePassKind::Render);
        if (!pass.success)
            return pass;
        if (scope_ && !prepared)
            return state_->fail("RHI-BINDLESS-GRAPHICS-PASS-SCOPE-PREPARED-BINDING-REQUIRED");
        const auto captured = state_->indirectAccesses.capture(accesses);
        if (!captured.success) {
            boundSnapshot_.reset();
            return state_->fail(captured.diagnostics);
        }
        const auto bound = bindIndexedSnapshot(state_,
            indexed_,
            boundSnapshot_,
            pipelineLayout_,
            boundDescriptors_,
            std::move(snapshot),
            selected,
            generation,
            accesses);
        if (!bound.success)
            state_->indirectAccesses.clear();
        return bound;
    }

    CommandValidationResult setPipeline(const RenderPipeline& pipeline) override
    {
        const auto validation = validatePassOpen(state_, ended_, ActivePassKind::Render);
        if (!validation.success) {
            return validation;
        }
        if (scope_ && scope_->owners()->desc().pipeline.get() != &pipeline)
            return state_->fail("RHI-BINDLESS-GRAPHICS-PASS-SCOPE-PIPELINE-MISMATCH");
        pipelineBound_ = false;
        activePipeline_ = nullptr;
        scopeBound_ = false;
        boundSnapshot_.reset();
        indexed_.reset();
        state_->indirectAccesses.clear();
        auto indexed = bindlessState(pipeline);
        if (indexed) {
            const auto compatible = validateBindlessRenderPass(indexed->renderState, passDesc_);
            if (!compatible.success)
                return state_->fail(compatible.diagnostics);
            viewportSet_ = false;
            scissorSet_ = false;
            if (!indexed->authorization->issuedBy(state_->issuer, indexed->authorization))
                return state_->fail("RHI-BINDLESS-RECORD-AUTHORIZATION-INVALID");
            if (!state_->retainedPipelines.canRetain(indexed))
                return state_->fail("RHI-BINDLESS-COMMAND-PIPELINE-BUDGET");
        } else if (!pipeline.shaderReflection().bindlessContractVersion.empty()) {
            return state_->fail("RHI-BINDLESS-RECORD-AUTHORIZATION-INVALID");
        }
        if (!pipeline.layout()) {
            return state_->fail("render pipeline has no pipeline layout");
        }
        pipelineLayout_ = pipeline.layout();
        shaderReflection_ = &pipeline.shaderReflection();
        pipeline_usesPatchListTessellation_ = pipeline.usesPatchListTessellation();
        boundDescriptors_.reset();
        pipelineBound_ = true;
        activePipeline_ = &pipeline;
        if (indexed) {
            state_->retainedPipelines.retain(indexed);
            indexed_ = std::move(indexed);
        }
        return state_->record();
    }

    CommandValidationResult setViewport(const ViewportState& viewport) override
    {
        const auto validation = validatePassOpen(state_, ended_, ActivePassKind::Render);
        if (!validation.success) {
            // Wrap the backend's generic pass-not-open diagnostic with the
            // stable [VS-E001] prefix so the contract test can assert on a
            // backend-agnostic code (AGENTS.md: stable diagnostic id).
            return state_->fail(
                std::pmr::string(rhi::semantics::ViewportScissorDiagCode::kPassNotOpen)
                + validation.diagnostics);
        }
        // L2 invariant: pipeline must be bound before dynamic viewport state.
        // Matches the gate drawIndexed applies; prevents setting rasterizer
        // state with no pipeline to anchor it to (AGENTS.md RHI Rule 4: no
        // implicit state). The stable diagnostic code is asserted by
        // rhi_dynamic_viewport_scissor_contract_tests.
        if (!pipelineBound_) {
            return state_->fail(
                std::pmr::string(rhi::semantics::ViewportScissorDiagCode::kNoPipeline)
                + "render pipeline must be bound before setViewport");
        }
        if (indexed_ && !indexed_->renderState.dynamicViewportScissor)
            return state_->fail("RHI-BINDLESS-RECORD-DYNAMIC-STATE-NOT-ENABLED");
        const auto valueResult = rhi::semantics::validateViewportCommand(viewport);
        if (!valueResult.success) {
            return state_->fail(valueResult.diagnostics);
        }
        viewportSet_ = true;
        return state_->record();
    }

    CommandValidationResult setScissor(const ScissorState& scissor) override
    {
        const auto validation = validatePassOpen(state_, ended_, ActivePassKind::Render);
        if (!validation.success) {
            return state_->fail(
                std::pmr::string(rhi::semantics::ViewportScissorDiagCode::kPassNotOpen)
                + validation.diagnostics);
        }
        if (!pipelineBound_) {
            return state_->fail(
                std::pmr::string(rhi::semantics::ViewportScissorDiagCode::kNoPipeline)
                + "render pipeline must be bound before setScissor");
        }
        if (indexed_ && !indexed_->renderState.dynamicViewportScissor)
            return state_->fail("RHI-BINDLESS-RECORD-DYNAMIC-STATE-NOT-ENABLED");
        const auto valueResult = rhi::semantics::validateScissorCommand(scissor);
        if (!valueResult.success) {
            return state_->fail(valueResult.diagnostics);
        }
        scissorSet_ = true;
        return state_->record();
    }

    CommandValidationResult bindVertexBuffer(
        std::uint32_t, const GpuBuffer&, std::uint64_t) override
    {
        const auto validation = validatePassOpen(state_, ended_, ActivePassKind::Render);
        return validation.success ? state_->record() : validation;
    }

    CommandValidationResult bindIndexBuffer(const GpuBuffer&, IndexFormat, std::uint64_t) override
    {
        const auto validation = validatePassOpen(state_, ended_, ActivePassKind::Render);
        return validation.success ? state_->record() : validation;
    }

    CommandValidationResult bindTessellationFactorBuffer(const GpuBuffer& buffer,
        std::uint64_t offsetBytes,
        std::uint32_t instanceStrideBytes) override
    {
        const auto validation = validatePassOpen(state_, ended_, ActivePassKind::Render);
        if (!validation.success) {
            return validation;
        }
        if (pipelineBound_ && !pipeline_usesPatchListTessellation_) {
            return state_->fail("RHI-NULL-TESSELLATION-FACTOR-BUFFER-REQUIRES-PATCHLIST");
        }
        if ((buffer.desc().usage & ResourceUsage::StorageBuffer) == ResourceUsage::None
            || instanceStrideBytes == 0u || (offsetBytes & 3u) != 0u
            || (instanceStrideBytes & 3u) != 0u || offsetBytes > buffer.desc().sizeBytes
            || buffer.desc().sizeBytes - offsetBytes < sizeof(std::uint32_t)) {
            return state_->fail("RHI-NULL-TESSELLATION-FACTOR-BUFFER-DESC-INVALID");
        }
        tessellationFactorBufferBound_ = true;
        return state_->record();
    }

    CommandValidationResult bindDescriptorSet(std::uint32_t set,
        const DescriptorSet& descriptorSet,
        const std::uint32_t* dynamicOffsets,
        std::uint32_t dynamicOffsetCount) override
    {
        const auto validation = validatePassOpen(state_, ended_, ActivePassKind::Render);
        if (!validation.success) {
            return validation;
        }
        if (indexed_) {
            boundSnapshot_.reset();
            return state_->fail("RHI-BINDLESS-RECORD-SNAPSHOT-REQUIRED");
        }
        const auto bindingValidation =
            validateDescriptorSetBinding(pipelineLayout_, set, descriptorSet);
        if (!bindingValidation.success) {
            return state_->fail(bindingValidation.diagnostics);
        }
        const auto trackingValidation =
            boundDescriptors_.bindSet(set, descriptorSet, dynamicOffsets, dynamicOffsetCount);
        if (!trackingValidation.success) {
            return state_->fail(trackingValidation.diagnostics);
        }
        return state_->record();
    }

    CommandValidationResult bindDescriptorTable(
        std::uint32_t set, const DescriptorTable& descriptorTable) override
    {
        const auto validation = validatePassOpen(state_, ended_, ActivePassKind::Render);
        if (!validation.success) {
            return validation;
        }
        if (indexed_) {
            boundSnapshot_.reset();
            return state_->fail("RHI-BINDLESS-RECORD-SNAPSHOT-REQUIRED");
        }
        const auto bindingValidation =
            validateDescriptorTableBinding(pipelineLayout_, set, descriptorTable);
        if (!bindingValidation.success) {
            return state_->fail(bindingValidation.diagnostics);
        }
        const auto trackingValidation = boundDescriptors_.bindTable(set, descriptorTable);
        if (!trackingValidation.success) {
            return state_->fail(trackingValidation.diagnostics);
        }
        return state_->record();
    }

    CommandValidationResult pushConstants(ShaderStage stages,
        std::uint32_t offsetBytes,
        std::uint32_t sizeBytes,
        const void* data) override
    {
        const auto validation = validatePassOpen(state_, ended_, ActivePassKind::Render);
        if (!validation.success) {
            return validation;
        }
        auto pcCmdResult =
            rhi::semantics::validatePushConstantCommand(stages, offsetBytes, sizeBytes, data);
        if (!pcCmdResult.success) {
            return state_->fail(pcCmdResult.diagnostics);
        }
        auto pcBindResult = rhi::semantics::validatePushConstantBinding(pipelineLayout_,
            ShaderStage::Vertex | ShaderStage::Fragment,
            stages,
            offsetBytes,
            sizeBytes,
            shaderReflection_);
        if (!pcBindResult.success) {
            return state_->fail(pcBindResult.diagnostics);
        }
        if (pushState_.hasUnconsumedUpdate()) {
            return state_->fail(
                std::pmr::string(rhi::semantics::PushConstantDiagCode::kDuplicateUnconsumed)
                + "push constants already set without intervening draw/dispatch");
        }
        pushState_.onPush();
        return state_->record();
    }

    CommandValidationResult executeSecondary(const SecondaryCommandBuffer& commandBuffer) override
    {
        if (indexed_)
            return state_->fail("RHI-BINDLESS-SECONDARY-CONTRACT-UNAVAILABLE");
        const auto descriptorUse = boundDescriptors_.validateLiveBindings();
        if (!descriptorUse.success)
            return state_->fail(descriptorUse.diagnostics);
        const auto validation = validatePassOpen(state_, ended_, ActivePassKind::Render);
        if (!validation.success) {
            return validation;
        }
        if (!pipelineBound_) {
            return state_->fail("render pipeline must be bound before executeSecondary");
        }
        const auto* secondary = dynamic_cast<const NullSecondaryCommandBuffer*>(&commandBuffer);
        if (!secondary) {
            return state_->fail("RHI-NULL-SECONDARY-COMMAND-BUFFER-TYPE-INVALID");
        }
        if (pipeline_usesPatchListTessellation_ != secondary->patchList()
            || (pipeline_usesPatchListTessellation_
                && (!secondary->tessellationFactorBufferBound()
                    || secondary->patchControlPointCount() == 0u
                    || (shaderReflection_
                        && shaderReflection_->metalTessellationControlPointCount != 0u
                        && shaderReflection_->metalTessellationControlPointCount
                            != secondary->patchControlPointCount())))) {
            return state_->fail("RHI-NULL-SECONDARY-PATCH-CONTRACT-MISMATCH");
        }
        state_->commandCount += commandBuffer.commandCount();
        return state_->succeed();
    }

    CommandValidationResult executeRenderBundle(const RenderBundle& bundle) override
    {
        if (!pipelineBound_)
            return state_->fail("RHI-BINDLESS-RECORD-PIPELINE-REQUIRED");
        if (indexed_)
            return state_->fail("RHI-BINDLESS-BUNDLE-CONTRACT-UNAVAILABLE");
        const auto descriptorUse = boundDescriptors_.validateLiveBindings();
        if (!descriptorUse.success)
            return state_->fail(descriptorUse.diagnostics);
        const auto validation = validatePassOpen(state_, ended_, ActivePassKind::Render);
        if (!validation.success) {
            return validation;
        }
        if (!pipelineBound_) {
            return state_->fail("render pipeline must be bound before executeRenderBundle");
        }
        const auto* renderBundle = dynamic_cast<const NullRenderBundle*>(&bundle);
        if (!renderBundle) {
            return state_->fail("RHI-NULL-RENDER-BUNDLE-TYPE-INVALID");
        }
        if (pipeline_usesPatchListTessellation_ != renderBundle->patchList()
            || (pipeline_usesPatchListTessellation_
                && (!renderBundle->tessellationFactorBufferBound()
                    || renderBundle->patchControlPointCount() == 0u
                    || (shaderReflection_
                        && shaderReflection_->metalTessellationControlPointCount != 0u
                        && shaderReflection_->metalTessellationControlPointCount
                            != renderBundle->patchControlPointCount())))) {
            return state_->fail("RHI-NULL-RENDER-BUNDLE-PATCH-CONTRACT-MISMATCH");
        }
        state_->commandCount += bundle.commandCount();
        return state_->succeed();
    }

    CommandValidationResult drawIndexed(
        std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) override
    {
        const auto validation = validatePassOpen(state_, ended_, ActivePassKind::Render);
        if (!validation.success) {
            return validation;
        }
        if (!pipelineBound_) {
            return state_->fail("render pipeline must be bound before drawIndexed");
        }
        if (pipeline_usesPatchListTessellation_ && !tessellationFactorBufferBound_) {
            return state_->fail("RHI-NULL-PATCH-DRAW-REQUIRES-TESSELLATION-FACTOR-BUFFER");
        }
        if (indexed_ && indexed_->renderState.dynamicViewportScissor
            && (!viewportSet_ || !scissorSet_))
            return state_->fail("RHI-BINDLESS-RECORD-VIEWPORT-SCISSOR-REQUIRED");
        const auto indexedUse =
            validateIndexedUse(state_, indexed_, boundSnapshot_, !scope_ || scopeBound_);
        if (!indexedUse.success)
            return state_->fail(indexedUse.diagnostics);
        const auto descriptorValidation = boundDescriptors_.validateComplete(
            pipelineLayout_, shaderReflection_, ShaderStage::Vertex | ShaderStage::Fragment);
        if (!descriptorValidation.success) {
            return state_->fail(descriptorValidation.diagnostics);
        }
        pushState_.onConsume();
        return state_->record();
    }

    CommandValidationResult drawIndexedIndirect(const GpuBuffer& indirect,
        std::uint64_t offset,
        std::uint32_t count,
        std::uint32_t stride) override
    {
        if (!pipelineBound_)
            return state_->fail("RHI-BINDLESS-RECORD-PIPELINE-REQUIRED");
        if (indexed_) {
            const auto valid = validateBindlessIndirectDrawAccess(
                indirect, offset, count, stride, state_->indirectAccesses);
            if (!valid.success)
                return state_->fail(valid.diagnostics);
            return state_->fail("RHI-BINDLESS-INDIRECT-CONTRACT-UNAVAILABLE");
        }
        const auto pass = validatePassOpen(state_, ended_, ActivePassKind::Render);
        if (!pass.success)
            return pass;
        const auto descriptors = boundDescriptors_.validateLiveBindings();
        if (!descriptors.success)
            return state_->fail(descriptors.diagnostics);
        pushState_.onConsume();
        return state_->record();
    }

    CommandValidationResult drawIndexedIndirectCount(const GpuBuffer&,
        std::uint64_t,
        const GpuBuffer&,
        std::uint64_t,
        std::uint32_t,
        std::uint32_t) override
    {
        if (!pipelineBound_)
            return state_->fail("RHI-BINDLESS-RECORD-PIPELINE-REQUIRED");
        if (indexed_)
            return state_->fail("RHI-BINDLESS-INDIRECT-CONTRACT-UNAVAILABLE");
        const auto pass = validatePassOpen(state_, ended_, ActivePassKind::Render);
        if (!pass.success)
            return pass;
        const auto descriptors = boundDescriptors_.validateLiveBindings();
        if (!descriptors.success)
            return state_->fail(descriptors.diagnostics);
        pushState_.onConsume();
        return state_->record();
    }

    CommandValidationResult draw(
        std::uint32_t vertexCount, std::uint32_t, std::uint32_t, std::uint32_t) override
    {
        const auto validation = validatePassOpen(state_, ended_, ActivePassKind::Render);
        if (!validation.success) {
            return validation;
        }
        if (!pipelineBound_) {
            return state_->fail("render pipeline must be bound before draw");
        }
        if (pipeline_usesPatchListTessellation_ && !tessellationFactorBufferBound_) {
            return state_->fail("RHI-NULL-PATCH-DRAW-REQUIRES-TESSELLATION-FACTOR-BUFFER");
        }
        if (vertexCount == 0) {
            return state_->fail("draw requires vertexCount > 0");
        }
        if (indexed_ && indexed_->renderState.dynamicViewportScissor
            && (!viewportSet_ || !scissorSet_))
            return state_->fail("RHI-BINDLESS-RECORD-VIEWPORT-SCISSOR-REQUIRED");
        const auto indexedUse =
            validateIndexedUse(state_, indexed_, boundSnapshot_, !scope_ || scopeBound_);
        if (!indexedUse.success)
            return state_->fail(indexedUse.diagnostics);
        const auto descriptorValidation = boundDescriptors_.validateComplete(
            pipelineLayout_, shaderReflection_, ShaderStage::Vertex | ShaderStage::Fragment);
        if (!descriptorValidation.success) {
            return state_->fail(descriptorValidation.diagnostics);
        }
        pushState_.onConsume();
        return state_->record();
    }

    CommandValidationResult writeTimestamp(const QueryPool&, std::uint32_t) override { return {}; }

    CommandValidationResult nextSubpass() override
    {
        const auto validation = validatePassOpen(state_, ended_, ActivePassKind::Render);
        if (!validation.success) {
            return validation;
        }
        if (indexed_)
            return state_->fail("RHI-BINDLESS-RENDER-PASS-ABI-UNAVAILABLE");
        // Bounds check: reject advancing past the last declared subpass.
        // Matches the Vulkan backend's nextSubpass bounds check.
        if (subpassCount_ > 1 && subpassIndex_ + 1 >= subpassCount_) {
            return state_->fail("null backend nextSubpass exceeds subpass count");
        }
        ++subpassIndex_;
        // L2 invariant: dynamic viewport/scissor does NOT persist across
        // subpass boundaries. Each subpass must re-issue setViewport /
        // setScissor if it needs a non-default rect. Resetting the tracking
        // flags here lets the contract test assert that a setViewport issued
        // before nextSubpass is "forgotten" by the next subpass.
        boundSnapshot_.reset();
        viewportSet_ = false;
        scissorSet_ = false;
        tessellationFactorBufferBound_ = false;
        return state_->record();
    }

    std::uint32_t currentSubpassIndex() const override { return subpassIndex_; }

    CommandValidationResult end() override
    {
        const auto validation = validatePassOpen(state_, ended_, ActivePassKind::Render);
        if (!validation.success) {
            return validation;
        }
        if (scope_ && !scopeBound_)
            return state_->fail("RHI-BINDLESS-GRAPHICS-PASS-SCOPE-NOT-BOUND");
        ended_ = true;
        state_->activePass = ActivePassKind::None;
        pushState_.onPassEnd();
        boundDescriptors_.reset();
        boundSnapshot_.reset();
        indexed_.reset();
        state_->indirectAccesses.clear();
        scope_.reset();
        state_->graphicsAccesses.clear();
        return state_->record();
    }

private:
    RecorderStatePtr state_;
    RenderPassDesc passDesc_;
    std::shared_ptr<const BindlessPipelineState> indexed_;
    std::shared_ptr<const BindlessSnapshot> boundSnapshot_;
    const PipelineLayout* pipelineLayout_ = nullptr;
    const ShaderReflectionDesc* shaderReflection_ = nullptr;
    BoundDescriptorSlots boundDescriptors_;
    rhi::semantics::PushConstantConsumptionState pushState_;
    bool ended_ = false;
    bool pipelineBound_ = false;
    const RenderPipeline* activePipeline_ = nullptr;
    bool pipeline_usesPatchListTessellation_ = false;
    bool tessellationFactorBufferBound_ = false;
    bool viewportSet_ = false; // tracks setViewport for nextSubpass reset
    bool scissorSet_ = false;  // tracks setScissor for nextSubpass reset
    std::uint32_t subpassIndex_ = 0;
    std::uint32_t subpassCount_ = 1;
    std::shared_ptr<const BindlessPreparedGraphicsBinding> scope_;
    bool scopeBound_ = false;
};

class NullComputePassEncoder final : public ComputePassEncoder, public BindlessPassExtension {
public:
    CommandValidationResult bindPreparedComputeBinding(
        std::shared_ptr<const BindlessPreparedComputeBinding> binding,
        const BindlessAccessDeclarations& accesses) override
    {
        const auto pass = validatePassOpen(state_, ended_, ActivePassKind::Compute);
        if (!pass.success)
            return pass;
        if (!binding || !binding->sharedOwnerMatches(binding)
            || !isPreparedComputeBinding(*binding))
            return state_->fail("RHI-BINDLESS-PREPARED-COMPUTE-PAYLOAD-MISMATCH");
        if (!activePipeline_ || binding->owners()->desc().pipeline.get() != activePipeline_)
            return state_->fail("RHI-BINDLESS-PREPARED-COMPUTE-PIPELINE-MISMATCH");
        const auto valid =
            binding->validateForRecord(*binding->owners()->desc().pipeline, accesses);
        if (!valid.success)
            return state_->fail(valid.diagnostics);
        if (!state_->retainedBindings.canRetain(binding))
            return state_->fail("RHI-BINDLESS-PREPARED-COMPUTE-COMMAND-BUDGET");
        const auto& desc = binding->owners()->desc();
        const auto result = bindSnapshot(desc.snapshot, desc.plan, desc.generation, accesses);
        if (result.success)
            state_->retainedBindings.retain(std::move(binding));
        return result;
    }

    std::uint64_t recordingIdentity() const noexcept override { return state_->recordingIdentity; }

    explicit NullComputePassEncoder(RecorderStatePtr state) : state_(std::move(state)) {}

    void* queryInterface(std::string_view extension) noexcept override
    {
        return extension == kExtensionBindlessCross ? static_cast<BindlessPassExtension*>(this)
                                                    : nullptr;
    }

    CommandValidationResult bindSnapshot(std::shared_ptr<const BindlessSnapshot> snapshot,
        const BindlessLoweringPlan& selected,
        std::uint64_t generation,
        const BindlessAccessDeclarations& accesses) override
    {
        const auto pass = validatePassOpen(state_, ended_, ActivePassKind::Compute);
        if (!pass.success)
            return pass;
        return bindIndexedSnapshot(state_,
            indexed_,
            boundSnapshot_,
            pipelineLayout_,
            boundDescriptors_,
            std::move(snapshot),
            selected,
            generation,
            accesses);
    }

    CommandValidationResult setPipeline(const ComputePipeline& pipeline) override
    {
        const auto validation = validatePassOpen(state_, ended_, ActivePassKind::Compute);
        if (!validation.success) {
            return validation;
        }
        pipelineBound_ = false;
        boundSnapshot_.reset();
        indexed_.reset();
        activePipeline_ = nullptr;
        auto indexed = bindlessState(pipeline);
        if (indexed) {
            if (!indexed->authorization->issuedBy(state_->issuer, indexed->authorization))
                return state_->fail("RHI-BINDLESS-RECORD-AUTHORIZATION-INVALID");
            if (!state_->retainedPipelines.canRetain(indexed))
                return state_->fail("RHI-BINDLESS-COMMAND-PIPELINE-BUDGET");
        } else if (!pipeline.shaderReflection().bindlessContractVersion.empty()) {
            return state_->fail("RHI-BINDLESS-RECORD-AUTHORIZATION-INVALID");
        }
        if (!pipeline.layout()) {
            return state_->fail("compute pipeline has no pipeline layout");
        }
        pipelineLayout_ = pipeline.layout();
        shaderReflection_ = &pipeline.shaderReflection();
        boundDescriptors_.reset();
        pipelineBound_ = true;
        if (indexed) {
            state_->retainedPipelines.retain(indexed);
            indexed_ = std::move(indexed);
        }
        activePipeline_ = &pipeline;
        return state_->record();
    }

    CommandValidationResult bindDescriptorSet(std::uint32_t set,
        const DescriptorSet& descriptorSet,
        const std::uint32_t* dynamicOffsets,
        std::uint32_t dynamicOffsetCount) override
    {
        const auto validation = validatePassOpen(state_, ended_, ActivePassKind::Compute);
        if (!validation.success) {
            return validation;
        }
        if (indexed_) {
            boundSnapshot_.reset();
            return state_->fail("RHI-BINDLESS-RECORD-SNAPSHOT-REQUIRED");
        }
        const auto bindingValidation =
            validateDescriptorSetBinding(pipelineLayout_, set, descriptorSet);
        if (!bindingValidation.success) {
            return state_->fail(bindingValidation.diagnostics);
        }
        const auto trackingValidation =
            boundDescriptors_.bindSet(set, descriptorSet, dynamicOffsets, dynamicOffsetCount);
        if (!trackingValidation.success) {
            return state_->fail(trackingValidation.diagnostics);
        }
        return state_->record();
    }

    CommandValidationResult bindDescriptorTable(
        std::uint32_t set, const DescriptorTable& descriptorTable) override
    {
        const auto validation = validatePassOpen(state_, ended_, ActivePassKind::Compute);
        if (!validation.success) {
            return validation;
        }
        if (indexed_) {
            boundSnapshot_.reset();
            return state_->fail("RHI-BINDLESS-RECORD-SNAPSHOT-REQUIRED");
        }
        const auto bindingValidation =
            validateDescriptorTableBinding(pipelineLayout_, set, descriptorTable);
        if (!bindingValidation.success) {
            return state_->fail(bindingValidation.diagnostics);
        }
        const auto trackingValidation = boundDescriptors_.bindTable(set, descriptorTable);
        if (!trackingValidation.success) {
            return state_->fail(trackingValidation.diagnostics);
        }
        return state_->record();
    }

    CommandValidationResult pushConstants(ShaderStage stages,
        std::uint32_t offsetBytes,
        std::uint32_t sizeBytes,
        const void* data) override
    {
        const auto validation = validatePassOpen(state_, ended_, ActivePassKind::Compute);
        if (!validation.success) {
            return validation;
        }
        auto pcCmdResult =
            rhi::semantics::validatePushConstantCommand(stages, offsetBytes, sizeBytes, data);
        if (!pcCmdResult.success) {
            return state_->fail(pcCmdResult.diagnostics);
        }
        auto pcBindResult = rhi::semantics::validatePushConstantBinding(pipelineLayout_,
            ShaderStage::Compute,
            stages,
            offsetBytes,
            sizeBytes,
            shaderReflection_);
        if (!pcBindResult.success) {
            return state_->fail(pcBindResult.diagnostics);
        }
        if (pushState_.hasUnconsumedUpdate()) {
            return state_->fail(
                std::pmr::string(rhi::semantics::PushConstantDiagCode::kDuplicateUnconsumed)
                + "push constants already set without intervening draw/dispatch");
        }
        pushState_.onPush();
        return state_->record();
    }

    CommandValidationResult dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override
    {
        const auto validation = validatePassOpen(state_, ended_, ActivePassKind::Compute);
        if (!validation.success) {
            return validation;
        }
        if (!pipelineBound_) {
            return state_->fail("compute pipeline must be bound before dispatch");
        }
        const auto indexedUse = validateIndexedUse(state_, indexed_, boundSnapshot_);
        if (!indexedUse.success)
            return state_->fail(indexedUse.diagnostics);
        const auto descriptorValidation = boundDescriptors_.validateComplete(
            pipelineLayout_, shaderReflection_, ShaderStage::Compute);
        if (!descriptorValidation.success) {
            return state_->fail(descriptorValidation.diagnostics);
        }
        pushState_.onConsume();
        return state_->record();
    }

    CommandValidationResult dispatchIndirect(const GpuBuffer&, std::uint64_t) override
    {
        const auto pass = validatePassOpen(state_, ended_, ActivePassKind::Compute);
        if (!pass.success)
            return pass;
        if (!pipelineBound_)
            return state_->fail("RHI-BINDLESS-RECORD-PIPELINE-REQUIRED");
        if (indexed_)
            return state_->fail("RHI-BINDLESS-INDIRECT-CONTRACT-UNAVAILABLE");
        const auto descriptors = boundDescriptors_.validateLiveBindings();
        if (!descriptors.success)
            return state_->fail(descriptors.diagnostics);
        pushState_.onConsume();
        return state_->record();
    }

    CommandValidationResult writeTimestamp(const QueryPool&, std::uint32_t) override { return {}; }

    CommandValidationResult end() override
    {
        const auto validation = validatePassOpen(state_, ended_, ActivePassKind::Compute);
        if (!validation.success) {
            return validation;
        }
        ended_ = true;
        state_->activePass = ActivePassKind::None;
        pushState_.onPassEnd();
        boundDescriptors_.reset();
        boundSnapshot_.reset();
        indexed_.reset();
        return state_->record();
    }

private:
    RecorderStatePtr state_;
    std::shared_ptr<const BindlessPipelineState> indexed_;
    std::shared_ptr<const BindlessSnapshot> boundSnapshot_;
    const ComputePipeline* activePipeline_ = nullptr;
    const PipelineLayout* pipelineLayout_ = nullptr;
    const ShaderReflectionDesc* shaderReflection_ = nullptr;
    BoundDescriptorSlots boundDescriptors_;
    rhi::semantics::PushConstantConsumptionState pushState_;
    bool ended_ = false;
    bool pipelineBound_ = false;
};

class NullTransferPassEncoder final : public TransferPassEncoder {
public:
    explicit NullTransferPassEncoder(RecorderStatePtr state, ResourceRegistryPtr registry)
        : state_(std::move(state)), registry_(std::move(registry))
    {}

    CommandValidationResult copyBufferToBuffer(std::uint64_t sizeBytes) override
    {
        const auto validation = validatePassOpen(state_, ended_, ActivePassKind::Transfer);
        if (!validation.success) {
            return validation;
        }
        if (sizeBytes == 0) {
            return state_->succeed();
        }
        return state_->record();
    }

    CommandValidationResult copyBufferToBuffer(const BufferCopyDesc& desc) override
    {
        const auto validation = validateTransferOpen();
        if (!validation.success) {
            return validation;
        }
        if (!desc.sourceBuffer.isValid() || !desc.destinationBuffer.isValid()
            || desc.sizeBytes == 0) {
            return state_->fail("buffer-to-buffer copy requires valid buffers and size");
        }
        if (registry_ && registry_->bufferGpuTransferWritesSealed(desc.destinationBuffer))
            return state_->fail("RHI-BUFFER-GPU-WRITES-SEALED");
        return state_->record();
    }

    CommandValidationResult copyBufferToTexture(const BufferToTextureCopyDesc& desc) override
    {
        const auto validation = validateTransferOpen();
        if (!validation.success) {
            return validation;
        }
        if (!desc.mipLayouts.empty() || !desc.sourceBuffer.isValid()
            || !desc.destinationTexture.isValid() || !isValidRange(desc.textureSubresources)
            || !isValidExtent(desc.extent) || desc.bytesPerRow == 0 || desc.rowsPerImage == 0
            || (desc.bytesPerRow % 256u) != 0u) {
            return state_->fail(
                "buffer-to-texture copy requires valid resources, layout, and extent");
        }
        return state_->record();
    }

    CommandValidationResult copyTextureToBuffer(const TextureToBufferCopyDesc& desc) override
    {
        const auto validation = validateTransferOpen();
        if (!validation.success) {
            return validation;
        }
        if (!desc.mipLayouts.empty() || !desc.sourceTexture.isValid()
            || !desc.destinationBuffer.isValid() || !isValidRange(desc.textureSubresources)
            || !isValidExtent(desc.extent) || desc.bytesPerRow == 0 || desc.rowsPerImage == 0) {
            return state_->fail(
                "texture-to-buffer copy requires valid resources, layout, and extent");
        }
        if (registry_ && registry_->bufferGpuTransferWritesSealed(desc.destinationBuffer))
            return state_->fail("RHI-BUFFER-GPU-WRITES-SEALED");
        return state_->record();
    }

    CommandValidationResult copyTextureToTexture(const TextureToTextureCopyDesc& desc) override
    {
        const auto validation = validateTransferOpen();
        if (!validation.success) {
            return validation;
        }
        if (!desc.mipLayouts.empty() || !desc.sourceTexture.isValid()
            || !desc.destinationTexture.isValid() || !isValidRange(desc.sourceSubresources)
            || !isValidRange(desc.destinationSubresources) || !isValidExtent(desc.extent)) {
            return state_->fail(
                "texture-to-texture copy requires valid resources, subresources, and extent");
        }
        return state_->record();
    }

    CommandValidationResult uploadBuffer(const UploadBufferDesc& desc) override
    {
        const auto validation = validateTransferOpen();
        if (!validation.success) {
            return validation;
        }
        if (!desc.sourceUploadBuffer.isValid() || !desc.destinationResource.isValid()
            || desc.sizeBytes == 0 || desc.stagingIntent != StagingIntent::Upload
            || !desc.backendOwnedStaging) {
            return state_->fail("upload command requires backend-owned upload staging intent");
        }
        if (registry_ && registry_->bufferGpuTransferWritesSealed(desc.destinationResource))
            return state_->fail("RHI-BUFFER-GPU-WRITES-SEALED");
        return state_->record();
    }

    CommandValidationResult uploadTexture(const UploadTextureDesc& desc) override
    {
        const auto validation = validateTransferOpen();
        if (!validation.success) {
            return validation;
        }
        if (!desc.destinationTexture.isValid()) {
            return state_->fail("uploadTexture: destination texture id must be valid");
        }
        if (desc.extent.width == 0 || desc.extent.height == 0 || desc.extent.depth == 0) {
            return state_->fail("uploadTexture: extent must be non-zero");
        }
        if (desc.format == TextureFormat::Unknown) {
            return state_->fail("uploadTexture: format must be specified");
        }
        if (desc.bytesPerRow == 0 || desc.rowsPerImage == 0) {
            return state_->fail("uploadTexture: bytesPerRow and rowsPerImage must be non-zero");
        }
        if (desc.pixelData == nullptr || desc.pixelDataSize == 0) {
            return state_->fail("uploadTexture: pixelData must be non-null with non-zero size");
        }
        if (desc.stagingIntent != StagingIntent::Upload) {
            return state_->fail("uploadTexture: staging intent must be Upload");
        }
        if (!desc.backendOwnedStaging) {
            return state_->fail("uploadTexture: backend-owned staging is required");
        }
        if (!desc.mipLayouts.empty() || desc.subresources.mipLevelCount != 1u) {
            return state_->fail("RHI-NULL-TEXTURE-UPLOAD-PER-MIP-LAYOUT-NOT-MODELED");
        }
        if (isValidRange(desc.subresources) && desc.mipLevel > 0
            && desc.subresources.baseMipLevel != desc.mipLevel) {
            return state_->fail("uploadTexture: mipLevel must match subresources.baseMipLevel");
        }
        // destinationOffset is the L2 sub-rect origin (default (0,0,0) for
        // full-texture uploads). Null does not track destination texture
        // extent, so it cannot bounds-check offset+extent, but it accepts the
        // field so callers exercising the sub-rect upload contract (e.g.
        // dynamic glyph atlas updates) get a non-failing record path.
        return state_->record();
    }

    CommandValidationResult readbackTexture(const ReadbackTextureDesc& desc) override
    {
        const auto validation = validateTransferOpen();
        if (!validation.success) {
            return validation;
        }
        if (!desc.sourceResource.isValid() || !desc.destinationReadbackBuffer.isValid()
            || desc.sizeBytes == 0 || desc.stagingIntent != StagingIntent::Readback
            || !desc.backendOwnedStaging) {
            return state_->fail("readback command requires backend-owned readback staging intent");
        }
        if (!desc.mipLayouts.empty() || desc.extent.width != 0u || desc.extent.height != 0u
            || desc.extent.depth != 0u || desc.destinationOffsetBytes != 0u
            || desc.bytesPerRow != 0u || desc.rowsPerImage != 0u
            || desc.sourceSubresources.mipLevelCount != 0u
            || desc.sourceSubresources.arrayLayerCount != 0u) {
            return state_->fail("RHI-NULL-EXPLICIT-READBACK-LAYOUT-NOT-MODELED");
        }
        return state_->record();
    }

    CommandValidationResult end() override
    {
        const auto validation = validatePassOpen(state_, ended_, ActivePassKind::Transfer);
        if (!validation.success) {
            return validation;
        }
        ended_ = true;
        state_->activePass = ActivePassKind::None;
        return state_->record();
    }

private:
    CommandValidationResult validateTransferOpen()
    {
        return validatePassOpen(state_, ended_, ActivePassKind::Transfer);
    }

    RecorderStatePtr state_;
    ResourceRegistryPtr registry_;
    bool ended_ = false;
};

class NullRenderBundleEncoder final : public RenderBundleEncoder {
public:
    explicit NullRenderBundleEncoder(RenderBundleDesc desc) : desc_(std::move(desc)) {}

    CommandValidationResult bindTessellationFactorBuffer(const GpuBuffer& buffer,
        std::uint64_t offsetBytes,
        std::uint32_t instanceStrideBytes) override
    {
        if (finished_) {
            return {false, "render bundle encoder already finished", commandCount_};
        }
        if (desc_.primitiveTopology != PrimitiveTopology::PatchList
            || desc_.patchControlPointCount == 0u) {
            return {false,
                "RHI-NULL-RENDER-BUNDLE-TESSELLATION-FACTOR-BUFFER-REQUIRES-PATCHLIST",
                commandCount_};
        }
        if ((buffer.desc().usage & ResourceUsage::StorageBuffer) == ResourceUsage::None
            || instanceStrideBytes == 0u || (offsetBytes & 3u) != 0u
            || (instanceStrideBytes & 3u) != 0u || offsetBytes > buffer.desc().sizeBytes
            || buffer.desc().sizeBytes - offsetBytes < sizeof(std::uint32_t)) {
            return {false,
                "RHI-NULL-RENDER-BUNDLE-TESSELLATION-FACTOR-BUFFER-DESC-INVALID",
                commandCount_};
        }
        tessellationFactorBufferBound_ = true;
        return {true, "", commandCount_};
    }

    CommandValidationResult bindIndexBuffer(const GpuBuffer&, IndexFormat, std::uint64_t) override
    {
        if (finished_) {
            return {false, "render bundle encoder already finished", commandCount_};
        }
        return {true, "", commandCount_};
    }

    CommandValidationResult drawIndexed(std::uint32_t indexCount,
        std::uint32_t,
        std::uint32_t,
        std::int32_t,
        std::uint32_t) override
    {
        if (finished_) {
            return {false, "render bundle encoder already finished", commandCount_};
        }
        if (desc_.primitiveTopology == PrimitiveTopology::PatchList
            && (!tessellationFactorBufferBound_ || desc_.patchControlPointCount == 0u
                || (indexCount % desc_.patchControlPointCount) != 0u)) {
            return {false, "RHI-NULL-RENDER-BUNDLE-PATCH-DRAW-CONTRACT-INVALID", commandCount_};
        }
        ++commandCount_;
        return {true, "", commandCount_};
    }

    CommandValidationResult draw(
        std::uint32_t vertexCount, std::uint32_t, std::uint32_t firstVertex, std::uint32_t) override
    {
        if (finished_) {
            return {false, "render bundle encoder already finished", commandCount_};
        }
        if (desc_.primitiveTopology == PrimitiveTopology::PatchList
            && (!tessellationFactorBufferBound_ || desc_.patchControlPointCount == 0u
                || (vertexCount % desc_.patchControlPointCount) != 0u
                || (firstVertex % desc_.patchControlPointCount) != 0u)) {
            return {false, "RHI-NULL-RENDER-BUNDLE-PATCH-DRAW-CONTRACT-INVALID", commandCount_};
        }
        ++commandCount_;
        return {true, "", commandCount_};
    }

    arp::core::UniquePtr<RenderBundle> finish() override
    {
        if (finished_) {
            return nullptr;
        }
        finished_ = true;
        return arp::core::makeUnique<NullRenderBundle>(commandCount_,
            desc_.primitiveTopology == PrimitiveTopology::PatchList,
            desc_.patchControlPointCount,
            tessellationFactorBufferBound_);
    }

private:
    RenderBundleDesc desc_;
    std::uint32_t commandCount_ = 0;
    bool finished_ = false;
    bool tessellationFactorBufferBound_ = false;
};

class NullSecondaryCommandEncoder final : public SecondaryCommandEncoder {
public:
    explicit NullSecondaryCommandEncoder(SecondaryCommandEncoderDesc desc) : desc_(std::move(desc))
    {}

    CommandValidationResult pushConstants(
        ShaderStage, std::uint32_t, std::uint32_t, const void*) override
    {
        if (finished_) {
            return {false, "secondary command encoder already finished", commandCount_};
        }
        // A-04: secondary push recording not supported in this profile.
        return {false,
            rhi::semantics::PushConstantDiagCode::kSecondaryNotSupported
                + std::pmr::string("secondary push-constant recording is not supported"),
            commandCount_};
    }

    CommandValidationResult bindIndexBuffer(const GpuBuffer&, IndexFormat, std::uint64_t) override
    {
        return finished_ ? CommandValidationResult{false,
                               "secondary command encoder already finished",
                               commandCount_}
                         : CommandValidationResult{true, "", commandCount_};
    }

    CommandValidationResult bindTessellationFactorBuffer(const GpuBuffer& buffer,
        std::uint64_t offsetBytes,
        std::uint32_t instanceStrideBytes) override
    {
        if (finished_) {
            return {false, "secondary command encoder already finished", commandCount_};
        }
        if (desc_.primitiveTopology != PrimitiveTopology::PatchList
            || desc_.patchControlPointCount == 0u) {
            return {false,
                "RHI-NULL-SECONDARY-TESSELLATION-FACTOR-BUFFER-REQUIRES-PATCHLIST",
                commandCount_};
        }
        if ((buffer.desc().usage & ResourceUsage::StorageBuffer) == ResourceUsage::None
            || instanceStrideBytes == 0u || (offsetBytes & 3u) != 0u
            || (instanceStrideBytes & 3u) != 0u || offsetBytes > buffer.desc().sizeBytes
            || buffer.desc().sizeBytes - offsetBytes < sizeof(std::uint32_t)) {
            return {
                false, "RHI-NULL-SECONDARY-TESSELLATION-FACTOR-BUFFER-DESC-INVALID", commandCount_};
        }
        tessellationFactorBufferBound_ = true;
        return {true, "", commandCount_};
    }

    CommandValidationResult drawIndexed(std::uint32_t indexCount,
        std::uint32_t,
        std::uint32_t,
        std::int32_t,
        std::uint32_t) override
    {
        if (finished_) {
            return {false, "secondary command encoder already finished", commandCount_};
        }
        if (desc_.primitiveTopology == PrimitiveTopology::PatchList
            && (!tessellationFactorBufferBound_ || desc_.patchControlPointCount == 0u
                || (indexCount % desc_.patchControlPointCount) != 0u)) {
            return {false, "RHI-NULL-SECONDARY-PATCH-DRAW-CONTRACT-INVALID", commandCount_};
        }
        ++commandCount_;
        return {true, "", commandCount_};
    }

    CommandValidationResult draw(
        std::uint32_t vertexCount, std::uint32_t, std::uint32_t firstVertex, std::uint32_t) override
    {
        if (finished_) {
            return {false, "secondary command encoder already finished", commandCount_};
        }
        if (desc_.primitiveTopology == PrimitiveTopology::PatchList
            && (!tessellationFactorBufferBound_ || desc_.patchControlPointCount == 0u
                || (vertexCount % desc_.patchControlPointCount) != 0u
                || (firstVertex % desc_.patchControlPointCount) != 0u)) {
            return {false, "RHI-NULL-SECONDARY-PATCH-DRAW-CONTRACT-INVALID", commandCount_};
        }
        ++commandCount_;
        return {true, "", commandCount_};
    }

    arp::core::UniquePtr<SecondaryCommandBuffer> finish() override
    {
        if (finished_) {
            return nullptr;
        }
        finished_ = true;
        return arp::core::makeUnique<NullSecondaryCommandBuffer>(commandCount_,
            desc_.primitiveTopology == PrimitiveTopology::PatchList,
            desc_.patchControlPointCount,
            tessellationFactorBufferBound_);
    }

private:
    SecondaryCommandEncoderDesc desc_;
    std::uint32_t commandCount_ = 0;
    bool finished_ = false;
    bool tessellationFactorBufferBound_ = false;
};

class NullCommandEncoder final : public CommandEncoder {
public:
    CommandValidationResult prepareBindlessGraphicsPass(
        std::shared_ptr<const BindlessPreparedGraphicsBinding> binding,
        const BindlessAccessDeclarations& accesses) override
    {
        if (state_->finished || state_->hasActivePass() || state_->pendingGraphics)
            return state_->fail("RHI-BINDLESS-GRAPHICS-PASS-SCOPE-RECORDING-INVALID");
        if (!state_->issuer)
            return state_->fail("RHI-BINDLESS-GRAPHICS-PASS-SCOPE-UNAVAILABLE");
        if (!binding || !binding->sharedOwnerMatches(binding)
            || !isPreparedGraphicsBinding(*binding))
            return state_->fail("RHI-BINDLESS-PREPARED-GRAPHICS-PAYLOAD-MISMATCH");
        const auto& desc = binding->owners()->desc();
        auto valid = binding->validateForRecord(*desc.pipeline, accesses);
        if (!valid.success)
            return state_->fail(valid.diagnostics);
        const auto indexed = bindlessState(*desc.pipeline);
        if (!indexed || !indexed->authorization->issuedBy(state_->issuer, indexed->authorization))
            return state_->fail("RHI-BINDLESS-RECORD-AUTHORIZATION-INVALID");
        if (!state_->retainedPipelines.canRetain(indexed)
            || !state_->retainedSnapshots.canRetain(desc.snapshot)
            || !state_->retainedBindings.canRetain(binding))
            return state_->fail("RHI-BINDLESS-GRAPHICS-PASS-SCOPE-COMMAND-BUDGET");
        valid = state_->graphicsAccesses.capture(accesses);
        if (!valid.success)
            return state_->fail(valid.diagnostics);
        state_->retainedPipelines.retain(indexed);
        state_->retainedSnapshots.retain(desc.snapshot);
        state_->retainedBindings.retain(binding);
        state_->pendingGraphics = std::move(binding);
        return state_->record();
    }

    std::uint64_t recordingIdentity() const noexcept override { return state_->recordingIdentity; }

    explicit NullCommandEncoder(
        CommandEncoderDesc desc, const BindlessCrossExtension* issuer, ResourceRegistryPtr registry)
        : desc_(std::move(desc)), state_(arp::core::makeShared<RecorderState>(desc_, issuer)),
          registry_(std::move(registry))
    {}

    ResourceBarrierResult transition(
        GpuBuffer& buffer, ResourceState before, ResourceState after) override
    {
        if (state_->finished) {
            state_->fail("resource state transition rejected after command encoder finish");
            return {false, state_->lastValidation.diagnostics};
        }
        if (state_->hasActivePass()) {
            state_->fail("resource state transition rejected while active pass is open");
            return {false, state_->lastValidation.diagnostics};
        }

        auto result = transitionStatefulResource(
            *recordedStates_, dynamic_cast<StatefulResource*>(&buffer), before, after);
        if (result.success) {
            state_->record();
        } else {
            state_->fail(result.diagnostics);
        }
        return result;
    }

    ResourceBarrierResult transition(
        GpuTexture& texture, ResourceState before, ResourceState after) override
    {
        if (state_->finished) {
            state_->fail("resource state transition rejected after command encoder finish");
            return {false, state_->lastValidation.diagnostics};
        }
        if (state_->hasActivePass()) {
            state_->fail("resource state transition rejected while active pass is open");
            return {false, state_->lastValidation.diagnostics};
        }

        auto result = transitionStatefulResource(
            *recordedStates_, dynamic_cast<StatefulResource*>(&texture), before, after);
        if (result.success) {
            state_->record();
        } else {
            state_->fail(result.diagnostics);
        }
        return result;
    }

    ResourceBarrierResult transition(GpuTexture& texture,
        const TextureSubresourceRange& range,
        ResourceState before,
        ResourceState after) override
    {
        if (state_->finished) {
            state_->fail(
                "texture subresource state transition rejected after command encoder finish");
            return {false, state_->lastValidation.diagnostics};
        }
        if (state_->hasActivePass()) {
            state_->fail("texture subresource state transition rejected while active pass is open");
            return {false, state_->lastValidation.diagnostics};
        }

        auto result = transitionStatefulTextureSubresource(*recordedStates_,
            dynamic_cast<StatefulTextureResource*>(&texture),
            range,
            before,
            after);
        if (result.success) {
            state_->record();
        } else {
            state_->fail(result.diagnostics);
        }
        return result;
    }

    ResourceBarrierResult transition(GpuBuffer& buffer, const ResourceBarrierDesc& desc) override
    {
        if (const auto* id = rhi::semantics::ResourceAccessUsageValidation::validateGeometryBarrier(
                buffer.desc(), desc, desc.before, desc.after)) {
            state_->fail(id);
            return {false, state_->lastValidation.diagnostics};
        }
        if (state_->finished) {
            state_->fail("resource barrier rejected after command encoder finish");
            return {false, state_->lastValidation.diagnostics};
        }
        if (state_->hasActivePass()) {
            state_->fail("resource barrier rejected while active pass is open");
            return {false, state_->lastValidation.diagnostics};
        }

        const auto barrierValidation = validateBarrierDesc(desc, buffer.id(), false);
        if (!barrierValidation.success) {
            state_->fail(barrierValidation.diagnostics);
            return {false, barrierValidation.diagnostics, buffer.id(), desc.before, desc.after};
        }

        auto result = transitionStatefulResource(
            *recordedStates_, dynamic_cast<StatefulResource*>(&buffer), desc.before, desc.after);
        result = applyBarrierMetadata(result, desc, false);
        if (result.success) {
            state_->record();
        } else {
            state_->fail(result.diagnostics);
        }
        return result;
    }

    ResourceBarrierResult transition(GpuTexture& texture, const ResourceBarrierDesc& desc) override
    {
        if (const auto* id = rhi::semantics::ResourceAccessUsageValidation::validateGeometryBarrier(
                texture.desc(), desc, desc.before, desc.after)) {
            state_->fail(id);
            return {false, state_->lastValidation.diagnostics};
        }
        if (state_->finished) {
            state_->fail("texture resource barrier rejected after command encoder finish");
            return {false, state_->lastValidation.diagnostics};
        }
        if (state_->hasActivePass()) {
            state_->fail("texture resource barrier rejected while active pass is open");
            return {false, state_->lastValidation.diagnostics};
        }

        const auto barrierValidation = validateBarrierDesc(desc, texture.id(), true);
        if (!barrierValidation.success) {
            state_->fail(barrierValidation.diagnostics);
            return {false, barrierValidation.diagnostics, texture.id(), desc.before, desc.after};
        }

        auto result = transitionStatefulTextureSubresource(*recordedStates_,
            dynamic_cast<StatefulTextureResource*>(&texture),
            desc.textureSubresources,
            desc.before,
            desc.after);
        result = applyBarrierMetadata(result, desc, true);
        if (result.success) {
            state_->record();
        } else {
            state_->fail(result.diagnostics);
        }
        return result;
    }

    ResourceBarrierResult memoryBarrier(
        GpuBuffer& buffer, const ResourceMemoryBarrierDesc& desc) override
    {
        if (const auto* id = rhi::semantics::ResourceAccessUsageValidation::validateGeometryBarrier(
                buffer.desc(), desc, buffer.state(), buffer.state())) {
            state_->fail(id);
            return {false, state_->lastValidation.diagnostics};
        }
        if (state_->finished) {
            state_->fail("RHI-RESOURCE-MEMORY-BARRIER-RECORDING-FINISHED");
            return {false, state_->lastValidation.diagnostics};
        }
        if (state_->hasActivePass()) {
            state_->fail("RHI-RESOURCE-MEMORY-BARRIER-ACTIVE-PASS");
            return {false, state_->lastValidation.diagnostics};
        }
        if (const auto* diagnostics =
                rhi::semantics::validateResourceMemoryBarrierDesc(desc, buffer.id())) {
            state_->fail(diagnostics);
            return {false, diagnostics, buffer.id(), buffer.state(), buffer.state()};
        }
        if (dynamic_cast<StatefulResource*>(&buffer) == nullptr) {
            state_->fail("RHI-RESOURCE-MEMORY-BARRIER-BACKEND-RESOURCE-MISSING");
            return {false,
                state_->lastValidation.diagnostics,
                buffer.id(),
                buffer.state(),
                buffer.state()};
        }
        ResourceBarrierResult result;
        result.success = true;
        result.resource = buffer.id();
        result.before = buffer.state();
        result.after = buffer.state();
        result.stageAccessAware = true;
        result.sourceStages = desc.sourceStages;
        result.destinationStages = desc.destinationStages;
        result.sourceAccess = desc.sourceAccess;
        result.destinationAccess = desc.destinationAccess;
        state_->record();
        return result;
    }

    ResourceBarrierResult memoryBarrier(
        GpuTexture& texture, const ResourceMemoryBarrierDesc& desc) override
    {
        if (const auto* id = rhi::semantics::ResourceAccessUsageValidation::validateGeometryBarrier(
                texture.desc(), desc, texture.state(), texture.state())) {
            state_->fail(id);
            return {false, state_->lastValidation.diagnostics};
        }
        if (state_->finished) {
            state_->fail("RHI-RESOURCE-MEMORY-BARRIER-RECORDING-FINISHED");
            return {false, state_->lastValidation.diagnostics};
        }
        if (state_->hasActivePass()) {
            state_->fail("RHI-RESOURCE-MEMORY-BARRIER-ACTIVE-PASS");
            return {false, state_->lastValidation.diagnostics};
        }
        if (const auto* diagnostics =
                rhi::semantics::validateResourceMemoryBarrierDesc(desc, texture.id())) {
            state_->fail(diagnostics);
            return {false, diagnostics, texture.id(), texture.state(), texture.state()};
        }
        if (!registry_ || !registry_->ownsResource(texture.id(), RegisteredResourceKind::Texture)) {
            state_->fail("RHI-RESOURCE-MEMORY-BARRIER-FOREIGN-RESOURCE");
            return {false, state_->lastValidation.diagnostics};
        }
        if (dynamic_cast<StatefulTextureResource*>(&texture) == nullptr) {
            state_->fail("RHI-RESOURCE-MEMORY-BARRIER-BACKEND-RESOURCE-MISSING");
            return {false,
                state_->lastValidation.diagnostics,
                texture.id(),
                texture.state(),
                texture.state()};
        }
        ResourceBarrierResult result;
        result.success = true;
        result.resource = texture.id();
        result.before = texture.state();
        result.after = texture.state();
        result.stageAccessAware = true;
        result.sourceStages = desc.sourceStages;
        result.destinationStages = desc.destinationStages;
        result.sourceAccess = desc.sourceAccess;
        result.destinationAccess = desc.destinationAccess;
        state_->record();
        return result;
    }

    arp::core::UniquePtr<RenderPassEncoder> beginRenderPass(const RenderPassDesc& desc) override
    {
        const auto renderPassValidation = validateRenderPassDesc(desc);
        if (!renderPassValidation.success) {
            state_->fail(renderPassValidation.diagnostics);
            return nullptr;
        }
        if (state_->pendingGraphics) {
            const auto valid = state_->pendingGraphics->validateOwners();
            if (!valid.success) {
                state_->fail(valid.diagnostics);
                return nullptr;
            }
            const auto indexed = bindlessState(*state_->pendingGraphics->owners()->desc().pipeline);
            const auto compatible = indexed
                ? validateBindlessRenderPass(indexed->renderState, desc)
                : ValidationResult{false, "RHI-BINDLESS-PREPARED-GRAPHICS-PAYLOAD-MISMATCH"};
            if (!compatible.success) {
                state_->fail(compatible.diagnostics);
                return nullptr;
            }
        }
        if (!beginPass(ActivePassKind::Render, "render")) {
            return nullptr;
        }
        const auto subpassCount =
            desc.subpasses.empty() ? 1u : static_cast<std::uint32_t>(desc.subpasses.size());
        return arp::core::makeUnique<NullRenderPassEncoder>(
            state_, subpassCount, desc, std::exchange(state_->pendingGraphics, {}));
    }

    arp::core::UniquePtr<ComputePassEncoder> beginComputePass(const ComputePassDesc&) override
    {
        if (!beginPass(ActivePassKind::Compute, "compute")) {
            return nullptr;
        }
        return arp::core::makeUnique<NullComputePassEncoder>(state_);
    }

    arp::core::UniquePtr<TransferPassEncoder> beginTransferPass(const TransferPassDesc&) override
    {
        if (!beginPass(ActivePassKind::Transfer, "transfer")) {
            return nullptr;
        }
        return arp::core::makeUnique<NullTransferPassEncoder>(state_, registry_);
    }

    arp::core::UniquePtr<CommandBuffer> finish() override
    {
        if (state_->finished) {
            state_->fail("command encoder already finished");
            return nullptr;
        }
        if (state_->hasActivePass()) {
            state_->fail("command encoder finish rejected while active pass is open");
            return nullptr;
        }
        if (state_->pendingGraphics) {
            state_->fail("RHI-BINDLESS-GRAPHICS-PASS-SCOPE-NOT-CONSUMED");
            return nullptr;
        }
        state_->finished = true;
        state_->succeed();
        return arp::core::makeUnique<NullCommandBuffer>(state_->commandCount,
            std::move(recordedStates_),
            std::move(state_->retainedPipelines),
            std::move(state_->retainedSnapshots),
            std::move(state_->retainedBindings));
    }

    void pushDebugGroup(std::string_view) override {}

    void popDebugGroup() override {}

    void insertDebugMarker(std::string_view) override {}

    CommandValidationResult lastValidation() const override { return state_->lastValidation; }

private:
    bool beginPass(ActivePassKind passKind, const char* passName)
    {
        if (state_->finished) {
            state_->fail("command pass begin rejected after command encoder finish");
            return false;
        }
        if (state_->hasActivePass()) {
            state_->fail(std::pmr::string(passName)
                + " pass begin rejected while another active pass is open");
            return false;
        }
        if (state_->pendingGraphics && passKind != ActivePassKind::Render) {
            state_->fail("RHI-BINDLESS-GRAPHICS-PASS-SCOPE-NOT-CONSUMED");
            return false;
        }
        state_->activePass = passKind;
        state_->record();
        return true;
    }

    CommandEncoderDesc desc_;
    RecorderStatePtr state_;
    ResourceRegistryPtr registry_;
    core::UniquePtr<core::MutationJournal> recordedStates_ =
        core::makeUnique<core::MutationJournal>();
};

} // namespace

arp::core::UniquePtr<CommandEncoder> createCommandEncoder(const CommandEncoderDesc& desc,
    const BindlessCrossExtension* issuer,
    ResourceRegistryPtr registry)
{
    if (!semantics::commandRetentionRequestAllowed(desc, CommandRetentionCapabilities{}))
        return {};
    return arp::core::makeUnique<NullCommandEncoder>(desc, issuer, std::move(registry));
}

ValidationResult validateBindlessSubmission(
    const CommandBuffer& commands, const BindlessCrossExtension* issuer)
{
    const auto* buffer = dynamic_cast<const NullCommandBuffer*>(&commands);
    if (!buffer)
        return {false, "RHI-NULL-COMMAND-STATE-OWNER-MISSING"};
    return buffer->validateBindless(issuer);
}

arp::core::UniquePtr<SecondaryCommandEncoder> createSecondaryCommandEncoder(
    const SecondaryCommandEncoderDesc& desc)
{
    return arp::core::makeUnique<NullSecondaryCommandEncoder>(desc);
}

arp::core::UniquePtr<RenderBundleEncoder> createRenderBundleEncoder(const RenderBundleDesc& desc)
{
    return arp::core::makeUnique<NullRenderBundleEncoder>(desc);
}

} // namespace arp::rhi::null_backend

// endregion Null commands
