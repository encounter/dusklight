#include "aurora/dvd.h"
#include "aurora/lib/logging.hpp"
#include "dusk/mod_loader.hpp"
#include "loader.hpp"

#include <cstring>
#include <mutex>
#include <unordered_map>

using namespace std::string_literals;

namespace {

aurora::Module Log("dusk::modLoader::overlay");

struct OverlayFileData {
    std::string bundlePath;
    std::shared_ptr<dusk::mods::loader::ModBundle> bundle;
};

// Keyed by the id passed to Aurora as per-file userdata. Guarded by s_overlayMutex: Aurora may
// call cbOpen from a DVD thread while the game thread replaces the set in syncOverlayFiles.
// The shared bundle pointer keeps a disabled/reloaded mod's old bundle readable until the last
// open completes.
std::unordered_map<uintptr_t, OverlayFileData> s_overlayFiles;
uintptr_t s_nextOverlayId = 1;
std::mutex s_overlayMutex;

void findOverlayFiles(std::vector<AuroraOverlayFile>& files, dusk::LoadedMod& mod) {
    for (const auto& file : mod.bundle->getFileNames()) {
        if (!file.starts_with("overlay/")) {
            continue;
        }

        auto overlayPath = file.substr("overlay/"s.size());
        assert(!overlayPath.starts_with('/'));
        overlayPath.insert(0, "/");

        const auto size = mod.bundle->getFileSize(file);

        const auto id = s_nextOverlayId++;
        s_overlayFiles.emplace(id, OverlayFileData{file, mod.bundle});
        files.emplace_back(
            strdup(overlayPath.c_str()),
            reinterpret_cast<void*>(id),
            size);
    }
}

struct OpenOverlayFile {
    std::vector<u8> data;
    size_t pos;
};

void* cbOpen(void* userdata) {
    const auto id = reinterpret_cast<uintptr_t>(userdata);
    OverlayFileData fileData;
    {
        std::lock_guard lock{s_overlayMutex};
        const auto it = s_overlayFiles.find(id);
        if (it == s_overlayFiles.end()) {
            // The overlay set was re-pushed between the FST lookup and this call.
            return nullptr;
        }
        fileData = it->second;
    }

    try {
        auto fileContents = fileData.bundle->readFile(fileData.bundlePath);
        return new OpenOverlayFile(std::move(fileContents), 0);
    } catch (const std::runtime_error& e) {
        Log.error("Failed to read overlay file {}: {}", fileData.bundlePath, e.what());
        return nullptr;
    }
}

void cbClose(void* handle) {
    const auto openFile = static_cast<OpenOverlayFile*>(handle);
    delete openFile;
}

int64_t cbRead(void* handle, uint8_t *buf, const size_t len) {
    auto& openFile = *static_cast<OpenOverlayFile*>(handle);

    const auto remainingSpace = openFile.data.size() - openFile.pos;
    const auto toRead = std::min(remainingSpace, len);
    std::memcpy(buf, openFile.data.data() + openFile.pos, toRead);
    openFile.pos += toRead;
    return static_cast<int64_t>(toRead);
}

int64_t cbSeek(void* handle, int64_t offset, int32_t whence) {
    if (whence != 0) {
        Log.fatal("Invalid seek mode from aurora: {}", whence);
    }

    auto& openFile = *static_cast<OpenOverlayFile*>(handle);
    const auto posSigned = std::clamp(offset, static_cast<int64_t>(0), static_cast<int64_t>(openFile.data.size()));
    openFile.pos = static_cast<size_t>(posSigned);
    return posSigned;
}

constexpr AuroraOverlayCallbacks s_overlayCallbacks = {
    .open = cbOpen,
    .close = cbClose,
    .read = cbRead,
    .seek = cbSeek,
};

}

namespace dusk {

void ModLoader::sync_overlay_files() {
    static bool callbacksRegistered = false;
    if (!callbacksRegistered) {
        aurora_dvd_overlay_callbacks(&s_overlayCallbacks);
        callbacksRegistered = true;
    }

    std::vector<AuroraOverlayFile> files;
    {
        std::lock_guard lock{s_overlayMutex};
        s_overlayFiles.clear();
        for (auto& mod : active_mods()) {
            findOverlayFiles(files, mod);
        }
    }

    Log.debug("Registering {} overlay file(s).", files.size());
    aurora_dvd_overlay_files(files.data(), files.size(), nullptr);

    for (const auto& file : files) {
        std::free(const_cast<char*>(file.fileName));
    }
}

}  // namespace dusk
