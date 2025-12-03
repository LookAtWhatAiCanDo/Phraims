#include "UpdateDialog.h"
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

#ifdef Q_OS_WIN
#include "WindowsUpdater.h"
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
  updateButton_->setText(tr("Download Update"));
#elif defined(Q_OS_WIN)
  updateButton_->setText(tr("Download and Install"));
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
  downloadAndInstallWindows();
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
  // TODO: Integrate with Sparkle framework
  // For now, return false to use manual download
  return false;
}
#endif

#ifdef Q_OS_WIN
void UpdateDialog::downloadAndInstallWindows() {
  if (updateInfo_.downloadUrl.isEmpty()) {
    // No direct download URL, open release page instead
    openUrl(updateInfo_.releaseUrl);
    accept();
    return;
  }
  
  // Create and configure Windows updater
  if (!windowsUpdater_) {
    windowsUpdater_ = new WindowsUpdater(this);
    
    // Connect progress signals
    connect(windowsUpdater_, &WindowsUpdater::downloadProgress, this,
      [this](qint64 bytesReceived, qint64 bytesTotal) {
        if (bytesTotal > 0) {
          progressBar_->setMaximum(static_cast<int>(bytesTotal));
          progressBar_->setValue(static_cast<int>(bytesReceived));
          progressBar_->setVisible(true);
        }
      });
    
    // Connect completion signals
    connect(windowsUpdater_, &WindowsUpdater::downloadCompleted, this,
      [this](const QString &installerPath) {
        Q_UNUSED(installerPath);
        progressBar_->setVisible(false);
      });
    
    connect(windowsUpdater_, &WindowsUpdater::installerLaunched, this,
      [this]() {
        QMessageBox::information(this, tr("Update Starting"),
          tr("The installer has been launched. Phraims will now exit to complete the update."));
        QApplication::quit();
      });
    
    connect(windowsUpdater_, &WindowsUpdater::downloadFailed, this,
      [this](const QString &errorMessage) {
        progressBar_->setVisible(false);
        QMessageBox::warning(this, tr("Update Failed"), errorMessage);
        updateButton_->setEnabled(true);
      });
  }
  
  // Disable the update button and start download
  updateButton_->setEnabled(false);
  progressBar_->setValue(0);
  progressBar_->setVisible(true);
  windowsUpdater_->downloadUpdate(updateInfo_.downloadUrl);
}
#endif
