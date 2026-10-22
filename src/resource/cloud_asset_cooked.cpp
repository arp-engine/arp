#include "resource/cloud_asset_cooked.h"

#include "core/encoding/unsigned_element_span.h"
#include "core/encoding/yaml_contract.h"
#include "core/hash/sha256.h"

#include <cstring>
#include <limits>

namespace arp::resource {
namespace {
using Y = core::YamlContract;
using W = core::UnsignedElementSpan;
using Result = assets::cloud::CloudAssetResult;
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
static_assert(sizeof(core::Vec4) == 16);

core::StringView view(const core::ByteBuffer& data)
{
    return {reinterpret_cast<const char*>(data.data()), data.size()};
}
} // namespace

Result CloudAssetCookedCodec::encode(
    const assets::cloud::CloudAsset& a, std::uint64_t maxDocumentBytes, core::pmr::String& output)
{
    output.clear();
    auto valid = assets::cloud::CloudAssetCooker::validate(a);
    if (!valid.success)
        return valid;
    const auto bytes = a.cells.size() * sizeof(core::Vec4);
    // Refuse before allocating base64/parser copies if payload alone exceeds
    // the caller's document budget. Final serialized length is checked too.
    if (bytes > (maxDocumentBytes / 4u) * 3u)
        return Result::failure("CLOUD-COOKED-DOCUMENT-BUDGET");
    core::pmr::String source;
    valid = assets::cloud::CloudAssetCooker::serialize(a.source, source);
    if (!valid.success)
        return valid;
    core::ByteBuffer payload;
    payload.resize(bytes);
    std::size_t offset = 0;
    for (const auto cell : a.cells)
        for (const float f : {cell.x, cell.y, cell.z, cell.w}) {
            std::uint32_t bits = 0;
            std::memcpy(&bits, &f, sizeof(bits));
            W::writeLittleEndian(bits, W::Width::Uint32, payload.data() + offset, 4u);
            offset += 4u;
        }
    auto root = Y::Node::mapping();
    root["type"] = "arp.cloud.cooked";
    root["contractVersion"] = assets::cloud::kCloudAssetVersion;
    root["source"] = Y::parseSingleDocument(source);
    root["encoding"] = "base64-float32-le-float4";
    root["cellCount"] = a.cells.size();
    // This field stores 64 lowercase hex digits, without the engine's tagged
    // "sha256:" presentation prefix.
    root["payloadSha256"] = core::Sha256::hexDigest(view(payload)).substr(7u).c_str();
    root["payload"] = core::Base64::encode(payload).c_str();
    auto document = Y::serialize(root);
    if (document.size() > maxDocumentBytes)
        return Result::failure("CLOUD-COOKED-DOCUMENT-BUDGET");
    output = std::move(document);
    return {};
}

Result CloudAssetCookedCodec::decode(core::StringView yaml,
    std::uint64_t maxDocumentBytes,
    std::uint64_t maxPayloadBytes,
    assets::cloud::CloudAsset& output)
{
    output = {};
    if (yaml.empty() || yaml.size() > maxDocumentBytes)
        return Result::failure("CLOUD-COOKED-DOCUMENT-BUDGET");
    try {
        const auto root = Y::parseSingleDocument(yaml);
        Y::onlyKeys(root,
            {"type",
                "contractVersion",
                "source",
                "encoding",
                "cellCount",
                "payloadSha256",
                "payload"});
        if (Y::text(root, "type") != "arp.cloud.cooked"
            || Y::uint32(root, "contractVersion") != assets::cloud::kCloudAssetVersion
            || Y::text(root, "encoding") != "base64-float32-le-float4")
            return Result::failure("CLOUD-COOKED-VERSION");
        assets::cloud::CloudAsset asset;
        const auto source = Y::serialize(Y::required(root, "source"));
        const auto parsed =
            assets::cloud::CloudAssetCooker::parse(source, maxDocumentBytes, asset.source);
        if (!parsed.success)
            return parsed;
        std::uint32_t count = 0;
        const auto planned = assets::cloud::CloudAssetCooker::plan(asset.source, count);
        if (!planned.success)
            return planned;
        if (Y::uint32(root, "cellCount") != count)
            return Result::failure("CLOUD-COOKED-PAYLOAD-SIZE");
        const auto bytes = std::uint64_t(count) * sizeof(core::Vec4);
        if (bytes > maxPayloadBytes)
            return Result::failure("CLOUD-COOKED-PAYLOAD-BUDGET");
        const auto encoded = Y::text(root, "payload");
        if (encoded.size() != 4u * ((bytes + 2u) / 3u))
            return Result::failure("CLOUD-COOKED-PAYLOAD-SIZE");
        core::ByteBuffer payload;
        if (!core::Base64::decode(encoded, payload) || payload.size() != bytes)
            return Result::failure("CLOUD-COOKED-BASE64");
        if (!core::Sha256::matchesHexDigest(view(payload), Y::text(root, "payloadSha256")))
            return Result::failure("CLOUD-COOKED-PAYLOAD-HASH");
        W words(payload.data(), payload.size(), W::Width::Uint32, W::ByteOrder::LittleEndian);
        asset.cells.resize(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            float fields[4]{};
            for (std::uint32_t j = 0; j < 4; ++j) {
                std::uint32_t bits = 0;
                if (!words.read(std::uint64_t(i) * 4u + j, bits))
                    return Result::failure("CLOUD-COOKED-PAYLOAD-SIZE");
                std::memcpy(fields + j, &bits, sizeof(bits));
            }
            asset.cells[i] = {fields[0], fields[1], fields[2], fields[3]};
        }
        const auto valid = assets::cloud::CloudAssetCooker::validate(asset);
        if (valid.success)
            output = std::move(asset);
        return valid;
    } catch (const core::YamlContractError&) {
        return Result::failure("CLOUD-COOKED-YAML");
    } catch (const std::exception&) {
        return Result::failure("CLOUD-COOKED-YAML");
    }
}

} // namespace arp::resource
