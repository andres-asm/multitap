# Multitap

Multi-emulator frontend (work in progress).

| Path                   | What                                                        |
|------------------------|-------------------------------------------------------------|
| `frontends/piccolo/`   | Small test frontend for `host-libretro` (SDL3 + Vulkan + ImGui) |
| `hosts/host-libretro/` | libretro core host (not started)                            |
| `deps/`                | Third-party code as git submodules (never edited)           |
| `STYLE.md`             | Code style: modern C++ written like C                       |
| `CLAUDE.md`            | Working brief for Claude Code sessions                      |
| `docs/design-notes.md` | Full design record: decisions, recommendations, open items  |

## Dependencies

| Submodule    | Pinned at        |
|--------------|------------------|
| `deps/imgui` | `v1.93.0`        |
| `deps/SDL`   | `release-3.4.20` |

Also needed on the system: a C++20 compiler, CMake >= 3.25, Ninja (recommended)
and the Vulkan SDK / loader + headers (Vulkan 1.3). For validation in Debug
builds, install the Khronos validation layers (`vulkan-validation-layers` or the LunarG SDK).

Clone with submodules:

```sh
git clone --recursive <repo-url>
# or, in an existing clone:
git submodule update --init --recursive
```

## Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/frontends/piccolo/piccolo
```

Options:

| Option                        | Default | Meaning                                  |
|-------------------------------|---------|------------------------------------------|
| `MULTITAP_SYSTEM_SDL3`        | OFF     | Use the system SDL3 instead of `deps/SDL` |
| `MULTITAP_WARNINGS_AS_ERRORS` | OFF     | `-Werror` / `/WX` for our code            |
| `MULTITAP_SANITIZE`           | OFF     | ASan + UBSan for our code (GCC/Clang)     |

Debug builds enable Vulkan validation when the layer is installed.
