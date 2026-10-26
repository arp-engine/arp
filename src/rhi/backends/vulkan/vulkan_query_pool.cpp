#include "rhi/backends/vulkan/vulkan_query_pool.h"

#include "core/memory/cpu_allocator.h"
#include "rhi/semantics/query_lowering.h"

#include <algorithm>
#include <stdexcept>
#include <type_traits>

namespace arp::rhi_vulkan {

VulkanRhiQueryPoolPayload::VulkanRhiQueryPoolPayload(std::shared_ptr<VulkanRhiRuntime> runtime,
    VkQueryPool queryPool,
    core::pmr::Vector<std::uint64_t> scopeIds,
    std::uint32_t count)
    : runtime_(std::move(runtime)), queryPool_(queryPool), scopeIds_(std::move(scopeIds))
{
    slots_.reserve(count);
    for (std::uint32_t i = 0u; i < count; ++i)
        slots_.push_back(core::makeUnique<SlotControl>());
    if (!scopeIds_.empty())
        completedTimestamps_.resize(count);
}

VulkanRhiQueryPoolPayload::~VulkanRhiQueryPoolPayload()
{
    if (runtime_ && queryPool_) {
        if (auto fn = reinterpret_cast<PFN_vkDestroyQueryPool>(
                runtime_->getDeviceProcAddr("vkDestroyQueryPool"))) {
            fn(runtime_->logicalDevice(), queryPool_, nullptr);
        }
    }
}

VkQueryPool VulkanRhiQueryPoolPayload::nativeHandle() const
{
    return queryPool_;
}

bool VulkanRhiQueryPoolPayload::belongsTo(const VulkanRhiRuntime& runtime) const noexcept
{
    return runtime_.get() == &runtime;
}

VulkanRhiQueryPoolPayload::SlotControl::Phase
VulkanRhiQueryPoolPayload::SlotObservation::phase() const noexcept
{
    return std::visit(
        [](const auto& value) noexcept {
            if constexpr (std::is_same_v<std::decay_t<decltype(value)>, SlotControl::Observation>)
                return value.value;
            else
                return value.value();
        },
        value_);
}

std::uint64_t VulkanRhiQueryPoolPayload::SlotObservation::revision() const noexcept
{
    return std::visit(
        [](const auto& value) noexcept {
            if constexpr (std::is_same_v<std::decay_t<decltype(value)>, SlotControl::Observation>)
                return value.revision;
            else
                return value.revision();
        },
        value_);
}

std::optional<VulkanRhiQueryPoolPayload::SlotObservation> VulkanRhiQueryPoolPayload::observeSlot(
    std::uint32_t index, const core::MutationJournal* journal) const noexcept
{
    std::lock_guard lock(mutex_);
    if (index >= slots_.size())
        return {};
    if (journal) {
        if (const auto value = slots_[index]->observePending(*journal))
            return SlotObservation(this, index, *value);
    } else if (const auto value = slots_[index]->observe())
        return SlotObservation(this, index, *value);
    return {};
}

bool VulkanRhiQueryPoolPayload::matchesSlotLocked(
    const SlotObservation& observation, const core::MutationJournal* journal) const noexcept
{
    if (observation.owner_ != this || observation.index_ >= slots_.size())
        return false;
    const auto& control = *slots_[observation.index_];
    return std::visit(
        [&](const auto& value) noexcept {
            if constexpr (std::is_same_v<std::decay_t<decltype(value)>, SlotControl::Observation>)
                return control.matches(value);
            else
                return journal && control.matchesPending(value, *journal);
        },
        observation.value_);
}

bool VulkanRhiQueryPoolPayload::matchesSlot(
    const SlotObservation& observation, const core::MutationJournal* journal) const noexcept
{
    std::lock_guard lock(mutex_);
    return matchesSlotLocked(observation, journal);
}

const char* VulkanRhiQueryPoolPayload::preflightRecordLocked(
    const SlotObservation& observation, core::MutationJournal* journal) const noexcept
{
    if (!matchesSlotLocked(observation, journal))
        return "RHI-VULKAN-QUERY-SLOT-OBSERVATION";
    const auto& control = *slots_[observation.index_];
    if (control.phase() != SlotControl::Phase::NotRecorded)
        return "RHI-VULKAN-QUERY-MUST-BE-RESET-FIRST";
    return control.preflight(SlotControl::Event::Record, journal, 4u);
}

const char* VulkanRhiQueryPoolPayload::preflightRecord(
    const SlotObservation& observation, core::MutationJournal* journal) const noexcept
{
    std::lock_guard lock(mutex_);
    return preflightRecordLocked(observation, journal);
}

rhi::QueryExecutionState VulkanRhiQueryPoolPayload::slotState(const SlotControl& control) noexcept
{
    using Phase = SlotControl::Phase;
    using State = rhi::QueryExecutionState;
    if (!control.active())
        return State::DiagnosticFailure;
    switch (control.phase()) {
    case Phase::NotRecorded:
        return State::NotRecorded;
    case Phase::Recorded:
        return State::Recorded;
    case Phase::Submitted:
    case Phase::Completed:
        return State::Pending;
    case Phase::SnapshotDeclared:
        return State::Available;
    case Phase::Quarantined:
    case Phase::FailedIdle:
        return State::DiagnosticFailure;
    }
    return State::DiagnosticFailure;
}

void VulkanRhiQueryPoolPayload::failCompletedSlot(SlotControl& control) noexcept
{
    using Event = SlotControl::Event;
    if (control.phase() == SlotControl::Phase::FailedIdle)
        return;
    if (control.phase() == SlotControl::Phase::Quarantined) {
        if (control.preflight(Event::RetireFailure) || control.declare(Event::RetireFailure))
            control.invalidate();
        return;
    }
    if (control.phase() != SlotControl::Phase::Submitted
        && control.phase() != SlotControl::Phase::Completed) {
        control.invalidate();
        return;
    }
    if (control.preflight(Event::Fail, nullptr, 2u) || control.declare(Event::Fail)
        || control.declare(Event::RetireFailure))
        control.invalidate();
}

const char* VulkanRhiQueryPoolPayload::createNativePool(std::uint32_t count) noexcept
{
    if (const auto* diagnostic = runtime_->timestampQueryDiagnostic())
        return diagnostic;
    const auto create =
        reinterpret_cast<PFN_vkCreateQueryPool>(runtime_->getDeviceProcAddr("vkCreateQueryPool"));
    if (!create)
        return "RHI-VULKAN-TIMESTAMP-ENTRYPOINTS";
    VkQueryPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = count;
    VkQueryPool pool = VK_NULL_HANDLE;
    const auto result = create(runtime_->logicalDevice(), &info, nullptr, &pool);
    runtime_->captureOriginalDeviceLoss(result);
    if (result != VK_SUCCESS)
        return result == VK_ERROR_DEVICE_LOST ? "RHI-VULKAN-TIMESTAMP-CREATE-DEVICE-LOST"
                                              : "RHI-VULKAN-TIMESTAMP-CREATE-FAILED";
    if (!pool)
        return "RHI-VULKAN-TIMESTAMP-CREATE-PAYLOAD-MISSING";
    queryPool_ = pool;
    return nullptr;
}

const core::pmr::Vector<std::uint64_t>& VulkanRhiQueryPoolPayload::scopeIds() const
{
    return scopeIds_;
}

rhi::GpuTimingSample VulkanRhiQueryPoolPayload::completedTimingSample(
    const VulkanRhiRuntime& runtime, const std::uint64_t frameSerial) const
{
    rhi::GpuTimingSample sample;
    sample.availability = rhi::GpuTimingAvailability::DiagnosticFailure;
    if (!belongsTo(runtime)) {
        sample.diagnostic = "RHI-VULKAN-TIMESTAMP-RESULT-RUNTIME";
        return sample;
    }
    if (const auto* diagnostic = runtime.timestampQueryDiagnostic()) {
        sample.diagnostic = diagnostic;
        return sample;
    }
    std::lock_guard lock(mutex_);
    if (scopeIds_.empty() || scopeIds_.size() > sample.scopes.size()
        || completedTimestamps_.size() != scopeIds_.size() * 2u
        || slots_.size() != completedTimestamps_.size()) {
        sample.diagnostic = "RHI-VULKAN-TIMESTAMP-RESULT-SCOPE-MAPPING";
        return sample;
    }
    // Check the entire generation before exposing even a partial scope.
    bool pending = false;
    for (const auto& slot : slots_) {
        const auto state = slotState(*slot);
        if (state == rhi::QueryExecutionState::DiagnosticFailure) {
            sample.diagnostic = "RHI-VULKAN-TIMESTAMP-RESULT-READBACK";
            return sample;
        }
        pending = pending || state != rhi::QueryExecutionState::Available;
    }
    if (pending) {
        sample.availability = rhi::GpuTimingAvailability::Pending;
        sample.diagnostic = "RHI-VULKAN-TIMESTAMP-RESULT-INCOMPLETE";
        return sample;
    }
    for (std::size_t i = 0u; i < scopeIds_.size(); ++i) {
        const auto duration =
            rhi::semantics::QueryLowering::timestampDuration(completedTimestamps_[i * 2u],
                completedTimestamps_[i * 2u + 1u],
                runtime.nativeLimits().timestampPeriod);
        if (duration.diagnostic) {
            sample.diagnostic = duration.diagnostic;
            return sample;
        }
        sample.scopes[i] = {scopeIds_[i], static_cast<float>(duration.nanoseconds) / 1000000.0f};
    }
    sample.scopeCount = static_cast<std::uint32_t>(scopeIds_.size());
    sample.completedFrameSerial = frameSerial;
    sample.availability = rhi::GpuTimingAvailability::Available;
    sample.diagnostic = "Vulkan completed timestamp scopes";
    return sample;
}

bool VulkanRhiQueryPoolPayload::markRecorded(
    const std::uint32_t queryIndex, std::pmr::string& diagnostics, core::MutationJournal* journal)
{
    std::lock_guard lock(mutex_);
    if (queryIndex >= slots_.size()) {
        diagnostics = "RHI-VULKAN-QUERY-INDEX-OUT-OF-RANGE";
        return false;
    }
    auto& control = *slots_[queryIndex];
    if (control.phase() != SlotControl::Phase::NotRecorded) {
        diagnostics = "RHI-VULKAN-QUERY-MUST-BE-RESET-FIRST";
        return false;
    }
    // Record + Submit + (Complete/Snapshot or Fail/RetireFailure).
    const auto* id = control.preflight(SlotControl::Event::Record, journal, 4u);
    if (!id)
        id = control.declare(SlotControl::Event::Record, journal);
    if (id) {
        diagnostics = id;
        return false;
    }
    return true;
}

bool VulkanRhiQueryPoolPayload::markRecorded(
    const SlotObservation& original, core::MutationJournal& journal, std::pmr::string& diagnostics)
{
    const auto* id = declareRecorded(original, journal);
    if (id) {
        diagnostics = id;
        return false;
    }
    return true;
}

const char* VulkanRhiQueryPoolPayload::declareRecorded(
    const SlotObservation& original, core::MutationJournal& journal) noexcept
{
    std::lock_guard lock(mutex_);
    if (const auto* id = preflightRecordLocked(original, &journal))
        return id;
    return slots_[original.index_]->declare(SlotControl::Event::Record, &journal);
}

bool VulkanRhiQueryPoolPayload::markSubmitted(
    const std::uint32_t queryIndex, std::pmr::string& diagnostics, core::MutationJournal* journal)
{
    std::lock_guard lock(mutex_);
    if (queryIndex >= slots_.size()
        || slots_[queryIndex]->phase() != SlotControl::Phase::Recorded) {
        diagnostics = "RHI-VULKAN-QUERY-SUBMISSION-STATE-INVALID";
        return false;
    }
    auto& control = *slots_[queryIndex];
    // Keep enough revisions for either genuine completion or quarantine/idle.
    if (journal && !control.observePending(*journal)) {
        diagnostics = "RHI-VULKAN-QUERY-SLOT-OBSERVATION";
        return false;
    }
    const auto* id = control.preflight(SlotControl::Event::Submit, journal, 3u);
    if (!id)
        id = control.declare(SlotControl::Event::Submit, journal);
    if (id) {
        diagnostics = id;
        return false;
    }
    return true;
}

bool VulkanRhiQueryPoolPayload::rollbackSubmitted(
    const std::uint32_t queryIndex, std::pmr::string& diagnostics, core::MutationJournal* journal)
{
    std::lock_guard lock(mutex_);
    if (queryIndex >= slots_.size()
        || slots_[queryIndex]->phase() != SlotControl::Phase::Submitted) {
        diagnostics = "RHI-VULKAN-QUERY-ROLLBACK-STATE-INVALID";
        return false;
    }
    auto& control = *slots_[queryIndex];
    if (journal && !control.observePending(*journal)) {
        diagnostics = "RHI-VULKAN-QUERY-SLOT-OBSERVATION";
        return false;
    }
    const auto* id = control.preflight(SlotControl::Event::CancelSubmit, journal);
    if (!id)
        id = control.declare(SlotControl::Event::CancelSubmit, journal);
    if (id) {
        diagnostics = id;
        return false;
    }
    return true;
}

bool VulkanRhiQueryPoolPayload::rollbackRecorded(const std::uint32_t queryIndex)
{
    std::lock_guard lock(mutex_);
    if (queryIndex >= slots_.size())
        return false;
    auto& control = *slots_[queryIndex];
    return !control.preflight(SlotControl::Event::CancelRecord)
        && !control.declare(SlotControl::Event::CancelRecord);
}

bool VulkanRhiQueryPoolPayload::markSubmissionUnproven(
    const std::uint32_t queryIndex, core::MutationJournal* journal) noexcept
{
    std::lock_guard lock(mutex_);
    if (queryIndex >= slots_.size())
        return false;
    auto& control = *slots_[queryIndex];
    if (journal && !control.observePending(*journal))
        return false;
    if (control.phase() != SlotControl::Phase::Submitted
        || control.preflight(SlotControl::Event::Fail, journal, 2u)
        || control.declare(SlotControl::Event::Fail, journal))
        return false;
    return !journal || control.discardPending(*journal);
}

void VulkanRhiQueryPoolPayload::markDiagnosticFailure(const std::uint32_t queryIndex, const char*)
{
    // Only genuine device idle authorizes retiring an unproven submission.
    std::lock_guard lock(mutex_);
    if (queryIndex < slots_.size())
        failCompletedSlot(*slots_[queryIndex]);
}

void VulkanRhiQueryPoolPayload::markCompleted(
    const VulkanRhiRuntime& runtime, const std::uint32_t queryIndex)
{
    std::lock_guard lock(mutex_);
    if (!belongsTo(runtime) || queryIndex >= slots_.size())
        return;
    auto& control = *slots_[queryIndex];
    if (control.phase() != SlotControl::Phase::Submitted)
        return;
    // Preflight both lifecycle writes before any native readback/publication.
    if (control.preflight(SlotControl::Event::Complete, nullptr, 2u)) {
        failCompletedSlot(control);
        return;
    }
    const auto getResults = reinterpret_cast<PFN_vkGetQueryPoolResults>(
        runtime.getDeviceProcAddr("vkGetQueryPoolResults"));
    std::uint64_t value = 0u;
    const auto result = getResults ? getResults(runtime.logicalDevice(),
                                         queryPool_,
                                         queryIndex,
                                         1u,
                                         sizeof(value),
                                         &value,
                                         sizeof(value),
                                         VK_QUERY_RESULT_64_BIT)
                                   : VK_ERROR_INITIALIZATION_FAILED;
    runtime.captureOriginalDeviceLoss(result);
    if (result != VK_SUCCESS) {
        failCompletedSlot(control);
        return;
    }
    if (queryIndex < completedTimestamps_.size())
        completedTimestamps_[queryIndex] = value;
    if (control.declare(SlotControl::Event::Complete)
        || control.declare(SlotControl::Event::DeclareSnapshot))
        control.invalidate();
}

rhi::CommandValidationResult VulkanRhiQueryPoolPayload::reset()
{
    std::lock_guard lock(mutex_);
    if (!runtime_)
        return {false, "RHI-VULKAN-TIMESTAMP-RUNTIME-UNAVAILABLE", 0};
    if (const auto* diagnostic = runtime_->timestampQueryDiagnostic())
        return {false, diagnostic, 0};
    // Every slot must admit the revision before touching the native pool.
    for (const auto& slot : slots_) {
        if (slot->pending() || !SlotControl::resettable(slot->phase()))
            return {false, "RHI-VULKAN-QUERY-POOL-RESET-WAITING-FOR-COMPLETION", 0};
        if (const auto* id = slot->preflight(SlotControl::Event::Reset))
            return {false, id, 0};
    }
    const auto resetQueryPool =
        reinterpret_cast<PFN_vkResetQueryPool>(runtime_->getDeviceProcAddr("vkResetQueryPool"));
    if (!resetQueryPool || queryPool_ == VK_NULL_HANDLE)
        return {false, "RHI-VULKAN-QUERY-POOL-RESET-ENTRYPOINT-MISSING", 0};
    resetQueryPool(
        runtime_->logicalDevice(), queryPool_, 0u, static_cast<std::uint32_t>(slots_.size()));
    for (const auto& slot : slots_) {
        if (const auto* id = slot->declare(SlotControl::Event::Reset)) {
            slot->invalidate();
            return {false, id, 0};
        }
    }
    std::fill(completedTimestamps_.begin(), completedTimestamps_.end(), 0u);
    return {true, "", 0};
}

rhi::QueryExecutionStatus VulkanRhiQueryPoolPayload::executionStatus() const
{
    rhi::QueryExecutionStatus status;
    status.type = rhi::QueryType::Timestamp;
    status.count = static_cast<std::uint32_t>(slots_.size());
    const auto* diagnostic = runtime_ ? runtime_->timestampQueryDiagnostic()
                                      : "RHI-VULKAN-TIMESTAMP-RUNTIME-UNAVAILABLE";
    status.creationSupported = !diagnostic && queryPool_ != VK_NULL_HANDLE;
    status.resetSupported = status.creationSupported;
    status.asynchronousReadbackSupported = status.creationSupported;
    status.completionGatedLifetimeRequired = status.creationSupported;
    if (!status.creationSupported) {
        status.state = rhi::QueryExecutionState::Unsupported;
        status.diagnostics =
            diagnostic ? diagnostic : "RHI-VULKAN-QUERY-POOL-NATIVE-PAYLOAD-MISSING";
        return status;
    }
    std::lock_guard lock(mutex_);
    bool hasDiagnosticFailure = false;
    bool hasPending = false;
    bool hasRecorded = false;
    bool hasAvailable = false;
    for (const auto& slot : slots_) {
        const auto state = slotState(*slot);
        hasDiagnosticFailure =
            hasDiagnosticFailure || state == rhi::QueryExecutionState::DiagnosticFailure;
        hasPending = hasPending || state == rhi::QueryExecutionState::Pending;
        hasRecorded = hasRecorded || state == rhi::QueryExecutionState::Recorded;
        hasAvailable = hasAvailable || state == rhi::QueryExecutionState::Available;
    }
    if (hasDiagnosticFailure) {
        status.state = rhi::QueryExecutionState::DiagnosticFailure;
        status.diagnostics = "RHI-VULKAN-QUERY-POOL-READBACK-FAILED";
    } else if (hasPending) {
        status.state = rhi::QueryExecutionState::Pending;
        status.diagnostics = "RHI-VULKAN-QUERY-POOL-WAITING-FOR-COMPLETION";
    } else if (hasRecorded) {
        status.state = rhi::QueryExecutionState::Recorded;
        status.diagnostics = "RHI-VULKAN-QUERY-POOL-RECORDED-BEFORE-SUBMISSION";
    } else if (hasAvailable) {
        status.state = rhi::QueryExecutionState::Available;
        status.diagnostics = "RHI-VULKAN-QUERY-POOL-READBACK-AVAILABLE";
    } else {
        status.state = rhi::QueryExecutionState::NotRecorded;
        status.diagnostics = "RHI-VULKAN-QUERY-POOL-NOT-RECORDED";
    }
    return status;
}

VulkanRhiQueryPool::VulkanRhiQueryPool(
    std::shared_ptr<VulkanRhiQueryPoolPayload> payload, rhi::QueryPoolDesc desc)
    : payload_(std::move(payload)), desc_(std::move(desc))
{
    // Normalize the copied descriptor to the pool's owned scope metadata;
    // the caller's optional source array must not survive creation.
    const auto* scopes = payload_ ? &payload_->scopeIds() : nullptr;
    desc_.timingScopeIds = scopes && !scopes->empty() ? scopes->data() : nullptr;
    desc_.timingScopeIdCount = scopes ? static_cast<std::uint32_t>(scopes->size()) : 0u;
}

std::uint32_t VulkanRhiQueryPool::count() const
{
    return desc_.count;
}

rhi::QueryType VulkanRhiQueryPool::type() const
{
    return desc_.type;
}

VkQueryPool VulkanRhiQueryPool::nativeHandle() const
{
    return payload_ ? payload_->nativeHandle() : VK_NULL_HANDLE;
}

const std::shared_ptr<VulkanRhiQueryPoolPayload>& VulkanRhiQueryPool::payload() const
{
    return payload_;
}

const char* VulkanRhiQueryPool::validateOriginalBudget(
    const VulkanOriginalQueryPoolBudget& b) noexcept
{
    return b.diagnostic();
}

std::shared_ptr<VulkanRhiQueryPool> VulkanRhiQueryPool::prepareOriginal(
    const std::shared_ptr<VulkanRhiRuntime>& runtime,
    std::shared_ptr<const rhi::QueryPoolInputSnapshot> input,
    const VulkanOriginalQueryPoolBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const char*& diagnostic)
{
    diagnostic = validateOriginalBudget(budget);
    if (diagnostic)
        return {};
    if (!quota || quota->limitBytes() != budget.maxCpuOwnerBytes) {
        diagnostic = "RHI-VULKAN-ORIGINAL-QUERY-QUOTA";
        return {};
    }
    const auto decision =
        rhi::semantics::OriginalQueryCreationLowering::decide(rhi::Backend::Vulkan,
            budget.contractVersion,
            budget.prototypeOptIn,
            input,
            runtime ? runtime->originalQueryCreationFacts()
                    : rhi::semantics::OriginalQueryCreationFacts{});
    diagnostic = decision.diagnostic();
    if (!decision.creationEligible())
        return {};
    if (core::SharedOwnerProjection::sameControlBlock(quota, input->originalMetadataQuota())) {
        diagnostic = "RHI-VULKAN-ORIGINAL-QUERY-QUOTA-DOMAIN";
        return {};
    }
    if (input->scalarFields().count() > budget.maxQuerySlots) {
        diagnostic = "RHI-VULKAN-ORIGINAL-QUERY-SLOT-CAPACITY";
        return {};
    }
    if (input->scopeIds().size() > budget.maxScopeIds) {
        diagnostic = "RHI-VULKAN-ORIGINAL-QUERY-SCOPE-CAPACITY";
        return {};
    }
    if (input->label().size() > budget.maxLabelBytes) {
        diagnostic = "RHI-VULKAN-ORIGINAL-QUERY-LABEL-CAPACITY";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        rhi::QueryPoolDesc desc;
        desc.label = input->label();
        desc.type = input->scalarFields().type();
        desc.count = input->scalarFields().count();
        desc.pipelineStatisticsMask = input->scalarFields().statisticMask();
        desc.timingScopeIdCount = static_cast<std::uint32_t>(input->scopeIds().size());
        desc.timingScopeIds = desc.timingScopeIdCount ? input->scopeIds().data() : nullptr;
        core::pmr::Vector<std::uint64_t> scopes(input->scopeIds().begin(), input->scopeIds().end());
        auto payload = core::makeShared<VulkanRhiQueryPoolPayload>(
            runtime, VK_NULL_HANDLE, std::move(scopes), desc.count);
        payload->originalInput_ = input;
        auto wrapper = core::makeUnique<VulkanRhiQueryPool>(payload, std::move(desc));
        wrapper->originalCreation_.emplace(OriginalCreation{decision, budget});
        // Authenticate the original shared control block before native creation.
        // A short CPU quota must not create even an unpublished native pool.
        auto created = shareOriginal(std::move(wrapper));
        if ((diagnostic = payload->createNativePool(created->count())))
            return {};
        const auto reset = created->reset();
        if (!reset.success) {
            // This gate provides a static diagnostic; never retain a pointer
            // into the temporary CommandValidationResult's owned string.
            diagnostic = runtime->timestampQueryDiagnostic();
            if (!diagnostic)
                diagnostic = "RHI-VULKAN-ORIGINAL-QUERY-INITIAL-RESET";
            return {};
        }
        diagnostic = nullptr;
        return created;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-VULKAN-ORIGINAL-QUERY-CPU-ALLOCATION";
    } catch (const std::length_error&) {
        diagnostic = "RHI-VULKAN-ORIGINAL-QUERY-CPU-LENGTH";
    }
    return {};
}

std::shared_ptr<const rhi::QueryPoolInputSnapshot>
VulkanRhiQueryPool::originalInput() const noexcept
{
    return originalCreation_ ? originalCreation_->decision.originalInput() : nullptr;
}

const char* VulkanRhiQueryPool::validateOriginal(
    const std::shared_ptr<const VulkanRhiQueryPool>& self, const VulkanRhiRuntime& runtime) noexcept
{
    if (!self || !self->hasQuotaBoundSharedOwner(self) || !self->originalCreation_)
        return "RHI-VULKAN-ORIGINAL-QUERY-OWNER";
    if (!self->payload_ || !self->payload_->belongsTo(runtime))
        return "RHI-VULKAN-ORIGINAL-QUERY-RUNTIME";
    if (const auto* id = runtime.timestampQueryDiagnostic())
        return id;
    const auto& original = *self->originalCreation_;
    if (const auto* id = validateOriginalBudget(original.budget))
        return id;
    if (!self->originalMetadataQuota()
        || self->originalMetadataQuota()->limitBytes() != original.budget.maxCpuOwnerBytes)
        return "RHI-VULKAN-ORIGINAL-QUERY-QUOTA";
    const auto& input = original.decision.originalInput();
    if (!original.decision.creationEligible() || !original.decision.matchesOriginal(input)
        || !original.decision.matchesOriginal(self->payload_->originalInput_)
        || !input->matches(self->desc_) || !self->nativeHandle())
        return "RHI-VULKAN-ORIGINAL-QUERY-INPUT";
    return nullptr;
}

const char* VulkanRhiQueryPool::validateOriginalInput(
    const rhi::QueryPoolOriginalInputArguments& arguments) const noexcept
{
    if (!originalCreation_)
        return "RHI-VULKAN-ORIGINAL-QUERY-OWNER";
    if (const auto* id = arguments.validateDeclaration())
        return id;
    return originalCreation_->decision.matchesOriginal(arguments.originalInput())
        ? nullptr
        : "RHI-VULKAN-ORIGINAL-QUERY-INPUT";
}

arp::core::UniquePtr<rhi::QueryPool> createVulkanRhiQueryPool(
    const std::shared_ptr<VulkanRhiRuntime>& runtime, const rhi::QueryPoolDesc& desc)
{
    const auto refuse = [](const char* diagnostic) -> core::UniquePtr<rhi::QueryPool> {
        rhi::RhiErrorHandler::get().report(rhi::RhiSeverity::Error, diagnostic);
        return {};
    };
    if (!runtime)
        return refuse("RHI-VULKAN-TIMESTAMP-RUNTIME-UNAVAILABLE");
    if (const auto* diagnostic = runtime->timestampQueryDiagnostic())
        return refuse(diagnostic);
    if (!rhi::validateQueryPoolDesc(desc).success || desc.type != rhi::QueryType::Timestamp)
        return refuse("RHI-VULKAN-TIMESTAMP-DESCRIPTOR-INVALID");
    try {
        // All fallible CPU storage precedes native creation. The unpublished
        // payload owns any successful native creation immediately.
        core::pmr::Vector<std::uint64_t> scopeIds;
        if (desc.timingScopeIds && desc.timingScopeIdCount)
            scopeIds.assign(desc.timingScopeIds, desc.timingScopeIds + desc.timingScopeIdCount);
        auto payload = core::makeShared<VulkanRhiQueryPoolPayload>(
            runtime, VK_NULL_HANDLE, std::move(scopeIds), desc.count);
        auto created = core::makeUnique<VulkanRhiQueryPool>(payload, desc);
        if (const auto* diagnostic = payload->createNativePool(desc.count))
            return refuse(diagnostic);
        // Host reset permits later timestamp writes inside render passes.
        const auto reset = created->reset();
        if (!reset.success)
            return refuse(reset.diagnostics.c_str());
        return created;
    } catch (const std::bad_alloc&) {
        return refuse("RHI-VULKAN-TIMESTAMP-CPU-ALLOCATION-FAILED");
    } catch (const std::length_error&) {
        return refuse("RHI-VULKAN-TIMESTAMP-CPU-LENGTH-INVALID");
    }
}

const VulkanRhiQueryPool* asVulkanRhiQueryPool(const rhi::QueryPool& pool)
{
    return dynamic_cast<const VulkanRhiQueryPool*>(&pool);
}

rhi::CommandValidationResult VulkanRhiQueryPool::reset()
{
    return payload_ ? payload_->reset()
                    : rhi::CommandValidationResult{
                          false,
                          "RHI-VULKAN-QUERY-POOL-PAYLOAD-MISSING",
                          0,
                      };
}

rhi::QueryExecutionStatus VulkanRhiQueryPool::executionStatus() const
{
    if (payload_) {
        return payload_->executionStatus();
    }
    rhi::QueryExecutionStatus status;
    status.type = desc_.type;
    status.state = rhi::QueryExecutionState::DiagnosticFailure;
    status.count = desc_.count;
    status.diagnostics = "RHI-VULKAN-QUERY-POOL-PAYLOAD-MISSING";
    return status;
}

} // namespace arp::rhi_vulkan
