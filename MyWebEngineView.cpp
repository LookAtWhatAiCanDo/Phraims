#include "MyWebEngineView.h"
#include <QContextMenuEvent>
#include <QMenu>
#include <QWebEnginePage>

void MyWebEngineView::contextMenuEvent(QContextMenuEvent *event) {
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

MyWebEngineView *MyWebEngineView::createWindow(QWebEnginePage::WebWindowType type) {
  Q_UNUSED(type);
  // Load popup targets in the same view. Returning 'this' tells the
  // engine to use the current view for the new window's contents.
  return this;
}
