# Agent Guide (Bando)

This repository currently contains no tracked source files, build scripts, or
documentation beyond `.gitignore`. The guidance below is based on the stated
project intent: C++ + SDL3 + Vulkan (future) on Linux/macOS with Bazel.
Update this file once real build/config files exist (e.g. `WORKSPACE`, `MODULE.bazel`,
`BUILD.bazel`, `CMakeLists.txt`, or tooling configs).

If you add Cursor rules (`.cursor/rules/*`, `.cursorrules`) or Copilot rules
(`.github/copilot-instructions.md`), mirror them here.

## Build, Lint, Test (Bazel-first)

### Bootstrap
- Install Bazel or Bazelisk; prefer Bazelisk to match repo Bazel version.
- Ensure SDL3 dev packages are installed on Linux; on macOS prefer Homebrew.
- Prefer Vulkan SDK via LunarG or system packages when Vulkan is introduced.

### Build
- Build everything:
  - `bazel build //...`
- Build a single target:
  - `bazel build //path/to:target_name`
- Build with custom config (example):
  - `bazel build --config=release //...`

### Run
- Run a single binary target:
  - `bazel run //path/to:target_name`
- Pass runtime args:
  - `bazel run //path/to:target_name -- --arg=value`

### Test
- Run all tests:
  - `bazel test //...`
- Run a single test target:
  - `bazel test //path/to:target_name`
- Run a single test case (gtest):
  - `bazel test //path/to:target_name --test_filter=SuiteName.TestName`
- Show test output:
  - `bazel test //path/to:target_name --test_output=all`

### Lint / Format
- If using clang-format:
  - `clang-format -i path/to/file.cc`
  - `bazel run //tools:clang_format` (if a wrapper target exists)
- If using clang-tidy:
  - `bazel run //tools:clang_tidy` (preferred once defined)
  - `clang-tidy path/to/file.cc -- -I<include_paths>` (fallback)
- If using buildifier for Bazel files:
  - `buildifier -r .`

### Diagnostics
- Bazel query targets:
  - `bazel query //...`
- Show toolchain debug info:
  - `bazel info` and `bazel version`

## Repository Layout (expected)

- `WORKSPACE` or `MODULE.bazel` for Bazel setup.
- `BUILD.bazel` files per module.
- `src/` for engine/game code.
- `include/` for public headers.
- `tests/` for unit/integration tests.
- `assets/` for runtime assets (keep large/binary in git-lfs if needed).

## Code Style Guidelines

### Language and Tooling
- C++20 or newer (align with toolchain once configured).
- SDL3 for window/input/audio; Vulkan for rendering when enabled.
- Prefer Bazel toolchains for compiler selection; avoid ad-hoc flags.

### Formatting
- Use clang-format with a repo `.clang-format` once present.
- Default formatting if not specified:
  - 2 or 4 spaces (pick one once defined; do not mix).
  - Max line length 100–120.
  - Braces on same line (Allman only if mandated).
- Always format before committing or handing off changes.

### Includes and Imports
- Order includes:
  1) Related header for the translation unit.
  2) C/C++ standard library headers.
  3) Third-party headers (SDL3/Vulkan/others).
  4) Project headers.
- Use forward declarations in headers where possible to reduce compile time.
- Prefer `<...>` for system/third-party, `"..."` for project headers.

### Naming Conventions
- Types: `PascalCase` for classes/structs/enums.
- Functions: `camelCase` for methods; `snake_case` if existing style says so.
- Variables: `camelCase` or `snake_case` consistently; avoid Hungarian notation.
- Constants: `kConstantName` or `SCREAMING_SNAKE_CASE` consistently.
- Files: `snake_case.cc/.h` or `PascalCase.cc/.h` consistently.

### Types and Ownership
- Prefer `std::unique_ptr` for exclusive ownership; `std::shared_ptr` only when
  shared ownership is explicit and necessary.
- Avoid raw `new`/`delete`; if unavoidable, encapsulate and document rationale.
- Use `std::span`, `std::string_view`, and `const` references for non-owning views.

### Error Handling
- Use explicit error returns (`tl::expected`, `std::expected`, or status enums)
  for recoverable failures.
- Assertions (`assert`, `SDL_assert`) for programmer errors only.
- Log actionable context on failures (file/line, resource IDs, subsystem).

### Logging
- Prefer a single logging abstraction (spdlog or custom) once added.
- Avoid `std::cout` for non-debug output; use logger levels.

### SDL3 + Vulkan Practices
- Initialize SDL3 subsystems explicitly; tear down in reverse order.
- Always check SDL return codes and `SDL_GetError()`.
- For Vulkan, validate return codes and enable validation layers in debug.

### Threading and Concurrency
- Use `std::jthread` if cancellation is needed; otherwise `std::thread`.
- Protect shared state with `std::mutex`/`std::scoped_lock` or atomics.
- Avoid blocking the render loop; use job systems if introduced.

### Performance
- Favor stack allocation for small objects; pool larger or frequent allocations.
- Minimize per-frame allocations; reuse buffers and vectors.
- Keep hot-path functions small and inlined when helpful.

### Tests
- Prefer deterministic tests; avoid time-based flakiness.
- Use test fixtures for SDL or rendering setup; keep graphics tests isolated.
- Name tests with clear component + behavior.

## File Editing Guidance for Agents

- Do not introduce new files without a clear need.
- Avoid editing `target/` or other build output directories.
- Keep changes scoped; avoid drive-by reformatting unless required.

## Missing Project Rules

No Cursor or Copilot rule files were found at:
- `.cursor/rules/*`
- `.cursorrules`
- `.github/copilot-instructions.md`

If any of these files are added later, copy their requirements here and
prioritize them over the defaults above.
