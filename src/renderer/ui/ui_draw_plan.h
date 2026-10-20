#pragma once

#include "core/cpu_allocator.h"
#include "rhi/gpu_common.h"
#include "rhi/semantics/semantic_report_backend_admission.h"
#include "ui/ui.h"

namespace arp::renderer::ui {

enum class UiDrawCommandKind {
    Box,
    Text,
    Image,
    GlyphQuad,
};

enum class UiTextureSource {
    Atlas,
    External,
};

struct UiDrawCommand {
    UiDrawCommandKind kind = UiDrawCommandKind::Box;
    arp::ui::WidgetId widgetId;
    arp::ui::LayoutRect rect;
    arp::ui::LayoutRect clipRect;
    arp::ui::LayoutRect textureSourceRect;
    bool hasTextureSourceRect = false;
    float color[4]{0.28f, 0.28f, 0.28f, 0.88f};
    arp::core::pmr::String text{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String textureResourceId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String styleId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String fontToken{&arp::core::defaultCpuMemoryResource()};
    std::uint32_t atlasPageIndex = 0u;
    bool hasAtlasPageIndex = false;
    /// True when this GlyphQuad samples an SDF (Signed Distance Field) atlas
    /// entry. The fragment shader uses this to reconstruct sharp edges via
    /// smoothstep on the sampled distance value. Propagated to flags[1] in
    /// the push constants for the overlay shader.
    bool sdfMode = false;
    std::uint64_t sortKey = 0;
    UiTextureSource textureSource = UiTextureSource::Atlas;
};

struct UiDrawStyleColor {
    arp::core::pmr::String styleId{&arp::core::defaultCpuMemoryResource()};
    float color[4]{0.28f, 0.28f, 0.28f, 0.88f};
};

struct UiDrawStyleFont {
    arp::core::pmr::String styleId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String fontToken{&arp::core::defaultCpuMemoryResource()};
};

struct UiDrawPlanLoweringDesc {
    rhi::Extent2D extent;
    float atlasWidth = 0.0f;
    float atlasHeight = 0.0f;
    arp::core::pmr::Vector<UiDrawStyleColor> styleColors{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::Vector<UiDrawStyleFont> styleFonts{&arp::core::defaultCpuMemoryResource()};
};

class UiDrawPlanLowerResult final {
public:
    static UiDrawPlanLowerResult success(arp::core::pmr::Vector<UiDrawCommand> commands)
    {
        UiDrawPlanLowerResult result;
        result.commands_ = std::move(commands);
        return result;
    }

    static UiDrawPlanLowerResult failure(arp::core::pmr::String diagnostics)
    {
        UiDrawPlanLowerResult result;
        result.diagnostics_ = std::move(diagnostics);
        result.ok_ = false;
        return result;
    }

    bool ok() const { return ok_; }

    const arp::core::pmr::String& diagnostics() const { return diagnostics_; }

    const arp::core::pmr::Vector<UiDrawCommand>& commands() const { return commands_; }

    bool exposesNativeBackendHandles() const { return false; }

private:
    bool ok_ = true;
    arp::core::pmr::String diagnostics_{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::Vector<UiDrawCommand> commands_{&arp::core::defaultCpuMemoryResource()};
};

class UiDrawPlanLowerer final {
public:
    UiDrawPlanLowerResult lower(const arp::ui::DrawPlan& plan, rhi::Extent2D extent) const
    {
        UiDrawPlanLoweringDesc desc;
        desc.extent = extent;
        return lower(plan, desc);
    }

    UiDrawPlanLowerResult lower(
        const arp::ui::DrawPlan& plan, const UiDrawPlanLoweringDesc& desc) const
    {
        if (desc.extent.width == 0u || desc.extent.height == 0u) {
            return UiDrawPlanLowerResult::failure(arp::core::pmr::String(
                "UI draw lowering requires an explicit non-zero framebuffer extent.",
                &arp::core::defaultCpuMemoryResource()));
        }
        if (plan.records().empty()) {
            return UiDrawPlanLowerResult::failure(
                arp::core::pmr::String("UI draw lowering requires at least one draw record.",
                    &arp::core::defaultCpuMemoryResource()));
        }

        arp::core::pmr::Vector<UiDrawCommand> commands{&arp::core::defaultCpuMemoryResource()};
        commands.reserve(plan.records().size());
        for (const auto& record : plan.records()) {
            if (!record.rect.valid() || !record.clipRect.valid()) {
                return UiDrawPlanLowerResult::failure(
                    arp::core::pmr::String("UI draw record requires valid rect and clip rect.",
                        &arp::core::defaultCpuMemoryResource()));
            }
            if (record.kind == arp::ui::DrawRecordKind::Image && record.textureResourceId.empty()
                && !record.texture.valid()) {
                return UiDrawPlanLowerResult::failure(arp::core::pmr::String(
                    "UI image draw record requires an explicit texture binding.",
                    &arp::core::defaultCpuMemoryResource()));
            }

            // Resolve the canonical (possibly atlas-normalized) texture source rect
            // and nine-slice border values once so that both the single-command and
            // 9-slice paths use consistent numbers.
            const auto isImage = record.kind == arp::ui::DrawRecordKind::Image;
            const auto hasAtlasNormalization = desc.atlasWidth > 0.0f && desc.atlasHeight > 0.0f;

            arp::ui::LayoutRect resolvedSourceRect = record.textureSourceRect;
            float nSliceLeft = record.sliceLeft;
            float nSliceTop = record.sliceTop;
            float nSliceRight = record.sliceRight;
            float nSliceBottom = record.sliceBottom;

            if (isImage && record.hasTextureSourceRect && hasAtlasNormalization) {
                resolvedSourceRect = arp::ui::LayoutRect::fromOriginSize(
                    record.textureSourceRect.x / desc.atlasWidth,
                    record.textureSourceRect.y / desc.atlasHeight,
                    record.textureSourceRect.width / desc.atlasWidth,
                    record.textureSourceRect.height / desc.atlasHeight);
                nSliceLeft /= desc.atlasWidth;
                nSliceTop /= desc.atlasHeight;
                nSliceRight /= desc.atlasWidth;
                nSliceBottom /= desc.atlasHeight;
            }

            // If the record carries valid nine-slice borders and the output
            // rect is large enough that the 9 slices will produce distinct quads,
            // emit 9 separate draw commands instead of one stretched Image.
            const auto useNineSlice = isImage && record.hasTextureSourceRect && record.hasNineSlice
                && record.rect.width > (record.sliceLeft + record.sliceRight)
                && record.rect.height > (record.sliceTop + record.sliceBottom);

            if (useNineSlice) {
                const auto& r = record.rect;
                const auto ox = r.x, oy = r.y, ow = r.width, oh = r.height;
                const auto& uv = resolvedSourceRect;
                const auto ux = uv.x, uy = uv.y, uw = uv.width, uh = uv.height;
                const auto sl = record.sliceLeft; // still in output-pixel space
                const auto st = record.sliceTop;
                const auto sr = record.sliceRight;
                const auto sb = record.sliceBottom;
                const auto nsl = nSliceLeft, nst = nSliceTop, nsr = nSliceRight, nsb = nSliceBottom;

                struct {
                    float x, y, w, h, ux, uy, uw, uh;
                } quads[9] = {
                    // tl               top               tr
                    {ox, oy, sl, st, ux, uy, nsl, nst},
                    {ox + sl, oy, ow - sl - sr, st, ux + nsl, uy, uw - nsl - nsr, nst},
                    {ox + ow - sr, oy, sr, st, ux + uw - nsr, uy, nsr, nst},
                    // left             center            right
                    {ox, oy + st, sl, oh - st - sb, ux, uy + nst, nsl, uh - nst - nsb},
                    {ox + sl,
                        oy + st,
                        ow - sl - sr,
                        oh - st - sb,
                        ux + nsl,
                        uy + nst,
                        uw - nsl - nsr,
                        uh - nst - nsb},
                    {ox + ow - sr,
                        oy + st,
                        sr,
                        oh - st - sb,
                        ux + uw - nsr,
                        uy + nst,
                        nsr,
                        uh - nst - nsb},
                    // bl               bottom            br
                    {ox, oy + oh - sb, sl, sb, ux, uy + uh - nsb, nsl, nsb},
                    {ox + sl,
                        oy + oh - sb,
                        ow - sl - sr,
                        sb,
                        ux + nsl,
                        uy + uh - nsb,
                        uw - nsl - nsr,
                        nsb},
                    {ox + ow - sr, oy + oh - sb, sr, sb, ux + uw - nsr, uy + uh - nsb, nsr, nsb},
                };

                for (const auto& q : quads) {
                    if (q.w <= 0.0f || q.h <= 0.0f)
                        continue;
                    UiDrawCommand c;
                    c.kind = UiDrawCommandKind::Image;
                    c.widgetId = record.widgetId;
                    c.rect = arp::ui::LayoutRect::fromOriginSize(q.x, q.y, q.w, q.h);
                    c.clipRect = record.clipRect;
                    c.textureSourceRect =
                        arp::ui::LayoutRect::fromOriginSize(q.ux, q.uy, q.uw, q.uh);
                    c.hasTextureSourceRect = true;
                    c.textureResourceId = record.textureResourceId;
                    c.styleId = record.styleId;
                    c.fontToken = record.fontToken;
                    c.text = record.text;
                    c.sortKey = record.sortKey;
                    if (!copyStyleColor(record.styleId, desc, c.color)) {
                        return UiDrawPlanLowerResult::failure(arp::core::pmr::String(
                            "UI draw lowering requires an explicit style color binding for nine-slice image.",
                            &arp::core::defaultCpuMemoryResource()));
                    }
                    commands.push_back(std::move(c));
                }
                continue; // already emitted 9-slice quads, skip single-command path below
            }

            UiDrawCommand command;
            command.kind = kindFor(record.kind);
            command.widgetId = record.widgetId;
            command.rect = record.rect;
            command.clipRect = record.clipRect;
            command.textureSourceRect = resolvedSourceRect;
            command.hasTextureSourceRect = record.hasTextureSourceRect;
            // Atlas normalization for non-nine-slice Image records is already
            // baked into resolvedSourceRect above, so the original post-normalization
            // block is no longer needed here.
            if (!copyStyleColor(record.styleId, desc, command.color)) {
                return UiDrawPlanLowerResult::failure(arp::core::pmr::String(
                    "UI draw lowering requires an explicit style color binding.",
                    &arp::core::defaultCpuMemoryResource()));
            }
            if (record.kind == arp::ui::DrawRecordKind::Text) {
                if (record.styleId.empty()) {
                    return UiDrawPlanLowerResult::failure(arp::core::pmr::String(
                        "UI text draw record requires style id before font token resolution.",
                        &arp::core::defaultCpuMemoryResource()));
                }
                command.fontToken = record.fontToken;
                if (command.fontToken.empty()
                    && !copyStyleFont(record.styleId, desc, command.fontToken)) {
                    return UiDrawPlanLowerResult::failure(arp::core::pmr::String(
                        "UI text draw lowering requires an explicit font token binding.",
                        &arp::core::defaultCpuMemoryResource()));
                }
            }
            command.text = record.text;
            command.textureResourceId = record.textureResourceId;
            command.styleId = record.styleId;
            command.sortKey = record.sortKey;
            commands.push_back(std::move(command));
        }
        return UiDrawPlanLowerResult::success(std::move(commands));
    }

private:
    static bool copyStyleColor(
        const arp::core::pmr::String& styleId, const UiDrawPlanLoweringDesc& desc, float (&out)[4])
    {
        if (desc.styleColors.empty()) {
            return true;
        }
        for (const auto& styleColor : desc.styleColors) {
            if (styleColor.styleId == styleId) {
                out[0] = styleColor.color[0];
                out[1] = styleColor.color[1];
                out[2] = styleColor.color[2];
                out[3] = styleColor.color[3];
                return true;
            }
        }
        return false;
    }

    static bool copyStyleFont(const arp::core::pmr::String& styleId,
        const UiDrawPlanLoweringDesc& desc,
        arp::core::pmr::String& out)
    {
        for (const auto& styleFont : desc.styleFonts) {
            if (styleFont.styleId == styleId && !styleFont.fontToken.empty()) {
                out = styleFont.fontToken;
                return true;
            }
        }
        return false;
    }

    static UiDrawCommandKind kindFor(arp::ui::DrawRecordKind kind)
    {
        switch (kind) {
        case arp::ui::DrawRecordKind::Text:
            return UiDrawCommandKind::Text;
        case arp::ui::DrawRecordKind::Image:
            return UiDrawCommandKind::Image;
        case arp::ui::DrawRecordKind::Box:
        default:
            return UiDrawCommandKind::Box;
        }
    }
};

enum class UiAtlasPagePolicy {
    SingleTexture,
    TextureArray,
    MultiPage,
};

enum class UiAtlasPageLoweringMode {
    Unspecified,
    SingleTexture2D,
    Texture2DArrayLayer,
    DescriptorPaged2D,
};

struct UiRhiSemanticContract {
    arp::core::pmr::String pipelineLayoutName{
        "UiPipelineLayout", &arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String frameDescriptorLayoutName{
        "UiFrameSet", &arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String atlasDescriptorLayoutName{
        "UiAtlasSet", &arp::core::defaultCpuMemoryResource()};
    bool explicitResourceStates = true;
    bool backendOwnedUploadStaging = true;
    bool alphaBlendEnabled = true;
    bool depthEnabled = false;
    bool cullEnabled = false;
    bool loadOpPreservesColor = true;
    bool storeOpStoresColor = true;
    bool boundedDrawAllocation = true;
    bool explicitAtlasPageIndex = true;
    bool backendOwnedReadback = true;
    bool exposesNativeBackendHandles = false;
    UiAtlasPagePolicy pagePolicy = UiAtlasPagePolicy::SingleTexture;
    UiAtlasPageLoweringMode loweringMode = UiAtlasPageLoweringMode::SingleTexture2D;
    arp::core::pmr::String atlasPagePolicy{"singleTexture", &arp::core::defaultCpuMemoryResource()};
};

struct UiRhiBackendReport {
    rhi::Backend backend = rhi::Backend::Null;
    bool supported = false;
    arp::core::pmr::String diagnostics{&arp::core::defaultCpuMemoryResource()};
    bool exposesNativeBackendHandles = false;
    UiAtlasPagePolicy pagePolicy = UiAtlasPagePolicy::SingleTexture;
    UiAtlasPageLoweringMode loweringMode = UiAtlasPageLoweringMode::SingleTexture2D;
    bool textureArraySupported = false;
    bool multiPageBatchingSupported = false;
    bool dynamicUploadSupported = false;
    bool backendOwnedUploadStaging = true;
    bool backendOwnedReadback = true;
};

struct UiRhiBackendContractResult {
    UiRhiSemanticContract semantic;
    arp::core::pmr::Vector<UiRhiBackendReport> backendReports{
        &arp::core::defaultCpuMemoryResource()};
};

class UiRhiBackendContract final {
public:
    static constexpr std::uint32_t contractVersion = 2u;

    UiRhiBackendContractResult evaluate() const
    {
        return evaluate(UiAtlasPagePolicy::SingleTexture);
    }

    UiRhiBackendContractResult evaluate(UiAtlasPagePolicy policy) const
    {
        UiRhiBackendContractResult result;
        result.semantic.pagePolicy = policy;
        result.semantic.loweringMode = loweringModeFor(policy);
        result.semantic.atlasPagePolicy =
            arp::core::pmr::String(policyName(policy), &arp::core::defaultCpuMemoryResource());
        result.backendReports.reserve(
            rhi::semantics::SemanticReportBackendAdmission::backends.size());
        for (const auto backend : rhi::semantics::SemanticReportBackendAdmission::backends) {
            const auto admission =
                rhi::semantics::SemanticReportBackendAdmission::evaluate(backend);
            if (result.semantic.loweringMode == UiAtlasPageLoweringMode::Unspecified) {
                UiRhiBackendReport report;
                report.backend = backend;
                report.pagePolicy = policy;
                report.loweringMode = UiAtlasPageLoweringMode::Unspecified;
                report.diagnostics = "RHI-UI-SEMANTIC-REPORT-POLICY-UNKNOWN";
                result.backendReports.push_back(std::move(report));
            } else if (admission.validatorOnly) {
                result.backendReports.push_back(makeNullReport(policy));
            } else {
                result.backendReports.push_back(
                    makeDiagnosticReport(backend, policy, admission.label));
            }
        }
        return result;
    }

private:
    static const char* policyName(UiAtlasPagePolicy policy)
    {
        switch (policy) {
        case UiAtlasPagePolicy::TextureArray:
            return "textureArray";
        case UiAtlasPagePolicy::MultiPage:
            return "multiPage";
        case UiAtlasPagePolicy::SingleTexture:
            return "singleTexture";
        }
        return "unknown";
    }

    static UiAtlasPageLoweringMode loweringModeFor(UiAtlasPagePolicy policy)
    {
        switch (policy) {
        case UiAtlasPagePolicy::TextureArray:
            return UiAtlasPageLoweringMode::Texture2DArrayLayer;
        case UiAtlasPagePolicy::MultiPage:
            return UiAtlasPageLoweringMode::DescriptorPaged2D;
        case UiAtlasPagePolicy::SingleTexture:
            return UiAtlasPageLoweringMode::SingleTexture2D;
        }
        return UiAtlasPageLoweringMode::Unspecified;
    }

    static UiRhiBackendReport makeNullReport(UiAtlasPagePolicy policy)
    {
        return {
            rhi::Backend::Null,
            true,
            {},
            false,
            policy,
            loweringModeFor(policy),
            policy == UiAtlasPagePolicy::TextureArray,
            policy == UiAtlasPagePolicy::MultiPage,
            true,
            true,
            true,
        };
    }

    static UiRhiBackendReport makeDiagnosticReport(
        rhi::Backend backend, UiAtlasPagePolicy policy, const char* backendName)
    {
        arp::core::pmr::String diagnostics{backendName, &arp::core::defaultCpuMemoryResource()};
        const auto admission = rhi::semantics::SemanticReportBackendAdmission::evaluate(backend);
        if (!admission.inspectStructuralContract) {
            diagnostics = admission.diagnostic;
            diagnostics += ": ";
            diagnostics += backendName;
        }
        diagnostics += " UI RHI atlas page lowering for ";
        diagnostics += policyName(policy);
        diagnostics +=
            " is a deterministic diagnostic until backend validation proves UiPipelineLayout, UiFrameSet, UiAtlasSet, atlas page binding, alpha blend, explicit resource states, backend-owned staging, backend-owned readback, and dynamic upload scheduling.";
        if (policy == UiAtlasPagePolicy::TextureArray) {
            diagnostics +=
                " The texture array layer path requires explicit texture array capability and descriptor lowering.";
        } else if (policy == UiAtlasPagePolicy::MultiPage) {
            diagnostics +=
                " The multi-page path requires explicit multi-page batching or deterministic pass splitting capability.";
        } else {
            diagnostics +=
                " The single texture path requires explicit sampled Texture2D binding validation.";
        }
        return {
            backend,
            false,
            std::move(diagnostics),
            false,
            policy,
            loweringModeFor(policy),
            false,
            false,
            false,
            true,
            true,
        };
    }
};

} // namespace arp::renderer::ui
