# Repository Guidelines

## Project Structure & Module Organization
Core source lives under `src/`. Shared platform code is split into libraries such as `src/ATCore`, `src/ATCPU`, `src/ATEmulation`, and `src/system`; the Linux frontend is in `src/AltirraLinux`. Tests are built from `src/ATTest`, with individual suites in `src/ATTest/source/Test*.cpp`. Runtime assets are under `assets/`, sample and reference data under `data/` and `testdata/`, packaging files under `dist/`, and helper scripts under `scripts/`.

## Build, Test, and Development Commands
Use CMake with Ninja for normal development:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

The main executable is `build/src/AltirraLinux/altirra`. Run the test binary with:

```bash
build/src/ATTest/attest all
build/src/ATTest/attest CoProc_6502 Emu_PokeyTimers System_Vector
```

Use an ASan build when chasing memory errors:

```bash
cmake -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
cmake --build build-asan
```

## Coding Style & Naming Conventions
This project uses C++23. Follow [`src/.editorconfig`](/home/pkilar/Devel/Altirra-Linux/src/.editorconfig): tabs for `*.cpp`, `*.h`, `*.inl`, and `*.fx*`, with a visual width target of 80 columns. Match existing naming patterns: `PascalCase` for types, `camelCase` for functions/locals, and `Test<Area>_<Case>.cpp` for test files. Keep module boundaries aligned with the current directory split instead of introducing catch-all utility files.

## Testing Guidelines
Add or update coverage in `src/ATTest/source` for behavior changes. Prefer targeted test names that mirror the subsystem, such as `TestSystem_Vector.cpp` or `TestEmu_PokeyTimers.cpp`. Before opening a PR, run `build/src/ATTest/attest all`; for focused work, run only the impacted suites first, then the full set before submission.

## Commit & Pull Request Guidelines
Recent commits use short, imperative, scope-first subjects, for example: `Audio scope: show full post-mix output including Covox`. Follow that pattern and keep the first line specific. PRs should describe the user-visible change, call out risky areas, link the issue when applicable, and include screenshots for UI work in `src/AltirraLinux`. Confirm build and test status in the PR body.

## Packaging & Release Notes
Distribution metadata lives in `dist/arch`, `dist/rpm`, and `dist/debian`. If a change affects dependencies, install paths, desktop integration, or bundled assets, update the relevant packaging files in the same change.
