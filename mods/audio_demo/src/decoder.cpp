#include "decoder.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "dr_mp3.h"
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#include "dr_flac.h"
#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO
#include "dr_wav.h"
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"

namespace audio_demo {
struct Decoder::Impl {
    enum class Kind { None, Mp3, Flac, Wav, Vorbis } kind{};
    drmp3 mp3{};
    drwav wav{};
    drflac* flac{};
    stb_vorbis* vorbis{};
    uint32_t rate{}, channels{};
    uint64_t length{};
    ~Impl() {
        if (kind == Kind::Mp3) drmp3_uninit(&mp3);
        if (kind == Kind::Wav) drwav_uninit(&wav);
        if (flac) drflac_close(flac);
        if (vorbis) stb_vorbis_close(vorbis);
    }
};
Decoder::Decoder() = default;
Decoder::~Decoder() = default;
void Decoder::close() { impl.reset(); }
void Decoder::open(std::span<const std::byte> bytes) {
    auto next = std::make_unique<Impl>();
    if (bytes.size() < 4 || bytes.size() > std::numeric_limits<int>::max())
        throw std::runtime_error{"Empty or oversized audio file."};
    if (std::memcmp(bytes.data(), "OggS", 4) == 0) {
        int error{};
        next->vorbis = stb_vorbis_open_memory(reinterpret_cast<const unsigned char*>(bytes.data()),
            static_cast<int>(bytes.size()), &error, nullptr);
        if (!next->vorbis) throw std::runtime_error{"Cannot decode this Ogg file. Choose Ogg Vorbis; Ogg Opus is not supported."};
        next->kind = Impl::Kind::Vorbis;
        const auto info = stb_vorbis_get_info(next->vorbis);
        next->rate = info.sample_rate;
        next->channels = info.channels;
        next->length = stb_vorbis_stream_length_in_samples(next->vorbis);
    } else if (std::memcmp(bytes.data(), "fLaC", 4) == 0) {
        next->flac = drflac_open_memory(bytes.data(), bytes.size(), nullptr);
        if (!next->flac) throw std::runtime_error{"Cannot decode this FLAC file."};
        next->kind = Impl::Kind::Flac;
        next->rate = next->flac->sampleRate;
        next->channels = next->flac->channels;
        next->length = next->flac->totalPCMFrameCount;
    } else if (drwav_init_memory(&next->wav, bytes.data(), bytes.size(), nullptr)) {
        next->kind = Impl::Kind::Wav;
        next->rate = next->wav.sampleRate;
        next->channels = next->wav.channels;
        next->length = next->wav.totalPCMFrameCount;
    } else if (drmp3_init_memory(&next->mp3, bytes.data(), bytes.size(), nullptr)) {
        next->kind = Impl::Kind::Mp3;
        next->rate = next->mp3.sampleRate;
        next->channels = next->mp3.channels;
        next->length = drmp3_get_pcm_frame_count(&next->mp3);
        if (!drmp3_seek_to_pcm_frame(&next->mp3, 0)) throw std::runtime_error{"Cannot rewind this MP3 file."};
    } else throw std::runtime_error{"Unsupported or damaged audio file. Choose MP3, Ogg Vorbis, FLAC, or WAV."};
    if (next->channels < 1 || next->channels > 2 || next->rate < 8000 || next->rate > 192000)
        throw std::runtime_error{"Choose mono or stereo audio between 8 and 192 kHz."};
    if (!next->length) throw std::runtime_error{"The file contains no decodable audio frames."};
    impl = std::move(next);
}
uint32_t Decoder::read(std::span<float> interleaved) {
    if (!impl) return 0;
    const auto frames = std::min<size_t>(interleaved.size() / impl->channels, std::numeric_limits<int>::max() / 2);
    switch (impl->kind) {
    case Impl::Kind::Mp3: return drmp3_read_pcm_frames_f32(&impl->mp3, frames, interleaved.data());
    case Impl::Kind::Flac: return drflac_read_pcm_frames_f32(impl->flac, frames, interleaved.data());
    case Impl::Kind::Wav: return drwav_read_pcm_frames_f32(&impl->wav, frames, interleaved.data());
    case Impl::Kind::Vorbis: return stb_vorbis_get_samples_float_interleaved(impl->vorbis, impl->channels,
        interleaved.data(), static_cast<int>(frames * impl->channels));
    default: return 0;
    }
}
bool Decoder::seek(uint64_t frame) {
    if (!impl || frame >= impl->length) return false;
    switch (impl->kind) {
    case Impl::Kind::Mp3: return drmp3_seek_to_pcm_frame(&impl->mp3, frame);
    case Impl::Kind::Flac: return drflac_seek_to_pcm_frame(impl->flac, frame);
    case Impl::Kind::Wav: return drwav_seek_to_pcm_frame(&impl->wav, frame);
    case Impl::Kind::Vorbis: return frame <= std::numeric_limits<unsigned int>::max() && stb_vorbis_seek(impl->vorbis, frame);
    default: return false;
    }
}
uint32_t Decoder::sample_rate() const { return impl ? impl->rate : 0; }
uint32_t Decoder::channels() const { return impl ? impl->channels : 0; }
uint64_t Decoder::length_frames() const { return impl ? impl->length : 0; }
const char* Decoder::format_name() const {
    if (!impl) return "None";
    switch (impl->kind) {
    case Impl::Kind::Mp3: return "MP3";
    case Impl::Kind::Flac: return "FLAC";
    case Impl::Kind::Wav: return "WAV";
    case Impl::Kind::Vorbis: return "Ogg Vorbis";
    default: return "None";
    }
}
}
