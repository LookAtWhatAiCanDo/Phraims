#include "UpdateChecker.h"
#include "version.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QDebug>

#ifdef Q_OS_MACOS
  #include <QSysInfo>
#endif

UpdateChecker::UpdateChecker(QObject *parent)
  : QObject(parent)
  , networkManager_(new QNetworkAccessManager(this))
{
}

void UpdateChecker::checkForUpdates() {
  // GitHub API endpoint for latest release
  const QString apiUrl = QStringLiteral("https://api.github.com/repos/LookAtWhatAiCanDo/Phraims/releases/latest");
  
  QNetworkRequest request(apiUrl);
  request.setHeader(QNetworkRequest::UserAgentHeader, 
                   QString("Phraims/%1").arg(PHRAIMS_VERSION));
  
  QNetworkReply *reply = networkManager_->get(request);
  connect(reply, &QNetworkReply::finished, this, &UpdateChecker::onNetworkReplyFinished);
}

void UpdateChecker::onNetworkReplyFinished() {
  QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
  if (!reply) {
    return;
  }
  
  reply->deleteLater();
  
  if (reply->error() != QNetworkReply::NoError) {
    emit updateCheckFailed(tr("Failed to check for updates: %1").arg(reply->errorString()));
    return;
  }
  
  const QByteArray data = reply->readAll();
  const UpdateInfo info = parseGitHubResponse(data);
  
  if (info.latestVersion.isEmpty()) {
    emit updateCheckFailed(tr("Could not parse update information from GitHub"));
    return;
  }
  
  emit updateCheckCompleted(info);
}

UpdateChecker::UpdateInfo UpdateChecker::parseGitHubResponse(const QByteArray &jsonData) {
  UpdateInfo info;
  info.currentVersion = QString(PHRAIMS_VERSION);
  
  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
  
  if (parseError.error != QJsonParseError::NoError) {
    qWarning() << "Failed to parse GitHub API response:" << parseError.errorString();
    return info;
  }
  
  const QJsonObject root = doc.object();
  
  // Extract version from tag_name (e.g., "v0.56" -> "0.56")
  QString tagName = root.value("tag_name").toString();
  if (tagName.startsWith('v') || tagName.startsWith('V')) {
    tagName = tagName.mid(1);
  }
  info.latestVersion = tagName;
  
  // Extract release information
  info.releaseUrl = root.value("html_url").toString();
  info.releaseNotes = root.value("body").toString();
  
  // Find download URL for current platform
  const QJsonArray assets = root.value("assets").toArray();
  info.downloadUrl = getDownloadUrlForPlatform(assets);
  
  // Compare versions
  const int comparison = compareVersions(info.currentVersion, info.latestVersion);
  info.updateAvailable = (comparison < 0);
  
  return info;
}

QString UpdateChecker::getDownloadUrlForPlatform(const QJsonArray &assets) {
  // Determine the appropriate file pattern for this platform
  QString platformPattern;
  
#ifdef Q_OS_MACOS
  // macOS: Look for .dmg file matching current architecture
  const QString arch = QSysInfo::currentCpuArchitecture();
  // On Apple Silicon, QSysInfo returns "arm64"
  // Also check "aarch64" for compatibility with older Qt versions
  if (arch == "arm64" || arch == "aarch64") {
    platformPattern = "macOS-arm64.dmg";
  } else {
    platformPattern = "macOS-x86_64.dmg";
  }
#elif defined(Q_OS_WIN)
  // Windows: Look for .exe installer
  // Use Qt's cross-compiler architecture detection for better portability
  const QString winArch = QSysInfo::currentCpuArchitecture();
  if (winArch == "arm64" || winArch == "aarch64") {
    platformPattern = "Windows-arm64.exe";
  } else {
    platformPattern = "Windows-x64.exe";
  }
#elif defined(Q_OS_LINUX)
  // Linux: Look for appropriate package (AppImage, deb, etc.)
  // For now, just return the release page URL
  platformPattern = ""; // Will return empty, causing fallback to release page
#endif
  
  // Search assets for matching file
  for (const QJsonValue &assetValue : assets) {
    const QJsonObject asset = assetValue.toObject();
    const QString name = asset.value("name").toString();
    
    if (!platformPattern.isEmpty() && name.contains(platformPattern, Qt::CaseInsensitive)) {
      return asset.value("browser_download_url").toString();
    }
  }
  
  return QString(); // Return empty if no matching asset found
}

int UpdateChecker::compareVersions(const QString &version1, const QString &version2) {
  // Strip any leading 'v' or 'V' prefix
  QString v1 = version1;
  QString v2 = version2;
  
  if (v1.startsWith('v') || v1.startsWith('V')) {
    v1 = v1.mid(1);
  }
  if (v2.startsWith('v') || v2.startsWith('V')) {
    v2 = v2.mid(1);
  }
  
  // Split into components
  const QStringList parts1 = v1.split('.');
  const QStringList parts2 = v2.split('.');
  
  // Compare each component
  const int maxParts = qMax(parts1.size(), parts2.size());
  for (int i = 0; i < maxParts; ++i) {
    const int num1 = (i < parts1.size()) ? parts1[i].toInt() : 0;
    const int num2 = (i < parts2.size()) ? parts2[i].toInt() : 0;
    
    if (num1 < num2) {
      return -1;
    } else if (num1 > num2) {
      return 1;
    }
  }
  
  return 0; // versions are equal
}
