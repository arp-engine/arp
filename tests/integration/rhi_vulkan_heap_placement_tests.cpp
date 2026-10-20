#include "tests/fixtures/vulkan_heap_placement_fixture.h"

#include <cstring>

int main(int argc, char** argv)
{
    using namespace arp;
    if (argc == 2 && std::string_view(argv[1]).substr(0u, 7u) == "native-")
        return tests::VulkanHeapPlacementFixture::run(argv[1]);
    tests::VulkanHeadlessRuntime native;
    if (!native.runtime() || !native.runtime()->valid())
        return 77;
    auto device = tests::VulkanHeapOwnerFixture::makeDevice(native.runtime());
    assert(device && device->capabilities().features.transientAliasing);
    rhi::TextureDesc desc;
    desc.extent = {4u, 4u, 1u};
    desc.format = rhi::TextureFormat::Rgba8Unorm;
    desc.usage = rhi::TextureUsage::Sampled;
    assert(argc == 2);
    if (std::strcmp(argv[1], "admission") == 0) {
        auto invalidAlignment = device->createMemoryHeap(1024u, 0u, rhi::MemoryUsage::Transient);
        auto invalidIntent = device->createMemoryHeap(1024u, 256u, rhi::MemoryUsage::CpuToGpu);
        auto disabled = tests::VulkanHeapOwnerFixture::makeDevice(native.runtime(), false);
        assert(disabled && !disabled->capabilities().features.transientAliasing);
        auto forbidden = disabled->createMemoryHeap(1024u, 256u, rhi::MemoryUsage::Transient);
        auto bounded = tests::VulkanHeapOwnerFixture::makeDevice(native.runtime(), true, 1u);
        assert(bounded);
        auto overBudget = bounded->createMemoryHeap(1024u, 256u, rhi::MemoryUsage::Transient);
        const bool rejected = !invalidAlignment && !invalidIntent && !forbidden && !overBudget;
        std::printf("heap admission: alignment=%d intent=%d disabled=%d budget=%d\n",
            !invalidAlignment,
            !invalidIntent,
            !forbidden,
            !overBudget);
        std::fflush(stdout);
        assert(rejected);
    } else if (std::strcmp(argv[1], "immediate") == 0) {
        const auto requirements = device->queryTexturePlacementRequirements(desc);
        assert(requirements.sizeBytes && requirements.alignmentBytes);
        auto heap = device->createMemoryHeap(
            requirements.sizeBytes, requirements.alignmentBytes, rhi::MemoryUsage::Transient);
        assert(heap);
        const auto actual = device->memoryStats().allocatedBytes;
        std::printf("heap immediate accounting: bytes=%llu requested=%llu\n",
            static_cast<unsigned long long>(actual),
            static_cast<unsigned long long>(requirements.sizeBytes));
        std::fflush(stdout);
        assert(actual >= requirements.sizeBytes);
    } else {
        assert(std::strcmp(argv[1], "query") == 0);
        desc.memory = rhi::MemoryUsage::GpuToCpu;
        const auto invalid = device->queryTexturePlacementRequirements(desc);
        desc.memory = rhi::MemoryUsage::GpuOnly;
        auto disabled = tests::VulkanHeapOwnerFixture::makeDevice(native.runtime(), false);
        const auto forbidden = disabled->queryTexturePlacementRequirements(desc);
        std::printf("heap query refusal: intent=%llu disabled=%llu\n",
            static_cast<unsigned long long>(invalid.sizeBytes),
            static_cast<unsigned long long>(forbidden.sizeBytes));
        std::fflush(stdout);
        assert(!invalid.sizeBytes && !invalid.alignmentBytes && !forbidden.sizeBytes
            && !forbidden.alignmentBytes);
    }
}
