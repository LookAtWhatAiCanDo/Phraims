#pragma once

#include <QFrame>
#include <QPointer>
#include <QWebEngineFullScreenRequest>

class QVBoxLayout;
class QLineEdit;
class QToolButton;
class QLabel;
class QWebEngineProfile;
class QWebEnginePage;
class QWebEngineFullScreenRequest;
class MyWebEngineView;
class EscapeFilter;

/**
 * @brief Self-contained frame widget for each split section.
 *
 * Each SplitFrameWidget represents one section in the split window layout and contains:
 * - Navigation controls (back, forward, refresh) at the top
 * - Address bar for URL input
 * - Section manipulation buttons (+/-/up/down)
 * - Frame zoom controls for the embedded web view
 * - Web view content area
 *
 * The widget handles HTML5 fullscreen requests, DOM patch application,
 * and coordinates with the parent SplitWindow for layout management.
 */
class SplitFrameWidget : public QFrame {
  Q_OBJECT

public:
  /** @brief Minimum allowed scale factor (50%) */
  inline static constexpr double kMinScaleFactor = 0.5;

  /** @brief Maximum allowed scale factor (175%) */
  inline static constexpr double kMaxScaleFactor = 1.75;

  /** @brief Increment applied when nudging scale from UI/shortcuts */
  inline static constexpr double kScaleStep = 0.1;

  /**
   * @brief Constructs a SplitFrameWidget.
   * @param index The visual index of this frame (used for alternating colors)
   * @param parent Optional parent widget
   */
  SplitFrameWidget(int index, QWidget *parent = nullptr);

  /**
   * @brief Returns the QWebEnginePage for this frame.
   * @return Pointer to the web page, or nullptr if not initialized
   */
  QWebEnginePage *page() const;

  /**
   * @brief Gets the current address from the address bar.
   * @return The current URL or address text
   */
  QString address() const;
  
  /**
   * @brief Sets the address bar text without loading.
   * @param s The address or URL to display
   */
  void setAddress(const QString &s);
  
  /**
   * @brief Loads the given address in the web view.
   * @param s The address or URL to load
   *
   * Validates the URL and loads it. Shows instruction HTML for empty/invalid addresses.
   */
  void applyAddress(const QString &s);
  
  /**
   * @brief Updates navigation button states based on history.
   *
   * Enables/disables back, forward, and refresh buttons based on the
   * current web view's navigation history state.
   */
  void updateNavButtons();

  /**
   * @brief Event filter for the address line edit.
   * @param watched The object being watched
   * @param event The event to filter
   * @return true if the event was handled, false otherwise
   *
   * Handles FocusOut events to reset the cursor position to show the
   * left-most characters of the URL.
   */
  bool eventFilter(QObject *watched, QEvent *event) override;

  /**
   * @brief Enables or disables the minus (-) button.
   * @param en true to enable, false to disable
   */
  void setMinusEnabled(bool en);
  
  /**
   * @brief Enables or disables the up (↑) button.
   * @param en true to enable, false to disable
   */
  void setUpEnabled(bool en);
  
  /**
   * @brief Enables or disables the down (↓) button.
   * @param en true to enable, false to disable
   */
  void setDownEnabled(bool en);

  /**
   * @brief Assigns a QWebEngineProfile to this frame's web view.
   * @param profile The profile to use for cookies, cache, and persistence
   *
   * Creates a new QWebEnginePage with the given profile and connects
   * signals for DOM patch application and fullscreen support.
   */
  void setProfile(QWebEngineProfile *profile);

  /**
   * @brief Sets keyboard focus to the address bar.
   *
   * Gives focus to the address QLineEdit and selects all text so the user
   * can immediately start typing a new address. This is called after adding
   * a new frame to streamline the workflow.
   */
  void focusAddress();

  /**
   * @brief Returns the current scale factor applied to this frame.
   * @return Scale multiplier where 1.0 is default size
   */
  double scaleFactor() const;

  /**
   * @brief Applies a specific scale factor to the frame.
   * @param scale Desired scale multiplier (clamped internally)
   * @param notify Whether to emit scaleChanged (used for user-initiated changes)
   */
  void setScaleFactor(double scale, bool notify = false);

private slots:
  /**
   * @brief Handles HTML5 fullscreen requests from the page.
   * @param request The fullscreen request containing toggleOn and origin
   *
   * When entering fullscreen, creates a top-level window and reparents the
   * web view into it. When exiting, restores the web view to this frame.
   * The original window is hidden during fullscreen to avoid visual artifacts.
   */
  void handleFullScreenRequested(QWebEngineFullScreenRequest request);

signals:
  /**
   * @brief Emitted when the plus (+) button is clicked.
   * @param who Pointer to this frame widget
   */
  void plusClicked(SplitFrameWidget *who);
  
  /**
   * @brief Emitted when the minus (-) button is clicked.
   * @param who Pointer to this frame widget
   */
  void minusClicked(SplitFrameWidget *who);
  
  /**
   * @brief Emitted when the up (↑) button is clicked.
   * @param who Pointer to this frame widget
   */
  void upClicked(SplitFrameWidget *who);
  
  /**
   * @brief Emitted when the down (↓) button is clicked.
   * @param who Pointer to this frame widget
   */
  void downClicked(SplitFrameWidget *who);
  
  /**
   * @brief Emitted when the address is edited and confirmed.
   * @param who Pointer to this frame widget
   * @param text The new address text
   */
  void addressEdited(SplitFrameWidget *who, const QString &text);
  
  /**
   * @brief Requests DevTools attachment for this frame.
   * @param who Pointer to this frame widget
   * @param page The QWebEnginePage to inspect
   * @param pos The position where the request originated (for positioning)
   */
  void devToolsRequested(SplitFrameWidget *who, QWebEnginePage *page, const QPoint &pos);

  /**
   * @brief Requests opening a translation URL in a new window.
   * @param who Pointer to this frame widget
   * @param translateUrl The Google Translate URL to open
   */
  void translateRequested(SplitFrameWidget *who, const QUrl &translateUrl);
  
  /**
   * @brief Emitted when the frame's scale changes.
   * @param who Pointer to this frame widget
   * @param scale The new scale multiplier
   */
  void scaleChanged(SplitFrameWidget *who, double scale);

private:
  QVBoxLayout *innerLayout_ = nullptr;  ///< Main layout for the frame
  QLineEdit *address_ = nullptr;        ///< Address bar for URL input
  MyWebEngineView *webview_ = nullptr;  ///< Web view content area
  QToolButton *upBtn_ = nullptr;        ///< Move section up button
  QToolButton *downBtn_ = nullptr;      ///< Move section down button
  QToolButton *plusBtn_ = nullptr;      ///< Add new section button
  QToolButton *minusBtn_ = nullptr;     ///< Remove section button
  QToolButton *backBtn_ = nullptr;      ///< Navigate back button
  QToolButton *forwardBtn_ = nullptr;   ///< Navigate forward button
  QToolButton *refreshBtn_ = nullptr;   ///< Refresh page button
  QLabel *scaleLabel_ = nullptr;        ///< Displays current scale percentage
  QToolButton *scaleDownBtn_ = nullptr; ///< Scale down button
  QToolButton *scaleUpBtn_ = nullptr;   ///< Scale up button
  QToolButton *scaleResetBtn_ = nullptr; ///< Reset scale button

  /** @brief Top-level window created for fullscreen mode */
  QPointer<QWidget> fullScreenWindow_;
  
  /** @brief Previous parent widget before entering fullscreen */
  QPointer<QWidget> previousParent_;
  
  /** @brief Event filter for Escape key during fullscreen */
  QPointer<EscapeFilter> escapeFilter_;
  
  /** @brief Whether the main window was hidden when entering fullscreen */
  bool hidWindowForFullscreen_ = false;
  
  /** @brief Previous window state to restore after exiting fullscreen */
  Qt::WindowStates previousTopWindowState_ = Qt::WindowNoState;

  /** @brief Current scale factor applied to web content */
  double scaleFactor_ = 1.0;

  /** @brief Applies the cached scale factor to fonts, zoom, and labels */
  void applyScale(bool notify);

  /** @brief Adjusts the scale factor by the given delta. */
  void nudgeScale(double delta);

  /** @brief Updates label/button state to reflect current scale. */
  void refreshScaleUi();
};
