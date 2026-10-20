#include "rhi/backends/metal/metal_native_format.h"
#include "rhi/semantics/native_pipeline_state_lowering.h"

#include <cassert>

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi::semantics;
    using arp::rhi_metal::metalVertexFormat;
    assert(metalVertexFormat(VertexFormat::Float32x1) == MTLVertexFormatFloat);
    assert(metalVertexFormat(VertexFormat::Float32x2) == MTLVertexFormatFloat2);
    assert(metalVertexFormat(VertexFormat::Float32x3) == MTLVertexFormatFloat3);
    assert(metalVertexFormat(VertexFormat::Float32x4) == MTLVertexFormatFloat4);
    assert(metalVertexFormat(static_cast<VertexFormat>(255)) == MTLVertexFormatInvalid);

    for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        RenderPipelineDesc desc;
        desc.colorFormat = TextureFormat::Bgra8Unorm;
        VertexBufferLayoutDesc layout;
        layout.slot = 0u;
        layout.strideBytes = sizeof(float);
        layout.attributes.push_back({0u, 0u, VertexFormat::Float32x1});
        desc.vertexBuffers.push_back(layout);
        const auto contract = nativeBackendCapabilityContract(backend);
        assert(lowerRenderPipelineState(contract, desc).success);
        desc.vertexBuffers[0].strideBytes = 0u;
        assert(!lowerRenderPipelineState(contract, desc).success);
    }
}
