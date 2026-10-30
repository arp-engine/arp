#pragma once

#include "core/io/filesystem_path.h"
#include "ui/ui_font.h"
#include "ui/ui_font_source.h"

#include <cassert>

namespace arp::tests {

// These are explicit cold test inputs; production renderer code never discovers
// a font or opens a host path. Native and Node consume the same vendored font.
inline ui::UiFontFaceDesc preparedUiFontFace(const char* token)
{
    ui::UiFontFaceDesc face;
    face.tokenId = token;
    face.family = "Droid Sans";
    face.sourcePath = ARP_FONT_IO_FIXTURE_PATH;
    face.maxSourceBytes = 2u * 1024u * 1024u;
    face.sizePx = 16;
    face.lineHeightPx = 22;
    core::pmr::String diagnostic;
    face.source = ui::acquireUiFontSource(face, diagnostic);
    assert(face.source && diagnostic.empty());
    return face;
}

inline core::pmr::Vector<ui::UiFontFaceDesc> preparedUiFontFaces(const char* token)
{
    core::pmr::Vector<ui::UiFontFaceDesc> faces;
    faces.push_back(preparedUiFontFace(token));
    return faces;
}

inline void prepareHostUiFontFaces(core::pmr::Vector<ui::UiFontFaceDesc>& faces)
{
    for (auto& face : faces) {
        core::pmr::String diagnostic;
        face.source = ui::acquireUiFontSource(face, diagnostic);
        assert(face.source && diagnostic.empty());
    }
}

inline void setCorruptUiFontSource(ui::UiFontFaceDesc& face)
{
    ui::UiFontSourceDesc source;
    source.path = core::FilePath::utf8(face.sourcePath);
    source.maxBytes = face.maxSourceBytes;
    core::pmr::Vector<std::uint8_t> bytes{1, 2, 3};
    core::pmr::String diagnostic;
    face.source = ui::UiFontSource::create(std::move(source), std::move(bytes), diagnostic);
    assert(face.source && diagnostic.empty());
}

} // namespace arp::tests
