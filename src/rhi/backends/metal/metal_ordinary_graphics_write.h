#pragma once

#include "rhi/backends/metal/metal_graphics_preparation.h"
#include "rhi/frontend/command_draw_validation.h"
#include "shader/fragment_atomic_write_proof.h"

namespace arp::rhi_metal {

// MetalOrdinaryGraphicsStorageWritePreparation/1. Owns evidence and dependency
// intent only. Neither this root nor its facts authorize any GPU operation.
class MetalRhiOrdinaryPreparedGraphicsWrite final {
    friend class MetalRhiNativeCommandPayload;

    class ConstructionKey {
        friend class MetalRhiOrdinaryPreparedGraphicsWrite;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    MetalRhiOrdinaryPreparedGraphicsWrite(ConstructionKey,
        std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding>,
        std::shared_ptr<rhi::GpuBuffer>,
        core::UniquePtr<rhi::GraphicsStorageWriteContract>,
        shader::FragmentAtomicWriteProof::Facts,
        const rhi::BindlessAccessDeclarations&,
        rhi::ResourceBarrierDesc,
        rhi::BindlessResourceAccess,
        MetalRhiStageBufferRange);
    MetalRhiOrdinaryPreparedGraphicsWrite(const MetalRhiOrdinaryPreparedGraphicsWrite&) = delete;
    MetalRhiOrdinaryPreparedGraphicsWrite& operator=(
        const MetalRhiOrdinaryPreparedGraphicsWrite&) = delete;
    static std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsWrite> prepare(
        std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding>,
        std::shared_ptr<rhi::GpuBuffer>,
        const rhi::BindlessAccessDeclarations&,
        const rhi::ResourceBarrierDesc&,
        const rhi::BindlessResourceAccess&,
        core::pmr::String&);
    rhi::ValidationResult validateObservation() const;

    const auto& binding() const noexcept { return binding_; }

    const auto& resource() const noexcept { return *buffer_; }

    const auto& proof() const noexcept { return proof_; }

    const auto& contract() const noexcept { return *contract_; }

    const auto& producer() const noexcept { return producer_; }

    const auto& consumer() const noexcept { return consumer_; }

    const auto& dependency() const noexcept { return dependency_; }

    const auto& range() const noexcept { return range_; }

    const auto& quota() const noexcept { return binding_->quota(); }

    std::size_t childOwnerCount() const noexcept { return owners_.size(); }

    auto childOwnerAt(std::size_t i) const noexcept { return owners_.at(i); }

private:
    static const char* captureRange(const MetalRhiOrdinaryPreparedGraphicsBinding&,
        const rhi::GpuBuffer&,
        const shader::FragmentAtomicWriteProof::Facts&,
        MetalRhiStageBufferRange&);

    template <class Visitor> void projectOwners(Visitor&& visit) const
    {
        visit(binding_);
        for (std::size_t i = 0; i < binding_->childOwnerCount(); ++i)
            visit(binding_->childOwnerAt(i));
        visit(buffer_);
        visit(seal_);
    }

    std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding> binding_;
    std::shared_ptr<rhi::GpuBuffer> buffer_;
    std::shared_ptr<const rhi::GpuBufferWriteSealState> seal_;
    core::UniquePtr<rhi::GraphicsStorageWriteContract> contract_;
    shader::FragmentAtomicWriteProof::Facts proof_;
    rhi::BindlessRetainedDeclarations producer_;
    rhi::ResourceBarrierDesc dependency_;
    rhi::BindlessResourceAccess consumer_;
    MetalRhiStageBufferRange range_;
    std::uint64_t bufferBytes_;
    rhi::ResourceUsage usage_;
    bool transferSealed_;
    core::SharedOwnerProjection owners_;
};
} // namespace arp::rhi_metal
