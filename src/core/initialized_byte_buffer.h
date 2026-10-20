#pragma once
#include "core/cpu_allocator.h"

#include <cstring>

namespace arp::core {
// Fixed-capacity CPU bytes with exact initialization coverage. Explicit writes
// are the only initialization source; zero-filled allocation is not evidence.
class InitializedByteBuffer final {
public:
    explicit InitializedByteBuffer(std::size_t bytes)
    {
        bytes_.resize(bytes);
        initialized_.resize(bytes);
    }

    bool write(std::size_t offset, const void* data, std::size_t bytes) noexcept
    {
        if (!data || !bytes || offset > bytes_.size() || bytes > bytes_.size() - offset)
            return false;
        std::memcpy(bytes_.data() + offset, data, bytes);
        std::memset(initialized_.data() + offset, 1, bytes);
        return true;
    }

    // External writes invalidate knowledge, never synthesize CPU bytes.
    void invalidate() noexcept { std::memset(initialized_.data(), 0, initialized_.size()); }

    bool initialized(std::size_t offset, std::size_t size) const noexcept
    {
        if (offset > bytes_.size() || size > bytes_.size() - offset)
            return false;
        for (std::size_t i = offset; i < offset + size; ++i)
            if (!initialized_[i])
                return false;
        return true;
    }

    const std::uint8_t* data() const noexcept { return bytes_.data(); }

    std::size_t size() const noexcept { return bytes_.size(); }

private:
    pmr::Vector<std::uint8_t> bytes_{&defaultCpuMemoryResource()};
    pmr::Vector<std::uint8_t> initialized_{&defaultCpuMemoryResource()};
};
} // namespace arp::core
