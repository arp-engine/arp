#pragma once

#include "core/math/half_float.h"
#include "renderer/volumetric_cloud/cloud_quality.h"
#include "tests/fixtures/cloud/cloud_fixture.h"
#include "tests/fixtures/cloud/cloud_sequence_oracle.h"

#include <cstdio>
#include <cstring>

namespace arp::tests::cloud {

// CloudQualityAcceptance/1: absolute errors in linear L RGB, scalar T and HDR
// RGB. Alpha is an exact invariant. Fixed limits precede measurement; these
// finite-scene gates do not grant 720p quality/performance/platform acceptance.
struct QualityError {
    std::uint64_t count = 0;
    double squared = 0, maximum = 0;

    void add(double actual, double expected)
    {
        assert(std::isfinite(actual) && std::isfinite(expected));
        const double error = std::fabs(actual - expected);
        squared += error * error;
        maximum = std::max(maximum, error);
        ++count;
    }

    double rms() const { return count ? std::sqrt(squared / count) : 0; }

    bool within(double rmsLimit, double maxLimit) const
    {
        return count && rms() <= rmsLimit && maximum <= maxLimit;
    }

    void write(std::FILE* file) const
    {
        std::fprintf(file,
            "{\"count\":%llu,\"rms\":%.12g,\"max\":%.12g}",
            static_cast<unsigned long long>(count),
            rms(),
            maximum);
    }
};

class QualityOracle final {
public:
    static constexpr double rmsLimit = .01, maxLimit = .05;
    static constexpr double convergenceRmsLimit = .001, convergenceMaxLimit = .005;
    QualityError radiance, transmittance, hdr, rawRadiance, rawTransmittance, exposedHdr;
    QualityError convergence;
    unsigned exposedPixels = 0, coveredCloudPixels = 0;

    void prepare(const SequenceOracle& frame, const assets::cloud::CloudAsset& asset)
    {
        using namespace renderer::cloud;
        assert(frame.layer.windVelocity.x == 0 && frame.layer.windVelocity.y == 0
            && frame.layer.windVelocity.z == 0);
        // Cache only physical reference inputs. Previous matrices, temporal
        // weight and the production sampling phase do not change this oracle.
        auto key = frame.parameters;
        key.previousViewProjection = {};
        key.previousCamera = {};
        key.cameraTime.w = 0;
        key.temporal = {0, 0, frame.parameters.temporal.z, 0};
        key.jitter = {0, 0, .5f, frame.parameters.jitter.w};
        key.sampling[0] = key.sampling[1] = 0;
        const bool first = fine_.empty();
        const bool changed = first || std::memcmp(&key, &referenceKey_, sizeof(key));
        referenceKey_ = key;
        if (first)
            previous_ = frame.foreground;
        if (changed) {
            clippedValid_.assign(
                std::size_t(frame.plan.extent.width) * frame.plan.extent.height, 0);
            clippedReference_.resize(clippedValid_.size());
            clippedFine_.resize(clippedValid_.size());
        }
        for (unsigned level = 0; changed && level < 2; ++level) {
            SequenceOracle cpu;
            cpu.parameters = frame.parameters;
            cpu.parameters.previousCamera.w = 0;
            cpu.parameters.jitter.z = .5f;
            auto config = tests::cloud::config();
            assert(CloudQuality::apply(1u, CloudQualityTier::Reference, config).success);
            if (level) {
                config.viewSteps *= 2u;
                config.lightSteps *= 2u;
            }
            assert(CloudExecutionPlanner::describeResources(config,
                frame.plan.extent,
                asset.cells.size() * sizeof(core::Vec4),
                sizeof(CloudGpuConstants),
                cpu.plan)
                    .success);
            cpu.parameters.dimensions[0] = cpu.plan.sampleExtent.width;
            cpu.parameters.dimensions[1] = cpu.plan.sampleExtent.height;
            cpu.parameters.sampling[0] = config.viewSteps;
            cpu.parameters.sampling[1] = config.lightSteps;
            cpu.layer = frame.layer;
            cpu.build(asset);
            (level ? fine_ : reference_) = std::move(cpu.raw);
        }
        CloudReference reference;
        assert(reference.configure(asset, frame.layer).success);
        vacuum_.clear();
        coveredCloudPixels = 0;
        for (unsigned y = 0; y < frame.plan.extent.height; ++y)
            for (unsigned x = 0; x < frame.plan.extent.width; ++x) {
                const auto index = y * frame.plan.extent.width + x;
                const bool covered = frame.foreground.covers(x, y);
                const float u = (x + .5f) / frame.plan.extent.width;
                const float v = (y + .5f) / frame.plan.extent.height;
                auto ray = frame.ray(u, v);
                ray.maxDistance = std::min(ray.maxDistance, double(frame.opaqueDistance(u, v)));
                CloudRayIntervals intervals;
                assert(CloudContract::intersect(frame.layer, ray, intervals).success);
                vacuum_.push_back(intervals.count == 0);
                if (!covered)
                    continue;
                coveredCloudPixels += intervals.count != 0;
                if (clippedValid_[index])
                    continue;
                for (unsigned level = 0; level < 2; ++level) {
                    renderer::volume::MediumIntegral integral;
                    const auto& p = frame.parameters;
                    assert(reference
                            .integrate(ray,
                                level ? 512u : 256u,
                                level ? 64u : 32u,
                                {p.sunDirection.x, p.sunDirection.y, p.sunDirection.z},
                                {p.sunRadiance.x, p.sunRadiance.y, p.sunRadiance.z},
                                {p.ambientRadiance.x, p.ambientRadiance.y, p.ambientRadiance.z},
                                p.cameraTime.w,
                                integral,
                                .5f)
                            .success);
                    (level ? clippedFine_ : clippedReference_)[index] = {integral.radiance.x,
                        integral.radiance.y,
                        integral.radiance.z,
                        integral.transmittance};
                }
                clippedValid_[index] = 1;
            }
        convergence = {};
        for (std::size_t i = 0; i < fine_.size(); ++i) {
            const bool covered =
                frame.foreground.covers(i % frame.plan.extent.width, i / frame.plan.extent.width);
            const auto a = expected(i, covered, false);
            const auto b = expected(i, covered, true);
            for (unsigned c = 0; c < 4; ++c)
                convergence.add(a[c], b[c]);
        }
    }

    void evaluate(const SequenceOracle& frame,
        const renderer::cloud::CloudGpuSample* actualRaw,
        const renderer::cloud::CloudGpuSample* actualResolved,
        const std::byte* actualHdr,
        std::uint64_t rowBytes)
    {
        assert(frame.parameters.inverseViewProjection.m == referenceKey_.inverseViewProjection.m);
        assert(fine_.size() == std::size_t(frame.plan.extent.width) * frame.plan.extent.height);
        radiance = transmittance = hdr = rawRadiance = rawTransmittance = exposedHdr = {};
        exposedPixels = 0;
        SequenceOracle reconstructed, raw;
        for (auto* source : {&reconstructed, &raw}) {
            source->plan = frame.plan;
            source->parameters = frame.parameters;
            source->layer = frame.layer;
            source->foreground = frame.foreground;
        }
        reconstructed.fullResolved = frame.fullResolved;
        reconstructed.resolved.assign(actualResolved, actualResolved + frame.resolved.size());
        raw.resolved.assign(actualRaw, actualRaw + frame.plan.sampleCount);
        exposed_.clear();
        exposed_.reserve(fine_.size());
        for (unsigned y = 0; y < frame.plan.extent.height; ++y)
            for (unsigned x = 0; x < frame.plan.extent.width; ++x) {
                const auto index = y * frame.plan.extent.width + x;
                const bool covered = frame.foreground.covers(x, y);
                const bool exposed = !covered && previous_.covers(x, y);
                exposed_.push_back(exposed);
                exposedPixels += exposed;
                const auto expected = this->expected(index, covered, true);
                const auto value = reconstructed.integral(x, y), rawValue = raw.integral(x, y);
                transmittance.add(value[3], expected[3]);
                rawTransmittance.add(rawValue[3], expected[3]);
                std::array<std::uint16_t, 4> pixel;
                std::memcpy(
                    pixel.data(), actualHdr + y * rowBytes + x * sizeof(pixel), sizeof(pixel));
                const auto& scene = covered ? frame.foregroundColor : ForegroundFixture::background;
                assert(core::halfToFloat(pixel[3]) == scene[3]);
                for (unsigned c = 0; c < 3; ++c) {
                    radiance.add(value[c], expected[c]);
                    rawRadiance.add(rawValue[c], expected[c]);
                    const auto color = core::halfToFloat(pixel[c]);
                    const auto target = expected[c] + expected[3] * scene[c];
                    hdr.add(color, target);
                    if (exposed)
                        exposedHdr.add(color, target);
                    if (covered && vacuum_[index]) {
                        assert(pixel[c]
                                == core::floatToHalf(scene[c], core::HalfFloatRounding::NearestEven)
                            || pixel[c]
                                == core::floatToHalf(
                                    scene[c], core::HalfFloatRounding::TowardZero));
                        assert(value[c] == 0 && value[3] == 1);
                    }
                }
            }
        previous_ = frame.foreground;
    }

    bool passed() const
    {
        return convergence.within(convergenceRmsLimit, convergenceMaxLimit)
            && radiance.within(rmsLimit, maxLimit) && transmittance.within(rmsLimit, maxLimit)
            && hdr.within(rmsLimit, maxLimit)
            && (!exposedPixels || exposedHdr.within(rmsLimit, maxLimit));
    }

    void write(std::FILE* file, const SequenceOracle& frame) const
    {
        std::fprintf(file,
            ",\"quality\":{\"contractVersion\":1,\"referenceSteps\":[256,32],\"convergedSteps\":[512,64],\"rmsLimit\":%.9g,\"maxLimit\":%.9g,\"convergenceRmsLimit\":%.9g,\"convergenceMaxLimit\":%.9g,\"passed\":%s,\"exposedPixels\":%u",
            rmsLimit,
            maxLimit,
            convergenceRmsLimit,
            convergenceMaxLimit,
            passed() ? "true" : "false",
            exposedPixels);
        const std::array errors{&radiance,
            &transmittance,
            &hdr,
            &rawRadiance,
            &rawTransmittance,
            &exposedHdr,
            &convergence};
        constexpr std::array names{"radiance",
            "transmittance",
            "hdr",
            "rawRadiance",
            "rawTransmittance",
            "exposedHdr",
            "convergence"};
        for (unsigned i = 0; i < errors.size(); ++i) {
            std::fprintf(file, ",\"%s\":", names[i]);
            errors[i]->write(file);
        }
        std::fprintf(file,
            "},\"qualityDepthClipped\":true,\"coveredCloudPixels\":%u,\"qualityVacuumMask\":[",
            coveredCloudPixels);
        for (std::size_t i = 0; i < vacuum_.size(); ++i)
            std::fprintf(file, "%s%u", i ? "," : "", unsigned(vacuum_[i]));
        std::fprintf(file, "],\"qualityExposedMask\":[");
        for (std::size_t i = 0; i < exposed_.size(); ++i)
            std::fprintf(file, "%s%u", i ? "," : "", unsigned(exposed_[i]));
        for (unsigned level = 0; level < 2; ++level) {
            std::fprintf(file, "],\"%s\":[", level ? "qualityFineReference" : "qualityReference");
            for (unsigned y = 0; y < frame.plan.extent.height; ++y)
                for (unsigned x = 0; x < frame.plan.extent.width; ++x) {
                    const auto index = y * frame.plan.extent.width + x;
                    const auto value = expected(index, frame.foreground.covers(x, y), level != 0);
                    std::fprintf(file,
                        "%s[%.9g,%.9g,%.9g,%.9g]",
                        index ? "," : "",
                        double(value[0]),
                        double(value[1]),
                        double(value[2]),
                        double(value[3]));
                }
        }
        std::fprintf(file, "]");
    }

private:
    std::array<float, 4> expected(std::size_t index, bool covered, bool fine) const
    {
        return covered
            ? SequenceOracle::components((fine ? clippedFine_ : clippedReference_)[index])
            : SequenceOracle::components((fine ? fine_ : reference_)[index].radianceTransmittance);
    }

    renderer::cloud::CloudGpuConstants referenceKey_{};
    ForegroundFixture previous_;
    core::pmr::Vector<renderer::cloud::CloudGpuSample> reference_, fine_;
    core::pmr::Vector<core::Vec4> clippedReference_, clippedFine_;
    core::pmr::Vector<unsigned char> exposed_, clippedValid_, vacuum_;
};

} // namespace arp::tests::cloud
