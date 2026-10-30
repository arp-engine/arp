#include "resource/vegetation_catalog_cooked.h"

#include "core/encoding/unsigned_element_span.h"
#include "core/encoding/yaml_contract.h"
#include "core/io/atomic_file_service.h"
#include "core/io/filesystem_path.h"
#include "core/io/staged_directory.h"
#include "resource/resource_source_io.h"
#include "resource/vegetation_catalog_geometry.h"
#include "resource/vegetation_catalog_layout.h"

#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>

namespace arp::resource {
namespace {
using Yaml = core::YamlContract;
using Words = core::UnsignedElementSpan;
using Template = vegetation::VegetationTemplate;
static_assert(sizeof(vegetation::InstanceVertex) == 60u);
constexpr std::size_t vertexWords = 15u, sectionWords = 3u, wordBytes = 4u;
static_assert(sizeof(float) == wordBytes && std::numeric_limits<float>::is_iec559);

struct Entry final {
    VegetationCatalogAssetEntry asset;
    Template geometry;
    std::uint32_t vertices = 0, indices = 0, sections = 0;
};

struct Index final {
    VegetationCatalogCookedResult result;
    core::pmr::Vector<Entry> entries{&core::defaultCpuMemoryResource()};
    core::pmr::String blockPath{&core::defaultCpuMemoryResource()};
    std::uint64_t hash = 0;
};

void require(bool condition)
{
    if (!condition)
        Yaml::fail();
}

std::array<float, vertexWords> vertexFields(const vegetation::InstanceVertex& v)
{
    return {v.position[0],
        v.position[1],
        v.position[2],
        v.normal[0],
        v.normal[1],
        v.normal[2],
        v.uv[0],
        v.uv[1],
        v.tangent[0],
        v.tangent[1],
        v.tangent[2],
        v.tangent[3],
        v.primaryBend,
        v.flutter,
        v.stiffness};
}

void setVertexFields(vegetation::InstanceVertex& v, const std::array<float, vertexWords>& f)
{
    std::copy_n(f.data(), 3, v.position);
    std::copy_n(f.data() + 3, 3, v.normal);
    std::copy_n(f.data() + 6, 2, v.uv);
    std::copy_n(f.data() + 8, 4, v.tangent);
    v.primaryBend = f[12];
    v.flutter = f[13];
    v.stiffness = f[14];
}

void validateGeometry(const Template& value, const VegetationCatalogAssetEntry& asset)
{
    require(!VegetationCatalogGeometryContract::validateBindings(value, asset));
    require(value.materialIndex == value.sections.front().materialSlot);
}

bool oneOf(std::string_view s, std::initializer_list<std::string_view> values)
{
    return std::find(values.begin(), values.end(), s) != values.end();
}

Index readIndex(const ResourceReference& reference,
    const std::filesystem::path& manifest,
    const ResourceLoadContext& context,
    ResourceFileOrigin origin)
{
    Index index;
    auto& result = index.result;
    const auto path = core::FilePath::utf8(manifest);
    const auto file =
        readResourceText(path, context, VegetationCatalogCookedCodec::maxManifestBytes, &origin);
    if (!file.success) {
        result.diagnostics = file.diagnostics;
        return index;
    }
    try {
        auto root = Yaml::parseSingleDocument(file.text);
        Yaml::onlyKeys(root,
            {"type",
                "contractVersion",
                "resourceId",
                "status",
                "maxDecodedBytes",
                "geometry",
                "templates"});
        require(Yaml::text(root, "type") == kVegetationCatalogCookedContract.type
            && Yaml::text(root, "contractVersion")
                == kVegetationCatalogCookedContract.contractVersion
            && reference.type == kVegetationCatalogCookedContract.type
            && isResourceUuid(reference.id) && Yaml::text(root, "resourceId") == reference.id
            && Yaml::text(root, "status") == reference.status);
        require(validateResourceStatus(
            Yaml::text(root, "status"), {ResourceStatus::ContractComplete}, "CookedCatalog")
                .success);
        const auto budget = Yaml::uint64(Yaml::required(root, "maxDecodedBytes"));
        require(budget && budget <= SIZE_MAX);
        const auto& block = Yaml::required(root, "geometry");
        Yaml::onlyKeys(block, {"path", "byteCount", "contentHash", "format"});
        require(Yaml::text(block, "format") == "VEGETATION_TEMPLATE_WORDS_LE_V1");
        const auto relative = Yaml::text(block, "path");
        require(relative.find('\0') == core::pmr::String::npos
            && core::FilePath::isSafeRelativePath(relative));
        const auto bytes = Yaml::uint64(Yaml::required(block, "byteCount"));
        if (!bytes || bytes > budget || bytes > SIZE_MAX
            || (context.maxDecodedBytes && bytes > context.maxDecodedBytes)) {
            result.diagnostics = "VEGETATION-COOKED-DECODE-BUDGET";
            return index;
        }
        const auto hash = Yaml::text(block, "contentHash");
        const auto parsed = std::from_chars(hash.data(), hash.data() + hash.size(), index.hash);
        require(
            !hash.empty() && parsed.ec == std::errc{} && parsed.ptr == hash.data() + hash.size());
        index.blockPath = core::FilePath::utf8(
            manifest.parent_path() / std::filesystem::u8path(relative.begin(), relative.end()));
        if (!context.requirePreloadedSources
            && origin.pathScope == core::io::FilePathScope::HostFilesystem) {
            std::error_code error;
            const auto base = std::filesystem::canonical(manifest.parent_path().empty()
                    ? std::filesystem::path(".")
                    : manifest.parent_path(),
                error);
            require(!error);
            const auto resolved = std::filesystem::canonical(
                std::filesystem::u8path(index.blockPath.begin(), index.blockPath.end()), error);
            if (error || !core::FilePath::contains(base, resolved)) {
                result.diagnostics = "VEGETATION-COOKED-BLOCK-PATH";
                return index;
            }
        }
        const auto& entries = Yaml::sequence(root, "templates");
        require(!entries.empty() && entries.size() <= VegetationCatalogCookedCodec::maxTemplates);
        vegetation::VegetationTemplateConfig config;
        std::uint64_t computed = 0;
        for (const auto& node : entries) {
            Yaml::onlyKeys(node,
                {"assetId",
                    "source",
                    "role",
                    "category",
                    "heightClass",
                    "templateCategory",
                    "templateId",
                    "variantIndex",
                    "lodIndex",
                    "vertexCount",
                    "indexCount",
                    "sectionCount",
                    "baseColor",
                    "materials"});
            Entry entry;
            auto& a = entry.asset;
            auto& g = entry.geometry;
            a.id = Yaml::text(node, "assetId");
            a.source = Yaml::text(node, "source");
            a.role = Yaml::text(node, "role");
            a.category = Yaml::text(node, "category");
            a.heightClass = Yaml::text(node, "heightClass");
            require(!a.id.empty() && a.id.find('\0') == core::pmr::String::npos
                && a.source.find('\0') == core::pmr::String::npos
                && oneOf(a.role, {"canopy", "landmark", "ground_cover", "understory"})
                && oneOf(a.category, {"canopy", "landmark", "ground_cover", "understory"})
                && oneOf(a.heightClass, {"low", "medium", "tall", "tall_robust"})
                && detail::CatalogFamilyContract::parse(
                    Yaml::text(node, "templateCategory"), a.templateCategory));
            require(Yaml::uint32(node, "templateId") == index.entries.size() + 1u);
            a.templateId = static_cast<std::uint16_t>(index.entries.size() + 1u);
            g.id = a.templateId;
            g.category = a.templateCategory;
            require(Yaml::uint32(node, "variantIndex")
                    == vegetation::familyVariantCount(config, a.templateCategory)
                && Yaml::uint32(node, "lodIndex") == 0u);
            g.variantIndex = vegetation::familyVariantCount(config, a.templateCategory);
            g.lodIndex = 0;
            require(detail::CatalogFamilyContract::increment(config, a.templateCategory));
            if (!index.entries.empty()) {
                const auto& previous = index.entries.back().asset;
                require(previous.templateCategory < a.templateCategory
                    || (previous.templateCategory == a.templateCategory && previous.id < a.id));
            }
            require(std::none_of(index.entries.begin(), index.entries.end(), [&](const auto& e) {
                return e.asset.id == a.id;
            }));
            const auto colors = Yaml::floatArray<4>(node, "baseColor");
            std::copy(colors.begin(), colors.end(), g.baseColor);
            entry.vertices = Yaml::uint32(node, "vertexCount");
            entry.indices = Yaml::uint32(node, "indexCount");
            entry.sections = Yaml::uint32(node, "sectionCount");
            require(entry.vertices && entry.indices && entry.sections && entry.indices % 3u == 0u
                && entry.sections <= entry.indices / 3u);
            const auto count =
                (static_cast<std::uint64_t>(entry.vertices) * vertexWords + entry.indices
                    + static_cast<std::uint64_t>(entry.sections) * sectionWords)
                * wordBytes;
            require(computed <= bytes && count <= bytes - computed);
            computed += count;
            const auto& bindings = Yaml::sequence(node, "materials");
            require(!bindings.empty() && bindings.size() <= entry.sections);
            for (const auto& binding : bindings) {
                Yaml::onlyKeys(binding, {"slot", "resourceId"});
                VegetationCatalogAssetEntry::MaterialBinding b;
                b.slot = Yaml::uint32(binding, "slot");
                b.resourceId = Yaml::text(binding, "resourceId");
                require(isResourceUuid(b.resourceId) && b.slot != UINT32_MAX
                    && (a.materials.empty() || a.materials.back().slot < b.slot));
                if (std::none_of(
                        result.dependencies.begin(), result.dependencies.end(), [&](const auto& d) {
                            return d.id == b.resourceId;
                        }))
                    result.dependencies.push_back({b.resourceId,
                        core::pmr::String("material", &core::defaultCpuMemoryResource())});
                a.materials.push_back(std::move(b));
            }
            index.entries.push_back(std::move(entry));
        }
        require(computed == bytes);
        result.decodedBytes = static_cast<std::size_t>(computed);
        // Physical location is not semantic content. All other manifest fields are fingerprinted.
        root["geometry"]["path"] = "";
        core::StableHash64 fingerprint;
        fingerprint.appendString(Yaml::serialize(root));
        result.contentFingerprint = fingerprint.value();
        if (!appendResourceReadIntent(path,
                VegetationCatalogCookedCodec::maxManifestBytes,
                result.files,
                result.diagnostics,
                origin)
            || !appendResourceReadIntent(
                index.blockPath, result.decodedBytes, result.files, result.diagnostics, origin))
            return index;
        result.success = true;
    } catch (...) {
        result.diagnostics = "VEGETATION-COOKED-MANIFEST-INVALID";
    }
    return index;
}

std::uint64_t hashBytes(const core::ByteBuffer& bytes)
{
    core::StableHash64 hash;
    hash.appendBytes(bytes.data(), bytes.size());
    return hash.value();
}

void appendWord(core::ByteBuffer& bytes, std::uint32_t value)
{
    const auto offset = bytes.size();
    bytes.resize(offset + wordBytes);
    require(
        Words::writeLittleEndian(value, Words::Width::Uint32, bytes.data() + offset, wordBytes));
}

void appendFloat(core::ByteBuffer& bytes, float f)
{
    std::uint32_t word;
    std::memcpy(&word, &f, sizeof(word));
    appendWord(bytes, word);
}

bool writeFile(
    const std::filesystem::path& path, const core::ByteBuffer& bytes, core::pmr::String& diagnostic)
{
    auto result = core::io::replaceFileAtomically(core::FilePath::utf8(path), bytes);
    diagnostic = std::move(result.diagnostic);
    return result.success;
}
} // namespace

VegetationCatalogCookedResult VegetationCatalogCookedCodec::inspect(
    const ResourceReference& reference,
    const std::filesystem::path& manifest,
    const ResourceLoadContext& context) const
{
    return inspect(reference, manifest, context, {});
}

VegetationCatalogCookedResult VegetationCatalogCookedCodec::inspect(
    const ResourceReference& reference,
    const std::filesystem::path& manifest,
    const ResourceLoadContext& context,
    ResourceFileOrigin origin) const
{
    return readIndex(reference, manifest, context, origin).result;
}

VegetationCatalogCookedResult VegetationCatalogCookedCodec::decode(
    const ResourceReference& reference,
    const std::filesystem::path& manifest,
    const ResourceLoadContext& context) const
{
    return decode(reference, manifest, context, {});
}

VegetationCatalogCookedResult VegetationCatalogCookedCodec::decode(
    const ResourceReference& reference,
    const std::filesystem::path& manifest,
    const ResourceLoadContext& context,
    ResourceFileOrigin origin) const
{
    auto index = readIndex(reference, manifest, context, origin);
    auto& result = index.result;
    if (!result.success)
        return std::move(result);
    result.success = false;
    auto block = readResourceSource(index.blockPath, context, result.decodedBytes, false, &origin);
    if (!block.success) {
        result.diagnostics = block.diagnostics;
        return std::move(result);
    }
    if (block.bytes.size() != result.decodedBytes || hashBytes(block.bytes) != index.hash) {
        result.diagnostics = "VEGETATION-COOKED-BLOCK-HASH-OR-SIZE";
        return std::move(result);
    }
    try {
        Words words(block.bytes.data(),
            block.bytes.size(),
            Words::Width::Uint32,
            Words::ByteOrder::LittleEndian);
        std::uint64_t cursor = 0;
        const auto next = [&]() {
            std::uint32_t word;
            require(words.read(cursor++, word));
            return word;
        };
        auto data = core::makeShared<VegetationCatalogResourceData>();
        data->contractVersion = 1u;
        data->status = "Contract Complete";
        data->templateConfig.hasEnvironmentAssets = true;
        for (auto& entry : index.entries) {
            auto& g = entry.geometry;
            g.vertices.resize(entry.vertices);
            g.indices.resize(entry.indices);
            g.sections.resize(entry.sections);
            for (auto& v : g.vertices) {
                std::array<float, vertexWords> fields{};
                for (auto& f : fields) {
                    const auto word = next();
                    std::memcpy(&f, &word, sizeof(f));
                }
                setVertexFields(v, fields);
            }
            for (auto& i : g.indices)
                i = next();
            for (auto& section : g.sections) {
                section.firstIndex = next();
                section.indexCount = next();
                section.materialSlot = next();
            }
            g.materialIndex = g.sections.front().materialSlot;
            validateGeometry(g, entry.asset);
            require(detail::CatalogFamilyContract::increment(data->templateConfig, g.category));
            data->templateIdsByAssetId.emplace(entry.asset.id, entry.asset.templateId);
            data->assets.push_back(std::move(entry.asset));
            data->templates.push_back(std::move(g));
        }
        require(cursor == words.size());
        result.data = std::move(data);
        result.success = true;
    } catch (...) {
        result.diagnostics = "VEGETATION-COOKED-GEOMETRY-INVALID";
    }
    return std::move(result);
}

VegetationCatalogCookedResult VegetationCatalogCookedCodec::write(
    const ResourceReference& reference,
    const VegetationCatalogResourceData& data,
    const std::filesystem::path& outputDirectory,
    std::size_t maxDecodedBytes) const
{
    VegetationCatalogCookedResult failed;
    try {
        require(maxDecodedBytes && !data.assets.empty()
            && data.assets.size() == data.templates.size() && data.assets.size() <= maxTemplates
            && maxDecodedBytes <= INT64_MAX);
        auto root = Yaml::Node::mapping();
        root["type"] = "VegetationCatalogResource";
        root["contractVersion"] = "vegetation.catalog.cooked/v1";
        root["resourceId"] = reference.id.c_str();
        root["status"] = reference.status.c_str();
        root["maxDecodedBytes"] = static_cast<std::int64_t>(maxDecodedBytes);
        root["templates"] = Yaml::Node::sequence();
        core::ByteBuffer block{&core::defaultCpuMemoryResource()};
        // Count and admit the entire geometry footprint before encoding any bytes.
        std::uint64_t total = 0;
        for (const auto& g : data.templates) {
            require(g.vertices.size() <= UINT32_MAX && g.indices.size() <= UINT32_MAX
                && g.sections.size() <= UINT32_MAX);
            const auto bytes =
                (static_cast<std::uint64_t>(g.vertices.size()) * vertexWords + g.indices.size()
                    + static_cast<std::uint64_t>(g.sections.size()) * sectionWords)
                * wordBytes;
            require(total <= maxDecodedBytes && bytes <= maxDecodedBytes - total);
            total += bytes;
        }
        vegetation::VegetationTemplateConfig expected;
        expected.hasEnvironmentAssets = true;
        for (const auto& g : data.templates)
            require(detail::CatalogFamilyContract::increment(expected, g.category));
        require(detail::CatalogFamilyContract::matches(data.templateConfig, expected));
        block.reserve(static_cast<std::size_t>(total));
        for (std::size_t i = 0; i < data.assets.size(); ++i) {
            const auto& a = data.assets[i];
            const auto& g = data.templates[i];
            for (const auto* field : {&a.id, &a.source, &a.role, &a.category, &a.heightClass})
                require(field->find('\0') == core::pmr::String::npos);
            require(a.templateId == g.id && a.templateCategory == g.category);
            validateGeometry(g, a);
            auto node = Yaml::Node::mapping();
            node["assetId"] = a.id.c_str();
            node["source"] = a.source.c_str();
            node["role"] = a.role.c_str();
            node["category"] = a.category.c_str();
            node["heightClass"] = a.heightClass.c_str();
            const auto* name = detail::CatalogFamilyContract::name(a.templateCategory);
            require(name);
            node["templateCategory"] = name;
            node["templateId"] = a.templateId;
            node["variantIndex"] = g.variantIndex;
            node["lodIndex"] = g.lodIndex;
            node["vertexCount"] = static_cast<std::int64_t>(g.vertices.size());
            node["indexCount"] = static_cast<std::int64_t>(g.indices.size());
            node["sectionCount"] = static_cast<std::int64_t>(g.sections.size());
            node["baseColor"] = Yaml::Node::sequence();
            for (auto f : g.baseColor) {
                require(std::isfinite(f));
                node["baseColor"].get_value_ref<Yaml::Node::sequence_type&>().emplace_back(f);
            }
            node["materials"] = Yaml::Node::sequence();
            for (const auto& b : a.materials) {
                require(isResourceUuid(b.resourceId));
                auto binding = Yaml::Node::mapping();
                binding["slot"] = static_cast<std::int64_t>(b.slot);
                binding["resourceId"] = b.resourceId.c_str();
                node["materials"].get_value_ref<Yaml::Node::sequence_type&>().push_back(
                    std::move(binding));
            }
            root["templates"].get_value_ref<Yaml::Node::sequence_type&>().push_back(
                std::move(node));
            for (const auto& v : g.vertices)
                for (auto f : vertexFields(v))
                    appendFloat(block, f);
            for (auto index : g.indices)
                appendWord(block, index);
            for (const auto& s : g.sections) {
                appendWord(block, s.firstIndex);
                appendWord(block, s.indexCount);
                appendWord(block, s.materialSlot);
            }
        }
        root["geometry"] = Yaml::Node::mapping();
        root["geometry"]["path"] = "geometry.bin";
        root["geometry"]["byteCount"] = static_cast<std::int64_t>(block.size());
        root["geometry"]["contentHash"] = core::formatUnsigned(hashBytes(block)).c_str();
        root["geometry"]["format"] = "VEGETATION_TEMPLATE_WORDS_LE_V1";
        const auto yaml = Yaml::serialize(root);
        require(yaml.size() <= maxManifestBytes);
        core::io::StagedDirectory staging;
        if (!staging.begin(outputDirectory)) {
            failed.diagnostics = staging.diagnostic();
            return failed;
        }
        core::ByteBuffer manifest(yaml.begin(), yaml.end(), &core::defaultCpuMemoryResource());
        if (!writeFile(staging.path() / "geometry.bin", block, failed.diagnostics)
            || !writeFile(staging.path() / "catalog.cooked.yaml", manifest, failed.diagnostics))
            return failed;
        auto verified = decode(reference, staging.path() / "catalog.cooked.yaml", {});
        if (!verified.success)
            return verified;
        // Do not return intents that refer to the temporary publication location.
        verified.files.clear();
        verified.data.reset();
        if (!staging.publish()) {
            failed.diagnostics = staging.diagnostic();
            return failed;
        }
        return verified;
    } catch (...) {
        failed.diagnostics = "VEGETATION-COOKED-WRITE-INVALID";
        return failed;
    }
}
} // namespace arp::resource
