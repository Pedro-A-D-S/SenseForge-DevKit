// test_udp_dsx.cpp — DSX v2 JSON builders + real loopback delivery (SF-4).

#include "test_common.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <string>

#include "udp_dsx_client.hpp"

using senseforge::DsxTriggerMode;
using senseforge::UdpDsxClient;

namespace {

std::uint16_t BindEphemeralListener(SOCKET* out) {
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
    *out = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    bind(*out, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    sockaddr_in bound{};
    int size = sizeof(bound);
    getsockname(*out, reinterpret_cast<sockaddr*>(&bound), &size);
    return ntohs(bound.sin_port);
}

std::string ReceiveJson(SOCKET sock) {
    char buffer[2048]{};
    sockaddr_in from{};
    int from_len = sizeof(from);
    const int received =
        recvfrom(sock, buffer, sizeof(buffer) - 1, 0,
                 reinterpret_cast<sockaddr*>(&from), &from_len);
    return received <= 0 ? std::string() : std::string(buffer, received);
}

}  // namespace

int main() {
    // ---- Packet builders: exact wire format (official C# example enums) --
    assert(UdpDsxClient::BuildTriggerJson(0, /*right=*/false,
                                          DsxTriggerMode::Resistance, {0, 8}) ==
           R"({"instructions":[{"parameters":[0,1,13,0,8],"type":1}]})");
    assert(UdpDsxClient::BuildTriggerJson(0, /*right=*/true, DsxTriggerMode::Bow,
                                          {1, 8, 3, 8}) ==
           R"({"instructions":[{"parameters":[0,2,14,1,8,3,8],"type":1}]})");
    assert(UdpDsxClient::BuildTriggerJson(0, true, DsxTriggerMode::Normal, {}) ==
           R"({"instructions":[{"parameters":[0,2,0],"type":1}]})");
    assert(UdpDsxClient::BuildRgbJson(0, 20, 210, 80) ==
           R"({"instructions":[{"parameters":[0,20,210,80],"type":2}]})");
    assert(UdpDsxClient::BuildPlayerLedJson(0, 0b11111) ==
           R"({"instructions":[{"parameters":[0,1,1,1,1,1],"type":3}]})");

    // ---- Real loopback delivery ------------------------------------------
    SOCKET listener = INVALID_SOCKET;
    const std::uint16_t port = BindEphemeralListener(&listener);
    assert(port != 0);

    DWORD timeout_ms = 2000;
    setsockopt(listener, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));

    UdpDsxClient client(port);
    assert(client.Send(UdpDsxClient::BuildTriggerJson(
        0, true, DsxTriggerMode::Resistance, {0, 8})));
    assert(ReceiveJson(listener) ==
           R"({"instructions":[{"parameters":[0,2,13,0,8],"type":1}]})");

    assert(client.Send(UdpDsxClient::BuildRgbJson(0, 20, 210, 80)));
    assert(ReceiveJson(listener) ==
           R"({"instructions":[{"parameters":[0,20,210,80],"type":2}]})");

    // Unreachable port: best-effort sendto still succeeds for UDP, but a
    // bogus port object must never crash — just return a status.
    UdpDsxClient dead_client(1);  // port 1: nothing listens
    dead_client.Send("{}");       // must not crash / must not throw

    closesocket(listener);
    WSACleanup();
    return 0;
}
