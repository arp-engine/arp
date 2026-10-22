#pragma once
#include <array>
#include <cstddef>
#include <vector>

namespace arp::core {
// Borrowed immutable contiguous elements. The owner must outlive the view.
template <class T> class ArrayView final {
public:
    constexpr ArrayView() noexcept = default;

    constexpr ArrayView(const T* data, std::size_t count) noexcept : data_(data), count_(count) {}

    template <std::size_t N>
    constexpr ArrayView(const T (&data)[N]) noexcept : data_(data), count_(N)
    {}

    template <std::size_t N>
    constexpr ArrayView(const std::array<T, N>& data) noexcept : data_(data.data()), count_(N)
    {}

    template <class A>
    ArrayView(const std::vector<T, A>& data) noexcept : data_(data.data()), count_(data.size())
    {}

    const T* data() const noexcept { return data_; }

    std::size_t size() const noexcept { return count_; }

    std::size_t size_bytes() const noexcept { return count_ * sizeof(T); }

    bool empty() const noexcept { return count_ == 0u; }

    const T* begin() const noexcept { return data_; }

    const T* end() const noexcept { return count_ ? data_ + count_ : data_; }

    const T& operator[](std::size_t index) const noexcept { return data_[index]; }

private:
    const T* data_ = nullptr;
    std::size_t count_ = 0u;
};
} // namespace arp::core
