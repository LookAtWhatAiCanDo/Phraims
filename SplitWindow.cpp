#include "AppSettings.h"
#include "DomPatch.h"
#include "MyWebEnginePage.h"
#include "SplitFrameWidget.h"
#include "SplitterDoubleClickFilter.h"
#include "SplitWindow.h"
#include "Utils.h"
#include "version.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QInputDialog>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QScreen>
#include <QScrollArea>
#include <QSplitter>
#include <QStandardPaths>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QUuid>
#include <QVBoxLayout>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>

bool DEBUG_SHOW_WINDOW_ID = 0;

// Visual feedback constants for the frame addition flash effect
namespace {
  constexpr int FLASH_HANDLE_WIDTH_INCREASE = 4;  // pixels to increase splitter handle width
  constexpr int FLASH_DURATION_MS = 150;          // milliseconds to show the flash
  
  // About dialog dimensions
  constexpr int ABOUT_DIALOG_MIN_WIDTH = 400;     // minimum width for About dialog
  constexpr int ABOUT_DIALOG_MAX_HEIGHT = 300;    // maximum height for text browser in About dialog
}


SplitWindow::SplitWindow(const QString &windowId, bool isIncognito, QWidget *parent) 
    : QMainWindow(parent), windowId_(windowId), isIncognito_(isIncognito) {
  setWindowTitle(QCoreApplication::applicationName());
  resize(800, 600);

  AppSettings settings;

  connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) {
    QWidget *w = now;
    while (w) {
      if (auto *frame = qobject_cast<SplitFrameWidget *>(w)) {
        if (frame->window() == this) lastFocusedFrame_ = frame;
        return;
      }
      w = w->parentWidget();
    }
  });

  // File menu: New Window (Cmd/Ctrl+N), New Incognito Window (Shift+Cmd/Ctrl+N), New Frame (Cmd/Ctrl+T)
  auto *fileMenu = menuBar()->addMenu(tr("File"));
  QAction *newWindowAction = fileMenu->addAction(tr("New Window"));
  newWindowAction->setShortcut(QKeySequence::New);
  connect(newWindowAction, &QAction::triggered, this, [](bool){ createAndShowWindow(); });
  
  QAction *newIncognitoWindowAction = fileMenu->addAction(tr("New Incognito Window"));
  newIncognitoWindowAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::CTRL | Qt::Key_N));
  connect(newIncognitoWindowAction, &QAction::triggered, this, [](bool){ createAndShowIncognitoWindow(); });
  
  QAction *newFrameAction = fileMenu->addAction(tr("New Frame"));
  newFrameAction->setShortcut(QKeySequence::AddTab);  // Command-T on macOS, Ctrl+T elsewhere
  connect(newFrameAction, &QAction::triggered, this, &SplitWindow::onNewFrameShortcut);

  // No global toolbar; per-frame + / - buttons control sections.

  // Load the profile for this window
  if (isIncognito_) {
    // Incognito windows use a new off-the-record profile
    currentProfileName_ = QString();  // No profile name for Incognito
    profile_ = createIncognitoProfile();
    qDebug() << "SplitWindow: using Incognito profile" << profile_
             << "offTheRecord=" << profile_->isOffTheRecord();
  } else if (!windowId_.isEmpty()) {
    // Normal window with saved state: load profile from settings
    AppSettings s;
    GroupScope _gs(s, QStringLiteral("windows/%1").arg(windowId_));
    currentProfileName_ = s->value("profileName", currentProfileName()).toString();
    profile_ = getProfileByName(currentProfileName_);
    qDebug() << "SplitWindow: using profile" << currentProfileName_ << profile_
             << "storage=" << profile_->persistentStoragePath();
  } else {
    // New normal window: use current global profile
    currentProfileName_ = currentProfileName();
    profile_ = getProfileByName(currentProfileName_);
    qDebug() << "SplitWindow: using profile" << currentProfileName_ << profile_
             << "storage=" << profile_->persistentStoragePath();
  }

  // (window geometry/state restored later after UI is built)

  // add a simple View menu with a helper to set the window height to the
  // screen available height (preserves width and x position)
  auto *viewMenu = menuBar()->addMenu(tr("View"));
  QAction *setHeightAction = viewMenu->addAction(tr("Set height to screen"));
  connect(setHeightAction, &QAction::triggered, this, &SplitWindow::setHeightToScreen);
  QAction *toggleDevToolsAction = viewMenu->addAction(tr("Toggle DevTools"));
  toggleDevToolsAction->setShortcut(QKeySequence(Qt::Key_F12));
  connect(toggleDevToolsAction, &QAction::triggered, this, &SplitWindow::toggleDevToolsForFocusedFrame);

  QAction *reloadFrameAction = viewMenu->addAction(tr("Reload Frame"));
  reloadFrameAction->setShortcut(QKeySequence::Refresh);
  reloadFrameAction->setShortcutContext(Qt::WindowShortcut);
  connect(reloadFrameAction, &QAction::triggered, this, &SplitWindow::reloadFocusedFrame);

  QAction *reloadBypassAction = viewMenu->addAction(tr("Reload Frame (Bypass Cache)"));
  reloadBypassAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R));
  reloadBypassAction->setShortcutContext(Qt::WindowShortcut);
  connect(reloadBypassAction, &QAction::triggered, this, &SplitWindow::reloadFocusedFrameBypassingCache);

  viewMenu->addSeparator();
  QAction *increaseScaleAction = viewMenu->addAction(tr("Increase Frame Scale"));
  connect(increaseScaleAction, &QAction::triggered, this, &SplitWindow::increaseFocusedFrameScale);
  QAction *decreaseScaleAction = viewMenu->addAction(tr("Decrease Frame Scale"));
  connect(decreaseScaleAction, &QAction::triggered, this, &SplitWindow::decreaseFocusedFrameScale);
  QAction *resetScaleAction = viewMenu->addAction(tr("Reset Frame Scale"));
  connect(resetScaleAction, &QAction::triggered, this, &SplitWindow::resetFocusedFrameScale);

  // Always-on-top toggle
  QAction *alwaysOnTopAction = viewMenu->addAction(tr("Always on Top"));
  alwaysOnTopAction->setCheckable(true);
  // read persisted value (default: false)
  {
    const bool on = settings->value("alwaysOnTop", false).toBool();
    alwaysOnTopAction->setChecked(on);
    // apply the window flag; setWindowFlag requires a show() to take effect on some platforms
    setWindowFlag(Qt::WindowStaysOnTopHint, on);
    if (on) show();
  }
  connect(alwaysOnTopAction, &QAction::toggled, this, [this](bool checked){
    setWindowFlag(Qt::WindowStaysOnTopHint, checked);
    if (checked) show();
    AppSettings s;
    s->setValue("alwaysOnTop", checked);
  });

  // Layout menu: Grid, Stack Vertically, Stack Horizontally
  auto *layoutMenu = menuBar()->addMenu(tr("Layout"));
  QActionGroup *layoutGroup = new QActionGroup(this);
  layoutGroup->setExclusive(true);
  QAction *gridAction = layoutMenu->addAction(tr("Grid"));
  gridAction->setCheckable(true);
  layoutGroup->addAction(gridAction);
  QAction *verticalAction = layoutMenu->addAction(tr("Stack Vertically"));
  verticalAction->setCheckable(true);
  layoutGroup->addAction(verticalAction);
  QAction *horizontalAction = layoutMenu->addAction(tr("Stack Horizontally"));
  horizontalAction->setCheckable(true);
  layoutGroup->addAction(horizontalAction);

  // restore persisted layout choice
  int storedMode = settings->value("layoutMode", (int)Vertical).toInt();
  layoutMode_ = (LayoutMode)storedMode;
  switch (layoutMode_) {
    case Grid: gridAction->setChecked(true); break;
    case Horizontal: horizontalAction->setChecked(true); break;
    case Vertical: default: verticalAction->setChecked(true); break;
  }

  connect(gridAction, &QAction::triggered, this, [this]() { setLayoutMode(Grid); });
  connect(verticalAction, &QAction::triggered, this, [this]() { setLayoutMode(Vertical); });
  connect(horizontalAction, &QAction::triggered, this, [this]() { setLayoutMode(Horizontal); });

  // Tools menu: DOM patches manager
  auto *toolsMenu = menuBar()->addMenu(tr("Tools"));
  QAction *domPatchesAction = toolsMenu->addAction(tr("DOM Patches"));
  connect(domPatchesAction, &QAction::triggered, this, &SplitWindow::showDomPatchesManager);

  // Profiles menu: manage browser profiles (not available in Incognito mode)
  if (!isIncognito_) {
    profilesMenu_ = menuBar()->addMenu(tr("Profiles"));
    QAction *newProfileAction = profilesMenu_->addAction(tr("New Profile..."));
    connect(newProfileAction, &QAction::triggered, this, &SplitWindow::createNewProfile);
    
    QAction *renameProfileAction = profilesMenu_->addAction(tr("Rename Profile..."));
    connect(renameProfileAction, &QAction::triggered, this, &SplitWindow::renameCurrentProfile);
    
    QAction *deleteProfileAction = profilesMenu_->addAction(tr("Delete Profile..."));
    connect(deleteProfileAction, &QAction::triggered, this, &SplitWindow::deleteSelectedProfile);
    
    profilesMenu_->addSeparator();
    
#ifndef NDEBUG
    // Debug builds only: Add menu item to open profiles folder
    QAction *openProfilesFolderAction = profilesMenu_->addAction(tr("Open Profiles Folder"));
    connect(openProfilesFolderAction, &QAction::triggered, this, [this]() {
      const QString dataRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
      const QString profilesDir = dataRoot + QStringLiteral("/profiles");
      // Ensure the profiles directory exists before trying to open it
      QDir().mkpath(profilesDir);
      QDesktopServices::openUrl(QUrl::fromLocalFile(profilesDir));
    });
    
    profilesMenu_->addSeparator();
#endif
    
    // Profile list will be populated by updateProfilesMenu()
  }

  // Window menu: per-macOS convention
  windowMenu_ = menuBar()->addMenu(tr("Window"));
  // Add standard close/minimize actions
  QAction *minimizeAct = windowMenu_->addAction(tr("Minimize"));
  minimizeAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));
  connect(minimizeAct, &QAction::triggered, this, &QWidget::showMinimized);
  QAction *closeAct = windowMenu_->addAction(tr("Close Window"));
  closeAct->setShortcut(QKeySequence::Close);
  connect(closeAct, &QAction::triggered, this, &SplitWindow::onCloseShortcut);
  windowMenu_->addSeparator();

  // Help menu: About dialog
  auto *helpMenu = menuBar()->addMenu(tr("Help"));
  QAction *aboutAction = helpMenu->addAction(tr("About Phraims"));
  connect(aboutAction, &QAction::triggered, this, &SplitWindow::showAboutDialog);

  // central scroll area to allow many sections
  auto *scroll = new QScrollArea();
  scroll->setWidgetResizable(true);
  central_ = new QWidget();
  scroll->setWidget(central_);
  setCentralWidget(scroll);

  layout_ = new QVBoxLayout(central_);
  layout_->setContentsMargins(4, 4, 4, 4);
  layout_->setSpacing(6);

  auto loadFrameState = [this](const QStringList &addresses, const QVariantList &scales) {
    frames_.clear();
    if (addresses.isEmpty()) {
      frames_.push_back(FrameState());
    } else {
      frames_.reserve(addresses.size());
      for (const QString &addr : addresses) {
        FrameState state;
        state.address = addr;
        frames_.push_back(state);
      }
    }
    if (frames_.empty()) frames_.push_back(FrameState());
    for (int i = 0; i < (int)frames_.size(); ++i) {
      double value = (i < scales.size()) ? scales[i].toDouble() : 1.0;
      frames_[i].scale = std::clamp(value, SplitFrameWidget::kMinScaleFactor, SplitFrameWidget::kMaxScaleFactor);
    }
  };

  // load persisted addresses (per-window if windowId_ present and not Incognito, otherwise global)
  if (isIncognito_) {
    // Incognito windows always start with a single empty frame
    loadFrameState(QStringList(), QVariantList());
  } else if (!windowId_.isEmpty()) {
    AppSettings s;
    {
      GroupScope _gs(s, QStringLiteral("windows/%1").arg(windowId_));
      const QStringList savedAddresses = s->value("addresses").toStringList();
      const QVariantList savedScales = s->value("frameScales").toList();
      loadFrameState(savedAddresses, savedScales);
      layoutMode_ = (LayoutMode)s->value("layoutMode", (int)layoutMode_).toInt();
    }
  } else {
    const QStringList savedAddresses = settings->value("addresses").toStringList();
    const QVariantList savedScales = settings->value("frameScales").toList();
    loadFrameState(savedAddresses, savedScales);
  }
  // build initial UI
  rebuildSections((int)frames_.size());
  // restore splitter sizes only once at startup (subsequent layout
  // selections/rebuilds should reset splitters to defaults)
  // Incognito windows skip splitter size restoration
  if (!isIncognito_) {
    if (!windowId_.isEmpty()) {
      restoreSplitterSizes(QStringLiteral("windows/%1/splitterSizes").arg(windowId_));
    } else {
      restoreSplitterSizes();
    }
  }
  restoredOnStartup_ = true;

  // restore saved window geometry and window state (position/size/state)
  // Incognito windows skip geometry restoration
  if (!isIncognito_) {
    if (!windowId_.isEmpty()) {
      AppSettings s;
      {
        GroupScope _gs(s, QStringLiteral("windows/%1").arg(windowId_));
        const QByteArray savedGeom = s->value("windowGeometry").toByteArray();
        if (!savedGeom.isEmpty()) restoreGeometry(savedGeom);
        const QByteArray savedState = s->value("windowState").toByteArray();
        if (!savedState.isEmpty()) restoreState(savedState);
      }
    } else {
      const QByteArray savedGeom = settings->value("windowGeometry").toByteArray();
      if (!savedGeom.isEmpty()) restoreGeometry(savedGeom);
      const QByteArray savedState = settings->value("windowState").toByteArray();
      if (!savedState.isEmpty()) restoreState(savedState);
    }
  }
  
  // Initialize the Profiles menu with the current profile list
  updateProfilesMenu();
}

void SplitWindow::savePersistentStateToSettings() {
  // Incognito windows should never persist state
  if (isIncognito_) {
    qDebug() << "savePersistentStateToSettings: skipping save for Incognito window";
    return;
  }
  
  AppSettings s;
  QString id = windowId_;
  if (id.isEmpty()) id = QUuid::createUuid().toString();
  qDebug() << "savePersistentStateToSettings: saving window id=" << id << " addresses.count=" << frames_.size() << " layoutMode=" << (int)layoutMode_ << " profile=" << currentProfileName_;
  {
    GroupScope _gs(s, QStringLiteral("windows/%1").arg(id));
    QStringList addressList;
    QVariantList scaleList;
    for (const auto &state : frames_) {
      addressList << state.address;
      scaleList << state.scale;
    }
    s->setValue("addresses", addressList);
    s->setValue("frameScales", scaleList);
    s->setValue("profileName", currentProfileName_);
    s->setValue("layoutMode", (int)layoutMode_);
    s->setValue("windowGeometry", saveGeometry());
    s->setValue("windowState", saveState());
  }
  s->sync();
  // persist splitter sizes under windows/<id>/splitterSizes/<index>
  saveCurrentSplitterSizes(QStringLiteral("windows/%1/splitterSizes").arg(id));
}

void SplitWindow::resetToSingleEmptySection() {
  frames_.clear();
  frames_.push_back(FrameState());
  rebuildSections(1);
  // Do not persist immediately; keep in-memory until user changes or window closes.
  // After rebuilding, focus the address field so the user can start typing
  // immediately. Use a queued invoke so focus is set after layout/stacking
  // completes.
  QMetaObject::invokeMethod(this, "focusFirstAddress", Qt::QueuedConnection);
}

void SplitWindow::refreshWindowMenu() { updateWindowMenu(); }

void SplitWindow::focusFirstAddress() {
  if (!central_) return;
  // Find the first SplitFrameWidget and its QLineEdit child
  SplitFrameWidget *frame = central_->findChild<SplitFrameWidget *>();
  if (!frame) return;
  QLineEdit *le = frame->findChild<QLineEdit *>();
  if (!le) return;
  le->setFocus(Qt::OtherFocusReason);
  // select all so typing replaces existing content
  le->selectAll();
}

void SplitWindow::updateWindowTitle() {
  // Determine 1-based index in g_windows
  int idx = 0;
  for (size_t i = 0; i < g_windows.size(); ++i) {
    if (g_windows[i] == this) { idx = (int)i + 1; break; }
  }
  const int count = (int)frames_.size();
  QString profileDisplay = isIncognito_ ? QStringLiteral("Incognito") : currentProfileName_;
  QString title = QStringLiteral("Group %1 (%2) - %3").arg(idx).arg(count).arg(profileDisplay);
  if (DEBUG_SHOW_WINDOW_ID && !windowId_.isEmpty()) {
      title += QStringLiteral(" [%1]").arg(windowId_);
  }
  setWindowTitle(title);
}

void SplitWindow::setFirstFrameAddress(const QString &address) {
  SplitFrameWidget *frame = central_ ? central_->findChild<SplitFrameWidget *>() : nullptr;
  if (frame) frame->setAddress(address);
}

void SplitWindow::rebuildSections(int n) {

  // Ensure frames_ vector matches requested size, preserving existing values.
  if ((int)frames_.size() != n) {
    frames_.resize(n);
  }
  // clamp n
  if (n < 1) n = 1;

  // clear existing items
  QLayoutItem *child;
  while ((child = layout_->takeAt(0)) != nullptr) {
    if (auto *w = child->widget()) {
      w->hide();
      w->deleteLater();
    }
    delete child;
  }

  // old splitters are going away; clear tracking vector so we start fresh
  currentSplitters_.clear();

  // create n sections according to the selected layout mode.
  QWidget *container = nullptr;
  if (layoutMode_ == Vertical || layoutMode_ == Horizontal) {
    QSplitter *split = new QSplitter(layoutMode_ == Vertical ? Qt::Vertical : Qt::Horizontal);
    // track this splitter for state persistence
    currentSplitters_.push_back(split);
    for (int i = 0; i < n; ++i) {
      auto *frame = new SplitFrameWidget(i);
      // logicalIndex property used for mapping frame -> frames_ index
      frame->setProperty("logicalIndex", i);
      frame->setProfile(profile_);
      frame->setScaleFactor(frames_[i].scale);
      frame->setAddress(frames_[i].address);
      connect(frame, &SplitFrameWidget::plusClicked, this, &SplitWindow::onPlusFromFrame);
      connect(frame, &SplitFrameWidget::minusClicked, this, &SplitWindow::onMinusFromFrame);
      connect(frame, &SplitFrameWidget::addressEdited, this, &SplitWindow::onAddressEdited);
      connect(frame, &SplitFrameWidget::upClicked, this, &SplitWindow::onUpFromFrame);
      connect(frame, &SplitFrameWidget::downClicked, this, &SplitWindow::onDownFromFrame);
      connect(frame, &SplitFrameWidget::devToolsRequested, this, &SplitWindow::onFrameDevToolsRequested);
      connect(frame, &SplitFrameWidget::translateRequested, this, &SplitWindow::onFrameTranslateRequested);
      connect(frame, &SplitFrameWidget::scaleChanged, this, &SplitWindow::onFrameScaleChanged);
      connect(frame, &SplitFrameWidget::interactionOccurred, this, &SplitWindow::onFrameInteraction);
      connect(frame, &QObject::destroyed, this, [this, frame]() {
        if (lastFocusedFrame_ == frame) lastFocusedFrame_ = nullptr;
      });
      frame->setMinusEnabled(n > 1);
      frame->setUpEnabled(i > 0);
      frame->setDownEnabled(i < n - 1);
      qDebug() << "";
      split->addWidget(frame);
    }
    // distribute sizes evenly across the children so switching layouts
    // starts with a balanced view
    if (n > 0) {
      QList<int> sizes;
      for (int i = 0; i < n; ++i) sizes << 1;
      split->setSizes(sizes);
    }
    // Install double-click handler for equal sizing after widgets/handles exist
    {
      SplitterDoubleClickFilter *filter = new SplitterDoubleClickFilter(split, this);
      connect(filter, &SplitterDoubleClickFilter::splitterResized, this, &SplitWindow::onSplitterDoubleClickResized);
    }
    container = split;
  } else { // Grid mode: nested splitters for resizable grid
    // Create a vertical splitter containing one horizontal splitter per row.
    QSplitter *outer = new QSplitter(Qt::Vertical);
    currentSplitters_.push_back(outer);
    int rows = (int)std::ceil(std::sqrt((double)n));
    int cols = (n + rows - 1) / rows;
    int idx = 0;
    for (int r = 0; r < rows; ++r) {
      // how many items in this row
      int itemsInRow = std::min(cols, n - idx);
      if (itemsInRow <= 0) break;
      QSplitter *rowSplit = new QSplitter(Qt::Horizontal);
      currentSplitters_.push_back(rowSplit);
      for (int c = 0; c < itemsInRow; ++c) {
        auto *frame = new SplitFrameWidget(idx);
        // logicalIndex property used for mapping frame -> frames_ index
        frame->setProperty("logicalIndex", idx);
        frame->setProfile(profile_);
        frame->setScaleFactor(frames_[idx].scale);
        frame->setAddress(frames_[idx].address);
        connect(frame, &SplitFrameWidget::plusClicked, this, &SplitWindow::onPlusFromFrame);
        connect(frame, &SplitFrameWidget::minusClicked, this, &SplitWindow::onMinusFromFrame);
        connect(frame, &SplitFrameWidget::addressEdited, this, &SplitWindow::onAddressEdited);
        connect(frame, &SplitFrameWidget::upClicked, this, &SplitWindow::onUpFromFrame);
        connect(frame, &SplitFrameWidget::downClicked, this, &SplitWindow::onDownFromFrame);
        connect(frame, &SplitFrameWidget::devToolsRequested, this, &SplitWindow::onFrameDevToolsRequested);
        connect(frame, &SplitFrameWidget::translateRequested, this, &SplitWindow::onFrameTranslateRequested);
        connect(frame, &SplitFrameWidget::scaleChanged, this, &SplitWindow::onFrameScaleChanged);
        connect(frame, &SplitFrameWidget::interactionOccurred, this, &SplitWindow::onFrameInteraction);
        connect(frame, &QObject::destroyed, this, [this, frame]() {
          if (lastFocusedFrame_ == frame) lastFocusedFrame_ = nullptr;
        });
        frame->setMinusEnabled(n > 1);
        frame->setUpEnabled(idx > 0);
        frame->setDownEnabled(idx < n - 1);
        qDebug() << "";
        rowSplit->addWidget(frame);
        ++idx;
      }
      // evenly distribute columns in this row
      if (itemsInRow > 0) {
        QList<int> colSizes;
        for (int i = 0; i < itemsInRow; ++i) colSizes << 1;
        rowSplit->setSizes(colSizes);
        // Install rowFilter now that the rowSplit and its handles exist
        {
          SplitterDoubleClickFilter *rowFilter = new SplitterDoubleClickFilter(rowSplit, this);
          connect(rowFilter, &SplitterDoubleClickFilter::splitterResized, this, &SplitWindow::onSplitterDoubleClickResized);
        }
      }
      outer->addWidget(rowSplit);
    }
    // evenly distribute rows in the outer splitter
    int actualRows = outer->count();
    if (actualRows > 0) {
      QList<int> rowSizes;
      for (int i = 0; i < actualRows; ++i) rowSizes << 1;
      outer->setSizes(rowSizes);
    }
    // Install outerFilter now that outer and its handles exist
    {
      SplitterDoubleClickFilter *outerFilter = new SplitterDoubleClickFilter(outer, this);
      connect(outerFilter, &SplitterDoubleClickFilter::splitterResized, this, &SplitWindow::onSplitterDoubleClickResized);
    }
    container = outer;
  }

  if (container) {
    layout_->addWidget(container, 1);
  }

  // add a final stretch with zero so that widgets entirely control spacing
  layout_->addStretch(0);
  central_->update();
  if (!lastFocusedFrame_) lastFocusedFrame_ = firstFrameWidget();
  // Update this window's title now that the number of frames may have changed
  // and ensure the Window menus across the app reflect the new title.
  updateWindowTitle();
  rebuildAllWindowMenus();
}

void SplitWindow::toggleDevToolsForFocusedFrame() {
  // If the shared DevTools view is open, hide it. Otherwise attach it to
  // the focused frame's page (or the first frame) and show it.
  if (sharedDevToolsView_ && sharedDevToolsView_->isVisible()) {
    sharedDevToolsView_->hide();
    return;
  }

  SplitFrameWidget *target = focusedFrameOrFirst();
  if (target) {
    QWebEnginePage *p = target->page();
    if (p) {
      createAndAttachSharedDevToolsForPage(p);
      if (sharedDevToolsView_) {
        sharedDevToolsView_->show();
        sharedDevToolsView_->raise();
        sharedDevToolsView_->activateWindow();
      }
    }
  }
}

void SplitWindow::onNewFrameShortcut() {
  // Find the currently focused frame (similar to toggleDevToolsForFocusedFrame)
  SplitFrameWidget *target = focusedFrameOrFirst();
  
  if (!target) {
    qDebug() << "onNewFrameShortcut: no target frame found";
    return;
  }

  const int pos = frameIndexFor(target);
  if (pos < 0) {
    qDebug() << "onNewFrameShortcut: target has no logicalIndex property";
    return;
  }
  
  // Insert a new empty frame after the focused frame
  frames_.insert(frames_.begin() + pos + 1, FrameState());
  
  // Persist addresses and scale defaults
  persistGlobalFrameState();
  
  // Rebuild UI with the updated state
  rebuildSections((int)frames_.size());
  
  // Provide a visual cue by briefly flashing the divider handle
  // Find the splitter that contains the newly added frame
  if (!currentSplitters_.empty()) {
    for (QSplitter *splitter : currentSplitters_) {
      if (!splitter) continue;
      // Flash effect: briefly change the handle width to make it visible
      // Use QPointer to ensure splitter is still valid when timer fires
      QPointer<QSplitter> splitterGuard(splitter);
      QTimer::singleShot(0, this, [splitterGuard]() {
        if (!splitterGuard) return;
        const int origWidth = splitterGuard->handleWidth();
        splitterGuard->setHandleWidth(origWidth + FLASH_HANDLE_WIDTH_INCREASE);
        QTimer::singleShot(FLASH_DURATION_MS, [splitterGuard, origWidth]() {
          if (splitterGuard) {
            splitterGuard->setHandleWidth(origWidth);
          }
        });
      });
    }
  }
  
  qDebug() << "onNewFrameShortcut: added new frame after position" << pos;
}

void SplitWindow::reloadFocusedFrame() {
  SplitFrameWidget *target = focusedFrameOrFirst();
  if (!target) return;
  target->reload(false);
}

void SplitWindow::reloadFocusedFrameBypassingCache() {
  SplitFrameWidget *target = focusedFrameOrFirst();
  if (!target) return;
  target->reload(true);
}

void SplitWindow::onPlusFromFrame(SplitFrameWidget *who) {
  // use logicalIndex property assigned during rebuildSections
  const QVariant v = who->property("logicalIndex");
  if (!v.isValid()) return;
  int pos = v.toInt();
  frames_.insert(frames_.begin() + pos + 1, FrameState());
  // persist addresses and scale defaults
  persistGlobalFrameState();
  // rebuild UI with the updated frames_
  rebuildSections((int)frames_.size());
  // Focus the newly added frame's address bar. The new frame is at index pos+1.
  // Use a queued connection to ensure focus is set after the layout has fully
  // updated and all widgets are visible.
  const int newFrameIndex = pos + 1;
  QMetaObject::invokeMethod(this, [this, newFrameIndex]() {
    if (!central_) return;
    // Find all SplitFrameWidget children and locate the one with logicalIndex == newFrameIndex
    const QList<SplitFrameWidget *> frames = central_->findChildren<SplitFrameWidget *>();
    for (SplitFrameWidget *frame : frames) {
      if (frame->property("logicalIndex").toInt() == newFrameIndex) {
        frame->focusAddress();
        break;
      }
    }
  }, Qt::QueuedConnection);
}


void SplitWindow::onUpFromFrame(SplitFrameWidget *who) {
  // move this frame up (towards index 0)
  const QVariant v = who->property("logicalIndex");
  if (!v.isValid()) return;
  int pos = v.toInt();
  if (pos <= 0) return; // already at top or not found
  std::swap(frames_[pos], frames_[pos - 1]);
  persistGlobalFrameState();
  rebuildSections((int)frames_.size());
}

void SplitWindow::onDownFromFrame(SplitFrameWidget *who) {
  // move this frame down (towards larger indices)
  const QVariant v = who->property("logicalIndex");
  if (!v.isValid()) return;
  int pos = v.toInt();
  if (pos < 0 || pos >= (int)frames_.size() - 1) return; // at bottom or not found
  std::swap(frames_[pos], frames_[pos + 1]);
  persistGlobalFrameState();
  rebuildSections((int)frames_.size());
}

void SplitWindow::setLayoutMode(SplitWindow::LayoutMode m) {
  AppSettings settings;

  // If the user re-selects the already-selected layout, treat that as a
  // request to reset splitters to their default sizes. Clear any saved
  // sizes for this layout and rebuild without saving the current sizes.
  if (m == layoutMode_) {
    const QString base = QStringLiteral("splitterSizes/%1").arg(layoutModeKey(layoutMode_));
    settings->remove(base);
    // rebuild so splitters are reset to defaults
    rebuildSections((int)frames_.size());
    return;
  }

  // Note: we do not save splitter sizes during runtime; sizes are only
  // persisted on application exit. When switching layouts we clear any
  // saved sizes for the target layout so the new layout starts with
  // default splitter positions.
  // Remove any previously-saved sizes for the new target layout so that
  // switching layouts starts with default splitter positions rather than
  // restoring an older saved configuration for that layout.
  {
    const QString targetBase = QStringLiteral("splitterSizes/%1").arg(layoutModeKey(m));
    settings->remove(targetBase);
  }
  // Apply the new layout mode and persist it.
  layoutMode_ = m;
  settings->setValue("layoutMode", (int)layoutMode_);
  // Rebuild UI for the new layout (splitter sizes are only restored at startup)
  rebuildSections((int)frames_.size());
}

void SplitWindow::setHeightToScreen() {
  QScreen *screen = QGuiApplication::primaryScreen();
  if (!screen) return;
  const QRect avail = screen->availableGeometry();
  // preserve current x and width, set y to top of available area and
  // height to available height
  const QRect geom = geometry();
  const int x = geom.x();
  const int w = geom.width();
  setGeometry(x, avail.y(), w, avail.height());
}

void SplitWindow::onMinusFromFrame(SplitFrameWidget *who) {
  if (frames_.size() <= 1) return; // shouldn't remove last

  const QVariant v = who->property("logicalIndex");
  if (!v.isValid()) return;
  int pos = v.toInt();
  if (pos < 0) return;

  // confirm with the user before removing
  const QMessageBox::StandardButton reply = QMessageBox::question(
    this, tr("Remove section"), tr("Remove this section?"),
    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (reply != QMessageBox::Yes) return;

  // Remove the frame surgically without rebuilding all frames
  removeSingleFrame(who);
}

void SplitWindow::removeSingleFrame(SplitFrameWidget *frameToRemove) {
  if (!frameToRemove) return;
  
  const QVariant v = frameToRemove->property("logicalIndex");
  if (!v.isValid()) return;
  const int removedIndex = v.toInt();
  if (removedIndex < 0 || removedIndex >= (int)frames_.size()) return;
  
  // Remove the frame from the data model
  frames_.erase(frames_.begin() + removedIndex);
  
  // Persist the updated frame state
  persistGlobalFrameState();
  
  // Find all remaining frames and update their logical indices
  const QList<SplitFrameWidget *> allFrames = central_->findChildren<SplitFrameWidget *>();
  QList<SplitFrameWidget *> remainingFrames;
  for (SplitFrameWidget *frame : allFrames) {
    if (frame != frameToRemove) {
      remainingFrames.append(frame);
    }
  }
  
  // Sort remaining frames by their current logical index
  std::sort(remainingFrames.begin(), remainingFrames.end(), [](SplitFrameWidget *a, SplitFrameWidget *b) {
    const int idxA = a->property("logicalIndex").toInt();
    const int idxB = b->property("logicalIndex").toInt();
    return idxA < idxB;
  });
  
  // Update logical indices for frames after the removed one
  for (int i = 0; i < remainingFrames.size(); ++i) {
    SplitFrameWidget *frame = remainingFrames[i];
    const int oldIndex = frame->property("logicalIndex").toInt();
    
    // If this frame was after the removed one, decrement its index
    if (oldIndex > removedIndex) {
      frame->setProperty("logicalIndex", oldIndex - 1);
    }
  }
  
  // Update button states for all remaining frames
  const int totalFrames = (int)frames_.size();
  for (int i = 0; i < remainingFrames.size(); ++i) {
    SplitFrameWidget *frame = remainingFrames[i];
    const int idx = frame->property("logicalIndex").toInt();
    
    frame->setMinusEnabled(totalFrames > 1);
    frame->setUpEnabled(idx > 0);
    frame->setDownEnabled(idx < totalFrames - 1);
  }
  
  // Remove the frame widget from the UI
  frameToRemove->hide();
  frameToRemove->deleteLater();
  
  // Clear the last focused frame if it's being removed
  if (lastFocusedFrame_ == frameToRemove) {
    lastFocusedFrame_ = nullptr;
  }
  
  // Update window title and menus
  updateWindowTitle();
  rebuildAllWindowMenus();
}

void SplitWindow::onAddressEdited(SplitFrameWidget *who, const QString &text) {
  const QVariant v = who->property("logicalIndex");
  if (!v.isValid()) return;
  int pos = v.toInt();
  if (pos < 0) return;
  if (pos < (int)frames_.size()) {
    frames_[pos].address = text;
    // persist addresses list
    persistGlobalFrameState();
  }
}

void SplitWindow::onFrameScaleChanged(SplitFrameWidget *who, double scale) {
  const int pos = frameIndexFor(who);
  if (pos < 0 || pos >= (int)frames_.size()) return;
  frames_[pos].scale = std::clamp(scale, SplitFrameWidget::kMinScaleFactor, SplitFrameWidget::kMaxScaleFactor);
  persistGlobalFrameState();
}

void SplitWindow::closeEvent(QCloseEvent *event) {
  // Incognito windows should never persist state
  if (isIncognito_) {
    qDebug() << "SplitWindow::closeEvent: Incognito window - skipping all persistence";
    rebuildAllWindowMenus();
    QMainWindow::closeEvent(event);
    return;
  }
  
  // Persist splitter sizes and either save or remove per-window restore data.
  // If this window has a persistent windowId_ it means it was part of the
  // saved session; when the user explicitly closes the window we remove
  // that saved group so the window will NOT be restored on next launch.
  if (!windowId_.isEmpty()) {
    // If the application is shutting down, persist this window's state so
    // it will be restored on next launch. If the user explicitly closed
    // the window during a running session, remove its saved group so it
    // does not get restored.
    if (qApp && qApp->closingDown()) {
      // During shutdown: save (do not remove) so session is preserved.
      AppSettings s;
      {
        GroupScope _gs(s, QStringLiteral("windows/%1").arg(windowId_));
        QStringList addressList;
        QVariantList scaleList;
        for (const auto &state : frames_) {
          addressList << state.address;
          scaleList << state.scale;
        }
        s->setValue("addresses", addressList);
        s->setValue("frameScales", scaleList);
        s->setValue("layoutMode", (int)layoutMode_);
        s->setValue("windowGeometry", saveGeometry());
        s->setValue("windowState", saveState());
      }
      // Ensure these shutdown-time writes are flushed to the backend.
      s->sync();
      saveCurrentSplitterSizes(QStringLiteral("windows/%1/splitterSizes").arg(windowId_));
    } else {
      // If other windows exist, remove this window's saved group now.
      // If this is the last window, preserve the saved group so it
      // reopens on next launch.
      saveCurrentSplitterSizes(QStringLiteral("windows/%1/splitterSizes").arg(windowId_));
      const size_t windowsCount = g_windows.size();
      qDebug() << "SplitWindow::closeEvent: g_windows.count (including this)=" << windowsCount;
      if (windowsCount > 1) {
        AppSettings s;
        s->beginGroup(QStringLiteral("windows"));
        const QStringList before = s->childGroups();
        if (before.contains(windowId_)) {
          qDebug() << "SplitWindow::closeEvent: removing stored group for" << windowId_;
          s->remove(windowId_);
          s->sync();
        } else {
          qDebug() << "SplitWindow::closeEvent: no stored group for" << windowId_;
        }
        s->endGroup();
      } else {
        qDebug() << "SplitWindow::closeEvent: single window or quitting; preserving stored group for" << windowId_;
      }

      // Schedule deletion; the destroyed() handler will prune g_windows
      // and update menus.
      this->deleteLater();
    }
  } else {
    // no per-window id: persist as legacy/global keys
    saveCurrentSplitterSizes();
    AppSettings settings;
    QStringList addressList;
    QVariantList scaleList;
    for (const auto &state : frames_) {
      addressList << state.address;
      scaleList << state.scale;
    }
    settings->setValue("addresses", addressList);
    settings->setValue("frameScales", scaleList);
    // persist window geometry
    settings->setValue("windowGeometry", saveGeometry());
    // persist window state (toolbars/dock state and maximized/minimized state)
    settings->setValue("windowState", saveState());
  }
  // Refresh all Window menus immediately when this window is closed
  // so other windows reflect the removal without waiting for object
  // destruction. This keeps the Window menu in sync across the app.
  rebuildAllWindowMenus();
  QMainWindow::closeEvent(event);
}

QString SplitWindow::layoutModeKey(SplitWindow::LayoutMode m) {
  switch (m) {
    case Vertical: return QStringLiteral("vertical");
    case Horizontal: return QStringLiteral("horizontal");
    case Grid: default: return QStringLiteral("grid");
  }
}

void SplitWindow::persistGlobalFrameState() {
  AppSettings settings;
  QStringList addresses;
  QVariantList scales;
  addresses.reserve((int)frames_.size());
  scales.reserve((int)frames_.size());
  for (const auto &state : frames_) {
    addresses << state.address;
    scales << state.scale;
  }
  settings->setValue("addresses", addresses);
  settings->setValue("frameScales", scales);
}

int SplitWindow::frameIndexFor(SplitFrameWidget *frame) const {
  if (!frame) return -1;
  const QVariant v = frame->property("logicalIndex");
  if (!v.isValid()) return -1;
  return v.toInt();
}

void SplitWindow::saveCurrentSplitterSizes() {
  saveCurrentSplitterSizes(QString());
}

void SplitWindow::saveCurrentSplitterSizes(const QString &groupPrefix) {
  if (currentSplitters_.empty()) return;
  AppSettings settings;
  // If no groupPrefix provided, store under splitterSizes/<layout>/<index>
  if (groupPrefix.isEmpty()) {
    settings->beginGroup(QStringLiteral("splitterSizes"));
    settings->beginGroup(layoutModeKey(layoutMode_));
    for (int i = 0; i < (int)currentSplitters_.size(); ++i) {
      QSplitter *s = currentSplitters_[i];
      if (!s) continue;
      const QList<int> sizes = s->sizes();
      QVariantList vl;
      for (int v : sizes) vl << v;
      settings->setValue(QString::number(i), vl);
    }
    settings->endGroup();
    settings->endGroup();
  } else {
    // Create nested groups for the provided prefix (e.g., windows/<id>/splitterSizes)
    {
      GroupScope _gs(settings, groupPrefix);
      settings->beginGroup(layoutModeKey(layoutMode_));
      for (int i = 0; i < (int)currentSplitters_.size(); ++i) {
        QSplitter *s = currentSplitters_[i];
        if (!s) continue;
        const QList<int> sizes = s->sizes();
        QVariantList vl;
        for (int v : sizes) vl << v;
        settings->setValue(QString::number(i), vl);
      }
      settings->endGroup();
    }
  }
}

void SplitWindow::restoreSplitterSizes() { restoreSplitterSizes(QString()); }

void SplitWindow::restoreSplitterSizes(const QString &groupPrefix) {
  if (currentSplitters_.empty()) return;
  AppSettings settings;
  // If no groupPrefix provided, read from splitterSizes/<layout>/<index>
  if (groupPrefix.isEmpty()) {
    settings->beginGroup(QStringLiteral("splitterSizes"));
    settings->beginGroup(layoutModeKey(layoutMode_));
    for (int i = 0; i < (int)currentSplitters_.size(); ++i) {
      QSplitter *s = currentSplitters_[i];
      if (!s) continue;
      const QVariant v = settings->value(QString::number(i));
      if (!v.isValid()) continue;
      const QVariantList vl = v.toList();
      if (vl.isEmpty()) continue;
      QList<int> sizes;
      sizes.reserve(vl.size());
      for (const QVariant &qv : vl) sizes << qv.toInt();
      if (!sizes.isEmpty()) s->setSizes(sizes);
    }
    settings->endGroup();
    settings->endGroup();
  } else {
    {
      GroupScope _gs(settings, groupPrefix);
      settings->beginGroup(layoutModeKey(layoutMode_));
      for (int i = 0; i < (int)currentSplitters_.size(); ++i) {
        QSplitter *s = currentSplitters_[i];
        if (!s) continue;
        const QVariant v = settings->value(QString::number(i));
        if (!v.isValid()) continue;
        const QVariantList vl = v.toList();
        if (vl.isEmpty()) continue;
        QList<int> sizes;
        sizes.reserve(vl.size());
        for (const QVariant &qv : vl) sizes << qv.toInt();
        if (!sizes.isEmpty()) s->setSizes(sizes);
      }
      settings->endGroup();
    }
  }
}

void SplitWindow::onSplitterDoubleClickResized() {
  // Save splitter sizes after double-click resize
  if (!windowId_.isEmpty()) {
    saveCurrentSplitterSizes(QStringLiteral("windows/%1/splitterSizes").arg(windowId_));
  } else {
    saveCurrentSplitterSizes();
  }
}

SplitFrameWidget *SplitWindow::focusedFrameOrFirst() const {
  QWidget *fw = QApplication::focusWidget();
  while (fw) {
    if (auto *frame = qobject_cast<SplitFrameWidget *>(fw)) return frame;
    fw = fw->parentWidget();
  }
  if (lastFocusedFrame_) return lastFocusedFrame_;
  return firstFrameWidget();
}

SplitFrameWidget *SplitWindow::firstFrameWidget() const {
  if (!central_) return nullptr;
  const QList<SplitFrameWidget *> frames = central_->findChildren<SplitFrameWidget *>();
  SplitFrameWidget *best = nullptr;
  int bestIndex = std::numeric_limits<int>::max();
  for (SplitFrameWidget *frame : frames) {
    bool ok = false;
    const int idx = frame->property("logicalIndex").toInt(&ok);
    if (ok && idx < bestIndex) {
      bestIndex = idx;
      best = frame;
    }
  }
  if (best) return best;
  return central_->findChild<SplitFrameWidget *>();
}

void SplitWindow::onFrameDevToolsRequested(SplitFrameWidget *who, QWebEnginePage *page, const QPoint &pos) {
  Q_UNUSED(who);
  Q_UNUSED(pos);
  if (!page) return;
  createAndAttachSharedDevToolsForPage(page);
  if (sharedDevToolsView_) {
    sharedDevToolsView_->show();
    sharedDevToolsView_->raise();
    sharedDevToolsView_->activateWindow();
  }
  page->triggerAction(QWebEnginePage::InspectElement);
}

void SplitWindow::onFrameTranslateRequested(SplitFrameWidget *who, const QUrl &translateUrl) {
  Q_UNUSED(who);
  if (!translateUrl.isValid()) return;
  // Open the translation URL in a new Phraims window
  createAndShowWindow(translateUrl.toString());
}

void SplitWindow::onFrameInteraction(SplitFrameWidget *who) {
  if (!who) return;
  if (who->window() != this) return;
  lastFocusedFrame_ = who;
}

void SplitWindow::createAndAttachSharedDevToolsForPage(QWebEnginePage *page) {
  if (!page) return;
  if (!sharedDevToolsView_) {
    sharedDevToolsView_ = new QWebEngineView(this);
    sharedDevToolsView_->setWindowFlag(Qt::Tool, true);
    sharedDevToolsView_->setAttribute(Qt::WA_DeleteOnClose);

    QWebEngineProfile *profile = page->profile();
    auto *devPage = new MyWebEnginePage(profile, sharedDevToolsView_);
    sharedDevToolsView_->setPage(devPage);

    page->setDevToolsPage(devPage);
    sharedDevToolsView_->resize(980, 720);
    sharedDevToolsView_->setWindowTitle(tr("DevTools"));

    // Add a Close action so Cmd/Ctrl+W will hide the DevTools window
    // rather than destroying it. Hiding preserves the DevTools page
    // and its localStorage/preferences (e.g., theme choice).
    QAction *closeAct = new QAction(sharedDevToolsView_);
    closeAct->setShortcut(QKeySequence::Close);
    connect(closeAct, &QAction::triggered, sharedDevToolsView_, &QWidget::hide);
    sharedDevToolsView_->addAction(closeAct);

    // When the shared devtools view is destroyed (app shutdown), only
    // clear the devToolsPage on the inspected page if it still points
    // to the dev page we created here. Use QPointer guards so we do
    // not dereference raw pointers that may already have been deleted
    // by Qt's shutdown sequence which can cause crashes.
    {
      QPointer<QWebEnginePage> pageGuard(page);
      QPointer<QWebEnginePage> devPageGuard(devPage);
      connect(sharedDevToolsView_, &QObject::destroyed, this, [this, pageGuard, devPageGuard](QObject *) {
        if (pageGuard && pageGuard->devToolsPage() == devPageGuard) pageGuard->setDevToolsPage(nullptr);
        sharedDevToolsView_ = nullptr;
      });
    }
  } else {
    // reattach the existing shared devtools to the new inspected page
    if (page->devToolsPage() != sharedDevToolsView_->page()) {
      page->setDevToolsPage(sharedDevToolsView_->page());
    }
  }
}

void SplitWindow::showDomPatchesManager() {
  // Create the manager as a modeless dialog so the user can interact with
  // DevTools / frames while editing patches. Reapply patches when the
  // dialog finishes (accepted or rejected) to ensure changes take effect.
  DomPatchesDialog *dlg = new DomPatchesDialog(this);
  dlg->setAttribute(Qt::WA_DeleteOnClose);
  dlg->show();
  connect(dlg, &QDialog::finished, this, [this](int) {
    const QList<SplitFrameWidget *> frames = central_->findChildren<SplitFrameWidget *>();
    for (SplitFrameWidget *f : frames) {
      if (auto *p = f->page()) applyDomPatchesToPage(p);
    }
  });
}

void SplitWindow::onCloseShortcut() {
  // If more than one frame exists, close the last/end frame instead of the window.
  if ((int)frames_.size() > 1) {
    qDebug() << "onCloseShortcut: removing last frame (Cmd-W pressed)";
    // Remove the last frame and rebuild UI. Persist the frame state.
    frames_.pop_back();
    persistGlobalFrameState();
    rebuildSections((int)frames_.size());
  } else {
    // Only one frame remains: close the window as normal.
    qDebug() << "onCloseShortcut: single frame, closing window";
    this->close();
  }
}

void SplitWindow::increaseFocusedFrameScale() {
  if (SplitFrameWidget *frame = focusedFrameOrFirst()) {
    frame->setScaleFactor(frame->scaleFactor() + SplitFrameWidget::kScaleStep, true);
  }
}

void SplitWindow::decreaseFocusedFrameScale() {
  if (SplitFrameWidget *frame = focusedFrameOrFirst()) {
    frame->setScaleFactor(frame->scaleFactor() - SplitFrameWidget::kScaleStep, true);
  }
}

void SplitWindow::resetFocusedFrameScale() {
  if (SplitFrameWidget *frame = focusedFrameOrFirst()) {
    frame->setScaleFactor(1.0, true);
  }
}

void SplitWindow::showAboutDialog() {
  // Create a custom dialog so we can handle link clicks and open them in Phraims
  QDialog aboutDialog(this);
  aboutDialog.setWindowTitle(tr("About Phraims"));
  aboutDialog.setModal(true);
  
  auto *layout = new QVBoxLayout(&aboutDialog);
  
  // Create a QTextBrowser to display the about text with clickable links
  auto *textBrowser = new QTextBrowser(&aboutDialog);
  textBrowser->setOpenExternalLinks(false);  // Don't open links in external browser
  textBrowser->setOpenLinks(false);          // Don't navigate internally either - we'll handle clicks
  textBrowser->setFrameStyle(QFrame::NoFrame);
  textBrowser->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  textBrowser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  textBrowser->setMaximumHeight(ABOUT_DIALOG_MAX_HEIGHT);
  
  const QString aboutText = QString(
    "<div style='text-align: center;'>"
    "<h2>%1</h2>"
    "<p><b>Version %2</b></p>"
    "</div>"
    "<p>A web browser that divides each window into multiple resizable web page frames.</p>"
    "<p>Built with Qt %3 and QtWebEngine (Chromium)</p>"
    "<p><a href='%4'>%4</a></p>"
  ).arg(QCoreApplication::applicationName())
   .arg(QString::fromUtf8(PHRAIMS_VERSION))
   .arg(QString::fromUtf8(qVersion()))
   .arg(QString::fromUtf8(PHRAIMS_HOMEPAGE_URL));
  
  textBrowser->setHtml(aboutText);
  
  // Handle link clicks by opening in a new Phraims window
  // The dialog remains showing the About text without navigating away
  connect(textBrowser, &QTextBrowser::anchorClicked, [](const QUrl &url) {
    createAndShowWindow(url.toString());
  });
  
  layout->addWidget(textBrowser);
  
  // Add OK button
  auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok, &aboutDialog);
  connect(buttonBox, &QDialogButtonBox::accepted, &aboutDialog, &QDialog::accept);
  layout->addWidget(buttonBox);
  
  aboutDialog.setMinimumWidth(ABOUT_DIALOG_MIN_WIDTH);
  
  // Center the dialog on the screen containing the active window
  // This ensures consistent positioning regardless of which window is active
  QScreen *screen = this->screen();
  if (!screen) {
    screen = QGuiApplication::primaryScreen();
  }
  if (screen) {
    const QRect screenGeometry = screen->availableGeometry();
    // Use sizeHint to get the dialog's preferred size before showing
    const QSize dialogSize = aboutDialog.sizeHint();
    const int x = screenGeometry.x() + (screenGeometry.width() - dialogSize.width()) / 2;
    const int y = screenGeometry.y() + (screenGeometry.height() - dialogSize.height()) / 2;
    aboutDialog.move(x, y);
  }
  
  aboutDialog.exec();
}

void SplitWindow::updateWindowMenu() {
  if (!windowMenu_) return;
  windowMenu_->clear();
  QAction *minimizeAct = windowMenu_->addAction(tr("Minimize"));
  minimizeAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));
  connect(minimizeAct, &QAction::triggered, this, &QWidget::showMinimized);
  QAction *closeAct = windowMenu_->addAction(tr("Close Window"));
  closeAct->setShortcut(QKeySequence::Close);
  connect(closeAct, &QAction::triggered, this, &SplitWindow::onCloseShortcut);
  windowMenu_->addSeparator();

  // Use the pre-created icons (created once at app startup) so we don't
  // redraw the icon pixmaps on every menu update.

  // List all windows
  int idx = 1;
  for (SplitWindow *w : g_windows) {
    if (!w) continue;

    // Build the visible title (no prefix); icon column will show diamond.
    QString title = w->windowTitle();
    if (title.isEmpty()) title = QStringLiteral("Window %1").arg(idx);

    const bool minimized = (w->windowState() & Qt::WindowMinimized) || w->isMinimized();
    const bool active = w->isActiveWindow();

    // Use the title as-is; the icon column displays the minimized
    // indicator (diamond) so we don't need a text-prefix fallback.
    QAction *a = windowMenu_->addAction(title);

    // Use our own icons instead of the platform check column so the
    // diamond and active indicator share the same icon column.
    a->setCheckable(false);

    QIcon useIcon = g_windowEmptyIcon;
    if (active && minimized) useIcon = g_windowCheckDiamondIcon;
    else if (active) useIcon = g_windowCheckIcon;
    else if (minimized) useIcon = g_windowDiamondIcon;
    a->setIcon(useIcon);
    a->setIconVisibleInMenu(true);

    connect(a, &QAction::triggered, this, [w]() {
      if (!w) return;
      // Ensure the target window is visible and not minimized before
      // attempting to raise/activate it. On macOS simply calling
      // raise()/activateWindow() may not be sufficient when a window
      // is hidden or minimized.
      if (!w->isVisible()) w->show();
      if (w->isMinimized()) w->showNormal();
      w->raise();
      w->activateWindow();
    });
    ++idx;
  }
}

void SplitWindow::changeEvent(QEvent *event) {
  if (event && event->type() == QEvent::WindowStateChange) {
    // Refresh menus so the minimized/active indicators update.
    rebuildAllWindowMenus();
  }
  QMainWindow::changeEvent(event);
}

void SplitWindow::updateProfilesMenu() {
  if (!profilesMenu_) return;
  
  // Remove all profile-specific actions (those after the last separator)
  // Find the last separator that marks the start of the profile list
  QList<QAction *> actions = profilesMenu_->actions();
  int lastSeparatorIndex = -1;
  for (int i = 0; i < actions.size(); ++i) {
    if (actions[i]->isSeparator()) {
      lastSeparatorIndex = i;
    }
  }
  
  if (lastSeparatorIndex >= 0) {
    // Remove all actions after the last separator
    for (int i = actions.size() - 1; i > lastSeparatorIndex; --i) {
      profilesMenu_->removeAction(actions[i]);
      delete actions[i];
    }
  }
  
  // Get the list of available profiles
  const QStringList profiles = listProfiles();
  
  // Add an action for each profile with a checkmark for the current one
  for (const QString &profileName : profiles) {
    QAction *action = profilesMenu_->addAction(profileName);
    action->setCheckable(true);
    action->setChecked(profileName == currentProfileName_);
    
    connect(action, &QAction::triggered, this, [this, profileName]() {
      switchToProfile(profileName);
    });
  }
}

void SplitWindow::switchToProfile(const QString &profileName) {
  if (profileName == currentProfileName_) {
    qDebug() << "switchToProfile: already using profile" << profileName;
    return;
  }
  
  qDebug() << "switchToProfile: switching from" << currentProfileName_ << "to" << profileName;
  
  currentProfileName_ = profileName;
  profile_ = getProfileByName(profileName);
  
  // Set the new profile as the global current profile so new windows use it by default.
  // Design choice: Switching profiles in one window sets the default for all new windows,
  // making it the "active" profile application-wide. This provides consistent behavior
  // where the most recently selected profile becomes the default.
  setCurrentProfileName(profileName);
  
  // Rebuild all frames with the new profile
  rebuildSections((int)frames_.size());
  
  // Update the profiles menu to reflect the change
  updateProfilesMenu();
  
  // Update all other windows' profiles menus
  for (SplitWindow *w : g_windows) {
    if (w && w != this && w->profilesMenu_) {
      w->updateProfilesMenu();
    }
  }
  
  // Persist the profile change immediately
  savePersistentStateToSettings();
}

void SplitWindow::createNewProfile() {
  bool ok = false;
  QString name = QInputDialog::getText(
    this,
    tr("New Profile"),
    tr("Enter a name for the new profile:"),
    QLineEdit::Normal,
    QString(),
    &ok
  );
  
  if (!ok || name.isEmpty()) return;
  
  // Validate the name
  if (!isValidProfileName(name)) {
    QMessageBox::warning(
      this,
      tr("Invalid Name"),
      tr("Profile names cannot be empty or contain slashes.")
    );
    return;
  }
  
  if (createProfile(name)) {
    QMessageBox::information(
      this,
      tr("Profile Created"),
      tr("Profile '%1' has been created.").arg(name)
    );
    
    // Update all windows' profiles menus
    for (SplitWindow *w : g_windows) {
      if (w && w->profilesMenu_) {
        w->updateProfilesMenu();
      }
    }
  } else {
    QMessageBox::warning(
      this,
      tr("Profile Exists"),
      tr("A profile named '%1' already exists.").arg(name)
    );
  }
}

void SplitWindow::renameCurrentProfile() {
  QStringList profiles = listProfiles();
  
  bool ok = false;
  QString oldName = QInputDialog::getItem(
    this,
    tr("Rename Profile"),
    tr("Select a profile to rename:"),
    profiles,
    profiles.indexOf(currentProfileName_),
    false,
    &ok
  );
  
  if (!ok || oldName.isEmpty()) return;
  
  QString newName = QInputDialog::getText(
    this,
    tr("Rename Profile"),
    tr("Enter a new name for profile '%1':").arg(oldName),
    QLineEdit::Normal,
    oldName,
    &ok
  );
  
  if (!ok || newName.isEmpty() || newName == oldName) return;
  
  // Validate the name
  if (!isValidProfileName(newName)) {
    QMessageBox::warning(
      this,
      tr("Invalid Name"),
      tr("Profile names cannot be empty or contain slashes.")
    );
    return;
  }
  
  if (renameProfile(oldName, newName)) {
    QMessageBox::information(
      this,
      tr("Profile Renamed"),
      tr("Profile '%1' has been renamed to '%2'.").arg(oldName, newName)
    );
    
    // If we renamed the current profile, update the local name
    if (currentProfileName_ == oldName) {
      currentProfileName_ = newName;
    }
    
    // Update all windows' profiles menus
    for (SplitWindow *w : g_windows) {
      if (w && w->profilesMenu_) {
        w->updateProfilesMenu();
      }
    }
  } else {
    QMessageBox::warning(
      this,
      tr("Rename Failed"),
      tr("Failed to rename profile. The new name may already exist.")
    );
  }
}

void SplitWindow::deleteSelectedProfile() {
  QStringList profiles = listProfiles();
  
  if (profiles.size() <= 1) {
    QMessageBox::warning(
      this,
      tr("Cannot Delete"),
      tr("Cannot delete the last profile. At least one profile must exist.")
    );
    return;
  }
  
  bool ok = false;
  QString name = QInputDialog::getItem(
    this,
    tr("Delete Profile"),
    tr("Select a profile to delete:"),
    profiles,
    0,
    false,
    &ok
  );
  
  if (!ok || name.isEmpty()) return;
  
  // Confirm deletion
  QMessageBox::StandardButton reply = QMessageBox::question(
    this,
    tr("Confirm Deletion"),
    tr("Are you sure you want to delete profile '%1'?\n\nThis will permanently delete all data associated with this profile including cookies, cache, and browsing history.").arg(name),
    QMessageBox::Yes | QMessageBox::No,
    QMessageBox::No
  );
  
  if (reply != QMessageBox::Yes) return;
  
  if (deleteProfile(name)) {
    QMessageBox::information(
      this,
      tr("Profile Deleted"),
      tr("Profile '%1' has been deleted.").arg(name)
    );
    
    // If we deleted the current profile, deleteProfile() automatically switched
    // the global current profile to another one. Update our window's local state.
    if (currentProfileName_ == name) {
      currentProfileName_ = currentProfileName();
      profile_ = getProfileByName(currentProfileName_);
      rebuildSections((int)frames_.size());
    }
    
    // Update all windows' profiles menus
    for (SplitWindow *w : g_windows) {
      if (w && w->profilesMenu_) {
        w->updateProfilesMenu();
      }
    }
  } else {
    QMessageBox::warning(
      this,
      tr("Delete Failed"),
      tr("Failed to delete profile.")
    );
  }
}
