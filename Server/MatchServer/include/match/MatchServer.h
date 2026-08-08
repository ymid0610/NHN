#pragma once

#include <array>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Core.h"
#include "match/ClientSession.h"
#include "match/MatchQueue.h"
#include "match/PeerRegistry.h"
#include "match/Room.h"
#include "protocol/Dispatcher.h"

namespace nhn::match {

/// Ties the pieces together: packet routing, ticket issuance and the room to
/// instance handoff.
///
/// Derives from JobQueue so handoff bookkeeping — which is touched from client
/// handlers, peer handlers and a timeout timer — is serialised without locks.
class MatchServer : public JobQueue {
public:
    struct Settings {
        uint16 clientPort = 7777;
        uint16 peerPort = 7900;
        /// How long an issued ticket stays redeemable.
        uint32 ticketLifetimeMs = 30'000;
        /// Grace period for a room to reach a running instance before the
        /// handoff is abandoned and everyone is returned to the lobby.
        uint32 handoffTimeoutMs = 15'000;
        uint32 kickCooldownMs = 60'000;
        int32 maxRooms = 4096;
        int32 maxSessions = 4096;
        /// Once a mode's queue has the minimum to start, how long to keep
        /// waiting for it to fill to capacity before starting anyway. Gomoku
        /// runs 2-4, so without this two players would either wait forever for
        /// a third or never get a four-player game.
        uint32 quickMatchFillMs = 8'000;
        /// A peer is dropped after this long without a word. Satellites
        /// heartbeat every 2 s, so this allows several to be missed.
        uint32 peerSilenceTimeoutMs = 10'000;
    };

    explicit MatchServer(Settings settings) : _settings(settings) {}

    const Settings& GetSettings() const { return _settings; }

    void RegisterClientHandlers();
    void RegisterPeerHandlers();

    void DispatchClientPacket(const ClientSessionRef& session, uint8* buffer, int32 length);
    void DispatchPeerPacket(const PeerSessionRef& session, uint8* buffer, int32 length);

    /// Mints chat and voice tickets for a freshly authenticated player and
    /// pushes them to those servers before the client is told where to connect.
    void IssueClientTickets(const ClientSessionRef& session, proto::S_HelloAck& outAck);

    /// Tells chat and voice that a session entered or left a channel. Room and
    /// instance channels are entirely server-driven; clients never name a
    /// channel id.
    void PushChannelJoin(SessionId sessionId, proto::ChannelType type, uint64 channelId);
    void PushChannelLeave(SessionId sessionId, proto::ChannelType type, uint64 channelId);
    void PushSessionClosed(SessionId sessionId);

    /// Takes a departing session out of the quick-match queue. Safe to call for
    /// a session that was never queued.
    void RemoveFromQuickMatch(SessionId sessionId);

    /// Begins a handoff for @p room. Replies arrive asynchronously on the peer
    /// link and end in Room::EnqueueHandoffReady or EnqueueHandoffFailed.
    void BeginHandoff(const RoomRef& room, const std::vector<proto::RoomMemberInfo>& members,
                      const proto::MatchConfig& config);

    /// Re-publishes state a reconnecting peer has lost. Without this a chat
    /// server restart would leave every room channel empty until players
    /// happened to move.
    void ResyncPeer(const PeerSessionRef& peer);

    void StartMaintenanceTimer();

private:
    // -- quick match ---------------------------------------------------------
    //
    // A queue rather than a room search. The player chooses a mode and nothing
    // else: no room to find, no settings to agree, no host to press start. When
    // enough have gathered the server builds the party and hands it straight to
    // an instance with the mode's default settings.

    void QuickMatchJoin(const ClientSessionRef& session, proto::GameMode mode);
    void QuickMatchCancel(const ClientSessionRef& session);
    /// Re-sends the waiting count to everyone queued for @p mode.
    void QuickMatchBroadcast(proto::GameMode mode, proto::ResultCode result);
    /// @param fillWindowExpired true when called from the fill timer, which is
    ///        what allows a start below capacity.
    void QuickMatchTryForm(proto::GameMode mode, bool fillWindowExpired);
    void QuickMatchForm(proto::GameMode mode);
    void QuickMatchArmFillTimer(proto::GameMode mode);

    enum class HandoffPhase : uint8 {
        AwaitingCreate,  // P_InstanceCreate sent, no ack yet
        AwaitingJoin,    // clients told where to go, waiting for them to arrive
        Running,         // instance confirmed everyone connected
    };

    struct PendingHandoff {
        RoomId roomId = kInvalidRoomId;
        InstanceId instanceId = kInvalidInstanceId;
        HandoffPhase phase = HandoffPhase::AwaitingCreate;
        TickCount startedAt = 0;
        proto::GameMode mode = proto::GameMode::None;
        proto::MatchConfig config;
        std::vector<proto::RoomMemberInfo> members;
        Weak<PeerSession> instancePeer;
    };

    /// One session's presence in one channel. Mirrored here so a chat or voice
    /// server that reconnects can be handed the full picture, rather than
    /// waiting for players to move before its channels repopulate.
    struct Membership {
        SessionId sessionId = kInvalidSessionId;
        proto::ChannelType channelType = proto::ChannelType::None;
        uint64 channelId = 0;

        bool operator<(const Membership& other) const {
            if (sessionId != other.sessionId) return sessionId < other.sessionId;
            if (channelType != other.channelType) return channelType < other.channelType;
            return channelId < other.channelId;
        }
    };

    void HandleInstanceCreateAck(const PeerSessionRef& peer, const proto::P_InstanceCreateAck& ack);
    void HandleInstanceReady(const proto::P_InstanceReady& ready);
    void HandleInstanceClosed(const proto::P_InstanceClosed& closed);
    void SweepHandoffs();

    Settings _settings;
    proto::PacketDispatcher<ClientSession> _clientDispatcher;
    proto::PacketDispatcher<PeerSession> _peerDispatcher;

    /// Only touched on this object's job queue.
    std::unordered_map<InstanceId, PendingHandoff> _pendingHandoffs;
    std::set<Membership> _memberships;
    InstanceId _nextInstanceId = 1;
    /// Only for naming auto-created parties readably; not an identifier.
    std::atomic<uint64> _quickMatchCounter{1};

    /// Only touched on this object's job queue, like the handoff bookkeeping.
    MatchQueue _matchQueue;
    /// One fill timer per mode, so a second player arriving does not restart
    /// the wait for the first.
    std::array<bool, 4> _fillTimerArmed{};
};

}  // namespace nhn::match
