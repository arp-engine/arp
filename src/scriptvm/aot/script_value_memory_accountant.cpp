#include "scriptvm/aot/script_value_memory_accountant.h"

#include "reflection/enum.h"
#include "reflection/instance.h"
#include "scriptvm/aot/aot_v5.h"
#include "scriptvm/runtime/vm_error.h"

#include <algorithm>
#include <functional>
#include <limits>

namespace arp::scriptvm {
namespace {

using IdentitySet = core::pmr::UnorderedSet<const void*>;
using ChargeMap = core::pmr::UnorderedMap<const void*, std::size_t>;

void setFailure(VmError* error, ScriptDiagnosticCode code, const char* message) noexcept;

bool addBytes(std::size_t bytes, std::size_t& total) noexcept
{
    if (bytes > std::numeric_limits<std::size_t>::max() - total)
        return false;
    total += bytes;
    return true;
}

bool addCharge(ChargeMap& charges, const void* identity, std::size_t bytes, VmError* error)
{
    if (identity == nullptr || bytes == 0u)
        return true;
    try {
        const auto [it, inserted] = charges.try_emplace(identity, bytes);
        if (!inserted)
            it->second = std::max(it->second, bytes);
        return true;
    } catch (const std::bad_alloc&) {
        setFailure(error,
            ScriptDiagnosticCode::ResourceLimitExceeded,
            "DirectNative Value root accounting allocation failed");
        return false;
    }
}

bool addAllocationForAddress(ChargeMap& charges,
    const void* address,
    const core::CpuMemoryQuota* quota,
    VmError* error,
    bool required = true)
{
    if (address == nullptr)
        return !required;
    const auto allocation = core::inspectCpuAllocationContaining(address);
    if (!allocation.found) {
        if (required) {
            setFailure(error,
                ScriptDiagnosticCode::AotSwitchUnsupported,
                "DirectNative retained Value has no allocator ownership contract");
        }
        return !required;
    }
    return allocation.quota == quota
        || addCharge(charges, allocation.allocationIdentity, allocation.sizeBytes, error);
}

void setFailure(VmError* error, ScriptDiagnosticCode code, const char* message) noexcept
{
    if (error == nullptr)
        return;
    error->code = code;
    try {
        error->message = message;
    } catch (const std::bad_alloc&) {
        error->message.clear();
    }
}

bool pushValue(core::pmr::Vector<const Value*>& pending, const Value* value, VmError* error)
{
    if (value == nullptr)
        return true;
    try {
        pending.push_back(value);
        return true;
    } catch (const std::bad_alloc&) {
        setFailure(error,
            ScriptDiagnosticCode::ResourceLimitExceeded,
            "DirectNative Value root accounting allocation failed");
        return false;
    }
}

bool pushCell(core::pmr::Vector<const Value*>& pending,
    IdentitySet& visitedCells,
    const std::shared_ptr<ScriptCell>& cell,
    ChargeMap& charges,
    const core::CpuMemoryQuota* quota,
    VmError* error)
{
    if (cell == nullptr || !visitedCells.insert(cell.get()).second)
        return true;
    if (!addAllocationForAddress(charges, cell.get(), quota, error))
        return false;
    return pushValue(pending, &cell->value, error);
}

} // namespace

ScriptValueMemoryAccountant::ScriptValueMemoryAccountant(
    std::shared_ptr<core::CpuMemoryQuota> quota)
    : quota_(std::move(quota))
{}

bool ScriptValueMemoryAccountant::synchronize(const core::pmr::Vector<Value>& globals,
    const ScriptValueSpan* spans,
    std::size_t spanCount,
    const core::pmr::Vector<std::shared_ptr<ScriptCell>>* captures,
    std::size_t maxVisitedValues,
    ScriptValueMemoryDescriptorFn descriptor,
    void* descriptorUserData,
    VmError* error)
{
    return synchronizeOwner(this,
        globals,
        spans,
        spanCount,
        captures,
        maxVisitedValues,
        descriptor,
        descriptorUserData,
        error);
}

bool ScriptValueMemoryAccountant::synchronizeOwner(const void* owner,
    const core::pmr::Vector<Value>& globals,
    const ScriptValueSpan* spans,
    std::size_t spanCount,
    const core::pmr::Vector<std::shared_ptr<ScriptCell>>* captures,
    std::size_t maxVisitedValues,
    ScriptValueMemoryDescriptorFn descriptor,
    void* descriptorUserData,
    VmError* error)
{
    if (owner == nullptr) {
        setFailure(error,
            ScriptDiagnosticCode::AotArtifactInvalid,
            "DirectNative Value root owner is unavailable");
        return false;
    }
    core::CpuMemoryQuotaScope accountingScope{nullptr};
    core::UniquePtr<TraversalWorkspace> recursiveWorkspace;
    TraversalWorkspace* workspace = nullptr;
    const bool mainWorkspace = !workspaceBusy_;
    try {
        if (mainWorkspace) {
            if (!workspace_)
                workspace_ = core::makeUnique<TraversalWorkspace>();
            workspace = workspace_.get();
            workspaceBusy_ = true;
        } else {
            recursiveWorkspace = core::makeUnique<TraversalWorkspace>();
            workspace = recursiveWorkspace.get();
        }
    } catch (const std::bad_alloc&) {
        setFailure(error,
            ScriptDiagnosticCode::ResourceLimitExceeded,
            "DirectNative Value root accounting allocation failed");
        return false;
    }

    struct WorkspaceLease {
        TraversalWorkspace& workspace;
        bool& busy;
        bool main;

        ~WorkspaceLease()
        {
            workspace.clear();
            if (main)
                busy = false;
        }
    } scratchLease{*workspace, workspaceBusy_, mainWorkspace};

    auto& pending = workspace->pending;
    auto& visitedValues = workspace->values;
    auto& visitedCells = workspace->cells;
    auto& visitedDescriptors = workspace->descriptors;
    ChargeMap candidateCharges{&core::defaultCpuMemoryResource()};

    try {
        pending.reserve(globals.size() + spanCount);
        for (const auto& value : globals)
            pending.push_back(&value);
        for (std::size_t spanIndex = 0u; spanIndex < spanCount; ++spanIndex) {
            const auto& span = spans[spanIndex];
            if (span.count != 0u && span.values == nullptr) {
                setFailure(error,
                    ScriptDiagnosticCode::AotArtifactInvalid,
                    "DirectNative Value root span is invalid");
                return false;
            }
            for (std::size_t index = 0u; index < span.count; ++index)
                pending.push_back(span.values + index);
        }
        if (captures != nullptr) {
            for (const auto& cell : *captures) {
                if (!pushCell(pending, visitedCells, cell, candidateCharges, quota_.get(), error))
                    return false;
            }
        }
    } catch (const std::bad_alloc&) {
        setFailure(error,
            ScriptDiagnosticCode::ResourceLimitExceeded,
            "DirectNative Value root accounting allocation failed");
        return false;
    }

    std::size_t visitedCount = 0u;
    while (!pending.empty()) {
        const Value* value = pending.back();
        pending.pop_back();
        if (value == nullptr)
            continue;
        const void* identity = value->storageIdentity();
        if (identity == nullptr || !visitedValues.insert(identity).second)
            continue;
        ++visitedCount;
        if (maxVisitedValues != 0u && visitedCount > maxVisitedValues) {
            setFailure(error,
                ScriptDiagnosticCode::ResourceLimitExceeded,
                "DirectNative retained Value graph exceeds the value limit");
            return false;
        }

        const auto charge = core::inspectCpuAllocationContaining(identity);
        if (!charge.found) {
            setFailure(error,
                ScriptDiagnosticCode::AotSwitchUnsupported,
                "DirectNative Value has no allocator ownership contract");
            return false;
        }
        const bool objectAlreadyCharged = charge.quota == quota_.get();
        if (!objectAlreadyCharged
            && !addCharge(candidateCharges, charge.allocationIdentity, charge.sizeBytes, error)) {
            return false;
        }

        // Canonical native references retain only their invalidation control.
        // They borrow the engine-owned target; traversing/charging that target
        // would invent ownership and could access an already retired object.
        if (const auto* native =
                exactValuePayload<reflection::detail::NativeObjectReference>(*value)) {
            if (native->control()
                && !addAllocationForAddress(
                    candidateCharges, native->control(), quota_.get(), error))
                return false;
            continue;
        }

        if (value->is<void>() || value->is<std::nullptr_t>() || value->is<bool>()
            || value->is<int>() || value->is<std::int64_t>() || value->is<double>()
            || value->is<float>() || value->is<std::uint64_t>() || value->is<std::uint32_t>()
            || value->is<std::uint16_t>() || value->is<std::uint8_t>() || value->is<std::int16_t>()
            || value->is<std::int8_t>()) {
            continue;
        }
        if (value->is<core::pmr::String>()) {
            const auto* string = value->ptr<core::pmr::String>();
            if (string != nullptr && string->data() != nullptr
                && !addAllocationForAddress(
                    candidateCharges, string->data(), quota_.get(), error)) {
                return false;
            }
            continue;
        }
        if (const auto* enumeration = checkedOwnedValuePayload<reflection::enumeration>(*value)) {
            if (!enumeration->name().empty()
                && !addAllocationForAddress(
                    candidateCharges, enumeration->name().data(), quota_.get(), error))
                return false;
            for (const auto& item : enumeration->values()) {
                if (!addAllocationForAddress(candidateCharges, &item, quota_.get(), error))
                    return false;
                if (!item.first.empty()
                    && !addAllocationForAddress(
                        candidateCharges, item.first.data(), quota_.get(), error))
                    return false;
            }
            continue;
        }
        if (value->is<ScriptBytes>()) {
            const auto* bytes = value->ptr<ScriptBytes>();
            if (bytes != nullptr && bytes->data.data() != nullptr
                && !addAllocationForAddress(
                    candidateCharges, bytes->data.data(), quota_.get(), error))
                return false;
            continue;
        }
        if (value->is<arp::reflection::hlist>()) {
            const auto* list = value->ptr<arp::reflection::hlist>();
            if (list != nullptr) {
                if (list->data() != nullptr
                    && !addAllocationForAddress(
                        candidateCharges, list->data(), quota_.get(), error))
                    return false;
                for (const auto& child : *list)
                    if (!pushValue(pending, &child, error))
                        return false;
            }
            continue;
        }
        if (value->is<arp::reflection::hdict>()) {
            const auto* dict = value->ptr<arp::reflection::hdict>();
            if (dict != nullptr) {
                for (const auto& item : *dict) {
                    if (!addAllocationForAddress(candidateCharges, &item, quota_.get(), error))
                        return false;
                    if (!pushValue(pending, &item.first, error)
                        || !pushValue(pending, &item.second, error))
                        return false;
                }
            }
            continue;
        }
        if (value->is<arp::reflection::instance>()) {
            const auto* instance = value->ptr<arp::reflection::instance>();
            if (instance != nullptr) {
                for (const auto& item : instance->slots()) {
                    if (!addAllocationForAddress(candidateCharges, &item, quota_.get(), error))
                        return false;
                    if (!item.first.empty()
                        && !addAllocationForAddress(
                            candidateCharges, item.first.data(), quota_.get(), error))
                        return false;
                    if (!pushValue(pending, &item.second, error))
                        return false;
                }
            }
            continue;
        }
        if (value->is<ScriptCell>()) {
            const auto* cell = value->ptr<ScriptCell>();
            if (cell != nullptr && !pushValue(pending, &cell->value, error))
                return false;
            continue;
        }
        if (value->is<ScriptClosure>()) {
            const auto* closure = value->ptr<ScriptClosure>();
            if (closure != nullptr) {
                if (closure->captures.data() != nullptr
                    && !addAllocationForAddress(
                        candidateCharges, closure->captures.data(), quota_.get(), error))
                    return false;
                if (closure->functionId.data() != nullptr
                    && !addAllocationForAddress(
                        candidateCharges, closure->functionId.data(), quota_.get(), error))
                    return false;
                for (const auto& cell : closure->captures)
                    if (!pushCell(
                            pending, visitedCells, cell, candidateCharges, quota_.get(), error))
                        return false;
            }
            continue;
        }
        if (value->is<ScriptPointer>()) {
            const auto* pointer = value->ptr<ScriptPointer>();
            if (pointer != nullptr) {
                if (!pushCell(
                        pending, visitedCells, pointer->cell, candidateCharges, quota_.get(), error)
                    || !pushValue(pending, &pointer->base, error)
                    || !pushValue(pending, &pointer->index, error))
                    return false;
                if (pointer->field.data() != nullptr
                    && !addAllocationForAddress(
                        candidateCharges, pointer->field.data(), quota_.get(), error))
                    return false;
            }
            continue;
        }
        if (value->is<ScriptMethod>()) {
            const auto* method = value->ptr<ScriptMethod>();
            if (method != nullptr && !pushValue(pending, &method->receiver, error))
                return false;
            continue;
        }
        if (value->is<ReflectedMethod>()) {
            const auto* method = value->ptr<ReflectedMethod>();
            if (method != nullptr && !pushValue(pending, &method->receiver, error))
                return false;
            continue;
        }
        if (value->is<ScriptAwaitable>()) {
            const auto* awaitable = value->ptr<ScriptAwaitable>();
            if (awaitable != nullptr && !pushValue(pending, &awaitable->operation, error))
                return false;
            continue;
        }
        if (value->is<DirectNativeModuleReferenceV5>()) {
            const auto* module = value->ptr<DirectNativeModuleReferenceV5>();
            if (module != nullptr && module->moduleId.data() != nullptr
                && !addAllocationForAddress(
                    candidateCharges, module->moduleId.data(), quota_.get(), error))
                return false;
            continue;
        }
        if (value->is<DirectNativeGeneratorV5>()) {
            const auto* generator = value->ptr<DirectNativeGeneratorV5>();
            if (generator != nullptr && generator->state != nullptr) {
                if (!addAllocationForAddress(
                        candidateCharges, generator->state.get(), quota_.get(), error))
                    return false;
                if (generator->state->initialArguments.data() != nullptr
                    && !addAllocationForAddress(candidateCharges,
                        generator->state->initialArguments.data(),
                        quota_.get(),
                        error))
                    return false;
                if (generator->state->captures.data() != nullptr
                    && !addAllocationForAddress(
                        candidateCharges, generator->state->captures.data(), quota_.get(), error))
                    return false;
                for (const auto& argument : generator->state->initialArguments)
                    if (!pushValue(pending, &argument, error))
                        return false;
                for (const auto& cell : generator->state->captures)
                    if (!pushCell(
                            pending, visitedCells, cell, candidateCharges, quota_.get(), error))
                        return false;
                if (!pushValue(pending, &generator->state->resume.injectionValue, error))
                    return false;
            }
            continue;
        }

        ScriptValueMemoryDescriptor described;
        if (descriptor == nullptr || !descriptor(descriptorUserData, *value, described)
            || described.allocationIdentity == nullptr) {
            setFailure(error,
                ScriptDiagnosticCode::AotSwitchUnsupported,
                "DirectNative opaque Value has no memory descriptor");
            if (error && value->class_()) {
                error->message += ": ";
                error->message += value->class_()->name();
            }
            return false;
        }
        const bool firstDescriptor = visitedDescriptors.insert(described.allocationIdentity).second;
        if (!described.externallyLeased && firstDescriptor
            && !addCharge(
                candidateCharges, described.allocationIdentity, described.retainedBytes, error)) {
            return false;
        }
    }

    auto ownerIt = owners_.end();
    bool insertedOwner = false;
    try {
        if (candidateCharges.empty()) {
            ownerIt = owners_.find(owner);
        } else {
            const auto inserted = owners_.try_emplace(owner, &core::defaultCpuMemoryResource());
            ownerIt = inserted.first;
            insertedOwner = inserted.second;
        }
    } catch (const std::bad_alloc&) {
        setFailure(error,
            ScriptDiagnosticCode::ResourceLimitExceeded,
            "DirectNative Value root owner allocation failed");
        return false;
    }

    std::size_t externalBytes = 0u;
    if (!aggregateBytes(owner, &candidateCharges, externalBytes)) {
        if (insertedOwner)
            owners_.erase(ownerIt);
        setFailure(error,
            ScriptDiagnosticCode::ResourceLimitExceeded,
            "DirectNative retained Value accounting overflow");
        return false;
    }
    if (externalBytes == 0u) {
        lease_.release();
    } else if (lease_.quota() == nullptr) {
        if (!lease_.acquire(quota_, externalBytes)) {
            if (insertedOwner)
                owners_.erase(ownerIt);
            setFailure(error,
                ScriptDiagnosticCode::ResourceLimitExceeded,
                "DirectNative retained Value memory budget exceeded");
            return false;
        }
    } else if (!lease_.resize(externalBytes)) {
        if (insertedOwner)
            owners_.erase(ownerIt);
        setFailure(error,
            ScriptDiagnosticCode::ResourceLimitExceeded,
            "DirectNative retained Value memory budget exceeded");
        return false;
    }
    if (candidateCharges.empty()) {
        if (ownerIt != owners_.end())
            owners_.erase(ownerIt);
    } else {
        ownerIt->second.charges.swap(candidateCharges);
        ownerIt->second.visitedValueCount = visitedCount;
    }
    visitedValueCount_ = visitedCount;
    return true;
}

bool ScriptValueMemoryAccountant::aggregateBytes(const void* replacementOwner,
    const ChargeMap* replacementCharges,
    std::size_t& total) const noexcept
{
    total = 0u;
    const auto lessOwner = std::less<const void*>{};
    const auto addMaximum = [&](const void* identity, std::size_t initialBytes) noexcept {
        std::size_t maximum = initialBytes;
        for (const auto& [owner, ledger] : owners_) {
            if (owner == replacementOwner)
                continue;
            const auto found = ledger.charges.find(identity);
            if (found != ledger.charges.end())
                maximum = std::max(maximum, found->second);
        }
        return addBytes(maximum, total);
    };

    if (replacementCharges != nullptr) {
        for (const auto& [identity, bytes] : *replacementCharges) {
            if (!addMaximum(identity, bytes))
                return false;
        }
    }
    for (const auto& [owner, ledger] : owners_) {
        if (owner == replacementOwner)
            continue;
        for (const auto& [identity, bytes] : ledger.charges) {
            if (replacementCharges != nullptr
                && replacementCharges->find(identity) != replacementCharges->end()) {
                continue;
            }
            bool canonicalOwner = true;
            for (const auto& [otherOwner, otherLedger] : owners_) {
                if (otherOwner == replacementOwner || otherOwner == owner)
                    continue;
                if (lessOwner(otherOwner, owner)
                    && otherLedger.charges.find(identity) != otherLedger.charges.end()) {
                    canonicalOwner = false;
                    break;
                }
            }
            if (canonicalOwner && !addMaximum(identity, bytes))
                return false;
        }
    }
    return true;
}

void ScriptValueMemoryAccountant::releaseOwner(const void* owner) noexcept
{
    if (owner == nullptr)
        return;
    const auto found = owners_.find(owner);
    if (found == owners_.end())
        return;
    std::size_t externalBytes = 0u;
    if (!aggregateBytes(owner, nullptr, externalBytes))
        return;
    if (externalBytes == 0u)
        lease_.release();
    else
        (void)lease_.resize(externalBytes);
    owners_.erase(found);
}

bool ScriptValueMemoryAccountant::transferOwner(
    const void* sourceOwner, const void* targetOwner) noexcept
{
    if (sourceOwner == nullptr || targetOwner == nullptr || sourceOwner == targetOwner)
        return false;
    if (owners_.find(targetOwner) != owners_.end())
        return false;
    auto node = owners_.extract(sourceOwner);
    if (node.empty())
        return true;
    node.key() = targetOwner;
    return owners_.insert(std::move(node)).inserted;
}

void ScriptValueMemoryAccountant::reset() noexcept
{
    if (!workspaceBusy_)
        workspace_.reset();
    owners_.clear();
    lease_.release();
    visitedValueCount_ = 0u;
}

} // namespace arp::scriptvm
