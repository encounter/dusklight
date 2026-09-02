#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace dusk::mods::queue {

enum class State {
    Queued,
    Downloading,
    Paused,
    Retrying,
    Verifying,
    Installing,
    Activating,
    Installed,
    Failed,
    InstallFailed,
    ActivationFailed,
    Uninstalling,
    Canceled,
};

struct Url {
    std::string url;
    std::string sha256;
    uint64_t size = 0;
};

struct LocalFile {
    std::filesystem::path path;
};

using Source = std::variant<Url, LocalFile>;

struct Request {
    std::string id;
    std::string name;
    std::string version;
    Source source;
};

struct Item {
    // Queue key. URL installs use their mod ID; local installs receive a generated key.
    std::string id;
    std::string modId;
    std::string name;
    std::string version;
    State state = State::Queued;
    uint64_t completed = 0;
    uint64_t total = 0;
    std::string message;
    int retrySeconds = 0;
    bool local = false;
};

/** Adds an install, replacing terminal URL history for the same package ID. */
bool enqueue(Request request, std::string* key = nullptr);

/** Polls transfer and verification work. Call once per UI frame on the main thread. */
void update();
void shutdown() noexcept;

[[nodiscard]] std::vector<Item> items();
[[nodiscard]] std::optional<Item> find(std::string_view key);
[[nodiscard]] std::optional<Item> find_by_mod_id(std::string_view id);
[[nodiscard]] bool has_active_items();

void pause(std::string_view id);
void resume(std::string_view id);
void retry(std::string_view id);
void cancel(std::string_view id);
void pause_all();
void clear_finished();

}  // namespace dusk::mods::queue
