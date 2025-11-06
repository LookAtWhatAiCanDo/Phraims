/**
 * Qt6 Widgets Web Brower app that divides the main window into multiple web page frames.
 */
#include <cmath>
#include <vector>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QPalette>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QThread>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>
#include <QWebEngineFullScreenRequest>
#include <QPainter>
#include <QIcon>
#include <QPixmap>
#include <QWebEngineHistory>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineView>
#include <QWidget>


// Debug helper: show per-window id in title for debugging.
static bool DEBUG_SHOW_WINDOW_ID = 0;


/**
 * Event filter that catches Escape key presses on the fullscreen host
 * widget and instructs the page to exit fullscreen. Kept minimal and
 * parented to the fullscreen widget so it is deleted with it.
 */
class EscapeFilter : public QObject {
  Q_OBJECT
public:
  explicit EscapeFilter(QWebEngineView *view, QObject *parent = nullptr) : QObject(parent), view_(view) {}
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

// --- DOM patch persistence helpers --------------------------------------

static QString domPatchesPath() {
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(root);
    const QString path = root + QDir::separator() + QStringLiteral("dom-patches.json");
    return path;
}

/**
 * RAII helper to begin/end a nested QSettings group path like
 * "windows/<id>/splitterSizes". Prefer this over manual begin/end
 * calls to avoid mismatched endGroup() calls on early returns.
 */
struct GroupScope {
  QSettings &s;
  int depth = 0;
  GroupScope(QSettings &settings, const QString &path) : s(settings) {
    const QStringList parts = path.split('/', Qt::SkipEmptyParts);
    for (const QString &p : parts) { s.beginGroup(p); ++depth; }
  }
  ~GroupScope() {
    for (int i = 0; i < depth; ++i) s.endGroup();
  }
};

struct DomPatch {
  QString id;
  QString urlPrefix; // match by startsWith
  QString selector;
  QString css; // css declarations (e.g., "display: none;")
  bool enabled = true;
};

/**
 * Whether to print verbose DOM-patch internals (injected JS payloads).
 * Controlled by the environment variable NVK_DOM_PATCH_VERBOSE (1 to enable).
 */
static bool domPatchesVerbose() {
  static int cached = -1;
  if (cached != -1) return cached;
  const QByteArray e = qgetenv("NVK_DOM_PATCH_VERBOSE");
  if (!e.isEmpty()) {
    bool ok = false;
    const int v = QString::fromUtf8(e).toInt(&ok);
    cached = (ok && v) ? 1 : 0;
  } else {
    cached = 0;
  }
  return cached;
}

static QString escapeForJs(const QString &s) {
  QString out = s;
  out.replace("\\", "\\\\");
  out.replace('\'', "\\'");
  out.replace('"', "\\\"");
  out.replace('\n', ' ');
  out.replace('\r', ' ');
  return out;
}

static QList<DomPatch> loadDomPatches() {
  // Cache patches in-memory to avoid reading the JSON file on every
  // applyDomPatchesToPage call. The file is re-read only when its
  // modification time changes.
  static QList<DomPatch> cache;
  static QDateTime cacheMtime;

  const QString path = domPatchesPath();
  QFileInfo fi(path);
  if (!fi.exists()) {
    if (!cache.isEmpty()) {
      cache.clear();
      cacheMtime = QDateTime();
      if (domPatchesVerbose()) qDebug() << "loadDomPatches: cleared cache (file removed):" << path;
    }
    return cache;
  }

  const QDateTime mtime = fi.lastModified();
  if (!cache.isEmpty() && cacheMtime.isValid() && cacheMtime >= mtime) {
    // cached and up-to-date
    return cache;
  }

  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (domPatchesVerbose()) qDebug() << "loadDomPatches: cannot open" << path;
    cache.clear();
    cacheMtime = QDateTime();
    return cache;
  }
  const QByteArray b = f.readAll();
  f.close();
  const QJsonDocument d = QJsonDocument::fromJson(b);
  if (!d.isArray()) {
    if (domPatchesVerbose()) qDebug() << "loadDomPatches: file exists but JSON is not an array:" << path;
    cache.clear();
    cacheMtime = QDateTime();
    return cache;
  }

  const QJsonArray arr = d.array();
  cache.clear();
  for (const QJsonValue &v : arr) {
    if (!v.isObject()) continue;
    const QJsonObject o = v.toObject();
    DomPatch p;
    p.id = o.value("id").toString(QUuid::createUuid().toString());
    p.urlPrefix = o.value("urlPrefix").toString();
    p.selector = o.value("selector").toString();
    p.css = o.value("css").toString();
    p.enabled = o.value("enabled").toBool(true);
    cache.push_back(p);
  }
  cacheMtime = mtime;
  if (domPatchesVerbose()) qDebug() << "loadDomPatches: loaded" << cache.size() << "entries from" << path;
  return cache;
}

static bool saveDomPatches(const QList<DomPatch> &patches) {
  QJsonArray arr;
  for (const DomPatch &p : patches) {
    QJsonObject o;
    o["id"] = p.id;
    o["urlPrefix"] = p.urlPrefix;
    o["selector"] = p.selector;
    o["css"] = p.css;
    o["enabled"] = p.enabled;
    arr.append(o);
  }
  QJsonDocument d(arr);
  const QString path = domPatchesPath();
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
  f.write(d.toJson(QJsonDocument::Indented));
  f.close();
  if (domPatchesVerbose()) qDebug() << "saveDomPatches: wrote" << arr.size() << "entries to" << path;
  return true;
}

/**
 * Apply patches to the given page immediately (and relies on being called
 * again on subsequent loads). This uses runJavaScript to insert/remove
 * a <style data-dom-patch-id="..."> element scoped to the selector.
 */
void applyDomPatchesToPage(QWebEnginePage *page) {
  if (!page) return;
  const QUrl url = page->url();
  const QString urlStr = url.toString();
  const QList<DomPatch> patches = loadDomPatches();
  // Helper: produce a JSON-quoted JS string for safe embedding
  auto jsonQuoted = [](const QString &s) {
    QJsonArray a;
    a.append(s);
    QString j = QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact));
    // j looks like ["..."] -- strip the surrounding [ and ]
    if (j.size() >= 2 && j.front() == '[' && j.back() == ']') return j.mid(1, j.size() - 2);
    return j; // fallback
  };

  for (const DomPatch &p : patches) {
    if (!p.enabled) continue;
    if (p.urlPrefix.isEmpty() || urlStr.startsWith(p.urlPrefix)) {
      const QString idQ = jsonQuoted(p.id);
      const QString selQ = jsonQuoted(p.selector);
      const QString cssQ = jsonQuoted(p.css);

      const QString js = QString(R"JS(
(function(){
  try {
    var id = %1;
    var sel = %2;
    var css = %3;

    // remove any previous style with the same id
    try {
      var existing = document.querySelector('style[data-dom-patch-id="' + id + '"]');
      if (existing) existing.remove();
    } catch(e) {}

    // insert/update a stylesheet in document head
    try {
      var s = document.createElement('style');
      s.setAttribute('data-dom-patch-id', id);
      s.textContent = sel + '{' + css + '}';
      document.head.appendChild(s);
    } catch(e) {}

    // Try to set an inline style on the first matching element (if present)
    try {
      var el = document.querySelector(sel);
      if (el) {
        try {
          var decl = css.replace(/;\s*$/,'');
          var parts = decl.split(':');
          if (parts.length >= 2) {
            var prop = parts[0].trim();
            var val = parts.slice(1).join(':').trim();
            el.style.setProperty(prop, val, 'important');
          } else {
            el.style.cssText += (' ' + css + ' !important;');
          }
        } catch(e) {}
      }
    } catch(e) {}

  } catch (e) {
    var msg = (e && e.name ? (e.name + ': ') : '') + (e && e.message ? e.message : String(e));
    console.error('dom-patch-inject-error', msg);
  }
})();

)JS").arg(idQ, selQ, cssQ);

      // High-level log for every applied patch (always enabled).
      qDebug() << "applyDomPatchesToPage: applying patch id=" << p.id << " url=" << urlStr << " selector=" << p.selector << " css=" << p.css;
      // Detailed injected-JS payload logging gated behind NVK_DOM_PATCH_VERBOSE.
      if (domPatchesVerbose()) {
        qDebug() << "applyDomPatchesToPage: js=" << js;
      }
      page->runJavaScript(js);
    }
  }
}

/**
 * Modeless dialog to list/add/edit/delete DOM patches.
 * Shown modelessly via show() in SplitWindow::showDomPatchesManager.
 */
class DomPatchesDialog : public QDialog {
  Q_OBJECT
public:
  DomPatchesDialog(QWidget *parent = nullptr) : QDialog(parent) {
    setWindowTitle(tr("DOM Patches"));
    resize(640, 360);
    auto *lay = new QVBoxLayout(this);
    list_ = new QListWidget(this);
    lay->addWidget(list_, 1);
    auto *btnRow = new QHBoxLayout();
    auto *add = new QPushButton(tr("Add"), this);
    auto *edit = new QPushButton(tr("Edit"), this);
    auto *del = new QPushButton(tr("Delete"), this);
    auto *close = new QPushButton(tr("Close"), this);
    btnRow->addWidget(add);
    btnRow->addWidget(edit);
    btnRow->addWidget(del);
    btnRow->addStretch(1);
    btnRow->addWidget(close);
    lay->addLayout(btnRow);

    connect(add, &QPushButton::clicked, this, &DomPatchesDialog::onAdd);
    connect(edit, &QPushButton::clicked, this, &DomPatchesDialog::onEdit);
    connect(del, &QPushButton::clicked, this, &DomPatchesDialog::onDelete);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);

    loadList();
  }

  QList<DomPatch> patches() const { return patches_; }

private slots:
  void loadList() {
    patches_ = loadDomPatches();
    list_->clear();
    for (const DomPatch &p : std::as_const(patches_)) {
      // show URL prefix, selector and the CSS declarations in the list
      const QString cssPreview = p.css.isEmpty() ? QStringLiteral("(no style)") : p.css;
      const QString enabledSuffix = p.enabled ? QString() : QStringLiteral(" (disabled)");
      QListWidgetItem *it = new QListWidgetItem(
        QStringLiteral("%1 | %2 | %3%4")
          .arg(p.urlPrefix, p.selector, cssPreview, enabledSuffix),
        list_);
      it->setData(Qt::UserRole, p.id);
      it->setToolTip(
        QStringLiteral("Selector: %1\nStyle: %2\nURL prefix: %3")
          .arg(p.selector, p.css, p.urlPrefix)
        );
    }
  }

  void onAdd() {
    DomPatch p;
    p.id = QUuid::createUuid().toString();
    // Show non-modal editor that will append the patch when the user
    // accepts. The editor works asynchronously so we don't block DevTools.
    editPatchDialog(p, true);
  }

  void onEdit() {
    auto *it = list_->currentItem();
    if (!it) return;
    const QString id = it->data(Qt::UserRole).toString();
    for (DomPatch &p : patches_) {
      if (p.id == id) {
        // Show non-modal editor that will update the existing patch
        // when the user accepts the dialog.
        editPatchDialog(p, false);
        break;
      }
    }
  }

  void onDelete() {
    auto *it = list_->currentItem();
    if (!it) return;
    const QString id = it->data(Qt::UserRole).toString();
    for (int i = 0; i < patches_.size(); ++i) {
      if (patches_[i].id == id) {
        patches_.removeAt(i);
        saveDomPatches(patches_);
        loadList();
        return;
      }
    }
  }

  /**
   * Non-modal editor for a single patch. When the user accepts the
   * dialog the patch is either added (isNew==true) or the existing
   * patch is updated. The dialog is heap-allocated and deleted on close.
   */
  void editPatchDialog(const DomPatch &p_in, bool isNew) {
    DomPatch p = p_in; // copy so we don't mutate until user accepts
    QDialog *d = new QDialog(this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->setWindowTitle(tr("Edit DOM Patch"));
    auto *lay = new QVBoxLayout(d);
    auto *urlLabel = new QLabel(tr("URL prefix (startsWith):"), d);
    auto *urlEdit = new QLineEdit(p.urlPrefix, d);
    auto *selLabel = new QLabel(tr("CSS selector:"), d);
    auto *selEdit = new QLineEdit(p.selector, d);
    auto *cssLabel = new QLabel(tr("CSS declarations (e.g. display: none;):"), d);
    auto *cssEdit = new QLineEdit(p.css, d);
    auto *enabledChk = new QCheckBox(tr("Enabled"), d);
    enabledChk->setChecked(p.enabled);
    lay->addWidget(urlLabel);
    lay->addWidget(urlEdit);
    lay->addWidget(selLabel);
    lay->addWidget(selEdit);
    lay->addWidget(cssLabel);
    lay->addWidget(cssEdit);
    lay->addWidget(enabledChk);
    auto *btnRow = new QHBoxLayout();
    auto *ok = new QPushButton(tr("OK"), d);
    auto *cancel = new QPushButton(tr("Cancel"), d);
    btnRow->addStretch(1);
    btnRow->addWidget(ok);
    btnRow->addWidget(cancel);
    lay->addLayout(btnRow);

    // OK: capture current widget values, persist, refresh list, then close
    connect(ok, &QPushButton::clicked, this, [this, d, urlEdit, selEdit, cssEdit, enabledChk, p]() mutable {
      DomPatch newP = p; // start from original id
      newP.urlPrefix = urlEdit->text();
      newP.selector = selEdit->text();
      newP.css = cssEdit->text();
      newP.enabled = enabledChk->isChecked();
      if (/*isNew*/ d->property("isNew").toBool()) {
        // append new patch
        patches_.push_back(newP);
      } else {
        // find and update existing
        for (DomPatch &q : patches_) {
          if (q.id == newP.id) {
            q = newP;
            break;
          }
        }
      }
      saveDomPatches(patches_);
      loadList();
      d->close();
    });

    // Cancel just closes the dialog
    connect(cancel, &QPushButton::clicked, d, &QDialog::close);

    // Mark whether this dialog is creating a new patch so the OK handler
    // knows whether to append or update. We store it as a dynamic property
    // on the dialog to keep the OK lambda simple.
    d->setProperty("isNew", isNew);

    d->show();
  }

private:
  QListWidget *list_ = nullptr;
  QList<DomPatch> patches_;
};

/**
 * A self-contained frame used for each split section.
 * Contains controls at its top (ex: back, forward, address, ...) and a simple content area below.
 */
class SplitFrameWidget : public QFrame {
  Q_OBJECT

public:
  SplitFrameWidget(int index, QWidget *parent = nullptr) : QFrame(parent) {
    setFrameShape(QFrame::StyledPanel);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // subtle alternating background color based on index
    QPalette pal = palette();
    QColor base = palette().color(QPalette::Window);
    int shift = (index % 2 == 0) ? 6 : -6;
    QColor bg = base.lighter(100 + shift);
    pal.setColor(QPalette::Window, bg);
    setAutoFillBackground(true);
    setPalette(pal);

    innerLayout_ = new QVBoxLayout(this);
    innerLayout_->setContentsMargins(6, 6, 6, 6);
    innerLayout_->setSpacing(6);

    // left: navigation buttons, center: address bar, right: +/- buttons
    auto *topRow = new QHBoxLayout();
    topRow->setSpacing(6);

    backBtn_ = new QToolButton(this);
    backBtn_->setText("<");
    backBtn_->setToolTip("Back");
    backBtn_->setEnabled(false);
    topRow->addWidget(backBtn_);

    forwardBtn_ = new QToolButton(this);
    forwardBtn_->setText(">");
    forwardBtn_->setToolTip("Forward");
    forwardBtn_->setEnabled(false);
    topRow->addWidget(forwardBtn_);

    refreshBtn_ = new QToolButton(this);
    refreshBtn_->setText("\u21BB"); // clockwise open circle arrow
    refreshBtn_->setToolTip("Refresh");
    refreshBtn_->setEnabled(false);
    topRow->addWidget(refreshBtn_);

    address_ = new QLineEdit(this);
    address_->setPlaceholderText("Address or URL");
    address_->setClearButtonEnabled(true);
    // show the left-most characters when not being edited and provide
    // a hover tooltip containing the full URL
    address_->setToolTip(address_->text());
    address_->installEventFilter(this);
    address_->setCursorPosition(0);
    connect(address_, &QLineEdit::textChanged, this, [this](const QString &t){
      address_->setToolTip(t);
    });
    topRow->addWidget(address_, 1);

    // up/down move this frame within the list
    upBtn_ = new QToolButton(this);
    upBtn_->setText("\u25B2"); // up triangle
    upBtn_->setToolTip("Move this section up");
    topRow->addWidget(upBtn_);

    downBtn_ = new QToolButton(this);
    downBtn_->setText("\u25BC"); // down triangle
    downBtn_->setToolTip("Move this section down");
    topRow->addWidget(downBtn_);

    plusBtn_ = new QToolButton(this);
    plusBtn_->setText("+");
    plusBtn_->setToolTip("Insert a new section after this one");
    topRow->addWidget(plusBtn_);

    minusBtn_ = new QToolButton(this);
    minusBtn_->setText("-");
    minusBtn_->setToolTip("Remove this section");
    topRow->addWidget(minusBtn_);

    innerLayout_->addLayout(topRow);

    // web view content area
    webview_ = new MyWebEngineView(this);
    webview_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    innerLayout_->addWidget(webview_, 1);

    // wire internal UI to emit signals and control webview
    connect(plusBtn_, &QToolButton::clicked, this, [this]() { emit plusClicked(this); });
    connect(minusBtn_, &QToolButton::clicked, this, [this]() { emit minusClicked(this); });
    connect(upBtn_, &QToolButton::clicked, this, [this]() { emit upClicked(this); });
    connect(downBtn_, &QToolButton::clicked, this, [this]() { emit downClicked(this); });
    connect(address_, &QLineEdit::editingFinished, this, [this]() {
      emit addressEdited(this, address_->text());
      applyAddress(address_->text());
    });

    connect(backBtn_, &QToolButton::clicked, this, [this]() { if (webview_) webview_->back(); });
    connect(forwardBtn_, &QToolButton::clicked, this, [this]() { if (webview_) webview_->forward(); });
    connect(refreshBtn_, &QToolButton::clicked, this, [this]() { if (webview_) webview_->reload(); });

    connect(webview_, &MyWebEngineView::urlChanged, this, [this](const QUrl &url) {
      // ignore internal data URLs (used for instruction/error HTML) so the
      // address bar doesn't show the data: URL. Only update the address when
      // a real navigable URL is loaded.
      if (url.scheme() == QStringLiteral("data") || url.isEmpty()) {
        updateNavButtons();
        return;
      }
      const QString s = url.toString();
      address_->setText(s);
      // When updating programmatically, ensure unfocused fields show the
      // left-most characters rather than scrolled to the end.
      if (!address_->hasFocus()) address_->setCursorPosition(0);
      // update nav button states
      updateNavButtons();
      // re-apply any DOM patches when the URL changes (helps single-page apps)
      extern void applyDomPatchesToPage(QWebEnginePage *page);
      if (webview_ && webview_->page()) applyDomPatchesToPage(webview_->page());
      emit addressEdited(this, s);
    });
    connect(webview_, &MyWebEngineView::loadStarted, this, [this]() { refreshBtn_->setEnabled(true); });
    connect(webview_, &MyWebEngineView::loadFinished, this, [this](bool ok) {
      Q_UNUSED(ok);
      updateNavButtons();
    });
    connect(webview_, &MyWebEngineView::devToolsRequested, this, [this](QWebEnginePage *page, const QPoint &pos) {
      emit devToolsRequested(this, page, pos);
    });
  }

  QWebEnginePage *page() const { return webview_ ? webview_->page() : nullptr; }

  QString address() const { return address_->text(); }
  void setAddress(const QString &s) {
      address_->setText(s);
      // if the user is not actively editing, ensure the left-most
      // characters are visible by resetting the cursor position
      if (!address_->hasFocus()) address_->setCursorPosition(0);
      applyAddress(s);
  }

  void applyAddress(const QString &s) {
    const QString trimmed = s.trimmed();
    if (trimmed.isEmpty()) {
      // show instruction HTML instead of loading
      const QString html = QStringLiteral("<html><body><div style=\"font-family: sans-serif; color: #666; padding: 20px;\">Enter an address above and press Enter to load a page.</div></body></html>");
      webview_->setHtml(html);
      refreshBtn_->setEnabled(false);
      backBtn_->setEnabled(false);
      forwardBtn_->setEnabled(false);
      return;
    }

    QUrl url = QUrl::fromUserInput(trimmed);
    // If user typed a bare host without scheme, prefer https
    if (url.isValid() && url.scheme().isEmpty()) {
      url.setScheme(QStringLiteral("https"));
    }
    if (!url.isValid()) {
      // show error-instruction
      const QString html = QStringLiteral("<html><body><div style=\"font-family: sans-serif; color: #900; padding: 20px;\">Invalid address.</div></body></html>");
      webview_->setHtml(html);
      refreshBtn_->setEnabled(false);
      backBtn_->setEnabled(false);
      forwardBtn_->setEnabled(false);
      return;
    }

    webview_->load(url);
    // nav buttons will be updated on urlChanged / loadFinished
  }

  void updateNavButtons() {
    if (!webview_) return;
    auto hist = webview_->history();
    backBtn_->setEnabled(hist->canGoBack());
    forwardBtn_->setEnabled(hist->canGoForward());
    refreshBtn_->setEnabled(!webview_->url().isEmpty());
  }

  bool eventFilter(QObject *watched, QEvent *event) override {
    if (watched == address_) {
      if (event->type() == QEvent::FocusOut) {
        // When the user finishes editing (or focus leaves), ensure the
        // displayed portion starts at the left so the left-most characters
        // are visible.
        address_->setCursorPosition(0);
      }
      // Let the line edit handle the event as well
      return false;
    }
    return QFrame::eventFilter(watched, event);
  }

  void setMinusEnabled(bool en) { if (minusBtn_) minusBtn_->setEnabled(en); }
  void setUpEnabled(bool en) { if (upBtn_) upBtn_->setEnabled(en); }
  void setDownEnabled(bool en) { if (downBtn_) downBtn_->setEnabled(en); }

  // assign a QWebEngineProfile by setting a new page for the internal view
  void setProfile(QWebEngineProfile *profile) {
    if (!webview_ || !profile) return;
    // assign a fresh page associated with the shared profile
    auto *page = new QWebEnginePage(profile, webview_);
    webview_->setPage(page);
    // Ensure DOM patches are applied on every load for this page.
    QObject::connect(page, &QWebEnginePage::loadFinished, page, [page](bool) {
      // apply patches after each load
      // forward to helper defined below
      extern void applyDomPatchesToPage(QWebEnginePage *page);
      applyDomPatchesToPage(page);
    });
    // Ensure the page has fullscreen support enabled (should be true by default
    // but being explicit helps diagnose platform differences).
    page->settings()->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
    qDebug() << "SplitFrameWidget::setProfile: FullScreenSupportEnabled=" << page->settings()->testAttribute(QWebEngineSettings::FullScreenSupportEnabled);

    // Log and (optionally) auto-grant feature permissions that some players
    // request when entering fullscreen, like mouse lock. This will help
    // diagnose permission-denied problems.
    QObject::connect(page, &QWebEnginePage::permissionRequested, this, [page](QWebEnginePermission permissionRequest){
      auto origin = permissionRequest.origin();
      auto permissionType = permissionRequest.permissionType();
      qDebug() << "SplitFrameWidget::featurePermissionRequested: origin=" << origin << " permissionType=" << permissionType;
      // Auto-grant mouse lock which some fullscreen players use.
      if (permissionType == QWebEnginePermission::PermissionType::MouseLock) {
        qDebug() << "SplitFrameWidget: granting MouseLock for" << origin;
        permissionRequest.grant();
        qDebug() << "SplitFrameWidget: granted MouseLock for" << origin;
        return;
      }
      // For other features, reject the request but log for diagnostics.
      qDebug() << "SplitFrameWidget: denying" << permissionType << "for" << origin;
      permissionRequest.deny();
      qDebug() << "SplitFrameWidget: denied" << permissionType << "for" << origin;
    });
    // Honor HTML5 fullscreen requests (e.g., YouTube fullscreen button).
    qDebug() << "SplitFrameWidget::setProfile: connecting fullScreenRequested for page" << page << "parent webview=" << webview_;
    QObject::connect(page, &QWebEnginePage::fullScreenRequested, this, &SplitFrameWidget::handleFullScreenRequested);
    qDebug() << "SplitFrameWidget::setProfile: connected fullScreenRequested";
  }

private slots:
  /**
   * Handler for HTML5 fullscreen requests from the page (e.g., YouTube
   * fullscreen button). The QWebEngineFullScreenRequest is accepted and
   * the internal webview is reparented into a top-level full-screen
   * window while the request is active.
   */
  void handleFullScreenRequested(QWebEngineFullScreenRequest request);

private:
  signals:
  void plusClicked(SplitFrameWidget *who);
  void minusClicked(SplitFrameWidget *who);
  void upClicked(SplitFrameWidget *who);
  void downClicked(SplitFrameWidget *who);
  void addressEdited(SplitFrameWidget *who, const QString &text);
  /**
   * Request that the window show/attach a shared DevTools view for this frame
   */
  void devToolsRequested(SplitFrameWidget *who, QWebEnginePage *page, const QPoint &pos);

private:
  QVBoxLayout *innerLayout_ = nullptr;
  QLineEdit *address_ = nullptr;
  MyWebEngineView *webview_ = nullptr;
  QToolButton *upBtn_ = nullptr;
  QToolButton *downBtn_ = nullptr;
  QToolButton *plusBtn_ = nullptr;
  QToolButton *minusBtn_ = nullptr;
  QToolButton *backBtn_ = nullptr;
  QToolButton *forwardBtn_ = nullptr;
  QToolButton *refreshBtn_ = nullptr;

  // When a page requests fullscreen we create a top-level window and
  // reparent the webview into it. Use QPointer guards to avoid dangling
  // pointers during teardown.
  QPointer<QWidget> fullScreenWindow_;
  QPointer<QWidget> previousParent_;
  // Event filter used while a frame is fullscreen so we can catch Escape
  // key presses regardless of which child widget currently has focus.
  QPointer<EscapeFilter> escapeFilter_;
  // If true we hid the top-level window when entering fullscreen and
  // should restore it on exit.
  bool hidWindowForFullscreen_ = false;
  // Remember the previous window state of the top-level window so we
  // can restore it (e.g., exit fullscreen) when leaving page fullscreen.
  Qt::WindowStates previousTopWindowState_ = Qt::WindowNoState;
};

/**
 * Handle QWebEngineFullScreenRequest for SplitFrameWidget.
 * Reparents the internal webview into a top-level full-screen window
 * while the request is active, and restores it when fullscreen exits.
 */
void SplitFrameWidget::handleFullScreenRequested(QWebEngineFullScreenRequest request) {
  qDebug() << "SplitFrameWidget::handleFullScreenRequested: received request toggleOn=" << request.toggleOn() << " origin=" << request.origin().toString();
  if (request.toggleOn()) {
    qDebug() << "SplitFrameWidget: entering fullscreen";
    // Enter fullscreen
    if (fullScreenWindow_) {
      qDebug() << "SplitFrameWidget: already in fullscreen, accepting request";
      request.accept();
      return;
    }
    // Create a dedicated top-level QMainWindow for fullscreen. Using a
    // full QMainWindow (rather than a child widget) ensures the OS treats
    // this window as a full application-space fullscreen window so the
    // whole window/space is switched on macOS when shown fullscreen.
    QMainWindow *fsw = new QMainWindow(nullptr);
    fsw->setAttribute(Qt::WA_DeleteOnClose);
    fsw->setWindowTitle(tr("Fullscreen"));
    fsw->setWindowState(Qt::WindowFullScreen);
    fsw->setWindowFlag(Qt::Window, true);

    // reparent the webview into the fullscreen main window
    previousParent_ = webview_->parentWidget();
    qDebug() << "SplitFrameWidget: previousParent=" << previousParent_ << " webview=" << webview_;
    webview_->setParent(fsw);
    // place the webview as the central widget so it fills the fullscreen window
    fsw->setCentralWidget(webview_);
    // Ensure keyboard focus goes to the webview so it (and page JS) can
    // receive key events, and install an EscapeFilter on both the
    // fullscreen window and the webview so ESC is caught regardless of
    // which widget has focus.
    webview_->setFocus(Qt::OtherFocusReason);
    auto *ef = new EscapeFilter(webview_, fsw);
    // keep a guarded pointer so we can remove the filter when exiting
    // fullscreen to avoid leaving stale filters on the webview.
    escapeFilter_ = ef;
    fsw->installEventFilter(ef);
    if (webview_) webview_->installEventFilter(ef);
    // Also install as a global application filter so we catch Escape even
    // if the key event is dispatched at a level the view/window doesn't
    // receive (some WebEngine content consumes events in the web process).
    if (qApp) qApp->installEventFilter(ef);

    fullScreenWindow_ = fsw;
    request.accept();
    fsw->showFullScreen();

    // Hide the original top-level window so the app chrome doesn't remain
    // visible on the original desktop/space while the page is fullscreen.
    hidWindowForFullscreen_ = false;
    if (QWidget *top = this->window()) {
      qDebug() << "SplitFrameWidget: hiding top-level window while page is fullscreen";
      // remember previous window state so we can restore it
      previousTopWindowState_ = top->windowState();
      top->hide();
      hidWindowForFullscreen_ = true;
    }

    // Ensure that if the fullscreen window is closed externally we
    // restore the webview back into this frame.
    QPointer<QWidget> fswGuard(fsw);
    connect(fsw, &QObject::destroyed, this, [this, fswGuard](QObject *) {
      qDebug() << "SplitFrameWidget: fullscreen window destroyed, restoring webview";
      // Remove event filter from the webview if present so we don't leave
      // a dangling filter installed when the fullscreen helper is gone.
      if (webview_ && escapeFilter_) {
        webview_->removeEventFilter(escapeFilter_);
      }
      if (qApp && escapeFilter_) {
        qApp->removeEventFilter(escapeFilter_);
      }
      escapeFilter_ = nullptr;

      // Restore visibility of this frame and possibly the top-level
      // window if we hid it when entering fullscreen.
      if (hidWindowForFullscreen_) {
        if (QWidget *top = this->window()) {
          qDebug() << "SplitFrameWidget: restoring top-level window after fullscreen";
          top->show();
          // restore previous window state (ensure we leave fullscreen if it
          // wasn't previously full-screen).
          if (previousTopWindowState_ & Qt::WindowFullScreen) {
            top->setWindowState(previousTopWindowState_);
          } else {
            top->showNormal();
          }
          top->raise();
          top->activateWindow();
        }
        hidWindowForFullscreen_ = false;
        previousTopWindowState_ = Qt::WindowNoState;
      }
      this->setVisible(true);

      // If the webview is not already parented to this frame, move it back.
      if (webview_ && webview_->parentWidget() != this) {
        webview_->setParent(this);
        if (innerLayout_) innerLayout_->addWidget(webview_, 1);
      }
      fullScreenWindow_ = nullptr;
    });
    return;
  }

  qDebug() << "SplitFrameWidget: exiting fullscreen";
  // Exit fullscreen
  if (!fullScreenWindow_) {
    qDebug() << "SplitFrameWidget: no fullscreen window present, accepting request and returning";
    request.accept();
    return;
  }

  // Remove any event filter installed on the webview by the fullscreen
  // helper before restoring parentage so we don't leave stale filters.
  if (webview_ && escapeFilter_) {
    webview_->removeEventFilter(escapeFilter_);
  }
  if (qApp && escapeFilter_) {
    qApp->removeEventFilter(escapeFilter_);
  }
  escapeFilter_ = nullptr;

  // Restore visibility of this frame and possibly the top-level window
  // if we hid it when entering fullscreen.
  if (hidWindowForFullscreen_) {
    if (QWidget *top = this->window()) {
      qDebug() << "SplitFrameWidget: restoring top-level window after fullscreen";
      top->show();
      if (previousTopWindowState_ & Qt::WindowFullScreen) {
        top->setWindowState(previousTopWindowState_);
      } else {
        top->showNormal();
      }
      top->raise();
      top->activateWindow();
    }
    hidWindowForFullscreen_ = false;
    previousTopWindowState_ = Qt::WindowNoState;
  }
  this->setVisible(true);

  // Reparent the webview back into this frame's layout
  webview_->setParent(this);
  if (innerLayout_) innerLayout_->addWidget(webview_, 1);

  // Close the fullscreen window (it will be deleted due to WA_DeleteOnClose)
  QWidget *w = fullScreenWindow_;
  fullScreenWindow_ = nullptr;
  if (w) {
    qDebug() << "SplitFrameWidget: closing fullscreen window" << w;
    w->close();
  }
  request.accept();
}

// Forward declare SplitWindow so helper prototype can appear before its use
class SplitWindow;
// Declare global windows vector (defined further below)
extern std::vector<SplitWindow*> g_windows;
// Prototype for helper used by menu/shortcuts inside SplitWindow
static void createAndShowWindow(const QString &initialAddress = QString(), const QString &windowId = QString());

// Forward declare helper so member methods can call it before its definition.
static void rebuildAllWindowMenus();

// Forward declarations for icons created later in this file. These are
// defined after the SplitWindow class so provide extern declarations
// here so member functions can refer to them.
extern QIcon g_windowDiamondIcon;
extern QIcon g_windowEmptyIcon;
extern QIcon g_windowCheckIcon;
extern QIcon g_windowCheckDiamondIcon;

class SplitWindow : public QMainWindow {
  Q_OBJECT

public:
  enum LayoutMode { Vertical = 0, Horizontal = 1, Grid = 2 };

  // Accept an optional windowId (UUID). If provided, the window will
  // load/save its state under QSettings group "windows/<windowId>".
  SplitWindow(const QString &windowId = QString(), QWidget *parent = nullptr) : QMainWindow(parent), windowId_(windowId) {
    setWindowTitle(QCoreApplication::applicationName());
    resize(800, 600);

    QSettings settings;

    // File menu: New Window (Cmd/Ctrl+N)
    auto *fileMenu = menuBar()->addMenu(tr("File"));
    QAction *newWindowAction = fileMenu->addAction(tr("New Window"));
    newWindowAction->setShortcut(QKeySequence::New);
    connect(newWindowAction, &QAction::triggered, this, [](bool){ createAndShowWindow(); });

    // No global toolbar; per-frame + / - buttons control sections.

    // create a shared persistent QWebEngineProfile for all frames so
    // cookies/localStorage/session state are persisted across frames and runs
    const QString dataRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString profilePath = dataRoot;
    qDebug() << "  WebEngine profilePath:" << profilePath;
    const QString profileCache = profilePath + "/cache";
    qDebug() << "  WebEngine profileCache:" << profileCache;
    QDir().mkpath(profilePath);
    QDir().mkpath(profileCache);
    const QString profileName = QCoreApplication::organizationName();
    qDebug() << "  WebEngine profileName:" << profileName;
    profile_ = new QWebEngineProfile(profileName, this);
    profile_->setPersistentStoragePath(profilePath);
    profile_->setCachePath(profileCache);
    profile_->setHttpCacheType(QWebEngineProfile::DiskHttpCache);
    profile_->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);

    // (window geometry/state restored later after UI is built)

    // add a simple View menu with a helper to set the window height to the
    // screen available height (preserves width and x position)
    auto *viewMenu = menuBar()->addMenu(tr("View"));
    QAction *setHeightAction = viewMenu->addAction(tr("Set height to screen"));
    connect(setHeightAction, &QAction::triggered, this, &SplitWindow::setHeightToScreen);
    QAction *toggleDevToolsAction = viewMenu->addAction(tr("Toggle DevTools"));
    toggleDevToolsAction->setShortcut(QKeySequence(Qt::Key_F12));
    connect(toggleDevToolsAction, &QAction::triggered, this, &SplitWindow::toggleDevToolsForFocusedFrame);

    // Always-on-top toggle
    QAction *alwaysOnTopAction = viewMenu->addAction(tr("Always on Top"));
    alwaysOnTopAction->setCheckable(true);
    // read persisted value (default: false)
    {
      const bool on = settings.value("alwaysOnTop", false).toBool();
      alwaysOnTopAction->setChecked(on);
      // apply the window flag; setWindowFlag requires a show() to take effect on some platforms
      setWindowFlag(Qt::WindowStaysOnTopHint, on);
      if (on) show();
    }
    connect(alwaysOnTopAction, &QAction::toggled, this, [this](bool checked){
      setWindowFlag(Qt::WindowStaysOnTopHint, checked);
      if (checked) show();
      QSettings settings;
      settings.setValue("alwaysOnTop", checked);
    });

    // Layout menu: Grid, Stack Vertically, Stack Horizontally
    auto *layoutMenu = menuBar()->addMenu(tr("Layout"));
    QActionGroup *layoutGroup = new QActionGroup(this);
    layoutGroup->setExclusive(true);
    QAction *gridAction = layoutMenu->addAction(tr("Grid"));
    gridAction->setCheckable(true);
    layoutGroup->addAction(gridAction);
    QAction *verticalAction = layoutMenu->addAction(tr("Stack Vertically"));
    verticalAction->setCheckable(true);
    layoutGroup->addAction(verticalAction);
    QAction *horizontalAction = layoutMenu->addAction(tr("Stack Horizontally"));
    horizontalAction->setCheckable(true);
    layoutGroup->addAction(horizontalAction);

    // restore persisted layout choice
    int storedMode = settings.value("layoutMode", (int)Vertical).toInt();
    layoutMode_ = (LayoutMode)storedMode;
    switch (layoutMode_) {
      case Grid: gridAction->setChecked(true); break;
      case Horizontal: horizontalAction->setChecked(true); break;
      case Vertical: default: verticalAction->setChecked(true); break;
    }

    connect(gridAction, &QAction::triggered, this, [this]() { setLayoutMode(Grid); });
    connect(verticalAction, &QAction::triggered, this, [this]() { setLayoutMode(Vertical); });
    connect(horizontalAction, &QAction::triggered, this, [this]() { setLayoutMode(Horizontal); });

    // Tools menu: DOM patches manager
    auto *toolsMenu = menuBar()->addMenu(tr("Tools"));
    QAction *domPatchesAction = toolsMenu->addAction(tr("DOM Patches"));
    connect(domPatchesAction, &QAction::triggered, this, &SplitWindow::showDomPatchesManager);

    // Window menu: per-macOS convention
    windowMenu_ = menuBar()->addMenu(tr("Window"));
    // Add standard close/minimize actions
    QAction *minimizeAct = windowMenu_->addAction(tr("Minimize"));
    minimizeAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));
    connect(minimizeAct, &QAction::triggered, this, &QWidget::showMinimized);
    QAction *closeAct = windowMenu_->addAction(tr("Close Window"));
    closeAct->setShortcut(QKeySequence::Close);
    connect(closeAct, &QAction::triggered, this, &QWidget::close);
    windowMenu_->addSeparator();

    // central scroll area to allow many sections
    auto *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    central_ = new QWidget();
    scroll->setWidget(central_);
    setCentralWidget(scroll);

    layout_ = new QVBoxLayout(central_);
    layout_->setContentsMargins(4, 4, 4, 4);
    layout_->setSpacing(6);

    // load persisted addresses (per-window if windowId_ present, otherwise global)
    if (!windowId_.isEmpty()) {
      QSettings s;
      {
        GroupScope _gs(s, QStringLiteral("windows/%1").arg(windowId_));
        const QStringList saved = s.value("addresses").toStringList();
        if (saved.isEmpty()) {
          addresses_.push_back(QString());
        } else {
          for (const QString &s2 : saved) addresses_.push_back(s2);
        }
        layoutMode_ = (LayoutMode)s.value("layoutMode", (int)layoutMode_).toInt();
      }
    } else {
      const QStringList saved = settings.value("addresses").toStringList();
      if (saved.isEmpty()) {
        addresses_.push_back(QString());
      } else {
        for (const QString &s : saved) addresses_.push_back(s);
      }
    }
    // build initial UI
    rebuildSections((int)addresses_.size());
    // restore splitter sizes only once at startup (subsequent layout
    // selections/rebuilds should reset splitters to defaults)
    if (!windowId_.isEmpty()) {
      restoreSplitterSizes(QStringLiteral("windows/%1/splitterSizes").arg(windowId_));
    } else {
      restoreSplitterSizes();
    }
    restoredOnStartup_ = true;

    // restore saved window geometry and window state (position/size/state)
    if (!windowId_.isEmpty()) {
      QSettings s;
      {
        GroupScope _gs(s, QStringLiteral("windows/%1").arg(windowId_));
        const QByteArray savedGeom = s.value("windowGeometry").toByteArray();
        if (!savedGeom.isEmpty()) restoreGeometry(savedGeom);
        const QByteArray savedState = s.value("windowState").toByteArray();
        if (!savedState.isEmpty()) restoreState(savedState);
      }
    } else {
      const QByteArray savedGeom = settings.value("windowGeometry").toByteArray();
      if (!savedGeom.isEmpty()) restoreGeometry(savedGeom);
      const QByteArray savedState = settings.value("windowState").toByteArray();
      if (!savedState.isEmpty()) restoreState(savedState);
    }
  }

public:
  /**
   * Persist this window's addresses, layout, geometry, state and splitter sizes
   * into QSettings under group "windows/<id>". If this window did not have
   * an id, a new one will be generated and used so the window will be
   * restorable on next launch.
   */
  void savePersistentStateToSettings() {
    QSettings s;
    QString id = windowId_;
    if (id.isEmpty()) id = QUuid::createUuid().toString();
    qDebug() << "savePersistentStateToSettings: saving window id=" << id << " addresses.count=" << addresses_.size() << " layoutMode=" << (int)layoutMode_;
    {
      GroupScope _gs(s, QStringLiteral("windows/%1").arg(id));
      QStringList list;
      for (const auto &a : addresses_) list << a;
      s.setValue("addresses", list);
      s.setValue("layoutMode", (int)layoutMode_);
      s.setValue("windowGeometry", saveGeometry());
      s.setValue("windowState", saveState());
    }
    s.sync();
    // persist splitter sizes under windows/<id>/splitterSizes/<index>
    saveCurrentSplitterSizes(QStringLiteral("windows/%1/splitterSizes").arg(id));
  }

public slots:
  // Reset this window to a single empty section (used for New Window behavior)
  void resetToSingleEmptySection() {
    addresses_.clear();
    addresses_.push_back(QString());
    rebuildSections(1);
    // Do not persist immediately; keep in-memory until user changes or window closes.
    // After rebuilding, focus the address field so the user can start typing
    // immediately. Use a queued invoke so focus is set after layout/stacking
    // completes.
    QMetaObject::invokeMethod(this, "focusFirstAddress", Qt::QueuedConnection);
  }

  /**
   * Public wrapper to refresh the Window menu. Keeps updateWindowMenu() private.
   */
  void refreshWindowMenu() { updateWindowMenu(); }

  /**
   * Focus the first frame's address QLineEdit so the user can start typing.
   */
  void focusFirstAddress();

public:
  /**
   * Update this window's title to the form "Group X (N)" where X is the
   * 1-based index of this window in the global windows list and N is the
   * number of frames (sections) currently in the window.
   */
  void updateWindowTitle();

private slots:
  void rebuildSections(int n) {

    // Ensure addresses_ vector matches requested size, preserving existing values.
    if ((int)addresses_.size() < n) {
      addresses_.resize(n);
    } else if ((int)addresses_.size() > n) {
      addresses_.resize(n);
    }
    // clamp n
    if (n < 1) n = 1;

    // clear existing items
    QLayoutItem *child;
    while ((child = layout_->takeAt(0)) != nullptr) {
      if (auto *w = child->widget()) {
        w->hide();
        w->deleteLater();
      }
      delete child;
    }

    // old splitters are going away; clear tracking vector so we start fresh
    currentSplitters_.clear();

    // create n sections according to the selected layout mode.
    QWidget *container = nullptr;
    if (layoutMode_ == Vertical || layoutMode_ == Horizontal) {
      QSplitter *split = new QSplitter(layoutMode_ == Vertical ? Qt::Vertical : Qt::Horizontal);
      // track this splitter for state persistence
      currentSplitters_.push_back(split);
      for (int i = 0; i < n; ++i) {
        auto *frame = new SplitFrameWidget(i);
        // logicalIndex property used for mapping frame -> addresses_ index
        frame->setProperty("logicalIndex", i);
        frame->setProfile(profile_);
        frame->setAddress(addresses_[i]);
        connect(frame, &SplitFrameWidget::plusClicked, this, &SplitWindow::onPlusFromFrame);
        connect(frame, &SplitFrameWidget::minusClicked, this, &SplitWindow::onMinusFromFrame);
        connect(frame, &SplitFrameWidget::addressEdited, this, &SplitWindow::onAddressEdited);
        connect(frame, &SplitFrameWidget::upClicked, this, &SplitWindow::onUpFromFrame);
        connect(frame, &SplitFrameWidget::downClicked, this, &SplitWindow::onDownFromFrame);
        connect(frame, &SplitFrameWidget::devToolsRequested, this, &SplitWindow::onFrameDevToolsRequested);
        frame->setMinusEnabled(n > 1);
        frame->setUpEnabled(i > 0);
        frame->setDownEnabled(i < n - 1);
        split->addWidget(frame);
      }
      // distribute sizes evenly across the children so switching layouts
      // starts with a balanced view
      if (n > 0) {
        QList<int> sizes;
        for (int i = 0; i < n; ++i) sizes << 1;
        split->setSizes(sizes);
      }
      container = split;
    } else { // Grid mode: nested splitters for resizable grid
      // Create a vertical splitter containing one horizontal splitter per row.
      QSplitter *outer = new QSplitter(Qt::Vertical);
      currentSplitters_.push_back(outer);
      int rows = (int)std::ceil(std::sqrt((double)n));
      int cols = (n + rows - 1) / rows;
      int idx = 0;
      for (int r = 0; r < rows; ++r) {
        // how many items in this row
        int itemsInRow = std::min(cols, n - idx);
        if (itemsInRow <= 0) break;
        QSplitter *rowSplit = new QSplitter(Qt::Horizontal);
        currentSplitters_.push_back(rowSplit);
        for (int c = 0; c < itemsInRow; ++c) {
          auto *frame = new SplitFrameWidget(idx);
          // logicalIndex property used for mapping frame -> addresses_ index
          frame->setProperty("logicalIndex", idx);
          frame->setProfile(profile_);
          frame->setAddress(addresses_[idx]);
          connect(frame, &SplitFrameWidget::plusClicked, this, &SplitWindow::onPlusFromFrame);
          connect(frame, &SplitFrameWidget::minusClicked, this, &SplitWindow::onMinusFromFrame);
          connect(frame, &SplitFrameWidget::addressEdited, this, &SplitWindow::onAddressEdited);
          connect(frame, &SplitFrameWidget::upClicked, this, &SplitWindow::onUpFromFrame);
          connect(frame, &SplitFrameWidget::downClicked, this, &SplitWindow::onDownFromFrame);
          connect(frame, &SplitFrameWidget::devToolsRequested, this, &SplitWindow::onFrameDevToolsRequested);
          frame->setMinusEnabled(n > 1);
          frame->setUpEnabled(idx > 0);
          frame->setDownEnabled(idx < n - 1);
          rowSplit->addWidget(frame);
          ++idx;
        }
        // evenly distribute columns in this row
        if (itemsInRow > 0) {
          QList<int> colSizes;
          for (int i = 0; i < itemsInRow; ++i) colSizes << 1;
          rowSplit->setSizes(colSizes);
        }
        outer->addWidget(rowSplit);
      }
      // evenly distribute rows in the outer splitter
      int actualRows = outer->count();
      if (actualRows > 0) {
        QList<int> rowSizes;
        for (int i = 0; i < actualRows; ++i) rowSizes << 1;
        outer->setSizes(rowSizes);
      }
      container = outer;
    }

    if (container) {
      layout_->addWidget(container, 1);
    }

    // add a final stretch with zero so that widgets entirely control spacing
    layout_->addStretch(0);
    central_->update();
    // Update this window's title now that the number of frames may have changed
    // and ensure the Window menus across the app reflect the new title.
    updateWindowTitle();
    rebuildAllWindowMenus();
  }

  void toggleDevToolsForFocusedFrame() {
    // If the shared DevTools view is open, hide it. Otherwise attach it to
    // the focused frame's page (or the first frame) and show it.
    if (sharedDevToolsView_ && sharedDevToolsView_->isVisible()) {
      sharedDevToolsView_->hide();
      return;
    }

    QWidget *fw = QApplication::focusWidget();
    SplitFrameWidget *target = nullptr;
    while (fw) {
      if (auto *f = qobject_cast<SplitFrameWidget *>(fw)) {
        target = f;
        break;
      }
      fw = fw->parentWidget();
    }
    if (!target && central_) target = central_->findChild<SplitFrameWidget *>();
    if (target) {
      QWebEnginePage *p = target->page();
      if (p) {
        createAndAttachSharedDevToolsForPage(p);
        if (sharedDevToolsView_) {
          sharedDevToolsView_->show();
          sharedDevToolsView_->raise();
          sharedDevToolsView_->activateWindow();
        }
      }
    }
  }

  void onPlusFromFrame(SplitFrameWidget *who) {
    // use logicalIndex property assigned during rebuildSections
    const QVariant v = who->property("logicalIndex");
    if (!v.isValid()) return;
    int pos = v.toInt();
    addresses_.insert(addresses_.begin() + pos + 1, QString());
    // persist addresses
    QSettings settings;
    QStringList list;
    for (const auto &a : addresses_) list << a;
    settings.setValue("addresses", list);
    // rebuild UI with the updated addresses_
    rebuildSections((int)addresses_.size());
  }

  void onUpFromFrame(SplitFrameWidget *who) {
    // move this frame up (towards index 0)
    const QVariant v = who->property("logicalIndex");
    if (!v.isValid()) return;
    int pos = v.toInt();
    if (pos <= 0) return; // already at top or not found
    std::swap(addresses_[pos], addresses_[pos - 1]);
    // persist addresses
    QSettings settings;
    QStringList list;
    for (const auto &a : addresses_) list << a;
    settings.setValue("addresses", list);
    rebuildSections((int)addresses_.size());
  }

  void onDownFromFrame(SplitFrameWidget *who) {
    // move this frame down (towards larger indices)
    const QVariant v = who->property("logicalIndex");
    if (!v.isValid()) return;
    int pos = v.toInt();
    if (pos < 0 || pos >= (int)addresses_.size() - 1) return; // at bottom or not found
    std::swap(addresses_[pos], addresses_[pos + 1]);
    // persist addresses
    QSettings settings;
    QStringList list;
    for (const auto &a : addresses_) list << a;
    settings.setValue("addresses", list);
    rebuildSections((int)addresses_.size());
  }

  void setLayoutMode(SplitWindow::LayoutMode m) {
    QSettings settings;

    // If the user re-selects the already-selected layout, treat that as a
    // request to reset splitters to their default sizes. Clear any saved
    // sizes for this layout and rebuild without saving the current sizes.
    if (m == layoutMode_) {
      const QString base = QStringLiteral("splitterSizes/%1").arg(layoutModeKey(layoutMode_));
      settings.remove(base);
      // rebuild so splitters are reset to defaults
      rebuildSections((int)addresses_.size());
      return;
    }

    // Note: we do not save splitter sizes during runtime; sizes are only
    // persisted on application exit. When switching layouts we clear any
    // saved sizes for the target layout so the new layout starts with
    // default splitter positions.
    // Remove any previously-saved sizes for the new target layout so that
    // switching layouts starts with default splitter positions rather than
    // restoring an older saved configuration for that layout.
    {
      const QString targetBase = QStringLiteral("splitterSizes/%1").arg(layoutModeKey(m));
      settings.remove(targetBase);
    }
    // Apply the new layout mode and persist it.
    layoutMode_ = m;
    settings.setValue("layoutMode", (int)layoutMode_);
    // Rebuild UI for the new layout (splitter sizes are only restored at startup)
    rebuildSections((int)addresses_.size());
  }

  void setHeightToScreen() {
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) return;
    const QRect avail = screen->availableGeometry();
    // preserve current x and width, set y to top of available area and
    // height to available height
    const QRect geom = geometry();
    const int x = geom.x();
    const int w = geom.width();
    setGeometry(x, avail.y(), w, avail.height());
  }

  void onMinusFromFrame(SplitFrameWidget *who) {
    if (addresses_.size() <= 1) return; // shouldn't remove last

    const QVariant v = who->property("logicalIndex");
    if (!v.isValid()) return;
    int pos = v.toInt();
    if (pos < 0) return;

    // confirm with the user before removing
    const QMessageBox::StandardButton reply = QMessageBox::question(
      this, tr("Remove section"), tr("Remove this section?"),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    addresses_.erase(addresses_.begin() + pos);
    // persist addresses
    QSettings settings;
    QStringList list;
    for (const auto &a : addresses_) list << a;
    settings.setValue("addresses", list);
    rebuildSections((int)addresses_.size());
  }

  void onAddressEdited(SplitFrameWidget *who, const QString &text) {
    const QVariant v = who->property("logicalIndex");
    if (!v.isValid()) return;
    int pos = v.toInt();
    if (pos < 0) return;
    if (pos < (int)addresses_.size()) {
      addresses_[pos] = text;
      // persist addresses list
      QSettings settings;
      QStringList list;
      for (const auto &a : addresses_) list << a;
      settings.setValue("addresses", list);
    }
  }

  void closeEvent(QCloseEvent *event) override {
    // Persist splitter sizes and either save or remove per-window restore data.
    // If this window has a persistent windowId_ it means it was part of the
    // saved session; when the user explicitly closes the window we remove
    // that saved group so the window will NOT be restored on next launch.
    if (!windowId_.isEmpty()) {
      // If the application is shutting down, persist this window's state so
      // it will be restored on next launch. If the user explicitly closed
      // the window during a running session, remove its saved group so it
      // does not get restored.
      if (qApp && qApp->closingDown()) {
        // During shutdown: save (do not remove) so session is preserved.
        QSettings s;
        {
          GroupScope _gs(s, QStringLiteral("windows/%1").arg(windowId_));
          QStringList list;
          for (const auto &a : addresses_) list << a;
          s.setValue("addresses", list);
          s.setValue("layoutMode", (int)layoutMode_);
          s.setValue("windowGeometry", saveGeometry());
          s.setValue("windowState", saveState());
        }
        // Ensure these shutdown-time writes are flushed to the backend.
        s.sync();
        saveCurrentSplitterSizes(QStringLiteral("windows/%1/splitterSizes").arg(windowId_));
      } else {
        // If other windows exist, remove this window's saved group now.
        // If this is the last window, preserve the saved group so it
        // reopens on next launch.
        saveCurrentSplitterSizes(QStringLiteral("windows/%1/splitterSizes").arg(windowId_));
        const size_t windowsCount = g_windows.size();
        qDebug() << "SplitWindow::closeEvent: g_windows.count (including this)=" << windowsCount;
        if (windowsCount > 1) {
          QSettings s;
          s.beginGroup(QStringLiteral("windows"));
          const QStringList before = s.childGroups();
          if (before.contains(windowId_)) {
            qDebug() << "SplitWindow::closeEvent: removing stored group for" << windowId_;
            s.remove(windowId_);
            s.sync();
          } else {
            qDebug() << "SplitWindow::closeEvent: no stored group for" << windowId_;
          }
          s.endGroup();
        } else {
          qDebug() << "SplitWindow::closeEvent: single window or quitting; preserving stored group for" << windowId_;
        }

        // Schedule deletion; the destroyed() handler will prune g_windows
        // and update menus.
        this->deleteLater();
      }
    } else {
      // no per-window id: persist as legacy/global keys
      saveCurrentSplitterSizes();
      QSettings settings;
      QStringList list;
      for (const auto &a : addresses_) list << a;
      settings.setValue("addresses", list);
      // persist window geometry
      settings.setValue("windowGeometry", saveGeometry());
      // persist window state (toolbars/dock state and maximized/minimized state)
      settings.setValue("windowState", saveState());
    }
    // Refresh all Window menus immediately when this window is closed
    // so other windows reflect the removal without waiting for object
    // destruction. This keeps the Window menu in sync across the app.
    rebuildAllWindowMenus();
    QMainWindow::closeEvent(event);
  }

  // Persist sizes for splitters associated with the current layout mode.
  static QString layoutModeKey(SplitWindow::LayoutMode m) {
    switch (m) {
      case Vertical: return QStringLiteral("vertical");
      case Horizontal: return QStringLiteral("horizontal");
      case Grid: default: return QStringLiteral("grid");
    }
  }

  void saveCurrentSplitterSizes() {
    saveCurrentSplitterSizes(QString());
  }

  void saveCurrentSplitterSizes(const QString &groupPrefix) {
    if (currentSplitters_.empty()) return;
    QSettings settings;
    // If no groupPrefix provided, store under splitterSizes/<layout>/<index>
    if (groupPrefix.isEmpty()) {
      settings.beginGroup(QStringLiteral("splitterSizes"));
      settings.beginGroup(layoutModeKey(layoutMode_));
      for (int i = 0; i < (int)currentSplitters_.size(); ++i) {
        QSplitter *s = currentSplitters_[i];
        if (!s) continue;
        const QList<int> sizes = s->sizes();
        QVariantList vl;
        for (int v : sizes) vl << v;
        settings.setValue(QString::number(i), vl);
      }
      settings.endGroup();
      settings.endGroup();
    } else {
      // Create nested groups for the provided prefix (e.g., windows/<id>/splitterSizes)
      {
        GroupScope _gs(settings, groupPrefix);
        settings.beginGroup(layoutModeKey(layoutMode_));
        for (int i = 0; i < (int)currentSplitters_.size(); ++i) {
          QSplitter *s = currentSplitters_[i];
          if (!s) continue;
          const QList<int> sizes = s->sizes();
          QVariantList vl;
          for (int v : sizes) vl << v;
          settings.setValue(QString::number(i), vl);
        }
        settings.endGroup();
      }
    }
  }

  void restoreSplitterSizes() { restoreSplitterSizes(QString()); }

  void restoreSplitterSizes(const QString &groupPrefix) {
    if (currentSplitters_.empty()) return;
    QSettings settings;
    // If no groupPrefix provided, read from splitterSizes/<layout>/<index>
    if (groupPrefix.isEmpty()) {
      settings.beginGroup(QStringLiteral("splitterSizes"));
      settings.beginGroup(layoutModeKey(layoutMode_));
      for (int i = 0; i < (int)currentSplitters_.size(); ++i) {
        QSplitter *s = currentSplitters_[i];
        if (!s) continue;
        const QVariant v = settings.value(QString::number(i));
        if (!v.isValid()) continue;
        const QVariantList vl = v.toList();
        if (vl.isEmpty()) continue;
        QList<int> sizes;
        sizes.reserve(vl.size());
        for (const QVariant &qv : vl) sizes << qv.toInt();
        if (!sizes.isEmpty()) s->setSizes(sizes);
      }
      settings.endGroup();
      settings.endGroup();
    } else {
      {
        GroupScope _gs(settings, groupPrefix);
        settings.beginGroup(layoutModeKey(layoutMode_));
        for (int i = 0; i < (int)currentSplitters_.size(); ++i) {
          QSplitter *s = currentSplitters_[i];
          if (!s) continue;
          const QVariant v = settings.value(QString::number(i));
          if (!v.isValid()) continue;
          const QVariantList vl = v.toList();
          if (vl.isEmpty()) continue;
          QList<int> sizes;
          sizes.reserve(vl.size());
          for (const QVariant &qv : vl) sizes << qv.toInt();
          if (!sizes.isEmpty()) s->setSizes(sizes);
        }
        settings.endGroup();
      }
    }
  }

  // Slot: a child frame requested DevTools for its page.
  // Use a single shared DevTools view for the whole window
  // and attach it to the requested page.
  void onFrameDevToolsRequested(SplitFrameWidget *who, QWebEnginePage *page, const QPoint &pos) {
    Q_UNUSED(who);
    Q_UNUSED(pos);
    if (!page) return;
    createAndAttachSharedDevToolsForPage(page);
    if (sharedDevToolsView_) {
      sharedDevToolsView_->show();
      sharedDevToolsView_->raise();
      sharedDevToolsView_->activateWindow();
    }
    page->triggerAction(QWebEnginePage::InspectElement);
  }

  // Create (if needed) and attach the single shared DevTools view to the
  // provided inspected page. This mirrors the previous per-frame behavior
  // but uses a single floating DevTools view for all frames.
  void createAndAttachSharedDevToolsForPage(QWebEnginePage *page) {
    if (!page) return;
    if (!sharedDevToolsView_) {
      sharedDevToolsView_ = new QWebEngineView(this);
      sharedDevToolsView_->setWindowFlag(Qt::Tool, true);
      sharedDevToolsView_->setAttribute(Qt::WA_DeleteOnClose);

      QWebEngineProfile *profile = page->profile();
      auto *devPage = new QWebEnginePage(profile, sharedDevToolsView_);
      sharedDevToolsView_->setPage(devPage);

      page->setDevToolsPage(devPage);
      sharedDevToolsView_->resize(980, 720);
      sharedDevToolsView_->setWindowTitle(tr("DevTools"));

      // Add a Close action so Cmd/Ctrl+W will hide the DevTools window
      // rather than destroying it. Hiding preserves the DevTools page
      // and its localStorage/preferences (e.g., theme choice).
      QAction *closeAct = new QAction(sharedDevToolsView_);
      closeAct->setShortcut(QKeySequence::Close);
      connect(closeAct, &QAction::triggered, sharedDevToolsView_, &QWidget::hide);
      sharedDevToolsView_->addAction(closeAct);

      // When the shared devtools view is destroyed (app shutdown), only
      // clear the devToolsPage on the inspected page if it still points
      // to the dev page we created here. Use QPointer guards so we do
      // not dereference raw pointers that may already have been deleted
      // by Qt's shutdown sequence which can cause crashes.
      {
        QPointer<QWebEnginePage> pageGuard(page);
        QPointer<QWebEnginePage> devPageGuard(devPage);
        connect(sharedDevToolsView_, &QObject::destroyed, this, [this, pageGuard, devPageGuard](QObject *) {
          if (pageGuard && pageGuard->devToolsPage() == devPageGuard) pageGuard->setDevToolsPage(nullptr);
          sharedDevToolsView_ = nullptr;
        });
      }
    } else {
      // reattach the existing shared devtools to the new inspected page
      if (page->devToolsPage() != sharedDevToolsView_->page()) {
        page->setDevToolsPage(sharedDevToolsView_->page());
      }
    }
  }

  void showDomPatchesManager() {
    // Create the manager as a modeless dialog so the user can interact with
    // DevTools / frames while editing patches. Reapply patches when the
    // dialog finishes (accepted or rejected) to ensure changes take effect.
    DomPatchesDialog *dlg = new DomPatchesDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
    connect(dlg, &QDialog::finished, this, [this](int) {
      const QList<SplitFrameWidget *> frames = central_->findChildren<SplitFrameWidget *>();
      for (SplitFrameWidget *f : frames) {
        if (auto *p = f->page()) applyDomPatchesToPage(p);
      }
    });
  }

  /**
   * Update this window's Window menu to list all open windows
   */
  void updateWindowMenu() {
    if (!windowMenu_) return;
    windowMenu_->clear();
    QAction *minimizeAct = windowMenu_->addAction(tr("Minimize"));
    minimizeAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));
    connect(minimizeAct, &QAction::triggered, this, &QWidget::showMinimized);
    QAction *closeAct = windowMenu_->addAction(tr("Close Window"));
    closeAct->setShortcut(QKeySequence::Close);
    connect(closeAct, &QAction::triggered, this, &QWidget::close);
    windowMenu_->addSeparator();

    // Use the pre-created icons (created once at app startup) so we don't
    // redraw the icon pixmaps on every menu update.

    // List all windows
    int idx = 1;
    for (SplitWindow *w : g_windows) {
      if (!w) continue;

      // Build the visible title (no prefix); icon column will show diamond.
      QString title = w->windowTitle();
      if (title.isEmpty()) title = QStringLiteral("Window %1").arg(idx);

      const bool minimized = (w->windowState() & Qt::WindowMinimized) || w->isMinimized();
      const bool active = w->isActiveWindow();

      // Use the title as-is; the icon column displays the minimized
      // indicator (diamond) so we don't need a text-prefix fallback.
      QAction *a = windowMenu_->addAction(title);

      // Use our own icons instead of the platform check column so the
      // diamond and active indicator share the same icon column.
      a->setCheckable(false);

      QIcon useIcon = g_windowEmptyIcon;
      if (active && minimized) useIcon = g_windowCheckDiamondIcon;
      else if (active) useIcon = g_windowCheckIcon;
      else if (minimized) useIcon = g_windowDiamondIcon;
      a->setIcon(useIcon);
      a->setIconVisibleInMenu(true);

      connect(a, &QAction::triggered, this, [w]() {
        if (!w) return;
        // Ensure the target window is visible and not minimized before
        // attempting to raise/activate it. On macOS simply calling
        // raise()/activateWindow() may not be sufficient when a window
        // is hidden or minimized.
        if (!w->isVisible()) w->show();
        if (w->isMinimized()) w->showNormal();
        w->raise();
        w->activateWindow();
      });
      ++idx;
    }
  }

protected:
  /**
   * Catch window state changes (minimize/restore) so we can refresh the
   * Window menu indicators immediately when the user minimizes or restores
   * a window. This avoids waiting for focus changes or other signals.
   */
  void changeEvent(QEvent *event) override {
    if (event && event->type() == QEvent::WindowStateChange) {
      // Refresh menus so the minimized/active indicators update.
      rebuildAllWindowMenus();
    }
    QMainWindow::changeEvent(event);
  }

private:
  QWidget *central_ = nullptr;
  QVBoxLayout *layout_ = nullptr;
  // QSpinBox removed; per-frame buttons control section count.
  std::vector<QString> addresses_;
  QWebEngineProfile *profile_ = nullptr;
  LayoutMode layoutMode_ = Vertical;
  std::vector<QSplitter*> currentSplitters_;
  QWebEngineView *sharedDevToolsView_ = nullptr;
  bool restoredOnStartup_ = false;
  QString windowId_;
  QMenu *windowMenu_ = nullptr;
};

/**
 * Global windows list for single-instance multiple-window support
 */
std::vector<SplitWindow*> g_windows;

// Pre-created icons used by the Window menu so we don't draw them on every
// menu rebuild. Created once after QApplication is initialized.
QIcon g_windowDiamondIcon;
QIcon g_windowEmptyIcon;
QIcon g_windowCheckIcon; // checkmark for active window
QIcon g_windowCheckDiamondIcon; // composite check + diamond for active+minimized

static void createWindowMenuIcons() {
  QPixmap emptyPix(16, 16);
  emptyPix.fill(Qt::transparent);
  g_windowEmptyIcon = QIcon(emptyPix);

  QPixmap diamondPix(16, 16);
  diamondPix.fill(Qt::transparent);
  QPainter p(&diamondPix);
  p.setRenderHint(QPainter::Antialiasing);
  QPen pen(QApplication::palette().color(QPalette::WindowText));
  pen.setWidthF(1.25);
  p.setPen(pen);
  p.setBrush(Qt::NoBrush);
  const QPoint center(8, 8);
  const int s = 4;
  QPolygon poly;
  poly << QPoint(center.x(), center.y() - s)
       << QPoint(center.x() + s, center.y())
       << QPoint(center.x(), center.y() + s)
       << QPoint(center.x() - s, center.y());
  p.drawPolygon(poly);
  p.end();
  g_windowDiamondIcon = QIcon(diamondPix);

  // Draw a simple checkmark icon for the active window indicator.
  QPixmap checkPix(16, 16);
  checkPix.fill(Qt::transparent);
  {
    QPainter pc(&checkPix);
    pc.setRenderHint(QPainter::Antialiasing);
    QPen penCheck(QApplication::palette().color(QPalette::WindowText));
    penCheck.setWidthF(1.6);
    penCheck.setCapStyle(Qt::RoundCap);
    penCheck.setJoinStyle(Qt::RoundJoin);
    pc.setPen(penCheck);
    // Simple two-segment check mark
    const QPointF p1(4.0, 8.5);
    const QPointF p2(7.0, 11.5);
    const QPointF p3(12.0, 5.0);
    pc.drawLine(p1, p2);
    pc.drawLine(p2, p3);
    pc.end();
  }
  g_windowCheckIcon = QIcon(checkPix);

  // Composite: check on left, diamond on right so both indicators can
  // appear in a single icon column when the window is active and minimized.
  QPixmap comboPix(16, 16);
  comboPix.fill(Qt::transparent);
  {
    QPainter p2(&comboPix);
    p2.setRenderHint(QPainter::Antialiasing);
    // draw check (left side) reusing the check pen
    QPen penCheck(QApplication::palette().color(QPalette::WindowText));
    penCheck.setWidthF(1.6);
    penCheck.setCapStyle(Qt::RoundCap);
    penCheck.setJoinStyle(Qt::RoundJoin);
    p2.setPen(penCheck);
    p2.drawLine(QPointF(3.0, 8.5), QPointF(6.5, 11.5));
    p2.drawLine(QPointF(6.5, 11.5), QPointF(10.5, 5.0));

    // draw diamond on right (slightly smaller to fit)
    QPen penDiamond(QApplication::palette().color(QPalette::WindowText));
    penDiamond.setWidthF(1.25);
    p2.setPen(penDiamond);
    p2.setBrush(Qt::NoBrush);
    const QPoint center(12, 8);
    const int s = 3;
    QPolygon poly2;
    poly2 << QPoint(center.x(), center.y() - s)
          << QPoint(center.x() + s, center.y())
          << QPoint(center.x(), center.y() + s)
          << QPoint(center.x() - s, center.y());
    p2.drawPolygon(poly2);
    p2.end();
  }
  g_windowCheckDiamondIcon = QIcon(comboPix);
}

static void rebuildAllWindowMenus() {
  for (SplitWindow *w : g_windows) {
    if (w) {
      // Ensure window titles reflect current ordering and counts before
      // rebuilding each window's Window menu.
      w->updateWindowTitle();
      w->refreshWindowMenu();
    }
  }
}

/**
 * Atomically migrate legacy global keys into a per-window group. This
 * function is idempotent and writes a persistent marker "migrationDone"
 * so it only runs once. It also writes a "migratedWindowIds" index so
 * startups on platforms where nested childGroups() may be unreliable can
 * still discover migrated windows.
 */
static void performLegacyMigration() {
  QSettings settings;
  // Fast path: if we've already completed migration, skip. We check both
  // a QSettings marker and a filesystem marker to avoid relying solely on
  // the native settings backend which on some platforms can behave
  // unexpectedly during early startup.
  const QString markerPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QDir::separator() + QStringLiteral("migration_done_v1");
  if (QFile::exists(markerPath) || settings.value(QStringLiteral("migrationDone"), false).toBool()) return;

  // Detect legacy presence
  const QStringList legacyAddresses = settings.value("addresses").toStringList();
  const bool hasLegacy = !legacyAddresses.isEmpty() || settings.contains("windowGeometry") || settings.contains("windowState");
  if (!hasLegacy) {
    // mark migration as done so we don't repeatedly check
    settings.setValue("migrationDone", true);
    settings.sync();
    return;
  }

  // Repair pass: some older code wrote literal keys with slashes (e.g.
  // "windows/<id>/addresses") which won't appear as childGroups() and
  // therefore confuse detection. Move any such slash-containing keys
  // into proper nested groups before proceeding.
  const QStringList allKeys = settings.allKeys();
  QStringList collectedWindowIds;
  for (const QString &fullKey : allKeys) {
    if (!fullKey.startsWith(QStringLiteral("windows/"))) continue;
    // Example fullKey: "windows/<id>/addresses" or "windows/<id>/splitterSizes/vertical/0"
    const QStringList parts = fullKey.split('/', Qt::SkipEmptyParts);
    if (parts.size() < 2) continue; // malformed
    // last element is the actual key name
    const QString last = parts.last();
    QString groupPath = parts.mid(0, parts.size() - 1).join('/'); // "windows/<id>"
    QVariant val = settings.value(fullKey);
    QSettings tmp;
    {
      GroupScope _gs(tmp, groupPath);
      tmp.setValue(last, val);
    }
    tmp.sync();
    qDebug() << "performLegacyMigration: moved literal key into group:" << fullKey << "->" << groupPath << "/" << last;
    // remove the flattened key
    settings.remove(fullKey);
    // remember any ids we repaired
    const QStringList gpParts = groupPath.split('/', Qt::SkipEmptyParts);
    if (gpParts.size() >= 2) collectedWindowIds << gpParts[1];
  }

  // If we repaired any windows from literal keys, write them into the
  // migratedWindowIds index so startup can discover them even if
  // childGroups() behaves oddly on this platform.
  if (!collectedWindowIds.isEmpty()) {
    // deduplicate
    std::sort(collectedWindowIds.begin(), collectedWindowIds.end());
    collectedWindowIds.erase(std::unique(collectedWindowIds.begin(), collectedWindowIds.end()), collectedWindowIds.end());
    settings.setValue("migratedWindowIds", collectedWindowIds);
  }

  // Re-check presence of per-window groups now that we've repaired any
  // flattened keys. If windows exist, we don't need to copy the legacy
  // global keys into a new group — they were already restored above.
  {
    QSettings probe;
    probe.beginGroup(QStringLiteral("windows"));
    const QStringList ids = probe.childGroups();
    probe.endGroup();
    if (!ids.isEmpty()) {
      qDebug() << "performLegacyMigration: found existing window groups after repair:" << ids;
      settings.setValue("migrationDone", true);
      settings.sync();
      // create filesystem marker as a durable guard
      QFile f(markerPath);
      if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toUtf8());
        f.close();
      }
      return;
    }
  }

  // If we still have legacy top-level values (addresses/windowGeometry/windowState)
  // migrate them into a newly-created per-window group and index it.
  const QStringList legacyKeys = { QStringLiteral("addresses"), QStringLiteral("layoutMode"), QStringLiteral("windowGeometry"), QStringLiteral("windowState") };
  const QStringList legacyAddressesList = settings.value("addresses").toStringList();
  const bool stillHasLegacy = !legacyAddressesList.isEmpty() || settings.contains("windowGeometry") || settings.contains("windowState");
  if (stillHasLegacy) {
    const QString newId = QUuid::createUuid().toString();
    QSettings dst;
    {
      GroupScope _gs(dst, QStringLiteral("windows/%1").arg(newId));
      dst.setValue("addresses", legacyAddressesList);
      dst.setValue("layoutMode", settings.value("layoutMode"));
      if (settings.contains("windowGeometry")) dst.setValue("windowGeometry", settings.value("windowGeometry"));
      if (settings.contains("windowState")) dst.setValue("windowState", settings.value("windowState"));
    }
    dst.sync();
    qDebug() << "performLegacyMigration: migrated legacy keys into window id=" << newId;

    // Remove legacy top-level keys
    for (const QString &k : legacyKeys) {
      if (settings.contains(k)) {
        settings.remove(k);
        qDebug() << "performLegacyMigration: removed legacy key:" << k;
      }
    }

    // Update migrated index to include the new id
    QStringList existing = settings.value("migratedWindowIds").toStringList();
    existing << newId;
    settings.setValue("migratedWindowIds", existing);
  }

  // Mark migration done both in QSettings and with the filesystem marker.
  settings.setValue("migrationDone", true);
  settings.sync();
  QFile mf(markerPath);
  if (mf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    mf.write(QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toUtf8());
    mf.close();
  }
}


/**
 * Update this window's title to the form "Group X (N)".
 */
void SplitWindow::updateWindowTitle() {
  // Determine 1-based index in g_windows
  int idx = 0;
  for (size_t i = 0; i < g_windows.size(); ++i) {
    if (g_windows[i] == this) { idx = (int)i + 1; break; }
  }
  const int count = (int)addresses_.size();
  QString title = QStringLiteral("Group %1 (%2)").arg(idx).arg(count);
  if (DEBUG_SHOW_WINDOW_ID && !windowId_.isEmpty()) {
      title += QStringLiteral(" [%1]").arg(windowId_);//_.left(8));
  }
  setWindowTitle(title);
}

void SplitWindow::focusFirstAddress() {
  if (!central_) return;
  // Find the first SplitFrameWidget and its QLineEdit child
  SplitFrameWidget *frame = central_->findChild<SplitFrameWidget *>();
  if (!frame) return;
  QLineEdit *le = frame->findChild<QLineEdit *>();
  if (!le) return;
  le->setFocus(Qt::OtherFocusReason);
  // select all so typing replaces existing content
  le->selectAll();
}

/**
 * Helper to create and show a new SplitWindow; keeps ownership in g_windows.
 */
static void createAndShowWindow(const QString &initialAddress, const QString &windowId) {
  QString id = windowId;
  if (id.isEmpty()) id = QUuid::createUuid().toString();
  // Construct the window with an id. The SplitWindow constructor will
  // attempt to restore saved per-window addresses/layout if the id exists.
  SplitWindow *w = new SplitWindow(id);
  qDebug() << "createAndShowWindow: created window id=" << id << " initialAddress=" << (initialAddress.isEmpty() ? QString("(none)") : initialAddress);
  w->show();
  if (!windowId.isEmpty()) {
    // This is a restored window: the constructor already loaded addresses
    // and rebuilt sections. Do not reset or override addresses here.
  } else if (!initialAddress.isEmpty()) {
    // New ephemeral window requested with an initial address: set it.
    QMetaObject::invokeMethod(w, [w, initialAddress]() {
      SplitFrameWidget *frame = w->findChild<SplitFrameWidget *>();
      if (frame) frame->setAddress(initialAddress);
    }, Qt::QueuedConnection);
  } else {
    // New window without an initial address: ensure a single empty section.
    QMetaObject::invokeMethod(w, "resetToSingleEmptySection", Qt::QueuedConnection);
  }
  // Track the window and remove it from the list when destroyed so we don't keep dangling pointers.
  g_windows.push_back(w);
  qDebug() << "createAndShowWindow: tracked window id=" << id << " g_windows.count=" << g_windows.size();
  // Update the new window's title now that it is tracked in g_windows.
  w->updateWindowTitle();
  QObject::connect(w, &QObject::destroyed, qApp, [w]() {
    g_windows.erase(std::remove_if(g_windows.begin(), g_windows.end(), [w](SplitWindow *x){ return x == w; }), g_windows.end());
    rebuildAllWindowMenus();
  });

  // Ensure all Window menus show the latest list
  rebuildAllWindowMenus();
}

int main(int argc, char **argv) {
  QCoreApplication::setOrganizationName(QStringLiteral("swooby"));
  QCoreApplication::setOrganizationDomain("swooby.com");
  QCoreApplication::setApplicationName(QStringLiteral("Phraim"));

  // Single-instance guard (activation-only): if another process is already
  // running, ask it to activate/focus itself and exit. We do NOT forward
  // command-line args in this simplified mode -- we only request activation.
  const QString serverName = QStringLiteral("swooby_Phraim_server");
  {
    QLocalSocket probe;
    const int maxAttempts = 6;
    bool connected = false;
    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
      probe.connectToServer(serverName);
      if (probe.waitForConnected(250)) { connected = true; break; }
      // give the primary a moment to finish starting up
      QThread::msleep(100);
    }
    if (connected) {
      // Send a tiny activation message (server will ignore payload content).
      const QByteArray msg = QByteArrayLiteral("ACT");
      probe.write(msg);
      probe.flush();
      probe.waitForBytesWritten(200);
      return 0; // exit second instance
    }
  }

  QApplication app(argc, argv);
  app.setWindowIcon(QIcon(QStringLiteral(":/icons/phraim.ico")));

  // Ensure menu action icons are shown on platforms (like macOS) where
  // the Qt default may hide icons in menus.
  app.setAttribute(Qt::AA_DontShowIconsInMenus, false);

  // Refresh Window menus when application focus or state changes so the
  // active/minimized indicators remain accurate across platforms.
  QObject::connect(&app, &QApplication::focusChanged, [](QObject *, QObject *) {
    rebuildAllWindowMenus();
  });
  QObject::connect(qApp, &QGuiApplication::applicationStateChanged, [](Qt::ApplicationState) {
    rebuildAllWindowMenus();
  });

  // Create the small icons used by the Window menu once here (after the
  // QApplication exists so palette colors are available).
  createWindowMenuIcons();

  const QString appDataLocation = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  qDebug() << "Startup paths:";
  qDebug() << "  QStandardPaths::AppDataLocation:" << appDataLocation;
  QSettings settings;
  qDebug() << "  QSettings - format:" << settings.format() << "-> fileName:" << settings.fileName();

  // Perform idempotent legacy migration if required. This centralizes
  // migration behavior (atomic, logged, and only runs once).
  performLegacyMigration();

  // Restore saved windows from last session if present. We store per-window
  // data under QSettings group "windows/<id>".
  {
    QSettings s;
    s.beginGroup(QStringLiteral("windows"));
    QStringList ids = s.childGroups();
    s.endGroup();
    qDebug() << "Startup: persisted window ids:" << ids;
    if (ids.isEmpty()) {
      // Fallback: check explicit migrated index (written during migration)
      const QStringList fallback = settings.value("migratedWindowIds").toStringList();
      if (!fallback.isEmpty()) {
        qDebug() << "Startup: using migratedWindowIds fallback:" << fallback;
        for (const QString &id : fallback) createAndShowWindow(QString(), id);
      } else {
        createAndShowWindow();
      }
    } else {
      for (const QString &id : std::as_const(ids)) {
        qDebug() << "Startup: restoring window id=" << id;
        createAndShowWindow(QString(), id);
      }
    }
  }

  // Create and start the QLocalServer for subsequent instances to connect
  // and ask this process to open windows/URLs.
  QLocalServer *localServer = new QLocalServer(&app);
  // Remove any stale server socket before listening
  QLocalServer::removeServer(serverName);
  if (!localServer->listen(serverName)) {
    qWarning() << "Failed to listen on local server:" << localServer->errorString();
  } else {
    QObject::connect(localServer, &QLocalServer::newConnection, &app, [localServer]() {
      QLocalSocket *client = localServer->nextPendingConnection();
      if (!client) return;
      QObject::connect(client, &QLocalSocket::disconnected, client, &QLocalSocket::deleteLater);
      QObject::connect(client, &QLocalSocket::readyRead, client, [client]() {
        // Activation-only: ignore any payload content and just raise/activate
        // an existing window in this process.
        QMetaObject::invokeMethod(qApp, []() {
          if (!g_windows.empty()) {
            SplitWindow *best = nullptr;
            // prefer an already-active window, otherwise first available
            for (SplitWindow *w : g_windows) {
              if (!w) continue;
              if (w->isActiveWindow()) { best = w; break; }
              if (!best) best = w;
            }
            if (best) {
              if (!best->isVisible()) best->show();
              if (best->isMinimized()) best->showNormal();
              best->raise();
              best->activateWindow();
            }
          }
        }, Qt::QueuedConnection);
        client->disconnectFromServer();
      });
    });
  }

  // Before quitting, persist state for all open windows so the session
  // (window geometry, layout, addresses and splitter sizes) is restored
  // on next launch. This will create per-window groups for windows that
  // did not previously have a persistent id.
  QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
    QSettings s;
    qDebug() << "aboutToQuit: saving" << g_windows.size() << "windows to QSettings";
    for (SplitWindow *w : g_windows) {
      if (!w) continue;
      // Use the new public helper to save each window's persistent state.
      w->savePersistentStateToSettings();
    }
  });

  return app.exec();
}

#include "main.moc"
