#pragma once

#include "core/memory/cpu_allocator.h"

#include <type_traits>
#include <utility>

namespace arp::core {

// Move-only, allocator-owned type erasure for asynchronous completion sinks.
template <class Signature> class Callback;

template <class R, class... Args> class Callback<R(Args...)> {
    struct Target {
        virtual ~Target() = default;
        virtual R call(Args... args) = 0;
    };

    template <class F> struct Stored final : Target {
        explicit Stored(F fn) : fn_(std::move(fn)) {}

        R call(Args... args) override { return fn_(std::forward<Args>(args)...); }

        F fn_;
    };

public:
    Callback() = default;
    Callback(Callback&&) noexcept = default;
    Callback& operator=(Callback&&) noexcept = default;

    template <class F,
        std::enable_if_t<!std::is_same_v<std::decay_t<F>, Callback>
                && std::is_invocable_r_v<R, std::decay_t<F>&, Args...>,
            int> = 0>
    Callback(F&& fn) : target_(makeUnique<Stored<std::decay_t<F>>>(std::forward<F>(fn)))
    {}

    explicit operator bool() const noexcept { return bool(target_); }

    R operator()(Args... args) { return target_->call(std::forward<Args>(args)...); }

private:
    UniquePtr<Target> target_;
};

// Copyable adapters share their core-owned target; copying never creates a
// hidden default-allocator closure allocation.
template <class Signature> class SharedCallback;

template <class R, class... Args> class SharedCallback<R(Args...)> {
public:
    SharedCallback() = default;

    SharedCallback(std::nullptr_t) {}

    template <class F,
        std::enable_if_t<!std::is_same_v<std::decay_t<F>, SharedCallback>
                && std::is_invocable_r_v<R, std::decay_t<F>&, Args...>,
            int> = 0>
    SharedCallback(F&& fn) : target_(makeShared<Callback<R(Args...)>>(std::forward<F>(fn)))
    {}

    explicit operator bool() const noexcept { return target_ && bool(*target_); }

    R operator()(Args... args) const { return (*target_)(std::forward<Args>(args)...); }

private:
    SharedPtr<Callback<R(Args...)>> target_;
};

} // namespace arp::core
