#include "core/memory/cpu_allocator.h"
#include "rhi/backends/null/null_backend.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

struct PbrPreviewShaderContract {
    std::pmr::string shaderName;
    std::pmr::string spirvFilename;
    std::pmr::string metalFilename;
    std::pmr::string wgslFilename;
    std::pmr::string reflectionFilename;
    std::uint32_t pushConstantSizeBytes = 0;
    arp::rhi::DescriptorSetDesc materialTextures;
    arp::rhi::DescriptorSetDesc iblTextures;
};

struct PbrPreviewIblResources {
    arp::rhi::TextureDesc irradianceCube;
    arp::rhi::TextureViewDesc irradianceCubeView;
    arp::rhi::TextureDesc prefilteredSpecularCube;
    arp::rhi::TextureViewDesc prefilteredSpecularCubeView;
    arp::rhi::TextureDesc brdfIntegrationLut;
    arp::rhi::TextureViewDesc brdfIntegrationLutView;
    arp::rhi::SamplerDesc sampler;
};

PbrPreviewShaderContract standardPbrPreviewContract()
{
    PbrPreviewShaderContract contract;
    contract.shaderName = "material_preview_pbr";
    contract.spirvFilename = "material_preview_pbr.spv";
    contract.metalFilename = "material_preview_pbr.metal";
    contract.wgslFilename = "material_preview_pbr.wgsl";
    contract.reflectionFilename = "material_preview_pbr.reflection.yaml";
    contract.pushConstantSizeBytes = 144u;
    contract.materialTextures = arp::rhi::DescriptorSetDesc{
        "MaterialPreviewPbrMaterialTextures",
        {
            "MaterialPreviewPbrMaterialTexturesLayout",
            {
                {0u, arp::rhi::DescriptorType::SampledTexture, arp::rhi::ShaderStage::Fragment},
                {1u, arp::rhi::DescriptorType::SampledTexture, arp::rhi::ShaderStage::Fragment},
                {2u, arp::rhi::DescriptorType::SampledTexture, arp::rhi::ShaderStage::Fragment},
                {3u, arp::rhi::DescriptorType::SampledTexture, arp::rhi::ShaderStage::Fragment},
                {4u, arp::rhi::DescriptorType::SampledTexture, arp::rhi::ShaderStage::Fragment},
                {5u, arp::rhi::DescriptorType::Sampler, arp::rhi::ShaderStage::Fragment},
            },
        },
    };
    contract.iblTextures = arp::rhi::DescriptorSetDesc{
        "MaterialPreviewPbrIblTextures",
        {
            "MaterialPreviewPbrIblTexturesLayout",
            {
                {0u, arp::rhi::DescriptorType::SampledTexture, arp::rhi::ShaderStage::Fragment},
                {1u, arp::rhi::DescriptorType::SampledTexture, arp::rhi::ShaderStage::Fragment},
                {2u, arp::rhi::DescriptorType::SampledTexture, arp::rhi::ShaderStage::Fragment},
                {3u, arp::rhi::DescriptorType::Sampler, arp::rhi::ShaderStage::Fragment},
            },
        },
    };
    return contract;
}

PbrPreviewIblResources standardPbrPreviewIblResources()
{
    PbrPreviewIblResources resources;
    resources.irradianceCube = arp::rhi::TextureDesc{
        "MaterialPreviewIrradianceCube",
        {64u, 64u, 1u},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::ShaderRead,
        1u,
        6u,
        true,
    };
    resources.irradianceCubeView = arp::rhi::TextureViewDesc{
        "MaterialPreviewIrradianceCubeView",
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::Sampled,
        arp::rhi::TextureViewDimension::TextureCube,
        0u,
        1u,
        0u,
        6u,
        arp::rhi::TextureAspect::Color,
    };
    resources.prefilteredSpecularCube = arp::rhi::TextureDesc{
        "MaterialPreviewPrefilteredSpecularCube",
        {256u, 256u, 1u},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::ShaderRead,
        6u,
        6u,
        true,
    };
    resources.prefilteredSpecularCubeView = arp::rhi::TextureViewDesc{
        "MaterialPreviewPrefilteredSpecularCubeView",
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::Sampled,
        arp::rhi::TextureViewDimension::TextureCube,
        0u,
        6u,
        0u,
        6u,
        arp::rhi::TextureAspect::Color,
    };
    resources.brdfIntegrationLut = arp::rhi::TextureDesc{
        "MaterialPreviewBrdfIntegrationLut",
        {256u, 256u, 1u},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::ShaderRead,
        1u,
        1u,
    };
    resources.brdfIntegrationLutView = arp::rhi::TextureViewDesc{
        "MaterialPreviewBrdfIntegrationLutView",
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::Sampled,
        arp::rhi::TextureViewDimension::Texture2D,
        0u,
        1u,
        0u,
        1u,
        arp::rhi::TextureAspect::Color,
    };
    resources.sampler = arp::rhi::SamplerDesc{
        "MaterialPreviewIblSampler",
        arp::rhi::SamplerFilter::Linear,
        arp::rhi::SamplerFilter::Linear,
        arp::rhi::SamplerAddressMode::ClampToEdge,
        arp::rhi::SamplerAddressMode::ClampToEdge,
        arp::rhi::SamplerAddressMode::ClampToEdge,
    };
    return resources;
}

bool hasBinding(const arp::rhi::DescriptorSetDesc& desc,
    const std::uint32_t binding,
    const arp::rhi::DescriptorType type,
    const arp::rhi::ShaderStage stages)
{
    for (const auto& entry : desc.layout.bindings) {
        if (entry.binding == binding && entry.type == type && entry.stages == stages) {
            return true;
        }
    }
    return false;
}

std::filesystem::path projectRoot()
{
#ifdef ARP_SOURCE_DIR
    return std::filesystem::path(ARP_SOURCE_DIR);
#else
    auto current = std::filesystem::current_path();
    while (!current.empty()) {
        if (std::filesystem::exists(current / "CMakeLists.txt")
            && std::filesystem::exists(current / "shaders")) {
            return current;
        }
        const auto parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }
    return std::filesystem::current_path();
#endif
}

std::pmr::string readText(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    std::ostringstream stream;
    stream << file.rdbuf();
    return arp::core::toPmrString(stream.str());
}

} // namespace

int main()
{
    const auto contract = standardPbrPreviewContract();

    assert(contract.shaderName == "material_preview_pbr");
    assert(contract.spirvFilename == "material_preview_pbr.spv");
    assert(contract.metalFilename == "material_preview_pbr.metal");
    assert(contract.wgslFilename == "material_preview_pbr.wgsl");
    assert(contract.reflectionFilename == "material_preview_pbr.reflection.yaml");
    assert(contract.pushConstantSizeBytes == 144u);

    assert(hasBinding(contract.materialTextures,
        0u,
        arp::rhi::DescriptorType::SampledTexture,
        arp::rhi::ShaderStage::Fragment));
    assert(hasBinding(contract.materialTextures,
        5u,
        arp::rhi::DescriptorType::Sampler,
        arp::rhi::ShaderStage::Fragment));
    assert(hasBinding(contract.iblTextures,
        0u,
        arp::rhi::DescriptorType::SampledTexture,
        arp::rhi::ShaderStage::Fragment));
    assert(hasBinding(contract.iblTextures,
        1u,
        arp::rhi::DescriptorType::SampledTexture,
        arp::rhi::ShaderStage::Fragment));
    assert(hasBinding(contract.iblTextures,
        2u,
        arp::rhi::DescriptorType::SampledTexture,
        arp::rhi::ShaderStage::Fragment));
    assert(hasBinding(contract.iblTextures,
        3u,
        arp::rhi::DescriptorType::Sampler,
        arp::rhi::ShaderStage::Fragment));

    const auto resources = standardPbrPreviewIblResources();
    assert(resources.irradianceCube.arrayLayerCount == 6u);
    assert(resources.irradianceCube.cubeCompatible);
    assert(resources.irradianceCube.format == arp::rhi::TextureFormat::Rgba16Float);
    assert(resources.irradianceCubeView.dimension == arp::rhi::TextureViewDimension::TextureCube);
    assert(resources.irradianceCubeView.arrayLayerCount == 6u);
    assert(resources.prefilteredSpecularCube.arrayLayerCount == 6u);
    assert(resources.prefilteredSpecularCube.cubeCompatible);
    assert(resources.prefilteredSpecularCube.mipLevelCount >= 5u);
    assert(resources.prefilteredSpecularCubeView.dimension
        == arp::rhi::TextureViewDimension::TextureCube);
    assert(resources.prefilteredSpecularCubeView.mipLevelCount
        == resources.prefilteredSpecularCube.mipLevelCount);
    assert(resources.brdfIntegrationLut.arrayLayerCount == 1u);
    assert(resources.brdfIntegrationLutView.dimension == arp::rhi::TextureViewDimension::Texture2D);
    assert(resources.brdfIntegrationLut.format == arp::rhi::TextureFormat::Rgba16Float);
    assert(resources.sampler.minFilter == arp::rhi::SamplerFilter::Linear);
    assert(resources.sampler.magFilter == arp::rhi::SamplerFilter::Linear);
    assert(resources.sampler.addressU == arp::rhi::SamplerAddressMode::ClampToEdge);
    assert(resources.sampler.addressV == arp::rhi::SamplerAddressMode::ClampToEdge);
    assert(resources.sampler.addressW == arp::rhi::SamplerAddressMode::ClampToEdge);

    const auto root = projectRoot();
    const auto shaderSource = readText(root / "shaders" / "material_preview" / "pbr_preview.slang");
    const auto pipelineSource = readText(
        root / "games" / "character_viewer" / "app" / "basic_character_preview_pipeline.cpp");
    const auto rendererSource =
        readText(root / "src" / "renderer" / "renderers" / "renderer_common.h");
    const auto stylizedSource =
        readText(root / "samples" / "stylized" / "stylized_scene_renderer.h");
    assert(shaderSource.find("vertexMain") != std::pmr::string::npos);
    assert(shaderSource.find("fragmentMain") != std::pmr::string::npos);
    assert(shaderSource.find("cameraPositionExposure") != std::pmr::string::npos);
    assert(shaderSource.find("baseColorAlpha") != std::pmr::string::npos);
    assert(shaderSource.find("pbrParams") != std::pmr::string::npos);
    assert(shaderSource.find("pbrExtraParams") != std::pmr::string::npos);
    assert(shaderSource.find("environmentParams") != std::pmr::string::npos);
    assert(pipelineSource.find("\"vertexMain\"") != std::pmr::string::npos);
    assert(pipelineSource.find("\"fragmentMain\"") != std::pmr::string::npos);
    assert(pipelineSource.find("cameraPositionExposure") != std::pmr::string::npos);
    assert(pipelineSource.find("baseColorAlpha") != std::pmr::string::npos);
    assert(pipelineSource.find("pbrParams") != std::pmr::string::npos);
    assert(pipelineSource.find("pbrExtraParams") != std::pmr::string::npos);
    assert(pipelineSource.find("environmentParams") != std::pmr::string::npos);
    assert(pipelineSource.find("scene.cameraPosition") != std::pmr::string::npos);
    assert(pipelineSource.find("scene.exposure") != std::pmr::string::npos);
    assert(pipelineSource.find("cubeCompatible") != std::pmr::string::npos);
    assert(pipelineSource.find("TextureViewDimension::TextureCube") != std::pmr::string::npos);
    assert(pipelineSource.find("kFallbackCubeArrayLayerCount = 6u") != std::pmr::string::npos);
    assert(pipelineSource.find("materialTextureBindingForBasicPreview") != std::pmr::string::npos);
    assert(pipelineSource.find("uploadMaterialTextureSource") != std::pmr::string::npos);
    assert(pipelineSource.find("MaterialTextureSemantic::BaseColor") != std::pmr::string::npos);
    assert(pipelineSource.find("MaterialTextureSemantic::Normal") != std::pmr::string::npos);
    assert(pipelineSource.find("MaterialTextureSemantic::MetallicRoughness")
        != std::pmr::string::npos);
    assert(pipelineSource.find("MaterialTextureSemantic::Emissive") != std::pmr::string::npos);
    assert(pipelineSource.find("MaterialTextureSemantic::Occlusion") != std::pmr::string::npos);
    assert(pipelineSource.find("source.materialSlot == material.slot") != std::pmr::string::npos);
    assert(pipelineSource.find("resources.textureViews[binding]") != std::pmr::string::npos);
    assert(pipelineSource.find("fallbackTextureViews_[binding]") != std::pmr::string::npos);
    assert(pipelineSource.find("\"vsMain\"") == std::pmr::string::npos);
    assert(pipelineSource.find("\"fsMain\"") == std::pmr::string::npos);
    assert(pipelineSource.find("bindVertexBuffer") != std::pmr::string::npos);
    assert(pipelineSource.find("bindIndexBuffer") != std::pmr::string::npos);
    assert(pipelineSource.find("vertexBuffers.push_back") != std::pmr::string::npos);
    assert(pipelineSource.find("hasViewport = true") != std::pmr::string::npos);
    assert(pipelineSource.find("hasScissor = true") != std::pmr::string::npos);
    assert(pipelineSource.find("&pushConstants") != std::pmr::string::npos);
    assert(pipelineSource.find("BasicCharacterPreviewFallbackUpload") != std::pmr::string::npos);
    assert(pipelineSource.find("copyBufferToTexture") != std::pmr::string::npos);
    assert(pipelineSource.find("ResourceState::CopyDst") != std::pmr::string::npos);
    assert(rendererSource.find("\"vertexMain\"") != std::pmr::string::npos);
    assert(rendererSource.find("\"fragmentMain\"") != std::pmr::string::npos);
    assert(rendererSource.find("\"vsMain\"") == std::pmr::string::npos);
    assert(rendererSource.find("\"fsMain\"") == std::pmr::string::npos);
    assert(stylizedSource.find("\"vertexMain\"") != std::pmr::string::npos);
    assert(stylizedSource.find("\"fragmentMain\"") != std::pmr::string::npos);
    assert(stylizedSource.find("\"vsMain\"") == std::pmr::string::npos);
    assert(stylizedSource.find("\"fsMain\"") == std::pmr::string::npos);

    return 0;
}
