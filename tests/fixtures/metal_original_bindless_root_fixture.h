#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_binding_validation.h"
#include "rhi/backends/metal/metal_bindless_root.h"
#include "rhi/backends/metal/metal_original_bindless_command.h"

namespace arp::test {
class MetalOriginalBindlessRootFixture final {
    using Closure = rhi_metal::MetalRhiBindlessRootNativeClosure;
    using Root = rhi::OriginalCommandBindlessRootSource;
    using Sources = rhi::OriginalCommandResourceSources;
    static constexpr std::uint64_t cpuBytes = 65536u;
    mutable std::shared_ptr<core::CpuMemoryQuota> quota_ =
        core::makeShared<core::CpuMemoryQuota>(cpuBytes);
    mutable std::shared_ptr<const Closure> retained_;

public:
    void operator()(const std::shared_ptr<const Root>& root,
        const std::shared_ptr<const Sources>& inventory,
        rhi::GpuDevice* device) const
    {
        using namespace rhi;
        using namespace rhi_metal;
        if (!device) {
            assert(!root && !inventory && retained_);
            assert(core::StringView(Closure::validateOriginal(retained_))
                == "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED");
            retained_.reset();
            assert(!quota_->liveBytes());
            return;
        }
        const char* id = nullptr;
        auto count = static_cast<std::uint32_t>(root->elements().size() + 1u);
        for (const auto& domain : root->ordinaryDomains())
            count += domain.source->frozenElements().size();
        const OriginalBindlessRootNativeClosureBudget budget{
            1u, true, count, 512u, 1024u, cpuBytes};
        auto prepare = [&](const auto& b, const auto& q) {
            return Closure::prepareOriginal(root, inventory, *device, b, q, id);
        };
        auto wrong = budget;
        wrong.contractVersion = 2u;
        assert(
            !prepare(wrong, quota_) && core::StringView(id) == "RHI-METAL-BINDLESS-ROOT-PROFILE");
        wrong = budget;
        wrong.prototypeOptIn = false;
        assert(!prepare(wrong, quota_)
            && core::StringView(id) == "RHI-BINDLESS-ROOT-NATIVE-CLOSURE-OPT-IN");
        wrong = budget;
        --wrong.maxResourceEntries;
        assert(!prepare(wrong, quota_)
            && core::StringView(id) == "RHI-METAL-BINDLESS-ROOT-NATIVE-ENTRY-CAPACITY");
        assert(!prepare(budget, inventory->originalMetadataQuota())
            && core::StringView(id) == "RHI-METAL-BINDLESS-ROOT-NATIVE-QUOTA-DOMAIN");
        {
            core::CpuMemoryQuotaLease pressure(quota_, cpuBytes);
            assert(pressure.charged());
            assert(!prepare(budget, quota_)
                && core::StringView(id) == "RHI-METAL-BINDLESS-ROOT-NATIVE-ALLOCATION");
            assert(quota_->liveBytes() == cpuBytes);
        }
        retained_ = prepare(budget, quota_);
        if (!retained_)
            std::fprintf(stderr, "Metal Root native closure: %s\n", id);
        assert(retained_ && !id && !retained_->nativeExecutionProven()
            && retained_->resourceEntryCount() == count);
        const auto charged = quota_->liveBytes();
        for (const bool native : {true, false}) {
            wrong = budget;
            if (native)
                wrong.maxNativeOwners = retained_->nativeOwnerCount() - 1u;
            else
                wrong.maxRetainedOwners = retained_->retainedOwnerCount() - 1u;
            assert(!prepare(wrong, quota_)
                && core::StringView(id)
                    == (native ? "RHI-METAL-BINDLESS-ROOT-NATIVE-OWNER-CAPACITY"
                               : "RHI-METAL-BINDLESS-ROOT-OWNER-CAPACITY"));
            assert(quota_->liveBytes() == charged);
        }
        auto alias = std::shared_ptr<const Closure>(quota_, retained_.get());
        assert(core::StringView(Closure::validateOriginal(alias))
            == "RHI-METAL-BINDLESS-ROOT-NATIVE-OWNER");
        alias.reset();
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota_}, [&]() noexcept -> const char* {
                return Closure::validateOriginal(retained_);
            }));
        const std::shared_ptr<const OriginalCommandBufferLease>* leasePointer = nullptr;
        for (const auto& element : root->elements())
            if (const auto* lease = std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(
                    &element.owner)) {
                leasePointer = lease;
                break;
            }
        assert(leasePointer);
        const auto& lease = *leasePointer;
        auto buffer = metalRhiNativePayloadFor(*lease->originalObject());
        assert(buffer);
        const auto original = buffer->resource.nativeObject;
        buffer->resource.nativeObject.reset();
        assert(core::StringView(Closure::validateOriginal(retained_))
            == "RHI-METAL-BINDLESS-ROOT-NATIVE-RESOURCE");
        buffer->resource.nativeObject = original;
        assert(!Closure::validateOriginal(retained_));
        auto descriptors = std::visit(
            [](const auto& binding) -> std::shared_ptr<const MetalRhiNativeDescriptorGroupPayload> {
                using Binding = typename std::decay_t<decltype(binding)>::element_type;
                if constexpr (std::is_same_v<Binding, const BindlessPreparedComputeBinding>)
                    return metalRhiPreparedComputePayloadFor(*binding)->descriptors;
                else
                    return metalRhiPreparedGraphicsPayloadFor(*binding)->descriptors;
            },
            root->originalPrepared());
        auto group = std::const_pointer_cast<MetalRhiNativeDescriptorGroupPayload>(descriptors);
        assert(group && !group->bindings.empty());
        auto bufferBinding =
            std::find_if(group->bindings.begin(), group->bindings.end(), [](const auto& binding) {
                return bool(binding.buffer);
            });
        assert(bufferBinding != group->bindings.end());
        const auto stored = bufferBinding->buffer;
        bufferBinding->buffer.reset();
        assert(core::StringView(Closure::validateOriginal(retained_))
            == "RHI-METAL-BINDLESS-ROOT-PHYSICAL-RESOURCE");
        bufferBinding->buffer = stored;
        const auto stages = bufferBinding->stages;
        bufferBinding->stages = ShaderStage::Fragment;
        assert(core::StringView(Closure::validateOriginal(retained_))
            == "RHI-METAL-BINDLESS-ROOT-PHYSICAL-RESOURCE");
        bufferBinding->stages = stages;
        ++bufferBinding->arrayCount;
        assert(core::StringView(Closure::validateOriginal(retained_))
            == "RHI-METAL-BINDLESS-ROOT-PHYSICAL-RESOURCE");
        --bufferBinding->arrayCount;
        assert(!Closure::validateOriginal(retained_) && quota_->liveBytes() == charged);
        std::uint32_t textures = 0u, samplers = 0u;
        std::shared_ptr<MetalRhiNativeSamplerPayload> earlierSampler;
        for (const auto& element : root->elements()) {
            if (const auto* view =
                    std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                        &element.owner)) {
                ++textures;
                auto parent =
                    metalRhiNativePayloadFor(*(*view)->originalParentLease()->originalObject());
                auto child = metalRhiNativePayloadFor(*(*view)->originalObject());
                assert(parent && child);
                for (auto* resource : {&parent->resource, &child->resource}) {
                    for (auto* identity : {&resource->nativeObject,
                             &resource->nativeDevice,
                             &resource->allocationReleaseToken}) {
                        const auto original = *identity;
                        *identity = std::shared_ptr<void>(quota_, original.get());
                        assert(Closure::validateOriginal(retained_));
                        *identity = original;
                        assert(!Closure::validateOriginal(retained_));
                    }
                }
                ++parent->extent.width;
                assert(Closure::validateOriginal(retained_));
                --parent->extent.width;
                ++child->mipLevelCount;
                assert(Closure::validateOriginal(retained_));
                --child->mipLevelCount;
                const auto heap = parent->aliasHeap;
                parent->aliasHeap = std::shared_ptr<void>(quota_, parent.get());
                assert(Closure::validateOriginal(retained_));
                parent->aliasHeap = heap;
                assert(!Closure::validateOriginal(retained_));
            } else if (const auto* sampler =
                           std::get_if<std::shared_ptr<const OriginalCommandSamplerLease>>(
                               &element.owner)) {
                ++samplers;
                auto native = metalRhiNativePayloadFor(*(*sampler)->originalObject());
                assert(native);
                if (earlierSampler
                    && earlierSampler->resource.nativeObject.get()
                        == native->resource.nativeObject.get())
                    assert(core::SharedOwnerProjection::sameOwner(
                        earlierSampler->resource.nativeObject, native->resource.nativeObject));
                earlierSampler = native;
                const auto original = native->resource.nativeObject;
                native->resource.nativeObject = std::shared_ptr<void>(quota_, original.get());
                assert(Closure::validateOriginal(retained_));
                native->resource.nativeObject = original;
                native->argumentBufferCompatible = false;
                assert(Closure::validateOriginal(retained_));
                native->argumentBufferCompatible = true;
                assert(!Closure::validateOriginal(retained_));
            }
        }
        if (textures || samplers) {
            assert(textures == 4u && samplers == 2u);
            assert(core::StringView(retained_->bufferExecutionDiagnostic())
                == "RHI-METAL-BINDLESS-ROOT-IMAGE-EXECUTION-UNAVAILABLE");
            core::pmr::String diagnostic;
            diagnostic.reserve(128u);
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{quota_}, [&]() noexcept -> const char* {
                    assert(!MetalOriginalBindlessCommand::prepareOriginal(
                        retained_, {}, {}, *device, diagnostic));
                    return diagnostic == "RHI-METAL-BINDLESS-ROOT-IMAGE-EXECUTION-UNAVAILABLE"
                        ? nullptr
                        : "image execution gate";
                }));
            std::puts(
                "BINDLESS_METAL_ORIGINAL_IMAGE_CLOSURE1 sampled2 storage2 sampler2 parent-view-native-owner shape alias-heap mutations-refused execution-refused hot0 quota0-on-retire");
        } else
            assert(!retained_->bufferExecutionDiagnostic());
        if (const auto* graphics =
                std::get_if<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
                    &root->originalPrepared())) {
            const auto native = metalRhiPreparedGraphicsPayloadFor(**graphics);
            assert(native);
            for (auto* function :
                {&native->pipeline->vertexFunction, &native->pipeline->fragmentFunction}) {
                if (!*function)
                    continue;
                const auto originalFunction = *function;
                *function = std::shared_ptr<void>(quota_, originalFunction.get());
                assert(Closure::validateOriginal(retained_));
                function->reset();
                assert(Closure::validateOriginal(retained_));
                *function = originalFunction;
                assert(!Closure::validateOriginal(retained_));
            }
            const auto writable =
                std::const_pointer_cast<MetalRhiNativePreparedGraphicsBindings>(native);
            {
                const auto original = writable->descriptors;
                writable->descriptors =
                    core::makeShared<MetalRhiNativeDescriptorGroupPayload>(*original);
                assert(Closure::validateOriginal(retained_));
                writable->descriptors = original;
            }
            for (std::size_t i = 0u; i < 2u; ++i) {
                const auto original = writable->stages[i];
                writable->stages[i] =
                    core::makeShared<MetalRhiNativePreparedFunctionBindings>(*original);
                assert(Closure::validateOriginal(retained_));
                writable->stages[i] = original;
                const auto layout = writable->layouts[i];
                writable->layouts[i] =
                    core::makeShared<MetalRhiPreparedGraphicsStageLayout>(*layout);
                assert(Closure::validateOriginal(retained_));
                writable->layouts[i] = layout;
            }
            std::swap(writable->stages[0], writable->stages[1]);
            assert(Closure::validateOriginal(retained_));
            std::swap(writable->stages[0], writable->stages[1]);
            assert(!Closure::validateOriginal(retained_));
            for (const auto& stage : native->stages) {
                if (!stage)
                    continue;
                const auto writableStage =
                    std::const_pointer_cast<MetalRhiNativePreparedFunctionBindings>(stage);
                for (auto& array : writableStage->arrays) {
                    const auto originalBuffer = array.buffer;
                    array.buffer.reset();
                    assert(Closure::validateOriginal(retained_));
                    array.buffer = originalBuffer;
                    assert(!Closure::validateOriginal(retained_));
                }
            }
            assert(!native->stages[0]->arrays.empty() && !native->stages[1]->arrays.empty());
            std::puts(
                "BINDLESS_METAL_ORIGINAL_GRAPHICS_CLOSURE1 authentic-VS-FS stage-layouts argument-buffers mutation-refusal no-execution-grant");
        }
        std::puts(
            "BINDLESS_METAL_ORIGINAL_NATIVE_CLOSURE1 real-payload exact-canonical-inventory prepared-function native-identity owner-budget quota failure-atomic hot0; no execution grant");
    }
};
} // namespace arp::test
