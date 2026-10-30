# Reuse the engine HTTP client, parser, serializer and socket reactor.
# FileService consumes this package; it never contains file-domain types.
function(arp_add_network_http_transport root)
    add_library(arp_network_http_transport STATIC
        "${root}/src/network/endpoint.cpp"
        "${root}/src/network/error.cpp"
        "${root}/src/network/resolver.cpp"
        "${root}/src/network/http/client.cpp"
        "${root}/src/network/http/header.cpp"
        "${root}/src/network/http/message.cpp"
        "${root}/src/network/http/parser.cpp"
        "${root}/src/network/http/serializer.cpp"
        "${root}/src/network/http/session.cpp"
        "${root}/src/network/http/uri.cpp"
        "${root}/src/network/http/byte_read_transport.cpp"
        "${root}/src/network/http/fetch_byte_transfer.cpp"
        "${root}/src/network/io_context.cpp"
        "${root}/src/network/io_poller.cpp"
        "${root}/src/network/io_poller_common.cpp"
        "${root}/src/network/io_poller_epoll.cpp"
        "${root}/src/network/io_poller_kqueue.cpp"
        "${root}/src/network/io_service.cpp"
        "${root}/src/network/socket.cpp"
        "${root}/src/network/socket_ops.cpp"
        "${root}/src/network/socket_runtime.cpp"
        "${root}/src/network/timer.cpp"
        "${root}/src/network/tls/ssl_stream.cpp"
        "${root}/src/network/tls/tls_context.cpp")
    target_include_directories(arp_network_http_transport PUBLIC "${root}/src")
    target_link_libraries(arp_network_http_transport PUBLIC arp_core_foundation)
    target_compile_features(arp_network_http_transport PUBLIC cxx_std_17)
    if(WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        target_link_libraries(arp_network_http_transport PUBLIC ws2_32)
    endif()
endfunction()
