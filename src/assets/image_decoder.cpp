#include "assets/image_decoder.h"

#include "core/memory/cpu_allocator.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>

namespace {

bool tgaMatchesPayload(arp::core::ArrayView<const std::uint8_t> bytes) noexcept;

bool mimeMatchesPayload(
    const arp::core::ArrayView<const std::uint8_t> bytes, const std::pmr::string& mimeType)
{
    if (mimeType.empty())
        return true;
    if (mimeType == "image/png") {
        constexpr std::uint8_t signature[] = {
            0x89u,
            0x50u,
            0x4eu,
            0x47u,
            0x0du,
            0x0au,
            0x1au,
            0x0au,
        };
        return bytes.size() >= sizeof(signature)
            && std::memcmp(bytes.data(), signature, sizeof(signature)) == 0;
    }
    if (mimeType == "image/jpeg") {
        return bytes.size() >= 3u && bytes[0] == 0xffu && bytes[1] == 0xd8u && bytes[2] == 0xffu;
    }
    if (mimeType == "image/webp") {
        return bytes.size() >= 12u && std::memcmp(bytes.data(), "RIFF", 4u) == 0
            && std::memcmp(bytes.data() + 8u, "WEBP", 4u) == 0;
    }
    if (mimeType == "image/vnd.adobe.photoshop") {
        return bytes.size() >= 4u && std::memcmp(bytes.data(), "8BPS", 4u) == 0;
    }
    if (mimeType == "image/tga")
        return tgaMatchesPayload(bytes);
    return false;
}

} // namespace

namespace {

void* stbImageAllocate(std::size_t sizeBytes)
{
    arp::core::CpuAllocationDesc desc;
    desc.sizeBytes = sizeBytes;
    desc.alignment = alignof(std::max_align_t);
    desc.intent = arp::core::CpuAllocationIntent::ImageDecode;
    desc.label = "stb_image";
    return arp::core::defaultCpuAllocator().allocate(desc).ptr;
}

void stbImageFree(void* ptr) noexcept
{
    if (!ptr) {
        return;
    }
    arp::core::CpuAllocationDesc desc;
    desc.alignment = alignof(std::max_align_t);
    desc.intent = arp::core::CpuAllocationIntent::ImageDecode;
    desc.label = "stb_image";
    arp::core::defaultCpuAllocator().deallocate(ptr, desc);
}

void* stbImageReallocateSized(void* ptr, std::size_t oldSizeBytes, std::size_t newSizeBytes)
{
    if (newSizeBytes == 0u) {
        stbImageFree(ptr);
        return nullptr;
    }

    void* resized = stbImageAllocate(newSizeBytes);
    if (!resized) {
        return nullptr;
    }

    if (ptr) {
        std::memcpy(resized, ptr, std::min(oldSizeBytes, newSizeBytes));
        stbImageFree(ptr);
    }
    return resized;
}

} // namespace

#define STBI_MALLOC(sz) stbImageAllocate(sz)
#define STBI_REALLOC_SIZED(p, oldsz, newsz) stbImageReallocateSized(p, oldsz, newsz)
#define STBI_FREE(p) stbImageFree(p)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "stb_image.h"

namespace {

bool tgaMatchesPayload(arp::core::ArrayView<const std::uint8_t> bytes) noexcept
{
    stbi__context context;
    stbi__start_mem(&context, bytes.data(), static_cast<int>(bytes.size()));
    return stbi__tga_test(&context) != 0;
}

// STB pixels are core-allocated; copying them into a result may still throw.
class StbImagePixels final {
public:
    explicit StbImagePixels(unsigned char* pixels) noexcept : pixels_(pixels) {}

    ~StbImagePixels() { stbi_image_free(pixels_); }

    StbImagePixels(const StbImagePixels&) = delete;
    StbImagePixels& operator=(const StbImagePixels&) = delete;

    unsigned char* data() const noexcept { return pixels_; }

private:
    unsigned char* pixels_;
};

} // namespace

namespace arp::assets {

ImageDecodeResult decodeImageToRgba8(
    const std::pmr::vector<uint8_t>& encodedBytes, const std::pmr::string& mimeType, bool srgb)
{
    return decodeImageToRgba8(
        core::ArrayView<const uint8_t>(encodedBytes.data(), encodedBytes.size()),
        mimeType,
        srgb,
        0u);
}

ImageDecodeResult decodeImageToRgba8(const core::ArrayView<const uint8_t> encodedBytes,
    const std::pmr::string& mimeType,
    const bool srgb,
    const std::size_t maxDecodedBytes)
{
    ImageDecodeResult result;
    if (encodedBytes.empty()) {
        result.diagnostics = "image decode input is empty";
        return result;
    }
    if (!mimeType.empty() && mimeType != "image/png" && mimeType != "image/jpeg"
        && mimeType != "image/webp" && mimeType != "image/tga"
        && mimeType != "image/vnd.adobe.photoshop") {
        result.diagnostics = "image mime type is not supported";
        return result;
    }
    if (encodedBytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        result.diagnostics = "image decode input is too large";
        return result;
    }
    if (maxDecodedBytes != 0u && !mimeMatchesPayload(encodedBytes, mimeType)) {
        result.diagnostics = "image MIME type does not match encoded payload";
        return result;
    }

    int width = 0;
    int height = 0;
    int sourceComponents = 0;
    if (stbi_info_from_memory(encodedBytes.data(),
            static_cast<int>(encodedBytes.size()),
            &width,
            &height,
            &sourceComponents)
            == 0
        || width <= 0 || height <= 0) {
        result.diagnostics = "stb_image failed to inspect image dimensions";
        return result;
    }
    const auto widthSize = static_cast<std::size_t>(width);
    const auto heightSize = static_cast<std::size_t>(height);
    if (heightSize != 0u && widthSize > std::numeric_limits<std::size_t>::max() / heightSize) {
        result.diagnostics = "decoded image pixel count overflows";
        return result;
    }
    const auto pixelCount = widthSize * heightSize;
    if (pixelCount > std::numeric_limits<std::size_t>::max() / 4u) {
        result.diagnostics = "decoded image byte count overflows";
        return result;
    }
    const auto pixelBytes = pixelCount * 4u;
    if (maxDecodedBytes != 0u && pixelBytes > maxDecodedBytes) {
        result.diagnostics = "decoded image exceeds byte budget";
        return result;
    }

    StbImagePixels decoded(stbi_load_from_memory(encodedBytes.data(),
        static_cast<int>(encodedBytes.size()),
        &width,
        &height,
        &sourceComponents,
        4));
    if (decoded.data() == nullptr || width <= 0 || height <= 0) {
        result.diagnostics = "stb_image failed to decode image";
        return result;
    }

    result.image.width = static_cast<uint32_t>(width);
    result.image.height = static_cast<uint32_t>(height);
    result.image.srgb = srgb;
    result.image.alpha = sourceComponents == 2 || sourceComponents == 4;
    result.image.rgba8.assign(decoded.data(), decoded.data() + pixelBytes);
    result.success = true;
    return result;
}

} // namespace arp::assets
