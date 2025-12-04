#pragma once

#include <QString>

/**
 * @brief Shared configuration for auto-update system.
 *
 * Centralizes update-related constants to avoid duplication across
 * platform-specific implementations and configuration files.
 */
namespace UpdateConfig {
  /**
   * @brief URL to the Sparkle/WinSparkle appcast feed.
   *
   * This feed is used by both macOS (Sparkle) and Windows (WinSparkle)
   * for checking and downloading updates.
   *
   * Note: This value is also used in resources/Info.plist.in for macOS.
   * Keep them synchronized manually until build-time substitution is implemented.
   */
  constexpr const char* APPCAST_URL = "https://github.com/LookAtWhatAiCanDo/Phraims/releases/latest/download/appcast.xml";
}
