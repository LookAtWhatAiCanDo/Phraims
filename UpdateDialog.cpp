#include "UpdateDialog.h"
#include "UpdateConfig.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextBrowser>
#include <QPushButton>
#include <QProgressBar>
#include <QDesktopServices>
#include <QUrl>
#include <QApplication>
#include <QStyle>
#include <QMessageBox>

#ifdef Q_OS_MACOS
#include "MacSparkleUpdater.h"
#endif

#ifdef Q_OS_WIN
#include "WinSparkleUpdater.h"
#endif

UpdateDialog::UpdateDialog(const UpdateChecker::UpdateInfo &info, QWidget *parent)
  : QDialog(parent)
  , updateInfo_(info)
{
  setupUi();
}

void UpdateDialog::setupUi() {
  setWindowTitle(tr("Update Available"));
  setMinimumSize(500, 400);
  
  auto *mainLayout = new QVBoxLayout(this);
  
  // Title
  titleLabel_ = new QLabel(this);
  titleLabel_->setText(tr("A new version of Phraims is available!"));
  QFont titleFont = titleLabel_->font();
  titleFont.setPointSize(titleFont.pointSize() + 2);
  titleFont.setBold(true);
  titleLabel_->setFont(titleFont);
  mainLayout->addWidget(titleLabel_);
  
  // Version information
  versionLabel_ = new QLabel(this);
  const QString versionText = tr("Current version: %1\nLatest version: %2")
    .arg(updateInfo_.currentVersion)
    .arg(updateInfo_.latestVersion);
  versionLabel_->setText(versionText);
  mainLayout->addWidget(versionLabel_);
  
  mainLayout->addSpacing(10);
  
  // Release notes section
  auto *notesLabel = new QLabel(tr("What's New:"), this);
  QFont notesFont = notesLabel->font();
  notesFont.setBold(true);
  notesLabel->setFont(notesFont);
  mainLayout->addWidget(notesLabel);
  
  releaseNotesBrowser_ = new QTextBrowser(this);
  releaseNotesBrowser_->setOpenExternalLinks(true);
  releaseNotesBrowser_->setMarkdown(updateInfo_.releaseNotes);
  mainLayout->addWidget(releaseNotesBrowser_);
  
  // Progress bar (hidden initially, used for Windows download)
  progressBar_ = new QProgressBar(this);
  progressBar_->setVisible(false);
  mainLayout->addWidget(progressBar_);
  
  // Buttons
  auto *buttonLayout = new QHBoxLayout();
  
  viewNotesButton_ = new QPushButton(tr("View Full Release Notes"), this);
  connect(viewNotesButton_, &QPushButton::clicked, this, &UpdateDialog::onViewReleaseNotesClicked);
  buttonLayout->addWidget(viewNotesButton_);
  
  buttonLayout->addStretch();
  
  remindLaterButton_ = new QPushButton(tr("Remind Me Later"), this);
  connect(remindLaterButton_, &QPushButton::clicked, this, &QDialog::reject);
  buttonLayout->addWidget(remindLaterButton_);
  
  // Platform-specific update button
  updateButton_ = new QPushButton(this);
#ifdef Q_OS_MACOS
  updateButton_->setText(tr("Check for Update"));
#elif defined(Q_OS_WIN)
  updateButton_->setText(tr("Check for Update"));
#else
  updateButton_->setText(tr("Download"));
#endif
  updateButton_->setDefault(true);
  connect(updateButton_, &QPushButton::clicked, this, &UpdateDialog::onUpdateButtonClicked);
  buttonLayout->addWidget(updateButton_);
  
  mainLayout->addLayout(buttonLayout);
}

void UpdateDialog::onUpdateButtonClicked() {
#ifdef Q_OS_MACOS
  // On macOS, try Sparkle first, then fallback to manual download
  if (!triggerSparkleUpdate()) {
    openUrl(updateInfo_.downloadUrl.isEmpty() ? updateInfo_.releaseUrl : updateInfo_.downloadUrl);
    accept();
  }
#elif defined(Q_OS_WIN)
  // On Windows, try WinSparkle first, then fallback to manual download
  if (!triggerWinSparkleUpdate()) {
    openUrl(updateInfo_.downloadUrl.isEmpty() ? updateInfo_.releaseUrl : updateInfo_.downloadUrl);
    accept();
  }
#else
  // Linux: Open release page for manual download
  openUrl(updateInfo_.releaseUrl);
  accept();
#endif
}

void UpdateDialog::onViewReleaseNotesClicked() {
  openUrl(updateInfo_.releaseUrl);
}

void UpdateDialog::openUrl(const QString &url) {
  if (!url.isEmpty()) {
    QDesktopServices::openUrl(QUrl(url));
  }
}

#ifdef Q_OS_MACOS
bool UpdateDialog::triggerSparkleUpdate() {
  // Check if Sparkle is available
  if (!SparkleUpdater::isAvailable()) {
    qDebug() << "Sparkle framework not available, falling back to manual download";
    return false;
  }
  
  // Create Sparkle updater if not already created
  if (!sparkleUpdater_) {
    sparkleUpdater_ = new SparkleUpdater(this);
  }
  
  // Trigger update check
  if (sparkleUpdater_->checkForUpdates()) {
    // Sparkle will handle the rest - close our dialog
    accept();
    return true;
  }
  
  // If Sparkle check failed, fallback to manual download
  return false;
}
#endif

#ifdef Q_OS_WIN
bool UpdateDialog::triggerWinSparkleUpdate() {
  // Check if WinSparkle is available
  if (!WinSparkleUpdater::isAvailable()) {
    qDebug() << "WinSparkle library not available, falling back to manual download";
    return false;
  }
  
  // Create WinSparkle updater if not already created
  if (!winSparkleUpdater_) {
    winSparkleUpdater_ = new WinSparkleUpdater(this);
    
    // Initialize with appcast URL (same feed as macOS Sparkle)
    if (!winSparkleUpdater_->initialize(QString::fromLatin1(UpdateConfig::APPCAST_URL))) {
      qWarning() << "Failed to initialize WinSparkle";
      return false;
    }
  }
  
  // Trigger update check
  if (winSparkleUpdater_->checkForUpdates()) {
    // WinSparkle will handle the rest - close our dialog
    accept();
    return true;
  }
  
  // If WinSparkle check failed, fallback to manual download
  return false;
}
#endif
