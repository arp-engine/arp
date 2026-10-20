#include "renderer/render_graph/render_graph.h"

#include "core/cpu_allocator.h"
#include "core/diagnostic_error.h"
#include "core/unsigned_range.h"
#include "renderer/render_graph/compiled_frame_graph.h"
#include "renderer/render_graph/frame_graph_compiler.h"
#include "renderer/render_graph/frame_graph_executor.h"
#include "renderer/render_graph/transient_resource_allocator.h"
#include "rhi/frontend/resource_access_usage.h"
#include "rhi/frontend/resource_cache_accounting.h"
#include "rhi/gpu_capabilities.h"
#include "rhi/gpu_extensions.h"
#include "rhi/semantics/gpu_timing_lowering.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace arp::render_graph {

namespace {

std::pmr::string resourceStateName(rhi::ResourceState state)
{
    switch (state) {
    case rhi::ResourceState::Undefined:
        return "Undefined";
    case rhi::ResourceState::CopySrc:
        return "CopySrc";
    case rhi::ResourceState::CopyDst:
        return "CopyDst";
    case rhi::ResourceState::ColorAttachment:
        return "ColorAttachment";
    case rhi::ResourceState::DepthAttachment:
        return "DepthAttachment";
    case rhi::ResourceState::ShaderRead:
        return "ShaderRead";
    case rhi::ResourceState::ShaderWrite:
        return "ShaderWrite";
    case rhi::ResourceState::Present:
        return "Present";
    }

    return "Unknown";
}

rhi::ResourceAccess accessFor(rhi::ResourceState state)
{
    switch (state) {
    case rhi::ResourceState::CopySrc:
        return rhi::ResourceAccess::TransferRead;
    case rhi::ResourceState::CopyDst:
        return rhi::ResourceAccess::TransferWrite;
    case rhi::ResourceState::ColorAttachment:
        return rhi::ResourceAccess::ColorAttachmentRead | rhi::ResourceAccess::ColorAttachmentWrite;
    case rhi::ResourceState::DepthAttachment:
        return rhi::ResourceAccess::DepthStencilRead | rhi::ResourceAccess::DepthStencilWrite;
    case rhi::ResourceState::ShaderRead:
        return rhi::ResourceAccess::ShaderRead;
    case rhi::ResourceState::ShaderWrite:
        return rhi::ResourceAccess::ShaderRead | rhi::ResourceAccess::ShaderWrite;
    case rhi::ResourceState::Present:
        return rhi::ResourceAccess::PresentRead;
    case rhi::ResourceState::Undefined:
        return rhi::ResourceAccess::None;
    }

    return rhi::ResourceAccess::None;
}

rhi::PipelineStage stageFor(rhi::ResourceState state)
{
    switch (state) {
    case rhi::ResourceState::CopySrc:
    case rhi::ResourceState::CopyDst:
        return rhi::PipelineStage::Transfer;
    case rhi::ResourceState::ColorAttachment:
        return rhi::PipelineStage::ColorAttachmentOutput;
    case rhi::ResourceState::DepthAttachment:
        return rhi::PipelineStage::DepthStencil;
    case rhi::ResourceState::ShaderRead:
    case rhi::ResourceState::ShaderWrite:
        return rhi::PipelineStage::VertexShader | rhi::PipelineStage::FragmentShader
            | rhi::PipelineStage::ComputeShader;
    case rhi::ResourceState::Present:
        return rhi::PipelineStage::Present;
    case rhi::ResourceState::Undefined:
        return rhi::PipelineStage::TopOfPipe;
    }

    return rhi::PipelineStage::TopOfPipe;
}

rhi::TextureAspect aspectFor(const rhi::GpuTexture& texture)
{
    return rhi::defaultTextureSubresourceRange(texture.desc()).aspect;
}

BarrierTraceRecord makeTextureBarrierTrace(const std::pmr::string& passName,
    const rhi::TextureDesc& desc,
    TextureHandle handle,
    rhi::ResourceState before,
    rhi::ResourceState after)
{
    const auto aspect = rhi::isDepthTextureFormat(desc.format) ? rhi::TextureAspect::Depth
                                                               : rhi::TextureAspect::Color;
    return BarrierTraceRecord{
        passName,
        desc.label,
        ResourceKind::Texture,
        before,
        after,
        stageFor(before),
        stageFor(after),
        accessFor(before),
        accessFor(after),
        resourceStateName(before),
        resourceStateName(after),
        aspect,
        handle.id,
    };
}

BarrierTraceRecord makeBufferBarrierTrace(const std::pmr::string& passName,
    const rhi::GpuBuffer& buffer,
    BufferHandle handle,
    rhi::ResourceState before,
    rhi::ResourceState after)
{
    return BarrierTraceRecord{
        passName,
        buffer.desc().label,
        ResourceKind::Buffer,
        before,
        after,
        stageFor(before),
        stageFor(after),
        accessFor(before),
        accessFor(after),
        "",
        "",
        rhi::TextureAspect::Color,
        handle.id,
    };
}

} // namespace

RenderContext::RenderContext(rhi::GpuDevice& device, rhi::CommandEncoder& commands)
    : device_(device), commands_(commands)
{}

rhi::GpuDevice& RenderContext::device()
{
    return device_;
}

rhi::CommandEncoder& RenderContext::commands()
{
    return commands_;
}

namespace {
class GraphAccessDeclarations final : public rhi::BindlessAccessDeclarations {
public:
    GraphAccessDeclarations(FrameGraph* graph, const core::pmr::Vector<CompiledResourceUse>* uses)
        : graph_(graph), uses_(uses)
    {}

    bool valid() const noexcept override { return graph_ && uses_; }

    std::size_t size() const noexcept override { return uses_ ? uses_->size() : 0; }

    bool read(std::size_t index, rhi::BindlessResourceAccess& out) const override
    {
        if (!valid() || index >= uses_->size())
            return false;
        const auto& use = (*uses_)[index];
        out = {};
        out.state = use.state;
        out.access = use.resourceAccess;
        out.pipelineStages = use.stages;
        using P = rhi::PipelineStage;
        using S = rhi::ShaderStage;
        if ((use.stages & P::VertexShader) != P::None)
            out.stages = out.stages | S::Vertex;
        if ((use.stages & P::FragmentShader) != P::None)
            out.stages = out.stages | S::Fragment;
        if ((use.stages & P::ComputeShader) != P::None)
            out.stages = out.stages | S::Compute;
        if (use.kind == ResourceKind::Buffer) {
            out.resource = graph_->buffer({use.handleId}).id();
            out.kind = rhi::BindlessAccessKind::Buffer;
            out.bufferOffset = use.bufferOffset;
            out.bufferSize = use.bufferSize;
        } else if (use.kind == ResourceKind::Texture) {
            out.resource = graph_->texture({use.handleId}).id();
            out.kind = rhi::BindlessAccessKind::Texture;
            out.textureRange = use.textureRange;
        } else
            return false;
        return true;
    }

private:
    FrameGraph* graph_;
    const core::pmr::Vector<CompiledResourceUse>* uses_;
};
} // namespace

rhi::CommandValidationResult RenderContext::bindSnapshot(rhi::BindlessPassExtension& pass,
    std::shared_ptr<const rhi::BindlessSnapshot> snapshot,
    const rhi::BindlessLoweringPlan& plan,
    std::uint64_t generation) const
{
    if (graph_
        && (!commands_.recordingIdentity()
            || pass.recordingIdentity() != commands_.recordingIdentity())) {
        (void)pass.bindSnapshot(
            snapshot, plan, generation, GraphAccessDeclarations(nullptr, nullptr));
        return {false, "RG-BINDLESS-RECORDING-SCOPE-MISMATCH", 0};
    }
    return pass.bindSnapshot(
        std::move(snapshot), plan, generation, GraphAccessDeclarations(graph_, passUses_));
}

const rhi::QueryPool* RenderContext::gpuTimingPool() const
{
    return gpuTimingPool_;
}

std::uint32_t RenderContext::gpuTimingBeginQuery() const
{
    return gpuTimingBeginQuery_;
}

std::uint32_t RenderContext::gpuTimingEndQuery() const
{
    return gpuTimingEndQuery_;
}

void RenderContext::setGpuTimingScope(
    const rhi::QueryPool* pool, const std::uint32_t beginQuery, const std::uint32_t endQuery)
{
    gpuTimingPool_ = pool;
    gpuTimingBeginQuery_ = beginQuery;
    gpuTimingEndQuery_ = endQuery;
}

RenderPassContext::RenderPassContext(RenderContext& context,
    rhi::RenderPassEncoder& pass,
    const std::pmr::vector<SubpassInputDescriptorRoute>* subpassInputBindings)
    : context_(context), pass_(pass), subpassInputBindings_(subpassInputBindings)
{}

rhi::GpuDevice& RenderPassContext::device()
{
    return context_.device();
}

rhi::CommandEncoder& RenderPassContext::commands()
{
    return context_.commands();
}

rhi::RenderPassEncoder& RenderPassContext::renderPass()
{
    return pass_;
}

rhi::CommandValidationResult RenderPassContext::bindSnapshot(
    std::shared_ptr<const rhi::BindlessSnapshot> snapshot,
    const rhi::BindlessLoweringPlan& plan,
    std::uint64_t generation) const
{
    auto* indexed = static_cast<rhi::BindlessPassExtension*>(
        pass_.queryInterface(rhi::kExtensionBindlessCross));
    if (!indexed)
        return {false, "RHI-BINDLESS-EXTENSION-UNAVAILABLE", 0};
    return context_.bindSnapshot(*indexed, std::move(snapshot), plan, generation);
}

rhi::ValidationResult RenderPassContext::validateSubpassShaderPackage(
    const rhi::ShaderPackageDesc& shaderPackage) const
{
    const auto packageValidation = rhi::validateShaderPackage(shaderPackage);
    if (!packageValidation.success) {
        return packageValidation;
    }
    if (!subpassInputBindings_) {
        return {true, {}};
    }
    for (const auto& binding : *subpassInputBindings_) {
        const auto validation = rhi::validateSubpassInputBindingAgainstReflection(
            shaderPackage.reflection, binding.shaderBinding);
        if (!validation.success) {
            return validation;
        }
    }
    return {true, {}};
}

rhi::ValidationResult RenderPassContext::writeSubpassInputDescriptors(
    const rhi::ShaderPackageDesc& shaderPackage,
    rhi::DescriptorSet* const* descriptorSets,
    std::uint32_t descriptorSetCount) const
{
    const auto validation = validateSubpassShaderPackage(shaderPackage);
    if (!validation.success) {
        return validation;
    }
    if (!subpassInputBindings_ || subpassInputBindings_->empty()) {
        return {true, {}};
    }
    if (!descriptorSets) {
        return {false, "subpass descriptor consumer requires descriptor-set storage"};
    }

    for (const auto& route : *subpassInputBindings_) {
        if (!route.sourceTexture.isValid() || !route.textureView.isValid()) {
            return {false,
                "subpass descriptor route is missing source or materialized texture identity"};
        }
        if (route.shaderBinding.set >= descriptorSetCount
            || !descriptorSets[route.shaderBinding.set]) {
            return {
                false, "subpass descriptor route targets an unavailable logical descriptor set"};
        }

        rhi::DescriptorBindingWrite write;
        write.binding = route.shaderBinding.binding;
        write.type = rhi::DescriptorType::InputAttachment;
        write.resource = route.textureView;
        write.resourceKind = rhi::DescriptorResourceKind::TextureView;
        if (route.shaderBinding.physicalBinding
            == rhi::semantics::SubpassInputPhysicalBinding::SampledTexture) {
            write.physicalLowering.enabled = true;
            write.physicalLowering.physicalType = rhi::DescriptorType::SampledTexture;
            write.physicalLowering.logicalAttachmentIndex =
                route.shaderBinding.logicalAttachmentIndex;
            write.physicalLowering.shaderPackageHash = rhi::stableShaderPackageHash(shaderPackage);
        } else if (route.shaderBinding.physicalBinding
            != rhi::semantics::SubpassInputPhysicalBinding::NativeInputAttachment) {
            return {false, "subpass descriptor route has no executable physical binding"};
        }

        const auto loweringValidation = rhi::validateDescriptorWriteLowering(write);
        if (!loweringValidation.success) {
            return loweringValidation;
        }
        const auto written = descriptorSets[route.shaderBinding.set]->write(write);
        if (!written.success) {
            return written;
        }
    }
    return {true, {}};
}

FrameGraph::FrameGraph(rhi::GpuDevice& device, std::shared_ptr<FrameGraphResourcePool> resourcePool)
    : device_(device),
      resourcePool_(resourcePool ? std::move(resourcePool)
                                 : arp::core::makeShared<FrameGraphResourcePool>(device)),
      graphPoolLease_(resourcePool_->retainGraphOwner(resourcePool_, device)),
      timestampQueriesSupported_(device.capabilities().features.timestampQueries)
{
    if (!graphPoolLease_.charged())
        throw core::DiagnosticError("RG-POOL-GRAPH-OWNER-CAPACITY");
}

FrameGraph::~FrameGraph()
{
    reset();
}

void CompiledFrameGraph::addPass(CompiledPass pass)
{
    invalidateRetirementSource();
    passes_.push_back(std::move(pass));
    passResourceUses_.emplace_back();
}

void CompiledFrameGraph::addExecutablePass(size_t compiledPassIndex)
{
    invalidateRetirementSource();
    executionOrder_.push_back(compiledPassIndex);
}

void CompiledFrameGraph::addResourceLifetime(ResourceLifetimeRecord lifetime)
{
    invalidateRetirementSource();
    resourceLifetimes_.push_back(std::move(lifetime));
}

void CompiledFrameGraph::addBarrier(BarrierTraceRecord barrier)
{
    invalidateRetirementSource();
    barrierPlan_.push_back(std::move(barrier));
}

void CompiledFrameGraph::addAlias(AliasTraceRecord alias)
{
    invalidateRetirementSource();
    aliasPlan_.push_back(std::move(alias));
}

void CompiledFrameGraph::addLoadStore(AttachmentTraceRecord attachment)
{
    invalidateRetirementSource();
    loadStorePlan_.push_back(std::move(attachment));
}

void CompiledFrameGraph::addQueuePlan(QueuePlanRecord queuePlan)
{
    invalidateRetirementSource();
    queuePlan_.push_back(std::move(queuePlan));
}

void CompiledFrameGraph::addExternalResource(ExternalResourceRecord resource)
{
    invalidateRetirementSource();
    externalResources_.push_back(std::move(resource));
}

void CompiledFrameGraph::addPassBarrierBatch(PassBarrierBatch batch)
{
    invalidateRetirementSource();
    passBarrierBatches_.push_back(std::move(batch));
}

void CompiledFrameGraph::addPassResourceUse(size_t passIndex, CompiledResourceUse use)
{
    invalidateRetirementSource();
    if (passIndex >= passResourceUses_.size()) {
        throw std::out_of_range("RG-COMPILED-PASS-USE-INDEX-INVALID");
    }
    passResourceUses_[passIndex].push_back(std::move(use));
}

void CompiledFrameGraph::addDependency(CompiledDependencyRecord dependency)
{
    invalidateRetirementSource();
    for (const auto& existing : dependencies_) {
        if (existing.fromPass == dependency.fromPass && existing.toPass == dependency.toPass
            && existing.resourceName == dependency.resourceName
            && existing.dependency == dependency.dependency) {
            return;
        }
    }
    dependencies_.push_back(std::move(dependency));
}

void CompiledFrameGraph::addRenderTargetFlow(CompiledRenderTargetFlow flow)
{
    invalidateRetirementSource();
    renderTargetFlows_.push_back(std::move(flow));
}

void CompiledFrameGraph::addSubpassTopology(CompiledSubpassTopology topology)
{
    invalidateRetirementSource();
    subpassTopologies_.push_back(std::move(topology));
}

void CompiledFrameGraph::addAliasHandoff(AliasHandoffRecord handoff)
{
    invalidateRetirementSource();
    aliasHandoffs_.push_back(std::move(handoff));
}

void CompiledFrameGraph::setTransientResourceBudget(
    std::uint64_t peakBytes, std::uint32_t resourceCount, std::uint64_t physicalPeakBytes)
{
    invalidateRetirementSource();
    transientPeakBytes_ = peakBytes;
    transientPhysicalPeakBytes_ = physicalPeakBytes;
    transientResourceCount_ = resourceCount;
}

size_t CompiledFrameGraph::passCount() const
{
    return passes_.size();
}

size_t CompiledFrameGraph::executablePassCount() const
{
    return executionOrder_.size();
}

const std::pmr::vector<CompiledPass>& CompiledFrameGraph::passes() const
{
    return passes_;
}

const std::pmr::vector<size_t>& CompiledFrameGraph::executionOrder() const
{
    return executionOrder_;
}

const std::pmr::vector<ResourceLifetimeRecord>& CompiledFrameGraph::resourceLifetimes() const
{
    return resourceLifetimes_;
}

const std::pmr::vector<BarrierTraceRecord>& CompiledFrameGraph::barrierPlan() const
{
    return barrierPlan_;
}

const std::pmr::vector<AliasTraceRecord>& CompiledFrameGraph::aliasPlan() const
{
    return aliasPlan_;
}

const std::pmr::vector<AttachmentTraceRecord>& CompiledFrameGraph::loadStorePlan() const
{
    return loadStorePlan_;
}

const std::pmr::vector<QueuePlanRecord>& CompiledFrameGraph::queuePlan() const
{
    return queuePlan_;
}

const std::pmr::vector<ExternalResourceRecord>& CompiledFrameGraph::externalResources() const
{
    return externalResources_;
}

const std::pmr::vector<PassBarrierBatch>& CompiledFrameGraph::passBarrierBatches() const
{
    return passBarrierBatches_;
}

const std::pmr::vector<std::pmr::vector<CompiledResourceUse>>&
CompiledFrameGraph::passResourceUses() const
{
    return passResourceUses_;
}

const std::pmr::vector<CompiledDependencyRecord>& CompiledFrameGraph::dependencies() const
{
    return dependencies_;
}

const std::pmr::vector<CompiledRenderTargetFlow>& CompiledFrameGraph::renderTargetFlows() const
{
    return renderTargetFlows_;
}

const std::pmr::vector<CompiledSubpassTopology>& CompiledFrameGraph::subpassTopologies() const
{
    return subpassTopologies_;
}

const std::pmr::vector<AliasHandoffRecord>& CompiledFrameGraph::aliasHandoffs() const
{
    return aliasHandoffs_;
}

TextureHandle FrameGraph::createTexture(const rhi::TextureDesc& desc)
{
    const auto handle = allocateTextureHandle();
    // The graph compiler deliberately starts every graph-owned image at
    // Undefined because a newly created Vulkan image has no valid layout.
    // Keep the graph description's initialState as the scheduling contract,
    // but create the backend object with a real Undefined state so the first
    // compiled barrier can be recorded instead of failing the RHI logical
    // state check (notably for D32 depth targets declared as
    // DepthAttachment).
    auto backendDesc = desc;
    backendDesc.initialState = rhi::ResourceState::Undefined;
    // Ordinary graph-owned resources have the same submission lifetime as
    // authored transient resources. Acquire them from the graph pool as well
    // so allocation and completion-gated release remain one ownership path.
    textures_.push_back(TextureRecord{
        handle, desc, resourcePool_->acquireTexture(backendDesc), nullptr, false, false, false});
    if (!textures_.back().texture) {
        textures_.pop_back();
        throw std::runtime_error(
            std::string("frame graph texture creation failed: ") + desc.label.c_str());
    }
    return handle;
}

TextureHandle FrameGraph::createTransientTexture(const rhi::TextureDesc& desc)
{
    if (resourcePool_->ownsGraphResources() && !transientResourceOwnersEnabled())
        throw core::DiagnosticError("RG-OWNED-RESOURCES-MATERIALIZATION-UNAVAILABLE");
    const auto handle = allocateTextureHandle();
    auto resolved = desc;
    if (resolved.memory != rhi::MemoryUsage::Transient) {
        resolved.memory = rhi::MemoryUsage::Transient;
    }
    // Deferred allocation: store desc + handle, do not create the GPU
    // texture yet. Explicit ownedResources/3 materializes at publication;
    // other domains acquire at execute() (see allocateTransientTextures).
    // This allows the
    // compiler to work with desc-only records and enables cross-frame
    // pool reuse.
    textures_.push_back(TextureRecord{handle, resolved, nullptr, nullptr, true, false, false});
    return handle;
}

TextureHandle FrameGraph::importTexture(arp::core::UniquePtr<rhi::GpuTexture> texture)
{
    rejectUnjoinedGraphImport();
    if (!texture) {
        throw std::runtime_error("cannot import null frame graph texture");
    }

    const auto handle = allocateTextureHandle();
    const auto desc = texture->desc();
    textures_.push_back(
        TextureRecord{handle, desc, std::move(texture), nullptr, false, true, false});
    return handle;
}

TextureHandle FrameGraph::importSwapchainTexture(
    arp::core::UniquePtr<rhi::GpuTexture> texture, const rhi::SwapchainImageRef& image)
{
    rejectUnjoinedGraphImport();
    if (!texture) {
        throw std::runtime_error("RG-IMPORT-SWAPCHAIN-NULL-TEXTURE");
    }

    const auto& desc = texture->desc();
    const bool contractMatches = image.image.isValid() && image.image.value == texture->id().value
        && image.frameSerial != 0 && image.extent.width != 0 && image.extent.height != 0
        && image.extent.width == desc.extent.width && image.extent.height == desc.extent.height
        && desc.extent.depth == 1 && image.format != rhi::TextureFormat::Unknown
        && image.format == desc.format && image.state == rhi::ResourceState::Present;
    if (!contractMatches) {
        throw std::runtime_error("RG-IMPORT-SWAPCHAIN-CONTRACT-MISMATCH");
    }

    const auto handle = allocateTextureHandle();
    TextureRecord record;
    record.handle = handle;
    record.desc = desc;
    record.texture = std::move(texture);
    record.imported = true;
    record.presentableExternal = true;
    record.externalFinalState = image.state;
    textures_.push_back(std::move(record));
    return handle;
}

TextureHandle FrameGraph::importTextureRef(std::shared_ptr<rhi::GpuTexture> texture)
{
    rejectUnjoinedGraphImport(true);
    if (!texture) {
        throw std::runtime_error("cannot import null frame graph texture reference");
    }

    for (const auto& record : textures_) {
        if (record.textureRef.get() == texture.get()) {
            return record.handle;
        }
    }

    const auto handle = allocateTextureHandle();
    const auto desc = texture->desc();
    TextureRecord record;
    record.handle = handle;
    record.desc = desc;
    record.canonicalSharedOwnerAtImport = texture->sharedOwnerMatches(texture);
    record.textureRef = std::move(texture);
    record.transient = false;
    record.imported = true;
    record.placed = false;
    textures_.push_back(std::move(record));
    return handle;
}

void FrameGraph::discardImportedContentsBeforeFirstWrite(TextureHandle handle)
{
    auto& record = textureRecord(handle);
    if (!record.imported || record.presentableExternal) {
        throw std::runtime_error("RG-DISCARD-IMPORTED-CONTENTS-REQUIRES-NON-PRESENTABLE-IMPORT");
    }
    invalidateCompiledRetirementSource();
    record.preserveInitialContents = false;
}

void FrameGraph::setExternalFinalState(TextureHandle handle, rhi::ResourceState state)
{
    auto& record = textureRecord(handle);
    if (!record.imported) {
        throw std::runtime_error("frame graph external final state requires an imported texture");
    }
    if (state == rhi::ResourceState::Present && !record.presentableExternal) {
        throw std::runtime_error("RG-EXTERNAL-PRESENT-NON-SWAPCHAIN");
    }
    invalidateCompiledRetirementSource();
    record.externalFinalState = state;
}

BufferHandle FrameGraph::createBuffer(const rhi::BufferDesc& desc)
{
    const auto handle = allocateBufferHandle();
    // Graph-owned buffers follow the same completion-gated pool ownership as
    // graph-owned textures.  Reset() returns every non-imported buffer to the
    // pool; allocating this path directly from the device made those entries
    // permanently cold and grew WebGPU memory once per frame.
    buffers_.push_back(BufferRecord{handle, resourcePool_->acquireBuffer(desc), false});
    if (!buffers_.back().buffer) {
        buffers_.pop_back();
        throw std::runtime_error("frame graph buffer creation failed");
    }
    return handle;
}

BufferHandle FrameGraph::importBufferRef(std::shared_ptr<rhi::GpuBuffer> buffer)
{
    rejectUnjoinedGraphImport(true);
    if (!buffer)
        throw std::runtime_error("RG-IMPORT-BUFFER-NULL");
    for (const auto& record : buffers_)
        if (record.bufferRef.get() == buffer.get())
            return record.handle;
    BufferRecord record;
    record.handle = allocateBufferHandle();
    record.canonicalSharedOwnerAtImport = buffer->sharedOwnerMatches(buffer);
    record.bufferRef = std::move(buffer);
    const auto handle = record.handle;
    buffers_.push_back(std::move(record));
    return handle;
}

BufferHandle FrameGraph::createTransientBuffer(const rhi::BufferDesc& desc)
{
    const auto handle = allocateBufferHandle();
    buffers_.push_back(BufferRecord{handle, resourcePool_->acquireBuffer(desc), true});
    if (!buffers_.back().buffer) {
        buffers_.pop_back();
        throw std::runtime_error("frame graph transient buffer creation failed");
    }
    return handle;
}

BufferHandle FrameGraph::importSnapshotMetadata(
    std::shared_ptr<const rhi::BindlessSnapshot> snapshot)
{
    if (!snapshot || !snapshot->validate().success || !snapshot->metadataBuffer())
        throw std::runtime_error("RG-BINDLESS-METADATA-IMPORT-INVALID");
    auto* buffer = snapshot->metadataBuffer();
    // Alias ownership keeps the snapshot's budget leases alive as well as the
    // resource. It cannot outlive publication accounting as a standalone buffer.
    const auto handle =
        importBufferRef(std::shared_ptr<rhi::GpuBuffer>(std::move(snapshot), buffer));
    invalidateCompiledRetirementSource();
    for (auto& record : buffers_)
        if (record.handle.id == handle.id)
            record.readOnly = true;
    return handle;
}

BufferHandle FrameGraph::importSnapshotFaultLatch(
    std::shared_ptr<const rhi::BindlessSnapshot> snapshot)
{
    if (!snapshot || !snapshot->validate().success || !snapshot->faultLatchBuffer())
        throw std::runtime_error("RG-BINDLESS-FAULT-LATCH-IMPORT-INVALID");
    auto* buffer = snapshot->faultLatchBuffer();
    return importBufferRef(std::shared_ptr<rhi::GpuBuffer>(std::move(snapshot), buffer));
}

BufferHandle FrameGraph::importSnapshotFaultReadback(
    std::shared_ptr<const rhi::BindlessSnapshot> snapshot)
{
    if (!snapshot || !snapshot->validate().success || !snapshot->faultReadbackBuffer())
        throw std::runtime_error("RG-BINDLESS-FAULT-READBACK-IMPORT-INVALID");
    auto* buffer = snapshot->faultReadbackBuffer();
    return importBufferRef(std::shared_ptr<rhi::GpuBuffer>(std::move(snapshot), buffer));
}

rhi::GpuTexture& FrameGraph::texture(TextureHandle handle)
{
    for (auto& record : textures_) {
        if (record.handle.id == handle.id) {
            auto* resolved = record.resolveTexture();
            if (resolved != nullptr) {
                return *resolved;
            }
        }
    }

    throw std::runtime_error("invalid frame graph texture handle " + std::to_string(handle.id));
}

rhi::GpuBuffer& FrameGraph::buffer(BufferHandle handle)
{
    for (auto& record : buffers_) {
        if (record.handle.id == handle.id) {
            return *record.resolveBuffer();
        }
    }

    throw std::runtime_error("invalid frame graph buffer handle");
}

const rhi::TextureDesc& FrameGraph::textureDesc(TextureHandle handle) const
{
    return textureRecord(handle).resolvedDesc();
}

rhi::ResourceState FrameGraph::textureState(TextureHandle handle) const
{
    return textureRecord(handle).resolvedState();
}

bool FrameGraph::isTransient(TextureHandle handle) const
{
    for (const auto& record : textures_) {
        if (record.handle.id == handle.id) {
            return record.transient;
        }
    }

    throw std::runtime_error("invalid frame graph texture handle " + std::to_string(handle.id));
}

bool FrameGraph::isTransient(BufferHandle handle) const
{
    for (const auto& record : buffers_) {
        if (record.handle.id == handle.id) {
            return record.transient;
        }
    }

    throw std::runtime_error("invalid frame graph buffer handle");
}

void FrameGraph::setPassCullingEnabled(bool enabled)
{
    invalidateCompiledRetirementSource();
    passCullingEnabled_ = enabled;
}

PassBuilder FrameGraph::addPass(std::pmr::string name)
{
    invalidateCompiledRetirementSource();
    const auto passId = static_cast<uint32_t>(passes_.size());
    PassRecord record;
    record.name = std::move(name);
    passes_.push_back(std::move(record));
    return PassBuilder(*this, passId);
}

CompiledFrameGraph FrameGraph::compile()
{
    if (retirementSourceState_ && (!subgraphs_.empty() || !subpassGraphs_.empty()))
        throw core::DiagnosticError("RG-COMPILED-SOURCE-COMPOSITION");
    // Phase 7 [P0-B]: migrate subgraph pass records + transient textures
    // into the parent before compiling, so the compiled alias plan / barrier
    // plan includes them. Then link metadata + output-pin exit barriers.
    for (const auto& sub : subgraphs_) {
        sub->migratePassesAndTextures();
    }
    // Subpass: lower SubpassGraphs into PassRecords before compiling.
    // Native path -> single multi-subpass PassRecord. An explicitly supported
    // degraded path -> multiple independent PassRecords.
    // Lower in reverse authoring order so replacing a deferred subpass slot
    // cannot shift the placeholder index of an earlier group. Each lowered
    // group is moved back into the slot reserved by addSubpassGraph().
    for (auto it = subpassGraphs_.rbegin(); it != subpassGraphs_.rend(); ++it) {
        lowerSubpassGraph(**it);
    }
    auto compiled = FrameGraphCompiler{}.compile(*this);
    for (const auto& sub : subgraphs_) {
        sub->compileAndLink(compiled, *this);
    }
    publishCompiledRetirementSource(compiled);
    return compiled;
}

void FrameGraph::lowerSubpassGraph(SubpassGraph& group)
{
    if (group.lowered_) {
        return;
    }

    const auto subpassCount = group.subpasses_.size();
    if (subpassCount == 0) {
        if (group.placeholderPassIndex_ != UINT32_MAX
            && group.placeholderPassIndex_ < passes_.size()) {
            passes_.erase(passes_.begin() + group.placeholderPassIndex_);
            group.placeholderPassIndex_ = UINT32_MAX;
        }
        group.lowered_ = true;
        return;
    }

    const auto loweredPassBegin = passes_.size();
    const auto placeholderPassIndex = group.placeholderPassIndex_;
    auto restoreAuthoringOrder = [&]() {
        if (placeholderPassIndex == UINT32_MAX || placeholderPassIndex >= loweredPassBegin) {
            throw std::runtime_error(
                "frame graph subpass lowering lost its authoring-order placeholder");
        }
        const auto loweredPassCount = passes_.size() - loweredPassBegin;
        if (loweredPassCount == 0u) {
            passes_.erase(passes_.begin() + placeholderPassIndex);
            group.placeholderPassIndex_ = UINT32_MAX;
            return;
        }

        // [prefix][placeholder][middle][lowered] ->
        // [prefix][lowered][placeholder][middle], then remove placeholder.
        std::rotate(passes_.begin() + placeholderPassIndex,
            passes_.begin() + loweredPassBegin,
            passes_.end());
        passes_.erase(passes_.begin() + placeholderPassIndex + loweredPassCount);
        group.placeholderPassIndex_ = UINT32_MAX;
    };

    // Resolve the backend-neutral lowering decision once. Backend identity and
    // shader target selection stay below the renderer/RHI boundary.
    const auto& caps = device_.capabilities();
    const auto subpassContract = rhi::semantics::subpassInputLoweringContract(caps);
    if (subpassContract.mode == rhi::semantics::SubpassInputLoweringMode::DiagnosticFailure) {
        throw std::runtime_error(
            arp::core::toStdString(std::pmr::string("frame graph subpass group '") + group.name_
                + "' requires an executable backend subpass lowering; the active "
                  "backend reported DiagnosticFailure"));
    }
    const bool nativeSubpass =
        subpassContract.mode == rhi::semantics::SubpassInputLoweringMode::Native;

    // Every logical input attachment must name the shader slot that consumes
    // it. The old attachment-only API remains available solely to produce
    // this deterministic diagnostic; no backend may infer the slot later.
    for (const auto& subpass : group.subpasses_) {
        for (const auto& input : subpass->inputReads_) {
            if (!input.shaderBinding.specified()) {
                throw std::runtime_error(
                    arp::core::toStdString(std::pmr::string("frame graph subpass '")
                        + subpass->name_ + "' in group '" + group.name_ + "' input attachment "
                        + arp::core::toPmrString(std::to_string(input.attachmentIndex))
                        + " is missing an explicit shader set/binding"));
            }
            for (const auto& other : subpass->inputReads_) {
                if (&other == &input) {
                    break;
                }
                if (other.shaderBinding.set == input.shaderBinding.set
                    && other.shaderBinding.binding == input.shaderBinding.binding
                    && other.attachmentIndex != input.attachmentIndex) {
                    throw std::runtime_error(arp::core::toStdString(
                        std::pmr::string("frame graph subpass '") + subpass->name_ + "' in group '"
                        + group.name_ + "' maps one shader binding to multiple input attachments"));
                }
            }
        }
    }

    bool hasDepthInputAttachmentRead = false;
    for (const auto& subpass : group.subpasses_) {
        for (const auto& input : subpass->inputReads_) {
            if (input.attachmentIndex < group.attachments_.size()
                && group.attachments_[input.attachmentIndex].role
                    == AttachmentRole::DepthAttachment) {
                hasDepthInputAttachmentRead = true;
            }
        }
    }
    // A depth input attachment is not interchangeable with a color sampled
    // texture. The separate-pass contract carries the depth semantic through
    // WGSL texture_depth_2d, reflection, and the WebGPU depth sample type.
    if (hasDepthInputAttachmentRead && !caps.features.subpassDepthInputAttachment
        && !caps.features.subpassDepthInputAttachmentDegraded) {
        throw std::runtime_error(
            arp::core::toStdString(std::pmr::string("frame graph subpass group '") + group.name_
                + "' requests a depth input attachment, but the active backend "
                  "does not provide an executable SubpassDepthInputAttachment "
                  "lowering"));
    }

    // Validate input-attachment read timing: an input attachment read in
    // subpass N must be written by some subpass M < N. A read-before-write
    // would read uninitialized tile-local / main-memory data. This is a
    // deterministic diagnostic (AGENTS.md: fail on undeterminable resource
    // semantics rather than silently producing garbage).
    {
        std::pmr::vector<bool> attachmentWrittenByEarlierSubpass(group.attachments_.size(), false);
        for (std::uint32_t sp = 0; sp < subpassCount; ++sp) {
            const auto& sub = *group.subpasses_[sp];
            for (const auto& ir : sub.inputReads_) {
                if (ir.attachmentIndex >= attachmentWrittenByEarlierSubpass.size()
                    || !attachmentWrittenByEarlierSubpass[ir.attachmentIndex]) {
                    throw std::runtime_error(
                        arp::core::toStdString(std::pmr::string("frame graph subpass '") + sub.name_
                            + "' in group '" + group.name_ + "' reads input attachment "
                            + arp::core::toPmrString(std::to_string(ir.attachmentIndex))
                            + " that was not written by any earlier subpass"));
                }
            }
            // Mark attachments written by this subpass as available to later
            // subpasses (after the read check for this subpass completes).
            for (const auto& aw : sub.attachmentWrites_) {
                if (aw.attachmentIndex < attachmentWrittenByEarlierSubpass.size()) {
                    attachmentWrittenByEarlierSubpass[aw.attachmentIndex] = true;
                }
            }
        }
    }

    if (nativeSubpass) {
        // --- Native path: single multi-subpass PassRecord ---
        auto info = std::make_shared<FrameGraph::SubpassGroupInfo>();
        info->groupName = group.name_;

        FrameGraph::PassRecord pr;
        pr.name = group.name_;
        pr.kind = PassKind::Graphics;
        pr.queue = QueueRole::Graphics;

        // Build attachment list from the SubpassGraph's declared attachments.
        // Color attachments first, then depth (matching RenderPassDesc layout).
        std::pmr::vector<std::uint32_t> attachmentIndexToColorIndex;
        std::uint32_t depthAttachmentIndex = UINT32_MAX;
        std::uint32_t colorCount = 0;

        for (std::uint32_t i = 0; i < group.attachments_.size(); ++i) {
            const auto& decl = group.attachments_[i];
            if (decl.role == AttachmentRole::ColorAttachment) {
                attachmentIndexToColorIndex.push_back(colorCount);
                pr.attachmentWrites.push_back({decl.handle,
                    decl.state,
                    rhi::LoadOp::Load,
                    rhi::StoreOp::Store,
                    decl.tileHints,
                    {}});
                // Also register as a normal write so the validator,
                // culler, and dependency tracker see the attachment
                // texture as produced by this pass.
                pr.writes.push_back({decl.handle, decl.state});
                ++colorCount;
            } else {
                attachmentIndexToColorIndex.push_back(UINT32_MAX);
                depthAttachmentIndex = i;
            }
        }

        // Add depth attachment to the PassRecord if present.
        if (depthAttachmentIndex != UINT32_MAX) {
            const auto& decl = group.attachments_[depthAttachmentIndex];
            // Depth is tracked via attachmentWrites too (the executor checks
            // format to distinguish color vs depth).
            pr.attachmentWrites.push_back({decl.handle,
                decl.state,
                rhi::LoadOp::Load,
                rhi::StoreOp::Store,
                decl.tileHints,
                {}});
            pr.writes.push_back({decl.handle, decl.state});
        }

        // Track first/last use of each attachment to set the correct
        // load/store on the PassRecord's attachmentWrites. The render pass
        // load op comes from the first subpass that touches the attachment;
        // the store op comes from the last subpass that touches it.
        std::pmr::vector<bool> attachmentSeen(group.attachments_.size(), false);

        // Build per-subpass entries + RHI SubpassDesc.
        for (std::uint32_t sp = 0; sp < subpassCount; ++sp) {
            const auto& sub = *group.subpasses_[sp];
            FrameGraph::SubpassGroupInfo::SubpassEntry entry;
            entry.name = sub.name_;
            entry.callback = sub.callback_;

            rhi::SubpassDesc subpassDesc;

            // Resolve attachment writes for this subpass.
            for (const auto& aw : sub.attachmentWrites_) {
                const auto& decl = group.attachments_[aw.attachmentIndex];
                entry.writtenAttachmentIndices.push_back(aw.attachmentIndex);

                if (decl.role == AttachmentRole::ColorAttachment) {
                    const auto colorIdx = attachmentIndexToColorIndex[aw.attachmentIndex];
                    // First use sets the render pass load op.
                    if (!attachmentSeen[aw.attachmentIndex]) {
                        pr.attachmentWrites[colorIdx].loadOp = aw.loadOp;
                        pr.attachmentWrites[colorIdx].clearColor = aw.clearColor;
                        attachmentSeen[aw.attachmentIndex] = true;
                    }
                    // Last use wins for store op (overwrite each time).
                    pr.attachmentWrites[colorIdx].storeOp = aw.storeOp;
                    subpassDesc.colorAttachmentIndices.push_back(colorIdx);
                } else {
                    // Depth attachment - already in pr.attachmentWrites at
                    // the depth slot (index == colorCount in attachmentWrites).
                    const auto depthSlot = colorCount;
                    if (!attachmentSeen[aw.attachmentIndex]) {
                        pr.attachmentWrites[depthSlot].loadOp = aw.loadOp;
                        pr.attachmentWrites[depthSlot].clearColor = aw.clearColor;
                        attachmentSeen[aw.attachmentIndex] = true;
                    }
                    pr.attachmentWrites[depthSlot].storeOp = aw.storeOp;
                    subpassDesc.useDepthStencilAttachment = true;
                }
            }

            // Resolve input attachment reads.
            for (const auto& ir : sub.inputReads_) {
                const auto& decl = group.attachments_[ir.attachmentIndex];
                entry.inputAttachmentIndices.push_back(ir.attachmentIndex);
                auto shaderBinding = ir.shaderBinding;
                shaderBinding.logicalAttachmentIndex = ir.attachmentIndex;
                shaderBinding.physicalBinding = subpassContract.physicalBinding;
                shaderBinding.depthTexture = decl.role == AttachmentRole::DepthAttachment;
                const auto loweringId = static_cast<std::uint32_t>(subpassInputLowerings_.size());
                SubpassInputDescriptorRoute route{
                    shaderBinding,
                    {},
                    {},
                    loweringId,
                };
                entry.subpassInputBindings.push_back(route);
                subpassInputLowerings_.push_back({
                    group.name_,
                    sp,
                    ir.attachmentIndex,
                    decl.handle,
                    subpassContract.physicalBinding,
                    subpassContract.loadSemantics,
                    subpassContract.requiresSampler,
                    shaderBinding,
                    {},
                    {},
                });
                if (decl.role == AttachmentRole::ColorAttachment) {
                    const auto colorIdx = attachmentIndexToColorIndex[ir.attachmentIndex];
                    subpassDesc.inputAttachmentIndices.push_back(colorIdx);
                } else {
                    subpassDesc.readDepthInputAttachment = true;
                }
                // Also register as a read for dependency tracking.
                pr.reads.push_back({decl.handle, rhi::ResourceState::ShaderRead});
            }

            // Resolve external reads.
            for (const auto& er : sub.externalReads_) {
                pr.reads.push_back({er.handle, er.state});
            }

            info->subpasses.push_back(std::move(entry));
            info->subpassDescs.push_back(std::move(subpassDesc));
        }

        // Build subpass dependencies. Two sources are merged (deduplicated):
        //   1. Data-flow dependencies: for each input attachment read in
        //      subpass `dst`, find the earliest subpass `src` that wrote the
        //      attachment, producing {src -> dst}. This is the precise
        //      tile-local data dependency (BY_REGION barrier) that makes the
        //      written data visible to the reading subpass - essential when
        //      the reader is not adjacent to the writer (e.g. subpass 0
        //      writes depth, subpass 2 reads it).
        //   2. Sequential adjacent dependencies {0->1, 1->2, ...} as a
        //      conservative supplement to guarantee BY_REGION barriers
        //      between consecutive subpasses even without an explicit
        //      input-attachment edge.
        {
            // Map attachment index -> earliest subpass that writes it.
            std::pmr::vector<std::uint32_t> firstWriterSubpass(
                group.attachments_.size(), UINT32_MAX);
            for (std::uint32_t sp = 0; sp < subpassCount; ++sp) {
                for (const auto& aw : group.subpasses_[sp]->attachmentWrites_) {
                    if (aw.attachmentIndex < firstWriterSubpass.size()
                        && firstWriterSubpass[aw.attachmentIndex] == UINT32_MAX) {
                        firstWriterSubpass[aw.attachmentIndex] = sp;
                    }
                }
            }

            // Deduplicated dependency set: (src, dst) pairs.
            std::pmr::vector<std::pair<std::uint32_t, std::uint32_t>> depSet;
            auto addDep = [&](std::uint32_t src, std::uint32_t dst) {
                if (src == dst || src == UINT32_MAX) {
                    return;
                }
                for (const auto& existing : depSet) {
                    if (existing.first == src && existing.second == dst) {
                        return;
                    }
                }
                depSet.emplace_back(src, dst);
            };

            // Data-flow dependencies from input attachment reads.
            for (std::uint32_t sp = 0; sp < subpassCount; ++sp) {
                for (const auto& ir : group.subpasses_[sp]->inputReads_) {
                    if (ir.attachmentIndex < firstWriterSubpass.size()) {
                        addDep(firstWriterSubpass[ir.attachmentIndex], sp);
                    }
                }
            }
            // Sequential adjacent dependencies (conservative supplement).
            for (std::uint32_t sp = 0; sp + 1 < subpassCount; ++sp) {
                addDep(sp, sp + 1);
            }

            for (const auto& dep : depSet) {
                info->subpassDependencies.push_back({dep.first, dep.second});
            }
        }

        pr.subpassGroup = std::move(info);
        passes_.push_back(std::move(pr));
    } else {
        // --- Separate-pass path ---
        // Each subpass becomes an independent pass. Input attachment reads
        // become cross-pass sampled texture reads. DontCare stores become
        // Store (the texture must persist to the next pass).
        for (std::uint32_t sp = 0; sp < subpassCount; ++sp) {
            const auto& sub = *group.subpasses_[sp];

            FrameGraph::PassRecord pr;
            pr.name = group.name_ + std::pmr::string("::") + sub.name_;
            pr.kind = PassKind::Graphics;
            pr.queue = QueueRole::Graphics;

            // External reads pass through as normal reads.
            for (const auto& er : sub.externalReads_) {
                pr.reads.push_back({er.handle, er.state});
            }

            // Input attachment reads become cross-pass sampled reads.
            for (const auto& ir : sub.inputReads_) {
                const auto& decl = group.attachments_[ir.attachmentIndex];
                auto shaderBinding = ir.shaderBinding;
                shaderBinding.logicalAttachmentIndex = ir.attachmentIndex;
                shaderBinding.physicalBinding = subpassContract.physicalBinding;
                shaderBinding.depthTexture = decl.role == AttachmentRole::DepthAttachment;
                const auto loweringId = static_cast<std::uint32_t>(subpassInputLowerings_.size());
                SubpassInputDescriptorRoute route{
                    shaderBinding,
                    {},
                    {},
                    loweringId,
                };
                subpassInputLowerings_.push_back({
                    group.name_,
                    sp,
                    ir.attachmentIndex,
                    decl.handle,
                    subpassContract.physicalBinding,
                    subpassContract.loadSemantics,
                    subpassContract.requiresSampler,
                    shaderBinding,
                    {},
                    {},
                });
                pr.subpassInputBindings.push_back(route);
                pr.reads.push_back({decl.handle, rhi::ResourceState::ShaderRead});
            }

            // Attachment writes: on the degraded path, each subpass is a
            // separate render pass, so an attachment written by subpass `sp`
            // must persist to main memory (Store) if any later subpass
            // consumes its content. Consumption happens in two ways:
            //   (a) a later subpass reads it as an input attachment
            //       (SubpassBuilder::readInputAttachment), or
            //   (b) a later subpass writes it with LoadOp::Load (which reads
            //       the prior pass's content into the tile). A DontCare store
            //       in case (b) would leave Load reading uninitialized memory.
            // Only DontCare stores need promotion; an explicit Store or
            // DontCare-with-no-later-consumer is honored as authored.
            auto isAttachmentConsumedLater = [&](std::uint32_t attachmentIndex,
                                                 std::uint32_t fromSubpass) {
                for (std::uint32_t later = fromSubpass; later < subpassCount; ++later) {
                    const auto& laterSub = *group.subpasses_[later];
                    // (a) input attachment read.
                    for (const auto& lr : laterSub.inputReads_) {
                        if (lr.attachmentIndex == attachmentIndex) {
                            return true;
                        }
                    }
                    // (b) attachment write with LoadOp::Load (reads prior content).
                    for (const auto& lw : laterSub.attachmentWrites_) {
                        if (lw.attachmentIndex == attachmentIndex
                            && lw.loadOp == rhi::LoadOp::Load) {
                            return true;
                        }
                    }
                }
                return false;
            };

            for (const auto& aw : sub.attachmentWrites_) {
                const auto& decl = group.attachments_[aw.attachmentIndex];
                rhi::StoreOp effectiveStore = aw.storeOp;
                if (aw.storeOp == rhi::StoreOp::DontCare
                    && isAttachmentConsumedLater(aw.attachmentIndex, sp + 1)) {
                    effectiveStore = rhi::StoreOp::Store;
                }
                pr.attachmentWrites.push_back({decl.handle,
                    decl.state,
                    aw.loadOp,
                    effectiveStore,
                    decl.tileHints,
                    aw.clearColor});
                // Also register as a normal write so the validator,
                // culler, and dependency tracker see the attachment
                // texture as produced by this pass.
                pr.writes.push_back({decl.handle, decl.state});
            }

            pr.renderPassCallback = sub.callback_;
            passes_.push_back(std::move(pr));
        }
    }

    restoreAuthoringOrder();

    // Do not poison retryable authoring errors or capability diagnostics:
    // this state becomes valid only after the complete lowering succeeds.
    group.lowered_ = true;
}

void FrameGraphCompiler::validateGraph(const FrameGraph& graph) const
{
    std::pmr::map<uint32_t, std::pmr::string> textureProducers;
    std::pmr::map<uint32_t, std::pmr::string> bufferProducers;
    std::pmr::map<uint32_t, bool> textureWritten;
    std::pmr::map<uint32_t, bool> bufferWritten;

    for (const auto& pass : graph.passes_) {
        for (const auto& write : pass.writes) {
            textureWritten[write.handle.id] = true;
            if (textureProducers[write.handle.id].empty()) {
                textureProducers[write.handle.id] = pass.name;
            }
        }
        for (const auto& write : pass.bufferWrites) {
            if (graph.bufferRecord(write.handle).readOnly)
                throw std::runtime_error("RG-BINDLESS-METADATA-READ-ONLY");
            bufferWritten[write.handle.id] = true;
            if (bufferProducers[write.handle.id].empty()) {
                bufferProducers[write.handle.id] = pass.name;
            }
        }
    }

    for (const auto& pass : graph.passes_) {
        for (const auto& read : pass.reads) {
            const auto& record = graph.textureRecord(read.handle);
            if (!record.imported && textureWritten.find(read.handle.id) == textureWritten.end()) {
                throw std::runtime_error(arp::core::toStdString(
                    std::pmr::string("frame graph pass '") + pass.name + "' reads texture '"
                    + record.resolvedDesc().label + "' that was never written by any pass"));
            }
            if (read.state == rhi::ResourceState::ShaderRead
                && record.resolvedState() == rhi::ResourceState::Undefined && !record.imported
                && !textureWritten[read.handle.id]) {
                throw std::runtime_error(arp::core::toStdString(
                    std::pmr::string("frame graph pass '") + pass.name + "' reads texture '"
                    + record.resolvedDesc().label
                    + "' with ShaderRead but the texture is in Undefined state and was never written"));
            }
        }
        for (const auto& read : pass.bufferReads) {
            const auto& record = graph.bufferRecord(read.handle);
            const auto access =
                read.access == rhi::ResourceAccess::None ? accessFor(read.state) : read.access;
            if (record.readOnly && rhi::semantics::resourceAccessWrites(access))
                throw std::runtime_error("RG-BINDLESS-METADATA-READ-ONLY");
            if (bufferWritten.find(read.handle.id) == bufferWritten.end()
                && (!record.imported()
                    || record.resolveBuffer()->state() == rhi::ResourceState::Undefined)) {
                throw std::runtime_error(
                    arp::core::toStdString(std::pmr::string("frame graph pass '") + pass.name
                        + "' reads buffer '" + record.resolveBuffer()->desc().label
                        + "' that was never written by any pass"));
            }
        }
    }

    for (const auto& pass : graph.passes_) {
        for (const auto& attachment : pass.attachmentWrites) {
            const auto& record = graph.textureRecord(attachment.handle);
            const auto& desc = record.resolvedDesc();
            const bool isDepth = (attachment.state == rhi::ResourceState::DepthAttachment)
                || rhi::isDepthTextureFormat(desc.format);
            if (attachment.state == rhi::ResourceState::ColorAttachment
                && rhi::isDepthTextureFormat(desc.format)) {
                throw std::runtime_error(arp::core::toStdString(
                    std::pmr::string("frame graph pass '") + pass.name
                    + "' declares ColorAttachment on depth-format texture '" + desc.label + "'"));
            }
            if (attachment.state == rhi::ResourceState::DepthAttachment
                && !rhi::isDepthTextureFormat(desc.format)) {
                throw std::runtime_error(arp::core::toStdString(
                    std::pmr::string("frame graph pass '") + pass.name
                    + "' declares DepthAttachment on color-format texture '" + desc.label + "'"));
            }
        }
    }

    for (const auto& pass : graph.passes_) {
        if (pass.kind == PassKind::Graphics) {
            for (const auto& read : pass.reads) {
                if (read.state == rhi::ResourceState::ShaderWrite
                    && (read.access == rhi::ResourceAccess::None
                        || rhi::semantics::resourceAccessWrites(read.access))) {
                    throw std::runtime_error(arp::core::toStdString(
                        std::pmr::string("frame graph graphics pass '") + pass.name
                        + "' declares ShaderWrite state on a texture; use a compute pass for storage writes"));
                }
            }
        }
    }

    for (std::size_t passIndex = 0; passIndex < graph.passes_.size(); ++passIndex) {
        const auto& pass = graph.passes_[passIndex];
        for (const auto& attachment : pass.attachmentWrites) {
            const auto& record = graph.textureRecord(attachment.handle);
            // Content validity contract (P1-C): a texture read with
            // LoadOp::Load must have valid prior content. Transient textures
            // are frame-local with no external content, and textures declared
            // Undefined have no producer yet — both read uninitialized memory
            // if no prior pass wrote them. This is independent of the
            // declared initial state: a transient color target created with
            // initial state ColorAttachment (render_pipeline.cpp) still has
            // no real producer, so Load on it without a prior write is a
            // read of uninitialized GPU memory.
            if (attachment.loadOp == rhi::LoadOp::Load && !record.imported
                && (record.transient || record.resolvedState() == rhi::ResourceState::Undefined)) {
                bool hasPriorWrite = false;
                // Scan only passes BEFORE the current pass (by index, not by
                // name): pass names have no uniqueness constraint, so a
                // name-based "stop at the current pass" check would break
                // early at an earlier pass that happens to share the name,
                // missing a real prior write and producing a false diagnostic.
                for (std::size_t otherIndex = 0; otherIndex < passIndex; ++otherIndex) {
                    const auto& otherPass = graph.passes_[otherIndex];
                    for (const auto& write : otherPass.writes) {
                        if (write.handle.id == attachment.handle.id) {
                            hasPriorWrite = true;
                            break;
                        }
                    }
                    if (!hasPriorWrite) {
                        for (const auto& aw : otherPass.attachmentWrites) {
                            if (aw.handle.id == attachment.handle.id) {
                                hasPriorWrite = true;
                                break;
                            }
                        }
                    }
                    if (hasPriorWrite) {
                        break;
                    }
                }
                if (!hasPriorWrite) {
                    throw std::runtime_error(
                        arp::core::toStdString(std::pmr::string("frame graph pass '") + pass.name
                            + "' uses LoadOp::Load on texture '" + record.resolvedDesc().label
                            + "' that is in Undefined state with no prior write"));
                }
            }
        }
    }
}

CompiledFrameGraph FrameGraphCompiler::compile(const FrameGraph& graph) const
{
    if (const auto* id = graph.ownedResourceSourceDiagnostic())
        throw core::DiagnosticError(id);
    if (const auto* id = graph.sharedImportSourceDiagnostic())
        throw core::DiagnosticError(id);
    CompiledFrameGraph compiled;
    compiled.setCommandEncoderDesc(graph.commandEncoderDesc_);
    std::pmr::vector<bool> culled(graph.passes_.size(), false);

    validateGraph(graph);

    const size_t maxCullIterations = graph.passes_.size() + 1;
    // Phase 6: passes inside a subgraph's range are culled as a unit by
    // shouldCullAsUnit, never by per-pass shouldCullPass. This matches the
    // "do not propagate upward beyond the parent producer's own cull
    // decision" contract and avoids per-pass isTextureReadAfter (which is
    // declaration-order based) mis-culling a subgraph whose external consumer
    // was declared before the migrated subgraph passes.
    auto isInSubgraphRange = [&](size_t idx) {
        for (const auto& sub : graph.subgraphs_) {
            if (idx >= sub->passRange_.first && idx < sub->passRange_.second) {
                return true;
            }
        }
        return false;
    };
    for (size_t iteration = 0; iteration < maxCullIterations; ++iteration) {
        bool changed = false;
        for (size_t passIndex = 0; passIndex < graph.passes_.size(); ++passIndex) {
            if (culled[passIndex]) {
                continue;
            }
            if (isInSubgraphRange(passIndex)) {
                continue;
            }
            if (graph.shouldCullPass(passIndex, culled)) {
                culled[passIndex] = true;
                changed = true;
            }
        }
        // Phase 6: subgraph whole-unit cull. If a subgraph's output pins all
        // have no external consumer, cull its entire pass range as a unit.
        // This converges faster than relying on per-pass cascade and records
        // the culledAsUnit semantics. Monotonic: culled passes stay culled.
        if (graph.passCullingEnabled_) {
            for (const auto& sub : graph.subgraphs_) {
                if (sub->shouldCullAsUnit(culled)) {
                    if (sub->markCulledAsUnit(culled)) {
                        changed = true;
                    }
                }
            }
        }
        if (!changed) {
            break;
        }
        if (iteration + 1 == maxCullIterations) {
            throw std::runtime_error("frame graph culling failed to converge");
        }
    }

    for (size_t passIndex = 0; passIndex < graph.passes_.size(); ++passIndex) {
        const auto& passRecord = graph.passes_[passIndex];
        CompiledPass compiledPass{
            passIndex,
            passRecord.name,
            passRecord.kind,
            passRecord.queue,
            passRecord.asyncComputeEligible,
            culled[passIndex],
        };
        compiledPass.rhiDrawCommands = passRecord.rhiDrawCommands;
        compiledPass.rhiDispatchCommands = passRecord.rhiDispatchCommands;
        compiledPass.rhiTransferCommands = passRecord.rhiTransferCommands;
        compiled.addPass(std::move(compiledPass));
        for (const auto& read : passRecord.reads) {
            compiled.addPassResourceUse(passIndex,
                CompiledResourceUse{graph.textureRecord(read.handle).resolvedDesc().label,
                    ResourceKind::Texture,
                    ResourceAccess::Read,
                    read.state,
                    read.handle.id,
                    read.stages == rhi::PipelineStage::None ? stageFor(read.state) : read.stages,
                    read.access == rhi::ResourceAccess::None ? accessFor(read.state) : read.access,
                    read.stages == rhi::PipelineStage::None
                        ? rhi::defaultTextureSubresourceRange(graph.textureDesc(read.handle))
                        : read.range});
        }
        for (const auto& read : passRecord.bufferReads) {
            compiled.addPassResourceUse(passIndex,
                CompiledResourceUse{graph.bufferRecord(read.handle).resolveBuffer()->desc().label,
                    ResourceKind::Buffer,
                    ResourceAccess::Read,
                    read.state,
                    read.handle.id,
                    read.stages == rhi::PipelineStage::None ? stageFor(read.state) : read.stages,
                    read.access == rhi::ResourceAccess::None ? accessFor(read.state) : read.access,
                    {},
                    read.offset,
                    read.stages == rhi::PipelineStage::None
                        ? graph.bufferRecord(read.handle).resolveBuffer()->desc().sizeBytes
                        : read.size});
        }
        for (const auto& write : passRecord.writes) {
            compiled.addPassResourceUse(passIndex,
                CompiledResourceUse{graph.textureRecord(write.handle).resolvedDesc().label,
                    ResourceKind::Texture,
                    ResourceAccess::Write,
                    write.state,
                    write.handle.id,
                    write.stages == rhi::PipelineStage::None ? stageFor(write.state) : write.stages,
                    write.access == rhi::ResourceAccess::None ? accessFor(write.state)
                                                              : write.access,
                    write.stages == rhi::PipelineStage::None
                        ? rhi::defaultTextureSubresourceRange(graph.textureDesc(write.handle))
                        : write.range});
        }
        for (const auto& write : passRecord.bufferWrites) {
            compiled.addPassResourceUse(passIndex,
                CompiledResourceUse{graph.bufferRecord(write.handle).resolveBuffer()->desc().label,
                    ResourceKind::Buffer,
                    ResourceAccess::Write,
                    write.state,
                    write.handle.id,
                    write.stages == rhi::PipelineStage::None ? stageFor(write.state) : write.stages,
                    write.access == rhi::ResourceAccess::None ? accessFor(write.state)
                                                              : write.access,
                    {},
                    write.offset,
                    write.stages == rhi::PipelineStage::None
                        ? graph.bufferRecord(write.handle).resolveBuffer()->desc().sizeBytes
                        : write.size});
        }
    }

    const auto noPass = graph.passes_.size();

    struct ResourceDependencyState {
        size_t lastWrite = 0;
        std::pmr::vector<size_t> lastReads;
        std::pmr::vector<size_t> pendingReadsBeforeFirstWrite;
    };

    std::pmr::vector<std::pmr::vector<size_t>> edges(graph.passes_.size());
    std::pmr::vector<size_t> indegree(graph.passes_.size(), 0);
    auto addEdge = [&](size_t before,
                       size_t after,
                       const std::pmr::string& resourceName,
                       ResourceKind resourceKind,
                       TopologyDependencyKind dependency,
                       bool renderTargetDependency = false) {
        if (before == after || culled[before] || culled[after]) {
            return;
        }
        compiled.addDependency(CompiledDependencyRecord{
            before, after, resourceName, resourceKind, dependency, renderTargetDependency});
        if (std::find(edges[before].begin(), edges[before].end(), after) != edges[before].end()) {
            return;
        }
        edges[before].push_back(after);
        ++indegree[after];
    };

    auto registerRead = [&](ResourceDependencyState& state,
                            size_t passIndex,
                            const std::pmr::string& resourceName,
                            ResourceKind resourceKind) {
        if (state.lastWrite != noPass) {
            addEdge(state.lastWrite,
                passIndex,
                resourceName,
                resourceKind,
                TopologyDependencyKind::Raw);
        } else {
            state.pendingReadsBeforeFirstWrite.push_back(passIndex);
        }
        state.lastReads.push_back(passIndex);
    };
    auto registerWrite = [&](ResourceDependencyState& state,
                             size_t passIndex,
                             const std::pmr::string& resourceName,
                             ResourceKind resourceKind,
                             bool preserveInitialContents,
                             bool renderTargetDependency = false) {
        if (state.lastWrite == noPass && !state.pendingReadsBeforeFirstWrite.empty()) {
            for (const auto pendingRead : state.pendingReadsBeforeFirstWrite) {
                if (preserveInitialContents) {
                    addEdge(pendingRead,
                        passIndex,
                        resourceName,
                        resourceKind,
                        TopologyDependencyKind::War,
                        renderTargetDependency);
                } else {
                    addEdge(passIndex,
                        pendingRead,
                        resourceName,
                        resourceKind,
                        TopologyDependencyKind::Raw,
                        renderTargetDependency);
                }
            }
        } else {
            if (state.lastWrite != noPass) {
                addEdge(state.lastWrite,
                    passIndex,
                    resourceName,
                    resourceKind,
                    TopologyDependencyKind::Waw,
                    renderTargetDependency);
            }
            for (const auto read : state.lastReads) {
                addEdge(read,
                    passIndex,
                    resourceName,
                    resourceKind,
                    TopologyDependencyKind::War,
                    renderTargetDependency);
            }
        }
        state.pendingReadsBeforeFirstWrite.clear();
        state.lastReads.clear();
        state.lastWrite = passIndex;
    };

    std::pmr::map<uint32_t, ResourceDependencyState> textureStates;
    std::pmr::map<uint32_t, ResourceDependencyState> bufferStates;
    for (const auto& texture : graph.textures_) {
        textureStates.emplace(texture.handle.id, ResourceDependencyState{noPass, {}, {}});
    }
    for (const auto& buffer : graph.buffers_) {
        bufferStates.emplace(buffer.handle.id, ResourceDependencyState{noPass, {}, {}});
    }

    for (size_t passIndex = 0; passIndex < graph.passes_.size(); ++passIndex) {
        if (culled[passIndex]) {
            continue;
        }

        const auto& passRecord = graph.passes_[passIndex];
        for (const auto& read : passRecord.reads) {
            registerRead(textureStates[read.handle.id],
                passIndex,
                graph.textureRecord(read.handle).resolvedDesc().label,
                ResourceKind::Texture);
        }
        for (const auto& read : passRecord.bufferReads) {
            registerRead(bufferStates[read.handle.id],
                passIndex,
                graph.bufferRecord(read.handle).resolveBuffer()->desc().label,
                ResourceKind::Buffer);
        }
        for (const auto& write : passRecord.writes) {
            const auto& texture = graph.textureRecord(write.handle);
            const bool attachmentWrite = std::any_of(passRecord.attachmentWrites.begin(),
                passRecord.attachmentWrites.end(),
                [&](const auto& attachment) {
                    return attachment.handle.id == write.handle.id;
                });
            registerWrite(textureStates[write.handle.id],
                passIndex,
                texture.resolvedDesc().label,
                ResourceKind::Texture,
                texture.imported && texture.preserveInitialContents,
                attachmentWrite);
        }
        for (const auto& write : passRecord.bufferWrites) {
            registerWrite(bufferStates[write.handle.id],
                passIndex,
                graph.bufferRecord(write.handle).resolveBuffer()->desc().label,
                ResourceKind::Buffer,
                graph.bufferRecord(write.handle).imported());
        }
    }

    size_t executableCount = 0;
    for (size_t passIndex = 0; passIndex < graph.passes_.size(); ++passIndex) {
        if (!culled[passIndex]) {
            ++executableCount;
        }
    }

    std::pmr::vector<bool> emitted(graph.passes_.size(), false);
    for (;;) {
        size_t next = graph.passes_.size();
        for (size_t passIndex = 0; passIndex < graph.passes_.size(); ++passIndex) {
            if (!culled[passIndex] && !emitted[passIndex] && indegree[passIndex] == 0) {
                next = passIndex;
                break;
            }
        }
        if (next == graph.passes_.size()) {
            break;
        }
        emitted[next] = true;
        compiled.addExecutablePass(next);
        for (const auto dependent : edges[next]) {
            --indegree[dependent];
        }
    }

    if (compiled.executablePassCount() != executableCount) {
        std::pmr::string cycleNames;
        for (size_t passIndex = 0; passIndex < graph.passes_.size(); ++passIndex) {
            if (!culled[passIndex] && !emitted[passIndex]) {
                if (!cycleNames.empty()) {
                    cycleNames += " -> ";
                }
                cycleNames += graph.passes_[passIndex].name;
            }
        }
        throw std::runtime_error(arp::core::toStdString(
            std::pmr::string("frame graph dependency cycle detected involving passes: ")
            + cycleNames));
    }

    for (size_t passIndex = 0; passIndex < graph.passes_.size(); ++passIndex) {
        const auto& passRecord = graph.passes_[passIndex];
        for (const auto& attachment : passRecord.attachmentWrites) {
            CompiledRenderTargetFlow flow;
            flow.writerPass = passIndex;
            flow.targetName = graph.textureRecord(attachment.handle).resolvedDesc().label;
            flow.role = attachment.state == rhi::ResourceState::DepthAttachment
                ? AttachmentRole::DepthAttachment
                : AttachmentRole::ColorAttachment;
            flow.loadOp = attachment.loadOp;
            flow.storeOp = attachment.storeOp;
            for (const auto& dependency : compiled.dependencies()) {
                if (dependency.fromPass == passIndex
                    && dependency.resourceName == flow.targetName) {
                    flow.consumers.push_back(dependency.toPass);
                }
            }
            compiled.addRenderTargetFlow(std::move(flow));
        }

        if (passRecord.subpassGroup) {
            CompiledSubpassTopology topology;
            topology.groupName = passRecord.subpassGroup->groupName;
            topology.parentPass = passIndex;
            for (const auto& subpass : passRecord.subpassGroup->subpasses) {
                CompiledSubpassTopology::Entry entry;
                entry.name = subpass.name;
                for (const auto index : subpass.writtenAttachmentIndices) {
                    if (index < passRecord.attachmentWrites.size()) {
                        entry.writes.push_back(
                            graph.textureRecord(passRecord.attachmentWrites[index].handle)
                                .resolvedDesc()
                                .label);
                    }
                }
                for (const auto index : subpass.inputAttachmentIndices) {
                    if (index < passRecord.attachmentWrites.size()) {
                        entry.inputs.push_back(
                            graph.textureRecord(passRecord.attachmentWrites[index].handle)
                                .resolvedDesc()
                                .label);
                    }
                }
                topology.entries.push_back(std::move(entry));
            }
            compiled.addSubpassTopology(std::move(topology));
        }
    }

    for (const auto& compiledPassIndex : compiled.executionOrder()) {
        const auto& pass = compiled.passes()[compiledPassIndex];
        compiled.addQueuePlan(QueuePlanRecord{
            pass.name,
            pass.kind,
            pass.queue,
            pass.asyncComputeEligible,
        });
    }

    for (const auto& texture : graph.textures_) {
        if (!texture.imported) {
            continue;
        }
        // Imported resources carry two separate facts: their live acquire
        // state (used for the entry barrier) and their explicit external
        // release contract. Present is never inferred from TextureDesc; only
        // importSwapchainTexture can establish that native ownership fact.
        const auto requiredFinal = texture.externalFinalState;
        compiled.addExternalResource(ExternalResourceRecord{
            texture.resolvedDesc().label,
            ResourceKind::Texture,
            texture.handle.id,
            texture.resolvedState(),
            requiredFinal,
            true,
            texture.presentableExternal,
        });
    }

    std::pmr::map<uint32_t, ResourceLifetimeRecord> textureLifetimes;
    std::pmr::map<uint32_t, ResourceLifetimeRecord> bufferLifetimes;
    auto touchLifetime =
        [](ResourceLifetimeRecord& lifetime, const std::pmr::string& passName, size_t order) {
            if (lifetime.firstUsePassName.empty()) {
                lifetime.firstUsePassName = passName;
                lifetime.firstUseOrder = order;
            }
            lifetime.lastUsePassName = passName;
            lifetime.lastUseOrder = order;
        };
    for (size_t executionOrder = 0; executionOrder < compiled.executionOrder().size();
        ++executionOrder) {
        const auto compiledPassIndex = compiled.executionOrder()[executionOrder];
        const auto& passRecord =
            graph.passes_[compiled.passes()[compiledPassIndex].originalPassIndex];
        for (const auto& write : passRecord.writes) {
            auto& lifetime = textureLifetimes[write.handle.id];
            if (lifetime.resourceName.empty()) {
                lifetime.resourceName = graph.textureRecord(write.handle).resolvedDesc().label;
                lifetime.kind = ResourceKind::Texture;
                lifetime.handleId = write.handle.id;
            }
            if (lifetime.producerPassName.empty()) {
                lifetime.producerPassName = passRecord.name;
            }
            touchLifetime(lifetime, passRecord.name, executionOrder);
        }
        for (const auto& write : passRecord.bufferWrites) {
            auto& lifetime = bufferLifetimes[write.handle.id];
            if (lifetime.resourceName.empty()) {
                lifetime.resourceName =
                    graph.bufferRecord(write.handle).resolveBuffer()->desc().label;
                lifetime.kind = ResourceKind::Buffer;
                lifetime.handleId = write.handle.id;
            }
            if (lifetime.producerPassName.empty()) {
                lifetime.producerPassName = passRecord.name;
            }
            touchLifetime(lifetime, passRecord.name, executionOrder);
        }
        for (const auto& read : passRecord.reads) {
            auto& lifetime = textureLifetimes[read.handle.id];
            if (lifetime.resourceName.empty()) {
                lifetime.resourceName = graph.textureRecord(read.handle).resolvedDesc().label;
                lifetime.kind = ResourceKind::Texture;
                lifetime.handleId = read.handle.id;
            }
            touchLifetime(lifetime, passRecord.name, executionOrder);
            lifetime.consumerPassNames.push_back(passRecord.name);
        }
        for (const auto& read : passRecord.bufferReads) {
            auto& lifetime = bufferLifetimes[read.handle.id];
            if (lifetime.resourceName.empty()) {
                lifetime.resourceName =
                    graph.bufferRecord(read.handle).resolveBuffer()->desc().label;
                lifetime.kind = ResourceKind::Buffer;
                lifetime.handleId = read.handle.id;
            }
            touchLifetime(lifetime, passRecord.name, executionOrder);
            lifetime.consumerPassNames.push_back(passRecord.name);
        }
    }
    for (auto& item : textureLifetimes) {
        compiled.addResourceLifetime(std::move(item.second));
    }
    for (auto& item : bufferLifetimes) {
        compiled.addResourceLifetime(std::move(item.second));
    }

    // L2 decision gate: only compute alias groups when the backend's
    // capability plan reports features.transientAliasing=true. Metal reaches
    // this path only after its native MTLHeap placement/completion probes pass;
    // WebGPU remains degraded and receives an empty alias plan. The executor
    // therefore keeps pool allocation as the fallback path without a backend
    // branch. This mirrors how
    // lowerSubpassGraph checks features.subpassInputAttachment at compile
    // time rather than deferring the decision to execution. The env var
    // override is checked here too, so the compiled graph reflects the
    // debug decision (empty alias plan = pool allocation).
    const bool aliasingEnabled = graph.device_.capabilities().features.transientAliasing
        && std::getenv("WOCC_DISABLE_TRANSIENT_HEAP_ALIASING") == nullptr;

    if (aliasingEnabled) {
        struct AliasGroupState {
            uint32_t group = 0;
            std::tuple<uint32_t,
                uint32_t,
                uint32_t,
                rhi::TextureFormat,
                rhi::TextureUsage,
                rhi::MemoryUsage,
                uint32_t,
                uint32_t,
                uint32_t,
                bool,
                uint32_t,
                QueueRole,
                uint32_t>
                key;
            std::pmr::vector<std::pair<size_t, size_t>> lifetimes;
            std::pmr::vector<TextureHandle> members;
        };

        std::pmr::vector<AliasGroupState> aliasGroups;
        uint32_t nextAliasGroup = 1;
        auto textureQueue = [&](TextureHandle handle) {
            QueueRole queue = QueueRole::Graphics;
            bool found = false;
            bool crossesQueues = false;
            for (const auto compiledPassIndex : compiled.executionOrder()) {
                const auto& pass =
                    graph.passes_[compiled.passes()[compiledPassIndex].originalPassIndex];
                const auto usesHandle = [&](const auto& uses) {
                    return std::any_of(uses.begin(), uses.end(), [&](const auto& use) {
                        return use.handle.id == handle.id;
                    });
                };
                if (!usesHandle(pass.reads) && !usesHandle(pass.writes)) {
                    continue;
                }
                if (!found) {
                    queue = pass.queue;
                    found = true;
                } else if (queue != pass.queue) {
                    crossesQueues = true;
                }
            }
            return std::pair<QueueRole, bool>{queue, crossesQueues};
        };
        for (const auto& texture : graph.textures_) {
            if (!texture.transient || texture.imported) {
                continue;
            }
            const auto lifetimeIt = textureLifetimes.find(texture.handle.id);
            if (lifetimeIt == textureLifetimes.end()) {
                continue;
            }
            const auto& lifetime = lifetimeIt->second;
            // Phase 6: aliasScopeId is part of the key so Isolated-scope
            // subgraph transients only alias within their scope (non-zero
            // scopeId never matches the global pool's scopeId=0).
            const auto [queue, crossesQueues] = textureQueue(texture.handle);
            const auto key = std::make_tuple(texture.desc.extent.width,
                texture.desc.extent.height,
                texture.desc.extent.depth,
                texture.desc.format,
                texture.desc.usage,
                texture.desc.memory,
                texture.desc.mipLevelCount,
                texture.desc.arrayLayerCount,
                texture.desc.sampleCount,
                texture.desc.cubeCompatible,
                texture.aliasScopeId,
                queue,
                crossesQueues ? texture.handle.id : 0u);
            uint32_t group = 0;
            for (auto& candidate : aliasGroups) {
                if (candidate.key != key) {
                    continue;
                }
                bool overlaps = false;
                for (const auto& interval : candidate.lifetimes) {
                    if (!(lifetime.lastUseOrder < interval.first
                            || lifetime.firstUseOrder > interval.second)) {
                        overlaps = true;
                        break;
                    }
                }
                if (!overlaps) {
                    candidate.lifetimes.push_back({lifetime.firstUseOrder, lifetime.lastUseOrder});
                    candidate.members.push_back(texture.handle);
                    group = candidate.group;
                    break;
                }
            }
            if (group == 0) {
                group = nextAliasGroup++;
                aliasGroups.push_back(AliasGroupState{
                    group,
                    key,
                    {{lifetime.firstUseOrder, lifetime.lastUseOrder}},
                    {texture.handle},
                });
            }
            compiled.addAlias(AliasTraceRecord{
                texture.resolvedDesc().label,
                texture.handle,
                group,
            });
        }

        auto scopeForPass = [&](TextureHandle handle, size_t originalPassIndex) {
            rhi::semantics::ResourceAccessScope scope;
            for (const auto& use : compiled.passResourceUses()[originalPassIndex]) {
                if (use.kind != ResourceKind::Texture || use.handleId != handle.id)
                    continue;
                scope.state = use.state;
                scope.stages = scope.stages | use.stages;
                scope.access = scope.access | use.resourceAccess;
            }
            return scope;
        };
        for (const auto& group : aliasGroups) {
            if (group.members.size() < 2) {
                continue;
            }
            auto members = group.members;
            std::sort(members.begin(), members.end(), [&](TextureHandle lhs, TextureHandle rhs) {
                return textureLifetimes[lhs.id].firstUseOrder
                    < textureLifetimes[rhs.id].firstUseOrder;
            });
            for (size_t index = 1; index < members.size(); ++index) {
                const auto before = members[index - 1];
                const auto after = members[index];
                const auto& beforeLifetime = textureLifetimes[before.id];
                const auto& afterLifetime = textureLifetimes[after.id];
                const auto producerCompiledIndex =
                    compiled.executionOrder()[beforeLifetime.lastUseOrder];
                const auto consumerCompiledIndex =
                    compiled.executionOrder()[afterLifetime.firstUseOrder];
                const auto producerPass =
                    compiled.passes()[producerCompiledIndex].originalPassIndex;
                const auto consumerPass =
                    compiled.passes()[consumerCompiledIndex].originalPassIndex;
                const auto queue = graph.passes_[producerPass].queue;
                const auto beforeState = scopeForPass(before, producerPass);
                const auto afterState = scopeForPass(after, consumerPass);
                compiled.addAliasHandoff(AliasHandoffRecord{before,
                    after,
                    group.group,
                    producerPass,
                    consumerPass,
                    queue,
                    beforeState.stages,
                    afterState.stages,
                    beforeState.access,
                    afterState.access});
                compiled.addDependency(CompiledDependencyRecord{producerPass,
                    consumerPass,
                    graph.textureRecord(after).resolvedDesc().label,
                    ResourceKind::Texture,
                    TopologyDependencyKind::Waw,
                    false});
            }
        }
    }

    using Scope = rhi::semantics::ResourceAccessScope;
    core::pmr::Map<std::uint32_t, Scope> textureHistory, bufferHistory;
    core::pmr::Map<std::uint32_t, Scope> textureAllUses, bufferAllUses;
    core::pmr::Set<std::pair<ResourceKind, std::uint32_t>> seenResources;
    for (const auto& texture : graph.textures_) {
        // A pooled owner keeps its original recorded state. Graph ownership
        // does not imply a newly allocated or discarded native resource.
        const auto state =
            texture.resolveTexture() ? texture.resolvedState() : rhi::ResourceState::Undefined;
        textureHistory.emplace(texture.handle.id, Scope{state, stageFor(state), accessFor(state)});
    }
    for (const auto& buffer : graph.buffers_) {
        const auto state = buffer.resolveBuffer()->state();
        const auto initial = state == rhi::ResourceState::ShaderRead
            ? rhi::semantics::ResourceAccessUsageValidation::conservativeBufferReadScope(
                  buffer.resolveBuffer()->desc())
            : Scope{state, stageFor(state), accessFor(state)};
        bufferHistory.emplace(buffer.handle.id, initial);
    }
    // Precompute conservative scopes for replay across frame boundaries. These
    // are metadata only; the encoder still orders only preceding GPU work.
    for (const auto passIndex : compiled.executionOrder()) {
        for (const auto& use : compiled.passResourceUses()[passIndex]) {
            auto& all =
                (use.kind == ResourceKind::Texture ? textureAllUses : bufferAllUses)[use.handleId];
            all.stages = all.stages | use.stages;
            all.access = all.access | use.resourceAccess;
        }
    }
    auto appendDependency = [&](core::pmr::Vector<BarrierTraceRecord>& batch,
                                const core::pmr::String& passName,
                                ResourceKind kind,
                                std::uint32_t handle,
                                Scope next) {
        auto& previous =
            (kind == ResourceKind::Texture ? textureHistory : bufferHistory).at(handle);
        const auto& all = (kind == ResourceKind::Texture ? textureAllUses : bufferAllUses)[handle];
        const bool firstUse = seenResources.emplace(kind, handle).second;
        if (firstUse || previous.state != next.state
            || rhi::semantics::resourceAccessWrites(previous.access)
            || rhi::semantics::resourceAccessWrites(next.access)) {
            auto barrier = kind == ResourceKind::Texture
                ? makeTextureBarrierTrace(passName,
                      graph.textureDesc(TextureHandle{handle}),
                      TextureHandle{handle},
                      previous.state,
                      next.state)
                : makeBufferBarrierTrace(passName,
                      *graph.bufferRecord(BufferHandle{handle}).resolveBuffer(),
                      BufferHandle{handle},
                      previous.state,
                      next.state);
            barrier.srcStage = previous.stages;
            barrier.srcAccess = previous.access;
            barrier.dstStage = next.stages;
            barrier.dstAccess = next.access;
            barrier.memoryOnly = previous.state == next.state;
            barrier.replaySourceStages = all.stages;
            barrier.replaySourceAccess = all.access;
            batch.push_back(std::move(barrier));
        }
        // State transitions consume the prior scope. Retain producer/readers
        // only within the same state; scopes from Undefined or an attachment
        // cannot describe a subsequent ShaderRead state's source access.
        if (previous.state != next.state)
            previous = next;
        else {
            previous.stages = previous.stages | next.stages;
            previous.access = previous.access | next.access;
        }
    };
    for (const auto& compiledPassIndex : compiled.executionOrder()) {
        const auto& compiledPass = compiled.passes()[compiledPassIndex];
        const auto& passRecord = graph.passes_[compiledPass.originalPassIndex];
        for (const auto& attachment : passRecord.attachmentWrites) {
            const auto& record = graph.textureRecord(attachment.handle);
            compiled.addLoadStore(AttachmentTraceRecord{
                passRecord.name,
                record.resolvedDesc().label,
                attachment.state,
                attachment.loadOp,
                attachment.storeOp,
                attachment.tileHints,
                attachment.clearColor,
                record.transient,
                record.imported,
                record.imported,
            });
        }
        PassBarrierBatch batch;
        batch.passOriginalIndex = static_cast<std::uint32_t>(compiledPass.originalPassIndex);
        core::pmr::Map<std::pair<ResourceKind, std::uint32_t>, Scope> passScopes;
        for (const auto& use : compiled.passResourceUses()[compiledPassIndex]) {
            const auto key = std::make_pair(use.kind, use.handleId);
            auto inserted =
                passScopes.emplace(key, Scope{use.state, use.stages, use.resourceAccess});
            if (!inserted.second) {
                auto& scope = inserted.first->second;
                if (scope.state != use.state) {
                    // Native subpass groups have a separate, validated internal
                    // attachment transition/dependency contract. The outer
                    // boundary uses the attachment state and unions all stages.
                    bool subpassAttachment = false;
                    if (passRecord.subpassGroup && use.kind == ResourceKind::Texture)
                        for (const auto& attachment : passRecord.attachmentWrites)
                            if (attachment.handle.id == use.handleId
                                && (scope.state == attachment.state
                                    || scope.state == rhi::ResourceState::ShaderRead)
                                && (use.state == attachment.state
                                    || use.state == rhi::ResourceState::ShaderRead)) {
                                scope.state = attachment.state;
                                subpassAttachment = true;
                                break;
                            }
                    if (!subpassAttachment)
                        throw std::runtime_error(core::toStdString(
                            core::pmr::String("RG-ACCESS-SAME-PASS-STATE-CONFLICT: ")
                            + passRecord.name));
                }
                scope.stages = scope.stages | use.stages;
                scope.access = scope.access | use.resourceAccess;
            }
        }
        for (const auto& item : passScopes)
            appendDependency(batch.prePassBarriers,
                passRecord.name,
                item.first.first,
                item.first.second,
                item.second);
        for (const auto& barrier : batch.prePassBarriers) {
            compiled.addBarrier(barrier);
        }
        compiled.addPassBarrierBatch(std::move(batch));
    }

    PassBarrierBatch frameEndBatch;
    frameEndBatch.passOriginalIndex = UINT32_MAX;
    const std::pmr::string frameEndPassName{"FrameEnd"};
    for (const auto& external : compiled.externalResources()) {
        if (external.requiredFinalState == rhi::ResourceState::Undefined) {
            continue;
        }
        auto& current = textureHistory.at(external.handleId).state;
        if (current == external.requiredFinalState) {
            continue;
        }
        appendDependency(frameEndBatch.prePassBarriers,
            frameEndPassName,
            ResourceKind::Texture,
            external.handleId,
            Scope{external.requiredFinalState,
                stageFor(external.requiredFinalState),
                accessFor(external.requiredFinalState)});
        compiled.addBarrier(frameEndBatch.prePassBarriers.back());
    }
    if (!frameEndBatch.prePassBarriers.empty()) {
        compiled.addPassBarrierBatch(std::move(frameEndBatch));
    }
    core::pmr::Map<std::uint32_t, std::uint32_t> aliasGroupByHandle;
    for (const auto& alias : compiled.aliasPlan()) {
        aliasGroupByHandle[alias.handle.id] = alias.aliasGroup;
    }
    core::pmr::Map<std::uint32_t, std::uint64_t> aliasGroupBytes;
    core::pmr::Map<std::uint32_t, std::uint64_t> aliasGroupAlignment;
    std::uint64_t transientPeakBytes = 0;
    std::uint32_t transientResourceCount = 0;
    for (const auto& texture : graph.textures_) {
        // reset() completion-gates every graph-owned texture, not only
        // explicitly transient ones.  Account for that exact ownership set
        // so cache eviction and resource telemetry remain bounded on all RHI
        // backends, including WebGPU where aliasing is degraded.
        if (texture.imported) {
            continue;
        }
        if (transientResourceCount == UINT32_MAX)
            throw core::DiagnosticError("RG-CACHE-RESOURCE-COUNT-OVERFLOW");
        ++transientResourceCount;
        const auto bytes = rhi::ResourceCacheAccounting::requireLogicalBytes(
            graph.device_.backend(), texture.desc);
        if (const auto* id = rhi::ResourceCacheAccounting::accumulate(transientPeakBytes, bytes))
            throw core::DiagnosticError(id);
        const auto aliasIt = aliasGroupByHandle.find(texture.handle.id);
        if (aliasIt != aliasGroupByHandle.end()) {
            const auto placement = graph.device_.queryTexturePlacementRequirements(texture.desc);
            if (!placement.sizeBytes || !core::isPowerOfTwoUnsigned(placement.alignmentBytes))
                throw core::DiagnosticError("RG-TEXTURE-PLACEMENT-REQUIREMENTS-UNAVAILABLE");
            aliasGroupBytes[aliasIt->second] =
                std::max(aliasGroupBytes[aliasIt->second], placement.sizeBytes);
            aliasGroupAlignment[aliasIt->second] =
                std::max(aliasGroupAlignment[aliasIt->second], placement.alignmentBytes);
        }
    }
    std::uint64_t transientPhysicalPeakBytes = 0u;
    for (const auto& item : aliasGroupBytes) {
        std::uint64_t alignedBytes = 0u;
        const auto alignment = aliasGroupAlignment[item.first];
        if (!core::roundUpUnsigned(item.second, alignment, alignedBytes)
            || !core::MemoryBudgetLedger::canAdd(transientPhysicalPeakBytes, alignedBytes))
            throw core::DiagnosticError("RG-ALIAS-PHYSICAL-BUDGET-OVERFLOW");
        transientPhysicalPeakBytes += alignedBytes;
    }
    for (const auto& buffer : graph.buffers_) {
        if (buffer.buffer) {
            if (transientResourceCount == UINT32_MAX)
                throw core::DiagnosticError("RG-CACHE-RESOURCE-COUNT-OVERFLOW");
            ++transientResourceCount;
            const auto bytes = rhi::ResourceCacheAccounting::requireLogicalBytes(
                graph.device_.backend(), buffer.resolveBuffer()->desc());
            if (const auto* id =
                    rhi::ResourceCacheAccounting::accumulate(transientPeakBytes, bytes))
                throw core::DiagnosticError(id);
        }
    }
    compiled.setTransientResourceBudget(
        transientPeakBytes, transientResourceCount, transientPhysicalPeakBytes);
    return compiled;
}

FrameGraphExecutor::FrameGraphExecutor(FrameGraph& graph) : graph_(graph)
{}

void FrameGraph::setGpuTimingPassScopes(std::pmr::vector<GpuTimingPassScope> scopes)
{
    invalidateCompiledRetirementSource();
    gpuTimingPassScopes_ = std::move(scopes);
}

void FrameGraphExecutor::execute(const CompiledFrameGraph& compiled)
{
    if (graph_.orderedExecution_)
        throw core::DiagnosticError("RG-ORDERED-ISSUER-ENTRY-REQUIRED");
    executeImpl(compiled, {}, nullptr);
}

void FrameGraphExecutor::executeConformance(const CompiledFrameGraph& compiled,
    rhi::OriginalConformanceClearExecution& execution,
    rhi::GpuTexture& target)
{
    if (const auto* id = execution.diagnostic(graph_.device_))
        throw core::DiagnosticError(id);
    const char* id = nullptr;
    auto encoder = execution.prepare(compiled.commandEncoderDesc(), target, id);
    if (!encoder)
        throw core::DiagnosticError(id ? id : "RG-CONFORMANCE-CLEAR-ENCODER");
    executeImpl(compiled, std::move(encoder), nullptr, nullptr, &execution);
}

void FrameGraphExecutor::executeImpl(const CompiledFrameGraph& compiled,
    core::UniquePtr<rhi::CommandEncoder> commandEncoder,
    rhi::OriginalOrderedGraphicsExecution* ordered,
    OrderedGraphicsMeasurements* measurements,
    rhi::OriginalConformanceClearExecution* conformance)
{
    core::CpuWorkMeasurementScope recordMeasurement(measurements ? &measurements->record : nullptr);
    if (const auto* id = graph_.compiledRetirementExecutionDiagnostic(compiled))
        throw core::DiagnosticError(id);
    const bool cpuDiag = std::getenv("ARP_FRAME_GRAPH_CPU_DIAG") != nullptr;
    const auto cpuStart = std::chrono::steady_clock::now();
    if (!commandEncoder)
        commandEncoder = graph_.device_.createCommandEncoder(compiled.commandEncoderDesc());
    if (!commandEncoder) {
        throw std::runtime_error("frame graph RHI command encoder creation failed");
    }

    auto& commands = *commandEncoder;
    std::pmr::vector<rhi::semantics::GpuTimingScopeRequest> timingRequests;
    for (const auto& requested : graph_.gpuTimingPassScopes_) {
        if (requested.scopeId == 0u)
            continue;
        for (const auto compiledPassIndex : compiled.executionOrder()) {
            const auto& compiledPass = compiled.passes()[compiledPassIndex];
            const auto& passRecord = graph_.passes_[compiledPass.originalPassIndex];
            if (!compiledPass.culled
                && ((compiledPass.kind == PassKind::Graphics
                        && (passRecord.renderPassCallback || passRecord.subpassGroup))
                    || (compiledPass.kind == PassKind::Compute && requested.callbackOwnsTimestamps))
                && compiledPass.name == requested.passName) {
                if (timingRequests.size() < rhi::GpuTimingSample::kMaxScopes - 1u) {
                    timingRequests.push_back({requested.scopeId, requested.passName, false});
                }
                break;
            }
        }
    }
    const auto timingPlan = rhi::semantics::lowerGpuTimingScopes(
        graph_.device_.backend(), graph_.timestampQueriesSupported_, timingRequests);
    if (std::getenv("ARP_FRAME_GRAPH_GPU_TIMING_DIAG") != nullptr) {
        std::cerr << "[frame-graph-gpu-timing] requested=" << graph_.gpuTimingPassScopes_.size()
                  << " matched=" << timingRequests.size()
                  << " outcome=" << static_cast<unsigned>(timingPlan.outcome)
                  << " queries=" << timingPlan.queryCount << '\n';
    }
    arp::core::UniquePtr<rhi::QueryPool> timingPool;
    if (timingPlan.outcome == rhi::semantics::GpuTimingLoweringOutcome::Native
        && rhi::semantics::gpuTimingPlanValid(timingPlan)) {
        std::vector<std::uint64_t> scopeIds;
        scopeIds.reserve(timingPlan.slots.size());
        for (const auto& slot : timingPlan.slots)
            scopeIds.push_back(slot.scopeId);
        timingPool = graph_.device_.createQueryPool({
            "FrameGraphPassGpuTiming",
            rhi::QueryType::Timestamp,
            timingPlan.queryCount,
            scopeIds.data(),
            static_cast<std::uint32_t>(scopeIds.size()),
        });
        if (std::getenv("ARP_FRAME_GRAPH_GPU_TIMING_DIAG") != nullptr) {
            std::cerr << "[frame-graph-gpu-timing] pool="
                      << (timingPool ? "created" : "unavailable") << '\n';
        }
    }
    const auto encoderReady = std::chrono::steady_clock::now();
    const auto timingSlotFor =
        [&](const std::pmr::string& passName) -> const rhi::semantics::GpuTimingScopeSlot* {
        if (!timingPool)
            return nullptr;
        for (std::size_t i = 0; i < timingPlan.scopes.size(); ++i) {
            if (timingPlan.scopes[i].label == passName)
                return &timingPlan.slots[i];
        }
        return nullptr;
    };
    RenderContext context(graph_.device_, commands);
    context.graph_ = &graph_;
    const bool firstCompiledExecution =
        !graph_.hasExecutedCompiledGraph_ || graph_.executedCompiledGraph_ != &compiled;
    graph_.reuseCompiledTrace_ = !firstCompiledExecution;
    graph_.resourceTraceCursor_ = 0u;
    graph_.attachmentTraceCursor_ = 0u;
    graph_.barrierTraceCursor_ = 0u;
    if (firstCompiledExecution) {
        graph_.executionTrace_.clear();
        graph_.resourceTrace_.clear();
        graph_.attachmentTrace_.clear();
        graph_.barrierTrace_.clear();
    }
    if (firstCompiledExecution) {
        graph_.passTrace_.clear();
        graph_.aliasTrace_.clear();
        graph_.buildAliasTrace(compiled);
    }
    graph_.markerTrace_.clear();
    // Phase 6: subgraph boundary markers. Track the active subgraph across
    // execution order; push a debug group on entry, pop on exit. Culled-as-
    // unit subgraphs are skipped (no passes execute, no marker). Markers are
    // scope-balanced: every push has a matching pop.
    const CompiledSubgraph* activeSubgraph = nullptr;
    auto findSubgraphForPass = [&](size_t originalIndex) -> const CompiledSubgraph* {
        for (const auto& sg : compiled.subgraphs()) {
            if (sg.culledAsUnit) {
                continue;
            }
            if (originalIndex >= sg.globalPassBegin && originalIndex < sg.globalPassEnd) {
                return &sg;
            }
        }
        return nullptr;
    };

    if (firstCompiledExecution) {
        for (const auto& compiledPass : compiled.passes()) {
            const auto& passRecord = graph_.passes_[compiledPass.originalPassIndex];
            if (!passRecord.callback && !passRecord.renderPassCallback
                && !passRecord.subpassGroup) {
                throw std::runtime_error(arp::core::toStdString(
                    std::pmr::string("frame graph pass has no execute callback: ")
                    + passRecord.name));
            }

            graph_.passTrace_.push_back(PassTraceRecord{
                compiledPass.name,
                compiledPass.kind,
                compiledPass.queue,
                compiledPass.asyncComputeEligible,
                compiledPass.culled,
            });
        }
    }
    const auto traceReady = std::chrono::steady_clock::now();
    for (const auto compiledPassIndex : compiled.executionOrder()) {
        const auto& compiledPass = compiled.passes()[compiledPassIndex];
        if (compiledPass.culled) {
            continue;
        }
        const auto passCpuStart =
            cpuDiag ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};

        // Phase 6: subgraph boundary marker. Push/pop debug group when
        // crossing into or out of a subgraph's pass range.
        const auto* passSubgraph = findSubgraphForPass(compiledPass.originalPassIndex);
        if (passSubgraph != activeSubgraph) {
            if (activeSubgraph != nullptr) {
                commands.popDebugGroup();
                graph_.markerTrace_.push_back(std::pmr::string("pop:") + activeSubgraph->name);
                activeSubgraph = nullptr;
            }
            if (passSubgraph != nullptr) {
                commands.pushDebugGroup(passSubgraph->name);
                graph_.markerTrace_.push_back(std::pmr::string("push:") + passSubgraph->name);
                activeSubgraph = passSubgraph;
            }
        }

        const auto& passRecord = graph_.passes_[compiledPass.originalPassIndex];
        context.passUses_ =
            passRecord.subpassGroup ? nullptr : &compiled.passResourceUses()[compiledPassIndex];
        const auto* timingSlot = timingSlotFor(passRecord.name);
        graph_.executionTrace_.push_back(passRecord.name);
        graph_.replayPassBarriers(
            commands, compiled, static_cast<std::uint32_t>(compiledPass.originalPassIndex));
        graph_.recordAttachmentTrace(passRecord);
        if (passRecord.subpassGroup) {
            // --- Multi-subpass execution (native path) ---
            const auto& sg = *passRecord.subpassGroup;
            auto desc = graph_.renderPassDescFor(passRecord);
            desc.subpasses = sg.subpassDescs;
            desc.subpassDependencies = sg.subpassDependencies;
            auto renderPass = commands.beginRenderPass(desc);
            if (!renderPass) {
                throw std::runtime_error(arp::core::toStdString(
                    std::pmr::string("frame graph subpass render pass begin failed: ")
                    + commands.lastValidation().diagnostics));
            }
            if (timingSlot != nullptr
                && !renderPass->writeTimestamp(*timingPool, timingSlot->beginQuery).success) {
                renderPass->end();
                throw std::runtime_error("frame graph subpass timing begin failed");
            }
            for (std::uint32_t sp = 0; sp < sg.subpasses.size(); ++sp) {
                if (sp > 0) {
                    const auto advanced = renderPass->nextSubpass();
                    if (!advanced.success) {
                        renderPass->end();
                        throw std::runtime_error(arp::core::toStdString(
                            std::pmr::string("frame graph nextSubpass failed: ")
                            + advanced.diagnostics));
                    }
                }
                if (sg.subpasses[sp].callback) {
                    RenderPassContext renderPassContext(
                        context, *renderPass, &sg.subpasses[sp].subpassInputBindings);
                    try {
                        sg.subpasses[sp].callback(renderPassContext);
                    } catch (...) {
                        renderPass->end();
                        throw;
                    }
                }
            }
            if (timingSlot != nullptr
                && !renderPass->writeTimestamp(*timingPool, timingSlot->endQuery).success) {
                renderPass->end();
                throw std::runtime_error("frame graph subpass timing end failed");
            }
            const auto ended = renderPass->end();
            if (!ended.success) {
                throw std::runtime_error(arp::core::toStdString(
                    std::pmr::string("frame graph subpass render pass end failed: ")
                    + ended.diagnostics));
            }
        } else if (passRecord.renderPassCallback) {
            auto renderPassDescFor = [this](const FrameGraph::PassRecord& passRecord) {
                return graph_.renderPassDescFor(passRecord);
            };
            auto renderPass = commands.beginRenderPass(renderPassDescFor(passRecord));
            if (!renderPass) {
                throw std::runtime_error(arp::core::toStdString(
                    std::pmr::string("frame graph render pass begin failed [") + passRecord.name
                    + "]: " + commands.lastValidation().diagnostics));
            }
            if (timingSlot != nullptr
                && !renderPass->writeTimestamp(*timingPool, timingSlot->beginQuery).success) {
                renderPass->end();
                throw std::runtime_error("frame graph render-pass timing begin failed");
            }
            RenderPassContext renderPassContext(
                context, *renderPass, &passRecord.subpassInputBindings);
            try {
                passRecord.renderPassCallback(renderPassContext);
            } catch (...) {
                renderPass->end();
                throw;
            }
            if (timingSlot != nullptr
                && !renderPass->writeTimestamp(*timingPool, timingSlot->endQuery).success) {
                renderPass->end();
                throw std::runtime_error("frame graph render-pass timing end failed");
            }
            const auto ended = renderPass->end();
            if (!ended.success) {
                throw std::runtime_error(arp::core::toStdString(
                    std::pmr::string("frame graph render pass end failed: ") + ended.diagnostics));
            }
        } else {
            context.setGpuTimingScope(timingSlot != nullptr ? timingPool.get() : nullptr,
                timingSlot != nullptr ? timingSlot->beginQuery : 0u,
                timingSlot != nullptr ? timingSlot->endQuery : 0u);
            passRecord.callback(context);
            context.setGpuTimingScope(nullptr, 0u, 0u);
        }
        if (cpuDiag) {
            std::cerr << "[frame-graph-cpu-pass] " << passRecord.name << "="
                      << std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - passCpuStart)
                             .count()
                      << '\n';
        }
    }

    const auto passExecutionReady = std::chrono::steady_clock::now();
    // Phase 6: pop any subgraph still active at frame end.
    if (activeSubgraph != nullptr) {
        commands.popDebugGroup();
        graph_.markerTrace_.push_back(std::pmr::string("pop:") + activeSubgraph->name);
        activeSubgraph = nullptr;
    }

    graph_.replayPassBarriers(commands, compiled, UINT32_MAX);
    auto commandBuffer = commands.finish();
    if (!commandBuffer) {
        throw std::runtime_error(arp::core::toStdString(
            std::pmr::string("frame graph RHI command encoder finish failed: ")
            + commands.lastValidation().diagnostics));
    }
    const auto commandFinished = std::chrono::steady_clock::now();
    // Keep every transient resource alive until the queue has consumed this
    // command buffer. WebGPU command submission is asynchronous; releasing a
    // texture directly from FrameGraph::~FrameGraph() would let the next
    // frame reuse or destroy it while the browser still owns the work.
    bool issuerOwnedColdCompletion = false;
    if (conformance) {
        switch (conformance->submissionCompletion()) {
        case rhi::ConformanceSubmissionCompletion::ExternalFence:
            break;
        case rhi::ConformanceSubmissionCompletion::IssuerOwnedColdObservation:
            issuerOwnedColdCompletion = true;
            for (const auto passIndex : compiled.executionOrder())
                if (!compiled.passes()[passIndex].culled
                    && graph_.passes_[compiled.passes()[passIndex].originalPassIndex]
                        .submissionObserver)
                    throw core::DiagnosticError(
                        "RG-CONFORMANCE-COLD-COMPLETION-OBSERVER-UNSUPPORTED");
            break;
        default:
            throw core::DiagnosticError("RG-CONFORMANCE-COMPLETION-MODE-UNKNOWN");
        }
    }
    const bool requiresFence = !ordered && !issuerOwnedColdCompletion;
    auto ownedSubmissionFence =
        requiresFence ? graph_.device_.createFence() : core::UniquePtr<rhi::GpuFence>{};
    if (requiresFence && !ownedSubmissionFence) {
        throw std::runtime_error("RG-COMPLETION-FENCE-CREATE-FAILED");
    }
    // Prepare the control block before queue acceptance. Aliasing keeps the
    // core-owned deleter and never allocates after work becomes irrevocable.
    std::shared_ptr<rhi::GpuFence> submissionFence;
    if (ownedSubmissionFence) {
        auto fenceOwner =
            core::makeShared<core::UniquePtr<rhi::GpuFence>>(std::move(ownedSubmissionFence));
        submissionFence = {fenceOwner, fenceOwner->get()};
    }
    const auto fenceCreated = std::chrono::steady_clock::now();
    graph_.validateSubmissionObservers(compiled);
    recordMeasurement.finish();
    core::CpuWorkMeasurementScope submitMeasurement(measurements ? &measurements->submit : nullptr);
    const auto submitted = ordered ? ordered->submit(*commandBuffer)
        : conformance              ? conformance->submit(*commandBuffer, submissionFence.get())
                      : graph_.device_.queue().submit(*commandBuffer, submissionFence.get());
    submitMeasurement.finish();
    if (submitted.hasAcceptedWork()) {
        graph_.submissionFence_ = std::move(submissionFence);
        graph_.lastExecutionSubmitted_ = true;
        const std::shared_ptr<const rhi::GpuFence> completion = graph_.submissionFence_;
        for (const auto compiledPassIndex : compiled.executionOrder()) {
            const auto& pass = compiled.passes()[compiledPassIndex];
            if (!pass.culled) {
                const auto& record = graph_.passes_[pass.originalPassIndex];
                if (record.submissionObserver)
                    record.submissionObserver->onSubmitted(record.submissionToken, completion);
            }
        }
    }
    if (!submitted.success) {
        throw std::runtime_error(arp::core::toStdString(
            std::pmr::string("frame graph RHI queue submit failed: ") + submitted.diagnostics));
    }
    if (cpuDiag) {
        const auto elapsedMs = [](const auto begin, const auto end) {
            return std::chrono::duration<double, std::milli>(end - begin).count();
        };
        std::cerr << "[frame-graph-cpu] encoder-and-timing=" << elapsedMs(cpuStart, encoderReady)
                  << " trace-setup=" << elapsedMs(encoderReady, traceReady)
                  << " pass-execution=" << elapsedMs(traceReady, passExecutionReady)
                  << " command-finish=" << elapsedMs(passExecutionReady, commandFinished)
                  << " fence-create=" << elapsedMs(commandFinished, fenceCreated)
                  << " queue-submit=" << elapsedMs(cpuStart, std::chrono::steady_clock::now())
                  << '\n';
    }
}

void FrameGraph::execute()
{
    lastExecutionSubmitted_ = false;
    auto compiled = compile();
    execute(compiled);
    // The compiled object is local to this convenience path. Do not retain
    // its address as a repeat-execution identity after it goes out of scope.
    executedCompiledGraph_ = nullptr;
    reuseCompiledTrace_ = false;
}

void FrameGraph::execute(const CompiledFrameGraph& compiled)
{
    executeNative(compiled, nullptr, nullptr);
}

void FrameGraph::executeConformance(
    rhi::OriginalConformanceClearExecution& execution, TextureHandle target)
{
    auto compiled = compile();
    executeNative(compiled, &execution, &texture(target));
    executedCompiledGraph_ = nullptr;
    reuseCompiledTrace_ = false;
}

void FrameGraph::executeNative(const CompiledFrameGraph& compiled,
    rhi::OriginalConformanceClearExecution* conformance,
    rhi::GpuTexture* target)
{
    lastExecutionSubmitted_ = false;
    if (orderedExecution_)
        throw core::DiagnosticError("RG-ORDERED-ISSUER-ENTRY-REQUIRED");
    if (const auto* id = compiledRetirementExecutionDiagnostic(compiled))
        throw core::DiagnosticError(id);
    validateSubmissionObservers(compiled);
    const bool cpuDiag = std::getenv("ARP_FRAME_GRAPH_CPU_DIAG") != nullptr;
    const bool retirementDiag = std::getenv("ARP_FRAME_GRAPH_RETIREMENT_DIAG") != nullptr;
    const auto outerStart = std::chrono::steady_clock::now();
    const auto poolBeforeRecycle =
        retirementDiag ? resourcePool_->report() : FrameGraphResourcePoolReport{};
    resourcePool_->recycleCompleted();
    const auto recycled = std::chrono::steady_clock::now();
    if (retirementDiag) {
        const auto poolAfterRecycle = resourcePool_->report();
        std::cerr << "[rg-retirement] pool=" << &resourcePool()
                  << " before-pending=" << poolBeforeRecycle.pendingBytes
                  << " before-cached=" << poolBeforeRecycle.cachedBytes
                  << " before-resources=" << poolBeforeRecycle.pendingResourceCount
                  << " after-pending=" << poolAfterRecycle.pendingBytes
                  << " after-cached=" << poolAfterRecycle.cachedBytes
                  << " after-resources=" << poolAfterRecycle.pendingResourceCount
                  << " budget=" << poolAfterRecycle.byteBudget << '\n';
    }
    resourcePool_->configureForCompiledGraph(
        compiled.transientPeakBytes(), compiled.transientResourceCount());
    const auto configured = std::chrono::steady_clock::now();
    allocateTransientTextures(compiled);
    const auto allocated = std::chrono::steady_clock::now();
    prepareSubpassInputViews();
    const auto viewsPrepared = std::chrono::steady_clock::now();
    FrameGraphExecutor executor(*this);
    if (conformance)
        executor.executeConformance(compiled, *conformance, *target);
    else
        executor.execute(compiled);
    const auto executed = std::chrono::steady_clock::now();
    hasExecutedCompiledGraph_ = true;
    executedCompiledGraph_ = &compiled;
    if (cpuDiag) {
        const auto elapsedMs = [](const auto begin, const auto end) {
            return std::chrono::duration<double, std::milli>(end - begin).count();
        };
        std::cerr << "[frame-graph-cpu-outer] recycle=" << elapsedMs(outerStart, recycled)
                  << " configure=" << elapsedMs(recycled, configured)
                  << " allocate=" << elapsedMs(configured, allocated)
                  << " views=" << elapsedMs(allocated, viewsPrepared)
                  << " executor=" << elapsedMs(viewsPrepared, executed)
                  << " total=" << elapsedMs(outerStart, executed) << '\n';
    }
}

void FrameGraph::validateSubmissionObservers(const CompiledFrameGraph& compiled) const
{
    for (const auto index : compiled.executionOrder()) {
        const auto& pass = compiled.passes()[index];
        if (pass.culled)
            continue;
        const auto& record = passes_[pass.originalPassIndex];
        if (record.submissionObserver) {
            if (record.submissionToken.ownerId != 0u || record.submissionToken.generation != 0u) {
                for (const auto earlierIndex : compiled.executionOrder()) {
                    if (earlierIndex == index)
                        break;
                    const auto& earlierPass = compiled.passes()[earlierIndex];
                    if (earlierPass.culled)
                        continue;
                    const auto& earlier = passes_[earlierPass.originalPassIndex];
                    if (earlier.submissionObserver == record.submissionObserver
                        && earlier.submissionToken.ownerId == record.submissionToken.ownerId
                        && earlier.submissionToken.generation == record.submissionToken.generation)
                        throw core::DiagnosticError("RG-SUBMISSION-TOKEN-DUPLICATE");
                }
            }
            if (const auto* diagnostic =
                    record.submissionObserver->validateSubmission(record.submissionToken)) {
                throw core::DiagnosticError(diagnostic);
            }
        }
    }
}

void FrameGraph::prepareSubpassInputViews()
{
    for (auto& lowering : subpassInputLowerings_) {
        const auto& record = textureRecord(lowering.texture);
        auto* sourceTexture = record.resolveTexture();
        if (!sourceTexture) {
            throw std::runtime_error(arp::core::toStdString(
                std::pmr::string("subpass input attachment texture is not allocated: ")
                + record.desc.label));
        }
        if (lowering.textureView.isValid()) {
            if (lowering.sourceTexture != sourceTexture->id()) {
                throw std::runtime_error(arp::core::toStdString(
                    std::pmr::string(
                        "subpass input texture identity changed without view rebuild: ")
                    + record.desc.label));
            }
            continue;
        }

        const auto requiresSampledView =
            lowering.physicalBinding == rhi::semantics::SubpassInputPhysicalBinding::SampledTexture;
        const auto requiredUsage =
            requiresSampledView ? rhi::TextureUsage::Sampled : rhi::TextureUsage::RenderAttachment;
        if ((static_cast<std::uint32_t>(record.desc.usage & requiredUsage)
                != static_cast<std::uint32_t>(requiredUsage))) {
            throw std::runtime_error(arp::core::toStdString(
                std::pmr::string("subpass input attachment texture lacks required view usage: ")
                + record.desc.label));
        }

        const auto range = rhi::defaultTextureSubresourceRange(record.desc);
        auto view = device_.createTextureView(*sourceTexture,
            {
                record.desc.label + std::pmr::string("SubpassInputView"),
                record.desc.format,
                requiredUsage,
                rhi::TextureViewDimension::Texture2D,
                range.baseMipLevel,
                range.mipLevelCount,
                range.baseArrayLayer,
                range.arrayLayerCount,
                range.aspect,
            });
        if (!view || view->textureId() != sourceTexture->id()) {
            throw std::runtime_error(arp::core::toStdString(
                std::pmr::string("subpass input attachment view identity validation failed: ")
                + record.desc.label));
        }
        lowering.sourceTexture = sourceTexture->id();
        lowering.textureView = view->id();
        subpassInputViews_.push_back(std::move(view));
    }

    // Propagate the same materialized route into the executable pass records;
    // RenderPassContext must never reconstruct it from a texture name.
    for (auto& pass : passes_) {
        auto updateRoutes = [&](auto& routes) {
            for (auto& route : routes) {
                for (const auto& lowering : subpassInputLowerings_) {
                    if (route.loweringId
                        == static_cast<std::uint32_t>(&lowering - subpassInputLowerings_.data())) {
                        route.sourceTexture = lowering.sourceTexture;
                        route.textureView = lowering.textureView;
                        break;
                    }
                }
            }
        };
        if (pass.subpassGroup) {
            for (auto& subpass : pass.subpassGroup->subpasses) {
                updateRoutes(subpass.subpassInputBindings);
            }
        }
        updateRoutes(pass.subpassInputBindings);
    }
}

void FrameGraph::allocateTransientTextures(const CompiledFrameGraph& compiled)
{
    // A compiled graph may be executed repeatedly (for example, a cached
    // ping-pong topology). Once every transient texture has been materialized,
    // the alias allocator must not be rerun with an empty pending set; doing
    // so would discard the previous lifetime proof and reject a valid repeat
    // execution.
    bool hasPendingTransient = false;
    for (const auto& record : textures_) {
        if (record.transient && !record.imported && !record.texture && !record.textureRef) {
            hasPendingTransient = true;
            break;
        }
    }
    if (!hasPendingTransient) {
        return;
    }

    const auto& aliasPlan = compiled.aliasPlan();

    // No alias groups -> pool allocation. This is the path taken by degraded
    // WebGPU, by Metal when its native aliasing contract is unavailable, and
    // by graphs with no aliasable transients.
    if (aliasPlan.empty()) {
        for (auto& record : textures_) {
            if (!record.transient || record.imported) {
                continue;
            }
            if (record.texture || record.textureRef) {
                continue;
            }
            auto backendDesc = record.desc;
            backendDesc.initialState = rhi::ResourceState::Undefined;
            record.texture = resourcePool_->acquireTexture(backendDesc);
            if (!record.texture) {
                throw std::runtime_error(arp::core::toStdString(
                    std::pmr::string("frame graph transient texture allocation failed: ")
                    + record.desc.label));
            }
        }
        return;
    }

    // Build handleId -> record* lookup and compute per-texture size info.
    std::pmr::map<std::uint32_t, TextureRecord*> recordByHandleId;
    std::pmr::vector<TransientTextureSizeInfo> sizeInfos;

    for (auto& record : textures_) {
        if (!record.transient || record.imported) {
            continue;
        }
        if (record.texture || record.textureRef) {
            continue;
        }
        recordByHandleId[record.handle.id] = &record;

        // P1-A: query placement requirements from the backend. Backends with
        // a native query (Vulkan vkGetImageMemoryRequirements2, Metal
        // heapTextureSizeAndAlignWithDescriptor) override
        // GpuDevice::queryTexturePlacementRequirements for precise row pitch,
        // compression block size and backend alignment; the default L2
        // estimate (mip chain + array + cube + MSAA) is used by Null, which
        // is the only non-native-query backend that still reaches this alias
        // path. Capability-gated Metal placement uses its native requirements;
        // WebGPU and any Metal device without the native contract stay on the
        // non-aliasing allocation path.
        const auto placement = device_.queryTexturePlacementRequirements(record.desc);
        if (!placement.sizeBytes || !core::isPowerOfTwoUnsigned(placement.alignmentBytes))
            throw core::DiagnosticError("RG-TEXTURE-PLACEMENT-REQUIREMENTS-UNAVAILABLE");
        TransientTextureSizeInfo info;
        info.handleId = record.handle.id;
        info.sizeBytes = placement.sizeBytes;
        info.alignmentBytes = placement.alignmentBytes;
        sizeInfos.push_back(info);
    }

    // Run the transient resource allocator with full size metadata.
    // Throws on incompatible packing (Phase 3 diagnostic). We reach this
    // path only when aliasingSupported is true (capability-gated above),
    // so the allocator is told the backend can actually share memory.
    FrameGraphTransientResourceAllocator allocator;
    allocator.setAliasedMemoryCapability(true);
    const auto report = allocator.allocate(compiled, sizeInfos);
    if (!report.success) {
        throw std::runtime_error(arp::core::toStdString(
            std::pmr::string("transient allocation failed: ") + report.diagnostics));
    }
    if (!report.lifetimeProofValidated || !report.sizeMetadataValidated
        || !report.budgetProofValidated
        || (report.aliasedMemory && !report.completionGatedRetirementRequired)) {
        throw std::runtime_error("RG-ALIAS-EXECUTION-PROOF-INCOMPLETE");
    }

    FrameGraphAliasedResourceLayout aliasLayout;
    core::pmr::Vector<TextureRecord*> placedRecords(&core::defaultCpuMemoryResource());
    for (std::uint32_t heapIndex = 0u; heapIndex < report.physicalAllocations.size(); ++heapIndex) {
        const auto& physAlloc = report.physicalAllocations[heapIndex];
        aliasLayout.heapSizes.push_back(physAlloc.sizeBytes);
        aliasLayout.heapAlignments.push_back(physAlloc.alignmentBytes);
        for (const auto& member : physAlloc.members) {
            auto recordIt = recordByHandleId.find(member.handleId);
            if (recordIt == recordByHandleId.end()) {
                throw std::runtime_error("RG-ALIAS-PLACED-TEXTURE-RECORD-MISSING");
            }
            auto desc = recordIt->second->desc;
            desc.initialState = rhi::ResourceState::Undefined;
            aliasLayout.textureDescs.push_back(std::move(desc));
            aliasLayout.textureHeapIndices.push_back(heapIndex);
            aliasLayout.textureOffsets.push_back(member.offsetBytes);
            placedRecords.push_back(recordIt->second);
        }
    }

    FrameGraphAliasedResourceGroup aliasGroup;
    const bool reusedAliasGroup = resourcePool_->acquireAliasedGroup(aliasLayout, aliasGroup);
    if (reusedAliasGroup) {
        if (aliasGroup.heaps.size() != report.physicalAllocations.size()
            || aliasGroup.textures.size() != placedRecords.size()) {
            throw std::runtime_error("RG-ALIAS-CACHED-GROUP-SHAPE-MISMATCH");
        }
        for (std::size_t index = 0; index < placedRecords.size(); ++index) {
            placedRecords[index]->texture = std::move(aliasGroup.textures[index]);
            placedRecords[index]->placed = true;
        }
        transientHeaps_ = std::move(aliasGroup.heaps);
        transientAliasedLayout_ = std::move(aliasLayout);
        transientAliasedTextureHandles_.clear();
        for (const auto* record : placedRecords)
            transientAliasedTextureHandles_.push_back(record->handle.id);
        return;
    }

    // Create a GpuMemoryHeap per physical allocation (one per alias group).
    // Null backend heaps are size-trackers with separate allocations per
    // member; Vulkan/Metal backends share native memory across members.
    std::pmr::map<std::uint32_t, rhi::GpuMemoryHeap*> heapByGroup;
    for (const auto& physAlloc : report.physicalAllocations) {
        if (physAlloc.sizeBytes == 0 || physAlloc.alignmentBytes == 0) {
            throw std::runtime_error("RG-ALIAS-ALLOCATION-METADATA-INVALID");
        }
        auto heap = device_.createMemoryHeap(
            physAlloc.sizeBytes, physAlloc.alignmentBytes, rhi::MemoryUsage::Transient);
        if (!heap) {
            throw std::runtime_error(arp::core::toStdString(
                std::pmr::string("RG-ALIAS-HEAP-CREATE-FAILED: group=")
                + std::pmr::string(std::to_string(physAlloc.aliasGroup))
                + std::pmr::string(" size=") + std::pmr::string(std::to_string(physAlloc.sizeBytes))
                + std::pmr::string(" alignment=")
                + std::pmr::string(std::to_string(physAlloc.alignmentBytes))
                + std::pmr::string(" diagnostic=")
                + std::pmr::string(device_.lastMemoryHeapCreationDiagnostic())));
        }
        heapByGroup[physAlloc.aliasGroup] = heap.get();
        aliasGroup.heaps.push_back(std::move(heap));
    }

    // Create placed textures from the heap for each alias-group member.
    // The heap outlives all placed textures; they are destroyed when the
    // heap is destroyed on reset().
    for (const auto& physAlloc : report.physicalAllocations) {
        auto heapIt = heapByGroup.find(physAlloc.aliasGroup);
        if (heapIt == heapByGroup.end()) {
            throw std::runtime_error(arp::core::toStdString(
                std::pmr::string("transient allocation heap missing for alias group ")
                + std::pmr::string(std::to_string(physAlloc.aliasGroup))));
        }
        auto* heap = heapIt->second;

        for (const auto& member : physAlloc.members) {
            auto recordIt = recordByHandleId.find(member.handleId);
            if (recordIt == recordByHandleId.end()) {
                throw std::runtime_error(
                    arp::core::toStdString(std::pmr::string("transient allocation member handle ")
                        + std::pmr::string(std::to_string(member.handleId))
                        + std::pmr::string(" not found in texture records")));
            }
            auto* record = recordIt->second;

            auto backendDesc = record->desc;
            backendDesc.initialState = rhi::ResourceState::Undefined;
            auto texture = device_.createPlacedTexture(backendDesc, *heap, member.offsetBytes);
            if (!texture) {
                throw std::runtime_error(arp::core::toStdString(
                    std::pmr::string("RG-ALIAS-PLACED-TEXTURE-CREATE-FAILED: ")
                    + record->desc.label));
            }
            record->texture = std::move(texture);
            record->placed = true;
            aliasGroup.textures.push_back(std::move(record->texture));
        }
    }
    for (std::size_t index = 0; index < placedRecords.size(); ++index) {
        placedRecords[index]->texture = std::move(aliasGroup.textures[index]);
    }
    transientHeaps_ = std::move(aliasGroup.heaps);
    transientAliasedLayout_ = std::move(aliasLayout);
    transientAliasedTextureHandles_.clear();
    for (const auto* record : placedRecords)
        transientAliasedTextureHandles_.push_back(record->handle.id);
}

void FrameGraph::reset()
{
    invalidateCompiledRetirementSource();
    lastExecutionSubmitted_ = false;
    hasExecutedCompiledGraph_ = false;
    executedCompiledGraph_ = nullptr;
    reuseCompiledTrace_ = false;
    resourceTraceCursor_ = 0u;
    attachmentTraceCursor_ = 0u;
    barrierTraceCursor_ = 0u;
    passes_.clear();
    executionTrace_.clear();
    resourceTrace_.clear();
    passTrace_.clear();
    attachmentTrace_.clear();
    aliasTrace_.clear();
    barrierTrace_.clear();
    subpassInputLowerings_.clear();
    subpassInputViews_.clear();

    // Resource records and lowered pass records are frame-local. Keep the
    // authored subgraph declarations, but invalidate the migration/lowering
    // caches so the next compile rebuilds them against the new handles.
    for (auto& sub : subgraphs_) {
        sub->localToParentTexture_.clear();
        sub->passRange_ = {0, 0};
        sub->migrated_ = false;
    }
    for (auto& group : subpassGraphs_) {
        group->lowered_ = false;
        group->placeholderPassIndex_ = static_cast<std::uint32_t>(passes_.size());
        PassRecord placeholder;
        placeholder.name = "__FrameGraphSubpassPlaceholder";
        passes_.push_back(std::move(placeholder));
    }

    const auto submissionFence = submissionFence_;
    core::pmr::Vector<arp::core::UniquePtr<rhi::GpuTexture>> placedTextures(
        &core::defaultCpuMemoryResource());
    for (auto& record : textures_) {
        // Every graph-owned texture can be referenced by the command buffer,
        // irrespective of whether it was authored as transient.  WebGPU
        // explicitly rejects a submit that uses a texture destroyed during
        // graph teardown, so route all non-imported owned textures through
        // the same completion-gated pool. Imported resources retain their
        // external ownership and must never be recycled here.
        if (record.imported || !record.texture) {
            continue;
        }
        if (record.placed) {
            continue;
        }
        if (orderedExecution_)
            resourcePool_->releaseOrdered(record.texture, orderedExecution_);
        else if (resourcePool_->ownsGraphResources())
            record.texture.reset(); // Never recorded/submitted in this explicit profile.
        else
            resourcePool_->releaseDeferred(std::move(record.texture), submissionFence);
    }
    for (const auto handleId : transientAliasedTextureHandles_) {
        auto& record = textureRecord({handleId});
        if (!record.texture || !record.placed) {
            throw std::runtime_error("RG-ALIAS-RESET-GROUP-MEMBER-MISSING");
        }
        placedTextures.push_back(std::move(record.texture));
    }
    transientAliasedTextureHandles_.clear();
    for (auto& record : buffers_) {
        // The command buffer lifetime rule is identical for ordinary and
        // transient graph-owned buffers.
        if (!record.buffer) {
            continue;
        }
        if (orderedExecution_)
            resourcePool_->releaseOrdered(record.buffer, orderedExecution_);
        else if (resourcePool_->ownsGraphResources())
            record.buffer.reset();
        else
            resourcePool_->releaseDeferredBuffer(std::move(record.buffer), submissionFence);
    }
    if (resourcePool_->ownsGraphResources()) {
        // Alias materialization and both execution entries remain closed.
        // Published /3 desc-only resources are retained by their original bundle.
        // No public fence, legacy return or cache path can grant reuse here.
    } else if (!transientAliasedLayout_.textureDescs.empty()) {
        FrameGraphAliasedResourceGroup aliasGroup;
        aliasGroup.textures = std::move(placedTextures);
        aliasGroup.heaps = std::move(transientHeaps_);
        resourcePool_->releaseDeferredAliasedGroup(
            std::move(aliasGroup), std::move(transientAliasedLayout_), submissionFence);
    } else {
        resourcePool_->releaseDeferredAliasedGroup(
            std::move(placedTextures), std::move(transientHeaps_), submissionFence);
    }
    if (std::getenv("ARP_FRAME_GRAPH_RETIREMENT_DIAG") != nullptr) {
        const auto pool = resourcePool_->report();
        std::cerr << "[rg-retirement] pool=" << &resourcePool()
                  << " deferred-pending=" << pool.pendingBytes
                  << " deferred-cached=" << pool.cachedBytes
                  << " deferred-resources=" << pool.pendingResourceCount
                  << " budget=" << pool.byteBudget << '\n';
    }
    submissionFence_.reset();
    orderedExecution_.reset();

    // IMPORTANT: placed textures must be erased from textures_ BEFORE the
    // heaps are destroyed, because the placed textures' destructors may
    // reference heap internals (e.g. Vulkan: destroying a VkImage created
    // with VK_IMAGE_CREATE_ALIAS_BIT after its VkDeviceMemory is freed is
    // undefined behavior). Erroring erased first, then heap destruction.
    textures_.erase(std::remove_if(textures_.begin(),
                        textures_.end(),
                        [](const TextureRecord& record) {
                            return !record.imported && !record.textureRef;
                        }),
        textures_.end());
    buffers_.erase(std::remove_if(buffers_.begin(),
                       buffers_.end(),
                       [](const BufferRecord& record) {
                           return !record.imported();
                       }),
        buffers_.end());

    // transientHeaps_ was moved into the pool's completion-gated alias group.
    // It remains empty until the next allocation plan is materialized.
}

const std::pmr::vector<std::pmr::string>& FrameGraph::executionTrace() const
{
    return executionTrace_;
}

const std::pmr::vector<ResourceTraceRecord>& FrameGraph::resourceTrace() const
{
    return resourceTrace_;
}

const std::pmr::vector<PassTraceRecord>& FrameGraph::passTrace() const
{
    return passTrace_;
}

const std::pmr::vector<AttachmentTraceRecord>& FrameGraph::attachmentTrace() const
{
    return attachmentTrace_;
}

const std::pmr::vector<AliasTraceRecord>& FrameGraph::aliasTrace() const
{
    return aliasTrace_;
}

const std::pmr::vector<std::pmr::string>& FrameGraph::markerTrace() const
{
    return markerTrace_;
}

const std::pmr::vector<BarrierTraceRecord>& FrameGraph::barrierTrace() const
{
    return barrierTrace_;
}

const std::pmr::vector<FrameGraph::SubpassInputLoweringRecord>&
FrameGraph::subpassInputLowerings() const
{
    return subpassInputLowerings_;
}

FrameGraph::PassRecord& FrameGraph::pass(uint32_t id)
{
    invalidateCompiledRetirementSource();
    if (id >= passes_.size()) {
        throw std::runtime_error("invalid frame graph pass id");
    }

    return passes_[id];
}

TextureHandle FrameGraph::allocateTextureHandle() const
{
    invalidateCompiledRetirementSource();
    uint32_t candidate = 1;
    while (true) {
        bool used = false;
        for (const auto& record : textures_) {
            if (record.handle.id == candidate) {
                used = true;
                break;
            }
        }
        if (!used) {
            return TextureHandle{candidate};
        }
        ++candidate;
    }
}

BufferHandle FrameGraph::allocateBufferHandle() const
{
    invalidateCompiledRetirementSource();
    uint32_t candidate = 1;
    while (true) {
        bool used = false;
        for (const auto& record : buffers_) {
            if (record.handle.id == candidate) {
                used = true;
                break;
            }
        }
        if (!used) {
            return BufferHandle{candidate};
        }
        ++candidate;
    }
}

const FrameGraph::TextureRecord& FrameGraph::textureRecord(TextureHandle handle) const
{
    for (const auto& record : textures_) {
        if (record.handle.id == handle.id) {
            return record;
        }
    }

    throw std::runtime_error("invalid frame graph texture handle " + std::to_string(handle.id));
}

FrameGraph::TextureRecord& FrameGraph::textureRecord(TextureHandle handle)
{
    for (auto& record : textures_) {
        if (record.handle.id == handle.id) {
            return record;
        }
    }

    throw std::runtime_error("invalid frame graph texture handle " + std::to_string(handle.id));
}

const FrameGraph::BufferRecord& FrameGraph::bufferRecord(BufferHandle handle) const
{
    for (const auto& record : buffers_) {
        if (record.handle.id == handle.id) {
            return record;
        }
    }

    throw std::runtime_error("invalid frame graph buffer handle");
}

bool FrameGraph::isTextureReadAfter(
    TextureHandle handle, size_t passIndex, const std::pmr::vector<bool>& culled) const
{
    for (size_t index = passIndex + 1; index < passes_.size(); ++index) {
        if (index < culled.size() && culled[index]) {
            continue;
        }
        const auto& candidate = passes_[index];
        for (const auto& resourceUse : candidate.reads) {
            if (resourceUse.handle.id == handle.id) {
                return true;
            }
        }
    }

    return false;
}

bool FrameGraph::isBufferReadAfter(
    BufferHandle handle, size_t passIndex, const std::pmr::vector<bool>& culled) const
{
    for (size_t index = passIndex + 1; index < passes_.size(); ++index) {
        if (index < culled.size() && culled[index]) {
            continue;
        }
        const auto& candidate = passes_[index];
        for (const auto& resourceUse : candidate.bufferReads) {
            if (resourceUse.handle.id == handle.id) {
                return true;
            }
        }
    }

    return false;
}

rhi::RenderPassDesc FrameGraph::renderPassDescFor(const PassRecord& passRecord) const
{
    if (passRecord.attachmentWrites.empty()) {
        throw std::runtime_error("frame graph render pass callback requires attachment writes");
    }

    rhi::RenderPassDesc desc;
    desc.label = passRecord.name;
    for (const auto& attachment : passRecord.attachmentWrites) {
        const auto& record = textureRecord(attachment.handle);
        const auto& texture = *record.resolveTexture();
        if (attachment.state == rhi::ResourceState::DepthAttachment
            || aspectFor(texture) == rhi::TextureAspect::Depth) {
            if (desc.hasDepthStencilAttachment) {
                throw std::runtime_error("frame graph render pass supports one depth attachment");
            }
            desc.hasDepthStencilAttachment = true;
            desc.depthStencilAttachment = rhi::RenderPassDepthStencilAttachmentDesc{
                texture.desc().label,
                texture.id(),
                texture.desc().format,
                attachment.loadOp,
                attachment.storeOp,
                texture.desc().format == rhi::TextureFormat::Depth24Stencil8 ? attachment.loadOp
                                                                             : rhi::LoadOp::Load,
                texture.desc().format == rhi::TextureFormat::Depth24Stencil8 ? attachment.storeOp
                                                                             : rhi::StoreOp::Store,
            };
            continue;
        }

        if (desc.colorTargetName.empty()) {
            desc.colorTargetName = texture.desc().label;
        }
        desc.colorAttachments.push_back({
            texture.desc().label,
            texture.id(),
            texture.desc().format,
            attachment.loadOp,
            attachment.storeOp,
        });
        desc.colorAttachments.back().clearColor = attachment.clearColor;
    }

    if (desc.colorAttachments.empty() && !desc.hasDepthStencilAttachment) {
        throw std::runtime_error("frame graph render pass requires color or depth attachments");
    }

    return desc;
}

bool FrameGraph::shouldCullPass(size_t passIndex, const std::pmr::vector<bool>& culled) const
{
    if (!passCullingEnabled_) {
        return false;
    }

    const auto& candidate = passes_[passIndex];
    if (candidate.externalSideEffects) {
        return false;
    }
    if (candidate.writes.empty() && candidate.bufferWrites.empty()) {
        return false;
    }

    for (const auto& resourceUse : candidate.writes) {
        const auto& record = textureRecord(resourceUse.handle);
        if (record.imported || isTextureReadAfter(resourceUse.handle, passIndex, culled)) {
            return false;
        }
    }
    for (const auto& resourceUse : candidate.bufferWrites) {
        if (bufferRecord(resourceUse.handle).imported()
            || isBufferReadAfter(resourceUse.handle, passIndex, culled)) {
            return false;
        }
    }

    return true;
}

void FrameGraph::buildAliasTrace(const CompiledFrameGraph& compiled)
{
    for (const auto& alias : compiled.aliasPlan()) {
        aliasTrace_.push_back(alias);
    }
}

void FrameGraph::replayPassBarriers(rhi::CommandEncoder& commands,
    const CompiledFrameGraph& compiled,
    std::uint32_t passOriginalIndex)
{
    for (const auto& handoff : compiled.aliasHandoffs()) {
        if (handoff.consumerPass != passOriginalIndex) {
            continue;
        }
        const auto transitioned = commands.aliasBarrier(rhi::AliasingBarrierDesc{
            texture(handoff.before).id(),
            texture(handoff.after).id(),
            handoff.aliasGroup,
            handoff.sourceStages,
            handoff.destinationStages,
            handoff.sourceAccess,
            handoff.destinationAccess,
            static_cast<std::uint32_t>(handoff.queue),
            static_cast<std::uint32_t>(handoff.queue),
            false,
        });
        if (!transitioned.success) {
            throw std::runtime_error(arp::core::toStdString(
                std::pmr::string("RG-ALIAS-HANDOFF-FAILED: ") + transitioned.diagnostics));
        }
    }
    for (const auto& batch : compiled.passBarrierBatches()) {
        if (batch.passOriginalIndex != passOriginalIndex) {
            continue;
        }
        for (const auto& barrier : batch.prePassBarriers) {
            const auto access = (barrier.before == rhi::ResourceState::Undefined
                                    || barrier.dstStage == rhi::PipelineStage::TopOfPipe)
                ? ResourceAccess::Write
                : ResourceAccess::Read;
            if (barrier.handleId == 0) {
                throw std::runtime_error(arp::core::toStdString(
                    std::pmr::string("frame graph barrier references unknown resource: ")
                    + barrier.resourceName));
            }
            if (barrier.kind == ResourceKind::Texture) {
                auto& target = texture(TextureHandle{barrier.handleId});
                if (reuseCompiledTrace_ && resourceTraceCursor_ < resourceTrace_.size()) {
                    auto& trace = resourceTrace_[resourceTraceCursor_++];
                    trace.kind = ResourceKind::Texture;
                    trace.access = access;
                    trace.state = barrier.after;
                } else {
                    resourceTrace_.push_back(ResourceTraceRecord{
                        barrier.passName,
                        target.desc().label,
                        ResourceKind::Texture,
                        access,
                        barrier.after,
                    });
                    ++resourceTraceCursor_;
                }
                auto runtimeBarrier = barrier;
                if (hasExecutedCompiledGraph_) {
                    runtimeBarrier.before = target.state();
                    runtimeBarrier.srcStage = runtimeBarrier.srcStage | barrier.replaySourceStages;
                    runtimeBarrier.srcAccess =
                        runtimeBarrier.srcAccess | barrier.replaySourceAccess;
                }
                runtimeBarrier.memoryOnly = runtimeBarrier.before == barrier.after;
                if (reuseCompiledTrace_ && barrierTraceCursor_ < barrierTrace_.size()) {
                    auto& trace = barrierTrace_[barrierTraceCursor_++];
                    trace.before = runtimeBarrier.before;
                    trace.memoryOnly = runtimeBarrier.memoryOnly;
                    trace.srcStage = runtimeBarrier.srcStage;
                    trace.srcAccess = runtimeBarrier.srcAccess;
                } else {
                    barrierTrace_.push_back(runtimeBarrier);
                    ++barrierTraceCursor_;
                }
                const bool memoryOnly = runtimeBarrier.before == barrier.after;
                // An unused external release can already be in its final
                // state on replay. No earlier graph access needs ordering.
                if (memoryOnly && runtimeBarrier.srcAccess == rhi::ResourceAccess::None)
                    continue;
                const auto transitioned = memoryOnly
                    ? commands.memoryBarrier(target,
                          rhi::ResourceMemoryBarrierDesc{target.id(),
                              runtimeBarrier.srcStage,
                              barrier.dstStage,
                              runtimeBarrier.srcAccess,
                              barrier.dstAccess})
                    : commands.transition(target,
                          rhi::ResourceBarrierDesc{target.id(),
                              rhi::defaultTextureSubresourceRange(target.desc()),
                              runtimeBarrier.before,
                              barrier.after,
                              runtimeBarrier.srcStage,
                              barrier.dstStage,
                              runtimeBarrier.srcAccess,
                              barrier.dstAccess});
                if (!transitioned.success) {
                    throw std::runtime_error(arp::core::toStdString(
                        std::pmr::string("RG-BARRIER-TRANSITION-FAILED: ")
                        + transitioned.diagnostics + " resource=" + target.desc().label + " pass="
                        + barrier.passName + " expected=" + resourceStateName(runtimeBarrier.before)
                        + " actual=" + resourceStateName(target.state())
                        + " requested=" + resourceStateName(barrier.after)));
                }
            } else {
                auto& target = buffer(BufferHandle{barrier.handleId});
                if (reuseCompiledTrace_ && resourceTraceCursor_ < resourceTrace_.size()) {
                    auto& trace = resourceTrace_[resourceTraceCursor_++];
                    trace.kind = ResourceKind::Buffer;
                    trace.access = access;
                    trace.state = barrier.after;
                } else {
                    resourceTrace_.push_back(ResourceTraceRecord{
                        barrier.passName,
                        target.desc().label,
                        ResourceKind::Buffer,
                        access,
                        barrier.after,
                    });
                    ++resourceTraceCursor_;
                }
                auto runtimeBarrier = barrier;
                if (hasExecutedCompiledGraph_) {
                    runtimeBarrier.before = target.state();
                    runtimeBarrier.srcStage = runtimeBarrier.srcStage | barrier.replaySourceStages;
                    runtimeBarrier.srcAccess =
                        runtimeBarrier.srcAccess | barrier.replaySourceAccess;
                }
                runtimeBarrier.memoryOnly = runtimeBarrier.before == barrier.after;
                if (reuseCompiledTrace_ && barrierTraceCursor_ < barrierTrace_.size()) {
                    auto& trace = barrierTrace_[barrierTraceCursor_++];
                    trace.before = runtimeBarrier.before;
                    trace.memoryOnly = runtimeBarrier.memoryOnly;
                    trace.srcStage = runtimeBarrier.srcStage;
                    trace.srcAccess = runtimeBarrier.srcAccess;
                } else {
                    barrierTrace_.push_back(runtimeBarrier);
                    ++barrierTraceCursor_;
                }
                const bool memoryOnly = runtimeBarrier.before == barrier.after;
                // An unused external release can already be in its final
                // state on replay. No earlier graph access needs ordering.
                if (memoryOnly && runtimeBarrier.srcAccess == rhi::ResourceAccess::None)
                    continue;
                const auto transitioned = memoryOnly
                    ? commands.memoryBarrier(target,
                          rhi::ResourceMemoryBarrierDesc{target.id(),
                              runtimeBarrier.srcStage,
                              barrier.dstStage,
                              runtimeBarrier.srcAccess,
                              barrier.dstAccess})
                    : commands.transition(target,
                          rhi::ResourceBarrierDesc{target.id(),
                              rhi::TextureSubresourceRange{},
                              runtimeBarrier.before,
                              barrier.after,
                              runtimeBarrier.srcStage,
                              barrier.dstStage,
                              runtimeBarrier.srcAccess,
                              barrier.dstAccess});
                if (!transitioned.success) {
                    throw std::runtime_error(arp::core::toStdString(
                        std::pmr::string("RG-BARRIER-TRANSITION-FAILED: ")
                        + transitioned.diagnostics + " resource=" + target.desc().label + " pass="
                        + barrier.passName + " expected=" + resourceStateName(runtimeBarrier.before)
                        + " actual=" + resourceStateName(target.state())
                        + " requested=" + resourceStateName(barrier.after)));
                }
            }
        }
        return;
    }
}

void FrameGraph::recordAttachmentTrace(const PassRecord& passRecord)
{
    for (const auto& attachment : passRecord.attachmentWrites) {
        const auto& record = textureRecord(attachment.handle);
        if (reuseCompiledTrace_ && attachmentTraceCursor_ < attachmentTrace_.size()) {
            auto& trace = attachmentTrace_[attachmentTraceCursor_++];
            trace.state = attachment.state;
            trace.loadOp = attachment.loadOp;
            trace.storeOp = attachment.storeOp;
            trace.tileHints = attachment.tileHints;
            trace.clearColor = attachment.clearColor;
            trace.transient = record.transient;
            trace.imported = record.imported;
            trace.externalProtected = record.imported;
        } else {
            attachmentTrace_.push_back(AttachmentTraceRecord{
                passRecord.name,
                record.resolvedDesc().label,
                attachment.state,
                attachment.loadOp,
                attachment.storeOp,
                attachment.tileHints,
                attachment.clearColor,
                record.transient,
                record.imported,
                record.imported,
            });
            ++attachmentTraceCursor_;
        }
    }
}

PassBuilder::PassBuilder(FrameGraph& graph, uint32_t passId) : graph_(graph), passId_(passId)
{}

PassBuilder& PassBuilder::read(TextureHandle handle, rhi::ResourceState state)
{
    graph_.pass(passId_).reads.push_back(FrameGraph::ResourceUse{handle, state});
    return *this;
}

PassBuilder& PassBuilder::write(TextureHandle handle, rhi::ResourceState state)
{
    graph_.pass(passId_).writes.push_back(FrameGraph::ResourceUse{handle, state});
    return *this;
}

PassBuilder& PassBuilder::writeAttachment(TextureHandle handle,
    rhi::ResourceState state,
    rhi::LoadOp loadOp,
    rhi::StoreOp storeOp,
    MobileTileHints tileHints,
    rhi::Color clearColor)
{
    auto& passRecord = graph_.pass(passId_);
    passRecord.writes.push_back(FrameGraph::ResourceUse{handle, state});
    passRecord.attachmentWrites.push_back(
        FrameGraph::AttachmentUse{handle, state, loadOp, storeOp, tileHints, clearColor});
    return *this;
}

PassBuilder& PassBuilder::read(BufferHandle handle, rhi::ResourceState state)
{
    if (graph_.bufferRecord(handle).readOnly
        && rhi::semantics::resourceAccessWrites(accessFor(state)))
        throw std::runtime_error("RG-BINDLESS-METADATA-READ-ONLY");
    graph_.pass(passId_).bufferReads.push_back(FrameGraph::BufferUse{handle, state});
    return *this;
}

PassBuilder& PassBuilder::write(BufferHandle handle, rhi::ResourceState state)
{
    if (graph_.bufferRecord(handle).readOnly)
        throw std::runtime_error("RG-BINDLESS-METADATA-READ-ONLY");
    graph_.pass(passId_).bufferWrites.push_back(FrameGraph::BufferUse{handle, state});
    return *this;
}

PassBuilder& PassBuilder::access(TextureHandle handle,
    const rhi::semantics::ResourceAccessScope& scope,
    const rhi::TextureSubresourceRange& range)
{
    if (const auto* error = rhi::semantics::validateResourceAccessScope(scope))
        throw std::runtime_error(error);
    const auto& desc = graph_.textureDesc(handle);
    const auto full = rhi::defaultTextureSubresourceRange(desc);
    if (range.aspect != full.aspect
        || !core::containsUnsignedRange(
            0u, full.mipLevelCount, range.baseMipLevel, range.mipLevelCount)
        || !core::containsUnsignedRange(
            0u, full.arrayLayerCount, range.baseArrayLayer, range.arrayLayerCount))
        throw std::runtime_error("RG-ACCESS-TEXTURE-RANGE-INVALID");
    using A = rhi::ResourceAccess;
    using U = rhi::TextureUsage;
    const auto needs = [&](A bits, U usage) {
        return (scope.access & bits) == A::None || (desc.usage & usage) != U::None;
    };
    if ((scope.access & A::IndirectCommandRead) != A::None
        || !needs(A::ShaderRead, U::Sampled | U::Storage) || !needs(A::ShaderWrite, U::Storage)
        || !needs(A::TransferRead, U::CopySrc) || !needs(A::TransferWrite, U::CopyDst)
        || !needs(A::ColorAttachmentRead | A::ColorAttachmentWrite | A::DepthStencilRead
                | A::DepthStencilWrite,
            U::RenderAttachment))
        throw std::runtime_error("RG-ACCESS-TEXTURE-USAGE-MISMATCH");
    auto& pass = graph_.pass(passId_);
    FrameGraph::ResourceUse use{handle, scope.state, scope.stages, scope.access, range};
    if (rhi::semantics::resourceAccessReads(scope.access))
        pass.reads.push_back(use);
    if (rhi::semantics::resourceAccessWrites(scope.access))
        pass.writes.push_back(use);
    return *this;
}

PassBuilder& PassBuilder::access(BufferHandle handle,
    const rhi::semantics::ResourceAccessScope& scope,
    std::uint64_t offset,
    std::uint64_t size)
{
    if (const auto* error = rhi::semantics::validateResourceAccessScope(scope))
        throw std::runtime_error(error);
    if (graph_.bufferRecord(handle).readOnly && rhi::semantics::resourceAccessWrites(scope.access))
        throw std::runtime_error("RG-BINDLESS-METADATA-READ-ONLY");
    const auto& desc = graph_.bufferRecord(handle).resolveBuffer()->desc();
    if (!core::containsUnsignedRange(std::uint64_t{0}, desc.sizeBytes, offset, size))
        throw std::runtime_error("RG-ACCESS-BUFFER-RANGE-INVALID");
    using A = rhi::ResourceAccess;
    using U = rhi::ResourceUsage;
    const auto needs = [&](A bits, U usage) {
        return (scope.access & bits) == A::None || (desc.usage & usage) != U::None;
    };
    if ((scope.access
            & (A::ColorAttachmentRead | A::ColorAttachmentWrite | A::DepthStencilRead
                | A::DepthStencilWrite | A::PresentRead))
            != A::None
        || !needs(A::ShaderRead, U::UniformBuffer | U::StorageBuffer)
        || !needs(A::ShaderWrite, U::StorageBuffer)
        || !needs(A::IndirectCommandRead, U::IndirectBuffer)
        || !needs(A::VertexAttributeRead, U::VertexBuffer) || !needs(A::IndexRead, U::IndexBuffer)
        || !needs(A::TransferRead, U::CopySrc) || !needs(A::TransferWrite, U::CopyDst))
        throw std::runtime_error("RG-ACCESS-BUFFER-USAGE-MISMATCH");
    auto& pass = graph_.pass(passId_);
    FrameGraph::BufferUse use{handle, scope.state, scope.stages, scope.access, offset, size};
    if (rhi::semantics::resourceAccessReads(scope.access))
        pass.bufferReads.push_back(use);
    if (rhi::semantics::resourceAccessWrites(scope.access))
        pass.bufferWrites.push_back(use);
    return *this;
}

PassBuilder& PassBuilder::kind(PassKind kind)
{
    graph_.pass(passId_).kind = kind;
    return *this;
}

PassBuilder& PassBuilder::queue(QueueRole queue)
{
    graph_.pass(passId_).queue = queue;
    return *this;
}

PassBuilder& PassBuilder::asyncComputeEligible(bool eligible)
{
    graph_.pass(passId_).asyncComputeEligible = eligible;
    return *this;
}

PassBuilder& PassBuilder::externalSideEffects(bool enabled)
{
    graph_.pass(passId_).externalSideEffects = enabled;
    return *this;
}

PassBuilder& PassBuilder::onSubmitted(PassSubmissionObserver& observer, PassSubmissionToken token)
{
    graph_.pass(passId_).submissionObserver = &observer;
    graph_.pass(passId_).submissionToken = token;
    return *this;
}

PassBuilder& PassBuilder::addRhiDrawIndexedCommand(const RhiDrawIndexedCommand& command)
{
    graph_.pass(passId_).rhiDrawCommands.push_back(command);
    return *this;
}

PassBuilder& PassBuilder::addRhiDispatchCommand(const RhiDispatchCommand& command)
{
    graph_.pass(passId_).rhiDispatchCommands.push_back(command);
    return *this;
}

PassBuilder& PassBuilder::addRhiTransferCommand(const RhiTransferCommand& command)
{
    graph_.pass(passId_).rhiTransferCommands.push_back(command);
    return *this;
}

void PassBuilder::execute(FrameGraph::ExecuteCallback callback)
{
    auto& passRecord = graph_.pass(passId_);
    passRecord.callback = std::move(callback);
    passRecord.renderPassCallback = {};
}

void PassBuilder::executeRenderPass(RenderPassExecuteCallback callback)
{
    auto& passRecord = graph_.pass(passId_);
    passRecord.callback = {};
    passRecord.renderPassCallback = std::move(callback);
}

// ── Phase 6: SubgraphBuilder ──

SubgraphBuilder::SubgraphBuilder(
    rhi::GpuDevice& device, FrameGraph& parent, const SubgraphRecipe& recipe)
    : device_(device), parentGraph_(parent), recipe_(recipe),
      localGraph_(core::makeUnique<FrameGraph>(device)),
      pinBindings_(&core::defaultCpuMemoryResource())
{}

SubgraphBuilder& SubgraphBuilder::bindPin(std::pmr::string pinName, TextureHandle handle)
{
    pinBindings_.emplace_back(std::move(pinName), handle);
    return *this;
}

FrameGraph& SubgraphBuilder::localGraph()
{
    return *localGraph_;
}

void SubgraphBuilder::markPinImported(TextureHandle handle)
{
    auto& record = localGraph_->textureRecord(handle);
    record.imported = true;
}

SubgraphBuilder& SubgraphBuilder::aliasScopeId(std::uint32_t scopeId)
{
    aliasScopeId_ = scopeId;
    return *this;
}

std::pmr::vector<TextureHandle> SubgraphBuilder::outputPinParentHandles() const
{
    std::pmr::vector<TextureHandle> handles(&core::defaultCpuMemoryResource());
    for (const auto& pin : recipe_.outputPins) {
        for (const auto& lt : localGraph_->textures_) {
            if (lt.desc.label == pin.name) {
                auto it = localToParentTexture_.find(lt.handle.id);
                if (it != localToParentTexture_.end()) {
                    handles.push_back(it->second);
                }
                break;
            }
        }
    }
    return handles;
}

bool SubgraphBuilder::hasExternalOutputConsumer(const std::pmr::vector<bool>& culled) const
{
    const auto outputHandles = outputPinParentHandles();
    if (outputHandles.empty()) {
        return false;
    }

    const auto& passes = parentGraph_.passes_;
    for (size_t i = 0; i < passes.size(); ++i) {
        // Skip this subgraph's own passes.
        if (i >= passRange_.first && i < passRange_.second) {
            continue;
        }
        // Skip already-culled passes.
        if (i < culled.size() && culled[i]) {
            continue;
        }
        for (const auto& read : passes[i].reads) {
            for (const auto& oh : outputHandles) {
                if (read.handle.id == oh.id) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool SubgraphBuilder::shouldCullAsUnit(const std::pmr::vector<bool>& culled) const
{
    // A subgraph with no output pins has no external consumer surface; its
    // dead transient writes are handled by per-pass culling.
    if (recipe_.outputPins.empty()) {
        return false;
    }
    // If every pass in the range is already culled, nothing to do.
    bool anyAlive = false;
    for (std::uint32_t i = passRange_.first; i < passRange_.second; ++i) {
        if (i >= culled.size() || !culled[i]) {
            anyAlive = true;
            break;
        }
    }
    if (!anyAlive) {
        return false;
    }
    return !hasExternalOutputConsumer(culled);
}

bool SubgraphBuilder::markCulledAsUnit(std::pmr::vector<bool>& culled) const
{
    bool changed = false;
    for (std::uint32_t i = passRange_.first; i < passRange_.second; ++i) {
        if (i < culled.size() && !culled[i]) {
            culled[i] = true;
            changed = true;
        }
    }
    return changed;
}

SubgraphBuilder& FrameGraph::addSubgraph(const SubgraphRecipe& recipe)
{
    invalidateCompiledRetirementSource();
    auto builder = core::makeUnique<SubgraphBuilder>(device_, *this, recipe);
    auto* raw = builder.get();
    subgraphs_.push_back(std::move(builder));
    return *raw;
}

SubpassGraph& FrameGraph::addSubpassGraph(std::pmr::string name)
{
    invalidateCompiledRetirementSource();
    auto group = core::makeUnique<SubpassGraph>(*this, std::move(name));
    auto* raw = group.get();
    group->placeholderPassIndex_ = static_cast<std::uint32_t>(passes_.size());
    PassRecord placeholder;
    placeholder.name = "__FrameGraphSubpassPlaceholder";
    passes_.push_back(std::move(placeholder));
    subpassGraphs_.push_back(std::move(group));
    return *raw;
}

// --- SubpassGraph ---

SubpassGraph::SubpassGraph(FrameGraph& parent, std::pmr::string name)
    : parent_(parent), name_(std::move(name))
{}

SubpassAttachment SubpassGraph::attachment(
    TextureHandle handle, rhi::ResourceState state, AttachmentRole role, MobileTileHints tileHints)
{
    const auto index = static_cast<std::uint32_t>(attachments_.size());
    attachments_.push_back({handle, state, role, tileHints});
    return {index};
}

SubpassBuilder& SubpassGraph::subpass(std::pmr::string name)
{
    auto builder = core::makeUnique<SubpassBuilder>(*this, std::move(name));
    auto* raw = builder.get();
    subpasses_.push_back(std::move(builder));
    return *raw;
}

std::uint32_t SubpassGraph::subpassCount() const
{
    return static_cast<std::uint32_t>(subpasses_.size());
}

// --- SubpassBuilder ---

SubpassBuilder::SubpassBuilder(SubpassGraph& group, std::pmr::string name)
    : group_(group), name_(std::move(name))
{}

SubpassBuilder& SubpassBuilder::writeAttachment(
    SubpassAttachment att, rhi::LoadOp loadOp, rhi::StoreOp storeOp, rhi::Color clearColor)
{
    attachmentWrites_.push_back({att.index, loadOp, storeOp, clearColor});
    return *this;
}

SubpassBuilder& SubpassBuilder::readInputAttachment(SubpassAttachment att)
{
    inputReads_.push_back({att.index, {}});
    return *this;
}

SubpassBuilder& SubpassBuilder::readInputAttachment(
    SubpassAttachment att, std::uint32_t shaderSet, std::uint32_t shaderBinding)
{
    inputReads_.push_back({
        att.index,
        {shaderSet,
            shaderBinding,
            rhi::semantics::kInvalidSubpassShaderBinding,
            rhi::semantics::SubpassInputPhysicalBinding::Unsupported},
    });
    return *this;
}

SubpassBuilder& SubpassBuilder::read(TextureHandle handle, rhi::ResourceState state)
{
    externalReads_.push_back({handle, state});
    return *this;
}

void SubpassBuilder::executeRenderPass(FrameGraph::RenderPassExecuteCallback callback)
{
    callback_ = std::move(callback);
}

void SubgraphBuilder::migratePassesAndTextures()
{
    using namespace rhi;

    // Idempotency: migrate at most once. compile() may be invoked repeatedly
    // (diagnostic pre-compile, compile()+execute(), repeated trace); without
    // this guard each call would create a fresh batch of parent transient
    // textures and re-append the local passes, accumulating duplicates and
    // turning the read-only compile() into a mutating operation. After the
    // first migration the local passes/textures already live in the parent
    // and passRange_/localToParentTexture_ are stable for compileAndLink.
    if (migrated_) {
        return;
    }

    // Build local texture handle.id -> parent handle remap.
    localToParentTexture_.clear();

    // Input pin textures reuse the bound parent handle.
    for (const auto& [pinName, handle] : pinBindings_) {
        for (const auto& lt : localGraph_->textures_) {
            if (lt.desc.label == pinName) {
                localToParentTexture_[lt.handle.id] = handle;
                break;
            }
        }
    }

    // Internal + output pin transient textures get real parent handles via
    // createTransientTexture (replaces the 10000+aliasScopeId_*1000 placeholder
    // that did not participate in the parent alias plan).
    // Phase 6 alias scope: Isolated-scope subgraphs tag their transient
    // textures with a non-zero aliasScopeId so the compiler groups them only
    // within the same scope (never with the global pool). AllowCrossScope
    // leaves aliasScopeId=0 so they enter the global pool like parent
    // transients.
    const bool isolateScope = (recipe_.aliasScope == AliasScopePolicy::Isolated);
    for (const auto& lt : localGraph_->textures_) {
        if (localToParentTexture_.find(lt.handle.id) != localToParentTexture_.end()) {
            continue;
        }
        auto parentHandle = parentGraph_.createTransientTexture(lt.desc);
        localToParentTexture_[lt.handle.id] = parentHandle;
        if (isolateScope) {
            parentGraph_.textureRecord(parentHandle).aliasScopeId = aliasScopeId_;
        }
    }

    // Append local pass records with remapped texture handles. Buffer handles
    // are not remapped yet (current subgraph tests use textures only).
    passRange_.first = static_cast<std::uint32_t>(parentGraph_.passes_.size());
    for (const auto& localPass : localGraph_->passes_) {
        if (!localPass.bufferReads.empty() || !localPass.bufferWrites.empty()) {
            throw std::runtime_error(arp::core::toStdString(std::pmr::string("subgraph \"")
                + recipe_.name + std::pmr::string("\" pass \"") + localPass.name
                + std::pmr::string(
                    "\" uses buffers, but subgraph buffer handle migration is not yet implemented")));
        }
        FrameGraph::PassRecord pr = localPass;
        pr.subpassGroup = nullptr; // subgraph passes never have subpass groups
        pr.name = recipe_.name + std::pmr::string("::") + localPass.name;
        for (auto& r : pr.reads) {
            auto it = localToParentTexture_.find(r.handle.id);
            if (it != localToParentTexture_.end())
                r.handle = it->second;
        }
        for (auto& w : pr.writes) {
            auto it = localToParentTexture_.find(w.handle.id);
            if (it != localToParentTexture_.end())
                w.handle = it->second;
        }
        for (auto& aw : pr.attachmentWrites) {
            auto it = localToParentTexture_.find(aw.handle.id);
            if (it != localToParentTexture_.end())
                aw.handle = it->second;
        }
        parentGraph_.passes_.push_back(std::move(pr));
    }
    passRange_.second = static_cast<std::uint32_t>(parentGraph_.passes_.size());
    migrated_ = true;
}

void SubgraphBuilder::compileAndLink(
    CompiledFrameGraph& parentCompiled, const FrameGraph& parentGraph) const
{
    using namespace rhi;

    // Compile local graph for producer/lifetime validation only. Migrated
    // pass records are already in the parent (migratePassesAndTextures) and
    // the parent compile() produced the real compiled passes / barriers /
    // alias plan including the subgraph.
    auto localCompiled = FrameGraphCompiler{}.compile(*localGraph_);

    // ── Validate pin bindings ──
    // Input pins must be bound.
    std::pmr::map<std::pmr::string, TextureHandle> boundPinHandle;
    for (const auto& [pinName, handle] : pinBindings_) {
        boundPinHandle[pinName] = handle;
    }

    for (const auto& pin : recipe_.inputPins) {
        if (boundPinHandle.find(pin.name) == boundPinHandle.end()) {
            throw std::runtime_error(arp::core::toStdString(std::pmr::string("subgraph \"")
                + recipe_.name + std::pmr::string("\" input pin \"") + pin.name
                + std::pmr::string("\" is not bound to a parent handle")));
        }
    }

    // Phase 6: pin usage vs parent texture format mismatch validation.
    // A DepthAttachment pin bound to a color-format texture (or a
    // ColorAttachment pin bound to a depth format) is a deterministic
    // authoring error that would produce wrong barriers and invalid
    // render-pass attachment configs.
    for (const auto& pin : recipe_.inputPins) {
        const auto it = boundPinHandle.find(pin.name);
        if (it == boundPinHandle.end()) {
            continue;
        }
        const auto& parentDesc = parentGraph.textureRecord(it->second).resolvedDesc();
        const bool parentIsDepth = rhi::isDepthTextureFormat(parentDesc.format);
        const bool pinIsDepth = (pin.usage == PinUsage::DepthAttachment);
        if (pinIsDepth != parentIsDepth) {
            throw std::runtime_error(arp::core::toStdString(std::pmr::string("subgraph \"")
                + recipe_.name + std::pmr::string("\" input pin \"") + pin.name
                + std::pmr::string("\" usage mismatches parent texture \"") + parentDesc.label
                + std::pmr::string("\" format (depth vs color)")));
        }
    }

    // Output pins: validate existence — at minimum, the pin name is declared.
    // Full producer validation (output pin declared but no producer writes it)
    // is done below by checking local lifetimes.
    for (const auto& pin : recipe_.outputPins) {
        (void)pin; // Output pin contract: accepted at Prototype level.
    }

    // Phase 6: attachment pin loadOp/storeOp conflicts. An attachment input
    // pin's expectedLoadOp must match the subgraph's first attachment access
    // loadOp; an attachment output pin's expectedStoreOp must match the last
    // attachment access storeOp. A mismatch is a deterministic authoring
    // error (e.g. expectedLoadOp::Load but the subgraph clears on first
    // access, discarding parent data the pin contract promised to preserve).
    auto findLocalPinHandle = [&](const std::pmr::string& pinName) -> TextureHandle {
        for (const auto& lt : localGraph_->textures_) {
            if (lt.desc.label == pinName) {
                return lt.handle;
            }
        }
        return TextureHandle{};
    };
    auto isAttachmentUsage = [](PinUsage usage) {
        return usage == PinUsage::ColorAttachment || usage == PinUsage::DepthAttachment;
    };

    for (const auto& pin : recipe_.inputPins) {
        if (!isAttachmentUsage(pin.usage)) {
            continue;
        }
        const auto localHandle = findLocalPinHandle(pin.name);
        if (localHandle.id == 0) {
            continue;
        }
        bool found = false;
        rhi::LoadOp firstLoadOp = rhi::LoadOp::Load;
        for (const auto& lp : localGraph_->passes_) {
            for (const auto& aw : lp.attachmentWrites) {
                if (aw.handle.id == localHandle.id) {
                    firstLoadOp = aw.loadOp;
                    found = true;
                    break;
                }
            }
            if (found) {
                break;
            }
        }
        if (found && firstLoadOp != pin.expectedLoadOp) {
            throw std::runtime_error(arp::core::toStdString(std::pmr::string("subgraph \"")
                + recipe_.name + std::pmr::string("\" input pin \"") + pin.name
                + std::pmr::string("\" expectedLoadOp conflicts with the subgraph's "
                                   "first attachment access loadOp")));
        }
    }

    for (const auto& pin : recipe_.outputPins) {
        if (!isAttachmentUsage(pin.usage)) {
            continue;
        }
        const auto localHandle = findLocalPinHandle(pin.name);
        if (localHandle.id == 0) {
            continue;
        }
        bool found = false;
        rhi::StoreOp lastStoreOp = rhi::StoreOp::Store;
        for (const auto& lp : localGraph_->passes_) {
            for (const auto& aw : lp.attachmentWrites) {
                if (aw.handle.id == localHandle.id) {
                    lastStoreOp = aw.storeOp;
                    found = true;
                }
            }
        }
        if (found && lastStoreOp != pin.expectedStoreOp) {
            throw std::runtime_error(arp::core::toStdString(std::pmr::string("subgraph \"")
                + recipe_.name + std::pmr::string("\" output pin \"") + pin.name
                + std::pmr::string("\" expectedStoreOp conflicts with the subgraph's "
                                   "last attachment access storeOp")));
        }
    }

    // Phase 7 [P0-B]: pass records, transient textures, handle remapping,
    // local pass linking and local barrier linking are now handled by
    // migratePassesAndTextures + the parent FrameGraphCompiler::compile —
    // the parent compiled graph already contains the subgraph's passes,
    // barrier plan and alias plan with real parent handles. Only the
    // subgraph-specific output-pin exit barriers and metadata are emitted
    // below.

    // ── Phase 6: infer culledAsUnit from compiled passes ──
    // After the parent compile() ran fixed-point culling (per-pass + whole-
    // unit), every pass in passRange_ is culled iff the subgraph was culled
    // as a unit (either explicitly by shouldCullAsUnit or by per-pass
    // cascade converging to the same result).
    bool culledAsUnit = true;
    const auto& compiledPasses = parentCompiled.passes();
    for (std::uint32_t i = passRange_.first; i < passRange_.second; ++i) {
        if (i >= compiledPasses.size() || !compiledPasses[i].culled) {
            culledAsUnit = false;
            break;
        }
    }

    if (culledAsUnit) {
        // Cull/read consistency check: a whole-culled subgraph must not have
        // any output pin still read by a non-culled pass outside the range.
        // If it does, the culling logic is inconsistent — throw rather than
        // emit barriers for a subgraph that produced no output.
        std::pmr::vector<bool> compiledCulled(
            compiledPasses.size(), false, &core::defaultCpuMemoryResource());
        for (size_t i = 0; i < compiledPasses.size(); ++i) {
            compiledCulled[i] = compiledPasses[i].culled;
        }
        if (hasExternalOutputConsumer(compiledCulled)) {
            throw std::runtime_error(
                arp::core::toStdString(std::pmr::string("subgraph \"") + recipe_.name
                    + std::pmr::string(
                        "\" was culled as a unit but an output pin "
                        "is still read by a non-culled pass (cull/read consistency violation)")));
        }

        // Whole-culled: skip output-pin exit barriers (no output produced)
        // and record the unit-cull decision.
        CompiledSubgraph meta;
        meta.name = recipe_.name;
        meta.globalPassBegin = passRange_.first;
        meta.globalPassEnd = passRange_.second;
        meta.aliasScopeId = aliasScopeId_;
        meta.aliasScopePolicy = recipe_.aliasScope;
        meta.culledAsUnit = true;
        for (const auto& [pinName, handle] : pinBindings_) {
            meta.pinBindings.emplace_back(pinName, handle);
        }
        parentCompiled.addSubgraph(std::move(meta));
        return;
    }

    // ── 5a. Emit input-pin entry barriers (boundary contract) ──
    // Each input pin has an entry barrier marking the state the subgraph
    // expects to read it in. The actual state transition is emitted by the
    // parent compiler's PassBarrierBatch (subgraph passes are migrated into
    // the parent); this boundary record is the contract metadata symmetric
    // to the output-pin exit barrier, so the barrier trace records both
    // subgraph boundaries.
    for (const auto& pin : recipe_.inputPins) {
        const auto it = boundPinHandle.find(pin.name);
        if (it == boundPinHandle.end()) {
            continue; // already validated above
        }

        ResourceState entryState = ResourceState::ShaderRead;
        switch (pin.usage) {
        case PinUsage::ColorAttachment:
            entryState = ResourceState::ColorAttachment;
            break;
        case PinUsage::DepthAttachment:
            entryState = ResourceState::DepthAttachment;
            break;
        case PinUsage::SampledTexture:
            entryState = ResourceState::ShaderRead;
            break;
        case PinUsage::StorageTexture:
            entryState = ResourceState::ShaderWrite;
            break;
        case PinUsage::StorageBuffer:
            entryState = ResourceState::ShaderWrite;
            break;
        case PinUsage::CopySource:
            entryState = ResourceState::CopySrc;
            break;
        case PinUsage::CopyDestination:
            entryState = ResourceState::CopyDst;
            break;
        }

        BarrierTraceRecord entryBarrier;
        entryBarrier.passName = recipe_.name + std::pmr::string("_InputPin_") + pin.name;
        entryBarrier.resourceName = pin.name;
        entryBarrier.handleId = it->second.id;
        entryBarrier.kind = ResourceKind::Texture;
        entryBarrier.before = entryState;
        entryBarrier.after = entryState;
        entryBarrier.srcStage = PipelineStage::TopOfPipe;
        entryBarrier.dstStage = PipelineStage::FragmentShader;
        entryBarrier.srcAccess = rhi::ResourceAccess::None;
        entryBarrier.dstAccess = rhi::ResourceAccess::ShaderRead;
        entryBarrier.oldLayout = "ShaderRead";
        entryBarrier.newLayout = "ShaderRead";
        entryBarrier.aspect = TextureAspect::Color;
        parentCompiled.addBarrier(entryBarrier);
    }

    // ── 5. Emit output pin exit barriers ──
    for (const auto& pin : recipe_.outputPins) {
        // Find the last local pass that writes to this output pin.
        bool hasProducer = false;
        for (const auto& lt : localCompiled.resourceLifetimes()) {
            if (lt.resourceName == pin.name && !lt.producerPassName.empty()) {
                hasProducer = true;
                break;
            }
        }

        if (!hasProducer) {
            throw std::runtime_error(arp::core::toStdString(std::pmr::string("subgraph \"")
                + recipe_.name + std::pmr::string("\" output pin \"") + pin.name
                + std::pmr::string("\" has no producer pass")));
        }

        ResourceState exitState = ResourceState::ShaderRead;
        switch (pin.usage) {
        case PinUsage::ColorAttachment:
            exitState = ResourceState::ColorAttachment;
            break;
        case PinUsage::DepthAttachment:
            exitState = ResourceState::DepthAttachment;
            break;
        case PinUsage::SampledTexture:
            exitState = ResourceState::ShaderRead;
            break;
        case PinUsage::StorageTexture:
            exitState = ResourceState::ShaderWrite;
            break;
        case PinUsage::StorageBuffer:
            exitState = ResourceState::ShaderWrite;
            break;
        case PinUsage::CopySource:
            exitState = ResourceState::CopySrc;
            break;
        case PinUsage::CopyDestination:
            exitState = ResourceState::CopyDst;
            break;
        }

        // Resolve the output pin's real parent handle (migrated by
        // migratePassesAndTextures). The exit barrier carries the parent
        // handleId so the executor can resolve the resource on replay.
        TextureHandle parentHandle{};
        TextureHandle localPinHandle{};
        bool resolved = false;
        for (const auto& lt : localGraph_->textures_) {
            if (lt.desc.label == pin.name) {
                localPinHandle = lt.handle;
                auto it = localToParentTexture_.find(lt.handle.id);
                if (it != localToParentTexture_.end()) {
                    parentHandle = it->second;
                    resolved = true;
                }
                break;
            }
        }
        if (!resolved) {
            throw std::runtime_error(arp::core::toStdString(std::pmr::string("subgraph \"")
                + recipe_.name + std::pmr::string("\" output pin \"") + pin.name
                + std::pmr::string("\" has no migrated parent handle")));
        }

        // Phase 6 [P0-B-后续]: find the last write state to this output pin
        // in local passes, so the exit barrier's before reflects the state
        // the subgraph actually leaves the resource in — not a no-op
        // exitState. The after remains the pin's exit contract state.
        ResourceState lastWriteState = exitState;
        for (const auto& lp : localGraph_->passes_) {
            for (const auto& w : lp.writes) {
                if (w.handle.id == localPinHandle.id) {
                    lastWriteState = w.state;
                }
            }
            for (const auto& aw : lp.attachmentWrites) {
                if (aw.handle.id == localPinHandle.id) {
                    lastWriteState = aw.state;
                }
            }
        }

        BarrierTraceRecord exitBarrier;
        exitBarrier.passName = recipe_.name + std::pmr::string("_OutputPin_") + pin.name;
        exitBarrier.resourceName = pin.name;
        exitBarrier.handleId = parentHandle.id;
        exitBarrier.kind = ResourceKind::Texture;
        exitBarrier.before = lastWriteState;
        exitBarrier.after = exitState;
        exitBarrier.srcStage = PipelineStage::FragmentShader;
        exitBarrier.dstStage = PipelineStage::Present;
        exitBarrier.srcAccess = rhi::ResourceAccess::ShaderRead;
        exitBarrier.dstAccess = rhi::ResourceAccess::None;
        exitBarrier.oldLayout = "ShaderRead";
        exitBarrier.newLayout = "Present";
        exitBarrier.aspect = TextureAspect::Color;
        parentCompiled.addBarrier(exitBarrier);
    }

    // ── 6. Record CompiledSubgraph metadata ──
    CompiledSubgraph meta;
    meta.name = recipe_.name;
    meta.globalPassBegin = passRange_.first;
    meta.globalPassEnd = passRange_.second;
    meta.aliasScopeId = aliasScopeId_;
    meta.aliasScopePolicy = recipe_.aliasScope;
    meta.culledAsUnit = false;
    for (const auto& [pinName, handle] : pinBindings_) {
        meta.pinBindings.emplace_back(pinName, handle);
    }
    parentCompiled.addSubgraph(std::move(meta));
}

// ── CompiledFrameGraph Phase 6 ──

void CompiledFrameGraph::addSubgraph(CompiledSubgraph subgraph)
{
    invalidateRetirementSource();
    subgraphs_.push_back(std::move(subgraph));
}

const std::pmr::vector<CompiledSubgraph>& CompiledFrameGraph::subgraphs() const
{
    return subgraphs_;
}

} // namespace arp::render_graph
