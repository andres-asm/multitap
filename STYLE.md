# Multitap code style

Modern C++ written like C. The goal: any human can read, debug and fix any
file without knowing clever C++. Explicit and boring beats short and smart.
Lines of code don't matter, clarity does.

This applies to our code only. Third-party code in `deps/` is never edited
and is built without our warning flags.

## Language

- C++20. We use it mainly for designated initializers (`.sType = ...`), which
  make Vulkan/SDL struct setup read like C.
- The shared protocol header (later, `protocol/`) is plain C.

## Allowed

- `struct` with plain data members and default member initializers.
- Free functions named `module_verb()` / `module_verb_noun()`:
  `vulkan_swapchain_create()`, `renderer_begin_frame()`.
- `enum class` for every enumeration (strongly typed, no implicit int).
- `constexpr` for constants. `static` for file-local functions and data.
- Fixed-width integer types (`uint32_t`, `int32_t`, ...) for sizes, counts, IDs.
- `nullptr`, `static_cast`, `reinterpret_cast` (only for function pointers from loaders).
- References only where a library API forces them (e.g. `ImGuiIO& io = ImGui::GetIO()`).
- `new[]`/`delete[]` for short-lived scratch arrays in one function, freed in the same function.
  Prefer fixed-size arrays with a named `MAX_` constant when there is a sane limit.
- `std::vector`, `std::string` and `std::string_view` outside hot paths (see
  below), in `.cpp` files and headers. Walk them with an index: `for (size_t i = 0; i < v.size(); i++)`.
  Pass them like any struct: pointers for out-parameters, `const` pointers for input.
- `<algorithm>` (`std::sort`, `std::find`, ...) outside hot paths, with a named
  `static` function as comparator.
- *Using* the library's member functions and operators (`v.size()`,
  `v.push_back(x)`, `a == b` on strings). What's not allowed is writing our own.
- `<filesystem>` only inside `hosts/common/host_fs.cpp`; everything else calls
  `host_fs_*` (plain `bool` results, errors logged). Exceptions stay off: inside
  `host_fs` only the overloads that take a `std::error_code` are used (they
  report the error there and never throw; the others would abort the program
  under `-fno-exceptions`). Paths are built from our UTF-8 strings through
  `char8_t` (C++20): a `path` from a plain `std::string` uses the system's
  narrow encoding, which is not UTF-8 on Windows.
- Structs whose members are STL types get a destructor and copying from the
  compiler: that's fine ("rule of zero"). We never write them ourselves.

## Not allowed

- `auto` (anywhere, including loops).
- Lambdas, templates we write ourselves, template metaprogramming.
- Classes with behavior: we don't write member functions, constructors/destructors,
  inheritance, `virtual`.
- Writing operator overloads.
- Exceptions (`-fno-exceptions`) and RTTI (`-fno-rtti`).
- Macros for code generation or control flow. `#define` only for include guards
  (we use `#pragma once`) and build-time feature switches (`MULTITAP_VULKAN_VALIDATION`).
- C-style casts. Implicit narrowing or sign conversion (`-Wconversion -Wsign-conversion`).
- Hidden global state. If a library forces it (libretro callbacks carry no
  userdata), keep it in one place, document why, and assert it is set.
- STL in the host/frontend protocol (plain C, crosses processes).
- Other STL containers (`std::unordered_map`, `std::map`, ...), smart pointers
  and iterators (beyond `erase()`): only where a spec says so. A vector with a linear or sorted
  search covers almost everything.

## Hot paths

The per-frame loop, the audio callback and input reading never allocate
memory: no `new`, no growing a vector or string. A vector reserved up front
(`reserve` when the object is created) may be used there, but not grown.

## Functions and errors

- Functions that can fail return `bool` (or an `enum class` result) and log
  the reason at the point of failure. No error codes passed around silently.
- Out-parameters are pointers: `bool window_size_in_pixels(SDL_Window* w, uint32_t* width, uint32_t* height)`.
- Objects are created by `x_create()` and destroyed by `x_destroy()`.
  `x_destroy()` must be safe on a partially created object (check each handle).
- One exit path for cleanup at the top level (`app_shutdown()`), not scattered `goto`s.

## Formatting

- 4 spaces, braces on their own line, always use braces (even one-line `if`).
- Names: `snake_case` for functions/variables/members, `PascalCase` for types,
  `UPPER_SNAKE_CASE` for constants.
- Comment the *why*, not the *what*. Vulkan sync decisions are always commented.

## Workflow

- One feature per change. A change touches one module unless its spec says otherwise.
- Every feature: short spec (what, why, acceptance check) -> one change -> review -> merge.
- Debug builds keep Vulkan validation on. Run `-DMULTITAP_SANITIZE=ON` (ASan + UBSan) before merging.
- `-DMULTITAP_WARNINGS_AS_ERRORS=ON` must build cleanly.
