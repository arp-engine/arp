#include "core/cpu_allocator.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

std::filesystem::path projectRoot()
{
#ifdef ARP_SOURCE_DIR
    return std::filesystem::path(ARP_SOURCE_DIR);
#else
    auto current = std::filesystem::current_path();
    for (;;) {
        if (std::filesystem::exists(current / "CMakeLists.txt")) {
            return current;
        }
        if (!current.has_parent_path() || current == current.parent_path()) {
            return std::filesystem::current_path();
        }
        current = current.parent_path();
    }
#endif
}

arp::core::pmr::String readText(const std::filesystem::path& file)
{
    std::ifstream input(file);
    arp::core::pmr::String text{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String line{&arp::core::defaultCpuMemoryResource()};
    while (std::getline(input, line)) {
        text += line;
        text += '\n';
    }
    return text;
}

void requireContains(const arp::core::pmr::String& text, const char* token, const char* diagnostic)
{
    if (text.find(token) == arp::core::pmr::String::npos) {
        std::cerr << diagnostic << ": missing `" << token << "`\n";
        assert(false);
    }
}

} // namespace

int main()
{
    const auto root = projectRoot();
    const auto cmake = readText(root / "CMakeLists.txt");

    const auto epollPath = root / "src" / "network" / "io_poller_epoll.cpp";
    const auto kqueuePath = root / "src" / "network" / "io_poller_kqueue.cpp";

    assert(std::filesystem::exists(epollPath));
    assert(std::filesystem::exists(kqueuePath));

    const auto epoll = readText(epollPath);
    const auto kqueue = readText(kqueuePath);

    requireContains(cmake, "src/network/io_poller_epoll.cpp", "network CMake source list");
    requireContains(cmake, "src/network/io_poller_kqueue.cpp", "network CMake source list");

    requireContains(epoll, "#if defined(__linux__)", "epoll backend guard");
    requireContains(epoll, "<sys/epoll.h>", "epoll backend include");
    requireContains(epoll, "epoll_create", "epoll backend creation");
    requireContains(epoll, "epoll_ctl", "epoll backend registration");
    requireContains(epoll, "epoll_wait", "epoll backend polling");

    requireContains(kqueue, "#if defined(__APPLE__)", "kqueue backend guard");
    requireContains(kqueue, "<sys/event.h>", "kqueue backend include");
    requireContains(kqueue, "kqueue()", "kqueue backend creation");
    requireContains(kqueue, "EV_SET", "kqueue backend registration");
    requireContains(kqueue, "kevent", "kqueue backend polling");

    return 0;
}
