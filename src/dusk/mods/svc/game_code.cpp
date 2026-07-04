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
#else
#define DUSK_ABI_OS "linux"
#endif

GameCodeService s_gameCodeService{
    .header = SERVICE_HEADER(GameCodeService, GAME_CODE_SERVICE_MAJOR, GAME_CODE_SERVICE_MINOR),
    .build_id = nullptr,
    .build_id_len = 0,
    .abi_tag = DUSK_ABI_COMPILER "-" DUSK_ABI_ARCH "-" DUSK_ABI_OS,
};

}  // namespace

const GameCodeService& game_code_service() {
    const auto& buildId = manifest::image_build_id();
    s_gameCodeService.build_id = buildId.empty() ? nullptr : buildId.data();
    s_gameCodeService.build_id_len = static_cast<uint32_t>(buildId.size());
    return s_gameCodeService;
}

}  // namespace dusk::mods::svc
