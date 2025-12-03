#ifdef Q_OS_WIN

#include "WindowsUpdater.h"
#include <QNetworkRequest>
#include <QFile>
#include <QTemporaryFile>
#include <QDir>
#include <QStandardPaths>
#include <QProcess>
#include <QDateTime>
#include <QDebug>
#include <windows.h>
#include <shellapi.h>

WindowsUpdater::WindowsUpdater(QObject *parent)
  : QObject(parent)
  , networkManager_(new QNetworkAccessManager(this))
  , currentReply_(nullptr)
{
}

void WindowsUpdater::downloadUpdate(const QString &downloadUrl) {
  if (currentReply_) {
    qWarning() << "Download already in progress";
    return;
  }
  
  QNetworkRequest request(downloadUrl);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, 
                      QNetworkRequest::NoLessSafeRedirectPolicy);
  
  currentReply_ = networkManager_->get(request);
  
  connect(currentReply_, &QNetworkReply::downloadProgress,
          this, &WindowsUpdater::onDownloadProgress);
  connect(currentReply_, &QNetworkReply::finished,
          this, &WindowsUpdater::onDownloadFinished);
}

void WindowsUpdater::cancelDownload() {
  if (currentReply_) {
    currentReply_->abort();
    currentReply_->deleteLater();
    currentReply_ = nullptr;
  }
}

void WindowsUpdater::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal) {
  emit downloadProgress(bytesReceived, bytesTotal);
}

void WindowsUpdater::onDownloadFinished() {
  if (!currentReply_) {
    return;
  }
  
  QNetworkReply *reply = currentReply_;
  currentReply_ = nullptr;
  reply->deleteLater();
  
  if (reply->error() != QNetworkReply::NoError) {
    emit downloadFailed(tr("Download failed: %1").arg(reply->errorString()));
    return;
  }
  
  const QByteArray data = reply->readAll();
  if (data.isEmpty()) {
    emit downloadFailed(tr("Downloaded file is empty"));
    return;
  }
  
  const QString installerPath = saveToTempFile(data);
  if (installerPath.isEmpty()) {
    emit downloadFailed(tr("Failed to save installer to disk"));
    return;
  }
  
  emit downloadCompleted(installerPath);
  
  // Automatically launch the installer
  if (launchInstaller(installerPath)) {
    emit installerLaunched();
  } else {
    emit downloadFailed(tr("Failed to launch installer"));
  }
}

QString WindowsUpdater::saveToTempFile(const QByteArray &data) {
  const QString tempPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  const QString fileName = QString("Phraims-Installer-%1.exe")
    .arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
  const QString fullPath = QDir(tempPath).filePath(fileName);
  
  QFile file(fullPath);
  if (!file.open(QIODevice::WriteOnly)) {
    qWarning() << "Failed to open temp file for writing:" << fullPath;
    return QString();
  }
  
  const qint64 written = file.write(data);
  file.close();
  
  if (written != data.size()) {
    qWarning() << "Failed to write complete installer data";
    QFile::remove(fullPath);
    return QString();
  }
  
  return fullPath;
}

bool WindowsUpdater::launchInstaller(const QString &installerPath) {
  // Convert to Windows path format
  QString nativePath = QDir::toNativeSeparators(installerPath);
  
  // Launch with ShellExecuteW to request elevation
  SHELLEXECUTEINFOW sei = {};
  sei.cbSize = sizeof(SHELLEXECUTEINFOW);
  sei.fMask = SEE_MASK_DEFAULT;
  sei.lpVerb = L"runas";  // Request elevation
  sei.lpFile = reinterpret_cast<LPCWSTR>(nativePath.utf16());
  // Note: Assumes NSIS-based installer. Adjust flag for other installer types:
  // - NSIS: /S
  // - MSI: /quiet or /qn
  // - InnoSetup: /VERYSILENT
  sei.lpParameters = L"/S"; // Run installer silently (NSIS silent flag)
  sei.nShow = SW_SHOWNORMAL;
  
  const BOOL result = ShellExecuteExW(&sei);
  
  if (!result) {
    const DWORD error = GetLastError();
    if (error == ERROR_CANCELLED) {
      qWarning() << "User cancelled elevation prompt";
    } else {
      qWarning() << "ShellExecuteExW failed with error:" << error;
    }
    return false;
  }
  
  qDebug() << "Installer launched successfully:" << installerPath;
  return true;
}

#endif // Q_OS_WIN
