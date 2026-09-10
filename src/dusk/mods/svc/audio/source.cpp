#include "source.hpp"
#include <cstring>

namespace dusk::mods::svc::audio {
Source::~Source() {
    if (heap.isAllocated()) heap.free();
}
bool Source::ready() const {
    return produced.load(std::memory_order_acquire) != consumed.load(std::memory_order_relaxed) || finished.load();
}
bool Source::read_block(void* destination) {
    auto index = consumed.load(std::memory_order_relaxed);
    if (index == produced.load(std::memory_order_acquire)) return false;
    auto* header = static_cast<JASAramStream::BlockHeader*>(destination);
    *header = {};
    header->tag = 'BLCK';
    header->mSize = JASAramStream::getBlockSize();
    std::memcpy(header + 1, blocks[index % blocks.size()].pcm.data(), channels * JASAramStream::getBlockSize());
    consumed.store(index + 1, std::memory_order_release);
    return true;
}
}
