#pragma once

#include "core/hash/sha256.h"
#include "core/ownership/quota_bound_shared_identity.h"
#include "core/ownership/shared_owner_projection.h"
#include "core/ownership/versioned_shared_owner.h"
#include "rhi/backends/webgpu/webgpu_native_runtime.h"
#include "rhi/backends/webgpu/webgpu_original_depth_table_expansion_pipeline_definition.h"
#include "rhi/frontend/descriptor_set_validation.h"
#include "rhi/semantics/viewport_scissor_validation.h"

namespace arp::rhi_webgpu {

class WebGpuOriginalArtifactComputeFactory;
class WebGpuOriginalArtifactGraphicsFactory;

struct WebGpuNativeDescriptorSetGroupLowering {
    std::uint32_t logicalSet = 0;
    std::uint32_t webGpuGroup = 0;

    bool operator==(const WebGpuNativeDescriptorSetGroupLowering& other) const noexcept
    {
        return logicalSet == other.logicalSet && webGpuGroup == other.webGpuGroup;
    }
};

struct WebGpuNativeDescriptorBindingLowering {
    std::uint32_t logicalSet = 0;
    std::uint32_t logicalBinding = 0;
    std::uint32_t webGpuGroup = 0;
    std::uint32_t webGpuBinding = 0;

    bool operator==(const WebGpuNativeDescriptorBindingLowering& other) const noexcept
    {
        return logicalSet == other.logicalSet && logicalBinding == other.logicalBinding
            && webGpuGroup == other.webGpuGroup && webGpuBinding == other.webGpuBinding;
    }
};

struct WebGpuNativeDescriptorTableLowering {
    std::uint32_t logicalSet = 0;
    std::uint32_t logicalBinding = 0;
    std::uint32_t webGpuGroup = 0;
    std::uint32_t webGpuBinding = 0;
    std::uint32_t capacity = 0;
    rhi::DescriptorType type = rhi::DescriptorType::SampledTexture;

    bool operator==(const WebGpuNativeDescriptorTableLowering& other) const noexcept
    {
        return logicalSet == other.logicalSet && logicalBinding == other.logicalBinding
            && webGpuGroup == other.webGpuGroup && webGpuBinding == other.webGpuBinding
            && capacity == other.capacity && type == other.type;
    }
};

struct WebGpuNativePipelineBindingFact {
    WebGpuNativeDescriptorBindingLowering mapping;
    rhi::DescriptorBindingDesc shape;
    rhi::DescriptorType physicalType = rhi::DescriptorType::UniformBuffer;
    // Native layout's declared minimum, not a shader footprint. Zero explicitly
    // means WebGPU defers the shader-required size check to resource use.
    std::uint64_t nativeDeclaredMinimumBytes = 0u;
};

struct WebGpuNativePipelineBindingSemantics {
    std::uint64_t shaderPackageHash = 0u;
    std::uint32_t groupCount = 0u;
    std::uint32_t pushGroup = UINT32_MAX;
    std::uint32_t pushBinding = UINT32_MAX;
    bool usesPush = false;
    bool validatedInputAttachment = false;
    // Version1 ordinary source custody; version2 retained original expansion.
    std::uint16_t contractVersion = 1u;

    bool operator==(const WebGpuNativePipelineBindingSemantics& other) const noexcept
    {
        return shaderPackageHash == other.shaderPackageHash && groupCount == other.groupCount
            && pushGroup == other.pushGroup && pushBinding == other.pushBinding
            && usesPush == other.usesPush
            && validatedInputAttachment == other.validatedInputAttachment
            && contractVersion == other.contractVersion;
    }
};

template <class Pipeline>
WebGpuNativePipelineBindingSemantics webGpuPipelineBindingSemantics(
    const Pipeline& pipeline) noexcept
{
    return {pipeline.shaderPackageHash,
        pipeline.bindGroupCount,
        pipeline.pushConstantGroup,
        pipeline.pushConstantBinding,
        pipeline.usesPushConstantGroup,
        pipeline.hasValidatedInputAttachmentLowering,
        pipeline.sourceContractVersion};
}

struct WebGpuNativePipelineStageFact {
    rhi::ShaderStage stage = rhi::ShaderStage::None;
    core::pmr::String entry{&core::defaultCpuMemoryResource()};
    std::array<std::uint8_t, core::Sha256::digestByteCount> sourceSha256{};
    std::uint64_t sourceBytes = 0u;
    // Historical native creation identity for deduplicating the original
    // module's input bytes. Never dereferenced or used as an execution handle.
    void* moduleIdentity = nullptr;
};

class WebGpuNativePipelineShaderSourceContract final {
public:
    struct Inventory {
        std::uint32_t modules = 0u;
        std::uint64_t bytes = 0u;
    };

    static const char* inspect(const core::pmr::Vector<WebGpuNativePipelineStageFact>& stages,
        bool graphics,
        Inventory& output) noexcept
    {
        output = {};
        Inventory inventory;
        rhi::ShaderStage covered = rhi::ShaderStage::None;
        for (std::size_t i = 0u; i < stages.size(); ++i) {
            const auto& stage = stages[i];
            if (!stage.moduleIdentity || !stage.sourceBytes || stage.entry.empty()
                || (stage.stage != rhi::ShaderStage::Vertex
                    && stage.stage != rhi::ShaderStage::Fragment
                    && stage.stage != rhi::ShaderStage::Compute)
                || (covered & stage.stage) != rhi::ShaderStage::None)
                return "RHI-WEBGPU-ORDINARY-PIPELINE-SOURCE-FACTS";
            covered = covered | stage.stage;
            bool shared = false;
            for (std::size_t j = 0u; j < i; ++j)
                if (stages[j].moduleIdentity == stage.moduleIdentity) {
                    if (stages[j].sourceSha256 != stage.sourceSha256
                        || stages[j].sourceBytes != stage.sourceBytes)
                        return "RHI-WEBGPU-ORDINARY-PIPELINE-SOURCE-FACTS";
                    shared = true;
                }
            if (!shared) {
                if (stage.sourceBytes > UINT64_MAX - inventory.bytes)
                    return "RHI-WEBGPU-ORDINARY-PIPELINE-SOURCE-FACTS";
                ++inventory.modules;
                inventory.bytes += stage.sourceBytes;
            }
        }
        if (covered
            != (graphics ? rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment
                         : rhi::ShaderStage::Compute))
            return "RHI-WEBGPU-ORDINARY-PIPELINE-SOURCE-FACTS";
        output = inventory;
        return nullptr;
    }
};

struct WebGpuNativePipelineGraphicsCompatibility {
    bool graphics = false, hasViewport = false, hasScissor = false;
    rhi::ViewportState viewport;
    rhi::ScissorState scissor;
    rhi::PrimitiveTopology topology = rhi::PrimitiveTopology::TriangleList;
    std::uint32_t vertexBufferCount = 0u, sampleCount = 1u;
    rhi::TextureFormat depthStencilFormat = rhi::TextureFormat::Unknown;
    core::pmr::Vector<rhi::TextureFormat> colorFormats{&core::defaultCpuMemoryResource()};

    template <class Pipeline> bool matches(const Pipeline& pipeline) const noexcept
    {
        if constexpr (std::is_same_v<Pipeline, WebGpuNativeRenderPipelinePayload>) {
            return graphics && hasViewport == pipeline.hasViewport
                && hasScissor == pipeline.hasScissor
                && rhi::semantics::viewportStatesEqual(viewport, pipeline.viewport)
                && rhi::semantics::scissorStatesEqual(scissor, pipeline.scissor)
                && topology == pipeline.primitiveTopology
                && vertexBufferCount == pipeline.vertexBufferCount
                && sampleCount == pipeline.sampleCount
                && depthStencilFormat == pipeline.depthStencilFormat
                && colorFormats == pipeline.colorFormats;
        } else
            return !graphics && colorFormats.empty();
    }
};

template <class Pipeline>
WebGpuNativePipelineGraphicsCompatibility webGpuPipelineGraphicsCompatibility(
    const Pipeline& pipeline)
{
    WebGpuNativePipelineGraphicsCompatibility result;
    if constexpr (std::is_same_v<Pipeline, WebGpuNativeRenderPipelinePayload>) {
        result.graphics = true;
        result.hasViewport = pipeline.hasViewport;
        result.hasScissor = pipeline.hasScissor;
        result.viewport = pipeline.viewport;
        result.scissor = pipeline.scissor;
        result.topology = pipeline.primitiveTopology;
        result.vertexBufferCount = pipeline.vertexBufferCount;
        result.sampleCount = pipeline.sampleCount;
        result.depthStencilFormat = pipeline.depthStencilFormat;
        result.colorFormats = pipeline.colorFormats;
    }
    return result;
}

// Construction key belongs only to real native factories. The source
// quota and immutable issued facts are never reconstructed by an observation.
class WebGpuNativeIssuedPipelineContract final {
    struct OrdinaryInputs {
        std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput> compute;
        std::shared_ptr<const rhi::OriginalGraphicsPipelineInput> graphics;
    };

    using SourceCustody = core::VersionedOwnedValue<WebGpuNativePipelineBindingSemantics,
        OrdinaryInputs,
        std::shared_ptr<const WebGpuOriginalDepthTableExpansionPipelineDefinition>,
        2u>;

public:
    class ConstructionKey {
        friend class WebGpuOriginalArtifactComputeFactory;
        friend class WebGpuOriginalArtifactGraphicsFactory;
        friend std::shared_ptr<WebGpuNativeComputePipelinePayload>
        createWebGpuNativeComputePipelinePayload(const rhi::ComputePipelineDesc&);
        friend std::shared_ptr<WebGpuNativeRenderPipelinePayload>
        createWebGpuNativeRenderPipelinePayload(const rhi::RenderPipelineDesc&);
        friend std::shared_ptr<WebGpuNativeComputePipelinePayload>
        createWebGpuNativeBindlessComputePipelinePayload(
            const rhi::BindlessShaderPackage&, std::string_view, core::pmr::String&);
        friend std::shared_ptr<WebGpuNativeRenderPipelinePayload>
        createWebGpuNativeBindlessRenderPipelinePayload(const rhi::BindlessShaderPackage&,
            std::string_view,
            std::string_view,
            const rhi::RenderPipelineDesc&,
            core::pmr::String&,
            const rhi::BindlessAuthorization*,
            const rhi::BindlessGraphicsPushRequest*);
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    WebGpuNativeIssuedPipelineContract(ConstructionKey,
        void* device,
        void* object,
        void* layout,
        WebGpuNativePipelineBindingSemantics semantics,
        const core::pmr::Vector<WebGpuNativeDescriptorSetGroupLowering>& sets,
        const core::pmr::Vector<WebGpuNativeDescriptorBindingLowering>& bindings,
        const core::pmr::Vector<WebGpuNativeDescriptorTableLowering>& tables,
        core::pmr::Vector<WebGpuNativePipelineBindingFact> facts,
        core::pmr::Vector<WebGpuNativePipelineStageFact> stages,
        WebGpuNativePipelineGraphicsCompatibility graphics,
        std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput> artifactInput = {},
        std::shared_ptr<const rhi::OriginalGraphicsPipelineInput> graphicsInput = {});
    WebGpuNativeIssuedPipelineContract(ConstructionKey,
        void* device,
        void* object,
        void* layout,
        WebGpuNativePipelineBindingSemantics semantics,
        const core::pmr::Vector<WebGpuNativeDescriptorSetGroupLowering>& sets,
        const core::pmr::Vector<WebGpuNativeDescriptorBindingLowering>& bindings,
        const core::pmr::Vector<WebGpuNativeDescriptorTableLowering>& tables,
        core::pmr::Vector<WebGpuNativePipelineBindingFact> facts,
        core::pmr::Vector<WebGpuNativePipelineStageFact> stages,
        WebGpuNativePipelineGraphicsCompatibility graphics,
        std::shared_ptr<const WebGpuOriginalDepthTableExpansionPipelineDefinition> expansion);
    ~WebGpuNativeIssuedPipelineContract();
    WebGpuNativeIssuedPipelineContract(const WebGpuNativeIssuedPipelineContract&) = delete;

    bool matches(void* object,
        void* layout,
        const WebGpuNativePipelineBindingSemantics& semantics,
        const core::pmr::Vector<WebGpuNativeDescriptorSetGroupLowering>& sets,
        const core::pmr::Vector<WebGpuNativeDescriptorBindingLowering>& bindings,
        const core::pmr::Vector<WebGpuNativeDescriptorTableLowering>& tables) const noexcept
    {
        return device_ && object_ == object && layout_ == layout && sources_.request() == semantics
            && sets_ == sets && bindings_ == bindings && tables_ == tables;
    }

    void* nativeDevice() const noexcept { return device_; }

    void* nativeObject() const noexcept { return object_; }

    void* nativeLayout() const noexcept { return layout_; }

    const auto& semantics() const noexcept { return sources_.request(); }

    const auto& sets() const noexcept { return sets_; }

    const auto& bindings() const noexcept { return bindings_; }

    const auto& tables() const noexcept { return tables_; }

    const auto& facts() const noexcept { return facts_; }

    const auto& quota() const noexcept { return quota_; }

    const auto& stages() const noexcept { return stages_; }

    const auto& graphics() const noexcept { return graphics_; }

    const auto& artifactInput() const noexcept
    {
        return sources_.request().contractVersion == 2u
            ? sources_.expanded()->space()->computeInput()
            : sources_.ordinary().compute;
    }

    const auto& graphicsInput() const noexcept
    {
        return sources_.request().contractVersion == 2u
            ? sources_.expanded()->space()->graphicsInput()
            : sources_.ordinary().graphics;
    }

    const auto& expansionDefinition() const noexcept { return sources_.expanded(); }

    struct ExpandedBindingFact {
        const WebGpuNativePipelineBindingFact& native;
        const WebGpuOriginalDepthTableExpansionBindingSpace::Entry& original;
    };

    // Exact retained logical-element/native-entry join. Caller first validates
    // version2 custody; no mapping inference, allocation or refcount projection.
    ExpandedBindingFact expandedBindingFact(std::size_t i) const
    {
        return {facts_.at(i), sources_.expanded()->space()->entry(i)};
    }

    std::uint64_t issuedCpuQuotaLimit() const noexcept { return issuedCpuQuotaLimit_; }

private:
    void* const device_;
    void* const object_;
    void* const layout_;
    const SourceCustody sources_;
    const core::pmr::Vector<WebGpuNativeDescriptorSetGroupLowering> sets_;
    const core::pmr::Vector<WebGpuNativeDescriptorBindingLowering> bindings_;
    const core::pmr::Vector<WebGpuNativeDescriptorTableLowering> tables_;
    const core::pmr::Vector<WebGpuNativePipelineBindingFact> facts_;
    const core::pmr::Vector<WebGpuNativePipelineStageFact> stages_;
    const WebGpuNativePipelineGraphicsCompatibility graphics_;
    const std::shared_ptr<core::CpuMemoryQuota> quota_;
    const std::uint64_t issuedCpuQuotaLimit_;
};

class WebGpuNativePipelineOwnership : public core::SharedObjectIdentity {
public:
    const auto& issuedContract() const noexcept { return issued_; }

    bool issueOriginal(WebGpuNativeIssuedPipelineContract::ConstructionKey key,
        void* device,
        void* object,
        void* layout,
        WebGpuNativePipelineBindingSemantics semantics,
        const core::pmr::Vector<WebGpuNativeDescriptorSetGroupLowering>& sets,
        const core::pmr::Vector<WebGpuNativeDescriptorBindingLowering>& bindings,
        const core::pmr::Vector<WebGpuNativeDescriptorTableLowering>& tables,
        WebGpuNativePipelineGraphicsCompatibility graphics,
        std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput> artifactInput = {},
        std::shared_ptr<const rhi::OriginalGraphicsPipelineInput> graphicsInput = {})
    {
        if (issued_ || !device || !object || !layout || semantics.contractVersion != 1u) {
            rhi::RhiErrorHandler::get().report(
                rhi::RhiSeverity::Error, "RHI-WEBGPU-PIPELINE-ISSUANCE-INVALID");
            return false;
        }
        issued_ = core::makeUnique<WebGpuNativeIssuedPipelineContract>(key,
            device,
            object,
            layout,
            semantics,
            sets,
            bindings,
            tables,
            std::move(pendingBindingFacts),
            std::move(pendingShaderFacts),
            std::move(graphics),
            std::move(artifactInput),
            std::move(graphicsInput));
        return true;
    }

    bool issueExpansion(WebGpuNativeIssuedPipelineContract::ConstructionKey key,
        void* device,
        void* object,
        void* layout,
        WebGpuNativePipelineBindingSemantics semantics,
        const core::pmr::Vector<WebGpuNativeDescriptorSetGroupLowering>& sets,
        const core::pmr::Vector<WebGpuNativeDescriptorBindingLowering>& bindings,
        const core::pmr::Vector<WebGpuNativeDescriptorTableLowering>& tables,
        WebGpuNativePipelineGraphicsCompatibility graphics,
        const std::shared_ptr<const WebGpuOriginalDepthTableExpansionPipelineDefinition>&
            definition)
    {
        if (issued_ || !device || !object || !layout || semantics.contractVersion != 2u
            || WebGpuOriginalDepthTableExpansionPipelineDefinition::validateOriginal(definition))
            return false;
        issued_ = core::makeUnique<WebGpuNativeIssuedPipelineContract>(key,
            device,
            object,
            layout,
            semantics,
            sets,
            bindings,
            tables,
            std::move(pendingBindingFacts),
            std::move(pendingShaderFacts),
            std::move(graphics),
            definition);
        return true;
    }

    // Populated only while createNativePipelineLayout emits the real layout.
    core::pmr::Vector<WebGpuNativePipelineBindingFact> pendingBindingFacts{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<WebGpuNativePipelineStageFact> pendingShaderFacts{
        &core::defaultCpuMemoryResource()};

protected:
    WebGpuNativePipelineOwnership() = default;

private:
    core::UniquePtr<WebGpuNativeIssuedPipelineContract> issued_;
};

struct WebGpuOrdinaryPipelineBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxGroups = 0u;
    std::uint32_t maxSets = 0u;
    std::uint32_t maxBindings = 0u;
    std::uint32_t maxTables = 0u;
    std::uint64_t maxOriginalMetadataBytes = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
    std::uint32_t maxShaderStages = 0u, maxShaderModules = 0u, maxColorTargets = 0u;
    std::uint64_t maxShaderSourceBytes = 0u;
};

class WebGpuOrdinaryPipelineObservation final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class WebGpuOrdinaryPipelineObservation;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    WebGpuOrdinaryPipelineObservation(ConstructionKey,
        std::shared_ptr<WebGpuNativeComputePipelinePayload> compute,
        std::shared_ptr<WebGpuNativeRenderPipelinePayload> graphics)
        : compute_(std::move(compute)), graphics_(std::move(graphics))
    {}

    static const char* validateBudget(const WebGpuOrdinaryPipelineBudget& budget) noexcept
    {
        if (budget.contractVersion != 2u)
            return "RHI-WEBGPU-ORDINARY-PIPELINE-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-WEBGPU-ORDINARY-PIPELINE-OPT-IN";
        if (!budget.maxGroups || !budget.maxOriginalMetadataBytes || !budget.maxCpuMetadataBytes
            || !budget.maxShaderStages || !budget.maxShaderModules || !budget.maxShaderSourceBytes)
            return "RHI-WEBGPU-ORDINARY-PIPELINE-BUDGET";
        return nullptr;
    }

    static core::UniquePtr<WebGpuOrdinaryPipelineObservation> prepare(
        const std::shared_ptr<WebGpuNativeComputePipelinePayload>&,
        const WebGpuOrdinaryPipelineBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*& diagnostic);
    static core::UniquePtr<WebGpuOrdinaryPipelineObservation> prepare(
        const std::shared_ptr<WebGpuNativeRenderPipelinePayload>&,
        const WebGpuOrdinaryPipelineBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*& diagnostic);
    const char* validate() const noexcept;

    const char* validateForDevice(void* device) const noexcept
    {
        if (const auto* failure = validate())
            return failure;
        return device && contract().nativeDevice() == device
            ? nullptr
            : "RHI-WEBGPU-ORDINARY-PIPELINE-DEVICE-MISMATCH";
    }

    const WebGpuNativeIssuedPipelineContract& contract() const noexcept;

    const auto& compute() const noexcept { return compute_; }

    const auto& graphics() const noexcept { return graphics_; }

private:
    template <class Pipeline>
    static core::UniquePtr<WebGpuOrdinaryPipelineObservation> prepareImpl(
        const std::shared_ptr<Pipeline>&,
        const WebGpuOrdinaryPipelineBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*&);
    const std::shared_ptr<WebGpuNativeComputePipelinePayload> compute_;
    const std::shared_ptr<WebGpuNativeRenderPipelinePayload> graphics_;
    const WebGpuNativeIssuedPipelineContract* original_ = nullptr;
};

} // namespace arp::rhi_webgpu
