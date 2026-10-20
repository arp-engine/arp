#pragma once
#include "core/runtime/bounded_terminal_observers.h"
#include "cpu_quota_factory_fixture.h"

#include <array>
#include <cassert>
#include <stdexcept>

namespace arp::test {
// CPU lifetime tests only. No registry notification is GPU completion or a
// real native device-loss receipt; native adapters need their own validation.
class BoundedTerminalObserversFixture final {
    enum class Event { Lost, Shutdown, Retired };
    using Registry = core::runtime::BoundedTerminalObservers<Event>;
    using Status = Registry::Status;

    class Receiver final {
    public:
        Receiver(Registry& registry, unsigned& calls, unsigned& destroyed) noexcept
            : registry(registry), calls(calls), destroyed(destroyed)
        {}

        ~Receiver() { ++destroyed; }

        Registry& registry;
        unsigned& calls;
        unsigned& destroyed;
        Registry::Ticket ticket;

        static void consume(const void* original, Event event) noexcept
        {
            const auto& receiver = *static_cast<const Receiver*>(original);
            assert(receiver.registry.terminal() && *receiver.registry.terminal() == event
                && !receiver.registry.accepting());
            assert(receiver.registry.pending()
                && receiver.registry.inspect(receiver.ticket) == Status::Complete);
            auto output = receiver.ticket;
            assert(receiver.registry.attach({}, nullptr, output) == Status::Reentrant && !output);
            bool invoked = false;
            assert(receiver.registry.suppress(receiver.ticket,
                       [&](const void*) noexcept {
                           invoked = true;
                           return true;
                       })
                    == Status::Reentrant
                && !invoked);
            assert(receiver.registry.deliver(event) == Status::Reentrant);
            ++receiver.calls;
        }
    };

public:
    static void verify()
    {
        auto metadata = core::makeShared<core::CpuMemoryQuota>(65536u);
        auto owners = core::makeShared<core::CpuMemoryQuota>(65536u);
        const auto factory = [&]() -> core::UniquePtr<Registry> {
            try {
                core::CpuMemoryQuotaScope scope(metadata);
                return core::makeUnique<Registry>(3u);
            } catch (const std::bad_alloc&) {
                return {};
            } catch (const std::length_error&) {
                return {};
            }
        };
        CpuQuotaFactoryFixture pressure(metadata);
        const auto bytes = pressure.measureRetained(factory);
        pressure.verify(bytes, factory);
        {
            core::CpuMemoryQuotaLease full(metadata, metadata->limitBytes());
            assert(full.charged() && !factory());
        }
        for (const auto event : std::array{Event::Lost, Event::Shutdown, Event::Retired})
            for (unsigned detached = 0u; detached < 8u; ++detached) {
                auto registry = factory();
                assert(registry && registry->capacity() == 3u && registry->accepting());
                std::array<unsigned, 3u> calls{}, destroyed{};
                std::array<Registry::Ticket, 3u> tickets;
                std::array<std::weak_ptr<Receiver>, 3u> borrowed;
                for (std::size_t i = 0u; i < tickets.size(); ++i) {
                    std::shared_ptr<Receiver> receiver;
                    {
                        core::CpuMemoryQuotaScope scope(owners);
                        receiver = core::makeShared<Receiver>(*registry, calls[i], destroyed[i]);
                    }
                    assert(registry->attach(receiver, &Receiver::consume, tickets[i])
                        == Status::Complete);
                    receiver->ticket = tickets[i];
                    borrowed[i] = receiver;
                    receiver.reset();
                }
                assert(registry->pending() == 3u);
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    std::array{metadata, owners}, [&]() noexcept -> const char* {
                        Registry::Ticket output = tickets[0];
                        assert(registry->attach(borrowed[0].lock(), &Receiver::consume, output)
                                == Status::Capacity
                            && !output);
                        assert(
                            registry->attach({}, &Receiver::consume, output) == Status::InvalidInput
                            && !output);
                        assert(registry->attach(borrowed[0].lock(), nullptr, output)
                                == Status::InvalidInput
                            && !output);
                        auto foreign = tickets[0];
                        ++foreign.domain;
                        bool suppressed = false;
                        assert(registry->suppress(foreign,
                                   [&](const void*) noexcept {
                                       suppressed = true;
                                       return true;
                                   })
                                == Status::Foreign
                            && !suppressed);
                        auto stale = tickets[0];
                        ++stale.generation;
                        assert(registry->suppress(stale, [](const void*) noexcept {
                            assert(false);
                            return true;
                        }) == Status::Stale);
                        stale = tickets[0];
                        stale.index = registry->capacity();
                        assert(registry->inspect(stale) == Status::Stale);
                        assert(registry->inspect({}) == Status::Unissued);
                        assert(registry->suppress(tickets[0], [&](const void* context) noexcept {
                            assert(context == borrowed[0].lock().get());
                            auto rejected = tickets[0];
                            assert(registry->attach({}, nullptr, rejected) == Status::Reentrant
                                && !rejected);
                            assert(registry->deliver(event) == Status::Reentrant);
                            return false;
                        }) == Status::SuppressionFailed);
                        assert(registry->pending() == 3u && !borrowed[0].expired()
                            && registry->inspect(tickets[0]) == Status::Complete);
                        for (unsigned i = 0u; i < 3u; ++i)
                            if (detached & (1u << i)) {
                                assert(registry->suppress(tickets[i], [](const void*) noexcept {
                                    return true;
                                }) == Status::Complete);
                                assert(borrowed[i].expired() && destroyed[i] == 1u && !calls[i]);
                                assert(registry->suppress(tickets[i], [](const void*) noexcept {
                                    assert(false);
                                    return true;
                                }) == Status::Stale);
                            }
                        assert(registry->deliver(event) == Status::Complete && !registry->pending()
                            && !registry->accepting());
                        for (unsigned i = 0u; i < 3u; ++i) {
                            assert(borrowed[i].expired() && destroyed[i] == 1u
                                && calls[i] == ((detached & (1u << i)) ? 0u : 1u));
                            assert(registry->inspect(tickets[i]) == Status::Stale);
                        }
                        assert(registry->deliver(event) == Status::Closed);
                        assert(registry->attach({}, nullptr, output) == Status::Closed && !output);
                        return nullptr;
                    }));
                for (auto& weak : borrowed)
                    weak.reset();
                registry.reset();
                assert(!metadata->liveBytes() && !owners->liveBytes());
            }
        // Logical closure retains physical observers without delivering a GPU
        // terminal. Explicit suppression and real loss remain independent.
        for (const auto logical : std::array{Event::Shutdown, Event::Retired})
            for (const bool suppress : std::array{false, true}) {
                auto registry = factory();
                unsigned calls = 0u, destroyed = 0u;
                auto receiver = core::makeShared<Receiver>(*registry, calls, destroyed);
                Registry::Ticket ticket;
                assert(registry->attach(receiver, &Receiver::consume, ticket) == Status::Complete);
                receiver->ticket = ticket;
                std::weak_ptr<Receiver> borrowed = receiver;
                receiver.reset();
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    std::array{metadata, owners}, [&]() noexcept -> const char* {
                        assert(registry->closeAdmission(logical) == Status::Complete);
                        assert(!registry->accepting() && !registry->delivered()
                            && registry->pending() == 1u && !calls && !borrowed.expired());
                        assert(registry->closeAdmission(Event::Lost) == Status::Closed
                            && *registry->terminal() == logical);
                        Registry::Ticket rejected;
                        assert(
                            registry->attach({}, nullptr, rejected) == Status::Closed && !rejected);
                        if (suppress)
                            assert(registry->suppress(ticket, [](const void*) noexcept {
                                return true;
                            }) == Status::Complete);
                        assert(registry->deliver(Event::Lost) == Status::Complete
                            && registry->delivered() && *registry->terminal() == Event::Lost);
                        assert(!registry->pending() && borrowed.expired() && destroyed == 1u
                            && calls == (suppress ? 0u : 1u));
                        assert(registry->deliver(logical) == Status::Closed
                            && *registry->terminal() == Event::Lost);
                        return nullptr;
                    }));
            }
        // Reusing one stable slot never makes the earlier ticket current.
        {
            core::CpuMemoryQuotaScope scope(metadata);
            Registry registry(1u), other(1u);
            auto receiver = core::makeShared<Receiver>(registry, discardCalls_, discardDestroyed_);
            Registry::Ticket first, next, foreign;
            assert(registry.attach(receiver, &Receiver::consume, first) == Status::Complete);
            assert(other.attach(receiver, &Receiver::consume, foreign) == Status::Complete);
            assert(registry.inspect(foreign) == Status::Foreign
                && other.inspect(first) == Status::Foreign);
            assert(registry.suppress(first, [](const void*) noexcept {
                return true;
            }) == Status::Complete);
            assert(registry.attach(receiver, &Receiver::consume, next) == Status::Complete
                && next.generation > first.generation);
            assert(registry.inspect(first) == Status::Stale
                && registry.inspect(next) == Status::Complete);
            assert(registry.suppress(next, [](const void*) noexcept {
                return true;
            }) == Status::Complete);
            assert(other.suppress(foreign, [](const void*) noexcept {
                return true;
            }) == Status::Complete);
        }
        assert(!metadata->liveBytes());
        // Same C++ storage address is a new instance/domain, even with the same
        // slot and first generation. No stale ticket can cross object lifetime.
        {
            core::CpuMemoryQuotaScope scope(metadata);
            std::optional<Registry> storage;
            Registry::Ticket previous, next;
            unsigned calls = 0u, destroyed = 0u;
            storage.emplace(1u);
            auto receiver = core::makeShared<Receiver>(*storage, calls, destroyed);
            assert(storage->attach(receiver, &Receiver::consume, previous) == Status::Complete);
            assert(storage->suppress(previous, [](const void*) noexcept {
                return true;
            }) == Status::Complete);
            receiver.reset();
            storage.reset();
            storage.emplace(1u);
            receiver = core::makeShared<Receiver>(*storage, calls, destroyed);
            assert(storage->attach(receiver, &Receiver::consume, next) == Status::Complete
                && next.domain != previous.domain);
            assert(storage->inspect(previous) == Status::Foreign);
            assert(storage->suppress(next, [](const void*) noexcept {
                return true;
            }) == Status::Complete);
        }
        assert(!metadata->liveBytes());
        {
            core::CpuMemoryQuotaScope scope(metadata);
            Registry last(1u, UINT64_MAX), exhausted(1u, 0u), empty(0u);
            auto receiver = core::makeShared<Receiver>(last, discardCalls_, discardDestroyed_);
            Registry::Ticket ticket;
            assert(last.attach(receiver, &Receiver::consume, ticket) == Status::Complete
                && ticket.generation == UINT64_MAX);
            assert(last.suppress(ticket, [](const void*) noexcept {
                return true;
            }) == Status::Complete);
            assert(last.attach(receiver, &Receiver::consume, ticket) == Status::IdentityExhausted
                && !ticket && !last.accepting());
            assert(
                exhausted.attach(receiver, &Receiver::consume, ticket) == Status::IdentityExhausted
                && !ticket);
            assert(
                empty.attach(receiver, &Receiver::consume, ticket) == Status::Capacity && !ticket);
        }
        assert(!metadata->liveBytes() && !owners->liveBytes());
    }

private:
    inline static unsigned discardCalls_ = 0u, discardDestroyed_ = 0u;
};
} // namespace arp::test
