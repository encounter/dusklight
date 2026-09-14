#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <mods/service.hpp>
#include <mods/svc/audio.h>
#include <mods/svc/file.h>
#include <mods/svc/log.h>
#include <mods/svc/ui.h>
#include <stdexcept>
#include <string>
#include <vector>
#include "decoder.hpp"

DEFINE_MOD();
IMPORT_SERVICE(AudioService, svc_audio);
IMPORT_SERVICE(FileService, svc_file);
IMPORT_SERVICE(UiService, svc_ui);
IMPORT_SERVICE(LogService, svc_log);

namespace {
struct Track {
    FileBuffer bytes = FILE_BUFFER_INIT;
    audio_demo::Decoder decoder{};
    std::string name, location;
    bool remembered{};

    ~Track() {
        decoder.close();
        svc_file->free(mod_ctx, &bytes);
    }
};

struct View {
    UiElementHandle file{}, format{}, error{}, time{}, progress{}, next{};
};

std::unique_ptr<Track> track;
AudioStreamHandle stream{};
AudioStreamState snapshot = AUDIO_STREAM_STATE_INIT;
UiWindowHandle window{};
View panelView{}, windowView{};

struct Entry {
    uint64_t key{};
    std::string location, name;
};

struct LibraryView {
    UiListHandle queue{}, history{};
    UiElementHandle queueSummary{}, historySummary{}, notice{};
    uint64_t revision{std::numeric_limits<uint64_t>::max()};
} libraryView{};

std::vector<Entry> queue, history;
uint64_t nextKey{1}, libraryRevision{};
constexpr size_t queueLimit{256}, historyLimit{32};
std::string libraryMessage;
bool advancePending{};
enum class Pick { Open, Enqueue, Folder };
std::string location, message;
std::array<float, 4096 * 2> pcm{};
uint32_t pendingFrames{}, pendingOffset{}, openRetries{};
int64_t volume{70}, pitch{100}, fadeMs{150}, seekPercent{};
bool picking{}, startPending{}, playing{}, paused{}, eof{}, loop{}, duckBgm{true}, wrapped{};
double startSeconds{}, positionSeconds{};
constexpr FileFilter filters[]{{"Music (MP3, Ogg Vorbis, FLAC, WAV)", "mp3;ogg;oga;flac;wav"}};

enum class Control {
    Choose,
    Play,
    Pause,
    Stop,
    Restart,
    Seek,
    Position,
    Volume,
    Pitch,
    Fade,
    Loop,
    Duck,
    Next,
    AddFile,
    AddFolder,
    ClearQueue,
    ClearHistory,
    Library
};

void check(ModResult result, const char* operation) {
    if (result != MOD_OK) {
        throw std::runtime_error{
            std::string{operation} + " failed (" + std::to_string(result) + ")."};
    }
}

void report(const std::exception& error) {
    message = error.what();
    svc_log->error(mod_ctx, message.c_str());
}

uint32_t fade_frames() {
    return static_cast<uint32_t>(fadeMs * 32);
}

double duration() {
    return track ? double(track->decoder.length_frames()) / track->decoder.sample_rate() : 0;
}

void close_stream() {
    if (stream) {
        svc_audio->close(mod_ctx, stream);
    }
    stream = 0;
    snapshot = AUDIO_STREAM_STATE_INIT;
    pendingFrames = pendingOffset = 0;
    startPending = false;
    eof = false;
}

void stop() {
    close_stream();
    advancePending = false;
    playing = paused = false;
    positionSeconds = startSeconds = 0;
    message.clear();
}

void start_at(double seconds, bool shouldPlay) {
    if (!track) {
        return;
    }
    close_stream();
    const auto frame = std::min<uint64_t>(
        static_cast<uint64_t>(std::max(0.0, seconds) * track->decoder.sample_rate()),
        track->decoder.length_frames() - 1);
    if (!track->decoder.seek(frame)) {
        playing = paused = false;
        throw std::runtime_error{"The decoder could not seek to that position."};
    }
    positionSeconds = startSeconds = double(frame) / track->decoder.sample_rate();
    playing = shouldPlay;
    paused = !shouldPlay;
    wrapped = false;
    startPending = true;
    openRetries = 0;
    message.clear();
}

std::string display_name(const char* selected) {
    std::array<char, 1024> name{};
    check(svc_file->display_name(mod_ctx, selected, name.data(), name.size()), "Reading file name");
    return name.data();
}

void load_track(const Entry& entry) {
    check(svc_file->check(mod_ctx, entry.location.c_str()),
        "Accessing music (select it again if permission was revoked)");
    FileStreamHandle file{};
    check(svc_file->open(mod_ctx, entry.location.c_str(), FILE_OPEN_READ, &file), "Opening file");
    uint64_t size{};
    const auto sizeResult = svc_file->size(mod_ctx, file, &size);
    svc_file->close(mod_ctx, file);
    check(sizeResult, "Reading file size");
    if (!size || size > 256u * 1024u * 1024u) {
        throw std::runtime_error{"Choose a nonempty music file smaller than 256 MiB."};
    }
    auto next = std::make_unique<Track>();
    check(svc_file->read_all(mod_ctx, entry.location.c_str(), &next->bytes), "Reading music");
    next->decoder.open({static_cast<const std::byte*>(next->bytes.data), next->bytes.size});
    next->name = entry.name;
    next->location = entry.location;
    std::string nextLocation{entry.location};
    stop();
    track = std::move(next);
    location = std::move(nextLocation);
    seekPercent = 0;
    message.clear();
}

void remember_track() {
    if (!track || track->remembered) {
        return;
    }
    std::erase_if(history, [](const Entry& item) { return item.location == track->location; });
    history.insert(history.begin(), Entry{nextKey++, track->location, track->name});
    if (history.size() > historyLimit) {
        history.pop_back();
    }
    track->remembered = true;
    ++libraryRevision;
}

bool enqueue(Entry entry) {
    if (queue.size() >= queueLimit) {
        return false;
    }
    entry.key = nextKey++;
    queue.push_back(std::move(entry));
    ++libraryRevision;
    return true;
}

void play_queued(size_t index) {
    if (index >= queue.size()) {
        return;
    }
    Entry entry = std::move(queue[index]);
    queue.erase(queue.begin() + index);
    ++libraryRevision;
    advancePending = false;
    try {
        load_track(entry);
        start_at(0, true);
    } catch (const std::exception& exception) {
        libraryMessage = "Skipped " + entry.name + ": " + exception.what();
        stop();
        message = libraryMessage;
        svc_log->error(mod_ctx, libraryMessage.c_str());
        advancePending = !queue.empty();
    }
}

bool supported_name(const std::string& name) {
    const auto dot = name.rfind('.');
    if (dot == std::string::npos) {
        return false;
    }
    auto extension = name.substr(dot + 1);
    for (auto& character : extension) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return extension == "mp3" || extension == "ogg" || extension == "oga" || extension == "flac" ||
           extension == "wav";
}

struct FolderEntries {
    std::vector<Entry> entries;
    bool limited{};
};

void listed(ModContext*, const FileEntry* entry, void* data) {
    if (!entry || entry->is_directory || !entry->name || !entry->location ||
        !supported_name(entry->name))
    {
        return;
    }
    auto& folder = *static_cast<FolderEntries*>(data);
    if (folder.entries.size() == queueLimit) {
        folder.limited = true;
        return;
    }
    // Entry locations may be platform permission tokens. Copy them without constructing paths.
    folder.entries.push_back({0, entry->location, entry->name});
}

void picked(ModContext*, ModResult result, const char* const* locations, uint32_t count,
    const char* error, void* data) {
    picking = false;
    if (result == MOD_UNAVAILABLE) {
        return;
    }
    try {
        if (result != MOD_OK || !count || !locations || !locations[0]) {
            throw std::runtime_error{error && *error ? error : "Could not select a file."};
        }
        const auto mode = static_cast<Pick>(reinterpret_cast<uintptr_t>(data));
        if (mode == Pick::Open) {
            load_track({0, locations[0], display_name(locations[0])});
        } else {
            size_t added{};
            bool limited{};
            if (mode == Pick::Folder) {
                FolderEntries folder{};
                check(svc_file->list(mod_ctx, locations[0], listed, &folder), "Reading folder");
                std::sort(folder.entries.begin(), folder.entries.end(),
                    [](const Entry& a, const Entry& b) { return a.name < b.name; });
                limited = folder.limited;
                for (auto& entry : folder.entries) {
                    if (enqueue(std::move(entry))) {
                        ++added;
                    } else {
                        limited = true;
                    }
                }
            } else {
                for (uint32_t i = 0; i < count; ++i) {
                    if (enqueue({0, locations[i], display_name(locations[i])})) {
                        ++added;
                    } else {
                        limited = true;
                    }
                }
            }
            libraryMessage.clear();
            if (limited) {
                libraryMessage = "Queue limit: 256 tracks.";
            } else if (mode == Pick::Folder && !added) {
                libraryMessage = "No supported audio files in this folder.";
            }
            ++libraryRevision;
        }
    } catch (const std::exception& exception) {
        report(exception);
        libraryMessage = exception.what();
        ++libraryRevision;
    }
}

void choose(Pick mode) {
    FilePickOptions options = FILE_PICK_OPTIONS_INIT;
    if (mode != Pick::Folder) {
        options.filters = filters;
        options.filter_count = std::size(filters);
        options.default_location = location.empty() ? nullptr : location.c_str();
    }
    auto* data = reinterpret_cast<void*>(static_cast<uintptr_t>(mode));
    check(mode == Pick::Folder ? svc_file->pick_folder(mod_ctx, &options, picked, data) :
                                 svc_file->pick_file(mod_ctx, &options, picked, data),
        "Opening file picker");
    picking = true;
}

void open_window(ModContext*, void*);

void pressed(ModContext*, void* data) {
    try {
        switch (static_cast<Control>(reinterpret_cast<uintptr_t>(data))) {
        case Control::Choose:
            choose(Pick::Open);
            break;
        case Control::AddFile:
            choose(Pick::Enqueue);
            break;
        case Control::AddFolder:
            choose(Pick::Folder);
            break;
        case Control::Next:
            play_queued(0);
            break;
        case Control::ClearQueue:
            queue.clear();
            advancePending = false;
            libraryMessage.clear();
            ++libraryRevision;
            break;
        case Control::ClearHistory:
            history.clear();
            libraryMessage.clear();
            ++libraryRevision;
            break;
        case Control::Library:
            open_window(mod_ctx, nullptr);
            break;
        case Control::Play:
            if (!track && !queue.empty()) {
                play_queued(0);
                break;
            }
            if (paused && stream) {
                check(svc_audio->resume(mod_ctx, stream, fade_frames()), "Resuming music");
                check(svc_audio->play(mod_ctx, stream, fade_frames()), "Playing music");
                paused = false;
                playing = true;
                message.clear();
            } else if (startPending) {
                playing = true;
                paused = false;
            } else {
                start_at(0, true);
            }
            break;
        case Control::Pause:
            if (stream) {
                check(svc_audio->pause(mod_ctx, stream, fade_frames()), "Pausing music");
            }
            paused = true;
            playing = false;
            message.clear();
            break;
        case Control::Stop:
            stop();
            break;
        case Control::Restart:
            start_at(0, true);
            break;
        case Control::Seek:
            start_at(duration() * seekPercent / 100.0, playing);
            break;
        default:
            break;
        }
    } catch (const std::exception& exception) {
        report(exception);
    }
}

bool disabled(ModContext*, void* data) {
    switch (static_cast<Control>(reinterpret_cast<uintptr_t>(data))) {
    case Control::Choose:
        return picking;
    case Control::AddFile:
    case Control::AddFolder:
        return picking || queue.size() >= queueLimit;
    case Control::Next:
    case Control::ClearQueue:
        return queue.empty();
    case Control::ClearHistory:
        return history.empty();
    case Control::Play:
        return (!track && queue.empty()) || playing;
    case Control::Pause:
        return !playing;
    case Control::Stop:
        return !stream && !startPending && !advancePending;
    case Control::Restart:
    case Control::Seek:
    case Control::Position:
        return !track;
    default:
        return false;
    }
}

void get_value(ModContext*, void* data, UiControlValue* value) {
    switch (static_cast<Control>(reinterpret_cast<uintptr_t>(data))) {
    case Control::Volume:
        value->int_value = volume;
        break;
    case Control::Pitch:
        value->int_value = pitch;
        break;
    case Control::Fade:
        value->int_value = fadeMs;
        break;
    case Control::Position:
        value->int_value = seekPercent;
        break;
    case Control::Loop:
        value->bool_value = loop;
        break;
    case Control::Duck:
        value->bool_value = duckBgm;
        break;
    default:
        break;
    }
}

void set_value(ModContext*, void* data, const UiControlValue* value) {
    try {
        switch (static_cast<Control>(reinterpret_cast<uintptr_t>(data))) {
        case Control::Volume:
            volume = std::clamp<int64_t>(value->int_value, 0, 200);
            if (stream) {
                check(svc_audio->set_volume(mod_ctx, stream, volume / 100.0f, fade_frames()),
                    "Setting volume");
            }
            break;
        case Control::Pitch:
            pitch = std::clamp<int64_t>(value->int_value, 50, 200);
            if (stream) {
                check(svc_audio->set_pitch(mod_ctx, stream, pitch / 100.0f), "Setting pitch");
            }
            break;
        case Control::Fade:
            fadeMs = std::clamp<int64_t>(value->int_value, 0, 5000);
            break;
        case Control::Position:
            seekPercent = std::clamp<int64_t>(value->int_value, 0, 100);
            break;
        case Control::Loop:
            loop = value->bool_value;
            break;
        case Control::Duck:
            duckBgm = value->bool_value;
            if (stream || startPending) {
                start_at(positionSeconds, playing);
            }
            break;
        default:
            break;
        }
    } catch (const std::exception& exception) {
        report(exception);
    }
}

void pump() {
    // File I/O happens only at a track transition. Skip at most one bad entry per update.
    if (advancePending) {
        advancePending = false;
        play_queued(0);
        return;
    }
    if (startPending) {
        AudioStreamDesc desc = AUDIO_STREAM_DESC_INIT;
        desc.format = AUDIO_FORMAT_F32;
        desc.sample_rate = track->decoder.sample_rate();
        desc.channels = track->decoder.channels();
        desc.volume = volume / 100.0f;
        desc.duck_bgm = duckBgm;
        desc.stop_on_scene_change = false;
        const auto result = svc_audio->open(mod_ctx, &desc, &stream);
        if (result == MOD_UNAVAILABLE && ++openRetries < 300) {
            return;
        }
        check(result,
            "Opening audio stream (start the game and free another music stream if necessary)");
        check(svc_audio->set_pitch(mod_ctx, stream, pitch / 100.0f), "Setting pitch");
        if (playing) {
            check(svc_audio->play(mod_ctx, stream, fade_frames()), "Starting playback");
        }
        startPending = false;
        message.clear();
    }
    if (!stream) {
        return;
    }
    check(svc_audio->get_state(mod_ctx, stream, &snapshot), "Reading playback state");
    if (playing && (snapshot.phase == AUDIO_STREAM_PLAYING || snapshot.phase == AUDIO_STREAM_ENDED))
    {
        remember_track();
    }
    positionSeconds = startSeconds + snapshot.position_frames / 32000.0;
    if (wrapped) {
        positionSeconds = std::fmod(positionSeconds, duration());
    } else {
        positionSeconds = std::min(positionSeconds, duration());
    }
    if (snapshot.phase == AUDIO_STREAM_ENDED) {
        if (loop && playing) {
            start_at(0, true);
        } else if (playing && !queue.empty()) {
            close_stream();
            advancePending = true;
        } else {
            close_stream();
            playing = paused = false;
            positionSeconds = duration();
            message.clear();
        }
        return;
    }
    // Bound decode work per update and retain PCM that write() cannot accept yet.
    for (unsigned batch = 0; batch < 4 && !eof; ++batch) {
        uint32_t budget{};
        check(svc_audio->free_frames(mod_ctx, stream, &budget), "Checking stream capacity");
        if (!budget) {
            break;
        }
        if (pendingOffset == pendingFrames) {
            pendingOffset = 0;
            const auto count = std::min(budget, 4096u);
            pendingFrames = track->decoder.read({pcm.data(), count * track->decoder.channels()});
            if (!pendingFrames && loop) {
                if (!track->decoder.seek(0)) {
                    throw std::runtime_error{"Could not rewind the loop."};
                }
                wrapped = true;
                pendingFrames =
                    track->decoder.read({pcm.data(), count * track->decoder.channels()});
                if (!pendingFrames) {
                    throw std::runtime_error{"No audio could be decoded at the loop start."};
                }
            }
            if (!pendingFrames) {
                check(svc_audio->end_of_stream(mod_ctx, stream), "Finishing stream");
                eof = true;
                break;
            }
        }
        uint32_t accepted{};
        check(svc_audio->write(mod_ctx, stream,
                  pcm.data() + pendingOffset * track->decoder.channels(),
                  std::min(budget, pendingFrames - pendingOffset), &accepted),
            "Writing audio");
        pendingOffset += accepted;
        if (!accepted) {
            break;
        }
    }
}

std::string clock_text(double seconds) {
    const auto total = static_cast<uint64_t>(std::max(0.0, seconds));
    char text[40]{};
    std::snprintf(text, sizeof(text), "%llu:%02llu", static_cast<unsigned long long>(total / 60),
        static_cast<unsigned long long>(total % 60));
    return text;
}

ModResult update_view(ModContext*, void* data, ModError*) {
    auto& view = *static_cast<View*>(data);
    const auto set = [](UiElementHandle element, const std::string& text) {
        return svc_ui->elem_set_text(mod_ctx, element, text.c_str());
    };
    set(view.file, track ? track->name : "No music selected");
    char info[180]{};
    if (track) {
        std::snprintf(info, sizeof(info), "%s · %u Hz · %s · %.1f MiB",
            track->decoder.format_name(), track->decoder.sample_rate(),
            track->decoder.channels() == 1 ? "Mono" : "Stereo", track->bytes.size / 1048576.0);
    } else {
        std::snprintf(info, sizeof(info), "MP3 · Ogg Vorbis · FLAC · WAV");
    }
    set(view.format, info);
    set(view.error, message);
    svc_ui->elem_set_class(mod_ctx, view.error, "music-hidden", message.empty());
    set(view.time, clock_text(positionSeconds) + " / " + clock_text(duration()));
    set(view.next, queue.empty() ? "Queue empty" :
                                   "Up next: " + queue.front().name + " · " +
                                       std::to_string(queue.size()) + " queued");
    return svc_ui->elem_set_progress(mod_ctx, view.progress,
        duration() ? std::clamp(positionSeconds / duration(), 0.0, 1.0) : 0);
}

ModResult add_control(UiElementHandle pane, Control id, const char* label,
    UiControlKind kind = UI_CONTROL_BUTTON, int64_t min = 0, int64_t max = 0, int64_t step = 1,
    const char* suffix = nullptr) {
    UiControlDesc desc = UI_CONTROL_DESC_INIT;
    desc.kind = kind;
    desc.label = label;
    switch (id) {
    case Control::Restart:
        desc.icon = "replay";
        break;
    case Control::Play:
        desc.icon = "play_arrow";
        break;
    case Control::Pause:
        desc.icon = "pause";
        break;
    case Control::Stop:
        desc.icon = "stop";
        break;
    case Control::Next:
        desc.icon = "skip_next";
        break;
    case Control::AddFile:
        desc.icon = "note_add";
        break;
    case Control::AddFolder:
        desc.icon = "create_new_folder";
        break;
    case Control::ClearQueue:
    case Control::ClearHistory:
        desc.icon = "delete";
        break;
    default:
        break;
    }
    if (desc.icon != nullptr) {
        desc.kind = UI_CONTROL_ICON_BUTTON;
    }
    desc.user_data = reinterpret_cast<void*>(static_cast<uintptr_t>(id));
    desc.on_pressed = pressed;
    desc.get = get_value;
    desc.set = set_value;
    desc.is_disabled = disabled;
    desc.min = min;
    desc.max = max;
    desc.step = step;
    desc.suffix = suffix;
    UiElementHandle element{};
    const auto result = svc_ui->pane_add_control(mod_ctx, pane, &desc, &element);
    if (result != MOD_OK) {
        return result;
    }
    const char* style = "music-setting";
    switch (id) {
    case Control::Play:
    case Control::Pause:
    case Control::Stop:
    case Control::Restart:
    case Control::Next:
        style = "music-transport";
        break;
    case Control::Position:
    case Control::Seek:
        style = "music-seek";
        break;
    case Control::Choose:
        style = "music-choose";
        break;
    case Control::AddFile:
        style = "music-add-file";
        break;
    case Control::AddFolder:
        style = "music-add-folder";
        break;
    case Control::ClearQueue:
    case Control::ClearHistory:
        style = "music-clear";
        break;
    case Control::Library:
        style = "music-library-action";
        break;
    default:
        break;
    }
    return svc_ui->elem_set_class(mod_ctx, element, style, true);
}

UiElementHandle add_row(
    UiElementHandle parent, const char* style = nullptr, UiRowAlign align = UI_ROW_ALIGN_START) {
    UiRowDesc desc = UI_ROW_DESC_INIT;
    desc.align = align;
    UiElementHandle row{};
    check(svc_ui->pane_add_row(mod_ctx, parent, &desc, &row), "Building row");
    if (style) {
        check(svc_ui->elem_set_class(mod_ctx, row, style, true), "Styling row");
    }
    return row;
}

ModResult build_player(UiElementHandle pane, UiElementHandle settings, void* data) {
    try {
        auto& view = *static_cast<View*>(data);
        view = {};
        check(svc_ui->elem_set_class(mod_ctx, pane, "music-player", true), "Styling player");
        check(
            svc_ui->elem_set_class(mod_ctx, settings, "music-settings", true), "Styling settings");
        const auto text = [&](UiElementHandle* element, const char* style) {
            check(svc_ui->pane_add_text(mod_ctx, pane, "", element), "Building track information");
            check(svc_ui->elem_set_class(mod_ctx, *element, style, true),
                "Styling track information");
        };
        check(svc_ui->pane_add_section(mod_ctx, pane, "Now playing"), "Building player");
        text(&view.file, "music-title");
        text(&view.format, "music-detail");
        text(&view.error, "music-error");
        text(&view.time, "music-time");
        check(svc_ui->pane_add_progress(mod_ctx, pane, 0, &view.progress), "Building progress");
        const auto transport = add_row(pane, "music-transport-row");
        check(add_control(transport, Control::Restart, "Restart"), "Building playback controls");
        check(add_control(transport, Control::Play, "Play"), "Building playback controls");
        check(add_control(transport, Control::Pause, "Pause"), "Building playback controls");
        check(add_control(transport, Control::Stop, "Stop"), "Building playback controls");
        check(add_control(transport, Control::Next, "Next"), "Building playback controls");
        const auto seek = add_row(pane, "music-seek-row");
        check(add_control(seek, Control::Position, "Position", UI_CONTROL_NUMBER, 0, 100, 5, "%"),
            "Building seek control");
        check(add_control(seek, Control::Seek, "Seek"), "Building seek control");
        check(add_control(pane, Control::Choose, "Choose audio file..."), "Building file picker");
        text(&view.next, "music-next");
        const auto actions = add_row(pane);
        check(add_control(actions, Control::AddFile, "Queue file..."), "Building queue controls");
        check(
            add_control(actions, Control::AddFolder, "Queue folder..."), "Building queue controls");
        if (pane == settings) {
            check(add_control(pane, Control::Library, "Open music window..."),
                "Building library control");
        }
        check(svc_ui->pane_add_section(mod_ctx, settings, "Playback"), "Building settings");
        check(add_control(settings, Control::Volume, "Volume", UI_CONTROL_NUMBER, 0, 200, 5, "%"),
            "Building volume control");
        check(add_control(
                  settings, Control::Pitch, "Speed / pitch", UI_CONTROL_NUMBER, 50, 200, 5, "%"),
            "Building pitch control");
        check(add_control(
                  settings, Control::Fade, "Fade time", UI_CONTROL_NUMBER, 0, 5000, 100, " ms"),
            "Building fade control");
        check(add_control(settings, Control::Loop, "Loop track", UI_CONTROL_TOGGLE),
            "Building loop control");
        check(add_control(settings, Control::Duck, "Duck game music", UI_CONTROL_TOGGLE),
            "Building ducking control");
        return update_view(mod_ctx, data, nullptr);
    } catch (const std::exception& exception) {
        report(exception);
        return MOD_ERROR;
    }
}

ModResult build_view(ModContext*, UiElementHandle pane, void* data, ModError*) {
    return build_player(pane, pane, data);
}

ModResult build_window(ModContext*, UiWindowHandle, UiElementHandle left, UiElementHandle right,
    void* data, ModError*) {
    return build_player(left, right, data);
}

void list_pressed(ModContext*, UiListHandle, uint64_t key, void* data) {
    try {
        if (data) {
            const auto found = std::find_if(history.begin(), history.end(),
                [key](const Entry& entry) { return entry.key == key; });
            if (found == history.end()) {
                return;
            }
            libraryMessage = enqueue(*found) ? "" : "Queue limit: 256 tracks.";
            ++libraryRevision;
        } else {
            const auto found = std::find_if(
                queue.begin(), queue.end(), [key](const Entry& entry) { return entry.key == key; });
            if (found != queue.end()) {
                play_queued(static_cast<size_t>(found - queue.begin()));
            }
        }
    } catch (const std::exception& exception) {
        report(exception);
    }
}

ModResult update_library(ModContext*, void*, ModError*) {
    auto& view = libraryView;
    if (view.revision == libraryRevision) {
        return MOD_OK;
    }
    try {
        const auto populate = [](UiListHandle list, const std::vector<Entry>& entries) {
            std::vector<UiListItem> items;
            for (const auto& entry : entries) {
                UiListItem item = UI_LIST_ITEM_INIT;
                item.key = entry.key;
                item.label = entry.name.c_str();
                items.push_back(item);
            }
            check(svc_ui->list_set_items(mod_ctx, list, items.data(), items.size()),
                "Updating music list");
        };
        populate(view.queue, queue);
        populate(view.history, history);
        check(svc_ui->elem_set_text(
                  mod_ctx, view.queueSummary, (std::to_string(queue.size()) + " queued").c_str()),
            "Updating queue");
        check(svc_ui->elem_set_text(mod_ctx, view.historySummary,
                  (std::to_string(history.size()) + " tracks").c_str()),
            "Updating history");
        check(svc_ui->elem_set_text(mod_ctx, view.notice, libraryMessage.c_str()),
            "Updating queue status");
        check(svc_ui->elem_set_class(mod_ctx, view.notice, "music-hidden", libraryMessage.empty()),
            "Updating queue error");
        view.revision = libraryRevision;
        return MOD_OK;
    } catch (const std::exception& exception) {
        report(exception);
        return MOD_ERROR;
    }
}

ModResult build_library(
    ModContext*, UiWindowHandle, UiElementHandle left, UiElementHandle right, void*, ModError*) {
    try {
        libraryView = {};
        for (auto pane : {left, right}) {
            check(svc_ui->elem_set_class(mod_ctx, pane, "music-library", true), "Styling library");
        }
        const auto queueHeader = add_row(left, nullptr, UI_ROW_ALIGN_SPACE_BETWEEN);
        check(svc_ui->pane_add_section(mod_ctx, queueHeader, "Up next"), "Building queue");
        check(
            svc_ui->pane_add_text(mod_ctx, left, "", &libraryView.queueSummary), "Building queue");
        const auto queueActions = add_row(queueHeader);
        check(
            add_control(queueActions, Control::AddFile, "Add file..."), "Building queue controls");
        check(add_control(queueActions, Control::AddFolder, "Add folder..."),
            "Building queue controls");
        check(add_control(queueActions, Control::ClearQueue, "Clear queue"),
            "Building queue controls");
        UiListDesc list = UI_LIST_DESC_INIT;
        list.on_pressed = list_pressed;
        check(
            svc_ui->pane_add_list(mod_ctx, left, &list, &libraryView.queue), "Building queue list");
        check(
            svc_ui->pane_add_text(mod_ctx, left, "", &libraryView.notice), "Building queue status");
        const auto historyHeader = add_row(right, nullptr, UI_ROW_ALIGN_SPACE_BETWEEN);
        check(svc_ui->pane_add_section(mod_ctx, historyHeader, "History"), "Building history");
        check(svc_ui->pane_add_text(mod_ctx, right, "", &libraryView.historySummary),
            "Building history");
        check(add_control(historyHeader, Control::ClearHistory, "Clear history"),
            "Building history controls");
        list.user_data = &libraryView;
        check(svc_ui->pane_add_list(mod_ctx, right, &list, &libraryView.history),
            "Building history list");
        return update_library(mod_ctx, nullptr, nullptr);
    } catch (const std::exception& exception) {
        report(exception);
        return MOD_ERROR;
    }
}

void open_window(ModContext*, void*) {
    if (window) {
        return;
    }
    UiTabDesc tabs[]{UI_TAB_DESC_INIT, UI_TAB_DESC_INIT};
    tabs[0].title = "Player";
    tabs[0].build = build_window;
    tabs[0].update = update_view;
    tabs[0].user_data = &windowView;
    tabs[1].title = "Queue & history";
    tabs[1].build = build_library;
    tabs[1].update = update_library;
    UiWindowDesc desc = UI_WINDOW_DESC_INIT;
    desc.tabs = tabs;
    desc.tab_count = std::size(tabs);
    desc.rcss = "window { max-width: 960dp; max-height: 600dp; }";
    desc.on_closed = [](ModContext*, UiWindowHandle, void*) {
        window = 0;
        windowView = {};
        libraryView = {};
    };
    try {
        check(svc_ui->window_push(mod_ctx, &desc, &window), "Opening music player");
    } catch (const std::exception& exception) {
        report(exception);
    }
}
}  // namespace

extern "C" {
MOD_EXPORT ModResult mod_initialize(ModError*) {
    UiStyleHandle style{};
    const auto styleResult =
        svc_ui->register_styles_file(mod_ctx, UI_SCOPE_WINDOW, "player.rcss", &style);
    if (styleResult != MOD_OK) {
        return styleResult;
    }
    UiModsPanelDesc panel = UI_MODS_PANEL_DESC_INIT;
    panel.build = build_view;
    panel.update = update_view;
    panel.user_data = &panelView;
    auto result = svc_ui->register_mods_panel(mod_ctx, &panel);
    if (result != MOD_OK) {
        return result;
    }
    UiMenuTabDesc menu = UI_MENU_TAB_DESC_INIT;
    menu.label = "Music";
    menu.on_selected = open_window;
    UiMenuTabHandle handle{};
    return svc_ui->register_menu_tab(mod_ctx, &menu, &handle);
}

MOD_EXPORT ModResult mod_update(ModError*) {
    try {
        pump();
    } catch (const std::exception& exception) {
        close_stream();
        playing = paused = false;
        report(exception);
    }
    return MOD_OK;
}

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    close_stream();
    track.reset();
    queue.clear();
    history.clear();
    return MOD_OK;
}
}
