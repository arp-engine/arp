#include "resource/terrain_asset_build_service.h"

#include "core/encoding/yaml_contract.h"
#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "core/io/staged_directory.h"
#include "resource/asset_package_closure.h"
#include "resource/internal_asset_package.h"

#include <algorithm>
#include <cstring>

namespace arp::resource {
namespace {
using Yaml = core::YamlContract;

// Explicit raw-source acquisition shared by height and weight producers.
class AuthoringBinaryInput final {
public:
    static core::pmr::Vector<std::uint8_t> read(const std::filesystem::path& root,
        core::StringView path,
        std::uint64_t byteCount,
        const char* pathDiagnostic,
        const char* sizeDiagnostic)
    {
        if (path.find('\0') != core::StringView::npos || !core::FilePath::isSafeRelativePath(path))
            throw core::YamlContractError{pathDiagnostic};
        std::error_code error;
        const auto canonicalRoot = std::filesystem::canonical(root, error);
        if (error)
            throw core::YamlContractError{pathDiagnostic};
        const auto resolved = std::filesystem::canonical(root / std::filesystem::path(path), error);
        if (error || !core::FilePath::contains(canonicalRoot, resolved)
            || !std::filesystem::is_regular_file(resolved, error) || error)
            throw core::YamlContractError{pathDiagnostic};
        if (std::filesystem::file_size(resolved, error) != byteCount || error)
            throw core::YamlContractError{sizeDiagnostic};
        auto file = core::readBinaryFile(resolved, byteCount);
        if (!file.success || file.bytes.size() != byteCount)
            throw core::YamlContractError{sizeDiagnostic};
        return std::move(file.bytes);
    }
};

class LatticeSource final {
public:
    struct PlacedInstance final {
        terrain::TerrainTileCoord tile;
        TerrainStaticVegetationInstance value;
    };

    bool constant = false;
    float height = 0.0f, scale = 1.0f, offset = 0.0f;
    std::uint64_t width = 0, depth = 0;
    bool increasingZ = true;
    core::pmr::Vector<std::uint8_t> bytes{&core::defaultCpuMemoryResource()};
    TerrainBakeWeights weights;
    std::uint64_t weightWidth = 0, weightDepth = 0;
    bool weightsIncreasingZ = true;
    core::pmr::Vector<std::uint8_t> weightPageA{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::uint8_t> weightPageB{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<PlacedInstance> instances{&core::defaultCpuMemoryResource()};

    static bool sampleWeights(void* context, float, float, TerrainBakeWeights& out)
    {
        out = static_cast<const LatticeSource*>(context)->weights;
        return true;
    }

    static bool sampleLatticeWeights(
        void* context, std::uint64_t x, std::uint64_t z, TerrainBakeWeights& out)
    {
        const auto& source = *static_cast<const LatticeSource*>(context);
        const core::RowMajorIndexGrid grid(
            source.weightDepth, source.weightWidth, source.weightPageA.size() / 4u);
        std::uint64_t index = 0;
        if (z >= source.weightDepth
            || !grid.index(source.weightsIncreasingZ ? z : source.weightDepth - 1u - z, x, index)
            || source.weightPageB.size() != source.weightPageA.size())
            return false;
        std::copy_n(source.weightPageA.data() + index * 4u, 4u, out.pageA.begin());
        std::copy_n(source.weightPageB.data() + index * 4u, 4u, out.pageB.begin());
        return true;
    }

    void readWeightPages(const Yaml::Node& surface,
        const TerrainAssetBuildDesc& desc,
        const std::filesystem::path& sourceRoot,
        std::uint64_t budget)
    {
        weightWidth = Yaml::uint32(surface, "width");
        weightDepth = Yaml::uint32(surface, "height");
        const auto direction = Yaml::text(surface, "rowDirection");
        weightsIncreasingZ = direction == "increasing-z";
        const core::RowMajorIndexGrid grid(weightDepth, weightWidth, budget / 8u);
        if (!grid.valid() || desc.samplesPerEdge < 2u
            || (direction != "increasing-z" && direction != "decreasing-z")
            || weightWidth
                != static_cast<std::uint64_t>(desc.tileCountX) * (desc.samplesPerEdge - 1u) + 1u
            || weightDepth
                != static_cast<std::uint64_t>(desc.tileCountZ) * (desc.samplesPerEdge - 1u) + 1u)
            throw core::YamlContractError{"TERRAIN-WEIGHTMAP-LAYOUT-OR-BUDGET"};
        if (bytes.size() > budget - grid.size() * 8u)
            throw core::YamlContractError{"TERRAIN-AUTHORING-INPUT-BUDGET"};
        weightPageA = AuthoringBinaryInput::read(sourceRoot,
            Yaml::text(surface, "pageA"),
            grid.size() * 4u,
            "TERRAIN-WEIGHTMAP-PATH",
            "TERRAIN-WEIGHTMAP-BYTE-COUNT");
        weightPageB = AuthoringBinaryInput::read(sourceRoot,
            Yaml::text(surface, "pageB"),
            grid.size() * 4u,
            "TERRAIN-WEIGHTMAP-PATH",
            "TERRAIN-WEIGHTMAP-BYTE-COUNT");
    }

    static bool bakeVegetation(void* context,
        terrain::TerrainTileCoord tile,
        core::pmr::Vector<TerrainStaticVegetationInstance>& out,
        core::pmr::String&)
    {
        for (const auto& placed : static_cast<const LatticeSource*>(context)->instances)
            if (placed.tile == tile)
                out.push_back(placed.value);
        return true;
    }

    static bool sample(void* context, std::uint64_t x, std::uint64_t z, float& out)
    {
        const auto& source = *static_cast<const LatticeSource*>(context);
        if (source.constant) {
            out = source.height;
            return std::isfinite(out);
        }
        const core::RowMajorIndexGrid grid(
            source.depth, source.width, source.bytes.size() / sizeof(float));
        std::uint64_t index = 0;
        if (z >= source.depth
            || !grid.index(source.increasingZ ? z : source.depth - 1u - z, x, index))
            return false;
        const auto* p = source.bytes.data() + index * sizeof(float);
        const std::uint32_t bits = static_cast<std::uint32_t>(p[0])
            | (static_cast<std::uint32_t>(p[1]) << 8u) | (static_cast<std::uint32_t>(p[2]) << 16u)
            | (static_cast<std::uint32_t>(p[3]) << 24u);
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        out = value * source.scale + source.offset;
        return std::isfinite(out);
    }
};

AssetPackageClosureBudget readAuthoringBudget(const Yaml::Node& root)
{
    const auto& limits = Yaml::required(root, "packageBudget");
    Yaml::onlyKeys(limits,
        {"maxResources", "maxFiles", "maxFileBytes", "maxTotalFileBytes", "maxDecodedBytes"});
    const auto size = [&](const char* field) {
        const auto value = Yaml::uint64(Yaml::required(limits, field));
        if (!value || value > SIZE_MAX)
            throw core::YamlContractError{"TERRAIN-AUTHORING-PACKAGE-BUDGET"};
        return static_cast<std::size_t>(value);
    };
    AssetPackageClosureBudget budget{size("maxResources"),
        size("maxFiles"),
        size("maxFileBytes"),
        size("maxTotalFileBytes"),
        size("maxDecodedBytes")};
    if (budget.maxResources > AssetPackageClosureService::maxResources)
        throw core::YamlContractError{"TERRAIN-AUTHORING-PACKAGE-BUDGET"};
    const auto dependency = Yaml::text(root, "dependencyPackage");
    if (!dependency.empty()
        && (dependency.find('\0') != core::pmr::String::npos
            || !core::FilePath::isSafeRelativePath(dependency)))
        throw core::YamlContractError{"TERRAIN-AUTHORING-DEPENDENCY-PATH"};
    return budget;
}

bool buildTerrainEntry(const TerrainAssetBuildDesc& desc,
    const char* manifestPath,
    InternalAssetPackageEntry& entry,
    core::pmr::String& diagnostic)
{
    Yaml::Node payload = Yaml::Node::mapping();
    payload["type"] = "TerrainAssetResource";
    payload["fields"] = Yaml::Node::mapping();
    payload["fields"]["manifestPath"] = manifestPath;
    const auto hash = computeResourcePayloadHash(Yaml::serialize(payload));
    if (!hash.success) {
        diagnostic = hash.diagnostics;
        return false;
    }
    Yaml::Node envelope = Yaml::Node::mapping();
    envelope["format"] = "ArpRuntimeResource";
    envelope["version"] = 1;
    envelope["resourceId"] = desc.resourceId.c_str();
    envelope["resourceType"] = "TerrainAssetResource";
    envelope["status"] = "Contract Complete";
    envelope["alias"] = desc.alias.c_str();
    envelope["contentHash"] = hash.hash;
    envelope["dependencies"] = Yaml::Node::sequence();
    envelope["payload"] = std::move(payload);
    entry.resourceId = desc.resourceId;
    entry.resourceType = "TerrainAssetResource";
    entry.alias = desc.alias;
    entry.status = "Contract Complete";
    entry.envelopePath = "resources/terrain.arpres.yaml";
    entry.contentHash = hash.hash;
    entry.envelopeYaml = Yaml::serialize(envelope);
    return true;
}
} // namespace

TerrainAssetBakeResult TerrainAssetBuildService::prepare(
    core::StringView yaml, const std::filesystem::path& sourceRoot) const
{
    TerrainAssetBakeResult result;
    try {
        const auto root = Yaml::parseSingleDocument(yaml);
        const auto version = Yaml::text(root, "contractVersion");
        const bool composed = version == "terrain.authoring/v2";
        if (composed)
            Yaml::onlyKeys(root,
                {"type",
                    "contractVersion",
                    "resourceId",
                    "alias",
                    "minX",
                    "minZ",
                    "tileCountX",
                    "tileCountZ",
                    "samplesPerEdge",
                    "tileWorldSize",
                    "maxDecodedBytes",
                    "input",
                    "surface",
                    "vegetation",
                    "dependencyPackage",
                    "packageBudget"});
        else
            Yaml::onlyKeys(root,
                {"type",
                    "contractVersion",
                    "resourceId",
                    "alias",
                    "minX",
                    "minZ",
                    "tileCountX",
                    "tileCountZ",
                    "samplesPerEdge",
                    "tileWorldSize",
                    "maxDecodedBytes",
                    "input"});
        if (Yaml::text(root, "type") != "TerrainAssetAuthoring"
            || (!composed && version != "terrain.authoring/v1")) {
            result.diagnostics = "TERRAIN-AUTHORING-CONTRACT";
            return result;
        }
        TerrainAssetBakeRequest request;
        request.asset.version = TerrainAssetVersion::V2;
        request.asset.hasSurfaceWeights = false;
        request.asset.hasVegetation = false;
        auto& desc = request.asset;
        desc.resourceId = Yaml::text(root, "resourceId");
        desc.alias = Yaml::text(root, "alias");
        if (desc.alias.find('\0') != core::pmr::String::npos)
            throw core::YamlContractError{"TERRAIN-AUTHORING-STRING-NUL"};
        desc.tileCountX = Yaml::uint32(root, "tileCountX");
        desc.tileCountZ = Yaml::uint32(root, "tileCountZ");
        desc.samplesPerEdge = Yaml::uint32(root, "samplesPerEdge");
        desc.tileWorldSize = static_cast<float>(Yaml::number(root, "tileWorldSize"));
        desc.bounds.minX = static_cast<float>(Yaml::number(root, "minX"));
        desc.bounds.minZ = static_cast<float>(Yaml::number(root, "minZ"));
        desc.bounds.maxX = desc.bounds.minX + desc.tileCountX * desc.tileWorldSize;
        desc.bounds.maxZ = desc.bounds.minZ + desc.tileCountZ * desc.tileWorldSize;
        auto budget = Yaml::uint64(Yaml::required(root, "maxDecodedBytes"));
        if (!budget || budget > std::numeric_limits<std::size_t>::max()
            || !isResourceUuid(desc.resourceId)) {
            result.diagnostics = "TERRAIN-AUTHORING-BUDGET-OR-ID";
            return result;
        }
        if (composed)
            budget = std::min(
                budget, static_cast<std::uint64_t>(readAuthoringBudget(root).maxDecodedBytes));
        request.maxDecodedBytes = static_cast<std::size_t>(budget);
        LatticeSource source;
        const auto& input = Yaml::required(root, "input");
        const auto kind = Yaml::text(input, "type");
        if (kind == "constant/v1") {
            Yaml::onlyKeys(input, {"type", "height"});
            source.constant = true;
            source.height = static_cast<float>(Yaml::number(input, "height"));
        } else if (kind == "r32-float-le/v1") {
            Yaml::onlyKeys(
                input, {"type", "path", "width", "height", "rowDirection", "scale", "offset"});
            const auto path = Yaml::text(input, "path");
            source.width = Yaml::uint32(input, "width");
            source.depth = Yaml::uint32(input, "height");
            source.scale = static_cast<float>(Yaml::number(input, "scale"));
            source.offset = static_cast<float>(Yaml::number(input, "offset"));
            const auto direction = Yaml::text(input, "rowDirection");
            source.increasingZ = direction == "increasing-z";
            const core::RowMajorIndexGrid grid(source.depth, source.width, budget / sizeof(float));
            if (path.find('\0') != core::pmr::String::npos
                || !core::FilePath::isSafeRelativePath(path) || !grid.valid()
                || (direction != "increasing-z" && direction != "decreasing-z")
                || desc.samplesPerEdge < 2u
                || source.width
                    != static_cast<std::uint64_t>(desc.tileCountX) * (desc.samplesPerEdge - 1u) + 1u
                || source.depth
                    != static_cast<std::uint64_t>(desc.tileCountZ) * (desc.samplesPerEdge - 1u)
                        + 1u) {
                result.diagnostics = "TERRAIN-HEIGHTMAP-LAYOUT";
                return result;
            }
            source.bytes = AuthoringBinaryInput::read(sourceRoot,
                path,
                grid.size() * sizeof(float),
                "TERRAIN-HEIGHTMAP-PATH",
                "TERRAIN-HEIGHTMAP-BYTE-COUNT");
        } else {
            result.diagnostics = "TERRAIN-HEIGHTMAP-ENCODING-UNSUPPORTED";
            return result;
        }
        if (composed) {
            const auto& surface = Yaml::required(root, "surface");
            const auto layout = Yaml::text(surface, "type");
            if (layout == "none")
                Yaml::onlyKeys(surface, {"type"});
            else if (layout == "constant-base6-overlays2/v1"
                || layout == "rgba8-base6-overlays2/v1") {
                if (layout == "constant-base6-overlays2/v1") {
                    Yaml::onlyKeys(surface, {"type", "pageA", "pageB", "layers"});
                    source.weights.pageA = Yaml::byteArray<4>(surface, "pageA");
                    source.weights.pageB = Yaml::byteArray<4>(surface, "pageB");
                    request.sampleWeights = &LatticeSource::sampleWeights;
                } else {
                    Yaml::onlyKeys(surface,
                        {"type", "pageA", "pageB", "width", "height", "rowDirection", "layers"});
                    source.readWeightPages(surface, desc, sourceRoot, budget);
                    request.sampleLatticeWeights = &LatticeSource::sampleLatticeWeights;
                }
                const auto& layers = Yaml::sequence(surface, "layers");
                if (layers.size() != desc.layers.size())
                    throw core::YamlContractError{"TERRAIN-AUTHORING-LAYERS"};
                for (std::size_t i = 0; i < layers.size(); ++i) {
                    const auto& layer = layers[i];
                    Yaml::onlyKeys(
                        layer, {"layerId", "semantic", "materialResourceId", "page", "channel"});
                    auto& output = desc.layers[i];
                    output.layerId = Yaml::text(layer, "layerId");
                    output.semantic = Yaml::text(layer, "semantic");
                    if (output.layerId.find('\0') != core::pmr::String::npos
                        || output.semantic.find('\0') != core::pmr::String::npos)
                        throw core::YamlContractError{"TERRAIN-AUTHORING-STRING-NUL"};
                    output.materialResourceId = Yaml::text(layer, "materialResourceId");
                    output.page = Yaml::uint32(layer, "page");
                    output.channel = Yaml::uint32(layer, "channel");
                    if (!isResourceUuid(output.materialResourceId))
                        throw core::YamlContractError{"TERRAIN-AUTHORING-MATERIAL-ID"};
                }
                desc.hasSurfaceWeights = true;
            } else
                throw core::YamlContractError{"TERRAIN-AUTHORING-SURFACE-UNSUPPORTED"};
            const auto& vegetation = Yaml::required(root, "vegetation");
            const auto instancesType = Yaml::text(vegetation, "type");
            if (instancesType == "none")
                Yaml::onlyKeys(vegetation, {"type"});
            else if (instancesType == "static-instances/v1") {
                Yaml::onlyKeys(vegetation, {"type", "catalogResourceId", "instances"});
                desc.vegetationCatalogResourceId = Yaml::text(vegetation, "catalogResourceId");
                if (!isResourceUuid(desc.vegetationCatalogResourceId))
                    throw core::YamlContractError{"TERRAIN-AUTHORING-CATALOG-ID"};
                const auto& instances = Yaml::sequence(vegetation, "instances");
                const auto recordBytes = sizeof(TerrainStaticVegetationInstance);
                if (instances.size() > budget / recordBytes
                    || instances.size() * recordBytes >= budget)
                    throw core::YamlContractError{"TERRAIN-AUTHORING-INSTANCE-BUDGET"};
                request.maxDecodedBytes -= instances.size() * recordBytes;
                source.instances.reserve(instances.size());
                for (const auto& node : instances) {
                    Yaml::onlyKeys(node,
                        {"tileX",
                            "tileZ",
                            "prototypeId",
                            "position",
                            "rotation",
                            "scale",
                            "variationSeed"});
                    const auto x = Yaml::uint32(node, "tileX"), z = Yaml::uint32(node, "tileZ");
                    if (x >= desc.tileCountX || z >= desc.tileCountZ || x > INT32_MAX
                        || z > INT32_MAX)
                        throw core::YamlContractError{"TERRAIN-AUTHORING-INSTANCE-TILE"};
                    LatticeSource::PlacedInstance placed;
                    placed.tile = {static_cast<std::int32_t>(x), static_cast<std::int32_t>(z)};
                    auto& value = placed.value;
                    value.prototypeId = Yaml::uint32(node, "prototypeId");
                    const auto position = Yaml::floatArray<3>(node, "position");
                    const auto scale = Yaml::floatArray<3>(node, "scale");
                    value.position = {position[0], position[1], position[2]};
                    value.scale = {scale[0], scale[1], scale[2]};
                    value.rotation = Yaml::floatArray<4>(node, "rotation");
                    value.variationSeed = Yaml::uint32(node, "variationSeed");
                    const auto minX = desc.bounds.minX + x * desc.tileWorldSize;
                    const auto minZ = desc.bounds.minZ + z * desc.tileWorldSize;
                    if (!terrain::TerrainBounds{
                            minX, minX + desc.tileWorldSize, minZ, minZ + desc.tileWorldSize}
                            .contains(value.position.x, value.position.z))
                        throw core::YamlContractError{"TERRAIN-AUTHORING-INSTANCE-TILE-BOUNDS"};
                    source.instances.push_back(std::move(placed));
                }
                desc.hasVegetation = true;
                request.bakeVegetation = &LatticeSource::bakeVegetation;
            } else
                throw core::YamlContractError{"TERRAIN-AUTHORING-VEGETATION-UNSUPPORTED"};
        }
        request.sourceContext = &source;
        request.sampleLatticeHeight = &LatticeSource::sample;
        auto baked = TerrainAssetBaker().bake(request);
        if (baked.success) {
            const auto validated = TerrainAssetWriter().validate(baked.asset);
            baked.success = validated.success;
            baked.diagnostics = validated.diagnostics;
        }
        return baked;
    } catch (const core::YamlContractError& error) {
        result.diagnostics = error.diagnostic;
        return result;
    } catch (...) {
        result.diagnostics = "TERRAIN-AUTHORING-INVALID-YAML";
        return result;
    }
}

TerrainAssetIoResult TerrainAssetBuildService::build(
    const std::filesystem::path& authoringFile, const std::filesystem::path& outputDirectory) const
{
    const auto file = core::readTextFile(authoringFile, 1024u * 1024u);
    if (!file.success)
        return {false, file.diagnostics};
    const auto prepared = prepare(file.text, authoringFile.parent_path());
    if (!prepared.success)
        return {false, prepared.diagnostics};
    const auto root = Yaml::parseSingleDocument(file.text);
    if (Yaml::text(root, "contractVersion") == "terrain.authoring/v2")
        return buildComposed(
            file.text, authoringFile.parent_path(), prepared.asset, outputDirectory);
    return packageGeometry(prepared.asset, outputDirectory);
}

TerrainAssetIoResult TerrainAssetBuildService::buildComposed(core::StringView authoringYaml,
    const std::filesystem::path& sourceRoot,
    const TerrainAssetBuildDesc& terrain,
    const std::filesystem::path& outputDirectory) const
{
    try {
        const auto root = Yaml::parseSingleDocument(authoringYaml);
        const auto budget = readAuthoringBudget(root);
        const auto dependency = Yaml::text(root, "dependencyPackage");
        AssetPackageClosureService closure;
        AssetPackageClosureResult dependencies;
        if (!dependency.empty()) {
            if (dependency.find('\0') != core::pmr::String::npos
                || !core::FilePath::isSafeRelativePath(dependency))
                throw core::YamlContractError{"TERRAIN-AUTHORING-DEPENDENCY-PATH"};
            const auto canonicalRoot = std::filesystem::canonical(sourceRoot);
            const auto path = std::filesystem::canonical(canonicalRoot / dependency.c_str());
            if (!core::FilePath::contains(canonicalRoot, path))
                throw core::YamlContractError{"TERRAIN-AUTHORING-DEPENDENCY-PATH"};
            dependencies = closure.inspect(path, budget);
            if (!dependencies.success)
                return {false, dependencies.diagnostics};
            const auto reserved = [](core::StringView path) {
                return path == "terrain" || path.substr(0u, 8u) == "terrain/"
                    || path == "resources/terrain.arpres.yaml";
            };
            for (const auto& entry : dependencies.package.entries)
                if (entry.resourceId == terrain.resourceId || reserved(entry.envelopePath))
                    throw core::YamlContractError{"TERRAIN-AUTHORING-DEPENDENCY-COLLISION"};
            for (const auto& file : dependencies.files)
                if (reserved(file.path))
                    throw core::YamlContractError{"TERRAIN-AUTHORING-DEPENDENCY-COLLISION"};
            if (reserved(dependencies.package.closurePath))
                throw core::YamlContractError{"TERRAIN-AUTHORING-DEPENDENCY-COLLISION"};
        }
        if (dependencies.package.entries.size() >= budget.maxResources)
            throw core::YamlContractError{"TERRAIN-AUTHORING-PACKAGE-RESOURCE-BUDGET"};
        // This private workspace is never published. The final closure service
        // owns the separate output staging transaction and complete reload.
        auto preparationName = outputDirectory;
        preparationName += ".authoring";
        core::io::StagedDirectory preparation;
        if (!preparation.begin(preparationName))
            return {false, preparation.diagnostic()};
        const auto packageRoot = preparation.path() / "package";
        InternalAssetPackageDesc package;
        if (!dependency.empty()) {
            const auto copied = closure.publish(
                std::filesystem::canonical(sourceRoot / dependency.c_str()), packageRoot, budget);
            if (!copied.success)
                return {false, copied.diagnostics};
            package = copied.package;
        } else {
            std::filesystem::create_directory(packageRoot);
            package.closurePath = "closure.arpkg.yaml";
        }
        // Native existence checks also catch case aliases on filesystems where
        // TERRAIN and terrain resolve to the same directory or control file.
        if (std::filesystem::exists(packageRoot / "terrain")
            || std::filesystem::exists(packageRoot / "resources/terrain.arpres.yaml"))
            throw core::YamlContractError{"TERRAIN-AUTHORING-DEPENDENCY-COLLISION"};
        const auto written = TerrainAssetWriter().write(terrain, packageRoot / "terrain");
        if (!written.success)
            return written;
        package.name = terrain.resourceId;
        package.status = "Contract Complete";
        InternalAssetPackageEntry entry;
        core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
        if (!buildTerrainEntry(terrain, "terrain/terrain.asset.yaml", entry, diagnostic))
            return {false, diagnostic};
        package.entries.push_back(std::move(entry));
        const auto indexed = closure.prepareIndex(package, packageRoot, budget);
        if (!indexed.success)
            return {false, indexed.diagnostics};
        const auto published = closure.publish(packageRoot, outputDirectory, budget);
        return {published.success, published.diagnostics};
    } catch (const core::YamlContractError& error) {
        return {false, core::pmr::String(error.diagnostic, &core::defaultCpuMemoryResource())};
    } catch (...) {
        return {false,
            core::pmr::String(
                "TERRAIN-AUTHORING-COMPOSITION-FAILED", &core::defaultCpuMemoryResource())};
    }
}

TerrainAssetIoResult TerrainAssetBuildService::packageClosed(
    const std::filesystem::path& preparedDirectory,
    const std::filesystem::path& outputDirectory) const
{
    const auto prepared = AssetPackageClosureService().inspect(preparedDirectory);
    if (!prepared.success)
        return {false, prepared.diagnostics};
    if (std::none_of(prepared.package.entries.begin(),
            prepared.package.entries.end(),
            [](const auto& entry) {
                return entry.resourceType == TerrainAssetResource::resourceTypeName();
            }))
        return {false,
            core::pmr::String(
                "TERRAIN-PACKAGE-TERRAIN-REQUIRED", &core::defaultCpuMemoryResource())};
    const auto result = AssetPackageClosureService().publish(preparedDirectory, outputDirectory);
    return {result.success, result.diagnostics};
}

TerrainAssetIoResult TerrainAssetBuildService::packageGeometry(
    const TerrainAssetBuildDesc& desc, const std::filesystem::path& outputDirectory) const
{
    if (desc.hasSurfaceWeights || desc.hasVegetation)
        return {false,
            core::pmr::String(
                "TERRAIN-PACKAGE-DEPENDENCY-CLOSURE-REQUIRED", &core::defaultCpuMemoryResource())};
    core::io::StagedDirectory transaction;
    if (!transaction.begin(outputDirectory))
        return {false, transaction.diagnostic()};
    const auto written = TerrainAssetWriter().write(desc, transaction.path());
    if (!written.success)
        return written;
    InternalAssetPackageEntry entry;
    core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
    if (!buildTerrainEntry(desc, "terrain.asset.yaml", entry, diagnostic))
        return {false, diagnostic};
    InternalAssetPackageDesc package;
    package.name = desc.resourceId;
    package.status = "Contract Complete";
    package.entries.push_back(std::move(entry));
    const auto packaged = InternalAssetPackageWriter().write(package, transaction.path());
    if (!packaged.success)
        return {false, packaged.diagnostics};
    ResourceManager candidate;
    const auto verified = InternalAssetPackageReader().load(transaction.path(), candidate);
    if (!verified.success)
        return {false, verified.diagnostics};
    if (!candidate.find<TerrainAssetResource>(desc.resourceId).ready())
        return {false,
            core::pmr::String(
                "TERRAIN-PACKAGE-VERIFICATION-FAILED", &core::defaultCpuMemoryResource())};
    if (!transaction.publish())
        return {false, transaction.diagnostic()};
    TerrainAssetIoResult result;
    result.success = true;
    return result;
}
} // namespace arp::resource
