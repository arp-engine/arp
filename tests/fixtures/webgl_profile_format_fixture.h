#pragma once

#include "webgl_profile_probe_fixture.h"

#include <array>
#include <cassert>
#include <cstring>
#include <limits>
#include <type_traits>

namespace arp::test {

class WebGlProfileFormatFixture final {
public:
    static void verify()
    {
        using namespace rhi;
        using namespace rhi::semantics;
        auto probe = WebGlProfileProbeFixture::baseline();
        const auto profile = WebGlRuntimeProfile::fromProbe(probe);
        assert(profile.valid());
        constexpr std::array formats{TextureFormat::Unknown,
            TextureFormat::Bgra8Unorm,
            TextureFormat::Rgba8Unorm,
            TextureFormat::Rgba8UnormSrgb,
            TextureFormat::R8Unorm,
            TextureFormat::Rgba16Float,
            TextureFormat::Rg16Float,
            TextureFormat::R32Float,
            TextureFormat::D32Float,
            TextureFormat::Depth24Stencil8,
            TextureFormat::Bc1Unorm,
            TextureFormat::Bc3Unorm,
            TextureFormat::Bc7Unorm,
            TextureFormat::Bc7UnormSrgb,
            static_cast<TextureFormat>(255u)};
        static_assert(std::is_same_v<std::underlying_type_t<TextureFormat>, int>);
        // The lightweight header and legacy gpu_common.h expose the same
        // stable enum identity; moving its declaration cannot renumber it.
        for (std::size_t index = 0u; index + 1u < formats.size(); ++index)
            assert(static_cast<int>(formats[index]) == static_cast<int>(index));
        constexpr std::array roles{WebGlFormatRole::Texture2DClientUpload,
            WebGlFormatRole::Texture2DSampled,
            WebGlFormatRole::Texture2DColorAttachment,
            WebGlFormatRole::DepthStencilRenderbuffer};
        for (auto format : formats) {
            for (auto role : roles) {
                const auto result = profile.admitFormat({1u, format, role, 1u});
                const bool expected = (format == TextureFormat::Rgba8Unorm
                                          && role != WebGlFormatRole::DepthStencilRenderbuffer)
                    || (format == TextureFormat::Depth24Stencil8
                        && role == WebGlFormatRole::DepthStencilRenderbuffer);
                assert(result.compatible == expected && !result.executable);
                if (!expected)
                    assert(std::strcmp(result.diagnostic, "RHI-WEBGL1-FORMAT-UNSUPPORTED") == 0);
            }
        }
        const WebGlFormatRequest depth{
            1u, TextureFormat::Depth24Stencil8, WebGlFormatRole::DepthStencilRenderbuffer, 1u};
        for (auto depthBits : {0, 16, 24, 32}) {
            for (auto stencilBits : {0, 8, 16}) {
                auto exact = probe;
                exact.formats.packedDepthBits = depthBits;
                exact.formats.packedStencilBits = stencilBits;
                const auto result = WebGlRuntimeProfile::fromProbe(exact).admitFormat(depth);
                assert(result.compatible == (depthBits == 24 && stencilBits == 8));
                assert(!result.executable);
            }
        }
        for (auto field : {&WebGlFormatFacts::rgba8Texture,
                 &WebGlFormatFacts::rgba8Framebuffer,
                 &WebGlFormatFacts::packedDepthStencil}) {
            for (auto outcome : {WebGlFormatQueryOutcome::NotQueried,
                     WebGlFormatQueryOutcome::NativeFailure,
                     WebGlFormatQueryOutcome::OutOfMemory,
                     WebGlFormatQueryOutcome::ContextLost,
                     static_cast<WebGlFormatQueryOutcome>(255u)}) {
                auto invalid = probe;
                invalid.formats.*field = outcome;
                const auto rejected = WebGlRuntimeProfile::fromProbe(invalid);
                assert(!rejected.valid());
                const char* expected = "RHI-WEBGL1-FORMAT-PROBE-INVALID";
                switch (outcome) {
                case WebGlFormatQueryOutcome::NotQueried:
                    expected = "RHI-WEBGL1-FORMAT-PROBE-INCOMPLETE";
                    break;
                case WebGlFormatQueryOutcome::NativeFailure:
                    expected = "RHI-WEBGL1-FORMAT-NATIVE-FAILURE";
                    break;
                case WebGlFormatQueryOutcome::OutOfMemory:
                    expected = "RHI-WEBGL1-FORMAT-OUT-OF-MEMORY";
                    break;
                case WebGlFormatQueryOutcome::ContextLost:
                    expected = "RHI-WEBGL1-FORMAT-CONTEXT-LOST";
                    break;
                default:
                    break;
                }
                assert(std::strcmp(rejected.admitFormat(depth).diagnostic, expected) == 0);
                assert(!rejected.admitFormat(depth).executable);
            }
        }
        for (auto field :
            {&WebGlFormatFacts::packedDepthBits, &WebGlFormatFacts::packedStencilBits}) {
            auto invalid = probe;
            invalid.formats.*field = -1;
            assert(!WebGlRuntimeProfile::fromProbe(invalid).valid());
        }
        WebGlFormatRequest request{
            1u, TextureFormat::Rgba8Unorm, WebGlFormatRole::Texture2DClientUpload, 1u};
        for (auto version : {0u, 2u, std::numeric_limits<std::uint32_t>::max()}) {
            auto invalid = probe;
            invalid.formats.contractVersion = version;
            assert(!WebGlRuntimeProfile::fromProbe(invalid).valid());
            request.contractVersion = version;
            assert(std::strcmp(
                       profile.admitFormat(request).diagnostic, "RHI-WEBGL1-FORMAT-REQUEST-VERSION")
                == 0);
        }
        request.contractVersion = 1u;
        for (auto count : {0u, 2u, 4u, std::numeric_limits<std::uint32_t>::max()}) {
            request.sampleCount = count;
            assert(std::strcmp(
                       profile.admitFormat(request).diagnostic, "RHI-WEBGL1-FORMAT-SAMPLE-COUNT")
                == 0);
        }
        request.sampleCount = 1u;
        request.role = static_cast<WebGlFormatRole>(255u);
        assert(std::strcmp(profile.admitFormat(request).diagnostic, "RHI-WEBGL1-FORMAT-ROLE") == 0);
        auto limited = probe;
        limited.formats.rgba8FramebufferComplete = false;
        limited.formats.packedFramebufferComplete = false;
        const auto limitedProfile = WebGlRuntimeProfile::fromProbe(limited);
        assert(limitedProfile.valid());
        assert(
            limitedProfile.admitFormat({1u, TextureFormat::Rgba8Unorm, roles[0], 1u}).compatible);
        assert(
            !limitedProfile.admitFormat({1u, TextureFormat::Rgba8Unorm, roles[2], 1u}).compatible);
        assert(!limitedProfile.admitFormat(depth).compatible);
        limited.formats.rgba8ClientUploadAccepted = false;
        assert(WebGlRuntimeProfile::fromProbe(limited).valid());
        limited.formats.rgba8FramebufferComplete = true;
        assert(!WebGlRuntimeProfile::fromProbe(limited).valid());
        // The original immutable profile is independent of later mutations.
        probe.formats = {};
        assert(profile.admitFormat(depth).compatible && profile.formats().packedDepthBits == 24);
    }
};

} // namespace arp::test
