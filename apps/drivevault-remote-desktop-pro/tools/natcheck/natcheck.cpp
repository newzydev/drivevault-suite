// Phase 0 measurement tool: what does the NAT of this network do to a UDP port?
// It sends ONE small STUN Binding Request from ONE local UDP port to each public STUN server below and prints what each server saw.
// Nothing of the user is sent (a STUN request has no payload); the servers only learn the public address they receive the packet from.
// Usage: rd_natcheck [host:port ...]   (default servers: Google x2, Cloudflare)
#include "Stun.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostInfo>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QTextStream>
#include <QUdpSocket>

using namespace rd::stun;

namespace {
QByteArray randomId()
{
    QByteArray id(12, '\0');
    for (char& c : id) c = char(QRandomGenerator::system()->bounded(256));
    return id;
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    QStringList servers;
    for (int i = 1; i < argc; ++i) servers << QString::fromLocal8Bit(argv[i]);
    if (servers.isEmpty())
        servers = {QStringLiteral("stun.l.google.com:19302"), QStringLiteral("stun1.l.google.com:19302"), QStringLiteral("stun.cloudflare.com:3478")};

    QUdpSocket socket;
    if (!socket.bind(QHostAddress::AnyIPv4, 0)) {
        out << "cannot open a UDP port: " << socket.errorString() << "\n";
        return 2;
    }
    out << "local UDP port " << socket.localPort() << "\n";

    // local interfaces: a global IPv6 address means a direct connection without NAT may be possible; a private IPv4 address is normal behind a router
    bool globalV6 = false;
    for (const QHostAddress& a : QNetworkInterface::allAddresses())
        if (a.protocol() == QAbstractSocket::IPv6Protocol && !a.isLoopback() && !a.isLinkLocal() && !a.isSiteLocal() && !a.isUniqueLocalUnicast())
            globalV6 = true;
    out << "global IPv6 address on this PC: " << (globalV6 ? "yes" : "no") << "\n";

    QList<Mapped> answers;
    for (const QString& server : servers) {
        const int colon = server.lastIndexOf(QLatin1Char(':'));
        const QString host = colon > 0 ? server.left(colon) : server;
        const quint16 port = colon > 0 ? quint16(server.mid(colon + 1).toUInt()) : quint16(3478);
        const QHostInfo info = QHostInfo::fromName(host);
        QHostAddress target;
        for (const QHostAddress& a : info.addresses())
            if (a.protocol() == QAbstractSocket::IPv4Protocol) { target = a; break; }
        if (target.isNull()) { out << server << ": cannot find the address (" << info.errorString() << ")\n"; answers << Mapped{}; continue; }

        Mapped best;
        for (int attempt = 0; attempt < 3 && !best.ok; ++attempt) {              // UDP: ask again when nothing came back
            const QByteArray id = randomId();
            socket.writeDatagram(makeBindingRequest(id), target, port);
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < 1500 && !best.ok) {
                if (!socket.waitForReadyRead(200)) continue;
                while (socket.hasPendingDatagrams()) {
                    QByteArray packet(int(socket.pendingDatagramSize()), '\0');
                    socket.readDatagram(packet.data(), packet.size());
                    const Mapped m = parseBindingResponse(packet, id);
                    if (m.ok) best = m;
                }
            }
        }
        if (best.ok)
            out << server << ": public " << best.address.toString() << ":" << best.port << "\n";
        else
            out << server << ": no answer in 3 tries (UDP may be blocked on this network)\n";
        answers << best;
    }

    out << "result: " << describe(classify(answers)) << "\n";
    return 0;
}
