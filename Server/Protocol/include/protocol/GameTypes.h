#pragma once

#include <array>

#include "core/Archive.h"
#include "protocol/Types.h"

namespace nhn::proto {

// ---------------------------------------------------------------------------
// Play field
//
// One shared orthographic view: every player sees the same target from the same
// angle, so a screen position means the same thing for everyone and an obstacle
// occludes the same cells regardless of who is shooting. Player avatars are
// presentation only — they never block a shot.
//
// Coordinates on the wire are integers in a fixed 1920x1080 space. The client
// maps its viewport onto that, which keeps aiming resolution-independent
// without needing floats on the wire. The server works in float internally;
// nothing has to be bit-reproducible because the server replicates state rather
// than having clients simulate it.
// ---------------------------------------------------------------------------
inline constexpr int32 kFieldWidth = 1920;
inline constexpr int32 kFieldHeight = 1080;

/// How far back a shot may claim to have happened. Long enough to cover normal
/// play, short enough that nobody can sit on a packet and take a cell that was
/// contested several frames ago.
inline constexpr uint32 kMaxRewindMs = 300;

/// One flight of the target sheet.
inline constexpr uint32 kWaveDurationMs = 4000;
/// Pause between waves within a round.
inline constexpr uint32 kWaveIntermissionMs = 800;
/// Longer pause after a round, so the result can be read.
inline constexpr uint32 kRoundIntermissionMs = 2000;

/// Safety cap for rounds configured with no wave limit. The board is finite so
/// a round terminates on its own, but a table of players who simply never shoot
/// should not hold an instance open forever.
inline constexpr uint32 kAbsoluteWaveCap = 60;

// ---------------------------------------------------------------------------
// Items
// ---------------------------------------------------------------------------

enum class ItemKind : uint8 {
    None = 0,
    Tumbleweed = 1,   // blocks shots, nothing else
    WantedPoster = 2, // decoy target
    FryingPan = 3,    // ricochets, stuns someone
    Balloon = 4,      // pops, shoves nearby entities
    PaintCan = 5,     // pops, blinds someone
    Grenade = 6,      // fragments into further shots
    SpeedLoader = 7,  // refills the shooter's ammo
    Count = 8,
};

/// Which container an entity lives in, and therefore the order it is tested in.
///
/// Storing this as a value in one table — rather than as a sortable number on
/// each entity — is what makes it impossible to accidentally place a target in
/// front of a blocker.
enum class HitLayer : uint8 {
    Blocker = 0,  // tested first, absorbs the shot
    Item = 1,
    Target = 2,  // tested last
};

inline constexpr uint32 ItemBit(ItemKind kind) {
    return 1u << static_cast<uint32>(kind);
}

/// Every item enabled. The lobby starts here and the host turns things off.
inline constexpr uint32 kAllItemsMask =
    ItemBit(ItemKind::Tumbleweed) | ItemBit(ItemKind::WantedPoster) |
    ItemBit(ItemKind::FryingPan) | ItemBit(ItemKind::Balloon) | ItemBit(ItemKind::PaintCan) |
    ItemBit(ItemKind::Grenade) | ItemBit(ItemKind::SpeedLoader);

struct ItemDef {
    ItemKind kind = ItemKind::None;
    HitLayer layer = HitLayer::Item;
    /// Excluded when the mode's ammo setting is below this. The frying pan
    /// takes a player out of a wave, which with a single shot each would be
    /// the whole round.
    uint8 minAmmo = 0;
    /// Excluded when ammo is unlimited — there is nothing to reload.
    bool bannedOnUnlimitedAmmo = false;
    const char* name = "";
};

const ItemDef* FindItem(ItemKind kind);
const char* ToString(ItemKind kind);
ItemKind ParseItemKind(std::string_view text);

/// Narrows the host's choices to what the ammo setting actually permits.
///
/// Computed on the server and sent to the lobby so the UI and the spawner agree;
/// letting the client work it out is how "I enabled it but it never appears"
/// bugs happen.
uint32 EffectiveItemMask(uint32 requestedMask, uint8 ammoPerWave);

// ---------------------------------------------------------------------------
// Match settings
// ---------------------------------------------------------------------------

struct MatchConfig {
    uint8 rounds = 3;         // 3, 5 or 7
    uint8 paperSizeMin = 2;   // 1..5, cell size; smaller is harder
    uint8 paperSizeMax = 4;
    uint8 ammoPerWave = 3;    // validated against the mode; kUnlimited allowed
    uint8 waveLimit = 10;     // validated against the mode; kUnlimited allowed
    uint32 itemMask = kAllItemsMask;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & rounds & paperSizeMin & paperSizeMax & ammoPerWave & waveLimit & itemMask;
    }
};

inline constexpr uint8 kMinPaperSize = 1;
inline constexpr uint8 kMaxPaperSize = 5;

/// The settings a match uses when nobody picked any: quick match, and the
/// starting state of a freshly created room.
///
/// Per-mode rather than one constant, and the paper size band is the reason.
/// Cell size depends only on the size setting, so the sheet is as wide as
/// cellSize * boardWidth — with one band for everything, tic-tac-toe's
/// three-cell sheet would be a fifth the width of gomoku's fifteen-cell one.
/// Small boards therefore get bigger cells and large boards smaller ones, which
/// keeps the target roughly the same size on screen whatever is being played.
MatchConfig DefaultMatchConfig(GameMode mode);

/// Forces @p config into something playable for @p mode, and reports whether it
/// had to change anything.
///
/// Clamping rather than rejecting: a settings packet arriving a moment after a
/// mode change should not fail, it should land on the nearest legal value.
bool ClampMatchConfig(GameMode mode, MatchConfig& config);

// ---------------------------------------------------------------------------
// Wire payloads
// ---------------------------------------------------------------------------

/// What a shot ended up hitting.
enum class HitKind : uint8 {
    Miss = 0,
    Cell = 1,       // scored a mark on the board
    CellTaken = 2,  // someone else got there first
    Decoy = 3,      // wanted poster: ammo spent, nothing else
    Blocker = 4,    // tumbleweed absorbed it
    Item = 5,
};

enum class StatusFlags : uint8 {
    None = 0,
    /// Cannot fire. Clears at the end of the wave it was applied in.
    Stunned = 1 << 0,
    /// View obscured. Clears at the end of the *following* wave, so it outlasts
    /// a stun and bleeds into the next pass.
    Blinded = 1 << 1,
};

inline StatusFlags operator|(StatusFlags a, StatusFlags b) {
    return static_cast<StatusFlags>(static_cast<uint8>(a) | static_cast<uint8>(b));
}
inline bool HasFlag(StatusFlags value, StatusFlags flag) {
    return (static_cast<uint8>(value) & static_cast<uint8>(flag)) != 0;
}

enum class EntityKind : uint8 {
    None = 0,
    TargetSheet = 1,
    Item = 2,
};

/// One entity's replicated transform. Sent at snapshot rate; clients interpolate.
struct EntityState {
    uint32 entityId = 0;
    EntityKind kind = EntityKind::None;
    ItemKind item = ItemKind::None;  // meaningful when kind == Item
    int32 x = 0;
    int32 y = 0;
    /// Half-extents, so a client can draw and a replay can re-check a hit.
    int32 halfWidth = 0;
    int32 halfHeight = 0;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & entityId & kind & item & x & y & halfWidth & halfHeight;
    }
};

/// Fixed description of the board for a match.
struct BoardSpec {
    uint8 width = 0;
    uint8 height = 0;
    uint8 winLength = 0;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & width & height & winLength;
    }
};

struct PlayerScore {
    SessionId sessionId = kInvalidSessionId;
    uint8 slot = 0;
    uint8 score = 0;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & sessionId & slot & score;
    }
};

}  // namespace nhn::proto
