#pragma once

#include <QDebug>
#include <QEvent>
#include <QKeyEvent>
#include <QObject>
#include <QPointer>
#include <QWebEngineView>
#include <QWebEnginePage>

/**
 * @brief Event filter that catches Escape key presses during fullscreen mode.
 *
 * This filter is installed on fullscreen host widgets to intercept Escape key presses
 * and instruct the web page to exit fullscreen. The filter is kept minimal and is
 * parented to the fullscreen widget so it is automatically deleted with it.
 */
class EscapeFilter : public QObject {
  Q_OBJECT
public:
  /**
   * @brief Constructs an EscapeFilter for the given web view.
   * @param view The QWebEngineView to monitor for fullscreen exit requests
   * @param parent The parent QObject for automatic cleanup
   */
  explicit EscapeFilter(QWebEngineView *view, QObject *parent = nullptr) 
    : QObject(parent), view_(view) {}

protected:
  /**
   * @brief Filters events to catch Escape key presses and exit fullscreen.
   * @param watched The object being watched for events
   * @param event The event to filter
   * @return true if the event was handled (Escape key pressed), false otherwise
   */
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (event->type() == QEvent::KeyPress) {
      QKeyEvent *ke = static_cast<QKeyEvent*>(event);
      if (ke && ke->key() == Qt::Key_Escape) {
        qDebug() << "EscapeFilter: Escape pressed, requesting document.exitFullscreen()";
        if (view_ && view_->page()) {
          view_->page()->runJavaScript("if (document.exitFullscreen) { document.exitFullscreen(); } else if (document.webkitExitFullscreen) { document.webkitExitFullscreen(); }");
        }
        return true;
      }
    }
    return QObject::eventFilter(watched, event);
  }

private:
  /** @brief Guarded pointer to the web view to control fullscreen state */
  QPointer<QWebEngineView> view_;
};
