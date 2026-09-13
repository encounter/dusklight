#include "dusk/main.h"
#include "dusk/mod_loader.hpp"
#include "dusk/mods/loader/native_module.hpp"
#include "dusk/save_manager.hpp"
#include "m_Do/m_Do_MemCard.h"

#include <aurora/card.h>
#include <fmt/format.h>
#include <gtest/gtest.h>

#include <fstream>

namespace dusk {
std::filesystem::path CachePath;
}

namespace dusk::mods {
ModLoader& ModLoader::instance() {
    // These fixtures exercise saves without installed mods.
    static auto* loader = new ModLoader{};
    return *loader;
}
}  // namespace dusk::mods

namespace dusk::mods::svc {
void invalidate_save(std::string_view) {}
}  // namespace dusk::mods::svc

mDoMemCd_Ctrl_c::mDoMemCd_Ctrl_c() : mCardCommand{COMM_NONE_e} {}
mDoMemCd_Ctrl_c g_mDoMemCd_control;

namespace {
using namespace dusk::save_manager;

class SaveManagerTest : public testing::Test {
protected:
    void SetUp() override {
        directory = std::filesystem::temp_directory_path() /
                    fmt::format("dusklight-save-test-{}",
                        std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(directory);
        dusk::CachePath = directory / "cache";
        storage = {.kind = StorageKind::GciDirectory, .path = directory / "card"};
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(directory, ec);
        dusk::CachePath.clear();
    }

    static Artifact make_save(uint8_t payload = 0x31) {
        Artifact artifact{.kind = ArtifactKind::Gci};
        artifact.gci.resize(0x40 + 0x2000, payload);
        std::memcpy(artifact.gci.data(), "GZ2E01", 6);
        std::fill_n(artifact.gci.begin() + 8, 32, uint8_t{});
        std::memcpy(artifact.gci.data() + 8, "gczelda2", 8);
        artifact.gci[0x38] = 0;
        artifact.gci[0x39] = 1;
        return artifact;
    }

    static void write_file(const std::filesystem::path& path, std::span<const uint8_t> bytes) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out{path, std::ios::binary};
        out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        ASSERT_TRUE(out.good());
    }

    std::filesystem::path directory;
    Storage storage;
    SaveIdentity identity{.maker = "01", .game = "GZ2E", .saveName = "gczelda2"};
};

TEST_F(SaveManagerTest, ImportsExportsAndRestoresWithModData) {
    const auto original = make_save();
    ASSERT_TRUE(import_artifact(storage, identity, original, ModDataAction::Clear));
    const std::vector<uint8_t> modData{'{', '}'};
    const auto sidecar = save_sidecar_directory(storage.path, storage.kind, identity.maker,
                             identity.game, identity.saveName) /
                         "example.json";
    write_file(sidecar, modData);
    auto exported = build_export(storage, identity, true);
    ASSERT_TRUE(exported);
    auto archive = read_artifact(exported.value.path.string());
    ASSERT_TRUE(archive);
    EXPECT_EQ(archive.value.gci, original.gci);
    EXPECT_EQ(archive.value.modFiles.at("example"), modData);

    ASSERT_TRUE(import_artifact(storage, identity, make_save(0x72), ModDataAction::Clear));
    EXPECT_FALSE(std::filesystem::exists(sidecar));
    auto backups = list_backups(storage, identity);
    ASSERT_TRUE(backups);
    ASSERT_EQ(backups.value.size(), 1);
    ASSERT_TRUE(restore_backup(storage, identity, backups.value.front().path));
    auto restored = build_export(storage, identity, true);
    ASSERT_TRUE(restored);
    auto restoredArchive = read_artifact(restored.value.path.string());
    ASSERT_TRUE(restoredArchive);
    EXPECT_EQ(restoredArchive.value.gci, original.gci);
    EXPECT_EQ(restoredArchive.value.modFiles, archive.value.modFiles);
}

TEST_F(SaveManagerTest, FailedReplacementPreservesAlternateGci) {
    const auto original = make_save();
    const auto alternate = storage.path / "renamed.gci";
    write_file(alternate, original.gci);
    // A directory at the canonical destination forces atomic replacement to fail.
    std::filesystem::create_directory(storage.path / "01-GZ2E-gczelda2.gci");
    EXPECT_FALSE(import_artifact(storage, identity, make_save(0x72), ModDataAction::Clear));
    auto preserved = read_artifact(alternate.string());
    ASSERT_TRUE(preserved);
    EXPECT_EQ(preserved.value.gci, original.gci);
}

TEST_F(SaveManagerTest, KeepsNewestBackupsAndSeparatesModePrefixes) {
    const auto backupDir = storage.path / "backups";
    std::filesystem::create_directories(backupDir);
    const auto now = std::filesystem::file_time_type::clock::now();
    for (int i = 0; i < 12; ++i) {
        const auto path =
            backupDir / fmt::format("01-GZ2E-gczelda2-20260912T120000Z-{}.dusksave", i);
        write_file(path, {});
        std::filesystem::last_write_time(path, now + std::chrono::seconds{i});
    }
    const auto otherMode = backupDir / "01-GZ2E-gczelda2-other-20260912T120000Z.dusksave";
    write_file(otherMode, {});
    auto backups = list_backups(storage, identity);
    ASSERT_TRUE(backups);
    ASSERT_EQ(backups.value.size(), 12);
    EXPECT_TRUE(backups.value.front().name.ends_with("-11.dusksave"));
    ASSERT_TRUE(import_artifact(storage, identity, make_save(), ModDataAction::Clear));
    ASSERT_TRUE(import_artifact(storage, identity, make_save(0x72), ModDataAction::Clear));
    backups = list_backups(storage, identity);
    ASSERT_TRUE(backups);
    EXPECT_EQ(backups.value.size(), kDefaultBackupRetention);
    EXPECT_TRUE(std::filesystem::exists(otherMode));
    EXPECT_TRUE(std::ranges::any_of(backups.value,
        [](const BackupInfo& backup) { return std::filesystem::file_size(backup.path) != 0; }));
}

TEST_F(SaveManagerTest, RejectsAnotherRegionWithoutChangingTheSave) {
    const auto original = make_save();
    ASSERT_TRUE(import_artifact(storage, identity, original, ModDataAction::Clear));
    auto foreign = make_save(0x72);
    foreign.gci[3] = 'P';
    EXPECT_FALSE(import_artifact(storage, identity, foreign, ModDataAction::Clear));
    auto exported = build_export(storage, identity, false);
    ASSERT_TRUE(exported);
    auto preserved = read_artifact(exported.value.path.string());
    ASSERT_TRUE(preserved);
    EXPECT_EQ(preserved.value.gci, original.gci);
}

TEST_F(SaveManagerTest, BacksUpBeforeDeletingModData) {
    ASSERT_TRUE(import_artifact(storage, identity, make_save(), ModDataAction::Clear));
    const auto sidecar = save_sidecar_directory(storage.path, storage.kind, identity.maker,
                             identity.game, identity.saveName) /
                         "example.json";
    const std::vector<uint8_t> modData{'{', '}'};
    write_file(sidecar, modData);
    ASSERT_TRUE(delete_mod_data(storage, identity, "example"));
    EXPECT_FALSE(std::filesystem::exists(sidecar));
    auto backups = list_backups(storage, identity);
    ASSERT_TRUE(backups);
    ASSERT_EQ(backups.value.size(), 1);
    auto backup = read_artifact(backups.value.front().path.string());
    ASSERT_TRUE(backup);
    EXPECT_EQ(backup.value.modFiles.at("example"), modData);
}
}  // namespace
