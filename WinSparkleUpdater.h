#pragma once

#ifdef Q_OS_WIN

#include <QObject>

/**
 * @brief Windows WinSparkle framework integration for auto-updates.
 *
 * This class provides a Qt-friendly wrapper around the WinSparkle update framework
 * for Windows. WinSparkle handles the complete update flow including:
 * - Checking for updates via appcast feed
 * - Downloading and verifying updates (DSA/EdDSA signatures)
 * - Installing updates with proper elevation
 * - Relaunching the application after update
 *
 * Note: This wrapper only works if WinSparkle.dll is present and linked.
 * If WinSparkle is not available, methods will return false and the app can fallback
 * to manual download links.
 */
class WinSparkleUpdater : public QObject {
  Q_OBJECT

public:
  /**
   * @brief Constructs a WinSparkleUpdater.
   * @param parent Optional parent object
   */
  explicit WinSparkleUpdater(QObject *parent = nullptr);

  /**
   * @brief Destructor - cleans up WinSparkle resources.
   */
  ~WinSparkleUpdater() override;

  /**
   * @brief Checks if WinSparkle library is available.
   * @return true if WinSparkle is loaded and functional, false otherwise
   */
  static bool isAvailable();

  /**
   * @brief Initializes WinSparkle with appcast URL.
   * @param appcastUrl URL to the Sparkle appcast feed
   * @return true if initialization succeeded, false otherwise
   */
  bool initialize(const QString &appcastUrl);

  /**
   * @brief Triggers a manual update check.
   *
   * Opens the WinSparkle update window if an update is available.
   * This is the equivalent of clicking "Check for Updates" in a typical Windows app.
   *
   * @return true if check was initiated, false if WinSparkle is unavailable
   */
  bool checkForUpdates();

  /**
   * @brief Enables or disables automatic update checks.
   * @param enabled true to enable automatic checks, false to disable
   * @return true if setting was applied, false if WinSparkle is unavailable
   */
  bool setAutomaticCheckEnabled(bool enabled);

  /**
   * @brief Sets the interval for automatic update checks.
   * @param seconds Number of seconds between checks (e.g., 86400 for daily)
   * @return true if setting was applied, false if WinSparkle is unavailable
   */
  bool setCheckInterval(int seconds);

private:
  bool initialized_ = false; ///< Whether WinSparkle has been initialized
};

#endif // Q_OS_WIN
