#include "EscapeFilter.h"
#include <QDebug>
#include <QEvent>
#include <QKeyEvent>
#include <QWebEngineView>
#include <QWebEnginePage>

EscapeFilter::EscapeFilter(QWebEngineView *view, QObject *parent) : QObject(parent), view_(view) {}

bool EscapeFilter::eventFilter(QObject *watched, QEvent *event) {
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
