#include "JSystem/JAudio2/JASPCMStream.h"
#include "JSystem/JSystem.h"

#if TARGET_PC
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <new>
#include <samplerate.h>
#include <vector>
#include "JSystem/JAudio2/JASChannel.h"
#include "JSystem/JAudio2/JASDSPChannel.h"
#include "JSystem/JAudio2/JASDriverIF.h"

namespace {
// 80 * 48 source frames plus at most 2195 frames of medium-sinc lookahead.
// Each call offers 1024 frames (including wraps); 8 calls cover startup and drain.
constexpr uint32_t kInputFrames = 1024;
constexpr uint32_t kProcessCalls = 8;

const JASOscillator::Point kReleaseTable[] = {{0, 2, 0}, {15, 0, 0}};
const JASOscillator::Data kEnvelope{0, 1.0f, nullptr, kReleaseTable, 1.0f, 0.0f};
}  // namespace

struct JASPCMStream::Impl {
    Desc desc{};
    std::vector<std::byte> ring{};
    std::array<float, kInputFrames * 2> input{};
    std::array<float, DSP_SUBFRAME_SIZE * 2> output{};
    SRC_STATE* converter = nullptr;
    std::atomic<uint64_t> written = 0;
    std::atomic<uint64_t> read = 0;
    std::atomic<bool> closed = false;
    std::atomic<bool> terminated = false;
    State state{};
    Params params{};
    uint64_t presented = 0;
    double fraction = 0;
    std::array<JASChannel*, 2> channels{};
    std::array<JASChannel*, 2> ownedChannels{};
    bool attached = false;
    bool callbackRegistered = false;
    bool startRequested = false;
    bool started = false;
    bool starting = false;
    bool paused = false;
    bool terminal = false;
    bool stopping = false;

    ~Impl() { src_delete(converter); }

    uint32_t frameBytes() const {
        return desc.channels * (desc.format == SampleFormat::S16 ? 2 : 4);
    }

    bool ready() const {
        const auto count = written.load(std::memory_order_acquire);
        return count >= desc.prepareFrames ||
               (closed.load(std::memory_order_acquire) && count != 0);
    }
};

JASPCMStream::JASPCMStream(std::unique_ptr<Impl> impl) : mImpl{std::move(impl)} {}

std::expected<std::shared_ptr<JASPCMStream>, JASPCMStream::Failure> JASPCMStream::create(
    const Desc& desc) {
    if ((desc.format != SampleFormat::S16 && desc.format != SampleFormat::F32) ||
        desc.sampleRate < 8000 || desc.sampleRate > 192000 || desc.channels < 1 ||
        desc.channels > 2)
    {
        return std::unexpected{
            Failure{Error::INVALID_ARGUMENT, "Invalid PCM format, rate or channel count"}};
    }
    Desc resolved = desc;
    if (resolved.capacityFrames == 0) {
        resolved.capacityFrames = (desc.sampleRate + 1) / 2;
    }
    if (uint64_t{resolved.capacityFrames} * 1000 < uint64_t{desc.sampleRate} * 50 ||
        uint64_t{resolved.capacityFrames} * 1000 > uint64_t{desc.sampleRate} * 2000)
    {
        return std::unexpected{
            Failure{Error::INVALID_ARGUMENT, "PCM capacity must represent 50..2000 ms"}};
    }
    if (resolved.prepareFrames == 0) {
        resolved.prepareFrames = std::min(resolved.capacityFrames, (desc.sampleRate + 9) / 10);
    }
    if (resolved.prepareFrames > resolved.capacityFrames) {
        return std::unexpected{
            Failure{Error::INVALID_ARGUMENT, "PCM preparation exceeds capacity"}};
    }
    try {
        auto impl = std::make_unique<Impl>();
        impl->desc = resolved;
        const uint64_t bytes = uint64_t{resolved.capacityFrames} * impl->frameBytes();
        if (bytes > std::numeric_limits<size_t>::max()) {
            return std::unexpected{Failure{Error::INVALID_ARGUMENT, "PCM ring size overflow"}};
        }
        impl->ring.resize(static_cast<size_t>(bytes));
        int error = 0;
        impl->converter = src_new(SRC_SINC_MEDIUM_QUALITY, desc.channels, &error);
        if (!impl->converter) {
            return std::unexpected{Failure{Error::RESAMPLER_FAILED, src_strerror(error)}};
        }
        // Touch coefficient pages and first-use processing before attaching audio callbacks.
        src_set_ratio(impl->converter, 1.0 / 48);
        for (uint32_t call = 0; call < kProcessCalls; ++call) {
            SRC_DATA warmup{};
            warmup.data_in = impl->input.data();
            warmup.data_out = impl->output.data();
            warmup.input_frames = kInputFrames;
            warmup.output_frames = DSP_SUBFRAME_SIZE;
            warmup.src_ratio = 1.0 / 48;
            error = src_process(impl->converter, &warmup);
            if (error) {
                return std::unexpected{Failure{Error::RESAMPLER_FAILED, src_strerror(error)}};
            }
        }
        // Reset touches history storage and removes all warmup content.
        src_reset(impl->converter);
        impl->state.capacityFrames = resolved.capacityFrames;
        return std::shared_ptr<JASPCMStream>{new JASPCMStream{std::move(impl)}};
    } catch (const std::bad_alloc&) {
        return std::unexpected{Failure{Error::OUT_OF_MEMORY, "Unable to allocate PCM stream"}};
    }
}

JASPCMStream::~JASPCMStream() {
    // Owners retain the stream until retirement; destruction belongs to the game thread.
    cancel();
    for (auto* channel : mImpl->ownedChannels) {
        if (channel) {
            JKR_DELETE(channel);
        }
    }
}

JASPCMStream::WriteResult JASPCMStream::write(const void* frames, uint32_t frameCount) {
    auto& s = *mImpl;
    if ((!frames && frameCount) || uint64_t{frameCount} * s.frameBytes() > SIZE_MAX) {
        return {0, Error::INVALID_ARGUMENT};
    }
    if (s.closed.load(std::memory_order_acquire) || s.terminated.load(std::memory_order_acquire)) {
        return {0, Error::INPUT_CLOSED};
    }
    const auto written = s.written.load(std::memory_order_relaxed);
    const auto read = s.read.load(std::memory_order_acquire);
    const uint32_t count = std::min<uint64_t>(frameCount, s.desc.capacityFrames - (written - read));
    if (count > UINT64_MAX - written) {
        return {0, Error::INVALID_ARGUMENT};
    }
    if (count) {
        const auto offset = written % s.desc.capacityFrames;
        const auto first = std::min<uint64_t>(count, s.desc.capacityFrames - offset);
        const auto bytes = s.frameBytes();
        std::memcpy(s.ring.data() + offset * bytes, frames, first * bytes);
        std::memcpy(s.ring.data(), static_cast<const std::byte*>(frames) + first * bytes,
            (count - first) * bytes);
        s.written.store(written + count, std::memory_order_release);
    }
    return {count, Error::NONE};
}

uint32_t JASPCMStream::getWritableFrames() const {
    const auto& s = *mImpl;
    if (s.closed.load(std::memory_order_acquire) || s.terminated.load(std::memory_order_acquire)) {
        return 0;
    }
    const auto written = s.written.load(std::memory_order_relaxed);
    return s.desc.capacityFrames - (written - s.read.load(std::memory_order_acquire));
}

JASPCMStream::Error JASPCMStream::endOfStream() {
    // Producer operations are serialized. Observing closed acquires the final written position.
    mImpl->closed.store(true, std::memory_order_release);
    return Error::NONE;
}

uint8_t JASPCMStream::getChannelCount() const {
    return mImpl->desc.channels;
}

JASPCMStream::State JASPCMStream::getState() const {
    auto& s = *mImpl;
    auto state = s.state;
    state.inputClosed = s.closed.load(std::memory_order_acquire);
    const auto written = s.written.load(std::memory_order_acquire);
    state.positionFrames = std::min(s.presented, written);
    state.bufferedFrames = s.terminal ? 0 : written - state.positionFrames;
    if (!s.started && !s.terminal) {
        state.phase = s.ready() ? Phase::PREPARED : Phase::PREPARING;
    }
    if (!s.started && state.inputClosed && written == 0) {
        state.phase = Phase::ENDED;
    }
    return state;
}

void JASPCMStream::finish(Error error, int resamplerError) {
    auto& s = *mImpl;
    s.terminal = true;
    s.terminated.store(true, std::memory_order_release);
    s.state.starved = false;
    s.state.phase = Phase::STOPPING;
    if (s.state.error == Error::NONE) {
        s.state.error = error;
        s.state.resamplerError = resamplerError;
    }
}

void JASPCMStream::renderSubFrame(const RenderRequest& request, RenderBlock& block) {
    auto& s = *mImpl;
    block = {};
    if (s.terminal) {
        block.drained = true;
        return;
    }
    if (!std::isfinite(request.playbackPitch)) {
        finish(Error::INVALID_PITCH);
        block.drained = true;
        return;
    }
    if (request.outputSampleRate != 32000 && request.outputSampleRate != 48000) {
        finish(Error::INVALID_ARGUMENT);
        block.drained = true;
        return;
    }
    s.state.phase = s.stopping ? Phase::STOPPING :
                                 (request.paused || s.paused ? Phase::PAUSED : Phase::PLAYING);
    if (request.paused || s.paused || request.playbackPitch <= 0) {
        s.state.starved = false;
        if (request.playbackPitch <= 0) {
            s.state.effectivePitch = 0;
            s.state.pitchLimited = false;
        }
        return;
    }
    const double rate = static_cast<double>(s.desc.sampleRate) / request.outputSampleRate;
    const double step = std::max(rate * std::min(request.playbackPitch, 8.0f), 1.0 / 256);
    const double ratio = 1.0 / step;
    s.state.effectivePitch = static_cast<float>(step / rate);
    s.state.pitchLimited = request.playbackPitch > 8 || rate * request.playbackPitch < 1.0 / 256;
    int error = src_is_valid_ratio(ratio) ? src_set_ratio(s.converter, ratio) : -1;
    if (error) {
        finish(Error::RESAMPLER_FAILED, error);
        block.drained = true;
        return;
    }

    bool drained = false;
    for (uint32_t call = 0; call < kProcessCalls && block.renderedFrames < DSP_SUBFRAME_SIZE;
        ++call)
    {
        const bool closed = s.closed.load(std::memory_order_acquire);
        const auto written = s.written.load(std::memory_order_acquire);
        const auto read = s.read.load(std::memory_order_relaxed);
        const uint32_t count = std::min<uint64_t>(kInputFrames, written - read);
        for (uint32_t frame = 0; frame < count; ++frame) {
            const auto offset = ((read + frame) % s.desc.capacityFrames) * s.frameBytes();
            for (uint32_t lane = 0; lane < s.desc.channels; ++lane) {
                float value = 0;
                if (s.desc.format == SampleFormat::S16) {
                    int16_t sample = 0;
                    std::memcpy(&sample, s.ring.data() + offset + lane * 2, 2);
                    value = sample / 32768.0f;
                } else {
                    std::memcpy(&value, s.ring.data() + offset + lane * 4, 4);
                    if (!std::isfinite(value)) {
                        value = 0;
                    }
                }
                s.input[frame * s.desc.channels + lane] = value;
            }
        }
        // Non-null data_in is required even for empty EOF by the pinned sinc prepare_data path.
        SRC_DATA data{};
        data.data_in = s.input.data();
        data.data_out = s.output.data();
        data.input_frames = count;
        data.output_frames = DSP_SUBFRAME_SIZE - block.renderedFrames;
        data.src_ratio = ratio;
        data.end_of_input = closed && read + count == written;
        error = src_process(s.converter, &data);
        if (error) {
            finish(Error::RESAMPLER_FAILED, error);
            // Discard any earlier prefix from this subframe; it has not advanced presentation.
            block = {};
            block.drained = true;
            return;
        }
        s.read.store(read + data.input_frames_used, std::memory_order_release);
        for (uint32_t frame = 0; frame < data.output_frames_gen; ++frame) {
            for (uint32_t lane = 0; lane < s.desc.channels; ++lane) {
                block.channels[lane][block.renderedFrames + frame] =
                    s.output[frame * s.desc.channels + lane];
            }
        }
        block.renderedFrames += data.output_frames_gen;
        if (!data.input_frames_used && !data.output_frames_gen) {
            drained = data.end_of_input && read == written;
            break;
        }
    }
    const double progress = s.fraction + block.renderedFrames * step;
    const auto whole = static_cast<uint64_t>(progress);
    s.fraction = progress - whole;
    s.presented += std::min(whole, UINT64_MAX - s.presented);
    if (drained) {
        s.presented = s.written.load(std::memory_order_acquire);
        s.fraction = 0;
        finish();
        block.drained = true;
    } else {
        const bool starved =
            block.renderedFrames < DSP_SUBFRAME_SIZE && !s.closed.load(std::memory_order_acquire);
        if (starved && !s.state.starved && s.state.underrunCount != UINT64_MAX) {
            ++s.state.underrunCount;
        }
        s.state.starved = starved;
    }
}

JASPCMStream::Error JASPCMStream::attach() {
    auto& s = *mImpl;
    if (s.attached) {
        return Error::ALREADY_ATTACHED;
    }
    s.attached = true;
    // Allocate the JAS objects on the game thread, before any callback can run.
    for (uint32_t lane = 0; lane < s.desc.channels; ++lane) {
        auto* channel = JKR_NEW JASChannel{channelCallback, this};
        if (!channel) {
            cancel();
            return Error::CHANNELS_UNAVAILABLE;
        }
        s.channels[lane] = channel;
        s.ownedChannels[lane] = channel;
        channel->setPcmSource(this, lane);
        channel->setInitPitch(1.0f);
        channel->setPriority(0x7f7f);
        channel->setMixConfig(0, 0xffff);
        channel->setOscInit(0, &kEnvelope);
    }
    if (!JASDriver::registerSubFrameCallback(channelProcCallback, this)) {
        cancel();
        return Error::CHANNELS_UNAVAILABLE;
    }
    s.callbackRegistered = true;
    return Error::NONE;
}

JASPCMStream::Error JASPCMStream::start() {
    auto& s = *mImpl;
    if (s.state.error != Error::NONE) {
        return s.state.error;
    }
    if (s.terminal || s.stopping) {
        return Error::INPUT_CLOSED;
    }
    s.startRequested = true;
    s.paused = false;
    return Error::NONE;
}

void JASPCMStream::pause(bool paused) {
    mImpl->paused = paused;
}

void JASPCMStream::stop(uint16_t directRelease) {
    auto& s = *mImpl;
    s.closed.store(true, std::memory_order_release);
    s.terminated.store(true, std::memory_order_release);
    if (s.terminal || s.stopping) {
        return;
    }
    s.stopping = true;
    s.state.phase = Phase::STOPPING;
    if (!s.started || s.paused) {
        cancel();
        return;
    }
    for (auto* channel : s.channels) {
        if (channel) {
            channel->release(directRelease);
        }
    }
}

void JASPCMStream::cancel() {
    auto& s = *mImpl;
    finish();
    s.closed.store(true, std::memory_order_release);
    // A control call may cancel even if audio callbacks have already stopped at shutdown.
    if (s.callbackRegistered) {
        JASDriver::rejectCallback(channelProcCallback, this);
        s.callbackRegistered = false;
    }
    for (auto*& channel : s.channels) {
        if (!channel) {
            continue;
        }
        auto* old = channel;
        channel = nullptr;
        old->retirePcm();
    }
    s.state.retired = true;
    s.state.phase = Phase::ENDED;
}

void JASPCMStream::setParams(const Params& params) {
    mImpl->params = params;
    if (!mImpl->started) {
        mImpl->state.effectivePitch = params.pitch;
    }
}

int32_t JASPCMStream::channelProcCallback(void* userData) {
    auto& stream = *static_cast<JASPCMStream*>(userData);
    auto& s = *stream.mImpl;
    if (s.closed.load(std::memory_order_acquire) && s.written.load(std::memory_order_acquire) == 0)
    {
        stream.finish();
    }
    if (s.terminal) {
        // Clear all borrowed DSP bindings before publishing retirement.
        for (auto*& channel : s.channels) {
            auto* old = channel;
            channel = nullptr;
            if (old) {
                old->retirePcm();
            }
        }
        s.callbackRegistered = false;
        s.state.retired = true;
        s.state.phase = Phase::ENDED;
        return -1;
    }
    for (uint32_t lane = 0; lane < s.desc.channels; ++lane) {
        auto* channel = s.channels[lane];
        if (!channel) {
            continue;
        }
        JASChannelParams params{};
        params.mPitch = s.params.pitch;
        params.mVolume = s.params.channels[lane].volume;
        params.mPan = s.params.channels[lane].pan;
        params.mFxMix = s.params.channels[lane].fxMix;
        params.mDolby = s.params.channels[lane].dolby;
        channel->setParams(params);
        channel->setPauseFlag(s.paused);
    }
    if (!s.started && s.startRequested && s.ready()) {
        s.starting = true;
        s.started = true;
        for (uint32_t lane = 0; lane < s.desc.channels; ++lane) {
            // PCM failures retire the binding; CB_STOP clears the live slot before returning.
            if (!s.channels[lane]->playForce()) {
                stream.finish(Error::CHANNELS_UNAVAILABLE);
                break;
            }
        }
        s.starting = false;
    }
    return 0;
}

void JASPCMStream::channelCallback(
    uint32_t event, JASChannel* channel, JASDsp::TChannel*, void* userData) {
    if (event != JASChannel::CB_STOP) {
        return;
    }
    auto& stream = *static_cast<JASPCMStream*>(userData);
    auto& s = *stream.mImpl;
    for (auto*& value : s.channels) {
        if (value == channel) {
            value = nullptr;
        }
    }
    if (!s.terminal && !s.starting) {
        stream.finish(s.stopping ? Error::NONE : Error::CHANNEL_LOST);
    }
}
#endif
