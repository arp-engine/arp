#include "rhi/backends/metal/metal_allocator.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/vulkan/vulkan_allocator.h"
#include "rhi/backends/webgpu/webgpu_allocator.h"
#include "tests/fixtures/cpu_quota_factory_fixture.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace arp::tests {
class AllocationChargeRollbackFixture final {
public:
    template <class Allocator, class AllocationDesc>
    static void verify(Allocator& allocator, const AllocationDesc& desc, const char* name)
    {
        auto unrelated = allocator.allocate(desc);
        assert(unrelated.success && unrelated.releaseToken);
        const auto baseline = allocator.memoryStats();
        auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
        const auto factory = [&]() -> std::shared_ptr<void> {
            std::shared_ptr<void> result;
            try {
                core::CpuMemoryQuotaScope scope(quota);
                auto allocation = allocator.allocate(desc);
                assert(allocation.success && allocation.releaseToken);
                assert(allocator.memoryStats().allocatedBytes
                    == baseline.allocatedBytes + desc.sizeBytes);
                assert(
                    allocator.memoryStats().liveResourceCount == baseline.liveResourceCount + 1u);
                result = std::move(allocation.releaseToken);
            } catch (const std::bad_alloc&) {
                // An existing charge must survive a failed *different* owner.
                assert(allocator.memoryStats().allocatedBytes == baseline.allocatedBytes);
                assert(allocator.memoryStats().liveResourceCount == baseline.liveResourceCount);
            }
            return result;
        };
        test::CpuQuotaFactoryFixture pressure(quota);
        const auto exact = pressure.measureSingleUse([] {}, factory);
        assert(exact < quota->limitBytes());
        for (std::uint64_t available = 0u; available <= exact; ++available) {
            {
                core::CpuMemoryQuotaLease occupied(quota, quota->limitBytes() - available);
                assert(occupied.charged());
                auto token = factory();
                assert(static_cast<bool>(token) == (available == exact));
                if (token) {
                    auto last = token;
                    token.reset();
                    assert(allocator.memoryStats().allocatedBytes
                        == baseline.allocatedBytes + desc.sizeBytes);
                    last.reset();
                }
            }
            assert(quota->liveBytes() == 0u);
            assert(allocator.memoryStats().allocatedBytes == baseline.allocatedBytes);
            assert(allocator.memoryStats().liveResourceCount == baseline.liveResourceCount);
        }
        unrelated.releaseToken.reset();
        assert(allocator.memoryStats().allocatedBytes == 0u);
        assert(allocator.memoryStats().liveResourceCount == 0u);
        std::printf(
            "allocation charge %s: exact CPU bytes=%llu; all failure boundaries rolled back\n",
            name,
            static_cast<unsigned long long>(exact));
    }

    template <class Factory> static void verifyNull(Factory factory, const char* name)
    {
        const rhi::BufferDesc desc{"quota",
            256u,
            rhi::ResourceUsage::CopySrc,
            rhi::MemoryUsage::CpuToGpu,
            rhi::ResourceState::CopySrc};
        auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
        const auto attempt = [&]() {
            auto device = rhi::createNullDevice();
            auto buffer = device->createBuffer(desc);
            rhi::TextureDesc td;
            td.extent = {2u, 2u, 1u};
            td.format = rhi::TextureFormat::Rgba8Unorm;
            td.usage = rhi::TextureUsage::Sampled;
            auto texture = device->createTexture(td);
            assert(buffer && texture);
            const auto baseline = device->memoryStats();
            bool created = false;
            try {
                core::CpuMemoryQuotaScope scope(quota);
                auto resource = factory(*device, *buffer, *texture);
                created = static_cast<bool>(resource);
            } catch (const std::bad_alloc&) {
            }
            const auto after = device->memoryStats();
            if (after.allocatedBytes != baseline.allocatedBytes
                || after.liveResourceCount != baseline.liveResourceCount) {
                std::printf("Null %s: created=%d bytes=%llu expected=%llu count=%u expected=%u\n",
                    name,
                    created,
                    static_cast<unsigned long long>(after.allocatedBytes),
                    static_cast<unsigned long long>(baseline.allocatedBytes),
                    after.liveResourceCount,
                    baseline.liveResourceCount);
                std::fflush(stdout);
            }
            assert(after.allocatedBytes == baseline.allocatedBytes);
            assert(after.liveResourceCount == baseline.liveResourceCount);
            // The registry owns its vector capacity until device teardown.
            // Do not mistake that declared owner for a leaked failed wrapper.
            return created;
        };
        assert(attempt() && quota->liveBytes() == 0u);
        const auto exact = quota->peakBytes();
        assert(exact && exact < quota->limitBytes());
        for (std::uint64_t available = 0u; available <= exact; ++available) {
            {
                core::CpuMemoryQuotaLease occupied(quota, quota->limitBytes() - available);
                assert(occupied.charged());
                assert(attempt() == (available == exact));
            }
            assert(quota->liveBytes() == 0u);
        }
        std::printf("allocation charge Null %s: validator-only rollback; exact CPU=%llu\n",
            name,
            static_cast<unsigned long long>(exact));
    }
};
} // namespace arp::tests

int main(int argc, char** argv)
{
    using namespace arp;
    assert(argc == 1 || argc == 2);
    const auto selected = [&](const char* name) {
        return argc == 1 || std::strcmp(argv[1], name) == 0;
    };
    assert(selected("null") || selected("vulkan") || selected("webgpu") || selected("metal"));
    if (selected("null")) {
        tests::AllocationChargeRollbackFixture::verifyNull(
            [](auto& device, auto& buffer, auto&) {
                return device.createBuffer(buffer.desc());
            },
            "buffer");
        tests::AllocationChargeRollbackFixture::verifyNull(
            [](auto& device, auto& buffer, auto&) {
                rhi::BufferViewDesc desc;
                desc.sizeBytes = buffer.desc().sizeBytes;
                desc.strideBytes = 4u;
                return device.createBufferView(buffer, desc);
            },
            "buffer view");
        tests::AllocationChargeRollbackFixture::verifyNull(
            [](auto& device, auto&, auto& texture) {
                return device.createTexture(texture.desc());
            },
            "texture");
        tests::AllocationChargeRollbackFixture::verifyNull(
            [](auto& device, auto&, auto& texture) {
                rhi::TextureViewDesc desc;
                desc.format = texture.desc().format;
                desc.usage = texture.desc().usage;
                return device.createTextureView(texture, desc);
            },
            "texture view");
        tests::AllocationChargeRollbackFixture::verifyNull(
            [](auto& device, auto&, auto&) {
                return device.createSampler({});
            },
            "sampler");
    }
    for (const auto* label :
        {"quota", "allocation charge long label copied into the exact release token"}) {
        if (selected("vulkan")) {
            rhi_vulkan::VulkanRhiAllocator vulkan({"quota", 4096u});
            tests::AllocationChargeRollbackFixture::verify(vulkan,
                rhi_vulkan::VulkanRhiAllocationDesc{label, 256u, rhi::MemoryUsage::GpuOnly},
                "Vulkan metadata");
        }
        if (selected("webgpu")) {
            rhi_webgpu::WebGpuRhiAllocator webgpu({"quota", 4096u});
            tests::AllocationChargeRollbackFixture::verify(webgpu,
                rhi_webgpu::WebGpuRhiAllocationDesc{label, 256u, rhi::MemoryUsage::GpuOnly},
                "WebGPU metadata");
        }
        if (selected("metal")) {
            rhi_metal::MetalRhiAllocator metal({"quota", 4096u});
            tests::AllocationChargeRollbackFixture::verify(metal,
                rhi_metal::MetalRhiAllocationDesc{label, 256u, rhi::MemoryUsage::GpuOnly},
                "Metal metadata");
        }
    }
}
