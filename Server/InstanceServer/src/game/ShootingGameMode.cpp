#include "instance/game/ShootingGameMode.h"

#include <algorithm>
#include <cmath>

// Instance.h only forward-declares InstanceSession; sending to one player needs
// the definition.
#include "instance/InstanceServer.h"
#include "protocol/Dispatcher.h"

namespace nhn::instance {

using namespace proto;

namespace {

/// How often replicated transforms go out. Coarser than the simulation because
/// clients interpolate; the simulation stays fine-grained so rewound hit tests
/// are accurate.
constexpr uint32 kSnapshotIntervalMs = 50;

/// Items per wave. Tunable, and capped by how many distinct kinds are enabled —
/// a wave never contains two of the same thing.
constexpr uint32 kItemsPerWaveMin = 4;
constexpr uint32 kItemsPerWaveMax = 5;

}  // namespace

ShootingGameMode::ShootingGameMode(GameMode mode, MatchConfig config, uint32 tickIntervalMs)
    : _mode(mode),
      _config(config),
      _modeDef(FindGameMode(mode)),
      _tickIntervalMs(tickIntervalMs == 0 ? 20 : tickIntervalMs),
      _rng(static_cast<uint64>(NowUnixMs()) * 0x9E3779B97F4A7C15ull) {}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void ShootingGameMode::OnStart(Instance& instance) {
    if (_modeDef == nullptr) {
        LOG_ERROR("instance {}: unknown game mode", instance.GetId());
        instance.EnqueueClose("unknown game mode");
        return;
    }

    _players.clear();
    for (const Instance::Member& member : instance.GetMembers()) {
        PlayerState player;
        player.sessionId = member.sessionId;
        player.slot = member.slot;
        player.present = member.connected;
        _players.push_back(player);
    }

    S_MatchSetup setup;
    setup.mode = _mode;
    setup.config = _config;
    setup.board.width = _modeDef->boardWidth;
    setup.board.height = _modeDef->boardHeight;
    setup.board.winLength = _modeDef->winLength;
    setup.fieldWidth = static_cast<uint32>(kFieldWidth);
    setup.fieldHeight = static_cast<uint32>(kFieldHeight);
    setup.waveDurationMs = kWaveDurationMs;
    for (const Instance::Member& member : instance.GetMembers()) {
        RoomMemberInfo info;
        info.sessionId = member.sessionId;
        info.nickname = member.nickname;
        info.slot = member.slot;
        setup.players.push_back(std::move(info));
    }
    instance.Broadcast(MakePacket(setup));

    LOG_INFO("instance {}: {} on {}x{} ({} in a row), {} rounds, ammo {}, wave cap {}",
             instance.GetId(), _modeDef->name, _modeDef->boardWidth, _modeDef->boardHeight,
             _modeDef->winLength, _config.rounds, _config.ammoPerWave, _config.waveLimit);

    _roundIndex = 0;
    BeginRound(instance);
}

void ShootingGameMode::BeginRound(Instance& instance) {
    _board.Reset(_modeDef->boardWidth, _modeDef->boardHeight, _modeDef->winLength);
    _roundSeed = _rng.Next();
    // Paper size is drawn per round from the configured band, so difficulty
    // moves around within a match.
    _paperSize = static_cast<uint8>(
        Rng(_roundSeed).Range(_config.paperSizeMin, _config.paperSizeMax));
    _waveIndex = 0;

    S_RoundStart packet;
    packet.roundIndex = _roundIndex;
    packet.roundCount = _config.rounds;
    packet.paperSize = _paperSize;
    packet.roundSeed = _roundSeed;
    instance.Broadcast(MakePacket(packet));

    LOG_INFO("instance {}: round {}/{} begins, paper size {}", instance.GetId(), _roundIndex + 1,
             _config.rounds, _paperSize);

    BeginWave(instance);
}

void ShootingGameMode::BeginWave(Instance& instance) {
    for (PlayerState& player : _players) {
        // Effects expire by wave index rather than by time, so they line up
        // with the pass they were meant to spoil.
        if (player.stunned && _waveIndex > player.stunThroughWave) {
            player.stunned = false;
            BroadcastStatus(instance, player);
        }
        if (player.blinded && _waveIndex > player.blindThroughWave) {
            player.blinded = false;
            BroadcastStatus(instance, player);
        }
        RefreshAmmo(instance, player);
    }

    const uint32 durationTicks = MsToTicks(kWaveDurationMs);
    const uint32 itemCount = static_cast<uint32>(
        _rng.Range(static_cast<int32>(kItemsPerWaveMin), static_cast<int32>(kItemsPerWaveMax)));

    _world.BeginWave(_roundSeed ^ (static_cast<uint64>(_waveIndex) * 0xA24BAED4963EE407ull),
                     _board, _paperSize, _tick, durationTicks, itemCount, _config.itemMask);

    _phase = Phase::WaveActive;
    _phaseEndTick = _tick + durationTicks;

    S_WaveStart packet;
    packet.waveIndex = _waveIndex;
    packet.startTick = _tick;
    packet.endTick = _phaseEndTick;
    packet.ammo = _config.ammoPerWave;
    instance.Broadcast(MakePacket(packet));
}

void ShootingGameMode::EndWave(Instance& instance) {
    _world.EndWave();

    S_WaveEnd packet;
    packet.waveIndex = _waveIndex;
    instance.Broadcast(MakePacket(packet));

    // A full board can never produce a line, so the round is over either way.
    const bool boardExhausted = _board.IsFull();
    const uint16 nextWave = static_cast<uint16>(_waveIndex + 1);
    const bool capReached =
        (_config.waveLimit != kUnlimited && nextWave >= _config.waveLimit) ||
        nextWave >= kAbsoluteWaveCap;

    if (boardExhausted || capReached) {
        // Nobody completed a line: the round is scoreless, by design.
        EndRound(instance, Board::kNoOwner);
        return;
    }

    _waveIndex = nextWave;
    _phase = Phase::WaveIntermission;
    _phaseEndTick = _tick + MsToTicks(kWaveIntermissionMs);
}

void ShootingGameMode::EndRound(Instance& instance, uint8 winnerSlot) {
    // A completed line ends the round part-way through a pass. Clients have to
    // be told the wave is over, or they keep aiming at a sheet that is gone and
    // collect rejections.
    if (_phase == Phase::WaveActive) {
        S_WaveEnd waveEnd;
        waveEnd.waveIndex = _waveIndex;
        instance.Broadcast(MakePacket(waveEnd));
    }

    _world.EndWave();

    if (winnerSlot != Board::kNoOwner) {
        if (PlayerState* winner = FindSlot(winnerSlot)) {
            ++winner->score;
        }
    }

    S_RoundEnd packet;
    packet.roundIndex = _roundIndex;
    packet.winnerSlot = winnerSlot;
    packet.scores = BuildScores();
    instance.Broadcast(MakePacket(packet));

    LOG_INFO("instance {}: round {} ends — {}", instance.GetId(), _roundIndex + 1,
             winnerSlot == Board::kNoOwner ? "no line completed"
                                           : "slot " + std::to_string(winnerSlot));

    _phase = Phase::RoundIntermission;
    _phaseEndTick = _tick + MsToTicks(kRoundIntermissionMs);
}

void ShootingGameMode::Finish(Instance& instance) {
    _phase = Phase::Finished;

    uint8 best = 0;
    for (const PlayerState& player : _players) {
        best = std::max(best, player.score);
    }

    S_GameOver packet;
    packet.scores = BuildScores();
    for (const PlayerState& player : _players) {
        // Scoreless rounds make a draw reachable even with an odd round count,
        // so the winner list is plural.
        if (player.score == best) {
            packet.winnerSlots.push_back(player.slot);
        }
    }
    instance.Broadcast(MakePacket(packet));

    LOG_INFO("instance {}: match complete, top score {}", instance.GetId(), best);
    instance.EnqueueClose("match complete");
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------

void ShootingGameMode::OnTick(Instance& instance, uint64 deltaMs) {
    ++_tick;

    switch (_phase) {
        case Phase::WaveActive: {
            _world.Step(_tick, static_cast<float>(deltaMs) / 1000.0f);

            _snapshotAccumMs += static_cast<uint32>(deltaMs);
            if (_snapshotAccumMs >= kSnapshotIntervalMs) {
                _snapshotAccumMs = 0;
                S_WorldSnapshot snapshot;
                snapshot.tick = _tick;
                snapshot.entities = _world.BuildSnapshot();
                instance.Broadcast(MakePacket(snapshot));
            }

            if (_tick >= _phaseEndTick) {
                EndWave(instance);
            }
            break;
        }

        case Phase::WaveIntermission:
            if (_tick >= _phaseEndTick) {
                BeginWave(instance);
            }
            break;

        case Phase::RoundIntermission:
            if (_tick >= _phaseEndTick) {
                ++_roundIndex;
                if (_roundIndex >= _config.rounds) {
                    Finish(instance);
                } else {
                    BeginRound(instance);
                }
            }
            break;

        case Phase::Idle:
        case Phase::Finished:
            break;
    }
}

void ShootingGameMode::OnMemberLeft(Instance& instance, SessionId sessionId) {
    if (PlayerState* player = FindPlayer(sessionId)) {
        player->present = false;
    }

    const auto remaining = std::count_if(_players.begin(), _players.end(),
                                         [](const PlayerState& p) { return p.present; });
    if (remaining < _modeDef->minimumToStart && _phase != Phase::Finished) {
        LOG_INFO("instance {}: only {} players left, ending early", instance.GetId(), remaining);
        Finish(instance);
    }
}

void ShootingGameMode::OnEnd(Instance& instance, const std::string& reason) {
    (void)instance;
    (void)reason;
    _phase = Phase::Finished;
    _world.EndWave();
}

// ---------------------------------------------------------------------------
// Firing
// ---------------------------------------------------------------------------

void ShootingGameMode::OnFire(Instance& instance, SessionId sessionId, const C_Fire& packet) {
    PlayerState* player = FindPlayer(sessionId);

    auto reject = [&](ResultCode reason) {
        S_FireRejected rejected;
        rejected.sequence = packet.sequence;
        rejected.result = reason;
        SendTo(instance, sessionId, MakePacket(rejected));
    };

    if (player == nullptr || !player->present) {
        reject(ResultCode::NotAuthenticated);
        return;
    }
    if (_phase != Phase::WaveActive) {
        reject(ResultCode::InvalidRequest);
        return;
    }
    if (player->stunned) {
        reject(ResultCode::InvalidRequest);
        return;
    }
    if (packet.aimX < 0 || packet.aimY < 0 || packet.aimX >= kFieldWidth ||
        packet.aimY >= kFieldHeight) {
        reject(ResultCode::InvalidRequest);
        return;
    }
    if (_config.ammoPerWave != kUnlimited && player->ammo == 0) {
        reject(ResultCode::InvalidRequest);
        return;
    }

    const uint32 minInterval = MsToTicks(kMinShotIntervalMs);
    if (player->lastShotTick != 0 && _tick < player->lastShotTick + minInterval) {
        reject(ResultCode::RateLimited);
        return;
    }

    // A shot may only reach so far back. Clamping rather than rejecting keeps a
    // briefly stalled client playable, while stopping anyone from sitting on a
    // packet to take a cell that was contested several frames ago.
    const uint32 maxRewindTicks = MsToTicks(kMaxRewindMs);
    uint32 tick = packet.clientTick;
    if (tick > _tick) {
        tick = _tick;  // Cannot have seen the future.
    } else if (_tick - tick > maxRewindTicks) {
        tick = _tick - maxRewindTicks;
    }

    player->lastShotTick = _tick;
    if (_config.ammoPerWave != kUnlimited) {
        --player->ammo;
        S_AmmoChanged ammo;
        ammo.slot = player->slot;
        ammo.ammo = player->ammo;
        instance.Broadcast(MakePacket(ammo));
    }

    ShotRequest request;
    request.shooter = sessionId;
    request.slot = player->slot;
    request.x = static_cast<float>(packet.aimX);
    request.y = static_cast<float>(packet.aimY);
    request.tick = tick;
    request.sequence = packet.sequence;
    ProcessShot(instance, request);
}

void ShootingGameMode::ProcessShot(Instance& instance, const ShotRequest& request) {
    std::deque<ShotRequest> work;
    work.push_back(request);

    int32 processed = 0;
    while (!work.empty() && processed < kMaxShotsPerTrigger) {
        const ShotRequest current = work.front();
        work.pop_front();
        ResolveOne(instance, current, work);
        ++processed;

        // A completed line ends the round immediately. Anything still queued is
        // dropped rather than marking a board that has already been decided.
        if (_phase != Phase::WaveActive) {
            break;
        }
    }
}

void ShootingGameMode::ResolveOne(Instance& instance, const ShotRequest& request,
                                  std::deque<ShotRequest>& work) {
    const World::HitResult hit = _world.Trace(request.x, request.y, request.tick, _board);

    S_ShotResolved resolved;
    resolved.shooter = request.shooter;
    resolved.sequence = request.sequence;
    resolved.kind = hit.kind;
    resolved.hitX = static_cast<int32>(request.x);
    resolved.hitY = static_cast<int32>(request.y);
    resolved.entityId = hit.entityId;
    resolved.cellIndex = hit.cellIndex;
    resolved.derived = request.derived;

    bool roundWon = false;

    switch (hit.kind) {
        case HitKind::Cell: {
            // First valid hit takes the cell; that is the entire contest in
            // simultaneous play, and it is decided by arrival order here.
            if (request.slot != Board::kNoOwner && _board.Claim(hit.cellIndex, request.slot)) {
                S_CellClaimed claimed;
                claimed.cellIndex = hit.cellIndex;
                claimed.ownerSlot = request.slot;
                instance.Broadcast(MakePacket(claimed));

                roundWon = _board.CompletesLine(hit.cellIndex, request.slot);
            } else {
                resolved.kind = HitKind::CellTaken;
            }
            break;
        }

        case HitKind::Item:
            ApplyItemEffect(instance, request, hit, work);
            break;

        // A blocker or a decoy costs the shot and nothing more — no penalty,
        // by design.
        case HitKind::Blocker:
        case HitKind::Decoy:
        case HitKind::CellTaken:
        case HitKind::Miss:
            break;
    }

    instance.Broadcast(MakePacket(resolved));

    if (roundWon) {
        EndRound(instance, request.slot);
    }
}

void ShootingGameMode::ApplyItemEffect(Instance& instance, const ShotRequest& request,
                                       const World::HitResult& hit,
                                       std::deque<ShotRequest>& work) {
    const Entity* entity = _world.Find(hit.entityId);
    if (entity == nullptr) {
        return;
    }
    const float itemX = entity->x;
    const float itemY = entity->y;

    S_ItemTriggered triggered;
    triggered.item = hit.item;
    triggered.entityId = hit.entityId;
    triggered.instigator = request.shooter;
    triggered.seed = static_cast<uint32>(_rng.Next());

    switch (hit.item) {
        case ItemKind::FryingPan: {
            _world.Kill(hit.entityId);
            // The victim is drawn on the server: letting clients roll would
            // have each of them believe a different player got hit.
            if (PlayerState* victim = PickRandomPlayer()) {
                victim->stunned = true;
                victim->stunThroughWave = _waveIndex;
                triggered.victimSlot = victim->slot;
                instance.Broadcast(MakePacket(triggered));
                BroadcastStatus(instance, *victim);
                return;
            }
            break;
        }

        case ItemKind::PaintCan: {
            _world.Kill(hit.entityId);
            if (PlayerState* victim = PickRandomPlayer()) {
                victim->blinded = true;
                // Outlasts a stun on purpose: it bleeds into the next pass.
                victim->blindThroughWave = static_cast<uint16>(_waveIndex + 1);
                triggered.victimSlot = victim->slot;
                instance.Broadcast(MakePacket(triggered));
                BroadcastStatus(instance, *victim);
                return;
            }
            break;
        }

        case ItemKind::Balloon:
            _world.Kill(hit.entityId);
            _world.ApplyBlast(hit.entityId, kBalloonRadius, kBalloonStrength);
            break;

        case ItemKind::Grenade: {
            _world.Kill(hit.entityId);
            if (request.depth < kMaxCascadeDepth) {
                // Fragments are ordinary shots. Reusing the pipeline is what
                // keeps "six firing events" from needing its own machinery.
                for (int32 i = 0; i < kGrenadeFragments; ++i) {
                    const float angle =
                        6.28318f * static_cast<float>(i) / static_cast<float>(kGrenadeFragments);
                    ShotRequest fragment;
                    fragment.shooter = request.shooter;
                    fragment.slot = request.slot;
                    fragment.x = itemX + std::cos(angle) * kGrenadeRadius;
                    fragment.y = itemY + std::sin(angle) * kGrenadeRadius;
                    fragment.tick = request.tick;
                    fragment.sequence = request.sequence;
                    fragment.depth = static_cast<uint8>(request.depth + 1);
                    fragment.derived = true;
                    work.push_back(fragment);
                }
            }
            break;
        }

        case ItemKind::SpeedLoader: {
            _world.Kill(hit.entityId);
            // Assignment, not addition: best used when nearly empty, wasted
            // when full.
            if (PlayerState* shooter = FindPlayer(request.shooter)) {
                shooter->ammo = _config.ammoPerWave;
                triggered.victimSlot = shooter->slot;
                S_AmmoChanged ammo;
                ammo.slot = shooter->slot;
                ammo.ammo = shooter->ammo;
                instance.Broadcast(MakePacket(ammo));
            }
            break;
        }

        default:
            _world.Kill(hit.entityId);
            break;
    }

    instance.Broadcast(MakePacket(triggered));
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

ShootingGameMode::PlayerState* ShootingGameMode::FindPlayer(SessionId sessionId) {
    for (PlayerState& player : _players) {
        if (player.sessionId == sessionId) {
            return &player;
        }
    }
    return nullptr;
}

ShootingGameMode::PlayerState* ShootingGameMode::FindSlot(uint8 slot) {
    for (PlayerState& player : _players) {
        if (player.slot == slot) {
            return &player;
        }
    }
    return nullptr;
}

ShootingGameMode::PlayerState* ShootingGameMode::PickRandomPlayer() {
    std::vector<PlayerState*> candidates;
    for (PlayerState& player : _players) {
        if (player.present) {
            candidates.push_back(&player);
        }
    }
    if (candidates.empty()) {
        return nullptr;
    }
    return candidates[static_cast<size_t>(
        _rng.Range(0, static_cast<int32>(candidates.size()) - 1))];
}

std::vector<PlayerScore> ShootingGameMode::BuildScores() const {
    std::vector<PlayerScore> scores;
    scores.reserve(_players.size());
    for (const PlayerState& player : _players) {
        PlayerScore entry;
        entry.sessionId = player.sessionId;
        entry.slot = player.slot;
        entry.score = player.score;
        scores.push_back(entry);
    }
    return scores;
}

void ShootingGameMode::SendTo(Instance& instance, SessionId sessionId,
                              const SendBufferRef& buffer) const {
    if (buffer == nullptr) {
        return;
    }
    for (const Instance::Member& member : instance.GetMembers()) {
        if (member.sessionId == sessionId && member.session != nullptr) {
            member.session->Send(buffer);
            return;
        }
    }
}

void ShootingGameMode::BroadcastStatus(Instance& instance, const PlayerState& player) {
    S_StatusChanged packet;
    packet.slot = player.slot;
    packet.flags = StatusFlags::None;
    if (player.stunned) {
        packet.flags = packet.flags | StatusFlags::Stunned;
    }
    if (player.blinded) {
        packet.flags = packet.flags | StatusFlags::Blinded;
    }
    packet.untilTick = _phaseEndTick;
    instance.Broadcast(MakePacket(packet));
}

void ShootingGameMode::RefreshAmmo(Instance& instance, PlayerState& player) {
    player.ammo = _config.ammoPerWave;
    player.lastShotTick = 0;

    S_AmmoChanged packet;
    packet.slot = player.slot;
    packet.ammo = player.ammo;
    instance.Broadcast(MakePacket(packet));
}

}  // namespace nhn::instance
