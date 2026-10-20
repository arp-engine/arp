#pragma once

#include "cpu_quota_factory_fixture.h"
#include "original_command_ordered_upload_contract_fixture.h"
#include "original_command_upload_staging_contract_fixture.h"
#include "rhi/frontend/command_recording.h"
#include "rhi/semantics/command_lowering.h"

#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalCommandStreamProgramFixture final {
    using Program = rhi::OriginalCommandStreamProgram;
    using Budget = rhi::OriginalCommandStreamBudget;
    static constexpr std::uint64_t bytes = 65536u;

    static void is(const char* actual, const char* expected)
    {
        assert(actual && std::string_view(actual) == expected);
    }

    static Budget exactBudget(const Program::Counts& c, std::uint32_t owners)
    {
        return {1u,
            true,
            c.commands,
            c.descriptors,
            c.references,
            c.copyElements,
            c.bufferUploads,
            c.textureUploadMips,
            c.textureReadbackMips,
            c.renderPasses,
            c.attachmentReferences,
            c.bufferUploadDeclaredBytes,
            c.textureUploadPixelBytes,
            c.readbackDeclaredBytes,
            owners,
            bytes};
    }

public:
    static void verifyContract()
    {
        auto b = exactBudget({1u}, 1u);
        assert(!rhi::OriginalCommandStreamContract::validateIntent(b));
        auto invalid = b;
        invalid.contractVersion = 0u;
        is(rhi::OriginalCommandStreamContract::validateIntent(invalid),
            "RHI-ORIGINAL-STREAM-VERSION");
        invalid = b;
        invalid.prototypeOptIn = false;
        is(rhi::OriginalCommandStreamContract::validateIntent(invalid),
            "RHI-ORIGINAL-STREAM-OPT-IN");
        invalid = b;
        invalid.maxCommands = 0u;
        is(rhi::OriginalCommandStreamContract::validateIntent(invalid),
            "RHI-ORIGINAL-STREAM-BUDGET");
        invalid = b;
        invalid.maxRetainedOwners = 0u;
        assert(rhi::OriginalCommandStreamContract::validateIntent(invalid));
        invalid = b;
        invalid.maxCpuMetadataBytes = 0u;
        assert(rhi::OriginalCommandStreamContract::validateIntent(invalid));
        invalid = b;
        invalid.maxPipelineBinds = 1u;
        is(rhi::OriginalCommandStreamContract::validateIntent(invalid),
            "RHI-ORIGINAL-STREAM-LEGACY-SCHEMA");
        invalid = b;
        invalid.contractVersion = 2u;
        invalid.maxPipelineBinds = 3u;
        invalid.maxDescriptorBinds = 3u;
        invalid.maxConsumptions = 3u;
        invalid.maxImplicitAccesses = 6u;
        invalid.descriptorConsumption = {1u, true, 1u, 1u, true, true, 3u, 6u};
        invalid.stateSequence = {2u, true, 20u, 0u, 4u, 14u, 6u};
        assert(!rhi::OriginalCommandStreamContract::validateIntent(invalid));
        is(Program::inspect({}, invalid).diagnostic, "RHI-ORIGINAL-STREAM-TYPED-STATE-REQUIRED");
        assert(Program::inspectTyped({}, invalid).diagnostic);
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto typed =
                arp::rhi::semantics::OriginalCommandStreamLowering::typedLowering(backend);
            assert(
                typed.contractVersion == 2u && !typed.nativeExecutionAvailable && typed.diagnostic);
        }
        auto texture = invalid;
        texture.contractVersion = 3u;
        texture.stateSequence.contractVersion = 3u;
        texture.descriptorConsumption = {2u, true, 0u, 0u, false, false, 1u, 3u, 1u, true, 3u};
        texture.renderDescriptorUsage = {2u, true, 0u, 0u, false, false, 11u, 2u, 6u, 1u, true, 6u};
        assert(!rhi::OriginalCommandStreamContract::validateIntent(texture));
        auto badTexture = texture;
        badTexture.contractVersion = 2u;
        is(rhi::OriginalCommandStreamContract::validateIntent(badTexture),
            "RHI-ORIGINAL-STREAM-RENDER-LEGACY-SCHEMA");
        badTexture = texture;
        badTexture.stateSequence.contractVersion = 2u;
        is(rhi::OriginalCommandStreamContract::validateIntent(badTexture),
            "RHI-ORIGINAL-STREAM-SEQUENCE-PROFILE");
        badTexture = texture;
        badTexture.renderDescriptorUsage.contractVersion = 1u;
        is(rhi::OriginalCommandStreamContract::validateIntent(badTexture),
            "RHI-ORIGINAL-STREAM-RENDER-PROFILE");
        badTexture = texture;
        badTexture.descriptorConsumption.contractVersion = 1u;
        is(rhi::OriginalCommandStreamContract::validateIntent(badTexture),
            "RHI-ORIGINAL-STREAM-CONSUMPTION-PROFILE");
        badTexture = texture;
        badTexture.contractVersion = 6u;
        is(rhi::OriginalCommandStreamContract::validateIntent(badTexture),
            "RHI-ORIGINAL-STREAM-VERSION");
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto typed =
                arp::rhi::semantics::OriginalCommandStreamLowering::typedLowering(backend, 3u);
            assert(
                typed.contractVersion == 3u && !typed.nativeExecutionAvailable && typed.diagnostic);
        }
        const std::array<const char*, 4> ids{"RHI-ORIGINAL-STREAM-NULL-VALIDATOR-ONLY",
            "RHI-ORIGINAL-STREAM-VULKAN-EXECUTOR-MISSING",
            "RHI-ORIGINAL-STREAM-METAL-EXECUTOR-MISSING",
            "RHI-ORIGINAL-STREAM-WEBGPU-EXECUTOR-MISSING"};
        std::size_t i = 0u;
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto result =
                arp::rhi::semantics::OriginalCommandStreamLowering::lowering(backend);
            assert(result.contractVersion == 1u && !result.nativeExecutionAvailable);
            assert(result.disposition
                == (i ? arp::rhi::semantics::OriginalCommandStreamLowering::Disposition::
                            DiagnosticFailure
                      : arp::rhi::semantics::OriginalCommandStreamLowering::Disposition::
                            ValidatorOnly));
            is(result.diagnostic, ids[i++]);
        }
        assert(!arp::rhi::semantics::OriginalCommandStreamLowering::lowering(
            static_cast<rhi::Backend>(255u))
                .nativeExecutionAvailable);
        std::puts(
            "BINDLESS_RHI_ORIGINAL_STREAM_CONTRACT_OK version1 explicit-opt-in independent-counts declared-bytes-only null-validator native-executors3-missing");
    }

    // Hook into existing original factories; never recreate their source graph.
    // Caller keeps this program through its existing caller-drop assertions.
    static std::shared_ptr<const Program> verifyInputs(
        std::shared_ptr<const rhi::OriginalCommandInputs> inputs,
        const Program::Counts& expected,
        const char* profile,
        const rhi::OriginalCommandUploadStagingContract::Requirements& staging = {})
    {
        auto quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        const auto source = inputs->originalScalars()->originalUsage()->originalSources();
        const auto b = exactBudget(expected, inputs->retainedOwnerCount() + 1u);
        const char* diagnostic = nullptr;
        if (staging.totalBytes)
            OriginalCommandUploadStagingContractFixture::verify(inputs, b, staging, profile);
        if (staging.totalBytes)
            OriginalCommandOrderedUploadContractFixture::verifyInputs(inputs, b, staging);
        auto inspected = Program::inspect(inputs, b);
        assert(!inspected.diagnostic && inspected.commandIndex == expected.commands
            && inspected.counts.matches(expected));
        auto make = [&] {
            return Program::prepareOriginal(inputs, b, quota, diagnostic);
        };
        CpuQuotaFactoryFixture pressure(quota);
        const auto peak = pressure.measure(make);
        pressure.verify(peak, make);
        assert(!quota->liveBytes());
        {
            core::CpuMemoryQuotaLease full(quota, bytes);
            assert(full.charged());
            assert(!make());
            is(diagnostic, "RHI-ORIGINAL-STREAM-ALLOCATION");
        }
        auto program = make();
        assert(program && !diagnostic && !program->validate());
        assert(program->counts().matches(expected)
            && program->retainedOwnerCount() == b.maxRetainedOwners);
        assert(core::SharedOwnerProjection::sameOwner(program->originalInputs(), inputs));
        auto forged = std::shared_ptr<const Program>(quota, program.get());
        is(Program::validateOriginal(forged), "RHI-ORIGINAL-STREAM-OWNER");
        forged.reset();
        auto forgedInputs = std::shared_ptr<const rhi::OriginalCommandInputs>(quota, inputs.get());
        assert(!Program::prepareOriginal(forgedInputs, b, quota, diagnostic));
        is(diagnostic, "RHI-ORIGINAL-INPUTS-OWNER");
        is(program->compareOriginalInputs(forgedInputs), "RHI-ORIGINAL-STREAM-INPUTS");
        forgedInputs.reset();
        is(program->compareOriginalInputs({}), "RHI-ORIGINAL-STREAM-INPUTS");
        for (const auto& alias :
            std::array{inputs->originalMetadataQuota(), source->originalMetadataQuota()}) {
            assert(!Program::prepareOriginal(inputs, b, alias, diagnostic));
            is(diagnostic, "RHI-ORIGINAL-STREAM-QUOTA-DOMAIN");
            auto forgedObject = std::shared_ptr<core::CpuMemoryQuota>(quota, alias.get());
            assert(!Program::prepareOriginal(inputs, b, forgedObject, diagnostic));
            is(diagnostic, "RHI-ORIGINAL-STREAM-QUOTA-DOMAIN");
            auto forgedControl = std::shared_ptr<core::CpuMemoryQuota>(alias, quota.get());
            assert(!Program::prepareOriginal(inputs, b, forgedControl, diagnostic));
            is(diagnostic, "RHI-ORIGINAL-STREAM-QUOTA-DOMAIN");
        }
        auto nonOwning = std::shared_ptr<core::CpuMemoryQuota>(
            std::shared_ptr<core::CpuMemoryQuota>{}, quota.get());
        assert(!Program::prepareOriginal(inputs, b, nonOwning, diagnostic));
        is(diagnostic, "RHI-ORIGINAL-STREAM-QUOTA-DOMAIN");
        assert(!Program::prepareOriginal(inputs, b, {}, diagnostic));
        is(diagnostic, "RHI-ORIGINAL-STREAM-QUOTA");
        auto bad = b;
        --bad.maxRetainedOwners;
        assert(!Program::prepareOriginal(inputs, bad, quota, diagnostic));
        is(diagnostic, "RHI-ORIGINAL-STREAM-OWNER-CAPACITY");
        bad = b;
        --bad.maxCpuMetadataBytes;
        assert(!Program::prepareOriginal(inputs, bad, quota, diagnostic));
        is(diagnostic, "RHI-ORIGINAL-STREAM-QUOTA");
        bad = b;
        ++bad.maxCpuMetadataBytes;
        auto larger = core::makeShared<core::CpuMemoryQuota>(bad.maxCpuMetadataBytes);
        assert(!Program::prepareOriginal(inputs, bad, larger, diagnostic));
        is(diagnostic, "RHI-ORIGINAL-STREAM-SOURCE-CPU-BUDGET");
        assert(!larger->liveBytes());

        struct Limit {
            std::uint64_t Budget::* member;
            const char* diagnostic;
        };

        const std::array<Limit, 12> limits{
            {{&Budget::maxCommands, "RHI-ORIGINAL-STREAM-COMMAND-CAPACITY"},
                {&Budget::maxDescriptors, "RHI-ORIGINAL-STREAM-DESCRIPTOR-CAPACITY"},
                {&Budget::maxReferences, "RHI-ORIGINAL-STREAM-REFERENCE-CAPACITY"},
                {&Budget::maxCopyElements, "RHI-ORIGINAL-STREAM-COPY-CAPACITY"},
                {&Budget::maxBufferUploads, "RHI-ORIGINAL-STREAM-BUFFER-UPLOAD-CAPACITY"},
                {&Budget::maxTextureUploadMips, "RHI-ORIGINAL-STREAM-UPLOAD-MIP-CAPACITY"},
                {&Budget::maxTextureReadbackMips, "RHI-ORIGINAL-STREAM-READBACK-MIP-CAPACITY"},
                {&Budget::maxRenderPasses, "RHI-ORIGINAL-STREAM-PASS-CAPACITY"},
                {&Budget::maxAttachmentReferences, "RHI-ORIGINAL-STREAM-ATTACHMENT-CAPACITY"},
                {&Budget::maxBufferUploadDeclaredBytes, "RHI-ORIGINAL-STREAM-BUFFER-UPLOAD-BYTES"},
                {&Budget::maxTextureUploadPixelBytes, "RHI-ORIGINAL-STREAM-UPLOAD-PIXEL-BYTES"},
                {&Budget::maxReadbackDeclaredBytes, "RHI-ORIGINAL-STREAM-READBACK-BYTES"}}};
        for (const auto& limit : limits)
            if (b.*limit.member) {
                bad = b;
                --(bad.*limit.member);
                const auto live = quota->liveBytes(),
                           allocations = core::defaultCpuAllocator().stats().allocationCount;
                assert(!Program::prepareOriginal(inputs, bad, quota, diagnostic));
                is(diagnostic, limit.diagnostic);
                assert(quota->liveBytes() == live
                    && core::defaultCpuAllocator().stats().allocationCount == allocations);
                std::size_t consumed = 0u;
                is(Program::visitOriginal(inputs,
                       bad,
                       [&](const Program::Operation&) noexcept -> const char* {
                           ++consumed;
                           return nullptr;
                       }),
                    limit.diagnostic);
                assert(!consumed);
            }
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{source->originalMetadataQuota(), inputs->originalMetadataQuota(), quota},
            [&]() noexcept -> const char* {
                assert(
                    !Program::validateOriginal(program) && !program->compareOriginalInputs(inputs));
                const auto inspected = Program::inspect(inputs, b);
                assert(!inspected.diagnostic && inspected.counts.matches(expected));
                std::size_t visited = 0u;
                assert(!Program::visitOriginal(inputs,
                           b,
                           [&](const Program::Operation& operation) noexcept -> const char* {
                               assert(operation.matches(program->operations()[visited++]));
                               return nullptr;
                           })
                    && visited == expected.commands);
                visited = 0u;
                is(Program::visitOriginal(inputs,
                       b,
                       [&](const Program::Operation&) noexcept -> const char* {
                           ++visited;
                           return "TEST-PROJECTION-REFUSED";
                       }),
                    "TEST-PROJECTION-REFUSED");
                assert(visited == 1u);
                for (const auto backend : {rhi::Backend::Null,
                         rhi::Backend::Vulkan,
                         rhi::Backend::Metal,
                         rhi::Backend::WebGPU})
                    assert(
                        program->executionDiagnostic(backend) && !program->nativeExecutionProven());
                return nullptr;
            }));
        for (const auto& operation : program->operations()) {
            assert(operation.commandIndex < expected.commands);
            if (operation.family == rhi::CommandInputContract::Family::Scalar)
                assert(
                    operation.descriptorIndex == Program::noDescriptor && !operation.elementCount);
        }
        std::printf(
            "BINDLESS_RHI_ORIGINAL_STREAM_PROGRAM_OK %s commands%llu copy%llu buffer-upload%llu upload-mips%llu readback-mips%llu passes%llu cold%llu preflight-before-allocation exact-less-exhausted independent-bounds forged-owner quotas3 hot-allocation0 gates4-closed\n",
            profile,
            static_cast<unsigned long long>(expected.commands),
            static_cast<unsigned long long>(expected.copyElements),
            static_cast<unsigned long long>(expected.bufferUploads),
            static_cast<unsigned long long>(expected.textureUploadMips),
            static_cast<unsigned long long>(expected.textureReadbackMips),
            static_cast<unsigned long long>(expected.renderPasses),
            static_cast<unsigned long long>(peak));
        return program;
    }
};
} // namespace arp::test
