#include "instance/game/World.h"

#include <algorithm>
#include <cmath>

namespace nhn::instance {

using namespace proto;

namespace {

/// Cell size in field units for paper sizes 1..5. Difficulty is the size of a
/// cell, so it means the same thing whatever the board dimensions are.
float CellSizeFor(uint8 paperSize) {
    const uint8 clamped = std::clamp(paperSize, kMinPaperSize, kMaxPaperSize);
    return 24.0f + static_cast<float>(clamped) * 8.0f;  // 32 .. 64
}

constexpr float kGravity = 900.0f;        // units per second squared
constexpr float kItemHalfExtent = 26.0f;
constexpr float kBlockerHalfW = 46.0f;
constexpr float kBlockerHalfH = 46.0f;
constexpr float kDecoyScale = 0.6f;       // decoys are smaller than the real sheet
constexpr float kOffsetDecayPerSecond = 3.0f;

}  // namespace

void World::BeginWave(uint64 waveSeed, const Board& board, uint8 paperSize, uint32 startTick,
                      uint32 durationTicks, uint32 itemCount, uint32 itemMask) {
    Rng rng(waveSeed);

    _blockers.clear();
    _items.clear();
    _decoys.clear();
    _history.fill(Snapshot{});
    _historyCursor = 0;

    _startTick = startTick;
    _durationTicks = std::max<uint32>(durationTicks, 1);
    _sheetOffsetX = 0.0f;
    _sheetOffsetY = 0.0f;

    _boardW = board.Width();
    _boardH = board.Height();
    _cellSize = CellSizeFor(paperSize);
    _sheetHalfW = _boardW * _cellSize * 0.5f;
    _sheetHalfH = _boardH * _cellSize * 0.5f;

    // Route varies a little per wave so the pass is not identical every time.
    _sweepCount = rng.Range(2, 4);
    _marginX = _sheetHalfW + 40.0f;

    _sheet = Entity{};
    _sheet.id = _nextEntityId++;
    _sheet.kind = EntityKind::TargetSheet;
    _sheet.halfW = _sheetHalfW;
    _sheet.halfH = _sheetHalfH;
    _sheet.active = true;
    _sheet.alive = true;
    UpdateSheetPath(startTick);

    // Item kinds are drawn without replacement, so a wave never contains two of
    // the same thing.
    std::vector<ItemKind> pool;
    for (uint8 raw = 1; raw < static_cast<uint8>(ItemKind::Count); ++raw) {
        const auto kind = static_cast<ItemKind>(raw);
        if ((itemMask & ItemBit(kind)) != 0) {
            pool.push_back(kind);
        }
    }
    for (size_t i = pool.size(); i > 1; --i) {
        std::swap(pool[i - 1], pool[static_cast<size_t>(rng.Range(0, static_cast<int32>(i) - 1))]);
    }

    const size_t spawnCount = std::min<size_t>(itemCount, pool.size());
    for (size_t i = 0; i < spawnCount; ++i) {
        const ItemKind kind = pool[i];
        const ItemDef* def = FindItem(kind);
        if (def == nullptr) {
            continue;
        }

        Entity entity;
        entity.id = _nextEntityId++;
        entity.item = kind;
        entity.alive = true;
        entity.active = false;

        // Spread arrivals across the pass rather than all at once.
        const uint32 window = _durationTicks * 3 / 4;
        entity.spawnTick =
            startTick + static_cast<uint32>(rng.Range(0, static_cast<int32>(window)));

        if (def->layer == HitLayer::Blocker) {
            // The tumbleweed sits in front and drifts across rather than
            // arcing, so it reliably occludes part of the field.
            entity.kind = EntityKind::Item;
            entity.halfW = kBlockerHalfW;
            entity.halfH = kBlockerHalfH;
            const bool fromLeft = rng.Chance();
            entity.x = fromLeft ? -kBlockerHalfW : kFieldWidth + kBlockerHalfW;
            entity.y = static_cast<float>(rng.Range(kFieldHeight / 4, kFieldHeight * 3 / 4));
            entity.vx = (fromLeft ? 1.0f : -1.0f) * static_cast<float>(rng.Range(280, 420));
            entity.vy = 0.0f;
            _blockers.push_back(entity);
            continue;
        }

        if (def->layer == HitLayer::Target) {
            // A decoy is a target, so it belongs behind the items — but in
            // front of nothing, which is what makes it a convincing mistake.
            entity.kind = EntityKind::Item;
            entity.halfW = _sheetHalfW * kDecoyScale;
            entity.halfH = _sheetHalfH * kDecoyScale;
            const bool fromLeft = rng.Chance();
            entity.x = fromLeft ? -entity.halfW : kFieldWidth + entity.halfW;
            entity.y = static_cast<float>(rng.Range(kFieldHeight / 5, kFieldHeight * 2 / 3));
            entity.vx = (fromLeft ? 1.0f : -1.0f) * static_cast<float>(rng.Range(260, 380));
            entity.vy = -static_cast<float>(rng.Range(120, 260));
            _decoys.push_back(entity);
            continue;
        }

        entity.kind = EntityKind::Item;
        entity.halfW = kItemHalfExtent;
        entity.halfH = kItemHalfExtent;
        const bool fromLeft = rng.Chance();
        entity.x = fromLeft ? -kItemHalfExtent : kFieldWidth + kItemHalfExtent;
        entity.y = static_cast<float>(rng.Range(kFieldHeight / 2, kFieldHeight * 4 / 5));
        entity.vx = (fromLeft ? 1.0f : -1.0f) * static_cast<float>(rng.Range(320, 520));
        // Upward kick, then gravity: the parabola the design calls for.
        entity.vy = -static_cast<float>(rng.Range(420, 640));
        _items.push_back(entity);
    }
}

void World::EndWave() {
    _blockers.clear();
    _items.clear();
    _decoys.clear();
    _sheet.alive = false;
    _sheet.active = false;
}

void World::UpdateSheetPath(uint32 tick) {
    const float progress =
        std::clamp(static_cast<float>(tick - _startTick) / static_cast<float>(_durationTicks),
                   0.0f, 1.0f);

    // Descends steadily while sweeping side to side: entering above the field
    // and leaving below it.
    const float yTop = -_sheetHalfH;
    const float yBottom = kFieldHeight + _sheetHalfH;
    const float pathY = yTop + (yBottom - yTop) * progress;

    const float sweeps = static_cast<float>(_sweepCount);
    const float scaled = progress * sweeps;
    const auto sweepIndex = static_cast<int32>(scaled);
    const float local = scaled - static_cast<float>(sweepIndex);
    const float across = (sweepIndex % 2 == 0) ? local : 1.0f - local;

    const float left = _marginX;
    const float right = static_cast<float>(kFieldWidth) - _marginX;
    const float pathX = left + (right - left) * across;

    _sheet.x = pathX + _sheetOffsetX;
    _sheet.y = pathY + _sheetOffsetY;
}

void World::Step(uint32 tick, float deltaSeconds) {
    // Blast displacement bleeds off, so a shove is felt without the sheet
    // permanently abandoning its route.
    const float decay = std::max(0.0f, 1.0f - kOffsetDecayPerSecond * deltaSeconds);
    _sheetOffsetX *= decay;
    _sheetOffsetY *= decay;
    UpdateSheetPath(tick);

    auto integrate = [&](std::vector<Entity>& list, bool gravity) {
        for (Entity& entity : list) {
            if (!entity.alive) {
                continue;
            }
            if (!entity.active) {
                if (tick < entity.spawnTick) {
                    continue;
                }
                entity.active = true;
            }
            if (gravity) {
                entity.vy += kGravity * deltaSeconds;
            }
            entity.x += entity.vx * deltaSeconds;
            entity.y += entity.vy * deltaSeconds;

            // Well outside the field it can never come back.
            if (entity.y > kFieldHeight + 400.0f || entity.x < -600.0f ||
                entity.x > kFieldWidth + 600.0f) {
                entity.alive = false;
            }
        }
    };

    integrate(_blockers, false);
    integrate(_items, true);
    integrate(_decoys, true);

    RecordSnapshot(tick);
}

void World::RecordSnapshot(uint32 tick) {
    Snapshot& slot = _history[_historyCursor];
    _historyCursor = (_historyCursor + 1) % kSnapshotCount;

    slot.tick = tick;
    slot.valid = true;
    slot.entries.clear();

    auto record = [&slot](const Entity& entity) {
        if (entity.alive && entity.active) {
            slot.entries.push_back(HistoryEntry{entity.id, entity.x, entity.y, entity.halfW,
                                                entity.halfH});
        }
    };

    record(_sheet);
    for (const Entity& entity : _blockers) record(entity);
    for (const Entity& entity : _items) record(entity);
    for (const Entity& entity : _decoys) record(entity);
}

const World::Snapshot* World::FindSnapshot(uint32 tick) const {
    // Nearest retained tick at or before the requested one. The caller has
    // already clamped how far back a shot may reach.
    const Snapshot* best = nullptr;
    for (const Snapshot& snapshot : _history) {
        if (!snapshot.valid || snapshot.tick > tick) {
            continue;
        }
        if (best == nullptr || snapshot.tick > best->tick) {
            best = &snapshot;
        }
    }
    return best;
}

bool World::ResolveAt(uint32 entityId, uint32 tick, HistoryEntry& out) const {
    if (const Snapshot* snapshot = FindSnapshot(tick)) {
        for (const HistoryEntry& entry : snapshot->entries) {
            if (entry.id == entityId) {
                out = entry;
                return true;
            }
        }
        // Present now but absent then: it had not spawned yet, so it cannot
        // have been hit.
        return false;
    }

    if (const Entity* live = Find(entityId)) {
        out = HistoryEntry{live->id, live->x, live->y, live->halfW, live->halfH};
        return live->alive && live->active;
    }
    return false;
}

World::HitResult World::Trace(float x, float y, uint32 tick, const Board& board) const {
    HitResult result;

    auto testList = [&](const std::vector<Entity>& list, HitKind kind) -> bool {
        for (const Entity& entity : list) {
            if (!entity.alive) {
                continue;
            }
            HistoryEntry entry;
            if (!ResolveAt(entity.id, tick, entry)) {
                continue;
            }
            if (PointInside(x, y, entry)) {
                result.kind = kind;
                result.entityId = entity.id;
                result.item = entity.item;
                return true;
            }
        }
        return false;
    };

    // Order is the rule. A blocker absorbs the shot outright.
    if (testList(_blockers, HitKind::Blocker)) {
        return result;
    }
    if (testList(_items, HitKind::Item)) {
        return result;
    }
    if (testList(_decoys, HitKind::Decoy)) {
        return result;
    }

    // The sheet last, and only then work out which cell.
    HistoryEntry sheet;
    if (_sheet.alive && ResolveAt(_sheet.id, tick, sheet) && PointInside(x, y, sheet)) {
        const float localX = x - (sheet.x - sheet.halfW);
        const float localY = y - (sheet.y - sheet.halfH);
        const auto cellX = static_cast<int32>(localX / _cellSize);
        const auto cellY = static_cast<int32>(localY / _cellSize);

        if (cellX >= 0 && cellY >= 0 && cellX < _boardW && cellY < _boardH) {
            result.entityId = _sheet.id;
            result.cellIndex = board.CellIndex(static_cast<uint8>(cellX),
                                               static_cast<uint8>(cellY));
            result.kind = board.IsEmpty(result.cellIndex) ? HitKind::Cell : HitKind::CellTaken;
            return result;
        }
    }

    return result;
}

void World::ApplyBlast(uint32 sourceEntityId, float radius, float strength) {
    const Entity* source = Find(sourceEntityId);
    if (source == nullptr) {
        return;
    }
    const float originX = source->x;
    const float originY = source->y;

    auto shove = [&](std::vector<Entity>& list) {
        for (Entity& entity : list) {
            if (!entity.alive || !entity.active || entity.id == sourceEntityId) {
                continue;
            }
            const float dx = entity.x - originX;
            const float dy = entity.y - originY;
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance > radius || distance < 0.001f) {
                continue;
            }
            const float falloff = 1.0f - distance / radius;
            entity.vx += (dx / distance) * strength * falloff;
            entity.vy += (dy / distance) * strength * falloff;
        }
    };

    shove(_blockers);
    shove(_items);
    shove(_decoys);

    // The sheet is nudged rather than accelerated: its motion is a scripted
    // path, so a velocity change would send it off route permanently.
    const float dx = _sheet.x - originX;
    const float dy = _sheet.y - originY;
    const float distance = std::sqrt(dx * dx + dy * dy);
    if (distance <= radius && distance > 0.001f) {
        const float falloff = 1.0f - distance / radius;
        _sheetOffsetX += (dx / distance) * strength * falloff * 0.25f;
        _sheetOffsetY += (dy / distance) * strength * falloff * 0.25f;
    }
}

void World::Kill(uint32 entityId) {
    auto killIn = [entityId](std::vector<Entity>& list) {
        for (Entity& entity : list) {
            if (entity.id == entityId) {
                entity.alive = false;
                return true;
            }
        }
        return false;
    };
    if (killIn(_blockers)) return;
    if (killIn(_items)) return;
    killIn(_decoys);
}

const Entity* World::Find(uint32 entityId) const {
    if (_sheet.id == entityId) {
        return &_sheet;
    }
    for (const auto* list : {&_blockers, &_items, &_decoys}) {
        for (const Entity& entity : *list) {
            if (entity.id == entityId) {
                return &entity;
            }
        }
    }
    return nullptr;
}

std::vector<EntityState> World::BuildSnapshot() const {
    std::vector<EntityState> result;
    result.reserve(1 + _blockers.size() + _items.size() + _decoys.size());

    auto append = [&result](const Entity& entity) {
        if (!entity.alive || !entity.active) {
            return;
        }
        EntityState state;
        state.entityId = entity.id;
        state.kind = entity.kind;
        state.item = entity.item;
        state.x = static_cast<int32>(entity.x);
        state.y = static_cast<int32>(entity.y);
        state.halfWidth = static_cast<int32>(entity.halfW);
        state.halfHeight = static_cast<int32>(entity.halfH);
        result.push_back(state);
    };

    append(_sheet);
    for (const Entity& entity : _blockers) append(entity);
    for (const Entity& entity : _items) append(entity);
    for (const Entity& entity : _decoys) append(entity);
    return result;
}

}  // namespace nhn::instance
