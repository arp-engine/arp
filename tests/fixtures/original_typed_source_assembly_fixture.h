#pragma once
#include "cpu_quota_factory_fixture.h"
#include "original_command_table_binding_fixture.h"
#include "rhi/frontend/command_state.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalTypedSourceAssemblyFixture final {
    using Sources = rhi::OriginalCommandResourceSources;
    using Set = rhi::OriginalCommandDescriptorSetSource;
    using Table = rhi::OriginalCommandDescriptorTableSource;
    using Usage = rhi::OriginalCommandUsage;

    static void is(const char* actual, const char* expected)
    {
        if (!actual || std::string_view(actual) != expected)
            std::fprintf(stderr,
                "typed assembly expected %s, got %s\n",
                expected,
                actual ? actual : "success");
        assert(actual && std::string_view(actual) == expected);
    }

public:
    static void verify(rhi::GpuDevice& device,
        const std::shared_ptr<core::CpuMemoryQuota>& sourceQuota,
        const std::array<Sources::Source, 3u>& prefix,
        rhi::OriginalCommandAdmissionBudget budget,
        const rhi::DescriptorLayoutDesc& layout,
        const std::array<Set::Element, 2u>& elements,
        const rhi::DescriptorTableDesc& tableDesc,
        const Table::Element& tableElement)
    {
        const auto bytes = sourceQuota->limitBytes(), baseline = sourceQuota->liveBytes();
        auto objects = core::makeShared<core::CpuMemoryQuota>(bytes),
             graphQuota = core::makeShared<core::CpuMemoryQuota>(bytes),
             stateQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Descriptor)] =
            1u;
        budget.maxSourceOwners[static_cast<std::size_t>(
            rhi::CommandReferenceDomain::DescriptorTable)] = 1u;
        const rhi::OriginalCommandSourceAssemblyBudget assembly{1u, true, 2u};
        const rhi::OriginalCommandDescriptorSourceBudget descriptorBudget{
            2u, true, 2u, 2u, 5u, bytes};
        const char* diagnostic = nullptr;
        {
            core::CpuMemoryQuotaScope objectScope(objects);
            const auto rootFactory = [&] {
                return Sources::prepareColdOriginalTyped(
                    prefix.data(), prefix.size(), budget, assembly, sourceQuota, diagnostic);
            };
            CpuQuotaFactoryFixture sourcePressure(sourceQuota);
            const auto rootPeak = sourcePressure.measure(rootFactory);
            auto root = rootFactory();
            assert(root && !root->inventorySealed());
            const auto* exactRoot = root.get();
            const auto checkAssembly = [&](auto change, const char* expected) {
                auto b = assembly;
                change(b);
                assert(!Sources::prepareColdOriginalTyped(
                    prefix.data(), prefix.size(), budget, b, sourceQuota, diagnostic));
                is(diagnostic, expected);
            };
            checkAssembly(
                [](auto& b) {
                    b.contractVersion = 0u;
                },
                "RHI-ORIGINAL-SOURCES-ASSEMBLY-VERSION");
            checkAssembly(
                [](auto& b) {
                    b.prototypeOptIn = false;
                },
                "RHI-ORIGINAL-SOURCES-ASSEMBLY-OPT-IN");
            checkAssembly(
                [](auto& b) {
                    --b.maxTypedSources;
                },
                "RHI-ORIGINAL-SOURCES-ASSEMBLY-CAPACITY");
            rhi::DescriptorArenaDesc ad;
            ad.label = "CanonicalTypedSourceArena";
            ad.maxSets = 2u;
            ad.maxDescriptors = 4u;
            auto arena = core::shareUnique(device.createOriginalDescriptorArena(ad));
            assert(arena);
            rhi::DescriptorSetDesc sd{"CanonicalTypedSet", rhi::DescriptorStateCopy::copy(layout)};
            const auto freshSet = [&] {
                arena->reset();
                auto set = device.allocateOriginalDescriptorSet(arena, sd);
                assert(set);
                for (const auto& element : elements)
                    assert(set->write(element.write).success);
                return set;
            };
            const auto freshTable = [&] {
                arena->reset();
                auto table = device.allocateOriginalDescriptorTable(arena, tableDesc);
                assert(table);
                assert(table->write(tableElement.write).success);
                return table;
            };
            auto set = freshSet();
            auto legacyBudget = descriptorBudget;
            legacyBudget.contractVersion = 1u;
            legacyBudget.maxExclusiveRetainedOwners = 6u;
            auto legacy = Set::prepareOriginal(set,
                arena,
                root,
                elements.data(),
                2u,
                legacyBudget,
                sourceQuota,
                device,
                diagnostic);
            assert(legacy);
            is(Sources::appendOriginalTyped(root, Sources::Source{legacy}, device),
                "RHI-ORIGINAL-SOURCES-ASSEMBLY-DESCRIPTOR-VERSION");
            legacy.reset();
            auto foreignRoot = rootFactory();
            set = freshSet();
            auto foreign = Set::prepareOriginal(set,
                arena,
                foreignRoot,
                elements.data(),
                2u,
                descriptorBudget,
                sourceQuota,
                device,
                diagnostic);
            assert(foreign);
            is(Sources::appendOriginalTyped(root, Sources::Source{foreign}, device),
                "RHI-ORIGINAL-SOURCES-ASSEMBLY-FOREIGN-INVENTORY");
            foreign.reset();
            foreignRoot.reset();
            set = freshSet();
            const auto setFactory = [&] {
                const auto* candidate = set.get();
                auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
                auto result = Set::prepareOriginal(set,
                    arena,
                    root,
                    elements.data(),
                    2u,
                    descriptorBudget,
                    sourceQuota,
                    device,
                    diagnostic);
                std::pmr::set_default_resource(previous);
                assert(result ? !set : set.get() == candidate);
                return result;
            };
            const auto setPeak = sourcePressure.measure(setFactory);
            auto table = freshTable();
            const auto tableFactory = [&] {
                const auto* candidate = table.get();
                auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
                auto result = Table::prepareOriginal(table,
                    arena,
                    root,
                    &tableElement,
                    1u,
                    descriptorBudget,
                    sourceQuota,
                    device,
                    diagnostic);
                std::pmr::set_default_resource(previous);
                assert(result ? !table : table.get() == candidate);
                return result;
            };
            const auto tablePeak = sourcePressure.measure(tableFactory);
            sourcePressure.verify(rootPeak, rootFactory);
            sourcePressure.verifySingleUse(
                setPeak,
                [&] {
                    set.reset();
                    set = freshSet();
                },
                setFactory);
            set.reset();
            sourcePressure.verifySingleUse(
                tablePeak,
                [&] {
                    table.reset();
                    table = freshTable();
                },
                tableFactory);
            table.reset();
            arena->reset();
            set = device.allocateOriginalDescriptorSet(arena, sd);
            assert(set);
            for (const auto& element : elements)
                assert(set->write(element.write).success);
            auto* mutableSet = set.get();
            auto setSource = setFactory();
            assert(setSource);
            table = device.allocateOriginalDescriptorTable(arena, tableDesc);
            assert(table && table->write(tableElement.write).success);
            auto tableSource = tableFactory();
            assert(tableSource);
            auto* mutableTable = const_cast<rhi::DescriptorTable*>(tableSource->originalObject());
            auto forged = std::shared_ptr<const Set>(sourceQuota, setSource.get());
            is(Sources::appendOriginalTyped(root, Sources::Source{forged}, device),
                "RHI-ORIGINAL-DESCRIPTOR-SOURCE-OWNER");
            assert(root->sources().size() == prefix.size());
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{sourceQuota, objects}, [&]() noexcept {
                    return Sources::appendOriginalTyped(root, Sources::Source{setSource}, device);
                }));
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{sourceQuota, objects}, [&]() noexcept {
                    return Sources::appendOriginalTyped(root, Sources::Source{tableSource}, device);
                }));
            assert(root.get() == exactRoot
                && root->sources().size() == prefix.size() + assembly.maxTypedSources);
            OriginalCommandTableBindingFixture::verify(root, prefix.size() + 1u, device);
            for (std::size_t i = 0u; i < prefix.size(); ++i)
                assert(Sources::resource(root->sources()[i]) == Sources::resource(prefix[i]));
            is(Sources::appendOriginalTyped(root, Sources::Source{setSource}, device),
                "RHI-ORIGINAL-SOURCES-DUPLICATE");
            assert(!Sources::validateRegisteredLifetime(root, device));
            std::uint32_t cpuOwners = 0u, owners = 0u, records = 0u;
            assert(!root->cpuOwnerCount(2u, 14u, cpuOwners) && cpuOwners == 14u);
            is(root->cpuOwnerCount(2u, 13u, cpuOwners), "RHI-ORIGINAL-USAGE-OWNER-CAPACITY");
            assert(!Sources::registeredOwnerUnion(root, 14u, 23u, 6u, owners, records)
                && owners == 23u && records == 6u);
            is(Sources::registeredOwnerUnion(root, 14u, 22u, 6u, owners, records),
                "RHI-ORIGINAL-STATE-OWNER-CAPACITY");
            is(Sources::registeredOwnerUnion(root, 14u, 23u, 5u, owners, records),
                "RHI-ORIGINAL-STATE-REGISTRATION-CAPACITY");
            // No fake Pipeline source/order bypass to demonstrate a descriptor bind.
            using K = rhi::CommandUsageKind;
            const std::array<rhi::CommandUsage, 4u> binds{{{K::BeginComputePass, 0u, 0u},
                {K::BindComputeDescriptor, 0u, 1u},
                {K::EndComputePass, 1u, 0u},
                {K::Finish, 1u, 0u}}};
            const Usage::Reference descriptorRef{3u};
            assert(!Usage::prepareOriginal(
                root, binds.data(), binds.size(), &descriptorRef, 1u, graphQuota, diagnostic));
            is(diagnostic, "RHI-COMMAND-ORDER-PIPELINE");
            assert(!root->inventorySealed());
            const std::array<rhi::CommandUsage, 2u> commands{
                {{K::RetainTexture, 0u, 1u}, {K::Finish, 1u, 0u}}};
            const Usage::Reference textureRef{1u};
            const auto graphFactory = [&] {
                return Usage::prepareOriginal(root,
                    commands.data(),
                    commands.size(),
                    &textureRef,
                    1u,
                    graphQuota,
                    diagnostic);
            };
            {
                core::CpuMemoryQuotaLease full(graphQuota, bytes);
                assert(full.charged());
                assert(!graphFactory());
                is(diagnostic, "RHI-ORIGINAL-USAGE-ALLOCATION");
                assert(!root->inventorySealed());
            }
            auto usage = graphFactory();
            assert(usage && root->inventorySealed() && usage->retainedOwnerCount() == 14u);
            assert(core::SharedOwnerProjection::sameOwner(usage->originalSources(), root));
            is(Sources::appendOriginalTyped(root, Sources::Source{setSource}, device),
                "RHI-ORIGINAL-SOURCES-ASSEMBLY-SEALED");
            using Scalar = rhi::CommandScalarArguments;
            const std::array<rhi::CommandVariableArguments::Input, 2u> input{
                {{*Scalar::capture(K::RetainTexture, Scalar::None{}), {}},
                    {*Scalar::capture(K::Finish, Scalar::None{}), {}}}};
            auto arguments = rhi::CommandVariableArguments::prepareOriginal(input.data(),
                input.size(),
                {1u, true, 2u, 0u, 0u, 0u, bytes},
                graphQuota,
                diagnostic);
            assert(arguments);
            auto scalars = rhi::OriginalCommandScalars::prepareOriginal(
                usage, arguments, graphQuota, diagnostic);
            assert(scalars);
            auto inputs = rhi::OriginalCommandInputs::prepareOriginal(
                scalars, nullptr, 0u, graphQuota, diagnostic);
            assert(inputs);
            const rhi::OriginalCommandStateAdmissionBudget stateBudget{
                1u, true, 1u, 6u, 2u, 1u, 24u, bytes};
            assert(!rhi::OriginalCommandResourceStates::prepareOriginal(
                inputs, device, stateBudget, stateQuota, diagnostic));
            is(diagnostic, "RHI-ORIGINAL-TYPED-DESCRIPTOR-STATE-JOIN-MISSING");
            assert(!rhi::OriginalCommandBufferStates::prepareOriginal(
                inputs, device, stateBudget, stateQuota, diagnostic));
            is(diagnostic, "RHI-ORIGINAL-TYPED-DESCRIPTOR-STATE-JOIN-MISSING");
            assert(root->originalStateTransactionCount() == 0u
                && root->originalReservedSubmissionCount() == 0u);
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{sourceQuota, graphQuota, stateQuota, objects}, [&]() noexcept {
                    return Usage::validateOriginal(usage);
                }));
            assert(mutableSet->write(elements[0].write).success);
            is(Usage::validateOriginal(usage), "RHI-DESCRIPTOR-CONTENT-CHANGED");
            assert(mutableTable->write(tableElement.write).success);
            OriginalCommandTableBindingFixture::verifyInvalidated(*root, prefix.size() + 1u);
            // Retain the canonical root through the existing Inputs while
            // releasing caller objects. The independent table diagnostic sees
            // changed successful-write content without creating another root.
            std::weak_ptr<const Sources> rootObserver = root;
            arena.reset();
            root.reset();
            assert(!rootObserver.expired());
            inputs.reset();
            scalars.reset();
            arguments.reset();
            usage.reset();
            assert(rootObserver.expired());
            is(Set::validateOriginal(setSource),
                "RHI-ORIGINAL-DESCRIPTOR-SOURCE-INVENTORY-EXPIRED");
            forged.reset();
            setSource.reset();
            tableSource.reset();
            rootObserver.reset();
            std::printf(
                "BINDLESS_RHI_ORIGINAL_TYPED_ASSEMBLY_OK backend%u root%llu set%llu table%llu same-source stable-indices weak-parent exact-dependencies append-allocation0 arena-record-dedup owners14-records6-total23 failed-publication-cold successful-publication-sealed no-cycle pipeline-input-state-native-closed\n",
                static_cast<unsigned>(device.backend()),
                static_cast<unsigned long long>(rootPeak),
                static_cast<unsigned long long>(setPeak),
                static_cast<unsigned long long>(tablePeak));
        }
        assert(sourceQuota->liveBytes() == baseline && !objects->liveBytes()
            && !graphQuota->liveBytes() && !stateQuota->liveBytes());
    }
};
} // namespace arp::test
