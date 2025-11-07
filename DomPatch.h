#pragma once

#include <QDialog>
#include <QList>
#include <QString>

class QListWidget;
class QWebEnginePage;

struct DomPatch {
  QString id;
  QString urlPrefix; // match by startsWith
  QString selector;
  QString css; // css declarations (e.g., "display: none;")
  bool enabled = true;
};

/**
 * Get the path to the DOM patches JSON file
 */
QString domPatchesPath();

/**
 * Whether to print verbose DOM-patch internals (injected JS payloads).
 * Controlled by the environment variable NVK_DOM_PATCH_VERBOSE (1 to enable).
 */
bool domPatchesVerbose();

/**
 * Escape a string for safe embedding in JavaScript
 */
QString escapeForJs(const QString &s);

/**
 * Load DOM patches from persistent storage
 */
QList<DomPatch> loadDomPatches();

/**
 * Save DOM patches to persistent storage
 */
bool saveDomPatches(const QList<DomPatch> &patches);

/**
 * Apply patches to the given page immediately (and relies on being called
 * again on subsequent loads). This uses runJavaScript to insert/remove
 * a <style data-dom-patch-id="..."> element scoped to the selector.
 */
void applyDomPatchesToPage(QWebEnginePage *page);

/**
 * Modeless dialog to list/add/edit/delete DOM patches.
 * Shown modelessly via show() in SplitWindow::showDomPatchesManager.
 */
class DomPatchesDialog : public QDialog {
  Q_OBJECT
public:
  DomPatchesDialog(QWidget *parent = nullptr);

  QList<DomPatch> patches() const { return patches_; }

private slots:
  void loadList();
  void onAdd();
  void onEdit();
  void onDelete();
  void editPatchDialog(const DomPatch &p_in, bool isNew);

private:
  QListWidget *list_ = nullptr;
  QList<DomPatch> patches_;
};
