#include "editor/project/project_package_snapshot.h"

#include "core/encoding/yaml_contract.h"
#include "core/hash/sha256.h"
#include "core/io/file_io.h"
#include "editor/project/project_manifest.h"
#include "resource/asset_package_closure.h"

#include <algorithm>

namespace arp::editor {
namespace {
using Yaml = core::YamlContract;

bool fail(core::pmr::String& diagnostic, const char* code)
{
    diagnostic = code;
    return false;
}

bool resolve(const std::filesystem::path& root,
    std::string_view relative,
    ProjectPathIntent intent,
    std::filesystem::path& path,
    core::pmr::String& diagnostic)
{
    ProjectDiagnostic issue;
    if (!ProjectManifestCodec::resolvePath(root, relative, intent, path, issue))
        return fail(diagnostic, issue.code);
    return true;
}
} // namespace

bool ProjectPackageSnapshot::capture(const std::filesystem::path& root,
    const core::pmr::Vector<core::pmr::String>& directories,
    core::SharedPtr<const resource::AssetPackageCodecRegistry> codecs,
    core::pmr::Vector<ProjectPackageBinding>& destination,
    core::pmr::String& diagnostic)
{
    try {
        std::error_code error;
        if (!root.is_absolute() || root != root.lexically_normal()
            || std::filesystem::canonical(root, error) != root || error
            || !std::filesystem::is_directory(root, error) || error
            || directories.size() > ProjectManifestBudget{}.maxAssetPackages)
            return fail(diagnostic, "EDITOR-RECOVERY-PACKAGE-ROOT-OR-MANIFEST");
        core::pmr::Vector<ProjectPackageBinding> packages;
        core::pmr::Vector<std::filesystem::path> roots;
        std::size_t remaining = maxAllPackageBytes;
        std::size_t decodedRemaining = maxAllDecodedBytes;
        for (const auto& relative : directories) {
            std::filesystem::path directory;
            if (!resolve(
                    root, relative, ProjectPathIntent::ExistingDirectory, directory, diagnostic))
                return false;
            if (std::find(roots.begin(), roots.end(), directory) != roots.end())
                return fail(diagnostic, "EDITOR-RECOVERY-PACKAGE-ALIAS");
            roots.push_back(directory);
            const auto closure = resource::AssetPackageClosureService(codecs).inspect(directory,
                {resource::AssetPackageClosureService::maxResources,
                    maxPackageFiles,
                    maxPackageFileBytes,
                    maxPackageBytes,
                    maxPackageBytes});
            if (!closure.success) {
                diagnostic = closure.diagnostics;
                return false;
            }
            const auto declaredDecoded = Yaml::uint64(
                Yaml::required(Yaml::parseSingleDocument(closure.indexYaml), "maxDecodedBytes"));
            if (declaredDecoded > maxPackageBytes || declaredDecoded > decodedRemaining)
                return fail(diagnostic, "EDITOR-RECOVERY-PACKAGE-DECODED-BUDGET");
            decodedRemaining -= static_cast<std::size_t>(declaredDecoded);
            core::pmr::Vector<core::pmr::String> paths;
            paths.emplace_back("manifest.arpkg.yaml");
            paths.push_back(closure.package.closurePath);
            for (const auto& entry : closure.package.entries)
                paths.push_back(entry.envelopePath);
            for (const auto& file : closure.files)
                paths.push_back(file.path);
            std::sort(paths.begin(), paths.end());
            if (std::adjacent_find(paths.begin(), paths.end()) != paths.end())
                return fail(diagnostic, "EDITOR-RECOVERY-PACKAGE-DUPLICATE-FILE");
            auto binding = Yaml::Node::mapping();
            binding["type"] = "arp.editor-recovery-package-binding";
            binding["contractVersion"] = 1;
            binding["files"] = Yaml::Node::sequence();
            std::size_t packageRemaining = maxPackageBytes;
            for (const auto& relativeFile : paths) {
                std::filesystem::path file;
                if (!resolve(
                        directory, relativeFile, ProjectPathIntent::ExistingFile, file, diagnostic))
                    return false;
                const auto read = core::readTextFile(
                    file, std::min({maxPackageFileBytes, remaining, packageRemaining}));
                if (!read.success)
                    return fail(diagnostic, "EDITOR-RECOVERY-PACKAGE-READ-OR-BUDGET");
                remaining -= read.text.size();
                packageRemaining -= read.text.size();
                auto item = Yaml::Node::mapping();
                item["path"] = relativeFile;
                item["bytes"] = static_cast<std::uint64_t>(read.text.size());
                item["sha256"] = core::Sha256::hexDigest(read.text);
                binding["files"].get_value_ref<Yaml::Node::sequence_type&>().push_back(
                    std::move(item));
            }
            packages.push_back({relative, core::Sha256::hexDigest(Yaml::serialize(binding))});
        }
        destination = std::move(packages);
        diagnostic.clear();
        return true;
    } catch (...) {
        return fail(diagnostic, "EDITOR-RECOVERY-PACKAGE-PREPARATION-FAILED");
    }
}

bool ProjectPackageSnapshot::equal(const core::pmr::Vector<ProjectPackageBinding>& left,
    const core::pmr::Vector<ProjectPackageBinding>& right) noexcept
{
    return left.size() == right.size()
        && std::equal(left.begin(), left.end(), right.begin(), [](const auto& a, const auto& b) {
               return a.directory == b.directory && a.fingerprint == b.fingerprint;
           });
}

} // namespace arp::editor
