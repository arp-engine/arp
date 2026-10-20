#pragma once
#include "rhi/backends/metal/metal_graphics_preparation.h"
#include "rhi/backends/metal/metal_ordinary_graphics_write.h"
#include "shader/fragment_storage_read_proof.h"
#include "shader/fragment_uniform_read_proof.h"

namespace arp::rhi_metal {
// Immutable consumer evidence only. Native execution requires a dedicated
// opted-in command entrypoint, actual producer end and exact dependency.
class MetalRhiOrdinaryPreparedGraphicsReadConsumer final {
    class ConstructionKey {
        friend class MetalRhiOrdinaryPreparedGraphicsReadConsumer;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    MetalRhiOrdinaryPreparedGraphicsReadConsumer(ConstructionKey,
        std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsWrite>,
        std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding>,
        std::shared_ptr<const MetalRhiOrdinaryPreparedRenderPass>,
        shader::FragmentStorageReadProof::Facts,
        MetalRhiStageBufferRange);
    MetalRhiOrdinaryPreparedGraphicsReadConsumer(
        const MetalRhiOrdinaryPreparedGraphicsReadConsumer&) = delete;
    MetalRhiOrdinaryPreparedGraphicsReadConsumer& operator=(
        const MetalRhiOrdinaryPreparedGraphicsReadConsumer&) = delete;
    static std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsReadConsumer> prepare(
        std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsWrite>,
        std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding>,
        std::shared_ptr<const MetalRhiOrdinaryPreparedRenderPass>,
        core::pmr::String&);
    rhi::ValidationResult validateObservation() const;

    const auto& write() const noexcept { return write_; }

    const auto& binding() const noexcept { return binding_; }

    const auto& pass() const noexcept { return pass_; }

    const auto& proof() const noexcept { return proof_; }

    const auto& quota() const noexcept { return write_->quota(); }

    std::size_t childOwnerCount() const noexcept { return owners_.size(); }

    auto childOwnerAt(std::size_t i) const noexcept { return owners_.at(i); }

private:
    template <class Visitor> void projectOwners(Visitor&& visit) const
    {
        visit(write_);
        for (std::size_t i = 0; i < write_->childOwnerCount(); ++i)
            visit(write_->childOwnerAt(i));
        visit(binding_);
        for (std::size_t i = 0; i < binding_->childOwnerCount(); ++i)
            visit(binding_->childOwnerAt(i));
        visit(pass_);
        for (std::size_t i = 0; i < pass_->childOwnerCount(); ++i)
            visit(pass_->childOwnerAt(i));
    }

    std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsWrite> write_;
    std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding> binding_;
    std::shared_ptr<const MetalRhiOrdinaryPreparedRenderPass> pass_;
    shader::FragmentStorageReadProof::Facts proof_;
    MetalRhiStageBufferRange range_;
    core::SharedOwnerProjection owners_;
};
} // namespace arp::rhi_metal
