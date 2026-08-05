#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <vector>

#include "core/IocpCore.h"
#include "core/Net.h"

namespace nhn {

/// Largest datagram the voice path will send or accept. Comfortably under a
/// 1500-byte Ethernet MTU so frames are never fragmented, which for voice would
/// turn one lost fragment into a lost packet.
inline constexpr uint32 kMaxDatagramSize = 1400;

/// A UDP endpoint driven by the same completion port as the TCP services.
///
/// Receives are overlapped, with several posted concurrently so a burst of
/// speakers does not serialise. Sends deliberately are not: a UDP sendto only
/// copies into the socket buffer and returns — there is no handshake or
/// congestion window to block on — so making them overlapped would add a
/// heap allocation and a completion per 20 ms frame per listener to buy
/// nothing.
class UdpSocket : public IocpObject {
public:
    /// Invoked on an IOCP worker for every datagram. @p from is the address the
    /// packet actually arrived from, which for a NAT-ed client is the mapping
    /// its router created — the only address replies can be sent to.
    using RecvHandler =
        std::function<void(const NetAddress& from, const uint8* data, int32 length)>;

    ~UdpSocket() override;

    bool Start(uint16 port, RecvHandler handler, int32 concurrentReceives = 16);
    void Stop();

    bool SendTo(const NetAddress& to, const uint8* data, int32 length);

    NetAddress GetBoundAddress() const { return _boundAddress; }

    HANDLE GetHandle() override;
    void Dispatch(IocpEvent* iocpEvent, int32 numOfBytes = 0) override;

private:
    struct UdpRecvEvent : IocpEvent {
        UdpRecvEvent() : IocpEvent(EventType::UdpRecv) {}

        std::array<uint8, kMaxDatagramSize> buffer{};
        SOCKADDR_IN from{};
        int32 fromLength = sizeof(SOCKADDR_IN);
        WSABUF wsaBuf{};
        DWORD flags = 0;
    };

    void RegisterRecv(UdpRecvEvent* recvEvent);
    void ProcessRecv(UdpRecvEvent* recvEvent, int32 numOfBytes);

    SOCKET _socket = INVALID_SOCKET;
    NetAddress _boundAddress;
    std::vector<UdpRecvEvent*> _recvEvents;
    RecvHandler _handler;
    std::atomic<bool> _running{false};
};

using UdpSocketRef = Ref<UdpSocket>;

}  // namespace nhn
