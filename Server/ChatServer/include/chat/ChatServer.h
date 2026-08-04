#pragma once

#include "chat/Channel.h"
#include "chat/ChatSession.h"
#include "common/PeerLink.h"
#include "common/TicketTable.h"
#include "core/Core.h"

namespace nhn::chat {

class ChatServer;

extern Ref<ChannelManager> GChannelManager;
extern Ref<TicketTable> GTicketTable;
extern Ref<ChatServer> GChatServer;

class ChatServer {
public:
    struct Settings {
        uint16 clientPort = 7800;
        /// Burst allowance and sustained rate for chat messages, per session.
        uint32 messageBurst = 4;
        double messagesPerSecond = 1.5;
        uint32 ticketSweepIntervalMs = 5000;
    };

    explicit ChatServer(Settings settings) : _settings(settings) {}

    const Settings& GetSettings() const { return _settings; }

    void RegisterClientHandlers();
    void RegisterPeerHandlers();

    void DispatchClientPacket(const ChatSessionRef& session, uint8* buffer, int32 length);

    PeerLink::Dispatcher* GetPeerDispatcher() { return &_peerDispatcher; }

    void StartMaintenanceTimer(const JobQueueRef& queue);

private:
    Settings _settings;
    proto::PacketDispatcher<ChatSession> _clientDispatcher;
    PeerLink::Dispatcher _peerDispatcher;
};

}  // namespace nhn::chat
