#include "core/math/half_float.h"
#include "renderer/shader_package_reflection.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_native_resources.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

struct InstanceData {
    float offset[2];
    float color[4];
};

template <typename Result> void check(const Result& result)
{
    if (!result.success) {
        std::fprintf(stderr, "%s\n", result.diagnostics.c_str());
    }
    assert(result.success);
}

} // namespace

int main()
{
    using namespace arp;
    using namespace arp::rhi;
    using namespace arp::rhi_metal;

    constexpr std::uint32_t width = 4u;
    constexpr std::uint32_t height = 4u;
    constexpr std::uint32_t pitch = 256u;
    constexpr std::array<float, 6> vertices{{
        -1.0f,
        -1.0f,
        3.0f,
        -1.0f,
        -1.0f,
        3.0f,
    }};
    constexpr std::array<std::uint32_t, 3> indices{{0u, 1u, 2u}};
    constexpr std::array<InstanceData, 2> instances{{
        {{0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
    }};
    constexpr std::uint64_t targetBytes = width * height * 8u;
    constexpr std::uint64_t readbackBytes = 2u * pitch * height;

    MetalRhiDeviceDesc deviceDesc;
    deviceDesc.capabilityContract = semantics::nativeBackendCapabilityContract(Backend::Metal);
    deviceDesc.capabilityContract.queriedFromNativeDevice = true;
    deviceDesc.capabilityContract.queriedExtendedFeatures = true;
    deviceDesc.uploadStagingPageSizeBytes = 4096u;
    deviceDesc.uploadStagingMaxPageCount = 1u;
    deviceDesc.memoryBudgetBytes =
        sizeof(vertices) + sizeof(indices) + sizeof(instances) + targetBytes + readbackBytes;
    auto device = createMetalRhiDevice(deviceDesc);
    assert(device);

    auto layout = device->createPipelineLayout({});
    assert(layout);

    const core::pmr::String prefix = ARP_VERTEX_STREAM_SHADER_DIR "/vertex_stream_instancing";
    ShaderPackageDesc package;
    package.label = "VertexStreamInstancingFixture";
    package.slangModulePath = prefix + ".slang";
    package.entrypoints = {
        {package.slangModulePath, "vertexMain", ShaderStage::Vertex, ShaderSourceLanguage::Slang},
        {package.slangModulePath,
            "fragmentMain",
            ShaderStage::Fragment,
            ShaderSourceLanguage::Slang},
    };
    package.artifacts.spirvPath = prefix + ".spv";
    package.artifacts.mslPath = prefix + ".metal";
    package.artifacts.wgslPath = prefix + ".wgsl";
    package.artifacts.reflectionPath = prefix + ".reflection.yaml";
    assert(renderer::hydrateShaderPackageReflectionFromArtifact(package));
    check(validateShaderPackage(package));

    RenderPipelineDesc pipelineDesc;
    pipelineDesc.label = "TwoStreamInstancing";
    pipelineDesc.layout = layout.get();
    pipelineDesc.shaderPackage = &package;
    pipelineDesc.vertexShader = package.entrypoints[0];
    pipelineDesc.fragmentShader = package.entrypoints[1];
    pipelineDesc.colorFormat = TextureFormat::Rgba16Float;
    pipelineDesc.cullMode = CullMode::None;
    pipelineDesc.hasViewport = true;
    pipelineDesc.viewport = {
        0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    pipelineDesc.hasScissor = true;
    pipelineDesc.scissor = {0, 0, width, height};

    VertexBufferLayoutDesc perVertex;
    perVertex.slot = 0u;
    perVertex.strideBytes = sizeof(float) * 2u;
    perVertex.inputRate = VertexInputRate::Vertex;
    perVertex.attributes.push_back({0u, 0u, VertexFormat::Float32x2});
    pipelineDesc.vertexBuffers.push_back(perVertex);

    VertexBufferLayoutDesc perInstance;
    perInstance.slot = 1u;
    perInstance.strideBytes = sizeof(InstanceData);
    perInstance.inputRate = VertexInputRate::Instance;
    perInstance.attributes.push_back({1u, 0u, VertexFormat::Float32x2});
    perInstance.attributes.push_back({2u, sizeof(float) * 2u, VertexFormat::Float32x4});
    pipelineDesc.vertexBuffers.push_back(perInstance);

    const auto validation = device->validateRenderPipeline(pipelineDesc);
    if (!validation.success) {
        std::fprintf(stderr, "%s\n", validation.diagnostics.c_str());
    }
    assert(validation.success);
    auto pipeline = device->createRenderPipeline(pipelineDesc);
    assert(pipeline);

    auto vertexBuffer = device->createBuffer({"InstancedVertices",
        sizeof(vertices),
        ResourceUsage::VertexBuffer,
        MemoryUsage::CpuToGpu,
        ResourceState::ShaderRead});
    auto instanceBuffer = device->createBuffer({"InstancedData",
        sizeof(instances),
        ResourceUsage::VertexBuffer,
        MemoryUsage::CpuToGpu,
        ResourceState::ShaderRead});
    auto indexBuffer = device->createBuffer({"InstancedIndices",
        sizeof(indices),
        ResourceUsage::IndexBuffer,
        MemoryUsage::CpuToGpu,
        ResourceState::ShaderRead});
    auto target = device->createTexture({"InstancedOutput",
        {width, height, 1u},
        TextureFormat::Rgba16Float,
        TextureUsage::RenderAttachment | TextureUsage::CopySrc,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined});
    auto readback = device->createBuffer({"InstancedReadback",
        readbackBytes,
        ResourceUsage::CopyDst,
        MemoryUsage::GpuToCpu,
        ResourceState::CopyDst});
    assert(vertexBuffer && instanceBuffer && indexBuffer && target && readback);
    check(device->writeBuffer(*vertexBuffer, vertices.data(), sizeof(vertices)));
    check(device->writeBuffer(*instanceBuffer, instances.data(), sizeof(instances)));
    check(device->writeBuffer(*indexBuffer, indices.data(), sizeof(indices)));

    const TextureSubresourceRange range{0u, 1u, 0u, 1u, TextureAspect::Color};
    auto encoder = device->createCommandEncoder({"TwoStreamInstancing"});
    assert(encoder);
    for (std::uint32_t firstInstance = 0u; firstInstance < 2u; ++firstInstance) {
        check(encoder->transition(*target,
            range,
            firstInstance == 0u ? ResourceState::Undefined : ResourceState::CopySrc,
            ResourceState::ColorAttachment));
        RenderPassDesc passDesc;
        RenderPassColorAttachmentDesc color;
        color.target = target->id();
        color.format = TextureFormat::Rgba16Float;
        color.loadOp = LoadOp::Clear;
        color.storeOp = StoreOp::Store;
        color.clearColor = {0.0f, 0.0f, 0.0f, 0.0f};
        passDesc.colorAttachments.push_back(color);
        auto pass = encoder->beginRenderPass(passDesc);
        assert(pass);
        check(pass->setPipeline(*pipeline));
        check(pass->bindVertexBuffer(0u, *vertexBuffer));
        check(pass->bindVertexBuffer(1u, *instanceBuffer));
        check(pass->bindIndexBuffer(*indexBuffer, IndexFormat::Uint32));
        check(pass->drawIndexed(3u, 1u, 0u, 0, firstInstance));
        check(pass->end());
        check(encoder->transition(
            *target, range, ResourceState::ColorAttachment, ResourceState::CopySrc));
        auto copy = encoder->beginTransferPass({"InstancedReadbackCopy"});
        assert(copy);
        check(copy->copyTextureToBuffer({target->id(),
            readback->id(),
            range,
            firstInstance * pitch * height,
            pitch,
            height,
            target->desc().extent}));
        check(copy->end());
    }

    auto commands = encoder->finish();
    assert(commands);
    check(device->queue().submit(*commands));
    device->waitIdle();

    std::array<std::uint8_t, readbackBytes> bytes{};
    core::pmr::String diagnostics;
    assert(readMetalRhiNativeBuffer(metalRhiNativePayloadFor(*readback),
        bytes.data(),
        bytes.size(),
        0u,
        bytes.size(),
        diagnostics));
    constexpr std::array<std::array<float, 4>, 2> expected{{
        {{1.0f, 0.0f, 0.0f, 1.0f}},
        {{0.0f, 1.0f, 0.0f, 1.0f}},
    }};
    for (std::uint32_t draw = 0u; draw < 2u; ++draw) {
        for (std::uint32_t y = 0u; y < height; ++y) {
            for (std::uint32_t x = 0u; x < width; ++x) {
                const auto pixelOffset = draw * pitch * height + y * pitch + x * 8u;
                for (std::uint32_t channel = 0u; channel < 4u; ++channel) {
                    std::uint16_t bits = 0u;
                    std::memcpy(
                        &bits, bytes.data() + pixelOffset + channel * sizeof(bits), sizeof(bits));
                    const float actual = core::halfToFloat(bits);
                    assert(std::isfinite(actual));
                    assert(std::abs(actual - expected[draw][channel]) <= 0.001f);
                }
            }
        }
    }

    commands.reset();
    encoder.reset();
    pipeline.reset();
    layout.reset();
    vertexBuffer.reset();
    instanceBuffer.reset();
    indexBuffer.reset();
    target.reset();
    readback.reset();
    assert(device->memoryStats().allocatedBytes == 0u);
    std::puts("VertexStreamBinding/1 native Metal two-stream indexed instancing, "
              "firstInstance 0/1 pixels and retirement: PASS");
}
