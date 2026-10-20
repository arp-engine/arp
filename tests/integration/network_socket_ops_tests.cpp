#include "network/endpoint.h"
#include "network/socket_ops.h"
#include "network/socket_runtime.h"

#include <cassert>
#include <cstring>

int main()
{
    using namespace arp::network;

    SocketRuntime runtime;
    assert(runtime.valid());

    error_code ec;
    socket_t acceptor = socketops::open(AF_INET, SOCK_STREAM, 0, ec);
    assert(acceptor != INVALID_SOCKET);
    assert(!ec);
    assert(socketops::set_reuseaddr(acceptor, true, ec) == 0);
    assert(socketops::bind(acceptor, endpoint::localhost<ipv4>(0).addr(), ec) == 0);

    sockaddr_storage bound{};
    assert(socketops::getsockname(acceptor, bound, ec) == 0);
    endpoint boundEndpoint(bound);
    assert(boundEndpoint.is_valid());
    assert(boundEndpoint.port() != 0);
    assert(socketops::close(acceptor, ec) == 0);

    socket_t pipe[2]{INVALID_SOCKET, INVALID_SOCKET};
    assert(socketops::create_pipe(pipe, ec) == 0);
    char sendByte[1]{'x'};
    char recvByte[1]{};
    assert(socketops::send(pipe[0], sendByte, sizeof(sendByte), 0, ec) == 1);
    assert(socketops::recv(pipe[1], recvByte, sizeof(recvByte), 0, ec) == 1);
    assert(std::memcmp(sendByte, recvByte, sizeof(sendByte)) == 0);
    assert(socketops::close(pipe[0], ec) == 0);
    assert(socketops::close(pipe[1], ec) == 0);

    socket_t invalid = INVALID_SOCKET;
    assert(socketops::close(invalid, ec) == 0);

    return 0;
}
