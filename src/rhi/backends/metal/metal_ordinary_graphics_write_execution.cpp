#include "rhi/backends/metal/metal_ordinary_graphics_write_execution.h"

#include "core/unsigned_element_span.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_device.h"

namespace arp::rhi_metal {

template <class Visitor>
void MetalRhiOrdinaryPreparedWriteReadback::projectOwners(Visitor&& visit) const
{
    visit(write_);
    for (std::size_t i = 0; i < write_->childOwnerCount(); ++i)
        visit(write_->childOwnerAt(i));
    visit(buffer_);
    visit(buffer_->gpuWriteSealToken().lock());
    const auto native = metalRhiNativePayloadFor(*buffer_);
    for (const auto& owner : MetalRhiResourceOwnerProjection::owners(
             rhi::DescriptorResourceKind::Buffer, native, {}, {}, {}))
        if (owner)
            visit(owner);
}

MetalRhiOrdinaryPreparedWriteReadback::MetalRhiOrdinaryPreparedWriteReadback(ConstructionKey,
    std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsWrite> write,
    std::shared_ptr<rhi::GpuBuffer> buffer,
    MetalRhiNativeResourceObservation observation)
    : write_(std::move(write)), buffer_(std::move(buffer)), observation_(std::move(observation))
{
    owners_.capture([&](auto&& visit) {
        projectOwners(visit);
    });
}

const char* MetalRhiOrdinaryPreparedWriteReadback::validateOutput(bool writable) const
{
    const auto actual = write_->binding()->registry()->find(buffer_->id());
    const auto native = metalRhiNativePayloadFor(*buffer_);
    if (!actual || !native || actual->kind != rhi::DescriptorResourceKind::Buffer
        || actual->buffer != native || actual->buffer.owner_before(native)
        || native.owner_before(actual->buffer)
        || buffer_->desc().sizeBytes != rhi::GraphicsStorageWriteContract::footprintBytes
        || actual->bufferSizeBytes != buffer_->desc().sizeBytes
        || buffer_->desc().usage != rhi::ResourceUsage::CopyDst
        || actual->bufferUsage != buffer_->desc().usage
        || buffer_->desc().memory != rhi::MemoryUsage::GpuToCpu || !buffer_->hostWritesSealed()
        || buffer_->state() != rhi::ResourceState::CopyDst
        || (writable && (buffer_->gpuTransferWritesSealed() || actual->gpuTransferWritesSealed)))
        return "RHI-METAL-ORDINARY-WRITE-READBACK-RESOURCE-MISMATCH";
    if (native->resource.nativeDevice != write_->binding()->provider()->native()->nativeDevice
        || !native->resource.allocationReleaseToken || !observation_.matches(native->resource)
        || (native->resource.storageMode != MetalRhiNativeStorageMode::Managed
            && native->resource.storageMode != MetalRhiNativeStorageMode::Shared))
        return "RHI-METAL-ORDINARY-WRITE-READBACK-NATIVE-MISMATCH";
    if (const auto* id =
            validateMetalRhiTrackedNativeBufferObject(native->resource, buffer_->desc().sizeBytes))
        return id;
    return owners_.matches([&](auto&& visit) {
        projectOwners(visit);
    })
        ? nullptr
        : "RHI-METAL-ORDINARY-WRITE-READBACK-OWNER-MISMATCH";
}

rhi::ValidationResult MetalRhiOrdinaryPreparedWriteReadback::validateObservation() const
{
    const auto write = write_->validateObservation();
    if (!write.success)
        return write;
    if (const auto* id = validateOutput(true))
        return {false, id};
    return {true, {}};
}

rhi::ValidationResult MetalRhiOrdinaryPreparedWriteReadback::read(std::uint32_t& output) const
{
    output = 0u;
    switch (status()) {
    case Status::Pending:
        return {false, "RHI-METAL-ORDINARY-WRITE-READBACK-PENDING"};
    case Status::Failed:
        return {false, "RHI-METAL-ORDINARY-WRITE-READBACK-FAILED"};
    case Status::Abandoned:
        return {false, "RHI-METAL-ORDINARY-WRITE-READBACK-ABANDONED"};
    case Status::Completed:
        break;
    }
    // Producer descriptor metadata may legitimately retire after encoding.
    // Successful native completion is private and original output identity is
    // still required; a newly substituted resource cannot manufacture data.
    if (const auto* id = validateOutput(false))
        return {false, id};
    std::array<std::uint8_t, sizeof(std::uint32_t)> bytes{};
    core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
    if (!readMetalRhiNativeBuffer(metalRhiNativePayloadFor(*buffer_),
            bytes.data(),
            bytes.size(),
            0u,
            bytes.size(),
            diagnostic))
        return {false, std::move(diagnostic)};
    core::UnsignedElementSpan words(bytes.data(),
        bytes.size(),
        core::UnsignedElementSpan::Width::Uint32,
        core::UnsignedElementSpan::ByteOrder::LittleEndian);
    if (!words.read(0u, output))
        return {false, "RHI-METAL-ORDINARY-WRITE-READBACK-DECODE"};
    return {true, {}};
}

std::shared_ptr<MetalRhiOrdinaryPreparedWriteReadback>
MetalRhiOrdinaryPreparedWriteReadback::prepare(
    std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsWrite> write,
    rhi::GpuDevice& device,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    const auto fail =
        [&](const char* id) -> std::shared_ptr<MetalRhiOrdinaryPreparedWriteReadback> {
        diagnostic = id;
        return {};
    };
    if (!write || write->binding()->registry() != metalRhiIssuingResourceRegistry(device))
        return fail("RHI-METAL-ORDINARY-WRITE-READBACK-ISSUER-MISMATCH");
    const auto observed = write->validateObservation();
    if (!observed.success) {
        diagnostic = observed.diagnostics;
        return {};
    }
    const auto& consumer = write->consumer();
    if (consumer.state != rhi::ResourceState::CopySrc
        || consumer.pipelineStages != rhi::PipelineStage::Transfer
        || consumer.access != rhi::ResourceAccess::TransferRead
        || consumer.stages != rhi::ShaderStage::None)
        return fail("RHI-METAL-ORDINARY-WRITE-READBACK-CONSUMER-DOMAIN");
    try {
        core::CpuMemoryQuotaScope scope(write->quota());
        auto buffer = device.createBuffer({"OrdinaryWriteReadback",
            rhi::GraphicsStorageWriteContract::footprintBytes,
            rhi::ResourceUsage::CopyDst,
            rhi::MemoryUsage::GpuToCpu,
            rhi::ResourceState::CopyDst});
        if (!buffer)
            return fail("RHI-METAL-ORDINARY-WRITE-READBACK-CREATION-FAILED");
        buffer->sealHostWrites();
        const auto native = metalRhiNativePayloadFor(*buffer);
        if (!native)
            return fail("RHI-METAL-ORDINARY-WRITE-READBACK-NATIVE-MISMATCH");
        auto result = core::makeShared<MetalRhiOrdinaryPreparedWriteReadback>(ConstructionKey{},
            write,
            core::shareUnique(std::move(buffer)),
            MetalRhiNativeResourceObservation(native->resource));
        const auto valid = result->validateObservation();
        if (!valid.success) {
            diagnostic = valid.diagnostics;
            return {};
        }
        return result;
    } catch (const std::bad_alloc&) {
        core::CpuMemoryQuotaScope diagnosticScope(nullptr);
        return fail("RHI-METAL-ORDINARY-WRITE-READBACK-ALLOCATION-FAILED");
    }
}

core::UniquePtr<MetalRhiOrdinaryWriteCommand> MetalRhiOrdinaryWriteCommand::prepare(
    const MetalRhiOrdinaryWritePrototypeIntent& intent,
    const core::pmr::String& label,
    std::shared_ptr<MetalRhiResourceRegistry> registry,
    const rhi::CommandRetentionBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (const auto* id = intent.validate()) {
        diagnostic = id;
        return {};
    }
    const char* id = nullptr;
    auto retention = rhi::common::CommandRetentionStorage::prepare(budget, quota, id);
    if (!retention) {
        diagnostic = id;
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto states = core::makeShared<core::MutationJournal>();
        auto payload = createMetalRhiOrdinaryWriteNativeCommandPayload(
            intent, label, std::move(registry), std::move(retention), states, diagnostic);
        if (!payload)
            return {};
        return core::makeUnique<MetalRhiOrdinaryWriteCommand>(
            ConstructionKey{}, std::move(payload), std::move(states));
    } catch (const std::bad_alloc&) {
        core::CpuMemoryQuotaScope diagnosticScope(nullptr);
        diagnostic = "RHI-METAL-ORDINARY-WRITE-COMMAND-ALLOCATION-FAILED";
        return {};
    }
}
} // namespace arp::rhi_metal
