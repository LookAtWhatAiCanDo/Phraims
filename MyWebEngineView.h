#pragma once

#include <QContextMenuEvent>
#include <QMenu>
#include <QWebEngineView>
#include <QWebEnginePage>

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
