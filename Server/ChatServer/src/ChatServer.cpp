#include "chat/ChatServer.h"

namespace nhn::chat {

using namespace proto;

Ref<ChannelManager> GChannelManager;
Ref<TicketTable> GTicketTable;
Ref<ChatServer> GChatServer;

void ChatServer::DispatchClientPacket(const ChatSessionRef& session, uint8* buffer, int32 length) {
    if (!session->IsAuthenticated() && PeekPacketId(buffer, length) != PacketId::C_ChatHello) {
        S_ChatError error;
        error.result = ResultCode::NotAuthenticated;
        session->SendPacket(error);
        session->Disconnect("packet before chat hello");
        return;
    }
    _clientDispatcher.Dispatch(session, buffer, length);
}

void ChatServer::RegisterClientHandlers() {
    _clientDispatcher.On<C_ChatHello>(
        [](const ChatSessionRef& session, const C_ChatHello& packet) {
            if (session->IsAuthenticated()) {
                S_ChatHelloAck ack;
                ack.result = ResultCode::AlreadyAuthenticated;
                session->SendPacket(ack);
                return;
            }

            TicketTable::Entry entry;
            ResultCode reason = ResultCode::TicketInvalid;
            if (!GTicketTable->Consume(packet.ticket, NowUnixMs(), entry, reason)) {
                S_ChatHelloAck ack;
                ack.result = reason;
                session->SendPacket(ack);
                session->Disconnect("ticket rejected");
                LOG_WARN("chat ticket rejected from {}: {}", session->GetNetAddress().ToString(),
                         ToString(reason));
                return;
            }

            session->Authenticate(entry.sessionId, entry.nickname);
            GChannelManager->OnSessionAuthenticated(session);

            S_ChatHelloAck ack;
            ack.result = ResultCode::Ok;
            ack.sessionId = entry.sessionId;
            session->SendPacket(ack);

            LOG_INFO("chat session {} '{}' authenticated", entry.sessionId, entry.nickname);
        });

    _clientDispatcher.On<C_ChatSend>(
        [](const ChatSessionRef& session, const C_ChatSend& packet) {
            auto reject = [&session](ResultCode result) {
                S_ChatError error;
                error.result = result;
                session->SendPacket(error);
            };

            // Rate limit first: a client spraying oversized messages should not
            // get a cheaper rejection than one sending valid ones.
            if (!session->ConsumeSendBudget()) {
                reject(ResultCode::RateLimited);
                return;
            }

            if (packet.message.empty()) {
                reject(ResultCode::MessageEmpty);
                return;
            }
            if (packet.message.size() > kMaxChatMessageLength) {
                reject(ResultCode::MessageTooLong);
                return;
            }
            if (!IsCleanUtf8(packet.message, kMaxChatMessageLength)) {
                reject(ResultCode::MessageInvalidEncoding);
                return;
            }

            // The client picks a channel *kind*; the id comes from what the
            // match server told us, so nobody can address a room they are not in.
            const uint64 channelId = session->GetChannelId(packet.channelType);
            if (packet.channelType != ChannelType::Global && channelId == 0) {
                reject(ResultCode::NotInChannel);
                return;
            }

            ChannelRef channel = GChannelManager->Find(packet.channelType, channelId);
            if (channel == nullptr) {
                reject(ResultCode::NotInChannel);
                return;
            }

            channel->EnqueueMessage(session->GetSessionId(), session->GetNickname(),
                                    packet.message);
        });
}

void ChatServer::RegisterPeerHandlers() {
    _peerDispatcher.On<P_TicketAdd>(
        [](const PeerLinkSessionRef&, const P_TicketAdd& packet) {
            GTicketTable->Add(packet.ticket, packet.sessionId, packet.nickname,
                              packet.expiresAtMs);
        });

    _peerDispatcher.On<P_TicketRevoke>(
        [](const PeerLinkSessionRef&, const P_TicketRevoke& packet) {
            GTicketTable->Revoke(packet.ticket);
        });

    _peerDispatcher.On<P_ChannelJoin>(
        [](const PeerLinkSessionRef&, const P_ChannelJoin& packet) {
            GChannelManager->ApplyChannelJoin(packet.sessionId, packet.channelType,
                                              packet.channelId);
        });

    _peerDispatcher.On<P_ChannelLeave>(
        [](const PeerLinkSessionRef&, const P_ChannelLeave& packet) {
            GChannelManager->ApplyChannelLeave(packet.sessionId, packet.channelType,
                                               packet.channelId);
            GChannelManager->RemoveIfEmpty(packet.channelType, packet.channelId);
        });

    _peerDispatcher.On<P_SessionClosed>(
        [](const PeerLinkSessionRef&, const P_SessionClosed& packet) {
            // The match server saw the player go. Drop their chat connection
            // too rather than leaving a socket that can no longer be addressed.
            if (ChatSessionRef session = GChannelManager->FindSession(packet.sessionId)) {
                session->Disconnect("player left the match server");
            }
            GChannelManager->OnSessionClosed(packet.sessionId);
        });
}

void ChatServer::StartMaintenanceTimer(const JobQueueRef& queue) {
    const uint32 interval = _settings.ticketSweepIntervalMs;
    queue->DoTimer(interval, [queue, interval, this]() {
        GTicketTable->PurgeExpired(NowUnixMs());
        StartMaintenanceTimer(queue);
    });
}

}  // namespace nhn::chat
