#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>

// Harmless stand-ins for what a filter list blocks: a request for an ad library is answered with a small file that defines what the page expects
// (so the page does not break and a check "did the ad script load?" passes) instead of failing. Written for this program; no code of other blockers.
namespace dv::br::ab {

struct StandIn {
    QByteArray body;
    QByteArray mime;
};

bool standIn(const QString& name, StandIn* out);                       // $redirect=<name> ("noopjs", "noop.js", "1x1.gif", "googletagmanager_gtm.js" ...)
QStringList standInNames();
// A blocked address that this program knows an ad / analytics library for: the name of the stand-in ("" when none). Only scripts.
QString standInNameForUrl(const QString& url, quint16 type);

} // namespace dv::br::ab
