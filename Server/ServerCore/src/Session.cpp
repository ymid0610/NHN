#include "core/Session.h"

#include <cstring>

#include "core/CoreGlobal.h"
#include "core/IocpCore.h"
#include "core/Log.h"
#include "core/Service.h"

namespace nhn {

Session::Session() : _recvBuffer(kRecvBufferSize) {
    _socket = SocketUtils::CreateTcpSocket();
}

Session::~Session() {
    SocketUtils::Close(_socket);
}

HANDLE Session::GetHandle() {
    return reinterpret_cast<HANDLE>(_socket);
}

std::string Session::GetDisconnectCause() const {
    std::lock_guard<std::mutex> guard(_lock);
    return _disconnectCause;
}

void Session::Dispatch(IocpEvent* iocpEvent, int32 numOfBytes) {
    switch (iocpEvent->eventType) {
        case EventType::Connect:
            ProcessConnect();
            break;
        case EventType::Disconnect:
            ProcessDisconnect();
            break;
        case EventType::Recv:
            ProcessRecv(numOfBytes);
            break;
        case EventType::Send:
            ProcessSend(numOfBytes);
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------

void Session::Send(SendBufferRef sendBuffer) {
    if (sendBuffer == nullptr || !IsConnected()) {
        return;
    }

    bool shouldRegister = false;
    {
        std::lock_guard<std::mutex> guard(_lock);
        _sendQueue.push(std::move(sendBuffer));
        // Exactly one thread wins the transition to "registered" and issues the
        // WSASend; the rest merely enqueue and let that one pick their data up.
        if (!_sendRegistered.exchange(true)) {
            shouldRegister = true;
        }
    }

    if (shouldRegister) {
        RegisterSend();
    }
}

bool Session::Connect() {
    return RegisterConnect();
}

void Session::Disconnect(std::string_view cause) {
    // First caller wins; later ones (error paths racing the peer close) return.
    if (!_connected.exchange(false)) {
        return;
    }

    {
        std::lock_guard<std::mutex> guard(_lock);
        _disconnectCause.assign(cause);
    }

    LOG_DEBUG("session {} disconnecting: {}", _netAddress.ToString(), cause);
    RegisterDisconnect();
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

bool Session::RegisterConnect() {
    if (IsConnected()) {
        return false;
    }

    Ref<Service> service = GetService();
    if (service == nullptr || service->GetType() != ServiceType::Client) {
        return false;
    }

    // A session may be redialled after a failed attempt, so start from a fresh
    // socket rather than reusing one in an unknown state.
    SocketUtils::Close(_socket);
    _socket = SocketUtils::CreateTcpSocket();
    if (_socket == INVALID_SOCKET) {
        LOG_ERROR("cannot create socket: {}", SocketUtils::ErrorText(::WSAGetLastError()));
        return false;
    }

    if (!GIocpCore->Register(GetSessionRef())) {
        LOG_ERROR("cannot associate socket with completion port");
        return false;
    }

    SocketUtils::SetReuseAddress(_socket, true);
    // ConnectEx requires an already-bound socket, unlike connect().
    if (!SocketUtils::BindAnyAddress(_socket, 0)) {
        LOG_ERROR("cannot bind: {}", SocketUtils::ErrorText(::WSAGetLastError()));
        return false;
    }

    _connectEvent.Init();
    _connectEvent.owner = shared_from_this();

    DWORD numOfBytes = 0;
    SOCKADDR_IN sockAddr = service->GetNetAddress().GetSockAddr();
    if (SocketUtils::ConnectEx(_socket, reinterpret_cast<SOCKADDR*>(&sockAddr), sizeof(sockAddr),
                               nullptr, 0, &numOfBytes, &_connectEvent) == FALSE) {
        const int32 errorCode = ::WSAGetLastError();
        if (errorCode != WSA_IO_PENDING) {
            _connectEvent.owner = nullptr;
            LOG_WARN("connect to {} failed: {}", service->GetNetAddress().ToString(),
                     SocketUtils::ErrorText(errorCode));
            return false;
        }
    }
    return true;
}

bool Session::RegisterDisconnect() {
    _disconnectEvent.Init();
    _disconnectEvent.owner = shared_from_this();

    if (SocketUtils::DisconnectEx(_socket, &_disconnectEvent, 0, 0) == FALSE) {
        const int32 errorCode = ::WSAGetLastError();
        if (errorCode != WSA_IO_PENDING) {
            // The socket is already unusable; run the teardown inline rather
            // than waiting for a completion that will never arrive.
            _disconnectEvent.owner = nullptr;
            ProcessDisconnect();
            return false;
        }
    }
    return true;
}

void Session::RegisterRecv() {
    if (!IsConnected()) {
        return;
    }

    // Compaction happens after every read, so this only trips if a single
    // packet could exceed the whole buffer — a framing bug, not back-pressure.
    if (_recvBuffer.FreeSize() == 0) {
        Disconnect("receive buffer exhausted");
        return;
    }

    _recvEvent.Init();
    _recvEvent.owner = shared_from_this();

    WSABUF wsaBuf{};
    wsaBuf.buf = reinterpret_cast<char*>(_recvBuffer.WritePos());
    wsaBuf.len = _recvBuffer.FreeSize();

    DWORD numOfBytes = 0;
    DWORD flags = 0;
    if (::WSARecv(_socket, &wsaBuf, 1, &numOfBytes, &flags, &_recvEvent, nullptr) == SOCKET_ERROR) {
        const int32 errorCode = ::WSAGetLastError();
        if (errorCode != WSA_IO_PENDING) {
            _recvEvent.owner = nullptr;
            HandleError(errorCode, "WSARecv");
        }
    }
}

void Session::RegisterSend() {
    if (!IsConnected()) {
        return;
    }

    _sendEvent.Init();
    _sendEvent.owner = shared_from_this();

    {
        std::lock_guard<std::mutex> guard(_lock);
        // Everything queued so far goes out as one scatter-gather send, which
        // is what keeps a chatty broadcast from becoming one syscall per packet.
        while (!_sendQueue.empty()) {
            _sendEvent.sendBuffers.push_back(std::move(_sendQueue.front()));
            _sendQueue.pop();
        }
    }

    if (_sendEvent.sendBuffers.empty()) {
        _sendEvent.owner = nullptr;
        _sendRegistered.store(false);
        return;
    }

    std::vector<WSABUF> wsaBufs;
    wsaBufs.reserve(_sendEvent.sendBuffers.size());
    for (const SendBufferRef& buffer : _sendEvent.sendBuffers) {
        WSABUF wsaBuf{};
        wsaBuf.buf = reinterpret_cast<char*>(buffer->Buffer());
        wsaBuf.len = buffer->WriteSize();
        wsaBufs.push_back(wsaBuf);
    }

    DWORD numOfBytes = 0;
    if (::WSASend(_socket, wsaBufs.data(), static_cast<DWORD>(wsaBufs.size()), &numOfBytes, 0,
                  &_sendEvent, nullptr) == SOCKET_ERROR) {
        const int32 errorCode = ::WSAGetLastError();
        if (errorCode != WSA_IO_PENDING) {
            _sendEvent.owner = nullptr;
            _sendEvent.sendBuffers.clear();
            _sendRegistered.store(false);
            HandleError(errorCode, "WSASend");
        }
    }
}

// ---------------------------------------------------------------------------
// Completion handling
// ---------------------------------------------------------------------------

void Session::ProcessConnect() {
    _connectEvent.owner = nullptr;

    SocketUtils::SetUpdateConnectContext(_socket);
    SocketUtils::SetTcpNoDelay(_socket, true);

    _connected.store(true, std::memory_order_release);

    SOCKADDR_IN peer{};
    int32 peerSize = sizeof(peer);
    if (::getpeername(_socket, reinterpret_cast<SOCKADDR*>(&peer), &peerSize) != SOCKET_ERROR) {
        _netAddress = NetAddress(peer);
    }

    if (Ref<Service> service = GetService()) {
        service->AddSession(GetSessionRef());
    }

    OnConnected();
    RegisterRecv();
}

void Session::ProcessAccepted() {
    _connected.store(true, std::memory_order_release);

    if (Ref<Service> service = GetService()) {
        service->AddSession(GetSessionRef());
    }

    OnConnected();
    RegisterRecv();
}

void Session::ProcessDisconnect() {
    _disconnectEvent.owner = nullptr;

    OnDisconnected();

    if (Ref<Service> service = GetService()) {
        service->ReleaseSession(GetSessionRef());
    }

    // Closing here forces any receive or send still queued in the kernel to
    // complete with ERROR_OPERATION_ABORTED, releasing the last owner
    // references and letting this object finally die.
    SocketUtils::Close(_socket);
}

void Session::ProcessRecv(int32 numOfBytes) {
    _recvEvent.owner = nullptr;

    if (numOfBytes == 0) {
        Disconnect("peer closed the connection");
        return;
    }

    // A completion that arrives after teardown began carries no useful data.
    if (!IsConnected()) {
        return;
    }

    if (!_recvBuffer.OnWrite(static_cast<uint32>(numOfBytes))) {
        Disconnect("receive buffer overflow");
        return;
    }

    const int32 dataSize = static_cast<int32>(_recvBuffer.DataSize());
    const int32 processedLength = OnRecv(_recvBuffer.ReadPos(), dataSize);
    if (processedLength < 0 || processedLength > dataSize) {
        Disconnect("protocol violation while framing");
        return;
    }
    if (!_recvBuffer.OnRead(static_cast<uint32>(processedLength))) {
        Disconnect("receive buffer accounting error");
        return;
    }

    _recvBuffer.Clean();
    RegisterRecv();
}

void Session::ProcessSend(int32 numOfBytes) {
    _sendEvent.owner = nullptr;
    _sendEvent.sendBuffers.clear();

    if (numOfBytes == 0) {
        Disconnect("send returned zero bytes");
        return;
    }

    if (!IsConnected()) {
        return;
    }

    OnSend(numOfBytes);

    bool shouldRegisterAgain = false;
    {
        std::lock_guard<std::mutex> guard(_lock);
        if (_sendQueue.empty()) {
            _sendRegistered.store(false);
        } else {
            shouldRegisterAgain = true;
        }
    }

    // Deliberately outside the lock: RegisterSend takes it again.
    if (shouldRegisterAgain) {
        RegisterSend();
    }
}

void Session::HandleError(int32 errorCode, const char* operation) {
    switch (errorCode) {
        case WSAECONNRESET:
        case WSAECONNABORTED:
        case WSAENETRESET:
        case WSAESHUTDOWN:
        case ERROR_OPERATION_ABORTED:
            Disconnect("connection reset by peer");
            break;
        default:
            LOG_WARN("{} failed on {}: {}", operation, _netAddress.ToString(),
                     SocketUtils::ErrorText(errorCode));
            Disconnect("socket error");
            break;
    }
}

// ---------------------------------------------------------------------------
// PacketSession
// ---------------------------------------------------------------------------

int32 PacketSession::OnRecv(uint8* buffer, int32 length) {
    int32 processedLength = 0;

    for (;;) {
        const int32 remaining = length - processedLength;
        if (remaining < static_cast<int32>(sizeof(PacketHeader))) {
            break;
        }

        PacketHeader header{};
        std::memcpy(&header, buffer + processedLength, sizeof(header));

        // The declared size must cover its own header and stay inside the
        // agreed ceiling. Anything else is a desynchronised or hostile stream,
        // and there is no way to resynchronise a length-prefixed protocol.
        if (header.size < sizeof(PacketHeader) || header.size > kMaxPacketSize) {
            LOG_WARN("bad packet length {} from {}", header.size, GetNetAddress().ToString());
            return -1;
        }

        if (remaining < header.size) {
            break;  // Wait for the rest of the frame.
        }

        OnRecvPacket(buffer + processedLength, header.size);
        processedLength += header.size;
    }

    return processedLength;
}

}  // namespace nhn
