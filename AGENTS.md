# Repository Guidelines

## Project Structure & Module Organization
The codebase follows a modular structure with classes separated into dedicated header/source files:

- **main.cpp** - Application entry point with QApplication initialization, single-instance guard, and window restoration logic
- **SplitWindow.h/.cpp** - Main window class managing splitter layouts, menus, persistence, and multi-window coordination
- **SplitFrameWidget.h/.cpp** - Individual frame widget for each split section with navigation controls and WebEngine view
- **MyWebEngineView.h/.cpp** - Custom QWebEngineView subclass providing context menus and window creation behavior
- **DomPatch.h/.cpp** - DOM patch structures, JSON persistence helpers, and patch management dialog
- **EscapeFilter.h/.cpp** - Event filter for handling Escape key during fullscreen mode
- **Utils.h/.cpp** - Shared utilities including GroupScope RAII helper, window menu icons, global window tracking, and legacy migration logic

`CMakeLists.txt` configures the `Phraim` executable target and links Qt Widgets and WebEngine modules. Generated binaries and intermediates belong in `build/`; feel free to create parallel out-of-source build directories (`build-debug`, `build-release`) to keep artifacts separated. User preferences persist through `QSettings` under the `swooby/Phraim` domain, so evolve keys carefully to avoid breaking stored layouts or address lists.

## Build, Test, and Development Commands
```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt        # configure; point to Qt6 if not on PATH
cmake --build build --config Release                       # compile the Phraim executable
./build/Phraim                                             # launch the multi-chat splitter UI
cmake --build build --target clean                         # remove compiled objects when needed
```
Use the same `build` tree for iterative work; regenerate only when toggling build options or Qt installs.

## Coding Style & Naming Conventions
Follow the existing C++17 + Qt style: two-space indentation, opening braces on the same line, and `PascalCase` for classes (`SplitFrameWidget`). Member variables carry a trailing underscore (`backBtn_`), free/static helpers use `camelCase`, and enums stay scoped within their owning classes. Prefer Qt containers and utilities over STL when interacting with Qt APIs, and keep comments focused on non-obvious behavior (signals, persistence, or ownership nuances).

## Testing Guidelines
Automated tests are not yet wired in; rely on the acceptance scenarios listed in `README.md` until a `tests/` suite is added. Document new manual test cases alongside features, and script them via Qt Test or GTest once coverage becomes practical. When adding tests, place sources under `tests/`, update `CMakeLists.txt` to call `enable_testing()` and `add_test`, and execute with `ctest --test-dir build`. Always verify splitter persistence by resizing panes, quitting, and relaunching.

## Commit & Pull Request Guidelines
Use short, imperative commit subjects (e.g., `Add vertical grid layout preset`) with optional extended bodies for rationale or follow-up TODOs. Reference issues with `Fixes #123` when applicable. Pull requests should summarize UI or behavior changes, list manual test evidence (steps from the acceptance checklist), and provide before/after screenshots when the UI shifts. Confirm that the app builds locally and that existing settings migrate cleanly before requesting review.

## Agent Responsibilities
- Keep `AGENTS.md` in sync with the current behavior and expectations of the codebase whenever functionality or workflows change.
- Keep `README.md` up to date with the implemented features, build/run steps, and any relevant operational notes introduced by code changes.
- Generate a commit message and accompanying description for every set of changes before handing work back to the user.
