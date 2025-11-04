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
  SplitWindow(QWidget *parent = nullptr) : QMainWindow(parent) {
    setWindowTitle("Qt6 Splitter Hello");
    resize(800, 600);

    // No global toolbar; per-frame + / - buttons control sections.

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

    // create n equal sections. Use stretch=1 on each widget so they share space equally.
    for (int i = 0; i < n; ++i) {
      auto *frame = new SplitFrameWidget(i);
      // restore saved address if present
      frame->setAddress(addresses_[i]);

      // wire signals from the frame to the window handlers
      connect(frame, &SplitFrameWidget::plusClicked, this, &SplitWindow::onPlusFromFrame);
      connect(frame, &SplitFrameWidget::minusClicked, this, &SplitWindow::onMinusFromFrame);
      connect(frame, &SplitFrameWidget::addressEdited, this, &SplitWindow::onAddressEdited);
      connect(frame, &SplitFrameWidget::upClicked, this, &SplitWindow::onUpFromFrame);
      connect(frame, &SplitFrameWidget::downClicked, this, &SplitWindow::onDownFromFrame);

      // enable/disable minus/up/down buttons depending on how many sections
      frame->setMinusEnabled(n > 1);
      frame->setUpEnabled(i > 0);
      frame->setDownEnabled(i < n - 1);

      layout_->addWidget(frame, 1); // stretch=1 -> equal share
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
    QMainWindow::closeEvent(event);
  }

 private:
  QWidget *central_ = nullptr;
  QVBoxLayout *layout_ = nullptr;
  // QSpinBox removed; per-frame buttons control section count.
  std::vector<QString> addresses_;
};

int main(int argc, char **argv) {
  QApplication app(argc, argv);

  SplitWindow w;
  w.show();

  return app.exec();
}

#include "main.moc"
