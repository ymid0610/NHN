#include "core/UdpSocket.h"

#include "core/CoreGlobal.h"
#include "core/FatalHandler.h"
#include "core/Log.h"

namespace nhn {

UdpSocket::~UdpSocket() {
    Stop();
    for (UdpRecvEvent* recvEvent : _recvEvents) {
        delete recvEvent;
    }
    _recvEvents.clear();
}

HANDLE UdpSocket::GetHandle() {
    return reinterpret_cast<HANDLE>(_socket);
}

bool UdpSocket::Start(uint16 port, RecvHandler handler, int32 concurrentReceives) {
    _handler = std::move(handler);
    NHN_CHECK(_handler != nullptr, "UdpSocket needs a receive handler");

    _socket = SocketUtils::CreateUdpSocket();
    if (_socket == INVALID_SOCKET) {
        LOG_ERROR("cannot create udp socket: {}", SocketUtils::ErrorText(::WSAGetLastError()));
        return false;
    }

    // Without this, a single client going away triggers an ICMP port-unreachable
    // that Windows surfaces as WSAECONNRESET on the *shared* socket, killing
    // voice for everyone.
    SocketUtils::DisableUdpConnResetReport(_socket);
    SocketUtils::SetReuseAddress(_socket, true);
    SocketUtils::SetRecvBufferSize(_socket, 1 * 1024 * 1024);
    SocketUtils::SetSendBufferSize(_socket, 1 * 1024 * 1024);

    if (!SocketUtils::BindAnyAddress(_socket, port)) {
        LOG_ERROR("cannot bind udp port {}: {}", port,
                  SocketUtils::ErrorText(::WSAGetLastError()));
        return false;
    }

    if (!GIocpCore->Register(shared_from_this())) {
        LOG_ERROR("cannot associate udp socket with completion port");
        return false;
    }

    _boundAddress = NetAddress::Any(port);
    _running.store(true, std::memory_order_release);

    _recvEvents.reserve(static_cast<size_t>(concurrentReceives));
    for (int32 i = 0; i < concurrentReceives; ++i) {
        auto* recvEvent = new UdpRecvEvent();
        _recvEvents.push_back(recvEvent);
        RegisterRecv(recvEvent);
    }

    LOG_INFO("udp listening on port {} with {} concurrent receives", port, concurrentReceives);
    return true;
}

void UdpSocket::Stop() {
    if (!_running.exchange(false)) {
        return;
    }
    // Closing completes every posted receive with ERROR_OPERATION_ABORTED,
    // which releases the owner references they hold on this object.
    SocketUtils::Close(_socket);
}

void UdpSocket::RegisterRecv(UdpRecvEvent* recvEvent) {
    if (!_running.load(std::memory_order_acquire)) {
        return;
    }

    recvEvent->Init();
    recvEvent->owner = shared_from_this();
    recvEvent->fromLength = sizeof(SOCKADDR_IN);
    recvEvent->flags = 0;
    recvEvent->wsaBuf.buf = reinterpret_cast<char*>(recvEvent->buffer.data());
    recvEvent->wsaBuf.len = static_cast<ULONG>(recvEvent->buffer.size());

    DWORD numOfBytes = 0;
    if (::WSARecvFrom(_socket, &recvEvent->wsaBuf, 1, &numOfBytes, &recvEvent->flags,
                      reinterpret_cast<SOCKADDR*>(&recvEvent->from),
                      reinterpret_cast<LPINT>(&recvEvent->fromLength), recvEvent,
                      nullptr) == SOCKET_ERROR) {
        const int32 errorCode = ::WSAGetLastError();
        if (errorCode != WSA_IO_PENDING) {
            recvEvent->owner = nullptr;
            if (_running.load(std::memory_order_acquire)) {
                LOG_WARN("WSARecvFrom failed: {}", SocketUtils::ErrorText(errorCode));
            }
        }
    }
}

void UdpSocket::Dispatch(IocpEvent* iocpEvent, int32 numOfBytes) {
    if (iocpEvent->eventType != EventType::UdpRecv) {
        return;
    }
    ProcessRecv(static_cast<UdpRecvEvent*>(iocpEvent), numOfBytes);
}

void UdpSocket::ProcessRecv(UdpRecvEvent* recvEvent, int32 numOfBytes) {
    recvEvent->owner = nullptr;

    if (!_running.load(std::memory_order_acquire)) {
        return;
    }

    if (numOfBytes > 0) {
        const NetAddress from(recvEvent->from);
        // A handler must never throw here: it runs on an IOCP worker, and an
        // escape would unwind past the completion loop.
        _handler(from, recvEvent->buffer.data(), numOfBytes);
    }

    // Re-arm regardless. A zero-length or failed receive is not a reason to
    // give up the slot; on a shared UDP socket that would leak capacity.
    RegisterRecv(recvEvent);
}

bool UdpSocket::SendTo(const NetAddress& to, const uint8* data, int32 length) {
    if (!_running.load(std::memory_order_acquire) || length <= 0) {
        return false;
    }
    if (static_cast<uint32>(length) > kMaxDatagramSize) {
        LOG_WARN("refusing to send a {} byte datagram", length);
        return false;
    }

    const SOCKADDR_IN& target = to.GetSockAddr();
    const int32 sent = ::sendto(_socket, reinterpret_cast<const char*>(data), length, 0,
                                reinterpret_cast<const SOCKADDR*>(&target), sizeof(SOCKADDR_IN));
    if (sent == SOCKET_ERROR) {
        const int32 errorCode = ::WSAGetLastError();
        // WSAEWOULDBLOCK means the send buffer is momentarily full. Dropping
        // the frame is the correct response for voice — retrying would deliver
        // audio that is already too late to play.
        if (errorCode != WSAEWOULDBLOCK) {
            LOG_DEBUG("sendto {} failed: {}", to.ToString(), SocketUtils::ErrorText(errorCode));
        }
        return false;
    }
    return true;
}

}  // namespace nhn
