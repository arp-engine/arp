#pragma once

#include "core/text/path_pattern.h"
#include "network/http/message.h"

namespace arp::network::http {

struct RouteMatch {
    DiagnosticCode diagnostic = DiagnosticCode::none;
    std::uint16_t status = 404;
    core::pmr::String pattern{&core::defaultCpuMemoryResource()};
    core::pmr::String path{&core::defaultCpuMemoryResource()};
    core::pmr::String query{&core::defaultCpuMemoryResource()};
    core::pmr::String allow{&core::defaultCpuMemoryResource()};
    core::PathPattern::Parameters parameters{&core::defaultCpuMemoryResource()};
    StringView param(StringView name) const noexcept;

    bool success() const noexcept { return diagnostic == DiagnosticCode::none && status == 200; }
};

class ARP_API Router {
public:
    explicit Router(std::size_t maxRoutes = 64, std::size_t maxPatternBytes = 8192)
        : max_routes_(maxRoutes), max_pattern_bytes_(maxPatternBytes)
    {}

    DiagnosticCode route(Method method, StringView pattern);
    RouteMatch match(const Request& request) const;

    void freeze() noexcept { frozen_ = true; }

private:
    struct Route {
        Method method;
        core::pmr::String text{&core::defaultCpuMemoryResource()};
        core::PathPattern pattern;
    };

    std::size_t max_routes_;
    std::size_t max_pattern_bytes_;
    bool frozen_ = false;
    core::pmr::Vector<Route> routes_{&core::defaultCpuMemoryResource()};
};

} // namespace arp::network::http
