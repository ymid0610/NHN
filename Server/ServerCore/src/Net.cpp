#include "core/Net.h"

#include <format>

#include "core/Log.h"

namespace nhn {

// ---------------------------------------------------------------------------
// NetAddress
// ---------------------------------------------------------------------------

NetAddress::NetAddress(std::string_view ip, uint16 port) {
    _sockAddr.sin_family = AF_INET;
    _sockAddr.sin_port = ::htons(port);

    const std::string ipText(ip);
    if (::inet_pton(AF_INET, ipText.c_str(), &_sockAddr.sin_addr) != 1) {
        // Fall back to a hostname lookup so "localhost" and machine names work
        // in config files.
        ADDRINFOA hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        ADDRINFOA* results = nullptr;
        if (::getaddrinfo(ipText.c_str(), nullptr, &hints, &results) == 0 && results != nullptr) {
            _sockAddr.sin_addr = reinterpret_cast<SOCKADDR_IN*>(results->ai_addr)->sin_addr;
            ::freeaddrinfo(results);
        } else {
            LOG_ERROR("cannot resolve address '{}'", ipText);
            _sockAddr.sin_addr.s_addr = INADDR_NONE;
        }
    }
}

NetAddress NetAddress::Any(uint16 port) {
    SOCKADDR_IN addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::htonl(INADDR_ANY);
    addr.sin_port = ::htons(port);
    return NetAddress(addr);
}

NetAddress NetAddress::Loopback(uint16 port) {
    SOCKADDR_IN addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
    addr.sin_port = ::htons(port);
    return NetAddress(addr);
}

std::string NetAddress::GetIpAddress() const {
    char buffer[INET_ADDRSTRLEN] = {};
    ::inet_ntop(AF_INET, &_sockAddr.sin_addr, buffer, sizeof(buffer));
    return buffer;
}

uint16 NetAddress::GetPort() const {
    return ::ntohs(_sockAddr.sin_port);
}

std::string NetAddress::ToString() const {
    return std::format("{}:{}", GetIpAddress(), GetPort());
}

uint64 NetAddress::Key() const {
    return (static_cast<uint64>(_sockAddr.sin_addr.s_addr) << 16) |
           static_cast<uint64>(_sockAddr.sin_port);
}

// ---------------------------------------------------------------------------
// SocketUtils
// ---------------------------------------------------------------------------

LPFN_ACCEPTEX SocketUtils::AcceptEx = nullptr;
LPFN_CONNECTEX SocketUtils::ConnectEx = nullptr;
LPFN_DISCONNECTEX SocketUtils::DisconnectEx = nullptr;

namespace {

/// The AcceptEx/ConnectEx entry points live in the provider, not in a lib, so
/// they have to be fetched per-provider through WSAIoctl.
bool LoadExtension(SOCKET socket, GUID guid, void* outFunction) {
    DWORD bytes = 0;
    return ::WSAIoctl(socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), outFunction,
                      sizeof(void*), &bytes, nullptr, nullptr) != SOCKET_ERROR;
}

template <class T>
bool SetOption(SOCKET socket, int32 level, int32 option, T value) {
    return ::setsockopt(socket, level, option, reinterpret_cast<char*>(&value), sizeof(T)) !=
           SOCKET_ERROR;
}

}  // namespace

bool SocketUtils::Startup() {
    WSADATA wsaData{};
    if (::WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LOG_ERROR("WSAStartup failed");
        return false;
    }

    const SOCKET probe = CreateTcpSocket();
    if (probe == INVALID_SOCKET) {
        LOG_ERROR("cannot create probe socket: {}", ErrorText(::WSAGetLastError()));
        return false;
    }

    GUID acceptExGuid = WSAID_ACCEPTEX;
    GUID connectExGuid = WSAID_CONNECTEX;
    GUID disconnectExGuid = WSAID_DISCONNECTEX;

    const bool ok = LoadExtension(probe, acceptExGuid, &AcceptEx) &&
                    LoadExtension(probe, connectExGuid, &ConnectEx) &&
                    LoadExtension(probe, disconnectExGuid, &DisconnectEx);

    SOCKET closing = probe;
    Close(closing);

    if (!ok) {
        LOG_ERROR("cannot load winsock extension functions: {}", ErrorText(::WSAGetLastError()));
        return false;
    }
    return true;
}

void SocketUtils::Cleanup() {
    ::WSACleanup();
}

SOCKET SocketUtils::CreateTcpSocket() {
    return ::WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
}

SOCKET SocketUtils::CreateUdpSocket() {
    return ::WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, WSA_FLAG_OVERLAPPED);
}

void SocketUtils::Close(SOCKET& socket) {
    if (socket != INVALID_SOCKET) {
        ::closesocket(socket);
        socket = INVALID_SOCKET;
    }
}

bool SocketUtils::Bind(SOCKET socket, const NetAddress& address) {
    return ::bind(socket, reinterpret_cast<const SOCKADDR*>(&address.GetSockAddr()),
                  sizeof(SOCKADDR_IN)) != SOCKET_ERROR;
}

bool SocketUtils::BindAnyAddress(SOCKET socket, uint16 port) {
    return Bind(socket, NetAddress::Any(port));
}

bool SocketUtils::Listen(SOCKET socket, int32 backlog) {
    return ::listen(socket, backlog) != SOCKET_ERROR;
}

bool SocketUtils::SetLinger(SOCKET socket, uint16 onoff, uint16 linger) {
    LINGER option{onoff, linger};
    return SetOption(socket, SOL_SOCKET, SO_LINGER, option);
}

bool SocketUtils::SetReuseAddress(SOCKET socket, bool enable) {
    return SetOption(socket, SOL_SOCKET, SO_REUSEADDR, enable ? TRUE : FALSE);
}

bool SocketUtils::SetTcpNoDelay(SOCKET socket, bool enable) {
    return SetOption(socket, IPPROTO_TCP, TCP_NODELAY, enable ? TRUE : FALSE);
}

bool SocketUtils::SetRecvBufferSize(SOCKET socket, int32 size) {
    return SetOption(socket, SOL_SOCKET, SO_RCVBUF, size);
}

bool SocketUtils::SetSendBufferSize(SOCKET socket, int32 size) {
    return SetOption(socket, SOL_SOCKET, SO_SNDBUF, size);
}

bool SocketUtils::SetUpdateAcceptSocket(SOCKET socket, SOCKET listenSocket) {
    return SetOption(socket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, listenSocket);
}

bool SocketUtils::SetUpdateConnectContext(SOCKET socket) {
    return ::setsockopt(socket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, nullptr, 0) != SOCKET_ERROR;
}

bool SocketUtils::DisableUdpConnResetReport(SOCKET socket) {
    BOOL enable = FALSE;
    DWORD bytes = 0;
    return ::WSAIoctl(socket, SIO_UDP_CONNRESET, &enable, sizeof(enable), nullptr, 0, &bytes,
                      nullptr, nullptr) != SOCKET_ERROR;
}

std::string SocketUtils::ErrorText(int32 errorCode) {
    char* buffer = nullptr;
    const DWORD length = ::FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(errorCode), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buffer), 0, nullptr);

    std::string message;
    if (length != 0 && buffer != nullptr) {
        message.assign(buffer, length);
        while (!message.empty() && (message.back() == '\r' || message.back() == '\n')) {
            message.pop_back();
        }
    }
    if (buffer != nullptr) {
        ::LocalFree(buffer);
    }

    return std::format("{} ({})", message.empty() ? "unknown error" : message, errorCode);
}

}  // namespace nhn
