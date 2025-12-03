#pragma once

#include <QDialog>
#include "UpdateChecker.h"

class QLabel;
class QTextBrowser;
class QPushButton;
class QProgressBar;

#ifdef Q_OS_WIN
class WindowsUpdater;
#endif

#ifdef Q_OS_MACOS
class SparkleUpdater;
#endif

/**
 * @brief Dialog for displaying update information and managing updates.
 *
 * Shows release notes, version information, and provides platform-specific
 * update actions:
 * - macOS: "Update" button triggers Sparkle update flow (if available)
 * - Windows: "Download and Install" button downloads and stages update
 * - Linux: "Download" button opens browser to release page
 */
class UpdateDialog : public QDialog {
  Q_OBJECT

public:
  /**
   * @brief Constructs an UpdateDialog.
   * @param info Update information from UpdateChecker
   * @param parent Optional parent widget
   */
  explicit UpdateDialog(const UpdateChecker::UpdateInfo &info, QWidget *parent = nullptr);

private slots:
  /**
   * @brief Handles the update/download button click.
   *
   * Behavior depends on platform:
   * - macOS: Triggers Sparkle update (or opens download URL if Sparkle unavailable)
   * - Windows: Downloads and installs update
   * - Linux: Opens browser to release page
   */
  void onUpdateButtonClicked();

  /**
   * @brief Opens the release notes URL in the default browser.
   */
  void onViewReleaseNotesClicked();

private:
  /**
   * @brief Initializes the dialog UI.
   */
  void setupUi();

  /**
   * @brief Opens a URL in the system's default browser.
   * @param url The URL to open
   */
  void openUrl(const QString &url);

#ifdef Q_OS_WIN
  /**
   * @brief Downloads and installs the Windows update.
   *
   * Downloads the installer, verifies it (if signature present),
   * and launches it with elevated privileges.
   */
  void downloadAndInstallWindows();
#endif

#ifdef Q_OS_MACOS
  /**
   * @brief Triggers Sparkle update check (if available).
   * @return true if Sparkle handled the update, false otherwise
   */
  bool triggerSparkleUpdate();
#endif

  UpdateChecker::UpdateInfo updateInfo_; ///< Update information to display
  
  // UI elements
  QLabel *titleLabel_ = nullptr;
  QLabel *versionLabel_ = nullptr;
  QTextBrowser *releaseNotesBrowser_ = nullptr;
  QPushButton *updateButton_ = nullptr;
  QPushButton *viewNotesButton_ = nullptr;
  QPushButton *remindLaterButton_ = nullptr;
  QProgressBar *progressBar_ = nullptr;

#ifdef Q_OS_WIN
  WindowsUpdater *windowsUpdater_ = nullptr; ///< Windows updater instance
#endif

#ifdef Q_OS_MACOS
  SparkleUpdater *sparkleUpdater_ = nullptr; ///< macOS Sparkle updater instance
#endif
};
