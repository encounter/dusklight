#include "queue.hpp"

#include "dusk/hash.hpp"
#include "dusk/mod_loader.hpp"
#include "dusk/ui/ui.hpp"

#include <borealis/http.hpp>
#include <borealis/io.hpp>
#include <borealis/task.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dusk::mods::queue {
namespace {

using clock = std::chrono::steady_clock;

struct VerifyResult {
    std::string error;
    ModMetadata metadata;
    std::filesystem::path stagedPath;
    bool canceled = false;
};

struct QueueItem {
    std::string key;
    Request request;
    State state = State::Queued;
    std::filesystem::path partialPath;
    std::filesystem::path installPath;
    uint64_t completed = 0;
    uint64_t total = 0;
    std::string message;
    int retryCount = 0;
    clock::time_point retryAt{};
    borealis::Task<borealis::http::Result> task;
    borealis::Task<VerifyResult> verification;
    ModOperationHandle operation;
    bool packagePublished = false;
    bool removeAfterOperation = false;
    bool pauseRequested = false;
    bool resumeRequested = false;
    bool cancelRequested = false;
};

std::vector<QueueItem> queueItems;
uint64_t nextLocalKey = 1;

bool terminal(State state);

QueueItem* find_queue_item(std::string_view key) {
    const auto item = std::ranges::find(queueItems, key,
        [](const QueueItem& candidate) { return std::string_view{candidate.key}; });
    return item == queueItems.end() ? nullptr : &*item;
}

QueueItem* find_queue_item_by_mod_id(std::string_view id) {
    const auto item = std::ranges::find(queueItems, id,
        [](const QueueItem& candidate) { return std::string_view{candidate.request.id}; });
    return item == queueItems.end() ? nullptr : &*item;
}

const Url* url_source(const QueueItem& item) {
    return std::get_if<Url>(&item.request.source);
}

const LocalFile* local_source(const QueueItem& item) {
    return std::get_if<LocalFile>(&item.request.source);
}

const LoadedMod* find_loaded_mod(std::string_view id) {
    for (const auto& mod : ModLoader::instance().mods()) {
        if (mod.metadata.id == id) {
            return &mod;
        }
    }
    return nullptr;
}

std::string lowercase(std::string value) {
    std::ranges::transform(value, value.begin(), [](char character) {
        return character >= 'A' && character <= 'Z' ? static_cast<char>(character + ('a' - 'A')) :
                                                      character;
    });
    return value;
}

bool valid_sha256(std::string_view value) {
    return value.size() == 64 && std::ranges::all_of(value, [](char character) {
        return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
               (character >= 'A' && character <= 'F');
    });
}

std::string safe_filename(std::string_view id) {
    std::string result{id};
    std::ranges::replace_if(
        result,
        [](char character) {
            return !((character >= 'a' && character <= 'z') ||
                     (character >= 'A' && character <= 'Z') ||
                     (character >= '0' && character <= '9') || character == '.' ||
                     character == '_' || character == '-');
        },
        '_');
    return result;
}

std::string sha256_file(
    const std::filesystem::path& path, borealis::TaskContext& context, std::string& error) {
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        error = "Could not open the downloaded package";
        return {};
    }

    hash::Sha256 hash;
    std::array<uint8_t, 64 * 1024> buffer{};
    uint64_t completed = 0;
    while (input) {
        if (context.cancel_requested()) {
            error = "Canceled";
            return {};
        }
        input.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        const auto count = input.gcount();
        if (count > 0) {
            hash.update(std::span{buffer.data(), static_cast<size_t>(count)});
            completed += static_cast<uint64_t>(count);
            context.report_progress(completed);
        }
    }
    if (!input.eof()) {
        error = "Could not read the downloaded package";
        return {};
    }

    return hash.finish();
}

std::filesystem::path staging_path(
    const std::filesystem::path& stagingDir, std::string_view modId, std::string_view key) {
    return stagingDir / fmt::format("{}-{}.dusk.part", safe_filename(modId), safe_filename(key));
}

bool copy_to_staging(const std::filesystem::path& source, const std::filesystem::path& destination,
    uint64_t total, borealis::TaskContext& context, std::string& error) {
    std::error_code filesystemError;
    std::filesystem::create_directories(destination.parent_path(), filesystemError);
    if (filesystemError) {
        error =
            fmt::format("Could not create the staging directory: {}", filesystemError.message());
        return false;
    }
    std::ifstream input{source, std::ios::binary};
    std::ofstream output{destination, std::ios::binary | std::ios::trunc};
    if (!input || !output) {
        error = "Could not stage the local package";
        return false;
    }
    std::array<char, 64 * 1024> buffer{};
    uint64_t completed = 0;
    while (input) {
        if (context.cancel_requested()) {
            error = "Canceled";
            output.close();
            std::filesystem::remove(destination, filesystemError);
            return false;
        }
        input.read(buffer.data(), buffer.size());
        const auto count = input.gcount();
        if (count > 0) {
            output.write(buffer.data(), count);
            completed += static_cast<uint64_t>(count);
            context.report_progress(completed, total);
        }
    }
    if (!input.eof() || !output) {
        error = "Could not copy the local package";
        output.close();
        std::filesystem::remove(destination, filesystemError);
        return false;
    }
    return true;
}

VerifyResult verify_url_package(const std::filesystem::path& path, const Request& request,
    const Url& source, const std::filesystem::path& stagingDir, std::string key,
    borealis::TaskContext& context) {
    std::error_code ec;
    const auto actualSize = std::filesystem::file_size(path, ec);
    if (ec) {
        return {.error = fmt::format("Could not read the downloaded package: {}", ec.message())};
    }
    if (actualSize != source.size) {
        return {.error = "Package size mismatch"};
    }

    std::string error;
    const auto actualHash = sha256_file(path, context, error);
    if (!error.empty()) {
        return {.error = std::move(error)};
    }
    if (context.cancel_requested()) {
        return {.canceled = true};
    }
    if (actualHash != lowercase(source.sha256)) {
        return {.error = "Package checksum mismatch"};
    }

    ModMetadata metadata;
    if (!inspect_mod_bundle(path, metadata, error)) {
        return {.error = fmt::format("Invalid mod package: {}", error)};
    }
    if (metadata.id != request.id) {
        return {.error = "Package ID does not match the catalog entry"};
    }
    if (metadata.version != request.version) {
        return {.error = "Package version does not match the catalog entry"};
    }
    if (context.cancel_requested()) {
        return {.canceled = true};
    }
    const auto stagedPath = staging_path(stagingDir, metadata.id, key);
    std::filesystem::create_directories(stagedPath.parent_path(), ec);
    if (ec) {
        return {.error = fmt::format("Could not create the staging directory: {}", ec.message())};
    }
    std::string replaceError;
    if (!borealis::io::atomic_replace(path, stagedPath, replaceError)) {
        return {.error = std::move(replaceError)};
    }
    return {.metadata = std::move(metadata), .stagedPath = stagedPath};
}

VerifyResult verify_local_package(const LocalFile& source, const std::filesystem::path& stagingDir,
    std::string key, borealis::TaskContext& context) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(source.path, ec);
    if (ec) {
        return {.error = fmt::format("Could not read the local package: {}", ec.message())};
    }
    context.report_progress(0, size);
    ModMetadata metadata;
    std::string error;
    if (!inspect_mod_bundle(source.path, metadata, error)) {
        return {.error = fmt::format("Invalid mod package: {}", error)};
    }
    const auto stagedPath = staging_path(stagingDir, metadata.id, key);
    if (!copy_to_staging(source.path, stagedPath, size, context, error)) {
        return {.error = std::move(error), .canceled = context.cancel_requested()};
    }
    return {.metadata = std::move(metadata), .stagedPath = stagedPath};
}

void remove_partial(const QueueItem& item) {
    std::error_code ec;
    if (!item.partialPath.empty()) {
        std::filesystem::remove(item.partialPath, ec);
        auto metadataPath = item.partialPath;
        metadataPath += ".borealis-resume.json";
        std::filesystem::remove(metadataPath, ec);
    }
    if (!item.installPath.empty()) {
        std::filesystem::remove(item.installPath, ec);
    }
}

void fail(QueueItem& item, State state, std::string message, bool discardPartial) {
    item.task = {};
    item.verification = {};
    item.operation.reset();
    item.state = state;
    item.message = std::move(message);
    item.pauseRequested = false;
    item.resumeRequested = false;
    item.cancelRequested = false;
    item.removeAfterOperation = false;
    if (discardPartial) {
        remove_partial(item);
        item.completed = 0;
    }
    const char* title = state == State::ActivationFailed ? "Mod activation failed" :
                        state == State::InstallFailed    ? "Mod install failed" :
                        local_source(item) != nullptr    ? "Mod package failed" :
                                                           "Mod download failed";
    ui::push_toast({
        .type = "warning",
        .title = title,
        .content = fmt::format("{}: {}", item.request.name, item.message),
        .duration = std::chrono::seconds{6},
    });
}

bool retryable(const borealis::http::Result& result) {
    if (result.error == borealis::http::Error::Network ||
        result.error == borealis::http::Error::Timeout)
    {
        return true;
    }
    const int status = result.response.statusCode;
    return result.error == borealis::http::Error::None &&
           (status == 408 || status == 425 || status == 429 || status >= 500);
}

void schedule_retry(QueueItem& item, std::string message) {
    ++item.retryCount;
    const int delaySeconds = std::min(30, 1 << std::min(item.retryCount, 4));
    item.retryAt = clock::now() + std::chrono::seconds{delaySeconds};
    item.state = State::Retrying;
    item.message = std::move(message);
    item.task = {};
}

void start_download(QueueItem& item) {
    const auto* source = url_source(item);
    if (source == nullptr) {
        fail(item, State::Failed, "The install source is not a URL", false);
        return;
    }
    const auto userDir = ModLoader::instance().user_mods_dir();
    if (userDir.empty()) {
        fail(item, State::Failed, "No writable mods directory is configured", false);
        return;
    }

    item.partialPath =
        userDir / ".downloads" / fmt::format("{}.dusk.part", safe_filename(item.request.id));
    std::error_code ec;
    std::filesystem::create_directories(item.partialPath.parent_path(), ec);
    if (ec) {
        fail(item, State::Failed,
            fmt::format("Could not create the download directory: {}", ec.message()), false);
        return;
    }

    item.pauseRequested = false;
    item.resumeRequested = false;
    item.cancelRequested = false;
    item.message.clear();
    item.total = source->size;
    item.state = State::Downloading;
    item.task = borealis::http::start({
        .url = source->url,
        .downloadTo = item.partialPath,
        .connectTimeout = std::chrono::seconds{10},
        .idleTimeout = std::chrono::seconds{15},
        .totalTimeout = std::nullopt,
    });
}

void start_local_verification(QueueItem& item) {
    const auto* source = local_source(item);
    if (source == nullptr) {
        fail(item, State::Failed, "The install source is not a local file", false);
        return;
    }
    const auto userDir = ModLoader::instance().user_mods_dir();
    if (userDir.empty()) {
        fail(item, State::Failed, "No writable mods directory is configured", false);
        return;
    }
    item.state = State::Verifying;
    item.message.clear();
    item.completed = 0;
    std::error_code error;
    item.total = std::filesystem::file_size(source->path, error);
    const auto stagingDir = userDir / ".staging";
    const auto local = *source;
    item.verification =
        borealis::spawn([local, stagingDir, key = item.key](borealis::TaskContext& context) {
            return verify_local_package(local, stagingDir, key, context);
        });
}

void finish_download(QueueItem& item) {
    const auto progress = item.task.progress();
    item.completed = std::max(item.completed, progress.completed);

    std::optional<borealis::http::Result> completed;
    try {
        completed = item.task.try_take();
    } catch (const std::exception& exception) {
        item.task = {};
        if (item.cancelRequested) {
            remove_partial(item);
            item.state = State::Canceled;
            return;
        }
        if (item.pauseRequested) {
            item.state = item.resumeRequested ? State::Queued : State::Paused;
            item.pauseRequested = false;
            item.resumeRequested = false;
            return;
        }
        schedule_retry(item, exception.what());
        return;
    }
    if (!completed) {
        return;
    }
    item.task = {};

    if (item.cancelRequested) {
        remove_partial(item);
        item.completed = 0;
        item.state = State::Canceled;
        return;
    }
    if (item.pauseRequested) {
        item.state = item.resumeRequested ? State::Queued : State::Paused;
        item.pauseRequested = false;
        item.resumeRequested = false;
        return;
    }

    if (completed->error != borealis::http::Error::None || completed->response.statusCode < 200 ||
        completed->response.statusCode >= 300)
    {
        const auto message = !completed->message.empty() ? completed->message :
                             completed->response.statusCode != 0 ?
                                                           fmt::format("Server returned HTTP {}",
                                                               completed->response.statusCode) :
                                                           "The download failed";
        if (retryable(*completed)) {
            schedule_retry(item, message);
        } else {
            fail(item, State::Failed, message, true);
        }
        return;
    }

    const auto* source = url_source(item);
    if (source == nullptr) {
        fail(item, State::Failed, "The install source changed", true);
        return;
    }
    item.completed = source->size;
    item.state = State::Verifying;
    item.message.clear();
    const auto stagingDir = ModLoader::instance().user_mods_dir() / ".staging";
    item.verification =
        borealis::spawn([path = item.partialPath, request = item.request, source = *source,
                            stagingDir, key = item.key](borealis::TaskContext& context) {
            return verify_url_package(path, request, source, stagingDir, key, context);
        });
}

void start_install(QueueItem& item) {
    auto& loader = ModLoader::instance();
    item.state = State::Installing;
    item.message.clear();
    item.operation = loader.request_install(item.installPath);
}

void publish_verified(QueueItem& item) {
    start_install(item);
}

void finish_verification(QueueItem& item) {
    VerifyResult result;
    try {
        auto completed = item.verification.try_take();
        if (!completed) {
            return;
        }
        result = std::move(*completed);
    } catch (const std::exception& exception) {
        result.error = exception.what();
    } catch (...) {
        result.error = "Package verification failed";
    }
    item.verification = {};
    if (result.canceled || item.cancelRequested) {
        if (!result.stagedPath.empty()) {
            std::error_code error;
            std::filesystem::remove(result.stagedPath, error);
        }
        remove_partial(item);
        item.state = State::Canceled;
        item.completed = 0;
        return;
    }
    if (!result.error.empty()) {
        fail(item, State::Failed, std::move(result.error), true);
        return;
    }
    if (const auto duplicate = find_queue_item_by_mod_id(result.metadata.id);
        duplicate != nullptr && duplicate != &item && !terminal(duplicate->state))
    {
        fail(item, State::InstallFailed, "This mod already has an active install", true);
        return;
    }
    if (local_source(item) != nullptr && !item.request.id.empty() &&
        (item.request.id != result.metadata.id || item.request.version != result.metadata.version))
    {
        fail(item, State::InstallFailed, "The local package changed after confirmation", true);
        return;
    }
    item.request.id = result.metadata.id;
    item.request.name = result.metadata.name;
    item.request.version = result.metadata.version;
    item.installPath = std::move(result.stagedPath);
    item.completed = item.total;
    publish_verified(item);
}

void update_install(QueueItem& item) {
    if (item.operation == nullptr || item.operation->state == ModOperation::State::Pending) {
        return;
    }

    const auto* mod = find_loaded_mod(item.request.id);
    item.packagePublished = mod != nullptr && mod->metadata.version == item.request.version;
    if (item.operation->state == ModOperation::State::Failed) {
        const bool activationFailed =
            item.packagePublished &&
            (mod->loadFailed || (mod->cvarIsEnabled->getValue() && !mod->active));
        fail(item, activationFailed ? State::ActivationFailed : State::InstallFailed,
            item.operation->message.empty() ? "The mod could not be installed" :
                                              item.operation->message,
            false);
        return;
    }

    item.message = item.operation->message;
    item.operation.reset();
    if (mod == nullptr || mod->metadata.version != item.request.version) {
        fail(item, State::InstallFailed, "The installed package was not loaded", false);
        return;
    }

    item.state = State::Installed;
    ui::push_toast({
        .title = "Mod installed",
        .content = fmt::format("{} {}", item.request.name, item.request.version),
        .duration = std::chrono::seconds{4},
    });
}

void update_uninstall(QueueItem& item) {
    if (item.operation == nullptr || item.operation->state == ModOperation::State::Pending) {
        return;
    }
    if (item.operation->state == ModOperation::State::Failed) {
        fail(item, State::ActivationFailed,
            item.operation->message.empty() ?
                "The mod could not be uninstalled" :
                fmt::format("Uninstall failed: {}", item.operation->message),
            false);
        return;
    }

    item.operation.reset();
    item.state = State::Canceled;
    item.message.clear();
}

Item snapshot(const QueueItem& item) {
    Item result{
        .id = item.key,
        .modId = item.request.id,
        .name = item.request.name,
        .version = item.request.version,
        .state = item.state,
        .completed = item.completed,
        .total = item.total,
        .message = item.message,
        .local = local_source(item) != nullptr,
    };
    if (item.task) {
        result.completed = std::max(result.completed, item.task.progress().completed);
    }
    if (item.verification) {
        const auto progress = item.verification.progress();
        result.completed = progress.completed;
        if (progress.total) {
            result.total = *progress.total;
        }
    }
    if (item.state == State::Retrying) {
        const auto remaining = item.retryAt - clock::now();
        result.retrySeconds = std::max(
            0, static_cast<int>(std::chrono::ceil<std::chrono::seconds>(remaining).count()));
    }
    return result;
}

bool terminal(State state) {
    return state == State::Installed || state == State::Failed || state == State::InstallFailed ||
           state == State::ActivationFailed || state == State::Canceled;
}

}  // namespace

bool enqueue(Request request, std::string* keyOut) {
    const auto* source = std::get_if<Url>(&request.source);
    const auto* local = std::get_if<LocalFile>(&request.source);
    if (source != nullptr) {
        if (request.id.empty() || request.version.empty() || source->size == 0 ||
            !source->url.starts_with("https://") || !valid_sha256(source->sha256))
        {
            return false;
        }
    } else if (local == nullptr || request.id.empty() || request.version.empty()) {
        return false;
    }
    const auto total = source == nullptr ? 0 : source->size;
    if (request.name.empty()) {
        request.name = request.id;
    }

    if (auto* existing = find_queue_item_by_mod_id(request.id)) {
        if (!terminal(existing->state)) {
            return false;
        }
        existing->request = std::move(request);
        existing->state = State::Queued;
        existing->completed = 0;
        existing->total = total;
        existing->partialPath.clear();
        existing->installPath.clear();
        existing->message.clear();
        existing->retryCount = 0;
        existing->operation.reset();
        existing->packagePublished = false;
        existing->removeAfterOperation = false;
        existing->pauseRequested = false;
        existing->resumeRequested = false;
        existing->cancelRequested = false;
        if (keyOut != nullptr) {
            *keyOut = existing->key;
        }
        return true;
    }

    const auto key = source != nullptr ? request.id : fmt::format("local-{}", nextLocalKey++);
    if (keyOut != nullptr) {
        *keyOut = key;
    }
    queueItems.push_back({.key = key, .request = std::move(request), .total = total});
    return true;
}

void update() {
    for (auto& item : queueItems) {
        if (item.task && item.task.ready()) {
            finish_download(item);
        }
        if (item.state == State::Verifying && item.verification && item.verification.ready()) {
            finish_verification(item);
        }
        if (item.state == State::Installing) {
            update_install(item);
        }
        if (item.state == State::Activating) {
            update_install(item);
        }
        if (item.state == State::Uninstalling) {
            update_uninstall(item);
        }
        if (item.state == State::ActivationFailed && item.packagePublished) {
            const auto* mod = find_loaded_mod(item.request.id);
            if (mod != nullptr && mod->active && mod->metadata.version == item.request.version) {
                item.state = State::Installed;
                item.message.clear();
            }
        }
    }

    std::erase_if(queueItems, [](const QueueItem& item) {
        if (item.removeAfterOperation && item.state == State::Canceled) {
            return true;
        }
        const bool trackedInstalledState =
            item.state == State::Installed || item.state == State::ActivationFailed;
        return item.packagePublished && trackedInstalledState &&
               find_loaded_mod(item.request.id) == nullptr;
    });

    for (auto& item : queueItems) {
        if (item.task || item.verification) {
            return;
        }
        if (terminal(item.state) || item.state == State::Paused) {
            continue;
        }
        if (item.state == State::Downloading || item.state == State::Verifying ||
            item.state == State::Installing || item.state == State::Activating ||
            item.state == State::Uninstalling)
        {
            return;
        }
        if (item.state == State::Retrying && clock::now() < item.retryAt) {
            return;
        }
        if (item.state == State::Queued || item.state == State::Retrying) {
            if (local_source(item) != nullptr) {
                start_local_verification(item);
            } else {
                start_download(item);
            }
        }
        return;
    }
}

void shutdown() noexcept {
    for (auto& item : queueItems) {
        if (item.task) {
            item.task.cancel();
        }
        if (item.verification) {
            item.verification.cancel();
        }
    }
    queueItems.clear();
}

std::vector<Item> items() {
    std::vector<Item> result;
    result.reserve(queueItems.size());
    for (const auto& item : queueItems) {
        result.push_back(snapshot(item));
    }
    return result;
}

std::optional<Item> find(std::string_view id) {
    const auto* item = find_queue_item(id);
    return item == nullptr ? std::nullopt : std::optional<Item>{snapshot(*item)};
}

std::optional<Item> find_by_mod_id(std::string_view id) {
    const auto* item = find_queue_item_by_mod_id(id);
    return item == nullptr ? std::nullopt : std::optional<Item>{snapshot(*item)};
}

bool has_active_items() {
    return std::ranges::any_of(
        queueItems, [](const QueueItem& item) { return !terminal(item.state); });
}

void pause(std::string_view id) {
    auto* item = find_queue_item(id);
    if (item == nullptr || local_source(*item) != nullptr) {
        return;
    }
    if (item->state == State::Queued || item->state == State::Retrying) {
        item->state = State::Paused;
        return;
    }
    if (item->state == State::Downloading && item->task) {
        item->completed = std::max(item->completed, item->task.progress().completed);
        item->pauseRequested = true;
        item->state = State::Paused;
        item->task.cancel();
    }
}

void resume(std::string_view id) {
    auto* item = find_queue_item(id);
    if (item == nullptr || item->state != State::Paused) {
        return;
    }
    if (item->task) {
        item->resumeRequested = true;
    } else {
        item->state = State::Queued;
    }
}

void retry(std::string_view id) {
    auto* item = find_queue_item(id);
    if (item == nullptr) {
        return;
    }

    if (item->state == State::ActivationFailed) {
        item->message.clear();
        item->state = State::Activating;
        item->operation = ModLoader::instance().request_reactivate(item->request.id);
        return;
    }
    if (item->state == State::InstallFailed) {
        item->message.clear();
        std::error_code ec;
        if (!item->installPath.empty() && std::filesystem::is_regular_file(item->installPath, ec)) {
            start_install(*item);
        } else {
            item->completed = 0;
            item->state = State::Queued;
        }
        return;
    }
    if (item->state == State::Failed) {
        remove_partial(*item);
        item->completed = 0;
        item->retryCount = 0;
        item->message.clear();
        item->state = State::Queued;
    }
}

void cancel(std::string_view id) {
    auto* item = find_queue_item(id);
    if (item == nullptr || item->state == State::Installing || item->state == State::Activating ||
        item->state == State::Uninstalling)
    {
        return;
    }
    if ((item->state == State::ActivationFailed || item->state == State::InstallFailed) &&
        item->packagePublished)
    {
        const auto* mod = find_loaded_mod(item->request.id);
        if (mod == nullptr) {
            std::error_code ec;
            if (!std::filesystem::remove(item->installPath, ec) && ec) {
                fail(*item, State::InstallFailed, fmt::format("Uninstall failed: {}", ec.message()),
                    false);
                return;
            }
            item->state = State::Canceled;
            item->message.clear();
            return;
        }
        item->message.clear();
        item->state = State::Uninstalling;
        item->removeAfterOperation = true;
        item->operation = ModLoader::instance().request_uninstall(item->request.id);
        return;
    }
    if (item->task) {
        item->cancelRequested = true;
        item->pauseRequested = false;
        item->resumeRequested = false;
        item->message = "Canceling...";
        item->task.cancel();
        return;
    }
    if (item->verification) {
        item->cancelRequested = true;
        item->message = "Canceling...";
        item->verification.cancel();
        return;
    }
    remove_partial(*item);
    item->completed = 0;
    item->state = State::Canceled;
}

void pause_all() {
    std::vector<std::string> ids;
    for (const auto& item : queueItems) {
        if (item.state == State::Queued || item.state == State::Retrying ||
            item.state == State::Downloading)
        {
            ids.push_back(item.key);
        }
    }
    for (const auto& id : ids) {
        pause(id);
    }
}

void clear_finished() {
    std::erase_if(queueItems, [](const QueueItem& item) {
        return item.state == State::Installed || item.state == State::Canceled;
    });
}

}  // namespace dusk::mods::queue
