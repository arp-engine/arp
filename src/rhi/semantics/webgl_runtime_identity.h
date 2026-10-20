#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace arp::rhi::semantics {

enum class WebGlIdentityField : unsigned char {
    ContextVersion,
    ShaderVersion,
    Vendor,
    Renderer,
    BrowserUserAgent,
    Count
};

inline constexpr std::size_t webGlIdentityFieldCount =
    static_cast<std::size_t>(WebGlIdentityField::Count);

struct WebGlIdentityValue {
    bool queried = false;
    std::string_view value;
};

// Borrowed cold validation input. An immutable runtime profile must own bytes.
using WebGlRuntimeIdentityView = std::array<WebGlIdentityValue, webGlIdentityFieldCount>;

class WebGlRuntimeIdentityContract final {
public:
    static const char* validate(const WebGlRuntimeIdentityView& identity) noexcept
    {
        for (const auto& field : identity) {
            if (field.value.find('\0') != std::string_view::npos)
                return "RHI-WEBGL1-IDENTITY-PROBE-INVALID";
        }
        for (const auto& field : identity) {
            if (!field.queried)
                return "RHI-WEBGL1-IDENTITY-PROBE-INCOMPLETE";
        }
        if (!version(identity[static_cast<std::size_t>(WebGlIdentityField::ContextVersion)].value,
                "WebGL 1.0"))
            return "RHI-WEBGL1-IDENTITY-CONTEXT-VERSION";
        if (!version(identity[static_cast<std::size_t>(WebGlIdentityField::ShaderVersion)].value,
                "WebGL GLSL ES 1.0"))
            return "RHI-WEBGL1-IDENTITY-SHADER-VERSION";
        return nullptr;
    }

private:
    static bool version(std::string_view actual, std::string_view prefix) noexcept
    {
        return actual.substr(0u, prefix.size()) == prefix
            && (actual.size() == prefix.size() || actual[prefix.size()] == ' ');
    }
};

} // namespace arp::rhi::semantics
