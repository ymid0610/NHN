#include "core/Listener.h"

#include "core/CoreGlobal.h"
#include "core/FatalHandler.h"
#include "core/Log.h"
#include "core/Service.h"
#include "core/Session.h"

namespace nhn {

Listener::~Listener() {
    CloseSocket();
    for (AcceptEvent* acceptEvent : _acceptEvents) {
        delete acceptEvent;
    }
    _acceptEvents.clear();
}

HANDLE Listener::GetHandle() {
    return reinterpret_cast<HANDLE>(_socket);
}

void Listener::CloseSocket() {
    SocketUtils::Close(_socket);
}

bool Listener::StartAccept(Ref<ServerService> service) {
    _service = std::move(service);
    if (_service == nullptr || !_service->CanStart()) {
        return false;
    }

    _socket = SocketUtils::CreateTcpSocket();
    if (_socket == INVALID_SOCKET) {
        LOG_ERROR("cannot create listen socket: {}", SocketUtils::ErrorText(::WSAGetLastError()));
        return false;
    }

    if (!GIocpCore->Register(shared_from_this())) {
        LOG_ERROR("cannot associate listen socket with completion port");
        return false;
    }

    if (!SocketUtils::SetReuseAddress(_socket, true)) {
        LOG_ERROR("SO_REUSEADDR failed: {}", SocketUtils::ErrorText(::WSAGetLastError()));
        return false;
    }
    // Abortive close on the listener so a restart is not blocked by TIME_WAIT.
    if (!SocketUtils::SetLinger(_socket, 0, 0)) {
        LOG_ERROR("SO_LINGER failed: {}", SocketUtils::ErrorText(::WSAGetLastError()));
        return false;
    }
    if (!SocketUtils::Bind(_socket, _service->GetNetAddress())) {
        LOG_ERROR("cannot bind {}: {}", _service->GetNetAddress().ToString(),
                  SocketUtils::ErrorText(::WSAGetLastError()));
        return false;
    }
    if (!SocketUtils::Listen(_socket)) {
        LOG_ERROR("listen failed: {}", SocketUtils::ErrorText(::WSAGetLastError()));
        return false;
    }

    _acceptEvents.reserve(kAcceptBacklog);
    for (int32 i = 0; i < kAcceptBacklog; ++i) {
        auto* acceptEvent = new AcceptEvent();
        acceptEvent->owner = shared_from_this();
        _acceptEvents.push_back(acceptEvent);
        RegisterAccept(acceptEvent);
    }

    LOG_INFO("listening on {}", _service->GetNetAddress().ToString());
    return true;
}

void Listener::Dispatch(IocpEvent* iocpEvent, int32 /*numOfBytes*/) {
    NHN_CHECK(iocpEvent->eventType == EventType::Accept, "listener received a non-accept event");
    ProcessAccept(static_cast<AcceptEvent*>(iocpEvent));
}

void Listener::RegisterAccept(AcceptEvent* acceptEvent) {
    // Iterative rather than recursive: a socket exhaustion spike would
    // otherwise recurse until the stack gave out.
    for (int32 attempt = 0; attempt < 8; ++attempt) {
        SessionRef session = _service->CreateSession();
        if (session == nullptr || session->GetSocket() == INVALID_SOCKET) {
            LOG_ERROR("cannot create a session for an accept slot");
            return;
        }

        acceptEvent->Init();
        acceptEvent->owner = shared_from_this();
        acceptEvent->session = session;

        DWORD bytesReceived = 0;
        // A receive length of zero means AcceptEx completes as soon as the
        // connection is established, instead of waiting for the client's first
        // bytes — otherwise a client that connects and stays silent would hold
        // an accept slot open indefinitely.
        if (SocketUtils::AcceptEx(_socket, session->GetSocket(), acceptEvent->addressBuffer, 0,
                                  sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
                                  &bytesReceived, static_cast<LPOVERLAPPED>(acceptEvent)) != FALSE) {
            return;  // Completed inline; the completion is still queued.
        }

        const int32 errorCode = ::WSAGetLastError();
        if (errorCode == WSA_IO_PENDING) {
            return;
        }

        LOG_WARN("AcceptEx failed, retrying: {}", SocketUtils::ErrorText(errorCode));
        acceptEvent->session = nullptr;
    }

    LOG_ERROR("giving up on an accept slot after repeated AcceptEx failures");
}

void Listener::ProcessAccept(AcceptEvent* acceptEvent) {
    SessionRef session = acceptEvent->session;
    acceptEvent->session = nullptr;

    if (session == nullptr) {
        RegisterAccept(acceptEvent);
        return;
    }

    // Until the accept context is applied the socket cannot be queried or shut
    // down; it inherits nothing from the listener on its own.
    if (!SocketUtils::SetUpdateAcceptSocket(session->GetSocket(), _socket)) {
        LOG_WARN("SO_UPDATE_ACCEPT_CONTEXT failed: {}",
                 SocketUtils::ErrorText(::WSAGetLastError()));
        RegisterAccept(acceptEvent);
        return;
    }

    if (!GIocpCore->Register(session)) {
        LOG_WARN("cannot associate accepted socket with completion port");
        RegisterAccept(acceptEvent);
        return;
    }

    SOCKADDR_IN peer{};
    int32 peerSize = sizeof(peer);
    if (::getpeername(session->GetSocket(), reinterpret_cast<SOCKADDR*>(&peer), &peerSize) ==
        SOCKET_ERROR) {
        RegisterAccept(acceptEvent);
        return;
    }

    SocketUtils::SetTcpNoDelay(session->GetSocket(), true);
    session->SetNetAddress(NetAddress(peer));
    session->ProcessAccepted();

    // Re-arm immediately so the pool never shrinks.
    RegisterAccept(acceptEvent);
}

}  // namespace nhn
