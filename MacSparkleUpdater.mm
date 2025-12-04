#include "MacSparkleUpdater.h"

#ifdef Q_OS_MACOS

#include <QDebug>

// Import Sparkle framework if available
#if __has_include(<Sparkle/Sparkle.h>)
  #define SPARKLE_AVAILABLE 1
  #import <Sparkle/Sparkle.h>
#else
  #define SPARKLE_AVAILABLE 0
  #warning "Sparkle framework not found - macOS auto-updates will be disabled"
#endif

MacSparkleUpdater::MacSparkleUpdater(QObject *parent)
  : QObject(parent)
  , updaterController_(nullptr)
{
#if SPARKLE_AVAILABLE
  @autoreleasepool {
    // Create Sparkle updater controller
    // SPUStandardUpdaterController manages the update checking and UI
    SPUStandardUpdaterController *controller = [[SPUStandardUpdaterController alloc] 
                                                initWithStartingUpdater:YES 
                                                updaterDelegate:nil 
                                                userDriverDelegate:nil];
    if (controller) {
      updaterController_ = (__bridge_retained void *)controller;
      qDebug() << "Sparkle updater initialized successfully";
    } else {
      qWarning() << "Failed to initialize Sparkle updater controller";
    }
  }
#else
  qWarning() << "MacSparkleUpdater created but Sparkle framework is not available";
#endif
}

MacSparkleUpdater::~MacSparkleUpdater() {
#if SPARKLE_AVAILABLE
  if (updaterController_) {
    @autoreleasepool {
      SPUStandardUpdaterController *controller = (__bridge_transfer SPUStandardUpdaterController *)updaterController_;
      (void)controller; // ARC will release it
      updaterController_ = nullptr;
    }
  }
#endif
}

bool MacSparkleUpdater::isAvailable() {
#if SPARKLE_AVAILABLE
  return YES;
#else
  return false;
#endif
}

bool MacSparkleUpdater::checkForUpdates() {
#if SPARKLE_AVAILABLE
  if (!updaterController_) {
    qWarning() << "MacSparkleUpdater not initialized";
    return false;
  }
  
  @autoreleasepool {
    SPUStandardUpdaterController *controller = (__bridge SPUStandardUpdaterController *)updaterController_;
    [controller checkForUpdates:nil];
    return true;
  }
#else
  qWarning() << "Sparkle framework not available - cannot check for updates";
  return false;
#endif
}

bool MacSparkleUpdater::setAutomaticCheckEnabled(bool enabled) {
#if SPARKLE_AVAILABLE
  if (!updaterController_) {
    return false;
  }
  
  @autoreleasepool {
    SPUStandardUpdaterController *controller = (__bridge SPUStandardUpdaterController *)updaterController_;
    controller.updater.automaticallyChecksForUpdates = enabled ? YES : NO;
    return true;
  }
#else
  Q_UNUSED(enabled);
  return false;
#endif
}

bool MacSparkleUpdater::setCheckInterval(int seconds) {
#if SPARKLE_AVAILABLE
  if (!updaterController_) {
    return false;
  }
  
  @autoreleasepool {
    SPUStandardUpdaterController *controller = (__bridge SPUStandardUpdaterController *)updaterController_;
    controller.updater.updateCheckInterval = seconds;
    return true;
  }
#else
  Q_UNUSED(seconds);
  return false;
#endif
}

#endif // Q_OS_MACOS
