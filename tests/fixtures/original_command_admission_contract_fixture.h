#pragma once

#include "core/memory/cpu_allocator.h"
#include "rhi/frontend/command_model.h"
#include "rhi/semantics/command_lowering.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace arp::test {
class OriginalCommandAdmissionContractFixture final {
public:
    static void verify()
    {
        using namespace rhi;
        using Contract = OriginalCommandAdmissionContract;
        using B = OriginalCommandAdmissionBudget;
        using C = B::CpuDomain;
        using D = CommandReferenceDomain;
        using K = CommandUsageKind;
        using F = CommandInputContract::Family;
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        B budget;
        budget.contractVersion = 1u;
        budget.prototypeOptIn = true;
        budget.maxCommands = 1u;
        budget.maxGraphOwners = 1u;
        budget.maxCpuBytes[static_cast<std::size_t>(C::Graph)] = 1u;
        assert(!Contract::validateIntent(budget)); // Empty Finish-only intent.
        const auto reject = [](const B& b, const char* id) {
            const auto* actual = Contract::validateIntent(b);
            assert(actual && !std::strcmp(actual, id));
        };
        auto invalid = budget;
        invalid.contractVersion = 0u;
        reject(invalid, "RHI-ORIGINAL-COMMAND-VERSION");
        invalid = budget;
        invalid.prototypeOptIn = false;
        reject(invalid, "RHI-ORIGINAL-COMMAND-OPT-IN");
        for (unsigned field = 0u; field < 3u; ++field) {
            invalid = budget;
            if (field == 0u)
                invalid.maxCommands = 0u;
            if (field == 1u)
                invalid.maxGraphOwners = 0u;
            if (field == 2u)
                invalid.maxCpuBytes[static_cast<std::size_t>(C::Graph)] = 0u;
            reject(invalid, "RHI-ORIGINAL-COMMAND-GRAPH-BUDGET");
        }
        for (std::size_t i = 0u; i < budget.maxSourceOwners.size(); ++i) {
            const auto domain = static_cast<D>(i);
            assert(Contract::ownership(domain) != Contract::Ownership::Unsupported);
            invalid = budget;
            invalid.maxSourceOwners[i] = UINT32_MAX;
            reject(invalid, "RHI-ORIGINAL-COMMAND-SOURCE-BUDGET");
            invalid.maxCpuBytes[static_cast<std::size_t>(C::Source)] = 1u;
            assert(!Contract::validateIntent(invalid)); // Counts are not guessed CPU byte costs.
        }
        assert(Contract::ownership(D::Count) == Contract::Ownership::Unsupported);
        assert(Contract::ownership(static_cast<D>(UINT32_MAX)) == Contract::Ownership::Unsupported);
        const auto independent = [&](C domain, auto set, const char* id) {
            auto b = budget;
            set(b);
            // Unrelated CPU domains cannot pay for this domain.
            b.maxCpuBytes.fill(UINT64_MAX);
            b.maxCpuBytes[static_cast<std::size_t>(domain)] = 0u;
            reject(b, id);
            b.maxCpuBytes[static_cast<std::size_t>(domain)] = 1u;
            assert(!Contract::validateIntent(b));
        };
        independent(
            C::Recording,
            [](B& b) {
                b.maxStateTransactions = 1u;
            },
            "RHI-ORIGINAL-COMMAND-RECORDING-BUDGET");
        independent(
            C::Recording,
            [](B& b) {
                b.maxNativePayloadObjects = 1u;
            },
            "RHI-ORIGINAL-COMMAND-RECORDING-BUDGET");
        independent(
            C::Submission,
            [](B& b) {
                b.maxPendingSubmissions = 1u;
            },
            "RHI-ORIGINAL-COMMAND-SUBMISSION-BUDGET");
        independent(
            C::Callback,
            [](B& b) {
                b.maxCallbackTickets = 1u;
            },
            "RHI-ORIGINAL-COMMAND-CALLBACK-BUDGET");
        independent(
            C::Callback,
            [](B& b) {
                b.maxCallbackRegistrations = 1u;
            },
            "RHI-ORIGINAL-COMMAND-CALLBACK-BUDGET");
        independent(
            C::Result,
            [](B& b) {
                b.maxUnpublishedResultBytes = UINT64_MAX;
            },
            "RHI-ORIGINAL-COMMAND-RESULT-BUDGET");
        const std::array<Backend, 4> backends{
            Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU};
        const std::array<const char*, 4> diagnostics{"RHI-ORIGINAL-COMMAND-NULL-VALIDATOR-ONLY",
            "RHI-ORIGINAL-COMMAND-VULKAN-UNIMPLEMENTED",
            "RHI-ORIGINAL-COMMAND-METAL-UNIMPLEMENTED",
            "RHI-ORIGINAL-COMMAND-WEBGPU-UNIMPLEMENTED"};
        for (std::size_t i = 0u; i < backends.size(); ++i) {
            const auto decision =
                arp::rhi::semantics::OriginalCommandAdmissionLowering::lowering(backends[i]);
            assert(decision.contractVersion == 1u && !decision.executionAvailable);
            assert(!std::strcmp(decision.diagnostic, diagnostics[i]));
            assert(decision.disposition
                == (i == 0u ? arp::rhi::semantics::OriginalCommandAdmissionLowering::Disposition::
                                  ValidatorOnly
                            : arp::rhi::semantics::OriginalCommandAdmissionLowering::Disposition::
                                  DiagnosticFailure));
            assert(!std::strcmp(
                arp::rhi::semantics::OriginalCommandAdmissionLowering::executionDiagnostic(
                    backends[i], budget),
                diagnostics[i]));
            invalid = budget;
            invalid.prototypeOptIn = false;
            assert(!std::strcmp(
                arp::rhi::semantics::OriginalCommandAdmissionLowering::executionDiagnostic(
                    backends[i], invalid),
                "RHI-ORIGINAL-COMMAND-OPT-IN"));
        }
        assert(!arp::rhi::semantics::OriginalCommandAdmissionLowering::lowering(
            static_cast<Backend>(UINT32_MAX))
                .executionAvailable);
        assert(!std::strcmp(arp::rhi::semantics::OriginalCommandAdmissionLowering::lowering(
                                static_cast<Backend>(UINT32_MAX))
                                .diagnostic,
            "RHI-ORIGINAL-COMMAND-BACKEND-UNKNOWN"));
        for (std::uint32_t i = 0u; i < static_cast<std::uint32_t>(K::Count); ++i) {
            const auto kind = static_cast<K>(i);
            const auto f = CommandInputContract::family(kind);
            switch (kind) {
            case K::BeginRenderPass:
                assert(f == F::Pass);
                break;
            case K::BufferCopy:
            case K::BufferToTextureCopy:
            case K::TextureToBufferCopy:
            case K::TextureToTextureCopy:
                assert(f == F::Copy);
                break;
            case K::BufferBarrier:
            case K::TextureBarrier:
            case K::BufferMemoryBarrier:
            case K::TextureMemoryBarrier:
            case K::AliasBarrier:
                assert(f == F::Dependency);
                break;
            case K::BufferUpload:
                assert(f == F::BufferUpload);
                break;
            case K::TextureUpload:
                assert(f == F::TextureUpload);
                break;
            case K::TextureReadback:
                assert(f == F::TextureReadback);
                break;
            case K::LegacySizeOnlyBufferCopy:
                assert(f == F::Unsupported);
                break;
            default:
                assert(f == F::Scalar);
                break;
            }
        }
        assert(CommandInputContract::family(K::Count) == F::Unsupported);
        assert(CommandInputContract::family(static_cast<K>(UINT32_MAX)) == F::Unsupported);
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::puts(
            "BINDLESS_RHI_GENERIC_ORIGINAL_ADMISSION_CONTRACT_OK domains15 backends4 independent-budgets shared-classifier metadata-only execution-closed allocation0");
    }
};
} // namespace arp::test
