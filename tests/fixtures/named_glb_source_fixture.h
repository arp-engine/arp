#pragma once

#include "assets/glb_static_mesh_importer.h"
#include "core/encoding/base64.h"
#include "tests/fixtures/static_glb_geometry.h"
#include "tests/fixtures/ui_atlas_pixels.h"

namespace arp::tests {
class NamedGlbSourceFixture final {
public:
    static constexpr std::size_t sourceBudget = 65536;
    static constexpr const char* id = "00000000-0000-4000-8000-000000000075";

    static void replace(std::string& text, std::string_view from, std::string_view to)
    {
        const auto at = text.find(from);
        assert(at != std::string::npos);
        text.replace(at, from.size(), to);
    }

    static std::string document()
    {
        auto json = StaticGlbGeometryFixture::document();
        replace(json,
            "\"nodes\":[{\"mesh\":0}]",
            "\"nodes\":[{\"mesh\":0,\"name\":\"leaf\\uD83C\\uDF3F\",\"translation\":[10,20,30],\"scale\":[2,3,4]}]");
        replace(json, "\"TANGENT\":3", "\"TANGENT\":3,\"COLOR_0\":3");
        replace(json,
            "\"name\":\"external-slot-zero\"",
            "\"name\":\"external-slot-zero\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.2,0.4,0.6,1],\"baseColorTexture\":{\"index\":0}},\"alphaMode\":\"BLEND\",\"extensions\":{\"KHR_materials_emissive_strength\":{\"emissiveStrength\":2}}");
        const auto encoded = core::Base64::encode(
            core::ByteBuffer(uiAtlasPngBytes, uiAtlasPngBytes + sizeof(uiAtlasPngBytes)));
        json.insert(json.size() - 1,
            std::string(
                ",\"extensionsUsed\":[\"KHR_materials_emissive_strength\"],\"images\":[{\"uri\":\"data:image/png;base64,")
                + encoded.c_str() + "\"}],\"textures\":[{\"source\":0}]");
        return json;
    }

    static assets::NamedVariantSourceImportLimits limits()
    {
        return {sourceBudget,
            2 * 1024 * 1024,
            12,
            12,
            4,
            4,
            16,
            16,
            32,
            2048,
            assets::NamedVariantColorPolicy::PreserveRgbaWind};
    }

    static core::pmr::Vector<std::uint8_t> bytes()
    {
        return StaticGlbGeometryFixture::bytes(document());
    }
};
} // namespace arp::tests
