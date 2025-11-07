#include "DomPatch.h"
#include <QCheckBox>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QStandardPaths>
#include <QUuid>
#include <QVBoxLayout>
#include <QWebEnginePage>

bool DEBUG_DOM_PATCH_VERBOSE = 0;

QString domPatchesPath() {
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(root);
    const QString path = root + QDir::separator() + QStringLiteral("dom-patches.json");
    return path;
}

QString escapeForJs(const QString &s) {
  QString out = s;
  out.replace("\\", "\\\\");
  out.replace('\'', "\\'");
  out.replace('"', "\\\"");
  out.replace('\n', ' ');
  out.replace('\r', ' ');
  return out;
}

QList<DomPatch> loadDomPatches() {
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
      if (DEBUG_DOM_PATCH_VERBOSE) qDebug() << "loadDomPatches: cleared cache (file removed):" << path;
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
    if (DEBUG_DOM_PATCH_VERBOSE) qDebug() << "loadDomPatches: cannot open" << path;
    cache.clear();
    cacheMtime = QDateTime();
    return cache;
  }
  const QByteArray b = f.readAll();
  f.close();
  const QJsonDocument d = QJsonDocument::fromJson(b);
  if (!d.isArray()) {
    if (DEBUG_DOM_PATCH_VERBOSE) qDebug() << "loadDomPatches: file exists but JSON is not an array:" << path;
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
  if (DEBUG_DOM_PATCH_VERBOSE) qDebug() << "loadDomPatches: loaded" << cache.size() << "entries from" << path;
  return cache;
}

bool saveDomPatches(const QList<DomPatch> &patches) {
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
  if (DEBUG_DOM_PATCH_VERBOSE) qDebug() << "saveDomPatches: wrote" << arr.size() << "entries to" << path;
  return true;
}

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
      if (DEBUG_DOM_PATCH_VERBOSE) {
        qDebug() << "applyDomPatchesToPage: js=" << js;
      }
      page->runJavaScript(js);
    }
  }
}

DomPatchesDialog::DomPatchesDialog(QWidget *parent) : QDialog(parent) {
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

void DomPatchesDialog::loadList() {
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

void DomPatchesDialog::onAdd() {
  DomPatch p;
  p.id = QUuid::createUuid().toString();
  // Show non-modal editor that will append the patch when the user
  // accepts. The editor works asynchronously so we don't block DevTools.
  editPatchDialog(p, true);
}

void DomPatchesDialog::onEdit() {
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

void DomPatchesDialog::onDelete() {
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

void DomPatchesDialog::editPatchDialog(const DomPatch &p_in, bool isNew) {
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
