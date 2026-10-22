#pragma once

#include "core/io/file_io.h"
#include "rhi/frontend/command_attachment_validation.h"
#include "rhi/gpu_device.h"

#include <cassert>
#include <limits>

namespace arp::test {
class DepthStencilAttachmentInputFixture final {
public:
    static void verifyContract()
    {
        using namespace rhi;
        using Contract = semantics::DepthStencilAttachmentInputContract;
        static_assert(Contract::version == 1u);
        RenderPassDepthStencilAttachmentDesc desc;
        assert(desc.depthClearValue == 1.0f && !desc.stencilClearValue && !desc.depthReadOnly
            && !desc.stencilReadOnly);
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (const auto load : {LoadOp::Load, LoadOp::Clear, LoadOp::DontCare})
            for (const auto store : {StoreOp::Store, StoreOp::DontCare}) {
                desc.depthLoadOp = desc.stencilLoadOp = load;
                desc.depthStoreOp = desc.stencilStoreOp = store;
                for (const auto depth : {0.0f, 0.125f, 0.375f, 0.875f, 1.0f}) {
                    desc.depthClearValue = depth;
                    for (const auto stencil : {0u, 73u, 255u}) {
                        desc.stencilClearValue = stencil;
                        assert(!Contract::diagnostic(desc));
                    }
                }
                for (const auto bad : {-0.125f,
                         1.125f,
                         std::numeric_limits<float>::quiet_NaN(),
                         std::numeric_limits<float>::infinity(),
                         -std::numeric_limits<float>::infinity()}) {
                    desc.depthClearValue = bad;
                    assert(std::string_view(Contract::diagnostic(desc))
                        == "RHI-RENDER-PASS-DEPTH-CLEAR-INVALID");
                }
                desc.depthClearValue = 0.375f;
                desc.stencilClearValue = 256u;
                assert(std::string_view(Contract::diagnostic(desc))
                    == "RHI-RENDER-PASS-STENCIL-CLEAR-INVALID");
                desc.stencilClearValue = 73u;
                for (unsigned mask = 1u; mask != 4u; ++mask) {
                    desc.depthReadOnly = (mask & 1u) != 0u;
                    desc.stencilReadOnly = (mask & 2u) != 0u;
                    assert(std::string_view(Contract::diagnostic(desc))
                        == "RHI-RENDER-PASS-DEPTH-STENCIL-READ-ONLY-UNPROVEN");
                }
                desc.depthReadOnly = desc.stencilReadOnly = false;
            }
        desc.depthLoadOp = static_cast<LoadOp>(UINT32_MAX);
        assert(std::string_view(Contract::diagnostic(desc))
            == "RHI-RENDER-PASS-ATTACHMENT-OP-INVALID");
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    }

    static void verifyLoweringSources()
    {
#if defined(ARP_SOURCE_DIR) && !defined(__EMSCRIPTEN__)
        const auto root = std::filesystem::path(ARP_SOURCE_DIR) / "src/rhi/backends";
        const auto vulkan = core::readTextFile(root / "vulkan/vulkan_native_graphics_commands.cpp");
        const auto metal = core::readTextFile(root / "metal/metal_native_commands.mm");
        const auto webgpu =
            core::readTextFile(root / "webgpu/webgpu_attachment_operation_lowering.h");
        assert(vulkan.success && metal.success && webgpu.success);
        const std::string_view depth = "desc.depthStencilAttachment.depthClearValue";
        const std::string_view stencil = "desc.depthStencilAttachment.stencilClearValue";
        for (const auto field : {depth, stencil}) {
            const auto first = vulkan.text.find(field);
            assert(first != vulkan.text.npos);
            const auto second = vulkan.text.find(field, first + field.size());
            assert(second != vulkan.text.npos);
            assert(vulkan.text.find(field, second + field.size()) == vulkan.text.npos);
            assert(metal.text.find(field) != metal.text.npos);
        }
        assert(
            webgpu.text.find("lowered.depthClearValue = desc.depthClearValue") != webgpu.text.npos);
        assert(webgpu.text.find("lowered.stencilClearValue = desc.stencilClearValue")
            != webgpu.text.npos);
        // This protects mapping sites; it does not prove a Vulkan native payload.
#endif
    }

    static void verifyPublicGate(rhi::GpuDevice& device)
    {
        using namespace rhi;
        // Negative input tests do not need to fabricate or bind a native target.
        // Input diagnostics must precede attachment lookup and pass mutation.
        RenderPassDesc pass;
        pass.hasDepthStencilAttachment = true;
        pass.depthStencilAttachment.target = allocateGpuResourceId();
        pass.depthStencilAttachment.format = TextureFormat::D32Float;
        const auto reject = [&](const char* id) {
            auto encoder = device.createCommandEncoder({"DepthStencilInputNegative"});
            assert(encoder);
            const auto count = encoder->lastValidation().recordedCommandCount;
            assert(!encoder->beginRenderPass(pass));
            const auto result = encoder->lastValidation();
            assert(!result.success && result.diagnostics == id
                && result.recordedCommandCount == count);
        };
        pass.depthStencilAttachment.depthClearValue = std::numeric_limits<float>::quiet_NaN();
        reject("RHI-RENDER-PASS-DEPTH-CLEAR-INVALID");
        pass.depthStencilAttachment.depthClearValue = 0.375f;
        pass.depthStencilAttachment.stencilClearValue = 256u;
        reject("RHI-RENDER-PASS-STENCIL-CLEAR-INVALID");
        pass.depthStencilAttachment.stencilClearValue = 73u;
        pass.depthStencilAttachment.depthReadOnly = true;
        reject("RHI-RENDER-PASS-DEPTH-STENCIL-READ-ONLY-UNPROVEN");
        pass.depthStencilAttachment.depthReadOnly = false;
        pass.depthStencilAttachment.stencilReadOnly = true;
        reject("RHI-RENDER-PASS-DEPTH-STENCIL-READ-ONLY-UNPROVEN");
    }
};
} // namespace arp::test
