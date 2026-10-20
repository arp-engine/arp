#pragma once

#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/command_transfer_validation.h"
#include "rhi/semantics/command_lowering.h"

#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalCommandOrderedUploadContractFixture final {
    using Contract = rhi::OriginalCommandOrderedUploadContract;

    static void is(const char* id, const char* expected)
    {
        assert(id && std::string_view(id) == expected);
    }

public:
    static void verifyContract()
    {
        Contract::Budget b{1u, true, 0u, 1u, 0u};
        assert(!Contract::validateIntent(b));
        auto bad = b;
        bad.contractVersion = 0u;
        is(Contract::validateIntent(bad), "RHI-ORIGINAL-ORDERED-UPLOAD-VERSION");
        bad = b;
        bad.prototypeOptIn = false;
        is(Contract::validateIntent(bad), "RHI-ORIGINAL-ORDERED-UPLOAD-OPT-IN");
        bad = b;
        bad.maxNativeCopyCommands = 0u;
        is(Contract::validateIntent(bad), "RHI-ORIGINAL-ORDERED-UPLOAD-BUDGET");
        using K = rhi::CommandUsageKind;
        for (auto kind : {K::BufferUpload,
                 K::TextureUpload,
                 K::BufferBarrier,
                 K::TextureBarrier,
                 K::BufferCopy,
                 K::Finish})
            assert(Contract::supports(kind));
        for (auto kind :
            {K::TextureReadback, K::Draw, K::Dispatch, K::PushDebugGroup, K::Timestamp})
            assert(!Contract::supports(kind));
        for (auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision =
                arp::rhi::semantics::OriginalCommandOrderedUploadLowering::lowering(backend);
            assert(decision.contractVersion == 1u && decision.diagnostic
                && !decision.nativeProjectionAvailable && !decision.recordingAvailable
                && !decision.executionAvailable);
        }
        rhi::ResourceBarrierDesc transition;
        transition.resource = {1u};
        transition.before = rhi::ResourceState::CopyDst;
        transition.after = rhi::ResourceState::CopySrc;
        transition.sourceStages = transition.destinationStages = rhi::PipelineStage::Transfer;
        transition.sourceAccess = rhi::ResourceAccess::TransferWrite;
        transition.destinationAccess = rhi::ResourceAccess::TransferRead;
        assert(!Contract::bufferTransition(transition));
        transition.destinationAccess = rhi::ResourceAccess::ShaderRead;
        assert(Contract::bufferTransition(transition));
        using Join = rhi::OriginalCommandUploadJoinContract;
        rhi::OriginalCommandUploadJoinBudget join{1u, true, 2u, 1u, 1u, 1u, 16u, 16u, 65536u};
        assert(!Join::validateIntent(join));
        auto invalid = join;
        invalid.contractVersion = 0u;
        is(Join::validateIntent(invalid), "RHI-ORIGINAL-UPLOAD-JOIN-VERSION");
        invalid = join;
        invalid.prototypeOptIn = false;
        is(Join::validateIntent(invalid), "RHI-ORIGINAL-UPLOAD-JOIN-OPT-IN");
        invalid = join;
        invalid.maxNativeSources = 0u;
        is(Join::validateIntent(invalid), "RHI-ORIGINAL-UPLOAD-JOIN-BUDGET");
        for (auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision =
                arp::rhi::semantics::OriginalCommandUploadJoinLowering::lowering(backend);
            assert(decision.diagnostic
                && decision.privateJoinAvailable == (backend == rhi::Backend::WebGPU));
        }
        std::puts(
            "BINDLESS_RHI_ORDERED_UPLOAD_PROFILE_CONTRACT_OK version1 explicit-opt-in complete-positional-stream independent-native-work four-native-gates-closed no-readback-draw-dispatch-debug");
    }

    static void verifyInputs(const std::shared_ptr<const rhi::OriginalCommandInputs>& inputs,
        const rhi::OriginalCommandStreamBudget& stream,
        const rhi::OriginalCommandUploadStagingContract::Requirements& expected)
    {
        const rhi::OriginalCommandUploadStagingContract::Budget staging{
            1u, true, expected.bufferBytes, expected.packedTextureBytes, expected.totalBytes};
        const auto copyCount = expected.bufferUploads + expected.textureMips;
        Contract::Budget budget{
            1u, true, expected.textureMips ? 2u : 0u, copyCount, expected.textureMips ? 40u : 0u};
        const auto inspect = [&](const auto& b) {
            return Contract::inspect(inputs, stream, staging, b);
        };
        const auto result = inspect(budget);
        if (result.diagnostic) {
            // The older pure-input Null fixture deliberately creates Undefined
            // intent, which is outside exclusive canonical content creation.
            is(result.diagnostic, "RHI-TEXTURE-DESTINATION-SELECTED-PROFILE");
            assert(!result.required.nativeCopyCommands);
            std::puts("BINDLESS_RHI_ORDERED_UPLOAD_PROFILE_UNISSUED_TARGET_REFUSED");
            return;
        }
        assert(result.required.staging.matches(expected)
            && result.required.nativeCopyCommands == copyCount
            && result.required.textureUploadCommands == budget.maxTextureUploadCommands
            && result.required.textureWriteTexelBytes == budget.maxTextureWriteTexelBytes);
        auto bad = budget;
        --bad.maxNativeCopyCommands;
        is(inspect(bad).diagnostic, "RHI-ORIGINAL-ORDERED-UPLOAD-NATIVE-COPY-CAPACITY");
        assert(
            !inspect(bad).required.nativeCopyCommands && !inspect(bad).required.staging.totalBytes);
        if (budget.maxTextureUploadCommands) {
            bad = budget;
            --bad.maxTextureUploadCommands;
            is(inspect(bad).diagnostic, "RHI-ORIGINAL-ORDERED-UPLOAD-TEXTURE-COMMAND-CAPACITY");
            bad = budget;
            --bad.maxTextureWriteTexelBytes;
            is(inspect(bad).diagnostic, "RHI-ORIGINAL-ORDERED-UPLOAD-TEXEL-WORK-CAPACITY");
        }
        const auto& source = inputs->originalScalars()->originalUsage()->originalSources();
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{inputs->originalMetadataQuota(), source->originalMetadataQuota()},
            [&]() noexcept -> const char* {
                const auto accepted = inspect(budget);
                assert(!accepted.diagnostic && accepted.required.staging.matches(expected));
                return nullptr;
            }));
        std::printf(
            "BINDLESS_RHI_ORDERED_UPLOAD_PROFILE_INPUT_OK %s original-commands%llu native-copies%llu destination-write-texels%llu exact-less bounded-staging no-partial-requirements hot-allocation0 gates4-closed\n",
            expected.bufferUploads ? "buffer" : "texture",
            static_cast<unsigned long long>(stream.maxCommands),
            static_cast<unsigned long long>(copyCount),
            static_cast<unsigned long long>(budget.maxTextureWriteTexelBytes));
    }
};
} // namespace arp::test
