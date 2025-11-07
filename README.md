Phraim
======

Phraim is a web browser that divides each window into multiple resizable frames.

## Code Organization

The codebase is organized into modular components for better maintainability:

- **main.cpp** - Application entry point and initialization
- **SplitWindow** - Main window class with menu bar and splitter management
- **SplitFrameWidget** - Individual web view frame with navigation controls
- **MyWebEngineView** - Custom QWebEngineView with context menu support
- **DomPatch** - DOM patching system for CSS customizations
- **EscapeFilter** - Fullscreen escape key handler
- **Utils** - Shared utilities and helper functions

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

Controls:
- Use the + / - toolbar buttons or the spinbox to change the number of sections.
- Each section is equally sized using layout stretch factors.

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

## TODOs
- AI based Window titles based on content
- Command-T to "Open New Tab" (really "Phraim")
- On new frame, focus cursor to address bar edit
- Improve Menu
  - Make similar to Chrome, VSCode, OBS, etc.
  - GOOD GRIEF LOTS OF OPTIONS!  
    Is there a cross-platform design guideline for what an app should have?!?!
  - Menu
    - ...
    - Window
      - Minimize Cmd-M
      - Zoom
      - Fill Control-Globe-F
      - Center Control-Globe-C
      - ---
      - Move & Resize
        - Halves
          - Left Control-Globe-Left
          - Right Control-Globe-Right
          - Top Control-Globe-Top
          - Bottom Control-Globe-Bottom
        - Quarters
          - Top Left
          - Top Right
          - Bottom Left
          - Bottom Right
        - Arrange
          - Left & Right Control-Shift-Globe-Left
          - Right & Left Control-Shift-Globe-Right
          - Top & Bottom Control-Shift-Globe-Up
          - Bottom & Top Control-Shift-Globe-Down
          - Quarters
        - Return to Previous Size Control-Globe-R
      - Full Screen Tile
        - Left of Screen
        - Right of Screen
      - ---
      - Remove Window from Set
      - ---
      - Switch Window...
      - ---
      - Bring All to Front
      - ---
      - ✓ Group 1
      - ...
      - ♢ Group N
- Per-window Always On Top; app's multi-window always on top does not make much sense
- Support multiple instances  
  Chromium may not support multiple instances **of a single profile**.
- Add pre-set collections that can be quickly recalled  
  aka: Bookmarks/Groups
- Address History
  - Drop down history
  - Auto-complete address from history
- Proper popup support (window, tab [create new frame], dialog, "background"?)
- Ability to drag windows together to frame up
- Ability to drag frames around to reorganize/dock them?
- Make a serious browser
  - Popup Menu
    - ...
    - Copy Link Address
    - ...
  - Browse History
  - Bookmarks
  - Passkeys!!
  - Downloads Shift-Command-J
  - Extensions
  - Settings
  - ...; all expected things from a browser!

### BUGs
- App can play YouTube vids but not Kick or Twitch?
