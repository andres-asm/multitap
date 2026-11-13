# Multitap

Multi-emulator frontend (early work in progress).

Multitap aims to run games through interchangeable emulation backends
(libretro first, then others) behind one frontend. It is in early days so far

| Path                   | What                                                        |
|------------------------|-------------------------------------------------------------|
| `frontends/common/`    | Code shared by all frontends (Vulkan, swapchain, ImGui glue) |
| `frontends/piccolo/`   | Basic libretro frontend (SDL3 + Vulkan + ImGui)             |
| `hosts/common/`        | Code shared by all core hosts (library loading, logging)     |
| `hosts/host-libretro/` | Our libretro implementation + `libretro_test_frontend` (video, audio, joypad/analog/pointer/keyboard, core options, save RAM, savestates, timing modes) |
| `hosts/host-libretro/test-libretro/` | **Test libretro**, a core for testing frontends (submodule, [own repo](https://github.com/andres-asm/test-libretro)) |
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
./build/hosts/host-libretro/libretro_test_frontend [core] [content]
```

Options:

| Option                        | Default | Meaning                                  |
|-------------------------------|---------|------------------------------------------|
| `MULTITAP_SYSTEM_SDL3`        | OFF     | Use the system SDL3 instead of `deps/SDL` |
| `MULTITAP_WARNINGS_AS_ERRORS` | OFF     | `-Werror` / `/WX` for our code            |
| `MULTITAP_SANITIZE`           | OFF     | ASan + UBSan for our code (GCC/Clang)     |
| `MULTITAP_BUILD_TESTS`        | ON      | Build the test cores and CTest tests      |

Debug builds enable Vulkan validation when the layer is installed.

## Test libretro

[Test libretro](https://github.com/andres-asm/test-libretro) is a libretro
core for testing libretro **frontends**. Instead of emulating a system, it walks
through a series of pages (environment, video, input, audio, save RAM,
savestates, summary) and reports what the frontend did: answers that break the
libretro spec show up in red. Hold Start to pass a page, hold Select to fail it.
Its core options exercise `SET_GEOMETRY` and `SET_SYSTEM_AV_INFO` (size and
frame rate); see its README for the details.

It lives in its own repository, so any frontend can use it, and is included
here as a submodule at `hosts/host-libretro/test-libretro`. It builds with
Multitap, and CTest runs it headless through host-libretro: core options in all
three formats (v2, v1 and the old `SET_VARIABLES`), size and timing changes,
save RAM and savestates.

Normal (non-sanitizer) builds copy the programs to `~/.host-libretro/` and the
core to `~/.host-libretro/cores/`, where the test frontend lists it next to any
other cores you drop in. Save RAM and savestates go to
`~/.host-libretro/saves/` and `~/.host-libretro/states/`.

```sh
~/.host-libretro/libretro_test_frontend
```

## Why Multitap?

It's a surprise.

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
