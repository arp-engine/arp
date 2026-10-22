#include "assets/fbx_character_importer.h"

#include "assets/animated_mesh_coordinate_conversion.h"
#include "assets/tangent_generation.h"
#include "core/memory/cpu_allocator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifndef ARP_HAS_ASSIMP
#define ARP_HAS_ASSIMP 0
#endif

#if ARP_HAS_ASSIMP
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#endif

namespace arp::assets {

namespace {

AnimatedMeshImportResult fail(const std::pmr::string& diagnostics)
{
    AnimatedMeshImportResult result;
    result.diagnostics = diagnostics;
    return result;
}

#if ARP_HAS_ASSIMP

std::pmr::string aiName(const aiString& value)
{
    return std::pmr::string(value.C_Str());
}

std::pmr::string mimeTypeFromExtension(const std::filesystem::path& path)
{
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (extension == ".png")
        return "image/png";
    if (extension == ".jpg" || extension == ".jpeg")
        return "image/jpeg";
    if (extension == ".bmp")
        return "image/bmp";
    if (extension == ".tga")
        return "image/x-targa";
    return {};
}

std::pmr::string mimeTypeFromAssimpHint(const aiTexture& texture)
{
    std::string hint(texture.achFormatHint, texture.achFormatHint + sizeof(texture.achFormatHint));
    hint.erase(std::find(hint.begin(), hint.end(), '\0'), hint.end());
    std::transform(hint.begin(), hint.end(), hint.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (hint == "png")
        return "image/png";
    if (hint == "jpg" || hint == "jpeg")
        return "image/jpeg";
    if (hint == "bmp")
        return "image/bmp";
    if (hint == "tga")
        return "image/x-targa";
    return {};
}

bool readFileBytes(const std::filesystem::path& path, std::pmr::vector<std::byte>& outBytes)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
        return false;
    }
    stream.seekg(0, std::ios::end);
    const auto size = stream.tellg();
    stream.seekg(0, std::ios::beg);
    if (size <= 0) {
        return false;
    }

    outBytes.resize(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char*>(outBytes.data()), size);
    return stream.good();
}

std::pmr::vector<unsigned char> toUnsignedBytes(const std::pmr::vector<std::byte>& bytes)
{
    std::pmr::vector<unsigned char> result;
    result.reserve(bytes.size());
    for (const auto value : bytes) {
        result.push_back(static_cast<unsigned char>(value));
    }
    return result;
}

uint32_t appendImageTexturePair(AnimatedMeshAsset& asset,
    const std::pmr::string& textureName,
    const std::pmr::string& imageUri,
    const std::pmr::string& mimeType,
    std::pmr::vector<std::byte>&& encodedBytes)
{
    ImportedTextureAsset texture;
    texture.name = textureName;
    texture.sourceImage = static_cast<uint32_t>(asset.images.size());

    ImportedImageAsset image;
    image.name = textureName;
    image.uri = imageUri;
    image.mimeType = mimeType;
    image.encodedBytes = toUnsignedBytes(encodedBytes);

    const auto textureIndex = static_cast<uint32_t>(asset.textures.size());
    asset.images.push_back(std::move(image));
    asset.textures.push_back(std::move(texture));
    return textureIndex;
}

uint32_t importTextureSlot(const aiScene& scene,
    const std::filesystem::path& sourcePath,
    AnimatedMeshAsset& asset,
    const aiMaterial& material,
    const aiTextureType textureType)
{
    aiString texturePath;
    if (material.GetTexture(textureType, 0, &texturePath) != AI_SUCCESS) {
        return invalidImportedTextureIndex;
    }

    const auto textureName = aiName(texturePath);
    if (const auto* embedded = scene.GetEmbeddedTexture(texturePath.C_Str())) {
        if (embedded->mHeight == 0 && embedded->pcData != nullptr && embedded->mWidth > 0) {
            std::pmr::vector<std::byte> encodedBytes;
            encodedBytes.resize(static_cast<std::size_t>(embedded->mWidth));
            std::memcpy(
                encodedBytes.data(), embedded->pcData, static_cast<std::size_t>(embedded->mWidth));
            return appendImageTexturePair(asset,
                textureName,
                textureName,
                mimeTypeFromAssimpHint(*embedded),
                std::move(encodedBytes));
        }
        return invalidImportedTextureIndex;
    }

    auto resolvedPath = sourcePath.parent_path() / texturePath.C_Str();
    if (!std::filesystem::exists(resolvedPath)) {
        resolvedPath = texturePath.C_Str();
    }

    std::pmr::vector<std::byte> encodedBytes;
    if (!readFileBytes(resolvedPath, encodedBytes)) {
        return invalidImportedTextureIndex;
    }
    return appendImageTexturePair(asset,
        textureName,
        arp::core::toPmrString(resolvedPath.string()),
        mimeTypeFromExtension(resolvedPath),
        std::move(encodedBytes));
}

animation::Mat4 aiToMat4(const aiMatrix4x4& value)
{
    animation::Mat4 result;
    result.m = {
        value.a1,
        value.b1,
        value.c1,
        value.d1,
        value.a2,
        value.b2,
        value.c2,
        value.d2,
        value.a3,
        value.b3,
        value.c3,
        value.d3,
        value.a4,
        value.b4,
        value.c4,
        value.d4,
    };
    return result;
}

animation::LocalTransform aiToLocalTransform(const aiMatrix4x4& value)
{
    aiVector3D scale;
    aiQuaternion rotation;
    aiVector3D translation;
    value.Decompose(scale, rotation, translation);
    return animation::LocalTransform{
        {translation.x, translation.y, translation.z},
        {rotation.x, rotation.y, rotation.z, rotation.w},
        {scale.x, scale.y, scale.z},
    };
}

void addWeight(SkinnedVertex& vertex, const uint16_t jointIndex, const float weight)
{
    std::size_t slot = 0;
    for (std::size_t index = 1; index < vertex.weights.size(); ++index) {
        if (vertex.weights[index] < vertex.weights[slot]) {
            slot = index;
        }
    }
    if (weight > vertex.weights[slot]) {
        vertex.joints[slot] = jointIndex;
        vertex.weights[slot] = weight;
    }
}

void normalizeWeights(SkinnedVertex& vertex)
{
    float sum = 0.0f;
    for (const auto weight : vertex.weights) {
        sum += weight;
    }
    if (sum <= 0.000001f) {
        vertex.joints = {0, 0, 0, 0};
        vertex.weights = {1.0f, 0.0f, 0.0f, 0.0f};
        return;
    }
    for (auto& weight : vertex.weights) {
        weight /= sum;
    }
}

float weightSum(const SkinnedVertex& vertex)
{
    float sum = 0.0f;
    for (const auto weight : vertex.weights) {
        sum += weight;
    }
    return sum;
}

bool copyNeighborWeights(SkinnedVertex& target, const SkinnedVertex& neighbor)
{
    if (weightSum(target) > 0.000001f || weightSum(neighbor) <= 0.000001f) {
        return false;
    }
    for (std::size_t slot = 0; slot < neighbor.weights.size(); ++slot) {
        addWeight(target, neighbor.joints[slot], neighbor.weights[slot]);
    }
    return weightSum(target) > 0.000001f;
}

void propagateMissingWeightsFromFaces(
    const aiMesh& mesh, const uint32_t firstVertex, std::pmr::vector<SkinnedVertex>& vertices)
{
    for (unsigned int pass = 0; pass < mesh.mNumVertices; ++pass) {
        bool changed = false;
        for (unsigned int faceIndex = 0; faceIndex < mesh.mNumFaces; ++faceIndex) {
            const auto& face = mesh.mFaces[faceIndex];
            if (face.mNumIndices != 3) {
                continue;
            }
            for (unsigned int corner = 0; corner < 3; ++corner) {
                const auto vertexIndex = firstVertex + face.mIndices[corner];
                if (vertexIndex >= vertices.size()
                    || weightSum(vertices[vertexIndex]) > 0.000001f) {
                    continue;
                }
                for (unsigned int neighborCorner = 0; neighborCorner < 3; ++neighborCorner) {
                    if (neighborCorner == corner) {
                        continue;
                    }
                    const auto neighborIndex = firstVertex + face.mIndices[neighborCorner];
                    if (neighborIndex < vertices.size()
                        && copyNeighborWeights(vertices[vertexIndex], vertices[neighborIndex])) {
                        changed = true;
                        break;
                    }
                }
            }
        }
        if (!changed) {
            break;
        }
    }
}

aiVector3D subtract(const aiVector3D& lhs, const aiVector3D& rhs)
{
    return aiVector3D(lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z);
}

aiVector3D add(const aiVector3D& lhs, const aiVector3D& rhs)
{
    return aiVector3D(lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z);
}

aiVector3D cross(const aiVector3D& lhs, const aiVector3D& rhs)
{
    return aiVector3D(lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x);
}

float dot(const aiVector3D& lhs, const aiVector3D& rhs)
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

aiVector3D transformDirection(const aiMatrix4x4& matrix, const aiVector3D& value)
{
    return aiVector3D(matrix.a1 * value.x + matrix.a2 * value.y + matrix.a3 * value.z,
        matrix.b1 * value.x + matrix.b2 * value.y + matrix.b3 * value.z,
        matrix.c1 * value.x + matrix.c2 * value.y + matrix.c3 * value.z);
}

bool faceWindingOpposesVertexNormals(
    const aiMesh& mesh, const aiFace& face, const aiMatrix4x4& meshTransform)
{
    if (!mesh.HasNormals() || face.mNumIndices != 3) {
        return false;
    }

    const auto first = face.mIndices[0];
    const auto second = face.mIndices[1];
    const auto third = face.mIndices[2];
    const auto firstPosition = meshTransform * mesh.mVertices[first];
    const auto secondPosition = meshTransform * mesh.mVertices[second];
    const auto thirdPosition = meshTransform * mesh.mVertices[third];
    const auto faceNormal =
        cross(subtract(secondPosition, firstPosition), subtract(thirdPosition, firstPosition));
    const auto normalSum = add(add(transformDirection(meshTransform, mesh.mNormals[first]),
                                   transformDirection(meshTransform, mesh.mNormals[second])),
        transformDirection(meshTransform, mesh.mNormals[third]));
    if (dot(faceNormal, faceNormal) <= 0.000001f || dot(normalSum, normalSum) <= 0.000001f) {
        return false;
    }
    return dot(faceNormal, normalSum) < 0.0f;
}

aiVector3D normalizeOrDefault(const aiVector3D& value, const aiVector3D& fallback)
{
    const auto length = std::sqrt(dot(value, value));
    if (length <= 0.000001f) {
        return fallback;
    }
    return aiVector3D(value.x / length, value.y / length, value.z / length);
}

bool findMeshGlobalTransform(const aiNode* Node,
    const unsigned int meshIndex,
    const aiMatrix4x4& parentTransform,
    aiMatrix4x4& outTransform)
{
    if (Node == nullptr) {
        return false;
    }

    const auto globalTransform = parentTransform * Node->mTransformation;
    for (unsigned int index = 0; index < Node->mNumMeshes; ++index) {
        if (Node->mMeshes[index] == meshIndex) {
            outTransform = globalTransform;
            return true;
        }
    }
    for (unsigned int child = 0; child < Node->mNumChildren; ++child) {
        if (findMeshGlobalTransform(
                Node->mChildren[child], meshIndex, globalTransform, outTransform)) {
            return true;
        }
    }
    return false;
}

bool nameStartsWith(const std::pmr::string& value, const char* prefix)
{
    const std::pmr::string prefixText(prefix);
    return value.size() >= prefixText.size()
        && value.compare(0, prefixText.size(), prefixText) == 0;
}

bool subtreeContainsPrimaryAvatarBone(
    const aiNode* Node, const std::pmr::unordered_set<std::pmr::string>& boneNames)
{
    if (Node == nullptr) {
        return false;
    }
    const auto name = aiName(Node->mName);
    if (boneNames.find(name) != boneNames.end()
        && (name == "Bip001" || nameStartsWith(name, "Bip001 "))) {
        return true;
    }
    for (unsigned int child = 0; child < Node->mNumChildren; ++child) {
        if (subtreeContainsPrimaryAvatarBone(Node->mChildren[child], boneNames)) {
            return true;
        }
    }
    return false;
}

void collectBoneOffsets(const aiScene& scene,
    std::pmr::unordered_map<std::pmr::string, animation::Mat4>& offsets,
    std::pmr::unordered_set<std::pmr::string>& boneNames)
{
    for (unsigned int meshIndex = 0; meshIndex < scene.mNumMeshes; ++meshIndex) {
        const auto* mesh = scene.mMeshes[meshIndex];
        if (mesh == nullptr) {
            continue;
        }
        for (unsigned int boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex) {
            const auto* bone = mesh->mBones[boneIndex];
            if (bone == nullptr) {
                continue;
            }
            const auto name = aiName(bone->mName);
            boneNames.insert(name);
            offsets[name] = aiToMat4(bone->mOffsetMatrix);
        }
    }
}

void collectAnimatedNodeNames(
    const aiScene& scene, std::pmr::unordered_set<std::pmr::string>& nodeNames)
{
    for (unsigned int animationIndex = 0; animationIndex < scene.mNumAnimations; ++animationIndex) {
        const auto* animation = scene.mAnimations[animationIndex];
        if (animation == nullptr) {
            continue;
        }
        for (unsigned int channelIndex = 0; channelIndex < animation->mNumChannels;
            ++channelIndex) {
            const auto* channel = animation->mChannels[channelIndex];
            if (channel == nullptr) {
                continue;
            }
            const auto name = aiName(channel->mNodeName);
            if (!name.empty()) {
                nodeNames.insert(name);
            }
        }
    }
}

bool invertMat4(const animation::Mat4& matrix, animation::Mat4& inverse)
{
    const auto& m = matrix.m;
    auto& inv = inverse.m;

    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14]
        + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15]
        - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13]
        + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14]
        - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15]
        - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14]
        + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13]
        - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13]
        + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14]
        + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14]
        - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13]
        + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13]
        - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10]
        - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10]
        + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9]
        - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9]
        + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

    const auto determinant = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::fabs(determinant) <= 0.0000001f) {
        return false;
    }

    const auto invDeterminant = 1.0f / determinant;
    for (auto& value : inv) {
        value *= invDeterminant;
    }
    return true;
}

bool rebuildInverseBindMatricesFromBindPose(AnimatedMeshAsset& asset)
{
    const auto modelPose = animation::buildModelPose(asset.skeleton, asset.bindPose);
    if (modelPose.size() != asset.skeleton.joints.size()) {
        return false;
    }
    for (std::size_t index = 0; index < asset.skeleton.joints.size(); ++index) {
        animation::Mat4 inverse;
        if (!invertMat4(modelPose[index], inverse)) {
            return false;
        }
        asset.skeleton.joints[index].inverseBindMatrix = inverse;
    }
    return true;
}

const aiNode* findNodeByName(const aiNode* Node, const std::pmr::string& name)
{
    if (Node == nullptr) {
        return nullptr;
    }
    if (aiName(Node->mName) == name) {
        return Node;
    }
    for (unsigned int child = 0; child < Node->mNumChildren; ++child) {
        if (const auto* found = findNodeByName(Node->mChildren[child], name)) {
            return found;
        }
    }
    return nullptr;
}

bool subtreeContainsBone(
    const aiNode* Node, const std::pmr::unordered_set<std::pmr::string>& boneNames)
{
    if (Node == nullptr) {
        return false;
    }
    if (boneNames.find(aiName(Node->mName)) != boneNames.end()) {
        return true;
    }
    for (unsigned int child = 0; child < Node->mNumChildren; ++child) {
        if (subtreeContainsBone(Node->mChildren[child], boneNames)) {
            return true;
        }
    }
    return false;
}

void appendSkeletonJoints(const aiNode* Node,
    const std::pmr::unordered_set<std::pmr::string>& boneNames,
    const std::pmr::unordered_map<std::pmr::string, animation::Mat4>& offsets,
    AnimatedMeshAsset& asset,
    std::pmr::unordered_map<std::pmr::string, uint32_t>& jointByName,
    const int parentJoint)
{
    if (Node == nullptr) {
        return;
    }
    if (!subtreeContainsBone(Node, boneNames)) {
        return;
    }

    const auto name = aiName(Node->mName);
    const auto jointIndex = static_cast<uint32_t>(asset.skeleton.joints.size());
    jointByName[name] = jointIndex;
    const auto offset = offsets.find(name);
    asset.skeleton.joints.push_back(animation::Joint{
        name,
        parentJoint,
        offset == offsets.end() ? animation::Mat4::identity() : offset->second,
    });
    asset.bindPose.push_back(aiToLocalTransform(Node->mTransformation));
    const auto currentParent = static_cast<int>(jointIndex);

    std::pmr::vector<unsigned int> children;
    children.reserve(Node->mNumChildren);
    for (unsigned int child = 0; child < Node->mNumChildren; ++child) {
        children.push_back(child);
    }
    std::stable_sort(
        children.begin(), children.end(), [&](const unsigned int left, const unsigned int right) {
            const auto leftPrimary =
                subtreeContainsPrimaryAvatarBone(Node->mChildren[left], boneNames);
            const auto rightPrimary =
                subtreeContainsPrimaryAvatarBone(Node->mChildren[right], boneNames);
            return leftPrimary && !rightPrimary;
        });
    for (const auto child : children) {
        appendSkeletonJoints(
            Node->mChildren[child], boneNames, offsets, asset, jointByName, currentParent);
    }
}

bool buildSkeleton(const aiScene& scene,
    AnimatedMeshAsset& asset,
    std::pmr::unordered_map<std::pmr::string, uint32_t>& jointByName,
    std::pmr::string& diagnostics)
{
    std::pmr::unordered_map<std::pmr::string, animation::Mat4> offsets;
    std::pmr::unordered_set<std::pmr::string> boneNames;
    collectBoneOffsets(scene, offsets, boneNames);
    if (boneNames.empty()) {
        collectAnimatedNodeNames(scene, boneNames);
    }
    if (boneNames.empty()) {
        jointByName["root"] = 0;
        asset.skeleton.joints.push_back(animation::Joint{
            "root",
            -1,
            animation::Mat4::identity(),
        });
        asset.bindPose.push_back(animation::LocalTransform{});
        return true;
    }

    appendSkeletonJoints(scene.mRootNode, boneNames, offsets, asset, jointByName, -1);
    for (const auto& name : boneNames) {
        if (jointByName.find(name) != jointByName.end()) {
            continue;
        }
        const auto jointIndex = static_cast<uint32_t>(asset.skeleton.joints.size());
        jointByName[name] = jointIndex;
        const auto offset = offsets.find(name);
        const auto* Node = findNodeByName(scene.mRootNode, name);
        asset.skeleton.joints.push_back(animation::Joint{
            name,
            -1,
            offset == offsets.end() ? animation::Mat4::identity() : offset->second,
        });
        asset.bindPose.push_back(Node == nullptr ? animation::LocalTransform{}
                                                 : aiToLocalTransform(Node->mTransformation));
    }

    if (!asset.skeleton.valid()) {
        diagnostics = "FBX skeleton hierarchy must be parent-before-child";
        return false;
    }
    if (!rebuildInverseBindMatricesFromBindPose(asset)) {
        diagnostics = "FBX bind pose matrices must be invertible";
        return false;
    }
    return true;
}

void importMaterials(
    const aiScene& scene, const std::filesystem::path& sourcePath, AnimatedMeshAsset& asset)
{
    asset.materials.reserve(scene.mNumMaterials);
    for (unsigned int index = 0; index < scene.mNumMaterials; ++index) {
        const auto* source = scene.mMaterials[index];
        ImportedMaterialAsset material;
        aiString name;
        if (source != nullptr && source->Get(AI_MATKEY_NAME, name) == AI_SUCCESS) {
            material.name = aiName(name);
        } else {
            material.name =
                std::pmr::string("Material") + arp::core::toPmrString(std::to_string(index));
        }
        // FBX commonly carries legacy Phong material data rather than glTF
        // metallic-roughness fields. Keep dielectric defaults and translate
        // the authored shininess exponent to perceptual GGX roughness when an
        // explicit roughness factor is unavailable.
        material.metallicFactor = 0.0f;
        material.roughnessFactor = 1.0f;
        if (source != nullptr) {
            float metallic = 0.0f;
            if (source->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS
                && std::isfinite(metallic)) {
                material.metallicFactor = std::clamp(metallic, 0.0f, 1.0f);
            }
            float roughness = 1.0f;
            if (source->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS
                && std::isfinite(roughness)) {
                material.roughnessFactor = std::clamp(roughness, 0.045f, 1.0f);
            } else {
                float shininess = 0.0f;
                if (source->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS
                    && std::isfinite(shininess) && shininess > 0.0f) {
                    const float ggxAlpha = std::sqrt(2.0f / (shininess + 2.0f));
                    material.roughnessFactor = std::clamp(std::sqrt(ggxAlpha), 0.045f, 1.0f);
                }
            }
            aiColor4D diffuseColor{};
            if (aiGetMaterialColor(source, AI_MATKEY_COLOR_DIFFUSE, &diffuseColor) == AI_SUCCESS) {
                material.baseColorFactor[0] = diffuseColor.r;
                material.baseColorFactor[1] = diffuseColor.g;
                material.baseColorFactor[2] = diffuseColor.b;
                material.baseColorFactor[3] = diffuseColor.a;
            }
            aiColor4D emissiveColor{};
            if (aiGetMaterialColor(source, AI_MATKEY_COLOR_EMISSIVE, &emissiveColor)
                == AI_SUCCESS) {
                material.emissiveFactor[0] = emissiveColor.r;
                material.emissiveFactor[1] = emissiveColor.g;
                material.emissiveFactor[2] = emissiveColor.b;
            }
            float opacity = 1.0f;
            if (aiGetMaterialFloat(source, AI_MATKEY_OPACITY, &opacity) == AI_SUCCESS) {
                material.baseColorFactor[3] = opacity;
            }

            material.baseColorTexture =
                importTextureSlot(scene, sourcePath, asset, *source, aiTextureType_BASE_COLOR);
            if (material.baseColorTexture == invalidImportedTextureIndex) {
                material.baseColorTexture =
                    importTextureSlot(scene, sourcePath, asset, *source, aiTextureType_DIFFUSE);
            }
            material.normalTexture =
                importTextureSlot(scene, sourcePath, asset, *source, aiTextureType_NORMALS);
        }
        asset.materials.push_back(std::move(material));
    }
}

bool importMeshes(const aiScene& scene,
    const std::pmr::unordered_map<std::pmr::string, uint32_t>& jointByName,
    AnimatedMeshAsset& asset,
    std::pmr::string& diagnostics,
    const bool flipUvY)
{
    for (unsigned int meshIndex = 0; meshIndex < scene.mNumMeshes; ++meshIndex) {
        const auto* source = scene.mMeshes[meshIndex];
        if (source == nullptr || source->mNumVertices == 0 || source->mNumFaces == 0) {
            continue;
        }

        aiMatrix4x4 meshTransform;
        findMeshGlobalTransform(scene.mRootNode, meshIndex, aiMatrix4x4(), meshTransform);

        const auto firstVertex = static_cast<uint32_t>(asset.mesh.vertices.size());
        const auto firstIndex = static_cast<uint32_t>(asset.mesh.indices.size());
        bool hasValidImportedTangentFrame = source->HasTangentsAndBitangents();
        for (unsigned int vertexIndex = 0; vertexIndex < source->mNumVertices; ++vertexIndex) {
            MeshVertex vertex;
            const auto position = meshTransform * source->mVertices[vertexIndex];
            vertex.position[0] = position.x;
            vertex.position[1] = position.y;
            vertex.position[2] = position.z;
            if (source->HasNormals()) {
                const auto normal = normalizeOrDefault(
                    transformDirection(meshTransform, source->mNormals[vertexIndex]),
                    source->mNormals[vertexIndex]);
                vertex.normal[0] = normal.x;
                vertex.normal[1] = normal.y;
                vertex.normal[2] = normal.z;
            }
            if (source->HasTextureCoords(0)) {
                vertex.uv[0] = source->mTextureCoords[0][vertexIndex].x;
                const auto sourceUvY = source->mTextureCoords[0][vertexIndex].y;
                vertex.uv[1] = flipUvY ? 1.0f - sourceUvY : sourceUvY;
            }
            if (source->HasTangentsAndBitangents()) {
                const auto tangent =
                    transformDirection(meshTransform, source->mTangents[vertexIndex]);
                const auto bitangent =
                    transformDirection(meshTransform, source->mBitangents[vertexIndex]);
                hasValidImportedTangentFrame = applyImportedTangentFrame(vertex,
                                                   {tangent.x, tangent.y, tangent.z},
                                                   {bitangent.x, bitangent.y, bitangent.z})
                    && hasValidImportedTangentFrame;
            }
            asset.mesh.vertices.push_back(vertex);
            asset.skinnedVertices.push_back(SkinnedVertex{});
        }

        for (unsigned int boneIndex = 0; boneIndex < source->mNumBones; ++boneIndex) {
            const auto* bone = source->mBones[boneIndex];
            const auto jointFound = jointByName.find(aiName(bone->mName));
            if (jointFound == jointByName.end()) {
                continue;
            }
            for (unsigned int weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex) {
                const auto& weight = bone->mWeights[weightIndex];
                const auto vertexIndex = firstVertex + weight.mVertexId;
                if (vertexIndex < asset.skinnedVertices.size()) {
                    addWeight(asset.skinnedVertices[vertexIndex],
                        static_cast<uint16_t>(jointFound->second),
                        weight.mWeight);
                }
            }
        }

        propagateMissingWeightsFromFaces(*source, firstVertex, asset.skinnedVertices);
        for (unsigned int vertexIndex = firstVertex; vertexIndex < asset.skinnedVertices.size();
            ++vertexIndex) {
            normalizeWeights(asset.skinnedVertices[vertexIndex]);
        }

        for (unsigned int faceIndex = 0; faceIndex < source->mNumFaces; ++faceIndex) {
            const auto& face = source->mFaces[faceIndex];
            if (face.mNumIndices != 3) {
                diagnostics = "FBX importer expects triangulated faces";
                return false;
            }
            if (faceWindingOpposesVertexNormals(*source, face, meshTransform)) {
                asset.mesh.indices.push_back(firstVertex + face.mIndices[0]);
                asset.mesh.indices.push_back(firstVertex + face.mIndices[2]);
                asset.mesh.indices.push_back(firstVertex + face.mIndices[1]);
            } else {
                asset.mesh.indices.push_back(firstVertex + face.mIndices[0]);
                asset.mesh.indices.push_back(firstVertex + face.mIndices[1]);
                asset.mesh.indices.push_back(firstVertex + face.mIndices[2]);
            }
        }
        asset.mesh.submeshes.push_back(Submesh{
            firstIndex,
            static_cast<uint32_t>(asset.mesh.indices.size()) - firstIndex,
            source->mMaterialIndex,
        });
        if (!hasValidImportedTangentFrame
            && !generateTangentsForIndexedMeshRange(asset.mesh,
                IndexedMeshTangentRange{
                    firstVertex,
                    source->mNumVertices,
                    firstIndex,
                    static_cast<uint32_t>(asset.mesh.indices.size()) - firstIndex,
                },
                flipUvY)) {
            diagnostics = "FBX mesh tangent fallback generation failed";
            return false;
        }
    }

    if (!asset.mesh.validForIndexedDraw()) {
        diagnostics = "FBX mesh is not valid for indexed draw";
        return false;
    }
    return true;
}

void importAnimations(const aiScene& scene,
    const std::pmr::unordered_map<std::pmr::string, uint32_t>& jointByName,
    AnimatedMeshAsset& asset,
    const bool keepUnmappedChannels)
{
    for (unsigned int animationIndex = 0; animationIndex < scene.mNumAnimations; ++animationIndex) {
        const auto* source = scene.mAnimations[animationIndex];
        if (source == nullptr) {
            continue;
        }
        const auto rawTicksPerSecond =
            source->mTicksPerSecond > 0.0 ? source->mTicksPerSecond : 30.0;
        const auto ticksPerSecond =
            rawTicksPerSecond <= 1.0 && source->mDuration > 30.0 ? 30.0 : rawTicksPerSecond;
        animation::AnimationClip clip;
        if (source->mName.length > 0) {
            clip.name = aiName(source->mName);
        } else {
            clip.name = std::pmr::string("Animation")
                + arp::core::toPmrString(std::to_string(animationIndex));
        }
        clip.durationSeconds = static_cast<float>(source->mDuration / ticksPerSecond);

        for (unsigned int channelIndex = 0; channelIndex < source->mNumChannels; ++channelIndex) {
            const auto* channelSource = source->mChannels[channelIndex];
            if (channelSource == nullptr) {
                continue;
            }
            const auto jointName = aiName(channelSource->mNodeName);
            const auto jointFound = jointByName.find(jointName);
            if (jointFound == jointByName.end() && !keepUnmappedChannels) {
                continue;
            }
            const auto jointIndex = jointFound == jointByName.end() ? 0u : jointFound->second;

            if (channelSource->mNumPositionKeys > 0) {
                animation::AnimationChannel channel;
                channel.jointIndex = jointIndex;
                channel.targetPath = "translation";
                channel.jointName = jointName;
                channel.keyframes.reserve(channelSource->mNumPositionKeys);
                for (unsigned int keyIndex = 0; keyIndex < channelSource->mNumPositionKeys;
                    ++keyIndex) {
                    const auto& key = channelSource->mPositionKeys[keyIndex];
                    channel.keyframes.push_back(animation::JointKeyframe{
                        static_cast<float>(key.mTime / ticksPerSecond),
                        {{key.mValue.x, key.mValue.y, key.mValue.z}},
                    });
                }
                if (!channel.keyframes.empty()) {
                    clip.channels.push_back(std::move(channel));
                }
            }

            if (channelSource->mNumRotationKeys > 0) {
                animation::AnimationChannel channel;
                channel.jointIndex = jointIndex;
                channel.targetPath = "rotation";
                channel.jointName = jointName;
                channel.keyframes.reserve(channelSource->mNumRotationKeys);
                for (unsigned int keyIndex = 0; keyIndex < channelSource->mNumRotationKeys;
                    ++keyIndex) {
                    const auto& key = channelSource->mRotationKeys[keyIndex];
                    animation::JointTransform transform;
                    transform.rotation = {key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w};
                    channel.keyframes.push_back(animation::JointKeyframe{
                        static_cast<float>(key.mTime / ticksPerSecond),
                        transform,
                    });
                }
                if (!channel.keyframes.empty()) {
                    clip.channels.push_back(std::move(channel));
                }
            }

            if (channelSource->mNumScalingKeys > 0) {
                animation::AnimationChannel channel;
                channel.jointIndex = jointIndex;
                channel.targetPath = "scale";
                channel.jointName = jointName;
                channel.keyframes.reserve(channelSource->mNumScalingKeys);
                for (unsigned int keyIndex = 0; keyIndex < channelSource->mNumScalingKeys;
                    ++keyIndex) {
                    const auto& key = channelSource->mScalingKeys[keyIndex];
                    animation::JointTransform transform;
                    transform.scale = {key.mValue.x, key.mValue.y, key.mValue.z};
                    channel.keyframes.push_back(animation::JointKeyframe{
                        static_cast<float>(key.mTime / ticksPerSecond),
                        transform,
                    });
                }
                if (!channel.keyframes.empty()) {
                    clip.channels.push_back(std::move(channel));
                }
            }
        }

        if (clip.durationSeconds > 0.0f && !clip.channels.empty()) {
            asset.clips.push_back(std::move(clip));
        }
    }
}

#endif

} // namespace

bool FbxCharacterImporter::assimpAvailable() const
{
    return ARP_HAS_ASSIMP != 0;
}

AnimatedMeshImportResult FbxCharacterImporter::importFile(
    const std::filesystem::path& path, const FbxCharacterImportOptions& options) const
{
#if !ARP_HAS_ASSIMP
    (void)path;
    (void)options;
    return fail(
        "Assimp is required for FBX character import but was not available at configure time");
#else
    Assimp::Importer importer;
    const auto* scene = importer.ReadFile(path.string(),
        aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_GenSmoothNormals
            | aiProcess_LimitBoneWeights | aiProcess_ImproveCacheLocality);
    if (scene == nullptr) {
        std::pmr::string diagnostics("Assimp failed to read FBX '");
        diagnostics += path.string();
        diagnostics += "': ";
        diagnostics += importer.GetErrorString();
        return fail(diagnostics);
    }
    if (scene->mRootNode == nullptr) {
        return fail("Assimp returned an incomplete FBX scene");
    }

    AnimatedMeshAsset asset;
    std::pmr::unordered_map<std::pmr::string, uint32_t> jointByName;
    std::pmr::string diagnostics;
    if (!buildSkeleton(*scene, asset, jointByName, diagnostics)) {
        return fail(diagnostics);
    }

    if (options.importMesh) {
        importMaterials(*scene, path, asset);
        if (!importMeshes(*scene, jointByName, asset, diagnostics, options.flipUvY)) {
            return fail(diagnostics);
        }
    }
    if (options.importAnimations) {
        importAnimations(*scene, jointByName, asset, !options.importMesh);
    }
    if (options.convertToLeftHanded) {
        convertAnimatedMeshRightHandedToLeftHanded(asset);
    }

    AnimatedMeshImportResult result;
    result.success = options.importMesh ? asset.mesh.validForIndexedDraw() && asset.skeleton.valid()
                                        : !asset.clips.empty();
    if (!result.success) {
        result.diagnostics = options.importMesh
            ? "FBX import did not produce a drawable skinned mesh"
            : "FBX import did not produce animation clips";
        return result;
    }
    result.asset = std::move(asset);
    return result;
#endif
}

} // namespace arp::assets
