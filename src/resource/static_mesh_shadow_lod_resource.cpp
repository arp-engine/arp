#include "resource/static_mesh_shadow_lod_resource.h"

#include "assets/mesh/source_index_repacking.h"
#include "assets/mesh_source_identity.h"
#include "core/encoding/yaml_contract.h"
#include "core/hash/sha256.h"
#include "core/io/file_io.h"
#include "resource/resource_source_io.h"

#include <algorithm>
#include <cstring>

namespace arp::resource {
namespace {
using Yaml = core::YamlContract;
using Node = Yaml::Node;

void require(bool condition, const char* diagnostic)
{
    if (!condition)
        throw core::DiagnosticError(diagnostic);
}

Node readDocument(
    const std::filesystem::path& path, std::size_t budget, const core::pmr::String& expectedDigest)
{
    const auto read = core::readTextFile(path, budget);
    require(read.success, "SHADOW-LOD-YAML-READ-FAILED");
    require(core::Sha256::matchesHexDigest(read.text, expectedDigest),
        "SHADOW-LOD-PARSED-CERTIFICATE-HASH-MISMATCH");
    return Yaml::parseSingleDocument(read.text);
}

std::size_t byteCount(const Node& node, const char* key, std::size_t budget)
{
    const auto count = Yaml::uint64(Yaml::required(node, key));
    require(count > 0 && count <= budget, "SHADOW-LOD-LOAD-BUDGET-EXCEEDED");
    return static_cast<std::size_t>(count);
}

std::filesystem::path checkedFile(
    const std::filesystem::path& root, const Node& binding, std::size_t budget)
{
    const auto path = root / std::filesystem::path(Yaml::text(binding, "source").c_str());
    const auto count = byteCount(binding, "bytes", budget);
    std::error_code error;
    require(std::filesystem::file_size(path, error) == count && !error,
        "SHADOW-LOD-FILE-SIZE-MISMATCH");
    core::ByteBuffer digest;
    require(core::Sha256::digestFile(path, digest)
            && core::Sha256::matchesRawHexDigest(digest, Yaml::text(binding, "sha256")),
        "SHADOW-LOD-FILE-HASH-MISMATCH");
    return path;
}

std::uint64_t prototypeKey(std::uint32_t mesh, std::uint32_t primitive)
{
    return (std::uint64_t(mesh) << 32u) | primitive;
}

} // namespace

StaticMeshShadowLodResource::StaticMeshShadowLodResource(core::pmr::String id,
    core::pmr::String sourceId,
    std::filesystem::path projectRoot,
    std::filesystem::path manifestPath)
    : ResourceBase({std::move(id), resourceTypeName(), {}, "Prototype Complete", {}}),
      sourceResourceId_(std::move(sourceId)), projectRoot_(std::move(projectRoot)),
      manifestPath_(std::move(manifestPath))
{
    addDependency({sourceResourceId_, "shadow-lod-original-generation"});
}

ResourceLoadResult StaticMeshShadowLodResource::load(
    ResourceManager& manager, const ResourceLoadContext& context)
{
    resetPrepared();
    try {
        require(!context.requirePreloadedSources, "SHADOW-LOD-PREPARATION-ONLY");
        require(context.maxDecodedBytes != 0, "SHADOW-LOD-EXPLICIT-BUDGET-REQUIRED");
        prepare(manager, context);
        auto result = ResourceBase::load(manager, context);
        if (!result.success)
            resetPrepared();
        return result;
    } catch (const core::YamlContractError& error) {
        resetPrepared();
        return resourceLoadFailure(*this, error.diagnostic);
    } catch (const std::exception& error) {
        resetPrepared();
        return resourceLoadFailure(*this, error.what());
    }
}

void StaticMeshShadowLodResource::resetPrepared()
{
    core::pmr::Vector<StaticMeshShadowLodSource>{&core::defaultCpuMemoryResource()}.swap(sources_);
    sourceGeneration_ = 0;
    candidates_ = {};
    core::pmr::Vector<std::uint32_t>{&core::defaultCpuMemoryResource()}.swap(
        reorderedOriginalIndices_);
    indexOrderVersion_ = 0;
    manifestDigest_ = {};
    shadowTexelCeiling_ = normalizedDepthCeiling_ = 0;
}

void StaticMeshShadowLodResource::prepare(
    ResourceManager& manager, const ResourceLoadContext& context)
{
    const auto handle = manager.find<MeshAssetResource>(sourceResourceId_);
    require(handle.ready(), "SHADOW-LOD-ORIGINAL-NOT-READY");
    const auto& original = *handle.get();
    require(original.immutableMeshGeometry() && original.sourceIdentityVersion() == 1,
        "SHADOW-LOD-ORIGINAL-IDENTITY-REQUIRED");
    const auto read = core::readTextFile(manifestPath_, context.maxDecodedBytes);
    require(read.success, "SHADOW-LOD-MANIFEST-READ-FAILED");
    const auto digest = core::Sha256::digest(read.text);
    std::copy(digest.begin(), digest.end(), manifestDigest_.begin());
    const auto manifest = Yaml::parseSingleDocument(read.text);
    const auto version = Yaml::uint32(manifest, "contractVersion");
    require(Yaml::text(manifest, "type") == "StaticMeshShadowLodAsset"
            && (version == 1 || version == 2)
            && Yaml::text(manifest, "status") == "Prototype Complete",
        "SHADOW-LOD-MANIFEST-CONTRACT-INVALID");
    require(core::Sha256::matchesRawHexDigest(
                original.sourceContentDigest(), Yaml::text(manifest, "sourceSha256")),
        "SHADOW-LOD-ORIGINAL-HASH-MISMATCH");
    const auto sourcePath =
        projectRoot_ / std::filesystem::path(Yaml::text(manifest, "source").c_str());
    std::error_code error;
    require(std::filesystem::equivalent(sourcePath, original.sourcePath(), error) && !error,
        "SHADOW-LOD-ORIGINAL-PATH-MISMATCH");
    const auto count = Yaml::uint32(manifest, "sourceCount");
    require(count == original.sourceIdentities().size()
            && count <= context.maxDecodedBytes / sizeof(StaticMeshShadowLodSource),
        "SHADOW-LOD-SOURCE-COUNT-MISMATCH");
    const auto& selection = Yaml::required(manifest, "selection");
    require(Yaml::text(selection, "status") == "ExperimentalInputsNotVisualAcceptance",
        "SHADOW-LOD-SELECTION-STATUS-INVALID");
    shadowTexelCeiling_ = Yaml::number(selection, "shadowTexelCeiling");
    normalizedDepthCeiling_ = Yaml::number(selection, "normalizedDepthCeiling");
    require(shadowTexelCeiling_ > 0 && normalizedDepthCeiling_ > 0,
        "SHADOW-LOD-SELECTION-BUDGET-INVALID");

    const auto& levels = Yaml::sequence(manifest, "levels");
    require(levels.size() == 2, "SHADOW-LOD-LEVEL-COUNT-INVALID");
    std::array<assets::StaticMeshSourceImportResult, 2> imports;
    std::array<core::pmr::Vector<std::uint32_t>, 3> bindings{
        core::pmr::Vector<std::uint32_t>{&core::defaultCpuMemoryResource()},
        core::pmr::Vector<std::uint32_t>{&core::defaultCpuMemoryResource()},
        core::pmr::Vector<std::uint32_t>{&core::defaultCpuMemoryResource()}};
    bindings[0] = assets::bindMeshSourceIdentities(*original.mesh(), original.sourceIdentities());
    core::pmr::Vector<std::uint32_t> sourceOrder{&core::defaultCpuMemoryResource()};
    if (version == 2) {
        const auto& orderBinding = Yaml::required(manifest, "indexOrder");
        const auto orderPath =
            checkedFile(manifestPath_.parent_path(), orderBinding, context.maxDecodedBytes);
        const auto document =
            readDocument(orderPath, context.maxDecodedBytes, Yaml::text(orderBinding, "sha256"));
        require(Yaml::text(document, "type") == "MeshSourceIndexOrder"
                && Yaml::uint32(document, "contractVersion") == 1
                && Yaml::text(document, "sourceSha256") == Yaml::text(manifest, "sourceSha256"),
            "SHADOW-LOD-INDEX-ORDER-CONTRACT");
        const auto& entries = Yaml::sequence(document, "sources");
        require(entries.size() == count, "SHADOW-LOD-INDEX-ORDER-COUNT");
        core::pmr::UnorderedMap<std::uint64_t, std::uint32_t> sourceByKey{
            &core::defaultCpuMemoryResource()};
        sourceByKey.reserve(count);
        for (std::uint32_t r = 0; r < count; ++r)
            sourceByKey.emplace(
                assets::meshSourceIdentityKey(original.sourceIdentities()[bindings[0][r]]), r);
        sourceOrder.reserve(count);
        for (const auto& entry : entries) {
            const auto key = (std::uint64_t(Yaml::uint32(entry, "node")) << 32u)
                | Yaml::uint32(entry, "primitive");
            const auto found = sourceByKey.find(key);
            require(found != sourceByKey.end(), "SHADOW-LOD-INDEX-ORDER-BIJECTION");
            sourceOrder.push_back(found->second);
            sourceByKey.erase(found);
        }
        require(sourceByKey.empty(), "SHADOW-LOD-INDEX-ORDER-BIJECTION");
    }
    std::array<core::pmr::UnorderedMap<std::uint64_t, std::uint32_t>, 2> candidatesByIdentity{
        core::pmr::UnorderedMap<std::uint64_t, std::uint32_t>{&core::defaultCpuMemoryResource()},
        core::pmr::UnorderedMap<std::uint64_t, std::uint32_t>{&core::defaultCpuMemoryResource()}};
    std::uint64_t retained = std::uint64_t(count) * sizeof(StaticMeshShadowLodSource)
        + (version == 2 ? std::uint64_t(original.mesh()->indices.size()) * sizeof(std::uint32_t)
                        : 0);
    require(retained <= context.maxDecodedBytes, "SHADOW-LOD-LOAD-BUDGET-EXCEEDED");
    for (unsigned level = 1; level <= 2; ++level) {
        const auto& spec = levels[level - 1];
        require(Yaml::uint32(spec, "level") == level, "SHADOW-LOD-LEVEL-ORDER-INVALID");
        const auto vertices =
            byteCount(spec, "vertices", context.maxDecodedBytes / sizeof(assets::MeshVertex));
        const auto indices =
            byteCount(spec, "indices", context.maxDecodedBytes / sizeof(std::uint32_t));
        require(vertices <= UINT32_MAX && indices <= UINT32_MAX, "SHADOW-LOD-LOAD-BUDGET-EXCEEDED");
        const std::uint64_t bytes = std::uint64_t(vertices) * sizeof(assets::MeshVertex)
            + std::uint64_t(indices) * sizeof(std::uint32_t)
            + std::uint64_t(count) * sizeof(assets::MeshSourceRange);
        require(bytes <= context.maxDecodedBytes - retained, "SHADOW-LOD-LOAD-BUDGET-EXCEEDED");
        retained += bytes;
        const auto path = checkedFile(manifestPath_.parent_path(), spec, context.maxDecodedBytes);
        auto& imported = imports[level - 1];
        imported = assets::importStaticMeshWithSourceIdentityFromGlbFile(path,
            {byteCount(spec, "bytes", context.maxDecodedBytes),
                static_cast<std::uint32_t>(vertices),
                static_cast<std::uint32_t>(indices),
                count});
        if (!imported.geometry.success)
            throw core::DiagnosticError(imported.geometry.diagnostics);
        require(imported.identityVersion == 1, "SHADOW-LOD-CANDIDATE-IMPORT-FAILED");
        require(core::Sha256::matchesRawHexDigest(
                    imported.sourceContentDigest, Yaml::text(spec, "sha256")),
            "SHADOW-LOD-IMPORTED-CONTENT-HASH-MISMATCH");
        require(imported.identities.size() == count
                && imported.geometry.mesh.vertices.size() == vertices
                && imported.geometry.mesh.indices.size() == indices,
            "SHADOW-LOD-CANDIDATE-COUNT-MISMATCH");
        assets::groupMeshSectionsByMaterial(imported.geometry.mesh, true);
        bindings[level] =
            assets::bindMeshSourceIdentities(imported.geometry.mesh, imported.identities);
        auto& map = candidatesByIdentity[level - 1];
        map.reserve(count);
        for (std::uint32_t r = 0; r < count; ++r)
            require(
                map.emplace(
                       assets::meshSourceIdentityKey(imported.identities[bindings[level][r]]), r)
                    .second,
                "SHADOW-LOD-CANDIDATE-IDENTITY-DUPLICATE");
    }

    const auto certificatePath = checkedFile(manifestPath_.parent_path(),
        Yaml::required(manifest, "certificates"),
        context.maxDecodedBytes);
    const auto certificates = readDocument(certificatePath,
        context.maxDecodedBytes,
        Yaml::text(Yaml::required(manifest, "certificates"), "sha256"));
    require(Yaml::text(certificates, "type") == "TriangleSurfaceCertificates"
            && Yaml::uint32(certificates, "contractVersion") == 1
            && Yaml::text(certificates, "sourceSha256") == Yaml::text(manifest, "sourceSha256")
            && core::Sha256::isHexDigest(Yaml::text(certificates, "certifierSha256")),
        "SHADOW-LOD-CERTIFICATE-CONTRACT-INVALID");
    const auto& prototypes = Yaml::sequence(manifest, "prototypes");
    const auto& proofs = Yaml::sequence(certificates, "certificates");
    require(
        !prototypes.empty() && prototypes.size() <= count && proofs.size() == prototypes.size() * 2,
        "SHADOW-LOD-PROTOTYPE-COUNT-INVALID");
    core::pmr::UnorderedMap<std::uint64_t, std::size_t> prototypeByKey{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<bool> used(prototypes.size(), false, &core::defaultCpuMemoryResource());
    for (std::size_t i = 0; i < prototypes.size(); ++i)
        require(prototypeByKey
                    .emplace(prototypeKey(Yaml::uint32(prototypes[i], "sourceMesh"),
                                 Yaml::uint32(prototypes[i], "sourcePrimitive")),
                        i)
                    .second,
            "SHADOW-LOD-PROTOTYPE-DUPLICATE");

    sources_.reserve(count);
    for (std::uint32_t r = 0; r < count; ++r) {
        const auto& identity = original.sourceIdentities()[bindings[0][r]];
        StaticMeshShadowLodSource source;
        source.node = identity.node;
        source.mesh = identity.mesh;
        source.primitive = identity.primitive;
        std::copy_n(identity.worldAffine, 16, source.worldAffine.begin());
        std::copy_n(identity.localAbsolutePosition, 3, source.localAbsoluteSupport.begin());
        source.ranges[0] = original.mesh()->sourceRanges[r];
        const auto prototype = prototypeByKey.find(prototypeKey(identity.mesh, identity.primitive));
        source.approximable = prototype != prototypeByKey.end();
        if (source.approximable) {
            used[prototype->second] = true;
            const auto& spec = prototypes[prototype->second];
            require(Yaml::uint32(spec, "sourceMaterial") == identity.sourceIndices.materialSlot,
                "SHADOW-LOD-PROTOTYPE-MATERIAL-MISMATCH");
            const auto& support = Yaml::sequence(spec, "localAbsoluteSupport");
            require(support.size() == 3, "SHADOW-LOD-SUPPORT-INVALID");
            for (unsigned a = 0; a < 3; ++a) {
                source.localAbsoluteSupport[a] = Yaml::number(support[a]);
                require(source.localAbsoluteSupport[a] >= identity.localAbsolutePosition[a],
                    "SHADOW-LOD-SUPPORT-NOT-CONSERVATIVE");
            }
        }
        for (unsigned level = 1; level <= 2; ++level) {
            const auto& imported = imports[level - 1];
            const auto found =
                candidatesByIdentity[level - 1].find(assets::meshSourceIdentityKey(identity));
            require(found != candidatesByIdentity[level - 1].end(), "SHADOW-LOD-IDENTITY-MISSING");
            const auto candidateRange = found->second;
            const auto& candidateIdentity = imported.identities[bindings[level][candidateRange]];
            require(candidateIdentity.mesh == identity.mesh
                    && candidateIdentity.sourceIndices.materialSlot
                        == identity.sourceIndices.materialSlot
                    && std::memcmp(identity.worldAffine,
                           candidateIdentity.worldAffine,
                           sizeof(identity.worldAffine))
                        == 0,
                "SHADOW-LOD-IDENTITY-BINDING-MISMATCH");
            source.ranges[level] = imported.geometry.mesh.sourceRanges[candidateRange];
            if (!source.approximable) {
                assets::validateExactMeshSourceRangeAttributes(*original.mesh(),
                    source.ranges[0],
                    imported.geometry.mesh,
                    source.ranges[level]);
                continue;
            }
            for (unsigned a = 0; a < 3; ++a)
                require(
                    source.localAbsoluteSupport[a] >= candidateIdentity.localAbsolutePosition[a],
                    "SHADOW-LOD-SUPPORT-NOT-CONSERVATIVE");
            const auto& specs = Yaml::sequence(prototypes[prototype->second], "levels");
            require(specs.size() == 2, "SHADOW-LOD-PROTOTYPE-LEVELS-INVALID");
            const auto& spec = specs[level - 1];
            const auto& certificate = proofs[prototype->second * 2 + level - 1];
            const auto& proof = Yaml::required(certificate, "proof");
            for (const auto* key : {"sourceMesh", "sourcePrimitive", "level"})
                require(Yaml::uint32(spec, key) == Yaml::uint32(certificate, key),
                    "SHADOW-LOD-CERTIFICATE-BINDING-MISMATCH");
            require(Yaml::uint32(spec, "sourceMesh") == identity.mesh
                    && Yaml::uint32(spec, "sourcePrimitive") == identity.primitive
                    && Yaml::uint32(spec, "level") == level
                    && Yaml::uint32(proof, "contractVersion") == 1,
                "SHADOW-LOD-CERTIFICATE-BINDING-MISMATCH");
            const auto distance = Yaml::number(spec, "upperBound");
            require(distance >= 0 && distance == Yaml::number(certificate, "upperBound")
                    && distance == Yaml::number(proof, "upperBound"),
                "SHADOW-LOD-CERTIFICATE-DISTANCE-MISMATCH");
            if (Yaml::boolean(proof, "exactSurfaceSet")) {
                require(distance == 0 && Yaml::required(proof, "forward").is_null()
                        && Yaml::required(proof, "reverse").is_null(),
                    "SHADOW-LOD-CERTIFICATE-EXACT-INVALID");
            } else {
                const auto forward = Yaml::number(Yaml::required(proof, "forward"), "upperBound");
                const auto reverse = Yaml::number(Yaml::required(proof, "reverse"), "upperBound");
                require(forward >= 0 && reverse >= 0 && distance == std::max(forward, reverse),
                    "SHADOW-LOD-CERTIFICATE-DISTANCE-MISMATCH");
            }
            for (const auto* key : {"sourceSurfaceSha256", "targetSurfaceSha256"})
                require(Yaml::text(spec, key) == Yaml::text(certificate, key)
                        && Yaml::text(spec, key) == Yaml::text(proof, key),
                    "SHADOW-LOD-CERTIFICATE-SURFACE-MISMATCH");
            require(core::Sha256::matchesRawHexDigest(
                        identity.localSurfaceDigest, Yaml::text(spec, "sourceSurfaceSha256"))
                    && core::Sha256::matchesRawHexDigest(candidateIdentity.localSurfaceDigest,
                        Yaml::text(spec, "targetSurfaceSha256")),
                "SHADOW-LOD-IMPORTED-SURFACE-MISMATCH");
            source.surfaceError[level] = distance;
        }
        sources_.push_back(source);
    }
    require(std::all_of(used.begin(),
                used.end(),
                [](bool value) {
                    return value;
                }),
        "SHADOW-LOD-PROTOTYPE-UNUSED");
    require(manager.generationOf(sourceResourceId_) == handle.generation(),
        "SHADOW-LOD-ORIGINAL-GENERATION-CHANGED");
    sourceGeneration_ = handle.generation();
    for (unsigned i = 0; i < 2; ++i)
        candidates_[i] = std::move(imports[i].geometry.mesh);
    if (version == 2) {
        for (unsigned level = 0; level < 3; ++level) {
            const auto& mesh = level ? candidates_[level - 1] : *original.mesh();
            core::pmr::Vector<std::uint32_t> order{&core::defaultCpuMemoryResource()};
            order.reserve(count);
            for (const auto r : sourceOrder) {
                const auto key = (std::uint64_t(sources_[r].node) << 32u) | sources_[r].primitive;
                order.push_back(level ? candidatesByIdentity[level - 1].at(key) : r);
            }
            const auto retainedBytes = processedByteCount();
            require(retainedBytes <= context.maxDecodedBytes, "SHADOW-LOD-LOAD-BUDGET-EXCEEDED");
            auto packed = assets::repackMeshSourceIndices(
                mesh, order, context.maxDecodedBytes - retainedBytes);
            for (std::uint32_t r = 0; r < count; ++r) {
                const auto key = (std::uint64_t(sources_[r].node) << 32u) | sources_[r].primitive;
                const auto old = level ? candidatesByIdentity[level - 1].at(key) : r;
                sources_[r].ranges[level] = packed.ranges[packed.oldToNew[old]];
            }
            if (level) {
                candidates_[level - 1].indices = std::move(packed.indices);
                candidates_[level - 1].sourceRanges = std::move(packed.ranges);
            } else {
                reorderedOriginalIndices_ = std::move(packed.indices);
            }
        }
        indexOrderVersion_ = 1;
    }
    require(processedByteCount() <= context.maxDecodedBytes, "SHADOW-LOD-LOAD-BUDGET-EXCEEDED");
}

const assets::MeshAsset& StaticMeshShadowLodResource::candidate(unsigned level) const
{
    require(level >= 1 && level <= 2 && sourceGeneration_, "SHADOW-LOD-CANDIDATE-LEVEL-INVALID");
    return candidates_[level - 1];
}

std::size_t StaticMeshShadowLodResource::processedByteCount() const noexcept
{
    std::size_t bytes = sources_.size() * sizeof(StaticMeshShadowLodSource)
        + reorderedOriginalIndices_.size() * sizeof(std::uint32_t);
    for (const auto& mesh : candidates_)
        bytes += mesh.vertices.size() * sizeof(assets::MeshVertex)
            + mesh.indices.size() * sizeof(std::uint32_t)
            + mesh.sourceRanges.size() * sizeof(assets::MeshSourceRange)
            + mesh.submeshes.size() * sizeof(assets::Submesh);
    return bytes;
}

void StaticMeshShadowLodResource::appendContentFingerprint(
    core::StableHash64& hasher) const noexcept
{
    hasher.appendString("StaticMeshShadowLodResource/1-2");
    hasher.appendU64(indexOrderVersion_);
    hasher.appendString(sourceResourceId_);
    hasher.appendU64(sourceGeneration_);
    hasher.appendBytes(manifestDigest_.data(), manifestDigest_.size());
}
} // namespace arp::resource
