#pragma once

#include <QColor>
#include <QObject>

class QWindow;

namespace dv {

// Thin wrapper around Win32 window decoration features. No-ops on other platforms.
class WindowsIntegration : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    // Makes the native title bar follow the application theme (Windows 10 2004+ / Windows 11).
    // `captionColor` / `textColor` (Windows 11 only) tint the caption to match the app background.
    Q_INVOKABLE void applyTitleBarTheme(QWindow* window, bool dark, const QColor& captionColor, const QColor& textColor) const;
};

} // namespace dv
