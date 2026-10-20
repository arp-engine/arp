#pragma once

#include "core/api.h"
#include "core/cpu_allocator.h"
#include "core/lifetime_epoch.h"
#include "rhi/frontend/descriptor_buffer_binding_range.h"
#include "rhi/frontend/descriptor_completed_texture.h"
#include "rhi/frontend/descriptor_original_buffer_range.h"
#include "rhi/frontend/descriptor_texture_cell.h"
#include "rhi/frontend/memory_cache_budget.h"
#include "rhi/gpu_resources.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace arp::rhi {

enum class DescriptorType {
    SampledTexture,
    StorageTexture,
    Sampler,
    UniformBuffer,
    StorageBuffer,
    /// Read-only storage buffer. WebGPU encodes this access mode in the
    /// bind-group layout; Vulkan/Metal/Null retain their storage-buffer form.
    ReadOnlyStorageBuffer,
    /// Tile-local attachment read within a render pass (Vulkan input
    /// attachment / Metal framebuffer fetch). On backends without subpass
    /// support (WebGPU), the FrameGraph compiler auto-lowers this to a
    /// cross-pass sampled texture binding.
    InputAttachment
};

enum class ShaderStage : std::uint32_t {
    None = 0,
    Vertex = 1u << 0,
    Fragment = 1u << 1,
    Compute = 1u << 2,
    TessellationControl = 1u << 3,
    TessellationEvaluation = 1u << 4
};

enum class StorageTextureAccess : std::uint8_t {
    WriteOnly,
    ReadOnly,
    ReadWrite,
};

inline ShaderStage operator|(ShaderStage lhs, ShaderStage rhs)
{
    return static_cast<ShaderStage>(
        static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

inline ShaderStage operator&(ShaderStage lhs, ShaderStage rhs)
{
    return static_cast<ShaderStage>(
        static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs));
}

struct ValidationResult {
    ValidationResult() = default;

    ValidationResult(bool valid, std::string_view message)
        : success(valid), diagnostics(message.empty() ? "" : message.data(),
                              message.size(),
                              &core::defaultCpuMemoryResource())
    {}

    ValidationResult(const ValidationResult& other)
        : success(other.success), diagnostics(other.diagnostics, &core::defaultCpuMemoryResource())
    {}

    ValidationResult(ValidationResult&&) noexcept = default;
    ValidationResult& operator=(const ValidationResult&) = default;
    ValidationResult& operator=(ValidationResult&&) noexcept = default;
    bool success = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

struct DescriptorBindingDesc {
    std::uint32_t binding = 0;
    DescriptorType type = DescriptorType::SampledTexture;
    ShaderStage stages = ShaderStage::None;
    std::uint32_t arrayCount = 1;
    bool tableBacked = false;
    bool dynamicOffset = false;
    bool textureViewDimensionConstrained = false;
    TextureViewDimension textureViewDimension = TextureViewDimension::Texture2D;
    /// SampledDepthView/1: true requires a depth-format, depth-aspect sampled
    /// view on every backend. False adds no depth requirement. InputAttachment
    /// retains its separate depth/color identity contract. Null validates;
    /// Vulkan/Metal validate before native binding; WebGPU also lowers this
    /// requirement to its depth sample type. No backend may bind color as depth.
    bool depthTexture = false;
    /// Storage texture format for StorageTexture bindings. WebGPU requires
    /// the bind group layout to declare a concrete WGPUTextureFormat (it
    /// cannot be inferred from the bound view at bind-group creation time),
    /// so this field is the L2 contract source for the WebGPU lowering.
    /// Vulkan/Metal/Null ignore it - they derive the format from the bound
    /// texture view. Default Unknown is a contract error for StorageTexture
    /// bindings on WebGPU (caught at bind group layout creation).
    TextureFormat storageTextureFormat = TextureFormat::Unknown;
    /// A sampled texture is filterable unless its L2 declaration says
    /// otherwise. For Sampler bindings this same flag declares whether the
    /// sampler is filtering; WebGPU uses it to reject invalid pairings with
    /// depth or unfilterable-float textures. Backends that do not distinguish
    /// the types retain their native sampled-texture lowering. Kept last to
    /// preserve positional aggregate initializers used by existing
    /// descriptor declarations.
    bool sampledTextureFilterable = true;
    /// A sampler used with textureSampleCompare / texture_depth_* resources.
    /// WebGPU validates this at bind-group-layout creation, while Vulkan and
    /// Metal derive it from the sampler state at descriptor binding time.
    bool comparisonSampler = false;
    // Native storage images may support both directions. WebGPU's explicit
    // bind-group layout must match the compiled WGSL access declaration.
    StorageTextureAccess storageTextureAccess = StorageTextureAccess::WriteOnly;
};

struct DescriptorLayoutDesc {
    std::pmr::string name;
    std::pmr::vector<DescriptorBindingDesc> bindings;
};

inline std::uint32_t dynamicOffsetBindingCount(const DescriptorLayoutDesc& layout)
{
    std::uint32_t count = 0u;
    for (const auto& binding : layout.bindings) {
        if (binding.dynamicOffset) {
            ++count;
        }
    }
    return count;
}

inline const char* validateDynamicOffsetArgumentsDiagnostic(const DescriptorLayoutDesc& layout,
    const std::uint32_t* dynamicOffsets,
    std::uint32_t dynamicOffsetCount) noexcept
{
    const auto expectedCount = dynamicOffsetBindingCount(layout);
    if (dynamicOffsetCount != expectedCount
        || (dynamicOffsetCount > 0u && dynamicOffsets == nullptr)) {
        return "descriptor dynamic offset count does not match the layout";
    }
    return nullptr;
}

inline ValidationResult validateDynamicOffsetArguments(const DescriptorLayoutDesc& layout,
    const std::uint32_t* dynamicOffsets,
    std::uint32_t dynamicOffsetCount)
{
    const auto* diagnostic =
        validateDynamicOffsetArgumentsDiagnostic(layout, dynamicOffsets, dynamicOffsetCount);
    return {diagnostic == nullptr, diagnostic ? diagnostic : ""};
}

struct DescriptorSetSlotDesc {
    std::uint32_t set = 0;
    std::pmr::string layoutName;
};

struct DescriptorTableSlotDesc {
    std::uint32_t set = 0;
    std::pmr::string tableName;
    DescriptorType type = DescriptorType::SampledTexture;
    ShaderStage stages = ShaderStage::None;
    std::uint32_t capacity = 0;
    bool textureViewDimensionConstrained = false;
    TextureViewDimension textureViewDimension = TextureViewDimension::Texture2D;
    bool depthTexture = false;
};

struct PushConstantRange {
    ShaderStage stages = ShaderStage::None;
    std::uint32_t offsetBytes = 0;
    std::uint32_t sizeBytes = 0;
};

struct PipelineLayoutDesc {
    std::pmr::vector<DescriptorLayoutDesc> descriptorLayouts;
    std::pmr::vector<DescriptorSetSlotDesc> descriptorSets;
    std::pmr::vector<DescriptorTableSlotDesc> descriptorTables;
    std::pmr::vector<PushConstantRange> pushConstantRanges;
};

struct DescriptorArenaDesc {
    std::pmr::string label;
    std::uint32_t maxSets = 0;
    std::uint32_t maxDescriptors = 0;
};

struct DescriptorSetAllocation {
    bool success = false;
    std::pmr::string diagnostics;
    std::uint32_t setIndex = 0;
    std::uint32_t descriptorCapacity = 0;
};

struct DescriptorSetDesc {
    std::pmr::string label;
    DescriptorLayoutDesc layout;
};

struct DescriptorTableDesc {
    std::pmr::string label;
    DescriptorType type = DescriptorType::SampledTexture;
    ShaderStage stages = ShaderStage::None;
    std::uint32_t capacity = 0;
    /// DescriptorTableTextureShape/1, same requirements as set elements.
    bool textureViewDimensionConstrained = false;
    TextureViewDimension textureViewDimension = TextureViewDimension::Texture2D;
    bool depthTexture = false;
};

enum class DescriptorResourceKind { Buffer, TextureView, Sampler };

struct DescriptorBindingWrite {
    std::uint32_t binding = 0;
    DescriptorType type = DescriptorType::SampledTexture;
    GpuResourceId resource;
    DescriptorResourceKind resourceKind = DescriptorResourceKind::TextureView;
    // The image layout required while this descriptor is consumed. Sampled
    // reads normally use ShaderRead, but a texture can legally be sampled
    // from GENERAL while another mip is bound for storage in the same pass.
    // Keeping this explicit prevents backend-specific layout guessing.
    ResourceState resourceState = ResourceState::ShaderRead;

    /// The logical descriptor type remains `type`. When enabled, the RHI
    /// backend must validate and consume `physicalType` for the resource
    /// payload. This is the only code-level escape from logical type equality
    /// and is reserved for an explicit lowering context.
    struct PhysicalLowering {
        bool enabled = false;
        DescriptorType physicalType = DescriptorType::SampledTexture;
        std::uint32_t logicalAttachmentIndex = UINT32_MAX;
        /// Stable identity of the validated shader package that authorized
        /// this physical lowering. Zero is invalid for an enabled ticket.
        std::uint64_t shaderPackageHash = 0;
    } physicalLowering;

    /// Element selected by this write for an array binding. Scalar bindings
    /// must use element zero. One write per element keeps resource identity
    /// explicit and lets backends lower arrays without inferring a physical
    /// slot from the logical binding number.
    std::uint32_t arrayElement = 0;
    DescriptorBufferBindingRange bufferBindingRange;
};

/// A descriptor write plus its logical descriptor-set identity. FrameGraph
/// uses this value to route a lowered input attachment to the correct set
/// without asking a backend to infer a set from a resource or name.
struct SubpassInputDescriptorWrite {
    std::uint32_t set = 0;
    DescriptorBindingWrite write;
};

inline DescriptorType physicalDescriptorType(const DescriptorBindingWrite& write) noexcept
{
    return write.physicalLowering.enabled ? write.physicalLowering.physicalType : write.type;
}

inline ValidationResult validateDescriptorWriteLowering(const DescriptorBindingWrite& write)
{
    if (!write.physicalLowering.enabled) {
        return {true, {}};
    }
    if (write.type != DescriptorType::InputAttachment) {
        return {false, "descriptor physical lowering is only valid for InputAttachment"};
    }
    if (write.physicalLowering.physicalType != DescriptorType::SampledTexture) {
        return {false, "InputAttachment lowering must use SampledTexture physical type"};
    }
    if (write.physicalLowering.logicalAttachmentIndex == UINT32_MAX) {
        return {false, "InputAttachment lowering is missing logical attachment identity"};
    }
    if (write.physicalLowering.shaderPackageHash == 0) {
        return {false, "InputAttachment lowering is missing shader-package identity"};
    }
    if (write.resourceKind != DescriptorResourceKind::TextureView) {
        return {false, "InputAttachment lowering requires a texture-view resource"};
    }
    return {true, {}};
}

struct DescriptorSetStats {
    std::uint32_t bindingCount = 0;
    std::uint32_t writtenBindings = 0;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;
};

struct DescriptorTableAllocation {
    bool success = false;
    std::pmr::string diagnostics;
    std::uint32_t tableIndex = 0;
    std::uint32_t descriptorCapacity = 0;
};

struct DescriptorTableWrite {
    std::uint32_t index = 0;
    DescriptorType type = DescriptorType::SampledTexture;
    GpuResourceId resource;
    DescriptorResourceKind resourceKind = DescriptorResourceKind::TextureView;
};

struct DescriptorTableStats {
    std::uint32_t capacity = 0;
    std::uint32_t writtenDescriptors = 0;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;
};

struct DescriptorArenaStats {
    std::uint32_t maxSets = 0;
    std::uint32_t maxDescriptors = 0;
    std::uint32_t allocatedSets = 0;
    std::uint32_t allocatedTables = 0;
    std::uint32_t allocatedDescriptors = 0;
};

class DescriptorSet;
class DescriptorTable;
class DescriptorArena;
class GpuDevice;
class ComputePipeline;
class RenderPipeline;
struct OriginalFiniteDescriptorWriteRequest;
struct OriginalFiniteTargetMemoryWriteRequest;
struct OriginalFiniteDescriptorBufferWrite;
struct OriginalWholeDescriptorWriteRequest;
struct OriginalWholeDescriptorBufferWrite;
struct OriginalGeneralDescriptorWriteRequest;
struct OriginalCommandDescriptorSetElement;
struct OriginalCommandDescriptorSetFrozenElement;
class OriginalCommandResourceSources;
struct OriginalDepthTablePublicationRequest;
struct OriginalDepthTablePublicationWrite;

struct OriginalGeneralDescriptorSourceDiagnosticRequest {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint64_t expectedContentRevision = 0u, sourceCpuBytes = 0u;

    constexpr const char* diagnostic() const noexcept
    {
        if (contractVersion != 1u)
            return "RHI-ORIGINAL-GENERAL-SOURCE-DIAGNOSTIC-VERSION";
        if (!prototypeOptIn)
            return "RHI-ORIGINAL-GENERAL-SOURCE-DIAGNOSTIC-OPT-IN";
        if (!expectedContentRevision)
            return "RHI-ORIGINAL-GENERAL-SOURCE-REVISION";
        return sourceCpuBytes ? nullptr : "RHI-ORIGINAL-GENERAL-SOURCE-CPU-BUDGET";
    }
};

struct OriginalFiniteDescriptorSourceDiagnosticRequest {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint64_t expectedContentRevision = 0u, sourceCpuBytes = 0u;

    constexpr const char* diagnostic() const noexcept
    {
        if (contractVersion != 1u)
            return "RHI-ORIGINAL-FINITE-SOURCE-DIAGNOSTIC-VERSION";
        if (!prototypeOptIn)
            return "RHI-ORIGINAL-FINITE-SOURCE-DIAGNOSTIC-OPT-IN";
        if (!expectedContentRevision)
            return "RHI-ORIGINAL-FINITE-SOURCE-REVISION";
        return sourceCpuBytes ? nullptr : "RHI-ORIGINAL-FINITE-SOURCE-CPU-BUDGET";
    }
};

struct OriginalFiniteTargetMemorySourceDiagnosticRequest {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint64_t expectedContentRevision = 0u, sourceCpuBytes = 0u;

    constexpr const char* diagnostic() const noexcept
    {
        if (contractVersion != 1u)
            return "RHI-ORIGINAL-TARGET-FINITE-SOURCE-DIAGNOSTIC-VERSION";
        if (!prototypeOptIn)
            return "RHI-ORIGINAL-TARGET-FINITE-SOURCE-DIAGNOSTIC-OPT-IN";
        if (!expectedContentRevision)
            return "RHI-ORIGINAL-TARGET-FINITE-SOURCE-REVISION";
        return sourceCpuBytes ? nullptr : "RHI-ORIGINAL-TARGET-FINITE-SOURCE-CPU-BUDGET";
    }
};

// Fixed-size successful content receipt, not original factory/native authority.
// Only the final L2 read boundary can issue it from complete backend storage.
template <class Descriptor, class Write> class DescriptorContentObservation final {
    friend class DescriptorSet;
    friend class DescriptorTable;

public:
    const Write& write() const noexcept { return write_; }

    const char* diagnostic() const noexcept
    {
        const auto* object = object_.get();
        if (!object)
            return "RHI-DESCRIPTOR-CONTENT-EXPIRED";
        if (!object->isCurrentAllocation())
            return "RHI-DESCRIPTOR-CONTENT-ALLOCATION-STALE";
        if constexpr (std::is_same_v<Descriptor, DescriptorTable>)
            if (const auto* id = object->originalDepthTableContentDiagnostic())
                return id;
        return revision_ && object->contentRevision() == revision_
            ? nullptr
            : "RHI-DESCRIPTOR-CONTENT-CHANGED";
    }

    bool matchesObject(const Descriptor& object) const noexcept
    {
        return object_.get() == &object && !diagnostic();
    }

    std::uint64_t revision() const noexcept { return revision_; }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    DescriptorContentObservation(const Descriptor& object, const Write& write) noexcept
        : object_(&object, object.lifetimeTicket()), write_(write),
          revision_(object.contentRevision())
    {}

    core::LifetimeObserver<const Descriptor> object_;
    const Write write_;
    const std::uint64_t revision_;
};

using DescriptorSetWriteObservation =
    DescriptorContentObservation<DescriptorSet, DescriptorBindingWrite>;
using DescriptorTableWriteObservation =
    DescriptorContentObservation<DescriptorTable, DescriptorTableWrite>;

/// DescriptorConsumptionLiveness/1: a recorder observes object lifetime without
/// owning the wrapper. Object destruction expires the ticket before a later
/// draw/dispatch can dereference it. Native recorded storage is owned separately.
class ARP_API DescriptorObject : public OriginalRegisteredObject {
    friend class GpuDevice;

public:
    core::LifetimeEpoch::Ticket lifetimeTicket() const noexcept { return lifetime_.ticket(); }

    std::uint64_t contentRevision() const noexcept { return contentRevision_; }

protected:
    explicit DescriptorObject(std::uint64_t firstRevision = 1u,
        OriginalRegisteredResourceKind kind = OriginalRegisteredResourceKind::Unsupported)
        : OriginalRegisteredObject(kind), revisions_(firstRevision),
          contentRevision_(revisions_.tryAcquire())
    {}

    ~DescriptorObject() = default;

    ValidationResult validateContentWrite() const
    {
        return revisions_.hasRemaining()
            ? ValidationResult{true, {}}
            : ValidationResult{false, "RHI-DESCRIPTOR-REVISION-EXHAUSTED"};
    }

    const char* contentWriteDiagnostic() const noexcept
    {
        return revisions_.hasRemaining() ? nullptr : "RHI-DESCRIPTOR-REVISION-EXHAUSTED";
    }

    void commitContentWrite() noexcept { contentRevision_ = revisions_.tryAcquire(); }

private:
    core::LifetimeEpoch lifetime_;
    core::MonotonicIdSequence revisions_;
    std::uint64_t contentRevision_;
    std::shared_ptr<DescriptorArena> originalArenaOwner_;
};

/// DescriptorResourceLiveness/1: validation re-resolves written IDs in the
/// arena's device registry. Destroyed wrappers and stale arena generations
/// reject new use, including identical/cached writes. Retained native storage
/// keeps already recorded work safe; it does not keep a public ID bindable.
/// Consumers validate required elements again before recording their use.
class ARP_API DescriptorSet : public DescriptorObject {
    friend class GpuDevice;

public:
    DescriptorSet() : DescriptorObject(1u, OriginalRegisteredResourceKind::Descriptor) {}

    virtual ~DescriptorSet() = default;
    virtual const DescriptorSetAllocation& allocation() const = 0;
    virtual const DescriptorLayoutDesc& layout() const = 0;

    // The non-overridable transaction gate keeps all backends on the same
    // revision contract. writeImpl must publish content only on success.
    virtual ValidationResult write(const DescriptorBindingWrite& write) final
    {
        if (!isCurrentAllocation())
            return {false,
                "RHI-DESCRIPTOR-ALLOCATION-STALE: descriptor set write rejected after arena reset "
                "or destruction"};
        auto result = validateContentWrite();
        if (!result.success)
            return result;
        if (const auto* id = ordinaryWriteAdmissionDiagnostic())
            return {false, id};
        const bool bufferDomain = write.resourceKind == DescriptorResourceKind::Buffer
            && (write.type == DescriptorType::UniformBuffer
                || write.type == DescriptorType::StorageBuffer
                || write.type == DescriptorType::ReadOnlyStorageBuffer);
        if (const auto* diagnostic = DescriptorBufferBindingRangeContract::writeDiagnostic(
                write.bufferBindingRange, bufferDomain))
            return {false, core::pmr::String(diagnostic, &core::defaultCpuMemoryResource())};
        result = writeImpl(write);
        if (result.success)
            commitContentWrite();
        return result;
    }

    ValidationResult writeOriginalCompletedTexture(const GpuDevice&,
        const DescriptorBindingWrite&,
        const std::shared_ptr<const OriginalCommandTextureViewSource>&,
        const GpuTexture::ContentExpectation&,
        const OriginalCompletedTextureDescriptorRequest&);
    ValidationResult writeOriginalCompletedTexture(const GpuDevice&,
        const DescriptorBindingWrite&,
        const std::shared_ptr<const OriginalCompletedDepthTextureSource>&,
        const OriginalCompletedTextureDescriptorRequest&);

    ValidationResult writeOriginalTextureCell(const GpuDevice&,
        const DescriptorBindingWrite&,
        const std::shared_ptr<const OriginalCommandTextureViewSource>&,
        const GpuTexture::ContentRangeExpectation&,
        const OriginalTextureCellDescriptorRequest&);

    // Source6 publication identity only. It cannot issue content or execution.
    const char* diagnoseOriginalCompletedDepthPublication(const DescriptorSetWriteObservation&,
        const std::shared_ptr<const OriginalCompletedDepthTextureSource>&) const noexcept;

    ValidationResult writeOriginalBufferRange(GpuDevice&,
        const DescriptorBindingWrite&,
        const std::shared_ptr<const OriginalCommandBufferLease>&,
        std::uint64_t minimumBytes,
        const OriginalBufferRangeDescriptorRequest&);

    // Explicit final commit boundary: callbacks can only prepare evidence.
    // No backend can advance the revision before the complete candidate is ready.
    const char* finishOriginalFiniteWrite() noexcept
    {
        if (!isCurrentAllocation())
            return "RHI-DESCRIPTOR-ALLOCATION-STALE";
        if (const auto* id = contentWriteDiagnostic())
            return id;
        if (const auto* id = finishOriginalFiniteWriteImpl())
            return id;
        commitContentWrite();
        return nullptr;
    }

    const char* cancelOriginalFiniteWrite() noexcept { return cancelOriginalFiniteWriteImpl(); }

    const char* finishOriginalWholeWrite() noexcept
    {
        if (!isCurrentAllocation())
            return "RHI-DESCRIPTOR-ALLOCATION-STALE";
        if (const auto* id = contentWriteDiagnostic())
            return id;
        if (const auto* id = finishOriginalWholeWriteImpl())
            return id;
        commitContentWrite();
        return nullptr;
    }

    const char* cancelOriginalWholeWrite() noexcept { return cancelOriginalWholeWriteImpl(); }

    virtual bool hasOriginalFiniteContent() const noexcept { return false; }

    virtual bool hasOriginalWholeContent() const noexcept { return false; }

    const char* finishOriginalGeneralWrite() noexcept
    {
        if (!isCurrentAllocation())
            return "RHI-DESCRIPTOR-ALLOCATION-STALE";
        if (const auto* id = contentWriteDiagnostic())
            return id;
        if (const auto* id = finishOriginalGeneralWriteImpl())
            return id;
        commitContentWrite();
        return nullptr;
    }

    const char* cancelOriginalGeneralWrite() noexcept { return cancelOriginalGeneralWriteImpl(); }

    virtual bool hasOriginalGeneralContent() const noexcept { return false; }

    virtual std::uint32_t originalGeneralContentContractVersion() const noexcept { return 0u; }

    virtual bool originalGeneralWriteScopeObserved(std::uint32_t) const noexcept { return false; }

    virtual bool originalGeneralWriteScopesDrained() const noexcept { return false; }

    // Existing Source observations must reject an uncommitted replacement.
    // This is content validation only, never native execution authority.
    virtual const char* originalScopedContentDiagnostic() const noexcept { return nullptr; }

    virtual bool originalFiniteWriteScopeObserved(std::uint32_t) const noexcept { return false; }

    virtual bool originalFiniteWriteScopesDrained() const noexcept { return false; }

    virtual bool originalWholeWriteScopeObserved(std::uint32_t) const noexcept { return false; }

    virtual bool originalWholeWriteScopesDrained() const noexcept { return false; }

    virtual bool isCurrentAllocation() const = 0;
    virtual bool hasWrittenBinding(std::uint32_t binding) const = 0;
    virtual ValidationResult validateBinding(std::uint32_t binding) const = 0;

    /// Element-granular array contract. Backends that only implement scalar
    /// descriptors retain the legacy behavior for element zero and reject
    /// other elements during write validation.
    virtual bool hasWrittenBindingElement(std::uint32_t binding, std::uint32_t arrayElement) const
    {
        return arrayElement == 0u && hasWrittenBinding(binding);
    }

    virtual ValidationResult validateBindingElement(
        std::uint32_t binding, std::uint32_t arrayElement) const
    {
        return arrayElement == 0u
            ? validateBinding(binding)
            : ValidationResult{false, "descriptor array element is not written"};
    }

    virtual DescriptorSetStats stats() const = 0;

    virtual core::LifetimeEpoch::Ticket allocationEpochTicket() const noexcept { return {}; }

    std::optional<DescriptorSetWriteObservation> observeSuccessfulBindingWrite(
        std::uint32_t binding, std::uint32_t element, const char*& diagnostic) const noexcept
    {
        diagnostic = nullptr;
        if (!isCurrentAllocation()) {
            diagnostic = "RHI-DESCRIPTOR-CONTENT-ALLOCATION-STALE";
            return {};
        }
        const auto written = successfulBindingWrite(binding, element);
        if (!written) {
            diagnostic = "RHI-DESCRIPTOR-CONTENT-NOT-WRITTEN";
            return {};
        }
        if (written->binding != binding || written->arrayElement != element || !contentRevision()) {
            diagnostic = "RHI-DESCRIPTOR-CONTENT-OBSERVATION-CONTRACT";
            return {};
        }
        return DescriptorSetWriteObservation(*this, *written);
    }

protected:
    virtual std::shared_ptr<const OriginalCompletedDepthTextureSource>
    originalCompletedDepthPublication(std::uint32_t, std::uint32_t) const noexcept
    {
        return {};
    }

    virtual ValidationResult writeOriginalCompletedTextureImpl(const DescriptorBindingWrite& write,
        const std::shared_ptr<const OriginalCommandTextureViewSource>& source,
        const GpuTexture::ContentExpectation& content,
        const OriginalCompletedTextureDescriptorRequest& request)
    {
        if (request.contractVersion == 5u)
            return {false, "RHI-COMPLETED-TEXTURE-DESCRIPTOR-HDR-BACKEND-MISSING"};
        return writeOriginalCompletedTextureImpl(write, source, content);
    }

    virtual ValidationResult writeOriginalTextureCellImpl(const DescriptorBindingWrite&,
        const std::shared_ptr<const OriginalCommandTextureViewSource>&,
        const GpuTexture::ContentRangeExpectation&,
        const OriginalTextureCellDescriptorRequest&)
    {
        return {false, "RHI-TEXTURE-CELL-DESCRIPTOR-BACKEND-MISSING"};
    }

    virtual ValidationResult writeOriginalCompletedTextureImpl(const DescriptorBindingWrite&,
        const std::shared_ptr<const OriginalCommandTextureViewSource>&,
        const GpuTexture::ContentExpectation&)
    {
        return {false, "RHI-COMPLETED-TEXTURE-DESCRIPTOR-BACKEND-MISSING"};
    }

    virtual ValidationResult writeOriginalCompletedTextureImpl(const DescriptorBindingWrite&,
        const std::shared_ptr<const OriginalCompletedDepthTextureSource>&)
    {
        return {false, "RHI-COMPLETED-TEXTURE-DESCRIPTOR-BACKEND-MISSING"};
    }

    virtual const char* ordinaryWriteAdmissionDiagnostic() const noexcept { return nullptr; }

    virtual const char* finishOriginalFiniteWriteImpl() noexcept
    {
        return "RHI-ORIGINAL-FINITE-WRITE-UNPREPARED";
    }

    virtual const char* cancelOriginalFiniteWriteImpl() noexcept
    {
        return "RHI-ORIGINAL-FINITE-WRITE-UNPREPARED";
    }

    virtual const char* finishOriginalWholeWriteImpl() noexcept
    {
        return "RHI-ORIGINAL-WHOLE-WRITE-UNPREPARED";
    }

    virtual const char* cancelOriginalWholeWriteImpl() noexcept
    {
        return "RHI-ORIGINAL-WHOLE-WRITE-UNPREPARED";
    }

    virtual ValidationResult writeOriginalBufferRangeImpl(
        const DescriptorBindingWrite&, const std::shared_ptr<const OriginalCommandBufferLease>&)
    {
        return {false, "RHI-BUFFER-RANGE-DESCRIPTOR-NATIVE-PAYLOAD-MISSING"};
    }

    virtual ValidationResult writeImpl(const DescriptorBindingWrite& write) = 0;

    virtual std::optional<DescriptorBindingWrite> successfulBindingWrite(
        std::uint32_t, std::uint32_t) const noexcept
    {
        return {};
    }

public:
    template <class Pipeline>
    const char* diagnoseOriginalGeneralSource(
        const OriginalGeneralDescriptorSourceDiagnosticRequest& request,
        const std::shared_ptr<const Pipeline>& pipeline,
        const std::shared_ptr<const OriginalCommandResourceSources>& resources,
        std::uint32_t set,
        const OriginalCommandDescriptorSetFrozenElement* elements,
        std::size_t count,
        const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
    {
        if (const auto* id = request.diagnostic())
            return id;
        if (!quota || quota->limitBytes() != request.sourceCpuBytes)
            return "RHI-ORIGINAL-GENERAL-SOURCE-QUOTA";
        if (!isCurrentAllocation())
            return "RHI-DESCRIPTOR-ALLOCATION-STALE";
        if (contentRevision() != request.expectedContentRevision)
            return "RHI-ORIGINAL-GENERAL-SOURCE-REVISION";
        if (const auto* id = originalScopedContentDiagnostic())
            return id;
        return diagnoseOriginalGeneralSourceImpl(pipeline, resources, set, elements, count, quota);
    }

    template <class Pipeline>
    const char* diagnoseOriginalFiniteSource(
        const OriginalFiniteDescriptorSourceDiagnosticRequest& request,
        const std::shared_ptr<const Pipeline>& pipeline,
        std::uint32_t set,
        const OriginalFiniteDescriptorBufferWrite* writes,
        std::size_t count,
        const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
    {
        if (const auto* id = request.diagnostic())
            return id;
        if (!quota || quota->limitBytes() != request.sourceCpuBytes)
            return "RHI-ORIGINAL-FINITE-SOURCE-QUOTA";
        if (!isCurrentAllocation())
            return "RHI-DESCRIPTOR-ALLOCATION-STALE";
        if (contentRevision() != request.expectedContentRevision)
            return "RHI-ORIGINAL-FINITE-SOURCE-REVISION";
        return diagnoseOriginalFiniteSourceImpl(pipeline, set, writes, count, quota);
    }

    template <class Pipeline>
    const char* diagnoseOriginalFiniteTargetMemorySource(
        const OriginalFiniteTargetMemorySourceDiagnosticRequest& request,
        const std::shared_ptr<const Pipeline>& pipeline,
        std::uint32_t set,
        const OriginalFiniteDescriptorBufferWrite* writes,
        std::size_t count,
        const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
    {
        if (const auto* id = request.diagnostic())
            return id;
        if (!quota || quota->limitBytes() != request.sourceCpuBytes)
            return "RHI-ORIGINAL-FINITE-SOURCE-QUOTA";
        if (!isCurrentAllocation())
            return "RHI-DESCRIPTOR-ALLOCATION-STALE";
        if (contentRevision() != request.expectedContentRevision)
            return "RHI-ORIGINAL-FINITE-SOURCE-REVISION";
        return diagnoseOriginalFiniteTargetMemorySourceImpl(pipeline, set, writes, count, quota);
    }

private:
    template <class Pipeline>
    const char* beginOriginalWholeWrite(const GpuDevice& device,
        const std::shared_ptr<const DescriptorArena>& arena,
        const std::shared_ptr<Pipeline>& pipeline,
        std::uint32_t set,
        const OriginalWholeDescriptorBufferWrite* writes,
        std::size_t count,
        const OriginalWholeDescriptorWriteRequest& request,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas)
    {
        if (!isCurrentAllocation())
            return "RHI-DESCRIPTOR-ALLOCATION-STALE";
        if (const auto* id = contentWriteDiagnostic())
            return id;
        return beginOriginalWholeWriteImpl(
            device, arena, pipeline, set, writes, count, request, quotas);
    }

    template <class Pipeline>
    const char* beginOriginalGeneralWrite(const GpuDevice& device,
        const std::shared_ptr<const DescriptorArena>& arena,
        const std::shared_ptr<Pipeline>& pipeline,
        const std::shared_ptr<const OriginalCommandResourceSources>& sources,
        std::uint32_t set,
        const OriginalCommandDescriptorSetElement* elements,
        std::size_t count,
        const OriginalGeneralDescriptorWriteRequest& request,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas)
    {
        if (!isCurrentAllocation())
            return "RHI-DESCRIPTOR-ALLOCATION-STALE";
        if (const auto* id = contentWriteDiagnostic())
            return id;
        return beginOriginalGeneralWriteImpl(
            device, arena, pipeline, sources, set, elements, count, request, quotas);
    }

    virtual const char* finishOriginalGeneralWriteImpl() noexcept
    {
        return "RHI-ORIGINAL-GENERAL-WRITE-BACKEND";
    }

    virtual const char* cancelOriginalGeneralWriteImpl() noexcept
    {
        return "RHI-ORIGINAL-GENERAL-WRITE-BACKEND";
    }

    virtual const char* beginOriginalGeneralWriteImpl(const GpuDevice&,
        const std::shared_ptr<const DescriptorArena>&,
        const std::shared_ptr<ComputePipeline>&,
        const std::shared_ptr<const OriginalCommandResourceSources>&,
        std::uint32_t,
        const OriginalCommandDescriptorSetElement*,
        std::size_t,
        const OriginalGeneralDescriptorWriteRequest&,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>&)
    {
        return "RHI-ORIGINAL-GENERAL-WRITE-BACKEND";
    }

    virtual const char* beginOriginalGeneralWriteImpl(const GpuDevice&,
        const std::shared_ptr<const DescriptorArena>&,
        const std::shared_ptr<RenderPipeline>&,
        const std::shared_ptr<const OriginalCommandResourceSources>&,
        std::uint32_t,
        const OriginalCommandDescriptorSetElement*,
        std::size_t,
        const OriginalGeneralDescriptorWriteRequest&,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>&)
    {
        return "RHI-ORIGINAL-GENERAL-WRITE-BACKEND";
    }

    virtual const char* beginOriginalWholeWriteImpl(const GpuDevice&,
        const std::shared_ptr<const DescriptorArena>&,
        const std::shared_ptr<ComputePipeline>&,
        std::uint32_t,
        const OriginalWholeDescriptorBufferWrite*,
        std::size_t,
        const OriginalWholeDescriptorWriteRequest&,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>&)
    {
        return "RHI-ORIGINAL-WHOLE-WRITE-BACKEND";
    }

    virtual const char* beginOriginalWholeWriteImpl(const GpuDevice&,
        const std::shared_ptr<const DescriptorArena>&,
        const std::shared_ptr<RenderPipeline>&,
        std::uint32_t,
        const OriginalWholeDescriptorBufferWrite*,
        std::size_t,
        const OriginalWholeDescriptorWriteRequest&,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>&)
    {
        return "RHI-ORIGINAL-WHOLE-WRITE-BACKEND";
    }

    virtual const char* diagnoseOriginalGeneralSourceImpl(
        const std::shared_ptr<const ComputePipeline>&,
        const std::shared_ptr<const OriginalCommandResourceSources>&,
        std::uint32_t,
        const OriginalCommandDescriptorSetFrozenElement*,
        std::size_t,
        const std::shared_ptr<core::CpuMemoryQuota>&) const noexcept
    {
        return "RHI-ORIGINAL-GENERAL-SOURCE-PAYLOAD-MISSING";
    }

    virtual const char* diagnoseOriginalGeneralSourceImpl(
        const std::shared_ptr<const RenderPipeline>&,
        const std::shared_ptr<const OriginalCommandResourceSources>&,
        std::uint32_t,
        const OriginalCommandDescriptorSetFrozenElement*,
        std::size_t,
        const std::shared_ptr<core::CpuMemoryQuota>&) const noexcept
    {
        return "RHI-ORIGINAL-GENERAL-SOURCE-PAYLOAD-MISSING";
    }

    virtual const char* diagnoseOriginalFiniteSourceImpl(
        const std::shared_ptr<const ComputePipeline>&,
        std::uint32_t,
        const OriginalFiniteDescriptorBufferWrite*,
        std::size_t,
        const std::shared_ptr<core::CpuMemoryQuota>&) const noexcept
    {
        return "RHI-ORIGINAL-FINITE-SOURCE-PAYLOAD-MISSING";
    }

    virtual const char* diagnoseOriginalFiniteSourceImpl(
        const std::shared_ptr<const RenderPipeline>&,
        std::uint32_t,
        const OriginalFiniteDescriptorBufferWrite*,
        std::size_t,
        const std::shared_ptr<core::CpuMemoryQuota>&) const noexcept
    {
        return "RHI-ORIGINAL-FINITE-SOURCE-PAYLOAD-MISSING";
    }

    virtual const char* diagnoseOriginalFiniteTargetMemorySourceImpl(
        const std::shared_ptr<const ComputePipeline>&,
        std::uint32_t,
        const OriginalFiniteDescriptorBufferWrite*,
        std::size_t,
        const std::shared_ptr<core::CpuMemoryQuota>&) const noexcept
    {
        return "RHI-ORIGINAL-TARGET-FINITE-WRITE-BACKEND";
    }

    virtual const char* diagnoseOriginalFiniteTargetMemorySourceImpl(
        const std::shared_ptr<const RenderPipeline>&,
        std::uint32_t,
        const OriginalFiniteDescriptorBufferWrite*,
        std::size_t,
        const std::shared_ptr<core::CpuMemoryQuota>&) const noexcept
    {
        return "RHI-ORIGINAL-TARGET-FINITE-WRITE-BACKEND";
    }

    template <class Pipeline>
    const char* beginOriginalFiniteWrite(const GpuDevice& device,
        const std::shared_ptr<const DescriptorArena>& arena,
        const std::shared_ptr<Pipeline>& pipeline,
        std::uint32_t set,
        const OriginalFiniteDescriptorBufferWrite* writes,
        std::size_t count,
        const OriginalFiniteDescriptorWriteRequest& request,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas)
    {
        if (!isCurrentAllocation())
            return "RHI-DESCRIPTOR-ALLOCATION-STALE";
        if (const auto* id = contentWriteDiagnostic())
            return id;
        return beginOriginalFiniteWriteImpl(
            device, arena, pipeline, set, writes, count, request, quotas);
    }

    virtual const char* beginOriginalFiniteWriteImpl(const GpuDevice&,
        const std::shared_ptr<const DescriptorArena>&,
        const std::shared_ptr<ComputePipeline>&,
        std::uint32_t,
        const OriginalFiniteDescriptorBufferWrite*,
        std::size_t,
        const OriginalFiniteDescriptorWriteRequest&,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>&)
    {
        return "RHI-ORIGINAL-FINITE-WRITE-BACKEND";
    }

    virtual const char* beginOriginalFiniteWriteImpl(const GpuDevice&,
        const std::shared_ptr<const DescriptorArena>&,
        const std::shared_ptr<RenderPipeline>&,
        std::uint32_t,
        const OriginalFiniteDescriptorBufferWrite*,
        std::size_t,
        const OriginalFiniteDescriptorWriteRequest&,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>&)
    {
        return "RHI-ORIGINAL-FINITE-WRITE-BACKEND";
    }

    template <class Pipeline>
    const char* beginOriginalFiniteTargetMemoryWrite(const GpuDevice& device,
        const std::shared_ptr<const DescriptorArena>& arena,
        const std::shared_ptr<Pipeline>& pipeline,
        std::uint32_t set,
        const OriginalFiniteDescriptorBufferWrite* writes,
        std::size_t count,
        const OriginalFiniteTargetMemoryWriteRequest& request,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas)
    {
        if (!isCurrentAllocation())
            return "RHI-DESCRIPTOR-ALLOCATION-STALE";
        if (const auto* id = contentWriteDiagnostic())
            return id;
        return beginOriginalFiniteTargetMemoryWriteImpl(
            device, arena, pipeline, set, writes, count, request, quotas);
    }

    virtual const char* beginOriginalFiniteTargetMemoryWriteImpl(const GpuDevice&,
        const std::shared_ptr<const DescriptorArena>&,
        const std::shared_ptr<ComputePipeline>&,
        std::uint32_t,
        const OriginalFiniteDescriptorBufferWrite*,
        std::size_t,
        const OriginalFiniteTargetMemoryWriteRequest&,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>&)
    {
        return "RHI-ORIGINAL-TARGET-FINITE-WRITE-BACKEND";
    }

    virtual const char* beginOriginalFiniteTargetMemoryWriteImpl(const GpuDevice&,
        const std::shared_ptr<const DescriptorArena>&,
        const std::shared_ptr<RenderPipeline>&,
        std::uint32_t,
        const OriginalFiniteDescriptorBufferWrite*,
        std::size_t,
        const OriginalFiniteTargetMemoryWriteRequest&,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>&)
    {
        return "RHI-ORIGINAL-TARGET-FINITE-WRITE-BACKEND";
    }
};

/// Shares DescriptorResourceLiveness/1 with DescriptorSet. A recorded index
/// is not proof that its resource is still alive in the owning device.
class ARP_API DescriptorTable : public DescriptorObject {
public:
    DescriptorTable() : DescriptorObject(1u, OriginalRegisteredResourceKind::DescriptorTable) {}

    virtual ~DescriptorTable() = default;
    virtual const DescriptorTableAllocation& allocation() const = 0;
    virtual const DescriptorTableDesc& desc() const = 0;

    virtual ValidationResult write(const DescriptorTableWrite& write) final
    {
        if (!isCurrentAllocation())
            return {false,
                "RHI-DESCRIPTOR-ALLOCATION-STALE: descriptor table write rejected after arena "
                "reset or destruction"};
        auto result = validateContentWrite();
        if (!result.success)
            return result;
        if (const auto* id = ordinaryWriteAdmissionDiagnostic())
            return {false, id};
        result = writeImpl(write);
        if (result.success)
            commitContentWrite();
        return result;
    }

    // Cold candidate preparation never publishes content. Only this final
    // boundary advances the revision once after the whole native candidate passes.
    template <class Pipeline>
    const char* beginOriginalDepthTablePublication(const GpuDevice& device,
        const std::shared_ptr<const DescriptorArena>& arena,
        const std::shared_ptr<Pipeline>& pipeline,
        const OriginalDepthTablePublicationWrite* writes,
        std::size_t count,
        const OriginalDepthTablePublicationRequest& request,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas)
    {
        if (!isCurrentAllocation())
            return "RHI-DESCRIPTOR-ALLOCATION-STALE";
        if (const auto* id = contentWriteDiagnostic())
            return id;
        return beginOriginalDepthTablePublicationImpl(
            device, arena, pipeline, writes, count, request, quotas);
    }

    const char* finishOriginalDepthTablePublication() noexcept
    {
        if (!isCurrentAllocation())
            return "RHI-DESCRIPTOR-ALLOCATION-STALE";
        if (const auto* id = contentWriteDiagnostic())
            return id;
        if (const auto* id = finishOriginalDepthTablePublicationImpl())
            return id;
        commitContentWrite();
        return nullptr;
    }

    const char* cancelOriginalDepthTablePublication() noexcept
    {
        return cancelOriginalDepthTablePublicationImpl();
    }

    virtual bool originalDepthTablePublicationScopesDrained() const noexcept { return false; }

    virtual bool hasOriginalDepthTableContent() const noexcept { return false; }

    virtual const char* originalDepthTableContentDiagnostic() const noexcept { return nullptr; }

    // OriginalDepthTableBinding/1: exact committed publication custody only.
    // Called by the selected Inputs4 validator; no native handles or command
    // execution authority are exposed by this relation.
    virtual const char* originalDepthTablePipelineDiagnostic(const ComputePipeline&) const noexcept
    {
        return "RHI-ORIGINAL-TABLE-BINDING-PAYLOAD-UNAVAILABLE";
    }

    virtual const char* originalDepthTablePipelineDiagnostic(const RenderPipeline&) const noexcept
    {
        return "RHI-ORIGINAL-TABLE-BINDING-PAYLOAD-UNAVAILABLE";
    }

    virtual bool isCurrentAllocation() const = 0;
    virtual bool hasWrittenDescriptor(std::uint32_t index) const = 0;
    virtual ValidationResult validateDescriptor(std::uint32_t index) const = 0;
    virtual DescriptorTableStats stats() const = 0;

    virtual core::LifetimeEpoch::Ticket allocationEpochTicket() const noexcept { return {}; }

    std::optional<DescriptorTableWriteObservation> observeSuccessfulDescriptorWrite(
        std::uint32_t index, const char*& diagnostic) const noexcept
    {
        diagnostic = nullptr;
        if (!isCurrentAllocation()) {
            diagnostic = "RHI-DESCRIPTOR-CONTENT-ALLOCATION-STALE";
            return {};
        }
        if ((diagnostic = originalDepthTableContentDiagnostic()))
            return {};
        const auto written = successfulDescriptorWrite(index);
        if (!written) {
            diagnostic = "RHI-DESCRIPTOR-CONTENT-NOT-WRITTEN";
            return {};
        }
        if (written->index != index || !contentRevision()) {
            diagnostic = "RHI-DESCRIPTOR-CONTENT-OBSERVATION-CONTRACT";
            return {};
        }
        return DescriptorTableWriteObservation(*this, *written);
    }

protected:
    virtual const char* ordinaryWriteAdmissionDiagnostic() const noexcept { return nullptr; }

    virtual const char* beginOriginalDepthTablePublicationImpl(const GpuDevice&,
        const std::shared_ptr<const DescriptorArena>&,
        const std::shared_ptr<ComputePipeline>&,
        const OriginalDepthTablePublicationWrite*,
        std::size_t,
        const OriginalDepthTablePublicationRequest&,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>&)
    {
        return "RHI-ORIGINAL-TABLE-PUBLICATION-UNAVAILABLE";
    }

    virtual const char* beginOriginalDepthTablePublicationImpl(const GpuDevice&,
        const std::shared_ptr<const DescriptorArena>&,
        const std::shared_ptr<RenderPipeline>&,
        const OriginalDepthTablePublicationWrite*,
        std::size_t,
        const OriginalDepthTablePublicationRequest&,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>&)
    {
        return "RHI-ORIGINAL-TABLE-PUBLICATION-UNAVAILABLE";
    }

    virtual const char* finishOriginalDepthTablePublicationImpl() noexcept
    {
        return "RHI-ORIGINAL-TABLE-PUBLICATION-UNPREPARED";
    }

    virtual const char* cancelOriginalDepthTablePublicationImpl() noexcept
    {
        return "RHI-ORIGINAL-TABLE-PUBLICATION-UNPREPARED";
    }

    virtual ValidationResult writeImpl(const DescriptorTableWrite& write) = 0;

    virtual std::optional<DescriptorTableWrite> successfulDescriptorWrite(
        std::uint32_t) const noexcept
    {
        return {};
    }
};

/// DescriptorArenaLifetime/1: reset and destruction invalidate allocation
/// permission, not already recorded native storage ownership. Surviving sets
/// and tables remain safe to inspect; stale writes/validation reject, written
/// queries/stats are empty. Generations never recycle, including on exhaustion.
/// Arena/descriptor/recording operations require external serialization.
class ARP_API DescriptorArena : public core::SharedObjectIdentity, public OriginalRegisteredObject {
public:
    DescriptorArena() noexcept
        : OriginalRegisteredObject(OriginalRegisteredResourceKind::DescriptorArena)
    {}

    virtual ~DescriptorArena() = default;
    virtual DescriptorSetAllocation allocateSet(const DescriptorLayoutDesc& layout) = 0;
    virtual arp::core::UniquePtr<DescriptorSet> allocateDescriptorSet(
        const DescriptorSetDesc& desc) = 0;
    virtual DescriptorTableAllocation allocateTable(const DescriptorTableDesc& desc) = 0;
    virtual arp::core::UniquePtr<DescriptorTable> allocateDescriptorTable(
        const DescriptorTableDesc& desc) = 0;
    virtual DescriptorArenaStats stats() const = 0;
    virtual void reset() = 0;

    virtual core::LifetimeEpoch::Ticket allocationEpochTicket() const noexcept { return {}; }
};

template <class T> struct OriginalRegisteredDescriptorTraits {
    using Base = std::conditional_t<std::is_base_of_v<DescriptorArena, T>,
        DescriptorArena,
        std::conditional_t<std::is_base_of_v<DescriptorSet, T>, DescriptorSet, DescriptorTable>>;
    static_assert(std::is_base_of_v<Base, T>);
};

ARP_API ValidationResult validateDescriptorLayout(const DescriptorLayoutDesc& desc);
ARP_API ValidationResult validateDescriptorTextureRequirement(
    DescriptorType type, bool dimensionConstrained, bool depthTexture);
ARP_API ValidationResult validateDescriptorTableSlot(
    const PipelineLayoutDesc& layout, std::uint32_t set, const DescriptorTableDesc& table);
/// Checked logical element capacity. On failure count is zero.
ARP_API ValidationResult countDescriptorElements(
    const DescriptorLayoutDesc& desc, std::uint32_t& count);
/// Flatten a validated layout in declaration order; UINT32_MAX means invalid.
/// Logical binding numbers are not physical offsets or native binding indices.
ARP_API std::uint32_t descriptorElementIndex(
    const DescriptorLayoutDesc& desc, std::uint32_t binding, std::uint32_t arrayElement);
/// Checks resolved view metadata, after resource identity/usage validation.
/// Shared by all four backends; no native type or backend decision is exposed.
ARP_API ValidationResult validateSampledDepthView(
    bool depthRequired, TextureFormat format, TextureAspect aspect);
ARP_API ValidationResult validatePipelineLayout(const PipelineLayoutDesc& desc);

class GpuDevice;

/// Backend-owned descriptor cache. Caches DescriptorArena objects by arena
/// configuration hash across frames, avoiding per-frame creation/destruction
/// of descriptor pools. Descriptor sets are cached by layout hash within
/// their parent arena.
///
/// The cache is allocator-compliant (core::pmr containers) and does not
/// expose native handles. Lifetime is backend-owned: callers receive
/// non-owning pointers.
///
/// Fence-gated eviction: entries not hit for maxUnusedFrames frames are
/// evicted via evictStale.
class ARP_API DescriptorCache {
public:
    /// Construct with an explicit CPU memory resource. When omitted, the
    /// process-wide default resource is used. Threaded through to all pmr
    /// members so a backend can bind a per-instance intent/budget (e.g. a
    /// device-scoped arena) instead of silently falling back to the global
    /// default.
    explicit DescriptorCache(GpuDevice& device,
        std::pmr::memory_resource* memoryResource = &core::defaultCpuMemoryResource());
    ~DescriptorCache();

    DescriptorCache(const DescriptorCache&) = delete;
    DescriptorCache& operator=(const DescriptorCache&) = delete;

    /// Acquire a descriptor arena matching the given config. Returns a cached
    /// arena on hit (after calling reset() to clear allocations), or creates
    /// and caches a new one on miss. Returns nullptr if the device cannot
    /// create the arena.
    DescriptorArena* acquireDescriptorArena(const DescriptorArenaDesc& desc);

    /// Acquire a descriptor set from the given arena with the specified
    /// layout. On hit, the cached set is returned (the bindings must be
    /// rebound). On miss, a new set is allocated from the arena and cached.
    /// Returns nullptr on allocation failure.
    DescriptorSet* acquireDescriptorSet(const DescriptorSetDesc& desc, DescriptorArena* arena);

    /// Mark the current frame as complete. All entries have their
    /// lastUsedFrameSerial updated.
    void onFrameComplete();

    /// Evict entries that have not been hit for at least maxUnusedFrames
    /// frames. Called after onFrameComplete.
    void evictStale(std::uint64_t currentFrameSerial, std::uint32_t maxUnusedFrames);
    /// Release sets before arenas at an explicit teardown boundary. Reserved
    /// bucket storage remains bounded/owned; no GPU completion is inferred.
    void clearForShutdown();

    std::uint32_t arenaHitCount() const noexcept { return arenaHitCount_; }

    std::uint32_t arenaMissCount() const noexcept { return arenaMissCount_; }

    std::uint32_t setHitCount() const noexcept { return setHitCount_; }

    std::uint32_t setMissCount() const noexcept { return setMissCount_; }

    std::uint32_t entryCount() const noexcept;
    /// Descriptor capacity is charged from each cached arena's declared
    /// maxDescriptors before the backend creates that arena.
    bool configureBudget(const semantics::CacheBudget& budget) noexcept;
    semantics::CacheBudgetStats budgetStats() const noexcept;

    const char* lastBudgetDiagnostic() const noexcept { return lastBudgetDiagnostic_; }

    /// Reset all telemetry counters (for testing).
    void resetTelemetry();

private:
    /// Compute a stable hash for a DescriptorArenaDesc. The label is excluded
    /// from the hash so that arenas with different labels but identical config
    /// can be reused.
    static std::uint64_t hashArenaDesc(const DescriptorArenaDesc& desc) noexcept;

    /// Compute a stable hash for a DescriptorLayoutDesc.
    static std::uint64_t hashLayoutDesc(const DescriptorLayoutDesc& desc) noexcept;

    struct ArenaEntry {
        arp::core::UniquePtr<DescriptorArena> arena;
        std::uint64_t lastUsedFrameSerial = 0;
        std::uint64_t chargedDescriptors = 0u;
    };

    struct SetEntry {
        /// The cached set is paired with its owning arena to ensure the arena
        /// outlives the set. When the arena is evicted, all its sets are
        /// evicted too.
        DescriptorArena* owningArena = nullptr;
        arp::core::UniquePtr<DescriptorSet> set;
        std::uint64_t lastUsedFrameSerial = 0;
    };

    GpuDevice& device_;
    std::pmr::memory_resource* memoryResource_;
    core::pmr::UnorderedMap<std::uint64_t, ArenaEntry> arenaCache_;
    core::pmr::UnorderedMap<std::uint64_t, SetEntry> setCache_;
    std::uint64_t currentFrameSerial_ = 0;
    std::uint32_t arenaHitCount_ = 0;
    std::uint32_t arenaMissCount_ = 0;
    std::uint32_t setHitCount_ = 0;
    std::uint32_t setMissCount_ = 0;
    semantics::CacheBudget budget_;
    std::uint64_t chargedDescriptors_ = 0u;
    std::uint64_t peakChargedDescriptors_ = 0u;
    std::uint64_t budgetRejectionCount_ = 0u;
    const char* lastBudgetDiagnostic_ = nullptr;
};

} // namespace arp::rhi
