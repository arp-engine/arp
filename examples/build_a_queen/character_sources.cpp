#include "character_sources.h"

#include "core/encoding/yaml_contract.h"
#include "core/io/file_read_contract.h"
#include "core/io/filesystem_path.h"
#include "core/text/text_decoder.h"

#include <algorithm>
#include <array>
#include <optional>

namespace arp::build_a_queen {
namespace {
using namespace core::io;

bool validBinding(const FileReadRequest& request, const FileReadResult& result, std::size_t cap)
{
    if (FileReadContract::validate(request).diagnostic || request.contractVersion != 1
        || request.pathScope != FilePathScope::MountedResource || !request.mount
        || !core::FilePath::isSafeRelativePath(request.path)
        || request.range.mode != FileReadRangeMode::WholeFile
        || request.partialPolicy != FilePartialReadPolicy::DenyPartial || request.maxBytes > cap
        || result.status != FileIoStatus::Success || result.bytes.empty()
        || result.bytes.size() > request.maxBytes || !result.version.valid()
        || (request.expectedVersion.kind != FileVersionKind::None
            && request.expectedVersion != result.version))
        return false;
    if (result.version.kind == FileVersionKind::ContentSha256) {
        const auto hash = core::Sha256::digest(
            {reinterpret_cast<const char*>(result.bytes.data()), result.bytes.size()});
        if (!std::equal(hash.begin(), hash.end(), result.version.digest.begin()))
            return false;
    }
    return true;
}
} // namespace

struct CharacterSources::State {
    struct Character {
        core::pmr::String name{&core::defaultCpuMemoryResource()};
        std::optional<core::io::FileReadResult> source;
    };

    State(core::io::FileReadResult raw, core::pmr::String decoded)
        : manifest(std::move(raw)), text(std::move(decoded))
    {}

    core::io::FileReadResult manifest;
    core::pmr::String text;
    core::pmr::String root{&core::defaultCpuMemoryResource()};
    core::PoolHandle mount;
    std::array<Character, characterCount> characters;
};

CharacterSources::CharacterSources() = default;
CharacterSources::~CharacterSources() = default;

bool CharacterSources::reject(const char* diagnostic) noexcept
{
    diagnostic_ = diagnostic;
    return false;
}

bool CharacterSources::setManifest(
    const core::io::FileReadRequest& request, core::io::FileReadResult result)
{
    if (state_)
        return reject("ARP-QUEEN-SOURCE-MANIFEST-ALREADY-BOUND");
    try {
        if (!validBinding(request, result, maxManifestBytes))
            return reject("ARP-QUEEN-SOURCE-MANIFEST-BINDING");
        const auto separator = request.path.rfind('/');
        const auto name = std::string_view(request.path)
                              .substr(separator == core::pmr::String::npos ? 0 : separator + 1);
        if (name != manifestFile)
            return reject("ARP-QUEEN-SOURCE-MANIFEST-NAME");
        core::TextDecodeDesc text;
        text.maxOutputBytes = maxManifestBytes;
        text.bom = core::TextBomPolicy::Strip;
        auto decoded = core::TextDecoder::decode(
            {reinterpret_cast<const char*>(result.bytes.data()), result.bytes.size()}, text);
        if (decoded.status != core::TextDecodeStatus::Success)
            return reject(decoded.diagnosticId);
        using Y = core::YamlContract;
        const auto root = Y::parseSingleDocument(decoded.text);
        Y::onlyKeys(root, {"type", "contractVersion", "source", "characters"});
        if (Y::text(root, "type") != "arp.build-a-queen-character-manifest"
            || Y::uint32(root, "contractVersion") != 1)
            return reject("ARP-QUEEN-SOURCE-MANIFEST-VERSION");
        const auto& characters = Y::sequence(root, "characters");
        if (characters.size() != characterCount)
            return reject("ARP-QUEEN-SOURCE-CHARACTER-COUNT");
        auto candidate = core::makeUnique<State>(std::move(result), std::move(decoded.text));
        candidate->mount = request.mount;
        if (separator != core::pmr::String::npos)
            candidate->root.assign(request.path.data(), separator + 1);
        for (std::size_t index = 0; index < characterCount; ++index) {
            const auto& character = characters[index];
            Y::onlyKeys(character, {"id", "file", "height", "yawDegrees", "materials"});
            if (Y::text(character, "id") != (index == 0 ? "player" : "rival"))
                return reject("ARP-QUEEN-SOURCE-CHARACTER-IDENTITY");
            auto file = Y::text(character, "file");
            if (!core::FilePath::isSafeRelativePath(file)
                || file.find('\0') != core::pmr::String::npos || core::Utf8Decoder::validate(file))
                return reject("ARP-QUEEN-SOURCE-CHARACTER-PATH");
            if (!core::FilePath::isSafeRelativePath(candidate->root + file))
                return reject("ARP-QUEEN-SOURCE-CHARACTER-PATH");
            candidate->characters[index].name = std::move(file);
        }
        if (candidate->characters[0].name == candidate->characters[1].name)
            return reject("ARP-QUEEN-SOURCE-CHARACTER-DUPLICATE");
        state_ = std::move(candidate);
        diagnostic_ = nullptr;
        return true;
    } catch (const core::YamlContractError&) {
        return reject("ARP-QUEEN-SOURCE-MANIFEST-SCHEMA");
    } catch (const fkyaml::exception&) {
        return reject("ARP-QUEEN-SOURCE-MANIFEST-SCHEMA");
    } catch (const std::bad_alloc&) {
        return reject("ARP-QUEEN-SOURCE-ALLOCATION");
    }
}

bool CharacterSources::addCharacter(
    const core::io::FileReadRequest& request, core::io::FileReadResult result)
{
    if (!state_)
        return reject("ARP-QUEEN-SOURCE-MANIFEST-REQUIRED");
    try {
        if (!(request.mount == state_->mount) || !validBinding(request, result, maxCharacterBytes))
            return reject("ARP-QUEEN-SOURCE-CHARACTER-BINDING");
        for (auto& character : state_->characters) {
            if (request.path != state_->root + character.name)
                continue;
            if (character.source)
                return reject("ARP-QUEEN-SOURCE-CHARACTER-ALREADY-BOUND");
            character.source.emplace(std::move(result));
            diagnostic_ = nullptr;
            return true;
        }
        return reject("ARP-QUEEN-SOURCE-CHARACTER-UNDECLARED");
    } catch (const std::bad_alloc&) {
        return reject("ARP-QUEEN-SOURCE-ALLOCATION");
    }
}

bool CharacterSources::ready() const noexcept
{
    return state_ && state_->characters[0].source && state_->characters[1].source;
}

std::string_view CharacterSources::manifestText() const noexcept
{
    return state_ ? std::string_view(state_->text) : std::string_view();
}

core::pmr::String CharacterSources::characterPath(std::size_t index) const
{
    return state_ && index < characterCount ? state_->root + state_->characters[index].name
                                            : core::pmr::String(&core::defaultCpuMemoryResource());
}

core::ArrayView<const std::uint8_t> CharacterSources::characterBytes(
    std::string_view name) const noexcept
{
    if (state_)
        for (const auto& character : state_->characters)
            if (character.name == name && character.source)
                return {character.source->bytes.data(), character.source->bytes.size()};
    return {};
}

const core::io::FileReadResult* CharacterSources::manifestSource() const noexcept
{
    return state_ ? &state_->manifest : nullptr;
}

const core::io::FileReadResult* CharacterSources::characterSource(std::size_t index) const noexcept
{
    return state_ && index < characterCount && state_->characters[index].source
        ? &*state_->characters[index].source
        : nullptr;
}

} // namespace arp::build_a_queen
