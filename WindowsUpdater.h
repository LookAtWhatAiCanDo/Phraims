#pragma once

#ifdef Q_OS_WIN

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkReply>

/**
 * @brief Windows-specific updater for downloading and installing updates.
 *
 * Handles the complete update flow for Windows:
 * 1. Downloads the installer from the provided URL
 * 2. Verifies the download (optional signature check)
 * 3. Stages the installer in a temp location
 * 4. Launches the installer with elevated privileges
 * 5. Exits the application to allow update to proceed
 */
class WindowsUpdater : public QObject {
  Q_OBJECT

public:
  /**
   * @brief Constructs a WindowsUpdater.
   * @param parent Optional parent object
   */
  explicit WindowsUpdater(QObject *parent = nullptr);

  /**
   * @brief Starts downloading the update installer.
   * @param downloadUrl Direct URL to the installer executable
   */
  void downloadUpdate(const QString &downloadUrl);

  /**
   * @brief Cancels an in-progress download.
   */
  void cancelDownload();

signals:
  /**
   * @brief Emitted periodically during download to report progress.
   * @param bytesReceived Number of bytes downloaded so far
   * @param bytesTotal Total size of the download in bytes
   */
  void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);

  /**
   * @brief Emitted when download completes successfully.
   * @param installerPath Path to the downloaded installer file
   */
  void downloadCompleted(const QString &installerPath);

  /**
   * @brief Emitted when download fails.
   * @param errorMessage Human-readable error description
   */
  void downloadFailed(const QString &errorMessage);

  /**
   * @brief Emitted when the installer is launched.
   *
   * The application should exit after receiving this signal to allow
   * the installer to replace the running executable.
   */
  void installerLaunched();

private slots:
  /**
   * @brief Handles download progress updates from QNetworkReply.
   */
  void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);

  /**
   * @brief Handles download completion from QNetworkReply.
   */
  void onDownloadFinished();

private:
  /**
   * @brief Saves the downloaded data to a temporary file.
   * @param data The downloaded installer data
   * @return Path to the saved file, or empty string on failure
   */
  QString saveToTempFile(const QByteArray &data);

  /**
   * @brief Launches the installer with elevated privileges.
   * @param installerPath Path to the installer executable
   * @return true if launcher succeeded, false otherwise
   */
  bool launchInstaller(const QString &installerPath);

  QNetworkAccessManager *networkManager_ = nullptr; ///< Network manager for HTTP requests
  QNetworkReply *currentReply_ = nullptr;           ///< Current download in progress
};

#endif // Q_OS_WIN
