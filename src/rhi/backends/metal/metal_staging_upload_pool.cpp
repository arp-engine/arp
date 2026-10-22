#include "rhi/backends/metal/metal_staging_upload_pool.h"

#include "core/memory/cpu_allocator.h"
#include "rhi/frontend/memory_staging_upload_pool.h"

#include <algorithm>
#include <utility>

namespace arp::rhi_metal {

MetalRhiNativeStagingBufferInfo inspectMetalRhiNativeStagingBuffer(const std::shared_ptr<void>&)
{
    return {};
}

bool prepareMetalRhiNativeStagingBufferForGpu(
    const std::shared_ptr<void>&, std::uint64_t, std::uint64_t, std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-STAGING-BUFFER-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool MetalRhiStagingUploadPool::createPage(std::uint32_t pageIndex, std::uint64_t capacityBytes)
{
    if (!rhi::stagingCreatePageInvariant(pageIndex, pages_.size(), capacityBytes)) {
        return false;
    }

    Page page;
    pages_.push_back(std::move(page));
    return true;
}

} // namespace arp::rhi_metal

#include "rhi/backends/metal/metal_staging_upload_pool_common.inl"
