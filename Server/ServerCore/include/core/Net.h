#pragma once

#include <string>
#include <string_view>

#include "core/Types.h"

namespace nhn {

/// An IPv4 endpoint. IPv4 only, deliberately: the servers bind on a LAN or a
/// single host and dual-stack support would only complicate the voice router's
/// endpoint bookkeeping.
class NetAddress {
public:
    NetAddress() = default;
    explicit NetAddress(SOCKADDR_IN sockAddr) : _sockAddr(sockAddr) {}
    NetAddress(std::string_view ip, uint16 port);

    /// Wildcard bind address for a listening socket.
    static NetAddress Any(uint16 port);
    static NetAddress Loopback(uint16 port);

    SOCKADDR_IN& GetSockAddr() { return _sockAddr; }
    const SOCKADDR_IN& GetSockAddr() const { return _sockAddr; }

    std::string GetIpAddress() const;
    uint16 GetPort() const;

    /// "127.0.0.1:7777", for logs.
    std::string ToString() const;

    /// Collapses address and port into one integer so endpoints can key a hash
    /// map. Used by the voice router to find the peer a datagram came from.
    uint64 Key() const;

    bool operator==(const NetAddress& other) const { return Key() == other.Key(); }

private:
    SOCKADDR_IN _sockAddr{};
};

/// Winsock startup plus the extension-function pointers that have to be
/// retrieved through WSAIoctl rather than linked directly.
class SocketUtils {
public:
    static bool Startup();
    static void Cleanup();

    static SOCKET CreateTcpSocket();
    static SOCKET CreateUdpSocket();
    static void Close(SOCKET& socket);

    static bool Bind(SOCKET socket, const NetAddress& address);
    static bool BindAnyAddress(SOCKET socket, uint16 port);
    static bool Listen(SOCKET socket, int32 backlog = SOMAXCONN);

    static bool SetLinger(SOCKET socket, uint16 onoff, uint16 linger);
    static bool SetReuseAddress(SOCKET socket, bool enable);
    static bool SetTcpNoDelay(SOCKET socket, bool enable);
    static bool SetRecvBufferSize(SOCKET socket, int32 size);
    static bool SetSendBufferSize(SOCKET socket, int32 size);

    /// Copies the listening socket's properties onto a freshly accepted one.
    /// Without this the accepted socket cannot be used with getpeername or
    /// shutdown.
    static bool SetUpdateAcceptSocket(SOCKET socket, SOCKET listenSocket);
    static bool SetUpdateConnectContext(SOCKET socket);

    /// Stops a UDP socket from being torn down by ICMP port-unreachable
    /// replies, which Windows otherwise surfaces as WSAECONNRESET on the next
    /// receive and would kill the voice socket whenever a client vanishes.
    static bool DisableUdpConnResetReport(SOCKET socket);

    static LPFN_ACCEPTEX AcceptEx;
    static LPFN_CONNECTEX ConnectEx;
    static LPFN_DISCONNECTEX DisconnectEx;

    /// Human-readable text for a WSAGetLastError value.
    static std::string ErrorText(int32 errorCode);
};

}  // namespace nhn
