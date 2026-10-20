#pragma once

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#include <utility>

namespace arp::test {

// Existing browser smoke timeout, shared by native queue/map/data callbacks.
// Timeout is a test failure; it never synthesizes GPU completion.
class WebGpuAsyncWaitFixture final {
public:
    template <class Ready> static bool until(Ready&& ready)
    {
        return untilDeadline(
            std::forward<Ready>(ready), emscripten_get_now() + timeoutMilliseconds);
    }

    // Long assertion-enabled chains consume CPU time inside genuine callbacks.
    // Callers may share the browser runner's original absolute test deadline;
    // this never resets it or manufactures native completion.
    template <class Ready> static bool untilDeadline(Ready&& ready, double deadline)
    {
        while (!ready()) {
            if (emscripten_get_now() >= deadline)
                return false;
            emscripten_sleep(pollMilliseconds);
        }
        return true;
    }

private:
    static constexpr double timeoutMilliseconds = 5000.0;
    static constexpr unsigned pollMilliseconds = 10u;
};

} // namespace arp::test
#endif
