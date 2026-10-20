#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "tests/fixtures/retained_allocation_contract.h"

#include <cstdio>

int main()
{
    using namespace arp::rhi;
    auto device = createNullDevice();
    const auto baseline = device->memoryStats().allocatedBytes;
    auto buffer = device->createBuffer({"NullOwnedAllocation",
        256u,
        ResourceUsage::CopySrc,
        MemoryUsage::CpuToGpu,
        ResourceState::CopySrc});
    assert(buffer && device->memoryStats().allocatedBytes == baseline + 256u);
    buffer.reset();
    assert(device->memoryStats().allocatedBytes == baseline);
    std::puts("RetainedAllocationBudget/1 Null validator allocation: PASS");

    using namespace arp::rhi_webgpu;
    arp::tests::verifyRetainedAllocationPayload<WebGpuNativeTexturePayload>(
        [](auto charge, auto payload) {
            WebGpuRhiAllocation allocation;
            allocation.success = true;
            allocation.sizeBytes = 4u;
            allocation.releaseToken = std::move(charge);
            return createWebGpuRhiTexture({1u},
                {"WebOwnershipFixture",
                    {1u, 1u, 1u},
                    TextureFormat::Rgba8Unorm,
                    TextureUsage::Sampled,
                    MemoryUsage::GpuOnly,
                    ResourceState::Undefined},
                allocation,
                std::move(payload),
                nullptr);
        },
        TextureDesc{"WebOwnershipFixture",
            {1u, 1u, 1u},
            TextureFormat::Rgba8Unorm,
            TextureUsage::Sampled,
            MemoryUsage::GpuOnly,
            ResourceState::Undefined});
    std::puts("RetainedAllocationBudget/1 WebGPU ownership fixture (no native runtime): PASS");
    // Mirror the real cached-view ownership graph without native handles:
    // commands/public views retain the parent; an idle cache must not cycle.
    auto parent = arp::core::makeShared<WebGpuNativeTexturePayload>();
    auto view = arp::core::makeShared<WebGpuNativeTextureViewPayload>();
    std::weak_ptr<WebGpuNativeTexturePayload> parentLifetime = parent;
    view->parentTexture = parent;
    parent->viewCache.push_back({{}, view});
    parent.reset();
    assert(!parentLifetime.expired());
    view.reset();
    assert(parentLifetime.expired());

    // A still-live parent must release the expired child's weak control block,
    // including its original allocation quota. Parent destruction alone hid
    // this retention in the older cycle-only check.
    parent = arp::core::makeShared<WebGpuNativeTexturePayload>();
    auto quota = arp::core::makeShared<arp::core::CpuMemoryQuota>(4096u);
    {
        arp::core::CpuMemoryQuotaScope allocation(quota);
        view = arp::core::makeShared<WebGpuNativeTextureViewPayload>();
        view->parentTexture = parent;
        parent->viewCache.push_back({{}, view});
    }
    assert(quota->liveBytes());
    view.reset();
    assert(parent->viewCache.empty() && !quota->liveBytes());
}
