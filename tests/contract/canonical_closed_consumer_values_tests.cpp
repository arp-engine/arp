#include "../fixtures/canonical_closed_consumer_recording_fixture.h"
#include "../fixtures/cpu_quota_factory_fixture.h"
#include "rhi/frontend/bindless_image_and_indirect_validation.h"

#include <iostream>
#include <string_view>

namespace arp::test {
namespace {
void validateClosedStoragePhases(CanonicalClosedConsumerRecordingFixture& fixture)
{
    using namespace rhi;
    using C = CanonicalClosedConsumerRecordingContract;
    using P = C::Producer;
    using Phase = C::StoragePhase;
    auto& inputs = fixture.inputs;
    const auto budget = fixture.budget();
    C::Facts original, current;
    CanonicalClosedConsumerRecordingAdmission admission;
    P::Facts producerFacts;
    CanonicalZeroBaseIndirectProducerRecordingAdmission producerAdmission;
    assert(!C::verify(inputs, budget, 1u, original)
        && !CanonicalClosedConsumerRecordingAdmission::capture(inputs, budget, 1u, admission));
    assert(!CanonicalZeroBaseIndirectProducerRecordingAdmission::capture(
        inputs.producer, budget.producerBudget(), 1u, producerAdmission));
    const auto denied = [&](Phase phase) {
        current = original;
        assert(C::verify(inputs, budget, 1u, current, phase) && !current.identity.valid()
            && !current.actualReadbackBytes && current.metadata == C::Consumer::Words{}
            && current.bytes == C::Consumer::Bytes{});
        assert(admission.validate(inputs, 1u, phase));
    };
    denied(Phase::ReadbackReady);
    denied(static_cast<Phase>(UINT32_MAX));
    fixture.original.transitionOutputs(ResourceState::ShaderWrite, ResourceState::CopySrc);
    // c11 Ready intentionally still requires the consumer's original cold state.
    assert(!P::verify(inputs.producer,
        budget.producerBudget(),
        1u,
        producerFacts,
        P::StoragePhase::ReadbackReady));
    assert(!producerAdmission.validate(inputs.producer, 1u, P::StoragePhase::ReadbackReady));
    denied(Phase::Cold);
    denied(Phase::ReadbackReady);
    for (std::size_t role = 0u; role < 3u; ++role) {
        fixture.transitionConsumerBuffer(role, true);
        if (role < 2u)
            denied(Phase::ReadbackReady);
    }
    const auto ready = [&] {
        assert(!C::verify(inputs, budget, 1u, current, Phase::ReadbackReady)
            && current.identity == original.identity && current.metadata == original.metadata
            && current.bytes == original.bytes
            && current.actualReadbackBytes == original.actualReadbackBytes);
        assert(!admission.validate(inputs, 1u, Phase::ReadbackReady));
        assert(!P::verifyClosedReadbackReady(
            inputs.producer, budget.producerBudget(), 1u, producerFacts));
        assert(!producerAdmission.validateClosedReadbackReady(inputs.producer, 1u));
        assert(P::verify(inputs.producer,
            budget.producerBudget(),
            1u,
            producerFacts,
            P::StoragePhase::ReadbackReady));
        assert(producerAdmission.validate(inputs.producer, 1u, P::StoragePhase::ReadbackReady));
    };
    ready();
    denied(Phase::Cold);
    denied(static_cast<Phase>(UINT32_MAX));
    CanonicalClosedConsumerRecordingAdmission failed;
    assert(CanonicalClosedConsumerRecordingAdmission::capture(inputs, budget, 1u, failed)
        && !failed.facts().identity.valid());
    for (std::size_t role = 0u; role < 3u; ++role) {
        fixture.transitionConsumerBuffer(role, false);
        denied(Phase::ReadbackReady);
        fixture.transitionConsumerBuffer(role, true);
        ready();
    }
    fixture.original.transitionOutputs(ResourceState::CopySrc, ResourceState::ShaderWrite);
    denied(Phase::ReadbackReady);
    fixture.original.transitionOutputs(ResourceState::ShaderWrite, ResourceState::CopySrc);
    ready();
    auto forged = inputs;
    forged.geometry = {inputs.metadata, inputs.geometry.get()};
    assert(admission.validate(forged, 1u, Phase::ReadbackReady));
    forged = inputs;
    ++forged.revisions[0u].revision;
    assert(!C::verify(forged, budget, 1u, current, Phase::ReadbackReady)
        && admission.validate(forged, 1u, Phase::ReadbackReady));
    auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    {
        core::CpuMemoryQuotaScope scope(quota);
        core::CpuMemoryQuotaLease pressure(quota, 1u);
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        for (unsigned i = 0u; i < 100u; ++i) {
            ready();
            denied(Phase::Cold);
            denied(static_cast<Phase>(UINT32_MAX));
            assert(admission.validate(forged, 1u, Phase::ReadbackReady));
        }
        std::pmr::set_default_resource(previous);
    }
    assert(
        allocations == core::defaultCpuAllocator().stats().allocationCount && !quota->liveBytes());
    for (std::size_t role = 0u; role < 3u; ++role)
        fixture.transitionConsumerBuffer(role, false);
    fixture.original.transitionOutputs(ResourceState::CopySrc, ResourceState::ShaderWrite);
    assert(!admission.validate(inputs, 1u));
}

void validateClosedTargetIntent()
{
    using namespace rhi;
    using C = CanonicalClosedConsumerTargetContract;
    C::Input input{{16u, 8u, 1u},
        {0.0f, 0.0f, 16.0f, 8.0f, 0.0f, 1.0f},
        {0, 0, 16u, 8u},
        {0.25f, 0.5f, 0.75f, 1.0f}};
    C::Budget budget{1u, true, 512u};
    C::Facts facts;
    const auto reject = [&] {
        facts.targetBytes = UINT64_MAX;
        assert(C::verify(input, budget, facts) && !facts.targetBytes);
    };
    assert(!C::verify(input, budget, facts) && facts.targetBytes == 512u);
    auto texture = C::textureDescriptor(input);
    auto attachment = C::attachmentDescriptor(input, {1u});
    assert(texture.format == TextureFormat::Rgba8Unorm
        && texture.usage == (TextureUsage::RenderAttachment | TextureUsage::CopySrc)
        && texture.memory == MemoryUsage::GpuOnly
        && texture.initialState == ResourceState::ColorAttachment && texture.mipLevelCount == 1u
        && texture.arrayLayerCount == 1u && texture.sampleCount == 1u && !texture.cubeCompatible
        && attachment.loadOp == LoadOp::Clear && attachment.storeOp == StoreOp::Store
        && !attachment.hasResolveTarget && !attachment.resolveTarget.isValid()
        && attachment.clearColor.g == input.clear.g);
    --budget.maxTargetBytes;
    reject();
    ++budget.maxTargetBytes;
    budget.prototypeOptIn = false;
    reject();
    budget.prototypeOptIn = true;
    --budget.contractVersion;
    reject();
    ++budget.contractVersion;
    input.extent.depth = 2u;
    reject();
    input.extent.depth = 1u;
    input.viewport.width = 0.0f;
    reject();
    input.viewport.width = 16.0f;
    input.scissor.width = 0u;
    reject();
    input.scissor.width = 16u;
    input.scissor.x = -1;
    reject();
    input.scissor.x = 0;
    input.scissor.x = 1;
    reject();
    input.scissor.x = 0;
    input.viewport.x = 1.0f;
    reject();
    input.viewport.x = 0.0f;
    input.viewport.minDepth = 2.0f;
    reject();
    input.viewport.minDepth = 0.0f;
    input.clear.r = 2.0f;
    reject();
    input.clear.r = 0.25f;
    input.clear.r = std::numeric_limits<float>::quiet_NaN();
    reject();
    input.clear.r = 0.25f;
    const auto saved = input;
    input.extent = {UINT32_MAX, UINT32_MAX, 1u};
    budget.maxTargetBytes = UINT64_MAX;
    input.viewport = {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    input.scissor = {0, 0, 1u, 1u};
    reject();
    input = saved;
    budget.maxTargetBytes = 512u;
    for (const auto backend : {Backend::Null,
             Backend::Metal,
             Backend::Vulkan,
             Backend::WebGPU,
             static_cast<Backend>(UINT32_MAX)}) {
        const auto decision = C::lowering(backend);
        assert(!decision.executable && decision.diagnosticId
            && decision.validatorOnly == (backend == Backend::Null)
            && decision.prototypeColdIntent == (backend == Backend::Metal));
    }
    auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    {
        core::CpuMemoryQuotaScope scope(quota);
        core::CpuMemoryQuotaLease pressure(quota, 1u);
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        for (unsigned i = 0u; i < 100u; ++i) {
            assert(!C::verify(input, budget, facts));
            input.viewport.width = 0.0f;
            reject();
            input.viewport.width = 16.0f;
        }
        std::pmr::set_default_resource(previous);
    }
    assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
}

void validateFreshClosedPacketPurposes()
{
    using Contract = rhi::BindlessIndirectPacketStorageContract;
    using Purpose = Contract::Issuance;
    const auto unknown = static_cast<Purpose>(UINT32_MAX);
    assert(Contract::knownIssuance(Purpose::PublicConsumer)
        && Contract::knownIssuance(Purpose::PrototypeStorageOnly)
        && Contract::knownIssuance(Purpose::PrototypeClosedConsumerCold)
        && !Contract::knownIssuance(unknown));
    assert(!Contract::publicConsumerDiagnostic(Purpose::PublicConsumer));
    assert(Contract::publicConsumerDiagnostic(Purpose::PrototypeStorageOnly)
        == std::string_view("RHI-BINDLESS-INDIRECT-PACKET-PROTOTYPE-STORAGE-ONLY"));
    assert(Contract::publicConsumerDiagnostic(Purpose::PrototypeClosedConsumerCold)
        == std::string_view("RHI-BINDLESS-INDIRECT-PACKET-PROTOTYPE-CLOSED-CONSUMER-COLD"));
    assert(Contract::publicConsumerDiagnostic(unknown)
        == std::string_view("RHI-BINDLESS-INDIRECT-PACKET-ISSUANCE-UNKNOWN"));
    assert(Contract::closedConsumerStructuralDiagnostic(Purpose::PrototypeStorageOnly));
    assert(!Contract::closedConsumerStructuralDiagnostic(Purpose::PublicConsumer)
        && !Contract::closedConsumerStructuralDiagnostic(Purpose::PrototypeClosedConsumerCold)
        && Contract::closedConsumerStructuralDiagnostic(unknown));
    for (const auto backend : {rhi::Backend::Null,
             rhi::Backend::Vulkan,
             rhi::Backend::Metal,
             rhi::Backend::WebGPU,
             static_cast<rhi::Backend>(UINT32_MAX)}) {
        for (const auto version : {0u, 1u, 2u})
            for (const bool optIn : {false, true}) {
                const auto decision = Contract::closedConsumerLowering(backend, version, optIn);
                assert(!decision.executable && decision.diagnosticId);
                const bool admitted = version == 1u && optIn;
                assert(decision.validatorOnly == (admitted && backend == rhi::Backend::Null));
                assert(
                    decision.prototypeColdIntent == (admitted && backend == rhi::Backend::Metal));
            }
    }
}

void validateExclusiveImageAuthoring(const CanonicalClosedConsumerRecordingFixture& fixture)
{
    using Contract = rhi::CanonicalExclusiveConsumerImagesContract;
    Contract::Inputs inputs;
    std::uint64_t total = 0u;
    for (std::size_t i = 0u; i < inputs.size(); ++i) {
        const auto& content = *fixture.inputs.contents[i];
        const auto& image = content.image();
        inputs[i] = {image.format,
            image.parentExtent,
            content.extent(),
            image.parentMipLevelCount,
            image.range.baseMipLevel,
            content.bytesPerRow(),
            content.rowsPerImage(),
            content.bytes().data(),
            content.bytes().size()};
        total += content.bytes().size();
        const auto parent = Contract::parentDescriptor(inputs[i]);
        const auto view = Contract::viewDescriptor(inputs[i]);
        assert(parent.usage
                == (rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst
                    | rhi::TextureUsage::CopySrc)
            && parent.memory == rhi::MemoryUsage::GpuOnly
            && parent.initialState == rhi::ResourceState::ShaderRead
            && parent.mipLevelCount == image.parentMipLevelCount
            && view.baseMipLevel == image.range.baseMipLevel
            && view.usage == rhi::TextureUsage::Sampled && view.mipLevelCount == 1u
            && view.arrayLayerCount == 1u);
    }
    Contract::Budget budget{1u, true, total, total, Contract::commandUnits};
    Contract::Facts facts;
    assert(!Contract::verify(inputs, budget, facts) && facts.totalBytes == total
        && Contract::commandUnits == 11u);
    const auto reject = [&] {
        facts.totalBytes = UINT64_MAX;
        assert(Contract::verify(inputs, budget, facts) && !facts.totalBytes
            && facts.bytes == decltype(facts.bytes){});
    };
    --budget.contractVersion;
    reject();
    ++budget.contractVersion;
    budget.prototypeOptIn = false;
    reject();
    budget.prototypeOptIn = true;
    --budget.maxCommands;
    reject();
    ++budget.maxCommands;
    --budget.maxSourceBytes;
    reject();
    ++budget.maxSourceBytes;
    --budget.maxSnapshotBytes;
    reject();
    ++budget.maxSnapshotBytes;
    for (auto& input : inputs) {
        const auto saved = input;
        input.format = rhi::TextureFormat::Unknown;
        reject();
        input = saved;
        input.parentExtent.depth = 2u;
        reject();
        input = saved;
        input.mipCount = 32u;
        reject();
        input = saved;
        input.selectedMip = 1u;
        reject();
        input = saved;
        input.extent.width = 3u;
        reject();
        input = saved;
        --input.bytesPerRow;
        reject();
        input = saved;
        input.bytes = nullptr;
        reject();
        input = saved;
        --input.byteCount;
        reject();
        input = saved;
    }
    for (const auto backend : {rhi::Backend::Null,
             rhi::Backend::Vulkan,
             rhi::Backend::Metal,
             rhi::Backend::WebGPU,
             static_cast<rhi::Backend>(99u)}) {
        const auto decision = Contract::lowering(backend);
        assert(!decision.executable && decision.prototypeCold == (backend == rhi::Backend::Metal)
            && decision.validatorOnly == (backend == rhi::Backend::Null) && decision.diagnosticId);
    }
    auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    {
        core::CpuMemoryQuotaScope scope(quota);
        core::CpuMemoryQuotaLease pressure(quota, 1u);
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        for (unsigned i = 0u; i < 100u; ++i)
            assert(!Contract::verify(inputs, budget, facts));
        std::pmr::set_default_resource(previous);
    }
    assert(
        allocations == core::defaultCpuAllocator().stats().allocationCount && !quota->liveBytes());
}

void validateImageContent(CanonicalClosedConsumerRecordingFixture& fixture)
{
    using namespace rhi;
    using Content = CanonicalConsumerImageContentContract;
    const auto& image = fixture.inputs.producer.images[0u];
    std::array<std::uint8_t, CanonicalClosedConsumerRecordingFixture::imageBytes> authored;
    for (std::size_t i = 0u; i < authored.size(); ++i)
        authored[i] = static_cast<std::uint8_t>(i);
    auto expected = authored;
    Content::Input input{
        image, {4u, 2u, 1u}, kTextureUploadRowPitchAlignment, 2u, authored.data(), authored.size()};
    Content::Budget budget{1u, true, authored.size(), authored.size()};
    auto content = core::makeShared<Content>(input, budget);
    assert(!content->diagnostic());
    Content::Revision revision{1u, image.view, image.parent, 1u};
    UploadTextureDesc upload;
    assert(!content->describeUpload(revision, 1u, upload));
    assert(upload.destinationTexture == image.parent && upload.format == image.format
        && upload.extent.width == 4u && upload.extent.height == 2u && upload.extent.depth == 1u
        && !upload.destinationOffset.width && !upload.destinationOffset.height
        && !upload.destinationOffset.depth && upload.bytesPerRow == kTextureUploadRowPitchAlignment
        && upload.rowsPerImage == 2u && upload.subresources == image.range
        && upload.mipLevel == image.range.baseMipLevel
        && upload.arrayLayer == image.range.baseArrayLayer
        && upload.pixelData == content->bytes().data() && upload.pixelDataSize == authored.size()
        && upload.stagingIntent == StagingIntent::Upload && upload.backendOwnedStaging
        && upload.frameSerial == 1u && upload.resourceGeneration == revision.revision
        && upload.mipLayouts.empty());
    authored.fill(0xffu);
    assert(std::equal(content->bytes().begin(), content->bytes().end(), expected.begin()));
    authored = expected;
    const auto reject = [&](const char* id) {
        Content failed(input, budget);
        assert(failed.diagnostic() == std::string_view(id) && failed.bytes().empty()
            && !failed.image().view.isValid());
    };
    const auto mutate = [&](auto& field, auto value, const char* id) {
        const auto saved = field;
        field = value;
        reject(id);
        field = saved;
    };
    mutate(budget.contractVersion, 0u, "RHI-CANONICAL-CONSUMER-IMAGE-CONTENT-BUDGET");
    mutate(budget.prototypeOptIn, false, "RHI-CANONICAL-CONSUMER-IMAGE-CONTENT-BUDGET");
    mutate(budget.maxSnapshotBytes,
        std::uint64_t{authored.size() - 1u},
        "RHI-CANONICAL-CONSUMER-IMAGE-CONTENT-BYTE-BUDGET");
    mutate(budget.maxSourceBytes,
        std::uint64_t{authored.size() - 1u},
        "RHI-CANONICAL-CONSUMER-IMAGE-CONTENT-BYTE-BUDGET");
    mutate(input.bytes,
        static_cast<const void*>(nullptr),
        "RHI-CANONICAL-CONSUMER-IMAGE-CONTENT-BYTES");
    mutate(input.byteCount, authored.size() - 1u, "RHI-CANONICAL-CONSUMER-IMAGE-CONTENT-BYTES");
    mutate(input.byteCount, authored.size() + 1u, "RHI-CANONICAL-CONSUMER-IMAGE-CONTENT-BYTES");
    mutate(input.bytesPerRow,
        kTextureUploadRowPitchAlignment - 1u,
        "RHI-CANONICAL-CONSUMER-IMAGE-CONTENT-LAYOUT");
    mutate(input.rowsPerImage, 1u, "RHI-CANONICAL-CONSUMER-IMAGE-CONTENT-LAYOUT");
    mutate(input.extent.width, 3u, "RHI-CANONICAL-CONSUMER-IMAGE-CONTENT-LAYOUT");
    mutate(input.extent.depth, 2u, "RHI-CANONICAL-CONSUMER-IMAGE-CONTENT-LAYOUT");
    mutate(input.image.format, TextureFormat::Unknown, "RHI-BINDLESS-TEXTURE-INVENTORY-DOMAIN");
    mutate(
        input.image.parentFormat, TextureFormat::R32Float, "RHI-BINDLESS-TEXTURE-INVENTORY-DOMAIN");
    mutate(input.image.parentUsage,
        TextureUsage::Sampled,
        "RHI-CANONICAL-CONSUMER-IMAGE-CONTENT-COPY-USAGE");
    mutate(input.image.range.mipLevelCount, 2u, "RHI-BINDLESS-TEXTURE-INVENTORY-RANGE");
    mutate(input.image.range.baseMipLevel, 1u, "RHI-BINDLESS-TEXTURE-INVENTORY-RANGE");
    mutate(input.image.range.baseArrayLayer, 1u, "RHI-BINDLESS-TEXTURE-INVENTORY-RANGE");
    mutate(input.image.parentMipLevelCount, 4u, "RHI-TEXTURE-SUBRESOURCE-MIP-RANGE");
    mutate(input.image.state, ResourceState::CopyDst, "RHI-BINDLESS-TEXTURE-INVENTORY-STATE");
    mutate(input.image.view, input.image.parent, "RHI-BINDLESS-TEXTURE-INVENTORY-IDENTITY");
    {
        const auto saved = input;
        input.image.parentExtent = {65u, 2u, 1u};
        input.extent = input.image.parentExtent;
        reject("RHI-CANONICAL-CONSUMER-IMAGE-CONTENT-LAYOUT");
        input = saved;
    }
    {
        const auto saved = input;
        input.image.parentExtent = {1u, UINT32_MAX, 1u};
        input.extent = input.image.parentExtent;
        input.rowsPerImage = UINT32_MAX;
        input.byteCount = std::uint64_t{kTextureUploadRowPitchAlignment} * UINT32_MAX;
        reject("RHI-CANONICAL-CONSUMER-IMAGE-CONTENT-BYTE-BUDGET");
        input = saved;
    }
    {
        const auto saved = input;
        const auto savedBudget = budget;
        input.image.parentExtent = {1u, UINT32_MAX, 1u};
        input.extent = input.image.parentExtent;
        input.rowsPerImage = UINT32_MAX;
        input.bytesPerRow = UINT32_MAX - (kTextureUploadRowPitchAlignment - 1u);
        input.byteCount = std::uint64_t{input.bytesPerRow} * input.rowsPerImage;
        budget.maxSourceBytes = budget.maxSnapshotBytes = UINT64_MAX;
        reject("RHI-CANONICAL-CONSUMER-IMAGE-CONTENT-BYTES");
        input = saved;
        budget = savedBudget;
    }
    {
        const auto saved = input;
        std::array<std::uint8_t, kTextureUploadRowPitchAlignment> oneMip{};
        input.image.parentExtent = {7u, 5u, 1u};
        input.image.parentMipLevelCount = 3u;
        input.image.range.baseMipLevel = 2u;
        input.extent = {1u, 1u, 1u};
        input.rowsPerImage = 1u;
        input.bytes = oneMip.data();
        input.byteCount = oneMip.size();
        Content selected(input, budget);
        assert(!selected.diagnostic());
        Content::Revision selectedRevision{1u, input.image.view, input.image.parent, 2u};
        assert(!selected.describeUpload(selectedRevision, 2u, upload) && upload.mipLevel == 2u
            && upload.extent.width == 1u && upload.extent.height == 1u
            && upload.pixelDataSize == oneMip.size());
        input = saved;
    }
    auto wrong = revision;
    ++wrong.view.value;
    assert(content->describeUpload(wrong, 1u, upload));
    assert(!upload.destinationTexture.isValid() && !upload.pixelData && !upload.pixelDataSize);
    wrong = revision;
    wrong.revision = 0u;
    assert(content->validate(image, wrong));
    assert(content->describeUpload(revision, 0u, upload));
    auto changedImage = image;
    changedImage.parentExtent.width = 5u;
    assert(content->validate(changedImage, revision));
    Content same(input, budget);
    assert(content->sameContent(same));
    authored.back() ^= 1u;
    Content paddingChanged(input, budget);
    assert(!content->sameContent(paddingChanged));
    authored = expected;
    Extent3D extent;
    assert(!semantics::TextureSubresourceExtent::resolve({7u, 5u, 3u}, 3u, 1u, extent)
        && extent.width == 3u && extent.height == 2u && extent.depth == 1u);
    assert(!semantics::TextureSubresourceExtent::resolve({UINT32_MAX, 1u, 1u}, 32u, 31u, extent)
        && extent.width == 1u);
    for (const auto pair : {std::pair{33u, 31u}, std::pair{32u, 32u}, std::pair{32u, UINT32_MAX}}) {
        assert(semantics::TextureSubresourceExtent::resolve(
                   {UINT32_MAX, 1u, 1u}, pair.first, pair.second, extent)
            && !extent.width && !extent.height && !extent.depth);
    }
    assert(semantics::TextureSubresourceExtent::resolve({0u, 1u, 1u}, 1u, 0u, extent));
    auto exhausted = core::makeShared<core::CpuMemoryQuota>(1u);
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    {
        core::CpuMemoryQuotaScope scope(exhausted);
        core::CpuMemoryQuotaLease pressure(exhausted, 1u);
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        for (unsigned i = 0u; i < 100u; ++i) {
            assert(!content->describeUpload(revision, 1u, upload));
            assert(!content->validate(image, revision));
            assert(content->sameContent(same));
        }
        std::pmr::set_default_resource(previous);
    }
    assert(allocations == core::defaultCpuAllocator().stats().allocationCount
        && !exhausted->liveBytes());
    auto quota = core::makeShared<core::CpuMemoryQuota>(64u * 1024u);
    const auto factory = [&] {
        core::CpuMemoryQuotaScope scope(quota);
        try {
            return core::makeShared<Content>(input, budget);
        } catch (const std::bad_alloc&) {
            return std::shared_ptr<Content>{};
        }
    };
    CpuQuotaFactoryFixture probe(quota);
    const auto peak = probe.measure(factory);
    probe.verify(peak, factory);
    assert(!quota->liveBytes());
}
} // namespace

void validateCanonicalClosedConsumerRecording(
    std::shared_ptr<const rhi::BindlessShaderPackage> consumer,
    std::shared_ptr<const rhi::BindlessShaderPackage> producer)
{
    using namespace rhi;
    using Fixture = CanonicalClosedConsumerRecordingFixture;
    using Contract = Fixture::Contract;
    using Admission = CanonicalClosedConsumerRecordingAdmission;
    using Sequence = CanonicalClosedConsumerSequence;
    using Status = Sequence::Status;
    using Outcome = CanonicalClosedConsumerOutcome;
    for (const auto order : {ZeroBaseIndirectSourceFixture::Order::LittleEndian,
             ZeroBaseIndirectSourceFixture::Order::BigEndian})
        for (const bool suppressed : {false, true}) {
            Fixture fixture(producer, consumer, order, suppressed);
            auto& inputs = fixture.inputs;
            auto budget = Fixture::budget();
            validateImageContent(fixture);
            validateExclusiveImageAuthoring(fixture);
            validateClosedTargetIntent();
            validateFreshClosedPacketPurposes();
            fixture.recordLogicalImageUploads();
            Contract::Facts facts;
            Admission admission;
            assert(!Contract::verify(inputs, budget, 1u, facts) && facts.identity.valid()
                && facts.actualReadbackBytes == Contract::readbackBytes);
            assert(!Admission::capture(inputs, budget, 1u, admission)
                && !admission.validate(inputs, 1u));
            std::size_t references = 0u;
            admission.projectReferences([&](const std::shared_ptr<const void>& ref) {
                assert(ref);
                ++references;
            });
            assert(references == Contract::referenceSlots);
            const auto original = facts;
            const auto reject = [&](const char* expected) {
                facts = original;
                const auto* id = Contract::verify(inputs, budget, 1u, facts);
                if (!id || std::string_view(id) != expected)
                    std::cerr << "Closed consumer expected " << expected << ", got "
                              << (id ? id : "success") << '\n';
                assert(id && std::string_view(id) == expected && !facts.identity.valid()
                    && !facts.actualReadbackBytes && facts.metadata == Contract::Consumer::Words{}
                    && facts.bytes == Contract::Consumer::Bytes{});
            };
            const auto mutate = [&](auto& field, auto value, const char* expected) {
                const auto saved = field;
                field = value;
                reject(expected);
                field = saved;
                assert(!admission.validate(inputs, 1u));
            };
            mutate(budget.contractVersion, 0u, "RHI-CLOSED-CONSUMER-RECORDING-BUDGET");
            mutate(budget.prototypeOptIn, false, "RHI-CLOSED-CONSUMER-RECORDING-BUDGET");
            mutate(budget.maxCommands,
                Contract::commandUnits - 1u,
                "RHI-CLOSED-CONSUMER-RECORDING-BUDGET");
            mutate(budget.maxReadbackBytes,
                Contract::readbackBytes - 1u,
                "RHI-CLOSED-CONSUMER-RECORDING-BUDGET");
            mutate(budget.maxReferenceSlots,
                Contract::referenceSlots - 1u,
                "RHI-CLOSED-CONSUMER-RECORDING-BUDGET");
            mutate(inputs.producer.packet,
                std::shared_ptr<const BindlessIndirectPacket>{},
                "RHI-CLOSED-CONSUMER-RECORDING-ROOT");
            mutate(inputs.producer.source,
                std::shared_ptr<const ZeroBaseIndirectSourceConsumptionContract>(
                    inputs.metadata, inputs.producer.source.get()),
                "RHI-CANONICAL-PRODUCER-RECORDING-ROOT");
            mutate(inputs.geometry,
                std::shared_ptr<const Contract::Geometry>(inputs.metadata, inputs.geometry.get()),
                "RHI-CLOSED-CONSUMER-RECORDING-VALUE-OWNER");
            mutate(inputs.vertex,
                std::shared_ptr<const VertexInputConsumptionContract>(
                    inputs.metadata, inputs.vertex.get()),
                "RHI-CLOSED-CONSUMER-RECORDING-VALUE-OWNER");
            mutate(
                inputs.geometryBuffer, inputs.metadata, "RHI-CANONICAL-CONSUMER-GEOMETRY-STORAGE");
            mutate(inputs.geometryBuffer,
                std::shared_ptr<const GpuBuffer>(
                    inputs.producer.source, inputs.geometryBuffer.get()),
                "RHI-CLOSED-CONSUMER-RECORDING-BUFFER-OWNER");
            mutate(inputs.metadata,
                std::shared_ptr<const GpuBuffer>(inputs.producer.source, inputs.metadata.get()),
                "RHI-CLOSED-CONSUMER-RECORDING-BUFFER-OWNER");
            mutate(inputs.contents[0u],
                std::shared_ptr<const Contract::Content>(
                    inputs.geometry, inputs.contents[0u].get()),
                "RHI-CLOSED-CONSUMER-IMAGE-CONTENT-OWNER");
            mutate(inputs.revisions[0u].revision,
                std::uint64_t{0u},
                "RHI-CANONICAL-CONSUMER-IMAGE-CONTENT-REVISION");
            {
                auto& desc = const_cast<BufferDesc&>(inputs.metadata->desc());
                mutate(
                    desc.sizeBytes, desc.sizeBytes - 1u, "RHI-CLOSED-CONSUMER-RECORDING-METADATA");
            }
            {
                auto& allocation = const_cast<ResourceAllocation&>(
                    inputs.producer.consumerFault->faultReadbackBuffer()->allocation());
                mutate(allocation.estimatedSizeBytes,
                    std::uint64_t{5u},
                    "RHI-CLOSED-CONSUMER-RECORDING-READBACK-BUDGET");
            }
            {
                const auto saved = inputs.revisions[0u];
                ++inputs.revisions[0u].revision;
                assert(!Contract::verify(inputs, budget, 1u, facts));
                assert(admission.validate(inputs, 1u));
                inputs.revisions[0u] = saved;
            }
            // A second immutable value with identical bytes is structurally eligible,
            // but cannot replace the admission's original control block.
            {
                const auto saved = inputs.contents[0u];
                inputs.contents[0u] =
                    core::makeShared<Contract::Content>(Contract::Content::Input{saved->image(),
                                                            saved->extent(),
                                                            saved->bytesPerRow(),
                                                            saved->rowsPerImage(),
                                                            saved->bytes().data(),
                                                            saved->bytes().size()},
                        Contract::Content::Budget{
                            1u, true, saved->bytes().size(), saved->bytes().size()});
                assert(!Contract::verify(inputs, budget, 1u, facts));
                assert(admission.validate(inputs, 1u));
                inputs.contents[0u] = saved;
            }
            for (const bool distinctView : {false, true}) {
                const auto saved = inputs;
                inputs.producer.images[1u] = inputs.producer.images[0u];
                if (distinctView)
                    inputs.producer.images[1u].view = saved.producer.images[1u].view;
                const auto& first = *inputs.contents[0u];
                inputs.contents[1u] = core::makeShared<Contract::Content>(
                    Contract::Content::Input{inputs.producer.images[1u],
                        first.extent(),
                        first.bytesPerRow(),
                        first.rowsPerImage(),
                        first.bytes().data(),
                        first.bytes().size()},
                    Contract::Content::Budget{
                        1u, true, first.bytes().size(), first.bytes().size()});
                inputs.revisions[1u] = {1u,
                    inputs.producer.images[1u].view,
                    inputs.producer.images[1u].parent,
                    inputs.revisions[0u].revision};
                assert(!Contract::verify(inputs, budget, 1u, facts));
                ++inputs.revisions[1u].revision;
                reject("RHI-CLOSED-CONSUMER-IMAGE-ALIASED-REVISION");
                --inputs.revisions[1u].revision;
                auto bytes = first.bytes();
                bytes.back() ^= 1u;
                inputs.contents[1u] = core::makeShared<Contract::Content>(
                    Contract::Content::Input{inputs.producer.images[1u],
                        first.extent(),
                        first.bytesPerRow(),
                        first.rowsPerImage(),
                        bytes.data(),
                        bytes.size()},
                    Contract::Content::Budget{1u, true, bytes.size(), bytes.size()});
                reject("RHI-CLOSED-CONSUMER-IMAGE-ALIASED-REVISION");
                inputs = saved;
            }
            assert(!admission.validate(inputs, 1u));
            validateClosedStoragePhases(fixture);
            for (const auto owner : {0u, 1u}) {
                auto forged = inputs;
                if (owner == 0u)
                    forged.geometry = {inputs.metadata, inputs.geometry.get()};
                else
                    forged.vertex = {inputs.metadata, inputs.vertex.get()};
                assert(admission.validate(forged, 1u));
            }
            {
                core::pmr::Vector<VertexBufferLayoutDesc> layout;
                VertexBufferLayoutDesc stream;
                stream.strideBytes = Contract::Geometry::strideBytes;
                stream.attributes = {{0u, 0u, VertexFormat::Float32x2}};
                layout.push_back(std::move(stream));
                auto copy = core::makeShared<VertexInputConsumptionContract>(layout);
                assert(copy->sharedOwnerMatches(copy) && !inputs.vertex->sharedOwnerMatches(copy));
                auto moved = core::makeShared<VertexInputConsumptionContract>(std::move(*copy));
                assert(moved->sharedOwnerMatches(moved) && !moved->sharedOwnerMatches(copy)
                    && !copy->sharedOwnerMatches(moved));
                assert(!inputs.geometry->validate(
                    *inputs.producer.consumerPackage, *inputs.producer.source, *moved));
            }
            assert(admission.validate(inputs, 2u));
            const auto run = [&] {
                Sequence sequence(budget, original.identity);
                for (std::size_t i = 0u; i < Contract::protocol.size(); ++i) {
                    assert(sequence.finish()
                        && sequence.transition(Status::Recorded, original.identity));
                    for (const auto hostile : Contract::protocol)
                        if (hostile != Contract::protocol[i]) {
                            assert(sequence.record(hostile, original.identity));
                            assert(sequence.commands() == i && sequence.status() == Status::Cold);
                        }
                    auto wrong = original.identity;
                    ++wrong.revisions[0u].revision;
                    assert(
                        sequence.record(Contract::protocol[i], wrong) && sequence.commands() == i);
                    wrong = original.identity;
                    ++wrong.producer.recording;
                    assert(
                        sequence.record(Contract::protocol[i], wrong) && sequence.commands() == i);
                    for (const auto terminal : {Status::Failed, Status::Abandoned}) {
                        auto cancelled = sequence;
                        assert(!cancelled.transition(terminal, original.identity));
                        assert(cancelled.record(Contract::protocol[i], original.identity)
                            && cancelled.transition(Status::Submitted, original.identity)
                            && cancelled.transition(Status::Completed, original.identity, true)
                            && cancelled.status() == terminal && cancelled.commands() == i);
                    }
                    assert(!sequence.record(Contract::protocol[i], original.identity)
                        && sequence.commands() == i + 1u);
                }
                assert(!sequence.finish());
                assert(!sequence.transition(Status::Recorded, original.identity));
                assert(!sequence.transition(Status::Submitted, original.identity));
                assert(sequence.transition(Status::Completed, original.identity)
                    && sequence.status() == Status::Submitted);
                assert(!sequence.transition(Status::Completed, original.identity, true));
                assert(sequence.transition(Status::Recorded, original.identity)
                    && sequence.record(Contract::protocol[0u], original.identity));
            };
            run();
            auto denied = budget;
            --denied.maxCommands;
            Sequence shortSequence(denied, original.identity);
            assert(shortSequence.record(Contract::protocol[0u], original.identity)
                && !shortSequence.commands());
            ZeroBaseIndirectSourceConsumptionContract::Facts source;
            assert(!inputs.producer.source->observe(source));
            for (const std::uint32_t consumerFault : {0u, 1u}) {
                Outcome::Readbacks readbacks{
                    source.physical, source.mapping, suppressed ? 1u : 0u, consumerFault};
                Outcome::Value value;
                assert(!Outcome::classify(*inputs.producer.source, readbacks, value));
                const auto expected = suppressed
                    ? (consumerFault ? Outcome::Value::ProducerAndConsumerFault
                                     : Outcome::Value::ProducerSuppressed)
                    : (consumerFault ? Outcome::Value::ConsumerFault
                                     : Outcome::Value::CleanProduced);
                assert(value == expected);
                for (auto* word : {&readbacks.producerFault,
                         &readbacks.consumerFault,
                         &readbacks.arguments.instanceCount,
                         &readbacks.arguments.firstInstance,
                         &readbacks.mapping.firstInstance,
                         &readbacks.mapping.instanceCount,
                         &readbacks.mapping.instanceCapacity,
                         &readbacks.mapping.contractVersion}) {
                    const auto saved = *word;
                    *word = UINT32_MAX;
                    assert(Outcome::classify(*inputs.producer.source, readbacks, value)
                        && value == Outcome::Value::Unclassified);
                    *word = saved;
                }
                readbacks.producerFault ^= 1u;
                assert(Outcome::classify(*inputs.producer.source, readbacks, value));
            }
            auto exhausted = core::makeShared<core::CpuMemoryQuota>(1u);
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            {
                core::CpuMemoryQuotaScope scope(exhausted);
                core::CpuMemoryQuotaLease pressure(exhausted, 1u);
                auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
                for (unsigned i = 0u; i < 100u; ++i) {
                    assert(!admission.validate(inputs, 1u));
                    run();
                }
                std::pmr::set_default_resource(previous);
            }
            assert(allocations == core::defaultCpuAllocator().stats().allocationCount
                && !exhausted->liveBytes());
            auto quota = core::makeShared<core::CpuMemoryQuota>(64u * 1024u);
            const auto factory = [&] {
                core::CpuMemoryQuotaScope scope(quota);
                try {
                    auto result = core::makeShared<Admission>();
                    assert(!Admission::capture(inputs, budget, 1u, *result));
                    return result;
                } catch (const std::bad_alloc&) {
                    return std::shared_ptr<Admission>{};
                }
            };
            CpuQuotaFactoryFixture probe(quota);
            const auto peak = probe.measure(factory);
            probe.verify(peak, factory);
            assert(!quota->liveBytes());
        }
    for (const auto backend : {Backend::Null,
             Backend::Vulkan,
             Backend::Metal,
             Backend::WebGPU,
             static_cast<Backend>(99u)}) {
        const auto recording = Contract::lowering(backend);
        const auto image = Contract::Content::lowering(backend);
        assert(!recording.executable && !image.executable && recording.diagnosticId
            && image.diagnosticId && recording.validatorOnly == (backend == Backend::Null)
            && image.validatorOnly == (backend == Backend::Null));
        const auto privateDecision = Contract::privateRecordingLowering(backend, Fixture::budget());
        assert(privateDecision.contractVersion == Contract::version && privateDecision.diagnosticId
            && privateDecision.allowsPrototypePayload() == (backend == Backend::Metal)
            && (privateDecision.disposition == Contract::PrivateDisposition::ValidatorOnly)
                == (backend == Backend::Null));
        for (unsigned invalid = 0u; invalid < 5u; ++invalid) {
            auto denied = Fixture::budget();
            switch (invalid) {
            case 0u:
                denied.contractVersion = 0u;
                break;
            case 1u:
                denied.prototypeOptIn = false;
                break;
            case 2u:
                --denied.maxCommands;
                break;
            case 3u:
                --denied.maxReadbackBytes;
                break;
            case 4u:
                --denied.maxReferenceSlots;
                break;
            }
            const auto decision = Contract::privateRecordingLowering(backend, denied);
            assert(!decision.allowsPrototypePayload()
                && decision.disposition == Contract::PrivateDisposition::DiagnosticFailure
                && decision.diagnosticId
                    == std::string_view("RHI-CLOSED-CONSUMER-RECORDING-BUDGET"));
        }
    }
    std::cout
        << "CanonicalClosedConsumerRecordingValues/1:4 source/endian/outcome cases, exact image512 snapshots, Null uploads/barriers, original refs26, protocol38, readbacks44, alias/revision/owners/budget/order/fault negatives, CPU exact/one-less and100 exhausted allocation0 PASS; no GPU revision/draw/completion authority\n";
}
} // namespace arp::test
