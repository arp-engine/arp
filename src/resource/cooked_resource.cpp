#include "resource/cooked_resource.h"

#include "core/encoding/binary_stream.h"
#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "core/io/staged_directory.h"
#include "resource/resource_source_io.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <thread>

namespace arp::resource {
namespace {
using V = reflection::SerializationValue;
using W = core::encoding::BinaryWriter;
using R = core::encoding::BinaryReader;
constexpr std::uint64_t catalogMagic = 0x31544143505241ull;
constexpr std::uint64_t resourceMagic = 0x31534552505241ull;
constexpr std::size_t headerSize = 104;
constexpr std::size_t catalogHeaderSize = 52;

void require(bool ok, const char* text)
{
    if (!ok)
        throw std::runtime_error(text);
}

core::StringView view(core::ArrayView<const std::uint8_t> b)
{
    return {reinterpret_cast<const char*>(b.data()), b.size()};
}

core::pmr::String digest(core::ArrayView<const std::uint8_t> b)
{
    return core::Sha256::hexDigest(view(b));
}

struct CatalogSections {
    bool packed;
    core::ArrayView<const std::uint8_t> records;
    core::ArrayView<const std::uint8_t> metadata;
};

CatalogSections catalogSections(core::ArrayView<const std::uint8_t> bytes)
{
    R r(bytes);
    require(r.integer() == catalogMagic, "COOKED-CATALOG-FORMAT-RECOOK");
    const auto format = r.integer(4);
    require(format == 1 || format == 2, "COOKED-CATALOG-FORMAT-RECOOK");
    const auto size = r.integer();
    const auto hash = r.raw(32);
    const auto body = r.raw(size);
    const auto metadata = r.raw(r.remaining());
    require(format == 2 || metadata.empty(), "COOKED-CATALOG-TRAILING");
    const auto actual = core::Sha256::digest(
        view({bytes.data() + catalogHeaderSize, bytes.size() - catalogHeaderSize}));
    require(std::equal(actual.begin(), actual.end(), hash.begin()), "COOKED-CATALOG-HASH");
    return {format == 2, body, metadata};
}

core::pmr::String string(const V& n)
{
    require(n.is_string(), "COOKED-EXPECTED-STRING");
    return n.get_value<V::string_type>();
}

std::uint64_t number(const V& n)
{
    require(n.is_unsigned(), "COOKED-EXPECTED-U64");
    return n.get_value<std::uint64_t>();
}

reflection::SerializationOptions options(CookedResourceBudget b)
{
    reflection::SerializationOptions o;
    o.maxBytes = b.maxMetadataBytes;
    o.maxCollectionLength = b.maxMetadataBytes / 8;
    o.maxValues = b.maxMetadataBytes / 8;
    return o;
}

V record(const CookedResourceEntry& e)
{
    auto n = V::mapping();
    n["id"] = e.reference.id;
    n["type"] = e.reference.type;
    n["alias"] = e.reference.alias;
    n["status"] = e.reference.status;
    n["name"] = e.reference.displayName;
    n["file"] = e.file;
    n["version"] = e.contentVersion;
    n["schema"] = e.schema;
    n["metadataBytes"] = e.metadataBytes;
    n["payloadBytes"] = e.payloadBytes;
    n["metadataHash"] = e.metadataHash;
    n["payloadHash"] = e.payloadHash;
    auto deps = V::sequence();
    for (const auto& d : e.dependencies) {
        auto dep = V::mapping();
        dep["id"] = d.binding.id;
        dep["usage"] = d.binding.usage;
        dep["type"] = d.expectedType;
        deps.get_value_ref<V::sequence_type&>().push_back(std::move(dep));
    }
    n["dependencies"] = std::move(deps);
    return n;
}

core::pmr::String version(const CookedResourceEntry& e)
{
    auto n = record(e);
    n["version"] = "";
    auto b = reflection::encode_serialization_value(n);
    require(b.success, b.diagnostics.c_str());
    return digest({b.bytes.data(), b.bytes.size()});
}

CookedResourceEntry entry(const V& n)
{
    require(n.is_mapping() && n.size() == 13, "COOKED-ENTRY-SCHEMA");
    CookedResourceEntry e;
    e.reference = {string(n["id"]),
        string(n["type"]),
        string(n["alias"]),
        string(n["status"]),
        string(n["name"])};
    e.file = string(n["file"]);
    e.contentVersion = string(n["version"]);
    e.schema = number(n["schema"]);
    e.metadataBytes = number(n["metadataBytes"]);
    e.payloadBytes = number(n["payloadBytes"]);
    e.metadataHash = string(n["metadataHash"]);
    e.payloadHash = string(n["payloadHash"]);
    require(n["dependencies"].is_sequence(), "COOKED-DEPENDENCIES");
    for (const auto& d : n["dependencies"].get_value_ref<const V::sequence_type&>()) {
        require(d.is_mapping() && d.size() == 3, "COOKED-DEPENDENCY-SCHEMA");
        e.dependencies.push_back({{string(d["id"]), string(d["usage"])}, string(d["type"])});
    }
    return e;
}

void validate(const core::pmr::Vector<CookedResourceEntry>& entries,
    const CookedResourceCodecRegistry& codecs,
    CookedResourceBudget b)
{
    require(codecs.frozen() && !entries.empty() && entries.size() <= b.maxResources,
        "COOKED-CATALOG-BUDGET-OR-REGISTRY");
    require(b.maxMetadataBytes && b.maxPayloadBytes && b.maxTotalBytes && b.maxDecodedBytes,
        "COOKED-BUDGET");
    std::size_t total = 0;
    core::pmr::Map<core::pmr::String, std::size_t> ids{&core::defaultCpuMemoryResource()};
    core::pmr::Map<core::pmr::String, bool> aliases{&core::defaultCpuMemoryResource()};
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        require(isResourceUuid(e.reference.id) && ids.emplace(e.reference.id, i).second,
            "COOKED-DUPLICATE-OR-INVALID-UUID");
        if (!e.reference.alias.empty())
            require(aliases.emplace(e.reference.alias, true).second, "COOKED-DUPLICATE-ALIAS");
        require(
            e.reference.status == "Contract Complete" || e.reference.status == "Prototype Complete",
            "COOKED-STATUS");
        auto codec = codecs.find(e.reference.type);
        require(bool(codec), "COOKED-UNKNOWN-CODEC");
        require(codec->schema() == e.schema, "COOKED-SCHEMA-RECOOK");
        require(
            e.file == e.reference.id + ".arpres.bin" && core::FilePath::isSafeRelativePath(e.file),
            "COOKED-FILE-BINDING");
        require(e.metadataBytes > 0 && e.metadataBytes <= b.maxMetadataBytes
                && e.payloadBytes <= b.maxPayloadBytes,
            "COOKED-SECTION-BUDGET");
        require(e.metadataBytes <= b.maxTotalBytes - total, "COOKED-TOTAL-BUDGET");
        total += static_cast<std::size_t>(e.metadataBytes);
        require(e.payloadBytes <= b.maxTotalBytes - total, "COOKED-TOTAL-BUDGET");
        total += static_cast<std::size_t>(e.payloadBytes);
        require(e.contentVersion == version(e), "COOKED-CONTENT-VERSION");
    }
    core::pmr::Vector<unsigned> states(entries.size(), 0, &core::defaultCpuMemoryResource());
    const auto visit = [&](auto&& self, std::size_t i, std::size_t depth) -> void {
        require(depth <= b.maxResources && states[i] != 1, "COOKED-DEPENDENCY-CYCLE");
        if (states[i] == 2)
            return;
        states[i] = 1;
        const auto& e = entries[i];
        core::pmr::Map<core::pmr::String, bool> bindings{&core::defaultCpuMemoryResource()};
        for (const auto& d : e.dependencies) {
            const auto found = ids.find(d.binding.id);
            require(found != ids.end(), "COOKED-MISSING-DEPENDENCY");
            require(
                !d.binding.usage.empty() && !d.expectedType.empty(), "COOKED-DEPENDENCY-BINDING");
            auto key = d.binding.id;
            key += '\n';
            key += d.binding.usage;
            require(bindings.emplace(key, true).second, "COOKED-DUPLICATE-DEPENDENCY");
            require(
                entries[found->second].reference.type == d.expectedType, "COOKED-DEPENDENCY-TYPE");
            self(self, found->second, depth + 1);
        }
        states[i] = 2;
    };
    for (std::size_t i = 0; i < entries.size(); ++i)
        visit(visit, i, 0);
}

void save(const std::filesystem::path& path, core::ArrayView<const std::uint8_t> b)
{
    std::ofstream f(path, std::ios::binary);
    require(bool(f), "COOKED-WRITE");
    if (!b.empty())
        f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    f.flush();
    require(bool(f), "COOKED-WRITE");
}

CookedBytes header(const CookedResourceEntry& e)
{
    W w(headerSize);
    w.integer(resourceMagic);
    w.integer(1, 4);
    w.integer(0, 4);
    w.integer(e.metadataBytes);
    w.integer(e.payloadBytes);
    w.integer(e.schema);
    // SHA-256 raw bytes have fixed width; the catalog stores canonical display form.
    for (const auto& hash : {e.metadataHash, e.payloadHash}) {
        require(hash.size() == 71 && hash.substr(0, 7) == "sha256:", "COOKED-DIGEST");
        for (std::size_t i = 7; i < hash.size(); i += 2) {
            const auto digit = [](char c) -> unsigned {
                if (c >= '0' && c <= '9')
                    return c - '0';
                if (c >= 'a' && c <= 'f')
                    return c - 'a' + 10;
                throw std::runtime_error("COOKED-DIGEST");
            };
            w.integer((digit(hash[i]) << 4) | digit(hash[i + 1]), 1);
        }
    }
    return w.take();
}

core::io::FileReadResult read(
    core::runtime::AsyncRuntime& runtime, const ResourceFileReadIntent& intent)
{
    struct Wait {
        bool done = false;
        core::io::FileReadResult result;
    } wait;

    core::io::FileReadRequest r;
    r.contractVersion = 1;
    r.path = intent.path;
    r.pathScope = intent.pathScope;
    r.mount = intent.mount;
    r.range = intent.range;
    r.maxBytes = intent.maxBytes;
    const auto status = runtime.fileService().read(r,
        {[](void* ptr, core::io::FileReadResult result) {
             auto& w = *static_cast<Wait*>(ptr);
             w.result = std::move(result);
             w.done = true;
         },
            &wait});
    require(status == core::io::FileIoStatus::Accepted, "COOKED-READ-SUBMIT");
    while (!wait.done) {
        runtime.dispatch();
        std::this_thread::yield();
    }
    require(wait.result.status == core::io::FileIoStatus::Success,
        wait.result.diagnosticId ? wait.result.diagnosticId : "COOKED-READ");
    return std::move(wait.result);
}
} // namespace

CookedBytes CookedResourceCodec::cookPayload(
    const ResourceBase&, const std::filesystem::path&, const CookedResourceBudget&) const
{
    return CookedBytes(&core::defaultCpuMemoryResource());
}

CookedResourceResult CookedResourceCodec::preparePayload(
    ResourceBase&, core::ArrayView<const std::uint8_t> bytes, std::size_t) const
{
    return {bytes.empty(), bytes.empty() ? "" : "COOKED-UNEXPECTED-PAYLOAD"};
}

CookedResourceResult CookedResourceCodec::beforeAdmission(ResourceBase&, ResourceManager&) const
{
    return {true};
}

bool CookedResourceCodecRegistry::add(core::SharedPtr<const CookedResourceCodec> codec)
{
    if (frozen_ || !codec || codec->resourceType().empty() || codecs_.size() >= capacity_
        || find(codec->resourceType()))
        return false;
    codecs_.push_back(std::move(codec));
    return true;
}

bool CookedResourceCodecRegistry::freeze()
{
    if (frozen_ || codecs_.empty())
        return false;
    for (const auto& c : codecs_)
        c->schema();
    frozen_ = true;
    return true;
}

core::SharedPtr<const CookedResourceCodec> CookedResourceCodecRegistry::find(NameView type) const
{
    for (const auto& c : codecs_)
        if (c->resourceType() == type)
            return c;
    return {};
}

bool verifyCookedPayload(const CookedResourceSpec& spec,
    const ResourceSourceSet& sources,
    core::ArrayView<const std::uint8_t>& bytes,
    core::pmr::String& error)
{
    if (!spec.entry.payloadBytes)
        return true;
    const auto* source = sources.find(spec.payloadRead);
    if (!source || source->bytes.size() != spec.entry.payloadBytes) {
        error = "COOKED-PAYLOAD-SOURCE";
        return false;
    }
    bytes = {source->bytes.data(), source->bytes.size()};
    if (digest(bytes) != spec.entry.payloadHash) {
        error = "COOKED-PAYLOAD-HASH";
        return false;
    }
    return true;
}

const CookedResourceEntry* CookedResourceCatalog::find(NameView id) const noexcept
{
    for (const auto& e : entries_)
        if (e.reference.id == id)
            return &e;
    return nullptr;
}

CookedResourceResult CookedResourceCatalog::assign(core::pmr::Vector<CookedResourceEntry> entries,
    const CookedResourceCodecRegistry& codecs,
    CookedResourceBudget b)
{
    try {
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            return a.reference.id < b.reference.id;
        });
        validate(entries, codecs, b);
        entries_ = std::move(entries);
        return {true};
    } catch (const std::exception& e) {
        return {false, e.what()};
    }
}

reflection::ReflectionBinaryResult CookedResourceCatalog::write(CookedResourceBudget b) const
{
    reflection::ReflectionBinaryResult result;
    try {
        V n = V::sequence();
        for (const auto& e : entries_)
            n.get_value_ref<V::sequence_type&>().push_back(record(e));
        auto body = reflection::encode_serialization_value(n, options(b));
        require(body.success, body.diagnostics.c_str());
        W w(b.maxMetadataBytes);
        w.integer(catalogMagic);
        w.integer(1, 4);
        w.integer(body.bytes.size());
        const auto hash = core::Sha256::digest(view({body.bytes.data(), body.bytes.size()}));
        w.raw({hash.data(), hash.size()});
        w.raw({body.bytes.data(), body.bytes.size()});
        result.bytes = w.take();
        result.success = true;
    } catch (const std::exception& e) {
        result.diagnostics = e.what();
    }
    return result;
}

CookedResourceResult CookedResourceCatalog::read(core::ArrayView<const std::uint8_t> bytes,
    const CookedResourceCodecRegistry& codecs,
    CookedResourceBudget b)
{
    try {
        require(bytes.size() <= b.maxMetadataBytes, "COOKED-CATALOG-BUDGET");
        const auto sections = catalogSections(bytes);
        auto decoded = reflection::decode_serialization_value(sections.records, options(b));
        require(decoded.success, decoded.diagnostics.c_str());
        require(decoded.value.is_sequence(), "COOKED-CATALOG-SCHEMA");
        core::pmr::Vector<CookedResourceEntry> entries{&core::defaultCpuMemoryResource()};
        for (const auto& n : decoded.value.get_value_ref<const V::sequence_type&>())
            entries.push_back(entry(n));
        if (sections.packed) {
            require(std::is_sorted(entries.begin(),
                        entries.end(),
                        [](const auto& a, const auto& c) {
                            return a.reference.id < c.reference.id;
                        }),
                "COOKED-PACK-ORDER");
            R metadata(sections.metadata);
            for (const auto& e : entries)
                require(digest(metadata.raw(e.metadataBytes)) == e.metadataHash,
                    "COOKED-METADATA-HASH");
            require(!metadata.remaining(), "COOKED-METADATA-TRAILING");
        }
        return assign(std::move(entries), codecs, b);
    } catch (const std::exception& e) {
        return {false, e.what()};
    }
}

namespace {
CookedResourcePlan buildPlan(const std::filesystem::path& root,
    core::SharedPtr<const CookedResourceCodecRegistry> codecs,
    CookedResourceBudget b,
    ResourceFileOrigin origin,
    const core::SharedCallback<core::io::FileReadResult(const ResourceFileReadIntent&)>& reader)
{
    CookedResourcePlan result;
    try {
        require(codecs && codecs->frozen(), "COOKED-LOADER-CONFIG");
        ResourceFileReadIntent intent;
        intent.path = core::FilePath::utf8(root / "catalog.arpbin");
        intent.pathScope = origin.pathScope;
        intent.mount = origin.mount;
        intent.maxBytes = b.maxMetadataBytes;
        auto catalog = reader(intent);
        result.metadataReadBytes += catalog.bytes.size();
        auto loaded = result.catalog.read({catalog.bytes.data(), catalog.bytes.size()}, *codecs, b);
        require(loaded.success, loaded.diagnostics.c_str());
        const auto sections = catalogSections({catalog.bytes.data(), catalog.bytes.size()});
        R packedMetadata(sections.metadata);
        std::uint64_t payloadOffset = 0;
        const auto packedVersion = sections.packed
            ? digest({catalog.bytes.data(), catalog.bytes.size()})
            : core::pmr::String{&core::defaultCpuMemoryResource()};
        std::size_t metadataTotal = catalog.bytes.size();
        for (const auto& e : result.catalog.entries()) {
            core::io::FileReadResult metadata;
            if (sections.packed) {
                const auto bytes = packedMetadata.raw(e.metadataBytes);
                metadata.bytes.assign(bytes.begin(), bytes.end());
                std::filesystem::path path;
                require(resolveResourceSourcePath(root, "payload.arpbin", origin, path),
                    "COOKED-RESOURCE-PATH");
                intent.path = core::FilePath::utf8(path);
                intent.contentVersion = packedVersion;
            } else {
                std::filesystem::path path;
                require(
                    resolveResourceSourcePath(root, e.file, origin, path), "COOKED-RESOURCE-PATH");
                intent.path = core::FilePath::utf8(path);
                intent.contentVersion = e.contentVersion;
                intent.range = core::io::FileReadRange::exactRange(0, headerSize);
                intent.maxBytes = headerSize;
                auto h = reader(intent);
                result.metadataReadBytes += h.bytes.size();
                auto expected = header(e);
                require(h.bytes == expected, "COOKED-HEADER-MISMATCH");
                require(e.metadataBytes <= b.maxMetadataBytes - metadataTotal,
                    "COOKED-METADATA-TOTAL-BUDGET");
                metadataTotal += static_cast<std::size_t>(e.metadataBytes);
                intent.range = core::io::FileReadRange::exactRange(headerSize, e.metadataBytes);
                intent.maxBytes = static_cast<std::size_t>(e.metadataBytes);
                metadata = reader(intent);
                result.metadataReadBytes += metadata.bytes.size();
            }
            require(digest({metadata.bytes.data(), metadata.bytes.size()}) == e.metadataHash,
                "COOKED-METADATA-HASH");
            auto spec = core::makeShared<CookedResourceSpec>();
            spec->entry = e;
            spec->metadata = std::move(metadata.bytes);
            spec->codec = codecs->find(e.reference.type);
            spec->maxDecodedBytes = b.maxDecodedBytes;
            spec->archiveOptions = options(b);
            spec->payloadRead = intent;
            spec->payloadRead.role = "cooked-payload";
            spec->payloadRead.range = core::io::FileReadRange::exactRange(
                sections.packed ? payloadOffset : headerSize + e.metadataBytes, e.payloadBytes);
            spec->payloadRead.maxBytes = static_cast<std::size_t>(e.payloadBytes);
            payloadOffset += e.payloadBytes;
            // Validate typed metadata before registration. No payload or source buffers are retained.
            auto factory = spec->codec->prepareDescriptorFactory(spec);
            require(bool(factory), "COOKED-DESCRIPTOR-FACTORY");
            auto checked = factory();
            require(bool(checked), "COOKED-DESCRIPTOR");
            result.registrations.push_back({e.identity(), std::move(factory)});
        }
        result.success = true;
    } catch (const std::exception& e) {
        result.registrations.clear();
        result.diagnostics = e.what();
    }
    return result;
}
} // namespace

CookedResourcePlan CookedResourceLoader::plan(const std::filesystem::path& root,
    core::runtime::AsyncRuntime& runtime,
    core::SharedPtr<const CookedResourceCodecRegistry> codecs,
    CookedResourceBudget b,
    ResourceFileOrigin origin) const
{
    if (!runtime.valid()) {
        CookedResourcePlan r;
        r.diagnostics = "COOKED-RUNTIME";
        return r;
    }
    return buildPlan(root, std::move(codecs), b, origin, [&](const auto& intent) {
        return read(runtime, intent);
    });
}

CookedResourcePlan CookedResourceLoader::planSources(const std::filesystem::path& root,
    const ResourceSourceSet& sources,
    core::SharedPtr<const CookedResourceCodecRegistry> codecs,
    CookedResourceBudget b,
    ResourceFileOrigin origin) const
{
    return buildPlan(root, std::move(codecs), b, origin, [&](const ResourceFileReadIntent& intent) {
        auto* source = sources.find(intent);
        require(source && source->bytes.size() <= intent.maxBytes, "COOKED-SOURCE-NOT-PRELOADED");
        core::io::FileReadResult r;
        r.status = core::io::FileIoStatus::Success;
        r.bytes = source->bytes;
        return r;
    });
}

bool CookedResourceLoader::planFileReadIntents(const std::filesystem::path& root,
    const ResourceSourceSet& sources,
    core::pmr::Vector<ResourceFileReadIntent>& reads,
    core::pmr::String& error,
    CookedResourceBudget b,
    ResourceFileOrigin origin) const
{
    try {
        ResourceFileReadIntent catalog;
        catalog.path = core::FilePath::utf8(root / "catalog.arpbin");
        catalog.pathScope = origin.pathScope;
        catalog.mount = origin.mount;
        catalog.maxBytes = b.maxMetadataBytes;
        reads.push_back(catalog);
        const auto* acquired = sources.find(catalog);
        if (!acquired)
            return true;
        require(acquired->bytes.size() <= b.maxMetadataBytes, "COOKED-CATALOG-BUDGET");
        // Read-only shape parsing discovers ranges. Full integrity/schema/DAG
        // validation is repeated by planSources before any registration.
        const auto sections = catalogSections({acquired->bytes.data(), acquired->bytes.size()});
        auto decoded = reflection::decode_serialization_value(sections.records, options(b));
        require(decoded.success && decoded.value.is_sequence()
                && decoded.value.size() <= b.maxResources,
            "COOKED-CATALOG-BUDGET");
        std::size_t total = acquired->bytes.size();
        R packedMetadata(sections.metadata);
        for (const auto& node : decoded.value.get_value_ref<const V::sequence_type&>()) {
            const auto e = entry(node);
            require(e.file == e.reference.id + ".arpres.bin" && isResourceUuid(e.reference.id)
                    && e.metadataBytes && e.metadataBytes <= b.maxMetadataBytes,
                "COOKED-METADATA-BUDGET");
            if (sections.packed) {
                require(digest(packedMetadata.raw(e.metadataBytes)) == e.metadataHash,
                    "COOKED-METADATA-HASH");
                continue;
            }
            require(e.metadataBytes <= b.maxMetadataBytes - total, "COOKED-METADATA-BUDGET");
            total += static_cast<std::size_t>(e.metadataBytes);
            std::filesystem::path path;
            require(resolveResourceSourcePath(root, e.file, origin, path), "COOKED-RESOURCE-PATH");
            auto intent = catalog;
            intent.path = core::FilePath::utf8(path);
            intent.contentVersion = e.contentVersion;
            intent.range = core::io::FileReadRange::exactRange(0, headerSize);
            intent.maxBytes = headerSize;
            reads.push_back(intent);
            intent.range = core::io::FileReadRange::exactRange(headerSize, e.metadataBytes);
            intent.maxBytes = static_cast<std::size_t>(e.metadataBytes);
            reads.push_back(std::move(intent));
        }
        require(!sections.packed || !packedMetadata.remaining(), "COOKED-METADATA-TRAILING");
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

CookedResourceResult CookedResourceLoader::registerPlan(
    const CookedResourcePlan& plan, ResourceDomain& domain) const
{
    if (!plan.success)
        return {false, plan.diagnostics};
    auto result = domain.registerResources({plan.registrations.data(), plan.registrations.size()});
    return {result.success(), result.diagnosticId ? result.diagnosticId : ""};
}

CookedResourceResult ResourceCookService::pack(const std::filesystem::path& source,
    const std::filesystem::path& output,
    core::SharedPtr<const CookedResourceCodecRegistry> codecs,
    CookedResourceBudget b) const
{
    try {
        require(b.maxMetadataBytes <= SIZE_MAX - headerSize
                && b.maxPayloadBytes <= SIZE_MAX - headerSize - b.maxMetadataBytes,
            "COOKED-PACK-BUDGET");
        core::io::StagedDirectory staging;
        const bool started = staging.begin(output);
        require(started, staging.diagnostic().c_str());
        core::runtime::AsyncRuntime runtime({128, 2, 64, 32});
        auto plan = CookedResourceLoader().plan(source, runtime, codecs, b);
        require(plan.success, plan.diagnostics.c_str());
        auto inputCatalog = core::readBinaryFile(source / "catalog.arpbin", b.maxMetadataBytes);
        require(inputCatalog.success, "COOKED-PACK-CATALOG");
        require(!catalogSections({inputCatalog.bytes.data(), inputCatalog.bytes.size()}).packed,
            "COOKED-PACK-INPUT-MUST-BE-LOOSE");
        W metadata(b.maxMetadataBytes);
        std::ofstream payload(staging.path() / "payload.arpbin", std::ios::binary);
        require(bool(payload), "COOKED-PACK-WRITE");
        for (const auto& e : plan.catalog.entries()) {
            ResourceFileReadIntent intent;
            std::filesystem::path path;
            require(resolveResourceSourcePath(source, e.file, {}, path), "COOKED-RESOURCE-PATH");
            intent.path = core::FilePath::utf8(path);
            intent.pathScope = core::io::FilePathScope::HostFilesystem;
            intent.maxBytes =
                headerSize + static_cast<std::size_t>(e.metadataBytes + e.payloadBytes);
            auto original = read(runtime, intent);
            R file({original.bytes.data(), original.bytes.size()});
            const auto h = file.raw(headerSize);
            const auto expected = header(e);
            require(std::equal(h.begin(), h.end(), expected.begin()), "COOKED-HEADER-MISMATCH");
            const auto m = file.raw(e.metadataBytes);
            const auto p = file.raw(e.payloadBytes);
            require(!file.remaining(), "COOKED-RESOURCE-TRAILING");
            require(
                digest(m) == e.metadataHash && digest(p) == e.payloadHash, "COOKED-PACK-INTEGRITY");
            metadata.raw(m);
            if (!p.empty())
                payload.write(reinterpret_cast<const char*>(p.data()),
                    static_cast<std::streamsize>(p.size()));
            require(bool(payload), "COOKED-PACK-WRITE");
        }
        payload.close();
        require(bool(payload), "COOKED-PACK-WRITE");
        auto records = plan.catalog.write(b);
        require(records.success, records.diagnostics.c_str());
        const auto sections = catalogSections({records.bytes.data(), records.bytes.size()});
        W content(b.maxMetadataBytes);
        content.raw(sections.records);
        content.raw({metadata.bytes().data(), metadata.bytes().size()});
        W catalog(b.maxMetadataBytes);
        catalog.integer(catalogMagic);
        catalog.integer(2, 4);
        catalog.integer(sections.records.size());
        const auto hash =
            core::Sha256::digest(view({content.bytes().data(), content.bytes().size()}));
        catalog.raw({hash.data(), hash.size()});
        catalog.raw({content.bytes().data(), content.bytes().size()});
        save(staging.path() / "catalog.arpbin", {catalog.bytes().data(), catalog.bytes().size()});
        // Re-read the new layout, validate every descriptor and independently
        // hash each payload range before publishing. No C++ layout is stored.
        auto verified = CookedResourceLoader().plan(staging.path(), runtime, codecs, b);
        require(verified.success, verified.diagnostics.c_str());
        std::uint64_t offset = 0;
        for (const auto& e : verified.catalog.entries()) {
            if (e.payloadBytes) {
                ResourceFileReadIntent intent;
                intent.path = core::FilePath::utf8(staging.path() / "payload.arpbin");
                intent.pathScope = core::io::FilePathScope::HostFilesystem;
                intent.range = core::io::FileReadRange::exactRange(offset, e.payloadBytes);
                intent.maxBytes = static_cast<std::size_t>(e.payloadBytes);
                auto bytes = read(runtime, intent);
                require(digest({bytes.bytes.data(), bytes.bytes.size()}) == e.payloadHash,
                    "COOKED-PACK-INTEGRITY");
            }
            offset += e.payloadBytes;
        }
        const bool published = staging.publish(std::chrono::milliseconds(500));
        require(published, staging.diagnostic().c_str());
        return {true};
    } catch (const std::exception& e) {
        return {false, e.what()};
    }
}

CookedResourceResult ResourceCookService::cook(core::ArrayView<const ResourceCookInput> inputs,
    const std::filesystem::path& output,
    core::SharedPtr<const CookedResourceCodecRegistry> codecs,
    CookedResourceBudget b) const
{
    try {
        require(codecs && codecs->frozen() && !inputs.empty() && inputs.size() <= b.maxResources
                && b.maxTotalBytes && b.maxMetadataBytes <= SIZE_MAX - headerSize
                && b.maxPayloadBytes <= SIZE_MAX - headerSize - b.maxMetadataBytes,
            "COOKED-COOK-CONFIG");
        const bool replacing = std::filesystem::exists(output);
        CookedResourceCatalog previous;
        if (replacing) {
            auto old = core::readBinaryFile(output / "catalog.arpbin", b.maxMetadataBytes);
            require(old.success, "COOKED-EXISTING-DELIVERY");
            auto checked = previous.read({old.bytes.data(), old.bytes.size()}, *codecs, b);
            require(checked.success, "COOKED-EXISTING-DELIVERY-RECOOK-TO-NEW-DIRECTORY");
        }
        auto pending = output;
        if (replacing)
            pending += std::filesystem::path(".cook-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        core::io::StagedDirectory staging;
        const bool stagingStarted = staging.begin(pending);
        require(stagingStarted, staging.diagnostic().c_str());
        core::pmr::Vector<CookedResourceEntry> entries{&core::defaultCpuMemoryResource()};
        std::size_t total = 0;
        for (const auto& input : inputs) {
            require(bool(input.resource), "COOKED-COOK-RESOURCE");
            const auto codec = codecs->find(input.resource->reference().type);
            require(bool(codec), "COOKED-UNKNOWN-CODEC");
            CookedResourceEntry e;
            e.reference = input.resource->reference();
            e.dependencies = input.dependencies;
            std::sort(
                e.dependencies.begin(), e.dependencies.end(), [](const auto& a, const auto& b) {
                    return a.binding.id == b.binding.id ? a.binding.usage < b.binding.usage
                                                        : a.binding.id < b.binding.id;
                });
            e.schema = codec->schema();
            require(isResourceUuid(e.reference.id), "COOKED-INVALID-UUID");
            e.file = e.reference.id + ".arpres.bin";
            auto metadata = codec->writeMetadata(*input.resource, options(b));
            require(metadata.success, metadata.diagnostics.c_str());
            auto payload = codec->cookPayload(*input.resource, input.sourceRoot, b);
            e.metadataBytes = metadata.bytes.size();
            e.payloadBytes = payload.size();
            require(e.metadataBytes <= b.maxMetadataBytes && e.payloadBytes <= b.maxPayloadBytes,
                "COOKED-SECTION-BUDGET");
            require(e.metadataBytes <= b.maxTotalBytes - total, "COOKED-TOTAL-BUDGET");
            total += metadata.bytes.size();
            require(e.payloadBytes <= b.maxTotalBytes - total, "COOKED-TOTAL-BUDGET");
            total += payload.size();
            e.metadataHash = digest({metadata.bytes.data(), metadata.bytes.size()});
            e.payloadHash = digest({payload.data(), payload.size()});
            e.contentVersion = version(e);
            W file(b.maxMetadataBytes + b.maxPayloadBytes + headerSize);
            const auto h = header(e);
            file.raw({h.data(), h.size()});
            file.raw({metadata.bytes.data(), metadata.bytes.size()});
            file.raw({payload.data(), payload.size()});
            const auto target = staging.path() / std::filesystem::path(e.file);
            bool reused = false;
            if (const auto* old = previous.find(e.reference.id);
                old && old->contentVersion == e.contentVersion) {
                auto original = core::readBinaryFile(
                    output / std::filesystem::path(e.file), file.bytes().size());
                if (original.success && original.bytes == file.bytes()) {
                    std::filesystem::copy_file(output / std::filesystem::path(e.file), target);
                    reused = true;
                }
            }
            if (!reused)
                save(target, {file.bytes().data(), file.bytes().size()});
            entries.push_back(std::move(e));
        }
        CookedResourceCatalog catalog;
        auto checked = catalog.assign(std::move(entries), *codecs, b);
        require(checked.success, checked.diagnostics.c_str());
        auto bytes = catalog.write(b);
        require(bytes.success, bytes.diagnostics.c_str());
        save(staging.path() / "catalog.arpbin", {bytes.bytes.data(), bytes.bytes.size()});
        // Real read/prepare/admit of the staged delivery before publication.
        core::runtime::AsyncRuntime runtime({128, 2, 64, 32});
        auto plan = CookedResourceLoader().plan(staging.path(), runtime, codecs, b);
        require(plan.success, plan.diagnostics.c_str());
        ResourceManager manager;
        ResourceDomainBudget domainBudget{b.maxResources,
            b.maxResources,
            b.maxResources,
            1,
            {b.maxResources, b.maxResources, b.maxTotalBytes, b.maxDecodedBytes, 32}};
        ResourceDomain domain(manager, runtime, domainBudget);
        auto registered = CookedResourceLoader().registerPlan(plan, domain);
        require(registered.success, registered.diagnostics.c_str());
        core::pmr::Vector<ResourceLease> verified{&core::defaultCpuMemoryResource()};
        for (const auto& e : plan.catalog.entries()) {
            auto loaded = domain.acquire(e.identity());
            require(loaded.result.success(), loaded.diagnostics.c_str());
            verified.push_back(std::move(loaded.lease));
        }
        verified.clear();
        domain.collect();
        const bool published = staging.publish();
        require(published, staging.diagnostic().c_str());
        if (replacing) {
            auto backup = pending;
            backup += ".previous";
            std::error_code error;
            std::filesystem::rename(output, backup, error);
            if (error) {
                std::filesystem::remove_all(pending);
                throw std::runtime_error("COOKED-PUBLISH-PRESERVED-ORIGINAL");
            }
            std::filesystem::rename(pending, output, error);
            if (error) {
                std::error_code restore;
                std::filesystem::rename(backup, output, restore);
                if (restore)
                    throw std::runtime_error("COOKED-PUBLISH-RESTORE-FAILED-PREVIOUS-AT-BACKUP");
                std::filesystem::remove_all(pending);
                throw std::runtime_error("COOKED-PUBLISH-PRESERVED-ORIGINAL");
            }
            // Retain unrelated user files in the old directory. Only delete our
            // verified previous artifacts; a nonempty backup remains recoverable.
            for (const auto& e : previous.entries())
                std::filesystem::remove(backup / std::filesystem::path(e.file), error);
            std::filesystem::remove(backup / "catalog.arpbin", error);
            std::filesystem::remove(backup, error);
        }
        return {true};
    } catch (const std::exception& e) {
        return {false, e.what()};
    }
}
} // namespace arp::resource
