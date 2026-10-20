#pragma once
#include "rhi/frontend/transfer_readback_borrowed_retirement.h"
#include "rhi/gpu_device.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class BorrowedTextureReadbackRetirementFixture final {
public:
    // Run inside the existing native admission instrumentation in the browser;
    // host use proves facade rejection only, never native execution semantics.
    explicit BorrowedTextureReadbackRetirementFixture(rhi::GpuDevice& device)
    {
        extension_ = static_cast<rhi::TextureReadbackExtension*>(
            device.queryInterface(rhi::kExtensionTextureReadback));
        assert(extension_);
        encoder_ = device.createCommandEncoder({"retired borrowed readback"});
        assert(encoder_);
        rhi::TextureDesc desc;
        desc.extent = {2u, 2u, 1u};
        desc.format = rhi::TextureFormat::Rgba8Unorm;
        desc.usage = rhi::TextureUsage::CopySrc;
        desc.initialState = rhi::ResourceState::CopySrc;
        source_ = device.createTexture(desc);
        assert(source_);
    }

    void verify() const
    {
        verify(*extension_, *encoder_, *source_, rhi::Backend::WebGPU);
        rhi::RetiredTextureReadbackExtension<rhi::Backend::Vulkan> vulkanFacade;
        verify(vulkanFacade, *encoder_, *source_, rhi::Backend::Vulkan);
        std::puts(
            "BINDLESS_RHI_BORROWED_READBACK_RETIRED_OK WebGPU-public Vulkan-facade schedule-poll-budget-tracked output-preserved commands0");
    }

private:
    rhi::TextureReadbackExtension* extension_ = nullptr;
    core::UniquePtr<rhi::CommandEncoder> encoder_;
    core::UniquePtr<rhi::GpuTexture> source_;

    static void verify(rhi::TextureReadbackExtension& extension,
        rhi::CommandEncoder& commands,
        const rhi::GpuTexture& texture,
        rhi::Backend backend)
    {
        std::string diagnostic;
        diagnostic.reserve(128u);
        core::pmr::String boundedDiagnostic;
        boundedDiagnostic.reserve(128u);
        rhi::TextureReadbackSample output;
        output.label = "busy";
        output.width = 37u;
        output.sampleHash = 43u;
        output.valid = true;
        output.completion = rhi::TextureReadbackCompletion::Unavailable;
        const auto count = commands.lastValidation().recordedCommandCount;
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!extension.schedule(commands, texture, "borrowed", diagnostic));
        assert(diagnostic
            == rhi::BorrowedTextureReadbackRetirementContract::lowering(backend).diagnostic);
        assert(!extension.poll(output) && output.label == "busy" && output.width == 37u
            && output.sampleHash == 43u && output.valid
            && output.completion == rhi::TextureReadbackCompletion::Unavailable);
        assert(!extension.configureBudget({}, boundedDiagnostic)
            && boundedDiagnostic == "RHI-READBACK-BOUNDED-MODE-UNSUPPORTED");
        assert(!extension.budgetStats().configured && !extension.supportsTrackedRequests());
        assert(!extension.scheduleTracked(commands, texture, "borrowed", {}, boundedDiagnostic)
            && boundedDiagnostic == "RHI-READBACK-TRACKED-MODE-UNSUPPORTED");
        assert(commands.lastValidation().recordedCommandCount == count
            && core::defaultCpuAllocator().stats().allocationCount == allocations);
    }
};
} // namespace arp::test
