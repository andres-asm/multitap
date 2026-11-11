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

## Not allowed

- `auto` (anywhere, including loops).
- Lambdas, templates we write ourselves, template metaprogramming.
- Classes with behavior: no member functions, constructors/destructors, inheritance, `virtual`.
- Operator overloading.
- Exceptions (`-fno-exceptions`) and RTTI (`-fno-rtti`).
- Macros for code generation or control flow. `#define` only for include guards
  (we use `#pragma once`) and build-time feature switches (`MULTITAP_VULKAN_VALIDATION`).
- C-style casts. Implicit narrowing or sign conversion (`-Wconversion -Wsign-conversion`).
- Hidden global state. If a library forces it (libretro callbacks carry no
  userdata), keep it in one place, document why, and assert it is set.
- STL containers and `<algorithm>` in hot paths or public interfaces.
  (Allowed later only where a spec says so.)

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
