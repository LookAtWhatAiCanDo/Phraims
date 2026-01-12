# Copilot Instructions for Phraims

Phraims is a multi-frame web browser built with Qt6 and QtWebEngine (Chromium-based). Each window can be split into multiple resizable web page frames with independent browsing contexts.

## Quick Reference

- **Language**: C++17 with Qt6 framework
- **Build System**: CMake
- **Main Components**: SplitWindow (main window), SplitFrameWidget (individual frames), QtWebEngine (web rendering)
- **Current Version**: 0.55 (defined in CMakeLists.txt)

## Quick Start for Contributors

1. **Setup**: Install Qt6 (e.g., `brew install qt6` on macOS)
2. **Build**: `cmake -S . -B build -DCMAKE_PREFIX_PATH=$(brew --prefix qt6) && cmake --build build`
3. **Run**: `./build/Phraims`
4. **Before making changes**:
   - Read AGENTS.md for detailed architecture
   - Understand the module you're modifying
   - Test your build setup works
5. **Making changes**:
   - Follow coding style (2 spaces, PascalCase for classes, trailing underscore for members)
   - Use AppSettings for persistence, never QSettings directly
   - Update documentation (AGENTS.md, README.md) when changing behavior
   - Test manually: build, run, exercise your feature, quit/relaunch to verify persistence
6. **Before committing**:
   - Ensure code builds without errors
   - Verify your changes work as expected
   - Update relevant documentation
   - Write clear commit message describing the change

## Essential Guidelines

### Persistence Rules (MANDATORY)
- **NEVER** use `QSettings` directly. Always use the `AppSettings` wrapper class
- Settings stored in INI format at `<AppDataLocation>/settings.ini`
- Document new settings keys in both AGENTS.md and README.md immediately

### Coding Style
- **Indentation**: 2 spaces (no tabs)
- **Classes**: PascalCase (e.g., `SplitFrameWidget`)
- **Member variables**: trailing underscore (e.g., `backBtn_`)
- **Free functions**: camelCase
- **Braces**: Opening brace on same line
- **Comments**: Doxygen-style (`/** */`) for all public APIs
- **Auto usage**: Use `auto` when type is obvious from initializer

### Keyboard Shortcuts
- Use `Qt::CTRL` for platform-agnostic shortcuts (Qt maps to ⌘ on macOS automatically)
- **NEVER** use `#ifdef Q_OS_MAC` with `Qt::META` - Qt handles this mapping
- Use `QKeySequence` standard sequences when available
- Document all shortcuts in README.md and AGENTS.md

### Build & Test Commands
```bash
# Configure (point to Qt6 install, e.g., $(brew --prefix qt6) on macOS)
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt

# Build
cmake --build build --config Release

# Run
./build/Phraims

# Clean
cmake --build build --target clean
```

### Example Code Patterns

**Correct way to persist settings:**
```cpp
// Good - uses AppSettings wrapper
AppSettings s;
s.setValue("myKey", value);
```

**Correct way to add keyboard shortcuts:**
```cpp
// Good - uses Qt::CTRL (maps to Cmd on macOS, Ctrl elsewhere)
QAction* action = new QAction("My Action", this);
action->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
```

**Correct way to remove a frame:**
```cpp
// Good - surgical removal preserves other frames
removeSingleFrame(frameToRemove);

// Bad - rebuilds all frames, stops media playback
rebuildSections();
```

### Module Organization
- **main.cpp** - Entry point, QApplication setup, single-instance guard
- **SplitWindow.h/.cpp** - Main window with splitter layouts, menus, persistence
- **SplitFrameWidget.h/.cpp** - Individual frame widget with navigation and web view
- **MyWebEngineView.h** - Custom QWebEngineView (header-only)
- **DomPatch.h/.cpp** - DOM patch management and persistence
- **Utils.h/.cpp** - Shared utilities, window tracking, profile management
- **AppSettings.h** - QSettings wrapper (mandatory for all persistence)

### Key Features

#### Profiles System
- Each profile has isolated cookies, cache, history under `<AppDataLocation>/profiles/<profileName>/`
- Managed via `Utils.h/.cpp`: `getProfileByName()`, `createProfile()`, `deleteProfile()`, `renameProfile()`
- Global current profile stored in `AppSettings` under `currentProfile` key
- Per-window profiles stored in `windows/<id>/profileName`

#### Incognito Mode
- Uses off-the-record `QWebEngineProfile` (no disk persistence)
- Windows marked with `isIncognito_` flag
- Skips all QSettings persistence operations
- Profile menu hidden in Incognito windows

#### Frame Scale Controls
- Zoom applies only to web content, not UI chrome
- Uses `QWebEngineView::setZoomFactor()`
- Persisted per-frame in `frameScales` key
- View menu actions operate on focused frame

### Continuous Integration
- macOS and Windows builds run on every push/PR via GitHub Actions
- Downloads proprietary QtWebEngine artifacts from private `QtWebEngineProprietaryCodecs` repo
- Matrix builds for arm64 and x86_64 (macOS) / x64 and arm64 (Windows)
- See `.github/workflows/build-phraims-{macos,windows}.yml`

### Important Patterns

✅ **DO**:
- Use `AppSettings` wrapper for all persistence
- Update documentation (AGENTS.md, README.md) when changing behavior
- Add Doxygen comments to all new public APIs
- Route all frame zoom operations through `setScaleFactor()`
- Use Qt containers when interacting with Qt APIs
- Test persistence by quit/relaunch cycles

❌ **DON'T**:
- Never instantiate `QSettings` directly
- Avoid platform-specific conditionals for keyboard shortcuts
- Don't duplicate zoom state outside `setScaleFactor()`
- Don't add transient "audit" comments in source code
- Don't modify working code unless necessary
- Don't break backward compatibility of settings keys

### Version Management
- Single source of truth: `project()` VERSION in CMakeLists.txt
- CMake generates `build/version.h` from `version.h.in`
- Version appears in: About dialog, startup logs, build artifacts
- Update CMakeLists.txt, regenerate, update README.md, rebuild

### Security Considerations
- **Never commit secrets or credentials** to source code
- Use `AppSettings` for user preferences, not sensitive data
- Profile data is stored unencrypted; treat it as user-controlled
- Incognito mode provides session isolation but data exists in memory
- Web content runs in Chromium sandbox via QtWebEngine
- Be cautious with user-provided URLs and JavaScript execution

### Testing Strategy
- Manual testing is primary verification method (see README.md acceptance scenarios)
- Always test persistence by quitting and relaunching the application
- Test profile switching, frame operations, and window restoration
- Verify Incognito mode doesn't persist data to disk
- Test on target platforms (macOS arm64/x86_64, Windows x64/arm64) when possible
- Automated tests are planned but not yet implemented

### Common Pitfalls
- **Frame lifecycle**: Don't use `rebuildSections()` when `removeSingleFrame()` or `addSingleFrame()` would work - it stops media playback in all frames
- **QSettings**: Never instantiate directly; breaking this rule causes persistence bugs
- **Memory management**: Qt parent-child ownership is used; be careful with `deleteLater()`
- **Signal connections**: Verify connections are established before emitting signals
- **Profile switching**: Must call `rebuildSections()` to update QWebEngineProfile instances
- **Keyboard shortcuts**: Platform differences are handled by Qt; don't add conditional logic

### Agent Responsibilities
- Keep AGENTS.md and README.md synchronized with code changes
- Update Doxygen comments when modifying APIs
- Generate descriptive commit messages
- Store learned patterns in documentation immediately
- When fixing bugs, document the root cause and solution in AGENTS.md
- Test changes manually before committing (build, run, exercise feature)

## More Details

For comprehensive architecture details, testing guidelines, and deeper implementation patterns, see [AGENTS.md](../AGENTS.md) in the repository root.
