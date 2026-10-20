#pragma once

#include "core/cpu_allocator.h"
#include "network/compat.h"
#include "network/error.h"

#include <cstdint>
#include <type_traits>
#include <utility>

namespace arp::network {

enum op_bit : std::uint8_t {
    error_bit = 0,
    read_bit,
    write_bit,
    bit_max,
};

enum op_mask : std::uint8_t {
    op_error = 1 << error_bit,
    op_read = 1 << read_bit,
    op_write = 1 << write_bit,
};

struct io_event {
    socket_t fd = INVALID_SOCKET;
    int opmask = 0;
};

class io_context;

class io_operation {
public:
    enum status { not_done, done, halt };

    io_operation() = default;

    io_operation(io_context* ctx, socket_t fd, int mask) : ctx_(ctx), event_({fd, mask}) {}

    virtual ~io_operation() = default;

    const io_event& event() const noexcept { return event_; }

    virtual status perform() = 0;
    virtual void complete(void* owner, const error_code& ec) = 0;

    virtual void destroy() { complete(nullptr, error_code()); }

    error_code ec_;
    io_context* ctx_ = nullptr;
    io_event event_{};
};

template <typename Handle> class basic_io_operation : public io_operation {
public:
    using handle_type = Handle;
    using io_operation::io_operation;

    template <typename U, std::enable_if_t<std::is_same_v<std::decay_t<U>, Handle>, int> = 0>
    basic_io_operation(io_context* ctx, socket_t fd, int mask, U&& handle)
        : io_operation(ctx, fd, mask), handle_(std::forward<U>(handle))
    {}

protected:
    handle_type handle_;
};

template <typename Void, typename T> struct has_handle : std::false_type {};

template <typename T>
struct has_handle<std::void_t<typename T::handle_type>, T> : std::true_type {};

template <typename T> constexpr bool has_handle_v = has_handle<void, T>::value;

template <typename Op, typename... Args> Op* initialize_async_operation(Args&&... args)
{
    auto holder = core::makeUnique<Op>(std::forward<Args>(args)...);
    Op* raw = holder.get();
    static_cast<void>(holder.release());
    return raw;
}

template <typename Op> core::UniquePtr<Op> reclaim_io_operation(Op* op) noexcept
{
    return core::UniquePtr<Op>{
        op,
        core::CpuAllocatedDeleter<Op>{op, &core::destroyCpuAllocatedUnique<Op>},
    };
}

} // namespace arp::network
