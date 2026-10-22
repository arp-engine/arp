#include "editor/viewport/editor_scene_viewport_texture_cache.h"

#include "renderer/materials/texture_upload.h"
#include "resource/prefab_asset_resources.h"

#include <limits>

namespace arp::editor {
namespace {
using Diagnostic = EditorSceneViewportTextureCacheDiagnostic;

EditorSceneViewportTextureCacheResult failure(Diagnostic diagnostic, const char* detail)
{
    EditorSceneViewportTextureCacheResult result;
    result.diagnostic = diagnostic;
    result.detail = detail;
    return result;
}
} // namespace

EditorSceneViewportTextureCache::EditorSceneViewportTextureCache(
    EditorSceneViewportTextureCacheBudget budget)
    : budget_(budget)
{}

EditorSceneViewportTextureCacheResult EditorSceneViewportTextureCache::acquire(
    rhi::GpuDevice& device,
    const resource::ResourceManager& resources,
    std::string_view resourceId,
    resource::ResourceGeneration generation,
    EditorSceneViewportTextureColorSpace colorSpace)
{
    if (budget_.maxResidentTextures == 0 || budget_.maxResidentDecodedBytes == 0
        || budget_.maxUploadStagingBytes == 0)
        return failure(Diagnostic::InvalidBudget,
            "texture cache requires explicit resident and upload staging limits");
    if (resourceId.empty() || generation == 0)
        return failure(
            Diagnostic::InvalidIdentity, "texture resource ID and generation are required");
    if ((ownerDevice_ != nullptr && ownerDevice_ != &device)
        || (ownerResources_ != nullptr && ownerResources_ != &resources))
        return failure(Diagnostic::OwnerMismatch,
            "texture cache cannot be reused across device or resource-manager owners");

    const auto handle = resources.find<resource::TextureResource>(resourceId);
    if (!handle.ready() || handle.generation() != generation)
        return failure(Diagnostic::ResourceChanged,
            "texture resource is no longer ready at the resolved generation");
    const auto* source = handle.get();
    const bool expectedSrgb = colorSpace == EditorSceneViewportTextureColorSpace::Srgb;
    if (source->srgb != expectedSrgb || source->decodedSrgb != expectedSrgb
        || (expectedSrgb ? source->usage != "color"
                         : source->usage != "data" && source->usage != "normal"))
        return failure(Diagnostic::ColorSpaceMismatch,
            "texture color-space metadata conflicts with the material binding");
    if (source->decodedWidth == 0 || source->decodedHeight == 0
        || source->decodedWidth > std::numeric_limits<std::uint32_t>::max() / 4u)
        return failure(
            Diagnostic::InvalidDecodedData, "decoded RGBA8 texture dimensions are invalid");
    if (source->decodedHasAlpha != source->alpha)
        return failure(Diagnostic::InvalidDecodedData,
            "decoded alpha metadata conflicts with the texture resource contract");
    const auto bytesPerRow = source->decodedWidth * 4u;
    const auto expectedBytes = static_cast<std::uint64_t>(bytesPerRow) * source->decodedHeight;
    if (expectedBytes == 0 || expectedBytes > std::numeric_limits<std::uint32_t>::max()
        || expectedBytes != source->decodedRgba8.size())
        return failure(Diagnostic::InvalidDecodedData,
            "decoded RGBA8 byte count does not match its dimensions");

    for (const auto& entry : entries_) {
        if (entry->resourceId == resourceId && entry->generation == generation) {
            EditorSceneViewportTextureCacheResult result;
            result.texture = entry->texture.get();
            result.view = entry->view.get();
            result.sampler = sampler_.get();
            result.reused = true;
            return result;
        }
    }
    if (entries_.size() >= budget_.maxResidentTextures
        || expectedBytes > budget_.maxResidentDecodedBytes - residentDecodedBytes_)
        return failure(
            Diagnostic::BudgetExceeded, "viewport texture cache resident budget is exhausted");

    rhi::TextureDesc textureDesc;
    textureDesc.label = "EditorViewportTexture";
    textureDesc.extent = {source->decodedWidth, source->decodedHeight, 1u};
    textureDesc.format =
        expectedSrgb ? rhi::TextureFormat::Rgba8UnormSrgb : rhi::TextureFormat::Rgba8Unorm;
    textureDesc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst;
    textureDesc.memory = rhi::MemoryUsage::GpuOnly;
    textureDesc.initialState = rhi::ResourceState::Undefined;
    auto texture = device.createTexture(textureDesc);
    if (!texture)
        return failure(Diagnostic::TextureCreateFailed, "RHI texture creation failed");

    rhi::TextureViewDesc viewDesc;
    viewDesc.label = "EditorViewportTextureView";
    viewDesc.format = textureDesc.format;
    viewDesc.usage = rhi::TextureUsage::Sampled;
    auto view = device.createTextureView(*texture, viewDesc);
    if (!view)
        return failure(Diagnostic::ViewCreateFailed, "RHI sampled texture view creation failed");

    core::UniquePtr<rhi::GpuSampler> samplerCandidate;
    if (!sampler_) {
        rhi::SamplerDesc samplerDesc;
        samplerDesc.label = "EditorViewportPbrSampler";
        samplerDesc.minFilter = rhi::SamplerFilter::Linear;
        samplerDesc.magFilter = rhi::SamplerFilter::Linear;
        samplerDesc.mipFilter = rhi::SamplerMipFilter::Linear;
        samplerDesc.addressU = rhi::SamplerAddressMode::Repeat;
        samplerDesc.addressV = rhi::SamplerAddressMode::Repeat;
        samplerDesc.addressW = rhi::SamplerAddressMode::ClampToEdge;
        samplerCandidate = device.createSampler(samplerDesc);
        if (!samplerCandidate)
            return failure(Diagnostic::SamplerCreateFailed, "RHI PBR sampler creation failed");
    }

    const auto packedRows = renderer::packRgba8TextureRows(source->decodedRgba8.data(),
        source->decodedRgba8.size(),
        source->decodedWidth,
        source->decodedHeight,
        budget_.maxUploadStagingBytes);
    if (!packedRows.success())
        return failure(Diagnostic::BudgetExceeded, packedRows.diagnostics.c_str());
    const auto uploaded = renderer::uploadTextureRgba8(device,
        *texture,
        packedRows.pixels.data(),
        source->decodedWidth,
        source->decodedHeight,
        packedRows.bytesPerRow,
        source->decodedHeight);
    if (!uploaded.success)
        return failure(Diagnostic::UploadFailed, uploaded.diagnostics.c_str());
    if (samplerCandidate)
        sampler_ = std::move(samplerCandidate);

    auto entry = core::makeUnique<Entry>();
    entry->resourceId = resourceId;
    entry->generation = generation;
    entry->srgb = expectedSrgb;
    entry->decodedBytes = expectedBytes;
    entry->texture = std::move(texture);
    entry->view = std::move(view);
    auto* cachedTexture = entry->texture.get();
    auto* cachedView = entry->view.get();
    entries_.push_back(std::move(entry));
    residentDecodedBytes_ += expectedBytes;
    ownerDevice_ = &device;
    ownerResources_ = &resources;

    EditorSceneViewportTextureCacheResult result;
    result.texture = cachedTexture;
    result.view = cachedView;
    result.sampler = sampler_.get();
    return result;
}

std::size_t EditorSceneViewportTextureCache::residentTextureCount() const noexcept
{
    return entries_.size();
}

std::uint64_t EditorSceneViewportTextureCache::residentDecodedBytes() const noexcept
{
    return residentDecodedBytes_;
}

const char* editorSceneViewportTextureCacheDiagnosticCode(Diagnostic diagnostic) noexcept
{
    switch (diagnostic) {
    case Diagnostic::None:
        return "ARP-EDITOR-VIEWPORT-TEXTURE-CACHE-OK";
    case Diagnostic::InvalidBudget:
        return "ARP-EDITOR-VIEWPORT-TEXTURE-CACHE-BUDGET-INVALID";
    case Diagnostic::InvalidIdentity:
        return "ARP-EDITOR-VIEWPORT-TEXTURE-CACHE-IDENTITY-INVALID";
    case Diagnostic::OwnerMismatch:
        return "ARP-EDITOR-VIEWPORT-TEXTURE-CACHE-OWNER-MISMATCH";
    case Diagnostic::ResourceChanged:
        return "ARP-EDITOR-VIEWPORT-TEXTURE-CACHE-RESOURCE-CHANGED";
    case Diagnostic::InvalidDecodedData:
        return "ARP-EDITOR-VIEWPORT-TEXTURE-CACHE-DECODED-DATA-INVALID";
    case Diagnostic::ColorSpaceMismatch:
        return "ARP-EDITOR-VIEWPORT-TEXTURE-CACHE-COLORSPACE-MISMATCH";
    case Diagnostic::BudgetExceeded:
        return "ARP-EDITOR-VIEWPORT-TEXTURE-CACHE-BUDGET-EXCEEDED";
    case Diagnostic::TextureCreateFailed:
        return "ARP-EDITOR-VIEWPORT-TEXTURE-CACHE-CREATE-FAILED";
    case Diagnostic::ViewCreateFailed:
        return "ARP-EDITOR-VIEWPORT-TEXTURE-CACHE-VIEW-FAILED";
    case Diagnostic::SamplerCreateFailed:
        return "ARP-EDITOR-VIEWPORT-TEXTURE-CACHE-SAMPLER-FAILED";
    case Diagnostic::UploadFailed:
        return "ARP-EDITOR-VIEWPORT-TEXTURE-CACHE-UPLOAD-FAILED";
    }
    return "ARP-EDITOR-VIEWPORT-TEXTURE-CACHE-DIAGNOSTIC-UNKNOWN";
}
} // namespace arp::editor
