#pragma once

#include <QDebug>
#include <QEvent>
#include <QKeyEvent>
#include <QObject>
#include <QPointer>
#include <QWebEngineView>
#include <QWebEnginePage>

/**
 * Event filter that catches Escape key presses on the fullscreen host
 * widget and instructs the page to exit fullscreen. Kept minimal and
 * parented to the fullscreen widget so it is deleted with it.
 */
class EscapeFilter : public QObject {
  Q_OBJECT
public:
  explicit EscapeFilter(QWebEngineView *view, QObject *parent = nullptr) 
    : QObject(parent), view_(view) {}

protected:
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
  QPointer<QWebEngineView> view_;
};
