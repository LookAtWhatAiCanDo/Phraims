#pragma once

#include <QWebEnginePage>

class MyWebEnginePage : public QWebEnginePage {
    Q_OBJECT
public:
    using QWebEnginePage::QWebEnginePage;
protected:
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
