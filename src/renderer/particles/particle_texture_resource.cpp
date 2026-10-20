#include "renderer/particles/particle_texture_resource.h"

#include "assets/image_decoder.h"
#include "reflection/reflection.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <utility>

namespace arp::renderer::particles {

namespace {

arp::core::pmr::String particleTextureText(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

arp::core::pmr::String pathToText(const std::filesystem::path& path)
{
    using StringAllocator = arp::core::pmr::String::allocator_type;
    return path.generic_string<char, std::char_traits<char>, StringAllocator>(StringAllocator{
        &arp::core::defaultCpuMemoryResource(),
    });
}

bool validParticleTextureStatus(const arp::core::pmr::String& status)
{
    return arp::resource::validateResourceStatus({status.data(), status.size()},
        {
            arp::resource::ResourceStatus::ContractComplete,
            arp::resource::ResourceStatus::PrototypeComplete,
            arp::resource::ResourceStatus::BackendValidated,
        },
        "particle texture resource")
        .success;
}

bool validateIntent(const ParticleTextureUploadIntent& intent, arp::core::pmr::String& diagnostics)
{
    if (intent.resourceUsage != "particle.sprite.sampled") {
        diagnostics = particleTextureText(
            "particle texture resource requires resourceUsage particle.sprite.sampled.");
        return false;
    }
    if (intent.uploadPolicy != "renderer-owned-staging") {
        diagnostics = particleTextureText(
            "particle texture resource requires renderer-owned-staging uploadPolicy.");
        return false;
    }
    if (intent.requiredFormat != "Rgba8Unorm") {
        diagnostics = particleTextureText("particle texture resource requires Rgba8Unorm format.");
        return false;
    }
    if (intent.requiredDescriptorSet != "ParticleTextureSet") {
        diagnostics = particleTextureText(
            "particle texture resource requires ParticleTextureSet descriptor binding.");
        return false;
    }
    return true;
}

std::filesystem::path resolveDeclaredSourcePath(
    const std::filesystem::path& sourceRoot, const std::filesystem::path& declaredPath)
{
    if (declaredPath.is_absolute()) {
        return declaredPath;
    }
    return sourceRoot / declaredPath;
}

arp::core::pmr::String mimeTypeForPath(const std::filesystem::path& path)
{
    auto extension = arp::core::pmr::String(
        path.extension().generic_string().c_str(), &arp::core::defaultCpuMemoryResource());
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (extension == ".png") {
        return particleTextureText("image/png");
    }
    if (extension == ".jpg" || extension == ".jpeg") {
        return particleTextureText("image/jpeg");
    }
    if (extension == ".tga") {
        return particleTextureText("image/tga");
    }
    return {};
}

bool readBinaryFile(const std::filesystem::path& path,
    const arp::resource::ResourceLoadContext& context,
    arp::core::pmr::Vector<std::uint8_t>& out,
    arp::core::pmr::String& diagnostics)
{
    const auto pathText = pathToText(path);
    if (context.sources != nullptr) {
        const auto* source = context.sources->find(pathText);
        if (source != nullptr) {
            out = source->bytes;
            if (out.empty()) {
                diagnostics = particleTextureText("particle texture preloaded source is empty: ");
                diagnostics += pathText;
                return false;
            }
            return true;
        }
    }
    if (context.requirePreloadedSources) {
        diagnostics = particleTextureText("particle texture requires preloaded source: ");
        diagnostics += pathText;
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input.good()) {
        diagnostics = particleTextureText("particle texture source file is missing: ");
        diagnostics += path.generic_string().c_str();
        return false;
    }
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size <= 0) {
        diagnostics = particleTextureText("particle texture source file is empty: ");
        diagnostics += path.generic_string().c_str();
        return false;
    }
    input.seekg(0, std::ios::beg);
    out.resize(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(out.data()), size);
    if (!input.good()) {
        diagnostics = particleTextureText("particle texture source file is unreadable: ");
        diagnostics += path.generic_string().c_str();
        out.clear();
        return false;
    }
    return true;
}

void registerParticleTextureResourceReflection()
{
    static bool registered = false;
    if (registered) {
        return;
    }
    (void)arp::reflection::registration::class_<ParticleTextureResource>{
        kParticleTextureResourceType,
    };
    arp::reflection::class_of<ParticleTextureResource>()->set_name(kParticleTextureResourceType);
    registered = true;
}

} // namespace

arp::resource::ResourceLoadResult ParticleTextureResource::load(
    arp::resource::ResourceManager& resourceManager,
    const arp::resource::ResourceLoadContext& context)
{
    (void)resourceManager;
    arp::resource::ResourceLoadResult result;
    const auto& desc = loadDesc;
    if (!arp::resource::isResourceUuid(desc.resourceId)) {
        result.diagnostics =
            particleTextureText("particle texture resource requires UUID resourceId.");
        return result;
    }
    if (!validParticleTextureStatus(desc.status)) {
        result.diagnostics = particleTextureText("invalid particle texture resource status");
        return result;
    }
    if (desc.sourceRoot.empty()) {
        result.diagnostics =
            particleTextureText("particle texture resource requires explicit sourceRoot.");
        return result;
    }
    if (desc.sourcePath.empty()) {
        result.diagnostics =
            particleTextureText("particle texture resource requires explicit sourcePath.");
        return result;
    }
    if (!validateIntent(desc.intent, result.diagnostics)) {
        return result;
    }

    const auto resolvedSourcePath = resolveDeclaredSourcePath(desc.sourceRoot, desc.sourcePath);
    const auto mimeType = mimeTypeForPath(resolvedSourcePath);
    if (mimeType.empty()) {
        result.diagnostics =
            particleTextureText("particle texture source mime type is unsupported: ");
        result.diagnostics += resolvedSourcePath.generic_string().c_str();
        return result;
    }

    arp::core::pmr::Vector<std::uint8_t> encodedBytes(&arp::core::defaultCpuMemoryResource());
    if (!readBinaryFile(resolvedSourcePath, context, encodedBytes, result.diagnostics)) {
        return result;
    }

    const auto decoded = assets::decodeImageToRgba8(encodedBytes, mimeType, desc.srgb);
    if (!decoded.success) {
        result.diagnostics = particleTextureText("particle texture decode failed: ");
        result.diagnostics += decoded.diagnostics.c_str();
        return result;
    }
    if (context.maxDecodedBytes != 0u && decoded.image.rgba8.size() > context.maxDecodedBytes) {
        result.diagnostics = particleTextureText("particle texture decoded byte budget exceeded.");
        return result;
    }

    data = arp::core::makeShared<ParticleTextureResourceData>();
    data->textureResourceId = desc.resourceId;
    data->sourcePath = desc.sourcePath;
    data->width = decoded.image.width;
    data->height = decoded.image.height;
    data->srgb = decoded.image.srgb;
    data->intent = desc.intent;
    data->intent.backendOwnedUploadStaging = true;
    data->exposesNativeBackendHandles = false;
    data->rgba8.assign(decoded.image.rgba8.begin(), decoded.image.rgba8.end());
    if (!data->valid()) {
        result.diagnostics =
            particleTextureText("particle texture resource produced an invalid RGBA8 payload.");
        return result;
    }

    return resourceManager.admitResourceMetadata(*this, context.policy);
}

bool ParticleTextureResource::collectFileReadIntents(
    arp::resource::ResourceManager& resourceManager,
    arp::core::pmr::Vector<arp::resource::ResourceFileReadIntent>& intents,
    arp::core::pmr::String& diagnostics) const
{
    (void)resourceManager;
    (void)diagnostics;
    const auto resolvedSourcePath =
        resolveDeclaredSourcePath(loadDesc.sourceRoot, loadDesc.sourcePath);
    arp::resource::ResourceFileReadIntent intent;
    intent.pathScope = arp::core::io::FilePathScope::HostFilesystem;
    intent.path = pathToText(resolvedSourcePath);
    intent.role = particleTextureText("particle.texture.encoded");
    intent.maxBytes = 64u * 1024u * 1024u;
    intents.push_back(std::move(intent));
    return true;
}

ParticleTextureResourceLoadResult loadParticleTextureResource(
    arp::resource::ResourceManager& resourceManager, const ParticleTextureResourceLoadDesc& desc)
{
    registerParticleTextureResourceReflection();

    ParticleTextureResourceLoadResult result;
    result.resource =
        resourceManager.load<ParticleTextureResource>(arp::resource::ResourceLoadContext{}, desc);
    if (!result.resource.success) {
        result.diagnostics = result.resource.diagnostics;
        return result;
    }
    result.success = true;
    return result;
}

} // namespace arp::renderer::particles
