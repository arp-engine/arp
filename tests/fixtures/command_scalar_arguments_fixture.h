#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/command_arguments.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <limits>
#include <type_traits>

namespace arp::test {
class CommandScalarArgumentsFixture final {
    using K = rhi::CommandUsageKind;
    using A = rhi::CommandScalarArguments;

    template <class T, std::size_t N>
    static void fields(K kind, const T& original, const std::array<void (*)(T&), N>& mutations)
    {
        auto caller = original;
        const auto frozen = A::capture(kind, caller);
        assert(frozen && frozen->matches(kind, original));
        caller = {};
        for (auto change : mutations) {
            auto altered = original;
            change(altered);
            assert(!frozen->matches(kind, altered));
            auto output = altered;
            assert(!frozen->write(K::Finish, output) && !frozen->matches(kind, output));
            assert(frozen->write(kind, output) && frozen->matches(kind, output));
        }
        A::None wrong;
        assert(!frozen->write(kind, wrong) && !A::capture(K::Finish, original));
        assert(!frozen->matches(K::Finish, original));
    }

public:
    static void verify()
    {
        using namespace rhi;
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        fields(K::Draw,
            A::Draw{3u, 5u, 7u, 11u},
            std::array<void (*)(A::Draw&), 4>{{[](auto& d) {
                                                   d.vertices ^= 1u;
                                               },
                [](auto& d) {
                    d.instances ^= 1u;
                },
                [](auto& d) {
                    d.firstVertex ^= 1u;
                },
                [](auto& d) {
                    d.firstInstance ^= 1u;
                }}});
        fields(K::DrawIndexed,
            A::DrawIndexed{3u, 5u, 7u, INT32_MIN, 11u},
            std::array<void (*)(A::DrawIndexed&), 5>{{[](auto& d) {
                                                          d.indices ^= 1u;
                                                      },
                [](auto& d) {
                    d.instances ^= 1u;
                },
                [](auto& d) {
                    d.firstIndex ^= 1u;
                },
                [](auto& d) {
                    d.vertexOffset = INT32_MAX;
                },
                [](auto& d) {
                    d.firstInstance ^= 1u;
                }}});
        fields(K::DrawIndexedIndirect,
            A::DrawIndirect{UINT64_MAX, 5u, 7u},
            std::array<void (*)(A::DrawIndirect&), 3>{{[](auto& d) {
                                                           d.offset ^= 1u;
                                                       },
                [](auto& d) {
                    d.count ^= 1u;
                },
                [](auto& d) {
                    d.stride ^= 1u;
                }}});
        fields(K::DrawIndexedIndirectCount,
            A::DrawIndirectCount{UINT64_MAX, UINT64_MAX - 3u, 5u, 7u},
            std::array<void (*)(A::DrawIndirectCount&), 4>{{[](auto& d) {
                                                                d.offset ^= 1u;
                                                            },
                [](auto& d) {
                    d.countOffset ^= 1u;
                },
                [](auto& d) {
                    d.maxCount ^= 1u;
                },
                [](auto& d) {
                    d.stride ^= 1u;
                }}});
        fields(K::Dispatch,
            A::Dispatch{3u, 5u, UINT32_MAX},
            std::array<void (*)(A::Dispatch&), 3>{{[](auto& d) {
                                                       d.x ^= 1u;
                                                   },
                [](auto& d) {
                    d.y ^= 1u;
                },
                [](auto& d) {
                    d.z ^= 1u;
                }}});
        fields(K::DispatchIndirect,
            A::DispatchIndirect{UINT64_MAX},
            std::array<void (*)(A::DispatchIndirect&), 1>{{[](auto& d) {
                d.offset ^= 1u;
            }}});
        fields(K::BindVertexBuffer,
            A::VertexBinding{3u, UINT64_MAX},
            std::array<void (*)(A::VertexBinding&), 2>{{[](auto& d) {
                                                            d.slot ^= 1u;
                                                        },
                [](auto& d) {
                    d.offset ^= 1u;
                }}});
        fields(K::BindIndexBuffer,
            A::IndexBinding{IndexFormat::Uint32, UINT64_MAX},
            std::array<void (*)(A::IndexBinding&), 2>{{[](auto& d) {
                                                           d.format =
                                                               static_cast<IndexFormat>(UINT32_MAX);
                                                       },
                [](auto& d) {
                    d.offset ^= 1u;
                }}});
        fields(K::BindTessellationFactorBuffer,
            A::TessellationBinding{UINT64_MAX, UINT32_MAX},
            std::array<void (*)(A::TessellationBinding&), 2>{{[](auto& d) {
                                                                  d.offset ^= 1u;
                                                              },
                [](auto& d) {
                    d.stride ^= 1u;
                }}});
        fields(K::BindRenderDescriptor,
            A::DescriptorBinding{UINT32_MAX},
            std::array<void (*)(A::DescriptorBinding&), 1>{{[](auto& d) {
                d.set ^= 1u;
            }}});
        fields(K::BindComputeDescriptor,
            A::DescriptorBinding{UINT32_MAX},
            std::array<void (*)(A::DescriptorBinding&), 1>{{[](auto& d) {
                d.set ^= 1u;
            }}});
        fields(K::BindRenderDescriptorTable,
            A::DescriptorBinding{UINT32_MAX},
            std::array<void (*)(A::DescriptorBinding&), 1>{{[](auto& d) {
                d.set ^= 1u;
            }}});
        fields(K::BindComputeDescriptorTable,
            A::DescriptorBinding{UINT32_MAX},
            std::array<void (*)(A::DescriptorBinding&), 1>{{[](auto& d) {
                d.set ^= 1u;
            }}});
        fields(K::Timestamp, A::Query{UINT32_MAX}, std::array<void (*)(A::Query&), 1>{{[](auto& d) {
            d.index ^= 1u;
        }}});
        fields(K::PipelineStatistics,
            A::Query{UINT32_MAX},
            std::array<void (*)(A::Query&), 1>{{[](auto& d) {
                d.index ^= 1u;
            }}});
        fields(K::PushConstants,
            A::Push{ShaderStage::Compute, UINT32_MAX, UINT32_MAX},
            std::array<void (*)(A::Push&), 3>{{[](auto& d) {
                                                   d.stages = static_cast<ShaderStage>(UINT32_MAX);
                                               },
                [](auto& d) {
                    d.offset ^= 1u;
                },
                [](auto& d) {
                    d.size ^= 1u;
                }}});
        fields(K::RecordIndirectProcessor,
            A::Processor{true},
            std::array<void (*)(A::Processor&), 1>{{[](auto& d) {
                d.consumerRequired = false;
            }}});
        fields(K::SetScissor,
            ScissorState{3u, 5u, 7u, 11u},
            std::array<void (*)(ScissorState&), 4>{{[](auto& d) {
                                                        d.x ^= 1u;
                                                    },
                [](auto& d) {
                    d.y ^= 1u;
                },
                [](auto& d) {
                    d.width ^= 1u;
                },
                [](auto& d) {
                    d.height ^= 1u;
                }}});
        fields(K::SetViewport,
            ViewportState{-0.0f, 5.0f, 7.0f, 11.0f, 0.125f, 0.875f},
            std::array<void (*)(ViewportState&), 6>{{[](auto& d) {
                                                         d.x += 1.0f;
                                                     },
                [](auto& d) {
                    d.y += 1.0f;
                },
                [](auto& d) {
                    d.width += 1.0f;
                },
                [](auto& d) {
                    d.height += 1.0f;
                },
                [](auto& d) {
                    d.minDepth += 1.0f;
                },
                [](auto& d) {
                    d.maxDepth += 1.0f;
                }}});
        auto viewport = ViewportState{};
        const std::uint32_t nanBits = 0x7fc00031u;
        std::memcpy(&viewport.width, &nanBits, sizeof(float));
        viewport.x = -0.0f;
        const auto frozen = A::capture(K::SetViewport, viewport);
        assert(frozen && frozen->matches(K::SetViewport, viewport));
        auto positiveZero = viewport;
        positiveZero.x = 0.0f;
        assert(!frozen->matches(K::SetViewport, positiveZero));
        const std::uint32_t otherNan = 0x7fc00032u;
        std::memcpy(&viewport.width, &otherNan, sizeof(float));
        assert(!frozen->matches(K::SetViewport, viewport));
        for (std::uint32_t i = 0u; i < static_cast<std::uint32_t>(K::Count); ++i) {
            const auto kind = static_cast<K>(i);
            const auto value = A::capture(kind, A::None{});
            assert(static_cast<bool>(value) == A::accepts(kind, A::None{}));
            if (value)
                assert(value->matches(kind, A::None{}));
        }
        assert(A::capture(K::TextureReadback, A::None{}));
        for (const auto kind : {K::LegacySizeOnlyBufferCopy, K::Count, static_cast<K>(UINT32_MAX)})
            assert(!A::capture(kind, A::None{}));
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::puts(
            "BINDLESS_RHI_SCALAR_ARGUMENTS_OK fields signed64 float-bits type kind caller-drop allocation0");
    }
};

class CommandVariableArgumentsFixture final {
public:
    static void verify()
    {
        using namespace rhi;
        using A = CommandScalarArguments;
        using Store = CommandVariableArguments;
        using K = CommandUsageKind;
        CommandScalarArgumentsFixture::verify();
        CommandVariableArgumentsBudget budget;
        assert(std::string_view(Store::validateBudget(budget)) == "RHI-COMMAND-ARGUMENTS-VERSION");
        budget.contractVersion = 1u;
        assert(std::string_view(Store::validateBudget(budget)) == "RHI-COMMAND-ARGUMENTS-OPT-IN");
        budget.prototypeOptIn = true;
        assert(std::string_view(Store::validateBudget(budget)) == "RHI-COMMAND-ARGUMENTS-BUDGET");
        auto quota = core::makeShared<core::CpuMemoryQuota>(16384u);
        budget = {1u, true, 3u, 2u, 3u, 4u, quota->limitBytes()};
        std::array<char, 3> text{{'a', '\0', 'b'}};
        std::array<std::uint32_t, 2> offsets{{UINT32_MAX, 19u}};
        std::array<unsigned char, 4> bytes{{0u, 255u, 3u, 7u}};
        std::array<Store::Input, 3> input{
            {{*A::capture(K::PushDebugGroup, A::None{}), {text.data(), text.size()}},
                {*A::capture(K::BindComputeDescriptor, A::DescriptorBinding{7u}),
                    {},
                    offsets.data(),
                    2u},
                {*A::capture(K::PushConstants, A::Push{ShaderStage::Compute, UINT32_MAX, 4u}),
                    {},
                    nullptr,
                    0u,
                    bytes.data(),
                    4u}}};
        const char* diagnostic = nullptr;
        const auto factory = [&] {
            return Store::prepareOriginal(input.data(), input.size(), budget, quota, diagnostic);
        };
        CpuQuotaFactoryFixture pressure(quota);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        const auto expect = [&](const char* id) {
            assert(!factory() && std::string_view(diagnostic) == id && !quota->liveBytes());
        };
        --budget.maxCommands;
        expect("RHI-COMMAND-ARGUMENTS-COUNT");
        ++budget.maxCommands;
        --budget.maxTextBytes;
        expect("RHI-COMMAND-ARGUMENTS-CAPACITY");
        ++budget.maxTextBytes;
        --budget.maxDynamicOffsets;
        expect("RHI-COMMAND-ARGUMENTS-CAPACITY");
        ++budget.maxDynamicOffsets;
        --budget.maxPushBytes;
        expect("RHI-COMMAND-ARGUMENTS-CAPACITY");
        ++budget.maxPushBytes;
        --budget.maxCpuMetadataBytes;
        expect("RHI-COMMAND-ARGUMENTS-QUOTA");
        ++budget.maxCpuMetadataBytes;
        const auto original = input;
        input[1].offsets = nullptr;
        expect("RHI-COMMAND-ARGUMENTS-POINTER");
        input = original;
        input[2].bytes = nullptr;
        expect("RHI-COMMAND-ARGUMENTS-POINTER");
        input = original;
        input[2].byteCount = 3u;
        expect("RHI-COMMAND-ARGUMENTS-PUSH-SIZE");
        input = original;
        input[1].text = "foreign";
        expect("RHI-COMMAND-ARGUMENTS-PAYLOAD");
        input = original;
        input[0].offsets = offsets.data();
        input[0].offsetCount = 1u;
        expect("RHI-COMMAND-ARGUMENTS-PAYLOAD");
        input = original;
        input[1].bytes = bytes.data();
        input[1].byteCount = 4u;
        expect("RHI-COMMAND-ARGUMENTS-PAYLOAD");
        input = original;
        input[1].scalar = *A::capture(K::BindComputeDescriptorTable, A::DescriptorBinding{7u});
        expect("RHI-COMMAND-ARGUMENTS-PAYLOAD");
        input = original;
        for (auto* ceiling : {&budget.maxCommands, &budget.maxDynamicOffsets}) {
            const auto saved = *ceiling;
            *ceiling = UINT32_MAX;
            expect("RHI-COMMAND-ARGUMENTS-ALLOCATION");
            *ceiling = saved;
        }
        for (auto* ceiling : {&budget.maxTextBytes, &budget.maxPushBytes}) {
            const auto saved = *ceiling;
            *ceiling = SIZE_MAX;
            expect("RHI-COMMAND-ARGUMENTS-ALLOCATION");
            *ceiling = saved;
#if SIZE_MAX < UINT64_MAX
            *ceiling = static_cast<std::uint64_t>(SIZE_MAX) + 1u;
            expect("RHI-COMMAND-ARGUMENTS-BUDGET");
            *ceiling = saved;
#endif
        }
        {
            core::CpuMemoryQuotaLease full(quota, quota->limitBytes());
            assert(full.charged() && !factory()
                && std::string_view(diagnostic) == "RHI-COMMAND-ARGUMENTS-ALLOCATION");
        }
        assert(!Store::prepareOriginal(nullptr, 3u, budget, quota, diagnostic)
            && std::string_view(diagnostic) == "RHI-COMMAND-ARGUMENTS-COUNT");
        assert(!Store::prepareOriginal(input.data(), 0u, budget, quota, diagnostic)
            && std::string_view(diagnostic) == "RHI-COMMAND-ARGUMENTS-COUNT");
        assert(!Store::prepareOriginal(input.data(), 3u, budget, {}, diagnostic)
            && std::string_view(diagnostic) == "RHI-COMMAND-ARGUMENTS-QUOTA");
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        auto frozen = factory();
        std::pmr::set_default_resource(previous);
        assert(frozen && frozen->hasQuotaBoundSharedOwner(frozen));
        std::shared_ptr<const Store> forged(quota, frozen.get());
        assert(!frozen->hasQuotaBoundSharedOwner(forged));
        forged.reset();
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!frozen->validateCapacity(budget));
        for (unsigned category = 0u; category < 4u; ++category) {
            auto smaller = budget;
            if (category == 0u)
                --smaller.maxCommands;
            if (category == 1u)
                --smaller.maxDynamicOffsets;
            if (category == 2u)
                --smaller.maxTextBytes;
            if (category == 3u)
                --smaller.maxPushBytes;
            assert(std::string_view(frozen->validateCapacity(smaller))
                == "RHI-COMMAND-ARGUMENTS-CAPACITY");
        }
        std::array<rhi::CommandUsage, 3> kinds{{{input[0].scalar.kind(), 0u, 0u},
            {input[1].scalar.kind(), 0u, 0u},
            {input[2].scalar.kind(), 0u, 0u}}};
        assert(!frozen->validateUsage(kinds.data(), kinds.size(), "count", "order"));
        assert(std::string_view(
                   frozen->validateUsage(kinds.data(), kinds.size() - 1u, "count", "order"))
            == "count");
        const auto expected = kinds[1].kind;
        kinds[1].kind = rhi::CommandUsageKind::Finish;
        assert(std::string_view(frozen->validateUsage(kinds.data(), kinds.size(), "count", "order"))
            == "order");
        kinds[1].kind = expected;
        assert(!frozen->matchesUsageKind(kinds.size(), expected));
        for (std::size_t i = 0u; i < input.size(); ++i)
            assert(frozen->matches(i, input[i]));
        assert(!frozen->matches(input.size(), input[0]));
        text[2] = 'c';
        offsets[1]++;
        bytes[3]++;
        for (std::size_t i = 0u; i < input.size(); ++i)
            assert(!frozen->matches(i, input[i]));
        input[0].text = frozen->text(0u);
        input[1].offsets = frozen->offsets(1u);
        input[2].bytes = frozen->bytes(2u);
        assert(frozen->text(0u) == std::string_view("a\0b", 3u) && frozen->offsets(1u)[1] == 19u
            && frozen->bytes(2u)[3] == 7u);
        for (std::size_t i = 0u; i < input.size(); ++i)
            assert(frozen->matches(i, input[i]));
        assert(frozen->text(3u).empty() && !frozen->offsets(3u) && !frozen->bytes(3u));
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::weak_ptr<const Store> weak = frozen;
        frozen.reset();
        assert(weak.expired());
        weak.reset();
        assert(!quota->liveBytes());
        std::printf(
            "BINDLESS_RHI_VARIABLE_ARGUMENTS_OK %llu quotas payload exact-copy owner hot-allocation0\n",
            static_cast<unsigned long long>(peak));
    }
};
} // namespace arp::test
