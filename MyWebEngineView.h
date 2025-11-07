#pragma once

#include <QContextMenuEvent>
#include <QMenu>
#include <QWebEngineView>
#include <QWebEnginePage>

/**
 * @brief Custom QWebEngineView with enhanced context menu and window creation behavior.
 *
 * This subclass provides:
 * 1. A default context menu with navigation and edit actions
 * 2. An override of createWindow() to load popup targets in the same view
 *    instead of opening new windows
 */
class MyWebEngineView : public QWebEngineView {
  Q_OBJECT
public:
  using QWebEngineView::QWebEngineView;

signals:
  /**
   * @brief Emitted when the user requests to open DevTools via the context menu.
   * @param source The QWebEnginePage to inspect
   * @param pos The position where the context menu was opened
   */
  void devToolsRequested(QWebEnginePage *source, const QPoint &pos);

protected:
  /**
   * @brief Shows a custom context menu with navigation, edit, and inspect actions.
   * @param event The context menu event containing the menu position
   */
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

  /**
   * @brief Overrides window creation to load popups in the current view.
   * @param type The type of window being requested
   * @return This view instance, causing the popup to load in place
   */
  MyWebEngineView *createWindow(QWebEnginePage::WebWindowType type) override {
    Q_UNUSED(type);
    // Load popup targets in the same view. Returning 'this' tells the
    // engine to use the current view for the new window's contents.
    return this;
  }
};
