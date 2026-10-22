#pragma once
#include "core/math/half_float.h"
#include "rhi/frontend/command_sources.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace arp::test {
// Authored finite diagnostic input, identical on all platforms. Each image
// and array layer has distinct bytes; both mips are constant within a layer.
// This oracle checks actual material/attachment output, not sampling gradients
// or production lighting. The independent sampling suites cover varying mips.
class BindlessSceneAttachmentFixture final {
public:
    using Observer = void (*)(const rhi::GpuTexture&, const char*, bool, const void*, bool);
    static void observeMetal(const rhi::GpuTexture&, const char*, bool, const void*, bool);

    static std::array<std::uint8_t, 4u> texel(std::uint32_t image, std::uint32_t layer = 0u)
    {
        assert(image < 27u && layer < 2u);
        return {static_cast<std::uint8_t>(16u + 7u * image + 3u * layer),
            static_cast<std::uint8_t>(64u + 3u * image + 5u * layer),
            static_cast<std::uint8_t>(128u + image + 7u * layer),
            255u};
    }

    static void initializeTexturePixels(
        std::uint32_t image, std::uint32_t layers, core::pmr::Vector<std::uint8_t>& bytes)
    {
        assert((layers == 1u || layers == 2u) && bytes.size() == 20u * layers);
        for (std::uint32_t layer = 0u; layer < layers; ++layer) {
            const auto value = texel(image, layer);
            for (std::uint32_t pixel = 0u; pixel < 4u; ++pixel)
                std::memcpy(bytes.data() + layer * 16u + pixel * 4u, value.data(), 4u);
            std::memcpy(bytes.data() + 16u * layers + layer * 4u, value.data(), 4u);
        }
    }

    static std::array<float, 4u> expectedColor(bool terrain, float worldY)
    {
        if (!terrain) {
            const auto base = texel(5u); // Draw16 selects Material96[1].
            return {base[0u] / 255.f * .25f, base[1u] / 255.f * .5f, base[2u] / 255.f * .75f, 1.f};
        }
        // Page32 selects layer 1 of both arrays. Weather/foam/brush are off.
        const auto a = texel(0u, 1u), b = texel(1u, 1u);
        std::array<double, 6u> weights{double(a[0u]),
            double(a[1u]),
            double(a[2u]),
            double(a[3u]),
            double(b[0u]),
            double(b[1u])};
        double sum = 0.;
        for (const auto weight : weights)
            sum += weight;
        for (auto& weight : weights)
            weight /= sum;
        const auto snow = (b[2u] / 255.) * std::clamp((double(worldY) - .25), 0., 1.) * .7;
        weights[5u] += snow;
        weights[0u] *= 1. - snow;
        weights[4u] *= 1. - snow * .8;
        const auto wet = b[3u] / 255.;
        weights[2u] += wet * .3;
        weights[0u] *= 1. - wet * .15;
        sum = 0.;
        for (const auto weight : weights)
            sum += weight;
        for (auto& weight : weights)
            weight /= sum;
        const auto macro = texel(24u)[0u] / 255.;
        const auto macroGain = (.92 + .16 * macro) * (1. - .21 * wet);
        const auto tint = (macro - .5) * .5 * weights[0u];
        constexpr std::array<double, 3u> tintDelta{.07, .03, -.14};
        std::array<float, 4u> result{0.f, 0.f, 0.f, 1.f};
        for (std::size_t channel = 0u; channel < 3u; ++channel) {
            double color = 0.;
            for (std::uint32_t layer = 0u; layer < 6u; ++layer)
                if (weights[layer] > .003)
                    color += texel(layer * 4u)[channel] / 255. * weights[layer];
            result[channel] =
                static_cast<float>(color * macroGain * (1. + tintDelta[channel] * tint));
        }
        return result;
    }

    static const rhi::GpuTexture& completedTarget(
        const rhi::OriginalCommandResourceSources& inventory)
    {
        const rhi::GpuTexture* target = nullptr;
        for (const auto& source : inventory.sources()) {
            const auto* view =
                std::get_if<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(&source);
            if (!view
                || (*view)->originalObject()->desc().usage != rhi::TextureUsage::RenderAttachment)
                continue;
            assert(!target);
            target = (*view)->originalParentLease()->originalObject();
        }
        assert(target);
        const auto content =
            target->observeOriginalContent(rhi::defaultTextureSubresourceRange(target->desc()));
        assert(content
            && content->state() == rhi::common::OriginalTextureContentControl::State::Completed);
        return *target;
    }

    // Packed rows only. Backend diagnostics copy after real completion, retain
    // the original texture and restore native layout; no L2 authority is issued.
    static const char* inspect(const void* bytes,
        std::size_t size,
        bool depth,
        bool terrain,
        const void* geometry,
        bool fault = false)
    {
        if (!bytes || size != (depth ? 16u : 32u) || (!depth && !fault && !geometry))
            return "SCENE-ATTACHMENT-SIZE";
        for (std::size_t pixel = 0u; pixel < 4u; ++pixel) {
            if (depth) {
                float actual;
                std::memcpy(&actual, static_cast<const std::byte*>(bytes) + pixel * 4u, 4u);
                if (actual != (fault ? 1.f : .5f))
                    return "SCENE-ATTACHMENT-DEPTH";
                continue;
            }
            float y = 0.f;
            if (!fault)
                std::memcpy(
                    &y, static_cast<const std::byte*>(geometry) + (pixel * 23u + 1u) * 4u, 4u);
            const auto expected =
                fault ? std::array<float, 4u>{0.f, 0.f, 0.f, 1.f} : expectedColor(terrain, y);
            for (std::size_t channel = 0u; channel < 4u; ++channel) {
                std::uint16_t half;
                std::memcpy(
                    &half, static_cast<const std::byte*>(bytes) + (pixel * 4u + channel) * 2u, 2u);
                const auto actual = core::halfToFloat(half);
                // One half-float relative ULP plus float arithmetic allowance.
                const auto tolerance = std::abs(expected[channel]) / 1024.f + .00002f;
                if (!std::isfinite(actual) || std::abs(actual - expected[channel]) > tolerance)
                    return "SCENE-ATTACHMENT-MATERIAL";
            }
        }
        return nullptr;
    }

    static void inspectAndReport(const rhi::GpuTexture& texture,
        const char* entry,
        bool terrain,
        const void* geometry,
        const void* packed,
        std::size_t bytes,
        bool fault = false)
    {
        const bool depth = texture.desc().format == rhi::TextureFormat::D32Float;
        assert(depth || texture.desc().format == rhi::TextureFormat::Rgba16Float);
        const auto* id = inspect(packed, bytes, depth, terrain, geometry, fault);
        if (id)
            std::fprintf(stderr, "%s entry=%s\n", id, entry);
        assert(!id);
        std::printf("BINDLESS_SCENE_ATTACHMENT_BYTES entry=%s fault=%u format=%s bytes=",
            entry,
            fault ? 1u : 0u,
            depth ? "d32" : "rgba16f");
        for (std::size_t i = 0u; i < bytes; ++i)
            std::printf(i ? ",%u" : "%u", static_cast<const std::uint8_t*>(packed)[i]);
        std::puts(" observer=completed-native-diagnostic");
    }
};
} // namespace arp::test
