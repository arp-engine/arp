#include "core/memory/cpu_allocator.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/semantics/native_pipeline_state_lowering.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

namespace {

arp::rhi::ShaderPackageDesc makePipelineStateShaderPackage()
{
    arp::rhi::ShaderPackageDesc shaderPackage;
    shaderPackage.label = "PipelineStateShaderPackage";
    shaderPackage.slangModulePath = "pipeline_state.slang";
    shaderPackage.entrypoints.push_back(
        {"pipeline_state.slang", "vsMain", arp::rhi::ShaderStage::Vertex});
    shaderPackage.entrypoints.push_back(
        {"pipeline_state.slang", "fsMain", arp::rhi::ShaderStage::Fragment});
    shaderPackage.artifacts.spirvPath = "pipeline_state.spv";
    shaderPackage.artifacts.mslPath = "pipeline_state.metal";
    shaderPackage.artifacts.reflectionPath = "pipeline_state.reflection.yaml";
    shaderPackage.reflection.pushConstantByteCount = 64;
    // Track A P5: validateShaderPackage requires the push-constant lowering
    // contract version and stage visibility whenever pushConstantByteCount > 0.
    shaderPackage.reflection.pushConstantContractVersion = "AtomicFullBlockPush/1";
    shaderPackage.reflection.pushConstantStages = "VS|FS|CS";
    return shaderPackage;
}

arp::rhi::RenderPipelineDesc makeParityPipelineDesc(
    const arp::rhi::PipelineLayout& layout, const arp::rhi::ShaderPackageDesc& shaderPackage)
{
    using namespace arp::rhi;

    RenderPipelineDesc desc;
    desc.label = "PipelineStateParity";
    desc.layout = &layout;
    desc.shaderPackage = &shaderPackage;
    desc.vertexShader = {"pipeline_state.slang", "vsMain", ShaderStage::Vertex};
    desc.fragmentShader = {"pipeline_state.slang", "fsMain", ShaderStage::Fragment};
    desc.colorFormat = TextureFormat::Bgra8Unorm;
    desc.colorTargets = {
        {
            TextureFormat::Bgra8Unorm,
            true,
            {BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha, BlendOp::Add},
            {BlendFactor::One, BlendFactor::OneMinusSrcAlpha, BlendOp::Add},
            ColorWriteMask::All,
            true,
        },
        {
            // Commercial TAA scene MRT contract: signed velocity.
            TextureFormat::Rg16Float,
            false,
            {},
            {},
            ColorWriteMask::Red | ColorWriteMask::Green,
            true,
        },
        {
            // Commercial TAA scene MRT contract: reactive mask.
            TextureFormat::R8Unorm,
            false,
            {},
            {},
            ColorWriteMask::Red,
            true,
        },
    };
    desc.depthFormat = TextureFormat::Depth24Stencil8;
    desc.depthStencil.format = TextureFormat::Depth24Stencil8;
    desc.depthStencil.depthTestEnabled = true;
    desc.depthStencil.depthWriteEnabled = true;
    desc.depthStencil.depthCompare = CompareOp::LessEqual;
    desc.depthStencil.stencilEnabled = true;
    desc.depthStencil.front.failOp = StencilOp::Keep;
    desc.depthStencil.front.depthFailOp = StencilOp::Replace;
    desc.depthStencil.front.passOp = StencilOp::Replace;
    desc.depthStencil.front.compare = CompareOp::Always;
    desc.depthStencil.back = desc.depthStencil.front;
    desc.depthStencil.stencilReadMask = 0xffu;
    desc.depthStencil.stencilWriteMask = 0x0fu;
    desc.depthStencil.depthBiasEnabled = true;
    desc.depthStencil.depthBiasConstantFactor = 1.25f;
    desc.depthStencil.depthBiasSlopeFactor = 0.75f;
    desc.cullMode = CullMode::Back;
    desc.frontFace = FrontFace::Clockwise;
    desc.topology = PrimitiveTopology::TriangleStrip;
    desc.multisample.sampleCount = 4;
    desc.multisample.sampleMask = 0x0fu;
    desc.multisample.alphaToCoverageEnabled = true;
    desc.hasViewport = true;
    desc.viewport = {0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f};
    desc.hasScissor = true;
    desc.scissor = {4, 8, 640, 360};
    return desc;
}

arp::rhi::RenderPipelineDesc makeDepthOnlyShadowPipelineDesc(
    const arp::rhi::PipelineLayout& layout, const arp::rhi::ShaderPackageDesc& shaderPackage)
{
    using namespace arp::rhi;

    RenderPipelineDesc desc;
    desc.label = "DepthOnlyShadowCaster";
    desc.layout = &layout;
    desc.shaderPackage = &shaderPackage;
    desc.vertexShader = {"pipeline_state.slang", "vsMain", ShaderStage::Vertex};
    desc.fragmentShader = {"pipeline_state.slang", "fsMain", ShaderStage::Fragment};
    desc.colorFormat = TextureFormat::Unknown;
    desc.colorTargets.clear();
    desc.depthFormat = TextureFormat::D32Float;
    desc.depthStencil.format = TextureFormat::D32Float;
    desc.depthStencil.depthTestEnabled = true;
    desc.depthStencil.depthWriteEnabled = true;
    desc.depthStencil.depthCompare = CompareOp::LessEqual;
    desc.depthStencil.depthBiasEnabled = true;
    desc.depthStencil.depthBiasConstantFactor = 2.0f;
    desc.depthStencil.depthBiasSlopeFactor = 1.25f;
    desc.cullMode = CullMode::Back;
    desc.frontFace = FrontFace::CounterClockwise;
    desc.topology = PrimitiveTopology::TriangleList;
    desc.hasViewport = true;
    desc.viewport = {0.0f, 0.0f, 2048.0f, 2048.0f, 0.0f, 1.0f};
    desc.hasScissor = true;
    desc.scissor = {0, 0, 2048, 2048};
    return desc;
}

bool hasSemantic(const arp::rhi::semantics::NativePipelineStateLoweringDecision& decision,
    arp::rhi::semantics::NativePipelineStateSemantic semantic)
{
    return arp::rhi::semantics::hasNativePipelineStateSemantic(decision, semantic);
}

bool hasComputeSemantic(
    const arp::rhi::semantics::NativeComputePipelineStateLoweringDecision& decision,
    arp::rhi::semantics::NativeComputePipelineStateSemantic semantic)
{
    return arp::rhi::semantics::hasNativeComputePipelineStateSemantic(decision, semantic);
}

arp::rhi::ComputePipelineDesc makeParityComputePipelineDesc(
    const arp::rhi::PipelineLayout& layout, const arp::rhi::ShaderPackageDesc& shaderPackage)
{
    using namespace arp::rhi;
    ComputePipelineDesc desc;
    desc.label = "PipelineStateParityCompute";
    desc.layout = &layout;
    desc.shaderPackage = &shaderPackage;
    desc.computeShader = {"pipeline_state.slang", "csMain", ShaderStage::Compute};
    return desc;
}

arp::rhi::ShaderPackageDesc makeComputePipelineStateShaderPackage()
{
    arp::rhi::ShaderPackageDesc shaderPackage;
    shaderPackage.label = "PipelineStateComputeShaderPackage";
    shaderPackage.slangModulePath = "pipeline_state.slang";
    shaderPackage.entrypoints.push_back(
        {"pipeline_state.slang", "csMain", arp::rhi::ShaderStage::Compute});
    shaderPackage.artifacts.spirvPath = "pipeline_state.spv";
    shaderPackage.artifacts.mslPath = "pipeline_state.metal";
    shaderPackage.artifacts.reflectionPath = "pipeline_state.reflection.yaml";
    shaderPackage.reflection.pushConstantByteCount = 64;
    // Track A P5: validateShaderPackage requires the push-constant lowering
    // contract version and stage visibility whenever pushConstantByteCount > 0.
    shaderPackage.reflection.pushConstantContractVersion = "AtomicFullBlockPush/1";
    shaderPackage.reflection.pushConstantStages = "VS|FS|CS";
    return shaderPackage;
}

std::pmr::string readSourceFile(const char* relativePath)
{
    // NOTE: this deliberately avoids std::filesystem and std::ifstream.
    // The test binary links against libstdc++-6.dll, and at runtime the
    // Windows loader resolves it from Git for Windows' bundled MinGW
    // (C:/Program Files/Git/mingw64/bin) when that directory precedes the
    // build toolchain on PATH. That older libstdc++-6.dll is missing the
    // std::codecvt / __codecvt_utf8_utf16_base vtable symbols that
    // std::filesystem pulls in for wchar_t path conversion, which makes the
    // loader abort the process with STATUS_ENTRYPOINT_NOT_FOUND (0xC0000139)
    // before main() runs. Using fopen + manual path string concatenation
    // keeps the dependency surface identical to the rest of the suite
    // (which links the same libstdc++ and runs fine) and avoids the
    // filesystem codecvt symbols entirely.
    auto tryRead = [](const std::pmr::string& path) -> std::pmr::string {
        std::FILE* fp = std::fopen(path.c_str(), "rb");
        if (!fp)
            return {};
        std::fseek(fp, 0, SEEK_END);
        const long size = std::ftell(fp);
        std::fseek(fp, 0, SEEK_SET);
        if (size <= 0) {
            std::fclose(fp);
            return {};
        }
        std::pmr::string contents;
        contents.resize(static_cast<std::size_t>(size));
        const std::size_t read = std::fread(contents.data(), 1, contents.size(), fp);
        std::fclose(fp);
        if (read != contents.size())
            return {};
        return contents;
    };

#ifdef ARP_SOURCE_DIR
    {
        std::pmr::string rooted(&arp::core::defaultCpuMemoryResource());
        rooted = ARP_SOURCE_DIR;
        rooted += '/';
        rooted += relativePath;
        if (auto contents = tryRead(rooted); !contents.empty()) {
            return contents;
        }
    }
#endif
    // Fall back to an upward-walking probe from the current working
    // directory for manual / legacy invocations without ARP_SOURCE_DIR.
    char cwdBuf[4096];
    std::pmr::string cwd(&arp::core::defaultCpuMemoryResource());
    if (::getcwd(cwdBuf, sizeof(cwdBuf))) {
        cwd = cwdBuf;
    }
    for (std::uint32_t depth = 0; depth < 8 && !cwd.empty(); ++depth) {
        std::pmr::string candidate = cwd;
        candidate += '/';
        candidate += relativePath;
        if (auto contents = tryRead(candidate); !contents.empty()) {
            return contents;
        }
        // Walk up one directory component.
        const auto slash = cwd.find_last_of('/');
        const auto backslash = cwd.find_last_of('\\');
        std::size_t cut = std::pmr::string::npos;
        if (slash != std::pmr::string::npos && backslash != std::pmr::string::npos)
            cut = std::max(slash, backslash);
        else if (slash != std::pmr::string::npos)
            cut = slash;
        else if (backslash != std::pmr::string::npos)
            cut = backslash;
        if (cut == std::pmr::string::npos)
            break;
        cwd = cwd.substr(0, cut);
        if (cwd.empty())
            break;
    }
    assert(false && "source file fixture not found");
    return {};
}

void assertVulkanNativeDepthStateIsLowered()
{
    const auto pipelineSource =
        readSourceFile("src/rhi/backends/vulkan/vulkan_native_pipelines.cpp");
    assert(pipelineSource.find("VkPipelineDepthStencilStateCreateInfo depthStencil")
        != std::pmr::string::npos);
    assert(pipelineSource.find("depthStencil.depthTestEnable = desc.depthStencil.depthTestEnabled")
        != std::pmr::string::npos);
    assert(
        pipelineSource.find("depthStencil.depthWriteEnable = desc.depthStencil.depthWriteEnabled")
        != std::pmr::string::npos);
    assert(pipelineSource.find(
               "depthStencil.depthCompareOp = compareOpFor(desc.depthStencil.depthCompare)")
        != std::pmr::string::npos);
    assert(pipelineSource.find("pipelineInfo.pDepthStencilState = &depthStencil")
        != std::pmr::string::npos);

    const auto commandsSource =
        readSourceFile("src/rhi/backends/vulkan/vulkan_native_graphics_commands.cpp");
    assert(commandsSource.find("desc.hasDepthStencilAttachment") != std::pmr::string::npos);
    assert(commandsSource.find("subpass.pDepthStencilAttachment = &depthAttachmentRef")
        != std::pmr::string::npos);
    assert(commandsSource.find("VK_IMAGE_ASPECT_DEPTH_BIT") != std::pmr::string::npos);
    assert(commandsSource.find("framebufferInfo.attachmentCount = attachmentCount")
        != std::pmr::string::npos);
}

void assertWebGpuNativeDepthStateIsLowered()
{
    const auto pipelineSource =
        readSourceFile("src/rhi/backends/webgpu/webgpu_native_resources.cpp");
    assert(pipelineSource.find("WGPUDepthStencilState depthStencil") != std::pmr::string::npos);
    assert(
        pipelineSource.find("depthStencil.depthWriteEnabled = desc.depthStencil.depthWriteEnabled")
        != std::pmr::string::npos);
    assert(pipelineSource.find(
               "depthStencil.depthCompare = nativeCompareFunction(desc.depthStencil.depthCompare)")
        != std::pmr::string::npos);
    assert(
        pipelineSource.find("pipelineDesc.depthStencil = &depthStencil") != std::pmr::string::npos);
    // depthBias three-piece + stencil front/back must be lowered into the
    // WGPUDepthStencilState, not silently dropped. Previously the WebGPU
    // backend only set format/depthWriteEnabled/depthCompare.
    assert(pipelineSource.find("depthStencil.depthBias =") != std::pmr::string::npos);
    assert(pipelineSource.find("depthStencil.depthBiasSlopeScale =") != std::pmr::string::npos);
    assert(pipelineSource.find("depthStencil.depthBiasClamp =") != std::pmr::string::npos);
    assert(pipelineSource.find("depthStencil.stencilFront = nativeStencilFaceState")
        != std::pmr::string::npos);
    assert(pipelineSource.find("depthStencil.stencilBack = nativeStencilFaceState")
        != std::pmr::string::npos);
    assert(pipelineSource.find("depthStencil.stencilReadMask =") != std::pmr::string::npos);
    assert(pipelineSource.find("depthStencil.stencilWriteMask =") != std::pmr::string::npos);

    const auto commandsSource =
        readSourceFile("src/rhi/backends/webgpu/webgpu_native_commands.cpp");
    assert(commandsSource.find("const WebGpuNativeTexturePayload* depthTarget")
        != std::pmr::string::npos);
    assert(commandsSource.find("renderDesc.depthStencilAttachment = &depthStencilAttachment")
        != std::pmr::string::npos);
    assert(commandsSource.find("WGPUTextureAspect_DepthOnly") != std::pmr::string::npos);
}

void assertMetalNativeDepthStateIsDiagnosed()
{
    const auto pipelineHeader = readSourceFile("src/rhi/backends/metal/metal_native_pipelines.h");
    assert(pipelineHeader.find("bool depthStencilStateRecorded = false") != std::pmr::string::npos);
    // Metal payload must carry the L2 depthBias + stencil state so the
    // command encoder can lower them faithfully. Previously the payload
    // dropped depthBias and stencil front/back entirely.
    assert(pipelineHeader.find("bool depthBiasEnabled = false") != std::pmr::string::npos);
    assert(pipelineHeader.find("float depthBiasConstantFactor = 0.0f") != std::pmr::string::npos);
    assert(pipelineHeader.find("float depthBiasClamp = 0.0f") != std::pmr::string::npos);
    assert(pipelineHeader.find("float depthBiasSlopeFactor = 0.0f") != std::pmr::string::npos);
    assert(pipelineHeader.find("bool stencilEnabled = false") != std::pmr::string::npos);
    assert(
        pipelineHeader.find("rhi::StencilFaceState stencilFront = {}") != std::pmr::string::npos);
    assert(pipelineHeader.find("rhi::StencilFaceState stencilBack = {}") != std::pmr::string::npos);

    const auto commandHeader = readSourceFile("src/rhi/backends/metal/metal_native_commands.h");
    assert(commandHeader.find("bool nativeDepthStencilAttachmentRecorded = false")
        != std::pmr::string::npos);
    assert(
        commandHeader.find("bool nativeDepthStencilStateBound = false") != std::pmr::string::npos);
    // The encoder records that a depthBias-enabled pipeline was bound, so
    // parity tests can assert the state reached the payload instead of being
    // silently dropped.
    assert(commandHeader.find("bool nativeDepthBiasSet = false") != std::pmr::string::npos);
    assert(commandHeader.find("bool recordRenderPassBegin(const rhi::RenderPassDesc& desc")
        != std::pmr::string::npos);
    assert(commandHeader.find("const MetalRhiNativePipelinePayload* pipeline")
        != std::pmr::string::npos);

    // Both the .mm (Apple) and _stub.cpp (non-Apple) payloads must mirror the
    // L2 depthBias + stencil state so the payload surface is uniform.
    const auto metalPipelineSource =
        readSourceFile("src/rhi/backends/metal/metal_native_pipelines.mm");
    assert(
        metalPipelineSource.find("payload->depthBiasEnabled = desc.depthStencil.depthBiasEnabled")
        != std::pmr::string::npos);
    assert(metalPipelineSource.find("payload->stencilFront = desc.depthStencil.front")
        != std::pmr::string::npos);
    const auto metalStubSource =
        readSourceFile("src/rhi/backends/metal/metal_native_pipelines_stub.cpp");
    assert(metalStubSource.find("payload->depthBiasEnabled = desc.depthStencil.depthBiasEnabled")
        != std::pmr::string::npos);
    assert(metalStubSource.find("payload->stencilFront = desc.depthStencil.front")
        != std::pmr::string::npos);
}

void assertNativeDepthOnlyRenderPassLoweringIsSourceVisible()
{
    const auto vulkanCommands =
        readSourceFile("src/rhi/backends/vulkan/vulkan_native_graphics_commands.cpp");
    assert(
        vulkanCommands.find(
            "const bool depthOnlyRenderPass = desc.colorAttachments.empty() && desc.hasDepthStencilAttachment")
        != std::pmr::string::npos);
    assert(
        vulkanCommands.find("subpass.colorAttachmentCount = colorCount") != std::pmr::string::npos);
    assert(vulkanCommands.find("colorTargets.size() != desc.colorAttachments.size()")
        != std::pmr::string::npos);
    assert(vulkanCommands.find(
               "Vulkan RHI native depth-only render pass requires a tracked depth target")
        != std::pmr::string::npos);

    const auto webGpuCommands =
        readSourceFile("src/rhi/backends/webgpu/webgpu_native_commands.cpp");
    assert(
        webGpuCommands.find(
            "const bool depthOnlyRenderPass = desc.colorAttachments.empty() && desc.hasDepthStencilAttachment")
        != std::pmr::string::npos);
    assert(
        webGpuCommands.find(
            "renderDesc.colorAttachmentCount = depthOnlyRenderPass ? 0u : static_cast<std::uint32_t>(colorAttachments.size())")
        != std::pmr::string::npos);
    assert(webGpuCommands.find("colorTargets.size() != desc.colorAttachments.size()")
        != std::pmr::string::npos);
    assert(webGpuCommands.find(
               "WebGPU RHI native depth-only render pass requires a compatible depth target")
        != std::pmr::string::npos);

    const auto metalCommands = readSourceFile("src/rhi/backends/metal/metal_backend_commands.cpp");
    assert(
        metalCommands.find(
            "const bool depthOnlyRenderPass = desc.colorAttachments.empty() && desc.hasDepthStencilAttachment")
        != std::pmr::string::npos);
    assert(metalCommands.find("Metal RHI render pass requires color or depth attachment")
        != std::pmr::string::npos);

    const auto metalPipelines =
        readSourceFile("src/rhi/backends/metal/metal_native_pipelines_stub.cpp");
    assert(metalPipelines.find("payload->depthOnlyRenderPipeline = report.colorTargetCount == 0u")
        != std::pmr::string::npos);
}

void assertTaaMrtFormatLoweringIsSourceVisible()
{
    // The scene MRT must pass through texture creation and native pipeline
    // lowering on every backend.  These source-visible assertions complement
    // the Null L2 validation in main() below; they intentionally name both
    // sides of each native mapping so a partial backend-only fix cannot pass.
    const auto vulkanResources =
        readSourceFile("src/rhi/backends/vulkan/vulkan_native_resources.cpp");
    const auto vulkanPipelines =
        readSourceFile("src/rhi/backends/vulkan/vulkan_native_pipelines.cpp");
    assert(vulkanResources.find("TextureFormat::R8Unorm") != std::pmr::string::npos);
    assert(vulkanResources.find("VK_FORMAT_R8_UNORM") != std::pmr::string::npos);
    assert(vulkanResources.find("TextureFormat::Rg16Float") != std::pmr::string::npos);
    assert(vulkanResources.find("VK_FORMAT_R16G16_SFLOAT") != std::pmr::string::npos);
    assert(vulkanPipelines.find("TextureFormat::R8Unorm") != std::pmr::string::npos);
    assert(vulkanPipelines.find("TextureFormat::Rg16Float") != std::pmr::string::npos);

    const auto webGpuResources =
        readSourceFile("src/rhi/backends/webgpu/webgpu_native_resources.cpp");
    assert(webGpuResources.find("TextureFormat::R8Unorm") != std::pmr::string::npos);
    assert(webGpuResources.find("WGPUTextureFormat_R8Unorm") != std::pmr::string::npos);
    assert(webGpuResources.find("TextureFormat::Rg16Float") != std::pmr::string::npos);
    assert(webGpuResources.find("WGPUTextureFormat_RG16Float") != std::pmr::string::npos);

    const auto metalResources = readSourceFile("src/rhi/backends/metal/metal_native_resources.mm");
    assert(metalResources.find("TextureFormat::R8Unorm") != std::pmr::string::npos);
    assert(metalResources.find("MTLPixelFormatR8Unorm") != std::pmr::string::npos);
    assert(metalResources.find("TextureFormat::Rg16Float") != std::pmr::string::npos);
    assert(metalResources.find("MTLPixelFormatRG16Float") != std::pmr::string::npos);

    // Water reactive uses BlendOp::Max plus a red-only write mask. Metal must
    // lower the exact L2 attachment state instead of retaining only format.
    const auto metalPipelines = readSourceFile("src/rhi/backends/metal/metal_native_pipelines.mm");
    assert(metalPipelines.find("MTLBlendOperation metalBlendOperation") != std::pmr::string::npos);
    assert(metalPipelines.find("case rhi::BlendOp::Max: return MTLBlendOperationMax;")
        != std::pmr::string::npos);
    assert(metalPipelines.find("attachment.blendingEnabled = target.blendEnabled")
        != std::pmr::string::npos);
    assert(metalPipelines.find(
               "attachment.rgbBlendOperation = metalBlendOperation(target.colorBlend.operation)")
        != std::pmr::string::npos);
    assert(metalPipelines.find("attachment.writeMask = metalColorWriteMask(target.writeMask)")
        != std::pmr::string::npos);

    const auto webGpuPipelines =
        readSourceFile("src/rhi/backends/webgpu/webgpu_native_resources.cpp");
    assert(webGpuPipelines.find("case rhi::BlendOp::Max:") != std::pmr::string::npos);
    assert(webGpuPipelines.find("return WGPUBlendOperation_Max;") != std::pmr::string::npos);

    const auto vulkanPipelinesForBlend =
        readSourceFile("src/rhi/backends/vulkan/vulkan_native_pipelines.cpp");
    assert(vulkanPipelinesForBlend.find("case rhi::BlendOp::Max:") != std::pmr::string::npos);
    assert(vulkanPipelinesForBlend.find("return VK_BLEND_OP_MAX;") != std::pmr::string::npos);
}

} // namespace

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi::semantics;

    auto device = createNullDevice();
    PipelineLayoutDesc layoutDesc;
    layoutDesc.pushConstantRanges.push_back({ShaderStage::Vertex | ShaderStage::Fragment, 0, 64});
    auto layout = device->createPipelineLayout(layoutDesc);
    assert(layout);
    auto shaderPackage = makePipelineStateShaderPackage();
    assert(validateShaderPackage(shaderPackage).success);

    auto parityDesc = makeParityPipelineDesc(*layout, shaderPackage);
    const auto stableKey = stableRenderPipelineStateKey(parityDesc);
    assert(stableKey != 0u);
    assert(stableRenderPipelineStateKey(parityDesc) == stableKey);

    auto changedWriteMask = parityDesc;
    changedWriteMask.colorTargets[1].writeMask = ColorWriteMask::All;
    assert(stableRenderPipelineStateKey(changedWriteMask) != stableKey);

    auto changedCullMode = parityDesc;
    changedCullMode.cullMode = CullMode::None;
    assert(stableRenderPipelineStateKey(changedCullMode) != stableKey);

    auto changedFrontFace = parityDesc;
    changedFrontFace.frontFace = FrontFace::CounterClockwise;
    assert(stableRenderPipelineStateKey(changedFrontFace) != stableKey);

    const auto parityReport = device->validateRenderPipeline(parityDesc);
    assert(parityReport.success);
    assert(parityReport.colorTargetCount == 3u);
    assert(parityReport.blendStateValidated);
    assert(parityReport.colorWriteMaskValidated);
    assert(parityReport.stencilStateValidated);
    assert(parityReport.depthBiasStateValidated);
    assert(parityReport.rasterStateValidated);
    assert(parityReport.topologyValidated);
    assert(parityReport.msaaStateValidated);
    assert(parityReport.resolveStateValidated);
    assert(parityReport.viewportScissorStateValidated);
    assert(parityReport.pipelineCacheKey == stableKey);
    assert(parityReport.pipelineCacheKeyStable);
    auto pipeline = device->createRenderPipeline(parityDesc);
    assert(pipeline);
    assert(pipeline->validationReport().pipelineCacheKey == stableKey);
    assert(!pipeline->nativeHandleExposedToUpperLayer());

    auto depthOnlyDesc = makeDepthOnlyShadowPipelineDesc(*layout, shaderPackage);
    const auto depthOnlyKey = stableRenderPipelineStateKey(depthOnlyDesc);
    assert(depthOnlyKey != 0u);
    assert(depthOnlyKey != stableKey);

    auto changedDepthBias = depthOnlyDesc;
    changedDepthBias.depthStencil.depthBiasSlopeFactor = 2.5f;
    assert(stableRenderPipelineStateKey(changedDepthBias) != depthOnlyKey);

    auto changedDepthCull = depthOnlyDesc;
    changedDepthCull.cullMode = CullMode::Front;
    assert(stableRenderPipelineStateKey(changedDepthCull) != depthOnlyKey);

    auto changedDepthScissor = depthOnlyDesc;
    changedDepthScissor.scissor.width = 1024u;
    assert(stableRenderPipelineStateKey(changedDepthScissor) != depthOnlyKey);

    const auto depthOnlyReport = device->validateRenderPipeline(depthOnlyDesc);
    assert(depthOnlyReport.success);
    assert(depthOnlyReport.colorTargetCount == 0u);
    assert(depthOnlyReport.stencilStateValidated);
    assert(depthOnlyReport.depthBiasStateValidated);
    assert(depthOnlyReport.rasterStateValidated);
    assert(depthOnlyReport.viewportScissorStateValidated);
    assert(depthOnlyReport.pipelineCacheKey == depthOnlyKey);
    auto depthOnlyPipeline = device->createRenderPipeline(depthOnlyDesc);
    assert(depthOnlyPipeline);
    assert(depthOnlyPipeline->validationReport().colorTargetCount == 0u);

    auto invalidDepthOnly = depthOnlyDesc;
    invalidDepthOnly.depthFormat = TextureFormat::Unknown;
    invalidDepthOnly.depthStencil.format = TextureFormat::Unknown;
    const auto invalidDepthOnlyReport = device->validateRenderPipeline(invalidDepthOnly);
    assert(!invalidDepthOnlyReport.success);
    assert(invalidDepthOnlyReport.diagnostics.find(
               "depth-only render pipeline requires explicit depth/stencil format")
        != std::pmr::string::npos);

    const auto nullDepthOnlyDecision =
        lowerRenderPipelineState(nativeBackendCapabilityContract(Backend::Null), depthOnlyDesc);
    assert(nullDepthOnlyDecision.success);
    assert(nullDepthOnlyDecision.colorTargetCount == 0u);
    assert(nullDepthOnlyDecision.depthOnlyStateLowered);
    assert(!nullDepthOnlyDecision.blendWriteMaskLowered);
    assert(hasSemantic(
        nullDepthOnlyDecision, NativePipelineStateSemantic::L2DepthOnlyRenderPassState));
    assert(hasSemantic(
        nullDepthOnlyDecision, NativePipelineStateSemantic::NullDepthOnlyRenderPassState));

    const auto vulkanDepthOnlyDecision =
        lowerRenderPipelineState(nativeBackendCapabilityContract(Backend::Vulkan), depthOnlyDesc);
    assert(vulkanDepthOnlyDecision.success);
    assert(vulkanDepthOnlyDecision.depthOnlyStateLowered);
    assert(hasSemantic(
        vulkanDepthOnlyDecision, NativePipelineStateSemantic::L2DepthOnlyRenderPassState));
    assert(hasSemantic(
        vulkanDepthOnlyDecision, NativePipelineStateSemantic::VulkanDepthOnlyRenderPassState));
    assert(
        hasSemantic(vulkanDepthOnlyDecision, NativePipelineStateSemantic::VulkanDepthStencilState));
    assert(hasSemantic(
        vulkanDepthOnlyDecision, NativePipelineStateSemantic::VulkanDynamicViewportScissor));

    const auto metalDepthOnlyDecision =
        lowerRenderPipelineState(nativeBackendCapabilityContract(Backend::Metal), depthOnlyDesc);
    assert(metalDepthOnlyDecision.success);
    assert(metalDepthOnlyDecision.depthOnlyStateLowered);
    assert(hasSemantic(
        metalDepthOnlyDecision, NativePipelineStateSemantic::L2DepthOnlyRenderPassState));
    assert(hasSemantic(
        metalDepthOnlyDecision, NativePipelineStateSemantic::MetalDepthOnlyRenderPassState));
    assert(hasSemantic(
        metalDepthOnlyDecision, NativePipelineStateSemantic::MetalDepthStencilDescriptor));

    const auto webGpuDepthOnlyDecision =
        lowerRenderPipelineState(nativeBackendCapabilityContract(Backend::WebGPU), depthOnlyDesc);
    assert(webGpuDepthOnlyDecision.success);
    assert(webGpuDepthOnlyDecision.depthOnlyStateLowered);
    assert(hasSemantic(
        webGpuDepthOnlyDecision, NativePipelineStateSemantic::L2DepthOnlyRenderPassState));
    assert(hasSemantic(
        webGpuDepthOnlyDecision, NativePipelineStateSemantic::WebGpuDepthOnlyRenderPassState));
    assert(
        hasSemantic(webGpuDepthOnlyDecision, NativePipelineStateSemantic::WebGpuDepthStencilState));

    auto invalidMsaa = parityDesc;
    invalidMsaa.multisample.sampleCount = 3;
    const auto invalidMsaaReport = device->validateRenderPipeline(invalidMsaa);
    assert(!invalidMsaaReport.success);
    assert(invalidMsaaReport.unsupportedStateDiagnosed);
    assert(invalidMsaaReport.diagnostics.find("MSAA sample count") != std::pmr::string::npos);

    auto invalidStencil = parityDesc;
    invalidStencil.depthStencil.format = TextureFormat::D32Float;
    const auto invalidStencilReport = device->validateRenderPipeline(invalidStencil);
    assert(!invalidStencilReport.success);
    assert(invalidStencilReport.diagnostics.find("stencil") != std::pmr::string::npos);

    auto invalidResolve = parityDesc;
    invalidResolve.multisample.sampleCount = 1;
    const auto invalidResolveReport = device->validateRenderPipeline(invalidResolve);
    assert(!invalidResolveReport.success);
    assert(invalidResolveReport.diagnostics.find("resolve") != std::pmr::string::npos);

    auto invalidViewport = parityDesc;
    invalidViewport.viewport.width = 0.0f;
    const auto invalidViewportReport = device->validateRenderPipeline(invalidViewport);
    assert(!invalidViewportReport.success);
    assert(invalidViewportReport.diagnostics.find("viewport") != std::pmr::string::npos);

    auto unsupportedTopology = parityDesc;
    unsupportedTopology.topology = PrimitiveTopology::PatchList;
    const auto unsupportedTopologyReport = device->validateRenderPipeline(unsupportedTopology);
    assert(!unsupportedTopologyReport.success);
    assert(unsupportedTopologyReport.unsupportedStateDiagnosed);
    assert(unsupportedTopologyReport.diagnostics.find("topology") != std::pmr::string::npos);

    const auto nullDecision =
        lowerRenderPipelineState(nativeBackendCapabilityContract(Backend::Null), parityDesc);
    assert(nullDecision.success);
    assert(nullDecision.backend == Backend::Null);
    assert(nullDecision.pipelineCacheKey == stableKey);
    assert(nullDecision.colorTargetCount == 3u);
    assert(nullDecision.sampleCount == 4u);
    assert(nullDecision.blendWriteMaskLowered);
    assert(nullDecision.stencilStateLowered);
    assert(nullDecision.depthBiasStateLowered);
    assert(nullDecision.rasterStateLowered);
    assert(nullDecision.topologyLowered);
    assert(nullDecision.viewportScissorLowered);
    assert(nullDecision.resolveStateLowered);
    assert(!nullDecision.nativeHandleExposedToUpperLayer);
    assert(hasSemantic(nullDecision, NativePipelineStateSemantic::NullRecordedPipelineState));

    const auto vulkanDecision =
        lowerRenderPipelineState(nativeBackendCapabilityContract(Backend::Vulkan), parityDesc);
    assert(vulkanDecision.success);
    assert(vulkanDecision.fallbackMode == FallbackMode::Native);
    assert(vulkanDecision.pipelineCacheKey == stableKey);
    assert(hasSemantic(vulkanDecision, NativePipelineStateSemantic::VulkanGraphicsPipelineState));
    assert(hasSemantic(vulkanDecision, NativePipelineStateSemantic::VulkanRasterizationState));
    assert(hasSemantic(vulkanDecision, NativePipelineStateSemantic::VulkanMrtBlendState));
    assert(hasSemantic(vulkanDecision, NativePipelineStateSemantic::VulkanDepthStencilState));
    assert(hasSemantic(vulkanDecision, NativePipelineStateSemantic::VulkanDynamicViewportScissor));
    assert(hasSemantic(vulkanDecision, NativePipelineStateSemantic::VulkanMsaaResolveState));

    const auto metalDecision =
        lowerRenderPipelineState(nativeBackendCapabilityContract(Backend::Metal), parityDesc);
    assert(metalDecision.success);
    assert(metalDecision.backendVocabularyHiddenFromUpperLayer);
    assert(hasSemantic(metalDecision, NativePipelineStateSemantic::MetalRenderPipelineDescriptor));
    assert(hasSemantic(metalDecision, NativePipelineStateSemantic::MetalRasterizationState));
    assert(hasSemantic(metalDecision, NativePipelineStateSemantic::MetalDepthStencilDescriptor));
    assert(hasSemantic(metalDecision, NativePipelineStateSemantic::MetalViewportScissorState));
    assert(hasSemantic(metalDecision, NativePipelineStateSemantic::MetalMsaaResolveState));

    const auto webGpuDecision =
        lowerRenderPipelineState(nativeBackendCapabilityContract(Backend::WebGPU), parityDesc);
    assert(webGpuDecision.success);
    assert(webGpuDecision.backendVocabularyHiddenFromUpperLayer);
    assert(
        hasSemantic(webGpuDecision, NativePipelineStateSemantic::WebGpuRenderPipelineDescriptor));
    assert(hasSemantic(webGpuDecision, NativePipelineStateSemantic::WebGpuPrimitiveState));
    assert(hasSemantic(webGpuDecision, NativePipelineStateSemantic::WebGpuDepthStencilState));
    assert(hasSemantic(webGpuDecision, NativePipelineStateSemantic::WebGpuViewportScissorState));
    assert(hasSemantic(webGpuDecision, NativePipelineStateSemantic::WebGpuMsaaResolveState));

    const auto unsupportedDecision = lowerRenderPipelineState(
        nativeBackendCapabilityContract(Backend::Vulkan), unsupportedTopology);
    assert(!unsupportedDecision.success);
    assert(unsupportedDecision.fallbackMode == FallbackMode::DiagnosticFailure);
    assert(unsupportedDecision.diagnostics.find("topology") != std::pmr::string::npos);
    assert(
        hasSemantic(unsupportedDecision, NativePipelineStateSemantic::UnsupportedStateDiagnostic));
    assert(!unsupportedDecision.nativeHandleExposedToUpperLayer);

    assertVulkanNativeDepthStateIsLowered();
    assertWebGpuNativeDepthStateIsLowered();
    assertMetalNativeDepthStateIsDiagnosed();
    assertNativeDepthOnlyRenderPassLoweringIsSourceVisible();
    assertTaaMrtFormatLoweringIsSourceVisible();

    // --- VertexInputRate parity + lowering ---------------------------------
    // Two pipeline descs differing only in inputRate (Vertex vs Instance)
    // must produce distinct stable cache keys, and Instance-rate lowering
    // must emit the per-backend vertex-input semantic.
    auto vertexInputBase = makeParityPipelineDesc(*layout, shaderPackage);
    VertexBufferLayoutDesc perVertexLayout;
    perVertexLayout.slot = 0;
    perVertexLayout.strideBytes = sizeof(float) * 3;
    perVertexLayout.inputRate = VertexInputRate::Vertex;
    perVertexLayout.attributes.push_back({0, 0, VertexFormat::Float32x3});
    vertexInputBase.vertexBuffers.push_back(perVertexLayout);

    const auto vertexRateKey = stableRenderPipelineStateKey(vertexInputBase);
    assert(vertexRateKey != 0u);
    assert(stableRenderPipelineStateKey(vertexInputBase) == vertexRateKey);

    auto instanceRateDesc = vertexInputBase;
    instanceRateDesc.vertexBuffers[0].inputRate = VertexInputRate::Instance;
    const auto instanceRateKey = stableRenderPipelineStateKey(instanceRateDesc);
    assert(instanceRateKey != 0u);
    assert(instanceRateKey != vertexRateKey);

    const auto vertexRateState = validateRenderPipelineState(vertexInputBase);
    assert(vertexRateState.success);
    assert(vertexRateState.vertexInputStateValidated);
    const auto instanceRateState = validateRenderPipelineState(instanceRateDesc);
    assert(instanceRateState.success);
    assert(instanceRateState.vertexInputStateValidated);

    const auto nullVertexInputDecision =
        lowerRenderPipelineState(nativeBackendCapabilityContract(Backend::Null), instanceRateDesc);
    assert(nullVertexInputDecision.success);
    assert(nullVertexInputDecision.vertexInputStateLowered);
    assert(hasSemantic(nullVertexInputDecision, NativePipelineStateSemantic::NullVertexInputState));

    const auto vulkanVertexInputDecision = lowerRenderPipelineState(
        nativeBackendCapabilityContract(Backend::Vulkan), instanceRateDesc);
    assert(vulkanVertexInputDecision.success);
    assert(vulkanVertexInputDecision.vertexInputStateLowered);
    assert(hasSemantic(
        vulkanVertexInputDecision, NativePipelineStateSemantic::VulkanVertexInputState));

    const auto metalVertexInputDecision =
        lowerRenderPipelineState(nativeBackendCapabilityContract(Backend::Metal), instanceRateDesc);
    assert(metalVertexInputDecision.success);
    assert(metalVertexInputDecision.vertexInputStateLowered);
    assert(hasSemantic(
        metalVertexInputDecision, NativePipelineStateSemantic::MetalVertexDescriptorState));

    const auto webGpuVertexInputDecision = lowerRenderPipelineState(
        nativeBackendCapabilityContract(Backend::WebGPU), instanceRateDesc);
    assert(webGpuVertexInputDecision.success);
    assert(webGpuVertexInputDecision.vertexInputStateLowered);
    assert(hasSemantic(webGpuVertexInputDecision, NativePipelineStateSemantic::WebGpuVertexState));

    // --- depthBiasClamp capability gate ------------------------------------
    // A render pipeline that uses a non-zero depthBiasClamp must fail closed
    // on a backend whose capability contract reports
    // depthBiasClampSupported=false (the Vulkan fallback contract), and must
    // succeed on backends that guarantee it (Metal/WebGPU/Null). This is the
    // RHI contract "default fail closed" rule: a device that cannot honor
    // the state must not let the native path silently drop it.
    auto depthBiasClampDesc = makeDepthOnlyShadowPipelineDesc(*layout, shaderPackage);
    depthBiasClampDesc.depthStencil.depthBiasClamp = 0.5f;
    {
        auto vulkanFallbackContract = nativeBackendCapabilityContract(Backend::Vulkan);
        // The static fallback contract for Vulkan has depthBiasClampSupported
        // == false (only a real device query overwrites it to true). Use it
        // directly to exercise the fail-closed path without a live device.
        assert(!vulkanFallbackContract.depthBiasClampSupported);
        const auto gateDecision =
            lowerRenderPipelineState(vulkanFallbackContract, depthBiasClampDesc);
        assert(!gateDecision.success);
        assert(gateDecision.fallbackMode == FallbackMode::DiagnosticFailure);
        assert(hasSemantic(gateDecision, NativePipelineStateSemantic::UnsupportedStateDiagnostic));
        assert(gateDecision.diagnostics.find("depthBiasClamp") != std::pmr::string::npos);
    }
    {
        const auto metalGateDecision = lowerRenderPipelineState(
            nativeBackendCapabilityContract(Backend::Metal), depthBiasClampDesc);
        assert(metalGateDecision.success);
        assert(metalGateDecision.depthBiasStateLowered);
    }
    {
        const auto webGpuGateDecision = lowerRenderPipelineState(
            nativeBackendCapabilityContract(Backend::WebGPU), depthBiasClampDesc);
        assert(webGpuGateDecision.success);
        assert(webGpuGateDecision.depthBiasStateLowered);
    }
    {
        const auto nullGateDecision = lowerRenderPipelineState(
            nativeBackendCapabilityContract(Backend::Null), depthBiasClampDesc);
        assert(nullGateDecision.success);
        assert(nullGateDecision.depthBiasStateLowered);
    }

    // --- Compute pipeline lowering parity ----------------------------------
    // Exercise the compute pipeline state lowering decision across all four
    // backends. Compute has a smaller state surface (shader/layout/push-
    // constant/descriptor-binding) but the same contract-first shape as
    // render: validateComputePipelineState runs first, then per-backend
    // vocabulary semantics are recorded.
    auto computeShaderPackage = makeComputePipelineStateShaderPackage();
    assert(validateShaderPackage(computeShaderPackage).success);
    auto computeDesc = makeParityComputePipelineDesc(*layout, computeShaderPackage);
    const auto computeKey = stableComputePipelineStateKey(computeDesc);
    assert(computeKey != 0u);
    assert(stableComputePipelineStateKey(computeDesc) == computeKey);
    const auto computeValidation = validateComputePipelineState(computeDesc);
    assert(computeValidation.success);
    assert(computeValidation.shaderStateValidated);
    assert(computeValidation.layoutStateValidated);
    assert(computeValidation.pushConstantStateValidated);
    assert(computeValidation.descriptorBindingStateValidated);
    assert(computeValidation.pipelineCacheKey == computeKey);

    {
        const auto nullComputeDecision =
            lowerComputePipelineState(nativeBackendCapabilityContract(Backend::Null), computeDesc);
        assert(nullComputeDecision.success);
        assert(nullComputeDecision.shaderStateLowered);
        assert(nullComputeDecision.layoutStateLowered);
        assert(nullComputeDecision.pushConstantStateLowered);
        assert(nullComputeDecision.descriptorBindingStateLowered);
        assert(nullComputeDecision.pipelineCacheKey == computeKey);
        assert(hasComputeSemantic(
            nullComputeDecision, NativeComputePipelineStateSemantic::L2ComputePipelineState));
        assert(hasComputeSemantic(nullComputeDecision,
            NativeComputePipelineStateSemantic::NullRecordedComputePipelineState));
    }
    {
        const auto vulkanComputeDecision = lowerComputePipelineState(
            nativeBackendCapabilityContract(Backend::Vulkan), computeDesc);
        assert(vulkanComputeDecision.success);
        assert(vulkanComputeDecision.shaderStateLowered);
        assert(vulkanComputeDecision.layoutStateLowered);
        assert(hasComputeSemantic(
            vulkanComputeDecision, NativeComputePipelineStateSemantic::VulkanComputePipelineState));
        assert(hasComputeSemantic(
            vulkanComputeDecision, NativeComputePipelineStateSemantic::VulkanComputeShaderStage));
        assert(hasComputeSemantic(vulkanComputeDecision,
            NativeComputePipelineStateSemantic::VulkanComputePipelineLayout));
    }
    {
        const auto metalComputeDecision =
            lowerComputePipelineState(nativeBackendCapabilityContract(Backend::Metal), computeDesc);
        assert(metalComputeDecision.success);
        assert(metalComputeDecision.shaderStateLowered);
        assert(hasComputeSemantic(
            metalComputeDecision, NativeComputePipelineStateSemantic::MetalComputePipelineState));
        assert(hasComputeSemantic(
            metalComputeDecision, NativeComputePipelineStateSemantic::MetalComputeShaderFunction));
        assert(hasComputeSemantic(
            metalComputeDecision, NativeComputePipelineStateSemantic::MetalComputePipelineLayout));
    }
    {
        const auto webGpuComputeDecision = lowerComputePipelineState(
            nativeBackendCapabilityContract(Backend::WebGPU), computeDesc);
        assert(webGpuComputeDecision.success);
        assert(webGpuComputeDecision.shaderStateLowered);
        assert(hasComputeSemantic(webGpuComputeDecision,
            NativeComputePipelineStateSemantic::WebGpuComputePipelineDescriptor));
        assert(hasComputeSemantic(
            webGpuComputeDecision, NativeComputePipelineStateSemantic::WebGpuComputeShaderModule));
        assert(hasComputeSemantic(webGpuComputeDecision,
            NativeComputePipelineStateSemantic::WebGpuComputeBindGroupLayout));
    }

    // --- Compute lowering fail-closed path ---------------------------------
    auto invalidCompute = computeDesc;
    invalidCompute.layout = nullptr;
    const auto invalidComputeDecision =
        lowerComputePipelineState(nativeBackendCapabilityContract(Backend::Null), invalidCompute);
    assert(!invalidComputeDecision.success);
    assert(invalidComputeDecision.fallbackMode == FallbackMode::DiagnosticFailure);
    assert(hasComputeSemantic(invalidComputeDecision,
        NativeComputePipelineStateSemantic::UnsupportedComputeStateDiagnostic));

    return 0;
}
