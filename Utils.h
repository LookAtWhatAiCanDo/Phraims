#pragma once

#include <QString>
#include <QSettings>
#include <QIcon>
#include <QUuid>
#include <vector>

class SplitWindow;
class QWebEnginePage;
class QWebEngineProfile;

/**
 * @brief RAII helper for managing nested QSettings group paths.
 *
 * Automatically begins nested QSettings groups from a path like "windows/<id>/splitterSizes"
 * and ensures all groups are properly ended when the object goes out of scope.
 * This prevents mismatched endGroup() calls that can occur with early returns.
 */
struct GroupScope {
  QSettings &s;        ///< Reference to the QSettings object being scoped
  int depth = 0;       ///< Number of nested groups opened
  
  /**
   * @brief Constructs a GroupScope and opens nested groups.
   * @param settings The QSettings object to scope
   * @param path The group path with '/' separators (e.g., "windows/id/sizes")
   */
  GroupScope(QSettings &settings, const QString &path);
  
  /**
   * @brief Destructor that closes all opened groups.
   */
  ~GroupScope();
};

/**
 * @brief Global list of all open SplitWindow instances.
 *
 * Used for single-instance multiple-window support, window menu management,
 * and coordinating actions across all windows.
 */
extern std::vector<SplitWindow*> g_windows;

/**
 * @brief Pre-created icon showing a diamond shape (for minimized window indicator).
 */
extern QIcon g_windowDiamondIcon;

/**
 * @brief Pre-created empty icon for window menu spacing.
 */
extern QIcon g_windowEmptyIcon;

/**
 * @brief Pre-created checkmark icon for active window indicator.
 */
extern QIcon g_windowCheckIcon;

/**
 * @brief Pre-created combo icon showing both checkmark and diamond.
 */
extern QIcon g_windowCheckDiamondIcon;

/**
 * @brief Creates all window menu icons once at application startup.
 *
 * These icons are drawn once and reused to avoid redrawing on every menu update.
 * Must be called after QApplication is initialized so palette colors are available.
 */
void createWindowMenuIcons();

/**
 * @brief Rebuilds the Window menu for all open windows.
 *
 * Updates window titles and refreshes the Window menu in all SplitWindow instances
 * to reflect current window states (active, minimized, ordering).
 */
void rebuildAllWindowMenus();

/**
 * @brief Creates a new SplitWindow and adds it to the global windows list.
 * @param initialAddress Optional URL to load in the first frame
 * @param windowId Optional UUID for restoring saved window state
 *
 * The window is shown immediately and ownership is managed by g_windows.
 * If windowId is provided, the window restores its saved state from QSettings.
 */
void createAndShowWindow(const QString &initialAddress = QString(), const QString &windowId = QString());

/**
 * @brief Performs one-time migration of legacy global settings to per-window settings.
 *
 * This function is idempotent and writes a persistent marker so it only runs once.
 * It atomically migrates old global keys (addresses, windowGeometry) into per-window
 * QSettings groups under "windows/<id>/".
 */
void performLegacyMigration();

/**
 * @brief Returns the shared persistent QWebEngineProfile used by all windows.
 *
 * The profile is created lazily on first call, configured for disk-backed
 * cookies/cache, and re-used for every SplitWindow instance.
 */
QWebEngineProfile *sharedWebEngineProfile();

/**
 * @brief Returns a QWebEngineProfile for the specified profile name.
 * @param profileName The name of the profile to retrieve or create
 * @return Pointer to the profile, created if it doesn't exist
 *
 * Profiles are cached and reused. Each profile has its own persistent storage
 * directory for cookies, cache, and other data.
 */
QWebEngineProfile *getProfileByName(const QString &profileName);

/**
 * @brief Returns the name of the currently active profile.
 * @return The current profile name (defaults to "Default")
 */
QString currentProfileName();

/**
 * @brief Sets the active profile for new windows and operations.
 * @param profileName The name of the profile to make active
 *
 * Persists the choice to QSettings so it's restored on next launch.
 */
void setCurrentProfileName(const QString &profileName);

/**
 * @brief Returns a list of all existing profile names.
 * @return QStringList of profile names, sorted alphabetically
 *
 * Scans the profiles directory to discover all available profiles.
 */
QStringList listProfiles();

/**
 * @brief Creates a new profile with the given name.
 * @param profileName The name for the new profile
 * @return True if the profile was created successfully, false if it already exists
 *
 * Creates the profile directory structure but doesn't switch to it automatically.
 */
bool createProfile(const QString &profileName);

/**
 * @brief Renames an existing profile.
 * @param oldName The current name of the profile
 * @param newName The new name for the profile
 * @return True if renamed successfully, false if old doesn't exist or new already exists
 *
 * Updates the filesystem directory and any references in QSettings.
 */
bool renameProfile(const QString &oldName, const QString &newName);

/**
 * @brief Deletes a profile and all its data.
 * @param profileName The name of the profile to delete
 * @return True if deleted successfully, false if it doesn't exist or is the last profile
 *
 * Permanently removes the profile directory and all associated data.
 * Cannot delete the last remaining profile.
 */
bool deleteProfile(const QString &profileName);

/**
 * @brief Applies all enabled DOM patches to the given page immediately.
 * @param page The QWebEnginePage to apply patches to
 *
 * Loads patches from persistent storage and injects CSS rules via JavaScript.
 * Should be called after each page load and on URL changes for single-page apps.
 */
void applyDomPatchesToPage(QWebEnginePage *page);
