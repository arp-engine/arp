#pragma once

#include "core/runtime/completion_evidence_batch.h"
#include "core/shared_allocation_projection.h"
#include "rhi/frontend/bindless_root_completion_profile.h"
#include "rhi/frontend/command_model.h"
#include "rhi/frontend/command_state.h"
#include "rhi/frontend/command_stream_program.h"
#include "rhi/frontend/resource_state_sequence.h"
#include "rhi/semantics/command_lowering.h"
#include "rhi/semantics/transfer_dependency_lowering.h"

// region Command native closure validation
// region original_command_native_closure
namespace arp::rhi {
// CanonicalOriginalNativeClosure/1: backend-private immutable resource binding,
// not a recording/submission/completion issuer. Requires the exact Prepared
// canonical plan/root, original device/registration generation and payload owners.
// Original generation seals the factory record; payload generation is its exact
// non-recycled shared owner + native object + native owner-device identity. A
// replaced native object/owner/token/layout fails; address equality alone fails.
// No second state graph/control/journal. Retained existing native allocation
// tokens/bytes are counted once; view parents share their existing allocation.
// Cold publication and all hot validation consume the root Recording quota.
// /1 covers State1 Buffer/Texture/View payloads only. State2 shader graphs
// require a complete separately versioned Pipeline/Descriptor native closure;
// skipping those original sources cannot authorize the old native path.
struct OriginalCommandNativeClosureBudget {
    struct IndexContentCustody {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint32_t maxSources = 0u, maxRetainedOwners = 0u;
        std::uint64_t maxInitializedBytes = 0u;

        constexpr bool empty() const noexcept
        {
            return !contractVersion && !prototypeOptIn && !maxSources && !maxRetainedOwners
                && !maxInitializedBytes;
        }

        constexpr const char* diagnostic() const noexcept
        {
            if (contractVersion != 1u)
                return "RHI-ORIGINAL-INDEX-CUSTODY-VERSION";
            if (!prototypeOptIn)
                return "RHI-ORIGINAL-INDEX-CUSTODY-OPT-IN";
            return maxSources && maxRetainedOwners && maxInitializedBytes
                ? nullptr
                : "RHI-ORIGINAL-INDEX-CUSTODY-BUDGET";
        }
    };

    struct GeometryContentCustody {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint32_t maxSources = 0u, maxRetainedOwners = 0u;
        std::uint64_t maxInitializedBytes = 0u;
        OriginalGeometryUsageBudget usage;
        OriginalIndexedDrawFetchContract::Budget fetch;

        constexpr bool empty() const noexcept
        {
            return !contractVersion && !prototypeOptIn && !maxSources && !maxRetainedOwners
                && !maxInitializedBytes && usage.empty() && !fetch.contractVersion
                && !fetch.prototypeOptIn && !fetch.maxCommands && !fetch.maxDraws
                && !fetch.maxStreams && !fetch.maxBindingScans && !fetch.maxAnalyzedIndices;
        }

        constexpr const char* diagnostic() const noexcept
        {
            if (contractVersion != 1u)
                return "RHI-ORIGINAL-GEOMETRY-CUSTODY-VERSION";
            if (!prototypeOptIn)
                return "RHI-ORIGINAL-GEOMETRY-CUSTODY-OPT-IN";
            if (const auto* id = usage.diagnostic())
                return id;
            if (usage.drawConsumptionEmpty())
                return "RHI-ORIGINAL-GEOMETRY-CUSTODY-DRAW-INTENT";
            if (const auto* id = fetch.diagnostic())
                return id;
            if (fetch.contractVersion != 1u)
                return "RHI-ORIGINAL-GEOMETRY-CUSTODY-FETCH-PROFILE";
            return maxSources && maxRetainedOwners && maxInitializedBytes
                ? nullptr
                : "RHI-ORIGINAL-GEOMETRY-CUSTODY-BUDGET";
        }
    };

    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxResourceBindings = 0u, maxNativePayloadObjects = 0u, maxRetainedOwners = 0u;
    std::uint64_t maxRetainedResourceBytes = 0u, maxCpuMetadataBytes = 0u;
    // /2 and /3 independently bound canonical Source positions. Stream bind and
    // access operation ceilings remain separate; /1 must leave these zero.
    std::uint32_t maxPipelineSources = 0u, maxDescriptorSources = 0u;
    // /4 retains the exact mixed-program readback join, with separate finite
    // declaration and copy-layout ceilings. Earlier profiles must leave zero.
    std::uint32_t maxReadbackSources = 0u, maxReadbackCopies = 0u;
    // /5 joins exact State4/Stream4 initialized immutable index sources.
    // Readback remains the independent /4 profile until the combined join exists.
    IndexContentCustody indexContent;
    // /6 authenticates initialized vertex/index content at binding and draw
    // positions in the same original State4/Stream4 graph. No new owner graph.
    // /7 composes that content with exact ReadbackJoin3 / ContentPlan3.
    GeometryContentCustody geometryContent;
    // /8 joins the explicitly declared scalar general resource profile over
    // exact Inputs3/State3/Stream3. Samplers are custody resources, not memory.
    std::uint32_t generalShaderResourcesVersion = 0u;
    bool generalShaderResourcesOptIn = false;

    struct ShaderBufferContentCustody {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint32_t maxSources = 0u, maxRetainedOwners = 0u;
        std::uint64_t maxInitializedBytes = 0u;

        constexpr bool empty() const noexcept
        {
            return !contractVersion && !prototypeOptIn && !maxSources && !maxRetainedOwners
                && !maxInitializedBytes;
        }

        constexpr const char* diagnostic() const noexcept
        {
            if (contractVersion != 1u && contractVersion != 2u)
                return "RHI-ORIGINAL-SHADER-CONTENT-CUSTODY-VERSION";
            if (!prototypeOptIn)
                return "RHI-ORIGINAL-SHADER-CONTENT-CUSTODY-OPT-IN";
            return maxSources && maxRetainedOwners && maxInitializedBytes
                ? nullptr
                : "RHI-ORIGINAL-SHADER-CONTENT-CUSTODY-BUDGET";
        }
    };

    ShaderBufferContentCustody shaderBufferContent;

    // /8 retains completed original sampled inputs. Initialization does not
    // authorize ShaderRead: the exact Stream/State still owns that transition.
    struct ShaderTextureContentCustody {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint32_t maxSources = 0u, maxRetainedOwners = 0u;
        std::uint64_t maxInitializedBytes = 0u, maxUploads = 0u, maxMipLevels = 0u,
                      maxJoinVisits = 0u;

        constexpr bool empty() const noexcept
        {
            return !contractVersion && !prototypeOptIn && !maxSources && !maxRetainedOwners
                && !maxInitializedBytes && !maxUploads && !maxMipLevels && !maxJoinVisits;
        }

        constexpr const char* diagnostic() const noexcept
        {
            if (contractVersion != 1u)
                return "RHI-ORIGINAL-SHADER-TEXTURE-CUSTODY-VERSION";
            if (!prototypeOptIn)
                return "RHI-ORIGINAL-SHADER-TEXTURE-CUSTODY-OPT-IN";
            return maxSources && maxRetainedOwners && maxInitializedBytes && maxUploads
                    && maxMipLevels && maxJoinVisits
                ? nullptr
                : "RHI-ORIGINAL-SHADER-TEXTURE-CUSTODY-BUDGET";
        }
    };

    ShaderTextureContentCustody shaderTextureContent;
};

class OriginalCommandNativeClosureContract final {
public:
    // AttachmentReadbackResourceClosure/1 retains only the existing resource
    // graph. The independently authenticated Join4 remains owned by Cold5;
    // this intent grants no recording, map or completion authority.
    static constexpr const char* validateAttachmentReadbackProfile(
        const OriginalCommandNativeClosureBudget& budget,
        std::uint32_t outputVersion,
        bool outputOptIn,
        std::uint32_t stateVersion,
        std::uint32_t contentVersion,
        std::uint32_t joinVersion) noexcept
    {
        if (const auto* id = validateIntent(budget))
            return id;
        return budget.contractVersion == 1u && outputVersion == 3u && outputOptIn
                && stateVersion == 1u && contentVersion == 3u && joinVersion == 4u
            ? nullptr
            : "RHI-ORIGINAL-NATIVE-CLOSURE-ATTACHMENT-READBACK-PROFILE";
    }

    // Closure7 composes readback with either the existing ordinary-descriptor
    // Registry7 domain or table-only Registry8. Mixed/empty descriptor domains
    // cannot infer registration authority from the shared content machinery.
    static constexpr std::uint32_t lossJoinVersion(
        std::uint32_t closureVersion, bool tables, bool ordinaryDescriptors) noexcept
    {
        if (tables == ordinaryDescriptors)
            return 0u;
        if (closureVersion == 7u)
            return tables ? 2u : 1u;
        if (closureVersion == 3u || closureVersion == 4u)
            return tables ? 2u : 0u;
        if (closureVersion == 2u || closureVersion == 5u || closureVersion == 6u
            || closureVersion == 8u || closureVersion == 9u || closureVersion == 10u)
            return ordinaryDescriptors ? 1u : 0u;
        return 0u;
    }

    // The accepted pipeline source belongs to the same declared descriptor
    // domain: ordinary /1 or scalar table expansion /2. Shared readback/content
    // custody cannot change that ABI or authorize a different issued pipeline.
    static constexpr std::uint32_t pipelineSourceVersion(
        std::uint32_t closureVersion, bool tables, bool ordinaryDescriptors) noexcept
    {
        return lossJoinVersion(closureVersion, tables, ordinaryDescriptors);
    }

    static constexpr const char* validateIntent(
        const OriginalCommandNativeClosureBudget& b) noexcept
    {
        if (b.contractVersion < 1u || b.contractVersion > 10u)
            return "RHI-ORIGINAL-NATIVE-CLOSURE-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-ORIGINAL-NATIVE-CLOSURE-OPT-IN";
        // /10 is the separate finite Graphics ContentPlan6 custody intent.
        // Sharing scalar content schemas cannot borrow Compute /9 execution.
        const bool finite = b.contractVersion == 9u || b.contractVersion == 10u;
        if (b.contractVersion == 8u || finite) {
            if (b.generalShaderResourcesVersion != (finite ? 2u : 1u))
                return "RHI-ORIGINAL-NATIVE-CLOSURE-GENERAL-VERSION";
            if (!b.generalShaderResourcesOptIn)
                return "RHI-ORIGINAL-NATIVE-CLOSURE-GENERAL-OPT-IN";
        } else if (b.generalShaderResourcesVersion || b.generalShaderResourcesOptIn)
            return "RHI-ORIGINAL-NATIVE-CLOSURE-GENERAL-SCHEMA";
        if (!b.shaderBufferContent.empty() || finite) {
            if (b.contractVersion != 8u && !finite)
                return "RHI-ORIGINAL-NATIVE-CLOSURE-SHADER-CONTENT-SCHEMA";
            if (b.shaderBufferContent.contractVersion != (finite ? 2u : 1u))
                return "RHI-ORIGINAL-SHADER-CONTENT-CUSTODY-VERSION";
            if (const auto* id = b.shaderBufferContent.diagnostic())
                return id;
        }
        if (!b.shaderTextureContent.empty() || finite) {
            if (b.contractVersion != 8u && !finite)
                return "RHI-ORIGINAL-NATIVE-CLOSURE-SHADER-TEXTURE-SCHEMA";
            if (const auto* id = b.shaderTextureContent.diagnostic())
                return id;
        }
        if (b.contractVersion == 5u) {
            if (const auto* id = b.indexContent.diagnostic())
                return id;
        } else if (!b.indexContent.empty())
            return "RHI-ORIGINAL-NATIVE-CLOSURE-INDEX-SCHEMA";
        if (b.contractVersion == 6u || b.contractVersion == 7u) {
            if (const auto* id = b.geometryContent.diagnostic())
                return id;
        } else if (!b.geometryContent.empty())
            return "RHI-ORIGINAL-NATIVE-CLOSURE-GEOMETRY-SCHEMA";
        if (!b.maxResourceBindings || !b.maxNativePayloadObjects || !b.maxRetainedOwners
            || !b.maxRetainedResourceBytes || !b.maxCpuMetadataBytes)
            return "RHI-ORIGINAL-NATIVE-CLOSURE-BUDGET";
        if (b.contractVersion == 1u && (b.maxPipelineSources || b.maxDescriptorSources))
            return "RHI-ORIGINAL-NATIVE-CLOSURE-LEGACY-SCHEMA";
        if (b.contractVersion >= 2u && (!b.maxPipelineSources || !b.maxDescriptorSources))
            return "RHI-ORIGINAL-NATIVE-CLOSURE-TYPED-SOURCE-BUDGET";
        if (b.contractVersion != 4u && b.contractVersion != 7u
            && (b.maxReadbackSources || b.maxReadbackCopies))
            return "RHI-ORIGINAL-NATIVE-CLOSURE-READBACK-SCHEMA";
        if ((b.contractVersion == 4u || b.contractVersion == 7u)
            && (!b.maxReadbackSources || !b.maxReadbackCopies))
            return "RHI-ORIGINAL-NATIVE-CLOSURE-READBACK-BUDGET";
        return nullptr;
    }

    static constexpr const char* validateFiniteGraphicsProfile(
        const OriginalFiniteGraphicsContentPlanContract::Profile& profile,
        std::uint32_t contentVersion,
        std::uint32_t streamVersion,
        bool geometry,
        bool tables) noexcept
    {
        if (contentVersion != 6u || streamVersion != 5u || geometry || tables)
            return "RHI-ORIGINAL-GRAPHICS-CUSTODY-PROFILE";
        return OriginalFiniteGraphicsContentPlanContract::validateRootProfile(profile);
    }
};

// /3 is the loss-bound original table custody profile: exact State3/Stream3,
// Prepared Transaction3/ContentPlan2, Registry8/TypedNativeLossJoin2 and accepted
// Input3 scalar-expansion pipelines. It retains every original table group,
// parent/view/output allocation and independent publication quota. It does not
// authorize recording, submission, content initialization or GPU completion.
// /1 and /2 cannot consume this profile or silently omit its table sources.
// /4 independently joins ContentPlan3 and exact ReadbackJoin2 to State3/Stream3.
// Every composite Source2 is covered by that join and both native endpoints by
// canonical inventory entries. It retains the join, deduplicates native owners
// and allocations, and grants no recording/map/result/completion authority.

// CanonicalLossBoundNativeClosure/1: a selected WebGPU cold closure retains a
// device-issued exact native-loss observation. Original loss binds BEFORE the
// resource registry, so every original native Buffer/Texture/View registration
// is checked against that captured native source. Closure creation verifies the
// observation with the same original facade and registered root. Domain owner
// is already in root's registry; the distinct subscription adds one owner.
// Unbound /1 resource closures remain inspection-only and must be refused by any
// future native lifecycle entry. Retired/Shutdown/Lost diagnostics remain distinct;
// retained subscription custody never proves queue drain or GPU completion.
// Null ValidatorOnly; Vulkan/Metal DiagnosticFailure; selected WebGPU Prototype
// loss-bound resource closure, recording/submission/execution all still closed.
// CanonicalOriginalNativeLifecycle/1 (execution remains closed):
// Prepared closure -> actual encoder/scopes -> Recorded -> reserved exact
// device/queue/command/ticket -> SubmitPending -> actual scopes+queue callback ->
// Completed. CPU commit is never an event. Before submit, rejection drains issued
// scopes then rolls back before releasing root. Once submit may have occurred,
// error/cancel/drop quarantines root/native owners until original queue drain or
// captured device loss; no rollback pretending submitted work never happened.
// Admission needs distinct Source-authored Recording/Submission/Callback count,
// owner and CPU limits, cold callback tickets and native loss subscription.
// NativeSync stays unavailable until actual completion/order/lifetime proof.

// endregion original_command_native_closure
// endregion Command native closure validation

// region Command readback validation
// region original_command_readback_result

// CanonicalOriginalReadbackResult/1 is cold admission only. Repeated writes to
// one exact Request/destination publish one final padded snapshot after the
// complete root finishes. Request IDs/frame/generation labels grant no authority.
class OriginalCommandReadbackResultContract final {
public:
    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t maxReadbackCommands = 0u, maxNativeCopyCommands = 0u,
                      maxResultDeclarations = 0u;
        std::uint64_t maxDeclaredReadbackBytes = 0u, maxUniqueResultBytes = 0u,
                      maxCpuResultBytes = 0u;
    };

    struct Requirements {
        std::uint64_t readbackCommands = 0u, nativeCopyCommands = 0u, resultDeclarations = 0u;
        std::uint64_t declaredReadbackBytes = 0u, uniqueResultBytes = 0u, cpuResultBytes = 0u;
    };

    struct Inspection {
        const char* diagnostic = nullptr;
        Requirements required;
    };

    static constexpr const char* validateIntent(const Budget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-ORIGINAL-READBACK-RESULT-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-ORIGINAL-READBACK-RESULT-OPT-IN";
        if (!b.maxReadbackCommands || !b.maxNativeCopyCommands || !b.maxResultDeclarations
            || !b.maxDeclaredReadbackBytes || !b.maxUniqueResultBytes || !b.maxCpuResultBytes)
            return "RHI-ORIGINAL-READBACK-RESULT-BUDGET";
        return nullptr;
    }

    static constexpr bool supports(CommandUsageKind kind) noexcept
    {
        using K = CommandUsageKind;
        return kind == K::TextureReadback || kind == K::BeginTransferPass
            || kind == K::EndTransferPass || kind == K::BufferBarrier || kind == K::TextureBarrier
            || kind == K::Finish;
    }

    static constexpr const char* validateRequirements(
        const Requirements& r, const Budget& b) noexcept
    {
        if (const auto* id = validateIntent(b))
            return id;
        if (!r.readbackCommands || !r.nativeCopyCommands || !r.resultDeclarations
            || !r.declaredReadbackBytes || !r.uniqueResultBytes
            || r.resultDeclarations > r.readbackCommands
            || r.readbackCommands > r.nativeCopyCommands
            || r.uniqueResultBytes > r.declaredReadbackBytes)
            return "RHI-ORIGINAL-READBACK-RESULT-REQUIREMENTS";
        if (r.readbackCommands > b.maxReadbackCommands
            || r.nativeCopyCommands > b.maxNativeCopyCommands
            || r.resultDeclarations > b.maxResultDeclarations
            || r.declaredReadbackBytes > b.maxDeclaredReadbackBytes
            || r.uniqueResultBytes > b.maxUniqueResultBytes
            || r.cpuResultBytes != b.maxCpuResultBytes)
            return "RHI-ORIGINAL-READBACK-RESULT-REQUIREMENTS-CAPACITY";
        return nullptr;
    }

    static Inspection inspect(const std::shared_ptr<const OriginalCommandInputs>& inputs,
        const OriginalCommandStreamBudget& stream,
        const Budget& b) noexcept
    {
        Inspection result;
        if ((result.diagnostic = validateIntent(b)))
            return result;
        Requirements pending;
        result.diagnostic = OriginalCommandStreamProgram::visitOriginal(inputs,
            stream,
            [&](const OriginalCommandStreamProgram::Operation& operation) noexcept -> const char* {
                if (!supports(operation.kind))
                    return "RHI-ORIGINAL-READBACK-RESULT-COMMAND";
                if (operation.family != OriginalCommandStreamProgram::Family::TextureReadback)
                    return nullptr;
                if (pending.readbackCommands == b.maxReadbackCommands)
                    return "RHI-ORIGINAL-READBACK-RESULT-COMMAND-CAPACITY";
                if (operation.elementCount > b.maxNativeCopyCommands - pending.nativeCopyCommands)
                    return "RHI-ORIGINAL-READBACK-RESULT-COPY-CAPACITY";
                ++pending.readbackCommands;
                pending.nativeCopyCommands += operation.elementCount;
                return nullptr;
            });
        if (result.diagnostic)
            return result;
        const auto program = OriginalCommandStreamProgram::inspect(inputs, stream);
        if (program.diagnostic) {
            result.diagnostic = program.diagnostic;
            return result;
        }
        return inspectDeclarations(inputs, b, pending, program.counts);
    }

    // /2 projects result requirements from an already authenticated immutable
    // program containing shader/attachment work and readback. It never issues
    // recording, content, map or publication authority. /1 consumers stay /1.
    static constexpr const char* validateProgramIntent(const Budget& b) noexcept
    {
        if (b.contractVersion != 2u)
            return "RHI-ORIGINAL-MIXED-READBACK-RESULT-VERSION";
        auto original = b;
        original.contractVersion = 1u;
        return validateIntent(original);
    }

    static constexpr const char* validateProgramRequirements(
        const Requirements& r, const Budget& b) noexcept
    {
        if (const auto* id = validateProgramIntent(b))
            return id;
        auto original = b;
        original.contractVersion = 1u;
        return validateRequirements(r, original);
    }

    static constexpr bool supportsProgram(CommandUsageKind kind) noexcept
    {
        if (supports(kind))
            return true;
        using K = CommandUsageKind;
        switch (kind) {
        case K::BeginRenderPass:
        case K::EndRenderPass:
        case K::BindGraphicsPipeline:
        case K::SetViewport:
        case K::SetScissor:
        case K::BindRenderDescriptor:
        case K::BindRenderDescriptorTable:
        case K::BindVertexBuffer:
        case K::BindIndexBuffer:
        case K::Draw:
        case K::DrawIndexed:
        case K::DrawIndexedIndirect:
        case K::BeginComputePass:
        case K::EndComputePass:
        case K::BindComputePipeline:
        case K::BindComputeDescriptor:
        case K::BindComputeDescriptorTable:
        case K::Dispatch:
        case K::DispatchIndirect:
        case K::PushConstants:
        case K::BufferMemoryBarrier:
        case K::TextureMemoryBarrier:
            return true;
        default:
            return false;
        }
    }

    static Inspection inspectProgram(
        const std::shared_ptr<const OriginalCommandStreamProgram>& program,
        const std::shared_ptr<const OriginalCommandInputs>& inputs,
        const Budget& b) noexcept
    {
        Inspection result;
        if ((result.diagnostic = validateProgramIntent(b)))
            return result;
        if ((result.diagnostic = OriginalCommandStreamProgram::validateOriginal(program)))
            return result;
        if ((result.diagnostic = OriginalCommandInputs::validateOriginal(inputs)))
            return result;
        if ((result.diagnostic = program->compareOriginalInputs(inputs)))
            return result;
        Requirements pending;
        for (const auto& operation : program->operations()) {
            if (!supportsProgram(operation.kind)) {
                result.diagnostic = "RHI-ORIGINAL-MIXED-READBACK-RESULT-COMMAND";
                return result;
            }
            if (operation.family != OriginalCommandStreamProgram::Family::TextureReadback)
                continue;
            if (pending.readbackCommands == b.maxReadbackCommands) {
                result.diagnostic = "RHI-ORIGINAL-READBACK-RESULT-COMMAND-CAPACITY";
                return result;
            }
            if (operation.elementCount > b.maxNativeCopyCommands - pending.nativeCopyCommands) {
                result.diagnostic = "RHI-ORIGINAL-READBACK-RESULT-COPY-CAPACITY";
                return result;
            }
            ++pending.readbackCommands;
            pending.nativeCopyCommands += operation.elementCount;
        }
        return inspectDeclarations(inputs, b, pending, program->counts());
    }

private:
    static Inspection inspectDeclarations(
        const std::shared_ptr<const OriginalCommandInputs>& inputs,
        const Budget& b,
        Requirements pending,
        const OriginalCommandStreamProgram::Counts& counts) noexcept
    {
        Inspection result;
        if (!pending.readbackCommands) {
            result.diagnostic = "RHI-ORIGINAL-READBACK-RESULT-MISSING";
            return result;
        }
        const auto& usage = *inputs->originalScalars()->originalUsage();
        const auto& authored = usage.originalSources()->originalBudget();
        const auto cpu = authored.maxCpuBytes[static_cast<std::size_t>(
            OriginalCommandAdmissionBudget::CpuDomain::Result)];
        if (!cpu || cpu != b.maxCpuResultBytes) {
            result.diagnostic = "RHI-ORIGINAL-READBACK-RESULT-CPU-BUDGET";
            return result;
        }
        const auto sourceAt =
            [&](std::size_t index) noexcept -> const OriginalCommandTextureReadbackSource* {
            const auto& command = usage.commands()[index];
            if (command.kind != CommandUsageKind::TextureReadback)
                return nullptr;
            const auto source = usage.references()[command.firstReference + 1u].sourceIndex;
            return std::get<std::shared_ptr<const OriginalCommandTextureReadbackSource>>(
                usage.originalSources()->sources()[source])
                .get();
        };
        const auto count = usage.commands().size();
        const auto identities = core::SharedIdentityProjection::inspect(count,
            count,
            b.maxResultDeclarations,
            [&](std::size_t index) noexcept -> core::SharedIdentityProjection::Record {
                const auto* source = sourceAt(index);
                if (!source)
                    return {};
                const auto& request = source->originalSource()->originalRequest();
                return {static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(request.get())),
                    1u,
                    request,
                    request.get()};
            });
        if (identities.status != core::SharedIdentityProjection::Status::Complete) {
            result.diagnostic = "RHI-ORIGINAL-READBACK-RESULT-DECLARATION-CAPACITY";
            return result;
        }
        // Destination is the prospective result charge token; Request is its
        // exact backing declaration. This is no actual allocation/native proof.
        // Distinct Requests sharing a destination are an ambiguous map lifetime
        // and fail rather than sharing guessed bytes or concurrent map ownership.
        const auto bytes = core::SharedAllocationProjection::inspect(count,
            count,
            std::min(b.maxUniqueResultBytes, authored.maxUnpublishedResultBytes),
            [&](std::size_t index) noexcept -> core::SharedAllocationProjection::Record {
                const auto* source = sourceAt(index);
                if (!source)
                    return {};
                return {source->originalDestinationLease(),
                    source->originalSource()->originalRequest(),
                    source->originalInput()->scalarFields().byteCapacity()};
            });
        if (bytes.status != core::SharedAllocationProjection::Status::Complete) {
            result.diagnostic = bytes.status == core::SharedAllocationProjection::Status::Alias
                ? "RHI-ORIGINAL-READBACK-RESULT-DESTINATION-ALIAS"
                : "RHI-ORIGINAL-READBACK-RESULT-BYTE-CAPACITY";
            return result;
        }
        if (counts.readbackDeclaredBytes > b.maxDeclaredReadbackBytes) {
            result.diagnostic = "RHI-ORIGINAL-READBACK-RESULT-DECLARED-CAPACITY";
            return result;
        }
        pending.resultDeclarations = identities.identities;
        pending.uniqueResultBytes = bytes.bytes;
        pending.declaredReadbackBytes = counts.readbackDeclaredBytes;
        pending.cpuResultBytes = cpu;
        result.required = pending;
        return result;
    }
};
} // namespace arp::rhi

// endregion original_command_readback_result

// region original_command_ordered_copy_attachment

namespace arp::rhi {
// OrderedOriginalCopyAttachment/1, explicitly selected by cold/lowering /2.
// L2 intent: copies and empty attachment passes execute at their original
// positions. No upload/readback adapter, draw, dispatch, query or debug no-op.
class OriginalCommandOrderedCopyAttachmentContract final {
public:
    static constexpr bool supports(CommandUsageKind kind) noexcept
    {
        using K = CommandUsageKind;
        switch (kind) {
        case K::BeginTransferPass:
        case K::EndTransferPass:
        case K::BufferCopy:
        case K::BufferToTextureCopy:
        case K::TextureToBufferCopy:
        case K::TextureToTextureCopy:
        case K::TextureBarrier:
        case K::BeginRenderPass:
        case K::EndRenderPass:
        case K::Finish:
            return true;
        default:
            return false;
        }
    }

    static OriginalCommandStreamProgram::Inspection inspect(
        const std::shared_ptr<const OriginalCommandInputs>& inputs,
        const OriginalCommandStreamBudget& budget) noexcept
    {
        auto result = OriginalCommandStreamProgram::inspect(inputs, budget);
        if (result.diagnostic)
            return result;
        const auto& commands = inputs->originalScalars()->originalUsage()->commands();
        for (std::size_t i = 0u; i < commands.size(); ++i)
            if (!supports(commands[i].kind)) {
                result.commandIndex = i;
                result.diagnostic = "RHI-ORIGINAL-ORDERED-COPY-ATTACHMENT-COMMAND";
                return result;
            }
        return result;
    }

    // Whole-range transitions use exact complete scopes and the existing state
    // validator. WebGPU automatic ordered usage supplies the normative native
    // mapping; the original root still owns the pending/final logical states.

    static const char* transition(
        const ResourceBarrierDesc& desc, const TextureSubresourceRange& whole) noexcept
    {
        if (const auto* id =
                semantics::RecordedResourceStateSequence::validateBarrier(desc, desc.before))
            return id;
        if (desc.textureSubresources != whole || !whole.mipLevelCount || !whole.arrayLayerCount
            || whole.baseMipLevel || whole.baseArrayLayer)
            return "RHI-ORIGINAL-ORDERED-COPY-ATTACHMENT-RANGE";
        const auto scope = [](ResourceState state,
                               PipelineStage stage,
                               ResourceAccess access) noexcept {
            switch (state) {
            case ResourceState::Undefined:
                return stage == PipelineStage::TopOfPipe && access == ResourceAccess::None;
            case ResourceState::CopySrc:
                return stage == PipelineStage::Transfer && access == ResourceAccess::TransferRead;
            case ResourceState::CopyDst:
                return stage == PipelineStage::Transfer && access == ResourceAccess::TransferWrite;
            case ResourceState::ColorAttachment:
                return stage == PipelineStage::ColorAttachmentOutput
                    && access
                    == (ResourceAccess::ColorAttachmentRead | ResourceAccess::ColorAttachmentWrite);
            case ResourceState::DepthAttachment:
                return stage == PipelineStage::DepthStencil
                    && access
                    == (ResourceAccess::DepthStencilRead | ResourceAccess::DepthStencilWrite);
            default:
                return false;
            }
        };
        if (!scope(desc.before, desc.sourceStages, desc.sourceAccess)
            || !scope(desc.after, desc.destinationStages, desc.destinationAccess))
            return "RHI-ORIGINAL-ORDERED-COPY-ATTACHMENT-SCOPE";
        if ((desc.before == ResourceState::ColorAttachment
                || desc.after == ResourceState::ColorAttachment)
            && whole.aspect != TextureAspect::Color)
            return "RHI-ORIGINAL-ORDERED-COPY-ATTACHMENT-ASPECT";
        if ((desc.before == ResourceState::DepthAttachment
                || desc.after == ResourceState::DepthAttachment)
            && whole.aspect != TextureAspect::Depth)
            return "RHI-ORIGINAL-ORDERED-COPY-ATTACHMENT-ASPECT";
        return nullptr;
    }
};

// endregion original_command_ordered_copy_attachment

// Explicit return from a completed sampled-depth consumer to the original
// attachment producer. This adds no shader command, completion or ownership
// authority to OrderedCopyAttachment/1. The original state/content journal must
// independently establish the committed ShaderRead state and retire all readers.
class OriginalCompletedDepthWriteReturnContract final {
public:
    struct Intent {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        PipelineStage previousStages = PipelineStage::None;
        std::uint32_t maxTransitions = 0u;
    };

    static constexpr bool selected(const Intent& intent) noexcept
    {
        return intent.contractVersion || intent.prototypeOptIn
            || intent.previousStages != PipelineStage::None || intent.maxTransitions;
    }

    static constexpr const char* validateIntent(const Intent& intent) noexcept
    {
        if (intent.contractVersion != 1u)
            return "RHI-DEPTH-WRITE-RETURN-VERSION";
        if (!intent.prototypeOptIn)
            return "RHI-DEPTH-WRITE-RETURN-OPT-IN";
        constexpr auto stages = static_cast<std::uint32_t>(PipelineStage::VertexShader)
            | static_cast<std::uint32_t>(PipelineStage::FragmentShader)
            | static_cast<std::uint32_t>(PipelineStage::ComputeShader);
        if (intent.previousStages == PipelineStage::None
            || (static_cast<std::uint32_t>(intent.previousStages) & stages)
                != static_cast<std::uint32_t>(intent.previousStages))
            return "RHI-DEPTH-WRITE-RETURN-STAGES";
        return intent.maxTransitions ? nullptr : "RHI-DEPTH-WRITE-RETURN-CAPACITY";
    }

    static const char* transition(const ResourceBarrierDesc& barrier,
        const TextureDesc& texture,
        const Intent& intent) noexcept
    {
        if (const auto* id = validateIntent(intent))
            return id;
        if (const auto* id =
                semantics::RecordedResourceStateSequence::validateBarrier(barrier, barrier.before))
            return id;
        const auto usage = TextureUsage::Sampled | TextureUsage::RenderAttachment;
        if (texture.format != TextureFormat::D32Float || texture.arrayLayerCount != 1u
            || texture.cubeCompatible || texture.sampleCount != 1u
            || (texture.usage & usage) != usage)
            return "RHI-DEPTH-WRITE-RETURN-TEXTURE";
        if (barrier.textureSubresources != defaultTextureSubresourceRange(texture)
            || barrier.textureSubresources.aspect != TextureAspect::Depth)
            return "RHI-DEPTH-WRITE-RETURN-RANGE";
        if (barrier.before != ResourceState::ShaderRead
            || barrier.after != ResourceState::DepthAttachment
            || barrier.sourceStages != intent.previousStages
            || barrier.sourceAccess != ResourceAccess::ShaderRead
            || barrier.destinationStages != PipelineStage::DepthStencil
            || barrier.destinationAccess
                != (ResourceAccess::DepthStencilRead | ResourceAccess::DepthStencilWrite))
            return "RHI-DEPTH-WRITE-RETURN-SCOPE";
        return nullptr;
    }
};

// region original_command_readback_result_storage

// CanonicalOriginalReadbackResultStorage/1: cold capacity only. One independent
// padded CPU snapshot per exact Request, including repeated writes. A cold
// declaration, enum, label or successful allocation grants no map/publication.
class OriginalCommandReadbackResultStorageContract final {
public:
    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t maxSnapshots = 0u, maxResultBytes = 0u, maxCpuResultBytes = 0u;
        std::uint32_t maxRetainedOwners = 0u;
    };

    static constexpr std::uint32_t fixedAdditionalOwners = 2u; // Storage and Result quota.

    static constexpr const char* validateIntent(const Budget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-ORIGINAL-READBACK-STORAGE-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-ORIGINAL-READBACK-STORAGE-OPT-IN";
        if (!b.maxSnapshots || !b.maxResultBytes || !b.maxCpuResultBytes || !b.maxRetainedOwners)
            return "RHI-ORIGINAL-READBACK-STORAGE-BUDGET";
        return nullptr;
    }

    // /2 binds the same owned snapshots to a typed mixed admission. It grants
    // neither map issuance nor publication; /1 remains the legacy profile.
    static constexpr const char* validateTypedIntent(const Budget& b) noexcept
    {
        if (b.contractVersion != 2u)
            return "RHI-ORIGINAL-TYPED-READBACK-STORAGE-VERSION";
        auto original = b;
        original.contractVersion = 1u;
        return validateIntent(original);
    }

    static constexpr const char* validateTypedRequirements(
        const OriginalCommandReadbackResultContract::Requirements& r,
        std::uint32_t originalOwners,
        const Budget& b) noexcept
    {
        if (const auto* id = validateTypedIntent(b))
            return id;
        auto original = b;
        original.contractVersion = 1u;
        return validateRequirements(r, originalOwners, original);
    }

    // /3 retains an already reserved genuine buffer snapshot. It cannot turn
    // a texture request into a buffer request or allocate a replacement result.
    static constexpr const char* validateBufferIntent(const Budget& b) noexcept
    {
        if (b.contractVersion != 3u)
            return "RHI-ORIGINAL-BUFFER-READBACK-STORAGE-VERSION";
        auto original = b;
        original.contractVersion = 1u;
        return validateIntent(original);
    }

    static constexpr const char* validateBufferRequirements(
        const OriginalCommandReadbackResultContract::Requirements& r,
        std::uint32_t originalOwners,
        const Budget& b) noexcept
    {
        if (const auto* id = validateBufferIntent(b))
            return id;
        auto original = b;
        original.contractVersion = 1u;
        return validateRequirements(r, originalOwners, original);
    }

    static constexpr const char* validateRequirements(
        const OriginalCommandReadbackResultContract::Requirements& r,
        std::uint32_t originalOwners,
        const Budget& b) noexcept
    {
        if (const auto* id = validateIntent(b))
            return id;
        if (!r.resultDeclarations || !r.uniqueResultBytes || !r.cpuResultBytes)
            return "RHI-ORIGINAL-READBACK-STORAGE-REQUIREMENTS";
        if (r.resultDeclarations > b.maxSnapshots || r.uniqueResultBytes > b.maxResultBytes
            || r.cpuResultBytes != b.maxCpuResultBytes || r.uniqueResultBytes > SIZE_MAX
            || r.resultDeclarations > SIZE_MAX)
            return "RHI-ORIGINAL-READBACK-STORAGE-CAPACITY";
        if (originalOwners > b.maxRetainedOwners
            || fixedAdditionalOwners > b.maxRetainedOwners - originalOwners
            || r.resultDeclarations > b.maxRetainedOwners - originalOwners - fixedAdditionalOwners)
            return "RHI-ORIGINAL-READBACK-STORAGE-OWNER-CAPACITY";
        return nullptr;
    }
};
} // namespace arp::rhi

// endregion original_command_readback_result_storage

// region original_command_readback_map

namespace arp::rhi {
// CanonicalOriginalReadbackMap/1 is an admission/lifetime contract, not an issuer.
// Same-root Submission evidence must precede native map issuance. Every exact
// Request requires one map receipt plus its own native error-scope batch; all
// callbacks and loss ownership are allocated before original recording starts.
class OriginalCommandReadbackMapContract final {
public:
    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t maxMaps = 0u, maxMapCallbacks = 0u, maxScopeCallbacks = 0u;
        std::uint64_t maxTotalCallbackTickets = 0u, maxTotalCallbackRegistrations = 0u;
    };

    struct Requirements {
        std::uint64_t maps = 0u, mapCallbacks = 0u, scopeCallbacks = 0u;
        std::uint64_t totalCallbackTickets = 0u, totalCallbackRegistrations = 0u;
    };

    // Backend mapping of this count must be versioned explicitly. Changing a
    // native error-scope policy must fail count checks, never infer extra slots.
    static constexpr std::uint64_t scopeCallbacksPerMap = 3u;
    static constexpr std::uint64_t mapCallbacksPerRequest = 1u;
    static constexpr std::uint64_t participantsPerMap =
        scopeCallbacksPerMap + mapCallbacksPerRequest;
    static_assert(
        participantsPerMap <= core::runtime::CompletionEvidenceBatch::maximumParticipants);

    static constexpr const char* validateIntent(const Budget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-ORIGINAL-READBACK-MAP-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-ORIGINAL-READBACK-MAP-OPT-IN";
        if (!b.maxMaps || !b.maxMapCallbacks || !b.maxScopeCallbacks || !b.maxTotalCallbackTickets
            || !b.maxTotalCallbackRegistrations)
            return "RHI-ORIGINAL-READBACK-MAP-BUDGET";
        return nullptr;
    }

    // Pure original count derivation. A failed inspection clears all outputs;
    // existing loss registration is retained once, never counted per Request.
    static const char* deriveRequirements(
        const OriginalCommandReadbackResultContract::Requirements& readback,
        const OriginalCommandAdmissionBudget& source,
        std::uint64_t originalTickets,
        std::uint64_t originalRegistrations,
        const Budget& b,
        Requirements& required) noexcept
    {
        required = {};
        if (const auto* id = validateIntent(b))
            return id;
        if (const auto* id = OriginalCommandAdmissionContract::validateIntent(source))
            return id;
        const auto resultCpu = source.maxCpuBytes[static_cast<std::size_t>(
            OriginalCommandAdmissionBudget::CpuDomain::Result)];
        if (const auto* id = OriginalCommandReadbackResultContract::validateRequirements(readback,
                {1u,
                    true,
                    readback.readbackCommands,
                    readback.nativeCopyCommands,
                    readback.resultDeclarations,
                    readback.declaredReadbackBytes,
                    source.maxUnpublishedResultBytes,
                    resultCpu}))
            return id;
        const auto maps = readback.resultDeclarations;
        if (!source.maxPendingSubmissions || !originalTickets || !originalRegistrations)
            return "RHI-ORIGINAL-READBACK-MAP-REQUIREMENTS";
        if (readback.uniqueResultBytes > source.maxUnpublishedResultBytes
            || readback.cpuResultBytes
                != source.maxCpuBytes[static_cast<std::size_t>(
                    OriginalCommandAdmissionBudget::CpuDomain::Result)])
            return "RHI-ORIGINAL-READBACK-MAP-SOURCE-RESULT-CAPACITY";
        if (maps > b.maxMaps || maps > SIZE_MAX || maps > UINT64_MAX / participantsPerMap)
            return "RHI-ORIGINAL-READBACK-MAP-COUNT-CAPACITY";
        const auto scopes = maps * scopeCallbacksPerMap, callbacks = maps * participantsPerMap;
        if (maps > b.maxMapCallbacks || scopes > b.maxScopeCallbacks
            || originalTickets > UINT64_MAX - callbacks
            || originalRegistrations > UINT64_MAX - callbacks)
            return "RHI-ORIGINAL-READBACK-MAP-CALLBACK-CAPACITY";
        const auto tickets = originalTickets + callbacks,
                   registrations = originalRegistrations + callbacks;
        if (tickets > b.maxTotalCallbackTickets || registrations > b.maxTotalCallbackRegistrations
            || tickets > source.maxCallbackTickets
            || registrations > source.maxCallbackRegistrations)
            return "RHI-ORIGINAL-READBACK-MAP-SOURCE-CALLBACK-CAPACITY";
        required = {maps, maps, scopes, tickets, registrations};
        return nullptr;
    }

    static constexpr bool hasBudget(const Budget& b) noexcept
    {
        return b.contractVersion || b.prototypeOptIn || b.maxMaps || b.maxMapCallbacks
            || b.maxScopeCallbacks || b.maxTotalCallbackTickets || b.maxTotalCallbackRegistrations;
    }

    static constexpr bool hasRequirements(const Requirements& r) noexcept
    {
        return r.maps || r.mapCallbacks || r.scopeCallbacks || r.totalCallbackTickets
            || r.totalCallbackRegistrations;
    }

    static constexpr const char* validateRequirements(
        const OriginalCommandReadbackResultContract::Requirements& readback,
        std::uint64_t originalTickets,
        std::uint64_t originalRegistrations,
        const Budget& b,
        const Requirements& r) noexcept
    {
        if (const auto* id = validateIntent(b))
            return id;
        if (!r.maps || r.maps != readback.resultDeclarations || r.maps != r.mapCallbacks
            || r.maps > UINT64_MAX / participantsPerMap
            || r.scopeCallbacks != r.maps * scopeCallbacksPerMap
            || originalTickets > UINT64_MAX - r.maps * participantsPerMap
            || originalRegistrations > UINT64_MAX - r.maps * participantsPerMap
            || r.totalCallbackTickets != originalTickets + r.maps * participantsPerMap
            || r.totalCallbackRegistrations != originalRegistrations + r.maps * participantsPerMap)
            return "RHI-ORIGINAL-READBACK-MAP-REQUIREMENTS";
        if (r.maps > b.maxMaps || r.mapCallbacks > b.maxMapCallbacks
            || r.scopeCallbacks > b.maxScopeCallbacks
            || r.totalCallbackTickets > b.maxTotalCallbackTickets
            || r.totalCallbackRegistrations > b.maxTotalCallbackRegistrations)
            return "RHI-ORIGINAL-READBACK-MAP-CALLBACK-CAPACITY";
        return nullptr;
    }
    enum class Phase {
        Prepared,
        AwaitingSubmission,
        Mapping,
        AwaitingMapScopes,
        Published,
        Quarantined,
        Retired
    };
    enum class Event {
        BeginSubmission,
        SameRootSubmissionProven,
        MapPixelsCopiedAndUnmapped,
        MapAndScopesProven,
        Cancel,
        NativeError,
        NativeLoss,
        CallbackSuppressionProven,
        AllForeignCallbacksDrained
    };
    enum class Action {
        Preserve,
        RetainRootAndIssueMap,
        KeepUnpublished,
        PublishImmutableSnapshot,
        InvalidateAndRetain,
        ReleaseRetiredOwners
    };

    struct Transition {
        Phase next;
        Action action = Action::Preserve;
        const char* diagnostic = "RHI-ORIGINAL-READBACK-MAP-TRANSITION";
    };

    static constexpr Transition inspectTransition(Phase phase, Event event) noexcept
    {
        using P = Phase;
        using E = Event;
        using A = Action;
        switch (phase) {
        case P::Prepared:
        case P::AwaitingSubmission:
        case P::Mapping:
        case P::AwaitingMapScopes:
        case P::Published:
        case P::Quarantined:
        case P::Retired:
            break;
        default:
            return {phase};
        }
        if (phase == P::Published || phase == P::Retired)
            return {phase};
        if (phase != P::Quarantined
            && (event == E::Cancel || event == E::NativeError || event == E::NativeLoss))
            return {P::Quarantined, A::InvalidateAndRetain, nullptr};
        switch (phase) {
        case P::Prepared:
            if (event == E::BeginSubmission)
                return {P::AwaitingSubmission, A::KeepUnpublished, nullptr};
            break;
        case P::AwaitingSubmission:
            if (event == E::SameRootSubmissionProven)
                return {P::Mapping, A::RetainRootAndIssueMap, nullptr};
            break;
        case P::Mapping:
            if (event == E::MapPixelsCopiedAndUnmapped)
                return {P::AwaitingMapScopes, A::KeepUnpublished, nullptr};
            break;
        case P::AwaitingMapScopes:
            if (event == E::MapAndScopesProven)
                return {P::Published, A::PublishImmutableSnapshot, nullptr};
            break;
        case P::Quarantined:
            if (event == E::AllForeignCallbacksDrained)
                return {P::Retired, A::ReleaseRetiredOwners, nullptr};
            break;
        case P::Published:
        case P::Retired:
            break;
        }
        // Suppressed map listeners are not map outcome or unmap lifetime proof.
        // The runtime additionally needs actual loss/finished-map evidence before
        // claiming AllForeignCallbacksDrained. Caller events cannot grant it.
        return {phase};
    }
};
} // namespace arp::rhi

// endregion original_command_readback_map

// region original_command_ordered_readback

namespace arp::rhi {
// OrderedOriginalReadback/1: ordinary source content, exact original Request
// and MapRead destination. All mips execute in original order in one encoder.
// Genuine scoped Recording and Submission own the sole root/seal/journal.
// Mapping/publication and exclusive content reads remain separate closed gates.
class OriginalCommandOrderedReadbackContract final {
public:
    using Budget = OriginalCommandReadbackResultContract::Budget;

    static auto inspect(const std::shared_ptr<const OriginalCommandInputs>& inputs,
        const OriginalCommandStreamBudget& stream,
        const Budget& b) noexcept
    {
        return OriginalCommandReadbackResultContract::inspect(inputs, stream, b);
    }
};
} // namespace arp::rhi

// endregion original_command_ordered_readback
// endregion Command readback validation

// region Command native lifecycle validation
// region original_command_native_lifecycle

namespace arp::rhi {
// CanonicalOriginalNativeLifecycle/1. Intent and read-only transition planning;
// public enum values/inspection never issue a recording or completion proof.
// Every event below requires a sealed backend-private issuer over the same root,
// original device/queue/command and non-recycled reservation/callback tickets.
struct OriginalCommandNativeLifecycleBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxPendingSubmissions = 0u, maxCallbackTickets = 0u,
                  maxCallbackRegistrations = 0u;
    std::uint32_t maxRecordingOwners = 0u, maxSubmissionOwners = 0u, maxCallbackOwners = 0u;
    std::uint64_t maxRecordingCpuBytes = 0u, maxSubmissionCpuBytes = 0u, maxCallbackCpuBytes = 0u;
};

// Explicit cold capacity demand, not native callback/queue evidence. Native
// recording must derive these counts from a versioned lowering, not trust caller
// counts. /1 retains one canonical recording's single pending submission slot.
struct OriginalCommandLifecycleCapacityRequest {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t pendingSubmissions = 0u, callbackTickets = 0u, callbackRegistrations = 0u;
};

class OriginalCommandNativeLifecycleContract final {
public:
    using Budget = OriginalCommandNativeLifecycleBudget;

    static constexpr bool isFenceProfile(std::uint32_t version) noexcept
    {
        return version == 6u || version == 7u || version == 8u || version == 9u || version == 14u
            || version == 15u || version == 17u || version == 19u || version == 21u;
    }
    // The sealed claim is exclusive and one-shot. Public CPU apply/begin/commit/
    // abandon must refuse while claimed, including callbacks and quarantine.
    // A backend must not release the claim back to CPU mode or retry this root.
    enum class Phase {
        Unclaimed,
        Claimed,
        RecordingPending,
        Recorded,
        SubmitReserved,
        SubmitPending,
        QuarantinedBeforeSubmit,
        QuarantinedAfterSubmit,
        DeviceLostRetained,
        Completed,
        Abandoned,
        DeviceLost
    };
    enum class Event {
        Claim,
        BeginRecording,
        RecordingValidatedAndDrained,
        RecordingFailed,
        ReserveSubmission,
        MaySubmit,
        // Backend-issued proof for this exact submission domain. RootFaultFence
        // /6 accepts the exact native command's successful terminal completion,
        // a successful submission fence or successful device-idle
        // observation for the same retained runtime, with no callbacks to drain.
        // Caller flags, wrapper destruction, timeouts and device loss are not
        // completion. An invalidated journal can only retire unpublished.
        CompletionProven,
        CompletionUnproven,
        Cancel,
        BeforeSubmitScopesDrained,
        SubmittedQueueAndCallbacksDrained,
        CapturedDeviceLoss,
        LossCallbacksDrainedOrDetached
    };
    enum class RootAction {
        Preserve,
        ApplyPlan,
        KeepPending,
        Commit,
        Rollback,
        InvalidateAndRetain,
        InvalidateAndRelease
    };

    struct Transition {
        Phase next;
        RootAction action = RootAction::Preserve;
        const char* diagnostic = "RHI-ORIGINAL-NATIVE-LIFECYCLE-TRANSITION";

        bool accepted() const noexcept { return !diagnostic; }
    };

    static constexpr Transition inspectTransition(Phase phase, Event event) noexcept
    {
        using P = Phase;
        using E = Event;
        using A = RootAction;
        // Unknown enums and terminal phases cannot gain new authority.
        switch (phase) {
        case P::Unclaimed:
            if (event == E::Claim)
                return {P::Claimed, A::Preserve, nullptr};
            break;
        case P::Claimed:
            if (event == E::BeginRecording)
                return {P::RecordingPending, A::ApplyPlan, nullptr};
            if (event == E::Cancel)
                return {P::Abandoned, A::Rollback, nullptr};
            break;
        case P::RecordingPending:
            if (event == E::RecordingValidatedAndDrained)
                return {P::Recorded, A::KeepPending, nullptr};
            if (event == E::RecordingFailed || event == E::Cancel)
                return {P::QuarantinedBeforeSubmit, A::KeepPending, nullptr};
            break;
        case P::Recorded:
            if (event == E::ReserveSubmission)
                return {P::SubmitReserved, A::KeepPending, nullptr};
            if (event == E::Cancel)
                return {P::Abandoned, A::Rollback, nullptr};
            break;
        case P::SubmitReserved:
            if (event == E::MaySubmit)
                return {P::SubmitPending, A::KeepPending, nullptr};
            if (event == E::Cancel)
                return {P::Abandoned, A::Rollback, nullptr};
            break;
        case P::SubmitPending:
            if (event == E::CompletionProven)
                return {P::Completed, A::Commit, nullptr};
            if (event == E::CompletionUnproven || event == E::Cancel)
                return {P::QuarantinedAfterSubmit, A::InvalidateAndRetain, nullptr};
            break;
        case P::QuarantinedBeforeSubmit:
            if (event == E::BeforeSubmitScopesDrained)
                return {P::Abandoned, A::Rollback, nullptr};
            break;
        case P::QuarantinedAfterSubmit:
            if (event == E::SubmittedQueueAndCallbacksDrained)
                return {P::Abandoned, A::InvalidateAndRelease, nullptr};
            break;
        case P::DeviceLostRetained:
            if (event == E::LossCallbacksDrainedOrDetached)
                return {P::DeviceLost, A::InvalidateAndRelease, nullptr};
            return {phase};
        case P::Completed:
        case P::Abandoned:
        case P::DeviceLost:
            return {phase};
        default:
            return {phase};
        }
        if (phase != P::Unclaimed && event == E::CapturedDeviceLoss)
            return {P::DeviceLostRetained, A::InvalidateAndRetain, nullptr};
        return {phase};
    }

    static constexpr const char* validateIntent(const Budget& b) noexcept
    {
        if (b.contractVersion != 1u && b.contractVersion != 2u && b.contractVersion != 3u
            && b.contractVersion != 4u && b.contractVersion != 5u && b.contractVersion != 6u
            && b.contractVersion != 7u && b.contractVersion != 8u && b.contractVersion != 9u
            && b.contractVersion != 10u && b.contractVersion != 11u && b.contractVersion != 12u
            && b.contractVersion != 13u && b.contractVersion != 14u && b.contractVersion != 15u
            && b.contractVersion != 16u && b.contractVersion != 17u && b.contractVersion != 18u
            && b.contractVersion != 19u && b.contractVersion != 20u && b.contractVersion != 21u
            && b.contractVersion != 22u)
            return "RHI-ORIGINAL-NATIVE-LIFECYCLE-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-ORIGINAL-NATIVE-LIFECYCLE-OPT-IN";
        if (OriginalCommandNativeLifecycleContract::isFenceProfile(b.contractVersion)) {
            // RootFaultFence/6 has a real Vulkan fence and a mapped buffer,
            // with no foreign callbacks or error-scope registrations.
            if (b.maxCallbackTickets || b.maxCallbackRegistrations || b.maxCallbackOwners
                || b.maxCallbackCpuBytes)
                return "RHI-ORIGINAL-ROOT-FENCE-CALLBACK-BUDGET";
            if (!b.maxPendingSubmissions || !b.maxRecordingOwners || !b.maxSubmissionOwners
                || !b.maxRecordingCpuBytes || !b.maxSubmissionCpuBytes)
                return "RHI-ORIGINAL-ROOT-FENCE-BUDGET";
            return nullptr;
        }
        if (!b.maxPendingSubmissions || !b.maxCallbackTickets || !b.maxCallbackRegistrations)
            return "RHI-ORIGINAL-NATIVE-LIFECYCLE-COUNT-BUDGET";
        if (!b.maxRecordingOwners || !b.maxSubmissionOwners || !b.maxCallbackOwners)
            return "RHI-ORIGINAL-NATIVE-LIFECYCLE-OWNER-BUDGET";
        if (!b.maxRecordingCpuBytes || !b.maxSubmissionCpuBytes || !b.maxCallbackCpuBytes)
            return "RHI-ORIGINAL-NATIVE-LIFECYCLE-CPU-BUDGET";
        return nullptr;
    }

    // Lifecycle2 retains mixed readback; Lifecycle3 retains completed general
    // shader reads; Lifecycle4 retains finite General ContentPlan5. These
    // Lifecycle5 retains only mixed Graphics ContentPlan6. These independent
    // cold seals grant no native execution.
    static constexpr const char* validateContentProfile(
        const Budget& budget, std::uint32_t contentVersion) noexcept
    {
        // ProducedGeometry/21 (fence) and /22 (callback) share Content9,
        // but ownership inspection additionally requires Root6/Inputs8 and
        // Geometry2. Historical Content9/Geometry1 cannot borrow this seal.
        if (budget.contractVersion == 21u || budget.contractVersion == 22u)
            return contentVersion == 9u ? nullptr
                                        : "RHI-BINDLESS-PRODUCED-GEOMETRY-LIFECYCLE-CONTENT";
        if (budget.contractVersion == 20u)
            return contentVersion == 19u ? nullptr
                                         : "RHI-CANONICAL-GRAPHICS-DEPTH-CALLBACK-CONTENT-PROFILE";
        if (contentVersion == 19u)
            return "RHI-CANONICAL-GRAPHICS-DEPTH-CONTENT-LIFECYCLE-UNAVAILABLE";
        if (budget.contractVersion == 19u)
            return contentVersion == 18u ? nullptr : "RHI-GRAPHICS-DEPTH-FENCE-CONTENT-PROFILE";
        if (contentVersion == 18u)
            return "RHI-GRAPHICS-DEPTH-CONTENT-LIFECYCLE-UNAVAILABLE";
        if (budget.contractVersion == 17u || budget.contractVersion == 18u)
            return contentVersion == (budget.contractVersion == 17u ? 15u : 16u)
                ? nullptr
                : "RHI-MIXED-ROOT-EXECUTION-CONTENT-PROFILE";
        if (OriginalStorageTextureContentPlanContract::isMixedVersion(contentVersion))
            return "RHI-MIXED-DEPTH-STORAGE-LIFECYCLE-UNAVAILABLE";
        if (budget.contractVersion == 16u)
            return contentVersion == 14u ? nullptr : "RHI-STORAGE-ROOT-CALLBACK-CONTENT-PROFILE";
        if (budget.contractVersion == 15u)
            return contentVersion == 13u ? nullptr
                                         : "RHI-STORAGE-ROOT-NATIVE-FENCE-CONTENT-PROFILE";
        if (contentVersion == 13u || contentVersion == 14u)
            return "RHI-STORAGE-CONTENT-PLAN-NATIVE-LIFECYCLE-UNAVAILABLE";
        if (budget.contractVersion == 14u)
            return contentVersion == OriginalCompletedDepthRootReadPlanContract::nativeVersion
                ? nullptr
                : "RHI-DEPTH-ROOT-NATIVE-FENCE-CONTENT-PROFILE";
        if (budget.contractVersion == 13u)
            return contentVersion == OriginalCompletedDepthRootReadPlanContract::version
                ? nullptr
                : "RHI-DEPTH-ROOT-NATIVE-LIFECYCLE-CONTENT-PROFILE";
        if (OriginalCompletedDepthRootReadPlanContract::isVersion(contentVersion))
            return "RHI-DEPTH-ROOT-READ-PLAN-NATIVE-LIFECYCLE-UNAVAILABLE";
        // Graphics callback12 has the original Content9 attachment journal.
        if (budget.contractVersion == 12u)
            return contentVersion == 9u ? nullptr
                                        : "RHI-ORIGINAL-ROOT-GRAPHICS-CALLBACK-CONTENT-PROFILE";
        // RootCallback /10 has buffer fault readback and foreign callbacks.
        // It cannot borrow a Vulkan fence or any texture content profile.
        if (budget.contractVersion == 10u || budget.contractVersion == 11u)
            return contentVersion == 0u ? nullptr : "RHI-ORIGINAL-ROOT-CALLBACK-CONTENT-PROFILE";
        // GraphicsRootFence /9 is independent of Compute /6,/8 and General /5.
        if (budget.contractVersion == 9u)
            return contentVersion == 9u ? nullptr : "RHI-BINDLESS-GRAPHICS-FENCE-CONTENT-PROFILE";
        if (contentVersion == 9u)
            return "RHI-BINDLESS-GRAPHICS-CONTENT-NATIVE-LIFECYCLE-UNAVAILABLE";
        // CompletedRootTextureFence /8 consumes only read-only ContentPlan /8.
        if (budget.contractVersion == 8u)
            return contentVersion == 8u ? nullptr
                                        : "RHI-BINDLESS-COMPLETED-TEXTURE-FENCE-CONTENT-PROFILE";
        if (contentVersion == 8u)
            return "RHI-BINDLESS-COMPLETED-TEXTURE-NATIVE-LIFECYCLE-UNAVAILABLE";
        // WholeOriginalUploadFence /7 is isolated from every existing callback
        // and buffer-only fence profile. It commits only ContentPlan /7.
        if (budget.contractVersion == 7u)
            return contentVersion == 7u ? nullptr : "RHI-ORIGINAL-UPLOAD-FENCE-CONTENT-PROFILE";
        if (contentVersion == 7u)
            return "RHI-UNDEFINED-TEXTURE-UPLOAD-NATIVE-LIFECYCLE-UNAVAILABLE";
        if (budget.contractVersion == 6u)
            return contentVersion == 0u ? nullptr : "RHI-ORIGINAL-ROOT-FENCE-CONTENT-PROFILE";
        if (budget.contractVersion == 5u)
            return contentVersion == 6u ? nullptr
                                        : "RHI-ORIGINAL-NATIVE-LIFECYCLE-GRAPHICS-CONTENT-PROFILE";
        if (contentVersion == 6u)
            return "RHI-ORIGINAL-NATIVE-LIFECYCLE-GRAPHICS-CONTENT-PROFILE-UNAVAILABLE";
        if (budget.contractVersion == 4u)
            return contentVersion == 5u ? nullptr
                                        : "RHI-ORIGINAL-NATIVE-LIFECYCLE-FINITE-CONTENT-PROFILE";
        if (contentVersion == 5u)
            return "RHI-ORIGINAL-NATIVE-LIFECYCLE-FINITE-CONTENT-PROFILE-UNAVAILABLE";
        if (budget.contractVersion == 3u)
            return contentVersion == 4u ? nullptr
                                        : "RHI-ORIGINAL-NATIVE-LIFECYCLE-GENERAL-CONTENT-PROFILE";
        if (contentVersion == 4u)
            return "RHI-ORIGINAL-NATIVE-LIFECYCLE-COMPLETED-READ-PROFILE-UNAVAILABLE";
        if (budget.contractVersion != 2u && contentVersion == 3u)
            return "RHI-ORIGINAL-NATIVE-LIFECYCLE-CONTENT-READBACK-PROFILE";
        if (budget.contractVersion == 2u && contentVersion != 3u)
            return "RHI-ORIGINAL-NATIVE-LIFECYCLE-MIXED-CONTENT-PROFILE";
        return nullptr;
    }

    static constexpr const char* validateCapacityRequest(
        const OriginalCommandLifecycleCapacityRequest& request, const Budget& b) noexcept
    {
        if (const auto* id = validateIntent(b))
            return id;
        if (request.contractVersion
            != (b.contractVersion == 21u       ? 11u
                    : b.contractVersion == 19u ? 10u
                    : b.contractVersion == 17u ? 9u
                    : b.contractVersion == 15u ? 8u
                    : b.contractVersion == 14u ? 7u
                    : b.contractVersion == 10u || b.contractVersion == 11u
                        || b.contractVersion == 12u || b.contractVersion == 13u
                        || b.contractVersion == 16u || b.contractVersion == 18u
                        || b.contractVersion == 20u || b.contractVersion == 22u
                    ? 6u
                    : b.contractVersion == 9u ? 5u
                    : b.contractVersion == 8u ? 4u
                    : b.contractVersion == 7u ? 3u
                    : b.contractVersion == 6u ? 2u
                                              : 1u))
            return "RHI-ORIGINAL-LIFECYCLE-CAPACITY-VERSION";
        if (!request.prototypeOptIn)
            return "RHI-ORIGINAL-LIFECYCLE-CAPACITY-OPT-IN";
        if (request.pendingSubmissions != 1u
            || ((OriginalCommandNativeLifecycleContract::isFenceProfile(b.contractVersion))
                    ? (request.callbackTickets || request.callbackRegistrations)
                    : (!request.callbackTickets || !request.callbackRegistrations)))
            return "RHI-ORIGINAL-LIFECYCLE-CAPACITY-REQUEST";
        if (request.pendingSubmissions > b.maxPendingSubmissions)
            return "RHI-ORIGINAL-LIFECYCLE-CAPACITY-SUBMISSION-BUDGET";
        if (request.callbackTickets > b.maxCallbackTickets)
            return "RHI-ORIGINAL-LIFECYCLE-CAPACITY-CALLBACK-BUDGET";
        if (request.callbackRegistrations > b.maxCallbackRegistrations)
            return "RHI-ORIGINAL-LIFECYCLE-CAPACITY-REGISTRATION-BUDGET";
        return nullptr;
    }

    // The cold Result join binds every buffer copy to a distinct Result.
    // Completion must consume that exact copy count, including ordinary data
    // beside the snapshot latch. This count is not native completion evidence.
    static const char* validateRootReadbackMapCount(
        const OriginalCommandUsage& usage, std::uint64_t maps) noexcept
    {
        std::uint64_t copies = 0u;
        for (const auto& command : usage.commands())
            if (command.kind == CommandUsageKind::BufferCopy)
                ++copies;
        return copies && maps == copies ? nullptr
                                        : "RHI-ORIGINAL-ROOT-READBACK-COMPLETION-COPY-COVERAGE";
    }

    // Source authors admission counts and independent CPU domains. Backend
    // requirements must additionally derive every scope/callback/native object
    // count from its versioned lowering; these caller limits prove no payload.
    static constexpr const char* validateSourceBudget(
        const OriginalCommandAdmissionBudget& source, const Budget& b) noexcept
    {
        if (const auto* id = validateIntent(b))
            return id;
        if (const auto* id = OriginalCommandAdmissionContract::validateIntent(source))
            return id;
        if (b.maxPendingSubmissions > source.maxPendingSubmissions)
            return "RHI-ORIGINAL-NATIVE-LIFECYCLE-SUBMISSION-CAPACITY";
        if (b.maxCallbackTickets > source.maxCallbackTickets)
            return "RHI-ORIGINAL-NATIVE-LIFECYCLE-CALLBACK-CAPACITY";
        if (b.maxCallbackRegistrations > source.maxCallbackRegistrations)
            return "RHI-ORIGINAL-NATIVE-LIFECYCLE-REGISTRATION-CAPACITY";
        using D = OriginalCommandAdmissionBudget::CpuDomain;
        if ((OriginalCommandNativeLifecycleContract::isFenceProfile(b.contractVersion))
            && (source.maxCallbackTickets || source.maxCallbackRegistrations
                || source.maxCpuBytes[static_cast<std::size_t>(D::Callback)]))
            return "RHI-ORIGINAL-ROOT-FENCE-SOURCE-CALLBACKS";
        if (b.maxRecordingCpuBytes != source.maxCpuBytes[static_cast<std::size_t>(D::Recording)]
            || b.maxSubmissionCpuBytes
                != source.maxCpuBytes[static_cast<std::size_t>(D::Submission)]
            || b.maxCallbackCpuBytes != source.maxCpuBytes[static_cast<std::size_t>(D::Callback)])
            return "RHI-ORIGINAL-NATIVE-LIFECYCLE-SOURCE-CPU-BUDGET";
        return nullptr;
    }

    static const char* inspectOriginalPreparation(
        const std::shared_ptr<const OriginalCommandStateTransaction>& root,
        const Budget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& submission,
        const std::shared_ptr<core::CpuMemoryQuota>& callback) noexcept
    {
        if (const auto* id = OriginalCommandStateTransaction::validateOriginal(root))
            return id;
        if (root->status() != OriginalCommandStateTransaction::Status::Prepared
            || !root->hasOriginalPrototypePlan())
            return "RHI-ORIGINAL-NATIVE-LIFECYCLE-PLAN-STATUS";
        if (const auto* id =
                validateContentProfile(budget, root->originalContentPlanContractVersion()))
            return id;
        return inspectOriginalOwnership(root, budget, submission, callback);
    }

    // Read-only ownership/final-state inspection, never a native event. Live
    // issuers must additionally bind exact native identity and completion facts.
    static const char* inspectOriginalOwnership(
        const std::shared_ptr<const OriginalCommandStateTransaction>& root,
        const Budget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& submission,
        const std::shared_ptr<core::CpuMemoryQuota>& callback) noexcept
    {
        if (const auto* id = OriginalCommandStateTransaction::validateOriginal(root))
            return id;
        if (!root->hasOriginalPrototypePlan())
            return "RHI-ORIGINAL-NATIVE-LIFECYCLE-PLAN-STATUS";
        if (const auto* id =
                validateContentProfile(budget, root->originalContentPlanContractVersion()))
            return id;
        const auto& graph = root->originalGraph();
        const bool produced = root->hasOriginalBindlessProducedGeometryProfile();
        const bool producedLifecycle =
            budget.contractVersion == 21u || budget.contractVersion == 22u;
        if (produced != producedLifecycle)
            return "RHI-BINDLESS-PRODUCED-GEOMETRY-LIFECYCLE-ROOT";
        if (producedLifecycle
            && (!root->originalMixedContentCounts() || !root->originalPlannedSequenceBudget()
                || root->originalPlannedSequenceBudget()->contractVersion != 1u
                || root->originalPlannedSequenceBudget()->bindlessRootStateVersion != 6u
                || !root->originalPlannedSequenceBudget()->bindlessRootStateOptIn
                || graph->originalInputs()->originalBudget().contractVersion != 8u))
            return "RHI-BINDLESS-PRODUCED-GEOMETRY-LIFECYCLE-ROOT";
        if ((budget.contractVersion == 19u || budget.contractVersion == 20u)
            && (!root->originalGraphicsDepthContentCounts()
                || root->hasOriginalBindlessGraphicsGeometryProfile()))
            return "RHI-GRAPHICS-DEPTH-FENCE-ROOT-PROFILE";
        if ((budget.contractVersion == 10u || budget.contractVersion == 11u
                || budget.contractVersion == 13u)
            && (graph->stateContractVersion() != 1u || !root->originalPlannedSequenceBudget()
                || root->originalPlannedSequenceBudget()->contractVersion != 1u
                || root->originalPlannedSequenceBudget()->bindlessRootStateVersion != 4u))
            return "RHI-ORIGINAL-ROOT-CALLBACK-ROOT-PROFILE";
        if ((budget.contractVersion == 12u || budget.contractVersion == 20u)
            && ((!root->hasOriginalBindlessRootShaderResourceProfile()
                    && !(budget.contractVersion == 12u
                        && root->hasOriginalBindlessGraphicsGeometryProfile()))
                || !root->originalMixedContentCounts() || !root->originalPlannedSequenceBudget()
                || root->originalPlannedSequenceBudget()->contractVersion != 1u
                || root->originalPlannedSequenceBudget()->bindlessRootStateVersion
                    != (root->hasOriginalBindlessGraphicsGeometryProfile() ? 5u : 4u)))
            return "RHI-ORIGINAL-ROOT-GRAPHICS-CALLBACK-ROOT-PROFILE";
        if ((budget.contractVersion == 9u || budget.contractVersion == 19u)
            && ((!root->hasOriginalBindlessRootShaderResourceProfile()
                    && !root->hasOriginalBindlessGraphicsGeometryProfile())
                || !root->originalMixedContentCounts() || !root->originalPlannedSequenceBudget()
                || root->originalPlannedSequenceBudget()->contractVersion != 1u
                || root->originalPlannedSequenceBudget()->bindlessRootStateVersion
                    != (root->hasOriginalBindlessGraphicsGeometryProfile() ? 5u : 4u)))
            return "RHI-BINDLESS-GRAPHICS-FENCE-ROOT-PROFILE";
        if ((budget.contractVersion == 8u || budget.contractVersion == 14u
                || budget.contractVersion == 15u || budget.contractVersion == 16u
                || budget.contractVersion == 17u || budget.contractVersion == 18u)
            && (!root->hasOriginalBindlessRootShaderResourceProfile()
                || !root->originalPlannedSequenceBudget()
                || root->originalPlannedSequenceBudget()->contractVersion != 1u
                || root->originalPlannedSequenceBudget()->bindlessRootStateVersion != 4u))
            return "RHI-BINDLESS-COMPLETED-TEXTURE-FENCE-ROOT-PROFILE";
        if (budget.contractVersion == 6u
            && (!root->originalPlannedSequenceBudget()
                || root->originalPlannedSequenceBudget()->bindlessRootStateVersion != 4u))
            return "RHI-ORIGINAL-ROOT-FENCE-ROOT-PROFILE";
        if (budget.contractVersion == 7u
            && (graph->stateContractVersion() != 1u
                || graph->originalInputs()->originalBudget().contractVersion != 1u
                || !root->originalPlannedSequenceBudget()
                || root->originalPlannedSequenceBudget()->contractVersion != 1u
                || root->originalPlannedSequenceBudget()->bindlessRootStateVersion))
            return "RHI-ORIGINAL-UPLOAD-FENCE-ROOT-PROFILE";
        if (budget.contractVersion == 5u
            && (!root->hasOriginalFiniteGeneralShaderResourceProfile()
                || root->hasOriginalGeneralShaderResourceProfile()
                || !root->originalMixedContentCounts()))
            return "RHI-ORIGINAL-NATIVE-LIFECYCLE-GRAPHICS-ROOT-PROFILE";
        if (budget.contractVersion == 3u && !root->hasOriginalGeneralShaderResourceProfile())
            return "RHI-ORIGINAL-NATIVE-LIFECYCLE-GENERAL-ROOT-PROFILE";
        if (budget.contractVersion == 4u
            && (!root->hasOriginalFiniteGeneralShaderResourceProfile()
                || root->hasOriginalGeneralShaderResourceProfile()))
            return "RHI-ORIGINAL-NATIVE-LIFECYCLE-FINITE-ROOT-PROFILE";
        const auto& inputs = graph->originalInputs();
        const auto& source = inputs->originalScalars()->originalUsage()->originalSources();
        if (const auto* id = validateSourceBudget(source->originalBudget(), budget))
            return id;
        const auto& recording = root->originalMetadataQuota();
        if (!recording || recording->limitBytes() != budget.maxRecordingCpuBytes || !submission
            || submission->limitBytes() != budget.maxSubmissionCpuBytes
            || ((OriginalCommandNativeLifecycleContract::isFenceProfile(budget.contractVersion))
                    ? bool(callback)
                    : (!callback || callback->limitBytes() != budget.maxCallbackCpuBytes)))
            return "RHI-ORIGINAL-NATIVE-LIFECYCLE-QUOTA";
        const std::array domains{source->originalMetadataQuota(),
            inputs->originalMetadataQuota(),
            graph->originalMetadataQuota(),
            recording,
            submission,
            callback};
        if (!core::SharedOwnerProjection::hasIndependentOwners(
                (OriginalCommandNativeLifecycleContract::isFenceProfile(budget.contractVersion))
                    ? domains.size() - 1u
                    : domains.size(),
                [&](std::size_t i) noexcept -> const auto& {
                    return domains[i];
                }))
            return "RHI-ORIGINAL-NATIVE-LIFECYCLE-QUOTA-DOMAIN";
        if (root->retainedOwnerCount() > budget.maxRecordingOwners)
            return "RHI-ORIGINAL-NATIVE-LIFECYCLE-RECORDING-OWNER-CAPACITY";
        return nullptr;
    }
};

// Lowering obligations /1:
// Null validates only; Vulkan/Metal/WebGPU reject until real issuer payloads exist.
// WebGPU must reuse Recording/Submission issuers, actual encoder/finished command,
// queue work-done + all error scopes, original device.lost registration, and
// CompletionRetentionPool/CompletionEvidenceBatch/ExternalCallbackGate. No second
// state graph/journal, CPU signal, host wait, event enum or boolean proves success.
// MaySubmit is sealed BEFORE the native queue call. Exceptions, scope failure,
// cancel/drop or unknown callback results after it cannot roll back submitted
// work. Quarantine closes admission and invalidates original affected controls;
// only exact queue drain or captured original loss AND safe callback teardown
// releases owners. Rollback after terminal invalidation cannot revive resources.
// Completed needs exact immutable root/native closure/command/device/queue/ticket,
// successful recording scopes, queue + submission scopes, unchanged pending seals,
// and live original loss registration. Receipt/drain alone is never success.
// Recording rejection drains issued callbacks before rollback/release. Runtime
// owns pending/quarantined roots independently of caller references. Cold prepare
// reserves all tickets/owners/capacities; every hot transition allocates0, waits0,
// IO0. Quota exhaustion refuses before claim/encoder, no implicit new allocation.
// Retry requires a new original graph/root after a proven pre-submit rollback;
// post-submit invalidation/loss never retries the original resources. No NativeSync
// promotion, renderer integration or mobile/platform readiness follows this file.
// TerminalRetention/1: original scalar/normalized controls become permanently
// invalid before graph release. The embedded journal discards saved snapshots
// without restoring values or claiming completion; root status is Invalidated.
// The retention pool closes admission but preserves each exact invalidated
// ticket. Cancel/submit/resolve and immediate invalidate cannot release that
// retained ticket. Only the private runtime releases it after every issued
// foreign gate is Delivered or natively suppressed/Detached; Unarmed slots owe
// no drain. Armed/Delivering includes the current loss callback until it exits
// delivery. Loss alone and ExternalCallbackGate::detach alone are not suppression
// proof for scope/queue userdata. Null is a validator, Vulkan/Metal remain
// DiagnosticFailure, and WebGPU loss custody remains Prototype with no recording
// or submission execution available from this prerequisite.

// endregion original_command_native_lifecycle

// region original_command_native_cold

// CanonicalOriginalColdAdmission/1. Requirements describe a selected versioned
// lowering, not actual callback issuance or native command execution. Recording
// encoder creation and all command recording share one error-scope batch; no
// separate creation callbacks are allocated. Queue completion, submission error
// scopes and a separately owned runtime loss registration must all be charged.
// The already-armed original device loss binding is a distinct existing dependency,
// with its own authored device budget; it is not registered or charged again here.
struct OriginalCommandNativeColdRequirements {
    std::uint32_t contractVersion = 1u;
    std::uint32_t originalCommands = 0u, encoders = 0u, commandBuffers = 0u, renderPasses = 0u;
    std::uint32_t recordingScopeCallbacks = 0u, submissionScopeCallbacks = 0u, queueCallbacks = 0u;
    std::uint32_t runtimeLossCallbacks = 0u, runtimeLossRegistrations = 0u;
    std::uint32_t existingDeviceLossCallbacks = 0u, existingDeviceLossRegistrations = 0u;
    OriginalCommandLifecycleCapacityRequest capacity;
    // /3 upload work counts are positional; retained unique native bytes belong
    // to OriginalInitializedUploadJoin/1, never these aggregate work counters.
    std::uint64_t bufferUploads = 0u, textureUploads = 0u, textureUploadMips = 0u,
                  nativeCopyCommands = 0u;
    std::uint64_t bufferUploadBytes = 0u, packedTextureUploadBytes = 0u, totalUploadBytes = 0u,
                  textureUploadPixelBytes = 0u, textureWriteTexelBytes = 0u;
    OriginalCommandReadbackResultContract::Requirements readback;
    OriginalCommandReadbackMapContract::Requirements readbackMap;
};

// Independent GeneralOutput/1-2 selection shared by the original downstream
// stages. A default intent is the legacy profile; no caller may infer this
// choice from a cold owner, command version or completed output snapshot.
struct OriginalGeneralOutputIntent {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
};

class OriginalGeneralOutputPairingContract final {
public:
    using Intent = OriginalGeneralOutputIntent;

    static constexpr bool selected(const Intent& intent) noexcept
    {
        return intent.contractVersion || intent.prototypeOptIn;
    }

    static constexpr const char* validateOptional(const Intent& intent) noexcept
    {
        if (!selected(intent))
            return nullptr;
        if (intent.contractVersion != 1u && intent.contractVersion != 2u
            && intent.contractVersion != 3u)
            return "RHI-GENERAL-OUTPUT-PROFILE-VERSION";
        return intent.prototypeOptIn ? nullptr : "RHI-GENERAL-OUTPUT-PROFILE-OPT-IN";
    }

    static constexpr const char* validatePair(const Intent& expected, const Intent& actual) noexcept
    {
        if (const auto* id = validateOptional(expected))
            return id;
        if (const auto* id = validateOptional(actual))
            return id;
        return expected.contractVersion == actual.contractVersion
                && expected.prototypeOptIn == actual.prototypeOptIn
            ? nullptr
            : "RHI-GENERAL-OUTPUT-PROFILE-MISMATCH";
    }

    static constexpr const char* validateRecorded(
        const Intent& expected, std::uint32_t recordedVersion, const Intent& actual) noexcept
    {
        if (const auto* id = validatePair(expected, actual))
            return id;
        if (recordedVersion == UINT32_MAX)
            return "RHI-GENERAL-OUTPUT-PROFILE-UNRECORDED";
        return recordedVersion == actual.contractVersion
            ? nullptr
            : "RHI-GENERAL-OUTPUT-PROFILE-RECORDED-MISMATCH";
    }
};

struct OriginalCommandNativeColdBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxOriginalCommands = 0u, maxEncoders = 0u, maxCommandBuffers = 0u,
                  maxRenderPasses = 0u;
    std::uint32_t maxRecordingScopeCallbacks = 0u, maxSubmissionScopeCallbacks = 0u,
                  maxQueueCallbacks = 0u;
    std::uint32_t maxRuntimeLossCallbacks = 0u, maxRuntimeLossRegistrations = 0u,
                  maxRetainedOwners = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
    // /2 explicitly selects OrderedOriginalCopyAttachment/1. Zero disables
    // each absent category; /1 must leave these fields zero.
    std::uint32_t maxCopyElements = 0u, maxAttachmentReferences = 0u;
    // /3 explicitly selects OrderedOriginalUpload/1; /1 and /2 require zero.
    std::uint64_t maxBufferUploads = 0u, maxTextureUploads = 0u, maxTextureUploadMips = 0u,
                  maxNativeCopyCommands = 0u;
    std::uint64_t maxBufferUploadBytes = 0u, maxPackedTextureUploadBytes = 0u,
                  maxTotalUploadBytes = 0u, maxTextureUploadPixelBytes = 0u,
                  maxTextureWriteTexelBytes = 0u;
    // /4 is an independently bounded readback-only profile; all upload fields zero.
    OriginalCommandReadbackResultContract::Budget readback;
    OriginalCommandReadbackMapContract::Budget readbackMap;
    // Independent General output cold admission. Default profiles keep Join1;
    // only Cold5 plus this intent may retain GeneralOutput/Join4.
    using GeneralOutputIntent = OriginalGeneralOutputIntent;
    GeneralOutputIntent generalOutput;
};

class OriginalCommandNativeColdContract final {
public:
    using Budget = OriginalCommandNativeColdBudget;
    using Requirements = OriginalCommandNativeColdRequirements;

    // Existing profiles1-5 consume only State1/NativeClosure1. Typed metadata
    // needs a separately versioned frozen lowering, never a budget adaptation.
    static constexpr const char* validateLegacyProfiles(
        std::uint32_t stateVersion, std::uint32_t closureVersion = 1u) noexcept
    {
        return stateVersion == 1u && closureVersion == 1u
            ? nullptr
            : "RHI-ORIGINAL-NATIVE-COLD-TYPED-PROFILE-REQUIRED";
    }

    static constexpr const char* validateIntent(const Budget& b) noexcept
    {
        if (b.generalOutput.contractVersion || b.generalOutput.prototypeOptIn) {
            if (const auto* id = validateGeneralOutputIntent(b))
                return id;
        }
        if (b.contractVersion != 1u && b.contractVersion != 2u && b.contractVersion != 3u
            && b.contractVersion != 4u && b.contractVersion != 5u)
            return "RHI-ORIGINAL-NATIVE-COLD-VERSION";
        if (b.contractVersion != 4u && b.contractVersion != 5u
            && (b.readback.contractVersion || b.readback.prototypeOptIn
                || b.readback.maxReadbackCommands || b.readback.maxNativeCopyCommands
                || b.readback.maxResultDeclarations || b.readback.maxDeclaredReadbackBytes
                || b.readback.maxUniqueResultBytes || b.readback.maxCpuResultBytes))
            return "RHI-ORIGINAL-NATIVE-COLD-READBACK-PROFILE";
        if (b.contractVersion == 4u || b.contractVersion == 5u) {
            if (b.maxCopyElements || b.maxAttachmentReferences || b.maxRenderPasses)
                return "RHI-ORIGINAL-NATIVE-COLD-READBACK-PROFILE";
            if (const auto* id = OriginalCommandReadbackResultContract::validateIntent(b.readback))
                return id;
        }
        if (b.contractVersion != 5u && OriginalCommandReadbackMapContract::hasBudget(b.readbackMap))
            return "RHI-ORIGINAL-NATIVE-COLD-MAP-PROFILE";
        if (b.contractVersion == 5u)
            if (const auto* id = OriginalCommandReadbackMapContract::validateIntent(b.readbackMap))
                return id;
        if (b.contractVersion == 1u && (b.maxCopyElements || b.maxAttachmentReferences))
            return "RHI-ORIGINAL-NATIVE-COLD-PROFILE";
        if (b.contractVersion != 3u
            && (b.maxBufferUploads || b.maxTextureUploads || b.maxTextureUploadMips
                || b.maxNativeCopyCommands || b.maxBufferUploadBytes
                || b.maxPackedTextureUploadBytes || b.maxTotalUploadBytes
                || b.maxTextureUploadPixelBytes || b.maxTextureWriteTexelBytes))
            return "RHI-ORIGINAL-NATIVE-COLD-UPLOAD-PROFILE";
        if (b.contractVersion == 3u
            && ((!b.maxBufferUploads && !b.maxTextureUploads) || !b.maxNativeCopyCommands
                || !b.maxTotalUploadBytes))
            return "RHI-ORIGINAL-NATIVE-COLD-UPLOAD-BUDGET";
        if (!b.prototypeOptIn)
            return "RHI-ORIGINAL-NATIVE-COLD-OPT-IN";
        if (!b.maxOriginalCommands || !b.maxEncoders || !b.maxCommandBuffers
            || (b.contractVersion == 1u && !b.maxRenderPasses))
            return "RHI-ORIGINAL-NATIVE-COLD-COMMAND-BUDGET";
        if (!b.maxRecordingScopeCallbacks || !b.maxSubmissionScopeCallbacks || !b.maxQueueCallbacks
            || !b.maxRuntimeLossCallbacks || !b.maxRuntimeLossRegistrations)
            return "RHI-ORIGINAL-NATIVE-COLD-CALLBACK-BUDGET";
        if (!b.maxRetainedOwners || !b.maxCpuMetadataBytes)
            return "RHI-ORIGINAL-NATIVE-COLD-OWNER-CPU-BUDGET";
        return nullptr;
    }

    static constexpr const char* validateGeneralOutputIntent(const Budget& b) noexcept
    {
        if (b.generalOutput.contractVersion != 1u && b.generalOutput.contractVersion != 2u
            && b.generalOutput.contractVersion != 3u)
            return "RHI-GENERAL-OUTPUT-COLD-VERSION";
        if (!b.generalOutput.prototypeOptIn)
            return "RHI-GENERAL-OUTPUT-COLD-OPT-IN";
        if (b.contractVersion != 5u)
            return "RHI-GENERAL-OUTPUT-COLD-READBACK-MAP-PROFILE";
        return nullptr;
    }

    static constexpr const char* validateRequirements(
        const Requirements& r, const Budget& b) noexcept
    {
        if (const auto* id = validateIntent(b))
            return id;
        if (r.contractVersion != b.contractVersion)
            return "RHI-ORIGINAL-NATIVE-COLD-REQUIREMENTS-VERSION";
        if (r.contractVersion != 4u && r.contractVersion != 5u
            && (r.readback.readbackCommands || r.readback.nativeCopyCommands
                || r.readback.resultDeclarations || r.readback.declaredReadbackBytes
                || r.readback.uniqueResultBytes || r.readback.cpuResultBytes))
            return "RHI-ORIGINAL-NATIVE-COLD-READBACK-REQUIREMENTS-PROFILE";
        if (r.contractVersion == 4u || r.contractVersion == 5u) {
            if (r.renderPasses)
                return "RHI-ORIGINAL-NATIVE-COLD-READBACK-REQUIREMENTS-PROFILE";
            if (const auto* id = OriginalCommandReadbackResultContract::validateRequirements(
                    r.readback, b.readback))
                return id;
        }
        if (r.contractVersion != 3u
            && (r.bufferUploads || r.textureUploads || r.textureUploadMips || r.nativeCopyCommands
                || r.bufferUploadBytes || r.packedTextureUploadBytes || r.totalUploadBytes
                || r.textureUploadPixelBytes || r.textureWriteTexelBytes))
            return "RHI-ORIGINAL-NATIVE-COLD-UPLOAD-REQUIREMENTS-PROFILE";
        if (r.contractVersion == 3u
            && ((!r.bufferUploads && !r.textureUploads) || !r.nativeCopyCommands
                || !r.totalUploadBytes || r.bufferUploadBytes > r.totalUploadBytes
                || r.packedTextureUploadBytes != r.totalUploadBytes - r.bufferUploadBytes
                || r.textureUploadMips < r.textureUploads || r.bufferUploads > r.nativeCopyCommands
                || r.textureUploadMips > r.nativeCopyCommands - r.bufferUploads
                || (bool(r.bufferUploads) != bool(r.bufferUploadBytes))
                || (bool(r.textureUploads) != bool(r.textureUploadMips))
                || (bool(r.textureUploads) != bool(r.packedTextureUploadBytes))
                || (bool(r.textureUploads) != bool(r.textureUploadPixelBytes))
                || (bool(r.textureUploads) != bool(r.textureWriteTexelBytes))))
            return "RHI-ORIGINAL-NATIVE-COLD-UPLOAD-REQUIREMENTS";
        if (!r.originalCommands || r.encoders != 1u || r.commandBuffers != 1u
            || (r.contractVersion == 1u && r.renderPasses != 1u) || !r.recordingScopeCallbacks
            || !r.submissionScopeCallbacks || r.queueCallbacks != 1u || r.runtimeLossCallbacks != 1u
            || r.runtimeLossRegistrations != 1u || r.existingDeviceLossCallbacks != 1u
            || r.existingDeviceLossRegistrations != 1u)
            return "RHI-ORIGINAL-NATIVE-COLD-REQUIREMENTS";
        const std::uint64_t scopeCallbacks = std::uint64_t(r.recordingScopeCallbacks)
            + r.submissionScopeCallbacks + r.queueCallbacks;
        const auto originalCallbacks = scopeCallbacks + r.runtimeLossCallbacks,
                   originalRegistrations = scopeCallbacks + r.runtimeLossRegistrations;
        if (r.contractVersion != 5u
            && OriginalCommandReadbackMapContract::hasRequirements(r.readbackMap))
            return "RHI-ORIGINAL-NATIVE-COLD-MAP-REQUIREMENTS-PROFILE";
        if (r.contractVersion == 5u)
            if (const auto* id =
                    OriginalCommandReadbackMapContract::validateRequirements(r.readback,
                        originalCallbacks,
                        originalRegistrations,
                        b.readbackMap,
                        r.readbackMap))
                return id;
        const auto callbacks =
            r.contractVersion == 5u ? r.readbackMap.totalCallbackTickets : originalCallbacks;
        const auto registrations = r.contractVersion == 5u
            ? r.readbackMap.totalCallbackRegistrations
            : originalRegistrations;
        if (callbacks > UINT32_MAX || registrations > UINT32_MAX || r.capacity.contractVersion != 1u
            || !r.capacity.prototypeOptIn || r.capacity.pendingSubmissions != 1u
            || r.capacity.callbackTickets != callbacks
            || r.capacity.callbackRegistrations != registrations)
            return "RHI-ORIGINAL-NATIVE-COLD-REQUIREMENTS-CAPACITY";
        if (r.originalCommands > b.maxOriginalCommands)
            return "RHI-ORIGINAL-NATIVE-COLD-COMMAND-CAPACITY";
        if (r.encoders > b.maxEncoders || r.commandBuffers > b.maxCommandBuffers
            || r.renderPasses > b.maxRenderPasses)
            return "RHI-ORIGINAL-NATIVE-COLD-NATIVE-OBJECT-CAPACITY";
        if (r.recordingScopeCallbacks > b.maxRecordingScopeCallbacks
            || r.submissionScopeCallbacks > b.maxSubmissionScopeCallbacks
            || r.queueCallbacks > b.maxQueueCallbacks
            || r.runtimeLossCallbacks > b.maxRuntimeLossCallbacks
            || r.runtimeLossRegistrations > b.maxRuntimeLossRegistrations)
            return "RHI-ORIGINAL-NATIVE-COLD-CALLBACK-CAPACITY";
        if (r.bufferUploads > b.maxBufferUploads || r.textureUploads > b.maxTextureUploads
            || r.textureUploadMips > b.maxTextureUploadMips)
            return "RHI-ORIGINAL-NATIVE-COLD-UPLOAD-COUNT-CAPACITY";
        if (r.nativeCopyCommands > b.maxNativeCopyCommands)
            return "RHI-ORIGINAL-NATIVE-COLD-UPLOAD-NATIVE-COPY-CAPACITY";
        if (r.bufferUploadBytes > b.maxBufferUploadBytes
            || r.packedTextureUploadBytes > b.maxPackedTextureUploadBytes
            || r.totalUploadBytes > b.maxTotalUploadBytes
            || r.textureUploadPixelBytes > b.maxTextureUploadPixelBytes
            || r.textureWriteTexelBytes > b.maxTextureWriteTexelBytes)
            return "RHI-ORIGINAL-NATIVE-COLD-UPLOAD-BYTE-CAPACITY";
        return nullptr;
    }

    static constexpr const char* compareCapacity(
        const Requirements& r, const OriginalCommandLifecycleCapacityRequest& actual) noexcept
    {
        const auto& required = r.capacity;
        if (actual.contractVersion != required.contractVersion
            || actual.prototypeOptIn != required.prototypeOptIn
            || actual.pendingSubmissions != required.pendingSubmissions
            || actual.callbackTickets != required.callbackTickets
            || actual.callbackRegistrations != required.callbackRegistrations)
            return "RHI-ORIGINAL-NATIVE-COLD-RESERVATION-REQUIREMENTS";
        return nullptr;
    }
};

// endregion original_command_native_cold

// region original_command_native_cold_command

// CanonicalOriginalColdCommand/1: original device/queue connection and opaque
// metadata custody only. All owned objects allocate under original Recording
// quota before publishing a sticky one-command claim on the exact original seal.
// Failed cold allocation/connection publishes no claim. Successful destruction
// is pre-record only: this version offers no encoder/record/submit/event API.
struct OriginalCommandNativeColdCommandBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxCommandPayloads = 0u, maxRetainedOwners = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
    OriginalGeneralOutputIntent generalOutput;
};

class OriginalCommandNativeColdCommandContract final {
public:
    static constexpr std::uint32_t additionalOwnerCount =
        3u; // Opaque wrapper, shared payload, owned Impl.

    static constexpr const char* validateIntent(
        const OriginalCommandNativeColdCommandBudget& b) noexcept
    {
        if (const auto* id =
                OriginalGeneralOutputPairingContract::validateOptional(b.generalOutput))
            return id;
        if (b.contractVersion != 1u)
            return "RHI-ORIGINAL-NATIVE-COLD-COMMAND-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-ORIGINAL-NATIVE-COLD-COMMAND-OPT-IN";
        if (b.maxCommandPayloads != 1u)
            return "RHI-ORIGINAL-NATIVE-COLD-COMMAND-COUNT";
        if (!b.maxRetainedOwners || !b.maxCpuMetadataBytes)
            return "RHI-ORIGINAL-NATIVE-COLD-COMMAND-BUDGET";
        return nullptr;
    }
};

// endregion original_command_native_cold_command

// region original_command_callback_custody

// CanonicalOriginalCallbackCustody/1: cold metadata and independent native loss
// observation only. Scope/queue delivery and canonical execution stay closed.
struct OriginalCommandCallbackCustodyBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxPendingRecords = 0u, maxCallbackSlots = 0u, maxRetainedOwners = 0u;
    std::uint64_t maxCpuSubmissionBytes = 0u, maxCpuCallbackBytes = 0u;
    OriginalGeneralOutputIntent generalOutput;
};

class OriginalCommandCallbackCustodyContract final {
public:
    static constexpr std::uint32_t additionalOwnerCount =
        11u; // wrapper, state, graph, eight slots.

    static constexpr const char* validateIntent(
        const OriginalCommandCallbackCustodyBudget& b) noexcept
    {
        if (const auto* id =
                OriginalGeneralOutputPairingContract::validateOptional(b.generalOutput))
            return id;
        if (OriginalGeneralOutputPairingContract::selected(b.generalOutput)
            && (b.contractVersion != 2u || b.maxCallbackSlots != 12u))
            return "RHI-GENERAL-OUTPUT-CALLBACK-MAP-PROFILE";
        if (b.contractVersion != 1u && b.contractVersion != 2u)
            return "RHI-ORIGINAL-CALLBACK-CUSTODY-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-ORIGINAL-CALLBACK-CUSTODY-OPT-IN";
        if (b.maxPendingRecords != 1u
            || (b.contractVersion == 1u ? b.maxCallbackSlots != 8u
                                        : b.maxCallbackSlots <= 8u
                        || (b.maxCallbackSlots - 8u)
                            % OriginalCommandReadbackMapContract::participantsPerMap))
            return "RHI-ORIGINAL-CALLBACK-CUSTODY-COUNT";
        if (!b.maxRetainedOwners || !b.maxCpuSubmissionBytes || !b.maxCpuCallbackBytes)
            return "RHI-ORIGINAL-CALLBACK-CUSTODY-BUDGET";
        return nullptr;
    }

    static constexpr std::uint32_t additionalOwners(
        const OriginalCommandCallbackCustodyBudget& b) noexcept
    {
        return b.contractVersion == 1u              ? additionalOwnerCount
            : b.maxCallbackSlots <= UINT32_MAX - 3u ? 3u + b.maxCallbackSlots
                                                    : 0u;
    }
};

// endregion original_command_callback_custody

// region original_command_issuer_preparation

// CanonicalOriginalIssuerPreparation/1 is cold object storage, never proof.
struct OriginalCommandIssuerPreparationBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxRecordingIssuers = 0u, maxSubmissionIssuers = 0u, maxRetainedOwners = 0u;
    std::uint64_t maxRecordingCpuBytes = 0u, maxSubmissionCpuBytes = 0u;
    OriginalGeneralOutputIntent generalOutput;
};

class OriginalCommandIssuerPreparationContract final {
public:
    // Opaque preparation wrapper + actual existing Recording + Submission.
    // Inline evidence has no separately owned object. Existing callbacks/pool/
    // command/lowering/root are retained aliases, never created again.
    static constexpr std::uint32_t additionalOwnerCount = 3u;

    static constexpr const char* validateIntent(
        const OriginalCommandIssuerPreparationBudget& b) noexcept
    {
        if (const auto* id =
                OriginalGeneralOutputPairingContract::validateOptional(b.generalOutput))
            return id;
        if (b.contractVersion != 1u)
            return "RHI-ORIGINAL-ISSUER-PREPARATION-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-ORIGINAL-ISSUER-PREPARATION-OPT-IN";
        if (b.maxRecordingIssuers != 1u || b.maxSubmissionIssuers != 1u)
            return "RHI-ORIGINAL-ISSUER-PREPARATION-COUNT";
        if (!b.maxRetainedOwners || !b.maxRecordingCpuBytes || !b.maxSubmissionCpuBytes)
            return "RHI-ORIGINAL-ISSUER-PREPARATION-BUDGET";
        return nullptr;
    }
};

// Recording metadata/share owner charges Recording; Submission and opaque
// wrapper metadata/share owners charge Submission. Both original quotas must be
// independent and exactly match the captured admission. All fallible allocations
// precede sticky publication and encoder. Failure destroys only unpublished
// issuer candidates, preserves existing cold custody and permits cold retry.
// State retains issuers; issuers weakly reference State, so cancellation/loss
// can release Submission then Recording before lowering/graph without a cycle.
// Recording evidence is inline scope3; Submission reuses its inline queue+scope4
// evidence. Existing8 slots/Source1/8/8 remain unchanged, all seven non-loss slots
// stay Unarmed. Public wrapper exposes no proof/issuer/native handle/event API.
// Existing ordinary record/submit paths must deterministically reject canonical
// issuer mode before ordinary completion/native access. Native live validation,
// scoped encoder, MaySubmit and successful completion commit remain separate.

// endregion original_command_issuer_preparation

// region original_command_queue_submission

// CanonicalOriginalQueueSubmission/1. Separate explicit opt-in; recording is
// insufficient authority to invoke a queue or commit the original journal.
struct OriginalCommandQueueSubmissionIntent {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    OriginalGeneralOutputIntent generalOutput;
};

class OriginalCommandQueueSubmissionContract final {
public:
    static constexpr const char* validateIntent(
        const OriginalCommandQueueSubmissionIntent& intent) noexcept
    {
        if (const auto* id =
                OriginalGeneralOutputPairingContract::validateOptional(intent.generalOutput))
            return id;
        if (intent.contractVersion != 1u)
            return "RHI-ORIGINAL-QUEUE-SUBMISSION-VERSION";
        if (!intent.prototypeOptIn)
            return "RHI-ORIGINAL-QUEUE-SUBMISSION-OPT-IN";
        return nullptr;
    }
};

// Reuse the existing original Submission issuer, inline queue+scope4 evidence
// and preallocated custody slots. No new command, owner, transaction, callback
// allocation, old ordinary completion reservation or Source demand is allowed.
// Requirements are the original versioned cold lowering, not caller counts.
// Bind exact root/seal, recording proof, device/queue/finished command, native
// loss registration and non-recycled pool ticket. Revalidate owned Pending
// finals and drained recording scopes before reserving submission.
// Publish pool submitted retention and MaySubmit BEFORE any queue invocation;
// mark the command non-retryable before control crosses the native boundary.
// Never commit in pool.submitWith or reuse ordinary commit-before-queue code.
// Only proven native invocation + successful queue completion + every successful
// submission scope + all issued userdata drained + unchanged Pending finals
// authorize the existing private Submission issuer to commit through the seal.
// Unknown/thrown invocation, callback registration failure, failed completion,
// changed Pending seals, cancellation or Shutdown after MaySubmit invalidate
// and quarantine immediately. No rollback/retry/cancel of a submitted ticket.
// Retain native shell/lowering/root until genuine queue termination AND all
// issued callbacks drain/suppress, or physical loss plus userdata drain. Queue
// listener suppression alone never proves native work termination. Physical loss invalidates first; it does not
// suppress scope/queue userdata. Callback ordering must not alter the outcome.
// All hot paths allocate0, perform no blocking wait/IO and use original budgets.
// Queue completion alone proves neither this invocation nor scope success.
// Successful private commit grants no general/bindless execution or NativeSync.

// endregion original_command_queue_submission

// region original_command_scoped_recording

// CanonicalOriginalScopedRecording/1. Explicit opt-in to the already admitted
// CopyPass/1 encoder, never queue submission or completion authority.
struct OriginalCommandScopedRecordingIntent {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    OriginalGeneralOutputIntent generalOutput;
};

class OriginalCommandScopedRecordingContract final {
public:
    static constexpr const char* validateIntent(
        const OriginalCommandScopedRecordingIntent& intent) noexcept
    {
        if (const auto* id =
                OriginalGeneralOutputPairingContract::validateOptional(intent.generalOutput))
            return id;
        if (intent.contractVersion != 1u)
            return "RHI-ORIGINAL-SCOPED-RECORDING-VERSION";
        if (!intent.prototypeOptIn)
            return "RHI-ORIGINAL-SCOPED-RECORDING-OPT-IN";
        return nullptr;
    }
};

// Requirements/counts/CPU quotas come from the immutable cold admission, not
// caller guesses. No new owner, callback, command, transaction or Source demand.
// Apply the exact sealed root before encoder creation, under Internal/OOM/
// Validation scopes. CopyPass/1 consumes every projected mip copy in order,
// the normative automatic attachment transition and the exact empty render pass.
// Recorded requires finished native payload, unchanged owned Pending finals,
// all three successful scope receipts, cleared userdata and Delivered gates.
// Failure is observable immediately; root/lowering stay retained until every
// issued scope drains, then rollback once. Caller drop/Shutdown requests the
// same drain-before-rollback. Physical loss invalidates first and releases only
// after drain; loss never suppresses scope userdata. One-shot root cannot retry.
// Hot entry/delivery/cancel are allocation-free. No blocking wait or IO. Queue,
// MaySubmit, successful completion commit and NativeSync remain unavailable.
} // namespace arp::rhi

// endregion original_command_scoped_recording
// endregion Command native lifecycle validation

// region Command recording lifetime

// region original_command_recording_seal

namespace arp::rhi_webgpu {
class WebGpuNativeCommandPreparation;
class WebGpuCanonicalCallbackCustody;
class WebGpuCanonicalColdAdmission;
class WebGpuCanonicalUploadJoins;
class WebGpuOrdinaryRecording;
class WebGpuOrdinarySubmission;
class WebGpuOriginalGeometryContent;
} // namespace arp::rhi_webgpu

namespace arp::rhi_metal {
class MetalOriginalTextureUploadCommand;
class MetalOriginalGeometryContent;
} // namespace arp::rhi_metal

namespace arp::rhi_vulkan {
class VulkanOriginalBindlessCommand;
class VulkanOriginalTextureUploadCommand;
class VulkanOriginalGeometryContent;
} // namespace arp::rhi_vulkan

namespace arp::rhi_metal {
class MetalOriginalBindlessCommand;
}

namespace arp::rhi {
// CanonicalOriginalRecordingSeal/1: CPU ownership interlock. Its public API
// retains the exact root and independent cold CPU domains and issues no native
// event, encoder, proof, callback or submit. ScopedRecording/1 authorizes only
// the existing private Recording issuer to apply/rollback through this seal.
// The seal may be requested publicly because it only removes CPU authority.
// Once published, that root never returns to caller-managed CPU mutation.
// Native lowering keeps this exact seal in runtime-owned recording/completion
// storage until all issued userdata drains. QueueSubmission/1 grants only the
// existing private Submission issuer completion-validated commit authority.
class OriginalCommandReadbackMapDrainContract;

class OriginalCommandRecordingSeal final : public core::QuotaBoundSharedObjectIdentity {
    friend class arp::rhi_metal::MetalOriginalBindlessCommand;
    friend class arp::rhi_vulkan::VulkanOriginalBindlessCommand;
    friend class arp::rhi_vulkan::VulkanOriginalTextureUploadCommand;
    friend class arp::rhi_metal::MetalOriginalTextureUploadCommand;
    friend class arp::rhi_vulkan::VulkanOriginalGeometryContent;
    friend class arp::rhi_metal::MetalOriginalGeometryContent;
    friend class arp::rhi_webgpu::WebGpuOriginalGeometryContent;
    friend class OriginalCommandLifecycleReservation;
    friend class arp::rhi_webgpu::WebGpuNativeCommandPreparation;
    friend class arp::rhi_webgpu::WebGpuCanonicalCallbackCustody;
    friend class arp::rhi_webgpu::WebGpuCanonicalUploadJoins;
    friend class arp::rhi_webgpu::WebGpuOrdinaryRecording;
    friend class arp::rhi_webgpu::WebGpuOrdinarySubmission;
    using Root = OriginalCommandStateTransaction;
    using Contract = OriginalCommandNativeLifecycleContract;
    using Budget = OriginalCommandNativeLifecycleBudget;

    class ConstructionKey {
        friend class OriginalCommandRecordingSeal;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    // Root and this seal, plus independently retained Submission/Callback quotas.
    static constexpr std::uint32_t additionalOwnerCount = 3u;

    static constexpr std::uint32_t additionalOwnerCountFor(const Budget& budget) noexcept
    {
        return (OriginalCommandNativeLifecycleContract::isFenceProfile(budget.contractVersion))
            ? 2u
            : additionalOwnerCount;
    }

    OriginalCommandRecordingSeal(ConstructionKey,
        std::shared_ptr<Root> root,
        const Budget& budget,
        std::shared_ptr<core::CpuMemoryQuota> submission,
        std::shared_ptr<core::CpuMemoryQuota> callback) noexcept
        : root_(std::move(root)), budget_(budget), submission_(std::move(submission)),
          callback_(std::move(callback))
    {}

    ~OriginalCommandRecordingSeal()
    {
        // Issued callbacks retain this exact seal until drain. Pre-submit
        // destruction rolls back once; invalidated/committed roots ignore it.
        // A failed cold publication has never claimed the root.
        if (published_)
            root_->rollbackOriginalOwnership();
    }

    static std::shared_ptr<const OriginalCommandRecordingSeal> prepareOriginal(
        std::shared_ptr<Root> root,
        const GpuDevice& device,
        const Budget& budget,
        std::shared_ptr<core::CpuMemoryQuota> submission,
        std::shared_ptr<core::CpuMemoryQuota> callback,
        const char*& diagnostic)
    {
        diagnostic = Root::validateOriginal(root);
        if (diagnostic)
            return {};
        if (root->recordingSealed_) {
            diagnostic = "RHI-ORIGINAL-RECORDING-SEAL-DUPLICATE";
            return {};
        }
        if ((diagnostic = root->compareOriginalGraph(root->originalGraph(), device)))
            return {};
        if ((diagnostic = Contract::inspectOriginalPreparation(root, budget, submission, callback)))
            return {};
        const auto extra = additionalOwnerCountFor(budget);
        if (root->retainedOwnerCount() > UINT32_MAX - extra
            || budget.maxRecordingOwners - root->retainedOwnerCount() < extra) {
            diagnostic = "RHI-ORIGINAL-RECORDING-SEAL-OWNER-CAPACITY";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(root->originalMetadataQuota());
            auto unique = core::makeUnique<OriginalCommandRecordingSeal>(
                ConstructionKey{}, root, budget, std::move(submission), std::move(callback));
            auto result = shareOriginal(std::move(unique));
            // The owner and all cold allocations exist before the one-shot
            // claim is published. External serialization prevents interleaving.
            root->recordingSealed_ = true;
            result->published_ = true;
            diagnostic = nullptr;
            return result;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-ORIGINAL-RECORDING-SEAL-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-ORIGINAL-RECORDING-SEAL-ALLOCATION";
            return {};
        }
    }

    static const char* validateOriginal(
        const std::shared_ptr<const OriginalCommandRecordingSeal>& self) noexcept
    {
        return !self || !self->hasQuotaBoundSharedOwner(self) ? "RHI-ORIGINAL-RECORDING-SEAL-OWNER"
                                                              : self->validate();
    }

    const char* validate() const noexcept
    {
        if (const auto* id = validateRetainedOwnership())
            return id;
        return Contract::inspectOriginalPreparation(root_, budget_, submission_, callback_);
    }

    const char* compareOriginalRoot(const std::shared_ptr<const Root>& root) const noexcept
    {
        if (const auto* id = validate())
            return id;
        return core::SharedOwnerProjection::sameOwner(root_, root)
            ? nullptr
            : "RHI-ORIGINAL-RECORDING-SEAL-ROOT";
    }

    const auto& originalRoot() const noexcept { return root_; }

    const auto& originalBudget() const noexcept { return budget_; }

    const auto& originalSubmissionQuota() const noexcept { return submission_; }

    const auto& originalCallbackQuota() const noexcept { return callback_; }

    std::uint32_t retainedOwnerCount() const noexcept
    {
        return root_->retainedOwnerCount() + additionalOwnerCountFor(budget_);
    }

    bool nativeExecutionProven() const noexcept { return false; }

    const char* executionDiagnostic(Backend backend) const noexcept
    {
        if (const auto* id = validate())
            return id;
        return arp::rhi::semantics::OriginalCommandNativeLifecycleLowering::lowering(
            backend, budget_.contractVersion)
            .diagnostic;
    }

private:
    const char* validateGeneralOutputReadPending(
        const std::shared_ptr<const OriginalCommandRecordingSeal>& self,
        const std::shared_ptr<const Root>& upstream,
        const GpuTexture& object,
        const GpuTexture::CommittedStateObservation& frozen,
        std::uint32_t outputVersion) const noexcept
    {
        if (!self || self.get() != this || !hasQuotaBoundSharedOwner(self) || !upstream
            || !upstream->hasQuotaBoundSharedOwner(upstream))
            return "RHI-GENERAL-OUTPUT-PENDING-SEAL-OWNER";
        if (const auto* id = validateRetainedOwnership())
            return id;
        if (!lifecycleReserved_ || !nativeColdCommandClaimed_)
            return "RHI-GENERAL-OUTPUT-PENDING-SEAL-RESERVATION";
        return root_->validateGeneralOutputReadPending(*upstream, object, frozen, outputVersion);
    }

    const char* validateInitializedTexturePending(std::size_t command,
        const GpuTexture& object,
        const GpuTexture::CommittedStateObservation& state,
        const GpuTexture::ContentExpectation& content) const noexcept
    {
        if (const auto* id = validateRetainedOwnership())
            return id;
        return root_->validateInitializedTexturePending(command, object, state, content);
    }

    const char* validateInitializedBufferPending(const GpuBuffer& object,
        const GpuBuffer::CommittedStateObservation& captured,
        OriginalInitializedBufferPendingUse use = OriginalInitializedBufferPendingUse::CopySource1,
        std::uint32_t version = 1u) const noexcept
    {
        if (const auto* id = validateRetainedOwnership())
            return id;
        if (use == OriginalInitializedBufferPendingUse::ImmutableShaderBufferRead3
            && budget_.contractVersion != 4u)
            return "RHI-ORIGINAL-INITIALIZED-SHADER-BUFFER-PENDING-LIFECYCLE";
        if (use == OriginalInitializedBufferPendingUse::ImmutableBindlessShaderRead6
            && (!lifecycleReserved_ || !nativeColdCommandClaimed_
                || (!root_->hasOriginalBindlessRootShaderResourceProfile()
                    && !root_->hasOriginalBindlessRootGeometryStateProfile()
                    && !root_->hasOriginalBindlessProducedGeometryProfile())))
            return "RHI-ORIGINAL-BINDLESS-SHADER-PENDING-LIFECYCLE";
        if (use == OriginalInitializedBufferPendingUse::ImmutableBindlessGeometryRead5
            && ((budget_.contractVersion != 9u
                    && !(budget_.contractVersion == 12u
                        && root_->hasOriginalBindlessGraphicsGeometryProfile())
                    && !((budget_.contractVersion == 21u || budget_.contractVersion == 22u)
                        && root_->hasOriginalBindlessProducedGeometryProfile()))
                || !lifecycleReserved_ || !nativeColdCommandClaimed_))
            return "RHI-ORIGINAL-BINDLESS-GEOMETRY-PENDING-LIFECYCLE";
        return root_->validateInitializedBufferPending(object, captured, use, version);
    }

    const char* validateCompletedTextureReadPending(const GpuTexture& object,
        const GpuTexture::ContentExpectation& completed,
        std::uint32_t version) const noexcept
    {
        if (const auto* id = validateRetainedOwnership())
            return id;
        if (version == 4u && budget_.contractVersion != 8u)
            return "RHI-BINDLESS-COMPLETED-TEXTURE-PENDING-LIFECYCLE";
        if (version == 5u && budget_.contractVersion != 9u)
            return "RHI-BINDLESS-GRAPHICS-TEXTURE-PENDING-LIFECYCLE";
        if (version == 6u && budget_.contractVersion != 12u)
            return "RHI-BINDLESS-GRAPHICS-CANONICAL-TEXTURE-PENDING-LIFECYCLE";
        if (version == 7u && budget_.contractVersion != 9u)
            return "RHI-BINDLESS-GRAPHICS-HDR-TEXTURE-PENDING-LIFECYCLE";
        if (version == 9u && budget_.contractVersion == 20u
            && (!lifecycleReserved_ || !nativeColdCommandClaimed_))
            return "RHI-CANONICAL-GRAPHICS-DEPTH-COLOR-PENDING-CUSTODY";
        if (version == 9u && budget_.contractVersion != 12u && budget_.contractVersion != 20u)
            return "RHI-BINDLESS-GRAPHICS-CANONICAL-COLOR-PENDING-LIFECYCLE";
        if (version == 8u
            && (budget_.contractVersion != 19u || !lifecycleReserved_
                || !nativeColdCommandClaimed_))
            return "RHI-GRAPHICS-DEPTH-COLOR-PENDING-LIFECYCLE";
        if (version == 2u && budget_.contractVersion != 4u)
            return "RHI-COMPLETED-SHADER-TEXTURE-PENDING-LIFECYCLE";
        return root_->validateCompletedTextureReadPending(object, completed, version);
    }

    const char* validateCompletedDepthReadPending(
        const OriginalCompletedDepthReadInputContract::Input& input,
        std::uint32_t version = 1u) const noexcept
    {
        if (const auto* id = validateRetainedOwnership())
            return id;
        if (const auto* id =
                OriginalCompletedDepthRootReadPlanContract::validatePendingProfile(version,
                    budget_.contractVersion,
                    root_->originalContentPlanContractVersion(),
                    lifecycleReserved_,
                    nativeColdCommandClaimed_))
            return id;
        return root_->validateCompletedDepthReadPending(input, version);
    }

    // Only the existing private Recording issuer can apply/rollback. The root
    // keeps this seal as its sole friend; public CPU mutation stays refused.
    const char* applyOriginalRecordingPlan() const noexcept
    {
        if (const auto* id = validate())
            return id;
        return root_->applyOriginalPlan();
    }

    // Only the existing Submission issuer checks native invocation/evidence/drain
    // before this private commit. Public inspection never grants authority.
    const char* commitOriginalSubmission(std::uint32_t generalSubmissionVersion = 0u) const noexcept
    {
        return root_->commitOriginalNativeOwnership(generalSubmissionVersion);
    }

    // Fence issuers consume the immutable seal's profile. A sequence may retain
    // any bounded number of Roots without changing its completion semantics.
    const char* commitOriginalRootSubmission() const noexcept
    {
        if (const auto* id = validateRetainedOwnership())
            return id;
        const auto profile =
            OriginalBindlessRootCompletionProfile::inspect(budget_.contractVersion);
        if (profile.diagnostic)
            return profile.diagnostic;
        if (profile.callbacks)
            return "RHI-BINDLESS-ROOT-COMPLETION-CALLBACK-DRAIN-REQUIRED";
        return commitOriginalSubmission(profile.submissionVersion);
    }

    // Deferred canonical type avoids a command-state/transfer-validation include
    // cycle. Only the existing private native Submission issuer may call this.
    // Pure caller facts never gain access to this private completion boundary.
    template <class Drain>
    const char* commitOriginalReadbackSubmission(
        const OriginalCommandReadbackMapContract::Requirements& required,
        const typename Drain::Facts& facts) const noexcept
    {
        static_assert(std::is_same_v<Drain, OriginalCommandReadbackMapDrainContract>);
        if (const auto* id = validateRetainedOwnership())
            return id;
        if (budget_.contractVersion != 2u
            || required.totalCallbackTickets > budget_.maxCallbackTickets
            || required.totalCallbackRegistrations > budget_.maxCallbackRegistrations)
            return "RHI-ORIGINAL-CONTENT-READBACK-COMPLETION-RESERVATION";
        const auto decision = Drain::inspect(required, facts);
        if (decision.action != Drain::Action::RetireUnpublished || decision.diagnostic
            || decision.mayPublish)
            return "RHI-ORIGINAL-CONTENT-READBACK-COMPLETION-UNPROVEN";
        return root_->commitOriginalReadbackNativeOwnership();
    }

    void rollbackOriginalRecordingPlan() const noexcept { root_->rollbackOriginalOwnership(); }

    // RootCallback/11 uses the same buffer-only journal. Its native issuer must
    // prove queue, scopes, fault copy, map/unmap and every receipt drain before
    // committing. Cold /10 can never enter this private boundary.
    template <class Drain>
    const char* commitOriginalRootReadbackSubmission(
        const OriginalCommandReadbackMapContract::Requirements& required,
        const typename Drain::Facts& facts) const noexcept
    {
        static_assert(std::is_same_v<Drain, OriginalCommandReadbackMapDrainContract>);
        if (const auto* id = validateRetainedOwnership())
            return id;
        if ((budget_.contractVersion != 11u && budget_.contractVersion != 12u
                && budget_.contractVersion != 13u && budget_.contractVersion != 16u
                && budget_.contractVersion != 18u && budget_.contractVersion != 20u
                && budget_.contractVersion != 22u)
            || required.totalCallbackTickets > budget_.maxCallbackTickets
            || required.totalCallbackRegistrations > budget_.maxCallbackRegistrations)
            return "RHI-ORIGINAL-ROOT-READBACK-COMPLETION-RESERVATION";
        if (const auto* id = Contract::validateRootReadbackMapCount(
                *root_->originalGraph()->originalInputs()->originalScalars()->originalUsage(),
                required.maps))
            return id;
        const auto decision = Drain::inspect(required, facts);
        if (decision.action != Drain::Action::RetireUnpublished || decision.diagnostic
            || decision.mayPublish)
            return "RHI-ORIGINAL-ROOT-READBACK-COMPLETION-UNPROVEN";
        const auto profile =
            OriginalBindlessRootCompletionProfile::inspect(budget_.contractVersion);
        if (profile.diagnostic)
            return profile.diagnostic;
        if (!profile.callbacks)
            return "RHI-BINDLESS-ROOT-COMPLETION-FENCE-REQUIRED";
        return root_->commitOriginalNativeOwnership(profile.submissionVersion);
    }

    const char* validateRetainedOwnership() const noexcept
    {
        if (!published_ || !root_->recordingSealed_)
            return "RHI-ORIGINAL-RECORDING-SEAL-CLAIM";
        if (!core::SharedOwnerProjection::sameOwner(
                originalMetadataQuota(), root_->originalMetadataQuota()))
            return "RHI-ORIGINAL-RECORDING-SEAL-QUOTA";
        return Contract::inspectOriginalOwnership(root_, budget_, submission_, callback_);
    }

    // Terminal loss only. This grants custody no application/commit authority.
    void invalidateOriginalOwnership() const noexcept { root_->invalidateOriginalOwnership(); }

    const std::shared_ptr<Root> root_;
    const Budget budget_;
    const std::shared_ptr<core::CpuMemoryQuota> submission_, callback_;
    bool published_ = false;
    mutable bool lifecycleReserved_ = false;
    mutable bool nativeColdCommandClaimed_ = false;
    mutable bool typedCallbackCustodyClaimed_ = false;
};

// endregion original_command_recording_seal

// region original_command_lifecycle_reservation

// CanonicalOriginalLifecycleCapacity/1: cold Source capacity ownership only.
// Does not issue native work/events or grant completion. The same original seal
// retains its root/graph/journal; capacity aliases retain that original Source,
// never a separately allocated or caller-supplied admission pool. /1 has no native
// event API, so destructor release is exclusively pre-record cancellation. Native
// integration must retain this reservation through actual callbacks/quarantine.
// Publication/destruction are externally serialized per Source and seal; atomic
// count leases do not make the mutable wrapper interlocks concurrently callable.
class OriginalCommandLifecycleReservation final : public core::QuotaBoundSharedObjectIdentity {
    friend class arp::rhi_metal::MetalOriginalBindlessCommand;
    friend class arp::rhi_vulkan::VulkanOriginalBindlessCommand;
    friend class arp::rhi_vulkan::VulkanOriginalTextureUploadCommand;
    friend class arp::rhi_metal::MetalOriginalTextureUploadCommand;
    friend class arp::rhi_webgpu::WebGpuNativeCommandPreparation;
    friend class arp::rhi_webgpu::WebGpuCanonicalColdAdmission;
    using Seal = OriginalCommandRecordingSeal;
    using Source = OriginalCommandResourceSources;
    using Request = OriginalCommandLifecycleCapacityRequest;
    using Contract = OriginalCommandNativeLifecycleContract;
    enum class Domain { Submission, CallbackTicket, CallbackRegistration };

    class ConstructionKey {
        friend class OriginalCommandLifecycleReservation;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    static constexpr std::uint32_t additionalOwnerCount = 1u;

    OriginalCommandLifecycleReservation(
        ConstructionKey, std::shared_ptr<const Seal> seal, Request request) noexcept
        : seal_(std::move(seal)), request_(request),
          submission_(capacity(seal_, Domain::Submission), request.pendingSubmissions),
          callbacks_(capacity(seal_, Domain::CallbackTicket), request.callbackTickets),
          registrations_(
              capacity(seal_, Domain::CallbackRegistration), request.callbackRegistrations)
    {}

    ~OriginalCommandLifecycleReservation()
    {
        if (published_)
            seal_->lifecycleReserved_ = false;
    }

    static std::shared_ptr<const OriginalCommandLifecycleReservation> prepareOriginal(
        std::shared_ptr<const Seal> seal, const Request& request, const char*& diagnostic)
    {
        diagnostic = Seal::validateOriginal(seal);
        if (diagnostic)
            return {};
        if (seal->lifecycleReserved_) {
            diagnostic = "RHI-ORIGINAL-LIFECYCLE-CAPACITY-DUPLICATE";
            return {};
        }
        if ((diagnostic = Contract::validateCapacityRequest(request, seal->budget_)))
            return {};
        if (seal->retainedOwnerCount() == UINT32_MAX
            || seal->retainedOwnerCount() >= seal->budget_.maxSubmissionOwners) {
            diagnostic = "RHI-ORIGINAL-LIFECYCLE-CAPACITY-OWNER-BUDGET";
            return {};
        }
        if ((diagnostic = available(*seal, request)))
            return {};
        try {
            core::CpuMemoryQuotaScope scope(seal->submission_);
            auto unique = core::makeUnique<OriginalCommandLifecycleReservation>(
                ConstructionKey{}, seal, request);
            if ((diagnostic = unique->chargedDiagnostic()))
                return {};
            auto result = shareOriginal(std::move(unique));
            seal->lifecycleReserved_ = true;
            result->published_ = true;
            if ((diagnostic = result->validate()))
                return {};
            return result;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-ORIGINAL-LIFECYCLE-CAPACITY-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-ORIGINAL-LIFECYCLE-CAPACITY-ALLOCATION";
            return {};
        }
    }

    static const char* validateOriginal(
        const std::shared_ptr<const OriginalCommandLifecycleReservation>& self) noexcept
    {
        return !self || !self->hasQuotaBoundSharedOwner(self)
            ? "RHI-ORIGINAL-LIFECYCLE-CAPACITY-OWNER"
            : self->validate();
    }

    const char* validate() const noexcept
    {
        if (const auto* id = Seal::validateOriginal(seal_))
            return id;
        return validateRetainedOwnership();
    }

private:
    const char* validateRetainedOwnership() const noexcept
    {
        if (!seal_ || !seal_->hasQuotaBoundSharedOwner(seal_))
            return "RHI-ORIGINAL-RECORDING-SEAL-OWNER";
        if (const auto* id = seal_->validateRetainedOwnership())
            return id;
        if (!published_ || !seal_->lifecycleReserved_)
            return "RHI-ORIGINAL-LIFECYCLE-CAPACITY-CLAIM";
        if (const auto* id = Contract::validateCapacityRequest(request_, seal_->budget_))
            return id;
        if (!core::SharedOwnerProjection::sameOwner(originalMetadataQuota(), seal_->submission_))
            return "RHI-ORIGINAL-LIFECYCLE-CAPACITY-QUOTA";
        if (retainedOwnerCount() > seal_->budget_.maxSubmissionOwners)
            return "RHI-ORIGINAL-LIFECYCLE-CAPACITY-OWNER-BUDGET";
        return chargedDiagnostic();
    }

public:
    const char* compareOriginalSeal(const std::shared_ptr<const Seal>& seal) const noexcept
    {
        if (const auto* id = validate())
            return id;
        return core::SharedOwnerProjection::sameOwner(seal_, seal)
            ? nullptr
            : "RHI-ORIGINAL-LIFECYCLE-CAPACITY-SEAL";
    }

    const auto& originalSeal() const noexcept { return seal_; }

    const auto& originalRequest() const noexcept { return request_; }

    std::uint32_t retainedOwnerCount() const noexcept
    {
        return seal_->retainedOwnerCount() + additionalOwnerCount;
    }

    bool nativeExecutionProven() const noexcept { return false; }

    const char* executionDiagnostic(Backend backend) const noexcept
    {
        if (const auto* id = validate())
            return id;
        return arp::rhi::semantics::OriginalCommandNativeLifecycleLowering::lowering(
            backend, seal_->originalBudget().contractVersion)
            .diagnostic;
    }

private:
    static const std::shared_ptr<const Source>& sources(const Seal& seal) noexcept
    {
        return seal.originalRoot()
            ->originalGraph()
            ->originalInputs()
            ->originalScalars()
            ->originalUsage()
            ->originalSources();
    }

    static std::shared_ptr<core::AtomicCapacityBudget> capacity(
        const std::shared_ptr<const Seal>& seal, Domain domain) noexcept
    {
        const auto& source = sources(*seal);
        switch (domain) {
        case Domain::Submission:
            return {source, &source->pendingSubmissions_};
        case Domain::CallbackTicket:
            return {source, &source->callbackTickets_};
        case Domain::CallbackRegistration:
            return {source, &source->callbackRegistrations_};
        }
        return {};
    }

    static const char* available(const Seal& seal, const Request& request) noexcept
    {
        const auto& source = *sources(seal);
        if (!source.pendingSubmissions_.canAcquire(request.pendingSubmissions)) {
            source.pendingSubmissions_.recordCapacityRejection();
            return "RHI-ORIGINAL-LIFECYCLE-CAPACITY-SUBMISSION-EXHAUSTED";
        }
        if (request.callbackTickets
            && !source.callbackTickets_.canAcquire(request.callbackTickets)) {
            source.callbackTickets_.recordCapacityRejection();
            return "RHI-ORIGINAL-LIFECYCLE-CAPACITY-CALLBACK-EXHAUSTED";
        }
        if (request.callbackRegistrations
            && !source.callbackRegistrations_.canAcquire(request.callbackRegistrations)) {
            source.callbackRegistrations_.recordCapacityRejection();
            return "RHI-ORIGINAL-LIFECYCLE-CAPACITY-REGISTRATION-EXHAUSTED";
        }
        return nullptr;
    }

    const char* chargedDiagnostic() const noexcept
    {
        if (submission_.units() != request_.pendingSubmissions)
            return "RHI-ORIGINAL-LIFECYCLE-CAPACITY-SUBMISSION-EXHAUSTED";
        if (callbacks_.units() != request_.callbackTickets)
            return "RHI-ORIGINAL-LIFECYCLE-CAPACITY-CALLBACK-EXHAUSTED";
        if (registrations_.units() != request_.callbackRegistrations)
            return "RHI-ORIGINAL-LIFECYCLE-CAPACITY-REGISTRATION-EXHAUSTED";
        return nullptr;
    }

    // Leases die before the seal/root, whose original Source contains the budgets.
    const std::shared_ptr<const Seal> seal_;
    const Request request_;
    bool published_ = false;
    core::AtomicCapacityLease submission_, callbacks_, registrations_;
};
} // namespace arp::rhi

// endregion original_command_lifecycle_reservation
// endregion Command recording lifetime
