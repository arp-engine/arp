#pragma once
#include "original_ordinary_depth_graphics_execution_fixture.h"
#include "rhi/backends/metal/metal_original_bindless_graphics_pass.h"

namespace arp::test {
struct MetalOrdinaryDepthGraphicsBackendFixture {
    inline static bool captureSubmittedOnly = false;
    using Closure = rhi_metal::MetalRhiBindlessRootNativeClosure;
    using Pass = rhi_metal::MetalOriginalBindlessGraphicsPass;
    static constexpr std::uint32_t passNativeObjects = 1u;
    static constexpr std::uint32_t submissionOwnerOverhead = 6u;
    static constexpr const char* ownerCapacityDiagnostic = "RHI-METAL-ROOT-COMMAND-OWNER-CAPACITY";

    static void readValues(
        const rhi::GpuBuffer& buffer, std::array<float, 40u>& values, core::pmr::String& diagnostic)
    {
        assert(rhi_metal::readMetalRhiNativeBuffer(rhi_metal::metalRhiNativePayloadFor(buffer),
            values.data(),
            sizeof(values),
            0u,
            buffer.desc().sizeBytes,
            diagnostic));
    }

    static void verifyOutput(const rhi::OriginalCommandResourceSources&, bool);
};

using MetalOrdinaryDepthGraphicsExecutionFixture =
    OriginalOrdinaryDepthGraphicsExecutionFixture<MetalOrdinaryDepthGraphicsBackendFixture>;
} // namespace arp::test
