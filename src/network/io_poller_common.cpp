#include "network/io_poller.h"

#include <algorithm>

namespace arp::network {

bool poller::isregistered(socket_t fd, int bit) const
{
    return bit >= 0 && bit < op_bit::bit_max
        && op_descriptors_[bit].find(fd) != op_descriptors_[bit].end();
}

bool poller::register_io_event(socket_t fd, int opmask, io_operation* op, error_code& ec)
{
    for (int bit = error_bit; bit < op_bit::bit_max; ++bit) {
        if ((opmask & (1 << bit)) == 0)
            continue;
        auto iter = op_descriptors_[bit].find(fd);
        if (iter == op_descriptors_[bit].end()) {
            if (!do_register(fd, bit, ec)) {
                const auto registrationError = ec;
                error_code ignored;
                deregister_io_event(fd, opmask, op, ignored);
                ec = registrationError;
                return false;
            }
            iter = op_descriptors_[bit].emplace(fd, op_descriptor_queue{}).first;
        }
        iter->second.operations.push_back(op);
    }
    clear(ec);
    return true;
}

bool poller::deregister_io_event(socket_t fd, int opmask, error_code& ec)
{
    return deregister_io_event(fd, opmask, nullptr, ec);
}

bool poller::deregister_io_event(socket_t fd, int opmask, io_operation* op, error_code& ec)
{
    for (int bit = error_bit; bit < op_bit::bit_max; ++bit) {
        if ((opmask & (1 << bit)) == 0)
            continue;
        auto iter = op_descriptors_[bit].find(fd);
        if (iter == op_descriptors_[bit].end())
            continue;
        auto& operations = iter->second.operations;
        if (op == nullptr) {
            operations.clear();
        } else {
            operations.erase(
                std::remove(operations.begin(), operations.end(), op), operations.end());
        }
        if (operations.empty()) {
            op_descriptors_[bit].erase(iter);
            do_deregister(fd, bit, ec);
        }
    }
    clear(ec);
    return true;
}

void poller::halt_io_events()
{
    core::pmr::Vector<io_operation*> operations{&core::defaultCpuMemoryResource()};
    for (int bit = error_bit; bit < op_bit::bit_max; ++bit) {
        auto& descriptors = op_descriptors_[bit];
        for (const auto& item : descriptors) {
            for (auto* op : item.second.operations) {
                if (std::find(operations.begin(), operations.end(), op) == operations.end()) {
                    operations.push_back(op);
                }
            }
            error_code ignored;
            do_deregister(item.first, bit, ignored);
        }
        descriptors.clear();
    }
    for (auto* op : operations) {
        if (op != nullptr)
            op->destroy();
    }
}

void poller::cancel_io_events(socket_t fd)
{
    core::pmr::Vector<io_operation*> operations{&core::defaultCpuMemoryResource()};
    for (auto& descriptors : op_descriptors_) {
        const auto it = descriptors.find(fd);
        if (it == descriptors.end())
            continue;
        for (auto* op : it->second.operations)
            if (std::find(operations.begin(), operations.end(), op) == operations.end())
                operations.push_back(op);
    }
    error_code ec;
    deregister_io_event(fd, op_error | op_read | op_write, ec);
    for (auto* op : operations)
        op->complete(this, make_error_code(inet_errors::operation_aborted));
}

void poller::trigger_event(socket_t fd, int bit)
{
    auto iter = op_descriptors_[bit].find(fd);
    if (iter == op_descriptors_[bit].end() || iter->second.operations.empty()) {
        return;
    }
    io_operation* op = iter->second.operations.front();
    const io_event previous = op->event();
    error_code ec;
    switch (op->perform()) {
    case io_operation::done:
    case io_operation::halt:
        deregister_io_event(previous.fd, previous.opmask, op, ec);
        op->complete(this, ec);
        break;
    case io_operation::not_done:
        if (previous.fd != op->event().fd) {
            const io_event current = op->event();
            deregister_io_event(previous.fd, previous.opmask, op, ec);
            register_io_event(current.fd, current.opmask, op, ec);
        } else if (previous.opmask != op->event().opmask) {
            const int removed = previous.opmask & ~op->event().opmask;
            const int added = op->event().opmask & ~previous.opmask;
            deregister_io_event(previous.fd, removed, op, ec);
            register_io_event(previous.fd, added, op, ec);
        }
        break;
    }
}

} // namespace arp::network
