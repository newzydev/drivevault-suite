#include "Stun.h"

#include <QtEndian>

namespace rd::stun {
namespace {
constexpr quint16 kBindingRequest = 0x0001;
constexpr quint16 kBindingSuccess = 0x0101;
constexpr quint32 kMagicCookie = 0x2112A442;
constexpr quint16 kAttrMapped = 0x0001;
constexpr quint16 kAttrXorMapped = 0x0020;

quint16 rd16(const char* p) { return qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(p)); }
quint32 rd32(const char* p) { return qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(p)); }
} // namespace

QByteArray makeBindingRequest(const QByteArray& transactionId)
{
    QByteArray out(20, '\0');
    qToBigEndian<quint16>(kBindingRequest, reinterpret_cast<uchar*>(out.data()));
    qToBigEndian<quint16>(0, reinterpret_cast<uchar*>(out.data() + 2));
    qToBigEndian<quint32>(kMagicCookie, reinterpret_cast<uchar*>(out.data() + 4));
    out.replace(8, 12, transactionId.left(12).leftJustified(12, '\0'));
    return out;
}

Mapped parseBindingResponse(const QByteArray& packet, const QByteArray& transactionId)
{
    Mapped m;
    if (packet.size() < 20) { m.error = QStringLiteral("answer too short"); return m; }
    if (rd16(packet.constData()) != kBindingSuccess) { m.error = QStringLiteral("not a Binding success response"); return m; }
    const int length = rd16(packet.constData() + 2);
    if (rd32(packet.constData() + 4) != kMagicCookie) { m.error = QStringLiteral("wrong magic cookie"); return m; }
    if (packet.mid(8, 12) != transactionId.left(12).leftJustified(12, '\0')) { m.error = QStringLiteral("answer is for another request"); return m; }
    if (20 + length > packet.size()) { m.error = QStringLiteral("answer is cut"); return m; }

    int pos = 20;
    const int end = 20 + length;
    bool haveMapped = false;
    while (pos + 4 <= end) {
        const quint16 type = rd16(packet.constData() + pos);
        const int len = rd16(packet.constData() + pos + 2);
        const int value = pos + 4;
        if (value + len > end) break;
        const bool xorForm = type == kAttrXorMapped;
        if ((xorForm || type == kAttrMapped) && len >= 8) {
            const uchar family = uchar(packet.at(value + 1));
            quint16 port = rd16(packet.constData() + value + 2);
            if (xorForm) port ^= quint16(kMagicCookie >> 16);
            if (family == 0x01) {                                           // IPv4
                quint32 addr = rd32(packet.constData() + value + 4);
                if (xorForm) addr ^= kMagicCookie;
                m.address = QHostAddress(addr);
                m.port = port;
                m.ok = true;
                haveMapped = xorForm;                                       // an XOR form wins over a plain one
            } else if (family == 0x02 && len >= 20) {                       // IPv6
                Q_IPV6ADDR a6;
                QByteArray key = packet.mid(4, 4) + packet.mid(8, 12);      // magic cookie + transaction id
                for (int i = 0; i < 16; ++i) {
                    const uchar b = uchar(packet.at(value + 4 + i));
                    a6.c[i] = xorForm ? uchar(b ^ uchar(key.at(i))) : b;
                }
                m.address = QHostAddress(a6);
                m.port = port;
                m.ok = true;
                haveMapped = xorForm;
            }
            if (haveMapped) break;
        }
        pos = value + ((len + 3) & ~3);                                     // attributes are padded to 4 bytes
    }
    if (!m.ok) m.error = QStringLiteral("no mapped address in the answer");
    return m;
}

Mapping classify(const QList<Mapped>& answers)
{
    QList<Mapped> good;
    for (const Mapped& a : answers)
        if (a.ok) good << a;
    if (good.size() < 2) return Mapping::Unknown;
    for (const Mapped& a : good)
        if (a.address != good.first().address || a.port != good.first().port)
            return Mapping::AddressDependent;
    return Mapping::EndpointIndependent;
}

QString describe(Mapping mapping)
{
    switch (mapping) {
    case Mapping::EndpointIndependent:
        return QStringLiteral("endpoint-independent mapping: the same public address and port for every server - direct connections by hole punching can work");
    case Mapping::AddressDependent:
        return QStringLiteral("address-dependent (symmetric-like) mapping: a different public port for each server - hole punching will usually fail, a relay is needed");
    default:
        return QStringLiteral("unknown: fewer than two servers answered");
    }
}

} // namespace rd::stun
