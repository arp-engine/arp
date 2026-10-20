#pragma once

#include "buffer_upload_arguments_fixture.h"
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_command_inputs.h"
#include "texture_readback_arguments_fixture.h"
#include "texture_readback_content_coverage_fixture.h"
#include "texture_readback_layout_fixture.h"
#include "texture_upload_arguments_fixture.h"
#include "texture_upload_content_transaction_fixture.h"
#include "texture_upload_packing_fixture.h"
#include "webgpu_completed_texture_readback_destination_fixture.h"
#include "webgpu_completed_texture_readback_source_fixture.h"
#include "webgpu_original_readback_state_transaction_fixture.h"
#include "webgpu_original_texture_readback_completion_fixture.h"
#include "webgpu_original_texture_readback_copies_fixture.h"
#include "webgpu_original_texture_readback_declaration_fixture.h"
#include "webgpu_original_texture_readback_destination_fixture.h"
#include "webgpu_original_texture_readback_inputs_fixture.h"
#include "webgpu_original_texture_readback_mapping_fixture.h"
#include "webgpu_original_texture_readback_pair_fixture.h"
#include "webgpu_original_texture_readback_projection_fixture.h"
#include "webgpu_original_texture_readback_recording_fixture.h"
#include "webgpu_original_texture_readback_source_fixture.h"
#include "webgpu_original_texture_readback_state_intent_fixture.h"

#include <cassert>
#include <type_traits>

namespace arp::test {
class WebGpuFrozenTransferArgumentsFixture final {
public:
    static void verifyContract()
    {
        BufferUploadArgumentsFixture::verify();
        TextureUploadArgumentsFixture::verify();
        TextureReadbackArgumentsFixture::verify();
        TextureReadbackLayoutFixture::verify();
        TextureReadbackContentCoverageFixture::verify();
        WebGpuOriginalTextureReadbackSourceFixture::verifyContract();
        WebGpuOriginalTextureReadbackDestinationFixture::verifyContract();
        WebGpuCompletedTextureReadbackSourceFixture::verifyContract();
        WebGpuCompletedTextureReadbackDestinationFixture::verifyContract();
        WebGpuOriginalTextureReadbackPairFixture::verifyContract();
        WebGpuOriginalTextureReadbackProjectionFixture::verifyContract();
        WebGpuOriginalTextureReadbackInputsFixture::verifyContract();
        WebGpuOriginalTextureReadbackDeclarationFixture::verifyContract();
        WebGpuOriginalTextureReadbackStateIntentFixture::verifyContract();
        WebGpuOriginalReadbackStateTransactionFixture::verifyContract();
        WebGpuOriginalTextureReadbackCopiesFixture::verifyContract();
        WebGpuOriginalTextureReadbackRecordingFixture::verifyContract();
        WebGpuOriginalTextureReadbackCompletionFixture::verifyContract();
        WebGpuOriginalTextureReadbackMappingFixture::verifyContract();
        WebGpuOriginalTextureReadbackNativeMappingFixture::verifyContract();
        TextureUploadPackingFixture::verify();
        TextureUploadContentTransactionFixture::verify();
        using namespace rhi;
        using namespace rhi_webgpu;
        static_assert(std::is_trivially_copyable_v<TransferCopyArguments>);
        static_assert(!std::is_default_constructible_v<TransferCopyArguments>);
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        BufferCopyDesc buffer{{1u}, {2u}, 4u, 8u, 12u};
        verifyValues(
            buffer,
            [](auto& d) {
                ++d.sourceBuffer.value;
            },
            [](auto& d) {
                ++d.destinationBuffer.value;
            },
            [](auto& d) {
                ++d.sourceOffsetBytes;
            },
            [](auto& d) {
                ++d.destinationOffsetBytes;
            },
            [](auto& d) {
                ++d.sizeBytes;
            });
        BufferToTextureCopyDesc upload;
        upload.sourceBuffer = {1u};
        upload.destinationTexture = {3u};
        upload.bufferOffsetBytes = 12u;
        upload.textureSubresources = {2u, 1u, 3u, 4u, TextureAspect::Color};
        upload.extent = {6u, 7u, 1u};
        upload.bytesPerRow = 256u;
        upload.rowsPerImage = 8u;
        verifyValues(
            upload,
            [](auto& d) {
                ++d.sourceBuffer.value;
            },
            [](auto& d) {
                ++d.destinationTexture.value;
            },
            [](auto& d) {
                ++d.bufferOffsetBytes;
            },
            [](auto& d) {
                ++d.bytesPerRow;
            },
            [](auto& d) {
                ++d.rowsPerImage;
            });
        verifyTextureFields(upload, [](auto& d) -> auto& {
            return d.textureSubresources;
        });
        TextureToBufferCopyDesc download;
        download.sourceTexture = {3u};
        download.destinationBuffer = {2u};
        download.bufferOffsetBytes = 16u;
        download.textureSubresources = upload.textureSubresources;
        download.extent = upload.extent;
        download.bytesPerRow = 512u;
        download.rowsPerImage = 16u;
        verifyValues(
            download,
            [](auto& d) {
                ++d.sourceTexture.value;
            },
            [](auto& d) {
                ++d.destinationBuffer.value;
            },
            [](auto& d) {
                ++d.bufferOffsetBytes;
            },
            [](auto& d) {
                ++d.bytesPerRow;
            },
            [](auto& d) {
                ++d.rowsPerImage;
            });
        verifyTextureFields(download, [](auto& d) -> auto& {
            return d.textureSubresources;
        });
        TextureToTextureCopyDesc image;
        image.sourceTexture = {3u};
        image.destinationTexture = {4u};
        image.extent = upload.extent;
        image.sourceSubresources = upload.textureSubresources;
        image.destinationSubresources = download.textureSubresources;
        verifyValues(
            image,
            [](auto& d) {
                ++d.sourceTexture.value;
            },
            [](auto& d) {
                ++d.destinationTexture.value;
            });
        verifyTextureFields(image, [](auto& d) -> auto& {
            return d.sourceSubresources;
        });
        verifyTextureFields(image, [](auto& d) -> auto& {
            return d.destinationSubresources;
        });
        assert(!TransferCopyArguments::capture(buffer)->matches(upload));
        assert(core::defaultCpuAllocator().stats().allocationCount == before);
        verifyUnsupportedLayout(upload);
        verifyUnsupportedLayout(download);
        verifyUnsupportedLayout(image);

        using Plan = WebGpuFrozenTransferArguments;
        WebGpuFrozenTransferArgumentsBudget budget;
        assert(std::string_view(Plan::validateBudget(budget))
            == "RHI-WEBGPU-TRANSFER-ARGUMENTS-VERSION");
        budget.contractVersion = 1u;
        assert(std::string_view(Plan::validateBudget(budget))
            == "RHI-WEBGPU-TRANSFER-ARGUMENTS-OPT-IN");
        budget.prototypeOptIn = true;
        assert(std::string_view(Plan::validateBudget(budget))
            == "RHI-WEBGPU-TRANSFER-ARGUMENTS-BUDGET");
        auto graphQuota = core::makeShared<core::CpuMemoryQuota>(16384u);
        auto registry = core::makeShared<WebGpuRhiResourceRegistry>();
        const char* diagnostic = nullptr;
        WebGpuOrdinaryTransferBudget transferBudget{1u,
            true,
            {kCommandRetentionBudgetContractVersion,
                true,
                0u,
                0u,
                0u,
                0u,
                graphQuota->limitBytes()},
            0u,
            0u};
        auto transfer =
            WebGpuOrdinaryTransferRetention::shareOriginal(WebGpuOrdinaryTransferRetention::prepare(
                *registry, nullptr, 0u, transferBudget, graphQuota, diagnostic));
        assert(transfer);
        WebGpuGenericCommandProjection::Sources sources;
        sources.transfer = transfer;
        auto projection = WebGpuGenericCommandProjection::shareOriginal(
            WebGpuGenericCommandProjection::prepare(std::move(sources),
                nullptr,
                0u,
                {1u, true, 1u, 0u, 0u, 0u, 0u, 0u, {}, graphQuota->limitBytes(), 0u},
                graphQuota,
                diagnostic));
        assert(projection);
        const CommandUsage finish{CommandUsageKind::Finish};
        auto usage = WebGpuGenericCommandUsage::prepareOriginal(projection,
            &finish,
            1u,
            {1u, true, 1u, 0u, graphQuota->limitBytes()},
            graphQuota,
            diagnostic);
        assert(usage);
        auto quota = core::makeShared<core::CpuMemoryQuota>(16384u);
        budget = {1u, true, 1u, quota->limitBytes()};
        auto input = TransferCopyArguments::finish();
        const auto factory = [&] {
            return Plan::prepareOriginal(usage, &input, 1u, budget, quota, diagnostic);
        };
        CpuQuotaFactoryFixture admission(quota);
        const auto peak = admission.measure(factory);
        admission.verify(peak, factory);
        assert(!quota->liveBytes());
        const auto expect = [&](const char* id) {
            assert(!factory() && std::string_view(diagnostic) == id && !quota->liveBytes());
        };
        --budget.maxCpuMetadataBytes;
        expect("RHI-WEBGPU-TRANSFER-ARGUMENTS-QUOTA");
        ++budget.maxCpuMetadataBytes;
        budget.maxCommands = UINT32_MAX;
        expect("RHI-WEBGPU-TRANSFER-ARGUMENTS-ALLOCATION");
        budget.maxCommands = 1u;
        auto canonical = usage;
        usage = std::shared_ptr<const WebGpuGenericCommandUsage>(quota, usage.get());
        expect("RHI-WEBGPU-TRANSFER-ARGUMENTS-ORIGINAL-USAGE");
        usage = canonical;
        assert(!Plan::prepareOriginal(usage, nullptr, 1u, budget, quota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-TRANSFER-ARGUMENTS-CAPACITY");
        auto plan = factory();
        assert(plan && !plan->validateAgainst(*transfer));
        Plan::Consumption consumer(plan), foreign(plan);
        auto alias = std::shared_ptr<const Plan>(quota, plan.get());
        Plan::Consumption forged(alias), missing({});
        assert(
            std::string_view(forged.validate()) == "RHI-WEBGPU-TRANSFER-ARGUMENTS-ORIGINAL-PLAN");
        assert(
            std::string_view(missing.validate()) == "RHI-WEBGPU-TRANSFER-ARGUMENTS-ORIGINAL-PLAN");
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        const auto ticket = consumer.preflightFinish(diagnostic);
        assert(ticket && !diagnostic);
        assert(!foreign.commit(ticket, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-COMMAND-USAGE-TICKET");
        assert(consumer.commit(ticket, diagnostic) && consumer.complete());
        assert(!consumer.commit(ticket, diagnostic));
        assert(
            std::string_view(plan->executionDiagnostic()) == webGpuGenericRecordingUsageDiagnostic);
        foreign.abort();
        assert(!foreign.preflightFinish(diagnostic));
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    }

private:
    template <class Desc, class... Mutate>
    static void verifyValues(const Desc& input, Mutate... mutate)
    {
        const auto snapshot = rhi::TransferCopyArguments::capture(input);
        assert(snapshot && snapshot->matches(input));
        Desc copied;
        assert(snapshot->write(copied) && snapshot->matches(copied));
        const auto check = [&](auto change) {
            auto changed = input;
            change(changed);
            assert(!snapshot->matches(changed));
        };
        (check(mutate), ...);
    }

    template <class Desc, class Range>
    static void verifyTextureFields(const Desc& input, Range range)
    {
        verifyValues(
            input,
            [&](auto& d) {
                ++range(d).baseMipLevel;
            },
            [&](auto& d) {
                ++range(d).mipLevelCount;
            },
            [&](auto& d) {
                ++range(d).baseArrayLayer;
            },
            [&](auto& d) {
                ++range(d).arrayLayerCount;
            },
            [&](auto& d) {
                range(d).aspect = rhi::TextureAspect::Depth;
            },
            [](auto& d) {
                ++d.extent.width;
            },
            [](auto& d) {
                ++d.extent.height;
            },
            [](auto& d) {
                ++d.extent.depth;
            });
    }

    template <class Desc> static void verifyUnsupportedLayout(Desc& d)
    {
        d.mipLayouts.emplace_back();
        assert(!rhi::TransferCopyArguments::capture(d));
        d.mipLayouts.clear();
        const auto a = rhi::TransferCopyArguments::capture(d);
        assert(a);
        d.mipLayouts.emplace_back();
        assert(!a->write(d) && d.mipLayouts.size() == 1u);
    }
};
} // namespace arp::test
