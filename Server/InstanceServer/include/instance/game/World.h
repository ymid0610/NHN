#pragma once

#include <array>
#include <vector>

#include "core/Types.h"
#include "instance/game/Board.h"
#include "protocol/GameTypes.h"

namespace nhn::instance {

/// Deterministic per-round randomness. Seeded from the round seed so a replay
/// or a bug report can reproduce exactly the same wave.
class Rng {
public:
    explicit Rng(uint64 seed) : _state(seed != 0 ? seed : 0x9E3779B97F4A7C15ull) {}

    uint64 Next() {
        // xorshift64*: small, fast, and good enough for spawn placement.
        _state ^= _state >> 12;
        _state ^= _state << 25;
        _state ^= _state >> 27;
        return _state * 0x2545F4914F6CDD1Dull;
    }

    /// Uniform in [minInclusive, maxInclusive].
    int32 Range(int32 minInclusive, int32 maxInclusive) {
        if (minInclusive >= maxInclusive) {
            return minInclusive;
        }
        const uint64 span = static_cast<uint64>(maxInclusive - minInclusive) + 1;
        return minInclusive + static_cast<int32>(Next() % span);
    }

    float Unit() { return static_cast<float>(Next() >> 40) / 16777216.0f; }
    bool Chance() { return (Next() & 1) != 0; }

private:
    uint64 _state;
};

struct Entity {
    uint32 id = 0;
    proto::EntityKind kind = proto::EntityKind::None;
    proto::ItemKind item = proto::ItemKind::None;

    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;  // units per second; items only
    float vy = 0.0f;
    float halfW = 0.0f;
    float halfH = 0.0f;

    /// Items appear partway through a wave rather than all at once.
    uint32 spawnTick = 0;
    bool active = false;
    bool alive = false;
};

/// Everything flying during one wave, plus the short history needed to judge a
/// shot at the moment the shooter saw it.
///
/// Three containers rather than one sorted list: hit-test order is then a
/// property of the code's structure, and it is impossible to place a target in
/// front of a blocker by getting a number wrong.
class World {
public:
    struct HitResult {
        proto::HitKind kind = proto::HitKind::Miss;
        uint32 entityId = 0;
        proto::ItemKind item = proto::ItemKind::None;
        uint16 cellIndex = Board::kNoCell;
    };

    /// @param paperSize 1..5, the cell size for this round
    void BeginWave(uint64 waveSeed, const Board& board, uint8 paperSize, uint32 startTick,
                   uint32 durationTicks, uint32 itemCount, uint32 itemMask);
    void EndWave();

    void Step(uint32 tick, float deltaSeconds);

    /// Point test at @p tick, walking blockers, then items, then the sheet.
    HitResult Trace(float x, float y, uint32 tick, const Board& board) const;

    /// Shoves entities within @p radius away from the pop, and marks the
    /// balloon itself dead.
    void ApplyBlast(uint32 sourceEntityId, float radius, float strength);

    void Kill(uint32 entityId);
    const Entity* Find(uint32 entityId) const;

    std::vector<proto::EntityState> BuildSnapshot() const;

    float SheetHalfWidth() const { return _sheetHalfW; }
    float SheetHalfHeight() const { return _sheetHalfH; }
    uint32 SheetEntityId() const { return _sheet.id; }

private:
    struct HistoryEntry {
        uint32 id = 0;
        float x = 0.0f;
        float y = 0.0f;
        float halfW = 0.0f;
        float halfH = 0.0f;
    };
    struct Snapshot {
        uint32 tick = 0;
        bool valid = false;
        std::vector<HistoryEntry> entries;
    };

    void RecordSnapshot(uint32 tick);
    const Snapshot* FindSnapshot(uint32 tick) const;
    /// Position of @p entityId at @p tick, falling back to the live position
    /// when the tick is outside the retained window.
    bool ResolveAt(uint32 entityId, uint32 tick, HistoryEntry& out) const;

    /// Serpentine descent: alternating horizontal sweeps at falling heights.
    void UpdateSheetPath(uint32 tick);

    static bool PointInside(float px, float py, const HistoryEntry& entry) {
        return px >= entry.x - entry.halfW && px <= entry.x + entry.halfW &&
               py >= entry.y - entry.halfH && py <= entry.y + entry.halfH;
    }

    /// 300 ms of history at the 20 ms simulation tick.
    static constexpr size_t kSnapshotCount = 16;

    Entity _sheet;
    std::vector<Entity> _blockers;
    std::vector<Entity> _items;
    std::vector<Entity> _decoys;

    /// Blast displacement applied on top of the scripted path, decaying back to
    /// zero so a shove is felt without the sheet leaving its route.
    float _sheetOffsetX = 0.0f;
    float _sheetOffsetY = 0.0f;

    float _sheetHalfW = 0.0f;
    float _sheetHalfH = 0.0f;
    float _cellSize = 0.0f;
    uint8 _boardW = 0;
    uint8 _boardH = 0;

    uint32 _startTick = 0;
    uint32 _durationTicks = 1;
    uint32 _nextEntityId = 1;
    int32 _sweepCount = 3;
    float _marginX = 0.0f;

    std::array<Snapshot, kSnapshotCount> _history{};
    size_t _historyCursor = 0;
};

}  // namespace nhn::instance
