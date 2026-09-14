#ifndef JASPCMSTREAM_H
#define JASPCMSTREAM_H

#if TARGET_PC
#include <array>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include "JSystem/JAudio2/JASDSPInterface.h"

class JASChannel;
class JAIStreamMgr;

class JASPCMStream final {
public:
    enum class SampleFormat { S16, F32 };
    enum class Error {
        NONE,
        INVALID_ARGUMENT,
        OUT_OF_MEMORY,
        RESAMPLER_FAILED,
        INVALID_PITCH,
        INPUT_CLOSED,
        ALREADY_ATTACHED,
        CHANNELS_UNAVAILABLE,
        CHANNEL_LOST,
    };

    struct Failure {
        Error code = Error::NONE;
        std::string message{};
    };

    struct Desc {
        SampleFormat format = SampleFormat::F32;
        uint32_t sampleRate = 48000;
        uint8_t channels = 2;
        uint32_t capacityFrames = 0;
        uint32_t prepareFrames = 0;
    };

    struct WriteResult {
        uint32_t acceptedFrames = 0;
        Error error = Error::NONE;
    };

    struct Params {
        struct Channel {
            float volume = 1.0f;
            float pan = 0.5f;
            float fxMix = 0.0f;
            float dolby = 0.0f;
        };

        float pitch = 1.0f;
        std::array<Channel, 2> channels{};
    };
    enum class Phase { PREPARING, PREPARED, PLAYING, PAUSED, STOPPING, ENDED };

    struct State {
        Phase phase = Phase::PREPARING;
        uint32_t capacityFrames = 0;
        uint64_t positionFrames = 0;
        uint64_t bufferedFrames = 0;
        uint64_t underrunCount = 0;
        float effectivePitch = 1.0f;
        bool pitchLimited = false;
        bool inputClosed = false;
        bool starved = false;
        bool retired = false;
        Error error = Error::NONE;
        int resamplerError = 0;
    };

    struct RenderRequest {
        uint32_t outputSampleRate = 0;
        float playbackPitch = 1.0f;
        bool paused = false;
    };

    struct RenderBlock {
        std::array<std::array<float, DSP_SUBFRAME_SIZE>, 2> channels{};
        uint32_t renderedFrames = 0;
        bool drained = false;
    };

    static std::expected<std::shared_ptr<JASPCMStream>, Failure> create(const Desc& desc);
    ~JASPCMStream();
    WriteResult write(const void* frames, uint32_t frameCount);
    uint32_t getWritableFrames() const;
    Error endOfStream();
    Error start();
    void pause(bool paused);
    void stop(uint16_t directRelease);
    void cancel();
    void setParams(const Params& params);
    State getState() const;
    uint8_t getChannelCount() const;
    void renderSubFrame(const RenderRequest& request, RenderBlock& output);

private:
    friend class JAIStreamMgr;
    struct Impl;
    explicit JASPCMStream(std::unique_ptr<Impl> impl);
    Error attach();
    void finish(Error error = Error::NONE, int resamplerError = 0);
    static int32_t channelProcCallback(void* userData);
    static void channelCallback(
        uint32_t event, JASChannel* channel, JASDsp::TChannel* dsp, void* userData);
    std::unique_ptr<Impl> mImpl;
};
#endif
#endif
