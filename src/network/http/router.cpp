#include "network/http/router.h"

#include <array>

namespace arp::network::http {

StringView RouteMatch::param(StringView name) const noexcept
{
    for (const auto& item : parameters)
        if (item.name == name)
            return item.value;
    return {};
}

DiagnosticCode Router::route(Method method, StringView pattern)
{
    if (frozen_)
        return DiagnosticCode::routes_frozen;
    if (methodText(method).empty())
        return DiagnosticCode::invalid_method;
    if (pattern.size() > max_pattern_bytes_)
        return DiagnosticCode::route_limit;
    Route entry;
    entry.method = method;
    entry.text.assign(pattern);
    if (!entry.pattern.assign(pattern))
        return DiagnosticCode::invalid_route;
    for (const auto& previous : routes_)
        if (previous.method == method && previous.pattern.sameShape(entry.pattern))
            return DiagnosticCode::duplicate_route;
    if (routes_.size() >= max_routes_)
        return DiagnosticCode::route_limit;
    routes_.push_back(std::move(entry));
    return DiagnosticCode::none;
}

RouteMatch Router::match(const Request& request) const
{
    RouteMatch result;
    const auto target = request.target();
    const auto mark = target.find('?');
    const auto path = target.substr(0, mark);
    if (!core::PathPattern::validPath(path) || !core::PathPattern::validEncodedText(target)) {
        result.diagnostic = DiagnosticCode::invalid_request_target;
        result.status = 400;
        return result;
    }
    result.path.assign(path);
    if (mark != StringView::npos)
        result.query.assign(target.substr(mark + 1));
    const Route* best = nullptr;
    std::array<bool, 9> allowed{};
    for (const auto& route : routes_) {
        if (!route.pattern.matches(path))
            continue;
        allowed[static_cast<std::size_t>(route.method)] = true;
        if (route.method == request.method()
            && (!best || route.pattern.moreSpecificThan(best->pattern)))
            best = &route;
    }
    if (best) {
        result.status = 200;
        result.pattern = best->text;
        best->pattern.matches(path, &result.parameters);
    } else {
        for (std::size_t i = 0; i < allowed.size(); ++i)
            if (allowed[i]) {
                if (!result.allow.empty())
                    result.allow += ", ";
                result.allow += methodText(static_cast<Method>(i));
            }
        if (!result.allow.empty())
            result.status = 405;
    }
    return result;
}

} // namespace arp::network::http
