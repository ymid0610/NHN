#pragma once

#include <cstring>
#include <functional>
#include <type_traits>
#include <unordered_map>

#include "core/Buffer.h"
#include "core/Log.h"
#include "core/Session.h"
#include "protocol/Packets.h"

namespace nhn::proto {

/// Serialises @p packet, with its framing header, into a pooled send buffer.
/// @return nullptr if the packet does not fit, having logged the reason.
template <class T>
SendBufferRef MakePacket(T&& packet) {
    using Packet = std::remove_cvref_t<T>;

    SendBufferRef sendBuffer = SendBufferManager::Open(kMaxPacketSize);
    if (sendBuffer == nullptr) {
        LOG_ERROR("no send buffer available for {}", Packet::kName);
        return nullptr;
    }

    BufferWriter writer(sendBuffer->Buffer(), sendBuffer->AllocSize());

    // The header is written last, once the body length is known, but its space
    // has to be claimed first so the body lands after it.
    uint8* const headerSlot = writer.Reserve(sizeof(PacketHeader));
    if (headerSlot == nullptr) {
        sendBuffer->Close(0);
        return nullptr;
    }

    packet.Serialize(writer);

    if (writer.Failed() || writer.WriteSize() > kMaxPacketSize) {
        LOG_ERROR("{} exceeds the {} byte packet ceiling", Packet::kName, kMaxPacketSize);
        sendBuffer->Close(0);
        return nullptr;
    }

    const PacketHeader header{static_cast<uint16>(writer.WriteSize()),
                              static_cast<uint16>(Packet::kId)};
    // memcpy rather than a cast: the slot is at an arbitrary offset inside the
    // chunk and is not guaranteed to be suitably aligned.
    std::memcpy(headerSlot, &header, sizeof(header));

    sendBuffer->Close(writer.WriteSize());
    return sendBuffer;
}

inline PacketId PeekPacketId(const uint8* buffer, int32 length) {
    if (buffer == nullptr || length < static_cast<int32>(sizeof(PacketHeader))) {
        return PacketId::None;
    }
    PacketHeader header{};
    std::memcpy(&header, buffer, sizeof(header));
    return static_cast<PacketId>(header.id);
}

/// Reads a packet body out of a complete frame.
///
/// Trailing bytes are tolerated on purpose: it lets a newer client append
/// fields that an older server simply ignores, which is the only version
/// tolerance a hand-rolled format gets for free.
template <class T>
bool ParsePacket(T& out, const uint8* buffer, int32 length) {
    if (buffer == nullptr || length < static_cast<int32>(sizeof(PacketHeader))) {
        return false;
    }
    BufferReader reader(buffer, static_cast<uint32>(length), sizeof(PacketHeader));
    out.Serialize(reader);
    return !reader.Failed();
}

/// Routes framed packets to typed handlers.
///
/// Handlers are registered once during start-up and only read afterwards, so
/// dispatch needs no synchronisation. Parsing failures disconnect the peer:
/// a frame that does not deserialise means the two sides disagree about the
/// protocol, and nothing good comes of continuing.
template <class SessionT>
class PacketDispatcher {
public:
    using SessionPtr = Ref<SessionT>;
    using RawHandler = std::function<void(const SessionPtr&, const uint8*, int32)>;

    /// @p handler is invoked as handler(session, const P&).
    template <class P, class F>
    void On(F&& handler) {
        _handlers[static_cast<uint16>(P::kId)] =
            [fn = std::forward<F>(handler)](const SessionPtr& session, const uint8* buffer,
                                            int32 length) {
                P packet{};
                if (!ParsePacket(packet, buffer, length)) {
                    LOG_WARN("malformed {} from {}", P::kName,
                             session->GetNetAddress().ToString());
                    session->Disconnect("malformed packet");
                    return;
                }
                fn(session, packet);
            };
    }

    bool Dispatch(const SessionPtr& session, const uint8* buffer, int32 length) const {
        const PacketId id = PeekPacketId(buffer, length);
        const auto it = _handlers.find(static_cast<uint16>(id));
        if (it == _handlers.end()) {
            LOG_WARN("no handler for packet id {} from {}", static_cast<uint16>(id),
                     session->GetNetAddress().ToString());
            return false;
        }
        it->second(session, buffer, length);
        return true;
    }

private:
    std::unordered_map<uint16, RawHandler> _handlers;
};

}  // namespace nhn::proto
