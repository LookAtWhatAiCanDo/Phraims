#include "Utils.h"
#include "SplitWindow.h"
#include "DomPatch.h"
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
#include <algorithm>

std::vector<SplitWindow*> g_windows;

QIcon g_windowDiamondIcon;
QIcon g_windowEmptyIcon;
QIcon g_windowCheckIcon;
QIcon g_windowCheckDiamondIcon;

GroupScope::GroupScope(QSettings &settings, const QString &path) : s(settings) {
  const QStringList parts = path.split('/', Qt::SkipEmptyParts);
  for (const QString &p : parts) { s.beginGroup(p); ++depth; }
}

GroupScope::~GroupScope() {
  for (int i = 0; i < depth; ++i) s.endGroup();
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

void createAndShowWindow(const QString &initialAddress, const QString &windowId) {
  QString id = windowId;
  if (id.isEmpty()) id = QUuid::createUuid().toString();
  // Construct the window with an id. The SplitWindow constructor will
  // attempt to restore saved per-window addresses/layout if the id exists.
  SplitWindow *w = new SplitWindow(id);
  qDebug() << "createAndShowWindow: created window id=" << id << " initialAddress=" << (initialAddress.isEmpty() ? QString("(none)") : initialAddress);
  w->show();
  if (!windowId.isEmpty()) {
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

void performLegacyMigration() {
  QSettings settings;
  // Fast path: if we've already completed migration, skip. We check both
  // a QSettings marker and a filesystem marker to avoid relying solely on
  // the native settings backend which on some platforms can behave
  // unexpectedly during early startup.
  const QString markerPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QDir::separator() + QStringLiteral("migration_done_v1");
  if (QFile::exists(markerPath) || settings.value(QStringLiteral("migrationDone"), false).toBool()) return;

  // Detect legacy presence
  const QStringList legacyAddresses = settings.value("addresses").toStringList();
  const bool hasLegacy = !legacyAddresses.isEmpty() || settings.contains("windowGeometry") || settings.contains("windowState");
  if (!hasLegacy) {
    // mark migration as done so we don't repeatedly check
    settings.setValue("migrationDone", true);
    settings.sync();
    return;
  }

  // Repair pass: some older code wrote literal keys with slashes (e.g.
  // "windows/<id>/addresses") which won't appear as childGroups() and
  // therefore confuse detection. Move any such slash-containing keys
  // into proper nested groups before proceeding.
  const QStringList allKeys = settings.allKeys();
  QStringList collectedWindowIds;
  for (const QString &fullKey : allKeys) {
    if (!fullKey.startsWith(QStringLiteral("windows/"))) continue;
    // Example fullKey: "windows/<id>/addresses" or "windows/<id>/splitterSizes/vertical/0"
    const QStringList parts = fullKey.split('/', Qt::SkipEmptyParts);
    if (parts.size() < 2) continue; // malformed
    // last element is the actual key name
    const QString last = parts.last();
    QString groupPath = parts.mid(0, parts.size() - 1).join('/'); // "windows/<id>"
    QVariant val = settings.value(fullKey);
    QSettings tmp;
    {
      GroupScope _gs(tmp, groupPath);
      tmp.setValue(last, val);
    }
    tmp.sync();
    qDebug() << "performLegacyMigration: moved literal key into group:" << fullKey << "->" << groupPath << "/" << last;
    // remove the flattened key
    settings.remove(fullKey);
    // remember any ids we repaired
    const QStringList gpParts = groupPath.split('/', Qt::SkipEmptyParts);
    if (gpParts.size() >= 2) collectedWindowIds << gpParts[1];
  }

  // If we repaired any windows from literal keys, write them into the
  // migratedWindowIds index so startup can discover them even if
  // childGroups() behaves oddly on this platform.
  if (!collectedWindowIds.isEmpty()) {
    // deduplicate
    std::sort(collectedWindowIds.begin(), collectedWindowIds.end());
    collectedWindowIds.erase(std::unique(collectedWindowIds.begin(), collectedWindowIds.end()), collectedWindowIds.end());
    settings.setValue("migratedWindowIds", collectedWindowIds);
  }

  // Re-check presence of per-window groups now that we've repaired any
  // flattened keys. If windows exist, we don't need to copy the legacy
  // global keys into a new group — they were already restored above.
  {
    QSettings probe;
    probe.beginGroup(QStringLiteral("windows"));
    const QStringList ids = probe.childGroups();
    probe.endGroup();
    if (!ids.isEmpty()) {
      qDebug() << "performLegacyMigration: found existing window groups after repair:" << ids;
      settings.setValue("migrationDone", true);
      settings.sync();
      // create filesystem marker as a durable guard
      QFile f(markerPath);
      if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toUtf8());
        f.close();
      }
      return;
    }
  }

  // If we still have legacy top-level values (addresses/windowGeometry/windowState)
  // migrate them into a newly-created per-window group and index it.
  const QStringList legacyKeys = { QStringLiteral("addresses"), QStringLiteral("layoutMode"), QStringLiteral("windowGeometry"), QStringLiteral("windowState") };
  const QStringList legacyAddressesList = settings.value("addresses").toStringList();
  const bool stillHasLegacy = !legacyAddressesList.isEmpty() || settings.contains("windowGeometry") || settings.contains("windowState");
  if (stillHasLegacy) {
    const QString newId = QUuid::createUuid().toString();
    QSettings dst;
    {
      GroupScope _gs(dst, QStringLiteral("windows/%1").arg(newId));
      dst.setValue("addresses", legacyAddressesList);
      dst.setValue("layoutMode", settings.value("layoutMode"));
      if (settings.contains("windowGeometry")) dst.setValue("windowGeometry", settings.value("windowGeometry"));
      if (settings.contains("windowState")) dst.setValue("windowState", settings.value("windowState"));
    }
    dst.sync();
    qDebug() << "performLegacyMigration: migrated legacy keys into window id=" << newId;

    // Remove legacy top-level keys
    for (const QString &k : legacyKeys) {
      if (settings.contains(k)) {
        settings.remove(k);
        qDebug() << "performLegacyMigration: removed legacy key:" << k;
      }
    }

    // Update migrated index to include the new id
    QStringList existing = settings.value("migratedWindowIds").toStringList();
    existing << newId;
    settings.setValue("migratedWindowIds", existing);
  }

  // Mark migration done both in QSettings and with the filesystem marker.
  settings.setValue("migrationDone", true);
  settings.sync();
  QFile mf(markerPath);
  if (mf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    mf.write(QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toUtf8());
    mf.close();
  }
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
  QSettings settings;
  return settings.value("currentProfile", QStringLiteral("Default")).toString();
}

void setCurrentProfileName(const QString &profileName) {
  QSettings settings;
  settings.setValue("currentProfile", profileName);
  settings.sync();
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

bool createProfile(const QString &profileName) {
  if (profileName.isEmpty()) return false;
  
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
  if (oldName.isEmpty() || newName.isEmpty() || oldName == newName) return false;
  
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
    QString newProfile = profiles.first() != profileName ? profiles.first() : profiles.at(1);
    setCurrentProfileName(newProfile);
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
