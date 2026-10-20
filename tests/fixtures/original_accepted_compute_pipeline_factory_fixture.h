#include "original_descriptor_buffer_binding_fixture.h"
#pragma once
#include "../rhi_shader_package_test_helpers.h"
#include "original_accepted_pipeline_wrapper_seal_fixture.h"
#include "original_artifact_compute_creation_fixture.h"
#include "original_command_pipeline_source_fixture.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/webgpu/webgpu_pipeline_preparation.h"
#include "rhi/frontend/pipeline_creation_validation.h"
#include "rhi/gpu_device.h"
#include "rhi/semantics/native_pipeline_state_lowering.h"

namespace arp::test {
class OriginalAcceptedComputePipelineFactoryFixture final {
    using Contract = rhi::OriginalAcceptedComputePipelineFactoryContract;
    static constexpr std::uint64_t bytes = 65536u;

public:
    static void verifyContract()
    {
        verifyLoweringAllocator();
        OriginalAcceptedPipelineWrapperSealFixture::verifyContract();
        static_assert(!std::is_default_constructible_v<rhi::OriginalComputePipelineCreation>);
        static_assert(!std::is_copy_constructible_v<rhi::OriginalComputePipelineCreation>);
        assert(Contract::validateBudget({}));
        Contract::Budget b{1u, true, bytes};
        assert(!Contract::validateBudget(b));
        b.contractVersion = 0u;
        assert(Contract::validateBudget(b));
        b = {1u, false, bytes};
        assert(Contract::validateBudget(b));
        b = {1u, true, 0u};
        assert(Contract::validateBudget(b));
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (const auto backend : std::array{rhi::Backend::Null,
                 rhi::Backend::Vulkan,
                 rhi::Backend::Metal,
                 rhi::Backend::WebGPU}) {
            const auto d = Contract::lowering(backend, true);
            assert(!d.executionAvailable && d.factoryEligible == (backend == rhi::Backend::WebGPU));
            assert(d.validatorOnly == (backend == rhi::Backend::Null));
            assert(!Contract::lowering(backend).factoryEligible);
            const auto registry = rhi::OriginalResourceRegistrationContract::lowering(backend, 5u);
            assert(registry.fullPipelineDefinitionCustodyAvailable && !registry.executionAvailable);
        }
        assert(!Contract::lowering(static_cast<rhi::Backend>(255u), true).factoryEligible);
        assert(rhi::OriginalComputePipelineCreation::validateOriginal({}));
        assert(rhi::OriginalResourceRegistrationContract::ownerCount(
                   rhi::OriginalRegisteredResourceKind::ComputePipeline, 5u)
            == 6u);
        assert(rhi::OriginalResourceRegistrationContract::ownerCount(
                   rhi::OriginalRegisteredResourceKind::GraphicsPipeline, 5u)
            == 5u);
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
#if !defined(__EMSCRIPTEN__)
        {
            auto domain = core::makeShared<core::CpuMemoryQuota>(bytes),
                 metadata = core::makeShared<core::CpuMemoryQuota>(bytes),
                 wrapper = core::makeShared<core::CpuMemoryQuota>(bytes);
            auto device = rhi::createNullDevice();
            assert(device);
            assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, domain));
            rhi::OriginalResourceRegistrationBudget registry;
            registry.contractVersion = 5u;
            registry.prototypeOptIn = true;
            registry.maxPipelineLayouts = 1u;
            registry.maxComputePipelines = 1u;
            registry.maxRetainedOwners = 11u;
            registry.maxCpuMetadataBytes = bytes;
            assert(!device->prepareOriginalResourceRegistration(registry, metadata));
            const auto before = core::defaultCpuAllocator().stats().allocationCount;
            assert(!device->createOriginalArtifactComputePipeline({}, {1u, true, bytes}, wrapper));
            assert(device->lastOriginalResourceRegistrationDiagnostic()
                == "RHI-ORIGINAL-COMPUTE-CREATION-OWNER");
            assert(!device->createOriginalComputePipeline({}, {}));
            assert(device->lastOriginalResourceRegistrationDiagnostic()
                == "RHI-ACCEPTED-COMPUTE-FACTORY-CREATION-MISSING");
            assert(before == core::defaultCpuAllocator().stats().allocationCount);
            device.reset();
            assert(!domain->liveBytes() && !metadata->liveBytes() && !wrapper->liveBytes());
        }
#endif
        std::puts(
            "BINDLESS_RHI_ACCEPTED_COMPUTE_FACTORY_CONTRACT_OK sealed-l2-receipt backends4 registry5 owners6 canonical-closed");
    }

    static void verifyLoweringAllocator()
    {
        auto package = rhi::test::makePushContractShaderPackage();
        rhi::PipelineLayoutDesc layoutDesc;
        layoutDesc.pushConstantRanges.push_back(
            {rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment | rhi::ShaderStage::Compute,
                0u,
                128u});
        auto device = rhi::createNullDevice();
        auto layout = device->createPipelineLayout(layoutDesc);
        assert(layout);
        rhi::ComputePipelineDesc desc;
        desc.layout = layout.get();
        desc.shaderPackage = &package;
        desc.computeShader = package.entrypoints[2];
        auto quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        for (const auto backend : std::array{rhi::Backend::Null,
                 rhi::Backend::Vulkan,
                 rhi::Backend::Metal,
                 rhi::Backend::WebGPU}) {
            rhi::semantics::NativeBackendCapabilityContract contract;
            contract.backend = backend;
            {
                core::CpuMemoryQuotaScope scope(quota);
                auto valid = rhi::semantics::lowerComputePipelineState(contract, desc);
                assert(valid.success && quota->liveBytes());
                const auto selected = desc.computeShader.stage;
                desc.computeShader.stage = rhi::ShaderStage::None;
                auto invalid = rhi::semantics::lowerComputePipelineState(contract, desc);
                desc.computeShader.stage = selected;
                assert(!invalid.success && !invalid.diagnostics.empty());
            }
            assert(!quota->liveBytes());
        }
        std::pmr::set_default_resource(previous);
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
private:
    using Owner = rhi_webgpu::WebGpuOriginalArtifactComputeCreation;
    using Input = rhi::OriginalSingleEntryPipelineInput;
    using Promises = WebGpuErrorScopeHandoffFixture<1002u>;

    static void wait(const std::shared_ptr<Owner>& receipt)
    {
        assert(WebGpuAsyncWaitFixture::until([&] {
            return receipt->scopesObserved();
        }));
    }

    class Context final {
    public:
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 7> quotas;
        std::shared_ptr<core::CpuMemoryQuota> lossQuota;
        core::UniquePtr<rhi::GpuDevice> device;
        std::shared_ptr<rhi::PipelineLayout> layout;
        std::shared_ptr<Owner> receipt;

        template <class Factory>
        Context(Factory&& create,
            const std::shared_ptr<const Input>& input,
            const rhi::PipelineLayoutDesc& layoutDesc,
            std::uint32_t ownerCount = 11u,
            std::uint32_t version = 5u,
            std::uint32_t pipelineCount = 1u,
            std::uint32_t extraBufferCount = 0u,
            std::uint32_t finiteDescriptorCount = 0u,
            bool overlapLossCreationOwner = false,
            const rhi::OriginalDeviceNativeLossObserverRequest* observedRequest = nullptr,
            std::uint32_t extraTextureCount = 0u,
            std::uint32_t extraSamplerCount = 0u)
        {
            for (auto& q : quotas)
                q = core::makeShared<core::CpuMemoryQuota>(bytes);
            device = create();
            assert(device);
            assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, quotas[0]));
            rhi::OriginalResourceRegistrationBudget registry;
            registry.contractVersion = version;
            registry.prototypeOptIn = true;
            registry.maxPipelineLayouts = 1u;
            registry.maxComputePipelines = pipelineCount;
            registry.maxBuffers = (pipelineCount == 2u ? 1u : 0u) + extraBufferCount;
            registry.maxRetainedOwners = ownerCount
                + extraBufferCount
                    * rhi::OriginalResourceRegistrationContract::perResourceOwnerCount;
            registry.maxCpuMetadataBytes = bytes;
            registry.maxTextures = registry.maxTextureViews = extraTextureCount;
            registry.maxRetainedOwners += 2u * extraTextureCount
                * rhi::OriginalResourceRegistrationContract::perResourceOwnerCount;
            if (extraSamplerCount) {
                registry.samplers = {1u, true, extraSamplerCount};
                registry.maxRetainedOwners += extraSamplerCount
                    * rhi::OriginalResourceRegistrationContract::perResourceOwnerCount;
            }
            if (finiteDescriptorCount) {
                registry.maxDescriptorArenas = 1u;
                registry.maxDescriptorSets = finiteDescriptorCount;
                registry.maxRetainedOwners +=
                    rhi::OriginalResourceRegistrationContract::perResourceOwnerCount
                    + finiteDescriptorCount
                        * rhi::OriginalResourceRegistrationContract::perDescriptorOwnerCount;
            }
            if (version == rhi::OriginalTypedNativeLossJoinContract::registrationVersion) {
                lossQuota = overlapLossCreationOwner
                    ? quotas[3]
                    : core::makeShared<core::CpuMemoryQuota>(bytes);
                const rhi::OriginalDeviceNativeLossBudget lossBudget{{1u, true, 1u, 1u, bytes}, 1u};
                assert(!(observedRequest
                        ? device->prepareOriginalDeviceNativeLoss(
                              lossBudget, *observedRequest, lossQuota)
                        : device->prepareOriginalDeviceNativeLoss(lossBudget, lossQuota)));
                const char* diagnostic = nullptr;
                auto loss = device->observeOriginalDeviceNativeLoss(diagnostic);
                assert(loss && !diagnostic);
                assert(!device->prepareOriginalTypedNativeLossRegistration(
                    {1u, true}, registry, quotas[1], *loss));
            } else
                assert(!device->prepareOriginalResourceRegistration(registry, quotas[1]));
            {
                core::CpuMemoryQuotaScope scope(quotas[2]);
                layout = core::shareUnique(device->createOriginalPipelineLayout(layoutDesc));
            }
            assert(layout);
            Owner::Budget b{1u,
                true,
                Owner::callbackCount,
                64u,
                512u,
                bytes,
                bytes,
                {1u,
                    true,
                    input->artifact()->bytes(shader::ShaderArtifactRole::Wgsl).size(),
                    bytes}};
            const char* diagnostic = nullptr;
            receipt = Owner::prepareOriginal(input,
                layout,
                "accepted original L2 compute factory",
                b,
                quotas[3],
                quotas[4],
                quotas[5],
                diagnostic);
            assert(receipt);
        }

        auto publish()
        {
            return device->createOriginalArtifactComputePipeline(
                receipt, {1u, true, bytes}, quotas[6]);
        }

        void ready()
        {
            assert(!Owner::initialize(receipt));
            wait(receipt);
            assert(!receipt->validate());
        }

        void retire()
        {
            receipt.reset();
            layout.reset();
            device.reset();
            for (const auto& q : quotas)
                assert(!q->liveBytes());
            if (lossQuota)
                assert(!lossQuota->liveBytes());
        }
    };

public:
    using PreparedContext = Context;

    template <class Factory>
    static auto prepareContext(Factory&& create,
        const std::shared_ptr<const Input>& input,
        const rhi::PipelineLayoutDesc& layoutDesc,
        std::uint32_t extraBufferCount = 0u,
        std::uint32_t finiteDescriptorCount = 0u,
        std::uint32_t version = 5u,
        const rhi::OriginalDeviceNativeLossObserverRequest* observedRequest = nullptr)
    {
        return core::makeUnique<Context>(std::forward<Factory>(create),
            input,
            layoutDesc,
            11u,
            version,
            1u,
            extraBufferCount,
            finiteDescriptorCount,
            false,
            observedRequest);
    }

    template <class Factory>
    static void verifyActual(Factory&& create,
        const std::shared_ptr<const Input>& input,
        const rhi::PipelineLayoutDesc& layoutDesc)
    {
        {
            auto inner = create();
            assert(inner);
            const auto proxy = [&] {
                return core::makeUnique<tests::CapabilityOverrideDevice>(*inner, nullptr);
            };
            Context sealed(proxy, input, layoutDesc);
            sealed.ready();
            OriginalAcceptedPipelineWrapperSealFixture::verifyActual<rhi::ComputePipeline, Owner>(
                sealed);
            sealed.retire();
        }
        {
            Context assembly(create, input, layoutDesc, 19u, 6u, 2u);
            assembly.ready();
            OriginalCommandPipelineSourceFixture::verifyActual<rhi::ComputePipeline>(
                *assembly.device,
                [&] {
                    return assembly.publish();
                },
                assembly.quotas,
                create);
            assembly.retire();
        }
        Context c(create, input, layoutDesc);
        const auto expected = [&](const char* id) {
            assert(c.device->lastOriginalResourceRegistrationDiagnostic() == id);
        };
        assert(!c.publish());
        expected("RHI-ARTIFACT-CREATION-UNINITIALIZED");
        Promises::holdScopes();
        assert(!Owner::initialize(c.receipt));
        Promises::restoreScopes();
        Promises::waitHeldScopes();
        assert(!c.publish());
        expected("RHI-ARTIFACT-CREATION-PENDING");
        Promises::releaseScopes();
        wait(c.receipt);
        assert(!c.receipt->validate());
        auto genuine = c.receipt;
        c.receipt = std::shared_ptr<Owner>(c.quotas[3], genuine.get());
        assert(!c.publish());
        expected("RHI-ORIGINAL-COMPUTE-CREATION-OWNER");
        c.receipt = genuine;
        genuine.reset();
        assert(
            !c.device->createOriginalArtifactComputePipeline({}, {1u, true, bytes}, c.quotas[6]));
        expected("RHI-ORIGINAL-COMPUTE-CREATION-OWNER");
        assert(!c.device->createOriginalComputePipeline(c.receipt->definition(), c.layout));
        expected("RHI-ACCEPTED-COMPUTE-FACTORY-CREATION-MISSING");
        assert(!c.device->createOriginalArtifactComputePipeline(
            c.receipt, {1u, true, bytes}, c.quotas[4]));
        expected("RHI-ACCEPTED-COMPUTE-FACTORY-QUOTA-DOMAIN");
        auto foreign = create();
        assert(foreign);
        auto lifetime = core::makeShared<core::CpuMemoryQuota>(bytes),
             metadata = core::makeShared<core::CpuMemoryQuota>(bytes);
        assert(!foreign->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, lifetime));
        rhi::OriginalResourceRegistrationBudget b;
        b.contractVersion = 5u;
        b.prototypeOptIn = true;
        b.maxPipelineLayouts = 1u;
        b.maxComputePipelines = 1u;
        b.maxRetainedOwners = 11u;
        b.maxCpuMetadataBytes = bytes;
        assert(!foreign->prepareOriginalResourceRegistration(b, metadata));
        assert(!foreign->createOriginalArtifactComputePipeline(
            c.receipt, {1u, true, bytes}, c.quotas[6]));
        assert(foreign->lastOriginalResourceRegistrationDiagnostic()
            == "RHI-ORIGINAL-REGISTRATION-FOREIGN-DEVICE");
        foreign.reset();
        assert(!metadata->liveBytes() && !lifetime->liveBytes());
        // Backend-private callers must also match the exact accepted definition.
        {
            auto changed = rhi::ComputePipelineStateCopy::copy(c.receipt->definition());
            const char* diagnostic = nullptr;
            changed.label = "different accepted label";
            assert(!rhi_webgpu::WebGpuAcceptedOriginalComputePipelineFactory::create(
                changed, c.receipt, c.quotas[6], diagnostic));
            assert(std::string_view(diagnostic) == "RHI-ACCEPTED-COMPUTE-FACTORY-DEFINITION");
            changed = rhi::ComputePipelineStateCopy::copy(c.receipt->definition());
            ++changed.computeShader.computeWorkgroupSizeX;
            assert(!rhi_webgpu::WebGpuAcceptedOriginalComputePipelineFactory::create(
                changed, c.receipt, c.quotas[6], diagnostic));
            assert(std::string_view(diagnostic) == "RHI-ACCEPTED-COMPUTE-FACTORY-DEFINITION");
        }
        // clang-format off
        EM_ASM({
            // clang-format off
                const d = Module.preinitializedWebGPUDevice;
                Module.arpAcceptedShaderOriginal = d.createShaderModule;
                Module.arpAcceptedComputeOriginal = d.createComputePipeline;
                Module.arpAcceptedRecreations = 0;
                d.createShaderModule = function(s) {
                    Module.arpAcceptedRecreations++;
                    return Module.arpAcceptedShaderOriginal.call(this, s);
                };
                d.createComputePipeline = function(s) {
                    Module.arpAcceptedRecreations++;
                    return Module.arpAcceptedComputeOriginal.call(this, s);
                };
            // clang-format on
        });
        // clang-format on
        const auto factory = [&] {
            return c.publish();
        };
        std::array<std::uint64_t, 2> peaks{};
        for (std::size_t i = 0u; i < peaks.size(); ++i) {
            const auto index = i == 0u ? 1u : 6u;
            CpuQuotaFactoryFixture pressure(c.quotas[index]);
            peaks[i] = pressure.measure(factory);
            pressure.verify(peaks[i], factory);
            {
                core::CpuMemoryQuotaLease full(
                    c.quotas[index], bytes - c.quotas[index]->liveBytes());
                assert(full.charged() && !factory());
            }
            assert(!c.receipt->validate());
            assert(factory());
        }
        OriginalDescriptorBufferBindingFixture::verifyCaptures(factory, "compute");
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        auto pipeline = factory();
        std::pmr::set_default_resource(previous);
        if (!pipeline)
            std::fprintf(stderr,
                "accepted compute publish failed %s\n",
                c.device->lastOriginalResourceRegistrationDiagnostic().data());
        assert(pipeline);
        assert(!pipeline->originalArtifactComputeDiagnostic()
            && !c.device->validateOriginalResourceRegistration(*pipeline));
        assert(
            core::SharedOwnerProjection::sameOwner(pipeline->originalComputeCreation(), c.receipt));
        const auto* definition = pipeline->originalComputePipelineDefinition();
        assert(definition && definition->layout == c.layout.get()
            && definition->shaderPackage != &input->package());
        assert(definition->label == c.receipt->definition().label
            && definition->computeShader.entryPoint == input->artifact()->entry());
        assert(core::inspectCpuAllocationContaining(definition).quota == c.quotas[1].get());
        assert(core::inspectCpuAllocationContaining(pipeline.get()).quota == c.quotas[6].get());
        auto native = rhi_webgpu::webGpuNativePayloadFor(*pipeline);
        assert(native
            && !rhi_webgpu::WebGpuOriginalArtifactComputeFactory::validateConsumedOriginal(native));
        assert(core::SharedOwnerProjection::sameOwner(
            native->issuedContract()->artifactInput(), input));
        native.reset();
        // clang-format off
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpAcceptedRecreations === 0;
            // clang-format on
        }) == 1);
        // clang-format on
        // clang-format off
        EM_ASM({
            // clang-format off
                const d = Module.preinitializedWebGPUDevice;
                d.createShaderModule = Module.arpAcceptedShaderOriginal;
                d.createComputePipeline = Module.arpAcceptedComputeOriginal;
                delete Module.arpAcceptedShaderOriginal;
                delete Module.arpAcceptedComputeOriginal;
            // clang-format on
        });
        // clang-format on
        c.receipt.reset();
        c.layout.reset();
        assert(!pipeline->originalArtifactComputeDiagnostic());
        OriginalDescriptorBufferBindingFixture::verifyActual(pipeline, c.quotas);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, [&]() noexcept {
            return pipeline->originalArtifactComputeDiagnostic();
        }));
        c.device->retireOriginalDeviceLifetime();
        assert(pipeline->originalArtifactComputeDiagnostic());
        assert(pipeline->originalDescriptorBufferBindingDiagnostic(
            {1u, true}, rhi::DescriptorType::UniformBuffer, 64u, 0u));
        pipeline.reset();
        c.retire();
        for (const auto count : std::array<std::uint32_t, 2>{10u, 11u}) {
            Context capacity(create, input, layoutDesc, count);
            capacity.ready();
            auto result = capacity.publish();
            assert(bool(result) == (count == 11u));
            if (!result)
                assert(capacity.device->lastOriginalResourceRegistrationDiagnostic()
                    == "RHI-ORIGINAL-REGISTRATION-OWNER-CAPACITY");
            result.reset();
            capacity.retire();
        }
        {
            Context older(create, input, layoutDesc, 11u, 4u);
            older.ready();
            assert(!older.publish());
            assert(older.device->lastOriginalResourceRegistrationDiagnostic()
                == "RHI-ACCEPTED-COMPUTE-FACTORY-REGISTRY-VERSION");
            older.retire();
        }
        {
            Context newer(create, input, layoutDesc, 11u, 6u);
            newer.ready();
            auto pipeline = newer.publish();
            assert(pipeline && !pipeline->originalArtifactComputeDiagnostic());
            pipeline.reset();
            newer.retire();
        }
        {
            Context rejected(create, input, layoutDesc);
            // clang-format off
            EM_ASM({
                // clang-format off
                    Module.arpAcceptedBad = Module.preinitializedWebGPUDevice.createComputePipeline;
                    Module.preinitializedWebGPUDevice.createComputePipeline = function(d) {
                        return Module.arpAcceptedBad.call(this, Object.assign({}, d, {
                            compute : Object.assign({}, d.compute, {entryPoint : 'missingAcceptedOriginalEntry'})
                        }));
                    };
                // clang-format on
            });
            // clang-format on
            Owner::initialize(rejected.receipt);
            // clang-format off
            EM_ASM({
                // clang-format off
                    Module.preinitializedWebGPUDevice.createComputePipeline = Module.arpAcceptedBad;
                    delete Module.arpAcceptedBad;
                // clang-format on
            });
            // clang-format on
            wait(rejected.receipt);
            assert(!rejected.publish() && rejected.receipt->status() == Owner::Status::Rejected);
            rejected.retire();
        }
        loss_ = core::makeUnique<Context>(create, input, layoutDesc);
        // clang-format off
        EM_ASM({
            // clang-format off
                const d = Module.preinitializedWebGPUDevice;
                Module.arpAcceptedLossObjects = [];
                Module.arpAcceptedLossCompute = d.createComputePipeline;
                Module.arpAcceptedLossLayout = d.createPipelineLayout;
                d.createComputePipeline = function(desc) {
                    const p = Module.arpAcceptedLossCompute.call(this, desc);
                    Module.arpAcceptedLossObjects.push(p);
                    return p;
                };
                d.createPipelineLayout = function(desc) {
                    const p = Module.arpAcceptedLossLayout.call(this, desc);
                    Module.arpAcceptedLossObjects.push(p);
                    return p;
                };
            // clang-format on
        });
        // clang-format on
        loss_->ready();
        const char* publicationDiagnostic = nullptr;
        lossPipeline_ = rhi::OriginalRegisteredPipeline::shareAcceptedOriginal(
            loss_->publish(), publicationDiagnostic);
        assert(lossPipeline_);
        OriginalCommandPipelineSourceFixture::retainForLoss(
            lossPipeline_, *loss_->device, loss_->quotas[2]);
        // clang-format off
        assert(EM_ASM_INT({
            // clang-format off
                const wrappers = Object.values(WebGPU.Internals.jsObjects);
                return Module.arpAcceptedLossObjects.length === 2 &&
                       Module.arpAcceptedLossObjects.every(p => wrappers.includes(p));
            // clang-format on
        }) == 1);
        // clang-format on
        // clang-format off
        EM_ASM({
            // clang-format off
                const d = Module.preinitializedWebGPUDevice;
                d.createComputePipeline = Module.arpAcceptedLossCompute;
                d.createPipelineLayout = Module.arpAcceptedLossLayout;
                delete Module.arpAcceptedLossCompute;
                delete Module.arpAcceptedLossLayout;
            // clang-format on
        });
        // clang-format on
        std::printf(
            "BINDLESS_RHI_ACCEPTED_COMPUTE_FACTORY_NATIVE_OK %llu %llu registry5 original-layout-definition-receipt exact-payload no-recreation owners11-less10 cold-exact-less-exhausted-retry hot-allocation0 canonical-closed\n",
            static_cast<unsigned long long>(peaks[0]),
            static_cast<unsigned long long>(peaks[1]));
    }

    static void verifyActualLoss()
    {
        assert(loss_ && lossPipeline_ && rhi_webgpu::webGpuNativeDeviceLost());
        assert(std::string_view(lossPipeline_->originalArtifactComputeDiagnostic())
            == "RHI-ARTIFACT-CREATION-DEVICE-LOST");
        assert(!loss_->publish()
            && loss_->device->lastOriginalResourceRegistrationDiagnostic()
                == "RHI-ARTIFACT-CREATION-DEVICE-LOST");
        OriginalDescriptorBufferBindingFixture::verifyLoss(lossPipeline_);
        OriginalCommandPipelineSourceFixture::verifyLoss<rhi::ComputePipeline>();
        lossPipeline_.reset();
        loss_->retire();
        loss_.reset();
        // clang-format off
        assert(EM_ASM_INT({
            // clang-format off
                const wrappers = Object.values(WebGPU.Internals.jsObjects);
                return Module.arpAcceptedLossObjects.length === 2 &&
                       !Module.arpAcceptedLossObjects.some(p => wrappers.includes(p));
            // clang-format on
        }) == 1);
        // clang-format on
        // clang-format off
        EM_ASM({
            // clang-format off
                delete Module.arpAcceptedLossObjects;
            // clang-format on
        });
        // clang-format on
        std::puts(
            "BINDLESS_RHI_ACCEPTED_COMPUTE_FACTORY_LOSS_OK actual-native-loss reject-wrapper-and-factory quotas7-zero native-wrappers2-zero");
    }

private:
    inline static core::UniquePtr<Context> loss_;
    inline static std::shared_ptr<rhi::ComputePipeline> lossPipeline_;
#endif
};
} // namespace arp::test
