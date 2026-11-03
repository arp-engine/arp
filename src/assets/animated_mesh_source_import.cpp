#include "assets/animated_mesh_source_import.h"

#include "assets/animated_mesh_coordinate_conversion.h"
#include "assets/mesh/glb_scene_source_contract.h"
#include "assets/mesh/glb_skeletal_source.h"

#include <optional>

namespace arp::assets {
namespace {
using namespace detail;
using Limits = AnimatedMeshSourceImportLimits;

AnimatedMeshSourceImportResult failure(const char* id, const char* detail = nullptr)
{
    AnimatedMeshSourceImportResult result;
    result.diagnosticId = id;
    try {
        result.diagnostics = detail ? detail : id;
    } catch (const std::bad_alloc&) { /* The stable ID is allocation-free. */
    }
    return result;
}

bool finiteSize(std::uint64_t value)
{
    return value && value < SIZE_MAX;
}

class AnimatedSourceDecoder final : private GlbGeometrySchema {
public:
    static AnimatedMeshSourceImportResult decode(
        core::ArrayView<const std::uint8_t> bytes, const Limits& limits)
    {
        try {
            return decodeChecked(bytes, limits);
        } catch (const Failure& error) {
            return failure(error.diagnostic);
        }
    }

private:
    static void charge(std::size_t& bytes, std::size_t count, std::size_t width)
    {
        chargeBudget(bytes, count, width, "ASSET-ANIMATED-SOURCE-DECODED-BUDGET");
    }

    static const GlbAccessorSpan& accessor(const GlbAccessorTable& table,
        const Json& attributes,
        const char* semantic,
        std::uint32_t components)
    {
        const auto* value = table.find(number(attributes, semantic));
        require(value && value->components() == components, "ASSET-ANIMATED-SOURCE-ATTRIBUTE");
        return *value;
    }

    static core::Vec3 unitVector(const GlbAccessorSpan& values, std::uint32_t index, float error)
    {
        core::Vec3 value;
        require(values.readFloat(index, 0, value.x) && values.readFloat(index, 1, value.y)
                && values.readFloat(index, 2, value.z),
            "ASSET-ANIMATED-SOURCE-FRAME");
        const auto length = std::hypot(double(value.x), double(value.y), double(value.z));
        core::Vec3 normalized;
        require(std::isfinite(length) && std::abs(length - 1) <= double(error)
                && core::tryNormalize(value, normalized),
            "ASSET-ANIMATED-SOURCE-FRAME");
        return normalized;
    }

    static std::size_t optionalCount(const Json& root, const char* name)
    {
        return root.find(name) ? array(root, name).size() : 0;
    }

    static void numberRange(const Json* value, double maximum)
    {
        if (value)
            require(value->type == Json::Type::Number && std::isfinite(value->numberValue)
                    && value->numberValue >= 0 && value->numberValue <= maximum,
                "ASSET-ANIMATED-SOURCE-MATERIAL-VALUE");
    }

    static void textureInfo(const Json& root, const Json* value)
    {
        if (!value)
            return;
        require(number(*value, "index") < optionalCount(root, "textures"),
            "ASSET-ANIMATED-SOURCE-TEXTURE");
        if (value->find("texCoord"))
            require(number(*value, "texCoord") == 0, "ASSET-ANIMATED-SOURCE-UV-SET-UNSUPPORTED");
    }

    static void admitMaterials(const Json& root, const Limits& limits)
    {
        for (const auto* declaration : {"extensionsUsed", "extensionsRequired"})
            if (root.find(declaration))
                for (const auto& extension : array(root, declaration))
                    require(extension.stringValue == "KHR_materials_emissive_strength",
                        "ASSET-ANIMATED-SOURCE-MATERIAL-EXTENSION-UNSUPPORTED");
        require(array(root, "materials").size() <= limits.materials.maxMaterials
                && optionalCount(root, "textures") <= limits.materials.maxTextures
                && optionalCount(root, "images") <= limits.materials.maxImages,
            "ASSET-ANIMATED-SOURCE-MATERIAL-BUDGET");
        if (root.find("samplers"))
            for (const auto& sampler : array(root, "samplers")) {
                keys(sampler, {"name", "wrapS", "wrapT", "extras"});
                for (const auto* wrap : {"wrapS", "wrapT"})
                    if (sampler.find(wrap)) {
                        const auto mode = number(sampler, wrap);
                        require(mode == 33071 || mode == 33648 || mode == 10497,
                            "ASSET-ANIMATED-SOURCE-SAMPLER");
                    }
            }
        if (root.find("textures"))
            for (const auto& texture : array(root, "textures")) {
                keys(texture, {"name", "source", "sampler", "extras"});
                require(number(texture, "source") < optionalCount(root, "images"),
                    "ASSET-ANIMATED-SOURCE-TEXTURE");
                if (texture.find("sampler"))
                    require(number(texture, "sampler") < optionalCount(root, "samplers"),
                        "ASSET-ANIMATED-SOURCE-SAMPLER");
            }
        if (root.find("images"))
            for (const auto& image : array(root, "images")) {
                keys(image, {"name", "uri", "bufferView", "mimeType", "extras"});
                for (const auto* fieldName : {"uri", "mimeType"})
                    if (const auto* value = image.find(fieldName))
                        require(value->type == Json::Type::String, "ASSET-ANIMATED-SOURCE-IMAGE");
                if (image.find("bufferView"))
                    require(number(image, "bufferView") < array(root, "bufferViews").size(),
                        "ASSET-ANIMATED-SOURCE-IMAGE");
            }
        for (const auto& material : array(root, "materials")) {
            keys(material,
                {"name",
                    "pbrMetallicRoughness",
                    "normalTexture",
                    "occlusionTexture",
                    "emissiveTexture",
                    "emissiveFactor",
                    "alphaMode",
                    "alphaCutoff",
                    "doubleSided",
                    "extensions",
                    "extras"});
            numberRange(material.find("alphaCutoff"), std::numeric_limits<float>::max());
            if (material.find("emissiveFactor")) {
                const auto& value = array(material, "emissiveFactor");
                require(value.size() == 3, "ASSET-ANIMATED-SOURCE-MATERIAL-VALUE");
                for (const auto& component : value)
                    numberRange(&component, 1);
            }
            if (const auto* pbr = material.find("pbrMetallicRoughness")) {
                keys(*pbr,
                    {"baseColorFactor",
                        "baseColorTexture",
                        "metallicFactor",
                        "roughnessFactor",
                        "metallicRoughnessTexture",
                        "extras"});
                numberRange(pbr->find("metallicFactor"), 1);
                numberRange(pbr->find("roughnessFactor"), 1);
                if (pbr->find("baseColorFactor")) {
                    const auto& value = array(*pbr, "baseColorFactor");
                    require(value.size() == 4, "ASSET-ANIMATED-SOURCE-MATERIAL-VALUE");
                    for (const auto& component : value)
                        numberRange(&component, 1);
                }
                for (const auto* fieldName : {"baseColorTexture", "metallicRoughnessTexture"}) {
                    const auto* value = pbr->find(fieldName);
                    if (value)
                        keys(*value, {"index", "texCoord", "extras"});
                    textureInfo(root, value);
                }
            }
            if (const auto* occlusion = material.find("occlusionTexture")) {
                keys(*occlusion, {"index", "texCoord", "strength", "extras"});
                numberRange(occlusion->find("strength"), 1);
            }
            if (const auto* emissive = material.find("emissiveTexture"))
                keys(*emissive, {"index", "texCoord", "extras"});
            if (const auto* normal = material.find("normalTexture")) {
                keys(*normal, {"index", "texCoord", "scale", "extras"});
                if (const auto* scale = normal->find("scale"))
                    require(scale->type == Json::Type::Number && scale->numberValue == 1,
                        "ASSET-ANIMATED-SOURCE-NORMAL-SCALE-UNSUPPORTED");
            }
            for (const auto* fieldName : {"normalTexture", "occlusionTexture", "emissiveTexture"})
                textureInfo(root, material.find(fieldName));
            if (const auto* extensions = material.find("extensions")) {
                for (const auto& extension : extensions->objectValue) {
                    require(extension.first == "KHR_materials_emissive_strength",
                        "ASSET-ANIMATED-SOURCE-MATERIAL-EXTENSION-UNSUPPORTED");
                    keys(extension.second, {"emissiveStrength", "extras"});
                    numberRange(extension.second.find("emissiveStrength"),
                        std::numeric_limits<float>::max());
                }
            }
        }
    }

    static void accountMaterials(
        const ImportedGlbMaterialAssets& materials, const Limits& limits, std::size_t& bytes)
    {
        auto metadata = limits.materials.maxMetadataBytes;
        auto images = limits.materials.maxEncodedImageBytes;
        const auto account = [&](std::size_t count, std::size_t width) {
            chargeBudget(metadata, count, width, "ASSET-ANIMATED-SOURCE-MATERIAL-BUDGET");
            charge(bytes, count, width);
        };
        account(materials.materials.size(), sizeof(ImportedMaterialAsset));
        account(materials.textures.size(), sizeof(ImportedTextureAsset));
        account(materials.images.size(), sizeof(ImportedImageAsset));
        for (const auto& material : materials.materials) {
            require(!material.hasUnsupportedMaterialExtensions,
                "ASSET-ANIMATED-SOURCE-MATERIAL-EXTENSION-UNSUPPORTED");
            account(material.name.size(), 1);
            account(material.sourceMaterialDocument.size(), 1);
            for (auto binding : {material.baseColorTexture,
                     material.normalTexture,
                     material.metallicRoughnessTexture,
                     material.occlusionTexture,
                     material.emissiveTexture})
                require(
                    binding == invalidImportedTextureIndex || binding < materials.textures.size(),
                    "ASSET-ANIMATED-SOURCE-TEXTURE");
            for (auto uv : {material.baseColorTextureCoordinateSet,
                     material.normalTextureCoordinateSet,
                     material.metallicRoughnessTextureCoordinateSet,
                     material.occlusionTextureCoordinateSet,
                     material.emissiveTextureCoordinateSet})
                require(uv == 0, "ASSET-ANIMATED-SOURCE-UV-SET-UNSUPPORTED");
        }
        for (const auto& texture : materials.textures) {
            account(texture.name.size(), 1);
            require(texture.sourceImage < materials.images.size(), "ASSET-ANIMATED-SOURCE-TEXTURE");
        }
        for (const auto& image : materials.images) {
            account(image.name.size(), 1);
            account(image.uri.size(), 1);
            account(image.mimeType.size(), 1);
            chargeBudget(
                images, image.encodedBytes.size(), 1, "ASSET-ANIMATED-SOURCE-IMAGE-BUDGET");
            charge(bytes, image.encodedBytes.size(), 1);
        }
    }

    static AnimatedMeshSourceImportResult decodeChecked(
        core::ArrayView<const std::uint8_t> bytes, const Limits& limits)
    {
        const core::pmr::String input(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        GlbChunks chunks;
        core::pmr::String diagnostic;
        require(readGlbChunks(input, chunks, diagnostic, GlbChunkPolicy::EmbeddedSourceV1),
            "ASSET-ANIMATED-SOURCE-CONTAINER");
        Json root;
        require(parseJson(chunks.json, root, {limits.maxJsonDepth, limits.maxJsonValues, false}),
            "ASSET-ANIMATED-SOURCE-JSON");
        if (const auto* issue = GlbSceneSourceContract::validate(root,
                chunks.bin,
                limits.geometry.colorPolicy == AnimatedMeshColorPolicy::DiscardValidatedRgba,
                GlbLayoutProfile::Animated))
            throw Failure{issue};
        const char* issue = nullptr;
        GlbAccessorTable accessors;
        if (!accessors.prepare(root,
                chunks.bin,
                {static_cast<std::size_t>(limits.maxSourceBytes),
                    limits.layout.maxBufferViews,
                    limits.layout.maxAccessors,
                    limits.layout.maxDescriptorBytes,
                    limits.layout.maxElements,
                    limits.layout.maxScalars},
                issue))
            throw Failure{issue};
        GlbAnimatedNodeGraph graph;
        const auto& s = limits.scene;
        if (!graph.prepare(root,
                accessors,
                limits.selectedScene,
                {s.maxNodes,
                    s.maxEdges,
                    s.maxScenes,
                    s.maxSceneRoots,
                    s.maxSkins,
                    s.maxSkinJoints,
                    s.maxMeshInstances,
                    s.maxWorkingBytes},
                issue))
            throw Failure{issue};
        GlbAnimationTrackTable tracks;
        const auto& a = limits.animation;
        if (!tracks.prepare(root,
                accessors,
                graph,
                {a.maxClips,
                    a.maxSamplers,
                    a.maxChannels,
                    a.maxTimeKeys,
                    a.maxOutputKeys,
                    a.maxValueScalars,
                    a.maxWorkingBytes},
                issue))
            throw Failure{issue};
        GlbSkeletalSource skeletal;
        const auto& k = limits.skeleton;
        if (!skeletal.prepare(root,
                graph,
                accessors,
                tracks,
                {k.maxJoints,
                    a.maxClips,
                    a.maxChannels,
                    k.maxKeyframes,
                    k.maxNameBytes,
                    static_cast<std::size_t>(limits.maxDecodedBytes),
                    k.matrixElementError},
                issue))
            throw Failure{issue};
        admitMaterials(root, limits);
        auto materials =
            parseImportedGlbMaterialAssets(root, chunks.bin, parseGlbBufferViewRanges(root));
        if (!materials.success)
            return failure("ASSET-ANIMATED-SOURCE-MATERIAL", materials.diagnostics.c_str());
        auto remaining = static_cast<std::size_t>(limits.maxDecodedBytes);
        charge(remaining, skeletal.decodedBytes(), 1);
        accountMaterials(materials, limits, remaining);
        AnimatedMeshSourceImportResult result;
        auto& asset = result.asset;
        std::size_t vertices = 0, indices = 0, primitives = 0;
        const auto& meshes = array(root, "meshes");
        for (const auto& node : graph.nodes()) {
            if (!node.selected || node.mesh == GlbAnimatedNodeGraph::absent)
                continue;
            for (const auto& primitive : array(meshes[node.mesh], "primitives")) {
                const auto& attributes = field(primitive, "attributes");
                const auto& positions = accessor(accessors, attributes, "POSITION", 3);
                const auto* index = accessors.find(number(primitive, "indices"));
                require(index, "ASSET-ANIMATED-SOURCE-INDICES");
                require(positions.count() <= limits.geometry.maxVertices - vertices
                        && index->count() <= limits.geometry.maxIndices - indices
                        && primitives < limits.geometry.maxPrimitives,
                    "ASSET-ANIMATED-SOURCE-GEOMETRY-BUDGET");
                vertices += positions.count();
                indices += index->count();
                ++primitives;
            }
        }
        require(vertices && indices && primitives, "ASSET-ANIMATED-SOURCE-EMPTY-SCENE");
        charge(remaining, vertices, sizeof(MeshVertex) + sizeof(SkinnedVertex));
        charge(remaining, indices, sizeof(std::uint32_t));
        charge(remaining, primitives, sizeof(Submesh));
        asset.mesh.vertices.reserve(vertices);
        asset.skinnedVertices.reserve(vertices);
        asset.mesh.indices.reserve(indices);
        asset.mesh.submeshes.reserve(primitives);
        for (std::uint32_t nodeIndex = 0; nodeIndex < graph.nodes().size(); ++nodeIndex) {
            const auto& node = graph.nodes()[nodeIndex];
            if (!node.selected || node.mesh == GlbAnimatedNodeGraph::absent)
                continue;
            for (const auto& primitive : array(meshes[node.mesh], "primitives")) {
                const auto& attributes = field(primitive, "attributes");
                const auto& positions = accessor(accessors, attributes, "POSITION", 3);
                const auto& normals = accessor(accessors, attributes, "NORMAL", 3);
                const auto& uv = accessor(accessors, attributes, "TEXCOORD_0", 2);
                const auto* tangent = attributes.find("TANGENT")
                    ? &accessor(accessors, attributes, "TANGENT", 4)
                    : nullptr;
                const auto materialIndex = number(primitive, "material");
                require(
                    materialIndex < materials.materials.size(), "ASSET-ANIMATED-SOURCE-MATERIAL");
                const auto& material = materials.materials[materialIndex];
                if (!tangent)
                    require(limits.geometry.tangentPolicy
                                == AnimatedMeshTangentPolicy::AuthoredOrMaterialUnused
                            && material.normalTexture == invalidImportedTextureIndex,
                        "ASSET-ANIMATED-SOURCE-TANGENT-REQUIRED");
                if (tangent)
                    ++result.authoredTangentPrimitives;
                else
                    ++result.unusedTangentPrimitives;
                const bool skinned = node.skin != GlbAnimatedNodeGraph::absent;
                require(
                    !skinned || attributes.find("JOINTS_0"), "ASSET-ANIMATED-SOURCE-SKIN-BINDING");
                const auto* joints =
                    skinned ? &accessor(accessors, attributes, "JOINTS_0", 4) : nullptr;
                const auto* weights =
                    skinned ? &accessor(accessors, attributes, "WEIGHTS_0", 4) : nullptr;
                const auto vertexBase = static_cast<std::uint32_t>(asset.mesh.vertices.size());
                for (std::uint32_t v = 0; v < positions.count(); ++v) {
                    MeshVertex vertex;
                    for (unsigned c = 0; c < 3; ++c)
                        require(positions.readFloat(v, c, vertex.position[c]),
                            "ASSET-ANIMATED-SOURCE-POSITION");
                    const auto normal = unitVector(normals, v, limits.geometry.unitVectorError);
                    vertex.normal[0] = normal.x;
                    vertex.normal[1] = normal.y;
                    vertex.normal[2] = normal.z;
                    for (unsigned c = 0; c < 2; ++c)
                        require(uv.readFloat(v, c, vertex.uv[c]), "ASSET-ANIMATED-SOURCE-UV");
                    if (attributes.find("COLOR_0")) {
                        const auto& color = accessor(accessors, attributes, "COLOR_0", 4);
                        for (unsigned c = 0; c < 4; ++c) {
                            float value;
                            require(color.readFloat(v, c, value) && value >= 0 && value <= 1,
                                "ASSET-ANIMATED-SOURCE-COLOR");
                        }
                    }
                    if (tangent) {
                        const auto direction =
                            unitVector(*tangent, v, limits.geometry.unitVectorError);
                        require(std::abs(core::dot(normal, direction))
                                    <= limits.geometry.orthogonalityError
                                && tangent->readFloat(v, 3, vertex.tangent[3])
                                && (vertex.tangent[3] == 1 || vertex.tangent[3] == -1),
                            "ASSET-ANIMATED-SOURCE-FRAME");
                        vertex.tangent[0] = direction.x;
                        vertex.tangent[1] = direction.y;
                        vertex.tangent[2] = direction.z;
                    }
                    SkinnedVertex influence;
                    if (skinned) {
                        double sum = 0;
                        for (unsigned lane = 0; lane < 4; ++lane) {
                            std::uint32_t slot;
                            require(joints->readUnsigned(v, lane, slot)
                                    && weights->readFloat(v, lane, influence.weights[lane])
                                    && influence.weights[lane] >= 0 && influence.weights[lane] <= 1,
                                "ASSET-ANIMATED-SOURCE-INFLUENCE");
                            const auto mapped = skeletal.paletteJoint(node.skin, slot);
                            require(mapped != GlbSkeletalSource::absent && mapped <= UINT16_MAX,
                                "ASSET-ANIMATED-SOURCE-JOINT");
                            influence.joints[lane] = static_cast<std::uint16_t>(mapped);
                            sum += influence.weights[lane];
                        }
                        require(sum > 0 && std::isfinite(sum)
                                && std::abs(sum - 1) <= limits.geometry.weightSumError,
                            "ASSET-ANIMATED-SOURCE-WEIGHT-SUM");
                        for (auto& weight : influence.weights)
                            weight = static_cast<float>(weight / sum);
                    } else {
                        // A node without skin is rigid even when another
                        // instance of the same mesh consumes its joint data.
                        const auto rigid = skeletal.nodeJoint(nodeIndex);
                        require(rigid != GlbSkeletalSource::absent && rigid <= UINT16_MAX,
                            "ASSET-ANIMATED-SOURCE-JOINT");
                        influence.joints.fill(static_cast<std::uint16_t>(rigid));
                        influence.weights = {1, 0, 0, 0};
                    }
                    asset.mesh.vertices.push_back(vertex);
                    asset.skinnedVertices.push_back(influence);
                }
                const auto* index = accessors.find(number(primitive, "indices"));
                const auto firstIndex = static_cast<std::uint32_t>(asset.mesh.indices.size());
                for (std::uint32_t i = 0; i < index->count(); ++i) {
                    std::uint32_t value;
                    require(index->readUnsigned(i, 0, value) && value < positions.count(),
                        "ASSET-ANIMATED-SOURCE-INDICES");
                    asset.mesh.indices.push_back(vertexBase + value);
                }
                asset.mesh.submeshes.push_back({firstIndex, index->count(), materialIndex});
            }
        }
        skeletal.moveRuntimeData(asset.skeleton, asset.bindPose, asset.clips);
        asset.materials = std::move(materials.materials);
        asset.textures = std::move(materials.textures);
        asset.images = std::move(materials.images);
        convertAnimatedMeshRightHandedToLeftHanded(asset);
        require(asset.mesh.validForIndexedDraw() && asset.mesh.recomputeBounds(),
            "ASSET-ANIMATED-SOURCE-MESH");
        result.decodedBytes = limits.maxDecodedBytes - remaining;
        result.success = true;
        return result;
    }
};
} // namespace

const char* validateAnimatedMeshSourceImportLimits(const Limits& limits) noexcept
{
    if (!limits.maxSourceBytes || limits.maxSourceBytes > UINT32_MAX
        || !finiteSize(limits.maxImportBytes) || !finiteSize(limits.maxDecodedBytes)
        || !limits.maxJsonDepth || limits.maxJsonDepth > 64 || !limits.maxJsonValues
        || limits.selectedScene == UINT32_MAX || limits.selectedScene >= limits.scene.maxScenes)
        return "ASSET-ANIMATED-SOURCE-LIMITS";
    for (auto value : {limits.layout.maxBufferViews,
             limits.layout.maxAccessors,
             limits.scene.maxNodes,
             limits.scene.maxEdges,
             limits.scene.maxScenes,
             limits.scene.maxSceneRoots,
             limits.scene.maxSkins,
             limits.scene.maxSkinJoints,
             limits.scene.maxMeshInstances,
             limits.animation.maxClips,
             limits.animation.maxSamplers,
             limits.animation.maxChannels,
             limits.animation.maxTimeKeys,
             limits.animation.maxOutputKeys,
             limits.animation.maxValueScalars,
             limits.skeleton.maxJoints,
             limits.skeleton.maxKeyframes,
             limits.geometry.maxVertices,
             limits.geometry.maxIndices,
             limits.geometry.maxPrimitives,
             limits.materials.maxMaterials,
             limits.materials.maxTextures,
             limits.materials.maxImages})
        if (!value || value == UINT32_MAX)
            return "ASSET-ANIMATED-SOURCE-LIMITS";
    for (auto value : {limits.layout.maxDescriptorBytes,
             limits.scene.maxWorkingBytes,
             limits.animation.maxWorkingBytes,
             limits.skeleton.maxNameBytes,
             limits.materials.maxMetadataBytes,
             limits.materials.maxEncodedImageBytes})
        if (!finiteSize(value))
            return "ASSET-ANIMATED-SOURCE-LIMITS";
    if (!limits.layout.maxElements || limits.layout.maxElements == UINT64_MAX
        || !limits.layout.maxScalars || limits.layout.maxScalars == UINT64_MAX
        || limits.skeleton.maxJoints > std::uint32_t(UINT16_MAX) + 1
        || !std::isfinite(limits.skeleton.matrixElementError)
        || limits.skeleton.matrixElementError < 0)
        return "ASSET-ANIMATED-SOURCE-LIMITS";
    for (auto error : {limits.geometry.unitVectorError,
             limits.geometry.orthogonalityError,
             limits.geometry.weightSumError})
        if (!std::isfinite(error) || error < 0 || error >= 1)
            return "ASSET-ANIMATED-SOURCE-LIMITS";
    if ((limits.geometry.colorPolicy != AnimatedMeshColorPolicy::RejectColor0
            && limits.geometry.colorPolicy != AnimatedMeshColorPolicy::DiscardValidatedRgba)
        || (limits.geometry.tangentPolicy != AnimatedMeshTangentPolicy::RequireAuthored
            && limits.geometry.tangentPolicy
                != AnimatedMeshTangentPolicy::AuthoredOrMaterialUnused))
        return "ASSET-ANIMATED-SOURCE-LIMITS";
    return nullptr;
}

AnimatedMeshSourceImportResult importAnimatedMeshFromGlbBytes(
    core::ArrayView<const std::uint8_t> bytes, const Limits& limits)
{
    if (const auto* issue = validateAnimatedMeshSourceImportLimits(limits))
        return failure(issue);
    if (!bytes.data() || bytes.empty() || bytes.size() > limits.maxSourceBytes)
        return failure("ASSET-ANIMATED-SOURCE-BYTES");
    try {
        auto quota = core::currentCpuMemoryQuota();
        std::optional<core::CpuMemoryQuotaScope> scope;
        if (quota) {
            if (!quota->limitBytes() || quota->limitBytes() > limits.maxImportBytes)
                return failure("ASSET-ANIMATED-SOURCE-QUOTA-LIMIT");
        } else {
            quota = core::makeShared<core::CpuMemoryQuota>(limits.maxImportBytes);
            scope.emplace(quota);
        }
        auto result = AnimatedSourceDecoder::decode(bytes, limits);
        result.importQuotaPeakBytes = quota->peakBytes();
        return result;
    } catch (const std::bad_alloc&) {
        return failure("ASSET-ANIMATED-SOURCE-IMPORT-BUDGET");
    } catch (const std::exception& error) {
        return failure("ASSET-ANIMATED-SOURCE-INVALID", error.what());
    }
}
} // namespace arp::assets
