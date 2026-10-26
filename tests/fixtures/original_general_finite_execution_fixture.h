#pragma once
#include "core/math/half_float.h"
#include "original_general_finite_native_recording_fixture.h"
#include "original_general_output_readback_inputs_fixture.h"

namespace arp::test {
class OriginalGeneralFiniteExecutionFixture final {
    using Snapshot = rhi_webgpu::WebGpuOriginalGeneralOutputSnapshot;
    using Contract = rhi::OriginalGeneralOutputSnapshotContract;
    using Stage = OriginalGeneralFiniteNativeRecordingFixture::CompletionStage;

    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "General finite execution7: %s\n", id);
        assert(!id);
    }

public:
    // Derive the numerical oracle from the authenticated selected buffer bytes
    // and the original uploaded texels. Both Slang shaders sample mip zero at
    // its centre with a linear, clamp-to-edge, non-comparison sampler.
    static std::array<std::uint16_t, 4u> expectedOutput(rhi::DescriptorType type,
        const core::ByteBuffer& initialized,
        std::uint64_t selectedOffset,
        const core::ByteBuffer& pixels,
        const rhi::SamplerDesc& sampler)
    {
        assert(selectedOffset <= initialized.size() && initialized.size() - selectedOffset >= 16u);
        assert(pixels.size() == 20u && sampler.minFilter == rhi::SamplerFilter::Linear
            && sampler.magFilter == rhi::SamplerFilter::Linear
            && sampler.addressU == rhi::SamplerAddressMode::ClampToEdge
            && sampler.addressV == rhi::SamplerAddressMode::ClampToEdge
            && sampler.compareOp == rhi::CompareOp::Never && sampler.maxAnisotropy <= 1.0f);
        const auto reference = core::analyzeStridedImageChannels(pixels.data(),
            pixels.size(),
            2u,
            2u,
            8u,
            4u,
            4u,
            [](const std::uint8_t* pixel, std::uint32_t channel) {
                return double(pixel[channel]) / 255.0;
            });
        assert(reference.valid);
        std::array<std::uint16_t, 4u> expected;
        for (std::size_t channel = 0u; channel < expected.size(); ++channel) {
            float value = 0.0f;
            const auto* selected = initialized.data() + selectedOffset + sizeof(value) * channel;
            if (type == rhi::DescriptorType::ReadOnlyStorageBuffer) {
                std::uint32_t word = 0u;
                std::memcpy(&word, selected, sizeof(word));
                value = float(word);
            } else {
                assert(type == rhi::DescriptorType::UniformBuffer);
                std::memcpy(&value, selected, sizeof(value));
            }
            expected[channel] =
                core::floatToHalf(value + float(reference.channels[channel].meanValue));
            assert(std::isfinite(core::halfToFloat(expected[channel])));
        }
        return expected;
    }

    static void verifyStage(std::shared_ptr<const Snapshot>& snapshot,
        const std::shared_ptr<const rhi_webgpu::WebGpuOriginalTypedIssuerPreparation>& issuer,
        const std::shared_ptr<rhi::OriginalCommandStateTransaction>& root,
        const std::shared_ptr<const rhi::OriginalCommandTextureLease>& output,
        const std::shared_ptr<const rhi_webgpu::WebGpuOriginalTypedNativePayload>& payload,
        rhi::GpuDevice& device,
        const std::shared_ptr<core::CpuMemoryQuota>& resource,
        const std::array<std::uint16_t, 4u>& expected,
        Stage stage)
    {
        const char* id = nullptr;
        if (stage == Stage::Prepared) {
            assert(payload && !snapshot);
            const auto& sources = root->originalGraph()
                                      ->originalInputs()
                                      ->originalScalars()
                                      ->originalUsage()
                                      ->originalSources();
            const auto quota = sources->originalMetadataQuota();
            const auto& decision = payload->originalDecision();
            const auto projected = rhi::common::OriginalTypedUsageProjection::inspectStateBound(
                decision->originalClosure()->originalStream(),
                root->originalGraph(),
                decision->originalRequest().projection.usages);
            require(projected.diagnostic);
            Snapshot::Budget budget{2u,
                true,
                Contract::Provenance::QueueOrderedSnapshot,
                rhi::defaultTextureSubresourceRange(output->originalObject()->desc()),
                issuer->retainedOwnerCount() + Contract::additionalOwnerCount,
                root->originalGraph()
                        ->originalInputs()
                        ->originalScalars()
                        ->originalUsage()
                        ->commands()
                        .size()
                    + projected.counts.contributions,
                output->originalObject()->allocation().estimatedSizeBytes,
                quota->limitBytes()};
            const auto make = [&] {
                return Snapshot::prepareOriginal(issuer, root, output, budget, quota, id);
            };
            CpuQuotaFactoryFixture pressure(quota);
            const auto peak = pressure.measureRetained(make);
            pressure.verify(peak, make);
            const auto valid = budget;
            for (unsigned field = 0u; field < 5u; ++field) {
                budget = valid;
                if (field == 0u)
                    --budget.maxRetainedOwners;
                if (field == 1u)
                    --budget.maxJoinVisits;
                if (field == 2u)
                    --budget.maxSourceBytes;
                if (field == 3u)
                    budget.contractVersion = 1u;
                if (field == 4u)
                    budget.contractVersion = 3u;
                assert(!make() && id);
            }
            budget = valid;
            auto forgedIssuer =
                std::shared_ptr<const rhi_webgpu::WebGpuOriginalTypedIssuerPreparation>(
                    quota, issuer.get());
            assert(!Snapshot::prepareOriginal(forgedIssuer, root, output, budget, quota, id));
            forgedIssuer.reset();
            auto forgedRoot =
                std::shared_ptr<const rhi::OriginalCommandStateTransaction>(quota, root.get());
            assert(!Snapshot::prepareOriginal(issuer, forgedRoot, output, budget, quota, id));
            forgedRoot.reset();
            auto forgedOutput =
                std::shared_ptr<const rhi::OriginalCommandTextureLease>(quota, output.get());
            assert(!Snapshot::prepareOriginal(issuer, root, forgedOutput, budget, quota, id));
            forgedOutput.reset();
            snapshot = make();
            require(id);
            assert(snapshot && Snapshot::validateCompletedOriginal(snapshot));
            std::printf(
                "BINDLESS_RHI_GENERAL_OUTPUT_SNAPSHOT2_PREPARED_OK Issuer7 ContentPlan5 owners%u visits%llu bytes%llu metadata%llu exact-less-exhausted cross-profile-forged-refused\n",
                budget.maxRetainedOwners,
                static_cast<unsigned long long>(budget.maxJoinVisits),
                static_cast<unsigned long long>(budget.maxSourceBytes),
                static_cast<unsigned long long>(peak));
        } else if (stage != Stage::Committed) {
            assert(snapshot
                && root->status() == rhi::OriginalCommandStateTransaction::Status::Pending
                && Snapshot::validateCompletedOriginal(snapshot));
            assert(!output->originalObject()->observeOriginalCommittedState(
                snapshot->originalBudget().range));
        } else {
            assert(snapshot && issuer->hasOriginalCompletedSubmission());
            require(Snapshot::validateCompletedOriginal(snapshot));
            using Readback = OriginalGeneralOutputReadbackInputsFixture;
            const auto mapTerminal = Readback::selectedMapTerminal();
            OriginalGeneralOutputReadbackInputsFixture::verifyPrepared(
                device, snapshot, resource, expected);
            // The downstream copy changes output state. Preserve original
            // completion evidence, but never reuse its stale upstream graph.
            assert(Snapshot::validateCompletedOriginal(snapshot));
            if (Readback::mapCompletes(mapTerminal))
                std::printf(
                    "BINDLESS_RHI_GENERAL_FINITE_NUMERICAL2_OK original-copy1 map1 unmap-drain committed-published rgba %.8f %.8f %.8f %.8f Output2 selected-bytes-oracle public-closed\n",
                    double(core::halfToFloat(expected[0])),
                    double(core::halfToFloat(expected[1])),
                    double(core::halfToFloat(expected[2])),
                    double(core::halfToFloat(expected[3])));
            if (mapTerminal != Readback::MapTerminal::None)
                std::printf(
                    "BINDLESS_RHI_GENERAL_FINITE_MAP4_OUTPUT_OK %s Snapshot2 Output2 original-upstream-completed original-copy-map-drained %s %s snapshot-reuse-refused public-closed\n",
                    Readback::mapTerminalName(mapTerminal),
                    Readback::mapCompletes(mapTerminal) ? "numerical-published"
                                                        : "invalidated-unpublished-no-retry",
                    Readback::mapUsesPhysicalLoss(mapTerminal) ? "actual-device-loss"
                                                               : "no-device-loss");
            snapshot.reset();
        }
    }
};
} // namespace arp::test
