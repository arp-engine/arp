#include "rhi/backends/metal/metal_texture_readback.h"

#include "rhi/frontend/transfer_readback_borrowed_retirement.h"
#include "rhi/frontend/transfer_readback_statistics.h"

#include <charconv>
#include <limits>

namespace arp::rhi_metal {

bool MetalTextureReadback::configureBudget(
    rhi::TextureReadbackBudget budget, core::pmr::String& diagnostic)
{
    if (!pending_.empty()) {
        diagnostic = "RHI-READBACK-BUDGET-IN-FLIGHT";
        return false;
    }
    budget_ = budget;
    charged_ = 0u;
    peakCharged_ = 0u;
    budgetRejectionCount_ = 0u;
    diagnostic.clear();
    return true;
}

rhi::TextureReadbackBudgetStats MetalTextureReadback::budgetStats() const
{
    return {
        budget_.maxBytes != 0u && budget_.maxPending != 0u,
        budget_.maxBytes,
        charged_,
        peakCharged_,
        budgetRejectionCount_,
        static_cast<std::uint32_t>(pending_.size()),
        budget_.maxPending,
    };
}

bool MetalTextureReadback::schedule(rhi::CommandEncoder& commands,
    const rhi::GpuTexture& texture,
    std::string_view label,
    std::string& diagnostic)
{
    core::pmr::String message;
    const auto result = scheduleImpl(commands, texture, label, {}, message);
    diagnostic.assign(message.data(), message.size());
    return result;
}

bool MetalTextureReadback::scheduleTracked(rhi::CommandEncoder& commands,
    const rhi::GpuTexture& texture,
    std::string_view label,
    rhi::TextureReadbackIdentity identity,
    core::pmr::String& diagnostic)
{
    if (!identity.requestId || !identity.frameSerial || !identity.resourceGeneration) {
        diagnostic = "RHI-READBACK-IDENTITY-INVALID";
        return false;
    }
    for (const auto& pending : pending_) {
        if (pending.identity == identity) {
            diagnostic = "RHI-READBACK-IDENTITY-DUPLICATE";
            return false;
        }
    }
    return scheduleImpl(commands, texture, label, identity, diagnostic);
}

bool MetalTextureReadback::scheduleImpl(rhi::CommandEncoder& commands,
    const rhi::GpuTexture& texture,
    std::string_view label,
    rhi::TextureReadbackIdentity identity,
    core::pmr::String& diagnostic)
{
    const auto fail = [&](const char* message) {
        diagnostic = message;
        return false;
    };
    if (texture.originalContentExclusive())
        return fail("RHI-TEXTURE-CONTENT-EXCLUSIVE-USE");
    const auto borrowedDecision = rhi::BorrowedTextureReadbackRetirementContract::lowering(
        rhi::Backend::Metal, budget_.maxBytes != 0u && budget_.maxPending != 0u);
    if (!borrowedDecision.executionAvailable) {
        ++budgetRejectionCount_;
        return fail(borrowedDecision.diagnostic);
    }
    if (pending_.size() >= budget_.maxPending) {
        ++budgetRejectionCount_;
        return fail("RHI-READBACK-PENDING-LIMIT");
    }
    if (label.size() > 256u)
        return fail("RHI-READBACK-LABEL-LIMIT");
    const auto& desc = texture.desc();
    const auto pixelBytes = rhi::bytesPerPixel(desc.format);
    switch (desc.format) {
    case rhi::TextureFormat::R8Unorm:
    case rhi::TextureFormat::R32Float:
    case rhi::TextureFormat::D32Float:
    case rhi::TextureFormat::Rg16Float:
    case rhi::TextureFormat::Rgba16Float:
    case rhi::TextureFormat::Rgba8Unorm:
    case rhi::TextureFormat::Rgba8UnormSrgb:
    case rhi::TextureFormat::Bgra8Unorm:
        break;
    default:
        return fail("RHI-READBACK-FORMAT-UNSUPPORTED");
    }
    const auto count = std::uint64_t(desc.extent.width) * desc.extent.height;
    if (!count || count > UINT32_MAX || desc.extent.depth != 1u || desc.arrayLayerCount != 1u
        || desc.sampleCount != 1u)
        return fail("RHI-READBACK-EXTENT-UNSUPPORTED");
    const rhi::TextureSubresourceRange range{0,
        1,
        0,
        1,
        desc.format == rhi::TextureFormat::D32Float ? rhi::TextureAspect::Depth
                                                    : rhi::TextureAspect::Color};
    if ((desc.usage & rhi::TextureUsage::CopySrc) != rhi::TextureUsage::CopySrc
        || texture.subresourceState(range) != rhi::ResourceState::CopySrc)
        return fail("RHI-READBACK-COPY-SOURCE-REQUIRED");
    const auto aligned = core::alignCpuAllocationOffsetUp(
        std::size_t(desc.extent.width) * pixelBytes, rhi::kTextureUploadRowPitchAlignment);
    if (!aligned.success || aligned.alignedValue > UINT32_MAX)
        return fail("RHI-READBACK-PITCH-OVERFLOW");
    const auto size = std::uint64_t(aligned.alignedValue) * desc.extent.height;
    if (size > (budget_.maxBytes - charged_) / 2u) {
        ++budgetRejectionCount_;
        return fail("RHI-READBACK-BYTE-LIMIT");
    }
    auto command = metalRhiNativePayloadFor(commands);
    const auto resource = registry_->find(texture.id());
    if (!command || !command->usesRegistry(registry_) || !resource
        || resource->texture != metalRhiNativePayloadFor(texture))
        return fail("RHI-READBACK-DEVICE-MISMATCH");
    if (command->stats()->nativeCommandBufferFinished || command->recordingPoisoned())
        return fail("RHI-READBACK-COMMAND-NOT-RECORDING");
    Pending pending;
    pending.buffer = device_.createBuffer({"MetalTextureReadback",
        size,
        rhi::ResourceUsage::CopyDst,
        rhi::MemoryUsage::GpuToCpu,
        rhi::ResourceState::CopyDst});
    if (!pending.buffer)
        return fail("RHI-READBACK-BUFFER-CREATE-FAILED");
    pending.command = std::move(command);
    pending.label.assign(label.data(), label.size());
    pending.format = desc.format;
    pending.width = desc.extent.width;
    pending.height = desc.extent.height;
    pending.pitch = static_cast<std::uint32_t>(aligned.alignedValue);
    pending.charge = size * 2u;
    pending.identity = identity;
    pending.source = texture.id();
    // Reserve before command mutation, bounded by the caller's request count.
    pending_.reserve(pending_.size() + 1u);
    auto transfer = commands.beginTransferPass({"MetalTextureReadback"});
    if (!transfer)
        return fail("RHI-READBACK-TRANSFER-BEGIN-FAILED");
    auto copied = transfer->copyTextureToBuffer({texture.id(),
        pending.buffer->id(),
        range,
        0u,
        pending.pitch,
        pending.height,
        desc.extent});
    const auto ended = transfer->end();
    if (copied.success)
        copied = ended;
    if (!copied.success) {
        pending.command->poisonRecording(copied.diagnostics);
        diagnostic.assign(copied.diagnostics.data(), copied.diagnostics.size());
        return false;
    }
    charged_ += pending.charge;
    if (charged_ > peakCharged_) {
        peakCharged_ = charged_;
    }
    pending_.push_back(std::move(pending));
    diagnostic.clear();
    return true;
}

bool MetalTextureReadback::poll(rhi::TextureReadbackSample& sample)
{
    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
        const auto status = it->command->readbackCompletionStatus();
        if (status == MetalReadbackCompletion::Pending)
            continue;
        sample = {};
        sample.label.assign(it->label.data(), it->label.size());
        char formatText[16];
        const auto formatted = std::to_chars(
            formatText, formatText + sizeof(formatText), static_cast<std::uint32_t>(it->format));
        sample.format.assign(formatText, formatted.ptr);
        sample.width = it->width;
        sample.height = it->height;
        sample.identity = it->identity;
        sample.source = it->source;
        sample.textureFormat = it->format;
        sample.completion = status == MetalReadbackCompletion::Completed
            ? rhi::TextureReadbackCompletion::Completed
            : status == MetalReadbackCompletion::Abandoned
            ? rhi::TextureReadbackCompletion::Abandoned
            : rhi::TextureReadbackCompletion::Failed;
        if (status != MetalReadbackCompletion::Completed) {
            sample.diagnostic = status == MetalReadbackCompletion::Abandoned
                ? "RHI-READBACK-COMMAND-ABANDONED"
                : "RHI-READBACK-GPU-FAILED";
        } else {
            core::pmr::Vector<std::uint8_t> bytes(
                static_cast<std::size_t>(it->buffer->desc().sizeBytes));
            core::pmr::String diagnostic;
            if (readMetalRhiNativeBuffer(metalRhiNativePayloadFor(*it->buffer),
                    bytes.data(),
                    bytes.size(),
                    0u,
                    bytes.size(),
                    diagnostic)) {
                rhi::applyTextureReadbackStatistics(sample,
                    it->format,
                    bytes.data(),
                    bytes.size(),
                    it->width,
                    it->height,
                    it->pitch);
                if (!sample.valid || !sample.channelStatistics.valid)
                    sample.diagnostic = "RHI-READBACK-NONFINITE";
            } else
                sample.diagnostic.assign(diagnostic.data(), diagnostic.size());
        }
        charged_ -= it->charge;
        pending_.erase(it);
        return true;
    }
    return false;
}
} // namespace arp::rhi_metal
