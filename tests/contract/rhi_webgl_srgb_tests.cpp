#include "../fixtures/webgl_profile_probe_fixture.h"
#include "rhi/semantics/webgl_resource_creation.h"

#include <cassert>
#include <cstring>

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi::semantics;
    auto probe = arp::test::WebGlProfileProbeFixture::baseline();
    WebGlExtensionOptIn optIn{2, {}};
    optIn.extensions.srgbTextures = true;
    WebGlGraphicsRequirements requirement;
    requirement.srgbTextures = true;
    const auto admit = [&] {
        return WebGlRuntimeProfile::fromProbe(probe).admit(requirement, optIn);
    };
    assert(!admit().compatible);
    probe.extensions.srgbTextures = true;
    assert(std::strcmp(admit().diagnostic, "RHI-WEBGL1-EXTENSION-NOT-ENABLED") == 0);
    probe.enabledExtensions.srgbTextures = true;
    assert(admit().compatible && !admit().executable);
    optIn.contractVersion = 1;
    assert(std::strcmp(admit().diagnostic, "RHI-WEBGL1-SRGB-PROFILE-REQUIRED") == 0);
    optIn.contractVersion = 2;
    WebGlExtensionExecutionRequest execution{2, true, optIn.extensions};
    const auto lower = [&] {
        return WebGlExtensionExecutionContract::lower(
            Backend::WebGL, execution, WebGlRuntimeProfile::fromProbe(probe), optIn);
    };
    assert(!lower().executable);
    probe.formats.contractVersion = 2;
    probe.formats.srgbTexture = WebGlFormatQueryOutcome::Observed;
    assert(!lower().executable);
    probe.formats.srgbClientUploadAccepted = true;
    assert(lower().executable);
    for (auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU})
        assert(!WebGlExtensionExecutionContract::lower(
            backend, execution, WebGlRuntimeProfile::fromProbe(probe), optIn)
                .executable);
    TextureDesc texture;
    texture.extent = {2, 2, 1};
    texture.format = TextureFormat::Rgba8UnormSrgb;
    texture.usage = TextureUsage::Sampled | TextureUsage::CopyDst;
    const auto create = [&] {
        return WebGlResourceCreationContract::texture(
            WebGlRuntimeProfile::fromProbe(probe), texture, optIn);
    };
    assert(!create());
    optIn.extensions.srgbTextures = false;
    assert(create());
    optIn.extensions.srgbTextures = true;
    texture.usage = texture.usage | TextureUsage::RenderAttachment;
    assert(create());
    texture.usage = TextureUsage::Sampled | TextureUsage::CopyDst;
    probe.formats.srgbTexture = WebGlFormatQueryOutcome::NotQueried;
    assert(WebGlFormatContract::validate(probe.formats));
    probe.formats.srgbTexture = WebGlFormatQueryOutcome::Observed;
    probe.formats.contractVersion = 1;
    assert(WebGlFormatContract::validate(probe.formats));
}
