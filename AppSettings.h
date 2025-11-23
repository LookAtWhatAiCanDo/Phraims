// AppSettings.h
// Lightweight wrapper around a single global QSettings instance stored at
// <AppDataLocation>/settings.ini (INI format).
// Direct QSettings import or usage must be avoided elsewhere in the codebase.
//
// Usage example:
//   AppSettings s; // default-construct a handle to the shared instance
//   s->setValue("foo", 123);

#pragma once

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QString>

class AppSettings {
public:
  // Default handle constructor: references the singleton QSettings.
  AppSettings() : settingsPtr_(&underlying()) {}

  // Backwards-compatible singleton accessor (still allowed in existing code).
  static AppSettings &instance() {
    static AppSettings instSingleton; // handle whose pointer targets underlying()
    return instSingleton;
  }

  // Operators for more natural usage: AppSettings behaves like a QSettings handle.
  QSettings *operator->() { return settingsPtr_; }
  const QSettings *operator->() const { return settingsPtr_; }
  operator QSettings &() { return *settingsPtr_; }
  operator const QSettings &() const { return *settingsPtr_; }

  static QString customSettingsFileName() { return QStringLiteral("settings.ini"); }
  static QString customSettingsPath() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base + QLatin1Char('/') + customSettingsFileName();
  }

private:
  // Create (once) and return the underlying shared QSettings instance.
  static QSettings &underlying() {
    static QSettings *inst = []() {
      const QString path = customSettingsPath();
      QFileInfo info(path);
      QDir dir = info.dir();
      if (!dir.exists()) { dir.mkpath(QStringLiteral(".")); }
      return new QSettings(path, QSettings::IniFormat);
    }();
    return *inst;
  }

  QSettings *settingsPtr_ = nullptr;
};
