# DdgiCacheDepthPrecision/2

The screen irradiance cache stores raw [0,1] depth in the alpha channel of
Rgba16Float. The scene attachment uses D32Float. Cache ownership comparisons
must discount only the binary16 rounding interval before applying the existing
geometric depth-footprint tolerance. Quantization alone must never reject an
unchanged surface or periodically blend in the probe fallback.

The producer explicitly encodes depth with `f16tof32(f32tof16(depth))` before
the storage-image write. This does not guarantee round-to-nearest behavior.
Do not assume a native float storage write rounds to nearest: the Metal image
fixture observed 0.95 stored with error 0.000292957 (toward zero), exceeding
half an ULP. An arithmetic-only conversion fixture did not expose that path.

For a stored finite nonnegative depth `s`, let
`ulp = 2^(max(floor(log2(s)), -14) - 10)`. Its source interval is conservatively
`[s - 0.5*ulp, s + ulp]`: the union of nearest and toward-zero conversion.
At zero/subnormal values the ULP is 2^-24. Compare the scene depth's distance
to this interval, then apply the existing geometric footprint tolerance.
Surface differences beyond it still receive depth and geometry-normal rejection.

The Windows Vulkan Sponza capture on 2026-10-15 exposed an error of about
0.000448 at pixel (312,400), larger than the old half-ULP allowance 0.000244.
Across the same planar stone surface, the old confidence periodically drops
to zero and switches from screen history to probe sampling. Replacing only
this decoder in that capture removes the bands with DDGI and CSM enabled.

Null validates the unchanged descriptors; Vulkan, Metal and WebGPU execute the
same Slang arithmetic against the declared Rgba16Float/D32Float resources.
There is no format substitution, new allocation, resource binding, or disabled
GI path. The native Metal sampling fixture sweeps quantized same-surface depths
and disocclusions. Windows Vulkan replay and fresh full-scene validation are
recorded separately in the Sponza stripe diagnosis report. A Windows replay
does not establish native Metal or WebGPU scene validation.
