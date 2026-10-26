#pragma once

#include "cpu_quota_factory_fixture.h"
#include "original_command_pipeline_binding_fixture.h"
#include "original_vertex_geometry_execution_fixture.h"
#include "rhi/frontend/command_state.h"
#include "rhi/semantics/command_lowering.h"

#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalCommandPipelineSourceFixture final {
    using Sources = rhi::OriginalCommandResourceSources;
    using Usage = rhi::OriginalCommandUsage;
    static constexpr std::uint64_t bytes = 65536u;

    static void is(const char* actual, const char* expected)
    {
        if (!actual || std::string_view(actual) != expected)
            std::fprintf(stderr,
                "pipeline source expected %s, got %s\n",
                expected,
                actual ? actual : "success");
        assert(actual && std::string_view(actual) == expected);
    }
    template <class Pipeline> using Adapter = rhi::OriginalCommandPipelineSource<Pipeline>;

    template <class Pipeline> static auto budget(const std::shared_ptr<const Pipeline>& pipeline)
    {
        return rhi::OriginalCommandPipelineSourceBudget{1u,
            true,
            Adapter<Pipeline>::kind,
            pipeline->originalResourceRegistration().generation(),
            bytes,
            Adapter<Pipeline>::exclusiveOwnerCount};
    }

public:
    static void verifyContract()
    {
        OriginalCommandPipelineBindingFixture::verifyContract();
        OriginalVertexGeometryExecutionFixture::verifyContract();
        using Compute = rhi::OriginalCommandComputePipelineSource;
        using Graphics = rhi::OriginalCommandGraphicsPipelineSource;
        static_assert(
            !std::is_default_constructible_v<Compute> && !std::is_copy_constructible_v<Graphics>);
        rhi::OriginalCommandPipelineSourceBudget b{1u, true, Compute::kind, 1u, bytes, 8u};
        assert(!Compute::validateBudget(b));
        assert(Graphics::validateBudget(b));
        --b.maxRetainedOwners;
        is(Compute::validateBudget(b), "RHI-ORIGINAL-PIPELINE-SOURCE-OWNER-CAPACITY");
        b.maxRetainedOwners = 8u;
        b.registrationGeneration = 0u;
        assert(Compute::validateBudget(b));
        b.registrationGeneration = 1u;
        b.contractVersion = 2u;
        assert(!Compute::validateBudget(b));
        b.contractVersion = 3u;
        assert(Compute::validateBudget(b));
        b.contractVersion = 1u;
        b.prototypeOptIn = false;
        assert(Compute::validateBudget(b));
        for (const auto backend : std::array{rhi::Backend::Null,
                 rhi::Backend::Vulkan,
                 rhi::Backend::Metal,
                 rhi::Backend::WebGPU}) {
            const auto d =
                arp::rhi::semantics::OriginalCommandPipelineSourceLowering::lowering(backend);
            assert(!d.positionalAdapterAvailable && !d.executionAvailable);
            assert(d.custodyAvailable == (backend == rhi::Backend::WebGPU)
                && d.validatorOnly == (backend == rhi::Backend::Null));
            assert(arp::rhi::semantics::OriginalCommandPipelineSourceLowering::lowering(backend)
                       .custodyAvailable
                == d.custodyAvailable);
        }
        assert(!arp::rhi::semantics::OriginalCommandPipelineSourceLowering::lowering(
            static_cast<rhi::Backend>(255u))
                .custodyAvailable);
        auto source = core::makeShared<core::CpuMemoryQuota>(bytes);
        auto other = core::makeShared<core::CpuMemoryQuota>(bytes);
        auto forgedQuota = std::shared_ptr<core::CpuMemoryQuota>(other, source.get());
        assert(core::SharedOwnerProjection::overlapsOwner(source, forgedQuota));
        assert(!core::SharedOwnerProjection::sameControlBlock(source, forgedQuota));
        assert(!core::SharedOwnerProjection::overlapsOwner(source, other));
        assert(!core::SharedOwnerProjection::overlapsOwner(source, {}));
        auto device = rhi::createNullDevice();
        const char* diagnostic = nullptr;
        b = {1u, true, Compute::kind, 1u, bytes, 8u};
        assert(!Compute::prepareOriginal({}, b, source, *device, diagnostic));
        is(diagnostic,
            arp::rhi::semantics::OriginalCommandPipelineSourceLowering::lowering(rhi::Backend::Null)
                .diagnostic);
        assert(!source->liveBytes());
        std::puts(
            "BINDLESS_RHI_ORIGINAL_PIPELINE_SOURCE_CONTRACT_OK backends4 genuine-accepted-shared-quota-only positional-state-native-closed");
    }

    template <class Pipeline, class Publish, class DeviceFactory>
    static void verifyActual(rhi::GpuDevice& device,
        Publish&& publish,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 7>& factoryQuotas,
        DeviceFactory&& create)
    {
        using A = Adapter<Pipeline>;
        auto source = core::makeShared<core::CpuMemoryQuota>(bytes),
             graph = core::makeShared<core::CpuMemoryQuota>(bytes),
             state = core::makeShared<core::CpuMemoryQuota>(bytes);
        const char* diagnostic = nullptr;
        {
            auto ordinaryShared = core::shareUnique(publish());
            assert(ordinaryShared);
            assert(!A::prepareOriginal(
                ordinaryShared, budget<Pipeline>(ordinaryShared), source, device, diagnostic));
            is(diagnostic, "RHI-ORIGINAL-PIPELINE-SOURCE-SHARED-QUOTA-RECEIPT");
        }
        const auto sharedFactory = [&] {
            return rhi::OriginalRegisteredPipeline::shareAcceptedOriginal(publish(), diagnostic);
        };
        CpuQuotaFactoryFixture wrapperPressure(factoryQuotas[6]);
        const auto sharedPeak = wrapperPressure.measure(sharedFactory);
        wrapperPressure.verify(sharedPeak, sharedFactory);
        is(diagnostic, "RHI-ORIGINAL-PIPELINE-SHARED-ALLOCATION");
        auto first = sharedFactory();
        auto second = sharedFactory();
        assert(first && second);
        assert(first->originalResourceRegistration().generation()
            != second->originalResourceRegistration().generation());
        assert(core::SharedOwnerProjection::sameOwner(
            first->originalLayoutOwner(), second->originalLayoutOwner()));
        assert(core::inspectCpuAllocationContaining(first.get()).quota == factoryQuotas[6].get());
        assert(core::inspectCpuAllocationContaining(first->originalShaderPackageMetadata()).quota
            == factoryQuotas[1].get());
        auto b = budget<Pipeline>(first);
        const auto adapterFactory = [&] {
            return A::prepareOriginal(first, b, source, device, diagnostic);
        };
        CpuQuotaFactoryFixture pressure(source);
        const auto adapterPeak = pressure.measure(adapterFactory);
        auto forged = std::shared_ptr<const Pipeline>(source, first.get());
        assert(!A::prepareOriginal(forged, b, source, device, diagnostic));
        is(diagnostic, "RHI-ORIGINAL-PIPELINE-SOURCE-WRAPPER-OWNER");
        forged.reset();
        const auto reject = [&](auto change, const char* expected) {
            auto invalid = b;
            change(invalid);
            assert(!A::prepareOriginal(first, invalid, source, device, diagnostic));
            is(diagnostic, expected);
        };
        reject(
            [](auto& x) {
                ++x.registrationGeneration;
            },
            "RHI-ORIGINAL-PIPELINE-SOURCE-GENERATION");
        reject(
            [](auto& x) {
                x.kind = rhi::OriginalRegisteredResourceKind::Unsupported;
            },
            "RHI-ORIGINAL-PIPELINE-SOURCE-KIND");
        reject(
            [](auto& x) {
                --x.maxRetainedOwners;
            },
            "RHI-ORIGINAL-PIPELINE-SOURCE-OWNER-CAPACITY");
        reject(
            [](auto& x) {
                --x.sourceCpuBytes;
            },
            "RHI-ORIGINAL-PIPELINE-SOURCE-QUOTA");
        for (const auto& q : factoryQuotas) {
            if (q == factoryQuotas[2])
                continue; // Layout's independent ordinary allocation is not a factory domain.
            auto invalid = b;
            invalid.sourceCpuBytes = q->limitBytes();
            assert(!A::prepareOriginal(first, invalid, q, device, diagnostic));
        }
        const auto input = [&]() {
            if constexpr (std::is_same_v<Pipeline, rhi::RenderPipeline>)
                return first->originalGraphicsCreation()->originalInput();
            else
                return first->originalComputeCreation()->originalInput();
        }();
        for (const auto& q : std::array{input->quota(), input->artifact()->quota()}) {
            auto invalid = b;
            invalid.sourceCpuBytes = q->limitBytes();
            assert(!A::prepareOriginal(first, invalid, q, device, diagnostic));
        }
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        auto a = adapterFactory();
        std::pmr::set_default_resource(previous);
        assert(a);
        auto a2 = A::prepareOriginal(second, budget<Pipeline>(second), source, device, diagnostic);
        assert(a2);
        {
            auto foreignDevice = create();
            assert(foreignDevice);
            auto lifetime = core::makeShared<core::CpuMemoryQuota>(bytes),
                 metadata = core::makeShared<core::CpuMemoryQuota>(bytes);
            assert(
                !foreignDevice->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, lifetime));
            rhi::OriginalResourceRegistrationBudget registration;
            registration.contractVersion = 6u;
            registration.prototypeOptIn = true;
            registration.maxPipelineLayouts = 1u;
            registration.maxComputePipelines = 1u;
            registration.maxRetainedOwners = 11u;
            registration.maxCpuMetadataBytes = bytes;
            assert(!foreignDevice->prepareOriginalResourceRegistration(registration, metadata));
            is(A::validateOriginal(a, *foreignDevice), "RHI-ORIGINAL-REGISTRATION-FOREIGN-DEVICE");
            assert(!A::prepareOriginal(first, b, source, *foreignDevice, diagnostic));
            is(diagnostic, "RHI-ORIGINAL-REGISTRATION-FOREIGN-DEVICE");
            foreignDevice.reset();
            assert(!lifetime->liveBytes() && !metadata->liveBytes());
        }
        auto fakeAdapter = std::shared_ptr<const A>(source, a.get());
        is(A::validateOriginal(fakeAdapter), "RHI-ORIGINAL-PIPELINE-SOURCE-OWNER");
        fakeAdapter.reset();
        rhi::OriginalCommandAdmissionBudget admission;
        admission.contractVersion = 1u;
        admission.prototypeOptIn = true;
        constexpr auto cpuExpected = std::is_same_v<Pipeline, rhi::RenderPipeline> ? 18u : 16u;
        admission.maxCommands = 8u;
        admission.maxReferences = 3u;
        admission.maxGraphOwners = cpuExpected + 3u;
        admission.maxInputDescriptors = 5u;
        admission.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Pipeline)] =
            2u;
        admission.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Buffer)] =
            1u;
        admission.maxCpuBytes.fill(bytes);
        rhi::BufferDesc desc;
        desc.sizeBytes = 16u;
        desc.usage = rhi::ResourceUsage::StorageBuffer;
        core::UniquePtr<rhi::GpuBuffer> object;
        {
            core::CpuMemoryQuotaScope scope(factoryQuotas[2]);
            object = device.createBuffer(desc);
        }
        assert(object);
        auto lease = rhi::OriginalCommandBufferLease::prepareOriginal(
            object, {1u, true, 2u, bytes}, source, diagnostic);
        assert(lease && !object);
        Sources::Source prefix{lease};
        const rhi::OriginalCommandSourceAssemblyBudget assembly{2u, true, 2u};
        const auto rootFactory = [&] {
            return Sources::prepareColdOriginalTyped(
                &prefix, 1u, admission, assembly, source, diagnostic);
        };
        const auto rootPeak = pressure.measure(rootFactory);
        // Measure every Source-domain peak before pressure raises its high water.
        pressure.verify(adapterPeak, adapterFactory);
        pressure.verify(rootPeak, rootFactory);
        {
            core::CpuMemoryQuotaLease full(source, bytes - source->liveBytes());
            assert(full.charged() && !adapterFactory());
            is(diagnostic, "RHI-ORIGINAL-PIPELINE-SOURCE-ALLOCATION");
        }
        auto root = rootFactory();
        assert(root);
        const auto* exact = root.get();
        auto legacyBudget = admission;
        legacyBudget
            .maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Descriptor)] =
            1u;
        auto legacy = Sources::prepareColdOriginalTyped(
            &prefix, 1u, legacyBudget, {1u, true, 1u}, source, diagnostic);
        assert(legacy);
        is(Sources::appendOriginalTyped(legacy, Sources::Source{a}, device),
            "RHI-ORIGINAL-SOURCES-ASSEMBLY-PIPELINE-VERSION");
        legacy.reset();
        auto foreign = graph;
        auto foreignAdapter = A::prepareOriginal(first, b, foreign, device, diagnostic);
        assert(foreignAdapter);
        is(Sources::appendOriginalTyped(root, Sources::Source{foreignAdapter}, device),
            "RHI-ORIGINAL-SOURCES-SOURCE-QUOTA");
        foreignAdapter.reset();
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(factoryQuotas, [&]() noexcept {
            return Sources::appendOriginalTyped(root, Sources::Source{a}, device);
        }));
        is(Sources::appendOriginalTyped(root, Sources::Source{a}, device),
            "RHI-ORIGINAL-SOURCES-DUPLICATE");
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{source, graph, state}, [&]() noexcept {
                return Sources::appendOriginalTyped(root, Sources::Source{a2}, device);
            }));
        assert(root.get() == exact && root->sources().size() == 3u
            && Sources::resource(root->sources()[0]) == lease->resource());
        assert(!Sources::resource(root->sources()[1]).isValid()
            && !Sources::resource(root->sources()[2]).isValid());
        std::uint32_t owners = 0u, registrations = 0u, total = 0u;
        assert(!root->cpuOwnerCount(2u, cpuExpected, owners) && owners == cpuExpected);
        is(root->cpuOwnerCount(2u, owners - 1u, total), "RHI-ORIGINAL-USAGE-OWNER-CAPACITY");
        assert(!Sources::registeredOwnerUnion(root, owners, owners + 7u, 4u, total, registrations)
            && total == owners + 7u && registrations == 4u);
        is(Sources::registeredOwnerUnion(root, owners, owners + 6u, 4u, total, registrations),
            "RHI-ORIGINAL-STATE-OWNER-CAPACITY");
        is(Sources::registeredOwnerUnion(root, owners, owners + 7u, 3u, total, registrations),
            "RHI-ORIGINAL-STATE-REGISTRATION-CAPACITY");
        using K = rhi::CommandUsageKind;
        const rhi::CommandUsage finish{K::Finish, 0u, 0u};
        const auto usageFactory = [&] {
            return Usage::prepareOriginal(root, &finish, 1u, nullptr, 0u, graph, diagnostic);
        };
        {
            core::CpuMemoryQuotaLease full(graph, bytes);
            assert(full.charged() && !usageFactory());
            assert(!root->inventorySealed());
        }
        auto usage = usageFactory();
        assert(usage && root->inventorySealed() && usage->retainedOwnerCount() == owners);
        is(Sources::appendOriginalTyped(root, Sources::Source{a2}, device),
            "RHI-ORIGINAL-SOURCES-ASSEMBLY-SEALED");
        using Scalar = rhi::CommandScalarArguments;
        if constexpr (std::is_same_v<Pipeline, rhi::ComputePipeline>) {
            const std::array<rhi::CommandUsage, 4u> commands{{{K::BeginComputePass, 0u, 0u},
                {K::BindComputePipeline, 0u, 1u},
                {K::EndComputePass, 1u, 0u},
                {K::Finish, 1u, 0u}}};
            const Usage::Reference pipelineReference{1u};
            auto pipelineUsage = Usage::prepareOriginal(
                root, commands.data(), commands.size(), &pipelineReference, 1u, graph, diagnostic);
            assert(pipelineUsage);
            assert(!pipelineUsage->compareOriginalReference(0u, *a->originalObject()));
            is(pipelineUsage->compareOriginalReference(0u, *a2->originalObject()),
                "RHI-ORIGINAL-USAGE-REFERENCE-OBJECT");
            const std::array<rhi::CommandVariableArguments::Input, 4u> fields{
                {{*Scalar::capture(K::BeginComputePass, Scalar::None{}), {}},
                    {*Scalar::capture(K::BindComputePipeline, Scalar::None{}), {}},
                    {*Scalar::capture(K::EndComputePass, Scalar::None{}), {}},
                    {*Scalar::capture(K::Finish, Scalar::None{}), {}}}};
            auto pipelineArguments = rhi::CommandVariableArguments::prepareOriginal(
                fields.data(), fields.size(), {1u, true, 4u, 0u, 0u, 0u, bytes}, graph, diagnostic);
            assert(pipelineArguments);
            auto pipelineScalars = rhi::OriginalCommandScalars::prepareOriginal(
                pipelineUsage, pipelineArguments, graph, diagnostic);
            assert(pipelineScalars);
            assert(!rhi::OriginalCommandInputs::prepareOriginal(
                pipelineScalars, nullptr, 0u, graph, diagnostic));
            is(diagnostic, "RHI-ORIGINAL-TYPED-INPUT-POSITIONAL-JOIN-MISSING");
            std::puts(
                "BINDLESS_RHI_ORIGINAL_PIPELINE_SOURCE_POSITIONAL_GATE_OK genuine-compute-reference correct-order fields-owned inputs-fail-closed");
        }
        const rhi::CommandVariableArguments::Input scalar{
            *Scalar::capture(K::Finish, Scalar::None{}), {}};
        auto arguments = rhi::CommandVariableArguments::prepareOriginal(
            &scalar, 1u, {1u, true, 1u, 0u, 0u, 0u, bytes}, graph, diagnostic);
        assert(arguments);
        auto scalars =
            rhi::OriginalCommandScalars::prepareOriginal(usage, arguments, graph, diagnostic);
        assert(scalars);
        auto inputs =
            rhi::OriginalCommandInputs::prepareOriginal(scalars, nullptr, 0u, graph, diagnostic);
        assert(inputs);
        auto positionalGraph = core::makeShared<core::CpuMemoryQuota>(bytes);
        std::shared_ptr<const rhi::OriginalCommandInputs> positional;
        if constexpr (std::is_same_v<Pipeline, rhi::ComputePipeline>)
            positional = OriginalCommandPipelineBindingFixture::verifyActual<Pipeline>(
                root, positionalGraph, device);
        const rhi::OriginalCommandStateAdmissionBudget stateBudget{
            1u, true, 1u, 4u, 1u, 1u, 32u, bytes};
        assert(!rhi::OriginalCommandBufferStates::prepareOriginal(
            inputs, device, stateBudget, state, diagnostic));
        is(diagnostic, "RHI-ORIGINAL-TYPED-PIPELINE-STATE-JOIN-MISSING");
        assert(!rhi::OriginalCommandResourceStates::prepareOriginal(
            inputs, device, stateBudget, state, diagnostic));
        is(diagnostic, "RHI-ORIGINAL-TYPED-PIPELINE-STATE-JOIN-MISSING");
        if (positional) {
            assert(!rhi::OriginalCommandBufferStates::prepareOriginal(
                positional, device, stateBudget, state, diagnostic));
            is(diagnostic, "RHI-ORIGINAL-TYPED-PIPELINE-STATE-JOIN-MISSING");
            assert(!rhi::OriginalCommandResourceStates::prepareOriginal(
                positional, device, stateBudget, state, diagnostic));
            is(diagnostic, "RHI-ORIGINAL-TYPED-PIPELINE-STATE-JOIN-MISSING");
        }
        first.reset();
        second.reset();
        assert(!A::validateOriginal(a) && !A::validateOriginal(a2));
        if (positional)
            assert(!positional->validate());
        assert(core::SharedOwnerProjection::sameOwner(a->originalPipeline()->originalPackageOwner(),
            std::static_pointer_cast<const rhi::ShaderPackageDesc>(a->cpuOwner(2u))));
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{source, graph, state}, [&]() noexcept {
                return Usage::validateOriginal(usage);
            }));
        assert(!root->originalStateTransactionCount() && !root->originalReservedSubmissionCount()
            && !root->originalReservedCallbackTicketCount());
        std::weak_ptr<const Sources> rootObserver = root;
        root.reset();
        assert(!rootObserver.expired());
        inputs.reset();
        scalars.reset();
        arguments.reset();
        usage.reset();
        assert(rootObserver.expired() == !bool(positional));
        positional.reset();
        assert(rootObserver.expired());
        assert(!A::validateOriginal(a));
        a.reset();
        a2.reset();
        prefix = Sources::Source{};
        lease.reset();
        rootObserver.reset();
        assert(!source->liveBytes() && !graph->liveBytes() && !state->liveBytes()
            && !positionalGraph->liveBytes());
        std::printf(
            "BINDLESS_RHI_ORIGINAL_PIPELINE_SOURCE_NATIVE_OK kind%u adapter%llu root%llu shared%llu cpu%u records4 total%u original-custody common-layout-and-receipt-dedup pressure-retry append-hot0 stable-prefix permanent-seal state-native-closed acyclic\n",
            static_cast<unsigned>(A::kind),
            static_cast<unsigned long long>(adapterPeak),
            static_cast<unsigned long long>(rootPeak),
            static_cast<unsigned long long>(sharedPeak),
            owners,
            owners + 7u);
    }

    template <class Pipeline>
    static void retainForLoss(const std::shared_ptr<Pipeline>& pipeline,
        rhi::GpuDevice& device,
        const std::shared_ptr<core::CpuMemoryQuota>& resources)
    {
        auto& retained = loss<Pipeline>();
        retained.quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        const char* diagnostic = nullptr;
        retained.adapter = Adapter<Pipeline>::prepareOriginal(
            pipeline, budget<Pipeline>(pipeline), retained.quota, device, diagnostic);
        assert(retained.adapter);
        retained.graph = core::makeShared<core::CpuMemoryQuota>(bytes);
        retained.inputs = OriginalCommandPipelineBindingFixture::retainForLoss<Pipeline>(
            retained.adapter, device, retained.quota, retained.graph, resources);
    }

    template <class Pipeline> static void verifyLoss()
    {
        auto& retained = loss<Pipeline>();
        assert(retained.adapter && Adapter<Pipeline>::validateOriginal(retained.adapter));
        assert(retained.inputs && retained.inputs->validate());
        retained.inputs.reset();
        assert(!retained.graph->liveBytes());
        retained.graph.reset();
        std::printf(
            "BINDLESS_RHI_PIPELINE_POSITIONAL_LOSS_OK kind%u physical-loss input-and-source-reject graph-quota-zero\n",
            static_cast<unsigned>(Adapter<Pipeline>::kind));
        retained.adapter.reset();
        assert(!retained.quota->liveBytes());
        retained.quota.reset();
        std::printf(
            "BINDLESS_RHI_ORIGINAL_PIPELINE_SOURCE_LOSS_OK kind%u physical-loss rejects-custody source-quota-zero\n",
            static_cast<unsigned>(Adapter<Pipeline>::kind));
    }

private:
    template <class Pipeline> struct Loss {
        std::shared_ptr<core::CpuMemoryQuota> quota, graph;
        std::shared_ptr<const Adapter<Pipeline>> adapter;
        std::shared_ptr<const rhi::OriginalCommandInputs> inputs;
    };

    template <class Pipeline> static Loss<Pipeline>& loss()
    {
        static Loss<Pipeline> value;
        return value;
    }
};
} // namespace arp::test
