Phraims
======

Phraims is a web browser that divides each window into multiple resizable web page frames.

## Code Organization

The codebase is organized into modular components for better maintainability:

- **main.cpp** - Application entry point and initialization
- **SplitWindow** - Main window class with menu bar and splitter management
- **SplitFrameWidget** - Individual web view frame with navigation controls
- **MyWebEngineView** (header-only) - Custom QWebEngineView with context menu support
- **DomPatch** - DOM patching system for CSS customizations
- **EscapeFilter** (header-only) - Fullscreen escape key handler
- **Utils** - Shared utilities and helper functions

Simple classes like `EscapeFilter` and `MyWebEngineView` use header-only implementations for easier maintenance.

Settings and Profiles are stored at:
- macOS:
  - Settings: `~/Library/Application Support/LookAtWhatAiCanDo/Phraims/settings.ini`
  - Profile: `~/Library/Application Support/LookAtWhatAiCanDo/Phraims/profiles/`
- Linux:
  - Settings: `~/.config/LookAtWhatAiCanDo/Phraims/settings.ini`
  - Profile: `~/.config/LookAtWhatAiCanDo/Phraims/profiles/`
- Windows:
  - Settings: `%APPDATA%/LookAtWhatAiCanDo/Phraims/settings.ini`
  - Profile: `%APPDATA%/LookAtWhatAiCanDo/Phraims/profiles/`

Build (requires CMake + Qt 6 + WebEngine w/ proprietary codecs enabled; the Homebrew `qt6` package is confirmed to ship with `-DFEATURE_webengine_proprietary_codecs=ON`):
* https://github.com/search?q=repo%3AHomebrew%2Fhomebrew-core%20DFEATURE_webengine_proprietary_codecs&type=code
```bash
# from repository root
brew install qt6
mkdir -p build && cd build
cmake .. -DCMAKE_PREFIX_PATH="$(brew --prefix qt6)"
cmake --build . --config Release
./Phraims
```

macOS CI builds an arm64 app bundle in `build_macos_arm64/` using Homebrew `qt6` and packages it as a DMG (Homebrew Qt bottles are arm64-only on Apple Silicon runners).

### Local macOS packaging

Run `./ci/build-local-macos.sh` to:
1. update Homebrew
1. install dependencies
1. build Release with Ninja in `build_macos_arm64`
1. run `macdeployqt`
1. sync QtWebEngine resources
1. fix rpaths
1. sign
1. emit `build_macos_arm64/Phraims.dmg`.  

It also validates that every dependency resolves inside the bundle and checks WebEngine resources.

Set `FORCE_BREW_UPDATE=0` to skip `brew update` if needed.

- Normal run: `./ci/build-local-macos.sh`
- Debug info: `DEBUG=1 ./ci/build-local-macos.sh` (shows macdeployqt log, staging/Frameworks listings, and rpaths for the main binary and QtWebEngineProcess)

## Controls and Shortcuts

### Frame Management
- **New Frame**: Click the `+` button on any frame or press `⌘T` (Command-T on macOS) or `Ctrl+T` (other platforms) to add a new frame after the currently focused frame.  
  When adding a new frame the address bar is automatically focused so you can start typing immediately.
- **Remove Frame**: Click the `-` button on any frame to remove it (confirmation required)
- **Reorder Frames**: Use the `↑` and `↓` buttons to move frames up or down
- **Double-click any splitter handle** to instantly resize the two adjacent panes to equal sizes (50/50 split).
- **Reload Frame**: Press `⌘R` (macOS) or `Ctrl+R` (other platforms) to reload the focused frame, or use `View -> Reload Frame`.
- **Reload Frame (Bypass Cache)**: Press `⌘⇧R` (macOS) or `Ctrl+Shift+R` (other platforms) to force-refresh the focused frame via `View -> Reload Frame (Bypass Cache)`.

### Window Management
- **New Window**: Press `⌘N` (Command-N on macOS) or `Ctrl+N` (other platforms)
- **New Incognito Window**: Press `⇧⌘N` (Shift+Command-N on macOS) or `Shift+Ctrl+N` (other platforms) to open a private browsing window
- **Toggle DevTools**: Press `F12` to toggle developer tools for the focused frame

### Other Controls
- Each section is equally sized using layout stretch factors
- Use the Layout menu to switch between Grid, Vertical, and Horizontal arrangements

## Profiles

Phraims supports multiple browser profiles, each with its own separate browsing data, cookies, cache, and history. This allows you to maintain completely isolated browsing contexts within the same application.

### Managing Profiles

Access profile management through the **Profiles** menu in the menu bar:

- **New Profile...**: Create a new profile with a custom name
  - Profile names cannot contain slashes (/ or \)
  - Each profile gets its own storage directory
  
- **Rename Profile...**: Rename an existing profile
  - Select the profile to rename from the list
  - Enter a new name (cannot contain slashes)
  - If you rename the currently active profile, it will be updated automatically
  
- **Delete Profile...**: Permanently delete a profile and all its data
  - Select the profile to delete from the list
  - Confirmation is required before deletion
  - Cannot delete the last remaining profile
  - All cookies, cache, history, and other data will be permanently removed
  
- **Open Profiles Folder** (debug builds only): Opens the profiles directory in your system file browser
  - Shows all profile directories on your filesystem
  - Useful for backup, inspection, or manual management of profile data
  - Only available when running a debug build of the application
  
- **Profile List**: Shows all available profiles
  - The currently active profile is marked with a checkmark
  - Click any profile to switch to it immediately

### Using Profiles

- Each window title displays the current profile name (e.g., "Group 1 (3) - Work")
- Each window remembers which profile it was using and restores it on app restart
- When you switch profiles, all frames in the current window are rebuilt with the new profile
- New windows use the most recently selected profile by default
- Profile data is stored in your application data directory under `profiles/<profile-name>/`

### Default Profile

When you first launch Phraims, a "Default" profile is automatically created and used. You can create additional profiles and switch between them at any time.

## Incognito Mode

Phraims supports Incognito (private) browsing windows for ephemeral sessions that do not persist history, cookies, or other browsing data.

### Opening Incognito Windows

- **Keyboard Shortcut**: Press `⇧⌘N` (Shift+Command-N on macOS) or `Shift+Ctrl+N` (other platforms)
- **Menu**: Select `File -> New Incognito Window`

### Incognito Window Behavior

- **Isolated Storage**: Each Incognito window uses a separate off-the-record profile that does not persist to disk
- **No History**: Browsing history, cookies, cache, and other data are discarded when the window closes
- **Visual Indicator**: Incognito windows display "Incognito" in the title bar to distinguish them from normal windows
- **No Persistence**: Window geometry, frame addresses, and splitter sizes are not saved between sessions
- **No Profile Management**: The Profiles menu is not available in Incognito windows since they use ephemeral profiles
- **Independent Operation**: Incognito and normal windows operate completely independently without cross-contamination

### Use Cases

- **Temporary Browsing**: View websites without affecting your browsing history or saved data
- **Multiple Logins**: Log into the same website with different accounts simultaneously
- **Testing**: Test website behavior without cached data or cookies
- **Privacy**: Browse sensitive content without leaving traces on your system

### Per-frame zoom controls
- Use the `A-`, `A+`, and `1x` buttons in each frame header (or `View -> Increase/Decrease/Reset Frame Scale`) to zoom the embedded page without touching splitter sizes or header chrome. These controls are simply a shortcut for adjusting the QWebEngineView zoom on a frame-by-frame basis.
- The UI chrome stays at a consistent size so controls remain easy to target even when a page is zoomed way in/out.
- Zoom choices are stored per frame in the current layout. Closing and reopening the app restores the last zoom factor for each saved slot.

## DOM Patches

This application supports persisting small DOM CSS "patches" you create while using the inspector.  
A patch is a site-scoped CSS tweak (for example hiding an element) that the app will automatically re-apply whenever a matching page is loaded or navigated to.

How it works
- Patches are stored in JSON at the application data root (the app prints this path on startup). The file is named `dom-patches.json`.
- Each patch contains:
	- `urlPrefix` — full URL prefix to match (startsWith matching). Use a value like `https://studio.youtube.com/live_chat` to match that page and its navigations.
	- `selector` — CSS selector for the element to target (e.g. `#card`).
	- `css` — CSS declarations applied to the selector (e.g. `display: none;`).
	- `enabled` — whether the patch is active.

Using the feature
- Open `Tools -> DOM Patches`.
- Click Add and fill in the fields. Example to hide the YouTube Studio chat card:
	- URL prefix: `https://studio.youtube.com/live_chat`
	- CSS selector: `#card`
	- CSS declarations: `display: none;`
- Save and reload the page — the element will be hidden automatically. Patches persist across app restarts.

Notes & limitations
- Matching is simple `startsWith` on the page URL. If you need broader matching we can add glob/regex options.
- This is CSS-only for now (safe and performant). If a rule needs JS, it can be added later.
- For single-page apps the app re-applies patches on URL changes and after page loads; that covers most SPA navigations.
- The manager is a lightweight dialog — future enhancements can include a context-menu helper to capture a selector directly from the page.

Data format example (`dom-patches.json` entry):
```
{
	"id": "550e8400-e29b-41d4-a716-446655440000",
	"urlPrefix": "https://studio.youtube.com/live_chat",
	"selector": "#card",
	"css": "display: none;",
	"enabled": true
}
```

Removing or editing patches
- Use `Tools -> DOM Patches` to edit or delete saved patches. Deleting a patch removes it from the JSON file and the page will return to its original styling.

Privacy & safety
- Patches run only locally in this application. They are not synced or sent anywhere.

## Context Menu

The web view context menu provides quick access to common actions:
- **Navigation**: Back, Forward, Reload
- **Editing**: Cut, Copy, Paste, Select All
- **Copy Link Address**: Copies the fully encoded hyperlink under the cursor to the clipboard
- **Translate…**: Translate selected text or the entire page using Google Translate
  - If text is selected, opens translation of the selected text
  - If no text is selected, opens full page translation
  - Opens in a new Phraims window
- **Inspect…**: Opens DevTools for page inspection and debugging

## Continuous Integration

The project uses GitHub Actions to automatically build macOS binaries on every push to `main` and on pull requests. This ensures the codebase stays healthy and provides downloadable artifacts for testers.

### Downloading Build Artifacts

After each successful CI run, you can download the built macOS app bundle:
1. Navigate to the [Actions tab](../../actions) in the repository
2. Click on the latest workflow run
3. Scroll down to the "Artifacts" section
4. Download `Phraims-macOS` - this contains the `Phraims.app` bundle
5. Extract and run the app (you may need to allow it in System Preferences > Security & Privacy)

Artifacts are retained for 90 days. The workflow can also be triggered manually via the "Run workflow" button for building release candidates.

## TODOs
- Improve Menu
  - Make similar to Chrome, VSCode, OBS, etc.
  - GOOD GRIEF LOTS OF OPTIONS!  
    Is there a cross-platform design guideline for what an app should have?!?!
- Support multiple instances  
  Chromium may not support multiple instances **of a single profile**.
- Add pre-set collections that can be quickly recalled  
  aka: Bookmarks/Groups
- Make a serious browser
  - Popup Menu
    - ...
    - Copy Link Address
    - ...
    - ~~Translate~~ ✓ (completed)
    - ...
  - Browser History
  - Bookmarks
  - Passkeys!!
  - Downloads Shift-Command-J
  - Extensions ([GitHub Issue #9](./issues/9))
  - Settings
  - ...; all expected things from a browser!
  - User-Agent
- Support opening `phraims://inspect` similar to [chrome://inspect](chrome://inspect)

## BUGS
- Discord page blank white
- messenger.com page not loading

## Debugging

```
open build/Qt_6_10_0_for_macOS-Debug/Phraims.app --args --webEngineArgs --remote-debugging-port=9222
```
Open Chrome to [chrome://inspect](chrome://inspect)

## Tests

Acceptance tests / expected behavior
---------------------------------
These tests define the desired behavior for layout selection and splitter persistence.

1) Setting a layout
	- Action: Select a layout from the `Layout` menu (Grid, Stack Vertically, Stack Horizontally).
	- Expected: The UI rebuilds so all frames are visible and equally sized.

2) Manually adjusting splitters
	- Action: Drag a splitter handle to change sizes of adjacent frames.
	- Expected: The UI updates immediately to reflect the new sizes. The new sizes are stored on application exit and will be loaded on next launch.

3) Close app and reopen — manual splitter positions persist
	- Action: With manual splitter adjustments made, close the application and then relaunch it.
	- Expected: The frames open with the splitters in the last positions the user set before exit.

4) Re-setting the layout (re-selecting the currently selected layout)
	- Action: Choose the currently-active layout again from the `Layout` menu.
	- Expected: The layout fully resets and rebuilds; all frames are laid out evenly (default sizes). Persisted splitter sizes are NOT applied when re-selecting a layout.

5) Changing to another layout
	- Action: Select a different layout from the `Layout` menu.
	- Expected: The layout switches and rebuilds. The new layout starts in default (evenly distributed) sizes. Persisted splitter sizes are only applied on application startup — not when changing layouts during a running session.

6) Per-frame zoom persists
	- Action: Use the `A-` / `A+` buttons (or the View menu actions) to change the zoom of a frame, quit the application, and relaunch it.
	- Expected: Each frame reopens with the exact zoom factor that was active before quitting. Only the web content should change size; splitter handles and chrome stay put.

7) Creating a new profile
	- Action: Select `Profiles -> New Profile...` from the menu bar, enter a name (e.g., "Work"), and click OK.
	- Expected: The profile is created successfully. A confirmation message appears. The Profiles menu now shows the new profile in the list.

8) Switching between profiles
	- Action: Open the Profiles menu and select a different profile from the list (e.g., switch from "Default" to "Work").
	- Expected: The window immediately switches to the selected profile. All frames are rebuilt with the new profile's data. The checkmark in the Profiles menu moves to the newly selected profile. Cookies and browsing data are now isolated to the selected profile.

9) Profile persistence across restarts
	- Action: Switch to a specific profile (e.g., "Work"), load a website that sets a cookie, close the app, and reopen it.
	- Expected: The window reopens using the last-selected profile ("Work"). The website's cookie is still present, confirming data persistence within that profile.

10) Renaming a profile
	- Action: Select `Profiles -> Rename Profile...`, choose a profile from the list, enter a new name, and confirm.
	- Expected: The profile is renamed successfully. If the renamed profile was active, the current window continues using it with the new name. The Profiles menu reflects the new name.

11) Deleting a profile
	- Action: Select `Profiles -> Delete Profile...`, choose a profile (not the current one), and confirm deletion.
	- Expected: A confirmation dialog appears warning about permanent data loss. After confirming, the profile and all its data are deleted. The profile disappears from the Profiles menu. If multiple profiles exist, deletion succeeds; if only one profile remains, deletion is blocked with an error message.

12) Profile data isolation
	- Action: Create two profiles (e.g., "Personal" and "Work"). In "Personal", log into a website. Switch to "Work" and visit the same website.
	- Expected: The website in the "Work" profile does not show the logged-in state from "Personal", confirming complete data isolation between profiles.

13) Opening an Incognito window
	- Action: Press `Shift+Command+N` (macOS) or `Shift+Ctrl+N` (other platforms), or select `File -> New Incognito Window`.
	- Expected: A new window opens with "Incognito" in the title bar. The window starts with a single empty frame.

14) Incognito window isolation
	- Action: Open an Incognito window, visit a website that sets a cookie or requires login, then close the Incognito window. Reopen a new Incognito window and visit the same website.
	- Expected: The second Incognito window does not have the cookie or login state from the first session, confirming ephemeral storage.

15) Incognito window non-persistence
	- Action: Open an Incognito window, load several websites in multiple frames, adjust splitter sizes, then close the application. Relaunch the application.
	- Expected: The Incognito window does not reopen. Only normal (non-Incognito) windows are restored with their saved state.

16) Normal and Incognito window independence
	- Action: Open a normal window and an Incognito window side-by-side. Log into a website in the normal window. Visit the same website in the Incognito window.
	- Expected: The Incognito window does not share the login state from the normal window, confirming complete isolation between normal and Incognito sessions.

Notes
- Persisted splitter positions are only loaded once at application startup. During normal runtime, selecting or re-selecting layouts resets to default split positions.
- The app persists splitter sizes on exit so they can be used for the next application launch.
- Recommended: test quickly by resizing splitters, closing the app, and reopening to verify persistence.
- Profile data is stored per-profile in separate directories under the application data location (shown at startup).
