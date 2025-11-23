#include "AppSettings.h"
#include "DomPatch.h"
#include "SplitWindow.h"
#include "Utils.h"
#include <algorithm>
#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QPainter>
#include <QPalette>
#include <QStandardPaths>
#include <QThread>
#include <QUuid>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineProfileBuilder>

std::vector<SplitWindow*> g_windows;

QIcon g_windowDiamondIcon;
QIcon g_windowEmptyIcon;
QIcon g_windowCheckIcon;
QIcon g_windowCheckDiamondIcon;

GroupScope::GroupScope(AppSettings &settings, const QString &path) : s(settings) {
  const QStringList parts = path.split('/', Qt::SkipEmptyParts);
  for (const QString &p : parts) { s->beginGroup(p); ++depth; }
}

GroupScope::~GroupScope() {
  for (int i = 0; i < depth; ++i) s->endGroup();
}

void createWindowMenuIcons() {
  QPixmap emptyPix(16, 16);
  emptyPix.fill(Qt::transparent);
  g_windowEmptyIcon = QIcon(emptyPix);

  QPixmap diamondPix(16, 16);
  diamondPix.fill(Qt::transparent);
  QPainter p(&diamondPix);
  p.setRenderHint(QPainter::Antialiasing);
  QPen pen(QApplication::palette().color(QPalette::WindowText));
  pen.setWidthF(1.25);
  p.setPen(pen);
  p.setBrush(Qt::NoBrush);
  const QPoint center(8, 8);
  const int s = 4;
  QPolygon poly;
  poly << QPoint(center.x(), center.y() - s)
       << QPoint(center.x() + s, center.y())
       << QPoint(center.x(), center.y() + s)
       << QPoint(center.x() - s, center.y());
  p.drawPolygon(poly);
  p.end();
  g_windowDiamondIcon = QIcon(diamondPix);

  // Draw a simple checkmark icon for the active window indicator.
  QPixmap checkPix(16, 16);
  checkPix.fill(Qt::transparent);
  {
    QPainter pc(&checkPix);
    pc.setRenderHint(QPainter::Antialiasing);
    QPen penCheck(QApplication::palette().color(QPalette::WindowText));
    penCheck.setWidthF(1.6);
    penCheck.setCapStyle(Qt::RoundCap);
    penCheck.setJoinStyle(Qt::RoundJoin);
    pc.setPen(penCheck);
    // Simple two-segment check mark
    const QPointF p1(4.0, 8.5);
    const QPointF p2(7.0, 11.5);
    const QPointF p3(12.0, 5.0);
    pc.drawLine(p1, p2);
    pc.drawLine(p2, p3);
    pc.end();
  }
  g_windowCheckIcon = QIcon(checkPix);

  // Composite: check on left, diamond on right so both indicators can
  // appear in a single icon column when the window is active and minimized.
  QPixmap comboPix(16, 16);
  comboPix.fill(Qt::transparent);
  {
    QPainter p2(&comboPix);
    p2.setRenderHint(QPainter::Antialiasing);
    // draw check (left side) reusing the check pen
    QPen penCheck(QApplication::palette().color(QPalette::WindowText));
    penCheck.setWidthF(1.6);
    penCheck.setCapStyle(Qt::RoundCap);
    penCheck.setJoinStyle(Qt::RoundJoin);
    p2.setPen(penCheck);
    p2.drawLine(QPointF(3.0, 8.5), QPointF(6.5, 11.5));
    p2.drawLine(QPointF(6.5, 11.5), QPointF(10.5, 5.0));

    // draw diamond on right (slightly smaller to fit)
    QPen penDiamond(QApplication::palette().color(QPalette::WindowText));
    penDiamond.setWidthF(1.25);
    p2.setPen(penDiamond);
    p2.setBrush(Qt::NoBrush);
    const QPoint center(12, 8);
    const int s = 3;
    QPolygon poly2;
    poly2 << QPoint(center.x(), center.y() - s)
          << QPoint(center.x() + s, center.y())
          << QPoint(center.x(), center.y() + s)
          << QPoint(center.x() - s, center.y());
    p2.drawPolygon(poly2);
    p2.end();
  }
  g_windowCheckDiamondIcon = QIcon(comboPix);
}

void rebuildAllWindowMenus() {
  for (SplitWindow *w : g_windows) {
    if (w) {
      // Ensure window titles reflect current ordering and counts before
      // rebuilding each window's Window menu.
      w->updateWindowTitle();
      w->refreshWindowMenu();
    }
  }
}

void createAndShowWindow(const QString &initialAddress, const QString &windowId, bool isIncognito) {
  QString id = windowId;
  // Generate a new ID for windows without one, or for Incognito windows.
  // Incognito windows always get a fresh ID and will not restore from AppSettings
  // even if an ID is provided, since the constructor skips restoration for Incognito.
  if (id.isEmpty() || isIncognito) id = QUuid::createUuid().toString();
  // Construct the window with an id. The SplitWindow constructor will
  // attempt to restore saved per-window addresses/layout if the id exists
  // and the window is not Incognito.
  SplitWindow *w = new SplitWindow(id, isIncognito);
  qDebug() << "createAndShowWindow: created window id=" << id 
           << " initialAddress=" << (initialAddress.isEmpty() ? QString("(none)") : initialAddress)
           << " isIncognito=" << isIncognito;
  w->show();
  if (!windowId.isEmpty() && !isIncognito) {
    // This is a restored window: the constructor already loaded addresses
    // and rebuilt sections. Do not reset or override addresses here.
  } else if (!initialAddress.isEmpty()) {
    // New ephemeral window requested with an initial address: set it.
    QMetaObject::invokeMethod(w, [w, initialAddress]() {
      w->setFirstFrameAddress(initialAddress);
    }, Qt::QueuedConnection);
  } else {
    // New window without an initial address: ensure a single empty section.
    QMetaObject::invokeMethod(w, "resetToSingleEmptySection", Qt::QueuedConnection);
  }
  // Track the window and remove it from the list when destroyed so we don't keep dangling pointers.
  g_windows.push_back(w);
  qDebug() << "createAndShowWindow: tracked window id=" << id << " g_windows.count=" << g_windows.size();
  // Update the new window's title now that it is tracked in g_windows.
  w->updateWindowTitle();
  QObject::connect(w, &QObject::destroyed, qApp, [w]() {
    g_windows.erase(std::remove_if(g_windows.begin(), g_windows.end(), [w](SplitWindow *x){ return x == w; }), g_windows.end());
    rebuildAllWindowMenus();
  });

  // Ensure all Window menus show the latest list
  rebuildAllWindowMenus();
}

void createAndShowIncognitoWindow(const QString &initialAddress) {
  createAndShowWindow(initialAddress, QString(), true);
}

void performLegacyMigration() {
  // No-op placeholder reserved for future settings schema migrations.
}

// Map of profile name -> profile instance for caching
static QMap<QString, QWebEngineProfile*> g_profileCache;

QWebEngineProfile *getProfileByName(const QString &profileName) {
  // Check cache first
  if (g_profileCache.contains(profileName)) {
    return g_profileCache.value(profileName);
  }

  const QString dataRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  const QString profileDir = dataRoot + QStringLiteral("/profiles/") + profileName;
  const QString cacheDir = profileDir + QStringLiteral("/cache");
  QDir().mkpath(profileDir);
  QDir().mkpath(cacheDir);

  QWebEngineProfileBuilder builder;
  builder.setPersistentStoragePath(profileDir);
  builder.setCachePath(cacheDir);
  builder.setHttpCacheType(QWebEngineProfile::DiskHttpCache);
  builder.setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);
  builder.setPersistentPermissionsPolicy(QWebEngineProfile::PersistentPermissionsPolicy::StoreOnDisk);

  QWebEngineProfile *profile = builder.createProfile(QStringLiteral("phraims-") + profileName, qApp);
  qDebug() << "getProfileByName: created profile" << profileName << "storage=" << profile->persistentStoragePath()
           << "cache=" << profile->cachePath() << "offTheRecord=" << profile->isOffTheRecord();

  // Cache the profile
  g_profileCache.insert(profileName, profile);
  return profile;
}

QString currentProfileName() {
  AppSettings s;
  return s->value("currentProfile", QStringLiteral("Default")).toString();
}

void setCurrentProfileName(const QString &profileName) {
  AppSettings s;
  s->setValue("currentProfile", profileName);
  s->sync();
  qDebug() << "setCurrentProfileName:" << profileName;
}

QStringList listProfiles() {
  const QString dataRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  const QString profilesDir = dataRoot + QStringLiteral("/profiles");
  
  QDir dir(profilesDir);
  if (!dir.exists()) {
    // If profiles dir doesn't exist yet, return the default profile.
    // Note: The Default profile directory is created lazily by getProfileByName()
    // on first use, so it may not exist in the filesystem yet.
    return QStringList() << QStringLiteral("Default");
  }

  QStringList profiles = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
  
  // Ensure at least "Default" exists in the list even if its directory hasn't
  // been created yet (lazy creation). This ensures Default is always available.
  if (profiles.isEmpty() || !profiles.contains(QStringLiteral("Default"))) {
    profiles.prepend(QStringLiteral("Default"));
  }
  
  profiles.sort(Qt::CaseInsensitive);
  return profiles;
}

bool isValidProfileName(const QString &name) {
  if (name.isEmpty()) return false;
  if (name.contains('/') || name.contains('\\')) return false;
  return true;
}

bool createProfile(const QString &profileName) {
  if (!isValidProfileName(profileName)) return false;
  
  const QString dataRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  const QString profileDir = dataRoot + QStringLiteral("/profiles/") + profileName;
  
  QDir dir;
  if (dir.exists(profileDir)) {
    qDebug() << "createProfile: profile already exists:" << profileName;
    return false;
  }
  
  if (!dir.mkpath(profileDir)) {
    qWarning() << "createProfile: failed to create directory:" << profileDir;
    return false;
  }
  
  qDebug() << "createProfile: created profile:" << profileName << "at" << profileDir;
  return true;
}

bool renameProfile(const QString &oldName, const QString &newName) {
  if (!isValidProfileName(oldName) || !isValidProfileName(newName) || oldName == newName) return false;
  
  const QString dataRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  const QString oldDir = dataRoot + QStringLiteral("/profiles/") + oldName;
  const QString newDir = dataRoot + QStringLiteral("/profiles/") + newName;
  
  QDir dir;
  if (!dir.exists(oldDir)) {
    qDebug() << "renameProfile: old profile doesn't exist:" << oldName;
    return false;
  }
  
  if (dir.exists(newDir)) {
    qDebug() << "renameProfile: new profile name already exists:" << newName;
    return false;
  }
  
  if (!dir.rename(oldDir, newDir)) {
    qWarning() << "renameProfile: failed to rename directory from" << oldDir << "to" << newDir;
    return false;
  }
  
  // Update cache if the profile is loaded
  if (g_profileCache.contains(oldName)) {
    QWebEngineProfile *profile = g_profileCache.take(oldName);
    g_profileCache.insert(newName, profile);
  }
  
  // Update current profile name if it was renamed
  if (currentProfileName() == oldName) {
    setCurrentProfileName(newName);
  }
  
  qDebug() << "renameProfile: renamed profile from" << oldName << "to" << newName;
  return true;
}

bool deleteProfile(const QString &profileName) {
  if (profileName.isEmpty()) return false;
  
  // Cannot delete if it's the only profile
  QStringList profiles = listProfiles();
  if (profiles.size() <= 1) {
    qDebug() << "deleteProfile: cannot delete the last profile";
    return false;
  }
  
  const QString dataRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  const QString profileDir = dataRoot + QStringLiteral("/profiles/") + profileName;
  
  QDir dir(profileDir);
  if (!dir.exists()) {
    qDebug() << "deleteProfile: profile doesn't exist:" << profileName;
    return false;
  }
  
  // Remove from cache if loaded
  if (g_profileCache.contains(profileName)) {
    // Note: We don't explicitly delete the QWebEngineProfile object because
    // it's parented to qApp and will be cleaned up on application exit.
    // Deleting it prematurely could cause crashes if pages are still using it.
    // Design trade-off: During long sessions with frequent profile deletions,
    // this could accumulate QWebEngineProfile objects in memory. This is
    // acceptable for typical usage patterns where profiles are rarely deleted.
    g_profileCache.remove(profileName);
  }
  
  // Switch to another profile if this is the current one
  if (currentProfileName() == profileName) {
    // Find a profile that isn't being deleted
    QString newProfile;
    for (const QString &p : profiles) {
      if (p != profileName) {
        newProfile = p;
        break;
      }
    }
    // This should always succeed since we checked profiles.size() > 1 above
    if (!newProfile.isEmpty()) {
      setCurrentProfileName(newProfile);
    }
  }
  
  // Delete the directory recursively
  if (!dir.removeRecursively()) {
    qWarning() << "deleteProfile: failed to remove directory:" << profileDir;
    return false;
  }
  
  qDebug() << "deleteProfile: deleted profile:" << profileName;
  return true;
}

QWebEngineProfile *sharedWebEngineProfile() {
  // Use the current profile name
  return getProfileByName(currentProfileName());
}

QWebEngineProfile *createIncognitoProfile() {
  // Create a unique name for this Incognito profile instance
  const QString profileName = QStringLiteral("incognito-") + QUuid::createUuid().toString();
  
  QWebEngineProfileBuilder builder;
  // Off-the-record profile: no persistent storage, all data is ephemeral
  QWebEngineProfile *profile = builder.createOffTheRecordProfile(profileName, qApp);
  qDebug() << "createIncognitoProfile: created off-the-record profile" << profileName
           << "offTheRecord=" << profile->isOffTheRecord();
  
  return profile;
}
