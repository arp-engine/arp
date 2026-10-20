#pragma once

#include "core/cpu_allocator.h"
#include "rhi/gpu_device.h"
#include "rhi/semantics/native_backend_capabilities.h"

#if defined(__EMSCRIPTEN__) && defined(__has_include)
#if __has_include(<webgpu/webgpu.h>)
#define ARP_RHI_WEBGPU_QUERY_POOL_HAS_NATIVE 1
#include <webgpu/webgpu.h>
#endif
#endif

#include <cstdint>
#include <memory>
#include <mutex>

namespace arp::rhi_webgpu {

class WebGpuRhiQueryPoolPayload final {
public:
    WebGpuRhiQueryPoolPayload(
#if defined(ARP_RHI_WEBGPU_QUERY_POOL_HAS_NATIVE)
        WGPUQuerySet querySet,
#endif
        core::pmr::Vector<std::uint64_t> scopeIds,
        std::uint32_t count);
    ~WebGpuRhiQueryPoolPayload();

#if defined(ARP_RHI_WEBGPU_QUERY_POOL_HAS_NATIVE)
    WGPUQuerySet nativeQuerySet() const noexcept { return querySet_; }
#endif
    const core::pmr::Vector<std::uint64_t>& scopeIds() const noexcept { return scopeIds_; }

    bool markRecorded(std::uint32_t queryIndex, std::pmr::string& diagnostics);
    bool markSubmitted(std::uint32_t queryIndex, std::pmr::string& diagnostics);
    bool rollbackSubmitted(std::uint32_t queryIndex, std::pmr::string& diagnostics);
    bool rollbackRecorded(std::uint32_t queryIndex);
    void markAvailable(std::uint32_t queryIndex);
    void markDiagnosticFailure(std::uint32_t queryIndex);
    rhi::CommandValidationResult reset();
    rhi::QueryExecutionStatus executionStatus() const;

private:
#if defined(ARP_RHI_WEBGPU_QUERY_POOL_HAS_NATIVE)
    WGPUQuerySet querySet_ = nullptr;
#endif
    core::pmr::Vector<std::uint64_t> scopeIds_{
        &core::defaultCpuMemoryResource(),
    };
    core::pmr::Vector<rhi::QueryExecutionState> states_{
        &core::defaultCpuMemoryResource(),
    };
    mutable std::mutex mutex_;

    friend arp::core::UniquePtr<rhi::QueryPool> createWebGpuRhiQueryPool(
        const rhi::QueryPoolDesc& desc,
        const rhi::semantics::NativeBackendCapabilityContract& capabilityContract);
};

class WebGpuRhiQueryPool final : public rhi::QueryPool {
public:
    WebGpuRhiQueryPool(std::shared_ptr<WebGpuRhiQueryPoolPayload> payload, rhi::QueryPoolDesc desc);

    std::uint32_t count() const override { return desc_.count; }

    rhi::QueryType type() const override { return desc_.type; }

    rhi::QueryExecutionStatus executionStatus() const override;
    rhi::CommandValidationResult reset() override;

#if defined(ARP_RHI_WEBGPU_QUERY_POOL_HAS_NATIVE)
    WGPUQuerySet nativeQuerySet() const noexcept
    {
        return payload_ ? payload_->nativeQuerySet() : nullptr;
    }
#endif
    const std::shared_ptr<WebGpuRhiQueryPoolPayload>& payload() const noexcept { return payload_; }

private:
    std::shared_ptr<WebGpuRhiQueryPoolPayload> payload_;
    rhi::QueryPoolDesc desc_;
};

arp::core::UniquePtr<rhi::QueryPool> createWebGpuRhiQueryPool(const rhi::QueryPoolDesc& desc,
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract);

const WebGpuRhiQueryPool* asWebGpuRhiQueryPool(const rhi::QueryPool& pool);

} // namespace arp::rhi_webgpu
