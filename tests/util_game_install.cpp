#include <gtest/gtest.h>

#include <nw/util/game_install.hpp>
#include <nw/util/platform.hpp>
#include <nw/util/scope_exit.hpp>

#include <nowide/cstdlib.hpp>

#include <fstream>
#include <optional>
#include <stdexcept>

#if defined(ROLLNW_OS_WINDOWS)
#include <shlobj.h>
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

auto restore_environment(const char* name)
{
    const auto* value = nowide::getenv(name);
    std::optional<std::string> saved;
    if (value) { saved = value; }
    return create_scope_exit([name, saved] {
        if (saved) {
            nowide::setenv(name, saved->c_str(), 1);
        } else {
            nowide::unsetenv(name);
        }
    });
}

struct GameInstall : testing::Test {
    fs::path root = nw::create_unique_tmp_path();

    ~GameInstall() override
    {
        std::error_code error;
        fs::remove_all(root, error);
    }

    fs::path install(const fs::path& name, const char* key = "data/nwn_base.key")
    {
        const auto result = root / name;
        fs::create_directories((result / key).parent_path());
        std::ofstream{result / key}.put('\0');
        return result;
    }
};

} // namespace

TEST_F(GameInstall, ExplicitUnicodeDirectoriesTakePrecedence)
{
    const auto restore_root = restore_environment("NWN_ROOT");
    const auto restore_user = restore_environment("NWN_HOME");
    const auto game = install(fs::path{u8"GOG Games/NWN \u00e9"});
    const auto user = root / fs::path{u8"OneDrive/MyDocuments/\u6587\u66f8/Neverwinter Nights"};
    fs::create_directories(user);
#if defined(ROLLNW_OS_LINUX)
    // Isolate the default directory too: it must exist to reproduce the old
    // override-clobbering behavior, without writing into the real user's home.
    const auto restore_home = restore_environment("HOME");
    ASSERT_EQ(nowide::setenv("HOME", nw::path_to_string(root).c_str(), 1), 0);
    fs::create_directories(root / ".local/share/Neverwinter Nights");
#endif
    ASSERT_EQ(nowide::setenv("NWN_ROOT", nw::path_to_string(game).c_str(), 1), 0);
    ASSERT_EQ(nowide::setenv("NWN_HOME", nw::path_to_string(user).c_str(), 1), 0);

    const auto found = nw::probe_nwn_install(nw::GameVersion::vEE);
    EXPECT_EQ(found.install, game);
    EXPECT_EQ(found.user, user);
    EXPECT_EQ(found.version, nw::GameVersion::vEE);
}

TEST_F(GameInstall, RejectsDirectoriesAsKeysAndFilesAsUserDirectories)
{
    const auto restore_root = restore_environment("NWN_ROOT");
    const auto restore_user = restore_environment("NWN_HOME");
    const auto game = root / "invalid-install";
    fs::create_directories(game / "data/nwn_base.key");
    const auto user = root / "not-a-directory";
    std::ofstream{user}.put('\0');
    ASSERT_EQ(nowide::setenv("NWN_ROOT", nw::path_to_string(game).c_str(), 1), 0);
    ASSERT_EQ(nowide::setenv("NWN_HOME", nw::path_to_string(user).c_str(), 1), 0);

    const auto found = nw::probe_nwn_install(nw::GameVersion::vEE);
    EXPECT_NE(found.install, game);
    EXPECT_NE(found.user, user);
}

TEST_F(GameInstall, LegacyVersionUsesItsInstallationForUserData)
{
    const auto restore_root = restore_environment("NWN_ROOT");
    const auto game = install("diamond", "chitin.key");
    ASSERT_EQ(nowide::setenv("NWN_ROOT", nw::path_to_string(game).c_str(), 1), 0);

    const auto found = nw::probe_nwn_install(nw::GameVersion::v1_69);
    EXPECT_EQ(found.install, game);
    EXPECT_EQ(found.user, game);
    EXPECT_EQ(found.version, nw::GameVersion::v1_69);
}

#if defined(ROLLNW_OS_LINUX)
TEST_F(GameInstall, DefaultCandidatesUseTheCurrentEnvironment)
{
    const auto restore_root = restore_environment("NWN_ROOT");
    const auto restore_user = restore_environment("NWN_HOME");
    const auto restore_home = restore_environment("HOME");
    ASSERT_EQ(nowide::unsetenv("NWN_ROOT"), 0);
    ASSERT_EQ(nowide::unsetenv("NWN_HOME"), 0);
    ASSERT_EQ(nowide::setenv("HOME", nw::path_to_string(root).c_str(), 1), 0);
    const auto game = install(".local/share/Steam/steamapps/common/Neverwinter Nights");
    const auto user = root / ".local/share/Neverwinter Nights";
    fs::create_directories(user);

    const auto found = nw::probe_nwn_install(nw::GameVersion::vEE);
    EXPECT_TRUE(fs::equivalent(found.install, game));
    EXPECT_EQ(found.user, user);
}
#endif

#if defined(ROLLNW_OS_WINDOWS)
TEST_F(GameInstall, GogRegistryFindsCustomDirectoryAndHonorsRootOverride)
{
    const auto restore_root = restore_environment("NWN_ROOT");
    const auto restore_user = restore_environment("NWN_HOME");
    const auto registry_name = L"Software\\rollnw-tests\\" + root.filename().wstring();
    HKEY registry = nullptr;
    ASSERT_EQ(RegCreateKeyExW(HKEY_CURRENT_USER, registry_name.c_str(), 0,
                  nullptr, 0, KEY_ALL_ACCESS, nullptr, &registry, nullptr),
        ERROR_SUCCESS);
    const auto cleanup_registry = create_scope_exit([&] {
        RegOverridePredefKey(HKEY_LOCAL_MACHINE, nullptr);
        RegCloseKey(registry);
        RegDeleteTreeW(HKEY_CURRENT_USER, registry_name.c_str());
    });
    // Redirect only this process's HKLM lookup to disposable test keys. Never
    // modify the real GOG registration or require administrator privileges.
    ASSERT_EQ(RegOverridePredefKey(HKEY_LOCAL_MACHINE, registry), ERROR_SUCCESS);
    const auto game = install("custom-drive/GOG/Neverwinter Nights Enhanced Edition");
    const auto path = game.wstring();
    HKEY gog = nullptr;
    ASSERT_EQ(RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                  L"SOFTWARE\\GOG.com\\Games\\1097893768", 0, nullptr, 0,
                  KEY_SET_VALUE | KEY_WOW64_32KEY, nullptr, &gog, nullptr),
        ERROR_SUCCESS);
    const auto close_gog = create_scope_exit([&] { RegCloseKey(gog); });
    ASSERT_EQ(RegSetValueExW(gog, L"path", 0, REG_SZ,
                  reinterpret_cast<const BYTE*>(path.c_str()),
                  static_cast<DWORD>((path.size() + 1) * sizeof(wchar_t))),
        ERROR_SUCCESS);
    ASSERT_EQ(nowide::unsetenv("NWN_ROOT"), 0);
    ASSERT_EQ(nowide::setenv("NWN_HOME", nw::path_to_string(root).c_str(), 1), 0);
    const auto found = nw::probe_nwn_install(nw::GameVersion::vEE);
    EXPECT_EQ(found.install, game);
    EXPECT_EQ(found.version, nw::GameVersion::vEE);

    const auto explicit_game = install("explicit-install");
    ASSERT_EQ(nowide::setenv("NWN_ROOT", nw::path_to_string(explicit_game).c_str(), 1), 0);
    EXPECT_EQ(nw::probe_nwn_install(nw::GameVersion::vEE).install, explicit_game);

    ASSERT_EQ(nowide::unsetenv("NWN_ROOT"), 0);
    const DWORD invalid_path = 42;
    ASSERT_EQ(RegSetValueExW(gog, L"path", 0, REG_DWORD,
                  reinterpret_cast<const BYTE*>(&invalid_path), sizeof(invalid_path)),
        ERROR_SUCCESS);
    EXPECT_NE(nw::probe_nwn_install(nw::GameVersion::vEE).install, game);
}

TEST_F(GameInstall, DocumentsUsesTheCurrentWindowsKnownFolder)
{
    PWSTR path = nullptr;
    const auto cleanup = create_scope_exit([&] { CoTaskMemFree(path); });
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents,
            KF_FLAG_DEFAULT, nullptr, &path))) {
        EXPECT_THROW(nw::documents_path(), std::runtime_error);
        return;
    }
    ASSERT_NE(path, nullptr);
    EXPECT_EQ(nw::documents_path().native(), std::wstring{path});
}
#endif
