#include "../fixtures/webgl_profile_probe_fixture.h"
#include "rhi/semantics/webgl_extension_execution.h"

#include <cassert>
#include <cstring>

using namespace arp::rhi;
using namespace arp::rhi::semantics;

int main()
{
    auto probe = arp::test::WebGlProfileProbeFixture::baseline();
    WebGlGraphicsRequirements requirement;
    requirement.fragmentDepth = true;
    WebGlExtensionOptIn consent;
    consent.extensions.fragmentDepth = true;
    const auto assess = [&] {
        return WebGlRuntimeProfile::fromProbe(probe).admit(requirement, consent);
    };
    for (auto version : {0u, 1u}) {
        consent.contractVersion = version;
        assert(std::strcmp(assess().diagnostic, "RHI-WEBGL1-FRAGMENT-DEPTH-PROFILE-REQUIRED") == 0);
    }
    consent.contractVersion = 2;
    assert(std::strcmp(assess().diagnostic, "RHI-WEBGL1-FRAGMENT-DEPTH-QUERY-REQUIRED") == 0);
    probe.fragmentDepthQueryVersion = 1;
    assert(std::strcmp(assess().diagnostic, "RHI-WEBGL1-EXTENSION-UNAVAILABLE") == 0);
    probe.extensions.fragmentDepth = true;
    assert(std::strcmp(assess().diagnostic, "RHI-WEBGL1-EXTENSION-NOT-ENABLED") == 0);
    probe.enabledExtensions.fragmentDepth = true;
    assert(assess().compatible && !assess().executable);
    consent.extensions.fragmentDepth = false;
    assert(std::strcmp(assess().diagnostic, "RHI-WEBGL1-EXTENSION-OPT-IN-REQUIRED") == 0);
    consent.extensions.fragmentDepth = true;
    WebGlExtensionExecutionRequest request{2, true, consent.extensions};
    const auto profile = WebGlRuntimeProfile::fromProbe(probe);
    for (auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU})
        assert(
            !WebGlExtensionExecutionContract::lower(backend, request, profile, consent).executable);
    assert(WebGlExtensionExecutionContract::lower(Backend::WebGL, request, profile, consent)
            .executable);
    request.contractVersion = 1;
    assert(!WebGlExtensionExecutionContract::lower(Backend::WebGL, request, profile, consent)
            .executable);
    probe.fragmentDepthQueryVersion = 0;
    assert(!WebGlRuntimeProfile::fromProbe(probe).valid());
    probe = arp::test::WebGlProfileProbeFixture::baseline();
    assert(WebGlRuntimeProfile::fromProbe(probe).valid());
}
