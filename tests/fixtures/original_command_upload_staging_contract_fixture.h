#pragma once

#include "rhi/frontend/command_transfer_validation.h"
#include "rhi/semantics/command_lowering.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalCommandUploadStagingContractFixture final {
    using Contract = rhi::OriginalCommandUploadStagingContract;

    static void is(const char* actual, const char* expected)
    {
        assert(actual && std::string_view(actual) == expected);
    }

public:
    static void verifyJournalEntries(
        const std::shared_ptr<const rhi::OriginalCommandInputs>& inputs,
        const rhi::OriginalCommandStreamBudget& stream)
    {
        std::uint64_t expected = 0u;
        for (const auto& command : inputs->originalScalars()->originalUsage()->commands())
            expected += command.kind == rhi::CommandUsageKind::BufferUpload
                || command.kind == rhi::CommandUsageKind::TextureUpload;
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        std::uint64_t entries = UINT64_MAX;
        assert(!Contract::inspectJournalEntries(inputs, stream, expected, entries)
            && entries == expected);
        if (expected) {
            is(Contract::inspectJournalEntries(inputs, stream, expected - 1u, entries),
                "RHI-ORIGINAL-UPLOAD-STAGING-JOURNAL-CAPACITY");
            assert(!entries);
            is(Contract::inspectJournalEntries(inputs, stream, 0u, entries),
                "RHI-ORIGINAL-UPLOAD-STAGING-JOURNAL-CAPACITY");
            assert(!entries);
        }
        auto bad = stream;
        bad.prototypeOptIn = false;
        assert(Contract::inspectJournalEntries(inputs, bad, UINT64_MAX, entries) && !entries);
        bad = stream;
        --bad.maxCommands;
        assert(Contract::inspectJournalEntries(inputs, bad, UINT64_MAX, entries) && !entries);
        assert(Contract::inspectJournalEntries({}, stream, UINT64_MAX, entries) && !entries);
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    }

    static void verify(const std::shared_ptr<const rhi::OriginalCommandInputs>& inputs,
        const rhi::OriginalCommandStreamBudget& stream,
        const Contract::Requirements& expected,
        const char* profile)
    {
        const Contract::Budget budget{
            1u, true, expected.bufferBytes, expected.packedTextureBytes, expected.totalBytes};
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        auto result = Contract::inspect(inputs, stream, budget);
        assert(!result.diagnostic && result.required.matches(expected));
        auto bad = budget;
        bad.contractVersion = 0u;
        is(Contract::inspect(inputs, stream, bad).diagnostic,
            "RHI-ORIGINAL-UPLOAD-STAGING-VERSION");
        bad = budget;
        bad.prototypeOptIn = false;
        is(Contract::inspect(inputs, stream, bad).diagnostic, "RHI-ORIGINAL-UPLOAD-STAGING-OPT-IN");
        bad = budget;
        bad.maxTotalBytes = 0u;
        is(Contract::inspect(inputs, stream, bad).diagnostic, "RHI-ORIGINAL-UPLOAD-STAGING-BUDGET");
        const auto refuse = [&](const Contract::Budget& bound, const char* id) {
            const auto refused = Contract::inspect(inputs, stream, bound);
            is(refused.diagnostic, id);
            assert(refused.required.matches({})); // no partial staging publication.
        };
        if (expected.bufferBytes) {
            bad = budget;
            --bad.maxBufferBytes;
            refuse(bad, "RHI-ORIGINAL-UPLOAD-STAGING-BUFFER-CAPACITY");
        }
        if (expected.packedTextureBytes) {
            bad = budget;
            --bad.maxPackedTextureBytes;
            refuse(bad, "RHI-ORIGINAL-UPLOAD-STAGING-TEXTURE-CAPACITY");
        }
        bad = budget;
        --bad.maxTotalBytes;
        refuse(bad, "RHI-ORIGINAL-UPLOAD-STAGING-TOTAL-CAPACITY");
        auto invalidStream = stream;
        --invalidStream.maxCommands;
        result = Contract::inspect(inputs, invalidStream, budget);
        assert(result.diagnostic && result.required.matches({}));
        assert(Contract::inspect({}, stream, budget).diagnostic);
        const std::array<const char*, 4> ids{"RHI-ORIGINAL-UPLOAD-STAGING-NULL-VALIDATOR-ONLY",
            "RHI-ORIGINAL-UPLOAD-STAGING-VULKAN-JOIN-MISSING",
            "RHI-ORIGINAL-UPLOAD-STAGING-METAL-JOIN-MISSING",
            "RHI-ORIGINAL-UPLOAD-STAGING-WEBGPU-JOIN-MISSING"};
        std::size_t i = 0u;
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision =
                arp::rhi::semantics::OriginalCommandUploadStagingLowering::lowering(backend);
            is(decision.diagnostic, ids[i]);
            assert(!decision.initializationAvailable && !decision.recordingAvailable);
            assert(decision.disposition
                == (i ? arp::rhi::semantics::OriginalCommandUploadStagingLowering::Disposition::
                            DiagnosticFailure
                      : arp::rhi::semantics::OriginalCommandUploadStagingLowering::Disposition::
                            ValidatorOnly));
            ++i;
        }
        assert(!arp::rhi::semantics::OriginalCommandUploadStagingLowering::lowering(
            static_cast<rhi::Backend>(255u))
                .initializationAvailable);
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::printf(
            "BINDLESS_RHI_UPLOAD_STAGING_REQUIREMENTS_OK %s buffers%llu mips%llu buffer-bytes%llu packed-texture-bytes%llu total%llu exact-one-less no-partial-publication allocation0 four-joins-closed\n",
            profile,
            static_cast<unsigned long long>(expected.bufferUploads),
            static_cast<unsigned long long>(expected.textureMips),
            static_cast<unsigned long long>(expected.bufferBytes),
            static_cast<unsigned long long>(expected.packedTextureBytes),
            static_cast<unsigned long long>(expected.totalBytes));
    }
};
} // namespace arp::test
