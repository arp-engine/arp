#pragma once

#include "webgpu_canonical_texture_upload_source_fixture.h"

#include <tuple>

namespace arp::test {
// Shared original initializer; both Compute and Graphics retain the same real
// upload root/issuer/content receipt. No shader or render work is performed.
class OriginalCompletedSampledInputFixture final {
    static constexpr std::uint64_t bytes = 65536u;

    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "completed sampled initializer: %s\n", id);
        assert(!id);
    }

public:
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
    using Context = WebGpuCanonicalTextureUploadSourceFixture::BorrowedContext;
    using Quotas = std::array<std::shared_ptr<core::CpuMemoryQuota>, 18u>;

    static auto prepare(Context& context, Quotas& quotas)
    {
        for (std::size_t i = 0u; i < context.quotas.size(); ++i)
            quotas[i] = context.quotas[i];
        quotas[14] = context.contentQuota;
        for (std::size_t i = 15u; i < quotas.size(); ++i)
            quotas[i] = core::makeShared<core::CpuMemoryQuota>(bytes);
        auto& device = context.gpu();
        const char* id = nullptr;
        using Content = rhi_webgpu::WebGpuCanonicalTextureContentUploadSource;
        using Issuer = rhi_webgpu::WebGpuCanonicalIssuerPreparation;
        auto staging = context.make(id);
        require(id);
        assert(staging);
        require(rhi_webgpu::WebGpuCanonicalBufferUploadSource::initialize(context.buffer));
        assert(WebGpuAsyncWaitFixture::until([&] {
            return context.buffer->callbacksDrained();
        }));
        auto parent = context.binding->originalDestinationLease();
        auto before = parent->originalObject()->observeOriginalContent();
        assert(before);
        auto content = Content::prepareOriginal(device,
            staging,
            *before,
            {1u, true, Content::retainedOwnerBound, bytes},
            quotas[15],
            id);
        require(id);
        assert(content);
        auto inputs = OriginalCommandTextureUploadInputsFixture::prepareForSource(
            context.binding->originalSource(),
            id,
            context.buffer->originalLease(),
            2u,
            context.buffer->originalLease()->originalObject()->desc().sizeBytes,
            true);
        require(id);
        assert(inputs);
        const auto& desc = parent->originalObject()->desc();
        const auto cells = std::uint64_t(desc.mipLevelCount) * desc.arrayLayerCount;
        // Two whole-upload observations and one staging-buffer observation.
        // Each upload visits every mip/layer content cell in the original graph.
        const rhi::OriginalCommandStateSequenceBudget sequence{
            1u, true, 5u, 0u, 1u + 2u * cells, 2u * cells};
        if (desc.arrayLayerCount > 1u) {
            auto oneLess = sequence;
            --oneLess.maxStateCells;
            assert(!WebGpuCanonicalUploadJoinsFixture::prepareRoot(
                inputs, device, quotas[16], quotas[17], true, 2u, 3u, 2u, oneLess, id));
            assert(id && std::string_view(id) == "RHI-ORIGINAL-SEQUENCE-CELL-CAPACITY"
                && !quotas[16]->liveBytes() && !quotas[17]->liveBytes());
        }
        auto root = WebGpuCanonicalUploadJoinsFixture::prepareRoot(
            inputs, device, quotas[16], quotas[17], true, 2u, 3u, 2u, sequence, id);
        require(id);
        assert(root);
        std::shared_ptr<const Issuer> issuer;
        {
            auto joinBudget = WebGpuCanonicalUploadJoinsFixture::budget(*root, true);
            joinBudget.maxStagingResourceBytes =
                context.buffer->originalLease()->originalObject()->desc().sizeBytes;
            joinBudget.maxDestinationResourceBytes =
                parent->originalObject()->allocation().estimatedSizeBytes;
            const std::array<rhi_webgpu::WebGpuCanonicalUploadJoins::Input, 2> entries{
                {{1u, content}, {2u, content}}};
            WebGpuCanonicalUploadProjectionFixture::Prepared prepared(root,
                device,
                entries,
                *context.loss,
                true,
                2u,
                joinBudget.maxStagingResourceBytes + joinBudget.maxDestinationResourceBytes,
                joinBudget,
                5u,
                0u);
            auto lowering = rhi_webgpu::WebGpuCanonicalCopyPassLowering::prepareOriginal(
                prepared.custody, prepared.lowerBudget, id);
            require(id);
            assert(lowering);
            issuer = Issuer::prepareOriginal(
                lowering, {1u, true, 1u, 1u, prepared.ownerLimit, bytes, bytes}, id);
            require(id);
            assert(issuer);
            require(Issuer::recordOriginal(issuer, {1u, true}));
            assert(WebGpuAsyncWaitFixture::until([&] {
                return issuer->hasOriginalRecordedCommand()
                    || root->status() != rhi::OriginalCommandStateTransaction::Status::Pending;
            }));
            require(issuer->recordingDiagnostic());
            assert(issuer->hasOriginalRecordedCommand());
            require(Issuer::submitOriginal(issuer, {1u, true}));
            assert(WebGpuAsyncWaitFixture::until([&] {
                return !prepared.custody->pendingOriginalRecords();
            }));
            assert(issuer->hasOriginalCompletedSubmission()
                && root->status() == rhi::OriginalCommandStateTransaction::Status::Committed
                && root->journalEmpty());
        }
        auto completed = parent->originalObject()->observeOriginalContent();
        assert(completed);
        if (desc.arrayLayerCount == 1u && !desc.cubeCompatible) {
            require(Issuer::validateOriginalCompletedShaderTextureInput(issuer,
                content,
                *completed,
                {1u,
                    true,
                    2u,
                    desc.mipLevelCount,
                    context.binding->originalInput()->pixelBytes().size(),
                    5u},
                root));
            if (desc.extent.width != 2u || desc.extent.height != 2u || desc.mipLevelCount != 2u)
                std::printf(
                    "BINDLESS_RHI_GENERAL_AUTHORED_2D_INITIALIZER_OK width%u height%u mips%u bytes%llu genuine-completion committed-root\n",
                    desc.extent.width,
                    desc.extent.height,
                    desc.mipLevelCount,
                    static_cast<unsigned long long>(
                        context.binding->originalInput()->pixelBytes().size()));
            else if (desc.format == rhi::TextureFormat::Rgba8Unorm)
                std::puts(
                    "BINDLESS_RHI_GENERAL_SAMPLED_INITIALIZER_OK same-device upload20 mips2 original-scopes-drained recorded-submit-completed committed-root");
            else {
                assert(desc.format == rhi::TextureFormat::Rgba16Float);
                std::puts(
                    "BINDLESS_RHI_GENERAL_HDR_INITIALIZER_OK same-device upload40 mips2 original-scopes-drained recorded-submit-completed committed-root");
            }
        } else {
            // The old private Compute input profile remains 2D-only. Array
            // publication authenticates this genuine completed upload instead.
            const auto* rejected = Issuer::validateOriginalCompletedShaderTextureInput(issuer,
                content,
                *completed,
                {1u,
                    true,
                    2u,
                    desc.mipLevelCount,
                    context.binding->originalInput()->pixelBytes().size(),
                    5u},
                root);
            assert(
                rejected && std::string_view(rejected) == "RHI-COMPLETED-SHADER-TEXTURE-PROFILE");
            assert(completed->controlVersion() == 1u && completed->contentGeneration() == 2u);
            if (desc.extent.width != 2u || desc.extent.height != 2u || desc.mipLevelCount != 2u)
                std::printf(
                    "BINDLESS_RHI_GENERAL_AUTHORED_ARRAY_INITIALIZER_OK width%u height%u mips%u layers%u bytes%llu genuine-completion committed-root legacy-compute-refused\n",
                    desc.extent.width,
                    desc.extent.height,
                    desc.mipLevelCount,
                    desc.arrayLayerCount,
                    static_cast<unsigned long long>(
                        context.binding->originalInput()->pixelBytes().size()));
            else
                std::printf(
                    "BINDLESS_RHI_GENERAL_ARRAY_INITIALIZER_OK layers%u upload%llu mips2 genuine-completion committed-root legacy-compute-refused\n",
                    parent->originalObject()->desc().arrayLayerCount,
                    static_cast<unsigned long long>(
                        context.binding->originalInput()->pixelBytes().size()));
        }
        return std::tuple{staging, content, inputs, root, issuer, completed};
    }
#endif
};
} // namespace arp::test
