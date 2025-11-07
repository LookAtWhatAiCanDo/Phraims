Phraim
======

Phraim is a web browser that divides each window into multiple resizable web page frames.

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

Settings are stored at:
- macOS:
  - Preferences: /Users/pv/Library/Preferences/com.swooby.Phraim.plist
  - Profile: /Users/pv/Library/Application Support/swooby/Phraim
- Windows: TBD
- Linux: TBD

Build (requires Qt6 development libraries and a working CMake):

```bash
# from repository root
mkdir -p build && cd build
cmake ..
cmake --build . --config Release
./Phraim
```

## Controls and Shortcuts

### Frame Management
- **New Frame**: Click the `+` button on any frame or press `⌘T` (Command-T on macOS) or `Ctrl+T` (other platforms) to add a new frame after the currently focused frame.  
  When adding a new frame the address bar is automatically focused so you can start typing immediately.
- **Remove Frame**: Click the `-` button on any frame to remove it (confirmation required)
- **Reorder Frames**: Use the `↑` and `↓` buttons to move frames up or down
- **Double-click any splitter handle** to instantly resize the two adjacent panes to equal sizes (50/50 split).

### Window Management
- **New Window**: Press `⌘N` (Command-N on macOS) or `Ctrl+N` (other platforms)
- **Toggle DevTools**: Press `F12` to toggle developer tools for the focused frame

### Other Controls
- Each section is equally sized using layout stretch factors
- Use the Layout menu to switch between Grid, Vertical, and Horizontal arrangements

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
- **Translate…**: Translate selected text or the entire page using Google Translate
  - If text is selected, opens translation of the selected text
  - If no text is selected, opens full page translation
  - Opens in the system's default browser
- **Inspect…**: Opens DevTools for page inspection and debugging

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
  - Extensions ([GitHub Issue #9](https://github.com/swooby/Phraim/issues/9))
  - Settings
  - ...; all expected things from a browser!
