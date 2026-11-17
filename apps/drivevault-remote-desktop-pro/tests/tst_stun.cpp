#include <QtEndian>
#include <QtTest>

#include "Stun.h"

using namespace rd::stun;

class TstStun : public QObject
{
    Q_OBJECT
    static QByteArray response(const QByteArray& id, const QByteArray& attributes)
    {
        QByteArray p(20, '\0');
        qToBigEndian<quint16>(0x0101, reinterpret_cast<uchar*>(p.data()));
        qToBigEndian<quint16>(quint16(attributes.size()), reinterpret_cast<uchar*>(p.data() + 2));
        qToBigEndian<quint32>(0x2112A442, reinterpret_cast<uchar*>(p.data() + 4));
        p.replace(8, 12, id);
        return p + attributes;
    }
    static QByteArray xorMappedV4(quint32 ip, quint16 port)
    {
        QByteArray a(12, '\0');
        qToBigEndian<quint16>(0x0020, reinterpret_cast<uchar*>(a.data()));
        qToBigEndian<quint16>(8, reinterpret_cast<uchar*>(a.data() + 2));
        a[5] = char(0x01);
        qToBigEndian<quint16>(quint16(port ^ 0x2112), reinterpret_cast<uchar*>(a.data() + 6));
        qToBigEndian<quint32>(ip ^ 0x2112A442u, reinterpret_cast<uchar*>(a.data() + 8));
        return a;
    }

private slots:
    void requestIsAValidBindingRequest()
    {
        const QByteArray id("abcdefghijkl");
        const QByteArray r = makeBindingRequest(id);
        QCOMPARE(r.size(), 20);
        QCOMPARE(quint8(r.at(0)), quint8(0x00));
        QCOMPARE(quint8(r.at(1)), quint8(0x01));                      // Binding Request
        QCOMPARE(r.mid(4, 4), QByteArray::fromHex("2112a442"));         // magic cookie
        QCOMPARE(r.mid(8, 12), id);
    }

    void readsAnXorMappedIPv4Address()
    {
        const QByteArray id("abcdefghijkl");
        const Mapped m = parseBindingResponse(response(id, xorMappedV4(QHostAddress("203.0.113.9").toIPv4Address(), 54321)), id);
        QVERIFY2(m.ok, qPrintable(m.error));
        QCOMPARE(m.address, QHostAddress("203.0.113.9"));
        QCOMPARE(m.port, quint16(54321));
    }

    void refusesAnAnswerForAnotherRequest()
    {
        const Mapped m = parseBindingResponse(response(QByteArray("zzzzzzzzzzzz"), xorMappedV4(1, 1)), QByteArray("abcdefghijkl"));
        QVERIFY(!m.ok);
    }

    void refusesGarbageAndShortPackets()
    {
        QVERIFY(!parseBindingResponse(QByteArray("short"), QByteArray("abcdefghijkl")).ok);
        QVERIFY(!parseBindingResponse(QByteArray(40, 'x'), QByteArray("abcdefghijkl")).ok);
        const QByteArray id("abcdefghijkl");
        QVERIFY(!parseBindingResponse(response(id, QByteArray()), id).ok);          // no mapped address
        QByteArray cut = response(id, xorMappedV4(1, 1));
        cut.chop(4);
        QVERIFY(!parseBindingResponse(cut, id).ok);                                // the length says more than there is
    }

    void classifiesTheMapping()
    {
        Mapped a; a.ok = true; a.address = QHostAddress("203.0.113.9"); a.port = 40000;
        Mapped same = a;
        Mapped other = a; other.port = 40001;
        Mapped none;
        QCOMPARE(classify({a, same}), Mapping::EndpointIndependent);
        QCOMPARE(classify({a, other}), Mapping::AddressDependent);
        QCOMPARE(classify({a, none}), Mapping::Unknown);                           // one answer proves nothing
        QCOMPARE(classify({}), Mapping::Unknown);
    }
};

QTEST_GUILESS_MAIN(TstStun)
#include "tst_stun.moc"
