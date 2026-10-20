#pragma once
#include "rhi/frontend/transfer_upload_copy.h"
#include "rhi/frontend/transfer_upload_copy_mapping.h"

#include <array>
#include <cassert>
#include <type_traits>

namespace arp::test {
class TextureUploadPackedCopyFixture final {
    using Mapping = rhi::semantics::TextureUploadPackedCopyMapping;
    using Copy = rhi::semantics::TextureUploadPackedCopy;
    using Lowering = rhi::semantics::TextureUploadCopyLowering;

public:
    static void verify(const std::shared_ptr<const rhi::TextureUploadPacking>& packing)
    {
        static_assert(std::is_trivially_copyable_v<Copy>);
        using namespace rhi;
        const auto source = allocateGpuResourceId();
        Copy copy, sentinel;
        sentinel.contentGeneration = UINT64_MAX;
        const auto reject = [&](std::uint32_t version,
                                bool optIn,
                                const std::shared_ptr<const TextureUploadPacking>& original,
                                GpuResourceId id,
                                std::size_t index,
                                const char* expected) {
            auto output = sentinel;
            assert(
                std::string_view(Mapping::mapOriginal(version, optIn, original, id, index, output))
                    == expected
                && output.matches(sentinel));
        };
        reject(0u, true, packing, source, 0u, "RHI-TEXTURE-PACKED-COPY-VERSION");
        reject(1u, false, packing, source, 0u, "RHI-TEXTURE-PACKED-COPY-OPT-IN");
        reject(1u, true, {}, source, 0u, "RHI-TEXTURE-PACKED-COPY-ORIGINAL-PACKING");
        auto forged =
            std::shared_ptr<const TextureUploadPacking>(packing->inputOwner(), packing.get());
        reject(1u, true, forged, source, 0u, "RHI-TEXTURE-PACKED-COPY-ORIGINAL-PACKING");
        forged.reset();
        reject(1u, true, packing, {}, 0u, "RHI-TEXTURE-PACKED-COPY-RESOURCE-IDS");
        UploadTextureDesc fields;
        packing->inputOwner()->scalarFields().writeScalarFields(fields);
        reject(1u,
            true,
            packing,
            fields.destinationTexture,
            0u,
            "RHI-TEXTURE-PACKED-COPY-RESOURCE-IDS");
        reject(1u,
            true,
            packing,
            source,
            packing->mipLayouts().size(),
            "RHI-TEXTURE-PACKED-COPY-MIP-INDEX");
        reject(1u, true, packing, source, SIZE_MAX, "RHI-TEXTURE-PACKED-COPY-MIP-INDEX");
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto result = Mapping::lowering(backend);
            assert(result.metadataEquivalent && !result.nativeExecution
                && std::string_view(result.diagnostic)
                    == "RHI-TEXTURE-PACKED-COPY-EXECUTION-UNPROVEN");
        }
        assert(!Mapping::lowering(static_cast<Backend>(99)).metadataEquivalent);
        const std::array<void (*)(Copy&), 20> changes{{[](auto& c) {
                                                           ++c.sourceBuffer.value;
                                                       },
            [](auto& c) {
                ++c.destinationTexture.value;
            },
            [](auto& c) {
                ++c.subresources.baseMipLevel;
            },
            [](auto& c) {
                ++c.subresources.mipLevelCount;
            },
            [](auto& c) {
                ++c.subresources.baseArrayLayer;
            },
            [](auto& c) {
                ++c.subresources.arrayLayerCount;
            },
            [](auto& c) {
                c.subresources.aspect = TextureAspect::Depth;
            },
            [](auto& c) {
                ++c.extent.width;
            },
            [](auto& c) {
                ++c.extent.height;
            },
            [](auto& c) {
                ++c.extent.depth;
            },
            [](auto& c) {
                ++c.origin.width;
            },
            [](auto& c) {
                ++c.origin.height;
            },
            [](auto& c) {
                ++c.origin.depth;
            },
            [](auto& c) {
                c.format = TextureFormat::Unknown;
            },
            [](auto& c) {
                ++c.bufferOffsetBytes;
            },
            [](auto& c) {
                ++c.packedFootprintBytes;
            },
            [](auto& c) {
                ++c.frameSerial;
            },
            [](auto& c) {
                ++c.contentGeneration;
            },
            [](auto& c) {
                ++c.bytesPerRow;
            },
            [](auto& c) {
                ++c.rowsPerImage;
            }}};
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (std::size_t index = 0u; index < packing->mipLayouts().size(); ++index) {
            assert(!Mapping::mapOriginal(1u, true, packing, source, index, copy));
            const auto& mip = packing->mipLayouts()[index];
            assert(copy.sourceBuffer == source
                && copy.destinationTexture == fields.destinationTexture
                && copy.format == fields.format && copy.subresources == mip.range
                && copy.origin == mip.origin && copy.extent == mip.extent
                && copy.bytesPerRow == mip.packedBytesPerRow && copy.rowsPerImage == mip.rowCount
                && copy.bufferOffsetBytes == mip.packedOffset
                && copy.packedFootprintBytes == mip.packedSize
                && copy.frameSerial == fields.frameSerial
                && copy.contentGeneration == fields.resourceGeneration);
            assert(!Mapping::validateOriginal(1u, true, packing, source, index, copy));
            Lowering::Decision decision, sentinelDecision;
            sentinelDecision.contractVersion = UINT32_MAX;
            assert(!Lowering::lowerOriginal(
                Backend::WebGPU, 1u, true, packing, source, index, copy, decision));
            assert(decision.packing.get() == packing.get()
                && core::SharedOwnerProjection::sameOwner(decision.packing, packing)
                && decision.original.matches(copy) && decision.mipIndex == index
                && decision.outcome == Lowering::Outcome::Prototype
                && decision.operation == Lowering::Operation::BufferToTexture);
            const bool volume = packing->destinationLayout().extent.depth > 1u;
            assert(decision.dimension
                == (volume ? Lowering::Dimension::Texture3D : Lowering::Dimension::Texture2D));
            assert(decision.physicalOrigin.width == mip.origin.width
                && decision.physicalOrigin.height == mip.origin.height
                && decision.physicalOrigin.depth
                    == (volume ? mip.origin.depth : mip.range.baseArrayLayer)
                && decision.physicalExtent.depth == mip.sliceCount);
            // Independent expected rounding: block edge fixture includes 6x6
            // logical BC extent at X=4, producing 8x8 physical copy geometry.
            const auto block = blockExtent(copy.format);
            const auto width = copy.extent.width / block + (copy.extent.width % block != 0u);
            const auto height = copy.extent.height / block + (copy.extent.height % block != 0u);
            assert(decision.physicalExtent.width == width * block
                && decision.physicalExtent.height == height * block);
            assert(!Lowering::validateOriginal(
                Backend::WebGPU, 1u, true, packing, source, index, copy, decision));
            for (const auto backend :
                {Backend::Null, Backend::Vulkan, Backend::Metal, static_cast<Backend>(99)}) {
                auto output = sentinelDecision;
                assert(
                    Lowering::lowerOriginal(backend, 1u, true, packing, source, index, copy, output)
                    && output.matches(sentinelDecision));
            }
            for (const auto version : {0u, 2u}) {
                auto output = sentinelDecision;
                assert(std::string_view(Lowering::lowerOriginal(
                           Backend::WebGPU, version, true, packing, source, index, copy, output))
                        == "RHI-TEXTURE-COPY-LOWERING-VERSION"
                    && output.matches(sentinelDecision));
            }
            {
                auto output = sentinelDecision;
                assert(std::string_view(Lowering::lowerOriginal(
                           Backend::WebGPU, 1u, false, packing, source, index, copy, output))
                        == "RHI-TEXTURE-COPY-LOWERING-OPT-IN"
                    && output.matches(sentinelDecision));
            }
            const std::array<void (*)(Lowering::Decision&), 13> decisionChanges{
                {[](auto& d) {
                     ++d.contractVersion;
                 },
                    [](auto& d) {
                        d.backend = Backend::Null;
                    },
                    [](auto& d) {
                        d.outcome = Lowering::Outcome::DiagnosticFailure;
                    },
                    [](auto& d) {
                        d.operation = Lowering::Operation::DiagnosticFailure;
                    },
                    [](auto& d) {
                        d.dimension = Lowering::Dimension::Unknown;
                    },
                    [](auto& d) {
                        ++d.mipIndex;
                    },
                    [](auto& d) {
                        ++d.physicalOrigin.width;
                    },
                    [](auto& d) {
                        ++d.physicalOrigin.height;
                    },
                    [](auto& d) {
                        ++d.physicalOrigin.depth;
                    },
                    [](auto& d) {
                        ++d.physicalExtent.width;
                    },
                    [](auto& d) {
                        ++d.physicalExtent.height;
                    },
                    [](auto& d) {
                        ++d.physicalExtent.depth;
                    },
                    [](auto& d) {
                        d.packing.reset();
                    }}};
            for (const auto change : decisionChanges) {
                auto altered = decision;
                change(altered);
                assert(std::string_view(Lowering::validateOriginal(
                           Backend::WebGPU, 1u, true, packing, source, index, copy, altered))
                    == "RHI-TEXTURE-COPY-LOWERING-DECISION-MISMATCH");
            }
            auto forgedDecision = decision;
            forgedDecision.packing =
                std::shared_ptr<const TextureUploadPacking>(packing->inputOwner(), packing.get());
            assert(std::string_view(Lowering::validateOriginal(
                       Backend::WebGPU, 1u, true, packing, source, index, copy, forgedDecision))
                == "RHI-TEXTURE-COPY-LOWERING-DECISION-MISMATCH");
            for (const auto change : changes) {
                auto candidate = copy;
                change(candidate);
                assert(std::string_view(
                           Mapping::validateOriginal(1u, true, packing, source, index, candidate))
                    == "RHI-TEXTURE-PACKED-COPY-MAPPING-MISMATCH");
                auto output = sentinelDecision;
                assert(Lowering::lowerOriginal(
                           Backend::WebGPU, 1u, true, packing, source, index, candidate, output)
                    && output.matches(sentinelDecision));
                auto altered = decision;
                change(altered.original);
                assert(std::string_view(Lowering::validateOriginal(
                           Backend::WebGPU, 1u, true, packing, source, index, copy, altered))
                    == "RHI-TEXTURE-COPY-LOWERING-DECISION-MISMATCH");
            }
        }
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(previous);
    }
};
} // namespace arp::test
