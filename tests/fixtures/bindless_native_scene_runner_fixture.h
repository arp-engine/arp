#pragma once
#include "bindless_native_scene_execution_fixture.h"

namespace arp::test {
class BindlessNativeSceneRunnerFixture final {
public:
    template <class CompletedTextures,
        class Closure,
        class Pass,
        class Content,
        class DeviceFactory>
    static void run(const std::shared_ptr<const rhi::BindlessShaderPackage>& package,
        std::uint64_t nativeStagingCeiling,
        DeviceFactory&& createDevice)
    {
        using namespace rhi;
        using Contract = OriginalBindlessSceneContentContract;
        using Data = BindlessNativeSceneDataFixture<CompletedTextures>;
        assert(package && package->artifact().execution == BindlessExecution::Native);
        std::size_t entries = 0u;
        for (const auto& vertex : package->modules()) {
            if (vertex.push.stages != ShaderStage::Vertex)
                continue;
            const BindlessPackageModule* fragment = nullptr;
            Contract::EntrySelection selected;
            for (const auto& candidate : package->modules()) {
                const auto selection = Contract::selectEntries(vertex.entry, candidate.entry);
                if (!selection.recognized || selection.diagnostic)
                    continue;
                assert(!fragment);
                fragment = &candidate;
                selected = selection;
            }
            assert(fragment);
            for (const bool cancel : {true, false}) {
                Data data;
                data.initializeNative = true;
                data.vertex = &vertex;
                data.family = selected.family;
                data.artifact = &package->artifact();
                data.draw.material = data.terrain() ? 0u : 1u;
                data.cancelBeforeSubmit = cancel;
                data.mixed2DArrayTextures = true;
                auto device = createDevice();
                assert(device
                    && (device->backend() == Backend::Vulkan
                        || device->backend() == Backend::Metal));
                data.nativeStagingCeiling = nativeStagingCeiling;
                BindlessNativeSceneExecutionFixture<Closure, Pass, Content, Data> observer(data);
                validateOriginalBindlessRootSource(package,
                    std::move(device),
                    "unused",
                    false,
                    observer,
                    data,
                    true,
                    fragment->entry,
                    vertex.entry,
                    selected.pass != 0u,
                    OrdinaryRootMutation::None,
                    TextureFormat::Rgba16Float,
                    true);
            }
            ++entries;
        }
        assert(entries == 12u || entries == 3u);
        std::printf(
            "BINDLESS_NATIVE_SCENE_LIFECYCLE_OK entries=%zu public-factory original-owners cancellation-completion retirement no-visual-or-numeric-equivalence-grant\n",
            entries);
    }
};
} // namespace arp::test
