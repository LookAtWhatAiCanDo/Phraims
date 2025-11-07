#include "SplitFrameWidget.h"
#include "MyWebEngineView.h"
#include "EscapeFilter.h"
#include "DomPatch.h"
#include <QApplication>
#include <QDebug>
#include <QEvent>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMainWindow>
#include <QPalette>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWebEngineHistory>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>

SplitFrameWidget::SplitFrameWidget(int index, QWidget *parent) : QFrame(parent) {
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

  innerLayout_ = new QVBoxLayout(this);
  innerLayout_->setContentsMargins(6, 6, 6, 6);
  innerLayout_->setSpacing(6);

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
  // show the left-most characters when not being edited and provide
  // a hover tooltip containing the full URL
  address_->setToolTip(address_->text());
  address_->installEventFilter(this);
  address_->setCursorPosition(0);
  connect(address_, &QLineEdit::textChanged, this, [this](const QString &t){
    address_->setToolTip(t);
  });
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

  innerLayout_->addLayout(topRow);

  // web view content area
  webview_ = new MyWebEngineView(this);
  webview_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  innerLayout_->addWidget(webview_, 1);

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
    // When updating programmatically, ensure unfocused fields show the
    // left-most characters rather than scrolled to the end.
    if (!address_->hasFocus()) address_->setCursorPosition(0);
    // update nav button states
    updateNavButtons();
    // re-apply any DOM patches when the URL changes (helps single-page apps)
    if (webview_ && webview_->page()) applyDomPatchesToPage(webview_->page());
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

QWebEnginePage *SplitFrameWidget::page() const { return webview_ ? webview_->page() : nullptr; }

QString SplitFrameWidget::address() const { return address_->text(); }

void SplitFrameWidget::setAddress(const QString &s) {
    address_->setText(s);
    // if the user is not actively editing, ensure the left-most
    // characters are visible by resetting the cursor position
    if (!address_->hasFocus()) address_->setCursorPosition(0);
    applyAddress(s);
}

void SplitFrameWidget::applyAddress(const QString &s) {
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

void SplitFrameWidget::updateNavButtons() {
  if (!webview_) return;
  auto hist = webview_->history();
  backBtn_->setEnabled(hist->canGoBack());
  forwardBtn_->setEnabled(hist->canGoForward());
  refreshBtn_->setEnabled(!webview_->url().isEmpty());
}

bool SplitFrameWidget::eventFilter(QObject *watched, QEvent *event) {
  if (watched == address_) {
    if (event->type() == QEvent::FocusOut) {
      // When the user finishes editing (or focus leaves), ensure the
      // displayed portion starts at the left so the left-most characters
      // are visible.
      address_->setCursorPosition(0);
    }
    // Let the line edit handle the event as well
    return false;
  }
  return QFrame::eventFilter(watched, event);
}

void SplitFrameWidget::setMinusEnabled(bool en) { if (minusBtn_) minusBtn_->setEnabled(en); }
void SplitFrameWidget::setUpEnabled(bool en) { if (upBtn_) upBtn_->setEnabled(en); }
void SplitFrameWidget::setDownEnabled(bool en) { if (downBtn_) downBtn_->setEnabled(en); }

void SplitFrameWidget::setProfile(QWebEngineProfile *profile) {
  if (!webview_ || !profile) return;
  // assign a fresh page associated with the shared profile
  auto *page = new QWebEnginePage(profile, webview_);
  webview_->setPage(page);
  // Ensure DOM patches are applied on every load for this page.
  QObject::connect(page, &QWebEnginePage::loadFinished, page, [page](bool) {
    // apply patches after each load
    applyDomPatchesToPage(page);
  });
  // Ensure the page has fullscreen support enabled (should be true by default
  // but being explicit helps diagnose platform differences).
  page->settings()->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
  qDebug() << "SplitFrameWidget::setProfile: FullScreenSupportEnabled=" << page->settings()->testAttribute(QWebEngineSettings::FullScreenSupportEnabled);

  // Log and (optionally) auto-grant feature permissions that some players
  // request when entering fullscreen, like mouse lock. This will help
  // diagnose permission-denied problems.
  QObject::connect(page, &QWebEnginePage::permissionRequested, this, [page](QWebEnginePermission permissionRequest){
    auto origin = permissionRequest.origin();
    auto permissionType = permissionRequest.permissionType();
    qDebug() << "SplitFrameWidget::featurePermissionRequested: origin=" << origin << " permissionType=" << permissionType;
    // Auto-grant mouse lock which some fullscreen players use.
    if (permissionType == QWebEnginePermission::PermissionType::MouseLock) {
      qDebug() << "SplitFrameWidget: granting MouseLock for" << origin;
      permissionRequest.grant();
      qDebug() << "SplitFrameWidget: granted MouseLock for" << origin;
      return;
    }
    // For other features, reject the request but log for diagnostics.
    qDebug() << "SplitFrameWidget: denying" << permissionType << "for" << origin;
    permissionRequest.deny();
    qDebug() << "SplitFrameWidget: denied" << permissionType << "for" << origin;
  });
  // Honor HTML5 fullscreen requests (e.g., YouTube fullscreen button).
  qDebug() << "SplitFrameWidget::setProfile: connecting fullScreenRequested for page" << page << "parent webview=" << webview_;
  QObject::connect(page, &QWebEnginePage::fullScreenRequested, this, &SplitFrameWidget::handleFullScreenRequested);
  qDebug() << "SplitFrameWidget::setProfile: connected fullScreenRequested";
}

void SplitFrameWidget::handleFullScreenRequested(QWebEngineFullScreenRequest request) {
  qDebug() << "SplitFrameWidget::handleFullScreenRequested: received request toggleOn=" << request.toggleOn() << " origin=" << request.origin().toString();
  if (request.toggleOn()) {
    qDebug() << "SplitFrameWidget: entering fullscreen";
    // Enter fullscreen
    if (fullScreenWindow_) {
      qDebug() << "SplitFrameWidget: already in fullscreen, accepting request";
      request.accept();
      return;
    }
    // Create a dedicated top-level QMainWindow for fullscreen. Using a
    // full QMainWindow (rather than a child widget) ensures the OS treats
    // this window as a full application-space fullscreen window so the
    // whole window/space is switched on macOS when shown fullscreen.
    QMainWindow *fsw = new QMainWindow(nullptr);
    fsw->setAttribute(Qt::WA_DeleteOnClose);
    fsw->setWindowTitle(tr("Fullscreen"));
    fsw->setWindowState(Qt::WindowFullScreen);
    fsw->setWindowFlag(Qt::Window, true);

    // reparent the webview into the fullscreen main window
    previousParent_ = webview_->parentWidget();
    qDebug() << "SplitFrameWidget: previousParent=" << previousParent_ << " webview=" << webview_;
    webview_->setParent(fsw);
    // place the webview as the central widget so it fills the fullscreen window
    fsw->setCentralWidget(webview_);
    // Ensure keyboard focus goes to the webview so it (and page JS) can
    // receive key events, and install an EscapeFilter on both the
    // fullscreen window and the webview so ESC is caught regardless of
    // which widget has focus.
    webview_->setFocus(Qt::OtherFocusReason);
    auto *ef = new EscapeFilter(webview_, fsw);
    // keep a guarded pointer so we can remove the filter when exiting
    // fullscreen to avoid leaving stale filters on the webview.
    escapeFilter_ = ef;
    fsw->installEventFilter(ef);
    if (webview_) webview_->installEventFilter(ef);
    // Also install as a global application filter so we catch Escape even
    // if the key event is dispatched at a level the view/window doesn't
    // receive (some WebEngine content consumes events in the web process).
    if (qApp) qApp->installEventFilter(ef);

    fullScreenWindow_ = fsw;
    request.accept();
    fsw->showFullScreen();

    // Hide the original top-level window so the app chrome doesn't remain
    // visible on the original desktop/space while the page is fullscreen.
    hidWindowForFullscreen_ = false;
    if (QWidget *top = this->window()) {
      qDebug() << "SplitFrameWidget: hiding top-level window while page is fullscreen";
      // remember previous window state so we can restore it
      previousTopWindowState_ = top->windowState();
      top->hide();
      hidWindowForFullscreen_ = true;
    }

    // Ensure that if the fullscreen window is closed externally we
    // restore the webview back into this frame.
    QPointer<QWidget> fswGuard(fsw);
    connect(fsw, &QObject::destroyed, this, [this, fswGuard](QObject *) {
      qDebug() << "SplitFrameWidget: fullscreen window destroyed, restoring webview";
      // Remove event filter from the webview if present so we don't leave
      // a dangling filter installed when the fullscreen helper is gone.
      if (webview_ && escapeFilter_) {
        webview_->removeEventFilter(escapeFilter_);
      }
      if (qApp && escapeFilter_) {
        qApp->removeEventFilter(escapeFilter_);
      }
      escapeFilter_ = nullptr;

      // Restore visibility of this frame and possibly the top-level
      // window if we hid it when entering fullscreen.
      if (hidWindowForFullscreen_) {
        if (QWidget *top = this->window()) {
          qDebug() << "SplitFrameWidget: restoring top-level window after fullscreen";
          top->show();
          // restore previous window state (ensure we leave fullscreen if it
          // wasn't previously full-screen).
          if (previousTopWindowState_ & Qt::WindowFullScreen) {
            top->setWindowState(previousTopWindowState_);
          } else {
            top->showNormal();
          }
          top->raise();
          top->activateWindow();
        }
        hidWindowForFullscreen_ = false;
        previousTopWindowState_ = Qt::WindowNoState;
      }
      this->setVisible(true);

      // If the webview is not already parented to this frame, move it back.
      if (webview_ && webview_->parentWidget() != this) {
        webview_->setParent(this);
        if (innerLayout_) innerLayout_->addWidget(webview_, 1);
      }
      fullScreenWindow_ = nullptr;
    });
    return;
  }

  qDebug() << "SplitFrameWidget: exiting fullscreen";
  // Exit fullscreen
  if (!fullScreenWindow_) {
    qDebug() << "SplitFrameWidget: no fullscreen window present, accepting request and returning";
    request.accept();
    return;
  }

  // Remove any event filter installed on the webview by the fullscreen
  // helper before restoring parentage so we don't leave stale filters.
  if (webview_ && escapeFilter_) {
    webview_->removeEventFilter(escapeFilter_);
  }
  if (qApp && escapeFilter_) {
    qApp->removeEventFilter(escapeFilter_);
  }
  escapeFilter_ = nullptr;

  // Restore visibility of this frame and possibly the top-level window
  // if we hid it when entering fullscreen.
  if (hidWindowForFullscreen_) {
    if (QWidget *top = this->window()) {
      qDebug() << "SplitFrameWidget: restoring top-level window after fullscreen";
      top->show();
      if (previousTopWindowState_ & Qt::WindowFullScreen) {
        top->setWindowState(previousTopWindowState_);
      } else {
        top->showNormal();
      }
      top->raise();
      top->activateWindow();
    }
    hidWindowForFullscreen_ = false;
    previousTopWindowState_ = Qt::WindowNoState;
  }
  this->setVisible(true);

  // Reparent the webview back into this frame's layout
  webview_->setParent(this);
  if (innerLayout_) innerLayout_->addWidget(webview_, 1);

  // Close the fullscreen window (it will be deleted due to WA_DeleteOnClose)
  QWidget *w = fullScreenWindow_;
  fullScreenWindow_ = nullptr;
  if (w) {
    qDebug() << "SplitFrameWidget: closing fullscreen window" << w;
    w->close();
  }
  request.accept();
}
