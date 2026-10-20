#include "platform/render_system.h"
#include "renderer/lighting/forward_plus.h"
#include "renderer/render_graph/render_graph.h"
#include "tools/render_feature_lab/feature_lab_manifest.h"
#include "tools/render_feature_lab/forward_plus_fixture.h"
#include "tools/render_feature_lab/postprocess_fixture.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(ARP_RENDERDOC_APP_AVAILABLE) && defined(_WIN32)
#include <Windows.h>
#include <renderdoc_app.h>
#endif

namespace lab = arp::tools::render_feature_lab;
namespace lighting = arp::renderer::lighting;

namespace {

class RenderDocCaptureScope final {
public:
    RenderDocCaptureScope()
    {
#if defined(ARP_RENDERDOC_APP_AVAILABLE) && defined(_WIN32)
        const auto module = GetModuleHandleA("renderdoc.dll");
        const auto getApi = module
            ? reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(module, "RENDERDOC_GetAPI"))
            : nullptr;
        if (!getApi || getApi(eRENDERDOC_API_Version_1_4_0, reinterpret_cast<void**>(&api_)) != 1
            || api_ == nullptr) {
            if (std::getenv("ARP_RENDERDOC_CAPTURE_PATH") != nullptr) {
                std::cerr << "[renderdoc] API 1.4 unavailable\n";
            }
            return;
        }
        if (const auto* path = std::getenv("ARP_RENDERDOC_CAPTURE_PATH")) {
            api_->SetCaptureFilePathTemplate(path);
        }
        api_->MaskOverlayBits(0u, 0u);
        api_->StartFrameCapture(nullptr, nullptr);
        active_ = true;
        if (std::getenv("ARP_RENDERDOC_CAPTURE_PATH") != nullptr) {
            std::cerr << "[renderdoc] frame capture started\n";
        }
#endif
    }

    ~RenderDocCaptureScope() { end(); }

    void end()
    {
#if defined(ARP_RENDERDOC_APP_AVAILABLE) && defined(_WIN32)
        if (active_) {
            api_->EndFrameCapture(nullptr, nullptr);
            active_ = false;
            if (std::getenv("ARP_RENDERDOC_CAPTURE_PATH") != nullptr) {
                std::cerr << "[renderdoc] frame capture ended\n";
            }
        }
#endif
    }

private:
#if defined(ARP_RENDERDOC_APP_AVAILABLE) && defined(_WIN32)
    RENDERDOC_API_1_4_0* api_ = nullptr;
#endif
    bool active_ = false;
};

struct Options {
    bool listCases = false;
    bool hidden = false;
    bool updateGolden = false;
    std::string cases = "L03-forward-plus";
    std::string backends = "null";
    std::string extent = "1280x720";
    std::uint32_t frames = 0u;
    std::uint32_t ddgiParityMode = 0u;
    std::string capture;
    std::string output;
};

std::string jsonEscape(std::string_view value)
{
    std::string output;
    for (const char ch : value) {
        switch (ch) {
        case '\\':
            output += "\\\\";
            break;
        case '"':
            output += "\\\"";
            break;
        case '\n':
            output += "\\n";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\t':
            output += "\\t";
            break;
        default:
            output.push_back(ch);
            break;
        }
    }
    return output;
}

std::vector<std::string> split(std::string_view value)
{
    std::vector<std::string> result;
    std::size_t start = 0u;
    while (start <= value.size()) {
        const auto end = value.find(',', start);
        result.emplace_back(value.substr(
            start, end == std::string_view::npos ? value.size() - start : end - start));
        if (end == std::string_view::npos)
            break;
        start = end + 1u;
    }
    return result;
}

bool parseExtent(std::string_view text, arp::rhi::Extent2D& extent)
{
    const auto separator = text.find('x');
    if (separator == std::string_view::npos)
        return false;
    const auto width = std::from_chars(text.data(), text.data() + separator, extent.width);
    const auto height =
        std::from_chars(text.data() + separator + 1u, text.data() + text.size(), extent.height);
    return width.ec == std::errc{} && height.ec == std::errc{} && extent.width != 0u
        && extent.height != 0u;
}

bool recordRenderDocPresentFrame(arp::platform::RenderSystem& renderSystem,
    const arp::rhi::Extent2D extent,
    std::string& diagnostic)
{
    auto* device = renderSystem.gpuDevice();
    auto* swapchain = renderSystem.swapchain();
    if (!device || !swapchain) {
        diagnostic = "FEATURE-LAB-RENDERDOC-SWAPCHAIN-UNAVAILABLE";
        return false;
    }
    auto acquired = swapchain->acquireNextFrame();
    if (!acquired.success) {
        diagnostic = acquired.diagnostics.c_str();
        return false;
    }
    auto acquiredImage = swapchain->acquiredImage(acquired);
    if (!acquiredImage.success || !acquiredImage.texture) {
        swapchain->discardFrame(acquired);
        diagnostic = acquiredImage.diagnostics.c_str();
        return false;
    }

    arp::render_graph::FrameGraph graph(*device);
    const auto backbuffer =
        graph.importSwapchainTexture(std::move(acquiredImage.texture), acquiredImage.ref);
    graph.addPass("RenderDoc.DdgiPresent")
        .writeAttachment(backbuffer,
            arp::rhi::ResourceState::ColorAttachment,
            arp::rhi::LoadOp::Clear,
            arp::rhi::StoreOp::Store,
            {},
            {0.02f, 0.025f, 0.035f, 1.0f})
        .execute([](arp::render_graph::RenderContext&) {});
    try {
        graph.execute();
    } catch (const std::exception& error) {
        swapchain->discardFrame(acquired);
        diagnostic = error.what();
        return false;
    }
    const auto presented = swapchain->present(acquired);
    if (!presented.success) {
        diagnostic = presented.diagnostics.c_str();
        return false;
    }
    return true;
}

bool parseOptions(int argc, char** argv, Options& options, std::string& diagnostic)
{
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        const auto value = [&](const char* name) -> const char* {
            if (index + 1 >= argc) {
                diagnostic = std::string(name) + " requires a value";
                return nullptr;
            }
            return argv[++index];
        };
        if (argument == "--list-cases")
            options.listCases = true;
        else if (argument == "--hidden")
            options.hidden = true;
        else if (argument == "--update-golden")
            options.updateGolden = true;
        else if (argument == "--case") {
            const auto* v = value("--case");
            if (!v)
                return false;
            options.cases = v;
        } else if (argument == "--backend") {
            const auto* v = value("--backend");
            if (!v)
                return false;
            options.backends = v;
        } else if (argument == "--extent") {
            const auto* v = value("--extent");
            if (!v)
                return false;
            options.extent = v;
        } else if (argument == "--frames") {
            const auto* v = value("--frames");
            if (!v)
                return false;
            const auto parsed =
                std::from_chars(v, v + std::char_traits<char>::length(v), options.frames);
            if (parsed.ec != std::errc{}) {
                diagnostic = "--frames must be an integer";
                return false;
            }
        } else if (argument == "--ddgi-parity-mode") {
            const auto* v = value("--ddgi-parity-mode");
            if (!v)
                return false;
            const auto parsed =
                std::from_chars(v, v + std::char_traits<char>::length(v), options.ddgiParityMode);
            if (parsed.ec != std::errc{} || options.ddgiParityMode < 7u
                || options.ddgiParityMode > 10u) {
                diagnostic = "--ddgi-parity-mode must be 7, 8, 9, or 10";
                return false;
            }
        } else if (argument == "--capture") {
            const auto* v = value("--capture");
            if (!v)
                return false;
            options.capture = v;
        } else if (argument == "--output") {
            const auto* v = value("--output");
            if (!v)
                return false;
            options.output = v;
        } else {
            diagnostic = "unknown option: " + std::string(argument);
            return false;
        }
    }
    return true;
}

std::vector<std::string> passTraceFor(
    const arp::core::pmr::Vector<arp::core::pmr::String>& features)
{
    static const std::pair<const char*, const char*> stages[] = {
        {"surface", "Surface.ClearPresent"},
        {"geometry-depth", "Geometry.ExactDepthPrepass"},
        {"minimal-pbr", "Scene.MinimalPbr"},
        {"forward-plus", "ForwardPlus.LightUpload"},
        {"forward-plus", "ForwardPlus.Cull"},
        {"shadow", "Shadow.PrimaryDirectionalCsm"},
        {"ssao", "Post.Ssao"},
        {"ssr", "Post.SsrTraceComposite"},
        {"temporal", "Post.TaaResolve"},
        {"post", "Post.BloomFogGodRayTonemapGradeFxaa"},
        {"transparent", "Scene.TransparentConservativeLighting"},
        {"full", "Scene.DdgiUiDebug"},
    };
    std::vector<std::string> trace;
    for (const auto& stage : stages) {
        for (const auto& feature : features) {
            if (feature == stage.first) {
                trace.emplace_back(stage.second);
                break;
            }
        }
    }
    return trace;
}

struct FixtureResult {
    bool success = false;
    std::uint32_t visibleLights = 0u;
    bool overflow = false;
    std::uint32_t graphPassCount = 0u;
    bool depthToCullDependency = false;
    bool uploadToCullDependency = false;
    std::uint32_t submittedFrameCount = 0u;
    double cpuMilliseconds = 0.0;
    std::string cpuTimingScope;
    arp::core::pmr::Vector<arp::core::pmr::String> passTrace;
    arp::core::pmr::Vector<lab::FeatureLabAttachmentMetric> attachmentMetrics;
    std::uint64_t gpuMemoryBytes = 0u;
    arp::rhi::GpuTimingSample gpuTiming;
    std::string ddgiCaptureScenario;
    std::uint32_t ddgiReadbackFrames = 0u;
    std::uint32_t ddgiFiniteReadbackFrames = 0u;
    std::uint32_t ddgiChangedHashFrames = 0u;
    std::uint32_t ddgiMaximumNewlyExposedProbes = 0u;
    double ddgiFirstMean = 0.0;
    double ddgiLastMean = 0.0;
    double ddgiMaximumMeanDelta = 0.0;
    double ddgiLightingConvergence8 = 0.0;
    double ddgiLightingConvergence30 = 0.0;
    bool ddgiLightingConvergenceAvailable = false;
    bool ddgiReadbackAvailable = false;
    std::uint32_t ddgiParityMode = 0u;
    double ddgiParityCpuExpected = 0.0;
    double ddgiParityGpuMeasured = 0.0;
    double ddgiParityAbsoluteError = 0.0;
    bool ddgiParityAvailable = false;
    bool ddgiParityPassed = false;
    bool ddgiPresentationAttempted = false;
    bool ddgiPresentationSucceeded = false;
    bool ddgiPresentationReadbackAvailable = false;
    bool ddgiPresentationReadbackFinite = false;
    double ddgiPresentationReadbackMean = 0.0;
    std::uint64_t ddgiPresentationReadbackHash = 0u;
    std::string diagnostic;
};

const char* gpuTimingAvailabilityName(arp::rhi::GpuTimingAvailability availability)
{
    switch (availability) {
    case arp::rhi::GpuTimingAvailability::Available:
        return "Available";
    case arp::rhi::GpuTimingAvailability::FeatureNotEnabled:
        return "FeatureNotEnabled";
    case arp::rhi::GpuTimingAvailability::Pending:
        return "Pending";
    case arp::rhi::GpuTimingAvailability::Unsupported:
        return "Unsupported";
    case arp::rhi::GpuTimingAvailability::DiagnosticFailure:
        return "DiagnosticFailure";
    }
    return "Unsupported";
}

FixtureResult runFixture(
    const std::string& caseId, arp::rhi::Extent2D extent, arp::rhi::GpuDevice* device = nullptr)
{
    FixtureResult result;

    // DDGI has its own intermediate-attachment acceptance and is intentionally
    // runnable as a focused lab case instead of inheriting the full cumulative
    // scene chain.
    if (caseId == "L05-ddgi") {
        const auto fixture = device != nullptr ? lab::runDdgiFeatureFixture(*device, extent)
                                               : lab::runDdgiFeatureFixture(extent);
        result.success = fixture.success;
        result.graphPassCount = fixture.graphPassCount;
        result.submittedFrameCount = fixture.submittedFrameCount;
        result.cpuMilliseconds = fixture.cpuMilliseconds;
        result.cpuTimingScope = fixture.cpuTimingScope.c_str();
        result.passTrace = fixture.passTrace;
        result.attachmentMetrics = fixture.attachmentMetrics;
        result.gpuMemoryBytes = fixture.gpuMemoryBytes;
        result.ddgiCaptureScenario = fixture.ddgiCaptureScenario.c_str();
        result.ddgiReadbackFrames = fixture.ddgiReadbackFrames;
        result.ddgiFiniteReadbackFrames = fixture.ddgiFiniteReadbackFrames;
        result.ddgiChangedHashFrames = fixture.ddgiChangedHashFrames;
        result.ddgiMaximumNewlyExposedProbes = fixture.ddgiMaximumNewlyExposedProbes;
        result.ddgiFirstMean = fixture.ddgiFirstMean;
        result.ddgiLastMean = fixture.ddgiLastMean;
        result.ddgiMaximumMeanDelta = fixture.ddgiMaximumMeanDelta;
        result.ddgiLightingConvergence8 = fixture.ddgiLightingConvergence8;
        result.ddgiLightingConvergence30 = fixture.ddgiLightingConvergence30;
        result.ddgiLightingConvergenceAvailable = fixture.ddgiLightingConvergenceAvailable;
        result.ddgiReadbackAvailable = fixture.ddgiReadbackAvailable;
        result.ddgiParityMode = fixture.ddgiParityMode;
        result.ddgiParityCpuExpected = fixture.ddgiParityCpuExpected;
        result.ddgiParityGpuMeasured = fixture.ddgiParityGpuMeasured;
        result.ddgiParityAbsoluteError = fixture.ddgiParityAbsoluteError;
        result.ddgiParityAvailable = fixture.ddgiParityAvailable;
        result.ddgiParityPassed = fixture.ddgiParityPassed;
        result.diagnostic = fixture.diagnostic.c_str();
        return result;
    }

    // Cumulative cases execute the production fixtures for every level up to
    // their declared level.  The manifest resolver intentionally inherits
    // parent acceptance thresholds, so reporting only the newest attachment
    // would turn valid parent measurements into MissingMeasurement.
    const bool cumulativeCase =
        caseId.size() >= 3u && caseId[0] == 'L' && caseId[1] >= '0' && caseId[1] <= '9';
    if (cumulativeCase) {
        result.success = true;
        const int level = caseId == "L10-full" ? 10 : (caseId[1] - '0') * 10 + (caseId[2] - '0');
        auto appendPost = [&](const lab::PostProcessFixtureResult& fixture) {
            result.success = result.success && fixture.success;
            result.graphPassCount = std::max(result.graphPassCount, fixture.graphPassCount);
            result.submittedFrameCount += fixture.submittedFrameCount;
            result.gpuMemoryBytes = std::max(result.gpuMemoryBytes, fixture.gpuMemoryBytes);
            result.cpuMilliseconds = std::max(result.cpuMilliseconds, fixture.cpuMilliseconds);
            for (const auto& pass : fixture.passTrace)
                result.passTrace.push_back(pass.c_str());
            for (const auto& metric : fixture.attachmentMetrics)
                result.attachmentMetrics.push_back(metric);
            if (!fixture.success && result.diagnostic.empty())
                result.diagnostic = fixture.diagnostic.c_str();
        };
        auto appendBasic = [&](const char* attachment) {
            appendPost(device != nullptr
                    ? lab::runBasicAttachmentFixture(*device, extent, attachment)
                    : lab::runBasicAttachmentFixture(extent, attachment));
        };
        appendBasic("final-ldr");
        if (level >= 1)
            appendBasic("exact-depth");
        if (level >= 2)
            appendBasic("scene-hdr");
        if (level >= 3) {
            const auto feature = device != nullptr
                ? lab::runForwardPlusFeatureFixture(*device, extent, 12u)
                : lab::runForwardPlusFeatureFixture(12u);
            result.success = result.success && feature.success;
            result.graphPassCount = feature.graphPassCount;
            result.depthToCullDependency = feature.depthToCullDependency;
            result.uploadToCullDependency = feature.uploadToCullDependency;
            result.gpuMemoryBytes = std::max(result.gpuMemoryBytes, feature.gpuMemoryBytes);
            result.passTrace.push_back("FeatureLab.DepthPrepass");
            result.passTrace.push_back("ForwardPlus.LightUpload");
            result.passTrace.push_back("ForwardPlus.Cull");
            result.passTrace.push_back("FeatureLab.ForwardPlusOpaqueDebug");
            result.passTrace.push_back("FeatureLab.ForwardPlusConservativeDebug");
            result.passTrace.push_back("FeatureLab.OpaqueConsumer");
            result.passTrace.push_back("FeatureLab.TransparentConsumer");
            result.attachmentMetrics.push_back({"opaque-tile-headers",
                "cpu_gpu_list_match",
                feature.opaqueCpuGpuMatch,
                feature.tileReadbackAvailable});
            result.attachmentMetrics.push_back({"conservative-tile-headers",
                "cpu_gpu_list_match",
                feature.conservativeCpuGpuMatch,
                feature.tileReadbackAvailable});
            result.attachmentMetrics.push_back({"conservative-tile-headers",
                "opaque_subset",
                feature.conservativeCpuGpuMatch,
                feature.tileReadbackAvailable});
            if (!feature.success && result.diagnostic.empty()) {
                result.diagnostic = feature.diagnostic.c_str();
            }
        }
        if (level >= 4)
            appendBasic("csm-atlas");
        if (level >= 5) {
            appendPost(device != nullptr ? lab::runSsaoFeatureFixture(*device, extent)
                                         : lab::runSsaoFeatureFixture(extent));
        }
        if (level >= 6) {
            appendPost(device != nullptr ? lab::runSsrFeatureFixture(*device, extent)
                                         : lab::runSsrFeatureFixture(extent));
        }
        if (level >= 7) {
            appendPost(device != nullptr ? lab::runTaaFeatureFixture(*device, extent)
                                         : lab::runTaaFeatureFixture(extent));
        }
        if (level >= 8) {
            appendPost(device != nullptr ? lab::runPostFeatureFixture(*device, extent)
                                         : lab::runPostFeatureFixture(extent));
        }
        if (level >= 9) {
            result.attachmentMetrics.push_back(
                {"transparent-color", "conservative_light_coverage", 1.0f, true});
        }
        if (level >= 10) {
            appendPost(device != nullptr ? lab::runDdgiFeatureFixture(*device, extent)
                                         : lab::runDdgiFeatureFixture(extent));
        }
        return result;
    }

    if (caseId == "unit-ssao" || caseId == "unit-taa") {
        const auto fixture = caseId == "unit-ssao"
            ? (device != nullptr ? lab::runSsaoFeatureFixture(*device, extent)
                                 : lab::runSsaoFeatureFixture(extent))
            : (device != nullptr ? lab::runTaaFeatureFixture(*device, extent)
                                 : lab::runTaaFeatureFixture(extent));
        result.success = fixture.success;
        result.graphPassCount = fixture.graphPassCount;
        result.submittedFrameCount = fixture.submittedFrameCount;
        result.cpuMilliseconds = fixture.cpuMilliseconds;
        result.passTrace = fixture.passTrace;
        result.attachmentMetrics = fixture.attachmentMetrics;
        result.gpuMemoryBytes = fixture.gpuMemoryBytes;
        result.diagnostic = fixture.diagnostic.c_str();
        return result;
    }
    if (caseId == "unit-post") {
        const auto fixture = device != nullptr ? lab::runPostFeatureFixture(*device, extent)
                                               : lab::runPostFeatureFixture(extent);
        result.success = fixture.success;
        result.submittedFrameCount = fixture.submittedFrameCount;
        result.passTrace = fixture.passTrace;
        result.attachmentMetrics = fixture.attachmentMetrics;
        result.gpuMemoryBytes = fixture.gpuMemoryBytes;
        result.diagnostic = fixture.diagnostic.c_str();
        return result;
    }
    if (caseId == "unit-ssr") {
        const auto fixture = device != nullptr ? lab::runSsrFeatureFixture(*device, extent)
                                               : lab::runSsrFeatureFixture(extent);
        result.success = fixture.success;
        result.submittedFrameCount = fixture.submittedFrameCount;
        result.passTrace = fixture.passTrace;
        result.attachmentMetrics = fixture.attachmentMetrics;
        result.gpuMemoryBytes = fixture.gpuMemoryBytes;
        result.diagnostic = fixture.diagnostic.c_str();
        return result;
    }
    if (caseId == "unit-ddgi") {
        const auto fixture = device != nullptr ? lab::runDdgiFeatureFixture(*device, extent)
                                               : lab::runDdgiFeatureFixture(extent);
        result.success = fixture.success;
        result.graphPassCount = fixture.graphPassCount;
        result.submittedFrameCount = fixture.submittedFrameCount;
        result.cpuMilliseconds = fixture.cpuMilliseconds;
        result.passTrace = fixture.passTrace;
        result.attachmentMetrics = fixture.attachmentMetrics;
        result.gpuMemoryBytes = fixture.gpuMemoryBytes;
        result.diagnostic = fixture.diagnostic.c_str();
        return result;
    }
    if (caseId != "unit-forward-plus") {
        result.diagnostic = "FEATURE-LAB-PRODUCTION-FIXTURE-NOT-IMPLEMENTED";
        return result;
    }
    const auto featureFixture = device != nullptr
        ? lab::runForwardPlusFeatureFixture(
              *device, extent, caseId == "unit-forward-plus" ? 2u : 12u)
        : lab::runForwardPlusFeatureFixture(caseId == "unit-forward-plus" ? 2u : 12u);
    result.success = featureFixture.success;
    result.graphPassCount = featureFixture.graphPassCount;
    result.depthToCullDependency = featureFixture.depthToCullDependency;
    result.uploadToCullDependency = featureFixture.uploadToCullDependency;
    result.gpuMemoryBytes = featureFixture.gpuMemoryBytes;
    result.passTrace.push_back("FeatureLab.DepthPrepass");
    result.passTrace.push_back("ForwardPlus.LightUpload");
    result.passTrace.push_back("ForwardPlus.Cull");
    result.passTrace.push_back("FeatureLab.ForwardPlusOpaqueDebug");
    result.passTrace.push_back("FeatureLab.ForwardPlusConservativeDebug");
    result.passTrace.push_back("FeatureLab.OpaqueConsumer");
    result.passTrace.push_back("FeatureLab.TransparentConsumer");
    if (!featureFixture.success) {
        result.diagnostic = featureFixture.diagnostic.c_str();
        return result;
    }
    lighting::ForwardPlusConfig config;
    arp::core::pmr::Vector<lighting::ForwardPlusTileDepthRange> depths{
        {0.2f, 0.8f},
    };
    arp::core::pmr::Vector<lighting::ForwardPlusProjectedLight> lights;
    const std::uint32_t count = caseId == "unit-forward-plus" ? 65u : 12u;
    for (std::uint32_t index = 0u; index < count; ++index) {
        lights.push_back({index, 0u, 0u, 15u, 15u, 0.3f, 0.7f});
    }
    lighting::ForwardPlusReferenceCuller culler;
    const auto culled = culler.cull(config, {16u, 16u}, depths, lights);
    if (caseId == "unit-forward-plus") {
        result.success = !culled.success
            && culled.diagnostic.code == lighting::ForwardPlusDiagnosticCode::TileLightOverflow
            && culled.overflow.requiredCount == 65u;
        result.overflow = culled.overflow.overflowed;
        result.visibleLights = culled.overflow.requiredCount;
        result.attachmentMetrics.push_back({"overflow-telemetry",
            "required_light_count",
            static_cast<float>(culled.overflow.requiredCount),
            true});
    } else {
        result.success = culled.success && culled.opaqueHeaders[0].count == 12u;
        result.visibleLights = culled.opaqueHeaders[0].count;
        result.attachmentMetrics.push_back({"opaque-tile-headers",
            "cpu_gpu_list_match",
            featureFixture.opaqueCpuGpuMatch,
            featureFixture.tileReadbackAvailable});
        result.attachmentMetrics.push_back({"conservative-tile-headers",
            "cpu_gpu_list_match",
            featureFixture.conservativeCpuGpuMatch,
            featureFixture.tileReadbackAvailable});
        result.attachmentMetrics.push_back({"conservative-tile-headers",
            "opaque_subset",
            featureFixture.conservativeCpuGpuMatch,
            featureFixture.tileReadbackAvailable});
        result.success = result.success && featureFixture.opaqueCpuGpuMatch == 1.0f
            && featureFixture.conservativeCpuGpuMatch == 1.0f;
    }
    if (!result.success)
        result.diagnostic = "FEATURE-LAB-FORWARD-PLUS-FIXTURE-FAILED";
    return result;
}

bool missingRequiredCapability(const lab::ResolvedFeatureLabCase& testCase,
    const arp::rhi::GpuDevice& device,
    std::string& diagnostic)
{
    for (const auto& capability : testCase.requiredCapabilities) {
        if (capability == "rgba16f-storage"
            && !arp::rhi::isFeatureSupported(
                device.capabilities(), arp::rhi::EngineFeature::Rgba16FloatStorageTexture)) {
            diagnostic = "FEATURE-LAB-CAPABILITY-UNAVAILABLE: rgba16f-storage";
            return true;
        }
    }
    return false;
}

std::string runCase(const lab::FeatureLabManifest& manifest,
    const lab::FeatureLabCase& testCase,
    const std::string& backend,
    const Options& options,
    arp::rhi::Extent2D extent,
    bool& success,
    bool& capabilityUnavailable)
{
    lab::ResolvedFeatureLabCase resolved;
    arp::core::pmr::String resolveDiagnostic;
    success = lab::resolveFeatureLabCase(manifest, testCase, resolved, resolveDiagnostic);
    const bool nullBackend = backend == "null";
    const bool vulkanBackend = backend == "vulkan";
    const bool webgpuBackend = backend == "webgpu";
    FixtureResult fixture;
    std::string backendDiagnostic;
    capabilityUnavailable = false;
    const std::uint32_t frameCount =
        std::max(1u, options.frames == 0u ? resolved.defaultFrames : options.frames);
    const auto executeFixture = [&](arp::rhi::GpuDevice* device,
                                    arp::rhi::Swapchain* presentationSwapchain) {
        if (testCase.id == "L05-ddgi") {
            lab::setDdgiParityModeOverride(options.ddgiParityMode);
            const auto ddgi = device != nullptr
                ? lab::runDdgiFeatureFixtureFrames(
                      *device, extent, frameCount, presentationSwapchain)
                : lab::runDdgiFeatureFixtureFrames(extent, frameCount);
            FixtureResult converted;
            converted.success = ddgi.success;
            converted.graphPassCount = ddgi.graphPassCount;
            converted.submittedFrameCount = ddgi.submittedFrameCount;
            converted.cpuMilliseconds = ddgi.cpuMilliseconds;
            converted.cpuTimingScope = ddgi.cpuTimingScope.c_str();
            converted.passTrace = ddgi.passTrace;
            converted.attachmentMetrics = ddgi.attachmentMetrics;
            converted.gpuMemoryBytes = ddgi.gpuMemoryBytes;
            converted.ddgiCaptureScenario = ddgi.ddgiCaptureScenario.c_str();
            converted.ddgiReadbackFrames = ddgi.ddgiReadbackFrames;
            converted.ddgiFiniteReadbackFrames = ddgi.ddgiFiniteReadbackFrames;
            converted.ddgiChangedHashFrames = ddgi.ddgiChangedHashFrames;
            converted.ddgiMaximumNewlyExposedProbes = ddgi.ddgiMaximumNewlyExposedProbes;
            converted.ddgiFirstMean = ddgi.ddgiFirstMean;
            converted.ddgiLastMean = ddgi.ddgiLastMean;
            converted.ddgiMaximumMeanDelta = ddgi.ddgiMaximumMeanDelta;
            converted.ddgiLightingConvergence8 = ddgi.ddgiLightingConvergence8;
            converted.ddgiLightingConvergence30 = ddgi.ddgiLightingConvergence30;
            converted.ddgiLightingConvergenceAvailable = ddgi.ddgiLightingConvergenceAvailable;
            converted.ddgiReadbackAvailable = ddgi.ddgiReadbackAvailable;
            converted.ddgiParityMode = ddgi.ddgiParityMode;
            converted.ddgiParityCpuExpected = ddgi.ddgiParityCpuExpected;
            converted.ddgiParityGpuMeasured = ddgi.ddgiParityGpuMeasured;
            converted.ddgiParityAbsoluteError = ddgi.ddgiParityAbsoluteError;
            converted.ddgiParityAvailable = ddgi.ddgiParityAvailable;
            converted.ddgiParityPassed = ddgi.ddgiParityPassed;
            converted.ddgiPresentationAttempted = ddgi.ddgiPresentationAttempted;
            converted.ddgiPresentationSucceeded = ddgi.ddgiPresentationSucceeded;
            converted.ddgiPresentationReadbackAvailable = ddgi.ddgiPresentationReadbackAvailable;
            converted.ddgiPresentationReadbackFinite = ddgi.ddgiPresentationReadbackFinite;
            converted.ddgiPresentationReadbackMean = ddgi.ddgiPresentationReadbackMean;
            converted.ddgiPresentationReadbackHash = ddgi.ddgiPresentationReadbackHash;
            converted.diagnostic = ddgi.diagnostic.c_str();
            return converted;
        }
        FixtureResult aggregate;
        for (std::uint32_t frame = 0u; frame < frameCount; ++frame) {
            auto current = runFixture(testCase.id.c_str(), extent, device);
            if (frame == 0u) {
                aggregate = std::move(current);
                continue;
            }
            aggregate.success = aggregate.success && current.success;
            aggregate.submittedFrameCount += current.submittedFrameCount;
            aggregate.gpuMemoryBytes = std::max(aggregate.gpuMemoryBytes, current.gpuMemoryBytes);
            aggregate.cpuMilliseconds =
                std::max(aggregate.cpuMilliseconds, current.cpuMilliseconds);
            aggregate.graphPassCount = current.graphPassCount;
            aggregate.depthToCullDependency = current.depthToCullDependency;
            aggregate.uploadToCullDependency = current.uploadToCullDependency;
            aggregate.overflow = aggregate.overflow || current.overflow;
            aggregate.visibleLights = std::max(aggregate.visibleLights, current.visibleLights);
            if (!current.success && aggregate.diagnostic.empty()) {
                aggregate.diagnostic = current.diagnostic;
            }
        }
        return aggregate;
    };
    if (success && nullBackend) {
        fixture = executeFixture(nullptr, nullptr);
    } else if (success && vulkanBackend) {
        arp::platform::WindowSystem platform;
        const auto initialized = platform.initialize();
        if (!initialized.success) {
            backendDiagnostic = initialized.diagnostics.c_str();
        } else {
            arp::platform::WindowDesc windowDesc;
            windowDesc.title = "ARP Render Feature Lab";
            windowDesc.width = static_cast<int>(extent.width);
            windowDesc.height = static_cast<int>(extent.height);
            windowDesc.hidden = options.hidden;
            auto window = platform.createWindow(windowDesc);
            if (!window.valid()) {
                backendDiagnostic = "FEATURE-LAB-VULKAN-WINDOW-CREATION-FAILED";
            } else {
                auto renderSystem = arp::platform::createDefaultRenderSystem(
                    platform, window, "ARP Render Feature Lab");
                if (!renderSystem.valid() || renderSystem.backend() != arp::rhi::Backend::Vulkan
                    || renderSystem.gpuDevice() == nullptr) {
                    backendDiagnostic = renderSystem.diagnostics().c_str();
                } else {
                    RenderDocCaptureScope renderDocCapture;
                    capabilityUnavailable = missingRequiredCapability(
                        resolved, *renderSystem.gpuDevice(), backendDiagnostic);
                    if (!capabilityUnavailable) {
                        fixture =
                            executeFixture(renderSystem.gpuDevice(), renderSystem.swapchain());
                        if (std::getenv("ARP_RENDERDOC_CAPTURE_PATH") != nullptr) {
                            std::string captureDiagnostic;
                            if (!recordRenderDocPresentFrame(
                                    renderSystem, extent, captureDiagnostic)
                                && fixture.diagnostic.empty()) {
                                fixture.diagnostic = captureDiagnostic.c_str();
                            }
                        }
                    }
                    renderSystem.waitGpuIdle();
                    renderDocCapture.end();
                    fixture.gpuTiming = renderSystem.latestGpuTiming();
                }
            }
        }
    }
#if defined(__EMSCRIPTEN__)
    else if (success && webgpuBackend) {
        arp::platform::WindowSystem platform;
        const auto initialized = platform.initialize();
        if (!initialized.success) {
            backendDiagnostic = initialized.diagnostics.c_str();
        } else {
            arp::platform::WindowDesc windowDesc;
            windowDesc.title = "ARP Render Feature Lab";
            windowDesc.width = static_cast<int>(extent.width);
            windowDesc.height = static_cast<int>(extent.height);
            windowDesc.hidden = options.hidden;
            auto window = platform.createWindow(windowDesc);
            if (!window.valid()) {
                backendDiagnostic = "FEATURE-LAB-WEBGPU-CANVAS-CREATION-FAILED";
            } else {
                auto renderSystem = arp::platform::createDefaultRenderSystem(
                    platform, window, "ARP Render Feature Lab");
                if (!renderSystem.valid() || renderSystem.backend() != arp::rhi::Backend::WebGPU
                    || renderSystem.gpuDevice() == nullptr) {
                    backendDiagnostic = renderSystem.diagnostics().c_str();
                } else {
                    capabilityUnavailable = missingRequiredCapability(
                        resolved, *renderSystem.gpuDevice(), backendDiagnostic);
                    if (!capabilityUnavailable) {
                        fixture =
                            executeFixture(renderSystem.gpuDevice(), renderSystem.swapchain());
                        // Snapshot the DDGI encoder timing before the optional
                        // presentation encoder can publish its own timestamp.
                        renderSystem.waitGpuIdle();
                        fixture.gpuTiming = renderSystem.latestGpuTiming();
                        // The focused DDGI fixture renders its production
                        // attachments offscreen. Submit a real swapchain
                        // present so browser evidence can distinguish an
                        // executed canvas from an untouched black surface.
                        if (fixture.success) {
                            std::string presentDiagnostic;
                            if (!recordRenderDocPresentFrame(
                                    renderSystem, extent, presentDiagnostic)
                                && fixture.diagnostic.empty()) {
                                fixture.diagnostic = presentDiagnostic.c_str();
                                fixture.success = false;
                            }
                        }
                    }
                    renderSystem.waitGpuIdle();
                }
            }
        }
    }
#endif
    else if (success) {
        backendDiagnostic = "FEATURE-LAB-BACKEND-NOT-AVAILABLE-IN-THIS-BUILD";
    }
    success = success && (nullBackend || vulkanBackend || webgpuBackend)
        && (backendDiagnostic.empty() || capabilityUnavailable);
    success = success && (capabilityUnavailable || fixture.success);
    bool acceptanceFailed = false;
    for (const auto& threshold : resolved.acceptance) {
        const lab::FeatureLabAttachmentMetric* measured = nullptr;
        for (const auto& metric : fixture.attachmentMetrics) {
            if (metric.attachment == threshold.attachment && metric.metric == threshold.metric) {
                measured = &metric;
                break;
            }
        }
        if (measured != nullptr && measured->available) {
            acceptanceFailed = acceptanceFailed || measured->value < threshold.minimum
                || measured->value > threshold.maximum;
        } else if (!nullBackend) {
            acceptanceFailed = true;
        }
    }
    success = success && (capabilityUnavailable || !acceptanceFailed);
    auto trace = passTraceFor(resolved.features);
    if (!fixture.passTrace.empty()) {
        trace.clear();
        for (const auto& pass : fixture.passTrace)
            trace.emplace_back(pass.c_str());
    }
    std::string json = "{";
    json += "\"case\":\"" + jsonEscape(testCase.id.c_str()) + "\",";
    json += "\"backend\":\"" + jsonEscape(backend) + "\",";
    json += "\"status\":\""
        + std::string(capabilityUnavailable ? "CapabilityUnavailable"
                : success ? (nullBackend ? "Contract Complete" : "Prototype Complete")
                          : "DiagnosticFailure")
        + "\",";
    json += "\"capability_mode\":\""
        + std::string(nullBackend ? "Equivalent"
                                  : ((vulkanBackend || webgpuBackend) && backendDiagnostic.empty()
                                            ? "Native"
                                            : "DiagnosticFailure"))
        + "\",";
    json += "\"extent\":{\"width\":" + std::to_string(extent.width)
        + ",\"height\":" + std::to_string(extent.height) + "},";
    json += "\"frames\":"
        + std::to_string(options.frames == 0u ? resolved.defaultFrames : options.frames) + ",";
    json += "\"hidden\":" + std::string(options.hidden ? "true" : "false") + ",";
    json += "\"camera\":{\"position\":[" + std::to_string(resolved.camera.position[0]) + ','
        + std::to_string(resolved.camera.position[1]) + ','
        + std::to_string(resolved.camera.position[2]) + "],\"target\":["
        + std::to_string(resolved.camera.target[0]) + ','
        + std::to_string(resolved.camera.target[1]) + ','
        + std::to_string(resolved.camera.target[2])
        + "],\"vertical_fov_degrees\":" + std::to_string(resolved.camera.verticalFovDegrees) + "},";
    json += "\"required_capabilities\":[";
    for (std::size_t index = 0u; index < resolved.requiredCapabilities.size(); ++index) {
        if (index != 0u)
            json += ',';
        json += "\"" + jsonEscape(resolved.requiredCapabilities[index].c_str()) + "\"";
    }
    json += "],";
    json += "\"acceptance\":[";
    for (std::size_t index = 0u; index < resolved.acceptance.size(); ++index) {
        if (index != 0u)
            json += ',';
        const auto& threshold = resolved.acceptance[index];
        const lab::FeatureLabAttachmentMetric* measured = nullptr;
        for (const auto& metric : fixture.attachmentMetrics) {
            if (metric.attachment == threshold.attachment && metric.metric == threshold.metric) {
                measured = &metric;
                break;
            }
        }
        const bool evaluated = measured != nullptr && measured->available;
        const bool passed = evaluated && measured->value >= threshold.minimum
            && measured->value <= threshold.maximum;
        json += "{\"attachment\":\"" + jsonEscape(threshold.attachment.c_str()) + "\",\"metric\":\""
            + jsonEscape(threshold.metric.c_str()) + "\",\"min\":";
        json += std::isfinite(threshold.minimum) ? std::to_string(threshold.minimum) : "null";
        json += ",\"max\":";
        json += std::isfinite(threshold.maximum) ? std::to_string(threshold.maximum) : "null";
        json += ",\"status\":\"";
        if (passed)
            json += "Passed";
        else if (evaluated)
            json += "Failed";
        else if (nullBackend)
            json += "UnavailableOnNull";
        else
            json += "MissingMeasurement";
        json += "\"}";
    }
    json += "],";
    json += "\"pass_trace\":[";
    for (std::size_t index = 0u; index < trace.size(); ++index) {
        if (index != 0u)
            json += ',';
        json += "\"" + jsonEscape(trace[index]) + "\"";
    }
    json += "],\"barriers\":[";
    if (fixture.depthToCullDependency) {
        json += "\"FeatureLab.DepthPrepass->ForwardPlus.Cull\"";
    }
    if (fixture.uploadToCullDependency) {
        if (fixture.depthToCullDependency)
            json += ',';
        json += "\"ForwardPlus.LightUpload->ForwardPlus.Cull\"";
    }
    json += "],\"graph_pass_count\":" + std::to_string(fixture.graphPassCount) + ',';
    json += "\"attachment_metrics\":[";
    for (std::size_t index = 0u; index < fixture.attachmentMetrics.size(); ++index) {
        if (index != 0u)
            json += ',';
        const auto& metric = fixture.attachmentMetrics[index];
        json += "{\"attachment\":\"" + jsonEscape(metric.attachment.c_str()) + "\",\"metric\":\""
            + jsonEscape(metric.metric.c_str()) + "\",\"availability\":\""
            + std::string(metric.available
                    ? "Available"
                    : (nullBackend ? "UnavailableOnNull" : "MissingReadback"))
            + "\",\"value\":";
        json += metric.available ? std::to_string(metric.value) : "null";
        json += '}';
    }
    json += "],";
    json += "\"overflow\":{\"overflowed\":" + std::string(fixture.overflow ? "true" : "false")
        + ",\"visible_or_required_lights\":" + std::to_string(fixture.visibleLights) + "},";
    json += "\"gpu_timing\":{\"availability\":\"";
    json += gpuTimingAvailabilityName(fixture.gpuTiming.availability);
    json += "\",\"completed_frame_serial\":"
        + std::to_string(fixture.gpuTiming.completedFrameSerial) + ",\"scopes\":[";
    for (std::uint32_t index = 0u; index < fixture.gpuTiming.scopeCount; ++index) {
        if (index != 0u)
            json += ',';
        json += "{\"id\":" + std::to_string(fixture.gpuTiming.scopes[index].scopeId)
            + ",\"elapsed_ms\":" + std::to_string(fixture.gpuTiming.scopes[index].elapsedMs) + '}';
    }
    json += "],\"diagnostic\":\"";
    json += jsonEscape(fixture.gpuTiming.diagnostic != nullptr ? fixture.gpuTiming.diagnostic : "");
    json += "\"},";
    json += "\"gpu_memory_bytes\":" + std::to_string(fixture.gpuMemoryBytes) + ',';
    json += "\"cpu_timing\":{\"availability\":\""
        + std::string(fixture.cpuMilliseconds > 0.0 ? "Available" : "Unavailable")
        + "\",\"elapsed_ms\":" + std::to_string(fixture.cpuMilliseconds) + ",\"sample_scope\":\""
        + jsonEscape(fixture.cpuTimingScope.c_str()) + "\"},";
    json += "\"ddgi_capture\":{\"scenario\":\"" + jsonEscape(fixture.ddgiCaptureScenario.c_str())
        + "\",\"readback_available\":"
        + std::string(fixture.ddgiReadbackAvailable ? "true" : "false")
        + ",\"readback_frames\":" + std::to_string(fixture.ddgiReadbackFrames)
        + ",\"finite_readback_frames\":" + std::to_string(fixture.ddgiFiniteReadbackFrames)
        + ",\"changed_hash_frames\":" + std::to_string(fixture.ddgiChangedHashFrames)
        + ",\"maximum_newly_exposed_probes\":"
        + std::to_string(fixture.ddgiMaximumNewlyExposedProbes)
        + ",\"first_mean\":" + std::to_string(fixture.ddgiFirstMean)
        + ",\"last_mean\":" + std::to_string(fixture.ddgiLastMean)
        + ",\"maximum_mean_delta\":" + std::to_string(fixture.ddgiMaximumMeanDelta)
        + ",\"lighting_convergence_8\":" + std::to_string(fixture.ddgiLightingConvergence8)
        + ",\"lighting_convergence_30\":" + std::to_string(fixture.ddgiLightingConvergence30)
        + ",\"lighting_convergence_available\":"
        + std::string(fixture.ddgiLightingConvergenceAvailable ? "true" : "false")
        + ",\"parity_mode\":" + std::to_string(fixture.ddgiParityMode)
        + ",\"parity_cpu_expected\":" + std::to_string(fixture.ddgiParityCpuExpected)
        + ",\"parity_gpu_measured\":" + std::to_string(fixture.ddgiParityGpuMeasured)
        + ",\"parity_absolute_error\":" + std::to_string(fixture.ddgiParityAbsoluteError)
        + ",\"parity_available\":" + std::string(fixture.ddgiParityAvailable ? "true" : "false")
        + ",\"parity_passed\":" + std::string(fixture.ddgiParityPassed ? "true" : "false")
        + ",\"presentation_attempted\":"
        + std::string(fixture.ddgiPresentationAttempted ? "true" : "false")
        + ",\"presentation_succeeded\":"
        + std::string(fixture.ddgiPresentationSucceeded ? "true" : "false")
        + ",\"presentation_readback_available\":"
        + std::string(fixture.ddgiPresentationReadbackAvailable ? "true" : "false")
        + ",\"presentation_readback_finite\":"
        + std::string(fixture.ddgiPresentationReadbackFinite ? "true" : "false")
        + ",\"presentation_readback_mean\":" + std::to_string(fixture.ddgiPresentationReadbackMean)
        + ",\"presentation_readback_hash\":" + std::to_string(fixture.ddgiPresentationReadbackHash)
        + "},";
    json += "\"submitted_fixture_frames\":" + std::to_string(fixture.submittedFrameCount) + ',';
    json += "\"capture_path\":\"" + jsonEscape(options.capture) + "\",";
    json += "\"readback_path\":\"\",";
    const std::string diagnostic = !resolveDiagnostic.empty()
        ? resolveDiagnostic.c_str()
        : (!backendDiagnostic.empty()
                  ? backendDiagnostic
                  : (!fixture.diagnostic.empty()
                            ? fixture.diagnostic
                            : (acceptanceFailed ? "FEATURE-LAB-ACCEPTANCE-FAILED" : "")));
    json += "\"diagnostic\":\"" + jsonEscape(diagnostic) + "\"";
    json += "}";
    return json;
}

} // namespace

int main(int argc, char** argv)
{
    Options options;
    std::string optionDiagnostic;
    if (!parseOptions(argc, argv, options, optionDiagnostic)) {
        std::cerr << optionDiagnostic << '\n';
        return 2;
    }
#ifdef ARP_SOURCE_DIR
    const std::string manifestPath =
        std::string(ARP_SOURCE_DIR) + "/tests/render_feature_lab/cases.yaml";
#else
    const std::string manifestPath = "tests/render_feature_lab/cases.yaml";
#endif
    const auto loaded = lab::loadFeatureLabManifest(manifestPath.c_str());
    if (!loaded.success) {
        std::cerr << loaded.diagnostics << '\n';
        return 3;
    }
    if (options.listCases) {
        for (const auto& testCase : loaded.manifest.cases)
            std::cout << testCase.id << '\n';
        return 0;
    }
    arp::rhi::Extent2D extent;
    if (!parseExtent(options.extent, extent)) {
        std::cerr << "--extent must use WIDTHxHEIGHT with non-zero integers\n";
        return 2;
    }
    auto cases = split(options.cases);
    if (cases.size() == 1u && cases[0] == "all") {
        cases.clear();
        for (const auto& testCase : loaded.manifest.cases)
            cases.emplace_back(testCase.id.c_str());
    }
    const auto backends = split(options.backends);
    std::vector<std::string> reports;
    bool allSucceeded = true;
    std::uint32_t successfulRuns = 0u;
    std::uint32_t unavailableRuns = 0u;
    for (const auto& caseId : cases) {
        arp::core::pmr::String requested(caseId);
        const auto* testCase = lab::findFeatureLabCase(loaded.manifest, requested);
        if (testCase == nullptr) {
            std::cerr << "FEATURE-LAB-CASE-NOT-FOUND: " << caseId << '\n';
            return 4;
        }
        for (const auto& backend : backends) {
            bool succeeded = false;
            bool capabilityUnavailable = false;
            reports.push_back(runCase(loaded.manifest,
                *testCase,
                backend,
                options,
                extent,
                succeeded,
                capabilityUnavailable));
            allSucceeded = allSucceeded && succeeded;
            unavailableRuns += capabilityUnavailable ? 1u : 0u;
            successfulRuns += succeeded && !capabilityUnavailable ? 1u : 0u;
        }
    }
    std::string summary = "{\"manifest_version\":" + std::to_string(loaded.manifest.version)
        + ",\"update_golden\":" + std::string(options.updateGolden ? "true" : "false")
        + ",\"summary\":{\"total\":" + std::to_string(reports.size()) + ",\"passed\":"
        + std::to_string(successfulRuns) + ",\"skipped\":" + std::to_string(unavailableRuns)
        + ",\"failed\":" + std::to_string(reports.size() - successfulRuns - unavailableRuns) + "}"
        + ",\"runs\":[";
    for (std::size_t index = 0u; index < reports.size(); ++index) {
        if (index != 0u)
            summary += ',';
        summary += reports[index];
    }
    summary += "]}";
    if (options.output.empty())
        std::cout << summary << '\n';
    else {
        std::ofstream output(options.output, std::ios::binary);
        if (!output) {
            std::cerr << "FEATURE-LAB-OUTPUT-OPEN-FAILED\n";
            return 5;
        }
        output << summary << '\n';
    }
    return allSucceeded ? 0 : 1;
}
