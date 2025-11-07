#pragma once

#include <QWebEngineView>

class QWebEnginePage;

/**
 * QWebEngineView subclass to:
 * 1. provide default context menu
 * 2. override createWindow to just navigate to address rather than opening new window
 */
class MyWebEngineView : public QWebEngineView {
  Q_OBJECT
public:
  using QWebEngineView::QWebEngineView;

signals:
  void devToolsRequested(QWebEnginePage *source, const QPoint &pos);

protected:
  void contextMenuEvent(QContextMenuEvent *event) override;
  MyWebEngineView *createWindow(QWebEnginePage::WebWindowType type) override;
};
