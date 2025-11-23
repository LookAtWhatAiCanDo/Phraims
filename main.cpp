/**
 * Qt6 Widgets web browser that divides each window into multiple resizable web page frames.
 */
#include "AppSettings.h"
#include "SplitWindow.h"
#include "Utils.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QLoggingCategory>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QThread>

int main(int argc, char **argv) {
  QCoreApplication::setOrganizationName(QStringLiteral("LookAtWhatAiCanDo"));
  QCoreApplication::setOrganizationDomain("LookAtWhatAiCanDo.llc");
  QCoreApplication::setApplicationName(QStringLiteral("Phraims"));
  // Single-instance guard (activation-only): if another process is already
  // running, ask it to activate/focus itself and exit. We do NOT forward
  // command-line args in this simplified mode -- we only request activation.
  const QString serverName = QStringLiteral("LookAtWhatAiCanDo_Phraims_server");
  {
    QLocalSocket probe;
    const int maxAttempts = 6;
    bool connected = false;
    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
      probe.connectToServer(serverName);
      if (probe.waitForConnected(250)) { connected = true; break; }
      // give the primary a moment to finish starting up
      QThread::msleep(100);
    }
    if (connected) {
      // Send a tiny activation message (server will ignore payload content).
      const QByteArray msg = QByteArrayLiteral("ACT");
      probe.write(msg);
      probe.flush();
      probe.waitForBytesWritten(200);
      return 0; // exit second instance
    }
  }

  QLoggingCategory::setFilterRules(QStringLiteral("qt.webenginecontext.debug=true"));
  QApplication app(argc, argv);
  app.setWindowIcon(QIcon(QStringLiteral(":/icons/phraims.ico")));

  // Ensure menu action icons are shown on platforms (like macOS) where
  // the Qt default may hide icons in menus.
  app.setAttribute(Qt::AA_DontShowIconsInMenus, false);

  // Refresh Window menus when application focus or state changes so the
  // active/minimized indicators remain accurate across platforms.
  QObject::connect(&app, &QApplication::focusChanged, [](QObject *, QObject *) {
    rebuildAllWindowMenus();
  });
  QObject::connect(qApp, &QGuiApplication::applicationStateChanged, [](Qt::ApplicationState) {
    rebuildAllWindowMenus();
  });

  // Create the small icons used by the Window menu once here (after the
  // QApplication exists so palette colors are available).
  createWindowMenuIcons();

  const QString appDataLocation = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  qDebug() << "Startup paths:";
  qDebug() << "  QStandardPaths::AppDataLocation:" << appDataLocation;
  AppSettings settings; // unified handle to shared settings
  qDebug() << "  AppSettings - format:" << settings->format() << "-> fileName:" << settings->fileName();

  // Perform idempotent legacy migration if required.
  // This centralizes migration behavior (atomic, logged, and only runs once).
  performLegacyMigration();

  // Restore saved windows from last session if present. We store per-window
  // data under AppSettings group "windows/<id>".
  {
    settings->beginGroup(QStringLiteral("windows"));
    QStringList ids = settings->childGroups();
    settings->endGroup();
    qDebug() << "Startup: persisted window ids:" << ids;
    if (ids.isEmpty()) {
      // Fallback: check explicit migrated index (written during migration)
      const QStringList fallback = settings->value("migratedWindowIds").toStringList();
      if (!fallback.isEmpty()) {
        for (const QString &id : fallback) {
            qDebug() << "Startup: restoring fallback window id=" << id;
            createAndShowWindow(QString(), id);
        }
      } else {
        createAndShowWindow();
      }
    } else {
      for (const QString &id : std::as_const(ids)) {
        qDebug() << "Startup: restoring window id=" << id;
        createAndShowWindow(QString(), id);
      }
    }
  }

  // Create and start the QLocalServer for subsequent instances to connect
  // and ask this process to open windows/URLs.
  QLocalServer *localServer = new QLocalServer(&app);
  // Remove any stale server socket before listening
  QLocalServer::removeServer(serverName);
  if (!localServer->listen(serverName)) {
    qWarning() << "Failed to listen on local server:" << localServer->errorString();
  } else {
    QObject::connect(localServer, &QLocalServer::newConnection, &app, [localServer]() {
      QLocalSocket *client = localServer->nextPendingConnection();
      if (!client) return;
      QObject::connect(client, &QLocalSocket::disconnected, client, &QLocalSocket::deleteLater);
      QObject::connect(client, &QLocalSocket::readyRead, client, [client]() {
        // Activation-only: ignore any payload content and just raise/activate
        // an existing window in this process.
        QMetaObject::invokeMethod(qApp, []() {
          if (!g_windows.empty()) {
            SplitWindow *best = nullptr;
            // prefer an already-active window, otherwise first available
            for (SplitWindow *w : g_windows) {
              if (!w) continue;
              if (w->isActiveWindow()) { best = w; break; }
              if (!best) best = w;
            }
            if (best) {
              if (!best->isVisible()) best->show();
              if (best->isMinimized()) best->showNormal();
              best->raise();
              best->activateWindow();
            }
          }
        }, Qt::QueuedConnection);
        client->disconnectFromServer();
      });
    });
  }

  // Before quitting, persist state for all open windows so the session
  // (window geometry, layout, addresses and splitter sizes) is restored
  // on next launch. This will create per-window groups for windows that
  // did not previously have a persistent id.
  QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
    qDebug() << "aboutToQuit: saving" << g_windows.size() << "windows to AppSettings";
    for (SplitWindow *w : g_windows) {
      if (!w) continue;
      // Use the new public helper to save each window's persistent state.
      w->savePersistentStateToSettings();
    }
  });

  return app.exec();
}
