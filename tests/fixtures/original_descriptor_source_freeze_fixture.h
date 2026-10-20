#pragma once

#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/command_sources.h"
#include "rhi/semantics/command_lowering.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
// Reuses the typed-source fixture's genuine registered resource inventory.
class OriginalDescriptorSourceFreezeFixture final {
    using Set = rhi::OriginalCommandDescriptorSetSource;
    using Table = rhi::OriginalCommandDescriptorTableSource;

    static void is(const char* actual, const char* expected)
    {
        if (!actual || std::string_view(actual) != expected)
            std::fprintf(stderr,
                "descriptor Source expected %s, got %s\n",
                expected,
                actual ? actual : "success");
        assert(actual && std::string_view(actual) == expected);
    }

public:
    static void verify(rhi::GpuDevice& device,
        rhi::GpuDevice& foreign,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& resources,
        const rhi::DescriptorLayoutDesc& layout,
        const std::array<Set::Element, 2u>& elements,
        const rhi::DescriptorTableDesc& td,
        const Table::Element& tableElement)
    {
        static_assert(
            !std::is_default_constructible_v<Set> && !std::is_default_constructible_v<Table>);
        const auto baseline = quota->liveBytes();
        const auto bytes = quota->limitBytes();
        const rhi::OriginalCommandDescriptorSourceBudget budget{1u, true, 2u, 2u, 6u, bytes};
        const char* diagnostic = nullptr;
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision =
                arp::rhi::semantics::OriginalCommandDescriptorSourceLowering::lowering(backend);
            assert(decision.mode == rhi::CapabilityMode::Equivalent && decision.freezeAvailable
                && !decision.positionalAdapterAvailable && !decision.executionAvailable);
        }
        auto objects = core::makeShared<core::CpuMemoryQuota>(bytes);
        {
            core::CpuMemoryQuotaScope objectScope(objects);
            rhi::DescriptorArenaDesc ad;
            ad.label = "FrozenOriginalArena";
            ad.maxSets = 2u;
            ad.maxDescriptors = 4u;
            auto arena = core::shareUnique(device.createOriginalDescriptorArena(ad));
            auto otherArena = core::shareUnique(device.createOriginalDescriptorArena(ad));
            assert(arena && otherArena);
            rhi::DescriptorSetDesc sd{"FrozenOriginalSet", rhi::DescriptorStateCopy::copy(layout)};
            const auto freshSet = [&] {
                arena->reset();
                auto object = device.allocateOriginalDescriptorSet(arena, sd);
                assert(object);
                for (const auto& element : elements)
                    assert(object->write(element.write).success);
                return object;
            };
            auto set = freshSet();
            const auto* exact = set.get();
            const auto freeze =
                [&](const Set::Element* input,
                    std::size_t count,
                    const rhi::OriginalCommandDescriptorSourceBudget* overrideBudget = nullptr) {
                    auto* previous =
                        std::pmr::set_default_resource(std::pmr::null_memory_resource());
                    auto result = Set::prepareOriginal(set,
                        arena,
                        resources,
                        input,
                        count,
                        overrideBudget ? *overrideBudget : budget,
                        quota,
                        device,
                        diagnostic);
                    std::pmr::set_default_resource(previous);
                    return result;
                };
            auto changedBudget = budget;
            changedBudget.contractVersion = 0u;
            assert(!freeze(elements.data(), 2u, &changedBudget));
            is(diagnostic, "RHI-ORIGINAL-DESCRIPTOR-SOURCE-VERSION");
            changedBudget = budget;
            changedBudget.prototypeOptIn = false;
            assert(!freeze(elements.data(), 2u, &changedBudget));
            is(diagnostic, "RHI-ORIGINAL-DESCRIPTOR-SOURCE-OPT-IN");
            changedBudget = budget;
            --changedBudget.maxExclusiveRetainedOwners;
            assert(!freeze(elements.data(), 2u, &changedBudget));
            is(diagnostic, "RHI-ORIGINAL-DESCRIPTOR-SOURCE-OWNER-CAPACITY");
            changedBudget = budget;
            changedBudget.maxLogicalElements = 1u;
            changedBudget.maxWrittenElements = 1u;
            assert(!freeze(elements.data(), 1u, &changedBudget));
            is(diagnostic, "RHI-ORIGINAL-DESCRIPTOR-SOURCE-LOGICAL-CAPACITY");
            assert(!freeze(elements.data(), 1u));
            is(diagnostic, "RHI-ORIGINAL-DESCRIPTOR-SOURCE-INCOMPLETE");
            const auto reject = [&](auto change, const char* expected) {
                auto input = elements;
                change(input);
                assert(!freeze(input.data(), input.size()));
                is(diagnostic, expected);
                assert(set.get() == exact);
            };
            reject(
                [](auto& e) {
                    e[1].write.binding = e[0].write.binding;
                    e[1].write.arrayElement = e[0].write.arrayElement;
                },
                "RHI-ORIGINAL-TYPED-DESCRIPTOR-ELEMENT-DUPLICATE");
            reject(
                [](auto& e) {
                    e[0].sourceIndex = SIZE_MAX;
                },
                "RHI-ORIGINAL-TYPED-DESCRIPTOR-SOURCE-INDEX");
            reject(
                [](auto& e) {
                    ++e[0].registrationGeneration;
                },
                "RHI-ORIGINAL-TYPED-DESCRIPTOR-GENERATION");
            reject(
                [](auto& e) {
                    ++e[0].write.physicalLowering.shaderPackageHash;
                },
                "RHI-ORIGINAL-DESCRIPTOR-SOURCE-WRITE-MISMATCH");
            reject(
                [](auto& e) {
                    ++e[0].write.bufferBindingRange.contractVersion;
                },
                "RHI-ORIGINAL-DESCRIPTOR-SOURCE-WRITE-MISMATCH");
            reject(
                [](auto& e) {
                    ++e[0].write.bufferBindingRange.offsetBytes;
                },
                "RHI-ORIGINAL-DESCRIPTOR-SOURCE-WRITE-MISMATCH");
            reject(
                [](auto& e) {
                    ++e[0].write.bufferBindingRange.sizeBytes;
                },
                "RHI-ORIGINAL-DESCRIPTOR-SOURCE-WRITE-MISMATCH");
            auto forgedArena = std::shared_ptr<const rhi::DescriptorArena>(quota, arena.get());
            assert(!Set::prepareOriginal(set,
                forgedArena,
                resources,
                elements.data(),
                2u,
                budget,
                quota,
                device,
                diagnostic));
            is(diagnostic, "RHI-ORIGINAL-DESCRIPTOR-SOURCE-ARENA-OWNER");
            assert(!Set::prepareOriginal(set,
                otherArena,
                resources,
                elements.data(),
                2u,
                budget,
                quota,
                device,
                diagnostic));
            is(diagnostic, "RHI-ORIGINAL-DESCRIPTOR-SOURCE-PARENT");
            assert(!Set::prepareOriginal(
                set, arena, resources, elements.data(), 2u, budget, quota, foreign, diagnostic));
            is(diagnostic, "RHI-ORIGINAL-REGISTRATION-FOREIGN-DEVICE");
            auto forgedResources =
                std::shared_ptr<const rhi::OriginalCommandResourceSources>(quota, resources.get());
            assert(!Set::prepareOriginal(set,
                arena,
                forgedResources,
                elements.data(),
                2u,
                budget,
                quota,
                device,
                diagnostic));
            is(diagnostic, "RHI-ORIGINAL-SOURCES-OWNER");
            // Original observations and equal metadata cannot admit an ordinary wrapper.
            auto ordinary = arena->allocateDescriptorSet(sd);
            assert(ordinary);
            for (const auto& element : elements)
                assert(ordinary->write(element.write).success);
            assert(!Set::prepareOriginal(ordinary,
                arena,
                resources,
                elements.data(),
                2u,
                budget,
                quota,
                device,
                diagnostic));
            is(diagnostic, "RHI-ORIGINAL-REGISTRATION-EXPIRED");
            ordinary.reset();
            CpuQuotaFactoryFixture pressure(quota);
            const auto sourceFactory = [&] {
                const auto* candidate = set.get();
                auto result = freeze(elements.data(), 2u);
                assert(result ? !set : set.get() == candidate);
                return result;
            };
            const auto peak = pressure.measure(sourceFactory);
            assert(!set);
            const auto freshTable = [&] {
                arena->reset();
                auto object = device.allocateOriginalDescriptorTable(arena, td);
                assert(object);
                assert(object->write(tableElement.write).success);
                return object;
            };
            auto table = freshTable();
            const auto tableFactory = [&] {
                auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
                const auto* candidate = table.get();
                auto result = Table::prepareOriginal(
                    table, arena, resources, &tableElement, 1u, budget, quota, device, diagnostic);
                std::pmr::set_default_resource(previous);
                assert(result ? !table : table.get() == candidate);
                return result;
            };
            const auto tablePeak = pressure.measure(tableFactory);
            assert(!table);
            pressure.verifySingleUse(
                peak,
                [&] {
                    set.reset();
                    set = freshSet();
                },
                sourceFactory);
            set.reset();
            set = freshSet();
            auto* mutableOriginal = set.get();
            const auto originalCharge = core::inspectCpuAllocationContaining(mutableOriginal);
            assert(originalCharge.found && originalCharge.quota == objects.get());
            auto owned = sourceFactory();
            assert(owned && !set && !diagnostic && owned->originalObject() == mutableOriginal);
            const auto retainedCharge =
                core::inspectCpuAllocationContaining(owned->originalObject());
            assert(retainedCharge.allocationIdentity == originalCharge.allocationIdentity
                && retainedCharge.quota == objects.get());
            assert(core::SharedOwnerProjection::sameOwner(
                owned->originalResourceSources(), resources));
            const auto preserved = owned->frozenDefinition().name;
            sd.layout.name = "CallerChanged";
            assert(owned->frozenDefinition().name == preserved
                && owned->frozenElements().size() == elements.size());
            assert(
                !CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{quota}, [&]() noexcept {
                    return Set::validateOriginal(owned, device);
                }));
            auto forgedSource = std::shared_ptr<const Set>(quota, owned.get());
            is(Set::validateOriginal(forgedSource, device), "RHI-ORIGINAL-DESCRIPTOR-SOURCE-OWNER");
            auto invalid = elements[0].write;
            invalid.arrayElement = 2u;
            assert(!mutableOriginal->write(invalid).success);
            assert(!Set::validateOriginal(owned, device));
            assert(mutableOriginal->write(elements[0].write).success);
            is(Set::validateOriginal(owned, device), "RHI-DESCRIPTOR-CONTENT-CHANGED");
            owned.reset();
            forgedSource.reset();
            // Table sources use exact sampled-view dependencies from the same inventory.
            table = freshTable();
            assert(!Table::prepareOriginal(
                table, arena, resources, nullptr, 0u, budget, quota, device, diagnostic));
            is(diagnostic, "RHI-ORIGINAL-DESCRIPTOR-SOURCE-INCOMPLETE");
            auto changedTable = tableElement;
            changedTable.write.index = 1u;
            assert(!Table::prepareOriginal(
                table, arena, resources, &changedTable, 1u, budget, quota, device, diagnostic));
            is(diagnostic, "RHI-DESCRIPTOR-CONTENT-NOT-WRITTEN");
            table.reset();
            pressure.verifySingleUse(
                tablePeak,
                [&] {
                    table.reset();
                    table = freshTable();
                },
                tableFactory);
            table.reset();
            table = freshTable();
            auto tableSource = tableFactory();
            assert(tableSource && !table);
            assert(
                !CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{quota}, [&]() noexcept {
                    return Table::validateOriginal(tableSource, device);
                }));
            std::weak_ptr<rhi::DescriptorArena> originalArenaObserver = arena;
            auto* actualArena = arena.get();
            otherArena.reset();
            forgedArena.reset();
            arena.reset();
            assert(!Table::validateOriginal(tableSource, device));
            actualArena->reset();
            is(Table::validateOriginal(tableSource, device),
                "RHI-ORIGINAL-REGISTRATION-ALLOCATION-STALE");
            // Reacquire the exact core control block while the stale child
            // still owns it, then prove retirement on a fresh current Source.
            arena = originalArenaObserver.lock();
            assert(arena && arena->sharedOwnerMatches(arena));
            tableSource.reset();
            table = freshTable();
            auto retirementSource = tableFactory();
            assert(retirementSource && !Table::validateOriginal(retirementSource, device));
            device.retireOriginalDeviceLifetime();
            is(CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{quota},
                   [&]() noexcept {
                       return Table::validateOriginal(retirementSource, device);
                   }),
                "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED");
            retirementSource.reset();
            std::printf(
                "BINDLESS_RHI_ORIGINAL_DESCRIPTOR_SOURCE_FREEZE_OK backend%u set%llu table%llu adopted-original complete-written-fields exact-resource-indices-generation-access parent-epoch caller-failure-retained quota-exact-less-exhausted hot-allocation0 positional-native-closed\n",
                static_cast<unsigned>(device.backend()),
                static_cast<unsigned long long>(peak),
                static_cast<unsigned long long>(tablePeak));
        }
        assert(quota->liveBytes() == baseline && !objects->liveBytes());
    }
};
} // namespace arp::test
