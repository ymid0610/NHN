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
      _rng(static_cast<uint64>(NowUnixMs()) * 0x9E3779B97F4A7C15ull),
      _botRng(static_cast<uint64>(NowUnixMs()) * 0xD1B54A32D192ED03ull + 1) {}

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
        player.botDifficulty = member.botDifficulty;
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
        // Carried through so the in-game scoreboard can label a bot and show
        // its level; without it every opponent looks like a person.
        info.botDifficulty = member.botDifficulty;
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
        if (player.IsBot()) {
            // Spread the opening shots out; four bots firing on the same tick
            // reads as a single volley rather than four opponents.
            player.botNextFireTick =
                _tick + MsToTicks(static_cast<uint32>(_botRng.Range(150, 900)));
        }
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

            BotTick(instance);

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

    FireFor(instance, *player, static_cast<float>(packet.aimX),
            static_cast<float>(packet.aimY), tick, packet.sequence);
}

void ShootingGameMode::FireFor(Instance& instance, PlayerState& player, float x, float y,
                               uint32 tick, uint16 sequence) {
    player.lastShotTick = _tick;
    if (_config.ammoPerWave != kUnlimited) {
        --player.ammo;
        S_AmmoChanged ammo;
        ammo.slot = player.slot;
        ammo.ammo = player.ammo;
        instance.Broadcast(MakePacket(ammo));
    }

    ShotRequest request;
    request.shooter = player.sessionId;
    request.slot = player.slot;
    request.x = x;
    request.y = y;
    request.tick = tick;
    request.sequence = sequence;
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
// Bots
//
// A bot is a player the server aims for. Its shots go through FireFor exactly
// as a person's do - same ammo, same rate limit, same hit resolution - so
// difficulty can only change *where* it aims and *how often*, never what a hit
// is worth. That is deliberate: a bot that cheated would be impossible to tune
// against, and impossible to trust when a player loses to one.
// ---------------------------------------------------------------------------

ShootingGameMode::BotProfile ShootingGameMode::ProfileFor(uint8 difficulty) {
    switch (std::clamp(difficulty, kMinBotDifficulty, kMaxBotDifficulty)) {
        // Scatters across a couple of cells and never reads the board.
        case 1:  return {85.0f, 1300, 0.00f, false};
        case 2:  return {55.0f, 1050, 0.15f, false};
        // From here it plays the board rather than just the paper.
        case 3:  return {34.0f,  820, 0.45f, true};
        case 4:  return {19.0f,  640, 0.70f, true};
        default: return { 8.0f,  480, 0.90f, true};
    }
}

float ShootingGameMode::BotJitter(float sigma) {
    // Three uniforms averaged: close enough to a bell curve for aim scatter,
    // and without the rare extreme outliers a true normal produces - a level 5
    // bot occasionally shooting the far corner would read as a bug.
    const float sum = _botRng.Unit() + _botRng.Unit() + _botRng.Unit() - 1.5f;
    return sum * 2.0f * sigma;
}

int32 ShootingGameMode::ScoreItem(const PlayerState& bot, ItemKind item) const {
    switch (item) {
        case ItemKind::SpeedLoader:
            // Worth everything when dry and nothing when full: the refill is an
            // assignment, not an addition.
            return bot.ammo == 0 ? 100 : bot.ammo <= 1 ? 60 : 5;
        case ItemKind::Grenade:
            return 55;  // Fragments claim several cells from one bullet.
        case ItemKind::PaintCan:
            return 35;  // Blinds somebody for two passes.
        case ItemKind::FryingPan:
            return 30;  // Might rebound onto the bot itself.
        case ItemKind::Balloon:
            return 15;
        // Shooting either of these only spends a bullet.
        case ItemKind::Tumbleweed:
        case ItemKind::WantedPoster:
        default:
            return -1;
    }
}

namespace {

/// Consecutive cells owned by @p slot walking away from (x, y).
int32 RunFrom(const Board& board, int32 x, int32 y, int32 dx, int32 dy, uint8 slot) {
    int32 count = 0;
    for (;;) {
        x += dx;
        y += dy;
        if (x < 0 || y < 0 || x >= board.Width() || y >= board.Height()) {
            return count;
        }
        if (board.OwnerAt(board.CellIndex(static_cast<uint8>(x), static_cast<uint8>(y))) != slot) {
            return count;
        }
        ++count;
    }
}

}  // namespace

int32 ShootingGameMode::ScoreCell(const PlayerState& bot, int32 cellX, int32 cellY) const {
    static constexpr int32 kAxes[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};
    const int32 winLength = _modeDef != nullptr ? _modeDef->winLength : 5;

    int32 bestOwn = 0;
    int32 bestBlock = 0;

    for (const auto& axis : kAxes) {
        const int32 own = 1 + RunFrom(_board, cellX, cellY, axis[0], axis[1], bot.slot) +
                          RunFrom(_board, cellX, cellY, -axis[0], -axis[1], bot.slot);
        bestOwn = std::max(bestOwn, own);

        for (const PlayerState& other : _players) {
            if (other.slot == bot.slot) {
                continue;
            }
            const int32 theirs = 1 + RunFrom(_board, cellX, cellY, axis[0], axis[1], other.slot) +
                                 RunFrom(_board, cellX, cellY, -axis[0], -axis[1], other.slot);
            bestBlock = std::max(bestBlock, theirs);
        }
    }

    if (bestOwn >= winLength) {
        return 1000;  // Takes the round outright.
    }
    if (bestBlock >= winLength) {
        return 900;  // Somebody else takes it next shot unless this is denied.
    }

    // Otherwise build, leaning on denial slightly less than on progress.
    return bestOwn * 8 + bestBlock * 5;
}

bool ShootingGameMode::ChooseBotTarget(const PlayerState& bot, const BotProfile& profile,
                                       const std::vector<EntityState>& view, float& outX,
                                       float& outY) {
    const EntityState* sheet = nullptr;
    for (const EntityState& entity : view) {
        if (entity.kind == EntityKind::TargetSheet) {
            sheet = &entity;
            break;
        }
    }
    if (sheet == nullptr || sheet->halfWidth <= 0) {
        return false;  // Between waves, or the sheet has gone.
    }

    // Items first, for the difficulties that bother with them.
    if (profile.itemChance > 0.0f) {
        const EntityState* best = nullptr;
        int32 bestScore = 0;
        for (const EntityState& entity : view) {
            if (entity.kind != EntityKind::Item) {
                continue;
            }
            const int32 score = ScoreItem(bot, entity.item);
            if (score > bestScore) {
                bestScore = score;
                best = &entity;
            }
        }

        // A dry bot goes for a speed loader whatever its temperament; anything
        // less urgent is a roll against the profile.
        const bool urgent = best != nullptr && bestScore >= 100;
        if (best != nullptr && (urgent || _botRng.Unit() < profile.itemChance)) {
            outX = static_cast<float>(best->x);
            outY = static_cast<float>(best->y);
            return true;
        }
    }

    const int32 boardW = _board.Width();
    const int32 boardH = _board.Height();
    if (boardW <= 0 || boardH <= 0) {
        return false;
    }

    const float cellSize = static_cast<float>(sheet->halfWidth) * 2.0f / static_cast<float>(boardW);
    const float left = static_cast<float>(sheet->x - sheet->halfWidth);
    const float top = static_cast<float>(sheet->y - sheet->halfHeight);

    int32 chosenX = -1;
    int32 chosenY = -1;

    if (profile.playsPositionally) {
        int32 bestScore = -1;
        for (int32 y = 0; y < boardH; ++y) {
            for (int32 x = 0; x < boardW; ++x) {
                const uint16 index =
                    _board.CellIndex(static_cast<uint8>(x), static_cast<uint8>(y));
                if (!_board.IsEmpty(index)) {
                    continue;
                }
                // A small nudge breaks ties without changing rank, so two bots
                // of the same level do not play an identical game.
                const int32 score = ScoreCell(bot, x, y) * 4 + _botRng.Range(0, 3);
                if (score > bestScore) {
                    bestScore = score;
                    chosenX = x;
                    chosenY = y;
                }
            }
        }
    } else {
        // Low difficulty does not read the board: it picks somewhere empty and
        // hopes.
        std::vector<uint16> empties;
        empties.reserve(static_cast<size_t>(boardW) * static_cast<size_t>(boardH));
        for (uint16 index = 0; index < _board.CellCount(); ++index) {
            if (_board.IsEmpty(index)) {
                empties.push_back(index);
            }
        }
        if (!empties.empty()) {
            const uint16 pick = empties[static_cast<size_t>(
                _botRng.Range(0, static_cast<int32>(empties.size()) - 1))];
            chosenX = pick % boardW;
            chosenY = pick / boardW;
        }
    }

    if (chosenX < 0) {
        return false;  // Board full; the round is about to end anyway.
    }

    // Cell centre in field space. Cell (0,0) is the sheet's top-left and y grows
    // downward, matching World::Trace.
    outX = left + (static_cast<float>(chosenX) + 0.5f) * cellSize;
    outY = top + (static_cast<float>(chosenY) + 0.5f) * cellSize;
    return true;
}

void ShootingGameMode::BotTick(Instance& instance) {
    // Built once and shared: every bot sees the frame a player would.
    std::vector<EntityState> view;
    bool viewBuilt = false;

    for (PlayerState& player : _players) {
        if (!player.IsBot() || !player.present || player.stunned) {
            continue;
        }
        if (_tick < player.botNextFireTick) {
            continue;
        }
        if (_config.ammoPerWave != kUnlimited && player.ammo == 0) {
            continue;
        }

        if (!viewBuilt) {
            view = _world.BuildSnapshot();
            viewBuilt = true;
        }

        const BotProfile profile = ProfileFor(player.botDifficulty);
        float x = 0.0f;
        float y = 0.0f;
        if (!ChooseBotTarget(player, profile, view, x, y)) {
            // Nothing to shoot at - look again shortly rather than every tick.
            player.botNextFireTick = _tick + MsToTicks(120);
            continue;
        }

        x += BotJitter(profile.aimSigma);
        y += BotJitter(profile.aimSigma);

        // A wild miss should cost the bot its bullet exactly as it would a
        // player's, so clamp into the field rather than skipping the shot.
        x = std::clamp(x, 0.0f, static_cast<float>(kFieldWidth - 1));
        y = std::clamp(y, 0.0f, static_cast<float>(kFieldHeight - 1));

        player.botNextFireTick = _tick + MsToTicks(profile.fireIntervalMs);
        FireFor(instance, player, x, y, _tick, 0);

        // A completed line ends the round; the rest of the table stops here.
        if (_phase != Phase::WaveActive) {
            return;
        }
    }
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
