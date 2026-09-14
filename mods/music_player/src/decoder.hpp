#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace audio_demo {

class Decoder {
public:
    Decoder();
    ~Decoder();
    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;
    // The compressed bytes must remain alive until close(). Throws on unsupported or invalid data.
    void open(std::span<const std::byte> bytes);
    void close();
    uint32_t read(std::span<float> interleaved);
    bool seek(uint64_t frame);
    uint32_t sample_rate() const;
    uint32_t channels() const;
    uint64_t length_frames() const;
    const char* format_name() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

}  // namespace audio_demo
