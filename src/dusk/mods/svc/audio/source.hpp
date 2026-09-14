#pragma once
#include <array>
#include <atomic>
#include <memory>
#include "JSystem/JAudio2/JASAramStream.h"
#include "JSystem/JAudio2/JASHeapCtrl.h"

namespace dusk::mods::svc::audio {
constexpr uint32_t blockFrames = 5040;
constexpr uint32_t outputRate = 32000;
constexpr int entryBase = 0x70000000;

// Only the game thread produces blocks; only JAS's load thread consumes them. The stream keeps
// this object alive after service handles close, until JAudio has retired all callbacks/tasks.
struct Source {
    struct Block {
        std::array<uint8_t, 0x2760 * 2> pcm{};
    };

    JASHeap heap{};
    uint32_t channels{}, ringBlocks{}, prepareBlocks{};
    std::array<Block, 2> blocks{};
    alignas(32) std::array<uint8_t, sizeof(JASAramStream::Header) + 0x2760 * 2> readBuffer{};
    std::atomic<uint32_t> produced{}, consumed{};
    std::atomic<uint32_t> loadedFrames{}, position{}, endFrame{}, underruns{};
    std::atomic<bool> finished{}, retired{}, stopped{};
    // Retry state is protected by JASCriticalSection, shared by audio and load threads.
    bool waiting{}, firstRetry{}, starved{}, guardEnd{};
    uint32_t refillRetries{};
    JASAramStream::TaskData retry{};
    ~Source();
    bool ready() const;
    bool read_block(void* destination);
};

std::shared_ptr<Source> find_source(int entry);
int resolve_path(const char* path);
bool is_ducking();
}  // namespace dusk::mods::svc::audio
