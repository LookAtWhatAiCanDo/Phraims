#include "WinSparkleUpdater.h"

#ifdef Q_OS_WIN

#include <QDebug>
#include <QString>

// Import WinSparkle library if available
#if __has_include(<winsparkle.h>)
  #define WINSPARKLE_AVAILABLE 1
  #include <winsparkle.h>
#else
  #define WINSPARKLE_AVAILABLE 0
  #pragma message("WinSparkle library not found - Windows auto-updates will be disabled")
#endif

WinSparkleUpdater::WinSparkleUpdater(QObject *parent)
  : QObject(parent)
  , initialized_(false)
{
}

WinSparkleUpdater::~WinSparkleUpdater() {
#if WINSPARKLE_AVAILABLE
  if (initialized_) {
    // Cleanup WinSparkle
    win_sparkle_cleanup();
    initialized_ = false;
    qDebug() << "WinSparkle cleaned up";
  }
#endif
}

bool WinSparkleUpdater::isAvailable() {
#if WINSPARKLE_AVAILABLE
  return true;
#else
  return false;
#endif
}

bool WinSparkleUpdater::initialize(const QString &appcastUrl) {
#if WINSPARKLE_AVAILABLE
  if (initialized_) {
    qWarning() << "WinSparkle already initialized";
    return true;
  }

  // Set appcast URL
  const std::string urlStr = appcastUrl.toStdString();
  win_sparkle_set_appcast_url(urlStr.c_str());

  // Initialize WinSparkle
  win_sparkle_init();
  initialized_ = true;
  
  qDebug() << "WinSparkle initialized with appcast URL:" << appcastUrl;
  return true;
#else
  Q_UNUSED(appcastUrl);
  qWarning() << "WinSparkle not available - cannot initialize";
  return false;
#endif
}

bool WinSparkleUpdater::checkForUpdates() {
#if WINSPARKLE_AVAILABLE
  if (!initialized_) {
    qWarning() << "WinSparkle not initialized";
    return false;
  }

  win_sparkle_check_update_with_ui();
  return true;
#else
  qWarning() << "WinSparkle not available - cannot check for updates";
  return false;
#endif
}

bool WinSparkleUpdater::setAutomaticCheckEnabled(bool enabled) {
#if WINSPARKLE_AVAILABLE
  if (!initialized_) {
    return false;
  }

  win_sparkle_set_automatic_check_for_updates(enabled ? 1 : 0);
  return true;
#else
  Q_UNUSED(enabled);
  return false;
#endif
}

bool WinSparkleUpdater::setCheckInterval(int seconds) {
#if WINSPARKLE_AVAILABLE
  if (!initialized_) {
    return false;
  }

  win_sparkle_set_update_check_interval(seconds);
  return true;
#else
  Q_UNUSED(seconds);
  return false;
#endif
}

#endif // Q_OS_WIN
