#include "rhi/gpu_swapchain.h"

#include <cassert>
#include <string>

namespace {

arp::rhi::AcquiredFrame makeAcquiredFrame()
{
    arp::rhi::AcquiredFrame frame;
    frame.success = true;
    frame.frameSerial = 7;
    frame.frameIndex = 1;
    frame.status = arp::rhi::AcquireStatus::Acquired;
    return frame;
}

arp::rhi::PresentResult makePresentResult()
{
    arp::rhi::PresentResult present;
    present.success = true;
    present.frameSerial = 7;
    present.frameIndex = 1;
    present.status = arp::rhi::PresentStatus::Presented;
    return present;
}

arp::rhi::TextureUploadMetadata makeTextureUploadMetadata()
{
    arp::rhi::TextureUploadMetadata upload;
    upload.label = "AlbedoUpload";
    upload.destinationTexture = {44};
    upload.extent = {64, 64, 1};
    upload.format = arp::rhi::TextureFormat::Rgba8Unorm;
    upload.bytesPerRow = 256;
    upload.rowsPerImage = 64;
    upload.subresources = {0, 1, 0, 1, arp::rhi::TextureAspect::Color};
    upload.stagingIntent = arp::rhi::StagingIntent::Upload;
    upload.layoutSource = arp::rhi::TextureUploadLayoutSource::ExplicitMetadata;
    upload.backendOwnedStaging = true;
    return upload;
}

arp::rhi::RendererInputContract makeRendererInput(arp::rhi::RendererInputKind kind)
{
    arp::rhi::RendererInputContract input;
    input.kind = kind;
    input.label = "RendererInput";
    input.descriptorBacked = true;
    input.pipelineLayoutKnown = true;
    input.explicitResourceStates = true;
    input.backendOwnedStaging = true;

    switch (kind) {
    case arp::rhi::RendererInputKind::GuiDraw:
        input.drawCommandCount = 3;
        input.vertexBytes = 1024;
        input.indexBytes = 256;
        break;
    case arp::rhi::RendererInputKind::ParticleDraw:
        input.drawCommandCount = 2;
        input.vertexBytes = 2048;
        input.particleCount = 128;
        break;
    case arp::rhi::RendererInputKind::Fxaa:
        input.drawCommandCount = 1;
        input.sampledTextureCount = 1;
        break;
    case arp::rhi::RendererInputKind::ScenePass:
        input.drawCommandCount = 12;
        input.meshCount = 4;
        input.materialBindingCount = 4;
        break;
    case arp::rhi::RendererInputKind::ShadowPass:
        input.drawCommandCount = 8;
        input.meshCount = 3;
        input.shadowCasterCount = 3;
        input.hasDepthTarget = true;
        break;
    case arp::rhi::RendererInputKind::GiCompute:
        input.dispatchCount = 2;
        input.storageBufferCount = 1;
        input.storageTextureCount = 1;
        break;
    case arp::rhi::RendererInputKind::Unknown:
        break;
    }

    return input;
}

void assertFrameAcquirePresentContract()
{
    const arp::rhi::RhiFrameContractValidator validator({2});

    const auto acquired = makeAcquiredFrame();
    const auto acquireOk = validator.validateAcquireResult(acquired);
    assert(acquireOk.success);
    assert(acquireOk.diagnostics.empty());

    auto invalidSerial = acquired;
    invalidSerial.frameSerial = 0;
    const auto serialFailure = validator.validateAcquireResult(invalidSerial);
    assert(!serialFailure.success);
    assert(serialFailure.diagnostics.find("frame serial") != std::pmr::string::npos);

    const auto present = makePresentResult();
    const auto presentOk = validator.validatePresentResult(acquired, present);
    assert(presentOk.success);

    auto mismatchedPresent = present;
    mismatchedPresent.frameSerial = 8;
    const auto presentFailure = validator.validatePresentResult(acquired, mismatchedPresent);
    assert(!presentFailure.success);
    assert(presentFailure.diagnostics.find("frame serial") != std::pmr::string::npos);

    const auto outOfDateAcquire = arp::rhi::makeAcquireFailureResult(
        "swapchain out of date", 7, 1, arp::rhi::AcquireStatus::OutOfDate);
    assert(!outOfDateAcquire.success);
    assert(outOfDateAcquire.recoverable);
    assert(outOfDateAcquire.swapchainRebuildRequired);
    assert(validator.validateAcquireResult(outOfDateAcquire).success);

    const auto suboptimalPresent =
        arp::rhi::makePresentSuccessResult(7, 1, arp::rhi::PresentStatus::Suboptimal);
    assert(suboptimalPresent.success);
    assert(suboptimalPresent.recoverable);
    assert(suboptimalPresent.swapchainRebuildRequired);
    assert(validator.validatePresentResult(acquired, suboptimalPresent).success);

    const auto outOfDatePresent = arp::rhi::makePresentFailureResult(
        "swapchain out of date", 7, 1, arp::rhi::PresentStatus::OutOfDate);
    assert(!outOfDatePresent.success);
    assert(outOfDatePresent.recoverable);
    assert(outOfDatePresent.swapchainRebuildRequired);
    assert(validator.validatePresentResult(acquired, outOfDatePresent).success);
}

void assertRenderOutputContract()
{
    const arp::rhi::RhiFrameContractValidator validator({2});

    arp::rhi::SwapchainImageRef backbuffer;
    backbuffer.image = {101};
    backbuffer.imageIndex = 1;
    backbuffer.frameSerial = 7;
    backbuffer.extent = {1280, 720};
    backbuffer.format = arp::rhi::TextureFormat::Bgra8Unorm;
    backbuffer.state = arp::rhi::ResourceState::Present;
    assert(validator.validateSwapchainImage(backbuffer).success);

    arp::rhi::RenderOutputDesc output;
    output.color.label = "MainColor";
    output.color.target = backbuffer.image;
    output.color.extent = backbuffer.extent;
    output.color.format = backbuffer.format;
    output.color.finalState = arp::rhi::ResourceState::Present;
    output.presentFormat = arp::rhi::TextureFormat::Bgra8Unorm;
    output.hasDepth = true;
    output.depth.label = "MainDepth";
    output.depth.target = {102};
    output.depth.extent = backbuffer.extent;
    output.depth.format = arp::rhi::TextureFormat::D32Float;
    output.depth.finalState = arp::rhi::ResourceState::DepthAttachment;
    assert(validator.validateRenderOutput(output).success);

    auto unknownFormat = output;
    unknownFormat.color.format = arp::rhi::TextureFormat::Unknown;
    const auto formatFailure = validator.validateRenderOutput(unknownFormat);
    assert(!formatFailure.success);
    assert(formatFailure.diagnostics.find("format") != std::pmr::string::npos);
}

void assertTextureUploadMetadataContract()
{
    const arp::rhi::RhiFrameContractValidator validator({2});
    const auto upload = makeTextureUploadMetadata();
    assert(validator.validateTextureUploadMetadata(upload).success);

    auto unknownLayout = upload;
    unknownLayout.layoutSource = arp::rhi::TextureUploadLayoutSource::Unknown;
    const auto layoutFailure = validator.validateTextureUploadMetadata(unknownLayout);
    assert(!layoutFailure.success);
    assert(layoutFailure.diagnostics.find("explicit texture upload metadata")
        != std::pmr::string::npos);

    auto invalidPitch = upload;
    invalidPitch.bytesPerRow = 130;
    const auto pitchFailure = validator.validateTextureUploadMetadata(invalidPitch);
    assert(!pitchFailure.success);
    assert(pitchFailure.diagnostics.find("row pitch") != std::pmr::string::npos);
}

void assertResourceIntentContracts()
{
    const arp::rhi::RhiFrameContractValidator validator({2});

    arp::rhi::MeshBufferUploadIntent mesh;
    mesh.label = "HeroMesh";
    mesh.vertexBuffer = {201};
    mesh.indexBuffer = {202};
    mesh.vertexBytes = 4096;
    mesh.indexBytes = 1024;
    mesh.vertexStrideBytes = 48;
    mesh.indexFormatBytes = 4;
    mesh.stagingIntent = arp::rhi::StagingIntent::Upload;
    mesh.backendOwnedStaging = true;
    assert(validator.validateMeshBufferUploadIntent(mesh).success);

    arp::rhi::MaterialTextureBindingIntent material;
    material.materialName = "HeroMaterial";
    material.binding = 0;
    material.texture = {301};
    material.sampler = {302};
    material.format = arp::rhi::TextureFormat::Rgba8Unorm;
    material.textureUsage = arp::rhi::TextureUsage::Sampled;
    material.descriptorBacked = true;
    assert(validator.validateMaterialTextureBindingIntent(material).success);

    auto invalidMaterial = material;
    invalidMaterial.textureUsage = arp::rhi::TextureUsage::Storage;
    const auto materialFailure = validator.validateMaterialTextureBindingIntent(invalidMaterial);
    assert(!materialFailure.success);
    assert(materialFailure.diagnostics.find("sampled") != std::pmr::string::npos);
}

void assertRendererInputContracts()
{
    const arp::rhi::RhiFrameContractValidator validator({2});
    const arp::rhi::RendererInputKind kinds[] = {
        arp::rhi::RendererInputKind::GuiDraw,
        arp::rhi::RendererInputKind::ParticleDraw,
        arp::rhi::RendererInputKind::Fxaa,
        arp::rhi::RendererInputKind::ScenePass,
        arp::rhi::RendererInputKind::ShadowPass,
        arp::rhi::RendererInputKind::GiCompute,
    };

    for (const auto kind : kinds) {
        const auto input = makeRendererInput(kind);
        assert(validator.validateRendererInput(input).success);
    }

    auto invalidGui = makeRendererInput(arp::rhi::RendererInputKind::GuiDraw);
    invalidGui.descriptorBacked = false;
    const auto guiFailure = validator.validateRendererInput(invalidGui);
    assert(!guiFailure.success);
    assert(guiFailure.diagnostics.find("GUI") != std::pmr::string::npos);

    auto invalidGi = makeRendererInput(arp::rhi::RendererInputKind::GiCompute);
    invalidGi.storageTextureCount = 0;
    const auto giFailure = validator.validateRendererInput(invalidGi);
    assert(!giFailure.success);
    assert(giFailure.diagnostics.find("GI") != std::pmr::string::npos);
}

} // namespace

int main()
{
    assertFrameAcquirePresentContract();
    assertRenderOutputContract();
    assertTextureUploadMetadataContract();
    assertResourceIntentContracts();
    assertRendererInputContracts();
    return 0;
}
