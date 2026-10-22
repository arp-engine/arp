#pragma once

#include "core/memory/cpu_allocator.h"
#include "core/ownership/lifetime_epoch.h"
#include "rhi/frontend/bindless_binding_budget.h"
#include "rhi/gpu_commands.h"
#include "rhi/gpu_descriptors.h"
#include "rhi/semantics/bindless_workload_resource_types.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace arp::rhi_metal {
class MetalRhiClosedConsumerGeometry;
class MetalRhiClosedConsumerImages;
} // namespace arp::rhi_metal

namespace arp::rhi {

// BindlessCross/1 deliberately separates optional indexed access from the
// older DescriptorTable flag. A discovered extension does not enable it.
inline constexpr std::uint32_t kBindlessContractVersion = 1;
inline constexpr std::size_t kBindlessResourceClasses = 6;
inline constexpr std::size_t kBindlessShaderStages = 3;

enum class BindlessExecution { DiagnosticFailure, Validator, Native, Emulated };
enum class BindlessFaultValidationStatus {
    Pending,
    Clean,
    Faulted,
    ReadbackFailed,
    Abandoned,
    NotExecuted,
    Unavailable,
};
// A validation frame is clean only after a backend has proved GPU completion
// and read back its fault latch. Pending/failed/abandoned work fails closed.
ARP_API ValidationResult validateBindlessFaultStatus(BindlessFaultValidationStatus) noexcept;

struct BindlessFaultReport {
    std::uint64_t snapshotGeneration = 0u;
    BindlessFaultValidationStatus status = BindlessFaultValidationStatus::Unavailable;
    std::uint32_t faultBits = 0u;
    ValidationResult validate() const noexcept;
};

// A validation generation succeeds only when every contributing operation has
// a completion-proven Clean result. A zero-instance consumer cannot hide faults.
ARP_API BindlessFaultReport mergeBindlessFaultReports(
    BindlessFaultReport, BindlessFaultReport) noexcept;
enum class BindlessBindingModel { DescriptorArrays, ScalarExpansion };
enum class BindlessPolicy { Auto, Required, Disabled };
enum class BindlessOperation : std::uint32_t {
    None = 0,
    Load = 1u << 0,
    Query = 1u << 1,
    SampleLevel = 1u << 2,
    SampleGrad = 1u << 3,
    Store = 1u << 4,
    Atomic = 1u << 5,
    SampleCompare = 1u << 6,
};

inline BindlessOperation operator|(BindlessOperation a, BindlessOperation b)
{
    return static_cast<BindlessOperation>(
        static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

struct BindlessBufferLayout {
    std::uint64_t identity = 0;
    std::uint32_t elementStrideBytes = 0;
    std::uint32_t elementAlignmentBytes = 0;
    std::uint64_t minBindingSizeBytes = 0;
};

struct BindlessTableRequirement {
    std::uint32_t tableId = 0;
    DescriptorBindingDesc binding;
    std::uint32_t resourceCount = 0;
    BindlessOperation operations = BindlessOperation::None;
    bool nonUniform = false;
    BindlessBufferLayout bufferLayout;
};

struct BindlessBudget {
    std::uint32_t maxSnapshots = 0;
    std::uint32_t maxUpdatesPerSnapshot = 0;
    std::uint64_t maxResidentBytes = 0;
    std::uint32_t maxSwitchCases = 0;
};

// Independent selected Graphics push pipeline intent. It does not widen the
// legacy factory or grant command/submit authority. Limits come from the device.
struct BindlessGraphicsPushRequest {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxBytes = 0u;

    constexpr const char* diagnostic() const noexcept
    {
        if (contractVersion != 1u)
            return "RHI-BINDLESS-GRAPHICS-PUSH-REQUEST-VERSION";
        if (!prototypeOptIn)
            return "RHI-BINDLESS-GRAPHICS-PUSH-REQUEST-OPT-IN";
        return maxBytes && maxBytes <= kL2AtomicPushConstantLimitBytes
            ? nullptr
            : "RHI-BINDLESS-GRAPHICS-PUSH-REQUEST-BUDGET";
    }
};

struct BindlessRequirements {
    std::uint32_t contractVersion = kBindlessContractVersion;
    bool optIn = false;
    bool allowEmulated = false;
    std::uint64_t deviceIdentity = 0;
    std::uint64_t shaderPackageIdentity = 0;
    BindlessBudget budget;
    core::pmr::Vector<BindlessTableRequirement> tables{&core::defaultCpuMemoryResource()};

    // Explicit original ownership/payload preparation only. It never selects
    // an executable application mode or authorizes native commands.
    struct OriginalPreparation {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;

        bool empty() const noexcept { return !contractVersion && !prototypeOptIn; }
    } originalPreparation;

    // Independent original Root execution intent. Preparation cannot be
    // promoted in place: this profile needs a new plan/snapshot/authorization
    // and a separately published device capability plus fault completion.
    struct OriginalExecution {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;

        bool empty() const noexcept { return !contractVersion && !prototypeOptIn; }
    } originalExecution;
};

struct BindlessStageLimits {
    std::uint32_t sampledTextures = 0;
    std::uint32_t samplers = 0;
    std::uint32_t storageTextures = 0;
    std::uint32_t uniformBuffers = 0;
    // Read-only and writable storage buffers share the same native limit.
    std::uint32_t storageBuffers = 0;
};

struct BindlessResourceSupport {
    BindlessOperation operations = BindlessOperation::None;
    bool nonUniform = false;
};

struct BindlessCapabilities {
    std::uint32_t contractVersion = kBindlessContractVersion;
    std::uint64_t deviceIdentity = 0;
    BindlessExecution execution = BindlessExecution::DiagnosticFailure;
    bool limitsQueried = false;
    bool executablePayload = false;
    std::uint32_t originalPreparationContractVersion = 0u;
    // Independent cold scalar Graphics profile /2. This is not executable,
    // does not widen Compute /1 and cannot select an application mode.
    std::uint32_t originalGraphicsPreparationContractVersion = 0u;
    // Independent cold /3: Fragment sampled 2D/2DArray + independent samplers
    // and scalar buffers. Does not grant content, command or completion.
    std::uint32_t originalSampledGraphicsPreparationContractVersion = 0u;
    // Independent cold /4 depth Snapshot only (Compute or Fragment). It does
    // not grant authorization, pipelines, prepared bindings or Root execution.
    std::uint32_t originalDepthSnapshotPreparationContractVersion = 0u;
    // Independent /5: authentic depth input, pipeline and prepared Root only.
    // Shader state, command execution and completion remain independently gated.
    std::uint32_t originalDepthRootPreparationContractVersion = 0u;
    // Independent cold Native /6: single-cell Source2/Publication4 depth Root.
    // No recording, submission, ShaderRead Pending or completion authority.
    std::uint32_t originalNativeDepthRootPreparationContractVersion = 0u;
    // Independent /7: cold Compute RGBA16F WriteOnly and sampled single-mip
    // cell snapshots. Native arrays or bounded scalar Emulated bindings only;
    // no authorization, command, content initialization or completion grant.
    std::uint32_t originalStoragePreparationContractVersion = 0u;
    // Independent /8 authorizes cold verified storage pipelines and bindings,
    // never record/submit/completion or application mode selection.
    std::uint32_t originalStorageRootPreparationContractVersion = 0u;
    // Independent /9: one cold snapshot retains completed D32 inputs beside
    // RGBA16F storage/sampled cells. No authorization or execution grant.
    std::uint32_t originalMixedImageSnapshotPreparationContractVersion = 0u;
    // /10 authenticates and prepares Compute entries over the /9 mixed image
    // domain. It grants neither recording nor completion authority.
    std::uint32_t originalMixedImageRootPreparationContractVersion = 0u;
    // Independent cold scalar ordinary color Graphics domain; never execution.
    std::uint32_t originalOrdinaryColorGraphicsPreparationContractVersion = 0u;
    // Independent /12 cold scalar ordinary Fragment depth/comparison Graphics.
    // No resource closure, command recording or completion authority.
    std::uint32_t originalOrdinaryDepthGraphicsPreparationContractVersion = 0u;
    // Independent /13 canonical ordinary color Graphics callback execution.
    // Kept zero until the original backend supplies the complete payload.
    std::uint32_t originalOrdinaryColorGraphicsExecutionContractVersion = 0u;
    // Independent canonical Fragment depth/color callback profile. Generic
    // Emulated support and cold Preparation12 cannot infer this authority.
    std::uint32_t originalOrdinaryDepthGraphicsExecutionContractVersion = 0u;
    std::uint32_t originalInitializedGeometryExecutionContractVersion = 0u;
    std::uint32_t originalNativeDepthComputeExecutionContractVersion = 0u;
    // Independent /7 native buffer Compute Root, original lifecycle6. This
    // grants no depth/image, Graphics, indirect or callback-ticket domain.
    std::uint32_t originalNativeBufferComputeExecutionContractVersion = 0u;
    // Independent /8 completed color Compute Root. Publication/1, ContentPlan/8,
    // lifecycle8 and capacity4; no implicit derivatives or writable images.
    std::uint32_t originalNativeColorComputeExecutionContractVersion = 0u;
    std::uint32_t originalNativeStorageComputeExecutionContractVersion = 0u;
    // Independent bounded callback storage execution over Content14. Native9
    // fences and cold Preparation8 cannot authorize this public execution.
    std::uint32_t originalEmulatedStorageComputeExecutionContractVersion = 0u;
    // Independent three-entry mixed depth/color execution. Kept zero until
    // the original command backend supplies its real completion payload.
    std::uint32_t originalNativeMixedComputeExecutionContractVersion = 0u;
    std::uint32_t originalEmulatedMixedComputeExecutionContractVersion = 0u;
    // Closed four-pass visibility producer -> finite indexed-indirect consumer
    // over Root6/Content9/Geometry2. Native15 and Emulated16 have independent
    // admission and real fence/callback completion; older profiles grant none.
    std::uint32_t originalNativeProducedGeometryExecutionContractVersion = 0u;
    std::uint32_t originalEmulatedProducedGeometryExecutionContractVersion = 0u;
    // Independent Compute depth Root execution: original cell content/state,
    // genuine queue/scope/map completion and retirement, not general Emulated.
    std::uint32_t originalDepthComputeExecutionContractVersion = 0u;
    // Independent Fragment-buffer Graphics Root callback execution; not the
    // Vulkan Graphics adapter or general scene execution.
    std::uint32_t originalFragmentGraphicsExecutionContractVersion = 0u;
    // Independent /3 completed 2D sampled Fragment execution. Cold /3 never
    // grants this; real original upload, native closure and completion required.
    std::uint32_t originalSampledFragmentGraphicsExecutionContractVersion = 0u;
    // Independent /4: completed whole 2DArray views, canonical upload/control1,
    // Fragment load/query/sampleLevel/sampleGrad. Earlier profiles stay closed.
    std::uint32_t originalArrayFragmentGraphicsExecutionContractVersion = 0u;
    // Buffer-only Compute Root execution, not general Emulated support. Zero
    // until original recording/submission/fault/map/retirement are validated.
    std::uint32_t originalExecutionContractVersion = 0u;
    // Scoped public adapter to the original Graphics/9 issuer, separately
    // gated from general scene coverage and OriginalExecution/1 Compute.
    std::uint32_t originalGraphicsExecutionContractVersion = 0u;
    // Independent public Graphics/2 ordinary completed-depth execution.
    std::uint32_t originalGraphicsDepthExecutionContractVersion = 0u;
    // Scoped selected Graphics push pipeline/recording support, independently
    // queried. Zero keeps the new explicit overload unavailable.
    std::uint32_t originalGraphicsPushContractVersion = 0u;
    // True only when the created device enabled indirect-first-instance.
    // This fact alone never authorizes bindless indirect execution.
    bool indirectFirstInstance = false;
    // Exact scalar Vertex mapping reader + Fragment fault forwarding subset.
    // Independent of GPU indirect argument production and draw authorization.
    std::uint32_t indirectInstanceConsumerContractVersion = 0;
    // Closed compiler kernel only; never grants zero-base draw authorization.
    std::uint32_t zeroBaseIndirectProducerContractVersion = 0;
    // Protected packet allocation only, independent of producer/draw execution.
    std::uint32_t indirectPacketStorageContractVersion = 0;
    // Backend-owned zeroing, GPU-completion readback and validation-frame
    // failure. A compiler package cannot grant this device capability.
    std::uint32_t faultReadbackContractVersion = 0;
    // Pipeline-specific immutable Compute bindings prepared outside recording.
    // Null reports validator ownership only; execution is a separate gate.
    std::uint32_t preparedComputeBindingContractVersion = 0;
    std::uint32_t preparedGraphicsBindingContractVersion = 0;
    std::uint32_t preparedGraphicsPassFaultScopeContractVersion = 0;
    std::uint32_t maxBindGroups = 0;
    std::uint32_t maxBindingsPerGroup = 0;
    std::array<BindlessStageLimits, kBindlessShaderStages> stageLimits{};
    // Extra shared physical texture namespace ceiling, per stage. Absent
    // declares no additional constraint; present zero forbids image resources.
    std::array<std::optional<std::uint32_t>, kBindlessShaderStages>
        combinedSampledStorageTextureLimits{};
    std::array<std::array<BindlessResourceSupport, kBindlessResourceClasses>, kBindlessShaderStages>
        support{};
    std::uint64_t maxUniformBufferBindingSize = 0;
    std::uint64_t maxStorageBufferBindingSize = 0;
    // Query facts only. Zero means unavailable and cannot admit a finite view.
    std::uint64_t uniformBufferOffsetAlignment = 0;
    std::uint64_t storageBufferOffsetAlignment = 0;
    // OriginalDeviceResourcePreflight/1: cached actual device size/shape facts.
    // Zero version/fact is unavailable. Metal alignment remains pipeline-owned.
    semantics::BindlessWorkloadResourceLimits workloadResourceLimits;
    // PipelineInterface/1 ordinary scalar descriptor domains. Each resource
    // class is admitted independently; Root/2 closure and genuine completion
    // are still required by the original command path.
    std::array<bool, kBindlessResourceClasses> ordinaryDescriptorDomains{};
};

// Compiler-owned mapping. Scalars are expanded only for Emulated; Native and
// Validator use one array binding. One artifact describes a complete binding
// domain, including ordinary bindings so reservations cannot be forgotten.
struct BindlessTableMapping {
    std::uint32_t tableId = 0;
    std::uint32_t firstBinding = 0;
    std::uint32_t capacity = 0;
    // Exact operation set used by this shader package, including non-uniform
    // indexing. A workload cannot underdeclare writes or side effects.
    BindlessOperation operations = BindlessOperation::None;
    bool nonUniform = false;
    BindlessBufferLayout bufferLayout;
    // Independent sampler table used by generated sampling helpers, or zero.
    std::uint32_t samplerTableId = 0;
    // SnapshotMetadata/1 word offset of the table header (zero means legacy).
    std::uint32_t metadataWordOffset = 0;
};

struct BindlessSnapshotMetadataLayout {
    std::uint32_t contractVersion = 0;
    std::uint32_t binding = 0;
    std::uint32_t wordCount = 0;
    ShaderStage stages = ShaderStage::None;
};

struct BindlessFaultLatchLayout {
    std::uint32_t contractVersion = 0;
    std::uint32_t binding = 0;
    ShaderStage stages = ShaderStage::None;
};

struct BindlessIndirectInstanceConsumerLayout {
    std::uint32_t contractVersion = 0;
    std::uint32_t binding = 0;
    std::uint32_t recordIndex = 0;
    std::uint32_t instanceCapacity = 0;
};

// PipelineInterface/1 retains the exact ordinary descriptor domains beside
// the indexed domain. These layouts describe shader ABI only; they do not
// retain resources or grant recording/submission/completion authority.
struct BindlessPipelineDescriptorDomain {
    std::uint32_t set = 0;
    DescriptorLayoutDesc layout{core::pmr::String{&core::defaultCpuMemoryResource()},
        core::pmr::Vector<DescriptorBindingDesc>{&core::defaultCpuMemoryResource()}};

    BindlessPipelineDescriptorDomain() = default;

    BindlessPipelineDescriptorDomain(const BindlessPipelineDescriptorDomain& other)
        : set(other.set),
          layout{core::pmr::String{other.layout.name, &core::defaultCpuMemoryResource()},
              core::pmr::Vector<DescriptorBindingDesc>{
                  other.layout.bindings, &core::defaultCpuMemoryResource()}}
    {}

    BindlessPipelineDescriptorDomain(BindlessPipelineDescriptorDomain&&) noexcept = default;
    BindlessPipelineDescriptorDomain& operator=(const BindlessPipelineDescriptorDomain&) = default;
    BindlessPipelineDescriptorDomain& operator=(
        BindlessPipelineDescriptorDomain&&) noexcept = default;
};

struct BindlessShaderArtifact {
    std::uint32_t contractVersion = kBindlessContractVersion;
    std::uint64_t shaderPackageIdentity = 0;
    BindlessExecution execution = BindlessExecution::DiagnosticFailure;
    std::uint32_t set = 0;
    DescriptorLayoutDesc physicalLayout{core::pmr::String{&core::defaultCpuMemoryResource()},
        core::pmr::Vector<DescriptorBindingDesc>{&core::defaultCpuMemoryResource()}};
    core::pmr::Vector<BindlessTableMapping> tables{&core::defaultCpuMemoryResource()};
    // Exact compiler report, including independent sampler combinations.
    std::uint32_t switchCases = 0;
    std::uint64_t snapshotBytes = 0;
    // Other descriptor domains in the same pipeline, copied from reflection.
    // This domain's ordinary bindings are already in physicalLayout.
    std::array<BindlessStageLimits, kBindlessShaderStages> reservedPerStage{};
    std::uint32_t otherBindGroups = 0;
    // Zero preserves the historical reservation-only validator contract.
    // /1 derives reservations from additionalDomains; the two historical
    // reservation fields above must be zero (there is only one fact source).
    std::uint32_t pipelineInterfaceContractVersion = 0;
    core::pmr::Vector<BindlessPipelineDescriptorDomain> additionalDomains{
        &core::defaultCpuMemoryResource()};
    // SnapshotActivePrefix/2 reads bounds from immutable snapshot metadata.
    // /1 is retained only for handwritten Null validator artifacts; sealed
    // compiler packages require /2. Zero requires fully populated tables.
    std::uint32_t activePrefixContractVersion = 0;
    BindlessSnapshotMetadataLayout snapshotMetadata;
    // Compiler-generated atomic OR sink. This does not imply that a backend
    // clears, retains or reads the sink after GPU completion.
    BindlessFaultLatchLayout faultLatch;
    // Compiler guarantee that every rejected indexed operation writes the
    // reserved fault latch. Backend completion/readback is a separate gate.
    std::uint32_t faultReportingContractVersion = 0;
    // Compiler encoder/decoder ABI only; independent from indirect execution.
    std::uint32_t indirectInstanceBaseContractVersion = 0;
    // Guarded scalar Vertex reader only, never an indirect execution ticket.
    BindlessIndirectInstanceConsumerLayout indirectInstanceConsumer;
    std::uint32_t zeroBaseIndirectProducerContractVersion = 0;
    // Closed compiler source/package provenance only; never execution support.
    std::uint32_t zeroBaseIndirectConsumerContractVersion = 0;
};

struct BindlessPipelineInterfaceSummary {
    std::array<BindlessStageLimits, kBindlessShaderStages> additionalPerStage{};
    std::uint64_t descriptorCount = 0;
    std::uint32_t bindGroupCount = 0;
};

// Complete compiler layout demand before device creation. This never grants
// execution or authenticates a package, and excludes live/in-flight byte quotas.
// Sparse sets require highest-set+1 slots. Emulated binding demand also includes
// highest-scalar-binding+1; native binding numbers are not descriptor counts.
struct BindlessPipelineInterfaceDemand {
    std::array<BindlessStageLimits, kBindlessShaderStages> perStage{};
    std::uint64_t descriptorCount = 0u;
    std::uint32_t domainCount = 0u;
    std::uint32_t bindGroupSlots = 0u;
    std::uint32_t bindingsPerGroup = 0u;
};

ARP_API const char* inspectBindlessPipelineInterfaceDemand(
    const BindlessShaderArtifact&, BindlessPipelineInterfaceDemand&) noexcept;

// Allocation-free, complete-interface validation and device-limit accounting.
// Success is a layout/budget proof, never an execution ticket. Output is reset
// on every failure. Snapshot update budgets remain local to the indexed set.
ARP_API const char* inspectBindlessPipelineInterface(const BindlessCapabilities&,
    const BindlessShaderArtifact&,
    BindlessPipelineInterfaceSummary&) noexcept;

struct BindlessLoweringPlan {
    std::uint32_t contractVersion = kBindlessContractVersion;
    BindlessExecution execution = BindlessExecution::DiagnosticFailure;
    BindlessBindingModel bindingModel = BindlessBindingModel::DescriptorArrays;
    bool success = false;
    bool executable = false;
    std::uint32_t originalPreparationContractVersion = 0u;
    std::uint32_t originalExecutionContractVersion = 0u;
    std::uint64_t deviceIdentity = 0;
    std::uint64_t shaderPackageIdentity = 0;
    // Stable digest of the complete validated intent and physical map. Not an
    // execution ticket: backend ownership is still required for Native modes.
    std::uint64_t identity = 0;
    BindlessBudget budget;
    std::uint64_t snapshotBytes = 0;
    std::uint64_t descriptorCount = 0;
    const char* diagnosticId = "RHI-BINDLESS-NOT-PLANNED";
    const char* telemetryLabel = "DiagnosticFailure";
};

// Index in support[], or kBindlessResourceClasses for an unsupported type.
ARP_API std::size_t bindlessResourceClass(DescriptorType) noexcept;
// Only successful, fully matching plans are interchangeable. Labels are not
// authorization fields; an identical digest never substitutes for ownership.
ARP_API bool bindlessPlansMatch(const BindlessLoweringPlan&, const BindlessLoweringPlan&) noexcept;

enum class BindlessSelectionPath { Rejected, Traditional, Indexed };

struct BindlessSelection {
    BindlessSelectionPath path = BindlessSelectionPath::Rejected;
    std::size_t artifactIndex = 0;
    BindlessLoweringPlan plan;
    const char* diagnosticId = "RHI-BINDLESS-NO-MATCHING-VARIANT";
};

// Offline variants only. The request's package identity must be zero here;
// selection resolves it to one candidate before creating the final plan.
// Smallest means total typed capacity, then switch size, then package identity.
// Null plans are deliberately excluded from executable path selection.
namespace semantics {
ARP_API BindlessLoweringPlan planBindless(
    const BindlessCapabilities&, const BindlessRequirements&, const BindlessShaderArtifact&);
ARP_API BindlessSelection selectBindlessVariant(BindlessPolicy,
    const BindlessCapabilities&,
    const BindlessRequirements&,
    const BindlessShaderArtifact* artifacts,
    std::size_t artifactCount);
} // namespace semantics

using semantics::planBindless;
using semantics::selectBindlessVariant;

class BindlessShaderPackage;
class BindlessSnapshot;
class BindlessAuthorization;
class BindlessIndirectPacket;
class BindlessIndirectProcessorIntentPool;
class GpuDevice;
class BindlessIndirectProcessorPool;
class BindlessComputeBindingPool;
class BindlessComputeBindingOwners;
class BindlessPreparedComputeBinding;
class BindlessGraphicsBindingPool;
class BindlessGraphicsBindingOwners;
class BindlessPreparedGraphicsBinding;
class BindlessBindingPreparationPool;
class OriginalBindlessRootExecution;
class OriginalInitializedGeometrySource;
struct OriginalInitializedBufferRequest;
struct OriginalBindlessRootExecutionInputs;
struct OriginalBindlessRootExecutionBudget;
struct OriginalBindlessRootFenceExecutionInputs;
struct OriginalBindlessRootFenceExecutionBudget;
class OriginalBindlessGraphicsExecution;
struct OriginalBindlessGraphicsExecutionInputs;
struct OriginalBindlessGraphicsExecutionBudget;
struct BindlessIndirectProcessorBudget;
class ComputePipeline;
class RenderPipeline;
struct RenderPipelineDesc;
struct RenderPassDesc;
ARP_API ValidationResult validateBindlessRenderPass(
    const RenderPipelineDesc&, const RenderPassDesc&);
struct PipelineValidationReport;
ARP_API ValidationResult validateBindlessRenderPass(
    const PipelineValidationReport&, const RenderPassDesc&);

class ARP_API BindlessCrossExtension {
public:
    BindlessCrossExtension() = default;
    BindlessCrossExtension(const BindlessCrossExtension&) = delete;
    virtual ~BindlessCrossExtension() = default;
    virtual const BindlessCapabilities& capabilities() const noexcept = 0;

    // Cold immutable buffer creation uses the existing HostGeometry/1 and
    // Shader/2 contracts. Query before budgeting; creation never grants work.
    std::uint64_t queryOriginalInitializedBufferAllocation(
        GpuDevice&, const BufferDesc&, std::uint32_t contentVersion, const char*& diagnostic) const;
    std::shared_ptr<const OriginalInitializedGeometrySource> prepareOriginalInitializedBuffer(
        GpuDevice&, const OriginalInitializedBufferRequest&, const char*& diagnostic) const;

    // Cold SnapshotAllocation/1 (Compute) or /2 (also Vertex/Fragment) query.
    // Returns the resident envelope for
    // a local artifact projection, never edits the immutable shader package.
    // Query failure returns zero. Ordinary execution and mode selection are
    // unaffected. /1 needs Preparation9; /2 needs queried device limits and
    // authentic backend allocation queries, independent of mixed-image execution.
    std::uint64_t queryOriginalMixedSnapshotAllocation(
        const BindlessShaderArtifact&, std::uint32_t version, const char*& diagnostic) const;

    BindlessLoweringPlan plan(
        const BindlessRequirements& request, const BindlessShaderArtifact& artifact) const
    {
        return planBindless(capabilities(), request, artifact);
    }

    std::shared_ptr<const BindlessAuthorization> authorize(
        std::shared_ptr<const BindlessShaderPackage>,
        const BindlessSnapshot&,
        core::pmr::String& diagnostic) const;
    virtual core::UniquePtr<ComputePipeline> createComputePipeline(
        std::shared_ptr<const BindlessAuthorization>,
        std::string_view entry,
        core::pmr::String& diagnostic) const;
    // State only: layout/package/shader fields must be empty. Entries come
    // from the retained authorized package, never caller-authored reflection.
    virtual core::UniquePtr<RenderPipeline> createRenderPipeline(
        std::shared_ptr<const BindlessAuthorization>,
        const RenderPipelineDesc& state,
        std::string_view vertexEntry,
        std::string_view fragmentEntry,
        core::pmr::String& diagnostic) const;
    virtual core::UniquePtr<RenderPipeline> createRenderPipeline(
        std::shared_ptr<const BindlessAuthorization>,
        const RenderPipelineDesc& state,
        std::string_view vertexEntry,
        std::string_view fragmentEntry,
        const BindlessGraphicsPushRequest&,
        core::pmr::String& diagnostic) const;
    virtual core::UniquePtr<BindlessIndirectProcessorPool> createIndirectProcessorPool(
        GpuDevice&, const BindlessIndirectProcessorBudget&, core::pmr::String& diagnostic) const;

    // Explicit scoped OriginalExecution/1. This does not widen general scene
    // Emulated capability or authorize a preparation-only snapshot.
    std::shared_ptr<const OriginalBindlessRootExecution> prepareOriginalRootExecution(GpuDevice&,
        const OriginalBindlessRootExecutionInputs&,
        const OriginalBindlessRootExecutionBudget&,
        core::pmr::String& diagnostic) const;

    std::shared_ptr<const OriginalBindlessGraphicsExecution> prepareOriginalGraphicsExecution(
        GpuDevice&,
        const OriginalBindlessGraphicsExecutionInputs&,
        const OriginalBindlessGraphicsExecutionBudget&,
        core::pmr::String& diagnostic) const;

    std::shared_ptr<const OriginalBindlessRootExecution> prepareOriginalRootExecution(GpuDevice&,
        const OriginalBindlessRootFenceExecutionInputs&,
        const OriginalBindlessRootFenceExecutionBudget&,
        core::pmr::String& diagnostic) const;

protected:
    virtual std::shared_ptr<const OriginalInitializedGeometrySource>
    prepareOriginalInitializedBufferPayload(
        GpuDevice&, const OriginalInitializedBufferRequest&, const char*& diagnostic) const;

    virtual std::uint64_t queryOriginalSnapshotBufferAllocation(
        const BufferDesc&, const char*& diagnostic) const
    {
        diagnostic = "RHI-BINDLESS-SNAPSHOT-ALLOCATION-BACKEND";
        return 0u;
    }

    virtual std::shared_ptr<const OriginalBindlessRootExecution>
    prepareOriginalRootExecutionPayload(GpuDevice&,
        const OriginalBindlessRootFenceExecutionInputs&,
        const OriginalBindlessRootFenceExecutionBudget&,
        core::pmr::String& diagnostic) const;
    virtual std::shared_ptr<const OriginalBindlessGraphicsExecution>
    prepareOriginalGraphicsExecutionPayload(GpuDevice&,
        const OriginalBindlessGraphicsExecutionInputs&,
        const OriginalBindlessGraphicsExecutionBudget&,
        core::pmr::String& diagnostic) const;
    virtual std::shared_ptr<const OriginalBindlessRootExecution>
    prepareOriginalRootExecutionPayload(GpuDevice&,
        const OriginalBindlessRootExecutionInputs&,
        const OriginalBindlessRootExecutionBudget&,
        core::pmr::String& diagnostic) const;
    friend class BindlessBindingPreparationPool;
    friend class BindlessGraphicsBindingPool;
    virtual std::shared_ptr<const BindlessPreparedGraphicsBinding> prepareGraphicsBinding(
        std::shared_ptr<const BindlessGraphicsBindingOwners>,
        const BindlessGraphicsBindingBudget&,
        core::pmr::String& diagnostic) const;
    friend class BindlessComputeBindingPool;
    virtual std::shared_ptr<const BindlessPreparedComputeBinding> prepareComputeBinding(
        std::shared_ptr<const BindlessComputeBindingOwners>,
        const BindlessComputeBindingBudget&,
        core::pmr::String& diagnostic) const;
    friend class BindlessSnapshot;
    friend class BindlessIndirectPacketPool;
    friend class BindlessIndirectProcessorIntentPool;

    // Preparation validates ownership only; producer execution remains an
    // independent capability and must retain its own approved native payload.
    virtual ValidationResult validateIndirectProcessorSource(const GpuBuffer&) const
    {
        return {false, "RHI-BINDLESS-INDIRECT-PROCESSOR-PREPARATION-UNAVAILABLE"};
    }

    virtual ValidationResult validateIndirectPacketCreation(const BindlessLoweringPlan&) const
    {
        return {false, "RHI-BINDLESS-INDIRECT-PACKET-STORAGE-UNAVAILABLE"};
    }

    // Final candidate gate: same-device registered storage, complete native
    // extents/usages and write seals. Allocation success alone is insufficient.
    virtual ValidationResult validateIndirectPacketPayload(const BindlessIndirectPacket&) const
    {
        return {false, "RHI-BINDLESS-INDIRECT-PACKET-PAYLOAD-UNAVAILABLE"};
    }

    // A backend may issue snapshots only after it owns the complete physical
    // allocation, upload, fault-collection and completion contract.
    virtual ValidationResult validateSnapshotCreation(const BindlessLoweringPlan&) const
    {
        return {false, "RHI-BINDLESS-SNAPSHOT-EXECUTION-UNAVAILABLE"};
    }

    // Called only after the sealed snapshot reproduces the retained package's
    // plan. The owning backend must prove storage and completion ownership
    // before it may issue an executable authorization.
    virtual ValidationResult validateAuthorization(
        const BindlessShaderPackage&, const BindlessSnapshot&) const
    {
        return {false, "RHI-BINDLESS-AUTHORIZATION-EXECUTION-UNAVAILABLE"};
    }

private:
    friend class arp::rhi_metal::MetalRhiClosedConsumerGeometry;
    friend class arp::rhi_metal::MetalRhiClosedConsumerImages;
    core::LifetimeEpoch lifetime_;
};

// Device-owned discovery surface for backends whose indexed execution path is
// still closed. It never grants a snapshot or executable authorization.
class ARP_API DiagnosticBindlessCrossExtension final : public BindlessCrossExtension {
public:
    DiagnosticBindlessCrossExtension() { caps_.deviceIdentity = allocateGpuResourceId().value; }

    const BindlessCapabilities& capabilities() const noexcept override { return caps_; }

private:
    BindlessCapabilities caps_;
};

struct BindlessResourceHandle {
    std::uint64_t generation = 0;
    std::uint32_t tableId = 0;
    std::uint32_t index = 0;
};

enum class BindlessAccessKind { Buffer, Texture };

struct BindlessResourceAccess {
    GpuResourceId resource;
    BindlessAccessKind kind = BindlessAccessKind::Buffer;
    ShaderStage stages = ShaderStage::None;
    PipelineStage pipelineStages = PipelineStage::None;
    ResourceAccess access = ResourceAccess::None;
    ResourceState state = ResourceState::Undefined;
    std::uint64_t bufferOffset = 0;
    std::uint64_t bufferSize = 0;
    TextureSubresourceRange textureRange;
};

// Synchronously consumed declaration view. Implementations must keep the same
// contents throughout validation; neither snapshots nor commands retain it.
class BindlessAccessDeclarations {
public:
    virtual ~BindlessAccessDeclarations() = default;
    virtual bool valid() const noexcept = 0;
    virtual std::size_t size() const noexcept = 0;
    virtual bool read(std::size_t index, BindlessResourceAccess&) const = 0;
};

class BindlessAccessArray final : public BindlessAccessDeclarations {
public:
    BindlessAccessArray(const BindlessResourceAccess* data, std::size_t size)
        : data_(data), size_(size)
    {}

    bool valid() const noexcept override { return data_ || !size_; }

    std::size_t size() const noexcept override { return size_; }

    bool read(std::size_t index, BindlessResourceAccess& result) const override
    {
        if (!data_ || index >= size_)
            return false;
        result = data_[index];
        return true;
    }

private:
    const BindlessResourceAccess* data_;
    std::size_t size_;
};

class ARP_API BindlessAuthorization final : public core::SharedObjectIdentity {
    class ConstructionKey {
        friend class BindlessCrossExtension;

        // User-provided: '= default' remains aggregate-initializable in C++17.
        ConstructionKey() {}

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };
    friend class BindlessCrossExtension;

public:
    BindlessAuthorization(ConstructionKey,
        const BindlessCrossExtension* issuer,
        core::LifetimeEpoch::Ticket lifetime,
        std::shared_ptr<const BindlessShaderPackage> package,
        BindlessLoweringPlan plan)
        : issuer_(issuer), lifetime_(std::move(lifetime)), package_(std::move(package)), plan_(plan)
    {}

    bool current() const noexcept;

    bool current(const std::shared_ptr<const BindlessAuthorization>& owner) const noexcept
    {
        return sharedOwnerMatches(owner) && current();
    }

    bool issuedBy(const BindlessCrossExtension* issuer) const noexcept
    {
        return current() && issuer == issuer_;
    }

    bool issuedBy(const BindlessCrossExtension* issuer,
        const std::shared_ptr<const BindlessAuthorization>& owner) const noexcept
    {
        return sharedOwnerMatches(owner) && issuedBy(issuer);
    }

    const BindlessLoweringPlan& plan() const noexcept { return plan_; }

    const BindlessShaderPackage& package() const noexcept { return *package_; }

    const std::shared_ptr<const BindlessShaderPackage>& originalPackageOwner() const noexcept
    {
        return package_;
    }

private:
    const BindlessCrossExtension* issuer_;
    core::LifetimeEpoch::Ticket lifetime_;
    std::shared_ptr<const BindlessShaderPackage> package_;
    BindlessLoweringPlan plan_;
};

// Queried on pass encoders using kExtensionBindlessCross. Default backends expose
// no such command interface until both binding and consume gates are implemented.
class BindlessIndirectProcessor;
class BindlessPreparedComputeBinding;

class ARP_API BindlessPassExtension {
public:
    virtual ~BindlessPassExtension() = default;
    virtual std::uint64_t recordingIdentity() const noexcept = 0;

    virtual CommandValidationResult bindPreparedComputeBinding(
        std::shared_ptr<const BindlessPreparedComputeBinding>, const BindlessAccessDeclarations&)
    {
        return {false, "RHI-BINDLESS-PREPARED-COMPUTE-UNAVAILABLE", 0u};
    }

    virtual CommandValidationResult bindPreparedGraphicsBinding(
        std::shared_ptr<const BindlessPreparedGraphicsBinding>, const BindlessAccessDeclarations&)
    {
        return {false, "RHI-BINDLESS-PREPARED-GRAPHICS-UNAVAILABLE", 0u};
    }

    // Fixed record zero, one indexed draw. The issuing backend must prove
    // same-encoder production and exact snapshot/package correspondence.
    virtual CommandValidationResult drawIndirectPacket(const BindlessIndirectProcessor&)
    {
        return {false, "RHI-BINDLESS-INDIRECT-PROCESSOR-EXECUTION-UNAVAILABLE", 0u};
    }

    virtual CommandValidationResult bindSnapshot(std::shared_ptr<const BindlessSnapshot>,
        const BindlessLoweringPlan&,
        std::uint64_t generation,
        const BindlessAccessDeclarations&) = 0;

    CommandValidationResult bindSnapshot(std::shared_ptr<const BindlessSnapshot> snapshot,
        const BindlessLoweringPlan& plan,
        std::uint64_t generation,
        const BindlessResourceAccess* access,
        std::size_t count)
    {
        return bindSnapshot(
            std::move(snapshot), plan, generation, BindlessAccessArray(access, count));
    }
};

// Queried on a finished command buffer. Reports are keyed by immutable
// snapshot generation and remain Pending until completion-gated readback.
class ARP_API BindlessCommandBufferExtension {
public:
    virtual ~BindlessCommandBufferExtension() = default;
    virtual BindlessFaultReport faultReport(std::uint64_t snapshotGeneration) const noexcept = 0;

    virtual BindlessFaultReport processorFaultReport(std::uint64_t) const noexcept
    {
        return {0u, BindlessFaultValidationStatus::Unavailable, 0u};
    }
};

} // namespace arp::rhi
