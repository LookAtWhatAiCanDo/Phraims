/**
 * Qt6 Widgets Web Brower app that divides the main window into multiple web page frames.
 */
#include <cmath>
#include <vector>
#include <QActionGroup>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDir>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QPalette>
#include <QPointer>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWebEngineHistory>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>
#include <QWidget>

// Qt6 Widgets Web Brower app that divides the main window into multiple web page frames.

/**
 * QWebEngineView subclass to:
 * 1. provide default context menu
 * 2. override createWindow to just navigate to address rather than opening new windows
 */
class MyWebEngineView : public QWebEngineView {
  Q_OBJECT
public:
  using QWebEngineView::QWebEngineView;

signals:
  void devToolsRequested(QWebEnginePage *source, const QPoint &pos);

protected:
  void contextMenuEvent(QContextMenuEvent *event) override {
    QMenu menu(this);
    auto page = this->page();

    // Common navigation actions
    if (auto *a = page->action(QWebEnginePage::Back))    menu.addAction(a);
    if (auto *a = page->action(QWebEnginePage::Forward)) menu.addAction(a);
    if (auto *a = page->action(QWebEnginePage::Reload))  menu.addAction(a);
    menu.addSeparator();
    // Edit actions
    if (auto *a = page->action(QWebEnginePage::Cut))       menu.addAction(a);
    if (auto *a = page->action(QWebEnginePage::Copy))      menu.addAction(a);
    if (auto *a = page->action(QWebEnginePage::Paste))     menu.addAction(a);
    if (auto *a = page->action(QWebEnginePage::SelectAll)) menu.addAction(a);
    menu.addSeparator();
    // Inspect...
    auto inspect = menu.addAction(tr("Inspect…"));

    auto pos = event->pos();
    if (menu.exec(mapToGlobal(pos)) == inspect) emit devToolsRequested(page, pos);
    // accept so the default menu doesn't show
    event->accept();
  }

  MyWebEngineView *createWindow(QWebEnginePage::WebWindowType type) override {
    Q_UNUSED(type);
    // Load popup targets in the same view. Returning 'this' tells the
    // engine to use the current view for the new window's contents.
    return this;
  }
};

// A self-contained frame used for each split section. Contains a top
// address bar (QLineEdit) and a simple content area below.
class SplitFrameWidget : public QFrame {
  Q_OBJECT

public:
  SplitFrameWidget(int index, QWidget *parent = nullptr) : QFrame(parent) {
    setFrameShape(QFrame::StyledPanel);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // subtle alternating background color based on index
    QPalette pal = palette();
    QColor base = palette().color(QPalette::Window);
    int shift = (index % 2 == 0) ? 6 : -6;
    QColor bg = base.lighter(100 + shift);
    pal.setColor(QPalette::Window, bg);
    setAutoFillBackground(true);
    setPalette(pal);

    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(6, 6, 6, 6);
    v->setSpacing(6);

    // left: navigation buttons, center: address bar, right: +/- buttons
    auto *topRow = new QHBoxLayout();
    topRow->setSpacing(6);

    backBtn_ = new QToolButton(this);
    backBtn_->setText("<");
    backBtn_->setToolTip("Back");
    backBtn_->setEnabled(false);
    topRow->addWidget(backBtn_);

    forwardBtn_ = new QToolButton(this);
    forwardBtn_->setText(">");
    forwardBtn_->setToolTip("Forward");
    forwardBtn_->setEnabled(false);
    topRow->addWidget(forwardBtn_);

    refreshBtn_ = new QToolButton(this);
    refreshBtn_->setText("\u21BB"); // clockwise open circle arrow
    refreshBtn_->setToolTip("Refresh");
    refreshBtn_->setEnabled(false);
    topRow->addWidget(refreshBtn_);

    address_ = new QLineEdit(this);
    address_->setPlaceholderText("Address or URL");
    address_->setClearButtonEnabled(true);
    topRow->addWidget(address_, 1);

    // up/down move this frame within the list
    upBtn_ = new QToolButton(this);
    upBtn_->setText("\u25B2"); // up triangle
    upBtn_->setToolTip("Move this section up");
    topRow->addWidget(upBtn_);

    downBtn_ = new QToolButton(this);
    downBtn_->setText("\u25BC"); // down triangle
    downBtn_->setToolTip("Move this section down");
    topRow->addWidget(downBtn_);

    plusBtn_ = new QToolButton(this);
    plusBtn_->setText("+");
    plusBtn_->setToolTip("Insert a new section after this one");
    topRow->addWidget(plusBtn_);

    minusBtn_ = new QToolButton(this);
    minusBtn_->setText("-");
    minusBtn_->setToolTip("Remove this section");
    topRow->addWidget(minusBtn_);

    v->addLayout(topRow);

    // web view content area
    webview_ = new MyWebEngineView(this);
    webview_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    v->addWidget(webview_, 1);

    // wire internal UI to emit signals and control webview
    connect(plusBtn_, &QToolButton::clicked, this, [this]() { emit plusClicked(this); });
    connect(minusBtn_, &QToolButton::clicked, this, [this]() { emit minusClicked(this); });
    connect(upBtn_, &QToolButton::clicked, this, [this]() { emit upClicked(this); });
    connect(downBtn_, &QToolButton::clicked, this, [this]() { emit downClicked(this); });
    connect(address_, &QLineEdit::editingFinished, this, [this]() {
      emit addressEdited(this, address_->text());
      applyAddress(address_->text());
    });

    connect(backBtn_, &QToolButton::clicked, this, [this]() { if (webview_) webview_->back(); });
    connect(forwardBtn_, &QToolButton::clicked, this, [this]() { if (webview_) webview_->forward(); });
    connect(refreshBtn_, &QToolButton::clicked, this, [this]() { if (webview_) webview_->reload(); });

    connect(webview_, &MyWebEngineView::urlChanged, this, [this](const QUrl &url) {
      // ignore internal data URLs (used for instruction/error HTML) so the
      // address bar doesn't show the data: URL. Only update the address when
      // a real navigable URL is loaded.
      if (url.scheme() == QStringLiteral("data") || url.isEmpty()) {
        updateNavButtons();
        return;
      }
      const QString s = url.toString();
      address_->setText(s);
      // update nav button states
      updateNavButtons();
      emit addressEdited(this, s);
    });
    connect(webview_, &MyWebEngineView::loadStarted, this, [this]() { refreshBtn_->setEnabled(true); });
    connect(webview_, &MyWebEngineView::loadFinished, this, [this](bool ok) {
      Q_UNUSED(ok);
      updateNavButtons();
    });
    connect(webview_, &MyWebEngineView::devToolsRequested, this, [this](QWebEnginePage *page, const QPoint &pos) {
      emit devToolsRequested(this, page, pos);
    });
  }

  QString address() const { return address_->text(); }
  void setAddress(const QString &s) { address_->setText(s); applyAddress(s); }
  QWebEnginePage *page() const { return webview_ ? webview_->page() : nullptr; }

  void applyAddress(const QString &s) {
    const QString trimmed = s.trimmed();
    if (trimmed.isEmpty()) {
      // show instruction HTML instead of loading
      const QString html = QStringLiteral("<html><body><div style=\"font-family: sans-serif; color: #666; padding: 20px;\">Enter an address above and press Enter to load a page.</div></body></html>");
      webview_->setHtml(html);
      refreshBtn_->setEnabled(false);
      backBtn_->setEnabled(false);
      forwardBtn_->setEnabled(false);
      return;
    }

    QUrl url = QUrl::fromUserInput(trimmed);
    // If user typed a bare host without scheme, prefer https
    if (url.isValid() && url.scheme().isEmpty()) {
      url.setScheme(QStringLiteral("https"));
    }
    if (!url.isValid()) {
      // show error-instruction
      const QString html = QStringLiteral("<html><body><div style=\"font-family: sans-serif; color: #900; padding: 20px;\">Invalid address.</div></body></html>");
      webview_->setHtml(html);
      refreshBtn_->setEnabled(false);
      backBtn_->setEnabled(false);
      forwardBtn_->setEnabled(false);
      return;
    }

    webview_->load(url);
    // nav buttons will be updated on urlChanged / loadFinished
  }

  void updateNavButtons() {
    if (!webview_) return;
    auto hist = webview_->history();
    backBtn_->setEnabled(hist->canGoBack());
    forwardBtn_->setEnabled(hist->canGoForward());
    refreshBtn_->setEnabled(!webview_->url().isEmpty());
  }

  void setMinusEnabled(bool en) { if (minusBtn_) minusBtn_->setEnabled(en); }
  void setUpEnabled(bool en) { if (upBtn_) upBtn_->setEnabled(en); }
  void setDownEnabled(bool en) { if (downBtn_) downBtn_->setEnabled(en); }

  // assign a QWebEngineProfile by setting a new page for the internal view
  void setProfile(QWebEngineProfile *profile) {
    if (!webview_ || !profile) return;
    // assign a fresh page associated with the shared profile
    auto *page = new QWebEnginePage(profile, webview_);
    webview_->setPage(page);
  }

private:
  signals:
  void plusClicked(SplitFrameWidget *who);
  void minusClicked(SplitFrameWidget *who);
  void upClicked(SplitFrameWidget *who);
  void downClicked(SplitFrameWidget *who);
  void addressEdited(SplitFrameWidget *who, const QString &text);
  // Request that the window show/attach a shared DevTools view for this frame
  void devToolsRequested(SplitFrameWidget *who, QWebEnginePage *page, const QPoint &pos);

private:
  QLineEdit *address_ = nullptr;
  MyWebEngineView *webview_ = nullptr;
  QToolButton *upBtn_ = nullptr;
  QToolButton *downBtn_ = nullptr;
  QToolButton *plusBtn_ = nullptr;
  QToolButton *minusBtn_ = nullptr;
  QToolButton *backBtn_ = nullptr;
  QToolButton *forwardBtn_ = nullptr;
  QToolButton *refreshBtn_ = nullptr;
};


class SplitWindow : public QMainWindow {
  Q_OBJECT

public:
  enum LayoutMode { Vertical = 0, Horizontal = 1, Grid = 2 };

  SplitWindow(QWidget *parent = nullptr) : QMainWindow(parent) {
    setWindowTitle("Qt6 Splitter Hello");
    resize(800, 600);

    // No global toolbar; per-frame + / - buttons control sections.

    // create a shared persistent QWebEngineProfile for all frames so
    // cookies/localStorage/session state are persisted across frames and runs
    const QString dataRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataRoot);
    QDir().mkpath(dataRoot + "/cache");
    profile_ = new QWebEngineProfile(QStringLiteral("LiveStreamMultiChat"), this);
    profile_->setPersistentStoragePath(dataRoot);
    profile_->setCachePath(dataRoot + "/cache");
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
    QSettings layoutSettings("NightVsKnight", "LiveStreamMultiChat");
    int storedMode = layoutSettings.value("layoutMode", (int)Vertical).toInt();
    layoutMode_ = (LayoutMode)storedMode;
    switch (layoutMode_) {
      case Grid: gridAction->setChecked(true); break;
      case Horizontal: horizontalAction->setChecked(true); break;
      case Vertical: default: verticalAction->setChecked(true); break;
    }

    connect(gridAction, &QAction::triggered, this, [this]() { setLayoutMode(Grid); });
    connect(verticalAction, &QAction::triggered, this, [this]() { setLayoutMode(Vertical); });
    connect(horizontalAction, &QAction::triggered, this, [this]() { setLayoutMode(Horizontal); });

    // central scroll area to allow many sections
    auto *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    central_ = new QWidget();
    scroll->setWidget(central_);
    setCentralWidget(scroll);

    layout_ = new QVBoxLayout(central_);
    layout_->setContentsMargins(4, 4, 4, 4);
    layout_->setSpacing(6);

    // load persisted addresses (if present) otherwise start with one empty
    QSettings settings("NightVsKnight", "LiveStreamMultiChat");
    const QStringList saved = settings.value("addresses").toStringList();
    if (saved.isEmpty()) {
      addresses_.push_back(QString());
    } else {
      for (const QString &s : saved) {
        addresses_.push_back(s);
      }
    }
    // build initial UI
    rebuildSections((int)addresses_.size());
    // restore splitter sizes only once at startup (subsequent layout
    // selections/rebuilds should reset splitters to defaults)
    restoreSplitterSizes();
    restoredOnStartup_ = true;

    // restore saved window geometry and window state (position/size/state)
    QSettings geomSettings("NightVsKnight", "LiveStreamMultiChat");
    const QByteArray savedGeom = geomSettings.value("windowGeometry").toByteArray();
    if (!savedGeom.isEmpty()) restoreGeometry(savedGeom);
    const QByteArray savedState = geomSettings.value("windowState").toByteArray();
    if (!savedState.isEmpty()) restoreState(savedState);
  }

private slots:
  void rebuildSections(int n) {

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
      container = outer;
    }

    if (container) {
      layout_->addWidget(container, 1);
    }

    // add a final stretch with zero so that widgets entirely control spacing
    layout_->addStretch(0);
    central_->update();
  }

  void toggleDevToolsForFocusedFrame() {
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

  void onPlusFromFrame(SplitFrameWidget *who) {
    // use logicalIndex property assigned during rebuildSections
    const QVariant v = who->property("logicalIndex");
    if (!v.isValid()) return;
    int pos = v.toInt();
    addresses_.insert(addresses_.begin() + pos + 1, QString());
    // persist addresses
    QSettings settings("NightVsKnight", "LiveStreamMultiChat");
    QStringList list;
    for (const auto &a : addresses_) list << a;
    settings.setValue("addresses", list);
    // rebuild UI with the updated addresses_
    rebuildSections((int)addresses_.size());
  }

  void onUpFromFrame(SplitFrameWidget *who) {
    // move this frame up (towards index 0)
    const QVariant v = who->property("logicalIndex");
    if (!v.isValid()) return;
    int pos = v.toInt();
    if (pos <= 0) return; // already at top or not found
    std::swap(addresses_[pos], addresses_[pos - 1]);
    // persist addresses
    QSettings settings("NightVsKnight", "LiveStreamMultiChat");
    QStringList list;
    for (const auto &a : addresses_) list << a;
    settings.setValue("addresses", list);
    rebuildSections((int)addresses_.size());
  }

  void onDownFromFrame(SplitFrameWidget *who) {
    // move this frame down (towards larger indices)
    const QVariant v = who->property("logicalIndex");
    if (!v.isValid()) return;
    int pos = v.toInt();
    if (pos < 0 || pos >= (int)addresses_.size() - 1) return; // at bottom or not found
    std::swap(addresses_[pos], addresses_[pos + 1]);
    // persist addresses
    QSettings settings("NightVsKnight", "LiveStreamMultiChat");
    QStringList list;
    for (const auto &a : addresses_) list << a;
    settings.setValue("addresses", list);
    rebuildSections((int)addresses_.size());
  }

  void setLayoutMode(LayoutMode m) {
    // If the user re-selects the already-selected layout, treat that as a
    // request to reset splitters to their default sizes. Clear any saved
    // sizes for this layout and rebuild without saving the current sizes.
    if (m == layoutMode_) {
      QSettings settings("NightVsKnight", "LiveStreamMultiChat");
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
      QSettings settings("NightVsKnight", "LiveStreamMultiChat");
      const QString targetBase = QStringLiteral("splitterSizes/%1").arg(layoutModeKey(m));
      settings.remove(targetBase);
    }
    // Apply the new layout mode and persist it.
    layoutMode_ = m;
    QSettings layoutSettings("NightVsKnight", "LiveStreamMultiChat");
    layoutSettings.setValue("layoutMode", (int)layoutMode_);
    // Rebuild UI for the new layout (splitter sizes are only restored at startup)
    rebuildSections((int)addresses_.size());
  }

  void setHeightToScreen() {
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

  void onMinusFromFrame(SplitFrameWidget *who) {
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
    QSettings settings("NightVsKnight", "LiveStreamMultiChat");
    QStringList list;
    for (const auto &a : addresses_) list << a;
    settings.setValue("addresses", list);
    rebuildSections((int)addresses_.size());
  }

  void onAddressEdited(SplitFrameWidget *who, const QString &text) {
    const QVariant v = who->property("logicalIndex");
    if (!v.isValid()) return;
    int pos = v.toInt();
    if (pos < 0) return;
    if (pos < (int)addresses_.size()) {
      addresses_[pos] = text;
      // persist addresses list
      QSettings settings("NightVsKnight", "LiveStreamMultiChat");
      QStringList list;
      for (const auto &a : addresses_) list << a;
      settings.setValue("addresses", list);
    }
  }

  void closeEvent(QCloseEvent *event) override {
    // persist splitter sizes, addresses and window geometry on exit
    saveCurrentSplitterSizes();
    QSettings settings("NightVsKnight", "LiveStreamMultiChat");
    QStringList list;
    for (const auto &a : addresses_) list << a;
    settings.setValue("addresses", list);
    // persist window geometry
    settings.setValue("windowGeometry", saveGeometry());
    // persist window state (toolbars/dock state and maximized/minimized state)
    settings.setValue("windowState", saveState());
    QMainWindow::closeEvent(event);
  }

  // Persist sizes for splitters associated with the current layout mode.
  static QString layoutModeKey(LayoutMode m) {
    switch (m) {
      case Vertical: return QStringLiteral("vertical");
      case Horizontal: return QStringLiteral("horizontal");
      case Grid: default: return QStringLiteral("grid");
    }
  }

  void saveCurrentSplitterSizes() {
    if (currentSplitters_.empty()) return;
    QSettings settings("NightVsKnight", "LiveStreamMultiChat");
    const QString base = QStringLiteral("splitterSizes/%1").arg(layoutModeKey(layoutMode_));
    for (int i = 0; i < (int)currentSplitters_.size(); ++i) {
      QSplitter *s = currentSplitters_[i];
      if (!s) continue;
      const QList<int> sizes = s->sizes();
      QVariantList vl;
      for (int v : sizes) vl << v;
      settings.setValue(base + QStringLiteral("/%1").arg(i), vl);
    }
  }

  void restoreSplitterSizes() {
    if (currentSplitters_.empty()) return;
    QSettings settings("NightVsKnight", "LiveStreamMultiChat");
    const QString base = QStringLiteral("splitterSizes/%1").arg(layoutModeKey(layoutMode_));
    for (int i = 0; i < (int)currentSplitters_.size(); ++i) {
      QSplitter *s = currentSplitters_[i];
      if (!s) continue;
      const QVariant v = settings.value(base + QStringLiteral("/%1").arg(i));
      if (!v.isValid()) continue;
      const QVariantList vl = v.toList();
      if (vl.isEmpty()) continue;
      QList<int> sizes;
      sizes.reserve(vl.size());
      for (const QVariant &qv : vl) sizes << qv.toInt();
      if (!sizes.isEmpty()) s->setSizes(sizes);
    }
  }

  // Slot: a child frame requested DevTools for its page.
  // Use a single shared DevTools view for the whole window
  // and attach it to the requested page.
  void onFrameDevToolsRequested(SplitFrameWidget *who, QWebEnginePage *page, const QPoint &pos) {
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

  // Create (if needed) and attach the single shared DevTools view to the
  // provided inspected page. This mirrors the previous per-frame behavior
  // but uses a single floating DevTools view for all frames.
  void createAndAttachSharedDevToolsForPage(QWebEnginePage *page) {
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

private:
  QWidget *central_ = nullptr;
  QVBoxLayout *layout_ = nullptr;
  // QSpinBox removed; per-frame buttons control section count.
  std::vector<QString> addresses_;
  QWebEngineProfile *profile_ = nullptr;
  LayoutMode layoutMode_ = Vertical;
  std::vector<QSplitter*> currentSplitters_;
  QWebEngineView *sharedDevToolsView_ = nullptr;
  bool restoredOnStartup_ = false;
};

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  SplitWindow w;
  w.show();
  return app.exec();
}

#include "main.moc"
