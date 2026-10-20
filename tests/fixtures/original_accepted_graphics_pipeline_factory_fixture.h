#include "original_descriptor_buffer_binding_fixture.h"
#pragma once
#include "../rhi_shader_package_test_helpers.h"
#include "original_accepted_pipeline_wrapper_seal_fixture.h"
#include "original_artifact_graphics_creation_fixture.h"
#include "original_command_pipeline_source_fixture.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/webgpu/webgpu_pipeline_preparation.h"
#include "rhi/frontend/pipeline_creation_validation.h"
#include "rhi/frontend/pipeline_definition_identity.h"
#include "rhi/gpu_device.h"
#include "rhi/semantics/native_pipeline_state_lowering.h"

namespace arp::test {
class OriginalAcceptedGraphicsPipelineFactoryFixture final {
    using Contract = rhi::OriginalAcceptedGraphicsPipelineFactoryContract;
    static constexpr std::uint64_t bytes = 65536u;

public:
    static void verifyContract()
    {
        static_assert(!std::is_default_constructible_v<rhi::OriginalGraphicsPipelineCreation>);
        static_assert(!std::is_copy_constructible_v<rhi::OriginalGraphicsPipelineCreation>);
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
            const auto registry = rhi::OriginalResourceRegistrationContract::lowering(backend, 6u);
            assert(registry.fullPipelineDefinitionCustodyAvailable && !registry.executionAvailable);
        }
        assert(!Contract::lowering(static_cast<rhi::Backend>(255u), true).factoryEligible);
        assert(rhi::OriginalGraphicsPipelineCreation::validateOriginal({}));
        assert(rhi::OriginalResourceRegistrationContract::ownerCount(
                   rhi::OriginalRegisteredResourceKind::GraphicsPipeline, 6u)
            == 6u);
        assert(rhi::OriginalResourceRegistrationContract::ownerCount(
                   rhi::OriginalRegisteredResourceKind::ComputePipeline, 6u)
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
            registry.contractVersion = 6u;
            registry.prototypeOptIn = true;
            registry.maxPipelineLayouts = 1u;
            registry.maxGraphicsPipelines = 1u;
            registry.maxRetainedOwners = 11u;
            registry.maxCpuMetadataBytes = bytes;
            assert(!device->prepareOriginalResourceRegistration(registry, metadata));
            const auto before = core::defaultCpuAllocator().stats().allocationCount;
            assert(!device->createOriginalArtifactGraphicsPipeline({}, {1u, true, bytes}, wrapper));
            assert(device->lastOriginalResourceRegistrationDiagnostic()
                == "RHI-ORIGINAL-GRAPHICS-CREATION-OWNER");
            assert(!device->createOriginalRenderPipeline({}, {}));
            assert(device->lastOriginalResourceRegistrationDiagnostic()
                == "RHI-ACCEPTED-GRAPHICS-FACTORY-CREATION-MISSING");
            assert(before == core::defaultCpuAllocator().stats().allocationCount);
            device.reset();
            assert(!domain->liveBytes() && !metadata->liveBytes() && !wrapper->liveBytes());
        }
#endif
        std::puts(
            "BINDLESS_RHI_ACCEPTED_GRAPHICS_FACTORY_CONTRACT_OK sealed-l2-receipt backends4 registry6 owners6 canonical-closed");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
private:
    using Owner = rhi_webgpu::WebGpuOriginalArtifactGraphicsCreation;
    using Input = rhi::OriginalGraphicsPipelineInput;
    using Promises = WebGpuErrorScopeHandoffFixture<1004u>;

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
            const rhi::RenderPipelineDesc& state,
            std::uint32_t ownerCount = 11u,
            std::uint32_t version = 6u,
            std::uint32_t pipelineCount = 1u,
            bool positionalAttachments = false,
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
            registry.maxGraphicsPipelines = pipelineCount;
            registry.maxBuffers = (pipelineCount == 2u ? 1u : 0u) + extraBufferCount;
            registry.maxTextures = positionalAttachments ? 1u : 0u;
            registry.maxTextureViews = positionalAttachments ? 1u : 0u;
            registry.maxRetainedOwners = ownerCount
                + extraBufferCount
                    * rhi::OriginalResourceRegistrationContract::perResourceOwnerCount
                + (positionalAttachments
                        ? 2u * rhi::OriginalResourceRegistrationContract::perResourceOwnerCount
                        : 0u);
            registry.maxCpuMetadataBytes = bytes;
            registry.maxTextures += extraTextureCount;
            registry.maxTextureViews += extraTextureCount;
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
            auto authored = rhi::RenderPipelineStateCopy::copy(state);
            authored.label = "accepted original L2 graphics factory";
            authored.layout = layout.get();
            authored.shaderPackage = &input->package();
            const char* diagnostic = nullptr;
            receipt = Owner::prepareOriginal(
                input, layout, authored, b, quotas[3], quotas[4], quotas[5], diagnostic);
            if (!receipt) {
                core::pmr::String definitionDiagnostic;
                rhi_webgpu::WebGpuOriginalArtifactGraphicsFactory::validateDefinition(
                    input, authored, definitionDiagnostic);
                std::fprintf(stderr,
                    "accepted Graphics preparation: %s; definition: %s\n",
                    diagnostic ? diagnostic : "missing diagnostic",
                    definitionDiagnostic.c_str());
            }
            assert(receipt);
        }

        auto publish()
        {
            return device->createOriginalArtifactGraphicsPipeline(
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
        const rhi::RenderPipelineDesc& state,
        std::uint32_t extraBufferCount = 0u,
        bool positionalAttachments = false,
        std::uint32_t finiteDescriptorCount = 0u,
        std::uint32_t version = 6u,
        const rhi::OriginalDeviceNativeLossObserverRequest* observedRequest = nullptr,
        std::uint32_t extraTextureCount = 0u,
        std::uint32_t extraSamplerCount = 0u)
    {
        return core::makeUnique<Context>(std::forward<Factory>(create),
            input,
            layoutDesc,
            state,
            11u,
            version,
            1u,
            positionalAttachments,
            extraBufferCount,
            finiteDescriptorCount,
            false,
            observedRequest,
            extraTextureCount,
            extraSamplerCount);
    }

    // Run the same original geometry context directly from the browser harness.
    // Its asynchronous initialization must not retain verifyActual's unrelated
    // factory negative-case frame. No owner, registry or continuation limit changes.
    template <class Factory>
    static void verifyGeometryActual(Factory&& create,
        const std::shared_ptr<const Input>& input,
        const rhi::PipelineLayoutDesc& layoutDesc,
        const rhi::RenderPipelineDesc& state)
    {
        const rhi::OriginalDeviceNativeLossObserverRequest observers{1u, true, 3u, 4u, bytes};
        Context geometry(create,
            input,
            layoutDesc,
            state,
            19u,
            rhi::OriginalTypedNativeLossJoinContract::registrationVersion,
            2u,
            true,
            0u,
            OriginalCommandPipelineBindingFixture::indexedDescriptorCount,
            false,
            &observers);
        geometry.ready();
        OriginalCommandPipelineBindingFixture::verifyGeometryGraphics(
            *geometry.device,
            [&] {
                return geometry.publish();
            },
            geometry.quotas[2]);
        OriginalCommandPipelineBindingFixture::verifyGeometryGraphics(
            *geometry.device,
            [&] {
                return geometry.publish();
            },
            geometry.quotas[2],
            false);
        geometry.retire();
    }

    template <class Factory>
    static void verifyStreamGeometryActual(Factory&& create,
        const std::shared_ptr<const Input>& input,
        const rhi::PipelineLayoutDesc& layoutDesc,
        const rhi::RenderPipelineDesc& state,
        OriginalVertexGeometryExecutionFixture::LifecycleCase mode =
            OriginalVertexGeometryExecutionFixture::LifecycleCase::Complete)
    {
        const rhi::OriginalDeviceNativeLossObserverRequest observers{1u, true, 3u, 4u, bytes};
        Context geometry(create,
            input,
            layoutDesc,
            state,
            19u,
            rhi::OriginalTypedNativeLossJoinContract::registrationVersion,
            2u,
            true,
            3u,
            1u,
            false,
            &observers);
        geometry.ready();
        OriginalVertexGeometryExecutionFixture::verify(
            *geometry.device,
            [&] {
                return geometry.publish();
            },
            geometry.quotas[2],
            mode);
        geometry.retire();
    }

    template <class Factory>
    static void verifyActual(Factory&& create,
        const std::shared_ptr<const Input>& input,
        const rhi::PipelineLayoutDesc& layoutDesc,
        const rhi::RenderPipelineDesc& state)
    {
        {
            auto inner = create();
            assert(inner);
            const auto proxy = [&] {
                return core::makeUnique<tests::CapabilityOverrideDevice>(*inner, nullptr);
            };
            Context sealed(proxy, input, layoutDesc, state);
            sealed.ready();
            OriginalAcceptedPipelineWrapperSealFixture::verifyActual<rhi::RenderPipeline, Owner>(
                sealed);
            sealed.retire();
        }
        {
            Context assembly(create, input, layoutDesc, state, 19u, 6u, 2u);
            assembly.ready();
            OriginalCommandPipelineSourceFixture::verifyActual<rhi::RenderPipeline>(
                *assembly.device,
                [&] {
                    return assembly.publish();
                },
                assembly.quotas,
                create);
            assembly.retire();
        }
        {
            Context positional(create, input, layoutDesc, state, 19u, 6u, 2u, true);
            positional.ready();
            OriginalCommandPipelineBindingFixture::verifyGraphics(
                *positional.device,
                [&] {
                    return positional.publish();
                },
                positional.quotas[2]);
            const char* diagnostic = nullptr;
            assert(!positional.device->observeOriginalDeviceNativeLoss(diagnostic));
            assert(diagnostic
                && std::string_view(diagnostic) == "RHI-ORIGINAL-DEVICE-LOSS-UNPREPARED");
            positional.retire();
        }
        Context c(create, input, layoutDesc, state);
        const auto expected = [&](const char* id) {
            assert(c.device->lastOriginalResourceRegistrationDiagnostic() == id);
        };
        assert(!c.publish());
        expected("RHI-GRAPHICS-CREATION-UNINITIALIZED");
        Promises::holdScopes();
        assert(!Owner::initialize(c.receipt));
        Promises::restoreScopes();
        Promises::waitHeldScopes();
        assert(!c.publish());
        expected("RHI-GRAPHICS-CREATION-PENDING");
        Promises::releaseScopes();
        wait(c.receipt);
        assert(!c.receipt->validate());
        auto genuine = c.receipt;
        c.receipt = std::shared_ptr<Owner>(c.quotas[3], genuine.get());
        assert(!c.publish());
        expected("RHI-ORIGINAL-GRAPHICS-CREATION-OWNER");
        c.receipt = genuine;
        genuine.reset();
        assert(
            !c.device->createOriginalArtifactGraphicsPipeline({}, {1u, true, bytes}, c.quotas[6]));
        expected("RHI-ORIGINAL-GRAPHICS-CREATION-OWNER");
        assert(!c.device->createOriginalRenderPipeline(c.receipt->definition(), c.layout));
        expected("RHI-ACCEPTED-GRAPHICS-FACTORY-CREATION-MISSING");
        assert(!c.device->createOriginalArtifactGraphicsPipeline(
            c.receipt, {1u, true, bytes}, c.quotas[4]));
        expected("RHI-ACCEPTED-GRAPHICS-FACTORY-QUOTA-DOMAIN");
        auto foreign = create();
        assert(foreign);
        auto lifetime = core::makeShared<core::CpuMemoryQuota>(bytes),
             metadata = core::makeShared<core::CpuMemoryQuota>(bytes);
        assert(!foreign->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, lifetime));
        rhi::OriginalResourceRegistrationBudget b;
        b.contractVersion = 6u;
        b.prototypeOptIn = true;
        b.maxPipelineLayouts = 1u;
        b.maxGraphicsPipelines = 1u;
        b.maxRetainedOwners = 11u;
        b.maxCpuMetadataBytes = bytes;
        assert(!foreign->prepareOriginalResourceRegistration(b, metadata));
        assert(!foreign->createOriginalArtifactGraphicsPipeline(
            c.receipt, {1u, true, bytes}, c.quotas[6]));
        assert(foreign->lastOriginalResourceRegistrationDiagnostic()
            == "RHI-ORIGINAL-REGISTRATION-FOREIGN-DEVICE");
        foreign.reset();
        assert(!metadata->liveBytes() && !lifetime->liveBytes());
        // Backend-private callers must also match the exact accepted definition.
        {
            auto changed = rhi::RenderPipelineStateCopy::copy(c.receipt->definition());
            const char* diagnostic = nullptr;
            changed.label = "different accepted label";
            assert(!rhi_webgpu::WebGpuAcceptedOriginalGraphicsPipelineFactory::create(
                changed, c.receipt, c.quotas[6], diagnostic));
            assert(std::string_view(diagnostic) == "RHI-ACCEPTED-GRAPHICS-FACTORY-DEFINITION");
            changed = rhi::RenderPipelineStateCopy::copy(c.receipt->definition());
            ++changed.multisample.sampleMask;
            assert(!rhi_webgpu::WebGpuAcceptedOriginalGraphicsPipelineFactory::create(
                changed, c.receipt, c.quotas[6], diagnostic));
            assert(std::string_view(diagnostic) == "RHI-ACCEPTED-GRAPHICS-FACTORY-DEFINITION");
        }
        // clang-format off
        EM_ASM({
            // clang-format off
                const d = Module.preinitializedWebGPUDevice;
                Module.arpAcceptedGraphicsShaderOriginal = d.createShaderModule;
                Module.arpAcceptedGraphicsGraphicsOriginal = d.createRenderPipeline;
                Module.arpAcceptedGraphicsRecreations = 0;
                d.createShaderModule = function(s) {
                    Module.arpAcceptedGraphicsRecreations++;
                    return Module.arpAcceptedGraphicsShaderOriginal.call(this, s);
                };
                d.createRenderPipeline = function(s) {
                    Module.arpAcceptedGraphicsRecreations++;
                    return Module.arpAcceptedGraphicsGraphicsOriginal.call(this, s);
                };
            // clang-format on
        });
        // clang-format on
        auto first = c.publish();
        if (!first)
            std::fprintf(stderr,
                "accepted graphics first publish failed %s\n",
                c.device->lastOriginalResourceRegistrationDiagnostic().data());
        assert(first);
        auto firstNative = rhi_webgpu::webGpuNativePayloadFor(*first);
        assert(firstNative);
        first.reset(); // The authored registry has capacity for one wrapper.
        auto second = c.publish();
        assert(second);
        assert(core::SharedOwnerProjection::sameOwner(
            firstNative, rhi_webgpu::webGpuNativePayloadFor(*second)));
        firstNative.reset();
        first.reset();
        second.reset();
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
        OriginalDescriptorBufferBindingFixture::verifyCaptures(factory, "graphics");
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        auto pipeline = factory();
        std::pmr::set_default_resource(previous);
        if (!pipeline)
            std::fprintf(stderr,
                "accepted graphics publish failed %s\n",
                c.device->lastOriginalResourceRegistrationDiagnostic().data());
        assert(pipeline);
        assert(!pipeline->originalArtifactGraphicsDiagnostic()
            && !c.device->validateOriginalResourceRegistration(*pipeline));
        assert(core::SharedOwnerProjection::sameOwner(
            pipeline->originalGraphicsCreation(), c.receipt));
        const auto* definition = pipeline->originalRenderPipelineDefinition();
        assert(definition && definition->layout == c.layout.get()
            && definition->shaderPackage != &input->package());
        assert(definition->label == c.receipt->definition().label
            && definition->vertexShader.matchesDefinition(input->package().entrypoints[0])
            && definition->fragmentShader.matchesDefinition(input->package().entrypoints[1]));
        assert(core::inspectCpuAllocationContaining(definition).quota == c.quotas[1].get());
        assert(core::inspectCpuAllocationContaining(pipeline.get()).quota == c.quotas[6].get());
        auto native = rhi_webgpu::webGpuNativePayloadFor(*pipeline);
        assert(native
            && !rhi_webgpu::WebGpuOriginalArtifactGraphicsFactory::validateConsumedOriginal(
                native));
        assert(core::SharedOwnerProjection::sameOwner(
            native->issuedContract()->graphicsInput(), input));
        native.reset();
        // clang-format off
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpAcceptedGraphicsRecreations === 0;
            // clang-format on
        }) == 1);
        // clang-format on
        // clang-format off
        EM_ASM({
            // clang-format off
                const d = Module.preinitializedWebGPUDevice;
                d.createShaderModule = Module.arpAcceptedGraphicsShaderOriginal;
                d.createRenderPipeline = Module.arpAcceptedGraphicsGraphicsOriginal;
                delete Module.arpAcceptedGraphicsShaderOriginal;
                delete Module.arpAcceptedGraphicsGraphicsOriginal;
            // clang-format on
        });
        // clang-format on
        c.receipt.reset();
        c.layout.reset();
        assert(!pipeline->originalArtifactGraphicsDiagnostic());
        OriginalDescriptorBufferBindingFixture::verifyActual(pipeline, c.quotas);
        assert(rhi::PipelineDefinitionIdentity::matches(
            *definition, pipeline->originalGraphicsCreation()->definition()));
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, [&]() noexcept {
            return pipeline->originalArtifactGraphicsDiagnostic();
        }));
        c.device->retireOriginalDeviceLifetime();
        assert(pipeline->originalArtifactGraphicsDiagnostic());
        assert(pipeline->originalDescriptorBufferBindingDiagnostic(
            {1u, true}, rhi::DescriptorType::UniformBuffer, 64u, 0u));
        pipeline.reset();
        c.retire();
        for (const auto count : std::array<std::uint32_t, 2>{10u, 11u}) {
            Context capacity(create, input, layoutDesc, state, count);
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
            Context older(create, input, layoutDesc, state, 11u, 5u);
            older.ready();
            assert(!older.publish());
            assert(older.device->lastOriginalResourceRegistrationDiagnostic()
                == "RHI-ACCEPTED-GRAPHICS-FACTORY-REGISTRY-VERSION");
            older.retire();
        }
        {
            Context rejected(create, input, layoutDesc, state);
            // clang-format off
            EM_ASM({
                // clang-format off
                    Module.arpAcceptedGraphicsBad = Module.preinitializedWebGPUDevice.createRenderPipeline;
                    Module.preinitializedWebGPUDevice.createRenderPipeline = function(d) {
                        return Module.arpAcceptedGraphicsBad.call(this, Object.assign({}, d, {
                            fragment :
                                Object.assign({}, d.fragment, {entryPoint : 'missingAcceptedOriginalGraphicsEntry'})
                        }));
                    };
                // clang-format on
            });
            // clang-format on
            Owner::initialize(rejected.receipt);
            // clang-format off
            EM_ASM({
                // clang-format off
                    Module.preinitializedWebGPUDevice.createRenderPipeline = Module.arpAcceptedGraphicsBad;
                    delete Module.arpAcceptedGraphicsBad;
                // clang-format on
            });
            // clang-format on
            wait(rejected.receipt);
            assert(!rejected.publish() && rejected.receipt->status() == Owner::Status::Rejected);
            rejected.retire();
        }
        loss_ = core::makeUnique<Context>(create, input, layoutDesc, state, 11u, 6u, 1u, true);
        // clang-format off
        EM_ASM({
            // clang-format off
                const d = Module.preinitializedWebGPUDevice;
                Module.arpAcceptedGraphicsLossObjects = [];
                Module.arpPipelinePositionalAttachmentObjects = [];
                Module.arpPipelinePositionalCreateTexture = d.createTexture;
                d.createTexture = function(desc) {
                    const p = Module.arpPipelinePositionalCreateTexture.call(this, desc);
                    Module.arpPipelinePositionalAttachmentObjects.push(p);
                    const createView = p.createView;
                    p.createView = function(desc) {
                        const v = createView.call(this, desc);
                        Module.arpPipelinePositionalAttachmentObjects.push(v);
                        return v;
                    };
                    return p;
                };
                Module.arpAcceptedGraphicsLossGraphics = d.createRenderPipeline;
                Module.arpAcceptedGraphicsLossLayout = d.createPipelineLayout;
                d.createRenderPipeline = function(desc) {
                    const p = Module.arpAcceptedGraphicsLossGraphics.call(this, desc);
                    Module.arpAcceptedGraphicsLossObjects.push(p);
                    return p;
                };
                d.createPipelineLayout = function(desc) {
                    const p = Module.arpAcceptedGraphicsLossLayout.call(this, desc);
                    Module.arpAcceptedGraphicsLossObjects.push(p);
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
                return Module.arpAcceptedGraphicsLossObjects.length === 2 &&
                       Module.arpAcceptedGraphicsLossObjects.every(p => wrappers.includes(p));
            // clang-format on
        }) == 1);
        // clang-format on
        // clang-format off
        assert(EM_ASM_INT({
            // clang-format off
                const wrappers = Object.values(WebGPU.Internals.jsObjects);
                return Module.arpPipelinePositionalAttachmentObjects.length === 2 &&
                       Module.arpPipelinePositionalAttachmentObjects.every(p => wrappers.includes(p));
            // clang-format on
        }) == 1);
        // clang-format on
        // clang-format off
        EM_ASM({
            // clang-format off
                const d = Module.preinitializedWebGPUDevice;
                d.createRenderPipeline = Module.arpAcceptedGraphicsLossGraphics;
                d.createPipelineLayout = Module.arpAcceptedGraphicsLossLayout;
                d.createTexture = Module.arpPipelinePositionalCreateTexture;
                delete Module.arpPipelinePositionalCreateTexture;
                delete Module.arpAcceptedGraphicsLossGraphics;
                delete Module.arpAcceptedGraphicsLossLayout;
            // clang-format on
        });
        // clang-format on
        std::printf(
            "BINDLESS_RHI_ACCEPTED_GRAPHICS_FACTORY_NATIVE_OK %llu %llu registry6 original-layout-definition-receipt exact-payload no-recreation owners11-less10 cold-exact-less-exhausted-retry hot-allocation0 canonical-closed\n",
            static_cast<unsigned long long>(peaks[0]),
            static_cast<unsigned long long>(peaks[1]));
    }

    static void verifyActualLoss()
    {
        assert(loss_ && lossPipeline_ && rhi_webgpu::webGpuNativeDeviceLost());
        assert(std::string_view(lossPipeline_->originalArtifactGraphicsDiagnostic())
            == "RHI-GRAPHICS-CREATION-DEVICE-LOST");
        assert(!loss_->publish()
            && loss_->device->lastOriginalResourceRegistrationDiagnostic()
                == "RHI-GRAPHICS-CREATION-DEVICE-LOST");
        OriginalDescriptorBufferBindingFixture::verifyLoss(lossPipeline_);
        OriginalCommandPipelineSourceFixture::verifyLoss<rhi::RenderPipeline>();
        lossPipeline_.reset();
        loss_->retire();
        loss_.reset();
        // clang-format off
        assert(EM_ASM_INT({
            // clang-format off
                const wrappers = Object.values(WebGPU.Internals.jsObjects);
                return Module.arpAcceptedGraphicsLossObjects.length === 2 &&
                       !Module.arpAcceptedGraphicsLossObjects.some(p => wrappers.includes(p));
            // clang-format on
        }) == 1);
        // clang-format on
        // clang-format off
        assert(EM_ASM_INT({
            // clang-format off
                const wrappers = Object.values(WebGPU.Internals.jsObjects);
                return Module.arpPipelinePositionalAttachmentObjects.length === 2 &&
                       !Module.arpPipelinePositionalAttachmentObjects.some(p => wrappers.includes(p));
            // clang-format on
        }) == 1);
        // clang-format on
        // clang-format off
        EM_ASM({
            // clang-format off
                delete Module.arpPipelinePositionalAttachmentObjects;
            // clang-format on
        });
        // clang-format on
        std::puts(
            "BINDLESS_RHI_PIPELINE_POSITIONAL_ATTACHMENT_RETIREMENT_OK actual-texture-view wrappers2-zero");
        // clang-format off
        EM_ASM({
            // clang-format off
                delete Module.arpAcceptedGraphicsLossObjects;
            // clang-format on
        });
        // clang-format on
        std::puts(
            "BINDLESS_RHI_ACCEPTED_GRAPHICS_FACTORY_LOSS_OK actual-native-loss reject-wrapper-and-factory quotas7-zero native-wrappers2-zero");
    }

private:
    inline static core::UniquePtr<Context> loss_;
    inline static std::shared_ptr<rhi::RenderPipeline> lossPipeline_;
#endif
};
} // namespace arp::test
