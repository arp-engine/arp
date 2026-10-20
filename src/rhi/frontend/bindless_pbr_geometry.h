#pragma once
#include "core/unsigned_range.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <type_traits>

namespace arp::rhi {
struct alignas(16) BindlessPbrWind64 {
    std::array<float, 4u> directionSpeed{}, timeGustTurb{}, flutterParams{}, interactionCenter{};
};

// Matrices retain the established default Slang row-memory ABI. Callers pack
// animation column-memory through the existing matrix conversion contract.
struct alignas(16) BindlessPbrGeometryFrame400 {
    std::array<float, 16u> renderViewProjection{}, currentViewProjection{},
        previousViewProjection{}, shadowViewProjection{};
    BindlessPbrWind64 currentWind, previousWind;
    std::array<std::uint32_t, 4u> outputExtent{};
};

struct BindlessPbrGeometryDraw16 {
    std::uint32_t material = 0u, instanceBase = 0u, reserved0 = 0u, reserved1 = 0u;
};

struct alignas(16) BindlessPbrSkinPalette16432 {
    std::array<std::array<float, 16u>, 256u> matrices{};
    std::array<float, 4u> worldTranslationAndScale{}, worldRotation{};
    std::uint32_t jointCount = 0u, reserved0 = 0u, reserved1 = 0u, reserved2 = 0u;
};

struct alignas(16) BindlessPbrTemporalPalette32864 {
    BindlessPbrSkinPalette16432 current, previous;
};

static_assert(std::is_trivially_copyable_v<BindlessPbrTemporalPalette32864>
    && sizeof(BindlessPbrSkinPalette16432) == 16432u
    && offsetof(BindlessPbrSkinPalette16432, worldTranslationAndScale) == 16384u
    && offsetof(BindlessPbrSkinPalette16432, worldRotation) == 16400u
    && offsetof(BindlessPbrSkinPalette16432, jointCount) == 16416u
    && sizeof(BindlessPbrTemporalPalette32864) == 32864u
    && offsetof(BindlessPbrTemporalPalette32864, previous) == 16432u);

static_assert(std::is_trivially_copyable_v<BindlessPbrGeometryFrame400>);
static_assert(sizeof(BindlessPbrWind64) == 64u && alignof(BindlessPbrWind64) == 16u);
static_assert(sizeof(BindlessPbrGeometryFrame400) == 400u
    && offsetof(BindlessPbrGeometryFrame400, currentViewProjection) == 64u
    && offsetof(BindlessPbrGeometryFrame400, previousViewProjection) == 128u
    && offsetof(BindlessPbrGeometryFrame400, shadowViewProjection) == 192u
    && offsetof(BindlessPbrGeometryFrame400, currentWind) == 256u
    && offsetof(BindlessPbrGeometryFrame400, previousWind) == 320u
    && offsetof(BindlessPbrGeometryFrame400, outputExtent) == 384u);
static_assert(sizeof(BindlessPbrGeometryDraw16) == 16u
    && offsetof(BindlessPbrGeometryDraw16, instanceBase) == 4u
    && offsetof(BindlessPbrGeometryDraw16, reserved0) == 8u
    && offsetof(BindlessPbrGeometryDraw16, reserved1) == 12u);

// Value checks only; native admission must obtain frame bytes and the actual
// draw/fetch maxima from original initialized actors, pipeline and pass. These
// functions cannot turn caller-authored capacities into content authority.
class BindlessPbrGeometryContract final {
public:
    static constexpr std::uint32_t version = 1u, numericLanesPerPixel = 23u;

    static const char* validatePalette(const BindlessPbrTemporalPalette32864& palettes) noexcept
    {
        for (const auto* palette : {&palettes.current, &palettes.previous}) {
            if (!palette->jointCount || palette->jointCount > palette->matrices.size()
                || palette->reserved0 || palette->reserved1 || palette->reserved2)
                return "RHI-BINDLESS-PBR-GEOMETRY-PALETTE-SCHEMA";
            for (const auto* vector : {&palette->worldTranslationAndScale, &palette->worldRotation})
                for (const auto lane : *vector)
                    if (!std::isfinite(lane))
                        return "RHI-BINDLESS-PBR-GEOMETRY-PALETTE-TRANSFORM";
            if (palette->worldTranslationAndScale[3u] <= 0.f)
                return "RHI-BINDLESS-PBR-GEOMETRY-PALETTE-SCALE";
            // The authored rotation formula consumes a unit quaternion.
            double norm = 0.;
            for (const auto lane : palette->worldRotation)
                norm += double(lane) * lane;
            if (std::abs(norm - 1.) > 0.0001)
                return "RHI-BINDLESS-PBR-GEOMETRY-PALETTE-ROTATION";
            for (std::uint32_t joint = 0u; joint < palette->jointCount; ++joint)
                for (const auto lane : palette->matrices[joint])
                    if (!std::isfinite(lane))
                        return "RHI-BINDLESS-PBR-GEOMETRY-PALETTE-MATRIX";
        }
        return nullptr;
    }

    static const char* validateFrame(const BindlessPbrGeometryFrame400& frame,
        std::uint32_t attachmentWidth,
        std::uint32_t attachmentHeight,
        std::uint64_t resultBytes) noexcept
    {
        for (const auto* matrix : {&frame.renderViewProjection,
                 &frame.currentViewProjection,
                 &frame.previousViewProjection,
                 &frame.shadowViewProjection})
            for (const auto lane : *matrix)
                if (!std::isfinite(lane))
                    return "RHI-BINDLESS-PBR-GEOMETRY-MATRIX";
        for (const auto* wind : {&frame.currentWind, &frame.previousWind})
            for (const auto* vector : {&wind->directionSpeed,
                     &wind->timeGustTurb,
                     &wind->flutterParams,
                     &wind->interactionCenter})
                for (const auto lane : *vector)
                    if (!std::isfinite(lane))
                        return "RHI-BINDLESS-PBR-GEOMETRY-WIND";
        const auto& extent = frame.outputExtent;
        if (!attachmentWidth || !attachmentHeight || extent[0u] != attachmentWidth
            || extent[1u] != attachmentHeight || extent[2u] != numericLanesPerPixel
            || extent[3u] != version || attachmentWidth > UINT32_MAX / attachmentHeight)
            return "RHI-BINDLESS-PBR-GEOMETRY-EXTENT";
        const auto pixels = std::uint64_t(attachmentWidth) * attachmentHeight;
        if (pixels > UINT32_MAX / numericLanesPerPixel
            || pixels * numericLanesPerPixel > resultBytes / sizeof(std::uint32_t))
            return "RHI-BINDLESS-PBR-GEOMETRY-RESULT-CAPACITY";
        return nullptr;
    }

    static const char* validateDraw(const BindlessPbrGeometryDraw16& draw,
        std::uint32_t materialRecords,
        std::uint32_t initializedInstances,
        std::uint32_t firstInstance,
        std::uint32_t instanceCount,
        bool usesInstances) noexcept
    {
        if (draw.reserved0 || draw.reserved1 || draw.material >= materialRecords || !instanceCount)
            return "RHI-BINDLESS-PBR-GEOMETRY-SELECTION";
        if (!usesInstances)
            return draw.instanceBase || firstInstance || instanceCount != 1u
                ? "RHI-BINDLESS-PBR-GEOMETRY-WORLD-DRAW"
                : nullptr;
        if (draw.instanceBase > initializedInstances
            || !core::containsUnsignedRange<std::uint64_t>(
                0u, initializedInstances - draw.instanceBase, firstInstance, instanceCount))
            return "RHI-BINDLESS-PBR-GEOMETRY-INSTANCE-RANGE";
        return nullptr;
    }
};
} // namespace arp::rhi
