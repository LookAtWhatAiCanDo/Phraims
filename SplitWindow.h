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

// Debug helper: show per-window id in title for debugging.
extern bool DEBUG_SHOW_WINDOW_ID;

class SplitWindow : public QMainWindow {
  Q_OBJECT

public:
  enum LayoutMode { Vertical = 0, Horizontal = 1, Grid = 2 };

  // Accept an optional windowId (UUID). If provided, the window will
  // load/save its state under QSettings group "windows/<windowId>".
  SplitWindow(const QString &windowId = QString(), QWidget *parent = nullptr);

  /**
   * Persist this window's addresses, layout, geometry, state and splitter sizes
   * into QSettings under group "windows/<id>". If this window did not have
   * an id, a new one will be generated and used so the window will be
   * restorable on next launch.
   */
  void savePersistentStateToSettings();

  /**
   * Public wrapper to refresh the Window menu. Keeps updateWindowMenu() private.
   */
  void refreshWindowMenu();

  /**
   * Update this window's title to the form "Group X (N)" where X is the
   * 1-based index of this window in the global windows list and N is the
   * number of frames (sections) currently in the window.
   */
  void updateWindowTitle();

  /**
   * Set the address for the first frame (helper for createAndShowWindow)
   */
  void setFirstFrameAddress(const QString &address);

public slots:
  // Reset this window to a single empty section (used for New Window behavior)
  void resetToSingleEmptySection();

  /**
   * Focus the first frame's address QLineEdit so the user can start typing.
   */
  void focusFirstAddress();

private slots:
  void rebuildSections(int n);
  void toggleDevToolsForFocusedFrame();
  void onPlusFromFrame(SplitFrameWidget *who);
  void onUpFromFrame(SplitFrameWidget *who);
  void onDownFromFrame(SplitFrameWidget *who);
  void setLayoutMode(SplitWindow::LayoutMode m);
  void setHeightToScreen();
  void onMinusFromFrame(SplitFrameWidget *who);
  void onAddressEdited(SplitFrameWidget *who, const QString &text);
  void onFrameDevToolsRequested(SplitFrameWidget *who, QWebEnginePage *page, const QPoint &pos);
  void createAndAttachSharedDevToolsForPage(QWebEnginePage *page);
  void showDomPatchesManager();
  void updateWindowMenu();

protected:
  void closeEvent(QCloseEvent *event) override;
  void changeEvent(QEvent *event) override;

private:
  static QString layoutModeKey(SplitWindow::LayoutMode m);
  void saveCurrentSplitterSizes();
  void saveCurrentSplitterSizes(const QString &groupPrefix);
  void restoreSplitterSizes();
  void restoreSplitterSizes(const QString &groupPrefix);

  QWidget *central_ = nullptr;
  QVBoxLayout *layout_ = nullptr;
  std::vector<QString> addresses_;
  QWebEngineProfile *profile_ = nullptr;
  LayoutMode layoutMode_ = Vertical;
  std::vector<QSplitter*> currentSplitters_;
  QWebEngineView *sharedDevToolsView_ = nullptr;
  bool restoredOnStartup_ = false;
  QString windowId_;
  QMenu *windowMenu_ = nullptr;
};
