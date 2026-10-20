#pragma once

#include "network/http/uri.h"

#include <cstddef>

namespace arp::network::http {

class ARP_API Header {
public:
    Header();
    Header(StringView name, StringView value);

    StringView name() const noexcept;
    StringView value() const noexcept;

private:
    core::pmr::String name_;
    core::pmr::String value_;
};

class ARP_API HeaderList {
public:
    HeaderList();

    DiagnosticCode add(StringView name, StringView value);
    std::size_t size() const noexcept;
    std::size_t count(StringView name) const noexcept;
    StringView firstValue(StringView name) const noexcept;
    const Header& at(std::size_t index) const noexcept;

private:
    core::pmr::Vector<Header> headers_;
};

ARP_API DiagnosticCode validateHeaderName(StringView name) noexcept;
ARP_API DiagnosticCode validateHeaderValue(StringView value) noexcept;

} // namespace arp::network::http
