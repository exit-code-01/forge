// engine/src/assets/ImageLoader.cpp — the ONLY TU that includes stb_image.
#include "forge/assets/Assets.hpp"
#include "forge/core/Log.hpp"

// stb_image's IMPLEMENTATION compiles inside this TU, so our warning wall
// would fire on stb's code — shield it; stb's warnings are not our problem.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244 4245 4100 4456 4457)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include <stb_image.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include <stdexcept>
#include <string>

namespace forge::assets {

ImageData loadImage(const std::string& path) {
    int width = 0;
    int height = 0;
    int sourceChannels = 0;
    // Force RGBA8: one texel format through the whole engine (ADR-013).
    stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &sourceChannels, STBI_rgb_alpha);
    if (pixels == nullptr) {
        throw std::runtime_error("image import failed: " + path + " (" + stbi_failure_reason() +
                                 ")");
    }

    ImageData out;
    out.width = static_cast<uint32_t>(width);
    out.height = static_cast<uint32_t>(height);
    out.rgba.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
    stbi_image_free(pixels);

    FORGE_INFO("image imported: {} ({}x{}, {} source channels)", path, width, height,
               sourceChannels);
    return out;
}

} // namespace forge::assets
