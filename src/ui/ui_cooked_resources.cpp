#include "ui/ui_cooked_resources.h"

#include "core/encoding/binary_stream.h"
#include "core/io/filesystem_path.h"
#include "scene/scene.h"
#include "ui/ui_atlas_image_resource.h"
#include "ui/ui_atlas_metadata_resource.h"
#include "ui/ui_document_resource.h"
#include "ui/ui_font_set_resource.h"
#include "ui/ui_font_source.h"
#include "ui/ui_theme_resource.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <type_traits>

namespace arp::ui {
namespace {
void require(bool ok, const char* error)
{
    if (!ok)
        throw std::runtime_error(error);
}

// A versioned field traversal; IDs, floats and enums have explicit wire widths.
// No native object storage, pointers, handles or padding enter the payload.
template <class A> void fields(A& a, LayoutRect& v)
{
    a(v.x, v.y, v.width, v.height);
}

template <class A> void fields(A& a, UiPoint& v)
{
    a(v.x, v.y);
}

template <class A> void fields(A& a, UiBoxEdges& v)
{
    a(v.left, v.top, v.right, v.bottom);
}

template <class A> void fields(A& a, UiStackLayout& v)
{
    a(v.direction, v.spacing, v.padding);
}

template <class A> void fields(A& a, UiNineSliceBorder& v)
{
    a(v.left, v.top, v.right, v.bottom);
}

template <class A> void fields(A& a, UiControlColorValue& v)
{
    a(v.r, v.g, v.b, v.a);
}

template <class A> void fields(A& a, UiDocumentCanvasMetadata& v)
{
    a(v.canvasId,
        v.contractVersion,
        v.width,
        v.height,
        v.hasExplicitSize,
        v.designDpi,
        v.hasExplicitDpi);
}

template <class A> void fields(A& a, UiDocumentResourceBinding& v)
{
    a(v.role, v.resourceId, v.required);
}

template <class A> void fields(A& a, UiDocumentPsdLayerBinding& v)
{
    a(v.sourceLayerId);
}

template <class A> void fields(A& a, UiDocumentNodeDesc& v)
{
    a(v.nodeId,
        v.parentNodeId,
        v.sourceId,
        v.controlType,
        v.maxTextBytes,
        v.layout,
        v.layoutMode,
        v.stackLayout,
        v.layoutParticipation,
        v.styleId,
        v.fontToken,
        v.text,
        v.stateId,
        v.eventRole,
        v.resourceBindings,
        v.psdLayerBinding);
}

template <class A> void fields(A& a, UiDocumentTextureEntry& v)
{
    a(v.resourceId,
        v.sourceLayerId,
        v.atlasId,
        v.sourceRect,
        v.pivot,
        v.nineSlice,
        v.alphaMode,
        v.srgb,
        v.hasExplicitSrgb,
        v.mipIntent,
        v.compressionIntent,
        v.samplerIntent);
}

template <class A> void fields(A& a, UiDocumentDesc& v)
{
    a(v.canvas, v.nodes, v.textures);
}

template <class A> void fields(A& a, UiAtlasSliceMetadata& v)
{
    a(v.sliceId, v.x, v.y, v.width, v.height);
}

template <class A> void fields(A& a, UiThemeTokenValue& v)
{
    a(v.kind, v.color, v.text);
}

template <class A> void fields(A& a, UiThemeToken& v)
{
    a(v.id, v.value);
}

template <class A> void fields(A& a, UiFontFaceDesc& v)
{
    a(v.tokenId,
        v.family,
        v.sourcePath,
        v.fallbackChain,
        v.supportedScripts,
        v.charset,
        v.sizePx,
        v.weight,
        v.lineHeightPx,
        v.renderMode);
    a.font(v);
}

template <class A> void fields(A& a, UiAtlasMetadataResourceData& v)
{
    a(v.atlasId,
        v.resourceId,
        v.sourcePath,
        v.width,
        v.height,
        v.srgb,
        v.slices,
        v.fontFaces,
        v.glyphPolicy,
        v.pagePolicy);
}

template <class A> void fields(A& a, UiAtlasImageResourceData& v)
{
    a(v.atlasResourceId, v.sourcePath, v.width, v.height, v.srgb, v.rgba8);
}

template <class A> void fields(A& a, UiFontSetResourceData& v)
{
    a(v.fonts, v.glyphPolicy, v.pagePolicy);
}

template <class A> void fields(A& a, UiThemeResourceData& v)
{
    a(v.theme,
        v.fonts,
        v.styleColors,
        v.styleFonts,
        v.atlasResourceId,
        v.atlasMetadataAlias,
        v.atlasSourcePath,
        v.atlasWidth,
        v.atlasHeight,
        v.atlasSrgb);
}

template <bool Reading> class UiArchive {
public:
    explicit UiArchive(std::size_t budget, std::filesystem::path sourceRoot = {})
        : writer_(budget), reader_({}), budget_(budget), sourceRoot_(std::move(sourceRoot))
    {}

    UiArchive(core::ArrayView<const std::uint8_t> input, std::size_t budget)
        : writer_(budget), reader_(input), budget_(budget)
    {
        require(input.size() <= budget, "UI-COOKED-BYTE-BUDGET");
    }

    template <class... T> void operator()(T&... v) { (field(v), ...); }

    template <class T> void field(T& v)
    {
        charge(1);
        if constexpr (std::is_same_v<T, bool>) {
            std::uint8_t n = v;
            integer(n);
            require(n <= 1, "UI-COOKED-BOOL");
            v = n != 0;
        } else if constexpr (std::is_enum_v<T>) {
            std::uint32_t n = static_cast<std::uint32_t>(v);
            integer(n);
            v = static_cast<T>(n);
        } else if constexpr (std::is_integral_v<T>)
            integer(v);
        else if constexpr (std::is_floating_point_v<T>) {
            static_assert(sizeof(T) == 4);
            std::uint32_t bits;
            std::memcpy(&bits, &v, 4);
            integer(bits);
            std::memcpy(&v, &bits, 4);
            require(std::isfinite(v), "UI-COOKED-NONFINITE");
        } else
            fields(*this, v);
    }

    void field(core::pmr::String& v)
    {
        if constexpr (Reading) {
            v = reader_.text(budget_);
            charge(v.size());
        } else
            writer_.text(v);
    }

    void field(std::filesystem::path& v)
    {
        auto path = v;
        if constexpr (!Reading) {
            if (path.is_absolute()) {
                require(!sourceRoot_.empty(), "UI-COOKED-SOURCE-ROOT-REQUIRED");
                path = path.lexically_relative(sourceRoot_);
                require(!path.empty() && *path.begin() != "..", "UI-COOKED-SOURCE-ESCAPE");
            }
        }
        auto text = core::FilePath::utf8(path);
        field(text);
        if constexpr (Reading)
            v = std::filesystem::u8path(text.begin(), text.end());
    }

    void field(UiDocumentNodeId& v)
    {
        auto id = v.value();
        field(id);
        v = UiDocumentNodeId::fromValue(id);
    }

    template <class T> void field(core::pmr::Vector<T>& v)
    {
        std::uint64_t size = v.size();
        integer(size);
        require(size <= 1048576 && size <= budget_ / std::max<std::size_t>(1, sizeof(T)),
            "UI-COOKED-COLLECTION-BUDGET");
        if constexpr (Reading) {
            charge(static_cast<std::size_t>(size) * sizeof(T));
            v.resize(static_cast<std::size_t>(size));
        }
        for (auto& item : v)
            field(item);
    }

    void field(core::pmr::Vector<std::uint8_t>& v)
    {
        std::uint64_t size = v.size();
        integer(size);
        require(size <= budget_, "UI-COOKED-BYTE-BUDGET");
        if constexpr (Reading) {
            charge(static_cast<std::size_t>(size));
            auto bytes = reader_.raw(size);
            v.assign(bytes.begin(), bytes.end());
        } else
            writer_.raw({v.data(), v.size()});
    }

    template <class T> void field(core::pmr::UnorderedMap<core::pmr::String, T>& map)
    {
        std::uint64_t size = map.size();
        integer(size);
        require(size <= 65536, "UI-COOKED-MAP-BUDGET");
        if constexpr (Reading) {
            core::pmr::String previous{&core::defaultCpuMemoryResource()};
            for (std::uint64_t i = 0; i < size; ++i) {
                core::pmr::String key{&core::defaultCpuMemoryResource()};
                T value;
                field(key);
                field(value);
                require(i == 0 || previous < key, "UI-COOKED-MAP-ORDER");
                previous = key;
                require(map.emplace(std::move(key), std::move(value)).second,
                    "UI-COOKED-MAP-DUPLICATE");
            }
        } else {
            core::pmr::Vector<core::pmr::String> keys{&core::defaultCpuMemoryResource()};
            for (const auto& item : map)
                keys.push_back(item.first);
            std::sort(keys.begin(), keys.end());
            for (auto& key : keys) {
                field(key);
                field(map.at(key));
            }
        }
    }

    void field(UiTheme& v)
    {
        auto id = v.themeId();
        core::pmr::Vector<UiThemeToken> tokens{&core::defaultCpuMemoryResource()};
        if constexpr (!Reading)
            tokens = v.tokens();
        field(id);
        field(tokens);
        if constexpr (Reading) {
            v = UiTheme(id);
            for (auto& token : tokens)
                v.addToken(std::move(token));
        }
    }

    void font(UiFontFaceDesc& v)
    {
        bool supplied = bool(v.source);
        field(supplied);
        if (!supplied)
            return; // Atlas metadata contains font declarations; the font set owns bytes.
        core::pmr::Vector<std::uint8_t> bytes{&core::defaultCpuMemoryResource()};
        if constexpr (!Reading)
            bytes.assign(v.source->bytes().begin(), v.source->bytes().end());
        field(bytes);
        if constexpr (Reading) {
            UiFontSourceDesc desc;
            desc.path = core::FilePath::utf8(v.sourcePath);
            desc.maxBytes = bytes.size();
            core::pmr::String error{&core::defaultCpuMemoryResource()};
            v.source = UiFontSource::create(std::move(desc), std::move(bytes), error);
            require(bool(v.source), error.c_str());
            v.maxSourceBytes = v.source->bytes().size();
        }
    }

    void finish()
    {
        if constexpr (Reading)
            require(!reader_.remaining(), "UI-COOKED-TRAILING");
    }

    resource::CookedBytes take() { return writer_.take(); }

private:
    template <class T> void integer(T& v)
    {
        static_assert(std::is_integral_v<T> && sizeof(T) <= 8);
        if constexpr (Reading)
            v = static_cast<T>(reader_.integer(sizeof(T)));
        else
            writer_.integer(static_cast<std::uint64_t>(v), sizeof(T));
    }

    void charge(std::size_t bytes)
    {
        require(bytes <= budget_ - used_, "UI-COOKED-DECODED-BUDGET");
        used_ += bytes;
    }

    core::encoding::BinaryWriter writer_;
    core::encoding::BinaryReader reader_;
    std::size_t budget_, used_ = 0;
    std::filesystem::path sourceRoot_;
};

// UiDocumentResource stores derived draw records/bindings; rebuild them from its
// validated typed document using the same runtime/mounter as the authoring loader.
resource::CookedBytes writeDocument(const UiDocumentResourceData& data, std::size_t budget)
{
    UiDocumentDesc document;
    document.canvas = data.document.canvas();
    document.nodes = data.document.nodes();
    document.textures = data.document.textures();
    UiArchive<false> archive(budget);
    auto contract = data.documentContract;
    auto source = data.sourcePath.filename();
    archive(contract, source, document);
    return archive.take();
}

void readDocument(
    UiDocumentResourceData& data, core::ArrayView<const std::uint8_t> input, std::size_t budget)
{
    UiArchive<true> archive(input, budget);
    UiDocumentDesc document;
    archive(data.documentContract, data.sourcePath, document);
    archive.finish();
    require(
        data.documentContract == contracts::kArpuiDocumentContract, "UI-COOKED-DOCUMENT-CONTRACT");
    data.document = UiDocument(std::move(document));
    auto loaded = instantiateUiDocument(data.document);
    require(loaded.ok(), loaded.diagnostic().message.c_str());
    const auto layout = loaded.runtime().canvas().resolveLayout();
    require(layout.ok(), layout.diagnostic().message.c_str());
    scene::Scene runtimeScene(core::pmr::String("CookedUiDocument"));
    auto& object = runtimeScene.createObject(core::pmr::String("document"));
    require(runtimeScene.reparent(object.id(), runtimeScene.rootId()).ok(), "UI-COOKED-SCENE");
    DocumentComponentDesc desc;
    desc.documentSource = core::FilePath::utf8(data.sourcePath);
    desc.resourceId = "ui.document.resource";
    desc.inputRoute = "ui.input";
    desc.renderRoute = "ui.render";
    auto& component = object.addComponent<DocumentComponent>(desc);
    auto mounted = DocumentComponentMounter().mount(component, loaded);
    require(mounted.ok(), mounted.diagnostic().message.c_str());
    data.drawPlan = component.mountedDrawPlan();
    for (const auto& node : data.document.nodes()) {
        if (node.sourceId.empty())
            continue;
        const auto id = loaded.widgetIdForSourceNodeId(node.sourceId);
        const auto* resolved = layout.findWidget(id);
        require(resolved, "UI-COOKED-BINDING");
        UiDocumentRuntimeBinding b;
        b.sourceId = node.sourceId;
        b.widgetId = id;
        b.rect = resolved->rect;
        b.eventRole = node.eventRole;
        b.text = node.text;
        b.order = resolved->order;
        b.visible = resolved->visibleInHierarchy;
        b.enabled = resolved->enabledInHierarchy;
        data.bindings.push_back(std::move(b));
    }
}

template <class T, class Data> class UiCodec final : public resource::CookedResourceCodec {
public:
    explicit UiCodec(const char* type) : type_(type)
    {
        static std::once_flag registered;
        std::call_once(registered, [this] {
            reflection::registration::class_<T>(type_).template construct<>().accessor_property(
                "cookedMetadataVersion",
                [](T*) {
                    return std::uint32_t{1};
                },
                [](T*, std::uint32_t value) {
                    require(value == 1, "UI-COOKED-METADATA-RECOOK");
                },
                {},
                static_cast<std::uint8_t>(reflection::PropertyHint::Serialize));
        });
    }

    resource::NameView resourceType() const noexcept override { return type_; }

    std::uint64_t schema() const override
    {
        core::StableHash64 h;
        h.appendString("arp.ui.cooked-payload/v2");
        h.appendString(type_);
        h.appendU64(reflection::serialization_schema(reflection::type_of<T>()));
        return h.value();
    }

    reflection::ReflectionBinaryResult writeMetadata(const resource::ResourceBase& value,
        reflection::SerializationOptions options) const override
    {
        return reflection::serialize_to_binary(
            reflection::object(dynamic_cast<const T&>(value)), options);
    }

    core::UniquePtr<resource::ResourceBase> descriptor(
        core::SharedPtr<const resource::CookedResourceSpec> spec) const override
    {
        return core::makeUnique<resource::CookedResourceCandidate<T>>(std::move(spec));
    }

    resource::CookedBytes cookPayload(const resource::ResourceBase& base,
        const std::filesystem::path& sourceRoot,
        const resource::CookedResourceBudget& budget) const override
    {
        const auto& value = dynamic_cast<const T&>(base);
        require(bool(value.data), "UI-COOKED-MISSING-TYPED-DATA");
        if constexpr (std::is_same_v<T, UiDocumentResource>)
            return writeDocument(*value.data, budget.maxPayloadBytes);
        else {
            auto data = *value.data;
            UiArchive<false> a(budget.maxPayloadBytes, sourceRoot);
            a(data);
            return a.take();
        }
    }

    resource::CookedResourceResult preparePayload(resource::ResourceBase& base,
        core::ArrayView<const std::uint8_t> bytes,
        std::size_t budget) const override
    {
        try {
            auto data = core::makeShared<Data>();
            if constexpr (std::is_same_v<T, UiDocumentResource>)
                readDocument(*data, bytes, budget);
            else {
                UiArchive<true> a(bytes, budget);
                a(*data);
                a.finish();
            }
            if constexpr (std::is_same_v<T, UiAtlasImageResource>)
                require(data->width && data->height
                        && std::uint64_t(data->width) * data->height * 4 == data->rgba8.size(),
                    "UI-COOKED-IMAGE-SIZE");
            dynamic_cast<T&>(base).data = std::move(data);
            return {true};
        } catch (const std::exception& e) {
            return {false, e.what()};
        }
    }

private:
    const char* type_;
};
} // namespace

core::SharedPtr<const resource::CookedResourceCodecRegistry> makeUiCookedResourceCodecs()
{
    auto r = core::makeShared<resource::CookedResourceCodecRegistry>(5);
#define UI(T, D, N) require(r->add(core::makeShared<UiCodec<T, D>>(N)), "UI-COOKED-REGISTRY")
    UI(UiDocumentResource, UiDocumentResourceData, kUiDocumentResourceType);
    UI(UiAtlasMetadataResource, UiAtlasMetadataResourceData, kUiAtlasMetadataResourceType);
    UI(UiAtlasImageResource, UiAtlasImageResourceData, kUiAtlasImageResourceType);
    UI(UiFontSetResource, UiFontSetResourceData, kUiFontSetResourceType);
    UI(UiThemeResource, UiThemeResourceData, kUiThemeResourceType);
#undef UI
    require(r->freeze(), "UI-COOKED-REGISTRY");
    return r;
}
} // namespace arp::ui
