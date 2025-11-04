Qt6 Splitter Hello
==================

Simple Qt6 Widgets demo that divides the main area into N equal sections.

Build (requires Qt6 development libraries and a working CMake):

```bash
# from repository root
mkdir -p prototypes/qt-splitter/build && cd prototypes/qt-splitter/build
cmake ..
cmake --build . --config Release
./QtSplitterHello
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
