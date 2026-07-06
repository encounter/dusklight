#include "registry.hpp"

#include "dusk/mods/manifest.hpp"

namespace dusk::mods::svc {
namespace {

#if defined(_MSC_VER) && !defined(__clang__)
#define DUSK_ABI_COMPILER "msvc"
#elif defined(__clang__)
#define DUSK_ABI_COMPILER "clang"
#else
#define DUSK_ABI_COMPILER "gcc"
#endif
#if defined(_M_ARM64) || defined(__aarch64__)
#define DUSK_ABI_ARCH "arm64"
#else
#define DUSK_ABI_ARCH "x64"
#endif
#if defined(_WIN32)
#define DUSK_ABI_OS "windows"
#elif defined(__APPLE__)
#define DUSK_ABI_OS "macos"
#elif defined(__ANDROID__)
#define DUSK_ABI_OS "android"
#else
#define DUSK_ABI_OS "linux"
#endif

GameService s_gameService{
    .header = SERVICE_HEADER(GameService, GAME_SERVICE_MAJOR, GAME_SERVICE_MINOR),
    .build_id = nullptr,
    .build_id_len = 0,
    .abi_tag = DUSK_ABI_COMPILER "-" DUSK_ABI_ARCH "-" DUSK_ABI_OS,
};

}  // namespace

const GameService& game_service() {
    const auto& buildId = manifest::image_build_id();
    s_gameService.build_id = buildId.empty() ? nullptr : buildId.data();
    s_gameService.build_id_len = static_cast<uint32_t>(buildId.size());
    return s_gameService;
}

}  // namespace dusk::mods::svc
