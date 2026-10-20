#pragma once
#include "rhi/backends/webgpu/webgpu_completed_texture_upload_declaration.h"
#include "rhi/backends/webgpu/webgpu_original_resource_states.h"
#include "rhi/frontend/transfer_upload_copy.h"

namespace arp::rhi_webgpu {
struct WebGpuOriginalTextureUploadTransactionBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxOriginalSources = 0u, maxMipMappings = 0u;
    std::uint64_t maxRetainedContentBytes = 0u, maxCpuMetadataBytes = 0u;
    rhi::TextureUploadContentTransaction::Budget content;
};

// OriginalTextureUploadTransaction/1. A sealed initial stream plus exact current
// content ownership. Submitted/Completed mutations are private to the existing
// common submission owner; public CPU apply never grants that authority.
class WebGpuOriginalTextureUploadTransaction final
    : public WebGpuCompletedTextureUploadDeclaration {
    using States = WebGpuOriginalResourceStates;
    using Content = rhi::TextureUploadContentTransaction;
    using Source = WebGpuOriginalTextureUploadSource;

    class ConstructionKey {
        friend class WebGpuOriginalTextureUploadTransaction;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    enum class Status { Prepared, Pending, Submitted, Completed, Abandoned, Invalidated };

    struct Lowering {
        bool validatorOnly = false, currentMetadataImplemented = false;
        const char* diagnostic = "RHI-WEBGPU-TEXTURE-TRANSACTION-BACKEND-UNKNOWN";
    };

    static constexpr Lowering lowering(rhi::Backend backend) noexcept
    {
        switch (backend) {
        case rhi::Backend::Null:
            return {true, false, "RHI-WEBGPU-TEXTURE-TRANSACTION-VALIDATOR-ONLY"};
        case rhi::Backend::Vulkan:
        case rhi::Backend::Metal:
            return {false, false, "RHI-WEBGPU-TEXTURE-TRANSACTION-NATIVE-UNIMPLEMENTED"};
        case rhi::Backend::WebGPU:
            return {false, true, "RHI-WEBGPU-TEXTURE-TRANSACTION-RECORDING-UNPROVEN"};
        case rhi::Backend::WebGL:
            break;
        }
        return {};
    }

    WebGpuOriginalTextureUploadTransaction(ConstructionKey,
        std::shared_ptr<const States> states,
        std::shared_ptr<Content> content,
        std::uint32_t maxMipMappings)
        : states_(std::move(states)), content_(std::move(content)), decisions_(maxMipMappings)
    {}

    ~WebGpuOriginalTextureUploadTransaction() { abandon(); }

    static const char* validateBudget(
        const WebGpuOriginalTextureUploadTransactionBudget& budget) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RHI-WEBGPU-TEXTURE-TRANSACTION-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-WEBGPU-TEXTURE-TRANSACTION-OPT-IN";
        if (!budget.maxOriginalSources || !budget.maxMipMappings || !budget.maxRetainedContentBytes
            || !budget.maxCpuMetadataBytes)
            return "RHI-WEBGPU-TEXTURE-TRANSACTION-BUDGET";
        if (budget.content.contractVersion != 1u || !budget.content.prototypeOptIn
            || !budget.content.maxCpuMetadataBytes)
            return "RHI-WEBGPU-TEXTURE-TRANSACTION-CONTENT-BUDGET";
        return nullptr;
    }

    static const char* validateInitialStream(const std::shared_ptr<const States>& states) noexcept
    {
        if (!states || !states->hasQuotaBoundSharedOwner(states))
            return "RHI-WEBGPU-TEXTURE-TRANSACTION-STATES-OWNER";
        if (const auto* id = states->validate())
            return id;
        const auto& inputs = *states->inputs();
        const auto& usage = *inputs.scalars()->usage();
        const auto& graph = *usage.projection();
        const auto& commands = usage.commands();
        using K = rhi::CommandUsageKind;
        if (commands.size() != 4u || commands[0].kind != K::BeginTransferPass
            || commands[1].kind != K::TextureUpload || commands[2].kind != K::EndTransferPass
            || commands[3].kind != K::Finish)
            return "RHI-WEBGPU-TEXTURE-TRANSACTION-LANGUAGE";
        if (graph.transfer() || graph.resources() || !graph.pipelines().empty()
            || !graph.physicalGroups().empty() || !graph.originalUploads().empty()
            || graph.originalTextureUploads().size() != 1u || graph.references().size() != 2u
            || inputs.passes() || inputs.descriptors().size() != 1u
            || inputs.descriptors()[0].commandIndex != 1u || states->sourceReferences() != 2u
            || states->states().size() != 2u
            || !std::holds_alternative<rhi::TextureUploadOriginalInputArguments>(
                inputs.descriptors()[0].arguments)
            || commands[0].referenceCount || commands[1].firstReference
            || commands[1].referenceCount != 2u || commands[2].referenceCount
            || commands[3].referenceCount)
            return "RHI-WEBGPU-TEXTURE-TRANSACTION-COVERAGE";
        const auto& source = graph.originalTextureUploads()[0];
        const auto* staging = states->find(source->resource().id);
        const auto* target = states->find(source->destination()->resource().id);
        if (!staging || !target || staging->source.domain != States::Domain::Buffer
            || target->source.domain != States::Domain::UniformWholeTexture
            || staging->observation.value != rhi::ResourceState::CopySrc
            || target->observation.value != rhi::ResourceState::CopyDst)
            return "RHI-WEBGPU-TEXTURE-TRANSACTION-INITIAL-STATES";
        return nullptr;
    }

    static std::shared_ptr<WebGpuOriginalTextureUploadTransaction> prepareOriginal(
        std::shared_ptr<const States> states,
        const WebGpuOriginalTextureUploadTransactionBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        std::shared_ptr<core::CpuMemoryQuota> contentQuota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(budget);
        if (diagnostic)
            return {};
        if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes || !contentQuota
            || contentQuota->limitBytes() != budget.content.maxCpuMetadataBytes) {
            diagnostic = "RHI-WEBGPU-TEXTURE-TRANSACTION-QUOTA";
            return {};
        }
        diagnostic = validateInitialStream(states);
        if (diagnostic)
            return {};
        const auto& original =
            states->inputs()->scalars()->usage()->projection()->originalTextureUploads();
        const auto& source = original[0];
        if (original.size() > budget.maxOriginalSources
            || source->packing()->mipLayouts().size() > budget.maxMipMappings
            || source->contentSource()->retainedBytes() > budget.maxRetainedContentBytes) {
            diagnostic = "RHI-WEBGPU-TEXTURE-TRANSACTION-SOURCE-CAPACITY";
            return {};
        }
        const auto& target = source->destination();
        auto content = Content::prepareOriginal(target->contentLedger(),
            target->source(),
            *target->contentExpectation(),
            budget.content,
            std::move(contentQuota),
            diagnostic);
        if (!content)
            return {};
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto result = core::makeUnique<WebGpuOriginalTextureUploadTransaction>(
                ConstructionKey{}, std::move(states), std::move(content), budget.maxMipMappings);
            for (std::size_t i = 0u; i < source->packing()->mipLayouts().size(); ++i) {
                rhi::semantics::TextureUploadPackedCopy mapped;
                diagnostic = rhi::semantics::TextureUploadPackedCopyMapping::mapOriginal(
                    1u, true, source->packing(), source->resource().id, i, mapped);
                if (diagnostic)
                    return {};
                rhi::semantics::TextureUploadCopyLowering::Decision decision;
                diagnostic =
                    rhi::semantics::TextureUploadCopyLowering::lowerOriginal(rhi::Backend::WebGPU,
                        1u,
                        true,
                        source->packing(),
                        source->resource().id,
                        i,
                        mapped,
                        decision);
                if (diagnostic)
                    return {};
                result->decisions_.append(std::move(decision));
            }
            return shareOriginal(std::move(result));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-WEBGPU-TEXTURE-TRANSACTION-ALLOCATION";
            return {};
        }
    }

    const char* validate() const noexcept
    {
        if (status_ == Status::Abandoned)
            return "RHI-WEBGPU-TEXTURE-TRANSACTION-ABANDONED";
        if (status_ == Status::Invalidated)
            return "RHI-WEBGPU-TEXTURE-TRANSACTION-INVALIDATED";
        // Sealed inputs were fully validated at cold preparation; their owners,
        // descriptors, labels, command order and budgets have no mutation API.
        // Rechecking initial content here would reject this exact pending journal.
        const auto expected = status_ == Status::Prepared ? Content::Status::Prepared
            : status_ == Status::Pending                  ? Content::Status::Pending
            : status_ == Status::Submitted                ? Content::Status::Submitted
                                                          : Content::Status::Completed;
        if (const auto* id = validateNativeContentDeclaration(expected))
            return id;
        const auto& source = originalSource();
        for (const auto& original : states_->states()) {
            const auto& control = original.source.control;
            if (!control.active())
                return "RHI-WEBGPU-TEXTURE-TRANSACTION-RESOURCE-INVALIDATED";
            if (control.pending())
                return "RHI-WEBGPU-TEXTURE-TRANSACTION-RESOURCE-PENDING";
            if (!control.matches(original.observation))
                return "RHI-WEBGPU-TEXTURE-TRANSACTION-RESOURCE-STALE";
        }
        if (decisions_.values().size() != source->packing()->mipLayouts().size())
            return "RHI-WEBGPU-TEXTURE-TRANSACTION-DECISION-COVERAGE";
        for (std::size_t i = 0u; i < decisions_.values().size(); ++i) {
            const auto& decision = decisions_.values()[i];
            if (const auto* id = rhi::semantics::TextureUploadCopyLowering::validateOriginal(
                    rhi::Backend::WebGPU,
                    1u,
                    true,
                    source->packing(),
                    source->resource().id,
                    i,
                    decision.original,
                    decision))
                return id;
        }
        return nullptr;
    }

    // Completed native/content fact only. Current resource access and readback
    // region/state ownership remain separate contracts; no copy/map authority.
    static const char* validateCompletedDeclarationOriginal(
        const std::shared_ptr<const WebGpuOriginalTextureUploadTransaction>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-WEBGPU-TEXTURE-COMPLETED-OWNER";
        return self->validateCompletedDeclaration();
    }

    static const char* apply(
        const std::shared_ptr<WebGpuOriginalTextureUploadTransaction>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-WEBGPU-TEXTURE-TRANSACTION-OWNER";
        if (self->status_ != Status::Prepared)
            return "RHI-WEBGPU-TEXTURE-TRANSACTION-STATUS";
        if (const auto* id = self->validate())
            return id;
        if (const auto* id = Content::apply(self->content_))
            return id;
        self->status_ = Status::Pending;
        if (const auto* id = self->validate()) {
            self->abandon();
            return id;
        }
        return nullptr;
    }

    static const char* mapPackedCopyCurrent(
        const std::shared_ptr<const WebGpuOriginalTextureUploadTransaction>& self,
        std::size_t mipIndex,
        rhi::semantics::TextureUploadPackedCopy& output) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-WEBGPU-TEXTURE-TRANSACTION-OWNER";
        if (const auto* id = self->validate())
            return id;
        const auto& source = self->originalSource();
        return rhi::semantics::TextureUploadPackedCopyMapping::mapOriginal(
            1u, true, source->packing(), source->resource().id, mipIndex, output);
    }

    void abandon() noexcept
    {
        if (status_ == Status::Abandoned || status_ == Status::Invalidated
            || status_ == Status::Completed)
            return;
        if (status_ == Status::Submitted) {
            invalidate();
            return;
        }
        if (webGpuNativeDeviceLost()) {
            invalidate();
            return;
        }
        content_->abandon();
        status_ = Status::Abandoned;
    }

    void invalidate() noexcept
    {
        if (status_ == Status::Abandoned || status_ == Status::Invalidated)
            return;
        if (status_ == Status::Submitted || status_ == Status::Completed)
            for (const auto& state : states_->states()) {
                if (state.source.domain == States::Domain::Buffer)
                    originalSource()->resource().buffer->invalidateRecordedState();
                else
                    originalSource()->destination()->resource().texture->invalidateRecordedState();
            }
        content_->invalidate();
        status_ = Status::Invalidated;
    }

    Status status() const noexcept { return status_; }

    const auto& states() const noexcept { return states_; }

    const auto& decisions() const noexcept { return decisions_.values(); }

    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = validate())
            return id;
        return "RHI-WEBGPU-TEXTURE-TRANSACTION-RECORDING-UNPROVEN";
    }

private:
    friend class WebGpuOrdinarySubmission;

    const char* validateCompletedDeclaration() const noexcept override
    {
        if (status_ != Status::Completed)
            return "RHI-WEBGPU-TEXTURE-COMPLETED-STATUS";
        if (const auto* id = validateNativeContentDeclaration(Content::Status::Completed))
            return id;
        for (const auto& original : states_->states()) {
            if (!original.source.control.active()
                || !rhi::semantics::isKnownResourceState(original.source.control.state()))
                return "RHI-WEBGPU-TEXTURE-COMPLETED-RESOURCE-INVALIDATED";
        }
        return nullptr;
    }

    const rhi::GpuTexture* completedTexture() const noexcept override
    {
        return originalSource()->destination()->texture();
    }

    const WebGpuTransferResourceObservation& completedTextureResource() const noexcept override
    {
        return originalSource()->destination()->resource();
    }

    const std::shared_ptr<WebGpuRhiResourceRegistry>& completedRegistry() const noexcept override
    {
        return originalSource()->destination()->registry();
    }

    const std::shared_ptr<const rhi::TextureUploadPacking>&
    completedPacking() const noexcept override
    {
        return originalSource()->packing();
    }

    std::uint64_t retainedContentBytes() const noexcept override
    {
        return originalSource()->contentSource()->retainedBytes();
    }

    std::uint64_t retainedNativeAllocationBytes() const noexcept override
    {
        return states_->inputs()->scalars()->usage()->projection()->retainedAllocationBytes();
    }

    std::size_t retainedNativeAllocationRecordCount() const noexcept override { return 2u; }

    core::SharedAllocationProjection::Record retainedNativeAllocationRecordAt(
        std::size_t index) const noexcept override
    {
        if (index == 0u)
            return originalSource()->resource().allocationRecord();
        if (index == 1u)
            return originalSource()->destination()->resource().allocationRecord();
        return {};
    }

    const WebGpuTransferResourceObservation* retainedNativeResourceAt(
        std::size_t index) const noexcept override
    {
        if (index == 0u)
            return &originalSource()->resource();
        if (index == 1u)
            return &originalSource()->destination()->resource();
        return nullptr;
    }

    const char* validateNativeContentDeclaration(Content::Status expected) const noexcept
    {
        const auto& source = originalSource();
        if (const auto* id = source->validateReadyDeclaration())
            return id;
        const auto& target = source->destination();
        if (!content_ || !content_->hasQuotaBoundSharedOwner(content_)
            || !content_->matchesOriginalAdmission(
                target->contentLedger(), target->source(), *target->contentExpectation()))
            return "RHI-WEBGPU-TEXTURE-TRANSACTION-CONTENT-OWNER";
        if (const auto* id = content_->validate())
            return id;
        if (content_->status() != expected)
            return "RHI-WEBGPU-TEXTURE-TRANSACTION-CONTENT-STATUS";
        return nullptr;
    }

    static const char* markSubmitted(
        const std::shared_ptr<WebGpuOriginalTextureUploadTransaction>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-WEBGPU-TEXTURE-TRANSACTION-OWNER";
        if (self->status_ != Status::Pending)
            return "RHI-WEBGPU-TEXTURE-TRANSACTION-SUBMIT-STATUS";
        if (const auto* id = self->validate())
            return id;
        if (const auto* id = self->content_->markNativeSubmitted()) {
            self->invalidate();
            return id;
        }
        self->status_ = Status::Submitted;
        return nullptr;
    }

    static const char* markCompleted(
        const std::shared_ptr<WebGpuOriginalTextureUploadTransaction>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-WEBGPU-TEXTURE-TRANSACTION-OWNER";
        if (self->status_ != Status::Submitted)
            return "RHI-WEBGPU-TEXTURE-TRANSACTION-COMPLETE-STATUS";
        if (const auto* id = self->validate())
            return id;
        if (const auto* id = self->content_->markNativeCompleted()) {
            self->invalidate();
            return id;
        }
        self->status_ = Status::Completed;
        return nullptr;
    }

    const std::shared_ptr<const Source>& originalSource() const noexcept
    {
        return states_->inputs()->scalars()->usage()->projection()->originalTextureUploads()[0];
    }

    const std::shared_ptr<const States> states_;
    const std::shared_ptr<Content> content_;
    core::BoundedSequence<rhi::semantics::TextureUploadCopyLowering::Decision> decisions_;
    Status status_ = Status::Prepared;
};
} // namespace arp::rhi_webgpu
