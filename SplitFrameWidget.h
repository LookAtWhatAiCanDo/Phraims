#pragma once

#include <QFrame>
#include <QPointer>

class QVBoxLayout;
class QLineEdit;
class QToolButton;
class QWebEngineProfile;
class QWebEnginePage;
class QWebEngineFullScreenRequest;
class MyWebEngineView;
class EscapeFilter;

/**
 * A self-contained frame used for each split section.
 * Contains controls at its top (ex: back, forward, address, ...) and a simple content area below.
 */
class SplitFrameWidget : public QFrame {
  Q_OBJECT

public:
  SplitFrameWidget(int index, QWidget *parent = nullptr);

  QWebEnginePage *page() const;

  QString address() const;
  void setAddress(const QString &s);
  void applyAddress(const QString &s);
  void updateNavButtons();

  bool eventFilter(QObject *watched, QEvent *event) override;

  void setMinusEnabled(bool en);
  void setUpEnabled(bool en);
  void setDownEnabled(bool en);

  // assign a QWebEngineProfile by setting a new page for the internal view
  void setProfile(QWebEngineProfile *profile);

private slots:
  /**
   * Handler for HTML5 fullscreen requests from the page (e.g., YouTube
   * fullscreen button). The QWebEngineFullScreenRequest is accepted and
   * the internal webview is reparented into a top-level full-screen
   * window while the request is active.
   */
  void handleFullScreenRequested(QWebEngineFullScreenRequest request);

signals:
  void plusClicked(SplitFrameWidget *who);
  void minusClicked(SplitFrameWidget *who);
  void upClicked(SplitFrameWidget *who);
  void downClicked(SplitFrameWidget *who);
  void addressEdited(SplitFrameWidget *who, const QString &text);
  /**
   * Request that the window show/attach a shared DevTools view for this frame
   */
  void devToolsRequested(SplitFrameWidget *who, QWebEnginePage *page, const QPoint &pos);

private:
  QVBoxLayout *innerLayout_ = nullptr;
  QLineEdit *address_ = nullptr;
  MyWebEngineView *webview_ = nullptr;
  QToolButton *upBtn_ = nullptr;
  QToolButton *downBtn_ = nullptr;
  QToolButton *plusBtn_ = nullptr;
  QToolButton *minusBtn_ = nullptr;
  QToolButton *backBtn_ = nullptr;
  QToolButton *forwardBtn_ = nullptr;
  QToolButton *refreshBtn_ = nullptr;

  // When a page requests fullscreen we create a top-level window and
  // reparent the webview into it. Use QPointer guards to avoid dangling
  // pointers during teardown.
  QPointer<QWidget> fullScreenWindow_;
  QPointer<QWidget> previousParent_;
  // Event filter used while a frame is fullscreen so we can catch Escape
  // key presses regardless of which child widget currently has focus.
  QPointer<EscapeFilter> escapeFilter_;
  // If true we hid the top-level window when entering fullscreen and
  // should restore it on exit.
  bool hidWindowForFullscreen_ = false;
  // Remember the previous window state of the top-level window so we
  // can restore it (e.g., exit fullscreen) when leaving page fullscreen.
  Qt::WindowStates previousTopWindowState_ = Qt::WindowNoState;
};
