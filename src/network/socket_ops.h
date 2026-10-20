#pragma once

#include "core/api.h"
#include "network/compat.h"
#include "network/error.h"

namespace arp::network::socketops {

ARP_API socket_t open(int family, int type, int protocol, error_code& ec);
ARP_API socket_t accept(socket_t sock, sockaddr_storage& addr, error_code& ec);

ARP_API int bind(socket_t sock, const sockaddr_storage& addr, error_code& ec);
ARP_API int listen(socket_t sock, int backlog, error_code& ec);
ARP_API int connect(socket_t sock, const sockaddr_storage& addr, error_code& ec);
ARP_API int shutdown(socket_t sock, int how, error_code& ec);
ARP_API int close(socket_t sock, error_code& ec);

ARP_API int send(socket_t sock, const void* data, socklen_t size, int flags, error_code& ec);
ARP_API int sendto(socket_t sock,
    const sockaddr_storage& addr,
    const void* data,
    socklen_t size,
    int flags,
    error_code& ec);

ARP_API int recv(socket_t sock, void* data, socklen_t size, int flags, error_code& ec);
ARP_API int recvfrom(
    socket_t sock, sockaddr_storage& addr, void* data, socklen_t size, int flags, error_code& ec);

ARP_API int setsockopt(
    socket_t sock, int level, int opt, const char* value, int size, error_code& ec);
ARP_API int getsockopt(socket_t sock, int level, int opt, char* value, int* size, error_code& ec);

ARP_API int set_nonblocking(socket_t sock, bool nonblocking, error_code& ec);
ARP_API int set_broadcast(socket_t sock, bool broadcast, error_code& ec);
ARP_API int set_reuseaddr(socket_t sock, bool reuseaddr, error_code& ec);
ARP_API int set_keepalive(socket_t sock, bool keepalive, error_code& ec);
ARP_API int set_nodelay(socket_t sock, bool nodelay, error_code& ec);
/// On macOS, converts a broken stream-pipe send into EPIPE so asynchronous
/// callers receive their error callback instead of process-wide SIGPIPE.
ARP_API int set_nosigpipe(socket_t sock, error_code& ec);
ARP_API int set_linger(socket_t sock, uint16 onoff, uint16 linger, error_code& ec);

ARP_API uint32 rtt(socket_t sock, error_code& ec);

ARP_API bool getbuffersize(socket_t sock, int optname, int* size, error_code& ec);
ARP_API bool setbuffersize(socket_t sock, int optname, int size, error_code& ec);

ARP_API int getsockname(socket_t sock, sockaddr_storage& addr, error_code& ec);
ARP_API int getpeername(socket_t sock, sockaddr_storage& addr, error_code& ec);

ARP_API int create_pipe(socket_t fd[2], error_code& ec);

} // namespace arp::network::socketops
