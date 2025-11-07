#pragma once

#include <QObject>
#include <QPointer>

class QWebEngineView;

/**
 * Event filter that catches Escape key presses on the fullscreen host
 * widget and instructs the page to exit fullscreen. Kept minimal and
 * parented to the fullscreen widget so it is deleted with it.
 */
class EscapeFilter : public QObject {
  Q_OBJECT
public:
  explicit EscapeFilter(QWebEngineView *view, QObject *parent = nullptr);
protected:
  bool eventFilter(QObject *watched, QEvent *event) override;
private:
  QPointer<QWebEngineView> view_;
};
