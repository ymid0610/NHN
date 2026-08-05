#pragma once

#include <vector>

#include "core/Types.h"

namespace nhn {

enum class EventType : uint8 {
    Connect,
    Disconnect,
    Accept,
    Recv,
    Send,
    UdpRecv,
    UdpSend,
};

class IocpObject;
using IocpObjectRef = Ref<IocpObject>;

/// An OVERLAPPED extended with the information the completion port cannot carry
/// on its own: which operation finished, and which object it belongs to.
///
/// `owner` is a strong reference held for the entire time the operation is in
/// flight. That is what stops a session from being destroyed while the kernel
/// still holds a pointer into its buffers.
class IocpEvent : public OVERLAPPED {
public:
    explicit IocpEvent(EventType type) : eventType(type) { Init(); }

    void Init() {
        OVERLAPPED* base = static_cast<OVERLAPPED*>(this);
        *base = OVERLAPPED{};
    }

    EventType eventType;
    IocpObjectRef owner;
};

class ConnectEvent : public IocpEvent {
public:
    ConnectEvent() : IocpEvent(EventType::Connect) {}
};

class DisconnectEvent : public IocpEvent {
public:
    DisconnectEvent() : IocpEvent(EventType::Disconnect) {}
};

class AcceptEvent : public IocpEvent {
public:
    AcceptEvent() : IocpEvent(EventType::Accept) {}

    /// AcceptEx writes the local and remote addresses here. It requires room
    /// for two sockaddrs each padded by 16 bytes, even when asked to receive no
    /// data with the accept.
    static constexpr uint32 kAddressBufferSize = (sizeof(SOCKADDR_IN) + 16) * 2;

    Ref<class Session> session;
    uint8 addressBuffer[kAddressBufferSize] = {};
};

class RecvEvent : public IocpEvent {
public:
    RecvEvent() : IocpEvent(EventType::Recv) {}
};

class SendEvent : public IocpEvent {
public:
    SendEvent() : IocpEvent(EventType::Send) {}
    std::vector<Ref<class SendBuffer>> sendBuffers;
};

/// Anything that can be associated with the completion port and receive
/// completions.
class IocpObject : public std::enable_shared_from_this<IocpObject> {
public:
    virtual ~IocpObject() = default;
    virtual HANDLE GetHandle() = 0;
    virtual void Dispatch(IocpEvent* iocpEvent, int32 numOfBytes = 0) = 0;
};

/// Thin wrapper over a completion port. One instance is shared by every
/// service in a process, so a server that both listens for clients and dials
/// out to peers still uses a single worker pool.
class IocpCore {
public:
    IocpCore();
    ~IocpCore();

    IocpCore(const IocpCore&) = delete;
    IocpCore& operator=(const IocpCore&) = delete;

    HANDLE GetHandle() const { return _iocpHandle; }

    bool Register(IocpObjectRef iocpObject);

    /// Waits for one completion and routes it to its owner.
    /// @return false only on timeout, so callers can use it to poll.
    bool Dispatch(uint32 timeoutMs = INFINITE);

private:
    HANDLE _iocpHandle = INVALID_HANDLE_VALUE;
};

using IocpCoreRef = Ref<IocpCore>;

}  // namespace nhn
