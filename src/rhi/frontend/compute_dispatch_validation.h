#pragma once

#include "core/math/unsigned_range.h"
#include "rhi/gpu_resource_types.h"
#include "rhi/semantics/compute_dispatch_validation_lowering.h"

namespace arp::rhi {

struct ComputeDispatchValidationBudget {
    std::uint64_t maxNativeResultBytes = 0u;
    std::uint64_t maxDispatches = 0u, maxWorkgroups = 0u, maxThreadInvocations = 0u;
    std::uint64_t maxResultChecks = 0u, maxQueueSubmissions = 0u;
    std::uint64_t maxColdValidationNanoseconds = 0u;
};

struct ComputeDispatchValidationRequest {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    Extent3D workgroup{0u, 0u, 0u}, groups{0u, 0u, 0u};
    ComputeDispatchValidationBudget budget;
};

// Pure declaration/costs. Public, copyable and deliberately not a native receipt.
struct ComputeDispatchValidationPlan {
    Extent3D workgroup{0u, 0u, 0u}, groups{0u, 0u, 0u};
    std::uint64_t dispatches = 0u, workgroups = 0u, threadInvocations = 0u;
    std::uint64_t resultCellsPerSlice = 0u, resultSliceBytes = 0u;
    std::uint64_t nativeResultBytes = 0u, resultChecks = 0u, queueSubmissions = 0u;
};

class ComputeDispatchValidationContract final {
public:
    // ComputeDispatchValidation/1 fixes the independently verified probe ABI.
    // These are wire-layout constants, not hardware limits or native defaults.
    static constexpr std::uint32_t workgroupWidth = 8u, workgroupHeight = 8u, workgroupDepth = 1u,
                                   threadCount = 64u;
    static constexpr std::uint64_t resultWordBytes = 4u, guardWords = 16u,
                                   resultSliceAlignmentBytes = 256u, resultCodeStride = 128u;

    static const char* inspect(const ComputeDispatchValidationRequest& request,
        ComputeDispatchValidationPlan& output) noexcept
    {
        output = {};
        if (request.contractVersion != 1u)
            return "RHI-DISPATCH-VALIDATION-VERSION";
        if (!request.prototypeOptIn)
            return "RHI-DISPATCH-VALIDATION-OPT-IN";
        const auto shape = request.workgroup;
        if (shape.width != workgroupWidth || shape.height != workgroupHeight
            || shape.depth != workgroupDepth)
            return "RHI-DISPATCH-VALIDATION-SHAPE";
        const auto groups = request.groups;
        if (!groups.width || !groups.height || !groups.depth)
            return "RHI-DISPATCH-VALIDATION-GROUPS";
        const auto& budget = request.budget;
        if (!budget.maxColdValidationNanoseconds || budget.maxColdValidationNanoseconds > INT64_MAX)
            return "RHI-DISPATCH-VALIDATION-TIMEOUT";
        ComputeDispatchValidationPlan next;
        next.workgroup = shape;
        next.groups = groups;
        auto cells = std::uint64_t(groups.width) * groups.height;
        // Every unique shader result must be representable, including count64.
        // Reject before multiplication by Z; zero and uint arithmetic differ.
        constexpr auto maxCells = (UINT32_MAX - threadCount) / resultCodeStride + 1u;
        if (cells > maxCells / groups.depth)
            return "RHI-DISPATCH-VALIDATION-RESULT-CODE-OVERFLOW";
        cells *= groups.depth;
        next.dispatches = next.resultCellsPerSlice = cells;
        const auto triangle = [](std::uint32_t extent) noexcept {
            return std::uint64_t(extent) * (std::uint64_t(extent) + 1u) / 2u;
        };
        const auto multiply = [](std::uint64_t a, std::uint64_t b, std::uint64_t& result) noexcept {
            if (!core::canMultiplyUnsigned(a, b))
                return false;
            result = a * b;
            return true;
        };
        if (!multiply(triangle(groups.width), triangle(groups.height), next.workgroups)
            || !multiply(next.workgroups, triangle(groups.depth), next.workgroups)
            || !multiply(next.workgroups, threadCount, next.threadInvocations))
            return "RHI-DISPATCH-VALIDATION-WORK-OVERFLOW";
        // One independent result slice for every Y/Z tuple; one command per X.
        // The bounded result-code check above also bounds this aligned sum.
        const auto rawBytes = (cells + guardWords) * resultWordBytes;
        next.resultSliceBytes = (rawBytes + resultSliceAlignmentBytes - 1u)
            / resultSliceAlignmentBytes * resultSliceAlignmentBytes;
        const auto slices = std::uint64_t(groups.height) * groups.depth;
        if (!multiply(next.resultSliceBytes, slices, next.nativeResultBytes)
            || !multiply(next.nativeResultBytes / resultWordBytes, groups.width, next.resultChecks))
            return "RHI-DISPATCH-VALIDATION-RESULT-SIZE-OVERFLOW";
        next.queueSubmissions = groups.width;
        if (next.dispatches > budget.maxDispatches)
            return "RHI-DISPATCH-VALIDATION-DISPATCH-BUDGET";
        if (next.workgroups > budget.maxWorkgroups)
            return "RHI-DISPATCH-VALIDATION-WORKGROUP-BUDGET";
        if (next.threadInvocations > budget.maxThreadInvocations)
            return "RHI-DISPATCH-VALIDATION-THREAD-BUDGET";
        if (next.nativeResultBytes > budget.maxNativeResultBytes)
            return "RHI-DISPATCH-VALIDATION-NATIVE-BUDGET";
        if (next.resultChecks > budget.maxResultChecks)
            return "RHI-DISPATCH-VALIDATION-CHECK-BUDGET";
        if (next.queueSubmissions > budget.maxQueueSubmissions)
            return "RHI-DISPATCH-VALIDATION-SUBMISSION-BUDGET";
        output = next;
        return nullptr;
    }

    // Coverage of a successfully declared plan only. A caller can construct a
    // plan, so this function cannot establish device or execution authority.
    static const char* contains(const ComputeDispatchValidationRequest& request,
        Extent3D workgroup,
        Extent3D groups) noexcept
    {
        ComputeDispatchValidationPlan checked;
        if (const auto* id = inspect(request, checked))
            return id;
        if (workgroup.width != checked.workgroup.width
            || workgroup.height != checked.workgroup.height
            || workgroup.depth != checked.workgroup.depth)
            return "RHI-DISPATCH-VALIDATION-SHAPE";
        return groups.width && groups.height && groups.depth && groups.width <= checked.groups.width
                && groups.height <= checked.groups.height && groups.depth <= checked.groups.depth
            ? nullptr
            : "RHI-DISPATCH-VALIDATION-OUTSIDE-DOMAIN";
    }
};

} // namespace arp::rhi
