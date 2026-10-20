#pragma once
#include "cpu_quota_factory_fixture.h"
#include "original_command_upload_stream_builder_fixture.h"
#include "rhi/frontend/command_recording.h"
#include "rhi/frontend/command_texture_upload_staging.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalUndefinedTextureUploadPlanFixture final {
    static constexpr std::uint64_t bytes = 65536u;

    static void expect(const char* actual, const char* expected)
    {
        if (!actual || std::string_view(actual) != expected)
            std::fprintf(stderr,
                "ContentPlan7 actual=%s expected=%s\n",
                actual ? actual : "<none>",
                expected);
        assert(actual && std::string_view(actual) == expected);
    }

public:
    static rhi::OriginalTextureContentPlanBudget budget()
    {
        return {7u, true, 1u, 1u, 0u, 2u, 5u, 17u};
    }

    static void verifyContract()
    {
        using Publication = rhi::OriginalCompletedTextureDescriptorContract;
        rhi::OriginalCompletedTextureDescriptorRequest publication{
            1u, true, 1u, 1u, rhi::OriginalCommandTextureViewSource::completeRetainedOwnerCount};
        assert(!Publication::validateIntent(publication));
        for (unsigned field = 0u; field != 5u; ++field) {
            auto rejected = publication;
            if (field == 0u)
                rejected.contractVersion = 0u;
            if (field == 1u)
                rejected.prototypeOptIn = false;
            if (field == 2u)
                rejected.maxNativeImageViews = 0u;
            if (field == 3u)
                rejected.maxNativeWrites = 0u;
            if (field == 4u)
                --rejected.maxRetainedSourceOwners;
            assert(Publication::validateIntent(rejected));
        }
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision = Publication::lowering(backend);
            assert(!decision.shaderExecutionAvailable);
            assert(decision.validatorOnly == (backend == rhi::Backend::Null));
            const bool nativePublication =
                backend == rhi::Backend::Vulkan || backend == rhi::Backend::Metal;
            assert(decision.publicationAvailable == nativePublication);
            assert(bool(decision.diagnostic) == !nativePublication);
            assert(!Publication::lowering(backend, 0u).publicationAvailable);
            const auto web = Publication::lowering(backend, 2u);
            assert(!web.shaderExecutionAvailable);
            assert(web.validatorOnly == (backend == rhi::Backend::Null));
            assert(web.publicationAvailable == (backend == rhi::Backend::WebGPU));
            assert(bool(web.diagnostic) == (backend != rhi::Backend::WebGPU));
        }
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto hdr = Publication::lowering(backend, 5u);
            assert(hdr.publicationAvailable
                == (backend == rhi::Backend::Vulkan || backend == rhi::Backend::Metal));
            assert(hdr.validatorOnly == (backend == rhi::Backend::Null));
            assert(!hdr.shaderExecutionAvailable);
        }
        auto hdr = publication;
        hdr.contractVersion = 5u;
        assert(!Publication::validateIntent(hdr));
        assert(!Publication::validateControl(3u, 5u));
        assert(Publication::validateControl(1u, 5u));
        publication.contractVersion = 2u;
        assert(!Publication::validateIntent(publication));
        for (std::uint32_t version = 0u; version <= 3u; ++version)
            for (std::uint32_t control = 0u; control <= 5u; ++control)
                assert(bool(Publication::validateControl(control, version))
                    == !((version == 1u && control == 3u) || (version == 2u && control == 1u)));
        using DescriptorLoss = rhi::OriginalDescriptorNativeLossRegistrationContract;
        rhi::OriginalResourceRegistrationBudget descriptorLoss{9u, true, 1u, 1u, 1u, 16u, bytes};
        descriptorLoss.maxDescriptorArenas = descriptorLoss.maxDescriptorSets = 1u;
        assert(!DescriptorLoss::validate({1u, true}, descriptorLoss));
        assert(DescriptorLoss::validate({0u, true}, descriptorLoss));
        assert(DescriptorLoss::validate({1u, false}, descriptorLoss));
        assert(rhi::OriginalResourceRegistrationContract::lowering(rhi::Backend::Vulkan, 9u)
                .diagnostic);
        for (unsigned field = 0u; field < 7u; ++field) {
            auto rejected = descriptorLoss;
            if (field == 0u)
                rejected.contractVersion = 3u;
            if (field == 1u)
                rejected.maxDescriptorArenas = 0u;
            if (field == 2u)
                rejected.maxDescriptorSets = 0u;
            if (field == 3u)
                rejected.maxPipelineLayouts = 1u;
            if (field == 4u)
                rejected.maxComputePipelines = 1u;
            if (field == 5u)
                rejected.maxDescriptorTables = 1u;
            if (field == 6u)
                rejected.samplers = {1u, true, 1u};
            assert(DescriptorLoss::validate({1u, true}, rejected));
        }
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision = DescriptorLoss::lowering(backend);
            assert(!decision.executionAvailable);
            assert(decision.validatorOnly == (backend == rhi::Backend::Null));
            assert(decision.registrationAvailable
                == (backend == rhi::Backend::Vulkan || backend == rhi::Backend::Metal));
            assert(!DescriptorLoss::lowering(backend, 0u).registrationAvailable);
        }
        assert(DescriptorLoss::validate({2u, true}, descriptorLoss));
        descriptorLoss.samplers = {1u, true, 1u};
        assert(
            DescriptorLoss::validate({1u, true}, descriptorLoss)); // Legacy request remains closed.
        assert(!DescriptorLoss::validate({2u, true}, descriptorLoss));
        assert(!DescriptorLoss::validate({3u, true}, descriptorLoss));
        using SamplerLoss = rhi::OriginalSamplerNativeLossJoinContract;
        assert(!SamplerLoss::validateRegistration(descriptorLoss, 2u));
        assert(SamplerLoss::validateRegistration(descriptorLoss));
        assert(SamplerLoss::validateRegistration(descriptorLoss, 3u));
        auto forbidden = descriptorLoss;
        forbidden.maxPipelineLayouts = 1u;
        assert(DescriptorLoss::validate({2u, true}, forbidden));
        forbidden = descriptorLoss;
        forbidden.maxDescriptorTables = 1u;
        assert(DescriptorLoss::validate({2u, true}, forbidden));
        descriptorLoss.samplers.prototypeOptIn = false;
        assert(DescriptorLoss::validate({2u, true}, descriptorLoss));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto registration = DescriptorLoss::lowering(backend, 2u);
            const auto sampler = SamplerLoss::lowering(backend, 2u);
            assert(registration.registrationAvailable
                == (backend == rhi::Backend::Vulkan || backend == rhi::Backend::Metal));
            assert(sampler.nativeSourceAvailable
                == (backend == rhi::Backend::Vulkan || backend == rhi::Backend::Metal
                    || backend == rhi::Backend::WebGPU));
            assert(sampler.validatorOnly == (backend == rhi::Backend::Null));
            assert(!sampler.recordingAvailable && !sampler.executionAvailable);
            assert(bool(sampler.diagnostic) == (backend == rhi::Backend::Null));
        }
        using C = rhi::OriginalUndefinedTextureUploadPlanContract;
        using Read = rhi::OriginalCompletedTextureReadPlanContract;
        rhi::OriginalTextureContentPlanBudget read{8u, true, 1u, 0u, 0u, 2u, 16u, 64u, 0u};
        assert(!Read::validateIntent(read) && Read::isReadOnly(8u));
        assert(!Read::validateRootProfile(8u, false, false, true));
        assert(Read::validateRootProfile(8u, true, false, true));
        assert(Read::validateRootProfile(8u, false, true, true));
        assert(Read::validateRootProfile(8u, false, false));
        for (const auto v : {4u, 5u})
            assert(Read::validateRootProfile(v, false, false, true));
        assert(!Read::validateObservation(3u, true, 1u, 2u, 8u));
        for (const auto v : {1u, 2u})
            assert(Read::validateObservation(v, true, 1u, 2u, 8u));
        assert(Read::validateObservation(3u, true, 1u, 2u));
        assert(Read::validateObservation(3u, false, 1u, 2u, 8u));
        assert(!Read::validatePendingProfile(4u, true, false, 8u, false, true));
        for (const auto v : {1u, 2u, 3u})
            assert(Read::validatePendingProfile(v, true, false, 8u, false, true));
        assert(Read::validatePendingProfile(4u, true, false, 8u));
        assert(Read::validatePendingProfile(4u, false, false, 8u, false, true));
        using RS = rhi::ResourceState;
        assert(!Read::validatePendingStates(
            RS::CopyDst, RS::CopyDst, RS::ShaderRead, RS::ShaderRead, 3u, 4u, 4u));
        assert(!Read::validatePendingStates(
            RS::ShaderRead, RS::ShaderRead, RS::ShaderRead, RS::ShaderRead, 4u, 4u, 4u));
        assert(Read::validatePendingStates(
            RS::ShaderRead, RS::ShaderRead, RS::ShaderRead, RS::ShaderRead, 4u, 5u, 4u));
        assert(Read::validatePendingStates(
            RS::ShaderRead, RS::ShaderRead, RS::ShaderRead, RS::ShaderRead, 4u, 4u));
        rhi::TextureDesc rt;
        rt.extent = {2u, 2u, 1u};
        rt.format = rhi::TextureFormat::Rgba8Unorm;
        rt.arrayLayerCount = 2u;
        rt.mipLevelCount = 2u;
        rt.usage = rhi::TextureUsage::Sampled;
        rhi::TextureViewDesc rv;
        rv.format = rt.format;
        rv.dimension = rhi::TextureViewDimension::Texture2DArray;
        rv.usage = rhi::TextureUsage::Sampled;
        rv.mipLevelCount = rt.mipLevelCount;
        rv.arrayLayerCount = rt.arrayLayerCount;
        const auto inspect = [&](std::uint32_t v) {
            return Read::inspectViewBinding(rt,
                rv,
                RS::ShaderRead,
                rhi::PipelineStage::ComputeShader,
                rhi::ResourceAccess::ShaderRead,
                v);
        };
        assert(!inspect(8u) && !inspect(10u) && inspect(4u) && inspect(5u) && inspect(0u));
        rv.arrayLayerCount = 1u;
        assert(inspect(8u) && inspect(10u));
        rv.arrayLayerCount = 2u;
        rv.dimension = rhi::TextureViewDimension::Texture2D;
        assert(inspect(8u) && inspect(10u));
        rv.dimension = rhi::TextureViewDimension::Texture2DArray;
        rt.format = rv.format = rhi::TextureFormat::Rgba16Float;
        assert(inspect(8u) && inspect(10u));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d = rhi::semantics::OriginalCommandStateAdmissionLowering::
                textureCompletedReadPlanLowering(backend, 8u);
            assert(d.validationAvailable && !d.executionAvailable && d.diagnostic);
            assert(d.validatorOnly == (backend == rhi::Backend::Null));
        }
        for (const auto v : {1u, 2u, 3u, 4u, 5u, 6u, 7u}) {
            rhi::OriginalCommandNativeLifecycleBudget life;
            life.contractVersion = v;
            assert(rhi::OriginalCommandNativeLifecycleContract::validateContentProfile(life, 8u));
        }
        using Life = rhi::OriginalCommandNativeLifecycleContract;
        rhi::OriginalCommandNativeLifecycleBudget textureLife{
            8u, true, 1u, 0u, 0u, 8u, 8u, 0u, bytes, bytes, 0u};
        assert(!Life::validateIntent(textureLife));
        assert(!Life::validateContentProfile(textureLife, 8u));
        for (const auto v : {0u, 3u, 4u, 5u, 6u, 7u})
            assert(Life::validateContentProfile(textureLife, v));
        assert(!Life::validateCapacityRequest({4u, true, 1u, 0u, 0u}, textureLife));
        for (const auto v : {1u, 2u, 3u})
            assert(Life::validateCapacityRequest({v, true, 1u, 0u, 0u}, textureLife));
        auto wrongLife = textureLife;
        wrongLife.maxCallbackTickets = 1u;
        assert(Life::validateIntent(wrongLife));
        wrongLife = textureLife;
        wrongLife.prototypeOptIn = false;
        assert(Life::validateIntent(wrongLife));
        using Completion = rhi::OriginalGeneralSubmissionCompletionContract;
        assert(!Completion::validateProfile(5u, false, false, 8u, true));
        for (const auto v : {0u, 1u, 2u, 3u, 4u})
            assert(Completion::validateProfile(v, false, false, 8u, true));
        assert(Completion::validateProfile(5u, false, false, 8u));
        assert(Completion::validateProfile(5u, true, false, 8u, true));
        assert(Completion::validateProfile(5u, false, false, 7u, true));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto lowering =
                rhi::semantics::OriginalCommandNativeLifecycleLowering::lowering(backend, 8u);
            assert(lowering.executionAvailable
                == (backend == rhi::Backend::Vulkan || backend == rhi::Backend::Metal));
            assert(lowering.sealedIssuerAvailable == lowering.executionAvailable);
            assert(bool(lowering.diagnostic) != lowering.executionAvailable);
            const auto idle =
                rhi::semantics::OriginalCommandNativeLifecycleLowering::deviceIdleLowering(
                    backend, 8u);
            assert(idle.completionAvailable == (backend == rhi::Backend::Vulkan));
        }
        using S = rhi::ResourceState;
        assert(!C::validateIntent(budget()));
        auto b = budget();
        b.contractVersion = 1u;
        assert(C::validateIntent(b));
        b = budget();
        b.prototypeOptIn = false;
        assert(C::validateIntent(b));
        b = budget();
        b.maxContentCells = 1u;
        assert(C::validateIntent(b));
        b = budget();
        b.maxReadbackCommands = 1u;
        assert(C::validateIntent(b));
        for (unsigned field = 0u; field < 5u; ++field) {
            b = budget();
            if (field == 0u)
                b.maxContentControls = 0u;
            if (field == 1u)
                b.maxUploadCommands = 0u;
            if (field == 2u)
                b.maxContentSteps = 0u;
            if (field == 3u)
                b.maxCommandVisits = 0u;
            if (field == 4u)
                b.maxProjectionVisits = 0u;
            assert(C::validateIntent(b));
        }
        for (const auto control : {0u, 1u, 2u, 3u, 7u})
            for (const auto plan : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u})
                assert(!C::validateControl(control, plan)
                    == ((control == 1u && plan == 1u) || (control == 3u && plan == 7u)));
        assert(!C::validateInitial(3u, true, 0u, S::Undefined));
        assert(C::validateInitial(1u, true, 0u, S::Undefined));
        assert(C::validateInitial(3u, false, 0u, S::Undefined));
        assert(C::validateInitial(3u, true, 1u, S::Undefined));
        assert(C::validateInitial(3u, true, 0u, S::CopyDst));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d =
                rhi::semantics::OriginalCommandStateAdmissionLowering::textureContentPlanLowering(
                    backend, 7u);
            assert(d.contractVersion == 7u && d.diagnostic && !d.executionAvailable);
            assert(d.validationAvailable
                == (backend == rhi::Backend::Null || backend == rhi::Backend::Vulkan));
            assert(d.validatorOnly == (backend == rhi::Backend::Null));
        }
        for (const auto version : {1u, 2u, 3u, 4u, 5u, 6u}) {
            rhi::OriginalCommandNativeLifecycleBudget life;
            life.contractVersion = version;
            expect(rhi::OriginalCommandNativeLifecycleContract::validateContentProfile(life, 7u),
                "RHI-UNDEFINED-TEXTURE-UPLOAD-NATIVE-LIFECYCLE-UNAVAILABLE");
        }
        for (const auto version : {0u, 1u, 2u, 3u})
            assert(rhi::OriginalGeneralSubmissionCompletionContract::validateProfile(
                version, false, false, 7u));
        assert(!rhi::OriginalGeneralSubmissionCompletionContract::validateProfile(
            4u, false, false, 7u));
        assert(
            rhi::OriginalGeneralSubmissionCompletionContract::validateProfile(4u, true, false, 7u));
        assert(
            rhi::OriginalGeneralSubmissionCompletionContract::validateProfile(4u, false, true, 7u));
        for (const auto content : {0u, 1u, 2u, 3u, 4u, 5u, 6u})
            assert(rhi::OriginalGeneralSubmissionCompletionContract::validateProfile(
                4u, false, false, content));
        rhi::OriginalCommandNativeLifecycleBudget life{
            7u, true, 1u, 0u, 0u, 32u, 64u, 0u, bytes, bytes, 0u};
        using L = rhi::OriginalCommandNativeLifecycleContract;
        assert(!L::validateIntent(life) && !L::validateContentProfile(life, 7u));
        for (const auto content : {0u, 1u, 2u, 3u, 4u, 5u, 6u})
            assert(L::validateContentProfile(life, content));
        assert(!L::validateCapacityRequest({3u, true, 1u, 0u, 0u}, life));
        for (const auto v : {0u, 1u, 2u, 4u})
            assert(L::validateCapacityRequest({v, true, 1u, 0u, 0u}, life));
        assert(L::validateCapacityRequest({3u, true, 1u, 1u, 0u}, life));
        assert(L::validateCapacityRequest({3u, true, 1u, 0u, 1u}, life));
        life.maxCallbackCpuBytes = bytes;
        assert(L::validateIntent(life));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d =
                rhi::semantics::OriginalCommandNativeLifecycleLowering::lowering(backend, 7u);
            const bool nativeUpload =
                backend == rhi::Backend::Vulkan || backend == rhi::Backend::Metal;
            assert(d.contractVersion == 7u && d.executionAvailable == nativeUpload
                && d.sealedIssuerAvailable == nativeUpload);
            assert(!d.diagnostic == nativeUpload);
            const auto staging =
                rhi::semantics::OriginalTextureUploadStagingLowering::lowering(backend);
            assert(staging.factoryAvailable == nativeUpload);
            const auto idle =
                rhi::semantics::OriginalCommandNativeLifecycleLowering::deviceIdleLowering(
                    backend, 7u);
            assert(idle.completionAvailable == (backend == rhi::Backend::Vulkan));
        }
        using Staging = rhi::OriginalTextureUploadStagingContract;
        rhi::OriginalTextureUploadStagingBudget sb{
            1u, true, 1u, 2u, Staging::retainedOwnerBound, 768u, 4096u, bytes, bytes, bytes};
        assert(!Staging::validateIntent(sb));
        sb.maxSources = 2u;
        assert(Staging::validateIntent(sb));
        sb.maxSources = 1u;
        sb.prototypeOptIn = false;
        assert(Staging::validateIntent(sb));
        sb.prototypeOptIn = true;
        sb.contractVersion = 2u;
        assert(!Staging::validateIntent(sb));
        sb.contractVersion = 3u;
        assert(Staging::validateIntent(sb));
        sb.contractVersion = 1u;
        --sb.maxRetainedOwners;
        assert(Staging::validateIntent(sb));
        rhi::TextureDesc td;
        td.format = rhi::TextureFormat::Rgba8Unorm;
        td.extent = {2u, 2u, 1u};
        assert(!Staging::validateDestination(td));
        td.arrayLayerCount = 2u;
        assert(!Staging::validateDestination(td));
        td.extent.depth = 2u;
        assert(Staging::validateDestination(td));
        td.extent.depth = 1u;
        td.cubeCompatible = true;
        assert(Staging::validateDestination(td));
        const auto saved = td;
        td.arrayLayerCount = 6u;
        td.extent.height = td.extent.width;
        assert(!Staging::validateDestination(td));
        td.arrayLayerCount = 12u;
        assert(Staging::validateDestination(td));
        td = saved;
        td.cubeCompatible = false;
        td.format = rhi::TextureFormat::Bc7Unorm;
        assert(Staging::validateDestination(td));
        td.format = rhi::TextureFormat::Rgba16Float;
        assert(Staging::validateDestination(td));
        assert(!Staging::validateDestination(td, 2u));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto hdr =
                rhi::semantics::OriginalTextureUploadStagingLowering::lowering(backend, 2u);
            assert(hdr.factoryAvailable
                == (backend == rhi::Backend::Vulkan || backend == rhi::Backend::Metal));
        }
        rhi::OriginalCommandAdmissionBudget nativeBudget;
        nativeBudget.maxNativePayloadObjects = 1u;
        nativeBudget.maxTypedNativeCommandCalls = 4u;
        nativeBudget.maxNativeStagingBytes = 4096u;
        assert(!Staging::validateExecutionBudget(nativeBudget, 2u, 4096u));
        --nativeBudget.maxTypedNativeCommandCalls;
        assert(Staging::validateExecutionBudget(nativeBudget, 2u, 4096u));
        ++nativeBudget.maxTypedNativeCommandCalls;
        --nativeBudget.maxNativeStagingBytes;
        assert(Staging::validateExecutionBudget(nativeBudget, 2u, 4096u));
        std::puts(
            "BINDLESS_RHI_UNDEFINED_TEXTURE_UPLOAD_PLAN7_CONTRACT_OK control3-only isolated-fence7 capacity3 completion4 four-lowering");
    }

    static void verifyRoot(rhi::GpuDevice& device,
        core::UniquePtr<rhi::GpuTexture> texture,
        const std::shared_ptr<core::CpuMemoryQuota>& contentQuota)
    {
        using namespace rhi;
        using K = CommandUsageKind;
        using Root = OriginalCommandStateTransaction;
        using Graph = OriginalCommandResourceStates;
        using Sources = OriginalCommandResourceSources;
        using Inputs = OriginalCommandInputs;
        using State = common::OriginalTextureContentControl::State;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 4> q;
        for (auto& owner : q)
            owner = core::makeShared<core::CpuMemoryQuota>(bytes);
        const char* id = nullptr;
        const auto* object = texture.get();
        assert(object);
        const auto range = defaultTextureSubresourceRange(object->desc());
        auto lease =
            OriginalCommandTextureLease::prepareOriginal(texture, {1u, true, 2u, bytes}, q[0], id);
        assert(lease);
        std::array<std::uint8_t, 20> pixels{};
        for (std::size_t i = 0u; i < pixels.size(); ++i)
            pixels[i] = static_cast<std::uint8_t>(i + 1u);
        UploadTextureDesc upload;
        upload.destinationTexture = object->id();
        upload.extent = object->desc().extent;
        upload.format = object->desc().format;
        upload.subresources = range;
        upload.bytesPerRow = 8u;
        upload.rowsPerImage = 2u;
        upload.pixelData = pixels.data();
        upload.pixelDataSize = pixels.size();
        upload.mipLayouts = {{0u, {2u, 2u, 1u}, 8u, 2u, 0u}, {1u, {1u, 1u, 1u}, 4u, 1u, 16u}};
        auto snapshot = TextureUploadInputSnapshot::prepareOriginal(
            upload, {1u, true, 2u, 0u, 20u, bytes, bytes, bytes}, q[1], q[1], q[0], id);
        assert(snapshot);
        auto source = OriginalCommandTextureUploadSource::prepareOriginal(
            snapshot, lease, {1u, true, 5u, bytes}, q[0], id);
        assert(source);
        const std::array<Sources::Source, 2> retained{lease, source};
        ResourceBarrierDesc barrier{
            object->id(), range, ResourceState::Undefined, ResourceState::CopyDst};
        barrier.sourceStages = PipelineStage::TopOfPipe;
        barrier.sourceAccess = ResourceAccess::None;
        barrier.destinationStages = PipelineStage::Transfer;
        barrier.destinationAccess = ResourceAccess::TransferWrite;
        const auto args = *TextureUploadOriginalInputArguments::captureOriginal(snapshot);
        OriginalCommandAdmissionBudget authored;
        authored.contractVersion = 1u;
        authored.prototypeOptIn = true;
        authored.maxStateObservations = authored.maxTextureRanges = 3u;
        authored.maxMipMappings = 4u;
        authored.maxRetainedUploadBytes = 40u;
        authored.maxStateTransactions = 1u;
        authored.maxCpuBytes[static_cast<std::size_t>(
            OriginalCommandAdmissionBudget::CpuDomain::Recording)] = bytes;
        const auto makeInputs = [&](bool duplicate = false,
                                    bool missing = false,
                                    bool partial = false) {
            auto dep = barrier;
            if (partial)
                dep.textureSubresources.mipLevelCount = 1u;
            if (duplicate) {
                const std::array<CommandUsage, 6> commands{{{K::TextureBarrier, 0u, 1u},
                    {K::BeginTransferPass, 1u, 0u},
                    {K::TextureUpload, 1u, 2u},
                    {K::TextureUpload, 3u, 2u},
                    {K::EndTransferPass, 5u, 0u},
                    {K::Finish, 5u, 0u}}};
                const std::array<OriginalCommandUsage::Reference, 5> refs{
                    {{0u}, {0u}, {1u}, {0u}, {1u}}};
                const std::array<Inputs::OriginalDescriptor, 3> descriptors{
                    {{0u, *ResourceDependencyArguments::capture(K::TextureBarrier, dep)},
                        {2u, args},
                        {3u, args}}};
                return OriginalCommandUploadStreamBuilderFixture::prepare(
                    retained, commands, refs, descriptors, authored, q[0], q[1], id);
            }
            const std::array<CommandUsage, 5> commands{
                {{missing ? K::RetainTexture : K::TextureBarrier, 0u, 1u},
                    {K::BeginTransferPass, 1u, 0u},
                    {K::TextureUpload, 1u, 2u},
                    {K::EndTransferPass, 3u, 0u},
                    {K::Finish, 3u, 0u}}};
            const std::array<OriginalCommandUsage::Reference, 3> refs{{{0u}, {0u}, {1u}}};
            if (missing) {
                const std::array<Inputs::OriginalDescriptor, 1> descriptors{{{2u, args}}};
                return OriginalCommandUploadStreamBuilderFixture::prepare(
                    retained, commands, refs, descriptors, authored, q[0], q[1], id);
            }
            const std::array<Inputs::OriginalDescriptor, 2> descriptors{
                {{0u, *ResourceDependencyArguments::capture(K::TextureBarrier, dep)}, {2u, args}}};
            return OriginalCommandUploadStreamBuilderFixture::prepare(
                retained, commands, refs, descriptors, authored, q[0], q[1], id);
        };
        const OriginalCommandStateAdmissionBudget graphBudget{1u, true, 1u, 1u, 3u, 3u, 64u, bytes};
        const auto makeGraph = [&](const auto& inputs) {
            return Graph::prepareOriginal(inputs, device, graphBudget, q[2], id);
        };
        const auto makeRoot = [&](const auto& graph) {
            return Root::prepareOriginalOwnership(graph, device, graphBudget, q[3], id);
        };
        for (const unsigned mode : {0u, 1u, 2u}) {
            auto inputs = makeInputs(mode == 0u, mode == 1u, mode == 2u);
            assert(inputs);
            auto graph = makeGraph(inputs);
            assert(graph);
            auto root = makeRoot(graph);
            assert(root);
            auto b = budget();
            b.maxCommandVisits = 6u;
            b.maxProjectionVisits = 64u;
            const auto result = root->preflightOriginalUndefinedTextureUploads(b);
            expect(result.diagnostic,
                mode == 0u ? "RHI-UNDEFINED-TEXTURE-UPLOAD-PLAN-DUPLICATE"
                           : "RHI-UNDEFINED-TEXTURE-UPLOAD-PLAN-TRANSITION");
            assert(root->journalEmpty() && !root->hasOriginalPrototypePlan());
        }
        auto inputs = makeInputs();
        assert(inputs);
        auto graph = makeGraph(inputs);
        assert(graph);
        auto root = makeRoot(graph);
        assert(root);
        const auto original = object->observeOriginalContent();
        assert(original);
        const auto originalState = object->observeOriginalCommittedState(range);
        assert(originalState);
        const auto complete = root->preflightOriginalUndefinedTextureUploads(budget());
        assert(!complete.diagnostic && complete.controls == 1u && complete.steps == 2u
            && complete.commandVisits == 5u && complete.projectionVisits == 17u
            && !complete.nativeExecutionProven());
        for (unsigned field = 0u; field < 5u; ++field) {
            auto b = budget();
            if (field == 0u)
                --b.maxContentControls;
            if (field == 1u)
                --b.maxUploadCommands;
            if (field == 2u)
                --b.maxContentSteps;
            if (field == 3u)
                --b.maxCommandVisits;
            if (field == 4u)
                --b.maxProjectionVisits;
            assert(root->preflightOriginalUndefinedTextureUploads(b).diagnostic
                && root->journalEmpty());
        }
        const OriginalCommandStateSequenceBudget sequence{1u, true, 5u, 0u, 4u, 4u};
        expect(root->materializeOriginalPrototypePlan(sequence, {1u, true, 1u, 1u}),
            "RHI-CANONICAL-CONTENT-PLAN-CELL-PROFILE-UNIMPLEMENTED");
        auto tooSmall = budget();
        --tooSmall.maxProjectionVisits;
        expect(root->materializeOriginalPrototypePlan(sequence, tooSmall),
            "RHI-UNDEFINED-TEXTURE-UPLOAD-PLAN-PROJECTION-CAPACITY");
        assert(!root->hasOriginalPrototypePlan() && root->journalEmpty());
        root.reset();
        const auto planned = [&] {
            auto value = makeRoot(graph);
            if (value && value->materializeOriginalPrototypePlan(sequence, budget()))
                return std::shared_ptr<Root>{};
            return value;
        };
        CpuQuotaFactoryFixture pressure(q[3]);
        const auto peak = pressure.measure(planned);
        pressure.verify(peak, planned);
        root = planned();
        assert(root && root->plannedContentControlCount() == 1u
            && root->plannedContentUploadCount() == 1u && root->plannedTransitionCount() == 1u
            && root->plannedFinalCellCount() == 2u);
        const std::array hotQuotas{q[0], q[1], q[2], q[3], contentQuota};
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            hotQuotas, [&]() noexcept -> const char* {
                if (const auto* failure = root->applyOriginalPrototypePlan())
                    return failure;
                assert(root->plannedContentGeneration(2u) == 1u
                    && root->status() == Root::Status::Pending);
                assert(!object->observeOriginalContent()
                    && !object->observeOriginalCommittedState(range));
                expect(root->commitOriginalPendingOwnership(),
                    "RHI-CANONICAL-CONTENT-NATIVE-COMPLETION-REQUIRED");
                if (const auto* failure = root->validate())
                    return failure;
                return root->abandonOriginalPendingOwnership();
            }));
        auto restored = object->observeOriginalContent();
        assert(restored && restored->state() == State::Uninitialized
            && !restored->contentGeneration() && restored->revision() > original->revision());
        auto state = object->observeOriginalCommittedState(range);
        assert(state && state->state() == ResourceState::Undefined
            && state->revision() > originalState->revision());
        assert(original->diagnostic() && object->validateOriginalCommittedState(*originalState));
        root.reset();
        graph.reset();
        graph = makeGraph(inputs);
        assert(graph);
        root = planned();
        assert(root);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            hotQuotas, [&]() noexcept -> const char* {
                if (const auto* failure = root->applyOriginalPrototypePlan())
                    return failure;
                assert(root->plannedContentGeneration(2u) == 2u);
                root.reset();
                return nullptr;
            }));
        assert(restored->diagnostic()
            && object->observeOriginalContent()->state() == State::Uninitialized);
        graph.reset();
        inputs.reset();
        source.reset();
        snapshot.reset();
        lease.reset();
        // Retained test arrays intentionally remain alive until function exit.
        std::printf(
            "BINDLESS_VULKAN_UNDEFINED_TEXTURE_UPLOAD_PLAN7 1 peak=%llu original-Undefined same-journal cancellation drop generation1-2 hot-allocation0 execution-closed\n",
            static_cast<unsigned long long>(peak));
    }
};
} // namespace arp::test
