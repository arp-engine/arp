#pragma once
#include "renderer/bindless/bindless_pbr_geometry.h"

namespace arp::test {
using namespace renderer;

class BindlessGeometryNumericContract final {
public:
    static constexpr std::uint32_t numericLanesPerPixel = 23u;

    static const char* validateFrame(const renderer::BindlessPbrGeometryFrame400& frame,
        std::uint32_t width,
        std::uint32_t height,
        std::uint64_t resultBytes) noexcept
    {
        if (const auto* id =
                renderer::BindlessPbrGeometryContract::validateFrame(frame, width, height))
            return id;
        if (frame.outputExtent[2] != numericLanesPerPixel || frame.outputExtent[3] != 1u
            || width > UINT32_MAX / height)
            return "RHI-BINDLESS-PBR-GEOMETRY-EXTENT";
        const auto pixels = std::uint64_t(width) * height;
        if (pixels > UINT32_MAX / numericLanesPerPixel
            || pixels * numericLanesPerPixel > resultBytes / sizeof(std::uint32_t))
            return "RHI-BINDLESS-PBR-GEOMETRY-RESULT-CAPACITY";
        return nullptr;
    }
};
} // namespace arp::test
