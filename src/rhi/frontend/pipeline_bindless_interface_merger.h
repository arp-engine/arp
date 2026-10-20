#pragma once
#include "rhi/gpu_pipeline.h"

namespace arp::rhi {
// Same selected-stage merge as ordinary Graphics, with the existing explicit
// Bindless package validator. Metadata composition issues no execution ticket.
ARP_API ValidationResult mergeBindlessGraphicsShaderInterface(std::uint32_t version,
    const ShaderPackageDesc& vertex,
    const ShaderPackageDesc& fragment,
    ShaderPackageDesc& output);
} // namespace arp::rhi
