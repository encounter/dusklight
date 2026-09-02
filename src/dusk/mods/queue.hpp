#pragma once

#include <cstddef>
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
    Handoff,
    Failed,
    Canceled,
};

[[nodiscard]] constexpr bool is_terminal(State state) noexcept {
    return state == State::Failed || state == State::Canceled;
}

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

/** Adds an install, replacing failed or canceled work for the same package ID. */
bool enqueue(Request request, std::string* key = nullptr);

void update();
void shutdown() noexcept;

[[nodiscard]] std::vector<Item> items();
[[nodiscard]] std::optional<Item> find(std::string_view key);
[[nodiscard]] std::optional<Item> find_by_mod_id(std::string_view id);
[[nodiscard]] bool has_active_items();
[[nodiscard]] size_t item_count() noexcept;
[[nodiscard]] size_t active_count() noexcept;
[[nodiscard]] std::optional<Item> first_active();
[[nodiscard]] size_t active_items_ahead(std::string_view id) noexcept;

void pause(std::string_view id);
void resume(std::string_view id);
void retry(std::string_view id);
void cancel(std::string_view id);
void pause_all();
void clear_finished();

}  // namespace dusk::mods::queue
