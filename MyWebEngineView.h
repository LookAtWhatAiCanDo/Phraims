#pragma once

#include <QContextMenuEvent>
#include <QMenu>
#include <QDebug>
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

  /**
   * @brief Emitted when the user requests translation via the context menu.
   * @param translateUrl The Google Translate URL to open in a new window
   *
   * The URL will contain either selected text or the current page URL
   * for translation. The parent widget should open this in a new window.
   */
  void translateRequested(const QUrl &translateUrl);

protected:
  /**
   * @brief Shows a custom context menu with navigation, edit, translation, and inspect actions.
   * @param event The context menu event containing the menu position
   */
  void contextMenuEvent(QContextMenuEvent *event) override {
    // Build the menu on the heap because we show it asynchronously after
    // running JS to expand the selection at the click point.
    QMenu *menu = new QMenu(this);
    auto page = this->page();

    // Common navigation actions
    if (page) {
      if (auto *a = page->action(QWebEnginePage::Back))    menu->addAction(a);
      if (auto *a = page->action(QWebEnginePage::Forward)) menu->addAction(a);
      if (auto *a = page->action(QWebEnginePage::Reload))  menu->addAction(a);
      menu->addSeparator();
      // Edit actions
      if (auto *a = page->action(QWebEnginePage::Cut))       menu->addAction(a);
      if (auto *a = page->action(QWebEnginePage::Copy))      menu->addAction(a);
      if (auto *a = page->action(QWebEnginePage::Paste))     menu->addAction(a);
      if (auto *a = page->action(QWebEnginePage::SelectAll)) menu->addAction(a);
    } else {
      // If no page, still provide basic menu entries (empty placeholders)
    }
    menu->addSeparator();
    // Translate action
    auto translate = menu->addAction(tr("Translate…"));
    menu->addSeparator();
    // Inspect...
    auto inspect = menu->addAction(tr("Inspect…"));

    // Show the menu after we try to select a contiguous run of characters
    // under the mouse. Use the page's contextMenuData() position (document
    // coordinates) and run JS to expand the selection there. This is
    // asynchronous, so exec the menu in the JS callback.
    const QPoint widgetPos = event->pos();
    const QPoint globalPos = mapToGlobal(widgetPos);
    qDebug() << "MyWebEngineView::contextMenuEvent: widgetPos=" << widgetPos << " globalPos=" << globalPos << " pagePresent=" << (page!=nullptr);

    if (!page) {
      qDebug() << "MyWebEngineView::contextMenuEvent: no page available, showing fallback menu";
      // Fallback: no page, show immediately
      QAction *selected = menu->exec(globalPos);
      if (selected == inspect) {
        qDebug() << "MyWebEngineView::contextMenuEvent: inspect selected (no page)";
        emit devToolsRequested(page, widgetPos);
      } else if (selected == translate) {
        qDebug() << "MyWebEngineView::contextMenuEvent: translate selected (no page)";
        handleTranslateAction();
      }
      menu->deleteLater();
      event->accept();
      return;
    }

    // Map the widget click position to page (viewport) coordinates. Older
    // Qt versions don't provide QWebEnginePage::contextMenuData(), so use
    // the event position adjusted for device pixel ratio to obtain client
    // coordinates suitable for document.caretRangeFromPoint.
    qreal dpr = this->devicePixelRatioF();
    // Use widget (logical) coordinates for caretRangeFromPoint. Multiplying
    // by devicePixelRatio previously produced device-pixel coords which made
    // caretRangeFromPoint miss the point on HiDPI displays. If you need to
    // account for page zoom you can multiply by page->zoomFactor().
    QPointF docPos = QPointF(widgetPos.x(), widgetPos.y());
    qDebug() << "MyWebEngineView::contextMenuEvent: devicePixelRatio=" << dpr << " docPos=" << docPos;

    // JavaScript: find a caret/range at (x,y) and expand to contiguous
    // word characters (letters/digits/underscore). If found, select it.
    // Return the selected text (or empty string).
    QString js = QString::fromUtf8(R"JS(
(function(x,y){
  try{
    // Prefer caretRangeFromPoint; fall back to caretPositionFromPoint for
    // broader engine compatibility.
    var r = null;
    if (document.caretRangeFromPoint) {
      r = document.caretRangeFromPoint(x,y);
    } else if (document.caretPositionFromPoint) {
      var p = document.caretPositionFromPoint(x,y);
      if (p) {
        r = document.createRange();
        r.setStart(p.offsetNode, p.offset);
        r.setEnd(p.offsetNode, p.offset);
      }
    }
    if(!r) {
      // As a last resort, try elementFromPoint and locate a nearby text node
      var el = document.elementFromPoint(x,y);
      if(!el) return '';
      var walker = document.createTreeWalker(el, NodeFilter.SHOW_TEXT, null, false);
      var node = null;
      while(walker.nextNode()){
        node = walker.currentNode;
        var rng = document.createRange();
        rng.selectNodeContents(node);
        var b = rng.getBoundingClientRect();
        if(x >= b.left && x <= b.right && y >= b.top && y <= b.bottom){
          r = document.createRange();
          r.setStart(node, 0);
          r.setEnd(node, 0);
          break;
        }
      }
      if(!r) return '';
    }

    var node = r.startContainer;
    var offset = r.startOffset;
    if(node.nodeType !== Node.TEXT_NODE){
      // walk up to find a text node child
      var found = null;
      var walker2 = document.createTreeWalker(node, NodeFilter.SHOW_TEXT, null, false);
      if(walker2.nextNode()) found = walker2.currentNode;
      if(!found) return '';
      node = found;
      offset = 0;
    }

    var text = node.textContent || '';
    var start = Math.min(Math.max(0, offset), text.length);
    var end = start;
    // Unicode-aware regex: letters and numbers (includes CJK). 'u' flag
    // enables \p{} property escapes supported in modern Chromium.
    var re = /[\p{L}\p{N}_]/u;
    while(start > 0 && re.test(text.charAt(start-1))) start--;
    while(end < text.length && re.test(text.charAt(end))) end++;
    var range2 = document.createRange();
    range2.setStart(node, start);
    range2.setEnd(node, end);
    var sel = window.getSelection();
    sel.removeAllRanges();
    sel.addRange(range2);
    return sel.toString();
  }catch(e){ return ''; }
})(%1, %2);
)JS").arg(QString::number(docPos.x())).arg(QString::number(docPos.y()));

    // Run the JS and then show the menu so the selection is visible when the
    // user sees the context menu.
    qDebug() << "MyWebEngineView::contextMenuEvent: executing JS to expand selection (truncated)";
    page->runJavaScript(js, [this, menu, globalPos, widgetPos, inspect, translate](const QVariant &result){
      qDebug() << "MyWebEngineView::contextMenuEvent: JS result selection='" << result.toString() << "'";
      QAction *selected = menu->exec(globalPos);
      if (selected == inspect) {
        qDebug() << "MyWebEngineView::contextMenuEvent: inspect selected";
        // devToolsRequested expects the QWebEnginePage and local pos
        emit devToolsRequested(this->page(), widgetPos);
      } else if (selected == translate) {
        qDebug() << "MyWebEngineView::contextMenuEvent: translate selected";
        handleTranslateAction();
      } else if (!selected) {
        qDebug() << "MyWebEngineView::contextMenuEvent: menu dismissed (no selection)";
      } else {
        qDebug() << "MyWebEngineView::contextMenuEvent: other action selected";
      }
      menu->deleteLater();
    });

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
   * If text is selected, constructs a Google Translate URL with the selected text.
   * Otherwise, constructs a Google Translate URL with the current page URL for
   * full page translation. Emits translateRequested() signal with the URL so the
   * parent can open it in a new Phraim window.
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

    if (translateUrl.isValid()) {
      emit translateRequested(translateUrl);
    }
  }
};
