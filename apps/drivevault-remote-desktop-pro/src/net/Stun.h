#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QString>

namespace rd::stun {

// A STUN Binding Request (RFC 5389): 20 byte header, no attributes. `transactionId` must be 12 bytes (the caller keeps it to match the answer).
QByteArray makeBindingRequest(const QByteArray& transactionId);

struct Mapped {
    bool ok = false;
    QHostAddress address;       // the public address the server saw
    quint16 port = 0;           // and the public port
    QString error;              // why it is not ok (empty when ok)
};

// Reads a Binding Success Response for that transaction: XOR-MAPPED-ADDRESS (IPv4 / IPv6) or, from older servers, MAPPED-ADDRESS.
Mapped parseBindingResponse(const QByteArray& packet, const QByteArray& transactionId);

// What a set of answers from different servers (all sent from ONE local socket) says about the mapping of the NAT.
enum class Mapping { Unknown, EndpointIndependent, AddressDependent };
Mapping classify(const QList<Mapped>& answers);
QString describe(Mapping mapping);

} // namespace rd::stun
