#pragma once

#include "core/cpu_allocator.h"
#include "renderer/render_frame.h"
#include "renderer/shader_package_reflection.h"
#include "rhi/gpu_device.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace arp::renderer {

namespace detail {

inline bool validExtent(const rhi::Extent2D& extent)
{
    return extent.width > 0 && extent.height > 0;
}

inline rhi::Extent3D extent3D(const rhi::Extent2D& extent)
{
    return {extent.width, extent.height, 1u};
}

inline rhi::Extent2D extent2D(const rhi::Extent3D& extent)
{
    return {extent.width, extent.height};
}

inline bool hasSuffix(const std::pmr::string& value, const char* suffix)
{
    const std::pmr::string suffixText(suffix);
    return value.size() >= suffixText.size()
        && value.compare(value.size() - suffixText.size(), suffixText.size(), suffixText) == 0;
}

inline std::pmr::string replaceShaderExtension(const std::pmr::string& path, const char* suffix)
{
    const auto slash = path.find_last_of("/\\");
    const auto dot = path.find_last_of('.');
    const bool dotIsInFilename =
        dot != std::pmr::string::npos && (slash == std::pmr::string::npos || dot > slash);
    if (!dotIsInFilename) {
        return path + suffix;
    }
    return path.substr(0, dot) + suffix;
}

inline RendererResult validateFrameTarget(const RenderFrameContext& frame)
{
    if (!frame.frame.success) {
        return RendererResult{false, "frame is not drawable"};
    }
    if (!validExtent(frame.colorTarget.extent)) {
        return RendererResult{false, "color target extent is invalid"};
    }
    if (frame.colorTarget.format == rhi::TextureFormat::Unknown) {
        return RendererResult{false, "color target format is unknown"};
    }
    if (!frame.colorTexture) {
        return RendererResult{false, "renderer RHI pass requires an explicit color target texture"};
    }
    return RendererResult{true, ""};
}

inline RendererResult validateRendererCreateDesc(
    const std::pmr::string& shaderPath, rhi::TextureFormat colorFormat, const rhi::Extent2D& extent)
{
    if (colorFormat == rhi::TextureFormat::Unknown) {
        return RendererResult{false, "color format is unknown"};
    }
    if (!validExtent(extent)) {
        return RendererResult{false, "viewport extent is invalid"};
    }
    if (shaderPath.empty()) {
        return RendererResult{false, "shader package source path is missing"};
    }
    if (!hasSuffix(shaderPath, ".slang")) {
        return RendererResult{false, "renderer RHI shader package requires Slang source"};
    }
    return RendererResult{true, ""};
}

inline RendererResult validateGeneratedReflectionHydration(const rhi::ShaderPackageDesc& package)
{
    if (!package.generatedReflectionHydrated) {
        return RendererResult{
            false,
            "RHI-SHADER-REFLECTION-HYDRATION-FAILED",
        };
    }
    return RendererResult{true, ""};
}

inline rhi::ShaderEntrypoint shaderEntrypoint(
    const std::pmr::string& modulePath, const char* entryPoint, rhi::ShaderStage stage)
{
    return {
        modulePath,
        entryPoint,
        stage,
        rhi::ShaderSourceLanguage::Slang,
    };
}

inline rhi::ShaderPackageDesc makeGraphicsShaderPackage(const std::pmr::string& label,
    const std::pmr::string& modulePath,
    std::uint32_t pushConstantBytes)
{
    rhi::ShaderPackageDesc shaderPackage;
    shaderPackage.label = label + "ShaderPackage";
    shaderPackage.slangModulePath = modulePath;
    shaderPackage.entrypoints.push_back(
        shaderEntrypoint(modulePath, "vertexMain", rhi::ShaderStage::Vertex));
    shaderPackage.entrypoints.push_back(
        shaderEntrypoint(modulePath, "fragmentMain", rhi::ShaderStage::Fragment));
    shaderPackage.artifacts.spirvPath = replaceShaderExtension(modulePath, ".spv");
    shaderPackage.artifacts.mslPath = replaceShaderExtension(modulePath, ".metal");
    shaderPackage.artifacts.wgslPath = replaceShaderExtension(modulePath, ".wgsl");
    shaderPackage.artifacts.reflectionPath = replaceShaderExtension(modulePath, ".reflection.yaml");
    shaderPackage.reflection.pushConstantByteCount = pushConstantBytes;
    if (pushConstantBytes > 0) {
        shaderPackage.reflection.pushConstantContractVersion = "AtomicFullBlockPush/1";
        shaderPackage.reflection.pushConstantStages = "VS|FS";
    }
    if (!hydrateShaderPackageReflectionFromArtifact(shaderPackage)) {
        shaderPackage.generatedReflectionHydrated = false;
    }
    return shaderPackage;
}

inline rhi::RenderPipelineDesc makeRenderPipelineDesc(const std::pmr::string& label,
    const rhi::PipelineLayout& layout,
    const rhi::ShaderPackageDesc& shaderPackage,
    rhi::TextureFormat colorFormat,
    const rhi::Extent2D& extent)
{
    rhi::RenderPipelineDesc desc;
    desc.label = label;
    desc.layout = &layout;
    desc.shaderPackage = &shaderPackage;
    desc.vertexShader =
        shaderEntrypoint(shaderPackage.slangModulePath, "vertexMain", rhi::ShaderStage::Vertex);
    desc.fragmentShader =
        shaderEntrypoint(shaderPackage.slangModulePath, "fragmentMain", rhi::ShaderStage::Fragment);
    desc.colorFormat = colorFormat;
    desc.hasViewport = true;
    desc.viewport = {
        0.0f,
        0.0f,
        static_cast<float>(extent.width),
        static_cast<float>(extent.height),
        0.0f,
        1.0f,
    };
    desc.hasScissor = true;
    desc.scissor = {0, 0, extent.width, extent.height};
    return desc;
}

inline RendererResult submitEncodedFrame(
    rhi::GpuDevice& device, arp::core::UniquePtr<rhi::CommandEncoder> encoder)
{
    if (!encoder) {
        return RendererResult{false, "RHI command encoder creation failed"};
    }
    auto commandBuffer = encoder->finish();
    if (!commandBuffer) {
        return RendererResult{false, encoder->lastValidation().diagnostics};
    }
    const auto submitted = device.queue().submit(*commandBuffer);
    return RendererResult{submitted.success, submitted.diagnostics};
}

} // namespace detail

} // namespace arp::renderer
