#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "core/Core.h"
#include "protocol/Dispatcher.h"
#include "protocol/Packets.h"

namespace nhn::match {

/// The match server's end of a control connection from chat, voice or instance.
class PeerSession : public PacketSession {
public:
    proto::ServerType GetServerType() const {
        return _serverType.load(std::memory_order_acquire);
    }
    void SetServerType(proto::ServerType type) {
        _serverType.store(type, std::memory_order_release);
    }

    std::string GetServerId() const;
    void SetServerId(std::string id);

    std::string GetPublicHost() const;
    uint16 GetPublicPort() const { return _publicPort.load(std::memory_order_acquire); }
    uint16 GetPublicWebPort() const { return _publicWebPort.load(std::memory_order_acquire); }
    void SetPublicEndpoint(std::string host, uint16 port, uint16 webPort);

    int32 GetCapacity() const { return _capacity.load(std::memory_order_acquire); }
    void SetCapacity(int32 capacity) { _capacity.store(capacity, std::memory_order_release); }

    int32 GetLoad() const { return _load.load(std::memory_order_acquire); }
    void AddLoad(int32 delta) { _load.fetch_add(delta, std::memory_order_acq_rel); }
    void SetLoad(int32 load) { _load.store(load, std::memory_order_release); }

    template <class T>
    void SendPacket(T&& packet) {
        Send(proto::MakePacket(std::forward<T>(packet)));
    }

protected:
    void OnDisconnected() override;
    void OnRecvPacket(uint8* buffer, int32 length) override;

private:
    std::atomic<proto::ServerType> _serverType{proto::ServerType::None};
    std::atomic<uint16> _publicPort{0};
    std::atomic<uint16> _publicWebPort{0};
    std::atomic<int32> _capacity{0};
    std::atomic<int32> _load{0};

    mutable std::mutex _lock;
    std::string _serverId;
    std::string _publicHost;
};

using PeerSessionRef = Ref<PeerSession>;

/// Which satellite servers are currently up, and how to reach them.
///
/// Peers dial in and announce themselves, so this fills itself; nothing here is
/// configured ahead of time. A peer that drops simply disappears from the
/// registry and reappears when it reconnects.
class PeerRegistry {
public:
    void Register(const PeerSessionRef& session);
    void Unregister(const PeerSessionRef& session);

    /// Any live peer of that type, or nullptr.
    PeerSessionRef FindAny(proto::ServerType type) const;

    /// Least-loaded instance server with spare capacity.
    PeerSessionRef PickInstanceServer() const;

    /// Public endpoint clients should be told to use.
    /// @param forWebSocket picks the browser-facing port instead of the raw one.
    bool GetClientEndpoint(proto::ServerType type, bool forWebSocket, std::string& outHost,
                           uint16& outPort) const;

    void ForEach(proto::ServerType type, const std::function<void(const PeerSessionRef&)>& fn) const;

    /// Sends to every peer of a type. Used to fan ticket and channel updates
    /// out to chat and voice at once.
    void SendToAll(proto::ServerType type, const SendBufferRef& sendBuffer);

    int32 Count(proto::ServerType type) const;

private:
    mutable std::mutex _lock;
    std::vector<PeerSessionRef> _peers;
};

}  // namespace nhn::match
