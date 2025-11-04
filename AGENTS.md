# Repository Guidelines

## Project Structure & Module Organization
The Qt GUI lives entirely in `main.cpp`, anchored by `SplitWindow` and nested `SplitFrameWidget` classes that manage splitter layouts, navigation chrome, and persistence. `CMakeLists.txt` configures one executable target, `QtSplitterHello`, and wires in the Qt Widgets and WebEngine modules. Generated binaries and intermediates belong in `build/`; feel free to create parallel out-of-source build directories (`build-debug`, `build-release`) to keep artifacts separated. User preferences persist through `QSettings` under the `NightVsKnight/LiveStreamMultiChat` domain, so evolve keys carefully to avoid breaking stored layouts or address lists.

## Build, Test, and Development Commands
```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt        # configure; point to Qt6 if not on PATH
cmake --build build --config Release                       # compile the QtSplitterHello executable
./build/QtSplitterHello                                    # launch the multi-chat splitter UI
cmake --build build --target clean                         # remove compiled objects when needed
```
Use the same `build` tree for iterative work; regenerate only when toggling build options or Qt installs.

## Coding Style & Naming Conventions
Follow the existing C++17 + Qt style: two-space indentation, opening braces on the same line, and `PascalCase` for classes (`SplitFrameWidget`). Member variables carry a trailing underscore (`backBtn_`), free/static helpers use `camelCase`, and enums stay scoped within their owning classes. Prefer Qt containers and utilities over STL when interacting with Qt APIs, and keep comments focused on non-obvious behavior (signals, persistence, or ownership nuances).

## Testing Guidelines
Automated tests are not yet wired in; rely on the acceptance scenarios listed in `README.md` until a `tests/` suite is added. Document new manual test cases alongside features, and script them via Qt Test or GTest once coverage becomes practical. When adding tests, place sources under `tests/`, update `CMakeLists.txt` to call `enable_testing()` and `add_test`, and execute with `ctest --test-dir build`. Always verify splitter persistence by resizing panes, quitting, and relaunching.

## Commit & Pull Request Guidelines
Use short, imperative commit subjects (e.g., `Add vertical grid layout preset`) with optional extended bodies for rationale or follow-up TODOs. Reference issues with `Fixes #123` when applicable. Pull requests should summarize UI or behavior changes, list manual test evidence (steps from the acceptance checklist), and provide before/after screenshots when the UI shifts. Confirm that the app builds locally and that existing settings migrate cleanly before requesting review.
