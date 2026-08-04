#pragma once

#include <atomic>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>

#include "core/Archive.h"
#include "core/Buffer.h"
#include "core/IocpCore.h"
#include "core/Net.h"

namespace nhn {

class Service;

/// One TCP connection.
///
/// Lifetime is governed by the strong reference each in-flight overlapped
/// operation holds through IocpEvent::owner: the object cannot be destroyed
/// while the kernel still points into its buffers. Closing the socket forces
/// those operations to complete, which is what finally releases it.
class Session : public IocpObject {
    friend class Listener;
    friend class Service;
    friend class ServerService;
    friend class ClientService;

public:
    Session();
    ~Session() override;

    /// Queues one packet. Thread-safe, and safe to call with the same
    /// SendBufferRef on many sessions — that is how broadcasts avoid copying.
    void Send(SendBufferRef sendBuffer);

    /// Client-side dial. Only valid on a session owned by a ClientService.
    bool Connect();

    /// Begins an orderly shutdown. Idempotent: repeated calls after the first
    /// are ignored, so error paths can call it freely.
    void Disconnect(std::string_view cause);

    Ref<Service> GetService() const { return _service.lock(); }
    void SetService(Ref<Service> service) { _service = service; }

    NetAddress GetNetAddress() const { return _netAddress; }
    void SetNetAddress(NetAddress address) { _netAddress = address; }
    SOCKET GetSocket() const { return _socket; }
    bool IsConnected() const { return _connected.load(std::memory_order_acquire); }

    Ref<Session> GetSessionRef() { return std::static_pointer_cast<Session>(shared_from_this()); }

    /// Reason recorded by the first Disconnect call, for logging.
    std::string GetDisconnectCause() const;

    HANDLE GetHandle() override;
    void Dispatch(IocpEvent* iocpEvent, int32 numOfBytes = 0) override;

protected:
    virtual void OnConnected() {}
    virtual void OnDisconnected() {}
    virtual void OnSend(int32 length) { (void)length; }

    /// @return how many bytes were consumed; the remainder stays buffered for
    ///         the next receive. Return -1 to signal a protocol violation.
    virtual int32 OnRecv(uint8* buffer, int32 length) {
        (void)buffer;
        return length;
    }

private:
    bool RegisterConnect();
    bool RegisterDisconnect();
    void RegisterRecv();
    void RegisterSend();

    void ProcessConnect();

    /// Accept-side counterpart to ProcessConnect. The socket options and
    /// address lookup differ, so the listener does those and calls this to
    /// bring the session live.
    void ProcessAccepted();

    void ProcessDisconnect();
    void ProcessRecv(int32 numOfBytes);
    void ProcessSend(int32 numOfBytes);

    void HandleError(int32 errorCode, const char* operation);

    Weak<Service> _service;
    SOCKET _socket = INVALID_SOCKET;
    NetAddress _netAddress;
    std::atomic<bool> _connected{false};

    mutable std::mutex _lock;
    RecvBuffer _recvBuffer;
    std::queue<SendBufferRef> _sendQueue;
    std::atomic<bool> _sendRegistered{false};
    std::string _disconnectCause;

    ConnectEvent _connectEvent;
    DisconnectEvent _disconnectEvent;
    RecvEvent _recvEvent;
    SendEvent _sendEvent;
};

using SessionRef = Ref<Session>;

/// A Session that slices the byte stream into length-prefixed packets.
///
/// Framing lives here rather than in each server so that a malformed length is
/// rejected in exactly one place.
class PacketSession : public Session {
public:
    Ref<PacketSession> GetPacketSessionRef() {
        return std::static_pointer_cast<PacketSession>(shared_from_this());
    }

protected:
    int32 OnRecv(uint8* buffer, int32 length) final;

    /// @param buffer points at the header; @param length is header.size.
    virtual void OnRecvPacket(uint8* buffer, int32 length) = 0;
};

using PacketSessionRef = Ref<PacketSession>;

}  // namespace nhn
