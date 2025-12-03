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

### Application Icons
The application uses platform-specific icon formats to ensure proper display in taskbars, window title bars, file explorers, and Start menus:

- **macOS**: Uses `resources/phraims.icns` (Apple Icon Image format)
  - Generated from `phraims.iconset/` PNG sources via the `icons.sh` script
  - Automatically embedded into the `.app` bundle via CMakeLists.txt configuration
  - CMake sets `MACOSX_BUNDLE_ICON_FILE` and copies the icon to the bundle's Resources folder

- **Windows**: Uses `resources/phraims.ico` (multi-resolution Windows icon)
  - Generated from `phraims.iconset/` PNG sources via the `generate_ico.py` Python script
  - Contains 6 resolutions: 16×16, 32×32, 48×48, 64×64, 128×128, 256×256 pixels
  - Embedded into the executable via `resources/phraims.rc.in` Windows resource script
  - CMake configures the `.rc.in` template with version information and compiles it as part of the Windows build
  - The resource script includes the icon (`IDI_ICON1`) and version metadata for proper Windows integration

- **Linux**: Currently uses default Qt application icon (platform-specific icon support planned)

**Regenerating Icons**

When updating the source icon (`phraims_icon_1024.png` or files in `phraims.iconset/`):

1. **Windows** (requires Python 3 with Pillow):
   ```bash
   pip3 install Pillow
   python3 generate_ico.py
   ```
   This regenerates `resources/phraims.ico` with all required resolutions. The next CMake build will automatically embed the updated icon.

2. **macOS** (requires macOS with `sips` and `iconutil`):
   ```bash
   ./icons.sh
   ```
   This regenerates `resources/phraims.icns` from `phraims_icon_1024.png`. The next CMake build will automatically copy the updated icon to the app bundle.

**Icon Build Integration**

- The icon resources are configured at CMake configure time and compiled/linked during the build phase
- No manual steps are needed for normal builds—the icons are automatically included
- The `.ico` and `.icns` files are tracked in git and only need regeneration when the source PNG files change
- The Windows `.rc` file is generated at configure time with version info from CMakeLists.txt

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

### Workflow Triggers
- **Push to `main`**: Builds all platforms (skips if only README.md or AGENTS.md changed)
- **Pull requests to `main`**: Builds all platforms from non-fork PRs
- **Tag push (v*)**: Builds all platforms and creates/updates GitHub Release with artifacts
- **Manual dispatch**: Allows selective platform/architecture builds via workflow_dispatch inputs

### Artifact Publishing
Workflow runs upload build artifacts to GitHub Actions for 90 days:
- **macOS**: `Phraims-macOS-{arch}` containing `Phraims.dmg` from `build/macos-{arch}/`
- **Windows**: `Phraims-Windows-{arch}` containing `Phraims-Installer-{arch}-Release.exe` from `build/windows-{arch}/`

For tagged builds (e.g., `v0.55`), the workflow also:
1. Creates a GitHub Release (if it doesn't exist) via the `create-release` job
2. Attaches platform-specific artifacts with versioned names:
   - **macOS**: `Phraims-{tag}-macOS-{arch}.dmg` (e.g., `Phraims-v0.55-macOS-arm64.dmg`)
   - **Windows**: `Phraims-{tag}-Windows-{arch}.exe` (e.g., `Phraims-v0.55-Windows-x64.exe`)

The `--clobber` flag ensures artifacts can be re-uploaded if a build is re-run for the same tag.

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

## Window Lifecycle and Media Cleanup
When a window is closed, all media playback must be explicitly stopped to prevent audio/video from continuing after the window is gone. This is critical because `QWebEngineView` and `QWebEnginePage` objects continue playing media until they are asynchronously deleted via `deleteLater()`, which can take several event loop iterations.

### Media Cleanup Implementation
- **SplitFrameWidget::stopMediaPlayback()**: Uses JavaScript to pause all `<audio>` and `<video>` elements in the page. This method is called on each frame when the window is closing.
- **SplitWindow::stopAllFramesMediaPlayback()**: Iterates through all `SplitFrameWidget` children and calls `stopMediaPlayback()` on each. This ensures all frames stop media immediately.
- **SplitWindow::closeEvent()**: Calls `stopAllFramesMediaPlayback()` as the first action before any state saving or cleanup. This guarantees media stops as soon as the user closes the window.

### Important Rules
1. **Always stop media before closing**: Any code path that destroys frames or windows must call `stopMediaPlayback()` or `stopAllFramesMediaPlayback()` before deletion.
2. **JavaScript-based cleanup**: Pausing media via JavaScript (`audio.pause()`, `video.pause()`) is the most reliable way to stop playback in `QWebEngineView`.
3. **Maintain this pattern**: Future changes to window closing, frame destruction, or layout rebuilding must preserve this media cleanup behavior to prevent resource leaks and user confusion.

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
- **Open Link in New Frame** appears when right-clicking a hyperlink and creates a new frame adjacent to the current one, loading the link there. Preserves the current window's profile/incognito state.
- **Translate…** opens Google Translate either with the selected text or the full page URL and spawns a new window.
- **Inspect…** forwards the request to DevTools, letting the parent window decide how to open the inspector.

## Frame Lifecycle Management
Managing frame creation, removal, and updates is critical to preserving user experience. The application uses a surgical approach for frame operations to avoid disrupting media playback and page state in other frames.

### Frame Removal Pattern (CRITICAL)
When removing a single frame, **NEVER** call `rebuildSections()` as it destroys and recreates all frames, causing:
- Media playback to stop in all frames
- Stateful pages to reset/reload
- Loss of user interaction state (scroll position, form data, etc.)

Instead, use the **surgical removal pattern** via `removeSingleFrame()`:
1. Validates the frame's `logicalIndex` property
2. Removes the frame from the `frames_` data model
3. Persists the updated frame state via `persistGlobalFrameState()`
4. Collects all remaining frames (excluding the removed one)
5. Validates each frame has a valid `logicalIndex` property
6. Sorts remaining frames by their logical index
7. Updates logical indices for frames after the removed one (decrement by 1)
8. Updates button states (minus, up, down) for all remaining frames using `updateFrameButtonStates()`
9. Hides and schedules the frame widget for deletion via `deleteLater()`
10. Clears `lastFocusedFrame_` if it points to the removed frame
11. Updates window title and rebuilds all window menus

### Frame Addition Pattern
When adding frames in **Vertical or Horizontal** layout modes, use the **surgical addition pattern** via `addSingleFrame()`:
1. Inserts frame data into the `frames_` vector
2. Persists the updated frame state via `persistGlobalFrameState()`
3. Creates a new `SplitFrameWidget` with all signal connections
4. Uses `QSplitter::insertWidget()` to insert at the correct position
5. Updates logical indices for frames after the insertion point
6. Updates button states for all frames using `updateFrameButtonStates()`
7. Focuses the new frame's address bar

For **Grid** layout mode, surgical addition is not supported due to nested splitter complexity. Grid mode must use `rebuildSections()` for frame addition.

### When to Use rebuildSections()
`rebuildSections()` should ONLY be used when:
- **Initial window construction**: Building frames for the first time
- **Layout mode changes**: Switching between Vertical, Horizontal, and Grid layouts
- **Profile switches**: Changing to a different browser profile (requires new QWebEngineProfile instances)
- **Frame addition in Grid mode**: Adding frames when in Grid layout (nested splitters make surgical addition infeasible)
- **Frame reordering**: Moving frames up/down (QSplitter doesn't support widget reordering without rebuild)

Note: Frame reordering requires `rebuildSections()` because QSplitter doesn't provide a way to reorder widgets without removing and re-adding them. A surgical approach that only swaps logical indices will not visually move the frames.

### Helper Methods
- `updateFrameButtonStates(frame, totalFrames)`: Centralized logic for updating minus/up/down button enabled states based on frame position and total count. Avoids code duplication across multiple methods.
- `removeSingleFrame(frameToRemove)`: Surgically removes a single frame without rebuilding all frames. Used by both `onMinusFromFrame()` (minus button) and `onCloseShortcut()` (Cmd/Ctrl+W).
- `addSingleFrame(afterIndex)`: Surgically adds a new frame after the specified index without rebuilding all frames. Returns true on success, false if `rebuildSections()` is needed (Grid mode). Used by both `onPlusFromFrame()` (plus button) and `onNewFrameShortcut()` (Cmd/Ctrl+T).

### Frame Properties
Each `SplitFrameWidget` has a `logicalIndex` dynamic property (set via `QObject::setProperty()`) that maps the widget to its position in the `frames_` vector. Always validate this property before using it:
```cpp
const QVariant v = frame->property("logicalIndex");
if (!v.isValid()) {
  qWarning() << "Frame has no valid logicalIndex property";
  return;
}
const int idx = v.toInt();
```

### Future Optimization Opportunities
The current implementation uses `findChildren<SplitFrameWidget *>()` to locate frame widgets, which searches the widget tree. For typical use cases (a few frames per window) this is acceptable, but potential optimizations include:
- Maintain a `QList<SplitFrameWidget *>` or `std::vector<SplitFrameWidget *>` member variable to avoid tree traversals
- Cache logical indices before sorting to avoid redundant `property()` calls
- Extract helper methods like `findFrameByMaxIndex()` or `getLastFrame()` to reduce code duplication
These optimizations should be considered if profiling shows performance issues with many frames.

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

## Auto-Update System
Phraims implements a cross-platform auto-update mechanism to keep users on the latest version:

### Architecture
- **UpdateChecker** (`UpdateChecker.h/.cpp`): Core update checking logic that queries GitHub API for the latest release
  - Fetches release information from `https://api.github.com/repos/LookAtWhatAiCanDo/Phraims/releases/latest`
  - Parses release metadata including version, download URLs, and release notes
  - Compares semantic versions to determine if update is available
  - Platform-aware download URL selection based on architecture

- **UpdateDialog** (`UpdateDialog.h/.cpp`): UI for displaying update information and triggering platform-specific update flows
  - Shows current vs. latest version comparison
  - Displays release notes from GitHub in Markdown format
  - Platform-specific update buttons and progress indicators
  - Handles user choices: update now, view release notes, or remind later

- **SparkleUpdater** (`SparkleUpdater.h/.mm`): macOS Sparkle framework integration
  - Qt-friendly wrapper around Sparkle's SPUStandardUpdaterController
  - Conditionally compiled only on macOS (`#ifdef Q_OS_MACOS`)
  - Gracefully degrades to manual download if Sparkle framework not present
  - Handles update checking, downloading, verification, and installation
  - Uses Objective-C++ (.mm) for Cocoa/Foundation framework interop

- **WinSparkleUpdater** (`WinSparkleUpdater.h/.cpp`): Windows WinSparkle library integration
  - Qt-friendly wrapper around WinSparkle C API
  - Conditionally compiled only on Windows (`#ifdef Q_OS_WIN`)
  - Gracefully degrades to manual download if WinSparkle library not present
  - Handles update checking, downloading, signature verification, and installation
  - Same appcast feed format as macOS Sparkle (unified feed approach)
  - Built-in DSA/EdDSA signature verification and rollback support

### Platform-Specific Behavior

#### macOS
- **Implemented**: Sparkle framework integration with fallback to manual download
  - Uses Sparkle 2.x API (`SPUStandardUpdaterController`)
  - Reads appcast feed from GitHub releases (appcast.xml)
  - Supports Ed25519 signature verification (when keys configured)
  - Quarantine-safe updates without manual "Open Anyway" approval
  - If Sparkle framework not found at build time:
    - Compiles warning shown during build
    - Falls back to opening browser for manual DMG download
  - Info.plist configured with Sparkle keys:
    - `SUFeedURL`: Points to appcast.xml in releases
    - `SUEnableAutomaticChecks`: Set to false (manual checks only)
    - `SUScheduledCheckInterval`: 86400 seconds (1 day)
    - `SUPublicEDKey`: Placeholder for Ed25519 public key

#### Windows
- **Implemented**: WinSparkle library integration with fallback to manual download
  - Uses WinSparkle (Windows port of Sparkle framework)
  - Reads same appcast feed as macOS (appcast.xml)
  - Built-in DSA/EdDSA signature verification
  - Automatic update download, installation, and rollback
  - Native Windows update UI consistent with Sparkle
  - If WinSparkle library not found at build time:
    - Compile warning shown during build
    - Falls back to opening browser for manual installer download
  - Shares appcast.xml with macOS (reduces CI complexity)
  - Supports delta updates and automatic rollback on failure

#### Linux
- **Current**: Opens browser to release page for manual download
- **Rationale**: Linux users typically prefer package manager updates
- No auto-install to respect distribution package management conventions

### Update Check Flow
1. User selects `Help → Check for Updates...`
2. `SplitWindow::checkForUpdates()` creates UpdateChecker instance
3. Progress dialog shows "Checking for updates..."
4. UpdateChecker queries GitHub API asynchronously
5. On success:
   - If update available: Show UpdateDialog with release notes
   - If up to date: Show info message
6. On failure: Show error message with details

### Version Comparison
- Uses semantic versioning (MAJOR.MINOR.PATCH)
- `UpdateChecker::compareVersions()` handles numeric component comparison
- Strips 'v' prefix from tags (e.g., "v0.56" → "0.56")
- Returns -1, 0, or 1 for less than, equal, or greater than

### CI Integration

#### Artifact Generation
The `create-release` job in `.github/workflows/build-phraims.yml` generates two update metadata files:

1. **version.json** - Universal version manifest for all platforms
   - Generated by `ci/generate-version-manifest.sh`
   - Contains version number, release date, and platform-specific download URLs
   - Used by UpdateChecker to determine latest version
   - Format supports future extensibility (signature hashes, file sizes)

2. **appcast.xml** - Sparkle-specific feed for macOS
   - Generated by `ci/generate-appcast.sh`
   - RSS 2.0 format with Sparkle extensions
   - Contains version info, release notes, DMG URLs, and signature placeholders
   - Supports both arm64 and x86_64 architectures
   - Minimum system version specified per architecture

#### Build-Time Configuration
- **macOS**: CMake searches for Sparkle.framework in standard locations
  - If found: Links framework and copies into app bundle
  - If not found: Shows warning, builds without Sparkle (manual download fallback)
- **Windows**: Always includes WindowsUpdater (no optional dependencies)
- **Linux**: No update infrastructure (manual-only by design)

#### Signing (Future)
- **macOS**: Ed25519 keys for appcast signature verification
  - Generate with `sign_update` tool from Sparkle SDK
  - Store private key securely in CI secrets
  - Public key embedded in Info.plist
- **Windows**: Authenticode code signing for installers
  - Requires signing certificate in CI
  - Integrated into installer build process

### Security Considerations
- **macOS**: Future Sparkle integration will use Ed25519 signatures
- **Windows**: Future code signing will verify installer authenticity
- **Linux**: Users rely on distribution package verification
- GitHub API uses HTTPS for all communications
- No automatic code execution without user confirmation (except Windows silent install after UAC)

### Future Enhancements
- Automatic update checks on startup (with user preference toggle)
- Update check interval configuration (daily, weekly, manual only)
- Beta/stable channel selection
- Delta updates for bandwidth efficiency
- Rollback capability if update fails
- Background download with notification when ready

### Settings Keys
No persistent settings yet; all update checks are user-initiated. Future additions:
- `updateCheckEnabled` (bool): Enable automatic update checks
- `updateCheckInterval` (int): Days between automatic checks
- `updateChannel` (string): "stable" or "beta"
- `lastUpdateCheck` (datetime): Timestamp of last check

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
