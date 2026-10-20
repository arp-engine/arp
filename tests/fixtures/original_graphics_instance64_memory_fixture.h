#pragma once
#include "original_graphics_runtime_vector_memory_fixture.h"

namespace arp::test {
class OriginalGraphicsInstance64MemoryFixture final {
public:
    template <class Root, class Sources>
    static void inspect(const Root& root,
        const Sources& sources,
        rhi::GpuDevice* device,
        bool fault,
        bool undersized = false)
    {
        OriginalGraphicsRuntimeVectorMemoryFixture::inspect(
            root, sources, device, fault, 64u, 4u, 224u, undersized);
        if (device)
            std::puts(
                "BINDLESS_INSTANCE64_ROOT_MEMORY2_OK stride64 capacity4 static224 VS-FS original-owner hot0");
    }
};
} // namespace arp::test
