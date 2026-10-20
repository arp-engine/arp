#pragma once

#include "rhi/backends/metal/metal_native_pipelines.h"
#include "rhi/frontend/bindless_shader_package.h"

#include <cassert>

namespace arp::test {

// A retained-source pipeline fixture shared by preparation and mixed-command
// tests. Deliberately absent artifact paths prove construction uses sealed bytes.
class MetalGraphicsPipelineFixture final {
public:
    MetalGraphicsPipelineFixture(const rhi::BindlessShaderPackage& package,
        std::string_view vertexEntry,
        std::string_view fragmentEntry)
        : package_(package), vertexEntry_(vertexEntry), fragmentEntry_(fragmentEntry)
    {
        core::pmr::String diagnostic;
        assert(rhi::appendBindlessShaderEntry(
            package, vertexEntry, rhi::ShaderStage::Vertex, interface_, diagnostic));
        if (!fragmentEntry.empty())
            assert(rhi::appendBindlessShaderEntry(
                package, fragmentEntry, rhi::ShaderStage::Fragment, interface_, diagnostic));
        interface_.artifacts.mslPath = "missing-retained-source-test.metal";
        desc_.label = "RetainedMetalGraphicsConstruction";
        desc_.shaderPackage = &interface_;
        desc_.vertexShader = interface_.entrypoints[0];
        if (!fragmentEntry.empty()) {
            desc_.fragmentShader = interface_.entrypoints[1];
            desc_.colorFormat = rhi::TextureFormat::Rgba8Unorm;
        } else {
            desc_.colorFormat = rhi::TextureFormat::Unknown;
            desc_.depthStencil.format = rhi::TextureFormat::D32Float;
            desc_.depthStencil.depthTestEnabled = true;
            desc_.depthStencil.depthWriteEnabled = true;
        }
        desc_.dynamicViewportScissor = true;
        const auto state = rhi::validateRenderPipelineState(desc_);
        assert(state.success);
        report_.backend = rhi::Backend::Metal;
        report_.colorTargetCount = state.colorTargetCount;
        report_.colorFormats = state.colorFormats;
        report_.depthStencilFormat = state.depthStencilFormat;
        report_.rasterStateValidated = state.rasterStateValidated;
        report_.stencilStateValidated = state.stencilStateValidated;
        streams_.success = true;
    }

    std::string_view source(std::string_view entry) const
    {
        if (entry.empty())
            return {};
        for (const auto& module : package_.modules()) {
            if (module.entry != entry)
                continue;
            const auto& bytes = package_.files()[module.metalFile].bytes;
            return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
        }
        assert(false);
        return {};
    }

    std::shared_ptr<rhi_metal::MetalRhiNativePipelinePayload> create() const
    {
        return createWithSources(source(vertexEntry_), source(fragmentEntry_));
    }

    std::shared_ptr<rhi_metal::MetalRhiNativePipelinePayload> createWithSources(
        std::string_view vertex, std::string_view fragment) const
    {
        return rhi_metal::createMetalRhiNativeRenderPipelinePayload(
            desc_, report_, streams_, vertex, fragment);
    }

private:
    const rhi::BindlessShaderPackage& package_;
    std::string_view vertexEntry_, fragmentEntry_;
    rhi::ShaderPackageDesc interface_;
    rhi::RenderPipelineDesc desc_;
    rhi::PipelineValidationReport report_;
    rhi::semantics::MetalVertexStreamMapping streams_;
};

} // namespace arp::test
