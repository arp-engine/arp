#pragma once
#include "rhi/backends/webgpu/webgpu_original_texture_readback_copies.h"
#include "texture_readback_layout_fixture.h"

namespace arp::test {
class WebGpuOriginalTextureReadbackCopiesFixture final {
public:
    using Copies = rhi_webgpu::WebGpuOriginalTextureReadbackCopies;
    using Transaction = rhi_webgpu::WebGpuOriginalStateTransaction;
    using Lowering = rhi::semantics::TextureReadbackCopyLowering;
    using Budget = rhi_webgpu::WebGpuOriginalTextureReadbackCopiesBudget;
    static constexpr std::uint64_t cpuBytes = 65536u;

    static Budget budget() { return {1u, true, 1u, 2u, 2u, 4432u, 1696u, 1536u, cpuBytes}; }

    static void verifyLayout(const std::shared_ptr<const rhi::TextureReadbackLayout>& layout)
    {
        using namespace rhi;
        const auto previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (std::size_t i = 0u; i < layout->mipCopies().size(); ++i) {
            const auto& mip = layout->mipCopies()[i];
            Lowering::Decision decision;
            assert(!Lowering::lowerOriginal(Backend::WebGPU, 1u, true, layout, i, mip, decision)
                && decision.original.matches(mip)
                && decision.dimension
                    == (layout->sourceLayout().extent.depth > 1u ? Lowering::Dimension::Texture3D
                                                                 : Lowering::Dimension::Texture2D));
            assert(
                !Lowering::validateOriginal(Backend::WebGPU, 1u, true, layout, i, mip, decision));
            auto sentinel = decision;
            sentinel.mipIndex = SIZE_MAX;
            auto output = sentinel;
            for (const auto backend :
                {Backend::Null, Backend::Vulkan, Backend::Metal, static_cast<Backend>(99)}) {
                assert(Lowering::lowerOriginal(backend, 1u, true, layout, i, mip, output)
                    && output.matches(sentinel));
            }
            assert(Lowering::lowerOriginal(Backend::WebGPU, 0u, true, layout, i, mip, output)
                && output.matches(sentinel));
            assert(Lowering::lowerOriginal(Backend::WebGPU, 1u, false, layout, i, mip, output)
                && output.matches(sentinel));
            for (const auto mutate : TextureReadbackLayoutFixture::mipMutations()) {
                auto changed = mip;
                mutate(changed);
                assert(
                    Lowering::lowerOriginal(Backend::WebGPU, 1u, true, layout, i, changed, output)
                    && output.matches(sentinel));
                auto forged = decision;
                forged.original = changed;
                assert(
                    Lowering::validateOriginal(Backend::WebGPU, 1u, true, layout, i, mip, forged));
            }
            auto changed = decision;
            changed.contractVersion = 0u;
            assert(Lowering::validateOriginal(Backend::WebGPU, 1u, true, layout, i, mip, changed));
            changed = decision;
            changed.layout =
                std::shared_ptr<const TextureReadbackLayout>(layout->inputOwner(), layout.get());
            assert(Lowering::validateOriginal(Backend::WebGPU, 1u, true, layout, i, mip, changed));
            assert(
                Lowering::lowerOriginal(Backend::WebGPU, 1u, true, changed.layout, i, mip, output)
                && output.matches(sentinel));
            changed = decision;
            changed.backend = Backend::Null;
            assert(Lowering::validateOriginal(Backend::WebGPU, 1u, true, layout, i, mip, changed));
            changed = decision;
            changed.operation = Lowering::Operation::DiagnosticFailure;
            assert(Lowering::validateOriginal(Backend::WebGPU, 1u, true, layout, i, mip, changed));
            changed = decision;
            changed.outcome = Lowering::Outcome::DiagnosticFailure;
            assert(Lowering::validateOriginal(Backend::WebGPU, 1u, true, layout, i, mip, changed));
            changed = decision;
            changed.dimension = Lowering::Dimension::Unknown;
            assert(Lowering::validateOriginal(Backend::WebGPU, 1u, true, layout, i, mip, changed));
            changed = decision;
            changed.mipIndex = SIZE_MAX;
            assert(Lowering::validateOriginal(Backend::WebGPU, 1u, true, layout, i, mip, changed));
        }
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(previous);
    }

    static void verifyContract()
    {
        TextureReadbackLayoutFixture::Context c;
        const char* diagnostic = nullptr;
        auto layout = c.prepare(TextureReadbackLayoutFixture::descriptor(),
            TextureReadbackLayoutFixture::texture(),
            diagnostic);
        assert(layout);
        verifyLayout(layout);
        layout.reset();
        c.retired();
        TextureReadbackLayoutFixture::verifyGeometry(verifyLayout);
        auto b = budget();
        auto quota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        Budget empty;
        assert(std::string_view(Copies::validateBudget(empty))
            == "RHI-WEBGPU-READBACK-COPIES-VERSION");
        empty.contractVersion = 1u;
        assert(
            std::string_view(Copies::validateBudget(empty)) == "RHI-WEBGPU-READBACK-COPIES-OPT-IN");
        empty.prototypeOptIn = true;
        assert(
            std::string_view(Copies::validateBudget(empty)) == "RHI-WEBGPU-READBACK-COPIES-BUDGET");
        assert(!Copies::prepareOriginal({}, b, quota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-STATE-TRANSACTION-OWNER");
        rhi::TextureReadbackLayout::Mip output, sentinel;
        sentinel.bufferOffsetBytes = UINT64_MAX;
        output = sentinel;
        assert(
            Transaction::mapReadbackMipCurrent({}, {}, 0u, {}, output) && output.matches(sentinel));
        std::puts(
            "BINDLESS_RHI_READBACK_COPIES_CONTRACT_OK backends4 original-physical-mips arrays-volume-bc-cube fields21 no-proof");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static auto verifyCold(const std::shared_ptr<Transaction>& transaction,
        std::shared_ptr<core::CpuMemoryQuota> quota)
    {
        auto b = budget();
        const char* diagnostic = nullptr;
        std::shared_ptr<const Transaction> original = transaction;
        const auto factory = [&] {
            return Copies::prepareOriginal(original, b, quota, diagnostic);
        };
        CpuQuotaFactoryFixture pressure(quota);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        const auto reject = [&](const char* id) {
            assert(!factory() && std::string_view(diagnostic) == id && !quota->liveBytes()
                && !transaction->validate());
        };
        --b.maxOriginalTransactions;
        reject("RHI-WEBGPU-READBACK-COPIES-BUDGET");
        b = budget();
        --b.maxMipMappings;
        reject("RHI-WEBGPU-READBACK-COPIES-CAPACITY");
        b = budget();
        --b.maxCopyDecisions;
        reject("RHI-WEBGPU-READBACK-COPIES-CAPACITY");
        b = budget();
        --b.maxRetainedNativeBytes;
        reject("RHI-WEBGPU-READBACK-COPIES-CAPACITY");
        b = budget();
        --b.maxRetainedContentBytes;
        reject("RHI-WEBGPU-READBACK-COPIES-CAPACITY");
        b = budget();
        --b.maxUnpublishedResultBytes;
        reject("RHI-WEBGPU-READBACK-COPIES-CAPACITY");
        b = budget();
        --b.maxCpuMetadataBytes;
        reject("RHI-WEBGPU-READBACK-COPIES-QUOTA");
        b = budget();
        b.maxCopyDecisions = UINT32_MAX;
        reject("RHI-WEBGPU-READBACK-COPIES-ALLOCATION");
        b = budget();
        {
            core::CpuMemoryQuotaLease full(quota, cpuBytes);
            assert(full.charged() && !factory()
                && std::string_view(diagnostic) == "RHI-WEBGPU-READBACK-COPIES-ALLOCATION");
        }
        original = std::shared_ptr<const Transaction>(quota, transaction.get());
        reject("RHI-WEBGPU-STATE-TRANSACTION-OWNER");
        original = transaction;
        auto copies = factory();
        assert(copies && !copies->validate());
        assert(
            std::string_view(Copies::validateCurrent(copies, 0u, copies->decisions()[0], nullptr))
            == "RHI-WEBGPU-READBACK-TRANSACTION-CURRENT-STATUS");
        std::printf(
            "BINDLESS_RHI_READBACK_COPIES_COLD_OK %llu decisions2 independent-budgets original-geometry prepared-gate\n",
            static_cast<unsigned long long>(peak));
        return copies;
    }

    static void verifyCurrent(const std::shared_ptr<const Copies>& copies)
    {
        const auto& transaction = copies->transaction();
        const auto& pair = transaction->states()
                               ->inputs()
                               ->scalars()
                               ->usage()
                               ->projection()
                               ->originalReadbacks()[0];
        auto device = pair->destination()->nativeResource()->ownerDevice;
        auto forged = std::shared_ptr<const Copies>(transaction, copies.get());
        const auto previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(std::string_view(Copies::validateCurrent(forged, 0u, copies->decisions()[0], device))
            == "RHI-WEBGPU-READBACK-COPIES-OWNER");
        forged.reset();
        assert(std::string_view(Copies::validateCurrent(copies, 2u, copies->decisions()[0], device))
            == "RHI-WEBGPU-READBACK-COPIES-INDEX");
        assert(
            std::string_view(Copies::validateCurrent(copies, 0u, copies->decisions()[0], nullptr))
            == "RHI-WEBGPU-READBACK-COPIES-DEVICE");
        for (std::size_t i = 0u; i < copies->decisions().size(); ++i) {
            const auto& decision = copies->decisions()[i];
            assert(!Copies::validateCurrent(copies, i, decision, device));
            for (const auto mutate : TextureReadbackLayoutFixture::mipMutations()) {
                auto changed = decision;
                mutate(changed.original);
                assert(Copies::validateCurrent(copies, i, changed, device));
            }
            rhi::TextureReadbackLayout::Mip mapped, sentinel;
            sentinel.bufferOffsetBytes = UINT64_MAX;
            mapped = sentinel;
            auto observed = decision.original;
            ++observed.bytesPerRow;
            assert(Transaction::mapReadbackMipCurrent(
                       transaction, pair->layout()->inputOwner(), i, observed, mapped)
                && mapped.matches(sentinel));
            auto forgedInput = std::shared_ptr<const rhi::TextureReadbackInputSnapshot>(
                transaction, pair->layout()->inputOwner().get());
            assert(std::string_view(Transaction::mapReadbackMipCurrent(
                       transaction, forgedInput, i, decision.original, mapped))
                    == "RHI-READBACK-PAIR-INPUT-OWNER"
                && mapped.matches(sentinel));
            assert(
                Transaction::mapReadbackMipCurrent(
                    transaction, pair->layout()->inputOwner(), SIZE_MAX, decision.original, mapped)
                && mapped.matches(sentinel));
            assert(!Transaction::mapReadbackMipCurrent(
                       transaction, pair->layout()->inputOwner(), i, decision.original, mapped)
                && mapped.matches(decision.original));
        }
        auto& native = *pair->destination()->nativeResource();
        const auto bytes = native.sizeBytes;
        ++native.sizeBytes;
        assert(copies->validate());
        native.sizeBytes = bytes;
        assert(!copies->validate()
            && std::string_view(copies->executionDiagnostic())
                == "RHI-WEBGPU-READBACK-COPIES-RECORDING-UNPROVEN");
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(previous);
        std::puts(
            "BINDLESS_RHI_READBACK_COPIES_NATIVE_OK exact-current original-mips2 decisions21 native-device allocation0 no-emit-proof");
    }
#endif
};
} // namespace arp::test
