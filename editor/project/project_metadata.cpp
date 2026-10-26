#include "editor/project/project_metadata.h"

#include "core/encoding/yaml_contract.h"
#include "core/io/filesystem_path.h"

#include <algorithm>
#include <exception>

namespace arp::editor {

bool ProjectMetadataCodec::decode(std::string_view yaml,
    ProjectMetadata& destination,
    ProjectDiagnostic& diagnostic,
    ProjectManifestBudget budget)
{
    const auto fail = [&](const char* code, std::string_view field) {
        diagnostic.code = code;
        diagnostic.field = field;
        return false;
    };
    if (!budget.maxDocumentBytes || !budget.maxAssetPackages)
        return fail("PROJECT-BUDGET-INVALID", "editorMetadata");
    if (yaml.size() > budget.maxDocumentBytes)
        return fail("PROJECT-METADATA-DOCUMENT-BUDGET", "editorMetadata");
    try {
        using Yaml = core::YamlContract;
        const auto root = Yaml::parseSingleDocument(yaml);
        Yaml::onlyKeys(root, {"type", "contractVersion", "terrainPreviews"});
        if (Yaml::text(root, "type") != "arp.editor-project"
            || Yaml::uint32(root, "contractVersion") != contractVersion)
            return fail("PROJECT-METADATA-VERSION-UNSUPPORTED", "editorMetadata.contractVersion");
        const auto& previews = Yaml::sequence(root, "terrainPreviews");
        if (previews.size() > budget.maxAssetPackages)
            return fail("PROJECT-METADATA-CAPACITY-EXCEEDED", "editorMetadata.terrainPreviews");
        ProjectMetadata candidate;
        core::pmr::UnorderedSet<core::pmr::String> unique;
        for (const auto& value : previews) {
            const auto path = Yaml::text(value);
            if (!core::FilePath::isSafeRelativePath(path)
                || std::any_of(path.begin(), path.end(), [](unsigned char c) {
                       return c < 32u;
                   }))
                return fail("PROJECT-METADATA-PATH-INVALID", "editorMetadata.terrainPreviews");
            core::pmr::String owned(path);
            if (!unique.insert(owned).second)
                return fail("PROJECT-METADATA-PATH-DUPLICATE", "editorMetadata.terrainPreviews");
            candidate.terrainPreviews.push_back(std::move(owned));
        }
        destination = std::move(candidate);
        diagnostic = {};
        return true;
    } catch (const core::YamlContractError&) {
        return fail("PROJECT-METADATA-FIELD-INVALID", "editorMetadata");
    } catch (const std::exception&) {
        return fail("PROJECT-METADATA-YAML-INVALID", "editorMetadata");
    }
}

} // namespace arp::editor
