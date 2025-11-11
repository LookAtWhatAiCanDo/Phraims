# Repository Guidelines

## Project Structure & Module Organization
The codebase follows a modular structure with classes separated into dedicated files:

- **main.cpp** - Application entry point with QApplication initialization, single-instance guard, and window restoration logic
- **SplitWindow.h/.cpp** - Main window class managing splitter layouts, menus, persistence, and multi-window coordination
- **SplitFrameWidget.h/.cpp** - Individual frame widget for each split section with navigation controls and WebEngine view
- **MyWebEngineView.h** (header-only) - Custom QWebEngineView subclass providing context menus and window creation behavior
- **DomPatch.h/.cpp** - DOM patch structures, JSON persistence helpers, and patch management dialog
- **EscapeFilter.h** (header-only) - Event filter for handling Escape key during fullscreen mode
- **SplitterDoubleClickFilter.h** (header-only) - Event filter for handling double-clicks on splitter handles to resize panes equally
- **Utils.h/.cpp** - Shared utilities including GroupScope RAII helper, window menu icons, global window tracking, and legacy migration logic

For simple classes like `EscapeFilter`, `MyWebEngineView`, and `SplitterDoubleClickFilter`, implementations are kept in the header as inline methods to reduce file count and keep code/comments together.

`CMakeLists.txt` configures the `Phraims` executable target and links Qt Widgets and WebEngine modules.
Generated binaries and intermediates belong in `build/`; feel free to create
parallel out-of-source build directories (`build-debug`, `build-release`) to keep artifacts separated.
User preferences persist through `QSettings` under the `swooby/Phraims` domain,
so evolve keys carefully to avoid breaking stored layouts or address lists.

## Build, Test, and Development Commands
```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt        # configure; point to Qt6 if not on PATH
cmake --build build --config Release                       # compile the Phraims executable
./build/Phraims                                             # launch the multi-chat splitter UI
cmake --build build --target clean                         # remove compiled objects when needed
```
Use the same `build` tree for iterative work; regenerate only when toggling build options or Qt installs.

## Continuous Integration

The repository uses GitHub Actions to automatically build macOS binaries on every push to `main` and pull requests. The workflow is defined in `.github/workflows/build-macos.yml` and:
- Installs Qt6 with WebEngine module using a caching action for faster builds
- Configures and builds the Phraims app bundle using CMake
- Uploads the resulting `Phraims.app` as a downloadable artifact (retained for 90 days)
- Can be manually triggered via workflow_dispatch for release builds

When modifying build requirements or dependencies, ensure the workflow file stays synchronized with local build instructions. Test the workflow by creating a pull request or triggering it manually from the GitHub Actions UI.

## Coding Style & Naming Conventions
Follow the existing C++17 + Qt style: two-space indentation, opening braces on the same line, and `PascalCase` for classes (`SplitFrameWidget`). Member variables carry a trailing underscore (`backBtn_`), free/static helpers use `camelCase`, and enums stay scoped within their owning classes. Prefer Qt containers and utilities over STL when interacting with Qt APIs, and keep comments focused on non-obvious behavior (signals, persistence, or ownership nuances).

## Keyboard Shortcuts & Navigation
The application implements standard keyboard shortcuts for common operations:

### Frame Management Shortcuts
- **Command-T** (macOS) / **Ctrl+T** (other platforms): Add new frame after the currently focused frame
- **Command-R** (macOS) / **Ctrl+R** (other platforms): Reload the focused frame via the View ▸ Reload Frame action
- **Command-Shift-R** (macOS) / **Ctrl+Shift+R** (other platforms): Reload the focused frame while bypassing cache via View ▸ Reload Frame (Bypass Cache)
- **Command-N** (macOS) / **Ctrl+N** (other platforms): Open a new window
- **F12**: Toggle DevTools for the focused frame
- **Command-W** (macOS) / **Ctrl+W** (other platforms): Close window
- **Command-M** (macOS) / **Ctrl+M** (other platforms): Minimize window

### Shortcut Implementation Guidelines
When adding new keyboard shortcuts:
- Use `QKeySequence` standard key sequences when available (e.g., `QKeySequence::AddTab` for Command-T)
- Register shortcuts in the appropriate menu (File, View, Layout, Tools, Window) for discoverability
- Provide visual feedback when shortcuts are triggered (e.g., brief animation or status message)
- Ensure shortcuts don't interfere with text input fields by using the main window context
- Document new shortcuts in both `README.md` and this file

## Frame Scale Controls
- Each frame header exposes `A-`, `A+`, and `1x` buttons that zoom only the embedded `QWebEngineView`. The header chrome intentionally stays a constant size so controls remain predictable; under the hood the buttons call `SplitFrameWidget::setScaleFactor`, which forwards the value to `QWebEngineView::setZoomFactor`.
- Matching View menu actions (`Increase/Decrease/Reset Frame Scale`) operate on the currently focused frame for accessibility and keyboard-driven workflows. Add future shortcuts to those actions, not to individual widgets.
- Scale factors are persisted per frame alongside addresses under the `frameScales` key in `QSettings`. Whenever you add, remove, or reorder frames, update the paired scale vector so indices remain aligned. Migrating persistence logic must keep both lists backward compatible.
- If you need traditional web zoom outside of this mechanism, avoid duplicating state—route everything through `setScaleFactor` so persistence and UI stay consistent.

## Web View Context Menu
- Navigation actions (Back, Forward, Reload) and editing commands (Cut, Copy, Paste, Select All) mirror Qt's built-in `QWebEnginePage` actions.
- **Copy Link Address** appears when right-clicking a hyperlink and copies the fully encoded target URL to the clipboard for easy sharing.
- **Translate…** opens Google Translate either with the selected text or the full page URL and spawns a new window.
- **Inspect…** forwards the request to DevTools, letting the parent window decide how to open the inspector.

## Documentation & Code Comments
All code should be thoroughly documented using Doxygen-style comments:

### Header Files (.h)
- Use `/** */` multi-line comment blocks for all classes, methods, and functions
- Include `@brief` to describe purpose
- Add `@param` for each parameter with description
- Add `@return` for methods that return values
- Use `///<` inline comments for member variables
- Document signals with what triggers them and what data they provide

### Implementation Files (.cpp)
- Add comments for complex algorithms or non-obvious logic
- Explain "why" rather than "what" when the code is self-explanatory
- Document ownership semantics, lifecycle assumptions, and thread-safety considerations

### Comment Maintenance
- **Always update comments when changing code** - comments must stay synchronized with implementation
- When refactoring, review and update all related documentation
- If removing or changing a method signature, update its Doxygen documentation
- Keep examples in comments current with actual API usage

Example Doxygen format:
```cpp
/**
 * @brief Brief description of what this does.
 * @param name Description of the parameter
 * @return Description of return value
 * 
 * Longer detailed explanation if needed.
 */
ReturnType methodName(ParamType name);
```

This documentation style enables IDE tooltips, generates API documentation with Doxygen, and helps maintainers understand code intent.

## Testing Guidelines
Automated tests are not yet wired in; rely on the acceptance scenarios listed in `README.md` until a `tests/` suite is added. Document new manual test cases alongside features, and script them via Qt Test or GTest once coverage becomes practical. When adding tests, place sources under `tests/`, update `CMakeLists.txt` to call `enable_testing()` and `add_test`, and execute with `ctest --test-dir build`. Always verify splitter persistence by resizing panes, quitting, and relaunching.

## Commit & Pull Request Guidelines
Use short, imperative commit subjects (e.g., `Add vertical grid layout preset`) with optional extended bodies for rationale or follow-up TODOs. Reference issues with `Fixes #123` when applicable. Pull requests should summarize UI or behavior changes, list manual test evidence (steps from the acceptance checklist), and provide before/after screenshots when the UI shifts. Confirm that the app builds locally and that existing settings migrate cleanly before requesting review.

## Agent Responsibilities
- Keep `AGENTS.md` in sync with the current behavior and expectations of the codebase whenever functionality or workflows change.
- Keep `README.md` up to date with the implemented features, build/run steps, and any relevant operational notes introduced by code changes.
- **Maintain code documentation**: Always update Doxygen-style comments when modifying code. Ensure `@param`, `@return`, and `@brief` tags remain accurate.
- Review and update documentation in both header and implementation files when refactoring or changing behavior.
- Generate a commit message and accompanying description for every set of changes before handing work back to the user.
