#include "SplitWindow.h"
#include "SplitFrameWidget.h"
#include "DomPatch.h"
#include "Utils.h"
#include "SplitterDoubleClickFilter.h"
#include <cmath>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QGridLayout>
#include <QGuiApplication>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QUuid>
#include <QVBoxLayout>
#include <QWebEngineProfile>
#include <QWebEnginePage>
#include <QWebEngineView>
#include <algorithm>

bool DEBUG_SHOW_WINDOW_ID = 0;

SplitWindow::SplitWindow(const QString &windowId, QWidget *parent) : QMainWindow(parent), windowId_(windowId) {
  setWindowTitle(QCoreApplication::applicationName());
  resize(800, 600);

  QSettings settings;

  // File menu: New Window (Cmd/Ctrl+N)
  auto *fileMenu = menuBar()->addMenu(tr("File"));
  QAction *newWindowAction = fileMenu->addAction(tr("New Window"));
  newWindowAction->setShortcut(QKeySequence::New);
  connect(newWindowAction, &QAction::triggered, this, [](bool){ createAndShowWindow(); });

  // No global toolbar; per-frame + / - buttons control sections.

  // create a shared persistent QWebEngineProfile for all frames so
  // cookies/localStorage/session state are persisted across frames and runs
  const QString dataRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  const QString profilePath = dataRoot;
  qDebug() << "  WebEngine profilePath:" << profilePath;
  const QString profileCache = profilePath + "/cache";
  qDebug() << "  WebEngine profileCache:" << profileCache;
  QDir().mkpath(profilePath);
  QDir().mkpath(profileCache);
  const QString profileName = QCoreApplication::organizationName();
  qDebug() << "  WebEngine profileName:" << profileName;
  profile_ = new QWebEngineProfile(profileName, this);
  profile_->setPersistentStoragePath(profilePath);
  profile_->setCachePath(profileCache);
  profile_->setHttpCacheType(QWebEngineProfile::DiskHttpCache);
  profile_->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);

  // (window geometry/state restored later after UI is built)

  // add a simple View menu with a helper to set the window height to the
  // screen available height (preserves width and x position)
  auto *viewMenu = menuBar()->addMenu(tr("View"));
  QAction *setHeightAction = viewMenu->addAction(tr("Set height to screen"));
  connect(setHeightAction, &QAction::triggered, this, &SplitWindow::setHeightToScreen);
  QAction *toggleDevToolsAction = viewMenu->addAction(tr("Toggle DevTools"));
  toggleDevToolsAction->setShortcut(QKeySequence(Qt::Key_F12));
  connect(toggleDevToolsAction, &QAction::triggered, this, &SplitWindow::toggleDevToolsForFocusedFrame);

  // Always-on-top toggle
  QAction *alwaysOnTopAction = viewMenu->addAction(tr("Always on Top"));
  alwaysOnTopAction->setCheckable(true);
  // read persisted value (default: false)
  {
    const bool on = settings.value("alwaysOnTop", false).toBool();
    alwaysOnTopAction->setChecked(on);
    // apply the window flag; setWindowFlag requires a show() to take effect on some platforms
    setWindowFlag(Qt::WindowStaysOnTopHint, on);
    if (on) show();
  }
  connect(alwaysOnTopAction, &QAction::toggled, this, [this](bool checked){
    setWindowFlag(Qt::WindowStaysOnTopHint, checked);
    if (checked) show();
    QSettings settings;
    settings.setValue("alwaysOnTop", checked);
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
  int storedMode = settings.value("layoutMode", (int)Vertical).toInt();
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

  // Window menu: per-macOS convention
  windowMenu_ = menuBar()->addMenu(tr("Window"));
  // Add standard close/minimize actions
  QAction *minimizeAct = windowMenu_->addAction(tr("Minimize"));
  minimizeAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));
  connect(minimizeAct, &QAction::triggered, this, &QWidget::showMinimized);
  QAction *closeAct = windowMenu_->addAction(tr("Close Window"));
  closeAct->setShortcut(QKeySequence::Close);
  connect(closeAct, &QAction::triggered, this, &QWidget::close);
  windowMenu_->addSeparator();

  // central scroll area to allow many sections
  auto *scroll = new QScrollArea();
  scroll->setWidgetResizable(true);
  central_ = new QWidget();
  scroll->setWidget(central_);
  setCentralWidget(scroll);

  layout_ = new QVBoxLayout(central_);
  layout_->setContentsMargins(4, 4, 4, 4);
  layout_->setSpacing(6);

  // load persisted addresses (per-window if windowId_ present, otherwise global)
  if (!windowId_.isEmpty()) {
    QSettings s;
    {
      GroupScope _gs(s, QStringLiteral("windows/%1").arg(windowId_));
      const QStringList saved = s.value("addresses").toStringList();
      if (saved.isEmpty()) {
        addresses_.push_back(QString());
      } else {
        for (const QString &s2 : saved) addresses_.push_back(s2);
      }
      layoutMode_ = (LayoutMode)s.value("layoutMode", (int)layoutMode_).toInt();
    }
  } else {
    const QStringList saved = settings.value("addresses").toStringList();
    if (saved.isEmpty()) {
      addresses_.push_back(QString());
    } else {
      for (const QString &s : saved) addresses_.push_back(s);
    }
  }
  // build initial UI
  rebuildSections((int)addresses_.size());
  // restore splitter sizes only once at startup (subsequent layout
  // selections/rebuilds should reset splitters to defaults)
  if (!windowId_.isEmpty()) {
    restoreSplitterSizes(QStringLiteral("windows/%1/splitterSizes").arg(windowId_));
  } else {
    restoreSplitterSizes();
  }
  restoredOnStartup_ = true;

  // restore saved window geometry and window state (position/size/state)
  if (!windowId_.isEmpty()) {
    QSettings s;
    {
      GroupScope _gs(s, QStringLiteral("windows/%1").arg(windowId_));
      const QByteArray savedGeom = s.value("windowGeometry").toByteArray();
      if (!savedGeom.isEmpty()) restoreGeometry(savedGeom);
      const QByteArray savedState = s.value("windowState").toByteArray();
      if (!savedState.isEmpty()) restoreState(savedState);
    }
  } else {
    const QByteArray savedGeom = settings.value("windowGeometry").toByteArray();
    if (!savedGeom.isEmpty()) restoreGeometry(savedGeom);
    const QByteArray savedState = settings.value("windowState").toByteArray();
    if (!savedState.isEmpty()) restoreState(savedState);
  }
}

void SplitWindow::savePersistentStateToSettings() {
  QSettings s;
  QString id = windowId_;
  if (id.isEmpty()) id = QUuid::createUuid().toString();
  qDebug() << "savePersistentStateToSettings: saving window id=" << id << " addresses.count=" << addresses_.size() << " layoutMode=" << (int)layoutMode_;
  {
    GroupScope _gs(s, QStringLiteral("windows/%1").arg(id));
    QStringList list;
    for (const auto &a : addresses_) list << a;
    s.setValue("addresses", list);
    s.setValue("layoutMode", (int)layoutMode_);
    s.setValue("windowGeometry", saveGeometry());
    s.setValue("windowState", saveState());
  }
  s.sync();
  // persist splitter sizes under windows/<id>/splitterSizes/<index>
  saveCurrentSplitterSizes(QStringLiteral("windows/%1/splitterSizes").arg(id));
}

void SplitWindow::resetToSingleEmptySection() {
  addresses_.clear();
  addresses_.push_back(QString());
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
  const int count = (int)addresses_.size();
  QString title = QStringLiteral("Group %1 (%2)").arg(idx).arg(count);
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

  // Ensure addresses_ vector matches requested size, preserving existing values.
  if ((int)addresses_.size() < n) {
    addresses_.resize(n);
  } else if ((int)addresses_.size() > n) {
    addresses_.resize(n);
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
      // logicalIndex property used for mapping frame -> addresses_ index
      frame->setProperty("logicalIndex", i);
      frame->setProfile(profile_);
      frame->setAddress(addresses_[i]);
      connect(frame, &SplitFrameWidget::plusClicked, this, &SplitWindow::onPlusFromFrame);
      connect(frame, &SplitFrameWidget::minusClicked, this, &SplitWindow::onMinusFromFrame);
      connect(frame, &SplitFrameWidget::addressEdited, this, &SplitWindow::onAddressEdited);
      connect(frame, &SplitFrameWidget::upClicked, this, &SplitWindow::onUpFromFrame);
      connect(frame, &SplitFrameWidget::downClicked, this, &SplitWindow::onDownFromFrame);
      connect(frame, &SplitFrameWidget::devToolsRequested, this, &SplitWindow::onFrameDevToolsRequested);
      frame->setMinusEnabled(n > 1);
      frame->setUpEnabled(i > 0);
      frame->setDownEnabled(i < n - 1);
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
        // logicalIndex property used for mapping frame -> addresses_ index
        frame->setProperty("logicalIndex", idx);
        frame->setProfile(profile_);
        frame->setAddress(addresses_[idx]);
        connect(frame, &SplitFrameWidget::plusClicked, this, &SplitWindow::onPlusFromFrame);
        connect(frame, &SplitFrameWidget::minusClicked, this, &SplitWindow::onMinusFromFrame);
        connect(frame, &SplitFrameWidget::addressEdited, this, &SplitWindow::onAddressEdited);
        connect(frame, &SplitFrameWidget::upClicked, this, &SplitWindow::onUpFromFrame);
        connect(frame, &SplitFrameWidget::downClicked, this, &SplitWindow::onDownFromFrame);
        connect(frame, &SplitFrameWidget::devToolsRequested, this, &SplitWindow::onFrameDevToolsRequested);
        frame->setMinusEnabled(n > 1);
        frame->setUpEnabled(idx > 0);
        frame->setDownEnabled(idx < n - 1);
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

  QWidget *fw = QApplication::focusWidget();
  SplitFrameWidget *target = nullptr;
  while (fw) {
    if (auto *f = qobject_cast<SplitFrameWidget *>(fw)) {
      target = f;
      break;
    }
    fw = fw->parentWidget();
  }
  if (!target && central_) target = central_->findChild<SplitFrameWidget *>();
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

void SplitWindow::onPlusFromFrame(SplitFrameWidget *who) {
  // use logicalIndex property assigned during rebuildSections
  const QVariant v = who->property("logicalIndex");
  if (!v.isValid()) return;
  int pos = v.toInt();
  addresses_.insert(addresses_.begin() + pos + 1, QString());
  // persist addresses
  QSettings settings;
  QStringList list;
  for (const auto &a : addresses_) list << a;
  settings.setValue("addresses", list);
  // rebuild UI with the updated addresses_
  rebuildSections((int)addresses_.size());
}

void SplitWindow::onUpFromFrame(SplitFrameWidget *who) {
  // move this frame up (towards index 0)
  const QVariant v = who->property("logicalIndex");
  if (!v.isValid()) return;
  int pos = v.toInt();
  if (pos <= 0) return; // already at top or not found
  std::swap(addresses_[pos], addresses_[pos - 1]);
  // persist addresses
  QSettings settings;
  QStringList list;
  for (const auto &a : addresses_) list << a;
  settings.setValue("addresses", list);
  rebuildSections((int)addresses_.size());
}

void SplitWindow::onDownFromFrame(SplitFrameWidget *who) {
  // move this frame down (towards larger indices)
  const QVariant v = who->property("logicalIndex");
  if (!v.isValid()) return;
  int pos = v.toInt();
  if (pos < 0 || pos >= (int)addresses_.size() - 1) return; // at bottom or not found
  std::swap(addresses_[pos], addresses_[pos + 1]);
  // persist addresses
  QSettings settings;
  QStringList list;
  for (const auto &a : addresses_) list << a;
  settings.setValue("addresses", list);
  rebuildSections((int)addresses_.size());
}

void SplitWindow::setLayoutMode(SplitWindow::LayoutMode m) {
  QSettings settings;

  // If the user re-selects the already-selected layout, treat that as a
  // request to reset splitters to their default sizes. Clear any saved
  // sizes for this layout and rebuild without saving the current sizes.
  if (m == layoutMode_) {
    const QString base = QStringLiteral("splitterSizes/%1").arg(layoutModeKey(layoutMode_));
    settings.remove(base);
    // rebuild so splitters are reset to defaults
    rebuildSections((int)addresses_.size());
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
    settings.remove(targetBase);
  }
  // Apply the new layout mode and persist it.
  layoutMode_ = m;
  settings.setValue("layoutMode", (int)layoutMode_);
  // Rebuild UI for the new layout (splitter sizes are only restored at startup)
  rebuildSections((int)addresses_.size());
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
  if (addresses_.size() <= 1) return; // shouldn't remove last

  const QVariant v = who->property("logicalIndex");
  if (!v.isValid()) return;
  int pos = v.toInt();
  if (pos < 0) return;

  // confirm with the user before removing
  const QMessageBox::StandardButton reply = QMessageBox::question(
    this, tr("Remove section"), tr("Remove this section?"),
    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (reply != QMessageBox::Yes) return;

  addresses_.erase(addresses_.begin() + pos);
  // persist addresses
  QSettings settings;
  QStringList list;
  for (const auto &a : addresses_) list << a;
  settings.setValue("addresses", list);
  rebuildSections((int)addresses_.size());
}

void SplitWindow::onAddressEdited(SplitFrameWidget *who, const QString &text) {
  const QVariant v = who->property("logicalIndex");
  if (!v.isValid()) return;
  int pos = v.toInt();
  if (pos < 0) return;
  if (pos < (int)addresses_.size()) {
    addresses_[pos] = text;
    // persist addresses list
    QSettings settings;
    QStringList list;
    for (const auto &a : addresses_) list << a;
    settings.setValue("addresses", list);
  }
}

void SplitWindow::closeEvent(QCloseEvent *event) {
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
      QSettings s;
      {
        GroupScope _gs(s, QStringLiteral("windows/%1").arg(windowId_));
        QStringList list;
        for (const auto &a : addresses_) list << a;
        s.setValue("addresses", list);
        s.setValue("layoutMode", (int)layoutMode_);
        s.setValue("windowGeometry", saveGeometry());
        s.setValue("windowState", saveState());
      }
      // Ensure these shutdown-time writes are flushed to the backend.
      s.sync();
      saveCurrentSplitterSizes(QStringLiteral("windows/%1/splitterSizes").arg(windowId_));
    } else {
      // If other windows exist, remove this window's saved group now.
      // If this is the last window, preserve the saved group so it
      // reopens on next launch.
      saveCurrentSplitterSizes(QStringLiteral("windows/%1/splitterSizes").arg(windowId_));
      const size_t windowsCount = g_windows.size();
      qDebug() << "SplitWindow::closeEvent: g_windows.count (including this)=" << windowsCount;
      if (windowsCount > 1) {
        QSettings s;
        s.beginGroup(QStringLiteral("windows"));
        const QStringList before = s.childGroups();
        if (before.contains(windowId_)) {
          qDebug() << "SplitWindow::closeEvent: removing stored group for" << windowId_;
          s.remove(windowId_);
          s.sync();
        } else {
          qDebug() << "SplitWindow::closeEvent: no stored group for" << windowId_;
        }
        s.endGroup();
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
    QSettings settings;
    QStringList list;
    for (const auto &a : addresses_) list << a;
    settings.setValue("addresses", list);
    // persist window geometry
    settings.setValue("windowGeometry", saveGeometry());
    // persist window state (toolbars/dock state and maximized/minimized state)
    settings.setValue("windowState", saveState());
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

void SplitWindow::saveCurrentSplitterSizes() {
  saveCurrentSplitterSizes(QString());
}

void SplitWindow::saveCurrentSplitterSizes(const QString &groupPrefix) {
  if (currentSplitters_.empty()) return;
  QSettings settings;
  // If no groupPrefix provided, store under splitterSizes/<layout>/<index>
  if (groupPrefix.isEmpty()) {
    settings.beginGroup(QStringLiteral("splitterSizes"));
    settings.beginGroup(layoutModeKey(layoutMode_));
    for (int i = 0; i < (int)currentSplitters_.size(); ++i) {
      QSplitter *s = currentSplitters_[i];
      if (!s) continue;
      const QList<int> sizes = s->sizes();
      QVariantList vl;
      for (int v : sizes) vl << v;
      settings.setValue(QString::number(i), vl);
    }
    settings.endGroup();
    settings.endGroup();
  } else {
    // Create nested groups for the provided prefix (e.g., windows/<id>/splitterSizes)
    {
      GroupScope _gs(settings, groupPrefix);
      settings.beginGroup(layoutModeKey(layoutMode_));
      for (int i = 0; i < (int)currentSplitters_.size(); ++i) {
        QSplitter *s = currentSplitters_[i];
        if (!s) continue;
        const QList<int> sizes = s->sizes();
        QVariantList vl;
        for (int v : sizes) vl << v;
        settings.setValue(QString::number(i), vl);
      }
      settings.endGroup();
    }
  }
}

void SplitWindow::restoreSplitterSizes() { restoreSplitterSizes(QString()); }

void SplitWindow::restoreSplitterSizes(const QString &groupPrefix) {
  if (currentSplitters_.empty()) return;
  QSettings settings;
  // If no groupPrefix provided, read from splitterSizes/<layout>/<index>
  if (groupPrefix.isEmpty()) {
    settings.beginGroup(QStringLiteral("splitterSizes"));
    settings.beginGroup(layoutModeKey(layoutMode_));
    for (int i = 0; i < (int)currentSplitters_.size(); ++i) {
      QSplitter *s = currentSplitters_[i];
      if (!s) continue;
      const QVariant v = settings.value(QString::number(i));
      if (!v.isValid()) continue;
      const QVariantList vl = v.toList();
      if (vl.isEmpty()) continue;
      QList<int> sizes;
      sizes.reserve(vl.size());
      for (const QVariant &qv : vl) sizes << qv.toInt();
      if (!sizes.isEmpty()) s->setSizes(sizes);
    }
    settings.endGroup();
    settings.endGroup();
  } else {
    {
      GroupScope _gs(settings, groupPrefix);
      settings.beginGroup(layoutModeKey(layoutMode_));
      for (int i = 0; i < (int)currentSplitters_.size(); ++i) {
        QSplitter *s = currentSplitters_[i];
        if (!s) continue;
        const QVariant v = settings.value(QString::number(i));
        if (!v.isValid()) continue;
        const QVariantList vl = v.toList();
        if (vl.isEmpty()) continue;
        QList<int> sizes;
        sizes.reserve(vl.size());
        for (const QVariant &qv : vl) sizes << qv.toInt();
        if (!sizes.isEmpty()) s->setSizes(sizes);
      }
      settings.endGroup();
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

void SplitWindow::createAndAttachSharedDevToolsForPage(QWebEnginePage *page) {
  if (!page) return;
  if (!sharedDevToolsView_) {
    sharedDevToolsView_ = new QWebEngineView(this);
    sharedDevToolsView_->setWindowFlag(Qt::Tool, true);
    sharedDevToolsView_->setAttribute(Qt::WA_DeleteOnClose);

    QWebEngineProfile *profile = page->profile();
    auto *devPage = new QWebEnginePage(profile, sharedDevToolsView_);
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

void SplitWindow::updateWindowMenu() {
  if (!windowMenu_) return;
  windowMenu_->clear();
  QAction *minimizeAct = windowMenu_->addAction(tr("Minimize"));
  minimizeAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));
  connect(minimizeAct, &QAction::triggered, this, &QWidget::showMinimized);
  QAction *closeAct = windowMenu_->addAction(tr("Close Window"));
  closeAct->setShortcut(QKeySequence::Close);
  connect(closeAct, &QAction::triggered, this, &QWidget::close);
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
