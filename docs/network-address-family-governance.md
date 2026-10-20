# Network Address Family Governance

Status: Contract Complete. Production Ready: not claimed.

This document defines the IPv4, IPv6, resolver, endpoint, and dual-stack
ownership contract for the NetworkService migration. It does not claim
Prototype Complete, Backend Validated, Platform Validated, or Production Ready
status for resolver or dual-stack backend implementation.

## Boundary

IPv4 and IPv6 are address-family data, not backend identities. Backends remain
split by event model: Select, Epoll, Kqueue, Windows IOCP, and Web platform.
Address family belongs to endpoint, resolver, connect, listen, and datagram
policy.

The old `tcp::socket<ipv4/ipv6>` and `udp::socket<ipv4/ipv6>` facades are
legacy migration debt. They must not define future protocol behavior because a
template family can diverge from the runtime `endpoint` family.

## Endpoint Contract

`endpoint` stores concrete IPv4 or IPv6 socket address data. It is valid only
when the address family is `AF_INET` or `AF_INET6`.

Endpoint text formatting is deterministic:

- IPv4 formats as `127.0.0.1:3037`.
- IPv6 formats as `[::1]:3038`.
- Invalid endpoints format as an empty string.

Connect and datagram-send operations consume concrete endpoints. They do not
perform DNS, address-family fallback, or endpoint guessing.

## Address Family Policy

`AddressFamilyPolicy` defines resolver and bind filtering:

- `IPv4Only`: only IPv4 endpoints are valid.
- `IPv6Only`: only IPv6 endpoints are valid.
- `AnySystemOrder`: preserve platform resolver order after deterministic
  filtering and diagnostics.
- `AnyIPv6First`: return IPv6 endpoints before IPv4 endpoints.
- `AnyIPv4First`: return IPv4 endpoints before IPv6 endpoints.

A policy mismatch must fail with deterministic diagnostics. Protocol code must
not retry another family unless the selected resolver policy explicitly allows
that family.

## Dual Stack Policy

`DualStackPolicy` defines listen and datagram-bind socket creation:

- `IPv4Only`: bind only an IPv4 socket.
- `IPv6Only`: bind only an IPv6 socket.
- `SeparateIPv4IPv6Sockets`: bind independent IPv4 and IPv6 sockets.
- `IPv6DualStackSocket`: bind one IPv6 socket with explicit dual-stack intent.

Backend code must not rely on native `IPV6_V6ONLY` defaults. Select, Epoll, and
Kqueue socket poller cores route listen/datagram bind policy through a shared
backend-private socket policy helper that sets `IPV6_V6ONLY` explicitly before
binding IPv6 sockets. If a platform cannot implement the requested policy, it
must return deterministic diagnostics or a capability downgrade; it must not
silently bind a different shape.

## Resolver Contract

Resolver is a typed NetworkService contract. It accepts host/service text plus
address-family policy, ordering policy, timeout, cancellation, socket-kind, and
diagnostic ownership. It returns owned endpoint values. Current execution
deterministically resolves numeric IPv4/IPv6 host text and numeric services,
then reports deterministic no-DNS diagnostics for named hosts until WorkService
or a native resolver backend owns real DNS. DNS must not be hidden inside HTTP,
WebSocket, TLS, or connect fallback behavior.

## Backend Matrix Status

SelectSocketBackend has focused Windows MinGW smoke evidence for TCP IPv4
loopback, TCP IPv6 loopback, UDP IPv4 loopback, UDP IPv6 loopback,
address-family policy mismatch diagnostics, and `IPv6DualStackSocket` accepting
an IPv4 loopback client through an IPv6 listener.

EpollSocketBackend and KqueueSocketBackend share the same backend-private
policy helper and source-level contract coverage for explicit `IPV6_V6ONLY`
handling, but Linux and Darwin runtime validation remain blocked until tests
run on their target platforms. This is not Platform Validated evidence for
those backends.

## Validation Status Rules

Contract Complete means the endpoint, policy, and diagnostic rules are
documented and covered by focused contract tests.

Prototype Complete requires at least one NetworkService resolver and transport
facade path to submit operations using these policies.

Backend Validated requires Select, Epoll, and Kqueue backend paths to pass
focused IPv4, IPv6, UDP, TCP, and dual-stack tests on their implementation
paths.

Platform Validated requires those backend tests to pass on their target
platforms.

Production Ready cannot be claimed while protocol production callers hardcode
IPv4, depend on old socket templates, or rely on native dual-stack defaults.
