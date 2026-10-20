#pragma once
#include <cstdint>

namespace arp::rhi::semantics {
// WebGlShaderCoordinates/1. These facts apply only to a validated ES100 target
// that already performs clip Y negation and z'=2z-w exactly once. They do not
// authorize the generic WebGL RHI coordinate/execution path before P2.
class WebGlShaderCoordinates final {
public:
    static constexpr bool compilerFlipsClipY = true;
    static constexpr bool compilerRemapsClipDepth = true;
    static constexpr bool cpuProjectionFlip = false;
    static constexpr bool attachmentSampleFlipY = true;
    static constexpr bool cpuRasterSampleFlipY = false;
    static constexpr bool readbackReverseRows = true;
    static constexpr bool nativeFrontFaceReversed = true;

    static constexpr float clipDepth(float z, float w) noexcept { return 2 * z - w; }

    static constexpr float clipY(float y) noexcept { return -y; }

    struct Rectangle {
        std::uint32_t x = 0, y = 0, width = 0, height = 0;
    };

    static const char* lowerTopLeftRectangle(
        Rectangle input, std::uint32_t width, std::uint32_t height, Rectangle& output) noexcept
    {
        output = {};
        if (!input.width || !input.height || input.x > width || input.y > height
            || input.width > width - input.x || input.height > height - input.y)
            return "RHI-WEBGL1-RECTANGLE-RANGE";
        output = {input.x, height - input.y - input.height, input.width, input.height};
        return nullptr;
    }
};
} // namespace arp::rhi::semantics
