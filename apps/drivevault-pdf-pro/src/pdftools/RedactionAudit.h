#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <functional>

namespace dp::tools {

// Verify that the new PDF contains only the expected page images and drawing commands.
bool auditRedaction(const QString& path, const QList<QByteArray>& imageHashes,
                    const std::function<bool(int)>& beforePage = {});

} // namespace dp::tools
