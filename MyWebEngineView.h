#pragma once

#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QMenu>
#include <QUrl>
#include <QUrlQuery>
#include <QWebEngineView>
#include <QWebEnginePage>

/**
 * @brief Custom QWebEngineView with enhanced context menu and window creation behavior.
 *
 * This subclass provides:
 * 1. A default context menu with navigation, edit, and translation actions
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
   * @brief Shows a custom context menu with navigation, edit, translation, and inspect actions.
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
    // Translate action
    auto translate = menu.addAction(tr("Translate…"));
    menu.addSeparator();
    // Inspect...
    auto inspect = menu.addAction(tr("Inspect…"));

    auto pos = event->pos();
    auto selected = menu.exec(mapToGlobal(pos));
    
    if (selected == inspect) {
      emit devToolsRequested(page, pos);
    } else if (selected == translate) {
      handleTranslateAction();
    }
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

private:
  /**
   * @brief Handles the translate action from the context menu.
   *
   * If text is selected, opens Google Translate with the selected text.
   * Otherwise, opens Google Translate with the current page URL for
   * full page translation. Uses the system default browser to open
   * the translation URL.
   */
  void handleTranslateAction() {
    auto page = this->page();
    if (!page) return;

    QString selectedText = page->selectedText();
    QUrl translateUrl;

    if (!selectedText.isEmpty()) {
      // Translate selected text
      QUrlQuery query;
      query.addQueryItem("text", selectedText);
      query.addQueryItem("op", "translate");
      translateUrl.setUrl("https://translate.google.com/");
      translateUrl.setQuery(query);
    } else {
      // Translate entire page
      QString currentUrl = page->url().toString();
      if (!currentUrl.isEmpty()) {
        QUrlQuery query;
        query.addQueryItem("u", currentUrl);
        query.addQueryItem("sl", "auto");
        query.addQueryItem("tl", "en");
        translateUrl.setUrl("https://translate.google.com/translate");
        translateUrl.setQuery(query);
      }
    }

    if (!translateUrl.isEmpty()) {
      QDesktopServices::openUrl(translateUrl);
    }
  }
};
