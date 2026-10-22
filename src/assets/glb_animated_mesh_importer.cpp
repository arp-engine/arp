#include "assets/glb_animated_mesh_importer.h"

#include "assets/animated_mesh_coordinate_conversion.h"
#include "assets/glb_importer_common.h"
#include "assets/tangent_generation.h"
#include "core/memory/cpu_allocator.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <unordered_map>

namespace arp::assets {

namespace {

constexpr uint32_t invalidIndex = std::numeric_limits<uint32_t>::max();

using detail::GlbChunks;
using detail::JsonValue;

struct BufferViewDesc {
    uint32_t byteOffset = 0;
    uint32_t byteLength = 0;
    uint32_t byteStride = 0;
};

struct AccessorDesc {
    uint32_t bufferView = invalidIndex;
    uint32_t byteOffset = 0;
    uint32_t componentType = 0;
    uint32_t count = 0;
    std::pmr::string type;
    bool normalized = false;
};

struct SkinDesc {
    std::pmr::vector<uint32_t> joints;
    uint32_t inverseBindMatricesAccessor = invalidIndex;
};

struct NodeDesc {
    std::pmr::string name;
    int parentIndex = -1;
    std::pmr::vector<uint32_t> children;
    animation::LocalTransform localTransform;
};

struct AnimationSamplerDesc {
    uint32_t inputAccessor = invalidIndex;
    uint32_t outputAccessor = invalidIndex;
};

AnimatedMeshImportResult fail(const std::pmr::string& diagnostics)
{
    AnimatedMeshImportResult result;
    result.diagnostics = diagnostics;
    return result;
}

uint32_t jsonUInt(const JsonValue* value, const uint32_t fallback)
{
    if (value == nullptr || value->type != JsonValue::Type::Number || value->numberValue < 0.0) {
        return fallback;
    }
    return static_cast<uint32_t>(value->numberValue);
}

bool jsonBool(const JsonValue* value, const bool fallback)
{
    return value != nullptr && value->type == JsonValue::Type::Bool ? value->boolValue : fallback;
}

std::pmr::string jsonString(const JsonValue* value, const std::pmr::string& fallback = {})
{
    return value != nullptr && value->type == JsonValue::Type::String ? value->stringValue
                                                                      : fallback;
}

std::pmr::vector<uint32_t> jsonUIntArray(const JsonValue* value)
{
    std::pmr::vector<uint32_t> result;
    if (value == nullptr || value->type != JsonValue::Type::Array) {
        return result;
    }
    result.reserve(value->arrayValue.size());
    for (const auto& entry : value->arrayValue) {
        result.push_back(jsonUInt(&entry, 0));
    }
    return result;
}

animation::Vec3 jsonVec3(const JsonValue* value, const animation::Vec3 fallback)
{
    if (value == nullptr || value->type != JsonValue::Type::Array || value->arrayValue.size() < 3) {
        return fallback;
    }
    return animation::Vec3{
        static_cast<float>(value->arrayValue[0].numberValue),
        static_cast<float>(value->arrayValue[1].numberValue),
        static_cast<float>(value->arrayValue[2].numberValue),
    };
}

animation::Quat jsonQuat(const JsonValue* value, const animation::Quat fallback)
{
    if (value == nullptr || value->type != JsonValue::Type::Array || value->arrayValue.size() < 4) {
        return fallback;
    }
    return animation::Quat{
        static_cast<float>(value->arrayValue[0].numberValue),
        static_cast<float>(value->arrayValue[1].numberValue),
        static_cast<float>(value->arrayValue[2].numberValue),
        static_cast<float>(value->arrayValue[3].numberValue),
    };
}

std::pmr::vector<BufferViewDesc> parseBufferViews(const JsonValue& root)
{
    std::pmr::vector<BufferViewDesc> views;
    const auto* array = root.find("bufferViews");
    if (array == nullptr || array->type != JsonValue::Type::Array) {
        return views;
    }
    views.reserve(array->arrayValue.size());
    for (const auto& object : array->arrayValue) {
        views.push_back(BufferViewDesc{
            jsonUInt(object.find("byteOffset"), 0),
            jsonUInt(object.find("byteLength"), 0),
            jsonUInt(object.find("byteStride"), 0),
        });
    }
    return views;
}

std::pmr::vector<AccessorDesc> parseAccessors(const JsonValue& root)
{
    std::pmr::vector<AccessorDesc> accessors;
    const auto* array = root.find("accessors");
    if (array == nullptr || array->type != JsonValue::Type::Array) {
        return accessors;
    }
    accessors.reserve(array->arrayValue.size());
    for (const auto& object : array->arrayValue) {
        accessors.push_back(AccessorDesc{
            jsonUInt(object.find("bufferView"), invalidIndex),
            jsonUInt(object.find("byteOffset"), 0),
            jsonUInt(object.find("componentType"), 0),
            jsonUInt(object.find("count"), 0),
            jsonString(object.find("type")),
            jsonBool(object.find("normalized"), false),
        });
    }
    return accessors;
}

std::pmr::vector<SkinDesc> parseSkins(const JsonValue& root)
{
    std::pmr::vector<SkinDesc> skins;
    const auto* array = root.find("skins");
    if (array == nullptr || array->type != JsonValue::Type::Array) {
        return skins;
    }
    skins.reserve(array->arrayValue.size());
    for (const auto& object : array->arrayValue) {
        SkinDesc skin;
        skin.joints = jsonUIntArray(object.find("joints"));
        skin.inverseBindMatricesAccessor =
            jsonUInt(object.find("inverseBindMatrices"), invalidIndex);
        skins.push_back(std::move(skin));
    }
    return skins;
}

std::pmr::vector<NodeDesc> parseNodes(const JsonValue& root)
{
    std::pmr::vector<NodeDesc> nodes;
    const auto* array = root.find("nodes");
    if (array == nullptr || array->type != JsonValue::Type::Array) {
        return nodes;
    }
    nodes.reserve(array->arrayValue.size());
    for (const auto& object : array->arrayValue) {
        NodeDesc Node;
        Node.name = jsonString(object.find("name"));
        Node.children = jsonUIntArray(object.find("children"));
        Node.localTransform.translation = jsonVec3(object.find("translation"), {});
        Node.localTransform.rotation =
            jsonQuat(object.find("rotation"), animation::Quat::identity());
        Node.localTransform.scale = jsonVec3(object.find("scale"), {1.0f, 1.0f, 1.0f});
        nodes.push_back(std::move(Node));
    }
    for (std::size_t parent = 0; parent < nodes.size(); ++parent) {
        for (const auto child : nodes[parent].children) {
            if (child < nodes.size()) {
                nodes[child].parentIndex = static_cast<int>(parent);
            }
        }
    }
    return nodes;
}

std::size_t componentSize(const uint32_t componentType)
{
    switch (componentType) {
    case 5120:
    case 5121:
        return 1;
    case 5122:
    case 5123:
        return 2;
    case 5125:
    case 5126:
        return 4;
    default:
        return 0;
    }
}

std::size_t componentCount(const std::pmr::string& type)
{
    if (type == "SCALAR") {
        return 1;
    }
    if (type == "VEC2") {
        return 2;
    }
    if (type == "VEC3") {
        return 3;
    }
    if (type == "VEC4") {
        return 4;
    }
    if (type == "MAT4") {
        return 16;
    }
    return 0;
}

bool accessorLayout(const std::pmr::string& bin,
    const std::pmr::vector<BufferViewDesc>& views,
    const AccessorDesc& accessor,
    std::size_t& base,
    std::size_t& stride,
    std::size_t& elementSize)
{
    if (accessor.bufferView >= views.size()) {
        return false;
    }
    const auto components = componentCount(accessor.type);
    const auto size = componentSize(accessor.componentType);
    if (components == 0 || size == 0) {
        return false;
    }
    const auto& view = views[accessor.bufferView];
    base = static_cast<std::size_t>(view.byteOffset) + accessor.byteOffset;
    elementSize = size * components;
    stride = view.byteStride == 0 ? elementSize : view.byteStride;
    if (accessor.count == 0) {
        return base <= bin.size();
    }
    return base + stride * (accessor.count - 1u) + elementSize <= bin.size();
}

float readF32(const std::pmr::string& bytes, const std::size_t offset)
{
    float value = 0.0f;
    std::memcpy(&value, bytes.data() + offset, sizeof(float));
    return value;
}

float halfToFloat(uint16_t half)
{
    // IEEE 754-2008 binary16 -> binary32
    const uint32_t mantissa = half & 0x03FFu;
    const uint32_t exponent = (half >> 10) & 0x001Fu;
    const uint32_t sign = (half >> 15) & 0x0001u;

    union {
        float f;
        uint32_t u;
    } result;

    if (exponent == 0) {
        // Subnormal / zero: map to float subnormal/zero
        const int shift = 24 - 1; // normal-mantissa bits for f32
        result.u = (sign << 31) | (0x70u << 23) | (mantissa << 13);
        result.f -= 16777216.0f * 0.5f; // magic number flush
    } else if (exponent == 31) {
        // Inf / NaN
        result.u = (sign << 31) | (0xFFu << 23) | (mantissa << 13);
    } else {
        // Normal
        result.u = (sign << 31) | ((exponent + 112) << 23) | (mantissa << 13);
    }
    return result.f;
}

bool readFloatVectorAccessor(const std::pmr::string& bin,
    const std::pmr::vector<BufferViewDesc>& views,
    const AccessorDesc& accessor,
    const char* expectedType,
    std::pmr::vector<float>& out)
{
    if (accessor.type != expectedType) {
        return false;
    }
    std::size_t base = 0;
    std::size_t stride = 0;
    std::size_t elementSize = 0;
    if (!accessorLayout(bin, views, accessor, base, stride, elementSize)) {
        return false;
    }
    const auto components = componentCount(accessor.type);
    const auto compSize = componentSize(accessor.componentType);
    out.resize(accessor.count * components);
    for (uint32_t index = 0; index < accessor.count; ++index) {
        const auto element = base + stride * index;
        for (std::size_t component = 0; component < components; ++component) {
            const auto compOffset = element + component * compSize;
            if (accessor.componentType == 5126) {
                // FLOAT
                out[index * components + component] = readF32(bin, compOffset);
            } else if (accessor.componentType == 5131) {
                // HALF_FLOAT
                uint16_t half = 0;
                std::memcpy(&half, bin.data() + compOffset, sizeof(uint16_t));
                out[index * components + component] = halfToFloat(half);
            } else if (accessor.componentType == 5123) {
                // UNSIGNED_SHORT normalized
                uint16_t us = 0;
                std::memcpy(&us, bin.data() + compOffset, sizeof(uint16_t));
                out[index * components + component] = static_cast<float>(us) / 65535.0f;
            } else if (accessor.componentType == 5122) {
                // SHORT normalized
                int16_t s = 0;
                std::memcpy(&s, bin.data() + compOffset, sizeof(int16_t));
                out[index * components + component] =
                    std::max(-1.0f, static_cast<float>(s) / 32767.0f);
            } else if (accessor.componentType == 5120) {
                // BYTE normalized
                out[index * components + component] = std::max(
                    -1.0f, static_cast<float>(static_cast<int8_t>(bin[compOffset])) / 127.0f);
            } else if (accessor.componentType == 5121) {
                // UNSIGNED_BYTE normalized
                out[index * components + component] =
                    static_cast<uint8_t>(bin[compOffset]) / 255.0f;
            } else {
                return false;
            }
        }
    }
    return true;
}

bool readScalarFloatAccessor(const std::pmr::string& bin,
    const std::pmr::vector<BufferViewDesc>& views,
    const AccessorDesc& accessor,
    std::pmr::vector<float>& out)
{
    return readFloatVectorAccessor(bin, views, accessor, "SCALAR", out);
}

bool readIndexAccessor(const std::pmr::string& bin,
    const std::pmr::vector<BufferViewDesc>& views,
    const AccessorDesc& accessor,
    std::pmr::vector<uint32_t>& out)
{
    if (accessor.type != "SCALAR") {
        return false;
    }
    std::size_t base = 0;
    std::size_t stride = 0;
    std::size_t elementSize = 0;
    if (!accessorLayout(bin, views, accessor, base, stride, elementSize)) {
        return false;
    }
    out.resize(accessor.count);
    for (uint32_t index = 0; index < accessor.count; ++index) {
        const auto offset = base + stride * index;
        if (accessor.componentType == 5121) {
            out[index] = static_cast<unsigned char>(bin[offset]);
        } else if (accessor.componentType == 5123) {
            uint16_t value = 0;
            std::memcpy(&value, bin.data() + offset, sizeof(uint16_t));
            out[index] = value;
        } else if (accessor.componentType == 5125) {
            uint32_t value = 0;
            std::memcpy(&value, bin.data() + offset, sizeof(uint32_t));
            out[index] = value;
        } else {
            return false;
        }
    }
    return true;
}

bool readJointsAccessor(const std::pmr::string& bin,
    const std::pmr::vector<BufferViewDesc>& views,
    const AccessorDesc& accessor,
    std::pmr::vector<std::array<uint16_t, 4>>& out)
{
    if (accessor.type != "VEC4") {
        return false;
    }
    std::size_t base = 0;
    std::size_t stride = 0;
    std::size_t elementSize = 0;
    if (!accessorLayout(bin, views, accessor, base, stride, elementSize)) {
        return false;
    }
    out.resize(accessor.count);
    for (uint32_t index = 0; index < accessor.count; ++index) {
        const auto element = base + stride * index;
        for (std::size_t component = 0; component < 4; ++component) {
            if (accessor.componentType == 5121) {
                out[index][component] = static_cast<unsigned char>(bin[element + component]);
            } else if (accessor.componentType == 5123) {
                uint16_t value = 0;
                std::memcpy(
                    &value, bin.data() + element + component * sizeof(uint16_t), sizeof(uint16_t));
                out[index][component] = value;
            } else {
                return false;
            }
        }
    }
    return true;
}

bool readWeightsAccessor(const std::pmr::string& bin,
    const std::pmr::vector<BufferViewDesc>& views,
    const AccessorDesc& accessor,
    std::pmr::vector<std::array<float, 4>>& out)
{
    if (accessor.type != "VEC4") {
        return false;
    }
    std::size_t base = 0;
    std::size_t stride = 0;
    std::size_t elementSize = 0;
    if (!accessorLayout(bin, views, accessor, base, stride, elementSize)) {
        return false;
    }
    out.resize(accessor.count);
    for (uint32_t index = 0; index < accessor.count; ++index) {
        const auto element = base + stride * index;
        float sum = 0.0f;
        for (std::size_t component = 0; component < 4; ++component) {
            float value = 0.0f;
            if (accessor.componentType == 5126) {
                value = readF32(bin, element + component * sizeof(float));
            } else if (accessor.componentType == 5121) {
                value = static_cast<unsigned char>(bin[element + component]);
                if (accessor.normalized) {
                    value /= 255.0f;
                }
            } else if (accessor.componentType == 5123) {
                uint16_t raw = 0;
                std::memcpy(
                    &raw, bin.data() + element + component * sizeof(uint16_t), sizeof(uint16_t));
                value = static_cast<float>(raw);
                if (accessor.normalized) {
                    value /= 65535.0f;
                }
            } else {
                return false;
            }
            out[index][component] = value;
            sum += value;
        }
        if (sum > 0.000001f) {
            for (auto& weight : out[index]) {
                weight /= sum;
            }
        } else {
            out[index] = {1.0f, 0.0f, 0.0f, 0.0f};
        }
    }
    return true;
}

bool readMat4Accessor(const std::pmr::string& bin,
    const std::pmr::vector<BufferViewDesc>& views,
    const AccessorDesc& accessor,
    std::pmr::vector<animation::Mat4>& out)
{
    std::pmr::vector<float> values;
    if (!readFloatVectorAccessor(bin, views, accessor, "MAT4", values)) {
        return false;
    }
    out.resize(accessor.count);
    for (uint32_t index = 0; index < accessor.count; ++index) {
        for (std::size_t component = 0; component < 16; ++component) {
            out[index].m[component] = values[index * 16u + component];
        }
    }
    return true;
}

bool buildSkeleton(const std::pmr::vector<NodeDesc>& nodes,
    const SkinDesc& skin,
    const std::pmr::vector<animation::Mat4>& inverseBindMatrices,
    AnimatedMeshAsset& asset,
    std::pmr::unordered_map<uint32_t, uint32_t>& nodeToJoint,
    std::pmr::string& diagnostics)
{
    if (skin.joints.empty()) {
        diagnostics = "skin joints are required";
        return false;
    }

    nodeToJoint.clear();
    for (uint32_t jointIndex = 0; jointIndex < skin.joints.size(); ++jointIndex) {
        if (skin.joints[jointIndex] >= nodes.size()) {
            diagnostics = "skin joint Node index is out of range";
            return false;
        }
        nodeToJoint[skin.joints[jointIndex]] = jointIndex;
    }

    asset.skeleton.joints.clear();
    asset.bindPose.clear();
    asset.skeleton.joints.reserve(skin.joints.size());
    asset.bindPose.reserve(skin.joints.size());
    for (uint32_t jointIndex = 0; jointIndex < skin.joints.size(); ++jointIndex) {
        const auto nodeIndex = skin.joints[jointIndex];
        const auto& Node = nodes[nodeIndex];
        int parentJoint = -1;
        if (Node.parentIndex >= 0) {
            const auto found = nodeToJoint.find(static_cast<uint32_t>(Node.parentIndex));
            if (found != nodeToJoint.end()) {
                parentJoint = static_cast<int>(found->second);
            }
        }
        asset.skeleton.joints.push_back(animation::Joint{
            Node.name,
            parentJoint,
            jointIndex < inverseBindMatrices.size() ? inverseBindMatrices[jointIndex]
                                                    : animation::Mat4::identity(),
        });
        asset.bindPose.push_back(Node.localTransform);
    }

    if (!asset.skeleton.valid()) {
        diagnostics = "skin joint hierarchy must be parent-before-child";
        return false;
    }
    return true;
}

bool importClips(const JsonValue& root,
    const std::pmr::vector<AccessorDesc>& accessors,
    const std::pmr::vector<BufferViewDesc>& bufferViews,
    const std::pmr::string& bin,
    const std::pmr::unordered_map<uint32_t, uint32_t>& nodeToJoint,
    const std::pmr::vector<animation::LocalTransform>& bindPose,
    std::pmr::vector<animation::AnimationClip>& clips)
{
    const auto* animations = root.find("animations");
    if (animations == nullptr || animations->type != JsonValue::Type::Array) {
        return false;
    }

    for (std::size_t animationIndex = 0; animationIndex < animations->arrayValue.size();
        ++animationIndex) {
        const auto& animationObject = animations->arrayValue[animationIndex];
        const auto* samplersJson = animationObject.find("samplers");
        const auto* channelsJson = animationObject.find("channels");
        if (samplersJson == nullptr || channelsJson == nullptr
            || samplersJson->type != JsonValue::Type::Array
            || channelsJson->type != JsonValue::Type::Array) {
            continue;
        }

        std::pmr::vector<AnimationSamplerDesc> samplers;
        samplers.reserve(samplersJson->arrayValue.size());
        for (const auto& samplerJson : samplersJson->arrayValue) {
            samplers.push_back(AnimationSamplerDesc{
                jsonUInt(samplerJson.find("input"), invalidIndex),
                jsonUInt(samplerJson.find("output"), invalidIndex),
            });
        }

        animation::AnimationClip clip;
        clip.name = jsonString(animationObject.find("name"),
            std::pmr::string("Animation") + arp::core::toPmrString(std::to_string(animationIndex)));
        for (const auto& channelJson : channelsJson->arrayValue) {
            const auto samplerIndex = jsonUInt(channelJson.find("sampler"), invalidIndex);
            const auto* target = channelJson.find("target");
            if (samplerIndex >= samplers.size() || target == nullptr) {
                continue;
            }
            const auto targetNode = jsonUInt(target->find("node"), invalidIndex);
            auto targetNodeFallback = jsonUInt(target->find("Node"), invalidIndex);
            const auto actualTargetNode =
                targetNode != invalidIndex ? targetNode : targetNodeFallback;
            const auto jointFound = nodeToJoint.find(actualTargetNode);
            if (jointFound == nodeToJoint.end()) {
                continue;
            }
            const auto& sampler = samplers[samplerIndex];
            if (sampler.inputAccessor >= accessors.size()
                || sampler.outputAccessor >= accessors.size()) {
                continue;
            }

            std::pmr::vector<float> times;
            const auto& timeAcc = accessors[sampler.inputAccessor];
            if (!readScalarFloatAccessor(bin, bufferViews, timeAcc, times) || times.empty()) {
                // diagnostic suppressed
                continue;
            }

            animation::AnimationChannel channel;
            channel.jointIndex = jointFound->second;
            channel.targetPath = jsonString(target->find("path"), "translation");
            channel.keyframes.reserve(times.size());
            if (channel.targetPath == "translation") {
                std::pmr::vector<float> translations;
                const auto& transAcc = accessors[sampler.outputAccessor];
                if (!readFloatVectorAccessor(bin, bufferViews, transAcc, "VEC3", translations)
                    || translations.size() / 3u != times.size()) {
                    // diagnostic suppressed
                    continue;
                }
                for (std::size_t key = 0; key < times.size(); ++key) {
                    channel.keyframes.push_back(animation::JointKeyframe{
                        times[key],
                        {{translations[key * 3u + 0u],
                            translations[key * 3u + 1u],
                            translations[key * 3u + 2u]}},
                    });
                }
            } else if (channel.targetPath == "rotation") {
                std::pmr::vector<float> rotations;
                if (!readFloatVectorAccessor(
                        bin, bufferViews, accessors[sampler.outputAccessor], "VEC4", rotations)
                    || rotations.size() / 4u != times.size()) {
                    continue;
                }
                for (std::size_t key = 0; key < times.size(); ++key) {
                    animation::JointTransform transform;
                    transform.rotation = {
                        rotations[key * 4u + 0u],
                        rotations[key * 4u + 1u],
                        rotations[key * 4u + 2u],
                        rotations[key * 4u + 3u],
                    };
                    channel.keyframes.push_back(animation::JointKeyframe{
                        times[key],
                        transform,
                    });
                }
            } else if (channel.targetPath == "scale") {
                std::pmr::vector<float> scales;
                if (!readFloatVectorAccessor(
                        bin, bufferViews, accessors[sampler.outputAccessor], "VEC3", scales)
                    || scales.size() / 3u != times.size()) {
                    continue;
                }
                for (std::size_t key = 0; key < times.size(); ++key) {
                    animation::JointTransform transform;
                    transform.scale = {
                        scales[key * 3u + 0u],
                        scales[key * 3u + 1u],
                        scales[key * 3u + 2u],
                    };
                    channel.keyframes.push_back(animation::JointKeyframe{
                        times[key],
                        transform,
                    });
                }
            } else {
                continue;
            }

            if (!channel.keyframes.empty()) {
                clip.durationSeconds =
                    std::max(clip.durationSeconds, channel.keyframes.back().timeSeconds);
                clip.channels.push_back(std::move(channel));
            }
        }
        if (clip.durationSeconds > 0.0f && !clip.channels.empty()) {
            clips.push_back(std::move(clip));
        }
    }
    return !clips.empty();
}

} // namespace

AnimatedMeshImportResult importAnimatedMeshFromGlbFile(const std::filesystem::path& path)
{
    std::pmr::string bytes;
    if (!detail::readFile(path, bytes)) {
        return fail("failed to read GLB file");
    }

    GlbChunks chunks;
    std::pmr::string diagnostics;
    if (!detail::readGlbChunks(bytes, chunks, diagnostics)) {
        return fail(diagnostics);
    }

    JsonValue root;
    if (!detail::parseJson(chunks.json, root)) {
        return fail("GLB JSON could not be parsed");
    }

    const auto bufferViews = parseBufferViews(root);
    const auto accessors = parseAccessors(root);
    const auto nodes = parseNodes(root);
    const auto skins = parseSkins(root);
    if (skins.empty()) {
        return fail("skin with joints is required");
    }

    // Collect ALL skinned primitives across all meshes
    struct PrimitiveDesc {
        uint32_t positionAccessor = invalidIndex;
        uint32_t normalAccessor = invalidIndex;
        uint32_t texcoordAccessor = invalidIndex;
        uint32_t tangentAccessor = invalidIndex;
        uint32_t jointsAccessor = invalidIndex;
        uint32_t weightsAccessor = invalidIndex;
        uint32_t indicesAccessor = invalidIndex;
        uint32_t material = 0;
        uint32_t skinIndex = 0; // Which skin this primitive's mesh node uses
    };

    std::pmr::vector<PrimitiveDesc> primitives;
    {
        const auto* meshes = root.find("meshes");
        if (meshes != nullptr && meshes->type == JsonValue::Type::Array) {
            // Map mesh index -> skin index (from nodes that reference mesh+skin)
            std::pmr::unordered_map<uint32_t, uint32_t> meshToSkin;
            const auto* nodesJson = root.find("nodes");
            if (nodesJson != nullptr && nodesJson->type == JsonValue::Type::Array) {
                for (const auto& node : nodesJson->arrayValue) {
                    const auto meshIdx = jsonUInt(node.find("mesh"), invalidIndex);
                    const auto skinIdx = jsonUInt(node.find("skin"), invalidIndex);
                    if (meshIdx != invalidIndex && skinIdx != invalidIndex
                        && skinIdx < skins.size()) {
                        meshToSkin[meshIdx] = skinIdx;
                    }
                }
            }
            for (std::size_t meshIdx = 0; meshIdx < meshes->arrayValue.size(); ++meshIdx) {
                const auto& mesh = meshes->arrayValue[meshIdx];
                const auto* primArr = mesh.find("primitives");
                if (primArr == nullptr || primArr->type != JsonValue::Type::Array)
                    continue;
                const auto skinIt = meshToSkin.find(static_cast<uint32_t>(meshIdx));
                const uint32_t primSkin = skinIt != meshToSkin.end() ? skinIt->second : 0u;
                for (const auto& primJson : primArr->arrayValue) {
                    const auto* attrs = primJson.find("attributes");
                    if (attrs == nullptr)
                        continue;
                    PrimitiveDesc pd;
                    pd.positionAccessor = jsonUInt(attrs->find("POSITION"), invalidIndex);
                    pd.normalAccessor = jsonUInt(attrs->find("NORMAL"), invalidIndex);
                    pd.texcoordAccessor = jsonUInt(attrs->find("TEXCOORD_0"), invalidIndex);
                    pd.tangentAccessor = jsonUInt(attrs->find("TANGENT"), invalidIndex);
                    pd.jointsAccessor = jsonUInt(attrs->find("JOINTS_0"), invalidIndex);
                    pd.weightsAccessor = jsonUInt(attrs->find("WEIGHTS_0"), invalidIndex);
                    pd.indicesAccessor = jsonUInt(primJson.find("indices"), invalidIndex);
                    pd.material = jsonUInt(primJson.find("material"), 0);
                    pd.skinIndex = primSkin;
                    if (pd.positionAccessor != invalidIndex && pd.jointsAccessor != invalidIndex
                        && pd.weightsAccessor != invalidIndex) {
                        primitives.push_back(pd);
                    }
                }
            }
        }
    }
    if (primitives.empty()) {
        return fail("skinned primitive with POSITION, JOINTS_0 and WEIGHTS_0 is required");
    }

    // Validate accessor ranges
    for (const auto& prim : primitives) {
        if (prim.positionAccessor >= accessors.size() || prim.jointsAccessor >= accessors.size()
            || prim.weightsAccessor >= accessors.size()
            || (prim.normalAccessor != invalidIndex && prim.normalAccessor >= accessors.size())
            || (prim.texcoordAccessor != invalidIndex && prim.texcoordAccessor >= accessors.size())
            || (prim.tangentAccessor != invalidIndex && prim.tangentAccessor >= accessors.size())
            || (prim.indicesAccessor != invalidIndex && prim.indicesAccessor >= accessors.size())) {
            return fail("primitive accessor index is out of range");
        }
    }

    AnimatedMeshAsset asset;

    // Read the canonical skin (skins[0]) inverse bind matrices. All primitives
    // share the same joint hierarchy, so skin[0]'s skeleton/IBM becomes the
    // reference. Primitives belonging to other meshes/skins get their vertices
    // pre-transformed into skin[0]'s mesh-local space so a single skeleton
    // correctly skins the whole combined mesh.
    std::pmr::vector<animation::Mat4> canonicalInverseBindMatrices;
    const auto& skin = skins.front();
    if (skin.inverseBindMatricesAccessor != invalidIndex) {
        if (skin.inverseBindMatricesAccessor >= accessors.size()
            || !readMat4Accessor(chunks.bin,
                bufferViews,
                accessors[skin.inverseBindMatricesAccessor],
                canonicalInverseBindMatrices)) {
            return fail("inverseBindMatrices accessor must be MAT4 float data");
        }
    }
    // Precompute the per-skin vertex pre-transform relative to skin[0].
    // T_p = affineInverse(IBM_0[firstJoint]) * IBM_p[firstJoint]
    // Applying T_p to a primitive's vertices puts them in skin[0]'s mesh-local
    // space, after which skin[0]'s IBM correctly skins them.
    std::pmr::vector<animation::Mat4> perSkinPreTransform(
        skins.size(), animation::Mat4::identity());
    if (!canonicalInverseBindMatrices.empty()) {
        const auto canonicalInverse =
            animation::affineInverse(canonicalInverseBindMatrices.front());
        for (std::size_t si = 0; si < skins.size(); ++si) {
            if (si == 0u) {
                perSkinPreTransform[si] = animation::Mat4::identity();
                continue;
            }
            const auto& otherSkin = skins[si];
            if (otherSkin.inverseBindMatricesAccessor == invalidIndex
                || otherSkin.inverseBindMatricesAccessor >= accessors.size()) {
                continue;
            }
            std::pmr::vector<animation::Mat4> otherIbm;
            if (!readMat4Accessor(chunks.bin,
                    bufferViews,
                    accessors[otherSkin.inverseBindMatricesAccessor],
                    otherIbm)
                || otherIbm.empty()) {
                continue;
            }
            perSkinPreTransform[si] = animation::multiply(canonicalInverse, otherIbm.front());
        }
    }

    for (const auto& prim : primitives) {
        const auto vertexBase = static_cast<uint32_t>(asset.mesh.vertices.size());

        std::pmr::vector<float> positions;
        std::pmr::vector<float> normals;
        std::pmr::vector<float> uvs;
        std::pmr::vector<float> tangents;
        std::pmr::vector<uint32_t> indices;
        std::pmr::vector<std::array<uint16_t, 4>> joints;
        std::pmr::vector<std::array<float, 4>> weights;

        if (!readFloatVectorAccessor(
                chunks.bin, bufferViews, accessors[prim.positionAccessor], "VEC3", positions)) {
            return fail("POSITION accessor must be float3 binary data");
        }
        const auto vertexCount = positions.size() / 3u;
        if (prim.normalAccessor != invalidIndex
            && (!readFloatVectorAccessor(
                    chunks.bin, bufferViews, accessors[prim.normalAccessor], "VEC3", normals)
                || normals.size() / 3u != vertexCount)) {
            return fail("NORMAL accessor must match POSITION count");
        }
        if (prim.texcoordAccessor != invalidIndex
            && (!readFloatVectorAccessor(
                    chunks.bin, bufferViews, accessors[prim.texcoordAccessor], "VEC2", uvs)
                || uvs.size() / 2u != vertexCount)) {
            return fail("TEXCOORD_0 accessor must match POSITION count");
        }
        if (prim.tangentAccessor != invalidIndex
            && (!readFloatVectorAccessor(
                    chunks.bin, bufferViews, accessors[prim.tangentAccessor], "VEC4", tangents)
                || tangents.size() / 4u != vertexCount)) {
            return fail("TANGENT accessor must match POSITION count");
        }
        if (!readJointsAccessor(chunks.bin, bufferViews, accessors[prim.jointsAccessor], joints)
            || joints.size() != vertexCount) {
            return fail("JOINTS_0 accessor must be vec4 u8/u16 data matching POSITION count");
        }
        if (!readWeightsAccessor(chunks.bin, bufferViews, accessors[prim.weightsAccessor], weights)
            || weights.size() != vertexCount) {
            return fail("WEIGHTS_0 accessor must be vec4 data matching POSITION count");
        }
        if (prim.indicesAccessor != invalidIndex) {
            if (!readIndexAccessor(
                    chunks.bin, bufferViews, accessors[prim.indicesAccessor], indices)) {
                return fail("indices accessor must be scalar u8/u16/u32 binary data");
            }
        } else {
            indices.reserve(vertexCount);
            for (uint32_t vi = 0; vi < vertexCount; ++vi) {
                indices.push_back(vi);
            }
        }

        // Append vertices
        asset.mesh.vertices.reserve(asset.mesh.vertices.size() + vertexCount);
        asset.skinnedVertices.reserve(asset.skinnedVertices.size() + vertexCount);
        const auto& preTransform = perSkinPreTransform[prim.skinIndex];
        const bool needsPreTransform = (prim.skinIndex != 0u);
        for (std::size_t vi = 0; vi < vertexCount; ++vi) {
            MeshVertex vertex;
            vertex.position[0] = positions[vi * 3u + 0u];
            vertex.position[1] = positions[vi * 3u + 1u];
            vertex.position[2] = positions[vi * 3u + 2u];
            if (!normals.empty()) {
                vertex.normal[0] = normals[vi * 3u + 0u];
                vertex.normal[1] = normals[vi * 3u + 1u];
                vertex.normal[2] = normals[vi * 3u + 2u];
            }
            if (!uvs.empty()) {
                vertex.uv[0] = uvs[vi * 2u + 0u];
                vertex.uv[1] = uvs[vi * 2u + 1u];
            }
            if (!tangents.empty()) {
                vertex.tangent[0] = tangents[vi * 4u + 0u];
                vertex.tangent[1] = tangents[vi * 4u + 1u];
                vertex.tangent[2] = tangents[vi * 4u + 2u];
                vertex.tangent[3] = tangents[vi * 4u + 3u];
            }
            // Pre-transform this primitive's vertices into skin[0]'s
            // mesh-local space so the canonical IBM skins them correctly.
            if (needsPreTransform) {
                const auto tp = animation::transformPoint(preTransform,
                    animation::Vec3{vertex.position[0], vertex.position[1], vertex.position[2]});
                vertex.position[0] = tp.x;
                vertex.position[1] = tp.y;
                vertex.position[2] = tp.z;
                if (!normals.empty()) {
                    const auto tn = animation::transformVector(preTransform,
                        animation::Vec3{vertex.normal[0], vertex.normal[1], vertex.normal[2]});
                    vertex.normal[0] = tn.x;
                    vertex.normal[1] = tn.y;
                    vertex.normal[2] = tn.z;
                }
                if (!tangents.empty()) {
                    const auto tt = animation::transformVector(preTransform,
                        animation::Vec3{vertex.tangent[0], vertex.tangent[1], vertex.tangent[2]});
                    vertex.tangent[0] = tt.x;
                    vertex.tangent[1] = tt.y;
                    vertex.tangent[2] = tt.z;
                    // tangent[3] (sign) is unaffected by an affine transform.
                }
            }
            asset.mesh.vertices.push_back(vertex);
            asset.skinnedVertices.push_back(SkinnedVertex{joints[vi], weights[vi]});
        }

        // Append indices (rebase for the current vertex offset)
        auto firstIndex = static_cast<uint32_t>(asset.mesh.indices.size());
        asset.mesh.indices.reserve(asset.mesh.indices.size() + indices.size());
        for (auto idx : indices) {
            asset.mesh.indices.push_back(idx + vertexBase);
        }
        asset.mesh.submeshes.push_back(Submesh{
            firstIndex,
            static_cast<uint32_t>(indices.size()),
            prim.material,
        });
    }

    if (!asset.mesh.validForIndexedDraw()) {
        return fail("imported animated mesh is not valid for indexed draw");
    }
    generateTangentsForIndexedMesh(asset.mesh);

    // The canonical inverse bind matrices (skin[0]) were already read above.
    // Build the skeleton from skin[0]; per-primitive vertices were pre-transformed
    // into skin[0]'s mesh-local space, so a single skeleton skins the whole mesh.

    std::pmr::unordered_map<uint32_t, uint32_t> nodeToJoint;
    if (!buildSkeleton(
            nodes, skin, canonicalInverseBindMatrices, asset, nodeToJoint, diagnostics)) {
        return fail(diagnostics);
    }
    importClips(root, accessors, bufferViews, chunks.bin, nodeToJoint, asset.bindPose, asset.clips);

    // Convert from glTF right-handed to engine left-handed coordinates.
    convertAnimatedMeshRightHandedToLeftHanded(asset);

    AnimatedMeshImportResult result;
    result.success = true;
    const auto materialAssets = detail::parseImportedGlbMaterialAssets(
        root, chunks.bin, detail::parseGlbBufferViewRanges(root));
    if (!materialAssets.success)
        return fail(materialAssets.diagnostics);
    asset.materials = std::move(materialAssets.materials);
    asset.textures = std::move(materialAssets.textures);
    asset.images = std::move(materialAssets.images);
    result.asset = std::move(asset);
    return result;
}

} // namespace arp::assets
