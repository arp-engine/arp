#include "rhi/semantics/webgl_prepared_commands.h"

#include <array>
#include <cassert>
#include <cstring>

int main()
{
    using namespace arp;
    using namespace rhi::semantics;
    PreparedGraphicsCommandsRequest request{
        4, true, 8, 4096, 32, {1, true, 14, 16, 1024, 65536, 64}, 3, 2, 3, 8};
    const std::array<PreparedGraphicsCatalogShape, 3> shapes{
        {{1, 1, 1, true}, {1, 1, 5, false}, {2, 0, 0, false}}};
    auto result = PreparedGraphicsCatalogContract::assess(request, shapes);
    assert(std::strcmp(result.diagnostic, "RHI-ORDERED-GRAPHICS-CATALOG-TOPOLOGY") == 0);
    request.contractVersion = 5;
    assert(PreparedGraphicsCommandsContract::validate(request));
    request.maxVertexStreams = 2;
    request.maxDescriptorSets = 1;
    request.maxSampledTextures = 5;
    result = PreparedGraphicsCatalogContract::assess(request, shapes);
    assert(!result.diagnostic && result.vertexStreams == 2 && result.descriptorSets == 1
        && result.sampledTextures == 5);
    for (auto backend : {rhi::Backend::Null,
             rhi::Backend::Vulkan,
             rhi::Backend::Metal,
             rhi::Backend::WebGPU,
             rhi::Backend::WebGL})
        assert((WebGlPreparedCommandsContract::validate(backend, request) == nullptr)
            == (backend == rhi::Backend::WebGL));
    assert(!PreparedGraphicsCommandsContract::pipelineSwitch(request, false, false));
    assert(PreparedGraphicsCommandsContract::pipelineSwitch(request, false, true));
    assert(!PreparedGraphicsCommandsContract::pipelineSwitch(request, true, true));
    for (auto* capacity :
        {&request.maxVertexStreams, &request.maxDescriptorSets, &request.maxSampledTextures}) {
        const auto original = *capacity;
        *capacity = 0;
        assert(PreparedGraphicsCommandsContract::validate(request));
        *capacity = original;
    }
    request.maxSampledTextures = 4;
    assert(PreparedGraphicsCatalogContract::assess(request, shapes).diagnostic);
    request.maxSampledTextures = 5;
    request.maxVertexStreams = 1;
    assert(PreparedGraphicsCatalogContract::assess(request, shapes).diagnostic);
    request.maxVertexStreams = 2;
    assert(PreparedGraphicsCatalogContract::assess(request, {}).diagnostic);
    request.contractVersion = 4;
    assert(PreparedGraphicsCommandsContract::validate(request));
    request.contractVersion = 6;
    assert(PreparedGraphicsCommandsContract::validate(request));
}
