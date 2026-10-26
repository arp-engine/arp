#pragma once

#include "rhi/frontend/command_recording.h"
#include "rhi/frontend/command_shader_validation.h"
#include "rhi/frontend/command_stream_program.h"
#include "rhi/frontend/resource_leases.h"
#include "rhi/frontend/resource_texture_state_range.h"
#include "rhi/frontend/transfer_upload_content_validation.h"
#include "rhi/frontend/transfer_upload_destination_validation.h"
#include "rhi/gpu_device.h"
#include "rhi/semantics/transfer_dependency_lowering.h"

// region original_command_upload_staging
namespace arp::rhi {
// CanonicalOriginalUploadStaging/1: required initialized native bytes only.
// A declaration, caller frame/generation label or successful CPU inspection
// never proves staging initialization, registration, completion or execution.
class OriginalCommandUploadStagingContract final {
public:
    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t maxBufferBytes = 0u, maxPackedTextureBytes = 0u, maxTotalBytes = 0u;
    };

    struct Requirements {
        std::uint64_t bufferUploads = 0u, textureMips = 0u, bufferBytes = 0u,
                      packedTextureBytes = 0u, totalBytes = 0u;

        bool matches(const Requirements& rhs) const noexcept
        {
            return std::tie(bufferUploads, textureMips, bufferBytes, packedTextureBytes, totalBytes)
                == std::tie(rhs.bufferUploads,
                    rhs.textureMips,
                    rhs.bufferBytes,
                    rhs.packedTextureBytes,
                    rhs.totalBytes);
        }
    };

    struct Inspection {
        const char* diagnostic = nullptr;
        Requirements required;
    };

    // One journal entry owns a contiguous staging reservation per original
    // upload opcode, independently of mip/native-copy counts and byte budgets.
    // The original stream's version/opt-in and typed binding validation apply.
    // Metadata projection never initializes bytes or authorizes native upload.
    static const char* inspectJournalEntries(
        const std::shared_ptr<const OriginalCommandInputs>& inputs,
        const OriginalCommandStreamBudget& stream,
        std::uint64_t maxEntries,
        std::uint64_t& entries) noexcept
    {
        entries = 0u;
        std::uint64_t pending = 0u;
        const auto* diagnostic = OriginalCommandStreamProgram::visitOriginal(inputs,
            stream,
            [&](const OriginalCommandStreamProgram::Operation& operation) noexcept -> const char* {
                using Family = OriginalCommandStreamProgram::Family;
                if (operation.family != Family::BufferUpload
                    && operation.family != Family::TextureUpload)
                    return nullptr;
                if (pending == maxEntries)
                    return "RHI-ORIGINAL-UPLOAD-STAGING-JOURNAL-CAPACITY";
                ++pending;
                return nullptr;
            });
        if (!diagnostic)
            entries = pending;
        return diagnostic;
    }

    static constexpr const char* validateIntent(const Budget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-ORIGINAL-UPLOAD-STAGING-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-ORIGINAL-UPLOAD-STAGING-OPT-IN";
        if (!b.maxTotalBytes)
            return "RHI-ORIGINAL-UPLOAD-STAGING-BUDGET";
        return nullptr;
    }

    static Inspection inspect(const std::shared_ptr<const OriginalCommandInputs>& inputs,
        const OriginalCommandStreamBudget& stream,
        const Budget& budget) noexcept
    {
        Inspection result;
        if ((result.diagnostic = validateIntent(budget)))
            return result;
        Requirements pending;
        core::MemoryBudgetLedger buffer({budget.maxBufferBytes, true});
        core::MemoryBudgetLedger texture({budget.maxPackedTextureBytes, true});
        core::MemoryBudgetLedger total({budget.maxTotalBytes, true});
        result.diagnostic = OriginalCommandStreamProgram::visitOriginal(inputs,
            stream,
            [&](const OriginalCommandStreamProgram::Operation& operation) noexcept -> const char* {
                using Family = OriginalCommandStreamProgram::Family;
                if (operation.family != Family::BufferUpload
                    && operation.family != Family::TextureUpload)
                    return nullptr;
                if (operation.family == Family::BufferUpload) {
                    UploadBufferDesc fields;
                    std::get<BufferUploadArguments>(
                        inputs->descriptors()[operation.descriptorIndex].arguments)
                        .writeScalarFields(fields);
                    if (!buffer.canAllocate(fields.sizeBytes))
                        return "RHI-ORIGINAL-UPLOAD-STAGING-BUFFER-CAPACITY";
                    if (!total.canAllocate(fields.sizeBytes))
                        return "RHI-ORIGINAL-UPLOAD-STAGING-TOTAL-CAPACITY";
                    buffer.recordAllocation(fields.sizeBytes);
                    total.recordAllocation(fields.sizeBytes);
                    ++pending.bufferUploads;
                } else {
                    const auto& usage = *inputs->originalScalars()->originalUsage();
                    const auto index =
                        usage.references()[operation.firstReference + 1u].sourceIndex;
                    const auto& source =
                        std::get<std::shared_ptr<const OriginalCommandTextureUploadSource>>(
                            usage.originalSources()->sources()[index]);
                    std::uint64_t bytes = 0u;
                    if (const auto* id =
                            semantics::TextureUploadLayoutValidation::inspectPackedBytes(
                                *source->originalInput(),
                                source->originalDestinationLease()->layout(),
                                bytes))
                        return id;
                    if (!texture.canAllocate(bytes))
                        return "RHI-ORIGINAL-UPLOAD-STAGING-TEXTURE-CAPACITY";
                    if (!total.canAllocate(bytes))
                        return "RHI-ORIGINAL-UPLOAD-STAGING-TOTAL-CAPACITY";
                    texture.recordAllocation(bytes);
                    total.recordAllocation(bytes);
                    pending.textureMips += operation.elementCount;
                }
                return nullptr;
            });
        if (!result.diagnostic) {
            pending.bufferBytes = buffer.stats().allocatedBytes;
            pending.packedTextureBytes = texture.stats().allocatedBytes;
            pending.totalBytes = total.stats().allocatedBytes;
            result.required = pending;
        }
        return result;
    }
};

// endregion original_command_upload_staging

// region original_command_ordered_upload

// OrderedOriginalUpload/1 is a complete positional prerequisite. It never
// initializes a resource, mutates a root or issues a native execution grant.
class OriginalCommandOrderedUploadContract final {
public:
    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t maxTextureUploadCommands = 0u, maxNativeCopyCommands = 0u,
                      maxTextureWriteTexelBytes = 0u;
    };

    struct Requirements {
        OriginalCommandStreamProgram::Counts stream;
        OriginalCommandUploadStagingContract::Requirements staging;
        std::uint64_t textureUploadCommands = 0u, nativeCopyCommands = 0u,
                      textureWriteTexelBytes = 0u;
    };

    struct Inspection {
        const char* diagnostic = nullptr;
        std::size_t commandIndex = 0u;
        Requirements required;
    };

    static constexpr const char* validateIntent(const Budget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-ORIGINAL-ORDERED-UPLOAD-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-ORIGINAL-ORDERED-UPLOAD-OPT-IN";
        if (!b.maxNativeCopyCommands)
            return "RHI-ORIGINAL-ORDERED-UPLOAD-BUDGET";
        return nullptr;
    }

    static constexpr bool supports(CommandUsageKind kind) noexcept
    {
        return kind == CommandUsageKind::BufferUpload || kind == CommandUsageKind::TextureUpload
            || kind == CommandUsageKind::BufferBarrier
            || OriginalCommandOrderedCopyAttachmentContract::supports(kind);
    }

    static Inspection inspect(const std::shared_ptr<const OriginalCommandInputs>& inputs,
        const OriginalCommandStreamBudget& stream,
        const OriginalCommandUploadStagingContract::Budget& staging,
        const Budget& budget) noexcept
    {
        Inspection result;
        if ((result.diagnostic = validateIntent(budget)))
            return result;
        const auto program = OriginalCommandStreamProgram::inspect(inputs, stream);
        result.commandIndex = program.commandIndex;
        if ((result.diagnostic = program.diagnostic))
            return result;
        const auto initialized =
            OriginalCommandUploadStagingContract::inspect(inputs, stream, staging);
        if ((result.diagnostic = initialized.diagnostic))
            return result;
        Requirements pending;
        pending.stream = program.counts;
        pending.staging = initialized.required;
        core::MemoryBudgetLedger copies({budget.maxNativeCopyCommands, true}),
            texels({budget.maxTextureWriteTexelBytes, true});
        result.diagnostic = OriginalCommandStreamProgram::visitOriginal(inputs,
            stream,
            [&](const OriginalCommandStreamProgram::Operation& operation) noexcept -> const char* {
                result.commandIndex = operation.commandIndex;
                if (!supports(operation.kind))
                    return "RHI-ORIGINAL-ORDERED-UPLOAD-COMMAND";
                using F = OriginalCommandStreamProgram::Family;
                if (operation.family == F::Copy || operation.family == F::BufferUpload
                    || operation.family == F::TextureUpload) {
                    if (!copies.canAllocate(operation.elementCount))
                        return "RHI-ORIGINAL-ORDERED-UPLOAD-NATIVE-COPY-CAPACITY";
                    copies.recordAllocation(operation.elementCount);
                }
                if (operation.family == F::TextureUpload) {
                    if (pending.textureUploadCommands == budget.maxTextureUploadCommands)
                        return "RHI-ORIGINAL-ORDERED-UPLOAD-TEXTURE-COMMAND-CAPACITY";
                    const auto& usage = *inputs->originalScalars()->originalUsage();
                    const auto index =
                        usage.references()[operation.firstReference + 1u].sourceIndex;
                    const auto& source =
                        std::get<std::shared_ptr<const OriginalCommandTextureUploadSource>>(
                            usage.originalSources()->sources()[index]);
                    const auto& desc = source->originalDestinationLease()->originalObject()->desc();
                    if (const auto* id = semantics::OriginalTextureContentUploadValidation::inspect(
                            *source->originalInput(), desc))
                        return id;
                    std::uint64_t bytes = 0u;
                    if (const auto* id =
                            semantics::TextureUploadDestinationValidation::validate(desc, bytes))
                        return id;
                    if (!texels.canAllocate(bytes))
                        return "RHI-ORIGINAL-ORDERED-UPLOAD-TEXEL-WORK-CAPACITY";
                    texels.recordAllocation(bytes);
                    ++pending.textureUploadCommands;
                }
                return nullptr;
            });
        if (result.diagnostic)
            return result;
        if (!pending.stream.bufferUploads && !pending.textureUploadCommands) {
            result.diagnostic = "RHI-ORIGINAL-ORDERED-UPLOAD-MISSING";
            return result;
        }
        pending.nativeCopyCommands = copies.stats().allocatedBytes;
        pending.textureWriteTexelBytes = texels.stats().allocatedBytes;
        result.commandIndex = program.commandIndex;
        result.required = pending;
        return result;
    }

    static const char* bufferTransition(const ResourceBarrierDesc& desc) noexcept
    {
        return semantics::TransferDependencyLowering::validateTransitionIntent(
            desc, desc.before, false);
    }
};
} // namespace arp::rhi

// endregion original_command_ordered_upload

// region original_command_readback_join

namespace arp::rhi {
// CanonicalOriginalReadbackJoin/1 freezes native facts only. It neither chooses
// a substitute destination nor grants recording, completion, map or publication.
class OriginalCommandReadbackJoinContract final {
public:
    using GeneralOutputIntent = OriginalGeneralOutputIntent;

    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint32_t maxReadbacks = 0u, maxNativeSources = 0u, maxNativeDestinations = 0u,
                      maxRetainedOwners = 0u;
        std::uint64_t maxSourceResourceBytes = 0u, maxStagingResourceBytes = 0u,
                      maxCpuMetadataBytes = 0u;
        OriginalCommandStreamBudget stream;
        OriginalCommandReadbackResultContract::Budget result;
        // Join4 is standalone ordinary output readback. A completed General
        // association is authenticated by the private join, never this intent.
        GeneralOutputIntent generalOutput;
    };

    static constexpr const char* validateIntent(const Budget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-ORIGINAL-READBACK-JOIN-VERSION";
        if (b.generalOutput.contractVersion || b.generalOutput.prototypeOptIn)
            return "RHI-ORIGINAL-READBACK-JOIN-GENERAL-OUTPUT-PROFILE";
        if (!b.prototypeOptIn)
            return "RHI-ORIGINAL-READBACK-JOIN-OPT-IN";
        if (!b.maxReadbacks || !b.maxNativeSources || !b.maxNativeDestinations
            || !b.maxRetainedOwners || !b.maxSourceResourceBytes || !b.maxStagingResourceBytes
            || !b.maxCpuMetadataBytes)
            return "RHI-ORIGINAL-READBACK-JOIN-BUDGET";
        if (const auto* id = OriginalCommandStreamContract::validateIntent(b.stream))
            return id;
        return OriginalCommandReadbackResultContract::validateIntent(b.result);
    }

    static constexpr const char* validateGeneralOutputIntent(const Budget& b) noexcept
    {
        if (b.contractVersion != 4u)
            return "RHI-GENERAL-OUTPUT-READBACK-JOIN-VERSION";
        if (b.generalOutput.contractVersion != 1u && b.generalOutput.contractVersion != 2u
            && b.generalOutput.contractVersion != 3u)
            return "RHI-GENERAL-OUTPUT-READBACK-VERSION";
        if (!b.generalOutput.prototypeOptIn)
            return "RHI-GENERAL-OUTPUT-READBACK-OPT-IN";
        if (b.stream.contractVersion != 1u || b.result.contractVersion != 1u)
            return "RHI-GENERAL-OUTPUT-READBACK-TRANSFER-PROFILE";
        auto original = b;
        original.contractVersion = 1u;
        original.generalOutput = {};
        return validateIntent(original);
    }

    static constexpr bool generalOutputProfileMatches(const Budget& b,
        std::uint32_t stateVersion,
        std::uint32_t inputsVersion,
        std::uint32_t contentVersion,
        std::uint32_t sourceVersion,
        std::uint32_t correspondenceVersion) noexcept
    {
        if (validateGeneralOutputIntent(b))
            return false;
        return generalOutputProfileVersionsMatch(b.generalOutput.contractVersion,
            stateVersion,
            inputsVersion,
            contentVersion,
            sourceVersion,
            correspondenceVersion);
    }

    static constexpr bool generalOutputProfileVersionsMatch(std::uint32_t outputVersion,
        std::uint32_t stateVersion,
        std::uint32_t inputsVersion,
        std::uint32_t contentVersion,
        std::uint32_t sourceVersion,
        std::uint32_t correspondenceVersion) noexcept
    {
        // The attachment owns a range content control. Its independent reader
        // must prove ContentPlan3 readback coverage; storage versions1/2 retain
        // their existing exclusive-content reader profile.
        if (outputVersion == 3u)
            return stateVersion == 1u && inputsVersion == 1u && contentVersion == 3u
                && sourceVersion == 2u && correspondenceVersion == 1u;
        return (outputVersion == 1u || outputVersion == 2u)
            && generalOutputProfileMatches(
                stateVersion, inputsVersion, contentVersion, sourceVersion, correspondenceVersion);
    }

    static constexpr bool generalOutputProfileMatches(std::uint32_t stateVersion,
        std::uint32_t inputsVersion,
        std::uint32_t contentVersion,
        std::uint32_t sourceVersion,
        std::uint32_t correspondenceVersion) noexcept
    {
        return stateVersion == 1u && inputsVersion == 1u && contentVersion == 0u
            && sourceVersion == 2u && correspondenceVersion == 1u;
    }

    static const char* validateGeneralOutputSource(
        const std::shared_ptr<const OriginalCommandTextureReadbackSource>&,
        std::uint32_t outputVersion = 1u) noexcept;

    static const char* validateGeneralOutputDependency(const ResourceBarrierDesc& desc,
        ResourceState current,
        const TextureSubresourceRange& wholeRange,
        std::uint32_t outputVersion = 1u) noexcept
    {
        return OriginalGeneralOutputReadbackDependencyContract::validate(
            desc, current, wholeRange, outputVersion);
    }

    static const char* validateGeneralOutputCommand(const OriginalCommandInputs& inputs,
        std::size_t commandIndex,
        const OriginalCommandTextureReadbackSource& source,
        std::uint32_t outputVersion = 1u) noexcept
    {
        const auto& usage = *inputs.originalScalars()->originalUsage();
        if (commandIndex >= usage.commands().size()
            || usage.commands()[commandIndex].kind != CommandUsageKind::TextureReadback)
            return "RHI-GENERAL-OUTPUT-READBACK-COMMAND";
        const auto& texture = source.originalSource()->originalLease()->originalTextureOwner();
        const auto& range = source.originalInput()->scalarFields().range();
        // The most recent explicit transition to this exact original resource
        // must be the authored shader-write dependency, never inferred from use.
        for (std::size_t i = commandIndex; i != 0u;) {
            --i;
            if (usage.commands()[i].kind != CommandUsageKind::TextureBarrier)
                continue;
            const auto* value = inputs.descriptorAt(i);
            const auto* dependency =
                value ? std::get_if<ResourceDependencyArguments>(value) : nullptr;
            ResourceBarrierDesc barrier;
            if (!dependency || !dependency->write(CommandUsageKind::TextureBarrier, barrier))
                return "RHI-GENERAL-OUTPUT-READBACK-DEPENDENCY-ARGUMENTS";
            if (barrier.resource != texture->resource())
                continue;
            const auto expected =
                outputVersion == 3u ? ResourceState::ColorAttachment : ResourceState::ShaderWrite;
            return validateGeneralOutputDependency(barrier, expected, range, outputVersion);
        }
        return "RHI-GENERAL-OUTPUT-READBACK-DEPENDENCY-MISSING";
    }

    static OriginalCommandReadbackResultContract::Inspection inspectGeneralOutput(
        const std::shared_ptr<OriginalCommandStateTransaction>& root, const Budget& b) noexcept
    {
        OriginalCommandReadbackResultContract::Inspection result;
        if ((result.diagnostic = validateGeneralOutputIntent(b)))
            return result;
        if ((result.diagnostic = OriginalCommandStateTransaction::validateOriginal(root)))
            return result;
        if (root->status() != OriginalCommandStateTransaction::Status::Prepared
            || !root->hasOriginalPrototypePlan() || !root->journalEmpty()) {
            result.diagnostic = "RHI-GENERAL-OUTPUT-READBACK-PREPARED-ROOT";
            return result;
        }
        const auto& graph = root->originalGraph();
        const auto& inputs = graph->originalInputs();
        const auto& sources = inputs->originalScalars()->originalUsage()->originalSources();
        if (const auto* id = sources->retainedReadbackCorrespondence().validate()) {
            result.diagnostic = id;
            return result;
        }
        std::uint64_t readbacks = 0u;
        for (const auto& source : sources->sources()) {
            const auto* readback =
                std::get_if<std::shared_ptr<const OriginalCommandTextureReadbackSource>>(&source);
            if (!readback)
                continue;
            if ((result.diagnostic =
                        validateGeneralOutputSource(*readback, b.generalOutput.contractVersion)))
                return result;
            if (!generalOutputProfileMatches(b,
                    graph->stateContractVersion(),
                    inputs->originalBudget().contractVersion,
                    root->originalContentPlanContractVersion(),
                    (*readback)->contractVersion(),
                    sources->retainedReadbackCorrespondence().contractVersion)) {
                result.diagnostic = "RHI-GENERAL-OUTPUT-READBACK-SOURCE-PROFILE";
                return result;
            }
            if (++readbacks > b.maxReadbacks) {
                result.diagnostic = "RHI-GENERAL-OUTPUT-READBACK-SOURCE-CAPACITY";
                return result;
            }
        }
        if (!readbacks) {
            result.diagnostic = "RHI-GENERAL-OUTPUT-READBACK-SOURCE-MISSING";
            return result;
        }
        const auto& usage = *inputs->originalScalars()->originalUsage();
        for (std::size_t i = 0u; i < usage.commands().size(); ++i) {
            const auto& command = usage.commands()[i];
            if (command.kind != CommandUsageKind::TextureReadback)
                continue;
            const auto& source =
                std::get<std::shared_ptr<const OriginalCommandTextureReadbackSource>>(sources
                        ->sources()[usage.references()[command.firstReference + 1u].sourceIndex]);
            if ((result.diagnostic = validateGeneralOutputCommand(
                     *inputs, i, *source, b.generalOutput.contractVersion)))
                return result;
        }
        return OriginalCommandReadbackResultContract::inspect(inputs, b.stream, b.result);
    }

    static constexpr const char* destinationIntent(const BufferResourceLayout& d) noexcept
    {
        return d.usage == ResourceUsage::CopyDst && d.memory == MemoryUsage::GpuToCpu
            ? nullptr
            : "RHI-ORIGINAL-READBACK-JOIN-MAP-INTENT";
    }

    // /2 joins State3/Inputs4/Stream3; /3 composes State4/Inputs5/Stream4
    // geometry with readback under the exact Prepared root and ContentPlan3.
    // Both grant cold custody only. /1 remains
    // transfer-only and cannot consume retained Source2 or mixed Result2.
    static constexpr const char* validateProgramIntent(const Budget& b) noexcept
    {
        if (b.contractVersion != 2u && b.contractVersion != 3u)
            return "RHI-ORIGINAL-READBACK-PROGRAM-JOIN-VERSION";
        if (b.stream.contractVersion != (b.contractVersion == 3u ? 4u : 3u))
            return "RHI-ORIGINAL-READBACK-PROGRAM-JOIN-STREAM-PROFILE";
        if (const auto* id = OriginalCommandReadbackResultContract::validateProgramIntent(b.result))
            return id;
        if (b.contractVersion == 3u && b.stream.geometryUsage.drawConsumptionEmpty())
            return "RHI-ORIGINAL-READBACK-PROGRAM-JOIN-GEOMETRY-REQUIRED";
        auto original = b;
        original.contractVersion = 1u;
        original.result.contractVersion = 1u;
        return validateIntent(original);
    }

    static constexpr bool programProfileMatches(const Budget& b,
        std::uint32_t stateVersion,
        std::uint32_t inputsVersion,
        std::uint32_t contentVersion) noexcept
    {
        return contentVersion == 3u
            && (b.contractVersion == 2u
                    ? stateVersion == 3u && inputsVersion == 4u && b.stream.contractVersion == 3u
                    : b.contractVersion == 3u && stateVersion == 4u && inputsVersion == 5u
                        && b.stream.contractVersion == 4u);
    }

    static OriginalCommandReadbackResultContract::Inspection inspectProgram(
        const std::shared_ptr<OriginalCommandStateTransaction>& root,
        const std::shared_ptr<const OriginalCommandStreamProgram>& program,
        const Budget& b) noexcept
    {
        using Root = OriginalCommandStateTransaction;
        OriginalCommandReadbackResultContract::Inspection result;
        if ((result.diagnostic = validateProgramIntent(b)))
            return result;
        if ((result.diagnostic = Root::validateOriginal(root)))
            return result;
        if (root->status() != Root::Status::Prepared || !root->hasOriginalPrototypePlan()
            || !root->journalEmpty()) {
            result.diagnostic = "RHI-ORIGINAL-READBACK-PROGRAM-JOIN-ROOT-STATUS";
            return result;
        }
        if (!programProfileMatches(b,
                root->originalGraph()->stateContractVersion(),
                root->originalGraph()->originalInputs()->originalBudget().contractVersion,
                root->originalContentPlanContractVersion())) {
            result.diagnostic = "RHI-ORIGINAL-READBACK-PROGRAM-JOIN-CONTENT-PROFILE";
            return result;
        }
        if ((result.diagnostic = OriginalCommandStreamProgram::validateOriginal(program)))
            return result;
        if ((result.diagnostic = program->compareOriginalStates(root->originalGraph())))
            return result;
        const auto& inputs = root->originalGraph()->originalInputs();
        if ((result.diagnostic = program->compareOriginalInputs(inputs)))
            return result;
        const auto projected =
            OriginalCommandStreamProgram::inspectTyped(root->originalGraph(), b.stream);
        if ((result.diagnostic = projected.diagnostic))
            return result;
        if (!program->counts().matches(projected.counts)) {
            result.diagnostic = "RHI-ORIGINAL-READBACK-PROGRAM-JOIN-STREAM-COUNTS";
            return result;
        }
        return OriginalCommandReadbackResultContract::inspectProgram(program, inputs, b.result);
    }
};
} // namespace arp::rhi

// endregion original_command_readback_join

// region original_command_upload_join
namespace arp::rhi {
// OriginalInitializedUploadJoin/1: cold exact-root ownership only. Resource
// bytes count unique native allocation tokens, separately for staging/targets.
struct OriginalCommandUploadJoinBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxUploads = 0u, maxNativeSources = 0u, maxNativeDestinations = 0u,
                  maxRetainedOwners = 0u;
    std::uint64_t maxStagingResourceBytes = 0u, maxDestinationResourceBytes = 0u,
                  maxCpuMetadataBytes = 0u;
};

class OriginalCommandUploadJoinContract final {
public:
    static constexpr const char* validateIntent(const OriginalCommandUploadJoinBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-ORIGINAL-UPLOAD-JOIN-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-ORIGINAL-UPLOAD-JOIN-OPT-IN";
        if (!b.maxUploads || !b.maxNativeSources || !b.maxNativeDestinations || !b.maxRetainedOwners
            || !b.maxStagingResourceBytes || !b.maxDestinationResourceBytes
            || !b.maxCpuMetadataBytes)
            return "RHI-ORIGINAL-UPLOAD-JOIN-BUDGET";
        return nullptr;
    }
};
} // namespace arp::rhi

// endregion original_command_upload_join

// region original_command_readback_map_drain

namespace arp::rhi {
// ReadbackMapDrain/1 is a pure lifetime validator, never an authority issuer.
// Native runtime must obtain each fact from the same original State/Graph and
// genuine receipts. Availability comes from the separate lowering contract;
// a decision never grants issuer or snapshot publication authority.
class OriginalCommandReadbackMapDrainContract final {
public:
    struct Facts {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t mapsIssued = 0u, mapCallbacks = 0u, copiedMaps = 0u, safeUnmaps = 0u;
        std::uint64_t scopeCallbacksDrained = 0u, successfulScopes = 0u;
        bool maySubmit = false, submissionProven = false, queueLifetimeProven = false,
             originalForeignCallbacksDrained = false;
        bool cancellation = false, nativeError = false, invocationUncertain = false,
             nativeLoss = false;
    };
    enum class Action { RetainOriginal, InvalidateAndRetain, RetireUnpublished, RetireTerminal };

    struct Decision {
        Action action = Action::RetainOriginal;
        const char* diagnostic = "RHI-ORIGINAL-READBACK-MAP-DRAIN-PENDING";
        bool mayPublish = false;
    };

    static constexpr Decision inspect(
        const OriginalCommandReadbackMapContract::Requirements& r, const Facts& f) noexcept
    {
        using Map = OriginalCommandReadbackMapContract;
        if (f.contractVersion != 1u)
            return {Action::RetainOriginal, "RHI-ORIGINAL-READBACK-MAP-DRAIN-VERSION"};
        if (!f.prototypeOptIn)
            return {Action::RetainOriginal, "RHI-ORIGINAL-READBACK-MAP-DRAIN-OPT-IN"};
        if (!r.maps || r.maps > UINT64_MAX / Map::participantsPerMap || r.mapCallbacks != r.maps
            || r.scopeCallbacks != r.maps * Map::scopeCallbacksPerMap
            || r.totalCallbackTickets < r.maps * Map::participantsPerMap
            || r.totalCallbackRegistrations < r.maps * Map::participantsPerMap)
            return {Action::RetainOriginal, "RHI-ORIGINAL-READBACK-MAP-DRAIN-REQUIREMENTS"};
        if (f.mapsIssued > r.maps || f.mapCallbacks > f.mapsIssued || f.copiedMaps > f.mapCallbacks
            || f.safeUnmaps > f.mapCallbacks
            || f.scopeCallbacksDrained > f.mapsIssued * Map::scopeCallbacksPerMap
            || f.successfulScopes > f.scopeCallbacksDrained
            || (f.submissionProven && (!f.maySubmit || !f.queueLifetimeProven))
            || (f.mapsIssued && !f.submissionProven))
            return {Action::RetainOriginal, "RHI-ORIGINAL-READBACK-MAP-DRAIN-FACTS"};
        const bool terminal =
            f.cancellation || f.nativeError || f.invocationUncertain || f.nativeLoss;
        const bool safe = f.originalForeignCallbacksDrained && f.mapCallbacks == f.mapsIssued
            && f.safeUnmaps == f.mapsIssued
            && f.scopeCallbacksDrained == f.mapsIssued * Map::scopeCallbacksPerMap
            && (!f.maySubmit || f.queueLifetimeProven || f.nativeLoss);
        if (terminal)
            return {safe ? Action::RetireTerminal : Action::InvalidateAndRetain,
                safe ? nullptr : "RHI-ORIGINAL-READBACK-MAP-DRAIN-TERMINAL-PENDING"};
        if (!safe)
            return {};
        if (!f.submissionProven || f.mapsIssued != r.maps || f.copiedMaps != r.maps
            || f.successfulScopes != r.scopeCallbacks)
            return {
                Action::InvalidateAndRetain, "RHI-ORIGINAL-READBACK-MAP-DRAIN-EVIDENCE-UNPROVEN"};
        return {Action::RetireUnpublished, nullptr};
    }
};
} // namespace arp::rhi

// endregion original_command_readback_map_drain

// region original_command_readback_snapshot_publication
namespace arp::rhi {
// CanonicalSnapshotPublication/1 is a pure validator, never a proof issuer or
// writer. Only the existing private Submission issuer may consume actual State
// evidence and freeze the exact independent snapshots after original commit.
class OriginalCommandReadbackSnapshotPublicationContract final {
public:
    struct Intent {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
    };

    struct BindingFacts {
        std::uint64_t validatedOriginalRequests = 0u, validatedIndependentSnapshots = 0u,
                      unpublishedSnapshots = 0u;
        bool originalSealAndRootCommitted = false, originalJournalEmpty = false;
        bool exactOriginalBindings = false, independentResultQuota = false,
             terminalObserved = false;
    };
    enum class Action { Reject, FreezeIndependentSnapshots };

    struct Decision {
        Action action = Action::Reject;
        const char* diagnostic = "RHI-ORIGINAL-SNAPSHOT-PUBLICATION-UNPROVEN";
    };

    static constexpr const char* validateIntent(const Intent& intent) noexcept
    {
        if (intent.contractVersion != 1u)
            return "RHI-ORIGINAL-SNAPSHOT-PUBLICATION-VERSION";
        if (!intent.prototypeOptIn)
            return "RHI-ORIGINAL-SNAPSHOT-PUBLICATION-OPT-IN";
        return nullptr;
    }

    static constexpr Decision inspect(
        const OriginalCommandReadbackMapContract::Requirements& required,
        const OriginalCommandReadbackMapDrainContract::Facts& drain,
        const Intent& intent,
        const BindingFacts& bindings) noexcept
    {
        if (const auto* id = validateIntent(intent))
            return {Action::Reject, id};
        const auto ready = OriginalCommandReadbackMapDrainContract::inspect(required, drain);
        if (ready.action != OriginalCommandReadbackMapDrainContract::Action::RetireUnpublished
            || ready.diagnostic || ready.mayPublish)
            return {Action::Reject, "RHI-ORIGINAL-SNAPSHOT-PUBLICATION-MAP-DRAIN"};
        if (bindings.terminalObserved)
            return {Action::Reject, "RHI-ORIGINAL-SNAPSHOT-PUBLICATION-TERMINAL"};
        if (!bindings.originalSealAndRootCommitted || !bindings.originalJournalEmpty)
            return {Action::Reject, "RHI-ORIGINAL-SNAPSHOT-PUBLICATION-COMMIT"};
        if (!bindings.exactOriginalBindings || !bindings.independentResultQuota
            || bindings.validatedOriginalRequests != required.maps
            || bindings.validatedIndependentSnapshots != required.maps
            || bindings.unpublishedSnapshots != required.maps)
            return {Action::Reject, "RHI-ORIGINAL-SNAPSHOT-PUBLICATION-BINDING"};
        return {Action::FreezeIndependentSnapshots, nullptr};
    }
};
} // namespace arp::rhi

// endregion original_command_readback_snapshot_publication

// region original_command_initialized_buffer_staging

namespace arp::rhi {
// InitializedCanonicalBufferStaging/1. One exact registered buffer is adopted,
// its entire content is frozen and initialized once. GPU allocation stays with
// the original device; these are per-source retention bounds, never a fake heap.
class OriginalCommandInitializedBufferStagingContract final {
public:
    static constexpr std::uint32_t scopeCallbacks = 3u, queueCallbacks = 1u, lossCallbacks = 1u;
    static constexpr std::uint32_t callbackCount = scopeCallbacks + queueCallbacks + lossCallbacks;
    // Wrapper + state + lease/object + native allocation/seal/payload + five
    // callback slots + original domain/loss subscription. CPU quotas are separate.
    static constexpr std::uint32_t retainedOwnerBound = 2u
        + OriginalCommandBufferLease::retainedOwnerCount + 3u + callbackCount
        + OriginalDeviceNativeLossObservation::retainedOwnerCount;

    // Independent ImmutableOriginalIndexContent/1 opt-in. /2 initializes the
    // same adopted buffer; it does not authorize original draw or submission.
    struct IndexContentIntent {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        IndexFormat format = IndexFormat::Uint16;
        core::UnsignedElementSpan::ByteOrder byteOrder =
            core::UnsignedElementSpan::ByteOrder::LittleEndian;
        std::uint64_t maxAnalysisIndices = 0u;
        // ImmutableOriginalIndexShaderRead/1: independent readonly descriptor
        // intent. It never grants shader writes or command execution.
        std::uint32_t shaderReadContractVersion = 0u;
        bool shaderReadOptIn = false;

        constexpr bool empty() const noexcept
        {
            return !contractVersion && !prototypeOptIn && format == IndexFormat::Uint16
                && byteOrder == core::UnsignedElementSpan::ByteOrder::LittleEndian
                && !maxAnalysisIndices && !shaderReadContractVersion && !shaderReadOptIn;
        }

        constexpr const char* diagnostic() const noexcept
        {
            if (contractVersion != 1u)
                return "RHI-INITIALIZED-INDEX-CONTENT-VERSION";
            if (!prototypeOptIn)
                return "RHI-INITIALIZED-INDEX-CONTENT-OPT-IN";
            if (!IndexInputConsumptionContract::elementBytes(format))
                return "RHI-INDEX-INPUT-FORMAT-UNKNOWN";
            if (byteOrder != core::UnsignedElementSpan::ByteOrder::LittleEndian)
                return "RHI-INITIALIZED-INDEX-BYTE-ORDER";
            if (shaderReadContractVersion || shaderReadOptIn) {
                if (shaderReadContractVersion != 1u)
                    return "RHI-INITIALIZED-INDEX-SHADER-READ-VERSION";
                if (!shaderReadOptIn)
                    return "RHI-INITIALIZED-INDEX-SHADER-READ-OPT-IN";
            }
            return maxAnalysisIndices ? nullptr : "RHI-INITIALIZED-INDEX-ANALYSIS-BUDGET";
        }
    };

    struct VertexContentIntent {
        static constexpr auto scalarBytes =
            VertexInputConsumptionContract::formatBytes(VertexFormat::Float32x1);
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint32_t strideBytes = 0u;
        VertexInputRate rate = VertexInputRate::Vertex;
        std::uint64_t elementCount = 0u;
        core::UnsignedElementSpan::ByteOrder byteOrder =
            core::UnsignedElementSpan::ByteOrder::LittleEndian;

        constexpr bool empty() const noexcept
        {
            return !contractVersion && !prototypeOptIn && !strideBytes
                && rate == VertexInputRate::Vertex && !elementCount
                && byteOrder == core::UnsignedElementSpan::ByteOrder::LittleEndian;
        }

        constexpr const char* diagnostic() const noexcept
        {
            if (contractVersion != 1u)
                return "RHI-INITIALIZED-VERTEX-CONTENT-VERSION";
            if (!prototypeOptIn)
                return "RHI-INITIALIZED-VERTEX-CONTENT-OPT-IN";
            if (!strideBytes || strideBytes % scalarBytes)
                return "RHI-INITIALIZED-VERTEX-CONTENT-STRIDE";
            if (rate != VertexInputRate::Vertex && rate != VertexInputRate::Instance)
                return "RHI-INITIALIZED-VERTEX-CONTENT-RATE";
            if (!elementCount || elementCount > UINT64_MAX / strideBytes)
                return "RHI-INITIALIZED-VERTEX-CONTENT-COUNT";
            return byteOrder == core::UnsignedElementSpan::ByteOrder::LittleEndian
                ? nullptr
                : "RHI-INITIALIZED-VERTEX-BYTE-ORDER";
        }
    };

    struct ShaderContentIntent {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        DescriptorType type = DescriptorType::UniformBuffer;
        std::uint64_t bindingBytes = 0u;

        constexpr bool empty() const noexcept
        {
            return !contractVersion && !prototypeOptIn && type == DescriptorType::UniformBuffer
                && !bindingBytes;
        }

        constexpr const char* diagnostic() const noexcept
        {
            if (contractVersion != 1u)
                return "RHI-INITIALIZED-SHADER-CONTENT-VERSION";
            if (!prototypeOptIn)
                return "RHI-INITIALIZED-SHADER-CONTENT-OPT-IN";
            if (type != DescriptorType::UniformBuffer
                && type != DescriptorType::ReadOnlyStorageBuffer)
                return "RHI-INITIALIZED-SHADER-CONTENT-TYPE";
            return bindingBytes ? nullptr : "RHI-INITIALIZED-SHADER-CONTENT-SIZE";
        }
    };

    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint32_t maxNativeBuffers = 0u, maxCallbacks = 0u, maxRetainedOwners = 0u;
        std::uint64_t maxRetainedNativeBytes = 0u, maxCpuMetadataBytes = 0u,
                      maxCpuPayloadBytes = 0u;
        std::uint64_t maxCpuResourceBytes = 0u, maxCpuLeaseBytes = 0u, maxCpuCallbackBytes = 0u;
        IndexContentIntent indexContent;
        // /3 independent immutable vertex/instance initialization. The accepted
        // pipeline remains the authority for attributes and actual draw fetch.
        VertexContentIntent vertexContent;
        // /4 independently initializes immutable whole shader buffers. Shader
        // ABI/ranges are still verified by the original descriptor/pipeline.
        ShaderContentIntent shaderContent;
    };

    static constexpr const char* validateIntent(const Budget& b) noexcept
    {
        if (b.contractVersion < 1u || b.contractVersion > 4u)
            return "RHI-INITIALIZED-CANONICAL-STAGING-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-INITIALIZED-CANONICAL-STAGING-OPT-IN";
        if (b.contractVersion == 2u) {
            if (const auto* id = b.indexContent.diagnostic())
                return id;
        } else if (!b.indexContent.empty())
            return "RHI-INITIALIZED-CANONICAL-STAGING-INDEX-SCHEMA";
        if (b.contractVersion == 3u) {
            if (const auto* id = b.vertexContent.diagnostic())
                return id;
        } else if (!b.vertexContent.empty())
            return "RHI-INITIALIZED-CANONICAL-STAGING-VERTEX-SCHEMA";
        if (b.contractVersion == 4u) {
            if (const auto* id = b.shaderContent.diagnostic())
                return id;
            if (b.shaderContent.bindingBytes > b.maxCpuPayloadBytes
                || b.shaderContent.bindingBytes > b.maxRetainedNativeBytes)
                return "RHI-INITIALIZED-SHADER-CONTENT-SIZE";
        } else if (!b.shaderContent.empty())
            return "RHI-INITIALIZED-CANONICAL-STAGING-SHADER-SCHEMA";
        if (b.maxNativeBuffers != 1u || b.maxCallbacks < callbackCount
            || b.maxRetainedOwners < retainedOwnerBound)
            return "RHI-INITIALIZED-CANONICAL-STAGING-OWNER-CAPACITY";
        if (!b.maxRetainedNativeBytes || !b.maxCpuMetadataBytes || !b.maxCpuPayloadBytes
            || !b.maxCpuResourceBytes || !b.maxCpuLeaseBytes || !b.maxCpuCallbackBytes)
            return "RHI-INITIALIZED-CANONICAL-STAGING-BUDGET";
        return nullptr;
    }

    static constexpr bool shaderReadEnabled(const Budget& b) noexcept
    {
        return b.contractVersion == 2u && b.indexContent.shaderReadContractVersion == 1u
            && b.indexContent.shaderReadOptIn;
    }

    static ResourceUsage expectedUsage(const Budget& b) noexcept
    {
        const auto usage = b.contractVersion == 4u
            ? (b.shaderContent.type == DescriptorType::UniformBuffer ? ResourceUsage::UniformBuffer
                                                                     : ResourceUsage::StorageBuffer)
                | ResourceUsage::CopySrc | ResourceUsage::CopyDst
            : b.contractVersion == 3u
            ? ResourceUsage::VertexBuffer | ResourceUsage::CopySrc | ResourceUsage::CopyDst
            : b.contractVersion == 2u
            ? ResourceUsage::IndexBuffer | ResourceUsage::CopySrc | ResourceUsage::CopyDst
            : ResourceUsage::CopySrc | ResourceUsage::CopyDst;
        return shaderReadEnabled(b) ? usage | ResourceUsage::StorageBuffer : usage;
    }

    static constexpr ResourceState expectedState(const Budget& b) noexcept
    {
        return b.contractVersion >= 2u && b.contractVersion <= 4u ? ResourceState::ShaderRead
                                                                  : ResourceState::CopySrc;
    }

    static const char* validateShaderPayload(const Budget& b, std::uint64_t bytes) noexcept
    {
        if (const auto* id = validateIntent(b))
            return id;
        if (b.contractVersion != 4u)
            return "RHI-INITIALIZED-SHADER-CONTENT-PROFILE";
        return bytes == b.shaderContent.bindingBytes ? nullptr
                                                     : "RHI-INITIALIZED-SHADER-CONTENT-SIZE";
    }

    static const char* validateVertexPayload(const Budget& b, std::uint64_t bytes) noexcept
    {
        if (const auto* id = validateIntent(b))
            return id;
        if (b.contractVersion != 3u)
            return "RHI-INITIALIZED-VERTEX-CONTENT-PROFILE";
        return bytes == b.vertexContent.elementCount * b.vertexContent.strideBytes
            ? nullptr
            : "RHI-INITIALIZED-VERTEX-CONTENT-SIZE";
    }

    static const char* validateVertexStream(
        const Budget& b, const VertexInputConsumptionContract::Stream& stream) noexcept
    {
        if (const auto* id = validateIntent(b))
            return id;
        if (b.contractVersion != 3u)
            return "RHI-INITIALIZED-VERTEX-CONTENT-PROFILE";
        return stream.strideBytes == b.vertexContent.strideBytes
                && stream.rate == b.vertexContent.rate && stream.attributeCount && stream.fetchBytes
                && stream.fetchBytes <= stream.strideBytes
            ? nullptr
            : "RHI-INITIALIZED-VERTEX-STREAM-JOIN";
    }

    static const char* validateVertexRange(const Budget& b,
        std::uint64_t bytes,
        const VertexInputConsumptionContract::Stream& stream,
        std::uint64_t bindingOffset,
        std::uint64_t vertexCount,
        std::uint64_t firstVertex,
        std::uint64_t instanceCount,
        std::uint64_t firstInstance) noexcept
    {
        if (const auto* id = validateVertexPayload(b, bytes))
            return id;
        if (const auto* id = validateVertexStream(b, stream))
            return id;
        if (bindingOffset % VertexContentIntent::scalarBytes)
            return "RHI-INITIALIZED-VERTEX-BINDING-OFFSET";
        return VertexInputConsumptionContract::validateStreamRange(
            stream, bytes, bindingOffset, vertexCount, firstVertex, instanceCount, firstInstance);
    }

    static const char* inspectOriginal(const GpuDevice& device,
        const GpuBuffer& buffer,
        const OriginalDeviceNativeLossObservation& loss,
        std::uint64_t payloadBytes,
        const Budget& b) noexcept
    {
        if (const auto* id = validateIntent(b))
            return id;
        if (b.contractVersion == 3u)
            if (const auto* id = validateVertexPayload(b, payloadBytes))
                return id;
        if (b.contractVersion == 4u)
            if (const auto* id = validateShaderPayload(b, payloadBytes))
                return id;
        if (shaderReadEnabled(b)) {
            const auto decision =
                semantics::OriginalCommandInitializedBufferStagingLowering::indexShaderReadLowering(
                    device.backend());
            if (!decision.privateInitializationAvailable)
                return decision.diagnostic;
        }
        if (const auto* id = device.validateOriginalDeviceNativeLoss(loss))
            return id;
        if (const auto* id = device.validateOriginalResourceRegistration(buffer))
            return id;
        if (const auto* id = buffer.originalCommittedStateDiagnostic())
            return id;
        const auto state = buffer.observeOriginalCommittedState();
        const auto& desc = buffer.desc();
        if (!state || state->state() != expectedState(b) || desc.initialState != expectedState(b)
            || desc.memory != MemoryUsage::CpuToGpu || desc.usage != expectedUsage(b))
            return "RHI-INITIALIZED-CANONICAL-STAGING-LAYOUT";
        if (!payloadBytes || payloadBytes != desc.sizeBytes || payloadBytes > b.maxCpuPayloadBytes
            || payloadBytes > b.maxRetainedNativeBytes
            || buffer.allocation().estimatedSizeBytes != payloadBytes)
            return "RHI-INITIALIZED-CANONICAL-STAGING-BYTE-CAPACITY";
        if (buffer.hostWritesSealed() || buffer.gpuWritesSealed()
            || buffer.gpuTransferWritesSealed())
            return "RHI-INITIALIZED-CANONICAL-STAGING-SEALED";
        return nullptr;
    }
};

// endregion original_command_initialized_buffer_staging

// region original_command_copy_pass_lowering

// CanonicalOriginalCopyPassLowering/1: immutable fully validated native input
// projection only. A lowering object never grants recording or queue authority.
struct OriginalCommandCopyPassLoweringBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxCopyOperations = 0u, maxColorAttachments = 0u, maxRetainedOwners = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
    std::uint32_t maxRenderPasses = 0u; // /2 independent aggregate native pass bound.
    // /3 keeps the regular Copy element space independent of upload copies.
    std::uint32_t maxUploadCopyOperations = 0u;
    std::uint64_t maxBufferUploadCommands = 0u, maxTextureUploadMips = 0u;
    std::uint64_t maxBufferUploadDeclaredBytes = 0u, maxTextureUploadPixelBytes = 0u;
    OriginalCommandUploadStagingContract::Budget uploadStaging;
    OriginalCommandOrderedUploadContract::Budget uploadWork;
    OriginalCommandReadbackResultContract::Budget readback;
    OriginalCommandReadbackMapContract::Budget readbackMap;
    OriginalGeneralOutputIntent generalOutput;
    OriginalCompletedDepthWriteReturnContract::Intent depthWriteReturn;
};

class OriginalCommandCopyPassLoweringContract final {
public:
    static constexpr std::uint32_t additionalOwnerCount =
        2u; // opaque wrapper, runtime-owned lowering payload.

    static constexpr const char* validateIntent(
        const OriginalCommandCopyPassLoweringBudget& b) noexcept
    {
        if (OriginalCompletedDepthWriteReturnContract::selected(b.depthWriteReturn)) {
            if (const auto* id =
                    OriginalCompletedDepthWriteReturnContract::validateIntent(b.depthWriteReturn))
                return id;
            if (b.contractVersion != 2u || !b.maxRenderPasses)
                return "RHI-DEPTH-WRITE-RETURN-LOWERING-PROFILE";
        }
        if (const auto* id =
                OriginalGeneralOutputPairingContract::validateOptional(b.generalOutput))
            return id;
        if (OriginalGeneralOutputPairingContract::selected(b.generalOutput)
            && b.contractVersion != 5u)
            return "RHI-GENERAL-OUTPUT-LOWERING-MAP-PROFILE";
        if (b.contractVersion != 1u && b.contractVersion != 2u && b.contractVersion != 3u
            && b.contractVersion != 4u && b.contractVersion != 5u)
            return "RHI-ORIGINAL-COPY-PASS-LOWERING-VERSION";
        if (b.contractVersion == 1u && b.maxRenderPasses)
            return "RHI-ORIGINAL-COPY-PASS-LOWERING-PROFILE";
        if (!b.prototypeOptIn)
            return "RHI-ORIGINAL-COPY-PASS-LOWERING-OPT-IN";
        if (b.contractVersion != 3u
            && (b.maxUploadCopyOperations || b.maxBufferUploadCommands || b.maxTextureUploadMips
                || b.maxBufferUploadDeclaredBytes || b.maxTextureUploadPixelBytes
                || b.uploadStaging.contractVersion || b.uploadStaging.prototypeOptIn
                || b.uploadStaging.maxBufferBytes || b.uploadStaging.maxPackedTextureBytes
                || b.uploadStaging.maxTotalBytes || b.uploadWork.contractVersion
                || b.uploadWork.prototypeOptIn || b.uploadWork.maxTextureUploadCommands
                || b.uploadWork.maxNativeCopyCommands || b.uploadWork.maxTextureWriteTexelBytes))
            return "RHI-ORIGINAL-COPY-PASS-LOWERING-UPLOAD-PROFILE";
        if (b.contractVersion != 4u && b.contractVersion != 5u
            && (b.readback.contractVersion || b.readback.prototypeOptIn
                || b.readback.maxReadbackCommands || b.readback.maxNativeCopyCommands
                || b.readback.maxResultDeclarations || b.readback.maxDeclaredReadbackBytes
                || b.readback.maxUniqueResultBytes || b.readback.maxCpuResultBytes))
            return "RHI-ORIGINAL-COPY-PASS-LOWERING-READBACK-PROFILE";
        if (b.contractVersion == 4u || b.contractVersion == 5u) {
            if (b.maxCopyOperations || b.maxColorAttachments || b.maxRenderPasses)
                return "RHI-ORIGINAL-COPY-PASS-LOWERING-READBACK-PROFILE";
            if (const auto* id = OriginalCommandReadbackResultContract::validateIntent(b.readback))
                return id;
        }
        if (b.contractVersion != 5u && OriginalCommandReadbackMapContract::hasBudget(b.readbackMap))
            return "RHI-ORIGINAL-COPY-PASS-LOWERING-MAP-PROFILE";
        if (b.contractVersion == 5u)
            if (const auto* id = OriginalCommandReadbackMapContract::validateIntent(b.readbackMap))
                return id;
        if (b.contractVersion == 3u) {
            if (!b.maxUploadCopyOperations)
                return "RHI-ORIGINAL-COPY-PASS-LOWERING-UPLOAD-BUDGET";
            if (const auto* id =
                    OriginalCommandUploadStagingContract::validateIntent(b.uploadStaging))
                return id;
            if (const auto* id = OriginalCommandOrderedUploadContract::validateIntent(b.uploadWork))
                return id;
        }
        if ((b.contractVersion == 1u && (!b.maxCopyOperations || !b.maxColorAttachments))
            || !b.maxRetainedOwners || !b.maxCpuMetadataBytes)
            return "RHI-ORIGINAL-COPY-PASS-LOWERING-BUDGET";
        return nullptr;
    }

    static const char* validatePassShape(
        const RenderPassDesc& pass, const OriginalCommandCopyPassLoweringBudget& budget) noexcept
    {
        if (pass.layeredRendering || !pass.subpasses.empty() || !pass.subpassDependencies.empty())
            return "RHI-ORIGINAL-COPY-PASS-LOWERING-SUBPASS-UNPROVEN";
        if (pass.colorAttachments.empty() && !pass.hasDepthStencilAttachment)
            return "RHI-ORIGINAL-COPY-PASS-LOWERING-EMPTY-PASS";
        if (pass.colorAttachments.size() > budget.maxColorAttachments)
            return "RHI-ORIGINAL-COPY-PASS-LOWERING-COLOR-CAPACITY";
        return nullptr;
    }

    // WebGPU's normative automatic usage transitions order the preceding copy
    // commands before the attachment use in this same encoder. Full range and
    // exact complete source/destination scopes are required; this is not a host
    // no-op synchronization primitive and never promotes NativeSync.
    static const char* attachmentTransition(
        const ResourceBarrierDesc& desc, const TextureSubresourceRange& whole) noexcept
    {
        if (const auto* id =
                semantics::RecordedResourceStateSequence::validateBarrier(desc, desc.before))
            return id;
        if (desc.textureSubresources != whole || !whole.mipLevelCount || !whole.arrayLayerCount
            || whole.baseMipLevel || whole.baseArrayLayer || whole.aspect != TextureAspect::Color)
            return "RHI-ORIGINAL-COPY-PASS-LOWERING-BARRIER-RANGE";
        if (desc.before != ResourceState::CopyDst || desc.after != ResourceState::ColorAttachment
            || desc.sourceStages != PipelineStage::Transfer
            || desc.sourceAccess != ResourceAccess::TransferWrite
            || desc.destinationStages != PipelineStage::ColorAttachmentOutput
            || desc.destinationAccess
                != (ResourceAccess::ColorAttachmentRead | ResourceAccess::ColorAttachmentWrite))
            return "RHI-ORIGINAL-COPY-PASS-LOWERING-BARRIER-SCOPE";
        return nullptr;
    }
};

// endregion original_command_copy_pass_lowering

// region original_command_initialized_buffer_upload_execution

// InitializedOriginalBufferUpload/1 is a versioned private Prototype subset.
// Availability never replaces per-root Pending, native scope or queue evidence.
class OriginalCommandInitializedBufferUploadExecutionContract final {
public:
    static constexpr const char* validateWork(
        std::uint64_t buffers, std::uint64_t textures) noexcept
    {
        if (textures)
            return "RHI-ORIGINAL-INITIALIZED-BUFFER-UPLOAD-TEXTURE-PROFILE";
        return buffers ? nullptr : "RHI-ORIGINAL-INITIALIZED-BUFFER-UPLOAD-MISSING";
    }
};
} // namespace arp::rhi

// endregion original_command_initialized_buffer_upload_execution

// region original_command_initialized_texture_upload_execution
namespace arp::rhi {
// InitializedOriginalTextureUpload/1: exact exclusive whole-texture writes,
// optionally interleaved with the existing initialized buffer upload subset.
class OriginalCommandInitializedTextureUploadExecutionContract final {
public:
    static constexpr const char* validateWork(std::uint64_t textures) noexcept
    {
        return textures ? nullptr : "RHI-ORIGINAL-INITIALIZED-TEXTURE-UPLOAD-MISSING";
    }
};

// OriginalCompletedShaderTextureInput/1 validates whole sampled input facts.
// These value checks grant no receipt, state transition or shader execution.
class OriginalCompletedShaderTextureInputContract final {
public:
    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t maxUploads = 0u, maxMipLevels = 0u, maxTexelBytes = 0u, maxJoinVisits = 0u;
    };

    static constexpr const char* validateIntent(const Budget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-COMPLETED-SHADER-TEXTURE-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-COMPLETED-SHADER-TEXTURE-OPT-IN";
        return b.maxUploads && b.maxMipLevels && b.maxTexelBytes && b.maxJoinVisits
            ? nullptr
            : "RHI-COMPLETED-SHADER-TEXTURE-BUDGET";
    }

    static const char* inspect(const TextureDesc& desc,
        const Budget& b,
        std::uint64_t uploads,
        std::uint64_t joins,
        std::uint64_t texelBytes,
        std::uint64_t beforeGeneration,
        std::uint64_t beforeRevision,
        std::uint64_t completedGeneration,
        std::uint64_t completedRevision,
        bool completed) noexcept
    {
        if (const auto* id = validateIntent(b))
            return id;
        const auto allowed = TextureUsage::Sampled | TextureUsage::CopyDst | TextureUsage::CopySrc;
        if ((desc.usage & TextureUsage::Sampled) == TextureUsage::None
            || (desc.usage & TextureUsage::CopyDst) == TextureUsage::None
            || (static_cast<std::uint32_t>(desc.usage) & ~static_cast<std::uint32_t>(allowed))
            || desc.memory != MemoryUsage::GpuOnly || desc.sampleCount != 1u
            || desc.extent.depth != 1u || desc.arrayLayerCount != 1u || desc.cubeCompatible
            || (desc.format != TextureFormat::Rgba8Unorm
                && desc.format != TextureFormat::Rgba16Float))
            return "RHI-COMPLETED-SHADER-TEXTURE-PROFILE";
        if (!uploads || uploads > b.maxUploads || !joins || joins > b.maxJoinVisits
            || joins < uploads || !desc.mipLevelCount || desc.mipLevelCount > b.maxMipLevels
            || !texelBytes || texelBytes > b.maxTexelBytes)
            return "RHI-COMPLETED-SHADER-TEXTURE-CAPACITY";
        if (!completed || !beforeRevision || uploads > UINT64_MAX - beforeGeneration
            || uploads >= UINT64_MAX - beforeRevision
            || completedGeneration != beforeGeneration + uploads
            || completedRevision != beforeRevision + uploads + 1u)
            return "RHI-COMPLETED-SHADER-TEXTURE-COMPLETION";
        return nullptr;
    }

    // Values describe a whole sampled view and its declared shader use only.
    // A matching footprint is neither a committed state nor a transition grant.
    static const char* inspectViewBinding(const TextureDesc& texture,
        const TextureViewDesc& view,
        ResourceState state,
        PipelineStage stages,
        ResourceAccess access) noexcept
    {
        return OriginalCompletedTextureReadPlanContract::inspectViewBinding(
            texture, view, state, stages, access);
    }
};
} // namespace arp::rhi

// endregion original_command_initialized_texture_upload_execution

// region original_general_output_snapshot
namespace arp::rhi {
// Queue completion authenticates an output association, not initialization or
// upload content. Copy/map/publication are separately admitted operations.
class OriginalGeneralOutputSnapshotContract final {
public:
    enum class Provenance { Unspecified, QueueOrderedSnapshot };

    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        Provenance provenance = Provenance::Unspecified;
        TextureSubresourceRange range;
        std::uint32_t maxRetainedOwners = 0u;
        std::uint64_t maxJoinVisits = 0u, maxSourceBytes = 0u, maxCpuMetadataBytes = 0u;
    };

    static constexpr const char* validateIntent(const Budget& b) noexcept
    {
        if (b.contractVersion != 1u && b.contractVersion != 2u && b.contractVersion != 3u)
            return "RHI-GENERAL-OUTPUT-SNAPSHOT-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-GENERAL-OUTPUT-SNAPSHOT-OPT-IN";
        if (b.provenance != Provenance::QueueOrderedSnapshot)
            return "RHI-GENERAL-OUTPUT-SNAPSHOT-PROVENANCE";
        return b.maxRetainedOwners && b.maxJoinVisits && b.maxSourceBytes && b.maxCpuMetadataBytes
            ? nullptr
            : "RHI-GENERAL-OUTPUT-SNAPSHOT-BUDGET";
    }

    static constexpr std::uint32_t additionalOwnerCount = 1u;

    static constexpr const char* validateUpstreamProfile(std::uint32_t outputVersion,
        std::uint32_t issuerVersion,
        bool whole,
        bool finite,
        std::uint32_t contentVersion) noexcept
    {
        return (outputVersion == 1u && issuerVersion == 6u && whole && !finite
                   && contentVersion == 4u)
                || (outputVersion == 2u && issuerVersion == 7u && finite && !whole
                    && contentVersion == 5u)
                || (outputVersion == 3u && issuerVersion == 8u && finite && !whole
                    && contentVersion == 6u)
            ? nullptr
            : "RHI-GENERAL-OUTPUT-ISSUER-PROFILE";
    }

    static const char* validateTarget(
        const TextureDesc& desc, const TextureSubresourceRange& range) noexcept
    {
        if (desc.usage != (TextureUsage::Storage | TextureUsage::CopySrc)
            || desc.memory != MemoryUsage::GpuOnly || desc.format != TextureFormat::Rgba16Float
            || desc.sampleCount != 1u || desc.mipLevelCount != 1u || desc.arrayLayerCount != 1u
            || desc.extent.depth != 1u || desc.cubeCompatible
            || range != defaultTextureSubresourceRange(desc))
            return "RHI-GENERAL-OUTPUT-SNAPSHOT-PROFILE";
        return nullptr;
    }

    // Output3 is a color-attachment role, not a storage output alias.
    static const char* validateTarget(const TextureDesc& desc,
        const TextureSubresourceRange& range,
        std::uint32_t version) noexcept
    {
        if (version == 1u || version == 2u)
            return validateTarget(desc, range);
        if (version != 3u)
            return "RHI-GENERAL-OUTPUT-SNAPSHOT-VERSION";
        if (desc.usage != (TextureUsage::RenderAttachment | TextureUsage::CopySrc)
            || desc.memory != MemoryUsage::GpuOnly || desc.format != TextureFormat::Rgba16Float
            || !desc.extent.width || !desc.extent.height || desc.extent.depth != 1u
            || desc.sampleCount != 1u || desc.mipLevelCount != 1u || desc.arrayLayerCount != 1u
            || desc.cubeCompatible || range != defaultTextureSubresourceRange(desc))
            return "RHI-GRAPHICS-OUTPUT-SNAPSHOT-PROFILE";
        return nullptr;
    }

    static const char* inspect(const TextureDesc& desc,
        const Budget& b,
        std::uint32_t issuerOwners,
        std::uint64_t visits,
        std::uint64_t bytes) noexcept
    {
        if (const auto* id = validateIntent(b))
            return id;
        if (const auto* id = validateTarget(desc, b.range, b.contractVersion))
            return id;
        if (issuerOwners == UINT32_MAX)
            return "RHI-GENERAL-OUTPUT-SNAPSHOT-OWNER-OVERFLOW";
        if (!issuerOwners || issuerOwners + 1u > b.maxRetainedOwners || !visits
            || visits > b.maxJoinVisits || !bytes || bytes > b.maxSourceBytes)
            return "RHI-GENERAL-OUTPUT-SNAPSHOT-CAPACITY";
        return nullptr;
    }

    // Prepared-state association preflight only; no execution/completion grant.
    template <class Projection>
    static const char* inspectPreparedGraphicsAttachment(
        const std::shared_ptr<const OriginalCommandStateTransaction>& root,
        const std::shared_ptr<const OriginalCommandStreamProgram>& stream,
        const std::shared_ptr<const OriginalCommandTextureLease>& output,
        const Budget& budget,
        const typename Projection::StateBoundRequest& request,
        std::uint32_t issuerOwners,
        std::uint64_t allocationBytes,
        typename Projection::AttachmentPoint& captured) noexcept
    {
        return inspectGraphicsAttachment<Projection>(
            root, stream, output, budget, request, issuerOwners, allocationBytes, captured, false);
    }

    // Frozen association only. Backend completion receipts remain mandatory;
    // current attachment state is checked separately, or by a downstream seal.
    template <class Projection>
    static const char* inspectCompletedGraphicsAttachment(
        const std::shared_ptr<const OriginalCommandStateTransaction>& root,
        const std::shared_ptr<const OriginalCommandTextureLease>& output,
        const Budget& budget,
        const typename Projection::StateBoundRequest& request,
        std::uint32_t issuerOwners,
        std::uint64_t allocationBytes,
        typename Projection::AttachmentPoint& captured) noexcept
    {
        if (!root || !root->hasQuotaBoundSharedOwner(root))
            return "RHI-ORIGINAL-TRANSACTION-OWNER";
        if (const auto* id = OriginalCommandTextureLease::validateOriginal(output))
            return id;
        if (const auto* id =
                root->validateCompletedGraphicsOutputIdentity(*output->originalObject()))
            return id;
        return inspectGraphicsAttachment<Projection>(root,
            root->plan_->graphicsStream,
            output,
            budget,
            request,
            issuerOwners,
            allocationBytes,
            captured,
            true);
    }

private:
    template <class Projection>
    static const char* inspectGraphicsAttachment(
        const std::shared_ptr<const OriginalCommandStateTransaction>& root,
        const std::shared_ptr<const OriginalCommandStreamProgram>& stream,
        const std::shared_ptr<const OriginalCommandTextureLease>& output,
        const Budget& budget,
        const typename Projection::StateBoundRequest& request,
        std::uint32_t issuerOwners,
        std::uint64_t allocationBytes,
        typename Projection::AttachmentPoint& captured,
        bool completed) noexcept
    {
        if (const auto* id = validateIntent(budget))
            return id;
        if (budget.contractVersion != 3u)
            return "RHI-GRAPHICS-OUTPUT-SNAPSHOT-VERSION";
        if (const auto* id = OriginalTypedStateBoundUsageContract::validateIntent(request))
            return id;
        if (request.contractVersion != 6u)
            return "RHI-GRAPHICS-OUTPUT-SNAPSHOT-USAGE-PROFILE";
        if (!completed) {
            if (const auto* id = OriginalCommandStateTransaction::validateOriginal(root))
                return id;
            if (const auto* id = root->compareOriginalGraphicsStream(stream))
                return id;
        }
        if (const auto* id = OriginalCommandTextureLease::validateOriginal(output))
            return id;
        const auto& graph = root->originalGraph();
        const auto& inputs = graph->originalInputs();
        const auto& usage = *inputs->originalScalars()->originalUsage();
        const auto& sources = usage.originalSources()->sources();
        if (!core::SharedOwnerProjection::sameOwner(
                output->originalMetadataQuota(), usage.originalSources()->originalMetadataQuota())
            || !output->originalMetadataQuota()
            || output->originalMetadataQuota()->limitBytes() != budget.maxCpuMetadataBytes)
            return "RHI-GENERAL-OUTPUT-SNAPSHOT-QUOTA";
        std::uint64_t visits = usage.commands().size(), matches = 0u;
        if (const auto* id = inspect(
                output->originalObject()->desc(), budget, issuerOwners, visits, allocationBytes))
            return id;
        typename Projection::AttachmentPoint candidate{};
        const auto visitor = [&](const typename Projection::Point& point) noexcept -> const char* {
            if (visits == budget.maxJoinVisits)
                return "RHI-GENERAL-OUTPUT-SNAPSHOT-CAPACITY";
            ++visits;
            const auto* attachment = std::get_if<typename Projection::AttachmentPoint>(&point);
            if (!attachment)
                return nullptr;
            const auto& p = attachment->original;
            if (p.sourceIndex >= sources.size() || p.commandIndex >= usage.commands().size())
                return "RHI-GRAPHICS-OUTPUT-SNAPSHOT-SOURCE";
            const auto* view = std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                &sources[p.sourceIndex]);
            if (!view || !*view)
                return "RHI-GRAPHICS-OUTPUT-SNAPSHOT-VIEW";
            if (!core::SharedOwnerProjection::sameOwner((*view)->originalParentLease(), output))
                return nullptr;
            if (const auto* id = OriginalCommandTextureViewSource::validateOriginal(*view))
                return id;
            if (p.origin != decltype(p.origin)::ExplicitOperand
                || usage.commands()[p.commandIndex].kind != CommandUsageKind::BeginRenderPass
                || p.range != budget.range || p.range.aspect != TextureAspect::Color
                || attachment->resource != output->resource()
                || attachment->access.state != ResourceState::ColorAttachment
                || attachment->access.stages != PipelineStage::ColorAttachmentOutput
                || attachment->access.access
                    != (ResourceAccess::ColorAttachmentRead | ResourceAccess::ColorAttachmentWrite)
                || attachment->scope.first != p.commandIndex)
                return "RHI-GRAPHICS-OUTPUT-SNAPSHOT-WRITE-PROFILE";
            const auto* arguments = inputs->descriptorAt(p.commandIndex);
            const auto* pass = arguments
                ? std::get_if<std::shared_ptr<const RenderPassArguments>>(arguments)
                : nullptr;
            if (!pass || !*pass)
                return "RHI-GRAPHICS-OUTPUT-SNAPSHOT-PASS";
            const auto& desc = (*pass)->desc();
            if (desc.hasDepthStencilAttachment || !desc.subpasses.empty() || desc.layeredRendering
                || desc.colorAttachments.size() != 1u)
                return "RHI-GRAPHICS-OUTPUT-SNAPSHOT-PASS";
            const auto& color = desc.colorAttachments[0u];
            if (color.target != (*view)->resource()
                || color.format != output->originalObject()->desc().format
                || color.loadOp != LoadOp::Clear || color.storeOp != StoreOp::Store
                || color.hasResolveTarget)
                return "RHI-GRAPHICS-OUTPUT-SNAPSHOT-PASS";
            if (attachment->registrationGeneration
                != output->originalObject()->originalResourceRegistration().generation())
                return "RHI-GENERAL-OUTPUT-SNAPSHOT-GENERATION";
            candidate = *attachment;
            ++matches;
            return nullptr;
        };
        const auto inspected = completed
            ? Projection::visitImmutableBindings(stream, graph, request, visitor)
            : Projection::visitStateBound(stream, graph, request, visitor);
        if (inspected.diagnostic)
            return inspected.diagnostic;
        if (matches != 1u)
            return "RHI-GRAPHICS-OUTPUT-SNAPSHOT-OUTPUT-BINDING";
        captured = candidate;
        return nullptr;
    }
};
} // namespace arp::rhi

// endregion original_general_output_snapshot

// region original_general_output_readback_source
namespace arp::rhi {
inline const char* OriginalCommandReadbackJoinContract::validateGeneralOutputSource(
    const std::shared_ptr<const OriginalCommandTextureReadbackSource>& source,
    std::uint32_t outputVersion) noexcept
{
    if (const auto* id = OriginalCommandTextureReadbackSource::validateOriginal(source))
        return id;
    if (source->contractVersion() != 2u)
        return "RHI-GENERAL-OUTPUT-READBACK-SOURCE-PROFILE";
    const auto& texture = source->originalSource();
    const auto& layout = texture->originalRequest()->originalLayout();
    const auto& fields = source->originalInput()->scalarFields();
    if (const auto* id = OriginalGeneralOutputSnapshotContract::validateTarget(
            texture->originalObject()->desc(), fields.range(), outputVersion))
        return id;
    if (layout->mipCopies().size() != 1u
        || layout->mipCopies()[0].extent != texture->originalObject()->desc().extent)
        return "RHI-GENERAL-OUTPUT-READBACK-WHOLE-RANGE";
    return destinationIntent(source->originalDestinationLease()->layout());
}
} // namespace arp::rhi

// endregion original_general_output_readback_source
