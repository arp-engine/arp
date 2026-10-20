#pragma once

#include "rhi/frontend/command_encoder_base.h"

#include <cassert>
#include <string_view>
#include <utility>

namespace arp::tests {

// Test-only result injection around a real encoder. It never manufactures a
// successful command, completion, or native payload. The counter also detects
// a consumer issuing any further operation after the injected first failure.
class ComputeRecordFailure final : public rhi::CommandEncoderBase {
public:
    enum class Point { Begin, Pipeline, Descriptor, PushConstants, Dispatch, End };

    struct Stats {
        unsigned attempts = 0u;
        bool failed = false;
        bool targetMatched = false;
    };

    ComputeRecordFailure(rhi::CommandEncoder& inner, Point point) : inner_(&inner), point_(point) {}

    ComputeRecordFailure(core::UniquePtr<rhi::CommandEncoder> inner,
        Point point,
        std::string_view targetLabel,
        Stats* stats = nullptr)
        : ownedInner_(std::move(inner)), inner_(ownedInner_.get()), point_(point),
          targetLabel_(targetLabel), stats_(stats)
    {
        assert(inner_);
    }

    unsigned attempts = 0u;
    bool failed = false;

    rhi::ResourceBarrierResult transition(
        rhi::GpuBuffer& buffer, const rhi::ResourceBarrierDesc& desc) override
    {
        return inner_->transition(buffer, desc);
    }

    rhi::ResourceBarrierResult transition(
        rhi::GpuTexture& texture, const rhi::ResourceBarrierDesc& desc) override
    {
        return inner_->transition(texture, desc);
    }

    core::UniquePtr<rhi::RenderPassEncoder> beginRenderPass(
        const rhi::RenderPassDesc& desc) override
    {
        return inner_->beginRenderPass(desc);
    }

    core::UniquePtr<rhi::TransferPassEncoder> beginTransferPass(
        const rhi::TransferPassDesc& desc) override
    {
        return inner_->beginTransferPass(desc);
    }

    core::UniquePtr<rhi::ComputePassEncoder> beginComputePass(
        const rhi::ComputePassDesc& desc) override
    {
        if (!targetLabel_.empty() && desc.label != targetLabel_)
            return inner_->beginComputePass(desc);
        if (stats_)
            stats_->targetMatched = true;
        if (reject(Point::Begin)) {
            markCommandFail("TEST-COMPUTE-RECORD-FIRST-FAILURE");
            return nullptr;
        }
        auto pass = inner_->beginComputePass(desc);
        assert(pass);
        return core::makeUnique<Pass>(*this, std::move(pass));
    }

    core::UniquePtr<rhi::CommandBuffer> finish() override { return inner_->finish(); }

    void pushDebugGroup(std::string_view label) override { inner_->pushDebugGroup(label); }

    void popDebugGroup() override { inner_->popDebugGroup(); }

    void insertDebugMarker(std::string_view label) override { inner_->insertDebugMarker(label); }

private:
    bool reject(Point point)
    {
        assert(!failed);
        ++attempts;
        failed = point_ == point;
        if (stats_) {
            stats_->attempts = attempts;
            stats_->failed = failed;
        }
        return failed;
    }

    static rhi::CommandValidationResult failure()
    {
        return {false, "TEST-COMPUTE-RECORD-FIRST-FAILURE", 0u};
    }

    class Pass final : public rhi::ComputePassEncoder {
    public:
        Pass(ComputeRecordFailure& owner, core::UniquePtr<rhi::ComputePassEncoder> inner)
            : owner_(owner), inner_(std::move(inner))
        {}

        rhi::CommandValidationResult setPipeline(const rhi::ComputePipeline& pipeline) override
        {
            return owner_.reject(Point::Pipeline) ? failure() : inner_->setPipeline(pipeline);
        }

        rhi::CommandValidationResult bindDescriptorSet(std::uint32_t set,
            const rhi::DescriptorSet& descriptors,
            const std::uint32_t* offsets,
            std::uint32_t count) override
        {
            return owner_.reject(Point::Descriptor)
                ? failure()
                : inner_->bindDescriptorSet(set, descriptors, offsets, count);
        }

        rhi::CommandValidationResult bindDescriptorTable(
            std::uint32_t set, const rhi::DescriptorTable& table) override
        {
            return inner_->bindDescriptorTable(set, table);
        }

        rhi::CommandValidationResult pushConstants(rhi::ShaderStage stage,
            std::uint32_t offset,
            std::uint32_t size,
            const void* data) override
        {
            return owner_.reject(Point::PushConstants)
                ? failure()
                : inner_->pushConstants(stage, offset, size, data);
        }

        rhi::CommandValidationResult dispatch(
            std::uint32_t x, std::uint32_t y, std::uint32_t z) override
        {
            return owner_.reject(Point::Dispatch) ? failure() : inner_->dispatch(x, y, z);
        }

        rhi::CommandValidationResult dispatchIndirect(
            const rhi::GpuBuffer& buffer, std::uint64_t offset) override
        {
            return inner_->dispatchIndirect(buffer, offset);
        }

        rhi::CommandValidationResult writeTimestamp(
            const rhi::QueryPool& pool, std::uint32_t query) override
        {
            assert(!owner_.failed);
            return inner_->writeTimestamp(pool, query);
        }

        rhi::CommandValidationResult end() override
        {
            return owner_.reject(Point::End) ? failure() : inner_->end();
        }

    private:
        ComputeRecordFailure& owner_;
        core::UniquePtr<rhi::ComputePassEncoder> inner_;
    };

    core::UniquePtr<rhi::CommandEncoder> ownedInner_;
    rhi::CommandEncoder* inner_ = nullptr;
    Point point_;
    std::string_view targetLabel_;
    Stats* stats_ = nullptr;
};

} // namespace arp::tests
