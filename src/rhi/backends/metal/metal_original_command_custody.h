#pragma once
#include "core/memory/cpu_allocator.h"

namespace arp::rhi_metal {
class MetalRhiNativeCommandPayload;

// One native submission/completed-handler custody for every original stream.
// Implementations consume the existing seal/journal; no receipt is caller issued.
class MetalOriginalCommandCustody {
    friend class MetalRhiNativeCommandPayload;

public:
    virtual ~MetalOriginalCommandCustody() = default;

private:
    virtual bool beforeSubmit(const MetalRhiNativeCommandPayload&, core::pmr::String&) noexcept = 0;
    virtual const char* beforeNativeCommit() noexcept = 0;
    virtual bool nativeCompleted(bool) noexcept = 0;
    // Native owner has terminally refused submission, ended every active
    // encoder and dropped its command buffer before journal rollback is legal.
    virtual void abandonUnsubmitted() noexcept = 0;
};
} // namespace arp::rhi_metal
