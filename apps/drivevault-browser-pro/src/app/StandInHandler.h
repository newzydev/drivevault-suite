#pragma once

#include <QWebEngineUrlSchemeHandler>

namespace dv::br {

// Serves "dvres:/<name>": the stand-in files (see StandIns.h) that a blocked ad library is replaced with. The scheme must be registered before the application object
// exists (registerScheme()); the handler is installed on every profile by AdBlockController::attach().
class StandInHandler : public QWebEngineUrlSchemeHandler {
    Q_OBJECT
public:
    explicit StandInHandler(QObject* parent = nullptr) : QWebEngineUrlSchemeHandler(parent) {}
    static void registerScheme();
    static QUrl urlFor(const QString& name);
    void requestStarted(QWebEngineUrlRequestJob* job) override;
};

} // namespace dv::br
