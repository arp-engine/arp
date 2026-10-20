#pragma once
#include "core/unsigned_range.h"
#include "rhi/frontend/completed_color_texture_view.h"
#include "rhi/frontend/resource_native_allocation_observation.h"
#include "rhi/frontend/resource_state_validation.h"
#include "rhi/frontend/resource_texture_state_range.h"
#include "rhi/frontend/transfer_upload_destination_validation.h"
#include "rhi/gpu_common.h"
#include "rhi/semantics/resource_lowering.h"

#include <optional>

// region original_texture_content_creation
namespace arp::rhi {
// ContentCreation1 is whole-texture upload; ContentCreation2 is a fixed-cell
// attachment domain. ContentCreation3 is native whole-texture creation from
// Undefined, with a separate physical allocation ceiling. Creation never grants
// upload, transition, sampling or completion authority.
// ContentCreation4 pairs the fixed-cell attachment domain with queried native
// allocation bounds. Its cells cannot be consumed by legacy ContentPlan2/3.
// Factory ownership/Uninitialized describe the new domain, never GPU texels.
// ContentCreation5 is the WebGPU original attachment domain. WebGPU exposes no
// driver allocation query: its separate bounded texel accounting is explicit
// and must never be reported as a queried native memory allocation.
// /6 (native queried allocation) and /7 (WebGPU texel bound) add RGBA16F
// storage usage to fixed cells. Creation leaves every cell Uninitialized;
// neither the legacy attachment plans nor upload plans can mutate them.
struct OriginalTextureContentCreationBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxTextures = 0u, maxContentDomains = 0u;
    std::uint64_t maxTexelBytes = 0u, maxUploadGenerations = 0u, maxCpuContentBytes = 0u;
    std::uint64_t maxContentCells = 0u;
    std::uint64_t maxNativeAllocationBytes = 0u;
    std::optional<OriginalNativeAllocationObservationContract::Intent> nativeAllocationObservation;
};

class OriginalTextureContentCreationContract final {
public:
    static constexpr bool isCellCreation(std::uint32_t version) noexcept
    {
        return version == 2u || version == 4u || version == 5u || isStorageCellCreation(version);
    }

    static constexpr bool isStorageCellCreation(std::uint32_t version) noexcept
    {
        return version == 6u || version == 7u;
    }

    static constexpr bool isBindlessCellCreation(std::uint32_t version) noexcept
    {
        return version == 4u || version == 5u;
    }

    static constexpr bool hasNativeAllocationBudget(std::uint32_t version) noexcept
    {
        return version == 3u || version == 4u || version == 6u;
    }

    static constexpr bool cellControlMatchesPlan(std::uint32_t control, std::uint32_t plan) noexcept
    {
        return (control == 2u && (plan == 2u || plan == 3u || plan == 6u))
            || (isBindlessCellCreation(control) && plan == 9u) || (control == 4u && plan == 18u)
            || (control == 5u && plan == 19u) || (control == 6u && (plan == 13u || plan == 15u))
            || (control == 7u && (plan == 14u || plan == 16u));
    }
    enum class Disposition { ValidatorOnly, Prototype, DiagnosticFailure };

    struct Decision {
        std::uint32_t contractVersion = 1u;
        Disposition disposition = Disposition::DiagnosticFailure;
        bool exclusiveCreationAvailable = false, nativeContentExecutionAvailable = false;
        const char* diagnostic = "RHI-CANONICAL-CONTENT-CREATION-BACKEND-UNKNOWN";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        return semantics::ResourceLowering::OriginalTextureContentCreation_lowering<Decision,
            Disposition>(backend, version);
    }

    static constexpr const char* validateIntent(
        const OriginalTextureContentCreationBudget& b) noexcept
    {
        if (b.contractVersion < 1u || b.contractVersion > 7u)
            return "RHI-CANONICAL-CONTENT-CREATION-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-CANONICAL-CONTENT-CREATION-OPT-IN";
        if (b.maxTextures != 1u || b.maxContentDomains != 1u || !b.maxTexelBytes
            || !b.maxUploadGenerations || !b.maxCpuContentBytes)
            return "RHI-CANONICAL-CONTENT-CREATION-BUDGET";
        if ((!isCellCreation(b.contractVersion) && b.maxContentCells)
            || (isCellCreation(b.contractVersion) && !b.maxContentCells))
            return "RHI-CANONICAL-CONTENT-CREATION-CELL-BUDGET";
        if ((hasNativeAllocationBudget(b.contractVersion) && !b.maxNativeAllocationBytes)
            || (!hasNativeAllocationBudget(b.contractVersion) && b.maxNativeAllocationBytes))
            return "RHI-CANONICAL-CONTENT-CREATION-NATIVE-BUDGET";
        if (b.nativeAllocationObservation)
            return OriginalNativeAllocationObservationContract::validateIntent(
                *b.nativeAllocationObservation, b.contractVersion, b.maxNativeAllocationBytes);
        return nullptr;
    }

    // Shared authored attachment layout for query and creation. This validates
    // logical geometry only; it never substitutes texel bytes for driver memory.
    static const char* inspectCellLayout(const TextureDesc& desc,
        std::uint64_t maxCells,
        std::uint64_t& texelBytes,
        std::uint32_t version = 2u) noexcept
    {
        texelBytes = 0u;
        if (!isCellCreation(version))
            return "RHI-CANONICAL-CONTENT-CREATION-CELL-VERSION";
        const bool storage = isStorageCellCreation(version);
        const auto allowed = TextureUsage::Sampled | TextureUsage::RenderAttachment
            | TextureUsage::CopySrc | (storage ? TextureUsage::Storage : TextureUsage::None);
        if (storage
            && (desc.format != TextureFormat::Rgba16Float || desc.arrayLayerCount != 1u
                || (desc.usage & TextureUsage::Storage) == TextureUsage::None
                || (desc.usage & TextureUsage::Sampled) == TextureUsage::None))
            return "RHI-CANONICAL-CONTENT-CREATION-STORAGE-PROFILE";
        if (desc.memory != MemoryUsage::GpuOnly || desc.initialState != ResourceState::Undefined
            || desc.sampleCount != 1u || desc.extent.depth != 1u || desc.cubeCompatible
            || isBlockCompressed(desc.format)
            || (desc.usage & TextureUsage::RenderAttachment) == TextureUsage::None
            || (static_cast<std::uint32_t>(desc.usage) & ~static_cast<std::uint32_t>(allowed)))
            return "RHI-CANONICAL-CONTENT-CREATION-CELL-PROFILE";
        const char* diagnostic = nullptr;
        if (!semantics::OriginalTextureStateRange::capture(desc, maxCells, diagnostic))
            return diagnostic;
        return semantics::TextureUploadDestinationValidation::footprint(desc, texelBytes);
    }

    static const char* inspect(const TextureDesc& desc,
        const OriginalTextureContentCreationBudget& b,
        std::uint64_t& texelBytes) noexcept
    {
        texelBytes = 0u;
        if (const auto* id = validateIntent(b))
            return id;
        std::uint64_t required = 0u;
        if (b.contractVersion == 1u || b.contractVersion == 3u) {
            if (const auto* id = semantics::TextureUploadDestinationValidation::validate(desc,
                    required,
                    b.contractVersion == 3u ? ResourceState::Undefined : ResourceState::CopyDst))
                return id;
        } else {
            if (const auto* id =
                    inspectCellLayout(desc, b.maxContentCells, required, b.contractVersion))
                return id;
        }
        if (required > b.maxTexelBytes)
            return "RHI-CANONICAL-CONTENT-CREATION-BYTE-CAPACITY";
        texelBytes = required;
        return nullptr;
    }
};

struct OriginalTextureContentPlanBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxContentControls = 0u, maxUploadCommands = 0u;
    std::uint64_t maxContentCells = 0u, maxContentSteps = 0u, maxCommandVisits = 0u,
                  maxProjectionVisits = 0u;
    std::uint64_t maxReadbackCommands = 0u;
};

// Storage cells use the original range graph and mutation journal. /13 pairs
// native creation6, /14 WebGPU creation7; neither version is an execution
// profile. Clear/Store initializes cells; partial stores preserve initialized
// texels. /15 and /16 compose the same storage controls with completed read-only
// D32 controls4/2 in one canonical journal. They remain CPU planning only;
// previous lifecycle/completion issuers cannot execute the mixed profile.
// The state graph, not content projection, proves dependencies.
class OriginalStorageTextureContentPlanContract final {
public:
    static constexpr bool isVersion(std::uint32_t v) noexcept
    {
        return v == 13u || v == 14u || isMixedVersion(v);
    }

    static constexpr bool isMixedVersion(std::uint32_t v) noexcept { return v == 15u || v == 16u; }

    static constexpr std::uint32_t storagePlanVersion(std::uint32_t v) noexcept
    {
        return v == 15u ? 13u : v == 16u ? 14u : v;
    }

    static constexpr std::uint32_t depthPlanVersion(std::uint32_t v) noexcept
    {
        return v == 15u ? 12u : v == 16u ? 11u : 0u;
    }

    static constexpr std::uint32_t controlVersion(std::uint32_t v) noexcept
    {
        return storagePlanVersion(v) == 13u ? 6u : storagePlanVersion(v) == 14u ? 7u : 0u;
    }

    static constexpr const char* validateIntent(const OriginalTextureContentPlanBudget& b) noexcept
    {
        if (!isVersion(b.contractVersion))
            return "RHI-STORAGE-CONTENT-PLAN-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-STORAGE-CONTENT-PLAN-OPT-IN";
        if (!b.maxContentControls || !b.maxContentCells || !b.maxContentSteps || !b.maxCommandVisits
            || !b.maxProjectionVisits || b.maxContentCells > SIZE_MAX)
            return "RHI-STORAGE-CONTENT-PLAN-BUDGET";
        if (isMixedVersion(b.contractVersion)
            && (b.maxContentControls < 2u || b.maxContentCells < 2u))
            return "RHI-MIXED-DEPTH-STORAGE-CONTENT-BUDGET";
        return b.maxUploadCommands || b.maxReadbackCommands ? "RHI-STORAGE-CONTENT-PLAN-SCHEMA"
                                                            : nullptr;
    }

    template <class Stages, class Access>
    static const char* inspectViewBinding(const TextureDesc& texture,
        const TextureViewDesc& view,
        ResourceState state,
        Stages stages,
        Access access,
        bool storage) noexcept
    {
        if (texture.format != TextureFormat::Rgba16Float || texture.sampleCount != 1u
            || texture.arrayLayerCount != 1u || view.format != texture.format
            || view.dimension != TextureViewDimension::Texture2D
            || view.aspect != TextureAspect::Color || view.mipLevelCount != 1u
            || view.baseArrayLayer || view.arrayLayerCount != 1u
            || view.baseMipLevel >= texture.mipLevelCount)
            return "RHI-STORAGE-CONTENT-PLAN-VIEW";
        if (stages != Stages::ComputeShader
            || state != (storage ? ResourceState::ShaderWrite : ResourceState::ShaderRead)
            || access != (storage ? Access::ShaderWrite : Access::ShaderRead)
            || view.usage != (storage ? TextureUsage::Storage : TextureUsage::Sampled))
            return "RHI-STORAGE-CONTENT-PLAN-ACCESS";
        return nullptr;
    }

    static constexpr auto lowering(Backend backend, std::uint32_t version) noexcept
    {
        return semantics::OriginalCommandStateAdmissionLowering::textureContentPlanLowering(
            backend, version);
    }
};

// ContentPlan11 (WebGPU control2) and /12 (Native control4) are read-only
// depth consumers over the original cell timeline.
// It reuses Sequence2, but admits only authentic public Root Compute reads.
// It grants neither native commands nor completion; no content is proposed.
class OriginalCompletedDepthRootReadPlanContract final {
public:
    static constexpr std::uint32_t version = 11u;
    static constexpr std::uint32_t nativeVersion = 12u;

    static constexpr bool isVersion(std::uint32_t v) noexcept
    {
        return v == version || v == nativeVersion;
    }

    static constexpr std::uint32_t controlVersion(std::uint32_t v) noexcept
    {
        return v == version ? 2u : v == nativeVersion ? 4u : 0u;
    }

    static constexpr std::uint32_t rootVersion(std::uint32_t v) noexcept
    {
        return v == version ? 5u : v == nativeVersion ? 6u : 0u;
    }

    static constexpr const char* validateReadOnlyControl(
        std::uint32_t plan, std::uint32_t control, bool hasCells, std::uint64_t mutations) noexcept
    {
        return isVersion(plan) && control == controlVersion(plan) && hasCells && !mutations
            ? nullptr
            : "RHI-DEPTH-ROOT-READ-PLAN-MUTATION";
    }

    static constexpr const char* validateIntent(const OriginalTextureContentPlanBudget& b) noexcept
    {
        if (!isVersion(b.contractVersion))
            return "RHI-DEPTH-ROOT-READ-PLAN-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-DEPTH-ROOT-READ-PLAN-OPT-IN";
        if (!b.maxContentControls || !b.maxContentCells || !b.maxContentSteps || !b.maxCommandVisits
            || !b.maxProjectionVisits)
            return "RHI-DEPTH-ROOT-READ-PLAN-BUDGET";
        return b.maxUploadCommands || b.maxReadbackCommands ? "RHI-DEPTH-ROOT-READ-PLAN-SCHEMA"
                                                            : nullptr;
    }

    static constexpr auto lowering(Backend backend, std::uint32_t v = version) noexcept
    {
        return semantics::OriginalCommandStateAdmissionLowering::textureContentPlanLowering(
            backend, v);
    }

    static constexpr const char* validatePendingProfile(std::uint32_t pendingVersion,
        std::uint32_t lifecycleVersion,
        std::uint32_t contentVersion,
        bool reserved,
        bool claimed) noexcept
    {
        if (pendingVersion == 4u)
            return lifecycleVersion == 20u && contentVersion == 19u && reserved && claimed
                ? nullptr
                : "RHI-CANONICAL-GRAPHICS-DEPTH-PENDING-LIFECYCLE";
        if (pendingVersion == 3u)
            return lifecycleVersion == 19u && contentVersion == 18u && reserved && claimed
                ? nullptr
                : "RHI-GRAPHICS-DEPTH-PENDING-LIFECYCLE";
        if (pendingVersion == 2u)
            return ((lifecycleVersion == 14u && contentVersion == nativeVersion)
                       || (lifecycleVersion == 17u && contentVersion == 15u))
                    && reserved && claimed
                ? nullptr
                : "RHI-DEPTH-ROOT-PENDING-LIFECYCLE";
        if (pendingVersion != 1u)
            return "RHI-DEPTH-ROOT-PENDING-VERSION";
        return ((lifecycleVersion == 13u && contentVersion == version)
                   || (lifecycleVersion == 18u && contentVersion == 16u))
                && reserved && claimed
            ? nullptr
            : "RHI-DEPTH-ROOT-PENDING-LIFECYCLE";
    }

    static constexpr const char* validatePendingStates(ResourceState captured,
        ResourceState initial,
        ResourceState final,
        ResourceState pending,
        std::uint64_t capturedRevision,
        std::uint64_t pendingRevision,
        std::uint64_t transitions) noexcept
    {
        if ((captured != ResourceState::DepthAttachment && captured != ResourceState::CopySrc
                && captured != ResourceState::ShaderRead)
            || initial != captured || final != ResourceState::ShaderRead || pending != final
            || transitions > 1u || (captured != ResourceState::ShaderRead && !transitions))
            return "RHI-DEPTH-ROOT-PENDING-STATE";
        return capturedRevision && capturedRevision <= UINT64_MAX - transitions
                && pendingRevision == capturedRevision + transitions
            ? nullptr
            : "RHI-DEPTH-ROOT-PENDING-REVISION";
    }
};

// ContentPlan /7 is the independent whole-upload domain for ContentCreation /3.
// It preserves the actual Undefined origin. The plan must prove one explicit
// whole transition and one complete upload per fresh control in the original
// graph. These value checks grant neither staging initialization nor execution.
class OriginalUndefinedTextureUploadPlanContract final {
public:
    static constexpr const char* validateIntent(const OriginalTextureContentPlanBudget& b) noexcept
    {
        if (b.contractVersion != 7u)
            return "RHI-UNDEFINED-TEXTURE-UPLOAD-PLAN-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-UNDEFINED-TEXTURE-UPLOAD-PLAN-OPT-IN";
        if (!b.maxContentControls || !b.maxUploadCommands || !b.maxContentSteps
            || !b.maxCommandVisits || !b.maxProjectionVisits)
            return "RHI-UNDEFINED-TEXTURE-UPLOAD-PLAN-BUDGET";
        if (b.maxContentCells || b.maxReadbackCommands)
            return "RHI-UNDEFINED-TEXTURE-UPLOAD-PLAN-SCHEMA";
        return nullptr;
    }

    static constexpr const char* validateControl(
        std::uint32_t controlVersion, std::uint32_t planVersion) noexcept
    {
        return (controlVersion == 1u && planVersion == 1u)
                || (controlVersion == 3u && planVersion == 7u)
            ? nullptr
            : "RHI-CANONICAL-CONTENT-UPLOAD-CONTROL-PLAN-PROFILE";
    }

    static constexpr const char* validateInitial(std::uint32_t controlVersion,
        bool uninitialized,
        std::uint64_t generation,
        ResourceState captured) noexcept
    {
        return controlVersion == 3u && uninitialized && !generation
                && captured == ResourceState::Undefined
            ? nullptr
            : "RHI-UNDEFINED-TEXTURE-UPLOAD-PLAN-INITIAL";
    }
};

// ContentPlan4/5 observe completed whole content on distinct General roots.
// /5 pairs the finite State5/Inputs6 graph and independently versioned Pending2.
// /8 observes genuine control3 uploads through the existing BindlessRoot /4
// graph and independently versioned Pending4. It grants no native issuer.
// /10 observes canonical control1 whole 2D uploads in the same graph. Content9
// explicitly selects this independent profile; Pending6 requires Lifecycle12.
// Resource-state ownership is
// enlisted by the existing transaction; content itself is never proposed or
// completed again. A value check does not authorize native shader execution.
class OriginalCompletedTextureReadPlanContract final {
public:
    static constexpr bool isReadOnly(std::uint32_t version) noexcept
    {
        return version == 4u || version == 5u || isRootRead(version);
    }

    // Read17 is explicitly selected by Graphics completed-read /2. It adds
    // RGBA16F to native control3 whole-view reads without changing Read8/10.
    static constexpr bool isRootRead(std::uint32_t version) noexcept
    {
        return version == 8u || version == 10u || version == 17u || version == 18u;
    }

    static constexpr std::uint32_t graphicsReadVersion(std::uint32_t profile) noexcept
    {
        return profile == 3u ? 18u : profile == 2u ? 17u : profile == 1u ? 10u : 8u;
    }

    static constexpr const char* validateRootProfile(std::uint32_t version,
        bool originalGeneralProfile,
        bool finiteGeneralProfile,
        bool bindlessRootProfile = false) noexcept
    {
        return (version == 4u && originalGeneralProfile && !finiteGeneralProfile
                   && !bindlessRootProfile)
                || (version == 5u && finiteGeneralProfile && !originalGeneralProfile
                    && !bindlessRootProfile)
                || (isRootRead(version) && bindlessRootProfile && !originalGeneralProfile
                    && !finiteGeneralProfile)
            ? nullptr
            : "RHI-COMPLETED-TEXTURE-READ-PLAN-ROOT-PROFILE";
    }

    // Values never authenticate a receipt, parent, resource control or journal.
    // Private Pending consumers must prove those original identities separately.
    static constexpr const char* validatePendingProfile(std::uint32_t version,
        bool optIn,
        bool originalGeneralProfile,
        std::uint32_t contentPlanVersion,
        bool finiteGeneralProfile = false,
        bool bindlessRootProfile = false,
        std::uint32_t rootCompletedReadVersion = 0u) noexcept
    {
        if (version != 1u && version != 2u && version != 3u && version != 4u && version != 5u
            && version != 6u && version != 7u && version != 8u && version != 9u)
            return "RHI-COMPLETED-SHADER-TEXTURE-PENDING-VERSION";
        if (!optIn)
            return "RHI-COMPLETED-SHADER-TEXTURE-PENDING-OPT-IN";
        if (version == 9u && contentPlanVersion == 19u)
            return bindlessRootProfile && !originalGeneralProfile && !finiteGeneralProfile
                    && rootCompletedReadVersion == 3u
                ? nullptr
                : "RHI-CANONICAL-GRAPHICS-DEPTH-COLOR-PENDING-PROFILE";
        if (version == 8u)
            return bindlessRootProfile && !originalGeneralProfile && !finiteGeneralProfile
                    && contentPlanVersion == 18u
                    && (rootCompletedReadVersion == 0u || rootCompletedReadVersion == 2u)
                ? nullptr
                : "RHI-GRAPHICS-DEPTH-COLOR-PENDING-PROFILE";
        if (rootCompletedReadVersion
            && !((version == 6u && rootCompletedReadVersion == 1u)
                || (version == 7u && rootCompletedReadVersion == 2u)
                || (version == 9u && rootCompletedReadVersion == 3u)))
            return "RHI-COMPLETED-SHADER-TEXTURE-PENDING-PROFILE";
        return (version == 1u && originalGeneralProfile && !finiteGeneralProfile
                   && !bindlessRootProfile && contentPlanVersion == 4u)
                || (version == 2u && finiteGeneralProfile && !originalGeneralProfile
                    && !bindlessRootProfile && contentPlanVersion == 5u)
                || (version == 3u && finiteGeneralProfile && !originalGeneralProfile
                    && !bindlessRootProfile && contentPlanVersion == 6u)
                || (version == 4u && bindlessRootProfile && !originalGeneralProfile
                    && !finiteGeneralProfile && contentPlanVersion == 8u)
                || (version == 5u && !rootCompletedReadVersion && bindlessRootProfile
                    && !originalGeneralProfile && !finiteGeneralProfile && contentPlanVersion == 9u)
                || (version == 6u && rootCompletedReadVersion == 1u && bindlessRootProfile
                    && !originalGeneralProfile && !finiteGeneralProfile && contentPlanVersion == 9u)
                || (version == 7u && rootCompletedReadVersion == 2u && bindlessRootProfile
                    && !originalGeneralProfile && !finiteGeneralProfile && contentPlanVersion == 9u)
                || (version == 9u && rootCompletedReadVersion == 3u && bindlessRootProfile
                    && !originalGeneralProfile && !finiteGeneralProfile && contentPlanVersion == 9u)
            ? nullptr
            : "RHI-COMPLETED-SHADER-TEXTURE-PENDING-PROFILE";
    }

    static constexpr const char* validatePendingStates(ResourceState captured,
        ResourceState initial,
        ResourceState final,
        ResourceState pending,
        std::uint64_t capturedRevision,
        std::uint64_t pendingRevision,
        std::uint32_t version = 1u) noexcept
    {
        if ((version == 4u || version == 5u || version == 6u || version == 7u || version == 8u
                || version == 9u)
            && captured == ResourceState::ShaderRead) {
            // Repeated frames retain the existing completed content/state.
            // They do not invent another upload or gratuitous transition.
            if (initial != captured || final != captured || pending != captured)
                return "RHI-COMPLETED-SHADER-TEXTURE-PENDING-STATE";
            return capturedRevision && pendingRevision == capturedRevision
                ? nullptr
                : "RHI-COMPLETED-SHADER-TEXTURE-PENDING-REVISION";
        }
        // The selected whole read has exactly one authored CopyDst->ShaderRead
        // transition. A normalized range write advances its control once.
        if (captured != ResourceState::CopyDst || initial != ResourceState::CopyDst
            || final != ResourceState::ShaderRead || pending != ResourceState::ShaderRead)
            return "RHI-COMPLETED-SHADER-TEXTURE-PENDING-STATE";
        return capturedRevision && capturedRevision != UINT64_MAX
                && pendingRevision == capturedRevision + 1u
            ? nullptr
            : "RHI-COMPLETED-SHADER-TEXTURE-PENDING-REVISION";
    }

    static constexpr const char* validateIntent(const OriginalTextureContentPlanBudget& b) noexcept
    {
        if (!isReadOnly(b.contractVersion))
            return "RHI-COMPLETED-TEXTURE-READ-PLAN-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-COMPLETED-TEXTURE-READ-PLAN-OPT-IN";
        if (!b.maxContentControls || !b.maxContentSteps || !b.maxCommandVisits
            || !b.maxProjectionVisits)
            return "RHI-COMPLETED-TEXTURE-READ-PLAN-BUDGET";
        if (b.maxUploadCommands || b.maxContentCells || b.maxReadbackCommands)
            return "RHI-COMPLETED-TEXTURE-READ-PLAN-SCHEMA";
        return nullptr;
    }

    static constexpr const char* validateObservation(std::uint32_t controlVersion,
        bool completed,
        std::uint64_t generation,
        std::uint64_t revision,
        std::uint32_t planVersion = 4u) noexcept
    {
        return (((planVersion == 8u || planVersion == 17u) && controlVersion == 3u)
                   || ((planVersion == 10u || planVersion == 18u) && controlVersion == 1u)
                   || ((planVersion == 4u || planVersion == 5u || planVersion == 6u)
                       && controlVersion == 1u))
                && completed && generation && revision
            ? nullptr
            : "RHI-COMPLETED-TEXTURE-READ-PLAN-INITIAL";
    }

    // Also used by completed-input association; keep whole sampled view
    // eligibility identical before native custody and during CPU planning.
    template <class Stages, class Access>
    static const char* inspectViewBinding(const TextureDesc& texture,
        const TextureViewDesc& view,
        ResourceState state,
        Stages stages,
        Access access,
        std::uint32_t planVersion = 4u) noexcept
    {
        if (planVersion != 4u && planVersion != 5u && planVersion != 6u && !isRootRead(planVersion))
            return "RHI-COMPLETED-TEXTURE-READ-PLAN-VERSION";
        const auto allowedStages =
            Stages::VertexShader | Stages::FragmentShader | Stages::ComputeShader;
        if (state != ResourceState::ShaderRead || access != Access::ShaderRead
            || stages == Stages::None
            || (static_cast<std::uint32_t>(stages) & ~static_cast<std::uint32_t>(allowedStages)))
            return "RHI-COMPLETED-SHADER-TEXTURE-VIEW-ACCESS";
        const bool root = isRootRead(planVersion);
        const bool nativeCube = (planVersion == 8u || planVersion == 17u || planVersion == 18u)
            && OriginalCompletedColorTextureViewContract::isSingleCube(texture, view);
        if (texture.extent.depth != 1u || (!root && texture.arrayLayerCount != 1u)
            || texture.sampleCount != 1u || (texture.cubeCompatible && !nativeCube)
            || (texture.usage & TextureUsage::Sampled) == TextureUsage::None
            || (texture.format != TextureFormat::Rgba8Unorm
                && ((root && planVersion != 17u && planVersion != 18u)
                    || texture.format != TextureFormat::Rgba16Float))
            || (view.dimension != TextureViewDimension::Texture2D
                && (!root || view.dimension != TextureViewDimension::Texture2DArray) && !nativeCube)
            || (view.dimension == TextureViewDimension::Texture2D && texture.arrayLayerCount != 1u)
            || view.usage != TextureUsage::Sampled || view.format != texture.format
            || view.aspect != TextureAspect::Color)
            return "RHI-COMPLETED-SHADER-TEXTURE-VIEW-SHAPE";
        const char* diagnostic = nullptr;
        // Two uint32 fields multiply exactly in uint64. This is the declared
        // whole color-view geometry, not an inferred native/device capacity.
        const auto cells = std::uint64_t(texture.mipLevelCount) * texture.arrayLayerCount;
        const auto geometry =
            semantics::OriginalTextureStateRange::capture(texture, cells, diagnostic);
        if (!geometry)
            return diagnostic;
        return geometry->isWholeAspect({view.baseMipLevel,
                   view.mipLevelCount,
                   view.baseArrayLayer,
                   view.arrayLayerCount,
                   view.aspect})
            ? nullptr
            : "RHI-COMPLETED-SHADER-TEXTURE-VIEW-RANGE";
    }
};

// ContentPlan2's allocation-free cell timeline. The caller projects frozen
// original pass/shader intent; these values never grant mutation or completion.
class OriginalTextureContentCellSequence final {
public:
    enum class Action {
        Clear,
        Load,
        DontCare,
        Store,
        Discard,
        SampledRead,
        ReadbackRead,
        StorageWrite
    };

    struct Step {
        GpuResourceId resource;
        std::uint64_t resourceGeneration;
        TextureSubresourceRange range;
        Action action;
        std::size_t passBegin;
    };

    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t maxCells = 0u, maxSteps = 0u, maxCommandVisits = 0u;
    };

    struct Result {
        const char* diagnostic = nullptr;
        bool stored = false, attachmentActive = false, tentative = false;
        std::uint64_t steps = 0u, commandVisits = 0u, writes = 0u;

        bool nativeExecutionProven() const noexcept { return false; }
    };

    static constexpr auto lowering(Backend backend, std::uint32_t version = 2u) noexcept
    {
        return semantics::OriginalCommandStateAdmissionLowering::textureContentSequenceLowering(
            backend, version);
    }

    template <class StepAt>
    static Result inspect(const semantics::OriginalTextureStateRange& geometry,
        GpuResourceId resource,
        std::uint64_t generation,
        std::uint64_t cell,
        bool initiallyStored,
        std::size_t beforeCommand,
        std::size_t commandCount,
        const Budget& budget,
        StepAt&& stepAt) noexcept
    {
        static_assert(noexcept(stepAt(std::size_t{})), "original projection must not throw");
        return inspectProjected(geometry,
            resource,
            generation,
            cell,
            initiallyStored,
            beforeCommand,
            commandCount,
            budget,
            [&](std::size_t command, auto&& emit) noexcept -> const char* {
                const auto step = stepAt(command);
                return step ? emit(*step) : nullptr;
            });
    }

    // Multiple original attachment/shader contributions can share one command.
    // Keep real command ordinals: a synthetic flattened ordinal would corrupt
    // Begin/End identity and shader-read ordering.
    template <class Visit>
    static Result inspectProjected(const semantics::OriginalTextureStateRange& geometry,
        GpuResourceId resource,
        std::uint64_t generation,
        std::uint64_t cell,
        bool initiallyStored,
        std::size_t beforeCommand,
        std::size_t commandCount,
        const Budget& budget,
        Visit&& visit) noexcept
    {
        Result result;
        const auto fail = [&](const char* id) noexcept {
            // No partial coverage or counts are published on failure.
            return Result{id};
        };
        if (budget.contractVersion != 2u && budget.contractVersion != 3u
            && budget.contractVersion != 4u)
            return fail("RHI-ORIGINAL-CONTENT-SEQUENCE-VERSION");
        if (!budget.prototypeOptIn)
            return fail("RHI-ORIGINAL-CONTENT-SEQUENCE-OPT-IN");
        if (!budget.maxCells || !budget.maxSteps || !budget.maxCommandVisits)
            return fail("RHI-ORIGINAL-CONTENT-SEQUENCE-BUDGET");
        if (!resource.isValid() || !generation || beforeCommand > commandCount
            || cell >= geometry.cellCount())
            return fail("RHI-ORIGINAL-CONTENT-SEQUENCE-INPUT");
        if (geometry.cellCount() > budget.maxCells)
            return fail("RHI-ORIGINAL-CONTENT-SEQUENCE-CELL-CAPACITY");
        if (beforeCommand > budget.maxCommandVisits)
            return fail("RHI-ORIGINAL-CONTENT-SEQUENCE-VISIT-CAPACITY");
        result.stored = initiallyStored;
        std::size_t activePass = SIZE_MAX;
        for (std::size_t command = 0u; command < beforeCommand; ++command) {
            ++result.commandVisits;
            bool shaderRead = false, storageWrite = false;
            const auto consume = [&](const Step& value) noexcept -> const char* {
                const auto* step = &value;
                if (step->resource != resource)
                    return nullptr;
                if (step->resourceGeneration != generation)
                    return "RHI-ORIGINAL-CONTENT-SEQUENCE-GENERATION";
                if (const auto* id = geometry.validate(step->range))
                    return id;
                if (step->action == Action::ReadbackRead && budget.contractVersion != 3u
                    && budget.contractVersion != 4u)
                    return "RHI-ORIGINAL-CONTENT-SEQUENCE-READBACK-PROFILE";
                if (step->action == Action::StorageWrite && budget.contractVersion != 4u)
                    return "RHI-ORIGINAL-CONTENT-SEQUENCE-STORAGE-PROFILE";
                if (!geometry.containsCell(step->range, cell))
                    return nullptr;
                if (result.steps == budget.maxSteps)
                    return "RHI-ORIGINAL-CONTENT-SEQUENCE-STEP-CAPACITY";
                ++result.steps;
                switch (step->action) {
                case Action::StorageWrite:
                    // Sequence4 never infers full coverage from a dispatch or a
                    // shader store. Preserve authentic initialized cell contents;
                    // its unchanged texels remain defined. State/hazard admission
                    // still belongs to the original graph, never this timeline.
                    if (step->passBegin != SIZE_MAX)
                        return "RHI-ORIGINAL-CONTENT-SEQUENCE-STORAGE-PASS";
                    if (step->range.aspect != TextureAspect::Color)
                        return "RHI-ORIGINAL-CONTENT-SEQUENCE-STORAGE-ASPECT";
                    if (result.attachmentActive)
                        return "RHI-ORIGINAL-CONTENT-SEQUENCE-WRITE-ACTIVE-ATTACHMENT";
                    if (!result.stored)
                        return "RHI-ORIGINAL-CONTENT-SEQUENCE-WRITE-UNINITIALIZED";
                    if (shaderRead)
                        return "RHI-ORIGINAL-CONTENT-SEQUENCE-STORAGE-FEEDBACK";
                    storageWrite = true;
                    ++result.writes;
                    break;
                case Action::Clear:
                case Action::Load:
                case Action::DontCare:
                    if (result.attachmentActive || step->passBegin != command)
                        return "RHI-ORIGINAL-CONTENT-SEQUENCE-BEGIN";
                    if (step->action == Action::Load && !result.stored)
                        return "RHI-ORIGINAL-CONTENT-SEQUENCE-LOAD-UNINITIALIZED";
                    result.tentative = step->action == Action::Clear
                        || (step->action == Action::Load && result.stored);
                    result.attachmentActive = true;
                    result.stored = false;
                    activePass = command;
                    break;
                case Action::Store:
                case Action::Discard:
                    if (!result.attachmentActive || step->passBegin != activePass)
                        return "RHI-ORIGINAL-CONTENT-SEQUENCE-END";
                    ++result.writes;
                    result.stored = step->action == Action::Store && result.tentative;
                    result.attachmentActive = result.tentative = false;
                    activePass = SIZE_MAX;
                    break;
                case Action::ReadbackRead:
                    if (step->passBegin != SIZE_MAX)
                        return "RHI-ORIGINAL-CONTENT-SEQUENCE-READBACK-PASS";
                    [[fallthrough]];
                case Action::SampledRead:
                    if (step->action == Action::SampledRead) {
                        if (storageWrite)
                            return "RHI-ORIGINAL-CONTENT-SEQUENCE-STORAGE-FEEDBACK";
                        shaderRead = true;
                    }
                    if (result.attachmentActive)
                        return "RHI-ORIGINAL-CONTENT-SEQUENCE-READ-ACTIVE-ATTACHMENT";
                    if (!result.stored)
                        return "RHI-ORIGINAL-CONTENT-SEQUENCE-READ-UNINITIALIZED";
                    break;
                default:
                    return "RHI-ORIGINAL-CONTENT-SEQUENCE-ACTION";
                }
                return nullptr;
            };
            if (const auto* id = visit(command, consume))
                return fail(id);
        }
        if (beforeCommand == commandCount && result.attachmentActive)
            return fail("RHI-ORIGINAL-CONTENT-SEQUENCE-UNCLOSED-ATTACHMENT");
        return result;
    }
};

// endregion original_texture_content_creation

// region normalized_texture_state_creation

// NormalizedTextureStateCreation/1. Descriptor-authored dimensions fix both
// arrays at creation. Allocation headers/owner bytes remain charged separately
// by core; storage never grows during recording or rollback.
class NormalizedTextureStateCreationContract final {
public:
    static constexpr std::uint32_t version = 1u;

    struct Plan {
        semantics::OriginalTextureStateRange geometry;
        std::uint64_t storageBytes;
    };

    static std::optional<Plan> inspect(
        const TextureDesc& desc, std::uint64_t maxCells, const char*& diagnostic) noexcept
    {
        const auto geometry =
            semantics::OriginalTextureStateRange::capture(desc, maxCells, diagnostic);
        if (!geometry)
            return {};
        if (!semantics::isTextureResourceState(desc.format, desc.initialState)) {
            diagnostic = "RHI-RESOURCE-STATE-INVALID";
            return {};
        }
        constexpr auto bytesPerCell = 2u * sizeof(ResourceState);
        if (geometry->cellCount() > UINT64_MAX / bytesPerCell) {
            diagnostic = "RHI-TEXTURE-STATE-STORAGE-OVERFLOW";
            return {};
        }
        return Plan{*geometry, geometry->cellCount() * bytesPerCell};
    }

    struct Lowering {
        bool normalizedControl;
        bool validatorOnly;
        bool nativeExecutionAvailable;
        const char* diagnostic;
    };

    static constexpr Lowering lowering(Backend backend) noexcept
    {
        return semantics::ResourceLowering::NormalizedTextureStateCreation_lowering<Lowering>(
            backend);
    }
};

// endregion normalized_texture_state_creation

// region original_texture_content_staging

class OriginalTextureContentStagingContract final {
public:
    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint32_t maxRetainedOwners = 0u;
        std::uint64_t maxCpuMetadataBytes = 0u;
    };
    enum class Disposition { ValidatorOnly, Prototype, DiagnosticFailure };

    struct Decision {
        Disposition disposition = Disposition::DiagnosticFailure;
        bool prerequisiteAvailable = false;
        const char* diagnostic = "RHI-CANONICAL-CONTENT-STAGING-BACKEND-UNKNOWN";
    };

    static constexpr Decision lowering(Backend backend) noexcept
    {
        return semantics::ResourceLowering::OriginalTextureContentStaging_lowering<Decision,
            Disposition>(backend);
    }
};
} // namespace arp::rhi

// endregion original_texture_content_staging
