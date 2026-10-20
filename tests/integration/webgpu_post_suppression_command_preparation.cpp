// Explicit browser-contract build only. This is the original implementation;
// its negative probe cannot deliver callbacks, publish bytes or issue proof.
#if !defined(ARP_RHI_WEBGPU_POST_SUPPRESSION_CONTRACT_PROBES)
#error "Instrumented command preparation must never be a default build input"
#endif
#include "../../src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp"
#include "../fixtures/webgpu_post_suppression_failure_probe_fixture.h"
