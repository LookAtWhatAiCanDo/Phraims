#pragma once

#include <QString>
#include <QSettings>
#include <QIcon>
#include <QUuid>
#include <vector>

class SplitWindow;
class QWebEnginePage;

/**
 * RAII helper to begin/end a nested QSettings group path like
 * "windows/<id>/splitterSizes". Prefer this over manual begin/end
 * calls to avoid mismatched endGroup() calls on early returns.
 */
struct GroupScope {
  QSettings &s;
  int depth = 0;
  GroupScope(QSettings &settings, const QString &path);
  ~GroupScope();
};

/**
 * Global windows list for single-instance multiple-window support
 */
extern std::vector<SplitWindow*> g_windows;

/**
 * Pre-created icons used by the Window menu so we don't draw them on every
 * menu rebuild. Created once after QApplication is initialized.
 */
extern QIcon g_windowDiamondIcon;
extern QIcon g_windowEmptyIcon;
extern QIcon g_windowCheckIcon;
extern QIcon g_windowCheckDiamondIcon;

/**
 * Create the window menu icons once at startup
 */
void createWindowMenuIcons();

/**
 * Rebuild all window menus across all open windows
 */
void rebuildAllWindowMenus();

/**
 * Helper to create and show a new SplitWindow; keeps ownership in g_windows.
 */
void createAndShowWindow(const QString &initialAddress = QString(), const QString &windowId = QString());

/**
 * Atomically migrate legacy global keys into a per-window group. This
 * function is idempotent and writes a persistent marker "migrationDone"
 * so it only runs once.
 */
void performLegacyMigration();

/**
 * Apply DOM patches to the given page immediately.
 */
void applyDomPatchesToPage(QWebEnginePage *page);
