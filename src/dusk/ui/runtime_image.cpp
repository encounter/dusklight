#include "runtime_image.hpp"

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_surface.h>
#include <borealis/log.hpp>

#include <cstddef>
#include <cstring>

namespace dusk::ui {
namespace {

constexpr borealis::Log Log{"dusk::ui"};
constexpr uint32_t kMaxImageDimension = 4096;

}  // namespace

std::optional<DecodedImage> decode_png(std::span<const uint8_t> data, std::string_view source) {
    SDL_IOStream* stream = SDL_IOFromConstMem(data.data(), data.size());
    if (stream == nullptr) {
        Log.warn("Failed to open image stream for '{}': {}", source, SDL_GetError());
        return std::nullopt;
    }

    SDL_Surface* loadedSurface = SDL_LoadPNG_IO(stream, true);
    if (loadedSurface == nullptr) {
        Log.warn("Failed to decode image '{}': {}", source, SDL_GetError());
        return std::nullopt;
    }

    SDL_Surface* rgbaSurface = SDL_ConvertSurface(loadedSurface, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loadedSurface);
    if (rgbaSurface == nullptr) {
        Log.warn("Failed to convert image '{}': {}", source, SDL_GetError());
        return std::nullopt;
    }

    const auto width = static_cast<uint32_t>(rgbaSurface->w);
    const auto height = static_cast<uint32_t>(rgbaSurface->h);
    if (width == 0 || height == 0 || width > kMaxImageDimension || height > kMaxImageDimension) {
        Log.warn("Image '{}' has unsupported dimensions {}x{}", source, width, height);
        SDL_DestroySurface(rgbaSurface);
        return std::nullopt;
    }

    const size_t rowSize = static_cast<size_t>(width) * 4;
    DecodedImage image{
        .pixels = std::vector<uint8_t>(rowSize * height),
        .width = width,
        .height = height,
    };
    for (uint32_t row = 0; row < height; ++row) {
        const auto* src = static_cast<const uint8_t*>(rgbaSurface->pixels) +
                          static_cast<size_t>(row) * static_cast<size_t>(rgbaSurface->pitch);
        auto* dst = image.pixels.data() + static_cast<size_t>(row) * rowSize;
        std::memcpy(dst, src, rowSize);

        for (size_t col = 0; col < rowSize; col += 4) {
            const uint8_t alpha = dst[col + 3];
            for (size_t channel = 0; channel < 3; ++channel) {
                dst[col + channel] =
                    static_cast<uint8_t>((static_cast<uint32_t>(dst[col + channel]) * alpha) / 255);
            }
        }
    }

    SDL_DestroySurface(rgbaSurface);
    return image;
}

}  // namespace dusk::ui
