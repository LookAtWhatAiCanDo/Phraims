/**
 * Qt6 Widgets Web Brower app that divides the main window into multiple web page frames.
 */
#include <cmath>
#include <vector>
#include <QActionGroup>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDebug>
#include <QDir>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QPalette>
#include <QPointer>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWebEngineHistory>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QUuid>
#include <QFileInfo>
#include <QDateTime>
#include <QDialog>
#include <QListWidget>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QHBoxLayout>
#include <QLabel>
#include <QWidget>

// Qt6 Widgets Web Brower app that divides the main window into multiple web page frames.

/**
 * QWebEngineView subclass to:
 * 1. provide default context menu
 * 2. override createWindow to just navigate to address rather than opening new windows
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

struct DomPatch {
  QString id;
  QString urlPrefix; // match by startsWith
  QString selector;
  QString css; // css declarations (e.g., "display: none;")
  bool enabled = true;
};

// Whether to print verbose DOM-patch internals (injected JS payloads).
// Controlled by the environment variable NVK_DOM_PATCH_VERBOSE (1 to enable).
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

// Apply patches to the given page immediately (and relies on being called
// again on subsequent loads). This uses runJavaScript to insert/remove
// a <style data-dom-patch-id="..."> element scoped to the selector.
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

)JS").arg(idQ).arg(selQ).arg(cssQ);

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

// Simple modal dialog to manage dom patches (list/add/edit/delete)
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
    for (const DomPatch &p : patches_) {
      // show URL prefix, selector and the CSS declarations in the list
      const QString cssPreview = p.css.isEmpty() ? QStringLiteral("(no style)") : p.css;
      const QString enabledSuffix = p.enabled ? QString() : QStringLiteral(" (disabled)");
      QListWidgetItem *it = new QListWidgetItem(
        QStringLiteral("%1 | %2 | %3%4")
          .arg(p.urlPrefix)
          .arg(p.selector)
          .arg(cssPreview)
          .arg(enabledSuffix),
        list_);
      it->setData(Qt::UserRole, p.id);
      it->setToolTip(
        QStringLiteral("Selector: %1\nStyle: %2\nURL prefix: %3")
          .arg(p.selector)
          .arg(p.css)
          .arg(p.urlPrefix)
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

  // Non-modal editor for a single patch. When the user accepts the
  // dialog the patch is either added (isNew==true) or the existing
  // patch is updated. The dialog is heap-allocated and deleted on close.
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

// A self-contained frame used for each split section. Contains a top
// address bar (QLineEdit) and a simple content area below.
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

    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(6, 6, 6, 6);
    v->setSpacing(6);

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

    v->addLayout(topRow);

    // web view content area
    webview_ = new MyWebEngineView(this);
    webview_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    v->addWidget(webview_, 1);

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

  QString address() const { return address_->text(); }
  void setAddress(const QString &s) { address_->setText(s); applyAddress(s); }
  QWebEnginePage *page() const { return webview_ ? webview_->page() : nullptr; }

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
  }

private:
  signals:
  void plusClicked(SplitFrameWidget *who);
  void minusClicked(SplitFrameWidget *who);
  void upClicked(SplitFrameWidget *who);
  void downClicked(SplitFrameWidget *who);
  void addressEdited(SplitFrameWidget *who, const QString &text);
  // Request that the window show/attach a shared DevTools view for this frame
  void devToolsRequested(SplitFrameWidget *who, QWebEnginePage *page, const QPoint &pos);

private:
  QLineEdit *address_ = nullptr;
  MyWebEngineView *webview_ = nullptr;
  QToolButton *upBtn_ = nullptr;
  QToolButton *downBtn_ = nullptr;
  QToolButton *plusBtn_ = nullptr;
  QToolButton *minusBtn_ = nullptr;
  QToolButton *backBtn_ = nullptr;
  QToolButton *forwardBtn_ = nullptr;
  QToolButton *refreshBtn_ = nullptr;
};


class SplitWindow : public QMainWindow {
  Q_OBJECT

public:
  enum LayoutMode { Vertical = 0, Horizontal = 1, Grid = 2 };

  SplitWindow(QWidget *parent = nullptr) : QMainWindow(parent) {
    setWindowTitle(QCoreApplication::applicationName());
    resize(800, 600);

    QSettings settings;

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

    // central scroll area to allow many sections
    auto *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    central_ = new QWidget();
    scroll->setWidget(central_);
    setCentralWidget(scroll);

    layout_ = new QVBoxLayout(central_);
    layout_->setContentsMargins(4, 4, 4, 4);
    layout_->setSpacing(6);

    // load persisted addresses (if present) otherwise start with one empty
    const QStringList saved = settings.value("addresses").toStringList();
    if (saved.isEmpty()) {
      addresses_.push_back(QString());
    } else {
      for (const QString &s : saved) {
        addresses_.push_back(s);
      }
    }
    // build initial UI
    rebuildSections((int)addresses_.size());
    // restore splitter sizes only once at startup (subsequent layout
    // selections/rebuilds should reset splitters to defaults)
    restoreSplitterSizes();
    restoredOnStartup_ = true;

    // restore saved window geometry and window state (position/size/state)
    const QByteArray savedGeom = settings.value("windowGeometry").toByteArray();
    if (!savedGeom.isEmpty()) restoreGeometry(savedGeom);
    const QByteArray savedState = settings.value("windowState").toByteArray();
    if (!savedState.isEmpty()) restoreState(savedState);
  }

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

  void setLayoutMode(LayoutMode m) {
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
    // persist splitter sizes, addresses and window geometry on exit
    saveCurrentSplitterSizes();
    QSettings settings;
    QStringList list;
    for (const auto &a : addresses_) list << a;
    settings.setValue("addresses", list);
    // persist window geometry
    settings.setValue("windowGeometry", saveGeometry());
    // persist window state (toolbars/dock state and maximized/minimized state)
    settings.setValue("windowState", saveState());
    QMainWindow::closeEvent(event);
  }

  // Persist sizes for splitters associated with the current layout mode.
  static QString layoutModeKey(LayoutMode m) {
    switch (m) {
      case Vertical: return QStringLiteral("vertical");
      case Horizontal: return QStringLiteral("horizontal");
      case Grid: default: return QStringLiteral("grid");
    }
  }

  void saveCurrentSplitterSizes() {
    if (currentSplitters_.empty()) return;
    QSettings settings;
    const QString base = QStringLiteral("splitterSizes/%1").arg(layoutModeKey(layoutMode_));
    for (int i = 0; i < (int)currentSplitters_.size(); ++i) {
      QSplitter *s = currentSplitters_[i];
      if (!s) continue;
      const QList<int> sizes = s->sizes();
      QVariantList vl;
      for (int v : sizes) vl << v;
      settings.setValue(base + QStringLiteral("/%1").arg(i), vl);
    }
  }

  void restoreSplitterSizes() {
    if (currentSplitters_.empty()) return;
    QSettings settings;
    const QString base = QStringLiteral("splitterSizes/%1").arg(layoutModeKey(layoutMode_));
    for (int i = 0; i < (int)currentSplitters_.size(); ++i) {
      QSplitter *s = currentSplitters_[i];
      if (!s) continue;
      const QVariant v = settings.value(base + QStringLiteral("/%1").arg(i));
      if (!v.isValid()) continue;
      const QVariantList vl = v.toList();
      if (vl.isEmpty()) continue;
      QList<int> sizes;
      sizes.reserve(vl.size());
      for (const QVariant &qv : vl) sizes << qv.toInt();
      if (!sizes.isEmpty()) s->setSizes(sizes);
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

  // Open the DOM patches manager dialog
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
};

int main(int argc, char **argv) {
  QCoreApplication::setOrganizationName(QStringLiteral("NightVsKnight"));
  QCoreApplication::setOrganizationDomain("nightvsknight.com");
  QCoreApplication::setApplicationName(QStringLiteral("LiveStreamMultiChat"));

  QApplication app(argc, argv);

  const QString appDataLocation = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  qDebug() << "Startup paths:";
  qDebug() << "  QStandardPaths::AppDataLocation:" << appDataLocation;
  QSettings settings;
  qDebug() << "  QSettings - format:" << settings.format() << "-> fileName:" << settings.fileName();

  SplitWindow w;
  w.show();
  return app.exec();
}

#include "main.moc"
