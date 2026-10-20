#pragma once
#include "rhi/frontend/command_sources.h"

namespace arp::test {
// Independent native observations after original public Root completion.
class MetalSampledMaterialReadbackFixture final {
public:
    static void colorGi(
        const rhi::OriginalCommandBindlessRootSource&, const rhi::GpuTexture&, bool);
    static void hdrGi(const rhi::OriginalCommandBindlessRootSource&, const rhi::GpuTexture&, bool);
    static void woccIbl(
        const rhi::OriginalCommandBindlessRootSource&, const rhi::GpuTexture&, bool);
    static void woccIblReference(
        const rhi::OriginalCommandBindlessRootSource&, const rhi::GpuTexture&, bool);
    static void pbr(const rhi::OriginalCommandBindlessRootSource&, const rhi::GpuTexture&, bool);
    static void terrain(
        const rhi::OriginalCommandBindlessRootSource&, const rhi::GpuTexture&, bool);
};
} // namespace arp::test
