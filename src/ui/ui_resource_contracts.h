#pragma once

#include "resource/resource_contract.h"

#include <string_view>

namespace arp::ui::contracts {

inline constexpr resource::ResourceContractDescriptor kTheme{
    "UiThemeResource",
    "ui.theme/v1",
};
inline constexpr resource::ResourceContractDescriptor kAtlasMetadata{
    "UiAtlasMetadataResource",
    "ui.atlas/v2",
};
inline constexpr resource::ResourceContractDescriptor kAtlasImage{
    "UiAtlasImageResource",
    "ui.atlas.image/v1",
};
inline constexpr resource::ResourceContractDescriptor kAtlasTexture{
    "UiAtlasTextureResource",
    "ui.atlas.texture/v1",
};
inline constexpr resource::ResourceContractDescriptor kFontSet{
    "UiFontSetResource",
    "ui.font-set/v1",
};
inline constexpr resource::ResourceContractDescriptor kDocument{
    "UiDocumentResource",
    "ui.document.resource/v1",
};
inline constexpr std::string_view kArpuiDocumentContract = "ui.document/v1";
inline constexpr resource::ResourceContractDescriptor kStartupBundle{
    "UiStartupBundleResource",
    "ui.startup-bundle/v1",
};
inline constexpr resource::ResourceContractDescriptor kPikaVisibleStartup{
    "PikaVisibleUiStartupResource",
    "pika.visible.ui.startup/v1",
};
inline constexpr resource::ResourceContractDescriptor kPikaVisibleGraph{
    "PikaVisibleUiResourceGraphResource",
    "pika.visible.ui.resource-graph/v1",
};

inline constexpr std::string_view kUsageTheme = "ui.theme";
inline constexpr std::string_view kUsageAtlasMetadata = "ui.atlas.metadata";
inline constexpr std::string_view kUsageAtlasMetadataFonts = "ui.atlas.metadata.fonts";
inline constexpr std::string_view kUsageAtlasImage = "ui.atlas.image";
inline constexpr std::string_view kUsageAtlasTexture = "ui.atlas.texture";
inline constexpr std::string_view kUsageFontSet = "ui.font-set";
inline constexpr std::string_view kUsageDocument = "ui.document";

} // namespace arp::ui::contracts
