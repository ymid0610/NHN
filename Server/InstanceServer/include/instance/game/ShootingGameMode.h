#pragma once

#include <deque>
#include <string>
#include <vector>

#include "instance/Instance.h"
#include "instance/game/Board.h"
#include "instance/game/World.h"
#include "protocol/GameTypes.h"

namespace nhn::instance {

/// The shooting gallery: a flying sheet is a board, and hitting a cell claims
/// it. Everyone shoots the same pass at the same time, so a round is a race
/// rather than a sequence of turns — which is what makes the disruptive items
/// worth having.
///
/// Runs entirely on the instance's job queue: one thread is ever inside it, and
/// nothing here takes a lock.
class ShootingGameMode : public IGameMode {
public:
    ShootingGameMode(proto::GameMode mode, proto::MatchConfig config, uint32 tickIntervalMs);

    void OnStart(Instance& instance) override;
    void OnTick(Instance& instance, uint64 deltaMs) override;
    void OnMemberLeft(Instance& instance, SessionId sessionId) override;
    void OnEnd(Instance& instance, const std::string& reason) override;
    void OnFire(Instance& instance, SessionId sessionId, const proto::C_Fire& packet) override;

    const char* Name() const override { return "shooting"; }

private:
    enum class Phase : uint8 {
        Idle,
        WaveActive,
        WaveIntermission,
        RoundIntermission,
        Finished,
    };

    struct PlayerState {
        SessionId sessionId = kInvalidSessionId;
        uint8 slot = 0;
        uint8 ammo = 0;
        uint8 score = 0;
        bool present = true;

        /// Wave index the effect lasts through, inclusive. A stun is applied
        /// for the current wave; blinding for the one after, so it outlasts the
        /// pass it landed in.
        bool stunned = false;
        uint16 stunThroughWave = 0;
        bool blinded = false;
        uint16 blindThroughWave = 0;

        uint32 lastShotTick = 0;
    };

    /// A trigger pull, or something a grenade or ricochet produced. Both go
    /// through one pipeline so an item can create shots without a second code
    /// path.
    struct ShotRequest {
        SessionId shooter = kInvalidSessionId;
        uint8 slot = Board::kNoOwner;
        float x = 0.0f;
        float y = 0.0f;
        uint32 tick = 0;
        uint16 sequence = 0;
        uint8 depth = 0;
        bool derived = false;
    };

    void BeginRound(Instance& instance);
    void BeginWave(Instance& instance);
    void EndWave(Instance& instance);
    void EndRound(Instance& instance, uint8 winnerSlot);
    void Finish(Instance& instance);

    /// Runs a shot and anything it spawns, bounded so a chain cannot stall the
    /// tick.
    void ProcessShot(Instance& instance, const ShotRequest& request);
    void ResolveOne(Instance& instance, const ShotRequest& request,
                    std::deque<ShotRequest>& work);
    void ApplyItemEffect(Instance& instance, const ShotRequest& request,
                         const World::HitResult& hit, std::deque<ShotRequest>& work);

    PlayerState* FindPlayer(SessionId sessionId);
    PlayerState* FindSlot(uint8 slot);
    /// Any player still connected, including the shooter — a pan to the face is
    /// funnier than a pan that politely avoids you.
    PlayerState* PickRandomPlayer();

    std::vector<proto::PlayerScore> BuildScores() const;
    void SendTo(Instance& instance, SessionId sessionId, const SendBufferRef& buffer) const;
    void BroadcastStatus(Instance& instance, const PlayerState& player);
    void RefreshAmmo(Instance& instance, PlayerState& player);

    uint32 MsToTicks(uint32 ms) const {
        return _tickIntervalMs == 0 ? ms : std::max<uint32>(1, ms / _tickIntervalMs);
    }

    /// Bound on one trigger pull's fan-out. Duplicate items never spawn in the
    /// same wave, so a grenade cannot find another grenade; this only has to
    /// cover grenade to pan to ricochet.
    static constexpr int32 kMaxShotsPerTrigger = 24;
    static constexpr uint8 kMaxCascadeDepth = 3;
    static constexpr int32 kGrenadeFragments = 6;
    static constexpr float kGrenadeRadius = 150.0f;
    static constexpr float kBalloonRadius = 280.0f;
    static constexpr float kBalloonStrength = 520.0f;
    /// Minimum gap between shots, so an unlimited-ammo client cannot empty the
    /// board in one tick.
    static constexpr uint32 kMinShotIntervalMs = 90;

    const proto::GameMode _mode;
    const proto::MatchConfig _config;
    const proto::GameModeDef* _modeDef = nullptr;
    const uint32 _tickIntervalMs;

    Board _board;
    World _world;
    Rng _rng;

    std::vector<PlayerState> _players;

    Phase _phase = Phase::Idle;
    uint32 _tick = 0;
    uint32 _phaseEndTick = 0;
    uint32 _snapshotAccumMs = 0;

    uint8 _roundIndex = 0;
    uint8 _paperSize = 0;
    uint64 _roundSeed = 0;
    uint16 _waveIndex = 0;
};

}  // namespace nhn::instance
