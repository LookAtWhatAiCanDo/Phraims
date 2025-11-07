#pragma once

#include <QMainWindow>
#include <QString>
#include <QPointer>
#include <vector>

class QVBoxLayout;
class QWidget;
class QWebEngineProfile;
class QWebEngineView;
class QWebEnginePage;
class QSplitter;
class QMenu;
class SplitFrameWidget;
class SplitterDoubleClickFilter;

/** @brief Debug flag to show per-window UUID in title for debugging */
extern bool DEBUG_SHOW_WINDOW_ID;

/**
 * @brief Main window managing multiple resizable web view frames.
 *
 * SplitWindow provides the main application window with:
 * - Menu bar (File, View, Layout, Tools, Window)
 * - Splitter-based layout for multiple web view frames
 * - Layout modes: Vertical, Horizontal, and Grid
 * - Persistent state (window geometry, frame addresses, splitter sizes)
 * - Multi-window coordination and Window menu management
 * - Shared DevTools view for debugging
 * - DOM patches management
 *
 * Each window has a unique ID for storing state separately in QSettings.
 */
class SplitWindow : public QMainWindow {
  Q_OBJECT

public:
  /** @brief Available layout modes for organizing frames */
  enum LayoutMode { Vertical = 0, Horizontal = 1, Grid = 2 };

  /**
   * @brief Constructs a SplitWindow.
   * @param windowId Optional UUID for restoring saved state. If empty, a new ID is generated.
   * @param parent Optional parent widget
   *
   * If windowId is provided, the window loads its saved addresses, layout, geometry,
   * and splitter sizes from QSettings group "windows/<windowId>".
   */
  SplitWindow(const QString &windowId = QString(), QWidget *parent = nullptr);

  /**
   * @brief Persists window state to QSettings.
   *
   * Saves addresses, layout mode, window geometry, window state, and splitter sizes
   * under QSettings group "windows/<id>". If this window doesn't have an ID yet,
   * a new UUID is generated so the window will be restorable on next launch.
   */
  void savePersistentStateToSettings();

  /**
   * @brief Refreshes the Window menu.
   *
   * Public wrapper for updateWindowMenu() to allow external callers to trigger
   * menu updates without exposing the private implementation.
   */
  void refreshWindowMenu();

  /**
   * @brief Updates the window title to show window index and frame count.
   *
   * Sets the title to "Group X (N)" where X is the 1-based index of this window
   * in g_windows and N is the number of frames currently in the window.
   * If DEBUG_SHOW_WINDOW_ID is true, also appends the window UUID.
   */
  void updateWindowTitle();

  /**
   * @brief Sets the address for the first frame.
   * @param address The URL to load in the first frame
   *
   * Helper method for createAndShowWindow() to set an initial address when
   * creating a new window with a specific URL.
   */
  void setFirstFrameAddress(const QString &address);

public slots:
  /**
   * @brief Resets the window to a single empty section.
   *
   * Clears all frames and creates one new empty frame. Used for the
   * "New Window" behavior. Focuses the address bar after resetting.
   */
  void resetToSingleEmptySection();

  /**
   * @brief Focuses the first frame's address bar.
   *
   * Finds the first SplitFrameWidget and focuses its address QLineEdit
   * with all text selected, ready for the user to type.
   */
  void focusFirstAddress();

private slots:
  /**
   * @brief Rebuilds the frame layout with the specified number of sections.
   * @param n The number of frames to create
   *
   * Clears existing frames and creates n new SplitFrameWidget instances
   * arranged according to the current layoutMode_. Preserves addresses
   * from the addresses_ vector.
   */
  void rebuildSections(int n);
  
  /**
   * @brief Toggles DevTools for the currently focused frame.
   *
   * If DevTools is already open, hides it. Otherwise, attaches DevTools
   * to the focused frame (or first frame if none focused) and shows it.
   */
  void toggleDevToolsForFocusedFrame();
  
  /**
   * @brief Handles the plus button click from a frame.
   * @param who The frame that emitted the signal
   *
   * Inserts a new empty frame immediately after the clicked frame.
   */
  void onPlusFromFrame(SplitFrameWidget *who);
  
  /**
   * @brief Handles the up button click from a frame.
   * @param who The frame that emitted the signal
   *
   * Moves the frame up (towards index 0) in the addresses list and rebuilds.
   */
  void onUpFromFrame(SplitFrameWidget *who);
  
  /**
   * @brief Handles the down button click from a frame.
   * @param who The frame that emitted the signal
   *
   * Moves the frame down (towards higher indices) in the addresses list and rebuilds.
   */
  void onDownFromFrame(SplitFrameWidget *who);
  
  /**
   * @brief Changes the layout mode and rebuilds frames.
   * @param m The new layout mode to apply
   *
   * If selecting the same mode again, resets splitters to default sizes.
   * Otherwise, switches to the new mode and rebuilds the frame layout.
   */
  void setLayoutMode(SplitWindow::LayoutMode m);
  
  /**
   * @brief Sets the window height to match the screen's available height.
   *
   * Preserves the current x position and width, but sets y to the top of
   * the screen and height to the screen's available height.
   */
  void setHeightToScreen();
  
  /**
   * @brief Handles the minus button click from a frame.
   * @param who The frame that emitted the signal
   *
   * Shows a confirmation dialog and removes the frame if confirmed.
   * Prevents removal of the last frame.
   */
  void onMinusFromFrame(SplitFrameWidget *who);
  
  /**
   * @brief Handles address editing from a frame.
   * @param who The frame that emitted the signal
   * @param text The new address text
   *
   * Updates the addresses_ vector and persists to QSettings.
   */
  void onAddressEdited(SplitFrameWidget *who, const QString &text);
  
  /**
   * @brief Handles DevTools request from a frame.
   * @param who The frame that emitted the signal
   * @param page The QWebEnginePage to inspect
   * @param pos The position where the request originated
   *
   * Creates or reuses the shared DevTools view and attaches it to the page.
   */
  void onFrameDevToolsRequested(SplitFrameWidget *who, QWebEnginePage *page, const QPoint &pos);
  
  /**
   * @brief Creates and attaches the shared DevTools view to a page.
   * @param page The QWebEnginePage to inspect
   *
   * Creates a new DevTools window if needed, or reattaches the existing
   * DevTools to the new page. The DevTools window is shown as a Tool window.
   */
  void createAndAttachSharedDevToolsForPage(QWebEnginePage *page);
  
  /**
   * @brief Shows the DOM patches manager dialog.
   *
   * Creates a modeless DomPatchesDialog and shows it. When the dialog closes,
   * reapplies all patches to all frames.
   */
  void showDomPatchesManager();
  
  /**
   * @brief Updates the Window menu with all open windows.
   *
   * Rebuilds the menu to show all windows in g_windows with appropriate
   * indicators (checkmark for active, diamond for minimized).
   */
  void updateWindowMenu();

protected:
  /**
   * @brief Handles window close events.
   * @param event The close event
   *
   * Saves window state and handles cleanup. If this is the last window or
   * the app is shutting down, preserves state. Otherwise, removes the
   * window's saved group from QSettings.
   */
  void closeEvent(QCloseEvent *event) override;
  
  /**
   * @brief Handles window state change events.
   * @param event The change event
   *
   * Refreshes all Window menus when window state changes (minimized,
   * activated) to update the indicators.
   */
  void changeEvent(QEvent *event) override;

private:
  /**
   * @brief Converts a layout mode enum to a settings key string.
   * @param m The layout mode
   * @return String key: "vertical", "horizontal", or "grid"
   */
  static QString layoutModeKey(SplitWindow::LayoutMode m);
  
  /**
   * @brief Saves current splitter sizes to QSettings.
   *
   * Uses the default path "splitterSizes/<layoutMode>/<index>".
   */
  void saveCurrentSplitterSizes();
  
  /**
   * @brief Saves current splitter sizes to a specific settings path.
   * @param groupPrefix The QSettings group path prefix (e.g., "windows/id/splitterSizes")
   */
  void saveCurrentSplitterSizes(const QString &groupPrefix);
  
  /**
   * @brief Restores splitter sizes from QSettings.
   *
   * Uses the default path "splitterSizes/<layoutMode>/<index>".
   */
  void restoreSplitterSizes();
  
  /**
   * @brief Restores splitter sizes from a specific settings path.
   * @param groupPrefix The QSettings group path prefix
   */
  void restoreSplitterSizes(const QString &groupPrefix);

  QWidget *central_ = nullptr;              ///< Central widget containing the layout
  QVBoxLayout *layout_ = nullptr;           ///< Main vertical layout
  std::vector<QString> addresses_;          ///< URL addresses for each frame
  QWebEngineProfile *profile_ = nullptr;    ///< Shared web engine profile
  LayoutMode layoutMode_ = Vertical;        ///< Current layout mode
  std::vector<QSplitter*> currentSplitters_; ///< Active splitters for current layout
  QWebEngineView *sharedDevToolsView_ = nullptr; ///< Shared DevTools window
  bool restoredOnStartup_ = false;          ///< Whether state was restored on construction
  QString windowId_;                        ///< Unique ID for this window instance
  QMenu *windowMenu_ = nullptr;             ///< The Window menu for this window
};
