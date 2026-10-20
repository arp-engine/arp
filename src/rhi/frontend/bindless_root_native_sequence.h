#pragma once
#include "rhi/frontend/bindless_root_sequence.h"

#include <initializer_list>

namespace arp::rhi {
// Inline custody metadata in the original native command/admission. There is
// no execution issuer, resource inventory, state graph or completion owner here.
// Storage is allocated once under that command's quota; Pending only inspects.
template <class Closure> class OriginalBindlessRootNativeSequence final {
public:
    using Owner = std::shared_ptr<const Closure>;

    // The compatibility prefix follows the same distinct-Root identity rule
    // as the full sequence. Repeated first/next/final bindings must not create
    // different authentic closures for one canonical Source.
    template <class Prepare>
    static const char* prepareHistoricalInput(
        const std::array<std::shared_ptr<const OriginalCommandBindlessRootSource>, 3u>& roots,
        std::array<Owner, 3u>& destination,
        Prepare&& prepare)
    {
        for (const auto& owner : destination)
            if (owner)
                return "RHI-BINDLESS-ROOT-NATIVE-SEQUENCE-DESTINATION-NOT-EMPTY";
        if (!roots[0u])
            return "RHI-BINDLESS-ROOT-NATIVE-SEQUENCE-INPUTS";
        std::array<Owner, 3u> prepared;
        bool ended = false;
        for (std::size_t i = 0u; i < roots.size(); ++i) {
            if (!roots[i]) {
                ended = true;
                continue;
            }
            if (ended)
                return "RHI-BINDLESS-ROOT-NATIVE-SEQUENCE-HOLE";
            if (const auto* id = OriginalCommandBindlessRootSource::validateOriginal(roots[i]))
                return id;
            for (std::size_t prior = 0u; prior < i; ++prior)
                if (roots[prior].get() == roots[i].get()
                    && core::SharedOwnerProjection::sameOwner(roots[prior], roots[i])) {
                    prepared[i] = prepared[prior];
                    break;
                }
            if (prepared[i])
                continue;
            const char* diagnostic = nullptr;
            prepared[i] = prepare(roots[i], diagnostic);
            if (!prepared[i] || diagnostic)
                return diagnostic ? diagnostic : "RHI-BINDLESS-ROOT-NATIVE-SEQUENCE-FACTORY";
            if (prepared[i]->originalRoot().get() != roots[i].get()
                || !core::SharedOwnerProjection::sameOwner(prepared[i]->originalRoot(), roots[i]))
                return "RHI-BINDLESS-ROOT-NATIVE-SEQUENCE-FACTORY-CORRESPONDENCE";
        }
        destination.swap(prepared);
        return nullptr;
    }

    struct Input {
        const Owner* closures = nullptr;
        std::size_t count = 0u;
        OriginalBindlessRootSequenceContract::Budget budget;

        bool empty() const noexcept
        {
            return !closures && !count && !budget.contractVersion && !budget.prototypeOptIn
                && !budget.generation && !budget.maxBindings && !budget.maxResourceVisits
                && budget.snapshotScope.empty();
        }
    };

private:
    void initialize(const Owner* closures, std::size_t count)
    {
        if (!closures || !count) {
            diagnostic_ = "RHI-BINDLESS-ROOT-NATIVE-SEQUENCE-INPUTS";
            return;
        }
        closures_.reserve(count);
        bool ended = false;
        for (std::size_t i = 0u; i < count; ++i) {
            if (!closures[i]) {
                ended = true;
                continue;
            }
            if (ended) {
                closures_.clear();
                diagnostic_ = "RHI-BINDLESS-ROOT-NATIVE-SEQUENCE-HOLE";
                return;
            }
            closures_.push_back(closures[i]);
        }
        if (closures_.empty())
            diagnostic_ = "RHI-BINDLESS-ROOT-NATIVE-SEQUENCE-INPUTS";
    }

public:
    // Project the public Source sequence once under the caller's native quota.
    // Rebindings reuse the same closure, avoiding ambiguous Source selection
    // and duplicate backend allocations. The factory authenticates new Roots;
    // this adapter cannot issue native execution or completion authority.
    template <class Prepare>
    static const char* preparePublicInput(const OriginalBindlessRootSequenceContract::Input& input,
        std::initializer_list<Owner> historical,
        const std::shared_ptr<const OriginalCommandResourceSources>& inventory,
        const std::shared_ptr<const OriginalCommandStreamProgram>& stream,
        core::pmr::Vector<Owner>& destination,
        Prepare&& prepare)
    {
        if (!destination.empty())
            return "RHI-BINDLESS-ROOT-NATIVE-SEQUENCE-DESTINATION-NOT-EMPTY";
        if (input.empty())
            return nullptr;
        if (historical.size() != 3u)
            return "RHI-BINDLESS-ROOT-NATIVE-SEQUENCE-HISTORICAL-CORRESPONDENCE";
        std::array<std::shared_ptr<const OriginalCommandBindlessRootSource>, 3u> roots;
        std::size_t index = 0u;
        for (const auto& closure : historical)
            roots[index++] = closure ? closure->originalRoot() : nullptr;
        if (const auto* id = OriginalBindlessRootSequenceContract::inspectPublicInput(
                input, roots, inventory, stream))
            return id;
        core::pmr::Vector<Owner> prepared(destination.get_allocator());
        prepared.reserve(input.count);
        index = 0u;
        for (const auto& closure : historical) {
            if (index++ == input.count)
                break;
            prepared.push_back(closure);
        }
        for (std::size_t i = prepared.size(); i < input.count; ++i) {
            Owner selected;
            for (const auto& retained : prepared)
                if (retained->originalRoot().get() == input.roots[i].get()
                    && core::SharedOwnerProjection::sameOwner(
                        retained->originalRoot(), input.roots[i])) {
                    selected = retained;
                    break;
                }
            if (!selected) {
                const char* diagnostic = nullptr;
                selected = prepare(input.roots[i], diagnostic);
                if (!selected || diagnostic)
                    return diagnostic ? diagnostic : "RHI-BINDLESS-ROOT-NATIVE-SEQUENCE-FACTORY";
                if (selected->originalRoot().get() != input.roots[i].get()
                    || !core::SharedOwnerProjection::sameOwner(
                        selected->originalRoot(), input.roots[i])
                    || !core::SharedOwnerProjection::sameOwner(
                        selected->originalInventory(), inventory))
                    return "RHI-BINDLESS-ROOT-NATIVE-SEQUENCE-FACTORY-CORRESPONDENCE";
            }
            prepared.push_back(std::move(selected));
        }
        destination.swap(prepared);
        return nullptr;
    }

    OriginalBindlessRootNativeSequence(const Owner* closures, std::size_t count)
    {
        initialize(closures, count);
    }

    OriginalBindlessRootNativeSequence(std::initializer_list<Owner> closures)
        : OriginalBindlessRootNativeSequence(closures.begin(), closures.size())
    {}

    // Explicit sequence custody replaces the historical first/next/final
    // storage only after the cold factory authenticates the entire stream.
    // Borrowed caller metadata is never retained by the command.
    OriginalBindlessRootNativeSequence(std::initializer_list<Owner> historical, const Input& input)
    {
        if (input.empty()) {
            initialize(historical.begin(), historical.size());
            return;
        }
        budget_ = input.budget;
        diagnostic_ = OriginalBindlessRootSequenceContract::validateBudget(budget_);
        if (diagnostic_)
            return;
        if (!input.closures || !input.count || input.count > budget_.maxBindings) {
            diagnostic_ = "RHI-BINDLESS-ROOT-NATIVE-SEQUENCE-INPUT-CAPACITY";
            return;
        }
        initialize(input.closures, input.count);
        if (!diagnostic_ && input.count != closures_.size())
            diagnostic_ = "RHI-BINDLESS-ROOT-NATIVE-SEQUENCE-INPUT-CAPACITY";
        if (diagnostic_)
            closures_.clear();
    }

    template <class Validate>
    static const char* inspectInput(const Input& input,
        const std::shared_ptr<const OriginalCommandResourceSources>& inventory,
        const std::shared_ptr<const OriginalCommandStreamProgram>& stream,
        Validate&& validateClosure) noexcept
    {
        if (input.empty())
            return nullptr;
        if (const auto* id = OriginalBindlessRootSequenceContract::validateBudget(input.budget))
            return id;
        if (!input.closures || !input.count || input.count > input.budget.maxBindings)
            return "RHI-BINDLESS-ROOT-NATIVE-SEQUENCE-INPUT-CAPACITY";
        for (std::size_t i = 0u; i < input.count; ++i) {
            if (!input.closures[i])
                return "RHI-BINDLESS-ROOT-NATIVE-SEQUENCE-INPUTS";
            if (const auto* id = validateClosure(input.closures[i]))
                return id;
            if (!core::SharedOwnerProjection::sameOwner(
                    input.closures[i]->originalInventory(), inventory))
                return "RHI-BINDLESS-ROOT-NATIVE-SEQUENCE-INVENTORY";
        }
        return OriginalBindlessRootSequenceContract::inspectProjected(
            input.count,
            [&](std::size_t i) noexcept -> const auto& {
                return input.closures[i]->originalRoot();
            },
            inventory,
            stream,
            input.budget)
            .diagnostic;
    }

    static const char* inspectHistorical(
        const Input& input, std::initializer_list<Owner> historical) noexcept
    {
        if (input.empty())
            return nullptr;
        if (!input.closures || !input.count)
            return "RHI-BINDLESS-ROOT-NATIVE-SEQUENCE-INPUTS";
        std::size_t i = 0u;
        for (const auto& owner : historical) {
            const bool present = i < input.count;
            if (present != bool(owner)
                || (present
                    && (input.closures[i].get() != owner.get()
                        || !core::SharedOwnerProjection::sameOwner(input.closures[i], owner))))
                return "RHI-BINDLESS-ROOT-NATIVE-SEQUENCE-HISTORICAL-CORRESPONDENCE";
            ++i;
        }
        return nullptr;
    }

    Input input() const noexcept
    {
        return budget_.contractVersion ? Input{closures_.data(), closures_.size(), budget_}
                                       : Input{};
    }

    // Conservative bound for extra closure owners. Factories already count
    // their historical three slots; duplicate bindings may over-reserve but
    // never undercount custody or overflow into an admitted smaller budget.
    static std::uint64_t additionalOwnerBound(
        const Input& input, std::size_t accountedPrefix = 3u) noexcept
    {
        if (input.empty())
            return 0u;
        if (!input.closures)
            return UINT64_MAX;
        std::uint64_t owners = 0u;
        for (std::size_t i = accountedPrefix; i < input.count; ++i) {
            if (!input.closures[i])
                return UINT64_MAX;
            const auto count = std::uint64_t(input.closures[i]->retainedOwnerCount()) + 1u;
            if (count > UINT64_MAX - owners)
                return UINT64_MAX;
            owners += count;
        }
        return owners;
    }

    OriginalBindlessRootNativeSequence(const OriginalBindlessRootNativeSequence&) = delete;
    OriginalBindlessRootNativeSequence& operator=(
        const OriginalBindlessRootNativeSequence&) = delete;

    const auto& closures() const noexcept { return closures_; }

    const char* diagnostic() const noexcept { return diagnostic_; }

    // Compatibility references for historical finite profiles. They do not add
    // retained owners; all native closure custody belongs to closures_.
    const Owner& atOrEmpty(std::size_t i) const noexcept
    {
        static const Owner empty;
        return i < closures_.size() ? closures_[i] : empty;
    }

    const Closure* select(const std::shared_ptr<const OriginalCommandResourceSources>& inventory,
        std::size_t source) const noexcept
    {
        if (diagnostic_)
            return nullptr;
        return OriginalBindlessRootSequenceContract::selectClosure(
            closures_.size(),
            [&](std::size_t i) noexcept -> const auto& {
                return closures_[i];
            },
            inventory,
            source);
    }

    template <class Validate>
    const char* validate(const std::shared_ptr<const OriginalCommandResourceSources>& inventory,
        const std::shared_ptr<const OriginalCommandStreamProgram>& stream,
        Validate&& validateClosure) const noexcept
    {
        if (diagnostic_)
            return diagnostic_;
        for (const auto& closure : closures_)
            if (const auto* id = validateClosure(closure))
                return id;
        if (!inventory)
            return "RHI-BINDLESS-ROOT-NATIVE-SEQUENCE-INVENTORY";
        for (const auto& closure : closures_)
            if (select(inventory, inventory->originalLeaseIndex(closure->originalRoot()))
                != closure.get())
                return "RHI-BINDLESS-ROOT-NATIVE-SEQUENCE-AMBIGUOUS";
        if (!stream)
            return "RHI-BINDLESS-ROOT-NATIVE-SEQUENCE-STREAM";
        const auto commands = stream->originalBudget().maxCommands;
        if (closures_.size() > UINT64_MAX - commands)
            return "RHI-BINDLESS-ROOT-SEQUENCE-VISIT-OVERFLOW";
        const auto selectedBudget = budget_.contractVersion
            ? budget_
            : OriginalBindlessRootSequenceContract::Budget{1u,
                  true,
                  closures_[0]->originalRoot()->originalSnapshot()->generation(),
                  closures_.size(),
                  commands + closures_.size()};
        return OriginalBindlessRootSequenceContract::inspectProjected(
            closures_.size(),
            [&](std::size_t i) noexcept -> const auto& {
                return closures_[i]->originalRoot();
            },
            inventory,
            stream,
            selectedBudget)
            .diagnostic;
    }

private:
    const char* diagnostic_ = nullptr;
    OriginalBindlessRootSequenceContract::Budget budget_;
    core::pmr::Vector<Owner> closures_{&core::defaultCpuMemoryResource()};
};
} // namespace arp::rhi
