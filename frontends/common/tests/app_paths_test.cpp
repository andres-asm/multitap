// Checks the program folder rules (app_paths.h): the XDG rule, the
// subfolders, and the one-time migration from the old folder.
//
// Usage: frontend_app_paths_test <scratch directory>
// Exit code 0 = all checks passed. Each failure is printed.

#include <cstdio>
#include <cstring>
#include <string>

#include "app_paths.h"
#include "host_fs.h"
#include "settings_file.h"

static int g_failures = 0;

static void check(bool condition, const char* what)
{
    if (!condition)
    {
        std::printf("FAIL %s\n", what);
        g_failures++;
    }
}

static void check_root(const char* xdg, const char* home, const char* expected, const char* what)
{
    std::string root;
    bool found = app_paths_xdg_root(xdg, home, "piccolo", &root);
    bool same = expected == nullptr ? !found : found && root == expected;
    if (!same)
    {
        std::printf("FAIL %s: expected %s, got %s\n", what, expected != nullptr ? expected : "(none)",
                    found ? root.c_str() : "(none)");
        g_failures++;
    }
}

static void test_xdg_rule()
{
    check_root(nullptr, "/home/ana", "/home/ana/.local/share/piccolo", "no XDG_DATA_HOME: ~/.local/share");
    check_root("", "/home/ana/", "/home/ana/.local/share/piccolo", "empty XDG_DATA_HOME, home with a slash");
    check_root("/data/xdg", "/home/ana", "/data/xdg/piccolo", "absolute XDG_DATA_HOME");
    check_root("/data/xdg/", "/home/ana", "/data/xdg/piccolo", "XDG_DATA_HOME with a trailing slash");
    check_root("relative/dir", "/home/ana", "/home/ana/.local/share/piccolo", "relative XDG_DATA_HOME ignored");
    check_root(nullptr, nullptr, nullptr, "nothing to go on");

    AppPaths paths = {};
    paths.root = "/home/ana/.local/share/piccolo";
    app_paths_fill(&paths);
    check(paths.settings == "/home/ana/.local/share/piccolo/settings", "settings subfolder");
    check(paths.cores == "/home/ana/.local/share/piccolo/cores", "cores subfolder");
    check(paths.saves == "/home/ana/.local/share/piccolo/saves", "saves subfolder");
    check(paths.states == "/home/ana/.local/share/piccolo/states", "states subfolder");
    check(paths.system == "/home/ana/.local/share/piccolo/system", "system subfolder");
}

static void write_text(const std::string& path, const char* text)
{
    check(host_fs_write_file(path.c_str(), text, std::strlen(text)), "write a test file");
}

static bool file_is(const std::string& path, const char* text)
{
    uint8_t* data = nullptr;
    size_t size = 0;
    if (!host_fs_exists(path.c_str()) || !host_fs_read_file(path.c_str(), &data, &size))
    {
        return false;
    }
    bool same = size == std::strlen(text) && std::memcmp(data, text, size) == 0;
    delete[] data;
    return same;
}

static void test_migration(const char* scratch)
{
    std::string base = scratch;
    base += "/run";
    std::string old_root = base + "/old home/.host-libretro";
    std::string new_root = base + "/new/piccolo";

    // CTest empties the scratch folder before this runs (a fixture in
    // CMakeLists.txt), so the new folder doesn't exist yet.
    write_text(old_root + "/cores/snes9x_libretro.so", "core");
    write_text(old_root + "/saves/snes9x/Pokémon ポケモン.srm", "sram");
    write_text(old_root + "/states/snes9x/Pokémon ポケモン.state1", "state");
    write_text(old_root + "/piccolo", "the old binary, not copied");
    write_text(old_root + "/piccolo.cfg",
               ("core=" + old_root + "/cores/snes9x_libretro.so\n" + "folder=/media/roms\n" +
                "other=~/.host-libretro/cores\n" + "near=" + old_root + "-old/x\n")
                   .c_str());

    // The build has already copied the program and the test core into the new
    // folder before the program's first run: the migration must still happen,
    // and must not overwrite them.
    write_text(new_root + "/piccolo", "the new binary");
    write_text(new_root + "/cores/test_libretro.so", "new test core");
    write_text(new_root + "/cores/snes9x_libretro.so", "newer core");

    AppPaths paths = {};
    paths.root = new_root;
    app_paths_fill(&paths);
    check(app_paths_setup(&paths, old_root.c_str(), "piccolo.cfg"), "first setup migrates");

    check(file_is(new_root + "/cores/snes9x_libretro.so", "newer core"), "a core already there is kept");
    check(file_is(new_root + "/cores/test_libretro.so", "new test core"), "the build's test core is kept");
    check(file_is(new_root + "/saves/snes9x/Pokémon ポケモン.srm", "sram"), "saves copied (UTF-8 names)");
    check(file_is(new_root + "/states/snes9x/Pokémon ポケモン.state1", "state"), "states copied");
    check(file_is(new_root + "/piccolo", "the new binary"), "other files in the old root not copied");
    check(host_fs_exists(paths.system.c_str()), "system folder created");
    check(host_fs_exists(old_root.c_str()), "old folder left in place");

    SettingsFile last_used = {};
    check(settings_file_load(&last_used, (paths.settings + "/last_used.cfg").c_str()), "last used file read");
    const char* core = settings_file_get(&last_used, "", "core");
    const char* folder = settings_file_get(&last_used, "", "folder");
    const char* other = settings_file_get(&last_used, "", "other");
    const char* near = settings_file_get(&last_used, "", "near");
    check(core != nullptr && new_root + "/cores/snes9x_libretro.so" == core, "core path moved to the new folder");
    check(folder != nullptr && std::strcmp(folder, "/media/roms") == 0, "a path outside the old folder kept");
    check(other != nullptr && new_root + "/cores" == other, "a '~/.host-libretro' path moved too");
    check(near != nullptr && old_root + "-old/x" == near, "a similar-looking path is not touched");

    // Second run: the new folder exists, nothing is copied again.
    write_text(old_root + "/saves/snes9x/Later.srm", "later");
    write_text(old_root + "/saves/snes9x/Pokémon ポケモン.srm", "changed in the old folder");
    check(app_paths_setup(&paths, old_root.c_str(), "piccolo.cfg"), "second setup");
    check(!host_fs_exists((new_root + "/saves/snes9x/Later.srm").c_str()), "no second migration");
    check(file_is(new_root + "/saves/snes9x/Pokémon ポケモン.srm", "sram"), "new files never overwritten");

    // No old folder at all: just the folders.
    AppPaths fresh = {};
    fresh.root = base + "/fresh/host-libretro";
    app_paths_fill(&fresh);
    check(app_paths_setup(&fresh, (base + "/missing/.host-libretro").c_str(), nullptr), "setup without old folder");
    check(host_fs_exists(fresh.cores.c_str()) && host_fs_exists(fresh.settings.c_str()), "folders created");
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("usage: %s <scratch directory>\n", argv[0]);
        return 2;
    }

    test_xdg_rule();
    test_migration(argv[1]);

    if (g_failures == 0)
    {
        std::printf("app paths: all checks passed\n");
        return 0;
    }
    std::printf("app paths: %d check(s) failed\n", g_failures);
    return 1;
}
