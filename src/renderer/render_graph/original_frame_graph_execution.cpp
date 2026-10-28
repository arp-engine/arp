#include "renderer/render_graph/original_frame_graph_execution.h"

#include "core/diagnostics/diagnostic_error.h"
#include "renderer/render_graph/frame_graph_executor.h"

namespace arp::render_graph {
namespace {
using namespace rhi;
using K = CommandUsageKind;
using Sources = OriginalCommandResourceSources;
using Intent = OriginalFrameGraphPass;

struct IntentFailure {
    const char* id;
};

void require(const char* id)
{
    if (id)
        throw IntentFailure{id};
}

void recorded(const CommandValidationResult& result)
{
    if (!result.success)
        throw core::DiagnosticError("RG-ORIGINAL-PASS-RECORDING");
}

template <class Source> const Source& source(const Sources& sources, std::size_t index)
{
    if (index >= sources.sources().size())
        throw IntentFailure{"RG-ORIGINAL-SOURCE-INDEX"};
    const auto* value = std::get_if<std::shared_ptr<const Source>>(&sources.sources()[index]);
    if (!value || !*value)
        throw IntentFailure{"RG-ORIGINAL-SOURCE-TYPE"};
    return **value;
}

template <class Source> std::size_t findSource(const Sources& sources, GpuResourceId id)
{
    std::size_t found = SIZE_MAX;
    for (std::size_t i = 0u; i < sources.sources().size(); ++i) {
        const auto* value = std::get_if<std::shared_ptr<const Source>>(&sources.sources()[i]);
        if (!value || !*value || (*value)->originalObject()->id() != id)
            continue;
        if (found != SIZE_MAX)
            throw IntentFailure{"RG-ORIGINAL-SOURCE-AMBIGUOUS"};
        found = i;
    }
    if (found == SIZE_MAX)
        throw IntentFailure{"RG-ORIGINAL-SOURCE-MISSING"};
    return found;
}

void renderIntent(const Intent& intent, RenderPassContext& context)
{
    auto& pass = context.renderPass();
    const auto& timing = intent.timing();
    if (timing)
        recorded(pass.writeTimestamp(
            *source<OriginalCommandQueryPoolSource>(*intent.sources, timing->sourceIndex)
                .originalObject(),
            timing->beginQuery));
    if (intent.kind == Intent::Kind::Clear)
        return;
    recorded(pass.setPipeline(
        *source<OriginalCommandGraphicsPipelineSource>(*intent.sources, intent.pipeline)
            .originalObject()));
    recorded(pass.setViewport(intent.viewport));
    recorded(pass.setScissor(intent.scissor));
    recorded(pass.bindDescriptorSet(0u,
        *source<OriginalCommandDescriptorSetSource>(*intent.sources, intent.descriptors)
            .originalObject(),
        nullptr,
        0u));
    recorded(pass.draw(intent.draw.vertices,
        intent.draw.instances,
        intent.draw.firstVertex,
        intent.draw.firstInstance));
    if (timing)
        recorded(pass.writeTimestamp(
            *source<OriginalCommandQueryPoolSource>(*intent.sources, timing->sourceIndex)
                .originalObject(),
            timing->endQuery));
}

void computeIntent(const Intent& intent, RenderContext& context)
{
    auto pass = context.commands().beginComputePass({});
    if (!pass)
        throw core::DiagnosticError("RG-ORIGINAL-COMPUTE-BEGIN");
    const auto& timing = intent.timing();
    if (timing)
        recorded(pass->writeTimestamp(
            *source<OriginalCommandQueryPoolSource>(*intent.sources, timing->sourceIndex)
                .originalObject(),
            timing->beginQuery));
    recorded(pass->setPipeline(
        *source<OriginalCommandComputePipelineSource>(*intent.sources, intent.pipeline)
            .originalObject()));
    recorded(pass->bindDescriptorSet(0u,
        *source<OriginalCommandDescriptorSetSource>(*intent.sources, intent.descriptors)
            .originalObject(),
        nullptr,
        0u));
    recorded(pass->dispatch(intent.groups.width, intent.groups.height, intent.groups.depth));
    if (timing)
        recorded(pass->writeTimestamp(
            *source<OriginalCommandQueryPoolSource>(*intent.sources, timing->sourceIndex)
                .originalObject(),
            timing->endQuery));
    recorded(pass->end());
}

void copyIntent(const Intent& intent, RenderContext& context)
{
    auto pass = context.commands().beginTransferPass({});
    if (!pass)
        throw core::DiagnosticError("RG-ORIGINAL-TRANSFER-BEGIN");
    recorded(pass->copyTextureToBuffer(intent.copy));
    recorded(pass->end());
}

// Only this domain consumes the assembler. All admission, ownership and state
// semantics remain in the existing original RHI factories.
class IntentAssembler final {
public:
    explicit IntentAssembler(const std::shared_ptr<const Sources>& sources)
        : sources_(sources), commands_(&core::defaultCpuMemoryResource()),
          references_(&core::defaultCpuMemoryResource()),
          arguments_(&core::defaultCpuMemoryResource()),
          descriptors_(&core::defaultCpuMemoryResource()),
          queryPoints_(&core::defaultCpuMemoryResource())
    {}

    template <class Value>
    std::size_t scalar(K kind, Value value, std::initializer_list<std::size_t> operands = {})
    {
        const auto captured = CommandScalarArguments::capture(kind, value);
        if (!captured || commands_.size() >= UINT32_MAX
            || references_.size() > UINT32_MAX - operands.size())
            throw IntentFailure{"RG-ORIGINAL-INTENT-RANGE"};
        const auto ordinal = commands_.size();
        commands_.push_back({kind,
            static_cast<std::uint32_t>(references_.size()),
            static_cast<std::uint32_t>(operands.size())});
        for (const auto index : operands) {
            if (index > UINT32_MAX)
                throw IntentFailure{"RG-ORIGINAL-INTENT-RANGE"};
            references_.push_back({static_cast<std::uint32_t>(index)});
        }
        arguments_.push_back({*captured});
        return ordinal;
    }

    std::size_t empty(K kind, std::initializer_list<std::size_t> operands = {})
    {
        return scalar(kind, CommandScalarArguments::None{}, operands);
    }

    void barrier(const BarrierTraceRecord& b, FrameGraph& graph)
    {
        // ResourceBarrierDesc preserves the complete dependency when before
        // equals after. Use the same explicit transition opcode in projection
        // and execution; no dependency or stage/access mask is discarded.
        if (b.before == b.after && b.srcAccess == rhi::ResourceAccess::None)
            return;
        ResourceBarrierDesc d;
        d.before = b.before;
        d.after = b.after;
        d.sourceStages = b.srcStage;
        d.destinationStages = b.dstStage;
        d.sourceAccess = b.srcAccess;
        d.destinationAccess = b.dstAccess;
        const bool texture = b.kind == ResourceKind::Texture;
        std::size_t index;
        if (texture) {
            auto& object = graph.texture(TextureHandle{b.handleId});
            d.resource = object.id();
            d.textureSubresources = defaultTextureSubresourceRange(object.desc());
            index = findSource<OriginalCommandTextureLease>(*sources_, d.resource);
        } else {
            d.resource = graph.buffer(BufferHandle{b.handleId}).id();
            index = findSource<OriginalCommandBufferLease>(*sources_, d.resource);
        }
        const auto kind = texture ? K::TextureBarrier : K::BufferBarrier;
        auto captured = ResourceDependencyArguments::capture(kind, d);
        if (!captured)
            throw IntentFailure{"RG-ORIGINAL-BARRIER-ARGUMENTS"};
        descriptors_.push_back({empty(kind, {index}), *captured});
    }

    void pass(const Intent& intent,
        const RenderPassDesc* render,
        const RenderPassArgumentsBudget& argumentsBudget,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const std::optional<Intent::TimingBinding>& timing)
    {
        const bool graphics = intent.kind == Intent::Kind::Graphics;
        std::size_t begin;
        if (render) {
            const auto first = references_.size();
            const auto ordinal = commands_.size();
            empty(K::BeginRenderPass);
            for (const auto& attachment : render->colorAttachments)
                references_.push_back({static_cast<std::uint32_t>(
                    findSource<OriginalCommandTextureViewSource>(*sources_, attachment.target))});
            if (render->hasDepthStencilAttachment)
                references_.push_back(
                    {static_cast<std::uint32_t>(findSource<OriginalCommandTextureViewSource>(
                        *sources_, render->depthStencilAttachment.target))});
            commands_.back().referenceCount =
                static_cast<std::uint32_t>(references_.size() - first);
            const char* id = nullptr;
            auto arguments =
                RenderPassArguments::prepareOriginal(*render, argumentsBudget, quota, id);
            require(id);
            descriptors_.push_back({ordinal, arguments});
            begin = ordinal;
        } else
            begin = empty(
                intent.kind == Intent::Kind::Compute ? K::BeginComputePass : K::BeginTransferPass);
        if (timing)
            timestamp(*timing, timing->beginQuery);
        if (intent.kind == Intent::Kind::Compute || graphics) {
            const auto pipelineKind = graphics ? K::BindGraphicsPipeline : K::BindComputePipeline;
            const auto descriptorKind =
                graphics ? K::BindRenderDescriptor : K::BindComputeDescriptor;
            const auto generation = graphics
                ? source<OriginalCommandGraphicsPipelineSource>(*sources_, intent.pipeline)
                      .originalObject()
                      ->originalResourceRegistration()
                      .generation()
                : source<OriginalCommandComputePipelineSource>(*sources_, intent.pipeline)
                      .originalObject()
                      ->originalResourceRegistration()
                      .generation();
            descriptors_.push_back({empty(pipelineKind, {intent.pipeline}),
                OriginalCommandPipelineBinding{pipelineKind, intent.pipeline, begin, generation}});
            if (graphics) {
                scalar(K::SetViewport, intent.viewport);
                scalar(K::SetScissor, intent.scissor);
            }
            const auto& set =
                *source<OriginalCommandDescriptorSetSource>(*sources_, intent.descriptors)
                     .originalObject();
            descriptors_.push_back({scalar(descriptorKind,
                                        CommandScalarArguments::DescriptorBinding{0u},
                                        {intent.descriptors}),
                OriginalCommandDescriptorBinding{descriptorKind,
                    intent.descriptors,
                    intent.pipeline,
                    begin,
                    set.originalResourceRegistration().generation(),
                    set.contentRevision()}});
            descriptorElements_ +=
                source<OriginalCommandDescriptorSetSource>(*sources_, intent.descriptors)
                    .frozenElements()
                    .size();
            if (graphics)
                scalar(K::Draw, intent.draw);
            else
                scalar(K::Dispatch,
                    CommandScalarArguments::Dispatch{
                        intent.groups.width, intent.groups.height, intent.groups.depth});
        } else if (intent.kind == Intent::Kind::TextureToBuffer) {
            const auto copy = TransferCopyArguments::capture(intent.copy);
            if (!copy)
                throw IntentFailure{"RG-ORIGINAL-COPY-ARGUMENTS"};
            descriptors_.push_back(
                {empty(K::TextureToBufferCopy,
                     {findSource<OriginalCommandTextureLease>(*sources_, intent.copy.sourceTexture),
                         findSource<OriginalCommandBufferLease>(
                             *sources_, intent.copy.destinationBuffer)}),
                    *copy});
        }
        if (timing)
            timestamp(*timing, timing->endQuery);
        empty(render                                   ? K::EndRenderPass
                : intent.kind == Intent::Kind::Compute ? K::EndComputePass
                                                       : K::EndTransferPass);
    }

    std::shared_ptr<const OriginalCommandStreamProgram> finish(GpuDevice& device,
        const OriginalFrameGraphBudget& b,
        const std::shared_ptr<core::CpuMemoryQuota>& intentQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& stateQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& recordingQuota)
    {
        empty(K::Finish);
        const char* id = nullptr;
        auto usage = OriginalCommandUsage::prepareOriginal(sources_,
            commands_.data(),
            commands_.size(),
            references_.data(),
            references_.size(),
            intentQuota,
            id);
        require(id);
        auto arguments = CommandVariableArguments::prepareOriginal(arguments_.data(),
            arguments_.size(),
            {1u,
                true,
                static_cast<std::uint32_t>(commands_.size()),
                0u,
                0u,
                0u,
                b.maxCpuIntentBytes},
            intentQuota,
            id);
        require(id);
        auto scalars = OriginalCommandScalars::prepareOriginal(usage, arguments, intentQuota, id);
        require(id);
        auto inputBudget = b.inputs;
        if (b.timing) {
            inputBudget.queryPoints = QueryCommandPoints::prepareOriginal(arguments,
                queryPoints_.data(),
                queryPoints_.size(),
                b.timing->points,
                intentQuota,
                id);
            require(id);
        }
        auto inputs = OriginalCommandInputs::prepareOriginal(
            scalars, descriptors_.data(), descriptors_.size(), inputBudget, intentQuota, id);
        require(id);
        auto states = OriginalCommandResourceStates::prepareOriginal(
            inputs, device, b.states, stateQuota, id);
        require(id);
        CommandRecordingBudget recording{1u, true, commands_.size()};
        for (const auto& command : commands_)
            recording.limits.append(command.kind);
        recording.limits.resourceReferences = references_.size();
        recording.limits.descriptorElements = descriptorElements_;
        auto program = OriginalCommandStreamProgram::prepareRecordingBoundOriginalTyped(
            states, b.stream, recording, recordingQuota, id);
        require(id);
        return program;
    }

private:
    void timestamp(const Intent::TimingBinding& binding, std::uint32_t slot)
    {
        const auto& pool = *source<OriginalCommandQueryPoolSource>(*sources_, binding.sourceIndex)
                                .originalObject();
        const auto original = QueryPoolOriginalInputArguments::captureOriginal(
            K::Timestamp, slot, pool.originalInput());
        if (!original)
            throw IntentFailure{"RG-ORIGINAL-TIMING-QUERY-ARGUMENTS"};
        const auto ordinal = scalar(K::Timestamp,
            *original->scalar().get<CommandScalarArguments::Query>(),
            {binding.sourceIndex});
        queryPoints_.push_back({ordinal, *original});
        descriptors_.push_back({ordinal,
            OriginalCommandQueryBinding{
                binding.sourceIndex, pool.originalResourceRegistration().generation(), *original}});
    }

    const std::shared_ptr<const Sources>& sources_;
    core::pmr::Vector<CommandUsage> commands_;
    core::pmr::Vector<OriginalCommandUsage::Reference> references_;
    core::pmr::Vector<CommandVariableArguments::Input> arguments_;
    core::pmr::Vector<OriginalCommandInputs::OriginalDescriptor> descriptors_;
    core::pmr::Vector<QueryCommandPoints::Input> queryPoints_;
    std::uint64_t descriptorElements_ = 0u;
};
} // namespace

OriginalFrameGraphExecution::OriginalFrameGraphExecution(FrameGraph& graph,
    std::shared_ptr<const Sources> sources,
    std::shared_ptr<core::CpuMemoryQuota> intentQuota)
    : graph_(graph), sources_(std::move(sources)), intentQuota_(std::move(intentQuota))
{
    if (!sources_ || !intentQuota_)
        throw core::DiagnosticError("RG-ORIGINAL-INPUT-REQUIRED");
}

const char* OriginalFrameGraphExecution::bindAttachment(
    TextureHandle handle, std::size_t index) noexcept
{
    try {
        const auto& view = source<OriginalCommandTextureViewSource>(*sources_, index);
        auto& record = graph_.textureRecord(handle);
        if (record.resolveTexture() != view.originalParentLease()->originalObject()
            || view.originalViewLease()->layout().usage != TextureUsage::RenderAttachment)
            return "RG-ORIGINAL-ATTACHMENT-PARENT";
        graph_.invalidateCompiledRetirementSource();
        record.originalAttachmentView =
            std::get<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                sources_->sources()[index]);
        return nullptr;
    } catch (const IntentFailure& e) {
        return e.id;
    } catch (...) {
        return "RG-ORIGINAL-ATTACHMENT-HANDLE";
    }
}

PassBuilder OriginalFrameGraphExecution::add(
    core::pmr::String name, const std::shared_ptr<const Intent>& intent)
{
    auto builder = graph_.addPass(std::move(name));
    const auto* borrowed = intent.get();
    if (intent->kind == Intent::Kind::Compute) {
        builder.kind(PassKind::Compute).queue(QueueRole::Graphics);
        builder.execute([borrowed](RenderContext& context) {
            computeIntent(*borrowed, context);
        });
    } else if (intent->kind == Intent::Kind::TextureToBuffer) {
        builder.kind(PassKind::Copy).queue(QueueRole::Graphics);
        builder.execute([borrowed](RenderContext& context) {
            copyIntent(*borrowed, context);
        });
    } else
        builder.executeRenderPass([borrowed](RenderPassContext& context) {
            renderIntent(*borrowed, context);
        });
    graph_.passes_.back().originalIntent = intent;
    return builder;
}

PassBuilder OriginalFrameGraphExecution::clear(core::pmr::String name)
{
    core::CpuMemoryQuotaScope scope(intentQuota_);
    auto intent = core::makeShared<Intent>();
    intent->sources = sources_;
    return add(std::move(name), intent);
}

PassBuilder OriginalFrameGraphExecution::compute(
    core::pmr::String name, std::size_t pipeline, std::size_t descriptors, Extent3D groups)
{
    core::CpuMemoryQuotaScope scope(intentQuota_);
    auto intent = core::makeShared<Intent>();
    intent->sources = sources_;
    intent->kind = Intent::Kind::Compute;
    intent->pipeline = pipeline;
    intent->descriptors = descriptors;
    intent->groups = groups;
    return add(std::move(name), intent);
}

PassBuilder OriginalFrameGraphExecution::graphics(core::pmr::String name,
    std::size_t pipeline,
    std::size_t descriptors,
    ViewportState viewport,
    ScissorState scissor,
    CommandScalarArguments::Draw draw)
{
    core::CpuMemoryQuotaScope scope(intentQuota_);
    auto intent = core::makeShared<Intent>();
    intent->sources = sources_;
    intent->kind = Intent::Kind::Graphics;
    intent->pipeline = pipeline;
    intent->descriptors = descriptors;
    intent->viewport = viewport;
    intent->scissor = scissor;
    intent->draw = draw;
    return add(std::move(name), intent);
}

PassBuilder OriginalFrameGraphExecution::copy(
    core::pmr::String name, const TextureToBufferCopyDesc& copy)
{
    core::CpuMemoryQuotaScope scope(intentQuota_);
    auto intent = core::makeShared<Intent>();
    intent->sources = sources_;
    intent->kind = Intent::Kind::TextureToBuffer;
    intent->copy = copy;
    return add(std::move(name), intent);
}

const char* OriginalFrameGraphExecution::prepare(const OriginalFrameGraphBudget& b,
    const std::shared_ptr<core::CpuMemoryQuota>& intentQuota,
    const std::shared_ptr<core::CpuMemoryQuota>& stateQuota,
    const std::shared_ptr<core::CpuMemoryQuota>& recordingQuota)
{
    if (const auto* id = validateBudget(b))
        return id;
    if (!intentQuota || intentQuota != intentQuota_
        || intentQuota->limitBytes() != b.maxCpuIntentBytes)
        return "RG-ORIGINAL-INTENT-QUOTA";
    if (encoder_ || executed_ || graph_.hasExecutedCompiledGraph_ || graph_.submissionFence_)
        return "RG-ORIGINAL-ALREADY-PREPARED";
    const char* id = nullptr;
    auto* extension =
        OriginalPreparedCommandDeviceExtension::query(graph_.device_, b.preparation, id);
    if (!extension)
        return id;
    if ((id = Sources::validateRegisteredLifetime(sources_, graph_.device_)))
        return id;
    if (!graph_.subgraphs_.empty() || !graph_.subpassGraphs_.empty())
        return "RG-ORIGINAL-COMPOSITION-PROFILE";
    if (!b.timing && !graph_.gpuTimingPassScopes_.empty())
        return "RG-ORIGINAL-COMPOSITION-PROFILE";
    if (b.timing && graph_.gpuTimingPassScopes_.empty())
        return "RG-ORIGINAL-TIMING-SCOPES-REQUIRED";
    for (const auto& texture : graph_.textures_)
        if (!texture.imported || !texture.resolveTexture() || texture.transient || texture.placed)
            return "RG-ORIGINAL-TEXTURE-IMPORT";
    for (const auto& buffer : graph_.buffers_)
        if (!buffer.imported() || buffer.transient)
            return "RG-ORIGINAL-BUFFER-IMPORT";
    try {
        core::CpuMemoryQuotaScope scope(intentQuota);
        auto preparedGraph = core::makeUnique<CompiledFrameGraph>(graph_.compile());
        if (!preparedGraph->aliasPlan().empty() || !preparedGraph->aliasHandoffs().empty())
            return "RG-ORIGINAL-ALIAS-PROFILE";
        core::pmr::Vector<std::optional<Intent::TimingBinding>> timings(
            graph_.passes_.size(), &core::defaultCpuMemoryResource());
        if (b.timing) {
            const auto& pool =
                *source<OriginalCommandQueryPoolSource>(*sources_, b.timing->querySourceIndex)
                     .originalObject();
            const auto input = pool.originalInput();
            if (!input || graph_.gpuTimingPassScopes_.size() != input->scopeIds().size())
                return "RG-ORIGINAL-TIMING-SCOPE-COVERAGE";
            for (const auto& requested : graph_.gpuTimingPassScopes_) {
                std::size_t slot = SIZE_MAX, matchedPass = SIZE_MAX;
                for (std::size_t i = 0u; i < input->scopeIds().size(); ++i)
                    if (requested.scopeId && input->scopeIds()[i] == requested.scopeId)
                        slot = i;
                if (slot == SIZE_MAX)
                    return "RG-ORIGINAL-TIMING-SCOPE-ID";
                for (const auto index : preparedGraph->executionOrder()) {
                    const auto& compiled = preparedGraph->passes()[index];
                    if (compiled.name != requested.passName || compiled.culled)
                        continue;
                    if (matchedPass != SIZE_MAX)
                        return "RG-ORIGINAL-TIMING-PASS-AMBIGUOUS";
                    matchedPass = compiled.originalPassIndex;
                }
                if (matchedPass == SIZE_MAX || timings[matchedPass])
                    return "RG-ORIGINAL-TIMING-PASS-MATCH";
                const auto& intent = graph_.passes_[matchedPass].originalIntent;
                if (!intent
                    || (intent->kind != Intent::Kind::Compute
                        && intent->kind != Intent::Kind::Graphics)
                    || requested.callbackOwnsTimestamps != (intent->kind == Intent::Kind::Compute))
                    return "RG-ORIGINAL-TIMING-PASS-KIND";
                for (const auto& timing : timings)
                    if (timing && timing->beginQuery == slot * 2u)
                        return "RG-ORIGINAL-TIMING-SCOPE-DUPLICATE";
                timings[matchedPass] = Intent::TimingBinding{b.timing->querySourceIndex,
                    static_cast<std::uint32_t>(slot * 2u),
                    static_cast<std::uint32_t>(slot * 2u + 1u)};
            }
        }
        IntentAssembler assembler(sources_);
        const auto barriers = [&](std::uint32_t pass) {
            for (const auto& batch : preparedGraph->passBarrierBatches())
                if (batch.passOriginalIndex == pass)
                    for (const auto& barrier : batch.prePassBarriers)
                        assembler.barrier(barrier, graph_);
        };
        for (const auto index : preparedGraph->executionOrder()) {
            const auto& compiled = preparedGraph->passes()[index];
            if (compiled.culled)
                continue;
            auto& pass = graph_.passes_[compiled.originalPassIndex];
            if (!pass.originalIntent || pass.originalIntent->sources != sources_
                || pass.subpassGroup || !pass.subpassInputBindings.empty()
                || pass.queue != QueueRole::Graphics || pass.asyncComputeEligible
                || !pass.rhiDrawCommands.empty() || !pass.rhiDispatchCommands.empty()
                || !pass.rhiTransferCommands.empty())
                return "RG-ORIGINAL-TYPED-PASS-REQUIRED";
            barriers(static_cast<std::uint32_t>(compiled.originalPassIndex));
            const bool render = pass.originalIntent->kind == Intent::Kind::Clear
                || pass.originalIntent->kind == Intent::Kind::Graphics;
            if (render != bool(pass.renderPassCallback)
                || render != (pass.kind == PassKind::Graphics) || (!render && !pass.callback))
                return "RG-ORIGINAL-PASS-KIND";
            if (render) {
                for (const auto& attachment : pass.attachmentWrites)
                    if (!graph_.textureRecord(attachment.handle).originalAttachmentView)
                        return "RG-ORIGINAL-ATTACHMENT-VIEW-REQUIRED";
                pass.originalRenderPass = graph_.renderPassDescFor(pass);
                const auto& descriptor = *pass.originalRenderPass;
                assembler.pass(*pass.originalIntent,
                    &descriptor,
                    b.preparation.storage.arguments,
                    intentQuota,
                    timings[compiled.originalPassIndex]);
            } else
                assembler.pass(*pass.originalIntent,
                    nullptr,
                    b.preparation.storage.arguments,
                    intentQuota,
                    timings[compiled.originalPassIndex]);
        }
        barriers(UINT32_MAX);
        auto program = assembler.finish(graph_.device_, b, intentQuota, stateQuota, recordingQuota);
        auto encoder = extension->prepare(program, b.preparation, diagnostic_);
        if (!encoder)
            return diagnostic_.empty() ? "RG-ORIGINAL-ENCODER-PREPARATION" : diagnostic_.c_str();
        auto completion = core::shareUnique(graph_.device_.createFence());
        if (!completion) {
            encoder.reset();
            return "RG-ORIGINAL-COMPLETION-FENCE";
        }
        graph_.executionTrace_.clear();
        graph_.passTrace_.clear();
        graph_.resourceTrace_.clear();
        graph_.barrierTrace_.clear();
        graph_.attachmentTrace_.clear();
        graph_.aliasTrace_.clear();
        for (const auto& pass : preparedGraph->passes())
            graph_.passTrace_.push_back(
                {pass.name, pass.kind, pass.queue, pass.asyncComputeEligible, pass.culled});
        const auto traceBarriers = [&](std::uint32_t index) {
            for (const auto& batch : preparedGraph->passBarrierBatches())
                if (batch.passOriginalIndex == index)
                    for (const auto& barrier : batch.prePassBarriers) {
                        graph_.barrierTrace_.push_back(barrier);
                        const auto access = barrier.before == ResourceState::Undefined
                                || barrier.dstStage == PipelineStage::TopOfPipe
                            ? arp::render_graph::ResourceAccess::Write
                            : arp::render_graph::ResourceAccess::Read;
                        const auto& label = barrier.kind == ResourceKind::Texture
                            ? graph_.texture(TextureHandle{barrier.handleId}).desc().label
                            : graph_.buffer(BufferHandle{barrier.handleId}).desc().label;
                        graph_.resourceTrace_.push_back(
                            {barrier.passName, label, barrier.kind, access, barrier.after});
                    }
        };
        graph_.reuseCompiledTrace_ = false;
        graph_.attachmentTraceCursor_ = 0u;
        for (const auto index : preparedGraph->executionOrder()) {
            const auto& compiled = preparedGraph->passes()[index];
            if (compiled.culled)
                continue;
            const auto& pass = graph_.passes_[compiled.originalPassIndex];
            graph_.executionTrace_.push_back(pass.name);
            traceBarriers(static_cast<std::uint32_t>(compiled.originalPassIndex));
            graph_.recordAttachmentTrace(pass);
        }
        traceBarriers(UINT32_MAX);
        if (!graph_.originalIntentEpoch_)
            graph_.originalIntentEpoch_.emplace();
        graphEpoch_ = graph_.originalIntentEpoch_->ticket();
        if (!graphEpoch_.isCurrent()) {
            encoder.reset();
            return "RG-ORIGINAL-GRAPH-EPOCH";
        }
        for (std::size_t i = 0u; i < graph_.passes_.size(); ++i)
            if (graph_.passes_[i].originalIntent)
                graph_.passes_[i].originalIntent->timing_ = timings[i];
        compiled_ = std::move(preparedGraph);
        program_ = std::move(program);
        encoder_ = std::move(encoder);
        completion_ = std::move(completion);
        return nullptr;
    } catch (const IntentFailure& failure) {
        return failure.id;
    } catch (const std::bad_alloc&) {
        return "RG-ORIGINAL-CPU-ALLOCATION";
    } catch (const std::length_error&) {
        return "RG-ORIGINAL-CPU-ALLOCATION";
    }
}

const char* OriginalFrameGraphExecution::cancel() noexcept
{
    if (executed_)
        return "RG-ORIGINAL-CANCEL-AFTER-EXECUTION";
    encoder_.reset();
    program_.reset();
    compiled_.reset();
    completion_.reset();
    graphEpoch_ = {};
    for (auto& pass : graph_.passes_) {
        pass.originalRenderPass.reset();
        if (pass.originalIntent)
            pass.originalIntent->timing_.reset();
    }
    return nullptr;
}

void OriginalFrameGraphExecution::execute()
{
    if (!encoder_ || !compiled_ || !program_ || !completion_ || executed_)
        throw core::DiagnosticError("RG-ORIGINAL-NOT-PREPARED");
    if (!graphEpoch_.isCurrent())
        throw core::DiagnosticError("RG-ORIGINAL-GRAPH-CHANGED");
    graph_.validateSubmissionObservers(*compiled_);
    executed_ = true;
    FrameGraphExecutor(graph_).executeImpl(
        *compiled_, std::move(encoder_), nullptr, nullptr, nullptr, std::move(completion_), true);
    graph_.hasExecutedCompiledGraph_ = true;
    graph_.executedCompiledGraph_ = nullptr;
    graph_.reuseCompiledTrace_ = false;
}
} // namespace arp::render_graph
