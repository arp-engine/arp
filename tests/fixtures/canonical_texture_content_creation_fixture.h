#pragma once
#include "original_command_texture_upload_inputs_fixture.h"
#include "original_texture_content_cell_sequence_fixture.h"
#include "original_texture_content_cells_plan_fixture.h"
#include "original_undefined_texture_upload_plan_fixture.h"
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "rhi/frontend/resource_texture_validation.h"

namespace arp::test {
class CanonicalTextureContentCreationFixture final {
    static constexpr std::uint64_t bytes = 65536u;

    static void is(const char* actual, const char* expected)
    {
        if (!actual || std::string_view(actual) != expected)
            std::fprintf(stderr,
                "content creation actual=%s expected=%s\n",
                actual ? actual : "<none>",
                expected);
        assert(actual && std::string_view(actual) == expected);
    }

    static rhi::TextureDesc descriptor()
    {
        rhi::TextureDesc desc;
        desc.extent = {2u, 2u, 1u};
        desc.format = rhi::TextureFormat::Rgba8Unorm;
        desc.mipLevelCount = 2u;
        desc.usage = rhi::TextureUsage::CopyDst;
        desc.initialState = rhi::ResourceState::CopyDst;
        return desc;
    }

    static rhi::OriginalTextureContentCreationBudget budget()
    {
        return {1u, true, 1u, 1u, 20u, 4u, bytes};
    }

    static rhi::TextureDesc rangeDescriptor()
    {
        rhi::TextureDesc desc;
        desc.extent = {8u, 8u, 1u};
        desc.format = rhi::TextureFormat::D32Float;
        desc.mipLevelCount = desc.arrayLayerCount = 3u;
        desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::RenderAttachment;
        desc.initialState = rhi::ResourceState::Undefined;
        return desc;
    }

    static rhi::OriginalTextureContentCreationBudget rangeBudget()
    {
        return {2u, true, 1u, 1u, 1008u, 4u, bytes, 9u};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    class Context final {
    public:
        template <class Factory> explicit Context(Factory&& create) : device(create())
        {
            assert(device);
            for (auto& q : quotas)
                q = core::makeShared<core::CpuMemoryQuota>(bytes);
            assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, quotas[0]));
            assert(!device->prepareOriginalDeviceNativeLoss(
                {{1u, true, 1u, 1u, bytes}, 1u}, quotas[1]));
            assert(!device->prepareOriginalResourceRegistration(
                {1u, true, 1u, 4u, 2u, 17u, bytes}, quotas[2]));
        }

        auto make(const char*& diagnostic, std::shared_ptr<core::CpuMemoryQuota> quota = {})
        {
            core::CpuMemoryQuotaScope resource(quotas[3]);
            return device->createOriginalContentTexture(
                descriptor(), budget(), quota ? quota : quotas[4], diagnostic);
        }

        auto makeRange(const char*& diagnostic, std::shared_ptr<core::CpuMemoryQuota> quota = {})
        {
            core::CpuMemoryQuotaScope resource(quotas[3]);
            return device->createOriginalContentTexture(
                rangeDescriptor(), rangeBudget(), quota ? quota : quotas[4], diagnostic);
        }

        std::array<std::shared_ptr<core::CpuMemoryQuota>, 5> quotas;
        core::UniquePtr<rhi::GpuDevice> device;
    };

    static inline core::UniquePtr<Context> lossContext_;
    static inline core::UniquePtr<rhi::GpuTexture> lossTexture_;
    static inline std::optional<rhi::GpuTexture::ContentExpectation> lossExpectation_;
    static inline core::UniquePtr<rhi::GpuTexture> lossRangeTexture_;
    static inline std::optional<rhi::GpuTexture::ContentRangeExpectation> lossRangeExpectation_;
#endif
public:
    template <class Factory> static void verifyContract(Factory&& createNull)
    {
        using Contract = rhi::OriginalTextureContentCreationContract;
        std::uint64_t output = 77u;
        auto b = budget();
        const auto desc = descriptor();
        assert(!Contract::inspect(desc, b, output) && output == 20u);
        --b.maxTexelBytes;
        is(Contract::inspect(desc, b, output), "RHI-CANONICAL-CONTENT-CREATION-BYTE-CAPACITY");
        assert(!output);
        b = budget();
        b.prototypeOptIn = false;
        is(Contract::inspect(desc, b, output), "RHI-CANONICAL-CONTENT-CREATION-OPT-IN");
        b = budget();
        b.contractVersion = 0u;
        is(Contract::inspect(desc, b, output), "RHI-CANONICAL-CONTENT-CREATION-VERSION");
        b = budget();
        b.maxUploadGenerations = 0u;
        is(Contract::inspect(desc, b, output), "RHI-CANONICAL-CONTENT-CREATION-BUDGET");
        auto invalid = desc;
        invalid.initialState = rhi::ResourceState::Undefined;
        assert(Contract::inspect(invalid, budget(), output));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto lowering = Contract::lowering(backend);
            assert(lowering.contractVersion == 1u && lowering.diagnostic
                && !lowering.nativeContentExecutionAvailable
                && lowering.exclusiveCreationAvailable == (backend == rhi::Backend::WebGPU));
            const auto staging = rhi::OriginalTextureContentStagingContract::lowering(backend);
            assert(staging.diagnostic
                && staging.prerequisiteAvailable == (backend == rhi::Backend::WebGPU));
        }
        auto device = createNull();
        const char* diagnostic = nullptr;
        auto quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!device->createOriginalContentTexture(desc, budget(), quota, diagnostic));
        is(diagnostic, "RHI-CANONICAL-CONTENT-CREATION-NULL-VALIDATOR-ONLY");
        assert(!quota->liveBytes()
            && allocations == core::defaultCpuAllocator().stats().allocationCount);
        {
            std::array<std::uint8_t, 20> pixels{};
            rhi::UploadTextureDesc upload;
            upload.destinationTexture = {1u};
            upload.extent = desc.extent;
            upload.format = desc.format;
            upload.bytesPerRow = 8u;
            upload.rowsPerImage = 2u;
            upload.pixelData = pixels.data();
            upload.pixelDataSize = pixels.size();
            upload.subresources.mipLevelCount = 2u;
            upload.mipLayouts = {{0u, {2u, 2u, 1u}, 8u, 2u, 0u}, {1u, {1u, 1u, 1u}, 4u, 1u, 16u}};
            const auto make = [&] {
                return rhi::TextureUploadInputSnapshot::prepareOriginal(upload,
                    {1u, true, 2u, 0u, 20u, bytes, bytes, bytes},
                    quota,
                    quota,
                    quota,
                    diagnostic);
            };
            auto full = make();
            assert(full
                && !rhi::semantics::OriginalTextureContentUploadValidation::inspect(*full, desc));
            full.reset();
            upload.subresources.mipLevelCount = 1u;
            upload.mipLayouts.pop_back();
            auto partial = make();
            assert(partial);
            is(rhi::semantics::OriginalTextureContentUploadValidation::inspect(*partial, desc),
                "RHI-CANONICAL-CONTENT-UPLOAD-WHOLE-RANGE");
        }
        assert(!quota->liveBytes());
        OriginalUndefinedTextureUploadPlanFixture::verifyContract();
        verifyNativeCreationContract(*device, quota);
        verifyRangeContract();
        verifyCompletedShaderInputContract();
        verifyCompletedReadPlanContract();
        OriginalTextureContentCellSequenceFixture::verify();
        verifyNativeAttachmentCreationContract(*device, quota);
        std::puts(
            "BINDLESS_RHI_CANONICAL_CONTENT_CREATION_CONTRACT_OK explicit-opt-in exact20 four-lowering creation-WebGPU-Prototype completion-all-closed Null-refused allocation0");
    }

    static void verifyNativeAttachmentCreationContract(
        rhi::GpuDevice& nullDevice, const std::shared_ptr<core::CpuMemoryQuota>& quota)
    {
        using Contract = rhi::OriginalTextureContentCreationContract;
        auto desc = rangeDescriptor();
        auto b = rangeBudget();
        b.contractVersion = 4u;
        b.maxNativeAllocationBytes = 4096u;
        std::uint64_t texels = 77u;
        const char* id = nullptr;
        assert(!Contract::inspect(desc, b, texels) && texels == 1008u);
        for (const auto field : {&rhi::OriginalTextureContentCreationBudget::maxContentCells,
                 &rhi::OriginalTextureContentCreationBudget::maxTexelBytes,
                 &rhi::OriginalTextureContentCreationBudget::maxNativeAllocationBytes}) {
            auto bad = b;
            bad.*field = field == &rhi::OriginalTextureContentCreationBudget::maxContentCells ? 8u
                : field == &rhi::OriginalTextureContentCreationBudget::maxTexelBytes ? 1007u
                                                                                     : 0u;
            assert(Contract::inspect(desc, bad, texels) && !texels);
        }
        auto legacy = b;
        legacy.contractVersion = 2u;
        is(Contract::inspect(desc, legacy, texels), "RHI-CANONICAL-CONTENT-CREATION-NATIVE-BUDGET");
        auto upload = b;
        upload.contractVersion = 3u;
        upload.maxContentCells = 0u;
        assert(Contract::inspect(desc, upload, texels) && !texels);
        for (const auto format : {rhi::TextureFormat::Rgba8Unorm,
                 rhi::TextureFormat::Rgba16Float,
                 rhi::TextureFormat::D32Float,
                 rhi::TextureFormat::Depth24Stencil8}) {
            auto authored = desc;
            authored.format = format;
            std::uint64_t footprint = 0u;
            assert(!rhi::semantics::TextureUploadDestinationValidation::footprint(
                authored, footprint));
            auto exact = b;
            exact.maxContentCells = 18u;
            exact.maxTexelBytes = footprint;
            assert(!Contract::inspect(authored, exact, texels) && texels == footprint);
        }
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d = Contract::lowering(backend, 4u);
            assert(d.contractVersion == 4u && d.diagnostic && !d.nativeContentExecutionAvailable
                && d.exclusiveCreationAvailable
                    == (backend == rhi::Backend::Vulkan || backend == rhi::Backend::Metal));
            assert(!Contract::lowering(backend, 8u).exclusiveCreationAvailable);
        }
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!nullDevice.queryOriginalContentTextureAllocation(desc, id, 4u));
        is(id, "RHI-CANONICAL-CONTENT-CREATION-NULL-VALIDATOR-ONLY");
        assert(!nullDevice.createOriginalContentTexture(desc, b, quota, id));
        is(id, "RHI-CANONICAL-CONTENT-CREATION-NULL-VALIDATOR-ONLY");
        assert(!nullDevice.queryOriginalContentTextureAllocation(desc, id, 2u));
        is(id, "RHI-CANONICAL-CONTENT-CREATION-NATIVE-QUERY-PROFILE");
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    }

    static void verifyNativeCreationContract(
        rhi::GpuDevice& nullDevice, const std::shared_ptr<core::CpuMemoryQuota>& quota)
    {
        using Contract = rhi::OriginalTextureContentCreationContract;
        auto desc = descriptor();
        desc.initialState = rhi::ResourceState::Undefined;
        auto b = budget();
        b.contractVersion = 3u;
        b.maxNativeAllocationBytes = 20u;
        std::uint64_t texels = 77u;
        const char* id = nullptr;
        assert(!Contract::inspect(desc, b, texels) && texels == 20u);
        auto invalid = desc;
        invalid.initialState = rhi::ResourceState::CopyDst;
        is(Contract::inspect(invalid, b, texels), "RHI-TEXTURE-DESTINATION-SELECTED-PROFILE");
        assert(!texels);
        assert(rhi::semantics::TextureUploadDestinationValidation::validate(desc, texels));
        auto bad = b;
        bad.maxNativeAllocationBytes = 0u;
        is(Contract::inspect(desc, bad, texels), "RHI-CANONICAL-CONTENT-CREATION-NATIVE-BUDGET");
        bad = b;
        bad.maxContentCells = 1u;
        is(Contract::inspect(desc, bad, texels), "RHI-CANONICAL-CONTENT-CREATION-CELL-BUDGET");
        bad = budget();
        bad.maxNativeAllocationBytes = 20u;
        is(Contract::inspect(descriptor(), bad, texels),
            "RHI-CANONICAL-CONTENT-CREATION-NATIVE-BUDGET");
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d = Contract::lowering(backend, 3u);
            assert(d.contractVersion == 3u && d.diagnostic && !d.nativeContentExecutionAvailable
                && d.exclusiveCreationAvailable
                    == (backend == rhi::Backend::Vulkan || backend == rhi::Backend::Metal));
            assert(d.disposition
                == (backend == rhi::Backend::Null ? Contract::Disposition::ValidatorOnly
                        : backend == rhi::Backend::Vulkan || backend == rhi::Backend::Metal
                        ? Contract::Disposition::Prototype
                        : Contract::Disposition::DiagnosticFailure));
        }
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!nullDevice.queryOriginalContentTextureAllocation(desc, id));
        is(id, "RHI-CANONICAL-CONTENT-CREATION-NULL-VALIDATOR-ONLY");
        assert(!nullDevice.createOriginalContentTexture(desc, b, quota, id));
        is(id, "RHI-CANONICAL-CONTENT-CREATION-NULL-VALIDATOR-ONLY");
        assert(!quota->liveBytes()
            && allocations == core::defaultCpuAllocator().stats().allocationCount);
        assert(!Contract::lowering(rhi::Backend::Vulkan, 5u).exclusiveCreationAvailable);
        std::puts(
            "BINDLESS_RHI_NATIVE_CONTENT_CREATION3_CONTRACT_OK Undefined exact-logical separate-native-budget four-lowering completion-closed Null-no-allocation");
    }

    static void verifyCompletedShaderInputContract()
    {
        using Contract = rhi::OriginalCompletedShaderTextureInputContract;
        using State = rhi::ResourceState;
        assert(!rhi::OriginalCompletedTextureReadPlanContract::validatePendingProfile(
            1u, true, true, 4u));
        assert(rhi::OriginalCompletedTextureReadPlanContract::validatePendingProfile(
            0u, true, true, 4u));
        assert(rhi::OriginalCompletedTextureReadPlanContract::validatePendingProfile(
            2u, true, true, 4u));
        assert(rhi::OriginalCompletedTextureReadPlanContract::validatePendingProfile(
            1u, false, true, 4u));
        assert(rhi::OriginalCompletedTextureReadPlanContract::validatePendingProfile(
            1u, true, false, 4u));
        for (const auto version : {0u, 1u, 2u, 3u, 5u})
            assert(rhi::OriginalCompletedTextureReadPlanContract::validatePendingProfile(
                1u, true, true, version));
        assert(!rhi::OriginalCompletedTextureReadPlanContract::validatePendingStates(
            State::CopyDst, State::CopyDst, State::ShaderRead, State::ShaderRead, 3u, 4u));
        for (const auto wrong : {State::Undefined, State::CopySrc, State::ShaderWrite}) {
            assert(rhi::OriginalCompletedTextureReadPlanContract::validatePendingStates(
                wrong, State::CopyDst, State::ShaderRead, State::ShaderRead, 3u, 4u));
            assert(rhi::OriginalCompletedTextureReadPlanContract::validatePendingStates(
                State::CopyDst, wrong, State::ShaderRead, State::ShaderRead, 3u, 4u));
            assert(rhi::OriginalCompletedTextureReadPlanContract::validatePendingStates(
                State::CopyDst, State::CopyDst, wrong, State::ShaderRead, 3u, 4u));
            assert(rhi::OriginalCompletedTextureReadPlanContract::validatePendingStates(
                State::CopyDst, State::CopyDst, State::ShaderRead, wrong, 3u, 4u));
        }
        assert(rhi::OriginalCompletedTextureReadPlanContract::validatePendingStates(
            State::CopyDst, State::CopyDst, State::CopyDst, State::ShaderRead, 3u, 4u));
        assert(rhi::OriginalCompletedTextureReadPlanContract::validatePendingStates(
            State::CopyDst, State::CopyDst, State::ShaderRead, State::CopyDst, 3u, 4u));
        for (const auto revision : std::array<std::uint64_t, 4u>{0u, 3u, 5u, UINT64_MAX})
            assert(
                rhi::OriginalCompletedTextureReadPlanContract::validatePendingStates(State::CopyDst,
                    State::CopyDst,
                    State::ShaderRead,
                    State::ShaderRead,
                    3u,
                    revision));
        assert(rhi::OriginalCompletedTextureReadPlanContract::validatePendingStates(
            State::CopyDst, State::CopyDst, State::ShaderRead, State::ShaderRead, UINT64_MAX, 0u));
        assert(rhi::OriginalCompletedTextureReadPlanContract::validatePendingStates(
            State::CopyDst, State::CopyDst, State::ShaderRead, State::ShaderRead, 0u, 1u));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision =
                rhi::semantics::OriginalCompletedShaderTextureInputLowering::lowering(backend);
            assert(decision.privateInputValidationAvailable == (backend == rhi::Backend::WebGPU)
                && !decision.shaderExecutionAvailable && decision.diagnostic);
            assert(
                !rhi::semantics::OriginalCompletedShaderTextureInputLowering::lowering(backend, 2u)
                    .privateInputValidationAvailable);
        }
        const Contract::Budget b{1u, true, 2u, 2u, 20u, 2u};
        auto desc = descriptor();
        desc.usage = desc.usage | rhi::TextureUsage::Sampled;
        const auto inspect = [&](const auto& budget,
                                 std::uint64_t generation = 2u,
                                 std::uint64_t revision = 4u,
                                 bool completed = true) {
            return Contract::inspect(
                desc, budget, 2u, 2u, 20u, 0u, 1u, generation, revision, completed);
        };
        assert(!inspect(b));
        rhi::TextureViewDesc view;
        view.format = desc.format;
        view.usage = rhi::TextureUsage::Sampled;
        view.mipLevelCount = desc.mipLevelCount;
        const auto inspectView = [&](rhi::ResourceState state = rhi::ResourceState::ShaderRead,
                                     rhi::PipelineStage stages = rhi::PipelineStage::ComputeShader,
                                     rhi::ResourceAccess access = rhi::ResourceAccess::ShaderRead) {
            return Contract::inspectViewBinding(desc, view, state, stages, access);
        };
        assert(!inspectView());
        is(inspectView(rhi::ResourceState::CopyDst), "RHI-COMPLETED-SHADER-TEXTURE-VIEW-ACCESS");
        is(inspectView(rhi::ResourceState::ShaderRead, rhi::PipelineStage::None),
            "RHI-COMPLETED-SHADER-TEXTURE-VIEW-ACCESS");
        is(inspectView(rhi::ResourceState::ShaderRead, rhi::PipelineStage::Transfer),
            "RHI-COMPLETED-SHADER-TEXTURE-VIEW-ACCESS");
        is(inspectView(rhi::ResourceState::ShaderRead,
               rhi::PipelineStage::ComputeShader,
               rhi::ResourceAccess::ShaderWrite),
            "RHI-COMPLETED-SHADER-TEXTURE-VIEW-ACCESS");
        --view.mipLevelCount;
        is(inspectView(), "RHI-COMPLETED-SHADER-TEXTURE-VIEW-RANGE");
        ++view.mipLevelCount;
        view.baseMipLevel = 1u;
        is(inspectView(), "RHI-COMPLETED-SHADER-TEXTURE-VIEW-RANGE");
        view.baseMipLevel = 0u;
        view.dimension = rhi::TextureViewDimension::Texture2DArray;
        is(inspectView(), "RHI-COMPLETED-SHADER-TEXTURE-VIEW-SHAPE");
        view.dimension = rhi::TextureViewDimension::Texture2D;
        view.usage = rhi::TextureUsage::Storage;
        is(inspectView(), "RHI-COMPLETED-SHADER-TEXTURE-VIEW-SHAPE");
        view.usage = rhi::TextureUsage::Sampled;
        view.format = rhi::TextureFormat::Rgba16Float;
        is(inspectView(), "RHI-COMPLETED-SHADER-TEXTURE-VIEW-SHAPE");
        view.format = desc.format;
        auto bad = b;
        bad.prototypeOptIn = false;
        is(inspect(bad), "RHI-COMPLETED-SHADER-TEXTURE-OPT-IN");
        bad = b;
        bad.contractVersion = 2u;
        is(inspect(bad), "RHI-COMPLETED-SHADER-TEXTURE-VERSION");
        for (auto member : {&Contract::Budget::maxUploads,
                 &Contract::Budget::maxMipLevels,
                 &Contract::Budget::maxTexelBytes,
                 &Contract::Budget::maxJoinVisits}) {
            bad = b;
            --(bad.*member);
            is(inspect(bad), "RHI-COMPLETED-SHADER-TEXTURE-CAPACITY");
            bad.*member = 0u;
            is(inspect(bad), "RHI-COMPLETED-SHADER-TEXTURE-BUDGET");
        }
        is(inspect(b, 29u), "RHI-COMPLETED-SHADER-TEXTURE-COMPLETION");
        is(inspect(b, 2u, 3u), "RHI-COMPLETED-SHADER-TEXTURE-COMPLETION");
        is(inspect(b, 2u, 4u, false), "RHI-COMPLETED-SHADER-TEXTURE-COMPLETION");
        assert(Contract::inspect(desc, b, 2u, 2u, 20u, UINT64_MAX, 1u, 2u, 4u, true));
        assert(Contract::inspect(desc, b, 2u, 2u, 20u, 0u, UINT64_MAX - 1u, 2u, 4u, true));
        desc.usage = rhi::TextureUsage::CopyDst;
        is(inspect(b), "RHI-COMPLETED-SHADER-TEXTURE-PROFILE");
        desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst;
        desc.arrayLayerCount = 2u;
        is(inspect(b), "RHI-COMPLETED-SHADER-TEXTURE-PROFILE");
        desc.arrayLayerCount = 1u;
        desc.format = rhi::TextureFormat::D32Float;
        is(inspect(b), "RHI-COMPLETED-SHADER-TEXTURE-PROFILE");
        std::puts(
            "BINDLESS_RHI_COMPLETED_SHADER_TEXTURE_INPUT_CONTRACT_OK same-original whole-sampled minted-generation revision bounded negative-proof no-shader-authority");
    }

    static void verifyCompletedReadPlanContract()
    {
        using Contract = rhi::OriginalCompletedTextureReadPlanContract;
        const rhi::OriginalTextureContentPlanBudget b{4u, true, 1u, 0u, 0u, 1u, 10u, 25u};
        assert(!Contract::validateIntent(b));
        auto bad = b;
        bad.contractVersion = 0u;
        is(Contract::validateIntent(bad), "RHI-COMPLETED-TEXTURE-READ-PLAN-VERSION");
        bad = b;
        bad.prototypeOptIn = false;
        is(Contract::validateIntent(bad), "RHI-COMPLETED-TEXTURE-READ-PLAN-OPT-IN");
        for (auto member : {&rhi::OriginalTextureContentPlanBudget::maxUploadCommands}) {
            bad = b;
            bad.*member = 1u;
            is(Contract::validateIntent(bad), "RHI-COMPLETED-TEXTURE-READ-PLAN-SCHEMA");
        }
        for (auto member : {&rhi::OriginalTextureContentPlanBudget::maxContentCells,
                 &rhi::OriginalTextureContentPlanBudget::maxReadbackCommands}) {
            bad = b;
            bad.*member = 1u;
            is(Contract::validateIntent(bad), "RHI-COMPLETED-TEXTURE-READ-PLAN-SCHEMA");
        }
        for (auto member : {&rhi::OriginalTextureContentPlanBudget::maxContentSteps,
                 &rhi::OriginalTextureContentPlanBudget::maxCommandVisits,
                 &rhi::OriginalTextureContentPlanBudget::maxProjectionVisits}) {
            bad = b;
            bad.*member = 0u;
            is(Contract::validateIntent(bad), "RHI-COMPLETED-TEXTURE-READ-PLAN-BUDGET");
        }
        bad = b;
        bad.maxContentControls = 0u;
        is(Contract::validateIntent(bad), "RHI-COMPLETED-TEXTURE-READ-PLAN-BUDGET");
        assert(!Contract::validateObservation(1u, true, 1u, 3u));
        assert(Contract::validateObservation(2u, true, 1u, 3u));
        assert(Contract::validateObservation(1u, false, 1u, 3u));
        assert(Contract::validateObservation(1u, true, 0u, 3u));
        assert(Contract::validateObservation(1u, true, 1u, 0u));
        for (const auto version : {1u, 2u}) {
            rhi::OriginalCommandNativeLifecycleContract::Budget lifecycle;
            lifecycle.contractVersion = version;
            is(rhi::OriginalCommandNativeLifecycleContract::validateContentProfile(lifecycle, 4u),
                "RHI-ORIGINAL-NATIVE-LIFECYCLE-COMPLETED-READ-PROFILE-UNAVAILABLE");
        }
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision = rhi::semantics::OriginalCommandStateAdmissionLowering::
                textureCompletedReadPlanLowering(backend);
            assert(decision.contractVersion == 4u && decision.validationAvailable
                && !decision.executionAvailable
                && decision.validatorOnly == (backend == rhi::Backend::Null)
                && decision.diagnostic);
            assert(!rhi::semantics::OriginalCommandStateAdmissionLowering::
                    textureCompletedReadPlanLowering(backend, 3u)
                        .validationAvailable);
        }
        std::puts(
            "BINDLESS_RHI_COMPLETED_TEXTURE_READ_PLAN4_CONTRACT_OK bounded whole completed-only four-lowering content-generation-unchanged native-execution-closed");
    }

    static void verifyRangeContract()
    {
        using Contract = rhi::OriginalTextureContentCreationContract;
        auto b = rangeBudget();
        auto desc = rangeDescriptor();
        std::uint64_t footprint = 77u;
        assert(!Contract::inspect(desc, b, footprint) && footprint == 1008u);
        desc.usage = desc.usage | rhi::TextureUsage::CopySrc;
        assert(!Contract::inspect(desc, b, footprint) && footprint == 1008u);
        desc = rangeDescriptor();
        desc.format = rhi::TextureFormat::Rgba16Float;
        desc.extent = {2u, 2u, 1u};
        desc.mipLevelCount = desc.arrayLayerCount = 1u;
        desc.usage = rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::CopySrc;
        auto colorBudget = b;
        colorBudget.maxContentCells = 1u;
        colorBudget.maxTexelBytes = 32u;
        assert(!Contract::inspect(desc, colorBudget, footprint) && footprint == 32u);
        desc = rangeDescriptor();
        --b.maxContentCells;
        is(Contract::inspect(desc, b, footprint), "RHI-ORIGINAL-TEXTURE-RANGE-CELL-CAPACITY");
        assert(!footprint);
        b = rangeBudget();
        --b.maxTexelBytes;
        is(Contract::inspect(desc, b, footprint), "RHI-CANONICAL-CONTENT-CREATION-BYTE-CAPACITY");
        assert(!footprint);
        b = rangeBudget();
        b.maxContentCells = 0u;
        is(Contract::inspect(desc, b, footprint), "RHI-CANONICAL-CONTENT-CREATION-CELL-BUDGET");
        b = rangeBudget();
        b.prototypeOptIn = false;
        is(Contract::inspect(desc, b, footprint), "RHI-CANONICAL-CONTENT-CREATION-OPT-IN");
        b = budget();
        b.maxContentCells = 1u;
        is(Contract::inspect(descriptor(), b, footprint),
            "RHI-CANONICAL-CONTENT-CREATION-CELL-BUDGET");
        for (const auto usage : {rhi::TextureUsage::Sampled,
                 rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Storage,
                 rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::CopyDst}) {
            desc = rangeDescriptor();
            desc.usage = usage;
            is(Contract::inspect(desc, rangeBudget(), footprint),
                "RHI-CANONICAL-CONTENT-CREATION-CELL-PROFILE");
        }
        desc = rangeDescriptor();
        desc.mipLevelCount = 5u;
        b = rangeBudget();
        b.maxContentCells = 15u;
        assert(Contract::inspect(desc, b, footprint) && !footprint);
        desc = rangeDescriptor();
        desc.format = rhi::TextureFormat::Depth24Stencil8;
        b = rangeBudget();
        is(Contract::inspect(desc, b, footprint), "RHI-ORIGINAL-TEXTURE-RANGE-CELL-CAPACITY");
        b.maxContentCells = 18u;
        assert(!Contract::inspect(desc, b, footprint) && footprint == 1008u);
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d = Contract::lowering(backend, 2u);
            assert(d.contractVersion == 2u && d.diagnostic && !d.nativeContentExecutionAvailable
                && d.exclusiveCreationAvailable == (backend == rhi::Backend::WebGPU));
        }
        assert(!Contract::lowering(rhi::Backend::WebGPU, 3u).exclusiveCreationAvailable);
        const rhi::semantics::TextureContentAccessPolicy attachmentPolicy{
            rhi::semantics::TextureContentAccessPolicy::Mode::OriginalAttachmentExclusive};
        assert(attachmentPolicy.originalAttachmentExclusive()
            && !attachmentPolicy.originalUploadExclusive()
            && attachmentPolicy.ordinaryAccessDiagnostic());
        std::puts(
            "BINDLESS_RHI_CONTENT_CELLS_CONTRACT_OK version2 explicit9 exact1008 depth-stencil18 bounded-footprint four-lowering execution-closed");
    }

    template <class Factory> static void verifyNative(Factory&& create)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        OriginalTextureContentCellSequenceFixture::verify();
        Context context(create);
        const char* diagnostic = nullptr;
        const auto make = [&] {
            return context.make(diagnostic);
        };
        CpuQuotaFactoryFixture pressure(context.quotas[4]);
        const auto peak = pressure.measure(make);
        pressure.verify(peak, make);
        for (const auto index : {0u, 1u, 2u, 3u}) {
            assert(!context.make(diagnostic, context.quotas[index]));
            is(diagnostic, "RHI-CANONICAL-CONTENT-CREATION-QUOTA-DOMAIN");
        }
        for (const auto index : {2u, 3u}) {
            const auto baseline = context.quotas[index]->liveBytes();
            {
                core::CpuMemoryQuotaLease full(context.quotas[index], bytes - baseline);
                assert(full.charged());
                assert(!make() && diagnostic);
            }
            assert(
                context.quotas[index]->liveBytes() == baseline && !context.quotas[4]->liveBytes());
        }
        {
            core::CpuMemoryQuotaLease full(context.quotas[4], bytes);
            assert(full.charged());
            assert(!make());
            is(diagnostic, "RHI-CANONICAL-CONTENT-CREATION-ALLOCATION");
        }
        auto texture = make();
        assert(texture && !diagnostic
            && !context.device->validateOriginalResourceRegistration(*texture));
        auto expectation = texture->observeOriginalContent();
        assert(expectation && !expectation->diagnostic() && !expectation->nativeExecutionProven()
            && expectation->state()
                == rhi::common::OriginalTextureContentControl::State::Uninitialized
            && !expectation->contentGeneration());
        auto native = rhi_webgpu::webGpuNativePayloadFor(*texture);
        assert(native && native->texture && native->ownerDevice
            && native->contentAccessPolicy().originalUploadExclusive()
            && native->contentAccessPolicy().ordinaryAccessDiagnostic()
            && native->extent == texture->desc().extent
            && texture->allocation().estimatedSizeBytes == 20u);
        native.reset();
        auto twin = make();
        assert(twin);
        is(twin->validateOriginalContent(*expectation),
            "RHI-CANONICAL-CONTENT-EXPECTATION-FOREIGN");
        twin.reset();
        {
            core::CpuMemoryQuotaScope resource(context.quotas[3]);
            auto ordinary = context.device->createTexture(descriptor());
            assert(ordinary && !ordinary->observeOriginalContent());
            is(ordinary->originalContentDiagnostic(), "RHI-CANONICAL-CONTENT-EXPECTATION-UNISSUED");
        }
        OriginalCommandTextureUploadInputsFixture::verify(
            *context.device, true, std::move(texture), context.quotas[4]);
        assert(expectation->diagnostic());
        expectation.reset();
        context.device.reset();
        for (const auto& q : context.quotas)
            assert(!q->liveBytes());
        std::printf(
            "BINDLESS_RHI_CANONICAL_CONTENT_CREATION_NATIVE_OK peak%llu exact-native-registration exclusive-policy uninitialized0 ordinary-unissued foreign-stale cold-exact-less-exhausted five-quotas-zero no-texel-completion-claim\n",
            static_cast<unsigned long long>(peak));
        lossContext_ = core::makeUnique<Context>(create);
        lossTexture_ = lossContext_->make(diagnostic);
        assert(lossTexture_);
        lossExpectation_.emplace(*lossTexture_->observeOriginalContent());
        verifyRangeNative(create);
#else
        (void)create;
#endif
    }

    template <class Factory> static void verifyRangeNative(Factory&& create)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        Context context(create);
        const char* diagnostic = nullptr;
        const auto make = [&] {
            return context.makeRange(diagnostic);
        };
        CpuQuotaFactoryFixture pressure(context.quotas[4]);
        const auto peak = pressure.measure(make);
        pressure.verify(peak, make);
        for (const auto index : {0u, 1u, 2u, 3u}) {
            assert(!context.makeRange(diagnostic, context.quotas[index]));
            is(diagnostic, "RHI-CANONICAL-CONTENT-CREATION-QUOTA-DOMAIN");
        }
        auto texture = make();
        assert(texture && !diagnostic && !texture->observeOriginalContent());
        assert(texture->allocation().estimatedSizeBytes == 1008u
            && texture->originalContentExclusive());
        auto native = rhi_webgpu::webGpuNativePayloadFor(*texture);
        assert(native && native->texture && native->contentAccessPolicy().ordinaryAccessDiagnostic()
            && native->contentAccessPolicy().originalAttachmentExclusive()
            && !native->contentAccessPolicy().originalUploadExclusive());
        native.reset();
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            context.quotas, [&]() noexcept -> const char* {
                for (std::uint32_t mip = 0u; mip < 3u; ++mip)
                    for (std::uint32_t layer = 0u; layer < 3u; ++layer) {
                        const rhi::TextureSubresourceRange range{
                            mip, 1u, layer, 1u, rhi::TextureAspect::Depth};
                        const auto observation = texture->observeOriginalContent(range);
                        assert(observation && observation->range() == range
                            && !observation->diagnostic()
                            && !texture->validateOriginalContent(*observation)
                            && !observation->nativeExecutionProven()
                            && !observation->contentGeneration()
                            && observation->state()
                                == rhi::common::OriginalTextureContentControl::State::Uninitialized
                            && observation->resourceGeneration()
                                == texture->originalResourceRegistration().generation());
                    }
                assert(
                    !texture->observeOriginalContent({3u, 1u, 0u, 1u, rhi::TextureAspect::Depth}));
                assert(
                    !texture->observeOriginalContent({0u, 0u, 0u, 1u, rhi::TextureAspect::Depth}));
                assert(!texture->observeOriginalContent(
                    {0u, 1u, 0u, 1u, rhi::TextureAspect::Stencil}));
                return nullptr;
            }));
        auto observation =
            texture->observeOriginalContent({1u, 1u, 2u, 1u, rhi::TextureAspect::Depth});
        assert(observation);
        auto twin = make();
        assert(twin);
        is(twin->validateOriginalContent(*observation), "RHI-CANONICAL-CONTENT-RANGE-FOREIGN");
        twin.reset();
        OriginalTextureContentCellsPlanFixture::verify(*context.device, texture, context.quotas[3]);
        texture.reset();
        assert(observation->diagnostic());
        observation.reset();
        context.device.reset();
        for (const auto& q : context.quotas)
            assert(!q->liveBytes());
        lossRangeTexture_ = lossContext_->makeRange(diagnostic);
        assert(lossRangeTexture_);
        lossRangeExpectation_.emplace(*lossRangeTexture_->observeOriginalContent(
            {2u, 1u, 1u, 1u, rhi::TextureAspect::Depth}));
        std::printf(
            "BINDLESS_RHI_CONTENT_CELLS_NATIVE_OK peak%llu original-nine-ranges exact1008 foreign-range lifetime-refused quotas-zero hot0 no-initialization-completion\n",
            static_cast<unsigned long long>(peak));
#else
        (void)create;
#endif
    }

    static void verifyActualLoss()
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        assert(lossTexture_->originalContentDiagnostic() && lossExpectation_->diagnostic()
            && !lossTexture_->observeOriginalContent());
        assert(lossRangeTexture_->originalContentDiagnostic() && lossRangeExpectation_->diagnostic()
            && !lossRangeTexture_->observeOriginalContent(lossRangeExpectation_->range()));
        const auto quotas = lossContext_->quotas;
        assert(
            !CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept -> const char* {
                lossTexture_.reset();
                lossExpectation_.reset();
                lossRangeTexture_.reset();
                lossRangeExpectation_.reset();
                lossContext_->device.reset();
                lossContext_.reset();
                return nullptr;
            }));
        for (const auto& q : quotas)
            assert(!q->liveBytes());
        std::puts(
            "BINDLESS_RHI_CANONICAL_CONTENT_CREATION_LOSS_OK physical-loss expectation-refused five-quotas-zero hot-allocation0 completion-unproven");
        std::puts(
            "BINDLESS_RHI_CONTENT_CELLS_LOSS_OK physical-loss original-range-refused five-quotas-zero hot0 completion-unproven");
#endif
    }
};
} // namespace arp::test
