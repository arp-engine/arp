#pragma once

#include "editor/project/project_input_snapshot.h"
#include "resource/internal_asset_package.h"

namespace arp::editor {

// Owner-thread cold planning over already acquired inputs. Repeated calls expose
// only the next declared reads; this class never submits IO or publishes state.
class EditorProjectResourcePlanner final {
public:
    static resource::AssetPackageReadPlan plan(const std::filesystem::path& root,
        const resource::AssetPackageSourceContext&,
        ProjectInputBudget,
        std::size_t maxDecodedBytes,
        core::SharedPtr<const resource::AssetPackageCodecRegistry> codecs = {});
};

} // namespace arp::editor
