#pragma once
#include "core/container/generation_pool.h"

#include <cmath>

namespace arp::core {
// Script-defined numeric schemas, fixed storage, generation checked tickets.
// Composition of GenerationPool; no game-specific fields or update algorithms.
class NumericRecordPool final {
public:
    NumericRecordPool(std::uint32_t capacity, std::uint32_t columns)
        : capacity_(capacity), columns_(columns), pool_(capacity)
    {
        if (!capacity || !columns || std::size_t(capacity) > SIZE_MAX / columns / sizeof(double)) {
            diagnostic_ = "CORE-RECORD-SCHEMA";
            return;
        }
        data_.resize(std::size_t(capacity) * columns);
        tickets_.resize(capacity);
        handles_.resize(capacity);
    }

    int acquire()
    {
        if (data_.empty()) {
            diagnostic_ = "CORE-RECORD-SCHEMA";
            return -1;
        }
        if (serial_ >= std::uint32_t(INT32_MAX) / capacity_ - 1) {
            diagnostic_ = "CORE-RECORD-TICKET-EXHAUSTED";
            return -1;
        }
        auto h = pool_.emplace(0);
        if (!h) {
            diagnostic_ = pool_.diagnostic();
            return -1;
        }
        const int ticket = int(++serial_ * capacity_ + h.index);
        tickets_[h.index] = ticket;
        handles_[h.index] = h;
        std::fill_n(data_.data() + std::size_t(h.index) * columns_, columns_, 0.0);
        return ticket;
    }

    bool contains(int ticket) const
    {
        return !data_.empty() && ticket >= 0
            && tickets_[std::uint32_t(ticket) % capacity_] == ticket
            && pool_.get(handles_[std::uint32_t(ticket) % capacity_]);
    }

    bool release(int ticket)
    {
        if (!contains(ticket))
            return fail("CORE-RECORD-STALE-TICKET");
        auto index = std::uint32_t(ticket) % capacity_;
        pool_.erase(handles_[index]);
        handles_[index] = {};
        tickets_[index] = 0;
        return true;
    }

    bool read(int ticket, int column, double& output) const
    {
        if (!contains(ticket) || column < 0 || std::uint32_t(column) >= columns_)
            return false;
        output = data_[std::size_t(std::uint32_t(ticket) % capacity_) * columns_ + column];
        return true;
    }

    bool write(int ticket, int column, double value)
    {
        if (!contains(ticket))
            return fail("CORE-RECORD-STALE-TICKET");
        if (column < 0 || std::uint32_t(column) >= columns_ || !std::isfinite(value))
            return fail("CORE-RECORD-FIELD");
        data_[std::size_t(std::uint32_t(ticket) % capacity_) * columns_ + column] = value;
        return true;
    }

    int at(int index) const
    {
        return !data_.empty() && index >= 0 && std::uint32_t(index) < capacity_ && handles_[index]
            ? tickets_[index]
            : -1;
    }

    std::uint32_t size() const { return pool_.liveCount(); }

    std::uint32_t capacity() const { return capacity_; }

    const char* diagnostic() const { return diagnostic_; }

private:
    bool fail(const char* id)
    {
        diagnostic_ = id;
        return false;
    }

    std::uint32_t capacity_, columns_, serial_ = 0;
    GenerationPool<int> pool_;
    pmr::Vector<double> data_;
    pmr::Vector<int> tickets_;
    pmr::Vector<PoolHandle> handles_;
    const char* diagnostic_ = nullptr;
};
} // namespace arp::core
