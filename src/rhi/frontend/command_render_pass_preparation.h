#pragma once

#include "rhi/frontend/command_attachment_validation.h"
#include "rhi/frontend/command_read_only_graphics.h"
#include "rhi/frontend/command_render_pass_storage.h"
#include "rhi/frontend/resource_access_usage.h"
#include "rhi/frontend/resource_state_sequence.h"
#include "rhi/frontend/transfer_validation.h"

#include <cmath>

namespace arp::rhi {

// Read-only observation of the exact original attachment cells. Bounds cover
// every literal occurrence and every replay visit, including repeated snapshots.
// Creation4 supplies the queried native fixed-cell domain; no mutation plan or
// completion authority is inferred from an observation.
struct OriginalRenderPassContentBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint64_t maxAttachmentObservations = 0u, maxParentCells = 0u;
    std::uint64_t maxContentSteps = 0u, maxCommandVisits = 0u, maxProjectionVisits = 0u;
};

class OriginalRenderPassContentContract final {
public:
    struct Counts {
        std::uint64_t observations = 0u, contentSteps = 0u, commandVisits = 0u,
                      projectionVisits = 0u;
    };

    struct Result {
        const char* diagnostic = nullptr;
        Counts counts;
    };

    static const char* validate(const OriginalRenderPassContentBudget& b) noexcept
    {
        if (b.contractVersion != 1u && b.contractVersion != 2u && b.contractVersion != 3u && b.contractVersion != 4u)
            return "RHI-RENDER-PASS-CONTENT-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-RENDER-PASS-CONTENT-OPT-IN";
        return !b.maxAttachmentObservations || !b.maxParentCells || !b.maxContentSteps
                || !b.maxCommandVisits || !b.maxProjectionVisits
            ? "RHI-RENDER-PASS-CONTENT-BUDGET"
            : nullptr;
    }

    static Result inspect(const std::shared_ptr<const OriginalCommandStreamProgram>& program,
        const OriginalRenderPassStorageContract::Budget& storage,
        const OriginalRenderPassContentBudget& budget) noexcept;

    template <class Visitor>
    static Result visit(const std::shared_ptr<const OriginalCommandStreamProgram>& program,
        const OriginalRenderPassStorageContract::Budget& storage,
        const OriginalRenderPassContentBudget& budget,
        Visitor&& visitor) noexcept
    {
        const auto checked = inspect(program, storage, budget);
        if (checked.diagnostic)
            return checked;
        return project(program, storage, budget, std::forward<Visitor>(visitor));
    }

private:
    template <class Visitor>
    static Result project(const std::shared_ptr<const OriginalCommandStreamProgram>& program,
        const OriginalRenderPassStorageContract::Budget& storage,
        const OriginalRenderPassContentBudget& budget,
        Visitor&& visitor) noexcept;
};

// Exact-cell clear/load/store recording, independently opted into by /4. One
// cell per parent may occur repeatedly; other cells, draws and shader writes
// fail closed. Bounds charge every literal timeline/operand replay, not owners.
struct OriginalRenderPassRecordingBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint64_t maxCommandVisits = 0u, maxAttachmentVisits = 0u;
    std::uint32_t opaqueDepthGraphicsVersion = 0u;
    bool opaqueDepthGraphicsOptIn = false;
    std::uint32_t dispatchDomainVersion = 0u;
    bool dispatchDomainOptIn = false;
};

class OriginalRenderPassRecordingContract final {
public:
    struct Result {
        const char* diagnostic = nullptr;
        std::uint64_t commandVisits = 0u, attachmentVisits = 0u;
    };

    static const char* validate(const OriginalRenderPassRecordingBudget& b) noexcept
    {
        if (b.contractVersion != 1u && b.contractVersion != 2u && b.contractVersion != 3u && b.contractVersion != 4u)
            return "RHI-ORDINARY-ATTACHMENT-RECORDING-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-ORDINARY-ATTACHMENT-RECORDING-OPT-IN";
        if (const auto* id = semantics::OpaqueDepthGraphicsContract::validateIntent(
                b.opaqueDepthGraphicsVersion, b.opaqueDepthGraphicsOptIn))
            return id;
        if (b.opaqueDepthGraphicsVersion && b.contractVersion != 3u)
            return "RHI-OPAQUE-DEPTH-GRAPHICS-RECORDING-PROFILE";
        if (const auto* id = semantics::ComputeDispatchRecordingContract::validateIntent(
                b.dispatchDomainVersion, b.dispatchDomainOptIn))
            return id;
        if (b.dispatchDomainVersion && b.contractVersion != 3u)
            return "RHI-DISPATCH-RECORDING-PROFILE";
        return b.maxCommandVisits && b.maxAttachmentVisits
            ? nullptr
            : "RHI-ORDINARY-ATTACHMENT-RECORDING-BUDGET";
    }

    static Result inspect(const std::shared_ptr<const OriginalCommandStreamProgram>&,
        const OriginalRenderPassStorageContract::Budget&,
        const OriginalRenderPassContentBudget&,
        const OriginalRenderPassRecordingBudget&,
        const OriginalComputeDispatchRecordingInput* = nullptr) noexcept;
};

// Independent private Prototype opt-in for native object preparation (/1)
// same-wrapper cold storage (/2), exact state/content observations (/3), and
// independently budgeted exact-cell clear/load/store recording (/4).
// Versions /1-/3 grant no recording authority.
// Object cardinality is not native driver bytes or complete CPU accounting.
struct OriginalRenderPassPreparationRequest {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    OriginalRenderPassStorageContract::Budget storage;
    std::uint64_t maxNativeObjects = 0u;
    // Version2 independently admits CPU wrappers for the literal original passes.
    // Version1 remains native-object-only and requires this ceiling to be zero.
    std::uint64_t maxPassWrappers = 0u;
    std::optional<OriginalRenderPassContentBudget> content;
    std::optional<OriginalRenderPassRecordingBudget> recording;
    std::shared_ptr<const ComputeDispatchValidationDomain> dispatchDomain;
};

class OriginalRenderPassPreparationContract final {
public:
    struct Decision {
        bool validatorOnly = false, privatePreparationAvailable = false;
        bool recordingAvailable = false;
        const char* diagnostic = "RHI-RENDER-PASS-PREPARATION-BACKEND";
    };

    static const char* validate(const OriginalRenderPassPreparationRequest& request) noexcept
    {
        if (request.contractVersion < 1u || request.contractVersion > 7u)
            return "RHI-RENDER-PASS-PREPARATION-VERSION";
        if (!request.prototypeOptIn)
            return "RHI-RENDER-PASS-PREPARATION-OPT-IN";
        if (!request.maxNativeObjects)
            return "RHI-RENDER-PASS-PREPARATION-OBJECT-BUDGET";
        if ((request.contractVersion == 1u && request.maxPassWrappers)
            || (request.contractVersion >= 2u && !request.maxPassWrappers))
            return "RHI-RENDER-PASS-PREPARATION-WRAPPER-BUDGET";
        if ((request.contractVersion >= 3u) != bool(request.content))
            return "RHI-RENDER-PASS-PREPARATION-CONTENT-BUDGET";
        if (request.content)
            if (const auto* id = OriginalRenderPassContentContract::validate(*request.content))
                return id;
        if ((request.contractVersion >= 4u) != bool(request.recording))
            return "RHI-RENDER-PASS-PREPARATION-RECORDING-BUDGET";
        if (request.recording)
            if (const auto* id = OriginalRenderPassRecordingContract::validate(*request.recording))
                return id;
        if (request.content
            && request.content->contractVersion
                != (request.contractVersion >= 5u ? request.contractVersion - 3u : 1u))
            return "RHI-RENDER-PASS-PREPARATION-CONTENT-PROFILE";
        if (request.recording
            && request.recording->contractVersion
                != (request.contractVersion >= 5u ? request.contractVersion - 3u : 1u))
            return "RHI-RENDER-PASS-PREPARATION-RECORDING-PROFILE";
        if (bool(request.dispatchDomain)
            != bool(request.recording && request.recording->dispatchDomainVersion))
            return "RHI-DISPATCH-RECORDING-DOMAIN-INPUT";
        if (request.dispatchDomain && request.contractVersion != 6u)
            return "RHI-DISPATCH-RECORDING-PREPARATION-PROFILE";
        return OriginalRenderPassStorageContract::validate(request.storage);
    }

    static const char* inspectWrapperCapacity(
        const OriginalRenderPassPreparationRequest& request, std::uint64_t literalPasses) noexcept
    {
        if (const auto* id = validate(request))
            return id;
        return request.contractVersion >= 2u && literalPasses > request.maxPassWrappers
            ? "RHI-RENDER-PASS-PREPARATION-WRAPPER-CAPACITY"
            : nullptr;
    }

    static Decision lowering(
        Backend backend, const OriginalRenderPassPreparationRequest& request) noexcept
    {
        if (const auto* id = validate(request))
            return {false, false, false, id};
        if (request.dispatchDomain) {
            const auto admission = semantics::ComputeDispatchRecordingContract::lowering(backend);
            if (!admission.inspectionAvailable || admission.diagnostic)
                return {admission.validatorOnly, false, false, admission.diagnostic};
        }
        switch (backend) {
        case Backend::Null:
            return {true, false, false, "RHI-RENDER-PASS-PREPARATION-NULL-VALIDATOR"};
        case Backend::Vulkan:
            return {false, true, bool(request.recording), nullptr};
        case Backend::Metal:
            return {false, false, false, "RHI-RENDER-PASS-PREPARATION-METAL-PAYLOAD-MISSING"};
        case Backend::WebGPU:
            return {false, false, false, "RHI-RENDER-PASS-PREPARATION-WEBGPU-PAYLOAD-MISSING"};
        case Backend::WebGL:
            return {false, false, false, "RHI-RENDER-PASS-PREPARATION-WEBGL-UNSUPPORTED"};
        }
        return {};
    }

    static const char* inspectDescriptor(const RenderPassDesc& desc) noexcept
    {
        if ((desc.colorAttachments.empty() && !desc.hasDepthStencilAttachment)
            || desc.layeredRendering || !desc.subpasses.empty()
            || !desc.subpassDependencies.empty())
            return "RHI-RENDER-PASS-PREPARATION-STRUCTURE";
        for (const auto& color : desc.colorAttachments) {
            if (!semantics::validAttachmentLoadOp(color.loadOp)
                || !semantics::validAttachmentStoreOp(color.storeOp))
                return "RHI-RENDER-PASS-ATTACHMENT-OP-INVALID";
            if (color.hasResolveTarget || color.resolveTarget.isValid())
                return "RHI-RENDER-PASS-PREPARATION-RESOLVE";
            if (!std::isfinite(color.clearColor.r) || !std::isfinite(color.clearColor.g)
                || !std::isfinite(color.clearColor.b) || !std::isfinite(color.clearColor.a))
                return "RHI-RENDER-PASS-PREPARATION-CLEAR-COLOR";
        }
        if (desc.hasDepthStencilAttachment) {
            const auto& depth = desc.depthStencilAttachment;
            if (const auto* id = semantics::DepthStencilAttachmentInputContract::diagnostic(depth))
                return id;
            if (depth.format != TextureFormat::D32Float || depth.stencilLoadOp != LoadOp::DontCare
                || depth.stencilStoreOp != StoreOp::DontCare)
                return "RHI-RENDER-PASS-PREPARATION-DEPTH-PROFILE";
        }
        return nullptr;
    }

    static const char* inspectView(const TextureDesc& parent, const TextureViewDesc& view) noexcept
    {
        const TextureSubresourceRange range{view.baseMipLevel,
            view.mipLevelCount,
            view.baseArrayLayer,
            view.arrayLayerCount,
            view.aspect};
        if (!parent.extent.width || !parent.extent.height || parent.sampleCount != 1u
            || parent.extent.depth != 1u || parent.cubeCompatible
            || (parent.usage & TextureUsage::RenderAttachment) == TextureUsage::None
            || view.usage != TextureUsage::RenderAttachment
            || view.dimension != TextureViewDimension::Texture2D || view.mipLevelCount != 1u
            || view.arrayLayerCount != 1u || view.format != parent.format
            || !semantics::TextureTransferValidation::textureRangeValid(parent, range))
            return "RHI-RENDER-PASS-PREPARATION-VIEW-PROFILE";
        return nullptr;
    }
};

template <class Visitor>
OriginalRenderPassContentContract::Result OriginalRenderPassContentContract::project(
    const std::shared_ptr<const OriginalCommandStreamProgram>& program,
    const OriginalRenderPassStorageContract::Budget& storage,
    const OriginalRenderPassContentBudget& budget,
    Visitor&& visitor) noexcept
{
    if (const auto* id = validate(budget))
        return {id};
    if (const auto checked = OriginalRenderPassStorageContract::inspect(program, storage);
        checked.diagnostic)
        return {checked.diagnostic};
    using K = CommandUsageKind;
    using Sequence = OriginalTextureContentCellSequence;
    using Role = RenderPassReferenceSequenceContract::Role;
    for (const auto& operation : program->operations())
        if (operation.kind != K::TextureBarrier && operation.kind != K::BeginRenderPass
            && operation.kind != K::EndRenderPass && operation.kind != K::Finish
            && !(budget.contractVersion >= 2u
                && (operation.kind == K::BindGraphicsPipeline
                    || operation.kind == K::BindRenderDescriptor || operation.kind == K::SetViewport
                    || operation.kind == K::SetScissor || operation.kind == K::Draw))
            && !(budget.contractVersion >= 3u
                && (operation.kind == K::BeginComputePass || operation.kind == K::EndComputePass
                    || operation.kind == K::BindComputePipeline
                    || operation.kind == K::BindComputeDescriptor || operation.kind == K::Dispatch
                    || operation.kind == K::BufferBarrier || operation.kind == K::BeginTransferPass
                    || operation.kind == K::EndTransferPass
                    || operation.kind == K::TextureToBufferCopy)))
            return {"RHI-RENDER-PASS-CONTENT-COMMAND-PROFILE"};
    const auto rangeOf = [](const OriginalCommandTextureViewSource& source) noexcept {
        const auto& d = source.originalObject()->desc();
        return TextureSubresourceRange{
            d.baseMipLevel, d.mipLevelCount, d.baseArrayLayer, d.arrayLayerCount, d.aspect};
    };
    Counts counts;
    const auto checked = OriginalRenderPassStorageContract::visitAttachments(program,
        storage,
        [&](const auto& operation,
            const auto& arguments,
            std::size_t operand,
            auto,
            std::size_t,
            const auto& source) noexcept -> const char* {
            if (counts.observations == budget.maxAttachmentObservations)
                return "RHI-RENDER-PASS-CONTENT-OBSERVATION-CAPACITY";
            if (const auto* id =
                    OriginalRenderPassPreparationContract::inspectDescriptor(arguments.desc()))
                return id;
            const auto& object = *source->originalParentLease()->originalObject();
            if (const auto* id = OriginalRenderPassPreparationContract::inspectView(
                    object.desc(), source->originalObject()->desc()))
                return id;
            const auto range = rangeOf(*source);
            const auto content = object.observeOriginalContent(range);
            if (!content || content->controlVersion() != 4u)
                return "RHI-RENDER-PASS-CONTENT-ORIGINAL-CONTROL";
            if (const auto* id = object.validateOriginalContent(*content))
                return id;
            const auto state = object.observeOriginalCommittedState(range);
            if (!state)
                return "RHI-RENDER-PASS-CONTENT-ORIGINAL-STATE";
            if (const auto* id = object.validateOriginalCommittedState(*state))
                return id;
            const char* id = nullptr;
            const auto geometry = semantics::OriginalTextureStateRange::capture(
                object.desc(), budget.maxParentCells, id);
            if (!geometry)
                return id;
            std::uint64_t cell = 0u;
            if (geometry->rangeCellCount(range) != 1u || !geometry->cellIndex(range, 0u, cell))
                return "RHI-RENDER-PASS-CONTENT-EXACT-CELL";
            std::size_t active = SIZE_MAX;
            const OriginalCommandDescriptorSetSource* sampledSet = nullptr;
            const auto sequence = Sequence::inspectProjected(*geometry,
                object.id(),
                content->resourceGeneration(),
                cell,
                content->state() == common::OriginalTextureContentControl::State::Completed,
                program->operations().size(),
                program->operations().size(),
                {budget.contractVersion >= 3u ? 3u : 2u,
                    true,
                    budget.maxParentCells,
                    budget.maxContentSteps - counts.contentSteps,
                    budget.maxCommandVisits - counts.commandVisits},
                [&](std::size_t command, auto&& emit) noexcept -> const char* {
                    const auto kind = program->operations()[command].kind;
                    if (kind == K::BeginRenderPass)
                        active = command;
                    if (kind == K::BeginRenderPass || kind == K::BindGraphicsPipeline
                        || kind == K::BeginComputePass || kind == K::BindComputePipeline
                        || kind == K::EndComputePass || kind == K::BeginTransferPass)
                        sampledSet = nullptr;
                    if (kind == K::BindRenderDescriptor || kind == K::BindComputeDescriptor)
                        sampledSet =
                            OriginalReadOnlyGraphicsPassProjection::descriptor(*program, command);
                    if (sampledSet
                        && (kind == K::BindRenderDescriptor || kind == K::Draw
                            || kind == K::BindComputeDescriptor || kind == K::Dispatch)) {
                        for (const auto& element : sampledSet->frozenElements()) {
                            if (counts.projectionVisits == budget.maxProjectionVisits)
                                return "RHI-RENDER-PASS-CONTENT-PROJECTION-CAPACITY";
                            ++counts.projectionVisits;
                            const auto* view = std::get_if<
                                std::shared_ptr<const OriginalCommandTextureViewSource>>(
                                &element.resourceOwner);
                            if (!view
                                || (*view)->originalParentLease()->originalObject() != &object)
                                continue;
                            if (const auto* id = emit(Sequence::Step{object.id(),
                                    content->resourceGeneration(),
                                    rangeOf(**view),
                                    Sequence::Action::SampledRead,
                                    active}))
                                return id;
                        }
                    }
                    if (kind == K::TextureToBufferCopy) {
                        const auto* copy = std::get_if<TransferCopyArguments>(
                            program->originalInputs()->descriptorAt(command));
                        if (!copy)
                            return "RHI-ORDINARY-MIXED-COPY-ARGUMENTS";
                        if (copy->source() == object.id())
                            return emit(Sequence::Step{object.id(),
                                content->resourceGeneration(),
                                copy->sourceRange(),
                                Sequence::Action::ReadbackRead,
                                SIZE_MAX});
                    }
                    if (kind != K::BeginRenderPass && kind != K::EndRenderPass)
                        return nullptr;
                    if (active == SIZE_MAX)
                        return "RHI-RENDER-PASS-CONTENT-PASS-ORDER";
                    const auto& passOperation = program->operations()[active];
                    const auto& pass = *std::get<std::shared_ptr<const RenderPassArguments>>(
                        *program->originalInputs()->descriptorAt(active));
                    if (const auto* error =
                            OriginalRenderPassPreparationContract::inspectDescriptor(pass.desc()))
                        return error;
                    const auto& usage =
                        *program->originalInputs()->originalScalars()->originalUsage();
                    const auto projected = RenderPassReferenceSequenceContract::visit(
                        pass.desc(),
                        passOperation.referenceCount,
                        [&](std::size_t index,
                            GpuResourceId,
                            TextureFormat,
                            Role role,
                            std::size_t color) noexcept -> const char* {
                            if (counts.projectionVisits == budget.maxProjectionVisits)
                                return "RHI-RENDER-PASS-CONTENT-PROJECTION-CAPACITY";
                            ++counts.projectionVisits;
                            const auto sourceIndex =
                                usage.references()[passOperation.firstReference + index]
                                    .sourceIndex;
                            const auto& candidate =
                                std::get<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                                    usage.originalSources()->sources()[sourceIndex]);
                            if (candidate->originalParentLease()->originalObject() != &object)
                                return nullptr;
                            const auto load = role == Role::Color
                                ? pass.desc().colorAttachments[color].loadOp
                                : pass.desc().depthStencilAttachment.depthLoadOp;
                            const auto store = role == Role::Color
                                ? pass.desc().colorAttachments[color].storeOp
                                : pass.desc().depthStencilAttachment.depthStoreOp;
                            const auto action = semantics::AttachmentContentActionContract::action(
                                kind == K::BeginRenderPass, load, store);
                            if (!action || role == Role::Resolve)
                                return "RHI-RENDER-PASS-CONTENT-ATTACHMENT-PROFILE";
                            return emit(Sequence::Step{object.id(),
                                content->resourceGeneration(),
                                rangeOf(*candidate),
                                *action,
                                active});
                        },
                        "RHI-RENDER-PASS-CONTENT-OPERANDS");
                    if (kind == K::EndRenderPass) {
                        active = SIZE_MAX;
                        sampledSet = nullptr;
                    }
                    return projected;
                });
            if (sequence.diagnostic)
                return sequence.diagnostic;
            ++counts.observations;
            counts.contentSteps += sequence.steps;
            counts.commandVisits += sequence.commandVisits;
            return visitor(operation.commandIndex, operand, *content, *state, sequence.stored);
        });
    return checked.diagnostic ? Result{checked.diagnostic} : Result{nullptr, counts};
}

inline OriginalRenderPassContentContract::Result OriginalRenderPassContentContract::inspect(
    const std::shared_ptr<const OriginalCommandStreamProgram>& program,
    const OriginalRenderPassStorageContract::Budget& storage,
    const OriginalRenderPassContentBudget& budget) noexcept
{
    return project(program,
        storage,
        budget,
        [](auto, auto, const auto&, const auto&, bool) noexcept -> const char* {
            return nullptr;
        });
}

inline OriginalRenderPassRecordingContract::Result OriginalRenderPassRecordingContract::inspect(
    const std::shared_ptr<const OriginalCommandStreamProgram>& program,
    const OriginalRenderPassStorageContract::Budget& storage,
    const OriginalRenderPassContentBudget& content,
    const OriginalRenderPassRecordingBudget& budget,
    const OriginalComputeDispatchRecordingInput* dispatchAdmission) noexcept
{
    if (const auto* id = validate(budget))
        return {id};
    if (bool(budget.dispatchDomainVersion) != bool(dispatchAdmission))
        return {"RHI-DISPATCH-RECORDING-ADMISSION-INPUT"};
    if (const auto checked = OriginalRenderPassContentContract::inspect(program, storage, content);
        checked.diagnostic)
        return {checked.diagnostic};
    using K = CommandUsageKind;
    if (!program->recordingBudget())
        return {"RHI-ORDINARY-ATTACHMENT-RECORDING-COUNTS"};
    const auto& usage = *program->originalInputs()->originalScalars()->originalUsage();
    Result result;
    if (content.contractVersion != budget.contractVersion)
        return {"RHI-ORDINARY-GRAPHICS-CONTENT-PROFILE"};
    if (budget.contractVersion == 4u)
        if (const auto* id = OriginalReadOnlyGraphicsPassProjection::inspectOpaqueDepth(
                *program, budget.maxCommandVisits, result.commandVisits))
            return {id};
    if (budget.contractVersion == 3u)
        if (const auto* id = OriginalReadOnlyGraphicsPassProjection::inspectMixed(*program,
                budget.maxCommandVisits,
                result.commandVisits,
                budget.opaqueDepthGraphicsVersion == 1u && budget.opaqueDepthGraphicsOptIn,
                dispatchAdmission))
            return {id};
    if (budget.contractVersion == 2u)
        if (const auto* id = OriginalReadOnlyGraphicsPassProjection::inspect(
                *program, budget.maxCommandVisits, result.commandVisits))
            return {id};
    const auto attachmentVisit = [&]() noexcept -> const char* {
        if (result.attachmentVisits == budget.maxAttachmentVisits)
            return "RHI-ORDINARY-ATTACHMENT-RECORDING-ATTACHMENT-CAPACITY";
        ++result.attachmentVisits;
        return nullptr;
    };
    const auto rangeOf = [](const auto& source) noexcept {
        const auto& d = source.originalObject()->desc();
        return TextureSubresourceRange{
            d.baseMipLevel, d.mipLevelCount, d.baseArrayLayer, d.arrayLayerCount, d.aspect};
    };
    // No native call, proposal or visitor publication occurs until the entire
    // original timeline is admitted. Each literal observation replays it.
    const auto checked = OriginalRenderPassStorageContract::visitAttachments(program,
        storage,
        [&](const auto&, const auto&, auto, auto role, auto, const auto& source) noexcept
            -> const char* {
            if (const auto* id = attachmentVisit())
                return id;
            const auto& parent = *source->originalParentLease()->originalObject();
            const auto range = rangeOf(*source);
            const auto initial = parent.observeOriginalCommittedState(range);
            if (!initial)
                return "RHI-ORDINARY-ATTACHMENT-RECORDING-STATE";
            auto state = initial->state();
            semantics::RecordedResourceAccessDependencies dependencies;
            bool active = false;
            bool transitioned = false;
            const OriginalCommandDescriptorSetSource* sampledSet = nullptr;
            for (const auto& operation : program->operations()) {
                if (result.commandVisits == budget.maxCommandVisits)
                    return "RHI-ORDINARY-ATTACHMENT-RECORDING-COMMAND-CAPACITY";
                ++result.commandVisits;
                if (operation.kind == K::BindGraphicsPipeline
                    || operation.kind == K::BeginRenderPass || operation.kind == K::EndRenderPass
                    || operation.kind == K::BeginComputePass || operation.kind == K::EndComputePass
                    || operation.kind == K::BindComputePipeline
                    || operation.kind == K::BeginTransferPass)
                    sampledSet = nullptr;
                if (operation.kind == K::BindRenderDescriptor
                    || operation.kind == K::BindComputeDescriptor)
                    sampledSet = OriginalReadOnlyGraphicsPassProjection::descriptor(
                        *program, operation.commandIndex);
                if (sampledSet
                    && (operation.kind == K::BindRenderDescriptor || operation.kind == K::Draw
                        || operation.kind == K::BindComputeDescriptor
                        || operation.kind == K::Dispatch)) {
                    for (const auto& element : sampledSet->frozenElements()) {
                        if (const auto* id = attachmentVisit())
                            return id;
                        const auto* view =
                            std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                                &element.resourceOwner);
                        if (!view)
                            continue;
                        bool found = false;
                        const auto joined =
                            OriginalRenderPassStorageContract::visitAttachmentsInScope(program,
                                [&](const auto&,
                                    const auto&,
                                    auto,
                                    auto,
                                    auto,
                                    const auto& candidate) noexcept -> const char* {
                                    if (const auto* id = attachmentVisit())
                                        return id;
                                    if (candidate->originalParentLease()->originalObject()
                                            == (*view)->originalParentLease()->originalObject()
                                        && rangeOf(*candidate) == rangeOf(**view))
                                        found = true;
                                    return nullptr;
                                });
                        if (joined)
                            return joined;
                        if (!found)
                            return "RHI-ORDINARY-GRAPHICS-SAMPLED-CONTENT-OWNER";
                        if ((*view)->originalParentLease()->originalObject() != &parent)
                            continue;
                        if (rangeOf(**view) != range || state != ResourceState::ShaderRead)
                            return "RHI-ORDINARY-GRAPHICS-SAMPLED-STATE";
                        if (const auto* id = dependencies.consume({ResourceState::ShaderRead,
                                element.declaration.stages,
                                element.declaration.access}))
                            return id;
                    }
                }
                if (operation.kind == K::TextureToBufferCopy) {
                    const auto* copy = std::get_if<TransferCopyArguments>(
                        program->originalInputs()->descriptorAt(operation.commandIndex));
                    if (!copy)
                        return "RHI-ORDINARY-MIXED-COPY-ARGUMENTS";
                    bool found = false;
                    const auto joined =
                        OriginalRenderPassStorageContract::visitAttachmentsInScope(program,
                            [&](const auto&,
                                const auto&,
                                auto,
                                auto,
                                auto,
                                const auto& candidate) noexcept -> const char* {
                                if (const auto* id = attachmentVisit())
                                    return id;
                                if (candidate->originalParentLease()->originalObject()->id()
                                        == copy->source()
                                    && rangeOf(*candidate) == copy->sourceRange())
                                    found = true;
                                return nullptr;
                            });
                    if (joined)
                        return joined;
                    if (!found)
                        return "RHI-ORDINARY-MIXED-COPY-CONTENT-OWNER";
                    if (copy->source() == parent.id()) {
                        if (active || copy->sourceRange() != range
                            || state != ResourceState::CopySrc)
                            return "RHI-ORDINARY-MIXED-COPY-STATE";
                        if (const auto* id = dependencies.consume({ResourceState::CopySrc,
                                PipelineStage::Transfer,
                                ResourceAccess::TransferRead}))
                            return id;
                    }
                }
                if (operation.kind == K::TextureBarrier) {
                    if (active)
                        return "RHI-ORDINARY-ATTACHMENT-RECORDING-BARRIER-IN-PASS";
                    const auto& input =
                        *program->originalInputs()->descriptorAt(operation.commandIndex);
                    const auto* dependency = std::get_if<ResourceDependencyArguments>(&input);
                    ResourceBarrierDesc barrier;
                    if (!dependency || !dependency->write(K::TextureBarrier, barrier))
                        return "RHI-ORDINARY-ATTACHMENT-RECORDING-BARRIER-JOIN";
                    // Every barrier must address a literal attachment's exact
                    // parent/cell. No unrelated state changes inherit authority.
                    bool found = false;
                    const auto joined =
                        OriginalRenderPassStorageContract::visitAttachmentsInScope(program,
                            [&](const auto&,
                                const auto&,
                                auto,
                                auto,
                                auto,
                                const auto& candidate) noexcept -> const char* {
                                if (const auto* id = attachmentVisit())
                                    return id;
                                if (candidate->originalParentLease()->originalObject()->id()
                                        == barrier.resource
                                    && rangeOf(*candidate) == barrier.textureSubresources)
                                    found = true;
                                return nullptr;
                            });
                    if (joined)
                        return joined;
                    if (!found)
                        return "RHI-ORDINARY-ATTACHMENT-RECORDING-BARRIER-TARGET";
                    if (barrier.resource != parent.id())
                        continue;
                    if (barrier.textureSubresources != range)
                        return "RHI-ORDINARY-ATTACHMENT-RECORDING-PARENT-CELL-PROFILE";
                    if (barrier.before != state)
                        return "RHI-ORDINARY-ATTACHMENT-RECORDING-BARRIER-BEFORE";
                    if (const auto* id = semantics::RecordedResourceStateSequence::validateBarrier(
                            barrier, state))
                        return id;
                    const semantics::ResourceAccessScope destination{
                        barrier.after, barrier.destinationStages, barrier.destinationAccess};
                    const auto* dependencyDiagnostic = barrier.before == ResourceState::Undefined
                        ? dependencies.discardTo(destination)
                        : dependencies.depend(
                              {barrier.before, barrier.sourceStages, barrier.sourceAccess},
                              destination);
                    if (dependencyDiagnostic)
                        return dependencyDiagnostic;
                    if (const auto* id = semantics::ResourceAccessUsageValidation::validate(
                            parent.desc(), barrier.sourceAccess | barrier.destinationAccess))
                        return id;
                    state = barrier.after;
                    transitioned = true;
                } else if (operation.kind == K::BeginRenderPass) {
                    active = true;
                    const auto& pass = *std::get<std::shared_ptr<const RenderPassArguments>>(
                        *program->originalInputs()->descriptorAt(operation.commandIndex));
                    const auto id = RenderPassReferenceSequenceContract::visit(
                        pass.desc(),
                        operation.referenceCount,
                        [&](std::size_t operand,
                            GpuResourceId,
                            TextureFormat,
                            auto candidateRole,
                            std::size_t) noexcept -> const char* {
                            if (const auto* error = attachmentVisit())
                                return error;
                            const auto index =
                                usage.references()[operation.firstReference + operand].sourceIndex;
                            const auto& candidate =
                                std::get<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                                    usage.originalSources()->sources()[index]);
                            if (candidate->originalParentLease()->originalObject() != &parent)
                                return nullptr;
                            if (rangeOf(*candidate) != range || candidateRole != role)
                                return "RHI-ORDINARY-ATTACHMENT-RECORDING-PARENT-CELL-PROFILE";
                            const auto expected =
                                candidateRole == RenderPassReferenceSequenceContract::Role::Color
                                ? ResourceState::ColorAttachment
                                : ResourceState::DepthAttachment;
                            if (!transitioned || state != expected)
                                return "RHI-ORDINARY-ATTACHMENT-RECORDING-PASS-STATE";
                            const bool color =
                                candidateRole == RenderPassReferenceSequenceContract::Role::Color;
                            return dependencies.consume({expected,
                                color ? PipelineStage::ColorAttachmentOutput
                                      : PipelineStage::DepthStencil,
                                color ? ResourceAccess::ColorAttachmentRead
                                        | ResourceAccess::ColorAttachmentWrite
                                      : ResourceAccess::DepthStencilRead
                                        | ResourceAccess::DepthStencilWrite});
                        },
                        "RHI-ORDINARY-ATTACHMENT-RECORDING-OPERANDS");
                    if (id)
                        return id;
                } else if (operation.kind == K::EndRenderPass) {
                    active = false;
                }
            }
            return nullptr;
        });
    return checked.diagnostic ? Result{checked.diagnostic} : result;
}

} // namespace arp::rhi
