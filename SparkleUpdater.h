#pragma once

//#ifdef Q_OS_MACOS
#if defined(Q_OS_MACOS) || defined(__APPLE__)

#include <QObject>

/**
 * @brief macOS Sparkle framework integration for auto-updates.
 *
 * This class provides a Qt-friendly wrapper around the Sparkle update framework
 * for macOS. Sparkle handles the complete update flow including:
 * - Checking for updates via appcast feed
 * - Downloading and verifying updates (Ed25519 signatures)
 * - Installing updates without breaking macOS quarantine
 * - Relaunching the application after update
 *
 * Note: This wrapper only works if Sparkle.framework is present in the app bundle.
 * If Sparkle is not available, methods will return false and the app can fallback
 * to manual download links.
 */
class SparkleUpdater : public QObject {
  Q_OBJECT

public:
  /**
   * @brief Constructs a SparkleUpdater.
   * @param parent Optional parent object
   */
  explicit SparkleUpdater(QObject *parent = nullptr);

  /**
   * @brief Destructor - cleans up Sparkle resources.
   */
  ~SparkleUpdater() override;

  /**
   * @brief Checks if Sparkle framework is available.
   * @return true if Sparkle is loaded and functional, false otherwise
   */
  static bool isAvailable();

  /**
   * @brief Triggers a manual update check.
   *
   * Opens the Sparkle update window if an update is available.
   * This is the equivalent of clicking "Check for Updates" in a typical macOS app.
   *
   * @return true if check was initiated, false if Sparkle is unavailable
   */
  bool checkForUpdates();

  /**
   * @brief Enables or disables automatic update checks.
   * @param enabled true to enable automatic checks, false to disable
   * @return true if setting was applied, false if Sparkle is unavailable
   */
  bool setAutomaticCheckEnabled(bool enabled);

  /**
   * @brief Sets the interval for automatic update checks.
   * @param seconds Number of seconds between checks (e.g., 86400 for daily)
   * @return true if setting was applied, false if Sparkle is unavailable
   */
  bool setCheckInterval(int seconds);

private:
  void *updaterController_ = nullptr; ///< Opaque pointer to SPUStandardUpdaterController (Sparkle)
};

#endif // Q_OS_MACOS
