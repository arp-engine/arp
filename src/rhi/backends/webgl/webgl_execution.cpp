#include "rhi/backends/webgl/webgl_execution.h"

#include "rhi/backends/webgl/webgl_canvas_internal.h"
#include "rhi/backends/webgl/webgl_descriptors_internal.h"
#include "rhi/backends/webgl/webgl_pipeline_internal.h"
#include "rhi/frontend/command_attachment_validation.h"
#include "rhi/frontend/command_encoder_base.h"
#include "rhi/frontend/command_ordered_issue.h"
#include "rhi/frontend/resource_access_usage.h"
#include "rhi/frontend/resource_state_sequence.h"
#include "rhi/gpu_device.h"
#include "rhi/semantics/attachment_content.h"
#include "rhi/semantics/ordered_graphics_dependency.h"
#include "rhi/semantics/push_constant_validation.h"
#include "rhi/semantics/viewport_scissor_validation.h"
#include "rhi/semantics/webgl_shader_coordinates.h"
#include "shader/webgl_uniform_snapshots.h"

#include <cmath>
#include <cstring>
#ifdef __EMSCRIPTEN__
#define GL_GLEXT_PROTOTYPES
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#endif

namespace arp::rhi::webgl {
namespace {
struct VertexBinding {
    std::shared_ptr<WebGlBufferPayload> buffer;
    std::uint64_t offset = 0, revision = 0;
};

struct TextureUse {
    std::shared_ptr<WebGlTexturePayload> texture;
    ResourceState initialState = ResourceState::Undefined, finalState = ResourceState::Undefined;
    ResourceState simulatedState = ResourceState::Undefined;
    std::uint64_t nativeRevision = 0, stateRevision = 0, recordedWrites = 0, simulatedWrites = 0;
    std::uint32_t ownedReads = 0;
    bool recordedColor = false, recordedDepth = false, recordedStencil = false;
    bool simulatedColor = false, simulatedDepth = false, simulatedStencil = false;
    bool transitioned = false;
    std::optional<common::RecordedTextureStateControl::PendingObservation> pending;
};

struct BufferUse {
    std::shared_ptr<WebGlBufferPayload> buffer;
    ResourceState initialState = ResourceState::Undefined, finalState = ResourceState::Undefined;
    ResourceState simulatedState = ResourceState::Undefined;
    std::uint64_t nativeRevision = 0, stateRevision = 0;
    bool transitioned = false;
    std::optional<common::RecordedResourceStateControl::PendingObservation> pending;
};

struct TextureBinding {
    std::shared_ptr<WebGlTextureViewPayload> view;
    std::shared_ptr<WebGlSamplerPayload> sampler;
    std::uint64_t revision = 0;
    TextureUse* use = nullptr;
};

struct Draw {
    std::shared_ptr<WebGlRenderPipeline::Impl> pipeline;
    core::pmr::Vector<VertexBinding> vertices{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<TextureBinding> textures{&core::defaultCpuMemoryResource()};
    std::shared_ptr<WebGlBufferPayload> index;
    std::uint64_t indexOffset = 0, indexRevision = 0;
    ViewportState viewport;
    ScissorState scissor;
    std::uint32_t count = 0, first = 0, snapshot = UINT32_MAX, instanceCount = 1;
    IndexFormat indexFormat = IndexFormat::Uint16;
    bool instanced = false;
    std::uint32_t pass = 0, uniformBank = 0;
    bool indexed = false, locked = false;

    const char* lockDiagnostic(
        const std::shared_ptr<WebGlBufferPayload>& indexSource) const noexcept
    {
        for (const auto& v : vertices) {
            const auto count = std::count_if(vertices.begin(),
                                   vertices.end(),
                                   [&](const auto& other) {
                                       return other.buffer == v.buffer;
                                   })
                + (indexSource == v.buffer);
            if (std::uint64_t(count) > UINT32_MAX - v.buffer->retainedReads)
                return "RHI-WEBGL1-RETAINED-READ-EXHAUSTED";
        }
        if (indexSource && indexSource->retainedReads == UINT32_MAX)
            return "RHI-WEBGL1-RETAINED-READ-EXHAUSTED";
        for (const auto& t : textures) {
            const auto count =
                std::count_if(textures.begin(), textures.end(), [&](const auto& other) {
                    return other.view->texture == t.view->texture;
                });
            if (std::uint64_t(count) > UINT32_MAX - t.view->texture->retainedReads)
                return "RHI-WEBGL1-RETAINED-READ-EXHAUSTED";
        }
        return nullptr;
    }

    void lock() noexcept
    {
        for (auto& v : vertices)
            ++v.buffer->retainedReads;
        for (auto& t : textures)
            ++t.view->texture->retainedReads;
        if (index)
            ++index->retainedReads;
        locked = true;
    }

    void unlock() noexcept
    {
        if (!locked)
            return;
        for (auto& v : vertices)
            --v.buffer->retainedReads;
        for (auto& t : textures)
            --t.view->texture->retainedReads;
        if (index)
            --index->retainedReads;
        locked = false;
    }
};

struct SetBinding {
    std::uint32_t set = 0;
    const WebGlDescriptorSet* object = nullptr;
    core::LifetimeEpoch::Ticket lifetime;
};

struct PreparedPass {
    std::shared_ptr<WebGlFramebuffer> framebuffer;
    const WebGlFramebuffer::Impl* target = nullptr;
    RenderPassColorAttachmentDesc color;
    RenderPassDepthStencilAttachmentDesc depth;
    std::uint64_t canvasRevision = 0;
};

struct TextureTransition {
    TextureUse* use = nullptr;
    BufferUse* buffer = nullptr;
    ResourceBarrierDesc barrier;
    std::uint32_t beforeOperation = 0;
    std::shared_ptr<WebGlCanvasPayload> canvas;
};

struct CommandState {
    std::shared_ptr<WebGlResourceDomain> domain;
    std::shared_ptr<WebGlRenderPipeline::Impl> pipeline;
    core::pmr::Vector<std::shared_ptr<WebGlRenderPipeline::Impl>> pipelines{
        &core::defaultCpuMemoryResource()};
    std::shared_ptr<WebGlFramebuffer> framebuffer;
    const WebGlFramebuffer::Impl* target = nullptr;
    core::pmr::Vector<std::shared_ptr<WebGlFramebuffer>> targets{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<const WebGlFramebuffer::Impl*> targetPayloads{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<PreparedPass> passes{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<TextureUse> textureUses{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<TextureTransition> transitions{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<BufferUse> bufferUses{&core::defaultCpuMemoryResource()};
    std::shared_ptr<core::MutationJournal> journal;
    semantics::WebGlPreparedCommandsRequest request;
    std::shared_ptr<core::CpuMemoryQuota> quota;
    core::UniquePtr<common::OrderedCommandIssue> issue;

    struct UniformBank {
        shader::WebGlUniformSnapshots snapshots;
        core::UniquePtr<shader::WebGlCpuUniformData> push;
    };

    core::pmr::Vector<UniformBank> uniformBanks{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::uint32_t> pipelineBanks{&core::defaultCpuMemoryResource()};
    std::uint32_t activeBank = 0;

    shader::WebGlUniformSnapshots& snapshots() noexcept
    {
        return uniformBanks[activeBank].snapshots;
    }

    const shader::WebGlUniformSnapshots& snapshots() const noexcept
    {
        return uniformBanks[activeBank].snapshots;
    }

    core::UniquePtr<shader::WebGlCpuUniformData>& push() noexcept
    {
        return uniformBanks[activeBank].push;
    }

    core::pmr::Vector<shader::WebGlUniformSnapshots::Binding> sources{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::shared_ptr<const void>> owners{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<SetBinding> sets{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<VertexBinding> vertices{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<Draw> draws{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<TextureDesc> sampledShapes{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<semantics::WebGlSampledBindingRequest> sampled{
        &core::defaultCpuMemoryResource()};
    semantics::PushConstantConsumptionState pushConsumption;
    VertexBinding index;
    IndexFormat indexFormat = IndexFormat::Uint16;
    ViewportState viewport;
    ScissorState scissor;
    RenderPassColorAttachmentDesc color;
    RenderPassDepthStencilAttachmentDesc depth;
    std::uint32_t drawCount = 0, commandCount = 0;
    std::uint32_t passCount = 0, currentPass = UINT32_MAX, textureUseCount = 0, transitionCount = 0;
    std::uint32_t bufferUseCount = 0;
    bool simulating = false;
    bool bound = false, viewportSet = false, scissorSet = false, pushInPass = false;
    bool begun = false, open = false, ended = false, finished = false;
    CommandValidationResult validation{true, "", 0, CommandValidationStatus::Success, ""};

    CommandValidationResult* encoderValidation = nullptr;

    ~CommandState()
    {
        for (auto& d : draws)
            d.unlock();
    }

    CommandValidationResult result(const char* id = nullptr)
    {
        if (!id)
            ++commandCount;
        validation = {id == nullptr,
            id ? id : "",
            commandCount,
            id ? CommandValidationStatus::DiagnosticFailure : CommandValidationStatus::Success,
            id ? id : ""};
        if (encoderValidation)
            *encoderValidation = validation;
        return validation;
    }

    const char* current() const noexcept
    {
        if (const auto* id = domain->currentDiagnostic())
            return id;
        if (const auto* id = framebuffer->currentDiagnostic())
            return id;
        return WebGlRenderPipeline::current(*pipeline);
    }

    const char* recording(std::uint32_t ordinal) const noexcept
    {
        if (const auto* id = current())
            return id;
        return open && !finished && ordinal == currentPass ? nullptr
                                                           : "RHI-WEBGL1-RENDER-PASS-NOT-OPEN";
    }

    const WebGlDescriptorSet::Cell* cell(std::uint32_t set, std::uint32_t binding) const noexcept
    {
        for (const auto& s : sets) {
            if (s.set != set || !s.object || !s.lifetime.isCurrent())
                continue;
            if (!s.object->isCurrentAllocation())
                return nullptr;
            const auto* c = s.object->cell(binding);
            return c && c->write && c->object && c->object->current() ? c : nullptr;
        }
        return nullptr;
    }

    const char* bindings(Draw&) noexcept;
    const char* preflightDraw(const Draw&) const noexcept;
    TextureUse* track(const std::shared_ptr<WebGlTexturePayload>&) noexcept;

    TextureUse* find(const WebGlTexturePayload* p) noexcept
    {
        for (std::uint32_t i = 0; i < textureUseCount; ++i)
            if (textureUses[i].texture.get() == p)
                return &textureUses[i];
        return nullptr;
    }

    BufferUse* track(const std::shared_ptr<WebGlBufferPayload>&) noexcept;

    BufferUse* find(const WebGlBufferPayload* p) const noexcept
    {
        for (std::uint32_t i = 0; i < bufferUseCount; ++i)
            if (bufferUses[i].buffer.get() == p)
                return const_cast<BufferUse*>(&bufferUses[i]);
        return nullptr;
    }

    bool readable(const WebGlBufferPayload& p) const noexcept
    {
        const auto* use = find(&p);
        return use && p.state.active()
            && (simulating ? use->simulatedState : p.state.state()) == ResourceState::ShaderRead;
    }

    const char* preflight() noexcept;
    void recordPassContents(const PreparedPass&) noexcept;

    static std::uint32_t beginMarker(std::uint32_t pass) noexcept
    {
        return UINT32_MAX - 1 - 2 * pass;
    }

    static std::uint32_t endMarker(std::uint32_t pass) noexcept { return UINT32_MAX - 2 * pass; }
};

bool initializedColor(const WebGlTexturePayload& p) noexcept
{
    return p.nativeContentsValid
        && (p.gpuColorInitialized
            || (p.cpuContentCurrent && p.content && p.content->initialized(0, p.content->size())));
}

TextureUse* CommandState::track(const std::shared_ptr<WebGlTexturePayload>& p) noexcept
{
    if (auto* existing = find(p.get()))
        return existing;
    if (textureUseCount == textureUses.size() || !p->state.active() || !p->state.uniformWholeState()
        || !p->state.canPrepare(*journal))
        return nullptr;
    auto& use = textureUses[textureUseCount++];
    use.texture = p;
    use.initialState = *p->state.uniformWholeState();
    use.nativeRevision = p->revision;
    use.recordedColor = initializedColor(*p);
    use.recordedDepth = p->nativeContentsValid && p->gpuDepthInitialized;
    use.recordedStencil = p->nativeContentsValid && p->gpuStencilInitialized;
    return &use;
}

BufferUse* CommandState::track(const std::shared_ptr<WebGlBufferPayload>& p) noexcept
{
    if (auto* existing = find(p.get()))
        return existing;
    if (bufferUseCount == bufferUses.size() || !p->state.canPrepare(*journal))
        return nullptr;
    auto& use = bufferUses[bufferUseCount++];
    use.buffer = p;
    use.initialState = p->state.state();
    use.nativeRevision = p->revision;
    return &use;
}

void CommandState::recordPassContents(const PreparedPass& pass) noexcept
{
    if (pass.target->canvas)
        return;
    auto& c = *find(pass.target->color.get());
    c.recordedColor = semantics::AttachmentContentContract::afterStore(
        pass.color.loadOp, pass.color.storeOp, c.recordedColor);
    ++c.recordedWrites;
    if (pass.target->depthStencil) {
        auto& z = *find(pass.target->depthStencil.get());
        z.recordedDepth = semantics::AttachmentContentContract::afterStore(
            pass.depth.depthLoadOp, pass.depth.depthStoreOp, z.recordedDepth);
        z.recordedStencil = semantics::AttachmentContentContract::afterStore(
            pass.depth.stencilLoadOp, pass.depth.stencilStoreOp, z.recordedStencil);
        ++z.recordedWrites;
    }
}

const char* CommandState::bindings(Draw& d) noexcept
{
    for (std::size_t i = 0; i < snapshots().slots().size(); ++i) {
        const auto& slot = snapshots().slots()[i];
        auto& b = sources[i];
        b = {};
        b.push = slot.push;
        b.set = slot.set;
        b.binding = slot.binding;
        b.dynamicAlignment = sizeof(std::uint32_t);
        if (slot.push) {
            if (!pushInPass)
                return "RHI-WEBGL1-PUSH-NOT-INITIALIZED-IN-PASS";
            b.source = push().get();
            b.range = slot.size;
            continue;
        }
        const auto* c = cell(slot.set, slot.binding);
        auto p = c ? std::dynamic_pointer_cast<WebGlBufferPayload>(c->object) : nullptr;
        if (!p || c->write->type != DescriptorType::UniformBuffer || !p->content
            || p->state.state() != ResourceState::ShaderRead)
            return "RHI-WEBGL1-DRAW-UNIFORM-SOURCE";
        DescriptorBufferBindingRangeContract::Resolved range;
        if (const auto* id = DescriptorBufferBindingRangeContract::resolve(
                c->write->bufferBindingRange, p->desc.sizeBytes, 0, range))
            return id;
        b.cpuBytes = p->content.get();
        b.offset = range.offsetBytes;
        b.range = range.sizeBytes;
    }
    const auto& program = WebGlRenderPipeline::program(*pipeline);
    for (auto* stage : {&program.artifact->vertex(), &program.artifact->fragment()}) {
        for (const auto& pair : stage->pairs) {
            std::size_t unit = 0;
            while (unit < program.artifact->textureUnits().size()
                && program.artifact->textureUnits()[unit] != pair.name)
                ++unit;
            if (unit >= d.textures.size())
                return "RHI-WEBGL1-DRAW-SAMPLER-ABI";
            const auto* tc = cell(pair.textureSet, pair.textureBinding);
            const auto* sc = cell(pair.samplerSet, pair.samplerBinding);
            auto view =
                tc ? std::dynamic_pointer_cast<WebGlTextureViewPayload>(tc->object) : nullptr;
            auto sampler =
                sc ? std::dynamic_pointer_cast<WebGlSamplerPayload>(sc->object) : nullptr;
            if (!view || !sampler || tc->write->type != DescriptorType::SampledTexture
                || sc->write->type != DescriptorType::Sampler || !view->texture->current())
                return "RHI-WEBGL1-DRAW-SAMPLED-SOURCE";
            auto& texture = *view->texture;
            if (texture.id == target->colorId()
                || (target->depthStencil && texture.id == target->depthStencil->id))
                return "RHI-WEBGL1-DRAW-ATTACHMENT-FEEDBACK";
            auto* use = track(view->texture);
            if (!use)
                return "RHI-WEBGL1-TEXTURE-STATE-INVENTORY";
            if (texture.state.uniformWholeState() != ResourceState::ShaderRead
                || !(isDepthTextureFormat(texture.desc.format) ? use->recordedDepth
                                                               : use->recordedColor)
                || use->recordedWrites > UINT64_MAX - texture.revision)
                return "RHI-WEBGL1-DRAW-TEXTURE-UNINITIALIZED";
            d.textures[unit] = {
                std::move(view), std::move(sampler), texture.revision + use->recordedWrites, use};
        }
    }
    for (std::size_t i = 0; i < d.textures.size(); ++i) {
        const auto& t = d.textures[i];
        if (!t.view || !t.sampler)
            return "RHI-WEBGL1-DRAW-SAMPLER-COVERAGE";
        const auto& original = t.view->texture->desc;
        auto& shape = sampledShapes[i];
        shape.extent = original.extent;
        shape.format = original.format;
        shape.mipLevelCount = original.mipLevelCount;
        shape.arrayLayerCount = original.arrayLayerCount;
        shape.sampleCount = original.sampleCount;
        shape.cubeCompatible = original.cubeCompatible;
        // ResourceCreation/1's cold CopyDst role is not a sampling role. The
        // immutable original already passed that composed creation contract.
        shape.usage = static_cast<TextureUsage>(static_cast<std::uint32_t>(original.usage)
            & ~static_cast<std::uint32_t>(TextureUsage::CopyDst));
        sampled[i] = {t.view->texture->id, &shape, &t.sampler->definition};
    }
    const auto decision = semantics::WebGlGraphicsRequestContract::sampledBindings(
        domain->runtime->profile(), 1, {sampled.data(), sampled.size()}, domain->executionOptIn());
    return decision.compatible ? nullptr : decision.diagnostic;
}

const char* CommandState::preflightDraw(const Draw& d) const noexcept
{
    if (!d.pipeline)
        return "RHI-WEBGL1-DRAW-PIPELINE-MISSING";
    if (const auto* id = WebGlRenderPipeline::current(*d.pipeline))
        return id;
    if (!d.locked || !d.count || d.uniformBank >= uniformBanks.size()
        || d.snapshot >= uniformBanks[d.uniformBank].snapshots.size())
        return "RHI-WEBGL1-DRAW-PAYLOAD";
    for (const auto& v : d.vertices)
        if (!v.buffer || !v.buffer->current() || !v.buffer->native
            || v.buffer->revision != v.revision || !readable(*v.buffer))
            return "RHI-WEBGL1-DRAW-VERTEX-STALE";
    if (d.indexed
        && (!d.index || !d.index->current() || !d.index->native
            || d.index->revision != d.indexRevision || !readable(*d.index)))
        return "RHI-WEBGL1-DRAW-INDEX-STALE";
    for (const auto& t : d.textures) {
        if (!t.view || !t.view->current() || !t.sampler || !t.sampler->current()
            || !t.view->texture->current() || !t.view->texture->native || !t.use
            || t.use->texture != t.view->texture)
            return "RHI-WEBGL1-DRAW-TEXTURE-STALE";
        if (simulating ? (t.use->nativeRevision + t.use->simulatedWrites != t.revision
                             || !(isDepthTextureFormat(t.view->texture->desc.format)
                                     ? t.use->simulatedDepth
                                     : t.use->simulatedColor)
                             || t.use->simulatedState != ResourceState::ShaderRead)
                       : (t.view->texture->revision != t.revision
                             || !(isDepthTextureFormat(t.view->texture->desc.format)
                                     ? (t.view->texture->nativeContentsValid
                                           && t.view->texture->gpuDepthInitialized)
                                     : initializedColor(*t.view->texture))))
            return "RHI-WEBGL1-DRAW-TEXTURE-STALE";
    }
    return nullptr;
}

const char* CommandState::preflight() noexcept
{
    for (std::uint32_t i = 0; i < textureUseCount; ++i) {
        auto& use = textureUses[i];
        auto& texture = *use.texture;
        if (!texture.current() || texture.revision != use.nativeRevision || !texture.state.active()
            || texture.state.revision() != use.stateRevision
            || texture.state.uniformWholeState() != use.finalState
            || (use.transitioned
                && (!use.pending || !texture.state.matchesPending(*use.pending, *journal))))
            return "RHI-WEBGL1-SUBMIT-TEXTURE-STATE-STALE";
        use.simulatedState = use.initialState;
        use.simulatedWrites = 0;
        use.simulatedColor = initializedColor(texture);
        use.simulatedDepth = texture.nativeContentsValid && texture.gpuDepthInitialized;
        use.simulatedStencil = texture.nativeContentsValid && texture.gpuStencilInitialized;
    }
    for (std::uint32_t i = 0; i < bufferUseCount; ++i) {
        auto& use = bufferUses[i];
        const auto& buffer = *use.buffer;
        if (!buffer.current() || (use.transitioned && buffer.revision != use.nativeRevision)
            || !buffer.state.active() || buffer.state.revision() != use.stateRevision
            || buffer.state.state() != use.finalState
            || (use.transitioned
                && (!use.pending || !buffer.state.matchesPending(*use.pending, *journal))))
            return "RHI-WEBGL1-SUBMIT-BUFFER-STATE-STALE";
        use.simulatedState = use.initialState;
    }
    simulating = true;
    const auto validation = [&]() -> const char* {
        std::uint32_t nextPass = 0, nextDraw = 0, nextTransition = 0;
        bool inPass = false;
        auto applyTransitions = [&](std::size_t ordinal) -> const char* {
            while (nextTransition < transitionCount
                && transitions[nextTransition].beforeOperation == ordinal) {
                const auto& transition = transitions[nextTransition++];
                if (inPass || (!transition.use && !transition.buffer && !transition.canvas))
                    return "RHI-WEBGL1-SUBMIT-TRANSITION-ORDER";
                if (transition.canvas) {
                    const auto& canvas = *transition.canvas;
                    if (const auto* id = canvas.diagnostic())
                        return id;
                    if (canvas.phase != WebGlCanvasPayload::Phase::Acquired
                        || canvas.state.pending() || transition.barrier.resource != canvas.id)
                        return "RHI-WEBGL1-SUBMIT-TRANSITION-ORDER";
                    if (const auto* id = semantics::RecordedResourceStateSequence::validateBarrier(
                            transition.barrier,
                            canvas.state.uniformWholeState().value_or(ResourceState::Undefined)))
                        return id;
                    continue;
                }
                auto& state = transition.use ? transition.use->simulatedState
                                             : transition.buffer->simulatedState;
                if (const auto* id = semantics::RecordedResourceStateSequence::validateBarrier(
                        transition.barrier, state))
                    return id;
                state = transition.barrier.after;
            }
            return nullptr;
        };
        const auto epoch = domain->runtime->epoch();
        for (std::size_t i = 0; i < issue->operationCount(); ++i) {
            if (const auto* id = applyTransitions(i))
                return id;
            common::OrderedCommandIssue::PayloadView view;
            if (const auto* id = issue->inspectSealed(epoch, i, view))
                return id;
            if (view.size != sizeof(std::uint32_t))
                return "RHI-WEBGL1-SUBMIT-PAYLOAD";
            std::uint32_t value;
            std::memcpy(&value, view.data, sizeof(value));
            if (nextPass >= passCount)
                return "RHI-WEBGL1-SUBMIT-ORDER";
            auto& pass = passes[nextPass];
            if (!inPass && value == beginMarker(nextPass)) {
                if (const auto* id = pass.framebuffer->currentDiagnostic())
                    return id;
                if (pass.target->canvas) {
                    const auto& canvas = *pass.target->canvas;
                    if (canvas.phase != WebGlCanvasPayload::Phase::Acquired
                        || canvas.state.revision() != pass.canvasRevision
                        || canvas.state.uniformWholeState() != ResourceState::ColorAttachment
                        || canvas.state.pending())
                        return "RHI-WEBGL1-SUBMIT-CANVAS-LEASE";
                    inPass = true;
                    continue;
                }
                auto* c = find(pass.target->color.get());
                auto* z =
                    pass.target->depthStencil ? find(pass.target->depthStencil.get()) : nullptr;
                if (!c || (pass.target->depthStencil && !z))
                    return "RHI-WEBGL1-SUBMIT-ATTACHMENT-INVENTORY";
                if (c->simulatedState != ResourceState::ColorAttachment
                    || (z && z->simulatedState != ResourceState::DepthAttachment))
                    return "RHI-WEBGL1-PREPARED-ATTACHMENT-STATE";
                if (c->nativeRevision >= UINT64_MAX - c->simulatedWrites
                    || (z && z->nativeRevision >= UINT64_MAX - z->simulatedWrites))
                    return "RHI-WEBGL1-RESOURCE-REVISION-EXHAUSTED";
                if (c->texture->retainedReads != c->ownedReads
                    || (z && z->texture->retainedReads != z->ownedReads))
                    return "RHI-WEBGL1-ATTACHMENT-RETAINED-READ";
                if ((pass.color.loadOp == LoadOp::Load && !c->simulatedColor)
                    || (z
                        && ((pass.depth.depthLoadOp == LoadOp::Load && !z->simulatedDepth)
                            || (pass.depth.stencilLoadOp == LoadOp::Load && !z->simulatedStencil))))
                    return "RHI-WEBGL1-ATTACHMENT-LOAD-UNINITIALIZED";
                inPass = true;
            } else if (inPass && value == endMarker(nextPass)) {
                if (pass.target->canvas) {
                    inPass = false;
                    ++nextPass;
                    continue;
                }
                auto& c = *find(pass.target->color.get());
                c.simulatedColor = semantics::AttachmentContentContract::afterStore(
                    pass.color.loadOp, pass.color.storeOp, c.simulatedColor);
                ++c.simulatedWrites;
                if (pass.target->depthStencil) {
                    auto& z = *find(pass.target->depthStencil.get());
                    z.simulatedDepth = semantics::AttachmentContentContract::afterStore(
                        pass.depth.depthLoadOp, pass.depth.depthStoreOp, z.simulatedDepth);
                    z.simulatedStencil = semantics::AttachmentContentContract::afterStore(
                        pass.depth.stencilLoadOp, pass.depth.stencilStoreOp, z.simulatedStencil);
                    ++z.simulatedWrites;
                }
                inPass = false;
                ++nextPass;
            } else if (inPass && value == nextDraw && value < drawCount
                && draws[value].pass == nextPass) {
                if (const auto* id = preflightDraw(draws[value]))
                    return id;
                ++nextDraw;
            } else {
                return "RHI-WEBGL1-SUBMIT-ORDER";
            }
        }
        if (const auto* id = applyTransitions(issue->operationCount()))
            return id;
        if (inPass || nextPass != passCount || nextDraw != drawCount
            || nextTransition != transitionCount)
            return "RHI-WEBGL1-SUBMIT-ORDER";
        for (std::uint32_t i = 0; i < textureUseCount; ++i)
            if (textureUses[i].simulatedState != textureUses[i].finalState)
                return "RHI-WEBGL1-SUBMIT-TEXTURE-FINAL-STATE";
        for (std::uint32_t i = 0; i < bufferUseCount; ++i)
            if (bufferUses[i].simulatedState != bufferUses[i].finalState)
                return "RHI-WEBGL1-SUBMIT-BUFFER-FINAL-STATE";
        return nullptr;
    }();
    simulating = false;
    return validation;
}

class RenderPass final : public RenderPassEncoder {
public:
    explicit RenderPass(std::shared_ptr<CommandState> s, std::uint32_t ordinal)
        : s_(std::move(s)), ordinal_(ordinal)
    {}

    CommandValidationResult setPipeline(const RenderPipeline&) override;
    CommandValidationResult setViewport(const ViewportState&) override;
    CommandValidationResult setScissor(const ScissorState&) override;
    CommandValidationResult bindVertexBuffer(
        std::uint32_t, const GpuBuffer&, std::uint64_t) override;
    CommandValidationResult bindIndexBuffer(const GpuBuffer&, IndexFormat, std::uint64_t) override;
    CommandValidationResult bindDescriptorSet(
        std::uint32_t, const DescriptorSet&, const std::uint32_t*, std::uint32_t) override;
    CommandValidationResult pushConstants(
        ShaderStage, std::uint32_t, std::uint32_t, const void*) override;
    CommandValidationResult draw(
        std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override;
    CommandValidationResult drawIndexed(
        std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) override;

    CommandValidationResult bindDescriptorTable(std::uint32_t, const DescriptorTable&) override
    {
        return unsupported();
    }

    CommandValidationResult executeSecondary(const SecondaryCommandBuffer&) override
    {
        return unsupported();
    }

    CommandValidationResult executeRenderBundle(const RenderBundle&) override
    {
        return unsupported();
    }

    CommandValidationResult drawIndexedIndirect(
        const GpuBuffer&, std::uint64_t, std::uint32_t, std::uint32_t) override
    {
        return unsupported();
    }

    CommandValidationResult drawIndexedIndirectCount(const GpuBuffer&,
        std::uint64_t,
        const GpuBuffer&,
        std::uint64_t,
        std::uint32_t,
        std::uint32_t) override
    {
        return unsupported();
    }

    CommandValidationResult writeTimestamp(const QueryPool&, std::uint32_t) override
    {
        return unsupported();
    }

    CommandValidationResult nextSubpass() override { return unsupported(); }

    std::uint32_t currentSubpassIndex() const override { return 0; }

    CommandValidationResult end() override;

private:
    CommandValidationResult unsupported()
    {
        return s_->result("RHI-WEBGL1-PREPARED-COMMAND-UNSUPPORTED");
    }

    CommandValidationResult recordDraw(
        bool, std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t);
    std::shared_ptr<CommandState> s_;
    std::uint32_t ordinal_;
};

CommandValidationResult RenderPass::setPipeline(const RenderPipeline& value)
{
    auto& s = *s_;
    if (const auto* id = s.recording(ordinal_))
        return s.result(id);
    const auto* p = dynamic_cast<const WebGlRenderPipeline*>(&value);
    if (!p || std::find(s.pipelines.begin(), s.pipelines.end(), p->impl) == s.pipelines.end())
        return s.result("RHI-WEBGL1-PREPARED-PIPELINE-NOT-DECLARED");
    const bool sameAbi = WebGlRenderPipeline::compatiblePreparedInputs(*s.pipeline, *p->impl);
    if (const auto* id = semantics::PreparedGraphicsCommandsContract::pipelineSwitch(
            s.request, sameAbi, s.bound))
        return s.result(id);
    const auto ordinal = std::size_t(
        std::find(s.pipelines.begin(), s.pipelines.end(), p->impl) - s.pipelines.begin());
    const auto bank = s.pipelineBanks[ordinal];
    if (bank != s.activeBank) {
        s.activeBank = bank;
        s.sources.resize(s.snapshots().slots().size());
        s.pushInPass = false;
        const auto units = WebGlRenderPipeline::program(*p->impl).artifact->textureUnits().size();
        s.sampled.resize(units);
        s.sampledShapes.resize(units);
    }
    s.pipeline = p->impl;
    s.bound = true;
    const auto& d = s.pipeline->definition;
    if (!d.dynamicViewportScissor) {
        s.viewport = d.viewport;
        s.scissor = d.scissor;
        s.viewportSet = d.hasViewport;
        s.scissorSet = d.hasScissor;
    }
    return s.result();
}

CommandValidationResult RenderPass::setViewport(const ViewportState& v)
{
    auto& s = *s_;
    if (const auto* id = s.recording(ordinal_))
        return s.result(id);
    if (!s.bound || !s.pipeline->definition.dynamicViewportScissor)
        return s.result("RHI-WEBGL1-VIEWPORT-PIPELINE-STATE");
    if (const auto* id = semantics::viewportCommandDiagnostic(v))
        return s.result(id);
    const auto& size = s.target->extent();
    // L2 top-left pixel coordinates lower to bottom-left ES2. Fractional
    // coordinates have no exact ES2 integer mapping and reject.
    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.width)
        || !std::isfinite(v.height) || !std::isfinite(v.minDepth) || !std::isfinite(v.maxDepth)
        || std::floor(v.x) != v.x || std::floor(v.y) != v.y || std::floor(v.width) != v.width
        || std::floor(v.height) != v.height || v.x < 0 || v.y < 0
        || double(v.x) + v.width > size.width || double(v.y) + v.height > size.height
        || v.minDepth < 0 || v.maxDepth > 1)
        return s.result("RHI-WEBGL1-VIEWPORT-NOT-EQUIVALENT");
    s.viewport = v;
    s.viewportSet = true;
    return s.result();
}

CommandValidationResult RenderPass::setScissor(const ScissorState& v)
{
    auto& s = *s_;
    if (const auto* id = s.recording(ordinal_))
        return s.result(id);
    if (!s.bound || !s.pipeline->definition.dynamicViewportScissor)
        return s.result("RHI-WEBGL1-SCISSOR-PIPELINE-STATE");
    if (const auto* id = semantics::scissorCommandDiagnostic(v))
        return s.result(id);
    const auto& size = s.target->extent();
    if (v.x < 0 || v.y < 0 || std::uint64_t{v.width} + v.x > size.width
        || std::uint64_t{v.height} + v.y > size.height)
        return s.result("RHI-WEBGL1-SCISSOR-RANGE");
    s.scissor = v;
    s.scissorSet = true;
    return s.result();
}

CommandValidationResult RenderPass::bindVertexBuffer(
    std::uint32_t slot, const GpuBuffer& value, std::uint64_t offset)
{
    auto& s = *s_;
    if (const auto* id = s.recording(ordinal_))
        return s.result(id);
    const auto* p = dynamic_cast<const WebGlBuffer*>(&value);
    if (!p || p->payload->domain != s.domain || !p->payload->current()
        || (p->desc().usage & ResourceUsage::VertexBuffer) == ResourceUsage::None
        || offset >= p->desc().sizeBytes || offset % sizeof(float))
        return s.result("RHI-WEBGL1-VERTEX-BINDING");
    const auto& streams = s.pipeline->vertexInput.streams();
    for (std::size_t i = 0; i < streams.size(); ++i)
        if (streams[i].slot == slot) {
            s.vertices[i] = {p->payload, offset, p->payload->revision};
            return s.result();
        }
    return s.result("RHI-WEBGL1-PREPARED-VERTEX-SLOT-NOT-DECLARED");
}

CommandValidationResult RenderPass::bindIndexBuffer(
    const GpuBuffer& value, IndexFormat format, std::uint64_t offset)
{
    auto& s = *s_;
    if (const auto* id = s.recording(ordinal_))
        return s.result(id);
    const auto* p = dynamic_cast<const WebGlBuffer*>(&value);
    if (!p || p->payload->domain != s.domain || !p->payload->current()
        || (p->desc().usage & ResourceUsage::IndexBuffer) == ResourceUsage::None
        || (format != IndexFormat::Uint16 && format != IndexFormat::Uint32)
        || (format == IndexFormat::Uint32 && !s.domain->extensionDecision.native.uint32Indices)
        || offset >= p->desc().sizeBytes
        || offset % (format == IndexFormat::Uint32 ? sizeof(std::uint32_t) : sizeof(std::uint16_t)))
        return s.result("RHI-WEBGL1-INDEX-BINDING");
    s.index = {p->payload, offset, p->payload->revision};
    s.indexFormat = format;
    return s.result();
}

CommandValidationResult RenderPass::bindDescriptorSet(std::uint32_t set,
    const DescriptorSet& value,
    const std::uint32_t* offsets,
    std::uint32_t count)
{
    auto& s = *s_;
    if (const auto* id = s.recording(ordinal_))
        return s.result(id);
    const auto* p = dynamic_cast<const WebGlDescriptorSet*>(&value);
    if (!s.bound || !p || p->object->domain != s.domain || !p->isCurrentAllocation())
        return s.result("RHI-WEBGL1-DESCRIPTOR-BINDING");
    const auto& layout = s.pipeline->layout->definition;
    for (std::size_t i = 0; i < s.sets.size(); ++i) {
        if (s.sets[i].set != set)
            continue;
        const auto& slot = layout.descriptorSets[i];
        const auto* declaration = findPipelineDescriptorLayout(layout, slot.layoutName);
        if (!declaration || declaration->bindings.size() != p->layout().bindings.size())
            return s.result("RHI-WEBGL1-DESCRIPTOR-LAYOUT-MISMATCH");
        std::uint32_t expected = 0;
        for (const auto& b : declaration->bindings) {
            const auto found = std::find_if(
                p->layout().bindings.begin(), p->layout().bindings.end(), [&](const auto& other) {
                    return other.binding == b.binding;
                });
            if (found == p->layout().bindings.end() || found->type != b.type
                || found->stages != b.stages || found->arrayCount != b.arrayCount
                || found->dynamicOffset != b.dynamicOffset)
                return s.result("RHI-WEBGL1-DESCRIPTOR-LAYOUT-MISMATCH");
            expected += b.dynamicOffset;
        }
        if (count != expected || (count && !offsets))
            return s.result("RHI-WEBGL1-DYNAMIC-OFFSET-COUNT");
        for (std::uint32_t j = 0; j < count; ++j)
            if (offsets[j])
                return s.result("RHI-DESCRIPTOR-WHOLE-BUFFER-DYNAMIC-RANGE-UNPROVEN");
        s.sets[i].object = p;
        s.sets[i].lifetime = p->lifetimeTicket();
        return s.result();
    }
    return s.result("RHI-WEBGL1-DESCRIPTOR-SET-UNDECLARED");
}

CommandValidationResult RenderPass::pushConstants(
    ShaderStage stages, std::uint32_t offset, std::uint32_t bytes, const void* data)
{
    auto& s = *s_;
    if (const auto* id = s.recording(ordinal_))
        return s.result(id);
    auto a = semantics::validatePushConstantCommand(stages, offset, bytes, data);
    if (!a.success)
        return s.result(a.diagnostics.c_str());
    a = semantics::validatePushConstantBinding(s.bound ? s.pipeline->layout.get() : nullptr,
        ShaderStage::Vertex | ShaderStage::Fragment,
        stages,
        offset,
        bytes,
        &s.pipeline->package.reflection);
    if (!a.success)
        return s.result(a.diagnostics.c_str());
    if (s.pushConsumption.hasUnconsumedUpdate())
        return s.result("[PC-E013] RHI-WEBGL1-PUSH-UNCONSUMED");
    if (!s.push())
        return s.result("RHI-WEBGL1-PUSH-NOT-DECLARED");
    if (const auto* id = s.push()->update(offset, data, bytes))
        return s.result(id);
    s.pushInPass = true;
    s.pushConsumption.onPush();
    return s.result();
}

CommandValidationResult RenderPass::draw(
    std::uint32_t count, std::uint32_t instances, std::uint32_t first, std::uint32_t firstInstance)
{
    return recordDraw(false, count, instances, first, 0, firstInstance);
}

CommandValidationResult RenderPass::drawIndexed(std::uint32_t count,
    std::uint32_t instances,
    std::uint32_t first,
    std::int32_t base,
    std::uint32_t firstInstance)
{
    return recordDraw(true, count, instances, first, base, firstInstance);
}

CommandValidationResult RenderPass::recordDraw(bool indexed,
    std::uint32_t count,
    std::uint32_t instances,
    std::uint32_t first,
    std::int32_t base,
    std::uint32_t firstInstance)
{
    auto& s = *s_;
    if (const auto* id = s.recording(ordinal_))
        return s.result(id);
    if (!s.bound || !s.viewportSet || !s.scissorSet)
        return s.result("RHI-WEBGL1-DRAW-STATE-INCOMPLETE");
    if (s.pipeline->definition.depthFormat
        != (s.target->depthStencil ? s.target->depthStencil->desc.format : TextureFormat::Unknown))
        return s.result("RHI-WEBGL1-PREPARED-PIPELINE-ATTACHMENT-MISMATCH");
    if (s.drawCount >= s.request.maxDraws)
        return s.result("RHI-WEBGL1-DRAW-CAPACITY");
    auto& d = s.draws[s.drawCount];
    d.vertices.resize(s.vertices.size());
    d.textures.resize(s.sampled.size());
    semantics::WebGlDrawRequest request;
    request.contractVersion = 1;
    request.topology = s.pipeline->definition.topology;
    request.indexed = indexed;
    request.count = count;
    request.instanceCount = instances;
    request.firstVertex = indexed ? 0 : first;
    request.baseVertex = base;
    request.firstInstance = firstInstance;
    const auto indexBytes =
        s.indexFormat == IndexFormat::Uint32 ? sizeof(std::uint32_t) : sizeof(std::uint16_t);
    if (indexed) {
        request.indexFormat = s.indexFormat;
        if (!s.index.buffer)
            return s.result("RHI-WEBGL1-DRAW-INDEX-MISSING");
        request.indexBufferSize = s.index.buffer->desc.sizeBytes;
        request.indexOffset = s.index.offset + std::uint64_t{first} * indexBytes;
    }
    const auto admission = semantics::WebGlGraphicsRequestContract::draw(
        s.domain->runtime->profile(), request, s.domain->executionOptIn());
    if (!admission.compatible)
        return s.result(admission.diagnostic);
    IndexInputConsumptionContract::DrawRange range{0, 0, first, count, 0};
    if (indexed) {
        const auto& p = *s.index.buffer;
        if (!s.track(s.index.buffer))
            return s.result("RHI-WEBGL1-BUFFER-STATE-INVENTORY");
        if (!p.content || p.state.state() != ResourceState::ShaderRead
            || !p.content->initialized(request.indexOffset, std::uint64_t{count} * indexBytes))
            return s.result("RHI-WEBGL1-DRAW-INDEX-UNINITIALIZED");
        if (const auto* id =
                IndexInputConsumptionContract::analyzeLittleEndianBytes(p.content->data(),
                    p.content->size(),
                    s.indexFormat,
                    s.index.offset,
                    first,
                    count,
                    base,
                    s.request.maxAnalysisIndices,
                    range))
            return s.result(id);
    }
    for (std::size_t i = 0; i < s.vertices.size(); ++i) {
        const auto& v = s.vertices[i];
        const auto& stream = s.pipeline->vertexInput.streams()[i];
        if (!v.buffer || !v.buffer->current() || !v.buffer->content
            || v.buffer->state.state() != ResourceState::ShaderRead)
            return s.result("RHI-WEBGL1-DRAW-VERTEX-MISSING");
        if (const auto* id = VertexInputConsumptionContract::validateStreamRange(stream,
                v.buffer->desc.sizeBytes,
                v.offset,
                range.vertexCount,
                range.firstVertex,
                instances,
                firstInstance))
            return s.result(id);
        const bool instanceStream = stream.rate == VertexInputRate::Instance;
        const auto start =
            v.offset + (instanceStream ? firstInstance : range.firstVertex) * stream.strideBytes;
        const auto bytes =
            ((instanceStream ? instances : range.vertexCount) - 1) * stream.strideBytes
            + stream.fetchBytes;
        if (!v.buffer->content->initialized(start, bytes))
            return s.result("RHI-WEBGL1-DRAW-VERTEX-UNINITIALIZED");
        if (!s.track(v.buffer))
            return s.result("RHI-WEBGL1-BUFFER-STATE-INVENTORY");
        d.vertices[i] = {v.buffer, v.offset, v.buffer->revision};
    }
    if (const auto* id = s.bindings(d))
        return s.result(id);
    if (const auto* id = s.snapshots().validateRecord(s.sources.data(), s.sources.size()))
        return s.result(id);
    if (const auto* id = d.lockDiagnostic(indexed ? s.index.buffer : nullptr))
        return s.result(id);
    const auto ordinal = s.drawCount;
    std::size_t owners = 0;
    for (const auto& v : d.vertices)
        s.owners[owners++] = v.buffer;
    for (const auto& t : d.textures) {
        s.owners[owners++] = t.view;
        s.owners[owners++] = t.sampler;
    }
    if (indexed)
        s.owners[owners++] = s.index.buffer;
    s.owners[owners++] = s.pipeline;
    if (const auto* id =
            s.issue->recordOperation(s.owners.data(), owners, &ordinal, sizeof(ordinal)))
        return s.result(id);
    if (const auto* id = s.snapshots().record(s.sources.data(), s.sources.size(), d.snapshot)) {
        s.issue->fail(common::OrderedCommandIssue::Failure::PayloadValidation);
        s.open = false;
        return s.result(id);
    }
    d.indexed = indexed;
    d.pass = s.currentPass;
    d.pipeline = s.pipeline;
    d.uniformBank = s.activeBank;
    d.count = count;
    d.instanceCount = instances;
    d.indexFormat = s.indexFormat;
    d.instanced = instances > 1
        || std::any_of(s.pipeline->definition.vertexBuffers.begin(),
            s.pipeline->definition.vertexBuffers.end(),
            [](const auto& stream) {
                return stream.inputRate == VertexInputRate::Instance;
            });
    d.first = first;
    d.index = indexed ? s.index.buffer : nullptr;
    d.indexOffset = request.indexOffset;
    d.indexRevision = indexed ? s.index.buffer->revision : 0;
    d.viewport = s.viewport;
    d.scissor = s.scissor;
    d.lock();
    for (auto& texture : d.textures)
        ++texture.use->ownedReads;
    ++s.drawCount;
    s.pushConsumption.onConsume();
    return s.result();
}

CommandValidationResult RenderPass::end()
{
    auto& s = *s_;
    if (const auto* id = s.recording(ordinal_))
        return s.result(id);
    const std::uint32_t end = CommandState::endMarker(s.currentPass);
    if (const auto* id = s.issue->recordOperation(nullptr, 0, &end, sizeof(end)))
        return s.result(id);
    s.open = false;
    s.ended = true;
    s.recordPassContents(s.passes[s.currentPass]);
    s.pushConsumption.onPassEnd();
    return s.result();
}

class Buffer final : public RecordedStateCommandBuffer {
public:
    Buffer(std::shared_ptr<CommandState> s, std::shared_ptr<core::MutationJournal> journal)
        : RecordedStateCommandBuffer(std::move(journal)), state(std::move(s))
    {}

    std::uint32_t commandCount() const override { return state->commandCount; }

    std::shared_ptr<CommandState> state;
};

class Encoder final : public CommandEncoderBase {
public:
    explicit Encoder(std::shared_ptr<CommandState> s) : s_(std::move(s))
    {
        s_->encoderValidation = &lastValidation_;
        journal_ = core::shareUnique(std::move(recordedStates_));
        s_->journal = journal_;
        passes_.resize(s_->passes.size());
        for (std::uint32_t i = 0; i < passes_.size(); ++i)
            passes_[i] = core::makeUnique<RenderPass>(s_, i);
        buffer_ = core::makeUnique<Buffer>(s_, journal_);
    }

    ~Encoder() override { s_->encoderValidation = nullptr; }

    core::UniquePtr<RenderPassEncoder> beginRenderPass(const RenderPassDesc& desc) override;

    core::UniquePtr<ComputePassEncoder> beginComputePass(const ComputePassDesc&) override
    {
        markCommandFail("RHI-WEBGL1-COMPUTE-UNSUPPORTED");
        return {};
    }

    core::UniquePtr<TransferPassEncoder> beginTransferPass(const TransferPassDesc&) override
    {
        markCommandFail("RHI-WEBGL1-PREPARED-TRANSFER-UNSUPPORTED");
        return {};
    }

    ResourceBarrierResult transition(GpuBuffer& buffer, const ResourceBarrierDesc& barrier) override
    {
        auto& s = *s_;
        auto reject = [&](const char* id) {
            markCommandFail(id);
            return ResourceBarrierResult{false, id};
        };
        if (s.request.contractVersion < 3)
            return reject("RHI-WEBGL1-PREPARED-BARRIER-UNSUPPORTED");
        if (s.open || s.finished)
            return reject("RHI-WEBGL1-TRANSITION-RECORDING-STATE");
        if (const auto* id = s.current())
            return reject(id);
        const auto* original = dynamic_cast<const WebGlBuffer*>(&buffer);
        if (!original || original->payload->domain != s.domain || !original->payload->current()
            || barrier.resource != buffer.id()
            || barrier.textureSubresources != TextureSubresourceRange{})
            return reject("RHI-WEBGL1-TRANSITION-ORIGINAL-WHOLE-BUFFER");
        auto* use = s.track(original->payload);
        if (!use)
            return reject("RHI-WEBGL1-BUFFER-STATE-INVENTORY");
        if (const auto* id =
                semantics::RecordedResourceStateSequence::validateBarrier(barrier, buffer.state()))
            return reject(id);
        if (const auto* id = semantics::ResourceAccessUsageValidation::validate(
                buffer.desc(), barrier.sourceAccess | barrier.destinationAccess))
            return reject(id);
        if (s.transitionCount == s.transitions.size())
            return reject("RHI-WEBGL1-TRANSITION-CAPACITY");
        if (const auto* id = original->payload->state.writeBuffer(barrier.after, journal_.get()))
            return reject(id);
        use->transitioned = true;
        s.transitions[s.transitionCount++] = {
            nullptr, use, barrier, static_cast<std::uint32_t>(s.issue->operationCount())};
        s.result();
        markCommandOk();
        return {true, ""};
    }

    ResourceBarrierResult memoryBarrier(
        GpuTexture& texture, const ResourceMemoryBarrierDesc& barrier) override
    {
        auto& s = *s_;
        const auto reject = [&](const char* id) {
            markCommandFail(id);
            return ResourceBarrierResult{false, id};
        };
        if (s.request.contractVersion < 3 || s.open || s.finished)
            return reject("RHI-WEBGL1-TRANSITION-RECORDING-STATE");
        if (const auto* id = s.current())
            return reject(id);
        if (const auto* id = semantics::OrderedGraphicsDependency::memory(
                Backend::WebGL, 1, s.request.prototypeOptIn, barrier, texture.state()))
            return reject(id);
        if (const auto* canvas = dynamic_cast<const WebGlCanvasImage*>(&texture)) {
            const auto& payload = canvas->payload;
            if (payload->domain != s.domain || barrier.resource != texture.id()
                || payload->phase != WebGlCanvasPayload::Phase::Acquired)
                return reject("RHI-WEBGL1-TRANSITION-ORIGINAL-WHOLE-TEXTURE");
            if (const auto* id = payload->diagnostic())
                return reject(id);
            if (std::none_of(
                    s.targetPayloads.begin(), s.targetPayloads.end(), [&](const auto* target) {
                        return target->canvas == payload;
                    }))
                return reject("RHI-WEBGL1-TEXTURE-STATE-INVENTORY");
            if (s.transitionCount == s.transitions.size())
                return reject("RHI-WEBGL1-TRANSITION-CAPACITY");
            s.transitions[s.transitionCount++] = {nullptr,
                nullptr,
                {texture.id(),
                    defaultTextureSubresourceRange(texture.desc()),
                    texture.state(),
                    texture.state(),
                    barrier.sourceStages,
                    barrier.destinationStages,
                    barrier.sourceAccess,
                    barrier.destinationAccess},
                static_cast<std::uint32_t>(s.issue->operationCount()),
                payload};
            s.result();
            markCommandOk();
            return {true, ""};
        }
        const auto* original = dynamic_cast<const WebGlTexture*>(&texture);
        if (!original || original->payload->domain != s.domain || !original->payload->current()
            || barrier.resource != texture.id())
            return reject("RHI-WEBGL1-TRANSITION-ORIGINAL-WHOLE-TEXTURE");
        if (const auto* id = semantics::ResourceAccessUsageValidation::validate(
                texture.desc(), barrier.sourceAccess | barrier.destinationAccess))
            return reject(id);
        auto* use = s.track(original->payload);
        if (!use)
            return reject("RHI-WEBGL1-TEXTURE-STATE-INVENTORY");
        if (s.transitionCount == s.transitions.size())
            return reject("RHI-WEBGL1-TRANSITION-CAPACITY");
        // Preserve the exact dependency position and original resource custody.
        // Same-state ordered visibility has no state mutation or CPU completion.
        const ResourceBarrierDesc dependency{texture.id(),
            defaultTextureSubresourceRange(texture.desc()),
            texture.state(),
            texture.state(),
            barrier.sourceStages,
            barrier.destinationStages,
            barrier.sourceAccess,
            barrier.destinationAccess};
        s.transitions[s.transitionCount++] = {
            use, nullptr, dependency, static_cast<std::uint32_t>(s.issue->operationCount())};
        s.result();
        markCommandOk();
        return {true, ""};
    }

    ResourceBarrierResult memoryBarrier(
        GpuBuffer& buffer, const ResourceMemoryBarrierDesc& barrier) override
    {
        if (const auto* id = semantics::OrderedGraphicsDependency::memory(
                Backend::WebGL, 1, s_->request.prototypeOptIn, barrier, buffer.state())) {
            markCommandFail(id);
            return {false, id};
        }
        return transition(buffer,
            {barrier.resource,
                {},
                buffer.state(),
                buffer.state(),
                barrier.sourceStages,
                barrier.destinationStages,
                barrier.sourceAccess,
                barrier.destinationAccess});
    }

    ResourceBarrierResult transition(
        GpuTexture& texture, const ResourceBarrierDesc& barrier) override
    {
        auto& s = *s_;
        auto reject = [&](const char* id) {
            markCommandFail(id);
            return ResourceBarrierResult{false, id};
        };
        if (s.request.contractVersion < 3)
            return reject("RHI-WEBGL1-PREPARED-BARRIER-UNSUPPORTED");
        if (s.open || s.finished)
            return reject("RHI-WEBGL1-TRANSITION-RECORDING-STATE");
        if (const auto* id = s.current())
            return reject(id);
        const auto* original = dynamic_cast<const WebGlTexture*>(&texture);
        if (!original || original->payload->domain != s.domain || !original->payload->current()
            || barrier.resource != texture.id()
            || barrier.textureSubresources != defaultTextureSubresourceRange(texture.desc()))
            return reject("RHI-WEBGL1-TRANSITION-ORIGINAL-WHOLE-TEXTURE");
        auto* use = s.track(original->payload);
        if (!use)
            return reject("RHI-WEBGL1-TEXTURE-STATE-INVENTORY");
        if (const auto* id =
                semantics::RecordedResourceStateSequence::validateBarrier(barrier, texture.state()))
            return reject(id);
        if (const auto* id = semantics::ResourceAccessUsageValidation::validate(
                texture.desc(), barrier.sourceAccess | barrier.destinationAccess))
            return reject(id);
        if (s.transitionCount == s.transitions.size())
            return reject("RHI-WEBGL1-TRANSITION-CAPACITY");
        if (const auto* id = original->payload->state.writeWhole(barrier.after, journal_.get()))
            return reject(id);
        use->transitioned = true;
        s.transitions[s.transitionCount++] = {
            use, nullptr, barrier, static_cast<std::uint32_t>(s.issue->operationCount())};
        s.result();
        markCommandOk();
        return {true, ""};
    }

    core::UniquePtr<CommandBuffer> finish() override
    {
        activePass_ = s_->open;
        if (!s_->ended) {
            markCommandFail("RHI-WEBGL1-UNCLOSED-PASS");
            return {};
        }
        if (!finishGuard("RHI-WEBGL1-UNCLOSED-PASS"))
            return {};
        if (const auto* id = s_->current()) {
            markCommandFail(id);
            return {};
        }
        for (std::uint32_t i = 0; i < s_->textureUseCount; ++i) {
            auto& use = s_->textureUses[i];
            const auto state = use.texture->state.uniformWholeState();
            if (!state || !use.texture->state.active()) {
                markCommandFail("RHI-WEBGL1-FINISH-TEXTURE-STATE");
                return {};
            }
            use.finalState = *state;
            use.stateRevision = use.texture->state.revision();
            if (use.transitioned) {
                use.pending = use.texture->state.observePending(
                    defaultTextureSubresourceRange(use.texture->desc), *journal_);
                if (!use.pending) {
                    markCommandFail("RHI-WEBGL1-FINISH-TEXTURE-JOURNAL");
                    return {};
                }
            }
        }
        for (std::uint32_t i = 0; i < s_->bufferUseCount; ++i) {
            auto& use = s_->bufferUses[i];
            if (!use.buffer->state.active()) {
                markCommandFail("RHI-WEBGL1-FINISH-BUFFER-STATE");
                return {};
            }
            use.finalState = use.buffer->state.state();
            use.stateRevision = use.buffer->state.revision();
            if (use.transitioned) {
                use.pending = use.buffer->state.observePending(*journal_);
                if (!use.pending) {
                    markCommandFail("RHI-WEBGL1-FINISH-BUFFER-JOURNAL");
                    return {};
                }
            }
        }
        if (const auto* id = s_->issue->seal()) {
            markCommandFail(id);
            return {};
        }
        s_->finished = true;
        return std::move(buffer_);
    }

    void pushDebugGroup(std::string_view) override
    {
        markCommandFail("RHI-WEBGL1-PREPARED-DEBUG-UNSUPPORTED");
    }

    void popDebugGroup() override { markCommandFail("RHI-WEBGL1-PREPARED-DEBUG-UNSUPPORTED"); }

    void insertDebugMarker(std::string_view) override
    {
        markCommandFail("RHI-WEBGL1-PREPARED-DEBUG-UNSUPPORTED");
    }

private:
    std::shared_ptr<CommandState> s_;
    std::shared_ptr<core::MutationJournal> journal_;
    core::pmr::Vector<core::UniquePtr<RenderPass>> passes_{&core::defaultCpuMemoryResource()};
    core::UniquePtr<Buffer> buffer_;
};

core::UniquePtr<RenderPassEncoder> Encoder::beginRenderPass(const RenderPassDesc& desc)
{
    auto& s = *s_;
    if (s.passCount == s.passes.size()) {
        markCommandFail("RHI-WEBGL1-PREPARED-SECOND-PASS");
        return {};
    }
    activePass_ = s.open;
    if (!beginPassGuard("RHI-WEBGL1-PASS-ACTIVE"))
        return {};
    auto reject = [&](const char* id) -> core::UniquePtr<RenderPassEncoder> {
        rollbackPassBegin();
        markCommandFail(id);
        return {};
    };
    std::shared_ptr<WebGlFramebuffer> framebuffer;
    const WebGlFramebuffer::Impl* target = nullptr;
    if (desc.colorAttachments.size() == 1) {
        for (std::size_t i = 0; i < s.targets.size(); ++i)
            if (s.targetPayloads[i]->colorId() == desc.colorAttachments[0].target) {
                framebuffer = s.targets[i];
                target = s.targetPayloads[i];
                break;
            }
    }
    if (!target)
        return reject("RHI-WEBGL1-PREPARED-TARGET-NOT-DECLARED");
    if (const auto* id = s.current())
        return reject(id);
    const auto attachmentValidation = semantics::validateRenderPassAttachmentOperations(desc);
    if (!attachmentValidation.success)
        return reject(attachmentValidation.diagnostics.c_str());
    if (desc.colorAttachments.size() != 1 || desc.layeredRendering || !desc.subpasses.empty()
        || !desc.subpassDependencies.empty())
        return reject("RHI-WEBGL1-PREPARED-PASS-SHAPE");
    const auto& c = desc.colorAttachments[0];
    if (c.target != target->colorId() || c.format != TextureFormat::Rgba8Unorm || c.hasResolveTarget
        || !std::isfinite(c.clearColor.r) || !std::isfinite(c.clearColor.g)
        || !std::isfinite(c.clearColor.b) || !std::isfinite(c.clearColor.a))
        return reject("RHI-WEBGL1-PREPARED-COLOR-ATTACHMENT");
    if (target->canvas) {
        const auto& canvas = *target->canvas;
        if (const auto* id = canvas.diagnostic())
            return reject(id);
        if (canvas.phase != WebGlCanvasPayload::Phase::Acquired || canvas.state.pending()
            || canvas.state.uniformWholeState() != ResourceState::ColorAttachment
            || c.loadOp != LoadOp::Clear || c.storeOp != StoreOp::Store)
            return reject("RHI-WEBGL1-CANVAS-CLEAR-STORE-LEASE-REQUIRED");
        for (std::uint32_t i = 0; i < s.passCount; ++i)
            if (s.passes[i].target == target)
                return reject("RHI-WEBGL1-CANVAS-LEASE-ALREADY-RECORDED");
    } else if (target->color->state.uniformWholeState() != ResourceState::ColorAttachment)
        return reject("RHI-WEBGL1-PREPARED-ATTACHMENT-STATE");
    if (desc.hasDepthStencilAttachment != bool(target->depthStencil))
        return reject("RHI-WEBGL1-PREPARED-DEPTH-ATTACHMENT");
    const auto& z = desc.depthStencilAttachment;
    if (target->depthStencil
        && (z.target != target->depthStencil->id || z.format != TextureFormat::Depth24Stencil8
            || z.depthReadOnly || z.stencilReadOnly || !std::isfinite(z.depthClearValue)
            || z.depthClearValue < 0 || z.depthClearValue > 1 || z.stencilClearValue > 255

            || target->depthStencil->state.uniformWholeState() != ResourceState::DepthAttachment))
        return reject("RHI-WEBGL1-PREPARED-DEPTH-ATTACHMENT");
    if (!target->canvas) {
        auto* colorUse = s.track(target->color);
        auto* depthUse = target->depthStencil ? s.track(target->depthStencil) : nullptr;
        if (!colorUse || (target->depthStencil && !depthUse))
            return reject("RHI-WEBGL1-TEXTURE-STATE-INVENTORY");
        if (colorUse->recordedWrites >= UINT64_MAX - colorUse->nativeRevision
            || (depthUse && depthUse->recordedWrites >= UINT64_MAX - depthUse->nativeRevision))
            return reject("RHI-WEBGL1-RESOURCE-REVISION-EXHAUSTED");
    }
    s.color.target = c.target;
    s.color.format = c.format;
    s.color.loadOp = c.loadOp;
    s.color.storeOp = c.storeOp;
    s.color.clearColor = c.clearColor;
    s.depth.target = z.target;
    s.depth.format = z.format;
    s.depth.depthLoadOp = z.depthLoadOp;
    s.depth.depthStoreOp = z.depthStoreOp;
    s.depth.stencilLoadOp = z.stencilLoadOp;
    s.depth.stencilStoreOp = z.stencilStoreOp;
    s.depth.depthClearValue = z.depthClearValue;
    s.depth.stencilClearValue = z.stencilClearValue;
    const std::uint32_t begin = CommandState::beginMarker(s.passCount);
    const std::shared_ptr<const void> owners[]{s.pipeline, framebuffer};
    if (const auto* id = s.issue->recordOperation(owners, 2, &begin, sizeof(begin)))
        return reject(id);
    s.framebuffer = std::move(framebuffer);
    s.target = target;
    s.currentPass = s.passCount++;
    auto& pass = s.passes[s.currentPass];
    pass.framebuffer = s.framebuffer;
    pass.target = target;
    pass.color = s.color;
    pass.depth = s.depth;
    pass.canvasRevision = target->canvas ? target->canvas->state.revision() : 0;
    s.bound = s.viewportSet = s.scissorSet = false;
    s.pushInPass = false;
    s.index = {};
    for (auto& vertex : s.vertices)
        vertex = {};
    for (auto& set : s.sets) {
        set.object = nullptr;
        set.lifetime = {};
    }
    s.begun = s.open = true;
    s.ended = false;
    s.result();
    markCommandOk();
    return std::move(passes_[s.currentPass]);
}
#ifdef __EMSCRIPTEN__
void issueUniforms(const CommandState& s, const Draw& d) noexcept
{
    const auto& program = WebGlRenderPipeline::program(*d.pipeline);
    for (const auto& u : program.uniforms) {
        if (!u.active)
            continue;
        if (!u.block) {
            (*s.domain).nativeCall(
                [&] {
                    return glUniform1i(u.location, u.textureUnit);
                },
                &semantics::WebGlExecutionCounters::uniformCalls,
                &semantics::WebGlExecutionCounters::uniformBytes,
                sizeof(GLint));
            continue;
        }
        const std::uint8_t* bytes = nullptr;
        for (const auto& slot : s.uniformBanks[d.uniformBank].snapshots.slots())
            if (slot.push == u.block->push && slot.set == u.block->set
                && slot.binding == u.block->binding)
                bytes = s.uniformBanks[d.uniformBank].snapshots.data(d.snapshot, slot);
        const auto& leaf = *u.leaf;
        // Parsed leaves are scalar/vector or exact 4x4. Copy avoids alignment
        // and aliasing assumptions; row-major bytes match original ES100 ABI.
        if (leaf.columns == 4) {
            float matrix[16];
            std::memcpy(matrix, bytes + leaf.offset, sizeof(matrix));
            (*s.domain).nativeCall(
                [&] {
                    return glUniformMatrix4fv(u.location, 1, GL_FALSE, matrix);
                },
                &semantics::WebGlExecutionCounters::uniformCalls,
                &semantics::WebGlExecutionCounters::uniformBytes,
                sizeof(matrix));
        } else if (leaf.integer) {
            GLint values[4]{};
            std::memcpy(values, bytes + leaf.offset, leaf.components * sizeof(GLint));
            switch (leaf.components) {
            case 1:
                (*s.domain).nativeCall(
                    [&] {
                        return glUniform1iv(u.location, 1, values);
                    },
                    &semantics::WebGlExecutionCounters::uniformCalls,
                    &semantics::WebGlExecutionCounters::uniformBytes,
                    leaf.components * sizeof(GLint));
                break;
            case 2:
                (*s.domain).nativeCall(
                    [&] {
                        return glUniform2iv(u.location, 1, values);
                    },
                    &semantics::WebGlExecutionCounters::uniformCalls,
                    &semantics::WebGlExecutionCounters::uniformBytes,
                    leaf.components * sizeof(GLint));
                break;
            case 3:
                (*s.domain).nativeCall(
                    [&] {
                        return glUniform3iv(u.location, 1, values);
                    },
                    &semantics::WebGlExecutionCounters::uniformCalls,
                    &semantics::WebGlExecutionCounters::uniformBytes,
                    leaf.components * sizeof(GLint));
                break;
            case 4:
                (*s.domain).nativeCall(
                    [&] {
                        return glUniform4iv(u.location, 1, values);
                    },
                    &semantics::WebGlExecutionCounters::uniformCalls,
                    &semantics::WebGlExecutionCounters::uniformBytes,
                    leaf.components * sizeof(GLint));
                break;
            }
        } else {
            GLfloat values[4]{};
            std::memcpy(values, bytes + leaf.offset, leaf.components * sizeof(GLfloat));
            switch (leaf.components) {
            case 1:
                (*s.domain).nativeCall(
                    [&] {
                        return glUniform1fv(u.location, 1, values);
                    },
                    &semantics::WebGlExecutionCounters::uniformCalls,
                    &semantics::WebGlExecutionCounters::uniformBytes,
                    leaf.components * sizeof(GLfloat));
                break;
            case 2:
                (*s.domain).nativeCall(
                    [&] {
                        return glUniform2fv(u.location, 1, values);
                    },
                    &semantics::WebGlExecutionCounters::uniformCalls,
                    &semantics::WebGlExecutionCounters::uniformBytes,
                    leaf.components * sizeof(GLfloat));
                break;
            case 3:
                (*s.domain).nativeCall(
                    [&] {
                        return glUniform3fv(u.location, 1, values);
                    },
                    &semantics::WebGlExecutionCounters::uniformCalls,
                    &semantics::WebGlExecutionCounters::uniformBytes,
                    leaf.components * sizeof(GLfloat));
                break;
            case 4:
                (*s.domain).nativeCall(
                    [&] {
                        return glUniform4fv(u.location, 1, values);
                    },
                    &semantics::WebGlExecutionCounters::uniformCalls,
                    &semantics::WebGlExecutionCounters::uniformBytes,
                    leaf.components * sizeof(GLfloat));
                break;
            }
        }
    }
}

GLint minFilter(const SamplerDesc& d) noexcept
{
    if (d.mipFilter == SamplerMipFilter::None)
        return d.minFilter == SamplerFilter::Linear ? GL_LINEAR : GL_NEAREST;
    if (d.mipFilter == SamplerMipFilter::Nearest)
        return d.minFilter == SamplerFilter::Linear ? GL_LINEAR_MIPMAP_NEAREST
                                                    : GL_NEAREST_MIPMAP_NEAREST;
    return d.minFilter == SamplerFilter::Linear ? GL_LINEAR_MIPMAP_LINEAR
                                                : GL_NEAREST_MIPMAP_LINEAR;
}

const char* issueDraw(CommandState& s, const Draw& d) noexcept
{
    if (const auto* id = WebGlRenderPipeline::issueState(*d.pipeline))
        return id;
    using Coordinates = semantics::WebGlShaderCoordinates;
    const auto& extent = s.target->extent();
    Coordinates::Rectangle viewport, scissor;
    if (const auto* id = Coordinates::lowerTopLeftRectangle({std::uint32_t(d.viewport.x),
                                                                std::uint32_t(d.viewport.y),
                                                                std::uint32_t(d.viewport.width),
                                                                std::uint32_t(d.viewport.height)},
            extent.width,
            extent.height,
            viewport))
        return id;
    if (const auto* id = Coordinates::lowerTopLeftRectangle({std::uint32_t(d.scissor.x),
                                                                std::uint32_t(d.scissor.y),
                                                                d.scissor.width,
                                                                d.scissor.height},
            extent.width,
            extent.height,
            scissor))
        return id;
    (*s.domain).nativeCall([&] {
        return glViewport(viewport.x, viewport.y, viewport.width, viewport.height);
    });
    (*s.domain).nativeCall([&] {
        return glDepthRangef(d.viewport.minDepth, d.viewport.maxDepth);
    });
    (*s.domain).nativeCall([&] {
        return glScissor(scissor.x, scissor.y, scissor.width, scissor.height);
    });
    const auto& program = WebGlRenderPipeline::program(*d.pipeline);
    (*s.domain).nativeCall([&] {
        return glUseProgram(program.object->native);
    });
    if (d.pipeline->vertexArray)
        s.domain->nativeCall([&] {
            glBindVertexArrayOES(d.pipeline->vertexArray->native);
        });
    for (std::uint32_t i = 0; i < s.domain->runtime->profile().limits().maxVertexAttributes; ++i) {
        if (s.domain->extensionDecision.native.instancing)
            s.domain->nativeCall([&] {
                glVertexAttribDivisorANGLE(i, 0);
            });
        (*s.domain).nativeCall([&] {
            return glDisableVertexAttribArray(i);
        });
    }
    for (std::size_t i = 0; i < d.vertices.size(); ++i) {
        const auto& v = d.vertices[i];
        const auto& stream = d.pipeline->vertexInput.streams()[i];
        (*s.domain).nativeCall([&] {
            return glBindBuffer(GL_ARRAY_BUFFER, v.buffer->native);
        });
        for (std::size_t j = 0; j < stream.attributeCount; ++j) {
            const auto& a = d.pipeline->vertexInput.attributes()[stream.firstAttribute + j].layout;
            (*s.domain).nativeCall([&] {
                return glEnableVertexAttribArray(a.shaderLocation);
            });
            if (s.domain->extensionDecision.native.instancing)
                s.domain->nativeCall([&] {
                    glVertexAttribDivisorANGLE(
                        a.shaderLocation, stream.rate == VertexInputRate::Instance ? 1 : 0);
                });
            (*s.domain).nativeCall([&] {
                return glVertexAttribPointer(a.shaderLocation,
                    VertexInputConsumptionContract::formatBytes(a.format) / sizeof(float),
                    GL_FLOAT,
                    GL_FALSE,
                    stream.strideBytes,
                    reinterpret_cast<const void*>(
                        static_cast<std::uintptr_t>(v.offset + a.offsetBytes)));
            });
        }
    }
    for (std::size_t i = 0; i < d.textures.size(); ++i) {
        const auto& t = d.textures[i];
        const auto& sampler = t.sampler->definition;
        (*s.domain).nativeCall([&] {
            return glActiveTexture(GL_TEXTURE0 + i);
        });
        (*s.domain).nativeCall([&] {
            return glBindTexture(GL_TEXTURE_2D, t.view->texture->native);
        });
        (*s.domain).nativeCall([&] {
            return glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter(sampler));
        });
        (*s.domain).nativeCall([&] {
            return glTexParameteri(GL_TEXTURE_2D,
                GL_TEXTURE_MAG_FILTER,
                sampler.magFilter == SamplerFilter::Linear ? GL_LINEAR : GL_NEAREST);
        });
        (*s.domain).nativeCall([&] {
            return glTexParameteri(GL_TEXTURE_2D,
                GL_TEXTURE_WRAP_S,
                sampler.addressU == SamplerAddressMode::Repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
        });
        (*s.domain).nativeCall([&] {
            return glTexParameteri(GL_TEXTURE_2D,
                GL_TEXTURE_WRAP_T,
                sampler.addressV == SamplerAddressMode::Repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
        });
    }
    issueUniforms(s, d);
    if (d.indexed) {
        (*s.domain).nativeCall([&] {
            return glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, d.index->native);
        });
        (*s.domain).nativeCall(
            [&] {
                const auto type =
                    d.indexFormat == IndexFormat::Uint32 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
                const auto offset =
                    reinterpret_cast<const void*>(static_cast<std::uintptr_t>(d.indexOffset));
                if (d.instanced)
                    glDrawElementsInstancedANGLE(
                        GL_TRIANGLES, d.count, type, offset, d.instanceCount);
                else
                    glDrawElements(GL_TRIANGLES, d.count, type, offset);
            },
            &semantics::WebGlExecutionCounters::draws);
    } else {
        (*s.domain).nativeCall([&] {
            return glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        });
        (*s.domain).nativeCall(
            [&] {
                if (d.instanced)
                    glDrawArraysInstancedANGLE(GL_TRIANGLES, d.first, d.count, d.instanceCount);
                else
                    glDrawArrays(GL_TRIANGLES, d.first, d.count);
            },
            &semantics::WebGlExecutionCounters::draws);
    }
    return s.domain->nativeResult();
}
#endif
} // namespace

core::UniquePtr<CommandEncoder> WebGlExecution::prepare(WebGlResources& resources,
    const std::shared_ptr<RenderPipeline>& pipeline,
    const std::shared_ptr<WebGlFramebuffer>& target,
    const semantics::WebGlPreparedCommandsRequest& request,
    const std::shared_ptr<core::CpuMemoryQuota>& quota,
    const char*& diagnostic)
{
    return prepare(resources, {&pipeline, 1}, target, request, quota, diagnostic);
}

core::UniquePtr<CommandEncoder> WebGlExecution::prepare(WebGlResources& resources,
    core::ArrayView<std::shared_ptr<RenderPipeline>> pipelines,
    const std::shared_ptr<WebGlFramebuffer>& target,
    const semantics::WebGlPreparedCommandsRequest& request,
    const std::shared_ptr<core::CpuMemoryQuota>& quota,
    const char*& diagnostic)
{
    return prepare(resources, pipelines, {&target, 1}, request, quota, diagnostic);
}

core::UniquePtr<CommandEncoder> WebGlExecution::prepare(WebGlResources& resources,
    core::ArrayView<std::shared_ptr<RenderPipeline>> pipelines,
    core::ArrayView<std::shared_ptr<WebGlFramebuffer>> targets,
    const semantics::WebGlPreparedCommandsRequest& request,
    const std::shared_ptr<core::CpuMemoryQuota>& quota,
    const char*& diagnostic)
{
    diagnostic = semantics::WebGlPreparedCommandsContract::validate(Backend::WebGL, request);
    if (diagnostic)
        return {};
    if ((diagnostic = resources.currentDiagnostic()))
        return {};
    if (!pipelines.data() || pipelines.empty()
        || pipelines.size() > (request.contractVersion == 1 ? 1 : request.maxPipelines)) {
        diagnostic = "RHI-WEBGL1-PREPARED-PIPELINE-CAPACITY";
        return {};
    }
    if (!targets.data() || targets.empty()
        || targets.size() > (request.contractVersion >= 3 ? request.maxTargets : 1)) {
        diagnostic = "RHI-WEBGL1-PREPARED-TARGET-CAPACITY";
        return {};
    }
    const auto& target = targets[0];
    auto p = std::dynamic_pointer_cast<WebGlRenderPipeline>(pipelines[0]);
    if (!p || p->impl->object->domain != resources.domain_ || !target
        || target->impl_->object->domain != resources.domain_) {
        diagnostic = "RHI-WEBGL1-PREPARED-FOREIGN-OWNER";
        return {};
    }
    if (!quota || quota->limitBytes() != request.issue.maxCpuMetadataBytes
        || quota == resources.domain_->metadataQuota || quota == resources.domain_->uploadQuota) {
        diagnostic = "RHI-WEBGL1-PREPARED-CPU-QUOTA";
        return {};
    }
    if ((diagnostic = target->currentDiagnostic())
        || (diagnostic = WebGlRenderPipeline::current(*p->impl)))
        return {};
    if (!p->impl->definition.dynamicViewportScissor) {
        diagnostic = "RHI-WEBGL1-PREPARED-DYNAMIC-RECTANGLES-REQUIRED";
        return {};
    }
    if (bool(target->impl_->depthStencil)
        != (p->impl->definition.depthFormat != TextureFormat::Unknown)) {
        diagnostic = "RHI-WEBGL1-PREPARED-PIPELINE-ATTACHMENT-MISMATCH";
        return {};
    }
    for (const auto& entry : pipelines) {
        const auto candidate = std::dynamic_pointer_cast<WebGlRenderPipeline>(entry);
        if (!candidate || candidate->impl->object->domain != resources.domain_
            || (diagnostic = WebGlRenderPipeline::current(*candidate->impl))) {
            if (!diagnostic)
                diagnostic = "RHI-WEBGL1-PREPARED-FOREIGN-OWNER";
            return {};
        }
        if (request.contractVersion < 4
            && !WebGlRenderPipeline::compatiblePreparedInputs(*p->impl, *candidate->impl)) {
            diagnostic = "RHI-WEBGL1-PREPARED-PIPELINE-ABI-MISMATCH";
            return {};
        }
        if (request.contractVersion >= 4) {
            const auto& a = p->impl->vertexInput.streams();
            const auto& b = candidate->impl->vertexInput.streams();
            const auto& x = p->impl->layout->definition.descriptorSets;
            const auto& y = candidate->impl->layout->definition.descriptorSets;
            bool matches = a.size() == b.size() && x.size() == y.size();
            for (std::size_t i = 0; matches && i < a.size(); ++i)
                matches = a[i].slot == b[i].slot;
            for (std::size_t i = 0; matches && i < x.size(); ++i)
                matches = x[i].set == y[i].set;
            if (!matches || !candidate->impl->definition.dynamicViewportScissor) {
                diagnostic = "RHI-ORDERED-GRAPHICS-CATALOG-TOPOLOGY";
                return {};
            }
        }
    }
    for (std::size_t i = 0; i < targets.size(); ++i) {
        const auto& entry = targets[i];
        if (!entry || entry->impl_->object->domain != resources.domain_
            || (diagnostic = entry->currentDiagnostic())) {
            if (!diagnostic)
                diagnostic = "RHI-WEBGL1-PREPARED-FOREIGN-OWNER";
            return {};
        }
        if (request.contractVersion < 4
            && bool(entry->impl_->depthStencil) != bool(target->impl_->depthStencil)) {
            diagnostic = "RHI-WEBGL1-PREPARED-PIPELINE-ATTACHMENT-MISMATCH";
            return {};
        }
        for (std::size_t j = 0; j < i; ++j)
            if (targets[j]->impl_->colorId() == entry->impl_->colorId()) {
                diagnostic = "RHI-WEBGL1-PREPARED-TARGET-AMBIGUOUS";
                return {};
            }
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto s = core::makeShared<CommandState>();
        s->domain = resources.domain_;
        s->pipeline = p->impl;
        s->pipelines.reserve(pipelines.size());
        for (const auto& entry : pipelines)
            s->pipelines.push_back(std::static_pointer_cast<WebGlRenderPipeline>(entry)->impl);
        s->framebuffer = target;
        s->target = target->impl_.get();
        s->targets.reserve(targets.size());
        s->targetPayloads.reserve(targets.size());
        for (const auto& entry : targets) {
            s->targets.push_back(entry);
            s->targetPayloads.push_back(entry->impl_.get());
        }
        s->passes.resize(request.contractVersion >= 3 ? request.maxPasses : 1);
        s->textureUses.resize(request.issue.maxResourceOwners);
        s->bufferUses.resize(request.issue.maxResourceOwners);
        s->transitions.resize(request.maxTransitions);
        s->request = request;
        s->quota = quota;
        s->issue = common::OrderedCommandIssue::prepare(
            request.issue, quota, resources.domain_->runtime->epoch(), diagnostic);
        if (!s->issue)
            return {};
        s->uniformBanks.reserve(pipelines.size());
        s->pipelineBanks.reserve(pipelines.size());
        std::uint64_t uniformCapacity = 0;
        std::size_t maxSources = 0, maxTextures = 0;
        for (std::size_t i = 0; i < s->pipelines.size(); ++i) {
            std::uint32_t bank = UINT32_MAX;
            for (std::size_t j = 0; j < i; ++j)
                if (WebGlRenderPipeline::compatiblePreparedInputs(
                        *s->pipelines[j], *s->pipelines[i])) {
                    bank = s->pipelineBanks[j];
                    break;
                }
            if (bank == UINT32_MAX) {
                bank = static_cast<std::uint32_t>(s->uniformBanks.size());
                s->uniformBanks.emplace_back();
                auto& b = s->uniformBanks.back();
                if ((diagnostic = b.snapshots.prepare(
                         WebGlRenderPipeline::program(*s->pipelines[i]).artifact,
                         {request.maxDraws, request.maxUniformBytes - uniformCapacity})))
                    return {};
                uniformCapacity += b.snapshots.capacityBytes();
                for (const auto& slot : b.snapshots.slots())
                    if (slot.push)
                        b.push = core::makeUnique<shader::WebGlCpuUniformData>(slot.size, true);
                maxSources = std::max(maxSources, b.snapshots.slots().size());
            }
            maxTextures = std::max(maxTextures,
                WebGlRenderPipeline::program(*s->pipelines[i]).artifact->textureUnits().size());
            s->pipelineBanks.push_back(bank);
        }
        s->sources.reserve(maxSources);
        s->sources.resize(s->snapshots().slots().size());
        s->sets.resize(p->impl->layout->definition.descriptorSets.size());
        for (std::size_t i = 0; i < s->sets.size(); ++i)
            s->sets[i].set = p->impl->layout->definition.descriptorSets[i].set;
        s->vertices.resize(p->impl->vertexInput.streams().size());
        s->owners.resize(s->vertices.size() + 2 * maxTextures + 2);
        s->draws.resize(request.maxDraws);
        s->sampledShapes.reserve(maxTextures);
        s->sampled.reserve(maxTextures);
        s->sampledShapes.resize(
            WebGlRenderPipeline::program(*p->impl).artifact->textureUnits().size());
        s->sampled.resize(WebGlRenderPipeline::program(*p->impl).artifact->textureUnits().size());
        for (auto& d : s->draws) {
            d.vertices.resize(s->vertices.size());
            d.textures.reserve(maxTextures);
            d.textures.resize(s->sampled.size());
        }
        return core::makeUnique<Encoder>(std::move(s));
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGL1-PREPARED-CPU-ALLOCATION";
    } catch (const std::length_error&) {
        diagnostic = "RHI-WEBGL1-PREPARED-CAPACITY";
    }
    return {};
}

WebGlExecution::CommandBudgetReport WebGlExecution::commandBudget(
    const CommandBuffer& commands) noexcept
{
    const auto* buffer = dynamic_cast<const Buffer*>(&commands);
    if (!buffer)
        return {};
    const auto& s = *buffer->state;
    std::size_t capacity = 0, recorded = 0;
    std::uint32_t draws = 0;
    for (const auto& bank : s.uniformBanks) {
        capacity += bank.snapshots.capacityBytes();
        recorded += bank.snapshots.recordedBytes();
        draws += bank.snapshots.size();
    }
    return {true,
        s.quota->limitBytes(),
        s.quota->liveBytes(),
        s.quota->peakBytes(),
        capacity,
        recorded,
        s.issue->stats().recordedOperations,
        draws};
}

const char* WebGlExecution::validateConformance(WebGlResources& resources,
    const CommandBuffer& commands,
    const OriginalConformanceRun& run) noexcept
{
    if (!run.device_.isCurrent() || !run.context_.isCurrent() || !run.domain_.isCurrent())
        return "RHI-CONFORMANCE-ORIGINAL-OWNER-RETIRED";
    if (const auto* id = run.ownerDiagnostic_(run.owner_.get()))
        return id;
    if (run.quota_->limitBytes() != run.quotaBytes_)
        return "RHI-CONFORMANCE-ORIGINAL-QUOTA";
    if (run.failure_)
        return run.failure_;
    if (run.next_ >= run.frames_.size())
        return "RHI-CONFORMANCE-ORIGINAL-FRAMES-EXHAUSTED";
    if (run.next_ && !run.frames_[run.next_ - 1].presented)
        return "RHI-CONFORMANCE-ORIGINAL-PRESENT-INCOMPLETE";
    const auto* buffer = dynamic_cast<const Buffer*>(&commands);
    if (!buffer || buffer->state->domain != resources.domain_)
        return "RHI-CONFORMANCE-ORIGINAL-COMMAND-OWNER";
    const auto& s = *buffer->state;
    if (const auto* id = s.current())
        return id;
    if (!s.finished || !s.ended || s.open
        || s.issue->phase() != common::OrderedCommandIssue::Phase::Sealed)
        return "RHI-CONFORMANCE-ORIGINAL-COMMAND-STATE";
    const auto& output = run.frames_[run.next_];
    const auto* input = run.sampled_ ? &run.sampled_->frames()[run.next_] : nullptr;
    const auto draws = input ? input->draws.size() : 0;
    const auto passes = input ? draws + 2 : 1;
    if (s.passCount != passes || s.drawCount != draws
        || s.issue->operationCount() != passes * 2 + draws)
        return "RHI-CONFORMANCE-ORIGINAL-PASS-DRAW-COUNT";
    const auto sameColor = [](Color a, Color b) noexcept {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    };
    std::array<const WebGlTexturePayload*, 5> textures{};
    const auto outputIndex = static_cast<std::size_t>(ConformanceSampledResource::Output);
    for (std::size_t i = 0; i < passes; ++i) {
        const auto& pass = s.passes[i];
        const auto& target = *pass.target;
        const auto extent = target.extent();
        const bool canvas = i + 1 == passes;
        if (bool(target.canvas) != canvas || target.depthStencil
            || extent.width != output.extent.width || extent.height != output.extent.height
            || extent.depth != 1 || pass.color.format != TextureFormat::Rgba8Unorm
            || pass.color.target != target.colorId() || pass.color.loadOp != LoadOp::Clear
            || pass.color.storeOp != StoreOp::Store)
            return "RHI-CONFORMANCE-ORIGINAL-ATTACHMENT";
        if (canvas) {
            if (const auto* id = target.canvas->diagnostic())
                return id;
            if (target.canvas->phase != WebGlCanvasPayload::Phase::Acquired)
                return "RHI-CONFORMANCE-ORIGINAL-CANVAS-LEASE";
        } else if (target.color->desc.format != TextureFormat::Rgba8Unorm)
            return "RHI-CONFORMANCE-ORIGINAL-TEXTURE-FORMAT";
        const auto color = !input ? run.clear_->frames()[run.next_].clearColor()
            : i < 2
            ? ConformanceClearFrame{input->extent, i ? input->sourceB : input->sourceA}.clearColor()
            : Color{0, 0, 0, 1};
        if (!sameColor(pass.color.clearColor, color))
            return "RHI-CONFORMANCE-ORIGINAL-CLEAR";
        if (!input)
            continue;
        const auto targetIndex = i < 2 ? i : static_cast<std::size_t>(input->draws[i - 2].target);
        if ((targetIndex == outputIndex) != canvas)
            return "RHI-CONFORMANCE-ORIGINAL-TARGET-ORDER";
        if (!canvas) {
            if (textures[targetIndex] && textures[targetIndex] != target.color.get())
                return "RHI-CONFORMANCE-ORIGINAL-TARGET-IDENTITY";
            for (std::size_t n = 0; n < textures.size(); ++n)
                if (n != targetIndex && textures[n] == target.color.get())
                    return "RHI-CONFORMANCE-ORIGINAL-TARGET-ALIAS";
            textures[targetIndex] = target.color.get();
        }
        if (i < 2)
            continue;
        const auto& expected = input->draws[i - 2];
        const auto& d = s.draws[i - 2];
        if (d.pass != i || d.indexed != expected.indexed || d.instanced || d.count != 3 || d.first
            || d.instanceCount != 1 || d.vertices.size() != 1 || d.textures.size() != 2
            || d.viewport.x != 0 || d.viewport.y != 0 || d.viewport.minDepth != 0
            || d.viewport.maxDepth != 1 || d.viewport.width != output.extent.width
            || d.viewport.height != output.extent.height || d.scissor.x || d.scissor.y
            || d.scissor.width != output.extent.width || d.scissor.height != output.extent.height)
            return "RHI-CONFORMANCE-ORIGINAL-DRAW";
        const auto& pipeline = *d.pipeline;
        const auto& definition = pipeline.definition;
        const auto& artifact = pipeline.program->artifact();
        if (shader::WebGlShaderProgramArtifact::validateOriginal(artifact)
            || pipeline.package.reflection.sourceSha256 != run.sampled_->shaderSourceSha256()
            || artifact->vertex().original->module().reflection.sourceSha256
                != run.sampled_->shaderSourceSha256()
            || artifact->fragment().original->module().reflection.sourceSha256
                != run.sampled_->shaderSourceSha256())
            return "RHI-CONFORMANCE-ORIGINAL-SHADER";
        if (definition.topology != PrimitiveTopology::TriangleList
            || definition.cullMode != CullMode::None || !definition.dynamicViewportScissor
            || definition.colorTargets.size() != 1 || definition.colorTargets[0].blendEnabled
            || definition.colorTargets[0].writeMask != ColorWriteMask::All
            || definition.depthFormat != TextureFormat::Unknown
            || definition.depthStencil.depthTestEnabled || definition.depthStencil.stencilEnabled
            || definition.multisample.sampleCount != 1
            || definition.multisample.alphaToCoverageEnabled
            || definition.vertexBuffers.size() != 1)
            return "RHI-CONFORMANCE-ORIGINAL-PIPELINE";
        const auto& stream = definition.vertexBuffers[0];
        if (stream.slot != 3 || stream.strideBytes != 9 * sizeof(float)
            || stream.inputRate != VertexInputRate::Vertex || stream.attributes.size() != 3)
            return "RHI-CONFORMANCE-ORIGINAL-VERTEX-LAYOUT";
        constexpr std::array<VertexFormat, 3> formats{
            VertexFormat::Float32x3, VertexFormat::Float32x2, VertexFormat::Float32x4};
        constexpr std::array<std::uint32_t, 3> offsets{0, 3 * sizeof(float), 5 * sizeof(float)};
        for (std::size_t a = 0; a < 3; ++a)
            if (stream.attributes[a].shaderLocation != a
                || stream.attributes[a].format != formats[a]
                || stream.attributes[a].offsetBytes != offsets[a])
                return "RHI-CONFORMANCE-ORIGINAL-VERTEX-LAYOUT";
        const auto& v = d.vertices[0];
        if (v.offset || !v.buffer->content
            || v.buffer->content->size() != sizeof(run.sampled_->vertices())
            || !v.buffer->content->initialized(0, sizeof(run.sampled_->vertices()))
            || std::memcmp(v.buffer->content->data(),
                run.sampled_->vertices().data(),
                sizeof(run.sampled_->vertices())))
            return "RHI-CONFORMANCE-ORIGINAL-VERTICES";
        if (expected.indexed
            && (d.indexFormat != IndexFormat::Uint16 || d.indexOffset || !d.index
                || !d.index->content || d.index->content->size() != sizeof(run.sampled_->indices())
                || !d.index->content->initialized(0, sizeof(run.sampled_->indices()))
                || std::memcmp(d.index->content->data(),
                    run.sampled_->indices().data(),
                    sizeof(run.sampled_->indices()))))
            return "RHI-CONFORMANCE-ORIGINAL-INDICES";
        const auto& snapshots = s.uniformBanks[d.uniformBank].snapshots;
        if (snapshots.slots().size() != 2)
            return "RHI-CONFORMANCE-ORIGINAL-UNIFORM-COVERAGE";
        for (const auto& slot : snapshots.slots()) {
            const auto* bytes = snapshots.data(d.snapshot, slot);
            const auto size = slot.push ? sizeof(expected.push) : sizeof(input->uniforms);
            const void* source =
                slot.push ? static_cast<const void*>(expected.push.data()) : input->uniforms.data();
            if (!bytes || slot.size != size || (!slot.push && (slot.set || slot.binding))
                || std::memcmp(bytes, source, size))
                return "RHI-CONFORMANCE-ORIGINAL-UNIFORM-SNAPSHOT";
        }
        for (std::size_t t = 0; t < 2; ++t) {
            const auto index = static_cast<std::size_t>(t ? expected.sourceB : expected.sourceA);
            const auto& binding = d.textures[t];
            const auto& sampler = binding.sampler->definition;
            if (!textures[index] || binding.view->texture.get() != textures[index]
                || sampler.minFilter != SamplerFilter::Nearest
                || sampler.magFilter != SamplerFilter::Nearest
                || sampler.addressU != SamplerAddressMode::ClampToEdge
                || sampler.addressV != SamplerAddressMode::ClampToEdge
                || sampler.mipFilter != SamplerMipFilter::None
                || sampler.compareOp != CompareOp::Never)
                return "RHI-CONFORMANCE-ORIGINAL-SAMPLED-IDENTITY";
        }
    }
    return nullptr;
}

const char* WebGlExecution::observeConformance(
    WebGlResources& resources, const CommandBuffer& commands, OriginalConformanceRun& run) noexcept
{
    const auto* buffer = dynamic_cast<const Buffer*>(&commands);
    if (!buffer || buffer->state->domain != resources.domain_ || run.next_ >= run.frames_.size())
        return "RHI-CONFORMANCE-ORIGINAL-COMMAND-OWNER";
    const auto& s = *buffer->state;
    const auto& canvas = s.passes[s.passCount - 1].target->canvas;
    if (const auto* id = resources.currentDiagnostic())
        return id;
    if (!canvas || canvas->phase != WebGlCanvasPayload::Phase::Submitted)
        return "RHI-CONFORMANCE-ORIGINAL-OUTPUT-LEASE";
    if (const auto* id = canvas->diagnostic())
        return id;
#ifndef __EMSCRIPTEN__
    return "RHI-WEBGL1-CONTEXT-PLATFORM-UNAVAILABLE";
#else
    auto& frame = run.frames_[run.next_];
    WebGlNativeCallScope scope(*resources.domain_);
    if (const auto* id = resources.domain_->nativeResult())
        return id;
    resources.domain_->nativeCall([&] {
        glBindFramebuffer(GL_FRAMEBUFFER, canvas->native);
    });
    resources.domain_->nativeCall([&] {
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
    });
    resources.domain_->nativeCall([&] {
        glReadPixels(0,
            0,
            frame.extent.width,
            frame.extent.height,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            frame.pixels.data());
    });
    if (const auto* id = resources.domain_->nativeResult())
        return id;
    const auto& expected = run.clear_ ? run.clear_->frames()[run.next_].rgba8
                                      : run.sampled_->frames()[run.next_].expected;
    for (std::size_t i = 0; i < frame.pixels.size(); ++i)
        if (frame.pixels[i] != expected[i % expected.size()])
            return "RHI-CONFORMANCE-ORIGINAL-OUTPUT-MISMATCH";
    if (run.sampled_)
        frame.shaderArtifact = s.draws[s.drawCount - 1].pipeline->program->artifact();
    frame.canvas = canvas->id;
    frame.serial = canvas->serial;
    frame.observed = true;
    ++run.next_;
    return nullptr;
#endif
}

WebGlExecution::Submission WebGlExecution::submit(
    WebGlResources& resources, const CommandBuffer& commands) noexcept
{
    WebGlNativeCallScope nativeScope(*resources.domain_);
    const auto execute = [&]() noexcept -> Submission {
        Submission result;
        const auto* buffer = dynamic_cast<const Buffer*>(&commands);
        if (!buffer || buffer->state->domain != resources.domain_) {
            result.diagnostic = "RHI-WEBGL1-SUBMIT-FOREIGN-COMMAND";
            return result;
        }
        auto& s = *buffer->state;
        if ((result.diagnostic = s.current()))
            return result;
        if (!s.finished || !s.ended || s.open
            || s.issue->phase() != common::OrderedCommandIssue::Phase::Sealed) {
            result.diagnostic = "RHI-WEBGL1-SUBMIT-COMMAND-STATE";
            return result;
        }
        const auto epoch = s.domain->runtime->epoch();
        if ((result.diagnostic = s.preflight()))
            return result;
#ifndef __EMSCRIPTEN__
        result.diagnostic = "RHI-WEBGL1-CONTEXT-PLATFORM-UNAVAILABLE";
        return result;
#else
        if ((result.diagnostic = s.domain->nativeResult()))
            return result;
        if ((result.diagnostic = s.issue->beginIssue(epoch)))
            return result;
        std::uint32_t currentPass = 0;
        for (std::size_t i = 0; i < s.issue->operationCount(); ++i) {
            common::OrderedCommandIssue::PayloadView view;
            if ((result.diagnostic = s.issue->beginOperation(epoch, view)))
                break;
            std::uint32_t value;
            std::memcpy(&value, view.data, sizeof(value));
            const bool ending = value == CommandState::endMarker(currentPass);
            if (value == CommandState::beginMarker(currentPass)) {
                const auto& pass = s.passes[currentPass];
                s.target = pass.target;
                s.framebuffer = pass.framebuffer;
                s.color = pass.color;
                s.depth = pass.depth;
                (*s.domain).nativeCall([&] {
                    return glBindFramebuffer(GL_FRAMEBUFFER, s.target->object->native);
                });
                const auto& size = s.target->extent();
                (*s.domain).nativeCall([&] {
                    return glDisable(GL_DITHER);
                });
                (*s.domain).nativeCall([&] {
                    return glDisable(GL_SCISSOR_TEST);
                });
                (*s.domain).nativeCall([&] {
                    return glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                });
                (*s.domain).nativeCall([&] {
                    return glDepthMask(GL_TRUE);
                });
                (*s.domain).nativeCall([&] {
                    return glStencilMaskSeparate(GL_FRONT, 0xff);
                });
                (*s.domain).nativeCall([&] {
                    return glStencilMaskSeparate(GL_BACK, 0xff);
                });
                (*s.domain).nativeCall([&] {
                    return glViewport(0, 0, size.width, size.height);
                });
                GLbitfield mask = 0;
                if (s.color.loadOp == LoadOp::Clear) {
                    const auto& c = s.color.clearColor;
                    (*s.domain).nativeCall([&] {
                        return glClearColor(c.r, c.g, c.b, c.a);
                    });
                    mask |= GL_COLOR_BUFFER_BIT;
                }
                if (s.target->depthStencil) {
                    if (s.depth.depthLoadOp == LoadOp::Clear) {
                        (*s.domain).nativeCall([&] {
                            return glClearDepthf(s.depth.depthClearValue);
                        });
                        mask |= GL_DEPTH_BUFFER_BIT;
                    }
                    if (s.depth.stencilLoadOp == LoadOp::Clear) {
                        (*s.domain).nativeCall([&] {
                            return glClearStencil(s.depth.stencilClearValue);
                        });
                        mask |= GL_STENCIL_BUFFER_BIT;
                    }
                }
                if (mask)
                    (*s.domain).nativeCall([&] {
                        return glClear(mask);
                    });
                result.diagnostic = s.domain->nativeResult();
            } else if (!ending) {
                result.diagnostic = issueDraw(s, s.draws[value]);
            } else {
                (*s.domain).nativeCall([&] {
                    return glBindFramebuffer(GL_FRAMEBUFFER, 0);
                });
                result.diagnostic = s.domain->nativeResult();
            }
            if (result.diagnostic)
                break;
            if ((result.diagnostic = s.issue->acceptOperation(epoch)))
                break;
            if (i == 0 && !acceptRecordedCommandOwnership(commands)) {
                result.diagnostic = "RHI-WEBGL1-SUBMIT-STATE-OWNERSHIP";
                break;
            }
            if (ending) {
                if (s.target->canvas) {
                    s.target->canvas->phase = WebGlCanvasPayload::Phase::Submitted;
                    ++currentPass;
                    continue;
                }
                auto& c = *s.target->color;
                c.gpuColorInitialized = semantics::AttachmentContentContract::afterStore(
                    s.color.loadOp, s.color.storeOp, initializedColor(c));
                c.cpuContentCurrent = false;
                if (c.content)
                    c.content->invalidate();
                ++c.revision;
                if (s.target->depthStencil) {
                    auto& z = *s.target->depthStencil;
                    z.gpuDepthInitialized = semantics::AttachmentContentContract::afterStore(
                        s.depth.depthLoadOp, s.depth.depthStoreOp, z.gpuDepthInitialized);
                    z.gpuStencilInitialized = semantics::AttachmentContentContract::afterStore(
                        s.depth.stencilLoadOp, s.depth.stencilStoreOp, z.gpuStencilInitialized);
                    z.cpuContentCurrent = false;
                    if (z.content)
                        z.content->invalidate();
                    ++z.revision;
                }
                ++currentPass;
            }
        }
        const auto stats = s.issue->stats();
        result.acceptedOperations = stats.acceptedOperations;
        result.mayHaveNativeSideEffects = stats.mayHaveNativeSideEffects;
        if (result.diagnostic) {
            for (std::uint32_t i = 0; i < s.passCount; ++i)
                if (auto& canvas = s.passes[i].target->canvas) {
                    canvas->phase = WebGlCanvasPayload::Phase::Failed;
                    canvas->state.invalidate();
                }
            for (std::uint32_t i = 0; i < s.textureUseCount; ++i) {
                auto& use = s.textureUses[i];
                if (use.recordedWrites)
                    use.texture->nativeContentsValid = false;
                if (use.transitioned)
                    use.texture->state.invalidate();
            }
            for (std::uint32_t i = 0; i < s.bufferUseCount; ++i)
                if (s.bufferUses[i].transitioned)
                    s.bufferUses[i].buffer->state.invalidate();
            s.journal->discard();
            s.issue->fail(common::OrderedCommandIssue::Failure::NativeCall);
            s.domain->failed = true;
            return result;
        }
        if ((result.diagnostic = s.issue->finishIssue(epoch)))
            return result;
        result.success = true;
        return result;
#endif
    };
    const auto result = execute();
    const auto outcome = result.success                ? semantics::WebGlSubmissionOutcome::Accepted
        : !resources.domain_->contextEpoch.isCurrent() ? semantics::WebGlSubmissionOutcome::Lost
                                                       : semantics::WebGlSubmissionOutcome::Failed;
    resources.domain_->telemetry.submission(outcome, result.acceptedOperations);
    return result;
}

namespace {
class GraphicsQueue final : public GpuQueue {
public:
    explicit GraphicsQueue(WebGlResources& resources) : resources_(resources) {}

    GpuQueueRole role() const override { return GpuQueueRole::Graphics; }

    std::uint32_t submitCount() const override { return submissions_; }

    QueueSubmitResult submit(const CommandBuffer& commands) override
    {
        if (submissions_ == UINT32_MAX)
            return {false, "RHI-WEBGL1-QUEUE-SUBMISSION-EXHAUSTED"};
        const auto result = WebGlExecution::submit(resources_, commands);
        if (result.mayHaveNativeSideEffects)
            ++submissions_;
        return {result.success,
            result.diagnostic ? result.diagnostic : "",
            result.success ? commands.commandCount() : result.acceptedOperations,
            result.mayHaveNativeSideEffects};
    }

    QueueSubmitResult submit(const CommandBuffer& commands, GpuFence* fence) override
    {
        if (fence)
            return {false, "RHI-WEBGL1-GPU-COMPLETION-UNAVAILABLE"};
        return submit(commands);
    }

    QueueSubmitResult submit(const CommandBuffer& commands,
        GpuTimelineSemaphore* wait,
        std::uint64_t waitValue,
        GpuTimelineSemaphore* signal,
        std::uint64_t signalValue,
        GpuFence* fence) override
    {
        if (wait || waitValue || signal || signalValue || fence)
            return {false, "RHI-WEBGL1-GPU-COMPLETION-UNAVAILABLE"};
        return submit(commands);
    }

private:
    WebGlResources& resources_;
    std::uint32_t submissions_ = 0;
};
} // namespace

core::UniquePtr<GpuQueue> WebGlExecution::createQueue(WebGlResources& resources)
{
    return core::makeUnique<GraphicsQueue>(resources);
}
} // namespace arp::rhi::webgl
