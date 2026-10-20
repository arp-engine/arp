#include "network/io_poller.h"
#include "network/socket_ops.h"
#include "network/socket_runtime.h"

#include <cassert>

namespace {

class CountingReadOp final : public arp::network::io_operation {
public:
    CountingReadOp(arp::network::socket_t fd, int& completions)
        : io_operation(nullptr, fd, arp::network::op_read), completions_(completions)
    {}

    status perform() override { return done; }

    void complete(void*, const arp::network::error_code&) override { ++completions_; }

private:
    int& completions_;
};

} // namespace

int main()
{
    using namespace arp::network;

    SocketRuntime runtime;
    assert(runtime.valid());

    socket_t pipe[2]{INVALID_SOCKET, INVALID_SOCKET};
    error_code ec;
    assert(socketops::create_pipe(pipe, ec) == 0);

    poller reactor;
    int completions = 0;
    CountingReadOp first(pipe[1], completions);
    CountingReadOp second(pipe[1], completions);

    assert(reactor.register_io_event(first.event().fd, first.event().opmask, &first, ec));
    assert(reactor.register_io_event(second.event().fd, second.event().opmask, &second, ec));

    const char byte = 1;
    assert(socketops::send(pipe[0], &byte, 1, 0, ec) == 1);

    assert(reactor.poll_io_events(100000.0, ec) >= 1);
    assert(completions == 1);

    assert(reactor.poll_io_events(100000.0, ec) >= 1);
    assert(completions == 2);

    socketops::close(pipe[0], ec);
    socketops::close(pipe[1], ec);
    return 0;
}
