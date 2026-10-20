#pragma once
#include "cpu_quota_factory_fixture.h"
#include "original_descriptor_buffer_binding_fixture.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/frontend/command_usage.h"

namespace arp::test {
class OriginalCommandDescriptorConsumptionFixture final {
    using C = rhi::common::OriginalCommandDescriptorConsumption;
    using Inputs = rhi::OriginalCommandInputs;

    static void is(const char* actual, const char* expected)
    {
        if (!actual || core::StringView(actual) != expected)
            std::fprintf(
                stderr, "consumption expected %s got %s\n", expected, actual ? actual : "success");
        assert(actual && core::StringView(actual) == expected);
    }

public:
    static void verifyGeneralResources()
    {
        using T = rhi::OriginalCommandTypedSourceContract;
        C::Budget general{2u, true, 0u, 1u, false, true, 1u, 3u, 1u, true, 2u};
        general.generalShaderResourcesVersion = 1u;
        general.generalShaderResourcesOptIn = true;
        assert(!C::validateBudget(general));
        // GeneralResources/1 adds ordinary Inputs3 without changing the legacy
        // table Inputs4 or geometry Inputs5 admission contracts.
        using Profile = rhi::OriginalCommandDescriptorConsumptionContract;
        for (std::uint32_t footprint = 0u; footprint <= 4u; ++footprint)
            for (std::uint32_t inputs = 0u; inputs <= 6u; ++inputs) {
                const bool legacy = footprint >= 1u && footprint <= 3u && inputs == footprint + 2u;
                const bool opted = footprint == 2u ? inputs == 3u || inputs == 4u
                                                   : footprint == 3u && inputs == 5u;
                assert(Profile::inputsProfileMatches(footprint, 0u, false, inputs) == legacy);
                assert(Profile::inputsProfileMatches(footprint, 1u, true, inputs) == opted);
                assert(!Profile::inputsProfileMatches(footprint, 1u, false, inputs));
                assert(!Profile::inputsProfileMatches(footprint, 0u, true, inputs));
                assert(Profile::inputsProfileMatches(footprint, 2u, true, inputs)
                    == (footprint == 4u && inputs == 6u));
            }
        assert(rhi::OriginalCommandPipelineBindingContract::validateBudget({4u, true, 1u, 1u}));
        for (const auto field : {0u, 1u, 2u}) {
            auto bad = general;
            if (field == 0u)
                bad.generalShaderResourcesVersion = 2u;
            if (field == 1u)
                bad.generalShaderResourcesOptIn = false;
            if (field == 2u)
                bad.contractVersion = 1u;
            assert(C::validateBudget(bad)
                && !rhi::OriginalCommandDescriptorConsumptionContract::matches(general, bad));
        }
        C::Budget empty;
        empty.generalShaderResourcesVersion = 1u;
        assert(!rhi::OriginalCommandDescriptorConsumptionContract::empty(empty));
        rhi::OriginalRenderDescriptorUsageBudget render{
            2u, true, 0u, 1u, false, true, 6u, 1u, 3u, 1u, true, 2u};
        render.generalShaderResourcesVersion = 1u;
        render.generalShaderResourcesOptIn = true;
        assert(!render.diagnostic());
        auto badRender = render;
        badRender.generalShaderResourcesOptIn = false;
        assert(badRender.diagnostic() && !render.matches(badRender));
        rhi::DescriptorBindingDesc binding;
        binding.type = rhi::DescriptorType::UniformBuffer;
        binding.stages = rhi::ShaderStage::Compute;
        rhi::OriginalCommandDescriptorSetElement e;
        e.write.type = binding.type;
        e.write.resourceState = rhi::ResourceState::ShaderRead;
        e.stages = rhi::PipelineStage::ComputeShader;
        e.access = rhi::ResourceAccess::ShaderRead;
        e.bufferAccessBytes = 16u;
        assert(T::validateWholeBoundView(e, binding, 16u));
        assert(!T::validateWholeBoundView(e, binding, 16u, 1u));
        auto bad = e;
        bad.access = bad.access | rhi::ResourceAccess::ShaderWrite;
        assert(T::validateWholeBoundView(bad, binding, 16u, 1u));
        bad = e;
        --bad.bufferAccessBytes;
        assert(T::validateWholeBoundView(bad, binding, 16u, 1u));
        rhi::TextureViewDesc desc;
        desc.format = rhi::TextureFormat::Rgba16Float;
        desc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;
        auto view = rhi::TextureViewResourceLayout::capture(desc);
        binding.type = rhi::DescriptorType::StorageTexture;
        binding.storageTextureFormat = desc.format;
        binding.storageTextureAccess = rhi::StorageTextureAccess::ReadOnly;
        e.write.type = binding.type;
        e.write.resourceKind = rhi::DescriptorResourceKind::TextureView;
        e.bufferAccessBytes = 0u;
        assert(!T::validateGeneralTextureFootprint(e, binding, view));
        binding.storageTextureAccess = rhi::StorageTextureAccess::ReadWrite;
        e.write.resourceState = rhi::ResourceState::ShaderWrite;
        assert(T::validateGeneralTextureFootprint(e, binding, view));
        e.access = e.access | rhi::ResourceAccess::ShaderWrite;
        assert(!T::validateGeneralTextureFootprint(e, binding, view));
        binding.storageTextureAccess = rhi::StorageTextureAccess::WriteOnly;
        e.access = rhi::ResourceAccess::ShaderWrite;
        assert(!T::validateGeneralTextureFootprint(e, binding, view));
        bad = e;
        bad.bufferAccessBytes = 1u;
        assert(T::validateGeneralTextureFootprint(bad, binding, view));
        bad = e;
        bad.stages = rhi::PipelineStage::FragmentShader;
        assert(T::validateGeneralTextureFootprint(bad, binding, view));
        auto badBinding = binding;
        badBinding.stages =
            static_cast<rhi::ShaderStage>(static_cast<std::uint32_t>(binding.stages) | 0x80000000u);
        assert(T::validateGeneralTextureFootprint(e, badBinding, view));
        auto badView = view;
        badView.format = rhi::TextureFormat::R32Float;
        assert(T::validateGeneralTextureFootprint(e, binding, badView));
        badView = view;
        badView.mipLevelCount = 0u;
        assert(T::validateGeneralTextureFootprint(e, binding, badView));
        badView = view;
        badView.aspect = rhi::TextureAspect::Depth;
        assert(T::validateGeneralTextureFootprint(e, binding, badView));
        binding.type = rhi::DescriptorType::SampledTexture;
        e.write.type = binding.type;
        e.access = rhi::ResourceAccess::ShaderRead;
        e.write.resourceState = rhi::ResourceState::ShaderRead;
        assert(!T::validateGeneralTextureFootprint(e, binding, view));
        auto quota = core::makeShared<core::CpuMemoryQuota>(1024u);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota}, [&]() noexcept -> const char* {
                assert(!C::validateBudget(general) && !render.diagnostic());
                return T::validateGeneralTextureFootprint(e, binding, view);
            }));
        std::puts(
            "BINDLESS_RHI_GENERAL_DESCRIPTOR_FOOTPRINT_CONTRACT_OK opt-in1 uniform whole-view sampled storage-read-write exact-stages format-range legacy-refused input-profile-matrix hot0 no-execution-authority");
    }

    static void verifyContract()
    {
        verifyGeneralResources();
        C::Budget b{1u, true, 1u, 1u, true, true, 3u, 6u};
        assert(!C::validateBudget(b));
        is(C::validateBudget({}), "RHI-ORIGINAL-CONSUMPTION-VERSION");
        auto bad = b;
        bad.contractVersion = 3u;
        assert(C::validateBudget(bad));
        bad = b;
        bad.prototypeOptIn = false;
        is(C::validateBudget(bad), "RHI-ORIGINAL-CONSUMPTION-OPT-IN");
        bad = b;
        bad.finiteFootprintVersion = 2u;
        assert(C::validateBudget(bad));
        bad = b;
        bad.wholeFootprintVersion = 2u;
        assert(C::validateBudget(bad));
        bad = b;
        bad.finiteFullBoundViewOptIn = false;
        assert(C::validateBudget(bad));
        bad = b;
        bad.wholeFullBoundViewOptIn = false;
        assert(C::validateBudget(bad));
        bad = b;
        bad.maxConsumptions = 0u;
        assert(C::validateBudget(bad));
        bad = b;
        bad.maxLogicalAccesses = 0u;
        assert(C::validateBudget(bad));
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (const auto backend : std::array{rhi::Backend::Null,
                 rhi::Backend::Vulkan,
                 rhi::Backend::Metal,
                 rhi::Backend::WebGPU}) {
            const auto d =
                rhi::semantics::OriginalCommandDescriptorConsumptionLowering::lowering(backend);
            assert(!d.stateAvailable && !d.executionAvailable);
            assert(d.projectionAvailable == (backend == rhi::Backend::WebGPU));
            assert(d.validatorOnly == (backend == rhi::Backend::Null));
            assert(bool(d.diagnostic) != d.projectionAvailable);
        }
        assert(!rhi::semantics::OriginalCommandDescriptorConsumptionLowering::lowering(
            static_cast<rhi::Backend>(255u))
                .projectionAvailable);
        rhi::DescriptorBindingDesc binding;
        binding.type = rhi::DescriptorType::ReadOnlyStorageBuffer;
        binding.stages = rhi::ShaderStage::Compute;
        rhi::OriginalCommandDescriptorSetElement element;
        element.write.type = binding.type;
        element.write.resourceState = rhi::ResourceState::ShaderRead;
        element.stages = rhi::PipelineStage::ComputeShader;
        element.access = rhi::ResourceAccess::ShaderRead;
        element.bufferAccessBytes = 256u;
        assert(!rhi::OriginalCommandTypedSourceContract::validateWholeBoundView(
            element, binding, 256u));
        --element.bufferAccessBytes;
        is(rhi::OriginalCommandTypedSourceContract::validateWholeBoundView(element, binding, 256u),
            "RHI-ORIGINAL-WHOLE-FOOTPRINT-COVERAGE");
        ++element.bufferAccessBytes;
        element.bufferAccessOffset = 1u;
        assert(rhi::OriginalCommandTypedSourceContract::validateWholeBoundView(
            element, binding, 256u));
        element.bufferAccessOffset = 0u;
        element.dynamicOffset = 1u;
        is(rhi::OriginalCommandTypedSourceContract::validateWholeBoundView(element, binding, 256u),
            "RHI-ORIGINAL-WHOLE-FOOTPRINT-SCHEMA");
        element.dynamicOffset = 0u;
        binding.stages = rhi::ShaderStage::Compute | rhi::ShaderStage::Fragment;
        is(rhi::OriginalCommandTypedSourceContract::validateWholeBoundView(element, binding, 256u),
            "RHI-ORIGINAL-FINITE-SOURCE-FOOTPRINT-STAGES");
        binding.stages = rhi::ShaderStage::Compute;
        element.access = element.access | rhi::ResourceAccess::ShaderWrite;
        is(rhi::OriginalCommandTypedSourceContract::validateWholeBoundView(element, binding, 256u),
            "RHI-ORIGINAL-FINITE-SOURCE-FOOTPRINT-ACCESS");
        element.access = rhi::ResourceAccess::ShaderRead;
        binding.arrayCount = 2u;
        assert(rhi::OriginalCommandTypedSourceContract::validateWholeBoundView(
            element, binding, 256u));
        binding.arrayCount = 1u;
        binding.type = rhi::DescriptorType::UniformBuffer;
        assert(rhi::OriginalCommandTypedSourceContract::validateWholeBoundView(
            element, binding, 256u));
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        auto device = rhi::createNullDevice();
        std::size_t emitted = 0u;
        is(C::visit({},
               *device,
               b,
               [&](const auto&) noexcept -> const char* {
                   ++emitted;
                   return nullptr;
               })
                .diagnostic,
            "RHI-ORIGINAL-CONSUMPTION-NULL-VALIDATOR-ONLY");
        assert(!emitted);
        C::Budget table{2u, true, 0u, 0u, false, false, 1u, 3u, 1u, true, 3u};
        assert(!C::validateBudget(table));
        auto indexed = table;
        indexed.contractVersion = 3u;
        indexed.indexedDrawConsumptionVersion = 1u;
        indexed.indexedDrawConsumptionOptIn = true;
        assert(!C::validateBudget(indexed));
        using Indexed = rhi::OriginalCommandDescriptorConsumptionContract;
        assert(Indexed::isConsumption(rhi::CommandUsageKind::DrawIndexed, indexed));
        assert(!Indexed::isConsumption(rhi::CommandUsageKind::DrawIndexed, table));
        assert(!Indexed::isConsumption(rhi::CommandUsageKind::DrawIndexedIndirect, indexed));
        bad = indexed;
        bad.contractVersion = 2u;
        is(C::validateBudget(bad), "RHI-ORIGINAL-CONSUMPTION-INDEXED-PROFILE");
        bad = indexed;
        bad.indexedDrawConsumptionVersion = 2u;
        is(C::validateBudget(bad), "RHI-ORIGINAL-CONSUMPTION-INDEXED-VERSION");
        bad = indexed;
        bad.indexedDrawConsumptionOptIn = false;
        is(C::validateBudget(bad), "RHI-ORIGINAL-CONSUMPTION-INDEXED-OPT-IN");
        assert(!rhi::OriginalCommandDescriptorConsumptionContract::matches(indexed, bad));
        bad = indexed;
        bad.indexedDrawConsumptionVersion = 0u;
        assert(!rhi::OriginalCommandDescriptorConsumptionContract::matches(indexed, bad));
        C::Budget indexedOnly;
        indexedOnly.indexedDrawConsumptionVersion = 1u;
        auto streams = indexed;
        streams.vertexStreamConsumptionVersion = 1u;
        streams.vertexStreamConsumptionOptIn = true;
        streams.maxVertexStreams = 2u;
        assert(!C::validateBudget(streams));
        assert(
            !Indexed::validateVertexStreams(streams, 5u, rhi::CommandUsageKind::DrawIndexed, 2u));
        is(Indexed::validateVertexStreams(indexed, 5u, rhi::CommandUsageKind::DrawIndexed, 2u),
            "RHI-ORIGINAL-CONSUMPTION-VERTEX-PROFILE");
        is(Indexed::validateVertexStreams(streams, 4u, rhi::CommandUsageKind::DrawIndexed, 2u),
            "RHI-ORIGINAL-CONSUMPTION-VERTEX-STREAM-INPUTS");
        is(Indexed::validateVertexStreams(streams, 5u, rhi::CommandUsageKind::Draw, 2u),
            "RHI-ORIGINAL-CONSUMPTION-VERTEX-STREAM-INPUTS");
        is(Indexed::validateVertexStreams(streams, 5u, rhi::CommandUsageKind::DrawIndexed, 3u),
            "RHI-ORIGINAL-CONSUMPTION-VERTEX-STREAM-CAPACITY");
        for (const auto field : {0u, 1u, 2u, 3u}) {
            bad = streams;
            if (field == 0u)
                bad.vertexStreamConsumptionVersion = 2u;
            if (field == 1u)
                bad.vertexStreamConsumptionOptIn = false;
            if (field == 2u)
                bad.maxVertexStreams = 0u;
            if (field == 3u)
                bad.indexedDrawConsumptionOptIn = false;
            assert(C::validateBudget(bad) && !Indexed::matches(streams, bad));
        }
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision = rhi::semantics::OriginalCommandDescriptorConsumptionLowering::
                vertexStreamShaderLowering(backend);
            assert(!decision.executionAvailable && !decision.stateAvailable);
            assert(decision.validatorOnly == (backend == rhi::Backend::Null));
            assert(decision.projectionAvailable == (backend == rhi::Backend::WebGPU));
            assert(rhi::semantics::OriginalCommandDescriptorConsumptionLowering::
                    vertexStreamShaderLowering(backend, 2u)
                        .diagnostic);
        }
        C::Budget streamOnly;
        streamOnly.maxVertexStreams = 1u;
        assert(!Indexed::empty(streamOnly));
        streamOnly = {};
        streamOnly.vertexStreamConsumptionOptIn = true;
        assert(!Indexed::empty(streamOnly));
        streamOnly = {};
        streamOnly.vertexStreamConsumptionVersion = 1u;
        assert(!Indexed::empty(streamOnly));
        assert(!rhi::OriginalCommandDescriptorConsumptionContract::empty(indexedOnly));
        indexedOnly = {};
        indexedOnly.indexedDrawConsumptionOptIn = true;
        assert(!rhi::OriginalCommandDescriptorConsumptionContract::empty(indexedOnly));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision =
                rhi::semantics::OriginalCommandDescriptorConsumptionLowering::indexedDrawLowering(
                    backend);
            assert(!decision.executionAvailable && !decision.stateAvailable);
            assert(decision.validatorOnly == (backend == rhi::Backend::Null));
            assert(decision.projectionAvailable == (backend == rhi::Backend::WebGPU));
        }
        bad = table;
        bad.contractVersion = 1u;
        is(C::validateBudget(bad), "RHI-ORIGINAL-CONSUMPTION-LEGACY-SCHEMA");
        bad = table;
        bad.depthTableFootprintVersion = 2u;
        is(C::validateBudget(bad), "RHI-ORIGINAL-CONSUMPTION-TABLE-FOOTPRINT-VERSION");
        bad = table;
        bad.depthTableFullBoundViewOptIn = false;
        is(C::validateBudget(bad), "RHI-ORIGINAL-CONSUMPTION-TABLE-FOOTPRINT-OPT-IN");
        bad = table;
        bad.maxTextureAccesses = 0u;
        is(C::validateBudget(bad), "RHI-ORIGINAL-CONSUMPTION-TEXTURE-BUDGET");
        bad = table;
        bad.maxTextureAccesses = 4u;
        is(C::validateBudget(bad), "RHI-ORIGINAL-CONSUMPTION-TEXTURE-BUDGET");
        assert(!rhi::OriginalCommandDescriptorConsumptionContract::empty(table));
        for (const auto field : {0u, 1u, 2u}) {
            bad = table;
            if (field == 0u)
                ++bad.depthTableFootprintVersion;
            if (field == 1u)
                bad.depthTableFullBoundViewOptIn = false;
            if (field == 2u)
                --bad.maxTextureAccesses;
            assert(!rhi::OriginalCommandDescriptorConsumptionContract::matches(table, bad));
        }
        for (const auto backend : std::array{rhi::Backend::Null,
                 rhi::Backend::Vulkan,
                 rhi::Backend::Metal,
                 rhi::Backend::WebGPU}) {
            const auto d =
                rhi::semantics::OriginalCommandDescriptorConsumptionLowering::lowering(backend, 2u);
            assert(!d.stateAvailable && !d.executionAvailable);
            assert(d.projectionAvailable == (backend == rhi::Backend::WebGPU));
            assert(d.validatorOnly == (backend == rhi::Backend::Null));
        }
        assert(rhi::semantics::OriginalCommandDescriptorConsumptionLowering::lowering(
            rhi::Backend::WebGPU, 5u)
                .diagnostic);
        is(C::visit({},
               *device,
               table,
               [&](const auto&) noexcept -> const char* {
                   ++emitted;
                   return nullptr;
               })
                .diagnostic,
            "RHI-ORIGINAL-CONSUMPTION-NULL-VALIDATOR-ONLY");
        assert(!emitted);
        rhi::OriginalCommandStateAdmissionBudget state;
        state.contractVersion = 2u;
        state.prototypeOptIn = true;
        state.descriptorConsumption = table;
        is(rhi::OriginalCommandStateAdmissionContract::validateIntent(state),
            "RHI-ORIGINAL-STATE-CONSUMPTION-PROFILE");
        C::RenderBindingBudget render{2u, true, 0u, 0u, false, false, 8u, 2u, 6u, 1u, true, 6u};
        assert(!rhi::OriginalRenderDescriptorUsageContract::validateBudget(render));
        auto invalidRender = render;
        invalidRender.contractVersion = 1u;
        is(rhi::OriginalRenderDescriptorUsageContract::validateBudget(invalidRender),
            "RHI-ORIGINAL-RENDER-USAGE-LEGACY-SCHEMA");
        invalidRender = render;
        invalidRender.contractVersion = 5u;
        is(rhi::OriginalRenderDescriptorUsageContract::validateBudget(invalidRender),
            "RHI-ORIGINAL-RENDER-USAGE-VERSION");
        invalidRender = render;
        invalidRender.depthTableFullBoundViewOptIn = false;
        is(rhi::OriginalRenderDescriptorUsageContract::validateBudget(invalidRender),
            "RHI-ORIGINAL-CONSUMPTION-TABLE-FOOTPRINT-OPT-IN");
        invalidRender = render;
        invalidRender.maxTextureAccesses = 0u;
        is(rhi::OriginalRenderDescriptorUsageContract::validateBudget(invalidRender),
            "RHI-ORIGINAL-CONSUMPTION-TEXTURE-BUDGET");
        for (const auto backend : std::array{rhi::Backend::Null,
                 rhi::Backend::Vulkan,
                 rhi::Backend::Metal,
                 rhi::Backend::WebGPU}) {
            const auto d = rhi::OriginalRenderDescriptorUsageContract::lowering(backend, 2u);
            assert(!d.stateAvailable && !d.executionAvailable);
            assert(d.projectionAvailable == (backend == rhi::Backend::WebGPU));
            assert(d.validatorOnly == (backend == rhi::Backend::Null));
        }
        is(C::visitRenderBindings({},
               *device,
               render,
               [&](const auto&) noexcept -> const char* {
                   ++emitted;
                   return nullptr;
               })
                .diagnostic,
            "RHI-ORIGINAL-RENDER-USAGE-NULL-VALIDATOR-ONLY");
        assert(!emitted);
        std::puts(
            "BINDLESS_RHI_DESCRIPTOR_CONSUMPTION_CONTRACT_OK projection1 separate-finite-whole-opt-ins exact-ceilings backends4 no-state-native-authority");
    }

    static void verifyRetained(
        const std::shared_ptr<const Inputs>& inputs, const rhi::GpuDevice& device, bool valid)
    {
        std::size_t emitted = 0u;
        const auto result = C::visit(inputs,
            device,
            {1u, true, 1u, 1u, true, true, 3u, 6u},
            [&](const auto&) noexcept -> const char* {
                ++emitted;
                return nullptr;
            });
        assert(bool(result.diagnostic) != valid && emitted == (valid ? 6u : 0u));
    }

    // Real accepted Source2/3 and original Inputs only. No fabricated actor.
    template <class Pipeline>
    static auto verify(const std::shared_ptr<const Inputs>& positional,
        const rhi::GpuDevice& device,
        bool report = true,
        bool indexed = false)
    {
        using K = rhi::CommandUsageKind;
        using Scalar = rhi::CommandScalarArguments;
        using Usage = rhi::OriginalCommandUsage;
        const auto& root = positional->originalScalars()->originalUsage()->originalSources();
        const auto& graph = positional->originalMetadataQuota();
        constexpr bool graphics = std::is_same_v<Pipeline, rhi::RenderPipeline>;
        constexpr auto begin = graphics ? K::BeginRenderPass : K::BeginComputePass,
                       end = graphics ? K::EndRenderPass : K::EndComputePass;
        constexpr auto pipe = graphics ? K::BindGraphicsPipeline : K::BindComputePipeline,
                       set = graphics ? K::BindRenderDescriptor : K::BindComputeDescriptor;
        assert(!indexed || graphics);
        const auto work = graphics ? (indexed ? K::DrawIndexed : K::Draw) : K::Dispatch;
        const auto& original = *positional->originalScalars()->originalUsage();
        const auto pipelineIndex = original.references()[graphics ? 1u : 0u].sourceIndex;
        const auto descriptorIndex = original.references()[graphics ? 2u : 1u].sourceIndex;
        const auto& descriptor =
            *std::get<std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>>(
                root->sources()[descriptorIndex]);
        const auto indexSource =
            indexed ? descriptor.frozenElements()[0u].declaration.sourceIndex : SIZE_MAX;
        if (indexed)
            assert((std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                        root->sources()[indexSource])
                           ->layout()
                           .usage
                       & rhi::ResourceUsage::IndexBuffer)
                != rhi::ResourceUsage::None);
        const auto& oldArguments = *positional->originalScalars()->originalArguments();
        const std::array<K, 14u> kinds{
            {begin, pipe, set, work, pipe, set, work, end, begin, pipe, set, work, end, K::Finish}};
        const std::array<K, 17u> indexedKinds{{begin,
            pipe,
            set,
            K::BindIndexBuffer,
            work,
            pipe,
            set,
            K::BindIndexBuffer,
            work,
            end,
            begin,
            pipe,
            set,
            K::BindIndexBuffer,
            work,
            end,
            K::Finish}};
        const std::size_t commandCount = indexed ? indexedKinds.size() : kinds.size();
        const char* id = nullptr;
        // Missing slot after a successful earlier work point must also emit0.
        const auto build = [&](std::size_t omit = SIZE_MAX,
                               bool replacePipeline = false,
                               bool shadowedUnused = false) {
            std::array<rhi::CommandUsage, 17u> commands{};
            std::array<Usage::Reference, 13u> references{};
            const rhi::CommandVariableArguments::Input emptyField{
                *Scalar::capture(K::Finish, Scalar::None{}), {}};
            std::array<rhi::CommandVariableArguments::Input, 17u> fields{{emptyField,
                emptyField,
                emptyField,
                emptyField,
                emptyField,
                emptyField,
                emptyField,
                emptyField,
                emptyField,
                emptyField,
                emptyField,
                emptyField,
                emptyField,
                emptyField,
                emptyField,
                emptyField,
                emptyField}};
            const Inputs::OriginalDescriptor emptyRecord{
                SIZE_MAX, rhi::OriginalCommandDescriptorBinding{}};
            std::array<Inputs::OriginalDescriptor, 13u> records{{emptyRecord,
                emptyRecord,
                emptyRecord,
                emptyRecord,
                emptyRecord,
                emptyRecord,
                emptyRecord,
                emptyRecord,
                emptyRecord,
                emptyRecord,
                emptyRecord,
                emptyRecord,
                emptyRecord}};
            std::size_t refs = 0u, count = 0u, scope = SIZE_MAX, latestPipeline = SIZE_MAX;
            for (std::size_t i = 0u; i < commandCount; ++i) {
                const std::array<K, 14u> shadowKinds{{begin,
                    pipe,
                    set,
                    set,
                    end,
                    begin,
                    pipe,
                    set,
                    work,
                    pipe,
                    pipe,
                    pipe,
                    end,
                    K::Finish}};
                auto kind = indexed ? indexedKinds[i] : shadowedUnused ? shadowKinds[i] : kinds[i];
                if (i == omit)
                    kind = indexed ? K::BindIndexBuffer : work;
                if (replacePipeline && i == (indexed ? 5u : 4u))
                    kind = set;
                auto& command = commands[i];
                command = {kind, static_cast<std::uint32_t>(refs), 0u};
                auto scalar = emptyField.scalar;
                if (kind != set && kind != work && kind != K::BindIndexBuffer)
                    scalar = *Scalar::capture(kind, Scalar::None{});
                if (kind == begin) {
                    scope = i;
                    if constexpr (graphics) {
                        references[refs++] = {original.references()[0u].sourceIndex};
                        command.referenceCount = 1u;
                        records[count++] = {i, *positional->descriptorAt(0u)};
                    }
                }
                if (kind == pipe) {
                    latestPipeline = i;
                    references[refs++] = {pipelineIndex};
                    command.referenceCount = 1u;
                    auto binding = std::get<rhi::OriginalCommandPipelineBinding>(
                        *positional->descriptorAt(1u));
                    binding.scopeBeginCommandIndex = scope;
                    records[count++] = {i, binding};
                } else if (kind == K::BindIndexBuffer) {
                    references[refs++] = {indexSource};
                    command.referenceCount = 1u;
                    const auto& lease =
                        std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                            root->sources()[indexSource]);
                    const auto& originalPipeline =
                        std::get<std::shared_ptr<const rhi::OriginalCommandGraphicsPipelineSource>>(
                            root->sources()[pipelineIndex]);
                    rhi::OriginalCommandGeometryBinding binding;
                    binding.commandKind = kind;
                    binding.sourceIndex = indexSource;
                    binding.pipelineSourceIndex = pipelineIndex;
                    binding.pipelineBindCommandIndex = latestPipeline;
                    binding.scopeBeginCommandIndex = scope;
                    binding.registrationGeneration =
                        lease->originalObject()->originalResourceRegistration().generation();
                    binding.pipelineRegistrationGeneration = originalPipeline->originalObject()
                                                                 ->originalResourceRegistration()
                                                                 .generation();
                    records[count++] = {i, binding};
                    scalar =
                        *Scalar::capture(kind, Scalar::IndexBinding{rhi::IndexFormat::Uint32, 0u});
                } else if (kind == set) {
                    references[refs++] = {descriptorIndex};
                    command.referenceCount = 1u;
                    auto binding = std::get<rhi::OriginalCommandDescriptorBinding>(
                        *positional->descriptorAt(2u));
                    binding.scopeBeginCommandIndex = scope;
                    records[count++] = {i, binding};
                    scalar = *Scalar::capture(
                        kind, Scalar::DescriptorBinding{descriptor.frozenLogicalSet()});
                } else if (kind == work) {
                    if constexpr (graphics)
                        scalar = indexed
                            ? *Scalar::capture(kind, Scalar::DrawIndexed{3u, 1u, 0u, 0, 0u})
                            : *Scalar::capture(kind, Scalar::Draw{3u, 1u, 0u, 0u});
                    else
                        scalar = *Scalar::capture(kind, Scalar::Dispatch{1u, 1u, 1u});
                }
                fields[i] = {scalar, {}};
                if (kind == set) {
                    fields[i].offsets = oldArguments.offsets(2u);
                    fields[i].offsetCount = oldArguments.records()[2u].offsetCount;
                }
            }
            auto usage = Usage::prepareOriginal(
                root, commands.data(), commandCount, references.data(), refs, graph, id);
            assert(usage);
            assert(commandCount <= UINT32_MAX);
            auto arguments = rhi::CommandVariableArguments::prepareOriginal(fields.data(),
                commandCount,
                {1u,
                    true,
                    static_cast<std::uint32_t>(commandCount),
                    root->originalBudget().maxDynamicOffsets,
                    0u,
                    0u,
                    graph->limitBytes()},
                graph,
                id);
            assert(arguments);
            auto scalars =
                rhi::OriginalCommandScalars::prepareOriginal(usage, arguments, graph, id);
            assert(scalars);
            auto budget = positional->originalBudget();
            // Genuine index binding permits ordered shader-footprint validation;
            // index content analysis and geometry execution remain independent.
            if (indexed) {
                budget.contractVersion = 5u;
                budget.maxIndexBindings = 4u;
            }
            if (shadowedUnused)
                budget.maxPipelineBindings += 2u;
            if (replacePipeline) {
                ++budget.maxDescriptorBindings;
                budget.maxFrozenDynamicOffsets += oldArguments.records()[2u].offsetCount;
            }
            auto inputs =
                Inputs::prepareOriginal(scalars, records.data(), count, budget, graph, id);
            if (!inputs)
                std::fprintf(stderr, "consumption Inputs failed: %s\n", id ? id : "unknown");
            assert(inputs);
            return inputs;
        };
        auto inputs = build();
        C::Budget budget{1u, true, 1u, 1u, true, true, 3u, 6u};
        if (indexed) {
            budget.contractVersion = 3u;
            budget.depthTableFootprintVersion = 1u;
            budget.depthTableFullBoundViewOptIn = true;
            budget.maxTextureAccesses = 1u;
            budget.indexedDrawConsumptionVersion = 1u;
            budget.indexedDrawConsumptionOptIn = true;
            auto missing = budget;
            missing.indexedDrawConsumptionVersion = 0u;
            missing.indexedDrawConsumptionOptIn = false;
            std::size_t emitted = 0u;
            is(C::visit(inputs,
                   device,
                   missing,
                   [&](const auto&) noexcept -> const char* {
                       ++emitted;
                       return nullptr;
                   })
                    .diagnostic,
                "RHI-ORIGINAL-CONSUMPTION-COMMAND-PROFILE");
            assert(!emitted);
        }
        const auto inspect = [&](const auto& value, const auto& b) {
            return C::inspect(value, device, b);
        };
        auto result = inspect(inputs, budget);
        if (result.diagnostic)
            std::fprintf(stderr, "consumption failed: %s\n", result.diagnostic);
        assert(!result.diagnostic && result.consumptions == 3u && result.logicalAccesses == 6u);
        auto bad = budget;
        --bad.maxConsumptions;
        is(inspect(inputs, bad).diagnostic, "RHI-ORIGINAL-CONSUMPTION-CAPACITY");
        bad = budget;
        --bad.maxLogicalAccesses;
        is(inspect(inputs, bad).diagnostic, "RHI-ORIGINAL-CONSUMPTION-ACCESS-CAPACITY");
        bad = budget;
        bad.maxConsumptions = root->originalBudget().maxCommands + 1u;
        assert(inspect(inputs, bad).diagnostic);
        bad = budget;
        if (descriptor.sourceContractVersion() == 3u) {
            bad.finiteFullBoundViewOptIn = false;
            bad.finiteFootprintVersion = 0u;
        } else {
            bad.wholeFullBoundViewOptIn = false;
            bad.wholeFootprintVersion = 0u;
        }
        is(inspect(inputs, bad).diagnostic, "RHI-ORIGINAL-CONSUMPTION-SOURCE-FOOTPRINT-OPT-IN");
        for (const auto omit : (indexed ? std::array<std::size_t, 3>{2u, 6u, 12u}
                                        : std::array<std::size_t, 3>{2u, 5u, 10u})) {
            auto missing = build(omit);
            std::size_t emitted = 0u;
            is(C::visit(missing,
                   device,
                   budget,
                   [&](const auto&) noexcept -> const char* {
                       ++emitted;
                       return nullptr;
                   })
                    .diagnostic,
                "RHI-ORIGINAL-CONSUMPTION-DESCRIPTOR-MISSING");
            assert(!emitted);
        }
        auto latest = build(SIZE_MAX, true);
        std::size_t accesses = 0u;
        assert(!C::visit(latest,
                   device,
                   budget,
                   [&](const auto& a) noexcept -> const char* {
                       if (a.commandIndex == (indexed ? 8u : 6u))
                           assert(a.pipelineBindCommandIndex == 1u
                               && a.descriptorBindCommandIndex == (indexed ? 6u : 5u));
                       ++accesses;
                       return nullptr;
                   })
                   .diagnostic
            && accesses == 6u);
        latest.reset();
        const auto verify = [&]() noexcept -> const char* {
            std::size_t emitted = 0u;
            const auto r =
                C::visit(inputs, device, budget, [&](const auto& a) noexcept -> const char* {
                    const auto ordinal = emitted / 2u, elementIndex = emitted % 2u;
                    const auto workAt = indexed ? std::array<std::size_t, 3>{4u, 8u, 14u}
                                                : std::array<std::size_t, 3>{3u, 6u, 11u};
                    const auto pipeAt = indexed ? std::array<std::size_t, 3>{1u, 5u, 11u}
                                                : std::array<std::size_t, 3>{1u, 4u, 9u};
                    const auto setAt = indexed ? std::array<std::size_t, 3>{2u, 6u, 12u}
                                               : std::array<std::size_t, 3>{2u, 5u, 10u};
                    assert(a.commandIndex == workAt[ordinal]
                        && a.pipelineBindCommandIndex == pipeAt[ordinal]
                        && a.descriptorBindCommandIndex == setAt[ordinal]
                        && a.scopeBeginCommandIndex
                            == (ordinal < 2u  ? 0u
                                    : indexed ? 10u
                                              : 8u));
                    const auto& e = descriptor.frozenElements()[elementIndex].declaration;
                    rhi::DescriptorBufferBindingRangeContract::Resolved range;
                    const auto& buffer =
                        std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                            root->sources()[e.sourceIndex]);
                    assert(!rhi::DescriptorBufferBindingRangeContract::resolve(
                        e.write.bufferBindingRange,
                        buffer->layout().sizeBytes,
                        e.dynamicOffset,
                        range));
                    assert(a.pipelineSourceIndex == pipelineIndex
                        && a.descriptorSourceIndex == descriptorIndex
                        && a.bufferSourceIndex == e.sourceIndex);
                    assert(a.offsetBytes == range.offsetBytes && a.sizeBytes == range.sizeBytes
                        && a.stages == e.stages && a.access == e.access
                        && a.state == e.write.resourceState
                        && a.registrationGeneration == e.registrationGeneration
                        && a.descriptorContentRevision == descriptor.frozenContentRevision()
                        && !a.nativeExecutionProven());
                    ++emitted;
                    return nullptr;
                });
            assert(emitted == 6u);
            return r.diagnostic;
        };
        if constexpr (graphics) {
            if (!indexed) {
                auto shadowed = build(SIZE_MAX, false, true);
                const C::RenderBindingBudget renderBudget{
                    1u, true, 1u, 1u, true, true, 14u, 3u, 6u};
                const auto active = C::inspect(shadowed, device, budget);
                assert(!active.diagnostic && active.consumptions == 1u
                    && active.logicalAccesses == 2u);
                std::size_t emitted = 0u;
                const auto all = C::visitRenderBindings(
                    shadowed, device, renderBudget, [&](const auto& a) noexcept -> const char* {
                        constexpr std::array<std::size_t, 3u> positions{2u, 3u, 7u};
                        assert(a.commandIndex == positions[emitted / 2u]
                            && a.descriptorBindCommandIndex == a.commandIndex
                            && a.scopeBeginCommandIndex == (emitted < 4u ? 0u : 5u));
                        ++emitted;
                        return nullptr;
                    });
                assert(!all.diagnostic && all.bindings == 3u && all.logicalAccesses == 6u
                    && emitted == 6u);
                auto less = renderBudget;
                --less.maxBindings;
                emitted = 0u;
                const auto rejected = C::visitRenderBindings(
                    shadowed, device, less, [&](const auto&) noexcept -> const char* {
                        ++emitted;
                        return nullptr;
                    });
                is(rejected.diagnostic, "RHI-ORIGINAL-RENDER-USAGE-BINDING-CAPACITY");
                assert(!rejected.bindings && !rejected.logicalAccesses && !emitted);
                std::printf(
                    "BINDLESS_RHI_ORIGINAL_RENDER_BIND_USAGE_OK source%u original-inputs3 passes2 binds3 buffers6 active-draw1-buffers2 shadowed-unused-first-pass exact-less emitted0 no-sync-native-proof\n",
                    descriptor.sourceContractVersion());
            }
        }
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
#endif
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{graph, root->originalMetadataQuota()}, verify));
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        });
#endif
        std::pmr::set_default_resource(previous);
        assert(!inputs->nativeExecutionProven() && root->typedStateJoinDiagnostic());
        if (indexed)
            std::printf(
                "BINDLESS_RHI_INDEXED_SHADER_CONSUMPTION_OK source%u inputs5 commands17 index-binds3 draws3 buffer-accesses6 late-missing-no-output exact-less independent-opt-in hot0 query0 geometry-native-closed\n",
                descriptor.sourceContractVersion());
        if (report)
            std::printf(
                "BINDLESS_RHI_DESCRIPTOR_CONSUMPTION_OK %s source%u commands14 consumptions3 accesses6 latest-slot-rebind-pass-reset exact-less opt-in hot0 query0 state-native-closed\n",
                graphics ? "graphics" : "compute",
                descriptor.sourceContractVersion());
        return inputs;
    }
};
} // namespace arp::test
