# Multitap

Multi-emulator frontend (early work in progress).

Multitap aims to run games through interchangeable emulation backends
(libretro first, then others) behind one frontend. It is in early days so far

| Path                   | What                                                        |
|------------------------|-------------------------------------------------------------|
| `frontends/common/`    | Code shared by all frontends (Vulkan, swapchain, ImGui glue) |
| `frontends/piccolo/`   | Basic libretro frontend (SDL3 + Vulkan + ImGui)             |
| `hosts/common/`        | Code shared by all core hosts (library loading, logging)     |
| `hosts/host-libretro/` | Our libretro implementation (early: loads cores, no running yet) |
| `deps/`                | Third-party code as git submodules (never edited)           |

## Dependencies

| Submodule    | Pinned at        |
|--------------|------------------|
| `deps/imgui` | `v1.93.0`        |
| `deps/SDL`   | `release-3.4.20` |
| `deps/libretro-common` | commit `52781c4` (only `libretro.h` is used) |

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
ctest --test-dir build --output-on-failure
./build/frontends/piccolo/piccolo
```

Options:

| Option                        | Default | Meaning                                  |
|-------------------------------|---------|------------------------------------------|
| `MULTITAP_SYSTEM_SDL3`        | OFF     | Use the system SDL3 instead of `deps/SDL` |
| `MULTITAP_WARNINGS_AS_ERRORS` | OFF     | `-Werror` / `/WX` for our code            |
| `MULTITAP_SANITIZE`           | OFF     | ASan + UBSan for our code (GCC/Clang)     |
| `MULTITAP_BUILD_TESTS`        | ON      | Build the test cores and CTest tests      |

Debug builds enable Vulkan validation when the layer is installed.

## How this is built

Multitap is developed with AI assistance. Most of the code is written by AI
models (currently Anthropic's Claude) working under my direction; I contributed
to RetroArch/libretro for years, so I know the inner workings of libretro and
Retroarch and I know what I like and what I don't like about it.

What that means in practice:

- **Design comes first.** Architecture and decisions are made and written down
  before code is generated. The AI implements a plan; it doesn't invent the project.
- **Small, scoped changes.** One feature at a time, each with a stated goal and
  a way to check it. No large unreviewed drops of code.
- **Readability is a hard requirement.** code style is deliberately
  restricted so any human can read, debug and fix every file.
- **Checked, not trusted.** The build treats warnings as errors, and debug
  builds run with the Vulkan validation layers. AddressSanitizer and
  UndefinedBehaviorSanitizer builds are part of the regular testing.

If you find code that is hard to follow, that's a bug. Please open an issue.

## License

MIT, see [`LICENSE`](LICENSE). Third-party code in `deps/` keeps its own license
(Dear ImGui: MIT, SDL: zlib, `libretro.h`: MIT).
