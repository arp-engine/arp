#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace arp::rhi::semantics {

// WebGlShaderPrecision/1: shader numeric intent and queried facts, no GL enum
// or native handle. Table indices are part of this versioned schema only.
enum class WebGlNumericPrecision : std::uint8_t {
    LowFloat,
    MediumFloat,
    HighFloat,
    LowInt,
    MediumInt,
    HighInt
};
inline constexpr std::size_t kWebGlNumericPrecisionCount =
    static_cast<std::size_t>(WebGlNumericPrecision::HighInt) + 1u;

struct WebGlPrecisionFormat {
    bool queried = false;
    std::int32_t rangeMin = 0;
    std::int32_t rangeMax = 0;
    std::int32_t precision = 0;
};

struct WebGlShaderPrecisionFacts {
    std::array<WebGlPrecisionFormat, kWebGlNumericPrecisionCount> vertex;
    std::array<WebGlPrecisionFormat, kWebGlNumericPrecisionCount> fragment;
};

struct WebGlPrecisionMinimum {
    bool required = false;
    std::int32_t rangeMin = 0;
    std::int32_t rangeMax = 0;
    std::int32_t precision = 0;
};

struct WebGlPrecisionRequirements {
    std::uint32_t contractVersion = 0;
    std::array<WebGlPrecisionMinimum, kWebGlNumericPrecisionCount> vertex;
    std::array<WebGlPrecisionMinimum, kWebGlNumericPrecisionCount> fragment;
};

class WebGlShaderPrecisionContract final {
public:
    static const char* validateProbe(const WebGlShaderPrecisionFacts& facts) noexcept
    {
        // Missing queries take precedence over malformed numeric records.
        for (const auto* stage : {&facts.vertex, &facts.fragment})
            for (const auto& format : *stage)
                if (!format.queried)
                    return "RHI-WEBGL1-PRECISION-PROBE-INCOMPLETE";
        for (const auto* stage : {&facts.vertex, &facts.fragment}) {
            for (std::size_t i = 0; i != stage->size(); ++i) {
                const auto& format = (*stage)[i];
                if (format.rangeMin < 0 || format.rangeMax < 0 || format.precision < 0
                    || (isInteger(i) && format.precision != 0))
                    return "RHI-WEBGL1-PRECISION-PROBE-INVALID";
            }
        }
        return nullptr;
    }

    static const char* assess(const WebGlShaderPrecisionFacts& facts,
        const WebGlPrecisionRequirements& requirements) noexcept
    {
        if (const auto* diagnostic = validateProbe(facts))
            return diagnostic;
        bool any = false;
        for (const auto* stage : {&requirements.vertex, &requirements.fragment})
            for (const auto& minimum : *stage)
                any = any || minimum.required || minimum.rangeMin != 0 || minimum.rangeMax != 0
                    || minimum.precision != 0;
        if (requirements.contractVersion != 1u && (requirements.contractVersion != 0u || any))
            return "RHI-WEBGL1-PRECISION-REQUIREMENT-VERSION";

        // Check the entire descriptor before capability comparisons. An
        // unsupported first field cannot conceal malformed later fields.
        for (const auto* stage : {&requirements.vertex, &requirements.fragment}) {
            for (std::size_t i = 0; i != stage->size(); ++i) {
                const auto& minimum = (*stage)[i];
                const bool values =
                    minimum.rangeMin != 0 || minimum.rangeMax != 0 || minimum.precision != 0;
                if (minimum.rangeMin < 0 || minimum.rangeMax < 0 || minimum.precision < 0
                    || (!minimum.required && values) || (isInteger(i) && minimum.precision != 0))
                    return "RHI-WEBGL1-PRECISION-REQUIREMENT-INVALID";
            }
        }
        if (const auto* diagnostic = assessStage(facts.vertex, requirements.vertex))
            return diagnostic;
        return assessStage(facts.fragment, requirements.fragment);
    }

private:
    static constexpr bool isInteger(std::size_t index) noexcept
    {
        return index >= static_cast<std::size_t>(WebGlNumericPrecision::LowInt);
    }

    static const char* assessStage(
        const std::array<WebGlPrecisionFormat, kWebGlNumericPrecisionCount>& facts,
        const std::array<WebGlPrecisionMinimum, kWebGlNumericPrecisionCount>& requirements) noexcept
    {
        for (std::size_t i = 0; i != facts.size(); ++i) {
            const auto& minimum = requirements[i];
            if (!minimum.required)
                continue;
            const auto& format = facts[i];
            const bool supported =
                isInteger(i) ? format.rangeMin != 0 || format.rangeMax != 0 : format.precision != 0;
            if (!supported)
                return "RHI-WEBGL1-PRECISION-UNSUPPORTED";
            if (minimum.rangeMin > format.rangeMin || minimum.rangeMax > format.rangeMax
                || minimum.precision > format.precision)
                return "RHI-WEBGL1-PRECISION-INSUFFICIENT";
        }
        return nullptr;
    }
};

} // namespace arp::rhi::semantics
