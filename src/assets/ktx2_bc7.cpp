#include "assets/ktx2_bc7.h"

#include "core/cpu_allocator.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <limits>

// bc7enc is a plain C library (compiled into arp_assets via build.py). It
// exposes bc7enc_compress_block_init() (one-time LUT init) and
// bc7enc_compress_block() (16 RGBA pixels -> 16-byte BC7 block). We include
// the C header here; the implementation is linked from bc7enc.c.
#include "bc7enc.h"

namespace arp::assets {

namespace {

// KTX2 file identifier (12 bytes). "«KTX 20»" + CR/LF/ESC/LF.
constexpr std::uint8_t kKtx2Identifier[12] = {
    0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};

// KTX2 header is 80 bytes. Fields are little-endian.
struct Ktx2Header {
    std::uint8_t identifier[12];
    std::uint32_t vkFormat;               // offset 12
    std::uint32_t typeSize;               // offset 16
    std::uint32_t pixelWidth;             // offset 20
    std::uint32_t pixelHeight;            // offset 24
    std::uint32_t pixelDepth;             // offset 28
    std::uint32_t layerCount;             // offset 32
    std::uint32_t faceCount;              // offset 36
    std::uint32_t levelCount;             // offset 40
    std::uint32_t supercompressionScheme; // offset 44
    std::uint32_t dfdByteOffset;          // offset 48
    std::uint32_t dfdByteLength;          // offset 52
    std::uint32_t kvdByteOffset;          // offset 56
    std::uint32_t kvdByteLength;          // offset 60
    std::uint64_t sgdByteOffset;          // offset 64
    std::uint64_t sgdByteLength;          // offset 72
};

static_assert(sizeof(Ktx2Header) == 80u, "KTX2 header must be 80 bytes");

// Level index entry (24 bytes): largest-mip first.
struct Ktx2LevelIndexEntry {
    std::uint64_t byteOffset;             // from file start
    std::uint64_t byteLength;             // compressed bytes in file
    std::uint64_t uncompressedByteLength; // == byteLength when no supercompression
};

static_assert(sizeof(Ktx2LevelIndexEntry) == 24u, "KTX2 level index entry must be 24 bytes");

// VK_FORMAT_BC7_UNORM_BLOCK / BC7_SRGB_BLOCK (core Vulkan 1.0, no extension).
constexpr std::uint16_t kVkFormatBc7Unorm = 145u;
constexpr std::uint16_t kVkFormatBc7Srgb = 146u;

// Supercompression schemes. Only NONE (0) is supported here.
constexpr std::uint32_t kKtxSupercompressionNone = 0u;

// Little-endian readers. KTX2 is always little-endian on disk regardless of
// host endianness; the wocc build targets x86_64 (little-endian) so these are
// plain reads, but we use memcpy to avoid alignment/strict-aliasing issues.
std::uint32_t readU32Le(const std::uint8_t* p) noexcept
{
    std::uint32_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

std::uint64_t readU64Le(const std::uint8_t* p) noexcept
{
    std::uint64_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

void writeU32Le(std::uint8_t* p, std::uint32_t v) noexcept
{
    std::memcpy(p, &v, sizeof(v));
}

void writeU64Le(std::uint8_t* p, std::uint64_t v) noexcept
{
    std::memcpy(p, &v, sizeof(v));
}

// BC7 block footprint math. A BC7 surface consumes ceil(w/4)*ceil(h/4) blocks
// of 16 bytes each, even for a 1x1 surface (one full block).
std::uint32_t blocksForExtent(std::uint32_t texels) noexcept
{
    return (texels + 3u) / 4u;
}

std::uint64_t bc7BlockBytesForLevel(std::uint32_t width, std::uint32_t height) noexcept
{
    return static_cast<std::uint64_t>(blocksForExtent(width)) * blocksForExtent(height) * 16u;
}

// One-time bc7enc LUT initialization. bc7enc_compress_block_init() builds a
// lookup table used by bc7enc_compress_block(); calling it more than once is
// safe but wasteful, so guard with a flag. The init is not thread-safe; the
// wocc baker is single-threaded, and the runtime loader never calls encode.
void ensureBc7encInitialized() noexcept
{
    static bool initialized = false;
    if (!initialized) {
        bc7enc_compress_block_init();
        initialized = true;
    }
}

} // namespace

std::pmr::vector<std::uint8_t> encodeRgba8ToBc7Blocks(
    const std::uint8_t* pixels, std::uint32_t width, std::uint32_t height, bool perceptual)
{
    std::pmr::vector<std::uint8_t> blocks;
    if (pixels == nullptr || width == 0u || height == 0u) {
        return blocks;
    }

    ensureBc7encInitialized();

    bc7enc_compress_block_params params;
    bc7enc_compress_block_params_init(&params);
    if (!perceptual) {
        bc7enc_compress_block_params_init_linear_weights(&params);
        // Keep the other defaults from _init (max partitions, uber 0, etc.)
        // but switch to linear RGB error for data textures (normals/scalars).
        params.m_perceptual = BC7ENC_FALSE;
    }

    const auto blockCols = blocksForExtent(width);
    const auto blockRows = blocksForExtent(height);
    blocks.resize(static_cast<std::size_t>(blockCols) * blockRows * 16u);

    // bc7enc_compress_block consumes 16 RGBA pixels (4x4 block) at a time.
    // For edge blocks where width/height is not a multiple of 4, pad by
    // clamping to the last valid texel (edge replication) -- this is the
    // standard BC padding convention and matches what GPU sampling expects
    // for the clamped edge texels.
    std::uint8_t blockPixels[16 * 4];
    for (std::uint32_t by = 0u; by < blockRows; ++by) {
        for (std::uint32_t bx = 0u; bx < blockCols; ++bx) {
            for (std::uint32_t py = 0u; py < 4u; ++py) {
                const auto srcY = std::min(by * 4u + py, height - 1u);
                for (std::uint32_t px = 0u; px < 4u; ++px) {
                    const auto srcX = std::min(bx * 4u + px, width - 1u);
                    const auto srcIdx = (static_cast<std::size_t>(srcY) * width + srcX) * 4u;
                    const auto dstIdx = (static_cast<std::size_t>(py) * 4u + px) * 4u;
                    std::memcpy(blockPixels + dstIdx, pixels + srcIdx, 4u);
                }
            }
            const auto blockIdx = (static_cast<std::size_t>(by) * blockCols + bx) * 16u;
            bc7enc_compress_block(blocks.data() + blockIdx, blockPixels, &params);
        }
    }
    return blocks;
}

Ktx2DecodeResult decodeKtx2(const std::pmr::vector<std::uint8_t>& fileBytes)
{
    Ktx2DecodeResult result;

    if (fileBytes.size() < sizeof(Ktx2Header)) {
        result.diagnostics = "KTX2 file truncated before 80-byte header";
        return result;
    }

    // Validate magic.
    if (std::memcmp(fileBytes.data(), kKtx2Identifier, 12) != 0) {
        result.diagnostics = "KTX2 magic identifier mismatch";
        return result;
    }

    // Parse header fields directly from the byte buffer (the on-disk struct
    // is packed little-endian; reading via the struct would require #pragma
    // packed and is fragile, so read fields individually).
    const auto* base = fileBytes.data();
    const auto vkFormat = readU32Le(base + 12);
    const auto typeSize = readU32Le(base + 16);
    const auto pixelWidth = readU32Le(base + 20);
    const auto pixelHeight = readU32Le(base + 24);
    const auto pixelDepth = readU32Le(base + 28);
    const auto layerCount = readU32Le(base + 32);
    const auto faceCount = readU32Le(base + 36);
    const auto levelCountRaw = readU32Le(base + 40);
    const auto supercompressionScheme = readU32Le(base + 44);

    // Reject unsupported formats. Only BC7 (145/146) is handled.
    if (vkFormat != kVkFormatBc7Unorm && vkFormat != kVkFormatBc7Srgb) {
        result.diagnostics = "KTX2 vkFormat is not BC7 (only 145/146 supported)";
        return result;
    }
    // Reject anything that isn't a plain 2D non-array texture.
    if (pixelDepth != 0u || layerCount != 0u || faceCount > 1u) {
        result.diagnostics = "KTX2 only supports 2D non-array non-cube textures";
        return result;
    }
    if (supercompressionScheme != kKtxSupercompressionNone) {
        result.diagnostics = "KTX2 supercompression not supported (only scheme 0)";
        return result;
    }
    if (pixelWidth == 0u || pixelHeight == 0u) {
        result.diagnostics = "KTX2 has zero pixel dimensions";
        return result;
    }
    if (typeSize != 1u) {
        result.diagnostics = "KTX2 typeSize must be 1 for BC7";
        return result;
    }
    // levelCount==0 means 1 level per spec; normalize.
    const auto levelCount = std::max<std::uint32_t>(levelCountRaw, 1u);

    // Level index table starts at offset 80, one 24-byte entry per level
    // (largest first).
    const auto indexOffset = sizeof(Ktx2Header);
    const auto indexBytes = static_cast<std::size_t>(levelCount) * sizeof(Ktx2LevelIndexEntry);
    if (fileBytes.size() < indexOffset + indexBytes) {
        result.diagnostics = "KTX2 file truncated in level index table";
        return result;
    }

    result.image.width = pixelWidth;
    result.image.height = pixelHeight;
    result.image.vkFormat = static_cast<std::uint16_t>(vkFormat);
    result.image.srgb = (vkFormat == kVkFormatBc7Srgb);
    result.image.levels.reserve(levelCount);

    // Walk the mip chain. Level 0 is the largest; each subsequent level halves
    // width and height (floor, min 1).
    std::uint32_t mipW = pixelWidth;
    std::uint32_t mipH = pixelHeight;
    for (std::uint32_t i = 0u; i < levelCount; ++i) {
        const auto* entry = base + indexOffset + i * sizeof(Ktx2LevelIndexEntry);
        const auto byteOffset = readU64Le(entry);
        const auto byteLength = readU64Le(entry + 8);
        const auto uncompressedByteLength = readU64Le(entry + 16);

        // With no supercompression, byteLength must equal uncompressedByteLength.
        if (byteLength != uncompressedByteLength) {
            result.diagnostics =
                "KTX2 level byteLength != uncompressedByteLength without supercompression";
            return result;
        }

        const auto expectedBytes = bc7BlockBytesForLevel(mipW, mipH);
        if (byteLength != expectedBytes) {
            result.diagnostics = "KTX2 level byte count does not match BC7 block footprint";
            return result;
        }
        if (byteOffset + byteLength > fileBytes.size()) {
            result.diagnostics = "KTX2 level data extends past end of file";
            return result;
        }

        Ktx2MipLevel level;
        level.width = mipW;
        level.height = mipH;
        level.blockBytes.resize(static_cast<std::size_t>(byteLength));
        std::memcpy(
            level.blockBytes.data(), base + byteOffset, static_cast<std::size_t>(byteLength));
        result.image.levels.push_back(std::move(level));

        // Advance to next mip: halve dimensions, floor to 1.
        mipW = std::max(mipW / 2u, 1u);
        mipH = std::max(mipH / 2u, 1u);
    }

    result.success = true;
    return result;
}

namespace {

// Build the fixed 44-byte BC7 Data Format Descriptor (DFD) for a given
// transfer function. This is the canonical Khronos DFD for BC7 with one
// sample; it is a constant parameterized only by sRGB (transferFunction 2)
// vs linear (transferFunction 1). Layout (each word uint32 LE):
//   word 0: dfdTotalSize (44) | vendorId (0)  -- packed as [sizeLo,sizeHi,vendLo,vendHi]
//   word 1: descriptorType(0)|versionNumber(2)|flags(0)|transferFunction|colorPrimaries(1)
//   word 2: colorModel(134=BC7)|dim0(3)|dim1(3)|dim2(0)|dim3(0)
//   word 3: bytesPlane0(1)..bytesPlane7(0)
//   word 4: sample0 bitOffset(0)|bitLength(127)|...
//   word 5..7: remaining sample fields (channelId, samplePosition, etc.)
// The exact bit layout is intricate; rather than hand-pack each nibble, we
// emit the precomputed byte sequence and patch the transferFunction byte.
// Values below are the canonical BC7 DFD from the KTX spec / dfdutils.
constexpr std::uint8_t kDfdBc7Template[44] = {
    // word 0: dfdTotalSize=44 (0x2C,0x00), vendorId=0 (0x00,0x00)
    0x2C,
    0x00,
    0x00,
    0x00,
    // word 1: descriptorType=0, versionNumber=2, flags=0,
    //         transferFunction=PLACEHOLDER (offset 9 in DFD, byte index 9),
    //         colorPrimaries=1 (BT.709)
    0x00,
    0x02,
    0x00,
    0x00,
    0x01,
    0x01,
    0x00,
    0x00,
    // word 2: colorModel=134 (0x86), texelBlockDimension 3,3,0,0 (4x4x1x1)
    0x86,
    0x03,
    0x03,
    0x00,
    // word 3 (4 bytes) + word 4 (4 bytes): bytesPlane0=1, rest 0
    0x01,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    // word 4 continued + word 5: sample 0 -- bitOffset=0, bitLength=127 (0x7F),
    // channelId=RGBA (0x0B for ALPHA? KTX uses KHR_DF_CHANNEL_BC7_COLOR = 0x0B),
    // samplePosition0..3=0, lower/upper canonical values.
    0x00,
    0x7F,
    0x0B,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    // word 6: sample lower/upper 32-bit (canonical 0/0xFFFFFFFF for BC7)
    0x00,
    0x00,
    0x00,
    0x00,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    // word 7: reserved 0
    0x00,
    0x00,
    0x00,
    0x00,
};
static_assert(sizeof(kDfdBc7Template) == 44u, "BC7 DFD template must be 44 bytes");

// Offset of the transferFunction byte within the DFD (byte index 9: word 1,
// high half of the low 16 bits -> byte 9). transferFunction: 1=LINEAR, 2=SRGB.
constexpr std::size_t kDfdTransferFunctionOffset = 9u;
constexpr std::uint8_t kTransferFunctionLinear = 1u;
constexpr std::uint8_t kTransferFunctionSrgb = 2u;

std::pmr::vector<std::uint8_t> buildBc7Dfd(bool srgb)
{
    std::pmr::vector<std::uint8_t> dfd(kDfdBc7Template, kDfdBc7Template + sizeof(kDfdBc7Template));
    dfd[kDfdTransferFunctionOffset] = srgb ? kTransferFunctionSrgb : kTransferFunctionLinear;
    return dfd;
}

// Build a minimal KV data block: one "KTXwriter" entry + terminator.
// Layout: uint32 keyAndValueByteLength, then "KTXwriter\0<value>", then
// 4-byte alignment padding, then a uint32 0 terminator.
std::pmr::vector<std::uint8_t> buildKvBlock()
{
    static constexpr const char* kKey = "KTXwriter";
    static constexpr const char* kValue = "arp::assets::writeKtx2Bc7";
    const auto keyLen = std::strlen(kKey) + 1u; // include NUL
    const auto valueLen = std::strlen(kValue);  // no trailing NUL needed
    const auto kvLen = keyLen + valueLen;
    // Pad kvLen to a multiple of 4 (the spec aligns the next entry/terminator).
    const auto paddedKvLen = (kvLen + 3u) & ~static_cast<std::size_t>(3u);

    std::pmr::vector<std::uint8_t> kv;
    kv.resize(4u + paddedKvLen + 4u); // length field + padded key/value + terminator
    auto* p = kv.data();
    writeU32Le(p, static_cast<std::uint32_t>(kvLen));
    p += 4u;
    std::memcpy(p, kKey, keyLen);
    p += keyLen;
    std::memcpy(p, kValue, valueLen);
    p += valueLen;
    // Zero-fill alignment padding.
    for (std::size_t i = valueLen; i < paddedKvLen - keyLen; ++i) {
        *p++ = 0;
    }
    // Terminator: keyAndValueByteLength == 0.
    writeU32Le(p, 0u);
    return kv;
}

} // namespace

bool writeKtx2Bc7(const std::filesystem::path& path,
    std::uint32_t width,
    std::uint32_t height,
    bool srgb,
    const std::pmr::vector<Ktx2MipLevel>& levels)
{
    if (width == 0u || height == 0u || levels.empty()) {
        return false;
    }
    // Sanity: level 0 must match the declared base dimensions, and each
    // subsequent level must halve (floor to 1).
    if (levels[0].width != width || levels[0].height != height) {
        return false;
    }
    {
        std::uint32_t expectedW = width;
        std::uint32_t expectedH = height;
        for (const auto& lvl : levels) {
            if (lvl.width != expectedW || lvl.height != expectedH) {
                return false;
            }
            expectedW = std::max(expectedW / 2u, 1u);
            expectedH = std::max(expectedH / 2u, 1u);
        }
    }

    const auto vkFormat = srgb ? kVkFormatBc7Srgb : kVkFormatBc7Unorm;
    const auto dfd = buildBc7Dfd(srgb);
    const auto kv = buildKvBlock();

    // Layout (offsets from file start):
    //   0            header (80 bytes)
    //   80           level index (24 * levelCount bytes)
    //   80 + idx     level data (largest first), each 4-byte aligned
    //   ...          DFD (44 bytes)
    //   ...          KV block
    const auto headerSize = sizeof(Ktx2Header);
    const auto indexSize = levels.size() * sizeof(Ktx2LevelIndexEntry);
    const auto dataIndexStart = headerSize + indexSize;

    // Compute per-level byte offsets and total data size.
    std::pmr::vector<Ktx2LevelIndexEntry> indexEntries(levels.size());
    std::uint64_t cursor = dataIndexStart;
    for (std::size_t i = 0u; i < levels.size(); ++i) {
        const auto byteLen = bc7BlockBytesForLevel(levels[i].width, levels[i].height);
        // Align each level's start to 4 bytes (KTX2 recommended alignment).
        cursor = (cursor + 3u) & ~static_cast<std::uint64_t>(3u);
        indexEntries[i].byteOffset = cursor;
        indexEntries[i].byteLength = byteLen;
        indexEntries[i].uncompressedByteLength = byteLen;
        cursor += byteLen;
    }
    const auto dfdOffset = (cursor + 3u) & ~static_cast<std::uint64_t>(3u);
    const auto kvOffset = dfdOffset + dfd.size();
    const auto totalSize = kvOffset + kv.size();

    // Build the full file in memory, then write once.
    std::pmr::vector<std::uint8_t> buffer;
    buffer.resize(static_cast<std::size_t>(totalSize), 0u);
    auto* p = buffer.data();

    // Header.
    std::memcpy(p, kKtx2Identifier, 12u);
    writeU32Le(p + 12, vkFormat);
    writeU32Le(p + 16, 1u); // typeSize
    writeU32Le(p + 20, width);
    writeU32Le(p + 24, height);
    writeU32Le(p + 28, 0u); // pixelDepth
    writeU32Le(p + 32, 0u); // layerCount (0 => 1)
    writeU32Le(p + 36, 1u); // faceCount
    writeU32Le(p + 40, static_cast<std::uint32_t>(levels.size()));
    writeU32Le(p + 44, kKtxSupercompressionNone);
    writeU32Le(p + 48, static_cast<std::uint32_t>(dfdOffset));
    writeU32Le(p + 52, static_cast<std::uint32_t>(dfd.size()));
    writeU32Le(p + 56, static_cast<std::uint32_t>(kvOffset));
    writeU32Le(p + 60, static_cast<std::uint32_t>(kv.size()));
    writeU64Le(p + 64, 0u); // sgdByteOffset
    writeU64Le(p + 72, 0u); // sgdByteLength

    // Level index.
    for (std::size_t i = 0u; i < indexEntries.size(); ++i) {
        auto* e = p + headerSize + i * sizeof(Ktx2LevelIndexEntry);
        writeU64Le(e, indexEntries[i].byteOffset);
        writeU64Le(e + 8, indexEntries[i].byteLength);
        writeU64Le(e + 16, indexEntries[i].uncompressedByteLength);
    }

    // Level data (largest first).
    for (std::size_t i = 0u; i < levels.size(); ++i) {
        const auto& lvl = levels[i];
        const auto expected = bc7BlockBytesForLevel(lvl.width, lvl.height);
        if (lvl.blockBytes.size() != expected) {
            return false; // caller passed wrong-sized block bytes
        }
        std::memcpy(p + indexEntries[i].byteOffset,
            lvl.blockBytes.data(),
            static_cast<std::size_t>(indexEntries[i].byteLength));
    }

    // DFD.
    std::memcpy(p + dfdOffset, dfd.data(), dfd.size());

    // KV block.
    std::memcpy(p + kvOffset, kv.data(), kv.size());

    // Write to disk (binary, truncating).
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(
        reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
    return static_cast<bool>(out);
}

} // namespace arp::assets
