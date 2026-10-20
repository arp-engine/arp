#pragma once

#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_binding_validation.h"
#include "rhi/backends/metal/metal_immutable_buffer_upload.h"
#include "rhi/backends/metal/metal_ordinary_binding.h"
#include "rhi/backends/metal/metal_prepared_function_ownership.h"
#include "rhi/frontend/command_draw_validation.h"
#include "rhi/frontend/descriptor_set_validation.h"
#include "rhi/gpu_device.h"
#include "rhi/semantics/native_backend_capabilities.h"
#include "shader/single_entry_shader_artifact.h"

// region Metal ordinary graphics provider
namespace arp::rhi_metal {

class MetalRhiOrdinaryGraphicsProvider final : public core::SharedObjectIdentity {
    class ConstructionKey {
        friend class MetalRhiOrdinaryGraphicsProvider;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    MetalRhiOrdinaryGraphicsProvider(ConstructionKey,
        std::array<std::shared_ptr<const shader::SingleEntryShaderArtifact>, 2> artifacts,
        std::array<std::shared_ptr<const MetalRhiPreparedGraphicsStageLayout>, 2> stages,
        core::UniquePtr<rhi::ShaderPackageDesc> interface,
        std::shared_ptr<MetalRhiNativePipelinePayload> native,
        const rhi::PipelineLayoutDesc& layout,
        core::UniquePtr<rhi::DescriptorSetLayoutContract> setLayouts,
        rhi::VertexInputConsumptionContract&& vertexInput,
        const rhi::semantics::MetalVertexStreamMapping& vertexMapping,
        std::shared_ptr<core::CpuMemoryQuota> quota)
        : artifacts_(std::move(artifacts)), stages_(std::move(stages)),
          interface_(std::move(interface)), native_(std::move(native)),
          consumption_(layout, interface_->reflection), setLayouts_(std::move(setLayouts)),
          vertexInput_(std::move(vertexInput)), vertexSlots_(vertexMapping.slots),
          quota_(std::move(quota))
    {}

    MetalRhiOrdinaryGraphicsProvider(const MetalRhiOrdinaryGraphicsProvider&) = delete;
    MetalRhiOrdinaryGraphicsProvider& operator=(const MetalRhiOrdinaryGraphicsProvider&) = delete;
    static std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider> create(
        std::shared_ptr<const shader::SingleEntryShaderArtifact> vertex,
        std::shared_ptr<const shader::SingleEntryShaderArtifact> fragment,
        rhi::RenderPipelineDesc&& authored,
        const rhi::semantics::NativeBackendCapabilityContract&,
        const MetalRhiOrdinaryBindingBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        core::pmr::String& diagnostic);

    const auto& artifacts() const noexcept { return artifacts_; }

    const auto& stages() const noexcept { return stages_; }

    const auto& native() const noexcept { return native_; }

    const rhi::DescriptorSetLayoutContract& setLayouts() const noexcept { return *setLayouts_; }

    const auto& quota() const noexcept { return quota_; }

    const rhi::ShaderPackageDesc& interface() const noexcept { return *interface_; }

    const rhi::DescriptorConsumptionContract& consumption() const noexcept { return consumption_; }

    const rhi::VertexInputConsumptionContract& vertexInput() const noexcept { return vertexInput_; }

    bool vertexMappingMatches() const noexcept
    {
        return native_ && native_->vertexStreams.success
            && native_->vertexStreams.slots == vertexSlots_ && !vertexInput_.diagnostic();
    }

    static auto ownerProjection(
        const std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider>& self) noexcept
    {
        const auto roots = std::array<std::shared_ptr<const void>, 7>{self,
            std::shared_ptr<const void>(self, &self->interface()),
            std::shared_ptr<const void>(self, &self->setLayouts()),
            self->artifacts()[0],
            self->artifacts()[1],
            self->stages()[0],
            self->stages()[1]};
        const auto native = MetalRhiPreparedFunctionOwnership::pipelineOwners(self->native());
        std::array<std::shared_ptr<const void>,
            std::tuple_size<decltype(roots)>::value + std::tuple_size<decltype(native)>::value>
            owners;
        std::copy(roots.begin(), roots.end(), owners.begin());
        std::copy(native.begin(), native.end(), owners.begin() + roots.size());
        return owners;
    }

    static void appendOwnerProjection(
        const std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider>& self,
        core::pmr::Vector<std::shared_ptr<const void>>& owners)
    {
        for (auto& owner : ownerProjection(self))
            if (owner)
                owners.push_back(std::move(owner));
    }

private:
    std::array<std::shared_ptr<const shader::SingleEntryShaderArtifact>, 2> artifacts_;
    std::array<std::shared_ptr<const MetalRhiPreparedGraphicsStageLayout>, 2> stages_;
    core::UniquePtr<rhi::ShaderPackageDesc> interface_;
    std::shared_ptr<MetalRhiNativePipelinePayload> native_;
    rhi::DescriptorConsumptionContract consumption_;
    core::UniquePtr<rhi::DescriptorSetLayoutContract> setLayouts_;
    rhi::VertexInputConsumptionContract vertexInput_;
    std::array<std::uint32_t, rhi::semantics::MetalVertexStreamMapping::capacity> vertexSlots_;
    std::shared_ptr<core::CpuMemoryQuota> quota_;
};

// endregion Metal ordinary graphics provider

// region Metal ordinary graphics binding

// Preparation-only; a later recorder must atomically admit this full projection.
class MetalRhiOrdinaryPreparedGraphicsBinding final : public core::SharedObjectIdentity {
    class ConstructionKey {
        friend class MetalRhiOrdinaryPreparedGraphicsBinding;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    MetalRhiOrdinaryPreparedGraphicsBinding(ConstructionKey,
        std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider> provider,
        std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings> payload,
        const rhi::DescriptorSet&,
        std::shared_ptr<MetalRhiResourceRegistry>,
        MetalRhiOrdinaryBindingBudget,
        std::shared_ptr<core::CpuMemoryQuota>);
    MetalRhiOrdinaryPreparedGraphicsBinding(
        const MetalRhiOrdinaryPreparedGraphicsBinding&) = delete;
    MetalRhiOrdinaryPreparedGraphicsBinding& operator=(
        const MetalRhiOrdinaryPreparedGraphicsBinding&) = delete;
    static std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding> prepare(
        std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider>,
        const rhi::DescriptorSet&,
        std::uint32_t,
        std::shared_ptr<MetalRhiResourceRegistry>,
        const MetalRhiOrdinaryBindingBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        core::pmr::String&,
        const std::uint32_t* dynamicOffsets = nullptr,
        std::uint32_t dynamicOffsetCount = 0u);

    const auto& provider() const noexcept { return provider_; }

    const auto& payload() const noexcept { return payload_; }

    const auto& quota() const noexcept { return quota_; }

    const auto& budget() const noexcept { return budget_; }

    const rhi::DescriptorSet* descriptor() const noexcept { return observation_.descriptor(); }

    const auto& registry() const noexcept { return observation_.registry(); }

    rhi::ValidationResult validateObservation() const;

    std::size_t childOwnerCount() const noexcept { return owners_.size(); }

    std::shared_ptr<const void> childOwnerAt(std::size_t index) const noexcept
    {
        return owners_.at(index);
    }

private:
    template <class Visitor> void projectOwners(Visitor&& visitor) const
    {
        const auto add = [&](std::shared_ptr<const void> owner) {
            if (owner)
                visitor(std::move(owner));
        };
        for (auto& owner : MetalRhiOrdinaryGraphicsProvider::ownerProjection(provider_))
            add(std::move(owner));
        add(payload_);
        add(observation_.registry());
        add(payload_->descriptors);
        add(payload_->descriptors->nativeDevice);
        add(payload_->descriptors->nativeObject);
        for (const auto& layout : payload_->layouts)
            add(layout);
        for (const auto& stage : payload_->stages)
            if (stage)
                MetalRhiPreparedFunctionOwnership::visit(stage, observation_.registry(), visitor);
    }

    std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider> provider_;
    std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings> payload_;
    MetalRhiOrdinaryDescriptorObservation observation_;
    MetalRhiOrdinaryBindingBudget budget_;
    std::shared_ptr<core::CpuMemoryQuota> quota_;
    core::SharedOwnerProjection owners_;
};

// endregion Metal ordinary graphics binding

// region Metal ordinary graphics vertex

class MetalRhiOrdinaryPreparedVertexBinding final : public core::SharedObjectIdentity {
    class ConstructionKey {
        friend class MetalRhiOrdinaryPreparedVertexBinding;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    MetalRhiOrdinaryPreparedVertexBinding(ConstructionKey,
        std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider> provider,
        std::shared_ptr<MetalRhiResourceRegistry> registry,
        rhi::GpuResourceId id,
        MetalRhiRegisteredResource resource,
        std::uint32_t slot,
        std::uint64_t offset,
        MetalRhiOrdinaryBindingBudget budget,
        std::shared_ptr<core::CpuMemoryQuota> quota)
        : provider_(std::move(provider)), registry_(std::move(registry)), id_(id),
          resource_(std::move(resource)), slot_(slot),
          physicalSlot_(provider_->native()->vertexStreams.slots[slot]), offset_(offset),
          observation_(resource_.buffer->resource), budget_(budget), quota_(std::move(quota))
    {
        owners_.capture([&](auto&& visitor) {
            projectOwners(visitor);
        });
    }

    static std::shared_ptr<const MetalRhiOrdinaryPreparedVertexBinding> prepare(
        std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider>,
        const rhi::GpuBuffer&,
        std::uint32_t slot,
        std::uint64_t offset,
        std::shared_ptr<MetalRhiResourceRegistry>,
        const MetalRhiOrdinaryBindingBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        core::pmr::String&);
    const char* validate() const noexcept;

    const char* validateDraw(std::uint64_t count,
        std::uint64_t first,
        std::uint64_t instances,
        std::uint64_t firstInstance) const noexcept
    {
        if (const auto* id = validate())
            return id;
        return provider_->vertexInput().validateRange(
            slot_, resource_.bufferSizeBytes, offset_, count, first, instances, firstInstance);
    }

    const auto& provider() const noexcept { return provider_; }

    const auto& registry() const noexcept { return registry_; }

    const auto& quota() const noexcept { return quota_; }

    const auto& buffer() const noexcept { return resource_.buffer; }

    auto slot() const noexcept { return slot_; }

    auto physicalSlot() const noexcept { return physicalSlot_; }

    auto offset() const noexcept { return offset_; }

    auto bufferBytes() const noexcept { return resource_.bufferSizeBytes; }

    std::size_t childOwnerCount() const noexcept { return owners_.size(); }

    auto childOwnerAt(std::size_t i) const noexcept { return owners_.at(i); }

private:
    template <class Visitor> void projectOwners(Visitor&& visitor) const
    {
        for (const auto& owner : MetalRhiOrdinaryGraphicsProvider::ownerProjection(provider_))
            if (owner)
                visitor(owner);
        visitor(registry_);
        for (const auto& owner : MetalRhiResourceOwnerProjection::owners(
                 rhi::DescriptorResourceKind::Buffer, resource_.buffer, {}, {}, {}))
            if (owner)
                visitor(owner);
    }

    std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider> provider_;
    std::shared_ptr<MetalRhiResourceRegistry> registry_;
    rhi::GpuResourceId id_;
    MetalRhiRegisteredResource resource_;
    std::uint32_t slot_, physicalSlot_;
    std::uint64_t offset_;
    MetalRhiNativeResourceObservation observation_;
    MetalRhiOrdinaryBindingBudget budget_;
    std::shared_ptr<core::CpuMemoryQuota> quota_;
    core::SharedOwnerProjection owners_;
};

// endregion Metal ordinary graphics vertex

// region Metal ordinary graphics push

class MetalRhiOrdinaryPreparedGraphicsPush final {
    class ConstructionKey {
        friend class MetalRhiOrdinaryPreparedGraphicsPush;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    MetalRhiOrdinaryPreparedGraphicsPush(ConstructionKey,
        std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider>,
        const rhi::PushConstantsDesc&,
        MetalRhiOrdinaryBindingBudget,
        std::shared_ptr<core::CpuMemoryQuota>,
        std::array<MetalRhiStageBufferRange, 2>);
    MetalRhiOrdinaryPreparedGraphicsPush(const MetalRhiOrdinaryPreparedGraphicsPush&) = delete;
    MetalRhiOrdinaryPreparedGraphicsPush& operator=(
        const MetalRhiOrdinaryPreparedGraphicsPush&) = delete;
    static std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsPush> prepare(
        std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider>,
        const rhi::PushConstantsDesc&,
        const MetalRhiOrdinaryBindingBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        core::pmr::String&);

    const auto& provider() const noexcept { return provider_; }

    const auto& bytes() const noexcept { return block_.bytes(); }

    const auto& quota() const noexcept { return block_.quota(); }

    const auto& ranges() const noexcept { return ranges_; }

    rhi::ValidationResult validate() const;

    std::size_t childOwnerCount() const noexcept { return owners_.size(); }

    std::shared_ptr<const void> childOwnerAt(std::size_t i) const noexcept
    {
        return i < owners_.size() ? owners_[i] : nullptr;
    }

private:
    std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider> provider_;
    MetalRhiOrdinaryPushBlock block_;
    std::array<MetalRhiStageBufferRange, 2> ranges_;
    // Same projection retained by the command; replacing a provider's native
    // member cannot silently change the set of admitted owners.
    core::pmr::Vector<std::shared_ptr<const void>> owners_{&core::defaultCpuMemoryResource()};
};

// endregion Metal ordinary graphics push

// region Metal ordinary graphics pass

// MTLRenderPassDescriptor's specified colorAttachment indices are 0..7.
inline constexpr std::size_t kMetalRenderPassColorSlots = 8u;

struct MetalRhiNativeRenderPassPreparation {
    struct Attachment {
        rhi::GpuResourceId id;
        MetalRhiRegisteredResource resource;
        std::shared_ptr<void> nativeDevice, nativeObject, allocationToken;
    };

    std::shared_ptr<void> descriptor;
    std::array<Attachment, kMetalRenderPassColorSlots * 2u + 1u> attachments;
    std::size_t attachmentCount = 0u;
    std::uint32_t width = 0u, height = 0u, subpassCount = 1u, sampleCount = 0u;
    std::array<rhi::TextureFormat, kMetalRenderPassColorSlots> colorFormats{};
    std::uint32_t colorCount = 0u;
    rhi::TextureFormat depthFormat = rhi::TextureFormat::Unknown;
    bool layered = false;
    rhi::Color firstClearColor;
};

// Shared allocation-free registry/native snapshot validation for prepared passes.
const char* validateMetalRhiNativeRenderPassResources(const MetalRhiNativeRenderPassPreparation&,
    const std::shared_ptr<MetalRhiResourceRegistry>&,
    const std::shared_ptr<void>&) noexcept;

class MetalRhiOrdinaryPreparedRenderPass final : public core::SharedObjectIdentity {
    class ConstructionKey {
        friend class MetalRhiOrdinaryPreparedRenderPass;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    MetalRhiOrdinaryPreparedRenderPass(ConstructionKey,
        std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider>,
        MetalRhiNativeRenderPassPreparation,
        std::shared_ptr<MetalRhiResourceRegistry>,
        std::shared_ptr<core::CpuMemoryQuota>);
    MetalRhiOrdinaryPreparedRenderPass(const MetalRhiOrdinaryPreparedRenderPass&) = delete;
    MetalRhiOrdinaryPreparedRenderPass& operator=(
        const MetalRhiOrdinaryPreparedRenderPass&) = delete;
    static std::shared_ptr<const MetalRhiOrdinaryPreparedRenderPass> prepare(
        const rhi::RenderPassDesc&,
        std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider>,
        std::shared_ptr<MetalRhiResourceRegistry>,
        const MetalRhiOrdinaryBindingBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        core::pmr::String&);
    rhi::ValidationResult validateObservation() const;
    rhi::ValidationResult validateProvider(const MetalRhiOrdinaryGraphicsProvider&) const;

    const auto& provider() const noexcept { return provider_; }

    const auto& registry() const noexcept { return registry_; }

    const auto& quota() const noexcept { return quota_; }

    const auto& native() const noexcept { return native_; }

    std::size_t childOwnerCount() const noexcept { return owners_.size(); }

    std::shared_ptr<const void> childOwnerAt(std::size_t i) const noexcept
    {
        return i < owners_.size() ? owners_[i] : nullptr;
    }

private:
    std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider> provider_;
    MetalRhiNativeRenderPassPreparation native_;
    std::shared_ptr<MetalRhiResourceRegistry> registry_;
    std::shared_ptr<core::CpuMemoryQuota> quota_;
    core::pmr::Vector<std::shared_ptr<const void>> owners_{&core::defaultCpuMemoryResource()};
};

// endregion Metal ordinary graphics pass

// region Metal ordinary graphics index

class MetalRhiOrdinaryPreparedIndexBinding final : public core::SharedObjectIdentity {
    class ConstructionKey {
        friend class MetalRhiOrdinaryPreparedIndexBinding;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    MetalRhiOrdinaryPreparedIndexBinding(ConstructionKey,
        std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider> provider,
        std::shared_ptr<MetalRhiResourceRegistry> registry,
        rhi::IndexInputConsumptionContract&& content,
        std::shared_ptr<rhi::GpuBuffer> buffer,
        MetalRhiRegisteredResource resource,
        MetalRhiOrdinaryBindingBudget budget,
        std::shared_ptr<core::CpuMemoryQuota> quota)
        : provider_(std::move(provider)), registry_(std::move(registry)),
          content_(std::move(content)), buffer_(std::move(buffer)), resource_(std::move(resource)),
          observation_(resource_.buffer->resource), budget_(budget), quota_(std::move(quota))
    {
        owners_.capture([&](auto&& visitor) {
            projectOwners(visitor);
        });
    }

    MetalRhiOrdinaryPreparedIndexBinding(const MetalRhiOrdinaryPreparedIndexBinding&) = delete;
    MetalRhiOrdinaryPreparedIndexBinding& operator=(
        const MetalRhiOrdinaryPreparedIndexBinding&) = delete;
    static std::shared_ptr<const MetalRhiOrdinaryPreparedIndexBinding> prepare(
        std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider>,
        rhi::GpuDevice&,
        const void*,
        std::size_t,
        rhi::IndexFormat,
        core::UnsignedElementSpan::ByteOrder,
        rhi::IndexInputConsumptionContract::PrimitiveRestart,
        std::shared_ptr<MetalRhiResourceRegistry>,
        const MetalRhiOrdinaryBindingBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        core::pmr::String&);
    const char* validate() const noexcept;

    const auto& provider() const noexcept { return provider_; }

    const auto& registry() const noexcept { return registry_; }

    const auto& quota() const noexcept { return quota_; }

    const auto& content() const noexcept { return content_; }

    const rhi::GpuBuffer& resource() const noexcept { return *buffer_; }

    const auto& buffer() const noexcept { return resource_.buffer; }

    std::size_t childOwnerCount() const noexcept { return owners_.size(); }

    auto childOwnerAt(std::size_t i) const noexcept { return owners_.at(i); }

private:
    template <class Visitor> void projectOwners(Visitor&& visitor) const
    {
        for (const auto& owner : MetalRhiOrdinaryGraphicsProvider::ownerProjection(provider_))
            if (owner)
                visitor(owner);
        visitor(registry_);
        visitor(buffer_);
        for (const auto& owner : MetalRhiResourceOwnerProjection::owners(
                 rhi::DescriptorResourceKind::Buffer, resource_.buffer, {}, {}, {}))
            if (owner)
                visitor(owner);
    }

    std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider> provider_;
    std::shared_ptr<MetalRhiResourceRegistry> registry_;
    rhi::IndexInputConsumptionContract content_;
    std::shared_ptr<rhi::GpuBuffer> buffer_;
    MetalRhiRegisteredResource resource_;
    MetalRhiNativeResourceObservation observation_;
    MetalRhiOrdinaryBindingBudget budget_;
    std::shared_ptr<core::CpuMemoryQuota> quota_;
    core::SharedOwnerProjection owners_;
};

class MetalRhiOrdinaryPreparedIndexedDraw final {
    class ConstructionKey {
        friend class MetalRhiOrdinaryPreparedIndexedDraw;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    MetalRhiOrdinaryPreparedIndexedDraw(ConstructionKey,
        std::shared_ptr<const MetalRhiOrdinaryPreparedIndexBinding> index,
        rhi::IndexInputConsumptionContract::DrawRange range,
        std::uint64_t offset,
        std::uint32_t instances,
        std::uint32_t firstInstance)
        : index_(std::move(index)), range_(range), offset_(offset), instances_(instances),
          firstInstance_(firstInstance)
    {}

    MetalRhiOrdinaryPreparedIndexedDraw(const MetalRhiOrdinaryPreparedIndexedDraw&) = delete;
    MetalRhiOrdinaryPreparedIndexedDraw& operator=(
        const MetalRhiOrdinaryPreparedIndexedDraw&) = delete;
    static std::shared_ptr<const MetalRhiOrdinaryPreparedIndexedDraw> prepare(
        std::shared_ptr<const MetalRhiOrdinaryPreparedIndexBinding>,
        std::uint64_t bindingByteOffset,
        std::uint32_t firstIndex,
        std::uint32_t count,
        std::int32_t baseVertex,
        std::uint32_t instances,
        std::uint32_t firstInstance,
        core::pmr::String&);

    const char* validate() const noexcept { return index_->validate(); }

    const auto& index() const noexcept { return index_; }

    auto range() const noexcept { return range_; }

    auto offset() const noexcept { return offset_; }

    auto instances() const noexcept { return instances_; }

    auto firstInstance() const noexcept { return firstInstance_; }

    std::size_t childOwnerCount() const noexcept { return index_->childOwnerCount() + 1u; }

    std::shared_ptr<const void> childOwnerAt(std::size_t i) const noexcept
    {
        return !i ? std::shared_ptr<const void>(index_) : index_->childOwnerAt(i - 1u);
    }

private:
    std::shared_ptr<const MetalRhiOrdinaryPreparedIndexBinding> index_;
    rhi::IndexInputConsumptionContract::DrawRange range_;
    std::uint64_t offset_;
    std::uint32_t instances_, firstInstance_;
};

// endregion Metal ordinary graphics index

// region Metal ordinary graphics indirect

// Immutable native host argument storage only; no indirect recording entry is
// authorized by this preparation. Index content and argument schema are bound
// by the issuing factory, never by caller-authored range observations.
class MetalRhiOrdinaryPreparedIndirectArguments final {
    class ConstructionKey {
        friend class MetalRhiOrdinaryPreparedIndirectArguments;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    MetalRhiOrdinaryPreparedIndirectArguments(ConstructionKey,
        std::shared_ptr<const MetalRhiOrdinaryPreparedIndexBinding> index,
        rhi::IndirectArgumentConsumptionContract&& content,
        MetalRhiImmutableBufferUpload::Result upload,
        MetalRhiOrdinaryBindingBudget budget)
        : index_(std::move(index)), content_(std::move(content)), upload_(std::move(upload)),
          observation_(upload_.resource.buffer->resource), budget_(budget)
    {
        owners_.capture([&](auto&& visitor) {
            projectOwners(visitor);
        });
    }

    MetalRhiOrdinaryPreparedIndirectArguments(
        const MetalRhiOrdinaryPreparedIndirectArguments&) = delete;
    MetalRhiOrdinaryPreparedIndirectArguments& operator=(
        const MetalRhiOrdinaryPreparedIndirectArguments&) = delete;
    static std::shared_ptr<const MetalRhiOrdinaryPreparedIndirectArguments> prepare(
        std::shared_ptr<const MetalRhiOrdinaryPreparedIndexBinding>,
        rhi::GpuDevice&,
        rhi::IndirectArgumentConsumptionContract::Source,
        rhi::IndirectArgumentConsumptionContract::Budget,
        const MetalRhiOrdinaryBindingBudget&,
        core::pmr::String&);
    const char* validate() const noexcept;

    const auto& index() const noexcept { return index_; }

    const auto& content() const noexcept { return content_; }

    const rhi::GpuBuffer& resource() const noexcept { return *upload_.buffer; }

    const auto& buffer() const noexcept { return upload_.resource.buffer; }

    std::size_t childOwnerCount() const noexcept { return owners_.size(); }

    auto childOwnerAt(std::size_t i) const noexcept { return owners_.at(i); }

private:
    template <class Visitor> void projectOwners(Visitor&& visitor) const
    {
        visitor(index_);
        for (std::size_t i = 0u; i < index_->childOwnerCount(); ++i)
            visitor(index_->childOwnerAt(i));
        visitor(upload_.buffer);
        for (const auto& owner : MetalRhiResourceOwnerProjection::owners(
                 rhi::DescriptorResourceKind::Buffer, upload_.resource.buffer, {}, {}, {}))
            if (owner)
                visitor(owner);
    }

    std::shared_ptr<const MetalRhiOrdinaryPreparedIndexBinding> index_;
    rhi::IndirectArgumentConsumptionContract content_;
    MetalRhiImmutableBufferUpload::Result upload_;
    MetalRhiNativeResourceObservation observation_;
    MetalRhiOrdinaryBindingBudget budget_;
    core::SharedOwnerProjection owners_;
};
} // namespace arp::rhi_metal

// endregion Metal ordinary graphics indirect
