#include "rhi/frontend/transfer_buffer_readback.h"
#include "rhi/frontend/transfer_readback_borrowed_retirement.h"
#include "rhi/frontend/transfer_readback_copy.h"
#include "rhi/frontend/transfer_readback_coverage.h"
#include "rhi/frontend/transfer_readback_owned_preparation.h"
#include "rhi/frontend/transfer_upload_copy.h"
#include "rhi/frontend/transfer_upload_generation_validation.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>

#ifdef NDEBUG
#error Transfer lowering contract tests require assertions
#endif

namespace {
using namespace arp;
using namespace arp::rhi;
constexpr std::array backends{Backend::Null,
    Backend::Vulkan,
    Backend::Metal,
    Backend::WebGPU,
    Backend::WebGL,
    static_cast<Backend>(254u),
    static_cast<Backend>(255u)};

bool original(Backend b)
{
    return b == Backend::Null || b == Backend::Vulkan || b == Backend::Metal
        || b == Backend::WebGPU;
}

void expect(const char* actual, const char* expected)
{
    if (!actual || std::strcmp(actual, expected))
        std::fprintf(stderr, "expected %s, got %s\n", expected, actual ? actual : "<null>");
    assert(actual && std::strcmp(actual, expected) == 0);
}

void versionedMetadata()
{
    for (auto backend : backends) {
        const bool known = original(backend), validator = backend == Backend::Null;
        for (auto version : {1u, 2u, 3u, 4u, 5u, 6u}) {
            const auto binding =
                semantics::OriginalBufferReadbackBindingLowering::lowering(backend, version);
            const bool metadata = known && (version != 3u || backend != Backend::WebGPU);
            assert(binding.cpuMetadataImplemented == metadata);
            assert(binding.pendingAssociationImplemented == (metadata && version >= 2u));
            assert(binding.validatorOnly == validator && !binding.nativeExecutionImplemented);
            assert(binding.contractVersion == (known || backend == Backend::WebGL ? version : 0u));
            if (backend == Backend::WebGL)
                expect(binding.diagnostic, "RHI-WEBGL1-BUFFER-READBACK-BINDING-UNAVAILABLE");
            else if (!known)
                expect(binding.diagnostic, "RHI-ORIGINAL-BUFFER-READBACK-BINDING-BACKEND-UNKNOWN");
            else if (metadata)
                assert(!binding.diagnostic);
            else
                expect(binding.diagnostic,
                    "RHI-ORIGINAL-GEOMETRY-READBACK-BINDING-WEBGPU-UNAVAILABLE");
        }
        const auto request = BufferReadbackRequestDeclaration::lowering(backend);
        assert(request.contractVersion == (known || backend == Backend::WebGL ? 1u : 0u));
        assert(request.cpuMetadataImplemented == known && request.validatorOnly == validator);
        assert(!request.nativeExecutionImplemented);
        if (backend == Backend::WebGL)
            expect(request.diagnostic, "RHI-WEBGL1-BUFFER-READBACK-REQUEST-UNAVAILABLE");
        else if (known)
            assert(!request.diagnostic);
        else
            expect(request.diagnostic, "RHI-BUFFER-READBACK-BACKEND-UNKNOWN");
        for (auto version : {1u, 2u}) {
            const auto lease =
                semantics::TextureReadbackSourceLeaseLowering::lowering(backend, version);
            assert(lease.contractVersion == (known || backend == Backend::WebGL ? version : 0u));
            assert(lease.cpuMetadataImplemented == known && lease.validatorOnly == validator);
            assert(!lease.nativeExecutionImplemented);
            if (backend == Backend::WebGL)
                expect(lease.diagnostic, "RHI-WEBGL1-READBACK-SOURCE-LEASE-UNAVAILABLE");
            else if (!known)
                expect(lease.diagnostic, "RHI-READBACK-SOURCE-LEASE-BACKEND-UNKNOWN");
            else
                expect(lease.diagnostic,
                    version == 1u   ? "RHI-TEXTURE-READBACK-ORIGINAL-OWNER-UNPROVEN"
                        : validator ? "RHI-READBACK-SOURCE-LEASE-NULL-VALIDATOR-ONLY"
                                    : "RHI-READBACK-SOURCE-LEASE-COMPLETION-UNPROVEN");
        }
        for (auto version : {0u, 7u, std::numeric_limits<std::uint32_t>::max()}) {
            const auto binding =
                semantics::OriginalBufferReadbackBindingLowering::lowering(backend, version);
            assert(!binding.cpuMetadataImplemented && !binding.pendingAssociationImplemented
                && !binding.validatorOnly && !binding.nativeExecutionImplemented);
            expect(binding.diagnostic, "RHI-ORIGINAL-BUFFER-READBACK-BINDING-VERSION");
            const auto requestInvalid =
                BufferReadbackRequestDeclaration::lowering(backend, version);
            assert(!requestInvalid.cpuMetadataImplemented && !requestInvalid.validatorOnly
                && !requestInvalid.nativeExecutionImplemented);
            expect(requestInvalid.diagnostic, "RHI-BUFFER-READBACK-VERSION");
            const auto lease =
                semantics::TextureReadbackSourceLeaseLowering::lowering(backend, version);
            assert(!lease.cpuMetadataImplemented && !lease.validatorOnly
                && !lease.nativeExecutionImplemented);
            expect(lease.diagnostic, "RHI-READBACK-SOURCE-LEASE-VERSION");
        }
        for (auto version : {2u, 3u}) {
            const auto requestInvalid =
                BufferReadbackRequestDeclaration::lowering(backend, version);
            assert(!requestInvalid.cpuMetadataImplemented && !requestInvalid.validatorOnly
                && !requestInvalid.nativeExecutionImplemented);
            expect(requestInvalid.diagnostic, "RHI-BUFFER-READBACK-VERSION");
        }
        const auto leaseInvalid =
            semantics::TextureReadbackSourceLeaseLowering::lowering(backend, 3u);
        assert(!leaseInvalid.cpuMetadataImplemented && !leaseInvalid.validatorOnly
            && !leaseInvalid.nativeExecutionImplemented);
        expect(leaseInvalid.diagnostic, "RHI-READBACK-SOURCE-LEASE-VERSION");
    }
}

void unversionedMetadata()
{
    for (auto backend : backends) {
        const bool known = original(backend), validator = backend == Backend::Null;
        const auto input = TextureReadbackInputSnapshot::lowering(backend);
        const auto cold = TextureReadbackColdBudgetPlan::lowering(backend);
        const auto coverage = semantics::TextureReadbackContentCoverage::lowering(backend);
        const auto statistics = TextureReadbackStatisticsPlan::lowering(backend);
        const auto packed = semantics::TextureUploadPackedCopyMapping::lowering(backend);
        const auto generation = semantics::TextureUploadGenerationValidation::lowering(backend);
        assert(input.cpuMetadataImplemented == known && input.validatorOnly == validator);
        assert(cold.cpuBudgetImplemented == known && cold.validatorOnly == validator);
        assert(coverage.metadataEquivalent == known && !coverage.nativeExecution);
        assert(statistics.cpuPlanImplemented == known && statistics.validatorOnly == validator);
        assert(packed.metadataEquivalent == known && !packed.nativeExecution);
        assert(generation.validatorOnly == validator && !generation.executable);
        if (backend == Backend::WebGL) {
            expect(input.diagnostic, "RHI-WEBGL1-READBACK-INPUT-SNAPSHOT-UNAVAILABLE");
            expect(cold.diagnostic, "RHI-WEBGL1-READBACK-COLD-BUDGET-UNAVAILABLE");
            expect(coverage.diagnostic, "RHI-WEBGL1-READBACK-COVERAGE-UNAVAILABLE");
            expect(statistics.diagnostic, "RHI-WEBGL1-READBACK-STATISTICS-UNAVAILABLE");
            expect(packed.diagnostic, "RHI-WEBGL1-PACKED-COPY-UNAVAILABLE");
            expect(generation.diagnostic, "RHI-WEBGL1-UPLOAD-GENERATION-UNAVAILABLE");
        } else if (known) {
            expect(input.diagnostic, "RHI-TEXTURE-READBACK-ORIGINAL-OWNER-UNPROVEN");
            expect(cold.diagnostic,
                validator ? "RHI-READBACK-COLD-BUDGET-VALIDATOR-ONLY"
                          : "RHI-READBACK-COLD-BUDGET-NATIVE-EXECUTION-REQUIRED");
            expect(coverage.diagnostic, "RHI-TEXTURE-READBACK-COVERAGE-EXECUTION-UNPROVEN");
            expect(statistics.diagnostic,
                validator ? "RHI-READBACK-STATISTICS-VALIDATOR-ONLY"
                          : "RHI-READBACK-STATISTICS-NATIVE-PUBLICATION-REQUIRED");
            expect(packed.diagnostic, "RHI-TEXTURE-PACKED-COPY-EXECUTION-UNPROVEN");
            expect(generation.diagnostic, "RHI-TEXTURE-UPLOAD-ORIGINAL-GENERATION-OWNER-UNPROVEN");
        } else {
            expect(input.diagnostic, "RHI-TEXTURE-READBACK-BACKEND-UNKNOWN");
            expect(cold.diagnostic, "RHI-READBACK-COLD-BUDGET-BACKEND");
            expect(coverage.diagnostic, "RHI-TEXTURE-READBACK-COVERAGE-BACKEND-UNKNOWN");
            expect(statistics.diagnostic, "RHI-READBACK-STATISTICS-BACKEND");
            expect(packed.diagnostic, "RHI-TEXTURE-PACKED-COPY-BACKEND-UNKNOWN");
            expect(generation.diagnostic, "RHI-TEXTURE-UPLOAD-GENERATION-BACKEND-UNKNOWN");
        }
    }
}

void selectedPathsCannotAuthorizeWebGl()
{
    using Owned = TextureReadbackOwnedPreparationContract;
    using Borrowed = BorrowedTextureReadbackRetirementContract;
    using Content = TextureReadbackContentIntent;
    for (auto backend : backends) {
        for (bool bounded : {false, true}) {
            const auto borrowed = Borrowed::lowering(backend, bounded);
            assert(borrowed.contractVersion == 1u);
            assert(borrowed.executionAvailable == (backend == Backend::Metal && bounded));
            if (backend == Backend::WebGL) {
                assert(borrowed.disposition == Borrowed::Disposition::Unavailable);
                expect(borrowed.diagnostic, "RHI-WEBGL1-READBACK-BORROWED-UNAVAILABLE");
            } else if (backend == Backend::Null) {
                assert(borrowed.disposition == Borrowed::Disposition::Unavailable);
                expect(borrowed.diagnostic, "RHI-READBACK-BORROWED-NULL-UNAVAILABLE");
            } else if (backend == Backend::Vulkan || backend == Backend::WebGPU) {
                assert(borrowed.disposition == Borrowed::Disposition::RetiredDiagnosticFailure);
                expect(borrowed.diagnostic,
                    backend == Backend::Vulkan ? "RHI-READBACK-BORROWED-VULKAN-RETIRED"
                                               : "RHI-READBACK-BORROWED-WEBGPU-RETIRED");
            } else if (backend == Backend::Metal) {
                assert(borrowed.disposition == Borrowed::Disposition::ExplicitBoundedLegacy);
                expect(borrowed.diagnostic,
                    bounded ? "RHI-READBACK-BORROWED-METAL-BOUNDED-LEGACY"
                            : "RHI-READBACK-BUDGET-REQUIRED");
            } else {
                assert(borrowed.disposition == Borrowed::Disposition::Unavailable);
                expect(borrowed.diagnostic, "RHI-READBACK-BORROWED-BACKEND-UNKNOWN");
            }
        }
        for (unsigned bits = 0u; bits < 32u; ++bits) {
            const Owned::ContextFacts facts{bool(bits & 1u),
                bool(bits & 2u),
                bool(bits & 4u),
                bool(bits & 8u),
                bool(bits & 16u)};
            const auto resolved = Owned::resolveLowering(backend, facts);
            const bool contextReady = facts.configured && facts.nativePayload
                && facts.originalLossArmed && facts.accepting && !facts.actualLoss;
            assert(resolved.executionAvailable == (backend == Backend::WebGPU && contextReady));
            if (backend == Backend::WebGL) {
                assert(resolved.disposition == Owned::Disposition::DiagnosticFailure);
                expect(resolved.diagnostic, "RHI-WEBGL1-READBACK-OWNED-UNAVAILABLE");
            }
            unsigned callbacks = 0u;
            const auto direct = semantics::TransferLowering::resolveOwnedReadback<Owned::Decision>(
                backend, facts, [&callbacks](const auto& value) noexcept {
                    ++callbacks;
                    return Owned::validateExecutionContext(value);
                });
            assert(callbacks == (backend == Backend::WebGPU ? 1u : 0u));
            assert(direct.executionAvailable == resolved.executionAvailable);
        }
        for (unsigned bits = 0u; bits < 8u; ++bits) {
            const Content::Descriptor desc{bits & 1u ? 1u : 0u,
                bool(bits & 2u),
                bits & 4u ? Content::Mode::CompletedOriginalUpload
                          : Content::Mode::QueueOrderedSnapshot};
            const auto content = Content::lower(backend, desc);
            assert(!content.nativeExecutionProven());
            if (const auto* invalid = Content::validate(desc))
                expect(content.diagnostic, invalid);
            else if (backend == Backend::WebGL) {
                assert(content.outcome == Content::Outcome::DiagnosticFailure);
                expect(content.diagnostic, "RHI-WEBGL1-READBACK-CONTENT-UNAVAILABLE");
            } else if (backend == Backend::WebGPU) {
                assert(content.outcome == Content::Outcome::Prototype && !content.diagnostic);
            } else {
                assert(content.diagnostic);
            }
        }
    }
}

void copyRejectionPreservesOutput()
{
    using Upload = semantics::TextureUploadCopyLowering;
    using Readback = semantics::TextureReadbackCopyLowering;
    for (auto backend : backends) {
        for (bool optIn : {false, true}) {
            for (auto version : {0u, 1u, 2u, UINT32_MAX}) {
                Upload::Decision upload;
                upload.contractVersion = 71u;
                upload.backend = Backend::Metal;
                upload.mipIndex = 93u;
                const auto uploadBefore = upload;
                const auto* up =
                    Upload::lowerOriginal(backend, version, optIn, {}, {}, 0u, {}, upload);
                assert(up && upload.matches(uploadBefore));
                Readback::Decision readback;
                readback.contractVersion = 53u;
                readback.backend = Backend::Vulkan;
                readback.mipIndex = 79u;
                const auto readbackBefore = readback;
                const auto* down =
                    Readback::lowerOriginal(backend, version, optIn, {}, 0u, {}, readback);
                assert(down && readback.matches(readbackBefore));
                if (version != 1u) {
                    expect(up, "RHI-TEXTURE-COPY-LOWERING-VERSION");
                    expect(down, "RHI-READBACK-COPY-LOWERING-VERSION");
                } else if (!optIn) {
                    expect(up, "RHI-TEXTURE-COPY-LOWERING-OPT-IN");
                    expect(down, "RHI-READBACK-COPY-LOWERING-OPT-IN");
                } else if (backend == Backend::WebGL) {
                    expect(up, "RHI-WEBGL1-UPLOAD-COPY-UNAVAILABLE");
                    expect(down, "RHI-WEBGL1-READBACK-COPY-UNAVAILABLE");
                }
            }
        }
    }
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::strcmp(argv[1], "diagnostic") == 0) {
        expect(arp::rhi::BufferReadbackRequestDeclaration::lowering(arp::rhi::Backend::WebGL)
                   .diagnostic,
            "RHI-WEBGL1-BUFFER-READBACK-REQUEST-UNAVAILABLE");
        return 0;
    }
    assert(argc == 1);
    const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
    versionedMetadata();
    unversionedMetadata();
    selectedPathsCannotAuthorizeWebGl();
    copyRejectionPreservesOutput();
    assert(arp::core::defaultCpuAllocator().stats().allocationCount == allocations);
}
