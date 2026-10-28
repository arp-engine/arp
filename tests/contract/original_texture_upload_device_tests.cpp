#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/null/null_hidden_frame_smoke.h"
#include "rhi/frontend/original_texture_upload_device.h"

#include <cassert>
#include <iostream>
#include <type_traits>

namespace {
using namespace arp;
using namespace rhi;

void contract()
{
    using P = OriginalTextureUploadPublication;
    using C = OriginalTextureUploadPreparationContract;
    static_assert(!std::is_default_constructible_v<P>);
    static_assert(!std::is_copy_constructible_v<P>);
    OriginalTextureUploadPreparationRequest request{
        {1u, true, 1u, 2u, 2048u}, {1u, true, 1u, 128u, 65536u}};
    assert(!C::validate(request));
    for (const auto backend : {Backend::Null,
             Backend::Vulkan,
             Backend::Metal,
             Backend::WebGPU,
             Backend::WebGL,
             static_cast<Backend>(255u)}) {
        const auto d = C::lowering(backend, request);
        assert(d.validatorOnly == (backend == Backend::Null));
        assert(d.privateRecordingAvailable == (backend == Backend::Vulkan));
        assert(d.completionPublicationRequired);
        assert((d.diagnostic == nullptr) == (backend == Backend::Vulkan));
    }
    auto bad = request;
    bad.publications.contractVersion = 0u;
    assert(std::string_view(C::validate(bad)) == "RHI-TEXTURE-UPLOAD-PUBLICATION-VERSION");
    assert(!C::lowering(Backend::Vulkan, bad).privateRecordingAvailable);
    bad = request;
    bad.publications.prototypeOptIn = false;
    assert(C::validate(bad));
    bad = request;
    bad.publications.maxPublications = 0u;
    assert(C::validate(bad));
    bad = request;
    bad.publications.maxRetainedOwners = 0u;
    assert(C::validate(bad));
    bad = request;
    bad.publications.maxCpuBytes = 0u;
    assert(C::validate(bad));
    bad = request;
    bad.uploads.prototypeOptIn = false;
    assert(C::validate(bad));
    assert(C::inspect({}, request, nullptr, 0u, {}));
    assert(P::validateOriginal({}));

    TextureDesc texture;
    texture.extent = {4u, 2u, 1u};
    texture.format = TextureFormat::Rgba8UnormSrgb;
    texture.usage = TextureUsage::Sampled | TextureUsage::CopyDst;
    const GpuResourceId target{42u};
    ResourceBarrierDesc release{target,
        defaultTextureSubresourceRange(texture),
        ResourceState::CopyDst,
        ResourceState::ShaderRead,
        PipelineStage::Transfer,
        PipelineStage::ComputeShader,
        ResourceAccess::TransferWrite,
        ResourceAccess::ShaderRead};
    using Upload = OriginalOrdinaryTextureUploadContract;
    assert(!Upload::validateCompletedReadBarrier(
        texture, target, release, PipelineStage::ComputeShader));
    assert(Upload::validateCompletedReadBarrier(
        texture, target, release, PipelineStage::FragmentShader));
    release.destinationStages = PipelineStage::FragmentShader | PipelineStage::VertexShader;
    assert(
        !Upload::validateCompletedReadBarrier(texture, target, release, release.destinationStages));
    assert(Upload::validateCompletedReadBarrier(texture, target, release, PipelineStage::None));
    assert(Upload::validateCompletedReadBarrier(texture, target, release, PipelineStage::Transfer));
    auto invalid = release;
    invalid.destinationAccess = ResourceAccess::None;
    assert(Upload::validateCompletedReadBarrier(
        texture, target, invalid, PipelineStage::FragmentShader));
    invalid = release;
    invalid.sourceAccess = ResourceAccess::None;
    assert(Upload::validateCompletedReadBarrier(
        texture, target, invalid, PipelineStage::FragmentShader));
    invalid = release;
    invalid.before = ResourceState::Undefined;
    assert(Upload::validateCompletedReadBarrier(
        texture, target, invalid, PipelineStage::FragmentShader));
    invalid = release;
    invalid.textureSubresources.baseMipLevel = 1u;
    assert(Upload::validateCompletedReadBarrier(
        texture, target, invalid, PipelineStage::FragmentShader));
    invalid = release;
    invalid.resource = GpuResourceId{43u};
    assert(Upload::validateCompletedReadBarrier(
        texture, target, invalid, PipelineStage::FragmentShader));
}

void nullSmoke()
{
    auto device = createNullDevice();
    const char* id = nullptr;
    OriginalTextureUploadPreparationRequest request{
        {1u, true, 1u, 1u, 256u}, {1u, true, 1u, 128u, 65536u}};
    assert(!OriginalTextureUploadDeviceExtension::query(*device, request, id));
    assert(std::string_view(id) == "RHI-ORDINARY-TEXTURE-UPLOAD-NULL-VALIDATOR");
    assert(!device->queryInterface(OriginalTextureUploadDeviceExtension::id));
    NullHiddenFrameSmokeRunner runner(*device);
    const auto smoke = runner.run(
        {"OriginalTextureUpload.NullRefusal", {16u, 16u}, TextureFormat::Bgra8Unorm, 2u});
    assert(smoke.success && smoke.submittedFrameCount == 2u);
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--null-hidden-frame")
        nullSmoke();
    else
        contract();
    std::cout << "OriginalTextureUploadDevice/1 contract; Null grants no upload execution\n";
}
