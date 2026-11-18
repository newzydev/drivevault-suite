#include "WinUtil.h"

#include <QDir>
#include <QStringList>

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

#include <algorithm>
#include <vector>

namespace dm::win {

QString versionInfo(const QString& file, const QString& key)
{
#ifdef Q_OS_WIN
    const std::wstring path = QDir::toNativeSeparators(file).toStdWString();
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (size == 0)
        return {};
    std::vector<char> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data()))
        return {};
    struct Lang { WORD language, codePage; };
    Lang* lang = nullptr;
    UINT len = 0;
    QStringList blocks;
    if (VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<LPVOID*>(&lang), &len) && lang && len >= sizeof(Lang))
        blocks << QStringLiteral("%1%2").arg(lang->language, 4, 16, QLatin1Char('0')).arg(lang->codePage, 4, 16, QLatin1Char('0'));
    blocks << QStringLiteral("040904b0") << QStringLiteral("040904e4");
    for (const QString& b : std::as_const(blocks)) {
        const std::wstring sub = QStringLiteral("\\StringFileInfo\\%1\\%2").arg(b, key).toStdWString();
        wchar_t* value = nullptr;
        UINT vlen = 0;
        if (VerQueryValueW(data.data(), sub.c_str(), reinterpret_cast<LPVOID*>(&value), &vlen) && value && vlen > 0)
            return QString::fromWCharArray(value).trimmed();
    }
#else
    Q_UNUSED(file) Q_UNUSED(key)
#endif
    return {};
}

int runAndWait(const QString& file, const QString& arguments, const QString& workingDir, QString* error)
{
#ifdef Q_OS_WIN
    const std::wstring f = QDir::toNativeSeparators(file).toStdWString();
    const std::wstring a = arguments.toStdWString();
    const std::wstring d = QDir::toNativeSeparators(workingDir).toStdWString();
    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    info.lpVerb = L"open";
    info.lpFile = f.c_str();
    info.lpParameters = a.empty() ? nullptr : a.c_str();
    info.lpDirectory = d.empty() ? nullptr : d.c_str();
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info) || !info.hProcess) {
        if (error)
            *error = QStringLiteral("The program could not be started (error %1).").arg(GetLastError());
        return -1;
    }
    WaitForSingleObject(info.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(info.hProcess, &code);
    CloseHandle(info.hProcess);
    return int(code);
#else
    Q_UNUSED(file) Q_UNUSED(arguments) Q_UNUSED(workingDir)
    if (error)
        *error = QStringLiteral("Windows only.");
    return -1;
#endif
}

int compareVersions(const QString& a, const QString& b)
{
    auto parts = [](const QString& v) {
        QList<int> n;
        for (const QString& p : v.split(QLatin1Char('.')))
            n << p.trimmed().toInt();
        while (n.size() < 4)
            n << 0;
        return n;
    };
    const QList<int> x = parts(a), y = parts(b);
    for (int i = 0; i < std::max(x.size(), y.size()); ++i) {
        const int xi = i < x.size() ? x[i] : 0, yi = i < y.size() ? y[i] : 0;
        if (xi != yi)
            return xi < yi ? -1 : 1;
    }
    return 0;
}

} // namespace dm::win
