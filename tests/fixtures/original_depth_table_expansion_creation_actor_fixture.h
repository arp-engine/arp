#pragma once
#include "original_depth_table_expansion_accepted_wrapper_fixture.h"
#include "original_depth_table_expansion_creation_source_fixture.h"
#include "rhi/backends/webgpu/webgpu_pipeline_preparation.h"
#include "webgpu_error_scope_handoff_fixture.h"

namespace arp::test {
class OriginalDepthTableExpansionCreationActorFixture final {
    using Definition = rhi_webgpu::WebGpuOriginalDepthTableExpansionPipelineDefinition;
    using Contract = rhi::OriginalDepthTableExpansionCreationContract;
    static constexpr std::uint64_t ceiling = 65536u;

public:
    template <class Desc>
    static void verify(const std::shared_ptr<const Definition::Space>& space,
        const std::shared_ptr<rhi::PipelineLayout>& layout,
        const Desc& desc,
        const Definition::Preflight::Budget& preflight,
        bool nativeRequested)
    {
        constexpr bool compute = std::is_same_v<Desc, rhi::ComputePipelineDesc>;
        using Actor = std::conditional_t<compute,
            rhi_webgpu::WebGpuOriginalArtifactComputeCreation,
            rhi_webgpu::WebGpuOriginalArtifactGraphicsCreation>;
        const char* id = nullptr;
        Definition::Budget db{1u, true, ceiling, 256u, preflight};
        auto dq = core::makeShared<core::CpuMemoryQuota>(ceiling);
        auto definition =
            Definition::prepareOriginal(nativeRequested ? rhi::Backend::WebGPU : rhi::Backend::Null,
                space,
                layout,
                desc,
                db,
                dq,
                id);
        assert(definition);
        Contract::Budget budget{1u,
            true,
            Actor::callbackCount,
            256u,
            256u,
            ceiling,
            ceiling,
            {1u, true, definition->requirements().shaderSourceBytes, ceiling}};
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 3> q;
        for (auto& domain : q)
            domain = core::makeShared<core::CpuMemoryQuota>(ceiling);
        auto prepare = [&] {
            return Actor::prepareTableExpansion(definition, budget, q[0], q[1], q[2], id);
        };
        assert(Actor::cancelTableExpansion({}));
        if (!nativeRequested) {
            assert(!prepare() && id
                && core::StringView(id) == "RHI-ORIGINAL-TABLE-CREATION-DEFINITION-BACKEND");
            for (const auto& domain : q)
                assert(!domain->liveBytes());
            return;
        }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        using Promises = WebGpuErrorScopeHandoffFixture<1101u>;
        auto empty = [&] {
            for (const auto& domain : q)
                assert(!domain->liveBytes());
        };
        const auto& input = [&]() -> const auto& {
            if constexpr (compute)
                return space->computeInput();
            else
                return space->graphicsInput();
        }();
        auto wait = [&](const auto& actor) {
            assert(WebGpuAsyncWaitFixture::until([&] {
                return actor->scopesObserved();
            }));
        };
        auto ready = [&] {
            auto actor = prepare();
            assert(actor);
            Actor::initialize(actor);
            wait(actor);
            return actor->validate() ? std::shared_ptr<Actor>{} : actor;
        };
        {
            auto initial = prepare();
            if (!initial)
                std::fprintf(
                    stderr, "selected actor preparation: %s\n", id ? id : "missing diagnostic");
            assert(initial);
        }
        for (auto index : {0u, 2u}) {
            CpuQuotaFactoryFixture pressure(q[index]);
            const auto peak = pressure.measureSingleUse([] {}, prepare);
            pressure.verifySingleUse(peak, [] {}, prepare);
            empty();
        }
        CpuQuotaFactoryFixture nativePressure(q[1]);
        const auto nativePeak = nativePressure.measureSingleUse([] {}, ready);
        nativePressure.verifySingleUse(nativePeak, [] {}, ready);
        empty();
        for (unsigned index = 0u; index < 3u; ++index) {
            core::CpuMemoryQuotaLease full(q[index], ceiling);
            assert(full.charged());
            if (index == 1u)
                assert(!ready());
            else
                assert(!prepare());
        }
        empty();
        for (const auto& domain :
            std::array{dq, space->quota(), input->quota(), input->tableExpansion()->quota()}) {
            const auto saved = budget.maxCpuOwnerBytes;
            budget.maxCpuOwnerBytes = domain->limitBytes();
            assert(!Actor::prepareTableExpansion(definition, budget, domain, q[1], q[2], id)
                && core::StringView(id) == "RHI-ORIGINAL-TABLE-CREATION-QUOTA-DOMAIN");
            budget.maxCpuOwnerBytes = saved;
        }
        auto saved = q[1];
        q[1] = q[0];
        assert(!prepare() && core::StringView(id) == "RHI-ORIGINAL-TABLE-CREATION-QUOTA-DOMAIN");
        q[1] = saved;
        saved = q[2];
        q[2] = q[1];
        assert(!prepare() && core::StringView(id) == "RHI-ORIGINAL-TABLE-CREATION-QUOTA-DOMAIN");
        q[2] = saved;
        for (auto& domain : q) {
            saved = domain;
            domain.reset();
            assert(!prepare());
            domain = saved;
        }
        empty();
        auto original = definition;
        definition = std::shared_ptr<const Definition>(q[0], original.get());
        assert(!prepare());
        definition = original;
        original.reset();
        --budget.native.maxTargetBytes;
        assert(!prepare());
        ++budget.native.maxTargetBytes;
        const auto label = budget.maxLabelBytes;
        budget.maxLabelBytes = desc.label.empty() ? 0u : desc.label.size() - 1u;
        assert(!prepare());
        budget.maxLabelBytes = label;
        auto actor = prepare();
        assert(actor && actor->isTableExpansion());
        assert(core::SharedOwnerProjection::sameOwner(actor->expansionDefinition(), definition)
            && &actor->originalInput() == &input);
        EM_ASM({
            // clang-format off
                Module.tableActorOriginalDevice = Module.preinitializedWebGPUDevice;
                Module.preinitializedWebGPUDevice = Module.tableEmissionForeignDevice;
            // clang-format on
        });
        assert(Actor::initialize(actor) && actor->status() == Actor::Status::Rejected
            && !q[1]->liveBytes());
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice = Module.tableActorOriginalDevice;
            // clang-format on
        });
        assert(Actor::initialize(actor));
        actor.reset();
        empty();
        actor = prepare();
        assert(actor);
        q[2]->setLimitBytes(ceiling - 1u);
        assert(Actor::initialize(actor));
        q[2]->setLimitBytes(ceiling);
        assert(actor->status() == Actor::Status::Rejected && !q[1]->liveBytes());
        actor.reset();
        empty();
        for (unsigned failure = 1u; failure <= 3u; ++failure) {
            actor = prepare();
            assert(actor);
            EM_ASM(
                {
                    // clang-format off
                    Module.tableEmissionFailure = $0;
                    // clang-format on
                },
                failure);
            Actor::initialize(actor);
            EM_ASM({
                // clang-format off
                    Module.tableEmissionFailure = 0;
                // clang-format on
            });
            wait(actor);
            assert(actor->status() == Actor::Status::Rejected && actor->validate()
                && !q[1]->liveBytes() && !q[2]->liveBytes());
            actor.reset();
            empty();
        }
        actor = prepare();
        assert(actor);
        EM_ASM({
            // clang-format off
                Module.tableActorPop = Module.preinitializedWebGPUDevice.popErrorScope;
                Module.preinitializedWebGPUDevice.popErrorScope = function() {
                    return Module.tableActorPop.call(this).then(
                        () => { throw new Error('selected creation scope uncertainty'); });
                };
            // clang-format on
        });
        Actor::initialize(actor);
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.popErrorScope = Module.tableActorPop;
            // clang-format on
        });
        wait(actor);
        assert(actor->status() == Actor::Status::Rejected && !q[1]->liveBytes());
        actor.reset();
        empty();
        for (unsigned phase = 0u; phase < 3u; ++phase) {
            actor = prepare();
            assert(actor);
            OriginalDepthTableExpansionAcceptedWrapperFixture::refuse(actor);
            if (phase == 1u) {
                Promises::holdScopes();
                assert(!Actor::initialize(actor));
                Promises::restoreScopes();
                Promises::waitHeldScopes();
                OriginalDepthTableExpansionAcceptedWrapperFixture::refuse(actor);
            }
            if (phase == 2u) {
                assert(!Actor::initialize(actor));
                wait(actor);
                assert(!actor->validate());
            }
            assert(!Actor::cancelTableExpansion(actor) && actor->status() == Actor::Status::Rejected
                && !q[1]->liveBytes());
            OriginalDepthTableExpansionAcceptedWrapperFixture::refuse(actor);
            assert(actor->validate() && Actor::initialize(actor));
            if (phase == 1u) {
                assert(!actor->scopesObserved());
                Promises::releaseScopes();
                wait(actor);
                assert(actor->status() == Actor::Status::Rejected);
            }
            actor.reset();
            empty();
        }
        actor = prepare();
        assert(actor);
        Promises::holdScopes();
        assert(!Actor::initialize(actor));
        Promises::restoreScopes();
        Promises::waitHeldScopes();
        Promises::releaseScope(2u);
        assert(WebGpuAsyncWaitFixture::until([&] {
            return actor->scopeObserved(2u);
        }));
        assert(actor->status() == Actor::Status::Pending);
        Promises::releaseScope(0u);
        assert(WebGpuAsyncWaitFixture::until([&] {
            return actor->scopeObserved(0u);
        }));
        assert(actor->status() == Actor::Status::Pending);
        Promises::releaseScopes();
        wait(actor);
        assert(!actor->validate());
        OriginalDepthTableExpansionAcceptedWrapperFixture::verify(actor);
        for (const auto& domain : std::array{q[0],
                 q[1],
                 q[2],
                 dq,
                 space->quota(),
                 input->quota(),
                 input->tableExpansion()->quota()})
            assert(actor->usesCpuQuotaDomain(domain));
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{q[0],
                                                                  q[1],
                                                                  q[2],
                                                                  dq,
                                                                  space->quota(),
                                                                  input->quota(),
                                                                  input->tableExpansion()->quota()},
            [&]() noexcept {
                return actor->validate();
            }));
        auto& changed = const_cast<Desc&>(actor->definition());
        if constexpr (compute) {
            ++changed.computeShader.computeWorkgroupSizeX;
            assert(actor->validate());
            --changed.computeShader.computeWorkgroupSizeX;
        } else {
            changed.depthStencil.stencilReadMask ^= 1u;
            assert(actor->validate());
            changed.depthStencil.stencilReadMask ^= 1u;
        }
        assert(!actor->validate());
        actor.reset();
        empty();
        actor = prepare();
        assert(actor);
        Promises::holdScopes();
        assert(!Actor::initialize(actor));
        Promises::restoreScopes();
        Promises::waitHeldScopes();
        std::weak_ptr<Actor> weakActor = actor;
        std::weak_ptr<const Definition> weakDefinition = definition;
        actor.reset();
        definition.reset();
        assert(!weakActor.expired() && !weakDefinition.expired());
        Promises::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return weakActor.expired();
        }));
        assert(weakDefinition.expired());
        weakActor.reset();
        weakDefinition.reset();
        empty();
        assert(!dq->liveBytes());
        definition =
            Definition::prepareOriginal(rhi::Backend::WebGPU, space, layout, desc, db, dq, id);
        assert(definition);
        std::array<std::shared_ptr<Actor>, 3> lossActors{prepare(), prepare(), ready()};
        for (const auto& a : lossActors)
            assert(a);
        using WrapperFactory = std::conditional_t<compute,
            rhi_webgpu::WebGpuAcceptedOriginalComputePipelineFactory,
            rhi_webgpu::WebGpuAcceptedOriginalGraphicsPipelineFactory>;
        using NativeFactory = std::conditional_t<compute,
            rhi_webgpu::WebGpuOriginalArtifactComputeFactory,
            rhi_webgpu::WebGpuOriginalArtifactGraphicsFactory>;
        auto wrapperQuota = core::makeShared<core::CpuMemoryQuota>(ceiling);
        auto lossWrapper =
            WrapperFactory::create(lossActors[2]->definition(), lossActors[2], wrapperQuota, id);
        assert(lossWrapper);
        Promises::holdScopes();
        assert(!Actor::initialize(lossActors[1]));
        Promises::restoreScopes();
        Promises::waitHeldScopes();
        bool lossObserved = false;
        auto device = emscripten_webgpu_get_device();
        assert(device);
        assert(rhi_webgpu::observeWebGpuNativeDeviceLoss(device, &lossObserved, [](void* p) {
            *static_cast<bool*>(p) = true;
        }));
        wgpuDeviceRelease(device);
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.destroy();
            // clang-format on
        });
        assert(WebGpuAsyncWaitFixture::until([&] {
            return lossObserved;
        }));
        for (const auto& a : lossActors) {
            assert(
                a->status() == Actor::Status::DeviceLost && a->validate() && Actor::initialize(a));
            OriginalDepthTableExpansionAcceptedWrapperFixture::refuse(a);
        }
        {
            auto native = rhi_webgpu::webGpuNativePayloadFor(*lossWrapper);
            assert(native && NativeFactory::validateConsumedTableExpansion(native));
        }
        lossWrapper.reset();
        assert(!wrapperQuota->liveBytes());
        assert(!q[1]->liveBytes() && !prepare());
        weakActor = lossActors[1];
        for (auto& a : lossActors)
            a.reset();
        definition.reset();
        assert(!weakActor.expired());
        Promises::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return weakActor.expired();
        }));
        weakActor.reset();
        empty();
        assert(!dq->liveBytes());
        std::printf(
            "ORIGINAL_TABLE_EXPANSION_CREATION_ACTOR_OK %s original-actor typed-request1 frozen-device scopes3 ordered2-0-1 failures3 unknown-pop cancel3 seven-domains exact-less-exhausted hot0 caller-drop real-loss3 commands-closed\n",
            compute ? "Compute" : "Graphics");
#endif
    }
};
} // namespace arp::test
