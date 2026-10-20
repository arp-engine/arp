#include "network/io_context.h"
#include "network/io_service.h"
#include "network/socket_runtime.h"
#include "network/timer.h"

#include <cassert>
#include <chrono>
#include <thread>

int main()
{
    using namespace arp::network;

    SocketRuntime runtime;
    assert(runtime.valid());

    io_service service;
    io_context context(&service);

    int onceCount = 0;
    int repeatCount = 0;

    auto once = context.run_after(std::chrono::milliseconds(10), [&]() {
        ++onceCount;
    });

    auto repeating = context.run_every(std::chrono::milliseconds(5), [&]() {
        ++repeatCount;
        if (repeatCount >= 3) {
            service.close();
        }
    });

    assert(once);
    assert(repeating);

    std::thread reactor([&]() {
        service.run();
    });

    while (!service.quit()) {
        context.dispatch();
    }

    reactor.join();
    context.dispatch();
    assert(onceCount == 1);
    assert(repeatCount >= 3);

    io_service cancelService;
    io_context cancelContext(&cancelService);
    int cancelCount = 0;
    auto cancelled = cancelContext.run_after(std::chrono::milliseconds(50), [&]() {
        ++cancelCount;
    });
    cancelled->cancel();
    cancelService.close();
    cancelContext.dispatch();
    assert(cancelCount == 0);

    return 0;
}
