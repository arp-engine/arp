#include "rhi/frontend/command_transfer_validation.h"
#include "rhi/frontend/resource_state_sequence.h"
#include "rhi/semantics/command_lowering.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#ifdef NDEBUG
#error Output3 dependency contracts require assertions
#endif
using namespace arp::rhi;
using Contract = OriginalGeneralOutputReadbackDependencyContract;
using Lowering = semantics::OriginalGeneralOutputReadbackDependencyLowering;
constexpr std::array backends{Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU};

ResourceBarrierDesc barrier(bool graphics)
{
    ResourceBarrierDesc value;
    value.resource = {1u};
    value.before = graphics ? ResourceState::ColorAttachment : ResourceState::ShaderWrite;
    value.after = ResourceState::CopySrc;
    value.sourceStages =
        graphics ? PipelineStage::ColorAttachmentOutput : PipelineStage::ComputeShader;
    value.sourceAccess = graphics
        ? ResourceAccess::ColorAttachmentRead | ResourceAccess::ColorAttachmentWrite
        : ResourceAccess::ShaderWrite;
    value.destinationStages = PipelineStage::Transfer;
    value.destinationAccess = ResourceAccess::TransferRead;
    return value;
}

ResourceBarrierDesc mutation(ResourceBarrierDesc value, unsigned field)
{
    switch (field) {
    case 0:
        break;
    case 1:
        value.sourceStages = PipelineStage::ComputeShader;
        break;
    case 2:
        value.sourceAccess = ResourceAccess::ColorAttachmentWrite;
        break;
    case 3:
        value.destinationStages = PipelineStage::FragmentShader;
        break;
    case 4:
        value.destinationAccess = ResourceAccess::TransferWrite;
        break;
    case 5:
        value.before = ResourceState::ShaderWrite;
        break;
    case 6:
        value.after = ResourceState::ShaderRead;
        break;
    case 7:
        value.textureSubresources.baseMipLevel = 1u;
        break;
    case 8:
        value.resource = {};
        break;
    }
    return value;
}

void legacy()
{
    for (auto backend : backends)
        for (auto version : {1u, 2u})
            for (bool graphics : {false, true})
                for (unsigned field = 0; field < 9; ++field) {
                    const auto value = mutation(barrier(graphics), field);
                    const auto result =
                        Lowering::transition(backend, version, true, value, value.before, {});
                    std::printf("legacy %u %u %u %u %u %u %s\n",
                        unsigned(backend),
                        version,
                        unsigned(graphics),
                        field,
                        unsigned(result.outcome),
                        unsigned(result.operation),
                        result.diagnostic ? result.diagnostic : "success");
                }
}

void graphics()
{
    assert(Contract::contentReaderProfileMatches(3u, 3u));
    for (auto output : {0u, 1u, 2u, 4u, UINT32_MAX})
        assert(!Contract::contentReaderProfileMatches(output, 3u));
    for (auto content : {0u, 1u, 2u, 4u, UINT32_MAX})
        assert(!Contract::contentReaderProfileMatches(3u, content));
    assert(Contract::contentReaderProfileMatches(1u, 0u));
    assert(Contract::contentReaderProfileMatches(2u, 0u));
    using Closure = OriginalCommandNativeClosureContract;
    OriginalCommandNativeClosureBudget closure{1u, true, 2u, 2u, 48u, 512u, 65536u};
    assert(!Closure::validateAttachmentReadbackProfile(closure, 3u, true, 1u, 3u, 4u));
    for (unsigned field = 0u; field < 8u; ++field) {
        auto wrong = closure;
        auto output = 3u, state = 1u, content = 3u, join = 4u;
        bool optIn = true;
        if (field == 0u)
            wrong.contractVersion = 4u;
        if (field == 1u)
            wrong.prototypeOptIn = false;
        if (field == 2u)
            output = 2u;
        if (field == 3u)
            optIn = false;
        if (field == 4u)
            state = 3u;
        if (field == 5u)
            content = 0u;
        if (field == 6u)
            join = 2u;
        if (field == 7u)
            wrong.maxResourceBindings = 0u;
        assert(
            Closure::validateAttachmentReadbackProfile(wrong, output, optIn, state, content, join));
    }
    for (auto backend : backends) {
        const auto decision =
            semantics::OriginalCommandNativeClosureLowering::attachmentReadbackLossBoundLowering(
                backend);
        assert(decision.resourceClosureAvailable == (backend == Backend::WebGPU));
        assert(!decision.recordingIssuerAvailable && !decision.executionAvailable);
        assert(decision.diagnostic);
    }
    const auto color = barrier(true), storage = barrier(false);
    using Pair = OriginalGeneralOutputPairingContract;
    const OriginalGeneralOutputIntent selected{3u, true};
    assert(!Pair::validateOptional(selected));
    assert(!Pair::validateRecorded(selected, 3u, selected));
    for (const auto version : {0u, 1u, 2u, 4u, UINT32_MAX})
        assert(Pair::validatePair(selected, {version, true}));
    assert(Pair::validateRecorded(selected, UINT32_MAX, selected));
    assert(Pair::validateRecorded(selected, 2u, selected));
    assert(Pair::validateOptional({3u, false}));
    using Join = OriginalCommandReadbackJoinContract;
    assert(Join::generalOutputProfileVersionsMatch(3u, 1u, 1u, 3u, 2u, 1u));
    assert(!Join::generalOutputProfileVersionsMatch(3u, 1u, 1u, 0u, 2u, 1u));
    for (auto version : {0u, 1u, 2u, 4u, UINT32_MAX})
        assert(!Join::generalOutputProfileVersionsMatch(version, 1u, 1u, 3u, 2u, 1u));
    Join::Budget join;
    join.contractVersion = 4u;
    join.prototypeOptIn = true;
    join.generalOutput = selected;
    join.maxReadbacks = join.maxNativeSources = join.maxNativeDestinations = 1u;
    join.maxRetainedOwners = 48u;
    join.maxSourceResourceBytes = 8u;
    join.maxStagingResourceBytes = 256u;
    join.maxCpuMetadataBytes = 65536u;
    join.stream = {1u, true, 5u, 2u, 3u, 0u, 0u, 0u, 1u, 0u, 0u, 0u, 0u, 256u, 48u, 65536u};
    join.result = {1u, true, 1u, 1u, 1u, 256u, 256u, 65536u};
    assert(!Join::validateGeneralOutputIntent(join));
    using Cold = OriginalCommandNativeColdContract;
    Cold::Budget cold;
    cold.contractVersion = 5u;
    cold.prototypeOptIn = true;
    cold.generalOutput = selected;
    cold.maxOriginalCommands = 5u;
    cold.maxEncoders = cold.maxCommandBuffers = 1u;
    cold.maxRecordingScopeCallbacks = cold.maxSubmissionScopeCallbacks = 3u;
    cold.maxQueueCallbacks = cold.maxRuntimeLossCallbacks = cold.maxRuntimeLossRegistrations = 1u;
    cold.maxRetainedOwners = 320u;
    cold.maxCpuMetadataBytes = 65536u;
    cold.readback = join.result;
    cold.readbackMap = {1u, true, 1u, 1u, 3u, 12u, 12u};
    assert(!Cold::validateIntent(cold));
    for (auto version : {0u, 1u, 2u, 3u, 4u, 6u}) {
        auto invalid = cold;
        invalid.contractVersion = version;
        assert(Cold::validateIntent(invalid));
    }
    assert(Join::generalOutputProfileMatches(join, 1u, 1u, 3u, 2u, 1u));
    assert(!Join::generalOutputProfileMatches(join, 1u, 1u, 0u, 2u, 1u));
    for (unsigned field = 0; field < 5u; ++field) {
        std::array<std::uint32_t, 5u> versions{1u, 1u, 3u, 2u, 1u};
        ++versions[field];
        assert(!Join::generalOutputProfileMatches(
            join, versions[0], versions[1], versions[2], versions[3], versions[4]));
    }
    for (auto version : {1u, 2u}) {
        auto legacyJoin = join;
        legacyJoin.generalOutput.contractVersion = version;
        assert(Join::generalOutputProfileMatches(legacyJoin, 1u, 1u, 0u, 2u, 1u));
        assert(!Join::generalOutputProfileMatches(legacyJoin, 1u, 1u, 3u, 2u, 1u));
    }
    for (auto version : {0u, 4u, UINT32_MAX}) {
        auto wrong = join;
        wrong.generalOutput.contractVersion = version;
        assert(Join::validateGeneralOutputIntent(wrong));
    }
    auto wrong = join;
    wrong.generalOutput.prototypeOptIn = false;
    assert(Join::validateGeneralOutputIntent(wrong));
    assert(!Join::validateGeneralOutputDependency(color, color.before, {}, 3u));
    assert(Join::validateGeneralOutputDependency(storage, storage.before, {}, 3u));
    assert(Join::validateGeneralOutputDependency(color, color.before, {}));
    assert(Join::validateGeneralOutputSource({}, 3u));
    // Ordinary output profiles must keep refusing attachment dependencies.
    assert(Contract::validate(color, color.before, {}));
    assert(!Contract::validate(storage, storage.before, {}));
    assert(!Contract::validate(color, color.before, {}, 3u));
    assert(Contract::validate(storage, storage.before, {}, 3u));
    for (auto version : {0u, 4u, UINT32_MAX}) {
        assert(Contract::validate(color, color.before, {}, version));
        assert(Contract::validatePendingStates(
            color.before, color.before, color.after, color.after, 7u, 8u, version));
    }
    for (auto revision : {std::uint64_t(1u), std::uint64_t(7u), UINT64_MAX - 1u})
        assert(!Contract::validatePendingStates(
            color.before, color.before, color.after, color.after, revision, revision + 1u, 3u));
    for (unsigned field = 0; field < 8; ++field) {
        auto captured = color.before, initial = color.before, final = color.after,
             pending = color.after;
        std::uint64_t before = 7u, after = 8u;
        if (field == 0)
            captured = ResourceState::ShaderWrite;
        if (field == 1)
            initial = ResourceState::Undefined;
        if (field == 2)
            final = ResourceState::ShaderRead;
        if (field == 3)
            pending = ResourceState::ColorAttachment;
        if (field == 4)
            after = before;
        if (field == 5)
            after = before + 2u;
        if (field == 6)
            before = 0u;
        if (field == 7) {
            before = UINT64_MAX;
            after = 0u;
        }
        assert(
            Contract::validatePendingStates(captured, initial, final, pending, before, after, 3u));
    }
    for (auto backend : backends) {
        const auto result = Lowering::transition(backend, 3u, true, color, color.before, {});
        assert((result.outcome == CapabilityMode::Equivalent) == (backend == Backend::WebGPU));
        assert(bool(result.diagnostic) == (backend != Backend::WebGPU));
        assert(Lowering::transition(backend, 3u, false, color, color.before, {}).diagnostic);
        assert(Lowering::transition(backend, 3u, true, storage, storage.before, {}).diagnostic);
        for (unsigned field = 1; field < 9; ++field) {
            const auto wrong = mutation(color, field);
            assert(Lowering::transition(backend, 3u, true, wrong, color.before, {}).diagnostic);
        }
        auto range = TextureSubresourceRange{};
        range.baseArrayLayer = 1u;
        assert(Lowering::transition(backend, 3u, true, color, color.before, range).diagnostic);
        for (auto version : {0u, 4u, UINT32_MAX})
            assert(
                Lowering::transition(backend, version, true, color, color.before, {}).diagnostic);
        const auto execution =
            semantics::OriginalGeneralOutputExecutionLowering::lowering(backend, 3u);
        assert(execution.profilePairingAvailable == (backend == Backend::WebGPU));
        assert(execution.recordingAvailable == (backend == Backend::WebGPU));
        assert(execution.submissionAvailable == (backend == Backend::WebGPU));
        assert(!execution.nativeSyncAvailable);
        const auto cold =
            semantics::OriginalCommandNativeColdLowering::generalOutputLowering(backend, 3u);
        assert(cold.coldJoinAvailable == (backend == Backend::WebGPU));
        assert(!cold.recordingAvailable && !cold.executionAvailable);
    }
    std::puts(
        "PASS Output3 exact attachment dependency; approved private execution profile checked; public remains closed");
}

int main(int argc, char** argv)
{
    if (argc == 2 && !std::strcmp(argv[1], "--legacy-only")) {
        legacy();
        return 0;
    }
    graphics();
}
