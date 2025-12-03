#pragma once

#include <QWebEnginePage>
#include <QWebEngineNavigationRequest>
#include <QDebug>

class MyWebEnginePage : public QWebEnginePage {
    Q_OBJECT
public:
    using QWebEnginePage::QWebEnginePage;

signals:
    /**
     * @brief Emitted when a link should open in a new frame (e.g., Ctrl/Cmd+click).
     * @param url The URL to open in a new frame
     */
    void openInNewFrameRequested(const QUrl &url);

protected:
    /**
     * @brief Creates a new window for navigation requests.
     * @param type The type of window being requested
     * @return A new page instance or nullptr
     *
     * This method is called when a link is clicked with modifiers (Ctrl/Cmd+click)
     * or when JavaScript requests a new window. For background tabs/windows
     * (typically Ctrl/Cmd+click), we emit a signal to open the link in a new frame.
     * For popup windows, we return nullptr to allow the default behavior.
     */
    QWebEnginePage *createWindow(QWebEnginePage::WebWindowType type) override {
        qDebug() << "MyWebEnginePage::createWindow: type=" << type;
        
        // WebBrowserBackgroundTab is triggered by Ctrl/Cmd+click on links
        // This is what we want to intercept to open in a new frame
        if (type == QWebEnginePage::WebBrowserBackgroundTab) {
            qDebug() << "MyWebEnginePage::createWindow: background tab requested (Ctrl/Cmd+click)";
            // Create a temporary page to capture the navigation request.
            // We can't just emit a signal here because the URL isn't known yet;
            // it will be delivered via navigation on the returned page.
            auto *tempPage = new MyWebEnginePage(profile(), nullptr);
            
            // Connect to acceptNavigationRequest on the temp page to intercept the URL
            // We use a unique connection that disconnects after first use
            QMetaObject::Connection *conn = new QMetaObject::Connection();
            *conn = connect(tempPage, &QWebEnginePage::navigationRequested, this,
                [this, tempPage, conn](QWebEngineNavigationRequest &request) {
                    qDebug() << "MyWebEnginePage: captured navigation request for new frame:" << request.url();
                    if (request.url().isValid() && !request.url().isEmpty()) {
                        emit openInNewFrameRequested(request.url());
                    }
                    // Reject the navigation since we're opening in a new frame instead
                    request.reject();
                    // Clean up the temporary page and connection
                    QObject::disconnect(*conn);
                    delete conn;
                    tempPage->deleteLater();
                });
            
            return tempPage;
        }
        
        // For popup windows, return nullptr to use default behavior (load in same view)
        qDebug() << "MyWebEnginePage::createWindow: returning nullptr for type" << type;
        return nullptr;
    }

    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level,
                                  const QString& message,
                                  int lineNumber,
                                  const QString& sourceID) override {
        const char* levelStr;
        switch (level) {
        case QWebEnginePage::JavaScriptConsoleMessageLevel::InfoMessageLevel:
            levelStr = "Info"; break;
        case QWebEnginePage::JavaScriptConsoleMessageLevel::WarningMessageLevel:
            levelStr = "Warn"; break;
        case QWebEnginePage::JavaScriptConsoleMessageLevel::ErrorMessageLevel:
            levelStr = "Error"; break;
        default:
            levelStr = "Unknown"; break;
        }
        auto formatted = QStringLiteral("JS[%1:%2:%3] %4")
                           .arg(levelStr)
                           .arg(sourceID)
                           .arg(lineNumber)
                           .arg(message);
        switch (level) {
        case WarningMessageLevel:
            qWarning().noquote() << formatted;
            break;
        case ErrorMessageLevel:
            qCritical().noquote() << formatted;
            break;
        default:
            qInfo().noquote() << formatted;
            break;
        }
    }
};
