#include "WindowsIntegration.h"

#include <QColor>
#include <QWindow>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif
#endif

namespace dv {

void WindowsIntegration::applyTitleBarTheme(QWindow* window, bool dark, const QColor& captionColor,
                                            const QColor& textColor) const
{
#ifdef Q_OS_WIN
    if (!window)
        return;
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd)
        return;

    const BOOL useDark = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));

    // COLORREF is 0x00BBGGRR. Ignored (returns an error) on Windows 10, which is fine.
    auto toColorRef = [](const QColor& c) { return COLORREF(c.red() | (c.green() << 8) | (c.blue() << 16)); };
    if (captionColor.isValid()) {
        const COLORREF caption = toColorRef(captionColor);
        DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
    }
    if (textColor.isValid()) {
        const COLORREF text = toColorRef(textColor);
        DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &text, sizeof(text));
    }
#else
    Q_UNUSED(window)
    Q_UNUSED(dark)
    Q_UNUSED(captionColor)
    Q_UNUSED(textColor)
#endif
}

} // namespace dv
