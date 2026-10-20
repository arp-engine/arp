#include "renderer/gi/ddgi_renderer.h"
#include "rhi/backends/null/null_backend.h"
#include "tests/fixtures/capability_override_device.h"
#include "tests/fixtures/flat_terrain_surface.h"

#include <array>
#include <cassert>
#include <cstdio>

int main()
{
    using namespace arp;
    bool correct = true;
    for (unsigned domain = 0u; domain < 3u; ++domain) {
        for (bool pending : {false, true}) {
            auto device = rhi::createNullDevice();
            tests::CapabilityOverrideDevice proxy(*device, nullptr);
            renderer::DdgiRenderer ddgi(proxy);
            assert(
                ddgi.create(ARP_SHADER_DIR "/ddgi.slang",
                        {core::makeShared<tests::FlatTerrainSurface>(), 2u, 2u},
                        2u,
                        2u,
                        2u,
                        4u,
                        4u)
                    .success);
            renderer::DdgiSceneBvhTriangle triangle{};
            triangle.v0[0] = -1.0f;
            triangle.v1[0] = 1.0f;
            triangle.v2[2] = 1.0f;
            triangle.stableId = 17u;
            renderer::DdgiSceneBvhInput input;
            input.staticGeometryRevision = 1u;
            input.dynamicGeometryRevision = 1u;
            if (domain == 0u) {
                input.triangles = &triangle;
                input.triangleCount = 1u;
            }
            if (domain == 1u) {
                input.staticTriangles = &triangle;
                input.staticTriangleCount = 1u;
            }
            if (domain == 2u) {
                input.dynamicTriangles = &triangle;
                input.dynamicTriangleCount = 1u;
            }
            const auto initial = ddgi.updateSceneBvh(input);
            assert(initial.success && initial.triangleCount == 1u);
            renderer::DdgiPushConstants push{};
            for (unsigned axis = 0u; axis < 3u; ++axis)
                push.probeGridSpacingStrength[axis] = 1.0f;
            push.updateParams[0] = 2.0f;
            assert(ddgi.prepareProbeUpdateSchedule(push, 1u).success);
            auto fenceOwner =
                core::makeShared<core::UniquePtr<rhi::GpuFence>>(device->createFence());
            const std::shared_ptr<rhi::GpuFence> completion(fenceOwner, fenceOwner->get());
            if (pending)
                assert(ddgi.acceptPreparedInput(ddgi.preparedHistoryToken(), completion));
            else
                proxy.failBufferWrites(
                    domain == 2u ? "DdgiDynamicTlasLeaves" : "DdgiSceneBvhLeaves");
            const auto originalToken = ddgi.preparedHistoryToken();
            const auto reused = ddgi.updateSceneBvh(input);
            assert(reused.success && reused.reused && reused.uploadBytes == 0u);
            triangle.v0[1] = triangle.v1[1] = triangle.v2[1] = 1.0f;
            if (domain == 1u)
                input.staticGeometryRevision = 2u;
            else
                input.dynamicGeometryRevision = 2u;
            const auto changed = ddgi.updateSceneBvh(input);
            const bool rejected = !changed.success;
            if (pending) {
                assert(ddgi.preparedHistoryToken().generation == originalToken.generation);
                completion->signal(); // Null fixture completion only.
            } else {
                assert(
                    changed.diagnostic.find("TEST-BUFFER-UPLOAD-FAILED") != std::string_view::npos);
                assert(ddgi.preparedHistoryToken().generation == 0u);
                const auto unusable = ddgi.prepareProbeUpdateSchedule(push, 1u);
                assert(!unusable.success && unusable.diagnostics == "DDGI-BVH-INPUT-INVALID");
                // The old revision cannot make a partially changed buffer valid.
                input.staticGeometryRevision = input.dynamicGeometryRevision = 1u;
                triangle.v0[1] = triangle.v1[1] = triangle.v2[1] = 0.0f;
                assert(!ddgi.updateSceneBvh(input).success);
                proxy.failBufferWrites({});
            }
            const auto recovered = ddgi.updateSceneBvh(input);
            assert(recovered.success && !recovered.reused && recovered.uploadBytes != 0u);
            assert(ddgi.prepareProbeUpdateSchedule(push, 1u).success);
            const auto cleared = ddgi.updateSceneBvh({});
            assert(cleared.success && cleared.triangleCount == 0u);
            assert(ddgi.prepareProbeUpdateSchedule(push, 1u).success);
            std::fprintf(stderr,
                "Null BVH domain=%u pending=%d rejected=%d bytes=%llu diagnostic=%s\n",
                domain,
                pending,
                rejected,
                static_cast<unsigned long long>(changed.uploadBytes),
                changed.diagnostic.c_str());
            correct = rejected && correct;
        }
    }
    for (unsigned domain = 0u; domain < 3u; ++domain) {
        const std::array<const char*, 3> labels = domain == 0u
            ? std::array<const char*, 3>{"DdgiSceneBvhNodes",
                  "DdgiSceneBvhLeaves",
                  "DdgiSceneBvhEmissive"}
            : std::array<const char*, 3>{
                  "DdgiSplitBvhNodes", "DdgiSplitBvhLeaves", "DdgiSplitBvhEmissive"};
        for (const auto* label : labels) {
            auto device = rhi::createNullDevice();
            tests::CapabilityOverrideDevice proxy(*device, nullptr);
            renderer::DdgiRenderer ddgi(proxy);
            assert(
                ddgi.create(ARP_SHADER_DIR "/ddgi.slang",
                        {core::makeShared<tests::FlatTerrainSurface>(), 2u, 2u},
                        2u,
                        2u,
                        2u,
                        4u,
                        4u)
                    .success);
            std::array<renderer::DdgiSceneBvhTriangle, 129> triangles{};
            for (std::size_t i = 0u; i < triangles.size(); ++i) {
                auto& triangle = triangles[i];
                triangle.v0[0] = static_cast<float>(i) * 4.0f;
                triangle.v1[0] = triangle.v0[0] + 1.0f;
                triangle.v2[0] = triangle.v0[0];
                triangle.v2[2] = 1.0f;
                triangle.stableId = i + 1u;
            }
            renderer::DdgiSceneBvhInput input;
            input.staticGeometryRevision = input.dynamicGeometryRevision = 1u;
            if (domain == 0u) {
                input.triangles = triangles.data();
                input.triangleCount = triangles.size();
            }
            if (domain == 1u) {
                input.staticTriangles = triangles.data();
                input.staticTriangleCount = triangles.size();
            }
            if (domain == 2u) {
                input.dynamicTriangles = triangles.data();
                input.dynamicTriangleCount = triangles.size();
            }
            proxy.failBufferCreation(label);
            const auto failed = ddgi.updateSceneBvh(input);
            assert(!failed.success);
            proxy.failBufferCreation({});
            const auto retried = ddgi.updateSceneBvh(input);
            assert(retried.success && retried.triangleCount == triangles.size()
                && retried.uploadBytes != 0u);
            std::fprintf(stderr,
                "Null BVH allocation recovery: domain=%u label=%s recovered=1\n",
                domain,
                label);
        }
    }
    // Instance storage is fixed at create time; failed creation must remain
    // unrecordable and a fresh create must recover without leaked resources.
    {
        auto device = rhi::createNullDevice();
        tests::CapabilityOverrideDevice proxy(*device, nullptr);
        renderer::DdgiRenderer ddgi(proxy);
        proxy.failBufferCreation("DdgiSceneBvhInstances");
        const auto failed = ddgi.create(ARP_SHADER_DIR "/ddgi.slang",
            {core::makeShared<tests::FlatTerrainSurface>(), 2u, 2u},
            2u,
            2u,
            2u,
            4u,
            4u);
        assert(
            !failed.success && failed.diagnostics == "DDGI Phase 4 scene BVH SSBO creation failed");
        proxy.failBufferCreation({});
        assert(
            ddgi.create(ARP_SHADER_DIR "/ddgi.slang",
                    {core::makeShared<tests::FlatTerrainSurface>(), 2u, 2u},
                    2u,
                    2u,
                    2u,
                    4u,
                    4u)
                .success);
        ddgi.destroy();
        device->waitIdle();
        assert(device->memoryStats().allocatedBytes == 0u);
        std::fprintf(stderr, "Null BVH instance allocation failure/recreate retired=1\n");
    }
    // Flat and split representations alias their physical static buffers.
    // Equal numerical revisions cannot authorize cross-representation reuse.
    {
        auto device = rhi::createNullDevice();
        renderer::DdgiRenderer ddgi(*device);
        assert(
            ddgi.create(ARP_SHADER_DIR "/ddgi.slang",
                    {core::makeShared<tests::FlatTerrainSurface>(), 2u, 2u},
                    2u,
                    2u,
                    2u,
                    4u,
                    4u)
                .success);
        renderer::DdgiSceneBvhTriangle triangle{};
        triangle.v0[0] = -1.0f;
        triangle.v1[0] = 1.0f;
        triangle.v2[2] = 1.0f;
        renderer::DdgiSceneBvhInput flat;
        flat.triangles = &triangle;
        flat.triangleCount = 1u;
        flat.staticGeometryRevision = flat.dynamicGeometryRevision = 1u;
        auto split = flat;
        split.triangles = nullptr;
        split.triangleCount = 0u;
        split.staticTriangles = &triangle;
        split.staticTriangleCount = 1u;
        for (const auto* input : {&flat, &split, &flat, &split}) {
            const auto result = ddgi.updateSceneBvh(*input);
            assert(result.success && !result.reused && result.uploadBytes != 0u);
        }
        auto mixed = split;
        mixed.dynamicTriangles = &triangle;
        mixed.dynamicTriangleCount = 1u;
        assert(ddgi.updateSceneBvh(mixed).triangleCount == 2u);
        mixed.staticTriangleCount = 0u;
        const auto removedStatic = ddgi.updateSceneBvh(mixed);
        std::fprintf(stderr,
            "Null BVH remove static: success=%d count=%u static=%u reused=%d\n",
            removedStatic.success,
            removedStatic.triangleCount,
            removedStatic.staticTriangleCount,
            removedStatic.reused);
        assert(removedStatic.success && removedStatic.triangleCount == 1u
            && removedStatic.staticTriangleCount == 0u && !removedStatic.reused);
        mixed.staticTriangleCount = 1u;
        assert(ddgi.updateSceneBvh(mixed).triangleCount == 2u);
        mixed.dynamicTriangleCount = 0u;
        const auto removedDynamic = ddgi.updateSceneBvh(mixed);
        assert(removedDynamic.success && removedDynamic.triangleCount == 1u
            && removedDynamic.dynamicProxyCount == 0u && !removedDynamic.reused);
        auto invalid = split;
        invalid.staticTriangles = nullptr;
        assert(!ddgi.updateSceneBvh(invalid).success);
        invalid = split;
        renderer::DdgiSceneBvhInstance instance{};
        invalid.instances = &instance;
        invalid.instanceCount = renderer::DdgiRenderer::kMaximumSceneBvhInstances + 1u;
        assert(!ddgi.updateSceneBvh(invalid).success);
    }
    // Both completion orders protect all shared domains, including a pure
    // instance-table mutation with unchanged geometry revisions.
    for (unsigned domain = 0u; domain < 4u; ++domain) {
        for (unsigned firstCompleted : {0u, 1u}) {
            auto device = rhi::createNullDevice();
            tests::CapabilityOverrideDevice proxy(*device, nullptr);
            renderer::DdgiRenderer ddgi(proxy);
            assert(
                ddgi.create(ARP_SHADER_DIR "/ddgi.slang",
                        {core::makeShared<tests::FlatTerrainSurface>(), 2u, 2u},
                        2u,
                        2u,
                        2u,
                        4u,
                        4u)
                    .success);
            renderer::DdgiSceneBvhTriangle triangle{};
            triangle.v0[0] = -1;
            triangle.v1[0] = 1;
            triangle.v2[2] = 1;
            triangle.stableId = 17u;
            renderer::DdgiSceneBvhInstance instance{};
            const auto identity = core::Mat4::identity();
            std::copy(identity.m.begin(), identity.m.end(), instance.worldFromLocal);
            std::copy(identity.m.begin(), identity.m.end(), instance.localFromWorld);
            renderer::DdgiSceneBvhInput input;
            input.staticGeometryRevision = input.dynamicGeometryRevision =
                input.staticTransformRevision = 1u;
            if (domain == 0u) {
                input.triangles = &triangle;
                input.triangleCount = 1u;
            } else if (domain == 2u) {
                input.dynamicTriangles = &triangle;
                input.dynamicTriangleCount = 1u;
            } else {
                input.staticTriangles = &triangle;
                input.staticTriangleCount = 1u;
            }
            if (domain == 3u) {
                input.instances = &instance;
                input.instanceCount = 1u;
            }
            assert(ddgi.updateSceneBvh(input).success);
            renderer::DdgiPushConstants push{};
            for (unsigned axis = 0; axis < 3; ++axis)
                push.probeGridSpacingStrength[axis] = 1;
            push.updateParams[0] = 2;
            std::array<std::shared_ptr<rhi::GpuFence>, 2> completions;
            for (unsigned slot = 0; slot < 2; ++slot) {
                push.scrollOffsetFrame[3] = static_cast<float>(slot);
                assert(ddgi.prepareProbeUpdateSchedule(push, slot + 1u).success);
                auto owner =
                    core::makeShared<core::UniquePtr<rhi::GpuFence>>(device->createFence());
                completions[slot] = std::shared_ptr<rhi::GpuFence>(owner, owner->get());
                assert(ddgi.acceptPreparedInput(ddgi.preparedHistoryToken(), completions[slot]));
            }
            const auto reuse = ddgi.updateSceneBvh(input);
            assert(reuse.success && reuse.reused);
            const auto receipt = ddgi.preparedHistoryToken();
            const auto telemetry = ddgi.telemetry();
            const auto writes = proxy.bufferWriteAttempts();
            const auto allocations = proxy.bufferCreateAttempts();
            if (domain == 3u) {
                const auto transform = core::Mat4::translation({0, 1, 0});
                const auto inverse = core::inverse(transform);
                std::copy(transform.m.begin(), transform.m.end(), instance.worldFromLocal);
                std::copy(inverse.m.begin(), inverse.m.end(), instance.localFromWorld);
                ++input.staticTransformRevision;
            } else {
                triangle.v0[1] = triangle.v1[1] = triangle.v2[1] = 1;
                if (domain == 1u)
                    ++input.staticGeometryRevision;
                else
                    ++input.dynamicGeometryRevision;
            }
            for (unsigned readyCount = 0; readyCount < 2; ++readyCount) {
                const auto busy = ddgi.updateSceneBvh(input);
                assert(!busy.success && busy.diagnostic == "DDGI-SHARED-BVH-INPUT-NOT-READY");
                assert(proxy.bufferWriteAttempts() == writes
                    && proxy.bufferCreateAttempts() == allocations);
                assert(ddgi.preparedHistoryToken().generation == receipt.generation);
                assert(ddgi.telemetry().accelerationUploadBytes == telemetry.accelerationUploadBytes
                    && ddgi.telemetry().accelerationUploadRangeCount
                        == telemetry.accelerationUploadRangeCount
                    && ddgi.telemetry().accelerationReused == telemetry.accelerationReused);
                completions[readyCount == 0u ? firstCompleted : 1u - firstCompleted]->signal();
            }
            const auto resumed = ddgi.updateSceneBvh(input);
            assert(resumed.success && !resumed.reused && resumed.uploadBytes != 0u);
            assert(proxy.bufferWriteAttempts() > writes
                && ddgi.preparedHistoryToken().generation == 0u);
            if (domain == 3u)
                assert(resumed.uploadBytes == sizeof(renderer::DdgiSceneBvhInstance)
                    && resumed.uploadRangeCount == 1u);
            std::fprintf(stderr,
                "Null BVH two readers domain=%u firstCompleted=%u busy-no-write=1 resumed=1\n",
                domain,
                firstCompleted);
        }
    }
    return correct ? 0 : 1;
}
