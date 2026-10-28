#include "core/memory/cpu_allocator.h"
#include "rhi/frontend/command_recording_budget.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>

#ifdef NDEBUG
#error Recording budget tests require assertions
#endif

using namespace arp;
using namespace rhi;

namespace {
void is(const char* actual, const char* expected)
{
    assert(actual && std::strcmp(actual, expected) == 0);
}

CommandRecordingBudget recipe()
{
    CommandRecordingBudget b{1u, true, 5u};
    b.limits.append(CommandUsageKind::BindComputePipeline);
    b.limits.append(CommandUsageKind::BindComputeDescriptor);
    b.limits.append(CommandUsageKind::PushConstants);
    b.limits.append(CommandUsageKind::Dispatch);
    b.limits.append(CommandUsageKind::Finish);
    b.limits.resourceReferences = 3u;
    b.limits.descriptorElements = 2u;
    b.limits.pushBytes = 16u;
    return b;
}

void transaction()
{
    auto b = recipe();
    const char* id = nullptr;
    auto meter = CommandRecordingMeter::prepare(b, id);
    assert(meter && !id);
    CommandRecordingUsage dispatch = b.limits;
    dispatch.commands[static_cast<std::size_t>(CommandUsageKind::Finish)] = 0u;
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    const auto empty = meter->used();
    assert(!meter->preflight(dispatch) && meter->used().matches(empty));
    assert(!meter->consume(dispatch) && meter->commandCount() == 4u);
    const auto once = meter->used();
    // Same resource identities could be deduplicated by a separate retainer.
    // The second command still consumes work and must reject atomically.
    assert(meter->consume(dispatch) && meter->used().matches(once));
    CommandRecordingUsage finish;
    finish.append(CommandUsageKind::Finish);
    assert(!meter->consume(finish) && meter->commandCount() == 5u);
    meter->seal();
    is(meter->consume({}), "RHI-COMMAND-RECORDING-BUDGET-CLOSED");
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);

    // A late category failure must not partially consume earlier opcodes.
    const auto rejected = [&](const CommandRecordingBudget& limit, const char* expected) {
        auto m = CommandRecordingMeter::prepare(limit, id);
        assert(m && !id);
        const auto before = m->used();
        is(m->consume(dispatch), expected);
        assert(m->used().matches(before) && m->commandCount() == 0u);
    };
    auto shortBudget = b;
    shortBudget.maxCommands = 3u;
    rejected(shortBudget, "RHI-COMMAND-RECORDING-BUDGET-COMMAND-CAPACITY");
    shortBudget = b;
    shortBudget.limits.commands[static_cast<std::size_t>(CommandUsageKind::Dispatch)] = 0u;
    rejected(shortBudget, "RHI-COMMAND-RECORDING-BUDGET-OPCODE-CAPACITY");
    shortBudget = b;
    --shortBudget.limits.resourceReferences;
    rejected(shortBudget, "RHI-COMMAND-RECORDING-BUDGET-REFERENCE-CAPACITY");
    shortBudget = b;
    --shortBudget.limits.descriptorElements;
    rejected(shortBudget, "RHI-COMMAND-RECORDING-BUDGET-DESCRIPTOR-CAPACITY");
    shortBudget = b;
    --shortBudget.limits.pushBytes;
    rejected(shortBudget, "RHI-COMMAND-RECORDING-BUDGET-PUSH-CAPACITY");
    for (const bool text : {false, true}) {
        auto input = dispatch;
        (text ? input.textBytes : input.dynamicOffsets) = 1u;
        auto m = CommandRecordingMeter::prepare(b, id);
        is(m->consume(input),
            text ? "RHI-COMMAND-RECORDING-BUDGET-TEXT-CAPACITY"
                 : "RHI-COMMAND-RECORDING-BUDGET-OFFSET-CAPACITY");
        assert(m->commandCount() == 0u);
        auto authored = b;
        (text ? authored.limits.textBytes : authored.limits.dynamicOffsets) = 1u;
        m = CommandRecordingMeter::prepare(authored, id);
        assert(!m->consume(input));
        (text ? input.textBytes : input.dynamicOffsets) = 2u;
        m = CommandRecordingMeter::prepare(authored, id);
        assert(m->consume(input) && m->commandCount() == 0u);
    }
}

void overflowAndGates()
{
    auto b = recipe();
    const char* id = nullptr;
    for (const auto version : {0u, 2u, UINT32_MAX}) {
        auto bad = b;
        bad.contractVersion = version;
        assert(!CommandRecordingMeter::prepare(bad, id));
        is(id, "RHI-COMMAND-RECORDING-BUDGET-VERSION");
    }
    auto bad = b;
    bad.prototypeOptIn = false;
    assert(!CommandRecordingMeter::prepare(bad, id));
    is(id, "RHI-COMMAND-RECORDING-BUDGET-OPT-IN");
    bad = b;
    bad.maxCommands = 0u;
    assert(!CommandRecordingMeter::prepare(bad, id));
    is(id, "RHI-COMMAND-RECORDING-BUDGET-COMMANDS");
    for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        const auto decision = CommandRecordingBudgetContract::lowering(backend, b);
        assert(decision.privateRecordingAvailable == (backend == Backend::Vulkan));
        assert(bool(decision.diagnostic) == (backend != Backend::Vulkan));
        assert(CommandRecordingBudgetContract::lowering(backend, bad).diagnostic);
        if (backend == Backend::Null)
            is(decision.diagnostic, "RHI-COMMAND-RECORDING-BUDGET-NULL-VALIDATOR");
        if (backend == Backend::Metal)
            is(decision.diagnostic, "RHI-COMMAND-RECORDING-BUDGET-METAL-PAYLOAD-MISSING");
        if (backend == Backend::WebGPU)
            is(decision.diagnostic, "RHI-COMMAND-RECORDING-BUDGET-WEBGPU-PAYLOAD-MISSING");
    }
    const auto webgl = CommandRecordingBudgetContract::lowering(Backend::WebGL, b);
    assert(!webgl.privateRecordingAvailable);
    is(webgl.diagnostic, "RHI-COMMAND-RECORDING-BUDGET-WEBGL-UNSUPPORTED");
    const auto unknown = CommandRecordingBudgetContract::lowering(static_cast<Backend>(255), b);
    assert(!unknown.privateRecordingAvailable);
    is(unknown.diagnostic, "RHI-COMMAND-RECORDING-BUDGET-BACKEND");
    CommandRecordingUsage input;
    assert(!input.append(CommandUsageKind::Count));
    assert(!input.append(static_cast<CommandUsageKind>(UINT32_MAX)));
    constexpr auto max = std::numeric_limits<std::uint64_t>::max();
    assert(input.append(CommandUsageKind::Dispatch, max));
    assert(!input.append(CommandUsageKind::Dispatch));
    b = {1u, true, max};
    b.limits = input;
    b.limits.resourceReferences = max;
    b.limits.append(CommandUsageKind::Finish);
    auto m = CommandRecordingMeter::prepare(b, id);
    input.resourceReferences = max;
    assert(!m->consume(input) && m->commandCount() == max);
    const auto full = m->used();
    CommandRecordingUsage finish;
    finish.append(CommandUsageKind::Finish);
    is(m->consume(finish), "RHI-COMMAND-RECORDING-BUDGET-COMMAND-CAPACITY");
    finish = {};
    finish.resourceReferences = 1u;
    is(m->consume(finish), "RHI-COMMAND-RECORDING-BUDGET-REFERENCE-CAPACITY");
    assert(m->used().matches(full));
}

void originalArgumentProjection()
{
    using K = CommandUsageKind;
    const auto marker =
        CommandScalarArguments::capture(K::InsertDebugMarker, CommandScalarArguments::None{});
    const auto binding = CommandScalarArguments::capture(
        K::BindComputeDescriptor, CommandScalarArguments::DescriptorBinding{0u});
    const auto push = CommandScalarArguments::capture(
        K::PushConstants, CommandScalarArguments::Push{ShaderStage::Compute, 0u, 16u});
    assert(marker && binding && push);
    const std::uint32_t offsets[]{64u, 256u};
    const std::array<unsigned char, 16u> bytes{};
    // UTF-8 payload is charged in bytes, not code points or null terminators.
    const CommandVariableArguments::Input inputs[]{{*marker, "\xE4\xBA\x91"},
        {*binding, {}, offsets, 2u},
        {*push, {}, nullptr, 0u, bytes.data(), 16u}};
    CommandRecordingUsage usage;
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    for (const auto& input : inputs)
        assert(!usage.appendArguments(input));
    assert(usage.textBytes == 3u && usage.dynamicOffsets == 2u && usage.pushBytes == 16u);
    assert(usage.resourceReferences == 0u && usage.descriptorElements == 0u);
    CommandRecordingBudget budget{1u, true, 3u, usage};
    const char* id = nullptr;
    auto meter = CommandRecordingMeter::prepare(budget, id);
    assert(meter && !id && !meter->consume(usage));
    const auto exact = meter->used();
    CommandRecordingUsage repeated;
    assert(!repeated.appendArguments(inputs[0]));
    assert(meter->consume(repeated) && meter->used().matches(exact));

    const auto rejected = [&](const CommandVariableArguments::Input& input, const char* expected) {
        const auto before = usage;
        is(usage.appendArguments(input), expected);
        assert(usage.matches(before));
    };
    auto bad = inputs[2];
    bad.byteCount = 15u;
    rejected(bad, "RHI-COMMAND-ARGUMENTS-PUSH-SIZE");
    bad = inputs[1];
    bad.offsets = nullptr;
    rejected(bad, "RHI-COMMAND-ARGUMENTS-POINTER");
    bad = inputs[0];
    bad.offsets = offsets;
    bad.offsetCount = 2u;
    rejected(bad, "RHI-COMMAND-ARGUMENTS-PAYLOAD");
    constexpr auto max = std::numeric_limits<std::uint64_t>::max();
    for (const auto field : {&CommandRecordingUsage::textBytes,
             &CommandRecordingUsage::dynamicOffsets,
             &CommandRecordingUsage::pushBytes}) {
        usage = {};
        usage.*field = max;
        const auto& input = field == &CommandRecordingUsage::textBytes ? inputs[0]
            : field == &CommandRecordingUsage::dynamicOffsets          ? inputs[1]
                                                                       : inputs[2];
        rejected(input, "RHI-COMMAND-RECORDING-BUDGET-ARGUMENT-OVERFLOW");
    }
    usage = {};
    usage.append(K::InsertDebugMarker, max);
    rejected(inputs[0], "RHI-COMMAND-RECORDING-BUDGET-OPCODE-OVERFLOW");
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
}
} // namespace

int main()
{
    transaction();
    overflowAndGates();
    originalArgumentProjection();
    std::cout << "COMMAND_RECORDING_BUDGET_OK exact-one-short all-categories overflow atomic "
                 "same-owner-repetition four-backends private-only allocation0\n";
}
