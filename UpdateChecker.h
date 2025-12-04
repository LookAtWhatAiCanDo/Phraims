#pragma once

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkReply>

/**
 * @brief Cross-platform update checker for Phraims.
 *
 * Fetches the latest release version from GitHub and compares it to the
 * currently running version. Handles platform-specific update flows:
 * - macOS: Delegates to Sparkle framework (if available)
 * - Windows: Downloads and verifies installer, then stages update
 * - Linux: Shows notification with download link (manual update)
 */
class UpdateChecker : public QObject {
  Q_OBJECT

public:
  /**
   * @brief Update check result information.
   */
  struct UpdateInfo {
    QString latestVersion;     ///< Latest available version (e.g., "0.56")
    QString currentVersion;    ///< Currently running version
    QString releaseUrl;        ///< URL to release page on GitHub
    QString downloadUrl;       ///< Direct download URL for current platform
    QString releaseNotes;      ///< Release notes/changelog
    bool updateAvailable = false; ///< True if latestVersion > currentVersion
  };

  /**
   * @brief Constructs an UpdateChecker.
   * @param parent Optional parent object
   */
  explicit UpdateChecker(QObject *parent = nullptr);

  /**
   * @brief Checks for updates asynchronously.
   *
   * Fetches the latest release information from GitHub API and emits
   * updateCheckCompleted() when done.
   */
  void checkForUpdates();

  /**
   * @brief Compares two version strings.
   * @param version1 First version (e.g., "0.55")
   * @param version2 Second version (e.g., "0.56")
   * @return -1 if version1 < version2, 0 if equal, 1 if version1 > version2
   *
   * Handles semantic versioning (MAJOR.MINOR.PATCH) and simple MAJOR.MINOR versions.
   */
  static int compareVersions(const QString &version1, const QString &version2);

signals:
  /**
   * @brief Emitted when update check completes successfully.
   * @param info The update information retrieved from GitHub
   */
  void updateCheckCompleted(const UpdateInfo &info);

  /**
   * @brief Emitted when update check fails.
   * @param errorMessage Human-readable error description
   */
  void updateCheckFailed(const QString &errorMessage);

private slots:
  /**
   * @brief Handles the network reply from GitHub API.
   */
  void onNetworkReplyFinished();

private:
  /**
   * @brief Parses the GitHub API JSON response.
   * @param jsonData The raw JSON response from GitHub
   * @return UpdateInfo structure with parsed data
   */
  UpdateInfo parseGitHubResponse(const QByteArray &jsonData);

  /**
   * @brief Determines the download URL for the current platform.
   * @param assets Array of asset objects from GitHub release
   * @return Direct download URL for the appropriate platform binary
   */
  QString getDownloadUrlForPlatform(const QJsonArray &assets);

  QNetworkAccessManager *networkManager_ = nullptr; ///< Network manager for HTTP requests
};
