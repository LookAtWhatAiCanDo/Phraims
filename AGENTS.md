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
- **version.h.in** - Template for CMake-generated version.h containing version constants and project URL

For simple classes like `EscapeFilter`, `MyWebEngineView`, and `SplitterDoubleClickFilter`, implementations are kept in the header as inline methods to reduce file count and keep code/comments together.

`CMakeLists.txt` configures the `Phraims` executable target and links Qt Widgets and WebEngine modules.
Generated binaries and intermediates belong in `build/`; feel free to create
parallel out-of-source build directories (`build-debug`, `build-release`) to keep artifacts separated.

### Versioning
The application version is defined as a single source of truth in `CMakeLists.txt` via the `project()` VERSION parameter (currently 0.55). CMake generates `build/version.h` from `version.h.in` during configuration, exposing:
- `PHRAIMS_VERSION` - full version string (e.g., "0.55")
- `PHRAIMS_VERSION_MAJOR`, `PHRAIMS_VERSION_MINOR`, `PHRAIMS_VERSION_PATCH` - individual version components
- `PHRAIMS_HOMEPAGE_URL` - GitHub repository URL

The version appears in:
- **About Dialog**: `Help -> About Phraims` menu shows version and clickable GitHub link
- **Startup Logs**: Version logged at application launch in main.cpp
- **Build Artifacts**: macOS Info.plist includes `CFBundleShortVersionString` and `CFBundleVersion` populated from `${PROJECT_VERSION}`

To increment the version:
1. Update the VERSION in `project(Phraims VERSION x.y.z)` in CMakeLists.txt
2. Reconfigure CMake (it will regenerate version.h automatically)
3. Update the version at the top of README.md to match
4. Rebuild and test

User preferences persist exclusively via the `AppSettings` wrapper (INI file `settings.ini` under `QStandardPaths::AppDataLocation`).
A [currently] no-op `performLegacyMigration()` hook is reserved for future schema evolution.
Evolve keys carefully to avoid breaking layouts or address lists within the current format.

Persistence Rules (MANDATORY)
1. Never import or instantiate `QSettings` directly. Always use `AppSettings` (e.g. `AppSettings s;`).
2. When adding a new key, document its purpose and lifecycle here (and in README) immediately.
3. Prefer simple, stable, lowercase key names; group logically with `beginGroup`/`endGroup` via the wrapper.
4. If a key must be renamed or removed, add a brief transitional note before deleting old usage.
5. Treat settings as part of public app surface; avoid gratuitous churn.

## Build, Test, and Development Commands
```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt # configure; point to Homebrew Qt6 (e.g. $(brew --prefix qt6), currently 6.9.3) if not on PATH
cmake -B build --config Release                     # compile the Phraims executable
./build/Phraims                                     # launch the multi-chat splitter UI
cmake -B build --target clean                       # remove compiled objects when needed
                                                    # QtWebEngine proprietary codec prefixes now live in the private repo
                                                    # LookAtWhatAiCanDo/QtWebEngineProprietaryCodecs; fetch the appropriate
                                                    # .qt/<ver>-prop-<platform>-<arch> prefix from there when packaging.
./ci/build-phraims-macos.sh                         # macOS-only: update Homebrew, install deps, require custom QtWebEngine from 
                                                    # .qt/<ver>-prop-macos-<arch> (copy from the QtWebEngineProprietaryCodecs repo
                                                    # or set QT_WEBENGINE_PROP_PREFIX), build Release in build/macos-<arch>,
                                                    # macdeployqt with staged libpaths, sync WebEngine payload, normalize install_name/rpaths, 
                                                    # validate bundle linkage, sign, and emit Phraims.dmg.
                                                    # Set FORCE_BREW_UPDATE=0 to skip brew update; DEBUG=1 for verbose deploy diagnostics.
                                                    # For diagnostics: DEBUG=1 ./ci/build-phraims-macos.sh
                                                    # (macdeployqt log, staging/Frameworks listings, rpaths).
                                                    # Override BUILD_ARCH/MACOS_ARCH to build x86_64 output (build dir becomes build/macos-<arch>).
```
Use the same `build` tree for iterative work; regenerate only when toggling build options or Qt installs.

## Continuous Integration

The repository uses GitHub Actions to automatically build macOS and Windows binaries on every push to `main` and pull requests
(skips when the diff only touches `README.md`). Both platforms download the matching QtWebEngine artifact from the private
`LookAtWhatAiCanDo/QtWebEngineProprietaryCodecs` repo using `PRIVATE_QTWEBENGINE_TOKEN` before building.

If the QtWebEngine artifact download fails (e.g., token expired), refresh @paulpv’s PAT named `LAWACD QtWebEngineProprietaryCodecs`
(current 366-day PAT created 2025/11/26, expires 2026/11/27) and paste the new token into the Phraims repository secret `PRIVATE_QTWEBENGINE_TOKEN`,
then rerun the workflows.

QtWebEngine build workflows and scripts now live exclusively in the private `LookAtWhatAiCanDo/QtWebEngineProprietaryCodecs`
repository. Trigger its macOS or Windows workflows there when bumping QtWebEngine to refresh the cached artifacts that this
repository consumes.

The unified build workflow (`.github/workflows/build-phraims.yml`) consolidates macOS and Windows builds into separate jobs within a single workflow file.
All platform builds follow the same pattern: checkout → artifact download → build → upload, with common env naming and only platform-specific differences (runner label, shell, script path, host Qt handling inside the scripts).
Test workflows by creating a pull request or triggering them manually from the GitHub Actions UI.

### macOS
macOS builds run as a job with architecture matrix (workflow_dispatch input `arch` can select arm64, x86_64, or both);
each arch uses its runner, downloads the matching `qtwebengine-macos-<ver>-<arch>` artifact via `ci/get-qtwebengine-macos.sh`,
and produces `Phraims-macOS-<arch>` DMGs from `build/macos-<arch>/`.

### Windows
Windows builds run as a job with architecture matrix (workflow_dispatch input `arch` can select x64, arm64, or both).
Each arch uses its runner and first downloads the matching `qtwebengine-windows-<ver>-<arch>` artifact via `ci/get-qtwebengine-windows.ps1`.
The job ensures a host Qt kit is available (via `qmake` on PATH or by installing one with `aqtinstall`) and builds Phraims with Ninja after seeding
the VS2022 environment via `vsdevcmd`.

Packaging-time replacement: instead of modifying the host Qt kit, the workflow runs `windeployqt` to create a `deploy` folder and then copies
only the runtime artifacts from the downloaded proprietary QtWebEngine prefix into that deploy folder (overwriting matching files). This
ensures the packaged application ships the proprietary WebEngine payload without altering system or cached Qt installs.

## Coding Style & Naming Conventions
Follow the existing C++17 + Qt style: two-space indentation, opening braces on the same line, and `PascalCase` for classes (`SplitFrameWidget`). Member variables carry a trailing underscore (`backBtn_`), free/static helpers use `camelCase`, and enums stay scoped within their owning classes. Prefer Qt containers and utilities over STL when interacting with Qt APIs, and keep comments focused on non-obvious behavior (signals, persistence, or ownership nuances).
- Use `auto` for local variable declarations whenever the type is clear from the initializer (iterators, Qt helpers, factory functions) to reduce verbosity; avoid it only when it would obscure meaning or hide important conversions.

## Keyboard Shortcuts & Navigation
The application implements standard keyboard shortcuts for common operations:

### Frame Management Shortcuts
- **Command-T** (macOS) / **Ctrl+T** (other platforms): Add new frame after the currently focused frame
- **Command-R** (macOS) / **Ctrl+R** (other platforms): Reload the focused frame via the View ▸ Reload Frame action
- **Command-Shift-R** (macOS) / **Ctrl+Shift+R** (other platforms): Reload the focused frame while bypassing cache via View ▸ Reload Frame (Bypass Cache)
- **Command-N** (macOS) / **Ctrl+N** (other platforms): Open a new window
- **Shift-Command-N** (macOS) / **Shift+Ctrl+N** (other platforms): Open a new Incognito window
- **F12**: Toggle DevTools for the focused frame
- **Command-W** (macOS) / **Ctrl+W** (other platforms): Close window
- **Command-M** (macOS) / **Ctrl+M** (other platforms): Minimize window

### Shortcut Implementation Guidelines
When adding new keyboard shortcuts:
- Use `QKeySequence` standard key sequences when available (e.g., `QKeySequence::AddTab` for Command-T)
- For custom shortcuts, use `Qt::CTRL` which Qt automatically maps to Command (⌘) on macOS and Ctrl on other platforms
- **NEVER** use platform-specific conditionals (`#ifdef Q_OS_MAC`) with `Qt::META` - Qt handles this mapping automatically
- Register shortcuts in the appropriate menu (File, View, Layout, Tools, Window) for discoverability
- Provide visual feedback when shortcuts are triggered (e.g., brief animation or status message)
- Ensure shortcuts don't interfere with text input fields by using the main window context
- Document new shortcuts in both `README.md` and this file

## Frame Scale Controls
- Each frame header exposes `A-`, `A+`, and `1x` buttons that zoom only the embedded `QWebEngineView`. The header chrome intentionally stays a constant size so controls remain predictable; under the hood the buttons call `SplitFrameWidget::setScaleFactor`, which forwards the value to `QWebEngineView::setZoomFactor`.
- Matching View menu actions (`Increase/Decrease/Reset Frame Scale`) operate on the currently focused frame for accessibility and keyboard-driven workflows. Add future shortcuts to those actions, not to individual widgets.
- Scale factors are persisted per frame alongside addresses under the `frameScales` key in `AppSettings`. Whenever you add, remove, or reorder frames, update the paired scale vector so indices remain aligned. Migrating persistence logic must keep both lists backward compatible.
- If you need traditional web zoom outside of this mechanism, avoid duplicating state—route everything through `setScaleFactor` so persistence and UI stay consistent.

## Profiles System
Phraims supports multiple browser profiles, allowing users to maintain separate browsing contexts with isolated cookies, cache, history, and other data.

### Architecture
- **Profile Storage**: Each profile has its own directory under `<AppDataLocation>/profiles/<profileName>/` containing persistent storage and cache.
- **Profile Management**: Functions in `Utils.h/.cpp` handle profile creation, deletion, renaming, and listing.
- **Profile Caching**: `QWebEngineProfile` instances are cached in `g_profileCache` (a `QMap<QString, QWebEngineProfile*>`) to avoid recreating profiles.
- **Current Profile**: The global current profile is stored in AppSettings under the `currentProfile` key (defaults to "Default").
- **Per-Window Profiles**: Each `SplitWindow` tracks its active profile in `currentProfileName_` and persists it in `windows/<id>/profileName`.
- **Incognito Profiles**: Incognito windows use off-the-record profiles created via `createIncognitoProfile()` that do not persist to disk.

### Key Functions (Utils.h/.cpp)
- `getProfileByName(name)` - Returns or creates a `QWebEngineProfile` for the given name
- `currentProfileName()` - Returns the globally active profile name (via AppSettings wrapper)
- `setCurrentProfileName(name)` - Sets and persists the global current profile (AppSettings)
- `listProfiles()` - Returns all profile directory names from the filesystem
- `createProfile(name)` - Creates a new profile directory
- `renameProfile(old, new)` - Renames a profile directory and updates references
- `deleteProfile(name)` - Deletes a profile directory and switches away if current
- `sharedWebEngineProfile()` - Convenience wrapper that calls `getProfileByName(currentProfileName())`

### Profiles Menu (SplitWindow)
The Profiles menu is located between Tools and Window menus and contains:
- **New Profile...** - Dialog to create a new profile with validation
- **Rename Profile...** - Dialog to select and rename a profile
- **Delete Profile...** - Dialog to select and delete a profile with confirmation
- **Open Profiles Folder** (debug builds only) - Opens the profiles directory in the system file browser (only shown when `NDEBUG` is not defined)
- **Profile List** (dynamic) - Shows all profiles with checkmark for the current one; clicking switches profiles

### Profile Switching
When switching profiles via `SplitWindow::switchToProfile()`:
1. Updates `currentProfileName_` and `profile_` members
2. Calls `setCurrentProfileName()` to update global current profile
3. Rebuilds all frames with `rebuildSections()` to use the new profile's QWebEngineProfile
4. Updates the Profiles menu in all windows to reflect the change
5. Updates window title to show the new profile name
6. Persists the window's profile choice via AppSettings (never import or call QSettings directly)

### Persistence
- Per-window profile: `windows/<id>/profileName` (stored via AppSettings)
- Global current profile: `currentProfile` (stored via AppSettings root scope)
- Profile data: `<AppDataLocation>/profiles/<profileName>/` on filesystem

### Validation Rules
- Profile names cannot contain slashes (/ or \)
- Cannot delete the last remaining profile
- Profile directories are created on first use via `getProfileByName()`
- Default profile is always "Default" and created automatically if needed

## Incognito Mode
Phraims supports Incognito (private) browsing windows that provide ephemeral sessions without persisting history, cookies, or other data.

### Architecture
- **Off-the-Record Profiles**: Incognito windows use `QWebEngineProfile` instances created with `isOffTheRecord()` set to true.
- **Ephemeral Storage**: All browsing data (cookies, cache, history, local storage) exists only in memory and is discarded when the window closes.
- **Window Flag**: Each `SplitWindow` tracks whether it is Incognito via the `isIncognito_` boolean member.
- **No Persistence**: Incognito windows skip all QSettings persistence in `savePersistentStateToSettings()` and `closeEvent()`.
- **Visual Indicator**: Window titles append " - Incognito" to distinguish private windows from normal ones.

### Key Functions (Utils.h/.cpp)
- `createIncognitoProfile()` - Creates a new off-the-record QWebEngineProfile for Incognito mode
- `createAndShowIncognitoWindow(address)` - Convenience wrapper to create an Incognito window
- `createAndShowWindow(address, id, isIncognito)` - Core window creation with optional Incognito flag

### SplitWindow Changes
- Constructor accepts `isIncognito` parameter (defaults to false)
- Incognito windows use `createIncognitoProfile()` instead of `getProfileByName()`
- Frame state loading skips QSettings restoration for Incognito windows
- Splitter size and window geometry restoration are skipped for Incognito windows
- `savePersistentStateToSettings()` returns early for Incognito windows
- `closeEvent()` skips all persistence logic for Incognito windows
- `updateWindowTitle()` displays "Incognito" as the profile name in the title
- `profilesMenu_` is not created for Incognito windows (remains null)

### Menu and Shortcuts
- File menu includes "New Incognito Window" action
- Keyboard shortcut: `Shift+Command+N` (macOS) / `Shift+Ctrl+N` (other platforms)
- Action triggers `createAndShowIncognitoWindow()` to open a new private window
- **Profiles menu is hidden** in Incognito windows to prevent switching to persistent profiles

### Design Decisions
- **DevTools Isolation**: DevTools for Incognito windows use the same ephemeral profile as the frames, ensuring complete isolation.
- **No Session Restoration**: Incognito windows never appear in the restored session on app restart.
- **Profile Name Display**: Incognito windows show "Incognito" as the profile name in the title for consistency with the UI pattern.
- **Independent Lifecycle**: Each Incognito window gets a unique off-the-record profile instance to ensure complete isolation even between multiple Incognito windows.
- **No Profile Management**: The Profiles menu is not available in Incognito windows since they use ephemeral profiles that cannot be managed or switched.

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
- Whenever correcting behavior, fixing inaccuracies, or adjusting semantics (e.g., settings keys, storage location), immediately update both `AGENTS.md` and `README.md` so documentation never drifts from reality.
- Always use `AppSettings` for persistence; direct `QSettings` construction is prohibited. If legacy direct usage is found, schedule a refactor in the next change set.
- Continuously enhance this document with improvements not limited to just code readability, maintainability, ownership semantics, error handling patterns, and performance techniques learned during development.
- **Maintain code documentation**: Always update Doxygen-style comments when modifying code. Ensure `@param`, `@return`, and `@brief` tags remain accurate.
- Review and update documentation in both header and implementation files when refactoring or changing behavior.
- Generate a commit message and accompanying description for every set of changes before handing work back to the user.

### Agent Conduct
- **Do not make careless changes:** Avoid inserting transient, audit-style comments in source (e.g., "removed X" or "no longer used"). Use commits and PR descriptions for change history so the code stays clean for readers who haven't seen prior iterations.
- **Learn and document:** When an agent or developer fixes a mistake or changes behaviour, immediately update `AGENTS.md` (or another appropriate doc) with a brief note describing the change and the rationale so the team learns and the same mistake or behavior is never repeated.
