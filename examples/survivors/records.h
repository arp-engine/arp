#pragma once
#include "core/encoding/yaml_contract.h"

namespace arp::survivors {
struct Record {
    std::uint32_t bestKills = 0, bestSeconds = 0, wins = 0, unlockPoints = 0;
    double volume = .6;
};

struct RecordSchema {
    static const char* type() { return "arp.survivors-record"; }

    static std::uint32_t version() { return 1; }

    static bool validate(const core::YamlContract::Node& n, core::pmr::String& diagnostic)
    {
        using Y = core::YamlContract;
        for (const char* field : {"bestKills", "bestSeconds", "wins", "unlockPoints"})
            (void)Y::uint32(n, field);
        const auto volume = Y::number(n, "volume");
        if (volume < 0 || volume > 1) {
            diagnostic = "ARP-SURVIVORS-RECORD-VOLUME";
            return false;
        }
        return true;
    }

    static Record decode(const core::pmr::Vector<std::uint8_t>& bytes)
    {
        using Y = core::YamlContract;
        const auto n = Y::Node::deserialize(
            core::pmr::String(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        return decodeNode(n);
    }

    // Validate before publishing a loaded or replacement record. The same
    // schema remains usable by the existing offline VersionedYamlStore tests.
    static bool prepare(
        const core::pmr::String& text, Record& output, core::pmr::String& diagnostic)
    {
        using Y = core::YamlContract;
        try {
            const auto n = Y::parseSingleDocument(text);
            if (Y::text(n, "type") != type() || Y::uint32(n, "contractVersion") != version()) {
                diagnostic = "ARP-SAVE-UNSUPPORTED-CONTRACT";
                return false;
            }
            if (!validate(n, diagnostic))
                return false;
            output = decodeNode(n);
            return true;
        } catch (const std::bad_alloc&) {
            throw;
        } catch (...) {
            diagnostic = "ARP-SAVE-CORRUPT-YAML";
            return false;
        }
    }

    static Record decodeNode(const core::YamlContract::Node& n)
    {
        using Y = core::YamlContract;
        return {Y::uint32(n, "bestKills"),
            Y::uint32(n, "bestSeconds"),
            Y::uint32(n, "wins"),
            Y::uint32(n, "unlockPoints"),
            Y::number(n, "volume")};
    }

    static core::pmr::String encode(const Record& record)
    {
        auto n = core::YamlContract::Node::mapping();
        n["type"] = type();
        n["contractVersion"] = version();
        n["bestKills"] = record.bestKills;
        n["bestSeconds"] = record.bestSeconds;
        n["wins"] = record.wins;
        n["unlockPoints"] = record.unlockPoints;
        n["volume"] = record.volume;
        const auto yaml = core::YamlContract::Node::serialize(n);
        return core::pmr::String(yaml.data(), yaml.size());
    }
};
} // namespace arp::survivors
