#pragma once
#include "rhi/backends/webgpu/webgpu_original_render_pass_payload.h"
#include "rhi/backends/webgpu/webgpu_original_typed_dependency_decision.h"
#include "rhi/backends/webgpu/webgpu_transfer_copy_validation.h"
#include "rhi/frontend/command_shader_validation.h"

namespace arp::rhi_webgpu {
// Immutable actual native inputs over the same loss-bound Closure2/3/4/6/7/8/9. Preparation
// calls no native API, creates no command object and owns no proof authority.
class WebGpuOriginalTypedNativePayload final : public core::QuotaBoundSharedObjectIdentity {
    friend class WebGpuCanonicalCallbackCustody;
    friend class WebGpuNativeCommandPreparation;
    using C = rhi::OriginalTypedNativePayloadContract;
    using D = WebGpuOriginalTypedDependencyDecision;
    using Closure = WebGpuCanonicalNativeClosure;
    using Stream = rhi::OriginalCommandStreamProgram;
    using K = rhi::CommandUsageKind;
    using Pass = WebGpuOriginalRenderPassPayload;
    using Joins = WebGpuCanonicalReadbackJoins;

    class ConstructionKey {
        friend class WebGpuOriginalTypedNativePayload;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Request = C::Request;
    using Work = C::Work;

    struct Inspection {
        const char* diagnostic = nullptr;
        Work work;
        std::uint64_t colorAttachments = 0u;
        std::uint64_t transferPasses = 0u, readbackCommands = 0u, readbackCopies = 0u;
    };

    struct Record {
        Stream::Operation original;
        rhi::CommandScalarArguments scalar;
        core::StringView text;
        const std::uint32_t* offsets = nullptr;
        std::size_t offsetCount = 0u, passIndex = SIZE_MAX;
        std::uint64_t passDrawCount = 0u;
        const Closure::Entry* binding = nullptr;
        const Closure::Entry* pipeline = nullptr;
        const rhi::OriginalCommandGeometryBinding* geometry = nullptr;
        std::uint64_t bufferBindingBytes = 0u;
        std::uint32_t physicalVertexSlot = UINT32_MAX;
        const D::Record* dependency = nullptr;
        const rhi::RenderPassArguments* pass = nullptr;
        const Closure::Entry* destination = nullptr;
        const Joins::Input* readback = nullptr;
        const Joins::Copy* readbackCopies = nullptr;
        std::size_t readbackCopyCount = 0u;
#if defined(ARP_RHI_WEBGPU_ORIGINAL_RENDER_PASS_PAYLOAD_HAS_NATIVE)
        WGPUComputePipeline compute = nullptr;
        WGPURenderPipeline graphics = nullptr;
        WGPUBindGroup group = nullptr;
        WGPUBuffer buffer = nullptr;
        WGPUTexture texture = nullptr;
#endif
        bool matches(const Record& other) const noexcept
        {
            return original.matches(other.original) && scalar.matches(other.scalar)
                && text == other.text && offsets == other.offsets
                && offsetCount == other.offsetCount && passIndex == other.passIndex
                && passDrawCount == other.passDrawCount && binding == other.binding
                && pipeline == other.pipeline && geometry == other.geometry
                && bufferBindingBytes == other.bufferBindingBytes
                && physicalVertexSlot == other.physicalVertexSlot && dependency == other.dependency
                && pass == other.pass && destination == other.destination
                && readback == other.readback && readbackCopies == other.readbackCopies
                && readbackCopyCount == other.readbackCopyCount
#if defined(ARP_RHI_WEBGPU_ORIGINAL_RENDER_PASS_PAYLOAD_HAS_NATIVE)
                && compute == other.compute && graphics == other.graphics && group == other.group
                && buffer == other.buffer && texture == other.texture
#endif
                ;
        }
    };

    WebGpuOriginalTypedNativePayload(
        ConstructionKey, std::shared_ptr<const D> decision, Request request, Inspection inspected)
        : decision_(std::move(decision)), request_(request), inspected_(inspected),
          records_(static_cast<std::size_t>(inspected.work.commands)),
          passes_(static_cast<std::size_t>(inspected.work.renderPasses))
    {}

    static Inspection inspectOriginal(
        const std::shared_ptr<const D>& decision, const Request& request) noexcept
    {
        Inspection result;
        if ((result.diagnostic = preflight(decision, request)))
            return result;
        result.diagnostic = walk(
            *decision,
            request,
            [](const Record&) noexcept -> const char* {
                return nullptr;
            },
            result);
        if (result.diagnostic) {
            result.work = {};
            result.colorAttachments = 0u;
            result.transferPasses = result.readbackCommands = result.readbackCopies = 0u;
        }
        return result;
    }

    static std::shared_ptr<const WebGpuOriginalTypedNativePayload> prepareOriginal(
        std::shared_ptr<const D> decision, const Request& request, const char*& diagnostic)
    {
        const auto inspected = inspectOriginal(decision, request);
        if ((diagnostic = inspected.diagnostic))
            return {};
        if (inspected.work.commands > SIZE_MAX || inspected.work.renderPasses > SIZE_MAX) {
            diagnostic = "RHI-WEBGPU-TYPED-PAYLOAD-SIZE-CAPACITY";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(decision->originalClosure()->originalMetadataQuota());
            auto result = core::makeUnique<WebGpuOriginalTypedNativePayload>(
                ConstructionKey{}, decision, request, inspected);
            Inspection replay;
            diagnostic = walk(
                *decision,
                request,
                [&](const Record& record) -> const char* {
                    if (!result->records_.append(record))
                        return "RHI-WEBGPU-TYPED-PAYLOAD-RECORD-CAPACITY";
                    if (record.original.kind == K::EndRenderPass
                        && !result->passes_.append(Pass(record.pass, record.passDrawCount)))
                        return "RHI-WEBGPU-TYPED-PAYLOAD-PASS-CAPACITY";
                    return nullptr;
                },
                replay);
            if (diagnostic)
                return {};
            if (!sameCounts(inspected, replay)) {
                diagnostic = "RHI-WEBGPU-TYPED-PAYLOAD-REPLAY";
                return {};
            }
#if defined(ARP_RHI_WEBGPU_ORIGINAL_RENDER_PASS_PAYLOAD_HAS_NATIVE)
            // All values occupy final bounded addresses before native pointers
            // are bound. No native API is called by this shared builder.
            result->passes_.visitMutable([&](Pass& pass) {
                if (!diagnostic)
                    diagnostic = pass.finalize([&](rhi::GpuResourceId id) noexcept {
                        return view(*decision->originalClosure(), id);
                    });
            });
            if (diagnostic)
                return {};
#endif
            auto shared = shareOriginal(std::move(result));
            if ((diagnostic = shared->validate()))
                return {};
            return shared;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-WEBGPU-TYPED-PAYLOAD-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-WEBGPU-TYPED-PAYLOAD-ALLOCATION";
            return {};
        }
    }

    static const char* validateOriginal(
        const std::shared_ptr<const WebGpuOriginalTypedNativePayload>& self) noexcept
    {
        return !self || !self->hasQuotaBoundSharedOwner(self) ? "RHI-WEBGPU-TYPED-PAYLOAD-OWNER"
                                                              : self->validate();
    }

    const char* validate() const noexcept
    {
        if (const auto* id = preflight(decision_, request_))
            return id;
        if (!core::SharedOwnerProjection::sameOwner(
                originalMetadataQuota(), decision_->originalClosure()->originalMetadataQuota()))
            return "RHI-WEBGPU-TYPED-PAYLOAD-QUOTA-DOMAIN";
        return validateImmutableRecords();
    }

    const char* compareOriginalDecision(const std::shared_ptr<const D>& decision) const noexcept
    {
        if (const auto* id = validate())
            return id;
        if (const auto* id = D::validateOriginal(decision))
            return id;
        return core::SharedOwnerProjection::sameOwner(decision_, decision)
            ? nullptr
            : "RHI-WEBGPU-TYPED-PAYLOAD-DECISION-IDENTITY";
    }

    static const char* originalPhysicalGroup(const Record& record, std::uint32_t& output) noexcept
    {
        output = UINT32_MAX;
        const auto* scalar =
            record.scalar.template get<rhi::CommandScalarArguments::DescriptorBinding>();
        if (!scalar || !record.pipeline)
            return "RHI-WEBGPU-TYPED-PAYLOAD-GROUP-MAPPING";
        const WebGpuNativeIssuedPipelineContract* issued = nullptr;
        if (record.original.kind == K::BindComputeDescriptor
            || record.original.kind == K::BindComputeDescriptorTable)
            issued = std::get<std::shared_ptr<WebGpuNativeComputePipelinePayload>>(
                record.pipeline->native)
                         ->issuedContract()
                         .get();
        else if (record.original.kind == K::BindRenderDescriptor
            || record.original.kind == K::BindRenderDescriptorTable)
            issued = std::get<std::shared_ptr<WebGpuNativeRenderPipelinePayload>>(
                record.pipeline->native)
                         ->issuedContract()
                         .get();
        if (!issued)
            return "RHI-WEBGPU-TYPED-PAYLOAD-GROUP-MAPPING";
        for (const auto& mapping : issued->sets())
            if (mapping.logicalSet == scalar->set) {
                if (output != UINT32_MAX) {
                    output = UINT32_MAX;
                    return "RHI-WEBGPU-TYPED-PAYLOAD-GROUP-MAPPING";
                }
                output = mapping.webGpuGroup;
            }
        return output != UINT32_MAX ? nullptr : "RHI-WEBGPU-TYPED-PAYLOAD-GROUP-MAPPING";
    }

    const auto& records() const noexcept { return records_.values(); }

    const auto& passes() const noexcept { return passes_.values(); }

    const Work& work() const noexcept { return inspected_.work; }

    std::uint64_t readbackCommandCount() const noexcept { return inspected_.readbackCommands; }

    std::uint64_t transferPassCount() const noexcept { return inspected_.transferPasses; }

    std::uint64_t readbackCopyCount() const noexcept { return inspected_.readbackCopies; }

    const auto& originalDecision() const noexcept { return decision_; }

    const Request& originalRequest() const noexcept { return request_; }

    std::uint32_t retainedOwnerCount() const noexcept
    {
        return decision_->retainedOwnerCount() + C::additionalOwners;
    }

    bool nativeExecutionProven() const noexcept { return false; }

    bool synchronizationProven() const noexcept { return false; }

    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = validate())
            return id;
        return "RHI-WEBGPU-TYPED-PAYLOAD-LIFECYCLE-EXECUTOR-MISSING";
    }

private:
    // Reuse the original walker and exact record/pass comparisons. This scan
    // reads no committed-state projection and grants no recording authority;
    // its caller must authenticate the retained decision/closure/stream first.
    const char* validateImmutableRecords() const noexcept
    {
        std::size_t index = 0u, passIndex = 0u;
        Inspection replay;
        const auto* diagnostic = walk(
            *decision_,
            request_,
            [&](const Record& record) noexcept -> const char* {
                if (index >= records_.values().size()
                    || !record.matches(records_.values()[index++]))
                    return "RHI-WEBGPU-TYPED-PAYLOAD-ORIGINAL-BINDING";
                if (record.original.kind == K::EndRenderPass) {
                    if (passIndex >= passes_.values().size())
                        return "RHI-WEBGPU-TYPED-PAYLOAD-PASS-COVERAGE";
                    const auto& pass = passes_.values()[passIndex++];
                    if (pass.pass != record.pass)
                        return "RHI-WEBGPU-TYPED-PAYLOAD-PASS-BINDING";
#if defined(ARP_RHI_WEBGPU_ORIGINAL_RENDER_PASS_PAYLOAD_HAS_NATIVE)
                    if (!pass.matches(
                            [&](rhi::GpuResourceId id) noexcept {
                                return view(*decision_->originalClosure(), id);
                            },
                            record.passDrawCount))
                        return "RHI-WEBGPU-TYPED-PAYLOAD-PASS-FIELDS";
#endif
                }
                return nullptr;
            },
            replay);
        if (diagnostic)
            return diagnostic;
        return index == records_.values().size() && passIndex == passes_.values().size()
                && sameCounts(inspected_, replay)
            ? nullptr
            : "RHI-WEBGPU-TYPED-PAYLOAD-COUNTS";
    }

    static bool sameCounts(const Inspection& a, const Inspection& b) noexcept
    {
        return a.work.matches(b.work) && a.colorAttachments == b.colorAttachments
            && a.transferPasses == b.transferPasses && a.readbackCommands == b.readbackCommands
            && a.readbackCopies == b.readbackCopies;
    }

    static const char* preflight(
        const std::shared_ptr<const D>& decision, const Request& request) noexcept
    {
        if (const auto* id = C::validateIntent(request))
            return id;
        if (!decision || !decision->hasQuotaBoundSharedOwner(decision))
            return "RHI-WEBGPU-TYPED-DEPENDENCY-OWNER";
        const auto& closure = decision->originalClosure();
        if (!C::profileMatches(request,
                decision->originalRequest().contractVersion,
                closure->closureContractVersion()))
            return "RHI-WEBGPU-TYPED-PAYLOAD-DEPENDENCY-PROFILE";
        const auto lowering = rhi::semantics::OriginalTypedNativePayloadLowering::lowering(
            rhi::Backend::WebGPU, request.contractVersion);
        if (!lowering.payloadAvailable)
            return lowering.diagnostic;
        const auto& source = closure->originalRoot()
                                 ->originalGraph()
                                 ->originalInputs()
                                 ->originalScalars()
                                 ->originalUsage()
                                 ->originalSources();
        if (request.maxCpuMetadataBytes != closure->originalMetadataQuota()->limitBytes())
            return "RHI-WEBGPU-TYPED-PAYLOAD-QUOTA";
        // Freeze6/7/8 validate their independent native custody ceilings over
        // Closure8/9/10. Append only this payload's owner; old profiles continue to
        // enforce Sources' CPU graph ceiling. Original root admission is unchanged.
        const auto ownerCeiling = (request.contractVersion == 6u || request.contractVersion == 7u
                                      || request.contractVersion == 8u)
            ? static_cast<std::uint64_t>(decision->originalRequest().maxRetainedOwners)
                + C::additionalOwners
            : source->originalBudget().maxGraphOwners;
        if (decision->retainedOwnerCount() > UINT32_MAX - C::additionalOwners
            || decision->retainedOwnerCount() + C::additionalOwners > request.maxRetainedOwners
            || request.maxRetainedOwners > ownerCeiling)
            return "RHI-WEBGPU-TYPED-PAYLOAD-OWNER-CAPACITY";
        if (const auto* id = D::validateOriginal(decision))
            return id;
#if !defined(ARP_RHI_WEBGPU_ORIGINAL_RENDER_PASS_PAYLOAD_HAS_NATIVE)
        return "RHI-WEBGPU-TYPED-PAYLOAD-SDK-CONTRACT-UNAVAILABLE";
#else
        return nullptr;
#endif
    }

    static const Closure::Entry* sourceEntry(const Closure& closure, std::size_t source) noexcept
    {
        return closure.entries_.findIf([&](const auto& entry) noexcept {
            return entry.sourceIndex == source;
        });
    }

    // Borrow the exact original binding and Closure6 actor. The existing Inputs5
    // validator supplies shape, registration, scope and pipeline-epoch checks;
    // Closure6 supplies initialized content/fetch custody. No new owner graph.
    static const char* geometryRecord(const Closure& closure,
        const rhi::OriginalCommandInputs& inputs,
        const Request& request,
        std::size_t activePipeline,
        std::size_t pipelineBind,
        std::size_t scopeBegin,
        Record& record) noexcept
    {
        if (!rhi::OriginalTypedNativeColdContract::geometryProfile(request.contractVersion))
            return "RHI-WEBGPU-TYPED-PAYLOAD-GEOMETRY-PROFILE";
        const auto* descriptor = inputs.descriptorAt(record.original.commandIndex);
        record.geometry =
            descriptor ? std::get_if<rhi::OriginalCommandGeometryBinding>(descriptor) : nullptr;
        if (!record.geometry)
            return "RHI-WEBGPU-TYPED-PAYLOAD-GEOMETRY-BINDING";
        const auto& original = *record.geometry;
        if (const auto* id = rhi::OriginalCommandGeometryBindingContract::validate(original,
                *inputs.originalScalars(),
                record.original.commandIndex,
                activePipeline,
                pipelineBind,
                scopeBegin,
                rhi::semantics::CommandRecordingOrder::Scope::Render))
            return id;
        record.binding = sourceEntry(closure, original.sourceIndex);
        record.pipeline = sourceEntry(closure, original.pipelineSourceIndex);
        if (!record.binding || !record.pipeline || !record.binding->object.buffer
            || !record.pipeline->object.graphics || !record.binding->indexContent
            || record.binding->generation != original.registrationGeneration
            || record.pipeline->generation != original.pipelineRegistrationGeneration)
            return "RHI-WEBGPU-TYPED-PAYLOAD-GEOMETRY-CUSTODY";
        const auto& backing = record.binding->backing();
        const auto offset = record.original.kind == K::BindIndexBuffer
            ? record.scalar.get<rhi::CommandScalarArguments::IndexBinding>()->offset
            : record.scalar.get<rhi::CommandScalarArguments::VertexBinding>()->offset;
        if (!backing.buffer || offset >= backing.bufferDesc.sizeBytes)
            return "RHI-WEBGPU-TYPED-PAYLOAD-GEOMETRY-RANGE";
        record.bufferBindingBytes = backing.bufferDesc.sizeBytes - offset;
        if (record.original.kind == K::BindVertexBuffer) {
            const auto* definition =
                record.pipeline->object.graphics->originalRenderPipelineDefinition();
            if (!definition)
                return "RHI-WEBGPU-TYPED-PAYLOAD-VERTEX-PIPELINE";
            const auto slot = record.scalar.get<rhi::CommandScalarArguments::VertexBinding>()->slot;
            for (std::size_t i = 0u; i < definition->vertexBuffers.size(); ++i) {
                if (definition->vertexBuffers[i].slot == slot) {
                    if (i >= UINT32_MAX)
                        return "RHI-WEBGPU-TYPED-PAYLOAD-VERTEX-SLOT";
                    record.physicalVertexSlot = static_cast<std::uint32_t>(i);
                    break;
                }
            }
            if (record.physicalVertexSlot == UINT32_MAX)
                return "RHI-WEBGPU-TYPED-PAYLOAD-VERTEX-SLOT";
        }

#if defined(ARP_RHI_WEBGPU_ORIGINAL_RENDER_PASS_PAYLOAD_HAS_NATIVE)
        record.buffer = backing.buffer->buffer;
        if (!record.buffer)
            return "RHI-WEBGPU-TYPED-PAYLOAD-GEOMETRY-NATIVE-MISSING";
#endif
        return nullptr;
    }

    // Borrow the original Join2 inputs/decisions. Closure4 owns their storage;
    // no transfer graph, native handle query or copy emission is introduced.
    static const char* readbackRecord(const Closure& closure,
        const Stream::Operation& operation,
        const Request& request,
        Record& record,
        Inspection& result) noexcept
    {
        if (!rhi::OriginalTypedNativeColdContract::readbackProfile(request.contractVersion))
            return "RHI-WEBGPU-TYPED-PAYLOAD-READBACK-PROFILE";
        const auto& joins = closure.originalReadbackJoins();
        if (!joins || joins->joinContractVersion() != (request.contractVersion == 5u ? 3u : 2u)
            || !core::SharedOwnerProjection::sameOwner(
                joins->originalProgram(), closure.originalStream())
            || !core::SharedOwnerProjection::sameOwner(
                joins->originalRoot(), closure.originalRoot()))
            return "RHI-WEBGPU-TYPED-PAYLOAD-READBACK-JOIN";
        if (result.readbackCommands >= request.maxReadbackCommands
            || result.readbackCommands >= joins->entries_.values().size())
            return "RHI-ORIGINAL-TYPED-PAYLOAD-READBACK-CAPACITY";
        const auto ordinal = static_cast<std::size_t>(result.readbackCommands);
        const auto& join = joins->entries_.values()[ordinal];
        const auto& native = joins->native_.values()[ordinal];
        const auto& usage =
            *closure.originalStream()->originalInputs()->originalScalars()->originalUsage();
        const auto& command = usage.commands()[operation.commandIndex];
        if (join.commandIndex != operation.commandIndex || command.referenceCount != 2u
            || operation.family != Stream::Family::TextureReadback
            || operation.firstElement != result.readbackCopies)
            return "RHI-WEBGPU-TYPED-PAYLOAD-READBACK-ORDINAL";
        const auto& original =
            std::get<std::shared_ptr<const rhi::OriginalCommandTextureReadbackSource>>(
                usage.originalSources()
                    ->sources()[usage.references()[command.firstReference + 1u].sourceIndex]);
        if (!core::SharedOwnerProjection::sameOwner(original, join.source))
            return "RHI-WEBGPU-TYPED-PAYLOAD-READBACK-SOURCE";
        const auto& layout = join.source->originalSource()->originalRequest()->originalLayout();
        if (operation.elementCount != layout->mipCopies().size())
            return "RHI-WEBGPU-TYPED-PAYLOAD-READBACK-EXPANSION";
        record.binding = closure.entries_.findIf([&](const auto& entry) noexcept {
            return entry.object.id() == native[0].id;
        });
        record.destination = closure.entries_.findIf([&](const auto& entry) noexcept {
            return entry.object.id() == native[1].id;
        });
        if (!record.binding || !record.destination || record.binding == record.destination
            || !record.binding->backing().sameOwnersAndLayout(native[0])
            || !record.destination->backing().sameOwnersAndLayout(native[1]))
            return "RHI-WEBGPU-TYPED-PAYLOAD-READBACK-BINDING";
        record.readback = &join;
        const auto first = result.readbackCopies;
        for (std::size_t mip = 0u; mip < layout->mipCopies().size(); ++mip) {
            if (result.readbackCopies >= request.maxReadbackCopies
                || result.readbackCopies >= joins->copies_.values().size())
                return "RHI-ORIGINAL-TYPED-PAYLOAD-READBACK-CAPACITY";
            const auto& copy =
                joins->copies_.values()[static_cast<std::size_t>(result.readbackCopies)];
            if (copy.commandIndex != operation.commandIndex)
                return "RHI-WEBGPU-TYPED-PAYLOAD-READBACK-ORDINAL";
            if (const auto* id = rhi::semantics::TextureReadbackCopyLowering::validateOriginal(
                    rhi::Backend::WebGPU,
                    1u,
                    true,
                    layout,
                    mip,
                    layout->mipCopies()[mip],
                    copy.decision))
                return id;
            const auto& value = copy.decision.original;
            rhi::TextureToBufferCopyDesc desc;
            desc.sourceTexture = native[0].id;
            desc.destinationBuffer = native[1].id;
            desc.textureSubresources = value.range;
            desc.extent = value.extent;
            desc.bytesPerRow = value.bytesPerRow;
            desc.rowsPerImage = value.rowsPerImage;
            desc.bufferOffsetBytes = value.bufferOffsetBytes;
            if (const auto* id = WebGpuTransferCopyValidation::inspectTextureBufferLayout(
                    native[0], native[1], desc))
                return id;
            ++result.readbackCopies;
        }
        record.readbackCopies = joins->copies_.values().data() + static_cast<std::size_t>(first);
        record.readbackCopyCount = layout->mipCopies().size();
#if defined(ARP_RHI_WEBGPU_ORIGINAL_RENDER_PASS_PAYLOAD_HAS_NATIVE)
        record.texture = native[0].texture->texture;
        record.buffer = native[1].buffer->buffer;
#endif
        ++result.readbackCommands;
        return nullptr;
    }
#if defined(ARP_RHI_WEBGPU_ORIGINAL_RENDER_PASS_PAYLOAD_HAS_NATIVE)
    static WGPUTextureView view(const Closure& closure, rhi::GpuResourceId id) noexcept
    {
        const auto* entry = closure.entries_.findIf([&](const auto& entry) noexcept {
            return entry.object.view && entry.object.id() == id;
        });
        return entry ? std::get<WebGpuOrdinaryResourceObservation>(entry->native).view()->view
                     : nullptr;
    }

    static const char* validatePass(
        const Closure& closure, const rhi::RenderPassArguments& arguments) noexcept
    {
        const auto& pass = arguments.desc();
        if (pass.layeredRendering || !pass.subpasses.empty() || !pass.subpassDependencies.empty())
            return "RHI-WEBGPU-TYPED-PAYLOAD-SUBPASS-UNPROVEN";
        if (pass.colorAttachments.empty() && !pass.hasDepthStencilAttachment)
            return "RHI-WEBGPU-TYPED-PAYLOAD-EMPTY-PASS";
        const auto check = [&](rhi::GpuResourceId id,
                               rhi::TextureFormat format,
                               bool depth) noexcept -> const char* {
            const auto* entry = closure.entries_.findIf([&](const auto& entry) noexcept {
                return entry.object.view && entry.object.id() == id;
            });
            if (!entry)
                return "RHI-WEBGPU-TYPED-PAYLOAD-PASS-VIEW";
            const auto& observation = std::get<WebGpuOrdinaryResourceObservation>(entry->native);
            if (!Pass::validViewBinding(observation, format, depth))
                return "RHI-WEBGPU-TYPED-PAYLOAD-PASS-FORMAT";
            return nullptr;
        };
        for (const auto& color : pass.colorAttachments) {
            if (const auto* id = check(color.target, color.format, false))
                return id;
            if (color.hasResolveTarget)
                if (const auto* id = check(color.resolveTarget, color.format, false))
                    return id;
        }
        return pass.hasDepthStencilAttachment
            ? check(pass.depthStencilAttachment.target, pass.depthStencilAttachment.format, true)
            : nullptr;
    }
#endif
    template <class Visitor>
    static const char* walk(
        const D& decision, const Request& request, Visitor&& visitor, Inspection& result)
    {
        result = {};
        const auto& closure = *decision.originalClosure();
        const auto& operations = closure.originalStream()->operations();
        const auto& inputs = *closure.originalStream()->originalInputs();
        const auto& arguments = *inputs.originalScalars()->originalArguments();
        std::size_t passIndex = SIZE_MAX, dependencyIndex = 0u;
        std::size_t activePipeline = SIZE_MAX, pipelineBind = SIZE_MAX, scopeBegin = SIZE_MAX;
        std::uint64_t draws = 0u;
        const rhi::RenderPassArguments* pass = nullptr;
        for (const auto& operation : operations) {
            Record record{operation,
                arguments.records()[operation.commandIndex].scalar,
                arguments.text(operation.commandIndex),
                arguments.offsets(operation.commandIndex),
                arguments.records()[operation.commandIndex].offsetCount,
                passIndex};
            auto& work = result.work;
            ++work.commands;
            work.implicitAccesses += operation.implicitAccessCount;
            const auto* descriptor = operation.descriptorIndex != Stream::noDescriptor
                ? &inputs.descriptors()[operation.descriptorIndex].arguments
                : nullptr;
            switch (operation.kind) {
            case K::BeginComputePass:
                ++work.computePasses;
                break;
            case K::EndComputePass:
                break;
            case K::BeginTransferPass:
                if (!rhi::OriginalTypedNativeColdContract::readbackProfile(request.contractVersion))
                    return "RHI-WEBGPU-TYPED-PAYLOAD-READBACK-PROFILE";
                if (++result.transferPasses > request.maxTransferPasses)
                    return "RHI-ORIGINAL-TYPED-PAYLOAD-READBACK-CAPACITY";
                break;
            case K::EndTransferPass:
                if (!rhi::OriginalTypedNativeColdContract::readbackProfile(request.contractVersion))
                    return "RHI-WEBGPU-TYPED-PAYLOAD-READBACK-PROFILE";
                break;
            case K::BeginRenderPass:
                pass = std::get<std::shared_ptr<const rhi::RenderPassArguments>>(*descriptor).get();
                draws = 0u;
                scopeBegin = operation.commandIndex;
                activePipeline = pipelineBind = SIZE_MAX;
                record.passIndex = passIndex = static_cast<std::size_t>(work.renderPasses++);
                record.pass = pass;
                work.attachmentReferences += operation.referenceCount;
                result.colorAttachments += pass->desc().colorAttachments.size();
#if defined(ARP_RHI_WEBGPU_ORIGINAL_RENDER_PASS_PAYLOAD_HAS_NATIVE)
                if (const auto* id = validatePass(closure, *pass))
                    return id;
#endif
                break;
            case K::EndRenderPass:
                record.pass = pass;
                record.passDrawCount = draws;
                pass = nullptr;
                passIndex = SIZE_MAX;
                activePipeline = pipelineBind = scopeBegin = SIZE_MAX;
                break;
            case K::BindComputePipeline:
            case K::BindGraphicsPipeline: {
                ++work.pipelineBinds;
                const auto& original = std::get<rhi::OriginalCommandPipelineBinding>(*descriptor);
                if (operation.kind == K::BindGraphicsPipeline) {
                    activePipeline = original.sourceIndex;
                    pipelineBind = operation.commandIndex;
                }
                record.binding = record.pipeline = sourceEntry(closure, original.sourceIndex);
                if (!record.binding
                    || record.binding->generation != original.registrationGeneration)
                    return "RHI-WEBGPU-TYPED-PAYLOAD-PIPELINE-BINDING";
#if defined(ARP_RHI_WEBGPU_ORIGINAL_RENDER_PASS_PAYLOAD_HAS_NATIVE)
                if (operation.kind == K::BindComputePipeline)
                    record.compute = std::get<std::shared_ptr<WebGpuNativeComputePipelinePayload>>(
                        record.binding->native)
                                         ->pipeline;
                else
                    record.graphics = std::get<std::shared_ptr<WebGpuNativeRenderPipelinePayload>>(
                        record.binding->native)
                                          ->pipeline;
                if (!record.compute && !record.graphics)
                    return "RHI-WEBGPU-TYPED-PAYLOAD-PIPELINE-MISSING";
#endif
                break;
            }
            case K::BindComputeDescriptor:
            case K::BindRenderDescriptor:
            case K::BindComputeDescriptorTable:
            case K::BindRenderDescriptorTable: {
                const bool table = operation.kind == K::BindComputeDescriptorTable
                    || operation.kind == K::BindRenderDescriptorTable;
                if (table && request.contractVersion != 2u && request.contractVersion != 3u
                    && request.contractVersion != 5u)
                    return "RHI-WEBGPU-TYPED-PAYLOAD-TABLE-PROFILE";
                ++work.descriptorBinds;
                work.dynamicOffsets += record.offsetCount;
                const auto& original = std::get<rhi::OriginalCommandDescriptorBinding>(*descriptor);
                record.binding = sourceEntry(closure, original.sourceIndex);
                record.pipeline = sourceEntry(closure, original.pipelineSourceIndex);
                if (!record.binding || !record.pipeline
                    || record.binding->generation != original.registrationGeneration)
                    return "RHI-WEBGPU-TYPED-PAYLOAD-DESCRIPTOR-BINDING";
#if defined(ARP_RHI_WEBGPU_ORIGINAL_RENDER_PASS_PAYLOAD_HAS_NATIVE)
                record.group = std::get<std::shared_ptr<WebGpuNativeDescriptorGroupPayload>>(
                    record.binding->native)
                                   ->group;
                if (!record.group)
                    return "RHI-WEBGPU-TYPED-PAYLOAD-DESCRIPTOR-MISSING";
#endif
                std::uint32_t physicalGroup = UINT32_MAX;
                if (const auto* id = originalPhysicalGroup(record, physicalGroup))
                    return id;
                break;
            }
            case K::BindVertexBuffer:
            case K::BindIndexBuffer:
                if (const auto* id = geometryRecord(
                        closure, inputs, request, activePipeline, pipelineBind, scopeBegin, record))
                    return id;
                if (operation.kind == K::BindVertexBuffer)
                    ++work.vertexBinds;
                else
                    ++work.indexBinds;
                break;
            case K::DrawIndexed:
                if (!rhi::OriginalTypedNativeColdContract::geometryProfile(request.contractVersion))
                    return "RHI-WEBGPU-TYPED-PAYLOAD-GEOMETRY-PROFILE";
                record.pipeline = sourceEntry(closure, activePipeline);
                if (!record.pipeline || !record.pipeline->object.graphics || !pass
                    || !record.scalar.get<rhi::CommandScalarArguments::DrawIndexed>())
                    return "RHI-WEBGPU-TYPED-PAYLOAD-GEOMETRY-DRAW";
                ++work.indexedDraws;
                ++draws;
                break;
            case K::Draw:
                ++work.draws;
                ++draws;
                break;
            case K::Dispatch:
                ++work.dispatches;
                break;
            case K::SetViewport:
                ++work.viewports;
                break;
            case K::SetScissor:
                ++work.scissors;
                break;
            case K::BufferBarrier:
            case K::TextureBarrier:
            case K::BufferMemoryBarrier:
            case K::TextureMemoryBarrier:
                ++work.dependencies;
                if (dependencyIndex >= decision.records().size())
                    return "RHI-WEBGPU-TYPED-PAYLOAD-DEPENDENCY-COVERAGE";
                record.dependency = &decision.records()[dependencyIndex++];
                if (record.dependency->original.commandIndex != operation.commandIndex)
                    return "RHI-WEBGPU-TYPED-PAYLOAD-DEPENDENCY-POSITION";
                record.binding = sourceEntry(closure, record.dependency->original.sourceIndex);
                if (!record.binding
                    || record.binding->object.id() != record.dependency->original.resource
                    || record.binding->generation
                        != record.dependency->original.registrationGeneration)
                    return "RHI-WEBGPU-TYPED-PAYLOAD-DEPENDENCY-RESOURCE";
#if defined(ARP_RHI_WEBGPU_ORIGINAL_RENDER_PASS_PAYLOAD_HAS_NATIVE)
                if (record.dependency->original.texture)
                    record.texture = record.binding->backing().texture->texture;
                else
                    record.buffer = record.binding->backing().buffer->buffer;
                if (!record.buffer && !record.texture)
                    return "RHI-WEBGPU-TYPED-PAYLOAD-DEPENDENCY-NATIVE-MISSING";
#endif
                break;
            case K::Finish:
                break;
            case K::TextureReadback:
                if (const auto* id = readbackRecord(closure, operation, request, record, result))
                    return id;
                break;
            default:
                return "RHI-WEBGPU-TYPED-PAYLOAD-OPERATION-UNPROVEN";
            }
            if (result.colorAttachments > request.maxColorAttachments)
                return "RHI-WEBGPU-TYPED-PAYLOAD-COLOR-CAPACITY";
            if (const auto* id = visitor(record))
                return id;
        }
        if (dependencyIndex != decision.records().size())
            return "RHI-WEBGPU-TYPED-PAYLOAD-DEPENDENCY-COVERAGE";
        if (rhi::OriginalTypedNativeColdContract::readbackProfile(request.contractVersion)
            && (result.readbackCommands != closure.originalReadbackJoins()->readbackCount()
                || result.readbackCopies != closure.originalReadbackJoins()->projectedCopyCount()))
            return "RHI-WEBGPU-TYPED-PAYLOAD-READBACK-COVERAGE";
        std::uint64_t commands = 0u;
        if (const auto* id = C::inspectWorkTotals(result.work,
                result.transferPasses,
                result.readbackCommands,
                result.readbackCopies,
                commands,
                result.work.nativeCalls))
            return id;
        if (commands != result.work.commands)
            return "RHI-WEBGPU-TYPED-PAYLOAD-COMMAND-TOTALS";
        if (request.contractVersion == 8u)
            if (const auto* id = C::validateGraphicsProfile(result.work, result.colorAttachments))
                return id;
        return C::validateWork(result.work,
            result.transferPasses,
            result.readbackCommands,
            result.readbackCopies,
            request);
    }

    const std::shared_ptr<const D> decision_;
    const Request request_;
    const Inspection inspected_;
    core::BoundedSequence<Record> records_;
    core::BoundedSequence<Pass> passes_;
};
} // namespace arp::rhi_webgpu
