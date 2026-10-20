#pragma once

#include "core/row_major_index_grid.h"
#include "rhi/backends/webgpu/webgpu_canonical_readback_joins.h"
#include "rhi/backends/webgpu/webgpu_ordinary_resource_retention.h"
#include "rhi/backends/webgpu/webgpu_pipeline_preparation.h"
#include "rhi/backends/webgpu/webgpu_upload_preparation.h"
#include "rhi/frontend/command_recording.h"
#include "rhi/frontend/command_state.h"
#include "rhi/frontend/command_stream_program.h"
#include "rhi/frontend/completed_depth_read_input.h"
#include "rhi/frontend/descriptor_buffer_write_validation.h"
#include "rhi/frontend/descriptor_table_publication.h"
#include "rhi/frontend/resource_binding_layout.h"
#include "rhi/semantics/command_lowering.h"

namespace arp::rhi_webgpu {
class WebGpuCanonicalIssuerPreparation;

// Backend-private native resource closure over the exact canonical root. This
// class owns neither a replacement state graph nor a journal or recorded proof.
class WebGpuCanonicalNativeClosure final : public core::QuotaBoundSharedObjectIdentity {
    friend class WebGpuCanonicalCopyPassLowering;
    friend class WebGpuCanonicalCallbackCustody;
    friend class WebGpuNativeCommandPreparation;
    friend class WebGpuOriginalTypedNativePayload;
    friend class WebGpuOriginalTypedDependencyDecision;
    using Root = rhi::OriginalCommandStateTransaction;
    using Sources = rhi::OriginalCommandResourceSources;
    using Budget = rhi::OriginalCommandNativeClosureBudget;
    using Stream = rhi::OriginalCommandStreamProgram;
    static constexpr std::size_t nativeOwnerSlots =
        1u + rhi::OriginalWholeDescriptorWriteContract::quotaCount;
    static constexpr std::size_t entryOwnerSlots = nativeOwnerSlots + 1u;
    static constexpr std::size_t entryAllocationSlots = 2u;
    static constexpr std::size_t readbackEndpointOwnerSlots =
        std::tuple_size_v<decltype(std::declval<WebGpuTransferResourceObservation>().owners())>;
    static constexpr std::size_t readbackEntryOwnerSlots =
        1u + std::tuple_size_v<WebGpuCanonicalReadbackJoins::Native> * readbackEndpointOwnerSlots;
    static_assert(rhi::OriginalDepthTablePublicationContract::quotaCount
        == rhi::OriginalWholeDescriptorWriteContract::quotaCount);

    class ConstructionKey {
        friend class WebGpuCanonicalNativeClosure;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    struct Object {
        const rhi::GpuBuffer* buffer = nullptr;
        const rhi::GpuTexture* texture = nullptr;
        const rhi::GpuTextureView* view = nullptr;
        const rhi::GpuTexture* parent = nullptr;
        const rhi::ComputePipeline* compute = nullptr;
        const rhi::RenderPipeline* graphics = nullptr;
        const rhi::DescriptorSet* descriptor = nullptr;
        const rhi::DescriptorTable* table = nullptr;
        const rhi::GpuSampler* sampler = nullptr;

        bool resource() const noexcept { return buffer || texture || view; }

        bool bindingResource() const noexcept { return resource() || sampler; }

        rhi::GpuResourceId id() const noexcept
        {
            return buffer ? buffer->id()
                : texture ? texture->id()
                : view    ? view->id()
                : sampler ? sampler->id()
                          : rhi::GpuResourceId{};
        }

        rhi::OriginalResourceRegistrationTicket registration() const noexcept
        {
            return buffer    ? buffer->originalResourceRegistration()
                : texture    ? texture->originalResourceRegistration()
                : view       ? view->originalResourceRegistration()
                : sampler    ? sampler->originalResourceRegistration()
                : compute    ? compute->originalResourceRegistration()
                : graphics   ? graphics->originalResourceRegistration()
                : descriptor ? descriptor->originalResourceRegistration()
                             : table->originalResourceRegistration();
        }

        bool matches(const Object& other) const noexcept
        {
            return buffer == other.buffer && texture == other.texture && view == other.view
                && parent == other.parent && compute == other.compute && graphics == other.graphics
                && descriptor == other.descriptor && table == other.table
                && sampler == other.sampler;
        }
    };

public:
    struct ShaderTextureContentInput {
        std::size_t sourceIndex = SIZE_MAX;
        std::shared_ptr<const Root> uploadRoot;
        std::shared_ptr<const WebGpuCanonicalIssuerPreparation> issuer;
        std::shared_ptr<const WebGpuCanonicalTextureContentUploadSource> source;
        std::optional<rhi::GpuTexture::ContentExpectation> completed;
        rhi::ResourceState shaderState = rhi::ResourceState::Undefined;
        rhi::PipelineStage stages = rhi::PipelineStage::None;
        rhi::ResourceAccess access = rhi::ResourceAccess::None;
    };

    // Borrows authentic completed initializer/view facts; no descriptor,
    // transition, command or execution authority is issued by this validator.
    static const char* validateCompletedTextureInput(const ShaderTextureContentInput&,
        const std::shared_ptr<const rhi::OriginalCommandTextureViewSource>&,
        const Budget::ShaderTextureContentCustody&) noexcept;
    static const char* validateCompletedDepthTextureInput(
        const rhi::OriginalCompletedDepthReadInputContract::Input&,
        const std::shared_ptr<const rhi::OriginalCommandTextureViewSource>&,
        const rhi::OriginalCompletedDepthReadInputContract::Budget&) noexcept;

private:
    static std::uint64_t completedTextureOwnerBound(const ShaderTextureContentInput&) noexcept;

    struct Entry {
        std::size_t sourceIndex;
        Object object;
        rhi::OriginalResourceRegistrationTicket registration;
        std::uint64_t generation;
        std::variant<WebGpuTransferResourceObservation,
            WebGpuOrdinaryResourceObservation,
            std::shared_ptr<WebGpuNativeComputePipelinePayload>,
            std::shared_ptr<WebGpuNativeRenderPipelinePayload>,
            std::shared_ptr<WebGpuNativeDescriptorGroupPayload>>
            native;
        // Legacy field name. Closure6 reuses this existing slot for either
        // initialized index or vertex content; one actor per original buffer.
        std::shared_ptr<const WebGpuCanonicalBufferUploadSource> indexContent;
        std::optional<ShaderTextureContentInput> textureContent;

        const WebGpuTransferResourceObservation& backing() const noexcept
        {
            return object.view ? std::get<WebGpuOrdinaryResourceObservation>(native).backing()
                               : std::get<WebGpuTransferResourceObservation>(native);
        }

        std::shared_ptr<const void> owner(std::size_t i) const noexcept
        {
            if (i == nativeOwnerSlots)
                return indexContent;
            if (object.compute) {
                const auto& p =
                    std::get<std::shared_ptr<WebGpuNativeComputePipelinePayload>>(native);
                return i == 0u                            ? std::shared_ptr<const void>(p)
                    : i == 1u && p && p->issuedContract() ? p->issuedContract()->quota()
                                                          : nullptr;
            }
            if (object.graphics) {
                const auto& p =
                    std::get<std::shared_ptr<WebGpuNativeRenderPipelinePayload>>(native);
                return i == 0u                            ? std::shared_ptr<const void>(p)
                    : i == 1u && p && p->issuedContract() ? p->issuedContract()->quota()
                                                          : nullptr;
            }
            if (object.descriptor || object.table) {
                const auto& p =
                    std::get<std::shared_ptr<WebGpuNativeDescriptorGroupPayload>>(native);
                return i == 0u ? std::shared_ptr<const void>(p)
                    : p        ? p->originalScopedQuotaOwner(i - 1u)
                               : nullptr;
            }
            const auto values = object.view || object.sampler
                ? std::get<WebGpuOrdinaryResourceObservation>(native).owners()
                : backing().owners();
            return i < values.size() ? values[i] : nullptr;
        }

        std::shared_ptr<const void> payload() const noexcept
        {
            return object.compute || object.graphics || object.descriptor || object.table
                ? owner(0u)
                : object.sampler
                ? std::shared_ptr<const void>(
                      std::get<WebGpuOrdinaryResourceObservation>(native).sampler())
                : object.view ? std::shared_ptr<const void>(
                                    std::get<WebGpuOrdinaryResourceObservation>(native).view())
                : object.buffer ? std::shared_ptr<const void>(backing().buffer)
                                : backing().texture;
        }

        core::SharedAllocationProjection::Record allocationRecord() const noexcept
        {
            return object.resource() ? backing().allocationRecord()
                                     : core::SharedAllocationProjection::Record{};
        }

        core::SharedAllocationProjection::Record stagingAllocationRecord() const noexcept
        {
            if (!textureContent)
                return {};
            WebGpuTransferResourceObservation observed;
            const auto& buffer =
                textureContent->source->originalStaging()->initializedBuffer()->originalLease();
            return !observeOriginalWebGpuResource(*buffer->originalObject(), observed)
                ? observed.allocationRecord()
                : core::SharedAllocationProjection::Record{{}, {}, buffer->layout().sizeBytes};
        }

        void* device() const noexcept
        {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
            if (object.compute) {
                const auto& p =
                    std::get<std::shared_ptr<WebGpuNativeComputePipelinePayload>>(native);
                return p && p->issuedContract() ? p->issuedContract()->nativeDevice() : nullptr;
            }
            if (object.graphics) {
                const auto& p =
                    std::get<std::shared_ptr<WebGpuNativeRenderPipelinePayload>>(native);
                return p && p->issuedContract() ? p->issuedContract()->nativeDevice() : nullptr;
            }
            if (object.descriptor || object.table) {
                const auto& p =
                    std::get<std::shared_ptr<WebGpuNativeDescriptorGroupPayload>>(native);
                return p ? p->originalScopedNativeDevice() : nullptr;
            }
            return object.view || object.sampler
                ? std::get<WebGpuOrdinaryResourceObservation>(native).nativeDevice()
                : object.buffer ? backing().buffer->ownerDevice
                                : backing().texture->ownerDevice;
#else
            return nullptr;
#endif
        }
    };

public:
    struct IndexContentInput {
        std::size_t sourceIndex = SIZE_MAX;
        std::shared_ptr<const WebGpuCanonicalBufferUploadSource> source;
    };

    using GeometryContentInput = IndexContentInput;
    using ShaderBufferContentInput = IndexContentInput;

    WebGpuCanonicalNativeClosure(ConstructionKey,
        std::shared_ptr<Root> root,
        Budget budget,
        std::size_t count,
        std::optional<rhi::OriginalDeviceNativeLossObservation> loss,
        std::shared_ptr<const Stream> stream = {},
        std::shared_ptr<const WebGpuCanonicalReadbackJoins> readbacks = {},
        bool attachmentReadback = false)
        : root_(std::move(root)), budget_(budget), entries_(count), loss_(std::move(loss)),
          stream_(std::move(stream)), readbacks_(std::move(readbacks)),
          attachmentReadback_(attachmentReadback)
    {}

    // AttachmentReadbackResourceClosure/1 authenticates Join4 but leaves its
    // custody with Cold5. This resource-only closure never issues the copy.
    static std::shared_ptr<const WebGpuCanonicalNativeClosure>
    prepareOriginalAttachmentReadbackLossBound(std::shared_ptr<Root> root,
        const std::shared_ptr<const WebGpuCanonicalReadbackJoins>& readbacks,
        const rhi::GpuDevice& device,
        const Budget& budget,
        const rhi::OriginalDeviceNativeLossObservation& loss,
        const char*& diagnostic)
    {
        if ((diagnostic = Root::validateOriginal(root)))
            return {};
        if ((diagnostic = WebGpuCanonicalReadbackJoins::validateOriginal(readbacks)))
            return {};
        const auto& output = readbacks->originalGeneralOutputIntent();
        if ((diagnostic =
                    rhi::OriginalCommandNativeClosureContract::validateAttachmentReadbackProfile(
                        budget,
                        output.contractVersion,
                        output.prototypeOptIn,
                        root->originalGraph()->stateContractVersion(),
                        root->originalContentPlanContractVersion(),
                        readbacks->joinContractVersion())))
            return {};
        if (readbacks->originalProgram()
            || !core::SharedOwnerProjection::sameOwner(readbacks->originalRoot(), root)
            || !core::SharedOwnerProjection::sameOwner(
                readbacks->originalMetadataQuota(), root->originalMetadataQuota())) {
            diagnostic = "RHI-WEBGPU-CANONICAL-CLOSURE-ATTACHMENT-READBACK-IDENTITY";
            return {};
        }
        const auto decision = rhi::semantics::OriginalCommandNativeClosureLowering::
            attachmentReadbackLossBoundLowering(device.backend());
        if (!decision.resourceClosureAvailable) {
            diagnostic = decision.diagnostic;
            return {};
        }
        if ((diagnostic = device.validateOriginalDeviceNativeLoss(loss)))
            return {};
        return prepareOriginalImpl(std::move(root),
            device,
            budget,
            loss,
            diagnostic,
            {},
            {},
            nullptr,
            0u,
            nullptr,
            0u,
            true);
    }

    static std::shared_ptr<const WebGpuCanonicalNativeClosure> prepareOriginal(
        std::shared_ptr<Root> root,
        const rhi::GpuDevice& device,
        const Budget& budget,
        const char*& diagnostic)
    {
        return prepareOriginalImpl(std::move(root), device, budget, {}, diagnostic, {});
    }

    // CanonicalLossBoundNativeClosure/1: require the exact already-armed original
    // subscription before allocating closure metadata. A registry created without
    // native loss cannot be retrofitted. /1 still issues no native command/proof.
    static std::shared_ptr<const WebGpuCanonicalNativeClosure> prepareOriginalLossBound(
        std::shared_ptr<Root> root,
        const rhi::GpuDevice& device,
        const Budget& budget,
        const rhi::OriginalDeviceNativeLossObservation& loss,
        const char*& diagnostic)
    {
        if ((diagnostic = device.validateOriginalDeviceNativeLoss(loss)))
            return {};
        return prepareOriginalImpl(std::move(root),
            device,
            budget,
            std::optional<rhi::OriginalDeviceNativeLossObservation>{loss},
            diagnostic,
            {});
    }

    static std::shared_ptr<const WebGpuCanonicalNativeClosure> prepareOriginalTyped(
        std::shared_ptr<Root> root,
        std::shared_ptr<const Stream> stream,
        const rhi::GpuDevice& device,
        const Budget& budget,
        const char*& diagnostic)
    {
        if (budget.contractVersion != 2u) {
            diagnostic = "RHI-WEBGPU-CANONICAL-CLOSURE-TYPED-VERSION";
            return {};
        }
        return prepareOriginalImpl(
            std::move(root), device, budget, {}, diagnostic, std::move(stream));
    }

    static std::shared_ptr<const WebGpuCanonicalNativeClosure> prepareOriginalTypedLossBound(
        std::shared_ptr<Root> root,
        std::shared_ptr<const Stream> stream,
        const rhi::GpuDevice& device,
        const Budget& budget,
        const rhi::OriginalDeviceNativeLossObservation& loss,
        const char*& diagnostic)
    {
        if (budget.contractVersion != 2u) {
            diagnostic = "RHI-WEBGPU-CANONICAL-CLOSURE-TYPED-VERSION";
            return {};
        }
        if ((diagnostic = rhi::OriginalCommandNativeClosureContract::validateIntent(budget)))
            return {};
        if ((diagnostic = device.validateOriginalDeviceNativeLoss(loss)))
            return {};
        const auto decision =
            arp::rhi::semantics::OriginalCommandNativeClosureLowering::typedLossBoundLowering(
                device.backend());
        if (!decision.resourceClosureAvailable) {
            diagnostic = decision.diagnostic;
            return {};
        }
        if ((diagnostic = device.validateOriginalTypedNativeLossJoin(loss)))
            return {};
        return prepareOriginalImpl(std::move(root),
            device,
            budget,
            std::optional<rhi::OriginalDeviceNativeLossObservation>{loss},
            diagnostic,
            std::move(stream));
    }

    static std::shared_ptr<const WebGpuCanonicalNativeClosure> prepareOriginalTableLossBound(
        std::shared_ptr<Root> root,
        std::shared_ptr<const Stream> stream,
        const rhi::GpuDevice& device,
        const Budget& budget,
        const rhi::OriginalDeviceNativeLossObservation& loss,
        const char*& diagnostic)
    {
        if (budget.contractVersion != 3u) {
            diagnostic = "RHI-WEBGPU-CANONICAL-CLOSURE-TABLE-VERSION";
            return {};
        }
        if ((diagnostic = rhi::OriginalCommandNativeClosureContract::validateIntent(budget)))
            return {};
        const auto decision =
            rhi::semantics::OriginalCommandNativeClosureLowering::tableLossBoundLowering(
                device.backend());
        if (!decision.resourceClosureAvailable) {
            diagnostic = decision.diagnostic;
            return {};
        }
        if ((diagnostic = device.validateOriginalTypedNativeLossJoin(loss, {2u, true})))
            return {};
        return prepareOriginalImpl(
            std::move(root), device, budget, loss, diagnostic, std::move(stream));
    }

    static std::shared_ptr<const WebGpuCanonicalNativeClosure>
    prepareOriginalTableReadbackLossBound(std::shared_ptr<Root> root,
        std::shared_ptr<const Stream> stream,
        std::shared_ptr<const WebGpuCanonicalReadbackJoins> readbacks,
        const rhi::GpuDevice& device,
        const Budget& budget,
        const rhi::OriginalDeviceNativeLossObservation& loss,
        const char*& diagnostic)
    {
        if (budget.contractVersion != 4u) {
            diagnostic = "RHI-WEBGPU-CANONICAL-CLOSURE-TABLE-READBACK-VERSION";
            return {};
        }
        if ((diagnostic = rhi::OriginalCommandNativeClosureContract::validateIntent(budget)))
            return {};
        const auto decision =
            rhi::semantics::OriginalCommandNativeClosureLowering::tableReadbackLossBoundLowering(
                device.backend());
        if (!decision.resourceClosureAvailable) {
            diagnostic = decision.diagnostic;
            return {};
        }
        if ((diagnostic = device.validateOriginalTypedNativeLossJoin(loss, {2u, true})))
            return {};
        return prepareOriginalImpl(std::move(root),
            device,
            budget,
            loss,
            diagnostic,
            std::move(stream),
            std::move(readbacks));
    }

    static std::shared_ptr<const WebGpuCanonicalNativeClosure> prepareOriginalIndexContentLossBound(
        std::shared_ptr<Root> root,
        std::shared_ptr<const Stream> stream,
        const IndexContentInput* indices,
        std::size_t count,
        const rhi::GpuDevice& device,
        const Budget& budget,
        const rhi::OriginalDeviceNativeLossObservation& loss,
        const char*& diagnostic)
    {
        if (budget.contractVersion != 5u) {
            diagnostic = "RHI-WEBGPU-CANONICAL-CLOSURE-INDEX-VERSION";
            return {};
        }
        if ((diagnostic = rhi::OriginalCommandNativeClosureContract::validateIntent(budget)))
            return {};
        const auto decision =
            rhi::semantics::OriginalCommandNativeClosureLowering::indexContentLossBoundLowering(
                device.backend());
        if (!decision.resourceClosureAvailable) {
            diagnostic = decision.diagnostic;
            return {};
        }
        if ((diagnostic = device.validateOriginalTypedNativeLossJoin(loss)))
            return {};
        return prepareOriginalImpl(std::move(root),
            device,
            budget,
            loss,
            diagnostic,
            std::move(stream),
            {},
            indices,
            count);
    }

    static std::shared_ptr<const WebGpuCanonicalNativeClosure>
    prepareOriginalGeometryContentLossBound(std::shared_ptr<Root> root,
        std::shared_ptr<const Stream> stream,
        const GeometryContentInput* contents,
        std::size_t count,
        const rhi::GpuDevice& device,
        const Budget& budget,
        const rhi::OriginalDeviceNativeLossObservation& loss,
        const char*& diagnostic)
    {
        if (budget.contractVersion != 6u) {
            diagnostic = "RHI-WEBGPU-CANONICAL-CLOSURE-GEOMETRY-VERSION";
            return {};
        }
        if ((diagnostic = rhi::OriginalCommandNativeClosureContract::validateIntent(budget)))
            return {};
        const auto decision =
            rhi::semantics::OriginalCommandNativeClosureLowering::geometryContentLossBoundLowering(
                device.backend());
        if (!decision.resourceClosureAvailable) {
            diagnostic = decision.diagnostic;
            return {};
        }
        if ((diagnostic = device.validateOriginalTypedNativeLossJoin(loss)))
            return {};
        return prepareOriginalImpl(std::move(root),
            device,
            budget,
            loss,
            diagnostic,
            std::move(stream),
            {},
            contents,
            count);
    }

    static std::shared_ptr<const WebGpuCanonicalNativeClosure>
    prepareOriginalGeometryReadbackLossBound(std::shared_ptr<Root> root,
        std::shared_ptr<const Stream> stream,
        std::shared_ptr<const WebGpuCanonicalReadbackJoins> readbacks,
        const GeometryContentInput* contents,
        std::size_t count,
        const rhi::GpuDevice& device,
        const Budget& budget,
        const rhi::OriginalDeviceNativeLossObservation& loss,
        const char*& diagnostic)
    {
        if (budget.contractVersion != 7u) {
            diagnostic = "RHI-WEBGPU-CANONICAL-CLOSURE-GEOMETRY-READBACK-VERSION";
            return {};
        }
        if ((diagnostic = rhi::OriginalCommandNativeClosureContract::validateIntent(budget)))
            return {};
        const auto decision =
            rhi::semantics::OriginalCommandNativeClosureLowering::geometryReadbackLossBoundLowering(
                device.backend());
        if (!decision.resourceClosureAvailable) {
            diagnostic = decision.diagnostic;
            return {};
        }
        if ((diagnostic = Root::validateOriginal(root)))
            return {};
        const auto lossVersion = descriptorDomainVersion(budget.contractVersion, *root);
        if (!lossVersion) {
            diagnostic = "RHI-WEBGPU-CANONICAL-CLOSURE-GEOMETRY-READBACK-DESCRIPTOR-DOMAIN";
            return {};
        }
        if ((diagnostic = device.validateOriginalTypedNativeLossJoin(loss, {lossVersion, true})))
            return {};
        return prepareOriginalImpl(std::move(root),
            device,
            budget,
            loss,
            diagnostic,
            std::move(stream),
            std::move(readbacks),
            contents,
            count);
    }

    static std::shared_ptr<const WebGpuCanonicalNativeClosure>
    prepareOriginalGeneralResourcesLossBound(std::shared_ptr<Root> root,
        std::shared_ptr<const Stream> stream,
        const rhi::GpuDevice& device,
        const Budget& budget,
        const rhi::OriginalDeviceNativeLossObservation& loss,
        const char*& diagnostic,
        const ShaderBufferContentInput* contents = nullptr,
        std::size_t count = 0u,
        const ShaderTextureContentInput* textures = nullptr,
        std::size_t textureCount = 0u)
    {
        if (budget.contractVersion != 8u && budget.contractVersion != 9u) {
            diagnostic = "RHI-WEBGPU-CANONICAL-CLOSURE-GENERAL-VERSION";
            return {};
        }
        if ((diagnostic = rhi::OriginalCommandNativeClosureContract::validateIntent(budget)))
            return {};
        const auto decision =
            rhi::semantics::OriginalCommandNativeClosureLowering::generalResourcesLossBoundLowering(
                device.backend(), budget.contractVersion == 9u ? 2u : 1u);
        if (!decision.resourceClosureAvailable) {
            diagnostic = decision.diagnostic;
            return {};
        }
        if ((diagnostic = device.validateOriginalTypedNativeLossJoin(loss, {1u, true})))
            return {};
        return prepareOriginalImpl(std::move(root),
            device,
            budget,
            loss,
            diagnostic,
            std::move(stream),
            {},
            contents,
            count,
            textures,
            textureCount);
    }

    static std::shared_ptr<const WebGpuCanonicalNativeClosure>
    prepareOriginalFiniteGraphicsLossBound(std::shared_ptr<Root> root,
        std::shared_ptr<const Stream> stream,
        const rhi::GpuDevice& device,
        const Budget& budget,
        const rhi::OriginalDeviceNativeLossObservation& loss,
        const char*& diagnostic,
        const ShaderBufferContentInput* contents = nullptr,
        std::size_t count = 0u,
        const ShaderTextureContentInput* textures = nullptr,
        std::size_t textureCount = 0u)
    {
        if (budget.contractVersion != 10u) {
            diagnostic = "RHI-WEBGPU-CANONICAL-CLOSURE-GRAPHICS-VERSION";
            return {};
        }
        if ((diagnostic = rhi::OriginalCommandNativeClosureContract::validateIntent(budget)))
            return {};
        const auto decision =
            rhi::semantics::OriginalCommandNativeClosureLowering::finiteGraphicsLossBoundLowering(
                device.backend());
        if (!decision.resourceClosureAvailable) {
            diagnostic = decision.diagnostic;
            return {};
        }
        if ((diagnostic = device.validateOriginalTypedNativeLossJoin(loss, {1u, true})))
            return {};
        return prepareOriginalImpl(std::move(root),
            device,
            budget,
            loss,
            diagnostic,
            std::move(stream),
            {},
            contents,
            count,
            textures,
            textureCount);
    }

private:
    static std::shared_ptr<const WebGpuCanonicalNativeClosure> prepareOriginalImpl(
        std::shared_ptr<Root> root,
        const rhi::GpuDevice& device,
        const Budget& budget,
        std::optional<rhi::OriginalDeviceNativeLossObservation> loss,
        const char*& diagnostic,
        std::shared_ptr<const Stream> stream,
        std::shared_ptr<const WebGpuCanonicalReadbackJoins> readbacks = {},
        const IndexContentInput* indices = nullptr,
        std::size_t indexCount = 0u,
        const ShaderTextureContentInput* textures = nullptr,
        std::size_t textureCount = 0u,
        bool attachmentReadback = false)
    {
        diagnostic = rhi::OriginalCommandNativeClosureContract::validateIntent(budget);
        if (diagnostic)
            return {};
        // The resource-only entry cannot infer or synthesize a typed Stream2.
        // /2 needs a separate exact-stream join, not a relaxed /1 profile gate.
        const bool typed = budget.contractVersion >= 2u;
        const bool mixed = budget.contractVersion == 4u || budget.contractVersion == 7u;
        const bool geometry = budget.contractVersion >= 5u && budget.contractVersion <= 7u;
        const bool finiteGraphics = budget.contractVersion == 10u;
        const bool finiteGeneral = budget.contractVersion == 9u || finiteGraphics;
        const bool general = budget.contractVersion == 8u || finiteGeneral;
        if (!general && textureCount) {
            diagnostic = "RHI-WEBGPU-GENERAL-TEXTURE-SCHEMA";
            return {};
        }
        const auto stateVersion = finiteGeneral ? 5u
            : geometry                          ? 4u
            : mixed || general                  ? 3u
                                                : budget.contractVersion;
        if (typed && !stream) {
            diagnostic = "RHI-WEBGPU-CANONICAL-CLOSURE-TYPED-STREAM-REQUIRED";
            return {};
        }
        if (typed && device.backend() != rhi::Backend::WebGPU) {
            diagnostic = arp::rhi::semantics::OriginalCommandNativeClosureLowering::typedLowering(
                device.backend())
                             .diagnostic;
            return {};
        }
        if ((diagnostic = Root::validateOriginal(root)))
            return {};
        const bool tables = usesTableExpansion(budget.contractVersion, *root);
        if (root->originalGraph()->stateContractVersion() != stateVersion) {
            diagnostic = "RHI-WEBGPU-CANONICAL-CLOSURE-TYPED-PROFILE";
            return {};
        }
        if (root->status() != Root::Status::Prepared || !root->hasOriginalPrototypePlan()) {
            diagnostic = "RHI-WEBGPU-CANONICAL-CLOSURE-PLAN-STATUS";
            return {};
        }
        if (root->originalContentPlanContractVersion()
            == rhi::OriginalCompletedDepthRootReadPlanContract::version) {
            diagnostic =
                rhi::OriginalCompletedDepthRootReadPlanContract::lowering(rhi::Backend::WebGPU)
                    .diagnostic;
            return {};
        }
        if (root->originalContentPlanContractVersion() == 4u && !general) {
            diagnostic = "RHI-WEBGPU-CANONICAL-CLOSURE-COMPLETED-READ-PROFILE";
            return {};
        }
        if (attachmentReadback
            && (diagnostic =
                    rhi::OriginalCommandNativeClosureContract::validateAttachmentReadbackProfile(
                        budget,
                        3u,
                        true,
                        root->originalGraph()->stateContractVersion(),
                        root->originalContentPlanContractVersion(),
                        4u)))
            return {};
        if (!mixed && !attachmentReadback && root->originalContentPlanContractVersion() == 3u) {
            diagnostic = "RHI-WEBGPU-CANONICAL-CLOSURE-CONTENT-READBACK-PROFILE";
            return {};
        }
        if (tables && (!loss || (!geometry && !root->plannedContentCellCount()))) {
            diagnostic = "RHI-WEBGPU-CANONICAL-CLOSURE-TABLE-CONTENT-LOSS-REQUIRED";
            return {};
        }
        if (typed) {
            if ((diagnostic = Stream::validateOriginal(stream)))
                return {};
            if (stream->streamContractVersion() != stateVersion) {
                diagnostic = "RHI-WEBGPU-CANONICAL-CLOSURE-TYPED-STREAM-PROFILE";
                return {};
            }
            if (budget.contractVersion != 6u && budget.contractVersion != 7u
                && !stream->originalBudget().geometryUsage.drawConsumptionEmpty()) {
                diagnostic = "RHI-WEBGPU-CANONICAL-CLOSURE-GEOMETRY-CONSUMPTION-JOIN-MISSING";
                return {};
            }
            if ((diagnostic = stream->compareOriginalStates(root->originalGraph())))
                return {};
            if ((diagnostic = validateGeneralProfile(
                     *root, *stream, general, finiteGeneral, finiteGraphics)))
                return {};
            if (core::SharedOwnerProjection::overlapsOwner(
                    stream->originalMetadataQuota(), root->originalMetadataQuota())) {
                diagnostic = "RHI-WEBGPU-CANONICAL-CLOSURE-STREAM-QUOTA-DOMAIN";
                return {};
            }
        }
        if ((diagnostic = root->compareOriginalGraph(root->originalGraph(), device)))
            return {};
        if (mixed && (diagnostic = validateReadbackBinding(root, stream, readbacks, budget)))
            return {};
        const auto& source = sources(*root);
        if ((diagnostic = Sources::validateRegisteredLifetime(source, device)))
            return {};
        const auto& authored = source->originalBudget();
        if (budget.maxCpuMetadataBytes != root->originalMetadataQuota()->limitBytes()
            || budget.maxCpuMetadataBytes
                != authored.maxCpuBytes[static_cast<std::size_t>(
                    rhi::OriginalCommandAdmissionBudget::CpuDomain::Recording)]) {
            diagnostic = "RHI-WEBGPU-CANONICAL-CLOSURE-QUOTA";
            return {};
        }
        if (budget.contractVersion == 5u
            && (diagnostic = validateIndexBindings(*root,
                    *stream,
                    budget,
                    indexCount,
                    [&](std::size_t i) noexcept -> IndexContentInput {
                        return indices ? indices[i] : IndexContentInput{};
                    })))
            return {};
        if ((budget.contractVersion == 6u || budget.contractVersion == 7u)
            && (diagnostic = validateGeometryContents(*root,
                    *stream,
                    budget,
                    indexCount,
                    [&](std::size_t i) noexcept -> GeometryContentInput {
                        return indices ? indices[i] : GeometryContentInput{};
                    })))
            return {};
        if (general
            && (diagnostic = validateShaderBufferContents(
                    *root,
                    budget,
                    indexCount,
                    [&](std::size_t i) noexcept -> ShaderBufferContentInput {
                        return indices ? indices[i] : ShaderBufferContentInput{};
                    },
                    [finiteGeneral](const auto& source) noexcept {
                        return finiteGeneral ? source.initializedCommittedReadyFacts()
                                             : source.readyDiagnostic();
                    })))
            return {};
        if (general
            && (diagnostic = validateShaderTextureContents(*root,
                    budget,
                    textureCount,
                    [&](std::size_t i) noexcept -> ShaderTextureContentInput {
                        return textures ? textures[i] : ShaderTextureContentInput{};
                    })))
            return {};
        std::size_t resources = 0u, pipelines = 0u, descriptors = 0u, count = 0u;
        for (const auto& input : source->sources())
            if (const auto current = object(input, typed, tables, general)) {
                ++count;
                if (current->bindingResource())
                    ++resources;
                else if (current->descriptor || current->table)
                    ++descriptors;
                else
                    ++pipelines;
            } else if (typed && !coveredReadbackSource(input, readbacks)) {
                diagnostic = "RHI-WEBGPU-CANONICAL-CLOSURE-SOURCE-PROFILE";
                return {};
            }
        if (!resources || resources > budget.maxResourceBindings
            || count > SIZE_MAX / nativeOwnerSlots) {
            diagnostic = "RHI-WEBGPU-CANONICAL-CLOSURE-BINDING-CAPACITY";
            return {};
        }
        if (typed && (!pipelines || pipelines > budget.maxPipelineSources)) {
            diagnostic = "RHI-WEBGPU-CANONICAL-CLOSURE-PIPELINE-CAPACITY";
            return {};
        }
        if (typed && ((!descriptors && !geometry) || descriptors > budget.maxDescriptorSources)) {
            diagnostic = "RHI-WEBGPU-CANONICAL-CLOSURE-DESCRIPTOR-CAPACITY";
            return {};
        }
        if ((!typed
                && (count > budget.maxNativePayloadObjects
                    || count > authored.maxNativePayloadObjects))
            || (typed && !authored.maxNativePayloadObjects)) {
            diagnostic = "RHI-WEBGPU-CANONICAL-CLOSURE-PAYLOAD-CAPACITY";
            return {};
        }
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        diagnostic = "RHI-WEBGPU-CANONICAL-CLOSURE-NATIVE-UNAVAILABLE";
        return {};
#else
        if (webGpuNativeDeviceLost()) {
            diagnostic = "RHI-WEBGPU-CANONICAL-CLOSURE-NATIVE-LOST";
            return {};
        }
        // All resource/count/owner/byte admission is inspected before allocation.
        // Capturing a local observation only copies existing shared owners.
        for (std::size_t i = 0u; i < source->sources().size(); ++i)
            if (const auto original = object(source->sources()[i], typed, tables, general)) {
                Entry entry{i,
                    *original,
                    original->registration(),
                    original->registration().generation(),
                    WebGpuTransferResourceObservation{}};
                if ((diagnostic = capture(entry, tables)))
                    return {};
                if (typed && (diagnostic = validateTypedBinding(*root, entry, general)))
                    return {};
            }
        const char* mappedDiagnostic = nullptr;
        const auto mapped = [&](std::size_t index) noexcept {
            auto entry = inspectEntry(*root, index, mappedDiagnostic, typed, tables, general);
            entry.indexContent = indexForSource(entry.sourceIndex, indices, indexCount);
            if (const auto input = textureForSource(entry.sourceIndex, textures, textureCount))
                entry.textureContent.emplace(*input);
            return entry;
        };
        const auto payloads = inspectPayloads(count,
            std::min(budget.maxNativePayloadObjects, authored.maxNativePayloadObjects),
            mapped);
        if (mappedDiagnostic) {
            diagnostic = mappedDiagnostic;
            return {};
        }
        if (!payloads) {
            diagnostic = "RHI-WEBGPU-CANONICAL-CLOSURE-PAYLOAD-CAPACITY";
            return {};
        }
        const auto owners = inspectOwners(*root,
            count,
            budget.maxRetainedOwners,
            loss.has_value(),
            typed,
            mapped,
            readbacks.get());
        if (mappedDiagnostic) {
            diagnostic = mappedDiagnostic;
            return {};
        }
        if (!owners) {
            diagnostic = "RHI-WEBGPU-CANONICAL-CLOSURE-OWNER-CAPACITY";
            return {};
        }
        const auto allocations = inspectAllocations(count, budget.maxRetainedResourceBytes, mapped);
        if (mappedDiagnostic) {
            diagnostic = mappedDiagnostic;
            return {};
        }
        if (allocations.status != core::SharedAllocationProjection::Status::Complete) {
            diagnostic = "RHI-WEBGPU-CANONICAL-CLOSURE-ALLOCATION-CAPACITY";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(root->originalMetadataQuota());
            auto result = core::makeUnique<WebGpuCanonicalNativeClosure>(ConstructionKey{},
                root,
                budget,
                count,
                std::move(loss),
                std::move(stream),
                std::move(readbacks),
                attachmentReadback);
            result->owners_ = owners;
            result->bytes_ = allocations.bytes;
            result->resources_ = resources;
            result->pipelines_ = pipelines;
            result->descriptors_ = descriptors;
            result->payloads_ = payloads;
            for (std::size_t i = 0u; i < source->sources().size(); ++i)
                if (const auto original = object(source->sources()[i], typed, tables, general)) {
                    Entry entry{i,
                        *original,
                        original->registration(),
                        original->registration().generation(),
                        WebGpuTransferResourceObservation{}};
                    if ((diagnostic = capture(entry, tables)))
                        return {};
                    if (typed && (diagnostic = validateTypedBinding(*root, entry, general)))
                        return {};
                    entry.indexContent = indexForSource(i, indices, indexCount);
                    if (const auto input = textureForSource(i, textures, textureCount))
                        entry.textureContent.emplace(*input);
                    result->entries_.append(std::move(entry));
                }
            auto shared = shareOriginal(std::move(result));
            if ((diagnostic = shared->validate()))
                return {};
            return shared;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-WEBGPU-CANONICAL-CLOSURE-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-WEBGPU-CANONICAL-CLOSURE-ALLOCATION";
            return {};
        }
#endif
    }

public:
    static const char* validateOriginal(
        const std::shared_ptr<const WebGpuCanonicalNativeClosure>& self) noexcept
    {
        return !self || !self->hasQuotaBoundSharedOwner(self) ? "RHI-WEBGPU-CANONICAL-CLOSURE-OWNER"
                                                              : self->validate();
    }

    const char* validate() const noexcept
    {
        if (loss_)
            if (const auto* id = loss_->diagnostic())
                return id;
        if (const auto* id = Root::validateOriginal(root_))
            return id;
        const bool typed = budget_.contractVersion >= 2u;
        const auto stateVersion = budget_.contractVersion == 9u || budget_.contractVersion == 10u
            ? 5u
            : budget_.contractVersion >= 5u && budget_.contractVersion <= 7u ? 4u
            : budget_.contractVersion == 4u || budget_.contractVersion == 8u
            ? 3u
            : budget_.contractVersion;
        if (root_->originalGraph()->stateContractVersion() != stateVersion)
            return "RHI-WEBGPU-CANONICAL-CLOSURE-TYPED-PROFILE";
        if (typed) {
            if (root_->status() != Root::Status::Prepared)
                return "RHI-WEBGPU-CANONICAL-CLOSURE-PLAN-STATUS";
            if (const auto* id = Stream::validateOriginal(stream_))
                return id;
            if (stream_->streamContractVersion() != stateVersion)
                return "RHI-WEBGPU-CANONICAL-CLOSURE-TYPED-STREAM-PROFILE";
            if (const auto* id = stream_->compareOriginalStates(root_->originalGraph()))
                return id;
            if (const auto* id = validateGeneralProfile(*root_,
                    *stream_,
                    budget_.contractVersion == 8u || budget_.contractVersion == 9u
                        || budget_.contractVersion == 10u,
                    budget_.contractVersion == 9u || budget_.contractVersion == 10u,
                    budget_.contractVersion == 10u))
                return id;
        }
        return validateImmutableBindings();
    }

    const char* compareOriginalRoot(const std::shared_ptr<const Root>& root) const noexcept
    {
        if (const auto* id = validate())
            return id;
        return core::SharedOwnerProjection::sameOwner(root_, root)
            ? nullptr
            : "RHI-WEBGPU-CANONICAL-CLOSURE-ROOT";
    }

    const auto& originalRoot() const noexcept { return root_; }

    std::size_t resourceBindingCount() const noexcept { return resources_; }

    std::size_t pipelineBindingCount() const noexcept { return pipelines_; }

    std::size_t descriptorBindingCount() const noexcept { return descriptors_; }

    std::size_t nativePayloadCount() const noexcept { return payloads_; }

    const auto& originalStream() const noexcept { return stream_; }

    const auto& originalReadbackJoins() const noexcept { return readbacks_; }

    bool hasAttachmentReadbackProfile() const noexcept { return attachmentReadback_; }

    std::uint32_t closureContractVersion() const noexcept { return budget_.contractVersion; }

    std::uint32_t retainedOwnerCount() const noexcept { return owners_; }

    bool overlapsNativeOwner(const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
    {
        if (loss_ && loss_->overlapsMetadataOwner(quota))
            return true;
        for (const auto& entry : entries_.values())
            for (std::size_t i = 0u; i < entryOwnerSlots; ++i)
                if (core::SharedOwnerProjection::overlapsOwner(quota, entry.owner(i)))
                    return true;
        for (const auto& entry : entries_.values())
            if (entry.indexContent && entry.indexContent->overlapsCpuOwner(quota))
                return true;
        if (overlapsCompletedTextureInputOwner(quota))
            return true;
        return readbacks_ && readbacks_->overlapsCpuOwner(quota);
    }

    std::uint64_t retainedResourceBytes() const noexcept { return bytes_; }

    const rhi::OriginalDeviceNativeLossObservation* originalNativeLossObservation() const noexcept
    {
        return loss_ ? &*loss_ : nullptr;
    }

    const char* nativeLossDiagnostic() const noexcept
    {
        return loss_ ? loss_->diagnostic() : "RHI-WEBGPU-CANONICAL-CLOSURE-LOSS-UNBOUND";
    }

    void* nativeDevice() const noexcept
    {
        return entries_.values().empty() ? nullptr : entries_.values().front().device();
    }

    bool nativeExecutionProven() const noexcept { return false; }

    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = validate())
            return id;
        return (budget_.contractVersion == 10u
                ? rhi::semantics::OriginalCommandNativeClosureLowering::
                      finiteGraphicsLossBoundLowering(rhi::Backend::WebGPU)
                : budget_.contractVersion == 8u || budget_.contractVersion == 9u
                ? rhi::semantics::OriginalCommandNativeClosureLowering::
                      generalResourcesLossBoundLowering(
                          rhi::Backend::WebGPU, budget_.contractVersion == 9u ? 2u : 1u)
                : budget_.contractVersion == 7u
                ? rhi::semantics::OriginalCommandNativeClosureLowering::
                      geometryReadbackLossBoundLowering(rhi::Backend::WebGPU)
                : budget_.contractVersion == 6u
                ? rhi::semantics::OriginalCommandNativeClosureLowering::
                      geometryContentLossBoundLowering(rhi::Backend::WebGPU)
                : budget_.contractVersion == 5u
                ? rhi::semantics::OriginalCommandNativeClosureLowering::
                      indexContentLossBoundLowering(rhi::Backend::WebGPU)
                : budget_.contractVersion == 4u
                ? rhi::semantics::OriginalCommandNativeClosureLowering::
                      tableReadbackLossBoundLowering(rhi::Backend::WebGPU)
                : budget_.contractVersion == 3u
                ? rhi::semantics::OriginalCommandNativeClosureLowering::tableLossBoundLowering(
                      rhi::Backend::WebGPU)
                : budget_.contractVersion == 2u
                ? arp::rhi::semantics::OriginalCommandNativeClosureLowering::typedLowering(
                      rhi::Backend::WebGPU)
                : arp::rhi::semantics::OriginalCommandNativeClosureLowering::lowering(
                      rhi::Backend::WebGPU))
            .diagnostic;
    }

private:
    bool overlapsCompletedTextureInputOwner(
        const std::shared_ptr<core::CpuMemoryQuota>&) const noexcept;

    // Only an authenticated original retained owner may consume this scan.
    // Public validation retains its Root/Prepared/Stream guards above; private
    // Pending callers must independently prove the same original root/journal.
    const char* validateImmutableBindings() const noexcept
    {
        const auto ready = [this](const auto& source) noexcept {
            return budget_.contractVersion == 9u || budget_.contractVersion == 10u
                ? source.initializedCommittedReadyFacts()
                : source.readyDiagnostic();
        };
        return validateImmutableBindings(ready);
    }

    template <class Ready> const char* validateImmutableBindings(Ready&& ready) const noexcept
    {
        const auto noPendingRead = [](const auto&, const auto&) noexcept -> const char* {
            return nullptr;
        };
        return validateImmutableBindings(ready, noPendingRead);
    }

    template <class Ready, class TextureRead>
    const char* validateImmutableBindings(Ready&& ready, TextureRead&& textureRead) const noexcept
    {
        const bool typed = budget_.contractVersion >= 2u;
        const bool tables = usesTableExpansion(budget_.contractVersion, *root_);
        const bool general = budget_.contractVersion == 8u || budget_.contractVersion == 9u
            || budget_.contractVersion == 10u;
        if (budget_.contractVersion == 9u || budget_.contractVersion == 10u)
            if (const auto* id = validateGeneralProfile(
                    *root_, *stream_, true, true, budget_.contractVersion == 10u))
                return id;
        if (!root_->hasOriginalPrototypePlan()
            || root_->originalMetadataQuota()->limitBytes() != budget_.maxCpuMetadataBytes)
            return "RHI-WEBGPU-CANONICAL-CLOSURE-QUOTA";
        if (root_->originalContentPlanContractVersion() == 4u && !general)
            return "RHI-WEBGPU-CANONICAL-CLOSURE-COMPLETED-READ-PROFILE";
        if (attachmentReadback_)
            if (const auto* id =
                    rhi::OriginalCommandNativeClosureContract::validateAttachmentReadbackProfile(
                        budget_,
                        3u,
                        true,
                        root_->originalGraph()->stateContractVersion(),
                        root_->originalContentPlanContractVersion(),
                        4u))
                return id;
        if (!attachmentReadback_ && budget_.contractVersion != 4u && budget_.contractVersion != 7u
            && root_->originalContentPlanContractVersion() == 3u)
            return "RHI-WEBGPU-CANONICAL-CLOSURE-CONTENT-READBACK-PROFILE";
        if (budget_.contractVersion == 4u || budget_.contractVersion == 7u)
            if (const auto* id = validateReadbackBinding(root_, stream_, readbacks_, budget_))
                return id;
        if (budget_.contractVersion == 5u) {
            std::size_t count = 0u;
            for (const auto& entry : entries_.values())
                if (entry.indexContent)
                    ++count;
            if (const auto* id = validateIndexBindings(*root_,
                    *stream_,
                    budget_,
                    count,
                    [&](std::size_t ordinal) noexcept -> IndexContentInput {
                        for (const auto& entry : entries_.values())
                            if (entry.indexContent) {
                                if (!ordinal--)
                                    return {entry.sourceIndex, entry.indexContent};
                            }
                        return {};
                    }))
                return id;
        }
        if (budget_.contractVersion == 6u || budget_.contractVersion == 7u) {
            std::size_t count = 0u;
            for (const auto& entry : entries_.values())
                if (entry.indexContent)
                    ++count;
            if (const auto* id = validateGeometryContents(
                    *root_,
                    *stream_,
                    budget_,
                    count,
                    [&](std::size_t ordinal) noexcept -> GeometryContentInput {
                        for (const auto& entry : entries_.values())
                            if (entry.indexContent) {
                                if (!ordinal--)
                                    return {entry.sourceIndex, entry.indexContent};
                            }
                        return {};
                    },
                    ready))
                return id;
        }
        if (general) {
            std::size_t count = 0u;
            for (const auto& entry : entries_.values())
                if (entry.indexContent)
                    ++count;
            if (const auto* id = validateShaderBufferContents(
                    *root_,
                    budget_,
                    count,
                    [&](std::size_t ordinal) noexcept -> ShaderBufferContentInput {
                        for (const auto& entry : entries_.values())
                            if (entry.indexContent) {
                                if (!ordinal--)
                                    return {entry.sourceIndex, entry.indexContent};
                            }
                        return {};
                    },
                    ready))
                return id;
            count = 0u;
            for (const auto& entry : entries_.values())
                if (entry.textureContent)
                    ++count;
            if (const auto* id = validateShaderTextureContents(
                    *root_,
                    budget_,
                    count,
                    [&](std::size_t ordinal) noexcept -> ShaderTextureContentInput {
                        for (const auto& entry : entries_.values())
                            if (entry.textureContent) {
                                if (!ordinal--)
                                    return *entry.textureContent;
                            }
                        return {};
                    },
                    textureRead))
                return id;
        }
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        return "RHI-WEBGPU-CANONICAL-CLOSURE-NATIVE-UNAVAILABLE";
#else
        if (webGpuNativeDeviceLost())
            return "RHI-WEBGPU-CANONICAL-CLOSURE-NATIVE-LOST";
        const auto& input = sources(*root_)->sources();
        std::size_t index = 0u;
        void* device = nullptr;
        for (std::size_t i = 0u; i < input.size(); ++i)
            if (const auto current = object(input[i], typed, tables, general)) {
                if (index >= entries_.values().size())
                    return "RHI-WEBGPU-CANONICAL-CLOSURE-COVERAGE";
                const auto& entry = entries_.values()[index++];
                if (entry.sourceIndex != i || !entry.object.matches(*current) || !entry.generation
                    || entry.registration.generation() != entry.generation
                    || current->registration().generation() != entry.generation)
                    return "RHI-WEBGPU-CANONICAL-CLOSURE-GENERATION";
                if (const auto* id = validateEntry(entry, tables))
                    return id;
                if (typed)
                    if (const auto* id = validateTypedBinding(*root_, entry, general))
                        return id;
                if (!device)
                    device = entry.device();
                if (!device || device != entry.device())
                    return "RHI-WEBGPU-CANONICAL-CLOSURE-DEVICE";
            } else if (typed && !coveredReadbackSource(input[i], readbacks_))
                return "RHI-WEBGPU-CANONICAL-CLOSURE-SOURCE-PROFILE";
        if (index != entries_.values().size())
            return "RHI-WEBGPU-CANONICAL-CLOSURE-COVERAGE";
        const auto mapped = [&](std::size_t i) noexcept -> const Entry& {
            return entries_.values()[i];
        };
        if (inspectPayloads(index,
                std::min(budget_.maxNativePayloadObjects,
                    sources(*root_)->originalBudget().maxNativePayloadObjects),
                mapped)
            != payloads_)
            return "RHI-WEBGPU-CANONICAL-CLOSURE-PAYLOAD-CAPACITY";
        if (inspectOwners(*root_,
                index,
                budget_.maxRetainedOwners,
                loss_.has_value(),
                typed,
                mapped,
                readbacks_.get())
            != owners_)
            return "RHI-WEBGPU-CANONICAL-CLOSURE-OWNER-CAPACITY";
        const auto allocations =
            inspectAllocations(index, budget_.maxRetainedResourceBytes, mapped);
        return allocations.status == core::SharedAllocationProjection::Status::Complete
                && allocations.bytes == bytes_
            ? nullptr
            : "RHI-WEBGPU-CANONICAL-CLOSURE-ALLOCATION-CAPACITY";
#endif
    }

    static bool coveredReadbackSource(const Sources::Source& source,
        const std::shared_ptr<const WebGpuCanonicalReadbackJoins>& readbacks) noexcept
    {
        const auto* original =
            std::get_if<std::shared_ptr<const rhi::OriginalCommandTextureReadbackSource>>(&source);
        return original && readbacks && readbacks->entries_.findIf([&](const auto& entry) {
            return core::SharedOwnerProjection::sameOwner(entry.source, *original);
        });
    }

    static const char* validateReadbackBinding(const std::shared_ptr<Root>& root,
        const std::shared_ptr<const Stream>& stream,
        const std::shared_ptr<const WebGpuCanonicalReadbackJoins>& readbacks,
        const Budget& budget) noexcept
    {
        if (!readbacks || !readbacks->hasQuotaBoundSharedOwner(readbacks))
            return "RHI-WEBGPU-READBACK-JOIN-OWNER";
        if (const auto* id = root && root->status() == Root::Status::Pending
                ? readbacks->validateImmutableProgramBindings()
                : WebGpuCanonicalReadbackJoins::validateOriginal(readbacks))
            return id;
        if (readbacks->joinContractVersion() != (budget.contractVersion == 7u ? 3u : 2u)
            || root->originalContentPlanContractVersion() != 3u)
            return "RHI-WEBGPU-CANONICAL-CLOSURE-READBACK-PROFILE";
        if (!core::SharedOwnerProjection::sameOwner(readbacks->originalRoot(), root)
            || !core::SharedOwnerProjection::sameOwner(readbacks->originalProgram(), stream)
            || !core::SharedOwnerProjection::sameOwner(
                readbacks->originalMetadataQuota(), root->originalMetadataQuota()))
            return "RHI-WEBGPU-CANONICAL-CLOSURE-READBACK-IDENTITY";
        const auto count = readbacks->readbackCount();
        const auto copies = readbacks->projectedCopyCount();
        if (!count || count > budget.maxReadbackSources || !copies
            || copies > budget.maxReadbackCopies || count > SIZE_MAX / readbackEntryOwnerSlots
            || copies > SIZE_MAX - count * readbackEntryOwnerSlots)
            return "RHI-WEBGPU-CANONICAL-CLOSURE-READBACK-CAPACITY";
        const auto& inventory = *sources(*root);
        for (std::size_t i = 0u; i < count; ++i) {
            const auto& source = readbacks->entries_.values()[i].source;
            if (inventory.originalLeaseIndex(source) == SIZE_MAX)
                return "RHI-WEBGPU-CANONICAL-CLOSURE-READBACK-COVERAGE";
            const std::array indices{
                inventory.originalLeaseIndex(
                    source->originalSource()->originalLease()->originalTextureOwner()),
                inventory.originalLeaseIndex(source->originalDestinationLease())};
            for (std::size_t endpoint = 0u; endpoint < indices.size(); ++endpoint) {
                const auto index = indices[endpoint];
                if (index >= inventory.sources().size())
                    return "RHI-WEBGPU-CANONICAL-CLOSURE-READBACK-COVERAGE";
                const auto original = object(inventory.sources()[index], true, true);
                if (!original || (endpoint == 0u ? !original->texture : !original->buffer))
                    return "RHI-WEBGPU-CANONICAL-CLOSURE-READBACK-ENDPOINT";
                Entry entry{index,
                    *original,
                    original->registration(),
                    original->registration().generation(),
                    WebGpuTransferResourceObservation{}};
                if (const auto* id = capture(entry, true))
                    return id;
                if (!entry.backing().sameOwnersAndLayout(readbacks->native_.values()[i][endpoint]))
                    return "RHI-WEBGPU-CANONICAL-CLOSURE-READBACK-NATIVE-IDENTITY";
            }
        }
        return nullptr;
    }

    static std::shared_ptr<const Sources> sources(const Root& root) noexcept
    {
        return root.originalGraph()
            ->originalInputs()
            ->originalScalars()
            ->originalUsage()
            ->originalSources();
    }

    static std::optional<Object> object(const Sources::Source& source,
        bool typed = false,
        bool tables = false,
        bool general = false) noexcept
    {
        return std::visit(
            [&](const auto& lease) -> std::optional<Object> {
                using T = std::remove_const_t<typename std::decay_t<decltype(lease)>::element_type>;
                if constexpr (std::is_same_v<T, rhi::OriginalCommandBufferLease>)
                    return Object{lease->originalObject()};
                else if constexpr (std::is_same_v<T, rhi::OriginalCommandSamplerLease>) {
                    if (general) {
                        Object o;
                        o.sampler = lease->originalObject();
                        return o;
                    }
                    return {};
                } else if constexpr (std::is_same_v<T, rhi::OriginalCommandTextureLease>
                    || std::is_same_v<T, rhi::OriginalCommandReadbackTextureSource>)
                    return Object{nullptr, lease->originalObject()};
                else if constexpr (std::is_same_v<T, rhi::OriginalCommandTextureViewSource>)
                    return Object{nullptr,
                        nullptr,
                        lease->originalObject(),
                        lease->originalParentLease()->originalObject()};
                else if constexpr (std::is_same_v<T, rhi::OriginalCommandComputePipelineSource>) {
                    if (typed) {
                        Object o;
                        o.compute = lease->originalObject();
                        return o;
                    }
                    return {};
                } else if constexpr (std::is_same_v<T,
                                         rhi::OriginalCommandGraphicsPipelineSource>) {
                    if (typed) {
                        Object o;
                        o.graphics = lease->originalObject();
                        return o;
                    }
                    return {};
                } else if constexpr (std::is_same_v<T, rhi::OriginalCommandDescriptorSetSource>) {
                    if (typed) {
                        Object o;
                        o.descriptor = lease->originalObject();
                        return o;
                    }
                    return {};
                } else if constexpr (std::is_same_v<T, rhi::OriginalCommandDescriptorTableSource>) {
                    if (tables) {
                        Object o;
                        o.table = lease->originalObject();
                        return o;
                    }
                    return {};
                } else
                    return {}; // Canonical upload/readback adapters have no native object; dependencies are explicit Source entries.
            },
            source);
    }

    static const char* capture(Entry& entry, bool tables = false) noexcept
    {
        if (entry.object.compute)
            entry.native = webGpuNativePayloadFor(*entry.object.compute);
        else if (entry.object.graphics)
            entry.native = webGpuNativePayloadFor(*entry.object.graphics);
        else if (entry.object.descriptor)
            entry.native = webGpuNativePayloadFor(*entry.object.descriptor);
        else if (entry.object.table)
            entry.native = webGpuNativePayloadFor(*entry.object.table);
        else if (entry.object.sampler) {
            entry.native.emplace<WebGpuOrdinaryResourceObservation>();
            if (const auto* id = observeOriginalWebGpuResource(*entry.object.sampler,
                    std::get<WebGpuOrdinaryResourceObservation>(entry.native)))
                return id;
        } else if (entry.object.view) {
            entry.native.emplace<WebGpuOrdinaryResourceObservation>();
            if (const auto* id = observeOriginalWebGpuResource(
                    *entry.object.view, std::get<WebGpuOrdinaryResourceObservation>(entry.native)))
                return id;
        } else if (entry.object.buffer) {
            if (const auto* id = observeOriginalWebGpuResource(*entry.object.buffer,
                    std::get<WebGpuTransferResourceObservation>(entry.native)))
                return id;
        } else if (const auto* id = observeOriginalWebGpuResource(*entry.object.texture,
                       std::get<WebGpuTransferResourceObservation>(entry.native)))
            return id;
        return validateEntry(entry, tables);
    }

    static const char* validateEntry(const Entry& entry, bool tables = false) noexcept
    {
        if (const auto* id = entry.registration.diagnostic())
            return id;
        if (entry.object.compute) {
            const auto& p =
                std::get<std::shared_ptr<WebGpuNativeComputePipelinePayload>>(entry.native);
            if (!core::SharedOwnerProjection::sameOwner(
                    p, webGpuNativePayloadFor(*entry.object.compute)))
                return "RHI-WEBGPU-CANONICAL-CLOSURE-ORIGINAL-PIPELINE";
            return tables ? WebGpuOriginalArtifactComputeFactory::validateConsumedTableExpansion(p)
                          : WebGpuOriginalArtifactComputeFactory::validateConsumedOriginal(p);
        }
        if (entry.object.graphics) {
            const auto& p =
                std::get<std::shared_ptr<WebGpuNativeRenderPipelinePayload>>(entry.native);
            if (!core::SharedOwnerProjection::sameOwner(
                    p, webGpuNativePayloadFor(*entry.object.graphics)))
                return "RHI-WEBGPU-CANONICAL-CLOSURE-ORIGINAL-PIPELINE";
            return tables ? WebGpuOriginalArtifactGraphicsFactory::validateConsumedTableExpansion(p)
                          : WebGpuOriginalArtifactGraphicsFactory::validateConsumedOriginal(p);
        }
        if (entry.object.descriptor) {
            const auto& p =
                std::get<std::shared_ptr<WebGpuNativeDescriptorGroupPayload>>(entry.native);
            return p
                    && core::SharedOwnerProjection::sameOwner(
                        p, webGpuNativePayloadFor(*entry.object.descriptor))
                ? nullptr
                : "RHI-WEBGPU-CANONICAL-CLOSURE-ORIGINAL-DESCRIPTOR";
        }
        if (entry.object.table) {
            const auto& p =
                std::get<std::shared_ptr<WebGpuNativeDescriptorGroupPayload>>(entry.native);
            if (!p || !p->containsOriginalDepthTableWrite()
                || !core::SharedOwnerProjection::sameOwner(
                    p, webGpuNativePayloadFor(*entry.object.table)))
                return "RHI-WEBGPU-CANONICAL-CLOSURE-ORIGINAL-TABLE";
            if (const auto* id = entry.object.table->originalDepthTableContentDiagnostic())
                return id;
            return p->depthContentDiagnostic();
        }
        if (entry.object.sampler) {
            const auto& sampler = std::get<WebGpuOrdinaryResourceObservation>(entry.native);
            if (!core::SharedOwnerProjection::sameOwner(
                    sampler.sampler(), webGpuNativePayloadFor(*entry.object.sampler)))
                return "RHI-WEBGPU-CANONICAL-CLOSURE-ORIGINAL-SAMPLER";
            return validateOriginalWebGpuResource(*entry.object.sampler, sampler);
        }
        if (entry.object.buffer) {
            const auto& n = entry.backing();
            if (!core::SharedOwnerProjection::sameOwner(
                    n.buffer, webGpuNativePayloadFor(*entry.object.buffer))
                || !rhi::BufferResourceLayout::capture(n.bufferDesc)
                    .matches(entry.object.buffer->desc())
                || n.allocationBytes != entry.object.buffer->allocation().estimatedSizeBytes)
                return "RHI-WEBGPU-CANONICAL-CLOSURE-ORIGINAL-BUFFER";
            return validateOriginalWebGpuResource(*entry.object.buffer, n);
        }
        const auto* texture = entry.object.view ? entry.object.parent : entry.object.texture;
        const auto& n = entry.backing();
        if (!core::SharedOwnerProjection::sameOwner(n.texture, webGpuNativePayloadFor(*texture))
            || !rhi::TextureResourceLayout::capture(n.textureDesc).matches(texture->desc())
            || n.allocationBytes != texture->allocation().estimatedSizeBytes)
            return "RHI-WEBGPU-CANONICAL-CLOSURE-ORIGINAL-TEXTURE";
        if (entry.object.view) {
            const auto& view = std::get<WebGpuOrdinaryResourceObservation>(entry.native);
            if (!entry.registration.matchesParent(texture->originalResourceRegistration())
                || !core::SharedOwnerProjection::sameOwner(
                    view.view(), webGpuNativePayloadFor(*entry.object.view))
                || !rhi::TextureViewResourceLayout::capture(view.viewDesc())
                    .matches(entry.object.view->desc()))
                return "RHI-WEBGPU-CANONICAL-CLOSURE-ORIGINAL-VIEW";
            return validateOriginalWebGpuResource(*entry.object.view, view);
        }
        return validateOriginalWebGpuResource(*texture, n);
    }

    static const char* validateTableBinding(const Root& root, const Entry& entry) noexcept
    {
        const auto& input = *root.originalGraph()->originalInputs();
        const auto originalSources = sources(root);
        const auto& source = originalSources->sources();
        const auto* frozen =
            std::get_if<std::shared_ptr<const rhi::OriginalCommandDescriptorTableSource>>(
                &source[entry.sourceIndex]);
        if (!frozen || (*frozen)->sourceContractVersion() != 2u
            || !(*frozen)->matchesCanonicalSources(*originalSources))
            return "RHI-WEBGPU-CANONICAL-CLOSURE-TABLE-SOURCE";
        if (const auto* id = rhi::OriginalCommandDescriptorTableSource::validateOriginal(*frozen))
            return id;
        const auto& payload =
            std::get<std::shared_ptr<WebGpuNativeDescriptorGroupPayload>>(entry.native);
        const auto& writes = payload->depthCandidateWrites();
        const auto& elements = (*frozen)->frozenElements();
        if (writes.size() != elements.size())
            return "RHI-WEBGPU-CANONICAL-CLOSURE-TABLE-COVERAGE";
        for (std::size_t i = 0u; i < elements.size(); ++i) {
            const auto* view =
                std::get_if<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                    &elements[i].resourceOwner);
            if (!view || !core::SharedOwnerProjection::sameOwner(*view, writes[i].view)
                || !rhi::descriptorTableWriteMatches(elements[i].declaration.write, writes[i].write)
                || elements[i].observation.revision() != (*frozen)->frozenContentRevision())
                return "RHI-WEBGPU-CANONICAL-CLOSURE-TABLE-BINDING";
        }
        bool bound = false;
        for (const auto& record : input.descriptors())
            if (const auto* binding =
                    std::get_if<rhi::OriginalCommandDescriptorBinding>(&record.arguments)) {
                if (binding->sourceIndex != entry.sourceIndex)
                    continue;
                if (!rhi::OriginalCommandDescriptorBindingContract::isTable(binding->kind()))
                    return "RHI-WEBGPU-CANONICAL-CLOSURE-TABLE-KIND";
                const auto scope =
                    binding->kind() == rhi::CommandUsageKind::BindRenderDescriptorTable
                    ? rhi::semantics::CommandRecordingOrder::Scope::Render
                    : rhi::semantics::CommandRecordingOrder::Scope::Compute;
                if (const auto* id =
                        rhi::OriginalCommandDescriptorBindingContract::validate(*binding,
                            *input.originalScalars(),
                            record.commandIndex,
                            binding->pipelineSourceIndex,
                            binding->scopeBeginCommandIndex,
                            scope,
                            4u))
                    return id;
                bound = true;
            }
        return bound ? nullptr : "RHI-WEBGPU-CANONICAL-CLOSURE-TABLE-UNBOUND";
    }

    static const char* validateGeneralProfile(const Root& root,
        const Stream& stream,
        bool general,
        bool finite = false,
        bool graphics = false) noexcept
    {
        const auto& b = stream.originalBudget();
        const auto& shader = b.descriptorConsumption;
        const auto& render = b.renderDescriptorUsage;
        if (!general)
            return shader.generalShaderResourcesVersion || shader.generalShaderResourcesOptIn
                    || render.generalShaderResourcesVersion || render.generalShaderResourcesOptIn
                ? "RHI-WEBGPU-CANONICAL-CLOSURE-GENERAL-PROFILE"
                : nullptr;
        if (graphics) {
            // The authenticated mixed root already proves Source5/Inputs6,
            // Usage6/Dependency6 and the independent completed/attachment controls.
            // Scan actual pipeline objects as well; a Compute root cannot borrow /10.
            bool graphicsPipeline = false;
            for (const auto& source : sources(root)->sources()) {
                if (std::holds_alternative<
                        std::shared_ptr<const rhi::OriginalCommandComputePipelineSource>>(source)
                    || std::holds_alternative<
                        std::shared_ptr<const rhi::OriginalCommandDescriptorTableSource>>(source))
                    return "RHI-WEBGPU-CANONICAL-CLOSURE-GRAPHICS-PROFILE";
                graphicsPipeline = graphicsPipeline
                    || std::holds_alternative<
                        std::shared_ptr<const rhi::OriginalCommandGraphicsPipelineSource>>(source);
                if (const auto* descriptor =
                        std::get_if<std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>>(
                            &source))
                    for (const auto& frozen : (*descriptor)->frozenElements())
                        if (frozen.declaration.stages != rhi::PipelineStage::FragmentShader)
                            return "RHI-WEBGPU-CANONICAL-CLOSURE-GRAPHICS-STAGE";
            }
            return finite && graphicsPipeline
                    && root.hasOriginalFiniteGeneralShaderResourceProfile()
                    && root.originalContentPlanContractVersion() == 6u
                    && root.originalMixedContentCounts()
                    && root.originalMixedContentCounts()->completedControls
                    && root.originalMixedContentCounts()->attachmentControls
                    && root.originalMixedContentCounts()->renderBindingReads
                    && root.originalMixedContentCounts()->drawReads
                    && stream.streamContractVersion() == 5u && b.stateSequence.contractVersion == 5u
                    && b.geometryUsage.drawConsumptionEmpty()
                    && descriptorDomainVersion(10u, root) == 1u && shader.contractVersion == 4u
                    && render.contractVersion == 4u && shader.generalShaderResourcesVersion == 2u
                    && shader.generalShaderResourcesOptIn
                    && render.generalShaderResourcesVersion == 2u
                    && render.generalShaderResourcesOptIn
                ? nullptr
                : "RHI-WEBGPU-CANONICAL-CLOSURE-GRAPHICS-PROFILE";
        }
        if (finite)
            return root.hasOriginalFiniteGeneralShaderResourceProfile()
                    && root.originalContentPlanContractVersion() == 5u
                    && root.originalGraph()->stateContractVersion() == 5u
                    && stream.streamContractVersion() == 5u
                    && root.originalGraph()->originalInputs()->originalBudget().contractVersion
                        == 6u
                    && shader.contractVersion == 4u && render.contractVersion == 4u
                    && shader.generalShaderResourcesVersion == 2u
                    && shader.generalShaderResourcesOptIn
                    && render.generalShaderResourcesVersion == 2u
                    && render.generalShaderResourcesOptIn && descriptorDomainVersion(9u, root) == 1u
                ? nullptr
                : "RHI-WEBGPU-CANONICAL-CLOSURE-GENERAL-FINITE-PROFILE";
        if (root.originalGraph()->originalInputs()->originalBudget().contractVersion != 3u
            || shader.generalShaderResourcesVersion != 1u || !shader.generalShaderResourcesOptIn
            || render.generalShaderResourcesVersion != 1u || !render.generalShaderResourcesOptIn
            || descriptorDomainVersion(8u, root) != 1u)
            return "RHI-WEBGPU-CANONICAL-CLOSURE-GENERAL-PROFILE";
        return nullptr;
    }

    static const char* validateTypedBinding(
        const Root& root, const Entry& entry, bool general = false) noexcept
    {
        if (entry.object.table)
            return validateTableBinding(root, entry);
        if (!entry.object.descriptor)
            return nullptr;
        const auto& input = *root.originalGraph()->originalInputs();
        const auto& source = sources(root)->sources();
        const auto* frozen =
            std::get_if<std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>>(
                &source[entry.sourceIndex]);
        if (!frozen)
            return "RHI-WEBGPU-CANONICAL-CLOSURE-DESCRIPTOR-SOURCE";
        if (general) {
            if (const auto* id = rhi::OriginalCommandDescriptorSetSource::validateOriginal(*frozen))
                return id;
            if (!(*frozen)->matchesCanonicalSources(*sources(root)))
                return "RHI-WEBGPU-CANONICAL-CLOSURE-GENERAL-OWNER";
        }
        const auto& payload =
            std::get<std::shared_ptr<WebGpuNativeDescriptorGroupPayload>>(entry.native);
        using Request = rhi::OriginalDescriptorNativeIdentityRequest;
        const auto version = (*frozen)->sourceContractVersion();
        const bool finiteGeneral = general && root.hasOriginalFiniteGeneralShaderResourceProfile();
        if (version != 2u && version != 3u && version != 4u && !(finiteGeneral && version == 5u))
            return "RHI-WEBGPU-CANONICAL-CLOSURE-DESCRIPTOR-PROFILE";
        if (general && version != (finiteGeneral ? 5u : 2u))
            return "RHI-WEBGPU-CANONICAL-CLOSURE-GENERAL-SOURCE-PROFILE";
        if (const auto* id = (*frozen)->validateFrozenFiniteFootprints())
            return id;
        const Request identity = finiteGeneral ? Request{4u, true, Request::Profile::GeneralWrite2}
            : general                          ? Request{3u, true, Request::Profile::GeneralWrite1}
            : version == 4u
            ? Request{2u, true, Request::Profile::FiniteTargetMemory1}
            : Request{1u,
                  true,
                  version == 3u ? Request::Profile::FiniteWrite1 : Request::Profile::WholeWrite1};
        if (const auto* id = payload->diagnoseOriginalNativeIdentity(identity))
            return id;
        const auto& elements = (*frozen)->frozenElements();
        for (std::size_t i = 0u; i < elements.size(); ++i) {
            const auto& e = elements[i];
            if (general) {
                if (const auto* id = payload->diagnoseOriginalGeneralClosureBinding(
                        i, elements.size(), e.declaration, *sources(root)))
                    return id;
                continue;
            }
            const auto* buffer =
                std::get_if<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                    &e.resourceOwner);
            if (!buffer)
                return "RHI-WEBGPU-CANONICAL-CLOSURE-DESCRIPTOR-BUFFER-PROFILE";
            if (const auto* id = payload->diagnoseOriginalClosureBinding(
                    i, elements.size(), e.declaration.write, *buffer, e.declaration.dynamicOffset))
                return id;
        }
        bool bound = false;
        for (const auto& record : input.descriptors())
            if (const auto* binding =
                    std::get_if<rhi::OriginalCommandDescriptorBinding>(&record.arguments)) {
                if (binding->sourceIndex != entry.sourceIndex)
                    continue;
                if (binding->pipelineSourceIndex >= source.size())
                    return "RHI-WEBGPU-CANONICAL-CLOSURE-DESCRIPTOR-PIPELINE";
                const auto* slot =
                    input.originalScalars()
                        ->originalArguments()
                        ->records()[record.commandIndex]
                        .scalar.template get<rhi::CommandScalarArguments::DescriptorBinding>();
                if (!slot)
                    return "RHI-WEBGPU-CANONICAL-CLOSURE-DESCRIPTOR-SLOT";
                if (rhi::OriginalCommandDescriptorSetSource::finiteVersion(version)
                    || rhi::OriginalCommandDescriptorSetSource::generalFiniteVersion(version)) {
                    if ((*frozen)->frozenPipelineSourceIndex() != binding->pipelineSourceIndex)
                        return "RHI-WEBGPU-CANONICAL-CLOSURE-DESCRIPTOR-PIPELINE";
                    if ((*frozen)->frozenLogicalSet() != slot->set)
                        return "RHI-WEBGPU-CANONICAL-CLOSURE-DESCRIPTOR-SLOT";
                }
                const auto* id = std::visit(
                    [&](const auto& pipeline) -> const char* {
                        using T = std::remove_const_t<
                            typename std::decay_t<decltype(pipeline)>::element_type>;
                        if constexpr (std::is_same_v<T, rhi::OriginalCommandComputePipelineSource>
                            || std::is_same_v<T, rhi::OriginalCommandGraphicsPipelineSource>)
                            return payload->diagnoseOriginalClosurePipeline(
                                pipeline->originalPipeline(), slot->set);
                        else
                            return "RHI-WEBGPU-CANONICAL-CLOSURE-DESCRIPTOR-PIPELINE";
                    },
                    source[binding->pipelineSourceIndex]);
                if (id)
                    return id;
                bound = true;
            }
        return bound ? nullptr : "RHI-WEBGPU-CANONICAL-CLOSURE-DESCRIPTOR-UNBOUND";
    }

    static std::shared_ptr<const WebGpuCanonicalBufferUploadSource> indexForSource(
        std::size_t source, const IndexContentInput* indices, std::size_t count) noexcept
    {
        if (indices)
            for (std::size_t i = 0u; i < count; ++i)
                if (indices[i].sourceIndex == source)
                    return indices[i].source;
        return {};
    }

    static std::optional<ShaderTextureContentInput> textureForSource(
        std::size_t source, const ShaderTextureContentInput* inputs, std::size_t count) noexcept
    {
        if (inputs)
            for (std::size_t i = 0u; i < count; ++i)
                if (inputs[i].sourceIndex == source)
                    return inputs[i];
        return {};
    }

    template <class At>
    static const char* validateShaderTextureContents(
        const Root& root, const Budget& budget, std::size_t count, At&& at) noexcept
    {
        const auto noPendingRead = [](const auto&, const auto&) noexcept -> const char* {
            return nullptr;
        };
        return validateShaderTextureContents(root, budget, count, at, noPendingRead);
    }

    template <class At, class TextureRead>
    static const char* validateShaderTextureContents(const Root& root,
        const Budget& budget,
        std::size_t count,
        At&& at,
        TextureRead&& textureRead) noexcept
    {
        const auto& intent = budget.shaderTextureContent;
        if (intent.empty())
            return count ? "RHI-WEBGPU-GENERAL-TEXTURE-NOT-REQUESTED" : nullptr;
        if (const auto* id = intent.diagnostic())
            return id;
        if (!count || count > intent.maxSources)
            return "RHI-WEBGPU-GENERAL-TEXTURE-SOURCE-CAPACITY";
        if (root.originalContentPlanContractVersion()
            != (budget.contractVersion == 10u      ? 6u
                    : budget.contractVersion == 9u ? 5u
                                                   : 4u))
            return "RHI-WEBGPU-GENERAL-TEXTURE-CONTENT-READ-PLAN";
        const auto& inputs = sources(root)->sources();
        std::uint64_t bytes = 0u, owners = 0u;
        for (std::size_t i = 0u; i < count; ++i) {
            const auto content = at(i);
            if (content.sourceIndex >= inputs.size())
                return "RHI-WEBGPU-GENERAL-TEXTURE-SOURCE";
            for (std::size_t j = 0u; j < i; ++j)
                if (at(j).sourceIndex == content.sourceIndex
                    || core::SharedOwnerProjection::sameOwner(at(j).source, content.source))
                    return "RHI-WEBGPU-GENERAL-TEXTURE-DUPLICATE";
            const auto* view =
                std::get_if<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                    &inputs[content.sourceIndex]);
            if (!view)
                return "RHI-WEBGPU-GENERAL-TEXTURE-VIEW-SOURCE";
            if (const auto* id = validateCompletedTextureInput(content, *view, intent))
                return id;
            if (const auto* id = textureRead(content, *view))
                return id;
            const auto retained = completedTextureOwnerBound(content);
            if (!retained || retained > intent.maxRetainedOwners - owners)
                return "RHI-WEBGPU-GENERAL-TEXTURE-OWNER-CAPACITY";
            owners += retained;
            std::uint64_t texels = 0u;
            if (const auto* id = rhi::semantics::TextureUploadDestinationValidation::footprint(
                    (*view)->originalParentLease()->originalObject()->desc(), texels))
                return id;
            if (texels > intent.maxInitializedBytes - bytes)
                return "RHI-WEBGPU-GENERAL-TEXTURE-BYTE-CAPACITY";
            bytes += texels;
            bool used = false;
            for (const auto& input : inputs)
                if (const auto* descriptor =
                        std::get_if<std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>>(
                            &input))
                    for (const auto& frozen : (*descriptor)->frozenElements()) {
                        const auto& e = frozen.declaration;
                        if (e.sourceIndex != content.sourceIndex)
                            continue;
                        if (e.write.type != rhi::DescriptorType::SampledTexture)
                            return "RHI-WEBGPU-GENERAL-TEXTURE-DESCRIPTOR-TYPE";
                        if (e.write.resourceState != content.shaderState
                            || e.stages != content.stages || e.access != content.access)
                            return "RHI-WEBGPU-GENERAL-TEXTURE-FROZEN-ACCESS";
                        if (const auto* id = rhi::OriginalCompletedShaderTextureInputContract::
                                inspectViewBinding(
                                    (*view)->originalParentLease()->originalObject()->desc(),
                                    (*view)->originalObject()->desc(),
                                    e.write.resourceState,
                                    e.stages,
                                    e.access))
                            return id;
                        used = true;
                    }
            if (!used)
                return "RHI-WEBGPU-GENERAL-TEXTURE-UNBOUND";
        }
        for (const auto& input : inputs)
            if (const auto* descriptor =
                    std::get_if<std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>>(
                        &input))
                for (const auto& frozen : (*descriptor)->frozenElements()) {
                    const auto& e = frozen.declaration;
                    if (e.write.type != rhi::DescriptorType::SampledTexture)
                        continue;
                    bool found = false;
                    for (std::size_t i = 0u; i < count; ++i)
                        found = found || at(i).sourceIndex == e.sourceIndex;
                    if (!found)
                        return "RHI-WEBGPU-GENERAL-TEXTURE-COVERAGE";
                }
        return nullptr;
    }

    template <class At, class Ready>
    static const char* validateShaderBufferContents(
        const Root& root, const Budget& budget, std::size_t count, At&& at, Ready&& ready) noexcept
    {
        const auto& intent = budget.shaderBufferContent;
        const bool finite = budget.contractVersion == 9u || budget.contractVersion == 10u;
        if (finite && intent.contractVersion != 2u)
            return "RHI-ORIGINAL-SHADER-CONTENT-CUSTODY-VERSION";
        if (intent.empty())
            return count ? "RHI-WEBGPU-GENERAL-CONTENT-NOT-REQUESTED" : nullptr;
        if (const auto* id = intent.diagnostic())
            return id;
        const auto lowering =
            rhi::semantics::OriginalCommandInitializedBufferStagingLowering::lowering(
                rhi::Backend::WebGPU, 4u);
        if (!lowering.privateInitializationAvailable)
            return lowering.diagnostic;
        if (!count || count > intent.maxSources
            || count > intent.maxRetainedOwners
                    / WebGpuCanonicalBufferUploadSource::initializedIndexOwnerBound)
            return "RHI-WEBGPU-GENERAL-CONTENT-SOURCE-CAPACITY";
        const auto& inputs = sources(root)->sources();
        std::uint64_t totalBytes = 0u;
        for (std::size_t i = 0u; i < count; ++i) {
            const auto content = at(i);
            if (!content.source || content.sourceIndex >= inputs.size())
                return "RHI-WEBGPU-GENERAL-CONTENT-SOURCE";
            for (std::size_t j = 0u; j < i; ++j)
                if (at(j).sourceIndex == content.sourceIndex
                    || core::SharedOwnerProjection::sameOwner(at(j).source, content.source))
                    return "RHI-WEBGPU-GENERAL-CONTENT-DUPLICATE";
            const auto* lease = std::get_if<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                &inputs[content.sourceIndex]);
            if (!lease)
                return "RHI-WEBGPU-GENERAL-CONTENT-BUFFER-SOURCE";
            bool used = false;
            for (const auto& input : inputs)
                if (const auto* descriptor =
                        std::get_if<std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>>(
                            &input))
                    for (std::size_t ordinal = 0u; ordinal < (*descriptor)->frozenElements().size();
                        ++ordinal) {
                        const auto& frozen = (*descriptor)->frozenElements()[ordinal];
                        const auto& e = frozen.declaration;
                        if (e.sourceIndex != content.sourceIndex)
                            continue;
                        if (finite) {
                            rhi::DescriptorBufferBindingRangeContract::Resolved resolved;
                            const rhi::OriginalGeneralFiniteInitializedBufferContentContract::
                                Request request{1u,
                                    true,
                                    intent.maxRetainedOwners,
                                    intent.maxInitializedBytes,
                                    budget.maxRetainedResourceBytes};
                            if (const auto* id = WebGpuCanonicalBufferUploadSource::
                                    validateGeneralFiniteInitializedContentWithReady(request,
                                        content.source,
                                        *descriptor,
                                        ordinal,
                                        ready,
                                        resolved))
                                return id;
                        } else {
                            if (e.dynamicOffset || e.bufferAccessOffset
                                || !e.write.bufferBindingRange.matches(
                                    rhi::DescriptorBufferBindingRange{}))
                                return "RHI-WEBGPU-GENERAL-CONTENT-WHOLE-RANGE";
                            if (const auto* id = WebGpuCanonicalBufferUploadSource::
                                    validateInitializedShaderBufferWithReady(content.source,
                                        *lease,
                                        e.write.type,
                                        e.bufferAccessOffset,
                                        e.bufferAccessBytes,
                                        ready))
                                return id;
                        }
                        used = true;
                    }
            if (!used)
                return "RHI-WEBGPU-GENERAL-CONTENT-UNBOUND";
            const auto bytes = content.source->initializedPayloadBytes();
            if (bytes > intent.maxInitializedBytes - totalBytes)
                return "RHI-WEBGPU-GENERAL-CONTENT-BYTE-CAPACITY";
            totalBytes += bytes;
        }
        // Requested general content covers every immutable shader-buffer source.
        for (const auto& input : inputs)
            if (const auto* descriptor =
                    std::get_if<std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>>(
                        &input))
                for (const auto& frozen : (*descriptor)->frozenElements()) {
                    const auto& e = frozen.declaration;
                    if (e.write.type != rhi::DescriptorType::UniformBuffer
                        && e.write.type != rhi::DescriptorType::ReadOnlyStorageBuffer)
                        continue;
                    bool covered = false;
                    for (std::size_t i = 0u; i < count; ++i)
                        if (at(i).sourceIndex == e.sourceIndex)
                            covered = true;
                    if (!covered)
                        return "RHI-WEBGPU-GENERAL-CONTENT-COVERAGE";
                }
        return nullptr;
    }

    template <class At>
    static const char* validateGeometryContents(const Root& root,
        const Stream& stream,
        const Budget& budget,
        std::size_t count,
        At&& at) noexcept
    {
        const auto ready = [](const auto& source) noexcept {
            return source.readyDiagnostic();
        };
        return validateGeometryContents(root, stream, budget, count, at, ready);
    }

    template <class At, class Ready>
    static const char* validateGeometryContents(const Root& root,
        const Stream& stream,
        const Budget& budget,
        std::size_t count,
        At&& at,
        Ready&& ready) noexcept
    {
        const auto& intent = budget.geometryContent;
        if (const auto* id = intent.diagnostic())
            return id;
        if (!intent.usage.matches(stream.originalBudget().geometryUsage))
            return "RHI-WEBGPU-CANONICAL-CLOSURE-GEOMETRY-FOOTPRINT-MISMATCH";
        if (!count || count > intent.maxSources)
            return "RHI-WEBGPU-CANONICAL-CLOSURE-GEOMETRY-SOURCE-CAPACITY";
        if (static_cast<std::uint64_t>(count)
                * WebGpuCanonicalBufferUploadSource::initializedIndexOwnerBound
            > intent.maxRetainedOwners)
            return "RHI-WEBGPU-CANONICAL-CLOSURE-GEOMETRY-OWNER-CAPACITY";
        const auto& inventory = sources(root)->sources();
        const auto& inputs = *stream.originalInputs();
        const auto& arguments = inputs.originalScalars()->originalArguments()->records();
        const auto content = [&](std::size_t source) noexcept
            -> std::shared_ptr<const WebGpuCanonicalBufferUploadSource> {
            for (std::size_t i = 0u; i < count; ++i)
                if (at(i).sourceIndex == source)
                    return at(i).source;
            return {};
        };
        std::uint64_t bytes = 0u;
        for (std::size_t i = 0u; i < count; ++i) {
            const auto entry = at(i);
            if (!entry.source || entry.sourceIndex >= inventory.size())
                return "RHI-WEBGPU-CANONICAL-CLOSURE-GEOMETRY-SOURCE";
            for (std::size_t j = 0u; j < i; ++j)
                if (at(j).sourceIndex == entry.sourceIndex)
                    return "RHI-WEBGPU-CANONICAL-CLOSURE-GEOMETRY-DUPLICATE";
            const auto* lease = std::get_if<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                &inventory[entry.sourceIndex]);
            if (!lease)
                return "RHI-WEBGPU-CANONICAL-CLOSURE-GEOMETRY-SOURCE";
            bool bound = false;
            const auto inspected = inputs.visitGeometryAccesses(
                intent.usage, [&](const auto& access) noexcept -> const char* {
                    if (access.binding.sourceIndex != entry.sourceIndex)
                        return nullptr;
                    const auto& scalar = arguments[access.bindingCommandIndex].scalar;
                    if (access.binding.kind() == rhi::CommandUsageKind::BindIndexBuffer) {
                        const auto* fields =
                            scalar.template get<rhi::CommandScalarArguments::IndexBinding>();
                        if (!fields)
                            return "RHI-WEBGPU-CANONICAL-CLOSURE-GEOMETRY-SCALAR";
                        if (const auto* id = WebGpuCanonicalBufferUploadSource::
                                validateInitializedIndexContentWithReady(
                                    entry.source, *lease, fields->format, ready))
                            return id;
                    } else {
                        const auto* fields =
                            scalar.template get<rhi::CommandScalarArguments::VertexBinding>();
                        const auto* pipeline = std::get_if<
                            std::shared_ptr<const rhi::OriginalCommandGraphicsPipelineSource>>(
                            &inventory[access.binding.pipelineSourceIndex]);
                        const auto* definition = pipeline
                            ? (*pipeline)->originalObject()->originalRenderPipelineDefinition()
                            : nullptr;
                        if (!fields || !definition)
                            return "RHI-WEBGPU-CANONICAL-CLOSURE-GEOMETRY-PIPELINE";
                        const rhi::VertexBufferLayoutDesc* layout = nullptr;
                        for (const auto& candidate : definition->vertexBuffers)
                            if (candidate.slot == fields->slot) {
                                layout = &candidate;
                                break;
                            }
                        if (!layout)
                            return "RHI-WEBGPU-CANONICAL-CLOSURE-GEOMETRY-STREAM";
                        rhi::VertexInputConsumptionContract::Stream shape{};
                        if (const auto* id =
                                rhi::VertexInputConsumptionContract::inspectStream(*layout, shape))
                            return id;
                        if (const auto* id = WebGpuCanonicalBufferUploadSource::
                                validateInitializedVertexContentWithReady(
                                    entry.source, *lease, shape, ready))
                            return id;
                        if (access.drawConsumption())
                            if (const auto* draw = arguments[access.commandIndex]
                                    .scalar.template get<rhi::CommandScalarArguments::Draw>())
                                if (const auto* id = WebGpuCanonicalBufferUploadSource::
                                        validateInitializedVertexRangeWithReady(entry.source,
                                            *lease,
                                            shape,
                                            fields->offset,
                                            draw->vertices,
                                            draw->firstVertex,
                                            draw->instances,
                                            draw->firstInstance,
                                            ready))
                                    return id;
                    }
                    bound = true;
                    return nullptr;
                });
            if (inspected.diagnostic)
                return inspected.diagnostic;
            if (!bound)
                return "RHI-WEBGPU-CANONICAL-CLOSURE-GEOMETRY-UNBOUND";
            if (entry.source->overlapsCpuOwner(root.originalMetadataQuota()))
                return "RHI-WEBGPU-CANONICAL-CLOSURE-GEOMETRY-QUOTA-DOMAIN";
            const auto size = entry.source->initializedPayloadBytes();
            if (!core::containsUnsignedRange(
                    std::uint64_t{0u}, intent.maxInitializedBytes, bytes, size))
                return "RHI-WEBGPU-CANONICAL-CLOSURE-GEOMETRY-BYTE-CAPACITY";
            bytes += size;
        }
        const auto coverage = inputs.visitGeometryAccesses(
            intent.usage, [&](const auto& access) noexcept -> const char* {
                return content(access.binding.sourceIndex)
                    ? nullptr
                    : "RHI-WEBGPU-CANONICAL-CLOSURE-GEOMETRY-COVERAGE";
            });
        if (coverage.diagnostic)
            return coverage.diagnostic;
        if (!coverage.draws || stream.counts().geometryBinds != coverage.bindings
            || stream.counts().geometryDraws != coverage.draws
            || stream.counts().geometryDrawAccesses != coverage.drawAccesses)
            return "RHI-WEBGPU-CANONICAL-CLOSURE-GEOMETRY-COUNTS";
        using Fetch = rhi::OriginalIndexedDrawFetchContract;
        const auto indices = [&](const Fetch::Point& point,
                                 const auto& lease,
                                 auto& output) noexcept -> const char* {
            return WebGpuCanonicalBufferUploadSource::analyzeInitializedIndicesWithReady(
                content(point.indexSource),
                lease,
                point.index.format,
                point.index.offset,
                point.draw.firstIndex,
                point.draw.indices,
                point.draw.vertexOffset,
                output,
                ready);
        };
        const auto vertices = [&](const Fetch::Point& point,
                                  const auto& binding,
                                  const auto& shape,
                                  const auto& fields,
                                  const auto& lease) noexcept -> const char* {
            return WebGpuCanonicalBufferUploadSource::validateInitializedVertexRangeWithReady(
                content(binding.sourceIndex),
                lease,
                shape,
                fields.offset,
                point.fetch.vertexCount,
                point.fetch.firstVertex,
                point.draw.instances,
                point.draw.firstInstance,
                ready);
        };
        const auto fetched = Fetch::visitInitialized(stream.originalInputs(),
            intent.fetch,
            indices,
            vertices,
            [&](const Fetch::Point& point) noexcept -> const char* {
                return point.commandIndex < stream.operations().size()
                        && stream.operations()[point.commandIndex].kind
                            == rhi::CommandUsageKind::DrawIndexed
                        && stream.operations()[point.commandIndex].elementCount == 1u
                    ? nullptr
                    : "RHI-WEBGPU-CANONICAL-CLOSURE-GEOMETRY-DRAW-CORRESPONDENCE";
            });
        return fetched.diagnostic;
    }

    template <class At>
    static const char* validateIndexBindings(const Root& root,
        const Stream& stream,
        const Budget& budget,
        std::size_t count,
        At&& at) noexcept
    {
        if (const auto* id = budget.indexContent.diagnostic())
            return id;
        if (!count || count > budget.indexContent.maxSources)
            return "RHI-WEBGPU-CANONICAL-CLOSURE-INDEX-SOURCE-CAPACITY";
        const auto& inventory = sources(root)->sources();
        std::uint64_t bytes = 0u;
        const auto ownerBound = static_cast<std::uint64_t>(count)
            * WebGpuCanonicalBufferUploadSource::initializedIndexOwnerBound;
        if (ownerBound > budget.indexContent.maxRetainedOwners)
            return "RHI-WEBGPU-CANONICAL-CLOSURE-INDEX-OWNER-CAPACITY";
        for (std::size_t i = 0u; i < count; ++i) {
            const auto entry = at(i);
            if (!entry.source || entry.sourceIndex >= inventory.size())
                return "RHI-WEBGPU-CANONICAL-CLOSURE-INDEX-SOURCE";
            for (std::size_t j = 0u; j < i; ++j)
                if (at(j).sourceIndex == entry.sourceIndex)
                    return "RHI-WEBGPU-CANONICAL-CLOSURE-INDEX-DUPLICATE";
            const auto* lease = std::get_if<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                &inventory[entry.sourceIndex]);
            if (!lease)
                return "RHI-WEBGPU-CANONICAL-CLOSURE-INDEX-SOURCE";
            bool bound = false;
            const auto validation = stream.originalInputs()->visitGeometryAccesses(
                stream.originalBudget().geometryUsage,
                [&](const auto& access) noexcept -> const char* {
                    if (access.binding.kind() != rhi::CommandUsageKind::BindIndexBuffer
                        || access.binding.sourceIndex != entry.sourceIndex)
                        return nullptr;
                    const auto* scalar =
                        stream.originalInputs()
                            ->originalScalars()
                            ->originalArguments()
                            ->records()[access.commandIndex]
                            .scalar.template get<rhi::CommandScalarArguments::IndexBinding>();
                    if (!scalar)
                        return "RHI-WEBGPU-CANONICAL-CLOSURE-INDEX-SCALAR";
                    if (const auto* id =
                            WebGpuCanonicalBufferUploadSource::validateInitializedIndexContent(
                                entry.source, *lease, scalar->format))
                        return id;
                    bound = true;
                    return nullptr;
                });
            if (validation.diagnostic)
                return validation.diagnostic;
            if (!bound)
                return "RHI-WEBGPU-CANONICAL-CLOSURE-INDEX-UNBOUND";
            if (entry.source->overlapsCpuOwner(root.originalMetadataQuota()))
                return "RHI-WEBGPU-CANONICAL-CLOSURE-INDEX-QUOTA-DOMAIN";
            const auto size = entry.source->initializedPayloadBytes();
            if (!core::containsUnsignedRange(
                    std::uint64_t{0u}, budget.indexContent.maxInitializedBytes, bytes, size))
                return "RHI-WEBGPU-CANONICAL-CLOSURE-INDEX-BYTE-CAPACITY";
            bytes += size;
        }
        // Every original index binding must consume one exact initialized actor.
        return stream.originalInputs()
            ->visitGeometryAccesses(stream.originalBudget().geometryUsage,
                [&](const auto& access) noexcept -> const char* {
                    if (access.binding.kind() != rhi::CommandUsageKind::BindIndexBuffer)
                        return nullptr;
                    for (std::size_t i = 0u; i < count; ++i)
                        if (at(i).sourceIndex == access.binding.sourceIndex)
                            return nullptr;
                    return "RHI-WEBGPU-CANONICAL-CLOSURE-INDEX-COVERAGE";
                })
            .diagnostic;
    }

    // Allocation-free dry mapping reuses exact existing registered observation.
    // Every recapture failure propagates before cold publication.
    // Closure7 retains the declared descriptor domain; shared content/readback
    // machinery must not reinterpret an ordinary issued pipeline as expansion.
    static bool usesTableExpansion(std::uint32_t version, const Root& root) noexcept
    {
        if (version != 7u)
            return version == 3u || version == 4u;
        return descriptorDomainVersion(version, root) == 2u;
    }

    static std::uint32_t descriptorDomainVersion(std::uint32_t version, const Root& root) noexcept
    {
        bool tables = false, ordinaryDescriptors = false;
        for (const auto& declaration : sources(root)->sources()) {
            tables = tables
                || std::holds_alternative<
                    std::shared_ptr<const rhi::OriginalCommandDescriptorTableSource>>(declaration);
            ordinaryDescriptors = ordinaryDescriptors
                || std::holds_alternative<
                    std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>>(declaration);
        }
        return rhi::OriginalCommandNativeClosureContract::pipelineSourceVersion(
            version, tables, ordinaryDescriptors);
    }

    static Entry inspectEntry(const Root& root,
        std::size_t ordinal,
        const char*& diagnostic,
        bool typed,
        bool tables,
        bool general = false) noexcept
    {
        const auto& input = sources(root)->sources();
        for (std::size_t i = 0u; i < input.size(); ++i)
            if (const auto original = object(input[i], typed, tables, general)) {
                if (ordinal--)
                    continue;
                Entry entry{i,
                    *original,
                    original->registration(),
                    original->registration().generation(),
                    WebGpuTransferResourceObservation{}};
                if (const auto* id = capture(entry, tables)) {
                    diagnostic = id;
                    return entry;
                }
                // prepareOriginalImpl validates every typed binding before
                // this quantitative scan, and publication validates it again.
                // Replaying the full semantic graph for each identity pair
                // is unnecessary; capture still checks every native identity.
                return entry;
            }
        diagnostic = "RHI-WEBGPU-CANONICAL-CLOSURE-COVERAGE";
        return {SIZE_MAX, {}, {}, 0u, WebGpuTransferResourceObservation{}};
    }

    template <class At>
    static core::SharedAllocationProjection::Result inspectAllocations(
        std::size_t count, std::uint64_t maxBytes, At&& at) noexcept
    {
        if (count > SIZE_MAX / entryAllocationSlots)
            return {core::SharedAllocationProjection::Status::CountCapacity, 0u};
        const auto slots = count * entryAllocationSlots;
        return core::SharedAllocationProjection::inspect(
            slots, slots, maxBytes, [&](std::size_t i) noexcept {
                const auto entry = at(i / entryAllocationSlots);
                return i % entryAllocationSlots ? entry.stagingAllocationRecord()
                                                : entry.allocationRecord();
            });
    }

    template <class At>
    static std::size_t inspectPayloads(std::size_t count, std::uint32_t limit, At&& at) noexcept
    {
        const auto result = core::SharedIdentityProjection::inspect(
            count, count, limit, [&](std::size_t i) noexcept {
                const auto payload = at(i).payload();
                return payload
                    ? core::SharedIdentityProjection::Record{static_cast<std::uint64_t>(
                                                                 reinterpret_cast<std::uintptr_t>(
                                                                     payload.get())),
                          1u,
                          payload,
                          payload.get()}
                    : core::SharedIdentityProjection::Record{};
            });
        return result.status == core::SharedIdentityProjection::Status::Complete ? result.identities
                                                                                 : 0u;
    }

    template <class At>
    static std::uint32_t inspectOwners(const Root& root,
        std::size_t count,
        std::uint32_t limit,
        bool lossBound,
        bool typed,
        At&& at,
        const WebGpuCanonicalReadbackJoins* readbacks = nullptr) noexcept
    {
        // The exact domain already belongs to the original registry in root.
        // Device + registered-lifetime checks prove that alias before publication;
        // add only this closure and the distinct loss subscription metadata owner.
        std::uint64_t extraIndexOwners = 0u;
        for (std::size_t i = 0u; i < count; ++i) {
            const auto entry = at(i);
            if (entry.indexContent)
                extraIndexOwners +=
                    WebGpuCanonicalBufferUploadSource::initializedIndexOwnerBound - 1u;
            if (entry.textureContent) {
                const auto bound = completedTextureOwnerBound(*entry.textureContent);
                if (!bound || bound > limit || extraIndexOwners > limit - bound)
                    return 0u;
                extraIndexOwners += bound;
            }
        }
        const std::uint64_t additional = 1u + (lossBound ? 1u : 0u) + (typed ? 1u : 0u)
            + (readbacks ? 1u : 0u) + extraIndexOwners;
        if (root.retainedOwnerCount() > limit || additional > limit - root.retainedOwnerCount())
            return 0u;
        const core::RowMajorIndexGrid grid(count, entryOwnerSlots, SIZE_MAX);
        if (!grid.valid())
            return 0u;
        // Exact declarations/copy layouts and native endpoint owners are
        // projected in the same union, never added as a second whole graph.
        const auto readbackSlots = readbacks
            ? readbacks->entries_.values().size() * readbackEntryOwnerSlots
                + readbacks->copies_.values().size()
            : 0u;
        if (readbackSlots > SIZE_MAX - grid.size())
            return 0u;
        const auto slots = grid.size() + readbackSlots;
        const auto identities = core::SharedIdentityProjection::inspect(slots,
            slots,
            limit - root.retainedOwnerCount() - additional,
            [&](std::size_t i) noexcept {
                std::shared_ptr<const void> owner;
                if (i < grid.size()) {
                    std::uint64_t row = 0u, column = 0u;
                    grid.coordinates(i, row, column);
                    owner = at(row).owner(column);
                } else {
                    const auto local = i - grid.size();
                    const auto endpointSlots =
                        readbacks->entries_.values().size() * readbackEntryOwnerSlots;
                    if (local >= endpointSlots)
                        owner = readbacks->copies_.values()[local - endpointSlots].decision.layout;
                    else if (local % readbackEntryOwnerSlots == 0u)
                        owner =
                            readbacks->entries_.values()[local / readbackEntryOwnerSlots].source;
                    else {
                        const auto endpoint =
                            (local % readbackEntryOwnerSlots - 1u) / readbackEndpointOwnerSlots;
                        const auto column =
                            (local % readbackEntryOwnerSlots - 1u) % readbackEndpointOwnerSlots;
                        owner =
                            readbacks->native_.values()[local / readbackEntryOwnerSlots][endpoint]
                                .owners()[column];
                    }
                }
                return owner
                    ? core::SharedIdentityProjection::Record{static_cast<std::uint64_t>(
                                                                 reinterpret_cast<std::uintptr_t>(
                                                                     owner.get())),
                          1u,
                          owner,
                          owner.get()}
                    : core::SharedIdentityProjection::Record{};
            });
        return identities.status == core::SharedIdentityProjection::Status::Complete
            ? root.retainedOwnerCount() + additional
                + static_cast<std::uint32_t>(identities.identities)
            : 0u;
    }

    const std::shared_ptr<Root> root_;
    const Budget budget_;
    core::BoundedSequence<Entry> entries_;
    const std::optional<rhi::OriginalDeviceNativeLossObservation> loss_;
    const std::shared_ptr<const Stream> stream_;
    const std::shared_ptr<const WebGpuCanonicalReadbackJoins> readbacks_;
    const bool attachmentReadback_;
    std::size_t resources_ = 0u, pipelines_ = 0u, descriptors_ = 0u, payloads_ = 0u;
    std::uint32_t owners_ = 0u;
    std::uint64_t bytes_ = 0u;
};
} // namespace arp::rhi_webgpu
