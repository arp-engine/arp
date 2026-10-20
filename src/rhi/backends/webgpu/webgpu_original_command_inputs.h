#pragma once
#include "rhi/backends/webgpu/webgpu_command_inputs.h"
#include "rhi/backends/webgpu/webgpu_original_buffer_upload_validation.h"
#include "rhi/backends/webgpu/webgpu_original_texture_readback_validation.h"
#include "rhi/backends/webgpu/webgpu_original_texture_upload_validation.h"
#include "rhi/backends/webgpu/webgpu_transfer_copy_validation.h"
#include "rhi/frontend/command_arguments.h"
#include "rhi/frontend/command_model.h"

namespace arp::rhi_webgpu {
struct WebGpuOriginalCommandInputsBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxDescriptorRecords = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
    std::uint32_t maxTextureUploadDescriptors = 0u, maxTextureUploadMipMappings = 0u;
    std::uint64_t maxTextureUploadRetainedPixelBytes = 0u;
    std::uint32_t maxReadbackDescriptors = 0u, maxReadbackMipMappings = 0u;
    std::uint64_t maxReadbackUnpublishedResultBytes = 0u;
};

// One full positional input composition. Native/state/scalar legality and the
// actual synchronous recorder remain independently mandatory, never implied.
class WebGpuOriginalCommandInputs final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class WebGpuOriginalCommandInputs;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Family = rhi::CommandInputContract::Family;
    using Descriptor = std::variant<rhi::TransferCopyArguments,
        rhi::ResourceDependencyArguments,
        rhi::BufferUploadArguments,
        rhi::TextureUploadOriginalInputArguments,
        rhi::TextureReadbackOriginalInputArguments>;

    struct OriginalDescriptor {
        std::size_t commandIndex;
        Descriptor arguments;

        rhi::CommandUsageKind kind() const noexcept
        {
            return std::visit(
                [](const auto& value) {
                    return value.kind();
                },
                arguments);
        }
    };

    struct RawInput {
        rhi::CommandVariableArguments::Input scalar;
        std::variant<std::monostate,
            const rhi::RenderPassDesc*,
            rhi::TransferCopyArguments,
            rhi::ResourceDependencyArguments,
            rhi::BufferUploadArguments,
            rhi::TextureUploadOriginalInputArguments,
            rhi::TextureReadbackOriginalInputArguments>
            descriptor;
    };

    WebGpuOriginalCommandInputs(ConstructionKey,
        std::shared_ptr<const WebGpuFrozenCommandScalars> scalars,
        std::shared_ptr<const WebGpuFrozenRenderPassBinding> passes,
        const WebGpuOriginalCommandInputsBudget& budget)
        : scalars_(std::move(scalars)), passes_(std::move(passes)),
          descriptors_(budget.maxDescriptorRecords), budget_(budget)
    {}

    static constexpr Family family(rhi::CommandUsageKind kind) noexcept
    {
        return rhi::CommandInputContract::family(kind);
    }

    static const char* validateBudget(const WebGpuOriginalCommandInputsBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-WEBGPU-ORIGINAL-INPUTS-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-WEBGPU-ORIGINAL-INPUTS-OPT-IN";
        if (!b.maxCpuMetadataBytes)
            return "RHI-WEBGPU-ORIGINAL-INPUTS-BUDGET";
        return nullptr;
    }

    static std::shared_ptr<const WebGpuOriginalCommandInputs> prepareOriginal(
        std::shared_ptr<const WebGpuFrozenCommandScalars> scalars,
        std::shared_ptr<const WebGpuFrozenRenderPassBinding> passes,
        const OriginalDescriptor* descriptors,
        std::size_t count,
        const WebGpuOriginalCommandInputsBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(budget);
        if (diagnostic)
            return {};
        if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
            diagnostic = "RHI-WEBGPU-ORIGINAL-INPUTS-QUOTA";
            return {};
        }
        if ((count && !descriptors) || count > budget.maxDescriptorRecords) {
            diagnostic = "RHI-WEBGPU-ORIGINAL-INPUTS-CAPACITY";
            return {};
        }
        if ((diagnostic = validateSources(scalars, passes, descriptors, count, budget)))
            return {};
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto result = core::makeUnique<WebGpuOriginalCommandInputs>(
                ConstructionKey{}, std::move(scalars), std::move(passes), budget);
            for (std::size_t i = 0u; i < count; ++i)
                result->descriptors_.append(descriptors[i]);
            return shareOriginal(std::move(result));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-WEBGPU-ORIGINAL-INPUTS-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-WEBGPU-ORIGINAL-INPUTS-ALLOCATION";
            return {};
        }
    }

    const char* validate() const noexcept
    {
        return validateSources(
            scalars_, passes_, descriptors_.values().data(), descriptors_.values().size(), budget_);
    }

    const auto& scalars() const noexcept { return scalars_; }

    const auto& passes() const noexcept { return passes_; }

    const auto& descriptors() const noexcept { return descriptors_.values(); }

    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = validate())
            return id;
        return webGpuGenericRecordingUsageDiagnostic;
    }

    const char* compareRaw(std::size_t index,
        const RawInput& input,
        const WebGpuGenericCommandUsage::RawReference* references,
        std::size_t count) const noexcept
    {
        if (const auto* id = validate())
            return id;
        if (const auto* id = scalars_->compareRaw(index, input.scalar, references, count))
            return id;
        switch (family(input.scalar.scalar.kind())) {
        case Family::Scalar:
            return std::holds_alternative<std::monostate>(input.descriptor)
                ? nullptr
                : "RHI-WEBGPU-ORIGINAL-INPUTS-PAYLOAD";
        case Family::Pass: {
            const auto* pass = std::get_if<const rhi::RenderPassDesc*>(&input.descriptor);
            if (!pass || !*pass)
                return "RHI-WEBGPU-ORIGINAL-INPUTS-PAYLOAD";
            return passes_->compareRaw(index, **pass, references, count);
        }
        case Family::Copy:
        case Family::Dependency:
        case Family::BufferUpload:
        case Family::TextureUpload:
        case Family::TextureReadback:
            for (const auto& original : descriptors())
                if (original.commandIndex == index)
                    return std::visit(
                        [&](const auto& value) -> const char* {
                            using T = std::decay_t<decltype(value)>;
                            const auto* incoming = std::get_if<T>(&input.descriptor);
                            return incoming && value.matches(*incoming)
                                ? nullptr
                                : "RHI-WEBGPU-ORIGINAL-INPUTS-PAYLOAD";
                        },
                        original.arguments);
            return "RHI-WEBGPU-ORIGINAL-INPUTS-ORDER";
        case Family::Unsupported:
            return "RHI-WEBGPU-ORIGINAL-INPUTS-UNSUPPORTED";
        }
        return "RHI-WEBGPU-ORIGINAL-INPUTS-UNSUPPORTED";
    }

private:
    static const char* validateSources(
        const std::shared_ptr<const WebGpuFrozenCommandScalars>& scalars,
        const std::shared_ptr<const WebGpuFrozenRenderPassBinding>& passes,
        const OriginalDescriptor* descriptors,
        std::size_t count,
        const WebGpuOriginalCommandInputsBudget& budget) noexcept
    {
        if (!scalars || !scalars->hasQuotaBoundSharedOwner(scalars))
            return "RHI-WEBGPU-ORIGINAL-INPUTS-SCALARS";
        if (const auto* id = scalars->validate())
            return id;
        if (passes) {
            if (!passes->hasQuotaBoundSharedOwner(passes))
                return "RHI-WEBGPU-ORIGINAL-INPUTS-PASSES";
            if (!core::SharedOwnerProjection::sameOwner(passes->usage(), scalars->usage()))
                return "RHI-WEBGPU-ORIGINAL-INPUTS-USAGE";
            if (const auto* id = passes->validate())
                return id;
        }
        const auto& usage = *scalars->usage();
        std::size_t consumed = 0u, textureDescriptors = 0u, textureMips = 0u;
        std::uint64_t texturePixels = 0u;
        std::size_t readbackDescriptors = 0u, readbackMips = 0u;
        std::uint64_t readbackResults = 0u;
        for (std::size_t i = 0u; i < usage.commands().size(); ++i) {
            const auto kind = usage.commands()[i].kind;
            const auto required = family(kind);
            if (required == Family::Unsupported)
                return "RHI-WEBGPU-ORIGINAL-INPUTS-UNSUPPORTED";
            if (required == Family::Pass && !passes)
                return "RHI-WEBGPU-ORIGINAL-INPUTS-PASSES";
            if (required != Family::Copy && required != Family::Dependency
                && required != Family::BufferUpload && required != Family::TextureUpload
                && required != Family::TextureReadback)
                continue;
            if (const auto* id = rhi::CommandInputContract::consumeDescriptor(i,
                    kind,
                    descriptors,
                    count,
                    consumed,
                    "RHI-WEBGPU-ORIGINAL-INPUTS-ORDER",
                    "RHI-WEBGPU-ORIGINAL-INPUTS-KIND"))
                return id;
            const auto& descriptor = descriptors[consumed - 1u].arguments;
            if (required == Family::TextureReadback) {
                const auto* readback =
                    std::get_if<rhi::TextureReadbackOriginalInputArguments>(&descriptor);
                if (!readback)
                    return "RHI-WEBGPU-ORIGINAL-INPUTS-KIND";
                if (readbackDescriptors == budget.maxReadbackDescriptors)
                    return "RHI-WEBGPU-ORIGINAL-READBACK-CAPACITY";
                if (const auto* id = WebGpuOriginalTextureReadbackValidation::validate(
                        *usage.projection(), *readback, usage.commands()[i].firstReference))
                    return id;
                const auto& input = *readback->originalInput();
                const auto mipCount =
                    rhi::semantics::TextureReadbackLayoutValidation::mipCount(input);
                rhi::ReadbackTextureDesc fields;
                input.scalarFields().writeScalarFields(fields);
                if (mipCount > budget.maxReadbackMipMappings - readbackMips)
                    return "RHI-WEBGPU-ORIGINAL-READBACK-MIP-CAPACITY";
                if (fields.sizeBytes > budget.maxReadbackUnpublishedResultBytes - readbackResults)
                    return "RHI-WEBGPU-ORIGINAL-READBACK-RESULT-CAPACITY";
                if (scalars->arguments()->text(i) != input.label())
                    return "RHI-WEBGPU-ORIGINAL-READBACK-LABEL";
                ++readbackDescriptors;
                readbackMips += mipCount;
                readbackResults += fields.sizeBytes;
            } else if (required == Family::TextureUpload) {
                const auto* upload =
                    std::get_if<rhi::TextureUploadOriginalInputArguments>(&descriptor);
                if (!upload)
                    return "RHI-WEBGPU-ORIGINAL-INPUTS-KIND";
                if (textureDescriptors == budget.maxTextureUploadDescriptors)
                    return "RHI-WEBGPU-ORIGINAL-TEXTURE-UPLOAD-CAPACITY";
                if (const auto* id = WebGpuOriginalTextureUploadValidation::validate(
                        *usage.projection(), *upload, usage.commands()[i].firstReference))
                    return id;
                const auto& input = *upload->originalInput();
                const auto mipCount =
                    rhi::semantics::TextureUploadLayoutValidation::mipCount(input);
                if (mipCount > budget.maxTextureUploadMipMappings - textureMips)
                    return "RHI-WEBGPU-ORIGINAL-TEXTURE-UPLOAD-MIP-CAPACITY";
                if (input.pixelBytes().size()
                    > budget.maxTextureUploadRetainedPixelBytes - texturePixels)
                    return "RHI-WEBGPU-ORIGINAL-TEXTURE-UPLOAD-PIXEL-CAPACITY";
                if (scalars->arguments()->text(i) != input.label())
                    return "RHI-WEBGPU-ORIGINAL-TEXTURE-UPLOAD-LABEL";
                ++textureDescriptors;
                textureMips += mipCount;
                texturePixels += input.pixelBytes().size();
            } else if (required == Family::BufferUpload) {
                const auto* upload = std::get_if<rhi::BufferUploadArguments>(&descriptor);
                if (!upload)
                    return "RHI-WEBGPU-ORIGINAL-INPUTS-KIND";
                if (const auto* id = WebGpuOriginalBufferUploadValidation::validate(
                        *usage.projection(), *upload, usage.commands()[i].firstReference))
                    return id;
            } else if (required == Family::Copy) {
                const auto* copy = std::get_if<rhi::TransferCopyArguments>(&descriptor);
                if (!copy)
                    return "RHI-WEBGPU-ORIGINAL-INPUTS-KIND";
                const auto& transfer = usage.projection()->transfer();
                if (!transfer || !transfer->hasQuotaBoundSharedOwner(transfer))
                    return "RHI-WEBGPU-ORIGINAL-INPUTS-TRANSFER";
                if (const auto* id = WebGpuTransferCopyValidation::validate(*transfer, *copy))
                    return id;
                if (!resourceMatches(usage, i, 0u, copy->source(), true)
                    || !resourceMatches(usage, i, 1u, copy->destination(), true))
                    return "RHI-WEBGPU-ORIGINAL-INPUTS-RESOURCE";
            } else {
                const auto* dependency = std::get_if<rhi::ResourceDependencyArguments>(&descriptor);
                if (!dependency)
                    return "RHI-WEBGPU-ORIGINAL-INPUTS-KIND";
                const auto first = dependency->firstResource(),
                           second = dependency->secondResource();
                if (!resourceMatches(usage, i, 0u, first)
                    || (kind == rhi::CommandUsageKind::AliasBarrier
                        && !resourceMatches(usage, i, 1u, second)))
                    return "RHI-WEBGPU-ORIGINAL-INPUTS-RESOURCE";
                const auto& reference =
                    usage.projection()->references()[usage.commands()[i].firstReference];
                if (reference.source == WebGpuCommandReferenceSource::OriginalTextureReadbackState
                    && kind != rhi::CommandUsageKind::TextureBarrier)
                    return "RHI-WEBGPU-ORIGINAL-READBACK-STATE-KIND";
            }
        }
        return consumed == count ? nullptr : "RHI-WEBGPU-ORIGINAL-INPUTS-ORDER";
    }

    static bool resourceMatches(const WebGpuGenericCommandUsage& usage,
        std::size_t command,
        std::size_t position,
        rhi::GpuResourceId id,
        bool requireTransfer = false) noexcept
    {
        const auto& entry = usage.commands()[command];
        if (position >= entry.referenceCount || !id.isValid())
            return false;
        const auto& reference = usage.projection()->references()[entry.firstReference + position];
        if (requireTransfer && reference.source != WebGpuCommandReferenceSource::Transfer)
            return false;
        return (reference.source == WebGpuCommandReferenceSource::Transfer
                   || reference.source == WebGpuCommandReferenceSource::Resource
                   || (reference.source
                           == WebGpuCommandReferenceSource::OriginalTextureReadbackState
                       && reference.domain == WebGpuCommandReferenceDomain::Texture))
            && reference.resource == id;
    }

    const std::shared_ptr<const WebGpuFrozenCommandScalars> scalars_;
    const std::shared_ptr<const WebGpuFrozenRenderPassBinding> passes_;
    core::BoundedSequence<OriginalDescriptor> descriptors_;
    const WebGpuOriginalCommandInputsBudget budget_;
};
} // namespace arp::rhi_webgpu
