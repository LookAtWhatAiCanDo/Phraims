#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolButton>
#include <QLineEdit>
#include <QWebEngineView>
#include <QWebEngineHistory>
#include <vector>
#include <QLabel>
#include <QFrame>
#include <QLineEdit>
#include <QScrollArea>
#include <QPalette>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QWebEngineProfile>
#include <QWebEnginePage>
#include <QMenuBar>
#include <QScreen>
#include <QGuiApplication>
#include <QActionGroup>
#include <QSplitter>
#include <QGridLayout>
#include <cmath>
#include <QMessageBox>

// Simple Qt6 Widgets app that divides the main area into N equal sections.
// The user controls the number of sections with + / - buttons or the spinbox.

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
    webview_ = new QWebEngineView(this);
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

    connect(webview_, &QWebEngineView::urlChanged, this, [this](const QUrl &url) {
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

    connect(webview_, &QWebEngineView::loadStarted, this, [this]() { refreshBtn_->setEnabled(true); });
    connect(webview_, &QWebEngineView::loadFinished, this, [this](bool ok) {
      Q_UNUSED(ok);
      updateNavButtons();
    });
  }

  QString address() const { return address_->text(); }
  void setAddress(const QString &s) { address_->setText(s); applyAddress(s); }

  void applyAddress(const QString &s) {
    const QString trimmed = s.trimmed();
    if (trimmed.isEmpty()) {
      // show instruction HTML instead of loading
      const QString html =
          QStringLiteral("<html><body><div style=\"font-family: sans-serif; color: #666; padding: 20px;\">Enter an address above and press Enter to load a page.</div></body></html>");
      webview_->setHtml(html);
      refreshBtn_->setEnabled(false);
      backBtn_->setEnabled(false);
      forwardBtn_->setEnabled(false);
      return;
    }

    QUrl url = QUrl::fromUserInput(trimmed);
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

 signals:
  void plusClicked(SplitFrameWidget *who);
  void minusClicked(SplitFrameWidget *who);
  void upClicked(SplitFrameWidget *who);
  void downClicked(SplitFrameWidget *who);
  void addressEdited(SplitFrameWidget *who, const QString &text);

 private:
  QLineEdit *address_ = nullptr;
  QWebEngineView *webview_ = nullptr;
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

    // restore saved window geometry (position/size) if present
    QSettings geomSettings("NightVsKnight", "LiveStreamMultiChat");
    const QByteArray savedGeom = geomSettings.value("windowGeometry").toByteArray();
    if (!savedGeom.isEmpty()) restoreGeometry(savedGeom);

    // add a simple View menu with a helper to set the window height to the
    // screen available height (preserves width and x position)
    auto *viewMenu = menuBar()->addMenu(tr("View"));
    QAction *setHeightAction = viewMenu->addAction(tr("Set height to screen"));
    connect(setHeightAction, &QAction::triggered, this, &SplitWindow::setHeightToScreen);

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
    rebuildSections((int)addresses_.size());
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

    // create n sections according to the selected layout mode.
    QWidget *container = nullptr;
    if (layoutMode_ == Vertical || layoutMode_ == Horizontal) {
      QSplitter *split = new QSplitter(layoutMode_ == Vertical ? Qt::Vertical : Qt::Horizontal);
      for (int i = 0; i < n; ++i) {
        auto *frame = new SplitFrameWidget(i);
        frame->setProfile(profile_);
        frame->setAddress(addresses_[i]);
        connect(frame, &SplitFrameWidget::plusClicked, this, &SplitWindow::onPlusFromFrame);
        connect(frame, &SplitFrameWidget::minusClicked, this, &SplitWindow::onMinusFromFrame);
        connect(frame, &SplitFrameWidget::addressEdited, this, &SplitWindow::onAddressEdited);
        connect(frame, &SplitFrameWidget::upClicked, this, &SplitWindow::onUpFromFrame);
        connect(frame, &SplitFrameWidget::downClicked, this, &SplitWindow::onDownFromFrame);
        frame->setMinusEnabled(n > 1);
        frame->setUpEnabled(i > 0);
        frame->setDownEnabled(i < n - 1);
        split->addWidget(frame);
      }
      container = split;
    } else { // Grid mode: nested splitters for resizable grid
      // Create a vertical splitter containing one horizontal splitter per row.
      QSplitter *outer = new QSplitter(Qt::Vertical);
      int rows = (int)std::ceil(std::sqrt((double)n));
      int cols = (n + rows - 1) / rows;
      int idx = 0;
      for (int r = 0; r < rows; ++r) {
        // how many items in this row
        int itemsInRow = std::min(cols, n - idx);
        if (itemsInRow <= 0) break;
        QSplitter *rowSplit = new QSplitter(Qt::Horizontal);
        for (int c = 0; c < itemsInRow; ++c) {
          auto *frame = new SplitFrameWidget(idx);
          frame->setProfile(profile_);
          frame->setAddress(addresses_[idx]);
          connect(frame, &SplitFrameWidget::plusClicked, this, &SplitWindow::onPlusFromFrame);
          connect(frame, &SplitFrameWidget::minusClicked, this, &SplitWindow::onMinusFromFrame);
          connect(frame, &SplitFrameWidget::addressEdited, this, &SplitWindow::onAddressEdited);
          connect(frame, &SplitFrameWidget::upClicked, this, &SplitWindow::onUpFromFrame);
          connect(frame, &SplitFrameWidget::downClicked, this, &SplitWindow::onDownFromFrame);
          frame->setMinusEnabled(n > 1);
          frame->setUpEnabled(idx > 0);
          frame->setDownEnabled(idx < n - 1);
          rowSplit->addWidget(frame);
          ++idx;
        }
        outer->addWidget(rowSplit);
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

  void onPlusFromFrame(SplitFrameWidget *who) {
    // find index of the emitter within layout
    int pos = -1;
    int widgetIndex = 0;
    for (int i = 0; i < layout_->count(); ++i) {
      QLayoutItem *it = layout_->itemAt(i);
      QWidget *w = it ? it->widget() : nullptr;
      if (!w) continue;
      if (w == who) {
        pos = widgetIndex;
        break;
      }
      ++widgetIndex;
    }
    if (pos < 0) return;

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
    int pos = -1;
    int widgetIndex = 0;
    for (int i = 0; i < layout_->count(); ++i) {
      QLayoutItem *it = layout_->itemAt(i);
      QWidget *w = it ? it->widget() : nullptr;
      if (!w) continue;
      if (w == who) {
        pos = widgetIndex;
        break;
      }
      ++widgetIndex;
    }
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
    int pos = -1;
    int widgetIndex = 0;
    for (int i = 0; i < layout_->count(); ++i) {
      QLayoutItem *it = layout_->itemAt(i);
      QWidget *w = it ? it->widget() : nullptr;
      if (!w) continue;
      if (w == who) {
        pos = widgetIndex;
        break;
      }
      ++widgetIndex;
    }
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
    // Always apply the layout mode and rebuild. This lets the user re-select
    // the same layout to reset any splitter sizes or other transient state.
    layoutMode_ = m;
    QSettings layoutSettings("NightVsKnight", "LiveStreamMultiChat");
    layoutSettings.setValue("layoutMode", (int)layoutMode_);
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

    int pos = -1;
    int widgetIndex = 0;
    for (int i = 0; i < layout_->count(); ++i) {
      QLayoutItem *it = layout_->itemAt(i);
      QWidget *w = it ? it->widget() : nullptr;
      if (!w) continue;
      if (w == who) {
        pos = widgetIndex;
        break;
      }
      ++widgetIndex;
    }
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
    int pos = -1;
    int widgetIndex = 0;
    for (int i = 0; i < layout_->count(); ++i) {
      QLayoutItem *it = layout_->itemAt(i);
      QWidget *w = it ? it->widget() : nullptr;
      if (!w) continue;
      if (w == who) {
        pos = widgetIndex;
        break;
      }
      ++widgetIndex;
    }
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
    // persist current addresses on exit
    QSettings settings("NightVsKnight", "LiveStreamMultiChat");
    QStringList list;
    for (const auto &a : addresses_) list << a;
    settings.setValue("addresses", list);
    // persist window geometry
    settings.setValue("windowGeometry", saveGeometry());
    QMainWindow::closeEvent(event);
  }

 private:
  QWidget *central_ = nullptr;
  QVBoxLayout *layout_ = nullptr;
  // QSpinBox removed; per-frame buttons control section count.
  std::vector<QString> addresses_;
  QWebEngineProfile *profile_ = nullptr;
  LayoutMode layoutMode_ = Vertical;
};

int main(int argc, char **argv) {
  QApplication app(argc, argv);

  SplitWindow w;
  w.show();

  return app.exec();
}

#include "main.moc"
