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
        const char* levelStr = "";
        switch (level) {
        case QWebEnginePage::JavaScriptConsoleMessageLevel::InfoMessageLevel:
            levelStr = "Info"; break;
        case QWebEnginePage::JavaScriptConsoleMessageLevel::WarningMessageLevel:
            levelStr = "Warn"; break;
        case QWebEnginePage::JavaScriptConsoleMessageLevel::ErrorMessageLevel:
            levelStr = "Error"; break;
        }
        qInfo().noquote() << "JS[" << levelStr << ":" << sourceID << ":" << lineNumber << "]" << message;
    }
};
