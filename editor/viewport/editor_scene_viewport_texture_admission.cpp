#include "editor/viewport/editor_scene_viewport_texture_admission.h"

#include "assets/image_decoder.h"
#include "editor/viewport/editor_scene_viewport_texture_source.h"

namespace arp::editor {
namespace {
using D = EditorViewportTextureAdmissionDiagnostic;
constexpr std::uint8_t unused = 0u, srgb = 1u, linear = 2u;

EditorViewportTextureAdmissionResult reject(D d, const char* text)
{
    EditorViewportTextureAdmissionResult r;
    r.diagnostic = d;
    r.detail = text;
    return r;
}

bool mark(std::uint32_t index,
    std::uint8_t space,
    std::size_t count,
    core::pmr::Vector<std::uint8_t>& uses,
    D& error)
{
    if (index == assets::invalidImportedTextureIndex)
        return true;
    if (index >= count) {
        error = D::InvalidTextureReference;
        return false;
    }
    if (uses[index] && uses[index] != space) {
        error = D::ConflictingColorSpace;
        return false;
    }
    uses[index] = space;
    return true;
}

bool same(const resource::TextureResource& a, const resource::TextureResource& b)
{
    return a.sourceResourceId == b.sourceResourceId && a.sourceGeneration == b.sourceGeneration
        && a.usage == b.usage && a.srgb == b.srgb && a.alpha == b.alpha
        && a.decodedWidth == b.decodedWidth && a.decodedHeight == b.decodedHeight
        && a.decodedRgba8 == b.decodedRgba8;
}
} // namespace

EditorViewportTextureAdmissionResult admitEditorViewportGlbTextures(
    resource::ResourceManager& resources,
    const resource::MeshAssetResource& mesh,
    resource::ResourceGeneration generation,
    std::size_t maxDecodedBytes)
{
    if (!generation || resources.generationOf(mesh.reference().id) != generation)
        return reject(D::SourceNotReady, "source mesh generation is not ready");
    if (mesh.textureResourceIds().size() != mesh.textures().size())
        return reject(D::InvalidTextureIdentity, "texture ID table size mismatch");
    if (!maxDecodedBytes)
        return reject(D::DecodedByteBudgetExceeded, "decoded byte budget is required");
    core::pmr::Vector<std::uint8_t> uses(&core::defaultCpuMemoryResource());
    uses.resize(mesh.textures().size(), unused);
    for (const auto& m : mesh.materials()) {
        if (!m.hasCompleteMaterialImportMetadata)
            return reject(
                D::MissingMaterialMetadata, "complete core material metadata is required");
        if (m.hasUnsupportedMaterialExtensions)
            return reject(
                D::UnsupportedMaterialSemantics, "material extension usage is unsupported");
        D error = D::None;
        if (!mark(m.baseColorTexture, srgb, uses.size(), uses, error)
            || !mark(m.emissiveTexture, srgb, uses.size(), uses, error)
            || !mark(m.normalTexture, linear, uses.size(), uses, error)
            || !mark(m.metallicRoughnessTexture, linear, uses.size(), uses, error)
            || !mark(m.occlusionTexture, linear, uses.size(), uses, error))
            return reject(error, "invalid texture reference or conflicting color space");
    }
    core::pmr::Vector<core::UniquePtr<resource::TextureResource>> staged(
        &core::defaultCpuMemoryResource());
    std::size_t bytes = 0u, reused = 0u;
    for (std::size_t i = 0u; i < uses.size(); ++i) {
        if (uses[i] == unused)
            continue;
        const auto& id = mesh.textureResourceIds()[i];
        if (!resource::isResourceUuid(id))
            return reject(D::InvalidTextureIdentity, "texture stable UUID is invalid");
        const auto imageIndex = mesh.textures()[i].sourceImage;
        if (imageIndex == assets::invalidImportedImageIndex || imageIndex >= mesh.images().size())
            return reject(D::InvalidImageBinding, "texture source image is invalid");

        auto texture = core::makeUnique<resource::TextureResource>();
        resource::ResourceReference ref;
        ref.id = id;
        ref.type = resource::TextureResource::resourceTypeName();
        ref.status = "Contract Complete";
        ref.displayName = mesh.textures()[i].name;
        texture->configureMetadata(std::move(ref));
        texture->name = mesh.textures()[i].name;
        texture->sourceResourceId = mesh.reference().id;
        texture->sourceGeneration = generation;
        texture->usage = uses[i] == srgb ? "color" : "data";
        texture->srgb = uses[i] == srgb;

        const auto& image = mesh.images()[imageIndex];
        core::pmr::Vector<std::uint8_t> encoded(&core::defaultCpuMemoryResource());
        encoded.assign(image.encodedBytes.begin(), image.encodedBytes.end());
        core::pmr::String mime(image.mimeType, &core::defaultCpuMemoryResource());
        const auto remaining = maxDecodedBytes - bytes;
        {
            const auto probe = assets::decodeImageToRgba8(
                core::ArrayView<const std::uint8_t>(encoded.data(), encoded.size()),
                mime,
                texture->srgb,
                remaining);
            if (!probe.success)
                return reject(D::DecodeFailed, probe.diagnostics.c_str());
            texture->alpha = probe.image.alpha;
        }
        const auto decoded = decodeEditorViewportTextureSource(
            *texture, i, mesh.textures(), mesh.images(), mesh.textureResourceIds(), remaining);
        if (!decoded.success)
            return reject(D::DecodeFailed, decoded.detail.c_str());
        if (texture->decodedRgba8.size() > remaining)
            return reject(
                D::DecodedByteBudgetExceeded, "decoded texture batch exceeds byte budget");
        bytes += texture->decodedRgba8.size();

        auto existing = resources.find<resource::TextureResource>(id);
        if (existing.get()) {
            if (!same(*existing.get(), *texture))
                return reject(
                    D::ExistingResourceConflict, "existing texture has different source or pixels");
            ++reused;
        } else if (resources.stateOf(id) != resource::ResourceState::Unloaded) {
            return reject(D::ExistingResourceConflict, "texture ID exists but is not reusable");
        } else {
            staged.push_back(std::move(texture));
        }
    }
    EditorViewportTextureAdmissionResult result;
    result.reusedCount = reused;
    core::pmr::Vector<core::UniquePtr<resource::ResourceBase>> batch(
        &core::defaultCpuMemoryResource());
    batch.reserve(staged.size());
    for (auto& texture : staged)
        batch.emplace_back(std::move(texture));
    const auto loaded = resources.loadResourcesAtomically(std::move(batch));
    if (!loaded.success) {
        result.diagnostic = D::ResourceManagerAdmissionFailed;
        result.detail = loaded.diagnostics;
        return result;
    }
    result.admittedCount = loaded.loadedCount;
    result.success = true;
    return result;
}

const char* editorViewportTextureAdmissionDiagnosticCode(D diagnostic) noexcept
{
    switch (diagnostic) {
    case D::None:
        return "ARP-EDITOR-VIEWPORT-TEXTURE-ADMISSION-OK";
    case D::SourceNotReady:
        return "ARP-EDITOR-VIEWPORT-TEXTURE-SOURCE-GENERATION-MISMATCH";
    case D::MissingMaterialMetadata:
        return "ARP-EDITOR-VIEWPORT-TEXTURE-MATERIAL-METADATA-INCOMPLETE";
    case D::UnsupportedMaterialSemantics:
        return "ARP-EDITOR-VIEWPORT-TEXTURE-MATERIAL-UNSUPPORTED";
    case D::InvalidTextureReference:
        return "ARP-EDITOR-VIEWPORT-TEXTURE-REFERENCE-INVALID";
    case D::ConflictingColorSpace:
        return "ARP-EDITOR-VIEWPORT-TEXTURE-COLORSPACE-CONFLICT";
    case D::InvalidTextureIdentity:
        return "ARP-EDITOR-VIEWPORT-TEXTURE-ID-INVALID";
    case D::InvalidImageBinding:
        return "ARP-EDITOR-VIEWPORT-TEXTURE-IMAGE-INVALID";
    case D::DecodeFailed:
        return "ARP-EDITOR-VIEWPORT-TEXTURE-DECODE-FAILED";
    case D::DecodedByteBudgetExceeded:
        return "ARP-EDITOR-VIEWPORT-TEXTURE-BUDGET-EXCEEDED";
    case D::ExistingResourceConflict:
        return "ARP-EDITOR-VIEWPORT-TEXTURE-EXISTING-CONFLICT";
    case D::ResourceManagerAdmissionFailed:
        return "ARP-EDITOR-VIEWPORT-TEXTURE-ADMISSION-FAILED";
    }
    return "ARP-EDITOR-VIEWPORT-TEXTURE-ADMISSION-UNKNOWN";
}
} // namespace arp::editor
