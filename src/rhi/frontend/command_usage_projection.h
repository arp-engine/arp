#pragma once

#include "core/algorithm/ordered_interval_neighborhood.h"
#include "rhi/frontend/command_shader_usage_validation.h"
#include "rhi/frontend/command_shader_validation.h"
#include "rhi/frontend/command_state.h"
#include "rhi/frontend/command_stream_program.h"
#include "rhi/semantics/command_lowering.h"

// region Command typed usage projection
namespace arp::rhi {
class OriginalGeneralOutputSnapshotContract;
}

namespace arp::rhi::common {
// Ephemeral original projection. No second source inventory, state owner or
// journal. Stream2 remains committed-state validated, with no pending bypass.
class OriginalTypedUsageProjection final {
    friend class OriginalTypedDependencyProjection;
    friend class arp::rhi::OriginalGeneralOutputSnapshotContract;
    using C = OriginalCommandDescriptorConsumption;
    using ScopeContract = semantics::TypedShaderUsageScopeContract;
    using K = CommandUsageKind;
    using Stream = OriginalCommandStreamProgram;

public:
    using Budget = OriginalTypedUsageProjectionContract::Budget;

    struct BufferPoint {
        C::Access original;
        ScopeContract::BufferUse use;
    };

    // Fixed-function binding provenance is distinct from descriptor and
    // transfer provenance. The original Inputs root retains the buffer owner.
    struct GeometryPoint {
        OriginalCommandGeometryBindingContract::Access original;
        ScopeContract::BufferUse use;
    };

    struct AttachmentPoint {
        OriginalCommandTextureRanges::Point original;
        GpuResourceId resource;
        ScopeContract::Scope scope;
        semantics::ResourceAccessScope access;
        std::uint64_t registrationGeneration = 0u;
    };

    struct TexturePoint {
        C::Access original;
        ScopeContract::TextureUse use;
    };

    // Borrowed only during a validated visit; the original Inputs graph owns
    // the readback source and both canonical operands. Descriptor provenance
    // is never fabricated for transfer contributions.
    struct ReadbackBufferPoint {
        const OriginalCommandTextureReadbackSource* original = nullptr;
        std::size_t sourceIndex = SIZE_MAX, destinationSourceIndex = SIZE_MAX;
        std::uint64_t registrationGeneration = 0u;
        ScopeContract::BufferUse use;
    };

    struct ReadbackTexturePoint {
        const OriginalCommandTextureReadbackSource* original = nullptr;
        std::size_t sourceIndex = SIZE_MAX, parentSourceIndex = SIZE_MAX;
        ScopeContract::TextureUse use;
    };

    using Point = std::variant<BufferPoint,
        AttachmentPoint,
        TexturePoint,
        ReadbackBufferPoint,
        ReadbackTexturePoint,
        GeometryPoint>;

    struct Counts {
        std::uint64_t renderBindings = 0u, renderBindingBuffers = 0u;
        std::uint64_t shaderConsumptions = 0u, shaderBuffers = 0u, attachments = 0u,
                      contributions = 0u;
        std::uint64_t renderBindingTextures = 0u, shaderTextures = 0u, textureCells = 0u;
        std::uint64_t readbacks = 0u, readbackMips = 0u, readbackDeclaredBytes = 0u;
        std::uint64_t geometryBindings = 0u, geometryDraws = 0u, geometryDrawAccesses = 0u;
        std::uint64_t renderBindingSamplers = 0u, shaderSamplers = 0u;

        bool matches(const Counts& other) const noexcept
        {
            return std::tie(renderBindings,
                       renderBindingBuffers,
                       shaderConsumptions,
                       shaderBuffers,
                       attachments,
                       contributions,
                       renderBindingTextures,
                       shaderTextures,
                       textureCells,
                       readbacks,
                       readbackMips,
                       readbackDeclaredBytes,
                       geometryBindings,
                       geometryDraws,
                       geometryDrawAccesses,
                       renderBindingSamplers,
                       shaderSamplers)
                == std::tie(other.renderBindings,
                    other.renderBindingBuffers,
                    other.shaderConsumptions,
                    other.shaderBuffers,
                    other.attachments,
                    other.contributions,
                    other.renderBindingTextures,
                    other.shaderTextures,
                    other.textureCells,
                    other.readbacks,
                    other.readbackMips,
                    other.readbackDeclaredBytes,
                    other.geometryBindings,
                    other.geometryDraws,
                    other.geometryDrawAccesses,
                    other.renderBindingSamplers,
                    other.shaderSamplers);
        }
    };

    struct Result {
        const char* diagnostic = nullptr;
        Counts counts;

        bool synchronizationProven() const noexcept { return false; }

        bool nativeExecutionProven() const noexcept { return false; }
    };

    static Result inspect(const std::shared_ptr<const Stream>& stream,
        const GpuDevice& device,
        const Budget& budget) noexcept
    {
        return walk(stream, device, budget, [](const Point&) noexcept -> const char* {
            return nullptr;
        });
    }

    template <class Visitor>
    static Result visit(const std::shared_ptr<const Stream>& stream,
        const GpuDevice& device,
        const Budget& budget,
        Visitor&& visitor) noexcept
    {
        static_assert(noexcept(visitor(std::declval<const Point&>())),
            "Original usage visitor must not throw");
        const auto preflight = inspect(stream, device, budget);
        if (preflight.diagnostic)
            return preflight;
        const auto replay = walk(stream, device, budget, std::forward<Visitor>(visitor));
        if (!replay.diagnostic && !preflight.counts.matches(replay.counts))
            return {"RHI-ORIGINAL-USAGE-PROJECTION-REPLAY"};
        return replay;
    }

    using StateBoundRequest = OriginalTypedStateBoundUsageContract::Request;

    static Result inspectStateBound(const std::shared_ptr<const Stream>& stream,
        const std::shared_ptr<const OriginalCommandResourceStates>& graph,
        const StateBoundRequest& request) noexcept
    {
        return walkStateBound(stream, graph, request, [](const Point&) noexcept -> const char* {
            return nullptr;
        });
    }

    template <class Visitor>
    static Result visitStateBound(const std::shared_ptr<const Stream>& stream,
        const std::shared_ptr<const OriginalCommandResourceStates>& graph,
        const StateBoundRequest& request,
        Visitor&& visitor) noexcept
    {
        static_assert(noexcept(visitor(std::declval<const Point&>())),
            "Original state-bound visitor must not throw");
        const auto preflight = inspectStateBound(stream, graph, request);
        if (preflight.diagnostic)
            return preflight;
        const auto replay = walkStateBound(stream, graph, request, std::forward<Visitor>(visitor));
        if (!replay.diagnostic && !preflight.counts.matches(replay.counts))
            return {"RHI-ORIGINAL-USAGE-PROJECTION-REPLAY"};
        return replay;
    }

private:
    // Borrowed, private and scoped to one serialized CPU projection. It is
    // issued only after the complete original join and footprint validation;
    // it never authorizes pending state, native commands or a persistent cache.
    class CapturedJoin final {
        friend class OriginalTypedUsageProjection;

        CapturedJoin(const std::shared_ptr<const Stream>& stream,
            const std::shared_ptr<const OriginalCommandResourceStates>& graph,
            StateBoundRequest request) noexcept
            : stream_(stream), graph_(graph), request_(request)
        {}

        const std::shared_ptr<const Stream>& stream_;
        const std::shared_ptr<const OriginalCommandResourceStates>& graph_;
        const StateBoundRequest request_;

    public:
        CapturedJoin(const CapturedJoin&) = default;
    };

    static std::optional<CapturedJoin> captureStateBound(
        const std::shared_ptr<const Stream>& stream,
        const std::shared_ptr<const OriginalCommandResourceStates>& graph,
        const StateBoundRequest& request,
        const char*& diagnostic) noexcept
    {
        const auto validated = inspectStateBound(stream, graph, request);
        diagnostic = validated.diagnostic;
        return diagnostic ? std::optional<CapturedJoin>{}
                          : std::optional<CapturedJoin>{CapturedJoin{stream, graph, request}};
    }

    static const char* resolveCapturedBuffer(const CapturedJoin& joined,
        GpuResourceId resource,
        std::size_t beforeCommand,
        ResourceState& output) noexcept
    {
        if (const auto* id = joined.graph_->validateTypedProjectionJoin())
            return id;
        return OriginalCommandStateSequence::bufferBefore(
            *joined.graph_, resource, beforeCommand, output);
    }

    static const char* resolveCapturedTextureCell(const CapturedJoin& joined,
        std::size_t observationIndex,
        std::uint64_t ordinal,
        std::size_t beforeCommand,
        ResourceState& output) noexcept
    {
        if (const auto* id = joined.graph_->validateTypedProjectionJoin())
            return id;
        return OriginalCommandStateSequence::resolveValidatedTextureCell(
            *joined.graph_, observationIndex, ordinal, beforeCommand, output);
    }

    template <class Visitor>
    static Result visitCaptured(const CapturedJoin& joined, Visitor&& visitor) noexcept
    {
        return visitImmutableBindings(
            joined.stream_, joined.graph_, joined.request_, std::forward<Visitor>(visitor));
    }

    template <class Visitor>
    static Result visitImmutableBindings(const std::shared_ptr<const Stream>& stream,
        const std::shared_ptr<const OriginalCommandResourceStates>& graph,
        const StateBoundRequest& request,
        Visitor&& visitor) noexcept
    {
        if (const auto* id = graph->validateTypedProjectionJoin())
            return {id, {}};
        const auto render = [&](auto&& consume) noexcept {
            return graph->visitCapturedRenderAccesses(
                request.projection.renderBindings, std::forward<decltype(consume)>(consume));
        };
        const auto shader = [&](auto&& consume) noexcept {
            return graph->visitCapturedDescriptorAccesses(
                request.projection.shaderConsumption, std::forward<decltype(consume)>(consume));
        };
        const auto geometry = [&](auto&& consume) noexcept {
            return graph->visitGeometryAccesses(
                request.projection.geometryUsage, std::forward<decltype(consume)>(consume));
        };
        return walkContributions(stream,
            request.projection,
            render,
            shader,
            geometry,
            std::forward<Visitor>(visitor),
            true);
    }

    template <class Visitor>
    static Result walkStateBound(const std::shared_ptr<const Stream>& stream,
        const std::shared_ptr<const OriginalCommandResourceStates>& graph,
        const StateBoundRequest& request,
        Visitor&& visitor) noexcept
    {
        const auto fail = [](const char* id) noexcept {
            return Result{id, {}};
        };
        if (const auto* id = OriginalTypedStateBoundUsageContract::validateIntent(request))
            return fail(id);
        if (const auto* id = OriginalCommandResourceStates::validateOriginal(graph))
            return fail(id);
        if (graph->stateContractVersion()
            != OriginalTypedUsageProjectionContract::programVersion(request.projection))
            return fail("RHI-ORIGINAL-STATE-BOUND-USAGE-STATE-VERSION");
        if ((request.contractVersion == 3u || request.projection.maxReadbacks)
            && (graph->budget_.retainedReadbackStateVersion != 1u
                || !graph->budget_.retainedReadbackStateOptIn))
            return fail("RHI-ORIGINAL-STATE-BOUND-USAGE-READBACK-PROFILE");
        if (const auto* id = Stream::validateOriginal(stream))
            return fail(id);
        if (const auto* id = stream->compareOriginalStates(graph))
            return fail(id);
        const auto render = [&](auto&& consume) noexcept {
            return graph->visitRenderDescriptorAccesses(
                request.projection.renderBindings, std::forward<decltype(consume)>(consume));
        };
        const auto shader = [&](auto&& consume) noexcept {
            return graph->visitDescriptorAccesses(
                request.projection.shaderConsumption, std::forward<decltype(consume)>(consume));
        };
        const auto geometry = [&](auto&& consume) noexcept {
            return graph->visitGeometryAccesses(
                request.projection.geometryUsage, std::forward<decltype(consume)>(consume));
        };
        return walkContributions(
            stream, request.projection, render, shader, geometry, std::forward<Visitor>(visitor));
    }

    template <class Visitor>
    static Result walk(const std::shared_ptr<const Stream>& stream,
        const GpuDevice& device,
        const Budget& budget,
        Visitor&& visitor) noexcept
    {
        const auto fail = [](const char* id) noexcept {
            return Result{id, {}};
        };
        if (const auto* id = OriginalTypedUsageProjectionContract::validateBudget(budget))
            return fail(id);
        if (const auto* id =
                arp::rhi::semantics::OriginalTypedUsageProjectionLowering::projectionLowering(
                    device.backend(), budget.contractVersion)
                    .diagnostic)
            return fail(id);
        if (const auto* id = Stream::validateOriginal(stream))
            return fail(id);
        const auto& inputs = stream->originalInputs();
        const auto render = [&](auto&& consume) noexcept {
            return C::visitRenderBindings(
                inputs, device, budget.renderBindings, std::forward<decltype(consume)>(consume));
        };
        const auto shader = [&](auto&& consume) noexcept {
            return C::visit(
                inputs, device, budget.shaderConsumption, std::forward<decltype(consume)>(consume));
        };
        const auto geometry = [&](auto&& consume) noexcept {
            return inputs->visitGeometryAccesses(
                budget.geometryUsage, std::forward<decltype(consume)>(consume));
        };
        return walkContributions(
            stream, budget, render, shader, geometry, std::forward<Visitor>(visitor));
    }

    template <class Render, class Shader, class Geometry, class Visitor>
    static Result walkContributions(const std::shared_ptr<const Stream>& stream,
        const Budget& budget,
        Render&& renderBindings,
        Shader&& shaderConsumptions,
        Geometry&& geometryBindings,
        Visitor&& visitor,
        bool captured = false) noexcept
    {
        Result result;
        const auto fail = [](const char* id) noexcept {
            return Result{id, {}};
        };
        if (stream->streamContractVersion()
            != OriginalTypedUsageProjectionContract::programVersion(budget))
            return fail("RHI-ORIGINAL-USAGE-PROJECTION-STREAM-VERSION");
        if (budget.contractVersion != 1u) {
            const auto& original = stream->originalBudget();
            if (!OriginalCommandDescriptorConsumptionContract::matches(
                    budget.shaderConsumption, original.descriptorConsumption))
                return fail("RHI-ORIGINAL-USAGE-PROJECTION-FOOTPRINT-MISMATCH");
            if (!budget.renderBindings.matches(original.renderDescriptorUsage))
                return fail("RHI-ORIGINAL-USAGE-PROJECTION-RENDER-FOOTPRINT-MISMATCH");
            if (!budget.geometryUsage.matches(original.geometryUsage))
                return fail("RHI-ORIGINAL-USAGE-PROJECTION-GEOMETRY-FOOTPRINT-MISMATCH");
        }
        const auto& inputs = stream->originalInputs();
        const auto& sources =
            inputs->originalScalars()->originalUsage()->originalSources()->sources();
        const ScopeContract::Budget scopes{
            OriginalTypedUsageProjectionContract::scopeVersion(budget),
            true,
            budget.maxCommands,
            budget.maxContributions,
            0u};
        if (const auto* id = ScopeContract::validateOperations(stream->operations(), scopes))
            return fail(id);
        const auto emit = [&](const Point& point) noexcept -> const char* {
            if (result.counts.contributions == budget.maxContributions)
                return "RHI-ORIGINAL-USAGE-PROJECTION-CONTRIBUTION-CAPACITY";
            ++result.counts.contributions;
            return visitor(point);
        };
        const auto chargeCells = [&](const GpuTexture& texture,
                                     const TextureSubresourceRange& range) noexcept -> const char* {
            if (budget.contractVersion == 1u)
                return nullptr;
            const char* id = nullptr;
            const auto geometry = semantics::OriginalTextureStateRange::capture(
                texture.desc(), budget.maxTextureCells, id);
            if (!geometry)
                return id;
            if (const auto* error = geometry->validate(range))
                return error;
            const auto cells = geometry->rangeCellCount(range);
            if (cells > budget.maxTextureCells - result.counts.textureCells)
                return "RHI-ORIGINAL-USAGE-PROJECTION-TEXTURE-CELL-CAPACITY";
            result.counts.textureCells += cells;
            return nullptr;
        };
        const auto buffer = [&](const C::Access& access,
                                ScopeContract::Contribution contribution) noexcept -> const char* {
            const auto& descriptor =
                *std::get<std::shared_ptr<const OriginalCommandDescriptorSetSource>>(
                    sources[access.descriptorSourceIndex]);
            const auto* binding =
                findDescriptorSetBinding(descriptor.frozenDefinition(), access.binding);
            if (!binding)
                return "RHI-ORIGINAL-USAGE-PROJECTION-BINDING";
            const auto& lease = std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
                sources[access.bufferSourceIndex]);
            BufferPoint point;
            point.original = access;
            auto& use = point.use;
            use.resource = lease->resource();
            use.bufferBytes = lease->layout().sizeBytes;
            use.offsetBytes = access.offsetBytes;
            use.sizeBytes = access.sizeBytes;
            use.commandIndex = access.commandIndex;
            use.set = access.set;
            use.binding = access.binding;
            use.contribution = contribution;
            if (binding->type == DescriptorType::ReadOnlyStorageBuffer)
                use.usage = ScopeContract::BufferUsage::StorageRead;
            else if (binding->type == DescriptorType::StorageBuffer)
                use.usage = ScopeContract::BufferUsage::Storage;
            else if (OriginalTypedUsageProjectionContract::isGeneral(budget)
                && binding->type == DescriptorType::UniformBuffer)
                use.usage = ScopeContract::BufferUsage::UniformRead;
            else
                return "RHI-ORIGINAL-USAGE-PROJECTION-BUFFER-PROFILE";
            use.access = {access.state, access.stages, access.access};
            if (const auto* id = ScopeContract::resolve(
                    stream->operations(), scopes, access.commandIndex, use.scope))
                return id;
            if (OriginalTypedUsageProjectionContract::isGeneral(budget))
                if (const auto* id = ScopeContract::validateBufferContribution(
                        stream->operations(), scopes, use))
                    return id;
            return emit(point);
        };
        const auto accessPoint =
            [&](const C::Access& access,
                ScopeContract::Contribution contribution) noexcept -> const char* {
            // Immutable reference custody consumes a logical budget and Stream
            // slot, but is not a buffer/texture hazard or memory contribution.
            if (access.kind == C::Access::Kind::Sampler)
                return OriginalTypedUsageProjectionContract::isGeneral(budget)
                    ? nullptr
                    : "RHI-ORIGINAL-USAGE-PROJECTION-SAMPLER-PROFILE";
            if (access.kind == C::Access::Kind::Buffer)
                return buffer(access, contribution);
            if (budget.contractVersion == 1u)
                return "RHI-ORIGINAL-USAGE-PROJECTION-TEXTURE-PROFILE";
            if (result.counts.renderBindingTextures + result.counts.shaderTextures
                == budget.maxTextureAccesses)
                return "RHI-ORIGINAL-USAGE-PROJECTION-TEXTURE-ACCESS-CAPACITY";
            const auto& parent = std::get<std::shared_ptr<const OriginalCommandTextureLease>>(
                sources[access.textureParentSourceIndex]);
            TexturePoint point{access, {}};
            auto& use = point.use;
            use.resource = parent->resource();
            use.registrationGeneration = access.textureParentRegistrationGeneration;
            use.range = access.textureRange;
            use.commandIndex = access.commandIndex;
            use.contribution = contribution;
            use.usage = ScopeContract::TextureUsage::SampledRead;
            if (const auto* set =
                    std::get_if<std::shared_ptr<const OriginalCommandDescriptorSetSource>>(
                        &sources[access.descriptorSourceIndex])) {
                if (!OriginalTypedUsageProjectionContract::isGeneral(budget))
                    return "RHI-ORIGINAL-USAGE-PROJECTION-TEXTURE-PROFILE";
                const auto* binding =
                    findDescriptorSetBinding((*set)->frozenDefinition(), access.binding);
                if (!binding)
                    return "RHI-ORIGINAL-USAGE-PROJECTION-BINDING";
                if (binding->type == DescriptorType::StorageTexture)
                    use.usage = binding->storageTextureAccess == StorageTextureAccess::ReadOnly
                        ? ScopeContract::TextureUsage::StorageRead
                        : ScopeContract::TextureUsage::Storage;
                else if (binding->type != DescriptorType::SampledTexture)
                    return "RHI-ORIGINAL-USAGE-PROJECTION-TEXTURE-PROFILE";
            }
            use.access = {access.state, access.stages, access.access};
            if (const auto* id = ScopeContract::resolve(
                    stream->operations(), scopes, access.commandIndex, use.scope))
                return id;
            if (const auto* id =
                    ScopeContract::validateTextureContribution(stream->operations(), scopes, use))
                return id;
            if (const auto* id = chargeCells(*parent->originalObject(), use.range))
                return id;
            if (contribution == ScopeContract::Contribution::RenderBinding)
                ++result.counts.renderBindingTextures;
            else
                ++result.counts.shaderTextures;
            return emit(point);
        };
        std::uint64_t renderLogicalAccesses = 0u;
        if (stream->counts().renderPasses
            || (OriginalTypedUsageProjectionContract::isGeneral(budget)
                && budget.renderBindings.contractVersion)) {
            if (!budget.renderBindings.contractVersion)
                return fail("RHI-ORIGINAL-USAGE-PROJECTION-RENDER-INTENT");
            const auto render =
                renderBindings([&](const C::Access& access) noexcept -> const char* {
                    return accessPoint(access, ScopeContract::Contribution::RenderBinding);
                });
            if (render.diagnostic)
                return fail(render.diagnostic);
            result.counts.renderBindings = render.bindings;
            renderLogicalAccesses = render.logicalAccesses;
            result.counts.renderBindingSamplers = render.samplerReferences;
            result.counts.renderBindingBuffers =
                render.logicalAccesses - render.textureAccesses - render.samplerReferences;
        } else if (budget.renderBindings.contractVersion || budget.maxAttachmentReferences)
            return fail("RHI-ORIGINAL-USAGE-PROJECTION-RENDER-INTENT");
        const auto shader =
            shaderConsumptions([&](const C::Access& access) noexcept -> const char* {
                return accessPoint(access, ScopeContract::Contribution::ShaderConsumption);
            });
        if (shader.diagnostic)
            return fail(shader.diagnostic);
        result.counts.shaderConsumptions = shader.consumptions;
        result.counts.shaderSamplers = shader.samplerReferences;
        result.counts.shaderBuffers =
            shader.logicalAccesses - shader.textureAccesses - shader.samplerReferences;
        if (budget.contractVersion == 4u
            || (budget.contractVersion == 5u && !budget.geometryUsage.empty())) {
            const auto geometry = geometryBindings(
                [&](const OriginalCommandGeometryBindingContract::Access& access) noexcept
                    -> const char* {
                    GeometryPoint point{access, {}};
                    auto& use = point.use;
                    use.resource = access.resource;
                    use.bufferBytes = access.bufferBytes;
                    use.offsetBytes = access.offsetBytes;
                    use.sizeBytes = access.sizeBytes;
                    use.commandIndex = access.commandIndex;
                    use.binding = access.vertexSlot;
                    use.contribution = access.drawConsumption()
                        ? ScopeContract::Contribution::GeometryConsumption
                        : ScopeContract::Contribution::GeometryBinding;
                    use.usage = access.binding.kind() == K::BindIndexBuffer
                        ? ScopeContract::BufferUsage::Index
                        : ScopeContract::BufferUsage::Vertex;
                    use.access = access.scope;
                    if (const auto* id = ScopeContract::resolve(
                            stream->operations(), scopes, access.commandIndex, use.scope))
                        return id;
                    if (use.scope.kind != ScopeContract::Kind::RenderPass
                        || use.scope.first != access.binding.scopeBeginCommandIndex
                        || use.scope.last != access.scopeEndCommandIndex)
                        return "RHI-ORIGINAL-USAGE-PROJECTION-GEOMETRY-SCOPE";
                    if (const auto* id = ScopeContract::validateBufferContribution(
                            stream->operations(), scopes, use))
                        return id;
                    return emit(point);
                });
            if (geometry.diagnostic)
                return fail(geometry.diagnostic);
            result.counts.geometryBindings = geometry.bindings;
            result.counts.geometryDraws = geometry.draws;
            result.counts.geometryDrawAccesses = geometry.drawAccesses;
        }
        if (result.counts.geometryBindings != stream->counts().geometryBinds
            || result.counts.geometryDraws != stream->counts().geometryDraws
            || result.counts.geometryDrawAccesses != stream->counts().geometryDrawAccesses)
            return fail("RHI-ORIGINAL-USAGE-PROJECTION-GEOMETRY-COVERAGE");
        if (shader.logicalAccesses + (budget.contractVersion != 1u ? renderLogicalAccesses : 0u)
                    + result.counts.geometryBindings + result.counts.geometryDrawAccesses
                != stream->counts().implicitAccesses
            || shader.consumptions != stream->counts().consumptions)
            return fail("RHI-ORIGINAL-USAGE-PROJECTION-STREAM-COVERAGE");
        const auto attachmentVisitor = [&](const OriginalCommandTextureRanges::Point& original,
                                           const GpuTexture& texture) noexcept -> const char* {
            if (stream->operations()[original.commandIndex].kind != K::BeginRenderPass)
                return nullptr;
            // Combined depth/stencil usage needs both independently tracked
            // planes; the existing one-view projection cannot prove it.
            if (texture.desc().format == TextureFormat::Depth24Stencil8)
                return "RHI-ORIGINAL-USAGE-PROJECTION-STENCIL-PLANE-UNPROVEN";
            if (result.counts.attachments == budget.maxAttachmentReferences)
                return "RHI-ORIGINAL-USAGE-PROJECTION-ATTACHMENT-CAPACITY";
            AttachmentPoint point{original, texture.id(), {}, {}};
            if (const auto* id = ScopeContract::resolveRenderPass(
                    stream->operations(), scopes, original.commandIndex, point.scope))
                return id;
            if (original.range.aspect == TextureAspect::Color)
                point.access = {ResourceState::ColorAttachment,
                    PipelineStage::ColorAttachmentOutput,
                    ResourceAccess::ColorAttachmentRead | ResourceAccess::ColorAttachmentWrite};
            else if (original.range.aspect == TextureAspect::Depth)
                point.access = {ResourceState::DepthAttachment,
                    PipelineStage::DepthStencil,
                    ResourceAccess::DepthStencilRead | ResourceAccess::DepthStencilWrite};
            else
                return "RHI-ORIGINAL-USAGE-PROJECTION-ATTACHMENT-ASPECT";
            point.registrationGeneration = texture.originalResourceRegistration().generation();
            if (const auto* id = chargeCells(texture, original.range))
                return id;
            ++result.counts.attachments;
            return emit(point);
        };
        const auto* attachmentDiagnostic = captured
            ? OriginalCommandTextureRanges::visitValidatedOriginal(inputs, attachmentVisitor)
            : OriginalCommandTextureRanges::visit(inputs, attachmentVisitor);
        if (attachmentDiagnostic)
            return fail(attachmentDiagnostic);
        if (result.counts.attachments != stream->counts().attachmentReferences)
            return fail("RHI-ORIGINAL-USAGE-PROJECTION-ATTACHMENT-COVERAGE");
        if (budget.contractVersion == 3u
            || ((budget.contractVersion == 4u || budget.contractVersion == 5u)
                && budget.maxReadbacks)) {
            const auto& usage = *inputs->originalScalars()->originalUsage();
            for (const auto& operation : stream->operations()) {
                if (operation.kind != K::TextureReadback)
                    continue;
                if (operation.referenceCount != 2u
                    || !core::containsUnsignedRange<std::size_t>(
                        0u, usage.references().size(), operation.firstReference, 2u))
                    return fail("RHI-ORIGINAL-USAGE-PROJECTION-READBACK-REFERENCE");
                const auto sourceIndex =
                    usage.references()[operation.firstReference + 1u].sourceIndex;
                const auto* source =
                    std::get_if<std::shared_ptr<const OriginalCommandTextureReadbackSource>>(
                        &sources[sourceIndex]);
                const auto* input = std::get_if<TextureReadbackOriginalInputArguments>(
                    inputs->descriptorAt(operation.commandIndex));
                if (!source || (*source)->contractVersion() != 2u || !input
                    || !core::SharedOwnerProjection::sameOwner(
                        input->originalInput(), (*source)->originalInput()))
                    return fail("RHI-ORIGINAL-USAGE-PROJECTION-READBACK-SOURCE");
                const auto& parent =
                    (*source)->originalSource()->originalLease()->originalTextureOwner();
                const auto& destination = (*source)->originalDestinationLease();
                if (!parent)
                    return fail("RHI-ORIGINAL-USAGE-PROJECTION-READBACK-PARENT");
                const auto& inventory = *usage.originalSources();
                const auto parentIndex = inventory.originalLeaseIndex(parent);
                const auto destinationIndex = inventory.originalLeaseIndex(destination);
                if (parentIndex == SIZE_MAX || destinationIndex == SIZE_MAX
                    || parentIndex != usage.references()[operation.firstReference].sourceIndex)
                    return fail("RHI-ORIGINAL-USAGE-PROJECTION-READBACK-CORRESPONDENCE");
                const auto& layout = (*source)->originalObject()->originalLayout();
                const auto mips = layout->mipCopies().size();
                const auto bytes = input->originalInput()->scalarFields().byteCapacity();
                if (result.counts.readbacks == budget.maxReadbacks
                    || mips > budget.maxReadbackMips - result.counts.readbackMips
                    || bytes
                        > budget.maxReadbackDeclaredBytes - result.counts.readbackDeclaredBytes)
                    return fail("RHI-ORIGINAL-USAGE-PROJECTION-READBACK-CAPACITY");
                ScopeContract::Scope scope;
                if (const auto* id = ScopeContract::resolve(
                        stream->operations(), scopes, operation.commandIndex, scope))
                    return fail(id);
                ReadbackTexturePoint texture{source->get(),
                    sourceIndex,
                    parentIndex,
                    {parent->resource(),
                        parent->originalObject()->originalResourceRegistration().generation(),
                        input->originalInput()->scalarFields().range(),
                        operation.commandIndex,
                        scope,
                        ScopeContract::Contribution::Readback,
                        ScopeContract::TextureUsage::CopySrc,
                        {ResourceState::CopySrc,
                            PipelineStage::Transfer,
                            ResourceAccess::TransferRead}}};
                // sizeBytes is an absolute result capacity, not a length after
                // destinationOffsetBytes. The immutable layout proves every
                // actual mip footprint is contained in this authored envelope.
                ReadbackBufferPoint buffer{source->get(),
                    sourceIndex,
                    destinationIndex,
                    destination->originalObject()->originalResourceRegistration().generation(),
                    {destination->resource(),
                        destination->layout().sizeBytes,
                        0u,
                        bytes,
                        operation.commandIndex,
                        0u,
                        0u,
                        scope,
                        ScopeContract::Contribution::Readback,
                        ScopeContract::BufferUsage::CopyDst,
                        {ResourceState::CopyDst,
                            PipelineStage::Transfer,
                            ResourceAccess::TransferWrite}}};
                if (const auto* id = ScopeContract::validateTextureContribution(
                        stream->operations(), scopes, texture.use))
                    return fail(id);
                if (const auto* id = ScopeContract::validateBufferContribution(
                        stream->operations(), scopes, buffer.use))
                    return fail(id);
                if (const auto* id = chargeCells(*parent->originalObject(), texture.use.range))
                    return fail(id);
                if (const auto* id = emit(texture))
                    return fail(id);
                if (const auto* id = emit(buffer))
                    return fail(id);
                ++result.counts.readbacks;
                result.counts.readbackMips += mips;
                result.counts.readbackDeclaredBytes += bytes;
            }
            if (!result.counts.readbacks
                || result.counts.readbackMips != stream->counts().textureReadbackMips
                || result.counts.readbackDeclaredBytes != stream->counts().readbackDeclaredBytes)
                return fail("RHI-ORIGINAL-USAGE-PROJECTION-READBACK-COVERAGE");
        } else if (stream->counts().textureReadbackMips || stream->counts().readbackDeclaredBytes)
            return fail("RHI-ORIGINAL-USAGE-PROJECTION-READBACK-COVERAGE");
        return result;
    }
};

// endregion Command typed usage projection

// region Command typed dependency projection

// Complete original CPU dependency projection, no native synchronization claim.
// Same guarded State2/Stream2 and complete usage walker; no source inventory,
// GpuDevice pointer, allocation, state machine, journal or proof issuer.
class OriginalTypedDependencyProjection final {
    using P = OriginalTypedUsageProjection;
    using S = semantics::TypedShaderUsageScopeContract;
    using K = CommandUsageKind;
    using N = core::OrderedIntervalNeighborhood<std::size_t>;

public:
    using Request = OriginalTypedDependencyProjectionContract::Request;
    enum class Clause { Unspecified, InitialBookkeeping, BetweenScopes, FinalBookkeeping };

    struct Endpoint {
        S::Scope scope;
        semantics::ResourceAccessScope access;
        std::uint64_t contributions = 0u;

        bool matches(const Endpoint& other) const noexcept
        {
            return scope.matches(other.scope) && access.state == other.access.state
                && access.stages == other.access.stages && access.access == other.access.access
                && contributions == other.contributions;
        }
    };

    struct Record {
        std::size_t commandIndex = SIZE_MAX, descriptorIndex = SIZE_MAX, referenceIndex = SIZE_MAX,
                    sourceIndex = SIZE_MAX;
        K kind = K::Count;
        GpuResourceId resource;
        std::uint64_t registrationGeneration = 0u, bufferBytes = 0u;
        bool texture = false;
        TextureSubresourceRange range;
        std::optional<TextureResourceLayout> textureLayout;
        ResourceState before = ResourceState::Undefined, after = ResourceState::Undefined;
        std::optional<ResourceDependencyArguments> original;
        Clause clause = Clause::Unspecified;
        std::optional<Endpoint> preceding, following;
        std::uint32_t projectionVersion = 1u;

        bool matches(const Record& other) const noexcept
        {
            return std::tie(commandIndex,
                       descriptorIndex,
                       referenceIndex,
                       sourceIndex,
                       kind,
                       resource.value,
                       registrationGeneration,
                       bufferBytes,
                       texture,
                       before,
                       after,
                       clause)
                == std::tie(other.commandIndex,
                    other.descriptorIndex,
                    other.referenceIndex,
                    other.sourceIndex,
                    other.kind,
                    other.resource.value,
                    other.registrationGeneration,
                    other.bufferBytes,
                    other.texture,
                    other.before,
                    other.after,
                    other.clause)
                && projectionVersion == other.projectionVersion && range == other.range && original
                && other.original && original->matches(*other.original)
                && sameTexture(textureLayout, other.textureLayout)
                && sameEndpoint(preceding, other.preceding)
                && sameEndpoint(following, other.following);
        }

    private:
        static bool sameTexture(const std::optional<TextureResourceLayout>& lhs,
            const std::optional<TextureResourceLayout>& rhs) noexcept
        {
            if (lhs.has_value() != rhs.has_value())
                return false;
            if (!lhs)
                return true;
            TextureDesc desc;
            rhs->write(desc);
            return lhs->matches(desc);
        }

        static bool sameEndpoint(
            const std::optional<Endpoint>& lhs, const std::optional<Endpoint>& rhs) noexcept
        {
            return lhs.has_value() == rhs.has_value() && (!lhs || lhs->matches(*rhs));
        }
    };

    struct Counts {
        std::uint64_t dependencies = 0u, initial = 0u, between = 0u, final = 0u,
                      endpointContributions = 0u;
        std::uint64_t projectionPasses = 0u, contributionVisits = 0u, pairChecks = 0u,
                      textureCells = 0u;

        bool matches(const Counts& other) const noexcept
        {
            return std::tie(dependencies,
                       initial,
                       between,
                       final,
                       endpointContributions,
                       projectionPasses,
                       contributionVisits,
                       pairChecks,
                       textureCells)
                == std::tie(other.dependencies,
                    other.initial,
                    other.between,
                    other.final,
                    other.endpointContributions,
                    other.projectionPasses,
                    other.contributionVisits,
                    other.pairChecks,
                    other.textureCells);
        }
    };

    struct Result {
        const char* diagnostic = nullptr;
        Counts counts;

        bool synchronizationProven() const noexcept { return false; }

        bool nativeExecutionProven() const noexcept { return false; }
    };

    static bool isDependency(K kind) noexcept
    {
        return kind == K::BufferBarrier || kind == K::TextureBarrier
            || kind == K::BufferMemoryBarrier || kind == K::TextureMemoryBarrier;
    }

    static Result inspect(const std::shared_ptr<const OriginalCommandStreamProgram>& stream,
        const std::shared_ptr<const OriginalCommandResourceStates>& graph,
        const Request& request) noexcept
    {
        return walk(stream, graph, request, [](const Record&) noexcept -> const char* {
            return nullptr;
        });
    }

    template <class Visitor>
    static Result visit(const std::shared_ptr<const OriginalCommandStreamProgram>& stream,
        const std::shared_ptr<const OriginalCommandResourceStates>& graph,
        const Request& request,
        Visitor&& visitor) noexcept
    {
        static_assert(
            noexcept(visitor(std::declval<const Record&>())), "Dependency visitor must not throw");
        if (const auto* id = OriginalTypedDependencyProjectionContract::validateIntent(request))
            return {id, {}};
        const char* diagnostic = nullptr;
        const auto captured = P::captureStateBound(stream, graph, request.usages, diagnostic);
        if (!captured)
            return {diagnostic, {}};
        const auto preflight = walkCaptured(
            stream, graph, *captured, request, [](const Record&) noexcept -> const char* {
                return nullptr;
            });
        if (preflight.diagnostic)
            return preflight;
        const auto replay =
            walkCaptured(stream, graph, *captured, request, std::forward<Visitor>(visitor));
        if (!replay.diagnostic && !preflight.counts.matches(replay.counts))
            return {"RHI-ORIGINAL-DEPENDENCY-PROJECTION-REPLAY", {}};
        return replay;
    }

private:
    template <class Visitor>
    static Result walk(const std::shared_ptr<const OriginalCommandStreamProgram>& stream,
        const std::shared_ptr<const OriginalCommandResourceStates>& graph,
        const Request& request,
        Visitor&& visitor) noexcept
    {
        const auto fail = [](const char* id) noexcept {
            return Result{id, {}};
        };
        if (const auto* id = OriginalTypedDependencyProjectionContract::validateIntent(request))
            return fail(id);
        const char* joinDiagnostic = nullptr;
        const auto captured = P::captureStateBound(stream, graph, request.usages, joinDiagnostic);
        if (!captured)
            return fail(joinDiagnostic);
        return walkCaptured(stream, graph, *captured, request, std::forward<Visitor>(visitor));
    }

    template <class Visitor>
    static Result walkCaptured(const std::shared_ptr<const OriginalCommandStreamProgram>& stream,
        const std::shared_ptr<const OriginalCommandResourceStates>& graph,
        const P::CapturedJoin& captured,
        const Request& request,
        Visitor&& visitor) noexcept
    {
        const auto fail = [](const char* id) noexcept {
            return Result{id, {}};
        };
        Result result;
        auto& counts = result.counts;
        // A pass is one complete preflight/replay usage visitor call, including
        // its existing independently bounded original command/binding scans.
        const auto project = [&](auto&& consume) noexcept -> const char* {
            if (counts.projectionPasses == request.maxProjectionPasses)
                return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-PASS-CAPACITY";
            ++counts.projectionPasses;
            const auto projected =
                P::visitCaptured(captured, [&](const P::Point& point) noexcept -> const char* {
                    if (counts.contributionVisits == request.maxContributionVisits)
                        return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-VISIT-CAPACITY";
                    ++counts.contributionVisits;
                    return consume(point);
                });
            return projected.diagnostic;
        };
        const S::Budget scopes{
            OriginalTypedUsageProjectionContract::scopeVersion(request.usages.projection),
            true,
            request.usages.projection.maxCommands,
            request.usages.projection.maxContributions,
            request.maxPairChecks};
        // Reuse all buffer compatibility/alias rules; attachments in one scope
        // may not overlap. No temporary list and no skipped shadowed render bind.
        std::uint64_t outer = 0u;
        if (const auto* id = project([&](const P::Point& lhs) noexcept -> const char* {
                if (const auto* buffer = bufferUseOf(lhs)) {
                    if (const auto* error =
                            S::validateBufferContribution(stream->operations(), scopes, *buffer))
                        return error;
                    if (const auto* error = semantics::validateResourceAccessScope(buffer->access))
                        return error;
                } else if (request.contractVersion != 1u) {
                    if (const auto* error = S::validateTextureContribution(
                            stream->operations(), scopes, textureUseOf(lhs)))
                        return error;
                }
                std::uint64_t inner = 0u;
                const auto* error = project([&](const P::Point& rhs) noexcept -> const char* {
                    const auto current = inner++;
                    if (current >= outer)
                        return nullptr;
                    if (counts.pairChecks == request.maxPairChecks)
                        return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-PAIR-CAPACITY";
                    ++counts.pairChecks;
                    return compatible(lhs, rhs);
                });
                ++outer;
                return error;
            }))
            return fail(id);
        for (const auto& operation : stream->operations())
            if (isDependency(operation.kind)) {
                if (counts.dependencies == request.maxDependencies)
                    return fail("RHI-ORIGINAL-DEPENDENCY-PROJECTION-DEPENDENCY-CAPACITY");
                Record record;
                if (const auto* id =
                        extract(stream, graph, captured, request, operation, counts, record))
                    return fail(id);
                if (const auto* id = bindNeighborhood(stream, request, record, counts, project))
                    return fail(id);
                ++counts.dependencies;
                if (record.clause == Clause::InitialBookkeeping)
                    ++counts.initial;
                else if (record.clause == Clause::BetweenScopes)
                    ++counts.between;
                else
                    ++counts.final;
                if (const auto* id = visitor(record))
                    return fail(id);
            }
        if (!counts.dependencies)
            return fail("RHI-ORIGINAL-DEPENDENCY-PROJECTION-EMPTY");
        return result;
    }

    template <class Project>
    static const char* bindNeighborhood(
        const std::shared_ptr<const OriginalCommandStreamProgram>& stream,
        const Request& request,
        Record& record,
        Counts& counts,
        Project&& project) noexcept
    {
        if (!record.texture || request.contractVersion == 1u) {
            if (const auto* id = bindRangeNeighborhood(stream, request, record, counts, project))
                return id;
            return validateCoverage(record, request.maxTextureCells);
        }
        TextureDesc desc;
        record.textureLayout->write(desc);
        const char* id = nullptr;
        const auto geometry =
            semantics::OriginalTextureStateRange::capture(desc, request.maxTextureCells, id);
        if (!geometry)
            return id;
        // Each exact authored cell is observed independently. A single record
        // cannot collapse distinct neighborhoods or omit unused cells.
        const auto cells = geometry->rangeCellCount(record.range);
        for (std::uint64_t ordinal = 0u; ordinal < cells; ++ordinal) {
            auto cell = record;
            std::uint64_t index = 0u;
            if (!geometry->cellIndex(record.range, ordinal, index)
                || !geometry->cellRange(index, cell.range))
                return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-TEXTURE-CELL";
            cell.preceding.reset();
            cell.following.reset();
            if (const auto* error = bindRangeNeighborhood(stream, request, cell, counts, project))
                return error;
            if (!ordinal) {
                record.preceding = cell.preceding;
                record.following = cell.following;
                record.clause = cell.clause;
            } else {
                const auto same = [](const auto& a, const auto& b) noexcept {
                    return a.has_value() == b.has_value() && (!a || a->matches(*b));
                };
                if (!same(record.preceding, cell.preceding)
                    || !same(record.following, cell.following))
                    return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-NONUNIFORM-TEXTURE-ENDPOINTS";
            }
        }
        return validateCoverage(record, request.maxTextureCells);
    }

    template <class Project>
    static const char* bindRangeNeighborhood(
        const std::shared_ptr<const OriginalCommandStreamProgram>& stream,
        const Request& request,
        Record& record,
        Counts& counts,
        Project&& project) noexcept
    {
        N neighborhood(record.commandIndex,
            stream->operations().size(),
            request.usages.projection.maxContributions);
        if (const auto* id = project([&](const P::Point& point) noexcept -> const char* {
                bool selected = false;
                if (const auto* error = select(record, point, selected))
                    return error;
                if (!selected)
                    return nullptr;
                const auto scope = scopeOf(point);
                if (neighborhood.observe({scope.first, scope.last}) != N::Status::Complete)
                    return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-IN-SCOPE-OR-AMBIGUOUS";
                return nullptr;
            }))
            return id;
        const auto& neighbors = neighborhood.result();
        if (!neighbors.inputs)
            return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-RESOURCE-UNUSED";
        if (neighbors.before)
            record.preceding = Endpoint{};
        if (neighbors.after)
            record.following = Endpoint{};
        if (const auto* id = project([&](const P::Point& point) noexcept -> const char* {
                bool selected = false;
                if (const auto* error = select(record, point, selected))
                    return error;
                if (!selected)
                    return nullptr;
                const auto scope = scopeOf(point);
                const N::Interval interval{scope.first, scope.last};
                Endpoint* endpoint = nullptr;
                if (neighbors.before && interval.matches(*neighbors.before))
                    endpoint = &*record.preceding;
                if (neighbors.after && interval.matches(*neighbors.after))
                    endpoint = &*record.following;
                if (!endpoint)
                    return nullptr;
                if (counts.endpointContributions == request.maxEndpointContributions)
                    return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-ENDPOINT-CAPACITY";
                const auto access = accessOf(point);
                if (endpoint->contributions
                    && (!endpoint->scope.matches(scope) || endpoint->access.state != access.state))
                    return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-ENDPOINT-STATE";
                endpoint->scope = scope;
                endpoint->access.state = access.state;
                endpoint->access.stages = endpoint->access.stages | access.stages;
                endpoint->access.access = endpoint->access.access | access.access;
                ++endpoint->contributions;
                ++counts.endpointContributions;
                return nullptr;
            }))
            return id;
        record.clause = record.preceding
            ? record.following ? Clause::BetweenScopes : Clause::FinalBookkeeping
            : Clause::InitialBookkeeping;
        return nullptr;
    }

    static S::Scope scopeOf(const P::Point& point) noexcept
    {
        return std::visit(
            [](const auto& p) {
                using T = std::decay_t<decltype(p)>;
                if constexpr (!std::is_same_v<T, P::AttachmentPoint>)
                    return p.use.scope;
                else
                    return p.scope;
            },
            point);
    }

    static semantics::ResourceAccessScope accessOf(const P::Point& point) noexcept
    {
        return std::visit(
            [](const auto& p) {
                using T = std::decay_t<decltype(p)>;
                if constexpr (!std::is_same_v<T, P::AttachmentPoint>)
                    return p.use.access;
                else
                    return p.access;
            },
            point);
    }

    static const char* compatible(const P::Point& lhs, const P::Point& rhs) noexcept
    {
        if (const auto* a = bufferUseOf(lhs)) {
            const auto* b = bufferUseOf(rhs);
            return b ? S::validateBufferPair(*a, *b) : nullptr;
        }
        if (bufferUseOf(rhs))
            return nullptr;
        return S::validateTexturePair(textureUseOf(lhs), textureUseOf(rhs));
    }

    static const S::BufferUse* bufferUseOf(const P::Point& point) noexcept
    {
        if (const auto* buffer = std::get_if<P::BufferPoint>(&point))
            return &buffer->use;
        if (const auto* buffer = std::get_if<P::ReadbackBufferPoint>(&point))
            return &buffer->use;
        if (const auto* buffer = std::get_if<P::GeometryPoint>(&point))
            return &buffer->use;
        return nullptr;
    }

    static S::TextureUse textureUseOf(const P::Point& point) noexcept
    {
        if (const auto* sampled = std::get_if<P::TexturePoint>(&point))
            return sampled->use;
        if (const auto* readback = std::get_if<P::ReadbackTexturePoint>(&point))
            return readback->use;
        const auto& attachment = std::get<P::AttachmentPoint>(point);
        return {attachment.resource,
            attachment.registrationGeneration,
            attachment.original.range,
            attachment.original.commandIndex,
            attachment.scope,
            S::Contribution::Attachment,
            S::TextureUsage::Attachment,
            attachment.access};
    }

    static const char* select(const Record& record, const P::Point& point, bool& selected) noexcept
    {
        selected = false;
        if (const auto* buffer = bufferUseOf(point)) {
            if (record.texture || buffer->resource != record.resource)
                return nullptr;
            const auto generation = std::holds_alternative<P::BufferPoint>(point)
                ? std::get<P::BufferPoint>(point).original.registrationGeneration
                : std::holds_alternative<P::GeometryPoint>(point)
                ? std::get<P::GeometryPoint>(point).original.binding.registrationGeneration
                : std::get<P::ReadbackBufferPoint>(point).registrationGeneration;
            if (buffer->bufferBytes != record.bufferBytes
                || generation != record.registrationGeneration)
                return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-BUFFER-IDENTITY";
            selected = true;
            return nullptr;
        }
        const auto texture = textureUseOf(point);
        if (!record.texture || texture.resource != record.resource)
            return nullptr;
        if (texture.registrationGeneration != record.registrationGeneration)
            return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-TEXTURE-IDENTITY";
        if (record.projectionVersion == 1u) {
            if (!(texture.range == record.range))
                return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-PARTIAL-TEXTURE-RANGE";
        } else if (texture.range.aspect != record.range.aspect
            || !core::overlapsUnsignedRanges(texture.range.baseMipLevel,
                texture.range.mipLevelCount,
                record.range.baseMipLevel,
                record.range.mipLevelCount)
            || !core::overlapsUnsignedRanges(texture.range.baseArrayLayer,
                texture.range.arrayLayerCount,
                record.range.baseArrayLayer,
                record.range.arrayLayerCount))
            return nullptr;
        selected = true;
        return nullptr;
    }

    static const char* extractImmutableBinding(
        const std::shared_ptr<const OriginalCommandStreamProgram>& stream,
        const std::shared_ptr<const OriginalCommandResourceStates>& graph,
        const Request& request,
        const OriginalCommandStreamProgram::Operation& operation,
        Counts& counts,
        Record& output) noexcept
    {
        output = {};
        output.projectionVersion = request.contractVersion;
        const auto& inputs = stream->originalInputs();
        const auto& usage = *inputs->originalScalars()->originalUsage();
        if (operation.referenceCount != 1u)
            return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-REFERENCE";
        if (operation.commandIndex
                >= inputs->originalScalars()->originalArguments()->records().size()
            || operation.firstReference >= usage.references().size()
            || operation.descriptorIndex >= inputs->descriptors().size())
            return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-POSITION";
        const auto* value =
            std::get_if<ResourceDependencyArguments>(inputs->descriptorAt(operation.commandIndex));
        if (!value || value->kind() != operation.kind)
            return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-DESCRIPTOR";
        output.commandIndex = operation.commandIndex;
        output.descriptorIndex = operation.descriptorIndex;
        output.referenceIndex = operation.firstReference;
        output.sourceIndex = usage.references()[operation.firstReference].sourceIndex;
        output.kind = operation.kind;
        output.resource = value->firstResource();
        output.original = *value;
        if (output.sourceIndex >= usage.originalSources()->sources().size())
            return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-POSITION";
        const auto& source = usage.originalSources()->sources()[output.sourceIndex];
        if (const auto* lease =
                std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(&source)) {
            if (output.kind != K::BufferBarrier && output.kind != K::BufferMemoryBarrier)
                return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-SOURCE-KIND";
            output.bufferBytes = (*lease)->layout().sizeBytes;
            output.registrationGeneration =
                (*lease)->originalObject()->originalResourceRegistration().generation();
            if ((*lease)->resource() != output.resource)
                return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-RESOURCE";
        } else if (const auto* lease =
                       std::get_if<std::shared_ptr<const OriginalCommandTextureLease>>(&source)) {
            if (output.kind != K::TextureBarrier && output.kind != K::TextureMemoryBarrier)
                return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-SOURCE-KIND";
            output.texture = true;
            output.textureLayout = (*lease)->layout();
            const auto* texture = (*lease)->originalObject();
            output.registrationGeneration = texture->originalResourceRegistration().generation();
            if (texture->id() != output.resource)
                return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-RESOURCE";
            if (texture->desc().format == TextureFormat::Depth24Stencil8)
                return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-STENCIL-PLANE-UNPROVEN";
            ResourceBarrierDesc fields;
            if (output.kind == K::TextureBarrier) {
                if (!value->write(output.kind, fields))
                    return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-DESCRIPTOR";
                output.range = fields.textureSubresources;
            } else
                output.range = defaultTextureSubresourceRange(texture->desc());
            const char* id = nullptr;
            const auto geometry = semantics::OriginalTextureStateRange::capture(
                texture->desc(), request.maxTextureCells, id);
            if (!geometry)
                return id;
            if (const auto* error = geometry->validate(output.range))
                return error;
            if (request.contractVersion == 1u && !geometry->isWholeAspect(output.range))
                return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-PARTIAL-TEXTURE-RANGE";
            const auto cells = geometry->rangeCellCount(output.range);
            if (cells > request.maxTextureCells - counts.textureCells)
                return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-TEXTURE-CAPACITY";
            counts.textureCells += cells;
            std::size_t observation = 0u;
            while (observation < graph->textureEntries().size()
                && graph->textureEntries()[observation].point.commandIndex != output.commandIndex)
                ++observation;
            if (observation == graph->textureEntries().size())
                return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-TEXTURE-OBSERVATION";
            const auto& original = graph->textureEntries()[observation].point;
            if (original.sourceIndex != output.sourceIndex
                || original.referenceIndex != output.referenceIndex
                || !(original.range == output.range))
                return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-TEXTURE-IDENTITY";

        } else
            return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-SOURCE-PROFILE";
        return output.registrationGeneration ? nullptr
                                             : "RHI-ORIGINAL-DEPENDENCY-PROJECTION-REGISTRATION";
    }

    static const char* extract(const std::shared_ptr<const OriginalCommandStreamProgram>& stream,
        const std::shared_ptr<const OriginalCommandResourceStates>& graph,
        const P::CapturedJoin& captured,
        const Request& request,
        const OriginalCommandStreamProgram::Operation& operation,
        Counts& counts,
        Record& output) noexcept
    {
        if (const auto* id =
                extractImmutableBinding(stream, graph, request, operation, counts, output))
            return id;
        if (!output.texture) {
            if (const auto* id = P::resolveCapturedBuffer(
                    captured, output.resource, output.commandIndex, output.before))
                return id;
        } else {
            std::size_t observation = 0u;
            while (observation < graph->textureEntries().size()
                && graph->textureEntries()[observation].point.commandIndex != output.commandIndex)
                ++observation;
            const char* id = nullptr;
            TextureDesc desc;
            output.textureLayout->write(desc);
            const auto geometry =
                semantics::OriginalTextureStateRange::capture(desc, request.maxTextureCells, id);
            if (!geometry)
                return id;
            const auto cells = geometry->rangeCellCount(output.range);
            for (std::uint64_t cell = 0u; cell < cells; ++cell) {
                ResourceState current;
                if (const auto* error = P::resolveCapturedTextureCell(
                        captured, observation, cell, output.commandIndex, current))
                    return error;
                if (cell && output.before != current)
                    return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-NONUNIFORM-TEXTURE-STATE";
                output.before = current;
            }
        }
        const auto* value = &*output.original;
        if (output.kind == K::BufferBarrier || output.kind == K::TextureBarrier) {
            ResourceBarrierDesc fields;
            if (!value->write(output.kind, fields))
                return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-DESCRIPTOR";
            if (const auto* id = semantics::RecordedResourceStateSequence::validateBarrier(
                    fields, output.before))
                return id;
            output.after = fields.after;
        } else {
            ResourceMemoryBarrierDesc fields;
            if (!value->write(output.kind, fields))
                return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-DESCRIPTOR";
            if (const auto* id = semantics::RecordedResourceStateSequence::validateMemoryBarrier(
                    fields, output.before))
                return id;
            output.after = output.before;
        }
        return nullptr;
    }

public:
    // Requires an authenticated immutable Stream2/graph owner. This value scan
    // binds the cold-frozen clause without reading current committed usage state.
    static const char* validateFrozenBinding(
        const std::shared_ptr<const OriginalCommandStreamProgram>& stream,
        const std::shared_ptr<const OriginalCommandResourceStates>& graph,
        const Request& request,
        const Record& frozen,
        Counts& counts) noexcept
    {
        if (!stream || !graph
            || !core::SharedOwnerProjection::sameOwner(
                stream->originalInputs(), graph->originalInputs())
            || frozen.commandIndex >= stream->operations().size())
            return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-POSITION";
        if (const auto* id = OriginalTypedDependencyProjectionContract::validateIntent(request))
            return id;
        if (stream->streamContractVersion()
                != OriginalTypedUsageProjectionContract::programVersion(request.usages.projection)
            || graph->stateContractVersion() != stream->streamContractVersion())
            return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-STATE-STREAM-PROFILE";
        Record current;
        if (const auto* id = extractImmutableBinding(
                stream, graph, request, stream->operations()[frozen.commandIndex], counts, current))
            return id;
        current.before = frozen.before;
        current.after = frozen.after;
        const auto project = [&](auto&& consume) noexcept -> const char* {
            if (counts.projectionPasses == request.maxProjectionPasses)
                return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-PASS-CAPACITY";
            ++counts.projectionPasses;
            const auto result = P::visitImmutableBindings(
                stream, graph, request.usages, [&](const P::Point& point) noexcept -> const char* {
                    if (counts.contributionVisits == request.maxContributionVisits)
                        return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-VISIT-CAPACITY";
                    ++counts.contributionVisits;
                    return consume(point);
                });
            return result.diagnostic;
        };
        if (const auto* id = bindNeighborhood(stream, request, current, counts, project))
            return id;
        if (!current.matches(frozen))
            return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-FROZEN-BINDING";
        return nullptr;
    }

    // Value coverage only; original complete identity is established by walk.
    static const char* validateCoverage(
        const Record& record, std::uint64_t maxTextureCells = 0u) noexcept
    {
        if ((record.projectionVersion != 1u && record.projectionVersion != 2u
                && record.projectionVersion != 3u && record.projectionVersion != 4u
                && record.projectionVersion != 5u && record.projectionVersion != 6u)
            || !record.original || !record.resource.isValid() || !record.registrationGeneration
            || !isDependency(record.kind))
            return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-RECORD";
        const bool textureKind =
            record.kind == K::TextureBarrier || record.kind == K::TextureMemoryBarrier;
        if (textureKind != record.texture
            || (record.texture ? record.bufferBytes != 0u || !record.textureLayout
                               : !record.bufferBytes || record.textureLayout.has_value()))
            return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-RESOURCE-PROFILE";
        if (record.texture) {
            TextureDesc desc;
            record.textureLayout->write(desc);
            if (desc.format == TextureFormat::Depth24Stencil8)
                return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-STENCIL-PLANE-UNPROVEN";
            const char* id = nullptr;
            const auto geometry =
                semantics::OriginalTextureStateRange::capture(desc, maxTextureCells, id);
            if (!geometry)
                return id;
            if (const auto* error = geometry->validate(record.range))
                return error;
            if (record.projectionVersion == 1u && !geometry->isWholeAspect(record.range))
                return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-PARTIAL-TEXTURE-RANGE";
        }
        if (!record.preceding && !record.following)
            return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-RESOURCE-UNUSED";
        const auto clause = record.preceding
            ? record.following ? Clause::BetweenScopes : Clause::FinalBookkeeping
            : Clause::InitialBookkeeping;
        if (record.clause != clause)
            return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-CLAUSE";
        PipelineStage src, dst;
        ResourceAccess srcAccess, dstAccess;
        ResourceBarrierDesc transition;
        ResourceMemoryBarrierDesc memory;
        if (record.original->write(record.kind, transition)) {
            if (transition.resource != record.resource || transition.before != record.before
                || transition.after != record.after
                || (record.texture && !(transition.textureSubresources == record.range)))
                return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-STATE-BINDING";
            if (const auto* id = semantics::RecordedResourceStateSequence::validateBarrier(
                    transition, record.before))
                return id;
            src = transition.sourceStages;
            dst = transition.destinationStages;
            srcAccess = transition.sourceAccess;
            dstAccess = transition.destinationAccess;
        } else if (record.original->write(record.kind, memory)) {
            if (memory.resource != record.resource || record.before != record.after)
                return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-STATE-BINDING";
            if (const auto* id = semantics::RecordedResourceStateSequence::validateMemoryBarrier(
                    memory, record.before))
                return id;
            src = memory.sourceStages;
            dst = memory.destinationStages;
            srcAccess = memory.sourceAccess;
            dstAccess = memory.destinationAccess;
        } else
            return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-DESCRIPTOR";
        const auto cover = [&](const Endpoint& endpoint, bool preceding) noexcept -> const char* {
            if (!endpoint.contributions
                || (endpoint.scope.kind != S::Kind::ComputeDispatch
                    && endpoint.scope.kind != S::Kind::RenderPass
                    && !((record.projectionVersion == 3u || record.projectionVersion == 4u
                             || record.projectionVersion == 5u)
                        && endpoint.scope.kind == S::Kind::TransferCopy))
                || endpoint.scope.first > endpoint.scope.last
                || ((endpoint.scope.kind == S::Kind::ComputeDispatch
                        || endpoint.scope.kind == S::Kind::TransferCopy)
                    && endpoint.scope.first != endpoint.scope.last)
                || (endpoint.scope.kind == S::Kind::RenderPass
                    && endpoint.scope.first == endpoint.scope.last)
                || (preceding ? endpoint.scope.last >= record.commandIndex
                              : endpoint.scope.first <= record.commandIndex))
                return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-ENDPOINT-POSITION";
            if (const auto* id = semantics::validateResourceAccessScope(endpoint.access))
                return id;
            const auto state = preceding ? record.before : record.after;
            const auto stages = preceding ? src : dst;
            const auto access = preceding ? srcAccess : dstAccess;
            return endpoint.access.state == state
                    && (endpoint.access.stages & stages) == endpoint.access.stages
                    && (endpoint.access.access & access) == endpoint.access.access
                ? nullptr
                : "RHI-ORIGINAL-DEPENDENCY-PROJECTION-ENDPOINT-COVERAGE";
        };
        if (record.preceding)
            if (const auto* id = cover(*record.preceding, true))
                return id;
        return record.following ? cover(*record.following, false) : nullptr;
    }
};
} // namespace arp::rhi::common

// endregion Command typed dependency projection
