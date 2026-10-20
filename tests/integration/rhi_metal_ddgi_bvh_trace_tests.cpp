#include "renderer/gi/ddgi_renderer.h"
#include "renderer/shader_package_reflection.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_native_resources.h"
#include "tests/fixtures/capability_override_device.h"
#include "tests/fixtures/flat_terrain_surface.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>

namespace {
struct TracePushConstants {
    std::array<float, 4> counts;
    std::array<float, 4> origin{0.0f, 5.0f, 0.25f, 0.0f};
    std::array<float, 4> direction{0.0f, -1.0f, 0.0f, 16.0f};
};

static_assert(sizeof(TracePushConstants) == 48u);

arp::renderer::DdgiSceneBvhInstance instance(const arp::core::Mat4& forward)
{
    arp::renderer::DdgiSceneBvhInstance result{};
    const auto inverse = arp::core::inverse(forward);
    std::copy(forward.m.begin(), forward.m.end(), result.worldFromLocal);
    std::copy(inverse.m.begin(), inverse.m.end(), result.localFromWorld);
    return result;
}

arp::renderer::DdgiSceneBvhInstance instance(float x, float y)
{
    return instance(arp::core::Mat4::translation({x, y, 0.0f}));
}

bool verifyTrace(arp::rhi::GpuDevice& device)
{
    using namespace arp;
    using namespace arp::rhi;
    tests::CapabilityOverrideDevice observed(device, nullptr);
    const std::array<const char*, 7> labels{"DdgiSceneBvhNodes",
        "DdgiSceneBvhLeaves",
        "DdgiSceneBvhEmissive",
        "DdgiDynamicTlasNodes",
        "DdgiDynamicTlasLeaves",
        "DdgiDynamicTlasEmissive",
        "DdgiSceneBvhInstances"};
    const std::array<std::uint32_t, 7> bindings{10u, 11u, 22u, 24u, 25u, 26u, 27u};
    for (std::size_t i = 0u; i < labels.size(); ++i)
        observed.observeBuffer(labels[i], i);
    renderer::DdgiRenderer ddgi(observed);
    assert(
        ddgi.create(ARP_SHADER_DIR "/ddgi.slang",
                {core::makeShared<tests::FlatTerrainSurface>(), 64u, 64u},
                2u,
                2u,
                2u,
                8u,
                8u)
            .success);
    DescriptorLayoutDesc descriptors;
    descriptors.name = "BvhTraceConsumer";
    descriptors.bindings.push_back({6u, DescriptorType::StorageBuffer, ShaderStage::Compute});
    for (const auto binding : bindings)
        descriptors.bindings.push_back(
            {binding, DescriptorType::ReadOnlyStorageBuffer, ShaderStage::Compute});
    PipelineLayoutDesc layoutDesc;
    layoutDesc.descriptorLayouts.push_back(descriptors);
    layoutDesc.descriptorSets.push_back({0u, descriptors.name});
    layoutDesc.pushConstantRanges.push_back({ShaderStage::Compute, 0u, sizeof(TracePushConstants)});
    auto layout = device.createPipelineLayout(layoutDesc);
    assert(layout);
    const core::pmr::String prefix = ARP_INPUT_SHADER_DIR "/ddgi_bvh_trace_consumer";
    ShaderPackageDesc package;
    package.label = "BvhTraceConsumer";
    package.slangModulePath = prefix + ".slang";
    package.entrypoints = {{package.slangModulePath,
        "computeMain",
        ShaderStage::Compute,
        ShaderSourceLanguage::Slang}};
    package.artifacts.spirvPath = prefix + ".spv";
    package.artifacts.mslPath = prefix + ".metal";
    package.artifacts.wgslPath = prefix + ".wgsl";
    package.artifacts.reflectionPath = prefix + ".reflection.yaml";
    for (const auto& binding : descriptors.bindings)
        package.reflection.descriptorBindings.push_back(makeShaderReflectionBinding(0u, binding));
    assert(renderer::hydrateShaderPackageReflectionFromArtifact(package));
    assert(package.reflection.pushConstantByteCount == sizeof(TracePushConstants));
    ComputePipelineDesc pipelineDesc;
    pipelineDesc.label = "BvhTraceConsumer";
    pipelineDesc.layout = layout.get();
    pipelineDesc.shaderPackage = &package;
    pipelineDesc.computeShader = package.entrypoints[0];
    auto pipeline = device.createComputePipeline(pipelineDesc);
    assert(pipeline);
    auto output = device.createBuffer({"BvhTraceOutput",
        32u,
        ResourceUsage::StorageBuffer | ResourceUsage::CopySrc,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined});
    auto readback = device.createBuffer({"BvhTraceReadback",
        32u,
        ResourceUsage::CopyDst,
        MemoryUsage::GpuToCpu,
        ResourceState::CopyDst});
    auto arena = device.createDescriptorArena({"BvhTraceConsumer", 1u, 8u});
    assert(output && readback && arena);
    auto set = arena->allocateDescriptorSet({"BvhTraceConsumer", descriptors});
    assert(set);
    assert(
        set->write(
               {6u, DescriptorType::StorageBuffer, output->id(), DescriptorResourceKind::Buffer})
            .success);
    const auto record = [&](const TracePushConstants& pc, bool writeBindings = true) {
        auto encoder = device.createCommandEncoder({"BvhTraceConsumer"});
        assert(encoder);
        for (std::size_t i = 0u; i < labels.size(); ++i) {
            auto* buffer = observed.observedBuffer(i);
            assert(buffer);
            if (writeBindings)
                assert(
                    set->write({bindings[i],
                                   DescriptorType::ReadOnlyStorageBuffer,
                                   buffer->id(),
                                   DescriptorResourceKind::Buffer})
                        .success);
            if (buffer->state() != ResourceState::ShaderRead)
                assert(encoder->transition(*buffer, buffer->state(), ResourceState::ShaderRead)
                        .success);
        }
        assert(encoder->transition(*output, output->state(), ResourceState::ShaderWrite).success);
        auto pass = encoder->beginComputePass({"BvhTraceConsumer"});
        assert(pass && pass->setPipeline(*pipeline).success);
        assert(pass->bindDescriptorSet(0u, *set).success);
        assert(pass->pushConstants(ShaderStage::Compute, 0u, sizeof(pc), &pc).success);
        assert(pass->dispatch(1u, 1u, 1u).success && pass->end().success);
        assert(encoder->transition(*output, ResourceState::ShaderWrite, ResourceState::CopySrc)
                .success);
        auto copy = encoder->beginTransferPass({"BvhTraceReadback"});
        assert(
            copy && copy->copyBufferToBuffer({output->id(), readback->id(), 0u, 0u, 32u}).success);
        assert(copy->end().success);
        return encoder->finish();
    };
    const auto read = [&] {
        std::array<float, 8> sample{};
        core::pmr::String diagnostic;
        assert(rhi_metal::readMetalRhiNativeBuffer(rhi_metal::metalRhiNativePayloadFor(*readback),
            sample.data(),
            sizeof(sample),
            0u,
            sizeof(sample),
            diagnostic));
        return sample;
    };
    const auto trace = [&](const TracePushConstants& pc) {
        auto commands = record(pc);
        auto completion = device.createFence();
        assert(commands && completion
            && device.queue().submit(*commands, completion.get()).hasAcceptedWork());
        completion->wait();
        device.waitIdle();
        return read();
    };
    bool correct = true;
    const auto updateAndTrace = [&](const renderer::DdgiSceneBvhInput& input,
                                    std::array<float, 4> origin = {0, 5, 0.25f, 0},
                                    std::array<float, 4> direction = {0, -1, 0, 16}) {
        const auto result = ddgi.updateSceneBvh(input);
        assert(result.success);
        if (!result.reused && result.instanceCount != 0u
            && (input.staticTriangles || input.dynamicTriangles)) {
            const bool accounted =
                result.uploadBytes >= result.instanceCount * sizeof(renderer::DdgiSceneBvhInstance)
                && result.uploadRangeCount != 0u;
            if (!accounted)
                std::fprintf(stderr,
                    "BVH instance upload missing from telemetry: bytes=%llu ranges=%u count=%u\n",
                    static_cast<unsigned long long>(result.uploadBytes),
                    result.uploadRangeCount,
                    result.instanceCount);
            correct = accounted && correct;
        }
        return trace({result.shaderParameters(), origin, direction});
    };
    const auto check = [&](const char* label,
                           const std::array<float, 8>& sample,
                           float distance,
                           float material,
                           float count,
                           bool admitted = true,
                           core::Vec3 normal = {0, 1, 0},
                           float emissive = 0.5f) {
        // These analytic unit-scale fixtures need only float32 arithmetic
        // tolerance; material/count/admission metadata must match exactly.
        const auto near = [](float a, float b) {
            return std::abs(a - b) <= 1.0e-5f;
        };
        const bool matched = near(sample[0], distance) && sample[1] == material
            && sample[4] == count && near(sample[5], normal.x) && near(sample[2], normal.y)
            && near(sample[6], normal.z) && sample[3] == (distance > 0 ? emissive : 0.0f)
            && sample[7] == (admitted ? 1.0f : 0.0f);
        std::fprintf(stderr,
            "BVH ray %s: distance=%g material=%g normal=(%g,%g,%g) emissive=%g instances=%g admitted=%g expected=%g/%g/%g normal=(%g,%g,%g) correct=%d\n",
            label,
            sample[0],
            sample[1],
            sample[5],
            sample[2],
            sample[6],
            sample[3],
            sample[4],
            sample[7],
            distance,
            material,
            count,
            normal.x,
            normal.y,
            normal.z,
            matched);
        correct = correct && matched;
    };
    renderer::DdgiSceneBvhTriangle triangle{};
    triangle.v0[0] = -1.0f;
    triangle.v1[0] = 1.0f;
    triangle.v2[2] = 1.0f;
    triangle.materialIdx = 3u;
    triangle.stableId = 17u;
    triangle.emissiveRadiance[0] = 0.5f;
    renderer::DdgiSceneBvhInput flat;
    flat.triangles = &triangle;
    flat.triangleCount = 1u;
    flat.staticGeometryRevision = flat.dynamicGeometryRevision = 1u;
    check("flat-initial", updateAndTrace(flat), 5, 3, 1);

    std::array<renderer::DdgiSceneBvhInstance, 2> instances{instance(0, 0), instance(0, 2)};
    renderer::DdgiSceneBvhInput split;
    split.staticTriangles = &triangle;
    split.staticTriangleCount = 1u;
    split.instances = instances.data();
    split.instanceCount = 1u;
    split.staticGeometryRevision = split.dynamicGeometryRevision = 1u;
    split.staticTransformRevision = 1u;
    check("split-identity", updateAndTrace(split), 5, 3, 1);
    instances[0] = instance(20, 0);
    split.instanceCount = 2u;
    split.staticTransformRevision = 2u;
    check("split-two-instances", updateAndTrace(split), 3, 3, 2);
    split.instanceCount = 1u;
    check("instance-count-shrink", updateAndTrace(split), -1, 0, 1);
    split.instanceCount = 2u;
    check("instance-count-expand", updateAndTrace(split), 3, 3, 2);

    check("split-to-flat", updateAndTrace(flat), 5, 3, 1);

    auto dynamic = triangle;
    dynamic.v0[1] = dynamic.v1[1] = dynamic.v2[1] = 3.0f;
    dynamic.materialIdx = 2u;
    dynamic.stableId = 29u;
    instances[0] = instance(0, 0);
    split.instanceCount = 1u;
    split.staticTransformRevision = 3u;
    split.dynamicTriangles = &dynamic;
    split.dynamicTriangleCount = 1u;
    check("nearest-dynamic", updateAndTrace(split), 2, 2, 1);
    split.dynamicTriangleCount = 0u;
    check("remove-dynamic", updateAndTrace(split), 5, 3, 1);
    split.staticTriangleCount = 0u;
    split.dynamicTriangleCount = 1u;
    check("remove-static", updateAndTrace(split), 2, 2, 1);

    // Leave populated dynamic storage behind while flat mode publishes zero
    // dynamic counts. Proxy telemetry and frame identity are not tree counts.
    std::array<renderer::DdgiSceneBvhTriangle, 2> flatWithProxy{triangle, {}};
    auto& proxy = flatWithProxy[1];
    proxy.v0[0] = 20;
    proxy.v0[1] = -1;
    proxy.v0[2] = -1;
    proxy.v1[0] = 22;
    proxy.v1[1] = 1;
    proxy.v1[2] = 1;
    proxy.materialIdx = renderer::kDdgiDynamicProxyMaterial;
    proxy.stableId = 31u;
    flat.triangles = flatWithProxy.data();
    flat.triangleCount = flatWithProxy.size();
    ++flat.staticGeometryRevision;
    const auto flatResult = ddgi.updateSceneBvh(flat);
    assert(flatResult.success && flatResult.proxyAabbCount == 1u);
    check("flat-ignores-stale-dynamic", trace({flatResult.shaderParameters()}), 5, 3, 1);
    const std::array<float, 4> historicalTuple{static_cast<float>(flatResult.triangleCount),
        static_cast<float>(flatResult.nodeCount),
        static_cast<float>(flatResult.proxyAabbCount),
        17.0f};
    check("historical-tuple-negative-control", trace({historicalTuple}), 2, 2, 1);
    check("empty", updateAndTrace({}), -1, 0, 1, false);

    split.staticTriangleCount = 1u;
    split.dynamicTriangleCount = 0u;
    instances[0] = instance(core::Mat4::scale({1.0f / 64, 1.0f / 64, 1.0f / 64}));
    ++split.staticTransformRevision;
    check("small-uniform-scale", updateAndTrace(split, {0, 5, 0.25f / 64, 0}), 5, 3, 1);

    // Local y=z becomes world y=z/2. The reference is computed from
    // transformed vertices, independently of the shader's normal transform.
    triangle.v2[1] = 1.0f;
    ++split.staticGeometryRevision;
    instances[0] = instance(core::Mat4::scale({1, 1, 2}));
    ++split.staticTransformRevision;
    const auto normal = core::normalize(core::cross(core::Vec3{0, 1, 2}, core::Vec3{2, 0, 0}));
    check("nonuniform-normal", updateAndTrace(split, {0, 5, 0.5f, 0}), 4.75f, 3, 1, true, normal);
    check("world-distance-bound", updateAndTrace(split, {0, 5, 0.5f, 0}, {0, -1, 0, 4}), -1, 0, 1);

    triangle.v2[1] = 0;
    ++split.staticGeometryRevision;
    const auto rotation = core::lookAt({0, 0, 0}, {0, 1, 0}, {0, 0, 1});
    instances[0] = instance(rotation);
    ++split.staticTransformRevision;
    const auto center = core::transformPoint(rotation, {0, 0, 0.25f});
    const auto rotatedNormal = core::transformVector(rotation, {0, 1, 0});
    const auto origin = center + core::scale(rotatedNormal, 5.0f);
    check("rotated-instance",
        updateAndTrace(split,
            {origin.x, origin.y, origin.z, 0},
            {-rotatedNormal.x, -rotatedNormal.y, -rotatedNormal.z, 16}),
        5,
        3,
        1,
        true,
        rotatedNormal);

    renderer::DdgiPushConstants prepare{};
    for (unsigned axis = 0; axis < 3; ++axis)
        prepare.probeGridSpacingStrength[axis] = 1;
    prepare.updateParams[0] = 2;
    // Actual native uploads occur before the injected error return. The
    // failed physical version must not be prepared, even with an old key.
    for (const auto* label : labels) {
        for (const bool oldRevision : {false, true}) {
            instances[0] = instance(0, 0);
            triangle.v0[1] = triangle.v1[1] = triangle.v2[1] = 0;
            triangle.emissiveRadiance[0] = 0.5f;
            dynamic.v0[1] = dynamic.v1[1] = dynamic.v2[1] = -1;
            dynamic.emissiveRadiance[0] = 0.5f;
            split.staticTriangleCount = split.dynamicTriangleCount = 1u;
            ++split.staticGeometryRevision;
            ++split.dynamicGeometryRevision;
            ++split.staticTransformRevision;
            check("before-partial-upload", updateAndTrace(split), 5, 3, 1);
            const auto original = split;
            assert(ddgi.prepareProbeUpdateSchedule(prepare, 1u).success);
            triangle.v0[1] = triangle.v1[1] = triangle.v2[1] = 1;
            triangle.emissiveRadiance[0] = 0.75f;
            dynamic.v0[1] = dynamic.v1[1] = dynamic.v2[1] = 3;
            dynamic.emissiveRadiance[0] = 0.75f;
            instances[0] = instance(0, 0.5f);
            ++split.staticGeometryRevision;
            ++split.dynamicGeometryRevision;
            ++split.staticTransformRevision;
            observed.failBufferWrites(label, true);
            const auto failed = ddgi.updateSceneBvh(split);
            assert(!failed.success
                && failed.diagnostic.find("TEST-BUFFER-UPLOAD-FAILED-AFTER-WRITE")
                    != std::string_view::npos);
            assert(ddgi.preparedHistoryToken().generation == 0u);
            const auto rejected = ddgi.prepareProbeUpdateSchedule(prepare, 1u);
            assert(!rejected.success && rejected.diagnostics == "DDGI-BVH-INPUT-INVALID");
            observed.failBufferWrites({});
            if (oldRevision) {
                split = original;
                instances[0] = instance(0, 0);
                triangle.v0[1] = triangle.v1[1] = triangle.v2[1] = 0;
                dynamic.v0[1] = dynamic.v1[1] = dynamic.v2[1] = -1;
                triangle.emissiveRadiance[0] = dynamic.emissiveRadiance[0] = 0.5f;
            }
            check("recovered-partial-upload",
                updateAndTrace(split),
                oldRevision ? 5 : 2,
                oldRevision ? 3 : 2,
                1,
                true,
                {0, 1, 0},
                oldRevision ? 0.5f : 0.75f);
            assert(ddgi.prepareProbeUpdateSchedule(prepare, 1u).success);
            std::fprintf(stderr,
                "BVH actual-upload recovery label=%s oldRevision=%d diagnostic=%s\n",
                label,
                oldRevision,
                failed.diagnostic.c_str());
        }
    }
    renderer::DdgiSceneBvhInput flatRetry;
    flatRetry.triangles = &triangle;
    flatRetry.triangleCount = 1u;
    flatRetry.staticGeometryRevision = flatRetry.dynamicGeometryRevision = 1000u;
    for (const unsigned labelIndex : {0u, 1u, 2u, 6u}) {
        for (const bool oldRevision : {false, true}) {
            triangle.v0[1] = triangle.v1[1] = triangle.v2[1] = 0;
            triangle.emissiveRadiance[0] = 0.5f;
            ++flatRetry.staticGeometryRevision;
            ++flatRetry.dynamicGeometryRevision;
            check("flat-before-partial-upload", updateAndTrace(flatRetry), 5, 3, 1);
            const auto original = flatRetry;
            if (labelIndex == 6u) {
                // The flat identity table is rewritten on mode change, not on
                // an ordinary flat geometry update. Exercise that real write.
                auto splitBeforeFlat = flatRetry;
                splitBeforeFlat.triangles = nullptr;
                splitBeforeFlat.triangleCount = 0u;
                splitBeforeFlat.staticTriangles = &triangle;
                splitBeforeFlat.staticTriangleCount = 1u;
                check(
                    "split-before-flat-identity-upload", updateAndTrace(splitBeforeFlat), 5, 3, 1);
            }
            assert(ddgi.prepareProbeUpdateSchedule(prepare, 1u).success);
            triangle.v0[1] = triangle.v1[1] = triangle.v2[1] = 1;
            triangle.emissiveRadiance[0] = 0.75f;
            ++flatRetry.staticGeometryRevision;
            ++flatRetry.dynamicGeometryRevision;
            observed.failBufferWrites(labels[labelIndex], true);
            const auto failed = ddgi.updateSceneBvh(flatRetry);
            assert(!failed.success
                && failed.diagnostic.find("TEST-BUFFER-UPLOAD-FAILED-AFTER-WRITE")
                    != std::string_view::npos);
            assert(ddgi.preparedHistoryToken().generation == 0u);
            const auto rejected = ddgi.prepareProbeUpdateSchedule(prepare, 1u);
            assert(!rejected.success && rejected.diagnostics == "DDGI-BVH-INPUT-INVALID");
            observed.failBufferWrites({});
            if (oldRevision) {
                flatRetry = original;
                triangle.v0[1] = triangle.v1[1] = triangle.v2[1] = 0;
                triangle.emissiveRadiance[0] = 0.5f;
            }
            check("flat-recovered-partial-upload",
                updateAndTrace(flatRetry),
                oldRevision ? 5 : 4,
                3,
                1,
                true,
                {0, 1, 0},
                oldRevision ? 0.5f : 0.75f);
            assert(ddgi.prepareProbeUpdateSchedule(prepare, 1u).success);
            std::fprintf(stderr,
                "BVH flat actual-upload recovery label=%s oldRevision=%d diagnostic=%s\n",
                labels[labelIndex],
                oldRevision,
                failed.diagnostic.c_str());
        }
    }
    // Both readers execute real traversal against old storage. FIFO queue
    // order is unchanged; reversing which slot owns the first submission
    // exercises both slot-completion orders without fabricated GPU signals.
    for (unsigned domain = 0; domain < 4; ++domain) {
        for (unsigned firstSlot : {0u, 1u}) {
            triangle.v0[1] = triangle.v1[1] = triangle.v2[1] = 0;
            triangle.emissiveRadiance[0] = 0.5f;
            instances[0] = instance(0, 0);
            renderer::DdgiSceneBvhInput leased;
            leased.staticGeometryRevision = split.staticGeometryRevision + 1u;
            leased.dynamicGeometryRevision = split.dynamicGeometryRevision + 1u;
            leased.staticTransformRevision = split.staticTransformRevision + 1u;
            if (domain == 0u) {
                leased.triangles = &triangle;
                leased.triangleCount = 1u;
            } else if (domain == 2u) {
                leased.dynamicTriangles = &triangle;
                leased.dynamicTriangleCount = 1u;
            } else {
                leased.staticTriangles = &triangle;
                leased.staticTriangleCount = 1u;
            }
            if (domain == 3u) {
                leased.instances = instances.data();
                leased.instanceCount = 1u;
            }
            const auto initial = ddgi.updateSceneBvh(leased);
            assert(initial.success);
            const auto fence = [&] {
                auto owner = core::makeShared<core::UniquePtr<GpuFence>>(device.createFence());
                return std::shared_ptr<GpuFence>(owner, owner->get());
            };
            auto firstDone = fence();
            auto secondDone = fence();
            auto gate = device.createTimelineSemaphore(0u);
            assert(firstDone && secondDone && gate);
            prepare.scrollOffsetFrame[3] = static_cast<float>(firstSlot);
            assert(ddgi.prepareProbeUpdateSchedule(prepare, 1u).success);
            auto firstCommands = record({initial.shaderParameters()});
            assert(firstCommands);
            assert(device.queue()
                    .submit(*firstCommands, gate.get(), 1u, nullptr, 0u, firstDone.get())
                    .hasAcceptedWork());
            const bool firstAccepted =
                ddgi.acceptPreparedInput(ddgi.preparedHistoryToken(), firstDone);
            prepare.scrollOffsetFrame[3] = static_cast<float>(1u - firstSlot);
            const auto secondPrepared = ddgi.prepareProbeUpdateSchedule(prepare, 2u);
            // Never rewrite a descriptor while its first recorded reader is
            // pending. Both packets consume the same immutable bindings.
            auto secondCommands = record({initial.shaderParameters()}, false);
            assert(secondCommands);
            const auto secondSubmitted = device.queue().submit(
                *secondCommands, gate.get(), 2u, nullptr, 0u, secondDone.get());
            const bool secondAccepted = secondSubmitted.hasAcceptedWork()
                && ddgi.acceptPreparedInput(ddgi.preparedHistoryToken(), secondDone);
            const bool pending = !firstDone->isSignaled() && !secondDone->isSignaled();
            const auto reused = ddgi.updateSceneBvh(leased);
            const auto receipt = ddgi.preparedHistoryToken();
            const auto writes = observed.bufferWriteAttempts();
            const auto allocations = observed.bufferCreateAttempts();
            if (domain == 3u) {
                instances[0] = instance(0, 1);
                ++leased.staticTransformRevision;
            } else {
                triangle.v0[1] = triangle.v1[1] = triangle.v2[1] = 1;
                if (domain == 1u)
                    ++leased.staticGeometryRevision;
                else
                    ++leased.dynamicGeometryRevision;
            }
            const auto bothBusy = ddgi.updateSceneBvh(leased);
            const bool bothPreserved = observed.bufferWriteAttempts() == writes
                && observed.bufferCreateAttempts() == allocations
                && ddgi.preparedHistoryToken().generation == receipt.generation;
            gate->signal(1u);
            firstDone->wait();
            const auto firstSample = read();
            const bool secondPending = !secondDone->isSignaled();
            const auto oneBusy = ddgi.updateSceneBvh(leased);
            const bool onePreserved = observed.bufferWriteAttempts() == writes
                && observed.bufferCreateAttempts() == allocations
                && ddgi.preparedHistoryToken().generation == receipt.generation;
            gate->signal(2u);
            secondDone->wait();
            device.waitIdle();
            const auto secondSample = read();
            // Check observations only after releasing all native wait gates.
            assert(firstAccepted && secondPrepared.success && secondAccepted && pending
                && secondPending);
            assert(reused.success && reused.reused && reused.uploadBytes == 0u);
            assert(!bothBusy.success && !oneBusy.success
                && bothBusy.diagnostic == "DDGI-SHARED-BVH-INPUT-NOT-READY"
                && oneBusy.diagnostic == bothBusy.diagnostic && bothPreserved && onePreserved);
            check("first-pending-reader", firstSample, 5, 3, 1);
            check("second-pending-reader", secondSample, 5, 3, 1);
            check("after-both-complete", updateAndTrace(leased), 4, 3, 1);
            std::fprintf(stderr,
                "BVH native two readers domain=%u firstSlot=%u pending=1 busy-no-write=1 recovered=1\n",
                domain,
                firstSlot);
            split.staticGeometryRevision = leased.staticGeometryRevision;
            split.dynamicGeometryRevision = leased.dynamicGeometryRevision;
            split.staticTransformRevision = leased.staticTransformRevision;
        }
    }
    // Cross the initial 128-leaf/256-node capacities in each representation.
    // The fixture follows the explicit replacement labels only for the tree
    // being grown; static and dynamic split buffers otherwise share labels.
    std::array<renderer::DdgiSceneBvhTriangle, 258> growth{};
    for (std::size_t i = 0; i < growth.size(); ++i) {
        growth[i] = triangle;
        const float x = static_cast<float>(i) * 4.0f;
        growth[i].v0[0] = x - 1;
        growth[i].v1[0] = x + 1;
        growth[i].v2[0] = x;
        growth[i].v0[1] = growth[i].v1[1] = growth[i].v2[1] = 0;
        growth[i].stableId = i + 1u;
    }
    const std::array<const char*, 3> splitLabels{
        "DdgiSplitBvhNodes", "DdgiSplitBvhLeaves", "DdgiSplitBvhEmissive"};
    for (std::size_t i = 0; i < splitLabels.size(); ++i)
        observed.observeBuffer(splitLabels[i], i);
    renderer::DdgiSceneBvhInput grown;
    grown.staticTriangles = growth.data();
    grown.staticTriangleCount = 129u;
    grown.staticGeometryRevision = grown.dynamicGeometryRevision = grown.staticTransformRevision =
        100u;
    check("static-capacity-growth", updateAndTrace(grown), 5, 3, 1);
    for (std::size_t i = 0; i < splitLabels.size(); ++i)
        observed.observeBuffer(labels[i], i);
    grown.staticTriangles = nullptr;
    grown.staticTriangleCount = 0u;
    grown.triangles = growth.data();
    grown.triangleCount = growth.size();
    check("flat-capacity-growth", updateAndTrace(grown), 5, 3, 1);
    for (std::size_t i = 0; i < splitLabels.size(); ++i)
        observed.observeBuffer(splitLabels[i], i + 3u);
    for (auto& item : growth)
        item.v0[1] = item.v1[1] = item.v2[1] = 1;
    grown.triangles = nullptr;
    grown.triangleCount = 0u;
    grown.dynamicTriangles = growth.data();
    grown.dynamicTriangleCount = 129u;
    check("dynamic-capacity-growth", updateAndTrace(grown), 4, 3, 1);
    triangle.v0[1] = triangle.v1[1] = triangle.v2[1] = 2;
    grown.staticTriangles = &triangle;
    grown.staticTriangleCount = 1u;
    ++grown.staticGeometryRevision;
    check("add-static-after-growth", updateAndTrace(grown), 3, 3, 1);
    grown.staticTriangleCount = 0u;
    check("remove-static-after-growth", updateAndTrace(grown), 4, 3, 1);
    grown.dynamicTriangleCount = 0u;
    check("remove-all-after-growth", updateAndTrace(grown), -1, 0, 1, false);
    return correct;
}
} // namespace

int main()
{
    using namespace arp;
    rhi_metal::MetalRhiDeviceDesc desc;
    desc.capabilityContract = rhi::semantics::nativeBackendCapabilityContract(rhi::Backend::Metal);
    desc.capabilityContract.queriedFromNativeDevice = true;
    desc.capabilityContract.queriedExtendedFeatures = true;
    desc.memoryBudgetBytes = 64u * 1024u * 1024u;
    desc.uploadStagingPageSizeBytes = 128u * 1024u;
    desc.uploadStagingMaxPageCount = 8u;
    auto device = rhi_metal::createMetalRhiDevice(desc);
    assert(device);
    const bool correct = verifyTrace(*device);
    device->waitIdle();
    const bool retired = device->memoryStats().allocatedBytes == 0u;
    std::fprintf(stderr, "BVH production traversal correct=%d retired=%d\n", correct, retired);
    return correct && retired ? 0 : 1;
}
