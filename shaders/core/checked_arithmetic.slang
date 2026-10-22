// CheckedArithmetic/1: exact uint32 arithmetic on SPIR-V, MSL and WGSL.
// No uint64 capability or dynamic integer division is required.
bool arpUintMultiplyFits(uint a, uint b)
{
    // Base 2^16 limbs. Each partial product fits uint32. If either cross
    // product exceeds one limb, or both high limbs are nonzero, a*b cannot
    // fit. Otherwise the carry sum is at most 3*65535 and cannot wrap.
    uint aLow = a & 0xffffu;
    uint aHigh = a >> 16u;
    uint bLow = b & 0xffffu;
    uint bHigh = b >> 16u;
    uint crossA = aHigh * bLow;
    uint crossB = aLow * bHigh;
    return !(aHigh != 0u && bHigh != 0u)
        && crossA <= 0xffffu && crossB <= 0xffffu
        && ((aLow * bLow) >> 16u) + crossA + crossB <= 0xffffu;
}
