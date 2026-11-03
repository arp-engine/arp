#pragma once
#include "core/encoding/yaml_contract.h"
#include "resource/static_mesh_shadow_lod_resource.h"
#include "tests/fixtures/static_glb_geometry.h"

#include <cassert>
#include <iomanip>
#include <sstream>

namespace arp::tests {
// Exact same surfaces at all levels: the zero-distance certificates have a
// literal, independently checkable proof rather than tuned error tolerances.
class ShadowLodSourceFixture final {
public:
    using Y = core::YamlContract;
    static constexpr const char* originalId = "00000000-0000-4000-8000-000000000070";
    static constexpr const char* lodId = "00000000-0000-4000-8000-000000000071";
    static constexpr std::size_t sourceBudget = 65536;
    core::pmr::Vector<resource::ResourceSourceBuffer> files;

    ShadowLodSourceFixture()
    {
        const auto geometry = StaticGlbGeometryFixture::bytes();
        const auto imported = assets::importStaticMeshWithSourceIdentityFromGlbBytes(
            {geometry.data(), geometry.size()}, {sourceBudget, 3, 3, 1, true, 32, 1024});
        assert(imported.geometry.success && imported.identities.size() == 1);
        add("source.glb", geometry);
        add("lod/one.glb", geometry);
        add("lod/two.glb", geometry);
        const auto surfaceHash = hex(imported.identities[0].localSurfaceDigest);
        auto certificate = Y::Node::mapping();
        certificate["type"] = "TriangleSurfaceCertificates";
        certificate["contractVersion"] = 1;
        certificate["sourceSha256"] = hash(geometry).c_str();
        certificate["certifierSha256"] =
            core::Sha256::hexDigest("ShadowLodSourceFixture/1").substr(7).c_str();
        certificate["certificates"] = Y::Node::sequence();
        auto prototype = Y::Node::mapping();
        prototype["sourceMesh"] = 0;
        prototype["sourcePrimitive"] = 0;
        prototype["sourceMaterial"] = 0;
        prototype["localAbsoluteSupport"] = Y::Node::sequence();
        for (const auto value : imported.identities[0].localAbsolutePosition)
            prototype["localAbsoluteSupport"].get_value_ref<Y::Node::sequence_type&>().push_back(
                Y::Node(value));
        prototype["levels"] = Y::Node::sequence();
        for (unsigned level = 1; level <= 2; ++level) {
            auto spec = Y::Node::mapping();
            spec["sourceMesh"] = 0;
            spec["sourcePrimitive"] = 0;
            spec["level"] = level;
            spec["upperBound"] = 0.0;
            spec["sourceSurfaceSha256"] = surfaceHash.c_str();
            spec["targetSurfaceSha256"] = surfaceHash.c_str();
            prototype["levels"].get_value_ref<Y::Node::sequence_type&>().push_back(spec);
            auto proof = Y::Node::mapping();
            proof["contractVersion"] = 1;
            proof["upperBound"] = 0.0;
            proof["sourceSurfaceSha256"] = surfaceHash.c_str();
            proof["targetSurfaceSha256"] = surfaceHash.c_str();
            proof["exactSurfaceSet"] = true;
            proof["forward"] = Y::Node(nullptr);
            proof["reverse"] = Y::Node(nullptr);
            spec["proof"] = std::move(proof);
            certificate["certificates"].get_value_ref<Y::Node::sequence_type&>().push_back(
                std::move(spec));
        }
        addText("lod/cert.yaml", Y::serialize(certificate));
        auto order = Y::Node::mapping();
        order["type"] = "MeshSourceIndexOrder";
        order["contractVersion"] = 1;
        order["sourceSha256"] = hash(geometry).c_str();
        order["sources"] = Y::Node::sequence();
        auto entry = Y::Node::mapping();
        entry["node"] = 0;
        entry["primitive"] = 0;
        order["sources"].get_value_ref<Y::Node::sequence_type&>().push_back(std::move(entry));
        addText("lod/order.yaml", Y::serialize(order));
        for (unsigned version = 1; version <= 2; ++version) {
            auto manifest = Y::Node::mapping();
            manifest["type"] = "StaticMeshShadowLodAsset";
            manifest["contractVersion"] = version;
            manifest["status"] = "Prototype Complete";
            manifest["source"] = "source.glb";
            manifest["sourceSha256"] = hash(geometry).c_str();
            manifest["sourceCount"] = 1;
            manifest["selection"] = Y::Node::mapping();
            manifest["selection"]["status"] = "ExperimentalInputsNotVisualAcceptance";
            manifest["selection"]["shadowTexelCeiling"] = 1.0;
            manifest["selection"]["normalizedDepthCeiling"] = 1.0;
            manifest["levels"] = Y::Node::sequence();
            for (unsigned level = 1; level <= 2; ++level) {
                auto spec = binding(level == 1 ? "lod/one.glb" : "lod/two.glb");
                spec["level"] = level;
                spec["vertices"] = 3;
                spec["indices"] = 3;
                manifest["levels"].get_value_ref<Y::Node::sequence_type&>().push_back(
                    std::move(spec));
            }
            manifest["certificates"] = binding("lod/cert.yaml");
            if (version == 2)
                manifest["indexOrder"] = binding("lod/order.yaml");
            manifest["prototypes"] = Y::Node::sequence();
            manifest["prototypes"].get_value_ref<Y::Node::sequence_type&>().push_back(prototype);
            addText(
                version == 1 ? "lod/manifest1.yaml" : "lod/manifest2.yaml", Y::serialize(manifest));
        }
    }

    resource::ResourceSourceBuffer& file(std::string_view path)
    {
        for (auto& input : files)
            if (input.path == path)
                return input;
        assert(false);
        return files.front();
    }

    Y::Node binding(std::string_view path)
    {
        const auto& bytes = file(path).bytes;
        auto result = Y::Node::mapping();
        const auto leaf = path.substr(path.find_last_of('/') + 1);
        result["source"] = core::pmr::String(leaf).c_str();
        result["bytes"] = bytes.size();
        result["sha256"] = hash(bytes).c_str();
        return result;
    }

    static core::pmr::String hash(const core::pmr::Vector<std::uint8_t>& bytes)
    {
        return core::Sha256::hexDigest({reinterpret_cast<const char*>(bytes.data()), bytes.size()})
            .substr(7);
    }

    void addText(const char* path, std::string_view text)
    {
        core::pmr::Vector<std::uint8_t> bytes(text.begin(), text.end());
        add(path, bytes);
    }

    resource::ResourceSourceSet sources(resource::ResourceFileOrigin origin) const
    {
        resource::ResourceSourceSet result;
        for (const auto& input : files) {
            auto source = input;
            source.pathScope = origin.pathScope;
            source.mount = origin.mount;
            assert(result.add(std::move(source)));
        }
        return result;
    }

private:
    void add(const char* path, const core::pmr::Vector<std::uint8_t>& bytes)
    {
        resource::ResourceSourceBuffer input;
        input.path = path;
        input.bytes = bytes;
        files.push_back(std::move(input));
    }

    static core::pmr::String hex(const std::array<std::uint8_t, 32>& digest)
    {
        std::ostringstream output;
        output << std::hex << std::setfill('0');
        for (auto byte : digest)
            output << std::setw(2) << static_cast<unsigned>(byte);
        return core::pmr::String(output.str().c_str());
    }
};
} // namespace arp::tests
