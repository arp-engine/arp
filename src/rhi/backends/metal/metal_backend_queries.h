#pragma once

#include "core/cpu_allocator.h"
#include "rhi/backends/metal/metal_native_queries.h"
#include "rhi/gpu_device.h"
#include "rhi/semantics/native_backend_capabilities.h"

#include <memory>

namespace arp::rhi_metal {

class MetalRhiQueryPool final : public rhi::QueryPool {
public:
    MetalRhiQueryPool(
        std::shared_ptr<MetalRhiNativeQueryPoolPayload> payload, rhi::QueryPoolDesc desc)
        : payload_(std::move(payload)), desc_(std::move(desc))
    {}

    std::uint32_t count() const override { return desc_.count; }

    rhi::QueryType type() const override { return desc_.type; }

    rhi::CommandValidationResult reset() override { return resetMetalRhiNativeQueryPool(payload_); }

    rhi::QueryExecutionStatus executionStatus() const override
    {
        return metalRhiNativeQueryPoolExecutionStatus(payload_);
    }

    const std::shared_ptr<MetalRhiNativeQueryPoolPayload>& payload() const { return payload_; }

private:
    std::shared_ptr<MetalRhiNativeQueryPoolPayload> payload_;
    rhi::QueryPoolDesc desc_;
};

arp::core::UniquePtr<rhi::QueryPool> createMetalRhiQueryPool(const rhi::QueryPoolDesc& desc,
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract);

const MetalRhiQueryPool* asMetalRhiQueryPool(const rhi::QueryPool& pool);

} // namespace arp::rhi_metal
