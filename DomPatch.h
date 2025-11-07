#pragma once

#include <QDialog>
#include <QList>
#include <QString>

class QListWidget;
class QWebEnginePage;

/**
 * @brief Represents a DOM patch that applies CSS rules to matching web pages.
 *
 * Each patch contains a URL prefix for matching, a CSS selector to target,
 * and CSS declarations to apply. Patches are persisted in JSON format.
 */
struct DomPatch {
  QString id;         ///< Unique identifier (UUID)
  QString urlPrefix;  ///< URL prefix to match (using startsWith)
  QString selector;   ///< CSS selector for targeting elements
  QString css;        ///< CSS declarations (e.g., "display: none;")
  bool enabled = true; ///< Whether this patch is currently active
};

/**
 * @brief Returns the file system path to the DOM patches JSON file.
 * @return Absolute path to dom-patches.json in the app data directory
 */
QString domPatchesPath();

/**
 * @brief Checks if verbose DOM patch logging is enabled.
 * @return true if NVK_DOM_PATCH_VERBOSE environment variable is set to 1
 *
 * When enabled, prints injected JavaScript payloads for debugging.
 * Controlled by the environment variable NVK_DOM_PATCH_VERBOSE.
 */
bool domPatchesVerbose();

/**
 * @brief Escapes a string for safe embedding in JavaScript code.
 * @param s The string to escape
 * @return Escaped string safe for use in JS string literals
 *
 * Escapes backslashes, quotes, and newlines to prevent injection.
 */
QString escapeForJs(const QString &s);

/**
 * @brief Loads all DOM patches from persistent storage.
 * @return List of DomPatch objects loaded from dom-patches.json
 *
 * Results are cached and only reloaded when the file modification time changes.
 * Returns an empty list if the file doesn't exist or can't be parsed.
 */
QList<DomPatch> loadDomPatches();

/**
 * @brief Saves DOM patches to persistent storage.
 * @param patches The list of patches to save
 * @return true if successfully saved, false on error
 *
 * Writes patches to dom-patches.json in indented JSON format.
 */
bool saveDomPatches(const QList<DomPatch> &patches);

/**
 * @brief Applies all enabled DOM patches to the given page.
 * @param page The QWebEnginePage to apply patches to
 *
 * Injects CSS rules via JavaScript by creating/updating <style> elements
 * with data-dom-patch-id attributes. Should be called after page loads
 * and on URL changes for single-page app support.
 */
void applyDomPatchesToPage(QWebEnginePage *page);

/**
 * @brief Modeless dialog for managing DOM patches.
 *
 * Provides UI to list, add, edit, and delete DOM patches. The dialog is shown
 * modelessly via show() to allow interaction with the browser while editing.
 */
class DomPatchesDialog : public QDialog {
  Q_OBJECT
public:
  /**
   * @brief Constructs the DOM patches manager dialog.
   * @param parent Optional parent widget
   */
  DomPatchesDialog(QWidget *parent = nullptr);

  /**
   * @brief Returns the current list of patches.
   * @return List of DomPatch objects currently loaded in the dialog
   */
  QList<DomPatch> patches() const { return patches_; }

private slots:
  /**
   * @brief Loads the patch list from storage and populates the UI.
   */
  void loadList();
  
  /**
   * @brief Handler for the Add button - creates a new patch.
   */
  void onAdd();
  
  /**
   * @brief Handler for the Edit button - edits the selected patch.
   */
  void onEdit();
  
  /**
   * @brief Handler for the Delete button - removes the selected patch.
   */
  void onDelete();
  
  /**
   * @brief Shows a dialog for editing a patch.
   * @param p_in The patch to edit (or template for new patch)
   * @param isNew true if creating a new patch, false if editing existing
   */
  void editPatchDialog(const DomPatch &p_in, bool isNew);

private:
  QListWidget *list_ = nullptr;   ///< List widget displaying patches
  QList<DomPatch> patches_;       ///< Current patches loaded from storage
};
