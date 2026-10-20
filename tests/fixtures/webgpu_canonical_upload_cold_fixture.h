#pragma once
#include "original_command_buffer_upload_inputs_fixture.h"
#include "original_command_texture_upload_inputs_fixture.h"
#include "rhi/backends/webgpu/webgpu_canonical_cold_command.h"
#include "webgpu_canonical_upload_joins_fixture.h"

namespace arp::test {
class WebGpuCanonicalUploadColdFixture final {
    using Admission = rhi_webgpu::WebGpuCanonicalColdAdmission;
    using Joins = rhi_webgpu::WebGpuCanonicalUploadJoins;
    using Seal = rhi::OriginalCommandRecordingSeal;
    using Reservation = rhi::OriginalCommandLifecycleReservation;
    static constexpr std::uint64_t bytes = 65536u;

    static void is(const char* actual, const char* expected)
    {
        if (!actual || std::string_view(actual) != expected)
            std::fprintf(stderr,
                "upload cold actual=%s expected=%s\n",
                actual ? actual : "<none>",
                expected);
        assert(actual && std::string_view(actual) == expected);
    }

public:
    static Admission::Budget budget(bool texture, std::uint32_t owners)
    {
        const auto r = Admission::selectedRequirements();
        Admission::Budget b;
        b.contractVersion = 3u;
        b.prototypeOptIn = true;
        b.maxOriginalCommands = 5u;
        b.maxEncoders = b.maxCommandBuffers = 1u;
        b.maxRecordingScopeCallbacks = r.recordingScopeCallbacks;
        b.maxSubmissionScopeCallbacks = r.submissionScopeCallbacks;
        b.maxQueueCallbacks = r.queueCallbacks;
        b.maxRuntimeLossCallbacks = r.runtimeLossCallbacks;
        b.maxRuntimeLossRegistrations = r.runtimeLossRegistrations;
        b.maxRetainedOwners = owners;
        b.maxCpuMetadataBytes = bytes;
        b.maxBufferUploads = texture ? 0u : 2u;
        b.maxTextureUploads = texture ? 2u : 0u;
        b.maxTextureUploadMips = texture ? 4u : 0u;
        b.maxNativeCopyCommands = texture ? 4u : 2u;
        b.maxBufferUploadBytes = texture ? 0u : 32u;
        b.maxPackedTextureUploadBytes = texture ? 1536u : 0u;
        b.maxTotalUploadBytes = texture ? 1536u : 32u;
        b.maxTextureUploadPixelBytes = b.maxTextureWriteTexelBytes = texture ? 40u : 0u;
        return b;
    }

    static void verify(const std::shared_ptr<rhi::OriginalCommandStateTransaction>& root,
        rhi::GpuDevice& device,
        Joins::Source source,
        const rhi::OriginalDeviceNativeLossObservation& loss,
        bool texture,
        const std::shared_ptr<core::CpuMemoryQuota>& substitutedCallback = {})
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        const char* diagnostic = nullptr;
        const std::array<Joins::Input, 2> entries{{{1u, source}, {2u, source}}};
        auto joins = Joins::prepareOriginal(root,
            device,
            entries.data(),
            entries.size(),
            WebGpuCanonicalUploadJoinsFixture::budget(*root, texture),
            diagnostic);
        assert(joins);
        using Closure = rhi_webgpu::WebGpuCanonicalNativeClosure;
        // Two original resource bindings, at most three native ownership slots
        // each, plus closure and existing captured loss subscription.
        auto closure = Closure::prepareOriginalLossBound(root,
            device,
            {1u,
                true,
                2u,
                2u,
                root->retainedOwnerCount() + 2u + 2u * 3u,
                joins->stagingResourceBytes() + joins->destinationResourceBytes(),
                bytes},
            loss,
            diagnostic);
        assert(closure);
        const auto owners = closure->retainedOwnerCount() + Seal::additionalOwnerCount
            + Reservation::additionalOwnerCount + 1u + joins->retainedOwnerCount()
            - root->retainedOwnerCount();
        const auto b = budget(texture, owners);
        Admission::Requirements required;
        assert(!Admission::deriveRequirements(root, b, required));
        assert(required.contractVersion == 3u && required.originalCommands == 5u
            && !required.renderPasses && required.nativeCopyCommands == b.maxNativeCopyCommands
            && required.totalUploadBytes == b.maxTotalUploadBytes
            && required.textureWriteTexelBytes == b.maxTextureWriteTexelBytes);
        for (auto member :
            {&Admission::Budget::maxNativeCopyCommands, &Admission::Budget::maxTotalUploadBytes}) {
            auto bad = b;
            --(bad.*member);
            Admission::Requirements refused = required;
            assert(Admission::deriveRequirements(root, bad, refused));
            assert(!refused.originalCommands && !refused.totalUploadBytes);
        }
        auto submission = core::makeShared<core::CpuMemoryQuota>(bytes);
        auto callback = substitutedCallback ? substitutedCallback
                                            : core::makeShared<core::CpuMemoryQuota>(bytes);
        auto seal = Seal::prepareOriginal(root,
            device,
            {1u,
                true,
                required.capacity.pendingSubmissions,
                required.capacity.callbackTickets,
                required.capacity.callbackRegistrations,
                owners,
                owners,
                owners,
                bytes,
                bytes,
                bytes},
            submission,
            callback,
            diagnostic);
        assert(seal);
        auto reservation = Reservation::prepareOriginal(seal, required.capacity, diagnostic);
        assert(reservation);
        const auto make = [&] {
            return Admission::prepareOriginal(closure, reservation, b, diagnostic, joins);
        };
        if (substitutedCallback) {
            assert(!make());
            is(diagnostic, "RHI-WEBGPU-CANONICAL-COLD-UPLOAD-QUOTA-DOMAIN");
            std::printf(
                "BINDLESS_RHI_CANONICAL_UPLOAD_COLD_QUOTA_REFUSED %s initialized-proof-callback-domain no-native-command\n",
                texture ? "texture" : "buffer");
            return;
        }
        assert(!Admission::prepareOriginal(closure, reservation, b, diagnostic));
        is(diagnostic, "RHI-WEBGPU-UPLOAD-JOIN-OWNER");
        auto forged = std::shared_ptr<const Joins>(submission, joins.get());
        assert(!Admission::prepareOriginal(closure, reservation, b, diagnostic, forged));
        is(diagnostic, "RHI-WEBGPU-UPLOAD-JOIN-OWNER");
        forged.reset();
        {
            const auto& inventory = root->originalGraph()
                                        ->originalInputs()
                                        ->originalScalars()
                                        ->originalUsage()
                                        ->originalSources()
                                        ->sources();
            auto otherInputs = texture
                ? OriginalCommandTextureUploadInputsFixture::prepareForSource(
                      std::get<std::shared_ptr<const rhi::OriginalCommandTextureUploadSource>>(
                          inventory[1u]),
                      diagnostic,
                      std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                          inventory[2u]),
                      2u,
                      768u,
                      true)
                : OriginalCommandBufferUploadInputsFixture::prepareForSource(
                      std::get<std::shared_ptr<const rhi::OriginalCommandBufferUploadSource>>(
                          inventory[2u]),
                      diagnostic,
                      2u,
                      16u,
                      true);
            assert(otherInputs);
            auto otherRoot = WebGpuCanonicalUploadJoinsFixture::prepareRoot(otherInputs,
                device,
                root->originalGraph()->originalMetadataQuota(),
                root->originalMetadataQuota(),
                texture,
                diagnostic);
            assert(otherRoot);
            auto other = Joins::prepareOriginal(otherRoot,
                device,
                entries.data(),
                entries.size(),
                WebGpuCanonicalUploadJoinsFixture::budget(*otherRoot, texture),
                diagnostic);
            assert(other);
            assert(!Admission::prepareOriginal(closure, reservation, b, diagnostic, other));
            is(diagnostic, "RHI-WEBGPU-CANONICAL-COLD-UPLOAD-ROOT");
        }
        CpuQuotaFactoryFixture pressure(root->originalMetadataQuota());
        const auto peak = pressure.measure(make);
        pressure.verify(peak, make);
        {
            core::CpuMemoryQuotaLease full(
                root->originalMetadataQuota(), bytes - root->originalMetadataQuota()->liveBytes());
            assert(full.charged());
            assert(!make());
            is(diagnostic, "RHI-WEBGPU-CANONICAL-COLD-ALLOCATION");
        }
        auto admission = make();
        assert(admission && !admission->validate() && admission->retainedOwnerCount() == owners
            && core::SharedOwnerProjection::sameOwner(admission->originalUploadJoins(), joins)
            && !admission->nativeExecutionProven());
        auto bad = b;
        --bad.maxRetainedOwners;
        assert(!Admission::prepareOriginal(closure, reservation, bad, diagnostic, joins));
        is(diagnostic, "RHI-WEBGPU-CANONICAL-COLD-OWNER-CAPACITY");
        const auto& original = root->originalGraph()
                                   ->originalInputs()
                                   ->originalScalars()
                                   ->originalUsage()
                                   ->originalSources();
        assert(original->originalReservedSubmissionCount() == 1u
            && original->originalReservedCallbackTicketCount() == required.capacity.callbackTickets
            && original->originalReservedCallbackRegistrationCount()
                == required.capacity.callbackRegistrations);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{root->originalMetadataQuota(), submission, callback},
            [&]() noexcept -> const char* {
                if (const auto* id = admission->validate())
                    return id;
                is(admission->executionDiagnostic(),
                    "RHI-ORIGINAL-NATIVE-COLD-UPLOAD-COLD-JOIN-ONLY");
                assert(!rhi_webgpu::WebGpuCanonicalColdCommand::prepareOriginal(
                    admission, {1u, true, 1u, owners + 3u, bytes}, diagnostic));
                is(diagnostic, "RHI-WEBGPU-CANONICAL-COLD-COMMAND-OWNER-CAPACITY");
                return nullptr;
            }));
        assert(root->status() == rhi::OriginalCommandStateTransaction::Status::Prepared
            && root->journalEmpty());
        std::printf(
            "BINDLESS_RHI_CANONICAL_UPLOAD_COLD_OK %s peak%llu original5 native-copies%llu aggregate-upload-work%llu same-root-joins scopes3-3-queue1 loss1 original-capacity1-8-8 cold-exact-less-exhausted owner-count-byte-forged-missing-refused hot-allocation0 command-storage-budget-refused\n",
            texture ? "texture" : "buffer",
            static_cast<unsigned long long>(peak),
            static_cast<unsigned long long>(required.nativeCopyCommands),
            static_cast<unsigned long long>(required.totalUploadBytes));
#else
        (void)root;
        (void)device;
        (void)source;
        (void)loss;
        (void)texture;
        (void)substitutedCallback;
#endif
    }
};
} // namespace arp::test
