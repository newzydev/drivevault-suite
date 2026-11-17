// Phase 0 measurement tool: does a direct UDP connection between two machines work when both are behind their own router (NAT)?
// No server of ours is involved. Each side
//   1. opens ONE UDP port and asks public STUN servers (Google, Cloudflare) what public address that port has,
//   2. shows that address; the user types the other side's address (by hand: chat, phone ...),
//   3. both sides send small probe packets to each other for up to 30 seconds (UDP hole punching) and measure the round trip.
// It sends nothing but those probes (a magic word, a counter and a random number); nothing of the user, no file, no screen.
// A single exe with the C++ runtime linked in, so it can be copied to a second PC without installing anything.
//   rd_punch              interactive test (run on both PCs at about the same time)
//   rd_punch --selftest   checks the tool itself on this PC (127.0.0.1 only, no Internet)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

namespace {

using Clock = std::chrono::steady_clock;

struct Endpoint {
    uint32_t ip = 0;        // network byte order
    uint16_t port = 0;      // host byte order
    bool valid() const { return ip != 0 && port != 0; }
    bool operator==(const Endpoint& o) const { return ip == o.ip && port == o.port; }
};

std::string toString(const Endpoint& e)
{
    in_addr a{};
    a.s_addr = e.ip;
    char buf[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &a, buf, sizeof buf);
    return std::string(buf) + ":" + std::to_string(e.port);
}

std::optional<Endpoint> parseEndpoint(std::string text)
{
    text.erase(std::remove_if(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c); }), text.end());
    const size_t colon = text.rfind(':');
    if (colon == std::string::npos) return std::nullopt;
    in_addr a{};
    if (inet_pton(AF_INET, text.substr(0, colon).c_str(), &a) != 1) return std::nullopt;
    const int port = std::atoi(text.substr(colon + 1).c_str());
    if (port < 1 || port > 65535) return std::nullopt;
    return Endpoint{a.s_addr, uint16_t(port)};
}

SOCKET openSocket(const char* bindAddress)
{
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return s;
    BOOL off = FALSE;
    DWORD bytes = 0;
    WSAIoctl(s, SIO_UDP_CONNRESET, &off, sizeof off, nullptr, 0, &bytes, nullptr, nullptr);   // an ICMP "port unreachable" must not break recvfrom
    sockaddr_in me{};
    me.sin_family = AF_INET;
    inet_pton(AF_INET, bindAddress, &me.sin_addr);
    me.sin_port = 0;
    if (bind(s, reinterpret_cast<sockaddr*>(&me), sizeof me) != 0) { closesocket(s); return INVALID_SOCKET; }
    return s;
}

uint16_t localPort(SOCKET s)
{
    sockaddr_in me{};
    int len = sizeof me;
    getsockname(s, reinterpret_cast<sockaddr*>(&me), &len);
    return ntohs(me.sin_port);
}

bool waitReadable(SOCKET s, int ms)
{
    fd_set set;
    FD_ZERO(&set);
    FD_SET(s, &set);
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    return select(0, &set, nullptr, nullptr, &tv) > 0;
}

void sendTo(SOCKET s, const Endpoint& to, const void* data, int size)
{
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = to.ip;
    a.sin_port = htons(to.port);
    sendto(s, static_cast<const char*>(data), size, 0, reinterpret_cast<sockaddr*>(&a), sizeof a);
}

// ---- STUN (RFC 5389 Binding request; the same format as src/net/Stun.cpp, which has the tests) -------------------------------
std::optional<Endpoint> stunQuery(SOCKET s, const std::string& host, const std::string& port)
{
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || !res) return std::nullopt;
    const sockaddr_in server = *reinterpret_cast<sockaddr_in*>(res->ai_addr);
    freeaddrinfo(res);

    std::random_device rd;
    for (int attempt = 0; attempt < 3; ++attempt) {
        unsigned char req[20] = {0x00, 0x01, 0x00, 0x00, 0x21, 0x12, 0xA4, 0x42};
        for (int i = 8; i < 20; ++i) req[i] = static_cast<unsigned char>(rd());
        sendto(s, reinterpret_cast<const char*>(req), sizeof req, 0, reinterpret_cast<const sockaddr*>(&server), sizeof server);
        const auto until = Clock::now() + std::chrono::milliseconds(1500);
        while (Clock::now() < until) {
            if (!waitReadable(s, 200)) continue;
            unsigned char buf[600];
            sockaddr_in from{};
            int flen = sizeof from;
            const int n = recvfrom(s, reinterpret_cast<char*>(buf), sizeof buf, 0, reinterpret_cast<sockaddr*>(&from), &flen);
            if (n < 20 || buf[0] != 0x01 || buf[1] != 0x01 || std::memcmp(buf + 4, req + 4, 16) != 0) continue;   // cookie + transaction id must match
            const int total = std::min(n, 20 + ((buf[2] << 8) | buf[3]));
            for (int pos = 20; pos + 4 <= total;) {
                const int type = (buf[pos] << 8) | buf[pos + 1];
                const int len = (buf[pos + 2] << 8) | buf[pos + 3];
                const int v = pos + 4;
                if (v + len > total) break;
                if ((type == 0x0020 || type == 0x0001) && len >= 8 && buf[v + 1] == 0x01) {
                    const bool x = type == 0x0020;
                    uint16_t p = uint16_t((buf[v + 2] << 8) | buf[v + 3]);
                    uint32_t a = (uint32_t(buf[v + 4]) << 24) | (uint32_t(buf[v + 5]) << 16) | (uint32_t(buf[v + 6]) << 8) | buf[v + 7];
                    if (x) { p ^= 0x2112; a ^= 0x2112A442u; }
                    return Endpoint{htonl(a), p};
                }
                pos = v + ((len + 3) & ~3);
            }
        }
    }
    return std::nullopt;
}

// ---- the punch --------------------------------------------------------------------------------------------------------------------
// Packet: "RDP1" | type (1 = probe, 2 = answer) | sequence (2 bytes) | sender id (8 bytes)  = 15 bytes.
constexpr int kPacket = 15;

struct PunchResult {
    bool success = false;
    int sent = 0, answers = 0, probesSeen = 0;
    double rttMin = 0, rttAvg = 0, rttMax = 0;
    bool peerPortChanged = false;
    Endpoint seenFrom;          // where the peer's packets really came from (a changed port = a NAT that changes the port per destination)
};

PunchResult punch(SOCKET s, const Endpoint& peer, int seconds, bool quiet)
{
    PunchResult r;
    std::mt19937_64 rng{std::random_device{}()};
    const uint64_t myId = rng();
    std::map<int, Clock::time_point> sentAt;
    std::vector<double> rtts;
    int seq = 0;
    auto nextProbe = Clock::now();
    const auto deadline = Clock::now() + std::chrono::seconds(seconds);
    Clock::time_point lingerUntil{};
    bool finished = false;

    auto build = [&](unsigned char type, int sq, uint64_t id) {
        unsigned char p[kPacket] = {'R', 'D', 'P', '1', type, uint8_t(sq >> 8), uint8_t(sq)};
        std::memcpy(p + 7, &id, 8);
        return std::string(reinterpret_cast<char*>(p), kPacket);
    };

    while (true) {
        const auto now = Clock::now();
        if (finished ? now >= lingerUntil : now >= deadline) break;
        if (!finished && now >= nextProbe) {
            const std::string p = build(1, ++seq, myId);
            sentAt[seq] = now;
            sendTo(s, peer, p.data(), int(p.size()));
            ++r.sent;
            nextProbe = now + std::chrono::milliseconds(200);
            if (!quiet && r.sent % 5 == 0) { std::printf("."); std::fflush(stdout); }
        }
        if (!waitReadable(s, 40)) continue;
        unsigned char buf[64];
        sockaddr_in from{};
        int flen = sizeof from;
        const int n = recvfrom(s, reinterpret_cast<char*>(buf), sizeof buf, 0, reinterpret_cast<sockaddr*>(&from), &flen);
        if (n != kPacket || std::memcmp(buf, "RDP1", 4) != 0) continue;
        const Endpoint src{from.sin_addr.s_addr, ntohs(from.sin_port)};
        if (src.ip != peer.ip) continue;                          // only the other PC counts
        uint64_t id = 0;
        std::memcpy(&id, buf + 7, 8);
        const int sq = (buf[5] << 8) | buf[6];
        if (buf[4] == 1 && id != myId) {                          // a probe from the peer: answer it where it came from
            ++r.probesSeen;
            r.seenFrom = src;
            if (src.port != peer.port) r.peerPortChanged = true;
            const std::string a = build(2, sq, id);
            sendTo(s, src, a.data(), int(a.size()));
        } else if (buf[4] == 2 && id == myId && sentAt.count(sq)) {   // the answer to one of our probes: the way there and back works
            ++r.answers;
            r.seenFrom = src;
            if (src.port != peer.port) r.peerPortChanged = true;
            rtts.push_back(std::chrono::duration<double, std::milli>(Clock::now() - sentAt[sq]).count());
            sentAt.erase(sq);
            if (!finished && r.answers >= 10) { finished = true; lingerUntil = Clock::now() + std::chrono::seconds(4); }   // keep answering a little: the peer may still need our answers
        }
    }
    r.success = r.answers > 0;
    if (!rtts.empty()) {
        r.rttMin = *std::min_element(rtts.begin(), rtts.end());
        r.rttMax = *std::max_element(rtts.begin(), rtts.end());
        double sum = 0;
        for (double v : rtts) sum += v;
        r.rttAvg = sum / double(rtts.size());
    }
    return r;
}

// ---- self test: the tool on this PC only ------------------------------------------------------------------------------------------
uint32_t loopback()
{
    in_addr a{};
    inet_pton(AF_INET, "127.0.0.1", &a);
    return a.s_addr;
}

int selfTest()
{
    int failures = 0;
    auto check = [&](bool ok, const char* what) { std::printf("  %s: %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++failures; };

    SOCKET a = openSocket("127.0.0.1"), b = openSocket("127.0.0.1");
    const Endpoint ea{loopback(), localPort(a)}, eb{loopback(), localPort(b)};
    PunchResult ra, rb;
    std::thread ta([&] { ra = punch(a, eb, 8, true); });
    std::thread tb([&] { rb = punch(b, ea, 8, true); });
    ta.join();
    tb.join();
    check(ra.success && rb.success, "two sockets reach each other");
    check(ra.answers >= 10 && rb.answers >= 10, "ten answers each");
    check(!ra.peerPortChanged && !rb.peerPortChanged, "no port change on a direct path");
    check(ra.rttMax < 200 && rb.rttMax < 200, "round trip on this PC is fast");
    closesocket(a);
    closesocket(b);

    SOCKET c = openSocket("127.0.0.1");
    const SOCKET quiet = openSocket("127.0.0.1");
    const Endpoint nobody{loopback(), localPort(quiet)};          // a port where nobody answers
    const PunchResult rc = punch(c, nobody, 2, true);
    check(!rc.success && rc.sent > 0, "nobody answering = a clear failure, not a hang");
    closesocket(c);
    closesocket(quiet);

    const auto good = parseEndpoint(" 203.0.113.9:54321 ");
    check(good && toString(*good) == "203.0.113.9:54321", "endpoint text is read and written back");
    check(!parseEndpoint("203.0.113.9") && !parseEndpoint("999.1.1.1:5") && !parseEndpoint("1.2.3.4:0") && !parseEndpoint("hello:80"), "bad endpoint text is refused");

    std::printf(failures ? "SELFTEST FAILED (%d)\n" : "SELFTEST PASSED\n", failures);
    return failures ? 1 : 0;
}

} // namespace

int main(int argc, char** argv)
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { std::printf("Windows sockets could not start.\n"); return 2; }
    if (argc > 1 && std::string(argv[1]) == "--selftest") return selfTest();

    std::printf("DriveVault Remote Desktop Pro - NAT punching test (phase 0)\n"
                "Run this on BOTH PCs at about the same time. It sends only small probe packets: to Google / Cloudflare STUN servers\n"
                "(to learn this port's public address) and to the other PC. Nothing else leaves this PC. Close the window to stop.\n\n");

    SOCKET s = openSocket("0.0.0.0");
    if (s == INVALID_SOCKET) { std::printf("Cannot open a UDP port.\n"); return 2; }

    const struct { const char* host; const char* port; } servers[] = {{"stun.l.google.com", "19302"}, {"stun1.l.google.com", "19302"}, {"stun.cloudflare.com", "3478"}};
    std::vector<Endpoint> seen;
    for (const auto& sv : servers) {
        const auto e = stunQuery(s, sv.host, sv.port);
        std::printf("  %-22s %s\n", sv.host, e ? "answered" : "no answer");
        if (e) seen.push_back(*e);
    }
    if (seen.empty()) { std::printf("\nNo STUN server answered: UDP seems blocked on this network. Direct connections will not work here.\n"); return 3; }
    const bool same = std::all_of(seen.begin(), seen.end(), [&](const Endpoint& e) { return e == seen.front(); });
    std::printf("\nNAT mapping: %s\n", seen.size() < 2 ? "unknown (only one server answered)"
                 : same ? "same public port for every server (punching can work)" : "a different public port per server (punching will usually fail)");
    std::printf("\nYOUR ADDRESS (send this line to the other side): %s\n\n", toString(seen.front()).c_str());

    std::printf("Type the OTHER side's address (a.b.c.d:port) and press Enter: ");
    std::string line;
    std::getline(std::cin, line);
    const auto peer = parseEndpoint(line);
    if (!peer) { std::printf("That is not an address like 203.0.113.9:54321.\n"); return 2; }

    std::printf("Punching (up to 30 s, one dot per second)...\n");
    const PunchResult r = punch(s, *peer, 30, false);
    std::printf("\n\nprobes sent: %d, answers received: %d, probes from the other side: %d\n", r.sent, r.answers, r.probesSeen);
    if (r.success) {
        std::printf("RESULT: DIRECT CONNECTION WORKS. round trip min %.1f / avg %.1f / max %.1f ms%s\n", r.rttMin, r.rttAvg, r.rttMax,
                    r.peerPortChanged ? "  (the other side's port was not the one it announced: its NAT changes ports)" : "");
    } else if (r.probesSeen > 0) {
        std::printf("RESULT: ONE WAY ONLY. The other side's probes arrived but ours were not answered. Our probes may be blocked on its side (firewall).\n");
    } else {
        std::printf("RESULT: FAILED. Nothing arrived in 30 s. Possible reasons: the other side was not running at the same time, a typed address was wrong,\n"
                    "or one of the networks does not allow direct UDP (CGNAT / symmetric NAT / a firewall). A relay would be needed here.\n");
    }
    std::printf("\nPress Enter to close.");
    std::getline(std::cin, line);
    closesocket(s);
    WSACleanup();
    return r.success ? 0 : 1;
}
