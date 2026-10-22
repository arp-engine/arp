#include "../fixtures/bindless_geometry_package_fixture.h"
#include "../fixtures/bindless_graph_validation.h"
#include "../fixtures/bindless_metal_graphics_pipeline.h"
#include "../fixtures/bindless_package_request_fixture.h"
#include "../fixtures/bindless_scene_root_inputs_fixture.h"
#include "../fixtures/canonical_producer_recording_fixture.h"
#include "../fixtures/cpu_quota_factory_fixture.h"
#include "../fixtures/original_bindless_geometry_fixture.h"
#include "../fixtures/original_bindless_root_source_fixture.h"
#include "../fixtures/original_mixed_image_snapshot_fixture.h"
#include "../fixtures/original_ordinary_opaque_binding_fixture.h"
#include "../fixtures/original_undefined_texture_upload_plan_fixture.h"
#include "../fixtures/zero_base_indirect_source_fixture.h"
#include "core/hash/sha256.h"
#include "core/io/file_io.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_binding_validation.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/webgpu/webgpu_binding_space_lowering.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "rhi/backends/webgpu/webgpu_native_runtime.h"
#include "rhi/frontend/bindless_compute_binding.h"
#include "rhi/frontend/bindless_graphics_binding.h"
#include "rhi/frontend/bindless_image_and_indirect_validation.h"
#include "rhi/frontend/bindless_indirect_packet.h"
#include "rhi/frontend/bindless_recording_validation.h"
#include "rhi/frontend/bindless_shader_package.h"
#include "rhi/frontend/bindless_snapshot.h"
#include "rhi/frontend/bindless_storage_image_domain.h"
#include "rhi/frontend/command_indirect_commands.h"
#include "rhi/frontend/command_usage.h"
#include "rhi/frontend/command_zero_base_indirect_processor_correspondence.h"
#include "rhi/gpu_bindless.h"
#include "rhi/gpu_extensions.h"
#include "rhi/semantics/bindless_entry_access.h"
#include "rhi/semantics/bindless_pipeline_interface.h"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <string_view>
#include <type_traits>

namespace arp::test {
void validateOriginalBindlessVisibilityRoots(
    const std::shared_ptr<const rhi::BindlessShaderPackage>&);
void validateCanonicalClosedConsumerRecording(std::shared_ptr<const rhi::BindlessShaderPackage>,
    std::shared_ptr<const rhi::BindlessShaderPackage>);
} // namespace arp::test

template <class T, class = void> struct CanForgePackage : std::false_type {};

template <class T> struct CanForgePackage<T, std::void_t<decltype(T({}))>> : std::true_type {};

template <class T, class = void> struct CanForgeAuthorization : std::false_type {};

template <class T>
struct CanForgeAuthorization<T,
    std::void_t<decltype(T({},
        nullptr,
        arp::core::LifetimeEpoch::Ticket{},
        std::shared_ptr<const arp::rhi::BindlessShaderPackage>{},
        arp::rhi::BindlessLoweringPlan{}))>> : std::true_type {};

static_assert(!CanForgePackage<arp::rhi::BindlessShaderPackage>::value);
static_assert(!CanForgePackage<arp::rhi_webgpu::WebGpuNativeZeroBaseIndirectProducer>::value);
static_assert(!CanForgeAuthorization<arp::rhi::BindlessAuthorization>::value);
static_assert(!CanForgePackage<arp::rhi::BindlessIndirectProcessorIntent>::value);
static_assert(
    !std::is_default_constructible_v<arp::rhi_metal::MetalRhiNativePreparedGraphicsBindings>);
static_assert(
    !std::is_copy_constructible_v<arp::rhi_metal::MetalRhiNativePreparedGraphicsBindings>);
static_assert(!CanForgePackage<arp::rhi_metal::MetalRhiNativePreparedGraphicsBindings>::value);
static_assert(!CanForgePackage<arp::rhi::BindlessOwnedFaultDomain>::value);
static_assert(!std::is_copy_constructible_v<arp::rhi::BindlessOwnedFaultDomain>);

void validateClosedConsumerGeometry(const arp::rhi::BindlessShaderPackage& package)
{
    using namespace arp;
    using namespace rhi;
    using Geometry = CanonicalZeroBaseIndirectConsumerGeometryContract;
    using Order = Geometry::Order;
    const std::array<std::uint32_t, 8u> bits{0xbf800000u,
        0xbf800000u,
        0x3f800000u,
        0xbf800000u,
        0u,
        0x3f800000u,
        0x3f000000u,
        0x3f000000u};
    std::array<std::uint8_t, 32u> canonical;
    core::UnsignedElementSpan::encodeWordsLittleEndian(bits, canonical);
    core::pmr::Vector<VertexBufferLayoutDesc> layout;
    VertexBufferLayoutDesc stream;
    stream.slot = 3u;
    stream.strideBytes = Geometry::strideBytes;
    stream.inputRate = VertexInputRate::Vertex;
    stream.attributes = {{0u, 0u, VertexFormat::Float32x2}};
    layout.push_back(std::move(stream));
    VertexInputConsumptionContract vertex(layout);
    for (const auto sourceOrder : {Order::LittleEndian, Order::BigEndian})
        for (const bool suppressed : {false, true})
            for (const auto positionOrder : {Order::LittleEndian, Order::BigEndian}) {
                test::ZeroBaseIndirectSourceFixture fixture(sourceOrder);
                if (suppressed)
                    fixture.put({UINT32_MAX, UINT32_MAX, UINT32_MAX, INT32_MIN, UINT32_MAX});
                auto source = fixture.make();
                assert(!source.diagnostic());
                auto authored = canonical;
                if (positionOrder == Order::BigEndian)
                    for (std::size_t offset = 0u; offset < authored.size();
                        offset += sizeof(std::uint32_t))
                        std::reverse(authored.begin() + offset,
                            authored.begin() + offset + sizeof(std::uint32_t));
                Geometry::Input input{authored.data(),
                    authored.size(),
                    Geometry::Encoding::Ieee754Binary32,
                    positionOrder};
                Geometry::Budget budget{
                    1u, true, authored.size(), authored.size() + source.sourceBytes().size()};
                const auto construct = [&] {
                    return Geometry(package, source, vertex, input, budget);
                };
                auto geometry = construct();
                assert(!geometry.diagnostic() && !geometry.validate(package, source, vertex));
                assert(geometry.slot() == 3u && geometry.bytes().size() == canonical.size()
                    && std::equal(
                        geometry.bytes().begin(), geometry.bytes().end(), canonical.begin())
                    && geometry.retainedBytes() == budget.maxSnapshotBytes);
                const auto reject = [&](const char* expected) {
                    auto rejected = construct();
                    assert(rejected.diagnostic() == std::string_view(expected)
                        && rejected.bytes().empty() && !rejected.packageIdentity());
                };
                --budget.contractVersion;
                reject("RHI-CANONICAL-CONSUMER-GEOMETRY-BUDGET");
                ++budget.contractVersion;
                budget.prototypeOptIn = false;
                reject("RHI-CANONICAL-CONSUMER-GEOMETRY-BUDGET");
                budget.prototypeOptIn = true;
                --budget.maxSnapshotBytes;
                reject("RHI-CANONICAL-CONSUMER-GEOMETRY-SNAPSHOT-BUDGET");
                ++budget.maxSnapshotBytes;
                --budget.maxGeometryBytes;
                reject("RHI-CANONICAL-CONSUMER-GEOMETRY-SNAPSHOT-BUDGET");
                ++budget.maxGeometryBytes;
                input.encoding = Geometry::Encoding::Unknown;
                reject("RHI-CANONICAL-CONSUMER-GEOMETRY-ENCODING");
                input.encoding = Geometry::Encoding::Ieee754Binary32;
                input.order = Order::Unknown;
                reject("CORE-UNSIGNED-SPAN-BYTE-ORDER-UNKNOWN");
                input.order = positionOrder;
                --input.bytes;
                reject("RHI-CANONICAL-CONSUMER-GEOMETRY-ENCODING");
                ++input.bytes;
                input.data = nullptr;
                reject("RHI-CANONICAL-CONSUMER-GEOMETRY-ENCODING");
                input.data = authored.data();
                // Suppressed draws perform no vertex fetch; active indices with base-2
                // reference vertices1..3 and require all four Float2 records.
                input.bytes = Geometry::strideBytes;
                if (suppressed) {
                    auto noFetch = construct();
                    assert(!noFetch.diagnostic());
                } else
                    reject("RHI-VERTEX-INPUT-FETCH-RANGE");
                input.bytes = authored.size();
                auto changed = fixture;
                changed.put({2u, 1u, 2u, -2, 2u});
                auto changedSource = changed.make();
                assert(geometry.validate(package, changedSource, vertex)
                    == std::string_view("RHI-CANONICAL-CONSUMER-GEOMETRY-CORRESPONDENCE"));
                const auto malformed = [&](auto& field, auto value) {
                    const auto saved = field;
                    field = value;
                    VertexInputConsumptionContract bad(layout);
                    auto rejected = Geometry(package, source, bad, input, budget);
                    assert(rejected.diagnostic() && geometry.validate(package, source, bad));
                    field = saved;
                };
                malformed(layout[0u].strideBytes, 16u);
                malformed(layout[0u].inputRate, VertexInputRate::Instance);
                malformed(layout[0u].attributes[0u].shaderLocation, 1u);
                malformed(layout[0u].attributes[0u].offsetBytes, 4u);
                malformed(layout[0u].attributes[0u].format, VertexFormat::Float32x3);
                layout[0u].slot = 4u;
                VertexInputConsumptionContract otherSlot(layout);
                assert(geometry.validate(package, source, otherSlot));
                layout[0u].slot = 3u;
                auto device = createNullDevice();
                assert(device);
                auto buffer = device->createBuffer({"ClosedGeometryValues",
                    geometry.bytes().size(),
                    ResourceUsage::VertexBuffer,
                    MemoryUsage::CpuToGpu,
                    ResourceState::ShaderRead});
                assert(buffer);
                assert(geometry.validateStorage(*buffer)
                    == std::string_view("RHI-CANONICAL-CONSUMER-GEOMETRY-WRITES-NOT-SEALED"));
                assert(
                    device->writeBuffer(*buffer, geometry.bytes().data(), geometry.bytes().size())
                        .success);
                buffer->sealWrites();
                assert(!geometry.validateStorage(*buffer));
                assert(
                    device->writeBuffer(*buffer, geometry.bytes().data(), geometry.bytes().size())
                        .diagnostics
                    == "RHI-BUFFER-HOST-WRITES-SEALED");
                auto& desc = const_cast<BufferDesc&>(buffer->desc());
                const auto savedDesc = desc;
                --desc.sizeBytes;
                assert(geometry.validateStorage(*buffer));
                desc = savedDesc;
                desc.usage = ResourceUsage::StorageBuffer;
                assert(geometry.validateStorage(*buffer));
                desc = savedDesc;
                desc.memory = MemoryUsage::GpuOnly;
                assert(geometry.validateStorage(*buffer));
                desc = savedDesc;
                auto exhausted = core::makeShared<core::CpuMemoryQuota>(1u);
                const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
                {
                    core::CpuMemoryQuotaScope scope(exhausted);
                    core::CpuMemoryQuotaLease pressure(exhausted, 1u);
                    auto* previous =
                        std::pmr::set_default_resource(std::pmr::null_memory_resource());
                    for (unsigned i = 0u; i < 100u; ++i) {
                        assert(!geometry.validate(package, source, vertex));
                        assert(!geometry.validateStorage(*buffer));
                    }
                    std::pmr::set_default_resource(previous);
                }
                assert(allocations == core::defaultCpuAllocator().stats().allocationCount
                    && !exhausted->liveBytes());
                authored.fill(0xffu);
                assert(std::equal(
                    geometry.bytes().begin(), geometry.bytes().end(), canonical.begin()));
                authored = canonical;
                if (positionOrder == Order::BigEndian)
                    for (std::size_t offset = 0u; offset < authored.size();
                        offset += sizeof(std::uint32_t))
                        std::reverse(authored.begin() + offset,
                            authored.begin() + offset + sizeof(std::uint32_t));
                auto quota = core::makeShared<core::CpuMemoryQuota>(64u * 1024u);
                const auto factory = [&] {
                    core::CpuMemoryQuotaScope scope(quota);
                    try {
                        return core::makeUnique<Geometry>(package, source, vertex, input, budget);
                    } catch (const std::bad_alloc&) {
                        return core::UniquePtr<Geometry>{};
                    }
                };
                test::CpuQuotaFactoryFixture probe(quota);
                const auto peak = probe.measure(factory);
                probe.verify(peak, factory);
                assert(!quota->liveBytes());
                auto moved = std::move(geometry);
                assert(!moved.validate(package, source, vertex)
                    && geometry.diagnostic()
                        == std::string_view("RHI-CANONICAL-CONSUMER-GEOMETRY-MOVED"));
            }
    for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        const auto decision = Geometry::lowering(backend);
        assert(!decision.executable && decision.validatorOnly == (backend == Backend::Null));
        const auto cold = Geometry::coldLowering(backend);
        assert(cold.prototypeUpload == (backend == Backend::Metal)
            && cold.validatorOnly == (backend == Backend::Null));
    }
    assert(!Geometry::lowering(static_cast<Backend>(99u)).executable);
    std::cout
        << "ClosedConsumerGeometryValues/1: explicit IEEE754 Float2 endian snapshots, full source/index fetch, active/suppressed cases, finite exact/one-less CPU/snapshot, hostile layout/storage, actual Null seals and100 allocation0 PASS; no native upload/execution\n";
}

void validateCanonicalProducerRecording(
    std::shared_ptr<const arp::rhi::BindlessShaderPackage> consumer,
    std::shared_ptr<const arp::rhi::BindlessShaderPackage> producer)
{
    using namespace arp;
    using namespace rhi;
    using Fixture = test::CanonicalProducerRecordingFixture;
    using Contract = Fixture::Contract;
    using Sequence = CanonicalZeroBaseIndirectProducerSequence;
    using Admission = CanonicalZeroBaseIndirectProducerRecordingAdmission;
    using Status = Sequence::Status;
    using Order = test::ZeroBaseIndirectSourceFixture::Order;
    for (const auto order : {Order::LittleEndian, Order::BigEndian})
        for (const bool suppressed : {false, true}) {
            Fixture fixture(producer, consumer, order, suppressed);
            auto& inputs = fixture.inputs;
            auto budget = Fixture::budget();
            Contract::Facts facts;
            Admission admission;
            assert(!Contract::verify(inputs, budget, 1u, facts));
            assert(facts.identity.valid() && facts.actualReadbackBytes == Contract::readbackBytes);
            assert(
                facts.metadata[2] == static_cast<std::uint32_t>(inputs.producerFault->generation())
                && facts.metadata[3]
                    == static_cast<std::uint32_t>(inputs.producerFault->generation() >> 32u));
            assert(!Admission::capture(inputs, budget, 1u, admission)
                && !admission.validate(inputs, 1u));
            const auto original = facts.identity;
            assert(
                Contract::verify(inputs, budget, 1u, facts, Contract::StoragePhase::ReadbackReady));
            assert(Contract::verify(
                       inputs, budget, 1u, facts, static_cast<Contract::StoragePhase>(99u))
                == std::string_view("RHI-CANONICAL-PRODUCER-RECORDING-STORAGE-PHASE"));
            fixture.transitionOutputs(ResourceState::ShaderWrite, ResourceState::CopySrc);
            assert(
                !Contract::verify(inputs, budget, 1u, facts, Contract::StoragePhase::ReadbackReady)
                && !admission.validate(inputs, 1u, Contract::StoragePhase::ReadbackReady));
            assert(Contract::verify(inputs, budget, 1u, facts) && admission.validate(inputs, 1u));
            fixture.transitionOutputs(ResourceState::CopySrc, ResourceState::ShaderWrite);
            assert(!admission.validate(inputs, 1u));
            const auto reject = [&](const char* expected) {
                facts.identity = original;
                facts.metadata.fill(UINT32_MAX);
                facts.bytes.fill(0xffu);
                facts.actualReadbackBytes = UINT64_MAX;
                const auto* actual = Contract::verify(inputs, budget, 1u, facts);
                if (!actual || std::strcmp(actual, expected))
                    std::cerr << "Canonical recording expected " << expected << ", got "
                              << (actual ? actual : "success") << '\n';
                assert(actual && !std::strcmp(actual, expected));
                assert(!facts.identity.valid() && !facts.identity.contractVersion
                    && !facts.actualReadbackBytes && facts.metadata == Contract::Cold::Words{}
                    && facts.bytes == Contract::Cold::Bytes{});
            };
            const auto mutate = [&](auto& field, auto value, const char* expected) {
                const auto saved = field;
                field = value;
                reject(expected);
                field = saved;
                assert(!admission.validate(inputs, 1u));
            };
            mutate(budget.contractVersion, 0u, "RHI-CANONICAL-PRODUCER-RECORDING-BUDGET");
            mutate(budget.prototypeOptIn, false, "RHI-CANONICAL-PRODUCER-RECORDING-BUDGET");
            mutate(budget.maxCommands,
                Contract::commandUnits - 1u,
                "RHI-CANONICAL-PRODUCER-RECORDING-BUDGET");
            mutate(budget.maxReadbackBytes,
                Contract::readbackBytes - 1u,
                "RHI-CANONICAL-PRODUCER-RECORDING-BUDGET");
            mutate(budget.maxReferenceSlots,
                Contract::referenceSlots - 1u,
                "RHI-CANONICAL-PRODUCER-RECORDING-BUDGET");
            mutate(inputs.consumerFault,
                inputs.producerFault,
                "RHI-CANONICAL-CONSUMER-COLD-FAULT-GENERATION");
            for (const auto role : {Contract::Role::Latch,
                     Contract::Role::FaultReadback,
                     Contract::Role::Parameters}) {
                auto& buffer = inputs.buffers[static_cast<std::size_t>(role)];
                mutate(buffer,
                    std::shared_ptr<const GpuBuffer>(inputs.consumerFault, buffer.get()),
                    "RHI-CANONICAL-PRODUCER-RECORDING-BUFFER-OWNER");
            }
            auto& args = inputs.buffers[static_cast<std::size_t>(Contract::Role::Arguments)];
            mutate(args,
                std::shared_ptr<const GpuBuffer>(inputs.packet, inputs.packet->physicalArgs()),
                "RHI-CANONICAL-PRODUCER-RECORDING-BUFFER-OWNER");
            {
                const auto saved = args;
                args = inputs.buffers[static_cast<std::size_t>(Contract::Role::Mapping)];
                reject("RHI-CANONICAL-PRODUCER-RECORDING-PROTECTED-STORAGE");
                args = saved;
            }
            {
                auto& image = inputs.images[0];
                const auto saved = image;
                image.view = args->id();
                reject("RHI-CANONICAL-PRODUCER-RECORDING-ALIAS");
                image = saved;
            }
            {
                auto& buffer =
                    inputs.buffers[static_cast<std::size_t>(Contract::Role::ArgumentsReadback)];
                auto& desc = const_cast<BufferDesc&>(buffer->desc());
                mutate(desc.memory,
                    MemoryUsage::CpuToGpu,
                    "RHI-CANONICAL-PRODUCER-RECORDING-READBACK");
                auto& allocation = const_cast<ResourceAllocation&>(buffer->allocation());
                mutate(allocation.estimatedSizeBytes,
                    allocation.estimatedSizeBytes + 1u,
                    "RHI-CANONICAL-PRODUCER-RECORDING-READBACK-BUDGET");
            }
            {
                const auto saved = inputs.source;
                test::ZeroBaseIndirectSourceFixture copy(order);
                if (suppressed)
                    copy.put({UINT32_MAX, UINT32_MAX, UINT32_MAX, INT32_MIN, UINT32_MAX});
                inputs.source =
                    core::makeShared<ZeroBaseIndirectSourceConsumptionContract>(copy.make());
                assert(!Contract::verify(inputs, budget, 1u, facts));
                assert(admission.validate(inputs, 1u)
                    == std::string_view("RHI-CANONICAL-PRODUCER-RECORDING-ORIGINAL-OWNER"));
                inputs.source = saved;
            }
            {
                auto forged = inputs;
                forged.source = std::shared_ptr<const ZeroBaseIndirectSourceConsumptionContract>(
                    inputs.packet, inputs.source.get());
                assert(admission.validate(forged, 1u)
                    == std::string_view("RHI-CANONICAL-PRODUCER-RECORDING-ORIGINAL-OWNER"));
                for (std::size_t i = 0u; i < forged.buffers.size(); ++i) {
                    forged = inputs;
                    forged.buffers[i] =
                        std::shared_ptr<const GpuBuffer>(inputs.source, inputs.buffers[i].get());
                    assert(admission.validate(forged, 1u)
                        == std::string_view("RHI-CANONICAL-PRODUCER-RECORDING-ORIGINAL-OWNER"));
                }
            }
            assert(admission.validate(inputs, 2u)
                == std::string_view("RHI-CANONICAL-PRODUCER-RECORDING-IDENTITY"));
            for (const auto backend :
                {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
                const auto privatePlan = Contract::privateRecordingLowering(backend, budget);
                assert(privatePlan.contractVersion == 1u
                    && privatePlan.allowsPrototypePayload() == (backend == Backend::Metal)
                    && (privatePlan.disposition == Contract::PrivateDisposition::ValidatorOnly)
                        == (backend == Backend::Null));
                auto denied = budget;
                denied.prototypeOptIn = false;
                assert(
                    !Contract::privateRecordingLowering(backend, denied).allowsPrototypePayload());
                denied = budget;
                denied.contractVersion = 0u;
                assert(
                    !Contract::privateRecordingLowering(backend, denied).allowsPrototypePayload());
                const auto lowering = Contract::lowering(backend);
                assert(!lowering.executable && lowering.validatorOnly == (backend == Backend::Null)
                    && lowering.diagnosticId);
                assert(Contract::executionDiagnostic()
                    == std::string_view("RHI-CANONICAL-PRODUCER-RECORDING-VALUES-ONLY"));
            }
            const auto run = [&] {
                Sequence sequence(budget, original);
                for (std::size_t step = 0u; step < Contract::protocol.size(); ++step) {
                    assert(sequence.finish() && sequence.transition(Status::Recorded, original));
                    assert(sequence.status() == Status::Cold && sequence.commands() == step);
                    for (const auto terminal : {Status::Failed, Status::Abandoned}) {
                        auto cancelled = sequence;
                        assert(!cancelled.transition(terminal, original));
                        assert(cancelled.transition(Status::Submitted, original)
                            && cancelled.transition(Status::Completed, original, true));
                        assert(cancelled.record(Contract::protocol[step], original));
                        assert(cancelled.status() == terminal && cancelled.commands() == step);
                    }
                    for (const auto hostile : Contract::protocol)
                        if (hostile != Contract::protocol[step]) {
                            assert(sequence.record(hostile, original));
                            assert(
                                sequence.commands() == step && sequence.status() == Status::Cold);
                        }
                    auto wrong = original;
                    ++wrong.recording;
                    assert(sequence.record(Contract::protocol[step], wrong));
                    assert(sequence.commands() == step);
                    ++wrong.producerGeneration;
                    assert(sequence.transition(Status::Completed, wrong, true));
                    assert(!sequence.record(Contract::protocol[step], original));
                }
                assert(!sequence.finish() && sequence.commands() == Contract::commandUnits);
                assert(sequence.transition(Status::Submitted, original));
                assert(!sequence.transition(Status::Recorded, original));
                auto abandoned = sequence;
                assert(!abandoned.transition(Status::Abandoned, original));
                assert(abandoned.transition(Status::Submitted, original));
                auto failed = sequence;
                assert(!failed.transition(Status::Failed, original));
                assert(failed.transition(Status::Submitted, original));
                assert(!sequence.transition(Status::Submitted, original));
                assert(sequence.transition(Status::Completed, original)
                    && sequence.transition(Status::Failed, original));
                assert(sequence.status() == Status::Submitted);
                auto wrong = original;
                ++wrong.consumerGeneration;
                assert(sequence.transition(Status::Completed, wrong, true));
                auto nativeFailed = sequence;
                assert(!nativeFailed.transition(Status::Failed, original, true));
                assert(nativeFailed.transition(Status::Completed, original, true));
                assert(!sequence.transition(Status::Completed, original, true));
                assert(sequence.transition(Status::Completed, original, true)
                    && sequence.transition(Status::Submitted, original));
                assert(sequence.record(Contract::protocol.front(), original));
                assert(!admission.validate(inputs, 1u));
            };
            run();
            for (std::size_t i = 0u; i < original.resources.size(); ++i) {
                auto wrong = original;
                wrong.resources[i] = {};
                Sequence invalid(budget, wrong);
                assert(invalid.record(Contract::protocol.front(), wrong) && invalid.finish());
            }
            auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            {
                core::CpuMemoryQuotaScope scope(quota);
                core::CpuMemoryQuotaLease pressure(quota, 1u);
                auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
                for (unsigned i = 0u; i < 100u; ++i)
                    run();
                std::pmr::set_default_resource(previous);
            }
            assert(allocations == core::defaultCpuAllocator().stats().allocationCount
                && !quota->liveBytes());
            {
                core::pmr::String diagnostic;
                auto lease =
                    BindlessFaultSubmissionLease::acquire(inputs.producerFault, diagnostic);
                assert(lease.valid());
                lease.markFaultReadbackCompleted();
                reject("RHI-CANONICAL-PRODUCER-RECORDING-PENDING-CLEAR");
            }
        }
    std::cout
        << "CanonicalProducerRecording/1: actual Null9 roles, exact originals,15 derived operations, correlated args20/map16/fault4, hostile order/completion,4 source cases and100 allocation0 observations PASS; values only\n";
}

void validateCanonicalConsumerColdResources(const arp::rhi::BindlessShaderPackage& package)
{
    using namespace arp;
    using namespace rhi;
    using Contract = CanonicalZeroBaseIndirectConsumerColdBindingContract;
    test::ZeroBaseIndirectSourceFixture sourceFixture;
    auto source = sourceFixture.make();
    auto device = createNullDevice();
    const auto* extension =
        static_cast<const BindlessCrossExtension*>(device->queryInterface(kExtensionBindlessCross));
    assert(extension);
    auto request = test::BindlessPackageRequestFixture::make(
        package.artifact(), extension->capabilities().deviceIdentity);
    BindlessIndirectPacketPool packets(*device,
        {2u,
            BindlessIndirectPacketStorageContract::minimumBytes,
            2u * BindlessIndirectPacketStorageContract::minimumBytes});
    core::pmr::String diagnostic;
    diagnostic.reserve(512u);
    auto packet =
        packets.prepareShared(request, package.artifact(), source.parameters(), diagnostic);
    if (!packet)
        std::cerr << "Cold Null packet: " << diagnostic << '\n';
    assert(packet && !packet->prototypeStorageOnly());
    auto alternatePacket =
        packets.prepareShared(request, package.artifact(), source.parameters(), diagnostic);
    assert(alternatePacket);
    auto producer = BindlessOwnedFaultDomain::create(
        *device, {1u, "ProducerColdValue", BindlessOwnedFaultDomain::minimumBytes}, diagnostic);
    auto consumer = BindlessOwnedFaultDomain::create(
        *device, {1u, "ConsumerColdValue", BindlessOwnedFaultDomain::minimumBytes}, diagnostic);
    assert(producer && consumer);
    std::array<core::UniquePtr<GpuTexture>, Contract::Consumer::capacity> textures;
    std::array<core::UniquePtr<GpuTextureView>, Contract::Consumer::capacity> views;
    Contract::Images images;
    for (std::size_t i = 0u; i < images.size(); ++i) {
        textures[i] = device->createTexture({"ActualLogicalImage",
            {4u, 2u, 1u},
            TextureFormat::Rgba8Unorm,
            TextureUsage::Sampled,
            MemoryUsage::GpuOnly,
            ResourceState::ShaderRead});
        assert(textures[i]);
        views[i] = device->createTextureView(
            *textures[i], {"ActualLogicalView", TextureFormat::Rgba8Unorm, TextureUsage::Sampled});
        assert(views[i]);
        assert(!BindlessTextureInventoryResource::capture(*textures[i], *views[i], images[i]));
    }
    const GpuBuffer* mapping = packet->instanceMappings();
    std::shared_ptr<const BindlessOwnedFaultDomain> selectedProducer = producer,
                                                    selectedConsumer = consumer;
    Contract::Facts facts;
    const auto check = [&](const char* id = nullptr) {
        facts.contractVersion = 99u;
        facts.words.fill(UINT32_MAX);
        facts.bytes.fill(0xffu);
        const auto* actual = Contract::verify(
            package, source, *packet, *mapping, images, selectedProducer, selectedConsumer, facts);
        if (id) {
            if (!actual || std::strcmp(actual, id))
                std::cerr << "Consumer cold expected " << id << ", got "
                          << (actual ? actual : "success") << '\n';
            assert(actual && !std::strcmp(actual, id));
            assert(!facts.contractVersion && !facts.consumerPackageIdentity
                && !facts.mapping.isValid() && !facts.packetGeneration
                && !facts.producerFaultGeneration && !facts.consumerFaultGeneration);
            assert(facts.words == Contract::Words{} && facts.bytes == Contract::Bytes{});
            for (const auto& image : facts.images)
                assert(!image.contractVersion && !image.view.isValid() && !image.parent.isValid());
        } else {
            assert(!actual && facts.contractVersion == 1u && facts.mapping == mapping->id());
            assert(facts.packetGeneration == packet->generation()
                && facts.producerFaultGeneration == producer->generation()
                && facts.consumerFaultGeneration == consumer->generation());
        }
    };
    check();
    const Contract::Words expected{1u,
        1u,
        static_cast<std::uint32_t>(consumer->generation()),
        static_cast<std::uint32_t>(consumer->generation() >> 32u),
        1u,
        3u,
        3u,
        0u,
        0u,
        0u,
        0u};
    assert(facts.words == expected && facts.bytes[0] == 1u && facts.bytes[4] == 1u);
    core::UnsignedElementSpan encoded(facts.bytes.data(),
        facts.bytes.size(),
        core::UnsignedElementSpan::Width::Uint32,
        core::UnsignedElementSpan::ByteOrder::LittleEndian);
    for (std::size_t i = 0u; i < expected.size(); ++i) {
        std::uint32_t word = 0u;
        assert(encoded.read(i, word) && word == expected[i]);
    }
    const auto mutate = [&](auto& field, auto value, const char* id) {
        const auto saved = field;
        field = value;
        check(id);
        field = saved;
        check();
    };
    mapping = alternatePacket->instanceMappings();
    check("RHI-CANONICAL-CONSUMER-COLD-MAPPING-ORIGINAL-STORAGE");
    mapping = packet->instanceMappings();
    auto& params = const_cast<ZeroBaseIndirectProducerParams&>(packet->parameters());
    mutate(params.sourceWordOffset,
        params.sourceWordOffset + 1u,
        "RHI-CANONICAL-CONSUMER-COLD-SOURCE-PARAMETERS");
    selectedConsumer = producer;
    check("RHI-CANONICAL-CONSUMER-COLD-FAULT-GENERATION");
    selectedConsumer = consumer;
    selectedConsumer = std::shared_ptr<const BindlessOwnedFaultDomain>(packet, consumer.get());
    check("RHI-CANONICAL-CONSUMER-COLD-FAULT-OWNER");
    selectedConsumer = consumer;
    selectedProducer = {};
    check("RHI-CANONICAL-CONSUMER-COLD-FAULT-OWNER");
    selectedProducer = producer;
    mutate(images[0].contractVersion, 0u, "RHI-BINDLESS-TEXTURE-INVENTORY-IDENTITY");
    mutate(images[0].view, GpuResourceId{}, "RHI-BINDLESS-TEXTURE-INVENTORY-IDENTITY");
    mutate(images[0].parent, GpuResourceId{}, "RHI-BINDLESS-TEXTURE-INVENTORY-IDENTITY");
    mutate(images[0].view, images[0].parent, "RHI-BINDLESS-TEXTURE-INVENTORY-IDENTITY");
    mutate(images[0].format, TextureFormat::Bgra8Unorm, "RHI-BINDLESS-TEXTURE-INVENTORY-DOMAIN");
    mutate(images[0].parentFormat,
        TextureFormat::Rgba8UnormSrgb,
        "RHI-BINDLESS-TEXTURE-INVENTORY-DOMAIN");
    mutate(images[0].dimension,
        TextureViewDimension::Texture2DArray,
        "RHI-BINDLESS-TEXTURE-INVENTORY-DOMAIN");
    mutate(images[0].range.aspect, TextureAspect::Depth, "RHI-BINDLESS-TEXTURE-INVENTORY-DOMAIN");
    mutate(images[0].sampleCount, 2u, "RHI-BINDLESS-TEXTURE-INVENTORY-DOMAIN");
    mutate(images[0].parentArrayLayerCount, 2u, "RHI-BINDLESS-TEXTURE-INVENTORY-DOMAIN");
    mutate(images[0].usage, TextureUsage::RenderAttachment, "RHI-BINDLESS-TEXTURE-INVENTORY-USAGE");
    mutate(images[0].parentUsage,
        TextureUsage::RenderAttachment,
        "RHI-BINDLESS-TEXTURE-INVENTORY-USAGE");
    mutate(images[0].parentExtent.width, 0u, "RHI-BINDLESS-TEXTURE-INVENTORY-RANGE");
    mutate(images[0].parentExtent.depth, 2u, "RHI-BINDLESS-TEXTURE-INVENTORY-RANGE");
    mutate(images[0].range.baseMipLevel, UINT32_MAX, "RHI-BINDLESS-TEXTURE-INVENTORY-RANGE");
    mutate(images[0].range.mipLevelCount, 0u, "RHI-BINDLESS-TEXTURE-INVENTORY-RANGE");
    mutate(images[0].range.baseArrayLayer, 1u, "RHI-BINDLESS-TEXTURE-INVENTORY-RANGE");
    mutate(images[0].state, ResourceState::Undefined, "RHI-BINDLESS-TEXTURE-INVENTORY-STATE");
    mutate(images[0].view, mapping->id(), "RHI-CANONICAL-CONSUMER-COLD-RESOURCE-ALIAS");
    mutate(images[0].parent,
        consumer->faultLatchBuffer()->id(),
        "RHI-CANONICAL-CONSUMER-COLD-RESOURCE-ALIAS");
    mutate(images[1].view, images[0].view, "RHI-BINDLESS-TEXTURE-INVENTORY-CONFLICT");
    {
        const auto original = images[1];
        images[1] = images[0];
        check();
        mutate(images[1].parentExtent.width, 5u, "RHI-BINDLESS-TEXTURE-INVENTORY-CONFLICT");
        images[1] = original;
    }
    {
        BindlessTextureInventoryResource missing;
        assert(BindlessTextureInventoryResource::capture(*textures[0], *views[1], missing));
        assert(!missing.contractVersion && !missing.view.isValid());
    }
    const auto& table = package.artifact().tables.front();
    BindlessTextureInventoryMetadataSource absent(table, nullptr, 3u),
        overflow(table, images.data(), 4u), valid(table, images.data(), 3u);
    assert(absent.diagnostic() && overflow.diagnostic() && !valid.diagnostic());
    BindlessMetadataTableSource row;
    std::uint32_t elements = 99u;
    assert(!valid.readTable(1u, row) && !row.mapping && !row.activeCount
        && !valid.readElementCount(0u, 3u, elements) && !elements);
    assert(valid.readTable(0u, row) && row.activeCount == images.size()
        && valid.readElementCount(0u, 2u, elements) && !elements);
    Contract::Budget budget{1u, true, Contract::minimumOwnedBytes, 1u};
    assert(!Contract::validateBudget(budget, Contract::minimumOwnedBytes, 1u));
    assert(Contract::validateBudget(budget, Contract::minimumOwnedBytes - 1u, 1u));
    assert(Contract::validateBudget(budget, Contract::minimumOwnedBytes + 1u, 1u));
    assert(Contract::validateBudget(budget, Contract::minimumOwnedBytes, 2u));
    assert(Contract::validateBudget(budget, Contract::minimumOwnedBytes, 0u));
    budget.prototypeOptIn = false;
    assert(Contract::validateBudget(budget, Contract::minimumOwnedBytes, 1u));
    budget.prototypeOptIn = true;
    for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        const auto lowering = Contract::lowering(backend);
        assert(!lowering.executable && lowering.validatorOnly == (backend == Backend::Null)
            && lowering.prototypeColdResources == (backend == Backend::Metal)
            && lowering.diagnosticId);
    }
    auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    {
        core::CpuMemoryQuotaScope scope(quota);
        core::CpuMemoryQuotaLease pressure(quota, 1u);
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        for (unsigned i = 0u; i < 100u; ++i) {
            check();
            images[0].range.baseMipLevel = UINT32_MAX;
            check("RHI-BINDLESS-TEXTURE-INVENTORY-RANGE");
            images[0].range.baseMipLevel = 0u;
            assert(Contract::validateBudget(budget, Contract::minimumOwnedBytes, 2u));
        }
        std::pmr::set_default_resource(previous);
    }
    assert(
        allocations == core::defaultCpuAllocator().stats().allocationCount && !quota->liveBytes());
    {
        auto lease = BindlessFaultSubmissionLease::acquire(consumer, diagnostic);
        assert(lease.valid());
        lease.markFaultReadbackCompleted();
        check("RHI-CANONICAL-CONSUMER-COLD-FAULT-PENDING-CLEAR");
    }
    std::cout
        << "ClosedConsumerColdResourceValues/1 actual Null logical packet/images/independent fault domains, original mapping, metadata44 generation, budgets/cleared facts/allocation0 PASS; no native resource authority\n";
}

void validateCanonicalConsumer(const arp::rhi::BindlessShaderPackage& package, bool canonical)
{
    using namespace arp;
    using namespace rhi;
    using Proof = CanonicalZeroBaseIndirectConsumerContract;
    test::ZeroBaseIndirectSourceFixture fixture;
    auto source = fixture.make();
    Proof::Facts facts;
    const auto cleared = [&] {
        assert(!facts.contractVersion && !facts.bindingCount && !facts.consumerPackageIdentity
            && !facts.consumer.contractVersion);
        for (const auto& binding : facts.bindings)
            assert(!binding.arrayCount && !binding.requiredBytes);
    };
    const auto check = [&](const char* expected) {
        facts.contractVersion = 99u;
        facts.bindingCount = 99u;
        facts.bindings[0].arrayCount = 99u;
        const auto* id = Proof::verify(package, source, facts);
        if (!id || std::strcmp(id, expected))
            std::cerr << "Consumer expected " << expected << ", got " << (id ? id : "success")
                      << '\n';
        assert(id && !std::strcmp(id, expected));
        cleared();
    };
    for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        const auto decision = Proof::lowering(backend);
        assert(!decision.executable && decision.validatorOnly == (backend == Backend::Null)
            && decision.diagnosticId);
    }
    if (!canonical) {
        check("RHI-CANONICAL-INDIRECT-CONSUMER-PACKAGE-DOMAIN");
        return;
    }
    assert(!Proof::verify(package, source, facts));
    const auto count = package.artifact().execution == BindlessExecution::Native ? 4u : 6u;
    assert(facts.contractVersion == 1u && facts.bindingCount == count
        && facts.consumerPackageIdentity == package.artifact().shaderPackageIdentity
        && facts.consumer.binding == 7u && facts.consumer.instanceCapacity == 3u);
    assert(facts.bindings[0].binding == 7u && facts.bindings[0].requiredBytes == 16u
        && facts.bindings[0].stages == ShaderStage::Vertex && facts.bindings[1].requiredBytes == 44u
        && facts.bindings[2].requiredBytes == 4u);
    {
        auto device = createNullDevice();
        const auto* extension = static_cast<const BindlessCrossExtension*>(
            device->queryInterface(kExtensionBindlessCross));
        assert(extension);
        auto request = test::BindlessPackageRequestFixture::make(
            package.artifact(), extension->capabilities().deviceIdentity);
        const auto plan = extension->plan(request, package.artifact());
        assert(plan.success && !plan.executable && plan.execution == BindlessExecution::Validator);
        for (const auto mode : {BindlessExecution::Native, BindlessExecution::Emulated}) {
            // Synthetic negative planner input isolates the new execution
            // refusal; it never supplies native creation or execution proof.
            auto caps = extension->capabilities();
            caps.execution = mode;
            caps.executablePayload = true;
            assert(planBindless(caps, request, package.artifact()).diagnosticId
                == std::string_view(
                    "RHI-BINDLESS-ZERO-BASE-INDIRECT-CONSUMER-EXECUTION-UNAVAILABLE"));
        }
        auto unsupported = package.artifact();
        unsupported.zeroBaseIndirectConsumerContractVersion = 2u;
        assert(extension->plan(request, unsupported).diagnosticId
            == std::string_view("RHI-BINDLESS-ZERO-BASE-INDIRECT-CONSUMER-VERSION"));
        auto generic = package.artifact();
        generic.zeroBaseIndirectConsumerContractVersion = 0u;
        const auto genericPlan = extension->plan(request, generic);
        assert(genericPlan.success && genericPlan.identity != plan.identity);
        core::pmr::String diagnostic;
        BindlessSnapshotPool pool(*device, request.budget);
        auto snapshot = pool.prepareShared(request, package.artifact(), diagnostic);
        assert(snapshot);
        const auto expectedBytes = Proof::metadataWordCount * sizeof(std::uint32_t)
            + sizeof(std::uint32_t)
            + (extension->capabilities().faultReadbackContractVersion == 1u ? sizeof(std::uint32_t)
                                                                            : 0u);
        assert(snapshot->reportedInternalBufferBytes() == expectedBytes
            && expectedBytes <= package.artifact().snapshotBytes);
        assert(!snapshot->seal(*device).success);
    }
    auto& artifact = const_cast<BindlessShaderArtifact&>(package.artifact());
    const auto mutate = [&](auto& field, auto value, const char* id) {
        const auto saved = field;
        field = value;
        check(id);
        field = saved;
        assert(!Proof::verify(package, source, facts));
    };
    mutate(artifact.zeroBaseIndirectConsumerContractVersion,
        0u,
        "RHI-CANONICAL-INDIRECT-CONSUMER-PACKAGE-DOMAIN");
    mutate(artifact.execution,
        BindlessExecution::Validator,
        "RHI-CANONICAL-INDIRECT-CONSUMER-PACKAGE-DOMAIN");
    mutate(artifact.indirectInstanceConsumer.binding, 0u, "RHI-ZERO-BASE-SOURCE-CONSUMER-MAPPING");
    mutate(artifact.indirectInstanceConsumer.instanceCapacity,
        2u,
        "RHI-ZERO-BASE-SOURCE-CONSUMER-MAPPING");
    mutate(
        artifact.indirectInstanceConsumer.recordIndex, 1u, "RHI-ZERO-BASE-SOURCE-CONSUMER-MAPPING");
    mutate(artifact.tables[0].operations,
        BindlessOperation::Store,
        "RHI-CANONICAL-INDIRECT-CONSUMER-LAYOUT");
    mutate(artifact.tables[0].capacity, 2u, "RHI-CANONICAL-INDIRECT-CONSUMER-LAYOUT");
    mutate(artifact.tables[0].metadataWordOffset, 5u, "RHI-CANONICAL-INDIRECT-CONSUMER-LAYOUT");
    mutate(artifact.snapshotMetadata.wordCount, 12u, "RHI-CANONICAL-INDIRECT-CONSUMER-LAYOUT");
    mutate(
        artifact.faultLatch.stages, ShaderStage::Vertex, "RHI-CANONICAL-INDIRECT-CONSUMER-LAYOUT");
    auto& module = const_cast<BindlessPackageModule&>(package.modules()[0]);
    mutate(module.reflection.reflection.vertexAttributes[0].format,
        shader::VertexAttributeFormat::Float3,
        "RHI-CANONICAL-INDIRECT-CONSUMER-VERTEX-INTERFACE");
    mutate(module.reflection.reflection.vertexAttributes[0].location,
        1u,
        "RHI-CANONICAL-INDIRECT-CONSUMER-VERTEX-INTERFACE");
    mutate(module.reflection.reflection.bindings[0].stageMask,
        static_cast<std::uint32_t>(ShaderStage::Fragment),
        "RHI-CANONICAL-INDIRECT-CONSUMER-LAYOUT");
    auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    {
        core::CpuMemoryQuotaScope scope(quota);
        core::CpuMemoryQuotaLease pressure(quota, 1u);
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        for (unsigned i = 0u; i < 100u; ++i) {
            assert(!Proof::verify(package, source, facts));
            artifact.indirectInstanceConsumer.instanceCapacity = 2u;
            check("RHI-ZERO-BASE-SOURCE-CONSUMER-MAPPING");
            artifact.indirectInstanceConsumer.instanceCapacity = 3u;
        }
        std::pmr::set_default_resource(previous);
    }
    assert(
        allocations == core::defaultCpuAllocator().stats().allocationCount && !quota->liveBytes());
    fixture.put({UINT32_MAX, UINT32_MAX, UINT32_MAX, INT32_MIN, UINT32_MAX});
    auto suppressed = fixture.make();
    assert(!Proof::verify(package, suppressed, facts));
    std::cout
        << "ClosedImageMappingConsumer/1 "
        << (package.artifact().execution == BindlessExecution::Native ? "Native" : "Emulated")
        << ": actual selected targets, tuple7/0/3, SPV Float2 location0, mapping16 metadata44 fault4, hostile correspondence/cleared facts/allocation0 PASS; no native execution authority\n";
}

void validateCanonicalSource(const arp::rhi::BindlessShaderPackage& package, bool canonical)
{
    using namespace arp;
    using namespace rhi;
    using Proof = CanonicalZeroBaseIndirectSourceContract;
    test::ZeroBaseIndirectSourceFixture fixture;
    auto source = fixture.make();
    assert(!source.diagnostic());
    Proof::Facts facts;
    const auto cleared = [&]() {
        assert(!facts.contractVersion && !facts.producerPackageIdentity
            && !facts.source.contractVersion && !facts.parameters.contractVersion
            && !facts.consumer.contractVersion);
        for (const auto& binding : facts.bindings)
            assert(!binding.requiredBytes);
    };
    const auto check = [&](const char* expected) {
        facts.contractVersion = 99u;
        facts.producerPackageIdentity = 99u;
        facts.bindings.front().requiredBytes = 99u;
        const auto* id = Proof::verify(package, source, Proof::selectedEntry, facts);
        if (!id || std::strcmp(id, expected))
            std::cerr << "Expected " << expected << ", got " << (id ? id : "success") << '\n';
        assert(id && !std::strcmp(id, expected));
        cleared();
    };
    if (!canonical) {
        check("RHI-CANONICAL-INDIRECT-SOURCE-PACKAGE-DOMAIN");
        return;
    }
    const auto* initialDiagnostic = Proof::verify(package, source, Proof::selectedEntry, facts);
    if (initialDiagnostic)
        std::cerr << "Canonical source admission: " << initialDiagnostic << '\n';
    assert(!initialDiagnostic);
    assert(facts.contractVersion == 1u
        && facts.producerPackageIdentity == package.artifact().shaderPackageIdentity
        && facts.execution == package.artifact().execution && !facts.source.physical.firstInstance
        && facts.source.physical.instanceCount == 2u && facts.source.mapping.firstInstance == 1u
        && facts.parameters.sourceWordOffset == 2u && facts.consumer.instanceCapacity == 3u);
    const auto expected = facts;
    const std::array<std::uint64_t, 6u> bytes{
        std::uint64_t{package.artifact().snapshotMetadata.wordCount} * 4u, 4u, 32u, 20u, 16u, 16u};
    for (std::size_t i = 0u; i < bytes.size(); ++i)
        assert(facts.bindings[i].requiredBytes == bytes[i]
            && facts.bindings[i].set == package.artifact().set);
    assert(Proof::verify(package, source, "computeMain", facts));
    cleared();
    // Published package fields are immutable in production. Hostile in-memory
    // mutations verify that correspondence is checked again before admission.
    auto& artifact = const_cast<BindlessShaderArtifact&>(package.artifact());
    const auto mutate = [&](auto& field, auto replacement, const char* id) {
        const auto saved = field;
        field = replacement;
        check(id);
        field = saved;
        assert(!Proof::verify(package, source, Proof::selectedEntry, facts));
    };
    mutate(artifact.zeroBaseIndirectProducerContractVersion,
        0u,
        "RHI-CANONICAL-INDIRECT-SOURCE-PACKAGE-DOMAIN");
    mutate(artifact.execution,
        BindlessExecution::Validator,
        "RHI-CANONICAL-INDIRECT-SOURCE-PACKAGE-DOMAIN");
    mutate(artifact.snapshotMetadata.stages,
        ShaderStage::Vertex,
        "RHI-CANONICAL-INDIRECT-SOURCE-LAYOUT");
    mutate(artifact.faultLatch.binding,
        artifact.snapshotMetadata.binding,
        "RHI-CANONICAL-INDIRECT-SOURCE-LAYOUT");
    mutate(artifact.tables.front().capacity, 2u, "RHI-CANONICAL-INDIRECT-SOURCE-LAYOUT");
    mutate(artifact.tables.front().operations,
        BindlessOperation::Store,
        "RHI-CANONICAL-INDIRECT-SOURCE-LAYOUT");
    mutate(artifact.tables.front().bufferLayout.elementStrideBytes,
        8u,
        "RHI-CANONICAL-INDIRECT-SOURCE-LAYOUT");
    mutate(artifact.physicalLayout.bindings.front().stages,
        ShaderStage::Fragment,
        "RHI-CANONICAL-INDIRECT-SOURCE-LAYOUT");
    auto& module = const_cast<BindlessPackageModule&>(package.modules().front());
    mutate(module.reflection.reflection.entryPoints.front().computeWorkgroupSizeX,
        2u,
        "RHI-CANONICAL-INDIRECT-SOURCE-ENTRY");
    mutate(module.reflection.reflection.bindings.front().stageMask,
        static_cast<std::uint32_t>(ShaderStage::Vertex),
        "RHI-CANONICAL-INDIRECT-SOURCE-LAYOUT");
    auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    {
        core::CpuMemoryQuotaScope scope(quota);
        core::CpuMemoryQuotaLease exhausted(quota, 1u);
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        for (unsigned i = 0u; i < 100u; ++i)
            assert(!Proof::verify(package, source, Proof::selectedEntry, facts));
        std::pmr::set_default_resource(previous);
    }
    assert(
        core::defaultCpuAllocator().stats().allocationCount == allocations && !quota->liveBytes());
    assert(facts.producerPackageIdentity == expected.producerPackageIdentity);
    fixture.put({UINT32_MAX, UINT32_MAX, UINT32_MAX, INT32_MIN, UINT32_MAX});
    auto suppressed = fixture.make();
    assert(!Proof::verify(package, suppressed, Proof::selectedEntry, facts)
        && facts.source.outcome
            == ZeroBaseIndirectSourceConsumptionContract::Outcome::FaultSuppressed
        && !facts.source.physical.instanceCount && !facts.source.mapping.contractVersion);
    fixture.parameters.contractVersion = 0u;
    auto invalid = fixture.make();
    assert(Proof::verify(package, invalid, Proof::selectedEntry, facts));
    cleared();
    std::cout
        << "CanonicalZeroBaseIndirectSource/1 "
        << (package.artifact().execution == BindlessExecution::Native ? "Native" : "Emulated")
        << ": actual compiler-owned targets, exact six roles/spans/source/params/mapping, hostile mutation cleared facts and allocation0 PASS; no native authority\n";
}

void validatePreparedComputeBinding(arp::rhi::GpuDevice& device,
    const arp::rhi::BindlessCrossExtension& extension,
    std::shared_ptr<const arp::rhi::BindlessSnapshot> snapshot,
    std::shared_ptr<const arp::rhi::ComputePipeline> pipeline,
    const arp::rhi::BindlessAccessDeclarations& accesses)
{
    using namespace arp::rhi;
    const auto retained =
        snapshot->plan().snapshotBytes + pipeline->bindlessAuthorization()->package().sourceBytes();
    BindlessComputeBindingBudget budget{1u, retained + 1024u, retained + 1024u, 6u, 1024u};
    BindlessComputeBindingDesc desc{
        true, snapshot, pipeline, snapshot->plan(), snapshot->generation()};
    BindlessComputeBindingPool pool(extension, budget);
    arp::core::pmr::String diagnostic;
    const auto reject = [&](BindlessComputeBindingDesc changed, const char* expected) {
        assert(!pool.prepare(std::move(changed), diagnostic) && diagnostic == expected);
        assert(pool.liveBindings() == 0u && pool.chargedBytes() == 0u);
    };
    auto changed = desc;
    changed.optIn = false;
    reject(changed, "RHI-BINDLESS-OPT-IN-REQUIRED");
    changed = desc;
    changed.snapshot.reset();
    reject(changed, "RHI-BINDLESS-PREPARED-COMPUTE-OWNERS-MISSING");
    changed = desc;
    changed.pipeline.reset();
    reject(changed, "RHI-BINDLESS-PREPARED-COMPUTE-OWNERS-MISSING");
    changed = desc;
    changed.pipeline = std::shared_ptr<const ComputePipeline>(snapshot, pipeline.get());
    reject(changed, "RHI-BINDLESS-PREPARED-COMPUTE-OWNERS-MISMATCH");
    changed = desc;
    changed.snapshot = std::shared_ptr<const BindlessSnapshot>(pipeline, snapshot.get());
    reject(changed, "RHI-BINDLESS-PREPARED-COMPUTE-OWNERS-MISMATCH");
    const auto authorizationAlias = std::shared_ptr<const BindlessAuthorization>(
        snapshot, pipeline->bindlessAuthorization().get());
    assert(!extension.createComputePipeline(authorizationAlias, "computeMain", diagnostic)
        && diagnostic == "RHI-BINDLESS-PIPELINE-AUTHORIZATION-INVALID");
    changed = desc;
    changed.generation++;
    reject(changed, "RHI-BINDLESS-PREPARED-COMPUTE-PLAN-MISMATCH");
    changed = desc;
    changed.plan.budget.maxSnapshots++;
    reject(changed, "RHI-BINDLESS-PREPARED-COMPUTE-PLAN-MISMATCH");
    auto other = createNullDevice();
    const auto* foreign =
        static_cast<BindlessCrossExtension*>(other->queryInterface(kExtensionBindlessCross));
    BindlessComputeBindingPool foreignPool(*foreign, budget);
    assert(!foreignPool.prepare(desc, diagnostic)
        && diagnostic == "RHI-BINDLESS-PREPARED-COMPUTE-AUTHORIZATION-INVALID");
    other.reset();
    assert(!foreignPool.prepare(desc, diagnostic)
        && diagnostic == "RHI-BINDLESS-PREPARED-COMPUTE-STALE");
    auto smallBudget = budget;
    smallBudget.maxBindingBytes--;
    BindlessComputeBindingPool small(extension, smallBudget);
    assert(!small.prepare(desc, diagnostic)
        && diagnostic == "RHI-BINDLESS-PREPARED-COMPUTE-FOOTPRINT");
    for (unsigned field = 0u; field < 5u; ++field) {
        auto invalidBudget = budget;
        if (field == 0u)
            invalidBudget.maxBindings = 0u;
        if (field == 1u)
            invalidBudget.maxResidentBytes = 0u;
        if (field == 2u)
            invalidBudget.maxBindingBytes = 0u;
        if (field == 3u)
            invalidBudget.maxArgumentBuffers = 0u;
        if (field == 4u)
            invalidBudget.maxArgumentBufferBytes = 0u;
        BindlessComputeBindingPool invalid(extension, invalidBudget);
        assert(!invalid.prepare(desc, diagnostic)
            && diagnostic == "RHI-BINDLESS-PREPARED-COMPUTE-BUDGET");
    }
    auto binding = pool.prepare(desc, diagnostic);
    assert(binding && binding->validateForRecord(*pipeline, accesses).success);
    assert(binding->argumentBufferCount() == 0u && binding->argumentBufferBytes() == 0u);
    assert(pool.liveBindings() == 1u && pool.chargedBytes() == budget.maxBindingBytes);
    assert(
        !pool.prepare(desc, diagnostic) && diagnostic == "RHI-BINDLESS-PREPARED-COMPUTE-POOL-FULL");
    auto alternate = arp::core::shareUnique(extension.createComputePipeline(
        pipeline->bindlessAuthorization(), "computeMain", diagnostic));
    assert(alternate
        && binding->validateForRecord(*alternate, accesses).diagnostics
            == "RHI-BINDLESS-PREPARED-COMPUTE-PIPELINE-MISMATCH");
    assert(!binding->validateForRecord(*pipeline, BindlessAccessArray(nullptr, 0u)).success);
    auto residentBudget = budget;
    residentBudget.maxBindings = 2u;
    BindlessComputeBindingPool resident(extension, residentBudget);
    auto residentBinding = resident.prepare(desc, diagnostic);
    assert(residentBinding && !resident.prepare(desc, diagnostic)
        && diagnostic == "RHI-BINDLESS-PREPARED-COMPUTE-RESIDENT-BUDGET");
    residentBinding.reset();
    assert(resident.liveBindings() == 0u && resident.chargedBytes() == 0u);
    auto encoder = device.createCommandEncoder({"prepared compute", 1u, 1u});
    auto pass = encoder->beginComputePass({"prepared compute"});
    auto* indexed =
        static_cast<BindlessPassExtension*>(pass->queryInterface(kExtensionBindlessCross));
    assert(indexed && !indexed->bindPreparedComputeBinding(binding, accesses).success);
    assert(pass->setPipeline(*pipeline).success);
    auto bindingAlias =
        std::shared_ptr<const BindlessPreparedComputeBinding>(snapshot, binding.get());
    assert(indexed->bindPreparedComputeBinding(bindingAlias, accesses).diagnostics
        == "RHI-BINDLESS-PREPARED-COMPUTE-PAYLOAD-MISMATCH");
    auto snapshotAlias = std::shared_ptr<const BindlessSnapshot>(pipeline, snapshot.get());
    assert(indexed->bindSnapshot(snapshotAlias, snapshot->plan(), snapshot->generation(), accesses)
               .diagnostics
        == "RHI-BINDLESS-RECORD-SNAPSHOT-OWNER-MISMATCH");
    bindingAlias.reset();
    snapshotAlias.reset();

    class ForgedBinding final : public BindlessPreparedComputeBinding {
    public:
        explicit ForgedBinding(std::shared_ptr<const BindlessComputeBindingOwners> owners)
            : BindlessPreparedComputeBinding(std::move(owners))
        {}

        ValidationResult validatePayload() const override { return {true, {}}; }

        std::uint32_t argumentBufferCount() const noexcept override { return 0u; }

        std::uint64_t argumentBufferBytes() const noexcept override { return 0u; }
    };

    auto forged = arp::core::makeShared<ForgedBinding>(binding->owners());
    auto ownersAlias =
        std::shared_ptr<const BindlessComputeBindingOwners>(snapshot, binding->owners().get());
    auto aliasedOwnersBinding = arp::core::makeShared<ForgedBinding>(ownersAlias);
    assert(aliasedOwnersBinding->validateOwners().diagnostics
        == "RHI-BINDLESS-PREPARED-COMPUTE-OWNERS-MISMATCH");
    assert(aliasedOwnersBinding->validateForRecord(*pipeline, accesses).diagnostics
        == "RHI-BINDLESS-PREPARED-COMPUTE-OWNERS-MISMATCH");
    aliasedOwnersBinding.reset();
    ownersAlias.reset();
    assert(indexed->bindPreparedComputeBinding(forged, accesses).diagnostics
        == "RHI-BINDLESS-PREPARED-COMPUTE-PAYLOAD-MISMATCH");
    forged.reset();
    const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
    for (unsigned i = 0u; i < 32u; ++i) {
        assert(pass->setPipeline(*pipeline).success);
        assert(indexed->bindPreparedComputeBinding(binding, accesses).success);
        assert(pass->dispatch(1u, 1u, 1u).success);
    }
    assert(arp::core::defaultCpuAllocator().stats().allocationCount == allocations);
    assert(pass->end().success);
    auto commands = encoder->finish();
    assert(commands);
    std::weak_ptr<const BindlessPreparedComputeBinding> observed = binding;
    binding.reset();
    pass.reset();
    encoder.reset();
    assert(!observed.expired() && pool.liveBindings() == 1u);
    assert(device.queue().submit(*commands).success);
    commands.reset();
    assert(observed.expired() && pool.liveBindings() == 0u && pool.chargedBytes() == 0u);
    std::shared_ptr<const BindlessPreparedComputeBinding> survivor;
    {
        BindlessComputeBindingPool temporary(extension, budget);
        survivor = temporary.prepare(desc, diagnostic);
        assert(survivor);
    }
    assert(survivor->validateForRecord(*pipeline, accesses).success);
}

void validateProcessorIntent(std::shared_ptr<const arp::rhi::BindlessShaderPackage> consumer,
    std::shared_ptr<const arp::rhi::BindlessShaderPackage> producer,
    bool mixedConsumer = false)
{
    validateCanonicalSource(*consumer, false);
    using namespace arp::rhi;
    auto device = createNullDevice();
    const auto* extension =
        static_cast<const BindlessCrossExtension*>(device->queryInterface(kExtensionBindlessCross));
    const auto& artifact = consumer->artifact();
    assert(consumer->modules().size() == (mixedConsumer ? 3u : 2u) && artifact.tables.size() == 1u);
    assert(artifact.indirectInstanceConsumer.contractVersion == 1u);
    BindlessRequirements request;
    request.optIn = true;
    request.allowEmulated = true;
    request.deviceIdentity = extension->capabilities().deviceIdentity;
    request.shaderPackageIdentity = artifact.shaderPackageIdentity;
    request.budget = {1u, 5u, 1024u, 2u};
    const auto& table = artifact.tables.front();
    const auto binding = std::find_if(artifact.physicalLayout.bindings.begin(),
        artifact.physicalLayout.bindings.end(),
        [&](const auto& value) {
            return value.binding == table.firstBinding;
        });
    assert(binding != artifact.physicalLayout.bindings.end());
    request.tables.push_back(
        {table.tableId, *binding, 2u, table.operations, table.nonUniform, table.bufferLayout});
    arp::core::pmr::String diagnostic;
    constexpr auto packetBytes = sizeof(DrawIndexedIndirectCommand) + sizeof(IndirectInstanceBase)
        + sizeof(ZeroBaseIndirectProducerParams);
    BindlessIndirectPacketPool packets(*device, {1u, packetBytes, packetBytes});
    auto packet = packets.prepareShared(request, artifact, {5u, 2u, 0u, 1u}, diagnostic);
    if (!packet)
        std::cerr << diagnostic << '\n';
    assert(packet);
    BindlessSnapshotPool snapshots(*device, request.budget);
    auto snapshot = snapshots.prepareShared(request, artifact, diagnostic);
    assert(snapshot && snapshot->writeOrdinaryBuffer(0u, *packet->instanceMappings()).success);
    auto sampled = device->createTexture({"processor consumer",
        {1u, 1u, 1u},
        TextureFormat::Rgba8Unorm,
        TextureUsage::Sampled,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderRead});
    TextureViewDesc viewDesc;
    viewDesc.format = TextureFormat::Rgba8Unorm;
    viewDesc.usage = TextureUsage::Sampled;
    auto view = device->createTextureView(*sampled, viewDesc);
    assert(sampled && view);
    for (std::uint32_t i = 0u; i < 2u; ++i)
        assert(snapshot->writeTexture(snapshot->handle(1u, i), *view, ResourceState::ShaderRead)
                .success);
    assert(snapshot->seal(*device).success);
    auto authorization = extension->authorize(consumer, *snapshot, diagnostic);
    auto source = arp::core::shareUnique(device->createBuffer({"logical indirect source",
        40u,
        ResourceUsage::StorageBuffer,
        MemoryUsage::CpuToGpu,
        ResourceState::ShaderRead}));
    assert(authorization && source);
    BindlessIndirectProcessorIntentDesc desc{true,
        producer,
        source,
        packet,
        snapshot,
        authorization,
        snapshot->plan(),
        snapshot->generation()};
    const auto retainedBytes = source->allocation().estimatedSizeBytes
        + packet->reportedBufferBytes() + snapshot->plan().snapshotBytes + producer->sourceBytes()
        + consumer->sourceBytes();
    BindlessIndirectProcessorIntentPool pool(*device, {1u, retainedBytes, retainedBytes});
    if (mixedConsumer) {
        assert(!pool.prepare(desc, diagnostic)
            && diagnostic == "RHI-BINDLESS-INDIRECT-PROCESSOR-CONSUMER-NOT-GRAPHICS-ONLY");
        return;
    }
    const auto reject = [&](BindlessIndirectProcessorIntentDesc changed, const char* expected) {
        assert(!pool.prepare(std::move(changed), diagnostic));
        assert(diagnostic == expected && pool.liveIntents() == 0u && pool.chargedBytes() == 0u);
    };
    auto changed = desc;
    changed.optIn = false;
    reject(changed, "RHI-BINDLESS-OPT-IN-REQUIRED");
    changed = desc;
    changed.consumerAuthorization.reset();
    reject(changed, "RHI-BINDLESS-INDIRECT-PROCESSOR-AUTHORIZATION-INVALID");
    changed = desc;
    changed.producer.reset();
    reject(changed, "RHI-BINDLESS-INDIRECT-PROCESSOR-OWNERS-MISSING");
    changed = desc;
    changed.logicalSource.reset();
    reject(changed, "RHI-BINDLESS-INDIRECT-PROCESSOR-OWNERS-MISSING");
    changed = desc;
    changed.packet.reset();
    reject(changed, "RHI-BINDLESS-INDIRECT-PROCESSOR-OWNERS-MISSING");
    changed = desc;
    changed.consumerSnapshot.reset();
    reject(changed, "RHI-BINDLESS-INDIRECT-PROCESSOR-OWNERS-MISSING");
    changed = desc;
    changed.consumerPlan.budget.maxSnapshots++;
    reject(changed, "RHI-BINDLESS-INDIRECT-PROCESSOR-PLAN-MISMATCH");
    changed = desc;
    changed.consumerGeneration++;
    reject(changed, "RHI-BINDLESS-INDIRECT-PACKET-PLAN-MISMATCH");
    changed = desc;
    changed.producer = consumer;
    reject(changed, "RHI-BINDLESS-INDIRECT-PROCESSOR-PACKAGE-MISMATCH");
    changed = desc;
    changed.producer = std::shared_ptr<const BindlessShaderPackage>(consumer, producer.get());
    reject(changed, "RHI-BINDLESS-INDIRECT-PROCESSOR-OWNER-MISMATCH");
    changed = desc;
    changed.logicalSource = std::shared_ptr<const GpuBuffer>(producer, source.get());
    reject(changed, "RHI-BINDLESS-INDIRECT-PROCESSOR-OWNER-MISMATCH");
    changed = desc;
    changed.packet = std::shared_ptr<const BindlessIndirectPacket>(producer, packet.get());
    reject(changed, "RHI-BINDLESS-INDIRECT-PROCESSOR-OWNER-MISMATCH");
    changed = desc;
    changed.consumerSnapshot = std::shared_ptr<const BindlessSnapshot>(producer, snapshot.get());
    reject(changed, "RHI-BINDLESS-INDIRECT-PROCESSOR-OWNER-MISMATCH");
    changed = desc;
    changed.consumerAuthorization =
        std::shared_ptr<const BindlessAuthorization>(producer, authorization.get());
    reject(changed, "RHI-BINDLESS-INDIRECT-PROCESSOR-OWNER-MISMATCH");
    assert(
        !extension->authorize(std::shared_ptr<const BindlessShaderPackage>(source, consumer.get()),
            *snapshot,
            diagnostic)
        && diagnostic == "RHI-BINDLESS-AUTHORIZATION-PACKAGE-OWNER-MISMATCH");
    auto foreign = createNullDevice();
    changed = desc;
    changed.logicalSource = arp::core::shareUnique(foreign->createBuffer(source->desc()));
    reject(changed, "RHI-BINDLESS-INDIRECT-PROCESSOR-SOURCE-DEVICE-MISMATCH");
    BindlessIndirectProcessorIntentPool foreignPool(*foreign, {1u, retainedBytes, retainedBytes});
    assert(!foreignPool.prepare(desc, diagnostic)
        && diagnostic == "RHI-BINDLESS-INDIRECT-PROCESSOR-AUTHORIZATION-INVALID");
    BindlessIndirectProcessorIntentPool small(*device, {1u, retainedBytes - 1u, retainedBytes});
    assert(!small.prepare(desc, diagnostic)
        && diagnostic == "RHI-BINDLESS-INDIRECT-PROCESSOR-FOOTPRINT-EXCEEDED");
    BindlessIndirectProcessorIntentPool resident(*device, {2u, retainedBytes, retainedBytes});
    auto first = resident.prepare(desc, diagnostic);
    assert(first && !resident.prepare(desc, diagnostic)
        && diagnostic == "RHI-BINDLESS-INDIRECT-PROCESSOR-RESIDENT-BUDGET"
        && resident.liveIntents() == 1u);
    first.reset();
    assert(resident.liveIntents() == 0u && resident.chargedBytes() == 0u);
    std::shared_ptr<const BindlessIndirectProcessorIntent> survivor;
    {
        BindlessIndirectProcessorIntentPool temporary(*device, {1u, retainedBytes, retainedBytes});
        survivor = temporary.prepare(desc, diagnostic);
        assert(survivor);
    }
    assert(survivor->validate().success);
    survivor.reset();
    for (const auto budget :
        {BindlessIndirectProcessorIntentBudget{0u, retainedBytes, retainedBytes},
            BindlessIndirectProcessorIntentBudget{1u, 0u, retainedBytes},
            BindlessIndirectProcessorIntentBudget{1u, retainedBytes, 0u},
            BindlessIndirectProcessorIntentBudget{1u, retainedBytes, retainedBytes - 1u}}) {
        BindlessIndirectProcessorIntentPool invalid(*device, budget);
        assert(!invalid.prepare(desc, diagnostic)
            && diagnostic == "RHI-BINDLESS-INDIRECT-PROCESSOR-BUDGET");
    }
    auto intent = pool.prepare(desc, diagnostic);
    assert(intent && intent->validate().success && intent->retainedBytes() == retainedBytes);
    {
        using Correspondence = ZeroBaseIndirectProcessorCorrespondence;
        using Content = ZeroBaseIndirectSourceConsumptionContract;
        std::array<std::uint8_t, 40u> logicalBytes{};
        assert(IndexedIndirectCommandSchema::encodeLittleEndian(
            IndexedIndirectCommandSchema::words({3u, 1u, 1u, -2, 1u}),
            logicalBytes.data() + 20u,
            20u));
        assert(device->writeBuffer(*source, logicalBytes.data(), logicalBytes.size()).success);
        arp::test::ZeroBaseIndirectSourceFixture fixture;
        const auto indices = fixture.indices();
        Content::Arguments::Source logical{logicalBytes.data(),
            logicalBytes.size(),
            20u,
            1u,
            20u,
            arp::core::UnsignedElementSpan::ByteOrder::LittleEndian,
            Content::Arguments::Origin::HostAuthoredImmutable,
            1u};
        Content::Budget limits{1u, true, 40u, 40u + Content::physicalSnapshotBytes};
        Content content(
            logical, packet->parameters(), artifact.indirectInstanceConsumer, limits, indices);
        assert(!content.diagnostic());
        Correspondence::Facts facts;
        auto expectedPackage = producer;
        std::shared_ptr<const GpuBuffer> expectedSource = source;
        const auto cleared = [&]() {
            assert(!facts.contractVersion && !facts.canonical.contractVersion
                && !facts.canonical.source.contractVersion
                && !facts.canonical.parameters.contractVersion && !facts.packetGeneration
                && !facts.snapshotGeneration);
        };
        const auto check = [&](const Content& selected, const char* id) {
            facts.contractVersion = 99u;
            facts.packetGeneration = 99u;
            facts.canonical.contractVersion = 99u;
            auto result =
                Correspondence::verify(*intent, expectedPackage, expectedSource, selected, facts);
            assert(!result.success && result.diagnostics == id);
            cleared();
        };
        check(content, "RHI-ZERO-BASE-SOURCE-WRITES-NOT-SEALED");
        source->sealWrites();
        assert(
            Correspondence::verify(*intent, expectedPackage, expectedSource, content, facts).success
            && facts.contractVersion == 1u && facts.packetGeneration == packet->generation()
            && facts.snapshotGeneration == snapshot->generation()
            && facts.canonical.source.mapping.firstInstance == 1u);
        expectedPackage = consumer;
        check(content, "RHI-ZERO-BASE-PROCESSOR-PACKAGE-OWNER-MISMATCH");
        expectedPackage = producer;
        expectedPackage = std::shared_ptr<const BindlessShaderPackage>(source, producer.get());
        check(content, "RHI-ZERO-BASE-PROCESSOR-PACKAGE-OWNER-MISMATCH");
        expectedPackage = producer;
        expectedSource = std::shared_ptr<const GpuBuffer>(producer, source.get());
        check(content, "RHI-ZERO-BASE-PROCESSOR-SOURCE-OWNER-MISMATCH");
        expectedSource = source;
        auto other = arp::core::shareUnique(device->createBuffer(source->desc()));
        assert(other);
        other->sealWrites();
        expectedSource = other;
        check(content, "RHI-ZERO-BASE-PROCESSOR-SOURCE-OWNER-MISMATCH");
        expectedSource = source;
        other.reset();
        auto wrongConsumer = artifact.indirectInstanceConsumer;
        ++wrongConsumer.binding;
        Content wrongMapping(logical, packet->parameters(), wrongConsumer, limits, indices);
        check(wrongMapping, "RHI-ZERO-BASE-SOURCE-CONSUMER-MAPPING");
        auto wrongParameters = packet->parameters();
        --wrongParameters.sourceWordOffset;
        auto changedLogical = logical;
        changedLogical.offsetBytes -= sizeof(std::uint32_t);
        Content wrongParams(
            changedLogical, wrongParameters, artifact.indirectInstanceConsumer, limits, indices);
        assert(!wrongParams.diagnostic());
        check(wrongParams, "RHI-ZERO-BASE-SOURCE-PARAMETERS");
        auto& mutableDesc = const_cast<BindlessIndirectProcessorIntentDesc&>(intent->desc());
        ++mutableDesc.consumerGeneration;
        check(content, "RHI-BINDLESS-INDIRECT-PACKET-PLAN-MISMATCH");
        --mutableDesc.consumerGeneration;
        const auto aliasMutation = [&](auto& field, auto alias) {
            const auto saved = field;
            field = alias;
            check(content, "RHI-BINDLESS-INDIRECT-PROCESSOR-OWNER-MISMATCH");
            field = saved;
        };
        aliasMutation(mutableDesc.packet,
            std::shared_ptr<const BindlessIndirectPacket>(source, packet.get()));
        aliasMutation(mutableDesc.consumerSnapshot,
            std::shared_ptr<const BindlessSnapshot>(source, snapshot.get()));
        aliasMutation(mutableDesc.consumerAuthorization,
            std::shared_ptr<const BindlessAuthorization>(source, authorization.get()));
        auto quota = arp::core::makeShared<arp::core::CpuMemoryQuota>(1u);
        const auto scopedAllocations = arp::core::defaultCpuAllocator().stats().allocationCount;
        {
            arp::core::CpuMemoryQuotaScope scope(quota);
            arp::core::CpuMemoryQuotaLease exhausted(quota, 1u);
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            for (unsigned i = 0u; i < 100u; ++i)
                assert(
                    Correspondence::verify(*intent, expectedPackage, expectedSource, content, facts)
                        .success);
            std::pmr::set_default_resource(previous);
        }
        assert(arp::core::defaultCpuAllocator().stats().allocationCount == scopedAllocations
            && !quota->liveBytes());
        std::cout
            << "ZeroBaseIndirectProcessorCorrespondence/1 actual package/Null intent: exact source/params/consumer/generation, aliases/seals/mismatches cleared, allocation0 PASS; no native byte/packet authority\n";
    }
    {
        class ValidatorProcessor final : public BindlessIndirectProcessor {
        public:
            explicit ValidatorProcessor(const BindlessIndirectProcessorIntent& value)
                : value_(value)
            {}

            std::uint64_t id = 1u;

            std::uint64_t identity() const noexcept override { return id; }

            const BindlessIndirectProcessorIntent& intent() const noexcept override
            {
                return value_;
            }

            ValidationResult validate() const override { return value_.validate(); }

            std::uint64_t retainedBytes() const noexcept override { return value_.retainedBytes(); }

        private:
            const BindlessIndirectProcessorIntent& value_;
        } validator(*intent);

        assert(validateBindlessIndirectProcessorConsumer(validator, *snapshot, *authorization)
                .success);
        auto otherAuthorization = extension->authorize(consumer, *snapshot, diagnostic);
        assert(otherAuthorization
            && validateBindlessIndirectProcessorConsumer(validator, *snapshot, *otherAuthorization)
                    .diagnostics
                == "RHI-BINDLESS-INDIRECT-PROCESSOR-CONSUMER-MISMATCH");
        BindlessSnapshot otherSnapshot;
        assert(validateBindlessIndirectProcessorConsumer(validator, otherSnapshot, *authorization)
                   .diagnostics
            == "RHI-BINDLESS-INDIRECT-PROCESSOR-CONSUMER-MISMATCH");
        validator.id = 0u;
        assert(!validateBindlessIndirectProcessorConsumer(validator, *snapshot, *authorization)
                .success);
    }
    assert(!pool.prepare(desc, diagnostic)
        && diagnostic == "RHI-BINDLESS-INDIRECT-PROCESSOR-POOL-FULL");
    const auto producerAccess = packet->producerAccesses(*source);
    std::array<BindlessResourceAccess, 5> consumerAccess{};
    consumerAccess[0] = {sampled->id(),
        BindlessAccessKind::Texture,
        ShaderStage::Fragment,
        PipelineStage::FragmentShader,
        ResourceAccess::ShaderRead,
        ResourceState::ShaderRead};
    consumerAccess[1] = {packet->instanceMappings()->id(),
        BindlessAccessKind::Buffer,
        ShaderStage::Vertex,
        PipelineStage::VertexShader,
        ResourceAccess::ShaderRead,
        ResourceState::ShaderRead,
        0u,
        sizeof(IndirectInstanceBase)};
    consumerAccess[2] = snapshot->metadataAccess();
    consumerAccess[3] = snapshot->faultLatchAccess();
    consumerAccess[4] = {packet->physicalArgs()->id(),
        BindlessAccessKind::Buffer,
        ShaderStage::None,
        PipelineStage::DrawIndirect,
        ResourceAccess::IndirectCommandRead,
        ResourceState::ShaderRead,
        0u,
        sizeof(DrawIndexedIndirectCommand)};
    const BindlessAccessArray producerDeclarations(producerAccess.data(), producerAccess.size());
    const BindlessAccessArray consumerDeclarations(consumerAccess.data(), consumerAccess.size());
    assert(intent->validateForRecord(producerDeclarations, consumerDeclarations).success);
    assert(!BindlessIndirectPacketRecordingLease::acquire(
               std::shared_ptr<const BindlessIndirectPacket>(source, packet.get()),
               snapshot,
               snapshot->plan(),
               snapshot->generation(),
               17u,
               diagnostic)
               .valid()
        && diagnostic == "RHI-BINDLESS-INDIRECT-PACKET-OWNER-MISMATCH");
    assert(!BindlessIndirectPacketRecordingLease::acquire(packet,
               std::shared_ptr<const BindlessSnapshot>(source, snapshot.get()),
               snapshot->plan(),
               snapshot->generation(),
               17u,
               diagnostic)
               .valid()
        && diagnostic == "RHI-BINDLESS-INDIRECT-PACKET-OWNER-MISMATCH");
    assert(!BindlessIndirectProcessorRecordingLease::acquire(
               std::shared_ptr<const BindlessIndirectProcessorIntent>(source, intent.get()),
               17u,
               producerDeclarations,
               consumerDeclarations,
               diagnostic)
               .valid()
        && diagnostic == "RHI-BINDLESS-INDIRECT-PROCESSOR-OWNER-MISMATCH");
    assert(!BindlessIndirectProcessorRecordingLease::acquire(
        intent, 0u, producerDeclarations, consumerDeclarations, diagnostic)
            .valid());
    assert(diagnostic == "RHI-BINDLESS-INDIRECT-PROCESSOR-RECORDING-INVALID");
    assert(!BindlessIndirectProcessorRecordingLease::acquire(
        intent, 17u, BindlessAccessArray(nullptr, 0u), consumerDeclarations, diagnostic)
            .valid());
    assert(diagnostic == "RHI-BINDLESS-INDIRECT-PRODUCER-ACCESS-INCOMPLETE");
    assert(!intent
            ->validateForRecord(
                producerDeclarations, BindlessAccessArray(consumerAccess.data(), 4u))
            .success);
    for (std::size_t i = 0u; i < consumerAccess.size(); ++i) {
        const auto saved = consumerAccess[i];
        consumerAccess[i].resource = {};
        assert(!intent->validateForRecord(producerDeclarations, consumerDeclarations).success);
        consumerAccess[i] = saved;
    }
    const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
    for (std::uint32_t i = 0u; i < 32u; ++i) {
        auto lease = BindlessIndirectProcessorRecordingLease::acquire(
            intent, 17u, producerDeclarations, consumerDeclarations, diagnostic);
        assert(lease.valid()
            && lease.validateForRecord(*intent, 17u, producerDeclarations, consumerDeclarations)
                .success);
    }
    assert(arp::core::defaultCpuAllocator().stats().allocationCount == allocations);
    auto lease = BindlessIndirectProcessorRecordingLease::acquire(
        intent, 17u, producerDeclarations, consumerDeclarations, diagnostic);
    assert(lease.valid());
    assert(!BindlessIndirectProcessorRecordingLease::acquire(
        intent, 18u, producerDeclarations, consumerDeclarations, diagnostic)
            .valid());
    assert(diagnostic == "RHI-BINDLESS-INDIRECT-PACKET-IN-FLIGHT");
    assert(
        !lease.validateForRecord(*intent, 18u, producerDeclarations, consumerDeclarations).success);
    auto moved = std::move(lease);
    assert(moved.valid() && !lease.valid());
    std::weak_ptr<const BindlessIndirectProcessorIntent> observed = intent;
    std::weak_ptr<const GpuBuffer> observedSource = source;
    intent.reset();
    source.reset();
    packet.reset();
    snapshot.reset();
    authorization.reset();
    desc = {};
    changed = {};
    assert(!observed.expired() && !observedSource.expired() && pool.liveIntents() == 1u);
    device.reset();
    assert(observed.lock()->validate().diagnostics == "RHI-BINDLESS-INDIRECT-PROCESSOR-STALE");
    moved = {};
    assert(observed.expired() && observedSource.expired() && pool.liveIntents() == 0u
        && pool.chargedBytes() == 0u);
}

void validatePreparedGraphicsScope(arp::rhi::GpuDevice& device,
    const arp::rhi::BindlessCrossExtension& extension,
    const std::shared_ptr<const arp::rhi::BindlessPreparedGraphicsBinding>& binding,
    const arp::rhi::RenderPipeline& alternate,
    const arp::rhi::RenderPassDesc& passDesc,
    const arp::rhi::BindlessAccessDeclarations& accesses)
{
    using namespace arp::rhi;
    using namespace arp::core;
    assert(extension.capabilities().preparedGraphicsPassFaultScopeContractVersion == 1u);
    const auto& pipeline = *binding->owners()->desc().pipeline;
    const auto accessCount = static_cast<std::uint32_t>(accesses.size());
    const auto retained = binding->owners()->desc().snapshot->plan().snapshotBytes
        + binding->authorization()->package().sourceBytes();
    BindlessGraphicsBindingPool independentPool(
        extension, {1u, retained + 1024u, retained + 1024u, 6u, 1024u});
    pmr::String diagnostic;
    auto independent = independentPool.prepare(binding->owners()->desc(), diagnostic);
    assert(independent);
    for (unsigned mode = 0u; mode < 4u; ++mode) {
        auto encoder = device.createCommandEncoder({"scope quota",
            mode == 1u ? 0u : 1u,
            mode == 2u ? 0u : 1u,
            0u,
            0u,
            mode == 0u       ? 0u
                : mode == 3u ? accessCount - 1u
                             : accessCount});
        assert(encoder);
        assert(!encoder->prepareBindlessGraphicsPass(binding, accesses).success);
        assert(encoder->finish());
    }
    auto foreign = createNullDevice();
    auto foreignEncoder =
        foreign->createCommandEncoder({"foreign scope", 1u, 1u, 0u, 0u, accessCount});
    assert(!foreignEncoder->prepareBindlessGraphicsPass(binding, accesses).success);
    auto encoder = device.createCommandEncoder({"graphics scope", 1u, 1u, 0u, 0u, accessCount});
    assert(encoder);
    assert(!encoder->prepareBindlessGraphicsPass({}, accesses).success);
    assert(
        !encoder->prepareBindlessGraphicsPass(binding, BindlessAccessArray(nullptr, 0u)).success);
    auto active = encoder->beginComputePass({"active preparation rejection"});
    assert(active);
    assert(!encoder->prepareBindlessGraphicsPass(binding, accesses).success);
    assert(active->end().success);
    const auto preparationAllocations = defaultCpuAllocator().stats().allocationCount;
    auto prepared = encoder->prepareBindlessGraphicsPass(binding, accesses);
    assert(prepared.success);
    assert(defaultCpuAllocator().stats().allocationCount == preparationAllocations);
    const auto count = prepared.recordedCommandCount;
    assert(!encoder->prepareBindlessGraphicsPass(binding, accesses).success);
    assert(!encoder->beginComputePass({"pending scope"}));
    assert(!encoder->beginTransferPass({"pending scope"}));
    assert(!encoder->finish());
    assert(encoder->lastValidation().recordedCommandCount == count);
    auto invalid = passDesc;
    if (invalid.colorAttachments.empty())
        invalid.depthStencilAttachment.format = TextureFormat::Unknown;
    else
        invalid.colorAttachments.front().format = TextureFormat::Unknown;
    assert(!encoder->beginRenderPass(invalid));
    auto pass = encoder->beginRenderPass(passDesc);
    assert(pass);
    auto* indexed =
        static_cast<BindlessPassExtension*>(pass->queryInterface(kExtensionBindlessCross));
    assert(indexed);
    assert(!pass->setPipeline(alternate).success);
    assert(pass->setPipeline(pipeline).success);
    assert(!indexed
            ->bindSnapshot(binding->owners()->desc().snapshot,
                binding->owners()->desc().plan,
                binding->owners()->desc().generation,
                accesses)
            .success);
    assert(indexed->bindPreparedGraphicsBinding(independent, accesses).diagnostics
        == "RHI-BINDLESS-GRAPHICS-PASS-SCOPE-MISMATCH");
    assert(!pass->end().success);
    assert(pass->setViewport({0, 0, 2, 2, 0, 1}).success);
    assert(pass->setScissor({0, 0, 2, 2}).success);
    assert(!pass->draw(3u, 1u, 0u, 0u).success);
    pmr::Vector<BindlessResourceAccess> changed;
    for (std::size_t i = 0u; i < accesses.size(); ++i) {
        BindlessResourceAccess access;
        assert(accesses.read(i, access));
        changed.push_back(access);
    }
    std::swap(changed.front(), changed.back());
    assert(!indexed
            ->bindPreparedGraphicsBinding(
                binding, BindlessAccessArray(changed.data(), changed.size()))
            .success);
    const auto allocations = defaultCpuAllocator().stats().allocationCount;
    for (unsigned i = 0u; i < 32u; ++i) {
        assert(indexed->bindPreparedGraphicsBinding(binding, accesses).success);
        assert(pass->draw(3u, 1u, 0u, 0u).success);
    }
    assert(defaultCpuAllocator().stats().allocationCount == allocations);
    assert(pass->end().success);
    auto commands = encoder->finish();
    assert(commands);
    assert(!encoder->prepareBindlessGraphicsPass(binding, accesses).success);
    auto* report = static_cast<BindlessCommandBufferExtension*>(
        commands->queryInterface(kExtensionBindlessCross));
    assert(report
        && report->faultReport(binding->owners()->desc().generation).status
            == BindlessFaultValidationStatus::NotExecuted);
    assert(device.queue().submit(*commands).success);
    commands.reset();
    pass.reset();
    encoder.reset();
    for (bool cancel : {true, false}) {
        encoder = device.createCommandEncoder({"scope ownership", 1u, 1u, 0u, 0u, accessCount});
        assert(encoder->prepareBindlessGraphicsPass(independent, accesses).success);
        std::weak_ptr<const BindlessPreparedGraphicsBinding> observed = independent;
        if (!cancel) {
            pass = encoder->beginRenderPass(passDesc);
            assert(pass);
            assert(pass->setPipeline(pipeline).success);
            indexed =
                static_cast<BindlessPassExtension*>(pass->queryInterface(kExtensionBindlessCross));
            assert(indexed && indexed->bindPreparedGraphicsBinding(independent, accesses).success);
            assert(pass->end().success);
            commands = encoder->finish();
            assert(commands);
            pass.reset();
        }
        independent.reset();
        assert(!observed.expired() && independentPool.liveBindings() == 1u);
        encoder.reset();
        if (!cancel) {
            assert(!observed.expired());
            commands.reset();
        }
        assert(observed.expired() && independentPool.liveBindings() == 0u);
        if (cancel) {
            independent = independentPool.prepare(binding->owners()->desc(), diagnostic);
            assert(independent);
        }
    }
}

void validatePreparedGraphicsBinding(arp::rhi::GpuDevice& device,
    const arp::rhi::BindlessCrossExtension& extension,
    std::shared_ptr<const arp::rhi::BindlessSnapshot> snapshot,
    std::shared_ptr<const arp::rhi::RenderPipeline> pipeline,
    const arp::rhi::RenderPassDesc& passDesc,
    const arp::rhi::BindlessAccessDeclarations& accesses)
{
    using namespace arp::rhi;
    using namespace arp::core;
    assert(extension.capabilities().preparedGraphicsBindingContractVersion == 1u);
    assert(pipeline->bindlessShaderStages() == (ShaderStage::Vertex | ShaderStage::Fragment));
    const auto retained =
        snapshot->plan().snapshotBytes + pipeline->bindlessAuthorization()->package().sourceBytes();
    BindlessGraphicsBindingBudget budget{1u, retained + 1024u, retained + 1024u, 6u, 1024u};
    BindlessGraphicsBindingDesc desc{
        true, snapshot, pipeline, snapshot->plan(), snapshot->generation()};
    BindlessGraphicsBindingPool pool(extension, budget);
    pmr::String diagnostic;
    auto changed = desc;
    changed.optIn = false;
    assert(!pool.prepare(changed, diagnostic) && diagnostic == "RHI-BINDLESS-OPT-IN-REQUIRED");
    changed = desc;
    ++changed.generation;
    assert(!pool.prepare(changed, diagnostic)
        && diagnostic == "RHI-BINDLESS-PREPARED-GRAPHICS-PLAN-MISMATCH");
    changed = desc;
    changed.pipeline.reset();
    assert(!pool.prepare(changed, diagnostic)
        && diagnostic == "RHI-BINDLESS-PREPARED-GRAPHICS-OWNERS-MISSING");
    changed = desc;
    changed.pipeline = std::shared_ptr<const RenderPipeline>(snapshot, pipeline.get());
    assert(!pool.prepare(changed, diagnostic)
        && diagnostic == "RHI-BINDLESS-PREPARED-GRAPHICS-OWNERS-MISMATCH");
    changed = desc;
    changed.snapshot = std::shared_ptr<const BindlessSnapshot>(pipeline, snapshot.get());
    assert(!pool.prepare(changed, diagnostic)
        && diagnostic == "RHI-BINDLESS-PREPARED-GRAPHICS-OWNERS-MISMATCH");
    assert(pool.liveBindings() == 0u && pool.chargedBytes() == 0u);
    RenderPipelineDesc aliasState;
    aliasState.colorFormat = passDesc.colorAttachments.front().format;
    aliasState.dynamicViewportScissor = true;
    const auto authorizationAlias = std::shared_ptr<const BindlessAuthorization>(
        snapshot, pipeline->bindlessAuthorization().get());
    assert(!extension.createRenderPipeline(
               authorizationAlias, aliasState, "vertexMain", "fragmentMain", diagnostic)
        && diagnostic == "RHI-BINDLESS-PIPELINE-AUTHORIZATION-INVALID");

    class StageOverridePipeline final : public RenderPipeline {
    public:
        StageOverridePipeline(std::shared_ptr<const RenderPipeline> original, ShaderStage stages)
            : original_(std::move(original)), stages_(stages)
        {}

        const PipelineLayout* layout() const override { return original_->layout(); }

        const PipelineValidationReport& validationReport() const override
        {
            return original_->validationReport();
        }

        const ShaderReflectionDesc& shaderReflection() const override
        {
            return original_->shaderReflection();
        }

        ShaderSourceLanguage shaderSourceLanguage() const override
        {
            return original_->shaderSourceLanguage();
        }

        bool backendFallbacksHiddenFromUpperLayer() const override { return true; }

        bool nativeHandleExposedToUpperLayer() const override { return false; }

        std::shared_ptr<const BindlessAuthorization> bindlessAuthorization() const noexcept override
        {
            return original_->bindlessAuthorization();
        }

        ShaderStage bindlessShaderStages() const noexcept override { return stages_; }

    private:
        std::shared_ptr<const RenderPipeline> original_;
        ShaderStage stages_;
    };

    for (auto stages :
        {ShaderStage::None, ShaderStage::Compute, ShaderStage::Vertex | ShaderStage::Fragment}) {
        changed = desc;
        changed.pipeline = makeShared<StageOverridePipeline>(pipeline, stages);
        assert(!pool.prepare(changed, diagnostic));
        assert(diagnostic
            == (stages == (ShaderStage::Vertex | ShaderStage::Fragment)
                    ? "RHI-BINDLESS-PREPARED-GRAPHICS-PAYLOAD-MISMATCH"
                    : "RHI-BINDLESS-PREPARED-GRAPHICS-STAGE-MISMATCH"));
        assert(pool.liveBindings() == 0u && pool.chargedBytes() == 0u);
    }
    auto foreignDevice = createNullDevice();
    auto* foreign = static_cast<BindlessCrossExtension*>(
        foreignDevice->queryInterface(kExtensionBindlessCross));
    BindlessGraphicsBindingPool foreignPool(*foreign, budget);
    assert(!foreignPool.prepare(desc, diagnostic)
        && diagnostic == "RHI-BINDLESS-PREPARED-GRAPHICS-AUTHORIZATION-INVALID");
    foreignDevice.reset();
    assert(!foreignPool.prepare(desc, diagnostic)
        && diagnostic == "RHI-BINDLESS-PREPARED-GRAPHICS-STALE");
    auto binding = pool.prepare(desc, diagnostic);
    assert(binding && binding->validateForRecord(*pipeline, accesses).success);
    assert(binding->owners()->stages() == pipeline->bindlessShaderStages());
    diagnostic.reserve(sizeof("RHI-METAL-BINDLESS-PREPARED-GRAPHICS-PAYLOAD-MISMATCH") - 1u);
    const auto allocationsBeforeForeignAdapter =
        arp::core::defaultCpuAllocator().stats().allocationCount;
    assert(!arp::rhi_metal::prepareMetalRhiGraphicsBinding(binding->owners(), budget, diagnostic)
        && diagnostic == "RHI-METAL-BINDLESS-PREPARED-GRAPHICS-PAYLOAD-MISMATCH");
    assert(arp::core::defaultCpuAllocator().stats().allocationCount
        == allocationsBeforeForeignAdapter);
    assert(!arp::rhi_metal::metalRhiPreparedGraphicsPayloadFor(*binding));
    {
        // Real Null Validator owners cannot enter a public Metal scope.
        auto metalEncoder =
            arp::rhi_metal::createMetalRhiCommandEncoder({"ForeignGraphicsIntent", 1u, 1u});
        assert(metalEncoder && metalEncoder->recordingIdentity());
        const auto count = metalEncoder->lastValidation().recordedCommandCount;
        const auto rejected = metalEncoder->prepareBindlessGraphicsPass(binding, accesses);
        assert(!rejected.success
            && rejected.diagnostics == "RHI-BINDLESS-GRAPHICS-PASS-SCOPE-UNAVAILABLE"
            && rejected.recordedCommandCount == count);
        const auto native = arp::rhi_metal::metalRhiNativePayloadFor(*metalEncoder);
        assert(native && !native->stats()->nativeBlitEncoderCreated
            && !native->stats()->nativeRenderEncoderCreated);
        assert(!native->prepareGraphicsPassScope(binding, accesses, diagnostic));
        assert(diagnostic
            == (native->stats()->nativeRuntimeBacked
                    ? "RHI-BINDLESS-PREPARED-GRAPHICS-PAYLOAD-MISMATCH"
                    : "RHI-METAL-BINDLESS-NATIVE-RUNTIME-UNAVAILABLE"));
        assert(!native->stats()->nativeBlitEncoderCreated
            && !native->stats()->nativeRenderEncoderCreated);
    }
    const auto foreignEntries = arp::rhi_metal::metalRhiSelectedGraphicsEntriesFor(*pipeline);
    assert(foreignEntries.vertex.empty() && foreignEntries.fragment.empty());
    assert(pool.liveBindings() == 1u && pool.chargedBytes() == budget.maxBindingBytes);
    assert(!pool.prepare(desc, diagnostic)
        && diagnostic == "RHI-BINDLESS-PREPARED-GRAPHICS-POOL-FULL");
    assert(!binding->validateForRecord(*pipeline, BindlessAccessArray(nullptr, 0u)).success);
    RenderPipelineDesc authored;
    authored.colorFormat = passDesc.colorAttachments.front().format;
    authored.dynamicViewportScissor = true;
    auto alternate = extension.createRenderPipeline(
        pipeline->bindlessAuthorization(), authored, "vertexMain", "fragmentMain", diagnostic);
    assert(alternate
        && binding->validateForRecord(*alternate, accesses).diagnostics
            == "RHI-BINDLESS-PREPARED-GRAPHICS-PIPELINE-MISMATCH");
    validatePreparedGraphicsScope(device, extension, binding, *alternate, passDesc, accesses);

    class ForgedBinding final : public BindlessPreparedGraphicsBinding {
    public:
        explicit ForgedBinding(std::shared_ptr<const BindlessGraphicsBindingOwners> owners)
            : BindlessPreparedGraphicsBinding(std::move(owners))
        {}

        ValidationResult validatePayload() const override { return {true, {}}; }

        std::uint32_t argumentBufferCount() const noexcept override { return 0u; }

        std::uint64_t argumentBufferBytes() const noexcept override { return 0u; }
    };

    auto encoder = device.createCommandEncoder({"prepared graphics", 2u, 1u});
    auto pass = encoder->beginRenderPass(passDesc);
    auto* indexed =
        static_cast<BindlessPassExtension*>(pass->queryInterface(kExtensionBindlessCross));
    assert(indexed && !indexed->bindPreparedGraphicsBinding(binding, accesses).success);
    assert(pass->setPipeline(*alternate).success);
    assert(!indexed->bindPreparedGraphicsBinding(binding, accesses).success);
    assert(pass->setPipeline(*pipeline).success);
    auto forged = makeShared<ForgedBinding>(binding->owners());
    assert(!arp::rhi_metal::metalRhiPreparedGraphicsPayloadFor(*forged));
    auto scopedEncoder = device.createCommandEncoder(
        {"forged graphics scope", 1u, 1u, 0u, 0u, static_cast<std::uint32_t>(accesses.size())});
    auto bindingAlias =
        std::shared_ptr<const BindlessPreparedGraphicsBinding>(snapshot, binding.get());
    assert(indexed->bindPreparedGraphicsBinding(bindingAlias, accesses).diagnostics
        == "RHI-BINDLESS-PREPARED-GRAPHICS-PAYLOAD-MISMATCH");
    assert(scopedEncoder->prepareBindlessGraphicsPass(bindingAlias, accesses).diagnostics
        == "RHI-BINDLESS-PREPARED-GRAPHICS-PAYLOAD-MISMATCH");
    auto snapshotAlias = std::shared_ptr<const BindlessSnapshot>(pipeline, snapshot.get());
    assert(indexed->bindSnapshot(snapshotAlias, snapshot->plan(), snapshot->generation(), accesses)
               .diagnostics
        == "RHI-BINDLESS-RECORD-SNAPSHOT-OWNER-MISMATCH");
    auto ownersAlias =
        std::shared_ptr<const BindlessGraphicsBindingOwners>(snapshot, binding->owners().get());
    auto aliasedOwnersBinding = makeShared<ForgedBinding>(ownersAlias);
    assert(aliasedOwnersBinding->validateOwners().diagnostics
        == "RHI-BINDLESS-PREPARED-GRAPHICS-OWNERS-MISMATCH");
    assert(aliasedOwnersBinding->validateForRecord(*pipeline, accesses).diagnostics
        == "RHI-BINDLESS-PREPARED-GRAPHICS-OWNERS-MISMATCH");
    aliasedOwnersBinding.reset();
    ownersAlias.reset();
    bindingAlias.reset();
    snapshotAlias.reset();
    assert(scopedEncoder->prepareBindlessGraphicsPass(forged, accesses).diagnostics
        == "RHI-BINDLESS-PREPARED-GRAPHICS-PAYLOAD-MISMATCH");
    scopedEncoder.reset();
    assert(indexed->bindPreparedGraphicsBinding(forged, accesses).diagnostics
        == "RHI-BINDLESS-PREPARED-GRAPHICS-PAYLOAD-MISMATCH");
    forged.reset();
    const auto allocations = defaultCpuAllocator().stats().allocationCount;
    for (unsigned i = 0u; i < 32u; ++i) {
        assert(pass->setPipeline(*pipeline).success);
        assert(pass->setViewport({0, 0, 2, 2, 0, 1}).success);
        assert(pass->setScissor({0, 0, 2, 2}).success);
        assert(indexed->bindPreparedGraphicsBinding(binding, accesses).success);
        assert(pass->draw(3u, 1u, 0u, 0u).success);
    }
    assert(defaultCpuAllocator().stats().allocationCount == allocations);
    assert(pass->end().success && !indexed->bindPreparedGraphicsBinding(binding, accesses).success);
    auto commands = encoder->finish();
    assert(commands);
    std::weak_ptr<const BindlessPreparedGraphicsBinding> observed = binding;
    binding.reset();
    pass.reset();
    encoder.reset();
    assert(!observed.expired());
    assert(device.queue().submit(*commands).success);
    auto* report = static_cast<BindlessCommandBufferExtension*>(
        commands->queryInterface(kExtensionBindlessCross));
    assert(report
        && report->faultReport(snapshot->generation()).status
            == BindlessFaultValidationStatus::NotExecuted);
    commands.reset();
    assert(observed.expired() && pool.liveBindings() == 0u && pool.chargedBytes() == 0u);

    binding = pool.prepare(desc, diagnostic);
    auto compute = shareUnique(extension.createComputePipeline(
        pipeline->bindlessAuthorization(), "computeMain", diagnostic));
    BindlessComputeBindingPool computePool(extension, budget);
    auto computeBinding =
        computePool.prepare({true, snapshot, compute, desc.plan, desc.generation}, diagnostic);
    assert(binding && computeBinding);
    for (bool computeFirst : {false, true}) {
        encoder = device.createCommandEncoder({"shared prepared command quota",
            2u,
            1u,
            0u,
            0u,
            static_cast<std::uint32_t>(accesses.size())});
        auto bindGraphics = [&] {
            const auto scoped = encoder->prepareBindlessGraphicsPass(binding, accesses);
            assert(scoped.success == !computeFirst);
            if (computeFirst)
                assert(scoped.diagnostics == "RHI-BINDLESS-GRAPHICS-PASS-SCOPE-COMMAND-BUDGET");
            auto render = encoder->beginRenderPass(passDesc);
            auto* extension = static_cast<BindlessPassExtension*>(
                render->queryInterface(kExtensionBindlessCross));
            assert(render->setPipeline(*pipeline).success);
            const auto result = extension->bindPreparedGraphicsBinding(binding, accesses);
            assert(result.success == !computeFirst);
            if (computeFirst)
                assert(result.diagnostics == "RHI-BINDLESS-PREPARED-GRAPHICS-COMMAND-BUDGET");
            assert(render->end().success);
        };
        auto bindCompute = [&] {
            auto dispatch = encoder->beginComputePass({"mixed"});
            auto* extension = static_cast<BindlessPassExtension*>(
                dispatch->queryInterface(kExtensionBindlessCross));
            assert(dispatch->setPipeline(*compute).success);
            const auto result = extension->bindPreparedComputeBinding(computeBinding, accesses);
            assert(result.success == computeFirst);
            if (!computeFirst)
                assert(result.diagnostics == "RHI-BINDLESS-PREPARED-COMPUTE-COMMAND-BUDGET");
            assert(dispatch->end().success);
        };
        if (computeFirst) {
            bindCompute();
            bindGraphics();
        } else {
            bindGraphics();
            bindCompute();
        }
    }
    std::shared_ptr<const BindlessPreparedGraphicsBinding> survivor;
    {
        BindlessGraphicsBindingPool temporary(extension, budget);
        survivor = temporary.prepare(desc, diagnostic);
        assert(survivor);
    }
    assert(survivor->validateForRecord(*pipeline, accesses).success);
    std::cout
        << "PreparedGraphicsBinding/1 Null exact stages/owners, quotas, retention and zero-allocation recording: PASS\n";
}

arp::core::UniquePtr<arp::rhi::CommandBuffer> validateGraphics(arp::rhi::GpuDevice& device,
    const arp::rhi::BindlessCrossExtension& extension,
    std::shared_ptr<const arp::rhi::BindlessAuthorization> authorization,
    std::shared_ptr<const arp::rhi::BindlessSnapshot> snapshot,
    const arp::rhi::BindlessResourceAccess* declarations,
    const arp::rhi::GpuBuffer& indirect)
{
    using namespace arp::rhi;
    arp::core::pmr::String diagnostic;
    RenderPipelineDesc desc;
    desc.colorFormat = TextureFormat::Rgba8Unorm;
    desc.dynamicViewportScissor = true;
    auto create = [&](const RenderPipelineDesc& state,
                      std::string_view vertex = "vertexMain",
                      std::string_view fragment = "fragmentMain") {
        return extension.createRenderPipeline(authorization, state, vertex, fragment, diagnostic);
    };
    assert(!extension.createRenderPipeline({}, desc, "vertexMain", "fragmentMain", diagnostic));
    assert(!create(desc, "computeMain"));
    assert(!create(desc, "vertexMain", "computeMain"));
    assert(!create(desc, "vertexMain", ""));
    auto bad = desc;
    bad.vertexShader.entryPoint = "vertexMain";
    assert(!create(bad) && diagnostic == "RHI-BINDLESS-PIPELINE-AUTHORED-INTERFACE");
    bad = desc;
    bad.topology = PrimitiveTopology::PatchList;
    assert(!create(bad) && diagnostic == "RHI-BINDLESS-PIPELINE-GRAPHICS-ABI-UNAVAILABLE");
    bad = desc;
    bad.hasViewport = true;
    assert(!create(bad));
    bad = desc;
    bad.dynamicViewportScissor = false;
    assert(!create(bad) && diagnostic == "RHI-BINDLESS-PIPELINE-VIEWPORT-SCISSOR-REQUIRED");
    bad.hasViewport = true;
    bad.viewport = {0, 0, 2, 2, 0, 1};
    bad.hasScissor = true;
    bad.scissor = {0, 0, 2, 2};
    assert(create(bad));
    bad = desc;
    bad.multisample.sampleCount = 4;
    assert(!create(bad) && diagnostic == "RHI-BINDLESS-PIPELINE-GRAPHICS-ABI-UNAVAILABLE");
    auto pipeline = arp::core::shareUnique(create(desc));
    if (!pipeline)
        std::cerr << diagnostic << '\n';
    assert(pipeline && pipeline->validationReport().success
        && pipeline->validationReport().entrypointCount == 2);
    assert(pipeline->validationReport().rasterStateValidated
        && pipeline->validationReport().viewportScissorStateValidated);
    auto second = create(desc);
    assert(second
        && pipeline->validationReport().pipelineCacheKey
            == second->validationReport().pipelineCacheKey);
    bad = desc;
    bad.cullMode = CullMode::Back;
    auto changed = create(bad);
    assert(changed
        && changed->validationReport().pipelineCacheKey
            != pipeline->validationReport().pipelineCacheKey);

    TextureDesc targetDesc;
    targetDesc.extent = {2, 2, 1};
    targetDesc.format = desc.colorFormat;
    targetDesc.usage = TextureUsage::RenderAttachment;
    auto target = device.createTexture(targetDesc);
    assert(target);
    RenderPassDesc passDesc;
    passDesc.colorAttachments.push_back({"color", target->id(), desc.colorFormat});
    validatePreparedGraphicsBinding(
        device, extension, snapshot, pipeline, passDesc, BindlessAccessArray(declarations, 7u));
    {
        auto state = desc;
        state.dynamicViewportScissor = false;
        state.hasViewport = true;
        state.viewport = {0, 0, 2, 2, 0, 1};
        state.hasScissor = true;
        state.scissor = {0, 0, 2, 2};
        auto fixed = create(state);
        auto encoder = device.createCommandEncoder({"static graphics", 1, 1});
        auto pass = encoder->beginRenderPass(passDesc);
        assert(fixed && pass->setPipeline(*fixed).success);
        auto* indexed =
            static_cast<BindlessPassExtension*>(pass->queryInterface(kExtensionBindlessCross));
        assert(indexed
                ->bindSnapshot(snapshot, snapshot->plan(), snapshot->generation(), declarations, 7)
                .success);
        assert(pass->setViewport(state.viewport).diagnostics
            == "RHI-BINDLESS-RECORD-DYNAMIC-STATE-NOT-ENABLED");
        assert(pass->setScissor(state.scissor).diagnostics
            == "RHI-BINDLESS-RECORD-DYNAMIC-STATE-NOT-ENABLED");
        assert(pass->draw(3, 1, 0, 0).success && pass->end().success);
        auto commands = encoder->finish();
        assert(commands && device.queue().submit(*commands).success);
    }
    auto foreign = createNullDevice();
    const auto* foreignExtension = static_cast<const BindlessCrossExtension*>(
        foreign->queryInterface(kExtensionBindlessCross));
    assert(!foreignExtension->createRenderPipeline(
        authorization, desc, "vertexMain", "fragmentMain", diagnostic));
    {
        auto encoder = foreign->createCommandEncoder({"foreign graphics", 1, 1});
        auto pass = encoder->beginRenderPass(passDesc);
        assert(pass && !pass->setPipeline(*pipeline).success);
    }
    {
        auto encoder = device.createCommandEncoder({"graphics disabled"});
        auto pass = encoder->beginRenderPass(passDesc);
        assert(pass->setPipeline(*pipeline).diagnostics == "RHI-BINDLESS-COMMAND-PIPELINE-BUDGET");
        assert(!pass->drawIndexedIndirect(indirect, 0, 1, 20).success);
    }
    {
        auto mismatch = passDesc;
        mismatch.colorAttachments[0].format = TextureFormat::R32Float;
        auto encoder = device.createCommandEncoder({"incompatible graphics", 1, 1});
        auto pass = encoder->beginRenderPass(mismatch);
        assert(
            pass->setPipeline(*pipeline).diagnostics == "RHI-BINDLESS-RENDER-PASS-FORMAT-MISMATCH");
    }
    auto encoder = device.createCommandEncoder({"graphics", 2, 1});
    auto pass = encoder->beginRenderPass(passDesc);
    auto* indexed =
        static_cast<BindlessPassExtension*>(pass->queryInterface(kExtensionBindlessCross));
    const auto& plan = snapshot->plan();
    const auto generation = snapshot->generation();
    auto bind = [&] {
        return indexed->bindSnapshot(snapshot, plan, generation, declarations, 7);
    };
    auto viewport = [&] {
        assert(pass->setViewport({0, 0, 2, 2, 0, 1}).success);
        assert(pass->setScissor({0, 0, 2, 2}).success);
    };
    assert(indexed && pass->setPipeline(*pipeline).success);
    assert(pass->nextSubpass().diagnostics == "RHI-BINDLESS-RENDER-PASS-ABI-UNAVAILABLE");
    assert(!pass->draw(3, 1, 0, 0).success);
    viewport();
    assert(pass->draw(3, 1, 0, 0).diagnostics == "RHI-BINDLESS-RECORD-SNAPSHOT-REQUIRED");
    assert(!indexed->bindSnapshot(snapshot, plan, generation + 1, declarations, 7).success);
    assert(!indexed->bindSnapshot(snapshot, plan, generation, declarations, 5).success);
    assert(bind().success && pass->draw(3, 1, 0, 0).success
        && pass->drawIndexed(3, 1, 0, 0, 0).success);
    assert(!pass->bindDescriptorSet(snapshot->setIndex(), *snapshot->descriptorSet()).success);
    assert(!pass->draw(3, 1, 0, 0).success);
    assert(bind().success);
    assert(pass->drawIndexedIndirect(indirect, 0, 1, 20).diagnostics
        == "RHI-BINDLESS-INDIRECT-ARGS-INVALID");
    assert(pass->drawIndexedIndirectCount(indirect, 0, indirect, 0, 1, 20).diagnostics
        == "RHI-BINDLESS-INDIRECT-CONTRACT-UNAVAILABLE");
    auto bundleEncoder = device.createRenderBundleEncoder({"bundle"});
    auto secondaryEncoder = device.createSecondaryCommandEncoder({"secondary"});
    assert(bundleEncoder && secondaryEncoder);
    auto bundle = bundleEncoder->finish();
    auto secondary = secondaryEncoder->finish();
    assert(bundle && secondary);
    assert(pass->executeRenderBundle(*bundle).diagnostics
        == "RHI-BINDLESS-BUNDLE-CONTRACT-UNAVAILABLE");
    assert(pass->executeSecondary(*secondary).diagnostics
        == "RHI-BINDLESS-SECONDARY-CONTRACT-UNAVAILABLE");
    const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
    for (unsigned i = 0; i < 64; ++i) {
        assert(pass->setPipeline(*pipeline).success);
        viewport();
        assert(bind().success && pass->draw(3, 1, 0, 0).success
            && pass->drawIndexed(3, 1, 0, 0, 0).success);
    }
    assert(arp::core::defaultCpuAllocator().stats().allocationCount == allocations);
    assert(pass->end().success);
    assert(!bind().success && !pass->draw(3, 1, 0, 0).success);

    desc.colorFormat = TextureFormat::Unknown;
    desc.depthStencil.format = TextureFormat::D32Float;
    desc.depthStencil.depthTestEnabled = true;
    desc.depthStencil.depthWriteEnabled = true;
    auto depth = arp::core::shareUnique(create(desc, "vertexMain", ""));
    assert(depth && depth->validationReport().depthOnlyStateValidated);
    assert(depth->validationReport().depthStencilFormat == TextureFormat::D32Float);
    targetDesc.format = TextureFormat::D32Float;
    targetDesc.usage = TextureUsage::RenderAttachment;
    auto depthTarget = device.createTexture(targetDesc);
    RenderPassDesc depthPass;
    depthPass.hasDepthStencilAttachment = true;
    depthPass.depthStencilAttachment = {"depth", depthTarget->id(), TextureFormat::D32Float};
    pass = encoder->beginRenderPass(depthPass);
    indexed = static_cast<BindlessPassExtension*>(pass->queryInterface(kExtensionBindlessCross));
    assert(pass->setPipeline(*depth).success);
    viewport();
    const auto depthRetained =
        snapshot->plan().snapshotBytes + authorization->package().sourceBytes();
    BindlessGraphicsBindingPool depthPool(
        extension, {1u, depthRetained + 1024u, depthRetained + 1024u, 6u, 1024u});
    auto depthBinding = depthPool.prepare({true, snapshot, depth, plan, generation}, diagnostic);
    assert(depthBinding && depthBinding->owners()->stages() == ShaderStage::Vertex);
    arp::core::pmr::Vector<BindlessResourceAccess> vertexAccesses;
    for (unsigned i = 0u; i < 7u; ++i)
        if ((declarations[i].stages & ShaderStage::Vertex) != ShaderStage::None)
            vertexAccesses.push_back(declarations[i]);
    assert(indexed
            ->bindPreparedGraphicsBinding(
                depthBinding, BindlessAccessArray(vertexAccesses.data(), vertexAccesses.size()))
            .success);
    assert(pass->draw(3, 1, 0, 0).success);
    assert(pass->setPipeline(*second).diagnostics == "RHI-BINDLESS-RENDER-PASS-FORMAT-MISMATCH");
    assert(!pass->drawIndexedIndirect(indirect, 0, 1, 20).success);
    assert(pass->setPipeline(*depth).success);
    viewport();
    assert(bind().success && pass->draw(3, 1, 0, 0).success && pass->end().success);
    auto commands = encoder->finish();
    assert(commands && device.queue().submit(*commands).success
        && !foreign->queue().submit(*commands).success);
    validatePreparedGraphicsScope(device,
        extension,
        depthBinding,
        *second,
        depthPass,
        BindlessAccessArray(vertexAccesses.data(), vertexAccesses.size()));

    auto indirectArgs = device.createBuffer({"bindless-indirect-validation",
        64u,
        ResourceUsage::StorageBuffer | ResourceUsage::IndirectBuffer,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderRead});
    assert(indirectArgs);
    BindlessResourceAccess withIndirect[8];
    std::copy_n(declarations, 7u, withIndirect);
    withIndirect[7].resource = indirectArgs->id();
    withIndirect[7].kind = BindlessAccessKind::Buffer;
    withIndirect[7].state = ResourceState::ShaderRead;
    withIndirect[7].pipelineStages = PipelineStage::DrawIndirect;
    withIndirect[7].access = ResourceAccess::IndirectCommandRead;
    withIndirect[7].bufferSize = sizeof(DrawIndexedIndirectCommand);
    auto indirectEncoder = device.createCommandEncoder({"bindless indirect validation", 1, 1, 1});
    auto indirectPass = indirectEncoder->beginRenderPass(passDesc);
    auto* indirectExtension =
        static_cast<BindlessPassExtension*>(indirectPass->queryInterface(kExtensionBindlessCross));
    assert(indirectPass->setPipeline(*pipeline).success);
    assert(indirectExtension->bindSnapshot(snapshot, plan, generation, withIndirect, 8u).success);
    withIndirect[7].resource = indirect.id();
    assert(
        indirectPass->drawIndexedIndirect(*indirectArgs, 0u, 1u, sizeof(DrawIndexedIndirectCommand))
            .diagnostics
        == "RHI-BINDLESS-INDIRECT-CONTRACT-UNAVAILABLE");
    assert(indirectPass->end().success && indirectEncoder->finish());

    auto limitedEncoder = device.createCommandEncoder({"bindless indirect budget", 1, 1});
    auto limitedPass = limitedEncoder->beginRenderPass(passDesc);
    auto* limitedExtension =
        static_cast<BindlessPassExtension*>(limitedPass->queryInterface(kExtensionBindlessCross));
    assert(limitedPass->setPipeline(*pipeline).success);
    assert(limitedExtension->bindSnapshot(snapshot, plan, generation, withIndirect, 8u).diagnostics
        == "RHI-BINDLESS-INDIRECT-ACCESS-BUDGET");
    assert(limitedPass->end().success && limitedEncoder->finish());
    return commands;
}

void validateCompiledSnapshot(
    std::shared_ptr<const arp::rhi::BindlessShaderPackage> package, bool partial)
{
    using namespace arp::rhi;
    const auto& compiled = package->artifact();
    auto device = createNullDevice();
    const auto* extension =
        static_cast<const BindlessCrossExtension*>(device->queryInterface(kExtensionBindlessCross));
    assert(extension && !extension->capabilities().executablePayload);
    auto artifact = compiled;
    // Explicit Null accounting input, not a claim about native allocation size.
    artifact.snapshotBytes = 4096;
    BindlessRequirements request;
    request.optIn = true;
    request.allowEmulated = true;
    request.deviceIdentity = extension->capabilities().deviceIdentity;
    request.shaderPackageIdentity = artifact.shaderPackageIdentity;
    request.budget = {1, 14, 4096, 32};
    for (const auto& table : artifact.tables) {
        const auto binding = std::find_if(artifact.physicalLayout.bindings.begin(),
            artifact.physicalLayout.bindings.end(),
            [&](const auto& b) {
                return b.binding == table.firstBinding;
            });
        assert(binding != artifact.physicalLayout.bindings.end());
        request.tables.push_back({table.tableId,
            *binding,
            partial ? 1u : table.capacity,
            table.operations,
            table.nonUniform,
            table.bufferLayout});
    }
    auto plan = extension->plan(request, artifact);
    assert(plan.success && !plan.executable && plan.execution == BindlessExecution::Validator);
    {
        auto wrongAccess = request;
        for (auto& table : wrongAccess.tables) {
            if (table.binding.type == DescriptorType::StorageTexture)
                table.binding.storageTextureAccess = StorageTextureAccess::WriteOnly;
        }
        assert(extension->plan(wrongAccess, artifact).diagnosticId
            == std::string_view("RHI-BINDLESS-STORAGE-ACCESS-MISMATCH"));
    }
    {
        auto invalid = artifact;
        invalid.snapshotMetadata.wordCount--;
        assert(!extension->plan(request, invalid).success);
        invalid = artifact;
        invalid.tables[0].metadataWordOffset++;
        assert(!extension->plan(request, invalid).success);
        invalid = artifact;
        invalid.snapshotMetadata.binding = invalid.tables[0].firstBinding;
        assert(!extension->plan(request, invalid).success);
        invalid = artifact;
        invalid.snapshotBytes = 159;
        assert(!extension->plan(request, invalid).success);
        auto limited = extension->capabilities();
        limited.maxStorageBufferBindingSize = 159;
        assert(!planBindless(limited, request, artifact).success);
        // A valid alternate internal binding still changes the plan identity.
        invalid = artifact;
        for (auto& b : invalid.physicalLayout.bindings)
            if (b.binding == invalid.snapshotMetadata.binding)
                b.binding = 14;
        invalid.snapshotMetadata.binding = 14;
        const auto moved = extension->plan(request, invalid);
        assert(moved.success && moved.identity != plan.identity);
    }
    assert(plan.bindingModel
        == (compiled.execution == BindlessExecution::Emulated
                ? BindlessBindingModel::ScalarExpansion
                : BindlessBindingModel::DescriptorArrays));
    // Even real compiled bytecode cannot make Null an executable variant.
    auto unresolved = request;
    unresolved.shaderPackageIdentity = 0;
    assert(selectBindlessVariant(
               BindlessPolicy::Required, extension->capabilities(), unresolved, &artifact, 1)
               .path
        == BindlessSelectionPath::Rejected);
    request.allowEmulated = false;
    assert(extension->plan(request, artifact).success
        == (compiled.execution != BindlessExecution::Emulated));
    request.allowEmulated = true;
    arp::core::pmr::String diagnostic;
    BindlessSnapshotPool pool(*device, request.budget);
    auto snapshot = pool.prepareShared(request, artifact, diagnostic);
    assert(snapshot && !snapshot->seal(*device).success);

    struct UnissuedExtension final : BindlessCrossExtension {
        BindlessCapabilities caps;

        const BindlessCapabilities& capabilities() const noexcept override { return caps; }
    } unissued;

    unissued.caps = extension->capabilities();
    assert(snapshot->reportedInternalBufferBytes()
        == std::uint64_t(artifact.snapshotMetadata.wordCount) * sizeof(std::uint32_t));
    assert(!extension->authorize(package, *snapshot, diagnostic));
    auto uniform = arp::core::shareUnique(device->createBuffer({"uniform",
        16,
        ResourceUsage::UniformBuffer,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderRead}));
    auto input = arp::core::shareUnique(device->createBuffer({"input",
        16,
        ResourceUsage::StorageBuffer,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderRead}));
    auto output = arp::core::shareUnique(device->createBuffer({"output",
        16,
        ResourceUsage::StorageBuffer,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderWrite}));
    auto fault = arp::core::shareUnique(device->createBuffer({"fault",
        16,
        ResourceUsage::StorageBuffer,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderWrite}));
    auto sampler = device->createSampler(SamplerDesc{});
    TextureDesc textureDesc;
    textureDesc.label = "sampled";
    textureDesc.extent = {2, 2, 1};
    textureDesc.format = TextureFormat::Rgba8Unorm;
    textureDesc.usage = TextureUsage::Sampled;
    auto texture = arp::core::shareUnique(device->createTexture(textureDesc));
    TextureViewDesc viewDesc;
    viewDesc.label = "sampled";
    viewDesc.format = textureDesc.format;
    viewDesc.usage = textureDesc.usage;
    auto view = device->createTextureView(*texture, viewDesc);
    textureDesc.label = "storage";
    textureDesc.format = TextureFormat::R32Float;
    textureDesc.usage = TextureUsage::Storage;
    auto storage = arp::core::shareUnique(device->createTexture(textureDesc));
    viewDesc.label = "storage";
    viewDesc.format = textureDesc.format;
    viewDesc.usage = textureDesc.usage;
    auto storageView = device->createTextureView(*storage, viewDesc);
    assert(uniform && input && output && fault && sampler && texture && view && storage
        && storageView);
    for (std::uint32_t i = 0; i < (partial ? 1u : 2u); ++i) {
        assert(snapshot->writeSampler(snapshot->handle(1, i), *sampler).diagnostics
            == "RHI-BINDLESS-WRITE-INVALID");
        assert(snapshot->writeTexture(snapshot->handle(1, i), *view, ResourceState::ShaderRead)
                .success);
        assert(snapshot->writeSampler(snapshot->handle(2, i), *sampler).success);
        assert(
            snapshot->writeTexture(snapshot->handle(3, i), *storageView, ResourceState::ShaderWrite)
                .success);
        assert(snapshot->writeBuffer(snapshot->handle(4, i), *uniform).success);
        assert(snapshot->writeBuffer(snapshot->handle(5, i), *input).success);
        assert(snapshot->writeBuffer(snapshot->handle(6, i), *output).success);
    }
    if (partial)
        for (const auto& table : artifact.tables) {
            assert(snapshot->activeCount(table.tableId) == 1u);
            assert(!snapshot->handle(table.tableId, 1).generation);
            assert(snapshot->initializeInactiveSlots(snapshot->handle(table.tableId, 0)).success);
        }
    assert(
        snapshot->writeOrdinaryBuffer(13, *input).diagnostics == "RHI-BINDLESS-METADATA-RESERVED");
    assert(snapshot->writeOrdinaryBuffer(12, *uniform).diagnostics
        == "RHI-BINDLESS-BUFFER-USAGE-MISMATCH");
    auto oversizedOrdinary = device->createBuffer({"oversized-ordinary",
        128u * 1024u * 1024u + 4u,
        ResourceUsage::StorageBuffer,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderWrite});
    assert(oversizedOrdinary);
    assert(snapshot->writeOrdinaryBuffer(12, *oversizedOrdinary).diagnostics
        == "RHI-BINDLESS-BUFFER-RANGE-INVALID");
    assert(snapshot->writeOrdinaryBuffer(12, *fault).success);
    assert(!snapshot->metadataAccess().resource.isValid());
    assert(!snapshot->faultLatchAccess().resource.isValid());
    assert(snapshot->seal(*device).success && snapshot->descriptorSet());
    assert(snapshot->metadataAccess().resource == snapshot->metadataBuffer()->id());
    assert(snapshot->metadataBuffer()
        && snapshot->metadataBuffer()->desc().memory == MemoryUsage::CpuToGpu);
    assert((snapshot->metadataBuffer()->desc().usage & ResourceUsage::CopyDst)
        == ResourceUsage::CopyDst);
    const auto& words = snapshot->metadataWords();
    assert(snapshot->metadataBuffer()->hostWritesSealed());
    assert(snapshot->metadataBuffer()->gpuWritesSealed());
    assert(snapshot->metadataBuffer()->gpuTransferWritesSealed());
    assert(!fault->gpuWritesSealed());
    assert(device->writeBuffer(*snapshot->metadataBuffer(), words.data(), words.size() * 4u)
               .diagnostics
        == "RHI-BUFFER-HOST-WRITES-SEALED");
    {
        auto encoder = device->createCommandEncoder({});
        auto transfer = encoder->beginTransferPass({});
        assert(transfer);
        BufferCopyDesc copy;
        copy.sourceBuffer = input->id();
        copy.destinationBuffer = snapshot->metadataBuffer()->id();
        copy.sizeBytes = sizeof(std::uint32_t);
        assert(transfer->copyBufferToBuffer(copy).diagnostics == "RHI-BUFFER-GPU-WRITES-SEALED");
        UploadBufferDesc upload;
        upload.sourceUploadBuffer = input->id();
        upload.destinationResource = snapshot->metadataBuffer()->id();
        upload.sizeBytes = sizeof(std::uint32_t);
        assert(transfer->uploadBuffer(upload).diagnostics == "RHI-BUFFER-GPU-WRITES-SEALED");
        copy.destinationBuffer = fault->id();
        assert(transfer->copyBufferToBuffer(copy).success);
    }
    {
        DescriptorArenaDesc arenaDesc;
        arenaDesc.maxSets = 2;
        arenaDesc.maxDescriptors = 2;
        auto arena = device->createDescriptorArena(arenaDesc);
        assert(arena);
        DescriptorSetDesc readOnly;
        readOnly.layout.name = "metadata-read-only";
        readOnly.layout.bindings.push_back(
            {0, DescriptorType::ReadOnlyStorageBuffer, ShaderStage::Compute, 1});
        auto readSet = arena->allocateDescriptorSet(readOnly);
        assert(readSet);
        DescriptorBindingWrite write;
        write.binding = 0;
        write.type = DescriptorType::ReadOnlyStorageBuffer;
        write.resource = snapshot->metadataBuffer()->id();
        write.resourceKind = DescriptorResourceKind::Buffer;
        assert(readSet->write(write).success);
        DescriptorSetDesc writable;
        writable.layout.name = "metadata-writable-alias";
        writable.layout.bindings.push_back(
            {0, DescriptorType::StorageBuffer, ShaderStage::Compute, 1});
        auto writeSet = arena->allocateDescriptorSet(writable);
        assert(writeSet);
        write.type = DescriptorType::StorageBuffer;
        write.resourceState = ResourceState::ShaderWrite;
        assert(writeSet->write(write).diagnostics == "RHI-BUFFER-GPU-WRITES-SEALED");
    }
    assert(words.size() == 40 && words[0] == 1 && words[1] == 6);
    assert((std::uint64_t(words[3]) << 32u | words[2]) == snapshot->generation());
    for (const auto& table : artifact.tables) {
        const auto offset = table.metadataWordOffset;
        assert(words[offset] == table.tableId && words[offset + 1] == (partial ? 1u : 2u));
        assert(words[offset + 2] == 2 && words[offset + 3] == 0);
        const auto elements = table.bufferLayout.elementStrideBytes
            ? 16u / table.bufferLayout.elementStrideBytes
            : 0u;
        assert(words[offset + 4] == elements);
        assert(words[offset + 5] == (partial ? 0u : elements));
    }
    assert(snapshot->seal(*device).success);
    auto wrongModel = plan;
    wrongModel.bindingModel = plan.bindingModel == BindlessBindingModel::ScalarExpansion
        ? BindlessBindingModel::DescriptorArrays
        : BindlessBindingModel::ScalarExpansion;
    assert(snapshot->validateForRecord(wrongModel, snapshot->generation(), nullptr, 0).diagnostics
        == "RHI-BINDLESS-RECORD-PLAN-MISMATCH");
    assert(snapshot->validateForRecord(plan, snapshot->generation(), nullptr, 0).diagnostics
        == "RHI-BINDLESS-ACCESS-SET-INCOMPLETE");
    auto escalated = plan;
    escalated.executable = true;
    assert(snapshot->validateForRecord(escalated, snapshot->generation(), nullptr, 0).diagnostics
        == "RHI-BINDLESS-RECORD-PLAN-MISMATCH");
    BindlessResourceAccess declarations[7];
    for (auto& access : declarations) {
        access.stages = ShaderStage::Compute;
        access.pipelineStages = PipelineStage::ComputeShader;
        access.state = ResourceState::ShaderRead;
        access.access = ResourceAccess::ShaderRead;
        access.bufferSize = 16;
    }
    declarations[0].resource = uniform->id();
    declarations[1].resource = input->id();
    declarations[1].stages = ShaderStage::Compute | ShaderStage::Vertex;
    declarations[1].pipelineStages = PipelineStage::ComputeShader | PipelineStage::VertexShader;
    declarations[2].resource = output->id();
    declarations[3].resource = fault->id();
    for (unsigned i : {2u, 3u, 5u}) {
        declarations[i].state = ResourceState::ShaderWrite;
        declarations[i].access = ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite;
    }
    declarations[4].resource = texture->id();
    declarations[4].kind = BindlessAccessKind::Texture;
    declarations[4].stages = ShaderStage::Fragment;
    declarations[4].pipelineStages = PipelineStage::FragmentShader;
    declarations[6] = snapshot->metadataAccess();
    declarations[5].resource = storage->id();
    declarations[5].kind = BindlessAccessKind::Texture;
    assert(snapshot->validateForRecord(plan, snapshot->generation(), declarations, 6).diagnostics
        == "RHI-BINDLESS-ACCESS-SET-INCOMPLETE");
    assert(snapshot->validateForRecord(plan, snapshot->generation(), declarations, 7).success);
    declarations[5].access = ResourceAccess::ShaderRead;
    assert(!snapshot->validateForRecord(plan, snapshot->generation(), declarations, 7).success);
    assert(!snapshot->writeBuffer(snapshot->handle(5, 0), *input).success);
    declarations[5].access = ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite;
    assert(!extension->authorize({}, *snapshot, diagnostic));
    assert(!unissued.authorize(package, *snapshot, diagnostic));
    assert(diagnostic == "RHI-BINDLESS-AUTHORIZATION-EXECUTION-UNAVAILABLE");
    auto authorization = extension->authorize(package, *snapshot, diagnostic);
    assert(authorization && authorization->current() && !authorization->plan().executable);
    arp::tests::validateGraphSnapshot(*device,
        *extension,
        authorization,
        snapshot,
        {uniform, input, output, fault},
        texture,
        storage);
    auto graphicsCommands =
        validateGraphics(*device, *extension, authorization, snapshot, declarations, *input);
    assert(!extension->createComputePipeline({}, "computeMain", diagnostic));
    assert(!extension->createComputePipeline(authorization, "fragmentMain", diagnostic));
    auto pipeline = arp::core::shareUnique(
        extension->createComputePipeline(authorization, "computeMain", diagnostic));
    if (!pipeline)
        std::cerr << diagnostic << '\n';
    assert(pipeline && pipeline->validationReport().success);
    assert(pipeline->bindlessAuthorization() == authorization);
    validatePreparedComputeBinding(
        *device, *extension, snapshot, pipeline, BindlessAccessArray(declarations, 7u));
    ShaderPackageDesc unapproved;
    unapproved.reflection = pipeline->shaderReflection();
    assert(validateShaderPackage(unapproved).diagnostics
        == "RHI-BINDLESS-PIPELINE-AUTHORIZATION-UNAVAILABLE");
    auto foreignDevice = createNullDevice();
    const auto* foreignExtension = static_cast<const BindlessCrossExtension*>(
        foreignDevice->queryInterface(kExtensionBindlessCross));
    assert(!foreignExtension->authorize(package, *snapshot, diagnostic));
    assert(!foreignExtension->createComputePipeline(authorization, "computeMain", diagnostic));
    {
        auto foreignEncoder = foreignDevice->createCommandEncoder({"foreign", 1, 1});
        auto foreignPass = foreignEncoder->beginComputePass({"foreign"});
        assert(foreignPass && !foreignPass->setPipeline(*pipeline).success);
    }
    {
        auto disabled = device->createCommandEncoder({"disabled"});
        auto pass = disabled->beginComputePass({"disabled"});
        assert(pass->setPipeline(*pipeline).diagnostics == "RHI-BINDLESS-COMMAND-PIPELINE-BUDGET");
    }
    {
        auto noSnapshots = device->createCommandEncoder({"no snapshots", 1, 0});
        auto pass = noSnapshots->beginComputePass({"no snapshots"});
        assert(pass->setPipeline(*pipeline).success);
        auto* indexed =
            static_cast<BindlessPassExtension*>(pass->queryInterface(kExtensionBindlessCross));
        assert(indexed
            && indexed->bindSnapshot(snapshot, plan, snapshot->generation(), declarations, 7)
                    .diagnostics
                == "RHI-BINDLESS-COMMAND-SNAPSHOT-BUDGET");
        assert(!pass->dispatch(1, 1, 1).success);
    }
    auto encoder = device->createCommandEncoder({"indexed", 1, 1});
    auto pass = encoder->beginComputePass({"indexed"});
    auto* indexed =
        static_cast<BindlessPassExtension*>(pass->queryInterface(kExtensionBindlessCross));
    assert(indexed && pass->setPipeline(*pipeline).success);
    assert(pass->dispatch(1, 1, 1).diagnostics == "RHI-BINDLESS-RECORD-SNAPSHOT-REQUIRED");
    assert(!pass->bindDescriptorSet(snapshot->setIndex(), *snapshot->descriptorSet()).success);
    assert(!indexed->bindSnapshot(snapshot, escalated, snapshot->generation(), declarations, 7)
            .success);
    assert(!indexed->bindSnapshot(snapshot, plan, snapshot->generation() + 1, declarations, 7)
            .success);
    assert(!indexed->bindSnapshot(snapshot, plan, snapshot->generation(), declarations, 5).success);
    assert(indexed->bindSnapshot(snapshot, plan, snapshot->generation(), declarations, 7).success);
    assert(pass->dispatch(1, 1, 1).success);
    const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
    for (unsigned iteration = 0; iteration < 64; ++iteration) {
        assert(pass->setPipeline(*pipeline).success);
        assert(
            indexed->bindSnapshot(snapshot, plan, snapshot->generation(), declarations, 7).success);
        assert(pass->dispatch(1, 1, 1).success);
    }
    assert(arp::core::defaultCpuAllocator().stats().allocationCount == allocations);
    // Duplicate retention fits the same explicit one-object budgets.
    assert(pass->setPipeline(*pipeline).success);
    assert(!pass->dispatch(1, 1, 1).success);
    assert(indexed->bindSnapshot(snapshot, plan, snapshot->generation(), declarations, 7).success);
    assert(pass->dispatch(1, 1, 1).success);
    assert(pass->dispatchIndirect(*input, 0).diagnostics
        == "RHI-BINDLESS-INDIRECT-CONTRACT-UNAVAILABLE");
    auto otherPipeline = extension->createComputePipeline(authorization, "computeMain", diagnostic);
    assert(otherPipeline
        && pass->setPipeline(*otherPipeline).diagnostics == "RHI-BINDLESS-COMMAND-PIPELINE-BUDGET");
    assert(pass->setPipeline(*pipeline).success);
    assert(indexed->bindSnapshot(snapshot, plan, snapshot->generation(), declarations, 7).success);
    assert(pass->dispatch(1, 1, 1).success && pass->end().success);
    auto commands = encoder->finish();
    assert(commands);
    auto* faultReports = static_cast<BindlessCommandBufferExtension*>(
        commands->queryInterface(kExtensionBindlessCross));
    assert(faultReports);
    const auto validatorReport = faultReports->faultReport(snapshot->generation());
    assert(validatorReport.status == BindlessFaultValidationStatus::NotExecuted);
    assert(validatorReport.validate().diagnostics == "RHI-BINDLESS-GPU-EXECUTION-NOT-PROVIDED");
    assert(faultReports->faultReport(snapshot->generation() + 1u).status
        == BindlessFaultValidationStatus::Unavailable);
    assert(!foreignDevice->queue().submit(*commands).success);
    auto metadataOwner = arp::core::makeUnique<arp::render_graph::FrameGraph>(*device);
    metadataOwner->importSnapshotMetadata(snapshot);
    snapshot.reset();
    pipeline.reset();
    assert(pool.liveSnapshots() == 1 && !pool.prepareShared(request, artifact, diagnostic));
    assert(device->queue().submit(*commands).success);
    // Null validation rejects a resource dying before a later submission. Native
    // factories remain closed until they retain actual resource storage.
    input.reset();
    assert(!device->queue().submit(*commands).success);
    assert(!device->queue().submit(*graphicsCommands).success);
    graphicsCommands.reset();
    commands.reset();
    assert(pool.liveSnapshots() == 1 && pool.chargedBytes() == 4096);
    metadataOwner.reset();
    assert(pool.liveSnapshots() == 0 && pool.chargedBytes() == 0);
    device.reset();
    assert(!authorization->current());
}

void validateMetalGraphicsStageLayouts(const arp::rhi::BindlessShaderPackage& package)
{
    using namespace arp::rhi;
    using namespace arp::rhi_metal;
    {
        RenderPipelineDesc desc;
        desc.vertexShader.stage = ShaderStage::Vertex;
        desc.vertexShader.entryPoint = "vertexMain";
        desc.colorFormat = TextureFormat::Unknown;
        desc.depthStencil.format = TextureFormat::D32Float;
        desc.depthStencil.depthTestEnabled = true;
        desc.depthStencil.depthWriteEnabled = true;
        PipelineValidationReport report;
        report.depthStencilFormat = TextureFormat::D32Float;
        ShaderStage stages = ShaderStage::Compute;
        assert(validateMetalRhiGraphicsStageSelection(desc, report, stages).success
            && stages == ShaderStage::Vertex);
        auto invalid = desc;
        invalid.fragmentShader.stage = ShaderStage::Fragment;
        assert(validateMetalRhiGraphicsStageSelection(invalid, report, stages).diagnostics
                == "RHI-METAL-GRAPHICS-FRAGMENT-ENTRY-INVALID"
            && stages == ShaderStage::None);
        invalid = desc;
        invalid.fragmentShader.modulePath = "declared-but-missing-entry";
        assert(validateMetalRhiGraphicsStageSelection(invalid, report, stages).diagnostics
            == "RHI-METAL-GRAPHICS-FRAGMENT-ENTRY-INVALID");
        auto mismatched = report;
        mismatched.depthStencilFormat = TextureFormat::Unknown;
        assert(validateMetalRhiGraphicsStageSelection(desc, mismatched, stages).diagnostics
            == "RHI-METAL-GRAPHICS-ATTACHMENT-REPORT-MISMATCH");
        desc.depthStencil.format = TextureFormat::Unknown;
        report.depthStencilFormat = TextureFormat::Unknown;
        assert(validateMetalRhiGraphicsStageSelection(desc, report, stages).diagnostics
            == "RHI-METAL-GRAPHICS-FRAGMENT-OMISSION-REQUIRES-DEPTH-ONLY");
        desc.colorFormat = TextureFormat::Rgba8Unorm;
        report.colorTargetCount = 1u;
        report.colorFormats.push_back(desc.colorFormat);
        assert(validateMetalRhiGraphicsStageSelection(desc, report, stages).diagnostics
            == "RHI-METAL-GRAPHICS-FRAGMENT-OMISSION-REQUIRES-DEPTH-ONLY");
        desc.fragmentShader.stage = ShaderStage::Fragment;
        desc.fragmentShader.entryPoint = "fragmentMain";
        assert(validateMetalRhiGraphicsStageSelection(desc, report, stages).success
            && stages == (ShaderStage::Vertex | ShaderStage::Fragment));
        report.colorFormats[0] = TextureFormat::Bgra8Unorm;
        assert(validateMetalRhiGraphicsStageSelection(desc, report, stages).diagnostics
            == "RHI-METAL-GRAPHICS-ATTACHMENT-REPORT-MISMATCH");
        desc.vertexShader.stage = ShaderStage::Compute;
        assert(validateMetalRhiGraphicsStageSelection(desc, report, stages).diagnostics
            == "RHI-METAL-GRAPHICS-VERTEX-ENTRY-INVALID");
    }
    MetalRhiPreparedGraphicsStageLayout vertex, fragment;
    const auto lowered =
        lowerMetalRhiPreparedGraphicsStage(package, "vertexMain", ShaderStage::Vertex, vertex);
    if (package.artifact().execution != BindlessExecution::Native) {
        assert(lowered.diagnostics == "RHI-METAL-PREPARED-GRAPHICS-MODE-UNSUPPORTED");
        assert(vertex.stage == ShaderStage::None && vertex.direct.empty() && vertex.arrays.empty());
        return;
    }
    assert(lowered.success);
    assert(
        lowerMetalRhiPreparedGraphicsStage(package, "fragmentMain", ShaderStage::Fragment, fragment)
            .success);
    assert(vertex.stage == ShaderStage::Vertex && fragment.stage == ShaderStage::Fragment);
    assert(vertex.entry == "vertexMain" && fragment.entry == "fragmentMain");
    assert(vertex.pushSlot == UINT32_MAX && fragment.pushSlot == UINT32_MAX);
    const auto covered = [&](const auto& layout) {
        for (const auto& logical : package.artifact().physicalLayout.bindings) {
            unsigned active = 0u, entries = 0u;
            for (const auto& map : layout.direct)
                if (map.set == package.artifact().set && map.binding == logical.binding) {
                    ++entries;
                    active += map.active;
                }
            for (const auto& map : layout.arrays)
                if (map.set == package.artifact().set && map.binding == logical.binding) {
                    ++entries;
                    active += map.active;
                }
            assert(entries == 1u && active <= 1u);
        }
    };
    covered(vertex);
    covered(fragment);
    // Shader entry mismatch rejects and clears any previous successful layout.
    assert(!lowerMetalRhiPreparedGraphicsStage(package, "computeMain", ShaderStage::Vertex, vertex)
            .success);
    assert(vertex.stage == ShaderStage::None && vertex.direct.empty() && vertex.arrays.empty());
    assert(lowerMetalRhiPreparedGraphicsStage(package, "vertexMain", ShaderStage::Compute, vertex)
               .diagnostics
        == "RHI-METAL-PREPARED-GRAPHICS-ENTRY-STAGE-MISMATCH");
}

class MetalGraphicsArgumentFixture final {
public:
    explicit MetalGraphicsArgumentFixture(const arp::rhi::BindlessShaderPackage& package)
    {
        using namespace arp::rhi;
        using namespace arp::rhi_metal;
        MetalRhiDeviceDesc desc;
        desc.label = "GraphicsArgumentPreparation";
        desc.capabilityContract = semantics::nativeBackendCapabilityContract(Backend::Metal);
        desc.capabilityContract.queriedFromNativeDevice = true;
        desc.capabilityContract.queriedExtendedFeatures = true;
        desc.uploadStagingPageSizeBytes = 4096u;
        desc.uploadStagingMaxPageCount = 1u;
        desc.memoryBudgetBytes = 8u * 1024u * 1024u;
        device_ = createMetalRhiDevice(desc);
        assert(device_);
        auto scalarOnlyDesc = desc;
        scalarOnlyDesc.enableArgumentBuffers = false;
        auto scalarOnly = createMetalRhiDevice(scalarOnlyDesc);
        assert(scalarOnly);
        auto scalarSampler = scalarOnly->createSampler(SamplerDesc{});
        assert(scalarSampler);
        assert(!metalRhiNativePayloadFor(*scalarSampler)->argumentBufferCompatible);
        const auto* extension =
            static_cast<BindlessCrossExtension*>(device_->queryInterface(kExtensionBindlessCross));
        assert(extension && extension->capabilities().preparedGraphicsBindingContractVersion == 0u);
        arp::core::pmr::Vector<MetalRhiNativeDescriptorBindingInput> inputs;
        for (const auto& binding : package.artifact().physicalLayout.bindings) {
            for (unsigned element = 0u; element < binding.arrayCount; ++element) {
                MetalRhiNativeDescriptorBindingInput input;
                input.binding = binding;
                input.arrayElement = element;
                input.write.binding = binding.binding;
                input.write.type = binding.type;
                input.write.arrayElement = element;
                switch (binding.type) {
                case DescriptorType::UniformBuffer:
                case DescriptorType::ReadOnlyStorageBuffer:
                case DescriptorType::StorageBuffer: {
                    // No shader executes in this preparation fixture. The
                    // fixed test allocation supplies real native ownership.
                    auto buffer = device_->createBuffer({"ArgumentBufferResource",
                        256u,
                        binding.type == DescriptorType::UniformBuffer
                            ? ResourceUsage::UniformBuffer
                            : ResourceUsage::StorageBuffer,
                        MemoryUsage::CpuToGpu,
                        ResourceState::ShaderRead});
                    assert(buffer);
                    input.buffer = metalRhiNativePayloadFor(*buffer);
                    input.write.resourceKind = DescriptorResourceKind::Buffer;
                    input.write.resource = buffer->id();
                    buffers_.push_back(std::move(buffer));
                    break;
                }
                case DescriptorType::SampledTexture:
                case DescriptorType::StorageTexture: {
                    const auto usage = binding.type == DescriptorType::SampledTexture
                        ? TextureUsage::Sampled
                        : TextureUsage::Storage;
                    const auto format = binding.type == DescriptorType::StorageTexture
                        ? binding.storageTextureFormat
                        : TextureFormat::Rgba8Unorm;
                    assert(format != TextureFormat::Unknown);
                    auto texture = device_->createTexture({"ArgumentTextureResource",
                        {1u, 1u, 1u},
                        format,
                        usage,
                        MemoryUsage::GpuOnly,
                        ResourceState::ShaderRead});
                    assert(texture);
                    auto view = device_->createTextureView(
                        *texture, {"ArgumentTextureView", format, usage});
                    assert(view);
                    input.textureView = metalRhiNativePayloadFor(*view);
                    input.write.resourceKind = DescriptorResourceKind::TextureView;
                    input.write.resource = view->id();
                    textures_.push_back(std::move(texture));
                    views_.push_back(std::move(view));
                    break;
                }
                case DescriptorType::Sampler: {
                    SamplerDesc samplerDesc;
                    samplerDesc.label = "ArgumentSamplerResource";
                    samplerDesc.minFilter = SamplerFilter::Nearest;
                    samplerDesc.magFilter = SamplerFilter::Nearest;
                    samplerDesc.mipFilter = SamplerMipFilter::None;
                    auto sampler = device_->createSampler(samplerDesc);
                    assert(sampler);
                    input.sampler = metalRhiNativePayloadFor(*sampler);
                    assert(input.sampler && input.sampler->argumentBufferCompatible);
                    input.write.resourceKind = DescriptorResourceKind::Sampler;
                    input.write.resource = sampler->id();
                    samplers_.push_back(std::move(sampler));
                    break;
                }
                default:
                    assert(false);
                }
                inputs.push_back(std::move(input));
            }
        }
        descriptors_ =
            createMetalRhiNativeDescriptorGroupPayload(package.artifact().physicalLayout, inputs);
        assert(descriptors_ && descriptors_->nativeRuntimeBacked);
    }

    void validateDepth(
        const std::shared_ptr<arp::rhi_metal::MetalRhiNativePipelinePayload>& pipeline,
        const arp::rhi_metal::MetalRhiPreparedGraphicsStageLayout& vertex,
        std::uint32_t set)
    {
        using namespace arp::rhi;
        using namespace arp::rhi_metal;
        std::array<std::shared_ptr<const MetalRhiPreparedGraphicsStageLayout>, 2> layouts{
            arp::core::makeShared<MetalRhiPreparedGraphicsStageLayout>(vertex), nullptr};
        BindlessGraphicsBindingBudget budget{1u, 65536u, 65536u, 16u, 65536u};
        arp::core::pmr::String diagnostic;
        auto prepared = prepareMetalRhiNativeGraphicsBindings(
            pipeline, descriptors_, set, layouts, budget, diagnostic);
        if (!prepared)
            std::cerr << diagnostic << '\n';
        assert(prepared && prepared->stages[0] && !prepared->stages[1] && !prepared->layouts[1]);
        assert(prepared->argumentCount == prepared->stages[0]->arrays.size()
            && prepared->argumentBytes == prepared->stages[0]->argumentBytes);
        auto unexpected = layouts;
        unexpected[1] = arp::core::makeShared<MetalRhiPreparedGraphicsStageLayout>();
        assert(!prepareMetalRhiNativeGraphicsBindings(
            pipeline, descriptors_, set, unexpected, budget, diagnostic));
        assert(diagnostic == "RHI-METAL-PREPARED-GRAPHICS-ENTRY-STAGE-MISMATCH");
        unexpected = layouts;
        unexpected[0].reset();
        assert(!prepareMetalRhiNativeGraphicsBindings(
            pipeline, descriptors_, set, unexpected, budget, diagnostic));
        assert(diagnostic == "RHI-METAL-PREPARED-GRAPHICS-ENTRY-STAGE-MISMATCH");
        auto exact = budget;
        exact.maxArgumentBuffers = prepared->argumentCount;
        exact.maxArgumentBufferBytes = prepared->argumentBytes;
        assert(prepareMetalRhiNativeGraphicsBindings(
            pipeline, descriptors_, set, layouts, exact, diagnostic));
        validateRecording(pipeline, layouts, set);
        std::cout << "depthArgumentBuffers: " << prepared->argumentCount
                  << "\ndepthArgumentBytes: " << prepared->argumentBytes << '\n';
    }

    void validate(std::shared_ptr<arp::rhi_metal::MetalRhiNativePipelinePayload> pipeline,
        const arp::rhi_metal::MetalRhiPreparedGraphicsStageLayout& vertex,
        const arp::rhi_metal::MetalRhiPreparedGraphicsStageLayout& fragment,
        std::uint32_t set)
    {
        using namespace arp::rhi;
        using namespace arp::rhi_metal;
        std::array<std::shared_ptr<const MetalRhiPreparedGraphicsStageLayout>, 2> layouts{
            arp::core::makeShared<MetalRhiPreparedGraphicsStageLayout>(vertex),
            arp::core::makeShared<MetalRhiPreparedGraphicsStageLayout>(fragment)};
        BindlessGraphicsBindingBudget budget{1u, 65536u, 65536u, 16u, 65536u};
        arp::core::pmr::String diagnostic;
        diagnostic.reserve(256u);
        auto prepared = prepareMetalRhiNativeGraphicsBindings(
            pipeline, descriptors_, set, layouts, budget, diagnostic);
        if (!prepared)
            std::cerr << diagnostic << '\n';
        assert(prepared && prepared->pipeline == pipeline && prepared->descriptors != descriptors_);
        assert(prepared->descriptors->bindings.size() == descriptors_->bindings.size());
        assert(prepared->descriptors->bindings.get_allocator().resource()
            == &arp::core::defaultCpuMemoryResource());
        assert(prepared->descriptors->diagnostics.get_allocator().resource()
            == &arp::core::defaultCpuMemoryResource());
        assert(prepared->stages[0]->stage == ShaderStage::Vertex
            && prepared->stages[1]->stage == ShaderStage::Fragment);
        assert(prepared->stages[0]->function == pipeline->vertexFunction
            && prepared->stages[1]->function == pipeline->fragmentFunction);
        assert(prepared->argumentCount
            == prepared->stages[0]->arrays.size() + prepared->stages[1]->arrays.size());
        assert(prepared->argumentBytes
            == prepared->stages[0]->argumentBytes + prepared->stages[1]->argumentBytes);
        assert(prepared->stages[0]->arrays.size() && prepared->stages[1]->arrays.size()
            && prepared->argumentBytes > 0u);
        assert(prepared->encodedArgumentBytes > 0u
            && prepared->encodedArgumentBytes <= prepared->argumentBytes);
        for (const auto& v : prepared->stages[0]->arrays)
            for (const auto& f : prepared->stages[1]->arrays)
                assert(v.buffer && f.buffer && v.buffer != f.buffer);
        auto exact = budget;
        exact.maxArgumentBuffers = prepared->argumentCount;
        exact.maxArgumentBufferBytes = prepared->argumentBytes;
        assert(prepareMetalRhiNativeGraphicsBindings(
            pipeline, descriptors_, set, layouts, exact, diagnostic));
        auto tooFew = exact;
        --tooFew.maxArgumentBuffers;
        const auto ownersBeforeFailure = descriptors_.use_count();
        const auto liveBytesBeforeFailure = arp::core::defaultCpuAllocator().stats().liveBytes;
        assert(!prepareMetalRhiNativeGraphicsBindings(
            pipeline, descriptors_, set, layouts, tooFew, diagnostic));
        assert(diagnostic == "RHI-METAL-PREPARED-GRAPHICS-ARGUMENT-COUNT-BUDGET");
        assert(descriptors_.use_count() == ownersBeforeFailure
            && arp::core::defaultCpuAllocator().stats().liveBytes == liveBytesBeforeFailure);
        auto tooSmall = exact;
        --tooSmall.maxArgumentBufferBytes;
        assert(!prepareMetalRhiNativeGraphicsBindings(
            pipeline, descriptors_, set, layouts, tooSmall, diagnostic));
        assert(diagnostic == "RHI-METAL-PREPARED-GRAPHICS-ARGUMENT-BYTE-BUDGET");
        assert(descriptors_.use_count() == ownersBeforeFailure
            && arp::core::defaultCpuAllocator().stats().liveBytes == liveBytesBeforeFailure);
        auto encodedLimit = exact;
        encodedLimit.maxArgumentBufferBytes = prepared->encodedArgumentBytes - 1u;
        assert(!prepareMetalRhiNativeGraphicsBindings(
            pipeline, descriptors_, set, layouts, encodedLimit, diagnostic));
        assert(diagnostic == "RHI-METAL-PREPARED-GRAPHICS-ARGUMENT-BYTE-BUDGET");
        assert(descriptors_.use_count() == ownersBeforeFailure
            && arp::core::defaultCpuAllocator().stats().liveBytes == liveBytesBeforeFailure);
        auto zero = budget;
        zero.maxArgumentBuffers = 0u;
        assert(!prepareMetalRhiNativeGraphicsBindings(
            pipeline, descriptors_, set, layouts, zero, diagnostic));
        assert(diagnostic == "RHI-BINDLESS-PREPARED-GRAPHICS-BUDGET");
        auto wrongEntry = arp::core::makeShared<MetalRhiPreparedGraphicsStageLayout>(fragment);
        wrongEntry->entry = "vertexMain";
        auto changedLayouts = layouts;
        changedLayouts[1] = wrongEntry;
        assert(!prepareMetalRhiNativeGraphicsBindings(
            pipeline, descriptors_, set, changedLayouts, budget, diagnostic));
        assert(diagnostic == "RHI-METAL-PREPARED-GRAPHICS-FUNCTION-STAGE-ENTRY-MISMATCH");
        changedLayouts = layouts;
        changedLayouts[1].reset();
        assert(!prepareMetalRhiNativeGraphicsBindings(
            pipeline, descriptors_, set, changedLayouts, budget, diagnostic));
        assert(diagnostic == "RHI-METAL-PREPARED-GRAPHICS-ENTRY-STAGE-MISMATCH");
        const auto savedFunction = pipeline->fragmentFunction;
        pipeline->fragmentFunction = pipeline->vertexFunction;
        assert(!prepareMetalRhiNativeGraphicsBindings(
            pipeline, descriptors_, set, layouts, budget, diagnostic));
        assert(diagnostic == "RHI-METAL-PREPARED-GRAPHICS-FUNCTION-STAGE-ENTRY-MISMATCH");
        pipeline->fragmentFunction = savedFunction;
        auto missing = arp::core::makeShared<MetalRhiNativeDescriptorGroupPayload>();
        *missing = *descriptors_;
        missing->bindings.clear();
        assert(!prepareMetalRhiNativeGraphicsBindings(
            pipeline, missing, set, layouts, budget, diagnostic));
        assert(diagnostic == "RHI-METAL-PREPARED-GRAPHICS-ELEMENT-MISSING");
        auto foreign = arp::core::makeShared<MetalRhiNativeDescriptorGroupPayload>();
        *foreign = *descriptors_;
        foreign->nativeDevice = arp::core::makeShared<std::uint32_t>(0u);
        assert(!prepareMetalRhiNativeGraphicsBindings(
            pipeline, foreign, set, layouts, budget, diagnostic));
        assert(diagnostic == "RHI-METAL-PREPARED-GRAPHICS-PAYLOAD-MISSING");
        auto incompatible = arp::core::makeShared<MetalRhiNativeDescriptorGroupPayload>();
        *incompatible = *descriptors_;
        bool foundSampler = false;
        for (auto& binding : incompatible->bindings) {
            if (binding.argumentKind != MetalRhiArgumentKind::Sampler)
                continue;
            SamplerDesc samplerDesc;
            samplerDesc.minFilter = SamplerFilter::Nearest;
            samplerDesc.magFilter = SamplerFilter::Nearest;
            samplerDesc.mipFilter = SamplerMipFilter::None;
            binding.sampler = createMetalRhiNativeSamplerPayload(samplerDesc, false);
            assert(binding.sampler && binding.sampler->resource.nativeRuntimeBacked
                && !binding.sampler->argumentBufferCompatible);
            foundSampler = true;
            break;
        }
        if (foundSampler) {
            assert(!prepareMetalRhiNativeGraphicsBindings(
                pipeline, incompatible, set, layouts, budget, diagnostic));
            assert(
                diagnostic == "RHI-METAL-PREPARED-GRAPHICS-SAMPLER-ARGUMENT-BUFFER-INCOMPATIBLE");
        }
        incompatible.reset();
        assert(prepareMetalRhiNativeGraphicsBindings(
            pipeline, descriptors_, set, layouts, budget, diagnostic));
        validateRecording(pipeline, layouts, set);
        {
            const auto savedBindings = descriptors_->bindings;
            descriptors_->bindings.clear();
            assert(prepared->descriptors->bindings.size() == savedBindings.size());
            descriptors_->bindings = savedBindings;
        }
        std::weak_ptr<const MetalRhiNativeDescriptorGroupPayload> observed = prepared->descriptors;
        std::weak_ptr<const MetalRhiPreparedGraphicsStageLayout> observedLayout =
            prepared->layouts[0];
        layouts = {};
        changedLayouts = {};
        wrongEntry.reset();
        missing.reset();
        foreign.reset();
        descriptors_.reset();
        views_.clear();
        textures_.clear();
        buffers_.clear();
        samplers_.clear();
        assert(!observed.expired() && !observedLayout.expired()
            && device_->memoryStats().allocatedBytes > 0u);
        std::cout << "graphicsArgumentBuffers: " << prepared->argumentCount
                  << "\ngraphicsArgumentBytes: " << prepared->argumentBytes
                  << "\ngraphicsEncodedArgumentBytes: " << prepared->encodedArgumentBytes << '\n';
        prepared.reset();
        assert(observed.expired() && observedLayout.expired()
            && device_->memoryStats().allocatedBytes == 0u);
    }

private:
    void validateRecording(
        const std::shared_ptr<arp::rhi_metal::MetalRhiNativePipelinePayload>& pipeline,
        const std::array<std::shared_ptr<const arp::rhi_metal::MetalRhiPreparedGraphicsStageLayout>,
            2>& layouts,
        std::uint32_t set)
    {
        using namespace arp::rhi;
        using namespace arp::rhi_metal;
        arp::core::pmr::String diagnostic;
        diagnostic.reserve(256u);
        BindlessGraphicsBindingBudget budget{1u, 65536u, 65536u, 16u, 65536u};
        const bool depth = pipeline->depthOnlyRenderPipeline;
        const auto format = depth ? pipeline->depthStencilFormat : TextureFormat::Rgba8Unorm;
        auto target = device_->createTexture({"PreparedGraphicsRecordingAttachment",
            {2u, 2u, 1u},
            format,
            TextureUsage::RenderAttachment,
            MemoryUsage::GpuOnly,
            depth ? ResourceState::DepthAttachment : ResourceState::ColorAttachment});
        assert(target);
        RenderPassDesc desc;
        if (depth) {
            desc.hasDepthStencilAttachment = true;
            desc.depthStencilAttachment.target = target->id();
            desc.depthStencilAttachment.format = format;
            desc.depthStencilAttachment.depthLoadOp = LoadOp::Clear;
            desc.depthStencilAttachment.depthStoreOp = StoreOp::Store;
        } else {
            RenderPassColorAttachmentDesc color;
            color.label = "PreparedGraphicsRecordingColor";
            color.target = target->id();
            color.format = format;
            color.loadOp = LoadOp::Clear;
            color.storeOp = StoreOp::Store;
            color.clearColor = {0.25f, 0.5f, 0.75f, 1.0f};
            desc.colorAttachments.push_back(color);
        }
        for (unsigned mode = 0u; mode < 4u; ++mode) {
            auto prepared = prepareMetalRhiNativeGraphicsBindings(
                pipeline, descriptors_, set, layouts, budget, diagnostic);
            assert(prepared);
            auto encoder = device_->createCommandEncoder(
                {"PreparedGraphicsRecording", 1u, mode == 0u ? 0u : 1u});
            assert(encoder);
            auto native = metalRhiNativePayloadFor(*encoder);
            assert(native);
            assert(!native->recordPreparedGraphicsBinding(prepared, diagnostic)
                && diagnostic == "RHI-METAL-PREPARED-GRAPHICS-RECORDING-MISMATCH");
            auto pass = encoder->beginRenderPass(desc);
            assert(pass);
            assert(!native->recordPreparedGraphicsBinding(prepared, diagnostic));
            assert(native->recordGraphicsPipelineBind(pipeline, diagnostic));
            const auto allocationCount = native->stats()->nativeArgumentBufferAllocationCount;
            const auto reuseCount = native->stats()->nativeArgumentBufferReuseCount;
            if (mode == 0u) {
                assert(!native->recordPreparedGraphicsBinding(prepared, diagnostic)
                    && diagnostic == "RHI-BINDLESS-PREPARED-GRAPHICS-COMMAND-BUDGET");
                assert(native->stats()->nativeArgumentBufferReuseCount == reuseCount);
                assert(pass->end().success);
                encoder.reset();
                continue;
            }
            const auto savedFunction =
                depth ? pipeline->vertexFunction : pipeline->fragmentFunction;
            if (depth)
                pipeline->vertexFunction.reset();
            else
                pipeline->fragmentFunction = pipeline->vertexFunction;
            assert(!native->recordPreparedGraphicsBinding(prepared, diagnostic)
                && diagnostic == "RHI-METAL-PREPARED-GRAPHICS-FUNCTION-STAGE-ENTRY-MISMATCH");
            assert(native->stats()->nativeArgumentBufferReuseCount == reuseCount);
            if (depth)
                pipeline->vertexFunction = savedFunction;
            else
                pipeline->fragmentFunction = savedFunction;
            const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
            for (unsigned repeat = 0u; repeat < 32u; ++repeat)
                assert(native->recordPreparedGraphicsBinding(prepared, diagnostic));
            assert(arp::core::defaultCpuAllocator().stats().allocationCount == before);
            assert(native->stats()->nativeArgumentBufferAllocationCount == allocationCount);
            assert(native->stats()->nativeArgumentBufferReuseCount
                == reuseCount + 32u * prepared->argumentCount);
            auto extra = prepareMetalRhiNativeGraphicsBindings(
                pipeline, descriptors_, set, layouts, budget, diagnostic);
            assert(extra && !native->recordPreparedGraphicsBinding(extra, diagnostic)
                && diagnostic == "RHI-BINDLESS-PREPARED-GRAPHICS-COMMAND-BUDGET");
            extra.reset();
            std::weak_ptr<const MetalRhiNativePreparedGraphicsBindings> observed = prepared;
            assert(pass->end().success);
            assert(!native->recordPreparedGraphicsBinding(prepared, diagnostic)
                && diagnostic == "RHI-METAL-PREPARED-GRAPHICS-RECORDING-MISMATCH");
            prepared.reset();
            assert(!observed.expired());
            if (mode == 1u) {
                encoder.reset();
                assert(observed.expired());
                continue;
            }
            auto commands = encoder->finish();
            assert(commands);
            encoder.reset();
            assert(!observed.expired());
            if (mode == 2u) {
                commands.reset();
                assert(observed.expired());
                continue;
            }
            auto timeline = device_->createTimelineSemaphore(0u);
            auto fence = device_->createFence();
            assert(timeline && fence);
            assert(device_->queue()
                    .submit(*commands, timeline.get(), 1u, nullptr, 0u, fence.get())
                    .success);
            commands.reset();
            assert(!fence->isSignaled() && !observed.expired());
            timeline->signal(1u);
            fence->wait();
            assert(observed.expired());
        }
        std::cout
            << "preparedGraphicsRecording: exact stages, 32 allocation-free rebinds, bounded cancel/native completion; no draws\n";
    }

    arp::core::UniquePtr<arp::rhi::GpuDevice> device_;
    std::shared_ptr<arp::rhi_metal::MetalRhiNativeDescriptorGroupPayload> descriptors_;
    arp::core::pmr::Vector<arp::core::UniquePtr<arp::rhi::GpuBuffer>> buffers_;
    arp::core::pmr::Vector<arp::core::UniquePtr<arp::rhi::GpuTexture>> textures_;
    arp::core::pmr::Vector<arp::core::UniquePtr<arp::rhi::GpuTextureView>> views_;
    arp::core::pmr::Vector<arp::core::UniquePtr<arp::rhi::GpuSampler>> samplers_;
};

void validateMetalRetainedGraphicsSources(const arp::rhi::BindlessShaderPackage& package,
    std::string_view vertexEntry = "vertexMain",
    std::string_view fragmentEntry = "fragmentMain")
{
    using namespace arp::rhi;
    using namespace arp::rhi_metal;
    arp::test::MetalGraphicsPipelineFixture fixture(package, vertexEntry, fragmentEntry);
    const auto vertexSource = fixture.source(vertexEntry),
               fragmentSource = fixture.source(fragmentEntry);
    auto native = fixture.create();
#if defined(__APPLE__)
    if (!native || !native->nativeRuntimeBacked)
        std::cerr << (native ? native->diagnostics.c_str() : "missing native pipeline") << '\n';
    assert(native && native->nativeRuntimeBacked && !native->nativeRuntimeDiagnosticOnly);
    assert(native->pipelineState && native->vertexFunction && native->fragmentFunction);
    assert(native->shaderLibrary && native->fragmentShaderLibrary);
    assert(native->shaderLibrary != native->fragmentShaderLibrary
        && !native->supportsIndirectCommands);
    MetalRhiPreparedGraphicsStageLayout vertex, fragment;
    assert(lowerMetalRhiPreparedGraphicsStage(package, vertexEntry, ShaderStage::Vertex, vertex)
            .success);
    assert(
        lowerMetalRhiPreparedGraphicsStage(package, fragmentEntry, ShaderStage::Fragment, fragment)
            .success);
    for (const auto* layout : {&vertex, &fragment}) {
        for (const auto& map : layout->direct)
            if (map.active)
                assert((native->nativeBindingStages.stages(map.type, map.metalSlot) & layout->stage)
                    != ShaderStage::None);
        for (const auto& map : layout->arrays)
            if (map.active)
                assert((native->nativeBindingStages.stages(
                            DescriptorType::UniformBuffer, map.argumentBufferSlot)
                           & layout->stage)
                    != ShaderStage::None);
    }
    MetalGraphicsArgumentFixture(package).validate(
        native, vertex, fragment, package.artifact().set);
    arp::test::MetalGraphicsPipelineFixture depthFixture(package, vertexEntry, {});
    auto depth = depthFixture.create();
    if (!depth || !depth->nativeRuntimeBacked)
        std::cerr << (depth ? depth->diagnostics.c_str() : "missing depth pipeline") << '\n';
    assert(depth && depth->nativeRuntimeBacked && depth->pipelineState && depth->vertexFunction);
    assert(depth->depthOnlyRenderPipeline && depth->preparedGraphicsStages == ShaderStage::Vertex
        && !depth->fragmentFunction && !depth->fragmentShaderLibrary && depth->depthStencilState);
    MetalGraphicsArgumentFixture(package).validateDepth(depth, vertex, package.artifact().set);
    auto unexpectedFragment = depthFixture.createWithSources(vertexSource, fragmentSource);
    assert(unexpectedFragment && !unexpectedFragment->nativeRuntimeBacked
        && !unexpectedFragment->pipelineState);
    assert(unexpectedFragment->diagnostics.find("RHI-METAL-GRAPHICS-UNEXPECTED-FRAGMENT-SOURCE")
        != std::string_view::npos);
    const auto missing = fixture.createWithSources(vertexSource, {});
    assert(missing && !missing->nativeRuntimeBacked && !missing->pipelineState
        && !missing->fragmentFunction);
    assert(missing->diagnostics.find("RHI-METAL-PREPARED-GRAPHICS-PIPELINE-CREATION-FAILED") == 0u);
    const auto wrong = fixture.createWithSources(vertexSource, vertexSource);
    assert(
        wrong && !wrong->nativeRuntimeBacked && !wrong->pipelineState && !wrong->fragmentFunction);
    assert(wrong->diagnostics.find("RHI-METAL-PREPARED-GRAPHICS-PIPELINE-CREATION-FAILED") == 0u);
#else
    assert(!native);
#endif
}

void validateOriginalSnapshotBufferSources(const arp::rhi::BindlessShaderPackage& package)
{
    using namespace arp::rhi;
    using namespace arp::core;
    using Role = BindlessSnapshot::OriginalBufferRole;
    using Sources = OriginalCommandResourceSources;
    constexpr std::uint64_t cpuBytes = 65536u;
    const auto quota = [&] {
        return makeShared<CpuMemoryQuota>(cpuBytes);
    };
    auto lifetimeQuota = quota(), registryQuota = quota(), sourceQuota = quota(),
         graphQuota = quota();
    auto device = createNullDevice();
    assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, cpuBytes}, lifetimeQuota));
    const auto& artifact = package.artifact();
    assert(artifact.faultLatch.contractVersion && artifact.snapshotMetadata.contractVersion);
    std::uint32_t tableElements = 0u;
    for (const auto& table : artifact.tables)
        tableElements += table.capacity;
    // Bound optional readback capacity, all table resources and one cold probe.
    assert(!device->prepareOriginalResourceRegistration(
        {1u, true, tableElements + 4u, 0u, 0u, 2u * (tableElements + 4u) + 3u, cpuBytes},
        registryQuota));
    auto* extension =
        static_cast<BindlessCrossExtension*>(device->queryInterface(kExtensionBindlessCross));
    assert(extension);
    auto request = arp::test::BindlessPackageRequestFixture::make(
        artifact, extension->capabilities().deviceIdentity);
    BindlessSnapshotPool pool(*device, request.budget);
    pmr::String diagnostic;
    auto snapshot = pool.prepareShared(request, artifact, diagnostic);
    assert(snapshot);
    const OriginalCommandResourceLeaseBudget leaseBudget{1u, true, 2u, cpuBytes};
    const auto is = [](const char* actual, const char* expected) {
        if (!actual || std::string_view(actual) != expected)
            std::cerr << "snapshot buffer source expected " << expected << ", got "
                      << (actual ? actual : "success") << '\n';
        assert(actual && std::string_view(actual) == expected);
    };
    const auto prepare = [&]() noexcept {
        return BindlessSnapshot::prepareOriginalBufferSources(
            snapshot, *device, leaseBudget, sourceQuota);
    };
    is(prepare(), "RHI-BINDLESS-SNAPSHOT-NOT-SEALED");
    pmr::Vector<UniquePtr<GpuBuffer>> tableBuffers;
    for (const auto& table : artifact.tables) {
        const auto* binding = findDescriptorSetBinding(artifact.physicalLayout, table.firstBinding);
        assert(binding
            && (binding->type == DescriptorType::UniformBuffer
                || binding->type == DescriptorType::ReadOnlyStorageBuffer
                || binding->type == DescriptorType::StorageBuffer));
        const auto write = binding->type == DescriptorType::StorageBuffer;
        for (std::uint32_t i = 0u; i < table.capacity; ++i) {
            auto buffer = device->createBuffer({"original-snapshot-table",
                table.bufferLayout.elementStrideBytes * 4u,
                binding->type == DescriptorType::UniformBuffer ? ResourceUsage::UniformBuffer
                                                               : ResourceUsage::StorageBuffer,
                MemoryUsage::GpuOnly,
                write ? ResourceState::ShaderWrite : ResourceState::ShaderRead});
            assert(buffer
                && snapshot->writeBuffer(snapshot->handle(table.tableId, i), *buffer).success);
            tableBuffers.push_back(std::move(buffer));
        }
    }
    assert(snapshot->seal(*device).success);
    std::array<GpuBuffer*, 3u> original{
        snapshot->metadataBuffer(), snapshot->faultLatchBuffer(), snapshot->faultReadbackBuffer()};
    assert(original[0] && original[1] && !original[2]); // Null never invents a GPU readback child.
    const auto unchanged = [&] {
        assert(snapshot->metadataBuffer() == original[0]
            && snapshot->faultLatchBuffer() == original[1]
            && snapshot->faultReadbackBuffer() == original[2] && snapshot->validate().success);
        for (const auto role : {Role::Metadata, Role::FaultLatch, Role::FaultReadback})
            assert(!snapshot->originalBufferSource(role));
    };
    auto disabled = leaseBudget;
    disabled.prototypeOptIn = false;
    is(BindlessSnapshot::prepareOriginalBufferSources(snapshot, *device, disabled, sourceQuota),
        "RHI-ORIGINAL-SOURCE-LEASE-OPT-IN");
    is(BindlessSnapshot::prepareOriginalBufferSources({}, *device, leaseBudget, sourceQuota),
        "RHI-BINDLESS-ORIGINAL-BUFFERS-SNAPSHOT-OWNER");
    is(BindlessSnapshot::prepareOriginalBufferSources(snapshot, *device, leaseBudget, {}),
        "RHI-ORIGINAL-SOURCE-LEASE-QUOTA");
    auto forged = std::shared_ptr<BindlessSnapshot>(sourceQuota, snapshot.get());
    is(BindlessSnapshot::prepareOriginalBufferSources(forged, *device, leaseBudget, sourceQuota),
        "RHI-BINDLESS-ORIGINAL-BUFFERS-SNAPSHOT-OWNER");
    forged.reset();
    unchanged();
    auto foreign = createNullDevice();
    auto foreignLifetime = quota(), foreignRegistry = quota();
    assert(!foreign->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, cpuBytes}, foreignLifetime));
    assert(!foreign->prepareOriginalResourceRegistration(
        {1u, true, 1u, 0u, 0u, 5u, cpuBytes}, foreignRegistry));
    assert(BindlessSnapshot::prepareOriginalBufferSources(
        snapshot, *foreign, leaseBudget, sourceQuota));
    unchanged();
    // Leave enough Source bytes for one original lease, then fail during the
    // second allocation. The first cold receipt must roll back without adoption.
    const char* stable = nullptr;
    auto probe = device->createBuffer({"lease-quota-probe",
        16u,
        ResourceUsage::StorageBuffer,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderRead});
    assert(probe);
    {
        auto capacity = makeShared<AtomicCapacityBudget>(1u);
        auto reservation = makeShared<AtomicCapacityLease>(capacity, 1u);
        const OriginalCommandBufferLease::CapacityOwners backing{reservation, {}};
        auto cold = OriginalCommandBufferLease::prepareColdOriginal(
            probe, leaseBudget, sourceQuota, stable, backing);
        assert(cold && !stable);
        static_assert(std::is_same_v<decltype(cold->originalCapacityLease(0u)),
            std::shared_ptr<const AtomicCapacityLease>>);
        static_assert(std::is_same_v<decltype(cold->originalCapacityBudget(0u)),
            std::shared_ptr<const AtomicCapacityBudget>>);
        assert(cold->originalCapacityLease(0u) == reservation
            && cold->originalCapacityBudget(0u) == capacity);
        assert(!cold->originalCapacityLease(1u) && !cold->originalCapacityBudget(1u)
            && !cold->originalCapacityLease(SIZE_MAX) && !cold->originalCapacityBudget(SIZE_MAX));
        reservation->release();
        is(arp::test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
               std::array<std::shared_ptr<CpuMemoryQuota>, 1u>{sourceQuota},
               [&]() noexcept {
                   return OriginalCommandBufferLease::validateColdOriginal(cold, *probe);
               }),
            "RHI-ORIGINAL-SOURCE-LEASE-CAPACITY-RELEASED");
        assert(!OriginalCommandBufferLease::prepareColdOriginal(
            probe, leaseBudget, sourceQuota, stable, backing));
        is(stable, "RHI-ORIGINAL-SOURCE-LEASE-CAPACITY-RELEASED");
        assert(probe);
    }
    const auto baseline = sourceQuota->liveBytes();
    std::uint64_t oneLeaseBytes = 0u;
    {
        auto cold = OriginalCommandBufferLease::prepareColdOriginal(
            probe, leaseBudget, sourceQuota, stable);
        assert(cold && !stable);
        oneLeaseBytes = sourceQuota->liveBytes() - baseline;
    }
    assert(sourceQuota->liveBytes() == baseline && oneLeaseBytes);
    {
        CpuMemoryQuotaLease pressure(sourceQuota, cpuBytes - baseline - oneLeaseBytes);
        assert(pressure.charged());
        is(prepare(), "RHI-ORIGINAL-SOURCE-LEASE-ALLOCATION");
        unchanged();
    }
    assert(sourceQuota->liveBytes() == baseline && !prepare());
    assert(snapshot->metadataBuffer() == original[0] && snapshot->faultLatchBuffer() == original[1]
        && snapshot->faultReadbackBuffer() == original[2]);
    assert(snapshot->metadataAccess().resource == original[0]->id());
    assert(snapshot->faultLatchAccess().resource == original[1]->id());
    assert(!snapshot->faultReadbackAccess().resource.isValid()
        && !snapshot->originalBufferSource(Role::FaultReadback));
    assert(snapshot->seal(*device).success);
    assert(!arp::test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
        std::array<std::shared_ptr<CpuMemoryQuota>, 1u>{sourceQuota}, prepare));
    auto foreignSourceQuota = quota();
    is(BindlessSnapshot::prepareOriginalBufferSources(
           snapshot, *device, leaseBudget, foreignSourceQuota),
        "RHI-BINDLESS-ORIGINAL-BUFFERS-QUOTA-OWNER");
    std::array<Sources::Source, 2u> inputs{snapshot->originalBufferSource(Role::Metadata),
        snapshot->originalBufferSource(Role::FaultLatch)};
    OriginalCommandAdmissionBudget admission;
    admission.contractVersion = 1u;
    admission.prototypeOptIn = true;
    admission.maxCommands = 3u;
    admission.maxReferences = 2u;
    admission.maxGraphOwners = 10u;
    admission.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::Buffer)] = 2u;
    admission
        .maxCpuBytes[static_cast<std::size_t>(OriginalCommandAdmissionBudget::CpuDomain::Source)] =
        cpuBytes;
    admission
        .maxCpuBytes[static_cast<std::size_t>(OriginalCommandAdmissionBudget::CpuDomain::Graph)] =
        cpuBytes;
    auto sources =
        Sources::prepareOriginal(inputs.data(), inputs.size(), admission, sourceQuota, stable);
    assert(sources && !stable && !Sources::validateRegisteredLifetime(sources, *device));
    std::uint32_t owners = 0u;
    assert(!sources->cpuOwnerCount(2u, 10u, owners)
        && owners == 10u); // 2 roots + 4 child nodes + 4 shared capacity nodes.
    assert(sources->cpuOwnerCount(2u, 9u, owners));
    const std::array<CommandUsage, 3u> commands{{{CommandUsageKind::BufferBarrier, 0u, 1u},
        {CommandUsageKind::BufferBarrier, 1u, 1u},
        {CommandUsageKind::Finish, 2u, 0u}}};
    const std::array<OriginalCommandUsage::Reference, 2u> references{{{0u}, {1u}}};
    auto usage = OriginalCommandUsage::prepareOriginal(sources,
        commands.data(),
        commands.size(),
        references.data(),
        references.size(),
        graphQuota,
        stable);
    assert(usage && !stable && !usage->nativeExecutionProven());
    for (std::size_t i = 0u; i < references.size(); ++i)
        assert(!usage->compareOriginalReference(i, *original[i]));
    const auto residentBytes = pool.chargedBytes();
    snapshot.reset();
    inputs = {};
    assert(pool.liveSnapshots() == 1u && pool.chargedBytes() == residentBytes);
    assert(!Sources::validateRegisteredLifetime(sources, *device) && !usage->validate());
    assert(sourceQuota->liveBytes() > baseline);
    usage.reset();
    sources.reset();
    assert(pool.liveSnapshots() == 0u && pool.chargedBytes() == 0u
        && sourceQuota->liveBytes() == baseline);
    for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        const auto lowering = semantics::OriginalBindlessBufferSourceLowering::lowering(backend);
        assert(lowering.contractVersion == 1u && lowering.sourceAvailable
            && lowering.mode == CapabilityMode::Equivalent && !lowering.executionAvailable);
    }
    assert(
        !semantics::OriginalBindlessBufferSourceLowering::lowering(static_cast<Backend>(UINT32_MAX))
            .sourceAvailable);
    std::cout
        << "originalSnapshotBufferSources: canonical Buffer inventory, original quota, atomic cold adoption, shared capacity retirement, allocation-free reuse; no bindless execution\n";
}

void validateGraphicsFaultPackage(
    std::shared_ptr<const arp::rhi::BindlessShaderPackage> package, bool nativePreparation)
{
    using namespace arp::rhi;
    using namespace arp::core;
    const auto& artifact = package->artifact();
    validateOriginalSnapshotBufferSources(*package);
    arp::test::validateOriginalBindlessRootSource(package);
    assert(package->modules().size() == 3u && artifact.tables.size() == 3u);
    assert(
        artifact.activePrefixContractVersion == 2u && artifact.faultReportingContractVersion == 1u);
    assert(artifact.snapshotMetadata.contractVersion == 1u
        && artifact.snapshotMetadata.binding == 6u
        && artifact.snapshotMetadata.stages == (ShaderStage::Vertex | ShaderStage::Fragment));
    assert(artifact.faultLatch.contractVersion == 1u && artifact.faultLatch.binding == 7u
        && artifact.faultLatch.stages == (ShaderStage::Vertex | ShaderStage::Fragment));
    assert(artifact.indirectInstanceConsumer.contractVersion == 0u);
    auto device = createNullDevice();
    auto* extension =
        static_cast<BindlessCrossExtension*>(device->queryInterface(kExtensionBindlessCross));
    assert(extension);
    BindlessRequirements request;
    request.optIn = true;
    request.allowEmulated = true;
    request.deviceIdentity = extension->capabilities().deviceIdentity;
    request.shaderPackageIdentity = artifact.shaderPackageIdentity;
    std::uint32_t descriptorCount = 0u;
    for (const auto& binding : artifact.physicalLayout.bindings)
        descriptorCount += binding.arrayCount;
    request.budget = {1u, descriptorCount, artifact.snapshotBytes, 16u};
    for (const auto& table : artifact.tables) {
        const auto physical = std::find_if(artifact.physicalLayout.bindings.begin(),
            artifact.physicalLayout.bindings.end(),
            [&](const auto& binding) {
                return binding.binding == table.firstBinding;
            });
        assert(physical != artifact.physicalLayout.bindings.end());
        request.tables.push_back({table.tableId,
            *physical,
            table.capacity,
            table.operations,
            table.nonUniform,
            table.bufferLayout});
    }
    BindlessSnapshotPool snapshots(*device, request.budget);
    pmr::String diagnostic;
    auto snapshot = snapshots.prepareShared(request, artifact, diagnostic);
    if (!snapshot)
        std::cerr << diagnostic << '\n';
    assert(snapshot);
    pmr::Vector<UniquePtr<GpuBuffer>> buffers;
    pmr::Vector<BindlessResourceAccess> accesses;
    for (const auto& table : request.tables) {
        const auto size = table.bufferLayout.elementStrideBytes;
        assert(size && size >= table.bufferLayout.minBindingSizeBytes && table.resourceCount == 2u);
        const bool writable = table.binding.type == DescriptorType::StorageBuffer;
        const auto usage = table.binding.type == DescriptorType::UniformBuffer
            ? ResourceUsage::UniformBuffer
            : ResourceUsage::StorageBuffer;
        for (std::uint32_t element = 0u; element < table.resourceCount; ++element) {
            auto buffer = device->createBuffer({"GraphicsFaultContractBuffer",
                size,
                usage,
                MemoryUsage::GpuOnly,
                writable ? ResourceState::ShaderWrite : ResourceState::ShaderRead});
            assert(buffer);
            assert(
                snapshot->writeBuffer(snapshot->handle(table.tableId, element), *buffer).success);
            BindlessResourceAccess access;
            access.resource = buffer->id();
            access.kind = BindlessAccessKind::Buffer;
            access.stages = table.binding.stages;
            access.bufferSize = size;
            access.state = buffer->desc().initialState;
            access.access = writable ? ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite
                                     : ResourceAccess::ShaderRead;
            if ((access.stages & ShaderStage::Vertex) != ShaderStage::None)
                access.pipelineStages = access.pipelineStages | PipelineStage::VertexShader;
            if ((access.stages & ShaderStage::Fragment) != ShaderStage::None)
                access.pipelineStages = access.pipelineStages | PipelineStage::FragmentShader;
            accesses.push_back(access);
            buffers.push_back(std::move(buffer));
        }
    }
    assert(snapshot->seal(*device).success);
    accesses.push_back(snapshot->metadataAccess());
    accesses.push_back(snapshot->faultLatchAccess());
    auto authorization = extension->authorize(package, *snapshot, diagnostic);
    assert(authorization);
    RenderPipelineDesc state;
    state.colorFormat = TextureFormat::Rgba8Unorm;
    state.dynamicViewportScissor = true;
    auto target = device->createTexture({"GraphicsFaultContractTarget",
        {2u, 2u, 1u},
        state.colorFormat,
        TextureUsage::RenderAttachment,
        MemoryUsage::GpuOnly,
        ResourceState::ColorAttachment});
    assert(target);
    RenderPassDesc renderDesc;
    renderDesc.colorAttachments.push_back(
        {"color", target->id(), state.colorFormat, LoadOp::Clear, StoreOp::Store});
    const auto retained = snapshot->plan().snapshotBytes + package->sourceBytes();
    constexpr std::uint64_t fixtureArgumentByteBudget = 4096u;
    BindlessGraphicsBindingPool bindings(*extension,
        {1u,
            retained + fixtureArgumentByteBudget,
            retained + fixtureArgumentByteBudget,
            8u,
            fixtureArgumentByteBudget});
    BindlessAccessArray declarations(accesses.data(), accesses.size());
    for (const auto entry : {"cleanFragment", "faultFragment"}) {
        auto pipeline = shareUnique(
            extension->createRenderPipeline(authorization, state, "vertexMain", entry, diagnostic));
        assert(pipeline);
        auto binding = bindings.prepare(
            {true, snapshot, pipeline, snapshot->plan(), snapshot->generation()}, diagnostic);
        if (!binding)
            std::cerr << diagnostic << '\n';
        assert(binding);
        auto encoder = device->createCommandEncoder({"GraphicsFaultContractScope",
            1u,
            1u,
            0u,
            0u,
            static_cast<std::uint32_t>(accesses.size())});
        assert(encoder);
        assert(encoder->prepareBindlessGraphicsPass(binding, declarations).success);
        auto pass = encoder->beginRenderPass(renderDesc);
        assert(pass);
        assert(pass->setPipeline(*pipeline).success);
        auto* indexed =
            static_cast<BindlessPassExtension*>(pass->queryInterface(kExtensionBindlessCross));
        assert(indexed);
        assert(indexed->bindPreparedGraphicsBinding(binding, declarations).success);
        assert(pass->setViewport({0, 0, 2, 2, 0, 1}).success
            && pass->setScissor({0, 0, 2, 2}).success);
        assert(pass->draw(3u, 1u, 0u, 0u).success && pass->end().success);
        auto commands = encoder->finish();
        assert(commands && device->queue().submit(*commands).success);
        auto* report = static_cast<BindlessCommandBufferExtension*>(
            commands->queryInterface(kExtensionBindlessCross));
        assert(report
            && report->faultReport(snapshot->generation()).status
                == BindlessFaultValidationStatus::NotExecuted);
    }
    if (nativePreparation) {
        assert(artifact.execution == BindlessExecution::Native);
        validateMetalRetainedGraphicsSources(*package, "vertexMain", "cleanFragment");
        validateMetalRetainedGraphicsSources(*package, "vertexMain", "faultFragment");
    }
    std::cout
        << "Graphics fault fixture: sealed target reflection and scoped Null NotExecuted; no native draw claim\n";
}

// Invoked by bindless_codegen_tests.py against real compiled temporary packages.
// These fixture ceilings are explicit test input, never production defaults.
int main(int argc, char** argv)
{
    arp::test::OriginalUndefinedTextureUploadPlanFixture::verifyContract();
    assert(argc >= 3);
    arp::rhi::BindlessPackageLoadBudget budget{
        2u * 1024u * 1024u, 16u * 1024u * 1024u, 64, 16, 256, 64};
    const std::string_view option = argc > 3 ? argv[3] : "";
    const std::string_view packageMode = argc > 4 ? argv[4] : "";
    using VisibilityTargetRequest = arp::shader::TargetBufferMemoryLoadRequest;
    using VisibilityRootForm = VisibilityTargetRequest::Lowering::RootForm;
    // Exact active vertex bindings from the versioned visibility consumer schema.
    const std::array<VisibilityTargetRequest::BindingPlan, 4u> visibilityTargetBindings{{
        {2u, 0u, VisibilityRootForm::StorageRuntimeArray, {nullptr, 0u}},
        {2u, 1u, VisibilityRootForm::StorageRuntimeArray, {nullptr, 0u}},
        {2u, 2u, VisibilityRootForm::StorageRuntimeArray, {nullptr, 0u}},
        {2u, 3u, VisibilityRootForm::BlockStruct, {nullptr, 0u}},
    }};
    const VisibilityTargetRequest visibilityTargetPush{1u,
        {65536u, 65536u, 256u},
        {65536u, 65536u, 256u},
        65536u,
        0u,
        visibilityTargetBindings.data(),
        visibilityTargetBindings.size(),
        true,
        {nullptr, 0u}};
    arp::rhi::BindlessPackagePushLoadRequest::Entry visibilityPushEntry{
        "visibilityVertexMain", {65536u, 65536u, 256u}, nullptr};
    if (option == "gpu-visibility-root-composition" && packageMode == "emulated")
        visibilityPushEntry.target = &visibilityTargetPush;
    const arp::rhi::BindlessPackagePushLoadRequest visibilityPush{
        1u, 16u, &visibilityPushEntry, 1u};
    if (option == "gpu-visibility-consumer" || option == "gpu-visibility-root-composition")
        budget.push = &visibilityPush;
    // Production geometry libraries use real attribute-bearing Vertex entries.
    // Load their exact active ordinary buffers and push ABI independently from
    // Python reflection, for both Native and scalar Emulated publications.
    const bool geometryFamily =
        option == "pbr-geometry-family" || option == "terrain-geometry-family";
    arp::core::UniquePtr<arp::test::BindlessGeometryPackageFixture> familyMemory;
    if (geometryFamily) {
        familyMemory = arp::core::makeUnique<arp::test::BindlessGeometryPackageFixture>(
            argv[1], option == "terrain-geometry-family", packageMode);
        budget = {4u * 1024u * 1024u, 64u * 1024u * 1024u, 256u, 16u, 256u, 64u};
        budget.push = &familyMemory->push;
        budget.ordinaryMemory = &familyMemory->memory;
    }
    const bool validateNativeMetalStageConstruction = option == "metal-stage-pipeline";
    if (option == "file")
        budget.maxFileBytes = 1;
    if (option == "total")
        budget.maxTotalBytes = 1;
    if (option == "files")
        budget.maxFiles = 1;
    if (option == "modules")
        budget.maxModules = 1;
    if (option == "bindings")
        budget.maxBindings = 1;
    if (option == "tables")
        budget.maxTables = 1;
    if (option == "zero")
        budget.maxFileBytes = 0;
    arp::core::pmr::String diagnostic;
    auto package = arp::rhi::BindlessShaderPackage::load(argv[1], budget, diagnostic);
    if (std::string_view(argv[2]) != "valid") {
        if (package || diagnostic != argv[2]) {
            std::cerr << "Expected " << argv[2] << ", got " << diagnostic << '\n';
            return 1;
        }
        return 0;
    }
    if (!package) {
        std::cerr << diagnostic << '\n';
        return 1;
    }
    assert(diagnostic.empty());
    if (option == "graphics-profile-present" || option == "graphics-profile-absent") {
        using namespace arp::rhi;
        const BindlessPackageModule *vertex = nullptr, *fragment = nullptr;
        for (const auto& module : package->modules()) {
            const auto stage = module.reflection.reflection.entryPoints[0].stage;
            if (stage == arp::shader::ShaderStage::Vertex)
                vertex = &module;
            if (stage == arp::shader::ShaderStage::Fragment)
                fragment = &module;
        }
        assert(vertex && fragment);
        const auto selected =
            selectBindlessInitializedGraphics(*package, vertex->entry, fragment->entry);
        assert(
            !selected.diagnostic && selected.recognized == (option == "graphics-profile-present"));
        return 0;
    }
    if (option == "gpu-visibility-root-composition") {
        arp::test::validateOriginalBindlessVisibilityRoots(package);
        return 0;
    }
    if (option == "root-record-fragment-package") {
        using namespace arp::rhi;
        const auto& artifact = package->artifact();
        assert(artifact.execution == BindlessExecution::Emulated && artifact.tables.size() == 3u
            && package->modules().size() == 3u && package->modules()[0u].entry == "vertexUnindexed"
            && artifact.tables[0u].bufferLayout.elementStrideBytes == 32u
            && artifact.tables[1u].bufferLayout.elementStrideBytes == 64u
            && artifact.tables[2u].bufferLayout.elementStrideBytes == 48u);
        assert(package->modules()[0u].reflection.reflection.bindings.empty());
        return 0;
    }
    if (geometryFamily) {
        using namespace arp::rhi;
        const bool terrain = option == "terrain-geometry-family";
        assert((packageMode == "native")
            == (package->artifact().execution == BindlessExecution::Native));
        assert(package->modules().size() == (terrain ? 6u : 15u));
        for (const auto& module : package->modules()) {
            if (module.push.stages == ShaderStage::Vertex) {
                assert(module.push.sizeBytes == 16u && module.push.occupiedBytes == 16u);
                assert(!module.reflection.reflection.vertexAttributes.empty());
                if (package->artifact().execution == BindlessExecution::Emulated)
                    assert(module.targetPush.sourceOccupiedBytes == 16u
                        && module.targetPush.targetDeclarationBytes == 16u
                        && module.targetPush.group == 3u && module.targetPush.binding == 0u);
            }
        }
        auto device = createNullDevice();
        auto* extension =
            static_cast<BindlessCrossExtension*>(device->queryInterface(kExtensionBindlessCross));
        assert(extension);
        auto caps = extension->capabilities();
        auto request = arp::test::BindlessPackageRequestFixture::make(
            package->artifact(), caps.deviceIdentity);
        if (packageMode == "emulated") {
            // The existing independent sampled/array Graphics4 contract owns
            // this exact scope: Fragment textures/samplers plus scalar ordinary
            // Vertex buffers. OrdinaryColor13 intentionally remains buffer-only.
            caps.execution = BindlessExecution::DiagnosticFailure;
            for (const auto type : {DescriptorType::UniformBuffer,
                     DescriptorType::ReadOnlyStorageBuffer,
                     DescriptorType::StorageBuffer})
                caps.ordinaryDescriptorDomains[bindlessResourceClass(type)] = true;
            caps.originalArrayFragmentGraphicsExecutionContractVersion = 4u;
            caps.faultReadbackContractVersion = 1u;
            request.originalExecution = {4u, true};
            const auto plan = planBindless(caps, request, package->artifact());
            if (!plan.success)
                std::cerr << plan.diagnosticId << '\n';
            assert(plan.success && plan.executable && plan.execution == BindlessExecution::Emulated
                && plan.originalExecutionContractVersion == 4u);
            // Declarations cannot authorize a different buffer-only profile.
            caps.originalOrdinaryColorGraphicsExecutionContractVersion = 13u;
            request.originalExecution = {13u, true};
            assert(!planBindless(caps, request, package->artifact()).success);
        }
        std::cout
            << "geometry-family: real package, Vertex attributes, Push16 and target layouts; no execution grant\n";
        if (argc > 5 && std::string_view(argv[5]) == "scene-roots")
            arp::test::BindlessSceneRootInputsFixture::verify(package);
        return 0;
    }
    if (option == "gpu-visibility" || option == "gpu-visibility-consumer") {
        using namespace arp::rhi;
        if (option == "gpu-visibility-consumer"
            && package->artifact().execution == BindlessExecution::Emulated) {
            // SPIR-V proof alone cannot authorize a WGSL uniform-push ABI.
            ShaderPackageDesc refused;
            const BindlessPackageModule* selected = nullptr;
            const auto missingTarget = BindlessGraphicsPipelineEntries::composeStage(
                package, "visibilityVertexMain", ShaderStage::Vertex, refused, selected);
            assert(!missingTarget.success
                && missingTarget.diagnostics == "RHI-BINDLESS-GRAPHICS-PUSH-TARGET-PROOF");
            visibilityPushEntry.target = &visibilityTargetPush;
            package = BindlessShaderPackage::load(argv[1], budget, diagnostic);
            if (!package)
                std::cerr << diagnostic << '\n';
            assert(package);
        }
        const auto& artifact = package->artifact();
        assert(package->gpuVisibilityKernelContractVersion() == 1u);
        const bool consumer = option == "gpu-visibility-consumer";
        if (consumer) {
            using Scope = semantics::BindlessPipelineInterfaceContract;
            assert(Scope::producedGeometryDomains(artifact));
            const auto refuses = [&](auto mutate) {
                auto invalid = artifact;
                mutate(invalid);
                assert(!Scope::producedGeometryDomains(invalid));
            };
            refuses([](auto& bad) {
                bad.additionalDomains[0u].layout.bindings[0u].type = DescriptorType::StorageBuffer;
            });
            refuses([](auto& bad) {
                bad.additionalDomains[0u].layout.bindings[0u].stages = ShaderStage::Compute;
            });
            refuses([](auto& bad) {
                bad.additionalDomains[0u].layout.bindings[0u].binding =
                    bad.additionalDomains[0u].layout.bindings[1u].binding;
            });
            refuses([](auto& bad) {
                bad.additionalDomains[0u].layout.bindings.pop_back();
            });
            refuses([](auto& bad) {
                bad.additionalDomains[0u].layout.bindings[0u].dynamicOffset = true;
            });
            refuses([](auto& bad) {
                bad.physicalLayout.bindings[0u].stages = ShaderStage::Vertex;
            });
            refuses([](auto& bad) {
                bad.faultLatch.stages = ShaderStage::Fragment;
            });
            refuses([](auto& bad) {
                bad.tables.back().operations = BindlessOperation::SampleGrad;
            });
            refuses([](auto& bad) {
                for (auto& binding : bad.physicalLayout.bindings)
                    if (binding.type == DescriptorType::SampledTexture)
                        binding.depthTexture = true;
            });
            refuses([](auto& bad) {
                for (auto& binding : bad.physicalLayout.bindings)
                    if (binding.type == DescriptorType::SampledTexture)
                        binding.textureViewDimension = TextureViewDimension::Texture2DArray;
            });
        }
        assert(package->gpuVisibilityConsumerContractVersion() == (consumer ? 2u : 0u));
        assert(package->modules().size() == (consumer ? 6u : 4u) && artifact.tables.size() == 10u
            && artifact.physicalLayout.bindings.size() == 12u && artifact.set == 1u
            && artifact.snapshotBytes == 224u && artifact.snapshotMetadata.wordCount == 54u
            && artifact.faultReportingContractVersion == 1u
            && artifact.switchCases
                == (artifact.execution == BindlessExecution::Emulated ? (consumer ? 18u : 16u)
                                                                      : 0u));
        constexpr const char* entries[]{"visibilityResetComputeMain",
            "visibilityCullComputeMain",
            "visibilityEmitComputeMain",
            "visibilityFinalizeComputeMain"};
        constexpr std::uint32_t strides[]{112u, 64u, 4u, 4u, 64u, 4u, 16u, 4u, 448u};
        for (std::size_t i = 0u; i < 9u; ++i)
            assert(artifact.tables[i].bufferLayout.elementStrideBytes == strides[i]);
        for (const auto* entry : entries) {
            ShaderPackageDesc view;
            assert(
                appendBindlessShaderEntry(*package, entry, ShaderStage::Compute, view, diagnostic));
            assert(validatePipelineLayoutAgainstShaderReflection(
                bindlessPipelineLayout(artifact), view.reflection)
                    .success);
        }
        if (consumer) {
            const auto& vertex = package->modules()[4u];
            assert(vertex.entry == "visibilityVertexMain" && vertex.push.sizeBytes == 16u
                && vertex.push.stages == ShaderStage::Vertex
                && vertex.reflection.reflection.pushConstantMembers.size() == 4u);
            for (std::size_t i = 0u; i < 4u; ++i)
                assert(vertex.reflection.reflection.pushConstantMembers[i].offset == i * 4u);
            for (std::size_t i = 0u; i < package->modules().size(); ++i)
                assert(package->modules()[i].reflection.reflection.pushConstantSizeBytes
                    == (i == 4u ? 16u : 0u));
            ShaderPackageDesc view;
            assert(!appendBindlessShaderEntry(
                *package, "visibilityVertexMain", ShaderStage::Vertex, view, diagnostic));
            assert(diagnostic == "RHI-BINDLESS-PIPELINE-PUSH-EXECUTION-UNAVAILABLE"
                && view.entrypoints.empty());
            PipelineLayoutDesc layout;
            BindlessGraphicsPipelineEntries selected;
            const auto composed = BindlessGraphicsPipelineEntries::compose(
                package, "visibilityVertexMain", "visibilityFragmentMain", view, layout, selected);
            if (!composed.success)
                std::fprintf(
                    stderr, "visibility consumer compose: %s\n", composed.diagnostics.c_str());
            assert(composed.success && view.reflection.pushConstantByteCount == 16u
                && layout.pushConstantRanges.size() == 1u
                && layout.pushConstantRanges.front().stages == ShaderStage::Vertex);
            // The closed consumer reads four ordinary Vertex buffers; its
            // Fragment returns the interpolated color and consumes no table,
            // metadata or fault resource. Require the actual active interface.
            assert(view.reflection.descriptorBindings.size() == 4u);
            for (std::uint32_t binding = 0u; binding < 4u; ++binding) {
                const auto found = std::find_if(view.reflection.descriptorBindings.begin(),
                    view.reflection.descriptorBindings.end(),
                    [&](const auto& value) {
                        return value.set == 2u && value.binding == binding;
                    });
                assert(found != view.reflection.descriptorBindings.end()
                    && found->stages == ShaderStage::Vertex && found->arrayCount == 1u
                    && found->type
                        == (binding == 3u ? DescriptorType::UniformBuffer
                                          : DescriptorType::ReadOnlyStorageBuffer));
            }
        }
        std::puts(consumer
                ? "BINDLESS_GPU_VISIBILITY_CONSUMER_PACKAGE_OK closed-source six-real-entries Push16 Instance64 Mapping16 no-native-execution-grant"
                : "BINDLESS_GPU_VISIBILITY_PACKAGE_OK closed-source four-real-entries typed-ABI atomic-add-or descriptor-and-computation-fault no-native-execution-grant");
        return 0;
    }
    if (option == "depth-pyramid") {
        using namespace arp::rhi;
        const auto& artifact = package->artifact();
        arp::test::OriginalMixedImageSnapshotFixture::verifyPlan(artifact);
        assert(artifact.tables.size() == 6u && package->modules().size() == 3u);
        assert(artifact.snapshotBytes == 144u && artifact.snapshotMetadata.wordCount == 34u);
        assert(
            artifact.switchCases == (artifact.execution == BindlessExecution::Emulated ? 9u : 0u));
        std::uint32_t depth = 0u, storage = 0u, sampled = 0u, descriptors = 0u;
        for (const auto& binding : artifact.physicalLayout.bindings) {
            assert(binding.stages == ShaderStage::Compute);
            descriptors += binding.arrayCount;
            if (binding.type == DescriptorType::SampledTexture) {
                assert(binding.textureViewDimension == TextureViewDimension::Texture2D);
                if (binding.depthTexture)
                    depth += binding.arrayCount;
                else
                    sampled += binding.arrayCount;
            } else if (binding.type == DescriptorType::StorageTexture) {
                assert(BindlessStorageImageDomain::accepts(binding.storageTextureFormat,
                    binding.textureViewDimension,
                    TextureAspect::Color,
                    1u,
                    binding.storageTextureAccess,
                    1u));
                storage += binding.arrayCount;
            } else
                assert(binding.type == DescriptorType::StorageBuffer
                    || binding.type == DescriptorType::ReadOnlyStorageBuffer);
        }
        assert(depth == 1u && storage == 2u && sampled == 2u && descriptors == 8u);
        for (const auto& module : package->modules()) {
            ShaderPackageDesc selected;
            assert(appendBindlessShaderEntry(
                *package, module.entry, ShaderStage::Compute, selected, diagnostic));
            assert(module.spirvFile != UINT32_MAX && module.metalFile != UINT32_MAX);
            arp::core::StringView wgsl;
            assert(selectBindlessEmulatedWgsl(
                       *package, module.entry, ShaderStage::Compute, wgsl, diagnostic)
                == (artifact.execution == BindlessExecution::Emulated));
            // Every shared physical declaration must have authenticated entry
            // consumption or explicit target inactivity, including depth beside
            // storage. This is prerequisite data for a single shared snapshot.
            std::uint32_t activeBindings = 0u;
            for (const auto& binding : artifact.physicalLayout.bindings) {
                bool active = false;
                const auto* id = semantics::OriginalBindlessEntryAccessContract::binding(
                    module.reflection.reflection,
                    artifact.execution,
                    artifact.set,
                    binding.binding,
                    binding.stages,
                    active);
                if (id)
                    std::cerr << module.entry << ": " << id << '\n';
                assert(!id);
                if (active)
                    activeBindings |= 1u << binding.binding;
            }
            const auto common = (1u << 6u) | (1u << 7u);
            const auto expected = module.entry == "depthPyramidCopyMain"
                ? common | (1u << 0u) | (1u << 1u)
                : module.entry == "depthPyramidReduceMain"
                ? common | (1u << 2u) | (1u << 3u)
                : common | (1u << 3u) | (1u << 4u) | (1u << 5u);
            assert(activeBindings == expected);
        }
        auto device = createNullDevice();
        auto* extension =
            static_cast<BindlessCrossExtension*>(device->queryInterface(kExtensionBindlessCross));
        auto request = arp::test::BindlessPackageRequestFixture::make(
            artifact, extension->capabilities().deviceIdentity);
        assert(!extension->plan(request, artifact).executable);
        assert(selectBindlessVariant(
                   BindlessPolicy::Required, extension->capabilities(), request, &artifact, 1u)
                   .path
            == BindlessSelectionPath::Rejected);
        std::cout
            << "DepthPyramid mixed depth/storage shared package and entry access validated; GPU execution not granted\n";
        return 0;
    }
    if (option == "storage-vector") {
        using namespace arp::rhi;
        std::uint32_t images = 0u;
        for (const auto& binding : package->artifact().physicalLayout.bindings) {
            if (binding.type != DescriptorType::StorageTexture)
                continue;
            assert(binding.storageTextureFormat == TextureFormat::Rgba16Float);
            assert(BindlessStorageImageDomain::accepts(binding.storageTextureFormat,
                binding.textureViewDimension,
                TextureAspect::Color,
                1u,
                binding.storageTextureAccess,
                1u));
            images += binding.arrayCount;
        }
        assert(images == 2u && package->modules().size() == 2u);
        auto device = createNullDevice();
        auto* extension =
            static_cast<BindlessCrossExtension*>(device->queryInterface(kExtensionBindlessCross));
        const auto request = arp::test::BindlessPackageRequestFixture::make(
            package->artifact(), extension->capabilities().deviceIdentity);
        assert(!extension->plan(request, package->artifact()).executable);
        std::cout
            << "StorageImage/1 RGBA16F four-lane declarations validated; execution not granted\n";
        return 0;
    }
    if (option == "pipeline-images") {
        using namespace arp::rhi;
        const auto& artifact = package->artifact();
        assert(artifact.pipelineInterfaceContractVersion == 1u
            && artifact.additionalDomains.size() == 2u);
        const auto gi = std::find_if(artifact.additionalDomains.begin(),
            artifact.additionalDomains.end(),
            [](const auto& d) {
                return d.set == 2u;
            });
        assert(gi != artifact.additionalDomains.end() && gi->layout.bindings.size() == 7u);
        for (const auto& binding : gi->layout.bindings) {
            assert(binding.arrayCount == 1u
                && binding.stages == (ShaderStage::Compute | ShaderStage::Fragment));
            if (binding.binding == 0u)
                assert(binding.textureViewDimension == TextureViewDimension::TextureCube);
            if (binding.binding == 3u)
                assert(binding.depthTexture && !binding.sampledTextureFilterable);
            if (binding.binding == 4u)
                assert(binding.comparisonSampler && !binding.sampledTextureFilterable);
            if (binding.binding == 5u)
                assert(binding.textureViewDimension == TextureViewDimension::Texture2DArray);
            if (binding.binding == 6u)
                assert(binding.textureViewDimension == TextureViewDimension::Texture3D);
        }
        auto device = createNullDevice();
        const auto* extension =
            static_cast<BindlessCrossExtension*>(device->queryInterface(kExtensionBindlessCross));
        auto request = arp::test::BindlessPackageRequestFixture::make(
            artifact, extension->capabilities().deviceIdentity);
        BindlessPipelineInterfaceSummary summary;
        assert(!inspectBindlessPipelineInterface(extension->capabilities(), artifact, summary));
        assert(summary.bindGroupCount == 3u && summary.descriptorCount == 12u);
        assert(summary.additionalPerStage[2].sampledTextures == 5u
            && summary.additionalPerStage[2].samplers == 2u);
        ShaderPackageDesc view;
        assert(appendBindlessShaderEntry(
            *package, "computeMain", ShaderStage::Compute, view, diagnostic));
        assert(appendBindlessShaderEntry(
            *package, "fragmentMain", ShaderStage::Fragment, view, diagnostic));
        const auto shadow = std::find_if(view.reflection.descriptorBindings.begin(),
            view.reflection.descriptorBindings.end(),
            [](const auto& b) {
                return b.set == 2u && b.binding == 3u;
            });
        assert(shadow != view.reflection.descriptorBindings.end() && shadow->depthTexture);
        auto caps = extension->capabilities();
        for (const auto execution : {BindlessExecution::Native, BindlessExecution::Emulated}) {
            caps.execution = execution;
            caps.executablePayload = true;
            const auto rejected = planBindless(caps, request, artifact);
            assert(!rejected.success
                && std::string_view(rejected.diagnosticId)
                    == "RHI-BINDLESS-PIPELINE-DOMAINS-EXECUTION-UNAVAILABLE");
        }
        std::cout
            << "Ordinary GI image/sampler declarations, exact budgets and closed execution validated\n";
        return 0;
    }
    if (option == "graphics-depth-color") {
        using namespace arp::rhi;
        const auto& artifact = package->artifact();
        assert(artifact.pipelineInterfaceContractVersion == 1u && artifact.set == 1u
            && artifact.additionalDomains.size() == 1u && package->modules().size() == 3u);
        const auto& ordinary = artifact.additionalDomains.front();
        assert(ordinary.set == 2u && ordinary.layout.bindings.size() == 5u);
        const auto* depth = findDescriptorSetBinding(ordinary.layout, 2u);
        assert(depth && depth->depthTexture && !depth->sampledTextureFilterable);
        for (const auto binding : {3u, 4u}) {
            const auto* sampler = findDescriptorSetBinding(ordinary.layout, binding);
            assert(sampler
                && !semantics::OriginalBindlessOrdinaryDepthDomain::inspectSelectedDeclarations(
                    *depth, *sampler, ShaderStage::Fragment));
        }
        for (const auto entry : {"cleanFragment", "faultFragment"}) {
            ShaderPackageDesc view;
            assert(appendBindlessShaderEntry(
                *package, "vertexMain", ShaderStage::Vertex, view, diagnostic));
            assert(appendBindlessShaderEntry(
                *package, entry, ShaderStage::Fragment, view, diagnostic));
            assert(validatePipelineLayoutAgainstShaderReflection(
                bindlessPipelineLayout(artifact), view.reflection)
                    .success);
        }
        auto device = createNullDevice();
        const auto* extension =
            static_cast<BindlessCrossExtension*>(device->queryInterface(kExtensionBindlessCross));
        BindlessPipelineInterfaceSummary summary;
        assert(!inspectBindlessPipelineInterface(extension->capabilities(), artifact, summary));
        // The summary counts occupied domains (indexed set1 + ordinary set2),
        // independently of the highest set number and native empty set0 slot.
        assert(summary.bindGroupCount == 2u && summary.descriptorCount == 8u
            && summary.additionalPerStage[1].sampledTextures == 2u
            && summary.additionalPerStage[1].samplers == 3u);
        std::puts(
            "BINDLESS_GRAPHICS_DEPTH_COLOR_PACKAGE_OK color-depth samplers3 selected-fragment compiler-loader-only no-execution-grant");
        return 0;
    }
    if (option == "graphics-root-images") {
        arp::test::validateOriginalBindlessRootSource(package,
            {},
            "computeMain",
            true,
            arp::test::OriginalOrdinaryOpaqueBindingFixture{true},
            arp::test::OriginalBindlessRootFixtureExecution{},
            false,
            "fragmentMain");
        return 0;
    }
    if (option == "pipeline-root-domains" || option == "pipeline-root-images"
        || option == "graphics-root-domains" || option == "color-gi-root") {
        const auto entry = option == "color-gi-root" ? "cleanMain" : "computeMain";
        if (option == "pipeline-root-images")
            arp::test::validateOriginalBindlessRootSource(
                package, {}, entry, true, arp::test::OriginalOrdinaryOpaqueBindingFixture{});
        else
            arp::test::validateOriginalBindlessRootSource(package, {}, entry);
        for (const auto mutation : {arp::test::OrdinaryRootMutation::DescriptorWrite,
                 arp::test::OrdinaryRootMutation::ArenaReset})
            arp::test::validateOriginalBindlessRootSource(package,
                {},
                entry,
                true,
                arp::test::OriginalBindlessRootFixtureObserver{},
                arp::test::OriginalBindlessRootFixtureExecution{},
                false,
                {},
                "vertexMain",
                false,
                mutation);
        std::cout
            << "Ordinary domain Root/2 canonical inventory and CPU journal validated; native execution closed\n";
        return 0;
    }
    if (option == "color-gi") {
        using namespace arp::rhi;
        const auto& artifact = package->artifact();
        assert(artifact.additionalDomains.size() == 2u && artifact.set == 1u
            && package->modules().size() == 2u);
        for (const auto entry : {"cleanMain", "faultMain"}) {
            ShaderPackageDesc view;
            assert(
                appendBindlessShaderEntry(*package, entry, ShaderStage::Compute, view, diagnostic));
            assert(validatePipelineLayoutAgainstShaderReflection(
                bindlessPipelineLayout(artifact), view.reflection)
                    .success);
        }
        auto device = createNullDevice();
        const auto* extension =
            static_cast<BindlessCrossExtension*>(device->queryInterface(kExtensionBindlessCross));
        BindlessPipelineInterfaceSummary summary;
        assert(!inspectBindlessPipelineInterface(extension->capabilities(), artifact, summary));
        assert(summary.bindGroupCount == 3u && summary.descriptorCount == 9u
            && summary.additionalPerStage[2].sampledTextures == 3u
            && summary.additionalPerStage[2].samplers == 1u
            && summary.additionalPerStage[2].uniformBuffers == 1u);
        std::cout
            << "ColorGi complete cube/2D/array regular-sampler ABI and device budgets validated; no execution grant\n";
        return 0;
    }
    if (option == "graphics-domains") {
        using namespace arp::rhi;
        const auto& artifact = package->artifact();
        assert(artifact.pipelineInterfaceContractVersion == 1u && artifact.set == 1u
            && artifact.additionalDomains.size() == 2u && package->modules().size() == 3u);
        for (const auto entry : {"cleanFragment", "faultFragment"}) {
            ShaderPackageDesc view;
            assert(appendBindlessShaderEntry(
                *package, "vertexMain", ShaderStage::Vertex, view, diagnostic));
            assert(appendBindlessShaderEntry(
                *package, entry, ShaderStage::Fragment, view, diagnostic));
            const auto layout = bindlessPipelineLayout(artifact);
            assert(layout.descriptorSets.size() == 3u
                && validatePipelineLayoutAgainstShaderReflection(layout, view.reflection).success);
            for (const auto& domain : artifact.additionalDomains)
                for (const auto& b : domain.layout.bindings) {
                    assert(b.arrayCount == 1u && !b.dynamicOffset);
                    assert(b.stages
                        == (b.type == DescriptorType::StorageBuffer
                                ? ShaderStage::Fragment
                                : ShaderStage::Vertex | ShaderStage::Fragment));
                    const auto found = std::find_if(view.reflection.descriptorBindings.begin(),
                        view.reflection.descriptorBindings.end(),
                        [&](const auto& actual) {
                            return actual.set == domain.set && actual.binding == b.binding;
                        });
                    assert(found != view.reflection.descriptorBindings.end()
                        && found->type == b.type && found->stages == b.stages);
                }
        }
        assert(
            semantics::BindlessPipelineInterfaceContract::standaloneSnapshotDiagnostic(artifact));
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            assert(bool(semantics::OriginalBindlessRootSourceLowering::nativeClosureDiagnostic(
                       backend, 2u))
                == (backend == Backend::Null));
        }
        std::cout
            << "Graphics Root/2 complete three-set Vertex/Fragment buffer ABI and four-backend refusal matrix validated\n";
        return 0;
    }
    if (option == "pipeline-domains") {
        using namespace arp::rhi;
        const auto& artifact = package->artifact();
        assert(artifact.pipelineInterfaceContractVersion == 1u
            && artifact.additionalDomains.size() == 2u);
        assert(!artifact.otherBindGroups && package->modules().size() == 2u);
        for (const auto& domain : artifact.additionalDomains) {
            assert(domain.set == 0u || domain.set == 2u);
            assert(domain.layout.bindings.size() == (domain.set == 0u ? 1u : 2u));
            for (const auto& binding : domain.layout.bindings) {
                assert(binding.arrayCount == 1u && !binding.dynamicOffset);
                assert(binding.stages
                    == (binding.type == DescriptorType::ReadOnlyStorageBuffer
                            ? ShaderStage::Compute
                            : ShaderStage::Compute | ShaderStage::Fragment));
            }
        }
        auto device = createNullDevice();
        const auto* extension =
            static_cast<BindlessCrossExtension*>(device->queryInterface(kExtensionBindlessCross));
        auto request = arp::test::BindlessPackageRequestFixture::make(
            artifact, extension->capabilities().deviceIdentity);
        BindlessPipelineInterfaceSummary summary;
        assert(!inspectBindlessPipelineInterface(extension->capabilities(), artifact, summary));
        assert(summary.bindGroupCount == 3u && summary.descriptorCount == 7u);
        assert(summary.additionalPerStage[2].storageBuffers == 2u
            && summary.additionalPerStage[1].storageBuffers == 1u
            && summary.additionalPerStage[2].uniformBuffers == 1u);
        const auto plan = planBindless(extension->capabilities(), request, artifact);
        assert(plan.success && !plan.executable && plan.descriptorCount == 4u);
        ShaderPackageDesc view;
        assert(appendBindlessShaderEntry(
            *package, "computeMain", ShaderStage::Compute, view, diagnostic));
        assert(appendBindlessShaderEntry(
            *package, "fragmentMain", ShaderStage::Fragment, view, diagnostic));
        assert(view.reflection.descriptorBindings.size()
            == artifact.physicalLayout.bindings.size() + 3u);
        for (const auto& domain : artifact.additionalDomains)
            for (const auto& binding : domain.layout.bindings) {
                const auto found = std::find_if(view.reflection.descriptorBindings.begin(),
                    view.reflection.descriptorBindings.end(),
                    [&](const auto& b) {
                        return b.set == domain.set && b.binding == binding.binding;
                    });
                assert(found != view.reflection.descriptorBindings.end()
                    && found->type == binding.type && found->stages == binding.stages);
            }
        auto caps = extension->capabilities();
        for (const auto execution : {BindlessExecution::Native, BindlessExecution::Emulated}) {
            caps.execution = execution;
            caps.executablePayload = true;
            const auto rejected = planBindless(caps, request, artifact);
            assert(!rejected.success
                && std::string_view(rejected.diagnosticId)
                    == "RHI-BINDLESS-PIPELINE-DOMAINS-EXECUTION-UNAVAILABLE");
        }
        const auto completeLayout = bindlessPipelineLayout(artifact);
        assert(completeLayout.descriptorSets.size() == 3u
            && completeLayout.descriptorLayouts.size() == 3u);
        assert(
            validatePipelineLayoutAgainstShaderReflection(completeLayout, view.reflection).success);
        caps.execution = artifact.execution;
        caps.executablePayload = true;
        caps.faultReadbackContractVersion = 1u;
        for (const auto kind : {3u, 4u, 5u})
            caps.ordinaryDescriptorDomains[kind] = true;
        const auto admitted = planBindless(caps, request, artifact);
        if (!admitted.success)
            std::cerr << admitted.diagnosticId << '\n';
        assert(admitted.success && admitted.executable);
        for (const auto kind : {3u, 4u, 5u}) {
            auto denied = caps;
            denied.ordinaryDescriptorDomains[kind] = false;
            const auto closed = planBindless(denied, request, artifact);
            assert(!closed.success
                && std::string_view(closed.diagnosticId)
                    == "RHI-BINDLESS-PIPELINE-DOMAINS-EXECUTION-UNAVAILABLE");
        }
        std::cout
            << "PipelineInterface/1 multi-set buffer ABI, exact budgets and closed execution validated\n";
        return 0;
    }
    if (option == "sampled-depth") {
        using namespace arp::rhi;
        const auto& artifact = package->artifact();
        assert(artifact.tables.size() == 3u && package->modules().size() == 4u);
        assert(artifact.tables[0].samplerTableId == 2u);
        std::uint32_t depthBindings = 0;
        for (const auto& binding : artifact.physicalLayout.bindings) {
            if (binding.type != DescriptorType::SampledTexture)
                continue;
            assert(binding.depthTexture && binding.textureViewDimensionConstrained
                && binding.textureViewDimension == TextureViewDimension::Texture2D);
            ++depthBindings;
        }
        assert(depthBindings == (artifact.execution == BindlessExecution::Emulated ? 2u : 1u));
        for (const auto& binding : artifact.physicalLayout.bindings)
            if (binding.type == DescriptorType::Sampler)
                assert(!binding.sampledTextureFilterable);
        for (const auto& module : package->modules()) {
            assert(module.spirvFile != UINT32_MAX && module.metalFile != UINT32_MAX);
            arp::core::StringView wgsl;
            assert(selectBindlessEmulatedWgsl(
                       *package, module.entry, ShaderStage::Compute, wgsl, diagnostic)
                == (artifact.execution == BindlessExecution::Emulated));
        }
        std::cout
            << "SampledDepth/1 compiler package and independent single-cell entry validated; no workload execution proof\n";
        return 0;
    }
    if (option == "graphics-fault-fixture" || option == "graphics-fault-metal") {
        validateGraphicsFaultPackage(std::move(package), option == "graphics-fault-metal");
        return 0;
    }
    if (option == "processor" || option == "processor-mixed") {
        assert(argc >= 5);
        auto producer = arp::rhi::BindlessShaderPackage::load(argv[4], budget, diagnostic);
        assert(producer);
        validateProcessorIntent(package, std::move(producer), option == "processor-mixed");
        return 0;
    }
    if (option == "closed-consumer-geometry") {
        validateClosedConsumerGeometry(*package);
        return 0;
    }
    if (option == "closed-consumer-recording") {
        assert(argc >= 5);
        auto producer = arp::rhi::BindlessShaderPackage::load(argv[4], budget, diagnostic);
        assert(producer);
        arp::test::validateCanonicalClosedConsumerRecording(package, std::move(producer));
        return 0;
    }
    if (option == "canonical-recording") {
        assert(argc >= 5);
        auto producer = arp::rhi::BindlessShaderPackage::load(argv[4], budget, diagnostic);
        assert(producer);
        validateCanonicalProducerRecording(package, std::move(producer));
        return 0;
    }
    if (option == "producer") {
        validateCanonicalSource(*package, true);
        const auto& artifact = package->artifact();
        assert(package->modules().size() == 1u && artifact.tables.size() == 4u);
        assert(package->modules()[0].entry == "arpZeroBaseIndirectMain");
        assert(artifact.zeroBaseIndirectProducerContractVersion == 1u);
        assert(artifact.indirectInstanceBaseContractVersion == 1u);
        assert(artifact.indirectInstanceConsumer.contractVersion == 0u);
        assert(artifact.physicalLayout.bindings.size() == 6u);
        assert(artifact.tables[3].bufferLayout.elementStrideBytes == 16u);
        assert(
            artifact.tables[3].bufferLayout.identity != artifact.tables[0].bufferLayout.identity);
        assert(!arp::rhi_webgpu::prepareWebGpuNativeZeroBaseIndirectProducer(
            *package, {}, diagnostic));
        assert(diagnostic
            == (artifact.execution == arp::rhi::BindlessExecution::Emulated
                    ? "RHI-WEBGPU-ZERO-BASE-PRODUCER-NATIVE-UNAVAILABLE"
                    : "RHI-WEBGPU-ZERO-BASE-PRODUCER-PACKAGE-INVALID"));
        return 0;
    }
    if (option == "closed-consumer-cold") {
        validateCanonicalConsumerColdResources(*package);
        return 0;
    }
    if (option == "closed-consumer" || option == "closed-consumer-negative") {
        validateCanonicalConsumer(*package, option == "closed-consumer");
        return 0;
    }
    if (option == "consumer") {
        const auto& artifact = package->artifact();
        assert(package->modules().size() == 3u && artifact.tables.size() == 6u);
        assert(artifact.indirectInstanceBaseContractVersion == 1u);
        assert(artifact.indirectInstanceConsumer.contractVersion == 1u);
        assert(artifact.indirectInstanceConsumer.binding == 0u);
        assert(artifact.indirectInstanceConsumer.recordIndex == 0u);
        assert(artifact.indirectInstanceConsumer.instanceCapacity == 2u);
        return 0;
    }
    if (option == "latch") {
        const auto& artifact = package->artifact();
        assert(package->modules().size() == 2 && artifact.tables.size() == 6);
        assert(artifact.faultLatch.contractVersion == 1u);
        assert(artifact.faultLatch.binding == 14u);
        assert(artifact.faultLatch.stages
            == (arp::rhi::ShaderStage::Compute | arp::rhi::ShaderStage::Fragment));
        assert(artifact.faultReportingContractVersion == 1u);
        return 0;
    }
    auto exactBudget = budget;
    exactBudget.maxTotalBytes = package->sourceBytes();
    assert(arp::rhi::BindlessShaderPackage::load(argv[1], exactBudget, diagnostic));
    --exactBudget.maxTotalBytes;
    assert(!arp::rhi::BindlessShaderPackage::load(argv[1], exactBudget, diagnostic));
    if (option == "completed-graphics-root" || option == "root-vertex-buffer"
        || option == "root-graphics-buffers" || option == "root-graphics-array"
        || option == "root-graphics-records") {
        using namespace arp::rhi;
        const auto& artifact = package->artifact();
        assert(artifact.tables.size()
                == (option == "root-vertex-buffer" ? 1u
                        : option == "root-graphics-buffers" || option == "root-graphics-records"
                        ? 3u
                        : 2u)
            && package->modules().size() == (option == "root-vertex-buffer" ? 2u : 3u));
        if (option == "root-graphics-records") {
            assert(artifact.tables[0].bufferLayout.elementStrideBytes == 32u
                && artifact.tables[1].bufferLayout.elementStrideBytes == 64u
                && artifact.tables[2].bufferLayout.elementStrideBytes == 48u);
        }
        if (option == "root-graphics-array")
            assert(artifact.physicalLayout.bindings[0].textureViewDimension
                    == TextureViewDimension::Texture2DArray
                && artifact.tables[0].samplerTableId == 2u
                && (static_cast<std::uint32_t>(artifact.tables[0].operations)
                       & static_cast<std::uint32_t>(BindlessOperation::SampleGrad))
                    != 0u);
        if (option == "completed-graphics-root")
            assert(artifact.tables[0].samplerTableId == 2u
                && artifact.snapshotMetadata.binding == 4u && artifact.faultLatch.binding == 5u);
        for (const auto& module : package->modules()) {
            const auto stage =
                module.entry == "vertexMain" ? ShaderStage::Vertex : ShaderStage::Fragment;
            assert(module.entry == "vertexMain" || module.entry == "fragmentMain"
                || module.entry == "cleanFragment" || module.entry == "faultFragment");
            assert(module.spirvFile != UINT32_MAX && module.metalFile != UINT32_MAX);
            arp::core::StringView wgsl;
            assert(selectBindlessEmulatedWgsl(*package, module.entry, stage, wgsl, diagnostic)
                == (artifact.execution == BindlessExecution::Emulated));
        }
        if (option == "root-graphics-array")
            arp::test::validateOriginalBindlessRootSource(package,
                {},
                "cleanMain",
                true,
                arp::test::OriginalBindlessRootFixtureObserver{},
                arp::test::OriginalBindlessRootFixtureExecution{},
                false,
                "cleanFragment");
        else
            arp::test::validateOriginalBindlessRootSource(package);
        if (option == "root-graphics-records")
            arp::test::validateOriginalBindlessRootSource(package,
                {},
                "cleanMain",
                true,
                arp::test::OriginalBindlessRootFixtureObserver{},
                arp::test::OriginalBindlessBufferRangeFixture{});
        if (option == "root-graphics-records") {
            arp::test::validateOriginalBindlessRootSource(package,
                {},
                "cleanMain",
                false,
                arp::test::OriginalBindlessGeometryFixture{},
                arp::test::OriginalBindlessGeometryDataFixture{});
            arp::test::OriginalBindlessGeometryDataFixture instances;
            instances.indirectAddresses = true;
            instances.instanceRate = true;
            arp::test::validateOriginalBindlessRootSource(package,
                {},
                "cleanMain",
                false,
                arp::test::OriginalBindlessGeometryFixture{},
                instances);
            arp::test::validateOriginalBindlessRootSource(package,
                {},
                "cleanMain",
                false,
                arp::test::OriginalBindlessGeometryFixture{},
                instances,
                true);
        }
        std::cout
            << "completed Graphics shader family: Native/Emulated SPIR-V/MSL/reflection/WGSL Root contracts; native draws closed\n";
        return 0;
    }
    if (option == "completed-texture-root" || option == "completed-array-root") {
        using namespace arp::rhi;
        const auto& artifact = package->artifact();
        assert(artifact.tables.size() == 3u && package->modules().size() == 2u);
        assert(artifact.tables[0].samplerTableId == 2u);
        assert(artifact.snapshotMetadata.binding == 6u && artifact.faultLatch.binding == 7u);
        assert(artifact.physicalLayout.bindings[0].textureViewDimension
            == (option == "completed-array-root" ? TextureViewDimension::Texture2DArray
                                                 : TextureViewDimension::Texture2D));
        for (const auto& module : package->modules()) {
            assert(module.entry == "cleanMain" || module.entry == "faultMain");
            assert(module.spirvFile != UINT32_MAX && module.metalFile != UINT32_MAX);
            arp::core::StringView wgsl;
            assert(selectBindlessEmulatedWgsl(
                       *package, module.entry, ShaderStage::Compute, wgsl, diagnostic)
                == (artifact.execution == BindlessExecution::Emulated));
        }
        arp::test::validateOriginalBindlessRootSource(package);
        std::cout
            << "completed texture shader family: Native/Emulated immutable package and Root contracts; no native sampling proof\n";
        return 0;
    }
    if (option == "images") {
        arp::test::validateOriginalBindlessRootSource(package);
        using namespace arp::rhi;
        const auto& artifact = package->artifact();
        const bool emulated = artifact.execution == BindlessExecution::Emulated;
        assert(artifact.tables.size() == 4u && package->modules().size() == 2u);
        assert(artifact.physicalLayout.bindings.size() == (emulated ? 10u : 6u));
        assert(artifact.tables[0].samplerTableId == 2u);
        assert(artifact.snapshotMetadata.binding == 8u && artifact.faultLatch.binding == 9u);
        assert(artifact.faultLatch.stages == ShaderStage::Compute
            && artifact.faultReportingContractVersion == 1u);
        assert(artifact.tables[3].bufferLayout.elementStrideBytes == 4u);
        assert(artifact.shaderPackageIdentity && package->sha256().size() == 64u);
        assert(artifact.snapshotBytes == 65536u);
        for (const auto& module : package->modules()) {
            assert(module.entry == "cleanMain" || module.entry == "faultMain");
            assert(module.reflection.reflection.entryPoints.size() == 1u
                && module.reflection.reflection.entryPoints[0].stage
                    == arp::shader::ShaderStage::Compute);
            assert(module.spirvFile != UINT32_MAX && module.metalFile != UINT32_MAX);
            arp::core::StringView wgsl;
            assert(selectBindlessEmulatedWgsl(
                       *package, module.entry, ShaderStage::Compute, wgsl, diagnostic)
                == emulated);
            if (emulated) {
                const auto prepared =
                    arp::rhi_webgpu::prepareBindlessWebGpuComputeInterface(*package, module.entry);
                if (!prepared.success)
                    std::cerr << prepared.diagnostics << '\n';
                assert(prepared.success && prepared.layout.descriptorLayouts.size() == 1u);
                assert(prepared.layout.descriptorLayouts[0].bindings.size() == 10u);
            }
        }
        return 0;
    }
    const auto& artifact = package->artifact();
    assert(artifact.tables.size() == 6 && package->modules().size() == 3);
    assert(artifact.tables[0].samplerTableId == 2);
    assert(artifact.tables[3].bufferLayout.elementStrideBytes == 16);
    assert(artifact.tables[4].bufferLayout.elementStrideBytes == 4);
    assert(artifact.tables[4].bufferLayout.identity == artifact.tables[5].bufferLayout.identity);
    assert(artifact.shaderPackageIdentity && package->sha256().size() == 64);
    assert(artifact.snapshotBytes
        == 65536u); // Authored offline budget is retained, not inferred as authorization.
    const bool emulated = artifact.execution == arp::rhi::BindlessExecution::Emulated;
    assert(artifact.switchCases == (emulated ? 32u : 0u));
    assert(artifact.physicalLayout.bindings.size() == (emulated ? 14u : 8u));
    validateMetalGraphicsStageLayouts(*package);
    arp::test::validateOriginalBindlessRootSource(package);
    if (validateNativeMetalStageConstruction)
        validateMetalRetainedGraphicsSources(*package);
    validateCompiledSnapshot(package, false);
    validateCompiledSnapshot(package, true);
    for (const auto& module : package->modules()) {
        assert(module.wgslFile != UINT32_MAX || !emulated);
        assert(
            module.reflection.reflection.bindlessExecution == (emulated ? "Emulated" : "Native"));
        arp::core::StringView wgsl;
        const auto stage = module.reflection.reflection.entryPoints[0].stage;
        const auto rhiStage = stage == arp::shader::ShaderStage::Compute
            ? arp::rhi::ShaderStage::Compute
            : stage == arp::shader::ShaderStage::Vertex ? arp::rhi::ShaderStage::Vertex
                                                        : arp::rhi::ShaderStage::Fragment;
        const auto selected = arp::rhi::selectBindlessEmulatedWgsl(
            *package, module.entry, rhiStage, wgsl, diagnostic);
        assert(selected == emulated);
        const auto lowering = arp::rhi_webgpu::loadBindlessWebGpuBindingSpaceLowering(
            *package, module.entry, rhiStage);
        assert(lowering.success == emulated);
        if (emulated) {
            assert(lowering.lowering.contractVersion == "WebGpuBindingSpace/5");
            assert(
                lowering.lowering.entries.size() == module.reflection.reflection.bindings.size());
            if (rhiStage == arp::rhi::ShaderStage::Compute) {
                const auto prepared =
                    arp::rhi_webgpu::prepareBindlessWebGpuComputeInterface(*package, module.entry);
                if (!prepared.success)
                    std::cerr << prepared.diagnostics << '\n';
                assert(prepared.success && prepared.shader.entrypoints.size() == 1);
                assert(prepared.layout.descriptorLayouts.size() == 1);
                assert(prepared.layout.descriptorLayouts[0].bindings.size()
                    == module.reflection.reflection.bindings.size());
                assert(!arp::rhi_webgpu::createWebGpuNativeBindlessComputePipelinePayload(
                    *package, module.entry, diagnostic));
                assert(diagnostic == "RHI-BINDLESS-WEBGPU-NATIVE-DEVICE-UNAVAILABLE");
            }
            if (rhiStage == arp::rhi::ShaderStage::Fragment) {
                const auto prepared = arp::rhi_webgpu::prepareBindlessWebGpuRenderInterface(
                    *package, "vertexMain", "fragmentMain");
                if (!prepared.success)
                    std::cerr << prepared.diagnostics << '\n';
                assert(prepared.success && prepared.shader.entrypoints.size() == 2);
                assert(prepared.layout.descriptorLayouts.size() == 1);
                assert(prepared.layout.descriptorLayouts[0].bindings.size()
                    >= module.reflection.reflection.bindings.size());
                assert(!arp::rhi_webgpu::prepareBindlessWebGpuRenderInterface(
                    *package, "computeMain", "fragmentMain")
                        .success);
                assert(!arp::rhi_webgpu::prepareBindlessWebGpuRenderInterface(
                    *package, "vertexMain", "computeMain")
                        .success);
                arp::rhi::RenderPipelineDesc state;
                state.label = "private bindless graphics";
                state.colorFormat = arp::rhi::TextureFormat::Rgba8Unorm;
                state.hasViewport = true;
                state.viewport = {0, 0, 2, 2, 0, 1};
                state.hasScissor = true;
                state.scissor = {0, 0, 2, 2};
                assert(!arp::rhi_webgpu::createWebGpuNativeBindlessRenderPipelinePayload(
                    *package, "vertexMain", "fragmentMain", state, diagnostic));
                assert(diagnostic == "RHI-BINDLESS-WEBGPU-NATIVE-DEVICE-UNAVAILABLE");
                auto unvalidated = state;
                unvalidated.colorFormat = arp::rhi::TextureFormat::Rgba16Float;
                assert(!arp::rhi_webgpu::createWebGpuNativeBindlessRenderPipelinePayload(
                    *package, "vertexMain", "fragmentMain", unvalidated, diagnostic));
                assert(diagnostic == "RHI-BINDLESS-WEBGPU-RENDER-STATE-UNVALIDATED");
            }
        }
        if (emulated) {
            const auto& retained = package->files()[module.wgslFile].bytes;
            assert(wgsl.size() == retained.size());
            assert(std::string_view(wgsl.data(), wgsl.size())
                == std::string_view(
                    reinterpret_cast<const char*>(retained.data()), retained.size()));
            assert(!arp::rhi::selectBindlessEmulatedWgsl(*package,
                module.entry,
                arp::rhi::ShaderStage::TessellationControl,
                wgsl,
                diagnostic));
            assert(wgsl.empty() && diagnostic == "RHI-BINDLESS-WEBGPU-ENTRY-MISMATCH");
        } else {
            assert(wgsl.empty() && diagnostic == "RHI-BINDLESS-WEBGPU-MODE-MISMATCH");
        }
    }
    {
        arp::core::StringView wgsl;
        assert(!arp::rhi::selectBindlessEmulatedWgsl(
            *package, "missingEntry", arp::rhi::ShaderStage::Compute, wgsl, diagnostic));
        assert(wgsl.empty());
    }
    const auto& target = package->files()[package->modules()[0].spirvFile];
    const auto path = std::filesystem::path(argv[1]).parent_path() / target.name.c_str();
    // File helpers must enforce limits before allocation, including exact boundary.
    assert(arp::core::readBinaryFile(path, target.bytes.size()).success);
    auto limited = arp::core::readBinaryFile(path, target.bytes.size() - 1);
    assert(!limited.success && limited.bytes.empty());
    if (option == "retain") {
        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            file << "changed";
        }
        const auto view = arp::core::StringView(
            reinterpret_cast<const char*>(target.bytes.data()), target.bytes.size());
        assert(arp::core::Sha256::matchesHexDigest(view, target.sha256));
        // Authorized pipeline creation/recording consume the retained package;
        // replacing the file after load must not reopen or switch its bytes.
        validateCompiledSnapshot(package, false);
        assert(!arp::rhi::BindlessShaderPackage::load(argv[1], budget, diagnostic));
        assert(diagnostic == "RHI-BINDLESS-PACKAGE-HASH-MISMATCH");
        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            file.write(reinterpret_cast<const char*>(target.bytes.data()), target.bytes.size());
        }
        if (emulated) {
            const auto& module = package->modules()[0];
            const auto& retained = package->files()[module.wgslFile].bytes;
            const auto wgslPath = std::filesystem::path(argv[1]).parent_path()
                / package->files()[module.wgslFile].name.c_str();
            {
                std::ofstream file(wgslPath, std::ios::binary | std::ios::trunc);
                file << "changed";
            }
            arp::core::StringView wgsl;
            const auto stage = module.reflection.reflection.entryPoints[0].stage;
            const auto rhiStage = stage == arp::shader::ShaderStage::Compute
                ? arp::rhi::ShaderStage::Compute
                : stage == arp::shader::ShaderStage::Vertex ? arp::rhi::ShaderStage::Vertex
                                                            : arp::rhi::ShaderStage::Fragment;
            assert(arp::rhi::selectBindlessEmulatedWgsl(
                *package, module.entry, rhiStage, wgsl, diagnostic));
            assert(arp::rhi_webgpu::loadBindlessWebGpuBindingSpaceLowering(
                *package, module.entry, rhiStage)
                    .success);
            if (rhiStage == arp::rhi::ShaderStage::Compute)
                assert(
                    arp::rhi_webgpu::prepareBindlessWebGpuComputeInterface(*package, module.entry)
                        .success);
            assert(std::string_view(wgsl.data(), wgsl.size())
                == std::string_view(
                    reinterpret_cast<const char*>(retained.data()), retained.size()));
            assert(!arp::rhi::BindlessShaderPackage::load(argv[1], budget, diagnostic));
            assert(diagnostic == "RHI-BINDLESS-PACKAGE-HASH-MISMATCH");
            {
                std::ofstream file(wgslPath, std::ios::binary | std::ios::trunc);
                file.write(reinterpret_cast<const char*>(retained.data()), retained.size());
            }
            const auto& reflection = package->files()[module.reflectionFile].bytes;
            const auto reflectionPath = std::filesystem::path(argv[1]).parent_path()
                / package->files()[module.reflectionFile].name.c_str();
            {
                std::ofstream file(reflectionPath, std::ios::binary | std::ios::trunc);
                file << "changed";
            }
            assert(arp::rhi_webgpu::loadBindlessWebGpuBindingSpaceLowering(
                *package, module.entry, rhiStage)
                    .success);
            assert(!arp::rhi::BindlessShaderPackage::load(argv[1], budget, diagnostic));
            assert(diagnostic == "RHI-BINDLESS-PACKAGE-HASH-MISMATCH");
            {
                std::ofstream file(reflectionPath, std::ios::binary | std::ios::trunc);
                file.write(reinterpret_cast<const char*>(reflection.data()), reflection.size());
            }
        }
    }
    return 0;
}
