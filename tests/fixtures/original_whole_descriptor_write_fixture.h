#pragma once
#include "cpu_quota_factory_fixture.h"
#include "original_descriptor_buffer_binding_fixture.h"
#include "original_descriptor_native_identity_fixture.h"
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/frontend/descriptor_buffer_write_validation.h"
#include "rhi/gpu_device.h"
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
#include "webgpu_error_scope_handoff_fixture.h"
#endif
#include <cassert>
#include <cstdio>
#include <type_traits>

namespace arp::test {
class OriginalWholeDescriptorWriteFixture final {
    using Contract = rhi::OriginalWholeDescriptorWriteContract;

public:
    static void verifyContract()
    {
        static_assert(!std::is_same_v<rhi::OriginalWholeDescriptorWriteRequest,
            rhi::OriginalFiniteDescriptorWriteRequest>);
        static_assert(!std::is_same_v<rhi::OriginalWholeDescriptorBufferWrite,
            rhi::OriginalFiniteDescriptorBufferWrite>);
        rhi::OriginalWholeDescriptorWriteRequest request{1u,
            true,
            2u,
            static_cast<std::uint32_t>(Contract::ownerSlots(2u)),
            Contract::callbackCount,
            {1024u, 1024u, 1024u}};
        assert(!Contract::validate(request));
        for (const auto v : {0u, 2u}) {
            auto invalid = request;
            invalid.contractVersion = v;
            assert(Contract::validate(invalid));
        }
        auto invalid = request;
        invalid.prototypeOptIn = false;
        assert(Contract::validate(invalid));
        invalid = request;
        invalid.maxBindings = 0u;
        assert(Contract::validate(invalid));
        invalid = request;
        --invalid.maxRetainedOwnerSlots;
        assert(Contract::validate(invalid));
        invalid = request;
        ++invalid.maxCallbacks;
        assert(Contract::validate(invalid));
        for (std::size_t i = 0u; i < Contract::quotaCount; ++i) {
            invalid = request;
            invalid.maxCpuBytes[i] = 0u;
            assert(Contract::validate(invalid));
        }
        invalid = request;
        invalid.maxBindings = UINT32_MAX;
        assert(Contract::validate(invalid));
        const Contract::Facts complete{true, true, true, true, true};
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision = Contract::decide(backend, complete);
            assert(!decision.executionAvailable
                && decision.validatorOnly == (backend == rhi::Backend::Null));
            assert(decision.preparationAvailable == (backend == rhi::Backend::WebGPU));
            assert(bool(decision.diagnostic) != decision.preparationAvailable);
        }
        for (const auto missing : std::array<Contract::Facts, 6u>{{{},
                 {false, true, true, true, true},
                 {true, false, true, true, true},
                 {true, true, false, true, true},
                 {true, true, true, false, true},
                 {true, true, true, true, false}}})
            assert(!Contract::decide(rhi::Backend::WebGPU, missing).preparationAvailable);
        assert(Contract::decide(static_cast<rhi::Backend>(255u), complete).diagnostic);
        rhi::DescriptorBindingDesc binding;
        binding.type = rhi::DescriptorType::StorageBuffer;
        binding.stages = rhi::ShaderStage::Compute;
        rhi::DescriptorBindingWrite write;
        write.type = binding.type;
        write.resourceKind = rhi::DescriptorResourceKind::Buffer;
        write.resource.value = 1u;
        rhi::DescriptorBufferBindingRangeContract::Resolved resolved;
        const auto check = [&](const auto& b,
                               const auto& w,
                               std::uint64_t extent = 16u,
                               std::uint32_t offset = 0u) {
            resolved = {99u, 99u};
            const auto* id = Contract::validateBinding(b, w, extent, offset, resolved);
            if (id)
                assert(!resolved.offsetBytes && !resolved.sizeBytes);
            else
                assert(!resolved.offsetBytes && resolved.sizeBytes == extent);
            return id;
        };
        assert(!check(binding, write));
        binding.dynamicOffset = true;
        assert(!check(binding, write));
        assert(check(binding, write, 16u, 1u) && check(binding, write, 0u));
        for (const auto type : {rhi::DescriptorType::UniformBuffer,
                 rhi::DescriptorType::Sampler,
                 rhi::DescriptorType::SampledTexture}) {
            auto b = binding;
            auto w = write;
            b.type = type;
            w.type = type;
            assert(check(b, w));
        }
        auto b = binding;
        b.arrayCount = 2u;
        assert(check(b, write));
        b = binding;
        b.tableBacked = true;
        assert(check(b, write));
        b = binding;
        b.stages = rhi::ShaderStage::None;
        assert(check(b, write));
        auto w = write;
        w.bufferBindingRange = {2u, 0u, 16u};
        assert(check(binding, w));
        w = write;
        w.bufferBindingRange.offsetBytes = 1u;
        assert(check(binding, w));
        w = write;
        w.bufferBindingRange.sizeBytes = 16u;
        assert(check(binding, w));
        w = write;
        w.physicalLowering.enabled = true;
        assert(check(binding, w));
        w = write;
        w.arrayElement = 1u;
        assert(check(binding, w));
        w = write;
        w.resource = {};
        assert(check(binding, w));
        w = write;
        w.resourceKind = rhi::DescriptorResourceKind::TextureView;
        assert(check(binding, w));
        b = binding;
        b.type = rhi::DescriptorType::ReadOnlyStorageBuffer;
        w = write;
        w.type = b.type;
        assert(!check(b, w));
        auto quota = core::makeShared<core::CpuMemoryQuota>(1024u);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota}, [&]() noexcept -> const char* {
                assert(!Contract::validate(request));
                return check(binding, write);
            }));
        std::puts(
            "BINDLESS_RHI_ORIGINAL_WHOLE_WRITE_CONTRACT_OK distinct1 opt-in domains3 backends4 scalar-storage whole-extent zero-dynamics finite-reject output0 hot0 execution-closed");
    }

    static void verifyValidator(rhi::GpuDevice& device)
    {
        assert(device.backend() == rhi::Backend::Null);
        rhi::DescriptorArenaDesc desc;
        desc.maxSets = 1u;
        desc.maxDescriptors = 1u;
        auto arena = device.createDescriptorArena(desc);
        assert(arena);
        rhi::DescriptorSetDesc set;
        set.layout.name = "whole-write Null validator";
        rhi::DescriptorBindingDesc binding;
        binding.type = rhi::DescriptorType::StorageBuffer;
        binding.stages = rhi::ShaderStage::Compute;
        set.layout.bindings.push_back(binding);
        auto object = arena->allocateDescriptorSet(set);
        assert(object);
        rhi::OriginalWholeDescriptorWriteRequest request{1u,
            true,
            1u,
            static_cast<std::uint32_t>(Contract::ownerSlots(1u)),
            Contract::callbackCount,
            {1024u, 1024u, 1024u}};
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u> quotas;
        const rhi::OriginalWholeDescriptorBufferWrite write;
        const auto revision = object->contentRevision();
        assert(
            core::StringView(device.beginOriginalWholeDescriptorWrite(
                *object, std::shared_ptr<rhi::ComputePipeline>{}, 0u, &write, 1u, request, quotas))
            == "RHI-ORIGINAL-WHOLE-WRITE-NULL-VALIDATOR-ONLY");
        assert(object->finishOriginalWholeWrite() && object->cancelOriginalWholeWrite());
        assert(!object->hasOriginalWholeContent() && !object->hasOriginalFiniteContent()
            && object->contentRevision() == revision && !object->originalWholeWriteScopeObserved(0u)
            && !object->originalWholeWriteScopesDrained());
        std::puts(
            "BINDLESS_RHI_ORIGINAL_WHOLE_WRITE_NULL_OK actual-L2-gate validator-only no-payload no-revision no-native-success");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
    template <class Pipeline>
    static shader::WgslBufferDeclarationLayout originalTarget(
        const std::shared_ptr<Pipeline>& pipeline, std::uint32_t set, std::uint32_t binding)
    {
        const auto input = [&] {
            if constexpr (std::is_same_v<Pipeline, rhi::ComputePipeline>)
                return pipeline->originalComputeCreation()->originalInput();
            else
                return pipeline->originalGraphicsCreation()->originalInput();
        }();
        using Artifact =
            std::remove_const_t<typename std::decay_t<decltype(input->artifact())>::element_type>;
        using Memory = std::conditional_t<std::is_same_v<Pipeline, rhi::ComputePipeline>,
            shader::TargetBufferMemoryLayout,
            shader::GraphicsTargetBufferMemoryLayout>;
        Memory memory;
        assert(!Artifact::originalTargetBufferMemory(input->artifact(), set, binding, memory));
        return memory.target;
    }

    template <class Context, class Pipeline, std::size_t Count>
    static void commitActual(Context& c,
        const std::shared_ptr<Pipeline>& pipeline,
        rhi::DescriptorSet& object,
        std::uint32_t set,
        const std::array<rhi::OriginalWholeDescriptorBufferWrite, Count>& writes,
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas)
    {
        constexpr std::uint64_t bytes = 65536u;
        for (auto& quota : quotas)
            quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        const rhi::OriginalWholeDescriptorWriteRequest request{1u,
            true,
            Count,
            static_cast<std::uint32_t>(Contract::ownerSlots(Count)),
            Contract::callbackCount,
            {bytes, bytes, bytes}};
        const auto revision = object.contentRevision();
        const auto* diagnostic = c.device->beginOriginalWholeDescriptorWrite(
            object, pipeline, set, writes.data(), writes.size(), request, quotas);
        if (diagnostic)
            std::fprintf(stderr, "committed whole descriptor preparation: %s\n", diagnostic);
        assert(!diagnostic);
        assert(WebGpuAsyncWaitFixture::until([&] {
            return object.originalWholeWriteScopesDrained();
        }));
        assert(!object.finishOriginalWholeWrite() && object.contentRevision() == revision + 1u
            && object.hasOriginalWholeContent() && !object.originalScopedContentDiagnostic());
    }

    template <class Context, class Pipeline, std::size_t Count>
    static void verifyPendingTermination(Context& c,
        const std::shared_ptr<Pipeline>& pipeline,
        core::UniquePtr<rhi::DescriptorSet>& object,
        const std::shared_ptr<rhi::DescriptorArena>& arena,
        std::uint32_t set,
        const std::array<rhi::OriginalWholeDescriptorBufferWrite, Count>& writes,
        bool callerDrop)
    {
        using Scopes = WebGpuErrorScopeHandoffFixture<1005u>;
        constexpr std::uint64_t bytes = 65536u;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u> quotas;
        for (auto& quota : quotas)
            quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        const rhi::OriginalWholeDescriptorWriteRequest request{1u,
            true,
            Count,
            static_cast<std::uint32_t>(Contract::ownerSlots(Count)),
            Contract::callbackCount,
            {bytes, bytes, bytes}};
        const auto revision = object->contentRevision();
        auto published = rhi_webgpu::webGpuNativePayloadFor(*object);
        assert(published && published->containsOriginalWholeWrite());
        OriginalDescriptorNativeIdentityFixture::verifyWholeIdentity(published);
        Scopes::holdScopes();
        assert(!c.device->beginOriginalWholeDescriptorWrite(
            *object, pipeline, set, writes.data(), writes.size(), request, quotas));
        Scopes::restoreScopes();
        Scopes::waitHeldScopes();
        assert(object->contentRevision() == revision && object->originalScopedContentDiagnostic()
            && rhi_webgpu::webGpuNativePayloadFor(*object) == published);
        OriginalDescriptorNativeIdentityFixture::verifyWholeIdentity(
            published, "RHI-ORIGINAL-WHOLE-WRITE-PENDING");
        assert(object->finishOriginalWholeWrite() && object->cancelOriginalWholeWrite());
        assert(quotas[0]->liveBytes() && quotas[1]->liveBytes() && quotas[2]->liveBytes());
        if (callerDrop) {
            object.reset();
            OriginalDescriptorNativeIdentityFixture::verifyWholeIdentity(
                published, "RHI-ORIGINAL-FINITE-WRITE-DESCRIPTOR-EXPIRED");
            // Native promises retain the candidate until their genuine scope
            // callbacks drain. No synthetic callback or completion is issued.
            assert(quotas[0]->liveBytes() && quotas[1]->liveBytes() && quotas[2]->liveBytes());
            Scopes::releaseScopes();
            assert(WebGpuAsyncWaitFixture::until([&] {
                return !quotas[0]->liveBytes() && !quotas[1]->liveBytes()
                    && !quotas[2]->liveBytes();
            }));
        } else {
            arena->reset();
            assert(!object->isCurrentAllocation());
            OriginalDescriptorNativeIdentityFixture::verifyWholeIdentity(
                published, "RHI-DESCRIPTOR-ALLOCATION-STALE");
            Scopes::releaseScopes();
            assert(WebGpuAsyncWaitFixture::until([&] {
                return object->originalWholeWriteScopesDrained();
            }));
            OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                assert(CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
                    return object->finishOriginalWholeWrite();
                }));
            });
            assert(
                object->contentRevision() == revision && object->originalScopedContentDiagnostic());
            assert(!object->cancelOriginalWholeWrite());
            for (const auto& quota : quotas)
                assert(!quota->liveBytes());
        }
        published.reset();
        std::printf(
            "BINDLESS_RHI_ORIGINAL_WHOLE_PUBLIC_IDENTITY_LIFETIME_OK %s diagnostic-only native-recording-closed\n",
            callerDrop ? "caller-drop" : "arena-epoch");
        std::printf(
            "BINDLESS_RHI_ORIGINAL_WHOLE_WRITE_PENDING_TERMINATION_OK %s %s genuine-scopes3 no-revision callback-custody quotas-reclaimed native-closed\n",
            std::is_same_v<Pipeline, rhi::ComputePipeline> ? "compute" : "graphics",
            callerDrop ? "caller-drop" : "arena-epoch");
    }

    template <class Context, class Pipeline, std::size_t Count>
    static auto verifyActual(Context& c,
        const std::shared_ptr<Pipeline>& pipeline,
        rhi::DescriptorSet& object,
        std::uint32_t set,
        const std::array<rhi::OriginalWholeDescriptorBufferWrite, Count>& writes,
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>* retainedQuotas = nullptr)
    {
        using Scopes = WebGpuErrorScopeHandoffFixture<1005u>;
        constexpr std::uint64_t bytes = 65536u;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u> quotas;
        for (auto& quota : quotas)
            quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        rhi::OriginalWholeDescriptorWriteRequest request{1u,
            true,
            Count,
            static_cast<std::uint32_t>(Contract::ownerSlots(Count)),
            Contract::callbackCount,
            {bytes, bytes, bytes}};
        const auto revision = object.contentRevision();
        const auto old = rhi_webgpu::webGpuNativePayloadFor(object);
        const auto begin = [&](const auto& declarations) {
            return c.device->beginOriginalWholeDescriptorWrite(
                object, pipeline, set, declarations.data(), declarations.size(), request, quotas);
        };
        const auto drain = [&] {
            assert(WebGpuAsyncWaitFixture::until([&] {
                return object.originalWholeWriteScopesDrained();
            }));
        };
        const auto factory = [&] {
            if (begin(writes))
                return false;
            drain();
            assert(!object.cancelOriginalWholeWrite());
            assert(object.contentRevision() == revision
                && rhi_webgpu::webGpuNativePayloadFor(object) == old);
            return true;
        };
        std::array<std::uint64_t, 3u> peaks{};
        for (std::size_t i = 0u; i < quotas.size(); ++i)
            peaks[i] = CpuQuotaFactoryFixture(quotas[i]).measure(factory);
        for (std::size_t i = 0u; i < quotas.size(); ++i) {
            CpuQuotaFactoryFixture(quotas[i]).verify(peaks[i], factory);
            {
                core::CpuMemoryQuotaLease pressure(quotas[i], quotas[i]->limitBytes());
                assert(!factory());
            }
            assert(!quotas[i]->liveBytes());
        }
        auto hostile = writes;
        hostile[0].dynamicOffset = 1u;
        assert(begin(hostile));
        hostile = writes;
        hostile[0].write.bufferBindingRange = {2u, 0u, writes[0].buffer->layout().sizeBytes};
        assert(begin(hostile));
        hostile = writes;
        hostile[0].buffer.reset();
        assert(begin(hostile));
        hostile = writes;
        hostile[0].write.resource = {};
        assert(begin(hostile));
        auto saved = quotas[1];
        quotas[1] = quotas[0];
        assert(begin(writes));
        quotas[1] = saved;
        const auto intent = request;
        request.prototypeOptIn = false;
        assert(begin(writes));
        request = intent;
        Scopes::holdScopes();
        assert(!begin(writes));
        Scopes::restoreScopes();
        Scopes::waitHeldScopes();
        assert(object.originalScopedContentDiagnostic() && object.finishOriginalWholeWrite()
            && object.cancelOriginalWholeWrite());
        assert(object.finishOriginalFiniteWrite() && object.cancelOriginalFiniteWrite());
        assert(begin(writes) && !object.write(writes[0].write).success);
        for (std::uint32_t participant = 0u; participant < Contract::callbackCount; ++participant) {
            Scopes::releaseScope(participant);
            assert(WebGpuAsyncWaitFixture::until([&] {
                return object.originalWholeWriteScopeObserved(participant);
            }));
            if (participant + 1u < Contract::callbackCount)
                assert(object.finishOriginalWholeWrite());
            assert(object.contentRevision() == revision
                && rhi_webgpu::webGpuNativePayloadFor(object) == old);
        }
        Scopes::releaseScopes();
        drain();
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
                return object.finishOriginalWholeWrite();
            }));
        });
        assert(object.contentRevision() == revision + 1u && object.hasOriginalWholeContent()
            && !object.hasOriginalFiniteContent());
        auto payload = rhi_webgpu::webGpuNativePayloadFor(object);
        assert(payload && payload->group && payload->layout);
        assert(payload->containsOriginalWholeWrite() && !payload->containsOriginalFiniteWrite());
        OriginalDescriptorNativeIdentityFixture::verifyWholeIdentity(payload);
        assert(payload->diagnoseOriginalNativeIdentity(
            {1u, true, rhi::OriginalDescriptorNativeIdentityRequest::Profile::FiniteWrite1}));
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                quotas, [&]() noexcept -> const char* {
                    OriginalDescriptorNativeIdentityFixture::mutateActual(payload, [&] {
                        assert(object.originalScopedContentDiagnostic());
                        OriginalDescriptorNativeIdentityFixture::verifyWholeIdentity(
                            payload, "RHI-ORIGINAL-FINITE-WRITE-NATIVE-IDENTITY");
                    });
                    OriginalDescriptorNativeIdentityFixture::verifyWholeIdentity(payload);
                    return object.originalScopedContentDiagnostic();
                }));
        });
        std::puts(
            "BINDLESS_RHI_ORIGINAL_WHOLE_WRITE_IDENTITY_CONTENT_OK genuine-whole-group shared-mutation-driver restored hot0 query0 quotas3 public-metadata-identity");
        assert(!object.write(writes[0].write).success && object.finishOriginalWholeWrite());
        std::uint32_t group = 99u, count = 99u;
        const std::uint32_t* offsets = nullptr;
        assert(payload->diagnoseOriginalFiniteGroup(group, offsets, count) && !group && !count
            && !offsets);
        for (const auto& declaration : writes) {
            const char* id = nullptr;
            auto receipt = object.observeSuccessfulBindingWrite(declaration.write.binding, 0u, id);
            assert(receipt && !id && !receipt->diagnostic()
                && rhi::descriptorBindingWriteMatches(receipt->write(), declaration.write));
        }
        // Native failure must preserve the committed original group/revision.
        EM_ASM({
            // clang-format off
                const d = Module.preinitializedWebGPUDevice;
                Module.arpWholeGroup = d.createBindGroup;
                d.createBindGroup = function(desc) {
                    const entries = desc.entries.map(e => ({...e, resource : {...e.resource}}));
                    entries[0].resource.offset = 1;
                    return Module.arpWholeGroup.call(this, {...desc, entries});
                };
            // clang-format on
        });
        assert(!begin(writes));
        drain();
        OriginalDescriptorNativeIdentityFixture::verifyWholeIdentity(
            payload, "RHI-ORIGINAL-WHOLE-WRITE-PENDING");
        assert(object.finishOriginalWholeWrite());
        assert(object.contentRevision() == revision + 1u
            && rhi_webgpu::webGpuNativePayloadFor(object) == payload);
        assert(!object.cancelOriginalWholeWrite());
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.createBindGroup = Module.arpWholeGroup;
                delete Module.arpWholeGroup;
            // clang-format on
        });
        assert(!object.originalScopedContentDiagnostic());
        OriginalDescriptorNativeIdentityFixture::verifyWholeIdentity(payload);
        assert(!begin(writes));
        drain();
        assert(!object.finishOriginalWholeWrite());
        assert(object.contentRevision() == revision + 2u);
        OriginalDescriptorNativeIdentityFixture::verifyWholeIdentity(
            payload, "RHI-ORIGINAL-FINITE-WRITE-REVISION");
        OriginalDescriptorNativeIdentityFixture::verifyWholeIdentity(
            rhi_webgpu::webGpuNativePayloadFor(object));
        std::printf(
            "BINDLESS_RHI_ORIGINAL_WHOLE_WRITE_SCOPES_OK %s set%u bindings%zu peaks%llu,%llu,%llu exact-less-exhausted independent-scopes3 revision2 failed-replacement-preserved hot0 query0 identity-metadata-only\n",
            std::is_same_v<Pipeline, rhi::ComputePipeline> ? "compute" : "graphics",
            set,
            Count,
            static_cast<unsigned long long>(peaks[0]),
            static_cast<unsigned long long>(peaks[1]),
            static_cast<unsigned long long>(peaks[2]));
        std::puts(
            "BINDLESS_RHI_ORIGINAL_WHOLE_PUBLIC_IDENTITY_OK committed-proven-scopes mutations profile-disjoint pending-replacement-cancel-restores old-revision-reject hot0 query0 execution-closed");
        if (retainedQuotas)
            *retainedQuotas = quotas;
        return rhi_webgpu::webGpuNativePayloadFor(object);
    }
#endif
};
} // namespace arp::test
