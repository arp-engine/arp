#pragma once
#include "core/io/file_io.h"
#include "rhi/gpu_device.h"

#include <cassert>
#include <cstdio>

namespace arp::tests {
class ConformanceClearNativeNegatives final {
public:
    static void verify(rhi::GpuDevice& device, rhi::Swapchain& surface, core::StringView source)
    {
        constexpr std::uint64_t bytes = 1024 * 1024;
        const rhi::ConformanceClearExecutionRequest request{
            {1, 3, true, 4096 * 4 * 3, bytes}, bytes, 5000000000};
        const auto create = [&](const auto& intent, const auto& quota, const char*& id) {
            return device.createConformanceClearExecution(
                rhi::ConformanceClearWorkload::parse(source, {8192, 4096, 3}, id),
                intent,
                quota,
                id);
        };
        for (int budget = 0; budget < 4; ++budget) {
            auto invalid = request;
            if (budget == 0)
                invalid.observation.maxOutputBytes = 1;
            if (budget == 1)
                invalid.maxNativeReadbackBytes = 1;
            if (budget == 2)
                invalid.observation.maxFrames = 1;
            if (budget == 3)
                invalid.observation.maxCpuMetadataBytes = 1;
            auto quota =
                core::makeShared<core::CpuMemoryQuota>(invalid.observation.maxCpuMetadataBytes);
            const char* id = nullptr;
            const auto count = device.queue().submitCount();
            assert(!create(invalid, quota, id) && id);
            assert(device.queue().submitCount() == count && quota->liveBytes() == 0);
            std::fprintf(stderr, "[conformance-negative-budget-%d] %s\n", budget, id);
        }
        const auto acquired = surface.acquireNextFrame();
        assert(acquired.success);
        auto image = surface.acquiredImage(acquired);
        assert(image.success && image.texture);
        const auto originalState = image.texture->state();
        for (int fault = 0; fault < 10; ++fault) {
            auto quota = core::makeShared<core::CpuMemoryQuota>(bytes);
            const char* id = nullptr;
            auto execution = create(request, quota, id);
            assert(execution && !id);
            auto encoder = execution->prepare({}, *image.texture, id);
            assert(encoder && !id);
            const auto before = device.queue().submitCount();
            if (fault == 1) {
                assert(!encoder->beginComputePass({}));
            } else if (fault == 2) {
                assert(!encoder->beginTransferPass({}));
            } else if (fault == 3) {
                auto foreign = device.createCommandEncoder({});
                auto buffer = foreign->finish();
                assert(buffer);
                const auto rejected = execution->submit(*buffer, nullptr);
                assert(!rejected.success && !rejected.hasAcceptedWork());
            } else if (fault != 4 && fault != 9) {
                assert(encoder
                        ->transition(
                            *image.texture, originalState, rhi::ResourceState::ColorAttachment)
                        .success);
                rhi::RenderPassDesc pass;
                rhi::RenderPassColorAttachmentDesc color;
                color.target = image.texture->id();
                color.format = image.texture->desc().format;
                color.loadOp = rhi::LoadOp::Clear;
                color.storeOp = rhi::StoreOp::Store;
                auto parsed = rhi::ConformanceClearWorkload::parse(source, {8192, 4096, 3}, id);
                color.clearColor = parsed->frames()[0].clearColor();
                if (fault == 0)
                    color.clearColor.r = 0;
                if (fault == 8)
                    color.storeOp = rhi::StoreOp::DontCare;
                pass.colorAttachments.push_back(color);
                auto recorded = encoder->beginRenderPass(pass);
                if (fault == 0 || fault == 8) {
                    assert(!recorded);
                } else {
                    assert(recorded && recorded->end().success);
                    if (fault == 5)
                        assert(!encoder->beginRenderPass(pass));
                    if (fault == 6) {
                        assert(encoder
                                ->transition(*image.texture,
                                    rhi::ResourceState::ColorAttachment,
                                    rhi::ResourceState::Present)
                                .success);
                        auto buffer = encoder->finish();
                        assert(buffer);
                        const auto rejected = device.queue().submit(*buffer);
                        assert(!rejected.success && !rejected.hasAcceptedWork());
                        assert(!execution->submit(*buffer, nullptr).hasAcceptedWork());
                    }
                    // fault 7 deliberately omits the final output transition.
                }
            }
            if (fault == 9)
                quota->setLimitBytes(bytes - 1);
            auto buffer = encoder->finish();
            if (buffer) {
                const auto rejected = execution->submit(*buffer, nullptr);
                assert(!rejected.success && !rejected.hasAcceptedWork());
            }
            assert(device.queue().submitCount() == before);
            assert(
                !rhi::OriginalConformanceAudit::inspect(execution->run()).originalOutputValidated);
            std::fprintf(
                stderr, "[conformance-negative-command-%d] rejected before acceptance\n", fault);
        }
        // Abandoned negative recordings never mutated the real acquired image.
        auto quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        const char* id = nullptr;
        auto execution = create(request, quota, id);
        assert(execution && !id);
        auto encoder = execution->prepare({}, *image.texture, id);
        assert(encoder && !id);
        assert(execution->finalizePresented());
        assert(!rhi::OriginalConformanceAudit::inspect(execution->run()).originalOutputValidated);
        execution.reset();
        encoder.reset();
        auto timed = request;
        timed.completionTimeoutNanoseconds = 1;
        execution = create(timed, quota, id);
        assert(execution && !id);
        encoder = execution->prepare({}, *image.texture, id);
        assert(encoder && !id);
        assert(
            encoder->transition(*image.texture, originalState, rhi::ResourceState::ColorAttachment)
                .success);
        auto authored = rhi::ConformanceClearWorkload::parse(source, {8192, 4096, 3}, id);
        rhi::RenderPassDesc pass;
        rhi::RenderPassColorAttachmentDesc color;
        color.target = image.texture->id();
        color.format = image.texture->desc().format;
        color.loadOp = rhi::LoadOp::Clear;
        color.storeOp = rhi::StoreOp::Store;
        color.clearColor = authored->frames()[0].clearColor();
        pass.colorAttachments.push_back(color);
        auto render = encoder->beginRenderPass(pass);
        assert(render && render->end().success);
        assert(encoder
                ->transition(*image.texture,
                    rhi::ResourceState::ColorAttachment,
                    rhi::ResourceState::Present)
                .success);
        auto command = encoder->finish();
        assert(command);
        const auto before = device.queue().submitCount();
        const auto timeout = execution->submit(*command, nullptr);
        assert(!timeout.success && timeout.hasAcceptedWork()
            && device.queue().submitCount() == before + 1);
        device.waitIdle();
        assert(surface.present(acquired).success);
        assert(execution->finalizePresented());
        assert(!rhi::OriginalConformanceAudit::inspect(execution->run()).originalOutputValidated);
        std::fprintf(stderr, "[conformance-negative-cold-deadline] real accepted work preserved\n");
    }
};
} // namespace arp::tests
